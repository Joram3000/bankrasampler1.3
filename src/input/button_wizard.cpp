#include "button_wizard.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include "config/config.h"
#include "input/button.h"
#include "input/mux.h"
#include "storage/pin_config_storage.h"
#include "ui.h"

// Number of raw low reads needed before we accept a press (simple debounce).
static constexpr int WIZARD_DEBOUNCE_COUNT = 5;
// Milliseconds to wait after a channel is accepted before listening again,
// to avoid a single long press registering as two different channels.
static constexpr uint32_t WIZARD_RELEASE_WAIT_MS = 400;

// Draw a single-line prompt on the display.
static void wizardDraw(const char* line1, const char* line2 = nullptr) {
    U8G2* u8g2 = getU8g2Display();
    if (!u8g2) return;

    auto* mx = static_cast<SemaphoreHandle_t*>(getDisplayMutex());
    if (mx && xSemaphoreTake(*mx, pdMS_TO_TICKS(50)) != pdTRUE) return;

    u8g2->clearBuffer();
    u8g2->setFont(u8g2_font_6x10_tr);
    u8g2->drawStr(0, 12, "-- WIZARD --");
    if (line1) u8g2->drawStr(0, 28, line1);
    if (line2) u8g2->drawStr(0, 44, line2);
    u8g2->sendBuffer();

    if (mx) xSemaphoreGive(*mx);
}


// Wacht op een LOW->HIGH flank op een vrij kanaal.
// Kanalen die al actief zijn bij aanroep worden genegeerd (bijv. schakelaars die al aan staan).
// usedChannels: lijst van al toegewezen kanalen die overgeslagen worden.
static uint8_t waitForNewPress(const uint8_t* usedChannels, uint8_t usedCount) {
    // Snapshot: welke kanalen zijn al actief? Die tellen niet mee als "nieuwe druk".
    bool baseline[8];
    for (uint8_t ch = 0; ch < 8; ++ch)
        baseline[ch] = readMuxActiveState(ch);

    while (true) {
        delay(20);
        for (uint8_t ch = 0; ch < 8; ++ch) {
            // Skip al toegewezen kanalen
            bool skip = false;
            for (uint8_t i = 0; i < usedCount; ++i) {
                if (usedChannels[i] == ch) { skip = true; break; }
            }
            if (skip) continue;
            // Skip kanalen die al actief waren bij aanvang (schakelaar staat al aan)
            if (baseline[ch]) continue;

            if (readMuxActiveState(ch)) {
                // Debounce
                int cnt = 1;
                while (cnt < WIZARD_DEBOUNCE_COUNT) {
                    delay(10);
                    cnt = readMuxActiveState(ch) ? cnt + 1 : 0;
                }
                return ch;
            }
        }
    }
}

void runButtonWizard() {
    Serial.println(F("[WIZARD] Starting button assignment wizard"));
    setScopeDisplaySuspended(true);

    // Bijhouden welke kanalen al zijn toegewezen (knoppen + schakelaars).
    uint8_t used[8];
    uint8_t usedCount = 0;

    // --- Assign sample buttons 1-6 ---
    for (int i = 0; i < (int)BUTTON_COUNT; ++i) {
        char line1[32];
        snprintf(line1, sizeof(line1), "Druk knop %d/%d", i + 1, (int)BUTTON_COUNT);
        wizardDraw(line1, "Raak aan...");
        Serial.printf("[WIZARD] Waiting for button %d\n", i + 1);

        uint8_t found = waitForNewPress(used, usedCount);

        runtimeButtonChannels[i] = found;
        setButtonChannel(i, found);
        used[usedCount++] = found;
        Serial.printf("[WIZARD] Button %d -> mux ch %u\n", i + 1, found);

        // Wacht tot losgelaten
        while (readMuxActiveState(found)) delay(10);
        delay(WIZARD_RELEASE_WAIT_MS);
    }

    // --- Assign delay send switch ---
    // Schakelaars kunnen al in elke stand staan — wacht op een verandering (elke flank).
    wizardDraw("Delay schakelaar", "Zet om...");
    Serial.println(F("[WIZARD] Waiting for delay switch"));
    {
        // Snapshot huidige stand van vrije kanalen.
        bool baseline[8];
        for (uint8_t ch = 0; ch < 8; ++ch)
            baseline[ch] = readMuxActiveState(ch);

        uint8_t found = 0xFF;
        while (found == 0xFF) {
            delay(20);
            for (uint8_t ch = 0; ch < 8; ++ch) {
                bool skip = false;
                for (uint8_t i = 0; i < usedCount; ++i)
                    if (used[i] == ch) { skip = true; break; }
                if (skip) continue;
                // Elke verandering t.o.v. baseline telt
                if (readMuxActiveState(ch) != baseline[ch]) {
                    int cnt = 1;
                    while (cnt < WIZARD_DEBOUNCE_COUNT) {
                        delay(10);
                        // Blijft veranderd t.o.v. baseline = stabiel
                        cnt = (readMuxActiveState(ch) != baseline[ch]) ? cnt + 1 : 0;
                    }
                    found = ch;
                    break;
                }
            }
        }
        runtimeSwitchDelayChannel = found;
        used[usedCount++] = found;
        Serial.printf("[WIZARD] Delay switch -> mux ch %u\n", found);
        delay(WIZARD_RELEASE_WAIT_MS);
    }

    // --- Assign filter switch ---
    wizardDraw("Filter schakelaar", "Zet om...");
    Serial.println(F("[WIZARD] Waiting for filter switch"));
    {
        bool baseline[8];
        for (uint8_t ch = 0; ch < 8; ++ch)
            baseline[ch] = readMuxActiveState(ch);

        uint8_t found = 0xFF;
        while (found == 0xFF) {
            delay(20);
            for (uint8_t ch = 0; ch < 8; ++ch) {
                bool skip = false;
                for (uint8_t i = 0; i < usedCount; ++i)
                    if (used[i] == ch) { skip = true; break; }
                if (skip) continue;
                if (readMuxActiveState(ch) != baseline[ch]) {
                    int cnt = 1;
                    while (cnt < WIZARD_DEBOUNCE_COUNT) {
                        delay(10);
                        cnt = (readMuxActiveState(ch) != baseline[ch]) ? cnt + 1 : 0;
                    }
                    found = ch;
                    break;
                }
            }
        }
        runtimeSwitchFilterChannel = found;
        used[usedCount++] = found;
        Serial.printf("[WIZARD] Filter switch -> mux ch %u\n", found);
        delay(WIZARD_RELEASE_WAIT_MS);
    }

    wizardDraw("Opgeslagen!", "Herstart...");
    Serial.println(F("[WIZARD] Done — saving pin config"));
    savePinConfigToSd();
    delay(1500);

    setScopeDisplaySuspended(false);
}
