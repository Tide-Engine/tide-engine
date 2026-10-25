// CPU-only chunk bursts. No renderer/window/audio; --quick is a correctness
// smoke. Wall clocks are observations, never CTest assertions.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/page.h"
#include "tide/platform.h"
#include "tide/time.h"
#include "tide/worker.h"

enum { EDGE = 34, CELLS = EDGE * EDGE * EDGE, MOST_FRAMES = 100000 };

static uint8_t source[CELLS];

static uint32_t at(const uint32_t x, const uint32_t y, const uint32_t z)
{
    return (z * EDGE + y) * EDGE + x;
}

static uint32_t mesh_c(int32_t *faces)
{
    uint32_t n = 0;
    for (uint32_t z = 1; z <= 32; z++) {
        for (uint32_t y = 1; y <= 32; y++) {
            for (uint32_t x = 1; x <= 32; x++) {
                const uint32_t p = at(x, y, z);
                if (!source[p]) continue;
                const int32_t cell = (int32_t)(((z - 1u) * 32u + y - 1u) * 32u + x - 1u) * 6;
                if (!source[p - 1u]) faces[n++] = cell;
                if (!source[p + 1u]) faces[n++] = cell + 1;
                if (!source[p - EDGE]) faces[n++] = cell + 2;
                if (!source[p + EDGE]) faces[n++] = cell + 3;
                if (!source[p - EDGE * EDGE]) faces[n++] = cell + 4;
                if (!source[p + EDGE * EDGE]) faces[n++] = cell + 5;
            }
        }
    }
    return n;
}

static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static double milliseconds(const uint64_t from, const uint64_t to)
{
    return to > from ? (double)(to - from) * 1e-6 : 0;
}

static bool number(const char *text, const uint32_t most, uint32_t *value)
{
    char *end;
    const unsigned long n = strtoul(text, &end, 10);
    if (!*text || *end || *text == '-' || n > most) return false;
    *value = (uint32_t)n;
    return true;
}

int main(int argc, char **argv)
{
    uint32_t chunks = 64, simulation = 0;
    bool fallback = false, profile = false;
    tide_worker_limits limits = {0};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) chunks = 2;
        else if (strcmp(argv[i], "--fallback") == 0) fallback = true;
        else if (strcmp(argv[i], "--profile") == 0) profile = true;
        else if (strcmp(argv[i], "--simulate") == 0) simulation = 8192;
        else if (strcmp(argv[i], "--help") == 0) {
            puts("bench_workers [--chunks 1..4096] [--pending 1..4096] [--simulate] [--fallback] [--profile] [--quick]");
            return 0;
        } else if ((strcmp(argv[i], "--chunks") == 0 || strcmp(argv[i], "--pending") == 0) && i + 1 < argc) {
            const bool pending = strcmp(argv[i], "--pending") == 0;
            uint32_t value;
            if (!number(argv[++i], 4096, &value) || !value) return 2;
            if (pending) limits.pending = value;
            else chunks = value;
        } else return 2;
    }
    for (uint32_t z = 0; z < EDGE; z++)
        for (uint32_t y = 0; y < EDGE; y++)
            for (uint32_t x = 0; x < EDGE; x++)
                source[at(x, y, z)] = ((x * 17u + y * 31u + z * 13u) & 7u) < 3u;

    // A handwritten serial face emitter reuses its maximum-size output buffer.
    // It is a kernel/verification reference, not a like-for-like asynchronous
    // ownership or threading baseline. Its full burst includes result checksums.
    int32_t *faces = tide_alloc(32u * 32u * 32u * 6u * sizeof *faces);
    const uint64_t c_began = tide_time_now_ns();
    uint64_t expected_faces = 0, expected_sum = 0;
    for (uint32_t chunk = 0; chunk < chunks; chunk++) {
        const uint32_t n = mesh_c(faces);
        expected_faces += n;
        expected_sum += chunk;
        for (uint32_t i = 0; i < n; i++) expected_sum += (uint32_t)faces[i];
    }
    const double c_ms = milliseconds(c_began, tide_time_now_ns());
    free(faces);

    tide_world world = {0};
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_profile measured = {0};
    tide_gui gui = {0};
    const tide_jobs *jobs = fallback ? NULL : tide_platform_jobs();
    gui.jobs = jobs;
    gui.worker_limits = &limits;
    gui.profile = profile ? &measured : NULL;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_local_init(&local);
    // Allocate the source grid and warm the frame outside the burst interval.
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    local.Meshing.chunks = (int32_t)chunks;
    world.Simulation.count = (int32_t)simulation;
    if (simulation) tide_world_tick_on(&world, jobs);
    double *frame_ms = tide_alloc(MOST_FRAMES * sizeof *frame_ms);
    double *tick_ms = tide_alloc(MOST_FRAMES * sizeof *tick_ms);
    uint32_t frames = 0;
    const uint64_t began = tide_time_now_ns(), until = began + UINT64_C(120000000000);
    while (local.Meshing.finished < (int32_t)chunks && frames < MOST_FRAMES && tide_time_now_ns() < until) {
        const uint64_t tick = tide_time_now_ns();
        if (simulation) tide_world_tick_on(&world, jobs);
        const uint64_t view = tide_time_now_ns();
        tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
        const uint64_t end = tide_time_now_ns();
        tick_ms[frames] = milliseconds(tick, view);
        frame_ms[frames++] = milliseconds(view, end);
        tide_scratch_free();
        tide_yield();
    }
    const double burst_ms = milliseconds(began, tide_time_now_ns());
    bool good = frames && local.Meshing.finished == (int32_t)chunks
        && (uint64_t)local.Meshing.faces == expected_faces && (uint64_t)local.Meshing.checksum == expected_sum;
    // Independent serial simulation oracle, irrespective of how many frames a
    // particular queue/pool needed to complete the same mesh burst.
    uint32_t value = 1;
    for (uint32_t tick = 0; simulation && tick <= frames; tick++)
        for (uint32_t n = 0; n < 512; n++) value = (value * 17u + n) & 65535u;
    tide_text_use(&world.heap, &local.heap);
    good &= tide_list_count(world.Simulation.values) == (int32_t)simulation;
    for (uint32_t i = 0; i < simulation; i++)
        good &= *(const int32_t *)tide_list_at(world.Simulation.values, (int32_t)i, sizeof(int32_t)) == (int32_t)value;
    qsort(frame_ms, frames, sizeof *frame_ms, compare_double);
    qsort(tick_ms, frames, sizeof *tick_ms, compare_double);
    const uint32_t median = frames ? frames / 2u : 0, p95 = frames ? (frames - 1u) * 95u / 100u : 0;
    printf("{\"benchmark\":\"workers\",\"chunks\":%u,\"threads\":%u,\"simulation\":%u,\"pending_limit\":%u,"
           "\"profile\":%s,\"frames\":%u,\"faces\":%llu,\"checksum\":%llu,\"burst_ms\":%.6f,"
           "\"c_serial_kernel_ms\":%.6f,\"view_median_ms\":%.6f,\"view_p95_ms\":%.6f,\"view_max_ms\":%.6f,"
           "\"tick_median_ms\":%.6f,\"tick_p95_ms\":%.6f,\"tick_max_ms\":%.6f,\"valid\":%s}\n",
           chunks, jobs ? jobs->threads : 1u, simulation, limits.pending, profile ? "true" : "false", frames,
           (unsigned long long)expected_faces, (unsigned long long)expected_sum, burst_ms, c_ms,
           frames ? frame_ms[median] : 0, frames ? frame_ms[p95] : 0, frames ? frame_ms[frames - 1u] : 0,
           frames ? tick_ms[median] : 0, frames ? tick_ms[p95] : 0, frames ? tick_ms[frames - 1u] : 0, good ? "true" : "false");
    for (uint32_t i = 0; profile && i < measured.workers.count; i++) {
        const tide_profile_time *time = &measured.workers.items[i];
        printf("worker phase %s: %llu measurements, %.6f ms total\n", time->name,
               (unsigned long long)time->measurements, time->measured_ms);
    }
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    tide_profile_free(&measured);
    tide_scratch_free();
    free(frame_ms);
    free(tick_ms);
    return good ? 0 : 1;
}
