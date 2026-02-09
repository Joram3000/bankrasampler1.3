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
#include "settings_mode.h"
#include "input/mux.h"
#include "config/screen.h"
#include "config/config.h"
#include "config/settings.h"
#if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
#include "InitializationScreenU8g2.h"
#endif

// Audio system
AudioInfo info(44100, 2, 16);
// ===== Single audio player =====
AudioSourceSD source("/", "wav");
WAVDecoder decoder;


FilteredStream<int16_t, float> filteredStream(scopeI2s,2); 
AudioPlayer player(source, filteredStream, decoder);
// we maken een filtered scream die het geluid nadat het door player is gegaan kan bewerken



// ===== Effects =====
static LowPassFilter<float> insertLowPassFilterL;
static LowPassFilter<float> insertLowPassFilterR;

// static Delay sendDelayEffectL;
// static Delay sendDelayEffectR;
// static LowPassFilter<float> filterEffect;
// static FilteredDelayMixerStream mixer;

// State
int currentSample = -1;
static uint32_t lastVolSample = 0;
static float lastVol = -1.0f;

// filtercutof
static float filterCutoff = 800.0f;
float smoothedCutoff = 1000.0f;




// UI Screens
static InitializationScreenU8g2* initializationScreen = nullptr;

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

// Forward declarations ?
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

// this is where the buttons are mapped to settings screen actions
static void onMuxChange(uint8_t channel, bool active) {
  int index = findButtonIndexForChannel(channel);
  if (index < 0) return;

  if (handleSettingsButtonInput(static_cast<size_t>(index), active)) return;

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
  // bepaal hier wat de output van the player is: in dit geval direct naar de i2s, maar we zouden ook een effect kunnen toevoegen in de chain
  player.setOutput(filteredStream);
  // player.setOutput(mixer);
  player.setAutoNext(false);
  // player.setVolumeControl(0.3f);  // hier zouden we dus onze eigen curve kunnen laten 
  player.setSilenceOnInactive(true);
  player.setFadeTime(BUTTON_FADE_MS);
  player.begin();
  player.setActive(false);

  
}

void initAudio() {
  auto config = scopeI2s.defaultConfig(TX_MODE);
  config.copyFrom(info); // we moeten de sample rate, bits per sample en channels doorgeven aan de i2s driver zodat die correct kan configureren
  config.pin_bck  = I2S_PIN_BCK;
  config.pin_ws   = I2S_PIN_WS;
  config.pin_data = I2S_PIN_DATA;
  config.i2s_format = I2S_STD_FORMAT; 
  // config.buffer_size = 512; 
  // config.buffer_count = 4; // we hebben een grotere buffer nodig omdat we soms niet op tijd kunnen leveren (door effecten of door SD card read vertragingen)
 if (!scopeI2s.begin(config)) {
    Serial.println(F("Fout: scopeI2s.begin(config) mislukt - I2S niet gestart"));
  } else {
    scopeI2s.setAudioInfo(info);
  }

  // setup filters for all available channels
  filteredStream.setFilter(0, &insertLowPassFilterL);
  filteredStream.setFilter(1, &insertLowPassFilterR);

  // // de Q zou ook uit de config moeten komen :S
  // filterEffect.begin(LOW_PASS_CUTOFF_HZ, info.sample_rate, 0.5f);

  // delayEffect.setSampleRate(info.sample_rate);
  // delayEffect.setFeedback(DEFAULT_DELAY_FEEDBACK); 
  // delayEffect.setDepth(DEFAULT_DELAY_DEPTH); 
  // delayEffect.setDuration(DEFAULT_DELAY_TIME_MS);
  // delayEffect.setActive(true);
  

// mixer.begin(scopeI2s, filterEffect, delayEffect, info);


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
  }
  
static void releaseAllButtons() {
  for (int i = 0; i < BUTTON_COUNT; ++i) {
    buttons[i].release();
  }
}

void updateCutoff(float target) {
  smoothedCutoff += 0.05f * (target - smoothedCutoff);

  insertLowPassFilterL.begin(smoothedCutoff, info.sample_rate, 0.5f);
  insertLowPassFilterR.begin(smoothedCutoff, info.sample_rate, 0.5f);
}

void checkPot(uint32_t now) {
if ((now - lastVolSample) >= POT_READ_INTERVAL_MS) {
    
  
  int raw = analogRead(POT_PIN);

#ifdef POT_POLARITY_INVERTED
raw = 4095 - raw;
#endif

// Direct naar float en normaliseren naar 0.0-1.0 voor maximale precisie
float normalized = constrain(raw, 0, 4095) / 4095.0f;

// Map naar gewenste Hz range met float precisie
filterCutoff = 200.0f + (normalized * 3300.0f); // 200 + (0..1 * 3300) = 200..3500



  
//   lastVolSample = now;
//     int raw = analogRead(POT_PIN);
//     float norm = static_cast<float>(raw) / 4095.0f;
// #ifdef POT_POLARITY_INVERTED
//     norm = 1.0f - norm;
// #endif
//     norm = constrain(norm, 0.0f, 1.0f);
//     // simple deadband + smoothing
//     if (lastVol < 0.0f || fabsf(norm - lastVol) > 0.01f) {
//       lastVol = norm;
//         player.setVolume(lastVol); // we zetten gewoon volume op de sampler
//     }
//   
}
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  
  initDisplay();
  initSd();
  initAudio();
  initPlayer();  
  setMuxChangeCallback(onMuxChange); 
  initMuxScanner(5000);
  // SettingsUiDependencies settingsDeps;
  // settingsDeps.delayEffect = &delayEffect;
  // settingsDeps.filterEffect = &filterEffect;
  // settingsDeps.releaseButtons = releaseAllButtons;
  // initSettingsUi(settingsDeps);

  initSettingsModeSwitch();
}



void loop() {
  uint32_t now = millis();
  size_t copied = player.copy();


  // check of sample klaar is
  // copied == 0 betekent dat er geen data meer is om te kopieren (einde sample)
  // en dat de player dus klaar is met afspelen
  // we controleren ook of er een sample actief is (currentSample >= 0)
    if (currentSample >= 0 && player.isActive() && copied == 0) {
      int idx = currentSample;               
      player.setActive(false);
      currentSample = -1;
      if (DEBUGMODE) {
        Serial.print(F("SAMPLE END: "));
        Serial.println(SAMPLE_PATHS[idx]);
      }
    }
    
  checkPot(now);
  updateCutoff(filterCutoff); 
  muxScanTick();
  checkSettingsMode(now);
  
  if (getOperatingMode() == OperatingMode::Settings) {
    updateSettingsScreenUi();
  }
  
}

