#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "tide_test.h"

static int attack;
static int32_t advertised;
static tide_view_time stamped;
static uint32_t malicious_write(const void *input, uint8_t *out, uint32_t capacity)
{
    Controls value = *(const Controls *)input;
    stamped = tide_game_api.view_time(input);
    switch (value.viewTime.valid ? attack : 0) {
    case 1: value.viewTime.tick = advertised + 100; break;
    case 2: value.viewTime.alpha = -0.5f; break;
    case 3: value.viewTime.alpha = 1.5f; break;
    case 4: value.viewTime.alpha = NAN; break;
    case 5: value.viewTime.tick = -1; break;
    case 6: value.viewTime.tick = 0; value.viewTime.alpha = 1; break;
    case 7: value.viewTime.tick = advertised - 30; value.viewTime.alpha = 1; break;
    }
    return tide_game_api.write_input(&value, out, capacity);
}
static void sample(void *user, uint32_t tick, void *input)
{
    (void)user;
    Controls *value = input;
    value->fire = (int32_t)tick + 1;
    // Even trusted Sample cannot replace the engine's presented timestamp.
    value->viewTime = (ViewTime){.tick = 12345, .alpha = 0.5f, .valid = true};
}
TIDE_TEST(view_time_is_stamped_and_server_rejects_untrusted_claims)
{
    tide_game client_game = tide_game_api;
    client_game.write_input = malicious_write;
    tide_loopback *net = tide_loopback_create(31);
    const tide_server_desc sd = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&sd, 0);
    const tide_client_desc cd = {.game = &client_game, .transport = tide_loopback_endpoint(net, 2),
        .server = tide_loopback_address(1), .sample = sample, .lead = 2};
    tide_client *client = tide_client_create(&cd, 0);
    double now = 0;
    double previous_presented = -1;
    bool fractional = false;
    attack = 0;
    for (int mode = 0; mode < 8; mode++) {
        attack = mode;
        for (int i = 0; i < 70; i++) {
            now += 1.0 / 120.0;
            tide_loopback_set_time(net, now);
            advertised = ((const tide_world *)tide_server_world(server))->Time.tick;
            tide_client_update(client, now);
            tide_server_update(server, now);
            const tide_view_worlds view = tide_client_view(client);
            if (view.verified) {
                const int32_t latest = ((const tide_world *)tide_client_verified_world(client))->Time.tick;
                const double presented = (double)view.verified_tick - 1.0 + (double)view.verified_alpha;
                TIDE_CHECK(view.verified_tick == (uint32_t)((const tide_world *)view.verified)->Time.tick);
                TIDE_CHECK(view.verified_alpha >= 0 && view.verified_alpha <= 1 && presented <= latest);
                TIDE_CHECK(presented >= previous_presented);
                previous_presented = presented;
                if (view.verified_previous) {
                    TIDE_CHECK(((const tide_world *)view.verified_previous)->Time.tick + 1 == (int32_t)view.verified_tick);
                    fractional |= view.verified_alpha > 0 && view.verified_alpha < 1;
                }
            }
            tide_client_mark_presented(client);
        }
        const Seen *seen = &((const tide_world *)tide_server_world(server))->Seen;
        TIDE_CHECK(tide_client_world(client) != NULL);
        TIDE_CHECK(seen->fire > 0 && stamped.valid && stamped.tick != 12345);
        TIDE_CHECK(seen->valid == (mode == 0));
        if (mode) TIDE_CHECK(seen->last.tick == 0 && seen->last.alpha == 0);
    }
    printf("    fractional %d, resyncs %u\n", fractional, tide_client_status_of(client).resyncs);
    TIDE_CHECK(fractional && tide_client_status_of(client).resyncs == 0);
    // With no new confirmations the presentation clock holds, while prediction continues.
    for (int i = 0; i < 40; i++) {
        now += 1.0 / 120.0;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
    }
    const tide_view_worlds held = tide_client_view(client);
    TIDE_CHECK(held.verified_alpha == 1 && held.verified_tick == (uint32_t)((const tide_world *)tide_client_verified_world(client))->Time.tick);
    TIDE_CHECK(((const tide_world *)held.current)->Time.tick > (int32_t)held.verified_tick);
    tide_client_destroy(client); tide_server_destroy(server); tide_loopback_destroy(net);
}
