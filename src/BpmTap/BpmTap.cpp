// this is used to calculate the BPM based on the time between taps
// we are using the back button in settings mode for this
// when tapping, calculate the average speed of the taps and calculate the BPM based on that and use it for the delay length
// if there is no new tap for 2 seconds, reset the tap count and start over




#include "BpmTap.h"

BpmTap::BpmTap(unsigned long timeoutMs)
    : tapCount(0), lastTapTime(0), totalInterval(0), timeoutMs(timeoutMs) {}

void BpmTap::reset() {
    tapCount = 0;
    lastTapTime = 0;
    totalInterval = 0;
}

void BpmTap::resetIfTimedOut() {
    if (tapCount > 0 && (millis() - lastTapTime) > timeoutMs) {
        reset();
    }
}

void BpmTap::tap()  {
    unsigned long currentTime = millis();
    // reset sequence if previous taps timed out
    if (tapCount > 0 && (currentTime - lastTapTime) > timeoutMs) {
        reset();
    }

    if (tapCount > 0) {
        unsigned long interval = currentTime - lastTapTime;
        totalInterval += interval;
    }

    lastTapTime = currentTime;
    tapCount++;

}

float BpmTap::getBPM() {
    resetIfTimedOut();

    if (tapCount < 2) return 0.0f;
    unsigned long intervalsCount = tapCount - 1;
    float avgIntervalMs = float(totalInterval) / float(intervalsCount);
    if (avgIntervalMs <= 0.0f) return 0.0f;
    return 60000.0f / avgIntervalMs; // 60_000 ms per minute
}

unsigned int BpmTap::getTapCount() const {
    return tapCount;
}

unsigned long BpmTap::getAverageInterval() const {
    if (tapCount < 2) return 0;
    return totalInterval / (tapCount - 1);
}