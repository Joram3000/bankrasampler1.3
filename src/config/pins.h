#pragma once
#include <array>

constexpr int SD_CS_PIN    = 5;  // CS
constexpr int SPI_MOSI_PIN = 23; // MOSI 
constexpr int SPI_SCK_PIN  = 18; // SCLK 
constexpr int SPI_MISO_PIN = 19; // MISO 

constexpr int I2S_PIN_BCK  = 14; // Bit Clock
constexpr int I2S_PIN_WS   = 15; // Word Select (LRCLK)
constexpr int I2S_PIN_DATA = 32; // DIN

constexpr int INPUT_MUX_PIN_A = 13;
constexpr int INPUT_MUX_PIN_B = 4;
constexpr int INPUT_MUX_PIN_C = 16;
constexpr int INPUT_MUX_PIN_Y = 17;

constexpr int POT_PIN = 34;
constexpr int SWITCH_PIN_SETTINGS_MODE = 35;

//   constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 7, 2,3, 6, 5, 4};
//   constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 3, 2,4, 5, 6, 7};
//   constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 2, 3,4, 1, 0, 5};
   

constexpr std::array<uint8_t, 6> BUTTON_CHANNEL_ON_MUX = { 5,6,1, 4, 3, 2};

constexpr uint8_t SWITCH_CHANNEL_DELAY_SEND = 0;
constexpr uint8_t SWITCH_CHANNEL_FILTER_ENABLE = 7;
