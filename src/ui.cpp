// ui.cpp - display + scope implementation
#include "ui.h"
#include "config.h"

#include <AudioTools.h>
#include <Wire.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "SettingsScreen.h"
#include "SettingsScreenU8g2.h"
#include "storage/logo.h"
#include <U8g2lib.h>
#include <ScopeDisplayU8g2.h>

// --- HUD state ---------------------------------------------------------------
// Written from any task (loop, BT callback), read from display task (Core 0).
// Volatile + 32-bit aligned types are atomic on Xtensa without a mutex.
static volatile bool  hudDelayEnabled = false;
static volatile float hudPotValue     = 0.0f;
static volatile bool  hudIsFilter     = false;
static volatile bool  hudBtConnected  = false;

void setHudDelayEnabled(bool on)                { hudDelayEnabled = on; }
void setHudPotValue(float v, bool isFilter)     { hudPotValue = v; hudIsFilter = isFilter; }
void setHudBtConnected(bool connected)          { hudBtConnected = connected; }

// --- Display objects ---------------------------------------------------------
static int16_t waveformBuffer[NUM_WAVEFORM_SAMPLES];
static int waveformIndex = 0;

// Splash animation parameters
static constexpr float    splash_minVel          = 2.3f;
static constexpr float    splash_maxVel          = 6.0f;
static constexpr uint16_t splash_frameDelayMs    = 16;
static constexpr float    splash_horizontalSpread = 3.5f;

static DISPLAY_U8G2_CLASS display(DISPLAY_U8G2_CTOR_ARGS);
static ScopeDisplayU8g2 scopeDisplay(&display, waveformBuffer, &waveformIndex, NUM_WAVEFORM_SAMPLES);

ScopeI2SStream scopeI2s(waveformBuffer, &waveformIndex, scopeDisplay.getScopeMutex());

// --- HUD draw callback -------------------------------------------------------
// Called from the display task (Core 0) after the waveform is drawn but
// before sendBuffer() — so HUD appears in the same frame.
static void drawHud(U8G2 *d) {
    // Bottom status bar — compact 4x6 font
    d->setFont(u8g2_font_5x7_tr);

    // Bottom-left: delay on/off
    const char *delTxt = hudDelayEnabled ? "del on" : "del off";
    uint16_t delW = d->getStrWidth(delTxt);
    d->setDrawColor(0);
    d->drawBox(0, DISPLAY_HEIGHT - 7, (int)(delW + 2), 7);
    d->setDrawColor(1);
    d->drawStr(1, DISPLAY_HEIGHT - 2, delTxt);

    // Bottom-right: vol/flt percentage
    char potTxt[12];
    int pct = (int)(hudPotValue * 100.0f + 0.5f);
    snprintf(potTxt, sizeof(potTxt), "%s %d%%", hudIsFilter ? "flt" : "vol", pct);
    uint16_t potW = d->getStrWidth(potTxt);
    d->setDrawColor(0);
    d->drawBox((int)(DISPLAY_WIDTH - potW - 2), DISPLAY_HEIGHT - 7, (int)(potW + 2), 7);
    d->setDrawColor(1);
    d->drawStr((int)(DISPLAY_WIDTH - potW - 1), DISPLAY_HEIGHT - 2, potTxt);

    // Top-center: BT connected overlay (slightly larger font for readability)
    if (hudBtConnected) {
        d->setFont(u8g2_font_5x7_tf);
        const char *btTxt = "BT verbonden";
        uint16_t btW = d->getStrWidth(btTxt);
        int bx = (DISPLAY_WIDTH - (int)btW) / 2;
        d->setDrawColor(0);
        d->drawBox(bx - 1, 0, (int)(btW + 2), 9);
        d->setDrawColor(1);
        d->drawStr(bx, 7, btTxt);
    }

    d->setDrawColor(1); // restore
}

// --- Public API --------------------------------------------------------------
bool initUi() {
    Serial.println(F("[UI] Using U8g2 display backend"));
    if (!scopeDisplay.begin(DISPLAY_I2C_ADDRESS)) {
        Serial.println(F("Display init failed"));
        return false;
    }
    scopeDisplay.setHudCallback(drawHud);
    return true;
}

U8G2* getU8g2Display()                { return &display; }
void* getDisplayMutex()               { return scopeDisplay.getMutex(); }
void setScopeHorizZoom(float z)       { scopeDisplay.setHorizZoom(z); }
void setScopeDisplaySuspended(bool s) { scopeDisplay.setSuspended(s); }

ISettingsScreen* createSettingsScreen() {
    auto* s = new SettingsScreenU8g2(display);
    s->begin();
    return s;
}

void showSplash() {
    setScopeDisplaySuspended(true);
    if (auto m = scopeDisplay.getMutex()) {
        if (*m != NULL && xSemaphoreTake(*m, pdMS_TO_TICKS(200)) == pdTRUE) {
            display.clearBuffer();
            display.drawXBMP(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, epd_bitmap_bankra_logo);
            display.sendBuffer();
            xSemaphoreGive(*m);
        }
    }
    delay(50);
}

void hideSplash() {
    setScopeDisplaySuspended(true);

    auto ease = [](float t) -> float {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return 1.0f - powf(1.0f - t, 3.0f);
    };

    const TickType_t frameDelayTicks = pdMS_TO_TICKS(splash_frameDelayMs);
    const int BYTES_PER_ROW = (DISPLAY_WIDTH + 7) / 8;

    auto xbmPixel = [&](const uint8_t *bm, int x, int y) -> bool {
        if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return false;
        int idx = y * BYTES_PER_ROW + (x / 8);
        return (bm[idx] >> (x % 8)) & 0x1;
    };

    std::vector<float> offs(DISPLAY_WIDTH, 0.0f), vel(DISPLAY_WIDTH), horizVel(DISPLAY_WIDTH);
    for (int x = 0; x < DISPLAY_WIDTH; ++x) {
        vel[x] = splash_minVel + ((float)random(0, 1000) / 1000.0f) * (splash_maxVel - splash_minVel);
        horizVel[x] = ((float)random(-1000, 1000) / 1000.0f) * splash_horizontalSpread;
        if (random(0, 100) < 30) offs[x] = -(float)random(0, 8);
    }

    bool hasMutex = false;
    if (auto m = scopeDisplay.getMutex()) {
        if (*m != NULL && xSemaphoreTake(*m, pdMS_TO_TICKS(200)) == pdTRUE) hasMutex = true;
    }

    bool running = true;
    while (running) {
        running = false;
        display.clearBuffer();
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            float progress = offs[x] / (float)DISPLAY_HEIGHT;
            progress = (progress < 0.0f) ? 0.0f : (progress > 1.0f ? 1.0f : progress);
            int hOffset = (int)roundf(horizVel[x] * ease(progress));
            int dy = (int)offs[x];
            for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
                if (xbmPixel(epd_bitmap_bankra_logo, x, y)) {
                    int nx = x + hOffset;
                    int ny = y + dy;
                    if (nx >= 0 && nx < DISPLAY_WIDTH && ny >= 0 && ny < DISPLAY_HEIGHT) {
                        display.drawPixel(nx, ny);
                    }
                }
            }
            if (offs[x] < DISPLAY_HEIGHT) {
                offs[x] += vel[x];
                running = running || (offs[x] < DISPLAY_HEIGHT);
            }
        }
        display.sendBuffer();
        if (hasMutex) vTaskDelay(frameDelayTicks); else delay(splash_frameDelayMs);
    }

    if (hasMutex) {
        Serial.println(F("[UI] Splash done, releasing display mutex"));
        if (auto m = scopeDisplay.getMutex()) {
            if (*m != NULL) xSemaphoreGive(*m);
        }
    }

    display.clearBuffer();
    display.sendBuffer();

    setScopeDisplaySuspended(false);
}
