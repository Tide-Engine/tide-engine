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

// The GPU driver's own cache of compiled shaders, kept in a folder of the
// game's cache folder (`driver`) where the host named the game before its
// window opened: NVIDIA's driver reads __GL_SHADER_DISK_CACHE_PATH as Vulkan
// starts and not after. Its own folder is the machine's, shared by every
// program, and once full it keeps nothing new, so a game whose shaders changed
// compiled them again at every start. The driver only adds to it: past 256
// MiB the folder starts over. Nothing where the environment already says
// where the driver keeps it, or that it keeps none (__GL_SHADER_DISK_CACHE=0).
// Desktop: before Vulkan starts (tide_window_open).
void tide_platform_driver_cache(void);

#ifndef __wasm__
// A whole file, of `limit` bytes at most (tide_alloc's, which the caller
// frees), or NULL where there's none or it's bigger.
uint8_t *tide_file_read_whole(const char *path, size_t limit, size_t *size);
// The game's folder of saved data (tide_io.persistent_data_path) by its app
// ID, or `directory` (--data-dir) made absolute, made where it wasn't:
// tide_alloc's, which the caller frees, or NULL where it can't be made.
char *tide_platform_data_folder(const char *app_id, const char *directory);
// A file written whole, in place of what was there only once all of it is:
// a program stopped halfway, or another writing it too, leaves a whole one.
bool tide_file_write_whole(const char *path, const void *data, size_t size);
#endif
