#include "game.h"
#include "tide_test.h"
#include <stdio.h>

#ifdef TIDE_TEST_RANGE
#define TEST_MODE TIDE_PREDICTION_ROLLBACK
#else
#define TEST_MODE TIDE_PREDICTION_OWNED
#endif

static void sample(void *user, uint32_t tick, void *input)
{
    (void)user; (void)tick;
    *(Controls *)input = (Controls){0};
}

static uint32_t client_ticks;
static void counted_tick(void *world, const tide_jobs *jobs)
{
    client_ticks++;
    tide_game_api.tick(world, jobs);
}

static tide_client_status compare_mode(tide_prediction mode, uint32_t *ticks)
{
    tide_game game = tide_game_api;
    game.prediction = mode;
    game.tick_events = NULL;
    game.tick = counted_tick;
    client_ticks = 0;
    tide_loopback *net = tide_loopback_create(17);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.1});
    const tide_server_desc sd = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&sd, 0);
    const tide_client_desc cd = {.game = &game, .transport = tide_loopback_endpoint(net, 2),
        .server = tide_loopback_address(1), .sample = sample, .lead = 2};
    tide_client *client = tide_client_create(&cd, 0);
    for (int frame = 1; frame <= 720; frame++) {
        const double now = (double)frame / 60.0;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
    }
    const tide_client_status result = tide_client_status_of(client);
    *ticks = client_ticks;
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
    return result;
}

TIDE_TEST(predict_owned_and_delay_confirm_without_recurring_world_transfers)
{
    uint32_t owned_ticks, delay_ticks;
    const tide_client_status owned = compare_mode(TEST_MODE, &owned_ticks);
    const tide_client_status delay = compare_mode(TIDE_PREDICTION_DELAY, &delay_ticks);
    TIDE_CHECK(owned.state == TIDE_SESSION_CONNECTED && delay.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(owned.repairs == 0 && delay.repairs == 0);
    TIDE_CHECK(owned.resyncs == 0 && delay.resyncs == 0);
    TIDE_CHECK(owned.verified_tick > 650 && delay.verified_tick > 650);
    TIDE_CHECK(owned.world_bytes == delay.world_bytes);
    TIDE_CHECK(delay_ticks < owned_ticks);
    printf("    owned/delay: client ticks %u/%u, world bytes %llu/%llu\n", owned_ticks, delay_ticks,
        (unsigned long long)owned.world_bytes, (unsigned long long)delay.world_bytes);
}

TIDE_TEST(predict_owned_includes_children_and_excludes_nearby_and_unpositioned_others)
{
    tide_loopback *net = tide_loopback_create(17);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.1});
    const tide_server_desc sd = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&sd, 0);
    const tide_client_desc cd = {.game = &tide_game_api, .transport = tide_loopback_endpoint(net, 2),
        .server = tide_loopback_address(1), .sample = sample, .lead = 2};
    tide_client *client = tide_client_create(&cd, 0);
    int checked = 0;
    for (int frame = 1; frame <= 720; frame++) {
        const double now = (double)frame / 60.0;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
        const tide_client_status s = tide_client_status_of(client);
        if (frame < 120 || s.predicted_tick <= s.verified_tick) continue;
        const tide_world *p = tide_client_world(client), *v = tide_client_verified_world(client);
        const int lead = (int)(s.predicted_tick - s.verified_tick);
        TIDE_CHECK(p->Observed.own == v->Observed.own + lead);
        TIDE_CHECK(p->Observed.child == v->Observed.child + lead);
#ifdef TIDE_TEST_RANGE
        TIDE_CHECK(p->Observed.other == v->Observed.other + lead);
        TIDE_CHECK(p->Observed.bare == v->Observed.bare + lead);
#else
        TIDE_CHECK(p->Observed.other == v->Observed.other);
        TIDE_CHECK(p->Observed.bare == v->Observed.bare);
#endif
        TIDE_CHECK(p->Observed.distant == v->Observed.distant);
        checked++;
    }
    TIDE_CHECK(checked > 100);
    TIDE_CHECK(tide_client_status_of(client).resyncs == 0);
    printf("    owned: verified %u, repairs %u, world bytes %llu\n", tide_client_status_of(client).verified_tick,
        tide_client_status_of(client).repairs, (unsigned long long)tide_client_status_of(client).world_bytes);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
