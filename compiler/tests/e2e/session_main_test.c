#include <string.h>

#include "game.h"
#include "tide/session.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
}

TIDE_TEST(session_start_with_no_scene_is_main_and_the_title_is_the_settings)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_world_tick(&world);
    TIDE_CHECK(world.Tally.titleLength == 12);

    tide_session_request request = {0};
    tide_start start = {0};
    frame();
    const tide_str title = tide_text_read(&local.heap, local.Menu.title);
    TIDE_CHECK(title.bytes == 12 && memcmp(title.ptr, "Session Main", 12) == 0);
    TIDE_REQUIRE(tide_local_take_request(&local, &request, &start));
    TIDE_CHECK(request.kind == TIDE_REQUEST_START && start.value.Main.size == 30);

    frame();
    TIDE_REQUIRE(tide_local_take_request(&local, &request, &start));
    TIDE_CHECK(request.kind == TIDE_REQUEST_START && start.value.Main.size == 40);
    TIDE_CHECK(!tide_local_take_request(&local, &request, &start));

    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
}
