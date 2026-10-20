// Rollback in C (see period.h), two ways.
//
// "C" keeps the whole world in one block, its arrays one after the other:
// keeping a world is a memcpy of the block into the ring, restoring one a
// memcpy back, and the hash is wyhash over every byte of it, as someone who
// knows the machine writes it first.
//
// "C, dirty" is what they write next, once they know a tick changes little:
// each array in blocks of 16 KiB, each with a version that a tick changes
// when it changes a byte of the block (which the loops find as they go,
// comparing bits), and the hash of the block kept with the version it's of.
// Keeping a world copies only the blocks whose version isn't the one the
// ring's world has, restoring one the same the other way, and the hash only
// hashes the blocks that changed since they were last hashed, then their
// hashes.
//
// Both hash all of the world's state every time they're asked, as a session
// has to: what changed can't be left out of a hash, and both are bit for bit
// what Tide's world is, whatever their hash's number.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "period.h"
#include "tide/math.h" // tide_hash_i2: the numbers Tide's Math.Hash gives
#include "tide/page.h" // tide_alloc: memory every thread can use on the web
#include "tide/time.h"
#include "versus.h"

// ---------------------------------------------------------------------------
// wyhash (Wang Yi's, final version 4): 48 bytes a round in three chains of a
// 64 by 64 to 128-bit multiply each.

#ifdef __SIZEOF_INT128__
__extension__ typedef unsigned __int128 wy_u128;
#endif

static inline void wy_mum(uint64_t *a, uint64_t *b)
{
#ifdef __SIZEOF_INT128__
    const wy_u128 r = (wy_u128)*a * *b;
    *a = (uint64_t)r;
    *b = (uint64_t)(r >> 64);
#else
    const uint64_t ha = *a >> 32, hb = *b >> 32, la = (uint32_t)*a, lb = (uint32_t)*b;
    const uint64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb, t = rl + (rm0 << 32);
    uint64_t c = t < rl;
    const uint64_t lo = t + (rm1 << 32);
    c += lo < t;
    *a = lo;
    *b = rh + (rm0 >> 32) + (rm1 >> 32) + c;
#endif
}

static inline uint64_t wy_mix(uint64_t a, uint64_t b)
{
    wy_mum(&a, &b);
    return a ^ b;
}

static inline uint64_t wy_r8(const uint8_t *p)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static inline uint64_t wy_r4(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static const uint64_t wy_p[4] = {0x2d358dccaa6c78a5ull, 0x8bb84b93962eacc9ull, 0x4b33a62ed433d4a3ull, 0x4d5a2da51de1aa47ull};

static uint64_t wy_hash(const void *key, const size_t len, uint64_t seed)
{
    const uint8_t *p = key;
    seed ^= wy_mix(seed ^ wy_p[0], wy_p[1]);
    uint64_t a, b;
    if (len <= 16) {
        if (len >= 4) {
            a = (wy_r4(p) << 32) | wy_r4(p + ((len >> 3) << 2));
            b = (wy_r4(p + len - 4) << 32) | wy_r4(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = ((uint64_t)p[0] << 16) | ((uint64_t)p[len >> 1] << 8) | p[len - 1];
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        size_t i = len;
        if (i > 48) {
            uint64_t see1 = seed, see2 = seed;
            do {
                seed = wy_mix(wy_r8(p) ^ wy_p[1], wy_r8(p + 8) ^ seed);
                see1 = wy_mix(wy_r8(p + 16) ^ wy_p[2], wy_r8(p + 24) ^ see1);
                see2 = wy_mix(wy_r8(p + 32) ^ wy_p[3], wy_r8(p + 40) ^ see2);
                p += 48;
                i -= 48;
            } while (i > 48);
            seed ^= see1 ^ see2;
        }
        while (i > 16) {
            seed = wy_mix(wy_r8(p) ^ wy_p[1], wy_r8(p + 8) ^ seed);
            i -= 16;
            p += 16;
        }
        a = wy_r8(p + i - 16);
        b = wy_r8(p + i - 8);
    }
    a ^= wy_p[1];
    b ^= seed;
    wy_mum(&a, &b);
    return wy_mix(a ^ wy_p[0] ^ len, b ^ wy_p[1]);
}

// ---------------------------------------------------------------------------
// The world: arrays of 4-byte elements, each mover's and turret's fields side
// by side, as SIMD wants them, then the list, then the scalars.

enum { PX, PY, PZ, VX, VY, VZ, TX, TY, TZ, COOL, RELOAD, SLOT, SHOTS, HEAT, SCALARS, ARRAYS };
enum { READY, SCORE, SCALAR_COUNT }; // The scalars: whether it's set up, and the shots fired

typedef struct view {
    void *a[ARRAYS];
} view;

static uint32_t elements(const plan *p, const uint32_t a)
{
    return a <= VZ ? p->movers : a <= SHOTS ? p->turrets : a == HEAT ? p->notes : SCALAR_COUNT;
}

#define F(v, k) ((float *)(v)->a[k])
#define I(v, k) ((int32_t *)(v)->a[k])

// The bits a float has, to tell a change of -0 from none
static inline uint32_t bits(const float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static void setup(const view *v, const plan *p)
{
    float *px = F(v, PX), *py = F(v, PY), *pz = F(v, PZ), *vx = F(v, VX), *vy = F(v, VY), *vz = F(v, VZ);
    for (uint32_t i = 0; i < p->movers; i++) {
        const int32_t h = tide_hash_i2((tide_int2){(int32_t)i, 0});
        const int32_t g = tide_hash_i2((tide_int2){(int32_t)i, 1});
        const float place = 100.0f / 65536.0f;
        px[i] = (float)(h & 0xFFFF) * place;
        py[i] = (float)(g & 0xFFFF) * place;
        pz[i] = (float)((h ^ g) & 0xFFFF) * place;
        const bool moves = p->spread ? i % 100u < p->moving : i < p->first;
        vx[i] = moves ? (float)(((h >> 16) & 0xFF) - 128) * 0.25f : 0.0f;
        vy[i] = moves ? (float)(((g >> 16) & 0xFF) - 128) * 0.25f : 0.0f;
        vz[i] = moves ? (float)(((h >> 24) & 0xFF) - 128) * 0.25f : 0.0f;
    }
    for (uint32_t i = 0; i < p->turrets; i++) {
        const int32_t h = tide_hash_i2((tide_int2){(int32_t)i, 3});
        F(v, TX)[i] = (float)(h & 0xFF) * (100.0f / 256.0f);
        F(v, TY)[i] = 0.0f * (100.0f / 256.0f);
        F(v, TZ)[i] = (float)((h >> 8) & 0xFF) * (100.0f / 256.0f);
        F(v, COOL)[i] = (float)((h >> 16) & 0x3FF) * (1.0f / 1024.0f);
        F(v, RELOAD)[i] = 0.25f + (float)((h >> 6) & 0x3FF) * (1.0f / 1024.0f);
        I(v, SLOT)[i] = (int32_t)(i % p->notes);
        I(v, SHOTS)[i] = 0;
    }
    memset(v->a[HEAT], 0, (size_t)p->notes * sizeof(int32_t));
    I(v, SCALARS)[READY] = 1;
    I(v, SCALARS)[SCORE] = 0;
}

// Movers from `from` to `to`, branches as selects so the loop goes with SIMD.
// Returns which of the six arrays it changed a bit of, a bit each (PX on):
// inlined, so where that's not wanted, finding it out costs nothing.
__attribute__((always_inline)) static inline uint32_t move(const view *v, const uint32_t from, const uint32_t to)
{
    float *restrict x = F(v, PX), *restrict y = F(v, PY), *restrict z = F(v, PZ);
    float *restrict vx = F(v, VX), *restrict vy = F(v, VY), *restrict vz = F(v, VZ);
    const float dt = VERSUS_DT;
    uint32_t cx = 0, cy = 0, cz = 0, cvx = 0, cvy = 0, cvz = 0;
    for (uint32_t i = from; i < to; i++) {
        const float u = vx[i], w = vy[i], s = vz[i];
        const float a = x[i] + u * dt, b = y[i] + w * dt, c = z[i] + s * dt;
        const bool al = a < 0.0f, ah = a > 100.0f, bl = b < 0.0f, bh = b > 100.0f, cl = c < 0.0f, ch = c > 100.0f;
        const float na = al ? -a : ah ? 200.0f - a : a, nb = bl ? -b : bh ? 200.0f - b : b, nc = cl ? -c : ch ? 200.0f - c : c;
        const float nu = al || ah ? -u : u, nw = bl || bh ? -w : w, ns = cl || ch ? -s : s;
        cx |= bits(na) ^ bits(x[i]);
        cy |= bits(nb) ^ bits(y[i]);
        cz |= bits(nc) ^ bits(z[i]);
        cvx |= bits(nu) ^ bits(u);
        cvy |= bits(nw) ^ bits(w);
        cvz |= bits(ns) ^ bits(s);
        x[i] = na;
        y[i] = nb;
        z[i] = nc;
        vx[i] = nu;
        vy[i] = nw;
        vz[i] = ns;
    }
    return (cx != 0) << PX | (cy != 0) << PY | (cz != 0) << PZ | (cvx != 0) << VX | (cvy != 0) << VY | (cvz != 0) << VZ;
}

// Turrets from `from` to `to`, one after another as Tide's go, since each
// that fires counts its shot in the list. `heat` gets a 1 for each block of
// the list (of `block` elements) it changed. Returns which of COOL and SHOTS
// it changed, and whether the score did (SCALARS).
static uint32_t aim(const view *v, const uint32_t from, const uint32_t to, const uint32_t block, uint8_t *heat)
{
    float *cool = F(v, COOL);
    const float *reload = F(v, RELOAD);
    const int32_t *slot = I(v, SLOT);
    int32_t *shots = I(v, SHOTS), *notes = I(v, HEAT);
    uint32_t changed = 0, cooled = 0;
    for (uint32_t i = from; i < to; i++) {
        const float was = cool[i];
        float c = was - VERSUS_DT;
        if (c <= 0.0f) {
            c = c + reload[i];
            shots[i]++;
            I(v, SCALARS)[SCORE]++;
            notes[slot[i]]++;
            changed |= 1u << SHOTS | 1u << SCALARS;
            if (heat) heat[(uint32_t)slot[i] / block] = 1;
        }
        cooled |= bits(c) ^ bits(was);
        cool[i] = c;
    }
    return changed | (cooled != 0) << COOL;
}

static uint64_t view_digest(const view *v, const plan *p)
{
    uint64_t sum = 0;
    const bool ready = I(v, SCALARS)[READY] != 0; // Or a world from before the first tick, with nothing in it yet
    for (uint32_t i = 0; ready && i < p->movers; i++) {
        const mover_item item = {{F(v, PX)[i], F(v, PY)[i], F(v, PZ)[i]}, {F(v, VX)[i], F(v, VY)[i], F(v, VZ)[i]}};
        sum += versus_item(&item, sizeof item);
    }
    for (uint32_t i = 0; ready && i < p->turrets; i++) {
        const turret_item item = {{F(v, TX)[i], F(v, TY)[i], F(v, TZ)[i]}, F(v, COOL)[i], F(v, RELOAD)[i], I(v, SLOT)[i], I(v, SHOTS)[i]};
        sum += versus_item(&item, sizeof item);
    }
    for (uint32_t i = 0; ready && i < p->notes; i++) {
        const note_item item = {(int32_t)i, I(v, HEAT)[i]};
        sum += versus_item(&item, sizeof item);
    }
    return sum + versus_item(&I(v, SCALARS)[SCORE], sizeof(int32_t));
}

static double since(const uint64_t start)
{
    return (double)(tide_time_now_ns() - start) / 1e6;
}

// What both ways do around their own keeping, hashing and restoring: the
// periods of period.h, the ring's bookkeeping and the times of the parts.
typedef struct way {
    plan p;
    uint32_t ticks, mismatches;
    uint64_t *hashes;
    double part[PART_COUNT];
    view live;
    void (*keep)(struct way *w, uint32_t slot, const tide_jobs *jobs);
    void (*restore)(struct way *w, uint32_t slot, const tide_jobs *jobs);
    uint64_t (*hash)(struct way *w, const tide_jobs *jobs);
    void (*step)(struct way *w, const tide_jobs *jobs); // A tick of the live world
    view (*slot_view)(const struct way *w, uint32_t slot);
} way;

// Movers in pieces on threads from about half a million, as particles find
// pays on a 12-core Ryzen 9 7900X, each a whole number of blocks
#define MOVE_ENOUGH 500000u
#define BLOCK 4096u // Elements of 4 bytes: 16 KiB

static void step_kept(way *w, const tide_jobs *jobs)
{
    const uint64_t kept = tide_time_now_ns();
    w->keep(w, w->ticks % w->p.kept, jobs);
    const uint64_t ticked = tide_time_now_ns();
    w->part[PART_SNAPSHOT] += (double)(ticked - kept) / 1e6;
    w->step(w, jobs);
    w->part[PART_TICK] += since(ticked);
    w->ticks++;
}

static void period(void *state, const tide_jobs *jobs)
{
    way *w = state;
    for (uint32_t k = 0; k < PART_COUNT; k++) w->part[k] = 0.0;
    for (uint32_t i = 0; i < w->p.every; i++) {
        step_kept(w, jobs);
        const uint64_t start = tide_time_now_ns();
        w->hashes[w->ticks % w->p.kept] = w->hash(w, jobs);
        w->part[PART_HASH] += since(start);
    }
    const uint32_t back = plan_back(&w->p, w->ticks);
    if (!back) return;
    const uint32_t now = w->ticks;
    const uint64_t start = tide_time_now_ns();
    w->ticks -= back;
    w->restore(w, w->ticks % w->p.kept, jobs);
    w->part[PART_RESTORE] += since(start);
    while (w->ticks < now) step_kept(w, jobs);
    const uint64_t hashed = tide_time_now_ns();
    w->mismatches += w->hash(w, jobs) != w->hashes[now % w->p.kept];
    w->part[PART_HASH] += since(hashed);
}

static void way_parts(const void *state, double *ms)
{
    const way *w = state;
    for (uint32_t k = 0; k < PART_COUNT; k++) ms[k] = w->part[k];
}

static uint64_t way_digest(const void *state)
{
    const way *w = state;
    uint64_t sum = view_digest(&w->live, &w->p) + versus_item(&w->mismatches, sizeof w->mismatches);
    if (w->ticks) {
        const view before = w->slot_view(w, (w->ticks - 1u) % w->p.kept);
        const uint64_t digest = view_digest(&before, &w->p);
        sum += versus_item(&digest, sizeof digest);
    }
    return sum;
}

// ---------------------------------------------------------------------------
// C: the world in one block, kept, hashed and restored whole

typedef struct whole {
    way w;
    size_t at[ARRAYS]; // Where each array starts in the block
    size_t bytes;      // The block's
    uint8_t *block;
    uint8_t *ring;     // `kept` blocks, one after another
    uint64_t *units;   // Each unit's hash, for the hash of them
} whole;

// The hash goes a unit at a time, each hashed apart and then their hashes
// together, so threads can take units, and the number is the same however
// many do. Copies and hashes go on threads past a few megabytes, where they
// pay for waking them.
#define UNIT (64u * 1024u)
#define BYTES_ENOUGH (4u * 1024u * 1024u)

static view whole_view(const whole *s, uint8_t *base)
{
    view v;
    for (uint32_t a = 0; a < ARRAYS; a++) v.a[a] = base + s->at[a];
    return v;
}

static view whole_slot(const way *w, const uint32_t slot)
{
    const whole *s = (const whole *)w;
    return whole_view(s, s->ring + (size_t)slot * s->bytes);
}

typedef struct copy_job {
    uint8_t *to;
    const uint8_t *from;
    size_t bytes;
    uint32_t pieces;
} copy_job;

static void copy_piece(void *context, const uint32_t piece)
{
    const copy_job *j = context;
    const size_t from = j->bytes * piece / j->pieces, to = j->bytes * (piece + 1u) / j->pieces;
    memcpy(j->to + from, j->from + from, to - from);
}

static void copy_bytes(uint8_t *to, const uint8_t *from, const size_t bytes, const tide_jobs *jobs)
{
    copy_job j = {to, from, bytes, versus_pieces(jobs, (uint32_t)(bytes / UNIT), BYTES_ENOUGH / UNIT)};
    versus_for(jobs, j.pieces, copy_piece, &j);
}

static void whole_keep(way *w, const uint32_t slot, const tide_jobs *jobs)
{
    whole *s = (whole *)w;
    copy_bytes(s->ring + (size_t)slot * s->bytes, s->block, s->bytes, jobs);
}

static void whole_restore(way *w, const uint32_t slot, const tide_jobs *jobs)
{
    whole *s = (whole *)w;
    copy_bytes(s->block, s->ring + (size_t)slot * s->bytes, s->bytes, jobs);
}

typedef struct hash_job {
    const whole *s;
    uint32_t units, pieces;
} hash_job;

static void hash_piece(void *context, const uint32_t piece)
{
    const hash_job *j = context;
    for (uint32_t u = j->units * piece / j->pieces; u < j->units * (piece + 1u) / j->pieces; u++) {
        const size_t from = (size_t)u * UNIT, to = from + UNIT < j->s->bytes ? from + UNIT : j->s->bytes;
        j->s->units[u] = wy_hash(j->s->block + from, to - from, u);
    }
}

static uint64_t whole_hash(way *w, const tide_jobs *jobs)
{
    whole *s = (whole *)w;
    const uint32_t units = (uint32_t)((s->bytes + UNIT - 1u) / UNIT);
    hash_job j = {s, units, versus_pieces(jobs, units, BYTES_ENOUGH / UNIT)};
    versus_for(jobs, j.pieces, hash_piece, &j);
    return wy_hash(s->units, (size_t)units * sizeof *s->units, s->bytes);
}

typedef struct move_job {
    const view *v;
    uint32_t count, pieces;
} move_job;

static void move_piece(void *context, const uint32_t piece)
{
    const move_job *j = context;
    (void)move(j->v, (uint32_t)((uint64_t)j->count * piece / j->pieces), (uint32_t)((uint64_t)j->count * (piece + 1u) / j->pieces));
}

static void whole_step(way *w, const tide_jobs *jobs)
{
    if (!I(&w->live, SCALARS)[READY]) { // Tide's first tick spawns them, and they move from the next
        setup(&w->live, &w->p);
        return;
    }
    move_job j = {&w->live, w->p.movers, versus_pieces(jobs, w->p.movers, MOVE_ENOUGH)};
    versus_for(jobs, j.pieces, move_piece, &j);
    (void)aim(&w->live, 0, w->p.turrets, BLOCK, NULL);
}

static void *make_whole(const versus_flag *f)
{
    whole *s = tide_alloc_zeroed(1, sizeof *s);
    s->w.p = plan_of(f);
    // Arrays as big as these each start a page in when they have blocks of
    // their own, and then a store to one and a load from the next share their
    // address's low bits, which the CPU takes for the same address until it
    // knows better ("4K aliasing"). Apart by a few cache lines more than
    // their size, they never do.
    size_t bytes = 0;
    for (uint32_t a = 0; a < ARRAYS; a++) {
        s->at[a] = bytes;
        bytes += ((size_t)elements(&s->w.p, a) * 4u + 63u) / 64u * 64u + 64u * 3u;
    }
    s->bytes = bytes;
    s->block = tide_alloc_zeroed(1, bytes);
    s->ring = tide_alloc_zeroed(s->w.p.kept, bytes);
    s->units = tide_alloc_zeroed((bytes + UNIT - 1u) / UNIT, sizeof *s->units);
    s->w.hashes = tide_alloc_zeroed(s->w.p.kept, sizeof *s->w.hashes);
    s->w.live = whole_view(s, s->block);
    s->w.keep = whole_keep;
    s->w.restore = whole_restore;
    s->w.hash = whole_hash;
    s->w.step = whole_step;
    s->w.slot_view = whole_slot;
    return s;
}

static void free_whole(void *state)
{
    whole *s = state;
    free(s->block);
    free(s->ring);
    free(s->units);
    free(s->w.hashes);
    free(s);
}

const versus_way rollback_c = {"C", make_whole, period, way_digest, free_whole};
const versus_parts rollback_c_parts = way_parts;

// ---------------------------------------------------------------------------
// C, dirty: each array in blocks with versions, kept, hashed and restored by
// the blocks that changed

typedef struct block_meta {
    uint64_t version; // Of what the block holds: a tick that changes a byte of it gives it a new one
    uint64_t hashed;  // The version `hash` is of
    uint64_t hash;
} block_meta;

typedef struct world_copy {
    view v;
    block_meta *meta;
} world_copy;

typedef struct dirty {
    way w;
    uint32_t first[ARRAYS + 1u]; // Each array's first block, and the blocks in all
    world_copy now;
    world_copy *ring;
    uint8_t *changed; // A tick's: the blocks it changed
    uint64_t versions; // Given out: the next is one more
    uint64_t *hashes; // Each block's, for the hash of them
    uint32_t *stale;  // Blocks to copy or hash
} dirty;

static uint32_t dirty_blocks(const dirty *s, const uint32_t a)
{
    return s->first[a + 1u] - s->first[a];
}

// The bytes of block `k` of array `a` in use
static size_t block_bytes(const dirty *s, const uint32_t a, const uint32_t k)
{
    const uint32_t count = elements(&s->w.p, a);
    const uint32_t from = k * BLOCK;
    return (size_t)(count - from < BLOCK ? count - from : BLOCK) * 4u;
}

static world_copy make_copy(const dirty *s)
{
    world_copy c;
    for (uint32_t a = 0; a < ARRAYS; a++) {
        // Each a few cache lines further into its memory than the one before,
        // as big blocks start a page in, so no two share their address's low
        // bits (4K aliasing, as above)
        c.v.a[a] = (uint8_t *)tide_alloc_zeroed(1, (size_t)dirty_blocks(s, a) * BLOCK * 4u + 192u * ARRAYS) + 192u * a;
    }
    c.meta = tide_alloc_zeroed(s->first[ARRAYS] ? s->first[ARRAYS] : 1u, sizeof *c.meta);
    for (uint32_t k = 0; k < s->first[ARRAYS]; k++) c.meta[k].hashed = UINT64_MAX;
    return c;
}

static void free_copy(const world_copy *c)
{
    for (uint32_t a = 0; a < ARRAYS; a++) free((uint8_t *)c->v.a[a] - 192u * a);
    free(c->meta);
}

typedef struct block_job {
    dirty *s;
    world_copy *to;
    const world_copy *from;
    uint32_t count, pieces; // Of s->stale
} block_job;

// Copies of the stale blocks from one world to the other, with what's known of them
static void copy_blocks(void *context, const uint32_t piece)
{
    const block_job *j = context;
    for (uint32_t i = j->count * piece / j->pieces; i < j->count * (piece + 1u) / j->pieces; i++) {
        const uint32_t k = j->s->stale[i];
        uint32_t a = 0;
        while (j->s->first[a + 1u] <= k) a++;
        const size_t at = (size_t)(k - j->s->first[a]) * BLOCK * 4u;
        memcpy((uint8_t *)j->to->v.a[a] + at, (const uint8_t *)j->from->v.a[a] + at, block_bytes(j->s, a, k - j->s->first[a]));
        j->to->meta[k] = j->from->meta[k];
    }
}

// Whatever `to` has of a version other than `from`'s, which is what a tick
// changed since `to` was `from`, copied over
static void sync_copy(dirty *s, world_copy *to, const world_copy *from, const tide_jobs *jobs)
{
    uint32_t count = 0;
    size_t bytes = 0;
    for (uint32_t a = 0; a < ARRAYS; a++) {
        for (uint32_t k = s->first[a]; k < s->first[a + 1u]; k++) {
            if (to->meta[k].version == from->meta[k].version) continue;
            s->stale[count++] = k;
            bytes += block_bytes(s, a, k - s->first[a]);
        }
    }
    block_job j = {s, to, from, count, versus_pieces(jobs, (uint32_t)(bytes / UNIT), BYTES_ENOUGH / UNIT)};
    if (j.pieces > count) j.pieces = count ? count : 1u;
    versus_for(jobs, j.pieces, copy_blocks, &j);
}

static void dirty_keep(way *w, const uint32_t slot, const tide_jobs *jobs)
{
    dirty *s = (dirty *)w;
    sync_copy(s, &s->ring[slot], &s->now, jobs);
}

static void dirty_restore(way *w, const uint32_t slot, const tide_jobs *jobs)
{
    dirty *s = (dirty *)w;
    sync_copy(s, &s->now, &s->ring[slot], jobs);
}

static void hash_blocks(void *context, const uint32_t piece)
{
    const block_job *j = context;
    for (uint32_t i = j->count * piece / j->pieces; i < j->count * (piece + 1u) / j->pieces; i++) {
        const uint32_t k = j->s->stale[i];
        uint32_t a = 0;
        while (j->s->first[a + 1u] <= k) a++;
        const size_t at = (size_t)(k - j->s->first[a]) * BLOCK * 4u;
        block_meta *m = &j->to->meta[k];
        m->hash = wy_hash((const uint8_t *)j->to->v.a[a] + at, block_bytes(j->s, a, k - j->s->first[a]), k);
        m->hashed = m->version;
    }
}

static uint64_t dirty_hash(way *w, const tide_jobs *jobs)
{
    dirty *s = (dirty *)w;
    uint32_t count = 0;
    size_t bytes = 0;
    for (uint32_t a = 0; a < ARRAYS; a++) {
        for (uint32_t k = s->first[a]; k < s->first[a + 1u]; k++) {
            if (s->now.meta[k].hashed == s->now.meta[k].version) continue;
            s->stale[count++] = k;
            bytes += block_bytes(s, a, k - s->first[a]);
        }
    }
    block_job j = {s, &s->now, &s->now, count, versus_pieces(jobs, (uint32_t)(bytes / UNIT), BYTES_ENOUGH / UNIT)};
    if (j.pieces > count) j.pieces = count ? count : 1u;
    versus_for(jobs, j.pieces, hash_blocks, &j);
    for (uint32_t k = 0; k < s->first[ARRAYS]; k++) s->hashes[k] = s->now.meta[k].hash;
    return wy_hash(s->hashes, (size_t)s->first[ARRAYS] * sizeof *s->hashes, s->first[ARRAYS]);
}

typedef struct dirty_move_job {
    dirty *s;
    uint32_t blocks, pieces; // The movers' blocks, and pieces of them
} dirty_move_job;

static void dirty_move_piece(void *context, const uint32_t piece)
{
    const dirty_move_job *j = context;
    dirty *s = j->s;
    for (uint32_t k = j->blocks * piece / j->pieces; k < j->blocks * (piece + 1u) / j->pieces; k++) {
        const uint32_t to = (k + 1u) * BLOCK < s->w.p.movers ? (k + 1u) * BLOCK : s->w.p.movers;
        const uint32_t changed = move(&s->w.live, k * BLOCK, to);
        for (uint32_t a = PX; a <= VZ; a++) {
            if (changed >> a & 1u) s->changed[s->first[a] + k] = 1;
        }
    }
}

static void dirty_step(way *w, const tide_jobs *jobs)
{
    dirty *s = (dirty *)w;
    if (!I(&w->live, SCALARS)[READY]) {
        setup(&w->live, &w->p);
        memset(s->changed, 1, s->first[ARRAYS]);
    } else {
        dirty_move_job j = {s, dirty_blocks(s, PX), versus_pieces(jobs, w->p.movers, MOVE_ENOUGH)};
        if (j.pieces > j.blocks) j.pieces = j.blocks ? j.blocks : 1u;
        versus_for(jobs, j.pieces, dirty_move_piece, &j);
        for (uint32_t k = 0; k < dirty_blocks(s, COOL); k++) {
            const uint32_t to = (k + 1u) * BLOCK < w->p.turrets ? (k + 1u) * BLOCK : w->p.turrets;
            const uint32_t changed = aim(&w->live, k * BLOCK, to, BLOCK, s->changed + s->first[HEAT]);
            if (changed >> COOL & 1u) s->changed[s->first[COOL] + k] = 1;
            if (changed >> SHOTS & 1u) s->changed[s->first[SHOTS] + k] = 1;
            if (changed >> SCALARS & 1u) s->changed[s->first[SCALARS]] = 1;
        }
    }
    for (uint32_t k = 0; k < s->first[ARRAYS]; k++) {
        if (!s->changed[k]) continue;
        s->changed[k] = 0;
        s->now.meta[k].version = ++s->versions;
    }
}

static view dirty_slot(const way *w, const uint32_t slot)
{
    const dirty *s = (const dirty *)w;
    return s->ring[slot].v;
}

static void *make_dirty(const versus_flag *f)
{
    dirty *s = tide_alloc_zeroed(1, sizeof *s);
    s->w.p = plan_of(f);
    for (uint32_t a = 0; a < ARRAYS; a++) s->first[a + 1u] = s->first[a] + (elements(&s->w.p, a) + BLOCK - 1u) / BLOCK;
    const uint32_t blocks = s->first[ARRAYS];
    s->now = make_copy(s);
    s->ring = tide_alloc_zeroed(s->w.p.kept, sizeof *s->ring);
    for (uint32_t k = 0; k < s->w.p.kept; k++) s->ring[k] = make_copy(s);
    s->changed = tide_alloc_zeroed(blocks ? blocks : 1u, 1);
    s->hashes = tide_alloc_zeroed(blocks ? blocks : 1u, sizeof *s->hashes);
    s->stale = tide_alloc_zeroed(blocks ? blocks : 1u, sizeof *s->stale);
    s->w.hashes = tide_alloc_zeroed(s->w.p.kept, sizeof *s->w.hashes);
    s->w.live = s->now.v;
    s->w.keep = dirty_keep;
    s->w.restore = dirty_restore;
    s->w.hash = dirty_hash;
    s->w.step = dirty_step;
    s->w.slot_view = dirty_slot;
    return s;
}

static void free_dirty(void *state)
{
    dirty *s = state;
    free_copy(&s->now);
    for (uint32_t k = 0; k < s->w.p.kept; k++) free_copy(&s->ring[k]);
    free(s->ring);
    free(s->changed);
    free(s->hashes);
    free(s->stale);
    free(s->w.hashes);
    free(s);
}

const versus_way rollback_c_dirty = {"C, dirty", make_dirty, period, way_digest, free_dirty};
const versus_parts rollback_c_dirty_parts = way_parts;
