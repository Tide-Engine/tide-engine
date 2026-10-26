#include <limits.h>
#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

typedef struct post_blit {
    uint64_t destination;
    const tide_draw_run *run;
} post_blit;

static void start(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.PostProbe.special = tide_f3(INFINITY, NAN, -INFINITY);
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
    local.PostProbe.disabled = true;
    frame();
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
}

static void near1(const float actual, const double expected)
{
    TIDE_CHECK(isfinite(actual) && fabs((double)actual - expected) <= 0.000003 * fmax(1, fabs(expected)));
}

static void near3(const tide_float3 actual, const double x, const double y, const double z)
{
    near1(actual.x, x);
    near1(actual.y, y);
    near1(actual.z, z);
}

static int blits(post_blit found[32])
{
    uint64_t destination = 0;
    int count = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *command = &draw.commands[i];
        if (command->kind == TIDE_DRAW_TARGET)
            destination = command->target ? draw.targets[command->target - 1u].id : 0;
        if (command->kind != TIDE_DRAW_BLIT) continue;
        if (count == 32) {
            TIDE_CHECK(count < 32);
            return count;
        }
        const tide_draw_run *run = &draw.runs[command->shaded.run];
        found[count++] = (post_blit){destination, run};
        // Every sampled target, including a secondary singleton image, must
        // differ from this destination. This witnesses package routing only;
        // low-level self-sampling rejection belongs to the engine's tests.
        for (uint32_t texture = 0; texture < run->shader->texture_count; texture++) {
            if (!run->textures[texture]) continue;
            const tide_draw_texture *source = &draw.textures[run->textures[texture] - 1u];
            if (source->space == TIDE_PIXELS_TARGET)
                TIDE_CHECK(tide_render_target_whole(source->id) != destination);
            TIDE_CHECK(tide_sampling_filter(run->sampling, texture) == TIDE_FILTER_BILINEAR);
            TIDE_CHECK(tide_sampling_wrap(run->sampling, texture) == TIDE_WRAP_CLAMP);
        }
    }
    return count;
}

static uint64_t source_id(const post_blit blit, const uint32_t index)
{
    if (index >= blit.run->shader->texture_count || !blit.run->textures[index]) return 0;
    const tide_draw_texture *texture = &draw.textures[blit.run->textures[index] - 1u];
    return texture->space == TIDE_PIXELS_TARGET ? tide_render_target_whole(texture->id) : 0;
}

static float uniform_float(const post_blit blit, const uint32_t word)
{
    float value;
    memcpy(&value, draw.uniforms + blit.run->uniforms + word * sizeof value, sizeof value);
    return value;
}

TIDE_TEST(render3d_post_independent_bright_gaussian_and_hdr_math)
{
    start();
    frame();
    const PostProbe *probe = &local.PostProbe;
    // Independent analytic landmarks of the quadratic knee at [0,2], and
    // hard-threshold/zero-threshold controls. No package helper supplies wants.
    static const double expected[12][3] = {
        {0, 0, 0}, {1.0 / 64, 1.0 / 128, 1.0 / 256},
        {0.25, 0.125, 0.0625}, {1, 0.5, 0.25}, {3, 1.5, 0.75},
        {0, 0, 0}, {0, 0, 0}, {1, 0.5, 0.25}, {2, 0.5, 0.25},
        {65503, 0, 0}, {0.125, 0.0625, 0.03125}, {0, 0, 0}
    };
    for (unsigned i = 0; i < 12; i++)
        near3(probe->bright.at[i], expected[i][0], expected[i][1], expected[i][2]);
    // Unpaired nine-tap impulse oracle, independent of five-fetch offsets and
    // paired weights. It also pins normalization, symmetry and all RGB lanes.
    static const int kernel[9] = {6, 20, 45, 72, 84, 72, 45, 20, 6};
    double total = 0;
    for (unsigned i = 0; i < 9; i++) {
        const double value = kernel[i] / 370.0;
        near3(probe->impulse.at[i], value, value * 2, value * 4);
        total += probe->impulse.at[i].x;
    }
    TIDE_CHECK(fabs(total - 1) < 0.000003);
    near3(probe->constantBlur, 2, 4, 8);
    near3(probe->invalidBlur, 65504, 0, 0);
    near3(probe->combined.at[0], 4.5, 2.25, 1.125);
    near3(probe->combined.at[1], 4, 2, 1);
    near3(probe->combined.at[2], 65504, 65504, 65504);
    near3(probe->combined.at[3], 65504, 0, 0);
    TIDE_CHECK(!probe->bounded.enabled && probe->bounded.threshold == 1 && probe->bounded.softness == 1);
    TIDE_CHECK(probe->bounded.intensity == 0.25f && probe->bounded.radius == 1 && probe->bounded.iterations == 2);
    local.PostProbe.raw.threshold = -INFINITY;
    local.PostProbe.raw.softness = NAN;
    local.PostProbe.raw.intensity = -INFINITY;
    local.PostProbe.raw.radius = NAN;
    local.PostProbe.raw.iterations = INT_MIN;
    frame();
    TIDE_CHECK(probe->bounded.threshold == 0 && probe->bounded.softness == 0 && probe->bounded.intensity == 0);
    TIDE_CHECK(probe->bounded.radius == 0 && probe->bounded.iterations == 1);
    local.PostProbe.raw.threshold = INFINITY;
    local.PostProbe.raw.softness = INFINITY;
    local.PostProbe.raw.intensity = INFINITY;
    local.PostProbe.raw.radius = INFINITY;
    local.PostProbe.raw.iterations = INT_MAX;
    frame();
    TIDE_CHECK(probe->bounded.threshold == 65504 && probe->bounded.softness == 65504 && probe->bounded.intensity == 16);
    TIDE_CHECK(probe->bounded.radius == 8 && probe->bounded.iterations == 4);
    finish();
}

TIDE_TEST(render3d_post_default_is_one_presentation_without_scratch)
{
    start();
    frame();
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    post_blit list[32];
    TIDE_REQUIRE(blits(list) == 1);
    TIDE_CHECK(list[0].destination == 0 && source_id(list[0], 0) == renderer->hdr.id);
    TIDE_CHECK(!renderer->postReady && !renderer->bloomReady);
    TIDE_CHECK(!renderer->postA.id && !renderer->postB.id && !renderer->bloomA.id && !renderer->bloomB.id);
    TIDE_CHECK(renderer->output.target.id == renderer->hdr.id && renderer->postSourceSlot == 0);
    local.PostProbe.present = false;
    frame();
    TIDE_CHECK(blits(list) == 0 && renderer->ready && renderer->output.target.id == renderer->hdr.id);
    finish();
}

TIDE_TEST(render3d_post_ordered_callbacks_captures_and_bypass)
{
    start();
    local.PostProbe.mode = 1;
    frame();
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    post_blit list[32];
    TIDE_REQUIRE(blits(list) == 4);
    const uint64_t hdr = renderer->hdr.id, a = renderer->postA.id, b = renderer->postB.id;
    TIDE_CHECK(hdr != a && hdr != b && a != b);
    TIDE_CHECK(source_id(list[0], 0) == hdr && list[0].destination == a);
    TIDE_CHECK(source_id(list[1], 0) == a && list[1].destination == b);
    TIDE_CHECK(source_id(list[2], 0) == b && list[2].destination == a);
    TIDE_CHECK(source_id(list[3], 0) == a && list[3].destination == 0);
    // (x * 2 + 3) * .5 differs from reordered transforms. The first value
    // must remain captured as 2 after its construction local changed to 99.
    near1(uniform_float(list[0], 0), 2);
    near1(uniform_float(list[0], 1), 0);
    near1(uniform_float(list[1], 0), 1);
    near1(uniform_float(list[1], 1), 3);
    near1(uniform_float(list[2], 0), 0.5);
    near1(uniform_float(list[2], 1), 0);
    TIDE_CHECK(renderer->postSourceSlot == 1 && renderer->postPendingSlot == 0);
    // Check default and explicit false independently; two erroneous advances
    // could otherwise cancel each other's ping-pong parity.
    for (int mode = 5; mode <= 6; mode++) {
        local.PostProbe.mode = mode;
        frame();
        TIDE_REQUIRE(blits(list) == 2);
        TIDE_CHECK(source_id(list[0], 0) == hdr && list[0].destination == a);
        TIDE_CHECK(renderer->output.target.id == a && renderer->postSourceSlot == 1);
    }
    local.PostProbe.mode = 1;
    local.PostProbe.present = false;
    frame();
    TIDE_CHECK(blits(list) == 3 && renderer->ready && renderer->postReady && renderer->output.target.id == a);
    finish();
}

TIDE_TEST(render3d_post_bloom_passes_formats_and_retained_targets)
{
    start();
    local.PostProbe.bloomEnabled = true;
    frame();
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    post_blit list[32];
    TIDE_REQUIRE(blits(list) == 7);
    const uint64_t hdr = renderer->hdr.id, a = renderer->postA.id, b = renderer->postB.id;
    const uint64_t bright = renderer->bloomA.id, spread = renderer->bloomB.id;
    TIDE_CHECK(renderer->bloomReady && renderer->postReady);
    TIDE_CHECK(source_id(list[0], 0) == hdr && list[0].destination == bright);
    TIDE_CHECK(strstr(list[0].run->shader->name, "ExtractBloom") != NULL);
    for (int pass = 1; pass <= 4; pass++) {
        TIDE_CHECK(source_id(list[pass], 0) == (pass & 1 ? bright : spread));
        TIDE_CHECK(list[pass].destination == (pass & 1 ? spread : bright));
        TIDE_CHECK(strstr(list[pass].run->shader->name, "BlurBloom") != NULL);
        near1(uniform_float(list[pass], 0), pass & 1 ? 1 : 0);
        near1(uniform_float(list[pass], 1), pass & 1 ? 0 : 1);
    }
    TIDE_CHECK(list[5].destination == a && source_id(list[5], 0) == hdr && source_id(list[5], 1) == bright);
    TIDE_CHECK(list[6].destination == 0 && source_id(list[6], 0) == a);
    const tide_render_target targets[4] = {renderer->postA, renderer->postB, renderer->bloomA, renderer->bloomB};
    for (unsigned i = 0; i < 4; i++) {
        TIDE_CHECK(tide_render_target_format(targets[i].id) == TIDE_TARGET_RGBA16F);
        TIDE_CHECK(tide_render_target_samples(targets[i].id) == 1);
        TIDE_CHECK(targets[i].size.window == (i < 2 ? 1.0f : 0.25f));
    }
    gui.width = 129;
    gui.height = 65;
    frame();
    TIDE_CHECK(renderer->hdr.id == hdr && renderer->postA.id == a && renderer->postB.id == b);
    TIDE_CHECK(renderer->bloomA.id == bright && renderer->bloomB.id == spread);
    local.PostProbe.bloomIterations = INT_MAX;
    frame();
    TIDE_CHECK(blits(list) == 11);
    local.PostProbe.bloomRadius = 0;
    frame();
    TIDE_CHECK(blits(list) == 3);
    local.PostProbe.bloomIntensity = 0;
    frame();
    TIDE_CHECK(blits(list) == 1 && renderer->output.target.id == hdr);
    TIDE_CHECK(!renderer->bloomReady && !tide_render_target_live(bright) && !tide_render_target_live(spread));
    TIDE_CHECK(renderer->postA.id == a && renderer->postB.id == b);
    finish();
}

TIDE_TEST(render3d_post_staged_singleton_passes_surround_bloom_and_chain)
{
    start();
    local.PostProbe.mode = 3;
    frame();
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    post_blit list[32];
    TIDE_REQUIRE(blits(list) == 3);
    TIDE_CHECK(local.PostProbe.beforeSlot == 0 && local.PostProbe.afterSlot == 1);
    TIDE_CHECK(local.PostProbe.finalSlot == 2 && local.PostProbe.finalPending == 0);
    TIDE_CHECK(source_id(list[1], 0) == renderer->postA.id && source_id(list[1], 1) == renderer->hdr.id);
    TIDE_CHECK(list[1].destination == renderer->postB.id && strstr(list[1].run->shader->name, "PostResource") != NULL);
    local.PostProbe.bloomEnabled = true;
    frame();
    TIDE_REQUIRE(blits(list) == 9);
    TIDE_CHECK(local.PostProbe.beforeSlot == 0 && local.PostProbe.afterSlot == 2);
    TIDE_CHECK(local.PostProbe.finalSlot == 1 && local.PostProbe.finalPending == 0);
    // Bloom reads the custom pass's current output, not original scene HDR.
    TIDE_CHECK(source_id(list[1], 0) == renderer->postA.id && list[1].destination == renderer->bloomA.id);
    TIDE_CHECK(source_id(list[6], 0) == renderer->postA.id && list[6].destination == renderer->postB.id);
    TIDE_CHECK(source_id(list[7], 0) == renderer->postB.id && list[7].destination == renderer->postA.id);
    TIDE_CHECK(source_id(list[8], 0) == renderer->postA.id && list[8].destination == 0);
    finish();
}

TIDE_TEST(render3d_post_pending_reset_independent_recreation_and_disposal)
{
    start();
    local.PostProbe.mode = 4;
    frame();
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    const uint64_t hdr = renderer->hdr.id, old = renderer->postA.id;
    const tide_texture_ref alias = tide_render_target_color(renderer->postA);
    TIDE_CHECK(local.PostProbe.firstPending.id == old && local.PostProbe.secondPending.id == old);
    TIDE_CHECK(renderer->postPendingSlot == 1 && renderer->postSourceSlot == 0 && renderer->output.target.id == hdr);
    local.PostProbe.mode = 0;
    frame();
    TIDE_CHECK(renderer->postPendingSlot == 0 && renderer->postSourceSlot == 0 && renderer->output.target.id == hdr);
    local.PostProbe.mode = 1;
    local.PostProbe.disposeScratch = true;
    frame();
    TIDE_CHECK(renderer->ready && renderer->postReady && renderer->hdr.id == hdr);
    TIDE_CHECK(renderer->postA.id != old && !tide_render_target_live(alias.target.id));
    TIDE_CHECK(!tide_target_size_valid(tide_render_target_resolve(alias.target).size));
    local.PostProbe.disposeScratch = false;
    local.PostProbe.bloomEnabled = true;
    frame();
    const uint64_t ids[4] = {renderer->postA.id, renderer->postB.id, renderer->bloomA.id, renderer->bloomB.id};
    local.PostProbe.disabled = true;
    frame();
    for (unsigned i = 0; i < 4; i++) TIDE_CHECK(!tide_render_target_live(ids[i]));
    TIDE_CHECK(!renderer->ready && !renderer->postReady && !renderer->bloomReady && !renderer->output.target.id);
    local.PostProbe.disabled = false;
    frame();
    TIDE_CHECK(renderer->ready && renderer->postReady && renderer->bloomReady && renderer->hdr.id != hdr);
    for (unsigned i = 0; i < 4; i++) {
        const uint64_t fresh = i == 0 ? renderer->postA.id : i == 1 ? renderer->postB.id :
                               i == 2 ? renderer->bloomA.id : renderer->bloomB.id;
        TIDE_CHECK(fresh != ids[i] && tide_render_target_live(fresh));
    }
    finish();
}
