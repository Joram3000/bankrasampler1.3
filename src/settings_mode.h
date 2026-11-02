#pragma once

#include <AudioTools.h>
#include "audio/prealloc_delay.h"
#include <cstdint>
#include <functional>

enum class OperatingMode { Performance, Settings };

struct SettingsUiDependencies {
  PreallocDelay* delayEffect = nullptr;
  std::function<void()> releaseButtons;
  std::function<void(int)> playSamplePreview;
  // Live settings, pushed on every change (also while loading settings.txt).
  std::function<void(bool)> setOneShot;
  std::function<void(float)> setFilterQ;
  std::function<void(float)> setBtFilterQ;
  uint16_t maxDelayMs = 0; // allocated delay maximum; 0 = no delay (BT mode)
};

// Create and configure the settings screen (UI-only setup).
void initSettingsUi(const SettingsUiDependencies& deps);

// Read and apply the physical settings-mode switch state.
void initSettingsModeSwitch();

void checkSettingsMode(uint32_t now);

// Update settings UI during the loop when active.
void updateSettingsScreenUi();

// Handle a button input when in settings mode.
// Returns true if the input was consumed by the settings UI.
bool handleSettingsButtonInput(size_t buttonIndex, bool active);

// Operating mode accessors for the main loop.
OperatingMode getOperatingMode();

void setOperatingMode(OperatingMode mode);
