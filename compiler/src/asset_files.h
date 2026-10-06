#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "tide/png.h"

// The files an `asset` declaration resolves to (docs/spec.md, Assets): found
// from the declaring file, read, hashed and, for a Grid2<Color>, decoded, so
// the checker can say what's wrong at the declaration and codegen can pack
// them. Reads are kept by path, size and time, so a language server that
// checks a game at every keystroke reads and decodes each file once.

typedef enum asset_kind {
    ASSET_TEXT,  // string: UTF-8 text
    ASSET_BYTES, // List<byte>: any file
    ASSET_GRID,  // Grid2<Color>: a PNG, decoded
} asset_kind;

typedef struct asset_match {
    const char *key;      // The path as written from the declaring file, forward slashes: a map's key. A URL as it is.
    const char *abs;      // Where the bytes were read from
    const uint8_t *data;  // The file's bytes (kept until the process ends)
    size_t size;
    uint8_t hash[32];     // SHA-256 of the bytes
    tide_png_image image; // ASSET_GRID: its pixels
} asset_match;

typedef struct asset_value {
    asset_kind kind;
    bool many;   // A glob: a List<T> or Map<string, T> of its matches
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
// with each, once.
typedef void (*asset_path_fn)(void *user, const char *path);
void asset_watched(const char *declaring, str path, asset_path_fn found, void *user);

// Whether `path` (forward slashes) matches the glob `pattern`: `*` within a
// name, `**` across folders, `?` a character; Windows paths ignore case.
bool asset_glob_match(const char *pattern, const char *path);

// Lets go of every file read so far: tests, or a process that goes on without them.
void asset_files_forget(void);
