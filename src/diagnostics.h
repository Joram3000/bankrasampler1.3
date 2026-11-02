// diagnostics.h — heap/stack report on request over the serial port
//
// Send 'd' in the serial monitor for: free heap after each boot stage, free
// heap now and its lowest point, the largest free block, every task's unused
// stack and why the chip last reset. The lowest values are minima since boot,
// so one report after a session shows that session's worst case.
#pragma once

// First thing in setup(): records the reset reason and whether a coredump of
// an earlier crash is stored in flash.
void initDiagnostics();

// Records the free heap after a boot stage. setup() only; pass a literal.
void diagMark(const char *stage);

// Loop task: prints the report when 'd' arrives on the serial port.
void pollDiagnostics();
