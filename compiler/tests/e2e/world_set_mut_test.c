#include <stdlib.h>

#include "game.h"
#include "tide_test.h"

#ifdef TIDE_SET_MUT_THREADS
#include "tide/platform.h"
#endif

static tide_world world, shared, kept, replay;

static tide_entity entity_at(tide_world *w, const int i)
{
    tide_text_use(&w->heap, NULL);
    return *(const tide_entity *)tide_list_at(w->Roster.rows, i, sizeof(tide_entity));
}

static void check_rows(tide_world *w, const int tick)
{
    for (int i = 0; i < 5000; i++) {
        const tide_entity e = entity_at(w, i);
        const Counter counter = tide_get_Counter(w, e);
        const Seen seen = tide_get_Seen(w, e);
        TIDE_REQUIRE(counter.index == i && counter.value == (i == 0 ? 22 : 21));
        TIDE_REQUIRE(seen.own == 21 && seen.other == 21 && seen.stored == 20);
        TIDE_REQUIRE(seen.entered == (i == 0 && tick == 1 ? 1 : 22));
        if (i >= 2048 || i % 2 == 0) {
            TIDE_REQUIRE(tide_get_Indirect(w, e).value == 72);
            TIDE_REQUIRE(seen.helper == 41 && seen.ordered == 107);
        } else {
            TIDE_REQUIRE(tide_get_Indirect(w, e).value == 1);
            TIDE_REQUIRE(seen.helper == 0 && seen.ordered == 0);
        }
    }
}

TIDE_TEST(world_set_mut_updates_self_and_helpers_without_overwriting_other_rows)
{
    tide_world_init(&world, 1.0f);
    tide_world_init(&shared, 1.0f);
    const tide_entity valid = entity_at(&world, 0);
    world.Roster.absent = (tide_entity){valid.index, valid.generation + 1u};
    shared.Roster.absent = world.Roster.absent;
    for (int tick = 0; tick < 3; tick++) {
        tide_world_copy(&kept, &shared);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        tide_world_tick(&shared);
        check_rows(&world, tick + 1);
        check_rows(&shared, tick + 1);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&shared));
        TIDE_REQUIRE(tide_world_hash(&kept) == before);
        TIDE_REQUIRE(tide_scratch_mark() == 0);
    }
    tide_world_copy(&replay, &kept);
    tide_world_tick(&replay);
    TIDE_REQUIRE(tide_world_hash(&replay) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&world, bytes, size);
    tide_world_free(&replay);
    TIDE_REQUIRE(tide_world_unpack(&replay, bytes, size));
    free(bytes);
    TIDE_REQUIRE(tide_world_hash(&replay) == tide_world_hash(&world));
    tide_world_tick(&replay);
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_world_hash(&replay) == tide_world_hash(&world));
    tide_world_free(&world);
    tide_world_free(&shared);
    tide_world_free(&kept);
    tide_world_free(&replay);
}

TIDE_TEST(world_set_mut_replaces_the_active_container_ownership)
{
    tide_world_init(&world, 1.0f);
    world.Settings.mode = Mode_Heap;
    for (int tick = 0; tick < 4; tick++) {
        tide_world_copy(&kept, &world);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        const Bag bag = tide_get_Bag(&world, world.Roster.bag);
        TIDE_CHECK(bag.value == 21);
        tide_text_use(&world.heap, NULL);
        TIDE_CHECK(tide_list_count(bag.data) == 3);
        TIDE_CHECK(*(const int32_t *)tide_list_at(bag.data, 0, sizeof(int32_t)) == 21);
        TIDE_CHECK(*(const int32_t *)tide_list_at(bag.data, 2, sizeof(int32_t)) == 55);
        TIDE_CHECK(world.Results.copies == tick + 1 && world.Results.failures == 0);
        TIDE_CHECK(tide_world_hash(&kept) == before && tide_scratch_mark() == 0);
    }
    tide_world_free(&world);
    tide_world_free(&kept);
}

TIDE_TEST(world_set_mut_rebuilds_bindings_at_event_and_task_turns)
{
    tide_world_init(&world, 1.0f);
    world.Settings.mode = Mode_Events;
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Bag(&world, world.Roster.bag).value == 21);
    TIDE_CHECK(world.Results.started == 1 && world.Results.ended == 0);
    world.Settings.mode = Mode_None;
    tide_world_copy(&kept, &world);
    const uint64_t before = tide_world_hash(&kept);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Bag(&world, world.Roster.bag).value == 22);
    TIDE_CHECK(world.Results.started == 1 && world.Results.ended == 1);
    TIDE_CHECK(world.Results.failures == 0);
    TIDE_CHECK(tide_world_hash(&kept) == before);
    tide_world_copy(&replay, &kept);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&replay) == tide_world_hash(&world));
    tide_world_free(&world);
    tide_world_free(&kept);
    tide_world_free(&replay);
}

TIDE_TEST(world_set_mut_in_local_views)
{
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f);
    world.Settings.mode = Mode_None;
    tide_local_init(&local);
    const uint64_t before = tide_world_hash(&world);
    for (int frame = 0; frame < 4; frame++) {
        tide_draw_reset(&draw);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    }
    TIDE_CHECK(local.Viewer.seen == 63);
    TIDE_CHECK(tide_get_tide_local_component_LocalValue(&local, local.Viewer.entity).value == 21);
    TIDE_CHECK(tide_world_hash(&world) == before);
    tide_world_free(&world);
    tide_local_free(&local);
}

TIDE_TEST(world_set_mut_in_awaited_component_tasks)
{
    tide_world_init(&world, 1.0f);
    world.Settings.mode = Mode_Nested;
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Bag(&world, world.Roster.bag).value == 13);
    TIDE_CHECK(world.Results.started == 1 && world.Results.ended == 0);
    world.Settings.mode = Mode_None;
    tide_world_copy(&kept, &world);
    const uint64_t before = tide_world_hash(&kept);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Bag(&world, world.Roster.bag).value == 21);
    TIDE_CHECK(world.Results.started == 1 && world.Results.ended == 1 && world.Results.failures == 0);
    TIDE_CHECK(tide_world_hash(&kept) == before && tide_scratch_mark() == 0);
    tide_world_copy(&replay, &kept);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&replay) == tide_world_hash(&world));
    tide_world_free(&world);
    tide_world_free(&kept);
    tide_world_free(&replay);
}

TIDE_TEST(world_set_mut_in_authenticated_command_dispatch)
{
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_world_init(&world, 1.0f);
    world.Settings.mode = Mode_None;
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
    local.Viewer.submit = true;
    local.Viewer.target = world.Roster.bag;
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    uint32_t size = 0;
    const uint8_t *bytes = tide_local_command(&local, 0, &size);
    TIDE_REQUIRE(bytes && size);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Bag(&world, world.Roster.bag).value == 23);
    tide_world_free(&world);
    tide_local_free(&local);
}

#ifdef TIDE_SET_MUT_THREADS
TIDE_TEST(world_set_mut_context_is_local_to_threaded_invocations)
{
    const tide_jobs *jobs = tide_platform_jobs();
    tide_world_init(&world, 1.0f);
    tide_world_init(&shared, 1.0f);
    for (int tick = 0; tick < 3; tick++) {
        tide_world_copy(&kept, &shared);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        tide_world_tick_on(&shared, jobs);
        TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&shared));
        TIDE_REQUIRE(tide_world_hash(&kept) == before);
    }
    check_rows(&shared, 3);
    tide_world_free(&world);
    tide_world_free(&shared);
    tide_world_free(&kept);
}
#endif
