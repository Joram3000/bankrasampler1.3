// ui.h — display and oscilloscope wrapper
#pragma once

#include <ScopeI2SStream.h>

// Expose the scoped I2S stream so initAudio() can configure it.
extern ScopeI2SStream scopeI2s;

// Initialize display and scope. Returns true on success.
bool initUi();

// Expose the underlying display objects so other modules (e.g. settings
// screens) can draw to the display using the same hardware instance. Each
// accessor returns nullptr when its corresponding backend is not active.
class U8G2; // forward
U8G2* getU8g2Display();
class Adafruit_SSD1306; // forward
Adafruit_SSD1306* getAdafruitDisplay();

// Expose the display mutex used by the scope display task so callers can
// safely take the mutex before drawing directly. Returns nullptr when not
// available.
void* getDisplayMutex();

// Adjust scope drawing parameters from other modules
void setScopeHorizZoom(float z);

// Temporarily pause/resume the scope task when drawing custom overlays.
void setScopeDisplaySuspended(bool suspended);

// Settings screen factory (display-backend dependent).
class ISettingsScreen; // forward
ISettingsScreen* createSettingsScreen();

// Small UI helper for a save overlay.
void uiShowSavingOverlay(uint16_t durationMs);

