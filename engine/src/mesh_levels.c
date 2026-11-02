// Levels of detail made while a game runs (docs/spec.md, Levels of detail): a mesh a game makes and draws with
// `lod:` asks for them as the draw list copies it (tide_draw_lod_request). Where the list keeps levels for what's
// in it, it has them at once; otherwise a copy of what's weighed waits for tide_draw_prepare, which hands a few
// at a time to the host's background threads (tide_jobs.background), and tide_draw_reset keeps what's made and
// gives it to every mesh that asked. The levels are tidec's own for an asset ([LOD]), by the same code
// (tide/internal/mesh_lod.h), so a mesh gives the same levels on every machine; when they come varies, which only
// what's drawn shows. Nothing here is the match's.
//
// What's kept is by what's in the mesh (its vertices and triangles as the list copied them, and what's weighed of
// them), never where it is, so a mesh a game makes again the same, or draws again after a while, finds its levels;
// the least lately used go once they hold more than LOD_KEPT_BYTES. A job copies what it reads, so the game may
// change or let go of its lists while it runs, and the only code a job runs is the engine's, handed over by
// tide_draw_prepare, which a host calls: a game's library unloaded on a reload never had a job of its own.

#include "mesh_levels.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "tide/internal/mesh_lod.h"
#include "tide/jobs.h"
#include "tide/list.h"
#include "tide/net.h"
#include "tide/time.h"
#include "tide/worker.h"

// The most made at once, so a frame's own work keeps the threads it had, and the most waiting, past which a mesh
// that asks draws whole (as it would before its levels came)
enum { LOD_RUNNING_MOST = 2, LOD_WAITING_MOST = 256 };
// What the levels kept may take, beyond the one just made
#define LOD_KEPT_BYTES (64ull << 20)
// Changes whenever the levels made of the same mesh would: with tidec's [LOD] version texts (codegen.c, mesh_levels)
#define LOD_VERSION 1ull

typedef struct lod_job {
    tide_background background; // Alive until the provider says it's done
    uint32_t stop;              // Set, it stops soon and keeps nothing: another version took its place
    bool submitted;
    uint64_t key;
    uint32_t space; // Whose mesh asked, to stop it when the same lists ask again with other contents
    uint64_t id, other;
    uint32_t vertex_count, index_count;
    float *position, *normal, *uv, *uv2, *color, *tangent;
    uint32_t *index;
    tide_mesh_lod_bones bones; // A skinned model's, or none
    tide_mesh_lod made;
} lod_job;

typedef struct lod_kept {
    uint64_t key;
    uint32_t vertex_count;
    uint32_t used; // The frame that last gave them
    tide_mesh_lod lod; // With no levels, the mesh had none to make: it isn't made again
} lod_kept;

typedef struct tide_draw_lod_builds {
    lod_job **jobs; // In the order they asked
    uint32_t job_count, job_capacity;
    lod_kept *kept;
    uint32_t kept_count, kept_capacity;
    uint64_t kept_bytes;
    const tide_jobs *provider; // The one jobs went to, to help while waiting for them
} tide_draw_lod_builds;

static bool fits(const uint32_t at, const uint32_t bytes, const uint32_t size)
{
    return at != 0u && at - 1u <= size && bytes <= size - (at - 1u);
}

static float *read_field(const tide_draw_mesh_data *m, const uint32_t at, const uint32_t floats)
{
    if (!fits(at, floats * (uint32_t)sizeof(float), m->vertex_size)) return NULL;
    float *out = malloc((size_t)m->vertex_count * floats * sizeof(float));
    if (!out) return NULL;
    const uint8_t *bytes = m->bytes;
    for (uint32_t v = 0; v < m->vertex_count; v++)
        memcpy(&out[(size_t)v * floats], bytes + (size_t)v * m->vertex_size + (at - 1u), floats * sizeof(float));
    return out;
}

static void job_free(lod_job *job)
{
    free(job->position), free(job->normal), free(job->uv), free(job->uv2), free(job->color), free(job->tangent);
    free(job->index);
    tide_mesh_lod_bones_free(&job->bones);
    tide_mesh_lod_free(&job->made);
    free(job);
}

static void make_levels(void *context)
{
    lod_job *job = context;
    if (__atomic_load_n(&job->stop, __ATOMIC_RELAXED)) return;
    const tide_mesh_lod_source source = {
        .vertices = job->vertex_count, .position = job->position, .normal = job->normal, .uv = job->uv,
        .uv2 = job->uv2, .color = job->color, .tangent = job->tangent, .index = job->index,
        .indices = job->index_count, .apart = job->bones.first ? tide_mesh_lod_bones_apart : NULL,
        .user = &job->bones, .mesh_least = TIDE_MESH_LOD_MESH_LEAST,
        .triangles_least = TIDE_MESH_LOD_TRIANGLES_LEAST, .stop = &job->stop};
    tide_mesh_lod_make(&source, &job->made);
}

static bool job_done(const lod_job *job)
{
    return job->submitted ? __atomic_load_n(&job->background.done, __ATOMIC_ACQUIRE) != 0u
                          : __atomic_load_n(&job->stop, __ATOMIC_RELAXED) != 0u;
}

static tide_draw_lod_builds *builds(tide_draw_list *d)
{
    if (!d->lod_builds) d->lod_builds = calloc(1, sizeof(tide_draw_lod_builds));
    return d->lod_builds;
}

static lod_kept *kept_of(tide_draw_lod_builds *b, const uint64_t key, const uint32_t vertex_count)
{
    for (uint32_t i = 0; i < b->kept_count; i++) {
        if (b->kept[i].key == key && b->kept[i].vertex_count == vertex_count) return &b->kept[i];
    }
    return NULL;
}

static uint64_t kept_bytes(const lod_kept *k)
{
    return sizeof *k + (uint64_t)k->lod.indices * sizeof(uint32_t);
}

// Gives mesh `mesh` the levels kept for it, with its own tolerance multiplier.
static void give(tide_draw_list *d, const uint32_t mesh, const lod_kept *k)
{
    const tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
    if (k->lod.levels == 0 || m->vertex_count != k->vertex_count) return;
    tide_mesh_levels levels = {.multiplier = m->lod_want, .radius = k->lod.radius, .count = k->lod.levels,
                               .index = k->lod.index, .index_count = k->lod.indices};
    memcpy(levels.center, k->lod.center, sizeof levels.center);
    for (uint32_t i = 0; i < k->lod.levels && i < TIDE_MESH_LEVELS; i++) {
        levels.error[i] = k->lod.error[i];
        levels.first[i] = k->lod.first[i];
        levels.indices[i] = k->lod.count[i];
    }
    tide_draw_mesh_levels(d, mesh, &levels);
}

void tide_draw_want_levels(tide_draw_list *d, const float multiplier, const tide_mesh_lod_layout *layout)
{
    d->lod_model = 0;
    if (!layout) {
        d->lod_want = 0.0f;
        return;
    }
    d->lod_want = multiplier > 0.0f && multiplier <= FLT_MAX ? multiplier : 1.0f;
    d->lod_layout = *layout;
}

void tide_draw_want_bones(tide_draw_list *d, const tide_list model)
{
    d->lod_model = model.at;
}

uint32_t tide_draw_levels_pending(const tide_draw_list *d)
{
    const tide_draw_lod_builds *b = d->lod_builds;
    uint32_t count = 0;
    for (uint32_t i = 0; b && i < b->job_count; i++) count += __atomic_load_n(&b->jobs[i]->stop, __ATOMIC_RELAXED) == 0u;
    return count; // Stopped ones are let go of at the next reset
}

uint64_t tide_draw_lod_version(const tide_draw_list *d, const uint64_t version)
{
    if (!(d->lod_want > 0.0f)) return version;
    // A mesh that asks is another than the same lists drawn without asking, or asking with another multiplier
    uint64_t h = tide_hash_more(version, &d->lod_want, sizeof d->lod_want);
    return tide_hash_end(tide_hash_more(h, &d->lod_layout, sizeof d->lod_layout));
}

void tide_draw_lod_request(tide_draw_list *d, const uint32_t mesh)
{
    if (!(d->lod_want > 0.0f) || mesh == 0 || mesh > d->mesh_count) return;
    tide_draw_mesh_data *m = &d->meshes[mesh - 1u];
    if (m->space == TIDE_MESH_NONE || m->base || !m->bytes || !m->indices) return;
    const uint32_t triangles = m->index_count / 3u;
    if (triangles < TIDE_MESH_LOD_MESH_LEAST || triangles > TIDE_MESH_LOD_TRIANGLES_MOST
        || m->vertex_count > TIDE_MESH_LOD_VERTICES_MOST) return; // Too few to gain, or too many to make
    const tide_mesh_lod_layout *layout = &d->lod_layout;
    if (!fits(layout->position, 3u * sizeof(float), m->vertex_size)) return;
    // A skinned model's bones, from its bytes: a model that isn't one of this many vertices weighs none
    tide_mesh_lod_bones bones = {0};
    if (d->lod_model) {
        const tide_list model = {d->lod_model};
        const int32_t size = tide_list_count(model);
        if (size > 0) {
            bool copied;
            uint8_t *bytes = tide_list_flatten(model, 1u, false, &copied);
            if (bytes) tide_mesh_lod_bones_of(bytes, (size_t)size, m->vertex_count, &bones);
            tide_list_unflatten(model, bytes, 1u, false, copied);
        }
    }
    // What the levels are made of: the list's copy, which left out triangles past the vertices, and the bones
    uint64_t h = tide_hash_more(TIDE_HASH_START, &(const uint64_t){LOD_VERSION}, sizeof(uint64_t));
    h = tide_hash_more(h, layout, sizeof *layout);
    h = tide_hash_more(h, &m->vertex_size, sizeof m->vertex_size);
    h = tide_hash_more(h, &m->vertex_count, sizeof m->vertex_count);
    h = tide_hash_more(h, m->bytes, (size_t)m->vertex_count * m->vertex_size);
    h = tide_hash_more(h, &m->index_count, sizeof m->index_count);
    h = tide_hash_more(h, m->indices, (size_t)m->index_count * sizeof(uint32_t));
    if (bones.first) {
        const uint32_t all = bones.vertices ? bones.first[bones.vertices - 1u] + bones.count[bones.vertices - 1u] : 0u;
        h = tide_hash_more(h, bones.count, (size_t)bones.vertices * sizeof(uint32_t));
        h = tide_hash_more(h, bones.palette, (size_t)all * sizeof(uint32_t));
        h = tide_hash_more(h, bones.weight, (size_t)all * sizeof(float));
    }
    const uint64_t key = tide_hash_end(h) | 1u; // Never 0, which is no mesh's
    m->lod_key = key;
    m->lod_want = d->lod_want;
    tide_draw_lod_builds *b = builds(d);
    if (!b) {
        tide_mesh_lod_bones_free(&bones);
        return;
    }
    lod_kept *k = kept_of(b, key, m->vertex_count);
    if (k) {
        k->used = d->frame;
        give(d, mesh, k);
        tide_mesh_lod_bones_free(&bones);
        return;
    }
    // Lists the world keeps that are being made again: what was made of them before is of no use now. Lists made
    // along the way share one place (the scratch area's), and tell their meshes apart by their contents alone
    const uint64_t scratch = tide_handle_of(TIDE_IN_SCRATCH, 0);
    const bool owned = m->id != scratch && m->other != scratch;
    for (uint32_t i = 0; i < b->job_count; i++) {
        lod_job *job = b->jobs[i];
        if (job->key == key && job->vertex_count == m->vertex_count) goto none; // Being made: given when it's there
        if (owned && job->space == m->space && job->id == m->id && job->other == m->other)
            __atomic_store_n(&job->stop, 1u, __ATOMIC_RELAXED);
    }
    if (b->job_count >= LOD_WAITING_MOST) goto none;
    if (b->job_count == b->job_capacity) {
        const uint32_t capacity = b->job_capacity ? b->job_capacity * 2u : 8u;
        lod_job **jobs = realloc(b->jobs, (size_t)capacity * sizeof *jobs);
        if (!jobs) goto none;
        b->jobs = jobs;
        b->job_capacity = capacity;
    }
    lod_job *job = calloc(1, sizeof *job);
    if (!job) goto none;
    *job = (lod_job){.key = key, .space = m->space, .id = m->id, .other = m->other, .vertex_count = m->vertex_count,
                     .index_count = m->index_count, .bones = bones};
    job->position = read_field(m, layout->position, 3u);
    job->index = malloc((size_t)m->index_count * sizeof(uint32_t));
    if (!job->position || !job->index) {
        job_free(job);
        return;
    }
    memcpy(job->index, m->indices, (size_t)m->index_count * sizeof(uint32_t));
    // A field the vertex hasn't room for is weighed as if it weren't there; one that can't be copied, as well
    job->normal = read_field(m, layout->normal, 3u);
    job->uv = read_field(m, layout->uv, 2u);
    job->uv2 = read_field(m, layout->uv2, 2u);
    job->color = read_field(m, layout->color, 4u);
    job->tangent = read_field(m, layout->tangent, 4u);
    b->jobs[b->job_count++] = job;
    return;
none:
    tide_mesh_lod_bones_free(&bones);
}

void tide_draw_lod_submit(const tide_draw_list *d, const tide_jobs *jobs)
{
    tide_draw_lod_builds *b = d->lod_builds;
    if (!b || !jobs || !jobs->background) return;
    uint32_t running = 0;
    for (uint32_t i = 0; i < b->job_count; i++) {
        if (b->jobs[i]->submitted && !job_done(b->jobs[i])) running++;
    }
    for (uint32_t i = 0; i < b->job_count && running < LOD_RUNNING_MOST; i++) {
        lod_job *job = b->jobs[i];
        if (job->submitted || __atomic_load_n(&job->stop, __ATOMIC_RELAXED)) continue;
        job->background = (tide_background){.work = make_levels, .context = job};
        job->submitted = true;
        b->provider = jobs;
        jobs->background(jobs->self, &job->background);
        running++;
    }
}

static void keep(tide_draw_list *d, tide_draw_lod_builds *b, lod_job *job)
{
    if (b->kept_count == b->kept_capacity) {
        const uint32_t capacity = b->kept_capacity ? b->kept_capacity * 2u : 16u;
        lod_kept *kept = realloc(b->kept, (size_t)capacity * sizeof *kept);
        if (!kept) return; // Made again when it's asked for again
        b->kept = kept;
        b->kept_capacity = capacity;
    }
    lod_kept *k = &b->kept[b->kept_count++];
    *k = (lod_kept){.key = job->key, .vertex_count = job->vertex_count, .used = d->frame, .lod = job->made};
    job->made = (tide_mesh_lod){0};
    b->kept_bytes += kept_bytes(k);
    // The least lately used go, all but the one just kept
    while (b->kept_bytes > LOD_KEPT_BYTES && b->kept_count > 1u) {
        uint32_t oldest = 0;
        for (uint32_t i = 1; i + 1u < b->kept_count; i++) {
            if ((int32_t)(b->kept[i].used - b->kept[oldest].used) < 0) oldest = i;
        }
        b->kept_bytes -= kept_bytes(&b->kept[oldest]);
        tide_mesh_lod_free(&b->kept[oldest].lod);
        memmove(&b->kept[oldest], &b->kept[oldest + 1u], (size_t)(b->kept_count - oldest - 1u) * sizeof *b->kept);
        b->kept_count--;
    }
    k = &b->kept[b->kept_count - 1u];
    // Every mesh that asked for them, whichever draws it was copied for
    for (uint32_t i = 0; i < d->mesh_count; i++) {
        const tide_draw_mesh_data *m = &d->meshes[i];
        if (m->lod_key == k->key && m->space != TIDE_MESH_NONE && !m->base && m->levels == 0) give(d, i + 1u, k);
    }
}

void tide_draw_lod_collect(tide_draw_list *d)
{
    tide_draw_lod_builds *b = d->lod_builds;
    if (!b) return;
    uint32_t waiting = 0;
    for (uint32_t i = 0; i < b->job_count; i++) {
        lod_job *job = b->jobs[i];
        if (!job_done(job)) {
            b->jobs[waiting++] = job;
            continue;
        }
        // Stopped, it may have made nothing of what it could: kept, it would say the mesh has no levels
        if (job->submitted && !__atomic_load_n(&job->stop, __ATOMIC_RELAXED)) keep(d, b, job);
        job_free(job);
    }
    b->job_count = waiting;
}

void tide_draw_lod_free(tide_draw_list *d)
{
    tide_draw_lod_builds *b = d->lod_builds;
    if (!b) return;
    for (uint32_t i = 0; i < b->job_count; i++) __atomic_store_n(&b->jobs[i]->stop, 1u, __ATOMIC_RELAXED);
    for (uint32_t i = 0; i < b->job_count; i++) {
        lod_job *job = b->jobs[i];
        while (job->submitted && !__atomic_load_n(&job->background.done, __ATOMIC_ACQUIRE)) {
            if (b->provider && b->provider->help) b->provider->help(b->provider->self, false);
            tide_yield();
        }
        job_free(job);
    }
    for (uint32_t i = 0; i < b->kept_count; i++) tide_mesh_lod_free(&b->kept[i].lod);
    free(b->jobs);
    free(b->kept);
    free(b);
    d->lod_builds = NULL;
}
