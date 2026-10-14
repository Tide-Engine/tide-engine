#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_files.h"
#include "tide_test.h"

static uint8_t *fixture(const char *name, size_t *size)
{
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", IMAGE_FIXTURES, name) >= (int)sizeof path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    const long end = ftell(f);
    if (end < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    *size = (size_t)end;
    uint8_t *data = malloc(*size ? *size : 1u);
    if (!data || fread(data, 1, *size, f) != *size) { free(data); fclose(f); return NULL; }
    fclose(f);
    return data;
}

static bool close_channel(const float actual, const uint8_t expected, const unsigned int tolerance)
{
    const float difference = actual * 255.0f - (float)expected;
    return difference >= -(float)tolerance && difference <= (float)tolerance;
}

static bool pixels_are(const tide_png_image *image, const uint8_t *rgba, const unsigned int tolerance)
{
    for (size_t i = 0; i < (size_t)image->width * image->height; i++) {
        const tide_color c = image->pixels[i];
        if (!close_channel(c.r, rgba[i * 4u], tolerance) || !close_channel(c.g, rgba[i * 4u + 1u], tolerance)
            || !close_channel(c.b, rgba[i * 4u + 2u], tolerance) || !close_channel(c.a, rgba[i * 4u + 3u], 0)) return false;
    }
    return true;
}

TIDE_TEST(image_jpeg_baseline_progressive_gray_and_cmyk_match_independent_pixels)
{
    static const char *const names[] = {"baseline", "progressive", "gray", "cmyk"};
    tide_png_image baseline = {0};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s.jpg", names[i]);
        size_t size;
        uint8_t *data = fixture(name, &size);
        TIDE_REQUIRE(data != NULL);
        snprintf(name, sizeof name, "%s.rgba", names[i]);
        size_t expected_size;
        uint8_t *expected = fixture(name, &expected_size);
        TIDE_REQUIRE(expected != NULL && expected_size == 8u * 6u * 4u);
        tide_png_image image;
        char why[256];
        const bool ok = image_file_read(data, size, "art/image.JPEG", &image, why);
        if (!ok) printf("    %s: %s\n", names[i], why);
        TIDE_REQUIRE(ok);
        TIDE_CHECK(image.width == 8 && image.height == 6 && !why[0]);
        TIDE_CHECK(pixels_are(&image, expected, 2)); // Independent libjpeg may round a channel differently.
        if (i == 0) baseline = image;
        else {
            if (i == 1) TIDE_CHECK(!memcmp(baseline.pixels, image.pixels, 8u * 6u * sizeof(tide_color)));
            free(image.pixels);
        }
        free(data);
        free(expected);
    }
    free(baseline.pixels);
}

TIDE_TEST(image_tga_raw_and_rle_keep_all_four_origins_and_alpha)
{
    static const char *const names[] = {"raw_top_left.tga", "raw_top_right.tga", "raw_bottom_left.tga", "raw_bottom_right.tga",
                                       "rle_top_left.tga", "rle_top_right.tga", "rle_bottom_left.tga", "rle_bottom_right.tga"};
    size_t expected_size;
    uint8_t *expected = fixture("colors.rgba", &expected_size);
    TIDE_REQUIRE(expected && expected_size == 24);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t size;
        uint8_t *data = fixture(names[i], &size);
        TIDE_REQUIRE(data != NULL);
        tide_png_image image;
        char why[256];
        TIDE_REQUIRE(image_file_read(data, size, names[i], &image, why));
        TIDE_CHECK(image.width == 3 && image.height == 2 && pixels_are(&image, expected, 0));
        free(image.pixels);
        free(data);
    }
    free(expected);
}

TIDE_TEST(image_tga_palette_origin_gray_and_packed_rgb_are_explicit)
{
    static const char *const names[] = {"indexed.tga", "indexed_rle.tga", "gray8.tga", "gray_alpha16.tga", "rgb16.tga", "rgb24.tga"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t size;
        uint8_t *data = fixture(names[i], &size);
        TIDE_REQUIRE(data != NULL);
        tide_png_image image;
        char why[256];
        TIDE_REQUIRE(image_file_read(data, size, names[i], &image, why));
        TIDE_CHECK(image.width == 3 && image.height == 2);
        TIDE_CHECK(image.pixels[0].r == 1.0f && image.pixels[0].a == 1.0f);
        if (i < 2) TIDE_CHECK(image.pixels[1].g == 1.0f && image.pixels[1].a == 128.0f / 255.0f);
        if (i == 2 || i == 3) TIDE_CHECK(image.pixels[0].g == 1.0f && image.pixels[0].b == 1.0f);
        if (i == 3) TIDE_CHECK(image.pixels[1].a == 128.0f / 255.0f);
        free(image.pixels);
        free(data);
    }
}

TIDE_TEST(image_bmp_rows_palettes_bitfields_and_alpha_are_preserved)
{
    static const char *const names[] = {"rgb24.bmp", "rgb24_top.bmp", "rgb16_bitfields.bmp", "rgba32_v56.bmp",
                                       "rgba32_v108.bmp", "rgba32_v124.bmp", "indexed1.bmp", "indexed4.bmp", "indexed8.bmp"};
    size_t expected_size;
    uint8_t *expected = fixture("colors.rgba", &expected_size);
    TIDE_REQUIRE(expected && expected_size == 24);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t size;
        uint8_t *data = fixture(names[i], &size);
        TIDE_REQUIRE(data != NULL);
        tide_png_image image;
        char why[256];
        const bool ok = image_file_read(data, size, names[i], &image, why);
        if (!ok) printf("    %s: %s\n", names[i], why);
        TIDE_REQUIRE(ok);
        TIDE_CHECK(image.width == 3 && image.height == 2);
        if (i < 2) {
            for (size_t p = 0; p < 6; p++) {
                const tide_color c = image.pixels[p];
                TIDE_CHECK(close_channel(c.r, expected[p * 4], 0) && close_channel(c.g, expected[p * 4 + 1], 0)
                           && close_channel(c.b, expected[p * 4 + 2], 0) && c.a == 1.0f);
            }
        } else if (i == 2) TIDE_CHECK(image.pixels[0].r == 1.0f && image.pixels[3].b == 1.0f);
        else if (i < 6) TIDE_CHECK(pixels_are(&image, expected, 0));
        else TIDE_CHECK(image.pixels[0].r == 1.0f && image.pixels[1].g == 1.0f && image.pixels[0].a == 1.0f);
        free(image.pixels);
        free(data);
    }
    free(expected);
}

TIDE_TEST(image_magic_precedes_names_and_tga_url_hints_use_only_the_path)
{
    static const struct { const char *fixture, *name; } good[] = {
        {"baseline.jpg", "misnamed.psd"}, {"rgb24.bmp", "misnamed.jpg"}, {"reference.png", "misnamed.tga"},
        {"baseline.jpg", "https://example.test/image.jpg?v=2#preview"},
        {"raw_top_left.tga", "https://example.test/image.TGA?v=2#preview"}
    };
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        size_t size;
        uint8_t *data = fixture(good[i].fixture, &size);
        TIDE_REQUIRE(data != NULL);
        tide_png_image image;
        char why[256];
        TIDE_CHECK(image_file_read(data, size, good[i].name, &image, why));
        free(image.pixels);
        free(data);
    }
    size_t size;
    uint8_t *data = fixture("raw_top_left.tga", &size);
    TIDE_REQUIRE(data != NULL);
    tide_png_image image;
    char why[256];
    TIDE_CHECK(!image_file_read(data, size, "https://example.test/image.bin?name=.tga", &image, why));
    TIDE_CHECK(!image_file_read(data, size, "https://example.test?name=/image.tga", &image, why));
    TIDE_CHECK(!image_file_read(data, size, "https://example.test#name=/image.tga", &image, why));
    TIDE_CHECK(!image_file_read(data, size, "local.tga#fragment", &image, why));
    free(data);
    static const uint8_t webp[] = "RIFF\x04\0\0\0WEBP";
    TIDE_CHECK(!image_file_read(webp, sizeof webp - 1u, "wrong.tga", &image, why));
    TIDE_CHECK(strstr(why, "WebP image import isn't supported") != NULL);
}

TIDE_TEST(image_failures_distinguish_unsupported_variants_corruption_and_limits)
{
    static const struct { const char *name, *reason; } cases[] = {
        {"bad/lossless.jpg", "JPEG encoding isn't supported"}, {"bad/twelve_bit.jpg", "isn't 8 bits"},
        {"bad/cut.jpg", "truncated"}, {"bad/rle.bmp", "BMP compression isn't supported"},
        {"bad/cut.bmp", "truncated pixel rows"}, {"bad/cut.tga", "truncated pixel data"},
        {"bad/overrun.tga", "extends past the image"}, {"bad/alpha16.tga", "16-bit pixel alpha isn't supported"},
        {"bad/index.tga", "outside its palette"}, {"bad/unsupported.webp", "WebP image import isn't supported"},
        {"bad/unknown.bin", "unrecognized image format"}, {"bad/broken.png", "PNG image has invalid or truncated data"},
        {"bad/large.png", "PNG image is 20000 by 6 pixels"}
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        size_t size;
        uint8_t *data = fixture(cases[i].name, &size);
        TIDE_REQUIRE(data != NULL);
        tide_png_image image = {7, 9, (tide_color *)(uintptr_t)1};
        char why[256];
        const bool ok = image_file_read(data, size, cases[i].name, &image, why);
        if (ok || !strstr(why, cases[i].reason)) printf("    %s: %s\n", cases[i].name, why);
        TIDE_CHECK(!ok && !image.pixels && !image.width && !image.height && strstr(why, cases[i].reason));
        free(data);
    }
    tide_png_image image;
    char why[256];
    TIDE_CHECK(!image_file_read(NULL, 0, "empty.jpg", &image, why) && !image.pixels && strstr(why, "empty"));
    const uint8_t fake[12] = {0xff, 0xd8, 0xff}; // Enough bytes for format sniffing; rejected before reading the payload.
    TIDE_CHECK(!image_file_read(fake, (size_t)INT32_MAX + 1u, "huge.jpg", &image, why));
    TIDE_CHECK(strstr(why, "512 MiB import limit") != NULL);

    size_t size;
    uint8_t *jpeg = fixture("baseline.jpg", &size);
    TIDE_REQUIRE(jpeg != NULL);
    size_t frame = 0;
    for (size_t i = 0; i + 8u < size; i++) if (jpeg[i] == 0xff && jpeg[i + 1u] == 0xc0) { frame = i; break; }
    TIDE_REQUIRE(frame != 0);
    jpeg[frame + 5u] = jpeg[frame + 7u] = 0x40;
    jpeg[frame + 6u] = jpeg[frame + 8u] = 0;
    TIDE_CHECK(!image_file_read(jpeg, size, "large.jpg", &image, why) && !image.pixels);
    TIDE_CHECK(strstr(why, "512 MiB decoded-image import limit") != NULL);
    jpeg[frame + 8u] = 1;
    TIDE_CHECK(!image_file_read(jpeg, size, "large.jpg", &image, why) && strstr(why, "16385"));
    free(jpeg);
}

TIDE_TEST(image_decoded_pixel_bits_are_the_same_on_every_build_host)
{
    static const char *const names[] = {"baseline.jpg", "progressive.jpg", "gray.jpg", "cmyk.jpg", "rle_top_right.tga",
                                       "indexed_rle.tga", "gray_alpha16.tga", "rgb16.tga", "rgb24_top.bmp", "rgba32_v56.bmp"};
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t n = 0; n < sizeof names / sizeof names[0]; n++) {
        size_t size;
        uint8_t *data = fixture(names[n], &size);
        TIDE_REQUIRE(data != NULL);
        tide_png_image image;
        char why[256];
        TIDE_REQUIRE(image_file_read(data, size, names[n], &image, why));
        for (size_t i = 0; i < (size_t)image.width * image.height; i++) {
            const tide_color c = image.pixels[i];
            const float channels[4] = {c.r, c.g, c.b, c.a};
            for (unsigned int k = 0; k < 4; k++) {
                uint32_t bits;
                memcpy(&bits, &channels[k], sizeof bits);
                for (unsigned int b = 0; b < 4; b++) hash = (hash ^ ((bits >> (b * 8u)) & 255u)) * UINT64_C(1099511628211);
            }
        }
        free(image.pixels);
        free(data);
    }
    if (hash != UINT64_C(0x3a61f99ef549dae6)) printf("    image pixel hash: %016llx\n", (unsigned long long)hash);
    TIDE_CHECK(hash == UINT64_C(0x3a61f99ef549dae6));
}

TIDE_TEST(image_exact_sized_cuts_and_corruption_never_read_past_supplied_bytes)
{
    static const char *const names[] = {"baseline.jpg", "progressive.jpg", "cmyk.jpg", "raw_top_left.tga", "rle_top_right.tga",
                                       "indexed_rle.tga", "rgb24.bmp", "indexed4.bmp", "rgba32_v56.bmp"};
    for (size_t n = 0; n < sizeof names / sizeof names[0]; n++) {
        size_t size;
        uint8_t *data = fixture(names[n], &size);
        TIDE_REQUIRE(data != NULL);
        for (size_t cut = 0; cut < size; cut++) {
            uint8_t *shorter = malloc(cut ? cut : 1u);
            TIDE_REQUIRE(shorter != NULL);
            memcpy(shorter, data, cut);
            tide_png_image image;
            char why[256];
            const bool ok = image_file_read(shorter, cut, names[n], &image, why);
            TIDE_CHECK(!ok && !image.pixels && why[0]);
            free(image.pixels);
            free(shorter);
        }
        static const uint8_t masks[] = {1, 128, 255};
        for (size_t at = 0; at < size; at++) {
            for (size_t m = 0; m < sizeof masks; m++) {
                uint8_t *changed = malloc(size);
                TIDE_REQUIRE(changed != NULL);
                memcpy(changed, data, size);
                changed[at] ^= masks[m];
                tide_png_image image;
                char why[256];
                const bool ok = image_file_read(changed, size, names[n], &image, why);
                if (ok) TIDE_CHECK(image.pixels && image.width && image.height);
                else TIDE_CHECK(!image.pixels && why[0]);
                free(image.pixels);
                free(changed);
            }
        }
        free(data);
    }
}
