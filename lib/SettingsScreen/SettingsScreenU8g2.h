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
		ITEM_BT_FILTER_Q,
		ITEM_DEBUG,
		ITEM_POT_INVERTED,
		ITEM_BT_ENABLED,
		ITEM_BT_CLEAR_BONDS,
		ITEM_SAMPLE_0,
		ITEM_SAMPLE_1,
		ITEM_SAMPLE_2,
		ITEM_SAMPLE_3,
		ITEM_SAMPLE_4,
		ITEM_SAMPLE_5,
		ITEM_WIFI_TRANSFER,
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
	void setBtFilterQCallback(std::function<void(float)> cb) override { btFilterQCallback = cb; }
	void setDebugModeCallback(std::function<void(bool)> cb) override { debugModeCallback = cb; }
	void setPotInvertedCallback(std::function<void(bool)> cb) override { potInvertedCallback = cb; }
	void setBtEnabledCallback(std::function<void(bool)> cb) override { btEnabledCallback = cb; }
	void setBtClearBondsCallback(std::function<void()> cb) override { btClearBondsCallback = cb; }
	void setTransferModeCallback(std::function<bool()> cb) override { transferModeCallback = cb; }

	void setSampleList(int count, const char* const* names) override {
		sampleCount = count;
		sampleNames = names;
		markDirty();
	}
	int getSampleIndex(int buttonIndex) const override {
		if (buttonIndex < 0 || buttonIndex >= kBtnCount) return 0;
		return sampleIndices[buttonIndex];
	}
	void setSampleIndex(int buttonIndex, int index) override {
		if (buttonIndex < 0 || buttonIndex >= kBtnCount) return;
		sampleIndices[buttonIndex] = index;
		markDirty();
	}
	void setSamplePreviewCallback(std::function<void(int)> cb) override {
		samplePreviewCallback = cb;
	}

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
				// Up: move selection up, skipping hidden items
				do {
					selection = (selection == 0) ? ITEM_COUNT - 1 : selection - 1;
				} while (!itemVisible(selection));
				markDirty();
                 return true;
            case Button::Down:
				// Down: move selection down, skipping hidden items
				do {
					selection = (selection + 1) % ITEM_COUNT;
				} while (!itemVisible(selection));
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
	float getBtFilterQ() const override { return btFilterQ; }
	bool getDebugMode() const override { return debugMode; }
	bool getPotInverted() const override { return potInverted; }
	bool getBtEnabled() const override { return btEnabled; }
	
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
	void setBtFilterQ(float q) override { btFilterQ = clampValue(q, BT_FILTER_Q_MIN, BT_FILTER_Q_MAX); markDirty(); notifyBtFilterQChanged(); }
	void setDebugMode(bool debug) override { debugMode = debug; markDirty(); if (debugModeCallback) debugModeCallback(debug); }
	void setPotInverted(bool inv) override { potInverted = inv; markDirty(); if (potInvertedCallback) potInvertedCallback(inv); }
	void setBtEnabled(bool en) override { btEnabled = en; markDirty(); if (btEnabledCallback) btEnabledCallback(en); }

	void setDelayAvailable(bool available) override { delayAvailable = available; markDirty(); }
	void setBtMenu(BtMenu mode) override { btMenu = mode; markDirty(); }
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
	float btFilterQ = BT_FILTER_Q;
	bool debugMode = false;
	bool potInverted = false;
	bool btEnabled = DEFAULT_BT_ENABLED;
	unsigned long btClearDoneUntil = 0;
	unsigned long transferMsgUntil = 0;
	bool transferStarting = false; // result of the last transfer request
	bool   delayAvailable = true;
	BtMenu btMenu         = BtMenu::Unsupported;

	bool itemVisible(uint8_t item) const {
		switch (item) {
			case ITEM_DELAY_TIME: case ITEM_DELAY_FEEDBACK:
			case ITEM_FB_HIGHPASS: case ITEM_FB_LOWPASS:
				return delayAvailable;
			case ITEM_BT_ENABLED:
				return btMenu != BtMenu::Unsupported;
			case ITEM_BT_FILTER_Q: case ITEM_BT_CLEAR_BONDS:
				return btMenu == BtMenu::On;
			default:
				return true;
		}
	}

	std::function<void(float)> zoomCallback;
	std::function<void(bool)> oneShotCallback;
	std::function<void(float)> delayTimeCallback;
	std::function<void(float)> delayFeedbackCallback;
	std::function<void(float)> feedbackLowpassCutoffCallback;
	std::function<void(float)> feedbackHighpassCutoffCallback;
	std::function<void(float)> filterQCallback;
	std::function<void(float)> btFilterQCallback;
	std::function<void(bool)> debugModeCallback;
	std::function<void(bool)> potInvertedCallback;
	std::function<void(bool)> btEnabledCallback;
	std::function<void()> btClearBondsCallback;
	std::function<bool()> transferModeCallback;
	std::function<void(int)> samplePreviewCallback;

	static constexpr int kBtnCount = 6;
	int sampleCount = 0;
	const char* const* sampleNames = nullptr;
	int sampleIndices[kBtnCount] = {0, 1, 2, 3, 4, 5};

	void markDirty() { dirty = true; }
	void notifyZoomChanged() { if (zoomCallback) zoomCallback(zoom); }
	void notifyDelayTimeChanged() { if (delayTimeCallback) delayTimeCallback(delayTimeMs); }
	void notifyDelayFeedbackChanged() { if (delayFeedbackCallback) delayFeedbackCallback(delayFeedback); }
	void notifyFilterQChanged() { if (filterQCallback) filterQCallback(filterQ); }
	void notifyBtFilterQChanged() { if (btFilterQCallback) btFilterQCallback(btFilterQ); }
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
			case ITEM_BT_FILTER_Q:
				applyAdjustment(btFilterQ, delta, BT_FILTER_Q_MIN, BT_FILTER_Q_MAX, BT_FILTER_Q_STEP, [this]{ notifyBtFilterQChanged(); });
				break;
			case ITEM_DEBUG:
				if (delta != 0) setDebugMode(!debugMode);
				break;
			case ITEM_POT_INVERTED:
				if (delta != 0) setPotInverted(!potInverted);
				break;
			case ITEM_BT_ENABLED:
				if (delta != 0) setBtEnabled(!btEnabled);
				break;
			case ITEM_BT_CLEAR_BONDS:
				if (delta != 0 && btClearBondsCallback) {
					btClearBondsCallback();
					btClearDoneUntil = millis() + 1500;
					markDirty();
				}
				break;
			case ITEM_SAMPLE_0: case ITEM_SAMPLE_1: case ITEM_SAMPLE_2:
			case ITEM_SAMPLE_3: case ITEM_SAMPLE_4: case ITEM_SAMPLE_5: {
				if (delta == 0 || sampleCount == 0) break;
				int bi = selection - ITEM_SAMPLE_0;
				int newIdx = sampleIndices[bi] + delta;
				if (newIdx < 0)            newIdx = sampleCount - 1;
				if (newIdx >= sampleCount) newIdx = 0;
				sampleIndices[bi] = newIdx;
				markDirty();
				if (samplePreviewCallback) samplePreviewCallback(bi);
				break;
			}
			case ITEM_WIFI_TRANSFER:
				if (delta != 0 && transferModeCallback) {
					transferStarting = transferModeCallback();
					transferMsgUntil = millis() + 1500;
					markDirty();
				}
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
			"BT Flt Q",
			"Debug",
			"Pot inv",
			"BT (reboot)",
			"BT: Clear pair",
			"Btn 1",
			"Btn 2",
			"Btn 3",
			"Btn 4",
			"Btn 5",
			"Btn 6",
			"WiFi upload",
		};
		const int rowHeight = 10;
		const int highlightHeight = rowHeight + 2;
		const int menuTop = 12;
		const uint8_t maxVisible = static_cast<uint8_t>(u8g2.getDisplayHeight() / rowHeight);

		// Only items that do something in the current mode are listed.
		uint8_t shown[ITEM_COUNT];
		uint8_t shownCount = 0;
		uint8_t selectedPos = 0;
		for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
			if (!itemVisible(i)) continue;
			if (i == selection) selectedPos = shownCount;
			shown[shownCount++] = i;
		}

		uint8_t visible = (shownCount < maxVisible) ? shownCount : maxVisible;
		uint8_t firstIndex = 0;
		if (shownCount > visible) {
			if (selectedPos >= visible) {
				firstIndex = selectedPos - visible + 1;
			}
			uint8_t maxFirst = shownCount - visible;
			if (firstIndex > maxFirst) firstIndex = maxFirst;
		}
		for (uint8_t row = 0; row < visible; ++row) {
			if (firstIndex + row >= shownCount) break;
			uint8_t idx = shown[firstIndex + row];
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
				case ITEM_BT_FILTER_Q: snprintf(valbuf, sizeof(valbuf), "%.2f", btFilterQ); break;
				case ITEM_DEBUG: snprintf(valbuf, sizeof(valbuf), "%s", debugMode ? "On" : "Off"); break;
				case ITEM_POT_INVERTED: snprintf(valbuf, sizeof(valbuf), "%s", potInverted ? "On" : "Off"); break;
				case ITEM_BT_ENABLED: snprintf(valbuf, sizeof(valbuf), "%s", btEnabled ? "On" : "Off"); break;
				case ITEM_BT_CLEAR_BONDS: snprintf(valbuf, sizeof(valbuf), "%s", (millis() < btClearDoneUntil) ? "Rebooting" : "< >"); break;
				case ITEM_WIFI_TRANSFER: snprintf(valbuf, sizeof(valbuf), "%s", (millis() < transferMsgUntil) ? (transferStarting ? "Rebooting" : "No app") : "< >"); break;
				case ITEM_SAMPLE_0: case ITEM_SAMPLE_1: case ITEM_SAMPLE_2:
				case ITEM_SAMPLE_3: case ITEM_SAMPLE_4: case ITEM_SAMPLE_5: {
					int bi = idx - ITEM_SAMPLE_0;
					int si = sampleIndices[bi];
					if (sampleNames && si >= 0 && si < sampleCount) {
						char noExt[16];
						strncpy(noExt, sampleNames[si], sizeof(noExt) - 1);
						noExt[sizeof(noExt) - 1] = '\0';
						int nl = (int)strlen(noExt);
						if (nl > 4 && strcasecmp(noExt + nl - 4, ".wav") == 0)
							noExt[nl - 4] = '\0';
						snprintf(valbuf, sizeof(valbuf), "%s", noExt);
					} else {
						snprintf(valbuf, sizeof(valbuf), "-");
					}
					break;
				}
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
