#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(asset_sharing_generated_mutation_ownership_and_self_contained_state)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0}, snapshot = {0}, back = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.checks == 15 && world.Probe.passed == 15);
    tide_world_copy(&snapshot, &world);
    uint32_t size;
    uint8_t *bytes = tide_game_api.pack_delta(&world, NULL, NULL, 0, true, &size);
    TIDE_REQUIRE(tide_game_api.unpack_delta(&back, NULL, bytes, size));
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&back));
    free(bytes);
    tide_world_free(&back);
    const uint32_t full_size = tide_world_pack(&world, NULL, 0);
    bytes = malloc(full_size);
    TIDE_REQUIRE(tide_world_pack(&world, bytes, full_size) == full_size);
    tide_pack_unload();
    TIDE_REQUIRE(tide_world_unpack(&back, bytes, full_size));
    TIDE_CHECK(tide_world_hash(&snapshot) == tide_world_hash(&back));
    tide_world_tick(&back);
    TIDE_CHECK(back.Probe.checks == 15 && back.Probe.passed == 15);
    free(bytes);
    tide_world_free(&back); tide_world_free(&snapshot); tide_world_free(&world);
}

static uint32_t reference_transfers, full_transfers;
static bool reject_once;

static uint8_t *counted_pack(const void *world, const void *base, const uint8_t *need,
    const uint32_t need_size, const bool assets, uint32_t *size)
{
    if (assets) reference_transfers++; else full_transfers++;
    return tide_game_api.pack_delta(world, base, need, need_size, assets, size);
}

static bool missing_asset_once(void *world, const void *base, const uint8_t *bytes, const uint32_t size)
{
    if (reject_once) { reject_once = false; return false; }
    return tide_game_api.unpack_delta(world, base, bytes, size);
}

TIDE_TEST(asset_sharing_new_connection_uses_generated_codec_under_loss)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    reference_transfers = full_transfers = 0;
    reject_once = true;
    tide_game game = tide_game_api;
    game.pack_delta = counted_pack;
    game.unpack_delta = missing_asset_once;
    tide_loopback *net = tide_loopback_create(17);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.03, .jitter = 0.02, .loss = 0.1});
    const tide_server_desc server_desc = {.game = &game, .tick_rate = 60,
        .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&server_desc, 0);
    const tide_client_desc client_desc = {.game = &game,
        .transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1), .lead = 2};
    tide_client *client = tide_client_create(&client_desc, 0);
    for (uint32_t step = 1; step < 600; step++) {
        const double now = (double)step / 60.0;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
    }
    const tide_client_status status = tide_client_status_of(client);
    TIDE_REQUIRE(status.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(status.resyncs == 0);
    TIDE_CHECK(reference_transfers > 0 && full_transfers > 0 && !reject_once);
    const tide_world *verified = tide_client_verified_world(client);
    TIDE_REQUIRE(verified);
    TIDE_CHECK(verified->Probe.checks == 15 && verified->Probe.passed == 15);
    tide_client_destroy(client); tide_server_destroy(server); tide_loopback_destroy(net);
    tide_pack_unload();
}
