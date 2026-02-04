# 🤖 Copilot Instructies — ESP32 Sample Player

## 🎯 Projectdoel

Ontwikkel een **sound sample player** op een **ESP32** met 6 drukknoppen + 2 switches op een multiplexer, een **I2S DAC (UDA1334A)**, en een **SD-kaartmodule**.  
Bij indrukken van een knop speelt de bijbehorende sample af, en bij loslaten stopt deze onmiddellijk.  
Systeem moet **polyfoon** zijn en **minimale latency** hebben.

---

## ⚙️ Hardware

| Component      | Beschrijving                          |
| -------------- | ------------------------------------- |
| ESP32          | Hoofdcontroller met I2S-ondersteuning |
| GME12864-41    | 128x64 OLED-display (I2C)             |
| SD-kaartmodule | MicroSD via SPI                       |
| UDA1334A       | I2S DAC audio-uitgang                 |

| Multiplexer 74HC151N

---

## 📦 Libraries

Gebruik de volgende Arduino libraries:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <AudioTools.h>
#include <AudioLibs/AudioPlayer.h>

#include <U8g2lib.h>

Gedragsspecificatie

Startup:

Initialiseer SD, DAC, OLED.

Toon iets op het scherm zodat we weten dat het allemaal gelukt is

Knoppen:

Gebruik 6 digitale inputs via de muxer.

Detecteer pressed (LOW) en released (HIGH) events.

Audio:

Elk samplebestand heet 1.wav, 2.wav, 3.wav, 4.wav.

Bij indrukken: start afspelen van sample.

Bij loslaten: stop sample.

Meerdere samples tegelijk toegestaan (polyfoon).

Weergave:
waveform scope

Performance:

Optimaliseer buffers en I2S-config voor lage latency.

Gebruik AudioPlayer of AudioGeneratorWAV uit AudioTools.

🧠 Verwachting van Copilot

Copilot moet code genereren die:

Een robuuste AudioPlayer setup maakt met I2SStream.

loop() gebruikt om knoppen te scannen en play/stop aan te roepen.

playSample(int index) en stopSample(int index) implementeert.

Foutmeldingen logt via Serial.

🚀 Uitbreidingsopties

Later uitbreiden met:

VU-meter of waveform op OLED.


effecten aan en uit zetten en bewerken
```
