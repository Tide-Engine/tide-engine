#pragma once

// tide-crash (crash.c), and the window it shows: ui_win32.c, ui_x11.c or
// ui_mac.c, one a system.

#include <stdbool.h>
#include <stdio.h>

typedef struct crash_view {
    const char *title;   // The window's: the game's
    const char *heading; // "<game> stopped unexpectedly."
    const char *preview; // What the report says, for the player to read: the crash, and the log's end
    const char *show;    // The first button's label: it shows the report's zip
} crash_view;

// The line under the heading, which the window asks for as it shows (every
// tenth of a second or so) while a report is sent: NULL keeps the last.
// *settled turns true once it won't change again.
typedef const char *(*crash_status)(void *user, bool *settled);

// Shows `view` until the player closes it: 1 for its first button, 0 for
// Close (or the window closed), -1 where no window can show (Linux with no X
// display). A Mac's window waits for the status to settle first.
int crash_show(const crash_view *view, crash_status status, void *user);

// fopen, for a UTF-8 path on Windows too.
FILE *crash_open(const char *path, const char *mode);

int crash_main(int argc, char **argv);
