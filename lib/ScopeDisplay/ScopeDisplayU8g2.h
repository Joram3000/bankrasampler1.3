#ifndef SCOPEDISPLAY_U8G2_H
#define SCOPEDISPLAY_U8G2_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <algorithm>
#include <cmath>

#include "config.h"

      const bool useSmoothing = false;
      const float smoothingAlpha = 0.1f;

class ScopeDisplayU8g2 {
  private:
    U8G2* display;
  TaskHandle_t displayTaskHandle;
  SemaphoreHandle_t displayMutex;
  volatile bool suspended = false;

    int16_t* waveformBuffer;
    int* waveformIndex;
    int waveformSamples;

    float horizZoom = DEFAULT_HORIZ_ZOOM;
    float vertScale = DEFAULT_VERT_SCALE;
    float lastDisplayY = NAN;

  // Delay/echo visualization parameters (updated from UI/audio)
  float delayMs = 0.0f;        // delay duration in milliseconds
  float delayFeedback = 0.0f;  // 0..1 feedback amount used to determine number of echoes
  int32_t delaySampleRate = 44100; // sample rate used to convert ms->samples

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

  float prevYf = isfinite(lastDisplayY) ? lastDisplayY : NAN;
  // Separate previous Y values for the additional overlays so each can be
  // smoothed independently without affecting the main waveform.
      float prevYfTwoThird = NAN;
      float prevYfThird = NAN;
      static constexpr int MAX_ECHO_COPIES = 5;
      float prevYfEcho[MAX_ECHO_COPIES];
      int prevXEcho[MAX_ECHO_COPIES];
      for (int i = 0; i < MAX_ECHO_COPIES; ++i) {
        prevYfEcho[i] = NAN;
        prevXEcho[i] = -1;
      }

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

  // Compute base amplitude once and reuse for overlays (1.0, 2/3, 1/3)
  float baseAmp = (val * ((kScreenHeight / 2) * vertScale) / 32768.0f);
  float yf = scopeCenter - baseAmp;
  float yfTwoThird = scopeCenter - (baseAmp * (2.0f/3.0f));
  float yfThird = scopeCenter - (baseAmp * (1.0f/3.0f));

        if (useSmoothing) {
          if (!isfinite(prevYf)) prevYf = yf;
          float smoothY = (smoothingAlpha * yf) + ((1.0f - smoothingAlpha) * prevYf);
          int yPrev = constrain(static_cast<int>(round(prevYf)), 0, kScreenHeight - 1);
          int yCur  = constrain(static_cast<int>(round(smoothY)), 0, kScreenHeight - 1);
          if (x == 0) {
            display->drawPixel(x, yCur);
          } else {
            display->drawLine(x - 1, yPrev, x, yCur);
          }
          prevYf = smoothY;
        } else {
          int y = constrain(static_cast<int>(round(yf)), 0, kScreenHeight - 1);
          if (!isfinite(prevYf)) {
            display->drawPixel(x, y);
          } else {
            display->drawLine(x - 1, static_cast<int>(round(prevYf)), x, y);
          }
          prevYf = static_cast<float>(y);
        }

        // Draw visual echoes (shifted copies) based on delayMs & delayFeedback.
        // We compute a pixel offset from delay (ms) -> samples -> pixels, then draw
        // a small pixel for each echo copy to keep it subtle.
        if (delayMs > 0.5f && delayFeedback > 0.01f && displayedSamples > 0 && delaySampleRate > 0) {
          int nCopies = static_cast<int>(std::ceil(delayFeedback * static_cast<float>(MAX_ECHO_COPIES)));
          nCopies = std::min(std::max(nCopies, 1), MAX_ECHO_COPIES);
          float delaySamples = (delayMs / 1000.0f) * static_cast<float>(delaySampleRate);
          float pxPerSample = static_cast<float>(kScreenWidth) / static_cast<float>(displayedSamples);
          float offsetPx = delaySamples * pxPerSample;
          if (offsetPx < 1.0f) offsetPx = 1.0f;

          for (int c = 1; c <= nCopies; ++c) {
            float txf = static_cast<float>(x) + static_cast<float>(c) * offsetPx;
            int tx = static_cast<int>(roundf(txf));
            if (tx < 0 || tx >= kScreenWidth) continue;

            float decay = 1.0f - (static_cast<float>(c) / static_cast<float>(nCopies + 1));
            float echoY = scopeCenter - (baseAmp * decay);
            float smoothEchoY = echoY;
            if (useSmoothing) {
              if (!isfinite(prevYfEcho[c - 1])) prevYfEcho[c - 1] = echoY;
              smoothEchoY = (smoothingAlpha * echoY) + ((1.0f - smoothingAlpha) * prevYfEcho[c - 1]);
            }
            int yEcho = constrain(static_cast<int>(round(smoothEchoY)), 0, kScreenHeight - 1);
            if (prevXEcho[c - 1] >= 0) {
              int yPrev = constrain(static_cast<int>(round(prevYfEcho[c - 1])), 0, kScreenHeight - 1);
              display->drawLine(prevXEcho[c - 1], yPrev, tx, yEcho);
            } else {
              display->drawPixel(tx, yEcho);
            }
            const int echoHalfHeight = 5;
            int echoTop = constrain(yEcho - echoHalfHeight, 0, kScreenHeight - 1);
            int echoBottom = constrain(yEcho + echoHalfHeight - 1, 0, kScreenHeight - 1);
            display->drawLine(tx, echoTop, tx, echoBottom);
            prevXEcho[c - 1] = tx;
            prevYfEcho[c - 1] = smoothEchoY;
          }
        }

        // Draw the 2/3-amplitude overlay
        if (useSmoothing) {
          if (!isfinite(prevYfTwoThird)) prevYfTwoThird = yfTwoThird;
          float smoothYTwoThird = (smoothingAlpha * yfTwoThird) + ((1.0f - smoothingAlpha) * prevYfTwoThird);
          int yPrevTwoThird = constrain(static_cast<int>(round(prevYfTwoThird)), 0, kScreenHeight - 1);
          int yCurTwoThird  = constrain(static_cast<int>(round(smoothYTwoThird)), 0, kScreenHeight - 1);
          if (x == 0) {
            display->drawPixel(x, yCurTwoThird);
          } else {
            display->drawLine(x - 1, yPrevTwoThird, x, yCurTwoThird);
          }
          prevYfTwoThird = smoothYTwoThird;
        } else {
          int yTwoThird = constrain(static_cast<int>(round(yfTwoThird)), 0, kScreenHeight - 1);
          if (!isfinite(prevYfTwoThird)) {
            display->drawPixel(x, yTwoThird);
          } else {
            display->drawLine(x - 1, static_cast<int>(round(prevYfTwoThird)), x, yTwoThird);
          }
          prevYfTwoThird = static_cast<float>(yTwoThird);
        }

        // Draw the 1/3-amplitude overlay (a bit subtler)
        if (useSmoothing) {
          if (!isfinite(prevYfThird)) prevYfThird = yfThird;
          float smoothYThird = (smoothingAlpha * yfThird) + ((1.0f - smoothingAlpha) * prevYfThird);
          int yPrevThird = constrain(static_cast<int>(round(prevYfThird)), 0, kScreenHeight - 1);
          int yCurThird  = constrain(static_cast<int>(round(smoothYThird)), 0, kScreenHeight - 1);
          if (x == 0) {
            display->drawPixel(x, yCurThird);
          } else {
            display->drawLine(x - 1, yPrevThird, x, yCurThird);
          }
          prevYfThird = smoothYThird;
        } else {
          int yThird = constrain(static_cast<int>(round(yfThird)), 0, kScreenHeight - 1);
          if (!isfinite(prevYfThird)) {
            display->drawPixel(x, yThird);
          } else {
            display->drawLine(x - 1, static_cast<int>(round(prevYfThird)), x, yThird);
          }
          prevYfThird = static_cast<float>(yThird);
        }
      }

      lastDisplayY = prevYf;
    }

  
  public:
    // Allow external control of horizontal zoom and vertical scale so UI
    // settings can affect the scope rendering.
    void setHorizZoom(float hz) { horizZoom = hz; }
    void setVertScale(float vs) { vertScale = vs; }
    // Set delay parameters used for echo visualization (delay in ms, feedback 0..1)
    void setDelayParams(float ms, float feedback) {
      delayMs = ms;
      delayFeedback = feedback;
    }
    void setDelaySampleRate(int32_t sr) { delaySampleRate = sr > 0 ? sr : 44100; }
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
      display->sendF("c", DISPLAY_INVERT_COLORS ? 0xA7 : 0xA6); // force normal/invert state
      display->clearBuffer();
      display->setFont(u8g2_font_5x7_tf);
      display->drawStr(0, 8, "Initializing...");
      display->sendBuffer();

      xTaskCreatePinnedToCore(
        displayTaskImpl,
        "ScopeDisplayU8G2",
        4096,
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
