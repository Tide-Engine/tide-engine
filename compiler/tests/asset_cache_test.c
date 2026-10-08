// Same-size asset edits within one second must replace cached bytes and models.
// Set file times explicitly so this regression needs no sleeps or clock timing.
#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

#include "asset_files.h"
#include "folders.h"
#include "tide/sha256.h"
#include "tide_test.h"

static bool make_work(char path[4096])
{
#ifdef _WIN32
    char temp[4096];
    const DWORD n = GetTempPathA(sizeof temp, temp);
    if (!n || n >= sizeof temp || !GetTempFileNameA(temp, "tac", 0, path)) return false;
    if (!DeleteFileA(path) || !CreateDirectoryA(path, NULL)) return false;
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    return true;
#else
    strcpy(path, "/tmp/tide-asset-cache-XXXXXX");
    return mkdtemp(path) != NULL;
#endif
}

static void remove_work(const char *path)
{
#ifdef _WIN32
    TIDE_CHECK(RemoveDirectoryA(path));
#else
    TIDE_CHECK(rmdir(path) == 0);
#endif
}

static bool write_file(const char *path, const void *bytes, const size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(bytes, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

// Independently confirm the OS kept the requested subsecond time, even when
// folder_file_info is the old implementation that discards that precision.
static bool set_time(const char *path, const long nanos)
{
#ifdef _WIN32
    const uint64_t ticks = (11644473600ull + 1600000000ull) * 10000000ull + (uint64_t)nanos / 100u;
    const FILETIME time = {(DWORD)ticks, (DWORD)(ticks >> 32)};
    const HANDLE f = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    const bool set = SetFileTime(f, NULL, NULL, &time);
    CloseHandle(f);
    WIN32_FILE_ATTRIBUTE_DATA info;
    return set && GetFileAttributesExA(path, GetFileExInfoStandard, &info)
        && info.ftLastWriteTime.dwLowDateTime == time.dwLowDateTime
        && info.ftLastWriteTime.dwHighDateTime == time.dwHighDateTime;
#else
    const struct timespec times[2] = {{1600000000, nanos}, {1600000000, nanos}};
    struct stat info;
    if (utimensat(AT_FDCWD, path, times, 0) != 0 || stat(path, &info) != 0) return false;
#ifdef __APPLE__
    return info.st_mtimespec.tv_sec == times[1].tv_sec && info.st_mtimespec.tv_nsec == nanos;
#else
    return info.st_mtim.tv_sec == times[1].tv_sec && info.st_mtim.tv_nsec == nanos;
#endif
#endif
}

static asset_match *resolve(const char *declaring, const char *path, const asset_kind kind)
{
    asset_value *v = asset_resolve(declaring, str_from(path), kind, false, false, (loc){1, 1, 0}, NULL);
    return v && v->count == 1 ? &v->items[0] : NULL;
}

TIDE_TEST(asset_cache_refreshes_same_size_bytes_and_text_within_one_second)
{
    char work[4096], path[4160], declaring[4160];
    TIDE_REQUIRE(make_work(work));
    snprintf(path, sizeof path, "%s/text.txt", work);
    snprintf(declaring, sizeof declaring, "%s/game.tide", work);
    static const char before[] = "first", after[] = "\xc3\xa9two";
    for (int kind = ASSET_TEXT; kind <= ASSET_BYTES; kind++) {
        asset_files_forget();
        diag_reset();
        TIDE_REQUIRE(write_file(path, before, sizeof before - 1));
        TIDE_REQUIRE(set_time(path, 100000000));
        unsigned long long size_before, time_before, size_after, time_after;
        TIDE_REQUIRE(folder_file_info(path, &size_before, &time_before));
        asset_match *a = resolve(declaring, "text.txt", (asset_kind)kind);
        TIDE_REQUIRE(a && a->size == sizeof before - 1);
        TIDE_CHECK(memcmp(a->data, before, a->size) == 0);
        uint8_t hash_before[32];
        memcpy(hash_before, a->hash, sizeof hash_before);
        TIDE_REQUIRE(write_file(path, after, sizeof after - 1));
        TIDE_REQUIRE(set_time(path, 200000000));
        TIDE_REQUIRE(folder_file_info(path, &size_after, &time_after));
        TIDE_CHECK(size_after == size_before && time_after != time_before);
        a = resolve(declaring, "text.txt", (asset_kind)kind);
        TIDE_REQUIRE(a && a->size == sizeof after - 1);
        TIDE_CHECK(memcmp(a->data, after, a->size) == 0);
        TIDE_CHECK(memcmp(a->hash, hash_before, sizeof hash_before) != 0);
        TIDE_CHECK(diag_error_count() == 0);
    }
    asset_files_forget();
    arena_reset();
    TIDE_CHECK(remove(path) == 0);
    remove_work(work);
}

static const char model[] = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
    "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1}}]}],"
    "\"buffers\":[{\"uri\":\"actor.bin\",\"byteLength\":72}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}]}";

static void check_model_refresh(const asset_kind kind)
{
    char work[4096], path[4160], side[4160], declaring[4160];
    TIDE_REQUIRE(make_work(work));
    snprintf(path, sizeof path, "%s/actor.gltf", work);
    snprintf(side, sizeof side, "%s/actor.bin", work);
    snprintf(declaring, sizeof declaring, "%s/game.tide", work);
    float geometry[18] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    asset_files_forget();
    diag_reset();
    TIDE_REQUIRE(write_file(path, model, sizeof model - 1));
    TIDE_REQUIRE(write_file(side, geometry, sizeof geometry));
    TIDE_REQUIRE(set_time(side, 100000000));
    unsigned long long size_before, time_before, size_after, time_after, json_time, json_time_after;
    TIDE_REQUIRE(folder_file_info(side, &size_before, &time_before));
    TIDE_REQUIRE(folder_file_info(path, NULL, &json_time));
    asset_match *a = resolve(declaring, "actor.gltf", kind);
    TIDE_REQUIRE(a && a->mesh.vertices == 3 && a->side_count == 1);
    TIDE_CHECK(a->mesh.position[0] == 0.0f);
    uint8_t hash_before[32], source_before[32];
    memcpy(source_before, a->hash, sizeof source_before);
    if (kind == ASSET_SKINNED) tide_sha256_of(a->skinned, a->skinned_size, hash_before);
    else memcpy(hash_before, a->mesh.hash, sizeof hash_before);
    geometry[0] = 0.5f;
    TIDE_REQUIRE(write_file(side, geometry, sizeof geometry));
    TIDE_REQUIRE(set_time(side, 200000000));
    TIDE_REQUIRE(folder_file_info(side, &size_after, &time_after));
    TIDE_REQUIRE(folder_file_info(path, NULL, &json_time_after));
    TIDE_CHECK(size_after == size_before && time_after != time_before);
    TIDE_CHECK(json_time_after == json_time);
    a = resolve(declaring, "actor.gltf", kind);
    TIDE_REQUIRE(a && a->mesh.vertices == 3 && a->side_count == 1);
    TIDE_CHECK(a->mesh.position[0] == -0.5f);
    TIDE_CHECK(memcmp(source_before, a->hash, sizeof source_before) == 0); // JSON is unchanged.
    uint8_t hash_after[32];
    if (kind == ASSET_SKINNED) tide_sha256_of(a->skinned, a->skinned_size, hash_after);
    else memcpy(hash_after, a->mesh.hash, sizeof hash_after);
    TIDE_CHECK(memcmp(hash_before, hash_after, sizeof hash_before) != 0);
    TIDE_CHECK(diag_error_count() == 0);
    asset_files_forget();
    arena_reset();
    TIDE_CHECK(remove(side) == 0 && remove(path) == 0);
    remove_work(work);
}

TIDE_TEST(asset_cache_refreshes_external_mesh_buffer_within_one_second)
{
    check_model_refresh(ASSET_MESH);
}

TIDE_TEST(asset_cache_refreshes_external_skinned_buffer_within_one_second)
{
    check_model_refresh(ASSET_SKINNED);
}
