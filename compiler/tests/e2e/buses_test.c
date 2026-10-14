#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/audio.h"
#include "tide/pack.h"
#include "tide_test.h"

// Buses (buses.tide): a Sound's bus and the bus volumes a frame records.

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_audio_frame audio;

static void frame(void)
{
    tide_audio_reset(&audio);
    tide_draw_reset(&draw);
    static tide_devices devices;
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL);
    tide_local_audio(&local, &audio);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

TIDE_TEST(buses_a_sounds_bus_and_the_frames_volumes)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    local.Mix.music = 0.5f;
    frame();
    TIDE_REQUIRE(audio.voice_count == 1);
    TIDE_CHECK(audio.voices[0].bus == 1); // Bus.Music
    TIDE_CHECK(audio.bus_volume[0] == 1.0f && audio.bus_volume[1] == 0.5f && audio.bus_volume[2] == 1.0f);
    local.Mix.quiet = true;
    local.Mix.volume = 0.75f;
    local.Mix.music = 0.25f;
    frame();
    TIDE_CHECK(audio.bus_volume[0] == 0.75f && audio.bus_volume[1] == 0.25f && audio.bus_volume[2] == 0.0f);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_clips_free();
    tide_pack_unload();
}
