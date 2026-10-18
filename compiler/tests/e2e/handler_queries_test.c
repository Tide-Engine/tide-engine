#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world snapshot;
static tide_world replay;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

TIDE_TEST(handler_queries_observe_their_queue_turn_and_lifecycle)
{
    tide_world_init(&world, 1.0f / 60.0f);
    TIDE_CHECK(world.Seen.spawns == 4 && world.Seen.spawnedMatches == 7);
    world.Seen.action = 1;
    tide_world_tick(&world);
    const int32_t counts[] = {2, 3, 2, 1};
    const int32_t sums[] = {31, 61, 50, 30};
    for (int i = 0; i < 4; i++) {
        TIDE_CHECK(world.Seen.counts.at[i] == counts[i]);
        TIDE_CHECK(world.Seen.sums.at[i] == sums[i]);
        TIDE_CHECK(world.Seen.pairs.at[i] == counts[i] * counts[i]);
    }
    TIDE_CHECK(world.Seen.empty == 0);
    TIDE_CHECK(world.Seen.spawns == 5 && world.Seen.spawnedMatches == 10);
    TIDE_CHECK(world.Seen.destroyed == 1 && world.Seen.destroyedOwn == 20 && world.Seen.destroyedQuery == 20);
    TIDE_CHECK(!tide_has_Selected(&world, world.Seen.first));
    TIDE_CHECK(!tide_entity_alive(&world.entities, world.Seen.second));
    tide_world_free(&world);
}

TIDE_TEST(handler_queries_keep_structural_changes_deferred_and_parameter_copy_rules)
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.Seen.action = 2;
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.culled == 2 && world.Seen.stillPresent == 2);
    TIDE_CHECK(world.Seen.counts.at[0] == 0 && world.Seen.sums.at[0] == 0);
    TIDE_CHECK(world.Seen.destroyed == 2 && world.Seen.destroyedQuery == 30);
    TIDE_CHECK(world.Seen.ownDuring == 5);
    TIDE_CHECK(world.Seen.ownAfter == 15 && world.Seen.queryAfter == 15);
    TIDE_CHECK(tide_get_Actor(&world, world.Seen.actor).value == 15);
    tide_world_free(&world);
}

TIDE_TEST(handler_queries_read_helper_writes_after_cow_and_replay_identically)
{
    tide_world_init(&world, 1.0f / 60.0f);
    world.Seen.action = 3;
    tide_world_copy(&snapshot, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.before == 210 && world.Seen.after == 212 && world.Seen.dataRead == 10);
    TIDE_CHECK(tide_get_Sample(&world, world.Seen.first).value == 11);
    TIDE_CHECK(tide_get_Sample(&world, world.Seen.second).value == 201);
    TIDE_CHECK(tide_list_count(tide_get_Sample(&world, world.Seen.first).data) == 3);
    TIDE_CHECK(tide_world_hash(&snapshot) == before);
    TIDE_CHECK(tide_get_Sample(&snapshot, snapshot.Seen.first).value == 10);
    TIDE_CHECK(tide_get_Sample(&snapshot, snapshot.Seen.second).value == 20);
    tide_world_copy(&replay, &snapshot);
    tide_world_tick(&replay);
    TIDE_CHECK(tide_world_hash(&replay) == tide_world_hash(&world));
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&replay);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}

static uint32_t submitted(uint8_t bytes[TIDE_COMMAND_BYTES])
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(&world, NULL, 1, &world, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    uint32_t size = 0;
    const uint8_t *data = tide_local_command(&local, 0, &size);
    if (!data || size > TIDE_COMMAND_BYTES) return 0;
    memcpy(bytes, data, size);
    return size;
}

TIDE_TEST(handler_queries_in_global_and_targeted_commands)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
    local.UI.shift = true;
    uint8_t bytes[TIDE_COMMAND_BYTES];
    uint32_t size = submitted(bytes);
    TIDE_REQUIRE(size > 0);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(1), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.commandRows == 0 && tide_get_Sample(&world, world.Seen.first).value == 10);
    tide_world_copy(&snapshot, &world);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    TIDE_REQUIRE(tide_game_api.command(&snapshot, tide_player_from_index(0), 0, 1, bytes, size));
    TIDE_CHECK(world.Seen.commandRows == 0);
    tide_world_tick(&world);
    tide_world_tick(&snapshot);
    TIDE_CHECK(world.Seen.commandRows == 2 && world.Seen.counts.at[0] == 2 && world.Seen.sums.at[0] == 44);
    TIDE_CHECK(tide_world_hash(&snapshot) == tide_world_hash(&world));
    tide_local_command_sent(&local, 0);
    local.UI.target = world.Seen.actor;
    local.UI.targeted = true;
    size = submitted(bytes);
    TIDE_REQUIRE(size > 0);
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 2, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Actor(&world, world.Seen.actor).value == 42 && world.Seen.commandRows == 5);
    TIDE_CHECK(!tide_game_api.command(&world, tide_player_from_index(0), 0, 2, bytes, size));
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
