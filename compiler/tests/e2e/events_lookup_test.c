#include <stdlib.h>

#include "game.h"
#include "tide_test.h"

static void operation(tide_world *w, const int32_t op, const tide_entity target)
{
    w->Cases.operation = op;
    w->Cases.target = target;
    tide_world_tick(w);
}

TIDE_TEST(events_lookup_validates_handles_and_other_archetypes)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    operation(&w, 1, w.Cases.first);
    operation(&w, 1, w.Cases.second);
    operation(&w, 1, w.Cases.gapped); // A gap in indices must use the actual row.
    TIDE_CHECK(tide_get_Health(&w, w.Cases.first).value == 93);
    TIDE_CHECK(tide_get_Health(&w, w.Cases.second).value == 93);
    TIDE_CHECK(tide_get_Health(&w, w.Cases.gapped).value == 93);
    operation(&w, 1, w.Cases.other); // Live, but no Health: global handler still runs.
    operation(&w, 7, (tide_entity){0}); // World event: global handler still runs.
    TIDE_CHECK(w.Cases.hits == 5);
    operation(&w, 1, (tide_entity){UINT32_MAX, 1});
    operation(&w, 1, (tide_entity){w.Cases.first.index, w.Cases.first.generation + 2u});
    TIDE_CHECK(w.Cases.hits == 5);
    tide_world_free(&w);
}

TIDE_TEST(events_lookup_falls_back_after_swapping_the_first_row)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    const tide_entity gone = w.Cases.first;
    operation(&w, 2, (tide_entity){0}); // Last Health moves to row zero.
    operation(&w, 1, w.Cases.second);
    operation(&w, 1, w.Cases.gapped);
    operation(&w, 1, gone);
    TIDE_CHECK(tide_get_Health(&w, w.Cases.second).value == 93);
    TIDE_CHECK(tide_get_Health(&w, w.Cases.gapped).value == 93);
    TIDE_CHECK(w.Cases.hits == 2);
    tide_world_free(&w);
}

TIDE_TEST(events_lookup_checks_reused_generations_and_pending_spawns)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    const tide_entity gone = w.Cases.last;
    operation(&w, 9, (tide_entity){0}); // Remove the gapped last row first.
    operation(&w, 3, (tide_entity){0});
    operation(&w, 4, (tide_entity){0}); // Spawn and Hit are applied in queue order.
    TIDE_REQUIRE(w.Cases.fresh.index == gone.index);
    TIDE_REQUIRE(w.Cases.fresh.generation != gone.generation);
    TIDE_CHECK(tide_get_Health(&w, w.Cases.fresh).value == 97);
    operation(&w, 1, gone);
    TIDE_CHECK(w.Cases.hits == 1);
    operation(&w, 1, w.Cases.fresh);
    TIDE_CHECK(tide_get_Health(&w, w.Cases.fresh).value == 90);
    TIDE_CHECK(w.Cases.hits == 2);
    tide_world_free(&w);
}

TIDE_TEST(events_lookup_handles_archetype_moves_and_an_empty_candidate_table)
{
    tide_world w = {0};
    tide_world_init(&w, 1.0f);
    operation(&w, 5, w.Cases.second);
    TIDE_CHECK(tide_has_Blocked(&w, w.Cases.second));
    TIDE_CHECK(tide_get_Health(&w, w.Cases.second).value == 100);
    TIDE_CHECK(w.Cases.hits == 1); // The live excluded target reaches the global handler.
    operation(&w, 6, w.Cases.second);
    TIDE_CHECK(!tide_has_Blocked(&w, w.Cases.second));
    TIDE_CHECK(tide_get_Health(&w, w.Cases.second).value == 89);
    TIDE_CHECK(w.Cases.hits == 2);
    operation(&w, 8, (tide_entity){0});
    operation(&w, 7, (tide_entity){0});
    operation(&w, 1, w.Cases.second);
    TIDE_CHECK(w.Cases.hits == 3);
    tide_world_free(&w);
}

TIDE_TEST(events_lookup_keeps_snapshots_and_unpacked_worlds_consistent)
{
    tide_world w = {0}, copy = {0}, back = {0};
    tide_world_init(&w, 1.0f);
    tide_world_copy(&copy, &w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_world_pack(&w, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&back, bytes, size));
    free(bytes);
    const uint64_t old_hash = tide_world_hash(&copy);
    operation(&w, 1, w.Cases.first);
    TIDE_CHECK(tide_world_hash(&copy) == old_hash); // Writing through the probe still owns its pages.
    operation(&copy, 1, copy.Cases.first);
    operation(&back, 1, back.Cases.first);
    TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&copy));
    TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&back));
    tide_world_free(&back);
    tide_world_free(&copy);
    tide_world_free(&w);
}
