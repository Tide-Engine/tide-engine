#include "elsewhere_render_test.h"
#include "tide/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { W = 640, H = 360 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static uint32_t before[W * H], after[W * H];
static tide_float2 points[W * H];
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Elsewhere render: %s at %d\n", #x, __LINE__); exit(1); } } while (0)

static void picture(uint32_t *out)
{
    const tide_devices devices = {0};
    const tide_float2 screen = {W, H};
    tide_local_frame_time(&local, 0);
    tide_draw_reset(&draw); tide_draw_screen_size(&draw, screen);
    tide_gui_begin(&gui, &devices, screen, tide_platform_measure_text);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    tide_platform_read_pixels_as(&draw, W, H, 1, points, W * H, out);
}
static int difference(void)
{
    int count = 0;
    for (int i = 0; i < W * H; i++) {
        int change = 0;
        for (int shift = 8; shift <= 24; shift += 8) change += abs((int)((before[i] >> shift) & 255) - (int)((after[i] >> shift) & 255));
        if (change > 28) count++;
    }
    return count;
}
static void close_window(Gardener p, float distance, float yaw, float pitch)
{
    // Close enough that the opening fills the view: every ray should show
    // the same garden as the translated camera, even inside the usual near clip.
    p.realm = 0; p.position = (tide_float3){-7, 0, 6 - distance};
    p.yaw = yaw; p.pitch = pitch;
    CHECK(tide_set_Gardener(&world, world.Garden.people.at[0], p));
    picture(before);
    p.realm = 1; p.position.x += 7; p.position.z -= 16;
    CHECK(tide_set_Gardener(&world, world.Garden.people.at[0], p));
    picture(after);
    const int changed = difference();
    printf("Close window %.5f, yaw %.2f, pitch %.2f: %d changed pixels\n", distance, yaw, pitch, changed);
    CHECK(changed < W * H / 50);
}
static int frame(void *user, float dt)
{
    (void)user; (void)dt;
    tide_world_init(&world, 1.0f / 60);
    tide_world_player_joined(&world, tide_player_from_index(0)); tide_world_tick(&world); tide_world_tick(&world);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    local.Menu.open = false; local.Menu.begun = true; local.Optics.photo = true;
    Gardener p = tide_get_Gardener(&world, world.Garden.people.at[0]);
    p.position = (tide_float3){-7, 0, 1}; p.yaw = 0; p.pitch = 0;
    CHECK(tide_set_Gardener(&world, world.Garden.people.at[0], p));
    picture(before);
    close_window(p, 0.1f, 0, 0);
    close_window(p, 0.02f, 0.65f, 0.25f);
    close_window(p, 0.0001f, -0.65f, -0.25f);
    CHECK(tide_set_Gardener(&world, world.Garden.people.at[0], p));
    picture(before);
    // A real destination plant must change the source window's picture.
    world.Garden.source.at[0] = 24; world.Garden.growth.at[0] = 1;
    picture(after); CHECK(difference() > 40);
    memcpy(before, after, sizeof(before));
    p.realm = 2; p.position = (tide_float3){0, 0, -7};
    CHECK(tide_set_Gardener(&world, world.Garden.people.at[0], p));
    picture(after); CHECK(difference() > W * H / 6);
    p.realm = 0; p.position = (tide_float3){-2, 0, -5}; p.yaw = 0.25f;
    CHECK(tide_set_Gardener(&world, world.Garden.people.at[0], p));
    picture(before);
    world.Garden.flowers = 3; world.Garden.finale = 1;
    for (int i = 0; i < 3; i++) world.Garden.planted.at[i] = true;
    picture(after); CHECK(difference() > 80);
    tide_draw_free(&draw); tide_draw_free(&gui.list);
    tide_draw_forget(&draw);
    tide_local_free(&local); tide_world_free(&world);
    puts("Elsewhere rendering checks passed");
    return 0;
}

int main(void)
{
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) points[y * W + x] = (tide_float2){x + 0.5f, y + 0.5f};
    tide_platform_open(&(tide_window_desc){.title = "Elsewhere rendering", .width = W, .height = H, .hidden = true});
    tide_platform_run(frame, NULL);
}
