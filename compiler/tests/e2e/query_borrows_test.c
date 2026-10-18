#include "game.h"
#include "tide_test.h"

TIDE_TEST(query_borrows_project_rows_preserve_filters_and_forward_capabilities)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Checks *c = &world.Checks;
    TIDE_CHECK(c->total == 50 && c->projected == 100 && c->filtered == 50 && c->method == 150);
    TIDE_CHECK(c->pairs == 504 && c->swapped == 405 && c->named == 504 && c->empty == 0);
    TIDE_CHECK(c->actions == 2);
    TIDE_CHECK(c->sorted == 10);
    TIDE_CHECK(world.ProbeGrid.total == 200);
    TIDE_CHECK(world.ActionChecks.total == 109);
    TIDE_CHECK(c->before == 50 && c->after == 47 && c->calls == 0 && !c->skipped);
    TIDE_CHECK(tide_get_Value(&world, c->first).number == 8);
    TIDE_CHECK(c->tasks == 0);
    tide_world_tick(&world);
    TIDE_CHECK(c->tasks == 1);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
}

TIDE_TEST(query_borrows_replay_without_changing_retained_worlds)
{
    tide_world world = {0}, snapshot = {0}, replay = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_copy(&snapshot, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    tide_world_tick(&world);
    TIDE_CHECK(world.ActionChecks.total == 109);
    TIDE_CHECK(tide_world_hash(&snapshot) == before);
    TIDE_CHECK(tide_get_Value(&snapshot, snapshot.Checks.first).number == 10);
    tide_world_copy(&replay, &snapshot);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&replay));
    tide_world_tick(&world);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&replay));
    tide_world_free(&replay);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
