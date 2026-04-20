#pragma once

#include <Arduino.h>
#include <functional>

// Interface implemented by concrete settings screen backends. Provides
// a common surface so the rest of the firmware can remain agnostic
// of the display library that renders the UI.
class ISettingsScreen {
public:
	// Logical button roles shared by the physical button mapper.
	enum class Button : uint8_t { Tap = 0, Up = 1, Ok = 2, Left = 3, Down = 4, Right = 5 };

	virtual ~ISettingsScreen() = default;

	virtual void begin() = 0;
	virtual void enter() = 0;
	virtual void exit() = 0;
	virtual bool isActive() const = 0;
	virtual void update() = 0;
	virtual bool onButton(Button button) = 0;

	virtual void setZoomCallback(std::function<void(float)> cb) = 0;
	virtual void setOneShotCallback(std::function<void(bool)> cb) = 0;
	virtual void setDelayTimeCallback(std::function<void(float)> cb) = 0;
	
	virtual void setDelayFeedbackCallback(std::function<void(float)> cb) = 0;
	virtual void setFeedbackLowpassCutoffCallback(std::function<void(float)> cb) = 0;
	virtual void setFeedbackHighpassCutoffCallback(std::function<void(float)> cb) = 0;
	
	virtual void setFilterQCallback(std::function<void(float)> cb) = 0;
	virtual void setDebugModeCallback(std::function<void(bool)> cb) = 0;
	virtual void setPotInvertedCallback(std::function<void(bool)> cb) = 0;
	virtual void setBtEnabledCallback(std::function<void(bool)> cb) = 0;

	// Get current feedback filter cutoff values (UI -> persistence)
	virtual float getFeedbackLowpassCutoff() const = 0;
	virtual float getFeedbackHighpassCutoff() const = 0;

	virtual float getZoom() const = 0;
	virtual bool getOneShot() const = 0;

	virtual float getDelayTimeMs() const = 0;
	virtual float getDelayFeedback() const = 0;
	virtual float getFilterQ() const = 0;
	virtual bool getDebugMode() const = 0;
	virtual bool getPotInverted() const = 0;
	virtual bool getBtEnabled() const = 0;

	virtual void setZoom(float zoom) = 0;
	virtual void setOneShot(bool oneShot) = 0;
	virtual void setDelayTimeMs(float ms) = 0;
	virtual void setFilterQ(float q) = 0;
	virtual void setDebugMode(bool debug) = 0;
	virtual void setPotInverted(bool inverted) = 0;
	virtual void setBtEnabled(bool enabled) = 0;

	virtual void setDelayFeedback(float feedback) = 0;
	virtual void setFeedbackLowpassCutoff(float hz) = 0;
	virtual void setFeedbackHighpassCutoff(float hz) = 0;

	// Override the maximum adjustable delay time at runtime (e.g. after
	// dynamic heap measurement). Clamped values in the UI will update accordingly.
	virtual void setDelayTimeMax(float maxMs) = 0;
};
