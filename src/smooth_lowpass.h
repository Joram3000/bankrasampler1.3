#pragma once

#include <Arduino.h>
#include <AudioTools.h>
#include <cmath>

// ─── Smoothed low-pass insert filter ─────────────────────────────────────────
// Built for click-free cutoff sweeps:
//  - Direct Form I biquad: its state is the real past input/output samples,
//    so changing coefficients does not make the output jump. (AudioTools'
//    LowPassFilter is Direct Form II, whose internal state is scaled by the
//    old coefficients and clicks when they change — badly at low cutoffs.)
//  - Coefficients are recomputed only in the audio task (tick()), between
//    blocks — never mid-block from another task.
//  - The cutoff glides towards the target in the log-frequency domain, so
//    pot steps become a smooth sweep regardless of how often the pot is read.

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; // passthrough
};

// One channel. All channels share the coefficients of one SmoothedLowPass.
class LowPassDF1 : public audio_tools::Filter<float> {
public:
    explicit LowPassDF1(const BiquadCoeffs &coeffs) : c(coeffs) {}

    float process(float x) override {
        float y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        // FilteredStream assigns the result straight to int16_t without
        // clamping, so overshoot (resonance, Gibbs) on a near-full-scale
        // sample would wrap around into a loud click. Clip instead; the
        // state above keeps the unclipped value so the filter stays linear.
        return fminf(fmaxf(y, -32768.0f), 32767.0f);
    }

private:
    const BiquadCoeffs &c;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

class SmoothedLowPass {
public:
    // Any task. Plain aligned 32-bit float stores are atomic on the ESP32.
    void setTarget(float hz, float q) {
        targetHz = hz;
        targetQ  = q;
    }

    // Audio task only, before each block that runs through the filter.
    // The first call (or the first after a long idle) jumps straight to the
    // target instead of gliding from a stale value.
    void tick(float sampleRate) {
        uint32_t now = micros();
        float dt = static_cast<float>(now - lastTickUs) * 1e-6f;
        lastTickUs = now;

        float hz = fminf(fmaxf(targetHz, kMinHz), kMaxNyquistFraction * sampleRate);

        // Q glides too: with high resonance a Q (and makeup-gain) jump is audible.
        float alpha = 1.0f - expf(-dt / kGlideSeconds);
        logHz += alpha * (logf(hz) - logHz);
        curQ  += alpha * (targetQ - curQ);
        float curHz = expf(logHz);

        if (fabsf(curHz - lastHz) < 0.1f && fabsf(curQ - lastQ) < 0.001f) return;
        lastHz = curHz;
        lastQ  = curQ;
        compute(curHz, curQ, sampleRate);
    }

    const BiquadCoeffs &coeffs() const { return c; }

private:
    static constexpr float kGlideSeconds       = 0.015f; // ~15 ms time constant
    static constexpr float kMinHz              = 20.0f;
    static constexpr float kMaxNyquistFraction = 0.45f;

    // RBJ cookbook low-pass. Above Q=1 the resonance peak (~Q×) is tamed by a
    // 1/sqrt(Q) makeup gain: the peak still stands out, but full-scale input
    // does not slam into the output clamp, and the passband drops only gently.
    void compute(float hz, float q, float sampleRate) {
        float w0    = 2.0f * PI * hz / sampleRate;
        float cosW0 = cosf(w0);
        float alpha = sinf(w0) / (2.0f * q);
        float a0inv = 1.0f / (1.0f + alpha);
        float gain  = (q > 1.0f) ? 1.0f / sqrtf(q) : 1.0f;
        c.b0 = 0.5f * (1.0f - cosW0) * a0inv * gain;
        c.b1 = (1.0f - cosW0) * a0inv * gain;
        c.b2 = c.b0;
        c.a1 = -2.0f * cosW0 * a0inv;
        c.a2 = (1.0f - alpha) * a0inv;
    }

    volatile float targetHz = 20000.0f;
    volatile float targetQ  = 0.7071f;

    BiquadCoeffs c;
    uint32_t lastTickUs = 0;
    float    logHz      = logf(20000.0f);
    float    curQ       = 0.7071f;
    float    lastHz     = -1.0f;
    float    lastQ      = -1.0f;
};
