#pragma once

#include <Arduino.h>

using MuxChangeCallback = void (*)(uint8_t channel, bool active);

// void initInputMux();
// Read the active state for a mux channel. The mux module keeps an internal
// `muxActiveLow` configuration (set by `initMuxScanner`) so callers don't need
// to pass the polarity again.
bool readMuxActiveState(uint8_t channel);
void initMuxScanner(uint32_t scanIntervalUs = 5000, bool activeLow = true);
void muxScanTick();
void setMuxChangeCallback(MuxChangeCallback callback);
