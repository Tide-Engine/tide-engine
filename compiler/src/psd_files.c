// Adobe's Photoshop File Formats Specification:
// https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
// Only the saved composite is decoded. Section lengths let us skip everything
// that would require Photoshop's compositor; layer metadata only identifies
// merged transparency. ZIP uses the engine's existing deflate reader.

#include "psd_files.h"
#include "tide/internal/count_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct psd_reader {
    const uint8_t *bytes;
    size_t size, at;
    bool ok;
} psd_reader;

static const uint8_t *take(psd_reader *r, const size_t n)
{
    if (!r->ok || n > r->size - r->at) {
        r->ok = false;
        return NULL;
    }
    const uint8_t *p = r->bytes + r->at;
    r->at += n;
    return p;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t read16(psd_reader *r) { const uint8_t *p = take(r, 2); return p ? be16(p) : 0; }
static uint32_t read32(psd_reader *r) { const uint8_t *p = take(r, 4); return p ? be32(p) : 0; }

static psd_reader section(psd_reader *r)
{
    const uint32_t n = read32(r);
    const uint8_t *p = take(r, n);
    return (psd_reader){p, p ? n : 0, 0, p != NULL};
}

static bool fail(char why[256], const char *message)
{
    snprintf(why, 256, "%s", message);
    return false;
}

static bool resources(psd_reader *r, bool *merged)
{
    while (r->ok && r->at < r->size) {
        const uint8_t *signature = take(r, 4);
        if (!signature || memcmp(signature, "8BIM", 4) != 0) return false;
        const uint16_t id = read16(r);
        const uint8_t *name = take(r, 1);
        if (!name) return false;
        take(r, (size_t)name[0] + ((name[0] & 1u) ? 0u : 1u)); // Pascal string, padded to two bytes
        psd_reader data = section(r);
        if (!r->ok) return false;
        if (id == 1057) {
            if (data.size < 5 || be32(data.bytes) != 1) return false;
            *merged = data.bytes[4] != 0; // Version Info: hasRealMergedData
        }
        if (data.size & 1u) take(r, 1);
    }
    return r->ok;
}

static bool layer_alpha(psd_reader *r, bool *alpha)
{
    if (!r->size) return r->ok;
    psd_reader info = section(r);
    if (!r->ok || !info.ok) return false;
    if (info.size) {
        const uint16_t count = read16(&info);
        if (!info.ok) return false;
        *alpha = (count & 0x8000u) != 0;
    }
    if (r->at == r->size) return true; // Older writers omit the global mask and tagged blocks
    // The layer records and their pixels need no interpretation for a saved
    // composite. Global mask and additional tagged blocks follow them.
    const size_t remaining = r->size - r->at;
    const bool tags = remaining >= 4 && (memcmp(r->bytes + r->at, "8BIM", 4) == 0
                                      || memcmp(r->bytes + r->at, "8B64", 4) == 0);
    if (!tags) section(r); // Some writers omit the mask length even before tagged blocks
    while (r->ok && r->at < r->size) {
        const uint8_t *signature = take(r, 4), *key = take(r, 4);
        if (!signature || !key) return false;
        const bool wide = memcmp(signature, "8B64", 4) == 0;
        if (!wide && memcmp(signature, "8BIM", 4) != 0) return false;
        // PSD version 1 keeps four-byte lengths for either signature. PSB's
        // wider lengths depend on the block's key, not this signature.
        const uint32_t n = read32(r);
        const uint8_t *data = take(r, n);
        if (!data) return false;
        if (memcmp(key, "Lr16", 4) == 0) {
            if (n < 2) return false;
            *alpha = *alpha || (be16(data) & 0x8000u) != 0;
        } else if (memcmp(key, "Mtrn", 4) == 0 || memcmp(key, "Mt16", 4) == 0) {
            *alpha = true;
        }
        take(r, (size_t)((4u - (n & 3u)) & 3u));
    }
    return r->ok;
}

typedef struct psd_rows {
    tide_color *pixels;
    uint8_t *row;
    uint32_t width, height;
    size_t row_bytes, filled, index, count;
    uint16_t depth;
    bool alpha, prediction;
    uint32_t adler_a, adler_b;
} psd_rows;

static void put_row(psd_rows *s)
{
    const size_t channel = s->index / s->height, y = s->index % s->height;
    uint16_t previous = 0;
    for (uint32_t x = 0; x < s->width; x++) {
        uint16_t v = s->depth == 8 ? s->row[x] : be16(s->row + (size_t)x * 2u);
        if (s->prediction) {
            v = (uint16_t)(v + previous);
            if (s->depth == 8) v &= 255u;
            previous = v;
        }
        const float value = (float)v / (s->depth == 8 ? 255.0f : 65535.0f);
        tide_color *c = &s->pixels[y * s->width + x];
        switch (channel) {
        case 0: *c = (tide_color){value, 0.0f, 0.0f, 1.0f}; break;
        case 1: c->g = value; break;
        case 2: c->b = value; break;
        case 3: if (s->alpha) c->a = value; break;
        default: break; // Saved selections and spot channels aren't opacity
        }
    }
    s->index++;
    s->filled = 0;
}

static bool zip_bytes(void *user, const uint8_t *bytes, const size_t count)
{
    psd_rows *s = user;
    // Adler-32 covers the decompressed bytes before reversing prediction.
    for (size_t i = 0; i < count;) {
        size_t end = i + (count - i > 5552u ? 5552u : count - i);
        for (; i < end; i++) { s->adler_a += bytes[i]; s->adler_b += s->adler_a; }
        s->adler_a %= 65521u;
        s->adler_b %= 65521u;
    }
    for (size_t i = 0; i < count;) {
        if (s->index == s->count) return false;
        size_t n = s->row_bytes - s->filled;
        if (n > count - i) n = count - i;
        memcpy(s->row + s->filled, bytes + i, n);
        s->filled += n;
        i += n;
        if (s->filled == s->row_bytes) put_row(s);
    }
    return true;
}

static bool packbits(const uint8_t *p, const size_t size, uint8_t *row, const size_t row_bytes)
{
    size_t at = 0, made = 0;
    while (at < size) {
        const uint8_t code = p[at++];
        if (code == 128u) continue; // No-op
        const size_t n = code < 128u ? (size_t)code + 1u : 257u - code;
        if (n > row_bytes - made) return false;
        if (code < 128u) {
            if (n > size - at) return false;
            memcpy(row + made, p + at, n);
            at += n;
        } else {
            if (at == size) return false;
            memset(row + made, p[at++], n);
        }
        made += n;
    }
    return made == row_bytes;
}

static float unmatte(const float value, const float alpha)
{
    const float straight = (value + alpha - 1.0f) / alpha;
    return straight < 0.0f ? 0.0f : straight > 1.0f ? 1.0f : straight;
}

bool psd_file_read(const uint8_t *bytes, const size_t size, tide_png_image *out, char why[256])
{
    *out = (tide_png_image){0};
    why[0] = '\0';
    psd_reader r = {bytes, size, 0, true};
    const uint8_t *signature = take(&r, 4);
    if (!signature || memcmp(signature, "8BPS", 4) != 0) return fail(why, "has no PSD signature");
    if (read16(&r) != 1) return fail(why, "isn't PSD version 1; export PSB as a PSD or PNG");
    const uint8_t *reserved = take(&r, 6);
    if (!reserved) return fail(why, "has a truncated header");
    for (size_t i = 0; i < 6; i++) if (reserved[i]) return fail(why, "has an invalid header");
    const uint16_t channels = read16(&r);
    const uint32_t height = read32(&r), width = read32(&r);
    const uint16_t depth = read16(&r), mode = read16(&r);
    if (!r.ok) return fail(why, "has a truncated header");
    if (mode != 3) return fail(why, "isn't RGB; convert the document to RGB before saving");
    if (depth != 8 && depth != 16) return fail(why, "isn't 8 or 16 bits per channel; convert the document before saving");
    if (channels < 3 || channels > 56) return fail(why, "has an invalid RGB channel count");
    if (!width || !height || width > TIDE_PNG_MAX_SIDE || height > TIDE_PNG_MAX_SIDE)
        return fail(why, "has invalid dimensions: zero, or more than the 16384 pixels each way an image import takes (TIDE_PNG_MAX_SIDE)");
    const uint64_t pixel_count = (uint64_t)width * height;
    if (pixel_count > TIDE_PSD_MAX_PIXELS) {
        char all[32], most[32];
        snprintf(why, 256, "is %u by %u pixels, %s in all, more than the %s a PSD import takes (TIDE_PSD_MAX_PIXELS); "
                           "save it smaller, or export it as a PNG, which can be 16384 pixels each way",
                 (unsigned)width, (unsigned)height, tide_count_text(pixel_count, all), tide_count_text(TIDE_PSD_MAX_PIXELS, most));
        return false;
    }
    if (pixel_count > SIZE_MAX / sizeof(tide_color)) return fail(why, "is too large to decode on this machine");
    section(&r); // Color mode data
    psd_reader image_resources = section(&r);
    bool merged = true, alpha = false;
    if (!r.ok || !resources(&image_resources, &merged)) return fail(why, "has truncated or invalid image resources");
    if (!merged) return fail(why, "has no saved composite image; save it with Maximize Compatibility enabled");
    psd_reader layers = section(&r);
    if (!r.ok || !layer_alpha(&layers, &alpha)) return fail(why, "has truncated or invalid layer metadata");
    if (alpha && channels < 4) return fail(why, "has no channel for its merged transparency");
    if (r.size - r.at < 2) return fail(why, "has no saved composite image; save it with Maximize Compatibility enabled");
    const uint16_t compression = read16(&r);
    if (compression > 3) return fail(why, "has an unknown composite compression method");
    const size_t row_bytes = (size_t)width * (depth / 8u), rows = (size_t)height * channels;
    const uint64_t expected = (uint64_t)row_bytes * rows;
    const uint8_t *lengths = NULL;
    if (compression == 0) {
        if (expected != r.size - r.at) return fail(why, "has truncated or invalid composite pixels");
    } else if (compression == 1) {
        lengths = take(&r, rows * 2u);
        if (!lengths) return fail(why, "has truncated RLE row lengths");
        uint64_t encoded = 0;
        for (size_t i = 0; i < rows; i++) {
            const uint16_t n = be16(lengths + i * 2u);
            // Even the shortest PackBits row needs one code and value for
            // each 128 bytes. Check before allocating an image's pixels.
            if (n < 2u * ((row_bytes + 127u) / 128u)) return fail(why, "has invalid RLE row lengths");
            encoded += n;
        }
        if (encoded != r.size - r.at) return fail(why, "has truncated or invalid RLE pixels");
    } else if (r.size - r.at < 6 || expected / 1032u > r.size - r.at) {
        return fail(why, "has truncated or invalid ZIP pixels");
    }
    tide_color *pixels = malloc((size_t)pixel_count * sizeof *pixels);
    uint8_t *row = malloc(row_bytes);
    if (!pixels || !row) { free(pixels); free(row); return fail(why, "is too large to decode on this machine"); }
    psd_rows s = {pixels, row, width, height, row_bytes, 0, 0, rows, depth, alpha, compression == 3, 1, 0};
    bool ok = true;
    if (compression < 2) {
        for (size_t i = 0; i < rows && ok; i++) {
            const size_t n = compression ? be16(lengths + i * 2u) : row_bytes;
            const uint8_t *p = take(&r, n);
            ok = p != NULL;
            if (ok && compression) ok = packbits(p, n, row, row_bytes);
            else if (ok) memcpy(row, p, row_bytes);
            if (ok) put_row(&s);
        }
    } else {
        const uint8_t *z = r.bytes + r.at;
        const size_t n = r.size - r.at;
        size_t used = 0;
        ok = (z[0] & 15u) == 8u && (z[0] >> 4) <= 7u && !(z[1] & 32u)
            && (((uint32_t)z[0] << 8 | z[1]) % 31u) == 0;
        if (ok) ok = tide_inflate(z + 2, n - 6, zip_bytes, &s, &used);
        if (ok) ok = used == n - 6 && s.index == rows && !s.filled
            && be32(z + n - 4) == (s.adler_b << 16 | s.adler_a);
    }
    free(row);
    if (!ok) { free(pixels); return fail(why, "has truncated or invalid composite pixels"); }
    if (alpha) {
        // Photoshop saves the composite against white. Recover straight
        // channels here, as the asset pipeline expects from PNGs too.
        for (size_t i = 0; i < (size_t)pixel_count; i++) {
            tide_color *c = &pixels[i];
            if (c->a == 0.0f) *c = TIDE_COLOR_CLEAR;
            else if (c->a != 1.0f) {
                c->r = unmatte(c->r, c->a);
                c->g = unmatte(c->g, c->a);
                c->b = unmatte(c->b, c->a);
            }
        }
    }
    *out = (tide_png_image){width, height, pixels};
    return true;
}
