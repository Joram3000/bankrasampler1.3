#pragma once
#include <Arduino.h>

class BpmTap {
public:
    // timeoutMs = reset tijd in milliseconden (default 2000 ms)
    explicit BpmTap(unsigned long timeoutMs = 2000) ;

    // registreer een tap (roep aan bij knopdruk) en returned de averageInterval
    void tap();

    // reset internal state (bijv. handmatig of na timeout)
    void reset();

    // return BPM berekend uit gemiddelde interval (0 als niet genoeg taps)
    float getBPM();

    // extra helpers
    unsigned int getTapCount() const;
    unsigned long getAverageInterval() const; // in ms

private:
    unsigned int tapCount;
    unsigned long lastTapTime;
    float totalInterval; // sum of intervals in ms
    unsigned long timeoutMs;

    // check en reset als er timeout is verlopen sinds lastTapTime
    void resetIfTimedOut();
};