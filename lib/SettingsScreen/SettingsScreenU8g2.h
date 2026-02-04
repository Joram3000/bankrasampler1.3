#pragma once

#include <AudioTools.h>
#include <algorithm>
#include <U8g2lib.h>
#include <Arduino.h>
#include <functional>

#include "config/settings.h"
#include "SettingsScreen.h"



class SettingsScreenU8g2 : public ISettingsScreen {
public:
    using Button = ISettingsScreen::Button;

	enum Item : uint8_t {
		ITEM_ZOOM = 0,
		ITEM_DELAY_TIME,
		ITEM_DELAY_FEEDBACK,
		ITEM_FILTER_CUTOFF,
		ITEM_FILTER_Q,
		ITEM_COMP_ENABLED,
		ITEM_COUNT
	};

	explicit SettingsScreenU8g2(U8G2 &display)
		: u8g2(display) {}

	void setZoomCallback(std::function<void(float)> cb) override { zoomCallback = cb; }
	void setFilterCutoffCallback(std::function<void(float)> cb) override { filterCutoffCallback = cb; }
	void setFilterQCallback(std::function<void(float)> cb) override { filterQCallback = cb; }
	void setDelayTimeCallback(std::function<void(float)> cb) override { delayTimeCallback = cb; }
	void setDelayFeedbackCallback(std::function<void(float)> cb) override { delayFeedbackCallback = cb; }
	void setCompressorEnabledCallback(std::function<void(bool)> cb) override { compEnabledCallback = cb; }

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
	float getDelayTimeMs() const override { return delayTimeMs; }
	float getDelayFeedback() const override { return delayFeedback; }
	float getFilterCutoffHz() const override { return filterCutoffHz; }
	float getFilterQ() const override { return filterQ; }
	bool getCompressorEnabled() const override { return compEnabled; }
	void setZoom(float z) override { zoom = clampValue(z, 0.1f, 12.0f); markDirty(); notifyZoomChanged(); }
	void setDelayTimeMs(float ms) override { delayTimeMs = clampValue(ms, DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS); markDirty(); notifyDelayTimeChanged(); }
	void setDelayFeedback(float fb) override { delayFeedback = clampValue(fb, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX); markDirty(); notifyDelayFeedbackChanged(); }
	void setFilterCutoffHz(float hz) override { filterCutoffHz = clampValue(hz, LOW_PASS_MIN_HZ, LOW_PASS_MAX_HZ); markDirty(); notifyFilterCutoffChanged(); }
	void setFilterQ(float q) override { filterQ = clampValue(q, LOW_PASS_Q_MIN, LOW_PASS_Q_MAX); markDirty(); notifyFilterQChanged(); }
	void setCompressorEnabled(bool enabled) override { compEnabled = enabled; markDirty(); notifyCompressorEnabledChanged(); }
private:
	U8G2 &u8g2;
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
	bool compEnabled = MASTER_COMPRESSOR_ENABLED;

	std::function<void(float)> zoomCallback;
	std::function<void(float)> filterCutoffCallback;
	std::function<void(float)> filterQCallback;
	std::function<void(float)> filterSlewCallback;
	std::function<void(float)> delayTimeCallback;
	std::function<void(float)> delayDepthCallback;
	std::function<void(float)> delayFeedbackCallback;
	std::function<void(float)> dryMixCallback;
	std::function<void(float)> wetMixCallback;
	std::function<void(bool)> compEnabledCallback;

	void markDirty() { dirty = true; }

	void notifyZoomChanged() { if (zoomCallback) zoomCallback(zoom); }
	void notifyDelayTimeChanged() { if (delayTimeCallback) delayTimeCallback(delayTimeMs); }
	void notifyDelayDepthChanged() { if (delayDepthCallback) delayDepthCallback(delayDepth); }
	void notifyDelayFeedbackChanged() { if (delayFeedbackCallback) delayFeedbackCallback(delayFeedback); }
	void notifyFilterCutoffChanged() { if (filterCutoffCallback) filterCutoffCallback(filterCutoffHz); }
	void notifyFilterQChanged() { if (filterQCallback) filterQCallback(filterQ); }
	void notifyCompressorEnabledChanged() { if (compEnabledCallback) compEnabledCallback(compEnabled); }
	void adjustCurrentItem(int delta) {
		auto coarseMult = [](float fine) { return fine * 5.0f; };
		switch (selection) {
			case ITEM_ZOOM:
				applyAdjustment(zoom, delta, 0.2f, 12.0f, 0.1f, 0.5f, [this]{ notifyZoomChanged(); });
				break;
			case ITEM_DELAY_TIME:
				applyAdjustment(delayTimeMs, delta, DELAY_TIME_MIN_MS, DELAY_TIME_MAX_MS, DELAY_TIME_STEP_MS, DELAY_TIME_STEP_MS * 10.0f, [this]{ notifyDelayTimeChanged(); });
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
			case ITEM_COMP_ENABLED:
				if (delta != 0) {
					compEnabled = !compEnabled;
					markDirty();
					notifyCompressorEnabledChanged();
				}
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
		u8g2.setFont(u8g2_font_6x12_tr);
		static const char* const labels[ITEM_COUNT] = {
			"Zoom","Delay ms","Delay fb","Filter Hz",
			"Filter Q","Comp on"
		};
		const int rowHeight = 10;
		const int highlightHeight = rowHeight + 2;
		const int menuTop = 12;
		uint8_t visible = 5; // number of visible items
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
			if (editing && idx == selection) {
				snprintf(labelBuf, sizeof(labelBuf), "* %s", labels[idx]);
			} else {
				snprintf(labelBuf, sizeof(labelBuf), "  %s", labels[idx]);
			}
			u8g2.drawStr(4, baseline, labelBuf);
			char valbuf[24];
			switch (idx) {
				case ITEM_ZOOM: snprintf(valbuf, sizeof(valbuf), "%.1fx", zoom); break;
				case ITEM_DELAY_TIME: snprintf(valbuf, sizeof(valbuf), "%.0fms", delayTimeMs); break;
				case ITEM_DELAY_FEEDBACK: snprintf(valbuf, sizeof(valbuf), "%.2f", delayFeedback); break;
				case ITEM_FILTER_CUTOFF: snprintf(valbuf, sizeof(valbuf), "%.0fHz", filterCutoffHz); break;
				case ITEM_FILTER_Q: snprintf(valbuf, sizeof(valbuf), "%.2f", filterQ); break;
				case ITEM_COMP_ENABLED: snprintf(valbuf, sizeof(valbuf), "%s", compEnabled ? "On" : "Off"); break;
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
