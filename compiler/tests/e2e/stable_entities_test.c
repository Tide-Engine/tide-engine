#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide_test.h"

TIDE_TEST(stable_entities_survive_projection_and_private_spawns)
{
    tide_world server = {0}, client = {0};
    const tide_player_id player = tide_player_from_index(0);
    tide_world_init(&server, 1.0f / 60.0f);
    for (int tick = 0; tick < 40; tick++) {
        uint32_t size = 0;
        uint8_t *bytes = tide_game_api.client_state(&server, player, &size);
        TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
        free(bytes);
        TIDE_CHECK(server.tide_identity_clock == client.tide_identity_clock);
        tide_world_tick(&server);
        tide_world_tick(&client);
        TIDE_CHECK(tide_game_api.client_hash(&server, player) == tide_game_api.client_hash(&client, player));
        tide_text_use(&server.heap, NULL);
        TIDE_CHECK(tide_list_count(server.Order.query) == 2);
        tide_text_use(&client.heap, NULL);
        TIDE_CHECK(tide_list_count(client.Order.query) == 2);
        tide_text_use(&server.heap, NULL);
        TIDE_CHECK(tide_list_count(server.Order.events) == 2);
        tide_entity previous = {0};
        for (int i = 0; i < 2; i++) {
            tide_text_use(&server.heap, NULL);
            const tide_entity a = *(const tide_entity *)tide_list_at(server.Order.query, i, sizeof(tide_entity));
            tide_text_use(&client.heap, NULL);
            const tide_entity b = *(const tide_entity *)tide_list_at(client.Order.query, i, sizeof(tide_entity));
            TIDE_CHECK(tide_entity_equal(a, b));
            TIDE_CHECK(i == 0 || tide_entity_compare(previous, a) < 0);
            previous = a;
        }
        int public_shots = 0;
        for (uint32_t i = 0; i < server.entities.next_unused; i++) {
            const tide_entity e = tide_entity_in_slot(&server.entities, i);
            if (!tide_has_Shot(&server, e) || !tide_world_visible(&server, e, player)) continue;
            TIDE_CHECK(tide_has_Shot(&client, e));
            const Shot a = tide_get_Shot(&server, e), b = tide_get_Shot(&client, e);
            TIDE_CHECK(tide_entity_equal(a.shooter, b.shooter) && a.tick == b.tick && a.kind == b.kind);
            public_shots++;
        }
        TIDE_CHECK(public_shots == 6);
    }
    tide_world_free(&client);
    tide_world_free(&server);
}

TIDE_TEST(stable_entities_repeated_public_lifecycle_same_clock)
{
    tide_world a = {0}, b = {0};
    tide_world_init(&a, 1.0f / 60.0f);
    tide_world_init(&b, 1.0f / 60.0f);
    const tide_player_id p = tide_player_from_index(0), q = tide_player_from_index(1);
    tide_world_player_joined(&a, p);
    tide_world_player_joined(&b, p);
    tide_world_player_joined(&a, q);
    tide_world_player_joined(&b, q);
    tide_world_player_left(&a, p);
    tide_world_player_left(&b, p);
    tide_world_player_joined(&a, p);
    tide_world_player_joined(&b, p);
    TIDE_CHECK(a.tide_lifecycle_occurrences[0] == 3 && a.tide_lifecycle_occurrences[1] == 1);
    tide_world_tick(&a);
    tide_world_tick(&b);
    int tickets = 0;
    for (uint32_t i = 0; i < a.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&a.entities, i);
        if (!tide_has_Ticket(&a, e)) continue;
        TIDE_CHECK(tide_has_Ticket(&b, e));
        tickets++;
    }
    TIDE_CHECK(tickets == 3);
    TIDE_CHECK(a.tide_lifecycle_occurrences[0] == 0 && a.tide_lifecycle_occurrences[1] == 0);
    tide_world_free(&a);
    tide_world_free(&b);
}
