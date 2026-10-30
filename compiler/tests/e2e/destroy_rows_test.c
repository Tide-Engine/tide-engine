#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"

enum { FIRST = 4099, MORE = 17, COUNT = FIRST + MORE, TICKS = 13 };
#define DT (1.0f / 64.0f)

static tide_world world, kept;
static tide_world *observed_world;

void CheckQueue(const int32_t ordinal)
{
    TIDE_REQUIRE(observed_world);
    const tide_queue *queue = tide_recording(&observed_world->commands);
    TIDE_REQUIRE(queue->count == (uint32_t)(ordinal + 1) / 2u);
}

static void alone(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    work(context, 0);
}

static const tide_jobs eight = {.threads = 8, .run = alone};

typedef struct model {
    Bullet items[COUNT];
    tide_entity handles[COUNT];
    uint32_t order[COUNT], row[COUNT], free_ids[COUNT], generations[COUNT + 16];
    uint32_t live, holes, made, free_count, next; // live: the table's rows, holes among them
    int32_t gone[COUNT], values[COUNT * 4];
    tide_float2 positions[COUNT];
    uint32_t gone_count;
} model;

static int32_t as_int(const uint32_t value)
{
    int32_t result;
    memcpy(&result, &value, sizeof result);
    return result;
}

#define HOLE UINT32_MAX // A row an entity left (tide_table_remove)

#define CHUNK_ROWS 2048u // Bullet's chunks (arch_shift)

// A row for `ordinal`: the lowest hole of the last chunk, or a new one at the end.
static void add_row(model *m, const uint32_t ordinal)
{
    uint32_t at = m->live;
    for (uint32_t row = m->live ? (m->live - 1u) / CHUNK_ROWS * CHUNK_ROWS : 0u; row < m->live; row++) {
        if (m->order[row] != HOLE) continue;
        at = row;
        break;
    }
    if (at < m->live) m->holes--;
    else m->live++;
    m->order[at] = ordinal;
    m->row[ordinal] = at;
}

static void drop_trailing_holes(model *m)
{
    while (m->live && m->order[m->live - 1u] == HOLE) {
        m->live--;
        m->holes--;
    }
}

// Its row is a hole, unless it's the last, which goes with the holes before it.
static void remove_row(model *m, const uint32_t ordinal)
{
    const uint32_t at = m->row[ordinal];
    m->row[ordinal] = UINT32_MAX;
    if (at + 1u == m->live) {
        m->live--;
        drop_trailing_holes(m);
        return;
    }
    m->order[at] = HOLE;
    m->holes++;
}

// The tick's end: past a 32nd of the rows, holes are filled from the end, lowest first.
static void settle(model *m)
{
    if ((uint64_t)m->holes * 32u <= m->live) return;
    for (;;) {
        drop_trailing_holes(m);
        if (!m->holes) break;
        uint32_t at = 0;
        while (m->order[at] != HOLE) at++;
        const uint32_t moved = m->order[--m->live];
        m->order[at] = moved;
        m->row[moved] = at;
        m->holes--;
    }
}

static void birth(model *m, const uint32_t first, const uint32_t count)
{
    for (uint32_t i = first; i < first + count; i++) {
        const uint32_t index = m->free_count ? m->free_ids[--m->free_count] : m->next++;
        TIDE_REQUIRE(index < COUNT + 16u);
        m->handles[i] = (tide_entity){index, ++m->generations[index]};
        m->items[i] = (Bullet){.ordinal = (int32_t)i,
            .position = {(float)(i % 3u) * 0.125f, -0.0f},
            .velocity = {(float)((int32_t)(i % 17u) - 8) * 0.25f, (float)((int32_t)(i % 5u) - 2) * 0.125f},
            .life = 1 + (int32_t)(i % 7u), .wrapped = (int32_t)i};
        add_row(m, i);
        m->made++;
    }
}

// Independent row/slot model: reserve births before the deferred deaths, leave
// a hole for each (filled in the last chunk, settled from the end past a 32nd),
// and reuse slots last-freed-first with generations.
static void model_tick(model *m, const uint32_t tick)
{
    uint32_t died[COUNT], deaths = 0;
    for (uint32_t row = 0; row < m->live; row++) {
        const uint32_t ordinal = m->order[row];
        if (ordinal == HOLE) continue;
        Bullet *b = &m->items[ordinal];
        b->position.x += b->velocity.x * DT;
        b->position.y += b->velocity.y * DT;
        b->life--;
        b->wrapped = as_int((uint32_t)b->wrapped + UINT32_C(2147483647));
        b->divided = 0; // Tide's divide-by-zero contract.
        b->minimumDivided = INT32_MIN; // INT32_MIN / -1 wraps.
        b->shifted = as_int((uint32_t)b->wrapped << (35u & 31u));
        if (b->life <= 0) died[deaths++] = ordinal;
    }
    if (tick == 0) birth(m, 0, FIRST);
    if (tick == 4) birth(m, FIRST, MORE);
    for (uint32_t i = 0; i < deaths; i++) {
        const uint32_t ordinal = died[i];
        const Bullet *b = &m->items[ordinal];
        const uint32_t trace = m->gone_count++;
        m->gone[trace] = (int32_t)ordinal;
        m->positions[trace] = b->position;
        m->values[trace * 4u] = b->wrapped;
        m->values[trace * 4u + 1u] = b->divided;
        m->values[trace * 4u + 2u] = b->minimumDivided;
        m->values[trace * 4u + 3u] = b->shifted;
        remove_row(m, ordinal);
        const tide_entity e = m->handles[ordinal];
        m->generations[e.index]++;
        m->free_ids[m->free_count++] = e.index;
    }
    settle(m);
}

static void check_world(const model *m)
{
    TIDE_REQUIRE(tide_list_count(world.Births.items) == (int32_t)m->made);
    for (uint32_t i = 0; i < m->made; i++) {
        const tide_entity *e = tide_list_read(&world.heap, world.Births.items, i, sizeof *e);
        TIDE_REQUIRE(e && tide_entity_equal(*e, m->handles[i]));
        if (m->row[i] == UINT32_MAX) {
            TIDE_REQUIRE(!tide_has_Bullet(&world, *e));
        } else {
            const Bullet actual = tide_get_Bullet(&world, *e);
            TIDE_REQUIRE(tide_has_Bullet(&world, *e) && memcmp(&actual, &m->items[i], sizeof actual) == 0);
        }
    }
    TIDE_REQUIRE(tide_list_count(world.Trace.gone) == (int32_t)m->gone_count);
    for (uint32_t i = 0; i < m->gone_count; i++) {
        const int32_t *ordinal = tide_list_read(&world.heap, world.Trace.gone, i, sizeof *ordinal);
        const tide_float2 *position = tide_list_read(&world.heap, world.Trace.positions, i, sizeof *position);
        TIDE_REQUIRE(ordinal && *ordinal == m->gone[i]);
        TIDE_REQUIRE(position && memcmp(position, &m->positions[i], sizeof *position) == 0);
        for (uint32_t k = 0; k < 4; k++) {
            const int32_t *value = tide_list_read(&world.heap, world.Trace.values, i * 4u + k, sizeof *value);
            TIDE_REQUIRE(value && *value == m->values[i * 4u + k]);
        }
    }
}

static void play(const bool snapshot, const tide_jobs *jobs, uint64_t hashes[TICKS])
{
    tide_world_init(&world, DT);
    model *m = calloc(1, sizeof *m);
    TIDE_REQUIRE(m);
    m->next = world.entities.next_unused;
    for (uint32_t tick = 0; tick < TICKS; tick++) {
        uint64_t before = 0;
        if (snapshot) {
            tide_world_copy(&kept, &world);
            before = tide_world_hash(&kept);
        }
        model_tick(m, tick);
        tide_world_tick_on(&world, jobs);
        check_world(m);
        hashes[tick] = tide_world_hash(&world);
        if (snapshot) TIDE_REQUIRE(tide_world_hash(&kept) == before);
        if (tick == 5) {
            const uint32_t size = tide_world_pack(&world, NULL, 0);
            uint8_t *bytes = malloc(size);
            tide_world restored = {0};
            TIDE_REQUIRE(bytes && tide_world_pack(&world, bytes, size) == size);
            TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
            TIDE_REQUIRE(tide_world_hash(&restored) == hashes[tick]);
            tide_world_copy(&world, &restored);
            tide_world_free(&restored);
            free(bytes);
        }
    }
    TIDE_CHECK(m->made == COUNT && m->live == 0 && m->gone_count == COUNT);
    free(m);
    tide_world_free(&world);
    tide_world_free(&kept);
}

TIDE_TEST(destroy_rows_keep_math_order_reuse_and_snapshots)
{
    uint64_t plain[TICKS], shared[TICKS], pieced[TICKS], pieced_shared[TICKS];
    play(false, NULL, plain);
    play(true, NULL, shared);
    play(false, &eight, pieced);
    play(true, &eight, pieced_shared);
    for (int i = 0; i < TICKS; i++) TIDE_REQUIRE(plain[i] == shared[i] && plain[i] == pieced[i] && plain[i] == pieced_shared[i]);
}

TIDE_TEST(destroy_rows_leave_c_observers_in_the_original_order)
{
    tide_world_init(&world, DT);
    world.Births.mode = 1;
    tide_world_tick(&world);
    observed_world = &world;
    tide_world_tick(&world);
    observed_world = NULL;
    uint32_t live = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Observed(&world, e)) continue;
        const Observed value = tide_get_Observed(&world, e);
        TIDE_REQUIRE(value.ordinal % 2 == 1 && value.value == 1);
        live++;
    }
    TIDE_CHECK(live == 129);
    tide_world_free(&world);
}

TIDE_TEST(destroy_rows_keep_helper_events_before_each_destroy)
{
    tide_world_init(&world, DT);
    world.Births.mode = 2;
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_list_count(world.Trace.events) == 518);
    for (int32_t i = 0; i < 259; i++) {
        const int32_t *notice = tide_list_read(&world.heap, world.Trace.events, (uint32_t)i * 2u, sizeof *notice);
        const int32_t *destroyed = tide_list_read(&world.heap, world.Trace.events, (uint32_t)i * 2u + 1u, sizeof *destroyed);
        TIDE_REQUIRE(notice && destroyed && *notice == i && *destroyed == -i - 1);
    }
    tide_world_free(&world);
}

TIDE_TEST(destroy_rows_preserve_filtered_fallback_and_skipped_rows)
{
    tide_world_init(&world, DT);
    world.Births.mode = 3;
    tide_world_tick(&world);
    tide_world_copy(&kept, &world);
    const uint64_t before = tide_world_hash(&kept);
    tide_world_tick(&world);
    int32_t at = 0;
    for (int32_t i = 0; i < 259; i++) {
        if (i % 3 == 0) continue;
        const int32_t *gone = tide_list_read(&world.heap, world.Trace.gone, (uint32_t)at++, sizeof *gone);
        TIDE_REQUIRE(gone && *gone == i);
    }
    TIDE_REQUIRE(tide_list_count(world.Trace.gone) == at && tide_world_hash(&kept) == before);
    tide_world_free(&world);
    tide_world_free(&kept);
}
