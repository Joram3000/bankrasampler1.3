#include "settings_storage.h"

#include <Arduino.h>
#include <SD.h>

#include "SettingsScreen.h"

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
		} else if (line.startsWith("delay_ms=")) {
			float d = line.substring(9).toFloat();
			settingsScreen->setDelayTimeMs(d);
			Serial.printf("Loaded delay_ms=%.0f from settings\n", d);
		} else if (line.startsWith("delay_fb=")) {
			float df = line.substring(9).toFloat();
			settingsScreen->setDelayFeedback(df);
			Serial.printf("Loaded delay_fb=%.2f from settings\n", df);
		} else if (line.startsWith("filter_hz=")) {
			float fh = line.substring(10).toFloat();
			settingsScreen->setFilterCutoffHz(fh);
			Serial.printf("Loaded filter_hz=%.0f from settings\n", fh);
		} else if (line.startsWith("filter_q=")) {
			float fq = line.substring(9).toFloat();
			settingsScreen->setFilterQ(fq);
			Serial.printf("Loaded filter_q=%.2f from settings\n", fq);
		} else if (line.startsWith("comp_enabled=")) {
			String ceStr = line.substring(13);
			ceStr.toLowerCase();
			bool ce = (ceStr == "1" || ceStr == "on" || ceStr == "true");
			settingsScreen->setCompressorEnabled(ce);
			Serial.printf("Loaded comp_enabled=%s from settings\n", ce ? "true" : "false");
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
	// print all the settings to Serial and to file
	Serial.printf("Saving settings to SD card:\n");
	Serial.printf(" zoom=%f\n", settingsScreen->getZoom());
	f.printf("zoom=%f\n", settingsScreen->getZoom());
	Serial.printf(" delay_ms=%.0f\n", settingsScreen->getDelayTimeMs());
	f.printf("delay_ms=%.0f\n", settingsScreen->getDelayTimeMs());
	Serial.printf(" delay_fb=%.2f\n", settingsScreen->getDelayFeedback());
	f.printf("delay_fb=%.2f\n", settingsScreen->getDelayFeedback());
	Serial.printf(" filter_hz=%.0f\n", settingsScreen->getFilterCutoffHz());
	f.printf("filter_hz=%.0f\n", settingsScreen->getFilterCutoffHz());
	Serial.printf(" filter_q=%.2f\n", settingsScreen->getFilterQ());
	f.printf("filter_q=%.2f\n", settingsScreen->getFilterQ());
	Serial.printf(" comp_enabled=%s\n", settingsScreen->getCompressorEnabled() ? "true" : "false");
	f.printf("comp_enabled=%s\n", settingsScreen->getCompressorEnabled() ? "true" : "false");
	Serial.println("Settings saved.");


	f.flush();
	f.close();
}
