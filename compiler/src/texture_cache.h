#pragma once
#include "asset_cache.h"
#include "tide/texture_blocks.h"

typedef struct texture_cache_view {
    uint32_t width, height;
    const uint8_t *rgba;
    tide_texture_blocks blocks;
} texture_cache_view;

void texture_cache_key(uint8_t key[32], const uint8_t source[32], const char *name, bool raw, bool compress);
bool texture_cache_open(const uint8_t *data, size_t size, texture_cache_view *out);
uint8_t *texture_cache_read(const char *folder, const uint8_t key[32], size_t *size, texture_cache_view *out);
void texture_cache_write(const char *folder, const uint8_t key[32], const texture_cache_view *view);
