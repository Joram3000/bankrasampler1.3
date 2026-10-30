#pragma once

#include <Arduino.h>
#include <AudioTools.h>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffect.h"
#include <AudioTools/AudioLibs/AudioEffectsSuite.h>
#include "prealloc_delay.h"
#include <cmath>
#include <vector>

#ifdef BLUETOOTH_MODE
  #include <freertos/FreeRTOS.h>
  #include <freertos/ringbuf.h>
  #include "smooth_lowpass.h"

// ─── BTRingBuffer ─────────────────────────────────────────────────────────────
// ESP-IDF RTOS ring buffer — lock-free between cores, no custom mutex needed.
//
// Acts as a jitter buffer between the bursty A2DP callback (Core 0) and the
// steady I2S-paced mixer (Core 1):
//  - prebuffer: after start or an underrun, read() returns nothing until this
//    many bytes are queued, so A2DP burst gaps do not turn into zero-filled holes.
//  - highWater: above this level read() discards one frame per call, slowly
//    pulling the level back down (BT source clock drift, SD-stall backlog).
class BTRingBuffer {
public:
    BTRingBuffer(size_t capacity, size_t prebufferBytes, size_t highWaterBytes)
        : cap(capacity), prebuffer(prebufferBytes), highWaterMark(highWaterBytes) {}

    void begin() {
        if (handle) return;
        handle = xRingbufferCreate(cap, RINGBUF_TYPE_BYTEBUF);
    }

    // Called from the BT stack task (Core 0) — must never block.
    size_t write(const uint8_t *data, size_t len) {
        RingbufHandle_t h = handle;
        if (!h) return 0;
        size_t freeNow = xRingbufferGetCurFreeSize(h);
        if (freeNow < len) {
            ++dropCount;
            return 0;
        }
        return xRingbufferSend(h, data, len, 0) == pdTRUE ? len : 0;
    }

    uint32_t getAndResetDropCount() {
        uint32_t n = dropCount;
        dropCount = 0;
        return n;
    }

    uint32_t getAndResetUnderrunCount() {
        uint32_t n = underrunCount;
        underrunCount = 0;
        return n;
    }

    // Called from audioTask (Core 1) — the only consumer.
    // Returns the number of int16_t values written to `out` (0 while priming).
    size_t read(int16_t *out, size_t samples) {
        if (!handle || samples == 0) return 0;

        const size_t level = available();
        if (!primed) {
            if (level < prebuffer) return 0;
            primed = true;
        }

        // Catch up after the level crept up (sample starts block audioTask on
        // SD file opens while BT keeps arriving): above highWater, drop ~3% of
        // frames as single frames spread evenly through the block — inaudible,
        // and fast enough (~1 s for 35 ms of surplus) to beat the next burst.
        // Hysteresis: keep going until the level is back halfway down.
        if (level > highWaterMark)                         catchingUp = true;
        else if (level <= (prebuffer + highWaterMark) / 2) catchingUp = false;

        const size_t frames = samples / kChannels;
        const size_t drops  = catchingUp ? std::max<size_t>(1, frames / 32) : 0;

        // Read in drops+1 segments with one discarded frame between them.
        size_t got = 0;
        for (size_t seg = 0; seg <= drops; ++seg) {
            size_t segFrames = frames / (drops + 1);
            if (seg == drops) segFrames = frames - got / kChannels; // remainder
            size_t want = segFrames * kChannels;
            size_t n = readRaw(out + got, want);
            got += n;
            if (n < want) break;
            if (seg < drops) discard(kFrameBytes);
        }
        if (got < samples) {
            // Ran dry: refill to the prebuffer level before playing again.
            primed = false;
            ++underrunCount;
        }
        return got;
    }

    size_t highWater() const { return highWaterMark; }

    size_t available() const {
        if (!handle) return 0;
        return cap - xRingbufferGetCurFreeSize(handle);
    }

    size_t capacity() const { return cap; }

    // Discard all pending data and start priming again. Called from audioTask.
    void clear() {
        primed = false;
        if (!handle) return;
        for (;;) {
            size_t rxSize = 0;
            void *item = xRingbufferReceiveUpTo(handle, &rxSize, 0, cap);
            if (!item) break;
            vRingbufferReturnItem(handle, item);
        }
    }

private:
    // A2DP sink output is always stereo 16-bit. Discarding whole frames keeps
    // L/R aligned.
    static constexpr size_t kChannels   = 2;
    static constexpr size_t kFrameBytes = kChannels * sizeof(int16_t);

    // Reads up to `samples` int16_t values, looping because the ESP-IDF
    // ringbuf returns a smaller chunk than requested at its wrap-around point.
    size_t readRaw(int16_t *out, size_t samples) {
        size_t totalRead = 0;
        while (totalRead < samples) {
            size_t want = (samples - totalRead) * sizeof(int16_t);
            size_t rxSize = 0;
            void *item = xRingbufferReceiveUpTo(handle, &rxSize, 0, want);
            if (!item) break;
            memcpy(out + totalRead, item, rxSize);
            totalRead += rxSize / sizeof(int16_t);
            vRingbufferReturnItem(handle, item);
        }
        return totalRead;
    }

    void discard(size_t bytes) {
        while (bytes > 0) {
            size_t rxSize = 0;
            void *item = xRingbufferReceiveUpTo(handle, &rxSize, 0, bytes);
            if (!item) break;
            bytes -= rxSize;
            vRingbufferReturnItem(handle, item);
        }
    }

    size_t          cap;
    size_t          prebuffer;
    size_t          highWaterMark;
    RingbufHandle_t handle        = nullptr;
    volatile uint32_t dropCount     = 0;
    uint32_t          underrunCount = 0;
    bool            primed        = false;
    bool            catchingUp    = false;
};

// Defined in main.cpp when BLUETOOTH_MODE is enabled.
extern BTRingBuffer btAudioBuffer;
#endif // BLUETOOTH_MODE

// ─── DelayMixerStream ─────────────────────────────────────────────────────────
// Wraps a PreallocDelay into an AudioTools ModifyingStream.
// Mixing pipeline:
//   dry signal → delay send → delay process → wet return
//   output = dry + wet [+ bt (BT mode only)]
class DelayMixerStream : public ModifyingStream {
public:
    DelayMixerStream() = default;

    void begin(Print &out, PreallocDelay &delayRef, AudioInfo info) {
        setAudioInfo(info);
        setOutput(out);
        setDelay(delayRef);

        // Pre-allocate processing buffers at startup — no heap allocs during audio.
        temp16.resize(kMaxSamples);
#ifdef BLUETOOTH_MODE
        btTemp.resize(kMaxSamples);
#endif
    }

    void setDelay(PreallocDelay &d) {
        delay = &d;
        delay->setActive(true);
        if (audioInfo.sample_rate > 0) delay->setSampleRate(audioInfo.sample_rate);
    }

    void setMix(float dry, float send, float wet) {
        dryLevel  = clamp01(dry);
        sendLevel = clamp01(send);
        wetLevel  = clamp01(wet);
    }

    void sendEnabled(bool enabled) {
        sendLevel = enabled ? 0.9f : 0.0f;
    }

#ifdef BLUETOOTH_MODE
    void setBtLevel(float level) {
        btLevel = clamp01(level);
    }

    // Low-pass on the BT signal only. Any task; the cutoff glides there in
    // write() (audioTask), like the sample insert filter.
    void setBtFilterTarget(float hz, float q) {
        btFilter.setTarget(hz, q);
    }
#endif

    void setAudioInfo(AudioInfo ai) override {
        AudioStream::setAudioInfo(ai);
        audioInfo   = ai;
        sampleBytes = std::max<int>(1, static_cast<int>(ai.bits_per_sample / 8));
        channels    = std::max<int>(1, static_cast<int>(ai.channels));
        frameBytes  = static_cast<size_t>(sampleBytes * channels);
        if (delay && ai.sample_rate > 0) delay->setSampleRate(ai.sample_rate);
    }

    void setStream(Stream &in) override { p_in = &in; }
    void setOutput(Print &out) override { p_out = &out; }

    // Pump silence through the delay so the tail keeps draining after a button
    // release. Also carries BT audio when the sample player is stopped.
    void pumpSilenceFrames(size_t frames) {
        if (frames == 0) return;
        size_t byteCount = frames
                         * static_cast<size_t>(std::max<int>(1, channels))
                         * static_cast<size_t>(sampleBytes);
        // Const zeros live in flash (.rodata), not in heap/DRAM.
        static const uint8_t kSilence[kMaxSamples * sizeof(int16_t)] = {};
        byteCount = std::min(byteCount, sizeof(kSilence));
        write(kSilence, byteCount);
    }

    // Core processing: dry signal → delay send → mix wet → output
    size_t write(const uint8_t *data, size_t len) override {
        if (!p_out || !delay) return 0;
        if (sampleBytes != 2 || len == 0) return 0; // PCM16 only

        const size_t samples = len / sizeof(int16_t);
        if (samples == 0) return 0;

        const size_t frames = samples / static_cast<size_t>(std::max<int>(1, channels));
        if (frames == 0) return 0;

        const size_t totalSamples = frames * static_cast<size_t>(channels);
        if (totalSamples > temp16.size()) return 0; // safety — should never happen

        const int16_t *in = reinterpret_cast<const int16_t *>(data);

#ifdef BLUETOOTH_MODE
        // Pre-fill BT temp buffer with silence; overwrite with ring buffer data.
        std::fill(btTemp.begin(), btTemp.begin() + totalSamples, int16_t(0));
        btAudioBuffer.read(btTemp.data(), totalSamples);

        // Always run the BT filter (fully open when off) so engaging it glides
        // instead of switching in with stale filter state.
        btFilter.tick(static_cast<float>(audioInfo.sample_rate));
        for (size_t i = 0; i < totalSamples; i += static_cast<size_t>(channels)) {
            btTemp[i] = static_cast<int16_t>(btFilterL.process(btTemp[i]));
            if (channels > 1)
                btTemp[i + 1] = static_cast<int16_t>(btFilterR.process(btTemp[i + 1]));
        }
#endif

        // False in BT mode (no delay buffer) or if allocation failed.
        const bool delayActive = delay->isAllocated();

        for (size_t f = 0; f < frames; ++f) {
            const size_t base = f * static_cast<size_t>(channels);

            if (!delayActive) {
                // No delay buffer: bypass all delay math, just mix dry (+ BT).
                for (int ch = 0; ch < channels; ++ch) {
                    float mixed = dryLevel * static_cast<float>(in[base + ch]);
#ifdef BLUETOOTH_MODE
                    mixed += btLevel * static_cast<float>(btTemp[base + ch]);
#endif
                    temp16[base + ch] = clamp16(mixed);
                }
                continue;
            }

            // Compute mono send from average of all channels.
            float monoSum = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                monoSum += static_cast<float>(in[base + ch]);
            float mono = monoSum / static_cast<float>(channels);

            // Run mono through the delay.
            int16_t  sendSample = clamp16(sendLevel * mono);
            effect_t wetSample  = delay->process(sendSample);

            // Reconstruct per-channel output.
            for (int ch = 0; ch < channels; ++ch) {
                float x     = static_cast<float>(in[base + ch]);
                float mixed = dryLevel * x
                            + wetLevel * static_cast<float>(wetSample);
#ifdef BLUETOOTH_MODE
                mixed += btLevel * static_cast<float>(btTemp[base + ch]);
#endif
                temp16[base + ch] = clamp16(mixed);
            }
        }

        const size_t bytes = totalSamples * sizeof(int16_t);
        return p_out->write(reinterpret_cast<const uint8_t *>(temp16.data()), bytes);
    }

private:
    PreallocDelay *delay = nullptr;

    float dryLevel  = 0.9f;
    float sendLevel = 0.9f;
    float wetLevel  = 1.0f;
#ifdef BLUETOOTH_MODE
    // Unity gain — matches the level of the old direct-to-I2S BT path.
    float btLevel   = 1.0f;
#endif

    AudioInfo audioInfo{44100, 2, 16};
    int       sampleBytes = 2;
    int       channels    = 2;
    size_t    frameBytes  = 4;

    Stream *p_in  = nullptr;
    Print  *p_out = nullptr;

    // Processing buffers sized for 512 frames × 2 channels (I2S buffer_size=512).
    static constexpr size_t kMaxSamples = 512 * 2;
    std::vector<int16_t> temp16;
#ifdef BLUETOOTH_MODE
    std::vector<int16_t> btTemp;
    SmoothedLowPass btFilter; // declared before L/R: they reference its coefficients
    LowPassDF1      btFilterL{btFilter.coeffs()};
    LowPassDF1      btFilterR{btFilter.coeffs()};
#endif

    static float clamp01(float v) {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }
    static int16_t clamp16(float v) {
        if (v >  32767.0f) return  32767;
        if (v < -32768.0f) return -32768;
        return static_cast<int16_t>(v);
    }
};
