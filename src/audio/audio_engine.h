// audio_engine.h — sample playback, effects pipeline and the audio task
//
// Pipeline: SD wav → player → insert low-pass → mixer (delay send/return and,
// in BT mode, the BT audio) → scopeI2s. audioTask (Core 1, prio 5) is the sole
// owner of player/mixer and the only task that writes to I2S; other tasks
// send it commands through a queue.
#pragma once
#include <cstdint>

class PreallocDelay;

// --- Init, in this order -----------------------------------------------------
// Before anything that can send commands (mux callback, BT callbacks).
void initAudioCommandQueue();
// I2S output + insert filter. The delay and mixer follow in initDelayAndMixer()
// once the heap used by BT and other init is known.
void initAudio();
void initPlayer();
// Largest delay that fits in the free heap. Only used without BT.
uint16_t computeMaxDelayMs();
// Must run after all other heap-consuming init so computeMaxDelayMs() is
// accurate. maxDelayMs == 0 -> no delay buffer at all (BT mode); the mixer
// bypasses it.
void initDelayAndMixer(uint16_t maxDelayMs, bool sendEnabled);
PreallocDelay& getDelayEffect();
// Last, once everything the audio task touches is initialised.
void startAudioTask();

// --- Commands (any task) -----------------------------------------------------
void playSample(int index);
void stopSample(int index);
#ifdef BLUETOOTH_MODE
void notifyBtConnectionChanged(bool connected);
#endif

// --- Live parameters ---------------------------------------------------------
// One-shot: a sample plays to its end and button releases are ignored.
// Any task.
void setOneShotEnabled(bool enabled);
// Loop task. Filter setters only set targets; audioTask glides each cutoff
// there and updates the coefficients between blocks (see smooth_lowpass.h).
void setPlaybackVolume(float volume);
void setInsertFilterTarget(float cutoffHz, float q);
void setDelaySendEnabled(bool enabled);
#ifdef BLUETOOTH_MODE
void setBtFilterTarget(float cutoffHz, float q);
#endif
