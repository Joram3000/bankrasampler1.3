#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <AudioTools.h>
#include "AudioTools/Disk/AudioSourceSD.h"
#include "audio_mixer.h"
#include "prealloc_delay.h"
#include "input/button.h"
#include "ui.h"
#include "storage/settings_storage.h"
#include "settings_mode.h"
#include "input/mux.h"
#include "config/config.h"
#include "config/settings.h"
#include "SettingsScreen.h"

#ifdef BLUETOOTH_MODE
  #include <BluetoothA2DPSink.h>
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
        if (pendingBtDisconnect) {
            pendingBtDisconnect = false;
            btDirectMode = false;
            btAudioBuffer.clear();
            delayEffect1.reallocate();
            mixer.sendEnabled(switchDelaySendEnabled);
            if (DEBUGMODE)
                Serial.printf("[AUDIO] BT disconnected — delay reallocated, heap=%d\n",
                              ESP.getFreeHeap());
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
                // Switch from direct I2S to ring-buffer mode so the mixer
                // can combine sample audio + BT audio together.
                // Ring buffer is already warm (callback always writes to it,
                // idle drain keeps it fresh) — no gap in BT audio.
                btDirectMode = false;
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
                    // Ring-buffer mode but no sample — switch back to direct.
                    // This happens when a sample finishes while BT is connected.
                    btDirectMode = true;
                    if (DEBUGMODE)
                        Serial.println(F("[BT] Sample done, back to direct mode"));
                }
                // Direct mode: BT callback writes to I2S at 44100 Hz.
                // Drain the ring buffer to prevent it filling up (callback
                // always writes to it). This keeps it "warm" with recent data
                // so the transition to mixing mode is seamless.
                btAudioBuffer.clear();
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
    if (channel == SWITCH_CHANNEL_FILTER_ENABLE && active != switchFilterEnabled) {
        switchFilterEnabled = active;
        setHudPotValue(lastVol < 0.0f ? 0.0f : lastVol, active);
        if (DEBUGMODE) Serial.printf("[MUX] Filter: %s\n", active ? "ON" : "OFF");
        return;
    }

    if (channel == SWITCH_CHANNEL_DELAY_SEND && active != switchDelaySendEnabled) {
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
static uint16_t computeMaxDelayMs() {
    constexpr uint32_t SAFETY_BYTES = 45 * 1024; // keep 45 KB free after alloc
    constexpr uint16_t ABS_MIN_MS   = 50;
    constexpr uint16_t ABS_MAX_MS   = 600;

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
    while (!SD.begin(SD_CS_PIN, SPI, 80000000UL) && attempts < 5) {
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
#ifdef POT_POLARITY_INVERTED
    raw = 4095 - raw;
#endif
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

    bool filterState = readMuxActiveState(SWITCH_CHANNEL_FILTER_ENABLE);
    bool delayState  = readMuxActiveState(SWITCH_CHANNEL_DELAY_SEND);

    onMuxChange(SWITCH_CHANNEL_FILTER_ENABLE, filterState);
    onMuxChange(SWITCH_CHANNEL_DELAY_SEND, delayState);

    // Bootstrap pot so volume/filter is correct from the very first frame.
    int raw = analogRead(POT_PIN);
#ifdef POT_POLARITY_INVERTED
    raw = 4095 - raw;
#endif
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
    // Always feed ring buffer so it stays "warm" with recent data.
    // This ensures smooth transitions when a sample starts playing —
    // the mixer can read from a pre-filled buffer without a BT audio gap.
    btAudioBuffer.write(data, length);

    if (btDirectMode) {
        // Direct path (no sample playing): also write straight to I2S + scope.
        // This is the AudioTools recommended approach (basic-a2dp-i2s.ino).
        // The BT stack delivers data at exactly 44100 Hz stereo 16-bit,
        // so I2S DMA consumption matches perfectly — no rate control needed.
        scopeI2s.write(data, length);
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
    a2dp_sink.start("BANKRAKAKA");

    if (DEBUGMODE)
        Serial.printf("[BT] Heap after A2DP start: %d\n", ESP.getFreeHeap());

    Serial.println(F("[BT] A2DP sink started as 'BANKRAKAKA'"));
}
#endif // BLUETOOTH_MODE

// --- Setup ------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

    initDisplay();
    initSd();
    setMuxChangeCallback(onMuxChange);
    initMuxScanner(5000);
    initAudio();
    initInputControls();
    initPlayer();

#ifdef BLUETOOTH_MODE
    // BT init before delay allocation so computeMaxDelayMs() accounts for BT heap.
    initBluetooth();
#endif

    // Allocate delay based on remaining heap (after BT if applicable).
    uint16_t dynMaxMs = computeMaxDelayMs();
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
