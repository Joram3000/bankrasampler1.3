#pragma once

#include <Arduino.h>

using MuxChangeCallback = void (*)(uint8_t channel, bool active);

bool readMuxActiveState(uint8_t channel);
void initMuxScanner(uint32_t scanIntervalUs = 5000);
void muxScanTick(); // to be called from main loop
void setMuxChangeCallback(MuxChangeCallback callback);
