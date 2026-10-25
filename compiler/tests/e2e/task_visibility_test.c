#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide_test.h"

static bool contains(const uint8_t *bytes, const uint32_t size, const char *text)
{
    const size_t count = strlen(text);
    for (uint32_t i = 0; (uint64_t)i + count <= size; i++)
        if (!memcmp(bytes + i, text, count)) return true;
    return false;
}

static Observed observe(const tide_world *world)
{
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_local_init(&local);
    tide_frame(world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    const Observed value = local.Observed;
    tide_local_free(&local);
    tide_draw_free(&draw);
    return value;
}

TIDE_TEST(task_visibility_keeps_registry_continuations_and_cached_results_private)
{
    tide_world server = {0}, client = {0};
    tide_world_init(&server, 0.1f);
    tide_world_tick(&server);
    const Observed original = observe(&server);
    TIDE_CHECK(original.cached && original.pending && !original.absent);
    uint32_t size;
    uint8_t *bytes = tide_game_api.client_state(&server, tide_player_from_index(0), &size);
    TIDE_REQUIRE(bytes);
    TIDE_CHECK(!contains(bytes, size, "PRIVATE-CACHED-TASK-RESULT"));
    TIDE_CHECK(!contains(bytes, size, "PRIVATE-WAITING-TASK-INPUT"));
    TIDE_REQUIRE(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(tide_game_api.client_hash(&server, tide_player_from_index(0)) ==
               tide_game_api.client_hash(&client, tide_player_from_index(0)));
    TIDE_CHECK(tide_task_handle_equal(server.State.cached, client.State.cached));
    const Observed projected = observe(&client);
    TIDE_CHECK(projected.absent && projected.cached && !projected.pending);
    TIDE_CHECK(tide_list_count(client.tide_match_task_slots.slots) == 0);
    client.State.startLater = true;
    tide_world_tick(&client);
    TIDE_CHECK(tide_task_handle_equal(client.State.later, (tide_task_handle_value){0}));
    TIDE_CHECK(observe(&client).absent);
    TIDE_REQUIRE(tide_game_api.load_client_state(&client, bytes, size));
    TIDE_CHECK(tide_game_api.client_hash(&server, tide_player_from_index(0)) ==
               tide_game_api.client_hash(&client, tide_player_from_index(0)));
    free(bytes);
    tide_world_free(&client);
    tide_world_free(&server);
}
