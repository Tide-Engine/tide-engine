// The rooms package with no window: where its pages leave a machine by how
// it started, what each page's buttons ask of the session, driven by the
// keyboard as a player would, and what the game reads of it.

#include <stdbool.h>
#include <string.h>

#include "game.h"
#include "tide/audio.h"
#include "tide/session.h"
#include "tide_test.h"

#define DT (1.0f / 60.0f)

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static tide_audio_frame audio;

static tide_player_id player(const int index)
{
    return tide_player_from_index(index);
}

// A frame as a host runs one, with no window: the views and the local
// handlers, over the devices as they are.
static void frame(const tide_world *w)
{
    tide_local_frame_time(&local, DT);
    tide_draw_reset(&draw);
    tide_audio_reset(&audio);
    tide_local_audio(&local, &audio);
    tide_touches_poll(&devices.touchscreen);
    tide_pointer_poll(&devices, true);
    tide_gui_begin(&gui, &devices, tide_f2(376, 400), NULL);
    tide_frame(w, NULL, 1.0f, w, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// A machine as it starts: its local state, and where the host says it stands.
static void machine(const uint32_t state, const int who, const bool server, const bool open, const char *room)
{
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    tide_local_set_session(&local, state, who < 0 ? (tide_player_id){0} : player(who), 0, server, open, room);
}

// Whether this machine's player's hands are on the game: what its input says.
static bool plays(void)
{
    return tide_input_sample(&devices, &local).playing;
}

static bool says_something(void)
{
    return tide_text_read(&local.heap, local.Rooms_Lobby.message).bytes > 0;
}

// What local code asked of the session since it was last asked
static tide_session_request last_request;
static tide_start last_start;

static uint32_t asked(void)
{
    last_request = (tide_session_request){0};
    return tide_local_take_request(&local, &last_request, &last_start) ? last_request.kind : (uint32_t)TIDE_REQUEST_NONE;
}

// A frame in which a finger does `phase` at (x, y), from the screen's top
// left: the mouse stays where it was
static void finger(const tide_touch_phase phase, const float x, const float y)
{
    tide_local_frame_time(&local, DT);
    tide_draw_reset(&draw);
    tide_audio_reset(&audio);
    tide_local_audio(&local, &audio);
    tide_touches_poll(&devices.touchscreen);
    tide_touch_event(&devices.touchscreen, phase, 1, tide_f2(x, 400.0f - y));
    tide_pointer_poll(&devices, false);
    tide_gui_begin(&gui, &devices, tide_f2(376, 400), NULL);
    tide_frame(&world, NULL, 1.0f, &world, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// A key held for one frame, as a player taps it
static void tap(tide_button *key)
{
    tide_button_set(key, true);
    frame(&world);
    tide_button_set(key, false);
    frame(&world);
}

TIDE_TEST(rooms_opens_on_its_title_unless_it_was_told_which_match)
{
    tide_world_init(&world, DT);
    // Started with nothing said: a match of its own, closed, behind the title.
    // The game's input sends nothing meanwhile
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title && !local.Rooms_Lobby.playing && !plays());
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);
    tide_local_free(&local);

    // --host, or a page's ?host: the match is open by the first frame, and no page is in the way
    machine(TIDE_SESSION_CONNECTED, 0, true, true, "K7QF2M");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Playing && local.Rooms_Lobby.playing && plays());
    // A page up over the match: the hands are off until it closes
    local.Rooms_Lobby.page = Rooms_Page_Room;
    frame(&world);
    TIDE_CHECK(!plays());
    local.Rooms_Lobby.page = Rooms_Page_Playing;
    frame(&world);
    TIDE_CHECK(plays());
    tide_local_free(&local);

    // --join <code>, or ?join=<code>: joining by the first frame. The page says so until the match's world is here
    machine(TIDE_SESSION_CONNECTING, -1, false, false, "K7QF2M");
    frame(NULL);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Join && local.Rooms_Lobby.joining && !plays());
    tide_local_connected(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, player(1), 30, false, false, "K7QF2M");
    frame(&world);
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Playing && !local.Rooms_Lobby.joining && plays());
    // That match ends: back on the title, with a line that says so, and a match of its own asked for, in Main
    tide_local_disconnected(&local, TIDE_DISCONNECT_ENDED, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title && says_something() && !plays());
    TIDE_CHECK(asked() == TIDE_REQUEST_START && last_start.scene == 0);
    tide_local_free(&local);

    // A code nobody has: the page it was typed on stays, to try again from
    machine(TIDE_SESSION_CONNECTING, -1, false, false, "");
    frame(NULL);
    tide_local_disconnected(&local, TIDE_DISCONNECT_FAILED, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Join && !local.Rooms_Lobby.joining && says_something());
    TIDE_CHECK(asked() == TIDE_REQUEST_START);
    // This machine leaving a match, as when it starts or joins another, says nothing
    tide_local_disconnected(&local, TIDE_DISCONNECT_LEFT, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Join && asked() == TIDE_REQUEST_NONE);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_world_free(&world);
}

// A laptop with a touchscreen has a mouse and fingers both: a tap presses the
// page's button wherever the mouse rests, the page's own panel included.
TIDE_TEST(rooms_a_finger_presses_play_with_the_mouse_on_the_title)
{
    tide_world_init(&world, DT);
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    // The title page is 217 tall, around the screen's middle: the game's name
    // is 118 down, where the mouse is, and Play 184
    devices.mouse.position = tide_f2(188.0f, 400.0f - 118.0f);
    for (int i = 0; i < 3; i++) frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title && gui.hot == gui.modal);
    finger(TIDE_TOUCH_BEGAN, 188.0f, 184.0f);
    TIDE_CHECK(gui.active != 0);
    finger(TIDE_TOUCH_ENDED, 188.0f, 184.0f);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Playing && local.Seen.newGames == 1);
    // The finger that pressed Play is let go, so the hands are on the game
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.playing && plays());
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_world_free(&world);
}

TIDE_TEST(rooms_pages_are_driven_by_the_keyboard)
{
    tide_world_init(&world, DT);
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    // The title comes up, and its first button, Play, takes the focus a frame later
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title);
    // Enter on Play: into the match, which the game starts over, as it would with no one else in it
    tap(&devices.keyboard.enter);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Playing && local.Seen.newGames == 1);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.playing && plays() && asked() == TIDE_REQUEST_NONE);

    // Escape in the match: the room's page, to resume from. Its first button is Invite friends
    tap(&devices.keyboard.escape);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Room && local.Rooms_Lobby.resume && !local.Rooms_Lobby.playing);
    tap(&devices.keyboard.enter);
    TIDE_CHECK(asked() == TIDE_REQUEST_OPEN && local.Rooms_Lobby.page == Rooms_Page_Room);
    // The room opened: its code shows, and Invite friends is gone with the focus. Tab to Copy, then past
    // Resume and New game to Bots, and press it
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, player(0), 0, true, true, "K7QF2M");
    frame(&world);
    for (int i = 0; i < 4; i++) tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
    TIDE_CHECK(local.Seen.toggles == 1 && !local.Seen.bots && local.Rooms_Lobby.page == Rooms_Page_Room);
    // Back to New game, which is the way back into the match too
    tide_button_set(&devices.keyboard.leftShift, true);
    tap(&devices.keyboard.tab);
    tide_button_set(&devices.keyboard.leftShift, false);
    tap(&devices.keyboard.enter);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Playing && local.Seen.newGames == 2);
    // Escape again, and Escape once more closes the page as Resume does
    tap(&devices.keyboard.escape);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Room);
    tap(&devices.keyboard.escape);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Playing);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.playing);
    // Leave the room: the last button. The page just came up, so the first Tab only lands the focus on its first
    // button, Copy, and five more go past Resume, New game, Bots and Settings. The title, and a match of this machine's own
    tap(&devices.keyboard.escape);
    for (int i = 0; i < 6; i++) tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title && asked() == TIDE_REQUEST_START);
    tide_local_free(&local);

    // Joining by the keyboard: the title's third button, the code typed into the field that has the focus, Enter
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frame(&world);
    frame(&world);
    tap(&devices.keyboard.tab);
    tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Join);
    frame(&world);
    const char *code = "k7qf2m";
    for (const char *c = code; *c; c++) devices.keyboard.text.chars[devices.keyboard.text.count++] = (uint32_t)*c;
    frame(&world);
    devices.keyboard.text.count = 0;
    tap(&devices.keyboard.enter);
    TIDE_CHECK(local.Rooms_Lobby.joining && asked() == TIDE_REQUEST_JOIN && strcmp(last_request.text, "K7QF2M") == 0);
    // While it waits, Escape cancels: the title, and a match of this machine's own again
    tide_local_set_session(&local, TIDE_SESSION_CONNECTING, (tide_player_id){0}, 0, false, false, "K7QF2M");
    frame(NULL);
    tap(&devices.keyboard.escape);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title && !local.Rooms_Lobby.joining && asked() == TIDE_REQUEST_START);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_world_free(&world);
}

TIDE_TEST(rooms_settings_control_the_master_volume_across_pages_and_matches)
{
    tide_world_init(&world, DT);
    machine(TIDE_SESSION_CONNECTED, 0, true, false, "");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.volume == 1.0f && audio.bus_volume[0] == 1.0f);

    // Settings is the title's fourth button. Its slider takes the focus.
    for (int i = 0; i < 3; i++) tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Settings && !plays());
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);

    // A step changes the audio handed to the host in that very frame.
    tide_button_set(&devices.keyboard.leftArrow, true);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.volume < 1.0f && local.Rooms_Lobby.volume > 0.0f);
    TIDE_CHECK(audio.bus_volume[0] == local.Rooms_Lobby.volume);
    TIDE_CHECK(audio.bus_volume[1] == 1.0f && audio.bus_volume[2] == 1.0f);
    tide_button_set(&devices.keyboard.leftArrow, false);
    frame(&world);

    // The keyboard can reach mute and full volume, and stays within them.
    for (int i = 0; i < 21; i++) tap(&devices.keyboard.leftArrow);
    TIDE_CHECK(local.Rooms_Lobby.volume == 0.0f && audio.bus_volume[0] == 0.0f);
    for (int i = 0; i < 21; i++) tap(&devices.keyboard.rightArrow);
    TIDE_CHECK(local.Rooms_Lobby.volume == 1.0f && audio.bus_volume[0] == 1.0f);
    for (int i = 0; i < 10; i++) tap(&devices.keyboard.leftArrow);
    const float volume = local.Rooms_Lobby.volume;
    TIDE_CHECK(volume > 0.49f && volume < 0.51f && audio.bus_volume[0] == volume);

    // Back returns to the title, keeping its first button usable. Escape
    // from settings also returns there, without starting or opening a match.
    tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title && !plays());
    TIDE_CHECK(audio.bus_volume[0] == volume && asked() == TIDE_REQUEST_NONE);
    for (int i = 0; i < 3; i++) tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Settings && local.Rooms_Lobby.volume == volume);
    tap(&devices.keyboard.escape);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title && asked() == TIDE_REQUEST_NONE);

    // Play keeps the chosen gain after the audio frame resets, and reopening
    // the menu offers settings after Invite, Resume, New game and Bots.
    tap(&devices.keyboard.enter);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Playing && plays() && audio.bus_volume[0] == volume);
    tap(&devices.keyboard.escape);
    frame(&world);
    for (int i = 0; i < 4; i++) tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Settings && !plays() && audio.bus_volume[0] == volume);
    tap(&devices.keyboard.escape);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Room && local.Rooms_Lobby.resume && !plays());
    TIDE_CHECK(audio.bus_volume[0] == volume && asked() == TIDE_REQUEST_NONE);

    // The room's Back route returns to the room too; Escape there resumes.
    for (int i = 0; i < 4; i++) tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Settings);
    tap(&devices.keyboard.tab);
    tap(&devices.keyboard.enter);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Room && local.Rooms_Lobby.resume && !plays());
    tap(&devices.keyboard.escape);
    frame(&world);
    TIDE_CHECK(plays() && audio.bus_volume[0] == volume);

    // A session that ends returns to the title and starts this machine's own
    // match. Its local volume survives that change of match.
    tide_local_disconnected(&local, TIDE_DISCONNECT_ENDED, "");
    tide_local_set_session(&local, TIDE_SESSION_OFFLINE, (tide_player_id){0}, 0, false, false, "");
    frame(NULL);
    frame(NULL);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title && audio.bus_volume[0] == volume);
    TIDE_CHECK(asked() == TIDE_REQUEST_START);
    tide_world_free(&world);
    tide_world_init(&world, DT);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, player(0), 0, true, false, "");
    frame(&world);
    frame(&world);
    TIDE_CHECK(local.Rooms_Lobby.volume == volume && audio.bus_volume[0] == volume);

    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_world_free(&world);
}
