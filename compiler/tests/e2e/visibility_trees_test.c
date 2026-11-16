// Projection hashes of containers too big for one chunk of their list, and of rows looked at one by one where a piece
// is read again (visibility_trees.tide): each tick, every player's projection hash from what the last tick's check
// kept is the one a copy of the world hashes to with nothing kept, and the one the player's own world hashes to,
// loaded from what it's sent (its heap laid out anew, its maps with their live entries alone: what a container
// hashes to is its value's, however it's stored); and a repair by what changed is the same, byte for byte, from what
// the tick's check kept as from the rows themselves (read before the check, which keeps them). The _checked variant
// works every hash out twice, and _memo_off keeps nothing.

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
#define PROJECTIONS UINT64_C(0xb8926a775cd5784c)
#define WORLD UINT64_C(0x05e1e8ce2de7cf99)

// The changes a tick can be asked for (visibility_trees.tide, Control.step), in the order they're made; and the step
// the twin beside it takes that tick, where it isn't none (what tells the two apart is then smaller than a step)
static const struct {
    int step, twin;
    const char *what;
} STEPS[] = {
    {1, 0, "a big map's entry, where it is, in its first chunk"},
    {2, 0, "...and in its last"},
    {36, 37, "two keys put again, against the other way round: their orders alone"},
    {3, 0, "an entry gone and another added: the count the same"},
    {4, 0, "a key taken out and put again with its value: another order"},
    {5, 0, "a big map growing across chunks"},
    {6, 0, "...and most of its entries gone"},
    {7, 0, "...and fewer than a chunk holds: no tree"},
    {8, 0, "...and a tree again"},
    {9, 0, "a big set's entry added"},
    {10, 0, "...one gone and another added"},
    {11, 0, "a big list's element, where it is, in its first chunk"},
    {12, 0, "...and in its last"},
    {13, 0, "two elements of different chunks, each where the other was"},
    {14, 0, "a big list growing across chunks"},
    {15, 0, "...its first element gone: every other moves along"},
    {16, 0, "...and one element: no tree"},
    {17, 0, "...and a tree again"},
    {35, 0, "...and its last element gone"},
    {18, 0, "an element with padding, where it is"},
    {19, 0, "a list held by a big map's entry"},
    {20, 0, "a text of a big list of them"},
    {21, 0, "a map's entries one more than a chunk holds"},
    {22, 0, "...one of them, where it is"},
    {23, 0, "...and a chunk's worth again"},
    {24, 0, "a singleton's big map's entry"},
    {25, 0, "a singleton's big list's element"},
    {26, 0, "a singleton's big map: one gone, another added"},
    {27, 0, "three rows' small values, and a later row's list, in one piece"},
    {33, 0, "one of those values, as it was before"},
    {34, 0, "that list's element, where the list is now"},
    {38, 0, "a row given a component one player of its audience may have"},
    {39, 0, "...and without it again"},
    {28, 0, "a row's list, and a later row's small value, in one piece"},
    {29, 0, "a row of the last piece, which gets another"},
    {30, 0, "a row gone beside one that changes"},
    {31, 0, "a row made, and one that changes"},
    {32, 0, "a row snapped beside one that changes"},
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

static tide_world world, base, fresh, twin, own;
static uint64_t projections = TIDE_HASH_START;
static int failures;

// A tick of the world from `base` (a copy of it before), then its checks
static void tick_and_check(const char *what)
{
    tide_world_copy(&base, &world);
    tide_world_tick(&world);
    (void)tide_world_hash(&world); // As a server hashes its world first
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
        if (kept[p] != whole[p]) {
            printf("    %s: player %u's projection hash from what was kept (%016" PRIx64 ") isn't its world's (%016" PRIx64 ")\n",
                   what, p, kept[p], whole[p]);
            failures++;
        }
        // The player's own world, loaded from what it's sent: the same hash from another layout
        uint32_t size = 0;
        uint8_t *bytes = tide_game_api.client_state(&world, player(p), &size);
        memset(&own, 0, sizeof own);
        if (!bytes || !tide_game_api.load_client_state(&own, bytes, size)) {
            printf("    %s: player %u's world doesn't load\n", what, p);
            failures++;
        } else {
            const uint64_t theirs = tide_game_api.client_hash(&own, player(p));
            if (theirs != kept[p]) {
                printf("    %s: player %u's own world hashes to %016" PRIx64 ", the server's of it to %016" PRIx64 "\n", what, p,
                       theirs, kept[p]);
                failures++;
            }
        }
        tide_world_free(&own);
        free(bytes);
    }
    projections = tide_hash_more(projections, kept, sizeof kept);
}

TIDE_TEST(visibility_trees_see_every_change)
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
            world.Control.target = noisy;
            world.Control.tile = noisy * 64;
            tick_and_check(STEPS[i].what);
            twin.Control.step = STEPS[i].twin;
            twin.Control.target = world.Control.target;
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
            TIDE_CHECK(shows);
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

// What the tick's check costs (no check of the times: they're the machine's) with nothing changing, with one entry of
// the book's big map (8,000 entries) set where it is each tick, with one number of its big list (40,000), and with
// an entry gone and another added
TIDE_TEST(visibility_trees_measure)
{
    static tide_world measured;
    static const char *const kinds[] = {"nothing changing", "a big map's entry", "a big list's element", "an entry gone, one added"};
    tide_world_init(&measured, 1.0f);
    uint64_t out[PLAYERS];
    for (int i = 0; i < 4; i++) {
        tide_world_tick(&measured);
        (void)tide_world_hash(&measured);
        hashes(&measured, out);
    }
    const int ticks = 200;
    for (int kind = 0; kind < 4; kind++) {
        measured.Measure.kind = kind;
        uint64_t took = 0;
        for (int i = 0; i < ticks; i++) {
            tide_world_tick(&measured);
            (void)tide_world_hash(&measured);
            const uint64_t start = tide_time_now_ns();
            hashes(&measured, out);
            took += tide_time_now_ns() - start;
        }
        printf("    %-26s %8.1f us a check, %d players\n", kinds[kind], (double)took / 1000.0 / ticks, (int)PLAYERS);
    }
    tide_world_free(&measured);
}
