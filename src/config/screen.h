#pragma once

#include <array>

constexpr int DISPLAY_WIDTH  = 128;
constexpr int DISPLAY_HEIGHT = 64;
constexpr int NUM_WAVEFORM_SAMPLES = DISPLAY_WIDTH;
constexpr uint8_t DISPLAY_I2C_ADDRESS = 0x3C;

// I2C clock for the display. A full frame is ~1 KB, so this caps the frame
// rate: 400 kHz (SH1106 spec) ≈ 25–30 ms per frame, 800 kHz ≈ half that.
// Most modules with short wires handle 800 kHz; if the screen shows
// glitches/garbage, go back to 400000.
constexpr uint32_t DISPLAY_I2C_CLOCK_HZ = 800000;

// Target scope frame period. The display task only sleeps for whatever is
// left of this after drawing + sending (at least 2 ms, so lower-priority
// tasks and the idle task on Core 0 always get CPU).
constexpr uint32_t DISPLAY_FRAME_PERIOD_MS = 20; // 50 fps cap

#ifndef DISPLAY_U8G2_CLASS
    #define DISPLAY_U8G2_CLASS U8G2_SH1106_128X64_NONAME_F_HW_I2C
#endif
#ifndef DISPLAY_U8G2_CTOR_ARGS
    #define DISPLAY_U8G2_CTOR_ARGS U8G2_R0, U8X8_PIN_NONE
#endif

