#include "tide/worker.h"
#include "tide_test.h"

typedef struct worker_probe {
    uint32_t ran;
    uint32_t released;
} worker_probe;

typedef struct deferred_workers {
    tide_background *first, *last;
} deferred_workers;

static void defer(void *self, tide_background *job)
{
    deferred_workers *queue = self;
    job->next = NULL;
    if (queue->last) queue->last->next = job;
    else queue->first = job;
    queue->last = job;
}

static void run(void *context) { ((worker_probe *)context)->ran++; }
static void release(void *context) { ((worker_probe *)context)->released++; }

static bool help(void *self, const bool helpers_only)
{
    deferred_workers *queue = self;
    if (!queue->first || helpers_only) return false;
    tide_background *job = queue->first;
    queue->first = job->next;
    if (!queue->first) queue->last = NULL;
    job->work(job->context);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    return true;
}

TIDE_TEST(worker_abandoned_queued_calculations_are_skipped)
{
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    worker_probe cancelled = {0}, current = {0};
    const uint32_t old = tide_worker_start(&workers, &jobs, run, release, &cancelled);
    const uint32_t active = tide_worker_start(&workers, &jobs, run, release, &current);
    tide_worker_forget(workers, old);
    TIDE_CHECK(cancelled.released == 0);
    TIDE_CHECK(tide_worker_poll(workers, old) == NULL);
    while (help(&queue, false)) { }
    TIDE_CHECK(cancelled.ran == 0);
    TIDE_CHECK(tide_worker_poll(workers, active) == &current);
    TIDE_CHECK(tide_worker_poll(workers, active) == &current);
    TIDE_CHECK(current.ran == 1);
    tide_workers_collect(workers);
    TIDE_CHECK(cancelled.released == 1);
    TIDE_CHECK(current.released == 0);
    tide_worker_forget(workers, active);
    tide_worker_forget(workers, old);
    TIDE_CHECK(current.released == 1 && cancelled.released == 1);
    tide_workers_free(workers);
}

TIDE_TEST(worker_release_waits_for_provider_completion)
{
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    worker_probe probe = {0};
    const uint32_t ticket = tide_worker_start(&workers, &jobs, run, release, &probe);
    tide_background *job = queue.first;
    queue.first = queue.last = NULL;
    job->work(job->context);
    TIDE_CHECK(probe.ran == 1);
    // The callback has returned, but the provider still owns the queue item.
    tide_worker_forget(workers, ticket);
    tide_workers_collect(workers);
    TIDE_CHECK(probe.released == 0);
    __atomic_store_n(&job->done, 1u, __ATOMIC_RELEASE);
    tide_workers_collect(workers);
    TIDE_CHECK(probe.released == 1);
    tide_workers_free(workers);
}

TIDE_TEST(worker_disposal_abandons_every_queued_calculation_before_helping)
{
    deferred_workers queue = {0};
    const tide_jobs jobs = {.self = &queue, .threads = 2, .background = defer, .help = help};
    tide_workers *workers = NULL;
    worker_probe probes[4] = {{0}};
    for (int i = 0; i < 4; i++) tide_worker_start(&workers, &jobs, run, release, &probes[i]);
    tide_workers_free(workers);
    TIDE_CHECK(queue.first == NULL);
    for (int i = 0; i < 4; i++) TIDE_CHECK(probes[i].ran == 0 && probes[i].released == 1);
    workers = NULL;
    for (int i = 0; i < 4; i++) tide_worker_start(&workers, NULL, run, release, &probes[i]);
    tide_workers_free(workers);
    for (int i = 0; i < 4; i++) TIDE_CHECK(probes[i].ran == 0 && probes[i].released == 2);
}
