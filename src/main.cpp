#include <SPI.h>
#include "soc/rtc_cntl_reg.h"
#include <SD.h>
#include <Wire.h>
#include <AudioTools.h>
#include "AudioTools/Disk/AudioSourceSD.h"
#include "audio_mixer.h"
#include "prealloc_delay.h"
#include "smooth_lowpass.h"
#include "input/button.h"
#include "input/button_wizard.h"
#include "ui.h"
#include "storage/settings_storage.h"
#include "storage/pin_config_storage.h"
#include "storage/sample_map.h"
#include "settings_mode.h"
#include "input/mux.h"
#include "config/config.h"
#include "config/settings.h"
#include "SettingsScreen.h"
#include "soc/soc.h"
// --- Runtime pin configuration (defaults come from pins.h) ------------------
std::array<uint8_t, BUTTON_COUNT> runtimeButtonChannels = BUTTON_CHANNEL_ON_MUX;
uint8_t runtimeSwitchDelayChannel  = SWITCH_CHANNEL_DELAY_SEND;
uint8_t runtimeSwitchFilterChannel = SWITCH_CHANNEL_FILTER_ENABLE;
bool    runtimeMuxActiveLow        = MUX_ACTIVE_LOW_DEFAULT;
bool    runtimePotInverted         = false;

#ifdef BLUETOOTH_MODE
  #include <BluetoothA2DPSink.h>
  // Runtime toggle — loaded from settings.txt before BT init.
  // false = BT stack never started, full heap available for delay.
  bool btEnabled = DEFAULT_BT_ENABLED;

  // Read bt_enabled from settings.txt early — before BT init.
  static bool loadBtEnabledFromSd() {
    if (!SD.exists("/settings.txt")) return DEFAULT_BT_ENABLED;
    File f = SD.open("/settings.txt", FILE_READ);
    if (!f) return DEFAULT_BT_ENABLED;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.startsWith("bt_enabled=")) {
        f.close();
        return line.substring(11).toInt() != 0;
      }
    }
    f.close();
    return DEFAULT_BT_ENABLED;
  }
  // BT jitter buffer — 20 KB = ~113 ms at 44100 Hz stereo 16-bit.
  // The BT callback only ever writes here; audioTask is the sole I2S writer.
  // Playback starts once 10 KB (~58 ms) is queued: a Pixel 7 was measured
  // leaving gaps > 29 ms between A2DP packets (underrun every 5–10 s with a
  // 5 KB prebuffer), followed by catch-up bursts. Above 16 KB the reader drops
  // single frames to absorb clock drift / SD stalls; the 4 KB above that
  // absorbs a catch-up burst. Heap: ~26 KB was still free at the low point
  // while streaming with a 12 KB buffer, so ~18 KB remains with this one.
  BTRingBuffer btAudioBuffer(20 * 1024, 10 * 1024, 16 * 1024);
  static BluetoothA2DPSink a2dp_sink;
  static volatile bool btConnected = false;
#endif

// Runtime debug toggle — default off, toggled from settings screen / settings.txt.
bool debugEnabled = false;

// --- Audio system -----------------------------------------------------------
AudioInfo info(44100, 2, 16);
AudioSourceSD source("/", "wav");
WAVDecoder decoder;

DelayMixerStream mixer;
FilteredStream<int16_t, float> filteredStream(mixer, 2);
AudioPlayer player(source, filteredStream, decoder);

// Cutoff target while the filter switch is off — high enough to be inaudible,
// and gliding there (instead of bypassing) keeps switching click-free.
static constexpr float FILTER_OPEN_HZ = 20000.0f;
static SmoothedLowPass insertFilter;
static LowPassDF1      insertFilterL(insertFilter.coeffs());
static LowPassDF1      insertFilterR(insertFilter.coeffs());
static PreallocDelay delayEffect1;

// --- Task infrastructure ----------------------------------------------------
// Static stacks — reserved at link time, never fragment the heap at runtime.
static StaticTask_t audioTaskTCB;
static StackType_t  audioTaskStack[8192];

static StaticTask_t inputTaskTCB;
static StackType_t  inputTaskStack[4096];

// Signal that setup() has finished; tasks spin-wait on this before proceeding.
static volatile bool setupDone = false;

// --- Command queue (inputTask / loop / BT callbacks -> audioTask) -----------
// A queue keeps commands ordered: a quick press+release is always seen as
// play-then-stop, and it wakes audioTask immediately instead of after a poll.
enum class AudioCmdType : uint8_t { Play, Stop, BtConnected, BtDisconnected };
struct AudioCmd {
    AudioCmdType type;
    int8_t       index; // button index for Play/Stop, unused otherwise
};
static constexpr UBaseType_t AUDIO_CMD_QUEUE_LEN = 16;
static StaticQueue_t audioCmdQueueStruct;
static uint8_t       audioCmdQueueStorage[AUDIO_CMD_QUEUE_LEN * sizeof(AudioCmd)];
static QueueHandle_t audioCmdQueue = nullptr;

static void sendAudioCmd(AudioCmdType type, int index = -1) {
    if (!audioCmdQueue) return;
    AudioCmd cmd{type, static_cast<int8_t>(index)};
    if (xQueueSend(audioCmdQueue, &cmd, 0) != pdTRUE && DEBUGMODE)
        Serial.println(F("[AUDIO] Command queue full — command dropped"));
}

// Playback + fade state lives only in audioTask — never read by other tasks.
static int   currentSample  = -1;
static bool  fadingOut      = false;
static int   fadeTicksLeft  = 0;
// BUTTON_FADE_MS=30 -> 30/1 = 30 ticks, +5 for AudioTools ramp drain margin.
static constexpr int FADE_DRAIN_TICKS = BUTTON_FADE_MS + 5;

// --- Runtime state ----------------------------------------------------------
static uint32_t lastPotRead            = 0;
static float    lastVol                = -1.0f;
static bool     switchDelaySendEnabled = false;
static bool     switchFilterEnabled    = false;
static float    filterCutoff           = LOW_PASS_CUTOFF_HZ;

// --- Forward declarations ----------------------------------------------------
void playSample(int index);
void stopSample(int index);
static void initInputControls();
static uint16_t computeMaxDelayMs();

// --- Audio task (Core 1, prio 5) --------------------------------------------
// Sole owner of player/mixer and the only task that writes to I2S.
// Other tasks talk to it exclusively through audioCmdQueue.
static void handleAudioCmd(const AudioCmd &cmd) {
    switch (cmd.type) {
    case AudioCmdType::Play:
        fadingOut = false;
        player.setPath(getSamplePathForButton(cmd.index));
        player.setActive(true);
        currentSample = cmd.index;
        break;

    case AudioCmdType::Stop:
        // Release of a button whose sample is no longer the active one.
        if (cmd.index != currentSample) break;
        fadingOut     = true;
        fadeTicksLeft = FADE_DRAIN_TICKS;
        player.setActive(false); // starts internal AudioTools fade ramp
        currentSample = -1;
        break;

#ifdef BLUETOOTH_MODE
    // No delay exists while BT is enabled, so there is nothing to free or
    // reallocate here — just start every session with an empty jitter buffer.
    case AudioCmdType::BtConnected:
    case AudioCmdType::BtDisconnected:
        btAudioBuffer.clear();
        if (DEBUGMODE)
            Serial.printf("[AUDIO] BT %s, heap=%d (min %d)\n",
                          cmd.type == AudioCmdType::BtConnected ? "connected" : "disconnected",
                          ESP.getFreeHeap(), ESP.getMinFreeHeap());
        break;
#else
    default:
        break;
#endif
    }
}

#ifdef BLUETOOTH_MODE
// Periodic jitter-buffer health report — drops mean the buffer overflowed,
// underruns mean it ran dry mid-stream (also logged once when the phone pauses).
// Also reports each new heap low-water mark: the BT stack fails silently
// when it runs out of heap, so this is the number to watch.
static void reportBtBufferStats() {
    static uint32_t lastReport  = 0;
    static uint32_t lastMinHeap = UINT32_MAX;
    uint32_t now = millis();
    if (now - lastReport < 5000) return;
    lastReport = now;
    uint32_t drops     = btAudioBuffer.getAndResetDropCount();
    uint32_t underruns = btAudioBuffer.getAndResetUnderrunCount();
    if (drops || underruns)
        Serial.printf("[BT] Jitter buffer: %u drop(s), %u underrun(s), level=%u B\n",
                      drops, underruns, (unsigned)btAudioBuffer.available());
    uint32_t minHeap = ESP.getMinFreeHeap();
    if (minHeap < lastMinHeap) {
        lastMinHeap = minHeap;
        Serial.printf("[HEAP] New low: %u B free (now %u B)\n", minHeap, ESP.getFreeHeap());
    }
}
#endif

static void audioTask(void *) {
    while (!setupDone) vTaskDelay(pdMS_TO_TICKS(10));

    for (;;) {
        AudioCmd cmd;
        while (xQueueReceive(audioCmdQueue, &cmd, 0) == pdTRUE) handleAudioCmd(cmd);

        bool   active = player.isActive();
        size_t copied = 0;

        if (active || fadingOut) {
            // Drain loop: push audio until I2S DMA is full or no more data.
            // mixer.write() reads btAudioBuffer on every call, so BT is
            // consumed proportional to I2S output.
            for (int i = 0; i < 16; ++i) {
                // Glide the filter cutoff once per block, never mid-block.
                insertFilter.tick(info.sample_rate);
                size_t n = player.copy();
                copied += n;
                if (n == 0) break;
            }
            active = player.isActive();

#ifdef BLUETOOTH_MODE
            // SD-card stall: keep BT flowing once the jitter buffer is close to
            // overflowing — copied == 0 for a single tick alone is not a stall.
            if (copied == 0 && btAudioBuffer.available() > btAudioBuffer.highWater()) {
                size_t btFrames = btAudioBuffer.available() / (2 * sizeof(int16_t));
                mixer.pumpSilenceFrames(std::min<size_t>(btFrames, 256u));
            }
#endif

            if (fadingOut) {
                if (fadeTicksLeft > 0) --fadeTicksLeft;
                else fadingOut = false;
            }
        }

        // When idle: pump silence so the delay tail drains, the scope stays
        // alive and (in BT mode) the BT audio from the jitter buffer is mixed in.
        if (!active && !fadingOut) {
            mixer.pumpSilenceFrames(kScopeSilenceFramesPerLoop);
        }

        // One-shot end detection
        if (currentSample >= 0 && active && copied == 0) {
            if (auto ss = getSettingsScreen()) {
                if (ss->getOneShot()) {
                    fadingOut     = true;
                    fadeTicksLeft = FADE_DRAIN_TICKS;
                    player.setActive(false);
                    currentSample = -1;
                    if (DEBUGMODE) Serial.println(F("[AUDIO] One-shot end"));
                }
            }
        }

#ifdef BLUETOOTH_MODE
        if (DEBUGMODE && btConnected) reportBtBufferStats();
#endif

        // Rate control: blocking I2S writes govern the rate; additionally wait
        // up to 1 ms, but wake immediately when a new command arrives.
        xQueuePeek(audioCmdQueue, &cmd, pdMS_TO_TICKS(1));
    }
}

// --- Input task (Core 0, prio 2) --------------------------------------------
// Polls the mux every 1 ms. Priority below BT stack so A2DP is never starved.
// Never touches player/mixer — uses the command queue above.
static void inputTask(void *) {
    while (!setupDone) vTaskDelay(pdMS_TO_TICKS(10));
    for (;;) {
        muxScanTick();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// --- Helper: start both RTOS tasks ------------------------------------------
static void startAudioAndInputTasks() {
    xTaskCreateStaticPinnedToCore(
        audioTask, "audioTask",
        sizeof(audioTaskStack) / sizeof(StackType_t),
        nullptr,
        5,              // high priority — preempts loop()/UI
        audioTaskStack,
        &audioTaskTCB,
        1               // Core 1 — same core as I2S driver
    );

    xTaskCreateStaticPinnedToCore(
        inputTask, "inputTask",
        sizeof(inputTaskStack) / sizeof(StackType_t),
        nullptr,
        2,              // prio 2: above idle, below BT stack (prio 5)
        inputTaskStack,
        &inputTaskTCB,
        0               // Core 0
    );
}

// --- Filters -----------------------------------------------------------------
// BT mode has no delay, so while BT is connected the delay switch engages a
// low-pass on the BT audio instead. Both filters share the pot cutoff.
static bool btFilterActive() {
#ifdef BLUETOOTH_MODE
    return btConnected && switchDelaySendEnabled;
#else
    return false;
#endif
}

// The pot sets the cutoff while any filter is engaged, otherwise the volume.
static bool potControlsCutoff() {
    return switchFilterEnabled || btFilterActive();
}

// Only sets the targets; audioTask glides each cutoff there and updates the
// coefficients between blocks (see smooth_lowpass.h).
static void updateFilterTargets() {
    auto ss = getSettingsScreen();
    float q = ss ? ss->getFilterQ() : LOW_PASS_Q;
    insertFilter.setTarget(switchFilterEnabled ? filterCutoff : FILTER_OPEN_HZ, q);
#ifdef BLUETOOTH_MODE
    // When off, the BT filter also glides back to a neutral Q so no
    // resonance peak lingers near 20 kHz.
    constexpr float kNeutralQ = 0.7071f;
    float btQ = ss ? ss->getBtFilterQ() : BT_FILTER_Q;
    if (btFilterActive()) mixer.setBtFilterTarget(filterCutoff, btQ);
    else                  mixer.setBtFilterTarget(FILTER_OPEN_HZ, kNeutralQ);
#endif
}

// Keeps the HUD in sync when the pot role or the BT filter state changes
// (switches toggling, BT connecting/disconnecting).
static void updateFilterHud() {
    static int lastRole = -1;
    int role = potControlsCutoff() ? 1 : 0;
    if (role != lastRole) {
        lastRole = role;
        setHudPotValue(lastVol < 0.0f ? 0.0f : lastVol, role == 1);
    }
#ifdef BLUETOOTH_MODE
    setHudBtFilter(btConnected ? (switchDelaySendEnabled ? 1 : 0) : -1);
#endif
}

// --- Mux change callback ----------------------------------------------------
static void onMuxChange(uint8_t channel, bool active) {
    if (channel == runtimeSwitchFilterChannel && active != switchFilterEnabled) {
        switchFilterEnabled = active;
        if (DEBUGMODE) Serial.printf("[MUX] Filter: %s\n", active ? "ON" : "OFF");
        return;
    }

    if (channel == runtimeSwitchDelayChannel && active != switchDelaySendEnabled) {
        switchDelaySendEnabled = active;
        setHudDelayEnabled(active);
        if (DEBUGMODE) Serial.printf("[MUX] Delay send: %s\n", active ? "ON" : "OFF");
        return;
    }

    int index = findButtonIndexForChannel(channel);
    if (index < 0) return;

    if (handleSettingsButtonInput(static_cast<size_t>(index), active)) return;

    if (active) {
        playSample(index);
    } else {
        stopSample(index);
    }

    if (DEBUGMODE) Serial.printf("[MUX] ch=%u active=%d\n", channel, (int)active);
}

// --- Sample playback commands ------------------------------------------------
void playSample(int index) {
    sendAudioCmd(AudioCmdType::Play, index);
    if (DEBUGMODE) Serial.printf("[PLAY] %s\n", getSamplePathForButton(index));
}

void stopSample(int index) {
    if (auto ss = getSettingsScreen()) {
        if (ss->getOneShot()) return; // one-shot ends itself
    }
    // audioTask ignores the stop if this button's sample is no longer playing.
    sendAudioCmd(AudioCmdType::Stop, index);
}

// --- Subsystem init ----------------------------------------------------------
void initPlayer() {
    player.setVolume(1.0f);
    player.setOutput(filteredStream);
    player.setAutoNext(false);
    player.setSilenceOnInactive(false);
    player.setFadeTime(BUTTON_FADE_MS);
    player.begin();
    player.setActive(false);
    if (DEBUGMODE) Serial.println(F("[INIT] Player OK"));
}

// Sets up I2S stream and filter pipeline. Delay + mixer are initialised later
// (initDelayAndMixer) once heap usage from BT and other init is known.
void initAudio() {
    if (DEBUGMODE) {
        Serial.printf("[HEAP] Before audio init: %d free\n", ESP.getFreeHeap());
        Serial.printf("[PSRAM] Size=%d Free=%d\n", ESP.getPsramSize(), ESP.getFreePsram());
    }

    auto config         = scopeI2s.defaultConfig(TX_MODE);
    config.copyFrom(info);
    config.pin_bck      = I2S_PIN_BCK;
    config.pin_ws       = I2S_PIN_WS;
    config.pin_data     = I2S_PIN_DATA;
    config.i2s_format   = I2S_STD_FORMAT;
    // DMA depth = trigger latency and SD-stall headroom (see config.h).
    config.buffer_count = I2S_DMA_BUFFER_COUNT;
    config.buffer_size  = I2S_DMA_BUFFER_FRAMES;

    if (!scopeI2s.begin(config)) {
        Serial.println(F("[ERROR] I2S init failed"));
    } else {
        scopeI2s.setAudioInfo(info);
    }

    filteredStream.setFilter(0, &insertFilterL);
    filteredStream.setFilter(1, &insertFilterR);

    if (DEBUGMODE) Serial.println(F("[INIT] Audio pipeline OK"));
}

// Compute how much delay we can safely allocate given the current free heap.
// Only used without BT (with BT enabled there is no delay at all).
// Keeps SAFETY_BYTES free for runtime allocations (SD lib, FreeRTOS, UI).
static uint16_t computeMaxDelayMs() {
    constexpr uint32_t SAFETY_BYTES = 15 * 1024;
    constexpr uint16_t ABS_MIN_MS   = 50;
    constexpr uint16_t ABS_MAX_MS   = 750;

    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap <= SAFETY_BYTES) return ABS_MIN_MS;

    uint32_t budget = freeHeap - SAFETY_BYTES;
    // The delay vector needs one contiguous block, which can be far smaller
    // than the total free heap.
    constexpr uint32_t BLOCK_MARGIN = 1024;
    uint32_t largestBlock = ESP.getMaxAllocHeap();
    budget = std::min(budget, largestBlock > BLOCK_MARGIN ? largestBlock - BLOCK_MARGIN : 0u);

    uint32_t maxMs = (budget / sizeof(PreallocDelay::effect_t) * 1000UL) / (uint32_t)info.sample_rate;

    if (maxMs < ABS_MIN_MS) return ABS_MIN_MS;
    if (maxMs > ABS_MAX_MS) return ABS_MAX_MS;
    return static_cast<uint16_t>(maxMs);
}

// Allocate the delay line and configure the mixer. Must be called after all
// other heap-consuming init so computeMaxDelayMs() is accurate.
// maxDelayMs == 0 -> no delay buffer at all (BT mode); the mixer bypasses it.
static void initDelayAndMixer(uint16_t maxDelayMs) {
    uint16_t defMs = std::min(maxDelayMs, static_cast<uint16_t>(DEFAULT_DELAY_TIME_MS));

    if (DEBUGMODE)
        Serial.printf("[HEAP] Delay: max=%ums, free=%u before alloc\n",
                      maxDelayMs, ESP.getFreeHeap());

    if (!delayEffect1.begin(info.sample_rate, maxDelayMs, defMs, DEFAULT_DELAY_FEEDBACK)
        && maxDelayMs > 0)
        Serial.printf("[ERROR] Delay alloc of %u ms failed — delay bypassed\n", maxDelayMs);

    if (DEBUGMODE)
        Serial.printf("[HEAP] After delay alloc: %u free\n", ESP.getFreeHeap());

    mixer.begin(scopeI2s, delayEffect1, info);
    mixer.sendEnabled(switchDelaySendEnabled); // apply switch state set by initInputControls
}

void initDisplay() {
    if (!initUi()) {
        Serial.println(F("[ERROR] Display init failed"));
    } else {
        showSplash();
        if (DEBUGMODE) Serial.println(F("[INIT] Display OK"));
    }
}

void initSd() {
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);

    int attempts = 0;
    while (!SD.begin(SD_CS_PIN, SPI, 25000000UL) && attempts < 5) {
        Serial.println(F("[SD] Card failed, retrying..."));
        delay(100);
        ++attempts;
    }
    if (attempts >= 5) {
        Serial.println(F("[ERROR] SD init failed — halting"));
        while (true) vTaskDelay(portMAX_DELAY);
    }
    if (DEBUGMODE) Serial.println(F("[INIT] SD OK"));
}

// Averages a few ADC reads — the ESP32 ADC is noisy, and the filter glide
// makes small steps inaudible, so a finer change threshold is usable.
static float readPotNormalized() {
    constexpr int kReads = 4;
    int sum = 0;
    for (int i = 0; i < kReads; ++i) sum += analogRead(POT_PIN);
    int raw = sum / kReads;
    if (runtimePotInverted) raw = 4095 - raw;
    return constrain(raw, 0, 4095) / 4095.0f;
}

void checkPot(uint32_t now) {
    if ((now - lastPotRead) < POT_READ_INTERVAL_MS) return;
    lastPotRead = now;

    float norm = readPotNormalized();

    if (potControlsCutoff()) {
        if (lastVol < 0.0f || fabsf(norm - lastVol) > 0.01f) {
            lastVol = norm;
            filterCutoff = LOW_PASS_MIN_HZ + norm * (LOW_PASS_MAX_HZ - LOW_PASS_MIN_HZ);
            setHudPotValue(norm, true);
        }
    } else {
        if (lastVol < 0.0f || fabsf(norm - lastVol) > 0.02f) {
            lastVol = norm;
            player.setVolume(lastVol);
            setHudPotValue(norm, false);
        }
    }
}

static void initInputControls() {
    initSettingsModeSwitch();

    bool filterState = readMuxActiveState(runtimeSwitchFilterChannel);
    bool delayState  = readMuxActiveState(runtimeSwitchDelayChannel);

    onMuxChange(runtimeSwitchFilterChannel, filterState);
    onMuxChange(runtimeSwitchDelayChannel, delayState);

    // Bootstrap pot so volume/filter is correct from the very first frame.
    float norm = readPotNormalized();

    if (filterState) {
        filterCutoff = LOW_PASS_MIN_HZ + norm * (LOW_PASS_MAX_HZ - LOW_PASS_MIN_HZ);
        updateFilterTargets(); // first audio tick jumps straight to it
        setHudPotValue(norm, true);
    } else {
        lastVol = norm;
        player.setVolume(lastVol);
        setHudPotValue(norm, false);
    }

    // mixer.sendEnabled() is re-applied in initDelayAndMixer() after mixer.begin().
    // Store the state here so initDelayAndMixer knows what to apply.
    switchDelaySendEnabled = delayState;
    setHudDelayEnabled(delayState);

    lastPotRead = millis();
}

#ifdef BLUETOOTH_MODE
// --- Bluetooth callbacks -----------------------------------------------------
// Runs in the BT stack task (Core 0). Only feeds the jitter buffer — it never
// touches I2S, so it never blocks the BT stack and never races audioTask.
static void btAudioDataCallback(const uint8_t *data, uint32_t length) {
    btAudioBuffer.write(data, length);
}

static void btConnectionStateChanged(esp_a2d_connection_state_t state, void *) {
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        btConnected = true;
        sendAudioCmd(AudioCmdType::BtConnected);
        setHudBtConnected(true);

        esp_bd_addr_t *addr = a2dp_sink.get_last_peer_address();
        Serial.printf("[BT] *** CONNECTED *** addr=%s  heap=%d\n",
                      a2dp_sink.to_str(*addr), ESP.getFreeHeap());

    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        btConnected = false;
        sendAudioCmd(AudioCmdType::BtDisconnected);
        setHudBtConnected(false);
        // Re-enable discoverability so the phone can reconnect without forgetting the device.
        // The BT stack may have left the device non-connectable after the session ended.
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        Serial.printf("[BT] *** DISCONNECTED ***  heap=%d\n", ESP.getFreeHeap());

    } else if (state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
        Serial.printf("[BT] Connecting...  heap=%d\n", ESP.getFreeHeap());

    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTING) {
        Serial.printf("[BT] Disconnecting...  heap=%d\n", ESP.getFreeHeap());
    }
}

static void initBluetooth() {
    if (DEBUGMODE)
        Serial.printf("[BT] Heap before A2DP start: %d\n", ESP.getFreeHeap());

    btAudioBuffer.begin();

    // Release BLE heap — we only use Classic BT (A2DP).
    // Frees ~30 KB of contiguous DRAM for the Classic BT controller.
    // Must be called before btStart() / a2dp_sink.start().
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

    a2dp_sink.set_stream_reader(btAudioDataCallback, false);
    a2dp_sink.set_sample_rate_callback([](uint16_t rate) {
        Serial.printf("[BT] Negotiated sample rate: %u Hz%s\n", rate,
                      rate != 44100 ? " <- MISMATCH! Expect crackle." : "");
    });
    a2dp_sink.set_on_connection_state_changed(btConnectionStateChanged, nullptr);
    // Disable auto-reconnect — heap fragmentation during reconnect can stall
    // the audio task long enough to cause a burst of crackle.
    a2dp_sink.set_auto_reconnect(false);
    // Unique BT name per unit using last 3 bytes of MAC address.
    // Static so the pointer remains valid for the lifetime of the A2DP sink.
    static char btName[20];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(btName, sizeof(btName), "BANKRAKAKA-%02X%02X%02X", mac[3], mac[4], mac[5]);
    a2dp_sink.start(btName);

    if (DEBUGMODE)
        Serial.printf("[BT] Heap after A2DP start: %d\n", ESP.getFreeHeap());

    // A phone that still has us paired while this count is 0 will fail
    // authentication and has to "forget" the device before it can re-pair.
    Serial.printf("[BT] A2DP sink started as '%s', %d bonded device(s)\n",
                  btName, esp_bt_gap_get_bond_device_num());
}
#endif // BLUETOOTH_MODE

// --- Setup ------------------------------------------------------------------
void setup() {

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // brownout detector uit — BT init stroompiek
  Serial.begin(115200);
   AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Error);
  delay(800);      // rust voor voeding na Serial.begin / power-on
  // Before anything that can send commands (mux callback, BT callbacks).
  audioCmdQueue = xQueueCreateStatic(AUDIO_CMD_QUEUE_LEN, sizeof(AudioCmd),
                                     audioCmdQueueStorage, &audioCmdQueueStruct);
  initSd();
  initSampleMap();
  delay(300);
  initDisplay();
  delay(200);

  // Apply button channels from compile-time defaults into runtime button array.
  for (int i = 0; i < (int)BUTTON_COUNT; ++i)
    setButtonChannel(i, runtimeButtonChannels[i]);

  // Load saved pin config from SD, or run the wizard if none exists.
  setMuxChangeCallback(onMuxChange);
  initMuxScanner(5000);
  if (!loadPinConfigFromSd()) {
    runButtonWizard();
  }

  initInputControls();
  delay(200);
  initAudio();
  initPlayer();

#ifdef BLUETOOTH_MODE
    btEnabled = loadBtEnabledFromSd();
    Serial.printf("[BT] bt_enabled=%d (from settings)\n", btEnabled);
    if (btEnabled) {
        delay(500); // rust voor BT radio init — voorkomt brownout door stroompiek
        initBluetooth();
        // The BT stack needs all the heap it can get (its stream setup alone
        // takes ~16 KB), and the few tens of ms of delay that would fit are
        // useless anyway — so BT mode has no delay at all.
        setHudDelayAvailable(false);
    } else {
        Serial.println("[BT] Disabled via settings — skipping BT init");
    }
    uint16_t dynMaxMs = btEnabled ? 0 : computeMaxDelayMs();
#else
    uint16_t dynMaxMs = computeMaxDelayMs();
#endif
    initDelayAndMixer(dynMaxMs);

    SettingsUiDependencies settingsDeps;
    settingsDeps.delayEffect    = &delayEffect1;
    settingsDeps.filterEffect   = &insertFilterL;
    settingsDeps.releaseButtons    = releaseAllButtons;
    settingsDeps.playSamplePreview = [](int idx) { playSample(idx); };
    settingsDeps.maxDelayMs        = dynMaxMs;
    initSettingsUi(settingsDeps);

    hideSplash();

    startAudioAndInputTasks();
    setupDone = true;

    if (DEBUGMODE)
        Serial.printf("[INIT] Done — heap: %d free, delay max: %u ms\n",
                      ESP.getFreeHeap(), dynMaxMs);
}

// --- Loop (UI only — Core 1, low priority) ----------------------------------
// Audio lives in audioTask. Mux lives in inputTask. loop() is UI-only.
void loop() {
    uint32_t now = millis();

    checkPot(now);
    updateFilterTargets();
    updateFilterHud();

    // Without a delay buffer (BT mode) the mixer bypasses the send anyway.
    mixer.sendEnabled(switchDelaySendEnabled);

    checkSettingsMode(now);

    if (getOperatingMode() == OperatingMode::Settings) {
        updateSettingsScreenUi();
    }

    // Yield so loop() does not spin at 100% on Core 1; nothing here needs
    // more than a few hundred updates per second.
    vTaskDelay(pdMS_TO_TICKS(2));
}
