#include "button.h"
#include <Arduino.h>
#include "config/config.h"

Button::Button(int pinOrChannel) : pin(pinOrChannel) {}

// --- Buttons ---
// Initialized with channel 0 as placeholder; setButtonChannel() must be called
// before use (done in applyPinConfig() after loading from SD / wizard).
Button buttons[BUTTON_COUNT] = {
  Button(0), Button(0), Button(0),
  Button(0), Button(0), Button(0),
};

void setButtonChannel(int index, uint8_t channel) {
  if (index >= 0 && index < (int)BUTTON_COUNT) {
    buttons[index].pin = channel;
  }
}

int findButtonIndexForChannel(uint8_t channel) {
  for (int i = 0; i < (int)BUTTON_COUNT; ++i) {
    if (buttons[i].pin == (int)channel) {
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
  for (int i = 0; i < (int)BUTTON_COUNT; ++i) {
    buttons[i].release();
  }
}
