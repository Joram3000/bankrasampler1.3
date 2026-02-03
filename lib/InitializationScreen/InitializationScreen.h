#pragma once

#include <Arduino.h>
#include <functional>

// Interface implemented by concrete settings screen backends. Provides
// a common surface so the rest of the firmware can remain agnostic
// of the display library that renders the UI.
class IInitializationScreen {
public:
	// Logical button roles shared by the physical button mapper.

	virtual ~IInitializationScreen() = default;

	virtual void begin() = 0;
	virtual void enter() = 0;
	virtual void exit() = 0;
	virtual bool isActive() const = 0;
	virtual void update() = 0;
	
};



// in eerste instantie laat het een timer lopen totdat we onder aan de setup zijn
// 