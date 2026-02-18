#ifndef SCOPEDISPLAY_U8G2_H
#define SCOPEDISPLAY_U8G2_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <algorithm>
#include <cmath>

#include "config.h"


class ScopeDisplayU8g2 {
  private:
  U8G2* display;
  TaskHandle_t displayTaskHandle;
  SemaphoreHandle_t displayMutex;
  volatile bool suspended = false;
  
  int16_t* waveformBuffer;
  int* waveformIndex;
  int waveformSamples;
  
  const bool useSmoothing = false; // naar settings?
  const float smoothingAlpha = 0.2f;// kan weg?
  
  float horizZoom = DEFAULT_HORIZ_ZOOM;
    float vertScale = DEFAULT_VERT_SCALE;
    float lastDisplayY = NAN;

    String currentFile;
    bool isPlaying;

    static constexpr int kScreenWidth = DISPLAY_WIDTH;
    static constexpr int kScreenHeight = DISPLAY_HEIGHT;

    static void displayTaskImpl(void* parameter) {
      auto* self = static_cast<ScopeDisplayU8g2*>(parameter);
      self->displayLoop();
    }

    void displayLoop() {
      for (;;) {
        if (suspended) {
          vTaskDelay(40 / portTICK_PERIOD_MS);
          continue;
        }
        if (xSemaphoreTake(displayMutex, portMAX_DELAY)) {
          display->clearBuffer();
          renderWaveform();
      
          display->sendBuffer();
          xSemaphoreGive(displayMutex);
        }
        vTaskDelay(40 / portTICK_PERIOD_MS);
      }
    }

    void renderWaveform() {
      const int scopeCenter = kScreenHeight / 2;
      if (waveformSamples <= 0 || !waveformBuffer) return;

      int displayedSamples = std::max(1, static_cast<int>(waveformSamples / horizZoom));
      displayedSamples = std::min(displayedSamples, waveformSamples);

      float step = (displayedSamples > 1 && kScreenWidth > 1)
                    ? static_cast<float>(displayedSamples - 1) / static_cast<float>(kScreenWidth - 1)
                    : 0.0f;

      int newestIndex = ((*waveformIndex - 1) + waveformSamples) % waveformSamples;
      int startIndex = newestIndex - displayedSamples + 1;
      if (startIndex < 0) startIndex += waveformSamples;

      int windowSize = std::max(1, static_cast<int>(ceilf((displayedSamples / (float)kScreenWidth))));
      int halfWin = (windowSize - 1) / 2;

      // previous Y for main use (kept for compatibility with lastDisplayY)
      float prevYf = isfinite(lastDisplayY) ? lastDisplayY : NAN;

      // Minder overlays = minder RAM
      constexpr float overlayMultipliers[] = {
        1.0f,     // full
        2.0f/3.0f,
        1.0f/3.0f
      };
      constexpr int kNumOverlays = static_cast<int>(sizeof(overlayMultipliers) / sizeof(overlayMultipliers[0]));

      // prev Y states for each overlay
      float prevYs[kNumOverlays];
      for (int i = 0; i < kNumOverlays; ++i) prevYs[i] = NAN;

      // Helper to draw one overlay given multiplier and prevY reference
      auto drawOverlay = [&](int x, float yValue, float &prevY) {
        if (useSmoothing) {
          if (!isfinite(prevY)) prevY = yValue;
          float smoothY = (smoothingAlpha * yValue) + ((1.0f - smoothingAlpha) * prevY);
          int yPrev = constrain(static_cast<int>(round(prevY)), 0, kScreenHeight - 1);
          int yCur  = constrain(static_cast<int>(round(smoothY)), 0, kScreenHeight - 1);
          if (x == 0) {
            display->drawPixel(x, yCur);
          } else {
            display->drawLine(x - 1, yPrev, x, yCur);
          }
          prevY = smoothY;
        } else {
          int y = constrain(static_cast<int>(round(yValue)), 0, kScreenHeight - 1);
          if (!isfinite(prevY)) {
            display->drawPixel(x, y);
          } else {
            display->drawLine(x - 1, static_cast<int>(round(prevY)), x, y);
          }
          prevY = static_cast<float>(y);
        }
      };

      for (int x = 0; x < kScreenWidth; ++x) {
        float samplePos = startIndex + x * step;
        int centerIdx = static_cast<int>(floor(samplePos));
        float frac = samplePos - static_cast<float>(centerIdx);
        centerIdx %= waveformSamples; if (centerIdx < 0) centerIdx += waveformSamples;

        int winSum = 0, winCount = 0;
        for (int w = -halfWin; w <= halfWin; ++w) {
          int i = (centerIdx + w) % waveformSamples; if (i < 0) i += waveformSamples;
          winSum += waveformBuffer[i];
          ++winCount;
        }
        int nextIdx = (centerIdx + 1) % waveformSamples;
        float sampleCenter = static_cast<float>(winSum) / static_cast<float>(winCount);
        float sampleNext = static_cast<float>(waveformBuffer[nextIdx]);
        float val = sampleCenter * (1.0f - frac) + sampleNext * frac;

        // base amplitude once
        float baseAmp = (val * ((kScreenHeight / 2) * vertScale) / 32768.0f);

        // Draw each overlay using its multiplier
        for (int o = 0; o < kNumOverlays; ++o) {
          float yf = scopeCenter - (baseAmp * overlayMultipliers[o]);
          drawOverlay(x, yf, prevYs[o]);
        }
      }

      // keep compatibility: store last shown main overlay (first overlay = full)
      lastDisplayY = prevYs[0];
    }
  
  public:
    // Allow external control of horizontal zoom and vertical scale so UI
    // settings can affect the scope rendering.
    void setHorizZoom(float hz) { horizZoom = hz; }
    void setVertScale(float vs) { vertScale = vs; }
    // Set delay parameters used for echo visualization (delay in ms, feedback 0..1)


    void setSuspended(bool value) {
      suspended = value;
      if (!value) {
        lastDisplayY = NAN;
      }
    }

    ScopeDisplayU8g2(U8G2* disp, int16_t* waveBuffer, int* waveIdx)
      : ScopeDisplayU8g2(disp, waveBuffer, waveIdx, NUM_WAVEFORM_SAMPLES) {}

    ScopeDisplayU8g2(U8G2* disp, int16_t* waveBuffer, int* waveIdx, int waveSamples)
      : display(disp),
        displayTaskHandle(nullptr),
        waveformBuffer(waveBuffer),
        waveformIndex(waveIdx),
        waveformSamples(waveSamples),
        currentFile(""),
        isPlaying(false) {
      displayMutex = xSemaphoreCreateMutex();
    }

    bool begin(uint8_t i2cAddress = DISPLAY_I2C_ADDRESS) {
      (void)i2cAddress;
      display->begin();
      display->setPowerSave(0);
      display->setFontMode(0);
      display->setBitmapMode(false);
      display->setDrawColor(1);
      display->sendF("c", 0xA6); 
      display->clearBuffer();
      display->setFont(u8g2_font_5x7_tf);
      display->drawStr(0, 8, "Initializing...");
      display->sendBuffer();

      xTaskCreatePinnedToCore(
        displayTaskImpl,
        "ScopeDisplayU8G2",
        2048,
        this,
        1,
        &displayTaskHandle,
        0
      );
      return true;
    }



    SemaphoreHandle_t* getMutex() {
      return &displayMutex;
    }
};

#endif
