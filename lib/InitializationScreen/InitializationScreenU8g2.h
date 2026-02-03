#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

#include "config.h"
#include "InitializationScreen.h"

class InitializationScreenU8g2 : public IInitializationScreen {
public:
    static constexpr size_t kMaxStatusEntries = 6;

    struct StatusEntry {
        String label;
        bool done = false;
    };

    explicit InitializationScreenU8g2(U8G2 &display,
                                     const char *initialMessage = INIT_SCREEN_MESSAGE,
                                     unsigned long durationMs_ = INIT_SCREEN_DURATION_MS)
        : u8g2(display), message(String(initialMessage)), durationMs(durationMs_) {}

    void begin() override { startMs = millis(); }
    void enter() override { active = true; startMs = millis(); markDirty(); }
    void exit() override { active = false; }
    bool isActive() const override { return active; }
    void update() override { draw(); }

    void setMessage(const char *msg) { message = String(msg); markDirty(); }
    void setMessage(const String &msg) { message = msg; markDirty(); }
    const String &getMessage() const { return message; }

    void setDurationMs(unsigned long ms) { durationMs = ms; markDirty(); }
    unsigned long getDurationMs() const { return durationMs; }

    void setStatus(size_t index, const char *label, bool done) {
        if (index >= kMaxStatusEntries) return;
        statuses[index].label = label;
        statuses[index].done = done;
        if (statusCount <= index) {
            statusCount = index + 1;
        }
        markDirty();
    }

private:
    U8G2 &u8g2;
    String message;
    unsigned long durationMs;
    StatusEntry statuses[kMaxStatusEntries];
    size_t statusCount = 0;
    bool active = false;
    bool dirty = true;
    unsigned long lastDrawMs = 0;
    unsigned long startMs = 0;

    void markDirty() { dirty = true; }

    void draw() {
        if (!active || !dirty) return;
        unsigned long now = millis();
        if (lastDrawMs != 0 && (now - lastDrawMs) < 33) return; // ~30 FPS cap
        lastDrawMs = now;
        u8g2.clearBuffer();
        drawMenu();
        u8g2.sendBuffer();
        dirty = false;
    }

    void drawMenu() {
        const char *title = message.c_str();
        u8g2.setFont(u8g2_font_6x10_tf);
        int16_t tw = u8g2.getStrWidth(title);
        int16_t x = (u8g2.getDisplayWidth() - tw) / 2;
        int16_t y = 14;
        u8g2.drawStr(x, y, title);

        unsigned long now = millis();
        unsigned long elapsed = now - startMs;

        if (statusCount > 0) {
            u8g2.setFont(u8g2_font_5x8_tf);
            size_t columns = 2;
            size_t rows = (statusCount + columns - 1) / columns;
            constexpr int rowHeight = 10;
            constexpr int statusStartY = 28;
            for (size_t row = 0; row < rows; ++row) {
                int16_t rowY = statusStartY + row * rowHeight;
                for (size_t col = 0; col < columns; ++col) {
                    size_t idx = col * rows + row;
                    if (idx >= statusCount) break;
                    char line[32];
                    snprintf(line, sizeof(line), "[%c] %s", statuses[idx].done ? 'x' : ' ', statuses[idx].label.c_str());
                    int16_t colX = (col == 0) ? 2 : (u8g2.getDisplayWidth() / 2 + 2);
                    u8g2.drawStr(colX, rowY, line);
                }
            }
        }

        int16_t statusBlockBottom;
        if (statusCount > 0) {
            size_t rows = (statusCount + 1) / 2;
            statusBlockBottom = 28 + rows * 10;
        } else {
            statusBlockBottom = 28;
        }
        int16_t timerY = statusBlockBottom + 8;
        if (timerY > u8g2.getDisplayHeight() - 18) {
            timerY = u8g2.getDisplayHeight() - 18;
        }

        char timerBuf[32];
        snprintf(timerBuf, sizeof(timerBuf), "Tijd %.1fs", elapsed / 1000.0f);
        u8g2.setFont(u8g2_font_5x8_tf);
        u8g2.drawStr(2, timerY, timerBuf);

        const int barW = u8g2.getDisplayWidth() - 20;
        const int barX = 10;
        const int barY = u8g2.getDisplayHeight() - 10;
        u8g2.drawFrame(barX, barY, barW, 8);
        unsigned long progMax = (durationMs == 0) ? INIT_SCREEN_DURATION_MS : durationMs;
        unsigned long p = (elapsed > progMax) ? progMax : elapsed;
        int fillW = (int)((uint32_t)barW * p / (uint32_t)progMax);
        if (fillW > 0) {
            u8g2.drawBox(barX + 1, barY + 1, fillW - 1, 6);
        }
    }
};