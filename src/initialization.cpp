#include "initialization.h"

#include "InitializationScreenU8g2.h"

namespace {
constexpr const char *kStatusLabels[] = {
		"Display",
		"SD kaart",
		"Audio",
		"Player",
		"Instelscherm",
		"Instelschak.",
};

InitializationScreenU8g2 *g_screen = nullptr;
size_t g_statusIndex = 0;
}

void setInitializationScreen(InitializationScreenU8g2 *screen) {
	g_screen = screen;
	g_statusIndex = 0;
	if (!g_screen) return;

	for (size_t i = 0; i < InitializationScreenU8g2::kMaxStatusEntries; ++i) {
		const char *label = (i < (sizeof(kStatusLabels) / sizeof(kStatusLabels[0]))) ?
													 kStatusLabels[i]
												 :
													 "";
		g_screen->setStatus(i, label, false);
	}
}

void initializationStepper(const char *stepMessage) {
	if (!g_screen) return;

	g_screen->setMessage(stepMessage);
	if (g_statusIndex < InitializationScreenU8g2::kMaxStatusEntries) {
		const char *label =
				(g_statusIndex < (sizeof(kStatusLabels) / sizeof(kStatusLabels[0]))) ?
						kStatusLabels[g_statusIndex]
					:
						stepMessage;
		g_screen->setStatus(g_statusIndex, label, true);
		++g_statusIndex;
	}
	g_screen->update();
}



//     // Show a short initialization screen animation (1s) instead of a blind delay.
// #if DISPLAY_DRIVER == DISPLAY_DRIVER_U8G2_SSD1306
//     if (auto* display = getU8g2Display()) {
//       if (!initializationScreen) {
//         initializationScreen = new InitializationScreenU8g2(*display, INIT_SCREEN_MESSAGE, INIT_SCREEN_DURATION_MS);
//         initializationScreen->begin();
//       }
//       // ensure the screen shows for the configured duration
//       initializationScreen->enter();
//       initializationScreen->update();
//     }
// #else
//     delay(1000);
// #endif
//   Serial.print("Init done, switching to Performance mode");
//     setOperatingMode(OperatingMode::Performance);