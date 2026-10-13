#pragma once

namespace platform {

// Registers the HOME-button exit callback. Call once at startup.
void init();

// False once the user chose "Exit" from the HOME menu.
bool running();

// Returns to the XMB. Never returns.
[[noreturn]] void shutdown();

// Seconds since an arbitrary epoch, microsecond resolution.
double now();

// CPU/bus to 333/166 MHz (firmware default is 222/111). Returns false on failure.
bool setMaxClock();

}  // namespace platform
