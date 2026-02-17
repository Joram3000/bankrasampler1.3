#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffect.h"
#include "config/settings.h"
// Simple preallocated delay line: allocates once for the maximum duration and
// never resizes on setDuration. This avoids heap fragmentation issues on ESP32.
class PreallocDelay : public audio_tools::AudioEffect {
 public:
  using effect_t = audio_tools::effect_t;

  PreallocDelay() = default;

  bool begin(uint32_t sampleRate, uint16_t maxDelayMs, uint16_t initialDelayMs,
             float depth, float feedback) {
    setMaxDelayMs(maxDelayMs);
    setSampleRate(sampleRate);
    setDepth(depth);
    setFeedback(feedback);
    allocateBuffer();
    setDuration(initialDelayMs);
    setActive(true);
    return !buffer_.empty();
  }

  PreallocDelay* clone() override { return new PreallocDelay(*this); }

  void setActive(bool value) override { active_flag = value; }

  void setSampleRate(uint32_t sr) {
    if (sr == 0) return;
    sampleRate_ = sr;
    // We keep the single allocation sized for the selected maxDelayMs.
    allocateBuffer();
    updateDelaySamples();
  }

  void setMaxDelayMs(uint16_t ms) { maxDelayMs_ = ms; }

  void setDuration(uint16_t durMs) {
    durationMs_ = (durMs > maxDelayMs_) ? maxDelayMs_ : durMs;
    updateDelaySamples();
  }

  uint16_t getDuration() const { return durationMs_; }

  void setDepth(float value) {
    depth_ = value;
    if (depth_ > 1.0f) depth_ = 1.0f;
    if (depth_ < 0.0f) depth_ = 0.0f;
  }

  float getDepth() const { return depth_; }

  void setFeedback(float feed) {
    feedback_ = feed;
    if (feedback_ > 1.0f) feedback_ = 1.0f;
    if (feedback_ < 0.0f) feedback_ = 0.0f;
  }

  float getFeedback() const { return feedback_; }

  float getSampleRate() const { return static_cast<float>(sampleRate_); }

  effect_t process(effect_t input) override {
    if (!active_flag || delaySamples_ == 0 || buffer_.empty()) return input;

    if (writeIndex_ >= delaySamples_) writeIndex_ = 0;

    int32_t delayed_value = buffer_[writeIndex_];

    int32_t out = static_cast<int32_t>((1.0f - depth_) * input + depth_ * delayed_value);

    float write_val = static_cast<float>(input) + feedback_ * static_cast<float>(delayed_value);
    int32_t write_int = static_cast<int32_t>(std::round(write_val));
    buffer_[writeIndex_] = clip(write_int);

    ++writeIndex_;
    if (writeIndex_ >= delaySamples_) writeIndex_ = 0;

    return clip(out);
  }

  // Optional helper: bytes currently allocated for the delay line
  size_t bufferBytes() const { return buffer_.size() * sizeof(effect_t); }

 private:
  uint32_t sampleRate_ = 44100;
  uint16_t maxDelayMs_ = DELAY_TIME_MAX_MS;
  uint16_t durationMs_ = 100;
  float depth_ = 1.0f;
  float feedback_ = 0.5f;
  size_t delaySamples_ = 0;
  size_t writeIndex_ = 0;
  std::vector<effect_t> buffer_;

  void allocateBuffer() {
    size_t neededSamples = static_cast<size_t>((static_cast<uint64_t>(sampleRate_) * maxDelayMs_) / 1000ULL);
    if (neededSamples == 0) neededSamples = 1;
    if (buffer_.size() != neededSamples) {
      buffer_.assign(neededSamples, 0);
      writeIndex_ = 0;
    }
    // Clamp current delay to the available buffer size
    size_t maxSamples = buffer_.size();
    delaySamples_ = (delaySamples_ > maxSamples) ? maxSamples : delaySamples_;
  }

  void updateDelaySamples() {
    size_t requested = static_cast<size_t>((static_cast<uint64_t>(sampleRate_) * durationMs_) / 1000ULL);
    if (requested == 0) requested = 1;
    if (requested > buffer_.size()) requested = buffer_.size();
    delaySamples_ = requested;
    if (writeIndex_ >= delaySamples_) writeIndex_ = 0;
  }

  effect_t clip(int32_t v, int32_t limit = 32767) const {
    if (v > limit) return static_cast<effect_t>(limit);
    if (v < -limit) return static_cast<effect_t>(-limit);
    return static_cast<effect_t>(v);
  }
};
