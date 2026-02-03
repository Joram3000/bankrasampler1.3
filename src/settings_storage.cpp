#include "settings_storage.h"

#include <Arduino.h>
#include <SD.h>

#include "SettingsScreen.h"

namespace {
constexpr const char* kSettingsPath = "/settings.txt";
}

void loadSettingsFromSd(ISettingsScreen* settingsScreen) {
	if (!settingsScreen)
	Serial.println("No settings screen provided, cannot load settings");
	return;
	if (!SD.exists(kSettingsPath)) 
	Serial.println("Settings file does not exist on SD card, using defaults");
	return;
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
	float z = settingsScreen ? settingsScreen->getZoom() : 1.0f;
	char buf[32];
	snprintf(buf, sizeof(buf), "zoom=%.2f\n", z);
	f.print(buf);
	if (settingsScreen) {
		char buf2[64];
		snprintf(buf2, sizeof(buf2), "delay_ms=%.0f\n", settingsScreen->getDelayTimeMs());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "delay_fb=%.2f\n", settingsScreen->getDelayFeedback());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "filter_hz=%.0f\n", settingsScreen->getFilterCutoffHz());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "filter_q=%.2f\n", settingsScreen->getFilterQ());
		f.print(buf2);		
		bool ce = settingsScreen->getCompressorEnabled();
		snprintf(buf2, sizeof(buf2), "comp_enabled=%d\n", ce ? 1 : 0);
		f.print(buf2);
	}
	f.flush();
	f.close();
	Serial.printf("Saved zoom=%.2f to settings\n", z);
}
