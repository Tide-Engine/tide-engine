#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "tide/draw.h"
#include "tide/heap.h"
#include "tide/list.h"
#include "tide/mesh.h"
#include "tide/page.h"
#include "tide/text.h"
#include "frustum_cull.h"    // The CPU's exact answers, engine/src
#include "list_data_cache.h" // tide_draw_shaded_culled, engine/src
#include "tide_test.h"

// Listed draws the GPU culls (tide_draw_list.gpu_culling): what a frame records for them, decided by the CPU
// (tide_draw_uncull), is what the CPU path draws, level by level, in order, mirrored ones apart; the levels each
// draw's runs cover hold every pick; and the GPU's own float decisions (cull.tide's, followed here in C, with and
// without fused multiply-adds) never cull what the CPU draws and pick within those levels.

static const tide_vertex3 corners[4] = {
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
};
static const uint32_t whole[12] = {0, 1, 2, 0, 2, 3, 0, 1, 3, 1, 2, 3};
static const uint32_t simpler[12] = {0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 1, 3};
// Three levels: within a pixel 0.01, 0.05 and 0.2 off, from about 5, 26 and 104 units out through the camera below
static const tide_mesh_levels three_levels = {
    .center = {0.0f, 0.0f, 0.0f}, .radius = 0.75f, .count = 3, .error = {0.01f, 0.05f, 0.2f}, .first = {0, 6, 9},
    .indices = {6, 3, 3}, .index = simpler, .index_count = 12};

static const tide_shader listed_shader = {
    .name = "listed", .dims = 3, .vertex_size = sizeof(tide_vertex3), .instance_floats = 5, .instance_ints = 1,
    .bounds_position = 1 + offsetof(tide_vertex3, position), .listed = true,
};

typedef struct item {
    tide_float4x4 transform;
    float value;
    int32_t kind;
} item;

typedef struct instance {
    float f[20];
    int32_t i[4];
} instance;

static void write_item(const void *from, int32_t *words)
{
    const item *it = from;
    instance out;
    memset(&out, 0, sizeof out);
    memcpy(out.f, &it->transform, sizeof it->transform);
    out.f[16] = it->value;
    out.i[0] = it->kind;
    memcpy(words, &out, sizeof out);
}

static uint32_t roll(uint32_t *state)
{
    *state = *state * 747796405u + 2891336453u;
    const uint32_t w = ((*state >> ((*state >> 28) + 4u)) ^ *state) * 277803737u;
    return (w >> 22) ^ w;
}

static float between(uint32_t *state, const float low, const float high)
{
    return low + (high - low) * (float)(roll(state) >> 8) * (1.0f / 16777216.0f);
}

// How a list's instances are mirrored: none, all, or every third
enum { NONE_MIRRORED, ALL_MIRRORED, SOME_MIRRORED };

// A list of `count` instances, turned about y, scaled and placed around and before the camera.
static tide_list scattered(const int32_t count, const uint32_t seed, const int mirrored, const float spread)
{
    uint32_t state = seed;
    tide_list list = {0};
    tide_list_resize(&list, count, sizeof(item), TIDE_IN_LOCAL);
    const float cx = between(&state, -spread, spread), cz = between(&state, -spread * 0.3f, spread * 1.5f);
    for (int32_t i = 0; i < count; i++) {
        item *it = tide_list_at_mut(list, i, sizeof(item));
        memset(it, 0, sizeof *it);
        const float angle = between(&state, 0.0f, 6.2831853f), scale = between(&state, 0.5f, 2.0f);
        const float c = cosf(angle) * scale, s = sinf(angle) * scale;
        const bool flip = mirrored == ALL_MIRRORED || (mirrored == SOME_MIRRORED && i % 3 == 0);
        it->transform = tide_identity_f4x4();
        it->transform.c0 = tide_f4(flip ? -c : c, 0, flip ? s : -s, 0);
        it->transform.c1 = tide_f4(0, scale, 0, 0);
        it->transform.c2 = tide_f4(s, 0, c, 0);
        it->transform.c3 = tide_f4(cx + between(&state, -spread * 0.2f, spread * 0.2f), between(&state, -6.0f, 6.0f),
                                   cz + between(&state, -spread * 0.2f, spread * 0.2f), 1.0f);
        it->value = (float)i;
        it->kind = (int32_t)seed;
    }
    return list;
}

// Its instances all before the camera.
static void in_view(const tide_list list, uint32_t seed)
{
    for (int32_t i = 0; i < tide_list_count(list); i++) {
        item *it = tide_list_at_mut(list, i, sizeof(item));
        it->transform.c3 = tide_f4(between(&seed, -3.0f, 3.0f), between(&seed, -2.0f, 2.0f), between(&seed, 8.0f, 20.0f), 1.0f);
    }
}

static void frame(tide_draw_list *d, const bool gpu)
{
    tide_draw_reset(d);
    d->gpu_culling = gpu;
    tide_draw_screen_size(d, tide_f2(800.0f, 600.0f));
    tide_draw_set_camera_3d(d, tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
}

// The list's mesh drawn last: its levels, given once it's there.
static void give_levels(tide_draw_list *d)
{
    for (uint32_t i = 0; i < d->run_count; i++) {
        const uint32_t mesh = d->runs[i].mesh;
        if (!mesh || mesh > d->mesh_count || d->meshes[mesh - 1u].base || d->meshes[mesh - 1u].levels) continue;
        tide_draw_mesh_levels(d, mesh, &three_levels);
    }
}

static bool draw(tide_draw_list *d, const tide_list items)
{
    const tide_list vertices = tide_list_from(corners, 4, sizeof(tide_vertex3));
    const tide_list indices = tide_list_from(whole, 12, sizeof(uint32_t));
    return tide_draw_mesh_shaded_list(d, &listed_shader, vertices, indices, items, sizeof(item), write_item, 0x80000001u,
                                      NULL, NULL, 0);
}

// What a list draws, level by level and side by side (mirrored or not), each in order: the instances' bytes.
typedef struct drawn {
    instance *each[TIDE_MESH_LEVELS + 1][2];
    uint32_t count[TIDE_MESH_LEVELS + 1][2];
} drawn;

static void drawn_of(const tide_draw_list *d, drawn *out)
{
    memset(out, 0, sizeof *out);
    for (uint32_t i = 0; i < d->count; i++) {
        const tide_draw_command *c = &d->commands[i];
        if (c->kind != TIDE_DRAW_SHADED || c->shaded.run >= d->run_count) continue;
        const tide_draw_run *r = &d->runs[c->shaded.run];
        if (!r->shader || !r->mesh || !r->count) continue;
        TIDE_CHECK(!r->culled);
        const uint32_t level = d->meshes[r->mesh - 1u].level, side = (c->cull & TIDE_DRAW_MIRRORED) != 0;
        TIDE_REQUIRE(level <= TIDE_MESH_LEVELS);
        out->each[level][side] = tide_realloc(out->each[level][side], out->count[level][side] * sizeof(instance),
                                              (out->count[level][side] + r->count) * sizeof(instance));
        for (uint32_t k = 0; k < r->count; k++) {
            const void *bytes = tide_draw_run_instance(d, r, k);
            TIDE_REQUIRE(bytes);
            memcpy(&out->each[level][side][out->count[level][side]++], bytes, sizeof(instance));
        }
    }
}

static void same_drawn(const tide_draw_list *culled, const tide_draw_list *cpu)
{
    drawn a, b;
    drawn_of(culled, &a);
    drawn_of(cpu, &b);
    for (uint32_t level = 0; level <= TIDE_MESH_LEVELS; level++) {
        for (uint32_t side = 0; side < 2; side++) {
            TIDE_CHECK(a.count[level][side] == b.count[level][side]);
            if (a.count[level][side] == b.count[level][side] && a.count[level][side]) {
                TIDE_CHECK(memcmp(a.each[level][side], b.each[level][side], a.count[level][side] * sizeof(instance)) == 0);
            }
            free(a.each[level][side]);
            free(b.each[level][side]);
        }
    }
}

static uint32_t culled_runs(const tide_draw_list *d)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < d->run_count; i++) n += d->runs[i].culled != 0;
    return n;
}

// cull.tide's Pick, in floats as the GPU works them out: each a * b + c fused, or not.
static float madd(const float a, const float b, const float c, const bool fused)
{
    return fused ? fmaf(a, b, c) : a * b + c;
}

static float dot4(const float row[4], const float x, const float y, const float z, const bool fused)
{
    return madd(row[2], z, madd(row[1], y, fused ? row[0] * x : row[0] * x, fused), fused) + row[3];
}

static float error_of(const tide_draw_mesh_data *m, const uint32_t level)
{
    return level < m->levels ? m->level_error[level] : 0.0f;
}

static uint32_t gpu_pick(const tide_draw_cull_camera *camera, const tide_draw_mesh_data *m, const float *c, const bool fused)
{
    const uint32_t levels = m->levels;
    if (camera->lod_level) return camera->lod_level - 1u < levels ? camera->lod_level - 1u : levels;
    if (!(camera->lod[0] > 0.0f) || !levels) return 0;
    const float x = m->center[0], y = m->center[1], z = m->center[2];
    const float row_x[4] = {c[0], c[4], c[8], c[12]}, row_y[4] = {c[1], c[5], c[9], c[13]};
    const float row_z[4] = {c[2], c[6], c[10], c[14]};
    const float cx = dot4(row_x, x, y, z, fused), cy = dot4(row_y, x, y, z, fused), cz = dot4(row_z, x, y, z, fused);
    const float sx = madd(c[2], c[2], madd(c[1], c[1], c[0] * c[0], fused), fused);
    const float sy = madd(c[6], c[6], madd(c[5], c[5], c[4] * c[4], fused), fused);
    const float sz = madd(c[10], c[10], madd(c[9], c[9], c[8] * c[8], fused), fused);
    const float most = sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz);
    const float big = sqrtf(most), r = m->radius * big;
    const float *w4 = &camera->lod[1], *x4 = &camera->lod[6], *y4 = &camera->lod[10];
    const float wl = camera->lod[5], xl = camera->lod[14], yl = camera->lod[15], scale = camera->lod[0];
    const float w = dot4(w4, cx, cy, cz, fused);
    float away = w - r * wl;
    const float side = dot4(x4, cx, cy, cz, fused), rise = dot4(y4, cx, cy, cz, fused);
    const float out_x = fabsf(side) - r * (xl + 2.0f * wl), out_y = fabsf(rise) - r * (yl + 2.0f * wl);
    const float behind = -w - 3.0f * r * wl;
    if (out_x > away) away = out_x;
    if (out_y > away) away = out_y;
    if (behind > away) away = behind;
    if (!(away > 0.0f)) return 0;
    const float allowed = away / (big * scale) * m->lod_multiplier;
    uint32_t level = 0;
    while (level < levels && error_of(m, level) <= allowed) level++;
    return level;
}

// ...and its Outside, with the GPU's planes (tide_draw_cull_planes).
static bool gpu_outside(const float planes[72], const tide_draw_bounds *b, const float *c, const bool fused)
{
    float at[3], radius[3], magnitude[3];
    for (uint32_t k = 0; k < 3; k++) {
        at[k] = madd(c[8 + k], b->center.z, madd(c[4 + k], b->center.y, madd(c[k], b->center.x, c[12 + k], fused), fused), fused);
        radius[k] = madd(fabsf(c[8 + k]), b->extents.z, madd(fabsf(c[4 + k]), b->extents.y, fabsf(c[k]) * b->extents.x, fused), fused);
        magnitude[k] = madd(fabsf(c[8 + k]), fabsf(b->center.z),
                            madd(fabsf(c[4 + k]), fabsf(b->center.y), madd(fabsf(c[k]), fabsf(b->center.x), fabsf(c[12 + k]), fused), fused),
                            fused) + radius[k];
    }
    const float size = (magnitude[0] + magnitude[1]) + magnitude[2];
    bool outside = false;
    for (uint32_t p = 0; p < 6; p++) {
        const float *n = planes + p * 12u, *s = n + 4, *t = n + 8;
        float distance = madd(n[0], at[0], n[3], fused);
        distance = madd(n[1], at[1], distance, fused);
        distance = madd(n[2], at[2], distance, fused);
        distance = madd(s[0], radius[0], distance, fused);
        distance = madd(s[1], radius[1], distance, fused);
        distance = madd(s[2], radius[2], distance, fused);
        if (madd(s[3], size, distance, fused) < t[0]) outside = true;
    }
    return outside;
}

// Every candidate of the list's culled draws: the CPU's pick is within its draw's levels, and so are the GPU's;
// and what the GPU culls the CPU does. How many the GPU draws that the CPU culls is counted in `slack`.
static void gpu_agrees(const tide_draw_list *d, uint32_t *slack, uint32_t *candidates)
{
    for (uint32_t j = 0; j < d->cull_count; j++) {
        const tide_draw_cull *job = &d->culls[j];
        const tide_draw_cull_camera *camera = &d->cull_cameras[job->camera];
        const tide_draw_mesh_data *m = &d->meshes[job->mesh - 1u];
        float planes[72];
        const bool culls = tide_draw_cull_planes(camera, planes);
        tide_draw_frustum f;
        cull_frustum_build(&f, camera->world_to_clip);
        const cull_box box = cull_box_of(&job->bounds, &f);
        const tide_draw_texture *page = &d->textures[job->page - 1u];
        for (uint32_t i = 0; i < job->count; i++) {
            const float *c = (const float *)(const void *)(page->pixels + ((size_t)job->first + i) * job->packs * 16u);
            const int cpu = cull_instance(&f, &box, c, NULL);
            for (int fused = 0; fused < 2; fused++) {
                const uint32_t level = gpu_pick(camera, m, c, fused != 0);
                TIDE_CHECK(level >= job->low && level <= job->high);
                const bool out = culls && job->bounds.set == 1 && gpu_outside(planes, &job->bounds, c, fused != 0);
                TIDE_CHECK(!out || cpu == CULL_REJECTED);
                if (!fused && !out && cpu == CULL_REJECTED) (*slack)++;
            }
            (*candidates)++;
        }
    }
}

TIDE_TEST(draw_culled_runs_draw_what_the_cpu_draws)
{
    const uint64_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    enum { LISTS = 12 };
    tide_list lists[LISTS];
    for (int i = 0; i < LISTS; i++) {
        lists[i] = scattered(40 + i * 37, 101u + (uint32_t)i * 7919u, i == 4 ? ALL_MIRRORED : i == 7 ? SOME_MIRRORED : NONE_MIRRORED,
                             i < 6 ? 30.0f : 150.0f);
    }
    tide_draw_list gpu = {0}, cpu = {0};
    uint32_t slack = 0, candidates = 0;
    for (int round = 0; round < 7; round++) {
        frame(&gpu, true);
        frame(&cpu, false);
        for (int pass = 0; pass < 2; pass++) {
            tide_draw_list *d = pass ? &cpu : &gpu;
            if (round == 3) tide_draw_set_lod(d, 2);            // Every one at a level
            if (round == 4) tide_draw_set_lod_pixels(d, 9.5f);   // Coarser
            if (round == 5) tide_draw_set_frustum_culling(d, false);
            if (round == 6) tide_draw_set_camera_3d(d, tide_f3(3.0f, 1.0f, 40.0f), tide_euler_q(tide_f3(0.2f, 2.7f, 0.0f)), 45.0f);
            for (int i = 0; i < LISTS; i++) {
                TIDE_CHECK(draw(d, lists[i]));
                if (i == 2 || i == 9) TIDE_CHECK(draw(d, lists[i])); // Again: a set of the same list's draws
            }
            if (round == 0) give_levels(d); // From the next frame on
        }
        if (round) {
            TIDE_CHECK(culled_runs(&gpu) > 0);
            gpu_agrees(&gpu, &slack, &candidates);
            // The list whose transforms mirror and don't is the CPU's to draw
            tide_draw_prepare(&gpu, NULL);
            tide_draw_prepare(&cpu, NULL);
            tide_draw_uncull(&gpu);
            TIDE_CHECK(culled_runs(&gpu) == 0);
            same_drawn(&gpu, &cpu);
        }
    }
    TIDE_CHECK(candidates > 10000);
    TIDE_CHECK(slack < candidates / 100); // The GPU's slack draws few the CPU culls
    tide_draw_free(&gpu);
    tide_draw_free(&cpu);
    for (int i = 0; i < LISTS; i++) tide_list_release(&lists[i], TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// Instances right at the frustum's planes, a few floats either side of where the CPU's exact test turns: the GPU
// culls none the CPU draws.
TIDE_TEST(draw_culled_gpu_never_culls_what_the_cpu_draws)
{
    uint32_t state = 77u;
    tide_draw_list d = {0};
    frame(&d, true);
    tide_draw_frustum f;
    const tide_float4x4 clip = tide_draw_world_to_clip(&d, d.screen);
    const tide_draw_cull_camera camera = {0};
    tide_draw_cull_camera at = camera;
    memcpy(at.world_to_clip, &clip, sizeof at.world_to_clip);
    cull_frustum_build(&f, at.world_to_clip);
    float planes[72];
    TIDE_REQUIRE(tide_draw_cull_planes(&at, planes));
    const tide_draw_bounds bounds = {.center = {0.1f, -0.2f, 0.0f}, .extents = {0.5f, 0.5f, 0.25f}, .set = 1};
    const cull_box box = cull_box_of(&bounds, &f);
    uint32_t tested = 0, both = 0, far = 0;
    for (int i = 0; i < 4000; i++) {
        // A line from a place surely in view to one surely out of it, bisected to where the CPU's answer turns
        const float scale = between(&state, 0.2f, 3.0f), angle = between(&state, 0.0f, 6.2831853f);
        float m[16] = {cosf(angle) * scale, 0, -sinf(angle) * scale, 0, 0, scale, 0, 0, sinf(angle) * scale, 0,
                       cosf(angle) * scale, 0, 0, 0, 0, 1};
        const float inside[3] = {between(&state, -1.0f, 1.0f), between(&state, -1.0f, 1.0f), between(&state, 10.0f, 50.0f)};
        const float outside[3] = {between(&state, -200.0f, 200.0f), between(&state, -200.0f, 200.0f), between(&state, -50.0f, 2000.0f)};
        float low = 0.0f, high = 1.0f;
        for (int step = 0; step < 40; step++) {
            const float t = (low + high) * 0.5f;
            for (int k = 0; k < 3; k++) m[12 + k] = inside[k] + (outside[k] - inside[k]) * t;
            if (cull_exact(&bounds, &f, m) == CULL_REJECTED) high = t;
            else low = t;
        }
        for (int k = 0; k < 3; k++) m[12 + k] = inside[k] + (outside[k] - inside[k]) * high;
        for (int nudge = -3; nudge <= 3; nudge++) {
            float n[16];
            memcpy(n, m, sizeof n);
            for (int k = 0; k < 3; k++) {
                for (int s = 0; s < (nudge < 0 ? -nudge : nudge); s++) n[12 + k] = nextafterf(n[12 + k], nudge < 0 ? -INFINITY : INFINITY);
            }
            const int cpu = cull_instance(&f, &box, n, NULL);
            for (int fused = 0; fused < 2; fused++) {
                const bool out = gpu_outside(planes, &bounds, n, fused != 0);
                TIDE_CHECK(!out || cpu == CULL_REJECTED);
            }
            tested++;
        }
        // ...and well past the edge, it culls what the CPU does
        memcpy(&m[12], outside, sizeof outside);
        if (cull_instance(&f, &box, m, NULL) == CULL_REJECTED) {
            far++;
            both += gpu_outside(planes, &bounds, m, false) && gpu_outside(planes, &bounds, m, true);
        }
    }
    TIDE_CHECK(tested == 28000);
    TIDE_CHECK(far > 300 && both * 50u >= far * 49u); // Those it doesn't are near an edge on their way out
    tide_draw_free(&d);
}

// What the CPU path draws instead, and what reads a culled run's instances: blended draws that read depth, a list
// whose transforms mirror and don't, a host that doesn't ask for it; appending and unlisting give the CPU's.
TIDE_TEST(draw_culled_declines_and_hands_over)
{
    const uint64_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    tide_list plain = scattered(300, 5u, NONE_MIRRORED, 20.0f), mixed = scattered(200, 6u, SOME_MIRRORED, 20.0f);
    tide_list mirrored = scattered(100, 8u, ALL_MIRRORED, 20.0f);
    in_view(plain, 1u);
    in_view(mixed, 2u);
    in_view(mirrored, 3u);
    tide_draw_list d = {0}, cpu = {0}, appended = {0};
    for (int round = 0; round < 2; round++) {
        frame(&d, true);
        frame(&cpu, false);
        for (int pass = 0; pass < 2; pass++) {
            tide_draw_list *l = pass ? &cpu : &d;
            TIDE_CHECK(draw(l, plain));
            TIDE_CHECK(draw(l, mixed));
            TIDE_CHECK(draw(l, mirrored));
            tide_draw_set_depth(l, TIDE_DEPTH_READ);
            tide_draw_set_blend(l, TIDE_BLEND_ALPHA);
            TIDE_CHECK(draw(l, plain));
            tide_draw_set_depth(l, TIDE_DEPTH_READ_WRITE);
            tide_draw_set_blend(l, TIDE_BLEND_ALPHA);
            if (!round) give_levels(l);
        }
    }
    // The plain list's and the mirrored one's draws are culled, in runs of their own; the others are as ever
    uint32_t mirrored_runs = 0;
    for (uint32_t i = 0; i < d.count; i++) {
        const tide_draw_command *c = &d.commands[i];
        if (c->kind != TIDE_DRAW_SHADED) continue;
        const tide_draw_run *r = &d.runs[c->shaded.run];
        mirrored_runs += r->culled && (c->cull & TIDE_DRAW_MIRRORED);
        if (c->depth == TIDE_DEPTH_READ) TIDE_CHECK(!r->culled);
    }
    TIDE_CHECK(mirrored_runs > 0 && culled_runs(&d) > 0 && d.cull_count == 2);
    for (uint32_t j = 0; j < d.cull_count; j++) TIDE_CHECK(d.culls[j].count == 300 || d.culls[j].count == 100);
    // Appended, the CPU decides them
    tide_draw_append(&appended, &d);
    TIDE_CHECK(culled_runs(&appended) == 0 && culled_runs(&d) == 0);
    tide_draw_prepare(&cpu, NULL);
    same_drawn(&d, &cpu);
    // A frame not asked culls nothing on the GPU, and unlisting a culled run gives its bytes
    frame(&d, false);
    TIDE_CHECK(draw(&d, plain) && culled_runs(&d) == 0);
    frame(&d, true);
    TIDE_CHECK(draw(&d, plain) && culled_runs(&d) > 0);
    for (uint32_t i = 0; i < d.run_count; i++) {
        tide_draw_run_unlist(&d, i);
        TIDE_CHECK(!d.runs[i].culled && !d.runs[i].listed);
    }
    tide_draw_free(&appended);
    tide_draw_free(&d);
    tide_draw_free(&cpu);
    tide_list_release(&plain, TIDE_IN_LOCAL);
    tide_list_release(&mixed, TIDE_IN_LOCAL);
    tide_list_release(&mirrored, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// Draw.Occlusion: a destination's own, off each frame, put back by blocks; each culled draw carries what it was,
// and draws with it on and off don't join one set.
TIDE_TEST(draw_culled_carry_occlusion)
{
    const uint64_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    tide_list plain = scattered(300, 5u, NONE_MIRRORED, 20.0f);
    in_view(plain, 1u);
    tide_draw_list d = {0};
    frame(&d, true);
    TIDE_CHECK(d.place.occlusion == 0);
    TIDE_CHECK(draw(&d, plain));
    tide_draw_set_occlusion(&d, true);
    TIDE_CHECK(d.place.occlusion == 1);
    TIDE_CHECK(draw(&d, plain));
    TIDE_CHECK(draw(&d, plain)); // Joins the one before: both with it on
    const uint32_t saved = tide_draw_push_occlusion(&d, false);
    TIDE_CHECK(d.place.occlusion == 0);
    TIDE_CHECK(draw(&d, plain));
    tide_draw_pop(&d, saved);
    TIDE_CHECK(d.place.occlusion == 1);
    tide_draw_unset_occlusion(&d);
    TIDE_CHECK(d.place.occlusion == 0);
    TIDE_CHECK(d.cull_count == 4);
    TIDE_CHECK(!d.culls[0].occlusion && d.culls[1].occlusion && d.culls[2].occlusion && !d.culls[3].occlusion);
    // Three sets: off, on (two draws), off
    uint32_t sets = 0, joined = 0;
    for (uint32_t i = 0; i < d.run_count; i++) {
        const tide_draw_run *r = &d.runs[i];
        if (!r->culled) continue;
        bool first = true;
        for (uint32_t k = 0; k < i; k++) first &= d.runs[k].culled != r->culled;
        sets += first;
        for (uint32_t j = r->culled - 1u; j < r->culled - 1u + r->culled_count; j++) {
            TIDE_CHECK(d.culls[j].occlusion == d.culls[r->culled - 1u].occlusion);
        }
        joined += first && r->culled_count == 2;
    }
    TIDE_CHECK(sets == 3 && joined == 1);
    // A target's own, and the next frame starts with it off everywhere
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.occlusion == 0);
    tide_draw_set_occlusion(&d, true);
    tide_draw_to(&d, 0, 0, 0);
    TIDE_CHECK(d.place.occlusion == 0);
    tide_draw_set_occlusion(&d, true);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.occlusion == 1);
    frame(&d, true);
    TIDE_CHECK(d.place.occlusion == 0);
    tide_draw_to(&d, 17, 64, 64);
    TIDE_CHECK(d.place.occlusion == 0);
    tide_draw_free(&d);
    tide_list_release(&plain, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}

// Draws of one list joining a set whose levels end below theirs (Draw.LOD 1, then 3): the levels between would have
// no mesh made, so the second is a set of its own. Joining left level 2's run with an unset mesh, which the next
// draw's join read as an index (Mount & Rifle's crash in tide_draw_shaded_culled). Every culled run's mesh is one of
// the list's levels, a set's runs a level each, and what's drawn is the CPU's.
TIDE_TEST(draw_culled_join_leaves_no_level_without_a_mesh)
{
    const uint64_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_text_use(NULL, &heap);
    tide_list plain = scattered(200, 9u, NONE_MIRRORED, 20.0f);
    in_view(plain, 3u);
    tide_draw_list gpu = {0}, cpu = {0};
    for (int round = 0; round < 2; round++) {
        frame(&gpu, true);
        frame(&cpu, false);
        for (int pass = 0; pass < 2; pass++) {
            tide_draw_list *d = pass ? &cpu : &gpu;
            tide_draw_set_lod(d, 1);
            TIDE_CHECK(draw(d, plain));
            tide_draw_set_lod(d, 3);
            TIDE_CHECK(draw(d, plain));
            TIDE_CHECK(draw(d, plain)); // Joins the one before, after the set that didn't take it
            tide_draw_set_lod(d, 1);
            TIDE_CHECK(draw(d, plain));
            if (round == 0) give_levels(d);
        }
        if (!round) continue;
        TIDE_REQUIRE(gpu.cull_count == 4);
        uint32_t sets = 0;
        for (uint32_t i = 0; i < gpu.run_count; i++) {
            const tide_draw_run *r = &gpu.runs[i];
            if (!r->culled) continue;
            TIDE_REQUIRE(r->mesh && r->mesh <= gpu.mesh_count);
            const tide_draw_mesh_data *m = &gpu.meshes[r->mesh - 1u];
            const uint32_t family = m->base ? m->base : r->mesh;
            TIDE_CHECK(family == gpu.culls[r->culled - 1u].mesh);
            bool first = true;
            for (uint32_t k = 0; k < i; k++) {
                if (gpu.runs[k].culled != r->culled) continue;
                first = false;
                TIDE_CHECK(gpu.meshes[gpu.runs[k].mesh - 1u].level != m->level);
            }
            sets += first;
        }
        TIDE_CHECK(sets == 3); // Level 1; level 3 twice; level 1
        tide_draw_prepare(&gpu, NULL);
        tide_draw_prepare(&cpu, NULL);
        tide_draw_uncull(&gpu);
        same_drawn(&gpu, &cpu);
    }
    tide_draw_free(&gpu);
    tide_draw_free(&cpu);
    tide_list_release(&plain, TIDE_IN_LOCAL);
    tide_scratch_reset(mark);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
}
