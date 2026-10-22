#pragma once

// The Unity shaders a game draws with in Unity (com.tide.unity's
// TideRender.cs replays Tide's renderer there): the renderer's own and the
// game's frags and verts, each its WGSL as HLSL (compiler/src/hlsl.h) in a
// Unity shader. Each is named by its key, FNV-1a's 64 bits of its SPIR-V's
// words as hex, which is what tide_embed_render's pipelines say they draw
// with: <package>/Resources/TideShaders/<key>.shader, "Hidden/Tide/<key>",
// which Resources.Load finds in the editor and in players. Those of shaders
// gone are removed.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The key of a shader's WGSL: its SPIR-V's hash, or 0 where it has none.
uint64_t unity_shader_key(const char *wgsl);

// Writes the renderer's shaders and the game's into the package `dir`: `game` is the game's, each a name and its
// WGSL, each NUL-terminated, one after another, `size` bytes in all. Each file is written only where it changed.
// False after saying what's wrong.
bool unity_shaders_write(const char *dir, const char *game, size_t size);
