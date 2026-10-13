#include "deadweight_network_test.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(v) do { if (!(v)) { fprintf(stderr, "DEADWEIGHT network failed: %s at %d\n", #v, __LINE__); exit(1); } } while (0)

typedef struct peer {
    tide_client *client;
    uint32_t ticks[128];
    uint64_t hashes[128];
} peer;
static void sample(void *user, uint32_t tick, void *out)
{
    (void)user;
    *(Controls *)out = (Controls){.reel = tick > 120 && tick < 580};
}
static void network(tide_net_conditions conditions)
{
    tide_loopback *net = tide_loopback_create(71);
    tide_loopback_set_conditions(net, conditions);
    const tide_server_desc sd = {.game = &tide_game_api, .tick_rate = 60,
        .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&sd, 0);
    peer peers[2] = {0};
    for (int i = 0; i < 2; i++) {
        const tide_client_desc cd = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, (uint32_t)i + 2),
            .server = tide_loopback_address(1), .sample = sample, .lead = 2, .lead_frame = true};
        peers[i].client = tide_client_create(&cd, 0);
    }
    for (int step = 0; step < 1920; step++) {
        const double now = step / 120.0;
        tide_loopback_set_time(net, now);
        if (step % 2 == 0) tide_server_update(server, now);
        for (int i = 0; i < 2; i++) {
            if (step % (i == 0 ? 2 : 4) != 0) continue;
            tide_client_update(peers[i].client, now);
            const tide_view_worlds view = tide_client_view(peers[i].client);
            if (!view.verified) continue;
            const uint32_t slot = view.verified_tick % 128;
            peers[i].ticks[slot] = view.verified_tick;
            peers[i].hashes[slot] = tide_world_hash(view.verified);
        }
    }
    int agreed = 0;
    for (int i = 0; i < 128; i++) {
        const uint32_t tick = peers[0].ticks[i];
        if (tick < 700 || peers[1].ticks[tick % 128] != tick) continue;
        CHECK(peers[0].hashes[i] == peers[1].hashes[tick % 128]);
        agreed++;
    }
    CHECK(agreed > 5);
    const tide_world *world = tide_server_world(server);
    CHECK(world->Site.joined == 2 && world->Site.bridge);
    CHECK(tide_get_Box3D_Body(world, world->Site.cargo.at[0]).position.z > 5);
    for (int i = 0; i < 2; i++) {
        const tide_client_status status = tide_client_status_of(peers[i].client);
        CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
        tide_client_destroy(peers[i].client);
    }
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    printf("DEADWEIGHT network: %.0f ms/way, %.0f%% loss, %d matching verified ticks\n", conditions.latency * 1000, conditions.loss * 100, agreed);
}
int main(void)
{
    network((tide_net_conditions){0});
    network((tide_net_conditions){.latency = 0.05, .jitter = 0.01, .loss = 0.05});
    puts("DEADWEIGHT network checks passed");
    return 0;
}
