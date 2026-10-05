#pragma once

#include <stdbool.h>

typedef struct build_options {
    const char *folder; // The game: every .tide file in it and its subfolders
    bool release;       // Optimized, no console window on Windows
    bool web;           // One self-contained .html, with clang's WebAssembly target
    bool android;       // An Android app (.apk), for arm64 phones and the x86-64 emulator
    const char *output; // Where the program goes; NULL for <folder>/build/<name>
    const char *title;  // The window's title, over the game's title setting; NULL for that, or else the folder's name
    bool stats;         // Show the stats, whether or not the game's stats setting does: a drawer of one line, in a corner
    bool stats_open;    // ...which starts open: tide run's, for whoever typed --stats to read them
    bool debug_views;   // The debug views are on, whatever the game's debugViews setting says: F7 shows the profiler
} build_options;

// Builds a game with the engine files installed in `root` (see AGENTS.md,
// Packaging). Returns the program's path (malloc'd), or NULL after reporting
// what went wrong.
char *tide_build(const char *root, const build_options *opts);

// Runs a game on this machine, as `tide run` does natively: the game is a
// library in a small host program, rebuilt whenever one of its files changes and
// swapped into the running game (see tide/host.h). `args` go to the game
// (--host, --join, --connect, --size, and a run with no window's), ending
// with NULL. With `once`, as for a run with no window (--capture, --frames,
// --script), the game runs as it was built: nothing is watched, rebuilt or
// swapped in. Returns the game's exit code.
int tide_run_reloading(const char *root, const build_options *opts, const char *const *args, bool once);

// The same on the web (`tide run --web`): tide serves the game's page on this
// machine, opens it in a browser if `open_page`, and builds the game again
// whenever one of its files changes, which the page starts in the running build's
// place (see platform/web/tide.js). `args` go to the game, as for
// tide_run_reloading. Runs until it's stopped.
int tide_run_web(const char *root, const build_options *opts, bool open_page, const char *const *args);

// Runs a game on an Android phone or emulator (`tide run --android`): builds
// its app, installs it with adb, starts it with `args` and shows what it
// prints until it ends. Returns 0 if it ran.
int tide_run_android(const char *root, const build_options *opts, const char *const *args);

// The folder tide keeps its work for the game in `folder` in: <folder>/.tide,
// hidden like .git, and ignored by git. Made if it isn't there; malloc'd.
char *tide_work_folder(const char *folder);

// Every file a build reads from the game's folder: its tide.packages, its
// .tide files, its C and C++ with their headers and its libraries, and those
// of its packages in folders inside it (packages from git aren't its files:
// they're named by their commit). Not what tide made there (.tide/, build/).
// Paths from the folder, with forward slashes, in the same order on every
// machine; `count` gets how many. malloc'd, as each path is. NULL after
// saying what's wrong: its tide.packages, or a package in a folder outside
// the game's.
char **tide_game_sources(const char *folder, int *count);

// Prints the schedule of the game in `folder` (see tidec --schedule), or with
// `shaders`, the shaders its mesh draw calls draw through (tidec --shaders).
bool tide_schedule(const char *folder, bool shaders);
