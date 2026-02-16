#include "button.h"
#include <Arduino.h>
#include "config/config.h"

Button::Button(int pinOrChannel) : pin(pinOrChannel) {}

// --- Buttons ---
Button buttons[BUTTON_COUNT] = {
  Button(BUTTON_CHANNEL_ON_MUX[0]),
  Button(BUTTON_CHANNEL_ON_MUX[1]),
  Button(BUTTON_CHANNEL_ON_MUX[2]),
  Button(BUTTON_CHANNEL_ON_MUX[3]),
  Button(BUTTON_CHANNEL_ON_MUX[4]),
  Button(BUTTON_CHANNEL_ON_MUX[5]),
};

int findButtonIndexForChannel(uint8_t channel) {
  for (int i = 0; i < BUTTON_COUNT; ++i) {
    if (BUTTON_CHANNEL_ON_MUX[i] == channel) {
      return i;
    }
  }
  return -1;
}

void Button::release() { 
  latched = false; 
  lastTriggerTime = 0; 
}

void releaseAllButtons() {
  for (int i = 0; i < BUTTON_COUNT; ++i) {
    buttons[i].release();
  }
}

