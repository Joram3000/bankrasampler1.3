#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <algorithm>
#include <functional>

#include "config/settings.h"
#include "SettingsScreen.h"

// A trimmed-down Adafruit-based settings screen that exposes the same
// user-editable items as the U8g2 implementation. Kept minimal so the
// settings storage only needs to handle a small set of keys.
class SettingsScreenAdafruit : public ISettingsScreen {
public:
    using Button = ISettingsScreen::Button;

    enum Item : uint8_t {
        ITEM_ZOOM,
        ITEM_DELAY_TIME,
        ITEM_DELAY_FEEDBACK,
        ITEM_FILTER_Q,
        ITEM_COMP_ENABLED,
        ITEM_ONE_SHOT,
        ITEM_COUNT
    };

    explicit SettingsScreenAdafruit(Adafruit_SSD1306 &display)
        : gfx(display) {}
    void setOneShotCallback(std::function<void(bool)> cb) override { oneShotCallback = cb; }
    void setZoomCallback(std::function<void(float)> cb) override { zoomCallback = cb; }
    void setFilterQCallback(std::function<void(float)> cb) override { filterQCallback = cb; }
    void setDelayTimeCallback(std::function<void(float)> cb) override { delayTimeCallback = cb; }
    void setDelayFeedbackCallback(std::function<void(float)> cb) override { delayFeedbackCallback = cb; }

    void begin() override {}

    void enter() override { active = true; markDirty(); }
    void exit() override { active = false; }
    bool isActive() const override { return active; }

    void update() override { draw(); }

    bool onButton(Button b) override {
        if (!active) return false;
        switch (b) {
            case Button::Ok:
                editing = !editing;
                markDirty();
                return true;
            case Button::Back:
                if (editing) { editing = false; markDirty(); }
                return true;
            case Button::Up:
                if (editing) adjustCurrentItem(+1);
                else { if (selection == 0) selection = ITEM_COUNT - 1; else --selection; markDirty(); }
                return true;
            case Button::Down:
                if (editing) adjustCurrentItem(-1);
                else { selection = (selection + 1) % ITEM_COUNT; markDirty(); }
                return true;
            case Button::Left:
                if (editing) adjustCurrentItem(-10);
                return true;
            case Button::Right:
                if (editing) adjustCurrentItem(+10);
                return true;
        }
        return false;
    }

    float getZoom() const override { return zoom; }
        float getDelayTimeMs() const override { return delayTimeMs; }
    bool getOneShot() const override { return oneShot ; }
    float getDelayFeedback() const override { return delayFeedback; }
    float getFilterQ() const override { return filterQ; }
    
    void setZoom(float z) override { zoom = clampValue(z, 0.1f, 12.0f); markDirty(); notifyZoomChanged(); }
    void setOneShot(bool oneShot) override {}
    void setDelayTimeMs(float ms) override { delayTimeMs = clampValue(ms, DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS); markDirty(); notifyDelayTimeChanged(); }
    void setDelayFeedback(float fb) override { delayFeedback = clampValue(fb, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX); markDirty(); notifyDelayFeedbackChanged(); }
    void setFilterQ(float q) override { filterQ = clampValue(q, LOW_PASS_Q_MIN, LOW_PASS_Q_MAX); markDirty(); notifyFilterQChanged(); }

private:
    Adafruit_SSD1306 &gfx;
    bool active = false;
    bool editing = false;
    bool dirty = true;
    unsigned long lastDrawMs = 0;
    uint8_t selection = 0;
    
    float zoom = DEFAULT_HORIZ_ZOOM;
    float delayTimeMs = DEFAULT_DELAY_TIME_MS;
    float delayFeedback = DEFAULT_DELAY_FEEDBACK;
    float filterQ = LOW_PASS_Q;
    bool oneShot = ONE_SHOT_DEFAULT;

    std::function<void(float)> zoomCallback;
    std::function<void(bool)> oneShotCallback;
    std::function<void(float)> filterCutoffCallback;
    std::function<void(float)> filterQCallback;
    std::function<void(float)> delayTimeCallback;
    std::function<void(float)> delayFeedbackCallback;


    void draw() {
        if (!active || !dirty) return;
        unsigned long now = millis();
        if ((now - lastDrawMs) < 33) return;
        lastDrawMs = now;
        gfx.clearDisplay();
        drawMenu();
        gfx.display();
        dirty = false;
    }

    void markDirty() { dirty = true; }

    void notifyZoomChanged() { if (zoomCallback) zoomCallback(zoom); }
    void notifyDelayTimeChanged() { if (delayTimeCallback) delayTimeCallback(delayTimeMs); }
    void notifyDelayFeedbackChanged() { if (delayFeedbackCallback) delayFeedbackCallback(delayFeedback); }
    void notifyFilterQChanged() { if (filterQCallback) filterQCallback(filterQ); }

    void adjustCurrentItem(int delta) {
        auto coarseMult = [](float fine) { return fine * 5.0f; };
        switch (selection) {
            case ITEM_ZOOM:
                applyAdjustment(zoom, delta, 0.1f, 12.0f, 0.1f, 0.5f, [this]{ notifyZoomChanged(); });
                break;
            case ITEM_ONE_SHOT:
                if (delta != 0) {
                    oneShot = !oneShot;
                    markDirty();
                    if (oneShotCallback) oneShotCallback(oneShot);
                }
                break;
            case ITEM_DELAY_TIME:
                applyAdjustment(delayTimeMs, delta, DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS, DELAY_TIME_STEP_MS, DELAY_TIME_STEP_MS * 10.0f, [this]{ notifyDelayTimeChanged(); });
                break;
            case ITEM_DELAY_FEEDBACK:
                applyAdjustment(delayFeedback, delta, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX, DELAY_FEEDBACK_STEP, coarseMult(DELAY_FEEDBACK_STEP), [this]{ notifyDelayFeedbackChanged(); });
                break;
            case ITEM_FILTER_Q:
                applyAdjustment(filterQ, delta, LOW_PASS_Q_MIN, LOW_PASS_Q_MAX, LOW_PASS_Q_STEP, coarseMult(LOW_PASS_Q_STEP), [this]{ notifyFilterQChanged(); });
                break;
            
        }
    }

    void applyAdjustment(float &value, int delta, float minVal, float maxVal,
                         float fineStep, float coarseStep,
                         const std::function<void(void)> &notifier) {
        if (delta == 0) return;
        bool coarse = (delta >= 10) || (delta <= -10);
        float step = coarse ? coarseStep : fineStep;
        float direction = (delta > 0) ? 1.0f : -1.0f;
        value = clampValue(value + step * direction, minVal, maxVal);
        markDirty();
        if (notifier) notifier();
    }

    void drawMenu() {
        gfx.setFont();
        gfx.setTextSize(1);
        gfx.setTextColor(SSD1306_WHITE);
        static const char* const labels[ITEM_COUNT] = {
            "Zoom", "One Shot", "Delay ms","Delay fb","Filter Q"
        };
        const uint8_t visible = 5;
        const int rowHeight = 9;
        const int menuTop = 8;
        uint8_t firstIndex = 0;
        if (ITEM_COUNT > visible) {
            if (selection >= visible) firstIndex = selection - visible + 1;
            uint8_t maxFirst = ITEM_COUNT - visible;
            if (firstIndex > maxFirst) firstIndex = maxFirst;
        }
        for (uint8_t row = 0; row < visible; ++row) {
            uint8_t idx = firstIndex + row;
            if (idx >= ITEM_COUNT) break;
            int rowTop = menuTop + row * rowHeight;
            bool selected = (idx == selection);
            if (selected) {
                int highlightTop = std::max(0, rowTop - 1);
                int highlightHeight = rowHeight + 1;
                if (highlightTop + highlightHeight > gfx.height()) highlightHeight = gfx.height() - highlightTop;
                gfx.fillRect(0, highlightTop, gfx.width(), highlightHeight, SSD1306_WHITE);
                gfx.setTextColor(SSD1306_BLACK);
            } else {
                gfx.setTextColor(SSD1306_WHITE);
            }

            char labelBuf[24];
            if (editing && idx == selection) snprintf(labelBuf, sizeof(labelBuf), "* %s", labels[idx]);
            else snprintf(labelBuf, sizeof(labelBuf), "  %s", labels[idx]);
            gfx.setCursor(4, rowTop);
            gfx.print(labelBuf);

            char valbuf[24];
            switch (idx) {
                case ITEM_ZOOM: snprintf(valbuf, sizeof(valbuf), "%.1fx", zoom); break;
                case ITEM_DELAY_TIME: snprintf(valbuf, sizeof(valbuf), "%.0fms", delayTimeMs); break;
                case ITEM_DELAY_FEEDBACK: snprintf(valbuf, sizeof(valbuf), "%.2f", delayFeedback); break;
                case ITEM_FILTER_Q: snprintf(valbuf, sizeof(valbuf), "%.2f", filterQ); break;
            }
            int16_t x1, y1;
            uint16_t w, h;
            gfx.getTextBounds(valbuf, 0, 0, &x1, &y1, &w, &h);
            int vx = static_cast<int>(gfx.width()) - static_cast<int>(w) - 4;
            if (vx < 4) vx = 4;
            gfx.setCursor(vx, rowTop);
            gfx.print(valbuf);

            if (selected) gfx.setTextColor(SSD1306_WHITE);
        }
    }

    static float clampValue(float value, float minValue, float maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }
};
