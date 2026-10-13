#include "deadweight_render_test.h"
#include "tide/platform.h"
#include <stdio.h>
#include <stdlib.h>

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
enum { WIDTH = 960, HEIGHT = 640, SAMPLES = 400 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static int failures;

static void check(bool passed, const char *message)
{
    if (passed) return;
    fprintf(stderr, "DEADWEIGHT rendering failed: %s\n", message);
    failures++;
}
static int distance(uint32_t a, uint32_t b)
{
    int sum = 0;
    for (int shift = 8; shift <= 24; shift += 8)
        sum += abs((int)(a >> shift & 255) - (int)(b >> shift & 255));
    return sum;
}
static void record(uint32_t pixels[SAMPLES])
{
    tide_float2 points[SAMPLES];
    const tide_devices devices = {0};
    local.Rooms_Lobby.started = true;
    local.Rooms_Lobby.page = Rooms_Page_Playing;
    local.Rooms_Lobby.playing = true;
    tide_local_frame_time(&local, 0);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, (tide_float2){WIDTH, HEIGHT});
    tide_gui_begin(&gui, &devices, (tide_float2){WIDTH, HEIGHT}, tide_platform_measure_text);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    check(draw.count > 12, "site, shadow pass, cables and HUD issue draw commands");
    for (int row = 0; row < 20; row++)
        for (int column = 0; column < 20; column++)
            points[row * 20 + column] = (tide_float2){150 + column * 32.0f, 130 + row * 19.0f};
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, 1, points, SAMPLES, pixels);
}
static int rendering(void *user, float seconds)
{
    (void)user;
    (void)seconds;
    uint32_t first[SAMPLES], next[SAMPLES];
    record(first);
    int varied = 0;
    for (int i = 1; i < SAMPLES; i++) if (distance(first[0], first[i]) > 40) varied++;
    check(varied > 80, "the station is geometry and shading rather than an empty canvas");
    tide_world_set_input(&world, tide_player_from_index(0), (Controls){.shared = true, .reel = true, .secondReel = true});
    for (int i = 0; i < 215; i++) tide_world_tick(&world);
    record(next);
    int moved = 0;
    for (int i = 0; i < SAMPLES; i++) if (distance(first[i], next[i]) > 40) moved++;
    check(moved > 10, "hauling the conveyor changes the cargo and cable image");
    for (int job = 1; job < 3; job++) {
        world.Site.job = job;
        world.Site.rebuild = true;
        tide_world_set_input(&world, tide_player_from_index(0), (Controls){0});
        for (int i = 0; i < 5; i++) tide_world_tick(&world);
        record(next);
        int changed = 0;
        for (int i = 0; i < SAMPLES; i++) if (distance(first[i], next[i]) > 40) changed++;
        check(changed > 20, "the other jobs render their machinery and distinct loads");
    }
    tide_draw_forget(&draw);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_render_targets_free();
    tide_world_free(&world);
    if (failures) return 1;
    puts("DEADWEIGHT rendering checks passed");
    return 0;
}
int main(void)
{
    check(tide_assets_load(tide_game_pack, tide_game_pack_size), "the embedded cue pack loads");
    tide_world_init(&world, 1.0f / 60);
    tide_world_player_joined(&world, tide_player_from_index(0));
    for (int i = 0; i < 5; i++) tide_world_tick(&world);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    tide_platform_open(&(tide_window_desc){.title = "DEADWEIGHT render checks", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(rendering, NULL);
}
