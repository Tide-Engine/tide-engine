// The C side of jobs.tide's ShareOut: work C shares out itself, through the
// engine's tasks (tide/jobs.h), as a library with a task interface of its own
// does.

#include <stdatomic.h>
#include <stdint.h>

#include "tide/jobs.h"

#define SHARES 24

typedef struct share {
    int32_t from;
    int32_t sum;
    atomic_int runs;
} share;

// How many tasks another thread than the one that started them ran, in all:
// what a host reads to see the pool took part.
atomic_int jobs_tasks_elsewhere;

static _Thread_local int here; // Its address tells threads apart
static _Atomic(const int *) starter;

static void add_up(void *context)
{
    share *s = context;
    int32_t sum = 0;
    for (int32_t i = 0; i < 2000; i++) sum += (s->from + i) % 7;
    s->sum = sum;
    atomic_fetch_add(&s->runs, 1);
    if (atomic_load(&starter) != &here) atomic_fetch_add(&jobs_tasks_elsewhere, 1);
}

// ---------------------------------------------------------------------------
// Work in stages, as a physics solver shares its own out: its workers are
// tasks that wait, inside their work, for each stage the one that leads gives
// them, and the leader waits for a stage's blocks to be done before the next.
// A worker that has waited a while sleeps where its thread can
// (tide_job_sleep), and the leader wakes it with the next stage.

#define STAGES 4
#define BLOCKS 12
#define WORKERS 6
#define LAST UINT32_MAX // After the last stage: the workers go

typedef struct staged {
    int32_t from;
    uint32_t stage;    // What workers wait on: the stage that's running, from 1
    atomic_int leader; // Taken
    atomic_int taken[STAGES][BLOCKS];
    int32_t sums[STAGES][BLOCKS];
    atomic_int done;                // Blocks of the running stage
    atomic_int workers;             // That are in it
    atomic_uint waiting[WORKERS];   // The stage each waits for, once it has waited a while
} staged;

typedef struct staged_worker {
    staged *of;
    uint32_t index;
} staged_worker;

// How many times a worker's wait slept: what a host reads to see whether this
// pool's threads sleep.
atomic_int jobs_tasks_slept;

static void run_blocks(staged *s, const uint32_t stage)
{
    for (int32_t b = 0; b < BLOCKS; b++) {
        int none = 0;
        if (!atomic_compare_exchange_strong(&s->taken[stage - 1u][b], &none, 1)) continue;
        int32_t sum = 0;
        for (int32_t i = 0; i < 500; i++) sum += (s->from + (int32_t)stage * 31 + b * 7 + i) % 11;
        s->sums[stage - 1u][b] = sum;
        atomic_fetch_add(&s->done, 1);
    }
}

static void lead(staged *s)
{
    for (uint32_t stage = 1; stage <= STAGES; stage++) {
        // Not before every worker that's in waits for it, and has for a while:
        // so workers do wait, and those whose threads can sleep are asleep
        for (;;) {
            int waiting = 0;
            for (int w = 0; w < WORKERS; w++) waiting += atomic_load(&s->waiting[w]) == stage;
            if (waiting >= atomic_load(&s->workers)) break;
        }
        __atomic_store_n(&s->stage, stage, __ATOMIC_SEQ_CST);
        tide_job_wake(&s->stage);
        run_blocks(s, stage);
        while (atomic_load(&s->done) != BLOCKS) {
        }
        atomic_store(&s->done, 0);
    }
    __atomic_store_n(&s->stage, LAST, __ATOMIC_SEQ_CST);
    tide_job_wake(&s->stage);
}

static void stage_work(void *context)
{
    const staged_worker *me = context;
    staged *s = me->of;
    // The first to run leads: with no thread to take the others, it's the only
    // one that runs before they're waited for, and has to do it all
    int none = 0;
    if (atomic_compare_exchange_strong(&s->leader, &none, 1)) {
        lead(s);
        return;
    }
    atomic_fetch_add(&s->workers, 1);
    uint32_t seen = 0;
    for (;;) {
        uint32_t stage;
        uint32_t looks = 0;
        while ((stage = __atomic_load_n(&s->stage, __ATOMIC_SEQ_CST)) == seen) {
            if (++looks < 64u) continue;
            looks = 0;
            atomic_store(&s->waiting[me->index], seen + 1u);
            if (tide_job_sleep(&s->stage, seen)) atomic_fetch_add(&jobs_tasks_slept, 1);
        }
        if (stage == LAST) return;
        run_blocks(s, stage);
        seen = stage;
    }
}

// The sum of work in stages, the same however many threads took part. -1 if
// a block ran more or less than once.
static int32_t stage_out(const int32_t from)
{
    staged s = {.from = from};
    staged_worker workers[WORKERS];
    tide_job tasks[WORKERS];
    for (uint32_t w = 0; w < WORKERS; w++) {
        workers[w] = (staged_worker){&s, w};
        tasks[w] = tide_job_start(stage_work, &workers[w]);
    }
    for (uint32_t w = 0; w < WORKERS; w++) tide_job_wait(tasks[w]);
    int32_t sum = 0;
    for (int32_t stage = 0; stage < STAGES; stage++) {
        for (int32_t b = 0; b < BLOCKS; b++) {
            if (atomic_load(&s.taken[stage][b]) != 1) return -1;
            sum += s.sums[stage][b];
        }
    }
    return sum;
}

// Adds up `count` shares of work, each a task, and work in stages, and the
// same sum however many threads took them. -1 if a task or a block ran more
// or less than once.
int32_t ShareWork(const int32_t from, const int32_t count)
{
    share shares[SHARES];
    tide_job tasks[SHARES];
    const int32_t n = count < SHARES ? count : SHARES;
    atomic_store(&starter, &here);
    for (int32_t i = 0; i < n; i++) {
        shares[i].from = from + i;
        shares[i].sum = 0;
        atomic_init(&shares[i].runs, 0);
        tasks[i] = tide_job_start(add_up, &shares[i]);
    }
    int32_t sum = 0;
    for (int32_t i = 0; i < n; i++) {
        tide_job_wait(tasks[i]);
        if (atomic_load(&shares[i].runs) != 1) return -1;
        sum += shares[i].sum;
    }
    const int32_t staged_sum = stage_out(from);
    return staged_sum < 0 ? -1 : sum + staged_sum % 1000;
}
