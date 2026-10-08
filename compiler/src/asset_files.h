#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "mesh_files.h"
#include "tide/png.h"

// The files an `asset` declaration resolves to (docs/spec.md, Assets): found
// from the declaring file, read, hashed and, for a Grid2<Color>, decoded, so
// the checker can say what's wrong at the declaration and codegen can pack
// them. Reads are kept by path, size and time, so a language server that
// checks a game at every keystroke reads and decodes each file once.

typedef enum asset_kind {
    ASSET_TEXT,  // string: UTF-8 text
    ASSET_BYTES, // List<byte>: any file
    ASSET_GRID,  // Grid2<Color>: a PNG or PSD composite, decoded
    ASSET_TEXTURE, // Texture: a PNG or PSD composite, decoded, kept a byte a channel to draw with
    // Cubemap: six images a glob matches, each named for its face by how its name ends before the extension (px,
    // nx, py, ny, pz, nz), decoded: its items are the six in that order
    ASSET_CUBEMAP,
    ASSET_CLIP,    // Clip: a WAV, checked here and kept as it is; the engine decodes it as it's first played
    // Mesh<V>: a glTF (.glb, .gltf) or an OBJ, read here as one mesh (mesh_files.h), which codegen lays out as
    // the declaration's vertex struct
    ASSET_MESH,
} asset_kind;

typedef struct asset_match {
    const char *key;      // The path as written from the declaring file, forward slashes: a map's key. A URL as it is.
    const char *abs;      // Where the bytes were read from
    const uint8_t *data;  // The file's bytes (kept until the process ends)
    size_t size;
    uint8_t hash[32];     // SHA-256 of the bytes
    tide_png_image image; // ASSET_GRID and ASSET_TEXTURE: its pixels
    mesh_file mesh;       // ASSET_MESH: its vertices and triangles, which stay until the file changes, as its bytes do
    const char **sides;   // ...and the files beside a .gltf that it was read with, its buffers': absolute paths
    int side_count;
} asset_match;

typedef struct asset_value {
    asset_kind kind;
    bool many;   // A List<T> or Map<string, T>: a glob's matches, or the one file a path or a URL names
    bool is_map; // ...a Map<string, T>
    asset_match *items;
    int count;
} asset_value;

// Resolves the path written in a declaration: `path` as the source has it
// (escapes and all), in the file `declaring` (an absolute path). Errors go to
// the diagnostics at `at`, saying what to write, and give NULL. `game_folder`
// is where the game's .tide/ is, or NULL where nothing says (tidec on its
// own): it's where the hashes of named files are kept, so a moved file is
// found by its bytes, and where fetched URLs are cached.
asset_value *asset_resolve(const char *declaring, str path, asset_kind kind, bool many, bool is_map, loc at, const char *game_folder);

// The folders and files a resolved value depends on, for whoever watches
// them: each file's path, and a glob's folder (ending in '/'). Calls `found`
// with each, once. What a file was read with beside it (a .gltf's buffers) is
// its value's to say: asset_match.sides.
typedef void (*asset_path_fn)(void *user, const char *path);
void asset_watched(const char *declaring, str path, asset_path_fn found, void *user);

// Whether `path` (forward slashes) matches the glob `pattern`: `*` within a
// name, `**` across folders, `?` a character; Windows paths ignore case.
bool asset_glob_match(const char *pattern, const char *path);

// An asset as checked: its declaring file, its path as written, and what it
// resolved to (NULL where it couldn't).
typedef struct asset_named {
    const char *declaring;
    str path;
    const asset_value *value;
} asset_named;

// Keeps the hashes of the named files in the game's .tide/ (assets.h), once a
// game is checked: each file resolved, by its path from the game's folder, and
// what was known of a file that's missing now, so it's found by its bytes
// later. Written only where it changed; nothing without a `game_folder`.
void asset_files_save_hashes(const char *game_folder, const asset_named *named, int count);

// Lets go of every file read so far: tests, or a process that goes on without them.
void asset_files_forget(void);

// A file or folder explicitly changed: read its cached files and dependent
// meshes again even if their size/time is unchanged. Existing values stay
// valid until the next resolution, as they do when metadata changes.
void asset_files_changed(const char *path);
