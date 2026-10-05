// The C side of extern_system.tide: its systems written in C. Each is called
// with a run of rows (tide/rows.h), then the singletons its declaration takes.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/math.h"
#include "tide/rows.h"

// The game's components and singletons, field for field, with the padding
// they write out
typedef struct place {
    tide_float2 at;
    int32_t id;
} place;

typedef struct speed {
    tide_float2 by;
} speed;

typedef struct badge {
    int64_t serial;
    tide_entity me;
    bool lit;
    uint8_t unused[7];
} badge;

typedef struct dots {
    uint8_t a;
    uint8_t b;
    uint8_t c;
} dots;

typedef struct wheel {
    float angle;
    float turns;
    int32_t id;
} wheel;

typedef struct game_time {
    float dt;
    int32_t tick;
    float t;
    int32_t tick_rate;
} game_time;

typedef struct rules {
    float pull;
    int32_t bonus;
} rules;

typedef struct tally {
    int32_t runs;
    int32_t rows;
    int32_t order;
    int32_t order_in_tide;
    int32_t bonus;
    int32_t once;
    int32_t once_rows;
    int32_t tick;
    int32_t wheels;
} tally;

typedef struct order {
    int32_t seen;
    int32_t zero;
    int32_t first;
    int32_t in_c;
    int32_t last;
} order;

_Static_assert(sizeof(place) == 12 && sizeof(badge) == 24 && sizeof(dots) == 3 && sizeof(wheel) == 12,
               "extern_system.tide's components changed: these are theirs");

// For the test: the fewest and the most rows a call of xs_spin was handed, and
// how many calls there were, since it last asked.
int xs_spin_least;
int xs_spin_most;
int xs_spin_calls;

// Components are numbered as the declaration takes them: place is 0, speed 1.
void xs_move(tide_rows *rows, const game_time *time)
{
    const uint32_t count = tide_rows_count(rows);
    float *x = tide_rows_floats_mut(rows, 0, offsetof(place, at.x));
    float *y = tide_rows_floats_mut(rows, 0, offsetof(place, at.y));
    const float *dx = tide_rows_floats(rows, 1, offsetof(speed, by.x));
    const float *dy = tide_rows_floats(rows, 1, offsetof(speed, by.y));
    for (uint32_t i = 0; i < count; i++) {
        x[i] += dx[i] * time->dt;
        y[i] += dy[i] * time->dt;
    }
}

void xs_idle(tide_rows *rows)
{
    const uint32_t count = tide_rows_count(rows);
    float *x = tide_rows_floats_mut(rows, 0, offsetof(place, at.x));
    int32_t *id = tide_rows_ints_mut(rows, 0, offsetof(place, id));
    for (uint32_t i = 0; i < count; i++) {
        x[i] = x[i] * 1.0f;
        id[i] |= 0;
    }
    // A row put back as it was asks for no lane
    place p;
    tide_rows_get(rows, 0, 0, &p);
    tide_rows_set(rows, 0, 0, &p);
}

void xs_count(tide_rows *rows, const rules *rule, tally *t)
{
    const uint32_t count = tide_rows_count(rows);
    const int32_t *id = tide_rows_ints(rows, 0, offsetof(place, id));
    // Signed overflow is Tide's to wrap, and C's to leave alone
    uint32_t seen = (uint32_t)t->order;
    for (uint32_t i = 0; i < count; i++) seen = seen * 31u + (uint32_t)id[i];
    t->order = (int32_t)seen;
    t->runs++;
    t->rows += (int32_t)count;
    t->bonus = rule->bonus;
}

void xs_stamp(tide_rows *rows)
{
    const uint32_t count = tide_rows_count(rows);
    const tide_entity *entities = tide_rows_entities(rows);
    const int32_t *id = tide_rows_ints(rows, 1, offsetof(place, id));
    for (uint32_t i = 0; i < count; i++) {
        badge b;
        tide_rows_get(rows, 0, i, &b);
        b.serial += id[i];
        b.me = entities[i];
        b.lit = !b.lit;
        tide_rows_set(rows, 0, i, &b);
    }
}

void xs_dot(tide_rows *rows)
{
    for (uint32_t i = 0; i < tide_rows_count(rows); i++) {
        dots d;
        tide_rows_get(rows, 0, i, &d);
        d.a = (uint8_t)(d.a + 1u);
        d.c = (uint8_t)(d.a + d.b);
        tide_rows_set(rows, 0, i, &d);
    }
}

void xs_weigh(tide_rows *rows, const rules *rule)
{
    float *dy = tide_rows_floats_mut(rows, 0, offsetof(speed, by.y));
    for (uint32_t i = 0; i < tide_rows_count(rows); i++) dy[i] += rule->pull;
}

void xs_once(tide_rows *rows, tally *t, const game_time *time)
{
    t->once++;
    t->once_rows += (int32_t)tide_rows_count(rows);
    t->tick = time->tick;
}

void xs_wheels(tide_rows *rows, tally *t)
{
    t->wheels += (int32_t)tide_rows_count(rows);
    // Its rows are entities, each one's own
    const tide_entity *entities = tide_rows_entities(rows);
    for (uint32_t i = 1; i < tide_rows_count(rows); i++) t->wheels -= tide_entity_equal(entities[i], entities[i - 1]);
}

void xs_spin(tide_rows *rows)
{
    const uint32_t count = tide_rows_count(rows);
    // Its runs are on several threads at once in a tick that shares them out
    __atomic_fetch_add(&xs_spin_calls, 1, __ATOMIC_RELAXED);
    int least = __atomic_load_n(&xs_spin_least, __ATOMIC_RELAXED);
    while ((least == 0 || (int)count < least) && !__atomic_compare_exchange_n(&xs_spin_least, &least, (int)count, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
    int most = __atomic_load_n(&xs_spin_most, __ATOMIC_RELAXED);
    while ((int)count > most && !__atomic_compare_exchange_n(&xs_spin_most, &most, (int)count, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
    float *angle = tide_rows_floats_mut(rows, 0, offsetof(wheel, angle));
    float *turns = tide_rows_floats_mut(rows, 0, offsetof(wheel, turns));
    const int32_t *id = tide_rows_ints(rows, 0, offsetof(wheel, id));
    for (uint32_t i = 0; i < count; i++) {
        angle[i] += 0.125f * (float)(id[i] % 7);
        if (angle[i] >= 16.0f) {
            angle[i] -= 16.0f;
            turns[i] += 1.0f;
        }
    }
}

void xs_mark(tide_rows *rows, order *o)
{
    (void)rows;
    o->seen++;
    o->in_c = o->seen;
}

void xs_zero(tide_rows *rows, order *o)
{
    (void)rows;
    o->seen++;
    o->zero = o->seen;
}
