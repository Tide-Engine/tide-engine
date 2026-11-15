// Projection hashes kept by blocks smaller than a page (visibility_blocks.tide): each tick, every player's
// projection hash from what the last tick's check kept is the one a copy of the world hashes to with nothing kept,
// and a repair by what changed is the same, byte for byte, from what the tick's check kept as from the rows
// themselves (read before the check, which keeps them). The _checked variant works every hash out twice, and
// _memo_off keeps nothing.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/time.h"
#include "tide_test.h"

#define PLAYERS 4u
// Every check's projection hashes, hashed together, and the world's hash at the end: the same on every platform and
// build, memo on, memo off and with every hash worked out twice
#define PROJECTIONS UINT64_C(0xf3c2b779965662eb)
#define WORLD UINT64_C(0x6d4a6b2604242435)

// The changes a tick can be asked for (visibility_blocks.tide, Control.step), in the order they're made. `other` is
// the plot made just before the target, whose blocks are beside the target's in its pages. Targets and their
// neighbours keep their groups (id % 8 is 0, 4 or 7: visibility_blocks.tide, Thin); the second round's are 16 on
static const struct {
    int step;
    int target;
    int other;
    const char *what;
} STEPS[] = {
    {1, 784, 783, "the neighbour's grid's chunk, in the target's page"},
    {2, 784, 783, "the target's grid's chunk, where it is, the tick after"},
    {3, 784, 783, "the neighbour's small map, in place"},
    {4, 784, 783, "the target's small map, in place, the tick after"},
    {5, 784, 783, "the neighbour's big map, in place"},
    {6, 784, 783, "the target's big map, in place, the tick after"},
    {7, 784, 783, "the target's grid gains a chunk: its directory, beside others"},
    {8, 784, 783, "the target's list, let go"},
    {9, 784, 783, "the neighbour's list made with the target's old bytes, where it was"},
    {10, 784, 783, "the neighbour's list made again, with other bytes, where its own was"},
    {11, 0, -1, "most groups gone"},
    {12, 0, -1, "a group made: the directory packs its live entries together"},
    {13, 1100, -1, "a group's audience, its entry moved"},
    {16, 784, -1, "a plot snapped: its slot's snaps alone changed"},
    {14, 784, -1, "a plot gone"},
    {15, 0, -1, "another made in its slot"},
};

static tide_player_id player(const uint32_t i) { return tide_player_from_index(i); }

static void hashes(const tide_world *w, uint64_t out[PLAYERS])
{
    tide_player_id recipients[PLAYERS];
    for (uint32_t i = 0; i < PLAYERS; i++) recipients[i] = player(i);
    tide_game_api.client_hashes(w, recipients, PLAYERS, out);
}

typedef struct changes {
    uint8_t *plan, *now, *was;
    uint32_t plan_size, now_size, was_size;
} changes;

static changes changes_of(const tide_world *w, const tide_world *base, const uint32_t p)
{
    changes c = {0};
    uint32_t changed = 0;
    c.plan = tide_game_api.client_changes(w, base, w, player(p), &c.plan_size, &c.now, &c.now_size, &c.was, &c.was_size, &changed);
    return c;
}

static bool same_changes(const changes *a, const changes *b)
{
    return a->plan && b->plan && a->plan_size == b->plan_size && !memcmp(a->plan, b->plan, a->plan_size)
           && a->now_size == b->now_size && !memcmp(a->now, b->now, a->now_size) && a->was_size == b->was_size
           && !memcmp(a->was, b->was, a->was_size);
}

static void free_changes(changes *c)
{
    free(c->plan);
    free(c->now);
    free(c->was);
}

static tide_world world, base, fresh, twin;
static uint64_t projections = TIDE_HASH_START;
static int failures;

// A tick of the world from `base` (a copy of it before), then its checks
static void tick_and_check(const char *what)
{
    tide_world_copy(&base, &world);
    tide_world_tick(&world);
    changes read[PLAYERS];
    for (uint32_t p = 0; p < PLAYERS; p++) read[p] = changes_of(&world, &base, p);
    uint64_t kept[PLAYERS];
    hashes(&world, kept);
    for (uint32_t p = 0; p < PLAYERS; p++) {
        changes again = changes_of(&world, &base, p);
        if (!same_changes(&read[p], &again)) {
            printf("    %s: player %u's repair from what was kept isn't the one read from the rows\n", what, p);
            failures++;
        }
        free_changes(&again);
        free_changes(&read[p]);
    }
    memset(&fresh, 0, sizeof fresh);
    tide_world_copy(&fresh, &world);
    uint64_t whole[PLAYERS];
    hashes(&fresh, whole);
    tide_world_free(&fresh);
    for (uint32_t p = 0; p < PLAYERS; p++) {
        if (kept[p] == whole[p]) continue;
        printf("    %s: player %u's projection hash from what was kept (%016" PRIx64 ") isn't its world's (%016" PRIx64 ")\n", what,
               p, kept[p], whole[p]);
        failures++;
    }
    projections = tide_hash_more(projections, kept, sizeof kept);
}

TIDE_TEST(visibility_blocks_see_every_block_that_changed)
{
    tide_world_init(&world, 1.0f);
    tick_and_check("the start");
    for (int noisy = 0; noisy < 2; noisy++) {
        world.Control.noise = noisy != 0;
        tick_and_check("quiet");
        for (size_t i = 0; i < sizeof STEPS / sizeof STEPS[0]; i++) {
            // The same tick without the step, beside it: what the step changes shows to some player
            tide_world_copy(&twin, &world);
            world.Control.step = STEPS[i].step;
            world.Control.target = STEPS[i].target + noisy * 16;
            world.Control.other = STEPS[i].other < 0 ? -1 : STEPS[i].other + noisy * 16;
            tick_and_check(STEPS[i].what);
            tide_world_tick(&twin);
            // (Both hashed with nothing kept, by copies: worlds of their own, whose memos go with them; and the twin
            // with the world's Control, a public singleton, so only what the step changed tells them apart)
            twin.Control = world.Control;
            uint64_t with[PLAYERS], without[PLAYERS];
            memset(&fresh, 0, sizeof fresh);
            tide_world_copy(&fresh, &world);
            hashes(&fresh, with);
            tide_world_free(&fresh);
            memset(&fresh, 0, sizeof fresh);
            tide_world_copy(&fresh, &twin);
            hashes(&fresh, without);
            tide_world_free(&fresh);
            bool shows = false;
            for (uint32_t p = 0; p < PLAYERS; p++) shows |= with[p] != without[p];
            if (!shows) printf("    %s: no player's projection changed\n", STEPS[i].what);
            TIDE_CHECK(shows || noisy); // (Groups gone the first time are gone already the second)
            world.Control.step = 0;
            tick_and_check("the tick after");
        }
    }
    tide_world_free(&twin);
    TIDE_CHECK(failures == 0);
    projections = tide_hash_end(projections);
    const uint64_t at_end = tide_world_hash(&world);
    if (projections != PROJECTIONS) printf("    projections 0x%016" PRIx64 "\n", projections);
    if (at_end != WORLD) printf("    world 0x%016" PRIx64 "\n", at_end);
    TIDE_CHECK(projections == PROJECTIONS && at_end == WORLD);
    tide_world_free(&base);
    tide_world_free(&world);
}

// What the tick's check costs (no check of the times: they're the machine's) with nothing changing, and with one
// plot of every 61 growing its list each tick, so that pages change under most pieces with their rows' own blocks as
// they were
TIDE_TEST(visibility_blocks_measure)
{
    static tide_world measured;
    tide_world_init(&measured, 1.0f);
    uint64_t out[PLAYERS];
    for (int i = 0; i < 4; i++) {
        tide_world_tick(&measured);
        hashes(&measured, out);
    }
    const int ticks = 60;
    for (int noisy = 0; noisy < 2; noisy++) {
        measured.Control.noise = noisy != 0;
        uint64_t took = 0;
        for (int i = 0; i < ticks; i++) {
            tide_world_tick(&measured);
            const uint64_t start = tide_time_now_ns();
            hashes(&measured, out);
            took += tide_time_now_ns() - start;
        }
        printf("    %-22s %8.1f us a check, %d players\n", noisy ? "lists growing" : "nothing changing", (double)took / 1000.0 / ticks,
               (int)PLAYERS);
    }
    tide_world_free(&measured);
}
