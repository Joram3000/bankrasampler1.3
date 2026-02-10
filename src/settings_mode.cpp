#include "settings_mode.h"
#include "config/config.h"
#include "config/settings.h"
#include "settings_storage.h"
#include "ui.h"
#include "SettingsScreen.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
ISettingsScreen* settingsScreen = nullptr;
OperatingMode currentMode = OperatingMode::Initializing;

// Persisted settings state (used to seed UI on boot)
float currentFilterQ = LOW_PASS_Q;
float currentDelayTimeMs = DEFAULT_DELAY_TIME_MS;
float currentDelayDepth = DEFAULT_DELAY_DEPTH;
float currentDelayFeedback = DEFAULT_DELAY_FEEDBACK;
bool currentCompEnabled = MASTER_COMPRESSOR_ENABLED;

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
      uiShowSavingOverlay(250);
      saveSettingsToSd(settingsScreen);
    }
  }

  currentMode = newMode;
}

} // namespace

void initSettingsUi(const SettingsUiDependencies& deps) {
  if (settingsScreen) return;
  settingsDeps = deps;

  settingsScreen = createSettingsScreen();
  if (!settingsScreen) {
    Serial.println("Settings screen unavailable");
    return;
  }

  settingsScreen->setZoomCallback([](float zoomFactor) { setScopeHorizZoom(zoomFactor); });
  settingsScreen->setDelayTimeCallback([](float durationMs) {
    currentDelayTimeMs = durationMs;
    if (settingsDeps.delayEffect) {
      settingsDeps.delayEffect->setDuration(static_cast<uint32_t>(durationMs));
    }
  });
  settingsScreen->setDelayFeedbackCallback([](float feedback) {
    currentDelayFeedback = feedback;
    if (settingsDeps.delayEffect) {
      settingsDeps.delayEffect->setFeedback(feedback);
    }
  });

  settingsScreen->setZoom(DEFAULT_HORIZ_ZOOM);
  settingsScreen->setDelayTimeMs(currentDelayTimeMs);
  settingsScreen->setDelayFeedback(currentDelayFeedback);
  settingsScreen->setFilterQ(currentFilterQ);
  settingsScreen->setCompressorEnabled(currentCompEnabled);

  loadSettingsFromSd(settingsScreen);
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
    case 0: mapped = ISettingsScreen::Button::Back; break;
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
  currentMode = mode;
}

ISettingsScreen* getSettingsScreen() {
  return settingsScreen;
}
