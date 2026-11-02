// controls.h — performance controls: pot, filter switch, delay switch, buttons
//
// The pot sets the volume, or the cutoff while a filter is engaged. The filter
// switch engages the insert low-pass; the delay switch the delay send, or while
// BT is connected (BT mode has no delay) a low-pass on the BT audio. Sample
// buttons go to the settings menu when it is open, otherwise to the player.
#pragma once
#include <cstdint>

// Mux change callback — runs in the input task.
void onMuxChange(uint8_t channel, bool active);

// Reads both switches and the pot so volume/filter are right from the very
// first frame. Call once the pin config is loaded.
void initControls();

// Delay switch state, for initDelayAndMixer().
bool delaySendSwitchOn();

// Filter resonance from the settings menu (any task). The BT filter Q only
// matters in BT mode.
void setFilterQ(float q);
void setBtFilterQ(float q);

// Loop task: polls the pot, updates filter targets, HUD and delay send.
void updateControls(uint32_t now);
