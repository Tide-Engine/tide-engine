// The window (src/window.h) of a build that has none of its own: Tide inside another program's frame
// (tide/embed.h), whose window, input devices and drawing are that program's. An iOS game under Unity is built
// this way, as a console's would be. Nothing here is a system's: a frame's devices and screen come from the other
// program (src/embedded.h), and what the game draws is written down for it to replay (src/gpu_record.c).

#include "window.h"

#include <stdio.h>

bool tide_window_open(const tide_window_desc *desc)
{
    (void)desc;
    fprintf(stderr, "tide: this build has no window of its own: it runs inside another program's (tide/embed.h)\n");
    return false;
}

void tide_window_close(void)
{
}

const tide_gpu *tide_window_gpu(void)
{
    return NULL;
}

bool tide_window_should_close(void)
{
    return false;
}

bool tide_window_unseen(void)
{
    return true;
}

void tide_window_size(int *width, int *height)
{
    *width = 0;
    *height = 0;
}

void tide_window_pixel_size(int *width, int *height)
{
    *width = 0;
    *height = 0;
}

void tide_window_present(void)
{
}

void tide_window_wait(const double seconds)
{
    (void)seconds;
}

bool tide_window_key_held(const tide_key key)
{
    (void)key;
    return false;
}

void tide_window_mouse_state(tide_window_mouse *out)
{
    *out = (tide_window_mouse){0};
}

void tide_window_cursor_lock(const bool lock)
{
    (void)lock;
}

void tide_window_gamepad_state(tide_window_gamepad *out)
{
    *out = (tide_window_gamepad){0};
}

bool tide_window_touchscreen(void)
{
    return false;
}

bool tide_window_take_touch(tide_touch_report *report)
{
    (void)report;
    return false;
}

uint32_t tide_window_take_char(void)
{
    return 0;
}

const char *tide_window_take_paste(void)
{
    return NULL;
}

void tide_window_copy(const char *text)
{
    (void)text;
}

void tide_window_typing(const bool typing)
{
    (void)typing;
}

// The other program hands the game's pack over (tide_embed_desc.pack)
uint8_t *tide_window_pack(size_t *size, uint64_t progress[2])
{
    (void)progress;
    *size = 0;
    return NULL;
}

bool tide_window_pack_path(char path[4096])
{
    (void)path;
    return false;
}
