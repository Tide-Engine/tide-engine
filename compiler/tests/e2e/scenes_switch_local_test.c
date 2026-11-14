#include "game.h"
#include "tide_test.h"

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(NULL, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui); // No match
}

TIDE_TEST(scenes_switch_local_menu_stays_gone_in_a_match)
{
    tide_local_init(&local);
    TIDE_CHECK(local.Frames.mains == 1);
    TIDE_CHECK(tide_local_entity_count(&local) == 2); // Main and its button

    // A function's switch: the menu goes, with its button, and the lobby loads
    frame();
    TIDE_CHECK(local.Frames.lobbies == 1 && local.Frames.seats == 4);
    TIDE_CHECK(local.Frames.mains == 1);
    TIDE_CHECK(tide_local_entity_count(&local) == 3); // The lobby and its two buttons
    frame();
    TIDE_CHECK(tide_local_entity_count(&local) == 3);

    // In a match, the last local scene unloads and nothing comes back
    local.Session.state = TIDE_SESSION_CONNECTED;
    frame();
    TIDE_CHECK(tide_local_entity_count(&local) == 0);
    frame();
    TIDE_CHECK(tide_local_entity_count(&local) == 0 && local.Frames.mains == 1);
    local.Session.state = TIDE_SESSION_CONNECTING;
    frame();
    TIDE_CHECK(tide_local_entity_count(&local) == 0 && local.Frames.mains == 1);

    // Offline, but with a match asked for: still gone
    local.Session.state = TIDE_SESSION_OFFLINE;
    local.tide_request.kind = TIDE_REQUEST_START;
    frame();
    TIDE_CHECK(tide_local_entity_count(&local) == 0 && local.Frames.mains == 1);

    // Offline with nothing asked: the startup scene loads again
    local.tide_request = (tide_session_request){0};
    frame();
    TIDE_CHECK(local.Frames.mains == 2);
    TIDE_CHECK(tide_local_entity_count(&local) == 2);
}
