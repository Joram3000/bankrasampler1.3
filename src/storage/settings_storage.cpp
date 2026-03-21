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
        } else if (line.startsWith("oneshot=")) {
            bool os = line.substring(8).toInt() != 0;
            settingsScreen->setOneShot(os);
            Serial.printf("Loaded oneshot=%d from settings\n", os);
        } else if (line.startsWith("delay_ms=")) {
            float d = line.substring(9).toFloat();
            settingsScreen->setDelayTimeMs(d);
            Serial.printf("Loaded delay_ms=%.2f from settings\n", d);
        } else if (line.startsWith("delay_fb=")) {
            float df = line.substring(9).toFloat();
            settingsScreen->setDelayFeedback(df);
            Serial.printf("Loaded delay_fb=%.2f from settings\n", df);
        } else if (line.startsWith("fb_hp=")) {
            float fh = line.substring(6).toFloat();
            settingsScreen->setFeedbackHighpassCutoff(fh);
            Serial.printf("Loaded fb_hp=%.2f from settings\n", fh);
        } else if (line.startsWith("fb_lp=")) {
            float fl = line.substring(6).toFloat();
            settingsScreen->setFeedbackLowpassCutoff(fl);
            Serial.printf("Loaded fb_lp=%.2f from settings\n", fl);
        } else if (line.startsWith("filter_q=")) {
            float fq = line.substring(9).toFloat();
            settingsScreen->setFilterQ(fq);
            Serial.printf("Loaded filter_q=%.2f from settings\n", fq);
        } else if (line.startsWith("debug=")) {
            bool dm = line.substring(6).toInt() != 0;
            settingsScreen->setDebugMode(dm);
            Serial.printf("Loaded debug=%d from settings\n", dm);
        }

    }
    f.close();
}

void saveSettingsToSd(const ISettingsScreen* settingsScreen) {
    if (!settingsScreen) {
        Serial.println("No settings screen provided, cannot save");
        return;
    }

    // Overwrite existing file to avoid appending multiple entries
    if (SD.exists(kSettingsPath)) SD.remove(kSettingsPath);

    File f = SD.open(kSettingsPath, FILE_WRITE);
    if (!f) {
        Serial.println("Failed to open settings file for write");
        return;
    }

    Serial.println("Saving settings to SD card:");
    Serial.printf(" zoom=%f\n", settingsScreen->getZoom());
    f.printf("zoom=%f\n", settingsScreen->getZoom());

    Serial.printf(" oneshot=%d\n", settingsScreen->getOneShot() ? 1 : 0);
    f.printf("oneshot=%d\n", settingsScreen->getOneShot() ? 1 : 0);

    float delayMs = settingsScreen->getDelayTimeMs();
    Serial.printf(" delay_ms=%.2f\n", delayMs);
    f.printf("delay_ms=%.2f\n", delayMs);

    Serial.printf(" delay_fb=%.2f\n", settingsScreen->getDelayFeedback());
    f.printf("delay_fb=%.2f\n", settingsScreen->getDelayFeedback());

    
    Serial.printf(" fb_hp=%.2f\n", settingsScreen->getFeedbackHighpassCutoff());
    f.printf("fb_hp=%.2f\n", settingsScreen->getFeedbackHighpassCutoff());
    
    Serial.printf(" fb_lp=%.2f\n", settingsScreen->getFeedbackLowpassCutoff());
    f.printf("fb_lp=%.2f\n", settingsScreen->getFeedbackLowpassCutoff());
 
    Serial.printf(" filter_q=%.2f\n", settingsScreen->getFilterQ());
    f.printf("filter_q=%.2f\n", settingsScreen->getFilterQ());

    Serial.printf(" debug=%d\n", settingsScreen->getDebugMode() ? 1 : 0);
    f.printf("debug=%d\n", settingsScreen->getDebugMode() ? 1 : 0);

    Serial.println("Settings saved.");

    f.flush();
    f.close();
}
