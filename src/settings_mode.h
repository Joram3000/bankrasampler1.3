#pragma once

#include <AudioTools.h>
#include <cstdint>
#include <functional>

class ISettingsScreen;
// deze hoort er toch eigenlijk niet 
enum class OperatingMode { Performance, Settings, Initializing };

struct SettingsUiDependencies {
  audio_tools::Delay* delayEffect = nullptr;
  void* filterEffect = nullptr; // Can be LowPassFilter, HighPassFilter, or BandPassFilter<float>*
  std::function<void()> releaseButtons;
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

// Optional access to the live settings screen.
ISettingsScreen* getSettingsScreen();
