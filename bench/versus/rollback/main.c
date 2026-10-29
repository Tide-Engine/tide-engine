// Rollback, Tide against C (see bench/versus/versus.h and period.h): a world
// kept, ticked, hashed, restored and run again, as a client that predicts
// does, with the snapshots, hashes and restores a session uses.

#include <stdlib.h>

#include "period.h"
#include "rollback.h"
#include "tide/list.h"
#include "tide/page.h"
#include "tide/time.h"
#include "versus.h"

extern const versus_way rollback_c, rollback_c_dirty;
extern const versus_parts rollback_c_parts, rollback_c_dirty_parts;

static versus_flag flags[] = {
    {"movers", "movers in a box", 100000, 2000, 0, 0},
    {"turrets", "turrets, which cool down every tick and fire when they're ready", 4000, 200, 0, 0},
    {"moving", "of each hundred movers, how many move (the rest lie still)", 25, 25, 0, 0},
    {"spread", "1 to spread the movers that move through the rest, rather than have them first", 0, 0, 0, 0},
    {"notes", "the length of a list each turret counts its shots in", 4096, 256, 0, 1},
    {"kept", "worlds kept, one from before each of the latest ticks", 16, 6, 0, 1},
    {"back", "ticks a correction goes back and runs again (as many as are kept at the most)", 8, 4, 0, 0},
    {"every", "ticks between corrections: a period, which is what's timed", 4, 3, 0, 1},
};

typedef struct tide_way {
    tide_world world;
    tide_world *ring; // The world before each of the latest ticks, by tick
    uint64_t *hashes; // The world's hash after each of them, by tick
    plan p;
    uint32_t ticks;      // Run, as the world counts them
    uint32_t mismatches; // Ticks run again whose hash wasn't the one they had: none
    double part[PART_COUNT];
} tide_way;

static void *make(const versus_flag *f)
{
    tide_way *s = tide_alloc_zeroed(1, sizeof *s);
    s->p = plan_of(f);
    tide_world_init(&s->world, VERSUS_DT);
    s->world.Setup.movers = (int32_t)s->p.movers;
    s->world.Setup.turrets = (int32_t)s->p.turrets;
    s->world.Setup.moving = (int32_t)s->p.moving;
    s->world.Setup.first = (int32_t)s->p.first;
    s->world.Setup.spread = (int32_t)s->p.spread;
    s->world.Setup.notes = (int32_t)s->p.notes;
    s->ring = tide_alloc_zeroed(s->p.kept, sizeof *s->ring);
    s->hashes = tide_alloc_zeroed(s->p.kept, sizeof *s->hashes);
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
    tide_world_copy(&s->ring[s->ticks % s->p.kept], &s->world);
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
    for (uint32_t i = 0; i < s->p.every; i++) {
        step(s, jobs);
        const uint64_t start = tide_time_now_ns();
        s->hashes[s->ticks % s->p.kept] = hash_world(&s->world, jobs);
        s->part[PART_HASH] += since(start);
    }
    const uint32_t back = plan_back(&s->p, s->ticks);
    if (!back) return;
    const uint32_t now = s->ticks;
    const uint64_t start = tide_time_now_ns();
    s->ticks -= back;
    tide_world_copy(&s->world, &s->ring[s->ticks % s->p.kept]);
    s->part[PART_RESTORE] += since(start);
    while (s->ticks < now) step(s, jobs);
    const uint64_t hashed = tide_time_now_ns();
    s->mismatches += hash_world(&s->world, jobs) != s->hashes[now % s->p.kept];
    s->part[PART_HASH] += since(hashed);
}

static void parts(const void *state, double *ms)
{
    const tide_way *s = state;
    for (uint32_t k = 0; k < PART_COUNT; k++) ms[k] = s->part[k];
}

static uint64_t world_digest(const tide_world *w)
{
    uint64_t sum = 0;
    for (uint32_t row = 0; row < w->arch0_Position_Velocity.count; row++) {
        if (TIDE_ROW_GONE(w, arch0_Position_Velocity, row)) continue; // A hole an entity left
        const Position p = TIDE_AT(w, arch0_Position_Velocity, Position, row);
        const Velocity v = TIDE_AT(w, arch0_Position_Velocity, Velocity, row);
        const mover_item item = {{p.value.x, p.value.y, p.value.z}, {v.value.x, v.value.y, v.value.z}};
        sum += versus_item(&item, sizeof item);
    }
    for (uint32_t row = 0; row < w->arch1_Position_Turret.count; row++) {
        if (TIDE_ROW_GONE(w, arch1_Position_Turret, row)) continue; // A hole an entity left
        const Position p = TIDE_AT(w, arch1_Position_Turret, Position, row);
        const Turret t = TIDE_AT(w, arch1_Position_Turret, Turret, row);
        const turret_item item = {{p.value.x, p.value.y, p.value.z}, t.cooldown, t.reload, t.slot, t.shots};
        sum += versus_item(&item, sizeof item);
    }
    const int32_t notes = w->Score.heat.at ? tide_list_read_count(&w->heap, w->Score.heat) : 0;
    for (int32_t i = 0; i < notes; i++) {
        const note_item item = {i, *(const int32_t *)tide_list_read(&w->heap, w->Score.heat, i, sizeof(int32_t))};
        sum += versus_item(&item, sizeof item);
    }
    return sum + versus_item(&w->Score.shots, sizeof w->Score.shots);
}

// The world, whether every tick run again came out as it had, and the world
// kept from before the latest tick: every way keeps the same ring
static uint64_t digest(const void *state)
{
    const tide_way *s = state;
    uint64_t sum = world_digest(&s->world) + versus_item(&s->mismatches, sizeof s->mismatches);
    if (s->ticks) {
        const uint64_t before = world_digest(&s->ring[(s->ticks - 1u) % s->p.kept]);
        sum += versus_item(&before, sizeof before);
    }
    return sum;
}

static void destroy(void *state)
{
    tide_way *s = state;
    tide_world_free(&s->world);
    for (uint32_t k = 0; k < s->p.kept; k++) tide_world_free(&s->ring[k]);
    free(s->ring);
    free(s->hashes);
    free(s);
}

int main(int argc, char **argv)
{
    const versus_way ways[] = {{"Tide", make, tick, digest, destroy}, rollback_c, rollback_c_dirty};
    const versus_parts timed[] = {parts, rollback_c_parts, rollback_c_dirty_parts};
    static const char *const part_names[PART_COUNT] = {"tick", "snapshot", "hash", "restore"};
    const versus_desc desc = {
        .name = "versus_rollback",
        .about = "a client's frames: a few ticks, each kept and hashed, then a correction that restores a kept world and runs the ticks since again",
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
