#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/audio.h"
#include "tide/pack.h"
#include "tide/wav.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_audio_frame audio;

static void begin(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_world_tick(&world);
    static tide_devices devices;
    tide_gui_begin(&gui, &devices, tide_f2(800, 600), NULL);
    tide_local_audio(&local, &audio);
    tide_frame(&world, NULL, 1, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

static void finish(void)
{
    tide_local_free(&local); tide_world_free(&world);
    tide_draw_free(&draw); tide_audio_free(&audio);
    tide_clips_free(); tide_pack_unload();
}

TIDE_TEST(audio_imported_assets_keep_raw_bytes_and_play_lists_maps_aliases)
{
    begin();
    TIDE_CHECK(world.RawProbe.mp3Count == 4223 && world.RawProbe.mp3Header);
    TIDE_CHECK(world.RawProbe.flacCount == 28044 && world.RawProbe.flacHeader);
    TIDE_CHECK(local.Played.count == 2 && local.Played.keyed);
    const tide_clip song = tide_clip_of(local.Played.kept);
    const tide_clip alias = tide_clip_of(local.Played.alias);
    TIDE_REQUIRE(song.samples && alias.samples == song.samples && song.id == alias.id);
    TIDE_CHECK(song.frames == 8820 && song.channels == 2 && song.rate == 44100 && !song.stream);
    const tide_clip precise = tide_clip_of(local.Played.precise);
    TIDE_REQUIRE(precise.samples && precise.frames == 4096 && precise.channels == 2 && precise.rate == 16000);
    for (int i = 0; i < 4096; i++) {
        const int32_t left = ((i * 73) % 4096 - 2048) * 256 + 17;
        const int32_t right = ((i * 131) % 2048 - 1024) * 1024 - 31;
        TIDE_CHECK(precise.samples[i * 2] == (float)left / 8388608.0f);
        TIDE_CHECK(precise.samples[i * 2 + 1] == (float)right / 8388608.0f);
    }
    TIDE_REQUIRE(audio.voice_count == 2);
    TIDE_CHECK(audio.voices[0].clip.frames == 4410 && audio.voices[0].clip.channels == 1 && audio.voices[0].clip.rate == 22050);
    TIDE_CHECK(audio.voices[1].clip.samples == precise.samples);
    const uint8_t *bytes;
    uint32_t size;
    uint64_t version;
    TIDE_REQUIRE(tide_pack_clip_find(local.Played.kept.name, &bytes, &size, &version));
    TIDE_CHECK(size == 58 + 8820 * 8 && !memcmp(bytes, "RIFF", 4) && !memcmp(bytes + 8, "WAVE", 4));
    finish();
}

TIDE_TEST(audio_imported_clip_copies_follow_reload_and_retain_playing_samples)
{
    begin();
    const tide_clip_ref kept = local.Played.kept;
    const tide_clip before = tide_clip_of(kept);
    TIDE_REQUIRE(before.samples && before.frames == 8820);
    const float first = before.samples[100];
    const float stereo[4] = {0.25f, -0.5f, 0.25f, -0.5f};
    size_t wav_size;
    uint8_t *wav = tide_wav_encode(stereo, 2, 8000, &wav_size);
    TIDE_REQUIRE(wav != NULL);
    tide_pack_builder builder = {0};
    const uint8_t hash[32] = {9, 7, 5, 3};
    const uint32_t at = tide_pack_clip(&builder, hash, wav, (uint32_t)wav_size);
    tide_pack_name(&builder, "cbr", TIDE_PACK_CLIP, at, (uint32_t)wav_size, 0);
    size_t size;
    uint8_t *pack = tide_pack_end(&builder, &size);
    TIDE_REQUIRE(tide_assets_load(pack, size));
    tide_clips_prepare(NULL);
    const tide_clip after = tide_clip_of(kept);
    TIDE_REQUIRE(after.samples && after.frames == 2 && after.rate == 8000);
    TIDE_CHECK(after.id != before.id && after.samples != before.samples);
    TIDE_CHECK(before.samples[100] == first);
    free(pack); free(wav);
    finish();
}
