#include "game.h"
#include "tide/mesh.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_int2 pixels(const tide_target_size size)
{
    return tide_i2((int32_t)(size.width * size.scale + 0.5f), (int32_t)(size.height * size.scale + 0.5f));
}

TIDE_TEST(texture_size_metadata_does_not_capture_pixels_and_follows_lifetimes)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world w = {0}; tide_local local = {0}; tide_draw_list draw = {0}; tide_gui gui = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    tide_world_tick(&w);
    tide_local_init(&local);
    local.Sizes.target = tide_render_target_make_scaled(10, 6, 2);
    local.Sizes.alias = tide_render_target_color(local.Sizes.target);
    local.Sizes.checkOrder = true;
    draw.target_pixels = pixels;
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Sizes.assetSize.x == 8 && local.Sizes.assetSize.y == 6);
    TIDE_CHECK(local.Sizes.rawSize.x == 8 && local.Sizes.rawSize.y == 6);
    TIDE_CHECK(local.Sizes.gridSize.x == 3 && local.Sizes.gridSize.y == 5);
    TIDE_CHECK(local.Sizes.helperSize.x == 3 && local.Sizes.helperSize.y == 5);
    TIDE_CHECK(local.Sizes.targetSize.x == 20 && local.Sizes.targetSize.y == 12);
    TIDE_CHECK(local.Sizes.directOrder == 20 && local.Sizes.helperOrder == 20);
    TIDE_CHECK(*(const int32_t *)tide_grid_read(&local.heap, local.Sizes.copies, 1, 1, 0, &tide_shape_Grid2_int) == 20);
    TIDE_CHECK(local.Sizes.noneSize.x == 0 && local.Sizes.noneSize.y == 0);
    TIDE_CHECK(draw.texture_count == 0);
    TIDE_CHECK(tide_texture_target_pixels(NULL) == NULL); // The frame restored its caller's context.
    tide_render_target_resize(local.Sizes.target, 7, 4);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Sizes.targetSize.x == 14 && local.Sizes.targetSize.y == 8);
    tide_render_target_dispose(local.Sizes.target);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Sizes.targetSize.x == 0 && local.Sizes.targetSize.y == 0);
    tide_pack_unload();
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Sizes.assetSize.x == 0 && local.Sizes.rawSize.y == 0);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Sizes.assetSize.x == 8 && local.Sizes.rawSize.y == 6);
    tide_draw_free(&draw); tide_local_free(&local); tide_world_free(&w);
    tide_render_targets_free(); tide_pack_unload();
}
