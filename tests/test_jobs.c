// A tick's systems as tasks (tide/jobs.h): what a system whose chunks go in
// pieces is called with, and when. The pool here is one thread that says it
// has eight, so nothing rests on how threads fall.

#include <stdio.h>
#include <string.h>

#include "tide/jobs.h"
#include "tide_test.h"

static void alone_run(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    work(context, 0);
}

static uint32_t asked;

static void count_more(void *self, const uint32_t count)
{
    (void)self;
    asked += count;
}

static char happened[128];

static void note(const char what)
{
    const size_t n = strlen(happened);
    if (n + 1 < sizeof happened) happened[n] = what;
}

static uint32_t pieces_seen[3];
static uint32_t tasks_run[3];

static uint32_t heavy_count(const void *world, uint64_t *work)
{
    (void)world;
    *work = UINT64_C(1) << 32;
    return 2;
}

static uint32_t light_count(const void *world, uint64_t *work)
{
    (void)world;
    *work = 1;
    return 1;
}

#define SYSTEM(n, letter)                                                        \
    static void run_##n(void *world, const uint32_t task, const uint32_t pieces) \
    {                                                                            \
        (void)world;                                                             \
        (void)task;                                                              \
        pieces_seen[n] = pieces;                                                 \
        tasks_run[n]++;                                                          \
        note(letter);                                                            \
    }
SYSTEM(0, 'a')
SYSTEM(1, 'b')
SYSTEM(2, 'c')

static void own_1(void *world)
{
    (void)world;
    note('[');
}

static void done_1(void *world)
{
    (void)world;
    note(']');
}

static void own_2(void *world)
{
    (void)world;
    note('(');
}

TIDE_TEST(jobs_pieces_are_begun_and_ended_around_their_system)
{
    static const uint32_t after_first[1] = {0}, after_second[1] = {1};
    const tide_system_tasks systems[3] = {
        {.count = heavy_count, .run = run_0, .splits = true, .name = "first"},
        {.count = heavy_count, .run = run_1, .own = own_1, .done = done_1, .splits = true, .wait_count = 1, .waits = after_first,
         .name = "second"},
        {.count = light_count, .run = run_2, .own = own_2, .splits = true, .wait_count = 1, .waits = after_second, .name = "third"},
    };
    tide_queue queue = {0};
    tide_entities entities = {0};
    const tide_tick_systems tick = {systems, 3, NULL, &queue, &entities};
    const tide_jobs eight = {.threads = 8, .run = alone_run, .more = count_more};
    memset(happened, 0, sizeof happened);
    memset(pieces_seen, 0, sizeof pieces_seen);
    memset(tasks_run, 0, sizeof tasks_run);
    asked = 0;
    tide_run_systems(NULL, &tick, &eight);
    TIDE_CHECK(pieces_seen[0] == 8 && tasks_run[0] == 16);
    TIDE_CHECK(pieces_seen[1] == 8 && tasks_run[1] == 16);
    TIDE_CHECK(pieces_seen[2] == 1 && tasks_run[2] == 1);
    TIDE_CHECK(strcmp(happened, "aaaaaaaaaaaaaaaa[bbbbbbbbbbbbbbbb]c") == 0);
    if (strcmp(happened, "aaaaaaaaaaaaaaaa[bbbbbbbbbbbbbbbb]c") != 0) printf("    it went %s\n", happened);
    TIDE_CHECK(asked >= 15);
    tide_queue_free(&queue);
    tide_entities_free(&entities);
}

static uint32_t many_count(const void *world, uint64_t *work)
{
    (void)world;
    *work = 64u * 1000u;
    return 64;
}

static uint32_t many_seen[64];

static void run_many(void *world, const uint32_t task, const uint32_t pieces)
{
    (void)world;
    pieces_seen[2] = pieces;
    tasks_run[2]++;
    if (task < 64u) many_seen[task]++;
}

TIDE_TEST(jobs_cheap_tasks_are_taken_a_few_at_a_time)
{
    static const uint32_t after_first[1] = {0};
    const tide_system_tasks systems[2] = {
        {.count = heavy_count, .run = run_0, .splits = true, .name = "first"},
        {.count = many_count, .run = run_many, .splits = true, .wait_count = 1, .waits = after_first, .name = "many"},
    };
    tide_queue queue = {0};
    tide_entities entities = {0};
    const tide_tick_systems tick = {systems, 2, NULL, &queue, &entities};
    const tide_jobs eight = {.threads = 8, .run = alone_run, .more = count_more};
    memset(pieces_seen, 0, sizeof pieces_seen);
    memset(tasks_run, 0, sizeof tasks_run);
    memset(many_seen, 0, sizeof many_seen);
    asked = 0;
    tide_run_systems(NULL, &tick, &eight);
    TIDE_CHECK(tasks_run[0] == 16 && tasks_run[2] == 64 && pieces_seen[2] == 1);
    bool once = true;
    for (uint32_t i = 0; i < 64u; i++) once &= many_seen[i] == 1u;
    TIDE_CHECK(once);
    TIDE_CHECK(asked == 7);
    if (asked != 7) printf("    asked for %u threads\n", asked);
    tide_queue_free(&queue);
    tide_entities_free(&entities);
}

// A tick on one thread, whose system's parallel loops are each worth threads
// or not, as they find themselves: the pool comes in for the costly one alone,
// and a loop inside its steps runs where it is.
static uint32_t rounds;
static uint32_t prepared;
static uint32_t steps_seen[3][40];

static void counted_round(void *self, void (*work)(void *context, uint32_t thread), void *context)
{
    (void)self;
    rounds++;
    work(context, 1); // Another thread first: it takes every step
    work(context, 0);
}

static void prepare_thread(void *world)
{
    (void)world;
    prepared++;
}

static void inner_step(void *context, const uint32_t task)
{
    (void)context;
    if (task < 40u) steps_seen[2][task]++;
}

static void costly_step(void *context, const uint32_t task)
{
    (void)context;
    if (task < 40u) steps_seen[0][task]++;
    if (task == 0) tide_parallel_for(40, inner_step, NULL, UINT64_C(1) << 40);
}

static void cheap_step(void *context, const uint32_t task)
{
    (void)context;
    if (task < 40u) steps_seen[1][task]++;
}

static void run_loops(void *world, const uint32_t task, const uint32_t pieces)
{
    (void)world;
    (void)task;
    (void)pieces;
    tide_parallel_for(40, costly_step, NULL, UINT64_C(1) << 40);
    tide_parallel_for(40, cheap_step, NULL, 40);
}

TIDE_TEST(jobs_costly_loops_in_a_tick_on_one_thread_call_threads_in)
{
    const tide_system_tasks systems[1] = {{.count = light_count, .run = run_loops, .name = "loops"}};
    tide_queue queue = {0};
    tide_entities entities = {0};
    const tide_tick_systems tick = {systems, 1, prepare_thread, &queue, &entities};
    const tide_jobs eight = {.threads = 8, .run = counted_round};
    rounds = 0;
    prepared = 0;
    memset(steps_seen, 0, sizeof steps_seen);
    tide_run_systems(NULL, &tick, &eight);
    TIDE_CHECK(rounds == 1u);
    TIDE_CHECK(prepared == 2u); // The tick's thread, and the other one before its first step
    bool once = true;
    for (uint32_t k = 0; k < 3u; k++) {
        for (uint32_t i = 0; i < 40u; i++) once &= steps_seen[k][i] == 1u;
    }
    TIDE_CHECK(once);
    // With no pool, or one thread, the same loops run in order
    rounds = 0;
    tide_run_systems(NULL, &tick, NULL);
    TIDE_CHECK(rounds == 0u);
    tide_queue_free(&queue);
    tide_entities_free(&entities);
}

TIDE_TEST(jobs_on_one_thread_make_no_pieces)
{
    static const uint32_t after_first[1] = {0};
    const tide_system_tasks systems[2] = {
        {.count = heavy_count, .run = run_0, .splits = true, .name = "first"},
        {.count = heavy_count, .run = run_1, .own = own_1, .done = done_1, .splits = true, .wait_count = 1, .waits = after_first,
         .name = "second"},
    };
    tide_queue queue = {0};
    tide_entities entities = {0};
    const tide_tick_systems tick = {systems, 2, NULL, &queue, &entities};
    memset(happened, 0, sizeof happened);
    memset(pieces_seen, 0, sizeof pieces_seen);
    memset(tasks_run, 0, sizeof tasks_run);
    tide_run_systems(NULL, &tick, NULL);
    TIDE_CHECK(pieces_seen[0] == 1 && pieces_seen[1] == 1 && tasks_run[0] == 2 && tasks_run[1] == 2);
    TIDE_CHECK(strcmp(happened, "aabb") == 0);
    tide_queue_free(&queue);
    tide_entities_free(&entities);
}

// Final keys are available inside each split task before physical registration.
typedef struct keyed_world {
    tide_entities entities;
    tide_entity observed[16];
    bool premature;
} keyed_world;
static void run_keyed(void *world, const uint32_t task, const uint32_t pieces)
{
    keyed_world *w = world;
    (void)pieces;
    const tide_entity key = {UINT64_C(0x123456789abcdef0), UINT64_C(0xfedcba9876543210) + task};
    w->observed[task] = tide_new_entity_key(&w->entities, key);
    if (tide_entity_alive(&w->entities, key)) w->premature = true;
}
TIDE_TEST(jobs_split_spawns_return_final_full_keys)
{
    const tide_system_tasks systems[] = {
        {.count = heavy_count, .run = run_keyed, .splits = true, .spawns = true, .settles = true, .name = "keys"},
    };
    const tide_jobs eight = {.threads = 8, .run = alone_run};
    keyed_world w = {0};
    tide_queue queue = {0};
    const tide_tick_systems tick = {systems, 1, NULL, &queue, &w.entities};
    tide_run_systems(&w, &tick, &eight);
    TIDE_CHECK(!w.premature && w.entities.next_unused == 16u);
    for (uint32_t i = 0; i < 16u; i++) {
        const tide_entity key = {UINT64_C(0x123456789abcdef0), UINT64_C(0xfedcba9876543210) + i};
        TIDE_CHECK(tide_entity_equal(w.observed[i], key));
        TIDE_CHECK(tide_entity_alive(&w.entities, key));
        TIDE_CHECK(tide_entity_slot_index(&w.entities, key) == i);
    }
    tide_entities_free(&w.entities);
    tide_queue_free(&queue);
}
