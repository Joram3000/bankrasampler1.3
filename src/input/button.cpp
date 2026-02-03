#include "button.h"
#include <Arduino.h>
#include <AudioTools.h>
#include "config.h"
#include "mux.h"

// Implementations for Button
Button::Button(int pinOrChannel, const char* samplePath) : pin(pinOrChannel), samplePath(samplePath) {}

void Button::begin() {
  rawState = false;
  debouncedState = false;
  lastDebounceTime = 0;
  lastTriggerTime = 0;
  latched = false;
}

bool Button::update(uint32_t now) {
  bool raw = readPressedHardware();
  if (raw != rawState) {
    lastDebounceTime = now;
    rawState = raw;
  }
  if ((now - lastDebounceTime) > BUTTON_DEBOUNCE_MS && raw != debouncedState) {
    debouncedState = raw;
    if (debouncedState) {
     if (!latched && (now - lastTriggerTime) > BUTTON_RETRIGGER_GUARD_MS) {
        lastTriggerTime = now;
        latched = true;
        if (DEBUGMODE) {
          Serial.print(F("Button pressed: "));
          Serial.println(samplePath ? samplePath : "<unnamed>");
        }
        return true;
      }
    } else {
      latched = false;
      if (DEBUGMODE) {
        Serial.print(F("Button released: "));
        Serial.println(samplePath ? samplePath : "<unnamed>");
      }
    }
  }
  return false;
}

void Button::release() { latched = false; lastTriggerTime = 0; }

void Button::sync(uint32_t now) {
  bool raw = readPressedHardware();
  rawState = raw;
  debouncedState = raw;
  latched = raw;
  lastDebounceTime = now;
  lastTriggerTime = raw ? now : 0;
}

bool Button::readRaw() const {
  return readPressedHardware();
}

bool Button::isLatched() const { return latched; }

const char* Button::getPath() const { return samplePath; }


bool Button::readPressedHardware() const {
  if (useMultiplexer) {
    // Mux module centrally tracks its active-low polarity (set by
    // initMuxScanner). Let the mux module interpret the line state so we
    // don't duplicate the polarity flag across modules.
    return readMuxActiveState(static_cast<uint8_t>(pin));
  }
  int level = digitalRead(pin);
  return activeLow ? (level == LOW) : (level == HIGH);
}

