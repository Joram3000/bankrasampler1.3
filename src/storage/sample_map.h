#pragma once

#include <cstdint>

constexpr int SAMPLE_MAP_MAX  = 48;
constexpr int SAMPLE_NAME_MAX = 48;

// Scan SD root for .wav files and load /sample_map.txt mapping.
// Must be called after SD is initialised.
void initSampleMap();

// Full path suitable for AudioPlayer::setPath (e.g. "/kick.wav").
// The returned pointer is stable for the lifetime of the program.
const char* getSamplePathForButton(int buttonIndex);

int         getAvailableSampleCount();
// Returns pointer into internal static storage — valid for program lifetime.
const char* const* getAvailableSampleNames();

// Update which sample (by index into the available list) a button plays.
void setSampleIndexForButton(int buttonIndex, int sampleIndex);
int  getSampleIndexForButton(int buttonIndex);

// Write current mapping to /sample_map.txt on the SD card.
void saveSampleMap();
