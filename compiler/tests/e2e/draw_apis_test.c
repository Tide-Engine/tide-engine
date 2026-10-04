#include "game.h"
#include "tide_test.h"

static bool close_to(const float a, const float b)
{
    const float difference = a - b;
    return difference * difference < 0.0001f;
}

TIDE_TEST(draw_apis_use_the_current_cameras_and_frame_size)
{
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_devices devices = {0};
    tide_local_init(&local);
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL);
    tide_frame(NULL, NULL, 1, NULL, &local, &draw, &gui);

    TIDE_CHECK(local.Probe.sameList);
    TIDE_CHECK(close_to(local.Probe.screen2.x, 400) && close_to(local.Probe.screen2.y, 300));
    TIDE_CHECK(close_to(local.Probe.world2.x, 2) && close_to(local.Probe.world2.y, 3));
    TIDE_CHECK(local.Probe.overlay.x == 12 && local.Probe.overlay.y == 34);
    TIDE_CHECK(close_to(local.Probe.screen3.x, 400) && close_to(local.Probe.screen3.y, 300));
    TIDE_CHECK(local.Probe.screen3.z > 0 && local.Probe.screen3.z < 1);
    TIDE_CHECK(close_to(local.Probe.world3.x, 0) && close_to(local.Probe.world3.y, 0) && close_to(local.Probe.world3.z, 5));
    TIDE_CHECK(close_to(local.Probe.rayOrigin.x, 0) && close_to(local.Probe.rayOrigin.y, 0)
               && close_to(local.Probe.rayOrigin.z, -4.7f));
    TIDE_CHECK(close_to(local.Probe.rayDirection.x, 0) && close_to(local.Probe.rayDirection.y, 0)
               && close_to(local.Probe.rayDirection.z, 1));
    uint32_t shaded = 0, instances = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind == TIDE_DRAW_SHADED) shaded++;
        if (draw.commands[i].kind == TIDE_DRAW_MESH_3D) instances += draw.commands[i].mesh3.count;
    }
    TIDE_CHECK(shaded == 2 && instances == 7);
    tide_draw_free(&draw);
    tide_local_free(&local);
}
