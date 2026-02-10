#pragma once

#include <Arduino.h>


class Button {
public:
  Button(int pinOrChannel, const char* samplePath);
  void begin();
  // bool update(uint32_t now);
  void release();
  void sync(uint32_t now);
  // bool readRaw() const;
  bool isLatched() const;
  // const char* getPath() const;

private:
  int pin;
  const char* samplePath;
  bool activeLow = true;
  bool rawState = false;
  bool debouncedState = false;
  bool latched = false;
  // bool useMultiplexer = false;
  uint32_t lastDebounceTime = 0;
  uint32_t lastTriggerTime = 0;
  bool readPressedHardware() const;
};


