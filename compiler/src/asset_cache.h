#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Best-effort, content-addressed build-tool cache in <game>/.tide/assets/imports/<importers>/, a folder for each
// version of the code entries come from (compiler/CMakeLists.txt). Returned bytes are malloc-owned.
// Keys include that code's identity and host scalar byte order, not the whole compiler's: a compiler or engine edit
// elsewhere keeps a game's imports. Missing, incompatible, damaged or unwritable entries are ordinary cache misses.
#define ASSET_CACHE_MAX_BYTES (1024u * 1024u * 1024u)
void asset_cache_key(uint8_t out[32], const char *kind, const uint8_t source[32], const void *variant, size_t size);
uint8_t *asset_cache_read(const char *game_folder, const uint8_t key[32], size_t *size);
// Whether there's an entry for `key` that may be read: its file, of a size one can be. Reading still checks it.
bool asset_cache_has(const char *game_folder, const uint8_t key[32]);
void asset_cache_write(const char *game_folder, const uint8_t key[32], const void *data, size_t size);
// Where `key`'s entry is kept. False without a game folder, or for a path too long.
bool asset_cache_path(char path[4096], const char *game_folder, const uint8_t key[32]);

// Days an entry no build has read is kept, and days another importer's folder is kept after its compiler last
// tidied: what isn't used is let go of, so the cache doesn't grow with each edit and each version of tide.
#define ASSET_CACHE_UNUSED_DAYS 30u
#define ASSET_CACHE_OTHER_DAYS 14u
// Once a day at the most for a game (and otherwise just a small file's read): removes other importers' folders
// unused for ASSET_CACHE_OTHER_DAYS, files earlier compilers kept beside the folders, and this importer's entries
// unread for ASSET_CACHE_UNUSED_DAYS. A compilation using the game's cache calls it first.
void asset_cache_tidy(const char *game_folder);
