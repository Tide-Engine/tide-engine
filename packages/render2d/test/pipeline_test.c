#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

#ifdef TIDE_HAS_ASSETS
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
#endif

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void start(RenderCase scenario)
{
#ifdef TIDE_HAS_ASSETS
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
#endif
    tide_world_init(&world, 1.0f / 60.0f);
    world.Fixture.scenario = scenario;
    tide_world_tick(&world);
    tide_local_init(&local);
    gui.width = 128;
    gui.height = 96;
}

static void record(void)
{
    const uint64_t hash = tide_world_hash(&world);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, tide_f2(gui.width, gui.height));
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(tide_world_hash(&world) == hash);
}

static void finish(void)
{
    local.Probe.disabled = true;
    record();
    // The same list survives across tests. Invalidate renderer target caches
    // before releasing target bookkeeping, preserving its increasing epoch.
    tide_draw_forget(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}

static void near1(float actual, double expected)
{
    TIDE_CHECK(isfinite(actual));
    TIDE_CHECK(fabs((double)actual - expected) <= 0.000003 * fmax(1, fabs(expected)));
}

TIDE_TEST(render2d_color_and_bounded_light_math)
{
    start(RenderCase_Dark);
    record();
    const float *a = local.Probe.attenuation.at;
    near1(a[0], 10000);
    near1(a[1], 225.0 / 1024);
    near1(a[2], 0);
    near1(a[3], 0);
    near1(a[4], 0.5);
    near1(a[5], 0);
    near1(a[6], 1);
    near1(a[7], 1);
    const float *c = local.Probe.transfer.at;
    near1(c[0], 0.21404114048223255);
    near1(c[1], 0.04045 / 12.92);
    near1(c[2], 0.0031308 * 12.92);
    near1(c[3], 0.5);
    near1(c[4], 0);
    const tide_float3 *mapped = local.Probe.mapped.at;
    near1(mapped[0].x, 0); near1(mapped[0].y, 0.5); near1(mapped[0].z, 0.8);
    near1(mapped[1].x, 0); near1(mapped[1].y, 0); near1(mapped[1].z, 0);
    near1(mapped[2].x, 0); near1(mapped[2].y, 2.0 / 3); near1(mapped[2].z, 8.0 / 9);
    finish();
}

static int presentations(void)
{
    uint64_t destination = 0;
    int found = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *command = &draw.commands[i];
        if (command->kind == TIDE_DRAW_TARGET)
            destination = command->target ? draw.targets[command->target - 1].id : 0;
        if (command->kind != TIDE_DRAW_BLIT) continue;
        TIDE_CHECK(command->shaded.run < draw.run_count);
        if (command->shaded.run >= draw.run_count) continue;
        const tide_draw_run *run = &draw.runs[command->shaded.run];
        if (!destination) found++;
        // Every recorded resource read must be distinct from the image being
        // written; compare the whole MRT target, not merely attachment bits.
        for (uint32_t j = 0; j < run->shader->texture_count; j++) {
            TIDE_CHECK(run->textures[j] <= draw.texture_count);
            if (!run->textures[j] || run->textures[j] > draw.texture_count) continue;
            const tide_draw_texture *texture = &draw.textures[run->textures[j] - 1];
            if (texture->space == TIDE_PIXELS_TARGET)
                TIDE_CHECK(tide_render_target_whole(texture->id) != destination);
        }
    }
    return found;
}

TIDE_TEST(render2d_passes_retain_release_and_recreate_targets)
{
    start(RenderCase_Shadows);
    record();
    TIDE_REQUIRE(local.Render2D_Renderer.ready);
    const uint64_t hdr = local.Render2D_Renderer.hdr.id;
    const uint64_t shadow = local.Render2D_Renderer.shadowMap.id;
    TIDE_REQUIRE(hdr && shadow && hdr != shadow);
    TIDE_CHECK(presentations() == 1);
    record();
    TIDE_CHECK(local.Render2D_Renderer.hdr.id == hdr);
    TIDE_CHECK(local.Render2D_Renderer.shadowMap.id == shadow);
    local.Probe.gi = true;
    local.Probe.bloom = true;
    record();
    TIDE_CHECK(presentations() == 1);
    TIDE_CHECK(local.Render2D_Renderer.hdr.id == hdr);
    TIDE_CHECK(local.Render2D_PostState.ready);
    const uint64_t bloom = local.Render2D_PostState.composite.id;
    local.Probe.present = false;
    record();
    TIDE_CHECK(presentations() == 0);
    TIDE_CHECK(draw.run_count > 0);
    local.Probe.disabled = true;
    record();
    TIDE_CHECK(!local.Render2D_Renderer.ready);
    TIDE_CHECK(!local.Render2D_PostState.ready);
    TIDE_CHECK(draw.run_count == 0);
    TIDE_CHECK(!tide_render_target_live(hdr) && !tide_render_target_live(shadow));
    TIDE_CHECK(!tide_render_target_live(bloom));
    local.Probe.disabled = false;
    local.Probe.present = true;
    record();
    TIDE_CHECK(local.Render2D_Renderer.ready && local.Render2D_Renderer.hdr.id != hdr);
    TIDE_CHECK(local.Render2D_Renderer.shadowMap.id != shadow);
    TIDE_CHECK(presentations() == 1);
    finish();
}

TIDE_TEST(render2d_light_and_shadow_budgets_are_independent)
{
    start(RenderCase_Shadows);
    record();
    TIDE_CHECK(tide_list_count(local.Render2D_Renderer.lights) == 1);
    TIDE_CHECK(tide_list_count(local.Render2D_Renderer.shadowViews) == 1);
    local.Probe.shadowLimit = 0;
    record();
    TIDE_CHECK(tide_list_count(local.Render2D_Renderer.lights) == 1);
    TIDE_CHECK(tide_list_count(local.Render2D_Renderer.shadowViews) == 0);
    local.Probe.lightLimit = 0;
    record();
    TIDE_CHECK(tide_list_count(local.Render2D_Renderer.lights) == 0);
    TIDE_CHECK(tide_list_count(local.Render2D_Renderer.shadowViews) == 0);
    local.Probe.lightLimit = -100;
    local.Probe.shadowLimit = -100;
    local.Probe.shadowResolution = -1;
    record();
    TIDE_CHECK(tide_list_count(local.Render2D_Renderer.lights) == 0);
    TIDE_CHECK(tide_list_count(local.Render2D_Renderer.shadowViews) == 0);
    finish();
}

TIDE_TEST(render2d_radiance_intervals_conserve_visibility_and_partition_distance)
{
    start(RenderCase_Dark);
    record();
    const tide_float4 *intervals = local.Probe.intervals.at;
    near1(intervals[0].x, 1); near1(intervals[0].y, 2); near1(intervals[0].z, 3); near1(intervals[0].w, 0);
    near1(intervals[1].x, 4); near1(intervals[1].y, 5); near1(intervals[1].z, 6); near1(intervals[1].w, 0.5);
    near1(intervals[2].x, 2); near1(intervals[2].y, 4); near1(intervals[2].z, 6); near1(intervals[2].w, 0.125);
    near1(intervals[3].x, 1); near1(intervals[3].y, 2); near1(intervals[3].z, 3); near1(intervals[3].w, 1);
    const float *ranges = local.Probe.ranges.at;
    near1(ranges[0], 0);
    near1(ranges[7], 8);
    for (int i = 0; i < 4; i++) {
        TIDE_CHECK(ranges[i * 2 + 1] > ranges[i * 2]);
        if (i) near1(ranges[i * 2], ranges[i * 2 - 1]);
    }
    const tide_float3 *directions = local.Probe.directions.at;
    near1(directions[0].x, 0); near1(directions[0].y, 0); near1(directions[0].z, 1);
    near1(directions[1].x, 1); near1(directions[1].y, 0); near1(directions[1].z, 0);
    near1(directions[2].x, 0); near1(directions[2].y, 0); near1(directions[2].z, -1);
    finish();
}
