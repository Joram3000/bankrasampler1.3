// battery.cpp — LiPo charge indicator (see battery.h)
#include "battery.h"

#include <Arduino.h>
#include "config/config.h"
#include "config/pins.h"
#include "ui.h"

// LiPo voltage via a 1:2 divider on BATTERY_ADC_PIN. The indicator stays
// hidden below ~1 V at the pin (no divider fitted) and above what a 1S LiPo
// can reach: on USB without a battery the charge module puts ~4.85 V there.
static constexpr uint32_t BATTERY_READ_INTERVAL_MS = 2000;
static constexpr float    BATTERY_MIN_MV = 2000.0f;
static constexpr float    BATTERY_MAX_MV = 4400.0f;
static uint32_t lastBatteryRead = 0;
static float    batteryMvFiltered = 0.0f;

static int batteryPercentFromMv(float mv) {
    // Approximate 1S LiPo discharge curve (resting voltage).
    static constexpr struct { float mv; int pct; } kCurve[] = {
        {4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50},
        {3750, 40},  {3700, 30}, {3650, 20}, {3600, 12}, {3500, 5}, {3300, 0},
    };
    if (mv >= kCurve[0].mv) return 100;
    for (size_t i = 1; i < sizeof(kCurve) / sizeof(kCurve[0]); ++i) {
        if (mv >= kCurve[i].mv) {
            float t = (mv - kCurve[i].mv) / (kCurve[i - 1].mv - kCurve[i].mv);
            return kCurve[i].pct + (int)(t * (kCurve[i - 1].pct - kCurve[i].pct) + 0.5f);
        }
    }
    return 0;
}

void checkBattery(uint32_t now) {
    if (lastBatteryRead && now - lastBatteryRead < BATTERY_READ_INTERVAL_MS) return;
    lastBatteryRead = now;

    constexpr int kReads = 16;
    uint32_t sum = 0;
    for (int i = 0; i < kReads; ++i) sum += analogReadMilliVolts(BATTERY_ADC_PIN);
    float mv = (sum / (float)kReads) * 2.0f;  // undo 1:2 divider

    if (mv < BATTERY_MIN_MV || mv > BATTERY_MAX_MV) {  // no battery
        batteryMvFiltered = 0.0f;
        setHudBattery(-1);
        return;
    }
    batteryMvFiltered = batteryMvFiltered == 0.0f ? mv : batteryMvFiltered * 0.8f + mv * 0.2f;
    setHudBattery(batteryPercentFromMv(batteryMvFiltered));
    if (DEBUGMODE) Serial.printf("[BAT] %.0f mV\n", batteryMvFiltered);
}
