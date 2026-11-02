// controls.cpp — performance controls (see controls.h)
#include "controls.h"

#include <Arduino.h>
#include "audio/audio_engine.h"
#include "audio/bluetooth.h"
#include "config/config.h"
#include "config/pins.h"
#include "config/settings.h"
#include "input/button.h"
#include "input/mux.h"
#include "settings_mode.h"
#include "ui.h"

// Cutoff target while the filter switch is off — high enough to be inaudible,
// and gliding there (instead of bypassing) keeps switching click-free.
static constexpr float FILTER_OPEN_HZ = 20000.0f;

// --- Runtime state ----------------------------------------------------------
static uint32_t lastPotRead            = 0;
static float    lastVol                = -1.0f;
static bool     switchDelaySendEnabled = false;
static bool     switchFilterEnabled    = false;
static float    filterCutoff           = LOW_PASS_CUTOFF_HZ;
// Written by the settings menu (input task), read in the loop task.
static volatile float filterQ   = LOW_PASS_Q;
static volatile float btFilterQ = BT_FILTER_Q;

// --- Filters -----------------------------------------------------------------
// BT mode has no delay, so while BT is connected the delay switch engages a
// low-pass on the BT audio instead. Both filters share the pot cutoff.
static bool btFilterActive() {
#ifdef BLUETOOTH_MODE
    return btIsConnected() && switchDelaySendEnabled;
#else
    return false;
#endif
}

// The pot sets the cutoff while any filter is engaged, otherwise the volume.
static bool potControlsCutoff() {
    return switchFilterEnabled || btFilterActive();
}

// Only sets the targets; audioTask glides each cutoff there and updates the
// coefficients between blocks (see smooth_lowpass.h).
static void updateFilterTargets() {
    setInsertFilterTarget(switchFilterEnabled ? filterCutoff : FILTER_OPEN_HZ, filterQ);
#ifdef BLUETOOTH_MODE
    // When off, the BT filter also glides back to a neutral Q so no
    // resonance peak lingers near 20 kHz.
    constexpr float kNeutralQ = 0.7071f;
    if (btFilterActive()) setBtFilterTarget(filterCutoff, btFilterQ);
    else                  setBtFilterTarget(FILTER_OPEN_HZ, kNeutralQ);
#endif
}

// Keeps the HUD in sync when the pot role or the BT filter state changes
// (switches toggling, BT connecting/disconnecting).
static void updateFilterHud() {
    static int lastRole = -1;
    int role = potControlsCutoff() ? 1 : 0;
    if (role != lastRole) {
        lastRole = role;
        setHudPotValue(lastVol < 0.0f ? 0.0f : lastVol, role == 1);
    }
#ifdef BLUETOOTH_MODE
    setHudBtFilter(btIsConnected() ? (switchDelaySendEnabled ? 1 : 0) : -1);
#endif
}

// --- Mux change callback ----------------------------------------------------
// Never touches player/mixer — playSample()/stopSample() go through the
// audio command queue.
void onMuxChange(uint8_t channel, bool active) {
    if (channel == runtimeSwitchFilterChannel && active != switchFilterEnabled) {
        switchFilterEnabled = active;
        if (DEBUGMODE) Serial.printf("[MUX] Filter: %s\n", active ? "ON" : "OFF");
        return;
    }

    if (channel == runtimeSwitchDelayChannel && active != switchDelaySendEnabled) {
        switchDelaySendEnabled = active;
        setHudDelayEnabled(active);
        if (DEBUGMODE) Serial.printf("[MUX] Delay send: %s\n", active ? "ON" : "OFF");
        return;
    }

    int index = findButtonIndexForChannel(channel);
    if (index < 0) return;

    if (handleSettingsButtonInput(static_cast<size_t>(index), active)) return;

    if (active) {
        playSample(index);
    } else {
        stopSample(index);
    }

    if (DEBUGMODE) Serial.printf("[MUX] ch=%u active=%d\n", channel, (int)active);
}

// --- Pot ---------------------------------------------------------------------
// Averages a few ADC reads — the ESP32 ADC is noisy, and the filter glide
// makes small steps inaudible, so a finer change threshold is usable.
static float readPotNormalized() {
    constexpr int kReads = 4;
    int sum = 0;
    for (int i = 0; i < kReads; ++i) sum += analogRead(POT_PIN);
    int raw = sum / kReads;
    if (runtimePotInverted) raw = 4095 - raw;
    return constrain(raw, 0, 4095) / 4095.0f;
}

static void checkPot(uint32_t now) {
    if ((now - lastPotRead) < POT_READ_INTERVAL_MS) return;
    lastPotRead = now;

    float norm = readPotNormalized();

    if (potControlsCutoff()) {
        if (lastVol < 0.0f || fabsf(norm - lastVol) > 0.01f) {
            lastVol = norm;
            filterCutoff = LOW_PASS_MIN_HZ + norm * (LOW_PASS_MAX_HZ - LOW_PASS_MIN_HZ);
            setHudPotValue(norm, true);
        }
    } else {
        if (lastVol < 0.0f || fabsf(norm - lastVol) > 0.02f) {
            lastVol = norm;
            setPlaybackVolume(lastVol);
            setHudPotValue(norm, false);
        }
    }
}

// --- Public ------------------------------------------------------------------
void initControls() {
    bool filterState = readMuxActiveState(runtimeSwitchFilterChannel);
    bool delayState  = readMuxActiveState(runtimeSwitchDelayChannel);

    onMuxChange(runtimeSwitchFilterChannel, filterState);
    onMuxChange(runtimeSwitchDelayChannel, delayState);

    // Bootstrap pot so volume/filter is correct from the very first frame.
    float norm = readPotNormalized();

    if (filterState) {
        filterCutoff = LOW_PASS_MIN_HZ + norm * (LOW_PASS_MAX_HZ - LOW_PASS_MIN_HZ);
        updateFilterTargets(); // first audio tick jumps straight to it
        setHudPotValue(norm, true);
    } else {
        lastVol = norm;
        setPlaybackVolume(lastVol);
        setHudPotValue(norm, false);
    }

    // The mixer doesn't exist yet: initDelayAndMixer() applies this state
    // via delaySendSwitchOn() after mixer.begin().
    switchDelaySendEnabled = delayState;
    setHudDelayEnabled(delayState);

    lastPotRead = millis();
}

bool delaySendSwitchOn() {
    return switchDelaySendEnabled;
}

void setFilterQ(float q) {
    filterQ = q;
}

void setBtFilterQ(float q) {
    btFilterQ = q;
}

void updateControls(uint32_t now) {
    checkPot(now);
    updateFilterTargets();
    updateFilterHud();

    // Without a delay buffer (BT mode) the mixer bypasses the send anyway.
    setDelaySendEnabled(switchDelaySendEnabled);
}
