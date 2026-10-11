// The example's game on its own network, as the docs' walkthrough has it:
// two players in this program, each as a host runs one, in a match that goes
// over udp.c's sockets on this machine. Nothing of the engine knows that
// network: the game's Tide asks Session.Open() and Session.Join(text), and
// its C registered what serves them.
//
// Nothing waits a time: a step goes on once the players are where it needs
// them, a frame at a time, and one not reached in WAIT_FRAMES frames fails.
// The sockets are the system's, so a frame lets a millisecond pass.

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
#define _DEFAULT_SOURCE // nanosleep under strict C
#endif

#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__wasm__)
#include <time.h>
#endif

#include "game.h"
#include "tide/network.h"
#include "tide/time.h"
#include "tide_test.h"

#define WAIT_FRAMES 20000

bool UseUdpNetwork(void);

#if defined(__wasm__)

// A page has no UDP: the game builds there all the same, registers nothing,
// and its matches are in Tide's rooms.
TIDE_TEST(network_udp_is_no_network_in_a_page)
{
    TIDE_CHECK(!UseUdpNetwork());
    TIDE_CHECK(tide_network_get() == NULL);
}

#else

#include "network_players.h"

static player players[2];
#define ANN (&players[0])
#define BOB (&players[1])

static double began;

static double now(void)
{
    return (double)tide_time_now_ns() * 1e-9 - began;
}

static void frame(void)
{
#ifdef _WIN32
    Sleep(1);
#else
    nanosleep(&(struct timespec){.tv_nsec = 1000000}, NULL);
#endif
    for (int i = 0; i < 2; i++) player_frame(&players[i], now(), 1.0f / 60.0f);
}

static void order(player *p, const Order what, const char *text)
{
    player_set_text(p, &p->local.Remote.text, text);
    p->local.Remote.order = what;
}

// Frames until `reached` says so: false if it never does.
static bool until(bool (*reached)(void))
{
    for (int i = 0; i < WAIT_FRAMES; i++) {
        if (reached()) return true;
        frame();
    }
    return false;
}

static bool ann_hosts(void)
{
    return player_in(ANN) && ANN->status.server && ANN->status.open && ANN->room[0];
}

static bool both_in(void)
{
    return player_in(ANN) && player_in(BOB) && ANN->local.Seen.runners == 2 && BOB->local.Seen.runners == 2;
}

static bool both_ran(void)
{
    bool same = false;
    return ANN->local.Seen.mine > 0.5f && ANN->local.Seen.theirs < -0.5f && BOB->local.Seen.mine < -0.5f
           && BOB->local.Seen.theirs > 0.5f && players_agree(ANN, BOB, &same) > 0;
}

static bool bob_out(void)
{
    return BOB->disconnected > 0 && !player_in(BOB);
}

TIDE_TEST(network_udp_two_players_play_a_match_on_the_games_own_sockets)
{
    began = (double)tide_time_now_ns() * 1e-9;
    player_init(ANN, "Ann");
    player_init(BOB, "Bob");
    TIDE_CHECK(tide_network_get() == NULL); // Nothing until the game's own code says

    // Ann's menu hosts: Session.Start, Session.Open, and the network her C registered
    order(ANN, Order_Host, "");
    TIDE_REQUIRE(until(ann_hosts));
    TIDE_CHECK(tide_network_get() != NULL);
    TIDE_CHECK(strncmp(ANN->room, "127.0.0.1:", 10) == 0); // Session.room is what the network says
    char said[TIDE_NETWORK_ADDRESS_BYTES];
    TIDE_CHECK(strcmp(player_text(ANN, ANN->local.Menu.address, said, sizeof said), "") == 0);

    // Bob joins by that text
    order(BOB, Order_Join, ANN->room);
    TIDE_REQUIRE(until(both_in));
    TIDE_CHECK(BOB->connected == 1 && !BOB->status.server);
    TIDE_CHECK(strcmp(BOB->room, ANN->room) == 0);

    // Each runs their own way, and both machines see both, in worlds that are the same
    ANN->devices.keyboard.d.held = ANN->devices.keyboard.d.pressed = true;
    BOB->devices.keyboard.a.held = BOB->devices.keyboard.a.pressed = true;
    TIDE_REQUIRE(until(both_ran));
    bool same = false;
    TIDE_CHECK(players_agree(ANN, BOB, &same) > 30u && same); // Half a unit takes 30 ticks
    TIDE_CHECK(BOB->status.client.resyncs == 0);
    TIDE_CHECK(ANN->status.sent_bytes > 0 && ANN->status.received_bytes > 0); // Over a network, not this program's memory

    // Ann ends the match: Bob hears it over the same sockets
    order(ANN, Order_End, "");
    TIDE_REQUIRE(until(bob_out));
    TIDE_CHECK(BOB->reason == TIDE_DISCONNECT_ENDED);

    // Text that's no address joins nothing, at once
    const int before = BOB->disconnected;
    order(BOB, Order_Join, "not an address");
    frame();
    frame();
    TIDE_CHECK(BOB->disconnected == before + 1 && BOB->reason == TIDE_DISCONNECT_FAILED);

    player_free(BOB);
    player_free(ANN);
    tide_network_set(NULL);
}

#endif
