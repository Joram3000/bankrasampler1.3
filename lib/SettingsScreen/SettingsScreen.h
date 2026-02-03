#pragma once

#include <Arduino.h>
#include <functional>

// Interface implemented by concrete settings screen backends. Provides
// a common surface so the rest of the firmware can remain agnostic
// of the display library that renders the UI.
class ISettingsScreen {
public:
	// Logical button roles shared by the physical button mapper.
	enum class Button : uint8_t { Back = 0, Up = 1, Ok = 2, Left = 3, Down = 4, Right = 5 };

	virtual ~ISettingsScreen() = default;

	virtual void begin() = 0;
	virtual void enter() = 0;
	virtual void exit() = 0;
	virtual bool isActive() const = 0;
	virtual void update() = 0;
	virtual bool onButton(Button button) = 0;

	virtual void setZoomCallback(std::function<void(float)> cb) = 0;
	virtual void setFilterCutoffCallback(std::function<void(float)> cb) = 0;
	virtual void setFilterQCallback(std::function<void(float)> cb) = 0;
	virtual void setFilterSlewCallback(std::function<void(float)> cb) = 0;
	virtual void setDelayTimeCallback(std::function<void(float)> cb) = 0;
	virtual void setDelayDepthCallback(std::function<void(float)> cb) = 0;
	virtual void setDelayFeedbackCallback(std::function<void(float)> cb) = 0;
	virtual void setDryMixCallback(std::function<void(float)> cb) = 0;
	virtual void setWetMixCallback(std::function<void(float)> cb) = 0;
	virtual void setCompressorAttackCallback(std::function<void(float)> cb) = 0;
	virtual void setCompressorReleaseCallback(std::function<void(float)> cb) = 0;
	virtual void setCompressorHoldCallback(std::function<void(float)> cb) = 0;
	virtual void setCompressorThresholdCallback(std::function<void(float)> cb) = 0;
	virtual void setCompressorRatioCallback(std::function<void(float)> cb) = 0;
	virtual void setCompressorEnabledCallback(std::function<void(bool)> cb) = 0;

	virtual float getZoom() const = 0;
	virtual float getDelayTimeMs() const = 0;
	virtual float getDelayDepth() const = 0;
	virtual float getDelayFeedback() const = 0;
	virtual float getFilterCutoffHz() const = 0;
	virtual float getFilterQ() const = 0;
	virtual float getFilterSlewHzPerSec() const = 0;
	virtual float getDryMix() const = 0;
	virtual float getWetMix() const = 0;
	virtual bool getCompressorEnabled() const = 0;
	virtual float getCompressorAttackMs() const = 0;
	virtual float getCompressorReleaseMs() const = 0;
	virtual float getCompressorHoldMs() const = 0;
	virtual float getCompressorThresholdPercent() const = 0;
	virtual float getCompressorRatio() const = 0;

	virtual void setZoom(float zoom) = 0;
	virtual void setDelayTimeMs(float ms) = 0;
	virtual void setDelayDepth(float depth) = 0;
	virtual void setDelayFeedback(float feedback) = 0;
	virtual void setFilterCutoffHz(float hz) = 0;
	virtual void setFilterQ(float q) = 0;
	virtual void setFilterSlewHzPerSec(float hz) = 0;
	virtual void setDryMix(float mix) = 0;
	virtual void setWetMix(float mix) = 0;
	virtual void setCompressorEnabled(bool enabled) = 0;
	virtual void setCompressorAttackMs(float ms) = 0;
	virtual void setCompressorReleaseMs(float ms) = 0;
	virtual void setCompressorHoldMs(float ms) = 0;
	virtual void setCompressorThresholdPercent(float pct) = 0;
	virtual void setCompressorRatio(float ratio) = 0;
};
