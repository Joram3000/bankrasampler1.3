#pragma once

#include <array>
#include <cstddef>
#include <cstdint>


#define DEBUGMODE 0

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
constexpr float DEFAULT_HORIZ_ZOOM = 1.0f; //>1 = inzoomen (minder samples weergegeven), <1 = uitzoomen
constexpr float DEFAULT_VERT_SCALE = 2.0f; // amplitude schaal factor

// samplepaths:

constexpr const char* SAMPLE_PATHS[] = {
    "/1.wav",
    "/2.wav",
    "/3.wav",
    "/4.wav",
    "/5.wav",
    "/6.wav"
};
constexpr int NUM_SAMPLES = sizeof(SAMPLE_PATHS) / sizeof(SAMPLE_PATHS[0]);

// pinnen waar de knopjes aan zitten (moeten interne pullups hebben)
constexpr int Button1 = 25;
constexpr int Button2 = 26;
constexpr int Button3 = 33;

constexpr int BUTTON_PINS[] = {Button1, Button2, Button3};

 constexpr uint32_t BUTTON_FADE_MS = 12;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 10;
constexpr uint32_t BUTTON_RETRIGGER_GUARD_MS = 20;


constexpr int POT_PIN = 34;
constexpr bool POT_POLARITY_INVERTED = true;

constexpr int SD_CS_PIN    = 5;  // CS
constexpr int SPI_MOSI_PIN = 23; // MOSI 
constexpr int SPI_SCK_PIN  = 18; // SCLK 
constexpr int SPI_MISO_PIN = 19; // MISO 

constexpr int I2S_PIN_BCK  = 14; // Bit Clock
constexpr int I2S_PIN_WS   = 15; // Word Select (LRCLK)
constexpr int I2S_PIN_DATA = 32; // DIN

