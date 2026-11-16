// Repairs from the rows the ticks' checks wrote down (visibility_repairs.tide):
// a change a tick, picked at random, for hundreds of ticks; each tick every
// player's repair from worlds 1 to 36 ticks old is worked out from the journal
// and by walking every row of both worlds (a lineage with nothing kept), and
// the two must be the same bytes. Which way a repair went is counted
// (TIDE_CLIENT_CHANGES_STATS), so a repair that should have gone by the
// journal and walked, or the other way, fails too: one from a world older
// than the journal's nodes (16 to 32 here), one from a world no check left
// (ticked on its own, or not checked yet), one across checks that wrote
// nothing because most of the world kept changing. The _checked variant also
// has the game work each of the first out again itself; _memo_off keeps
// nothing, so every check writes every row and most write none: either way;
// _wide is the game with 160 players, whose audiences are too big to keep
// with each row written down, checked the same way.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#define PLAYERS 4u
#define HISTORY 40u
#define TICKS 240u
// Every repair's bytes, hashed together, and the world's hash at the end: the same on every platform and build, and
// in all three variants
#ifndef VISIBILITY_REPAIRS_WIDE
#define REPAIRS UINT64_C(0xfd3420e6cae6814c)
#define WORLD UINT64_C(0x38b24a1a8622905c)
#else // The _wide variant: 160 players (visibility_repairs_wide.tide), another layout and other bytes
#define REPAIRS UINT64_C(0x2d81a84a665643d7)
#define WORLD UINT64_C(0x2dc7444e8fcdf4f7)
#endif

extern uint64_t tide_client_changes_stats[3];

static tide_player_id player(const uint32_t i) { return tide_player_from_index((int32_t)i); }

static void hashes(const tide_world *w)
{
    tide_player_id recipients[PLAYERS];
    uint64_t out[PLAYERS];
    for (uint32_t i = 0; i < PLAYERS; i++) recipients[i] = player(i);
    tide_game_api.client_hashes(w, recipients, PLAYERS, out);
}

typedef struct changes {
    uint8_t *plan, *now, *was;
    uint32_t plan_size, now_size, was_size, changed;
    bool kept; // Written into `rooms`: not to free
} changes;

// Writers a session uses again (session.c, read_room): each repair's bytes written over the last one's, whatever
// room it had, bigger or smaller, and still the same bytes as a repair written into writers with none
static tide_writer rooms[3];

static changes changes_of(const tide_world *w, const tide_world *base, const void *lineage, const uint32_t p, const bool in_rooms)
{
    changes c = {.kept = in_rooms};
    tide_writer fresh[3] = {{0}};
    tide_writer *to = in_rooms ? rooms : fresh;
    if (!tide_game_api.client_changes(w, base, lineage, player(p), &to[0], &to[1], &to[2], &c.changed)) {
        for (uint32_t i = 0; i < 3u && !in_rooms; i++) free(to[i].data);
        c.kept = false;
        return c;
    }
    c.plan = to[0].data;
    c.plan_size = to[0].size;
    c.now = to[1].data;
    c.now_size = to[1].size;
    c.was = to[2].data;
    c.was_size = to[2].size;
    return c;
}

static bool same_changes(const changes *a, const changes *b)
{
    return a->plan && b->plan && a->plan_size == b->plan_size && !memcmp(a->plan, b->plan, a->plan_size)
           && a->now_size == b->now_size && !memcmp(a->now, b->now, a->now_size) && a->was_size == b->was_size
           && !memcmp(a->was, b->was, a->was_size) && a->changed == b->changed;
}

static void free_changes(changes *c)
{
    if (c->kept) return;
    free(c->plan);
    free(c->now);
    free(c->was);
}

static tide_world world, bent, history[HISTORY];
static tide_world plain; // Never a world: a lineage nothing is kept for, so a repair by it walks every row
static uint64_t repairs = TIDE_HASH_START;
static uint32_t tick, failures, by_journal, by_walk, named;

enum { EITHER = -1, WALKED = 0, FOUND = 1 };

// Every player's repair of `now` from `base`, both ways; `expect` says which way the first must have gone
static void compare(const tide_world *now, const tide_world *base, int expect, const char *what, const uint32_t age)
{
#ifdef TIDE_CLIENT_MEMO_OFF // Nothing kept: every check finds every row changed, and most write none for it
    if (expect == FOUND) expect = EITHER;
#endif
    for (uint32_t p = 0; p < PLAYERS; p++) {
        const uint64_t found_before = tide_client_changes_stats[0], walked_before = tide_client_changes_stats[1];
        changes first = changes_of(now, base, &world, p, false);
        const bool found = tide_client_changes_stats[0] == found_before + 1u && tide_client_changes_stats[1] == walked_before;
        const bool walked = tide_client_changes_stats[1] == walked_before + 1u && tide_client_changes_stats[0] == found_before;
        changes all = changes_of(now, base, &plain, p, true);
        if (tide_client_changes_stats[0] != found_before + (found ? 1u : 0u)) {
            printf("    tick %u, %s: a lineage with nothing kept didn't walk\n", tick, what);
            failures++;
        }
        if (!same_changes(&first, &all)) {
            if (failures < 20u)
                printf("    tick %u, %s, %u ticks: player %u's repair %s isn't the one from every row (plans of %u and %u bytes)\n",
                       tick, what, age, p, found ? "from the rows written down" : "(walked)", first.plan_size, all.plan_size);
            failures++;
        }
        if (!(found || walked) || (expect == FOUND && !found) || (expect == WALKED && !walked)) {
            if (failures < 20u) printf("    tick %u, %s, %u ticks: player %u's repair %s\n", tick, what, age, p, found ? "went by the rows written down" : "walked");
            failures++;
        }
        by_journal += found;
        by_walk += walked;
        if (first.plan) {
            repairs = tide_hash_more(repairs, first.plan, first.plan_size);
            repairs = tide_hash_more(repairs, first.now, first.now_size);
            repairs = tide_hash_more(repairs, first.was, first.was_size);
            // (A plan that names something: 4 bytes of mark, a count of records and a count of those gone)
            named += first.plan_size > 6u;
        }
        free_changes(&first);
        free_changes(&all);
    }
}

static uint32_t seed = 0x2545f491u;
static uint32_t roll(const uint32_t below)
{
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % below;
}

TIDE_TEST(visibility_repairs_from_the_rows_written_down_are_the_walks)
{
    // The steps a tick may take (visibility_repairs.tide), the commoner ones more than once
    static const int STEPS[] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 14, 15, 15, 16, 17, 17, 18, 18,
                                19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 29, 30, 30, 0, 0};
    static const uint32_t AGES[] = {1, 2, 3, 5, 9, 14, 20, 36};
    tide_world_init(&world, 1.0f);
    int next = 5000, groupless = 1300;
    for (tick = 0; tick < TICKS; tick++) {
        int step = tick < 3u ? 0 : STEPS[roll(sizeof STEPS / sizeof STEPS[0])];
        // (Thinning and every cell's value: a few times, not every thirtieth tick. Every cell's is more than half the
        // world's rows in one check, which is written down like any other while the checks around it write few)
        if ((step == 19 || step == 26) && roll(3u)) step = 3;
        // A cell of the first 1,299, one of the zone's, or one made since; a cell whose group goes is never asked again
        int target = (int)roll(1299u);
        if (roll(6u) == 0u) target = 10000 + (int)roll(200u);
        else if (roll(6u) == 0u && next > 5000) target = 5000 + (int)roll((uint32_t)(next - 5000));
        if (step == 25) target = groupless < 1400 ? groupless++ : 1399;
        world.Control.step = step;
        world.Control.target = target;
        world.Control.next = next;
        if (step == 17 || step == 18) next += 1;
        if (step == 27) next += 40;
        tide_world_tick(&world);
        // Before the tick's check the world is no check's: what was kept is of the tick before, and the repair walks
        if (tick > 0u && tick % 4u == 0u) compare(&world, &history[(tick - 1u) % HISTORY], WALKED, "before its check", 1u);
        hashes(&world);
        tide_world_copy(&history[tick % HISTORY], &world);
        for (size_t i = 0; i < sizeof AGES / sizeof AGES[0]; i++) {
            const uint32_t age = AGES[i];
            if (age > tick) continue;
            // (Nodes go by halves at 32 here: a world 16 to 31 ticks old may or may not still be one)
            const int expect = age >= 32u ? WALKED : age >= 16u ? EITHER : FOUND;
            compare(&world, &history[(tick - age) % HISTORY], expect, "the world from one", age);
        }
        // Two worlds of the past, either way round: by the journal still, though neither is the world the match ticks
        if (tick >= 8u && tick % 5u == 0u) {
            compare(&history[(tick - 2u) % HISTORY], &history[(tick - 7u) % HISTORY], FOUND, "a past world from an older one", 5u);
            compare(&history[(tick - 7u) % HISTORY], &history[(tick - 2u) % HISTORY], FOUND, "a past world from a newer one", 5u);
        }
        // A world of the same tick as one a check left, but another: ticked on its own from the one before, with a
        // change the match never made. Its hash is no node's, so it's walked
        if (tick >= 8u && tick % 11u == 0u) {
            tide_world_copy(&bent, &history[(tick - 3u) % HISTORY]);
            bent.Control.step = 3;
            bent.Control.target = 1299;
            tide_world_tick(&bent);
            TIDE_CHECK(bent.Time.tick == history[(tick - 2u) % HISTORY].Time.tick);
            compare(&world, &bent, WALKED, "the world from one no check left", 2u);
        }
    }
    // Six ticks in a row that change every cell, then nothing changing: the first four checks write their rows (more
    // than half the world's each), the two after them and the next ten write none without trying (a repair across
    // one walks), and the sixteenth of the run writes, finds few, and ends it. (Two ticks of nothing first: the
    // loop above may have ended on a check that wrote most rows)
    for (uint32_t i = 0; i < 28u; i++, tick++) {
        world.Control.step = i >= 2u && i < 8u ? 26 : 0;
        tide_world_tick(&world);
        hashes(&world);
        hashes(&world); // (Checked again as it is, as a hash asked for one player is: no check of the run's)
#ifdef TIDE_CLIENT_MEMO_OFF
        const int expect = EITHER;
#else
        const int expect = i >= 6u && i < 18u ? WALKED : FOUND;
#endif
        compare(&world, &history[(tick - 1u) % HISTORY], expect, "in a run of checks that wrote most rows", 1u);
        if (i >= 4u) compare(&world, &history[(tick - 2u) % HISTORY], expect == FOUND && i == 18u ? WALKED : expect, "in a run of checks that wrote most rows", 2u);
        tide_world_copy(&history[tick % HISTORY], &world);
    }
    printf("    %u repairs from the rows written down, %u walked; %u named something\n", by_journal, by_walk, named);
    TIDE_CHECK(failures == 0);
#ifndef TIDE_CLIENT_MEMO_OFF
    TIDE_CHECK(by_journal > 5000u && tide_client_changes_stats[2] > 0u);
#endif
    TIDE_CHECK(by_walk > 1000u && named > 4000u);
    repairs = tide_hash_end(repairs);
    const uint64_t at_end = tide_world_hash(&world);
    if (repairs != REPAIRS) printf("    repairs 0x%016" PRIx64 "\n", repairs);
    if (at_end != WORLD) printf("    world 0x%016" PRIx64 "\n", at_end);
    TIDE_CHECK(repairs == REPAIRS && at_end == WORLD);
    for (uint32_t i = 0; i < 3u; i++) {
        free(rooms[i].data);
        rooms[i] = (tide_writer){0};
    }
    tide_world_free(&bent);
    for (uint32_t i = 0; i < HISTORY; i++) tide_world_free(&history[i]);
    tide_world_free(&world);
}

// Whom entities came to (tide_game.client_came): the players a visibility group's audience gained in the checks
// since it was last asked, and nobody for a change that brings nobody anything. A session sends a repair asked
// for of a tick before such a check as the world is now (session.c, repair_as_it_is_now). A build that keeps
// nothing of a check (TIDE_CLIENT_MEMO_OFF, the tests' own) finds every group new each time, and says it of
// everyone a group has: of more players than it's true of, never of fewer.
static uint32_t came_to(const tide_world *w)
{
    tide_player_id recipients[PLAYERS];
    bool came[PLAYERS];
    for (uint32_t i = 0; i < PLAYERS; i++) recipients[i] = player(i);
    tide_game_api.client_came(w, recipients, PLAYERS, came);
    uint32_t bits = 0;
    for (uint32_t i = 0; i < PLAYERS; i++) bits |= came[i] ? 1u << i : 0u;
    return bits;
}

TIDE_TEST(visibility_came_says_whom_a_groups_audience_gained)
{
    static tide_world w;
    tide_world_init(&w, 1.0f);
    w.Control.next = 5000;
    for (uint32_t i = 0; i < 4u; i++) {
        tide_world_tick(&w);
        hashes(&w);
    }
    (void)came_to(&w); // The first checks made every group: asked, it starts from nothing
    TIDE_CHECK(came_to(&w) == 0u); // (No check since)
    // Cell 5's group is player 1's: given to player 3 alone, it came to player 3 and to nobody else
    w.Control.step = 1;
    w.Control.target = 5;
    tide_world_tick(&w);
    hashes(&w);
#ifdef TIDE_CLIENT_MEMO_OFF
    TIDE_CHECK(came_to(&w) & 1u << 3);
#else
    TIDE_CHECK(came_to(&w) == 1u << 3);
    TIDE_CHECK(came_to(&w) == 0u); // Said once
    // A lane everyone who has the cell has, and a policy: nothing came to anyone
    w.Control.step = 3;
    tide_world_tick(&w);
    hashes(&w);
    TIDE_CHECK(came_to(&w) == 0u);
    w.Control.step = 6;
    tide_world_tick(&w);
    hashes(&w);
    TIDE_CHECK(came_to(&w) == 0u);
    // Back to player 1, then two checks before it's asked: both are said, player 3's again among them
    w.Control.step = 2;
    tide_world_tick(&w);
    hashes(&w);
    w.Control.step = 1;
    tide_world_tick(&w);
    hashes(&w);
    TIDE_CHECK(came_to(&w) == (1u << 1 | 1u << 3));
    // The zone's group (players 0, 1 and 2) given to player 1 alone came to nobody; given back, to 0 and 2, not to 1
    w.Control.step = 20;
    tide_world_tick(&w);
    hashes(&w);
    TIDE_CHECK(came_to(&w) == 0u);
    w.Control.step = 21;
    tide_world_tick(&w);
    hashes(&w);
    TIDE_CHECK(came_to(&w) == (1u << 0 | 1u << 2));
    // A group made with an audience (a cell spawned: step 17) came to that audience
    w.Control.step = 17;
    w.Control.target = 5000;
    tide_world_tick(&w);
    hashes(&w);
    TIDE_CHECK(came_to(&w) != 0u);
    // Another world has said nothing
    static tide_world other;
    tide_world_copy(&other, &w);
    TIDE_CHECK(came_to(&other) == 0u);
    tide_world_free(&other);
#endif
    tide_world_free(&w);
}
