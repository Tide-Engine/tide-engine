#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/mesh.h"
#include "tide/pack.h"
#include "tide/layout.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
extern const tide_layout tide_game_layout;
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}

TIDE_TEST(linear_fields_capture_grids_and_keep_their_role_across_asset_replacement_and_local_reload)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    frame(); frame();
    const tide_texture_ref retained = local.Art.kept;
    uint32_t marked = 0;
    for (uint32_t i = 0; i < draw.texture_count; i++) {
        const tide_draw_texture *t = &draw.textures[i];
        if (!t->linear) continue;
        marked++;
        if (t->width == 2 && t->height == 1 && t->pixels) {
            const uint8_t expected = (uint8_t)(tide_todisplay_f(tide_tolinear_f(0.5f) * 0.5f) * 255.0f + 0.5f);
            TIDE_CHECK(t->pixels[0] == expected && t->pixels[3] == 128u);
            TIDE_CHECK(t->pixels[4] == 0 && t->pixels[7] == 0);
        }
    }
    TIDE_CHECK(marked == 2); // The asset and its kept aliases share one image; the grid is another.
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    void *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_local_pack(&local, bytes, size) == size);
    tide_local restored = {0};
    TIDE_REQUIRE(tide_local_unpack(&restored, bytes, size));
    TIDE_CHECK(memcmp(&restored.Art.kept, &retained, sizeof retained) == 0);
    tide_local_free(&restored);
    free(bytes);
    // A different pack under the same declaration is visible through the field retained before reload.
    tide_pack_builder builder = {0};
    const uint8_t rgba[] = {0, 255, 0, 255, 0, 0, 255, 255};
    const uint8_t content[32] = {7};
    const uint64_t at = tide_pack_texture(&builder, content, 2, 1, rgba);
    tide_pack_name(&builder, "image", TIDE_PACK_TEXTURE, at, 0, TIDE_PACK_LINEAR); // Packed for the mark, as its code reads it
    size_t pack_size;
    uint8_t *pack = tide_pack_end(&builder, &pack_size);
    TIDE_REQUIRE(pack != NULL);
    tide_draw_forget(&draw);
    tide_pack_unload();
    TIDE_REQUIRE(tide_assets_load(pack, pack_size));
    free(pack);
    frame();
    TIDE_CHECK(memcmp(&local.Art.kept, &retained, sizeof retained) == 0);
    const uint32_t slot = tide_draw_texture_source_linear(&draw, local.Art.kept);
    TIDE_REQUIRE(slot && slot <= draw.texture_count);
    const tide_draw_texture *changed = &draw.textures[slot - 1u];
    TIDE_CHECK(changed->linear && changed->width == 2 && changed->height == 1);
    TIDE_REQUIRE(changed->pixels != NULL);
    TIDE_CHECK(changed->pixels[1] == 255u && changed->pixels[6] == 255u);
    // The pack says which reading its image is stored for, and the code that isn't built for it draws none of it
    bool packed = false;
    TIDE_CHECK(tide_pack_image_linear(local.Art.kept.image.version & TIDE_TEXTURE_NAME, &packed) && packed);
    TIDE_CHECK(tide_draw_texture_source(&draw, local.Art.kept) == 0);
    TIDE_CHECK(!tide_pack_image_linear(12345u, &packed) && !packed);
    // ...as a pack whose image was packed without the mark isn't drawn by code that reads it with it: its soft
    // pixels are the other encoding's. Without the mark it's the image it is
    tide_pack_builder plain_builder = {0};
    const uint64_t plain_at = tide_pack_texture(&plain_builder, content, 2, 1, rgba);
    tide_pack_name(&plain_builder, "image", TIDE_PACK_TEXTURE, plain_at, 0, 0);
    size_t plain_size;
    uint8_t *plain = tide_pack_end(&plain_builder, &plain_size);
    TIDE_REQUIRE(plain != NULL);
    tide_draw_forget(&draw);
    tide_pack_unload();
    TIDE_REQUIRE(tide_assets_load(plain, plain_size));
    free(plain);
    tide_draw_reset(&draw);
    TIDE_CHECK(tide_pack_image_linear(local.Art.kept.image.version & TIDE_TEXTURE_NAME, &packed) && !packed);
    TIDE_CHECK(tide_draw_texture_source_linear(&draw, local.Art.kept) == 0);
    const uint32_t unmarked = tide_draw_texture_source(&draw, local.Art.kept);
    TIDE_REQUIRE(unmarked && !draw.textures[unmarked - 1u].linear && draw.textures[unmarked - 1u].pixels[1] == 255u);
    frame(); // The game's own frame over that pack: its marked reads draw nothing, and nothing is read out of place
    uint32_t roles = 0;
    for (uint32_t i = 0; i < tide_game_layout.type_count; i++) {
        const tide_layout_type *t = &tide_game_layout.types[i];
        if (strcmp(t->name, "Art") != 0) continue;
        for (uint32_t j = 0; j < t->field_count; j++)
            if (strcmp(t->fields[j].type, "[Linear] Texture") == 0) roles++;
    }
    TIDE_CHECK(roles == 3);
    tide_draw_free(&draw); draw = (tide_draw_list){0};
    tide_local_free(&local);
    tide_world_free(&world);
    tide_pack_unload();
}
