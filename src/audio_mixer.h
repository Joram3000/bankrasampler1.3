#pragma once

#include <Arduino.h>
#include <AudioTools.h>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffect.h"
#include <AudioTools/AudioLibs/AudioEffectsSuite.h>
#include <cmath>
#include <vector>

class DelayMixerStream : public ModifyingStream {
public:
    DelayMixerStream() = default;

    void begin(Print &out, Delay &delayRef, AudioInfo info) {
        setAudioInfo(info);
        setOutput(out);
        setDelay(delayRef);
    }

    void setDelay(Delay &d) {
        delay = &d;
        delay->setActive(true);
        if (audioInfo.sample_rate > 0) delay->setSampleRate(audioInfo.sample_rate);
    }

    // dry = direct, send = hoeveel de delay in gaat, wet = terugkomend niveau
    void setMix(float dry, float wet) {
        setMix(dry, wet, wet); // backward compatible: send = wet
    }

       void setMix(float dry, float send, float wet) {
        dryLevel = clamp01(dry);
        sendLevel = clamp01(send);
        wetLevel = clamp01(wet);
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

    // Core processing: filter -> split (dry + send to delay) -> mix -> output
    size_t write(const uint8_t *data, size_t len) override {
        if (!p_out || !delay) return 0;
        if (sampleBytes != 2 || len == 0) return 0; // only PCM16

        const size_t samples = len / sizeof(int16_t);
        const int16_t *in = reinterpret_cast<const int16_t *>(data);
        temp16.resize(samples);

        for (size_t i = 0; i < samples; ++i) {
            float x = static_cast<float>(in[i]);
       
            // Send naar delay: clamp naar int16 omdat Delay int16 verwacht
            int16_t sendSample = clamp16(sendLevel * x);
            effect_t wet = delay->process(sendSample);

            float mixed = dryLevel * x + wetLevel * static_cast<float>(wet);
            temp16[i] = clamp16(mixed);
        }

        size_t bytes = samples * sizeof(int16_t);
        return p_out->write(reinterpret_cast<const uint8_t *>(temp16.data()), bytes);
    }

private:
    Delay *delay = nullptr;

    float dryLevel = 1.0f;
    float sendLevel = 1.0f;
    float wetLevel = 0.8f;

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
