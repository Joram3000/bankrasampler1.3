#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <algorithm>
#include <functional>

#include "config.h"
#include "SettingsScreen.h"

class SettingsScreenAdafruit : public ISettingsScreen {
public:
    using Button = ISettingsScreen::Button;

    enum Item : uint8_t {
        ITEM_ZOOM = 0,
        ITEM_DELAY_TIME,
        ITEM_DELAY_DEPTH,
        ITEM_DELAY_FEEDBACK,
        ITEM_FILTER_CUTOFF,
        ITEM_FILTER_Q,
        ITEM_FILTER_SLEW,
        ITEM_DRY_MIX,
        ITEM_WET_MIX,
        ITEM_COMP_ENABLED,
        ITEM_COMP_ATTACK,
        ITEM_COMP_RELEASE,
        ITEM_COMP_HOLD,
        ITEM_COMP_THRESHOLD,
        ITEM_COMP_RATIO,
        ITEM_COUNT
    };

    explicit SettingsScreenAdafruit(Adafruit_SSD1306 &display)
        : gfx(display) {}

    void setZoomCallback(std::function<void(float)> cb) override { zoomCallback = cb; }
    void setFilterCutoffCallback(std::function<void(float)> cb) override { filterCutoffCallback = cb; }
    void setFilterQCallback(std::function<void(float)> cb) override { filterQCallback = cb; }
    void setFilterSlewCallback(std::function<void(float)> cb) override { filterSlewCallback = cb; }
    void setDelayTimeCallback(std::function<void(float)> cb) override { delayTimeCallback = cb; }
    void setDelayDepthCallback(std::function<void(float)> cb) override { delayDepthCallback = cb; }
    void setDelayFeedbackCallback(std::function<void(float)> cb) override { delayFeedbackCallback = cb; }
    void setDryMixCallback(std::function<void(float)> cb) override { dryMixCallback = cb; }
    void setWetMixCallback(std::function<void(float)> cb) override { wetMixCallback = cb; }
    void setCompressorAttackCallback(std::function<void(float)> cb) override { compAttackCallback = cb; }
    void setCompressorReleaseCallback(std::function<void(float)> cb) override { compReleaseCallback = cb; }
    void setCompressorHoldCallback(std::function<void(float)> cb) override { compHoldCallback = cb; }
    void setCompressorThresholdCallback(std::function<void(float)> cb) override { compThresholdCallback = cb; }
    void setCompressorRatioCallback(std::function<void(float)> cb) override { compRatioCallback = cb; }
    void setCompressorEnabledCallback(std::function<void(bool)> cb) override { compEnabledCallback = cb; }

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
                if (editing) {
                    editing = false;
                    markDirty();
                }
                return true;
            case Button::Up:
                if (editing) {
                    adjustCurrentItem(+1);
                } else {
                    if (selection == 0) selection = ITEM_COUNT - 1; else --selection;
                    markDirty();
                }
                return true;
            case Button::Down:
                if (editing) {
                    adjustCurrentItem(-1);
                } else {
                    selection = (selection + 1) % ITEM_COUNT;
                    markDirty();
                }
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
    void setZoom(float z) override { zoom = clampValue(z, ZOOM_MIN, ZOOM_MAX); markDirty(); notifyZoomChanged(); }

    float getDelayTimeMs() const override { return delayTimeMs; }
    float getDelayDepth() const override { return delayDepth; }
    float getDelayFeedback() const override { return delayFeedback; }
    float getFilterCutoffHz() const override { return filterCutoffHz; }
    float getFilterQ() const override { return filterQ; }
    float getFilterSlewHzPerSec() const override { return filterSlewHzPerSec; }
    float getDryMix() const override { return dryMix; }
    float getWetMix() const override { return wetMix; }
    bool getCompressorEnabled() const override { return compEnabled; }
    float getCompressorAttackMs() const override { return compAttackMs; }
    float getCompressorReleaseMs() const override { return compReleaseMs; }
    float getCompressorHoldMs() const override { return compHoldMs; }
    float getCompressorThresholdPercent() const override { return compThresholdPercent; }
    float getCompressorRatio() const override { return compRatio; }

    void setDelayTimeMs(float ms) override { delayTimeMs = clampValue(ms, DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS); markDirty(); notifyDelayTimeChanged(); }
    void setDelayDepth(float d) override { delayDepth = clampValue(d, DELAY_DEPTH_MIN, DELAY_DEPTH_MAX); markDirty(); notifyDelayDepthChanged(); }
    void setDelayFeedback(float fb) override { delayFeedback = clampValue(fb, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX); markDirty(); notifyDelayFeedbackChanged(); }
    void setFilterCutoffHz(float hz) override { filterCutoffHz = clampValue(hz, LOW_PASS_MIN_HZ, LOW_PASS_MAX_HZ); markDirty(); notifyFilterCutoffChanged(); }
    void setFilterQ(float q) override { filterQ = clampValue(q, LOW_PASS_Q_MIN, LOW_PASS_Q_MAX); markDirty(); notifyFilterQChanged(); }
    void setFilterSlewHzPerSec(float hz) override { filterSlewHzPerSec = clampValue(hz, FILTER_SLEW_MIN_HZ_PER_SEC, FILTER_SLEW_MAX_HZ_PER_SEC); markDirty(); notifyFilterSlewChanged(); }
    void setDryMix(float mix) override { dryMix = clampValue(mix, MIXER_DRY_MIN, MIXER_DRY_MAX); markDirty(); notifyDryMixChanged(); }
    void setWetMix(float mix) override { wetMix = clampValue(mix, MIXER_WET_MIN, MIXER_WET_MAX); markDirty(); notifyWetMixChanged(); }
    void setCompressorEnabled(bool enabled) override { compEnabled = enabled; markDirty(); notifyCompressorEnabledChanged(); }
    void setCompressorAttackMs(float ms) override { compAttackMs = clampValue(ms, MASTER_COMPRESSOR_ATTACK_MIN_MS, MASTER_COMPRESSOR_ATTACK_MAX_MS); markDirty(); notifyCompressorAttackChanged(); }
    void setCompressorReleaseMs(float ms) override { compReleaseMs = clampValue(ms, MASTER_COMPRESSOR_RELEASE_MIN_MS, MASTER_COMPRESSOR_RELEASE_MAX_MS); markDirty(); notifyCompressorReleaseChanged(); }
    void setCompressorHoldMs(float ms) override { compHoldMs = clampValue(ms, MASTER_COMPRESSOR_HOLD_MIN_MS, MASTER_COMPRESSOR_HOLD_MAX_MS); markDirty(); notifyCompressorHoldChanged(); }
    void setCompressorThresholdPercent(float pct) override { compThresholdPercent = clampValue(pct, MASTER_COMPRESSOR_THRESHOLD_MIN, MASTER_COMPRESSOR_THRESHOLD_MAX); markDirty(); notifyCompressorThresholdChanged(); }
    void setCompressorRatio(float ratio) override { compRatio = clampValue(ratio, MASTER_COMPRESSOR_RATIO_MIN, MASTER_COMPRESSOR_RATIO_MAX); markDirty(); notifyCompressorRatioChanged(); }

private:
    Adafruit_SSD1306 &gfx;
    bool active = false;
    bool editing = false;
    float zoom = DEFAULT_HORIZ_ZOOM;
    bool dirty = true;
    unsigned long lastDrawMs = 0;
    uint8_t selection = 0;

    float delayTimeMs = DEFAULT_DELAY_TIME_MS;
    float delayDepth = DEFAULT_DELAY_DEPTH;
    float delayFeedback = DEFAULT_DELAY_FEEDBACK;
    float filterCutoffHz = LOW_PASS_CUTOFF_HZ;
    float filterQ = LOW_PASS_Q;
    float filterSlewHzPerSec = FILTER_SLEW_DEFAULT_HZ_PER_SEC;
    float dryMix = MIXER_DEFAULT_DRY_LEVEL;
    float wetMix = MIXER_DEFAULT_WET_LEVEL;
    bool compEnabled = MASTER_COMPRESSOR_ENABLED;
    float compAttackMs = MASTER_COMPRESSOR_ATTACK_MS;
    float compReleaseMs = MASTER_COMPRESSOR_RELEASE_MS;
    float compHoldMs = MASTER_COMPRESSOR_HOLD_MS;
    float compThresholdPercent = MASTER_COMPRESSOR_THRESHOLD_PERCENT;
    float compRatio = MASTER_COMPRESSOR_RATIO;

    std::function<void(float)> zoomCallback;
    std::function<void(float)> filterCutoffCallback;
    std::function<void(float)> filterQCallback;
    std::function<void(float)> filterSlewCallback;
    std::function<void(float)> delayTimeCallback;
    std::function<void(float)> delayDepthCallback;
    std::function<void(float)> delayFeedbackCallback;
    std::function<void(float)> dryMixCallback;
    std::function<void(float)> wetMixCallback;
    std::function<void(float)> compAttackCallback;
    std::function<void(float)> compReleaseCallback;
    std::function<void(float)> compHoldCallback;
    std::function<void(float)> compThresholdCallback;
    std::function<void(float)> compRatioCallback;
    std::function<void(bool)> compEnabledCallback;

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
    void notifyDelayDepthChanged() { if (delayDepthCallback) delayDepthCallback(delayDepth); }
    void notifyDelayFeedbackChanged() { if (delayFeedbackCallback) delayFeedbackCallback(delayFeedback); }
    void notifyFilterCutoffChanged() { if (filterCutoffCallback) filterCutoffCallback(filterCutoffHz); }
    void notifyFilterQChanged() { if (filterQCallback) filterQCallback(filterQ); }
    void notifyFilterSlewChanged() { if (filterSlewCallback) filterSlewCallback(filterSlewHzPerSec); }
    void notifyDryMixChanged() { if (dryMixCallback) dryMixCallback(dryMix); }
    void notifyWetMixChanged() { if (wetMixCallback) wetMixCallback(wetMix); }
    void notifyCompressorEnabledChanged() { if (compEnabledCallback) compEnabledCallback(compEnabled); }
    void notifyCompressorAttackChanged() { if (compAttackCallback) compAttackCallback(compAttackMs); }
    void notifyCompressorReleaseChanged() { if (compReleaseCallback) compReleaseCallback(compReleaseMs); }
    void notifyCompressorHoldChanged() { if (compHoldCallback) compHoldCallback(compHoldMs); }
    void notifyCompressorThresholdChanged() { if (compThresholdCallback) compThresholdCallback(compThresholdPercent); }
    void notifyCompressorRatioChanged() { if (compRatioCallback) compRatioCallback(compRatio); }

    void adjustCurrentItem(int delta) {
        auto coarseMult = [](float fine) { return fine * 5.0f; };
        switch (selection) {
            case ITEM_ZOOM:
                applyAdjustment(zoom, delta, ZOOM_MIN, ZOOM_MAX, ZOOM_STEP, ZOOM_BIG_STEP, [this]{ notifyZoomChanged(); });
                break;
            case ITEM_DELAY_TIME:
                applyAdjustment(delayTimeMs, delta, DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS, DELAY_TIME_STEP_MS, DELAY_TIME_STEP_MS * 10.0f, [this]{ notifyDelayTimeChanged(); });
                break;
            case ITEM_DELAY_DEPTH:
                applyAdjustment(delayDepth, delta, DELAY_DEPTH_MIN, DELAY_DEPTH_MAX, DELAY_DEPTH_STEP, coarseMult(DELAY_DEPTH_STEP), [this]{ notifyDelayDepthChanged(); });
                break;
            case ITEM_DELAY_FEEDBACK:
                applyAdjustment(delayFeedback, delta, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX, DELAY_FEEDBACK_STEP, coarseMult(DELAY_FEEDBACK_STEP), [this]{ notifyDelayFeedbackChanged(); });
                break;
            case ITEM_FILTER_CUTOFF:
                applyAdjustment(filterCutoffHz, delta, LOW_PASS_MIN_HZ, LOW_PASS_MAX_HZ, LOW_PASS_STEP_HZ, LOW_PASS_STEP_HZ * 10.0f, [this]{ notifyFilterCutoffChanged(); });
                break;
            case ITEM_FILTER_Q:
                applyAdjustment(filterQ, delta, LOW_PASS_Q_MIN, LOW_PASS_Q_MAX, LOW_PASS_Q_STEP, coarseMult(LOW_PASS_Q_STEP), [this]{ notifyFilterQChanged(); });
                break;
            case ITEM_FILTER_SLEW:
                applyAdjustment(filterSlewHzPerSec, delta, FILTER_SLEW_MIN_HZ_PER_SEC, FILTER_SLEW_MAX_HZ_PER_SEC, FILTER_SLEW_STEP_HZ_PER_SEC, FILTER_SLEW_STEP_HZ_PER_SEC * 10.0f, [this]{ notifyFilterSlewChanged(); });
                break;
            case ITEM_DRY_MIX:
                applyAdjustment(dryMix, delta, MIXER_DRY_MIN, MIXER_DRY_MAX, MIXER_DRY_STEP, coarseMult(MIXER_DRY_STEP), [this]{ notifyDryMixChanged(); });
                break;
            case ITEM_WET_MIX:
                applyAdjustment(wetMix, delta, MIXER_WET_MIN, MIXER_WET_MAX, MIXER_WET_STEP, coarseMult(MIXER_WET_STEP), [this]{ notifyWetMixChanged(); });
                break;
            case ITEM_COMP_ENABLED:
                if (delta != 0) {
                    compEnabled = !compEnabled;
                    markDirty();
                    notifyCompressorEnabledChanged();
                }
                break;
            case ITEM_COMP_ATTACK:
                applyAdjustment(compAttackMs, delta, MASTER_COMPRESSOR_ATTACK_MIN_MS, MASTER_COMPRESSOR_ATTACK_MAX_MS, MASTER_COMPRESSOR_ATTACK_STEP_MS, coarseMult(MASTER_COMPRESSOR_ATTACK_STEP_MS), [this]{ notifyCompressorAttackChanged(); });
                break;
            case ITEM_COMP_RELEASE:
                applyAdjustment(compReleaseMs, delta, MASTER_COMPRESSOR_RELEASE_MIN_MS, MASTER_COMPRESSOR_RELEASE_MAX_MS, MASTER_COMPRESSOR_RELEASE_STEP_MS, coarseMult(MASTER_COMPRESSOR_RELEASE_STEP_MS), [this]{ notifyCompressorReleaseChanged(); });
                break;
            case ITEM_COMP_HOLD:
                applyAdjustment(compHoldMs, delta, MASTER_COMPRESSOR_HOLD_MIN_MS, MASTER_COMPRESSOR_HOLD_MAX_MS, MASTER_COMPRESSOR_HOLD_STEP_MS, coarseMult(MASTER_COMPRESSOR_HOLD_STEP_MS), [this]{ notifyCompressorHoldChanged(); });
                break;
            case ITEM_COMP_THRESHOLD:
                applyAdjustment(compThresholdPercent, delta, MASTER_COMPRESSOR_THRESHOLD_MIN, MASTER_COMPRESSOR_THRESHOLD_MAX, MASTER_COMPRESSOR_THRESHOLD_STEP, coarseMult(MASTER_COMPRESSOR_THRESHOLD_STEP), [this]{ notifyCompressorThresholdChanged(); });
                break;
            case ITEM_COMP_RATIO:
                applyAdjustment(compRatio, delta, MASTER_COMPRESSOR_RATIO_MIN, MASTER_COMPRESSOR_RATIO_MAX, MASTER_COMPRESSOR_RATIO_STEP, coarseMult(MASTER_COMPRESSOR_RATIO_STEP), [this]{ notifyCompressorRatioChanged(); });
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
            "Zoom","Delay ms","Delay depth","Delay fb","Filter Hz",
            "Filter Q","Filter slew","Dry mix","Wet mix","Comp on",
            "Comp atk","Comp rel","Comp hold","Comp thr","Comp ratio"
        };
        const uint8_t visible = SETTINGS_VISIBLE_MENU_ITEMS;
        const int rowHeight = 9;
        const int menuTop = 8;
        uint8_t firstIndex = 0;
        if (ITEM_COUNT > visible) {
            if (selection >= visible) {
                firstIndex = selection - visible + 1;
            }
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
                if (highlightTop + highlightHeight > gfx.height()) {
                    highlightHeight = gfx.height() - highlightTop;
                }
                gfx.fillRect(0, highlightTop, gfx.width(), highlightHeight, SSD1306_WHITE);
                gfx.setTextColor(SSD1306_BLACK);
            } else {
                gfx.setTextColor(SSD1306_WHITE);
            }

            char labelBuf[24];
            if (editing && idx == selection) {
                snprintf(labelBuf, sizeof(labelBuf), "* %s", labels[idx]);
            } else {
                snprintf(labelBuf, sizeof(labelBuf), "  %s", labels[idx]);
            }
            gfx.setCursor(4, rowTop);
            gfx.print(labelBuf);

            char valbuf[24];
            switch (idx) {
                case ITEM_ZOOM: snprintf(valbuf, sizeof(valbuf), "%.1fx", zoom); break;
                case ITEM_DELAY_TIME: snprintf(valbuf, sizeof(valbuf), "%.0fms", delayTimeMs); break;
                case ITEM_DELAY_DEPTH: snprintf(valbuf, sizeof(valbuf), "%.2f", delayDepth); break;
                case ITEM_DELAY_FEEDBACK: snprintf(valbuf, sizeof(valbuf), "%.2f", delayFeedback); break;
                case ITEM_FILTER_CUTOFF: snprintf(valbuf, sizeof(valbuf), "%.0fHz", filterCutoffHz); break;
                case ITEM_FILTER_Q: snprintf(valbuf, sizeof(valbuf), "%.2f", filterQ); break;
                case ITEM_FILTER_SLEW: snprintf(valbuf, sizeof(valbuf), "%.1fk/s", filterSlewHzPerSec / 1000.0f); break;
                case ITEM_DRY_MIX: snprintf(valbuf, sizeof(valbuf), "%.2f", dryMix); break;
                case ITEM_WET_MIX: snprintf(valbuf, sizeof(valbuf), "%.2f", wetMix); break;
                case ITEM_COMP_ENABLED: snprintf(valbuf, sizeof(valbuf), "%s", compEnabled ? "On" : "Off"); break;
                case ITEM_COMP_ATTACK: snprintf(valbuf, sizeof(valbuf), "%.0fms", compAttackMs); break;
                case ITEM_COMP_RELEASE: snprintf(valbuf, sizeof(valbuf), "%.0fms", compReleaseMs); break;
                case ITEM_COMP_HOLD: snprintf(valbuf, sizeof(valbuf), "%.0fms", compHoldMs); break;
                case ITEM_COMP_THRESHOLD: snprintf(valbuf, sizeof(valbuf), "%.0f%%", compThresholdPercent); break;
                case ITEM_COMP_RATIO: {
                    float displayRatio = (compRatio > 0.001f) ? (1.0f / compRatio) : 0.0f;
                    snprintf(valbuf, sizeof(valbuf), "1:%.1f", displayRatio);
                    break;
                }
            }
            int16_t x1, y1;
            uint16_t w, h;
            gfx.getTextBounds(valbuf, 0, 0, &x1, &y1, &w, &h);
            int vx = static_cast<int>(gfx.width()) - static_cast<int>(w) - 4;
            if (vx < 4) vx = 4;
            gfx.setCursor(vx, rowTop);
            gfx.print(valbuf);

            if (selected) {
                gfx.setTextColor(SSD1306_WHITE);
            }
        }
    }

    static float clampValue(float value, float minValue, float maxValue) {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }
};
