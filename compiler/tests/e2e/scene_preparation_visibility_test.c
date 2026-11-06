#include "game.h"
#include "tide_test.h"
#include <stdlib.h>

TIDE_TEST(scene_preparation_visibility_keeps_private_inputs_and_sends_results)
{
    tide_loopback *net = tide_loopback_create(8741);
    const tide_transport host = tide_loopback_endpoint(net, 1), peer = tide_loopback_endpoint(net, 2);
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api,
        .tick_rate = 60, .transports = {host}}, 0);
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
        .transport = peer, .server = tide_loopback_address(1)}, 0);
    TIDE_REQUIRE(server && client);
    for (int frame = 0; frame < 600; frame++) {
        const double now = (double)frame / 120.0;
        tide_loopback_set_time(net, now);
        tide_server_update(server, now);
        tide_client_update(client, now);
    }
    const tide_client_status status = tide_client_status_of(client);
    const tide_world *authoritative = tide_server_world(server);
    const tide_world *verified = tide_client_verified_world(client);
    TIDE_REQUIRE(verified != NULL);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(authoritative->State.calls == 1 && verified->State.calls == 1);
    TIDE_CHECK(verified->State.value == 12345);
    const tide_entity scene = authoritative->State.destination;
    TIDE_CHECK(tide_has_Recipe(authoritative, scene) && !tide_has_Recipe(verified, scene));
    TIDE_CHECK(tide_world_player_scene_prepared(authoritative, scene, status.player));
    TIDE_CHECK(tide_world_scene_prepared(verified, scene, (tide_player_id){0}));
    TIDE_CHECK(!tide_world_prepare_pending(verified));
    uint32_t size = 0;
    uint8_t *bytes = tide_game_api.client_state(authoritative, status.player, &size);
    tide_world restored = {0};
    TIDE_REQUIRE(bytes && tide_game_api.load_client_state(&restored, bytes, size));
    TIDE_CHECK(tide_get_Levels_Region(&restored, scene).tide_preparationOrder == 0);
    TIDE_CHECK(restored.tide_preparation_order.value == 0);
    free(bytes);
    tide_world_free(&restored);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
