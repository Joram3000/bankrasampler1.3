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

// ─── BTRingBuffer ─────────────────────────────────────────────────────────────
// ESP-IDF RTOS ring buffer — lock-free between cores, no custom mutex needed.
class BTRingBuffer {
public:
    explicit BTRingBuffer(size_t capacity = 8192) : cap(capacity) {}

    void begin() {
        if (handle) return;
        handle = xRingbufferCreate(cap, RINGBUF_TYPE_BYTEBUF);
    }

    // Resize the ring buffer — called from audioTask (Core 1).
    // Pauses writes briefly so Core 0 cannot use the old handle after deletion.
    bool resize(size_t newCap) {
        paused_ = true;
        __sync_synchronize();
        vTaskDelay(pdMS_TO_TICKS(3)); // let any in-flight write() on Core 0 finish

        RingbufHandle_t old = handle;
        handle = nullptr;
        __sync_synchronize();

        handle = xRingbufferCreate(newCap, RINGBUF_TYPE_BYTEBUF);
        if (handle) cap = newCap;
        else        handle = old, old = nullptr; // restore on failure

        __sync_synchronize();
        paused_ = false;

        if (old) vRingbufferDelete(old);
        return handle != nullptr;
    }

    // Called from the BT stack task (Core 0) — must never block.
    // Captures handle once so Core 1 resize() cannot swap it mid-call.
    size_t write(const uint8_t *data, size_t len) {
        RingbufHandle_t h = handle; // atomic 32-bit load
        if (!h || paused_) return 0;
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

    // Called from audioTask (Core 1).
    // Reads exactly `samples` int16_t values, looping until satisfied or
    // the buffer is empty. Partial reads are filled in by looping — this
    // prevents silence gaps when the ESP-IDF ringbuf returns a smaller
    // chunk than requested (happens at internal wrap-around boundary).
    size_t read(int16_t *out, size_t samples) {
        if (!handle || samples == 0) return 0;
        size_t totalRead = 0;
        while (totalRead < samples) {
            size_t want = (samples - totalRead) * sizeof(int16_t);
            size_t rxSize = 0;
            void *item = xRingbufferReceiveUpTo(handle, &rxSize, 0, want);
            if (!item) break;
            rxSize -= rxSize % sizeof(int16_t); // keep int16_t alignment
            if (rxSize > 0) {
                memcpy(out + totalRead, item, rxSize);
                totalRead += rxSize / sizeof(int16_t);
            }
            vRingbufferReturnItem(handle, item);
        }
        return totalRead;
    }

    size_t available() const {
        if (!handle) return 0;
        return cap - xRingbufferGetCurFreeSize(handle);
    }

    // Actual capacity of the underlying ring buffer (may be < requested size
    // if resize() failed — callers should use this, not a hardcoded constant).
    size_t capacity() const { return cap; }

    // Discard all pending data. Called from audioTask when switching modes.
    void clear() {
        if (!handle) return;
        for (;;) {
            size_t rxSize = 0;
            void *item = xRingbufferReceiveUpTo(handle, &rxSize, 0, cap);
            if (!item) break;
            vRingbufferReturnItem(handle, item);
        }
    }

private:
    size_t          cap;
    RingbufHandle_t handle    = nullptr;
    uint32_t        dropCount = 0;
    volatile bool   paused_   = false;
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
        // Sized for 512 frames × 2 channels to match I2S buffer_size=512.
        const size_t maxSamples = 512 * 2;
        temp16.resize(maxSamples);
        silenceBytes.assign(maxSamples * sizeof(int16_t), 0);
#ifdef BLUETOOTH_MODE
        btTemp.resize(maxSamples);
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
        byteCount = std::min(byteCount, silenceBytes.size());
        write(silenceBytes.data(), byteCount);
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
#endif

#ifdef BLUETOOTH_MODE
        const bool delayActive = delay->isAllocated();
#else
        constexpr bool delayActive = true;
#endif

        for (size_t f = 0; f < frames; ++f) {
            const size_t base = f * static_cast<size_t>(channels);

#ifdef BLUETOOTH_MODE
            if (!delayActive) {
                // Delay buffer freed (BT connected): bypass all delay math,
                // just mix dry + BT directly.
                for (int ch = 0; ch < channels; ++ch) {
                    float mixed = dryLevel * static_cast<float>(in[base + ch])
                                + btLevel  * static_cast<float>(btTemp[base + ch]);
                    temp16[base + ch] = clamp16(mixed);
                }
                continue;
            }
#endif

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
    float btLevel   = 0.8f;
#endif

    AudioInfo audioInfo{44100, 2, 16};
    int       sampleBytes = 2;
    int       channels    = 2;
    size_t    frameBytes  = 4;

    Stream *p_in  = nullptr;
    Print  *p_out = nullptr;

    std::vector<int16_t> temp16;
    std::vector<uint8_t> silenceBytes;
#ifdef BLUETOOTH_MODE
    std::vector<int16_t> btTemp;
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
