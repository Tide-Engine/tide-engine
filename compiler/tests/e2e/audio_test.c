#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/audio.h"
#include "tide/pack.h"
#include "tide/wav.h"
#include "tide_test.h"

// Clip assets and Audio.Play (audio.tide): what a frame records into the
// audio list the host gives it, with each Clip's samples found in the pack.

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_audio_frame audio;
static tide_event_log events;

static void frame(tide_audio_frame *into)
{
    tide_audio_reset(&audio);
    tide_draw_reset(&draw);
    static tide_devices devices;
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL);
    tide_local_audio(&local, into);
    tide_local_events(&local, &events);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_world_tick_events(&world, NULL, &events); // Main spawns the body
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_event_log_free(&events);
    tide_clips_free();
    tide_pack_unload();
}

static bool near(const float a, const float b) { return a - b < 1e-6f && b - a < 1e-6f; }

TIDE_TEST(audio_clip_assets_name_sounds_of_the_pack_and_decode_once)
{
    start();
    const tide_clip_ref beep = tide_pack_clip_of(tide_pack_find("beep", TIDE_PACK_CLIP));
    TIDE_CHECK(beep.name != 0);
    const tide_clip clip = tide_clip_of(beep);
    TIDE_REQUIRE(clip.samples != NULL);
    TIDE_CHECK(clip.frames == 40 && clip.channels == 1 && clip.rate == 8000);
    TIDE_CHECK(near(clip.samples[3], 1500.0f / 32768.0f) && clip.samples[0] == 0.0f);
    TIDE_CHECK(clip.id != 0 && clip.name == beep.name);
    TIDE_CHECK(tide_clip_of(beep).samples == clip.samples); // Decoded once
    // The glob's list and map: two files, as clips
    const tide_pack_entry *list = tide_pack_find("hits", TIDE_PACK_LIST);
    TIDE_REQUIRE(list != NULL);
    TIDE_CHECK(list->extra == TIDE_PACK_CLIP && list->size == 2);
    TIDE_CHECK(tide_pack_find("named", TIDE_PACK_CLIPS) != NULL);
    // A name the pack hasn't got, and none at all
    TIDE_CHECK(tide_clip_of((tide_clip_ref){.name = 12345}).samples == NULL);
    TIDE_CHECK(tide_clip_of((tide_clip_ref){0}).samples == NULL && tide_clip_of((tide_clip_ref){0}).id == 0);
    finish();
}

TIDE_TEST(audio_a_local_handler_plays_the_matchs_event)
{
    start();
    frame(&audio);
    TIDE_CHECK(audio.voice_count == 0 && local.Sfx.heard == 0 && local.Sfx.count == 2);
    tide_world_tick_events(&world, NULL, &events); // Tick 2 sends Hit
    frame(&audio);
    TIDE_CHECK(local.Sfx.heard == 1);
    TIDE_REQUIRE(audio.voice_count == 1);
    const tide_voice *v = &audio.voices[0];
    TIDE_CHECK(v->id == 0 && v->playing && v->spatial);
    TIDE_CHECK(v->position.x == 1.0f && v->position.y == 2.0f && v->position.z == 3.0f);
    TIDE_CHECK(v->volume == 1.0f && v->pitch == 1.0f);
    TIDE_CHECK(v->clip.frames == 40 && v->clip.rate == 8000 && v->clip.samples != NULL); // beep, found in the pack
    frame(&audio);
    TIDE_CHECK(audio.voice_count == 0 && local.Sfx.heard == 1); // Heard once
    finish();
}

TIDE_TEST(audio_views_and_the_functions_they_call_play)
{
    start();
    local.Sfx.play = true;
    frame(&audio);
    TIDE_REQUIRE(audio.voice_count == 1);
    const tide_voice *v = &audio.voices[0];
    TIDE_CHECK(v->spatial && v->position.x == 4.0f && v->position.y == 5.0f && v->position.z == 6.0f);
    TIDE_CHECK(v->volume == 0.5f && v->pitch == 2.0f);
    TIDE_CHECK(v->clip.frames == 4 && v->clip.channels == 2 && v->clip.rate == 22050); // hits[0]: a.wav, 8 bits
    TIDE_CHECK(v->clip.samples && v->clip.samples[0] == -1.0f && near(v->clip.samples[2], -0.5f) && v->clip.samples[1] == 0.0f);
    // The keeper copies a clip out of the map and plays it through two functions
    local.Sfx.play = false;
    local.Sfx.plays = 1;
    frame(&audio);
    TIDE_REQUIRE(audio.voice_count == 2);
    TIDE_CHECK(local.Sfx.kept.name != 0);
    const tide_voice *at = &audio.voices[0], *plain = &audio.voices[1];
    TIDE_CHECK(at->spatial && at->position.x == 7.0f && at->position.y == 8.0f && at->position.z == 0.0f);
    TIDE_CHECK(at->volume == 0.25f && at->pitch == 1.5f);
    TIDE_CHECK(at->clip.frames == 3 && at->clip.channels == 1 && at->clip.rate == 48000 && at->clip.samples[1] == -0.5f); // b.wav
    TIDE_CHECK(!plain->spatial && plain->volume == 1.0f && plain->pitch == 1.0f && plain->clip.id == at->clip.id);
    TIDE_CHECK(at->clip.name == local.Sfx.kept.name);
    // A host that gives no audio list: nothing is recorded, and nothing breaks
    local.Sfx.plays = 1;
    frame(NULL);
    TIDE_CHECK(audio.voice_count == 0);
    finish();
}

TIDE_TEST(audio_a_clip_follows_the_pack_thats_loaded)
{
    start();
    const tide_clip_ref beep = tide_pack_clip_of(tide_pack_find("beep", TIDE_PACK_CLIP));
    const tide_clip before = tide_clip_of(beep);
    TIDE_REQUIRE(before.samples != NULL && before.frames == 40);
    // The pack loaded again with another beep under the same name, as tide run packs an edited file
    float stereo[10 * 2];
    for (int i = 0; i < 20; i++) stereo[i] = 0.5f;
    size_t wav_size;
    uint8_t *wav = tide_wav_encode(stereo, 10, 8000, &wav_size);
    uint8_t content[32] = {9, 8, 7};
    tide_pack_builder b = {0};
    const uint32_t at = tide_pack_clip(&b, content, wav, (uint32_t)wav_size);
    tide_pack_name(&b, "beep", TIDE_PACK_CLIP, at, (uint32_t)wav_size, 0);
    size_t size;
    uint8_t *bytes = tide_pack_end(&b, &size);
    TIDE_REQUIRE(tide_assets_load(bytes, size));
    const tide_clip after = tide_clip_of(beep);
    TIDE_CHECK(after.samples != NULL && after.frames == 10 && after.channels == 2 && after.id != before.id);
    TIDE_CHECK(before.samples[3] != 0.0f); // The old samples are still there for a voice that plays them
    // The local handler's play records the new file's samples
    tide_world_tick_events(&world, NULL, &events);
    frame(&audio);
    TIDE_CHECK(audio.voice_count == 1 && audio.voices[0].clip.samples != NULL && audio.voices[0].clip.frames == 10);
    free(wav);
    free(bytes);
    finish();
}
