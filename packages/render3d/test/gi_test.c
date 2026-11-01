#include <limits.h>
#include <math.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void start(void)
{
    TIDE_CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
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

TIDE_TEST(render3d_gi_opt_in_has_no_default_resources_or_passes)
{
    start();
    frame();
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    TIDE_CHECK(!renderer->gi.enabled && !renderer->giState.ready);
    TIDE_CHECK(!renderer->giResources.atlas.id && !renderer->giResources.face0.id && !renderer->giResources.face5.id);
    TIDE_CHECK(!renderer->giIrradiance.target.id && !renderer->giDistance.target.id);
    TIDE_CHECK(renderer->giState.initialized == 0);
    int presentations = 0;
    for (uint32_t at = 0; at < draw.count; at++) {
        TIDE_CHECK(draw.commands[at].kind != TIDE_DRAW_SHADED);
        if (draw.commands[at].kind == TIDE_DRAW_BLIT) presentations++;
    }
    TIDE_CHECK(presentations == 1);
    finish();
}

TIDE_TEST(render3d_gi_budget_round_robin_freeze_and_volume_invalidation)
{
    start();
    GIProbe *probe = &local.GIProbe;
    probe->settings.enabled = true;
    probe->settings.counts = tide_i3(2, 2, 2);
    probe->settings.size = tide_f3(2, 4, 6);
    probe->settings.center = tide_f3(3, -2, 1);
    probe->settings.updatesPerFrame = 2;
    frame();
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    const Render3D_GIState *state = &renderer->giState;
    TIDE_CHECK(state->ready && state->total == 8 && state->initialized == 2 && state->cursor == 2);
    TIDE_CHECK(shaded_into(renderer->giResources.atlas.id) == 2);
    const uint64_t atlas = renderer->giResources.atlas.id, face = renderer->giResources.face0.id;
    TIDE_CHECK(atlas && face && atlas != face && face != renderer->giResources.face5.id);
    TIDE_CHECK(tide_render_target_samples(atlas) == 1);
    TIDE_CHECK(tide_render_target_format(face) == TIDE_TARGET_RGBA16F);
    // Independent grid endpoints and x-fastest indexing in a translated,
    // nonuniform box. No package position helper supplies these expectations.
    for (int index = 0; index < 8; index++) {
        const tide_float3 p = probe->grid.at[index];
        TIDE_CHECK(p.x == (index & 1 ? 4 : 2));
        TIDE_CHECK(p.y == (index & 2 ? 0 : -4));
        TIDE_CHECK(p.z == (index & 4 ? 4 : -2));
    }
    frame();
    TIDE_CHECK(state->initialized == 4 && state->cursor == 4);
    TIDE_CHECK(renderer->giResources.atlas.id == atlas && renderer->giResources.face0.id == face);
    probe->settings.updatesPerFrame = 0;
    frame();
    TIDE_CHECK(state->initialized == 4 && state->cursor == 4 && shaded_into(atlas) == 0);
    probe->settings.intensity = 2;
    frame();
    TIDE_CHECK(state->initialized == 4 && state->cursor == 4 && renderer->giResources.atlas.id == atlas);
    probe->settings.updatesPerFrame = 4;
    frame();
    TIDE_CHECK(state->initialized == 8 && state->cursor == 0 && shaded_into(atlas) == 4);
    frame();
    TIDE_CHECK(state->initialized == 8 && state->cursor == 4);
    probe->settings.center.x += 0.5f;
    probe->settings.updatesPerFrame = 1;
    frame();
    TIDE_CHECK(state->initialized == 1 && state->cursor == 1);
    probe->settings.size.y += 1;
    frame();
    TIDE_CHECK(state->initialized == 1 && state->cursor == 1);
    probe->settings.counts = tide_i3(4, 4, 4);
    frame();
    TIDE_CHECK(state->total == 64 && state->initialized == 1 && state->cursor == 1);
    TIDE_CHECK(state->atlasRows == 8);
    finish();
}

TIDE_TEST(render3d_gi_disable_dispose_and_reenable_release_every_target)
{
    start();
    GIProbe *probe = &local.GIProbe;
    probe->settings.enabled = true;
    probe->settings.updatesPerFrame = 0;
    frame();
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    const Render3D_GIState *state = &renderer->giState;
    TIDE_CHECK(state->ready && state->initialized == 0);
    const uint64_t ids[] = {renderer->giResources.atlas.id, renderer->giResources.face0.id, renderer->giResources.face1.id,
        renderer->giResources.face2.id, renderer->giResources.face3.id, renderer->giResources.face4.id, renderer->giResources.face5.id};
    probe->settings.enabled = false;
    frame();
    TIDE_CHECK(!state->ready && !renderer->giResources.atlas.id && !renderer->giResources.face0.id && !renderer->giResources.face5.id);
    TIDE_CHECK(!renderer->giIrradiance.target.id && !renderer->giDistance.target.id);
    for (unsigned at = 0; at < sizeof ids / sizeof ids[0]; at++)
        TIDE_CHECK(!tide_render_target_live(ids[at]));
    TIDE_CHECK(renderer->ready);
    probe->settings.enabled = true;
    frame();
    TIDE_CHECK(state->ready && renderer->giResources.atlas.id != ids[0] && state->initialized == 0);
    const uint64_t next_atlas = renderer->giResources.atlas.id;
    probe->dispose = true;
    frame();
    TIDE_CHECK(state->ready && renderer->giResources.atlas.id != next_atlas && !tide_render_target_live(next_atlas));
    probe->dispose = false;
    probe->disabled = true;
    frame();
    TIDE_CHECK(!renderer->ready && !state->ready && !renderer->giResources.atlas.id && !renderer->giResources.face0.id);
    finish();
}

TIDE_TEST(render3d_gi_settings_are_bounded_for_arbitrary_game_values)
{
    start();
    GIProbe *probe = &local.GIProbe;
    probe->raw.center = tide_f3(-INFINITY, 0, INFINITY);
    probe->raw.size = tide_f3(-INFINITY, NAN, INFINITY);
    probe->raw.counts = tide_i3(INT_MIN, 3, INT_MAX);
    probe->raw.updatesPerFrame = INT_MAX;
    probe->raw.intensity = INFINITY;
    frame();
    const Render3D_GISettings bounded = probe->bounded;
    TIDE_CHECK(bounded.center.x == -1000000 && bounded.center.z == 1000000);
    TIDE_CHECK(bounded.size.x == 0.1f && bounded.size.y == 0.1f && bounded.size.z == 100);
    TIDE_CHECK(bounded.counts.x == 2 && bounded.counts.y == 3 && bounded.counts.z == 4);
    TIDE_CHECK(bounded.updatesPerFrame == 4 && bounded.intensity == 16);
    probe->raw.updatesPerFrame = INT_MIN;
    probe->raw.intensity = NAN;
    frame();
    TIDE_CHECK(probe->bounded.updatesPerFrame == 0 && probe->bounded.intensity == 0);
    finish();
}
