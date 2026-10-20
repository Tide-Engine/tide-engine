#pragma once

// The platform layer inside another program's frame (tide/embed.h, embed.c):
// what that program's window read stands in for a window of Tide's own, as a
// headless run's script does.

#include "tide/embed.h"

// The frame's input and screen, before tide_platform_poll: until the next
// call, the devices and the screen are these. NULL `input` is a frame with
// nothing held.
void tide_platform_embed_frame(const tide_embed_input *input, tide_float2 screen_size, float screen_scale);

// What the game last asked of the window, which the other program does: the
// GUI is typing into a field, and the cursor is to be locked.
bool tide_platform_embed_typing(void);
bool tide_platform_embed_cursor_lock(void);

// Another relay than TIDE_RELAY's or the default, or NULL for those again (rooms.c).
void tide_platform_set_relay(const char *url);
