#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "pins.h"

#define DEBUGMODE 1

constexpr size_t BUTTON_COUNT = BUTTON_CHANNEL_ON_MUX.size();
constexpr bool MUX_ACTIVE_LOW = true;

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

const int COPIED_ZERO_THRESHOLD = 3; // number of consecutive loops with copied==0
static const size_t kScopeSilenceFramesPerLoop = 64; // number of silence frames to feed per loop when no audio
  
#define POT_POLARITY_INVERTED 1
constexpr uint32_t POT_READ_INTERVAL_MS = 25;
static const uint32_t SETTINGS_POLL_INTERVAL_MS = 100;
static const uint32_t SETTINGS_DEBOUNCE_MS = 25;
constexpr uint8_t INPUT_MUX_SETTLE_TIME_US = 5;

