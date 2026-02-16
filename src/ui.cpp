// ui.cpp - display + scope implementation moved out of bankrasampler.cpp
#include "ui.h"
#include "config.h"

#include <AudioTools.h>
#include <Wire.h>
#include <algorithm>
#include "SettingsScreen.h"

#if DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
  #include "SettingsScreenAdafruit.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  #include "SettingsScreenU8g2.h"
#endif

#if DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  #include <ScopeDisplay.h>
#else
  class Adafruit_SSD1306;
#endif

#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
#include <U8g2lib.h>
#include <ScopeDisplayU8g2.h>
#ifndef DISPLAY_U8G2_CLASS
  #define DISPLAY_U8G2_CLASS U8G2_SSD1306_128X64_NONAME_F_HW_I2C
#endif
#ifndef DISPLAY_U8G2_CTOR_ARGS
  #define DISPLAY_U8G2_CTOR_ARGS U8G2_R0, U8X8_PIN_NONE
#endif
#endif

#if DISPLAY_DRIVER != DISPLAY_DRIVER_ADAFRUIT_SSD1306 && DISPLAY_DRIVER != DISPLAY_DRIVER_U8G2_SSD1306
#error "Unsupported DISPLAY_DRIVER selection"
#endif

static int16_t waveformBuffer[NUM_WAVEFORM_SAMPLES];
static int waveformIndex = 0;

#if DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
  static Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
  static ScopeDisplay scopeDisplay(&display, waveformBuffer, &waveformIndex, NUM_WAVEFORM_SAMPLES);
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  static DISPLAY_U8G2_CLASS display(DISPLAY_U8G2_CTOR_ARGS);
  static ScopeDisplayU8g2 scopeDisplay(&display, waveformBuffer, &waveformIndex, NUM_WAVEFORM_SAMPLES);
#endif

ScopeI2SStream scopeI2s(waveformBuffer, &waveformIndex, scopeDisplay.getMutex());

bool initUi() {
#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  Serial.println(F("[UI] Using U8g2 display backend"));
#else
  Serial.println(F("[UI] Using Adafruit SSD1306 backend"));
#endif
  if (!scopeDisplay.begin(DISPLAY_I2C_ADDRESS)) {
    Serial.println(F("Display init failed"));
    return false;
  }
  return true;
}

U8G2* getU8g2Display() {
#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  return &display;
#else
  return nullptr;
#endif
}

Adafruit_SSD1306* getAdafruitDisplay() {
#if DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
  return &display;
#else
  return nullptr;
#endif
}

void* getDisplayMutex() {
  return scopeDisplay.getMutex();
}

void setScopeHorizZoom(float z) {
  scopeDisplay.setHorizZoom(z);
}

void setScopeDisplaySuspended(bool suspended) {
  scopeDisplay.setSuspended(suspended);
}

ISettingsScreen* createSettingsScreen() {
#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
    if (auto* display = getU8g2Display()) {
        auto* s = new SettingsScreenU8g2(*display);
        s->begin();
        return s;
    }
    return nullptr;
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
    if (auto* display = getAdafruitDisplay()) {
        auto* s = new SettingsScreenAdafruit(*display);
        s->begin();
        return s;
    }
    return nullptr;
#else
    return nullptr;
#endif
}



