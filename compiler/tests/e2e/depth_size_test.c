#include "game.h"
#include "tide/mesh.h"
#include "tide_test.h"

static tide_int2 pixels(const tide_target_size size)
{
    const float width = size.width * size.scale, height = size.height * size.scale;
    const float longest = width > height ? width : height;
    const float shrink = longest > 2048 ? 2048 / longest : 1;
    return tide_i2((int32_t)(width * shrink + 0.5f), (int32_t)(height * shrink + 0.5f));
}

TIDE_TEST(depth_size_metadata_preserves_lifetimes_order_and_device_limits)
{
    tide_world w = {0}; tide_local local = {0}; tide_draw_list draw = {0}; tide_gui gui = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    tide_world_tick(&w);
    tide_local_init(&local);
    const tide_render_target color = tide_render_target_make_scaled(3, 5, 2);
    const tide_cubemap_ref cube = tide_cubemap_make(8);
    local.Sizes.target = tide_render_target_make_scaled_as(10, 6, 2, TIDE_TARGET_DEPTH);
    local.Sizes.depth = tide_render_target_depth(local.Sizes.target);
    local.Sizes.colorDepth = tide_render_target_depth(color);
    local.Sizes.window = tide_render_target_depth((tide_render_target){.id = TIDE_RENDER_WINDOW});
    local.Sizes.cube = tide_render_target_depth(tide_cubemap_face(cube, 0));
    local.Sizes.checkOrder = true;
    draw.target_pixels = pixels;
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Sizes.depthSize.x == 20 && local.Sizes.depthSize.y == 12);
    TIDE_CHECK(local.Sizes.colorSize.x == 6 && local.Sizes.colorSize.y == 10);
    TIDE_CHECK(local.Sizes.directOrder == 20 && local.Sizes.helperOrder == 20);
    TIDE_CHECK(*(const int32_t *)tide_grid_read(&local.heap, local.Sizes.copies, 1, 1, 0, &tide_shape_Grid2_int) == 20);
    TIDE_CHECK(local.Sizes.missingSize.x == 0 && local.Sizes.missingSize.y == 0);
    TIDE_CHECK(local.Sizes.windowSize.x == 0 && local.Sizes.windowSize.y == 0);
    TIDE_CHECK(local.Sizes.cubeSize.x == 0 && local.Sizes.cubeSize.y == 0);
    TIDE_CHECK(draw.texture_count == 0);
    TIDE_REQUIRE(draw.run_count > 0);
    TIDE_CHECK(draw.runs[draw.run_count - 1].shader->texture_count == 0);
    TIDE_CHECK(tide_texture_target_pixels(NULL) == NULL);
    tide_render_target_resize(local.Sizes.target, 4096, 2048);
    tide_draw_reset(&draw);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Sizes.depthSize.x == 2048 && local.Sizes.depthSize.y == 1024);
    tide_render_target_dispose(local.Sizes.target);
    tide_render_target_dispose(color);
    tide_draw_reset(&draw);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Sizes.depthSize.x == 0 && local.Sizes.depthSize.y == 0);
    TIDE_CHECK(local.Sizes.colorSize.x == 0 && local.Sizes.colorSize.y == 0);
    local.Sizes.target = tide_render_target_make_scaled_as(4, 3, 1, TIDE_TARGET_DEPTH);
    local.Sizes.depth = tide_render_target_depth(local.Sizes.target);
    draw.target_pixels = NULL;
    tide_draw_reset(&draw);
    tide_frame(&w, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Sizes.depthSize.x == 0 && local.Sizes.depthSize.y == 0);
    tide_cubemap_dispose(cube);
    tide_draw_free(&draw); tide_local_free(&local); tide_world_free(&w);
    tide_render_targets_free();
}
