#pragma once

#include <stdbool.h>

// Debug.Log and Debug.Assert, as generated code calls them.
//
// Output goes to stderr: the terminal under `tide run`, the browser's console
// on the web (through WASI's fd_write) and the system's log on Android. A tick
// the client runs again after a rollback logs again, so a log of match state
// is best prefixed with Time.tick. Neither writes to the world, so the match
// stays the same on every machine with or without them.

// Prints `text` and a newline.
void tide_debug_log(const char *text);

// Nothing when `ok`. Otherwise ends the program, naming the Tide file and
// line, the source line (`source`), and `message` when it isn't empty.
void tide_debug_assert(bool ok, const char *source, const char *file, int line, const char *message);
