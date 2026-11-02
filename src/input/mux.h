#pragma once

#include <Arduino.h>

using MuxChangeCallback = void (*)(uint8_t channel, bool active);

bool readMuxActiveState(uint8_t channel);
void initMuxScanner(uint32_t scanIntervalUs = 5000);
void muxScanTick(); // called from the input task
void setMuxChangeCallback(MuxChangeCallback callback);
// Starts the input task that runs muxScanTick() (Core 0, prio 2). The change
// callback runs in that task.
void startMuxScanTask();
// bool muxGetChannelState(uint8_t channel);