#pragma once

#include <stddef.h>
#include <stdint.h>

// Best-effort, content-addressed build-tool cache. Returned bytes are malloc-owned.
// Keys include the compiler/runtime source identity and host scalar byte order.
// Missing, incompatible, damaged or unwritable entries are ordinary cache misses.
#define ASSET_CACHE_MAX_BYTES (1024u * 1024u * 1024u)
void asset_cache_key(uint8_t out[32], const char *kind, const uint8_t source[32], const void *variant, size_t size);
uint8_t *asset_cache_read(const char *game_folder, const uint8_t key[32], size_t *size);
void asset_cache_write(const char *game_folder, const uint8_t key[32], const void *data, size_t size);
