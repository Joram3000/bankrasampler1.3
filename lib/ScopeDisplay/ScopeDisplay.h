#ifndef SCOPEDISPLAY_H
#define SCOPEDISPLAY_H

#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Wire.h>
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include "config/settings.h"
#include "config/screen.h"
// #include "config.h"

/**
 * ScopeDisplay - Beheert OLED display met oscilloscope visualisatie
 * 
 * Features:
 * - Real-time waveform display (oscilloscope)
 * - Thread-safe updates via mutex
 * - Eigen FreeRTOS task op Core 0
 */
class ScopeDisplay {
  private:
    Adafruit_SSD1306* display;
  TaskHandle_t displayTaskHandle;
  SemaphoreHandle_t displayMutex;
  volatile bool suspended = false;
    
  // Waveform data (gedeeld met ScopeI2SStream)
  int16_t* waveformBuffer;
  int* waveformIndex;
  int waveformSamples; // aantal samples in de cirkelbuffer
    
    // Zoom/state
    float horizZoom = DEFAULT_HORIZ_ZOOM;   // >1 = inzoomen (minder samples weergegeven), <1 = uitzoomen
    float vertScale = DEFAULT_VERT_SCALE;   // amplitude schaal factor

    // bewaar laatst getekende Y tussen frames om jumps te voorkomen
    float lastDisplayY = NAN;
    
    // Status info
    String currentFile;
    bool isPlaying;
    
  // Display layout configuratie
  static constexpr int kScreenWidth = DISPLAY_WIDTH;
  static constexpr int kScreenHeight = DISPLAY_HEIGHT;
    
    /**
     * Display update task - draait in aparte thread
     */
    static void displayTaskImpl(void* parameter) {
      ScopeDisplay* self = (ScopeDisplay*)parameter;
      self->displayLoop();
    }
    
    /**
     * Main display loop - rendert scope en status
     */
    void displayLoop() {
      for(;;) {
        if (suspended) {
          vTaskDelay(40 / portTICK_PERIOD_MS);
          continue;
        }
        if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
          display->clearDisplay();
          
          // Render waveform scope
          renderWaveform();

 
          
          display->display();
          xSemaphoreGive(displayMutex);
        }
        
        // Update display elke 40ms voor vloeiende scope (25 FPS)
        vTaskDelay(40 / portTICK_PERIOD_MS);
      }
    }
    
   
    /**
     * Render oscilloscope waveform met zoom support
     * - linear interpolation tussen samples
     * - optional exponential smoothing voor vloeiender curve
     * - optional envelope (min/max) voor behoud van transiënten
     */
    void renderWaveform() {
  const int scopeCenter = kScreenHeight / 2;
      if (waveformSamples <= 0 || !waveformBuffer) return;

      const bool useSmoothing = false;
      const float smoothingAlpha = 0.6f;
      const bool drawEnvelope = false;

  int displayedSamples = std::max(1, static_cast<int>(waveformSamples / horizZoom));
  displayedSamples = std::min(displayedSamples, waveformSamples);

      // Zorg dat endpoints precies overeenkomen: pixel 0 -> startIndex, pixel W-1 -> newestIndex
  float step = (displayedSamples > 1 && kScreenWidth > 1)
        ? static_cast<float>(displayedSamples - 1) / static_cast<float>(kScreenWidth - 1)
                    : 0.0f;

      int newestIndex = ((*waveformIndex - 1) + waveformSamples) % waveformSamples;
      int startIndex = newestIndex - displayedSamples + 1;
      if (startIndex < 0) startIndex += waveformSamples;

      // window-size voor gemiddelde per pixel (smaller window to avoid over-blur)
  int windowSize = std::max(1, static_cast<int>(ceilf((displayedSamples / (float)kScreenWidth))));
      int halfWin = (windowSize - 1) / 2;

      // gebruik vorige frame eindwaarde als start voor smoothing (vermijdt hoge eerste pixel)
      float prevYf = isfinite(lastDisplayY) ? lastDisplayY : NAN;

  for (int x = 0; x < kScreenWidth; ++x) {
        float samplePos = startIndex + x * step;
        int centerIdx = static_cast<int>(floor(samplePos));
        float frac = samplePos - static_cast<float>(centerIdx);
        centerIdx %= waveformSamples; if (centerIdx < 0) centerIdx += waveformSamples;

        // gemiddelde over een klein, symmetrisch venster rond samplePos (demp spikes)
        int winSum = 0, winCount = 0;
        for (int w = -halfWin; w <= halfWin; ++w) {
          int i = (centerIdx + w) % waveformSamples; if (i < 0) i += waveformSamples;
          winSum += waveformBuffer[i];
          ++winCount;
        }
        // lineaire interp tussen centerIdx en centerIdx+1 om fractionele positie mee te nemen
        int nextIdx = (centerIdx + 1) % waveformSamples;
        float sampleCenter = static_cast<float>(winSum) / static_cast<float>(winCount);
        float sampleNext = static_cast<float>(waveformBuffer[nextIdx]);
        float val = sampleCenter * (1.0f - frac) + sampleNext * frac;


  float yf = scopeCenter - (val * ((kScreenHeight / 2) * vertScale) / 32768.0f);

        if (useSmoothing) {
          if (!isfinite(prevYf)) prevYf = yf; // initialiseer netjes met eerste mapped waarde
          float smoothY = (smoothingAlpha * yf) + ((1.0f - smoothingAlpha) * prevYf);
          int yPrev = constrain(static_cast<int>(round(prevYf)), 0, kScreenHeight - 1);
          int yCur  = constrain(static_cast<int>(round(smoothY)), 0, kScreenHeight - 1);
          if (x == 0) {
            display->drawPixel(x, yCur, SSD1306_WHITE);
          } else {
            display->drawLine(x - 1, yPrev, x, yCur, SSD1306_WHITE);
          }
          prevYf = smoothY;
        } else {
          int y = constrain(static_cast<int>(round(yf)), 0, kScreenHeight - 1);
          if (!isfinite(prevYf)) {
            display->drawPixel(x, y, SSD1306_WHITE);
          } else {
            display->drawLine(x - 1, static_cast<int>(round(prevYf)), x, y, SSD1306_WHITE);
          }
          prevYf = static_cast<float>(y);
        }
      }

      lastDisplayY = prevYf;
    }

  public:
    /**
     * Constructor
     * @param disp Pointer naar Adafruit_SSD1306 display object
     * @param waveBuffer Pointer naar waveform buffer array
     * @param waveIdx Pointer naar buffer index
     * @param waveSamples Aantal samples in de buffer (geef dezelfde waarde als ScopeI2SStream)
     */
    // Allow external control of horizontal zoom and vertical scale so UI
    // settings can affect the scope rendering.
    void setHorizZoom(float hz) { horizZoom = hz; }
    void setVertScale(float vs) { vertScale = vs; }
    void setSuspended(bool value) {
      suspended = value;
      if (!value) {
        lastDisplayY = NAN;
      }
    }
    // Backward-compatible 3-arg constructor (delegates to 4-arg)
    ScopeDisplay(Adafruit_SSD1306* disp, int16_t* waveBuffer, int* waveIdx)
      : ScopeDisplay(disp, waveBuffer, waveIdx, NUM_WAVEFORM_SAMPLES) {}

    ScopeDisplay(Adafruit_SSD1306* disp, int16_t* waveBuffer, int* waveIdx, int waveSamples) 
      : display(disp),
        displayTaskHandle(NULL),
        waveformBuffer(waveBuffer),
        waveformIndex(waveIdx),
        waveformSamples(waveSamples),
        currentFile(""),
        isPlaying(false) {
      
      // Creëer mutex voor thread-safe updates
      displayMutex = xSemaphoreCreateMutex();
    }
    
    // Zoom controls (thread-safe gebruik via mutex bij nodig)
    void zoomHorizIn()  { horizZoom = std::min(8.0f, horizZoom * 1.5f); }   // inzoomen
    void zoomHorizOut() { horizZoom = std::max(0.25f, horizZoom / 1.5f); } // uitzoomen
    void zoomVertIn()   { vertScale = std::min(8.0f, vertScale * 1.25f); }
    void zoomVertOut()  { vertScale = std::max(0.125f, vertScale / 1.25f); }
    void resetZoom()    { horizZoom = 1.0f; vertScale = 1.0f; }
    
    /**
     * Initialiseer display en start update task
     * @param i2cAddress I2C adres van display (default: 0x3C)
     * @return true als succesvol
     */
  bool begin(uint8_t i2cAddress = DISPLAY_I2C_ADDRESS) {
      // Initialiseer display hardware
      if(!display->begin(SSD1306_SWITCHCAPVCC, i2cAddress)) {
        return false;
      }
      display->invertDisplay(DISPLAY_INVERT_COLORS);
      
      // Toon startup bericht
      display->clearDisplay();
      display->setTextSize(1);
      display->setTextColor(SSD1306_WHITE);
      display->setCursor(0, 0);
      display->println("Initializing...");
      display->display();
      
      // Start display task op core 0 (audio blijft op core 1)
      xTaskCreatePinnedToCore(
        displayTaskImpl,      // Task functie
        "ScopeDisplay",       // Task naam
        4096,                 // Stack size
        this,                 // Parameter (this pointer)
        1,                    // Priority (lager dan audio)
        &displayTaskHandle,   // Task handle
        0                     // Core 0
      );
      
      return true;
    }
    
     SemaphoreHandle_t* getMutex() {
      return &displayMutex;
    }
};

#endif // SCOPEDISPLAY_H
