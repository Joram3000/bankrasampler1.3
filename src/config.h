#pragma once

#include <array>
#include <cstddef>
#include <cstdint>


#define DEBUGMODE 1

// -----------------------------------------------------------------------------
// Display selection & geometry
// -----------------------------------------------------------------------------
#define DISPLAY_DRIVER_ADAFRUIT_SSD1306 0
#define DISPLAY_DRIVER_U8G2_SSD1306     1
// Pick which display backend to compile (see ui.cpp for usage)
#define DISPLAY_DRIVER DISPLAY_DRIVER_U8G2_SSD1306

constexpr int DISPLAY_WIDTH  = 128;
constexpr int DISPLAY_HEIGHT = 64;
constexpr int NUM_WAVEFORM_SAMPLES = DISPLAY_WIDTH;
constexpr uint8_t DISPLAY_I2C_ADDRESS = 0x3C;
constexpr bool DISPLAY_INVERT_COLORS = false;

#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
	// Pas deze macro's aan als je een andere U8g2 constructor nodig hebt
	#ifndef DISPLAY_U8G2_CLASS
		// Gebruik de SH1106 constructor voor DIYUSER 1.3 (SH1106) modules.
		// Als je een echte SSD1306-module hebt, zet deze terug naar
		// U8G2_SSD1306_128X64_NONAME_F_HW_I2C
		#define DISPLAY_U8G2_CLASS U8G2_SH1106_128X64_NONAME_F_HW_I2C
	#endif
	#ifndef DISPLAY_U8G2_CTOR_ARGS
		#define DISPLAY_U8G2_CTOR_ARGS U8G2_R0, U8X8_PIN_NONE
	#endif
#endif


// zoom screen defaults
constexpr float DEFAULT_HORIZ_ZOOM = 0.1f; //>1 = inzoomen (minder samples weergegeven), <1 = uitzoomen
constexpr float DEFAULT_VERT_SCALE = 1.0f; // amplitude schaal factor

// constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 7, 2,3, 6, 5, 4}; // dit gaat van button 1 tot 6 , wat de muxpin is 
 constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 3, 2,4, 5, 6, 7}; // dit gaat van button 1 tot 6 , wat de muxpin is 
constexpr size_t BUTTON_COUNT = BUTTON_CHANNEL_ON_MUX.size();

constexpr uint8_t SWITCH_CHANNEL_DELAY_SEND = 1;
constexpr uint8_t SWITCH_CHANNEL_FILTER_ENABLE = 0;

constexpr const char* SAMPLE_PATHS[] = {
    "/1.wav",
    "/2.wav",
    "/3.wav",
    "/4.wav",
    "/5.wav",
    "/6.wav"
};
constexpr bool MUX_ACTIVE_LOW = true;

constexpr uint32_t BUTTON_FADE_MS = 10;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 4;
constexpr uint32_t BUTTON_RETRIGGER_GUARD_MS = 10;
// -----------------------------------------------------------------------------
  const int COPIED_ZERO_THRESHOLD = 3; // number of consecutive loops with copied==0
  static const size_t kScopeSilenceFramesPerLoop = 128; // number of silence frames to feed per loop when no audio


constexpr int POT_PIN = 34;
#define POT_POLARITY_INVERTED 1
constexpr uint32_t POT_READ_INTERVAL_MS = 50; // read volume pot every 50ms

constexpr int SWITCH_PIN_SETTINGS_MODE = 35; // dedicated pin (not muxed)
static const uint32_t SETTINGS_POLL_INTERVAL_MS = 150;
static const uint32_t SETTINGS_DEBOUNCE_MS = 50;

constexpr int SD_CS_PIN    = 5;  // CS
constexpr int SPI_MOSI_PIN = 23; // MOSI 
constexpr int SPI_SCK_PIN  = 18; // SCLK 
constexpr int SPI_MISO_PIN = 19; // MISO 

constexpr int I2S_PIN_BCK  = 14; // Bit Clock
constexpr int I2S_PIN_WS   = 15; // Word Select (LRCLK)
constexpr int I2S_PIN_DATA = 32; // DIN


// SN74HC151 select pins (A = LSB) and shared output
constexpr int INPUT_MUX_PIN_A = 13;
constexpr int INPUT_MUX_PIN_B = 4;
constexpr int INPUT_MUX_PIN_C = 16;
constexpr int INPUT_MUX_PIN_Y = 17;
constexpr uint8_t INPUT_MUX_SETTLE_TIME_US = 5;

// -----------------------------------------------------------------------------
// Audio mixer / delay defaults exposed to UI and storage
// -----------------------------------------------------------------------------
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

// Initialization screen defaults (customize to change startup message and how
// long the init animation should run)
#ifndef INIT_SCREEN_MESSAGE
#define INIT_SCREEN_MESSAGE "Opstarten..."
#endif

#ifndef INIT_SCREEN_DURATION_MS
#define INIT_SCREEN_DURATION_MS 3000
#endif
