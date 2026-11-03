#include "asset_import_cache.h"

#include <stdlib.h>
#include <string.h>

static uint32_t word(const uint8_t *p)
{
    uint32_t value;
    memcpy(&value, p, sizeof value);
    return value;
}

uint8_t *asset_import_cache_read(const char *folder, const uint8_t key[32], asset_cache_check_dependency check,
                               void *user, size_t *size)
{
    size_t length;
    uint8_t *bytes = asset_cache_read(folder, key, &length);
    if (!bytes) return NULL;
    size_t at = 4;
    bool ok = length >= 4 && word(bytes) <= 65536;
    const uint32_t count = ok ? word(bytes) : 0;
    for (uint32_t i = 0; ok && i < count; i++) {
        if (length - at < 36) { ok = false; break; }
        const uint32_t n = word(bytes + at);
        const uint8_t *hash = bytes + at + 4;
        at += 36;
        if (!n || n > 65536 || n > length - at || memchr(bytes + at, 0, n)) { ok = false; break; }
        char *path = malloc((size_t)n + 1);
        if (!path) { ok = false; break; }
        memcpy(path, bytes + at, n);
        path[n] = '\0';
        ok = check && check(user, path, hash);
        free(path);
        at += n;
    }
    if (!ok || at >= length) { free(bytes); return NULL; }
    *size = length - at;
    memmove(bytes, bytes + at, *size);
    return bytes;
}

void asset_import_cache_write(const char *folder, const uint8_t key[32], const asset_cache_dependency *deps,
                              const int count, const void *data, const size_t size)
{
    if (!folder || count < 0 || count > 65536 || size > ASSET_CACHE_MAX_BYTES - 4u) return;
    size_t length = size + 4;
    for (int i = 0; i < count; i++) {
        const size_t n = strlen(deps[i].path);
        if (!n || n > 65536 || length > ASSET_CACHE_MAX_BYTES - 36u - n) return;
        length += 36 + n;
    }
    uint8_t *bytes = malloc(length);
    if (!bytes) return;
    const uint32_t items = (uint32_t)count;
    memcpy(bytes, &items, 4);
    size_t at = 4;
    for (int i = 0; i < count; i++) {
        const uint32_t n = (uint32_t)strlen(deps[i].path);
        memcpy(bytes + at, &n, 4);
        memcpy(bytes + at + 4, deps[i].hash, 32);
        memcpy(bytes + at + 36, deps[i].path, n);
        at += 36 + n;
    }
    memcpy(bytes + at, data, size);
    asset_cache_write(folder, key, bytes, length);
    free(bytes);
}

// Explicit scalar header and independent streams: no pointers, struct padding
// or typed game vertex layouts in an import cache. Preserve provenance for tangents.
uint8_t *asset_mesh_cache_encode(const mesh_file *m, size_t *size)
{
    const void *streams[] = {m->position, m->normal, m->uv, m->uv2, m->color, m->tangent, m->index, m->sources};
    const uint32_t widths[] = {12, 12, 8, 8, 16, 16, 4, 1};
    uint32_t mask = 0;
    uint64_t length = 52;
    uint64_t expanded = 52;
    for (int i = 0; i < 8; i++) if (streams[i]) {
        mask |= 1u << i;
        const uint32_t count = i == 6 ? m->indices : m->vertices;
        bool constant = count > 1;
        for (uint32_t j = 1; constant && j < count; j++)
            constant = !memcmp(streams[i], (const uint8_t *)streams[i] + (size_t)j * widths[i], widths[i]);
        if (constant) mask |= 1u << (i + 8);
        length += (uint64_t)widths[i] * (constant ? 1u : count);
        expanded += (uint64_t)widths[i] * count;
    }
    if (expanded > ASSET_CACHE_MAX_BYTES) return NULL;
    uint8_t *data = malloc((size_t)length);
    if (!data) return NULL;
    const uint32_t head[] = {m->vertices, m->indices, m->parts, mask,
        (m->has_normals ? 1u : 0u) | (m->has_uvs ? 2u : 0u) | (m->has_uvs2 ? 4u : 0u)
        | (m->has_colors ? 8u : 0u) | (m->has_tangents ? 16u : 0u)};
    memcpy(data, head, sizeof head);
    memcpy(data + sizeof head, m->hash, 32);
    size_t at = 52;
    for (int i = 0; i < 8; i++) if (streams[i]) {
        const size_t n = (size_t)widths[i] * (mask & (1u << (i + 8)) ? 1u : i == 6 ? m->indices : m->vertices);
        memcpy(data + at, streams[i], n);
        at += n;
    }
    *size = (size_t)length;
    return data;
}

bool asset_mesh_cache_decode(const uint8_t *data, const size_t size, mesh_file *out)
{
    if (size < 52 || size > ASSET_CACHE_MAX_BYTES) return false;
    const uint32_t vertices = word(data), indices = word(data + 4), mask = word(data + 12), flags = word(data + 16);
    if (!vertices || vertices > TIDE_MESH_MAX_VERTICES || indices > TIDE_MESH_MAX_INDICES || indices % 3
        || mask > 65535 || (mask & 127u) != 127u || ((mask >> 8) & ~mask) || flags > 31) return false;
    const uint32_t widths[] = {12, 12, 8, 8, 16, 16, 4, 1};
    uint64_t length = 52;
    uint64_t expanded = 52;
    for (int i = 0; i < 8; i++) if (mask & (1u << i)) {
        const uint32_t count = i == 6 ? indices : vertices;
        if (!count && (mask & (1u << (i + 8)))) return false;
        length += (uint64_t)widths[i] * (mask & (1u << (i + 8)) ? 1u : count);
        expanded += (uint64_t)widths[i] * count;
    }
    if (length != size || expanded > ASSET_CACHE_MAX_BYTES) return false;
    mesh_file m = {.vertices = vertices, .indices = indices, .parts = word(data + 8),
        .has_normals = (flags & 1u) != 0, .has_uvs = (flags & 2u) != 0, .has_uvs2 = (flags & 4u) != 0,
        .has_colors = (flags & 8u) != 0, .has_tangents = (flags & 16u) != 0};
    memcpy(m.hash, data + 20, 32);
    void *streams[8] = {0};
    size_t at = 52;
    bool ok = true;
    for (int i = 0; i < 8 && ok; i++) if (mask & (1u << i)) {
        const size_t n = (size_t)widths[i] * (i == 6 ? indices : vertices);
        streams[i] = malloc(n ? n : 1);
        ok = streams[i] != NULL;
        if (mask & (1u << (i + 8))) {
            if (ok) {
                memcpy(streams[i], data + at, widths[i]);
                for (size_t copied = widths[i]; copied < n;) {
                    const size_t take = copied < n - copied ? copied : n - copied;
                    memcpy((uint8_t *)streams[i] + copied, streams[i], take);
                    copied += take;
                }
            }
            at += widths[i];
        } else {
            if (ok) memcpy(streams[i], data + at, n);
            at += n;
        }
    }
    m.position = streams[0]; m.normal = streams[1]; m.uv = streams[2]; m.uv2 = streams[3];
    m.color = streams[4]; m.tangent = streams[5]; m.index = streams[6]; m.sources = streams[7];
    for (uint32_t i = 0; ok && i < indices; i++) ok = m.index[i] < vertices;
    if (!ok) { mesh_file_free(&m); return false; }
    *out = m;
    return true;
}
