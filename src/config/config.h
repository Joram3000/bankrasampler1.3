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


constexpr uint32_t BUTTON_FADE_MS = 30;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 4;
constexpr uint32_t BUTTON_RETRIGGER_GUARD_MS = 10;
constexpr uint8_t INPUT_MUX_SETTLE_TIME_US = 4;

// I2S DMA depth: trigger latency vs. SD-card stall headroom.
// audioTask keeps the DMA full (silence when idle), so everything queued in it
// plays before a newly triggered sample: this IS the button-to-sound latency.
// It is also how long an SD read may stall during playback before a dropout.
//   8 × 512 frames ≈ 93 ms   6 × 256 ≈ 35 ms   4 × 256 ≈ 23 ms   (@ 44.1 kHz)
// With 6 × 256, BT buffer creep during rapid sample triggering suggested SD
// stalls (file open on each sample start) outlasting the DMA; 8 × 256 (≈46 ms)
// gives that ~11 ms more room.
constexpr int I2S_DMA_BUFFER_COUNT  = 8;
constexpr int I2S_DMA_BUFFER_FRAMES = 256;

// Silence frames pumped per audioTask tick (~1 ms) when the player is inactive.
// 128 frames ≈ 2.9 ms per tick — faster than real time, so the DMA stays full.
static const size_t kScopeSilenceFramesPerLoop = 128;
  
// Runtime pot polarity — true = inverted (raw = 4095 - analogRead).
// Replaces the old compile-time #define POT_POLARITY_INVERTED.
// Set from pin_config.txt or toggled via the settings screen.
extern bool runtimePotInverted;

constexpr uint32_t POT_READ_INTERVAL_MS = 40;
static const uint32_t SETTINGS_POLL_INTERVAL_MS = 80;
static const uint32_t SETTINGS_DEBOUNCE_MS = 20;

