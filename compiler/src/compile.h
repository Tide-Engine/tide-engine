#pragma once

#include "codegen.h"

// One file of a game.
typedef struct compile_input {
    const char *path;
    const char *package; // The package it's in, by name; NULL for the game's own files
    const char *text;    // Its text, when it isn't read from `path` (a Unity prefab, as `tide unity` makes it)
} compile_input;

// The whole transpiler, as tidec and the tide command run it: reads the files,
// checks the program and writes the C (see codegen). The files compile in the
// order given, which decides the default order of systems: each package's
// files, then the game's, each by path (see docs/spec.md, Packages). With
// `schedule`, it prints the tick's schedule there instead of writing code.
// Errors go to stderr; returns false after reporting them.
bool compile_inputs(const compile_input *inputs, int count, const codegen_options *opts, sb *schedule);

// "dir/game.tide" -> "game", in the arena.
const char *path_stem(const char *path);
