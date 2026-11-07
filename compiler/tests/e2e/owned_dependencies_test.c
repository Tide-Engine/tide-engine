#include "game.h"
#include "tide_test.h"
#include <stdlib.h>

static void sample(void *user, uint32_t tick, void *input)
{
    (void)user; (void)tick;
    *(Controls *)input = (Controls){0};
}
TIDE_TEST(owned_prediction_keeps_transitive_helpers_and_event_dependencies)
{
    tide_loopback *net = tide_loopback_create(42);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.04});
    const tide_server_desc sd = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&sd, 0);
    const tide_client_desc cd = {.game = &tide_game_api, .sample = sample, .lead = 2,
        .transport = tide_loopback_endpoint(net, 2), .server = tide_loopback_address(1)};
    tide_client *client = tide_client_create(&cd, 0);
    int checked = 0;
    for (int frame = 1; frame <= 720; frame++) {
        const double now = frame / 60.0;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
        const tide_client_status s = tide_client_status_of(client);
        if (frame < 120 || s.predicted_tick <= s.verified_tick) continue;
        const tide_world *p = tide_client_world(client), *v = tide_client_verified_world(client);
        const int lead = (int)(s.predicted_tick - s.verified_tick);
        TIDE_CHECK(p->Source.value == v->Source.value + lead);
        TIDE_CHECK(p->Middle.value == p->Source.value * 2);
        TIDE_CHECK(p->EventValue.value == p->Middle.value);
        TIDE_CHECK(p->QueryValue.calls == v->QueryValue.calls + lead);
        TIDE_CHECK(p->WorldValue.calls == v->WorldValue.calls + lead);
        TIDE_CHECK(p->SpawnPrerequisite.value == v->SpawnPrerequisite.value + lead);
        if (s.predicted_tick > 120) TIDE_CHECK(p->SpawnEffect.value == 121);
        if (s.predicted_tick > 153) TIDE_CHECK(p->TaskValue.done == 1);
        TIDE_CHECK(p->arch1_Owner_Controlled.count == 1);
        if (frame > 200) {
            const Controlled controlled = TIDE_AT(p, arch1_Owner_Controlled, Controlled, 0);
            // Events dispatch at the end of the tick; the query producer is
            // explicitly ordered before Control.
            TIDE_CHECK(controlled.value == p->Middle.value + p->EventValue.value - 2
                + p->QueryValue.calls + p->TaskValue.done);
        }
        TIDE_CHECK(p->Unrelated.calls <= v->Unrelated.calls);
        TIDE_CHECK(v->Unrelated.calls > 0);
        checked++;
    }
    const tide_client_status s = tide_client_status_of(client);
    TIDE_CHECK(checked > 100 && s.repairs == 0 && s.resyncs == 0);
    TIDE_CHECK(s.near_kept > 100);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

TIDE_TEST(owned_prediction_without_owned_entities_keeps_globals_confirmed)
{
    tide_world world = {0}, copy = {0}, loaded = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    TIDE_CHECK(tide_game_api.predict_far(&world, tide_player_from_index(0)));
    tide_world_copy(&copy, &world);
    tide_world_tick(&copy);
    TIDE_CHECK(copy.Source.value == 0 && copy.Unrelated.calls == 0);
    const uint32_t size = tide_world_pack(&copy, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes);
    TIDE_CHECK(tide_world_pack(&copy, bytes, size) == size);
    TIDE_CHECK(tide_world_unpack(&loaded, bytes, size));
    TIDE_CHECK(!loaded.tide_owned_predicting);
    for (unsigned i = 0; i < sizeof loaded.tide_owned_selected / sizeof loaded.tide_owned_selected[0]; i++)
        TIDE_CHECK(!loaded.tide_owned_selected[i]);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&loaded));
    tide_world_tick(&loaded); // No prediction marker: normal confirmed execution.
    TIDE_CHECK(loaded.Source.value == 1 && loaded.Unrelated.calls == 1);
    free(bytes);
    tide_world_free(&loaded); tide_world_free(&copy); tide_world_free(&world);
}
