#pragma once

#include <array>

constexpr int DISPLAY_WIDTH  = 128;
constexpr int DISPLAY_HEIGHT = 64;
constexpr int NUM_WAVEFORM_SAMPLES = DISPLAY_WIDTH;
constexpr uint8_t DISPLAY_I2C_ADDRESS = 0x3C;

#ifndef DISPLAY_U8G2_CLASS
    #define DISPLAY_U8G2_CLASS U8G2_SH1106_128X64_NONAME_F_HW_I2C
#endif
#ifndef DISPLAY_U8G2_CTOR_ARGS
    #define DISPLAY_U8G2_CTOR_ARGS U8G2_R0, U8X8_PIN_NONE
#endif

