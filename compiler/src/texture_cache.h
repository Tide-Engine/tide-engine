#pragma once
#include "asset_cache.h"
#include "tide/texture_blocks.h"

typedef struct texture_cache_view {
    uint32_t width, height;
    const uint8_t *rgba;
    tide_texture_blocks blocks;
    // What the image is in the pack, as texture_content says: texture_cache_open fills it, and
    // texture_cache_write works it out itself
    uint8_t content[32];
} texture_cache_view;

void texture_cache_key(uint8_t key[32], const uint8_t source[32], const char *name, bool raw, bool compress);
bool texture_cache_open(const uint8_t *data, size_t size, texture_cache_view *out);
uint8_t *texture_cache_read(const char *folder, const uint8_t key[32], size_t *size, texture_cache_view *out);
void texture_cache_write(const char *folder, const uint8_t key[32], const texture_cache_view *view);

// The pack's content key of an image: the SHA-256 of what it stores (its size, and its blocks or else its RGBA).
// Declarations whose images come out the same, such as an opaque file as a color and as [Raw], are then one
// block in the pack and one image on the GPU.
void texture_content(const texture_cache_view *view, uint8_t content[32]);
