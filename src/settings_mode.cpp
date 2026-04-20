#include "settings_mode.h"
#include "config/config.h"
#include "config/settings.h"
#include "storage/settings_storage.h"
#include "storage/pin_config_storage.h"
#include "ui.h"
#include "SettingsScreen.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <Arduino.h>

namespace {
ISettingsScreen* settingsScreen = nullptr;
OperatingMode currentMode = OperatingMode::Performance;
#ifdef BLUETOOTH_MODE
bool btEnabledAtBoot = DEFAULT_BT_ENABLED;
#endif

// Persisted settings state (used to seed UI on boot)
float currentFilterQ = LOW_PASS_Q;
#ifdef BLUETOOTH_MODE
float currentDelayTimeMs = BT_DEFAULT_DELAY_TIME_MS;
#else
float currentDelayTimeMs = DEFAULT_DELAY_TIME_MS;
#endif
float currentDelayFeedback = DEFAULT_DELAY_FEEDBACK;

// Runtime delay maximum, set once after dynamic allocation.
static uint16_t runtimeMaxDelayMs = 0; // 0 = not yet set

// Settings mode switch state
bool settingsModeRawState = false;
bool settingsModeDebouncedState = false;
uint32_t settingsModeLastDebounceTime = 0;
uint32_t settingsModeLastPoll = 0;

SettingsUiDependencies settingsDeps{};

void applyOperatingModeChange(OperatingMode newMode) {
  if (newMode == OperatingMode::Settings && !settingsScreen) {
    Serial.println("Settings mode requested but unavailable; reverting to performance mode");
    newMode = OperatingMode::Performance;
  }
  if (newMode == currentMode) return;

  if (newMode == OperatingMode::Settings) {
      setScopeDisplaySuspended(true);
      if (settingsScreen) settingsScreen->enter();
      if (settingsDeps.releaseButtons) settingsDeps.releaseButtons();
    } else {
      if (settingsScreen) settingsScreen->exit();
      setScopeDisplaySuspended(false);
      if (settingsDeps.releaseButtons) settingsDeps.releaseButtons();
      if (currentMode == OperatingMode::Settings) {
        saveSettingsToSd(settingsScreen);
#ifdef BLUETOOTH_MODE
        if (settingsScreen && settingsScreen->getBtEnabled() != btEnabledAtBoot) {
          Serial.println("[BT] bt_enabled changed — rebooting...");
          delay(200);
          ESP.restart();
        }
#endif
      }
    }
  

  currentMode = newMode;
}

} // namespace

void initSettingsUi(const SettingsUiDependencies& deps) {
  if (settingsScreen) return;
  settingsDeps = deps;

  // Apply runtime max before creating the screen so the first draw is correct.
  if (deps.maxDelayMs > 0) runtimeMaxDelayMs = deps.maxDelayMs;

  settingsScreen = createSettingsScreen();
  if (!settingsScreen) {
    Serial.println("Settings screen unavailable");
    return;
  }

  // Inform the screen of the runtime delay range immediately.
  if (runtimeMaxDelayMs > 0) settingsScreen->setDelayTimeMax(static_cast<float>(runtimeMaxDelayMs));

  settingsScreen->setZoomCallback([](float zoomFactor) { setScopeHorizZoom(zoomFactor); });
  settingsScreen->setDelayTimeCallback([](float durationMs) {
    const float maxMs = runtimeMaxDelayMs > 0
                      ? static_cast<float>(runtimeMaxDelayMs)
                      : DELAY_TIME_MAX_MS;
    float clamped = durationMs;
    if (clamped < DELAY_TIME_MIN_MS) clamped = DELAY_TIME_MIN_MS;
    if (clamped > maxMs)             clamped = maxMs;
    currentDelayTimeMs = clamped;
    if (settingsDeps.delayEffect) {
      settingsDeps.delayEffect->setDuration(static_cast<uint16_t>(clamped));
    }
  });
  settingsScreen->setDelayFeedbackCallback([](float feedback) {
    currentDelayFeedback = feedback;
    if (settingsDeps.delayEffect) {
      settingsDeps.delayEffect->setFeedback(feedback);
    }
  });
  // Apply filter Q changes from the UI to the live filter effect and persist in local state
  settingsScreen->setFilterQCallback([](float q) {
    // store new Q value; the audio thread (main) will read the live value
    // from the settings screen when updating the filter coefficients
    currentFilterQ = q;
  });

  // Forward feedback filter cutoff changes to the delay effect when available.
  settingsScreen->setFeedbackLowpassCutoffCallback([](float hz) {
    if (settingsDeps.delayEffect) {
      settingsDeps.delayEffect->setFeedbackLowpassCutoff(hz);
    }
  });
  settingsScreen->setFeedbackHighpassCutoffCallback([](float hz) {
    if (settingsDeps.delayEffect) {
      settingsDeps.delayEffect->setFeedbackHighpassCutoff(hz);
    }
  });


  settingsScreen->setDebugModeCallback([](bool on) {
    extern bool debugEnabled;
    debugEnabled = on;
  });

#ifdef BLUETOOTH_MODE
  settingsScreen->setBtEnabledCallback([](bool on) {
    extern bool btEnabled;
    btEnabled = on;
  });
#else
  settingsScreen->setBtEnabledCallback(nullptr);
#endif

  settingsScreen->setZoom(DEFAULT_HORIZ_ZOOM);
  settingsScreen->setDelayTimeMs(currentDelayTimeMs);
  settingsScreen->setDelayFeedback(currentDelayFeedback);
  settingsScreen->setFilterQ(currentFilterQ);
  // seed feedback filter cutoffs from defaults so the UI shows sensible values
  settingsScreen->setFeedbackLowpassCutoff(FB_LOW_PASS_CUTOFF_HZ);
  settingsScreen->setFeedbackHighpassCutoff(FB_HIGH_PASS_CUTOFF_HZ);
  settingsScreen->setDebugMode(true); // default on

  // Seed pot polarity without triggering the save callback.
  settingsScreen->setPotInvertedCallback(nullptr);
  settingsScreen->setPotInverted(runtimePotInverted);
  settingsScreen->setPotInvertedCallback([](bool inv) {
    runtimePotInverted = inv;
    savePinConfigToSd();
  });

  loadSettingsFromSd(settingsScreen);

#ifdef BLUETOOTH_MODE
  // Record the bt_enabled state as it was when the device booted,
  // so we can detect a change and trigger a reboot on settings exit.
  btEnabledAtBoot = settingsScreen->getBtEnabled();
#endif
}

void initSettingsModeSwitch() {
  pinMode(SWITCH_PIN_SETTINGS_MODE, INPUT);
  bool settingsModeInit = (digitalRead(SWITCH_PIN_SETTINGS_MODE) == LOW);
  settingsModeRawState = settingsModeDebouncedState = settingsModeInit;
  applyOperatingModeChange(settingsModeDebouncedState ? OperatingMode::Settings : OperatingMode::Performance);

  if (DEBUGMODE) {
    Serial.print("Settings mode switch initialized to: ");
    Serial.println(settingsModeDebouncedState ? "ON" : "OFF");
  }
}

void checkSettingsMode(uint32_t now) {
  if ((now - settingsModeLastPoll) < SETTINGS_POLL_INTERVAL_MS) return;
  settingsModeLastPoll = now;

  bool raw = (digitalRead(SWITCH_PIN_SETTINGS_MODE) == LOW);
  if (raw != settingsModeRawState) {
    settingsModeRawState = raw;
    settingsModeLastDebounceTime = now;
  }

  if ((now - settingsModeLastDebounceTime) >= SETTINGS_DEBOUNCE_MS && settingsModeDebouncedState != settingsModeRawState) {
    settingsModeDebouncedState = settingsModeRawState;
    applyOperatingModeChange(settingsModeDebouncedState ? OperatingMode::Settings : OperatingMode::Performance);
    if (DEBUGMODE) {
      Serial.print(F("Settings mode -> "));
      Serial.println(settingsModeDebouncedState ? F("ON") : F("OFF"));
    }
  }
}

void updateSettingsScreenUi() {
  if (!settingsScreen) {
    Serial.print("In settings mode loop without settings screen\n");
    return;
  }
  if (auto mutexPtr = static_cast<SemaphoreHandle_t*>(getDisplayMutex())) {
    if (xSemaphoreTake(*mutexPtr, pdMS_TO_TICKS(5)) == pdTRUE) {
      settingsScreen->update();
      xSemaphoreGive(*mutexPtr);
    }
  } else {
    settingsScreen->update();
  }
}

bool handleSettingsButtonInput(size_t buttonIndex, bool active) {
  if (currentMode != OperatingMode::Settings || !active) return false;
  if (!settingsScreen) return true;

  ISettingsScreen::Button mapped;
  switch (buttonIndex) {
    case 0: mapped = ISettingsScreen::Button::Tap; break;
    case 1: mapped = ISettingsScreen::Button::Up; break;
    case 2: mapped = ISettingsScreen::Button::Ok; break;
    case 3: mapped = ISettingsScreen::Button::Left; break;
    case 4: mapped = ISettingsScreen::Button::Down; break;
    case 5: mapped = ISettingsScreen::Button::Right; break;
    default: return true;
  }
  settingsScreen->onButton(mapped);
  return true;
}

OperatingMode getOperatingMode() {
  return currentMode;
}

void setOperatingMode(OperatingMode mode) {
  // Ensure mode transitions run the same enter/exit logic as the physical switch
  applyOperatingModeChange(mode);
}

ISettingsScreen* getSettingsScreen() {
  return settingsScreen;
}

void setRuntimeMaxDelayMs(uint16_t ms) {
  runtimeMaxDelayMs = ms;
  if (settingsScreen) settingsScreen->setDelayTimeMax(static_cast<float>(ms));
}
