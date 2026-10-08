#include <stdint.h>
#include <stdio.h>

#include "game.h"
#include "tide/platform.h"
#include "tide/net.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

static int frame(void *user, float seconds)
{
    (void)user;
    (void)seconds;
    int failures = 0;
    for (int scene = 0; scene < 7; scene++) {
        world.Flight.phase = scene == 0 ? 1 : scene == 1 ? 2 : scene == 6 ? 5 : 3;
        world.Flight.caseIndex = scene >= 2 && scene <= 5 ? scene - 2 : 0;
        world.Flight.clues = 3;
        world.Flight.readers = 3;
        world.Flight.outcome = 1;
        for (uint32_t slot = 0; slot < world.entities.next_unused; slot++) {
            const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
            if (!tide_has_Passenger(&world, entity)) continue;
            Passenger passenger = tide_get_Passenger(&world, entity);
            passenger.identity = passenger.id == 8 ? (int[]){0, 3, 5, 2}[world.Flight.caseIndex] : passenger.id;
            passenger.position = tide_f3(passenger.id % 2 ? 1.3f : -1.3f, 0, 3.2f + (float)(passenger.id / 2) * 1.8f);
            (void)tide_set_Passenger(&world, entity, passenger);
        }
        local.Me.notebook = scene == 5;
        for (int settle = 0; settle < 6; settle++) {
            tide_local_frame_time(&local, 1.0f / 60.0f);
            tide_draw_reset(&draw);
            tide_gui_begin(&gui, &devices, tide_f2(960, 540), tide_platform_measure_text);
            tide_frame(&world, NULL, 1, NULL, &local, &draw, &gui);
            tide_gui_end(&gui, &draw);
        }
        tide_float2 points[64];
        uint32_t pixels[64];
        for (int i = 0; i < 64; i++) points[i] = tide_f2(70 + (float)(i % 8) * 116, 120 + (float)(i / 8) * 48);
        tide_platform_read_pixels_as(&draw, 960, 540, 1, points, 64, pixels);
        int variations = 0;
        for (int i = 1; i < 64; i++) if (pixels[i] != pixels[0]) variations++;
        const int passes = variations > 16 && draw.count > 20;
        printf("%s: cabin scene %d draws cabin and overlays (%d varied pixels, %u commands)\n",
               passes ? "ok" : "FAIL", scene, variations, draw.count);
        if (!passes) failures++;
    }
    puts(failures ? "Cabin render failed" : "Cabin render passed");
    return failures;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    for (int seat = 0; seat < 2; seat++) tide_world_player_joined(&world, tide_player_from_index(seat));
    tide_world_tick(&world);
    tide_world_tick(&world);
    world.Roster.ready = 3;
    for (uint32_t slot = 0; slot < world.entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
        if (!tide_has_Crew(&world, entity)) continue;
        Crew crew = tide_get_Crew(&world, entity);
        crew.ready = true;
        (void)tide_set_Crew(&world, entity, crew);
    }
    world.Flight.phase = 2;
    world.Flight.phaseSeconds = 8;
    tide_world_tick(&world);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    local.Rooms_Lobby.started = true;
    local.Rooms_Lobby.page = Rooms_Page_Playing;
    tide_platform_open(&(tide_window_desc){.title = "Cabin render test", .width = 960, .height = 540, .hidden = true});
    tide_platform_run(frame, NULL);
}
