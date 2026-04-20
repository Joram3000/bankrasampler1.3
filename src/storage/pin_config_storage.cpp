#include "pin_config_storage.h"

#include <Arduino.h>
#include <SD.h>
#include "config/config.h"
#include "input/button.h"

namespace {
constexpr const char* kPinConfigPath = "/pin_config.txt";
}

bool pinConfigExistsOnSd() {
    return SD.exists(kPinConfigPath);
}

bool loadPinConfigFromSd() {
    if (!SD.exists(kPinConfigPath)) return false;

    File f = SD.open(kPinConfigPath, FILE_READ);
    if (!f) {
        Serial.println(F("[PIN] Failed to open pin_config.txt"));
        return false;
    }

    bool gotButtons  = false;
    bool gotDelaySw  = false;
    bool gotFilterSw = false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;

        if (line.startsWith("btn=")) {
            // Format: btn=1,0,4,5,6,7
            String vals = line.substring(4);
            int idx = 0;
            int start = 0;
            while (idx < (int)BUTTON_COUNT) {
                int comma = vals.indexOf(',', start);
                String tok = (comma < 0) ? vals.substring(start) : vals.substring(start, comma);
                tok.trim();
                if (tok.length() > 0) {
                    uint8_t ch = (uint8_t)tok.toInt();
                    runtimeButtonChannels[idx] = ch;
                    setButtonChannel(idx, ch);
                    ++idx;
                }
                if (comma < 0) break;
                start = comma + 1;
            }
            gotButtons = (idx == (int)BUTTON_COUNT);
        } else if (line.startsWith("delay_sw=")) {
            runtimeSwitchDelayChannel = (uint8_t)line.substring(9).toInt();
            gotDelaySw = true;
        } else if (line.startsWith("filter_sw=")) {
            runtimeSwitchFilterChannel = (uint8_t)line.substring(10).toInt();
            gotFilterSw = true;
        } else if (line.startsWith("mux_active_low=")) {
            runtimeMuxActiveLow = line.substring(15).toInt() != 0;
        } else if (line.startsWith("pot_inverted=")) {
            runtimePotInverted = line.substring(13).toInt() != 0;
        }
    }
    f.close();

    if (gotButtons && gotDelaySw && gotFilterSw) {
        Serial.printf("[PIN] Loaded: btn=%u,%u,%u,%u,%u,%u  delay_sw=%u  filter_sw=%u  active_low=%d\n",
            runtimeButtonChannels[0], runtimeButtonChannels[1], runtimeButtonChannels[2],
            runtimeButtonChannels[3], runtimeButtonChannels[4], runtimeButtonChannels[5],
            runtimeSwitchDelayChannel, runtimeSwitchFilterChannel, runtimeMuxActiveLow);
        return true;
    }

    Serial.println(F("[PIN] pin_config.txt incomplete — will run wizard"));
    return false;
}

void savePinConfigToSd() {
    if (SD.exists(kPinConfigPath)) SD.remove(kPinConfigPath);

    File f = SD.open(kPinConfigPath, FILE_WRITE);
    if (!f) {
        Serial.println(F("[PIN] Failed to open pin_config.txt for write"));
        return;
    }

    f.printf("# Button mux channels for samples 1-6\n");
    f.printf("btn=%u,%u,%u,%u,%u,%u\n",
        runtimeButtonChannels[0], runtimeButtonChannels[1], runtimeButtonChannels[2],
        runtimeButtonChannels[3], runtimeButtonChannels[4], runtimeButtonChannels[5]);
    f.printf("delay_sw=%u\n",  runtimeSwitchDelayChannel);
    f.printf("filter_sw=%u\n", runtimeSwitchFilterChannel);
    f.printf("mux_active_low=%d\n", runtimeMuxActiveLow ? 1 : 0);
    f.printf("pot_inverted=%d\n",   runtimePotInverted  ? 1 : 0);

    f.flush();
    f.close();
    Serial.println(F("[PIN] Saved pin_config.txt"));
}
