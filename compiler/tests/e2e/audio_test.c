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
    // One file where the type holds many: a list of one, and a map of one
    const tide_pack_entry *shot = tide_pack_find("shot", TIDE_PACK_LIST);
    TIDE_REQUIRE(shot != NULL);
    TIDE_CHECK(shot->extra == TIDE_PACK_CLIP && shot->size == 1);
    const tide_pack_entry *by_name = tide_pack_find("shotByName", TIDE_PACK_CLIPS);
    TIDE_CHECK(by_name != NULL && by_name->extra == 1);
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
    // A list and a map of one file each: the list's one is a.wav, as hits[0] is, and the map's is b.wav by its path
    local.Sfx.plays = 3;
    frame(&audio);
    TIDE_CHECK(local.Sfx.shots == 1 && local.Sfx.keyed);
    TIDE_REQUIRE(audio.voice_count == 3);
    const tide_voice *first = &audio.voices[0], *second = &audio.voices[1];
    TIDE_CHECK(first->clip.frames == 4 && first->clip.channels == 2 && first->clip.rate == 22050 && first->clip.samples != NULL);
    TIDE_CHECK(second->clip.frames == 3 && second->clip.channels == 1 && second->clip.rate == 48000 && second->clip.samples != NULL);
    TIDE_CHECK(audio.voices[2].clip.samples == NULL && audio.voices[2].clip.id == 0); // Past the list's one: no clip
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

TIDE_TEST(audio_an_ogg_vorbis_clip_decodes_whole_or_streams_by_its_length)
{
    start();
    const tide_clip_ref blip = tide_pack_clip_of(tide_pack_find("blip", TIDE_PACK_CLIP));
    TIDE_CHECK(blip.name != 0);
    // 0.3 s of a tone at 8 kHz, mono: well under the ten seconds a clip decodes whole at
    tide_clip clip = tide_clip_of(blip);
    TIDE_REQUIRE(clip.samples != NULL && clip.stream == NULL);
    TIDE_CHECK(clip.frames == 2400 && clip.channels == 1 && clip.rate == 8000);
    float loudest = 0.0f;
    for (uint32_t i = 0; i < clip.frames; i++) {
        const float v = clip.samples[i] < 0 ? -clip.samples[i] : clip.samples[i];
        if (v > loudest) loudest = v;
    }
    TIDE_CHECK(loudest > 0.05f && loudest <= 1.0f);
    TIDE_CHECK(tide_clip_of(blip).samples == clip.samples); // Decoded once
    // Longer than the threshold, it's kept to stream: the same numbers, no samples
    tide_clips_free();
    tide_clips_whole_seconds = 0;
    clip = tide_clip_of(blip);
    TIDE_CHECK(clip.samples == NULL && clip.stream != NULL);
    TIDE_CHECK(clip.frames == 2400 && clip.channels == 1 && clip.rate == 8000 && clip.id != 0);
    TIDE_CHECK(tide_clip_of(blip).stream == clip.stream);
    tide_clips_whole_seconds = 10;
    // A WAV is never streamed
    const tide_clip beep = tide_clip_of(tide_pack_clip_of(tide_pack_find("beep", TIDE_PACK_CLIP)));
    TIDE_CHECK(beep.samples != NULL && beep.stream == NULL);
    // Prepared at load: every clip decoded before any plays, and the same entries after
    tide_clips_free();
    tide_clips_prepare(NULL);
    const tide_clip prepared = tide_clip_of(blip);
    TIDE_CHECK(prepared.samples != NULL && prepared.frames == 2400);
    tide_clips_prepare(NULL); // Nothing left to do
    TIDE_CHECK(tide_clip_of(blip).samples == prepared.samples);
    finish();
}
