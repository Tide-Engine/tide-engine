#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

TIDE_TEST(render_lifecycle_copies_resize_and_local_handler_disposal)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);

    const tide_render_target original = local.Buffers.surface.target;
    const tide_render_target alias = local.Buffers.alias;
    TIDE_REQUIRE(tide_render_target_managed(original.id));
    TIDE_CHECK(original.id == alias.id && local.Buffers.surface.image.target == original.id);
    TIDE_CHECK(tide_render_target_live(original.id));
    const tide_render_target resolved = tide_render_target_resolve(original);
    TIDE_CHECK(resolved.width == 32 && resolved.height == 24);
    TIDE_CHECK(tide_render_target_version(original.id) == 2);
    TIDE_CHECK(local.Buffers.frames == 1);

    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Buffers.frames == 2);
    TIDE_CHECK(!tide_render_target_live(original.id) && !tide_render_target_live(alias.id));
    const tide_render_target disposed = tide_render_target_resolve(alias);
    TIDE_CHECK(disposed.width == 0 && disposed.height == 0);

    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
    tide_render_targets_free();
}
