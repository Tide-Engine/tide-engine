// The lobby package with no window and no service (game.tide fills the
// identity, lobby and matchmaking slots itself): where its pages leave a
// machine by how it started, what each page's buttons ask, driven by the
// keyboard as a player would, what the transport asks of the session as a
// lobby's match starts and ends, and what the game reads of it.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/session.h"
#include "tide_test.h"

#define DT (1.0f / 60.0f)

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

#define CLIENT local.Lobbies_LobbyClient

// A frame as a host runs one, with no window: the views, the local handlers
// and the tasks, over the devices as they are.
static void frame(const tide_world *w)
{
    tide_local_frame_time(&local, DT);
    tide_draw_reset(&draw);
    tide_touches_poll(&devices.touchscreen);
    tide_pointer_poll(&devices, true);
    tide_gui_begin(&gui, &devices, tide_f2(376, 600), NULL);
    tide_frame(w, NULL, 1.0f, w, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static void frames(const int count)
{
    for (int i = 0; i < count; i++) frame(&world);
}

// A machine as it starts: its local state, and where the host says it stands.
static void machine(const uint32_t state, const int who, const bool server, const bool open, const char *room)
{
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    tide_local_set_session(&local, state, who < 0 ? (tide_player_id){0} : tide_player_from_index(who), 0, server, open, room);
}

static void done(void)
{
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}

// Whether this machine's player's hands are on the game: what its input says.
static bool plays(void)
{
    return tide_input_sample(&devices, &local).playing;
}

static bool is(const tide_text text, const char *expected)
{
    const tide_str read = tide_text_read(&local.heap, text);
    return (size_t)read.bytes == strlen(expected) && memcmp(read.ptr, expected, (size_t)read.bytes) == 0;
}

// What local code asked of the session since it was last asked
static tide_session_request last_request;
static tide_start last_start;

static uint32_t asked(void)
{
    last_request = (tide_session_request){0};
    return tide_local_take_request(&local, &last_request, &last_start) ? last_request.kind : (uint32_t)TIDE_REQUEST_NONE;
}

// A key held for one frame, as a player taps it
static void tap(tide_button *key)
{
    tide_button_set(key, true);
    frame(&world);
    tide_button_set(key, false);
    frame(&world);
}

// Tab to the page's `nth` button from the one that has the focus, and Enter
static void press(const int nth)
{
    for (int i = 0; i < nth; i++) tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
}

// A finger's tap at (x, y), from the screen's top left
static void touch(const tide_touch_phase phase, const float x, const float y)
{
    tide_local_frame_time(&local, DT);
    tide_draw_reset(&draw);
    tide_touches_poll(&devices.touchscreen);
    tide_touch_event(&devices.touchscreen, phase, 1, tide_f2(x, 600.0f - y));
    tide_pointer_poll(&devices, false);
    tide_gui_begin(&gui, &devices, tide_f2(376, 600), NULL);
    tide_frame(&world, NULL, 1.0f, &world, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// What the test has this player ask, where no button is pressed for it
static void order(const Order what, const char *text)
{
    tide_text_use(NULL, &local.heap);
    tide_text_set(&local.Remote.text, tide_str_from_cstr(text), TIDE_IN_LOCAL);
    local.Remote.order = what;
    frame(&world);
}

TIDE_TEST(lobby_opens_on_its_title_unless_it_was_told_which_match)
{
    tide_world_init(&world, DT);
    // Started with nothing said: a match of its own, closed, behind the title.
    // The game's input sends nothing meanwhile, and the service isn't asked
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frames(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && !CLIENT.playing && !plays());
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE && is(CLIENT.me.id, "") && !CLIENT.signing);
    tide_local_free(&local);

    // --host, or a page's ?host: the match is open by the first frame, and no page is in the way
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frames(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing && CLIENT.playing && plays());
    tide_local_free(&local);

    // --join <code>, or ?join=<code>: joining by the first frame, with no page, and the hands on the game once it's in
    machine(TIDE_SESSION_CONNECTING, -1, false, false, "K7QF2M");
    frame(NULL);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing && !plays());
    tide_local_connected(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(1), 30, false, false, "K7QF2M");
    frames(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing && plays());
    // That match ends: on the title, with a line that says so, and a match of its own asked for
    tide_local_disconnected(&local, TIDE_DISCONNECT_ENDED, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && is(CLIENT.message, "The match ended.") && !plays());
    TIDE_CHECK(asked() == TIDE_REQUEST_START && last_start.scene == 0);
    // This machine leaving a match, as when it starts or joins another, says nothing
    tide_local_disconnected(&local, TIDE_DISCONNECT_LEFT, "");
    frame(NULL);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);
    done();
}


TIDE_TEST(lobby_pages_are_driven_by_the_keyboard)
{
    tide_world_init(&world, DT);
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    // The title comes up, and its first button, Play alone, takes the focus a frame later
    frames(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && gui.modal != 0);
    // Enter on Play alone: into the match, which the game starts over
    tap(&devices.keyboard.enter);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing && local.Seen.newGames == 1);
    frame(&world);
    TIDE_CHECK(CLIENT.playing && plays() && asked() == TIDE_REQUEST_NONE);
    // Escape in the match, with no lobby: the title, whose first button resumes
    tap(&devices.keyboard.escape);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && CLIENT.resume && !plays());
    tap(&devices.keyboard.enter);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing && local.Seen.newGames == 1);
    // ...and so does Escape on the title
    tap(&devices.keyboard.escape);
    tap(&devices.keyboard.escape);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing);

    // Create a lobby, the title's second button: the service is asked who this is, then for the lobby the
    // game's NewLobby says, and its page comes up. A page that a key brought up has no focus until Tab gives
    // its first button one
    tap(&devices.keyboard.escape);
    press(2);
    TIDE_CHECK(CLIENT.busy == Lobbies_LobbyBusy_Creating && CLIENT.page == Lobbies_LobbyMenu_Title);
    frames(6);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && CLIENT.busy == Lobbies_LobbyBusy_Idle && !plays());
    TIDE_CHECK(is(CLIENT.lobby.id, "MADE0000") && is(CLIENT.lobby.name, "Tester's table") && CLIENT.lobby.max == 4);
    TIDE_CHECK(CLIENT.owned && is(CLIENT.me.id, "me") && is(CLIENT.name, "Tester") && local.Seen.members == 1);
    TIDE_CHECK(is(CLIENT.message, "") && CLIENT.failure == Lobbies_LobbyFailure_None);
    // Its first button copies the ID
    tap(&devices.keyboard.enter);
    TIDE_CHECK(asked() == TIDE_REQUEST_COPY && strcmp(last_request.text, "MADE0000") == 0 && CLIENT.copied < 1.0f);
    // Ready, the next: this member's data says so, to every member
    TIDE_CHECK(!local.Seen.mine && !local.Seen.allReady);
    press(1);
    frames(4);
    TIDE_CHECK(local.Seen.mine && local.Seen.ready == 1 && local.Seen.allReady && CLIENT.lobby.version == 2);

    // Start the match: this machine's match opens, and once it has a room the lobby is told its code. The
    // menu is out of the way when the lobby says it
    press(1);
    TIDE_CHECK(asked() == TIDE_REQUEST_OPEN && CLIENT.hosting && CLIENT.page == Lobbies_LobbyMenu_Lobby);
    frames(4);
    TIDE_CHECK(is(CLIENT.lobby.match, "") && asked() == TIDE_REQUEST_NONE);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, true, "K7QF2M");
    frames(6);
    TIDE_CHECK(is(CLIENT.lobby.match, "K7QF2M") && is(CLIENT.match, "K7QF2M") && CLIENT.page == Lobbies_LobbyMenu_Playing);
    TIDE_CHECK(plays() && asked() == TIDE_REQUEST_NONE);
    // Escape in the lobby's match: its page, which Escape closes again
    tap(&devices.keyboard.escape);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && CLIENT.resume && !plays());
    tap(&devices.keyboard.escape);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing);
    // End the match, after Copy, Resume and Ready: a match of this machine's own again, and the lobby names none
    tap(&devices.keyboard.escape);
    press(4);
    TIDE_CHECK(asked() == TIDE_REQUEST_START && !CLIENT.hosting && CLIENT.page == Lobbies_LobbyMenu_Lobby);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    frames(6);
    TIDE_CHECK(is(CLIENT.lobby.match, "") && is(CLIENT.match, "") && is(CLIENT.left, "") && asked() == TIDE_REQUEST_NONE);

    // The button that ended it went with the focus. Tab to Copy, then past Ready and Start to the door: closed
    press(4);
    frames(4);
    TIDE_CHECK(!CLIENT.lobby.open && CLIENT.page == Lobbies_LobbyMenu_Lobby);
    // Leave, the last: the title, with no lobby, in the match this machine never left
    press(1);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && is(CLIENT.lobby.id, "") && !CLIENT.owned);
    frames(4);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE && is(CLIENT.message, ""));
    tide_local_free(&local);

    // Joining by the keyboard: the title's fifth button, the ID typed into the field that has the focus, Enter
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frames(2);
    press(4);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Join);
    frame(&world);
    const char *id = "full0000";
    for (const char *c = id; *c; c++) devices.keyboard.text.chars[devices.keyboard.text.count++] = (uint32_t)*c;
    frame(&world);
    devices.keyboard.text.count = 0;
    tap(&devices.keyboard.enter);
    TIDE_CHECK(CLIENT.busy == Lobbies_LobbyBusy_Joining && is(CLIENT.typed, "FULL0000"));
    // A full lobby: the page it was typed on stays, with why
    frames(6);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Join && CLIENT.busy == Lobbies_LobbyBusy_Idle);
    TIDE_CHECK(CLIENT.failure == Lobbies_LobbyFailure_Full && is(CLIENT.message, "That lobby is full."));
    // Escape: the title, with nothing said
    tap(&devices.keyboard.escape);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && is(CLIENT.message, ""));
    done();
}

TIDE_TEST(lobby_members_follow_its_match)
{
    tide_world_init(&world, DT);
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frames(2);
    // A lobby whose match is on: joined, and its match with it, by the room's code
    order(Order_Join, "MATCH000");
    frames(6);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && is(CLIENT.lobby.id, "MATCH000") && !CLIENT.owned && local.Seen.members == 2);
    TIDE_CHECK(asked() == TIDE_REQUEST_JOIN && strcmp(last_request.text, "K7QF2M") == 0 && CLIENT.joining);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTING, (tide_player_id){0}, 0, false, false, "K7QF2M");
    frame(NULL);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && asked() == TIDE_REQUEST_NONE);
    // In it: no page
    tide_local_connected(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(1), 30, false, false, "K7QF2M");
    frames(3);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing && !CLIENT.joining && plays());
    // The match ends under it: the lobby's page, with why, in a match of its own. It isn't joined again by itself
    tide_local_disconnected(&local, TIDE_DISCONNECT_SERVER_LEFT, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && is(CLIENT.message, "The match ended.") && !plays());
    TIDE_CHECK(asked() == TIDE_REQUEST_START && is(CLIENT.left, "K7QF2M") && is(CLIENT.match, ""));
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    frames(6);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);
    // ...until the player says so
    order(Order_Rejoin, "");
    frames(2);
    TIDE_CHECK(asked() == TIDE_REQUEST_JOIN && strcmp(last_request.text, "K7QF2M") == 0 && is(CLIENT.message, ""));
    // A room nobody has any more: the lobby's page again
    tide_local_disconnected(&local, TIDE_DISCONNECT_FAILED, "");
    frames(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && is(CLIENT.message, "Couldn't find the match.") && asked() == TIDE_REQUEST_START);
    // Leaving the lobby says nothing more of its match
    order(Order_Leave, "");
    frames(3);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && is(CLIENT.lobby.id, "") && is(CLIENT.left, "") && asked() == TIDE_REQUEST_NONE);

    // In a lobby's match, and out of the lobby: out of its match too
    order(Order_Join, "MATCH000");
    frames(6);
    TIDE_CHECK(asked() == TIDE_REQUEST_JOIN);
    tide_local_connected(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(1), 30, false, false, "K7QF2M");
    frames(2);
    order(Order_Leave, "");
    TIDE_CHECK(asked() == TIDE_REQUEST_START && CLIENT.page == Lobbies_LobbyMenu_Title);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    frames(3);

    // Its owner ends the match, which the lobby then names none of: back on the lobby's page
    order(Order_Join, "MATCH000");
    frames(6);
    TIDE_CHECK(asked() == TIDE_REQUEST_JOIN);
    tide_local_connected(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(1), 30, false, false, "K7QF2M");
    frames(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing);
    tide_text_use(NULL, &local.heap);
    tide_text_set(&CLIENT.lobby.match, tide_str_from_cstr(""), TIDE_IN_LOCAL);
    CLIENT.lobby.version += 1;
    frames(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && asked() == TIDE_REQUEST_START && is(CLIENT.match, ""));
    done();
}

TIDE_TEST(lobby_leaves_a_match_it_joined_before_it_hosts)
{
    tide_world_init(&world, DT);
    // A machine that was told which match to join is a player of someone else's. Its title, from the match,
    // has Leave the match after the ways in: a match of its own, behind the title
    machine(TIDE_SESSION_CONNECTED, 1, false, false, "K7QF2M");
    frames(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Playing && plays());
    tap(&devices.keyboard.escape);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && CLIENT.resume);
    press(6);
    TIDE_CHECK(asked() == TIDE_REQUEST_START && CLIENT.page == Lobbies_LobbyMenu_Title && !CLIENT.resume);
    tide_local_free(&local);

    // In such a match, it makes a lobby and starts its match: that match isn't its to open, so it leaves
    // for one of its own first, once, and opens that one when it's there
    machine(TIDE_SESSION_CONNECTED, 1, false, false, "K7QF2M");
    frames(2);
    order(Order_Create, "Mine");
    frames(6);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && CLIENT.owned && asked() == TIDE_REQUEST_NONE);
    // Start the match, after Copy and Ready
    press(2);
    TIDE_CHECK(CLIENT.hosting && asked() == TIDE_REQUEST_START);
    frames(3);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTING, (tide_player_id){0}, 0, false, false, "");
    frames(2);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    frames(3);
    TIDE_CHECK(asked() == TIDE_REQUEST_OPEN && asked() == TIDE_REQUEST_NONE && is(CLIENT.lobby.match, ""));
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, true, "Q2Q2Q2");
    frames(6);
    TIDE_CHECK(is(CLIENT.lobby.match, "Q2Q2Q2") && is(CLIENT.match, "Q2Q2Q2") && CLIENT.page == Lobbies_LobbyMenu_Playing);

    // A machine whose match was open already (--host) asks the session nothing more: its room is the lobby's match
    tide_local_free(&local);
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frames(2);
    order(Order_Create, "Open");
    frames(6);
    press(2);
    frames(6);
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE && is(CLIENT.lobby.match, "K7QF2M") && CLIENT.page == Lobbies_LobbyMenu_Playing);
    done();
}

TIDE_TEST(lobby_says_what_became_of_a_lobby)
{
    tide_world_init(&world, DT);
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frames(2);
    // A lobby this player is in already, as the service has it: another program on this machine plays as it,
    // so this one joins as a player of its run's own
    order(Order_Join, "SAME0000");
    frames(10);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && is(CLIENT.me.id, "guest") && is(CLIENT.lobby.you, "guest"));
    // The owner's part: a name, a member sent out, and the lobby handed on
    order(Order_Rename, "Saturday");
    frames(4);
    TIDE_CHECK(is(CLIENT.lobby.name, "Saturday"));
    order(Order_Transfer, "guest");
    frames(4);
    TIDE_CHECK(CLIENT.owned && local.Seen.members == 2);
    // The owner's page has Kick beside each other member: after Copy
    press(1);
    frames(4);
    TIDE_CHECK(local.Seen.members == 1 && CLIENT.page == Lobbies_LobbyMenu_Lobby);
    order(Order_Leave, "");
    frames(3);

    // Sent out by its owner: the title, with a line that says so
    order(Order_Join, "KICK0000");
    frames(4);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby);
    frames(6);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && is(CLIENT.lobby.id, "") && CLIENT.failure == Lobbies_LobbyFailure_Kicked);
    TIDE_CHECK(is(CLIENT.message, "The owner sent you out of the lobby."));

    // A service that stops answering is asked a few times, a second apart, and then the lobby is given up
    order(Order_Join, "LOST0000");
    frames(4);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && is(CLIENT.message, ""));
    frames(200);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && is(CLIENT.lobby.id, "LOST0000"));
    frames(120);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && CLIENT.failure == Lobbies_LobbyFailure_Unreachable);
    TIDE_CHECK(is(CLIENT.message, "Lost the connection to the lobby service."));

    // A lobby asked for and no longer waited for is left when its answer comes
    order(Order_Join, "LATE0000");
    order(Order_Cancel, "");
    TIDE_CHECK(CLIENT.busy == Lobbies_LobbyBusy_Idle);
    frames(10);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && is(CLIENT.lobby.id, "") && CLIENT.busy == Lobbies_LobbyBusy_Idle);
    // ...and the next one asked for is the one this player is in
    order(Order_Create, "Next");
    frames(6);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && is(CLIENT.lobby.id, "MADE0000") && is(CLIENT.lobby.name, "Next"));
    done();
}

TIDE_TEST(lobby_finds_a_match_and_lists_lobbies)
{
    tide_world_init(&world, DT);
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frames(2);
    // Quick match, the title's fourth button: the game's own page is up while the ticket is searching, with
    // no modal of the package's
    press(3);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Searching && CLIENT.busy == Lobbies_LobbyBusy_Searching);
    frames(4);
    TIDE_CHECK(gui.modal == 0 && CLIENT.ticket.status == Lobbies_TicketStatus_Searching && !plays());
    // The ticket ends in a lobby, which this player is in
    frames(8);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && is(CLIENT.lobby.id, "QUICK000") && CLIENT.busy == Lobbies_LobbyBusy_Idle);
    TIDE_CHECK(CLIENT.ticket.status == Lobbies_TicketStatus_None);
    order(Order_Leave, "");
    frames(3);

    // The game's page has one button, Stop: nobody waits for the ticket, and the lobby it finds is left
    order(Order_Quick, "duel");
    frames(3);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Searching);
    touch(TIDE_TOUCH_BEGAN, 60.0f, 30.0f);
    touch(TIDE_TOUCH_ENDED, 60.0f, 30.0f);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && CLIENT.busy == Lobbies_LobbyBusy_Idle);
    frames(12);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && is(CLIENT.lobby.id, "") && is(CLIENT.message, ""));
    // A search that finds nobody says so on the title
    order(Order_Quick, "nobody");
    frames(5);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title && CLIENT.failure == Lobbies_LobbyFailure_NotFound);
    TIDE_CHECK(is(CLIENT.message, "No lobby has that ID."));
    tide_local_free(&local);

    // Browse lobbies, the title's third button: the list is asked for as the page comes up
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frames(2);
    press(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Browse && CLIENT.listing && local.Seen.listed == 0);
    frames(6);
    TIDE_CHECK(!CLIENT.listing && local.Seen.listed == 8 && CLIENT.listFrom == 0);
    // Six of the eight show. More, after their Join buttons, shows the rest, and then the first again
    press(7);
    TIDE_CHECK(CLIENT.listFrom == 6);
    tap(&devices.keyboard.enter);
    TIDE_CHECK(CLIENT.listFrom == 0);
    // Back, after Refresh: the title
    press(2);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Title);
    // The page again, with the list it had while the next is on its way: the first lobby's Join has the focus
    press(2);
    frames(6);
    tap(&devices.keyboard.enter);
    TIDE_CHECK(CLIENT.busy == Lobbies_LobbyBusy_Joining && CLIENT.page == Lobbies_LobbyMenu_Browse);
    frames(6);
    TIDE_CHECK(CLIENT.page == Lobbies_LobbyMenu_Lobby && is(CLIENT.lobby.id, "LIST0001"));
    order(Order_Leave, "");
    frames(3);
    // A list that finds none
    order(Order_Empty, "");
    frames(6);
    TIDE_CHECK(local.Seen.listed == 0 && !CLIENT.listing);
    done();
}
