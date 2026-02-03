#pragma once

#include <Arduino.h>

void initInputMux();
bool readMuxActiveState(uint8_t channel, bool activeLow = true);
