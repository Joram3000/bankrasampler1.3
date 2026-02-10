#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <AudioTools.h>
#include "AudioTools/Disk/AudioSourceSD.h"
#include "audio_mixer.h"
#include "input/button.h"
#include "ui.h"
#include "settings_storage.h"
#include "settings_mode.h"
#include "input/mux.h"
#include "config/config.h"
#include "config/settings.h"
#include "initialization.h" 
#include "SettingsScreen.h"

// Audio system
AudioInfo info(44100, 2, 16);
// ===== Single audio player =====
AudioSourceSD source("/", "wav");
WAVDecoder decoder;


DelayMixerStream mixer;
FilteredStream<int16_t, float> filteredStream(mixer,2); 
AudioPlayer player(source, filteredStream, decoder);

static LowPassFilter<float> insertFilterL;
static LowPassFilter<float> insertFilterR;
static Delay delayEffect1;

// State
int initializationStep = 0;
int currentSample = -1;
static uint32_t lastPotRead = 0;// timestamp of last pot read
static float lastVol = -1.0f;
static bool switchDelaySendEnabled = false;
static bool switchFilterEnabled = false;

// filtercutof
static float filterCutoff = LOW_PASS_CUTOFF_HZ;
float smoothedCutoff = filterCutoff;

// UI Screens

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


void updateCutoff(float target) {
  smoothedCutoff += 0.1f * (target - smoothedCutoff);

  static float lastAppliedQ = -1.0f;
  static float lastAppliedFreq = -1.0f;

  // Cache pointer één keer per call i.p.v. meerdere calls
  if (auto ss = static_cast<decltype(getSettingsScreen())>(getSettingsScreen())) {
    float newQ = ss->getFilterQ();
    if (fabsf(newQ - lastAppliedQ) > 0.05f || fabsf(smoothedCutoff - lastAppliedFreq) > 0.5f) {
      insertFilterL.begin(smoothedCutoff, info.sample_rate, newQ);
      insertFilterR.begin(smoothedCutoff, info.sample_rate, newQ);
      lastAppliedQ = newQ;
      lastAppliedFreq = smoothedCutoff;
    }
  } else {
    if (fabsf(smoothedCutoff - lastAppliedFreq) > 0.5f) {
      insertFilterL.begin(smoothedCutoff, info.sample_rate, LOW_PASS_Q);
      insertFilterR.begin(smoothedCutoff, info.sample_rate, LOW_PASS_Q);
      lastAppliedFreq = smoothedCutoff;
    }
  }
}

// This is called whenever a change on the mux is detected (button press/release or switch toggle)
static void onMuxChange(uint8_t channel, bool active) {
  // if channel is one of the switches and the state has changed, update the corresponding state variable and move on
  if (channel == SWITCH_CHANNEL_FILTER_ENABLE && active != switchFilterEnabled) {
    switchFilterEnabled = active;
    if (DEBUGMODE) {
      Serial.print(F("SWITCH_FILTER_ENABLE = "));
      Serial.println(active ? F("ON") : F("OFF"));
    }
    return;
  }
 
if(channel == SWITCH_CHANNEL_DELAY_SEND && active != switchDelaySendEnabled) {
    switchDelaySendEnabled = active;

  if (DEBUGMODE) {
      Serial.print(F("SWITCH_DELAY_SEND_ENABLE = "));
      Serial.println(active ? F("ON") : F("OFF"));
    }
    return;
  }

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
  if (DEBUGMODE) {
    Serial.print(F("STOP: "));
    Serial.print(SAMPLE_PATHS[index]);    
  }
}

void initPlayer() {
  player.setVolume(1.0);
  player.setOutput(filteredStream);
  player.setAutoNext(false);
  // player.setVolumeControl(0.3f);  // hier zouden we dus onze eigen curve kunnen laten 
  player.setSilenceOnInactive(false);
  player.setFadeTime(BUTTON_FADE_MS);
  player.begin();
  player.setActive(false);

  if(DEBUGMODE) {
    Serial.println("player initialized.");
  }
}

void initAudio() {
  auto config = scopeI2s.defaultConfig(TX_MODE);
  config.copyFrom(info);
  config.pin_bck  = I2S_PIN_BCK;
  config.pin_ws   = I2S_PIN_WS;
  config.pin_data = I2S_PIN_DATA;
  config.i2s_format = I2S_STD_FORMAT; 
  config.buffer_count = 2;
  config.buffer_size = 256;

 if (!scopeI2s.begin(config)) {
    Serial.println(F("Fout: scopeI2s.begin(config) mislukt - I2S niet gestart"));
  } else {
    scopeI2s.setAudioInfo(info);
  }

  filteredStream.setFilter(0, &insertFilterL);
  filteredStream.setFilter(1, &insertFilterR);

  delayEffect1.setSampleRate(info.sample_rate);
  delayEffect1.setFeedback(DEFAULT_DELAY_FEEDBACK); 
  delayEffect1.setDepth(DEFAULT_DELAY_DEPTH); 
  delayEffect1.setDuration(DEFAULT_DELAY_TIME_MS);
  delayEffect1.setActive(true);

  mixer.begin(scopeI2s, delayEffect1, info);
  initializationStep++;
  if(DEBUGMODE) {
     Serial.print(initializationStep );
    Serial.println("Audio initialized.");
  }
}

void initDisplay() {
  if (!initUi()) {
    // oude code: for(;;);
    // Niet blokkeren: log en ga in veilige modus (geen display), of retry a-synchroon.
    Serial.println(F("Display init failed, continuing without UI"));
    // alternatief: zet een flag showUiAvailable = false en laat rest van init doorgaan
    return;
  } else {
    auto* initScreen = getInitializationScreen();
    setInitializationScreen(initScreen);
    initializationStepper("Welkom");
    delay(INIT_SCREEN_DURATION_MS);
    initializationStepper("Display initialized");
    initializationStep++;
    if (DEBUGMODE) {
      Serial.print(initializationStep );
      Serial.println("Display initialized.");
    }
  }
}

void initSd() {
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);
  
  int attempts = 0;
  while (!SD.begin(SD_CS_PIN, SPI, 80000000UL) && attempts < 5) {
    Serial.println("Card failed, retrying...");
    delay(100);
    attempts++;
  }
  
  if (attempts >= 5) {
    Serial.println("SD initialization failed after 5 attempts");
    while(1);  // Nu alsnog stoppen
  }
  initializationStep++;
  if(DEBUGMODE) {
    Serial.print(initializationStep );
  Serial.println("SD card initialized.");
  }
}
  
static void releaseAllButtons() {
  for (int i = 0; i < BUTTON_COUNT; ++i) {
    buttons[i].release();
  }
}


void checkPot(uint32_t now) {
  // only read pot at configured interval
  if ((now - lastPotRead) < POT_READ_INTERVAL_MS) return;
  int raw = analogRead(POT_PIN);
#ifdef POT_POLARITY_INVERTED
  raw = 4095 - raw;
#endif
  // direct naar float 0.0 - 1.0
  float norm = constrain(raw, 0, 4095) / 4095.0f;
  if (switchFilterEnabled) {
    // stuur pot naar filter cutoff (wordt verder gesmooth in updateCutoff)
    filterCutoff = LOW_PASS_MIN_HZ + (norm * (LOW_PASS_MAX_HZ - LOW_PASS_MIN_HZ));
  } else {
    // deadband + update volume alleen als verandering > threshold
    if (lastVol < 0.0f || fabsf(norm - lastVol) > 0.01f) {
      lastVol = norm;
      player.setVolume(lastVol);
    }
  }
  // update timestamp alleen als we daadwerkelijk gelezen hebben
  lastPotRead = now;
}


static void initSwitchStates() {
  // lees switches via muxGetChannelState (zie hieronder voor implementatie)
  bool filterState = readMuxActiveState(SWITCH_CHANNEL_FILTER_ENABLE);
  bool delayState  = readMuxActiveState(SWITCH_CHANNEL_DELAY_SEND);

  // laat onMuxChange hetzelfde werk doen (logica zit daar al)
  onMuxChange(SWITCH_CHANNEL_FILTER_ENABLE, filterState);
  onMuxChange(SWITCH_CHANNEL_DELAY_SEND, delayState);

  // lees pot en initialiseer volume of filter cutoff meteen
  int raw = analogRead(POT_PIN);
#ifdef POT_POLARITY_INVERTED
  raw = 4095 - raw;
#endif
  float norm = constrain(raw, 0, 4095) / 4095.0f;

  if (filterState) {
    filterCutoff = LOW_PASS_MIN_HZ + (norm * (LOW_PASS_MAX_HZ - LOW_PASS_MIN_HZ));
    smoothedCutoff = filterCutoff; // voorkom grote ramp op start
    updateCutoff(filterCutoff);
  } else {
    lastVol = norm;
    player.setVolume(lastVol);
  }

  // zet delay send direct
  mixer.sendEnabled(delayState);

  // voorkom dat checkPot meteen opnieuw overschrijft timestamp
  lastPotRead = millis();
}


void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Error);
  
 
  initDisplay();
  initSd();
  setMuxChangeCallback(onMuxChange); 
  initMuxScanner(5000);
  initAudio();
  initPlayer();  
  SettingsUiDependencies settingsDeps;
  settingsDeps.delayEffect = &delayEffect1;
  settingsDeps.filterEffect = &insertFilterL; 
  settingsDeps.releaseButtons = releaseAllButtons;
  initSettingsUi(settingsDeps);
  initSettingsModeSwitch();
  initSwitchStates();
}



void loop() {
  uint32_t now = millis();
  size_t copied = player.copy();
    if (!player.isActive()) {
       mixer.pumpSilenceFrames(kScopeSilenceFramesPerLoop);
    }
    
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
    
    muxScanTick();

// HANDLE INPUTS
  checkPot(now);
  switchFilterEnabled ? updateCutoff(filterCutoff) : updateCutoff(20000.0f);
  switchDelaySendEnabled ? mixer.sendEnabled(true) : mixer.sendEnabled(false);
  checkSettingsMode(now);
  

  if (getOperatingMode() == OperatingMode::Settings) {
    updateSettingsScreenUi();
  }
  
}

