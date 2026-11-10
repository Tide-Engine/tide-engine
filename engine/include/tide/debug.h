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

// Where Debug.Log's lines go as well as stderr, each as it's said, on whichever thread says it: for a program that
// shows them itself (tide/embed.h hands them on, for Unity's Console). NULL, as at the start, for nowhere else. A
// game's library has its own copy of this, which the host sets through tide_host_game.debug_log_to.
void tide_debug_log_to(void (*heard)(const char *text));

// Nothing when `ok`. Otherwise ends the program, naming the Tide file and
// line, the source line (`source`), and `message` when it isn't empty.
void tide_debug_assert(bool ok, const char *source, const char *file, int line, const char *message);
