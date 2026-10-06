#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// A server and a player on a loopback network, in made-up time, with updates
// a tenth of a second apart: the server sends 64 KB of the world in each, so
// the world's 4 MB take about six seconds, more than the four seconds of
// ticks it keeps. The ticks the player needs once it has the world are kept
// until it catches up, rather than the world being sent again and again.

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    Controls *c = input;
    *c = (Controls){.value = (int32_t)(tick % 7u)};
}

TIDE_TEST(net_join_big_world_catches_up)
{
    tide_loopback *net = tide_loopback_create(7);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02, .loss = 0.01});
    double now = 0.0;
    const tide_server_desc server_desc = {.game = &tide_game_api, .tick_rate = 60, .transports = {tide_loopback_endpoint(net, 1)}};
    tide_server *server = tide_server_create(&server_desc, now);
    TIDE_REQUIRE(server != NULL);
    const tide_client_desc client_desc = {
        .game = &tide_game_api,
        .transport = tide_loopback_endpoint(net, 2),
        .server = tide_loopback_address(1),
        .sample = sample,
        .lead = 2,
    };
    tide_client *client = tide_client_create(&client_desc, now);
    TIDE_REQUIRE(client != NULL);

    double connected = 0.0;
    while (now < 20.0) {
        now += 0.1;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
        if (!connected && tide_client_status_of(client).state == TIDE_SESSION_CONNECTED) connected = now;
    }

    const tide_client_status s = tide_client_status_of(client);
    printf("    the world arrived after %.1f s\n", connected);
    TIDE_CHECK(connected > 4.0); // Longer than the ticks the server keeps
    TIDE_CHECK(s.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(s.resyncs == 0);
    TIDE_CHECK(s.verified_tick + 30u > tide_server_tick(server)); // Caught up
    const tide_world *world = tide_client_verified_world(client);
    TIDE_CHECK(world && world->Archive.total > 0); // Its inputs reached the match

    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}

// A player who joins runs no further ahead than its inputs need, however long
// its world took to arrive. The server runs ticks for it meanwhile, with no
// input of its to run them with, as it has no world to sample one for: those
// aren't inputs that came late. Counted as late, by as long as the world took,
// they had the player set its clock that far ahead once it was in, on a
// network late and lossy enough that this was still the last thing it had
// heard: a second and more, where it then stayed for a long while, running
// every tick it predicts again and again, with the other players seeing what
// it did that much later.
TIDE_TEST(net_join_big_world_runs_no_further_ahead_than_its_inputs_need)
{
    uint32_t furthest = 0;
    float longest = 0.0f;
    for (uint32_t seed = 1; seed <= 8; seed++) {
        tide_loopback *net = tide_loopback_create(seed);
        tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.04, .jitter = 0.015, .loss = 0.05});
        double now = 0.0;
        tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                                   .transports = {tide_loopback_endpoint(net, 1)}}, now);
        TIDE_REQUIRE(server != NULL);
        tide_client *client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                                                   .transport = tide_loopback_endpoint(net, 2),
                                                   .server = tide_loopback_address(1), .sample = sample, .lead = 2}, now);
        TIDE_REQUIRE(client != NULL);

        // Updates a 60th of a second apart: the world takes a second and more
        double connected = 0.0;
        uint32_t world_took = 0;
        uint32_t ahead = 0;
        float waited = 0.0f;
        while (connected == 0.0 || now < connected + 4.0) {
            TIDE_REQUIRE(now < 30.0);
            now += 1.0 / 60;
            tide_loopback_set_time(net, now);
            tide_client_update(client, now);
            tide_server_update(server, now);
            const tide_client_status s = tide_client_status_of(client);
            if (s.state != TIDE_SESSION_CONNECTED) continue;
            if (connected == 0.0) {
                connected = now;
                world_took = tide_server_tick(server);
            }
            // Not its first second: it starts from a world as old as the world took to come,
            // and predicts from that until the ticks since have come too, a round trip later
            if (now < connected + 1.0) continue;
            if (s.predicted_tick - s.verified_tick > ahead) ahead = s.predicted_tick - s.verified_tick;
            if (s.early_ms > waited) waited = s.early_ms;
        }
        const tide_client_status s = tide_client_status_of(client);
        TIDE_CHECK(world_took > 60u); // Ticks the server ran for the player before it had the world
        TIDE_CHECK(s.resyncs == 0);
        TIDE_CHECK(s.early_ms >= 0.0f); // In time, at the end
        const tide_world *world = tide_client_verified_world(client);
        TIDE_CHECK(world && world->Archive.total > 0);
        if (ahead > furthest) furthest = ahead;
        if (waited > longest) longest = waited;

        tide_client_destroy(client);
        tide_server_destroy(server);
        tide_loopback_destroy(net);
    }
    // A round trip here is about 7 ticks, and the lead 2. With the ticks
    // before it had the world counted as late, it ran the 60 a client may,
    // its inputs waiting most of a second.
    printf("    of 8 networks, the furthest a player ran ahead of what's confirmed, from a second after it joined, was %u"
           " ticks, and the longest its inputs waited %.0f ms\n", furthest, (double)longest);
    TIDE_CHECK(furthest <= 20u);
    TIDE_CHECK(longest <= 150.0f);
}
