#include <SPI.h>
#include "soc/rtc_cntl_reg.h"
#include <SD.h>
#include <Wire.h>
#include <AudioTools.h>
#include "AudioTools/Disk/AudioSourceSD.h"
#include "audio_mixer.h"
#include "prealloc_delay.h"
#include "input/button.h"
#include "input/button_wizard.h"
#include "ui.h"
#include "storage/settings_storage.h"
#include "storage/pin_config_storage.h"
#include "settings_mode.h"
#include "input/mux.h"
#include "config/config.h"
#include "config/settings.h"
#include "SettingsScreen.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
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
  // BT audio ringbuffer — 8 KB = ~45 ms at 44100 Hz stereo 16-bit.
  // A2DP bursts are ~4–8 KB every ~20 ms; audioTask drains every 1 ms so
  // 8 KB (2× burst) is ample. Keeping this small leaves heap for the BT
  // stack to complete the A2DP connection handshake (~20–30 KB needed).
  BTRingBuffer btAudioBuffer(8 * 1024);
  static BluetoothA2DPSink a2dp_sink;
  static volatile bool btConnected = false;

  // Lock-free flags from BT callback (Core 0) to audioTask (Core 1).
  // audioTask is the sole owner of the mixer — callbacks must never touch it directly.
  static volatile bool pendingBtConnect    = false;
  static volatile bool pendingBtDisconnect = false;

  // When true, the BT callback writes directly to scopeI2s (I2S + scope capture)
  // instead of the ring buffer. Set true when BT is connected and no sample
  // is playing — follows the AudioTools basic-a2dp-i2s pattern.
  // When a sample plays, this flips to false and BT data goes to the ring buffer
  // so the mixer can combine dry + BT.
  static volatile bool btDirectMode = false;
#endif

// Runtime debug toggle — default on, toggled from settings screen.
bool debugEnabled = true;

// --- Audio system -----------------------------------------------------------
AudioInfo info(44100, 2, 16);
AudioSourceSD source("/", "wav");
WAVDecoder decoder;

DelayMixerStream mixer;
FilteredStream<int16_t, float> filteredStream(mixer, 2);
AudioPlayer player(source, filteredStream, decoder);

static LowPassFilter<float> insertFilterL;
static LowPassFilter<float> insertFilterR;
static PreallocDelay delayEffect1;

// --- Task infrastructure ----------------------------------------------------
// Static stacks — reserved at link time, never fragment the heap at runtime.
static StaticTask_t audioTaskTCB;
static StackType_t  audioTaskStack[8192];

static StaticTask_t inputTaskTCB;
static StackType_t  inputTaskStack[4096];

// Signal that setup() has finished; tasks spin-wait on this before proceeding.
static volatile bool setupDone = false;

// --- Lock-free command channel (inputTask -> audioTask) ---------------------
// Only 32-bit aligned types — safe as volatile on Xtensa/ESP32 without mutex.
static volatile int  pendingPlayIndex = -1;    // >=0 -> start this sample
static volatile bool pendingStop      = false; // true -> begin fade-out

// Fade state lives only in audioTask — never read by other tasks.
static bool  fadingOut      = false;
static int   fadeTicksLeft  = 0;
// BUTTON_FADE_MS=30 -> 30/1 = 30 ticks, +5 for AudioTools ramp drain margin.
static constexpr int FADE_DRAIN_TICKS = BUTTON_FADE_MS + 5;

// --- Runtime state ----------------------------------------------------------
static int      currentSample          = -1;
static uint32_t lastPotRead            = 0;
static float    lastVol                = -1.0f;
static bool     switchDelaySendEnabled = false;
static bool     switchFilterEnabled    = false;
static float    filterCutoff           = LOW_PASS_CUTOFF_HZ;
float           smoothedCutoff         = filterCutoff;

// --- Forward declarations ----------------------------------------------------
void playSample(int index);
void stopSample(int index);
static void initInputControls();
static uint16_t computeMaxDelayMs(bool btActive = false);

// --- Audio task (Core 1, prio 5) --------------------------------------------
// Sole writer to player/mixer. Communicates with inputTask via volatile flags.
static void audioTask(void *) {
    while (!setupDone) vTaskDelay(pdMS_TO_TICKS(10));

    for (;;) {
#ifdef BLUETOOTH_MODE
        // BT connect: free delay buffer to save heap, enter direct mode
        // where the BT callback writes straight to I2S (basic-a2dp-i2s pattern).
        if (pendingBtConnect) {
            pendingBtConnect = false;
            mixer.sendEnabled(false);
            delayEffect1.freeBuffer();
            // Enter direct mode: BT callback writes straight to I2S.
            // No prebuffering needed — the callback delivers data at
            // exactly 44100 Hz, matching I2S consumption perfectly.
            btDirectMode = true;
            if (DEBUGMODE)
                Serial.printf("[AUDIO] BT connected — delay freed, direct mode, heap=%d\n",
                              ESP.getFreeHeap());
        }
        // BT disconnect: restore delay + switch state, exit direct mode.
        // Re-compute the maximum delay now that the BT stack heap is freed —
        // typically allows a much larger buffer than during BT operation.
        if (pendingBtDisconnect) {
            pendingBtDisconnect = false;
            btDirectMode = false;
            btAudioBuffer.clear();
            uint16_t newMaxMs = computeMaxDelayMs(false);
            delayEffect1.reallocate(newMaxMs);
            mixer.sendEnabled(switchDelaySendEnabled);
            // Inform the settings UI so the delay-time slider reflects the new range.
            setRuntimeMaxDelayMs(newMaxMs);
            if (DEBUGMODE)
                Serial.printf("[AUDIO] BT disconnected — delay reallocated max=%u ms, heap=%d\n",
                              newMaxMs, ESP.getFreeHeap());
        }
#endif

        // Command dispatch
        int playIdx = pendingPlayIndex;
        if (playIdx >= 0) {
            pendingPlayIndex = -1;
            pendingStop      = false;
            fadingOut        = false;
#ifdef BLUETOOTH_MODE
            if (btConnected && btDirectMode) {
                // Flip the flag first so the BT callback stops writing to I2S.
                // A memory barrier ensures Core 0 sees the updated flag before
                // audioTask (Core 1) starts writing to I2S via player.copy().
                btDirectMode = false;
                __sync_synchronize();
                // Wait 2 ms so any in-flight scopeI2s.write() on Core 0
                // finishes before audioTask takes over as sole I2S writer.
                // The I2S DMA has ~92 ms of headroom, so this gap is safe.
                vTaskDelay(pdMS_TO_TICKS(2));
            }
#endif
            player.setPath(SAMPLE_PATHS[playIdx]);
            player.setActive(true);
            currentSample = playIdx;
        } else if (pendingStop && !fadingOut) {
            pendingStop    = false;
            fadingOut      = true;
            fadeTicksLeft  = FADE_DRAIN_TICKS;
            player.setActive(false); // starts internal AudioTools fade ramp
            currentSample  = -1;
        }

        bool   active = player.isActive();
        size_t copied = 0;

        if (active || fadingOut) {
            // Drain loop: push audio until I2S DMA is full or no more data.
            // mixer.write() reads btAudioBuffer on every call, so BT is
            // consumed proportional to I2S output.
            for (int i = 0; i < 16; ++i) {
                size_t n = player.copy();
                copied += n;
                if (n == 0) break;
            }
            active = player.isActive();

#ifdef BLUETOOTH_MODE
            // SD-card stall: only drain BT buffer if it's getting full (>4 KB),
            // i.e. a real stall — not just I2S-DMA full (which also returns copied==0).
            if (copied == 0 && btAudioBuffer.available() > 4 * 1024) {
                size_t btFrames = btAudioBuffer.available() / (2 * sizeof(int16_t));
                mixer.pumpSilenceFrames(std::min<size_t>(btFrames, 256u));
            }
#endif

            if (fadingOut) {
                if (fadeTicksLeft > 0) --fadeTicksLeft;
                else fadingOut = false;
            }
        }

        // When idle: pump silence so delay tail drains / scope stays alive.
        if (!active && !fadingOut) {
#ifdef BLUETOOTH_MODE
            if (btConnected) {
                if (!btDirectMode) {
                    // Sample just finished — switch back to direct mode.
                    // BT callback will now write straight to I2S again.
                    btDirectMode = true;
                    // Discard any BT data that accumulated in the ring buffer
                    // during playback so it does not leak into the next sample.
                    btAudioBuffer.clear();
                    if (DEBUGMODE)
                        Serial.println(F("[BT] Sample done, back to direct mode"));
                }
                // In direct mode the BT callback owns I2S — nothing to do here.
            } else {
                mixer.pumpSilenceFrames(kScopeSilenceFramesPerLoop);
            }
#else
            mixer.pumpSilenceFrames(kScopeSilenceFramesPerLoop);
#endif
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

        // Rate control:
        //  Direct BT mode (idle): BT callback drives I2S, audioTask just yields.
        //  Active playback:       player.copy() → I2S blocking governs rate.
        //  No BT / idle:          simple 1 ms yield.
#ifdef BLUETOOTH_MODE
        if (btConnected && btDirectMode) {
            // BT callback handles all I2S output — just yield time.
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
#else
        vTaskDelay(pdMS_TO_TICKS(1));
#endif
    }
}

// --- Input task (Core 0, prio 2) --------------------------------------------
// Polls the mux every 1 ms. Priority below BT stack so A2DP is never starved.
// Never touches player/mixer — uses the lock-free flag channel above.
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

// --- Filter cutoff smoothing -------------------------------------------------
void updateCutoff(float target) {
    smoothedCutoff += 0.1f * (target - smoothedCutoff);

    static float lastQ    = -1.0f;
    static float lastFreq = -1.0f;

    float newQ = LOW_PASS_Q;
    if (auto ss = getSettingsScreen()) newQ = ss->getFilterQ();

    bool qChanged    = fabsf(newQ - lastQ) > 0.05f;
    bool freqChanged = fabsf(smoothedCutoff - lastFreq) > 0.5f;
    if (qChanged || freqChanged) {
        insertFilterL.begin(smoothedCutoff, info.sample_rate, newQ);
        insertFilterR.begin(smoothedCutoff, info.sample_rate, newQ);
        lastQ    = newQ;
        lastFreq = smoothedCutoff;
    }
}

// --- Mux change callback ----------------------------------------------------
static void onMuxChange(uint8_t channel, bool active) {
    if (channel == runtimeSwitchFilterChannel && active != switchFilterEnabled) {
        switchFilterEnabled = active;
        setHudPotValue(lastVol < 0.0f ? 0.0f : lastVol, active);
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
    pendingPlayIndex = index;
    pendingStop      = false;
    if (DEBUGMODE) Serial.printf("[PLAY] %s\n", SAMPLE_PATHS[index]);
}

void stopSample(int index) {
    if (auto ss = getSettingsScreen()) {
        if (ss->getOneShot()) return; // one-shot ends itself
    }
    if (currentSample != index) return;
    pendingStop = true;
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
    // 8 buffers × 512 samples ≈ 92 ms headroom at 44100 Hz.
    // Allows SD-card reads to stall up to ~90 ms without an I2S underrun.
    config.buffer_count = 8;
    config.buffer_size  = 512;

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
// Called after BT init so BT heap consumption is already accounted for.
// Keeps SAFETY_BYTES free for runtime allocations (BT queues, SD lib, stacks).
//
// btActive: true  -> BT stack is running (needs ~20 KB headroom for queues/events)
//           false -> no BT, less headroom needed
static uint16_t computeMaxDelayMs(bool btActive) {
    // With BT running we need more headroom: the BT stack allocates small
    // chunks during connect/disconnect events. Without BT we only need room
    // for SD lib, FreeRTOS housekeeping, and UI — 20 KB is ample.
    // BT audio streaming needs ~30 KB for A2DP decoder + jitter buffers at runtime.
    // Without BT, 15 KB covers SD lib + FreeRTOS housekeeping.
    const uint32_t SAFETY_BYTES = btActive ? (30 * 1024) : (15 * 1024);
    constexpr uint16_t ABS_MIN_MS   = 50;
    const uint16_t ABS_MAX_MS       = btActive ? 300 : 750;

    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap <= SAFETY_BYTES) return ABS_MIN_MS;

    uint32_t usable = freeHeap - SAFETY_BYTES;
    // effect_t is int32_t (4 bytes); 44100 samples/sec
    uint32_t maxMs = (usable / sizeof(int32_t) * 1000UL) / (uint32_t)info.sample_rate;

    if (maxMs < ABS_MIN_MS) return ABS_MIN_MS;
    if (maxMs > ABS_MAX_MS) return ABS_MAX_MS;
    return static_cast<uint16_t>(maxMs);
}

// Allocate the delay line and configure the mixer. Must be called after all
// other heap-consuming init (especially BT) so computeMaxDelayMs() is accurate.
static void initDelayAndMixer(uint16_t maxDelayMs) {
    uint16_t defMs = std::min(maxDelayMs, static_cast<uint16_t>(DEFAULT_DELAY_TIME_MS));

    if (DEBUGMODE)
        Serial.printf("[HEAP] Delay: max=%ums, free=%u before alloc\n",
                      maxDelayMs, ESP.getFreeHeap());

    delayEffect1.begin(info.sample_rate, maxDelayMs, defMs, DEFAULT_DELAY_FEEDBACK);

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

void checkPot(uint32_t now) {
    if ((now - lastPotRead) < POT_READ_INTERVAL_MS) return;
    lastPotRead = now;

    int raw = analogRead(POT_PIN);
    if (runtimePotInverted) raw = 4095 - raw;
    float norm = constrain(raw, 0, 4095) / 4095.0f;

    if (switchFilterEnabled) {
        if (lastVol < 0.0f || fabsf(norm - lastVol) > 0.02f) {
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
    int raw = analogRead(POT_PIN);
    if (runtimePotInverted) raw = 4095 - raw;
    float norm = constrain(raw, 0, 4095) / 4095.0f;

    if (filterState) {
        filterCutoff   = LOW_PASS_MIN_HZ + norm * (LOW_PASS_MAX_HZ - LOW_PASS_MIN_HZ);
        smoothedCutoff = filterCutoff;
        updateCutoff(filterCutoff);
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
static void btAudioDataCallback(const uint8_t *data, uint32_t length) {
    if (btDirectMode) {
        // Direct path (no sample playing): BT callback is the sole I2S writer.
        // Write straight to I2S — no ring buffer involvement so there is no
        // double-write and no unnecessary ring buffer contention.
        scopeI2s.write(data, length);
    } else {
        // Mixer path (sample playing): audioTask owns I2S.
        // Feed ring buffer so the mixer can combine dry + BT audio.
        btAudioBuffer.write(data, length);
    }
}

static void btConnectionStateChanged(esp_a2d_connection_state_t state, void *) {
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        btConnected        = true;
        pendingBtConnect   = true;
        pendingBtDisconnect = false;
        setHudBtConnected(true);

        esp_bd_addr_t *addr = a2dp_sink.get_last_peer_address();
        Serial.printf("[BT] *** CONNECTED *** addr=%s  heap=%d\n",
                      a2dp_sink.to_str(*addr), ESP.getFreeHeap());

    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        btConnected         = false;
        pendingBtDisconnect = true;
        pendingBtConnect    = false;
        setHudBtConnected(false);
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

    Serial.printf("[BT] A2DP sink started as '%s'\n", btName);
}
#endif // BLUETOOTH_MODE

// --- Setup ------------------------------------------------------------------
void setup() {

      WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Brownout detector uitzetten
//   WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // brownout detector uit — BT init stroompiek
  Serial.begin(115200);
   AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Error);
  delay(800);      // rust voor voeding na Serial.begin / power-on
  initSd();
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
        // BT init before delay allocation so computeMaxDelayMs() accounts for BT heap.
        delay(500); // rust voor BT radio init — voorkomt brownout door stroompiek
        initBluetooth();
    } else {
        Serial.println("[BT] Disabled via settings — skipping BT init");
    }
#endif

    // Allocate delay based on remaining heap (after BT if applicable).
#ifdef BLUETOOTH_MODE
    uint16_t dynMaxMs = computeMaxDelayMs(btEnabled);
#else
    uint16_t dynMaxMs = computeMaxDelayMs(false);
#endif
    initDelayAndMixer(dynMaxMs);

    SettingsUiDependencies settingsDeps;
    settingsDeps.delayEffect    = &delayEffect1;
    settingsDeps.filterEffect   = &insertFilterL;
    settingsDeps.releaseButtons = releaseAllButtons;
    settingsDeps.maxDelayMs     = dynMaxMs;
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
    switchFilterEnabled ? updateCutoff(filterCutoff) : updateCutoff(20000.0f);

    // Delay send: only apply via the switch when BT is not connected.
    // When BT connects/disconnects the mixer is updated by audioTask via flags.
#ifdef BLUETOOTH_MODE
    if (!btConnected) {
        mixer.sendEnabled(switchDelaySendEnabled);
    }
#else
    mixer.sendEnabled(switchDelaySendEnabled);
#endif

    checkSettingsMode(now);

    if (getOperatingMode() == OperatingMode::Settings) {
        updateSettingsScreenUi();
    }
}
