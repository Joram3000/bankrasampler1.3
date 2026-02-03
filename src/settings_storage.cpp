#include "settings_storage.h"

#include <Arduino.h>
#include <SD.h>

#include "SettingsScreen.h"

namespace {
constexpr const char* kSettingsPath = "/settings.txt";
}

void loadSettingsFromSd(ISettingsScreen* settingsScreen) {
	if (!settingsScreen) return;
	if (!SD.exists(kSettingsPath)) return;
	File f = SD.open(kSettingsPath, FILE_READ);
	if (!f) {
		Serial.println("Failed to open settings file for read");
		return;
	}

	while (f.available()) {
		String line = f.readStringUntil('\n');
		line.trim();
		if (line.length() == 0) continue;

		if (line.startsWith("zoom=")) {
			float z = line.substring(5).toFloat();
			settingsScreen->setZoom(z);
			Serial.printf("Loaded zoom=%.2f from settings\n", z);
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

	// Defaults if settingsScreen is null
	float z = settingsScreen ? settingsScreen->getZoom() : 1.00f;
	float delayMs = settingsScreen ? settingsScreen->getDelayTimeMs() : 300.0f;
	float delayFb = settingsScreen ? settingsScreen->getDelayFeedback() : 0.8f;
	float filterHz = settingsScreen ? settingsScreen->getFilterCutoffHz() : 7000.0f;
	float filterQ = settingsScreen ? settingsScreen->getFilterQ() : 0.5f;
	bool compEnabled = settingsScreen ? settingsScreen->getCompressorEnabled() : false;

	char buf[128];
	snprintf(buf, sizeof(buf), "zoom=%.2f\n", z);
	f.print(buf);
	snprintf(buf, sizeof(buf), "delay_ms=%.0f\n", delayMs);
	f.print(buf);
	snprintf(buf, sizeof(buf), "delay_fb=%.2f\n", delayFb);
	f.print(buf);
	snprintf(buf, sizeof(buf), "filter_hz=%.0f\n", filterHz);
	f.print(buf);
	snprintf(buf, sizeof(buf), "filter_q=%.2f\n", filterQ);
	f.print(buf);
	snprintf(buf, sizeof(buf), "comp_enabled=%d\n", compEnabled ? 1 : 0);
	f.print(buf);

	f.flush();
	f.close();

	Serial.printf("Saved settings: zoom=%.2f delay_ms=%.0f delay_fb=%.2f filter_hz=%.0f filter_q=%.2f comp_enabled=%d\n",
				  z, delayMs, delayFb, filterHz, filterQ, compEnabled ? 1 : 0);
}
