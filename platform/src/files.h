#pragma once

// Files the platform layer keeps for itself, beside a game's own (io.c): the
// renderer's pipeline cache, which goes in the game's temporary cache folder.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The game's temporary cache folder, once the host named the game
// (tide_platform_io_init), else NULL. Another string, at another address,
// once another game is named.
const char *tide_platform_cache_folder(void);

#ifndef __wasm__
// A whole file, of `limit` bytes at most (tide_alloc's, which the caller
// frees), or NULL where there's none or it's bigger.
uint8_t *tide_file_read_whole(const char *path, size_t limit, size_t *size);
// A file written whole, in place of what was there only once all of it is:
// a program stopped halfway, or another writing it too, leaves a whole one.
bool tide_file_write_whole(const char *path, const void *data, size_t size);
#endif
