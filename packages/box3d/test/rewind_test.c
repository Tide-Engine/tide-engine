#include <math.h>
#include <stdio.h>
#include "tide/time.h"
#include <stdlib.h>
#include <string.h>
#include "rewind.h"
#include "tide_test.h"

int64_t rewind_clock(void) { return (int64_t)tide_time_now_ns(); }

static tide_entity tagged(tide_world *w, bool (*has)(const tide_world *, tide_entity))
{
    for (uint32_t i = 1; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (has(w, e)) return e;
    }
    return (tide_entity){0};
}
static bool near(const float a, const float b) { return fabsf(a - b) < 0.0001f; }
static bool same(const tide_entity a, const tide_entity b) { return a.index == b.index && a.generation == b.generation; }
static const Observed_local *probe(tide_world *w, tide_local *local, const ViewTime time)
{
    w->Request.time = time;
    const uint64_t before = tide_world_hash(w);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(w, NULL, 1.0f, NULL, NULL, 1.0f, local, &draw, &gui);
    tide_draw_free(&draw);
    TIDE_CHECK(tide_world_hash(w) == before);
    return &local->Observed_local;
}
TIDE_TEST(box3d_rewind_is_readonly_and_keeps_current_walls)
{
    tide_world w = {0}, saved = {0}, unpacked = {0};
    tide_local local = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    const tide_entity target = tagged(&w, tide_has_Target), wall = tagged(&w, tide_has_Wall);
    w.Request.sensor = tagged(&w, tide_has_Sensor);
    for (int i = 0; i < 10; i++) tide_world_tick(&w);
    const Box3D_Body ignored = tide_get_Box3D_Body(&w, target);
    w.Request.ignore = ignored.id;
    const ViewTime time = {.tick = 2, .alpha = 0.5f, .valid = true};
    const Observed_local *o = probe(&w, &local, time);
    TIDE_CHECK(o->prepared && o->ray && !o->ignored && same(o->hit, target));
    TIDE_CHECK(near(o->fraction, (4.05f - 0.5f - 3.0f) / 3.0f));
    TIDE_CHECK(o->wall && same(o->wallHit, wall) && near(o->wallFraction, 1.875f / 6.0f));
    TIDE_CHECK(o->roundRay && o->roundInsideRay == false);
    TIDE_CHECK(o->partRay && same(o->partHit, tagged(&w, tide_has_Cap)));
    TIDE_CHECK(o->boxCast && o->roundCast && o->boxOverlap && o->roundOverlap && o->inside && o->insideCount == 1);
    TIDE_CHECK(o->capsuleCast && o->capsuleOverlap);
    // Rewind the player while the wall moves in the current world.
    Box3D_Body body = tide_get_Box3D_Body(&w, wall);
    body.position.x = 2.5f;
    tide_set_Box3D_Body(&w, wall, body);
    tide_world_tick(&w);
    o = probe(&w, &local, time);
    TIDE_CHECK(o->wall && near(o->wallFraction, 2.375f / 6.0f));
    tide_world_copy(&saved, &w);
    const uint32_t size = tide_world_pack(&w, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&w, bytes, size);
    TIDE_CHECK(tide_world_unpack(&unpacked, bytes, size));
    free(bytes);
    o = probe(&unpacked, &local, time);
    TIDE_CHECK(o->prepared && near(o->fraction, (4.05f - 0.5f - 3.0f) / 3.0f));
    // Invalid, unavailable, future, stale and malformed times never fall back.
    const ViewTime rejected[] = {{0}, {.tick=0, .alpha=1, .valid=true}, {.tick=50, .alpha=1, .valid=true},
        {.tick=2, .alpha=-0.1f, .valid=true}, {.tick=2, .alpha=NAN, .valid=true}};
    for (size_t i=0; i<sizeof rejected / sizeof rejected[0]; i++) {
        o=probe(&w, &local, rejected[i]); TIDE_CHECK(!o->prepared && !o->ray && !o->wall && !o->inside);
    }
    for (int i=0; i<20; i++) tide_world_tick(&w);
    o=probe(&w, &local, time); TIDE_CHECK(!o->prepared && !o->ray);
    tide_world_copy(&w, &saved);
    o=probe(&w, &local, time); TIDE_CHECK(o->prepared && o->ray);
    tide_world_free(&unpacked); tide_world_free(&saved); tide_world_free(&w); tide_local_free(&local);
}

TIDE_TEST(box3d_rewind_respects_snaps_opt_out_and_despawns)
{
    tide_world w = {0};
    tide_local local = {0};
    tide_world_init(&w, 1.0f/60.0f);
    const tide_entity target = tagged(&w, tide_has_Target);
    w.Request.sensor = tagged(&w, tide_has_Sensor);
    for (int i=0; i<4; i++) tide_world_tick(&w);
    const ViewTime old = {.tick=2, .alpha=0.5f, .valid=true};
    w.Request.jump = true;
    tide_world_tick(&w);
    w.Request.jump = false;
    const Observed_local *o = probe(&w, &local, (ViewTime){.tick=5, .alpha=0.5f, .valid=true});
    TIDE_CHECK(o->prepared && o->ray && near(o->fraction, (5.0f+2.0f/60.0f-0.5f-3.0f)/3.0f));
    w.Request.optOut = true;
    tide_world_tick(&w);
    o = probe(&w, &local, old);
    TIDE_CHECK(o->prepared && o->ray && near(o->fraction, (5.0f+4.0f/60.0f-0.5f-3.0f)/3.0f));
    // Restore opt-in through the component, then remove it. Historical handles remain intact.
    w.Request.optOut = false;
    Box3D_Body body = tide_get_Box3D_Body(&w, target);
    body.rewind = true; tide_set_Box3D_Body(&w, target, body);
    tide_world_tick(&w);
    w.Request.remove = true;
    tide_world_tick(&w);
    o = probe(&w, &local, (ViewTime){.tick = w.Time.tick, .alpha = 1, .valid = true});
    TIDE_CHECK(o->prepared && !o->ray && o->savedHit);
    tide_world_tick(&w);
    o = probe(&w, &local, old);
    TIDE_CHECK(!tide_has_Target(&w, target) && o->prepared && o->ray && same(o->hit, target));
    tide_world_free(&w); tide_local_free(&local);
}

TIDE_TEST(box3d_rewind_batches_reuse_the_spatial_index)
{
    tide_world w = {0};
    tide_local local = {0};
    tide_world_init(&w, 1.0f / 60.0f);
    w.Request.populate = true;
    for (int i = 0; i < 4; i++) tide_world_tick(&w);
    w.Request.bench = 1000;
    const Observed_local *o = probe(&w, &local, (ViewTime){.tick = 3, .alpha = 0.5f, .valid = true});
    TIDE_CHECK(o->prepared && o->historicalHits == 1000 && o->currentHits == 1000);
    printf("  1024 colliders, 1000 queries: prepare %.3f ms, historical %.3f ms, current %.3f ms\n",
        (double)o->prepareNs / 1e6, (double)o->historicalNs / 1e6, (double)o->currentNs / 1e6);
    tide_world_free(&w); tide_local_free(&local);
}
