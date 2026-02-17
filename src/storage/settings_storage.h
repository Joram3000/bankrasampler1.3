#pragma once

class ISettingsScreen;

// Loads persisted settings from the SD card into the provided settings screen.
void loadSettingsFromSd(ISettingsScreen* settingsScreen);

// Saves the current settings from the provided settings screen to the SD card.
void saveSettingsToSd(const ISettingsScreen* settingsScreen);
