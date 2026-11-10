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

// An image file's texture: decoded where it wasn't, a byte a channel, premultiplied but for [Raw] (`raw`), and in
// blocks where it has authored mips or is to be compressed. False with `why` where its file isn't an image. Touches
// nothing but its own memory, so images are made on several threads at once. What it makes is what the cache keeps,
// so it's here, with the code the cache's key covers (compiler/CMakeLists.txt). Its view's RGBA and blocks are the
// made's, freed by texture_made_free.
struct asset_match;
typedef struct texture_made {
    texture_cache_view view;
} texture_made;
bool texture_make(const struct asset_match *item, bool raw, bool compress, texture_made *out, char why[256]);
void texture_made_free(texture_made *made);
