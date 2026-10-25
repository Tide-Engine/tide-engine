// CPU presentation only: real generated interpolation/correction/decay paths.
// No renderer, network, replay or hierarchy traversal. --quick checks semantics.
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/time.h"

#if BENCH_TRANSFORM
typedef Transform Pose;
#define tide_get_Pose tide_get_Transform
#define tide_set_Pose tide_set_Transform
#endif

_Static_assert(sizeof(Pose) == 40, "Transform-equivalent payload");
_Static_assert(offsetof(Pose, localRotation) == 12, "Transform rotation offset");
_Static_assert(offsetof(Pose, localScale) == 28, "Transform scale offset");

static tide_world worlds[2], previous[2];
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static volatile float sink;

static void require(bool ok, const char *message)
{
    if (!ok) { fprintf(stderr, "%s: %s\n", BENCH_MODE, message); exit(1); }
}

static bool near(double a, double b)
{
    const double difference = a > b ? a - b : b - a;
    return difference <= 0.000001 * (1.0 + (b < 0 ? -b : b));
}

static void reset(void)
{
    tide_local_free(&local);
    tide_local_init(&local);
}

static void correct(unsigned from, unsigned to)
{
#ifdef TIDE_HAS_VIEW_CORRECTIONS
    const tide_view_correction c = {&worlds[from], &previous[from], &worlds[to], &previous[to], 0, 0.5f, false, false};
    tide_local_correct(&local, &c);
#else
    (void)from;
    (void)to;
#endif
}

static uint32_t rows(void)
{
#ifdef TIDE_HAS_VIEW_CORRECTIONS
    return local.tide_corrections_Pose.rows.count;
#else
    return 0;
#endif
}

// Allocations owned by the correction table, excluding allocator bookkeeping
// and the fixed tide_local member. The error payload currently keeps all Pose.
static size_t correction_bytes(void)
{
#ifdef TIDE_HAS_VIEW_CORRECTIONS
    const tide_attached *a = &local.tide_corrections_Pose;
    size_t bytes = (size_t)a->indexed * sizeof(uint32_t) + (size_t)a->other_room * sizeof(uint32_t)
        + (size_t)a->rows.room * 3u * sizeof(tide_page *);
    for (uint32_t i = 0; i < a->rows.chunks * 3u; i++)
        bytes += sizeof(tide_page) + a->rows.pages[i]->size;
    return bytes;
#else
    return 0;
#endif
}

static void frame(unsigned side)
{
    local.Observed = (Observed){0};
    tide_draw_reset(&draw);
    tide_frame(&worlds[side], &previous[side], 0.5f, NULL, NULL, 1, &local, &draw, &gui);
    sink = local.Observed.position.x + local.Observed.rotation.y;
}

static void setup(uint32_t count, uint32_t changed)
{
    reset();
    tide_world_init(&worlds[0], 1.0f / 60.0f);
    worlds[0].Setup.count = (int32_t)count;
    tide_world_tick(&worlds[0]);
    tide_world_copy(&worlds[1], &worlds[0]);
    tide_world_copy(&previous[0], &worlds[0]);
    tide_world_copy(&previous[1], &worlds[0]);
    uint32_t found = 0;
    // Visit live handles, including the scene root, rather than assuming IDs.
    for (uint32_t i = 0; i < worlds[0].entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&worlds[0].entities, i);
#if BENCH_TRANSFORM
        if (!tide_has_Transform(&worlds[0], e)) continue;
#else
        if (!tide_has_Pose(&worlds[0], e)) continue;
#endif
        Pose p = tide_get_Pose(&worlds[0], e);
        p.localPosition.x -= 0.125f;
        require(tide_set_Pose(&previous[0], e, p), "previous write");
        // Evenly spread corrections, including the highest live handle, so a
        // sparse workload exercises the full handle-index allocation/lookup.
        if ((uint64_t)(found + 1u) * changed / count != (uint64_t)found * changed / count) {
            p.localPosition.x -= 0.5f;
            p.localPosition.y += 0.25f;
            p.localRotation = (tide_quaternion){tide_f4(0, 0.8f, 0, 0.6f)};
        }
        require(tide_set_Pose(&previous[1], e, p), "corrected previous write");
        p.localPosition.x += 0.125f;
        require(tide_set_Pose(&worlds[1], e, p), "corrected write");
        found++;
    }
    require(found == count, "entity count");
}

static void verify(uint32_t count, uint32_t changed)
{
    frame(0);
    const Observed a = local.Observed;
    frame(1);
    const Observed b = local.Observed;
    require(a.count == (int32_t)count && b.count == a.count, "view count");
    require(near(b.position.x, a.position.x - 0.5f * (float)changed), "uncorrected position");
    require(near(b.rotation.y, a.rotation.y + 0.2f * (float)changed), "uncorrected rotation");
    const uint64_t hash = tide_world_hash(&worlds[1]);
    correct(0, 1);
    frame(1);
#ifdef TIDE_HAS_VIEW_CORRECTIONS
    require(rows() == changed, "active error row count");
    require(near(local.Observed.position.x, a.position.x), "position continuity");
    if (strcmp(BENCH_MODE, "both") == 0)
        require(near(local.Observed.rotation.y, a.rotation.y), "rotation continuity");
    else require(near(local.Observed.rotation.y, b.rotation.y), "ordinary rotation");
    tide_local_frame_time(&local, 0.075f);
    frame(1);
    require(near(local.Observed.position.x, (a.position.x + b.position.x) * 0.5f), "75 ms position half life");
#else
    require(near(local.Observed.position.x, b.position.x), "untagged position");
#endif
    require(near(local.Observed.scale.x, (float)count), "scale stays ordinary");
    require(tide_world_hash(&worlds[1]) == hash, "presentation changed match state");
    reset();
}

static int compare(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void stats(double *values, uint32_t count)
{
    qsort(values, count, sizeof *values, compare);
    printf("{\"median_ms\":%.9f,\"p95_ms\":%.9f}", values[count / 2], values[(count * 95 + 99) / 100 - 1]);
}

static void run(uint32_t count, uint32_t changed, const char *scenario, bool quick)
{
    setup(count, changed);
    verify(count, changed);
    if (!quick) {
        enum { SAMPLES = 31, BATCH = 32, WARMUP = 64 };
        double correction[SAMPLES], decay[SAMPLES], view[SAMPLES], total[SAMPLES], cold[SAMPLES];
        for (uint32_t i = 0; i < SAMPLES; i++) {
            reset();
            const uint64_t start = tide_time_now_ns();
            if (strcmp(scenario, "steady") != 0) correct(0, 1);
            cold[i] = (double)(tide_time_now_ns() - start) / 1e6;
        }
        reset();
        unsigned side = 0;
        for (uint32_t sample = 0; sample < SAMPLES + 1; sample++) {
            uint64_t cn = 0, dn = 0, vn = 0;
            const uint32_t n = sample ? BATCH : WARMUP;
            const uint64_t start = tide_time_now_ns();
            for (uint32_t i = 0; i < n; i++) {
                const uint64_t t0 = tide_time_now_ns();
                if (strcmp(scenario, "steady") != 0) { correct(side, 1u - side); side = 1u - side; }
                const uint64_t t1 = tide_time_now_ns();
                tide_local_frame_time(&local, 1.0f / 60.0f);
                const uint64_t t2 = tide_time_now_ns();
                frame(side);
                const uint64_t t3 = tide_time_now_ns();
                cn += t1 - t0; dn += t2 - t1; vn += t3 - t2;
            }
            const uint64_t end = tide_time_now_ns();
            if (!sample) continue;
            const double divisor = 1e6 * (double)n;
            correction[sample - 1] = (double)cn / divisor;
            decay[sample - 1] = (double)dn / divisor;
            view[sample - 1] = (double)vn / divisor;
            total[sample - 1] = (double)(end - start) / divisor;
        }
        printf("{\"mode\":\"%s\",\"count\":%u,\"changed\":%u,\"scenario\":\"%s\",\"samples\":%u,\"batch\":%u,\"active_rows\":%u,\"correction_bytes\":%zu,\"pose_bytes\":%zu,\"local_bytes\":%zu,\"cold\":",
               BENCH_MODE, count, changed, scenario, SAMPLES, BATCH, rows(), correction_bytes(), sizeof(Pose), sizeof(tide_local));
        stats(cold, SAMPLES); printf(",\"correction\":");
        stats(correction, SAMPLES); printf(",\"decay\":"); stats(decay, SAMPLES);
        printf(",\"view\":"); stats(view, SAMPLES); printf(",\"combined\":"); stats(total, SAMPLES);
        printf(",\"world_hash\":\"%016" PRIx64 "\",\"checksum\":%.9g}\n", tide_world_hash(&worlds[1]), (double)sink);
    }
    for (unsigned i = 0; i < 2; i++) { tide_world_free(&worlds[i]); tide_world_free(&previous[i]); }
    reset();
}

int main(int argc, char **argv)
{
    bool quick = false;
    uint32_t count = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) quick = true;
        else if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            char *end;
            const unsigned long parsed = strtoul(argv[++i], &end, 10);
            if (!*argv[i] || *end || *argv[i] == '-' || !parsed || parsed > 100000) return 2;
            count = (uint32_t)parsed;
        } else return 2;
    }
    tide_local_init(&local);
    const uint32_t counts[] = {100, 1000, 10000};
    for (unsigned k = 0; k < (count || quick ? 1u : 3u); k++) {
        const uint32_t n = count ? count : quick ? 100 : counts[k];
        run(n, 0, "steady", quick);
        run(n, 0, "unchanged", quick);
        run(n, n / 100u ? n / 100u : 1u, "sparse", quick);
        run(n, n, "dense", quick);
    }
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_scratch_free();
    if (quick) printf("%s: correction benchmark correctness passed\n", BENCH_MODE);
    return 0;
}
