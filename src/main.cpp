// main.cpp — boot sequence and UI loop
//
// Tasks: audioTask (Core 1, prio 5) plays samples and is the only I2S writer
// (audio/audio_engine), inputTask (Core 0, prio 2) scans the mux (input/mux),
// and loop() below runs the UI.
#include <SPI.h>
#include <SD.h>
#include <AudioTools.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "audio/audio_engine.h"
#include "audio/bluetooth.h"
#include "battery.h"
#include "controls.h"
#include "diagnostics.h"
#include "input/button.h"
#include "input/button_wizard.h"
#include "input/mux.h"
#include "settings_mode.h"
#include "storage/pin_config_storage.h"
#include "storage/sample_map.h"
#include "config/config.h"
#include "ui.h"

// setup() and loop() run in loopTask, whose stack comes from the heap that
// the BT stack needs. Measured peak ~2.5 KB (setup incl. BT init) of the
// default 8 KB ('d' report).
SET_LOOP_TASK_STACK_SIZE(5 * 1024);

static void initDisplay() {
    if (!initUi()) {
        Serial.println(F("[ERROR] Display init failed"));
    } else {
        showSplash();
        if (DEBUGMODE) Serial.println(F("[INIT] Display OK"));
    }
}

static void initSd() {
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);

    int attempts = 0;
    while (!SD.begin(SD_CS_PIN, SPI, 25000000UL, "/sd", SD_MAX_OPEN_FILES) && attempts < 5) {
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

// --- Setup ------------------------------------------------------------------
void setup() {

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // brownout detector uit — BT init stroompiek
  Serial.begin(115200);
   AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Error);
  delay(800);      // rust voor voeding na Serial.begin / power-on
  initDiagnostics();
  diagMark("start");
  // Before anything that can send commands (mux callback, BT callbacks).
  initAudioCommandQueue();
  initSd();
#ifdef BLUETOOTH_MODE
  // Early: the DMA depth and mux scan rate depend on it (see btRunning()).
  loadBtEnabledFromSd();
#endif
  initSampleMap();
  diagMark("sd");
  delay(300);
  initDisplay();
  diagMark("display");
  delay(200);

  // Apply button channels from compile-time defaults into runtime button array.
  for (int i = 0; i < (int)BUTTON_COUNT; ++i)
    setButtonChannel(i, runtimeButtonChannels[i]);

  // Load saved pin config from SD, or run the wizard if none exists.
  setMuxChangeCallback(onMuxChange);
  initMuxScanner(btRunning() ? MUX_SCAN_INTERVAL_BT_US : MUX_SCAN_INTERVAL_US);
  if (!loadPinConfigFromSd()) {
    runButtonWizard();
  }

  initSettingsModeSwitch();
  initControls();
  delay(200);
  initAudio();
  initPlayer();
  diagMark("audio");

#ifdef BLUETOOTH_MODE
    if (btEnabled) {
        delay(500); // rust voor BT radio init — voorkomt brownout door stroompiek
        initBluetooth();
        diagMark("bluetooth");
        // The BT stack needs all the heap it can get (its stream setup alone
        // takes ~16 KB), and the few tens of ms of delay that would fit are
        // useless anyway — so BT mode has no delay at all.
        setHudDelayAvailable(false);
    } else {
        Serial.println("[BT] Disabled via settings — skipping BT init");
    }
#endif
    uint16_t dynMaxMs = btRunning() ? 0 : computeMaxDelayMs();
    initDelayAndMixer(dynMaxMs, delaySendSwitchOn());
    diagMark("delay");

    SettingsUiDependencies settingsDeps;
    settingsDeps.delayEffect       = &getDelayEffect();
    settingsDeps.releaseButtons    = releaseAllButtons;
    settingsDeps.playSamplePreview = [](int idx) { playSample(idx); };
    settingsDeps.setOneShot        = setOneShotEnabled;
    settingsDeps.setFilterQ        = setFilterQ;
    settingsDeps.setBtFilterQ      = setBtFilterQ;
    settingsDeps.maxDelayMs        = dynMaxMs;
    initSettingsUi(settingsDeps);

    hideSplash();

    // Last: both tasks run straight away and rely on everything above.
    startAudioTask();
    startMuxScanTask();
    diagMark("ready");

    if (DEBUGMODE)
        Serial.printf("[INIT] Done — heap: %d free, delay max: %u ms\n",
                      ESP.getFreeHeap(), dynMaxMs);
}

// --- Loop (UI only — Core 1, low priority) ----------------------------------
// Audio lives in audioTask. Mux lives in inputTask. loop() is UI-only.
void loop() {
    uint32_t now = millis();

    updateControls(now);
    checkBattery(now);
    checkSettingsMode(now);

    if (getOperatingMode() == OperatingMode::Settings) {
        updateSettingsScreenUi();
    }

    pollDiagnostics();

    // Yield so loop() does not spin at 100% on Core 1; nothing here needs
    // more than a few hundred updates per second.
    vTaskDelay(pdMS_TO_TICKS(2));
}
