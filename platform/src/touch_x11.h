#pragma once

// Touchscreens on Linux (touch_x11.c), where GLFW has no touch: X11's touch
// events (XInput 2.2), which XWayland has too, as on a Steam Deck. It's apart
// from desktop/glfw.c for X11's headers, whose names collide with the game's.

#if defined(__linux__) && !defined(__ANDROID__)

#include <stdbool.h>

#include "window.h"

// Takes the touches of `window` (an X11 Window, GLFW's): they're no longer
// the mouse's. Does nothing where the system has no X11 or no XInput 2.2.
void tide_x11_touch_attach(unsigned long window);
// ...and lets them go, as the window closes.
void tide_x11_touch_detach(void);
// Whether this machine has a touchscreen.
bool tide_x11_touchscreen(void);
// The next touch event since the last call, false when there's none: where,
// from 0 to 1 across the window's inside, from the top left, for a window
// `across` by `down` of X11's pixels (GLFW's window size on X11).
bool tide_x11_take_touch(tide_touch_report *report, int across, int down);

#endif
