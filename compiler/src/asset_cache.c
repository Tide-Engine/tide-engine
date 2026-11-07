#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include "asset_cache.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "folders.h"
#include "tide/sha256.h"
#include "version.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

void asset_cache_key(uint8_t out[32], const char *kind, const uint8_t source[32], const void *variant, const size_t size)
{
    tide_sha256 h;
    tide_sha256_init(&h);
    const uint32_t format[] = {1, 0x01020304, sizeof(float)};
    tide_sha256_add(&h, format, sizeof format);
    const char *identity = tide_simulation_runtime();
    tide_sha256_add(&h, identity, strlen(identity) + 1);
    static const char vendors[] = TIDE_ASSET_CACHE_VENDORS;
    tide_sha256_add(&h, vendors, sizeof vendors);
    tide_sha256_add(&h, kind, strlen(kind) + 1);
    tide_sha256_add(&h, source, 32);
    if (size) tide_sha256_add(&h, variant, size);
    tide_sha256_end(&h, out);
}

static bool cache_path(char path[4096], const char *folder, const uint8_t key[32])
{
    if (!folder || !*folder) return false;
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", key[i]);
    return snprintf(path, 4096, "%s/.tide/assets/imports/%s.bin", folder, hex) < 4096;
}

bool asset_cache_has(const char *folder, const uint8_t key[32])
{
    char path[4096];
    unsigned long long bytes;
    return cache_path(path, folder, key) && folder_file_info(path, &bytes, NULL) && bytes > 64
        && bytes - 64 <= ASSET_CACHE_MAX_BYTES;
}

uint8_t *asset_cache_read(const char *folder, const uint8_t key[32], size_t *size)
{
    char path[4096];
    unsigned long long bytes;
    if (!cache_path(path, folder, key) || !folder_file_info(path, &bytes, NULL)
        || bytes <= 64 || bytes - 64 > ASSET_CACHE_MAX_BYTES) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    const size_t count = (size_t)(bytes - 64);
    uint8_t *data = malloc(count), header[64], digest[32];
    bool ok = data && fread(header, 1, sizeof header, file) == sizeof header
        && !memcmp(header, key, 32) && fread(data, 1, count, file) == count && fgetc(file) == EOF;
    fclose(file);
    if (ok) {
        tide_sha256_of(data, count, digest);
        ok = !memcmp(digest, header + 32, 32);
    }
    if (!ok) { free(data); return NULL; }
    *size = count;
    return data;
}

void asset_cache_write(const char *folder, const uint8_t key[32], const void *data, const size_t size)
{
    char path[4096], directory[4096], fresh[4160];
    if (!size || size > ASSET_CACHE_MAX_BYTES || !cache_path(path, folder, key)) return;
    snprintf(directory, sizeof directory, "%s/.tide/assets/imports", folder);
    if (!folder_make(directory)) return;
    static atomic_uint serial; // Images are written from several threads (codegen.c, want_textures)
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = (unsigned long)getpid();
#endif
    snprintf(fresh, sizeof fresh, "%s.%lu-%u.new", path, pid, atomic_fetch_add(&serial, 1u) + 1u);
    FILE *file = fopen(fresh, "wb");
    if (!file) return;
    uint8_t digest[32];
    tide_sha256_of(data, size, digest);
    bool ok = fwrite(key, 1, 32, file) == 32 && fwrite(digest, 1, 32, file) == 32
        && fwrite(data, 1, size, file) == size;
    ok = fclose(file) == 0 && ok;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(fresh, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
        ok = rename(fresh, path) == 0;
#endif
    }
    if (!ok) remove(fresh);
}
