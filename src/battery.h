// battery.h — LiPo charge indicator in the HUD
#pragma once
#include <cstdint>

// Loop task. Reads the battery every 2 s and updates the HUD; hides the
// indicator when no battery is detected.
void checkBattery(uint32_t now);
