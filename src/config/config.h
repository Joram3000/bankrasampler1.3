#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "pins.h"

// Runtime debug toggle — controlled from settings screen.
// When true, serial diagnostic prints are active.
// When false, prints are suppressed (reduces CPU overhead / dropout risk).
extern bool debugEnabled;
#define DEBUGMODE debugEnabled

constexpr size_t BUTTON_COUNT = 6;

// Runtime pin configuration — overrides the compile-time defaults in pins.h
// once loaded from /pin_config.txt on the SD card (or set by the wizard).
// Defaults come from pins.h and are copied into these at startup.
extern std::array<uint8_t, BUTTON_COUNT> runtimeButtonChannels;
extern uint8_t runtimeSwitchDelayChannel;
extern uint8_t runtimeSwitchFilterChannel;
extern bool    runtimeMuxActiveLow;

// Keep compile-time constant for code that needs it before setup() runs.
constexpr bool MUX_ACTIVE_LOW_DEFAULT = true;

constexpr const char* SAMPLE_PATHS[] = {
    "/1.wav",
    "/2.wav",
    "/3.wav",
    "/4.wav",
    "/5.wav",
    "/6.wav"
};

constexpr uint32_t BUTTON_FADE_MS = 30;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 4;
constexpr uint32_t BUTTON_RETRIGGER_GUARD_MS = 10;
constexpr uint8_t INPUT_MUX_SETTLE_TIME_US = 4;

// Silence frames pumped per audioTask tick when player is inactive.
// Must be enough to keep the I2S buffer (512 frames) filled within the task period (6 ms).
// 6 ms @ 44100 Hz = ~265 frames. Use 128 as a conservative value — task runs fast enough.
static const size_t kScopeSilenceFramesPerLoop = 128;
  
// Runtime pot polarity — true = inverted (raw = 4095 - analogRead).
// Replaces the old compile-time #define POT_POLARITY_INVERTED.
// Set from pin_config.txt or toggled via the settings screen.
extern bool runtimePotInverted;

constexpr uint32_t POT_READ_INTERVAL_MS = 40;
static const uint32_t SETTINGS_POLL_INTERVAL_MS = 80;
static const uint32_t SETTINGS_DEBOUNCE_MS = 20;

