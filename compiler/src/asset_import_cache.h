#pragma once
#include "asset_cache.h"
#include "mesh_files.h"

typedef struct asset_cache_dependency {
    const char *path;
    uint8_t hash[32];
} asset_cache_dependency;
typedef bool (*asset_cache_check_dependency)(void *user, const char *path, const uint8_t hash[32]);

uint8_t *asset_import_cache_read(const char *folder, const uint8_t key[32], asset_cache_check_dependency check,
                               void *user, size_t *size);
void asset_import_cache_write(const char *folder, const uint8_t key[32], const asset_cache_dependency *deps,
                              int count, const void *data, size_t size);
uint8_t *asset_mesh_cache_encode(const mesh_file *mesh, size_t *size);
bool asset_mesh_cache_decode(const uint8_t *data, size_t size, mesh_file *mesh);
