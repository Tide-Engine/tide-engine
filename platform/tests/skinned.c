#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide/pack.h"
#include "tide/platform.h"
#include "tide/skinned.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

typedef struct observed_jobs {
    const tide_jobs *pool;
    void (*work)(void *context, uint32_t thread);
    void *context;
    atomic_uint callbacks, worker_callbacks;
} observed_jobs;

static void observed_work(void *context, const uint32_t thread)
{
    observed_jobs *o = context;
    atomic_fetch_add_explicit(&o->callbacks, 1u, memory_order_relaxed);
    if (thread) atomic_fetch_add_explicit(&o->worker_callbacks, 1u, memory_order_relaxed);
    o->work(o->context, thread);
}

static void observed_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    observed_jobs *o = self;
    o->work = work;
    o->context = context;
    o->pool->run(o->pool->self, observed_work, o);
}

static void observed_run_some(void *self, const uint32_t others,
                              void (*work)(void *context, uint32_t thread), void *context)
{
    observed_jobs *o = self;
    o->work = work;
    o->context = context;
    o->pool->run_some(o->pool->self, others, observed_work, o);
}

// Exercise the real pool with enough distinct captured poses to dispatch a
// round, and compare every prepared byte with the same inputs on one thread.
// A pool may legitimately finish on the caller before any worker joins.
static int parallel_palettes(void)
{
    tide_draw_list serial = {0}, parallel = {0};
    const tide_list data = tide_pack_skinned_of(tide_pack_find("model", TIDE_PACK_SKINNED), sizeof(tide_vertex3)).data;
    const tide_jobs *pool = tide_platform_jobs();
    observed_jobs observed = {.pool = pool};
    atomic_init(&observed.callbacks, 0u);
    atomic_init(&observed.worker_callbacks, 0u);
    tide_jobs jobs = {0};
    if (pool) {
        jobs = *pool;
        jobs.self = &observed;
        jobs.run = observed_run;
        jobs.run_some = pool->run_some ? observed_run_some : NULL;
    }
    uint32_t first[2] = {0};
    int failures = 0;
    for (uint32_t i = 0; i < 2048u; i++) {
        const uint32_t mark = tide_scratch_mark();
        tide_list pose = {0};
        uint32_t textures[2];
        int32_t info[4];
        bool valid = tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH);
        tide_bone_transform *bone = valid ? tide_list_at_mut(pose, 1, sizeof *bone) : NULL;
        if (bone) bone->position.x += (float)i * 0.001f;
        valid = bone && tide_draw_skinned_atlas(&serial, data, pose, textures, info);
        if (valid && !i) first[0] = textures[0];
        valid = valid && tide_draw_skinned_atlas(&parallel, data, pose, textures, info);
        if (valid && !i) first[1] = textures[0];
        tide_scratch_reset(mark);
        if (!valid) { failures++; break; }
    }
    if (!failures) {
        tide_draw_prepare(&serial, NULL);
        tide_draw_prepare(&parallel, pool ? &jobs : NULL);
        const tide_draw_texture *a = &serial.textures[first[0] - 1u], *b = &parallel.textures[first[1] - 1u];
        const bool same = a->width == b->width && a->height == b->height && a->format == b->format
            && memcmp(a->pixels, b->pixels, (size_t)a->width * (size_t)a->height * 16u) == 0;
        printf("%s: 2048 independent palettes match serial preparation (pool %u, callbacks %u, workers %u)\n",
               same ? "ok" : "FAIL", pool ? pool->threads : 1u,
               atomic_load_explicit(&observed.callbacks, memory_order_relaxed),
               atomic_load_explicit(&observed.worker_callbacks, memory_order_relaxed));
        if (!same) failures++;
    }
    tide_draw_free(&serial);
    tide_draw_free(&parallel);
    return failures;
}

static int frame(void *user, const float seconds)
{
    (void)user;
    (void)seconds;
    printf("renderer: %s\n", tide_platform_renderer());
    int failures = parallel_palettes();
    static const struct {
        tide_float2 at;
        uint32_t color;
        const char *what;
    } checks[] = {
        {{20, 20}, 0xffffffffu, "built-in Vertex3 draws its rest pose"},
        {{20, 52}, 0x000000ffu, "animated geometry leaves its original position"},
        {{44, 52}, 0xffffffffu, "eight influences deform on the GPU before drawing"},
        {{108, 20}, 0x39e4ffffu, "custom vert sees inverse-transpose normals, tangents and default fields"},
        {{84, 20}, 0x000000ffu, "custom vert moves the already deformed geometry"},
        {{20, 84}, 0x000000ffu, "the shadow pass leaves the original position too"},
        {{44, 84}, 0xff00ffffu, "render-target shadow and color use the same pose"},
        {{84, 52}, 0xff0000ffu, "a tinted instance draws another independent rest pose"},
        {{108, 84}, 0x0000ffffu, "a tinted animated instance preserves earlier palette captures"},
        {{52, 116}, 0x00ffffffu, "this.clip sees skinning and explicit placement retains posed world coordinates"},
        {{28, 116}, 0x000000ffu, "explicit clip placement uses the posed vertex"},
        {{116, 116}, 0x000000ffu, "the engine does not replace an explicit clip position"},
    };
    enum { COUNT = sizeof checks / sizeof checks[0] };
    for (int repeat = 0; repeat < 2; repeat++) {
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, tide_f2(128, 128), tide_platform_measure_text);
        tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
        tide_gui_end(&gui, &draw);
        tide_float2 points[COUNT];
        uint32_t pixels[COUNT];
        for (int i = 0; i < COUNT; i++) points[i] = checks[i].at;
        tide_platform_read_pixels_as(&draw, 128, 128, 1.0f, points, COUNT, pixels);
        for (int i = 0; i < COUNT; i++) {
            bool same = true;
            for (int c = 0; c < 3; c++) {
                const int actual = (int)(pixels[i] >> (24 - 8 * c) & 255u);
                const int expected = (int)(checks[i].color >> (24 - 8 * c) & 255u);
                same = same && actual >= expected - 5 && actual <= expected + 5;
            }
            printf("%s: %s%s (%08X, expected %08X)\n", same ? "ok" : "FAIL", checks[i].what,
                   repeat ? ", again" : "", (unsigned)pixels[i], (unsigned)checks[i].color);
            if (!same) failures++;
        }
    }
    return failures;
}

int main(void)
{
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_platform_open(&(tide_window_desc){.title = "skinned", .width = 128, .height = 128, .hidden = true});
    tide_platform_run(frame, NULL);
}
