#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/events.h"
#include "tide/jobs.h"
#include "tide_test.h"

// events_runs.tide, worked out in C: every shooter's Hit, then its Ping, then
// its Roll, then every third one's graze, each to its target as its turn finds
// it, then what the handlers sent, in the order they sent it.

#define TARGETS 3000
#define SHOOTERS 5000
#define TICKS 6u

typedef struct model_target {
    int32_t value; // Health's, or Armor's
    bool armor, shielded, alive;
} model_target;

typedef struct model {
    model_target targets[TARGETS];
    int32_t *trace;
    uint32_t trace_count;
    uint32_t pings; // Pings that reached a target, which the local handler hears
    uint32_t *heard; // Their targets, in order
} model;

static void model_add(model *m, const int32_t value)
{
    m->trace = realloc(m->trace, (m->trace_count + 1u) * sizeof *m->trace);
    TIDE_REQUIRE(m->trace);
    m->trace[m->trace_count++] = value;
}

static void model_init(model *m)
{
    memset(m, 0, sizeof *m);
    for (int32_t i = 0; i < TARGETS; i++) {
        model_target *t = &m->targets[i];
        t->armor = i % 4 == 3;
        t->shielded = !t->armor && i % 6 == 1;
        t->value = t->armor ? 50 : 30;
        t->alive = true;
    }
}

static int32_t aim(const int32_t shooter)
{
    return (shooter * 7 + 3) % TARGETS;
}

static void model_tick(model *m, const uint32_t tick)
{
    if (tick == 0) return; // The shooters spawn at its end
    if (tick == 3) {
        for (int32_t i = 0; i < TARGETS; i++) {
            if (i % 9 == 2) m->targets[i].alive = false;
        }
    }
    int32_t *echoes = malloc(2u * SHOOTERS * sizeof *echoes);
    TIDE_REQUIRE(echoes);
    uint32_t echo_count = 0;
    for (int32_t s = 0; s < SHOOTERS; s++) {
        model_target *t = &m->targets[aim(s)];
        if (!t->alive || t->armor || t->shielded) continue;
        t->value -= 1 + s % 13;
        model_add(m, s * 10000 + aim(s));
        if (t->value <= 0) {
            t->value += 100;
            echoes[echo_count++] = s;
        }
    }
    m->heard = realloc(m->heard, (m->pings + SHOOTERS) * sizeof *m->heard);
    TIDE_REQUIRE(m->heard);
    for (int32_t s = 0; s < SHOOTERS; s++) {
        model_target *t = &m->targets[aim(s)];
        if (!t->alive) continue;
        m->heard[m->pings++] = (uint32_t)aim(s);
        if (!t->armor) {
            t->value += 1;
            continue;
        }
        t->value -= 1;
        model_add(m, -1 - (s * 10000 + aim(s)));
    }
    for (int32_t s = 0; s < SHOOTERS; s += 97) model_add(m, 900000000 + s);
    for (int32_t s = 0; s < SHOOTERS; s += 3) { // Graze
        model_target *t = &m->targets[aim(s)];
        if (!t->alive || t->armor || t->shielded) continue;
        t->value -= 1;
        model_add(m, (-1 - s) * 10000 + aim(s));
        if (t->value <= 0) {
            t->value += 100;
            echoes[echo_count++] = -1 - s;
        }
    }
    for (uint32_t i = 0; i < echo_count; i++) model_add(m, 2000000000 - echoes[i]);
    free(echoes);
}

static tide_entity target_of(const tide_world *w, const int32_t i)
{
    return *(const tide_entity *)tide_list_read(&w->heap, w->Log.targets, (uint32_t)i, sizeof(tide_entity));
}

static void check_world(const tide_world *w, const model *m)
{
    TIDE_REQUIRE(tide_list_count(w->Log.targets) == TARGETS);
    uint32_t wrong = 0;
    for (int32_t i = 0; i < TARGETS; i++) {
        const model_target *t = &m->targets[i];
        const tide_entity e = target_of(w, i);
        if (!t->alive) {
            wrong += tide_has_Target(w, e);
            continue;
        }
        if (t->armor) wrong += !tide_has_Armor(w, e) || tide_get_Armor(w, e).value != t->value;
        else wrong += !tide_has_Health(w, e) || tide_get_Health(w, e).value != t->value;
    }
    TIDE_CHECK(wrong == 0);
    const uint32_t count = (uint32_t)tide_list_count(w->Log.trace);
    TIDE_CHECK(count == m->trace_count);
    uint32_t differ = 0;
    for (uint32_t i = 0; i < m->trace_count && i < count; i++) {
        differ += *(const int32_t *)tide_list_read(&w->heap, w->Log.trace, i, sizeof(int32_t)) != m->trace[i];
    }
    TIDE_CHECK(differ == 0);
    TIDE_CHECK(w->commands.count == 0);
}

static void work(void *self, void (*run)(void *, uint32_t), void *context)
{
    (void)self;
    run(context, 0u);
}

TIDE_TEST(events_runs_take_each_event_as_its_turn_finds_its_target)
{
    model m;
    model_init(&m);
    tide_world one = {0}, many = {0}, heard = {0}, back = {0};
    tide_world_init(&one, 1.0f);
    tide_world_copy(&many, &one);
    tide_world_copy(&heard, &one);
    // A pool that says it has four threads: each system's chunks record into queues of their own
    const tide_jobs jobs = {NULL, 4u, work, NULL, NULL, NULL, NULL, NULL};
    tide_event_log log = {0};
    uint32_t back_from = 0;
    for (uint32_t tick = 0; tick < TICKS; tick++) {
        model_tick(&m, tick);
        tide_world_tick(&one);
        tide_world_tick_on(&many, &jobs);
        tide_world_tick_events(&heard, NULL, &log);
        if (tick == 2u) { // The world as bytes, ticked on from there
            const uint32_t size = tide_world_pack(&one, NULL, 0);
            uint8_t *bytes = malloc(size);
            TIDE_REQUIRE(bytes);
            TIDE_REQUIRE(tide_world_pack(&one, bytes, size) == size);
            TIDE_REQUIRE(tide_world_unpack(&back, bytes, size));
            free(bytes);
            back_from = tick + 1u;
        } else if (back_from) {
            tide_world_tick(&back);
        }
        check_world(&one, &m);
        TIDE_CHECK(tide_world_hash(&many) == tide_world_hash(&one));
        TIDE_CHECK(tide_world_hash(&heard) == tide_world_hash(&one));
        if (back_from) TIDE_CHECK(tide_world_hash(&back) == tide_world_hash(&one));
    }
    check_world(&many, &m);
    check_world(&back, &m);
    // The local handler hears each Ping that reached its target, in order
    uint32_t records = 0, misplaced = 0;
    for (uint32_t at = 0; at < log.size;) {
        const tide_heard *h = (const tide_heard *)(log.bytes + at);
        if (h->event != TIDE_HEARD_ROWS && h->handlers) {
            if (records >= m.pings || !tide_entity_equal(h->target, target_of(&one, (int32_t)m.heard[records]))) misplaced++;
            records++;
        }
        at += h->size;
    }
    TIDE_CHECK(records == m.pings);
    TIDE_CHECK(misplaced == 0);
    tide_event_log_free(&log);
    free(m.trace);
    free(m.heard);
    tide_world_free(&back);
    tide_world_free(&heard);
    tide_world_free(&many);
    tide_world_free(&one);
}
