#pragma once

#include <Arduino.h>
#include <AudioTools.h>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffect.h"
#include "AudioTools/CoreAudio/AudioFilter/Filter.h"
#include <AudioTools/AudioLibs/AudioEffectsSuite.h>
#include <cmath>
#include <vector>

// Mixer/routing:
// player -> filter -> split
//   1) dry direct naar output
//   2) send naar delay -> wet return -> mix -> output
// Daarnaast kan er ook na het stoppen nog tail uit de delay komen via
// renderTailFrames().
class FilteredDelayMixerStream : public ModifyingStream {
public:
    FilteredDelayMixerStream() = default;

    // Snelle helper om alles in één keer te koppelen.
    void begin(Print &out, LowPassFilter<float> &filterRef, Delay &delayRef, AudioInfo info) {
        setAudioInfo(info);
        setOutput(out);
        setFilter(filterRef);
        setDelay(delayRef);
    }

    void setFilter(LowPassFilter<float> &f) { filter = &f; }

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
        if (!p_out || !filter || !delay) return 0;
        if (sampleBytes != 2 || len == 0) return 0; // only PCM16

        const size_t samples = len / sizeof(int16_t);
        const int16_t *in = reinterpret_cast<const int16_t *>(data);
        temp16.resize(samples);

        for (size_t i = 0; i < samples; ++i) {
            float x = static_cast<float>(in[i]);
            float filtered = filter->process(x);              // filter vóór split

            // Send naar delay: clamp naar int16 omdat Delay int16 verwacht
            int16_t sendSample = clamp16(sendLevel * filtered);
            effect_t wet = delay->process(sendSample);

            float mixed = dryLevel * filtered + wetLevel * static_cast<float>(wet);
            temp16[i] = clamp16(mixed);
            updateTailEnergy(static_cast<float>(wet));
        }

        size_t bytes = samples * sizeof(int16_t);
        return p_out->write(reinterpret_cast<const uint8_t *>(temp16.data()), bytes);
    }

    // Render alleen de tail van de delay (bijv. aanroepen wanneer de player geen
    // data meer stuurt). Returned aantal bytes dat geschreven is.
    size_t renderTailFrames(size_t frames = 128) {
        if (!p_out || !delay || frameBytes == 0) return 0;
        if (!tailActive()) return 0;

        const size_t samples = frames * static_cast<size_t>(channels);
        temp16.resize(samples);

        for (size_t i = 0; i < samples; ++i) {
            effect_t wet = delay->process(0); // 0-input; alleen buffer/tail
            float mixed = wetLevel * static_cast<float>(wet);
            temp16[i] = clamp16(mixed);
            updateTailEnergy(static_cast<float>(wet));
        }

        size_t bytes = samples * sizeof(int16_t);
        return p_out->write(reinterpret_cast<const uint8_t *>(temp16.data()), bytes);
    }

    bool tailActive() const { return tailEnergy > tailThreshold; }

    void flush() override {
        // Probeer de tail nog even uit te schrijven maar voorkom lange blokkeertijd
        const size_t maxBatches = 4;
        size_t batches = 0;
        while (tailActive() && batches < maxBatches) {
            renderTailFrames(64);
            batches++;
            tailEnergy *= tailHold; // extra demping om af te kappen
        }
        if (p_out) p_out->flush();
    }

private:
    LowPassFilter<float> *filter = nullptr;
    Delay *delay = nullptr;

    float dryLevel = 1.0f;
    float sendLevel = 1.0f;
    float wetLevel = 0.7f;

    AudioInfo audioInfo{44100, 2, 16};
    int sampleBytes = 2;
    int channels = 2;
    size_t frameBytes = 4;

    Stream *p_in = nullptr;
    Print *p_out = nullptr;

    std::vector<int16_t> temp16;

    float tailEnergy = 0.0f;
    const float tailHold = 0.9995f;     // hoe snel de tail-meter zakt
    const float tailThreshold = 0.5f;   // grens voor tailActive()

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

    void updateTailEnergy(float sample) {
        float absSample = fabsf(sample);
        tailEnergy = std::max(absSample, tailEnergy * tailHold);
    }
};
