#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

// A match of 150 players, each a session on one loopback network, in a game
// whose maxPlayers is 200: more than a 32-bit set of players held, and more
// digests than one datagram does.

#define MACHINES 150

static tide_loopback *net;
static tide_session *machines[MACHINES];
static double now;
static tide_game full_world_game;

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)tick;
    // Each machine pushes its own number plus one, so whose input reached whom shows in the sums
    *(Controls *)input = (Controls){.push = (int32_t)(intptr_t)user + 1};
}

static void setup(void)
{
    // This case explicitly exercises the trusted full-world handover path.
    // The same generated game tests actual private projection separately below;
    // partial clients must never be promoted into authoritative hosts.
    full_world_game = tide_game_api;
    full_world_game.client_state = NULL;
    full_world_game.load_client_state = NULL;
    full_world_game.client_hash = NULL;
    full_world_game.filter_client_input = NULL;
    full_world_game.filter_client_inputs = NULL;
    full_world_game.client_destroyed = NULL;
    full_world_game.hear_visibility = NULL;
    full_world_game.tick_client_events = NULL;
    full_world_game.check_client_events = NULL;
    now = 0.0;
    net = tide_loopback_create(5);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02});
    for (int i = 0; i < MACHINES; i++) {
        machines[i] = tide_session_create(&(tide_session_desc){.game = &full_world_game, .sample = sample, .user = (void *)(intptr_t)i});
    }
}

static void teardown(void)
{
    for (int i = 0; i < MACHINES; i++) tide_session_destroy(machines[i]);
    tide_loopback_destroy(net);
}

static void run(const double seconds)
{
    const double until = now + seconds;
    while (now < until) {
        now += 1.0 / 60.0;
        tide_loopback_set_time(net, now);
        for (int i = 0; i < MACHINES; i++) {
            tide_session_update(machines[i], now);
            tide_session_event e;
            while (tide_session_next_event(machines[i], &e)) {
            }
        }
    }
}

static tide_session_status status(const int i)
{
    return tide_session_status_of(machines[i]);
}

// Count the scene root's effective audience across every configured word.
static int seated(const tide_world *w)
{
    int n = 0;
    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++)
        n += tide_world_visible(w, w->Match.table, tide_player_from_index(i));
    return n;
}

TIDE_TEST(players_a_game_says_how_many_its_matches_take)
{
    TIDE_CHECK(TIDE_GAME_PLAYERS == 200u);
    TIDE_CHECK(tide_game_api.max_players == 200u);
    TIDE_CHECK(tide_game_players(&tide_game_api) == 200u);
    // The world keeps an input for each, and the server's after them
    TIDE_CHECK(sizeof(((tide_world *)0)->inputs) == 201u * sizeof(Controls));
    // Players past the game's are no one's: their input goes nowhere
    static tide_world world;
    tide_world_init(&world, 1.0f);
    tide_world_set_input(&world, tide_player_from_index(199), (Controls){.push = 7});
    tide_world_set_input(&world, tide_player_from_index(200), (Controls){.push = 9});
    TIDE_CHECK(world.inputs[199].push == 7);
    TIDE_CHECK(world.inputs[TIDE_SERVER_INPUT].push == 0);
    tide_world_free(&world);
}

TIDE_TEST(players_private_scene_projects_membership_across_all_200_players)
{
    tide_world server = {0}, member = {0}, excluded = {0};
    tide_world_init(&server, 1.0f / 60.0f);
    for (int32_t player = 0; player < (int32_t)TIDE_GAME_PLAYERS; player++)
        tide_world_player_joined(&server, tide_player_from_index(player));
    tide_world_tick(&server);
    const tide_entity table = server.Match.table;
    TIDE_CHECK(server.Match.joined == 200 && seated(&server) == 200);
    TIDE_REQUIRE(tide_game_api.client_state && tide_game_api.load_client_state);
    const tide_player_id last = tide_player_from_index(199);
    uint32_t size;
    uint8_t *bytes = tide_game_api.client_state(&server, last, &size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(tide_game_api.load_client_state(&member, bytes, size));
    TIDE_CHECK(tide_has_Table(&member, table));
    TIDE_CHECK(tide_game_api.client_hash(&member, last) == tide_game_api.client_hash(&server, last));
    free(bytes);

    // The last configured bit of the seventh word restricts state delivery too.
    tide_world_player_left(&server, last);
    tide_world_tick(&server);
    TIDE_CHECK(server.Match.left == 1 && seated(&server) == 199);
    bytes = tide_game_api.client_state(&server, last, &size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(tide_game_api.load_client_state(&excluded, bytes, size));
    TIDE_CHECK(!tide_entity_alive(&excluded.entities, table) && !tide_has_Table(&excluded, table));
    free(bytes);
    bytes = tide_game_api.client_state(&server, tide_player_from_index(198), &size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_CHECK(tide_game_api.load_client_state(&member, bytes, size));
    TIDE_CHECK(tide_has_Table(&member, table));
    free(bytes);
    tide_world_free(&excluded);
    tide_world_free(&member);
    tide_world_free(&server);
}

TIDE_TEST(players_a_match_takes_more_than_a_word_of_them)
{
    setup();
    tide_session_start(machines[0], NULL, now);
    tide_session_open(machines[0], tide_loopback_endpoint(net, 1));
    tide_session_set_room(machines[0], "ABCDEF", "key1");
    for (int i = 1; i < MACHINES; i++) {
        tide_session_join(machines[i], tide_loopback_endpoint(net, (uint32_t)i + 1u), tide_loopback_address(1), now);
    }
    run(3.0);
    // Everyone's in, each a player of their own
    bool taken[TIDE_GAME_PLAYERS] = {false};
    int highest = -1;
    for (int i = 0; i < MACHINES; i++) {
        TIDE_REQUIRE(status(i).client.state == TIDE_SESSION_CONNECTED);
        const int32_t player = tide_player_index(status(i).client.player);
        TIDE_REQUIRE(player >= 0 && player < (int32_t)TIDE_GAME_PLAYERS && !taken[player]);
        taken[player] = true;
        if (player > highest) highest = player;
    }
    TIDE_CHECK(highest == MACHINES - 1);
    const tide_world *w = tide_session_server_world(machines[0]);
    TIDE_REQUIRE(w);
    TIDE_CHECK(w->Match.joined == MACHINES && w->Match.left == 0);
    TIDE_CHECK(seated(w) == MACHINES); // Players past the first word too

    // Every player's input reaches the match: a second of ticks adds each one's push 60 times
    const int64_t before = w->Match.pushes;
    const int32_t ticks = w->Match.ticks;
    run(1.0);
    w = tide_session_server_world(machines[0]);
    const int64_t each = (int64_t)MACHINES * (MACHINES + 1) / 2; // 1 + 2 + ... + 150
    TIDE_CHECK(w->Match.pushes - before == each * (w->Match.ticks - ticks));
    // ...and every machine agrees on the world
    const uint32_t verified = status(MACHINES - 1).client.verified_tick;
    TIDE_CHECK(verified + 30u > (uint32_t)w->Match.ticks);

    // The host leaves: its record of 149 players and their digests came in parts,
    // and the player who takes the match over has them all back as themselves
    uint32_t ids[MACHINES];
    for (int i = 0; i < MACHINES; i++) ids[i] = status(i).client.player.id;
    tide_session_leave(machines[0]);
    run(0.2);
    char code[TIDE_ROOM_CODE_LENGTH + 1];
    char key[TIDE_ROOM_KEY_LENGTH + 1];
    for (int i = 1; i < MACHINES; i++) TIDE_REQUIRE(tide_session_migrating(machines[i], code, key));
    TIDE_CHECK(strcmp(code, "ABCDEF") == 0 && strcmp(key, "key1") == 0);
    tide_session_take_over(machines[1], tide_loopback_endpoint(net, 300), now);
    for (int i = 2; i < MACHINES; i++) {
        tide_session_join(machines[i], tide_loopback_endpoint(net, 300u + (uint32_t)i), tide_loopback_address(300), now);
    }
    run(3.0);
    for (int i = 1; i < MACHINES; i++) {
        TIDE_REQUIRE(status(i).client.state == TIDE_SESSION_CONNECTED);
        TIDE_CHECK(status(i).client.player.id == ids[i]);
    }
    w = tide_session_server_world(machines[1]);
    TIDE_REQUIRE(w);
    TIDE_CHECK(w->Match.joined == MACHINES); // No one joined again
    TIDE_CHECK(w->Match.left == 1);          // The last host's player
    TIDE_CHECK(seated(w) == MACHINES - 1);
    teardown();
}
