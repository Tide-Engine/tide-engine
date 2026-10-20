#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static void sample(void *user, uint32_t tick, void *input)
{
    (void)user; (void)tick;
    *(Controls *)input = (Controls){.push = 7};
}

TIDE_TEST(players_ceiling_input_and_audience_reach_the_last_player)
{
    TIDE_CHECK(TIDE_GAME_PLAYERS == TIDE_PLAYERS_MOST && TIDE_PLAYERS_MOST == 65536u);
    TIDE_CHECK(tide_player_index(tide_player_from_index(65535)) == 65535);
    TIDE_CHECK(tide_player_index(tide_player_from_index(65536)) == -1);
    tide_player_set set = {0};
    tide_players_add(&set, 65535);
    tide_players_add(&set, 65536); // The server's separate input slot.
    TIDE_CHECK(tide_players_has(&set, 65535) && tide_players_has(&set, 65536));
    tide_players_remove(&set, 65536);
    TIDE_CHECK(tide_players_has(&set, 65535));
    tide_players_remove(&set, 65535);
    TIDE_CHECK(!tide_players_any(&set));
    tide_world *world = calloc(1, sizeof *world);
    TIDE_REQUIRE(world);
    tide_world_init(world, 1.0f);
    tide_world_set_input(world, tide_player_from_index(65535), (Controls){.push = 7});
    TIDE_CHECK(world->inputs[65535].push == 7 && world->inputs[TIDE_SERVER_INPUT].push == 0);
    tide_world_free(world);
    free(world);
}

TIDE_TEST(players_ceiling_last_slot_connects_replays_and_recovers)
{
    tide_world *initial = calloc(1, sizeof *initial);
    uint64_t *digests = calloc(TIDE_GAME_PLAYERS, sizeof *digests);
    uint8_t (*identities)[32] = calloc(TIDE_GAME_PLAYERS, sizeof *identities);
    TIDE_REQUIRE(initial && digests && identities);
    tide_world_init(initial, 1.0f);
    // A restored match reserves the preceding slots without creating 65,535
    // machines. Only its final slot is unused, so HELLO must assign that one.
    for (uint32_t i = 0; i + 1u < TIDE_GAME_PLAYERS; i++) digests[i] = i + 1u;
    tide_loopback *net = tide_loopback_create(19);
    tide_server_desc desc = {.game = &tide_game_api, .world = initial, .digests = digests,
        .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&desc, 0.0);
    TIDE_REQUIRE(server);
    tide_client_desc client_desc = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 2),
        .server = tide_loopback_address(1), .sample = sample, .lead = 1};
    tide_client *client = tide_client_create(&client_desc, 0.0);
    TIDE_REQUIRE(client);
    double now = 0.0;
    for (int i = 0; i < 240; i++) {
        now += 1.0 / 60.0;
        tide_loopback_set_time(net, now);
        tide_server_update(server, now);
        tide_client_update(client, now);
    }
    const tide_client_status status = tide_client_status_of(client);
    TIDE_REQUIRE(status.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(status.player.id == 65536u && status.verified_tick >= 2u && status.resyncs == 0);
    const tide_world *world = tide_server_world(server);
    TIDE_CHECK(world->Match.joined == 1 && world->Match.last.id == 65536u);
    TIDE_CHECK(world->inputs[65535].push == 7);
    TIDE_CHECK((uint32_t)world->Match.audience.players2047 == UINT32_C(0x80000000));
    // Capture only the game's metadata; no ceiling-sized stack arrays.
    tide_server_recovery recovery = {.digests = digests, .identities = identities};
    tide_server_recovery_state(server, &recovery);
    TIDE_CHECK(tide_players_has(&recovery.players, 65535) && digests[65535] != 0);
    tide_world_copy(initial, world);
    uint32_t packed_size = 0;
    uint8_t *packed = tide_game_api.pack_delta(initial, NULL, NULL, 0, false, &packed_size);
    tide_world *decoded = calloc(1, sizeof *decoded);
    TIDE_CHECK(tide_game_api.unpack_delta(decoded, NULL, packed, packed_size));
    tide_world_free(decoded); free(decoded); free(packed);
    tide_client_destroy(client);
    tide_server_destroy(server);
    desc.players = recovery.players;
    desc.leaving = recovery.leaving;
    desc.identities = (const uint8_t (*)[32])identities;
    desc.dt = recovery.dt;
    desc.transports[0] = tide_loopback_endpoint(net, 3);
    server = tide_server_create(&desc, now);
    TIDE_REQUIRE(server);
    client_desc.transport = tide_loopback_endpoint(net, 4);
    client_desc.server = tide_loopback_address(3);
    client_desc.cookie = status.cookie;
    client = tide_client_create(&client_desc, now);
    for (int i = 0; i < 240; i++) {
        now += 1.0 / 60.0;
        tide_loopback_set_time(net, now);
        tide_server_update(server, now);
        tide_client_update(client, now);
    }
    TIDE_CHECK(tide_client_status_of(client).state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(tide_client_status_of(client).player.id == 65536u);
    TIDE_CHECK(((const tide_world *)tide_server_world(server))->Match.joined == 1);
    tide_client_destroy(client);
    for (int i = 0; i < 90; i++) {
        now += 1.0 / 60.0;
        tide_loopback_set_time(net, now);
        tide_server_update(server, now);
    }
    TIDE_CHECK(((const tide_world *)tide_server_world(server))->Match.left == 1);
    TIDE_CHECK(((const tide_world *)tide_server_world(server))->Match.audience.players2047 == 0);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    tide_world_free(initial);
    free(initial); free(digests); free(identities);
}

TIDE_TEST(players_ceiling_rejects_larger_handwritten_games)
{
    tide_game game = tide_game_api;
    game.max_players = TIDE_PLAYERS_MOST + 1u;
    TIDE_CHECK(tide_server_create(&(tide_server_desc){.game = &game}, 0.0) == NULL);
    TIDE_CHECK(tide_client_create(&(tide_client_desc){.game = &game}, 0.0) == NULL);
    game.max_players = TIDE_PLAYERS_MOST;
    game.max_input_bytes = UINT32_MAX;
    TIDE_CHECK(tide_server_create(&(tide_server_desc){.game = &game}, 0.0) == NULL);
    TIDE_CHECK(tide_client_create(&(tide_client_desc){.game = &game}, 0.0) == NULL);
}
