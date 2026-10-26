#pragma once

#include <stdbool.h>

#include "unity_mapping.h"

// The game's C# for Unity's Tide package, which tide unity writes into the project's Packages/com.tide.game beside the
// schema, so it never waits on Unity compiling (a project whose scripts use the game's types compiles once they're
// there): a TideComponent for each of the game's match components (Components/<namespaces>/<Name>.cs, a file each, as
// Unity finds a component's script by its file's name, its .meta's GUID made from its Tide name so scenes and prefabs
// keep it), the other types as structs laid out as the game's C lays them out (TideGame.cs), and components.json,
// which the converter (unity_assets.c) reads the components' serialized fields by. A component's fields are what's
// authored outside Play and its entity's values in Play (com.tide.unity's TideComponent).

// Writes them into `dir` from the build's schema JSON and the declarations tidec gave it (print_outline's JSON, for
// text fields' defaults; NULL for none), each file only where it changed, and takes away component scripts the game
// no longer has. `packages` are the game's: what their unity.json says of their components (unity_mapping.h) is in
// their C# (fields hidden, components kept from Add Component, shapes the Scene view draws, fields by the scale).
// False after saying what's wrong.
bool unity_code_write(const char *dir, const char *schema, const char *outline, const unity_package *packages, int package_count);

// The same, but taking no component script away: from a build of the game's declarations alone (tide unity
// --declarations), which leaves out components only its code gives entities (Owner), whose scripts then stay.
bool unity_code_add(const char *dir, const char *schema, const char *outline, const unity_package *packages, int package_count);

// What became of the C# names of the game's types the last C# had (`was`, the schema it was written from) in the C#
// of `schema` (both as unity_code_write takes them), into the record at `record` (Library/Tide/renamed.json, local to
// the machine, never in source control), which com.tide.unity reads to explain a script's C# error on one of them: a
// type, field or enum member renamed (one gone and one come of the same kind and fields, or the only ones of their
// kind; a field or member at the same place with the same type), with where the new one is declared, or removed
// (`removals`: false for a build of the declarations alone, which leaves some out). Names are C#'s after Tide.Game.
// ("Combat.Health.hp"). An entry goes when its old name is back, and a rename of a renamed name follows it on; the
// newest 100 are kept. False after saying what's wrong.
bool unity_code_record(const char *record, const char *was, const char *schema, const char *outline, const char *dir,
                       const unity_package *packages, int package_count, bool removals);

// A component's script GUID: from its Tide name, the same on every machine and build ("tide component <name>"'s
// SHA-256, its first 16 bytes as hex). `out` gets 32 digits and a NUL.
void unity_code_guid(const char *name, char out[33]);
