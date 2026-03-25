// ui.h — display and oscilloscope wrapper
#pragma once

#include <ScopeI2SStream.h>

// Expose the scoped I2S stream so initAudio() can configure it.
extern ScopeI2SStream scopeI2s;

// Initialize display and scope. Returns true on success.
bool initUi();

// Expose the U8g2 display object for settings screens and other modules.
class U8G2; // forward
U8G2* getU8g2Display();

// Expose the display mutex used by the scope display task so callers can
// safely take the mutex before drawing directly.
void* getDisplayMutex();

// Adjust scope drawing parameters from other modules
void setScopeHorizZoom(float z);

// Temporarily pause/resume the scope task when drawing custom overlays.
void setScopeDisplaySuspended(bool suspended);

// Settings screen factory.
class ISettingsScreen; // forward
ISettingsScreen* createSettingsScreen();

// Show splash/logo and block scope updates until hideSplash() is called.
void showSplash();
void hideSplash();

// HUD state setters — safe to call from any task; reads happen in display task.
// Delay indicator shown bottom-left; pot value shown bottom-right.
// BT connected message shown top-center when connected.
void setHudDelayEnabled(bool on);
void setHudPotValue(float normalizedValue, bool isFilter);  // 0..1
void setHudBtConnected(bool connected);
