// The cache of levels of detail tidec makes for meshes marked [LOD] (mesh_lod.h): kept for the process, and in
// the game's .tide/assets/lod/ for the builds after it. The levels themselves are the engine's (tide/internal/mesh_lod.h).

#include "mesh_lod.h"

#include "build_threads.h"
#include "folders.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// The cache

typedef struct kept {
    uint8_t key[32];
    tide_mesh_lod lod;
} kept;

static kept *kept_levels;
static uint32_t kept_count, kept_room;
static uint32_t made_count;

uint32_t mesh_lod_made(void)
{
    return made_count;
}

// A file of levels: these words, each level's first index, count and error, then the indices.
enum { FILE_MAGIC = 0x444F4C54, FILE_VERSION = 1, FILE_HEAD = 8 + TIDE_MESH_LOD_MOST * 3 };

static bool levels_hold(const tide_mesh_lod *lod, const uint32_t vertices)
{
    if (lod->levels > TIDE_MESH_LOD_MOST || !(lod->radius >= 0.0f) || !isfinite(lod->radius)) return false;
    float before = 0.0f;
    for (uint32_t i = 0; i < lod->levels; i++) {
        if (lod->count[i] == 0 || lod->count[i] % 3u || lod->first[i] > lod->indices || lod->count[i] > lod->indices - lod->first[i]
            || !(lod->error[i] >= before) || !isfinite(lod->error[i])) {
            return false;
        }
        before = lod->error[i];
    }
    for (uint32_t i = 0; i < lod->indices; i++) {
        if (lod->index[i] >= vertices) return false;
    }
    return isfinite(lod->center[0]) && isfinite(lod->center[1]) && isfinite(lod->center[2]);
}

static bool read_levels(const char *path, const uint32_t vertices, tide_mesh_lod *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint32_t head[FILE_HEAD];
    tide_mesh_lod lod = {0};
    bool ok = fread(head, sizeof(uint32_t), FILE_HEAD, f) == FILE_HEAD && head[0] == FILE_MAGIC && head[1] == FILE_VERSION
           && head[6] <= TIDE_MESH_LOD_MOST && head[7] <= (3u << 25) * TIDE_MESH_LOD_MOST;
    if (ok) {
        memcpy(lod.center, &head[2], sizeof lod.center);
        memcpy(&lod.radius, &head[5], sizeof lod.radius);
        lod.levels = head[6];
        lod.indices = head[7];
        for (uint32_t i = 0; i < TIDE_MESH_LOD_MOST; i++) {
            lod.first[i] = head[8u + i * 3u];
            lod.count[i] = head[9u + i * 3u];
            memcpy(&lod.error[i], &head[10u + i * 3u], sizeof(float));
        }
        if (lod.indices) {
            lod.index = calloc(lod.indices, sizeof(uint32_t));
            if (!lod.index) abort();
            ok = fread(lod.index, sizeof(uint32_t), lod.indices, f) == lod.indices;
        }
        ok = ok && fgetc(f) == EOF && levels_hold(&lod, vertices);
    }
    fclose(f);
    if (!ok) {
        free(lod.index);
        return false;
    }
    *out = lod;
    return true;
}

static void write_levels(const char *path, const tide_mesh_lod *lod)
{
    char fresh[2400];
    if (snprintf(fresh, sizeof fresh, "%s.new", path) >= (int)sizeof fresh) return;
    FILE *f = fopen(fresh, "wb");
    if (!f) return; // Nothing's lost: they're made again
    uint32_t head[FILE_HEAD] = {FILE_MAGIC, FILE_VERSION};
    memcpy(&head[2], lod->center, sizeof lod->center);
    memcpy(&head[5], &lod->radius, sizeof lod->radius);
    head[6] = lod->levels;
    head[7] = lod->indices;
    for (uint32_t i = 0; i < TIDE_MESH_LOD_MOST; i++) {
        head[8u + i * 3u] = lod->first[i];
        head[9u + i * 3u] = lod->count[i];
        memcpy(&head[10u + i * 3u], &lod->error[i], sizeof(float));
    }
    bool ok = fwrite(head, sizeof(uint32_t), FILE_HEAD, f) == FILE_HEAD;
    if (ok && lod->indices) ok = fwrite(lod->index, sizeof(uint32_t), lod->indices, f) == lod->indices;
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path); // Windows doesn't rename onto a file that's there
        ok = rename(fresh, path) == 0;
    }
    if (!ok) remove(fresh);
}

static const tide_mesh_lod *find_kept(const uint8_t key[32])
{
    for (uint32_t i = 0; i < kept_count; i++) {
        if (memcmp(kept_levels[i].key, key, 32) == 0) return &kept_levels[i].lod;
    }
    return NULL;
}

static const tide_mesh_lod *keep(const uint8_t key[32], const tide_mesh_lod *lod)
{
    if (kept_count == kept_room) {
        kept_room = kept_room ? kept_room * 2u : 16u;
        kept_levels = realloc(kept_levels, (size_t)kept_room * sizeof(kept));
        if (!kept_levels) abort();
    }
    kept *k = &kept_levels[kept_count++];
    memcpy(k->key, key, 32);
    k->lod = *lod;
    return &k->lod;
}

// Where the levels of `key` are kept between builds: "" with no game folder.
static void file_of(const uint8_t key[32], const char *game_folder, char path[2300])
{
    *path = '\0';
    if (!game_folder) return;
    char dir[2048];
    if (snprintf(dir, sizeof dir, "%s/.tide/assets/lod", game_folder) >= (int)sizeof dir) return;
    folder_make(dir);
    char hex[65];
    for (uint32_t i = 0; i < 32u; i++) snprintf(hex + i * 2u, 3, "%02x", key[i]);
    snprintf(path, 2300, "%s/%s.lod", dir, hex);
}

const tide_mesh_lod *mesh_lod_cached(const uint8_t key[32], const tide_mesh_lod_source *source, const char *game_folder)
{
    const tide_mesh_lod *found = find_kept(key);
    if (found) return found;
    char path[2300];
    file_of(key, game_folder, path);
    tide_mesh_lod lod;
    if (!*path || !read_levels(path, source->vertices, &lod)) {
        tide_mesh_lod_make(source, &lod);
        made_count++;
        if (*path) write_levels(path, &lod);
    }
    return keep(key, &lod);
}

// ---------------------------------------------------------------------------
// Levels wanted ahead, made at once

typedef struct wanted {
    uint8_t key[32];
    char path[2300];
    tide_mesh_lod_source source; // Over copies of the mesh's numbers, which are the want's
    const uint8_t *skinned;      // The model's bytes, whose bones the levels weigh, or NULL
    size_t skinned_size;
    tide_mesh_lod lod;
} wanted;

static wanted *wants;
static uint32_t want_count, want_room;

static const float *copy_floats(const float *from, const size_t count)
{
    if (!from) return NULL;
    float *to = malloc((count ? count : 1u) * sizeof(float));
    if (!to) abort();
    memcpy(to, from, count * sizeof(float));
    return to;
}

void mesh_lod_want(const uint8_t key[32], const tide_mesh_lod_source *source, const uint8_t *skinned, const size_t skinned_size,
                   const char *game_folder)
{
    if (find_kept(key)) return;
    for (uint32_t i = 0; i < want_count; i++) {
        if (memcmp(wants[i].key, key, 32) == 0) return;
    }
    char path[2300];
    file_of(key, game_folder, path);
    tide_mesh_lod lod;
    if (*path && read_levels(path, source->vertices, &lod)) {
        keep(key, &lod);
        return;
    }
    if (want_count == want_room) {
        want_room = want_room ? want_room * 2u : 16u;
        wants = realloc(wants, (size_t)want_room * sizeof(wanted));
        if (!wants) abort();
    }
    wanted *w = &wants[want_count++];
    memset(w, 0, sizeof *w);
    memcpy(w->key, key, 32);
    memcpy(w->path, path, sizeof path);
    const size_t v = source->vertices;
    w->source = *source;
    w->source.position = copy_floats(source->position, v * 3u);
    w->source.normal = copy_floats(source->normal, v * 3u);
    w->source.uv = copy_floats(source->uv, v * 2u);
    w->source.uv2 = copy_floats(source->uv2, v * 2u);
    w->source.color = copy_floats(source->color, v * 4u);
    w->source.tangent = copy_floats(source->tangent, v * 4u);
    uint32_t *index = malloc((source->indices ? source->indices : 1u) * sizeof(uint32_t));
    if (!index) abort();
    if (source->indices) memcpy(index, source->index, source->indices * sizeof(uint32_t));
    w->source.index = index;
    w->source.apart = NULL; // The bones' are weighed where the levels are made
    w->source.user = NULL;
    w->source.stop = NULL;
    w->skinned = skinned;
    w->skinned_size = skinned_size;
}

static void make_wanted(void *context, const uint32_t i)
{
    wanted *w = &((wanted *)context)[i];
    tide_mesh_lod_bones bones = {0};
    if (w->skinned) tide_mesh_lod_bones_of(w->skinned, w->skinned_size, w->source.vertices, &bones);
    tide_mesh_lod_source source = w->source;
    source.apart = bones.first ? tide_mesh_lod_bones_apart : NULL;
    source.user = &bones;
    tide_mesh_lod_make(&source, &w->lod);
    tide_mesh_lod_bones_free(&bones);
}

void mesh_lod_make_wanted(void)
{
    // Each mesh's levels are its own, made the same whichever thread makes them: only how long it takes changes
    build_threads_each(want_count, 0u, make_wanted, wants);
    for (uint32_t i = 0; i < want_count; i++) {
        wanted *w = &wants[i];
        made_count++;
        if (*w->path) write_levels(w->path, &w->lod);
        keep(w->key, &w->lod);
        free((void *)w->source.position), free((void *)w->source.normal), free((void *)w->source.uv);
        free((void *)w->source.uv2), free((void *)w->source.color), free((void *)w->source.tangent);
        free((void *)w->source.index);
    }
    want_count = 0;
}
