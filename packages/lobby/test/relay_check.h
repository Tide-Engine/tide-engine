// Four players of relay.tide against a relay on this machine, each a local
// state of its own in this program: what the package's client and the
// relay's lobbies do together, from a player's first request to its last.
// Shared by the native host (relay_test.c, against the relay relay.mjs
// starts) and the page's (relay_web.c, against the server that serves the
// page, with relay_routes.mjs answering).
//
// Nothing here waits a time. Each step asks something of a player and goes
// on once every player it concerns sees it, a frame at a time: a change
// reaches the others through the answers the relay holds for them. A step
// that isn't reached in LOBBY_WAIT_FRAMES frames fails the test, with how
// every player stands.
#pragma once

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/session.h"

#define ROOM "K7QF2M"

typedef struct seat {
    const char *name;
    tide_local local;
    tide_gui gui;
    tide_devices devices;
    tide_draw_list draw;
    uint32_t asked;                 // What it last asked of the session...
    char said[TIDE_MESSAGE_BYTES];  // ...and with which text
} seat;

static seat seats[4] = {{.name = "Ann"}, {.name = "Bob"}, {.name = "Cat"}, {.name = "Dan"}};
#define ANN (&seats[0])
#define BOB (&seats[1])
#define CAT (&seats[2])
#define DAN (&seats[3])
#define CLIENT(s) ((s)->local.Lobbies_LobbyClient)
#define SEEN(s) ((s)->local.Remote)

static int lobby_failures;

static void lobby_check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) lobby_failures++;
}

static const char *text_of(seat *s, const tide_text text, char *out, const size_t room)
{
    const tide_str read = tide_text_read(&s->local.heap, text);
    const size_t bytes = (size_t)read.bytes < room - 1 ? (size_t)read.bytes : room - 1;
    memcpy(out, read.ptr, bytes);
    out[bytes] = 0;
    return out;
}

static bool is(seat *s, const tide_text text, const char *expected)
{
    char got[256];
    return strcmp(text_of(s, text, got, sizeof got), expected) == 0;
}

static void set_text(seat *s, tide_text *field, const char *value)
{
    tide_text_use(NULL, &s->local.heap);
    tide_text_set(field, tide_str_from_cstr(value), TIDE_IN_LOCAL);
}

// A player as its program starts, in a match of its own, with the service at `origin`
static void seat_init(seat *s, const tide_io *io, const char *origin)
{
    tide_local_set_io(&s->local, io);
    tide_local_init(&s->local);
    set_text(s, &CLIENT(s).service, origin);
    set_text(s, &CLIENT(s).name, s->name);
    tide_local_set_session(&s->local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
}

static void seat_free(seat *s)
{
    tide_local_free(&s->local);
    tide_draw_free(&s->draw);
}

// A frame as a host runs one, with no window and no match: the views, the
// handlers and the tasks. What the player asked of the session is noted.
static void seat_frame(seat *s, const float seconds)
{
    tide_local_frame_time(&s->local, seconds);
    tide_draw_reset(&s->draw);
    tide_touches_poll(&s->devices.touchscreen);
    tide_pointer_poll(&s->devices, true);
    tide_gui_begin(&s->gui, &s->devices, tide_f2(376, 600), NULL);
    tide_frame(NULL, NULL, 1.0f, NULL, NULL, 1.0f, &s->local, &s->draw, &s->gui);
    tide_gui_end(&s->gui, &s->draw);
    tide_session_request request = {0};
    tide_start start;
    while (tide_local_take_request(&s->local, &request, &start)) {
        s->asked = request.kind;
        memcpy(s->said, request.text, sizeof s->said);
    }
}

static void order(seat *s, const Order what, const char *text, const char *value)
{
    set_text(s, &SEEN(s).text, text);
    set_text(s, &SEEN(s).value, value);
    SEEN(s).order = what;
}

// In a lobby, with nothing more on its way
static bool in_lobby(seat *s)
{
    return CLIENT(s).busy == Lobbies_LobbyBusy_Idle && !is(s, CLIENT(s).lobby.id, "");
}

// What it asked for was answered, one way or the other
static bool settled(seat *s)
{
    return CLIENT(s).busy == Lobbies_LobbyBusy_Idle && (CLIENT(s).failure != Lobbies_LobbyFailure_None || !is(s, CLIENT(s).lobby.id, ""));
}

static bool out(seat *s)
{
    return is(s, CLIENT(s).lobby.id, "");
}

static bool same(seat *a, const tide_text of_a, seat *b, const tide_text of_b)
{
    char one[256], other[256];
    return strcmp(text_of(a, of_a, one, sizeof one), text_of(b, of_b, other, sizeof other)) == 0;
}

// How every player stands, for a step that wasn't reached
static void account(void)
{
    for (int i = 0; i < 4; i++) {
        seat *s = &seats[i];
        char id[32], lobby[32], match[64], message[128], names[128];
        printf("  %s: id %s, lobby %s (version %d, %d members: %s), match '%s', page %d, busy %d, failure %d, lists %d, '%s'\n",
               s->name, text_of(s, CLIENT(s).me.id, id, sizeof id), text_of(s, CLIENT(s).lobby.id, lobby, sizeof lobby),
               CLIENT(s).lobby.version, SEEN(s).members, text_of(s, SEEN(s).names, names, sizeof names),
               text_of(s, CLIENT(s).lobby.match, match, sizeof match), (int)CLIENT(s).page, (int)CLIENT(s).busy,
               (int)CLIENT(s).failure, SEEN(s).lists, text_of(s, CLIENT(s).message, message, sizeof message));
    }
}

// The scenario is one function that leaves at each wait and comes back to it
// the next frame: WAIT goes on once its condition holds, a frame after what
// was asked before it at the earliest.
static int lobby_at;
static int lobby_waited;
#define WAIT(condition)                                                                    \
    lobby_at = __LINE__;                                                                   \
    lobby_waited = 0;                                                                      \
    return 0;                                                                              \
    case __LINE__:                                                                         \
        if (!(condition)) {                                                                \
            if (++lobby_waited < LOBBY_WAIT_FRAMES) return 0;                              \
            printf("FAIL: never reached in %d frames: %s\n", LOBBY_WAIT_FRAMES, #condition); \
            account();                                                                     \
            return -1;                                                                     \
        }

// A frame of every player's, then the scenario from where it waits: 0 while
// it goes on, 1 once it's through with every check right, -1 when it failed.
static int lobby_relay_step(const float seconds)
{
    static char lobby[32], quick[32], bob[32], cat[32];
    for (int i = 0; i < 4; i++) seat_frame(&seats[i], seconds);
    switch (lobby_at) {
    case 0:
        // Who a player is: made by the service once, and kept by this machine
        order(ANN, Order_Forget, "", "");
        WAIT(SEEN(ANN).order == Order_None);
        order(ANN, Order_List, "", "");
        WAIT(SEEN(ANN).lists == 1);
        lobby_check(tide_text_read(&ANN->local.heap, CLIENT(ANN).me.id).bytes == 16, "the service makes a player, with an ID");
        lobby_check(SEEN(ANN).listed == 0 && CLIENT(ANN).failure == Lobbies_LobbyFailure_None, "a game nobody plays lists no lobbies");
        order(DAN, Order_List, "", "");
        WAIT(SEEN(DAN).lists == 1);
        lobby_check(same(DAN, CLIENT(DAN).me.id, ANN, CLIENT(ANN).me.id), "another program on this machine is the player it kept");

        // A lobby, and the lists it's in by its data
        order(ANN, Order_Create, "Friday", "duel");
        WAIT(settled(ANN) && SEEN(ANN).members == 1);
        lobby_check(in_lobby(ANN) && CLIENT(ANN).owned && CLIENT(ANN).page == Lobbies_LobbyMenu_Lobby, "a lobby is made, and its maker owns it");
        lobby_check(SEEN(ANN).members == 1 && is(ANN, SEEN(ANN).names, "Ann") && is(ANN, SEEN(ANN).mode, "duel") && CLIENT(ANN).lobby.max == 4
                        && is(ANN, CLIENT(ANN).lobby.name, "Friday") && same(ANN, CLIENT(ANN).lobby.you, ANN, CLIENT(ANN).me.id),
                    "it's as it was asked for, with its maker in it");
        text_of(ANN, CLIENT(ANN).lobby.id, lobby, sizeof lobby);
        order(BOB, Order_List, "duel", "");
        WAIT(SEEN(BOB).lists == 1);
        lobby_check(SEEN(BOB).listed == 1 && is(BOB, SEEN(BOB).first, lobby), "a list by its data has it");
        order(BOB, Order_List, "race", "");
        WAIT(SEEN(BOB).lists == 2);
        lobby_check(SEEN(BOB).listed == 0, "a list by other data doesn't");

        // Joining by its ID. Bob's program has the player this machine keeps, which is Ann: the service says
        // that player is in the lobby already, and Bob joins as one of his run's own. Ann sees him arrive in
        // the answer the relay held for her
        order(BOB, Order_Join, lobby, "");
        WAIT(settled(BOB) && SEEN(ANN).members == 2 && SEEN(BOB).members == 2);
        lobby_check(in_lobby(BOB) && !CLIENT(BOB).owned && CLIENT(BOB).page == Lobbies_LobbyMenu_Lobby, "a second program on the machine joins");
        lobby_check(!same(BOB, CLIENT(BOB).me.id, ANN, CLIENT(ANN).me.id) && same(BOB, CLIENT(BOB).lobby.you, BOB, CLIENT(BOB).me.id),
                    "as a player of its own");
        lobby_check(is(ANN, SEEN(ANN).names, "Ann,Bob") && is(BOB, SEEN(BOB).names, "Ann,Bob"), "both see both members, in the order they joined");
        order(CAT, Order_Join, lobby, "");
        WAIT(settled(CAT) && SEEN(ANN).members == 3 && SEEN(BOB).members == 3 && SEEN(CAT).members == 3);
        lobby_check(in_lobby(CAT) && is(CAT, SEEN(CAT).names, "Ann,Bob,Cat"), "a third joins, and the two there see it");
        text_of(BOB, CLIENT(BOB).me.id, bob, sizeof bob);
        text_of(CAT, CLIENT(CAT).me.id, cat, sizeof cat);
        lobby_check(strcmp(bob, cat) != 0, "each is a player of its own");

        // A member's own data: the ready flag, and a key of the game's
        order(BOB, Order_Ready, "", "");
        WAIT(SEEN(ANN).ready == 1 && SEEN(BOB).ready == 1 && SEEN(CAT).ready == 1);
        order(BOB, Order_Data, "color", "red");
        WAIT(is(ANN, SEEN(ANN).colors, "-,red,-") && is(CAT, SEEN(CAT).colors, "-,red,-"));
        lobby_check(SEEN(ANN).ready == 1, "a member's data is set a key at a time, and every member sees it");
        order(BOB, Order_Undata, "color", "");
        WAIT(is(ANN, SEEN(ANN).colors, "-,-,-") && is(BOB, SEEN(BOB).colors, "-,-,-"));
        order(BOB, Order_Unready, "", "");
        WAIT(SEEN(ANN).ready == 0 && SEEN(CAT).ready == 0);
        lobby_check(true, "and taken away");

        // The owner's changes, which only the owner makes
        order(ANN, Order_Rename, "Saturday", "");
        WAIT(is(BOB, CLIENT(BOB).lobby.name, "Saturday") && is(CAT, CLIENT(CAT).lobby.name, "Saturday"));
        order(ANN, Order_Mode, "race", "");
        WAIT(is(BOB, SEEN(BOB).mode, "race") && is(ANN, SEEN(ANN).mode, "race"));
        lobby_check(is(BOB, CLIENT(BOB).lobby.name, "Saturday"), "the owner's changes reach every member");
        order(BOB, Order_Rename, "Mine", "");
        WAIT(CLIENT(BOB).failure != Lobbies_LobbyFailure_None);
        lobby_check(CLIENT(BOB).failure == Lobbies_LobbyFailure_NotOwner && is(BOB, CLIENT(BOB).lobby.name, "Saturday") && in_lobby(BOB),
                    "a member's aren't made, and it's told why");
        order(ANN, Order_Close, "", "");
        WAIT(!CLIENT(BOB).lobby.open && !CLIENT(ANN).lobby.open);
        order(DAN, Order_Join, lobby, "");
        WAIT(settled(DAN));
        lobby_check(out(DAN) && CLIENT(DAN).failure == Lobbies_LobbyFailure_Closed && is(DAN, CLIENT(DAN).message, "That lobby is closed."),
                    "a closed lobby lets nobody in, and says so");
        order(ANN, Order_Open, "", "");
        WAIT(CLIENT(BOB).lobby.open && CLIENT(CAT).lobby.open);

        // The lobby's match: its owner's machine opens its match, the lobby says the room's code, and the
        // members' machines join that room
        ANN->asked = TIDE_REQUEST_NONE;
        order(ANN, Order_Start, "", "");
        WAIT(ANN->asked == TIDE_REQUEST_OPEN);
        tide_local_set_session(&ANN->local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, true, ROOM);
        WAIT(is(BOB, CLIENT(BOB).lobby.match, ROOM) && is(CAT, CLIENT(CAT).lobby.match, ROOM) && is(ANN, CLIENT(ANN).match, ROOM)
             && BOB->asked == TIDE_REQUEST_JOIN && CAT->asked == TIDE_REQUEST_JOIN);
        lobby_check(strcmp(BOB->said, ROOM) == 0 && strcmp(CAT->said, ROOM) == 0 && CLIENT(BOB).joining,
                    "the owner's room is the lobby's match, which the members join");
        lobby_check(CLIENT(ANN).page == Lobbies_LobbyMenu_Playing && CLIENT(ANN).hosting, "the owner is in it, with no page in the way");
        tide_local_connected(&BOB->local);
        tide_local_set_session(&BOB->local, TIDE_SESSION_CONNECTED, tide_player_from_index(1), 0, false, false, ROOM);
        WAIT(CLIENT(BOB).page == Lobbies_LobbyMenu_Playing);
        // The owner ends it: the lobby names no match, and the members are back in the lobby
        order(ANN, Order_End, "", "");
        WAIT(ANN->asked == TIDE_REQUEST_START);
        tide_local_set_session(&ANN->local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
        WAIT(is(BOB, CLIENT(BOB).lobby.match, "") && is(CAT, CLIENT(CAT).lobby.match, "") && BOB->asked == TIDE_REQUEST_START
             && CAT->asked == TIDE_REQUEST_START && is(ANN, CLIENT(ANN).lobby.match, ""));
        lobby_check(CLIENT(BOB).page == Lobbies_LobbyMenu_Lobby && is(BOB, CLIENT(BOB).match, "") && in_lobby(CAT),
                    "a match its owner ends is left by every member, who stay in the lobby");
        tide_local_set_session(&BOB->local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");

        // A member sent out, while the relay holds its answer: told so, and kept out
        order(ANN, Order_Kick, cat, "");
        WAIT(out(CAT) && SEEN(ANN).members == 2 && SEEN(BOB).members == 2);
        lobby_check(CLIENT(CAT).page == Lobbies_LobbyMenu_Title && CLIENT(CAT).failure == Lobbies_LobbyFailure_Kicked
                        && is(CAT, CLIENT(CAT).message, "The owner sent you out of the lobby."),
                    "a kicked member is on its title, with a line that says why");
        order(CAT, Order_Join, lobby, "");
        WAIT(settled(CAT));
        lobby_check(out(CAT) && CLIENT(CAT).failure == Lobbies_LobbyFailure_Kicked, "and can't come back");

        // The lobby handed over, and its owner leaving: whoever has been there longest owns it
        order(ANN, Order_Transfer, bob, "");
        WAIT(CLIENT(BOB).owned && !CLIENT(ANN).owned);
        lobby_check(same(ANN, CLIENT(ANN).lobby.owner, BOB, CLIENT(BOB).me.id), "the lobby is handed to another member");
        order(BOB, Order_Leave, "", "");
        WAIT(CLIENT(ANN).owned && SEEN(ANN).members == 1);
        lobby_check(out(BOB) && CLIENT(BOB).page == Lobbies_LobbyMenu_Title && is(BOB, CLIENT(BOB).message, ""),
                    "an owner who leaves is on its title, and the lobby is the longest-standing member's");
        order(ANN, Order_Leave, "", "");
        WAIT(out(ANN));

        // A quick match finds a lobby with room, or makes one
        order(CAT, Order_Quick, "arena", "");
        WAIT(settled(CAT) && SEEN(CAT).members == 1);
        lobby_check(in_lobby(CAT) && CLIENT(CAT).owned && SEEN(CAT).members == 1 && is(CAT, SEEN(CAT).mode, "arena")
                        && is(CAT, CLIENT(CAT).lobby.name, "Quick") && CLIENT(CAT).lobby.max == 2,
                    "a quick match with nobody looking makes a lobby");
        text_of(CAT, CLIENT(CAT).lobby.id, quick, sizeof quick);
        order(BOB, Order_Quick, "arena", "");
        WAIT(settled(BOB) && SEEN(CAT).members == 2);
        lobby_check(in_lobby(BOB) && is(BOB, CLIENT(BOB).lobby.id, quick) && !CLIENT(BOB).owned, "the next to look joins it");
        order(ANN, Order_Quick, "tower", "");
        WAIT(settled(ANN));
        lobby_check(in_lobby(ANN) && !is(ANN, CLIENT(ANN).lobby.id, quick) && CLIENT(ANN).owned, "another mode is another lobby");
        order(DAN, Order_Quick, "arena", "");
        WAIT(settled(DAN));
        lobby_check(in_lobby(DAN) && !is(DAN, CLIENT(DAN).lobby.id, quick) && CLIENT(DAN).owned, "and so is a full one's mode");

        // Everyone leaves, and the lobbies go with their last member
        order(ANN, Order_Leave, "", "");
        order(BOB, Order_Leave, "", "");
        order(CAT, Order_Leave, "", "");
        order(DAN, Order_Leave, "", "");
        WAIT(out(ANN) && out(BOB) && out(CAT) && out(DAN));
        // A leave that's still on its way would list its lobby: asked until none is
        order(ANN, Order_List, "", "");
        WAIT(SEEN(ANN).order == Order_None && !CLIENT(ANN).listing);
        if (SEEN(ANN).listed != 0) {
            if (++lobby_waited >= LOBBY_WAIT_FRAMES) {
                printf("FAIL: the lobbies everyone left are still listed\n");
                account();
                return -1;
            }
            order(ANN, Order_List, "", "");
            return 0;
        }
        lobby_check(true, "lobbies everyone left are gone");
        lobby_at = -1;
        return lobby_failures ? -1 : 1;
    }
    return lobby_failures ? -1 : 1;
}
