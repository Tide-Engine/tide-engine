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

// Adds up `count` shares of work, each a task, and the same sum however many
// threads took them. -1 if a task ran more or less than once.
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
    return sum;
}
