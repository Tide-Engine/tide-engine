// The steam package, against a Steam that isn't (fake_steam.c, linked in the
// SDK's library's place): four Steam users in this program, each a player
// as a host runs one, with a session of its own. The game lists `lobby` and
// `steam` and writes nothing for Steam itself: the package's overrides of
// the four slots, its C over Steamworks' flat API, and its network
// (tide/network.h) do all of it.
//
// It checks the package's own code. It can't check Steam: what the real one
// does otherwise than the fake is found with the real one (README.md).
//
// Time is made up, a 60th of a second a frame, so a run repeats exactly. A
// step goes on once the players are where it needs them, and one not
// reached in WAIT_FRAMES frames fails with how every player stands.

#include "game.h"
#include "network_players.h"
#include "tide_test.h"

#define WAIT_FRAMES 3000

void FakeSteamUser(int32_t number);
void FakeSteamReset(void);
void FakeSteamDeafen(int32_t number, bool on);
int32_t FakeSteamStarts(void);
const char *FakeSteamData(const char *key);
void FakeSteamForge(const char *key, const char *value);
void TideSteamUse(int32_t user);
void TideSteamForget(void);

// The fake's users' Steam IDs, and its first lobby's
#define ANN_ID "76561197960265729"
#define BOB_ID "76561197960265730"
#define CAT_ID "76561197960265731"
#define DAN_ID "76561197960265732"
#define LOBBY_ID "109775240917155841"

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
    const int32_t number = (int32_t)(p - players);
    FakeSteamUser(number + 1);
    TideSteamUse(number);
}

static void frame(void)
{
    now += 1.0 / 60.0;
    for (int i = 0; i < 4; i++) player_frame(&players[i], now, 1.0f / 60.0f);
}

static bool is(player *p, const tide_text text, const char *expected)
{
    char got[256];
    return strcmp(player_text(p, text, got, sizeof got), expected) == 0;
}

static void account(void)
{
    for (int i = 0; i < 4; i++) {
        player *p = &players[i];
        char lobby[32], named[32], match[32], owner[32], message[128], who[128];
        printf("  %s: state %d, server %d, open %d, room '%s', on its network %d, runners %d, lobby '%s' of %d (%s) owned %d "
               "by '%s', its match '%s', in '%s', hosting %d, busy %d, failure %d '%s', events %d/%d, reason %u\n",
               p->name, (int)p->status.client.state, (int)p->status.server, (int)p->status.open, p->room, (int)p->network.on,
               REMOTE(p).runners, player_text(p, CLIENT(p).lobby.id, lobby, sizeof lobby), REMOTE(p).members,
               player_text(p, REMOTE(p).names, who, sizeof who), (int)CLIENT(p).owned,
               player_text(p, CLIENT(p).lobby.owner, owner, sizeof owner),
               player_text(p, CLIENT(p).lobby.match, named, sizeof named), player_text(p, CLIENT(p).match, match, sizeof match),
               (int)CLIENT(p).hosting, (int)CLIENT(p).busy, (int)CLIENT(p).failure,
               player_text(p, CLIENT(p).message, message, sizeof message), p->connected, p->disconnected, (unsigned)p->reason);
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

static bool in_lobby(player *p)
{
    return CLIENT(p).busy == Lobbies_LobbyBusy_Idle && !is(p, CLIENT(p).lobby.id, "");
}

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

static bool ann_searches(void)
{
    return CLIENT(ANN).ticket.status == Lobbies_TicketStatus_Searching && !is(ANN, CLIENT(ANN).ticket.id, "");
}

static bool ann_made(void)
{
    return in_lobby(ANN) && CLIENT(ANN).owned;
}

static bool three_in_lobby(void)
{
    return in_lobby(ANN) && in_lobby(BOB) && in_lobby(CAT) && REMOTE(ANN).members == 3 && REMOTE(BOB).members == 3
           && REMOTE(CAT).members == 3;
}

static bool data_seen(void)
{
    return REMOTE(ANN).ready == 1 && REMOTE(CAT).ready == 1 && is(ANN, REMOTE(ANN).colors, "-,-,teal")
           && is(BOB, REMOTE(BOB).colors, "-,-,teal") && is(CAT, CLIENT(CAT).lobby.name, "Friday");
}

static bool forged_seen(void)
{
    return is(BOB, CLIENT(BOB).lobby.name, "Fri?day\"\\");
}

static bool three_play(void)
{
    return plays(ANN, ANN_ID, 3) && plays(BOB, ANN_ID, 3) && plays(CAT, ANN_ID, 3) && is(CAT, CLIENT(CAT).lobby.match, ANN_ID);
}

static bool dan_listed(void)
{
    return REMOTE(DAN).listed == 1 && !CLIENT(DAN).listing;
}

static bool four_play(void)
{
    return plays(ANN, ANN_ID, 4) && plays(BOB, ANN_ID, 4) && plays(CAT, ANN_ID, 4) && plays(DAN, ANN_ID, 4);
}

static bool dan_out(void)
{
    return is(DAN, CLIENT(DAN).lobby.id, "") && player_in(DAN) && DAN->status.server && REMOTE(ANN).members == 3
           && REMOTE(ANN).runners == 3;
}

static bool dan_settled(void)
{
    return CLIENT(DAN).busy == Lobbies_LobbyBusy_Idle && CLIENT(DAN).failure != Lobbies_LobbyFailure_None;
}

static bool all_ran(void)
{
    bool same = false;
    return REMOTE(ANN).mine > 0.5f && REMOTE(BOB).mine < -0.5f && REMOTE(CAT).mine > 0.5f && REMOTE(ANN).theirs != 0
           && REMOTE(BOB).theirs > 0.5f && players_agree(ANN, BOB, &same) > 0 && players_agree(ANN, CAT, &same) > 0;
}

static bool bob_hosts(void)
{
    return player_in(BOB) && BOB->status.server && BOB->status.open && plays(BOB, BOB_ID, 2) && plays(CAT, BOB_ID, 2)
           && !CAT->status.server && is(CAT, CLIENT(CAT).lobby.match, BOB_ID) && is(CAT, CLIENT(CAT).match, BOB_ID);
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

TIDE_TEST(steam_lobbies_and_messages_take_players_into_a_match_that_follows_the_lobbys_owner)
{
    FakeSteamReset();
    TideSteamForget();
    now = 0.0;
    static const char *const names[4] = {"Ann", "Bob", "Cat", "Dan"};
    for (int i = 0; i < 4; i++) {
        players[i].enter = enter;
        player_init(&players[i], names[i]);
    }
    TIDE_REQUIRE(until(all_alone));
    TIDE_CHECK(FakeSteamStarts() == 0); // Steam isn't started until a player asks something of it

    // Matchmaking: a ticket searches while Steam answers, and ends in a lobby the first to ask made
    order(ANN, Order_Quick, "duel");
    TIDE_REQUIRE(until(ann_searches));
    TIDE_REQUIRE(until(ann_made));
    // Identity: the Steam user, by its ID and its name
    TIDE_CHECK(is(ANN, CLIENT(ANN).me.id, ANN_ID) && is(ANN, CLIENT(ANN).name, "Ann"));
    TIDE_CHECK(is(ANN, CLIENT(ANN).lobby.id, LOBBY_ID) && is(ANN, CLIENT(ANN).lobby.owner, ANN_ID));
    TIDE_CHECK(strcmp(FakeSteamData("mode"), "duel") == 0 && strcmp(FakeSteamData("name"), "Quick") == 0);
    // ...which the next two find, where another mode's search would make its own
    order(BOB, Order_Quick, "duel");
    order(CAT, Order_Quick, "duel");
    TIDE_REQUIRE(until(three_in_lobby));
    TIDE_CHECK(is(BOB, CLIENT(BOB).lobby.id, LOBBY_ID) && is(CAT, CLIENT(CAT).lobby.id, LOBBY_ID));
    TIDE_CHECK(CLIENT(ANN).owned && !CLIENT(BOB).owned && !CLIENT(CAT).owned);
    // A name that a JSON writer has to mind arrives whole
    TIDE_CHECK(is(ANN, REMOTE(ANN).names, "Ann,Bob,Cat \"C\\\" Ray"));

    // Lobbies: a member's data and the owner's changes reach the others through Steam's callbacks
    order(BOB, Order_Ready, "");
    order(CAT, Order_Color, "teal");
    order(ANN, Order_Rename, "Friday");
    TIDE_REQUIRE(until(data_seen));
    // What another program wrote into the lobby's data is another player's bytes: text that isn't UTF-8 is
    // made text, and the package's own keys aren't the game's data
    FakeSteamUser(1);
    FakeSteamForge("name", "Fri\xFF" "day\"\\");
    FakeSteamForge("tide_match", "not a Steam ID");
    TIDE_REQUIRE(until(forged_seen));
    TIDE_CHECK(is(BOB, CLIENT(BOB).lobby.match, "") && player_in(BOB) && BOB->status.server);
    FakeSteamUser(1);
    FakeSteamForge("tide_match", "");

    // Transport: the owner's match opens on Steam's messages, joined by its Steam ID, which the lobby names
    order(ANN, Order_Start, "");
    TIDE_REQUIRE(until(three_play));
    TIDE_CHECK(ANN->status.server && ANN->status.open && !BOB->status.server && !CAT->status.server);
    TIDE_CHECK(ANN->network.on && BOB->network.on && CAT->network.on);
    TIDE_CHECK(strcmp(FakeSteamData("tide_match"), ANN_ID) == 0);

    // A fourth finds the lobby in a list of its mode, joins it, and is in its match
    order(DAN, Order_List, "duel");
    frame();
    TIDE_REQUIRE(until(dan_listed));
    order(DAN, Order_Join, "");
    TIDE_REQUIRE(until(four_play));
    // A kick is the owner's list in the lobby's data, which the listed player's own program obeys...
    order(ANN, Order_Kick, DAN_ID);
    TIDE_REQUIRE(until(dan_out));
    TIDE_CHECK(CLIENT(DAN).failure == Lobbies_LobbyFailure_Kicked);
    TIDE_CHECK(strcmp(FakeSteamData("tide_kicked"), DAN_ID ",") == 0);
    // ...and which keeps it out
    order(DAN, Order_Join, "");
    frame();
    TIDE_REQUIRE(until(dan_settled));
    TIDE_CHECK(CLIENT(DAN).failure == Lobbies_LobbyFailure_Kicked && is(DAN, CLIENT(DAN).lobby.id, ""));

    // The match's packets go over Steam's messages: each runs their own way, and every machine's world is the same
    ANN->devices.keyboard.d.held = ANN->devices.keyboard.d.pressed = true;
    BOB->devices.keyboard.a.held = BOB->devices.keyboard.a.pressed = true;
    CAT->devices.keyboard.d.held = CAT->devices.keyboard.d.pressed = true;
    TIDE_REQUIRE(until(all_ran));
    bool same = false;
    TIDE_CHECK(players_agree(ANN, BOB, &same) > 30u && same);
    TIDE_CHECK(players_agree(ANN, CAT, &same) > 30u && same);
    TIDE_CHECK(BOB->status.client.resyncs == 0 && CAT->status.client.resyncs == 0);
    TIDE_CHECK(BOB->status.sent_bytes > 0 && BOB->status.received_bytes > 0);

    // Host migration: the host leaves the lobby, and Steam hands it to Bob. Bob's machine takes the match
    // over, Cat's joins it at Bob's Steam ID, and the lobby names the match by its new host
    const int bob_events = BOB->connected + BOB->disconnected, cat_events = CAT->connected + CAT->disconnected;
    const float bob_was = REMOTE(BOB).mine;
    order(ANN, Order_Leave, "");
    TIDE_REQUIRE(until(bob_hosts));
    TIDE_CHECK(CLIENT(BOB).owned && is(BOB, CLIENT(BOB).lobby.owner, BOB_ID));
    TIDE_CHECK(BOB->connected + BOB->disconnected == bob_events && CAT->connected + CAT->disconnected == cat_events);
    TIDE_CHECK(REMOTE(BOB).mine <= bob_was); // The match went on from where it was
    TIDE_CHECK(strcmp(FakeSteamData("tide_match"), BOB_ID) == 0);
    TIDE_REQUIRE(until(ann_alone));
    for (int i = 0; i < 30; i++) frame();
    TIDE_CHECK(players_agree(BOB, CAT, &same) > 0 && same);
    TIDE_CHECK(bob_hosts());

    // A match its host ended isn't taken over. Cat hears nothing more (the goodbye is lost with the rest),
    // goes to the lobby as for a host that went, and reads there that the match ended
    FakeSteamDeafen(3, true);
    cat_gone_before = CAT->disconnected;
    order(BOB, Order_End, "");
    TIDE_REQUIRE(until(cat_gone));
    TIDE_CHECK(CAT->reason == TIDE_DISCONNECT_ENDED);
    TIDE_CHECK(strcmp(FakeSteamData("tide_ended"), BOB_ID) == 0);
    FakeSteamDeafen(3, false);

    TIDE_CHECK(FakeSteamStarts() == 4); // Once a user
    for (int i = 0; i < 4; i++) player_free(&players[i]);
    TideSteamForget();
    FakeSteamReset();
}
