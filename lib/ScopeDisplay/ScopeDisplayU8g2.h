#ifndef SCOPEDISPLAY_U8G2_H
#define SCOPEDISPLAY_U8G2_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <algorithm>
#include <cmath>
#include <functional>

#include "config.h"
#include "storage/logo.h"

class ScopeDisplayU8g2 {
  private:
  U8G2* display;
  TaskHandle_t displayTaskHandle;
  SemaphoreHandle_t displayMutex;
  SemaphoreHandle_t scopeMutex;   // guards waveform buffer only, separate from display I2C lock
  volatile bool suspended = false;
  std::function<void(U8G2*)> _hudCallback;

  int16_t* waveformBuffer;
  int* waveformIndex;
  int waveformSamples;
  
  
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

        // Snapshot the waveform buffer under the lightweight scopeMutex so
        // the display render (slow I2C) never blocks audio capture.
        int16_t snapBuf[NUM_WAVEFORM_SAMPLES];
        int     snapIndex = 0;
        if (scopeMutex != NULL && xSemaphoreTake(scopeMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
          memcpy(snapBuf, waveformBuffer, waveformSamples * sizeof(int16_t));
          snapIndex = *waveformIndex;
          xSemaphoreGive(scopeMutex);
        }

        if (displayMutex != NULL && xSemaphoreTake(displayMutex, portMAX_DELAY) == pdTRUE) {
          display->clearBuffer();
          renderWaveform(snapBuf, snapIndex);
          if (_hudCallback) _hudCallback(display);
          display->sendBuffer();
          xSemaphoreGive(displayMutex);
        }

        // ~50 fps — keep low enough that BT stack (prio 5) is never starved
        vTaskDelay(20 / portTICK_PERIOD_MS);
      }
    }

    void renderWaveform(const int16_t* buf, int snapIndex) {
      const int scopeCenter = kScreenHeight / 2;
      if (waveformSamples <= 0 || !buf) return;

      int displayedSamples = std::max(1, static_cast<int>(waveformSamples / horizZoom));
      displayedSamples = std::min(displayedSamples, waveformSamples);

      float step = (displayedSamples > 1 && kScreenWidth > 1)
                    ? static_cast<float>(displayedSamples - 1) / static_cast<float>(kScreenWidth - 1)
                    : 0.0f;

      int newestIndex = (snapIndex - 1 + waveformSamples) % waveformSamples;
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
      // (Simplified — no exponential smoothing)
      auto drawOverlay = [&](int x, float yValue, float &prevY) {
        int y = constrain(static_cast<int>(round(yValue)), 0, kScreenHeight - 1);
        if (!isfinite(prevY)) {
          display->drawPixel(x, y);
        } else {
          display->drawLine(x - 1, static_cast<int>(round(prevY)), x, y);
        }
        prevY = static_cast<float>(y);
      };

      for (int x = 0; x < kScreenWidth; ++x) {
        float samplePos = startIndex + x * step;
        int centerIdx = static_cast<int>(floor(samplePos));
        float frac = samplePos - static_cast<float>(centerIdx);
        centerIdx %= waveformSamples; if (centerIdx < 0) centerIdx += waveformSamples;

        int winSum = 0, winCount = 0;
        for (int w = -halfWin; w <= halfWin; ++w) {
          int i = (centerIdx + w) % waveformSamples; if (i < 0) i += waveformSamples;
          winSum += buf[i];
          ++winCount;
        }
        int nextIdx = (centerIdx + 1) % waveformSamples;
        float sampleCenter = static_cast<float>(winSum) / static_cast<float>(winCount);
        float sampleNext = static_cast<float>(buf[nextIdx]);
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
    // HUD callback — called each frame after the waveform is drawn but before
    // sendBuffer(), so HUD elements appear in the same frame as the waveform.
    void setHudCallback(std::function<void(U8G2*)> cb) { _hudCallback = std::move(cb); }

    // Allow external control of horizontal zoom and vertical scale so UI
    // settings can affect the scope rendering.
    void setHorizZoom(float hz) { horizZoom = hz; }
    void setVertScale(float vs) { vertScale = vs; }
    // Set delay parameters used for echo visualization (delay in ms, feedback 0..1)


    void setSuspended(bool value) {
      suspended = value;
      // when resuming, give display a clean clear so splash/overlays don't remain
      if (!value) {
        if (displayMutex != NULL && xSemaphoreTake(displayMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
          display->clearBuffer();
          display->sendBuffer();
          xSemaphoreGive(displayMutex);
        } else {
          // best-effort fallback
          display->clearBuffer();
          display->sendBuffer();
        }
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
      scopeMutex   = xSemaphoreCreateMutex();
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
        4096,   
        this,
        2,      // prio 2: above inputTask (1), below BT stack (5)
        &displayTaskHandle,
        0
      );
      return true;
    }



    SemaphoreHandle_t* getMutex()      { return &displayMutex; }
    SemaphoreHandle_t* getScopeMutex() { return &scopeMutex; }
};

#endif
