// Sleep: a client's frames over bodies that fall asleep and wake (see
// sleep.tide). A period is `every` ticks, each with the world before it kept
// in a ring and the world after it hashed, then a correction that restores
// the world `back` ticks ago and runs those ticks again, as versus_rollback
// does. Tide only: built with Sleeping sparse (versus_sleep) and moving
// (versus_sleep_lasting), compared run against run.

#include <stdlib.h>

#include "sleep.h"
#include "tide/page.h"
#include "tide/time.h"
#include "versus.h"

enum { FLAG_BODIES, FLAG_ASLEEP, FLAG_TOGGLES, FLAG_KEPT, FLAG_BACK, FLAG_EVERY, FLAG_COUNT };
enum { PART_TICK, PART_SNAPSHOT, PART_HASH, PART_RESTORE, PART_COUNT };

static versus_flag flags[] = {
    {"bodies", "bodies", 100000, 2000, 0, 0},
    {"asleep", "of each hundred bodies, how many start asleep", 90, 90, 0, 0},
    {"toggles", "bodies that wake or fall asleep each tick", 10, 10, 0, 0},
    {"kept", "worlds kept, one from before each of the latest ticks", 16, 6, 0, 1},
    {"back", "ticks a correction goes back and runs again (as many as are kept at the most)", 8, 4, 0, 0},
    {"every", "ticks between corrections: a period, which is what's timed", 4, 3, 0, 1},
};

typedef struct tide_way {
    tide_world world;
    tide_world *ring; // The world before each of the latest ticks, by tick
    uint64_t *hashes; // The world's hash after each of them, by tick
    uint32_t kept, back, every;
    uint32_t ticks;      // Run, as the world counts them
    uint32_t mismatches; // Ticks run again whose hash wasn't the one they had: none
    double part[PART_COUNT];
} tide_way;

static void *make(const versus_flag *f)
{
    tide_way *s = tide_alloc_zeroed(1, sizeof *s);
    tide_world_init(&s->world, VERSUS_DT);
    s->world.Setup.bodies = (int32_t)f[FLAG_BODIES].value;
    s->world.Setup.asleep = (int32_t)f[FLAG_ASLEEP].value;
    s->world.Setup.toggles = (int32_t)f[FLAG_TOGGLES].value;
    s->kept = f[FLAG_KEPT].value;
    s->back = f[FLAG_BACK].value < s->kept ? f[FLAG_BACK].value : s->kept;
    s->every = f[FLAG_EVERY].value;
    s->ring = tide_alloc_zeroed(s->kept, sizeof *s->ring);
    s->hashes = tide_alloc_zeroed(s->kept, sizeof *s->hashes);
    return s;
}

// As a session hashes: the pages that changed on a few threads first, where it has them
static uint64_t hash_world(const tide_world *w, const tide_jobs *jobs)
{
    return jobs ? tide_game_api.hash_world_on(w, jobs) : tide_world_hash(w);
}

static double since(const uint64_t start)
{
    return (double)(tide_time_now_ns() - start) / 1e6;
}

// A tick, with the world before it kept as it starts
static void step(tide_way *s, const tide_jobs *jobs)
{
    const uint64_t kept = tide_time_now_ns();
    tide_world_copy(&s->ring[s->ticks % s->kept], &s->world);
    const uint64_t ticked = tide_time_now_ns();
    s->part[PART_SNAPSHOT] += (double)(ticked - kept) / 1e6;
    tide_world_tick_on(&s->world, jobs);
    s->part[PART_TICK] += since(ticked);
    s->ticks++;
}

static void tick(void *state, const tide_jobs *jobs)
{
    tide_way *s = state;
    for (uint32_t k = 0; k < PART_COUNT; k++) s->part[k] = 0.0;
    for (uint32_t i = 0; i < s->every; i++) {
        step(s, jobs);
        const uint64_t start = tide_time_now_ns();
        s->hashes[s->ticks % s->kept] = hash_world(&s->world, jobs);
        s->part[PART_HASH] += since(start);
    }
    const uint32_t back = s->back < s->ticks ? s->back : s->ticks;
    if (!back) return;
    const uint32_t now = s->ticks;
    const uint64_t start = tide_time_now_ns();
    s->ticks -= back;
    tide_world_copy(&s->world, &s->ring[s->ticks % s->kept]);
    s->part[PART_RESTORE] += since(start);
    while (s->ticks < now) step(s, jobs);
    const uint64_t hashed = tide_time_now_ns();
    s->mismatches += hash_world(&s->world, jobs) != s->hashes[now % s->kept];
    s->part[PART_HASH] += since(hashed);
}

static void parts(const void *state, double *ms)
{
    const tide_way *s = state;
    for (uint32_t k = 0; k < PART_COUNT; k++) ms[k] = s->part[k];
}

typedef struct body_item {
    uint32_t index;
    float motion[12];
    int32_t asleep;
} body_item;

// Every body by its entity, wherever its archetype keeps it: the same for
// both builds
static uint64_t world_digest(tide_world *w)
{
    uint64_t sum = 0;
    for (uint32_t index = 0; index < w->entities.next_unused; index++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, index);
        if (!tide_has_Body(w, e)) continue;
        const Motion m = tide_get_Body(w, e).motion;
        const body_item item = {index,
                                {m.position.x, m.position.y, m.position.z, m.velocity.x, m.velocity.y, m.velocity.z, m.push.x, m.push.y,
                                 m.push.z, m.drag, m.bounce, m.radius},
                                tide_has_Sleeping(w, e)};
        sum += versus_item(&item, sizeof item);
    }
    return sum;
}

static uint64_t digest(const void *state)
{
    tide_way *s = (tide_way *)state;
    uint64_t sum = world_digest(&s->world) + versus_item(&s->mismatches, sizeof s->mismatches);
    if (s->ticks) {
        const uint64_t before = world_digest(&s->ring[(s->ticks - 1u) % s->kept]);
        sum += versus_item(&before, sizeof before);
    }
    return sum;
}

static void destroy(void *state)
{
    tide_way *s = state;
    tide_world_free(&s->world);
    for (uint32_t k = 0; k < s->kept; k++) tide_world_free(&s->ring[k]);
    free(s->ring);
    free(s->hashes);
    free(s);
}

int main(int argc, char **argv)
{
    const versus_way ways[] = {{"Tide", make, tick, digest, destroy}};
    const versus_parts timed[] = {parts};
    static const char *const part_names[PART_COUNT] = {"tick", "snapshot", "hash", "restore"};
    const versus_desc desc = {
        .name = "versus_sleep",
        .about = "a client's frames over bodies that fall asleep and wake: ticks kept and hashed, then a correction run again",
        .flags = flags,
        .flag_count = sizeof flags / sizeof flags[0],
        .warmup = 10,
        .ticks = 100,
        .quick_warmup = 3,
        .quick_ticks = 6,
        .ways = ways,
        .way_count = sizeof ways / sizeof ways[0],
        .part_names = part_names,
        .part_count = PART_COUNT,
        .parts = timed,
    };
    return versus_main(&desc, argc, argv);
}
