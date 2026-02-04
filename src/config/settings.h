#pragma once

// zoom screen defaults
constexpr float DEFAULT_HORIZ_ZOOM = 0.2f; //>1 = inzoomen (minder samples weergegeven), <1 = uitzoomen
constexpr float DEFAULT_VERT_SCALE = 1.0f; // amplitude schaal factor

constexpr float DEFAULT_DELAY_TIME_MS    = 420.0f;
constexpr float DEFAULT_DELAY_DEPTH      = 0.40f;
constexpr float DEFAULT_DELAY_FEEDBACK   = 0.45f;

constexpr float DELAY_TIME_MIN_MS        = 50.0f;
constexpr float DELAY_TIME_MAX_MS        = 1000.0f;
constexpr float DELAY_TIME_STEP_MS       = 10.0f;

constexpr float DELAY_FEEDBACK_MIN       = 0.0f;
constexpr float DELAY_FEEDBACK_MAX       = 0.95f;
constexpr float DELAY_FEEDBACK_STEP      = 0.02f;

// FILTER SETTINGS
constexpr float LOW_PASS_CUTOFF_HZ = 500.0f;
constexpr float LOW_PASS_Q         = 0.8071f;
constexpr float LOW_PASS_MIN_HZ    = 300.0f;
constexpr float LOW_PASS_MAX_HZ    = 4500.0f;
constexpr float LOW_PASS_STEP_HZ   = 25.0f;

constexpr float LOW_PASS_Q_MIN      = 0.2f;
constexpr float LOW_PASS_Q_MAX      = 2.5f;
constexpr float LOW_PASS_Q_STEP     = 0.05f;

// Master bus compression (gentle glue on final output)
constexpr bool MASTER_COMPRESSOR_ENABLED = true;
