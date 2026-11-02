// config.cpp — runtime state declared in config.h
#include "config/config.h"

// --- Runtime pin configuration (defaults come from pins.h) ------------------
std::array<uint8_t, BUTTON_COUNT> runtimeButtonChannels = BUTTON_CHANNEL_ON_MUX;
uint8_t runtimeSwitchDelayChannel  = SWITCH_CHANNEL_DELAY_SEND;
uint8_t runtimeSwitchFilterChannel = SWITCH_CHANNEL_FILTER_ENABLE;
bool    runtimeMuxActiveLow        = MUX_ACTIVE_LOW_DEFAULT;
bool    runtimePotInverted         = false;

// Runtime debug toggle — default off, toggled from settings screen / settings.txt.
bool debugEnabled = false;
