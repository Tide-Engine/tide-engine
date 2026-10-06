// Particles in C: an array for each coordinate, which the compiler steps
// through four at a time with SIMD, on as many threads as there are.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "tide/math.h" // tide_hash_i2: the start Tide's Math.Hash gives
#include "tide/page.h" // tide_alloc: memory every thread can use on the web
#include "versus.h"

typedef struct particles {
    uint32_t count;
    bool ready;
    float *block; // The arrays, one after the other
    float *x, *y, *vx, *vy;
    float *kept; // The coordinates before the latest tick, when --kept is set
    uint32_t kept_count;
} particles;

static void *make(const versus_flag *flags)
{
    particles *p = tide_alloc_zeroed(1, sizeof *p);
    p->count = flags[0].value;
    if (flags[1].value) p->kept = tide_alloc((size_t)p->count * 4u * sizeof(float));
    return p;
}

static void setup(particles *p)
{
    // Arrays as big as these each start a page in when they have blocks of
    // their own, and then a store to one and a load from the next one share
    // their address's low bits, which the CPU takes for the same address
    // until it knows better ("4K aliasing"). Apart by a few cache lines more
    // than their size, they never do.
    const size_t stride = ((size_t)p->count + 15u) / 16u * 16u + 64u * 3u;
    p->block = tide_alloc(4u * stride * sizeof(float));
    p->x = p->block;
    p->y = p->x + stride;
    p->vx = p->y + stride;
    p->vy = p->vx + stride;
    for (uint32_t i = 0; i < p->count; i++) {
        const int32_t h = tide_hash_i2((tide_int2){(int32_t)i, 0});
        const int32_t g = tide_hash_i2((tide_int2){(int32_t)i, 1});
        p->x[i] = (float)(h % 10000) * 0.01f;
        p->y[i] = (float)(g % 10000) * 0.01f;
        p->vx[i] = (float)(h % 201 - 100) * 0.1f;
        p->vy[i] = (float)(g % 201 - 100) * 0.1f;
    }
}

typedef struct move_job {
    particles *p;
    uint32_t pieces;
} move_job;

// Branches as selects, so every particle takes the same path and the loop
// goes with SIMD.
static void move(void *context, const uint32_t piece)
{
    const move_job *j = context;
    const uint32_t from = (uint32_t)((uint64_t)j->p->count * piece / j->pieces);
    const uint32_t to = (uint32_t)((uint64_t)j->p->count * (piece + 1u) / j->pieces);
    float *restrict x = j->p->x, *restrict y = j->p->y, *restrict vx = j->p->vx, *restrict vy = j->p->vy;
    const float dt = VERSUS_DT;
    const float fall = 9.81f * dt;
    for (uint32_t i = from; i < to; i++) {
        const float v = vy[i] - fall;
        const float h = y[i] + v * dt;
        const bool floor = h < 0.0f;
        y[i] = floor ? -h : h;
        vy[i] = floor ? -v * 0.9f : v;
        const float u = vx[i];
        const float w = x[i] + u * dt;
        const bool left = w < 0.0f, right = w > 100.0f;
        x[i] = left ? -w : right ? 200.0f - w : w;
        vx[i] = left || right ? -u : u;
    }
}

static void tick(void *state, const tide_jobs *jobs)
{
    particles *p = state;
    if (p->kept && p->ready) {
        const size_t bytes = (size_t)p->count * sizeof(float);
        memcpy(p->kept, p->x, bytes);
        memcpy(p->kept + p->count, p->y, bytes);
        memcpy(p->kept + (size_t)p->count * 2u, p->vx, bytes);
        memcpy(p->kept + (size_t)p->count * 3u, p->vy, bytes);
        p->kept_count = p->count;
    }
    if (!p->ready) { // Tide's first tick spawns them, and they move from the next
        setup(p);
        p->ready = true;
        return;
    }
    // Threads pay from about half a million (on a 12-core Ryzen 9 7900X)
    move_job j = {p, versus_pieces(jobs, p->count, 500000u)};
    versus_for(jobs, j.pieces, move, &j);
}

static uint64_t arrays_digest(const uint32_t count, const float *x, const float *y, const float *vx, const float *vy)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        const float item[4] = {x[i], y[i], vx[i], vy[i]};
        sum += versus_item(item, sizeof item);
    }
    return sum;
}

static uint64_t digest(const void *state)
{
    const particles *p = state;
    uint64_t sum = arrays_digest(p->count, p->x, p->y, p->vx, p->vy);
    if (p->kept) {
        const uint64_t before = arrays_digest(p->kept_count, p->kept, p->kept + p->count,
                                              p->kept + (size_t)p->count * 2u, p->kept + (size_t)p->count * 3u);
        sum += versus_item(&before, sizeof before);
    }
    return sum;
}

static void destroy(void *state)
{
    particles *p = state;
    free(p->block);
    free(p->kept);
    free(p);
}

const versus_way particles_c = {"C", make, tick, digest, destroy};
