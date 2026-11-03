// audio_engine.cpp — playback pipeline and audio task (see audio_engine.h)
#include "audio_engine.h"

#include <SPI.h>
#include <SD.h>
#include <AudioTools.h>
#include "AudioTools/Disk/AudioSourceSD.h"
#include "audio_mixer.h"
#include "bluetooth.h"
#include "prealloc_delay.h"
#include "smooth_lowpass.h"
#include "wav_data_decoder.h"
#include "config/config.h"
#include "config/pins.h"
#include "config/settings.h"
#include "storage/sample_map.h"
#include "ui.h"

// --- Audio system -----------------------------------------------------------
static AudioInfo info(44100, 2, 16);
static AudioSourceSD source("/", "wav");
static WavDataDecoder decoder;

static DelayMixerStream mixer;
static FilteredStream<int16_t, float> filteredStream(mixer, 2);
static AudioPlayer player(source, filteredStream, decoder);

static SmoothedLowPass insertFilter;
static LowPassDF1      insertFilterL(insertFilter.coeffs());
static LowPassDF1      insertFilterR(insertFilter.coeffs());
static PreallocDelay delayEffect1;

// --- Task infrastructure ----------------------------------------------------
// Static stack — reserved at link time, never fragments the heap at runtime.
// Measured peak ~2.4 KB with BT streaming and debug prints on.
static StaticTask_t audioTaskTCB;
static StackType_t  audioTaskStack[5 * 1024];

// --- Command queue (inputTask / loop / BT callbacks -> audioTask) -----------
// A queue keeps commands ordered: a quick press+release is always seen as
// play-then-stop, and it wakes audioTask immediately instead of after a poll.
enum class AudioCmdType : uint8_t { Play, Stop, BtConnected, BtDisconnected };
struct AudioCmd {
    AudioCmdType type;
    int8_t       index; // button index for Play/Stop, unused otherwise
};
static constexpr UBaseType_t AUDIO_CMD_QUEUE_LEN = 16;
static StaticQueue_t audioCmdQueueStruct;
static uint8_t       audioCmdQueueStorage[AUDIO_CMD_QUEUE_LEN * sizeof(AudioCmd)];
static QueueHandle_t audioCmdQueue = nullptr;

static void sendAudioCmd(AudioCmdType type, int index = -1) {
    if (!audioCmdQueue) return;
    AudioCmd cmd{type, static_cast<int8_t>(index)};
    if (xQueueSend(audioCmdQueue, &cmd, 0) != pdTRUE && DEBUGMODE)
        Serial.println(F("[AUDIO] Command queue full — command dropped"));
}

// Playback state lives only in audioTask — never read by other tasks.
static int currentSample = -1;

// Written by the settings menu (input task), read by audioTask and stopSample().
static volatile bool oneShot = ONE_SHOT_DEFAULT;

// --- Audio task (Core 1, prio 5) --------------------------------------------
static void handleAudioCmd(const AudioCmd &cmd) {
    switch (cmd.type) {
    case AudioCmdType::Play:
        // The fader has one length for both directions. Short here: setPath()
        // fades out a still-playing sample with it, then the new one fades in.
        player.setFadeTime(SAMPLE_FADE_IN_MS);
        player.setPath(getSamplePathForButton(cmd.index));
        player.setActive(true);
        currentSample = cmd.index;
        break;

    case AudioCmdType::Stop:
        // Release of a button whose sample is no longer the active one.
        if (cmd.index != currentSample) break;
        player.setFadeTime(BUTTON_FADE_MS);
        player.setActive(false); // writes the fade-out right away
        currentSample = -1;
        break;

#ifdef BLUETOOTH_MODE
    // No delay exists while BT is enabled, so there is nothing to free or
    // reallocate here — just start every session with an empty jitter buffer.
    case AudioCmdType::BtConnected:
    case AudioCmdType::BtDisconnected:
        btAudioBuffer.clear();
        if (DEBUGMODE)
            Serial.printf("[AUDIO] BT %s, heap=%d (min %d)\n",
                          cmd.type == AudioCmdType::BtConnected ? "connected" : "disconnected",
                          ESP.getFreeHeap(), ESP.getMinFreeHeap());
        break;
#else
    default:
        break;
#endif
    }
}

static void audioTask(void *) {
    for (;;) {
        AudioCmd cmd;
        while (xQueueReceive(audioCmdQueue, &cmd, 0) == pdTRUE) handleAudioCmd(cmd);

        size_t copied = 0;

        if (player.isActive()) {
            // Drain loop: push audio until I2S DMA is full or no more data.
            // mixer.write() reads btAudioBuffer on every call, so BT is
            // consumed proportional to I2S output.
            for (int i = 0; i < 16; ++i) {
                // Past the data chunk the file only holds metadata: the
                // sample has ended (copied stays 0).
                if (decoder.atDataEnd()) break;
                // Glide the filter cutoff once per block, never mid-block.
                insertFilter.tick(info.sample_rate);
                size_t n = player.copy();
                copied += n;
                if (n == 0) break;
                // Once the DMA is full every copy blocks ~5.8 ms: a button
                // press must not wait for the rest of the drain.
                if (uxQueueMessagesWaiting(audioCmdQueue) > 0) break;
            }

            // One-shot end detection
            if (oneShot && currentSample >= 0 && copied == 0) {
                player.setFadeTime(BUTTON_FADE_MS);
                player.setActive(false);
                currentSample = -1;
                if (DEBUGMODE) Serial.println(F("[AUDIO] One-shot end"));
            }
        }

        // Nothing copied (idle, or a sample that ended while its button is
        // still held): pump silence so the delay tail drains, the scope stays
        // alive and (in BT mode) the BT audio from the jitter buffer is mixed in.
        if (copied == 0) mixer.pumpSilenceFrames(kScopeSilenceFramesPerLoop);

#ifdef BLUETOOTH_MODE
        if (DEBUGMODE && btIsConnected()) reportBtBufferStats();
#endif

        // Rate control: blocking I2S writes govern the rate; additionally wait
        // up to 1 ms, but wake immediately when a new command arrives.
        xQueuePeek(audioCmdQueue, &cmd, pdMS_TO_TICKS(1));
    }
}

void startAudioTask() {
    xTaskCreateStaticPinnedToCore(
        audioTask, "audioTask",
        sizeof(audioTaskStack) / sizeof(StackType_t),
        nullptr,
        5,              // high priority — preempts loop()/UI
        audioTaskStack,
        &audioTaskTCB,
        1               // Core 1 — same core as I2S driver
    );
}

// --- Commands ----------------------------------------------------------------
void playSample(int index) {
    sendAudioCmd(AudioCmdType::Play, index);
    if (DEBUGMODE) Serial.printf("[PLAY] %s\n", getSamplePathForButton(index));
}

void stopSample(int index) {
    if (oneShot) return; // one-shot ends itself
    // audioTask ignores the stop if this button's sample is no longer playing.
    sendAudioCmd(AudioCmdType::Stop, index);
}

#ifdef BLUETOOTH_MODE
void notifyBtConnectionChanged(bool connected) {
    sendAudioCmd(connected ? AudioCmdType::BtConnected : AudioCmdType::BtDisconnected);
}
#endif

// --- Live parameters -----------------------------------------------------------
void setOneShotEnabled(bool enabled) {
    oneShot = enabled;
}

void setPlaybackVolume(float volume) {
    player.setVolume(volume);
}

void setInsertFilterTarget(float cutoffHz, float q) {
    insertFilter.setTarget(cutoffHz, q);
}

void setDelaySendEnabled(bool enabled) {
    mixer.sendEnabled(enabled);
}

#ifdef BLUETOOTH_MODE
void setBtFilterTarget(float cutoffHz, float q) {
    mixer.setBtFilterTarget(cutoffHz, q);
}
#endif

// --- Init ----------------------------------------------------------------------
void initAudioCommandQueue() {
    audioCmdQueue = xQueueCreateStatic(AUDIO_CMD_QUEUE_LEN, sizeof(AudioCmd),
                                       audioCmdQueueStorage, &audioCmdQueueStruct);
}

// No setVolume() here: initControls() already set it from the pot, and
// begin() keeps it.
void initPlayer() {
    player.setOutput(filteredStream);
    player.setSilenceOnInactive(false);
    player.setFadeTime(BUTTON_FADE_MS);
    player.begin();
    // begin() takes auto-next from the source, which is on for SD. Off, or a
    // button held past the end of its sample plays on into the next file.
    player.setAutoNext(false);
    // At the end of a file the copier sleeps 10 ms per copy() — in audioTask,
    // which may only ever block on I2S. That chopped the delay tail into
    // pieces while a button was held past the end of its sample, and made
    // each trigger after a finished sample 10 ms late (the fade-out of the
    // old file reads at its end), often longer than the DMA.
    player.getStreamCopy().setDelayOnNoData(0);
    player.setActive(false);
    if (DEBUGMODE) Serial.println(F("[INIT] Player OK"));
}

void initAudio() {
    if (DEBUGMODE) {
        Serial.printf("[HEAP] Before audio init: %d free\n", ESP.getFreeHeap());
        Serial.printf("[PSRAM] Size=%d Free=%d\n", ESP.getPsramSize(), ESP.getFreePsram());
    }

    auto config         = scopeI2s.defaultConfig(TX_MODE);
    config.copyFrom(info);
    config.pin_bck      = I2S_PIN_BCK;
    config.pin_ws       = I2S_PIN_WS;
    config.pin_data     = I2S_PIN_DATA;
    config.i2s_format   = I2S_STD_FORMAT;
    // DMA depth = trigger latency and SD-stall headroom (see config.h).
    config.buffer_count = btRunning() ? I2S_DMA_BUFFER_COUNT_BT : I2S_DMA_BUFFER_COUNT;
    config.buffer_size  = I2S_DMA_BUFFER_FRAMES;
    Serial.printf("[AUDIO] I2S DMA %d x %d frames (~%d ms)\n",
                  config.buffer_count, config.buffer_size,
                  config.buffer_count * config.buffer_size * 1000 / (int)info.sample_rate);

    if (!scopeI2s.begin(config)) {
        Serial.println(F("[ERROR] I2S init failed"));
    } else {
        scopeI2s.setAudioInfo(info);
    }

    filteredStream.setFilter(0, &insertFilterL);
    filteredStream.setFilter(1, &insertFilterR);

    if (DEBUGMODE) Serial.println(F("[INIT] Audio pipeline OK"));
}

// Keeps SAFETY_BYTES free for runtime allocations (SD lib, FreeRTOS, UI).
uint16_t computeMaxDelayMs() {
    constexpr uint32_t SAFETY_BYTES = 15 * 1024;
    constexpr uint16_t ABS_MIN_MS   = 50;
    constexpr uint16_t ABS_MAX_MS   = DELAY_TIME_ABS_MAX_MS;

    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap <= SAFETY_BYTES) return ABS_MIN_MS;

    uint32_t budget = freeHeap - SAFETY_BYTES;
    // The delay vector needs one contiguous block, which can be far smaller
    // than the total free heap.
    constexpr uint32_t BLOCK_MARGIN = 1024;
    uint32_t largestBlock = ESP.getMaxAllocHeap();
    budget = std::min(budget, largestBlock > BLOCK_MARGIN ? largestBlock - BLOCK_MARGIN : 0u);

    uint32_t maxMs = (budget / sizeof(PreallocDelay::effect_t) * 1000UL) / (uint32_t)info.sample_rate;

    if (maxMs < ABS_MIN_MS) return ABS_MIN_MS;
    if (maxMs > ABS_MAX_MS) return ABS_MAX_MS;
    return static_cast<uint16_t>(maxMs);
}

void initDelayAndMixer(uint16_t maxDelayMs, bool sendEnabled) {
    uint16_t defMs = std::min(maxDelayMs, static_cast<uint16_t>(DEFAULT_DELAY_TIME_MS));

    if (DEBUGMODE)
        Serial.printf("[HEAP] Delay: max=%ums, free=%u before alloc\n",
                      maxDelayMs, ESP.getFreeHeap());

    if (!delayEffect1.begin(info.sample_rate, maxDelayMs, defMs, DEFAULT_DELAY_FEEDBACK)
        && maxDelayMs > 0)
        Serial.printf("[ERROR] Delay alloc of %u ms failed — delay bypassed\n", maxDelayMs);

    if (DEBUGMODE)
        Serial.printf("[HEAP] After delay alloc: %u free\n", ESP.getFreeHeap());

    mixer.begin(scopeI2s, delayEffect1, info);
    mixer.sendEnabled(sendEnabled);
}

PreallocDelay& getDelayEffect() {
    return delayEffect1;
}
