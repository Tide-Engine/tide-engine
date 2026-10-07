#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/audio.h"
#include "tide/pack.h"
#include "tide_test.h"

// Sound fields (sound.tide): what the frame names to the audio list for the
// Sounds of local state that play, and the listener a view sets.

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_audio_frame audio;

static void frame(tide_audio_frame *into)
{
    tide_audio_reset(&audio);
    tide_draw_reset(&draw);
    static tide_devices devices;
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL);
    tide_local_audio(&local, into);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_world_tick(&world); // Main spawns the bodies
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_clips_free();
    tide_pack_unload();
}

static const tide_voice *voice_with_pitch(const float pitch)
{
    for (uint32_t i = 0; i < audio.voice_count; i++)
        if (audio.voices[i].pitch == pitch) return &audio.voices[i];
    return NULL;
}

static bool near(const float a, const float b) { return a - b < 1e-5f && b - a < 1e-5f; }

TIDE_TEST(sound_a_match_entitys_local_sound_is_named_each_frame_by_its_entity)
{
    start();
    frame(&audio);
    TIDE_REQUIRE(audio.voice_count == 2);
    const tide_voice *a = voice_with_pitch(0.9f), *b = voice_with_pitch(1.2f);
    TIDE_REQUIRE(a && b);
    TIDE_CHECK(a->id != 0 && b->id != 0 && a->id != b->id);
    TIDE_CHECK(a->spatial && a->loop && a->playing && a->volume == 1.0f);
    TIDE_CHECK(a->position.x == 4.0f && a->position.y == 5.0f && a->position.z == 6.0f);
    TIDE_CHECK(b->position.x == 7.0f && b->position.y == 8.0f && b->position.z == 9.0f);
    TIDE_CHECK(a->clip.frames == 40 && a->clip.samples != NULL && a->clip.name != 0); // hum, found in the pack
    const uint64_t id_a = a->id, id_b = b->id;
    frame(&audio); // The same ids again: the mixer keeps the voices going
    TIDE_REQUIRE(audio.voice_count == 2);
    TIDE_CHECK(voice_with_pitch(0.9f)->id == id_a && voice_with_pitch(1.2f)->id == id_b);
    TIDE_CHECK(!audio.listener_set && audio.falloff_near == 0.0f);
    finish();
}

TIDE_TEST(sound_local_singleton_and_struct_sounds_play_while_set_so)
{
    start();
    const tide_clip_ref buzz = tide_pack_clip_of(tide_pack_find("buzz", TIDE_PACK_CLIP));
    TIDE_CHECK(local.Music.track.volume == 1.0f && local.Music.track.pitch == 1.0f); // The struct's defaults
    TIDE_CHECK(local.Music.pair.right.volume == 1.0f);
    local.Music.track.clip = buzz;
    local.Music.track.playing = true;
    local.Music.track.volume = 0.5f;
    local.Music.pair.left.clip = buzz;
    local.Music.pair.left.playing = true;
    local.Music.pair.left.pitch = 2.0f;
    local.Music.pair.right.clip = buzz; // Not playing: not named
    frame(&audio);
    TIDE_REQUIRE(audio.voice_count == 4);
    const tide_voice *track = NULL, *left = NULL;
    for (uint32_t i = 0; i < audio.voice_count; i++) {
        if (audio.voices[i].volume == 0.5f) track = &audio.voices[i];
        if (audio.voices[i].pitch == 2.0f) left = &audio.voices[i];
    }
    TIDE_REQUIRE(track && left);
    TIDE_CHECK(track->id != left->id && track->id != 0 && left->id != 0);
    TIDE_CHECK(!track->spatial && track->clip.frames == 3 && track->clip.name == buzz.name);
    const uint64_t id_track = track->id;
    local.Music.track.playing = false;
    frame(&audio);
    TIDE_CHECK(audio.voice_count == 3); // The track is no longer named: the mixer fades it
    local.Music.track.playing = true;
    frame(&audio);
    TIDE_REQUIRE(audio.voice_count == 4);
    bool again = false;
    for (uint32_t i = 0; i < audio.voice_count; i++) again |= audio.voices[i].id == id_track;
    TIDE_CHECK(again); // The same field is the same voice
    finish();
}

TIDE_TEST(sound_a_local_entitys_sound_and_the_listener)
{
    start();
    local.Music.spark = true;
    local.Music.place = true;
    frame(&audio);
    TIDE_REQUIRE(audio.voice_count == 3); // Two engines and the spark, spawned and named in one frame
    const tide_voice *spark = NULL;
    for (uint32_t i = 0; i < audio.voice_count; i++)
        if (audio.voices[i].pitch == 1.0f) spark = &audio.voices[i];
    TIDE_REQUIRE(spark != NULL);
    TIDE_CHECK(spark->spatial && spark->position.y == 1.0f && spark->clip.frames == 3);
    TIDE_CHECK(audio.listener_set && audio.listener_position.x == 1.0f && audio.listener_position.z == 3.0f);
    TIDE_CHECK(near(audio.listener_rotation.value.w, 1.0f));
    TIDE_CHECK(audio.falloff_near == 2.0f && audio.falloff_far == 20.0f);
    local.Music.place = false;
    frame(&audio);
    TIDE_CHECK(!audio.listener_set && audio.falloff_far == 0.0f); // A frame's setting: not said, not set
    TIDE_CHECK(audio.voice_count == 3);
    frame(NULL); // No audio list: nothing recorded, nothing broken
    TIDE_CHECK(audio.voice_count == 0);
    finish();
}
