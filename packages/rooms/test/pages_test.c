// A game with pages of its own (pages.tide), with no window: a page it
// overrides is the one drawn, its buttons take the menu where the package's
// would, what they ask of the session is asked, and the pages it left alone
// are the package's still.

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

static void frame(void)
{
    tide_local_frame_time(&local, DT);
    tide_draw_reset(&draw);
    tide_audio_reset(&audio);
    tide_local_audio(&local, &audio);
    tide_touches_poll(&devices.touchscreen);
    tide_pointer_poll(&devices, true);
    tide_gui_begin(&gui, &devices, tide_f2(376, 400), NULL);
    tide_frame(&world, NULL, 1.0f, &world, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

// A frame in which a finger does `phase` at (x, y), from the screen's top left
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

static void press(const float x, const float y)
{
    finger(TIDE_TOUCH_BEGAN, x, y);
    finger(TIDE_TOUCH_ENDED, x, y);
}

// What local code asked of the session since it was last asked
static uint32_t asked(void)
{
    tide_session_request request = {0};
    tide_start start;
    return tide_local_take_request(&local, &request, &start) ? request.kind : (uint32_t)TIDE_REQUEST_NONE;
}

TIDE_TEST(rooms_a_game_draws_pages_of_its_own)
{
    tide_world_init(&world, DT);
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    memset(&devices, 0, sizeof devices);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    frame();
    frame();
    // The title is the game's: no modal of the package's is up
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title && gui.modal == 0);

    // Its Friends button hosts, as the package's would: the session is asked
    // to open, and the room's page, which is the package's, comes up
    press(60.0f, 80.0f);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Room && !local.Rooms_Lobby.resume);
    TIDE_CHECK(asked() == TIDE_REQUEST_OPEN);
    frame();
    TIDE_CHECK(gui.modal != 0 && asked() == TIDE_REQUEST_NONE);

    // The settings are a view of the game's, after the package's: its page draws nothing
    local.Rooms_Lobby.settingsFrom = Rooms_Page_Title;
    local.Rooms_Lobby.page = Rooms_Page_Settings;
    frame();
    frame();
    TIDE_CHECK(local.Look.shown == 2 && gui.modal == 0);
    // What a view after the package's asks for is asked the frame after
    press(60.0f, 80.0f);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Room && asked() == TIDE_REQUEST_NONE);
    frame();
    TIDE_CHECK(asked() == TIDE_REQUEST_OPEN);
    frame();
    TIDE_CHECK(asked() == TIDE_REQUEST_NONE);

    // Done goes back to the page the settings came from
    local.Rooms_Lobby.page = Rooms_Page_Settings;
    frame();
    press(60.0f, 30.0f);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Title);
    // Solo: into the match, with a new game for the game to start
    frame();
    finger(TIDE_TOUCH_BEGAN, 60.0f, 30.0f);
    finger(TIDE_TOUCH_ENDED, 60.0f, 30.0f);
    TIDE_CHECK(local.Rooms_Lobby.page == Rooms_Page_Playing && asked() == TIDE_REQUEST_NONE);

    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_world_free(&world);
}
