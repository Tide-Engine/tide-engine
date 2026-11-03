#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_import_cache.h"
#include "asset_files.h"
#include "texture_cache.h"
#include "folders.h"
#include "tide/sha256.h"
#include "tide_test.h"

static bool put(const char *path, const void *bytes, const size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(bytes, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

TIDE_TEST(asset_disk_cache_rejects_wrong_keys_damage_and_every_truncation)
{
    uint8_t source[32] = {1}, key[32], other[32], payload[128];
    memset(payload, 0xA5, sizeof payload);
    asset_cache_key(key, "test", source, NULL, 0);
    asset_cache_key(other, "another-kind", source, NULL, 0);
    TIDE_CHECK(memcmp(key, other, 32) != 0);
    size_t size = 999;
    TIDE_CHECK(!asset_cache_read(NULL, key, &size) && size == 999);
    asset_cache_write(ASSET_FILES_WORK, key, payload, sizeof payload);
    uint8_t *got = asset_cache_read(ASSET_FILES_WORK, key, &size);
    TIDE_REQUIRE(got && size == sizeof payload);
    TIDE_CHECK(memcmp(got, payload, size) == 0);
    free(got);
    TIDE_CHECK(!asset_cache_read(ASSET_FILES_WORK, other, &size));
    char path[2048], hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", key[i]);
    snprintf(path, sizeof path, "%s/.tide/assets/imports/%s.bin", ASSET_FILES_WORK, hex);
    uint8_t encoded[64 + sizeof payload];
    memcpy(encoded, key, 32);
    tide_sha256_of(payload, sizeof payload, encoded + 32);
    memcpy(encoded + 64, payload, sizeof payload);
    for (size_t n = 0; n < sizeof encoded; n++) {
        TIDE_REQUIRE(put(path, encoded, n));
        got = asset_cache_read(ASSET_FILES_WORK, key, &size);
        TIDE_CHECK(!got);
        free(got);
    }
    for (size_t i = 0; i < sizeof encoded; i++) {
        encoded[i] ^= 1;
        TIDE_REQUIRE(put(path, encoded, sizeof encoded));
        got = asset_cache_read(ASSET_FILES_WORK, key, &size);
        TIDE_CHECK(!got);
        free(got);
        encoded[i] ^= 1;
    }
    asset_cache_write(ASSET_FILES_WORK, key, payload, sizeof payload);
    got = asset_cache_read(ASSET_FILES_WORK, key, &size);
    TIDE_CHECK(got && size == sizeof payload && !memcmp(got, payload, size));
    free(got);
    remove(path);
}

TIDE_TEST(asset_mesh_disk_representation_preserves_streams_and_checks_bounds)
{
    static const char obj[] = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    mesh_file original = {0}, copy = {0};
    char why[256], note[256];
    TIDE_REQUIRE(mesh_file_read((const uint8_t *)obj, sizeof obj - 1, "triangle.obj", NULL, NULL, &original, why, note));
    size_t size;
    uint8_t *bytes = asset_mesh_cache_encode(&original, &size);
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(asset_mesh_cache_decode(bytes, size, &copy));
    TIDE_CHECK(copy.vertices == original.vertices && copy.indices == original.indices);
    TIDE_CHECK(!memcmp(copy.hash, original.hash, 32));
    TIDE_CHECK(!memcmp(copy.position, original.position, original.vertices * 12u));
    TIDE_CHECK(!memcmp(copy.normal, original.normal, original.vertices * 12u));
    TIDE_CHECK(!memcmp(copy.uv, original.uv, original.vertices * 8u));
    TIDE_CHECK(!memcmp(copy.uv2, original.uv2, original.vertices * 8u));
    TIDE_CHECK(!memcmp(copy.color, original.color, original.vertices * 16u));
    TIDE_CHECK(!memcmp(copy.tangent, original.tangent, original.vertices * 16u));
    TIDE_CHECK(!memcmp(copy.index, original.index, original.indices * 4u));
    TIDE_CHECK(!memcmp(copy.sources, original.sources, original.vertices));
    mesh_file_free(&copy);
    for (size_t n = 0; n < size; n++) TIDE_CHECK(!asset_mesh_cache_decode(bytes, n, &copy));
    const uint32_t invalid = UINT32_MAX;
    uint32_t saved;
    memcpy(&saved, bytes, 4); memcpy(bytes, &invalid, 4);
    TIDE_CHECK(!asset_mesh_cache_decode(bytes, size, &copy));
    memcpy(bytes, &saved, 4);
    uint32_t mask;
    memcpy(&mask, bytes + 12, 4);
    const uint32_t widths[] = {12, 12, 8, 8, 16, 16};
    size_t index_at = 52;
    for (int i = 0; i < 6; i++) index_at += (size_t)widths[i] * (mask & (1u << (i + 8)) ? 1u : original.vertices);
    memcpy(bytes + index_at, &invalid, 4);
    TIDE_CHECK(!asset_mesh_cache_decode(bytes, size, &copy));
    free(bytes);
    mesh_file_free(&original);
}

TIDE_TEST(prepared_textures_preserve_direct_importer_pixel_contract)
{
    static const uint8_t png[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,8,0,0,0,6,8,6,0,0,0,254,5,223,251,
        0,0,0,90,73,68,65,84,120,218,21,202,33,1,196,48,12,0,192,40,25,158,136,138,24,14,174,136,
        40,41,158,136,136,24,174,161,255,43,56,118,17,49,127,23,131,164,88,52,59,102,68,220,2,131,
        164,88,52,251,62,225,17,24,36,197,162,217,207,9,37,48,72,138,69,179,235,132,87,96,144,20,
        139,102,191,39,124,2,131,164,88,52,251,155,241,7,56,152,99,7,241,114,109,244,0,0,0,0,73,69,78,68,174,66,96,130};
    char source[2048], image[2048];
    snprintf(source, sizeof source, "%s/preview.tide", ASSET_FILES_WORK);
    snprintf(image, sizeof image, "%s/preview.png", ASSET_FILES_WORK);
    TIDE_REQUIRE(folder_make(ASSET_FILES_WORK) && put(image, png, sizeof png));
    uint8_t hash[32], cache[32], rgba[8 * 6 * 4] = {0};
    tide_sha256_of(png, sizeof png, hash);
    texture_cache_key(cache, hash, "preview.png", false, true);
    const texture_cache_view view = {.width = 8, .height = 6, .rgba = rgba};
    texture_cache_write(ASSET_FILES_WORK, cache, &view);
    asset_files_forget();
    asset_files_use_build_cache(true);
    asset_value *prepared = asset_resolve(source, str_from("preview.png"), ASSET_TEXTURE, false, false, (loc){0}, ASSET_FILES_WORK);
    TIDE_REQUIRE(prepared && prepared->items[0].prepared_texture && !prepared->items[0].image.pixels);
    asset_files_use_build_cache(false);
    asset_value *ordinary = asset_resolve(source, str_from("preview.png"), ASSET_TEXTURE, false, false, (loc){0}, ASSET_FILES_WORK);
    TIDE_REQUIRE(ordinary && ordinary->items[0].image.pixels);
    TIDE_CHECK(ordinary->items[0].image.width == 8 && ordinary->items[0].image.height == 6);
    asset_files_forget();
}
