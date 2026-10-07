#pragma once

#include <stdbool.h>
#include <stddef.h>

// The files `asset` declarations name (docs/spec.md, Assets), as the tools
// keep track of them: paths relative to the file that names them, and the
// SHA-256 of each named file, kept in the game's .tide/ so that a file moved
// outside the editor is found again by its bytes.
//
// Paths here are absolute with forward slashes, and a folder ends in '/'.
// Windows paths ignore case.

// Whether a path is a glob (`*` or `?` in it), or a URL (http:// or https://).
bool asset_path_is_glob(const char *path, size_t len);
bool asset_path_is_url(const char *path, size_t len);

// `folder` + `relative` (`len` bytes, either separator), with `.` and `..`
// resolved. malloc'd.
char *asset_path_join(const char *folder, const char *relative, size_t len);

// The folder `file` is in. malloc'd.
char *asset_path_folder(const char *file);

// `to` written from `from`, a folder (with or without its '/'): "img/hero.png",
// "../a/img/hero.png". `to` itself when they share no root (another drive). malloc'd.
char *asset_path_relative(const char *from, const char *to);

// Whether `path` is in `folder` or its subfolders (`folder` itself isn't).
bool asset_path_under(const char *path, const char *folder);
bool asset_path_same(const char *a, const char *b);

// "D:/game/art/hero.png" -> "hero.png" (a pointer into `path`).
const char *asset_path_name(const char *path);

// ---------------------------------------------------------------------------
// The hashes of the game's named files: <folder>/.tide/assets.sha256, a line
// a file: its SHA-256 as hex, its size and its path from the game's folder.
// Written when the game is checked, and read when a named file is missing,
// to look for one with the same bytes.

typedef struct asset_hash {
    char *path; // From the game's folder, forward slashes
    char hex[65];
    unsigned long long size;
} asset_hash;

typedef struct asset_hashes {
    asset_hash *items;
    int count;
    int cap;
} asset_hashes;

#define ASSET_HASHES_FILE ".tide/assets.sha256"

// Reads the game's file, if there's one.
void asset_hashes_load(asset_hashes *h, const char *game_folder);
void asset_hashes_free(asset_hashes *h);
// Notes the file at `path` (from the game's folder) and its bytes. A path
// noted before is replaced.
void asset_hashes_note(asset_hashes *h, const char *path, const void *data, size_t size);
// Notes the file at `path` with a hash worked out already.
void asset_hashes_set(asset_hashes *h, const char *path, const char hex[65], unsigned long long size);
// Keeps what `from` knew of `path`, for a file that's missing now.
void asset_hashes_keep(asset_hashes *h, const asset_hashes *from, const char *path);
const asset_hash *asset_hashes_find(const asset_hashes *h, const char *path);
// The file's text, as asset_hashes_save writes it. malloc'd.
char *asset_hashes_text(const asset_hashes *h);
// Writes the game's file where its text would differ from what's there,
// making .tide/ as tide does (hidden, with a .gitignore); nothing for a
// store with no entries and no file. Returns whether it wrote.
bool asset_hashes_save(const asset_hashes *h, const char *game_folder);

// The SHA-256 of some bytes as 64 hex digits and a NUL (sha256.h), and
// whether it's `hex`.
void asset_hash_of(const void *data, size_t size, char out[65]);
