#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/time.h"
#include "tide_test.h"

typedef CornerCulling_Box box;
typedef CornerCulling_CullingQuery query;
typedef CornerCulling_CullingResult result;

static box wall(float x, float y, float z, float ex, float ey, float ez, int32_t mask)
{
    return (box){.bounds = {.center = {x, y, z}, .extents = {ex, ey, ez}}, .mask = mask};
}

static query sight(void)
{
    return (query){.observer = {.center = {-5, 0, 0}},
                   .target = {.center = {5, 0, 0}, .extents = {0.4f, 0.9f, 0.4f}},
                   .mask = -1, .surfaceMargin = 0.0001f};
}

static void init(tide_world *w)
{
    tide_world_init(w, 1.0f / 128.0f);
    w->Fixture.query = sight();
}

static void rebuild(tide_world *w, const box *boxes, int count)
{
    tide_text_use(&w->heap, NULL);
    tide_list_resize(&w->Fixture.boxes, count, sizeof *boxes, TIDE_IN_MATCH);
    for (int i = 0; i < count; i++)
        *(box *)tide_list_at_mut(w->Fixture.boxes, i, sizeof *boxes) = boxes[i];
    w->Fixture.action = 1;
    tide_world_tick(w);
}

static result ask(tide_world *w, query q)
{
    w->Fixture.query = q;
    tide_world_tick(w);
    return w->Fixture.answer;
}

static void update(tide_world *w, int source, box replacement)
{
    w->Fixture.action = 2;
    w->Fixture.source = source;
    w->Fixture.replacement = replacement;
    tide_world_tick(w);
    TIDE_CHECK(w->Fixture.updated);
}

TIDE_TEST(cornerculling_endpoints_masks_contacts_and_invalid_values)
{
    static tide_world w;
    init(&w);
    TIDE_CHECK(ask(&w, sight()).status == CornerCulling_Status_Unknown);
    rebuild(&w, NULL, 0);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Visible);
    box b = wall(0, 0, 0, 0.5f, 4, 4, 2);
    rebuild(&w, &b, 1);
    TIDE_CHECK(!w.OcclusionExample.visible);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Occluded);
    TIDE_CHECK(w.Fixture.answer.occluder == 0 && w.Fixture.cache.candidate == 1);
    query q = sight();
    q.mask = 1;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Visible && w.Fixture.cache.candidate == 0);
    q.mask = 0;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Visible);
    q.mask = 2;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Occluded);
    q.observer.extents = tide_f3(0.5f, 0.5f, 0.5f);
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Occluded);
    q.target.center = tide_f3(-4.5f, 0, 0);
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Visible); // overlapping endpoints
    q = sight();
    q.observer.center.x = -0.5f;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Visible); // endpoint contact
    q = sight();
    q.observer.center.y = q.target.center.y = 4;
    q.target.extents = tide_f3(0, 0, 0);
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Visible); // surface tangency
    q.observer.center.y = q.target.center.y = 3.999f;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Occluded);
    q = sight();
    q.target.extents.y = 20;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Visible); // center ray alone is insufficient
    q = sight();
    q.observer.extents.y = 20;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Visible);
    q = sight();
    q.surfaceMargin = 1;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Visible);

    const float invalid[] = {NAN, INFINITY, -INFINITY};
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        q = sight(); q.observer.center.x = invalid[i];
        TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Unknown);
        q = sight(); q.target.extents.z = invalid[i];
        TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Unknown);
        q = sight(); q.surfaceMargin = invalid[i];
        TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Unknown);
        b.bounds.center.y = invalid[i];
        rebuild(&w, &b, 1);
        TIDE_CHECK(ask(&w, sight()).status == CornerCulling_Status_Visible);
    }
    q = sight(); q.target.extents.x = -1;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Unknown);
    q = sight(); q.surfaceMargin = -1;
    TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Unknown);
    b = wall(0, 0, 0, 0, 4, 4, -1);
    rebuild(&w, &b, 1);
    TIDE_CHECK(ask(&w, sight()).status == CornerCulling_Status_Visible);
    w.Fixture.action = 3;
    tide_world_tick(&w);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Unknown);
    TIDE_CHECK(w.Fixture.answer.occluder == -1);
    tide_world_free(&w);
}

TIDE_TEST(cornerculling_never_combines_unrelated_blockers)
{
    static tide_world w;
    init(&w);
    query q = sight();
    q.target.extents = tide_f3(0.25f, 4, 0.25f);
    w.Fixture.query = q;
    const box boxes[] = {wall(0, -2, 0, 0.5f, 2, 4, -1), wall(0, 2, 0, 0.5f, 2, 4, -1)};
    rebuild(&w, boxes, 2);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Visible);
    tide_world_free(&w);
}

// These are the fixed geometries shown in visual.html. A split wall can
// cover the whole player while neither individual box certifies occlusion.
TIDE_TEST(cornerculling_fps_visual_scenarios)
{
    static tide_world w;
    init(&w);
    query q = sight();
    box solid = wall(0, 0, 0, 0.5f, 4, 4, -1);
    w.Fixture.query = q;
    rebuild(&w, &solid, 1);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Occluded);

    const box split[] = {wall(0, 0, -2, 0.5f, 4, 2, -1),
                         wall(0, 0, 2, 0.5f, 4, 2, -1)};
    rebuild(&w, split, 2);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Visible);

    box narrow = wall(0, 0, 0, 0.5f, 4, 1, -1);
    q.target.center.z = 3;
    w.Fixture.query = q;
    rebuild(&w, &narrow, 1);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Visible);
    tide_world_free(&w);
}

TIDE_TEST(cornerculling_cached_candidates_follow_updates_rebuilds_and_other_worlds)
{
    static tide_world w, other;
    init(&w); init(&other);
    box boxes[65];
    for (int i = 0; i < 65; i++) boxes[i] = wall(100 + i * 3.0f, 100, 0, 0.5f, 1, 1, -1);
    boxes[37] = wall(0, 0, 0, 0.5f, 4, 4, -1);
    rebuild(&w, boxes, 65);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Occluded && w.Fixture.answer.occluder == 37);
    update(&w, 37, wall(0, 0, 0, 0.5f, 4, 4, 0));
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Visible);
    update(&w, 0, boxes[37]); // crosses the old tree's spatial partitions
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Occluded && w.Fixture.answer.occluder == 0);
    rebuild(&other, boxes, 65);
    other.Fixture.cache = w.Fixture.cache;
    TIDE_CHECK(ask(&other, sight()).status == CornerCulling_Status_Occluded && other.Fixture.answer.occluder == 37);
    w.Fixture.cache.candidate = INT32_MIN;
    TIDE_CHECK(ask(&w, sight()).status == CornerCulling_Status_Occluded);
    w.Fixture.cache.candidate = INT32_MAX;
    TIDE_CHECK(ask(&w, sight()).status == CornerCulling_Status_Occluded);
    box invalid = wall(0, 0, 0, -1, 4, 4, -1);
    rebuild(&w, &invalid, 1);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Visible);
    update(&w, 0, boxes[37]);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Occluded);
    w.Fixture.action = 2; w.Fixture.source = -1;
    tide_world_tick(&w);
    TIDE_CHECK(!w.Fixture.updated);
    tide_world_free(&other); tide_world_free(&w);
}

// Independent face-plane oracle, not the implementation's slab clipping.
// Inputs in the randomized fixture are small binary rationals. Long double
// gives extra native precision; WASM's long double is still sufficient here.
static bool ray_hits(const box *b, const double from[3], const double to[3])
{
    const float center[3] = {b->bounds.center.x, b->bounds.center.y, b->bounds.center.z};
    const float extent[3] = {b->bounds.extents.x, b->bounds.extents.y, b->bounds.extents.z};
    for (int axis = 0; axis < 3; axis++) {
        const long double direction = (long double)to[axis] - from[axis];
        if (direction == 0) continue;
        for (int side = -1; side <= 1; side += 2) {
            const long double face = (long double)center[axis] + side * (long double)extent[axis];
            const long double t = (face - from[axis]) / direction;
            if (t <= 0 || t >= 1) continue;
            bool inside = true;
            for (int k = 0; k < 3; k++) {
                if (k == axis) continue;
                const long double p = (long double)from[k] + t * ((long double)to[k] - from[k]);
                if (p <= (long double)center[k] - extent[k] || p >= (long double)center[k] + extent[k]) inside = false;
            }
            if (inside) return true;
        }
    }
    return false;
}

static void point(CornerCulling_Bounds bounds, int sample, double out[3])
{
    const double weights[] = {-1, 0, 1};
    out[0] = (double)bounds.center.x + bounds.extents.x * weights[sample % 3];
    out[1] = (double)bounds.center.y + bounds.extents.y * weights[sample / 3 % 3];
    out[2] = (double)bounds.center.z + bounds.extents.z * weights[sample / 9 % 3];
}

static uint32_t random_word(uint32_t *state)
{
    *state ^= *state << 13; *state ^= *state >> 17; *state ^= *state << 5;
    return *state;
}

static float random_value(uint32_t *state)
{
    return ((int32_t)(random_word(state) % 1025) - 512) / 32.0f;
}

TIDE_TEST(cornerculling_bvh_matches_exhaustive_search_and_interior_ray_oracle)
{
    static tide_world w;
    init(&w);
    box boxes[129];
    uint32_t seed = 0xd718bc31;
    for (int i = 0; i < 129; i++) {
        const float x = random_value(&seed), y = random_value(&seed), z = random_value(&seed);
        boxes[i] = wall(x, y, z, 0.5f + (i % 3), 1.5f + (i % 5), 2, 1 << (i % 3));
    }
    boxes[0] = wall(0, 0, 0, 0.5f, 4, 4, -1);
    rebuild(&w, boxes, 129);
    uint64_t fingerprint = UINT64_C(14695981039346656037);
    int occluded = 0;
    for (int n = 0; n < 1024; n++) {
        query q = sight();
        const float ay = random_value(&seed), az = random_value(&seed);
        const float by = random_value(&seed), bz = random_value(&seed);
        q.observer.center = tide_f3(-10, ay, az);
        q.target.center = tide_f3(10, by, bz);
        q.observer.extents = n % 2 ? tide_f3(0.25f, 0.5f, 0.25f) : tide_f3(0, 0, 0);
        q.mask = n % 4 ? 1 << (n % 3) : -1;
        w.Fixture.query = q; w.Fixture.action = 5;
        tide_world_tick(&w);
        const result answer = w.Fixture.answer;
        TIDE_CHECK((answer.status == CornerCulling_Status_Occluded) == w.Fixture.brute);
        TIDE_CHECK(answer.status == w.Fixture.cold.status);
        fingerprint = (fingerprint ^ (uint32_t)w.Fixture.cold.status) * UINT64_C(1099511628211);
        fingerprint = (fingerprint ^ (uint32_t)w.Fixture.cold.occluder) * UINT64_C(1099511628211);
        if (answer.status != CornerCulling_Status_Occluded) continue;
        occluded++;
        TIDE_REQUIRE(answer.occluder >= 0 && answer.occluder < 129);
        for (int a = 0; a < 27; a++) for (int b = 0; b < 27; b++) {
            double from[3], to[3];
            point(q.observer, a, from); point(q.target, b, to);
            TIDE_REQUIRE(ray_hits(&boxes[answer.occluder], from, to));
        }
    }
    TIDE_CHECK(occluded > 100);
    TIDE_CHECK(fingerprint == UINT64_C(0xcc7d62b609066c8b));
    printf("    deterministic answers: %016llx (%d occluded)\n", (unsigned long long)fingerprint, occluded);
    tide_world_free(&w);
}

TIDE_TEST(cornerculling_large_tiny_and_nearly_tangent_geometry_fails_open)
{
    static tide_world w;
    init(&w);
    const float scales[] = {1e-30f, 1e-10f, 1, 1e10f, 1e30f};
    for (size_t i = 0; i < sizeof scales / sizeof *scales; i++) {
        const float s = scales[i];
        box b = wall(0, 0, 0, s, 4 * s, 4 * s, -1);
        query q = sight();
        q.surfaceMargin = 0;
        q.observer.center = tide_f3(-5 * s, 0, 0);
        q.target.center = tide_f3(5 * s, 0, 0);
        q.target.extents = tide_f3(0.25f * s, 0.5f * s, 0.25f * s);
        w.Fixture.query = q;
        rebuild(&w, &b, 1);
        TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Occluded);
        q.observer.center.y = q.target.center.y = 4 * s;
        q.target.extents = tide_f3(0, 0, 0);
        TIDE_CHECK(ask(&w, q).status == CornerCulling_Status_Visible);
    }
    const box b = wall(FLT_MAX / 2, 0, 0, 1, 4, 4, -1);
    rebuild(&w, &b, 1);
    TIDE_CHECK(ask(&w, sight()).status == CornerCulling_Status_Visible);
    tide_world_free(&w);
}

TIDE_TEST(cornerculling_snapshots_replay_packing_and_scalar_query_storage)
{
    static tide_world w, snapshot, loaded;
    init(&w);
    const box b = wall(0, 0, 0, 0.5f, 4, 4, -1);
    rebuild(&w, &b, 1);
    tide_world_copy(&snapshot, &w);
    const uint64_t hash = tide_world_hash(&snapshot);
    const uint32_t bytes = tide_world_pack(&snapshot, NULL, 0);
    void *packed = malloc(bytes);
    TIDE_REQUIRE(packed != NULL);
    TIDE_CHECK(tide_world_pack(&snapshot, packed, bytes) == bytes);
    TIDE_CHECK(tide_world_unpack(&loaded, packed, bytes));
    TIDE_CHECK(tide_world_hash(&loaded) == hash);
    free(packed);
    update(&w, 0, wall(0, 0, 0, 0.5f, 4, 4, 0));
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Visible);
    TIDE_CHECK(tide_world_hash(&snapshot) == hash);
    const uint32_t used = snapshot.heap.used;
    for (int i = 0; i < 64; i++) {
        tide_world_tick(&snapshot); tide_world_tick(&loaded);
        TIDE_CHECK(snapshot.Fixture.answer.status == CornerCulling_Status_Occluded);
        TIDE_CHECK(tide_world_hash(&snapshot) == tide_world_hash(&loaded));
        TIDE_CHECK(snapshot.heap.used == used && tide_scratch_mark() == 0);
    }
    tide_world_free(&loaded); tide_world_free(&snapshot); tide_world_free(&w);
}

TIDE_TEST(cornerculling_drives_authoritative_component_visibility)
{
    static tide_world server, client;
    init(&server);
    server.Integration.enabled = true;
    const box b = wall(0, 0, 0, 0.5f, 4, 4, -1);
    rebuild(&server, &b, 1);
    for (int phase = 0; phase < 3; phase++) {
        if (phase == 1) update(&server, 0, wall(0, 0, 0, 0.5f, 4, 4, 0));
        if (phase == 2) update(&server, 0, b);
        for (int who = 0; who < 2; who++) {
            uint32_t size = 0;
            const tide_player_id player = tide_player_from_index(who);
            uint8_t *bytes = tide_game_api.client_state(&server, player, &size);
            TIDE_REQUIRE(bytes != NULL);
            TIDE_CHECK(tide_game_api.load_client_state(&client, bytes, size));
            TIDE_CHECK(tide_has_Enemy(&client, server.Integration.enemy) == (who == 0 || phase == 1));
            TIDE_CHECK(tide_game_api.client_hash(&client, player) == tide_game_api.client_hash(&server, player));
            const char *marker = "CORNER-CULLING-HIDDEN-POSITION";
            bool found = false;
            for (uint32_t i = 0; i + strlen(marker) <= size; i++)
                if (memcmp(bytes + i, marker, strlen(marker)) == 0) found = true;
            TIDE_CHECK(found == (who == 0 || phase == 1));
            free(bytes);
        }
    }
    tide_world_free(&client); tide_world_free(&server);
}

static void batch(tide_world *w)
{
    w->Fixture.action = 4;
    tide_world_tick(w);
}

static void player_queries(tide_world *w, int players, float expansion)
{
    tide_text_use(&w->heap, NULL);
    tide_list_resize(&w->Fixture.queries, players * (players - 1), sizeof(query), TIDE_IN_MATCH);
    int index = 0;
    for (int a = 0; a < players; a++) for (int b = 0; b < players; b++) {
        if (a == b) continue;
        query q = sight();
        q.observer.center = tide_f3(a < players / 2 ? -5 : 5, (a % 5) * 0.3f, 0);
        q.target.center = tide_f3(b < players / 2 ? -5 : 5, (b % 5) * 0.3f, 0);
        q.observer.extents = tide_f3(expansion, expansion, expansion);
        q.target.extents = tide_f3(0.4f + expansion, 0.9f + expansion, 0.4f + expansion);
        *(query *)tide_list_at_mut(w->Fixture.queries, index++, sizeof q) = q;
    }
}

static int occluded_results(tide_world *w)
{
    tide_text_use(&w->heap, NULL);
    int count = 0;
    for (int i = 0; i < tide_list_count(w->Fixture.results); i++) {
        const result *r = tide_list_at(w->Fixture.results, i, sizeof *r);
        if (r->status == CornerCulling_Status_Occluded) count++;
    }
    return count;
}

// Straight handwritten C for the equivalent one-box certificate. It uses
// the same outward endpoint/inward blocker rounding and strict slab test.
// Benchmarking this alongside the package catches excessive language overhead.
static bool baseline_ray(const double a[3], const double b[3], const double lo[3], const double hi[3])
{
    double enter = 1e-12, exit = 1 - 1e-12;
    for (int k = 0; k < 3; k++) {
        const double d = b[k] - a[k];
        if (d == 0) { if (a[k] <= lo[k] || a[k] >= hi[k]) return false; }
        else {
            const double x = (lo[k] - a[k]) / d, y = (hi[k] - a[k]) / d;
            const double first = x < y ? x : y, last = x > y ? x : y;
            if (first > enter) enter = first;
            if (last < exit) exit = last;
            if (exit - enter <= 1e-12) return false;
        }
    }
    return true;
}

static bool baseline_query(query q, box blocker)
{
    if ((q.mask & blocker.mask) == 0) return false;
    const double a[3] = {q.observer.center.x, q.observer.center.y, q.observer.center.z};
    const double b[3] = {q.target.center.x, q.target.center.y, q.target.center.z};
    const double ae[3] = {q.observer.extents.x, q.observer.extents.y, q.observer.extents.z};
    const double be[3] = {q.target.extents.x, q.target.extents.y, q.target.extents.z};
    const double c[3] = {blocker.bounds.center.x, blocker.bounds.center.y, blocker.bounds.center.z};
    const double e[3] = {blocker.bounds.extents.x, blocker.bounds.extents.y, blocker.bounds.extents.z};
    const int ac = ae[0] == 0 && ae[1] == 0 && ae[2] == 0 ? 1 : 8;
    const int bc = be[0] == 0 && be[1] == 0 && be[2] == 0 ? 1 : 8;
    double lo[3], hi[3], amin[3], amax[3], bmin[3], bmax[3];
    bool overlap_a = true, overlap_b = true;
    for (int k = 0; k < 3; k++) {
        const double ar = ac == 1 ? 0 : fmax(fabs(a[k]), ae[k]) * 1e-12;
        const double br = bc == 1 ? 0 : fmax(fabs(b[k]), be[k]) * 1e-12;
        amin[k] = a[k] - (ae[k] + ar); amax[k] = a[k] + (ae[k] + ar);
        bmin[k] = b[k] - (be[k] + br); bmax[k] = b[k] + (be[k] + br);
        const double rounding = fmax(fabs(c[k]), e[k]) * 1e-12;
        const double outer_min = c[k] - e[k] - rounding, outer_max = c[k] + e[k] + rounding;
        if (outer_min > amax[k] || outer_max < amin[k]) overlap_a = false;
        if (outer_min > bmax[k] || outer_max < bmin[k]) overlap_b = false;
        const double inner = e[k] - rounding - q.surfaceMargin;
        if (inner <= 0) return false;
        lo[k] = c[k] - inner; hi[k] = c[k] + inner;
    }
    if (overlap_a || overlap_b || !baseline_ray(a, b, lo, hi)) return false;
    for (int i = 0; i < ac; i++) for (int j = 0; j < bc; j++) {
        double from[3], to[3];
        for (int k = 0; k < 3; k++) {
            from[k] = i & (1 << k) ? amax[k] : amin[k];
            to[k] = j & (1 << k) ? bmax[k] : bmin[k];
        }
        if (!baseline_ray(from, to, lo, hi)) return false;
    }
    return true;
}

static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

TIDE_TEST(cornerculling_performance_player_matrix_and_handwritten_baseline)
{
    static tide_world w;
    init(&w);
    box *boxes = calloc(4096, sizeof *boxes);
    TIDE_REQUIRE(boxes != NULL);
    boxes[0] = wall(0, 0, 0, 0.5f, 8, 8, -1);
    for (int i = 1; i < 4096; i++)
        boxes[i] = wall(30 + (i % 64) * 3.0f, 30 + (i / 64) * 3.0f, 0, 0.5f, 1, 1, -1);
    bool (*volatile baseline)(query, box) = baseline_query;
    const int player_counts[] = {10, 50};
    volatile int total = 0;
    for (int expanded = 0; expanded < 2; expanded++) for (int size = 0; size < 2; size++) {
        const int players = player_counts[size], count = players * (players - 1);
        player_queries(&w, players, expanded ? 0.5f : 0);
        query *queries = malloc((size_t)count * sizeof *queries);
        TIDE_REQUIRE(queries != NULL);
        tide_text_use(&w.heap, NULL);
        for (int i = 0; i < count; i++) queries[i] = *(query *)tide_list_at(w.Fixture.queries, i, sizeof(query));
        for (int blockers = 1; blockers <= 4096; blockers *= 4096) {
            rebuild(&w, boxes, blockers);
            for (int i = 0; i < 4; i++) batch(&w);
            TIDE_CHECK(occluded_results(&w) == players * players / 2);
            const uint32_t used = w.heap.used;
            double samples[9], cold[9], reference[9];
            for (int sample = 0; sample < 9; sample++) {
                const uint64_t start = tide_time_now_ns();
                for (int repeat = 0; repeat < 16; repeat++) batch(&w);
                samples[sample] = (double)(tide_time_now_ns() - start) / 16000.0;
                const uint64_t cold_start = tide_time_now_ns();
                for (int repeat = 0; repeat < 16; repeat++) {
                    w.Fixture.action = 7;
                    tide_world_tick(&w);
                }
                cold[sample] = (double)(tide_time_now_ns() - cold_start) / 16000.0;
                const uint64_t before = tide_time_now_ns();
                int actual = 0;
                for (int repeat = 0; repeat < 16; repeat++)
                    for (int i = 0; i < count; i++) actual += baseline(queries[i], boxes[0]);
                reference[sample] = (double)(tide_time_now_ns() - before) / 16000.0;
                TIDE_CHECK(actual == 16 * players * players / 2);
                total = total + actual;
            }
            qsort(samples, 9, sizeof *samples, compare_double);
            qsort(cold, 9, sizeof *cold, compare_double);
            qsort(reference, 9, sizeof *reference, compare_double);
            TIDE_CHECK(w.heap.used == used && tide_scratch_mark() == 0);
            printf("    %d players, %d links, %d boxes, %s: cached %.3f us, cold %.3f us; C one-box %.3f us\n",
                   players, count, blockers, expanded ? "bounds" : "point", samples[4], cold[4], reference[4]);
            TIDE_CHECK(occluded_results(&w) == players * players / 2);
        }
        free(queries);
    }
    TIDE_CHECK(total > 0);
    // Equal centroids must also build a balanced, traversable tree.
    for (int i = 0; i < 4096; i++) boxes[i] = boxes[0];
    const uint64_t start = tide_time_now_ns();
    rebuild(&w, boxes, 4096);
    printf("    4096 identical-centroid rebuild: %.3f ms\n", (double)(tide_time_now_ns() - start) / 1e6);
    TIDE_CHECK(w.Fixture.answer.status == CornerCulling_Status_Occluded);
    free(boxes);
    tide_world_free(&w);
}

TIDE_TEST(cornerculling_performance_near_misses_and_door_refit)
{
    static tide_world w;
    init(&w);
    box boxes[512];
    // Every broadphase overlaps the query envelope, but each small blocker
    // misses the center ray. No blocker can certify the whole target.
    for (int i = 0; i < 512; i++)
        boxes[i] = wall((i % 16 - 8) * 0.4f, (i / 16 + 1) * 0.02f, 0, 0.1f, 0.005f, 0.1f, -1);
    query q = sight();
    q.target.extents = tide_f3(0.4f, 2, 0.4f);
    tide_text_use(&w.heap, NULL);
    tide_list_resize(&w.Fixture.queries, 90, sizeof q, TIDE_IN_MATCH);
    for (int i = 0; i < 90; i++) *(query *)tide_list_at_mut(w.Fixture.queries, i, sizeof q) = q;
    rebuild(&w, boxes, 512);
    batch(&w);
    TIDE_CHECK(occluded_results(&w) == 0);
    double samples[9], refits[9];
    for (int sample = 0; sample < 9; sample++) {
        const uint64_t start = tide_time_now_ns();
        for (int repeat = 0; repeat < 16; repeat++) batch(&w);
        samples[sample] = (double)(tide_time_now_ns() - start) / 16000.0;
        const uint64_t refit_start = tide_time_now_ns();
        for (int repeat = 0; repeat < 16; repeat++) update(&w, 0, boxes[0]);
        refits[sample] = (double)(tide_time_now_ns() - refit_start) / 16000.0;
    }
    qsort(samples, 9, sizeof *samples, compare_double);
    qsort(refits, 9, sizeof *refits, compare_double);
    TIDE_CHECK(occluded_results(&w) == 0);
    printf("    90 visible near-miss queries / 512 boxes: %.3f us/batch; one-box update/full refit %.3f us\n",
           samples[4], refits[4]);
    tide_world_free(&w);
}
