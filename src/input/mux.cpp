#include "mux.h"
#include <Arduino.h>
#include <AudioTools.h>
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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

// 74HC151 heeft 8 kanalen
static const uint8_t MUX_CHANNELS = 8;

// change-detect state
static bool muxLastState[MUX_CHANNELS];
static bool muxActiveLow = MUX_ACTIVE_LOW;
static MuxChangeCallback muxChangeCallback = nullptr;

// Mutex used for ISR/Timer critical sections (ESP32 portMUX)
static portMUX_TYPE muxTimerMux = portMUX_INITIALIZER_UNLOCKED;

// MUX scan timer (interrupt-driven)
static hw_timer_t* muxTimer = nullptr;
static volatile bool muxScanPending = false;
static uint32_t muxScanIntervalUs = 5000;

static void IRAM_ATTR onMuxTimer() {
  portENTER_CRITICAL_ISR(&muxTimerMux);
  muxScanPending = true;
  portEXIT_CRITICAL_ISR(&muxTimerMux);
}

void Sn74hc151Mux::begin() {
  if (initialized) return;
  // Only configure pins that are configured (>= 0). This avoids compile/runtime issues
  // when a pin is intentionally left unconfigured in config.h (set to -1).
  if (INPUT_MUX_PIN_A >= 0) pinMode(INPUT_MUX_PIN_A, OUTPUT);
  if (INPUT_MUX_PIN_B >= 0) pinMode(INPUT_MUX_PIN_B, OUTPUT);
  if (INPUT_MUX_PIN_C >= 0) pinMode(INPUT_MUX_PIN_C, OUTPUT);
  if (INPUT_MUX_PIN_Y >= 0) pinMode(INPUT_MUX_PIN_Y, INPUT);
  selectChannel(0);
  initialized = true;
  Serial.println(F("Input MUX initialized"));
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




bool readMuxActiveState(uint8_t channel) {
  int level = gInputMux.readChannel(channel);
  // Use the module-level `muxActiveLow` flag configured via initMuxScanner
  return muxActiveLow ? (level == LOW) : (level == HIGH);
}

static void initMuxState() {
  for (uint8_t ch = 0; ch < MUX_CHANNELS; ++ch) {
    muxLastState[ch] = readMuxActiveState(ch);
  }
}

static void scanMuxAndPrint() {
  for (uint8_t ch = 0; ch < MUX_CHANNELS; ++ch) {
  bool state = readMuxActiveState(ch); // externe pullups => activeLow
    if (state != muxLastState[ch]) {
      muxLastState[ch] = state;
      if (muxChangeCallback) {
        muxChangeCallback(ch, state);
      } else {
        Serial.print(F("MUX "));
        Serial.print(ch);
        Serial.print(F(" = "));
        Serial.println(state ? F("ACTIVE") : F("INACTIVE"));
      }
    }
  }
}

static void initMuxTimer() {
  if (muxTimer != nullptr) {
    timerAlarmDisable(muxTimer);
    timerDetachInterrupt(muxTimer);
    timerEnd(muxTimer);
  }
  // ESP32: timer 0, prescaler 80 => 1 tick = 1 us
  muxTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(muxTimer, &onMuxTimer, true);
  timerAlarmWrite(muxTimer, muxScanIntervalUs, true);
  timerAlarmEnable(muxTimer);
}

void initMuxScanner(uint32_t scanIntervalUs, bool activeLow) {
  muxScanIntervalUs = scanIntervalUs;
  muxActiveLow = activeLow;
  initMuxState();
  initMuxTimer();
}

// To be called periodically from main loop
void muxScanTick() {
  bool doScan = false;
  portENTER_CRITICAL(&muxTimerMux);
  if (muxScanPending) {
    muxScanPending = false;
    doScan = true;
  }
  portEXIT_CRITICAL(&muxTimerMux);

  if (doScan) {
    scanMuxAndPrint();
  }
}

void setMuxChangeCallback(MuxChangeCallback callback) {
  muxChangeCallback = callback;
}