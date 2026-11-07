#include <stdlib.h>
#include "game.h"
#include "tide_test.h"
TIDE_TEST(stable_async_births_ignore_hidden_task_slots_and_survive_raw_resume)
{
    tide_world server = {0}, client = {0}, resumed = {0};
    const tide_player_id player = tide_player_from_index(0);
    tide_world_init(&server, 1.0f / 60.0f);
    uint32_t size = 0;
    uint8_t *projection = tide_game_api.client_state(&server, player, &size);
    TIDE_CHECK(tide_game_api.load_client_state(&client, projection, size));
    free(projection);
    for (int tick = 0; tick < 8; tick++) {
        tide_world_tick(&server);
        tide_world_tick(&client);
        TIDE_CHECK(tide_game_api.client_hash(&server, player) == tide_game_api.client_hash(&client, player));
        if (tick == 0) {
            const uint32_t n = tide_world_pack(&client, NULL, 0);
            uint8_t *bytes = malloc(n);
            TIDE_CHECK(bytes && tide_world_pack(&client, bytes, n) == n);
            TIDE_CHECK(tide_world_unpack(&resumed, bytes, n));
            free(bytes);
        } else {
            tide_world_tick(&resumed);
            TIDE_CHECK(tide_world_hash(&resumed) == tide_world_hash(&client));
        }
        int trails = 0;
        for (uint32_t i = 0; i < server.entities.next_unused; i++) {
            const tide_entity e = tide_entity_in_slot(&server.entities, i);
            if (!tide_has_Trail(&server, e) || !tide_world_visible(&server, e, player)) continue;
            TIDE_CHECK(tide_has_Trail(&client, e));
            const Trail a = tide_get_Trail(&server, e), b = tide_get_Trail(&client, e);
            TIDE_CHECK(tide_entity_equal(a.actor, b.actor) && a.ordinal == b.ordinal);
            trails++;
        }
        TIDE_CHECK(trails == (tick < 3 ? tick * 2 : 6));
    }
    tide_world_free(&resumed);
    tide_world_free(&client);
    tide_world_free(&server);
}
