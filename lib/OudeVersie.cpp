#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <AudioTools.h>
#include "AudioTools/Disk/AudioSourceSD.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include <ScopeI2SStream.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffects.h"
#include "config.h"
#include "audio_mixer.h"
#include "input.h"
#include "settings_storage.h"

// Audio stack
AudioSourceSD source("/", "wav");
I2SStream i2s;
WAVDecoder wavDecoder;
AudioPlayer player(source, i2s, wavDecoder);
DryWetMixerStream mixerStream;
Delay delayEffect;
DryWetMixerStream* DryWetMixerStream::s_instance = nullptr;

// Display & scope (moved to ui module)
#include "ui.h"
#include "SettingsScreen.h"

#if DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
#include "SettingsScreenAdafruit.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
#include "SettingsScreenU8g2.h"
#endif

// FreeRTOS semaphore helpers for safely drawing to the shared display
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "settings_storage.h"
// State
int activeButtonIndex = -1;
String currentSamplePath = "";
float currentFilterCutoffHz = LOW_PASS_CUTOFF_HZ;
float currentFilterQ = LOW_PASS_Q;
float currentFilterSlewHzPerSec = FILTER_SLEW_DEFAULT_HZ_PER_SEC;
float currentDelayTimeMs = DEFAULT_DELAY_TIME_MS;
float currentDelayDepth = DEFAULT_DELAY_DEPTH;
float currentDelayFeedback = DEFAULT_DELAY_FEEDBACK;
float currentDryMix = MIXER_DEFAULT_DRY_LEVEL;
float currentWetMix = MIXER_DEFAULT_WET_LEVEL;
bool currentCompEnabled = MASTER_COMPRESSOR_ENABLED;
uint16_t currentCompAttackMs = MASTER_COMPRESSOR_ATTACK_MS;
uint16_t currentCompReleaseMs = MASTER_COMPRESSOR_RELEASE_MS;
uint16_t currentCompHoldMs = MASTER_COMPRESSOR_HOLD_MS;
uint8_t currentCompThresholdPercent = MASTER_COMPRESSOR_THRESHOLD_PERCENT;
float currentCompRatio = MASTER_COMPRESSOR_RATIO;

// Settings screen instance (created at runtime after display init)
ISettingsScreen* settingsScreen = nullptr;
static SemaphoreHandle_t displayMutex = nullptr;

enum class OperatingMode { Performance, Settings };
static constexpr OperatingMode kStartupMode = OperatingMode::Performance; // Toggle to Settings to boot into settings mode.
static OperatingMode operatingMode = kStartupMode;
static OperatingMode lastOperatingMode = OperatingMode::Performance;

// Switch state (, one side to GND -> use INPUT_PULLUP; LOW = ON)
bool switchRawState = false;
bool switchDebouncedState = false;
uint32_t switchLastDebounceTime = 0;

// Filter switch state
bool filterSwitchRawState = false;
bool filterSwitchDebouncedState = false;
uint32_t filterSwitchLastDebounceTime = 0;

// Settings mode switch state
bool settingsModeRawState = false;
bool settingsModeDebouncedState = false;
uint32_t settingsModeLastDebounceTime = 0;

Button buttons[BUTTON_COUNT] = {
  Button(BUTTON_CHANNELS[0], "/1.wav", BUTTONS_ACTIVE_LOW, true),
  Button(BUTTON_CHANNELS[1], "/2.wav", BUTTONS_ACTIVE_LOW, true),
  Button(BUTTON_CHANNELS[2], "/3.wav", BUTTONS_ACTIVE_LOW, true),
  Button(BUTTON_CHANNELS[3], "/4.wav", BUTTONS_ACTIVE_LOW, true),
  Button(BUTTON_CHANNELS[4], "/5.wav", BUTTONS_ACTIVE_LOW, true),
  Button(BUTTON_CHANNELS[5], "/6.wav", BUTTONS_ACTIVE_LOW, true),
};

VolumeManager volume(POT_PIN);


// Audio/display init helpers
void initSd() {
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, 80000000UL)) {
    Serial.println("Card failed, or not present");
    while (1);
  }
}

void initDisplay() {
  if (!initUi()) {
    for (;;) ;
  }
}

//check which parts and lines arent used anymore and delete them
void initAudio() {
  auto cfg = scopeI2s.defaultConfig(TX_MODE);
  cfg.pin_bck = I2S_PIN_BCK;
  cfg.pin_ws  = I2S_PIN_WS;
  cfg.pin_data = I2S_PIN_DATA;
  scopeI2s.begin(cfg);
  mixerStream.begin(scopeI2s, delayEffect);
  uint32_t effectiveSampleRate = cfg.sample_rate > 0 ? cfg.sample_rate : 44100;
  AudioInfo mixInfo;
  mixInfo.sample_rate = effectiveSampleRate;
  mixInfo.channels = cfg.channels > 0 ? cfg.channels : 2;
  mixInfo.bits_per_sample = cfg.bits_per_sample > 0 ? cfg.bits_per_sample : 16;
  mixerStream.setAudioInfo(mixInfo);
  mixerStream.updateEffectSampleRate(effectiveSampleRate);
  mixerStream.setMix(currentDryMix, currentWetMix);
  mixerStream.configureMasterCompressor(currentCompAttackMs,
                                        currentCompReleaseMs,
                                        currentCompHoldMs,
                                        currentCompThresholdPercent,
                                        currentCompRatio,
                                        currentCompEnabled);
  mixerStream.setInputLowPassSlewRate(currentFilterSlewHzPerSec);
  delayEffect.setDuration(static_cast<uint32_t>(currentDelayTimeMs));      // milliseconds
  delayEffect.setDepth(currentDelayDepth);       // wet mix ratio handled in mixer
  delayEffect.setFeedback(currentDelayFeedback);    // repeats
  player.setOutput(mixerStream);
  player.setSilenceOnInactive(true);
  player.setAutoNext(false);
  player.setDelayIfOutputFull(0);
  player.setFadeTime(BUTTON_FADE_MS);
  player.begin();
  player.stop();
}

void applyFilterSwitchState(bool enabled) {
  mixerStream.setInputLowPassSlewRate(currentFilterSlewHzPerSec);
  mixerStream.configureMasterLowPass(currentFilterCutoffHz,
                                     currentFilterQ, enabled);
}

// play helper
bool playSampleForButton(size_t idx) {
  if (idx >= BUTTON_COUNT) return false;
  const char* path = buttons[idx].getPath();
  if (path == nullptr || path[0] == '\0') {
    Serial.println("Geen geldig pad om af te spelen");
    return false;
  }
  String full = String(path);
  if (full.charAt(0) != '/') full = String("/") + full;
  if (!player.setPath(full.c_str())) {
    Serial.printf("Kon bestand %s niet openen\n", full.c_str());
    return false;
  }
  currentSamplePath = full;
  player.play();
  // No per-play attack fade: the delay always runs and sending is controlled
  // by the hardware switch via setSendActive().
  activeButtonIndex = (int)idx;
  return true;
}

static void initSettingsScreen() {
  if (settingsScreen) return;
#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  if (auto* display = getU8g2Display()) {
    settingsScreen = new SettingsScreenU8g2(*display);
  } else {
    Serial.println("Settings screen unavailable: no U8G2 display detected");
    return;
  }
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
  if (auto* display = getAdafruitDisplay()) {
    settingsScreen = new SettingsScreenAdafruit(*display);
  } else {
    Serial.println("Settings screen unavailable: no Adafruit SSD1306 display detected");
    return;
  }
#else
  return;
#endif
  settingsScreen->begin();
  settingsScreen->setZoomCallback([](float zoomFactor) {
    setScopeHorizZoom(zoomFactor);
  });
  settingsScreen->setDelayTimeCallback([](float durationMs) {
    currentDelayTimeMs = durationMs;
    delayEffect.setDuration(static_cast<uint32_t>(durationMs));
  });
  settingsScreen->setDelayDepthCallback([](float depth) {
    currentDelayDepth = depth;
    delayEffect.setDepth(depth);
  });
  settingsScreen->setDelayFeedbackCallback([](float feedback) {
    currentDelayFeedback = feedback;
    delayEffect.setFeedback(feedback);
  });
  settingsScreen->setFilterCutoffCallback([](float cutoffHz) {
    currentFilterCutoffHz = cutoffHz;
    applyFilterSwitchState(filterSwitchDebouncedState);
  });
  settingsScreen->setFilterQCallback([](float q) {
    currentFilterQ = q;
    mixerStream.setInputLowPassQ(q);
  });
  settingsScreen->setFilterSlewCallback([](float hzPerSec) {
    currentFilterSlewHzPerSec = hzPerSec;
    mixerStream.setInputLowPassSlewRate(hzPerSec);
  });
  auto applyMix = []() {
    mixerStream.setMix(currentDryMix, currentWetMix);
  };
  settingsScreen->setDryMixCallback([applyMix](float dry) {
    currentDryMix = dry;
    applyMix();
  });
  settingsScreen->setWetMixCallback([applyMix](float wet) {
    currentWetMix = wet;
    applyMix();
  });
  auto rebuildCompressor = []() {
    mixerStream.configureMasterCompressor(currentCompAttackMs,
                                          currentCompReleaseMs,
                                          currentCompHoldMs,
                                          currentCompThresholdPercent,
                                          currentCompRatio,
                                          currentCompEnabled);
  };
  settingsScreen->setCompressorEnabledCallback([rebuildCompressor](bool enabled) {
    currentCompEnabled = enabled;
    rebuildCompressor();
  });
  settingsScreen->setCompressorAttackCallback([rebuildCompressor](float attackMs) {
    currentCompAttackMs = static_cast<uint16_t>(attackMs);
    rebuildCompressor();
  });
  settingsScreen->setCompressorReleaseCallback([rebuildCompressor](float releaseMs) {
    currentCompReleaseMs = static_cast<uint16_t>(releaseMs);
    rebuildCompressor();
  });
  settingsScreen->setCompressorHoldCallback([rebuildCompressor](float holdMs) {
    currentCompHoldMs = static_cast<uint16_t>(holdMs);
    rebuildCompressor();
  });
  settingsScreen->setCompressorThresholdCallback([rebuildCompressor](float thresholdPercent) {
    currentCompThresholdPercent = static_cast<uint8_t>(thresholdPercent);
    rebuildCompressor();
  });
  settingsScreen->setCompressorRatioCallback([rebuildCompressor](float ratio) {
    currentCompRatio = ratio;
    rebuildCompressor();
  });

  settingsScreen->setZoom(DEFAULT_HORIZ_ZOOM);
  settingsScreen->setDelayTimeMs(currentDelayTimeMs);
  settingsScreen->setDelayDepth(currentDelayDepth);
  settingsScreen->setDelayFeedback(currentDelayFeedback);
  settingsScreen->setFilterCutoffHz(currentFilterCutoffHz);
  settingsScreen->setFilterQ(currentFilterQ);
  settingsScreen->setFilterSlewHzPerSec(currentFilterSlewHzPerSec);
  settingsScreen->setDryMix(currentDryMix);
  settingsScreen->setWetMix(currentWetMix);
  settingsScreen->setCompressorEnabled(currentCompEnabled);
  settingsScreen->setCompressorAttackMs(currentCompAttackMs);
  settingsScreen->setCompressorReleaseMs(currentCompReleaseMs);
  settingsScreen->setCompressorHoldMs(currentCompHoldMs);
  settingsScreen->setCompressorThresholdPercent(currentCompThresholdPercent);
  settingsScreen->setCompressorRatio(currentCompRatio);
}

static void releaseAllButtons() {
  activeButtonIndex = -1;
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    buttons[i].release();
  }
}

static void applyOperatingModeChange(OperatingMode newMode) {
  if (newMode == OperatingMode::Settings && !settingsScreen) {
    Serial.println("Settings mode requested but unavailable; reverting to performance mode");
    operatingMode = OperatingMode::Performance;
    newMode = OperatingMode::Performance;
  }
  if (newMode == lastOperatingMode) return;
  if (newMode == OperatingMode::Settings) {
    setScopeDisplaySuspended(true);
    if (settingsScreen) settingsScreen->enter();
    releaseAllButtons();
  } else {
    if (settingsScreen) settingsScreen->exit();
    setScopeDisplaySuspended(false);
    releaseAllButtons();
  saveSettingsToSd(settingsScreen);
  }
  lastOperatingMode = newMode;
}

static void updateSettingsScreenUi() {
  if (!settingsScreen) return;
  if (displayMutex) {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      settingsScreen->update();
      xSemaphoreGive(displayMutex);
    }
  } else {
    settingsScreen->update();
  }
}

// this is where the buttons are mapped to settings screen actions
static void handleSettingsButtonTrigger(size_t buttonIndex) {
  if (!settingsScreen) return;
  ISettingsScreen::Button mapped;
  switch (buttonIndex) {
    case 0: mapped = ISettingsScreen::Button::Back;     break; 
    case 1: mapped = ISettingsScreen::Button::Up;      break;
    case 2: mapped = ISettingsScreen::Button::Ok;      break; 
    case 3: mapped = ISettingsScreen::Button::Left;      break; 
    case 4: mapped = ISettingsScreen::Button::Down;        break;
    case 5: mapped = ISettingsScreen::Button::Right;        break; 
    default: return;
  }
  settingsScreen->onButton(mapped);
}

// Setup & loop (slim)
void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  initInputMux();

  for (size_t i = 0; i < BUTTON_COUNT; ++i) buttons[i].begin();

  // init switch pin
  bool init = readMuxActiveState(SWITCH_CHANNEL_DELAY_SEND, true);
  switchRawState = switchDebouncedState = init;

  bool filterInit = readMuxActiveState(SWITCH_CHANNEL_FILTER_ENABLE, true);
  filterSwitchRawState = filterSwitchDebouncedState = filterInit;

  pinMode(SWITCH_PIN_SETTINGS_MODE, INPUT_PULLUP);
  bool settingsModeInit = (digitalRead(SWITCH_PIN_SETTINGS_MODE) == LOW);
  settingsModeRawState = settingsModeDebouncedState = settingsModeInit;

  initSd();
  initDisplay();
  if (auto mutexPtr = static_cast<SemaphoreHandle_t*>(getDisplayMutex())) {
    displayMutex = *mutexPtr;
  }

  initAudio();
  initSettingsScreen();
  loadSettingsFromSd(settingsScreen);
  if (settingsScreen) {
    setScopeHorizZoom(settingsScreen->getZoom());
  }
  applyOperatingModeChange(operatingMode);

  volume.begin();
  volume.setCutoffUpdateCallback([](float cutoffHz) {
    currentFilterCutoffHz = cutoffHz;
    mixerStream.setInputLowPassCutoff(cutoffHz);
  });
  volume.setFilterControlActive(filterSwitchDebouncedState);
  volume.forceImmediateSample();
  // Keep the effect audible by default, but control whether we send audio
  // into the delay via the hardware switch (setSendActive).
  mixerStream.setEffectActive(true);
  mixerStream.setSendActive(switchDebouncedState);
  applyFilterSwitchState(filterSwitchDebouncedState);
}

void loop() {
  uint32_t now = millis();
  volume.update(now);
  applyOperatingModeChange(operatingMode);

  // Read switch (debounced) - pin wired to GND on one side; LOW = ON
  bool raw = readMuxActiveState(SWITCH_CHANNEL_DELAY_SEND, true);
  if (raw != switchRawState) {
    switchLastDebounceTime = now;
    switchRawState = raw;
  }
  if ((now - switchLastDebounceTime) > BUTTON_DEBOUNCE_MS && raw != switchDebouncedState) {
    switchDebouncedState = raw;
    // Switch now controls whether we send audio into the delay line.
    mixerStream.setSendActive(switchDebouncedState);
  }

  bool filterRaw = readMuxActiveState(SWITCH_CHANNEL_FILTER_ENABLE, true);
  if (filterRaw != filterSwitchRawState) {
    filterSwitchLastDebounceTime = now;
    filterSwitchRawState = filterRaw;
  }
  if ((now - filterSwitchLastDebounceTime) > BUTTON_DEBOUNCE_MS &&
      filterRaw != filterSwitchDebouncedState) {
    filterSwitchDebouncedState = filterRaw;
    applyFilterSwitchState(filterSwitchDebouncedState);
    volume.setFilterControlActive(filterSwitchDebouncedState);
    volume.forceImmediateSample();
  }

  // buttons: update first, then process triggers. This allows detecting
  // multi-button combinations (e.g. all-pressed) before routing events.
  bool triggered[BUTTON_COUNT] = {false};
  for (size_t i = 0; i < BUTTON_COUNT; ++i) {
    triggered[i] = buttons[i].update(now);
  }

  bool suppressButtonHandling = modeToggled;

  if (!suppressButtonHandling) {
    if (operatingMode == OperatingMode::Performance) {
      for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        if (triggered[i]) {
          if (playSampleForButton(i)) {
            // trigger handled
          }
        }
      }
      for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        if (!buttons[i].isLatched() && activeButtonIndex == static_cast<int>(i)) {
          player.stop();
          buttons[i].release();
          activeButtonIndex = -1;
        }
      }
    } else {
      for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        if (triggered[i]) {
          handleSettingsButtonTrigger(i);
        }
      }
      activeButtonIndex = -1;
    }
  } 

  player.copy();
  // When player is inactive, pump a small amount of silence through the
  // mixer so delay/feedback buffers continue to advance and tails decay.
  if (!player.isActive()) {
    mixerStream.pumpSilenceFrames(64);
  }

  if (!player.isActive() && activeButtonIndex >= 0) {
    // sample finished: release latched state so next press works cleanly
    buttons[activeButtonIndex].release();
    activeButtonIndex = -1;
  }



}
