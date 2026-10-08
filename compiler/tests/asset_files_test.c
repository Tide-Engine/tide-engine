// Full mesh imports keep side-buffer dependencies independently of the
// editor's JSON-only model metadata cache.
#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#include "asset_files.h"
#include "folders.h"
#include "tide_test.h"

#ifndef ASSET_FILES_WORK
#define ASSET_FILES_WORK "."
#endif

static bool write_bytes(const char *path, const void *bytes, const size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool written = fwrite(bytes, 1, size, f) == size;
    return fclose(f) == 0 && written;
}

static bool pin_time(const char *path)
{
#ifdef _WIN32
    struct _utimbuf time = {1700000000, 1700000000};
    return _utime(path, &time) == 0;
#else
    const struct utimbuf time = {1700000000, 1700000000};
    return utime(path, &time) == 0;
#endif
}

TIDE_TEST(asset_mesh_side_notifications_override_unchanged_metadata)
{
    static const char json[] = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1},\"indices\":2}]}],"
        "\"buffers\":[{\"uri\":\"shape.bin\",\"byteLength\":80}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":6}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}]}";
    struct {
        float positions[9], normals[9];
        uint16_t indices[4];
    } triangle = {{0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 0, 1, 0, 0, 1, 0, 0, 1}, {0, 1, 2, 0}};
    _Static_assert(sizeof triangle == 80u, "the fixture's buffer views name these bytes");
    char source[1024], model[1024], side[1024];
    snprintf(source, sizeof source, "%s/main.tide", ASSET_FILES_WORK);
    snprintf(model, sizeof model, "%s/shape.gltf", ASSET_FILES_WORK);
    snprintf(side, sizeof side, "%s/shape.bin", ASSET_FILES_WORK);
    TIDE_REQUIRE(folder_make(ASSET_FILES_WORK));
    TIDE_REQUIRE(write_bytes(model, json, sizeof json - 1u));
    static const asset_kind kinds[] = {ASSET_MESH, ASSET_SKINNED};
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        // Separate cache lifetimes ensure the skinned case has no static-mesh
        // side list that could accidentally make its missing invalidation pass.
        asset_files_forget();
        diag_reset();
        triangle.positions[3] = 1.0f;
        TIDE_REQUIRE(write_bytes(side, &triangle, sizeof triangle) && pin_time(side));
        unsigned long long bytes, modified, changed_bytes, changed_time;
        TIDE_REQUIRE(folder_file_info(side, &bytes, &modified));
        asset_value *value = asset_resolve(source, str_from("shape.gltf"), kinds[k], false, false, (loc){0}, NULL);
        TIDE_REQUIRE(value && value->count == 1 && value->items[0].side_count == 1);
        TIDE_CHECK(value->items[0].mesh.vertices == 3 && value->items[0].mesh.position[3] == -1.0f);
        uint8_t original[32];
        memcpy(original, value->items[0].mesh.hash, sizeof original);
        triangle.positions[3] = 2.0f;
        TIDE_REQUIRE(write_bytes(side, &triangle, sizeof triangle) && pin_time(side));
        TIDE_REQUIRE(folder_file_info(side, &changed_bytes, &changed_time));
        TIDE_CHECK(bytes == changed_bytes && modified == changed_time);
        value = asset_resolve(source, str_from("shape.gltf"), kinds[k], false, false, (loc){0}, NULL);
        TIDE_REQUIRE(value && value->count == 1);
        TIDE_CHECK(memcmp(value->items[0].mesh.hash, original, sizeof original) == 0);
        asset_files_changed(side);
        value = asset_resolve(source, str_from("shape.gltf"), kinds[k], false, false, (loc){0}, NULL);
        TIDE_REQUIRE(value && value->count == 1);
        TIDE_CHECK(value->items[0].mesh.position[3] == -2.0f);
        TIDE_CHECK(memcmp(value->items[0].mesh.hash, original, sizeof original) != 0);
        triangle.positions[3] = 1.0f;
        TIDE_REQUIRE(write_bytes(side, &triangle, sizeof triangle) && pin_time(side));
        asset_files_changed(side);
        value = asset_resolve(source, str_from("shape.gltf"), kinds[k], false, false, (loc){0}, NULL);
        TIDE_REQUIRE(value && value->count == 1);
        TIDE_CHECK(value->items[0].mesh.position[3] == -1.0f);
        TIDE_CHECK(memcmp(value->items[0].mesh.hash, original, sizeof original) == 0);
    }
    asset_files_forget();
    arena_reset();
    diag_reset();
    remove(side);
    remove(model);
}
