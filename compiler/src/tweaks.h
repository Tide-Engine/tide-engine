#pragma once

#include <stdint.h>

#include "ast.h"

// The game's tweaks (`tweak float KickUp = 0.00012;`) and the .tweaks files that give them other values.

// Reads the .tweaks files in `folders`, and with `deep` their subfolders (folder_find_own's), or the working folder
// with none, when the game has tweaks: each value one gives a tweak becomes
// that tweak's value (decl.code_value keeps the code's; tweaked_file, _at and _text say where). A tweak set in two
// files is an error, and a name no tweak has a warning. The files read go in prog->tweak_files. False when one
// can't be read as `Name = value;` entries.
bool tweaks_read(program *prog, const char *const *folders, int count, bool deep);

// A dev build's: TideTweakValues, the tweaks' values; TideTweaks, the match's, which code that runs reads; its copy in
// this machine's local state, TideTweaksHere; and TideSetTweaks, which changes them. Sets prog->tweaks_live when the
// game has tweaks.
bool tweaks_prelude(program *prog);

// `path` from the root, with forward slashes
const char *tweaks_absolute(const char *path);

// A tweak's qualified name, its parts joined by `dot`, and its type as code in the global namespace names it
void tweak_name_text(sb *o, const decl *d, char dot);
void tweak_type_text(sb *o, const decl *d);

// The .tweaks file a tweak's value is written to: the one that sets it, or the one beside the code that declares it,
// of its name (combat.tide's combat.tweaks), absolute
const char *tweak_file(const decl *d);

// A checked tweak's value as its C type's `size` bytes, worked out as the generated C would; false where it uses what
// this can't work out (a function other than Math's, a constant build code makes)
bool tweak_value(const decl *k, uint8_t *out, size_t size);

// Whether a tweak's value is written as a plain value (a number, true, a vector of numbers, an enum's member), which
// the dev menu can write over; otherwise it's worked out, and the menu leaves it to the file
bool tweak_value_plain(const decl *k);
