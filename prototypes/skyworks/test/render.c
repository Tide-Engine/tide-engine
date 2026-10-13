#include "fixture.h"
#include "tide/platform.h"
#include <stdio.h>

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static int frames;
static bool thumbnail;
static int frame(void *user, float seconds)
{
    (void)user; (void)seconds;
    tide_devices devices = {0};
    if (!thumbnail && (frames == 1 || frames == 3))
        devices.keyboard.tab = (tide_button){.held = true, .pressed = true, .down = true};
    if (frames == 1 && !thumbnail)
    {
        // Corrected terrain can have the same revision as a predicted edit.
        tide_text_use(&world.heap, NULL);
        const Block air = Block_Air;
        memcpy(tide_grid_poke(&world.Island.blocks, 16, 3, 16, &tide_shape_Grid3_Block, true, TIDE_IN_MATCH), &air, sizeof air);
    }
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_platform_screen_size(), tide_platform_measure_text);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    assert(local.Tools.menu == (!thumbnail && (frames == 1 || frames == 2)));
    tide_gui_end(&gui, &draw);
    assert(tide_list_count(local.Scenery.vertices) > 100);
    if (frames >= 1 && !thumbnail)
    {
        const Block *cached = tide_grid_read(&local.heap, local.Scenery.cached, 16, 3, 16, &tide_shape_Grid3_Block);
        assert(!cached || *cached == Block_Air);
    }
    tide_platform_draw(&draw);
    if (++frames == 4) { puts("SKYWORKS render passed"); return thumbnail ? TIDE_KEEP_RUNNING : 0; }
    return TIDE_KEEP_RUNNING;
}
int main(int argc, char **argv)
{
    factory_fixture(&world); ticks(&world, 900);
    world.Pilot.position = (tide_float3){21.5f, 4.01f, 11.5f};
    world.Pilot.yaw = -0.55f; world.Pilot.pitch = 0.23f;
    tide_local_init(&local);
#if defined(__wasm__)
    (void)argc; (void)argv;
    tide_platform_open(&(tide_window_desc){.title = "SKYWORKS render test", .width = 960, .height = 540, .hidden = true});
#else
    const char *capture = argc == 3 && strcmp(argv[1], "--capture") == 0 ? argv[2] : NULL;
    thumbnail = capture != NULL;
    tide_platform_open_headless(&(tide_headless_desc){.width = 960, .height = 540, .frames = 4, .capture = capture});
#endif
    tide_platform_run(frame, NULL);
}
