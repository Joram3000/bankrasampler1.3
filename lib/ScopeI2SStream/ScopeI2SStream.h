#pragma once

#include <AudioTools.h>
#include <algorithm>

#include "config.h"

/**
 * Custom output stream die samples captured voor waveform display
 * Intercepteert audio data op weg naar I2S voor visualisatie
 */
class ScopeI2SStream : public I2SStream {
  private:
    int16_t* waveformBuffer;
    int* waveformIndex;
    SemaphoreHandle_t* mutex;
    int downsampleRate;
    float amplitudeGamma = 0.5f; // Schaalfactor voor amplitude (wortel)
    int sampleBytes = sizeof(int16_t);
    int channelCount = 2;
    uint32_t sampleCounter = 0;
    
  public:
    /**
     * Constructor
     * @param buffer Pointer naar waveform buffer array
     * @param index Pointer naar buffer index
     * @param displayMutex Pointer naar mutex voor thread-safe access
     * @param downsample Neem 1 van elke N samples (default: 16)
     */
    ScopeI2SStream(int16_t* buffer, int* index, SemaphoreHandle_t* displayMutex, int downsample = 4) 
      : waveformBuffer(buffer), 
        waveformIndex(index), 
        mutex(displayMutex),
        downsampleRate(downsample) {
    }

    void setAudioInfo(AudioInfo info) override {
  sampleBytes = std::max<int>(1, info.bits_per_sample / 8);
  channelCount = std::max<int>(1, info.channels);
      I2SStream::setAudioInfo(info);
    }
    
    /**
     * Override write() om samples te capturen voor scope display
     */
    size_t write(const uint8_t *data, size_t len) override {
      captureForScope(data, len);
      // Schrijf data door naar I2S hardware
      return I2SStream::write(data, len);
    }

    // Voer expliciet stilte-samples in de scope-buffer in (handig als er geen audio is).
    // frames = aantal audio-frames (per kanaal) om te simuleren.
    void feedSilenceFrames(size_t frames) {
      feedSyntheticFrames(frames, 0.0f);
    }

  private:
    void captureForScope(const uint8_t *data, size_t len) {
      if (waveformBuffer == nullptr || waveformIndex == nullptr || mutex == nullptr) return;
      if (sampleBytes <= 0 || channelCount <= 0) return;

      size_t frameSize = sampleBytes * channelCount;
      if (frameSize == 0) return;
      size_t frames = len / frameSize;
      const uint8_t* framePtr = data;

      for (size_t frame = 0; frame < frames; ++frame) {
        if (sampleCounter++ % downsampleRate == 0) {
          float norm = extractLeftChannelNormalized(framePtr);
          if (xSemaphoreTake(*mutex, 0)) {
            float scaled = powf(fabsf(norm), amplitudeGamma);
            if (norm < 0) scaled = -scaled;
            int16_t out = (int16_t)(scaled * 32767.0f);
            waveformBuffer[*waveformIndex] = out;
            *waveformIndex = (*waveformIndex + 1) % NUM_WAVEFORM_SAMPLES;
            xSemaphoreGive(*mutex);
          }
        }
        framePtr += frameSize;
      }
    }

    void feedSyntheticFrames(size_t frames, float normalizedValue) {
      if (waveformBuffer == nullptr || waveformIndex == nullptr || mutex == nullptr) return;
      if (frames == 0) return;

      if (xSemaphoreTake(*mutex, 0)) {
        float scaled = powf(fabsf(normalizedValue), amplitudeGamma);
        if (normalizedValue < 0) scaled = -scaled;
        int16_t out = static_cast<int16_t>(scaled * 32767.0f);
        for (size_t frame = 0; frame < frames; ++frame) {
          if (sampleCounter++ % downsampleRate == 0) {
            waveformBuffer[*waveformIndex] = out;
            *waveformIndex = (*waveformIndex + 1) % NUM_WAVEFORM_SAMPLES;
          }
        }
        xSemaphoreGive(*mutex);
      }
    }

    float extractLeftChannelNormalized(const uint8_t* framePtr) const {
      if (sampleBytes == sizeof(int16_t)) {
        const int16_t* sample16 = reinterpret_cast<const int16_t*>(framePtr);
        return static_cast<float>(*sample16) / 32768.0f;
      }
      if (sampleBytes == sizeof(int32_t)) {
        const int32_t* sample32 = reinterpret_cast<const int32_t*>(framePtr);
        return static_cast<float>(*sample32) / 2147483648.0f;
      }
      // fallback: treat as unsigned byte stream centered at 0
      int32_t accum = 0;
      for (int i = 0; i < sampleBytes; ++i) {
        accum |= static_cast<int32_t>(framePtr[i]) << (8 * i);
      }
      return static_cast<float>(accum) / 2147483648.0f;
    }
};

