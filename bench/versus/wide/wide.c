// Wide in C: an array for each of a body's twelve floats, of which a tick
// writes six, stepped through with SIMD on as many threads as there are.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "tide/math.h" // tide_hash_i2: the start Tide's Math.Hash gives
#include "tide/page.h" // tide_alloc: memory every thread can use on the web
#include "versus.h"

enum { PX, PY, PZ, VX, VY, VZ, AX, AY, AZ, DRAG, BOUNCE, RADIUS, LANES };

typedef struct wide {
    uint32_t count;
    uint32_t moving; // The mask never changes: none, all, or a mix
    uint32_t frozen_share;
    bool tagged;
    bool ready;
    float *block; // The arrays, one after the other
    float *lane[LANES];
    uint32_t *frozen; // Which crates stay where they are: all ones for those
    float *kept; // The six mutable lanes before the latest tick, with --kept
    float *kept_lane[LANES]; // The other six borrow the arrays that never change
    uint32_t kept_count; // Zero before setup; the fixed count afterwards
} wide;

static void *make(const versus_flag *flags)
{
    wide *w = tide_alloc_zeroed(1, sizeof *w);
    w->count = flags[0].value;
    if (flags[1].value) w->kept = tide_alloc((size_t)(w->count ? w->count : 1u) * AX * sizeof(float));
    w->tagged = flags[2].value != 0;
    w->frozen_share = flags[3].value;
    return w;
}

static void setup(wide *w)
{
    // Apart by a few cache lines more than their size, so a store to one and
    // a load from the next never share their address's low bits (see
    // particles.c)
    const size_t stride = ((size_t)w->count + 15u) / 16u * 16u + 64u * 3u;
    w->block = tide_alloc(LANES * stride * sizeof(float));
    for (int k = 0; k < LANES; k++) w->lane[k] = w->block + (size_t)k * stride;
    if (w->kept) {
        for (int k = 0; k < LANES; k++) w->kept_lane[k] = k < AX ? w->kept + (size_t)k * w->count : w->lane[k];
    }
    w->frozen = tide_alloc_zeroed(w->count ? w->count : 1u, sizeof *w->frozen);
    for (uint32_t i = 0; i < w->count; i++) {
        const int32_t h = tide_hash_i2((tide_int2){(int32_t)i, 0});
        const int32_t g = tide_hash_i2((tide_int2){(int32_t)i, 1});
        const int32_t f = tide_hash_i2((tide_int2){(int32_t)i, 2});
        w->lane[PX][i] = (float)(h % 10000) * 0.01f;
        w->lane[PY][i] = (float)(g % 10000) * 0.01f + 1.0f;
        w->lane[PZ][i] = (float)(f % 10000) * 0.01f;
        w->lane[VX][i] = (float)(h % 201 - 100) * 0.1f;
        w->lane[VY][i] = (float)(g % 201 - 100) * 0.1f;
        w->lane[VZ][i] = (float)(f % 201 - 100) * 0.1f;
        w->lane[AX][i] = (float)(f % 21 - 10) * 0.05f;
        w->lane[AY][i] = -9.81f;
        w->lane[AZ][i] = (float)(h % 21 - 10) * 0.05f;
        w->lane[DRAG][i] = (float)(g % 50) * 0.001f;
        w->lane[BOUNCE][i] = 0.5f + (float)(h % 40) * 0.01f;
        w->lane[RADIUS][i] = 0.25f + (float)(f % 4) * 0.25f;
        if (w->tagged && tide_hash_i2((tide_int2){(int32_t)i, 3}) % 100 < (int32_t)w->frozen_share) w->frozen[i] = UINT32_MAX;
        if (!w->frozen[i]) w->moving++;
    }
}

typedef struct move_job {
    wide *w;
    uint32_t pieces;
} move_job;

// Branches as selects, so every body takes the same path and the loop goes
// with SIMD. The arrays are parameters, which is where the compiler takes
// `restrict` at its word: it has twelve of them to tell apart otherwise, and
// gives up. It's told to use SIMD, as by its own reckoning the selects cost
// too much without AVX2 (they don't: the loop takes half as long this way).
static void move_rows(const uint32_t from, const uint32_t to, const float dt, float *restrict px, float *restrict py,
                      float *restrict pz, float *restrict vx, float *restrict vy, float *restrict vz,
                      const float *restrict ax, const float *restrict ay, const float *restrict az,
                      const float *restrict drag, const float *restrict bounce, const float *restrict radius)
{
    // Sanitizer checks can prevent vectorization; keep checking the scalar loop.
#if !__has_feature(address_sanitizer) && !__has_feature(undefined_behavior_sanitizer)
#pragma clang loop vectorize(enable)
#endif
    for (uint32_t i = from; i < to; i++) {
        const float keep = 1.0f - drag[i] * dt;
        const float u = (vx[i] + ax[i] * dt) * keep;
        const float v = (vy[i] + ay[i] * dt) * keep;
        const float s = (vz[i] + az[i] * dt) * keep;
        const float y = py[i] + v * dt;
        const float r = radius[i], back = bounce[i]; // Read whichever way it goes, or reading them is a branch
        const bool floor = y < r;
        px[i] += u * dt;
        py[i] = floor ? r * 2.0f - y : y;
        pz[i] += s * dt;
        vx[i] = u;
        vy[i] = floor ? -v * back : v;
        vz[i] = s;
    }
}

// One of two floats by a word that's all ones or none, bit by bit.
static inline float pick(const uint32_t first, const float a, const float b)
{
    uint32_t x, y;
    memcpy(&x, &a, sizeof x);
    memcpy(&y, &b, sizeof y);
    const uint32_t bits = (x & first) | (y & ~first);
    float picked;
    memcpy(&picked, &bits, sizeof picked);
    return picked;
}

// The same for crates: a frozen one is written back as it was. Which are
// frozen is a word a crate, all ones or none, and what's written is picked
// with it bit by bit: written as a condition, the compiler takes each store
// for one that only sometimes happens, and branches for every crate.
static void shift_rows(const uint32_t from, const uint32_t to, const float dt, const uint32_t *restrict frozen,
                       float *restrict px, float *restrict py, float *restrict pz, float *restrict vx, float *restrict vy,
                       float *restrict vz, const float *restrict ax, const float *restrict ay, const float *restrict az,
                       const float *restrict drag, const float *restrict bounce, const float *restrict radius)
{
#if !__has_feature(address_sanitizer) && !__has_feature(undefined_behavior_sanitizer)
#pragma clang loop vectorize(enable)
#endif
    for (uint32_t i = from; i < to; i++) {
        const uint32_t still = frozen[i];
        const float keep = 1.0f - drag[i] * dt;
        const float u = (vx[i] + ax[i] * dt) * keep;
        const float v = (vy[i] + ay[i] * dt) * keep;
        const float s = (vz[i] + az[i] * dt) * keep;
        const float x = px[i] + u * dt;
        const float y = py[i] + v * dt;
        const float z = pz[i] + s * dt;
        const float r = radius[i], back = bounce[i];
        const bool floor = y < r;
        px[i] = pick(still, px[i], x);
        py[i] = pick(still, py[i], floor ? r * 2.0f - y : y);
        pz[i] = pick(still, pz[i], z);
        vx[i] = pick(still, vx[i], u);
        vy[i] = pick(still, vy[i], floor ? -v * back : v);
        vz[i] = pick(still, vz[i], s);
    }
}

static void move(void *context, const uint32_t piece)
{
    const move_job *j = context;
    const wide *w = j->w;
    const uint32_t from = (uint32_t)((uint64_t)w->count * piece / j->pieces);
    const uint32_t to = (uint32_t)((uint64_t)w->count * (piece + 1u) / j->pieces);
    float *const *l = w->lane;
    if (w->moving != w->count) {
        shift_rows(from, to, VERSUS_DT, w->frozen, l[PX], l[PY], l[PZ], l[VX], l[VY], l[VZ], l[AX], l[AY], l[AZ], l[DRAG],
                   l[BOUNCE], l[RADIUS]);
    } else {
        move_rows(from, to, VERSUS_DT, l[PX], l[PY], l[PZ], l[VX], l[VY], l[VZ], l[AX], l[AY], l[AZ], l[DRAG], l[BOUNCE],
                  l[RADIUS]);
    }
}

static void tick(void *state, const tide_jobs *jobs)
{
    wide *w = state;
    if (!w->ready) { // Tide's first tick spawns them, and they move from the next
        setup(w);
        w->ready = true;
        return;
    }
    if (w->kept) {
        // The retained state is read after the tick too. Copy only what can
        // change; with no moving row, initialize it once and leave it there.
        if (w->kept_count != w->count || w->moving) {
            const size_t bytes = (size_t)w->count * sizeof(float);
            for (int k = 0; k < AX; k++) memcpy(w->kept_lane[k], w->lane[k], bytes);
        }
        w->kept_count = w->count;
    }
    if (!w->moving) return;
    // Threads pay from about a quarter of a million (on a 12-core Ryzen 9 7900X)
    move_job j = {w, versus_pieces(jobs, w->count, 250000u)};
    versus_for(jobs, j.pieces, move, &j);
}

static uint64_t arrays_digest(const uint32_t count, float *const *lane)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        float item[LANES];
        for (int k = 0; k < LANES; k++) item[k] = lane[k][i];
        sum += versus_item(item, sizeof item);
    }
    return sum;
}

static uint64_t digest(const void *state)
{
    const wide *w = state;
    uint64_t sum = arrays_digest(w->ready ? w->count : 0u, w->lane);
    if (w->kept) {
        const uint64_t before = arrays_digest(w->kept_count, w->kept_lane);
        sum += versus_item(&before, sizeof before);
    }
    return sum;
}

static void destroy(void *state)
{
    wide *w = state;
    free(w->frozen);
    free(w->block);
    free(w->kept);
    free(w);
}

const versus_way wide_c = {"C", make, tick, digest, destroy};
