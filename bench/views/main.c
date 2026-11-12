// Views run once for each of many entities, timed a view at a time
// (bench/views/game.tide): `bench_views [frames]` prints each view's average
// over the frames after the first, the frame blended halfway between two ticks
// (or by `bench_views <frames> <alpha>`); `bench_views <frames> <alpha> threads`
// hands the frame's parallel loops the platform's pool, as a host does.
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide/profile.h"
#ifdef TIDE_BENCH_VIEWS_THREADS
#include "tide/platform.h"
#endif

static tide_world world, before;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static tide_profile profile;

int main(int argc, char **argv)
{
    const int frames = argc > 1 ? atoi(argv[1]) : 30;
    if (frames < 2) {
        fprintf(stderr, "usage: bench_views [frames, 2 or more] [alpha] [threads]\n");
        return 2;
    }
    const float alpha = argc > 2 ? (float)atof(argv[2]) : 0.5f;
    const tide_jobs *pool = NULL;
#ifdef TIDE_BENCH_VIEWS_THREADS
    if (argc > 3) pool = tide_platform_jobs();
#endif
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    tide_world_tick(&world);
    tide_world_copy(&before, &world);
    tide_world_tick(&world);
    gui.profile = &profile;
    for (int f = 0; f < frames; f++) {
        tide_draw_reset(&draw);
        tide_gui_begin(&gui, &devices, tide_f2(640.0f, 360.0f), NULL);
        const tide_jobs *const loops = tide_loops_enter(pool);
        tide_frame(&world, &before, alpha, NULL, NULL, 1.0f, &local, &draw, &gui);
        tide_loops_leave(loops);
        tide_gui_end(&gui, &draw);
        if (f == 0) tide_profile_clear(&profile); // The first made the singletons' lists
    }
    printf("Views over 20,000 entities, ms a frame (%d frames, %u threads):\n", frames - 1,
           pool ? (unsigned)pool->threads : 1u);
    for (uint32_t v = 0; v < profile.views.count; v++) {
        printf("  %-12s %8.3f\n", profile.views.items[v].name, profile.views.items[v].total / (frames - 1));
    }
    tide_profile_free(&profile);
    tide_draw_free(&draw);
    tide_local_free(&local);
    tide_world_free(&before);
    tide_world_free(&world);
    return 0;
}
