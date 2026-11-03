#include "texture_cache.h"

#include <stdlib.h>
#include <string.h>
#include "image_files.h"
#include "tide/sha256.h"

void texture_cache_key(uint8_t key[32], const uint8_t source[32], const char *name, const bool raw, const bool compress)
{
    const uint32_t variant[] = {raw, compress};
    tide_sha256 h;
    uint8_t context[32];
    tide_sha256_init(&h);
    tide_sha256_add(&h, variant, sizeof variant);
    tide_sha256_add(&h, name, strlen(name)); // TGA uses its written suffix to select a decoder.
    tide_sha256_end(&h, context);
    asset_cache_key(key, "texture", source, context, sizeof context);
}

bool texture_cache_open(const uint8_t *data, const size_t size, texture_cache_view *out)
{
    if (size < 20) return false;
    uint32_t head[5], levels, bytes;
    memcpy(head, data, sizeof head);
    const uint64_t pixels = (uint64_t)head[0] * head[1];
    if (!head[0] || !head[1] || head[0] > TIDE_PNG_MAX_SIDE || head[1] > TIDE_PNG_MAX_SIDE
        || pixels > TIDE_IMAGE_MAX_BYTES / sizeof(tide_color)) return false;
    const bool blocks = tide_texture_blocks_size((int32_t)head[0], (int32_t)head[1], head[2], &levels, &bytes);
    if (!((head[2] == 0 && head[3] == 0 && head[4] == pixels * 4 && size - 20 == pixels * 4)
          || (blocks && head[3] == levels && head[4] == bytes && size - 20 == bytes))) return false;
    *out = (texture_cache_view){.width = head[0], .height = head[1], .rgba = head[2] ? NULL : data + 20,
        .blocks = {.pixels = head[2] ? data + 20 : NULL, .format = head[2], .levels = head[3], .bytes = head[4]}};
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
    const size_t size = v->blocks.pixels ? v->blocks.bytes : (size_t)v->width * v->height * 4u;
    if (!folder || size > ASSET_CACHE_MAX_BYTES - 20u) return;
    uint8_t *encoded = malloc(size + 20);
    if (!encoded) return;
    const uint32_t head[] = {v->width, v->height, v->blocks.pixels ? v->blocks.format : 0,
        v->blocks.pixels ? v->blocks.levels : 0, (uint32_t)size};
    memcpy(encoded, head, sizeof head);
    memcpy(encoded + sizeof head, v->blocks.pixels ? v->blocks.pixels : v->rgba, size);
    asset_cache_write(folder, key, encoded, size + sizeof head);
    free(encoded);
}
