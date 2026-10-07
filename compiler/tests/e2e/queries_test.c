#include <inttypes.h>
#include <stdio.h>

#include "game.h"
#include "tide_test.h"

#ifdef TIDE_QUERY_THREADS
#include "tide/platform.h"
#endif

static tide_world world;
static tide_world snapshot;

// Main occupies slot 0; these are Setup's spawn handles, not query ordering.
static const tide_entity FAR = {1, 1};
static const tide_entity NEAR = {2, 1};
static const tide_entity SHIELDED = {3, 1};
static const tide_entity SHOT = {8, 1};
static const tide_entity MISS = {9, 1};
static const tide_entity COUNT_A = {10, 1};
static const tide_entity COUNT_B = {11, 1};
static const tide_entity COUNT_C = {12, 1};
static const tide_entity SAMPLE_B = {14, 1};
static const tide_entity PROBE = {16, 1};

TIDE_TEST(queries_select_a_second_set_and_defer_events)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    const Bullet shot = tide_get_Bullet(&world, SHOT);
    const Bullet miss = tide_get_Bullet(&world, MISS);
    TIDE_CHECK(tide_entity_equal(shot.hit, NEAR));
    TIDE_CHECK(shot.fraction == 0.375f); // MoveFighters ran before the query.
    TIDE_CHECK(tide_entity_is_null(miss.hit) && miss.fraction == 1.0f);
    TIDE_CHECK(shot.candidates == 2 && miss.candidates == 2);
    TIDE_CHECK(shot.emptyVisits == 0 && miss.emptyVisits == 0);
    TIDE_CHECK(shot.pairs == 2 && miss.pairs == 2);
    TIDE_CHECK(shot.continued == 1 && miss.continued == 1);
    TIDE_CHECK(shot.stopped == 1 && miss.stopped == 1);
    TIDE_CHECK(shot.markedVisits == 0 && miss.markedVisits == 0);
    TIDE_CHECK(tide_has_Seen(&world, FAR) && tide_has_Seen(&world, NEAR) && !tide_has_Seen(&world, SHIELDED));
    TIDE_CHECK(world.Stats.healthBeforeEvents == 150 && world.Stats.handles == 5);
    TIDE_CHECK(world.Stats.workVisits == 8192 && world.Stats.workPrefix == 4096 && world.Stats.impossibleVisits == 0);
    TIDE_CHECK(tide_get_Health(&world, NEAR).value == 33);
    TIDE_CHECK(tide_get_Health(&world, FAR).value == 50 && tide_get_Health(&world, SHIELDED).value == 30);
    // The query's with Fighter did not filter the outer bullets away.
    TIDE_CHECK(!tide_has_Fighter(&world, SHOT) && !tide_has_Fighter(&world, MISS));
    tide_world_tick(&world);
    TIDE_CHECK(world.Stats.healthBeforeEvents == 143 && tide_get_Health(&world, NEAR).value == 26);
    TIDE_CHECK(tide_get_Bullet(&world, SHOT).markedVisits == 4 && tide_get_Bullet(&world, MISS).markedVisits == 4);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
}

TIDE_TEST(queries_read_previous_writes_and_replaced_pages)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_copy(&snapshot, &world);
    const uint64_t before = tide_world_hash(&snapshot);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Counter(&world, COUNT_A).value == 11 && tide_get_Counter(&world, COUNT_A).seen == 60);
    TIDE_CHECK(tide_get_Counter(&world, COUNT_B).value == 21 && tide_get_Counter(&world, COUNT_B).seen == 61);
    TIDE_CHECK(tide_get_Counter(&world, COUNT_C).value == 31 && tide_get_Counter(&world, COUNT_C).seen == 62);
    const Probe first = tide_get_Probe(&world, PROBE);
    TIDE_CHECK(first.before == 24 && first.after == 27);
    TIDE_CHECK(tide_get_Sample(&world, SAMPLE_B).value == 21);
    TIDE_CHECK(tide_world_hash(&snapshot) == before && tide_get_Sample(&snapshot, SAMPLE_B).value == 2);
    tide_world_tick(&world);
    const Probe second = tide_get_Probe(&world, PROBE);
    TIDE_CHECK(second.before == 26 && second.after == 29);
    TIDE_CHECK(tide_get_Counter(&world, COUNT_A).seen == 63 && tide_get_Counter(&world, COUNT_C).seen == 65);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}

// Hash the simulation's semantic values and handles, independently of page
// sizes and capacity, and pin the exact native/WebAssembly result.
static uint64_t hash_word(uint64_t hash, const uint32_t value)
{
    for (int i = 0; i < 4; i++) {
        hash ^= value >> (8 * i) & 255u;
        hash *= UINT64_C(0x100000001B3);
    }
    return hash;
}

static uint64_t semantic_hash(const tide_world *w)
{
    uint64_t hash = UINT64_C(0xCBF29CE484222325);
    hash = hash_word(hash, (uint32_t)w->Stats.healthBeforeEvents);
    hash = hash_word(hash, (uint32_t)w->Stats.handles);
    hash = hash_word(hash, (uint32_t)w->Stats.workVisits);
    hash = hash_word(hash, (uint32_t)w->Stats.workPrefix);
    hash = hash_word(hash, (uint32_t)w->Stats.impossibleVisits);
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        hash = hash_word(hash_word(hash, e.index), e.generation);
        if (tide_has_Body(w, e)) {
            const Body body = tide_get_Body(w, e);
            hash = hash_word(hash_word(hash, tide_f_bits(body.position.x)), tide_f_bits(body.position.y));
        }
        if (tide_has_Health(w, e)) hash = hash_word(hash, (uint32_t)tide_get_Health(w, e).value);
        if (tide_has_Bullet(w, e)) {
            const Bullet b = tide_get_Bullet(w, e);
            hash = hash_word(hash_word(hash, b.hit.index), b.hit.generation);
            hash = hash_word(hash, tide_f_bits(b.fraction));
            hash = hash_word(hash_word(hash, (uint32_t)b.candidates), (uint32_t)b.pairs);
            hash = hash_word(hash_word(hash, (uint32_t)b.continued), (uint32_t)b.stopped);
            hash = hash_word(hash, (uint32_t)b.markedVisits);
        }
        if (tide_has_Counter(w, e)) {
            const Counter c = tide_get_Counter(w, e);
            hash = hash_word(hash_word(hash, (uint32_t)c.value), (uint32_t)c.seen);
        }
        if (tide_has_Sample(w, e)) hash = hash_word(hash, (uint32_t)tide_get_Sample(w, e).value);
        if (tide_has_Probe(w, e)) {
            const Probe p = tide_get_Probe(w, e);
            hash = hash_word(hash_word(hash, (uint32_t)p.before), (uint32_t)p.after);
        }
        if (tide_has_Work(w, e)) {
            const Work work = tide_get_Work(w, e);
            hash = hash_word(hash_word(hash_word(hash, (uint32_t)work.value), (uint32_t)work.total), (uint32_t)work.visits);
        }
    }
    return hash;
}

TIDE_TEST(queries_match_across_platforms)
{
    tide_world_init(&world, 1.0f / 60.0f);
    for (int i = 0; i < 16; i++) tide_world_tick(&world);
    const uint64_t hash = semantic_hash(&world);
    const uint64_t expected = UINT64_C(0x23FB1DD4AEBCB224);
    if (hash != expected) printf("    queries: hash is 0x%016" PRIX64 ", expected 0x%016" PRIX64 "\n", hash, expected);
    TIDE_CHECK(hash == expected);
    tide_world_free(&world);
}

#ifdef TIDE_QUERY_THREADS
typedef struct limited_jobs {
    const tide_jobs *pool;
    uint32_t threads;
    uint32_t runs;
    void (*work)(void *context, uint32_t thread);
    void *context;
} limited_jobs;

static void limited_work(void *context, const uint32_t thread)
{
    limited_jobs *limited = context;
    if (thread < limited->threads) limited->work(limited->context, thread);
}

static void limited_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    limited_jobs *limited = self;
    limited->runs++;
    limited->work = work;
    limited->context = context;
    limited->pool->run(limited->pool->self, limited_work, limited);
}

TIDE_TEST(queries_threads_and_snapshots_change_nothing)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool) {
        printf("    one core: query thread comparison needs a pool\n");
        return;
    }
    for (uint32_t threads = 2; threads <= 4 && threads <= pool->threads; threads += 2) {
        tide_world many = {0};
        limited_jobs limited = {.pool = pool, .threads = threads};
        const tide_jobs jobs = {.self = &limited, .threads = threads, .run = limited_run};
        tide_world_init(&world, 1.0f / 60.0f);
        tide_world_init(&many, 1.0f / 60.0f);
        for (int t = 0; t < 16; t++) {
            tide_world_copy(&snapshot, &many);
            const uint64_t before = tide_world_hash(&snapshot);
            tide_world_tick(&world);
            tide_world_tick_on(&many, &jobs);
            if (tide_world_hash(&world) != tide_world_hash(&many)) printf("    %u threads differ at tick %d\n", threads, t);
            TIDE_REQUIRE(tide_world_hash(&world) == tide_world_hash(&many));
            TIDE_REQUIRE(tide_world_hash(&snapshot) == before);
            TIDE_REQUIRE(many.Stats.workVisits == 8192 && many.Stats.workPrefix == 4096 && many.Stats.impossibleVisits == 0);
        }
        TIDE_CHECK(limited.runs > 0); // Actually entered the threaded schedule.
        TIDE_CHECK(semantic_hash(&world) == semantic_hash(&many));
        tide_world_free(&snapshot);
        tide_world_free(&many);
        tide_world_free(&world);
    }
}
#endif
