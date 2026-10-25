// Compare production Cast with the independent original Tide loop, after LOD
// gathering. Registry slots and batch boundaries are not observable draw data.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#ifdef SHADOW_BATCHES_GPU
#include "tide/platform.h"
#else
#include "tide_test.h"
#endif

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { FRAME_WIDTH = 192, FRAME_HEIGHT = 192 };
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

#define CHECK(condition) do { if (!(condition)) { \
    printf("FAIL: shadow batches line %d: %s (step %d)\n", __LINE__, #condition, world.ShadowState.step); \
    return false; \
} } while (0)

typedef struct shadow_event {
    const tide_draw_command *command;
    const tide_draw_run *run;
    const tide_draw_mesh_data *mesh;
    const uint8_t *instance;
    tide_float4x4 camera;
    uint32_t fit, mirrored, clip, stencil;
} shadow_event;

typedef struct shadow_iterator {
    uint64_t wanted, destination;
    uint32_t at, item, clip, stencil, fit, mirrored;
    tide_float4x4 camera;
    bool failed;
} shadow_iterator;

static shadow_iterator iterate(const uint64_t target)
{
    return (shadow_iterator){.wanted = target, .camera = tide_draw_first_camera_3d().matrix};
}

static bool next_event(shadow_iterator *it, shadow_event *out)
{
    while (it->at < draw.count) {
        const tide_draw_command *c = &draw.commands[it->at];
        if (c->kind == TIDE_DRAW_TARGET) {
            if (c->target > draw.target_count) { it->failed = true; return false; }
            it->destination = c->target ? draw.targets[c->target - 1u].id : 0;
        } else if (it->destination == it->wanted) {
            switch (c->kind) {
            case TIDE_DRAW_CAMERA_3D:
                if (c->camera.matrix == TIDE_DRAW_FIRST_CAMERA) it->camera = tide_draw_first_camera_3d().matrix;
                else {
                    if (c->camera.matrix >= draw.matrix_count) { it->failed = true; return false; }
                    it->camera = draw.matrices[c->camera.matrix];
                }
                it->fit = c->camera.fit;
                it->mirrored = c->camera.mirrored;
                break;
            case TIDE_DRAW_CLIP: it->clip = c->clip; break;
            case TIDE_DRAW_NO_CLIP: it->clip = 0; break;
            case TIDE_DRAW_STENCIL: it->stencil = c->stencil; break;
            case TIDE_DRAW_SHADED: {
                if (c->shaded.run >= draw.run_count) { it->failed = true; return false; }
                const tide_draw_run *run = &draw.runs[c->shaded.run];
                // LOD preparation leaves vacated commands with empty runs.
                if (!run->count) break;
                if (!run->shader || !run->mesh || run->mesh > draw.mesh_count) {
                    it->failed = true;
                    return false;
                }
                const uint32_t size = tide_shader_instance_size(run->shader);
                if (size < sizeof(tide_float4x4) ||
                    (uint64_t)run->first + (uint64_t)run->count * size > draw.instance_used) {
                    it->failed = true;
                    return false;
                }
                *out = (shadow_event){.command = c, .run = run, .mesh = &draw.meshes[run->mesh - 1u],
                    .instance = draw.instances + run->first + (size_t)it->item * size,
                    .camera = it->camera, .fit = it->fit, .mirrored = it->mirrored,
                    .clip = it->clip, .stencil = it->stencil};
                if (++it->item == run->count) it->item = 0, it->at++;
                return true;
            }
            default: break;
            }
        }
        it->at++;
    }
    return false;
}

static bool same_clip(uint32_t a, uint32_t b)
{
    while (a || b) {
        CHECK(a && b && a <= draw.clip_count && b <= draw.clip_count);
        const tide_draw_clip_data *x = &draw.clips[a - 1u], *y = &draw.clips[b - 1u];
        CHECK(memcmp(&x->rect, &y->rect, sizeof x->rect) == 0);
        const tide_draw_camera_data ca = x->camera ? draw.cameras[x->camera - 1u] : (tide_draw_camera_data){0};
        const tide_draw_camera_data cb = y->camera ? draw.cameras[y->camera - 1u] : (tide_draw_camera_data){0};
        CHECK((ca.screen != 0) == (cb.screen != 0));
        if (!ca.screen) {
            CHECK(memcmp(&ca.center, &cb.center, sizeof ca.center) == 0);
            CHECK(tide_draw_bits(ca.size) == tide_draw_bits(cb.size));
        }
        a = x->parent;
        b = y->parent;
    }
    return true;
}

static bool same_event(const shadow_event *a, const shadow_event *b)
{
    CHECK(a->command->depth == b->command->depth && a->command->cull == b->command->cull &&
          a->command->blend == b->command->blend && a->stencil == b->stencil);
    CHECK(a->fit == b->fit && a->mirrored == b->mirrored);
    CHECK(memcmp(&a->camera, &b->camera, sizeof a->camera) == 0);
    CHECK(same_clip(a->clip, b->clip));
    const tide_shader *sa = a->run->shader, *sb = b->run->shader;
    CHECK(sa->id == sb->id && tide_shader_instance_size(sa) == tide_shader_instance_size(sb));
    CHECK(tide_shader_uniform_size(sa) == tide_shader_uniform_size(sb));
    CHECK(sa->texture_count == 0 && sb->texture_count == 0);
    CHECK(a->run->sampling == b->run->sampling && a->run->moved == b->run->moved);
    CHECK(memcmp(&a->run->tint, &b->run->tint, sizeof a->run->tint) == 0);
    CHECK(memcmp(a->instance, b->instance, tide_shader_instance_size(sa)) == 0);
    CHECK(memcmp(draw.uniforms + a->run->uniforms, draw.uniforms + b->run->uniforms,
                 tide_shader_uniform_size(sa)) == 0);
    const tide_draw_mesh_data *ma = a->mesh, *mb = b->mesh;
    CHECK(ma->level == mb->level && ma->index_count == mb->index_count);
    CHECK(memcmp(ma->indices, mb->indices, (size_t)ma->index_count * sizeof *ma->indices) == 0);
    if (ma->base) ma = &draw.meshes[ma->base - 1u];
    if (mb->base) mb = &draw.meshes[mb->base - 1u];
    CHECK(ma->vertex_count == mb->vertex_count && ma->vertex_size == mb->vertex_size);
    CHECK(memcmp(ma->bytes, mb->bytes, (size_t)ma->vertex_count * ma->vertex_size) == 0);
    return true;
}

static int wanted_casters(const int step)
{
    int count = 0;
    for (int i = 0; i < 290; i++) {
        if (step == 3) continue;
        if (step >= 2 && (i % 17 == 0 || i % 19 == 0)) continue;
        if (step == 4 ? i % 5 != 0 : step >= 1 && (i % 7 == 0 || i % 11 == 0)) continue;
        count++;
    }
    return count;
}

static bool compare_casts(const bool require_witnesses)
{
    const Render3D_Renderer *renderer = &local.Render3D_Renderer;
    const ShadowProbe *probe = &local.ShadowProbe;
    if (!renderer->ready) { CHECK(draw.run_count == 0); return true; }
    CHECK(probe->referenceReady && probe->referenceSize == renderer->atlasSize);
    if (!probe->views) {
        bool actual_found = false, reference_found = false;
        for (uint32_t i = 0; i < draw.target_count; i++) {
            const tide_draw_target_data *target = &draw.targets[i];
            if (target->id == renderer->shadowMap.id) { CHECK(target->place.clip == 0); actual_found = true; }
            if (target->id == probe->reference.id) { CHECK(target->place.clip == 0); reference_found = true; }
        }
        CHECK(actual_found && reference_found);
    }
    tide_draw_prepare(&draw, NULL);
    shadow_iterator actual = iterate(renderer->shadowMap.id), reference = iterate(probe->reference.id);
    uint32_t count = 0, max_run = 0, camera_count = 0;
    bool full_lod = false, simplified_lod = false, mirrored = false, ordinary = false, sheared = false;
    tide_float4x4 previous_camera = {0};
    uint32_t previous_clip = 0;
    for (;;) {
        shadow_event a, b;
        const bool have_a = next_event(&actual, &a), have_b = next_event(&reference, &b);
        CHECK(!actual.failed && !reference.failed && have_a == have_b);
        if (!have_a) break;
        if (!same_event(&a, &b)) {
            printf("shadow instance %u differs\n", (unsigned)count);
            return false;
        }
        if (count == 0 || memcmp(&previous_camera, &a.camera, sizeof a.camera) != 0 || previous_clip != a.clip) {
            camera_count++;
            previous_camera = a.camera;
            previous_clip = a.clip;
        }
        if (a.run->count > max_run) max_run = a.run->count;
        const tide_draw_mesh_data *base = a.mesh->base ? &draw.meshes[a.mesh->base - 1u] : a.mesh;
        if (base->levels) {
            full_lod |= a.mesh->level == 0;
            simplified_lod |= a.mesh->level > 0;
        }
        mirrored |= (a.command->cull & TIDE_DRAW_MIRRORED) != 0;
        ordinary |= (a.command->cull & TIDE_DRAW_MIRRORED) == 0;
        tide_float4x4 matrix;
        memcpy(&matrix, a.instance, sizeof matrix);
        const float dot = matrix.c0.x * matrix.c1.x + matrix.c0.y * matrix.c1.y + matrix.c0.z * matrix.c1.z;
        sheared |= fabsf(dot) > 0.0001f;
        count++;
    }
    CHECK(count == (uint32_t)probe->referenceItems);
    CHECK(count == (uint32_t)(wanted_casters(world.ShadowState.step) * probe->views));
    if (count) CHECK(camera_count == (uint32_t)probe->views);
    if (require_witnesses) {
        CHECK(max_run > 256 && full_lod && simplified_lod && mirrored && ordinary && sheared);
        CHECK(probe->views == 8);
    }
    return true;
}

static void record_frame(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, (tide_float2){FRAME_WIDTH, FRAME_HEIGHT});
    gui.width = FRAME_WIDTH;
    gui.height = FRAME_HEIGHT;
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
}

#ifdef SHADOW_BATCHES_GPU
static bool gpu_pair(const float scale, const bool atlas)
{
    const int width = (int)((float)FRAME_WIDTH * scale), height = (int)((float)FRAME_HEIGHT * scale);
    const int count = width * height;
    tide_float2 *points = malloc((size_t)count * sizeof *points);
    uint32_t *actual = malloc((size_t)count * sizeof *actual), *reference = malloc((size_t)count * sizeof *reference);
    CHECK(points && actual && reference);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) points[y * width + x] = (tide_float2){(float)x + 0.5f, (float)y + 0.5f};
    }
    local.ShadowProbe.showAtlas = atlas;
    local.ShadowProbe.referenceColor = false;
    record_frame();
    bool good = compare_casts(false);
    if (good) tide_platform_read_pixels_as(&draw, FRAME_WIDTH, FRAME_HEIGHT, scale, points, count, actual);
    if (good && atlas) {
        bool occupied = false;
        for (int i = 0; i < count; i++) {
            if ((actual[i] >> 24) != 0) {
                printf("FAIL: unequal atlas depth at physical pixel %d, scale %.0f, step %d\n",
                       i, (double)scale, world.ShadowState.step);
                good = false;
                break;
            }
            occupied |= (actual[i] & UINT32_C(0x0000ff00)) != 0;
        }
        if (!occupied && world.ShadowState.step != 3) {
            puts("FAIL: exact atlas comparison has no rendered occluder depth");
            good = false;
        }
    }
    if (good && !atlas) {
        local.ShadowProbe.referenceColor = true;
        record_frame();
        good = compare_casts(false);
        if (good) {
            tide_platform_read_pixels_as(&draw, FRAME_WIDTH, FRAME_HEIGHT, scale, points, count, reference);
            if (memcmp(actual, reference, (size_t)count * sizeof *actual) != 0) {
                puts("FAIL: original and optimized shadow atlases change complete color-frame pixels");
                good = false;
            }
        }
    }
    free(reference);
    free(actual);
    free(points);
    local.ShadowProbe.referenceColor = false;
    local.ShadowProbe.showAtlas = false;
    return good;
}
#endif

static bool exercise(void)
{
    CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    for (int step = 0; step <= 4; step++) {
        if (step) {
            world.ShadowState.step = step;
            tide_world_tick(&world);
        }
        const uint64_t hash = tide_world_hash(&world);
        record_frame();
        CHECK(compare_casts(step == 0));
        CHECK(tide_world_hash(&world) == hash);
#ifdef SHADOW_BATCHES_GPU
        CHECK(gpu_pair(step == 1 ? 2.0f : 1.0f, false));
        if (step == 0 || step == 3) CHECK(gpu_pair(1, true));
        CHECK(tide_world_hash(&world) == hash);
#endif
    }
    // Reuse the same local state across no-view frames, atlas resizes, LOD
    // overrides and disable/re-enable. Invocation scratch must not leak draws.
    const uint64_t hash = tide_world_hash(&world);
    const uint64_t initial_shadow = local.Render3D_Renderer.shadowMap.id;
    const int budgets[] = {0, 5, 8, 8};
    const int resolutions[] = {64, 96, 48, 64};
    for (unsigned i = 0; i < sizeof budgets / sizeof budgets[0]; i++) {
        local.ShadowProbe.budget = budgets[i];
        local.ShadowProbe.resolution = resolutions[i];
        local.ShadowProbe.lodMode = (int)(i % 3);
        record_frame();
        CHECK(compare_casts(false));
        CHECK(local.ShadowProbe.views == (budgets[i] == 0 ? 0 : budgets[i] < 8 ? 2 : 8));
        CHECK(local.Render3D_Renderer.shadowMap.id == initial_shadow);
    }
    local.ShadowProbe.noShadows = true;
    record_frame();
    CHECK(compare_casts(false) && local.ShadowProbe.views == 0);
    local.ShadowProbe.disabled = true;
    record_frame();
    CHECK(compare_casts(false));
    CHECK(!tide_render_target_live(initial_shadow) && !local.ShadowProbe.referenceReady);
    local.ShadowProbe.noShadows = false;
    local.ShadowProbe.disabled = false;
    record_frame();
    CHECK(compare_casts(false) && local.ShadowProbe.views == 8);
    CHECK(local.Render3D_Renderer.shadowMap.id != initial_shadow);
    CHECK(tide_world_hash(&world) == hash);
    local.ShadowProbe.disabled = true;
    record_frame();
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&world);
    tide_render_targets_free();
    return true;
}

#ifdef SHADOW_BATCHES_GPU
static int frame(void *context, const float seconds)
{
    (void)context;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    if (!exercise()) return 1;
    puts("ok: original and batched shadows preserve prepared draws and exact full-frame/atlas pixels");
    return 0;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "Render3D shadow batches",
        .width = FRAME_WIDTH, .height = FRAME_HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
#else
TIDE_TEST(render3d_shadow_batches_match_original_cast_after_prepare_and_dynamic_edits)
{
    TIDE_REQUIRE(exercise());
}
#endif
