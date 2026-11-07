#include <stdlib.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
TIDE_TEST(resource_collections_copy_store_play_and_rebind)
{
    static tide_world world;
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    static tide_audio_frame audio;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    const uint64_t hash = tide_world_hash(&world);
    for (int i = 0; i < 3; i++) {
        tide_audio_reset(&audio);
        tide_local_audio(&local, &audio);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        TIDE_CHECK(audio.voice_count == 5);
        for (uint32_t a = 0; a < audio.voice_count; a++)
            for (uint32_t b = a + 1; b < audio.voice_count; b++)
                TIDE_CHECK(audio.voices[a].id != audio.voices[b].id);
        TIDE_CHECK(local.Saved.pair.at[0].name != 0);
        TIDE_CHECK(local.Saved.pair.at[0].name == local.Saved.pair.at[1].name);
        TIDE_CHECK(audio.voices[0].id != audio.voices[1].id);
        TIDE_CHECK(audio.voices[1].id != audio.voices[2].id);
        TIDE_CHECK(local.Saved.checks == i + 1);
        TIDE_CHECK(tide_scratch_mark() == 0);
        tide_local_rebind_resources(&local, true);
        TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    }
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    TIDE_REQUIRE(tide_local_pack(&local, bytes, size) == size);
    tide_local_free(&local);
    tide_local_init(&local);
    TIDE_REQUIRE(tide_local_unpack(&local, bytes, size));
    free(bytes);
    // Rebinding after a different heap was active must find this local heap.
    tide_text_use(NULL, &local.heap);
    tide_texture_ref *texture = tide_list_at_mut(local.Saved.images, 0, sizeof *texture);
    TIDE_REQUIRE(texture != NULL);
    texture->image.pixels = (const uint8_t *)&size;
    local.Saved.pictures.at[0].image.pixels = (const uint8_t *)&size;
    tide_text_use(NULL, NULL);
    tide_local_rebind_resources(&local, false);
    texture = tide_list_at_mut(local.Saved.images, 0, sizeof *texture);
    TIDE_REQUIRE(texture != NULL);
    TIDE_CHECK(texture->image.pixels == NULL);
    TIDE_CHECK(local.Saved.pictures.at[0].image.pixels == NULL);
    TIDE_CHECK(local.Saved.pair.at[0].name != 0);
    TIDE_CHECK(tide_world_hash(&world) == hash);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_audio_free(&audio);
    tide_clips_free();
    tide_pack_unload();
}
