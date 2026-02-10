#pragma once

#include <Arduino.h>

int findButtonIndexForChannel(uint8_t channel);
void releaseAllButtons();


class Button {
public:
  Button(int pinOrChannel);
  void begin();
  void release();

private:
  int pin;
  bool latched = false;
  uint32_t lastTriggerTime = 0;
};


