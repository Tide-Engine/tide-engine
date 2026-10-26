#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tide/draw.h"
#include "tide/page.h"
#include "tide_test.h"

int32_t TransparentMark(tide_draw_list *list);
void SortTransparent(tide_draw_list *list, int32_t first, tide_float3 eye, tide_float3 forward);

typedef struct captured_instance {
    tide_float4x4 world;
    float captures[8];
    int32_t functions[4];
} captured_instance;

static const tide_shader shaded = {.id = 917, .dims = 3, .vertex_size = sizeof(tide_vertex3),
    .instance_floats = 6, .instance_ints = 1, .uniform_floats = 2, .uniform_ints = 1, .texture_count = 2,
    .bounds_position = 1};
static const tide_shader smaller = {.id = 918, .dims = 3, .vertex_size = sizeof(tide_vertex3),
    .instance_floats = 5, .uniform_floats = 1, .texture_count = 1};
static const tide_vertex3 triangle[] = {
    {{-0.2f, -0.2f, 0}, {0, 0}, {1, 1, 1, 1}},
    {{0.2f, -0.2f, 0}, {1, 0}, {1, 1, 1, 1}},
    {{0, 0.2f, 0}, {0.5f, 1}, {1, 1, 1, 1}},
};
static const uint32_t indices[] = {0, 1, 2};
static const uint32_t simpler_indices[] = {0, 1, 2, 0, 1, 2};
static const tide_mesh_levels detail = {.radius = 0.5f, .count = 2, .error = {0.01f, 0.1f},
    .first = {0, 3}, .indices = {3, 3}, .index = simpler_indices, .index_count = 6};

static uint32_t mesh(tide_draw_list *d, const uint64_t id, const bool levels)
{
    bool fill;
    const uint32_t slot = tide_draw_mesh_slot(d, TIDE_MESH_BYTES, id, id + 1u, 1, &fill);
    if (fill) {
        tide_draw_mesh_fill_bytes(d, slot, triangle, sizeof *triangle, 3, indices, 3);
        if (levels) tide_draw_mesh_levels(d, slot, &detail);
    }
    return slot;
}

static uint32_t texture(tide_draw_list *d, const uint64_t id, const uint32_t rgba)
{
    uint8_t *pixels;
    const uint32_t slot = tide_draw_texture_slot(d, TIDE_PIXELS_MEMORY, id, 1, 1, 1, &pixels);
    memcpy(pixels, &rgba, sizeof rgba);
    return slot;
}

static captured_instance instance(const uint32_t id, const float depth, const bool mirrored)
{
    captured_instance value = {.world = tide_translate_f4x4(tide_f3(0, 0, depth))};
    if (mirrored) value.world = tide_mul_f4x4(value.world, tide_scaling_f4x4(tide_f3(-1, 1, 1)));
    for (uint32_t i = 0; i < 8; i++) value.captures[i] = (float)(id * 17u + i);
    for (uint32_t i = 0; i < 4; i++) value.functions[i] = (int32_t)(id * 31u + i);
    return value;
}

static void frame(tide_draw_list *d)
{
    tide_draw_reset(d);
    tide_draw_screen_size(d, tide_f2(800, 600));
    tide_draw_set_frustum_culling(d, false);
    tide_draw_set_camera_3d(d, tide_f3(0, 0, 0), tide_identity_q(), 60);
    tide_draw_set_blend(d, TIDE_BLEND_ALPHA);
    tide_draw_set_depth(d, TIDE_DEPTH_READ);
}

// A flattened public draw packet, including effective state and every owned
// capture byte. Registry slots, byte offsets and batching are not appearance.
typedef struct packet {
    uint64_t shader, mesh, destination;
    uint32_t level, size, uniform_size, moved;
    uint64_t sampling;
    uint8_t blend, depth, cull;
    tide_color tint;
    tide_draw_transform transform;
    uint8_t instance[sizeof(captured_instance)], uniforms[48];
    uint64_t textures[TIDE_SHADER_TEXTURES];
    uint32_t pixels[TIDE_SHADER_TEXTURES];
    tide_float4x4 camera;
    uint32_t fit, camera_mirrored, stencil;
    bool clipped;
    tide_rect clip;
} packet;

typedef struct destination_state {
    tide_float4x4 camera;
    uint32_t fit, mirrored, clip, stencil;
} destination_state;

static uint32_t capture(const tide_draw_list *d, packet *out, const uint32_t capacity)
{
    destination_state state[16];
    memset(state, 0, sizeof state);
    for (uint32_t i = 0; i < 16; i++) state[i].camera = tide_draw_first_camera_3d().matrix;
    uint32_t target = 0, count = 0;
    for (uint32_t i = 0; i < d->count; i++) {
        const tide_draw_command *c = &d->commands[i];
        if (c->kind == TIDE_DRAW_TARGET) target = c->target;
        if (target >= 16) { TIDE_CHECK(target < 16); return count; }
        destination_state *at = &state[target];
        switch (c->kind) {
        case TIDE_DRAW_CAMERA_3D:
            at->camera = c->camera.matrix == TIDE_DRAW_FIRST_CAMERA ? tide_draw_first_camera_3d().matrix
                : d->matrices[c->camera.matrix];
            at->fit = c->camera.fit;
            at->mirrored = c->camera.mirrored;
            break;
        case TIDE_DRAW_CLIP: at->clip = c->clip; break;
        case TIDE_DRAW_NO_CLIP: at->clip = 0; break;
        case TIDE_DRAW_STENCIL: at->stencil = c->stencil; break;
        case TIDE_DRAW_SHADED: {
            if (c->shaded.run >= d->run_count) { TIDE_CHECK(c->shaded.run < d->run_count); return count; }
            const tide_draw_run *r = &d->runs[c->shaded.run];
            if (!r->shader || !r->count) break;
            const uint32_t size = tide_shader_instance_size(r->shader), uniform_size = tide_shader_uniform_size(r->shader);
            if (size > sizeof out[0].instance || uniform_size > sizeof out[0].uniforms
                || !r->mesh || r->mesh > d->mesh_count) { TIDE_CHECK(false); return count; }
            const tide_draw_mesh_data *m = &d->meshes[r->mesh - 1u];
            for (uint32_t k = 0; k < r->count; k++) {
                if (count >= capacity) { TIDE_CHECK(count < capacity); return count; }
                packet *p = &out[count++];
                memset(p, 0, sizeof *p);
                p->shader = r->shader->id;
                p->mesh = m->id;
                p->level = m->level;
                p->destination = target ? d->targets[target - 1u].id : 0;
                p->size = size;
                p->uniform_size = uniform_size;
                p->sampling = r->sampling;
                p->moved = r->moved;
                p->blend = c->blend; p->depth = c->depth; p->cull = c->cull;
                p->tint = r->tint;
                p->transform = r->transform;
                memcpy(p->instance, d->instances + r->first + (size_t)k * size, size);
                if (uniform_size) memcpy(p->uniforms, d->uniforms + r->uniforms, uniform_size);
                for (uint32_t t = 0; t < TIDE_SHADER_TEXTURES; t++) {
                    if (!r->textures[t]) continue;
                    const tide_draw_texture *tex = &d->textures[r->textures[t] - 1u];
                    p->textures[t] = tex->id;
                    if (tex->pixels) memcpy(&p->pixels[t], tex->pixels, sizeof p->pixels[t]);
                }
                p->camera = at->camera;
                p->fit = at->fit; p->camera_mirrored = at->mirrored; p->stencil = at->stencil;
                p->clipped = at->clip != 0;
                if (at->clip) p->clip = d->clips[at->clip - 1u].rect;
            }
            break;
        }
        default: break;
        }
    }
    return count;
}

static void check_order(const packet *before, const packet *after, const uint32_t *order, const uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) TIDE_CHECK(memcmp(&before[order ? order[i] : i], &after[i], sizeof *after) == 0);
}

static uint32_t state_commands(const tide_draw_list *d, tide_draw_command *out)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < d->count; i++) {
        const tide_draw_command *c = &d->commands[i];
        if (c->kind == TIDE_DRAW_SHADED) continue;
        TIDE_CHECK(n < 64);
        if (n < 64) out[n++] = *c;
    }
    return n;
}

TIDE_TEST(render3d_transparent_sorts_merged_instances_and_owns_captures)
{
    tide_draw_list d = {0}, appended = {0};
    frame(&d);
    const uint32_t shape = mesh(&d, 11, false);
    const uint32_t textures[] = {texture(&d, 111, 0xff665544), texture(&d, 222, 0xffccbbaa)};
    uint32_t uniforms[12];
    for (uint32_t i = 0; i < 12; i++) uniforms[i] = i * 13u + 1u;
    captured_instance prefix = instance(99, 500, false);
    tide_draw_shaded(&d, &shaded, shape, &prefix, uniforms, textures, 19);
    const int32_t first = TransparentMark(&d);
    TIDE_REQUIRE(first >= 0);
    static const float depths[] = {3, 12, 3, 6, 12, -1};
    captured_instance values[6];
    for (uint32_t i = 0; i < 6; i++) values[i] = instance(i, depths[i], false);
    tide_draw_shaded_instances(&d, &shaded, shape, values, 6, uniforms, textures, 19);
    TIDE_REQUIRE(d.runs[d.commands[d.count - 1u].shaded.run].count == 6);
    packet before[7], after[7];
    TIDE_REQUIRE(capture(&d, before, 7) == 7);
    const uint32_t used = d.instance_used, uniform_used = d.uniform_used;
    tide_draw_set_depth(&d, TIDE_DEPTH_READ_WRITE);
    tide_draw_set_blend(&d, TIDE_BLEND_REPLACE);
    const tide_draw_place place = d.place;
    SortTransparent(&d, first, tide_f3(5, -2, -3), tide_f3(0, 0, 2));
    TIDE_CHECK(d.instance_used == used && d.uniform_used == uniform_used);
    TIDE_CHECK(memcmp(&place, &d.place, sizeof place) == 0);
    TIDE_REQUIRE(capture(&d, after, 7) == 7);
    static const uint32_t order[] = {0, 2, 5, 4, 1, 3, 6};
    check_order(before, after, order, 7);
    TIDE_CHECK(d.runs[d.commands[d.count - 1u].shaded.run].count == 6);
    memset(values, 0xab, sizeof values);
    memset(uniforms, 0xcd, sizeof uniforms);
    SortTransparent(&d, first, tide_f3(5, -2, -3), tide_f3(0, 0, 2));
    tide_draw_prepare(&d, NULL);
    tide_draw_prepare(&d, NULL);
    TIDE_REQUIRE(capture(&d, after, 7) == 7);
    check_order(before, after, order, 7);
    tide_draw_append(&appended, &d);
    tide_draw_reset(&d);
    frame(&d);
    const uint32_t replacement = mesh(&d, 21, false);
    captured_instance changed = instance(111, 3, false);
    uint32_t changed_uniforms[12] = {0};
    tide_draw_shaded(&d, &shaded, replacement, &changed, changed_uniforms, NULL, 0);
    tide_draw_free(&d);
    tide_draw_prepare(&appended, NULL);
    TIDE_REQUIRE(capture(&appended, after, 7) == 7);
    check_order(before, after, order, 7);
    tide_draw_free(&appended);
}

TIDE_TEST(render3d_transparent_keeps_material_textures_lods_and_mirrored_cull)
{
    tide_draw_list d = {0};
    frame(&d);
    const uint32_t first_mesh = mesh(&d, 31, true), second_mesh = mesh(&d, 41, false);
    const uint32_t red = texture(&d, 333, 0xff0000ff), blue = texture(&d, 444, 0xffff0000);
    const uint32_t images[][2] = {{red, blue}, {blue, red}};
    uint32_t uniforms[2][12];
    for (uint32_t j = 0; j < 2; j++) for (uint32_t i = 0; i < 12; i++) uniforms[j][i] = 5u + i + 101u * j;
    static const float depth[] = {10, 100, 2, 100, 6, 3, 10, 200, 4};
    static const uint32_t material[] = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    const int32_t first = TransparentMark(&d);
    for (uint32_t i = 0; i < 9; i++) {
        const captured_instance value = instance(i, depth[i], i == 3 || i == 6);
        tide_draw_set_tint(&d, i == 4 ? (tide_color){0.25f, 0.5f, 0.75f, 0.5f} : TIDE_COLOR_WHITE);
        tide_draw_set_cull(&d, i == 5 ? TIDE_CULL_NONE : TIDE_CULL_BACK);
        tide_draw_shaded(&d, i == 8 ? &smaller : &shaded, i == 1 || i == 4 || i == 7 ? second_mesh : first_mesh,
                         &value, uniforms[material[i]], images[material[i]], material[i] * 17u);
    }
    packet before[9], after[9];
    TIDE_REQUIRE(capture(&d, before, 9) == 9);
    SortTransparent(&d, first, tide_f3(0, 0, 0), tide_f3(0, 0, 1));
    tide_draw_unset_depth(&d);
    tide_draw_unset_blend(&d);
    tide_draw_prepare(&d, NULL);
    TIDE_REQUIRE(capture(&d, after, 9) == 9);
    static const uint32_t order[] = {7, 1, 3, 0, 6, 4, 8, 5, 2};
    check_order(before, after, order, 9);
    TIDE_CHECK(after[2].cull == (TIDE_CULL_BACK | TIDE_DRAW_MIRRORED));
    TIDE_CHECK(after[7].cull == TIDE_CULL_NONE);
    TIDE_CHECK(after[3].level == 1 && after[8].level == 0);
    tide_draw_prepare(&d, NULL);
    TIDE_REQUIRE(capture(&d, after, 9) == 9);
    check_order(before, after, order, 9);
    tide_draw_free(&d);
}

TIDE_TEST(render3d_transparent_preserves_state_and_other_draw_barriers)
{
    tide_draw_list d = {0};
    const tide_render_target target = tide_render_target_make_as(128, 128, TIDE_TARGET_RGBA16F);
    frame(&d);
    tide_draw_to_sized(&d, target.id, target.size);
    tide_draw_set_frustum_culling(&d, false);
    tide_draw_set_blend(&d, TIDE_BLEND_ALPHA);
    tide_draw_set_depth(&d, TIDE_DEPTH_READ);
    const uint32_t shape = mesh(&d, 51, false);
    uint32_t uniforms[12] = {19};
    const int32_t first = TransparentMark(&d);
    for (uint32_t group = 0; group < 6; group++) {
        if (group == 1) tide_draw_set_clip(&d, (tide_rect){1, 2, 30, 40});
        if (group == 2) tide_draw_set_stencil(&d, tide_stencil_tested(TIDE_STENCIL_EQUAL, 7));
        if (group == 3) tide_draw_set_camera_3d(&d, tide_f3(2, 3, 4), tide_identity_q(), 45);
        if (group == 4) tide_draw_to_sized(&d, 0, (tide_target_size){0});
        if (group == 5) tide_draw_rect(&d, tide_f2(1, 2), tide_f2(3, 4), TIDE_COLOR_WHITE);
        captured_instance pair[] = {instance(group * 2u, (float)group + 1, false),
                                    instance(group * 2u + 1u, (float)group + 10, false)};
        tide_draw_shaded_instances(&d, &shaded, shape, pair, 2, uniforms, NULL, 0);
    }
    packet before[12], after[12];
    tide_draw_command states[64], after_states[64];
    TIDE_REQUIRE(capture(&d, before, 12) == 12);
    const uint32_t state_count = state_commands(&d, states);
    SortTransparent(&d, first, tide_f3(0, 0, 0), tide_f3(0, 0, 1));
    TIDE_REQUIRE(state_commands(&d, after_states) == state_count);
    TIDE_CHECK(memcmp(states, after_states, state_count * sizeof *states) == 0);
    TIDE_REQUIRE(capture(&d, after, 12) == 12);
    static const uint32_t order[] = {1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10};
    check_order(before, after, order, 12);
    tide_draw_free(&d);
    tide_render_target_dispose(target);
}

TIDE_TEST(render3d_transparent_checks_marks_and_nonfinite_sort_keys)
{
    tide_draw_list d = {0};
    TIDE_CHECK(TransparentMark(NULL) == -1 && TransparentMark(&d) == 0);
    tide_draw_list too_many = {.count = UINT32_MAX};
    TIDE_CHECK(TransparentMark(&too_many) == -1);
    SortTransparent(NULL, 0, tide_f3(0, 0, 0), tide_f3(0, 0, 1));
    SortTransparent(&d, 0, tide_f3(0, 0, 0), tide_f3(0, 0, 1));
    frame(&d);
    tide_draw_set_cull(&d, TIDE_CULL_NONE);
    const uint32_t shape = mesh(&d, 61, false);
    uint32_t uniforms[12] = {0};
    static const float depth[] = {NAN, 0, INFINITY, -INFINITY, -0.0f, FLT_MAX, -FLT_MAX};
    const int32_t first = TransparentMark(&d);
    captured_instance values[7];
    for (uint32_t i = 0; i < 7; i++) values[i] = instance(i, depth[i], false);
    tide_draw_shaded_instances(&d, &shaded, shape, values, 7, uniforms, NULL, 0);
    packet before[7], after[7];
    TIDE_REQUIRE(capture(&d, before, 7) == 7);
    static const int32_t bad_marks[] = {-1, INT32_MIN, INT32_MAX};
    for (uint32_t i = 0; i < sizeof bad_marks / sizeof *bad_marks; i++) {
        SortTransparent(&d, bad_marks[i], tide_f3(0, 0, 0), tide_f3(0, 0, 1));
    }
    SortTransparent(&d, (int32_t)d.count, tide_f3(0, 0, 0), tide_f3(0, 0, 1));
    SortTransparent(&d, first, tide_f3(NAN, 0, 0), tide_f3(0, 0, 1));
    SortTransparent(&d, first, tide_f3(0, 0, 0), tide_f3(0, INFINITY, 1));
    SortTransparent(&d, first, tide_f3(0, 0, 0), tide_f3(0, 0, 0));
    TIDE_REQUIRE(capture(&d, after, 7) == 7);
    check_order(before, after, NULL, 7);
    SortTransparent(&d, first, tide_f3(FLT_MAX, FLT_MAX, -FLT_MAX), tide_f3(0, 0, FLT_MAX));
    TIDE_REQUIRE(capture(&d, after, 7) == 7);
    // Invalid object positions have key zero. The finite zero positions are
    // now far in front of the extreme eye; equal keys still retain order.
    static const uint32_t order[] = {5, 1, 4, 0, 2, 3, 6};
    check_order(before, after, order, 7);
    tide_draw_free(&d);
}

TIDE_TEST(render3d_transparent_reuses_owned_bytes_across_large_frames)
{
    enum { COUNT = 1025, DEPTHS = 29 };
    tide_draw_list d = {0};
    captured_instance *values = tide_alloc(COUNT * sizeof *values);
    packet *before = tide_alloc(COUNT * sizeof *before), *after = tide_alloc(COUNT * sizeof *after);
    uint32_t *order = tide_alloc(COUNT * sizeof *order);
    uint32_t next = 0;
    for (uint32_t z = DEPTHS; z > 0; z--) for (uint32_t i = 0; i < COUNT; i++) {
        if (i % DEPTHS == z - 1u) order[next++] = i;
    }
    for (uint32_t tick = 0; tick < 12; tick++) {
        frame(&d);
        const uint32_t shape = mesh(&d, 71, false);
        uint32_t uniforms[12] = {tick};
        for (uint32_t i = 0; i < COUNT; i++) values[i] = instance(i + tick * COUNT, (float)(i % DEPTHS + 1u), false);
        const int32_t first = TransparentMark(&d);
        tide_draw_shaded_instances(&d, &shaded, shape, values, COUNT, uniforms, NULL, 0);
        TIDE_REQUIRE(capture(&d, before, COUNT) == COUNT);
        const uint32_t used = d.instance_used;
        for (uint32_t repeat = 0; repeat < 3; repeat++) {
            SortTransparent(&d, first, tide_f3(0, 0, 0), tide_f3(0, 0, 1));
            tide_draw_prepare(&d, NULL);
            TIDE_REQUIRE(capture(&d, after, COUNT) == COUNT);
            check_order(before, after, order, COUNT);
            TIDE_CHECK(d.instance_used == used && d.run_count == 1);
        }
    }
    tide_draw_free(&d);
    free(order); free(after); free(before); free(values);
}

TIDE_TEST(render3d_transparent_uses_transformed_cached_centers_and_origin_fallback)
{
    enum { CACHED_BOUNDS, ORIGIN_FALLBACK, LOD_CENTER, MODES };
    static const tide_vertex3 off_center[] = {
        {{9.75f, -0.25f, 0}, {0, 0}, {1, 1, 1, 1}},
        {{10.25f, -0.25f, 0}, {1, 0}, {1, 1, 1, 1}},
        {{10, 0.25f, 0}, {0.5f, 1}, {1, 1, 1, 1}},
    };
    static const tide_mesh_levels levels = {.center = {10, 0, 0}, .radius = 0.5f, .count = 1,
        .error = {0.1f}, .indices = {3}, .index = indices, .index_count = 3};
    for (uint32_t mode = 0; mode < MODES; mode++) {
        // Fresh geometry must not inherit a previous frame's cached AABB.
        tide_draw_list d = {0};
        frame(&d);
        tide_draw_set_frustum_culling(&d, mode == CACHED_BOUNDS);
        bool fill;
        const uint32_t shape = tide_draw_mesh_slot(&d, TIDE_MESH_BYTES, 81u + mode, 91u + mode, 1, &fill);
        TIDE_REQUIRE(shape && fill);
        tide_draw_mesh_fill_bytes(&d, shape, off_center, sizeof *off_center, 3, indices, 3);
        if (mode == LOD_CENTER) {
            tide_draw_mesh_levels(&d, shape, &levels);
            tide_draw_set_lod(&d, 1);
        }
        const uint32_t images[] = {texture(&d, 811, 0xff13579b), texture(&d, 812, 0xff2468ac)};
        uint32_t uniforms[12];
        for (uint32_t i = 0; i < 12; i++) uniforms[i] = 201u + i;
        captured_instance values[] = {instance(0, 1, false), instance(1, 30, false), instance(2, 1, false)};
        values[0].world = (tide_float4x4){.c0 = {0, 0, 2, 0}, .c1 = {0, 3, 0, 0},
            .c2 = {-4, 0, 0, 0}, .c3 = {0, 0, 1, 1}};
        values[1].world = (tide_float4x4){.c0 = {0, 0, -1, 0}, .c1 = {0, 1, 0, 0},
            .c2 = {1, 0, 0, 0}, .c3 = {0, 0, 30, 1}};
        values[2].world = values[0].world;
        const int32_t first = TransparentMark(&d);
        TIDE_REQUIRE(first >= 0);
        tide_draw_shaded_instances(&d, &shaded, shape, values, 3, uniforms, images, 23);
        TIDE_CHECK(d.meshes[shape - 1u].bounds.set == (mode == CACHED_BOUNDS ? 1u : 0u));
        TIDE_CHECK(d.meshes[shape - 1u].bounds_position == (mode == CACHED_BOUNDS ? 1u : 0u));
        packet before[3], after[3];
        TIDE_REQUIRE(capture(&d, before, 3) == 3);
        for (uint32_t i = 0; i < 3; i++) TIDE_CHECK(before[i].level == (mode == LOD_CENTER ? 1u : 0u));
        if (mode == CACHED_BOUNDS) {
            TIDE_CHECK(d.meshes[shape - 1u].bounds.center.x == 10
                && d.meshes[shape - 1u].bounds.center.y == 0 && d.meshes[shape - 1u].bounds.center.z == 0);
        }
        // Local center (10,0,0) becomes z=21 for A and z=20 for B.
        // Origins instead put B (z=30) ahead of A (z=1); A's tie stays stable.
        static const uint32_t center_order[] = {0, 2, 1}, origin_order[] = {1, 0, 2};
        const uint32_t *order = mode == ORIGIN_FALLBACK ? origin_order : center_order;
        for (uint32_t repeat = 0; repeat < 2; repeat++) {
            SortTransparent(&d, first, tide_f3(0, 0, 0), tide_f3(0, 0, 1));
            TIDE_REQUIRE(capture(&d, after, 3) == 3);
            check_order(before, after, order, 3);
            tide_draw_prepare(&d, NULL);
            TIDE_REQUIRE(capture(&d, after, 3) == 3);
            check_order(before, after, order, 3);
        }
        tide_draw_free(&d);
    }
}
