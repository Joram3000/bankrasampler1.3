#pragma once

#include <cmath>

// ─── Peak limiter on the final mix ───────────────────────────────────────────
// Only there to keep the sum of samples, delay and BT out of digital clipping;
// below the ceiling it changes nothing.
//  - Instant attack: a frame that would exceed the ceiling gets exactly the
//    gain that fits, so the output never goes over. No look-ahead, because
//    that would delay the whole signal (trigger latency).
//  - Release: the gain recovers towards 1 with a ~30 ms time constant.
//  - Stereo-linked: one gain for all channels, so the image doesn't shift.

class PeakLimiter {
public:
    void setSampleRate(float sampleRate) {
        releaseCoef = 1.0f - expf(-1.0f / (kReleaseSeconds * sampleRate));
    }

    // Audio task only. Scales one frame in place so no sample exceeds the
    // ceiling.
    void process(float *frame, int channels) {
        float peak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) peak = fmaxf(peak, fabsf(frame[ch]));

        gain += (1.0f - gain) * releaseCoef;
        if (peak * gain > kCeiling) gain = kCeiling / peak;

        if (gain < 1.0f)
            for (int ch = 0; ch < channels; ++ch) frame[ch] *= gain;
    }

private:
    static constexpr float kCeiling        = 32767.0f; // 0 dBFS for int16
    static constexpr float kReleaseSeconds = 0.03f;

    float gain        = 1.0f;
    float releaseCoef = 1.0f - expf(-1.0f / (kReleaseSeconds * 44100.0f));
};
