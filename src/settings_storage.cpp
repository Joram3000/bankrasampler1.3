#include "settings_storage.h"

#include <Arduino.h>
#include <SD.h>

#include "SettingsScreen.h"


// it would be nice to add downsample
// and make filters switchable
// downsample too
// en debugmode

namespace {
constexpr const char* kSettingsPath = "/settings.txt";
}

void loadSettingsFromSd(ISettingsScreen* settingsScreen) {
	if (!settingsScreen) {
	Serial.println("No settings screen provided, cannot load settings");
	return;
	}
	if (!SD.exists(kSettingsPath)) {
		Serial.println("Settings file does not exist on SD card, using defaults");
		return;
	}
	File f = SD.open(kSettingsPath, FILE_READ);
	if (!f) {
		Serial.println("Failed to open settings file for read");
		return;
	}
	while (f.available()) {
		String line = f.readStringUntil('\n');
		line.trim();
		if (line.startsWith("zoom=")) {
			float z = line.substring(5).toFloat();
			settingsScreen->setZoom(z);
			Serial.printf("Loaded zoom=%f from settings\n", z);
		} else if (line.startsWith("oneshot=")) {
			bool os = line.substring(8).toInt() != 0;
			settingsScreen->setOneShot(os);
			Serial.printf("Loaded oneshot=%d from settings\n", os);
		} else if (line.startsWith("delay_ms=")) {
			float d = line.substring(9).toFloat();
			settingsScreen->setDelayTimeMs(d);
			Serial.printf("Loaded delay_ms=%.0f from settings\n", d);
		} else if (line.startsWith("delay_fb=")) {
			float df = line.substring(9).toFloat();
			settingsScreen->setDelayFeedback(df);
			Serial.printf("Loaded delay_fb=%.2f from settings\n", df);
		} else if (line.startsWith("filter_q=")) {
			float fq = line.substring(9).toFloat();
			settingsScreen->setFilterQ(fq);
			Serial.printf("Loaded filter_q=%.2f from settings\n", fq);
		} 

	}
	f.close();
}

void saveSettingsToSd(const ISettingsScreen* settingsScreen) {
	File f = SD.open(kSettingsPath, FILE_WRITE);
	if (!f) {
		Serial.println("Failed to open settings file for write");
		return;
	}
	Serial.printf("Saving settings to SD card:\n");
	Serial.printf(" zoom=%f\n", settingsScreen->getZoom());
	f.printf("zoom=%f\n", settingsScreen->getZoom());
    Serial.printf(" oneshot=%d\n", settingsScreen->getOneShot() ? 1 : 0);
    f.printf("oneshot=%d\n", settingsScreen->getOneShot() ? 1 : 0);
	Serial.printf(" delay_ms=%.0f\n", settingsScreen->getDelayTimeMs());
	f.printf("delay_ms=%.0f\n", settingsScreen->getDelayTimeMs());
	Serial.printf(" delay_fb=%.2f\n", settingsScreen->getDelayFeedback());
	f.printf("delay_fb=%.2f\n", settingsScreen->getDelayFeedback());
	Serial.printf(" filter_q=%.2f\n", settingsScreen->getFilterQ());
	f.printf("filter_q=%.2f\n", settingsScreen->getFilterQ());
	Serial.println("Settings saved.");


	f.flush();
	f.close();
}
