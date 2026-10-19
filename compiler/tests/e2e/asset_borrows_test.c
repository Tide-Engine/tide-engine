#include "game.h"
#include "tide/audio.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world, kept;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_audio_frame audio;

int32_t ScratchMark(void) { return (int32_t)tide_scratch_mark(); }

TIDE_TEST(asset_collections_are_borrowed_without_copying_or_retaining_the_pack)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    for (int frame = 1; frame <= 3; frame++) {
        if (frame == 2) TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
        tide_world_copy(&kept, &world);
        const uint64_t before = tide_world_hash(&kept);
        tide_world_tick(&world);
        tide_audio_reset(&audio);
        tide_draw_reset(&draw);
        tide_local_audio(&local, &audio);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        TIDE_CHECK(world.Geometry.count == 2 && world.Geometry.vertices > 0);
        TIDE_CHECK(local.Seen.clips == 4 && local.Seen.frames == 6 && local.Seen.entries == 7);
        TIDE_CHECK(local.Seen.vertices == world.Geometry.vertices && local.Seen.misses == frame);
        TIDE_CHECK(local.Seen.order == frame && local.Seen.found && local.Seen.kept.name != 0);
        TIDE_CHECK(local.Seen.before == local.Seen.inside && local.Seen.action == 2);
        TIDE_CHECK(audio.voice_count == 2);
        TIDE_CHECK(tide_world_hash(&kept) == before);
        TIDE_CHECK(tide_scratch_mark() == 0);
    }
    tide_local_free(&local);
    tide_world_free(&world);
    tide_world_free(&kept);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_clips_free();
    tide_pack_unload();
}
