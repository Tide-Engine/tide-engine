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
    gui.width = 1280.0f, gui.height = 720.0f; // Screen.width and Screen.height
    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);

    const tide_render_target original = local.Buffers.surface.target;
    const tide_render_target alias = local.Buffers.alias;
    TIDE_REQUIRE(tide_render_target_managed(original.id));
    TIDE_CHECK(original.id == alias.id && local.Buffers.surface.image.target.id == original.id);
    TIDE_CHECK(tide_render_target_live(original.id));
    const tide_render_target resolved = tide_render_target_resolve(original);
    TIDE_CHECK(resolved.size.width == 32.0f && resolved.size.height == 24.0f && resolved.size.scale == 0.0f);
    TIDE_CHECK(tide_render_target_version(original.id) == 2);
    TIDE_CHECK(local.Buffers.frames == 1);
    // A size in logical pixels, at the display's scale or with pixels per unit of its own
    const tide_render_target exact = tide_render_target_resolve(local.Buffers.exact);
    TIDE_CHECK(exact.size.width == 320.0f && exact.size.height == 180.0f && exact.size.scale == 1.0f);
    const tide_render_target card = tide_render_target_resolve(local.Buffers.card);
    TIDE_CHECK(card.size.width == 320.0f && card.size.height == 90.5f && card.size.scale == 0.0f);
    // ...or the window's, which no size is kept for
    const tide_render_target screen = tide_render_target_resolve(local.Buffers.screen);
    const tide_render_target half = tide_render_target_resolve(local.Buffers.half);
    TIDE_CHECK(tide_render_target_live(screen.id) && screen.size.window == 1.0f && screen.size.width == 0.0f);
    TIDE_CHECK(tide_render_target_live(half.id) && half.size.window == 0.5f && half.id != screen.id);

    tide_frame(&world, NULL, 1.0f, NULL, &local, &draw, &gui);
    TIDE_CHECK(local.Buffers.frames == 2);
    TIDE_CHECK(!tide_render_target_live(original.id) && !tide_render_target_live(alias.id));
    const tide_render_target disposed = tide_render_target_resolve(alias);
    TIDE_CHECK(!tide_target_size_valid(disposed.size));
    // Resize keeps a target's pixels per unit, and leaves one that follows the window as it is
    const tide_render_target smaller = tide_render_target_resolve(local.Buffers.exact);
    TIDE_CHECK(smaller.size.width == 64.0f && smaller.size.height == 32.5f && smaller.size.scale == 1.0f);
    TIDE_CHECK(tide_render_target_version(smaller.id) == 2);
    const tide_render_target followed = tide_render_target_resolve(local.Buffers.half);
    TIDE_CHECK(followed.size.window == 0.5f && tide_render_target_version(followed.id) == 1);

    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&world);
    tide_render_targets_free();
}
