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

#if DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
#include "SettingsScreenAdafruit.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
#include "SettingsScreenU8g2.h"
#include "InitializationScreenU8g2.h"
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

// settings States
float currentFilterCutoffHz = LOW_PASS_CUTOFF_HZ;
float currentFilterQ = LOW_PASS_Q;
float currentDelayTimeMs = DEFAULT_DELAY_TIME_MS;
float currentDelayDepth = DEFAULT_DELAY_DEPTH;
float currentDelayFeedback = DEFAULT_DELAY_FEEDBACK;
bool currentCompEnabled = MASTER_COMPRESSOR_ENABLED;

static ISettingsScreen* settingsScreen = nullptr;
static InitializationScreenU8g2* initializationScreen = nullptr;
enum class OperatingMode { Performance, Settings, Initializing };
OperatingMode currentMode = OperatingMode::Initializing;


// Settings mode switch state
bool settingsModeRawState = false;
bool settingsModeDebouncedState = false;
uint32_t settingsModeLastDebounceTime = 0;

// --- Buttons ---
Button buttons[BUTTON_COUNT] = {
  Button(BUTTON_CHANNEL_ON_MUX[0], SAMPLE_PATHS[0]),
  Button(BUTTON_CHANNEL_ON_MUX[1], SAMPLE_PATHS[1]),
  Button(BUTTON_CHANNEL_ON_MUX[2], SAMPLE_PATHS[2]),
  Button(BUTTON_CHANNEL_ON_MUX[3], SAMPLE_PATHS[3]),
  Button(BUTTON_CHANNEL_ON_MUX[4], SAMPLE_PATHS[4]),
  Button(BUTTON_CHANNEL_ON_MUX[5], SAMPLE_PATHS[5]),
};

static bool prevLatched[BUTTON_COUNT] = { false, false, false, false, false, false };

void playSample(int index);
void stopSample(int index);

static int findButtonIndexForChannel(uint8_t channel) {
  for (int i = 0; i < BUTTON_COUNT; ++i) {
    if (BUTTON_CHANNEL_ON_MUX[i] == channel) {
      return i;
    }
  }
  return -1;
}

static void onMuxChange(uint8_t channel, bool active) {
  int index = findButtonIndexForChannel(channel);
  if (index < 0) return;

  if (active) {
    playSample(index);
  } else {
    stopSample(index);
  }

  if (DEBUGMODE) {
    Serial.print(F("  MUX->CHANNEL "));
    Serial.println(channel);

  }
}

void playSample(int index) {
  if (index < 0 || index >= BUTTON_COUNT) return;
  // If same sample already active, restart it
  currentSample = index;
  player.stop();
  player.setPath(SAMPLE_PATHS[index]);
  player.setActive(true);
  // String full = String(SAMPLE_PATHS[index]);
  // currentSamplePath = full;
  if (DEBUGMODE) {
    Serial.print(F("PLAY: "));
    Serial.print(SAMPLE_PATHS[index]);
    }
}

void stopSample(int index) {
  if (index < 0 || index >= BUTTON_COUNT) return;
  if (currentSample != index) return;
  player.setActive(false);
  currentSample = -1;
  if (DEBUGMODE) {
    Serial.print(F("STOP: "));
    Serial.print(SAMPLE_PATHS[index]);    
  }
}

void initPlayer() {
  player.setVolume(1.0);
  player.setOutput(mixer);
  player.setAutoNext(false);
  player.setSilenceOnInactive(true); // doet dit iets? When enabled, writes zeros while inactive to keep sinks alive
  player.setDelayIfOutputFull(1); // Sets delay (ms) to wait when output is ful
  player.setFadeTime(BUTTON_FADE_MS);
  player.begin();
  player.stop();
  
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
  mixer.setOutput(scopeI2s);
  mixer.setFilter(filterEffect);
  mixer.setDelay(delayEffect);
  mixer.setMix(1.0f, 0.9f);

  if(DEBUGMODE) {
    Serial.println("Audio initialized.");
  }
}
void initSd() {
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, 80000000UL)) {
    Serial.println("Card failed, or not present");
    while (1);
  }
  if(DEBUGMODE) {
    Serial.println("SD card initialized.");
  }
}

void initDisplay() {
  if (!initUi()) {
    for (;;) ;
  } else if (DEBUGMODE) {
    Serial.println("Display initialized.");
  }


    // Show a short initialization screen animation (1s) instead of a blind delay.
#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
    if (auto* display = getU8g2Display()) {
      if (!initializationScreen) {
        initializationScreen = new InitializationScreenU8g2(*display, INIT_SCREEN_MESSAGE, INIT_SCREEN_DURATION_MS);
        initializationScreen->begin();
      }
      // ensure the screen shows for the configured duration
      initializationScreen->enter();
      initializationScreen->update();
    }
#else
    delay(1000);
#endif
  Serial.print("Init done, switching to Performance mode");
    currentMode = OperatingMode::Performance;
  }


static void initSettingsScreen() {
  if (settingsScreen) return;
#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
  if (auto* display = getU8g2Display()) {
    settingsScreen = new SettingsScreenU8g2(*display);
  } else {
    Serial.println("Settings screen unavailable: no U8G2 display detected");
    return;
  }
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ADAFRUIT_SSD1306
  if (auto* display = getAdafruitDisplay()) {
    settingsScreen = new SettingsScreenAdafruit(*display);
  } else {
    Serial.println("Settings screen unavailable: no Adafruit SSD1306 display detected");
    return;
  }
#else
  return;
#endif
  settingsScreen->begin();
  settingsScreen->setZoomCallback([](float zoomFactor) {
    setScopeHorizZoom(zoomFactor);
  });
  settingsScreen->setDelayTimeCallback([](float durationMs) {
    currentDelayTimeMs = durationMs;
    delayEffect.setDuration(static_cast<uint32_t>(durationMs));
  });
  
  settingsScreen->setDelayFeedbackCallback([](float feedback) {
    currentDelayFeedback = feedback;
    delayEffect.setFeedback(feedback);
  });  
  settingsScreen->setZoom(DEFAULT_HORIZ_ZOOM);
  settingsScreen->setDelayTimeMs(currentDelayTimeMs);
  settingsScreen->setDelayFeedback(currentDelayFeedback);
  settingsScreen->setFilterCutoffHz(currentFilterCutoffHz);
  settingsScreen->setFilterQ(currentFilterQ);
  settingsScreen->setCompressorEnabled(currentCompEnabled);  
}

void initSettingsModeSwitch() {
  pinMode(SWITCH_PIN_SETTINGS_MODE, INPUT);
  bool settingsModeInit = (digitalRead(SWITCH_PIN_SETTINGS_MODE) == LOW);
  settingsModeRawState = settingsModeDebouncedState = settingsModeInit;

  if(DEBUGMODE) {
    Serial.print("Settings mode switch initialized to: ");
    Serial.println(settingsModeDebouncedState ? "ON" : "OFF");
  }
} 

static uint32_t settingsModeLastPoll = 0;

static void checkSettingsMode(uint32_t now) {
  if ((now - settingsModeLastPoll) < SETTINGS_POLL_INTERVAL_MS) return;
  settingsModeLastPoll = now;

  bool raw = (digitalRead(SWITCH_PIN_SETTINGS_MODE) == LOW);
  if (raw != settingsModeRawState) {
    settingsModeRawState = raw;
    settingsModeLastDebounceTime = now;
  }

  if ((now - settingsModeLastDebounceTime) >= SETTINGS_DEBOUNCE_MS && settingsModeDebouncedState != settingsModeRawState) {
    settingsModeDebouncedState = settingsModeRawState;
    settingsModeDebouncedState ? currentMode = OperatingMode::Settings : currentMode = OperatingMode::Performance;
    if (DEBUGMODE) {
      Serial.print(F("Settings mode -> "));
      Serial.println(settingsModeDebouncedState ? F("ON") : F("OFF"));
    }

  }
}

static void updateSettingsScreenUi() {
  if (!settingsScreen)
  Serial.print("In settings mode loop without settings screen\n");
 return;
  if (displayMutex) {
    if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      settingsScreen->update();
      xSemaphoreGive(displayMutex);
    }
  } else {
    settingsScreen->update();
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
  initDisplay();
  setMuxChangeCallback(onMuxChange); 
  initMuxScanner(5000, true);
  initSettingsScreen();
  loadSettingsFromSd(settingsScreen); 
  initSettingsModeSwitch();

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
    
    
    checkVolume(now); 
  

  muxScanTick();
  checkSettingsMode(now);
  
}

