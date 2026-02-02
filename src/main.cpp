#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <AudioTools.h>
#include "AudioTools/Disk/AudioSourceSD.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include <ScopeI2SStream.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffects.h"
#include "config.h"
#include "input.h"
#include "ui.h"
// FreeRTOS semaphore helpers for safely drawing to the shared display
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
AudioInfo info(44100, 2, 16);

constexpr int DAC_SLEEP_PIN = 27;

// ===== Single audio player =====
AudioSourceSD source("/", "wav");
WAVDecoder decoder;
I2SStream i2s;
AudioPlayer player(source, i2s, decoder);

// Master volume stream
audio_tools::VolumeStream volume;

// Current sample index
int currentSample = -1;

// Display mutex (obtained from scope display implementation)
static SemaphoreHandle_t displayMutex = nullptr;


// --- Buttons ---
const int NUM_BUTTONS = sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0]);
Button buttons[NUM_BUTTONS] = {
  Button(BUTTON_PINS[0], SAMPLE_PATHS[0], true, false),
  Button(BUTTON_PINS[1], SAMPLE_PATHS[1], true, false),
  Button(BUTTON_PINS[2], SAMPLE_PATHS[2], true, false)
};
static bool prevLatched[NUM_BUTTONS];

void playSample(int index) {
  if (index < 0 || index >= NUM_BUTTONS) return;
  // If same sample already active, restart it
  currentSample = index;
  player.stop();
  player.setPath(SAMPLE_PATHS[index]);
  player.setActive(true);
  if (Serial) {
    Serial.print(F("PLAY: "));
    Serial.println(SAMPLE_PATHS[index]);
  }
}

void stopSample(int index) {
  if (index < 0 || index >= NUM_BUTTONS) return;
  if (currentSample != index) return;
  player.stop();
  currentSample = -1;
  if (Serial) {
    Serial.print(F("STOP: "));
    Serial.println(SAMPLE_PATHS[index]);
  }
}

void checkButtons() {
  uint32_t now = millis();
  for (int i = 0; i < NUM_BUTTONS; ++i) {
    if (buttons[i].update(now)) {
      playSample(i);
    }
    bool latched = buttons[i].isLatched();
    if (prevLatched[i] && !latched) {
      // On release: stop the sample so next press starts from the beginning
      stopSample(i);
    }
    prevLatched[i] = latched;
  }
}

void initPlayer() {
  // ---- Initialize player ----
  player.setVolume(1.0);
  player.setOutput(i2s);
  player.setSilenceOnInactive(true);
  player.setAutoNext(false);
  // player.setDelayIfOutputFull(0);
  player.setFadeTime(BUTTON_FADE_MS);
  player.begin();
  player.stop();
  
}

void initButtons() {
  for (int i = 0; i < NUM_BUTTONS; ++i) {
    buttons[i].begin();
    buttons[i].sync(millis());
    prevLatched[i] = buttons[i].isLatched();
  }
}

void initAudio() {
  auto config = i2s.defaultConfig(TX_MODE);
  config.copyFrom(info);
  config.pin_bck  = I2S_PIN_BCK;
  config.pin_ws   = I2S_PIN_WS;
  config.pin_data = I2S_PIN_DATA;
  config.i2s_format = I2S_STD_FORMAT;

  i2s.begin(config);
  i2s.setAudioInfo(info);

  // initialize volume stream with same audio info as I2S
  {
    auto vcfg = volume.defaultConfig();
    vcfg.copyFrom(config);
    // start volume stream so it knows bits/channels
    volume.begin(vcfg);
    // default full volume
    volume.setVolume(1.0f);
    // wire: volume  -> I2S
    volume.setOutput(i2s);
  }
}

void initSd() {
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, 80000000UL)) {
    Serial.println("Card failed, or not present");
    while (1);
  }
}

void initDisplay() {
  if (!initUi()) {
    for (;;) ;
  }
}


void setup() {
  // ---- DAC aan ----
  pinMode(DAC_SLEEP_PIN, OUTPUT);
  digitalWrite(DAC_SLEEP_PIN, HIGH);
  delay(50);

  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  initSd();

  initAudio();

  initPlayer();

  initButtons();

  initDisplay();
  if (auto mutexPtr = static_cast<SemaphoreHandle_t*>(getDisplayMutex())) {
    displayMutex = *mutexPtr;
  }
}



void loop() {
  // Update player
  size_t copied = player.copy();

  // Debug status for end-of-sample diagnosis
  if (currentSample >= 0) {
    bool active = player.isActive();
    if (Serial) {
      Serial.print(F("[STATUS] sampleIndex="));
      Serial.print(currentSample);
      Serial.print(F(" path="));
      Serial.print(SAMPLE_PATHS[currentSample]);
      Serial.print(F(" copied="));
      Serial.print(copied);
      Serial.print(F(" active="));
      Serial.println(active ? "1" : "0");
    }
  }

  // End-of-sample handling: if we observe no copied bytes for a few
  // consecutive loops, consider the sample finished and stop the player.
  // Some player backends may still report isActive() == true briefly even
  // when no data is copied (fade/drain), so rely on a short zero-counter
  // to avoid missed ends.
  static int copiedZeroCount = 0;
  const int COPIED_ZERO_THRESHOLD = 3; // number of consecutive loops with copied==0

  if (copied == 0) {
    ++copiedZeroCount;
  } else {
    copiedZeroCount = 0;
  }

  if (currentSample >= 0 && copied == 0 && (copiedZeroCount >= COPIED_ZERO_THRESHOLD || !player.isActive())) {
    if (Serial) {
      Serial.print(F("END: "));
      Serial.println(SAMPLE_PATHS[currentSample]);
    }
    // Ensure the player is stopped and marked inactive
    player.stop();
    // Also defensively clear the active flag if the API exposes it.
    // (player.stop() should normally do this.)
    player.setActive(false);
    currentSample = -1;
    copiedZeroCount = 0;
  }


  checkButtons();


  // read pot and update master volume periodically
  static uint32_t lastVolSample = 0;
  static float lastVol = -1.0f;
  const uint32_t VOL_READ_INTERVAL_MS = 100;
  uint32_t now = millis();
  if ((now - lastVolSample) >= VOL_READ_INTERVAL_MS) {
    lastVolSample = now;
    int raw = analogRead(POT_PIN); // ESP32: 0..4095
    float norm = static_cast<float>(raw) / 4095.0f;
#ifdef POT_POLARITY_INVERTED
    norm = 1.0f - norm;
#endif
    norm = constrain(norm, 0.0f, 1.0f);
    // simple deadband + smoothing
    if (lastVol < 0.0f || fabsf(norm - lastVol) > 0.01f) {
      lastVol = norm;
      volume.setVolume(lastVol);
    }
  }
}
