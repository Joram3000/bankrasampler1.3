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
