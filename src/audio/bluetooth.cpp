// bluetooth.cpp — A2DP sink (see bluetooth.h)
#include "bluetooth.h"

#ifdef BLUETOOTH_MODE
#include <SD.h>
#include <BluetoothA2DPSink.h>
#include "audio_engine.h"
#include "audio_mixer.h"
#include "config/config.h"
#include "config/settings.h"
#include "ui.h"

bool btEnabled = DEFAULT_BT_ENABLED;

// BT jitter buffer — 20 KB = ~113 ms at 44100 Hz stereo 16-bit.
// The BT callback only ever writes here; audioTask is the sole I2S writer.
// Playback starts once 10 KB (~58 ms) is queued: a Pixel 7 was measured
// leaving gaps > 29 ms between A2DP packets (underrun every 5–10 s with a
// 5 KB prebuffer), followed by catch-up bursts. Above 16 KB the reader drops
// single frames to absorb clock drift / SD stalls; the 4 KB above that
// absorbs a catch-up burst. Heap: ~26 KB was still free at the low point
// while streaming with a 12 KB buffer, so ~18 KB remains with this one.
BTRingBuffer btAudioBuffer(20 * 1024, 10 * 1024, 16 * 1024);
static BluetoothA2DPSink a2dp_sink;
static volatile bool btConnected = false;

bool btRunning() {
    return btEnabled;
}

bool btIsConnected() {
    return btConnected;
}

static bool readBtEnabledSetting() {
    if (!SD.exists("/settings.txt")) return DEFAULT_BT_ENABLED;
    File f = SD.open("/settings.txt", FILE_READ);
    if (!f) return DEFAULT_BT_ENABLED;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("bt_enabled=")) {
            f.close();
            return line.substring(11).toInt() != 0;
        }
    }
    f.close();
    return DEFAULT_BT_ENABLED;
}

void loadBtEnabledFromSd() {
    btEnabled = readBtEnabledSetting();
    Serial.printf("[BT] bt_enabled=%d (from settings)\n", btEnabled);
}

// Periodic jitter-buffer health report — drops mean the buffer overflowed,
// underruns mean it ran dry mid-stream (also logged once when the phone pauses).
// Also reports each new heap low-water mark: the BT stack fails silently
// when it runs out of heap, so this is the number to watch.
void reportBtBufferStats() {
    static uint32_t lastReport  = 0;
    static uint32_t lastMinHeap = UINT32_MAX;
    uint32_t now = millis();
    if (now - lastReport < 5000) return;
    lastReport = now;
    uint32_t drops     = btAudioBuffer.getAndResetDropCount();
    uint32_t underruns = btAudioBuffer.getAndResetUnderrunCount();
    if (drops || underruns)
        Serial.printf("[BT] Jitter buffer: %u drop(s), %u underrun(s), level=%u B\n",
                      drops, underruns, (unsigned)btAudioBuffer.available());
    uint32_t minHeap = ESP.getMinFreeHeap();
    if (minHeap < lastMinHeap) {
        lastMinHeap = minHeap;
        Serial.printf("[HEAP] New low: %u B free (now %u B)\n", minHeap, ESP.getFreeHeap());
    }
}

// --- Bluetooth callbacks -----------------------------------------------------
// Runs in the BT stack task (Core 0). Only feeds the jitter buffer — it never
// touches I2S, so it never blocks the BT stack and never races audioTask.
static void btAudioDataCallback(const uint8_t *data, uint32_t length) {
    btAudioBuffer.write(data, length);
}

static void btConnectionStateChanged(esp_a2d_connection_state_t state, void *) {
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        btConnected = true;
        notifyBtConnectionChanged(true);
        setHudBtConnected(true);

        esp_bd_addr_t *addr = a2dp_sink.get_last_peer_address();
        Serial.printf("[BT] *** CONNECTED *** addr=%s  heap=%d\n",
                      a2dp_sink.to_str(*addr), ESP.getFreeHeap());

    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        btConnected = false;
        notifyBtConnectionChanged(false);
        setHudBtConnected(false);
        // Re-enable discoverability so the phone can reconnect without forgetting the device.
        // The BT stack may have left the device non-connectable after the session ended.
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        Serial.printf("[BT] *** DISCONNECTED ***  heap=%d\n", ESP.getFreeHeap());

    } else if (state == ESP_A2D_CONNECTION_STATE_CONNECTING) {
        Serial.printf("[BT] Connecting...  heap=%d\n", ESP.getFreeHeap());

    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTING) {
        Serial.printf("[BT] Disconnecting...  heap=%d\n", ESP.getFreeHeap());
    }
}

void initBluetooth() {
    if (DEBUGMODE)
        Serial.printf("[BT] Heap before A2DP start: %d\n", ESP.getFreeHeap());

    btAudioBuffer.begin();

    // Release BLE heap — we only use Classic BT (A2DP).
    // Frees ~30 KB of contiguous DRAM for the Classic BT controller.
    // Must be called before btStart() / a2dp_sink.start().
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

    a2dp_sink.set_stream_reader(btAudioDataCallback, false);
    a2dp_sink.set_sample_rate_callback([](uint16_t rate) {
        Serial.printf("[BT] Negotiated sample rate: %u Hz%s\n", rate,
                      rate != 44100 ? " <- MISMATCH! Expect crackle." : "");
    });
    a2dp_sink.set_on_connection_state_changed(btConnectionStateChanged, nullptr);
    // Disable auto-reconnect — heap fragmentation during reconnect can stall
    // the audio task long enough to cause a burst of crackle.
    a2dp_sink.set_auto_reconnect(false);
    // Unique BT name per unit using last 3 bytes of MAC address.
    // Static so the pointer remains valid for the lifetime of the A2DP sink.
    static char btName[20];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(btName, sizeof(btName), "BANKRAKAKA-%02X%02X%02X", mac[3], mac[4], mac[5]);
    a2dp_sink.start(btName);

    if (DEBUGMODE)
        Serial.printf("[BT] Heap after A2DP start: %d\n", ESP.getFreeHeap());

    // A phone that still has us paired while this count is 0 will fail
    // authentication and has to "forget" the device before it can re-pair.
    Serial.printf("[BT] A2DP sink started as '%s', %d bonded device(s)\n",
                  btName, esp_bt_gap_get_bond_device_num());
}

#else

bool btRunning() {
    return false;
}

#endif // BLUETOOTH_MODE
