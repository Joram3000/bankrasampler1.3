#include "sample_map.h"

#include <Arduino.h>
#include <SD.h>

#include "config/config.h"

namespace {

constexpr const char* kSampleMapPath = "/sample_map.txt";

char sampleNames[SAMPLE_MAP_MAX][SAMPLE_NAME_MAX];
// Pointer array so we can return const char* const* from an accessor.
const char* namePtrs[SAMPLE_MAP_MAX];
int sampleCount = 0;

int  buttonSampleIdx[BUTTON_COUNT];
// Per-button path buffer — stable pointer valid for player.setPath().
char pathBufs[BUTTON_COUNT][SAMPLE_NAME_MAX + 2];

bool isSupportedExtension(const char* name) {
    int len = (int)strlen(name);
    if (len < 5) return false;
    const char* ext = name + len - 4;
    return strcasecmp(ext, ".wav") == 0;
}

void scanSdForSamples() {
    sampleCount = 0;
    File root = SD.open("/");
    if (!root) return;

    while (sampleCount < SAMPLE_MAP_MAX) {
        File entry = root.openNextFile();
        if (!entry) break;
        bool isDir = entry.isDirectory();
        const char* name = entry.name();
        if (!isDir && isSupportedExtension(name)) {
            strncpy(sampleNames[sampleCount], name, SAMPLE_NAME_MAX - 1);
            sampleNames[sampleCount][SAMPLE_NAME_MAX - 1] = '\0';
            namePtrs[sampleCount] = sampleNames[sampleCount];
            ++sampleCount;
        }
        entry.close();
    }
    root.close();
}

void applyDefaultMapping() {
    for (int i = 0; i < (int)BUTTON_COUNT; ++i)
        buttonSampleIdx[i] = (sampleCount > 0) ? (i % sampleCount) : 0;
}

void loadMapFromSd() {
    if (!SD.exists(kSampleMapPath)) return;
    File f = SD.open(kSampleMapPath, FILE_READ);
    if (!f) return;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (!line.startsWith("btn")) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        int btnIdx = line.substring(3, eq).toInt();
        if (btnIdx < 0 || btnIdx >= (int)BUTTON_COUNT) continue;
        String fname = line.substring(eq + 1);
        fname.trim();
        for (int i = 0; i < sampleCount; ++i) {
            if (fname.equalsIgnoreCase(sampleNames[i])) {
                buttonSampleIdx[btnIdx] = i;
                break;
            }
        }
    }
    f.close();
}

} // namespace

void initSampleMap() {
    scanSdForSamples();
    applyDefaultMapping();
    loadMapFromSd();

    Serial.printf("[SMAP] %d sample(s) found\n", sampleCount);
    for (int i = 0; i < (int)BUTTON_COUNT; ++i) {
        int si = buttonSampleIdx[i];
        Serial.printf("[SMAP] btn%d -> %s\n", i,
                      (si >= 0 && si < sampleCount) ? sampleNames[si] : "(none)");
    }
}

const char* getSamplePathForButton(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= (int)BUTTON_COUNT) return "/1.wav";
    int si = buttonSampleIdx[buttonIndex];
    if (si < 0 || si >= sampleCount || sampleCount == 0) return "/1.wav";
    snprintf(pathBufs[buttonIndex], sizeof(pathBufs[buttonIndex]),
             "/%s", sampleNames[si]);
    return pathBufs[buttonIndex];
}

int getAvailableSampleCount() { return sampleCount; }

const char* const* getAvailableSampleNames() { return namePtrs; }

void setSampleIndexForButton(int buttonIndex, int sampleIndex) {
    if (buttonIndex < 0 || buttonIndex >= (int)BUTTON_COUNT) return;
    if (sampleCount > 0) {
        if (sampleIndex < 0)            sampleIndex = sampleCount - 1;
        if (sampleIndex >= sampleCount) sampleIndex = 0;
    } else {
        sampleIndex = 0;
    }
    buttonSampleIdx[buttonIndex] = sampleIndex;
}

int getSampleIndexForButton(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= (int)BUTTON_COUNT) return 0;
    return buttonSampleIdx[buttonIndex];
}

void saveSampleMap() {
    if (SD.exists(kSampleMapPath)) SD.remove(kSampleMapPath);
    File f = SD.open(kSampleMapPath, FILE_WRITE);
    if (!f) {
        Serial.println("[SMAP] Failed to open sample_map.txt for write");
        return;
    }
    for (int i = 0; i < (int)BUTTON_COUNT; ++i) {
        int si = buttonSampleIdx[i];
        if (si >= 0 && si < sampleCount)
            f.printf("btn%d=%s\n", i, sampleNames[si]);
    }
    f.flush();
    f.close();
    Serial.println("[SMAP] Saved sample_map.txt");
}
