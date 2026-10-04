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

static void sample(void *user, const uint32_t tick, void *input)
{
    (void)tick;
    // Each machine pushes its own number plus one, so whose input reached whom shows in the sums
    *(Controls *)input = (Controls){.push = (int32_t)(intptr_t)user + 1};
}

static void setup(void)
{
    now = 0.0;
    net = tide_loopback_create(5);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.02});
    for (int i = 0; i < MACHINES; i++) {
        machines[i] = tide_session_create(&(tide_session_desc){.game = &tide_game_api, .sample = sample, .user = (void *)(intptr_t)i});
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

// How many players a scene says see it: the bits of its words.
static int seated(const Table t)
{
    const int32_t words[] = {t.tide_players, t.tide_players1, t.tide_players2, t.tide_players3, t.tide_players4,
                             t.tide_players5, t.tide_players6};
    int n = 0;
    for (size_t w = 0; w < sizeof words / sizeof words[0]; w++) {
        for (int b = 0; b < 32; b++) n += (int)((uint32_t)words[w] >> b & 1u);
    }
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
    TIDE_CHECK(seated(tide_get_Table(w, w->Match.table)) == MACHINES); // Players past the first word too

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
    TIDE_CHECK(seated(tide_get_Table(w, w->Match.table)) == MACHINES - 1);
    teardown();
}
