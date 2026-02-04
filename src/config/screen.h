#pragma once

#include <array>

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

// Initialization screen defaults (customize to change startup message and how
// long the init animation should run)
#ifndef INIT_SCREEN_MESSAGE
#define INIT_SCREEN_MESSAGE "Opstarten..."
#endif

#ifndef INIT_SCREEN_DURATION_MS
#define INIT_SCREEN_DURATION_MS 3000
#endif
