# 🤖 Copilot Instructies — ESP32 Sample Player

## 🎯 Projectdoel

Ontwikkel een **sound sample player** op een **ESP32** met 6 drukknoppen + 2 switches op een multiplexer, een **I2S DAC (UDA1334A)**, en een **SD-kaartmodule**.  
Bij indrukken van een knop speelt de bijbehorende sample af, en bij loslaten stopt deze onmiddellijk.  
Systeem mag **polyfoon** zijn en **minimale latency** hebben.

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

Gebruik 6 digitale inputs via de muxer voor de drukknoppen

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
die de conventies volgt van audio-tools van phil schatzman

loop() gebruikt om knoppen te scannen en play/stop aan te roepen.

playSample(int index) en stopSample(int index) implementeert.

zoveel mogelijk audio-tools classes gebruikt in plaats van zelf code schrijven.

de routing moet zijn:

player

filter

geluid splitsen van dry en send naar delay

geluid mergen zodat je dry en de (wet) delay effect hebt

dat naar de output doen,
en die waveform tonen op de schermpje.

de stream / sink of hoe dat ook heet moet altijd aan staan, zodat de delay tail (als de delay door blijft klinken)  hoorbaar blijft in plaats van dat die afgekapt wordt op button release.
Dus dat die mooi uit blijft klinken.



ik wil de interactive audio

Foutmeldingen logt via Serial.

🚀 Uitbreidingsopties

Later uitbreiden met:

effecten aan en uit zetten en bewerken
en realtime kunnen bewerken
```
