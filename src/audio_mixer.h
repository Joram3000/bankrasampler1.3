#pragma once

#include <Arduino.h>
#include <AudioTools.h>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffect.h"
#include <AudioTools/AudioLibs/AudioEffectsSuite.h>
#include "prealloc_delay.h"
#include <cmath>
#include <vector>

class DelayMixerStream : public ModifyingStream {
public:
    DelayMixerStream() = default;

    void begin(Print &out, PreallocDelay &delayRef1, AudioInfo info) {
        setAudioInfo(info);
        setOutput(out);
        setDelay(delayRef1);
    }

    void setDelay(PreallocDelay &d) {
        delay = &d;
        delay->setActive(true);
        if (audioInfo.sample_rate > 0) delay->setSampleRate(audioInfo.sample_rate);
    }


    void setMix(float dry, float send, float wet) {
        dryLevel = clamp01(dry);
        sendLevel = clamp01(send);
        wetLevel = clamp01(wet);
    }

void sendEnabled(bool enabled) {
        sendLevel = enabled ? 0.9f : 0.0f;
    }

    void setAudioInfo(AudioInfo info) override {
        AudioStream::setAudioInfo(info);
        audioInfo = info;
        sampleBytes = std::max<int>(1, static_cast<int>(info.bits_per_sample / 8));
        channels = std::max<int>(1, static_cast<int>(info.channels));
        frameBytes = static_cast<size_t>(sampleBytes * channels);
        if (delay && info.sample_rate > 0) delay->setSampleRate(info.sample_rate);
    }

    void setStream(Stream &in) override { p_in = &in; }

    void setOutput(Print &out) override { p_out = &out; }

  void pumpSilenceFrames(size_t frames) {
    if (frames == 0) return;
    size_t sampleCount = frames * std::max<int>(1, channels);
    size_t byteCount = sampleCount * static_cast<size_t>(sampleBytes);
    // allocate a temporary zero buffer on the heap to avoid stack pressure
    std::vector<uint8_t> zeros(byteCount);
    // write will call the CallbackStream which will call our updateCallback
    // and thus call delay->process(0) for each frame.
    write(zeros.data(), byteCount);
  }

    // Core processing: filter -> split (dry + send to delay) -> mix -> output
    size_t write(const uint8_t *data, size_t len) override {
        if (!p_out || !delay) return 0;
        if (sampleBytes != 2 || len == 0) return 0; // only PCM16

        const size_t samples = len / sizeof(int16_t);
        if (samples == 0) return 0;

        // Treat samples as interleaved channels; process per frame and make a mono send.
        const size_t frames = samples / static_cast<size_t>(std::max<int>(1, channels));
        if (frames == 0) return 0;

        const int16_t *in = reinterpret_cast<const int16_t *>(data);
        temp16.resize(frames * static_cast<size_t>(channels));

        for (size_t f = 0; f < frames; ++f) {
            // compute mono send as the average of all channel samples for this frame
            float monoSum = 0.0f;
            for (int ch = 0; ch < channels; ++ch) {
                monoSum += static_cast<float>(in[f * static_cast<size_t>(channels) + ch]);
            }
            float mono = monoSum / static_cast<float>(channels);

            // Send één mono sample naar de delay
            int16_t sendSample = clamp16(sendLevel * mono);
            effect_t wet = delay->process(sendSample);

            // Mix wet (mono) back into every channel, keep dry per-channel
            for (int ch = 0; ch < channels; ++ch) {
                float x = static_cast<float>(in[f * static_cast<size_t>(channels) + ch]);
                float mixed = dryLevel * x + wetLevel * static_cast<float>(wet);
                temp16[f * static_cast<size_t>(channels) + ch] = clamp16(mixed);
            }
        }

        size_t bytes = frames * static_cast<size_t>(channels) * sizeof(int16_t);
        return p_out->write(reinterpret_cast<const uint8_t *>(temp16.data()), bytes);
    }

private:
    PreallocDelay *delay = nullptr;

    float dryLevel = 0.9f;
    float sendLevel = 0.9f;
    float wetLevel = 1.0f;

    AudioInfo audioInfo{44100, 2, 16};
    int sampleBytes = 2;
    int channels = 2;
    size_t frameBytes = 4;

    Stream *p_in = nullptr;
    Print *p_out = nullptr;

    std::vector<int16_t> temp16;


    static float clamp01(float v) {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }

    static int16_t clamp16(float v) {
        if (v > 32767.0f) return 32767;
        if (v < -32768.0f) return -32768;
        return static_cast<int16_t>(v);
    }

};
