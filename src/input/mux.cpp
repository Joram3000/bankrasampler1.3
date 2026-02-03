#include "mux.h"
#include <Arduino.h>
#include <AudioTools.h>
#include "config.h"

class Sn74hc151Mux {
public:
  void begin();
  int readChannel(uint8_t channel);

private:
  void selectChannel(uint8_t channel);
  bool initialized = false;
  uint8_t lastChannel = 0xFF;
};

Sn74hc151Mux gInputMux;

void Sn74hc151Mux::begin() {
  if (initialized) return;
  // Only configure pins that are configured (>= 0). This avoids compile/runtime issues
  // when a pin is intentionally left unconfigured in config.h (set to -1).
  if (INPUT_MUX_PIN_A >= 0) pinMode(INPUT_MUX_PIN_A, OUTPUT);
  if (INPUT_MUX_PIN_B >= 0) pinMode(INPUT_MUX_PIN_B, OUTPUT);
  if (INPUT_MUX_PIN_C >= 0) pinMode(INPUT_MUX_PIN_C, OUTPUT);
  if (INPUT_MUX_PIN_EN >= 0) {
    pinMode(INPUT_MUX_PIN_EN, OUTPUT);
    digitalWrite(INPUT_MUX_PIN_EN, LOW);
  }
  if (INPUT_MUX_PIN_Y >= 0) pinMode(INPUT_MUX_PIN_Y, INPUT);
  selectChannel(0);
  initialized = true;
}

void Sn74hc151Mux::selectChannel(uint8_t channel) {
  channel &= 0x07;
  if (channel == lastChannel) return;
  if (INPUT_MUX_PIN_A >= 0) digitalWrite(INPUT_MUX_PIN_A, channel & 0x01);
  if (INPUT_MUX_PIN_B >= 0) digitalWrite(INPUT_MUX_PIN_B, (channel >> 1) & 0x01);
  if (INPUT_MUX_PIN_C >= 0) digitalWrite(INPUT_MUX_PIN_C, (channel >> 2) & 0x01);
  lastChannel = channel;
}

int Sn74hc151Mux::readChannel(uint8_t channel) {
  if (!initialized) begin();
  selectChannel(channel);
  delayMicroseconds(INPUT_MUX_SETTLE_TIME_US);
  if (INPUT_MUX_PIN_Y < 0) {
    // No Y pin configured: return HIGH (not-active) to be safe. Caller should
    // ensure pins are configured in config.h.
    return HIGH;
  }
  return digitalRead(INPUT_MUX_PIN_Y);
}


void initInputMux() {
  gInputMux.begin();
}

bool readMuxActiveState(uint8_t channel, bool activeLow) {
  int level = gInputMux.readChannel(channel);
  return activeLow ? (level == LOW) : (level == HIGH);
}