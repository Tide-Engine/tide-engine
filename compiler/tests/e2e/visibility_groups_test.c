#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static bool sees(const tide_world *w, tide_entity e, int player)
{
    return tide_world_visible(w, e, tide_player_from_index(player));
}

static bool sees_secret(const tide_world *w, tide_entity e, int player)
{
    return tide_world_component_visible(w, e, TIDE_COMPONENT_ID_Secret, tide_player_from_index(player));
}

static bool has_scene_secret(const uint8_t *bytes, const uint32_t size)
{
    static const char secret[] = "scene-hidden-state";
    for (uint32_t i = 0; i + sizeof secret - 1u <= size; i++)
        if (!memcmp(bytes + i, secret, sizeof secret - 1u)) return true;
    return false;
}

TIDE_TEST(visibility_groups_share_audience_and_restrict_components)
{
    tide_world w = {0};
    tide_world_init(&w, 0.1f);
    tide_world_tick(&w);
    const tide_entity object = w.Control.object, card = w.Control.card;
    TIDE_CHECK(sees(&w, object, 0));
    TIDE_CHECK(!sees(&w, object, 32));
    TIDE_CHECK(sees(&w, object, 33));
    TIDE_CHECK(sees(&w, object, 69));
    TIDE_CHECK(!sees(&w, object, 70));
    TIDE_CHECK(sees(&w, card, 1));
    TIDE_CHECK(sees_secret(&w, card, 0));
    TIDE_CHECK(!sees_secret(&w, card, 1));
    TIDE_CHECK(tide_has_Secret(&w, card));
    const uint64_t hash = tide_world_hash(&w);
    TIDE_CHECK(sees_secret(&w, object, 69));
    TIDE_CHECK(tide_world_hash(&w) == hash);
    w.Control.action = 1;
    tide_world_tick(&w);
    TIDE_CHECK(!sees(&w, object, 0));
    TIDE_CHECK(sees(&w, object, 1));
    w.Control.action = 2;
    tide_world_tick(&w);
    TIDE_CHECK(!sees_secret(&w, card, 0));
    TIDE_CHECK(!sees_secret(&w, card, 1));
    w.Control.action = 3;
    tide_world_tick(&w);
    TIDE_CHECK(sees_secret(&w, card, 0));
    TIDE_CHECK(sees_secret(&w, card, 1));
    w.Control.action = 4;
    tide_world_tick(&w);
    TIDE_CHECK(!sees(&w, card, 0));
    TIDE_CHECK(!sees_secret(&w, card, 0));
    TIDE_CHECK(!sees_secret(&w, card, 1));
    tide_world_free(&w);
}

TIDE_TEST(visibility_scene_defaults_and_group_state_survive_snapshots_and_packing)
{
    tide_world w = {0}, saved = {0}, restored = {0};
    tide_world_init(&w, 0.1f);
    tide_world_tick(&w);
    TIDE_CHECK(sees(&w, w.Control.handCard, 0));
    TIDE_CHECK(!sees(&w, w.Control.handCard, 1));
    TIDE_CHECK(!sees_secret(&w, w.Control.handCard, 1));
    TIDE_CHECK(!sees_secret(&w, w.Control.handCard, 69)); // All cannot widen the scene audience.
    tide_world_copy(&saved, &w);
    const uint64_t hash = tide_world_hash(&w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_CHECK(bytes != NULL);
    TIDE_CHECK(tide_world_pack(&w, bytes, size) == size);
    TIDE_CHECK(tide_world_unpack(&restored, bytes, size));
    TIDE_CHECK(tide_world_hash(&restored) == hash);
    TIDE_CHECK(sees_secret(&restored, restored.Control.card, 0));
    TIDE_CHECK(!sees_secret(&restored, restored.Control.card, 1));
    w.Control.action = 1;
    tide_world_tick(&w);
    TIDE_CHECK(!sees(&w, w.Control.object, 0));
    TIDE_CHECK(sees(&saved, saved.Control.object, 0));
    TIDE_CHECK(tide_world_hash(&saved) == hash);
    free(bytes);
    tide_world_free(&restored);
    tide_world_free(&saved);
    tide_world_free(&w);
}

TIDE_TEST(visibility_scene_groups_update_roots_contents_and_later_spawns)
{
    tide_world server = {0}, client = {0}, saved = {0};
    tide_world_init(&server, 0.1f);
    const tide_entity scene = server.Control.hand, card = server.Control.handCard;
    TIDE_CHECK(sees(&server, scene, 0) && sees_secret(&server, card, 0));
    TIDE_CHECK(!sees(&server, scene, 69) && !sees(&server, card, 69));
    tide_world_copy(&saved, &server);
    server.Control.action = 6;
    tide_world_tick(&server);
    TIDE_CHECK(!sees(&server, scene, 0) && !sees_secret(&server, card, 0));
    TIDE_CHECK(sees(&server, scene, 69) && sees_secret(&server, card, 69));
    TIDE_CHECK(sees_secret(&saved, card, 0) && !sees_secret(&saved, card, 69));

    uint32_t size;
    uint8_t *bytes = tide_game_api.client_state(&server, tide_player_from_index(0), &size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(!has_scene_secret(bytes, size));
    TIDE_REQUIRE(tide_game_api.load_client_state(&client, bytes, size));
    free(bytes);
    TIDE_CHECK(!tide_entity_alive(&client.entities, scene));
    TIDE_CHECK(!tide_entity_alive(&client.entities, card));
    bytes = tide_game_api.client_state(&server, tide_player_from_index(69), &size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(has_scene_secret(bytes, size));
    TIDE_REQUIRE(tide_game_api.load_client_state(&client, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_has_Hand(&client, scene) && tide_has_Secret(&client, card));

    server.Control.action = 9;
    tide_world_tick(&server);
    TIDE_CHECK(sees(&server, server.Control.laterCard, 69));
    TIDE_CHECK(!sees(&server, server.Control.laterCard, 0));
    server.Control.action = 7;
    tide_world_tick(&server);
    TIDE_CHECK(sees(&server, scene, 69));
    TIDE_CHECK(!sees_secret(&server, card, 69)); // The entity further narrows the scene.
    TIDE_CHECK(!sees(&server, card, 0));
    TIDE_CHECK(sees(&server, server.Control.laterCard, 69));
    server.Control.action = 2;
    tide_world_tick(&server);
    TIDE_CHECK(!sees(&server, scene, 69) && !sees(&server, server.Control.laterCard, 69));
    TIDE_CHECK(tide_entity_alive(&server.entities, scene)); // Concealment does not unload.
    server.Control.action = 12;
    tide_world_tick(&server);
    TIDE_CHECK(sees(&server, scene, 0) && sees(&server, scene, 69));
    TIDE_CHECK(sees(&server, server.Control.laterCard, 0) && sees(&server, server.Control.laterCard, 69));
    TIDE_CHECK(sees_secret(&server, card, 0) && !sees_secret(&server, card, 69));
    tide_world_free(&saved);
    tide_world_free(&client);
    tide_world_free(&server);
}

TIDE_TEST(visibility_scene_policy_exists_before_initialization_journals)
{
    tide_world server = {0};
    tide_event_log events[TIDE_GAME_PLAYERS] = {{0}};
    tide_world_init(&server, 0.1f);
    server.Control.action = 11;
    tide_game_api.tick_client_events(&server, NULL, events);
    TIDE_CHECK(events[0].count >= 2); // Both the root and its initialized contents.
    for (uint32_t i = 1; i < TIDE_GAME_PLAYERS; i++) TIDE_CHECK(events[i].count == 0);
    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++) tide_event_log_free(&events[i]);
    tide_world_free(&server);
}
