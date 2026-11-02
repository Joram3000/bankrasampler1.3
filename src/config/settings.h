#pragma once

#include <cstdint>

// zoom screen defaults
constexpr float DEFAULT_HORIZ_ZOOM = 0.2f; //>1 = inzoomen (minder samples weergegeven), <1 = uitzoomen
constexpr float DEFAULT_VERT_SCALE = 1.0f; // amplitude schaal factor // zou ook naar settings kunnen 

constexpr bool ONE_SHOT_DEFAULT = false;

// delay effect defaults
constexpr float DEFAULT_DELAY_TIME_MS    = 280.0f;
constexpr float DELAY_TIME_MIN_MS        = 50.0f;
constexpr float DELAY_TIME_MAX_MS        = 300.0f;
// Largest delay buffer, however much heap is free (only without BT).
constexpr uint16_t DELAY_TIME_ABS_MAX_MS = 750;

// Default BT enabled state (overridden by settings.txt on SD card).
constexpr bool DEFAULT_BT_ENABLED = true;
constexpr float DELAY_TIME_STEP_MS       = 10.0f;

constexpr float DEFAULT_DELAY_FEEDBACK   = 0.8f;
constexpr float DELAY_FEEDBACK_MIN       = 0.0f;
constexpr float DELAY_FEEDBACK_MAX       = 0.99f;
constexpr float DELAY_FEEDBACK_STEP      = 0.02f;

// low pass filter defaults
constexpr float LOW_PASS_CUTOFF_HZ = 777.0f;  
constexpr float LOW_PASS_MIN_HZ    = 150.0f;
constexpr float LOW_PASS_MAX_HZ    = 4500.0f;

constexpr float LOW_PASS_Q         = 0.5f;
constexpr float LOW_PASS_Q_MIN      = 0.1f;
constexpr float LOW_PASS_Q_MAX      = 1.0f;
constexpr float LOW_PASS_Q_STEP     = 0.05f;

// BT filter (delay switch while BT is connected) — allows real resonance.
// Its output level is compensated by 1/sqrt(Q) above Q=1 (see smooth_lowpass.h).
constexpr float BT_FILTER_Q        = 3.0f;
constexpr float BT_FILTER_Q_MIN    = 0.7f;
constexpr float BT_FILTER_Q_MAX    = 8.0f;
constexpr float BT_FILTER_Q_STEP   = 0.25f;

// dit zit in de feedback chain
// high pass filter defaults
constexpr float FB_HIGH_PASS_CUTOFF_HZ = 800.0f;
constexpr float FB_HIGH_PASS_MIN_HZ    = 150.0f;
constexpr float FB_HIGH_PASS_MAX_HZ    = 4500.0f;

// low pass filter defaults
constexpr float FB_LOW_PASS_CUTOFF_HZ = 6000.0f;
constexpr float FB_LOW_PASS_MIN_HZ    = 150.0f;
constexpr float FB_LOW_PASS_MAX_HZ    = 9000.0f;

