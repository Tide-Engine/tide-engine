// The C side of resident.tide: a stand-in for a library that keeps its state
// resident (tide/resident.h). Its state is eight counters in a block of the
// arena, with the block's place in the arena's user bytes; its stamp counts
// its steps; its digest is the counters' sum; its canonical bytes are the
// counters themselves.

#include <stdint.h>
#include <string.h>

#include "game.h"
#include "tide/resident.h"
#include "tide/rows.h"

enum { CELLS = 8 };

typedef struct user { // The arena's user bytes: where the state is
    uint32_t block; // Offset of the counters from the user bytes themselves, 0 for none yet
} user;

static tide_arena *arena;
static uint64_t kind;
static int relinks;
static int packs, unpacks;

static int64_t *cells(void)
{
    user *u = tide_arena_user(arena);
    if (!u->block) {
        int64_t *block = tide_arena_alloc(arena, CELLS * sizeof(int64_t));
        memset(block, 0, CELLS * sizeof(int64_t));
        u->block = (uint32_t)((uint8_t *)block - (uint8_t *)u);
    }
    return (int64_t *)(void *)((uint8_t *)u + u->block);
}

static uint64_t digest_of(const int64_t *c)
{
    uint64_t sum = 0;
    for (int i = 0; i < CELLS; i++) sum += (uint64_t)c[i] * (uint64_t)(i + 1);
    return sum;
}

// The arena holds the state `r` names: restored when it doesn't.
static void at(const tide_resident *r)
{
    if (tide_arena_stamp(arena) != r->stamp || !r->pages) tide_arena_restore(arena, r);
}

static void pack(void *context, const tide_resident *r, tide_writer *w)
{
    (void)context;
    packs++;
    at(r);
    const int64_t *c = cells();
    tide_write_u64(w, r->stamp);
    for (int i = 0; i < CELLS; i++) tide_write_u64(w, (uint64_t)c[i]);
}

static bool unpack(void *context, tide_resident *r, tide_reader *rd, const uint32_t size)
{
    (void)context;
    unpacks++;
    if (size != 8u * (CELLS + 1)) return false;
    const uint64_t stamp = tide_read_u64(rd);
    tide_resident none = {0};
    tide_arena_restore(arena, &none); // An empty arena: the state is made from the bytes alone
    int64_t *c = cells();
    for (int i = 0; i < CELLS; i++) c[i] = (int64_t)tide_read_u64(rd);
    tide_arena_touch(arena, c, CELLS * sizeof(int64_t));
    tide_arena_commit(arena, r, stamp, digest_of(c));
    return !rd->failed;
}

static void relink(void *context, tide_arena *a)
{
    (void)context;
    (void)a;
    relinks++;
}

static void ready(void)
{
    if (arena) return;
    const tide_resident_kind k = {.name = "tally", .pack = pack, .unpack = unpack, .relink = relink};
    kind = tide_resident_register(&k);
    arena = tide_resident_arena(kind);
}

// The system Step: adds `which` to the counter at which % 8, and commits
void Step(tide_rows *rows, Tally *tally)
{
    (void)rows;
    ready();
    at(&tally->state);
    int64_t *c = cells();
    c[tally->which % CELLS] += tally->which;
    tide_arena_touch(arena, &c[tally->which % CELLS], sizeof c[0]);
    tide_arena_commit(arena, &tally->state, tally->state.stamp + 1u, digest_of(c));
    tally->seen = (int64_t)tally->state.digest;
}

// For the host: what the state `r` names holds, by restoring it
int64_t resident_test_cell(const tide_resident *r, const int i)
{
    ready();
    at(r);
    return cells()[i];
}

tide_arena *resident_test_arena(void)
{
    ready();
    return arena;
}

int resident_test_packs(void)
{
    return packs;
}

int resident_test_unpacks(void)
{
    return unpacks;
}

int resident_test_relinks(void)
{
    return relinks;
}
