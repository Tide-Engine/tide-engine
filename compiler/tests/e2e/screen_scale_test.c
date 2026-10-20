#include "game.h"
#include "tide_test.h"

TIDE_TEST(screen_scale_is_frame_local_host_metadata_and_defaults_to_one)
{
    tide_world world = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_devices devices = {0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    for (int frame = 0; frame < 3; frame++) {
        tide_draw_reset(&draw);
        draw.screen_scale = frame == 1 ? tide_f2(1.5f, 2.0f) : tide_f2(0, 0);
        tide_gui_begin(&gui, &devices, tide_f2(320, 240), NULL);
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        TIDE_CHECK(local.Seen.scale.x == (frame == 1 ? 1.5f : 1.0f));
        TIDE_CHECK(local.Seen.scale.y == (frame == 1 ? 2.0f : 1.0f));
        TIDE_CHECK(local.Seen.width == 320 && local.Seen.height == 240);
        tide_gui_end(&gui, &draw);
    }
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
}
