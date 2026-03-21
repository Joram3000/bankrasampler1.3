#pragma once

#include <AudioTools.h>
#include <algorithm>
#include <U8g2lib.h>
#include <Arduino.h>
#include <functional>

#include "config/settings.h"
#include "SettingsScreen.h"
#include "BpmTap/BpmTap.h"

class SettingsScreenU8g2 : public ISettingsScreen {
public:
    using Button = ISettingsScreen::Button;

	enum Item : uint8_t {
		ITEM_ZOOM,
		ITEM_ONE_SHOT,
		ITEM_DELAY_TIME,
		ITEM_DELAY_FEEDBACK,
		ITEM_FB_HIGHPASS,
		ITEM_FB_LOWPASS,
		ITEM_FILTER_Q,
		ITEM_DEBUG,
		ITEM_COUNT
	};

	explicit SettingsScreenU8g2(U8G2 &display)
		: u8g2(display) {}
	
	void setZoomCallback(std::function<void(float)> cb) override { zoomCallback = cb; }
	void setDelayTimeCallback(std::function<void(float)> cb) override { delayTimeCallback = cb; }
	void setDelayFeedbackCallback(std::function<void(float)> cb) override { delayFeedbackCallback = cb; }
	void setOneShotCallback(std::function<void(bool)> cb) override { oneShotCallback = cb; }
	void setFeedbackLowpassCutoffCallback(std::function<void(float)> cb) override { feedbackLowpassCutoffCallback = cb; }
	void setFeedbackHighpassCutoffCallback(std::function<void(float)> cb) override { feedbackHighpassCutoffCallback = cb; }
	void setFilterQCallback(std::function<void(float)> cb) override { filterQCallback = cb; }
	void setDebugModeCallback(std::function<void(bool)> cb) override { debugModeCallback = cb; }

	void begin() override {}

	void enter() override { active = true; markDirty(); }
	void exit() override  { active = false; }
	bool isActive() const override { return active; }

	void draw() {
        if (!active || !dirty) return;
        unsigned long now = millis();
        if ((now - lastDrawMs) < 33) return;
        lastDrawMs = now;
        u8g2.clearBuffer();
        drawMenu();
        u8g2.sendBuffer();
        dirty = false;
    }

	void update() override { draw(); }

	bool onButton(Button b) override {
        if (!active) return false;
        switch(b) {
            case Button::Ok:
				Serial.println("SettingsScreen: OK button pressed, ");
                 return true;
            case Button::Tap:
                bpmTap.tap();
              				 {
                    float avg = bpmTap.getAverageInterval(); 
                    if (avg > 0) {
                        tappedIntervalMs = avg;
                        setDelayTimeMs(tappedIntervalMs + 0.5f);
                    }
                }
                 return true;
            case Button::Up:
				// Up: move selection up
				if (selection == 0) selection = ITEM_COUNT - 1; else --selection;
				markDirty();
                 return true;
            case Button::Down:
				// Down: move selection down
				selection = (selection + 1) % ITEM_COUNT;
				markDirty();
                 return true;
            case Button::Left:
				// Left: adjust selected item (small step)
				adjustCurrentItem(-1);
                 return true;
            case Button::Right:
				// Right: adjust selected item (small step)
				adjustCurrentItem(+1);
                 return true;
        }
        return false;
    }

	float getZoom() const override { return zoom; }
	bool getOneShot() const override { return oneShot; }
	float getDelayTimeMs() const override { return delayTimeMs; }
	float getDelayFeedback() const override { return delayFeedback; }
	float getFeedbackLowpassCutoff() const override { return feedbackLowpassCutoff; }
	float getFeedbackHighpassCutoff() const override { return feedbackHighpassCutoff; }
	float getFilterQ() const override { return filterQ; }
	bool getDebugMode() const override { return debugMode; }
	
	void setZoom(float z) override { zoom = clampValue(z, 0.1f, 12.0f); markDirty(); notifyZoomChanged(); }
	void setOneShot(bool oneShot) override { this->oneShot = oneShot; markDirty(); if (oneShotCallback) oneShotCallback(oneShot); }
	void setDelayTimeMs(float ms) override { delayTimeMs = clampValue(ms, DELAY_TIME_MIN_MS, delayTimeMaxMs); markDirty(); notifyDelayTimeChanged(); }
	void setDelayTimeMax(float maxMs) override {
		delayTimeMaxMs = std::max(DELAY_TIME_MIN_MS, maxMs);
		delayTimeMs    = clampValue(delayTimeMs, DELAY_TIME_MIN_MS, delayTimeMaxMs);
		markDirty();
	}
	void setDelayFeedback(float fb) override { delayFeedback = clampValue(fb, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX); markDirty(); notifyDelayFeedbackChanged(); }
	void setFeedbackLowpassCutoff(float hz) override { feedbackLowpassCutoff = clampValue(hz, FB_LOW_PASS_MIN_HZ, FB_LOW_PASS_MAX_HZ); markDirty(); if (feedbackLowpassCutoffCallback) feedbackLowpassCutoffCallback(feedbackLowpassCutoff); }
	void setFeedbackHighpassCutoff(float hz) override { feedbackHighpassCutoff = clampValue(hz, FB_HIGH_PASS_MIN_HZ, FB_HIGH_PASS_MAX_HZ); markDirty(); if (feedbackHighpassCutoffCallback) feedbackHighpassCutoffCallback(feedbackHighpassCutoff); }
	void setFilterQ(float q) override { filterQ = clampValue(q, LOW_PASS_Q_MIN, LOW_PASS_Q_MAX); markDirty(); notifyFilterQChanged(); }
	void setDebugMode(bool debug) override { debugMode = debug; markDirty(); if (debugModeCallback) debugModeCallback(debug); }
private:
    U8G2 &u8g2;
    bool active = false;
    // bool editing = false;
    BpmTap bpmTap;
	unsigned long tappedIntervalMs = 0;
    float zoom = DEFAULT_HORIZ_ZOOM;
    bool dirty = true;
    unsigned long lastDrawMs = 0;
    uint8_t selection = 0;

	float delayTimeMs    = DEFAULT_DELAY_TIME_MS;
	float delayTimeMaxMs = DELAY_TIME_MAX_MS;
	bool oneShot = false;
	float delayFeedback = DEFAULT_DELAY_FEEDBACK;
	float feedbackLowpassCutoff = FB_LOW_PASS_CUTOFF_HZ;
	float feedbackHighpassCutoff = FB_HIGH_PASS_CUTOFF_HZ;
	float filterQ = LOW_PASS_Q;
	bool debugMode = true;

	std::function<void(float)> zoomCallback;
	std::function<void(bool)> oneShotCallback;
	std::function<void(float)> delayTimeCallback;
	std::function<void(float)> delayFeedbackCallback;
	std::function<void(float)> feedbackLowpassCutoffCallback;
	std::function<void(float)> feedbackHighpassCutoffCallback;
	std::function<void(float)> filterQCallback;
	std::function<void(bool)> debugModeCallback;

	void markDirty() { dirty = true; }
	void notifyZoomChanged() { if (zoomCallback) zoomCallback(zoom); }
	void notifyDelayTimeChanged() { if (delayTimeCallback) delayTimeCallback(delayTimeMs); }
	void notifyDelayFeedbackChanged() { if (delayFeedbackCallback) delayFeedbackCallback(delayFeedback); }
	void notifyFilterQChanged() { if (filterQCallback) filterQCallback(filterQ); }
	void adjustCurrentItem(int delta) {
		
		switch (selection) {
			case ITEM_ZOOM:
				applyAdjustment(zoom, delta, 0.2f, 12.0f, 0.1f, [this]{ notifyZoomChanged(); });
				break;
             case ITEM_ONE_SHOT:
                 if (delta != 0) {
                     setOneShot(!oneShot);
                 }
                 break;
            case ITEM_DELAY_TIME:
				applyAdjustment(delayTimeMs, delta, DELAY_TIME_MIN_MS, delayTimeMaxMs, DELAY_TIME_STEP_MS, [this]{ notifyDelayTimeChanged(); });
                break;
            case ITEM_DELAY_FEEDBACK:
				applyAdjustment(delayFeedback, delta, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX, DELAY_FEEDBACK_STEP, [this]{ notifyDelayFeedbackChanged(); });
                break;
			case ITEM_FB_HIGHPASS:
				applyAdjustment(feedbackHighpassCutoff, delta, FB_HIGH_PASS_MIN_HZ, FB_HIGH_PASS_MAX_HZ, 50.0f, [this]{ if (feedbackHighpassCutoffCallback) feedbackHighpassCutoffCallback(feedbackHighpassCutoff); });
				break;
			case ITEM_FB_LOWPASS:
				applyAdjustment(feedbackLowpassCutoff, delta, FB_LOW_PASS_MIN_HZ, FB_LOW_PASS_MAX_HZ, 100.0f, [this]{ if (feedbackLowpassCutoffCallback) feedbackLowpassCutoffCallback(feedbackLowpassCutoff); });
				break;
			case ITEM_FILTER_Q:
				applyAdjustment(filterQ, delta, LOW_PASS_Q_MIN, LOW_PASS_Q_MAX, LOW_PASS_Q_STEP, [this]{ notifyFilterQChanged(); });
				break;
			case ITEM_DEBUG:
				if (delta != 0) setDebugMode(!debugMode);
				break;
         }
     }
 
	void applyAdjustment(float &value, int delta, float minVal, float maxVal,
					     float step,
					     const std::function<void(void)> &notifier) {
		if (delta == 0) return;
		float direction = (delta > 0) ? 1.0f : -1.0f;
		value = clampValue(value + step * direction, minVal, maxVal);
		markDirty();
		if (notifier) notifier();
	}

	void drawMenu() {
		u8g2.setFont(u8g2_font_6x12_tr);
		static const char* const labels[ITEM_COUNT] = {
			"Zoom",
			"One Shot",
			"Delay ms",
			"Delay fb",
			"FB HP",
			"FB LP",
			"Filter Q",
			"Debug"
		};
		const int rowHeight = 10;
		const int highlightHeight = rowHeight + 2;
		const int menuTop = 12;
		const uint8_t maxVisible = static_cast<uint8_t>(u8g2.getDisplayHeight() / rowHeight);
		uint8_t visible = (ITEM_COUNT < maxVisible) ? ITEM_COUNT : maxVisible;
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
			int baseline = menuTop + row * rowHeight;
			if (idx == selection) {
				u8g2.drawBox(0, baseline - rowHeight, u8g2.getDisplayWidth(), highlightHeight);
				u8g2.setDrawColor(0);
			} else {
				u8g2.setDrawColor(1);
			}
			char labelBuf[24];
			
		
			snprintf(labelBuf, sizeof(labelBuf), labels[idx]); 
			
             u8g2.drawStr(4, baseline, labelBuf);
			char valbuf[24];
			switch (idx) {
				case ITEM_ONE_SHOT: snprintf(valbuf, sizeof(valbuf), "%s", oneShot ? "On" : "Off"); break;
				case ITEM_ZOOM: snprintf(valbuf, sizeof(valbuf), "%.1fx", zoom); break;
				case ITEM_DELAY_TIME: snprintf(valbuf, sizeof(valbuf), "%.0fms", delayTimeMs); break; //
				case ITEM_DELAY_FEEDBACK: snprintf(valbuf, sizeof(valbuf), "%.2f", delayFeedback); break;
				case ITEM_FB_HIGHPASS: snprintf(valbuf, sizeof(valbuf), "%.0fHz", feedbackHighpassCutoff); break;
				case ITEM_FB_LOWPASS: snprintf(valbuf, sizeof(valbuf), "%.0fHz", feedbackLowpassCutoff); break;
				case ITEM_FILTER_Q: snprintf(valbuf, sizeof(valbuf), "%.2f", filterQ); break;
				case ITEM_DEBUG: snprintf(valbuf, sizeof(valbuf), "%s", debugMode ? "On" : "Off"); break;
			}
			int vx = u8g2.getDisplayWidth() - (int)strlen(valbuf) * 6 - 4;
			u8g2.drawStr(vx, baseline, valbuf);
			u8g2.setDrawColor(1);
		}
	}

	static float clampValue(float value, float minValue, float maxValue) {
		if (value < minValue) return minValue;
		if (value > maxValue) return maxValue;
		return value;
	}
};
