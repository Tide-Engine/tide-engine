// Projection hashes that go by the pages written since the check before (visibility_walks.tide): each tick checked,
// every player's projection hash from what the check before kept is the one a copy of the world hashes to with
// nothing kept, and a repair by what changed is the same, byte for byte, from what the tick's check kept as from the
// rows themselves. Some ticks go unchecked, and the world is taken back to a tick before. The _checked variant works
// every hash out twice, and _memo_off keeps nothing.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#define PLAYERS 4u
// Every check's projection hashes, hashed together, and the world's hash at the end: the same in every build, memo
// on, memo off and with every hash worked out twice
#define PROJECTIONS UINT64_C(0x3e12b0ac6ef30465)
#define WORLD UINT64_C(0x4fd29b092c60a8ea)

typedef enum how { CHECKED, UNCHECKED_FIRST, TAKEN_BACK } how;

// The changes a tick can be asked for (visibility_walks.tide, Control.step), in order, each round at another target:
// a multiple of 64, the first row of its piece, and `other` the plot made just before it, the last row of the piece
// before, whose blocks are beside the target's in its pages and whose slot is beside its slot. So a page or a line of
// slots they share changes under a piece none of whose rows changed; which ones they share is the heap's to say, so
// every step goes at sixteen such pairs, noise in every other round
#define ROUNDS 16
static const struct {
    int step;
    bool alone; // No target: a plot made
    how how;
    const char *what;
} STEPS[] = {
    {1, false, CHECKED, "the target's and the neighbour's grids' chunks, in one page, in one tick"},
    {2, false, CHECKED, "the neighbour's grid's chunk alone"},
    {3, false, CHECKED, "the target's grid's chunk the tick after"},
    {4, false, UNCHECKED_FIRST, "the target's small map, and the tick after (both checked once) the neighbour's"},
    {2, false, CHECKED, "the neighbour's grid's chunk alone"},
    {4, false, UNCHECKED_FIRST, "the target's small map, its grid's page changed the tick before"},
    {6, false, TAKEN_BACK, "the target's list's element in a world taken back two ticks"},
    {8, false, CHECKED, "the target snapped as the neighbour in the slot beside it goes"},
    {9, true, CHECKED, "a plot made in the neighbour's slot"},
    {10, false, CHECKED, "another of the piece before gone, and a plot made"},
    {11, false, CHECKED, "the target snapped the tick after"},
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
    tide_writer plan = {0}, now = {0}, was = {0};
    if (!tide_game_api.client_changes(w, base, w, player(p), &plan, &now, &was, &changed)) {
        free(plan.data);
        free(now.data);
        free(was.data);
        return c;
    }
    c.plan = plan.data;
    c.plan_size = plan.size;
    c.now = now.data;
    c.now_size = now.size;
    c.was = was.data;
    c.was_size = was.size;
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

static tide_world world, base, fresh, twin, back;
static uint64_t projections = TIDE_HASH_START;
static int failures;

static void hashes_alone(const tide_world *w, uint64_t out[PLAYERS])
{
    memset(&fresh, 0, sizeof fresh);
    tide_world_copy(&fresh, w);
    hashes(&fresh, out);
    tide_world_free(&fresh);
}

// The world's checks: from what the check before kept, against a copy's with nothing kept, and repairs from the world
// `base` (the tick before) both ways
static void check(const char *what)
{
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
    uint64_t whole[PLAYERS];
    hashes_alone(&world, whole);
    for (uint32_t p = 0; p < PLAYERS; p++) {
        if (kept[p] == whole[p]) continue;
        printf("    %s: player %u's projection hash from what was kept (%016" PRIx64 ") isn't its world's (%016" PRIx64 ")\n", what,
               p, kept[p], whole[p]);
        failures++;
    }
    projections = tide_hash_more(projections, kept, sizeof kept);
}

static void tick(void)
{
    tide_world_copy(&base, &world);
    tide_world_tick(&world);
}

TIDE_TEST(visibility_walks_see_every_page_written_since)
{
    tide_world_init(&world, 1.0f);
    for (int k = 0; k < 4; k++) tick(); // (The plots are made in the third)
    check("the start");
    for (int round = 0; round < ROUNDS; round++) {
        world.Control.noise = (round & 1) != 0;
        tick();
        check("quiet");
        for (size_t i = 0; i < sizeof STEPS / sizeof STEPS[0]; i++) {
            const int target = STEPS[i].alone ? 0 : 64 * (round + 1);
            const int other = STEPS[i].alone ? -1 : 64 * (round + 1) - 1;
            if (STEPS[i].how == TAKEN_BACK) {
                // Two ticks checked, then the world as it was before them
                tide_world_copy(&back, &world);
                for (int k = 0; k < 2; k++) {
                    tick();
                    check("a tick to be taken back");
                }
                tide_world_copy(&world, &back);
            }
            tide_world_copy(&twin, &world);
            world.Control.step = STEPS[i].step;
            world.Control.target = target;
            world.Control.other = other;
            tick();
            if (STEPS[i].how == UNCHECKED_FIRST) {
                // Its neighbour's the tick after, both checked as one: the page changed twice since the check before,
                // and was hashed in between (as a world's own hash would), so the hash it had before isn't the kept one
                (void)tide_world_hash(&world);
                world.Control.step = STEPS[i].step + 1;
                tick();
            }
            check(STEPS[i].what);
            // The same ticks without the target's step, beside it: what it changes shows to some player
            twin.Control.step = 0;
            twin.Control.target = target;
            twin.Control.other = other;
            tide_world_tick(&twin);
            if (STEPS[i].how == UNCHECKED_FIRST) {
                twin.Control.step = STEPS[i].step + 1;
                tide_world_tick(&twin);
            }
            twin.Control = world.Control;
            uint64_t with[PLAYERS], without[PLAYERS];
            hashes_alone(&world, with);
            hashes_alone(&twin, without);
            bool shows = false;
            for (uint32_t p = 0; p < PLAYERS; p++) shows |= with[p] != without[p];
            if (!shows) printf("    %s: no player's projection changed\n", STEPS[i].what);
            TIDE_CHECK(shows);
            world.Control.step = 0;
            tick();
            check("the tick after");
        }
    }
    tide_world_free(&twin);
    tide_world_free(&back);
    TIDE_CHECK(failures == 0);
    projections = tide_hash_end(projections);
    const uint64_t at_end = tide_world_hash(&world);
    if (projections != PROJECTIONS) printf("    projections 0x%016" PRIx64 "\n", projections);
    if (at_end != WORLD) printf("    world 0x%016" PRIx64 "\n", at_end);
    TIDE_CHECK(projections == PROJECTIONS && at_end == WORLD);
    tide_world_free(&base);
    tide_world_free(&world);
}
