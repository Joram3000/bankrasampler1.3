#pragma once

// zoom screen defaults
constexpr float DEFAULT_HORIZ_ZOOM = 0.2f; //>1 = inzoomen (minder samples weergegeven), <1 = uitzoomen
constexpr float DEFAULT_VERT_SCALE = 1.0f; // amplitude schaal factor // zou ook naar settings kunnen 

constexpr bool ONE_SHOT_DEFAULT = false;

// delay effect defaults
constexpr float DEFAULT_DELAY_TIME_MS    = 280.0f;
constexpr float DELAY_TIME_MIN_MS        = 50.0f;
constexpr float DELAY_TIME_MAX_MS        = 300.0f;

// BT mode: shorter delay to free ~13 KB of heap for the Bluetooth stack
constexpr float BT_DELAY_TIME_MAX_MS    = 150.0f;
constexpr float BT_DEFAULT_DELAY_TIME_MS = 120.0f;
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

// dit zit in de feedback chain
// high pass filter defaults
constexpr float FB_HIGH_PASS_CUTOFF_HZ = 800.0f;
constexpr float FB_HIGH_PASS_MIN_HZ    = 150.0f;
constexpr float FB_HIGH_PASS_MAX_HZ    = 4500.0f;

// low pass filter defaults
constexpr float FB_LOW_PASS_CUTOFF_HZ = 6000.0f;
constexpr float FB_LOW_PASS_MIN_HZ    = 150.0f;
constexpr float FB_LOW_PASS_MAX_HZ    = 9000.0f;

