// ui.cpp - display + scope implementation moved out of bankrasampler.cpp
#include "ui.h"
#include "config.h"

#include <AudioTools.h>
#include <Wire.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "SettingsScreen.h"
#include "storage/logo.h"

#if DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
  #include "SettingsScreenAdafruit.h"
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  #include <ScopeDisplay.h>

#elif DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  #include "SettingsScreenU8g2.h"
  #include <U8g2lib.h>
  #include <ScopeDisplayU8g2.h>
  #ifndef DISPLAY_U8G2_CLASS
    #define DISPLAY_U8G2_CLASS U8G2_SSD1306_128X64_NONAME_F_HW_I2C
  #endif
  #ifndef DISPLAY_U8G2_CTOR_ARGS
    #define DISPLAY_U8G2_CTOR_ARGS U8G2_R0, U8X8_PIN_NONE
  #endif
#else
  #error "Unsupported DISPLAY_DRIVER selection"
#endif

static int16_t waveformBuffer[NUM_WAVEFORM_SAMPLES];
static int waveformIndex = 0;

// splash params
static float splash_minVel = 3.0f;
static float splash_maxVel = 6.0f;
static uint16_t splash_frameDelayMs = 8;
static float splash_horizontalSpread = 2.5f; // is normaal 


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

void* getDisplayMutex() { return scopeDisplay.getMutex(); }
void setScopeHorizZoom(float z) { scopeDisplay.setHorizZoom(z); }
void setScopeDisplaySuspended(bool suspended) { scopeDisplay.setSuspended(suspended); }

ISettingsScreen* createSettingsScreen() {
#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  if (auto* d = getU8g2Display()) { auto* s = new SettingsScreenU8g2(*d); s->begin(); return s; }
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
  if (auto* d = getAdafruitDisplay()) { auto* s = new SettingsScreenAdafruit(*d); s->begin(); return s; }
#endif
  return nullptr;
}

void showSplash() {
  setScopeDisplaySuspended(true);
#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  if (auto m = scopeDisplay.getMutex()) {
    if (*m != NULL && xSemaphoreTake(*m, pdMS_TO_TICKS(200)) == pdTRUE) {
      display.clearBuffer();
      display.drawXBMP(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, epd_bitmap_bankra_logo);
      display.sendBuffer();
      xSemaphoreGive(*m);
     } 
  } 
  
#endif
  delay(50); 
}

void hideSplash() {
  setScopeDisplaySuspended(true);

  // easing helper
  auto ease = [](float t)->float {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 1.0f - powf(1.0f - t, 3.0f);
  };

  const TickType_t frameDelayTicks = pdMS_TO_TICKS(splash_frameDelayMs);

#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  const int BYTES_PER_ROW = (DISPLAY_WIDTH + 7) / 8;
  auto xbmPixel = [&](const uint8_t *bm, int x, int y)->bool {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return false;
    int idx = y * BYTES_PER_ROW + (x / 8);
    return (bm[idx] >> (x % 8)) & 0x1;
  };

  std::vector<float> offs(DISPLAY_WIDTH, 0.0f), vel(DISPLAY_WIDTH), horizVel(DISPLAY_WIDTH);
  for (int x = 0; x < DISPLAY_WIDTH; ++x) {
    vel[x] = splash_minVel + ((float)random(0,1000)/1000.0f) * (splash_maxVel - splash_minVel);
    horizVel[x] = ((float)random(-1000,1000)/1000.0f) * splash_horizontalSpread;
    if (random(0,100) < 30) offs[x] = -(float)random(0,8);
  }

  bool hasMutex = false;
  if (auto m = scopeDisplay.getMutex()) {
    if (*m != NULL && xSemaphoreTake(*m, pdMS_TO_TICKS(200)) == pdTRUE) hasMutex = true;
  }

  bool running = true;
  while (running) {
    running = false;
    display.clearBuffer();
    for (int x = 0; x < DISPLAY_WIDTH; ++x) {
      float progress = offs[x] / (float)DISPLAY_HEIGHT;
      progress = (progress < 0.0f) ? 0.0f : (progress > 1.0f ? 1.0f : progress);
      int hOffset = (int)roundf(horizVel[x] * ease(progress));
      int dy = (int)offs[x];
      for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
        if (xbmPixel(epd_bitmap_bankra_logo, x, y)) {
          int nx = x + hOffset;
          int ny = y + dy;
          if (nx >= 0 && nx < DISPLAY_WIDTH && ny >= 0 && ny < DISPLAY_HEIGHT) {
            display.drawPixel(nx, ny);
          }
        }
      }
      if (offs[x] < DISPLAY_HEIGHT) {
        offs[x] += vel[x];
        running = running || (offs[x] < DISPLAY_HEIGHT);
      }
    }
    display.sendBuffer();
    if (hasMutex) vTaskDelay(frameDelayTicks); else delay(splash_frameDelayMs);
  }

  if (hasMutex) {
    Serial.println(F("[UI] Splash animation complete, releasing display mutex..."));
    // release if we took it
    if (auto m = scopeDisplay.getMutex()) {
      if (*m != NULL) xSemaphoreGive(*m);
    }
  }

  display.clearBuffer();
  display.sendBuffer();


#endif

  setScopeDisplaySuspended(false);
}



