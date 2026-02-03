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
		if (line.startsWith("zoom=")) {
			float z = line.substring(5).toFloat();
			settingsScreen->setZoom(z);
			Serial.printf("Loaded zoom=%f from settings\n", z);
		} else if (line.startsWith("delay_ms=")) {
			float d = line.substring(9).toFloat();
			settingsScreen->setDelayTimeMs(d);
			Serial.printf("Loaded delay_ms=%.0f from settings\n", d);
		} else if (line.startsWith("delay_depth=")) {
			float dd = line.substring(12).toFloat();
			settingsScreen->setDelayDepth(dd);
			Serial.printf("Loaded delay_depth=%.2f from settings\n", dd);
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
		} else if (line.startsWith("filter_slew=")) {
			float fs = line.substring(12).toFloat();
			settingsScreen->setFilterSlewHzPerSec(fs);
			Serial.printf("Loaded filter_slew=%.0f from settings\n", fs);
		} else if (line.startsWith("dry_mix=")) {
			float dm = line.substring(8).toFloat();
			settingsScreen->setDryMix(dm);
			Serial.printf("Loaded dry_mix=%.2f from settings\n", dm);
		} else if (line.startsWith("wet_mix=")) {
			float wm = line.substring(8).toFloat();
			settingsScreen->setWetMix(wm);
			Serial.printf("Loaded wet_mix=%.2f from settings\n", wm);
		} else if (line.startsWith("comp_attack=")) {
			float ca = line.substring(12).toFloat();
			settingsScreen->setCompressorAttackMs(ca);
			Serial.printf("Loaded comp_attack=%.0f from settings\n", ca);
		} else if (line.startsWith("comp_release=")) {
			float cr = line.substring(13).toFloat();
			settingsScreen->setCompressorReleaseMs(cr);
			Serial.printf("Loaded comp_release=%.0f from settings\n", cr);
		} else if (line.startsWith("comp_hold=")) {
			float ch = line.substring(10).toFloat();
			settingsScreen->setCompressorHoldMs(ch);
			Serial.printf("Loaded comp_hold=%.0f from settings\n", ch);
		} else if (line.startsWith("comp_threshold=")) {
			float ct = line.substring(15).toFloat();
			settingsScreen->setCompressorThresholdPercent(ct);
			Serial.printf("Loaded comp_threshold=%.0f from settings\n", ct);
		} else if (line.startsWith("comp_ratio=")) {
			float crat = line.substring(11).toFloat();
			settingsScreen->setCompressorRatio(crat);
			Serial.printf("Loaded comp_ratio=%.2f from settings\n", crat);
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
		snprintf(buf2, sizeof(buf2), "delay_depth=%.2f\n", settingsScreen->getDelayDepth());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "delay_fb=%.2f\n", settingsScreen->getDelayFeedback());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "filter_hz=%.0f\n", settingsScreen->getFilterCutoffHz());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "filter_q=%.2f\n", settingsScreen->getFilterQ());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "filter_slew=%.0f\n", settingsScreen->getFilterSlewHzPerSec());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "dry_mix=%.2f\n", settingsScreen->getDryMix());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "wet_mix=%.2f\n", settingsScreen->getWetMix());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "comp_attack=%.0f\n", settingsScreen->getCompressorAttackMs());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "comp_release=%.0f\n", settingsScreen->getCompressorReleaseMs());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "comp_hold=%.0f\n", settingsScreen->getCompressorHoldMs());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "comp_threshold=%.0f\n", settingsScreen->getCompressorThresholdPercent());
		f.print(buf2);
		snprintf(buf2, sizeof(buf2), "comp_ratio=%.2f\n", settingsScreen->getCompressorRatio());
		f.print(buf2);
		bool ce = settingsScreen->getCompressorEnabled();
		snprintf(buf2, sizeof(buf2), "comp_enabled=%d\n", ce ? 1 : 0);
		f.print(buf2);
	}
	f.flush();
	f.close();
	Serial.printf("Saved zoom=%.2f to settings\n", z);
}
