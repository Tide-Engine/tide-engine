#include "texture_cache.h"

#include <stdlib.h>
#include <string.h>
#include "asset_files.h"
#include "image_files.h"
#include "texture_compress.h"
#include "tide/sha256.h"

void texture_cache_key(uint8_t key[32], const uint8_t source[32], const char *name, const bool raw, const bool compress)
{
    // The third word is the entry's layout: 2 since entries carry their content key and [Raw] images are compressed
    // too (2026-11-04)
    const uint32_t variant[] = {raw, compress, 2};
    tide_sha256 h;
    uint8_t context[32];
    tide_sha256_init(&h);
    tide_sha256_add(&h, variant, sizeof variant);
    tide_sha256_add(&h, name, strlen(name)); // TGA uses its written suffix to select a decoder.
    tide_sha256_end(&h, context);
    asset_cache_key(key, "texture", source, context, sizeof context);
}

static size_t payload_size(const texture_cache_view *v)
{
    return v->blocks.pixels ? v->blocks.bytes : (size_t)v->width * v->height * 4u;
}

void texture_content(const texture_cache_view *v, uint8_t content[32])
{
    static const char version[] = "tide texture content 1";
    const uint32_t head[] = {v->width, v->height, v->blocks.pixels ? v->blocks.format : 0,
        v->blocks.pixels ? v->blocks.levels : 0};
    tide_sha256 h;
    tide_sha256_init(&h);
    tide_sha256_add(&h, version, sizeof version);
    tide_sha256_add(&h, head, sizeof head);
    tide_sha256_add(&h, v->blocks.pixels ? v->blocks.pixels : v->rgba, payload_size(v));
    tide_sha256_end(&h, content);
}

// An entry: width, height, the blocks' format, levels and bytes (0, 0 and the RGBA's for RGBA), the content key,
// then the blocks or the RGBA.
enum { HEAD = 20, ENTRY = HEAD + 32 };

bool texture_cache_open(const uint8_t *data, const size_t size, texture_cache_view *out)
{
    if (size < ENTRY) return false;
    uint32_t head[5], levels, bytes;
    memcpy(head, data, sizeof head);
    const uint64_t pixels = (uint64_t)head[0] * head[1];
    if (!head[0] || !head[1] || head[0] > TIDE_PNG_MAX_SIDE || head[1] > TIDE_PNG_MAX_SIDE
        || pixels > TIDE_IMAGE_MAX_BYTES / sizeof(tide_color)) return false;
    const bool blocks = tide_texture_blocks_size((int32_t)head[0], (int32_t)head[1], head[2], &levels, &bytes);
    if (!((head[2] == 0 && head[3] == 0 && head[4] == pixels * 4 && size - ENTRY == pixels * 4)
          || (blocks && head[3] == levels && head[4] == bytes && size - ENTRY == bytes))) return false;
    *out = (texture_cache_view){.width = head[0], .height = head[1], .rgba = head[2] ? NULL : data + ENTRY,
        .blocks = {.pixels = head[2] ? data + ENTRY : NULL, .format = head[2], .levels = head[3], .bytes = head[4]}};
    memcpy(out->content, data + HEAD, sizeof out->content);
    return true;
}

uint8_t *texture_cache_read(const char *folder, const uint8_t key[32], size_t *size, texture_cache_view *out)
{
    uint8_t *bytes = asset_cache_read(folder, key, size);
    if (bytes && !texture_cache_open(bytes, *size, out)) { free(bytes); bytes = NULL; }
    return bytes;
}

void texture_cache_write(const char *folder, const uint8_t key[32], const texture_cache_view *v)
{
    const size_t size = payload_size(v);
    if (!folder || size > ASSET_CACHE_MAX_BYTES - ENTRY) return;
    uint8_t *encoded = malloc(size + ENTRY);
    if (!encoded) return;
    const uint32_t head[] = {v->width, v->height, v->blocks.pixels ? v->blocks.format : 0,
        v->blocks.pixels ? v->blocks.levels : 0, (uint32_t)size};
    memcpy(encoded, head, sizeof head);
    texture_content(v, encoded + HEAD);
    memcpy(encoded + ENTRY, v->blocks.pixels ? v->blocks.pixels : v->rgba, size);
    asset_cache_write(folder, key, encoded, size + ENTRY);
    free(encoded);
}

// A color's channel as a byte, times the color's alpha, as the draw list makes
// a grid's cell one (mesh.c): what a GPU blends between two pixels is then
// right, whatever color the file gives its clear ones.
static uint8_t channel_byte(const float v, const float alpha)
{
    const float a = !(alpha > 0.0f) ? 0.0f : alpha >= 1.0f ? 1.0f : alpha;
    const float c = !(v > 0.0f) ? 0.0f : v >= 1.0f ? 1.0f : v;
    return (uint8_t)(c * a * 255.0f + 0.5f);
}

bool texture_make(const asset_match *item, const bool raw, const bool compress, texture_made *out, char why[256])
{
    asset_match decoded;
    bool decoded_here = false;
    if (!item->image.pixels) {
        decoded = *item;
        if (!image_file_read_mips(item->data, item->size, item->key, &decoded.image, &decoded.image_mips, why)) return false;
        item = &decoded;
        decoded_here = true;
    }
    const size_t pixels = (size_t)item->image.width * (size_t)item->image.height;
    uint8_t *rgba = malloc(pixels ? pixels * 4u : 1u);
    if (!rgba) abort();
    for (size_t i = 0; i < pixels; i++) {
        const tide_color c = item->image.pixels[i];
        const float worked = raw ? 1.0f : c.a; // [Raw]: numbers, as the file has them
        rgba[i * 4u] = channel_byte(c.r, worked), rgba[i * 4u + 1u] = channel_byte(c.g, worked);
        rgba[i * 4u + 2u] = channel_byte(c.b, worked), rgba[i * 4u + 3u] = channel_byte(1.0f, c.a);
    }
    tide_texture_blocks compressed = {0};
    if (item->image_mips.pixels) {
        // Authored mip levels (DDS) retain their alpha coverage and raw
        // channels. Premultiply each color level, not only the largest.
        compressed = item->image_mips;
        uint8_t *chain = malloc(compressed.bytes);
        if (!chain) abort();
        for (uint32_t i = 0; i < compressed.bytes; i += 4u) {
            const uint8_t *c = compressed.pixels + i;
            const float alpha = (float)c[3] / 255.0f, worked = raw ? 1.0f : alpha;
            chain[i] = channel_byte((float)c[0] / 255.0f, worked);
            chain[i + 1u] = channel_byte((float)c[1] / 255.0f, worked);
            chain[i + 2u] = channel_byte((float)c[2] / 255.0f, worked);
            chain[i + 3u] = c[3];
        }
        compressed.pixels = chain;
    } else if (compress) {
        // Colors are premultiplied by now, and [Raw]'s channels are numbers as the file has them: either way
        // each channel of a mip is the mean of the four below it, and BC1/BC3 keep the bytes they're given.
        compressed = texture_compress(rgba, (int32_t)item->image.width, (int32_t)item->image.height);
    }
    out->view = (texture_cache_view){.width = item->image.width, .height = item->image.height, .rgba = rgba,
        .blocks = compressed};
    if (decoded_here) {
        free(decoded.image.pixels);
        free((void *)(uintptr_t)decoded.image_mips.pixels);
    }
    return true;
}

void texture_made_free(texture_made *made)
{
    free((void *)(uintptr_t)made->view.blocks.pixels);
    free((void *)(uintptr_t)made->view.rgba);
}
