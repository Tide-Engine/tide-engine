#include <limits.h>
#include <math.h>

#include "game.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void start(const int scenario)
{
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    world.GIFixture.scenario = scenario;
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 128;
    gui.height = 64;
}

static void frame(void)
{
    const uint64_t hash = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(gui.width, gui.height));
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(tide_world_hash(&world) == hash);
}

static void finish(void)
{
    local.GIProbe.disabled = true;
    frame();
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}

static int shaded_into(const uint64_t id)
{
    uint64_t destination = 0;
    int count = 0;
    for (uint32_t at = 0; at < draw.count; at++) {
        const tide_draw_command *command = &draw.commands[at];
        if (command->kind == TIDE_DRAW_TARGET)
            destination = command->target ? draw.targets[command->target - 1u].id : 0;
        if (command->kind == TIDE_DRAW_SHADED && destination == id) count++;
    }
    return count;
}

static tide_float3 upper(const GlobalIllumination_FieldLayout layout)
{
    return tide_f3(layout.minimum.x + layout.spacing * (layout.counts.x - 1),
                   layout.minimum.y + layout.spacing * (layout.counts.y - 1),
                   layout.minimum.z + layout.spacing * (layout.counts.z - 1));
}

static void covers(const GlobalIllumination_FieldLayout layout, const tide_float3 lo, const tide_float3 hi)
{
    const tide_float3 end = upper(layout);
    TIDE_CHECK(layout.minimum.x < lo.x && layout.minimum.y < lo.y && layout.minimum.z < lo.z);
    TIDE_CHECK(end.x > hi.x && end.y > hi.y && end.z > hi.z);
    TIDE_CHECK(layout.total > 0 && layout.total == layout.counts.x * layout.counts.y * layout.counts.z);
    TIDE_CHECK(layout.total <= local.GIProbe.settings.maxProbes);
    TIDE_CHECK(layout.spacing > 0 && isfinite(layout.spacing));
}

TIDE_TEST(globalillumination_empty_or_disabled_world_has_no_capture_resources)
{
    start(0);
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    TIDE_CHECK(environment->settings.enabled && !environment->ready);
    TIDE_CHECK(!environment->resources.atlas.id && !environment->resources.face0.id);
    TIDE_CHECK(!environment->irradiance.target.id && !environment->distance.target.id);
    finish();

    start(1);
    local.GIProbe.settings.enabled = false;
    frame();
    TIDE_CHECK(!environment->ready && !environment->resources.atlas.id && !environment->resources.face5.id);
    TIDE_CHECK(environment->initialized == 0);
    finish();
}

TIDE_TEST(globalillumination_automatic_bounds_include_receivers_and_cache_shared_assets)
{
    start(1);
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    const GlobalIllumination_WorldBounds *bounds = &local.GlobalIllumination_WorldBounds;
    TIDE_CHECK(environment->ready && bounds->ready && bounds->staticScans == 1);
    covers(environment->layout, tide_f3(1, -3, 0), tide_f3(3, 1, 6));
    frame();
    TIDE_CHECK(bounds->staticScans == 1);
    const int64_t generation = bounds->generation;
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    frame();
    TIDE_CHECK(bounds->generation != generation && bounds->staticScans == 1);
    const GlobalIllumination_FieldLayout first = environment->layout;
    world.GIFixture.offset.x = 0.01f;
    tide_world_tick(&world);
    frame();
    TIDE_CHECK(bounds->staticScans == 1);
    TIDE_CHECK(environment->layout.minimum.x == first.minimum.x && environment->layout.minimum.y == first.minimum.y
        && environment->layout.minimum.z == first.minimum.z && environment->layout.spacing == first.spacing
        && environment->layout.total == first.total);
    world.GIFixture.spawnFar = true;
    tide_world_tick(&world);
    frame();
    TIDE_CHECK(bounds->staticScans == 1); // The new entity uses the same mesh asset.
    covers(environment->layout, tide_f3(1, -3, -12), tide_f3(31, 3, 6));
    world.GIFixture.offset = tide_f3(-80, 12, -40);
    tide_world_tick(&world);
    frame();
    covers(environment->layout, tide_f3(-79, 1, -40), tide_f3(31, 13, -10));
    finish();

    start(3);
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    // These are the actual floor and wall extents from the visual regression.
    // Both exact receiver surfaces must be inside automatic coverage.
    covers(environment->layout, tide_f3(-4.3f, -0.3f, -4), tide_f3(4, 6, 4));
    // The room must retain the requested spatial detail within its budget;
    // excess exterior padding previously doubled spacing and left dark gaps.
    TIDE_CHECK(environment->layout.spacing == local.GIProbe.settings.probeSpacing);
    TIDE_CHECK(bounds->staticScans == 1);
    finish();

    start(4);
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    // The authored ribbon spans [-1,1] in X/Y and zero Z. Its independent
    // rest bound is transformed by the entity's translation/nonuniform scale.
    covers(environment->layout, tide_f3(5, -6, 2), tide_f3(9, 0, 2));
    TIDE_CHECK(bounds->skinnedScans == 1 && bounds->staticScans == 0);
    frame();
    TIDE_CHECK(bounds->skinnedScans == 1);
    finish();
}

TIDE_TEST(globalillumination_update_budget_freeze_invalidation_and_disposal)
{
    start(1);
    local.GIProbe.settings.maxProbes = 512;
    local.GIProbe.settings.updatesPerFrame = 2;
    frame();
    const GlobalIllumination_Environment *environment = &local.GlobalIllumination_Environment;
    TIDE_CHECK(environment->ready && environment->initialized == 2 && environment->cursor == 2);
    const uint64_t atlas = environment->resources.atlas.id;
    TIDE_CHECK(shaded_into(atlas) == 2);
    frame();
    TIDE_CHECK(environment->initialized == 4 && environment->cursor == 4 && environment->resources.atlas.id == atlas);
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    TIDE_CHECK(environment->initialized == 4 && environment->cursor == 4 && shaded_into(atlas) == 0);
    local.GIProbe.settings.intensity = 2;
    frame();
    TIDE_CHECK(environment->initialized == 4 && environment->cursor == 4 && environment->resources.atlas.id == atlas);
    local.GIProbe.settings.updatesPerFrame = 1;
    world.GIFixture.offset.x = 100;
    tide_world_tick(&world);
    frame();
    TIDE_CHECK(environment->initialized == 1 && environment->cursor == 1);
    const uint64_t ids[] = {environment->resources.atlas.id, environment->resources.projection.id,
        environment->resources.partialProjection.id,
        environment->resources.face0.id, environment->resources.face1.id, environment->resources.face2.id,
        environment->resources.face3.id, environment->resources.face4.id, environment->resources.face5.id};
    local.GIProbe.settings.enabled = false;
    frame();
    TIDE_CHECK(!environment->ready && !environment->resources.atlas.id);
    TIDE_CHECK(!environment->irradiance.target.id && !environment->distance.target.id);
    for (unsigned at = 0; at < sizeof ids / sizeof ids[0]; at++) TIDE_CHECK(!tide_render_target_live(ids[at]));
    TIDE_CHECK(local.Render3D_Renderer.ready);
    local.GIProbe.settings.enabled = true;
    frame();
    TIDE_CHECK(environment->ready && environment->initialized == 1);
    const uint64_t renewed = environment->resources.atlas.id;
    local.GIProbe.dispose = true;
    frame();
    TIDE_CHECK(environment->ready && environment->initialized == 1 && !tide_render_target_live(renewed));
    local.GIProbe.dispose = false;
    local.GIProbe.disabled = true;
    frame();
    TIDE_CHECK(!environment->ready && !local.Render3D_Renderer.ready);
    finish();
}

TIDE_TEST(globalillumination_settings_and_probe_count_remain_bounded)
{
    start(1);
    local.GIProbe.raw.probeSpacing = NAN;
    local.GIProbe.raw.maxProbes = INT_MIN;
    local.GIProbe.raw.updatesPerFrame = INT_MAX;
    local.GIProbe.raw.intensity = INFINITY;
    local.GIProbe.settings.maxProbes = 125;
    local.GIProbe.settings.probeSpacing = 0.01f;
    local.GIProbe.settings.updatesPerFrame = 0;
    frame();
    const GlobalIllumination_Settings bounded = local.GIProbe.bounded;
    TIDE_CHECK(isfinite(bounded.probeSpacing) && bounded.probeSpacing > 0);
    TIDE_CHECK(bounded.maxProbes >= 125 && bounded.maxProbes <= 2048);
    TIDE_CHECK(bounded.updatesPerFrame == 16);
    TIDE_CHECK(isfinite(bounded.intensity) && bounded.intensity >= 0 && bounded.intensity <= 16);
    TIDE_CHECK(local.GlobalIllumination_Environment.layout.total <= 125);
    local.GIProbe.raw.maxProbes = INT_MAX;
    local.GIProbe.raw.updatesPerFrame = INT_MIN;
    local.GIProbe.raw.intensity = NAN;
    frame();
    TIDE_CHECK(local.GIProbe.bounded.maxProbes == 2048);
    TIDE_CHECK(local.GIProbe.bounded.updatesPerFrame == 0 && local.GIProbe.bounded.intensity == 0);
    local.GIProbe.settings.maxProbes = 2048;
    frame();
    TIDE_CHECK(local.GlobalIllumination_Environment.resources.atlas.size.width <= 2048);
    TIDE_CHECK(local.GlobalIllumination_Environment.resources.atlas.size.height <= 2048);
    finish();
}
