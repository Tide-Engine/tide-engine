#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queries.h"
#include "tide_test.h"
#ifdef QUERY_ALL_JOBS
#include "tide/platform.h"
#endif

#ifdef QUERY_ALL_3D
typedef Box3D_Body QueryBody;
typedef Box3D_RayHit QueryHit;
#define QUERY_KINDS 4
#define forget_queries b3t_forget
#else
typedef Box2D_Body QueryBody;
typedef Box2D_RayHit QueryHit;
#define QUERY_KINDS 3
#define forget_queries b2t_forget
#endif
extern void forget_queries(void);

static bool same_entity(const tide_entity a, const tide_entity b)
{
    return a.index == b.index && a.generation == b.generation;
}

static tide_entity marked(const tide_world *w, const int kind)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Marker(w, e) && tide_get_Marker(w, e).kind == kind) return e;
    }
    return (tide_entity){0};
}

static QueryBody body_of(const tide_world *w, const tide_entity e)
{
#ifdef QUERY_ALL_3D
    return tide_get_Box3D_Body(w, e);
#else
    return tide_get_Box2D_Body(w, e);
#endif
}

static void current_time(tide_world *w, tide_local *local)
{
    local->Controls.time = (ViewTime){.tick = w->Time.tick, .alpha = 1.0f, .valid = true};
}

static void start(tide_world *w, tide_local *local)
{
    tide_world_init(w, 1.0f / 60.0f);
    tide_local_init(local);
    for (int i = 0; i < 6; i++) tide_world_tick(w);
    TIDE_CHECK(w->Captured.done && tide_list_count(w->Captured.hits) == 7);
    current_time(w, local);
}

static const Observed *probe(tide_world *w, tide_local *local, const int which)
{
    local->Controls.which = which;
    const uint64_t hash = tide_world_hash(w);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(w, NULL, 1.0f, NULL, NULL, 1.0f, local, &draw, &gui);
    tide_draw_free(&draw);
    TIDE_CHECK(tide_world_hash(w) == hash);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_text_use(NULL, &local->heap);
    return &local->Observed;
}

static const QueryHit *hit_at(const tide_list hits, const int i)
{
    return tide_list_at(hits, i, sizeof(QueryHit));
}

static void ordered(const tide_list hits)
{
    float before = -1.0f;
    for (int i = 0; i < tide_list_count(hits); i++) {
        const QueryHit *hit = hit_at(hits, i);
        TIDE_CHECK(isfinite(hit->fraction) && hit->fraction >= before && hit->fraction <= 1.0f);
        TIDE_CHECK(isfinite(hit->point.x) && isfinite(hit->normal.x));
        before = hit->fraction;
    }
}

static void equivalent(const tide_list a, const tide_list b)
{
    TIDE_REQUIRE(tide_list_count(a) == tide_list_count(b));
    for (int i = 0; i < tide_list_count(a); i++) {
        const QueryHit *x = hit_at(a, i), *y = hit_at(b, i);
        TIDE_CHECK(same_entity(x->entity, y->entity) && same_entity(x->part, y->part));
        TIDE_CHECK(fabsf(x->fraction - y->fraction) < 0.0001f);
        // An initial overlap has no unique surface contact. Existing casts
        // promise fraction zero and no normal, not a particular point.
        if (x->fraction > 0.0f) TIDE_CHECK(fabsf(x->point.x - y->point.x) < 0.0001f);
    }
}

static int hits_on(const tide_list hits, const tide_entity e)
{
    int count = 0;
    for (int i = 0; i < tide_list_count(hits); i++) count += same_entity(hit_at(hits, i)->entity, e);
    return count;
}

static uint64_t digest_hits(uint64_t hash, const tide_list hits)
{
    for (int i = 0; i < tide_list_count(hits); i++) {
        const uint8_t *bytes = (const uint8_t *)hit_at(hits, i);
        for (size_t j = 0; j < sizeof(QueryHit); j++) hash = (hash ^ bytes[j]) * UINT64_C(1099511628211);
    }
    return hash;
}

TIDE_TEST(all_queries_sort_filter_and_preserve_individual_colliders)
{
    tide_world w = {0};
    tide_local local = {0};
    start(&w, &local);
    const tide_entity compound = marked(&w, 60), part = marked(&w, 61), near = marked(&w, 20);
    local.Controls.ignore = body_of(&w, compound).id;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int which = 0; which < QUERY_KINDS; which++) {
        const Observed *o = probe(&w, &local, which);
        printf("    query %d: live %d, historical %d, sensors %d, ignored %d\n", which,
               tide_list_count(o->live), tide_list_count(o->historical), tide_list_count(o->sensors), tide_list_count(o->ignored));
        TIDE_REQUIRE(o->prepared && o->liveAny && o->historicalAny);
        TIDE_REQUIRE(tide_list_count(o->live) == 7 && tide_list_count(o->sensors) == 8);
        TIDE_CHECK(tide_list_count(o->ignored) == 4 && tide_list_count(o->ignoredSensors) == 5);
        ordered(o->live); ordered(o->sensors); ordered(o->ignored);
        equivalent(o->live, o->historical);
        equivalent(o->sensors, o->historicalSensors);
        equivalent(o->ignored, o->historicalIgnored);
        TIDE_CHECK(hits_on(o->live, compound) == 3 && hits_on(o->ignored, compound) == 0);
        TIDE_CHECK(same_entity(hit_at(o->live, 0)->entity, near));
        TIDE_CHECK(same_entity(hit_at(o->live, 1)->entity, marked(&w, 40)));
        TIDE_CHECK(same_entity(hit_at(o->live, 2)->entity, marked(&w, 41)));
        TIDE_CHECK(hit_at(o->live, 1)->fraction == hit_at(o->live, 2)->fraction);
        bool saw_part = false;
        for (int i = 0; i < tide_list_count(o->live); i++) saw_part |= same_entity(hit_at(o->live, i)->part, part);
        TIDE_CHECK(saw_part && o->closestAny && o->closest.fraction == hit_at(o->live, 0)->fraction);
        TIDE_CHECK(same_entity(o->filtered.entity, compound)); // Filtering by the game's ordinary component.
        hash = digest_hits(hash, o->live);
        hash = digest_hits(hash, o->sensors);
        hash = digest_hits(hash, o->ignored);
    }
    printf("    all-hit output bits: %016llx\n", (unsigned long long)hash);
#ifdef QUERY_ALL_3D
    TIDE_CHECK(hash == UINT64_C(0xf8d8985883f8ec9f));
#else
    TIDE_CHECK(hash == UINT64_C(0x7533602157f73627));
#endif
    local.Controls.ignore = body_of(&w, near).id;
    const Observed *o = probe(&w, &local, 0);
    TIDE_CHECK(hits_on(o->ignored, near) == 0 && o->ignoredClosestAny);
    TIDE_CHECK(same_entity(o->ignoredClosest.entity, hit_at(o->ignored, 0)->entity));
    tide_local_free(&local); tide_world_free(&w);
}

TIDE_TEST(all_queries_grow_and_replace_results_without_changing_the_world)
{
    tide_world w = {0}, snapshot = {0}, restored = {0};
    tide_local local = {0};
    start(&w, &local);
    local.Controls.y = 5; local.Controls.to = 80;
    for (int which = 0; which < QUERY_KINDS; which++) {
        const Observed *o = probe(&w, &local, which);
        TIDE_REQUIRE(tide_list_count(o->live) == 70);
        equivalent(o->live, o->historical); ordered(o->live);
        for (int i = 0; i < 70; i++) TIDE_CHECK(same_entity(hit_at(o->live, i)->entity, marked(&w, 100 + i)));
    }
    tide_world_copy(&snapshot, &w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_world_pack(&w, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    free(bytes);
    forget_queries();
    TIDE_CHECK(tide_list_count(probe(&restored, &local, 0)->live) == 70);
    for (int i = 0; i < 3; i++) {
        tide_world_tick(&w); tide_world_tick(&snapshot); tide_world_tick(&restored);
        TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&snapshot));
        TIDE_CHECK(tide_world_hash(&w) == tide_world_hash(&restored));
    }
    local.Controls.y = 50;
    const Observed *o = probe(&w, &local, 0);
    TIDE_CHECK(!o->liveAny && tide_list_count(o->live) == 0); // A previously nonempty destination is cleared.
    local.Controls.y = 5; local.Controls.time.valid = false;
    o = probe(&w, &local, 0);
    TIDE_CHECK(!o->prepared && !o->historicalAny && tide_list_count(o->historical) == 0);
    current_time(&w, &local);
    const float invalid[] = {NAN, INFINITY, -INFINITY, -FLT_MAX};
    for (size_t invalid_index = 0; invalid_index < sizeof invalid / sizeof invalid[0]; invalid_index++) {
        local.Controls.from = invalid[invalid_index];
        local.Controls.to = FLT_MAX;
        for (int which = 0; which < QUERY_KINDS; which++) {
            o = probe(&w, &local, which);
            TIDE_CHECK(!o->liveAny && !o->historicalAny && tide_list_count(o->live) == 0 && tide_list_count(o->historical) == 0);
        }
    }
    tide_local_free(&local); tide_world_free(&restored); tide_world_free(&snapshot); tide_world_free(&w);
}

TIDE_TEST(all_queries_before_the_first_step_return_empty_lists)
{
    tide_world w = {0}; tide_local local = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    tide_local_init(&local);
    for (int which = 0; which < QUERY_KINDS; which++) {
        const Observed *o = probe(&w, &local, which);
        TIDE_CHECK(!o->liveAny && !o->historicalAny && tide_list_count(o->live) == 0 && tide_list_count(o->historical) == 0);
    }
    tide_local_free(&local); tide_world_free(&w);
}

TIDE_TEST(all_queries_rewind_movement_and_retain_detached_geometry)
{
    tide_world w = {0}; tide_local local = {0};
    start(&w, &local);
    const tide_entity moving = marked(&w, 20), part = marked(&w, 61);
    local.Controls.time = (ViewTime){.tick = 2, .alpha = 0.5f, .valid = true};
    const Observed *o = probe(&w, &local, 0);
    TIDE_REQUIRE(o->prepared && tide_list_count(o->historical) == 7);
    TIDE_CHECK(same_entity(hit_at(o->historical, 0)->entity, moving));
    TIDE_CHECK(hit_at(o->historical, 0)->point.x < hit_at(o->live, 0)->point.x);
    local.Controls.keep = true;
    w.Request.drop = 61;
    tide_world_tick(&w); // Destroyed parts are absent before the native world removes their shapes.
    o = probe(&w, &local, 0);
    TIDE_CHECK(tide_list_count(o->live) == 6 && tide_list_count(o->historical) == 7 && tide_list_count(o->savedHits) == 7);
    for (int i = 0; i < tide_list_count(o->live); i++) TIDE_CHECK(!same_entity(hit_at(o->live, i)->part, part));
    w.Request.drop = 60;
    tide_world_tick(&w);
    o = probe(&w, &local, 0);
    TIDE_CHECK(tide_list_count(o->live) == 4 && tide_list_count(o->historical) == 7 && tide_list_count(o->savedHits) == 7);
    tide_local_free(&local); tide_world_free(&w);
}

TIDE_TEST(all_queries_ignore_body_generations_and_preserve_saved_results)
{
    tide_world w = {0}; tide_local local = {0}, restored_local = {0};
    start(&w, &local);
    const tide_entity old = marked(&w, 60);
    const int64_t old_body = body_of(&w, old).id;
    const Observed *o = probe(&w, &local, 0);
    TIDE_REQUIRE(tide_list_count(o->live) == 7);
    local.Controls.keep = true;
    const uint32_t size = tide_local_pack(&local, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && tide_local_pack(&local, bytes, size) == size);
    TIDE_REQUIRE(tide_local_unpack(&restored_local, bytes, size));
    free(bytes);
    w.Request.drop = 60;
    tide_world_tick(&w);
    w.Request.drop = 0;
    w.Request.spawn = true;
    tide_world_tick(&w);
    tide_world_tick(&w);
    current_time(&w, &restored_local);
    restored_local.Controls.ignore = old_body;
    o = probe(&w, &restored_local, 0);
    const tide_entity fresh = marked(&w, 62);
    const int64_t fresh_body = body_of(&w, fresh).id;
    TIDE_REQUIRE(fresh_body != 0 && fresh_body != old_body);
    TIDE_CHECK((uint32_t)fresh_body == (uint32_t)old_body); // Same slot, a new generation.
    TIDE_CHECK(!tide_entity_alive(&w.entities, old));
    TIDE_CHECK(tide_list_count(o->live) == 5 && tide_list_count(o->ignored) == 5);
    TIDE_CHECK(hits_on(o->ignored, fresh) == 1);
    TIDE_CHECK(tide_list_count(o->savedHits) == 7 && hits_on(o->savedHits, old) == 3);
    restored_local.Controls.ignore = fresh_body;
    o = probe(&w, &restored_local, 0);
    TIDE_CHECK(tide_list_count(o->ignored) == 4 && hits_on(o->ignored, fresh) == 0);
    tide_local_free(&restored_local); tide_local_free(&local); tide_world_free(&w);
}

TIDE_TEST(all_shape_queries_keep_initial_overlap_and_can_ignore_it)
{
    tide_world w = {0}; tide_local local = {0};
    start(&w, &local);
    local.Controls.from = body_of(&w, marked(&w, 20)).position.x;
    local.Controls.ignore = body_of(&w, marked(&w, 20)).id;
    for (int which = 1; which < QUERY_KINDS; which++) {
        const Observed *o = probe(&w, &local, which);
        TIDE_REQUIRE(tide_list_count(o->live) > 0 && tide_list_count(o->ignored) > 0);
        TIDE_CHECK(hit_at(o->live, 0)->fraction == 0.0f && hit_at(o->live, 0)->normal.x == 0.0f);
        TIDE_CHECK(hit_at(o->ignored, 0)->fraction > 0.0f);
        equivalent(o->live, o->historical);
    }
    tide_local_free(&local); tide_world_free(&w);
}

#ifdef QUERY_ALL_3D
TIDE_TEST(all_mesh_queries_keep_one_hit_for_the_whole_mesh)
{
    tide_world w = {0}; tide_local local = {0};
    start(&w, &local);
    local.Controls.y = 10;
    for (int which = 0; which < QUERY_KINDS; which++) {
        const Observed *o = probe(&w, &local, which);
        TIDE_REQUIRE(tide_list_count(o->live) == 1 && tide_list_count(o->historical) == 1);
        TIDE_CHECK(same_entity(hit_at(o->live, 0)->entity, marked(&w, 90)));
        TIDE_CHECK(hit_at(o->live, 0)->point.x < 4.0f);
        equivalent(o->live, o->historical);
    }
    tide_local_free(&local); tide_world_free(&w);
}
#endif

#ifdef QUERY_ALL_JOBS
TIDE_TEST(all_queries_match_when_system_rows_run_on_workers)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) return;
    tide_world serial = {0}, parallel = {0};
    tide_world_init(&serial, 1.0f / 60.0f);
    tide_world_copy(&parallel, &serial);
    for (int tick = 0; tick < 6; tick++) {
        tide_world_tick(&serial);
        forget_queries();
        tide_world_tick_on(&parallel, jobs);
        TIDE_REQUIRE(tide_world_hash(&serial) == tide_world_hash(&parallel));
    }
    TIDE_CHECK(tide_get_Checked(&parallel, marked(&parallel, 20)).count == 70);
    tide_world_free(&parallel); tide_world_free(&serial);
}
#endif
