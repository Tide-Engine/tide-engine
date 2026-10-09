#include <stdint.h>
#include <stdio.h>
#include "game.h"
#include "tide/platform.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static int frame(void *unused, float seconds)
{
    (void)unused;
    (void)seconds;
    uint64_t hash = tide_world_hash(&world);
    int failed = 0;
    const int sizes[][2] = {{960, 540}, {390, 780}};
    for (unsigned size = 0; size < 2; size++) {
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, tide_f2((float)sizes[size][0], (float)sizes[size][1]), tide_platform_measure_text);
        tide_local_frame_time(&local, 1.0f / 60.0f);
        tide_frame(&world, NULL, 1, NULL, &local, &draw, &gui);
        tide_gui_end(&gui, &draw);
        tide_float2 points[32];
        uint32_t pixels[32];
        for (int i = 0; i < 32; i++) points[i] = tide_f2((float)sizes[size][0] * (float)(i % 8 + 1) / 9,
                                                      (float)sizes[size][1] * (float)(i / 8 + 1) / 5);
        tide_platform_read_pixels_as(&draw, sizes[size][0], sizes[size][1], 1, points, 32, pixels);
        unsigned varied = 0;
        for (int i = 1; i < 32; i++) if (pixels[i] != pixels[0]) varied++;
        printf("deadbell: %dx%d, %d enemies shown, %u varied samples\n", sizes[size][0], sizes[size][1], local.Visuals.shown, varied);
        if (local.Visuals.shown < 1000 || varied < 8) failed++;
    }
    // On portrait phones the interact button overlaps the movement half.
    // Exercise the real view-to-input path so its press stays an action.
    devices.touchscreen.connected = true;
    local.Rooms_Lobby.playing = true;
    for (int action = 0; action < 6; action++) {
        tide_touch *touch = &devices.touchscreen.touches.at[0];
        *touch = (tide_touch){.id = 1, .press = {.pressed = true, .held = true}};
        touch->position = action < 4 ? tide_f2(166.0f + (float)action * 64.0f, 65)
                                    : action == 4 ? tide_f2(70, 250) : tide_f2(240, 450);
        touch->startPosition = action == 4 ? tide_f2(70, 198) : touch->position;
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, tide_f2(390, 780), tide_platform_measure_text);
        tide_frame(&world, NULL, 1, NULL, &local, &draw, &gui);
        tide_gui_end(&gui, &draw);
        const Controls sampled = tide_input_sample(&devices, &local);
        if (sampled.interact != (action == 0) || sampled.lure != (action == 1)
            || sampled.dash != (action == 2) || sampled.heavy != (action == 3)
            || sampled.fire != (action == 5)) failed++;
        if (action == 4 ? sampled.move.y < 0.99f : sampled.move.x != 0 || sampled.move.y != 0) failed++;
    }
    printf("deadbell: portrait touch actions, movement and aiming checked\n");
    if (world.Expedition.live != 8000 || tide_world_hash(&world) != hash) failed++;
    printf("DEADBELL_RENDER_%s\n", failed ? "FAIL" : "PASS");
    return failed;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    world.Expedition.phase = RunPhase_Hold;
    tide_world_player_joined(&world, tide_player_from_index(0));
    tide_world_player_joined(&world, tide_player_from_index(1));
    tide_world_tick(&world);
    world.Expedition.goal = tide_f2(0, 12);
    world.Expedition.goalB = world.Expedition.goal;
    tide_local_init(&local);
    local.Rooms_Lobby.page = Rooms_Page_Playing;
    local.Rooms_Lobby.started = true;
    tide_local_set_session(&local, SessionState_Connected, tide_player_from_index(0), 0, true, true, "AAAAAA");
    tide_platform_open(&(tide_window_desc){.title = "Deadbell private renderer", .width = 960, .height = 540, .hidden = true});
    tide_platform_run(frame, NULL);
}
