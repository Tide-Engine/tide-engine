#pragma once

#include <stdbool.h>
#include <stdint.h>

// Anonymous usage data (docs/tools/telemetry.md): one small event per command,
// queued in <root>/telemetry/ and sent by a detached `tide _telemetry-send`, so
// no command waits on the network. Nothing in an event names a path, a project,
// a file, code, an error's text, a user or a machine.

// The kind of failure a command ended with, one of a fixed set. The last one
// noted wins; a command that fails without noting one is "usage" when it
// exited with 2 and "other" otherwise.
typedef enum telemetry_error {
    TELEMETRY_ERROR_NONE,
    TELEMETRY_ERROR_SETUP,    // No compiler, SDK or engine files; an installation that isn't whole
    TELEMETRY_ERROR_PACKAGES, // tide.packages, or a package that couldn't be found or fetched
    TELEMETRY_ERROR_COMPILE,  // The game's Tide code had errors
    TELEMETRY_ERROR_C,        // A C or C++ file didn't compile
    TELEMETRY_ERROR_LINK,     // The program didn't link
    TELEMETRY_ERROR_NETWORK,  // A download failed
    TELEMETRY_ERROR_GAME,     // The game a run started exited with an error
} telemetry_error;

void telemetry_fail(telemetry_error error);
// How many .tide files the game has of its own and how many packages it lists,
// once a command has read them. Sent only in ranges.
void telemetry_sizes(int files, int packages);

// After a command ended with exit code `code` in `ms` milliseconds: queues its
// event and starts the sender, or shows the one-time notice, when usage data
// is on (see telemetry_off). Never waits on the network.
void tide_telemetry_record(const char *root, int argc, const char *const *argv, int code, int64_t ms, const char *version);
// `tide _telemetry-send`, detached: sends what's queued, once, with a short
// timeout. Failures are silent; what couldn't be sent stays queued.
int tide_telemetry_send(const char *root, const char *version);
// `tide telemetry on|off|status`.
int tide_telemetry(const char *root, int count, const char *const *args, const char *version);

// Why nothing is sent, or NULL when events are: turned off, TIDE_TELEMETRY=0,
// DO_NOT_TRACK, a development build or TIDE_HIDDEN.
const char *telemetry_off(const char *root, const char *version);
