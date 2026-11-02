// bluetooth.h — A2DP sink that streams phone audio into the mixer
//
// Only built with -DBLUETOOTH_MODE (env:esp32dev-bt). The BT callback only
// ever writes to the jitter buffer (btAudioBuffer, see audio_mixer.h); the
// audio task mixes it in and stays the sole I2S writer.
#pragma once

// BT is decided once at boot (toggling bt_enabled reboots), so the audio and
// input timing can be chosen for it up front. Always false without BT support.
bool btRunning();

#ifdef BLUETOOTH_MODE
// Runtime toggle — loaded from settings.txt before BT init.
// false = BT stack never started, full heap available for delay.
extern bool btEnabled;

// Reads bt_enabled from settings.txt into btEnabled. Needs the SD card, and
// runs before anything that depends on btRunning().
void loadBtEnabledFromSd();

void initBluetooth();
bool btIsConnected();

// Debug: jitter-buffer health and heap low-water mark, at most every 5 s.
void reportBtBufferStats();

// Diagnostics report: connection state and jitter-buffer counters.
void printBtStatus();
#endif
