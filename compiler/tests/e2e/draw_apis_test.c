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
    devices.pointer.position = tide_f2(100, 450); // From the bottom left, as devices count
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL);
    tide_frame(NULL, NULL, 1, NULL, NULL, 1.0f, &local, &draw, &gui);

    TIDE_CHECK(local.Probe.sameList);
    TIDE_CHECK(close_to(local.Probe.screen2.x, 400) && close_to(local.Probe.screen2.y, 300));
    TIDE_CHECK(close_to(local.Probe.world2.x, 2) && close_to(local.Probe.world2.y, 3));
    TIDE_CHECK(local.Probe.overlay.x == 12 && local.Probe.overlay.y == 34);
    // Screen.Flip: the pointer from the top left, 60 pixels a unit from the camera's center, and back again
    TIDE_CHECK(local.Probe.pointer.x == 100 && local.Probe.pointer.y == 150);
    TIDE_CHECK(close_to(local.Probe.pointerWorld.x, -3) && close_to(local.Probe.pointerWorld.y, 5.5f));
    TIDE_CHECK(local.Probe.corner.x == 0 && local.Probe.corner.y == 600);
    TIDE_CHECK(local.Probe.flippedTwice.x == 100 && local.Probe.flippedTwice.y == 450);
    TIDE_CHECK(close_to(local.Probe.screen3.x, 400) && close_to(local.Probe.screen3.y, 300));
    TIDE_CHECK(local.Probe.screen3.z > 0 && local.Probe.screen3.z < 1);
    TIDE_CHECK(close_to(local.Probe.world3.x, 0) && close_to(local.Probe.world3.y, 0) && close_to(local.Probe.world3.z, 5));
    TIDE_CHECK(close_to(local.Probe.rayOrigin.x, 0) && close_to(local.Probe.rayOrigin.y, 0)
               && close_to(local.Probe.rayOrigin.z, -4.7f));
    TIDE_CHECK(close_to(local.Probe.rayDirection.x, 0) && close_to(local.Probe.rayDirection.y, 0)
               && close_to(local.Probe.rayDirection.z, 1));
    // The cameras as matrices. The 2D one: 60 pixels a unit around (2, 3), y turned over; the identity in pixels
    const tide_float3x3 flat = local.Probe.flat, same = tide_identity_f3x3();
    TIDE_CHECK(close_to(flat.c0.x, 60) && close_to(flat.c1.y, -60) && close_to(flat.c2.x, 280) && close_to(flat.c2.y, 480));
    TIDE_CHECK(flat.c0.y == 0 && flat.c1.x == 0 && flat.c2.z == 1);
    TIDE_CHECK(memcmp(&local.Probe.flatPixels, &same, sizeof same) == 0);
    // The 3D one is what WorldToScreen goes through, in a function too, and its parts are the camera's
    TIDE_CHECK(close_to(local.Probe.throughMatrix.x, local.Probe.screen3b.x) && close_to(local.Probe.throughMatrix.y, local.Probe.screen3b.y)
               && close_to(local.Probe.throughMatrix.z, local.Probe.screen3b.z));
    const tide_float4x4 square = tide_draw_camera_3d_matrix(tide_f3(0, 0, -5), tide_identity_q(), 60);
    const tide_float4x4 fitted = tide_draw_camera_fit(square, 800, 600);
    TIDE_CHECK(memcmp(&local.Probe.toClip, &fitted, sizeof fitted) == 0);
    TIDE_CHECK(local.Probe.place.c3.z == -5 && local.Probe.place.c0.x == 1 && local.Probe.place.c3.w == 1);
    TIDE_CHECK(close_to(local.Probe.projection.c0.x, local.Probe.projection.c1.y * 0.75f) && local.Probe.projection.c2.w == -1);
    // Before any camera is set, each frame's first
    const tide_float4x4 first = tide_draw_camera_fit(tide_draw_first_camera_3d().matrix, 800, 600);
    TIDE_CHECK(memcmp(&local.Probe.firstClip, &first, sizeof first) == 0);
    // A target's own camera, with nothing of the window's shape: given back as it was set...
    const tide_float4x4 sees = tide_ortho_f4x4(8, 2, 1, 9);
    tide_float4 seen = tide_mul_f4x4_f4(local.Probe.wideClip, tide_f4(4, -1, 6, 1));
    TIDE_CHECK(close_to(seen.x, 1) && close_to(seen.y, -1) && close_to(seen.z, 1) && close_to(seen.w, 1));
    TIDE_CHECK(memcmp(&local.Probe.wideAfter, &local.Probe.wideClip, sizeof sees) == 0);
    // ...and one with a field of view fitted to the target's 64 by 16, inside its block
    TIDE_CHECK(close_to(local.Probe.wideBlock.c0.x, 0.25f) && close_to(local.Probe.wideBlock.c1.y, 1));
    TIDE_CHECK(close_to(local.Probe.wideProjection.c0.x, 0.25f) && close_to(local.Probe.wideProjection.c1.y, 1));
    TIDE_CHECK(close_to(local.Probe.wideFlat.c0.x, 2) && close_to(local.Probe.wideFlat.c2.x, 30) && close_to(local.Probe.wideFlat.c2.y, 10));
    uint32_t shaded = 0, instances = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        if (draw.commands[i].kind == TIDE_DRAW_SHADED) shaded++;
        if (draw.commands[i].kind == TIDE_DRAW_MESH_3D) instances += draw.commands[i].mesh3.count;
    }
    TIDE_CHECK(shaded == 2 && instances == 7);
    tide_draw_free(&draw);
    tide_local_free(&local);
}
