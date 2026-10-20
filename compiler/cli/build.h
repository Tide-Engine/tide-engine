#pragma once

#include <stdbool.h>

typedef struct build_options {
    const char *folder; // The game: every .tide file in it and its subfolders
    bool release;       // Optimized, no console window on Windows
    bool web;           // One self-contained .html, with clang's WebAssembly target
    bool android;       // An Android app (.apk), for arm64 phones and the x86-64 emulator
    bool server;        // Headless native process hosting independent matches.
    bool managed;       // Internal deploy target: single-match sandboxed WASM reactor.
    bool server_strip_assets; // Explicit promise that custom C has no hidden asset dependencies.
    bool watch;         // Interactive run: rebuild as the game's files change
    const char *output; // Where the program goes; NULL for <folder>/build/<name>
    const char *title;  // The window's title, over the game's title setting; NULL for that, or else the folder's name
    bool stats;         // Show the stats, whether or not the game's stats setting does: a drawer of one line, in a corner
    bool stats_open;    // ...which starts open: tide run's, for whoever typed --stats to read them
    bool debug_views;   // The debug views are on, whatever the game's debugViews setting says: F7 shows the profiler
    // The symbols every file's #if conditions read as defined: --define's, and DEBUG without --release
    const char *const *defines; // Kept by the caller for the build and every watched rebuild
    int define_count;
    const char *editor_prefab, *editor_source, *editor_draft;
    bool editor_local;
    bool unity; // For Unity's Tide package (tide unity): the game built for another program to run (tidec --embed)
    const char *unity_assets; // ...and the Unity project's Assets folder, whose prefabs and scenes it has (unity_assets.h)
} build_options;

// Builds a game with the engine files installed in `root` (see AGENTS.md,
// Packaging). Returns the program's path (malloc'd), or NULL after reporting
// what went wrong.
char *tide_build(const char *root, const build_options *opts);
// Internal authoring module build; output is a private directory owned by the editor.
char *tide_build_editor(const char *root, const build_options *opts);

// Builds a standalone program and runs it once (a run with no window). `args` go to the
// game (--host, --join, --connect, --size, and a run with no window's), ending
// with NULL. Returns the game's exit code.
int tide_run_native(const char *root, const build_options *opts, const char *const *args);

// `tide unity`: builds the game for Unity's Tide package into opts->output: the
// host Unity loads (tide_embed, tide/embed.h), then the game's libraries
// (game-<n>, with its pack-<n>), numbered on from what's there, a new one each
// time the game's files change while opts->watch, which the running game swaps
// in. A watching one ends when its input does. Returns its exit code.
int tide_unity(const char *root, const build_options *opts);

// Interactive run: the game is a library in a small host program, rebuilt whenever
// one of its files changes and swapped into the running game (see tide/host.h).
// `args` go to the game, as for tide_run_native. Returns the game's exit code.
int tide_run_reloading(const char *root, const build_options *opts, const char *const *args);

// The same on the web (`tide run --web`): tide serves the game's page on this
// machine and opens it in a browser if `open_page`. With opts->watch, builds
// the game again whenever one of its files changes, which the page starts in the
// running build's place (see platform/web/tide.js). Otherwise serves one
// standalone build. `args` go to the game. Runs until it's stopped.
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

// Prints the schedule of the game in `opts->folder` (see tidec --schedule), or with
// `shaders`, the shaders its mesh draw calls draw through (tidec --shaders),
// as a build with these options has them.
bool tide_schedule(const build_options *opts, bool shaders);
