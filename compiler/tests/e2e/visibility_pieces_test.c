// Projection hashes kept 64 rows at a time (visibility_pieces.tide): each tick,
// every player's projection hash from what the last tick's check kept is the
// one a copy of the world hashes to with nothing kept, and a repair by what
// changed is the same, byte for byte, from what the tick's check kept as from
// the rows themselves (read before the check, which keeps them).

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#define PLAYERS 4u
// Every check's projection hashes, hashed together, and the world's hash at the end: the same on every platform and
// build, memo on, memo off and with every hash worked out twice (the _checked and _memo_off variants pin them too)
#define PROJECTIONS UINT64_C(0x1a126f2f5372bcb9)
#define WORLD UINT64_C(0x81b2108f888cafd7)

// The changes a tick can be asked for (visibility_pieces.tide, Control.step), in the order they're made, each of a
// cell of its own in a piece of its own (and 12 on, which has the same player and policy, with the noise). A cell
// goes to player id % 4; its Secret is denied that player but where id % 3 is 2
static const struct {
    int step;
    int target;
    const char *what;
} STEPS[] = {
    {1, 1197, "a cell's group's audience"},
    {5, 1585, "a cell of a grid, where it is"},
    {6, 1682, "an element of a list, where it is"},
    {7, 1779, "a policy, where it is"},
    {12, 1876, "a lane"},
    {13, 2361, "a cell's Visibility, another group"},
    {15, 2556, "a policy, gone"},
    {8, 1973, "a component gained: another table"},
    {9, 1973, "...and lost"},
    {10, 2070, "a cell's group, gone"},
    {2, 0, "the zone's group's audience"},
    {16, 0, "the other group's audience, which the cell given it goes by"},
    {3, 0, "the zone's root, another group"},
    {11, 0, "a cell, new"},
    {4, 0, "holes past a 32nd of the table, filled from its end"},
    {14, 0, "the zone's root, no Visibility"},
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
    // Repairs read from the rows: the world ticked since its last check, so what that kept is of another world
    changes read[PLAYERS];
    for (uint32_t p = 0; p < PLAYERS; p++) read[p] = changes_of(&world, &base, p);
    uint64_t kept[PLAYERS];
    hashes(&world, kept); // The tick's check, which keeps what each row came to
    // ...and from what it kept
    for (uint32_t p = 0; p < PLAYERS; p++) {
        changes again = changes_of(&world, &base, p);
        if (!same_changes(&read[p], &again)) {
            printf("    %s: player %u's repair from what was kept isn't the one read from the rows\n", what, p);
            failures++;
        }
        free_changes(&again);
        free_changes(&read[p]);
    }
    // A copy, hashed with nothing kept (a world of its own: its memo is new, and goes with it)
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

TIDE_TEST(visibility_pieces_pass_over_only_what_is_as_it_was)
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
            world.Control.target = STEPS[i].target + noisy * 12;
            tick_and_check(STEPS[i].what);
            tide_world_tick(&twin);
            uint64_t with[PLAYERS], without[PLAYERS];
            hashes(&world, with);
            hashes(&twin, without);
            bool shows = false;
            for (uint32_t p = 0; p < PLAYERS; p++) shows |= with[p] != without[p];
            if (!shows) printf("    %s: no player's projection changed\n", STEPS[i].what);
            TIDE_CHECK(shows || noisy); // (The zone's root has had its steps by then: they change nothing again)
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
