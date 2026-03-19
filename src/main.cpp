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
  // BT audio ringbuffer — 16 KB = ~90 ms bij 44100 Hz stereo 16-bit.
  // A2DP bursts zijn typisch 4-8 KB elke ~20 ms; 16 KB geeft ruim 4× marge.
  // Groter dan dit riskeert heap fragmentatie die de BT controller verhindert
  // te starten (die heeft ~65 KB aaneengesloten intern DRAM nodig).
  BTRingBuffer btAudioBuffer(16 * 1024);
  static BluetoothA2DPSink a2dp_sink;
  static volatile bool btConnected = false;
#endif

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

// Set by audioTask itself once pendingStop is consumed; counts down fade ticks
// before actually calling player.setActive(false). Stays on audioTask side only.
static bool  fadingOut      = false;
static int   fadeTicksLeft  = 0;
// Number of 1 ms yields to keep copy() running after setActive(false),
// so AudioTools' fade ramp fully drains before switching to silence pump.
// BUTTON_FADE_MS=30 -> 30/1 = 30 ticks, add 5 for margin.
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
        // Command dispatch
        int playIdx = pendingPlayIndex;
        if (playIdx >= 0) {
            pendingPlayIndex = -1;
            pendingStop      = false;
            fadingOut        = false;
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
            // mixer.write() (inside player.copy()) reads btAudioBuffer on every
            // call, so BT is automatically consumed proportional to I2S output.
            for (int i = 0; i < 16; ++i) {
                size_t n = player.copy();
                copied += n;
                if (n == 0) break;
            }
            active = player.isActive();

            if (fadingOut) {
                if (fadeTicksLeft > 0) --fadeTicksLeft;
                else fadingOut = false;
            }
        }

        // When idle: pump silence so delay tail drains and BT audio keeps flowing.
        // mixer.write() reads btAudioBuffer automatically on every call.
        if (!active && !fadingOut) {
#ifdef BLUETOOTH_MODE
            // Drain everything available so BT ring buffer never fills up.
            size_t btFrames = btAudioBuffer.available() / (2 * sizeof(int16_t));
            size_t frames   = std::max<size_t>(kScopeSilenceFramesPerLoop, btFrames);
            frames          = std::min<size_t>(frames, 512u);
            mixer.pumpSilenceFrames(frames);
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

        // Yield 1 ms — short enough that A2DP bursts (~every 10-20 ms) are
        // always consumed well before the 8 KB ring buffer fills up.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// --- Input task (Core 0, prio 2) --------------------------------------------
// Polls the mux every 1 ms. Kept below BT stack priority so A2DP is not
// starved. Never touches player/mixer — uses the lock-free flag channel above.
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
        1,              // prio 1: below BT stack (prio 5) and BT callbacks — never starves A2DP
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
        if (DEBUGMODE) Serial.printf("[MUX] Filter: %s\n", active ? "ON" : "OFF");
        return;
    }

    if (channel == SWITCH_CHANNEL_DELAY_SEND && active != switchDelaySendEnabled) {
        switchDelaySendEnabled = active;
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
    // Signal audioTask via lock-free flag — never block or delay inputTask.
    pendingPlayIndex = index;
    pendingStop      = false;
    if (DEBUGMODE) Serial.printf("[PLAY] %s\n", SAMPLE_PATHS[index]);
}

void stopSample(int index) {
    if (auto ss = getSettingsScreen()) {
        if (ss->getOneShot()) return; // one-shot ends itself
    }
    if (currentSample != index) return;
    // Signal audioTask to fade out. The delay tail drains via silence pump.
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
    // 4 buffers × 512 samples = ~46 ms headroom at 44100 Hz.
    // Prevents I2S underruns when SD reads stall for a few ms.
    config.buffer_count = 4;
    config.buffer_size  = 512;

    if (!scopeI2s.begin(config)) {
        Serial.println(F("[ERROR] I2S init failed"));
    } else {
        scopeI2s.setAudioInfo(info);
    }

    filteredStream.setFilter(0, &insertFilterL);
    filteredStream.setFilter(1, &insertFilterR);

#ifdef BLUETOOTH_MODE
    // In BT mode: shorter delay buffer to leave ~13 KB for BT GATT/SDP queues.
    constexpr uint16_t delayMax = static_cast<uint16_t>(BT_DELAY_TIME_MAX_MS);
    constexpr uint16_t delayDef = static_cast<uint16_t>(BT_DEFAULT_DELAY_TIME_MS);
    if (DEBUGMODE) Serial.printf("[BT] Delay capped at %u ms to save RAM\n", delayMax);
#else
    constexpr uint16_t delayMax = static_cast<uint16_t>(DELAY_TIME_MAX_MS);
    constexpr uint16_t delayDef = static_cast<uint16_t>(DEFAULT_DELAY_TIME_MS);
#endif

    if (DEBUGMODE)
        Serial.printf("[HEAP] Before delay alloc: %d free\n", ESP.getFreeHeap());

    delayEffect1.begin(info.sample_rate, delayMax, delayDef, DEFAULT_DELAY_FEEDBACK);

    if (DEBUGMODE)
        Serial.printf("[HEAP] After delay alloc:  %d free\n", ESP.getFreeHeap());

    mixer.begin(scopeI2s, delayEffect1, info);
    if (DEBUGMODE) Serial.println(F("[INIT] Audio OK"));
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
        filterCutoff = LOW_PASS_MIN_HZ + norm * (LOW_PASS_MAX_HZ - LOW_PASS_MIN_HZ);
    } else {
        if (lastVol < 0.0f || fabsf(norm - lastVol) > 0.01f) {
            lastVol = norm;
            player.setVolume(lastVol);
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
    } else {
        lastVol = norm;
        player.setVolume(lastVol);
    }

    mixer.sendEnabled(delayState);
    lastPotRead = millis(); // prevent immediate re-read in checkPot()
}

#ifdef BLUETOOTH_MODE
// --- Bluetooth callbacks -----------------------------------------------------
static void btAudioDataCallback(const uint8_t *data, uint32_t length) {
    btAudioBuffer.write(data, length);
}

static void btConnectionStateChanged(esp_a2d_connection_state_t state, void *) {
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        btConnected = true;
        Serial.println(F("[BT] Connected"));
    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        btConnected = false;
        Serial.printf("[BT] Disconnected — heap: %d\n", ESP.getFreeHeap());
    }
}

static void initBluetooth() {
    if (DEBUGMODE)
        Serial.printf("[BT] Heap before A2DP start: %d\n", ESP.getFreeHeap());

    btAudioBuffer.begin();

    // Geef de BLE heap vrij — we gebruiken alleen Classic BT (A2DP).
    // Dit maakt ~30 KB aaneengesloten DRAM vrij voor de Classic BT controller.
    // Moet vóór btStart() / a2dp_sink.start() worden aangeroepen.
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

    // output_raw_stream=false → the A2DP library handles decoding internally
    // and delivers raw PCM to the callback.
    // The default m_sample_rate is already 44100 Hz. Log if the phone
    // negotiates a different rate so we can detect samplerate mismatch.
    a2dp_sink.set_stream_reader(btAudioDataCallback, false);
    a2dp_sink.set_sample_rate_callback([](uint16_t rate) {
        Serial.printf("[BT] Negotiated sample rate: %u Hz%s\n", rate,
                      rate != 44100 ? " ← MISMATCH! Expect crackle." : "");
    });
    a2dp_sink.set_on_connection_state_changed(btConnectionStateChanged, nullptr);
    // Disable auto-reconnect — heap fragmentation during reconnect can stall
    // the audio task long enough to cause a crackle burst.
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
    AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Error);

    initDisplay();
    initSd();
    setMuxChangeCallback(onMuxChange);
    initMuxScanner(5000);
    initInputControls();
    initAudio();
    initPlayer();

    SettingsUiDependencies settingsDeps;
    settingsDeps.delayEffect    = &delayEffect1;
    settingsDeps.filterEffect   = &insertFilterL;
    settingsDeps.releaseButtons = releaseAllButtons;
    initSettingsUi(settingsDeps);

    hideSplash();

#ifdef BLUETOOTH_MODE
    initBluetooth();
#endif

    // Always spawn tasks — audioTask drives I2S, inputTask drives mux scanning.
    startAudioAndInputTasks();

    // Release tasks; setup() is now complete.
    setupDone = true;

    if (DEBUGMODE)
        Serial.printf("[INIT] Done — heap: %d free\n", ESP.getFreeHeap());
}

// --- Loop (UI only — Core 1, low priority) ----------------------------------
// Audio lives in audioTask. Mux lives in inputTask. loop() is UI-only.
void loop() {
    uint32_t now = millis();

    checkPot(now);
    switchFilterEnabled ? updateCutoff(filterCutoff) : updateCutoff(20000.0f);
    switchDelaySendEnabled ? mixer.sendEnabled(true) : mixer.sendEnabled(false);
    checkSettingsMode(now);

    if (getOperatingMode() == OperatingMode::Settings) {
        updateSettingsScreenUi();
    }
}
