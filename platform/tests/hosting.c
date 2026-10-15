#include "game.h"
#include "tide/hosting.h"
#include "tide/platform.h"
#include "tide/time.h"
#include "tide_test.h"

TIDE_TEST(hosting_two_matches_over_real_udp_without_a_graphics_context)
{
    tide_transport listener = {0};
    uint16_t port = (uint16_t)(30000u + tide_platform_entropy() % 20000u);
    for (int i = 0; i < 32 && !listener.send; i++, port++)
        (void)tide_platform_udp_open(port, &listener);
    // The loop increments once after a successful open.
    port--;
    TIDE_REQUIRE(listener.send);
    double now = (double)tide_time_now_ns() * 1e-9;
    const tide_hosting_desc hd = {.game = &tide_game_api, .transport = listener,
        .allow_anonymous_create = true, .allow_anonymous_join = true, .entropy = tide_platform_entropy,
        .jobs = tide_platform_jobs(), .max_matches = 2};
    tide_hosting *h = tide_hosting_create(&hd, now);
    TIDE_REQUIRE(h);
    tide_hosting_client *admission[2] = {0};
    tide_client *client[2] = {0};
    for (int i = 0; i < 2; i++) {
        tide_transport transport;
        TIDE_REQUIRE(tide_platform_udp_open(0, &transport));
        const tide_hosting_client_desc cd = {.transport = transport,
            .server = {.kind = TIDE_ADDRESS_IPV4, .host = UINT32_C(0x7f000001), .port = port},
            .nonce = tide_platform_entropy()};
        admission[i] = tide_hosting_client_create(&cd, now);
        TIDE_REQUIRE(admission[i]);
    }
    const double deadline = now + 4;
    bool connected = false;
    while (now < deadline && !connected) {
        now = (double)tide_time_now_ns() * 1e-9;
        for (int i = 0; i < 2; i++) {
            tide_hosting_client_update(admission[i], now);
            if (!client[i] && tide_hosting_client_result(admission[i]) == TIDE_HOSTING_ACCEPTED) {
                const tide_client_desc cd = {.game = &tide_game_api,
                    .transport = tide_hosting_client_transport(admission[i]),
                    .server = tide_hosting_client_address(admission[i])};
                client[i] = tide_client_create(&cd, now);
            }
            if (client[i]) tide_client_update(client[i], now);
        }
        tide_hosting_update(h, now);
        connected = client[0] && client[1] && tide_client_status_of(client[0]).verified_tick >= 20
                    && tide_client_status_of(client[1]).verified_tick >= 20;
        tide_yield();
    }
    TIDE_CHECK(connected);
    const uint64_t a = tide_hosting_client_match(admission[0]), b = tide_hosting_client_match(admission[1]);
    TIDE_CHECK(a && b && a != b && tide_hosting_match_count(h) == 2);
    for (int i = 0; i < 2; i++) {
        if (client[i]) {
            TIDE_CHECK(tide_client_status_of(client[i]).resyncs == 0);
            tide_client_destroy(client[i]);
        }
        tide_hosting_client_destroy(admission[i]);
    }
    tide_hosting_destroy(h);
}
