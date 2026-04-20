#pragma once
#include <cstdint>

// Loads /pin_config.txt from SD into the runtime pin config arrays.
// Returns true if the file existed and was parsed successfully.
bool loadPinConfigFromSd();

// Saves the current runtime pin config arrays to /pin_config.txt on SD.
void savePinConfigToSd();

// Returns true if /pin_config.txt exists on the SD card.
bool pinConfigExistsOnSd();
