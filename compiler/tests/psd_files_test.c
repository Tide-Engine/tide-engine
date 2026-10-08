// The build-time PSD reader, against specification-derived fixtures. Inputs
// cut or spoiled are allocated at exactly their given length so sanitizers
// find any read beyond it. End-to-end tests cover the pack and generated code.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psd_files.h"
#include "tide_test.h"

static uint8_t *fixture(const char *name, size_t *size)
{
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", PSD_FIXTURES, name) >= (int)sizeof path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    const long end = ftell(f);
    if (end <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    *size = (size_t)end;
    uint8_t *bytes = malloc(*size);
    if (!bytes || fread(bytes, 1u, *size, f) != *size) { free(bytes); fclose(f); return NULL; }
    fclose(f);
    return bytes;
}

static bool near(const float a, const float b)
{
    return a - b < 1e-6f && b - a < 1e-6f;
}

static bool color(const tide_color c, const int r, const int g, const int b, const int a, const int max)
{
    return near(c.r, (float)r / (float)max) && near(c.g, (float)g / (float)max)
        && near(c.b, (float)b / (float)max) && near(c.a, (float)a / (float)max);
}

TIDE_TEST(psd_rgb_samples_keep_rows_channels_and_all_sixteen_bits)
{
    static const char *const names[] = {"raw8.psd", "spot8.psd", "zip8.psd", "pred8.psd",
                                         "raw16.psd", "rle16.psd", "zip16.psd", "pred16.psd"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t size;
        uint8_t *bytes = fixture(names[i], &size);
        TIDE_REQUIRE(bytes != NULL);
        tide_png_image image = {0};
        char why[256];
        const bool ok = psd_file_read(bytes, size, &image, why);
        if (!ok) printf("    %s: %s\n", names[i], why);
        free(bytes);
        TIDE_REQUIRE(ok);
        TIDE_CHECK(image.width == 2 && image.height == 2);
        if (i < 4) {
            TIDE_CHECK(color(image.pixels[0], 255, 0, 0, 255, 255));
            TIDE_CHECK(color(image.pixels[1], 0, 255, 0, 255, 255));
            TIDE_CHECK(color(image.pixels[2], 0, 0, 255, 255, 255));
            TIDE_CHECK(color(image.pixels[3], 51, 102, 153, 255, 255));
        } else {
            TIDE_CHECK(color(image.pixels[0], 0, 65535, 1, 65535, 65535));
            TIDE_CHECK(color(image.pixels[1], 1, 32768, 65534, 65535, 65535));
            TIDE_CHECK(color(image.pixels[2], 12345, 45678, 20000, 65535, 65535));
            TIDE_CHECK(color(image.pixels[3], 65535, 0, 32767, 65535, 65535));
            TIDE_CHECK(image.pixels[1].r == 1.0f / 65535.0f);
        }
        free(image.pixels);
    }
}

TIDE_TEST(psd_composite_transparency_recovers_straight_colors_from_white_matte)
{
    static const char *const names[] = {"rle8.psd", "lr16.psd", "cases/tag8.psd", "cases/tag16.psd", "cases/no_global_mask.psd",
                                         "cases/no_global_tag.psd", "cases/no_global_tag64.psd"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t size;
        uint8_t *bytes = fixture(names[i], &size);
        TIDE_REQUIRE(bytes != NULL);
        tide_png_image image = {0};
        char why[256];
        const bool ok = psd_file_read(bytes, size, &image, why);
        if (!ok) printf("    %s: %s\n", names[i], why);
        free(bytes);
        TIDE_REQUIRE(ok);
        TIDE_CHECK(image.width == 3 && image.height == 2);
        TIDE_CHECK(color(image.pixels[0], 255, 0, 0, 85, 255));
        TIDE_CHECK(color(image.pixels[1], 51, 102, 153, 85, 255));
        TIDE_CHECK(color(image.pixels[2], 0, 0, 0, 0, 255));
        TIDE_CHECK(color(image.pixels[3], 0, 255, 0, 170, 255));
        TIDE_CHECK(color(image.pixels[4], 0, 0, 255, 170, 255));
        TIDE_CHECK(color(image.pixels[5], 51, 102, 153, 170, 255));
        free(image.pixels);
    }
}

TIDE_TEST(psd_refuses_unsupported_and_incomplete_composites)
{
    static const char *const names[] = {"bad/cmyk.psd", "bad/large.psb", "bad/no_composite.psd",
                                         "bad/cut.psd", "bad/rle_cut.psd", "bad/absent.psd", "bad/rle_long_run.psd",
                                         "bad/rle_short_literal.psd", "bad/rle_missing_value.psd", "bad/rle_lengths.psd",
                                         "bad/zip_checksum.psd", "bad/zip_cut.psd", "bad/zip_excess.psd",
                                         "bad/partial_global_mask1.psd", "bad/partial_global_mask2.psd", "bad/partial_global_mask3.psd"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t size;
        uint8_t *bytes = fixture(names[i], &size);
        TIDE_REQUIRE(bytes != NULL);
        tide_png_image image = {0};
        char why[256];
        TIDE_CHECK(!psd_file_read(bytes, size, &image, why));
        TIDE_CHECK(image.pixels == NULL);
        TIDE_CHECK(why[0] != 0);
        if (i == 2) TIDE_CHECK(strstr(why, "Maximize Compatibility") != NULL);
        free(image.pixels);
        free(bytes);
    }
}

TIDE_TEST(psd_files_cut_or_spoiled_are_never_read_past_their_bytes)
{
    static const char *const names[] = {"raw8.psd", "rle8.psd", "pred16.psd", "lr16.psd", "cases/no_global_mask.psd",
                                         "cases/no_global_tag.psd", "cases/no_global_tag64.psd"};
    for (size_t n = 0; n < sizeof names / sizeof names[0]; n++) {
        size_t size;
        uint8_t *bytes = fixture(names[n], &size);
        TIDE_REQUIRE(bytes != NULL);
        for (size_t cut = 0; cut < size; cut++) {
            uint8_t *shorter = malloc(cut ? cut : 1u);
            TIDE_REQUIRE(shorter != NULL);
            memcpy(shorter, bytes, cut);
            tide_png_image image = {0};
            char why[256];
            const bool ok = psd_file_read(shorter, cut, &image, why);
            if (ok) printf("    %s unexpectedly decoded at %zu of %zu bytes\n", names[n], cut, size);
            TIDE_CHECK(!ok && image.pixels == NULL);
            free(image.pixels);
            free(shorter);
        }
        static const uint8_t masks[] = {0x01, 0x80, 0xff};
        for (size_t at = 0; at < size; at++) {
            for (size_t m = 0; m < sizeof masks / sizeof masks[0]; m++) {
                uint8_t *changed = malloc(size);
                TIDE_REQUIRE(changed != NULL);
                memcpy(changed, bytes, size);
                changed[at] ^= masks[m];
                tide_png_image image = {0};
                char why[256];
                const bool ok = psd_file_read(changed, size, &image, why);
                if (ok) TIDE_CHECK(image.pixels != NULL && image.width != 0 && image.height != 0);
                else TIDE_CHECK(image.pixels == NULL);
                free(image.pixels);
                free(changed);
            }
        }
        free(bytes);
    }
}

TIDE_TEST(psd_header_dimensions_and_section_lengths_are_bounded)
{
    size_t size;
    uint8_t *bytes = fixture("raw8.psd", &size);
    TIDE_REQUIRE(bytes != NULL);
    const size_t offsets[] = {14u, 18u, 26u, 30u}; // Height, width, color data, resources.
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        uint8_t *changed = malloc(size);
        TIDE_REQUIRE(changed != NULL);
        memcpy(changed, bytes, size);
        memset(changed + offsets[i], 255, 4u);
        tide_png_image image = {0};
        char why[256];
        TIDE_CHECK(!psd_file_read(changed, size, &image, why) && image.pixels == NULL);
        free(image.pixels);
        free(changed);
    }
    free(bytes);
}
