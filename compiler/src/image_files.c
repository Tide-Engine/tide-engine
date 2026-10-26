#include "image_files.h"
#include "psd_files.h"
#include "tide/internal/count_text.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_MIB (TIDE_IMAGE_MAX_BYTES >> 20) // For messages

// stb's temporary allocations are accounted per importing thread against
// TIDE_IMAGE_MAX_BYTES (image_files.h). The final float pixels are also
// checked against this budget, but use ordinary malloc
// because the existing asset cache releases them with free().
typedef union image_allocation {
    max_align_t alignment;
    size_t size;
} image_allocation;
static _Thread_local size_t image_memory;
static _Thread_local bool image_limited;

static void *image_alloc(const size_t size)
{
    if (size > TIDE_IMAGE_MAX_BYTES - image_memory || size > SIZE_MAX - sizeof(image_allocation)) {
        image_limited = true;
        return NULL;
    }
    image_allocation *p = malloc(sizeof *p + size);
    if (!p) return NULL;
    p->size = size;
    image_memory += size;
    return p + 1;
}

static void image_free(void *ptr)
{
    if (!ptr) return;
    image_allocation *p = (image_allocation *)ptr - 1;
    image_memory -= p->size;
    free(p);
}

static __attribute__((unused)) void *image_realloc(void *ptr, const size_t size)
{
    if (!ptr) return image_alloc(size);
    image_allocation *p = (image_allocation *)ptr - 1;
    const size_t old = p->size;
    if (size > TIDE_IMAGE_MAX_BYTES - (image_memory - old) || size > SIZE_MAX - sizeof *p) {
        image_limited = true;
        return NULL;
    }
    p = realloc(p, sizeof *p + size);
    if (!p) return NULL;
    p->size = size;
    image_memory = image_memory - old + size;
    return p + 1;
}

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS TIDE_PNG_MAX_SIDE
#define STBI_MALLOC(size) image_alloc(size)
#define STBI_REALLOC(ptr, size) image_realloc(ptr, size)
#define STBI_FREE(ptr) image_free(ptr)
#include <stb_image.h>

typedef enum image_format { IMAGE_UNKNOWN, IMAGE_PNG, IMAGE_PSD, IMAGE_JPEG, IMAGE_BMP, IMAGE_TGA } image_format;

static bool fail(char why[256], const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(why, 256, format, args);
    va_end(args);
    return false;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static bool suffix(const char *name, const char *ext)
{
    if (!name) return false;
    size_t end = strlen(name);
    if (strncmp(name, "https://", 8) == 0 || strncmp(name, "http://", 7) == 0) {
        const char *path = strpbrk(name + (name[4] == 's' ? 8 : 7), "/?#");
        if (!path || *path != '/') return false;
        const char *stop = strpbrk(path, "?#");
        if (stop) end = (size_t)(stop - name);
    }
    const size_t n = strlen(ext);
    if (end < n) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[end - n + i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        if (c != (unsigned char)ext[i]) return false;
    }
    return true;
}

static bool dimensions(const uint32_t width, const uint32_t height, const char *format, char why[256])
{
    if (!width || !height) return fail(why, "%s image has invalid zero dimensions", format);
    if (width > TIDE_PNG_MAX_SIDE || height > TIDE_PNG_MAX_SIDE)
        return fail(why, "%s image is %u by %u pixels, more than the %u each way an image import takes (TIDE_PNG_MAX_SIDE)", format, width, height,
                    TIDE_PNG_MAX_SIDE);
    return true;
}

// The decoder deliberately tolerates some damaged files. Preflight validates
// lengths/complete pixel payloads before allowing those recovery paths, and
// gives unsupported encodings a useful message instead of invented pixels.
static bool jpeg_header(const uint8_t *data, const size_t size, uint32_t *width, uint32_t *height, char why[256])
{
    if (size < 4 || data[0] != 0xff || data[1] != 0xd8) return fail(why, "JPEG image has an invalid or truncated header");
    size_t at = 2;
    bool frame = false, scan = false;
    uint8_t component_ids[4] = {0};
    unsigned int components = 0, dc_components = 0, scans = 0;
    int adobe_transform = -1;
    while (at < size) {
        if (data[at++] != 0xff) return fail(why, "JPEG image has invalid marker data");
        while (at < size && data[at] == 0xff) at++;
        if (at == size) break;
        const uint8_t marker = data[at++];
        if (marker == 0xd9) {
            if (components == 4 && adobe_transform != 0 && adobe_transform != 2)
                return fail(why, "four-channel JPEG requires an Adobe CMYK or YCCK marker; export an RGB JPEG or PNG");
            return frame && scan && dc_components == (1u << components) - 1u
                     ? true : fail(why, "JPEG image has no complete frame");
        }
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd8)) return fail(why, "JPEG image has an invalid marker sequence");
        if (size - at < 2) break;
        const size_t n = be16(data + at);
        if (n < 2 || n > size - at) break;
        if (marker == 0xee && n >= 14 && !memcmp(data + at + 2, "Adobe", 5)) adobe_transform = data[at + 13];
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (marker != 0xc0 && marker != 0xc1 && marker != 0xc2)
                return fail(why, "JPEG encoding isn't supported; use an 8-bit baseline or progressive JPEG, or export PNG");
            if (frame || n < 8) return fail(why, "JPEG image has an invalid frame header");
            if (data[at + 2] != 8)
                return fail(why, "JPEG isn't 8 bits per channel; use an 8-bit baseline or progressive JPEG, or export PNG");
            *height = be16(data + at + 3);
            *width = be16(data + at + 5);
            const uint8_t channels = data[at + 7];
            if ((channels != 1 && channels != 3 && channels != 4) || n != 8u + (size_t)channels * 3u)
                return fail(why, "JPEG image has an invalid component count or frame length");
            components = channels;
            for (unsigned int i = 0; i < components; i++) {
                component_ids[i] = data[at + 8u + i * 3u];
                for (unsigned int k = 0; k < i; k++) if (component_ids[k] == component_ids[i])
                    return fail(why, "JPEG image has duplicate component identifiers");
            }
            if (!dimensions(*width, *height, "JPEG", why)) return false;
            frame = true;
        }
        if (marker == 0xcc) return fail(why, "arithmetic-coded JPEG isn't supported; use a baseline or progressive JPEG, or export PNG");
        if (marker == 0xda) {
            if (!frame) return fail(why, "JPEG image has a scan before its frame header");
            if (++scans > TIDE_IMAGE_MAX_JPEG_SCANS)
                return fail(why, "JPEG image has more than the %u progressive scans an image import takes (TIDE_IMAGE_MAX_JPEG_SCANS); save it again as a baseline or ordinary progressive JPEG",
                            TIDE_IMAGE_MAX_JPEG_SCANS);
            if (n < 6) return fail(why, "JPEG image has an invalid scan header");
            const unsigned int count = data[at + 2];
            if (!count || count > components || n != 6u + count * 2u) return fail(why, "JPEG image has an invalid scan header");
            if (data[at + 3u + count * 2u] == 0) {
                for (unsigned int i = 0; i < count; i++) {
                    const uint8_t id = data[at + 3u + i * 2u];
                    for (unsigned int k = 0; k < components; k++) if (id == component_ids[k]) dc_components |= 1u << k;
                }
            }
        }
        at += n;
        if (marker == 0xda) {
            scan = true;
            // Entropy bytes use FF00 escaping and restart markers. The next
            // other marker resumes the segment walk, including progressive scans.
            while (at < size) {
                if (data[at++] != 0xff) continue;
                const size_t start = at - 1;
                while (at < size && data[at] == 0xff) at++;
                if (at == size) break;
                const uint8_t next = data[at++];
                if (next == 0 || (next >= 0xd0 && next <= 0xd7)) continue;
                at = start;
                break;
            }
        }
    }
    return fail(why, "JPEG image is truncated or has invalid segment lengths; save the complete image again");
}

static bool mask_valid(uint32_t mask)
{
    if (!mask) return false;
    while (!(mask & 1u)) mask >>= 1;
    return mask <= 255u && (mask & (mask + 1u)) == 0;
}

static bool bmp_header(const uint8_t *data, const size_t size, uint32_t *width, uint32_t *height, char why[256])
{
    if (size < 26 || data[0] != 'B' || data[1] != 'M') return fail(why, "BMP image has an invalid or truncated header");
    const uint32_t hsz = le32(data + 14), offset = le32(data + 10);
    if (hsz != 12 && hsz != 40 && hsz != 56 && hsz != 108 && hsz != 124)
        return fail(why, "BMP header isn't supported; use a Windows BMP with uncompressed pixels or RGB bitfields, or export PNG");
    if ((size_t)hsz + 14u > size) return fail(why, "BMP image has a truncated header");
    uint16_t bpp;
    uint32_t compression = 0;
    size_t header_end = 14u + (size_t)hsz;
    if (hsz == 12) {
        *width = le16(data + 18), *height = le16(data + 20);
        if (le16(data + 22) != 1) return fail(why, "BMP image has an invalid plane count");
        bpp = le16(data + 24);
    } else {
        *width = le32(data + 18);
        const uint32_t signed_height = le32(data + 22);
        *height = signed_height & 0x80000000u ? 0u - signed_height : signed_height;
        if (le16(data + 26) != 1) return fail(why, "BMP image has an invalid plane count");
        bpp = le16(data + 28);
        compression = le32(data + 30);
        if (compression != 0 && compression != 3)
            return fail(why, "BMP compression isn't supported; use uncompressed pixels or RGB bitfields, or export PNG (RLE, JPEG and PNG-in-BMP aren't supported)");
    }
    if (!dimensions(*width, *height, "BMP", why)) return false;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        return fail(why, "BMP bit depth isn't supported; use 1, 4, 8, 16, 24 or 32 bits per pixel, or export PNG");
    if (hsz == 12 && bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24)
        return fail(why, "OS/2 core BMP bit depth isn't supported; use a Windows BMP or export PNG");
    if (compression == 3) {
        if (bpp != 16 && bpp != 32) return fail(why, "BMP RGB bitfields require 16 or 32 bits per pixel");
        if (hsz == 40) header_end += 12;
        if (header_end > size) return fail(why, "BMP image has truncated RGB bitfields");
        const uint32_t r = le32(data + 54), g = le32(data + 58), b = le32(data + 62);
        const uint32_t a = hsz >= 56 ? le32(data + 66) : 0;
        const uint32_t all = r | g | b | a;
        if (!mask_valid(r) || !mask_valid(g) || !mask_valid(b) || (a && !mask_valid(a))
            || (r & g) || (r & b) || (g & b) || (a & (r | g | b)) || (bpp == 16 && (all >> 16)))
            return fail(why, "BMP image has invalid or unsupported RGB bitfields; use contiguous, separate channels of at most 8 bits, or export PNG");
    }
    if (offset < header_end || offset > size) return fail(why, "BMP image has an invalid pixel offset");
    const size_t row = (((size_t)*width * bpp + 31u) / 32u) * 4u;
    if (row * *height > size - offset) return fail(why, "BMP image has truncated pixel rows");
    if (bpp < 16) {
        const size_t entry = hsz == 12 ? 3u : 4u;
        const size_t palette = (offset - header_end) / entry;
        if (!palette || palette > 256 || palette > (1u << bpp)) return fail(why, "BMP image has an invalid palette");
        for (uint32_t y = 0; y < *height; y++) {
            const uint8_t *p = data + offset + row * y;
            for (uint32_t x = 0; x < *width; x++) {
                const unsigned int index = bpp == 8 ? p[x] : bpp == 4 ? (p[x / 2] >> (x % 2 ? 0 : 4)) & 15u
                                                                     : (p[x / 8] >> (7 - x % 8)) & 1u;
                if (index >= palette) return fail(why, "BMP image has a pixel outside its palette");
            }
        }
    }
    return true;
}

static bool tga_header(const uint8_t *data, const size_t size, uint32_t *width, uint32_t *height, char why[256])
{
    if (size < 18) return fail(why, "TGA image has a truncated header");
    const uint8_t type = data[2], bpp = data[16], descriptor = data[17];
    const bool indexed = type == 1 || type == 9, gray = type == 3 || type == 11;
    const bool rle = type == 9 || type == 10 || type == 11;
    if (!(indexed || gray || type == 2 || type == 10) || data[1] != (indexed ? 1 : 0) || (descriptor & 0xc0))
        return fail(why, "TGA encoding isn't supported; use non-interleaved RGB, grayscale or indexed TGA, raw or RLE, or export PNG");
    *width = le16(data + 12), *height = le16(data + 14);
    if (!dimensions(*width, *height, "TGA", why)) return false;
    if ((indexed && bpp != 8 && bpp != 16) || (gray && bpp != 8 && bpp != 16)
        || (!indexed && !gray && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32))
        return fail(why, "TGA bit depth isn't supported; use RGB 15/16/24/32-bit, gray 8/16-bit or indices 8/16-bit, or export PNG");
    // stb treats packed RGB15/16 as opaque. Do not silently discard a declared alpha bit.
    if (!indexed && !gray && bpp == 16 && (descriptor & 15u))
        return fail(why, "TGA 16-bit pixel alpha isn't supported; export a 32-bit RGBA TGA or PNG");
    size_t at = 18u + data[0];
    const uint32_t first = le16(data + 3), entries = le16(data + 5);
    if (indexed) {
        const uint8_t bits = data[7];
        if (!entries || first + entries > 65536u || (bits != 8 && bits != 15 && bits != 16 && bits != 24 && bits != 32))
            return fail(why, "TGA image has an invalid or unsupported palette");
        if ((bits == 15 || bits == 16) && (descriptor & 15u))
            return fail(why, "TGA 16-bit palette alpha isn't supported; export a 32-bit palette or PNG");
        at += (size_t)entries * ((bits + 7u) / 8u);
    }
    if (at > size) return fail(why, "TGA image has a truncated identifier or palette");
    const size_t unit = (bpp + 7u) / 8u;
    size_t remaining = (size_t)*width * *height;
    while (remaining) {
        size_t count = remaining, stored = remaining;
        if (rle) {
            if (at == size) return fail(why, "TGA image has truncated RLE packets");
            const uint8_t packet = data[at++];
            count = (packet & 127u) + 1u;
            if (count > remaining) return fail(why, "TGA RLE packet extends past the image");
            stored = packet & 128u ? 1u : count;
        }
        if (stored > (size - at) / unit) return fail(why, "TGA image has truncated pixel data");
        if (indexed) {
            for (size_t i = 0; i < stored; i++) {
                const uint32_t index = unit == 1 ? data[at + i] : le16(data + at + i * unit);
                if (index < first || index - first >= entries) return fail(why, "TGA image has a pixel outside its palette");
            }
        }
        at += stored * unit;
        remaining -= count;
    }
    return true;
}

static const char *unsupported_magic(const uint8_t *data, const size_t size)
{
    if (size >= 6 && (!memcmp(data, "GIF87a", 6) || !memcmp(data, "GIF89a", 6))) return "GIF";
    if (size >= 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WEBP", 4)) return "WebP";
    if (size >= 4 && (!memcmp(data, "II\x2a\0", 4) || !memcmp(data, "MM\0\x2a", 4)
                     || !memcmp(data, "II\x2b\0", 4) || !memcmp(data, "MM\0\x2b", 4))) return "TIFF";
    if (size >= 4 && !memcmp(data, "\x76\x2f\x31\x01", 4)) return "OpenEXR";
    if (size >= 6 && (!memcmp(data, "#?RADI", 6) || !memcmp(data, "#?RGBE", 6))) return "Radiance HDR";
    if (size >= 4 && !memcmp(data, "DDS ", 4)) return "DDS";
    if (size >= 12 && (!memcmp(data, "\xabKTX 11\xbb\r\n\x1a\n", 12)
                      || !memcmp(data, "\xabKTX 20\xbb\r\n\x1a\n", 12))) return "KTX";
    return NULL;
}

bool image_file_read(const uint8_t *data, const size_t size, const char *name, tide_png_image *out, char why[256])
{
    *out = (tide_png_image){0};
    why[0] = '\0';
    if (!data || !size) return fail(why, "image file is empty; supported image formats are PNG, PSD, JPEG, TGA and BMP");
    image_format format = IMAGE_UNKNOWN;
    if (size >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8)) format = IMAGE_PNG;
    else if (size >= 4 && !memcmp(data, "8BPS", 4)) format = IMAGE_PSD;
    else if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) format = IMAGE_JPEG;
    else if (size >= 2 && data[0] == 'B' && data[1] == 'M') format = IMAGE_BMP;
    else {
        const char *unsupported = unsupported_magic(data, size);
        if (unsupported) return fail(why, "%s image import isn't supported; export PNG, PSD, JPEG, TGA or BMP", unsupported);
        if (suffix(name, ".png")) format = IMAGE_PNG;
        else if (suffix(name, ".psd") || suffix(name, ".psb")) format = IMAGE_PSD;
        else if (suffix(name, ".jpg") || suffix(name, ".jpeg")) format = IMAGE_JPEG;
        else if (suffix(name, ".bmp")) format = IMAGE_BMP;
        else if (suffix(name, ".tga")) format = IMAGE_TGA;
    }
    if (format == IMAGE_PNG) {
        if (size >= 24 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8) && !memcmp(data + 12, "IHDR", 4)
            && !dimensions(be32(data + 16), be32(data + 20), "PNG", why)) return false;
        char limit[TIDE_PNG_WHY];
        if (tide_png_decode(data, size, out, limit)) return true;
        if (limit[0]) return fail(why, "PNG image is refused: %s", limit);
        return fail(why, "PNG image has invalid or truncated data; save a valid PNG with at most 16384 pixels each way");
    }
    if (format == IMAGE_PSD) {
        char psd[256];
        if (psd_file_read(data, size, out, psd)) return true;
        return fail(why, "PSD image %s", psd);
    }
    if (format == IMAGE_UNKNOWN) return fail(why, "unrecognized image format; supported image formats are PNG, PSD, JPEG, TGA and BMP");
    if (size > TIDE_IMAGE_MAX_BYTES || size > INT_MAX) {
        char have[32];
        return fail(why, "image source is %s bytes, more than the %u MiB an image import takes (TIDE_IMAGE_MAX_BYTES)", tide_count_text(size, have), IMAGE_MIB);
    }
    uint32_t width = 0, height = 0;
    const char *label = format == IMAGE_JPEG ? "JPEG" : format == IMAGE_BMP ? "BMP" : "TGA";
    const bool valid = format == IMAGE_JPEG ? jpeg_header(data, size, &width, &height, why)
                     : format == IMAGE_BMP ? bmp_header(data, size, &width, &height, why)
                                          : tga_header(data, size, &width, &height, why);
    if (!valid) return false;
    const size_t count = (size_t)width * height;
    if (count > TIDE_IMAGE_MAX_BYTES / (sizeof(tide_color) + 4u))
        return fail(why, "%s image decodes to more than the %u MiB an image import takes (TIDE_IMAGE_MAX_BYTES); reduce its dimensions", label, IMAGE_MIB);
    image_memory = 0;
    image_limited = false;
    int x = 0, y = 0, channels = 0;
    stbi_uc *pixels = stbi_load_from_memory(data, (int)size, &x, &y, &channels, 4);
    if (!pixels) {
        if (image_limited) return fail(why, "%s image needs more than the %u MiB of decoder memory an image import takes (TIDE_IMAGE_MAX_BYTES); reduce its dimensions", label, IMAGE_MIB);
        return fail(why, "%s image has invalid or truncated pixels (%s)", label, stbi_failure_reason());
    }
    if (x != (int)width || y != (int)height || count * sizeof(tide_color) > TIDE_IMAGE_MAX_BYTES - image_memory) {
        image_free(pixels);
        return fail(why, "%s image needs more than the %u MiB of decoder memory an image import takes (TIDE_IMAGE_MAX_BYTES); reduce its dimensions", label, IMAGE_MIB);
    }
    tide_color *colors = malloc(count * sizeof *colors);
    if (!colors) { image_free(pixels); return fail(why, "%s image couldn't allocate its decoded pixels", label); }
    const bool reverse_x = format == IMAGE_TGA && (data[17] & 16u) != 0;
    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            const size_t from = ((size_t)row * width + (reverse_x ? width - 1u - col : col)) * 4u;
            colors[(size_t)row * width + col] = (tide_color){(float)pixels[from] / 255.0f, (float)pixels[from + 1u] / 255.0f,
                                                          (float)pixels[from + 2u] / 255.0f, (float)pixels[from + 3u] / 255.0f};
        }
    }
    image_free(pixels);
    *out = (tide_png_image){width, height, colors};
    return true;
}
