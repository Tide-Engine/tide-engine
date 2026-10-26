// Events in C: a turret's hit is a write straight into its target's health,
// in an array by target.

#include <stdbool.h>
#include <stdlib.h>

#include "tide/math.h" // tide_hash_i2: the random numbers Tide's Math.Hash gives
#include "tide/page.h" // tide_alloc: memory every thread can use on the web
#include "versus.h"

typedef struct events {
    uint32_t targets, turrets;
    bool ready;
    int32_t *health; // By target
    uint32_t *aim;   // By turret: its target
    int32_t *damage;
} events;

static void *make(const versus_flag *flags)
{
    events *e = tide_alloc_zeroed(1, sizeof *e);
    e->targets = flags[0].value;
    e->turrets = flags[1].value;
    return e;
}

static void setup(events *e)
{
    e->health = tide_alloc(e->targets * sizeof *e->health);
    e->aim = tide_alloc(e->turrets * sizeof *e->aim);
    e->damage = tide_alloc(e->turrets * sizeof *e->damage);
    for (uint32_t i = 0; i < e->targets; i++) e->health[i] = 1000;
    for (uint32_t i = 0; i < e->turrets; i++) {
        const int32_t h = tide_hash_i2((tide_int2){(int32_t)i, 7});
        e->aim[i] = (uint32_t)(h % (int32_t)e->targets);
        e->damage[i] = 1 + h / (int32_t)e->targets % 50;
    }
}

static void tick(void *state, const tide_jobs *jobs)
{
    (void)jobs; // Turrets share targets, and one thread goes faster than any way of sharing them
    events *e = state;
    if (!e->ready) { // Tide's first tick spawns them, and they shoot from the next
        setup(e);
        e->ready = true;
        return;
    }
    int32_t *restrict health = e->health;
    const uint32_t *restrict aim = e->aim;
    const int32_t *restrict damage = e->damage;
    for (uint32_t i = 0; i < e->turrets; i++) {
        int32_t *const h = &health[aim[i]];
        *h -= damage[i];
        if (*h <= 0) *h += 1000;
    }
}

static uint64_t digest(const void *state)
{
    const events *e = state;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < e->targets; i++) {
        const int32_t item[2] = {(int32_t)i, e->health[i]};
        sum += versus_item(item, sizeof item);
    }
    return sum;
}

static void destroy(void *state)
{
    events *e = state;
    free(e->health);
    free(e->aim);
    free(e->damage);
    free(e);
}

const versus_way events_c = {"C", make, tick, digest, destroy};

// The same as an event a handler takes at the end of the tick, as Tide's are:
// each hit recorded (its target and damage, 8 bytes), then all of them taken
// in the order they were sent. What deferring costs C, written as tightly as
// C allows; the result is the same, as nothing reads health in between.
typedef struct hit {
    uint32_t target;
    int32_t damage;
} hit;

typedef struct deferred {
    events e;
    hit *hits;
} deferred;

static void *make_deferred(const versus_flag *flags)
{
    deferred *d = tide_alloc_zeroed(1, sizeof *d);
    d->e.targets = flags[0].value;
    d->e.turrets = flags[1].value;
    return d;
}

// One thread: taking the hits on threads, each piece the targets in a range of
// its own going through every hit, was slower than one thread, as every thread
// reads every hit and branches on where its target is.
static void tick_deferred(void *state, const tide_jobs *jobs)
{
    (void)jobs;
    deferred *d = state;
    events *e = &d->e;
    if (!e->ready) {
        setup(e);
        d->hits = tide_alloc(e->turrets * sizeof *d->hits);
        e->ready = true;
        return;
    }
    hit *restrict hits = d->hits;
    const uint32_t *restrict aim = e->aim;
    const int32_t *restrict damage = e->damage;
    for (uint32_t i = 0; i < e->turrets; i++) hits[i] = (hit){aim[i], damage[i]};
    int32_t *restrict health = e->health;
    for (uint32_t i = 0; i < e->turrets; i++) {
        int32_t *const h = &health[hits[i].target];
        *h -= hits[i].damage;
        if (*h <= 0) *h += 1000;
    }
}

static uint64_t digest_deferred(const void *state)
{
    return digest(&((const deferred *)state)->e);
}

static void destroy_deferred(void *state)
{
    deferred *d = state;
    free(d->e.health);
    free(d->e.aim);
    free(d->e.damage);
    free(d->hits);
    free(d);
}

const versus_way events_c_deferred = {"C, deferred", make_deferred, tick_deferred, digest_deferred, destroy_deferred};
