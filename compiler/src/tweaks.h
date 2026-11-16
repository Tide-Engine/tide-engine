#pragma once

#include "ast.h"

// The game's tweaks (`tweak float KickUp = 0.00012;`) and tweaks.json, which gives them other values.

// Reads `folder`'s tweaks.json, when there's one: each value it gives a tweak becomes that tweak's value
// (decl.code_value keeps the code's). False when the file can't be read as one.
bool tweaks_read(program *prog, const char *folder);

// A dev build's: TideTweakValues, the tweaks' values; TideTweaks, the match's, which code that runs reads; its copy in
// this machine's local state, TideTweaksHere; and TideSetTweaks, which changes them. Sets prog->tweaks_live when the
// game has tweaks.
bool tweaks_prelude(program *prog);
