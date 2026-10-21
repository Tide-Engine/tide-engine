// own.tide: a game whose identity, lobbies, quick match and network are all
// its own (own.c), with nothing of the engine or of the package changed for
// it. Four players in this program, each as a host runs one, go from a quick
// match's lobby into a match on the game's network, play it, and keep it
// when its host goes: the lobby's owner is who hosts next.
//
// Time is made up, a 60th of a second a frame, and the network is this
// program's memory, so a run repeats exactly. A step goes on once the
// players are where it needs them, and one not reached in WAIT_FRAMES fails.

#include "game.h"
#include "network_players.h"
#include "tide_test.h"

#define WAIT_FRAMES 3000

void OwnEnter(int32_t player);
void OwnReset(void);
int32_t OwnUpdates(void);
void OwnDeafen(int32_t player, bool on);

static player players[4];
#define ANN (&players[0])
#define BOB (&players[1])
#define CAT (&players[2])
#define DAN (&players[3])
#define CLIENT(p) ((p)->local.Lobbies_LobbyClient)
#define REMOTE(p) ((p)->local.Remote)

static double now;

static void enter(player *p)
{
    OwnEnter((int32_t)(p - players) + 1);
}

static void frame(void)
{
    now += 1.0 / 60.0;
    for (int i = 0; i < 4; i++) player_frame(&players[i], now, 1.0f / 60.0f);
}

// How every player stands, for a step that wasn't reached.
static void account(void)
{
    for (int i = 0; i < 4; i++) {
        player *p = &players[i];
        char lobby[32], named[32], match[32], owner[32];
        printf("  %s: state %d, server %d, open %d, room '%s', on its network %d, runners %d, lobby '%s' owned %d by '%s', "
               "its match '%s', in '%s', hosting %d, events %d/%d, reason %u\n",
               p->name, (int)p->status.client.state, (int)p->status.server, (int)p->status.open, p->room, (int)p->network.on,
               p->local.Remote.runners, player_text(p, p->local.Lobbies_LobbyClient.lobby.id, lobby, sizeof lobby),
               (int)p->local.Lobbies_LobbyClient.owned,
               player_text(p, p->local.Lobbies_LobbyClient.lobby.owner, owner, sizeof owner),
               player_text(p, p->local.Lobbies_LobbyClient.lobby.match, named, sizeof named),
               player_text(p, p->local.Lobbies_LobbyClient.match, match, sizeof match),
               (int)p->local.Lobbies_LobbyClient.hosting, p->connected, p->disconnected, (unsigned)p->reason);
    }
}

static bool until(bool (*reached)(void))
{
    for (int i = 0; i < WAIT_FRAMES; i++) {
        if (reached()) return true;
        frame();
    }
    account();
    return false;
}

static void order(player *p, const Order what, const char *text)
{
    player_set_text(p, &REMOTE(p).text, text);
    REMOTE(p).order = what;
}

static bool is(player *p, const tide_text text, const char *expected)
{
    char got[128];
    return strcmp(player_text(p, text, got, sizeof got), expected) == 0;
}

static bool in_lobby(player *p)
{
    return CLIENT(p).busy == Lobbies_LobbyBusy_Idle && !is(p, CLIENT(p).lobby.id, "");
}

// In the lobby's match, on another machine's server or its own
static bool plays(player *p, const char *room, const int runners)
{
    return player_in(p) && strcmp(p->room, room) == 0 && REMOTE(p).runners == runners;
}

static bool all_alone(void)
{
    for (int i = 0; i < 4; i++) {
        if (!player_in(&players[i]) || !players[i].status.server) return false;
    }
    return true;
}

static bool three_in_lobby(void)
{
    return in_lobby(ANN) && in_lobby(BOB) && in_lobby(CAT) && REMOTE(ANN).members == 3 && REMOTE(BOB).members == 3
           && REMOTE(CAT).members == 3;
}

static bool ready_seen(void)
{
    return REMOTE(ANN).ready == 1 && REMOTE(CAT).ready == 1;
}

static bool three_play(void)
{
    return plays(ANN, "p1", 3) && plays(BOB, "p1", 3) && plays(CAT, "p1", 3) && is(CAT, CLIENT(CAT).lobby.match, "p1");
}

static bool dan_listed(void)
{
    return REMOTE(DAN).listed == 1 && !CLIENT(DAN).listing;
}

static bool four_play(void)
{
    return plays(ANN, "p1", 4) && plays(BOB, "p1", 4) && plays(CAT, "p1", 4) && plays(DAN, "p1", 4);
}

static bool dan_out(void)
{
    return is(DAN, CLIENT(DAN).lobby.id, "") && player_in(DAN) && DAN->status.server && REMOTE(ANN).members == 3;
}

static bool all_ran(void)
{
    bool same = false;
    return REMOTE(ANN).mine > 0.5f && REMOTE(BOB).mine < -0.5f && REMOTE(CAT).mine > 0.5f && REMOTE(ANN).theirs != 0
           && REMOTE(BOB).theirs > 0.5f && players_agree(ANN, BOB, &same) > 0 && players_agree(ANN, CAT, &same) > 0;
}

// The match is Bob's, the lobby's owner now, and Cat is in it there
static bool bob_hosts(void)
{
    return player_in(BOB) && BOB->status.server && BOB->status.open && plays(BOB, "p2", 2) && plays(CAT, "p2", 2)
           && !CAT->status.server && is(CAT, CLIENT(CAT).lobby.match, "p2") && is(CAT, CLIENT(CAT).match, "p2");
}

static bool ann_alone(void)
{
    return player_in(ANN) && ANN->status.server && !ANN->status.open && is(ANN, CLIENT(ANN).lobby.id, "");
}

static int cat_gone_before;

static bool cat_gone(void)
{
    return CAT->disconnected > cat_gone_before;
}

static int dan_gone_before;

static bool dan_failed(void)
{
    return DAN->disconnected > dan_gone_before && DAN->reason == TIDE_DISCONNECT_FAILED;
}

TIDE_TEST(lobby_own_service_and_network_take_players_from_a_lobby_into_a_match_that_changes_hands)
{
    OwnReset();
    now = 0.0;
    static const char *const names[4] = {"Ann", "Bob", "Cat", "Dan"};
    for (int i = 0; i < 4; i++) {
        players[i].enter = enter;
        player_init(&players[i], names[i]);
        player_set_text(&players[i], &CLIENT(&players[i]).name, names[i]);
    }
    TIDE_REQUIRE(until(all_alone));
    TIDE_CHECK(tide_network_get() == NULL); // Nothing is registered until the game's transport slot is asked

    order(ANN, Order_Quick, "duel");
    frame();
    order(BOB, Order_Quick, "duel");
    order(CAT, Order_Quick, "duel");
    TIDE_REQUIRE(until(three_in_lobby));
    TIDE_CHECK(is(ANN, CLIENT(ANN).me.id, "p1") && is(BOB, CLIENT(BOB).me.id, "p2")); // The identity slot
    TIDE_CHECK(is(ANN, CLIENT(ANN).lobby.id, "101") && is(BOB, CLIENT(BOB).lobby.id, "101") && is(CAT, CLIENT(CAT).lobby.id, "101"));
    TIDE_CHECK(CLIENT(ANN).owned && !CLIENT(BOB).owned && !CLIENT(CAT).owned);

    order(BOB, Order_Ready, "");
    TIDE_REQUIRE(until(ready_seen));

    order(ANN, Order_Start, "");
    TIDE_REQUIRE(until(three_play));
    TIDE_CHECK(tide_network_get() != NULL && OwnUpdates() > 0);
    TIDE_CHECK(ANN->status.server && ANN->status.open && !BOB->status.server && !CAT->status.server);
    TIDE_CHECK(ANN->network.on && BOB->network.on && CAT->network.on);

    order(DAN, Order_List, "");
    frame();
    TIDE_REQUIRE(until(dan_listed));
    order(DAN, Order_Join, "");
    TIDE_REQUIRE(until(four_play));
    order(ANN, Order_Kick, "p4");
    TIDE_REQUIRE(until(dan_out));
    TIDE_CHECK(CLIENT(DAN).failure == Lobbies_LobbyFailure_Kicked);

    ANN->devices.keyboard.d.held = ANN->devices.keyboard.d.pressed = true;
    BOB->devices.keyboard.a.held = BOB->devices.keyboard.a.pressed = true;
    CAT->devices.keyboard.d.held = CAT->devices.keyboard.d.pressed = true;
    TIDE_REQUIRE(until(all_ran));
    bool same = false;
    TIDE_CHECK(players_agree(ANN, BOB, &same) > 30u && same);
    TIDE_CHECK(players_agree(ANN, CAT, &same) > 30u && same);
    TIDE_CHECK(BOB->status.client.resyncs == 0 && CAT->status.client.resyncs == 0);

    const int bob_events = BOB->connected + BOB->disconnected, cat_events = CAT->connected + CAT->disconnected;
    const float bob_was = REMOTE(BOB).mine;
    order(ANN, Order_Leave, "");
    TIDE_REQUIRE(until(bob_hosts));
    TIDE_CHECK(CLIENT(BOB).owned && is(BOB, CLIENT(BOB).lobby.owner, "p2"));
    TIDE_CHECK(BOB->connected + BOB->disconnected == bob_events && CAT->connected + CAT->disconnected == cat_events);
    TIDE_CHECK(REMOTE(BOB).mine <= bob_was);
    TIDE_REQUIRE(until(ann_alone));
    for (int i = 0; i < 30; i++) frame();
    TIDE_CHECK(players_agree(BOB, CAT, &same) > 0 && same);
    TIDE_CHECK(bob_hosts());

    OwnDeafen(3, true);
    cat_gone_before = CAT->disconnected;
    order(BOB, Order_End, "");
    TIDE_REQUIRE(until(cat_gone));
    TIDE_CHECK(CAT->reason == TIDE_DISCONNECT_ENDED);
    OwnDeafen(3, false);

    dan_gone_before = DAN->disconnected;
    order(DAN, Order_Stray, "");
    TIDE_REQUIRE(until(dan_failed));

    for (int i = 0; i < 4; i++) player_free(&players[i]);
    OwnReset();
}
