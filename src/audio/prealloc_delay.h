#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <new>
#include <vector>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffect.h"
#include "config/settings.h"

// Preallocated delay line: allocates once for the selected maximum delay and
// never resizes when changing duration. This avoids heap fragmentation on ESP32.
class PreallocDelay : public audio_tools::AudioEffect {
 public:
  using effect_t = audio_tools::effect_t;

  PreallocDelay() = default;

  bool begin(uint32_t sampleRate, uint16_t maxDelayMs, uint16_t initialDelayMs,
             float feedback) {
    setMaxDelayMs(maxDelayMs);
    setSampleRate(sampleRate);
    setFeedback(feedback);
    setDuration(initialDelayMs);
    setActive(true);
    return !buffer_.empty();
  }

  uint16_t getMaxDelayMs() const { return maxDelayMs_; }

  bool isAllocated() const { return !buffer_.empty(); }

  PreallocDelay* clone() override { return new PreallocDelay(*this); }

  void setActive(bool value) override { active_flag = value; }

  void setSampleRate(uint32_t sr) {
    if (sr == 0) return;
    sampleRate_ = sr;
    allocateBuffer();
    updateDelaySamples();
    updateFeedbackFilterCoeff();
    resetFilterState();
  }

  void setMaxDelayMs(uint16_t ms) { maxDelayMs_ = ms; }

  void setDuration(uint16_t durMs) {
    durationMs_ = (durMs > maxDelayMs_) ? maxDelayMs_ : durMs;
    updateDelaySamples();
  }

  void setFeedback(float feed) { feedback_ = clamp01(feed); }
  
  float getFeedback() const { return feedback_; }

  void setFeedbackLowpassCutoff(float hz) {
    fb_lp_cutoffHz_ = std::max(0.0f, hz);
    updateFeedbackFilterCoeff();
    fb_lp_state_ = 0.0f;
  }

  float getFeedbackLowpassCutoff() const { return fb_lp_cutoffHz_; }

  void setFeedbackHighpassCutoff(float hz) {
    fb_hp_cutoffHz_ = std::max(0.0f, hz);
    updateFeedbackFilterCoeff();
    fb_hp_state_ = 0.0f;
    fb_hp_prev_x_ = 0.0f;
  }

  float getFeedbackHighpassCutoff() const { return fb_hp_cutoffHz_; }

  effect_t process(effect_t input) override {
    if (!active_flag || delaySamples_ == 0 || buffer_.empty()) return input;

    if (writeIndex_ >= delaySamples_) writeIndex_ = 0;

    const int32_t delayed = buffer_[writeIndex_];
    const int32_t mixed = static_cast<int32_t>((1.0f - depth_) * input + depth_ * delayed);
    const float feedbackSample = processFeedback(static_cast<float>(delayed));
    const float toWrite = static_cast<float>(input) + feedback_ * feedbackSample;
    buffer_[writeIndex_] = clip(static_cast<int32_t>(std::round(saturate(toWrite))));

    ++writeIndex_;
    if (writeIndex_ >= delaySamples_) writeIndex_ = 0;

    return clip(mixed);
  }

  size_t bufferBytes() const { return buffer_.size() * sizeof(effect_t); }

 private:
  static float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
  }

  size_t msToSamples(uint16_t ms) const {
    size_t s = static_cast<size_t>((static_cast<uint64_t>(sampleRate_) * ms) / 1000ULL);
    return (s == 0) ? 1 : s;
  }

  float onePoleCoeff(float cutoffHz) const {
    if (sampleRate_ == 0 || cutoffHz <= 0.0f) return 0.0f;
    constexpr float pi = 3.14159265358979323846f;
    return static_cast<float>(std::exp(-2.0f * pi * cutoffHz / static_cast<float>(sampleRate_)));
  }

  void resetFilterState() {
    fb_lp_state_ = 0.0f;
    fb_hp_state_ = 0.0f;
    fb_hp_prev_x_ = 0.0f;
  }

  uint32_t sampleRate_ = 44100;
  uint16_t maxDelayMs_ = DELAY_TIME_MAX_MS;
  uint16_t durationMs_ = DEFAULT_DELAY_TIME_MS;

  float depth_ = 0.95f;
  float feedback_ = DEFAULT_DELAY_FEEDBACK;
  size_t delaySamples_ = 0;
  size_t writeIndex_ = 0;
  std::vector<effect_t> buffer_;

  // Feedback filter parameters/state.
  // Low-pass in feedback path.
  float fb_lp_cutoffHz_ = FB_LOW_PASS_CUTOFF_HZ;
  float fb_lp_a_ = 0.4f;
  float fb_lp_state_ = 0.0f; // state for low-pass filter in feedback path.

  // High-pass in feedback path.
  float fb_hp_cutoffHz_ = FB_HIGH_PASS_CUTOFF_HZ;
  float fb_hp_a_ = 0.4f;
  float fb_hp_state_ = 0.0f; // state for high-pass filter in feedback path.
  float fb_hp_prev_x_ = 0.0f; //

  void updateFeedbackFilterCoeff() {
    fb_lp_a_ = onePoleCoeff(fb_lp_cutoffHz_);
    fb_hp_a_ = onePoleCoeff(fb_hp_cutoffHz_);
  }

  float processFeedback(float delayed_f) {
    float after_hp = delayed_f;
    if (fb_hp_a_ > 0.0f) {
      after_hp = fb_hp_a_ * (fb_hp_state_ + delayed_f - fb_hp_prev_x_);
      fb_hp_state_ = after_hp;
      fb_hp_prev_x_ = delayed_f;
    }

    if (fb_lp_a_ > 0.0f) {
      fb_lp_state_ = fb_lp_a_ * fb_lp_state_ + (1.0f - fb_lp_a_) * after_hp;
      return fb_lp_state_;
    }

    return after_hp;
  }

  void allocateBuffer() {
    // maxDelayMs_ == 0 means "no delay": keep the buffer empty.
    size_t neededSamples = (maxDelayMs_ == 0) ? 0 : msToSamples(maxDelayMs_);
    if (buffer_.size() != neededSamples) {
      // Release the old block first so the new one does not need both at once.
      buffer_.clear();
      buffer_.shrink_to_fit();
      try {
        buffer_.assign(neededSamples, 0);
      } catch (const std::bad_alloc&) {
        // No contiguous block this large: leave the delay unallocated
        // (process() and the mixer bypass it) instead of aborting.
        buffer_.clear();
        buffer_.shrink_to_fit();
      }
      writeIndex_ = 0;
      resetFilterState();
    }

    size_t maxSamples = buffer_.size();
    if (delaySamples_ > maxSamples) delaySamples_ = maxSamples;
  }

  void updateDelaySamples() {
    size_t requested = msToSamples(durationMs_);
    if (requested > buffer_.size()) requested = buffer_.size();
    delaySamples_ = requested;
    if (writeIndex_ >= delaySamples_) writeIndex_ = 0;
  }

  // Soft knee for the feedback loop. Repeats played in time with the delay
  // add up (up to 1 / (1 - feedback), ~8x at 0.88), and a hard clip there is
  // stored and then repeated as digital distortion. Linear up to the knee
  // (-3 dBFS), above it bends smoothly towards full scale, like tape.
  static float saturate(float x) {
    constexpr float kFull = 32767.0f;
    constexpr float kKnee = 0.7f * kFull;
    const float ax = fabsf(x);
    if (ax <= kKnee) return x;
    // tanh(over), rational approximation: exact enough, and 1 from over = 3 on.
    const float over = (ax - kKnee) / (kFull - kKnee);
    const float t = over < 3.0f
                  ? over * (27.0f + over * over) / (27.0f + 9.0f * over * over)
                  : 1.0f;
    const float y = kKnee + (kFull - kKnee) * t;
    return x < 0.0f ? -y : y;
  }

  effect_t clip(int32_t v) const {
    const int32_t maxv = static_cast<int32_t>(std::numeric_limits<effect_t>::max());
    const int32_t minv = static_cast<int32_t>(std::numeric_limits<effect_t>::min());
    if (v > maxv) return static_cast<effect_t>(maxv);
    if (v < minv) return static_cast<effect_t>(minv);
    return static_cast<effect_t>(v);
  }
};
