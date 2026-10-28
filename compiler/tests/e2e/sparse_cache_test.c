#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide_test.h"

enum { COUNT = 2051, MOST = COUNT + 17 };
typedef struct expected_item {
    Item value;
    bool needed, blocked;
} expected_item;
typedef struct model {
    expected_item items[MOST];
    int rows[MOST], seen[MOST];
    int count, next, seen_count, bias;
} model;

static tide_world world, kept;
static bool pooled;

static void alone(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    pooled = true;
    work(context, 0);
}
static const tide_jobs eight = {.threads = 8, .run = alone};

static void model_birth(model *m, const int ordinal, const bool last)
{
    TIDE_REQUIRE(ordinal < MOST && m->count < MOST);
    m->rows[m->count++] = ordinal;
    m->items[ordinal] = (expected_item){
        .value = {.ordinal = ordinal, .value = (float)ordinal * 0.03125f + 1.0f, .other = -0.5f, .small = 9,
                  .spare = {1.0f, -2.0f, 3.0f}},
        .needed = (ordinal + m->bias) % 97 == 0 || last,
        .blocked = (ordinal + m->bias) % 191 == 0,
    };
}

static void init(tide_world *w, model *m, const int bias)
{
    memset(m, 0, sizeof *m);
    m->bias = bias;
    m->next = COUNT;
    tide_world_init(w, 1.0f / 64.0f);
    w->Rules.bias = bias;
    tide_world_tick(w);
    for (int i = 0; i < COUNT; i++) model_birth(m, i, i == COUNT - 1);
}

static void model_tick(model *m, const int mode)
{
    m->seen_count = 0;
    for (int row = 0; row < m->count; row++) {
        expected_item *e = &m->items[m->rows[row]];
        if (!e->needed || e->blocked) continue;
        e->value.value = (e->value.value + 0.125f) * 0.5f;
        e->value.other -= e->value.value * 0.25f;
        e->value.visits++;
        e->value.small = (uint8_t)(e->value.small + 19u);
        m->seen[m->seen_count++] = e->value.ordinal;
    }
    for (int row = 0; row < m->count; row++) {
        expected_item *e = &m->items[m->rows[row]];
        if (mode == 1 || mode == 6) {
            const int offset = mode == 6 ? 41 : 0;
            e->needed = (e->value.ordinal + m->bias + offset) % 97 == 0;
            e->blocked = (e->value.ordinal + m->bias + offset) % 191 == 0;
        }
        if (mode == 2) { e->needed = true; e->blocked = false; }
        if (mode == 3) e->needed = false;
    }
    if (mode == 4) {
        // Commands were recorded in pre-removal row order. Each removal fills
        // its hole with the current last row, including its sparse presence.
        const int gone[] = {1, 1023, 2048};
        for (size_t g = 0; g < sizeof gone / sizeof *gone; g++) {
            for (int row = 0; row < m->count; row++) {
                if (m->rows[row] != gone[g]) continue;
                m->rows[row] = m->rows[--m->count];
                break;
            }
        }
    }
    if (mode == 5) {
        for (int i = 0; i < 17; i++) model_birth(m, m->next++, i == 16);
    }
}

static void check_world(const tide_world *w, const model *m)
{
    TIDE_REQUIRE(w->arch0_Item.count == (uint32_t)m->count);
    TIDE_REQUIRE(tide_list_count(w->Rules.seen) == m->seen_count);
    for (int i = 0; i < m->seen_count; i++) {
        const int32_t *seen = tide_list_at(w->Rules.seen, i, sizeof *seen);
        TIDE_REQUIRE(seen && *seen == m->seen[i]);
    }
    for (int row = 0; row < m->count; row++) {
        const Item got = TIDE_AT(w, arch0_Item, Item, (uint32_t)row);
        const expected_item *e = &m->items[m->rows[row]];
        const tide_entity entity = TIDE_ENTITY_AT(w, arch0_Item, (uint32_t)row);
        TIDE_REQUIRE(got.ordinal == e->value.ordinal && got.visits == e->value.visits && got.small == e->value.small);
        TIDE_REQUIRE(memcmp(&got.value, &e->value.value, sizeof got.value) == 0);
        TIDE_REQUIRE(memcmp(&got.other, &e->value.other, sizeof got.other) == 0);
        TIDE_REQUIRE(memcmp(&got.spare, &e->value.spare, sizeof got.spare) == 0);
        TIDE_REQUIRE(tide_has_Needed(w, entity) == e->needed && tide_has_Blocked(w, entity) == e->blocked);
    }
}

static uint8_t *pack(const tide_world *w, uint32_t *size)
{
    *size = tide_world_pack(w, NULL, 0);
    uint8_t *bytes = malloc(*size);
    TIDE_CHECK(bytes != NULL);
    if (!bytes) return NULL;
    const bool wrote = tide_world_pack(w, bytes, *size) == *size;
    TIDE_CHECK(wrote);
    if (!wrote) { free(bytes); return NULL; }
    struct tide_rows_cache *cache = (struct tide_rows_cache *)(uintptr_t)1;
    memcpy(&cache, bytes + offsetof(tide_world, tide_rows_cache), sizeof cache);
    TIDE_CHECK(cache == NULL);
    return bytes;
}

static void cold_and_warm_pack_same(const tide_world *warm)
{
    tide_world cold = {0};
    tide_world_copy(&cold, warm);
    TIDE_REQUIRE(cold.tide_rows_cache == NULL && warm->tide_rows_cache != NULL);
    TIDE_REQUIRE(tide_world_hash(&cold) == tide_world_hash(warm));
    uint32_t warm_size, cold_size;
    uint8_t *warm_bytes = pack(warm, &warm_size), *cold_bytes = pack(&cold, &cold_size);
    TIDE_REQUIRE(warm_bytes && cold_bytes);
    TIDE_REQUIRE(warm_size == cold_size && memcmp(warm_bytes, cold_bytes, warm_size) == 0);
    free(cold_bytes);
    free(warm_bytes);
    tide_world_free(&cold);
}

enum { STEPS = 18 };
static void play(const bool snapshot, const tide_jobs *jobs, uint64_t hashes[STEPS])
{
    static const int modes[STEPS] = {0, 0, 4, 0, 5, 0, 1, 0, 2, 0, 3, 0, 6, 0, 2, 0, 1, 0};
    model *m = calloc(1, sizeof *m);
    TIDE_REQUIRE(m);
    init(&world, m, 0);
    for (int step = 0; step < STEPS; step++) {
        world.Rules.mode = modes[step];
        uint64_t before = 0;
        if (snapshot) {
            tide_world_copy(&kept, &world);
            before = tide_world_hash(&kept);
        }
        model_tick(m, modes[step]);
        tide_world_tick_on(&world, jobs);
        check_world(&world, m);
        hashes[step] = tide_world_hash(&world);
        if (snapshot) TIDE_REQUIRE(tide_world_hash(&kept) == before);
        if (step == 1) cold_and_warm_pack_same(&world);
        if (step == 7) {
            uint32_t size;
            uint8_t *bytes = pack(&world, &size);
            TIDE_REQUIRE(bytes);
            TIDE_REQUIRE(world.tide_rows_cache != NULL);
            TIDE_REQUIRE(tide_world_unpack(&world, bytes, size));
            TIDE_REQUIRE(world.tide_rows_cache == NULL && tide_world_hash(&world) == hashes[step]);
            free(bytes);
        }
    }
    tide_world_free(&world);
    tide_world_free(&kept);
    TIDE_REQUIRE(world.tide_rows_cache == NULL && kept.tide_rows_cache == NULL);
    free(m);
}

TIDE_TEST(sparse_cache_keeps_dynamic_membership_rows_math_and_snapshots)
{
    uint64_t baseline[STEPS], shared[STEPS], pieces[STEPS];
    play(false, NULL, baseline);
    play(true, NULL, shared);
    play(true, &eight, pieces);
    TIDE_REQUIRE(pooled);
    for (int step = 0; step < STEPS; step++) {
        TIDE_REQUIRE(shared[step] == baseline[step] && pieces[step] == baseline[step]);
    }
}

TIDE_TEST(sparse_cache_world_copy_and_restore_keep_ownership_separate)
{
    tide_world a = {0}, b = {0};
    model *ma = calloc(1, sizeof *ma), *mb = calloc(1, sizeof *mb);
    TIDE_REQUIRE(ma && mb);
    init(&a, ma, 0);
    init(&b, mb, 11);
    for (int tick = 0; tick < 2; tick++) {
        model_tick(ma, 0); tide_world_tick(&a); check_world(&a, ma);
        model_tick(mb, 0); tide_world_tick(&b); check_world(&b, mb);
    }
    struct tide_rows_cache *const owned = b.tide_rows_cache;
    TIDE_REQUIRE(owned && a.tide_rows_cache && owned != a.tide_rows_cache);
    tide_world_copy(&b, &a);
    *mb = *ma;
    TIDE_REQUIRE(b.tide_rows_cache == owned && tide_world_hash(&b) == tide_world_hash(&a));
    model_tick(mb, 0); tide_world_tick(&b); check_world(&b, mb);
    uint32_t size;
    uint8_t *bytes = pack(&a, &size);
    TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_world_unpack(&b, bytes, size));
    *mb = *ma;
    TIDE_REQUIRE(b.tide_rows_cache == NULL);
    model_tick(mb, 0); tide_world_tick_on(&b, &eight); check_world(&b, mb);
    free(bytes);
    tide_world_free(&a);
    tide_world_free(&b);
    free(ma);
    free(mb);
}

TIDE_TEST(sparse_cache_unpack_never_owns_a_received_pointer)
{
    model *m = calloc(1, sizeof *m);
    TIDE_REQUIRE(m);
    init(&world, m, 0);
    tide_world_tick(&world);
    uint32_t size;
    uint8_t *bytes = pack(&world, &size);
    TIDE_REQUIRE(bytes);
    struct tide_rows_cache *const hostile = (struct tide_rows_cache *)(uintptr_t)1;
    memcpy(bytes + offsetof(tide_world, tide_rows_cache), &hostile, sizeof hostile);
    // Both valid and rejected complete raw headers must clear the untrusted
    // pointer before any reader can fail and call world_free.
    tide_world loaded = {0};
    TIDE_REQUIRE(tide_world_unpack(&loaded, bytes, size));
    TIDE_REQUIRE(loaded.tide_rows_cache == NULL && tide_world_hash(&loaded) == tide_world_hash(&world));
    tide_world_free(&loaded);
    TIDE_REQUIRE(!tide_world_unpack(&loaded, bytes, (uint32_t)sizeof loaded));
    TIDE_REQUIRE(loaded.tide_rows_cache == NULL);
    tide_world_free(&loaded);
    tide_world_free(&world);
    free(bytes);
    free(m);
}
