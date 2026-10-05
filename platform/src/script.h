#pragma once

// A headless window's devices (tide_platform_open_headless): what they say is
// made up, never the system's, so nothing on this machine's desk gets into a
// run. Events come one at a time (tide_platform_input) or from a script, a
// file of them by frame, which also says when the run takes pictures and when
// it ends (see script.c for what a line is).
//
// platform.c polls these as it polls a window's: the calls below say what
// window.h's say, so made-up input reaches the devices the way a player's does.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "window.h"

bool tide_script_key_held(tide_key key);
void tide_script_mouse_state(tide_window_mouse *out);
void tide_script_gamepad_state(tide_window_gamepad *out);
bool tide_script_touchscreen(void);
bool tide_script_take_touch(tide_touch_report *report);
uint32_t tide_script_take_char(void);
// The window holds the cursor whenever the game asks: nothing takes it away.
void tide_script_cursor_lock(bool lock);

// One event into the devices, as a script's line has it after its frame
// ("key d down"). False, with why in `error`, for what isn't input.
bool tide_script_event(const char *text, char *error, size_t error_size);

// Loads the script in the file at `path`. False, having said why: the file
// can't be read, or a line of it is wrong, which it names.
bool tide_script_load(const char *path);

// Whether the script loaded ends the run: it has a `quit` line.
bool tide_script_quits(void);

// Before frame `frame` (they count from 1): its lines' input, into the devices.
void tide_script_input(uint32_t frame);

// Once the frame drew: writes the pictures its lines ask for
// (tide_platform_capture). TIDE_KEEP_RUNNING, or the run's exit code: 0 at
// its `quit`, 1 when a picture couldn't be written.
int tide_script_drawn(uint32_t frame);
