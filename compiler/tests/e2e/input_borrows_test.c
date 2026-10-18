#include "game.h"
#include "tide_test.h"

TIDE_TEST(input_borrows_keep_edges_through_helpers_methods_actions_sort_and_parallel_steps)
{
    tide_world world = {0}, snapshot = {0}, replay = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    const Controls press = {.jump = true, .buttons = {.fire = true}, .amount = 7};
    tide_world_set_server_input(&world, press);
    tide_world_copy(&snapshot, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.downs == 1 && world.Seen.ups == 0);
    TIDE_CHECK(world.Seen.fires == 1 && world.Seen.releases == 0);
    TIDE_CHECK(world.Seen.amount == 21 && world.Seen.paired == 77);
    TIDE_CHECK(world.Seen.sorted == 3 && world.Seen.actions == 1 && world.Seen.parallelTotal == 28);
    TIDE_CHECK(world.Seen.events == 1);
    TIDE_CHECK(world.Seen.formatted);
    TIDE_CHECK(world.Seen.calls == 2 && world.Seen.ordered == 809);
    TIDE_CHECK(tide_world_hash(&snapshot) == before);
    tide_world_copy(&replay, &snapshot);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&replay));
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.downs == 1 && world.Seen.fires == 1 && world.Seen.actions == 1);
    TIDE_CHECK(world.Seen.sorted == 1 && world.Seen.parallelTotal == 0 && world.Seen.events == 1);
    tide_world_set_server_input(&world, (Controls){0});
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.ups == 1 && world.Seen.releases == 1);
    TIDE_CHECK(world.Seen.amount == 0 && world.Seen.paired == 0);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&replay);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
