#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <AudioTools.h>
#include "AudioTools/Disk/AudioSourceSD.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include <ScopeI2SStream.h>
#include <algorithm>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffects.h"
#include "audio_mixer.h"
#include "config.h"
#include "input/button.h"
#include "ui.h"
#include "settings_storage.h"
#include "SettingsScreen.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "input/mux.h"

#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  #include "SettingsScreenU8g2.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
  #include "SettingsScreenAdafruit.h"
#endif

AudioInfo info(44100, 2, 16);

// ===== Single audio player =====
AudioSourceSD source("/", "wav");
WAVDecoder decoder;
AudioPlayer player(source, scopeI2s, decoder);

static LowPassFilter<float> filterEffect;
static Delay delayEffect;
static FilteredDelayMixerStream mixer;


// State
int currentSample = -1;
// String currentSamplePath = "";
static uint32_t lastVolSample = 0;
static float lastVol = -1.0f;
static SemaphoreHandle_t displayMutex = nullptr;
static int copiedZeroCount = 0;
static ISettingsScreen* settingsScreen = nullptr;


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
  // String full = String(SAMPLE_PATHS[index]);
  // currentSamplePath = full;
  if (DEBUGMODE) {
    Serial.print(F("PLAY: "));
    Serial.println(SAMPLE_PATHS[index]);
    }
}

void stopSample(int index) {
  if (index < 0 || index >= NUM_BUTTONS) return;
  if (currentSample != index) return;
  player.setActive(false);
  currentSample = -1;
  if (DEBUGMODE) {
    Serial.print(F("STOP: "));
    Serial.println(SAMPLE_PATHS[index]);    
  }
}


void initPlayer() {
  // ---- Initialize player ----
  player.setVolume(1.0);
  // route output to the mixer so filter+delay kunnen worden toegepast
  // player -> mixer -> scopeI2s
  // Nederlands: we zetten hier de output van de sampler op de mixer zodat
  // de mixer het signaal kan bewerken (filter + delay) voordat het naar
  // de scope of audio-uitgang gaat.
  player.setOutput(mixer);
  player.setAutoNext(false);
  player.setSilenceOnInactive(true); // doet dit iets? When enabled, writes zeros while inactive to keep sinks alive
  player.setDelayIfOutputFull(1); // Sets delay (ms) to wait when output is ful
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
  auto config = scopeI2s.defaultConfig(TX_MODE);
  config.copyFrom(info);
  config.pin_bck  = I2S_PIN_BCK;
  config.pin_ws   = I2S_PIN_WS;
  config.pin_data = I2S_PIN_DATA;
  config.i2s_format = I2S_STD_FORMAT;

 if (!scopeI2s.begin(config)) {
    Serial.println(F("Fout: scopeI2s.begin(config) mislukt - I2S niet gestart"));
  } else {

    scopeI2s.setAudioInfo(info);
  }

  filterEffect.begin(7000, info.sample_rate, 0.5f); // cutoff 7kHz, Q=0.5

  delayEffect.setSampleRate(info.sample_rate);
  delayEffect.setFeedback(0.8f); // feedback in %
  delayEffect.setDepth(0.9f); // depth in %
  delayEffect.setDuration(300); // ms delay
  delayEffect.setActive(true);

  // Let the scope visualization know about the delay settings so it can
  // render visual echoes.  Forward sample rate, duration and feedback.
  setScopeDelaySampleRate(info.sample_rate);
  setScopeDelayParams(static_cast<float>(delayEffect.getDuration()), delayEffect.getFeedback());


  mixer.setAudioInfo(info);
  // Zet output van mixer naar scope; mixer ontvangt data via player.write()
  mixer.setOutput(scopeI2s);
  // Geef referenties door aan de mixer
  mixer.setFilter(filterEffect);
  mixer.setDelay(delayEffect);
  // mix levels
  mixer.setMix(1.0f, 0.9f);
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

void checkVolume(uint32_t now) {
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
        player.setVolume(lastVol); // we zetten gewoon volume op de sampler
    }
  }
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  initSd();
  initAudio();
  initPlayer();
  initButtons();
  initDisplay();
  initInputMux();
  

  if (auto mutexPtr = static_cast<SemaphoreHandle_t*>(getDisplayMutex())) {
    displayMutex = *mutexPtr;
  }
}



void loop() {
  uint32_t now = millis();
  size_t copied = player.copy();
  if (copied == 0) {
    // Als er geen nieuwe data binnenkomt, toch de delay-tail blijven
    // uitsturen zodat de reverb hoorbaar blijft.
    mixer.renderTailFrames(64);
  }

  if (!mixer.tailActive()) {
    scopeI2s.feedSilenceFrames(kScopeSilenceFramesPerLoop);
  }

  // End-of-sample handling: if we observe no copied bytes for a few
  // consecutive loops, consider the sample finished and stop the player.
  // Some player backends may still report isActive() == true briefly even
  // when no data is copied (fade/drain), so rely on a short zero-counter
  // to avoid missed ends.
  if (copied == 0) {
    ++copiedZeroCount;
  } else {
    copiedZeroCount = 0;
  }

  if (currentSample >= 0 && copied == 0 && (copiedZeroCount >= COPIED_ZERO_THRESHOLD || !player.isActive())) {
    if (DEBUGMODE) {
      Serial.print(F("END: "));
      Serial.println(SAMPLE_PATHS[currentSample]);
    }
    // Ensure the player is stopped; silence-on-inactive handles zero output.
    //  player.stop();
    player.setActive(false);
    currentSample = -1;
    copiedZeroCount = 0;
  }

  
  checkButtons();
  checkVolume(now); 

  if (settingsScreen) {
    settingsScreen->update();
  }

  
}

