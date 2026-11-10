// The standard host (tide/host.h) over prewarm.tide, whose game draws a mesh
// from its first frame and, once the test says (Later), a mesh of the same
// kind into a target and a blit into the window. The host has the renderer
// make the pipelines of its shaders (tidec's tide_game_shaders) before they
// draw (tide_platform_prewarm), with background pipelines on where the
// backend has them, as a game's host has them, and starts it as a game's host
// does (tide_run_begin_loaded): its loading frames go on till the pipelines it
// starts with are made. Run in this order by prewarm.cmake, natively, the
// game's temporary cache folder the script's:
//
//   prewarm first     no file of how the game's shaders drew: nothing of the game's is made as it starts; once its
//                     first frame drew the mesh, the late mesh, of its kind, is made as that was, and its first frame
//                     makes no pipeline but the blit's, the first of its kind
//   prewarm again     the file the last run left: all three are made as the game starts, and the late frame makes
//                     none and draws them all
//   prewarm corrupt   a byte of the file changed: it's left out, as there were none, and written over whole
//   prewarm cut       the file cut short: left out too
//
// In a page, which keeps no file, a run is the first's.

#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/run.h"
#ifndef __wasm__
#include "files.h"
#endif

uint32_t tide_platform_shader_pipelines(void); // The renderer's, for a test (platform/src/gpu.h)

// A bound on a hang, not on speed: headless frames take a millisecond or so, and the pool's threads compiling the
// game's pipelines with the driver's cache cold under ASan took more than 600 of them (2026-11-06)
enum { WAIT_FRAMES = 6000 };

#define GAME_ID "dev.tide.prewarm" // What the host names the game, whose temporary cache folder has the file

static int failures;
static bool again; // The file the last run left is read
static bool later;

bool Later(void)
{
    return later;
}

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

// The file as the mode has it before the run: false where it can't be.
static bool spoil(const char *mode)
{
#ifdef __wasm__
    (void)mode;
    return true;
#else
    const bool corrupt = strcmp(mode, "corrupt") == 0, cut = strcmp(mode, "cut") == 0;
    if (!corrupt && !cut) return true;
    tide_platform_name_game(GAME_ID);
    const char *folder = tide_platform_cache_folder();
    char file[1024];
    snprintf(file, sizeof file, "%s/pipelines.keys", folder ? folder : ".");
    size_t size = 0;
    uint8_t *bytes = tide_file_read_whole(file, 1u << 20, &size);
    if (!bytes || size < 48u) {
        printf("FAIL: the last run left no file of how the game's shaders drew (%zu bytes)\n", size);
        return false;
    }
    if (corrupt) bytes[size - 20u] ^= 0x5Au; // In the last key: a whole file, whose hash says it isn't
    else size = 24u + (size - 24u) / 2u;
    const bool written = tide_file_write_whole(file, bytes, size);
    free(bytes);
    return written;
#endif
}

#ifndef __wasm__
// Work queued for the pool's threads that takes long, as a pipeline the driver compiles does: more of it than the
// pool has threads, so some is always waiting its turn while the loader's frames go on. A frame never does one
// itself: it would hold the window for as long as the work takes (seconds, for a big game's pipelines). Each ends
// once it's let go, or at once on the frame's thread, saying so.
enum { SLOW_JOBS = 80 };
static tide_background slow_jobs[SLOW_JOBS];
static uint32_t slow_release, slow_on_frame_thread, slow_queued;
static TIDE_THREAD_LOCAL bool frame_thread;

static void slow_work(void *context)
{
    (void)context;
    if (frame_thread) {
        __atomic_add_fetch(&slow_on_frame_thread, 1u, __ATOMIC_RELAXED);
        return;
    }
    while (!__atomic_load_n(&slow_release, __ATOMIC_ACQUIRE)) { // A few frames: it only waits
    }
}

static void slow_begin(void)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs || !jobs->background) return;
    for (uint32_t i = 0; i < SLOW_JOBS; i++) {
        slow_jobs[i] = (tide_background){.work = slow_work, .context = NULL};
        jobs->background(jobs->self, &slow_jobs[i]);
    }
    slow_queued = SLOW_JOBS;
}

// Lets them go, and waits for them, without doing any here either
static void slow_end(void)
{
    __atomic_store_n(&slow_release, 1u, __ATOMIC_RELEASE);
    for (uint32_t i = 0; i < slow_queued; i++) {
        while (!__atomic_load_n(&slow_jobs[i].done, __ATOMIC_ACQUIRE)) {
        }
    }
    slow_queued = 0;
}
#endif

static int frame(void *user, const float seconds)
{
    static bool started, begun;
    static int frames, loading_frames;
    static uint32_t made, calls;
    if (!started) {
        printf("renderer: %s\n", tide_platform_renderer()); // What a test of one backend looks for
        uint32_t shaders = 0;
        while (tide_game_shaders[shaders]) shaders++;
        check(shaders == 3, "tidec's table has the game's three shaders");
        started = true;
    }
    const uint32_t before = tide_platform_shader_pipelines(); // ...as the frame that begins the game begins
    const int code = tide_run_frame(user, seconds);
    if (code != TIDE_KEEP_RUNNING) return code;
    if (tide_run_loading.on) { // The pipelines it starts with are being made, while the window's frames go on
#ifndef __wasm__
        // ...with long work queued on the pool from the first of them, and let go a few frames on: the frames
        // between drew and were shown without doing any of it
        if (loading_frames == 0) slow_begin();
        if (loading_frames == 8) {
            check(slow_on_frame_thread == 0, "a loader's frame does none of the long work queued for the pool's threads");
            slow_end();
        }
#endif
        if (++loading_frames < WAIT_FRAMES) return TIDE_KEEP_RUNNING;
        check(false, "the game begins once the pipelines it starts with are made, within the frames given them");
        return failures;
    }
    if (!begun) {
        begun = true;
#ifndef __wasm__
        check(slow_on_frame_thread == 0, "nor does the frame that begins the game");
        slow_end();
#endif
        printf("the game began in loading frame %d\n", loading_frames + 1);
        check(tide_platform_prewarm_waiting() == 0, "the game began once every pipeline it starts with was made");
        if (again) check(before == 3, "the last run's way of drawing each made its pipeline as the game started");
        else check(before == 0, "with nothing known of how they draw, none is made as the game starts");
    }
    frames++;
    if (!later) {
        // The frame after the first, which drew the early mesh, has the late one made
        if ((frames < 2 || tide_platform_prewarm_waiting() > 0) && frames < WAIT_FRAMES) return TIDE_KEEP_RUNNING;
        printf("prewarmed pipelines: made by frame %d\n", frames);
        check(tide_platform_prewarm_waiting() == 0, "they're made within the frames given them");
        made = tide_platform_shader_pipelines();
        if (again) check(made == 3, "and the frames that drew one of them made none");
        else check(made == 2, "once a mesh of its kind drew, the late mesh's is made too, and the blit's isn't");
        later = true;
        return TIDE_KEEP_RUNNING;
    }
    if (!calls) {
        if (again) check(tide_platform_shader_pipelines() == made, "the first frame through the others made no pipeline");
        else check(tide_platform_shader_pipelines() == made + 1u, "the first frame through the others made the blit's alone");
        calls = tide_platform_draw_calls();
        return TIDE_KEEP_RUNNING;
    }
    if (again) {
        check(calls == tide_platform_draw_calls(), "and drew as many calls as the frame after it: every one");
    } else if (tide_platform_draw_calls() < 4u && frames < WAIT_FRAMES) {
        return TIDE_KEEP_RUNNING; // The blit's, made in the background where it's that backend
    }
    check(tide_platform_draw_calls() >= 4u, "the mesh into the target, the blit, and the mesh over it, with the target's clear");
    return failures;
}

int main(const int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "first";
    again = strcmp(mode, "again") == 0;
    if (!spoil(mode)) return 1;
    tide_run_game = &tide_host_game_api;
    // The mode is the test's, not the game's command line
    tide_run_configure(&(tide_run_desc){.title = "prewarm", .game_name = "prewarm", .argc = 1, .argv = argv});
    tide_platform_open(&(tide_window_desc){.title = tide_run_settings.title,
                                           .width = tide_run_settings.width,
                                           .height = tide_run_settings.height,
                                           .hidden = true});
#ifndef __wasm__
    frame_thread = true;
#endif
    tide_run_begin_loaded();
    tide_platform_run(frame, NULL);
}
