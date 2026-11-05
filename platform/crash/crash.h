#pragma once

// tide-crash (crash.c), and the window it asks in: ui_win32.c, ui_x11.c or
// ui_mac.c, one a system.

#include <stdbool.h>
#include <stdio.h>

// Asks the player, in a window titled `title` saying `text`, whether to show
// the report: 1 for Show report, 0 for Close (or the window closed), -1
// where no window can show (Linux with no X display).
int crash_ask(const char *title, const char *text);

// fopen, for a UTF-8 path on Windows too.
FILE *crash_open(const char *path, const char *mode);

int crash_main(int argc, char **argv);
