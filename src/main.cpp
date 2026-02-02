#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <AudioTools.h>
#include "AudioTools/Disk/AudioSourceSD.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include <algorithm>
#include <cstring>
#include <vector>
#include "AudioTools/CoreAudio/AudioEffects/AudioEffects.h"
#include "AudioTools.h"
#include <Arduino.h>
#include <algorithm>


AudioInfo info(44100, 2, 16);
// Audio stack
AudioSourceSD source("/", "wav");
I2SStream i2s;
WAVDecoder wavDecoder;
AudioPlayer player(source, i2s, wavDecoder);

constexpr int POT_PIN = 34;
constexpr int DAC_SLEEP_PIN = 27;

constexpr int VOICES = 6;

// ===== Generators en streams =====
SineWaveGenerator<int16_t>* oscillators[VOICES];
GeneratedSoundStream<int16_t>* streams[VOICES];

InputMixer<int16_t> mixer;
I2SStream out;

// Master volume stream: sits between the mixer and the physical I2S output.
audio_tools::VolumeStream volume;

// StreamCopy will copy from the mixer into the volume stream, then filtered delay -> I2S.
StreamCopy copier(volume, mixer);

const float pitches[VOICES] = { N_C3, N_E3, N_G3, N_C4, N_E4, N_G4 };



void setup() {
  // ---- DAC aan ----
  pinMode(DAC_SLEEP_PIN, OUTPUT);
  digitalWrite(DAC_SLEEP_PIN, HIGH);
  delay(50);

  Serial.begin(115200);

  // ---- I2S ----
  auto config = out.defaultConfig(TX_MODE);
  config.copyFrom(info);
  config.pin_bck  = 14;
  config.pin_ws   = 15;
  config.pin_data = 22;
  config.i2s_format = I2S_STD_FORMAT;

  out.begin(config);
  out.setAudioInfo(info);

  // initialize volume stream with same audio info as I2S
  {
    auto vcfg = volume.defaultConfig();
    vcfg.copyFrom(config);
    // start volume stream so it knows bits/channels
    volume.begin(vcfg);
    // default full volume
    volume.setVolume(1.0f);
    // wire: volume  -> I2S
    volume.setOutput(out);
  }

  // ---- Oscillators + streams via loop ----
  for (int i = 0; i < VOICES; i++) {
    oscillators[i] = new SineWaveGenerator<int16_t>(8000);
    streams[i]     = new GeneratedSoundStream<int16_t>(*oscillators[i]);
    oscillators[i]->begin(info, pitches[i]);
    mixer.add(*streams[i]);
  }

  mixer.begin(info);
}

void loop() {
  // copy mixed audio into the volume stream (then to I2S)
  copier.copy();

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
