#pragma once

#include <Arduino.h>

int findButtonIndexForChannel(uint8_t channel);
void releaseAllButtons();
void setButtonChannel(int index, uint8_t channel);


class Button {
public:
  Button(int pinOrChannel);
  void begin();
  void release();

  int pin; // public so applyPinConfig() and the wizard can set it

private:
  bool latched = false;
  uint32_t lastTriggerTime = 0;
};


