// transfer_mode.h — switching to the WiFi transfer app
//
// Managing the SD card over WiFi is a separate firmware in the "transfer"
// app partition (src/transfer_app/), not part of this one: linking the WiFi
// stack costs ~20 KB of static RAM even when WiFi is never started, which
// the BT stack can't spare. The transfer app hands control back by itself.
#pragma once

// True when the transfer app has been flashed (pio run -e transfer -t upload).
bool transferAppInstalled();

// Boots the transfer app next and reboots. The caller saves pending settings
// first. Returns false, without rebooting, when the app is missing.
bool rebootIntoTransferApp();
