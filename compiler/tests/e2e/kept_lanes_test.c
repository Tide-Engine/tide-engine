#include <string.h>

#include "game.h"
#include "tide_test.h"

enum { WIDES = 2600, LOTS = 97, KEPT_TICKS = 4 };

// Every entity as its systems leave it after it ran for `ticks` ticks: each row read and wrote its own lanes,
// whichever archetype and chunk the run before it was in. `count`: how many there are.
static void check_world(const tide_world *w, const int ticks, int *count)
{
    static bool placed[WIDES], found[WIDES];
    static int rare[WIDES], chained[WIDES], order[WIDES];
    memset(placed, 0, sizeof placed);
    memset(found, 0, sizeof found);
    bool lot[LOTS] = {false}; // The ticks in 97 that Rare took rows in
    int read = 0;
    *count = 0;
    for (uint32_t slot = 0; slot < w->entities.next_unused; slot++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, slot);
        if (tide_entity_is_null(e) || !tide_has_Wide(w, e)) continue;
        const Wide v = tide_get_Wide(w, e);
        (*count)++;
        TIDE_REQUIRE(v.id >= 0 && v.id < WIDES && !found[v.id]);
        found[v.id] = true;
        TIDE_CHECK(v.x == (float)v.id * 0.25f && v.skip == (v.id % 5 == 0) && v.lot == v.id % LOTS);
        TIDE_CHECK(v.seen == ticks && v.out == v.x * 2.0f + (float)ticks);
        for (int k = 0; k < 24; k++) TIDE_CHECK(v.spare.at[k] == (k == 3 ? v.id + ticks : k == 5 && v.rare ? v.id : 0));
        TIDE_CHECK(v.at.x == 0.0f && v.at.y == 0.0f && v.at.z == 0.0f);
        TIDE_CHECK(tide_has_Tag(w, e) == (v.id % 3 == 0));
        TIDE_CHECK(v.reached == (v.skip ? 0 : ticks * (v.id % 3 == 0 ? 2 : 1)));
        // Its place in the order: one no other has
        TIDE_REQUIRE(v.order >= 0 && v.order < WIDES && !placed[v.order]);
        placed[v.order] = true;
        if (!v.skip) read += v.id % 7 + v.id;
        // Rare: at most once in these few ticks, in its own tick of the 97; Never: never
        TIDE_CHECK(v.never == 0 && (v.rare == 0 || v.rare == 1));
        if (v.rare) {
            TIDE_CHECK(!v.skip && v.rareAt == v.lot);
            lot[v.lot] = true;
        }
        rare[v.id] = v.rare;
        chained[v.id] = v.chained;
        order[v.id] = v.order;
    }
    TIDE_CHECK(w->Tally.n == *count && w->Tally.read == read);
    if (*count != WIDES) return;
    // Rare: a tick's rows are one lot's, another lot each tick, and every row of it that isn't skipped ran
    int lots = 0;
    for (int r = 0; r < LOTS; r++) lots += lot[r];
    TIDE_CHECK(lots == ticks);
    for (int id = 0; id < WIDES; id++) TIDE_CHECK(rare[id] == (id % 5 != 0 && lot[id % LOTS] ? 1 : 0));
    // Chain: entity 7 and every one after it in the entities' order, each tick
    for (int id = 0; id < WIDES; id++) TIDE_CHECK(chained[id] == (order[id] >= order[7] ? ticks : 0));
}

// A world written in place, and one written apart beside a snapshot kept each tick, as a client's is
TIDE_TEST(kept_lanes_rows_read_and_write_their_own)
{
    tide_world plain = {0}, apart = {0}, kept = {0};
    tide_world_init(&plain, 1.0f / 60.0f);
    tide_world_init(&apart, 1.0f / 60.0f);
    int ran = 0;
    for (int tick = 0; tick < KEPT_TICKS + 2; tick++) {
        tide_world_free(&kept);
        tide_world_copy(&kept, &apart);
        tide_world_tick(&apart);
        tide_world_tick(&plain);
        TIDE_CHECK(tide_world_hash(&plain) == tide_world_hash(&apart));
        TIDE_CHECK(plain.Tally.sum == apart.Tally.sum && plain.Tally.rare == apart.Tally.rare);
        if (plain.Tally.n == 0) continue; // Not spawned yet
        ran++;
        int in_place = 0, written_apart = 0;
        check_world(&plain, ran, &in_place);
        check_world(&apart, ran, &written_apart);
        TIDE_CHECK(in_place == WIDES && written_apart == WIDES);
    }
    TIDE_CHECK(ran >= KEPT_TICKS);
    TIDE_CHECK(plain.Tally.sum != 0.0f && plain.Tally.rare != 0);
    tide_world_free(&kept);
    tide_world_free(&apart);
    tide_world_free(&plain);
}
