#pragma once

#include <AudioTools.h>
#include "prealloc_delay.h"
#include <cstdint>
#include <functional>

class ISettingsScreen;


enum class OperatingMode { Performance, Settings };

struct SettingsUiDependencies {
  PreallocDelay* delayEffect = nullptr;
  void* filterEffect = nullptr;
  std::function<void()> releaseButtons;
  uint16_t maxDelayMs = 0; // 0 = use compile-time default; set after dynamic allocation
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

// Update the maximum delay time shown/enforced in the settings UI.
// Call after dynamic delay allocation so the screen range matches reality.
void setRuntimeMaxDelayMs(uint16_t ms);

// Operating mode accessors for the main loop.
OperatingMode getOperatingMode();

void setOperatingMode(OperatingMode mode);

// Optional access to the live settings screen.
ISettingsScreen* getSettingsScreen();
