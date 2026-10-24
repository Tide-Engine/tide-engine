#include "tide/worker.h"

#include <stdlib.h>

#include "tide/page.h"
#include "tide/text.h"
#include "tide/time.h"

typedef struct worker_item {
    tide_background job;
    struct worker_item *next;
    void (*work)(void *);
    void (*release)(void *);
    void *context;
    uint32_t ticket;
    uint32_t forgotten;
    bool submitted;
    const tide_jobs *jobs;
} worker_item;

struct tide_workers {
    worker_item *items;
    uint32_t next;
};

static TIDE_THREAD_LOCAL const tide_jobs *worker_jobs;

const tide_jobs *tide_worker_enter(const tide_jobs *jobs)
{
    const tide_jobs *previous = worker_jobs;
    worker_jobs = jobs;
    return previous;
}
void tide_worker_leave(const tide_jobs *previous) { worker_jobs = previous; }

static void worker_run(void *context)
{
    const worker_item *item = context;
    // Abandoning queued work should not delay current work or reload. Once a
    // calculation has started it retains its private captures until completion.
    if (!__atomic_load_n(&item->forgotten, __ATOMIC_ACQUIRE)) item->work(item->context);
}

uint32_t tide_worker_start(tide_workers **workers, const tide_jobs *jobs,
                           void (*work)(void *), void (*release)(void *), void *context)
{
    if (!*workers) *workers = tide_alloc_zeroed(1, sizeof **workers);
    tide_workers *w = *workers;
    worker_item *item = tide_alloc_zeroed(1, sizeof *item);
    // Never reuse a live ticket, including at the unsigned wrap boundary.
    bool used;
    do {
        if (++w->next == 0) ++w->next;
        used = false;
        for (worker_item *p = w->items; p && !used; p = p->next) used = p->ticket == w->next;
    } while (used);
    item->ticket = w->next;
    item->work = work;
    item->release = release;
    item->context = context;
    item->jobs = jobs;
    item->job.work = worker_run;
    item->job.context = item;
    item->next = w->items;
    w->items = item;
    if (jobs && jobs->background) {
        item->submitted = true;
        jobs->background(jobs->self, &item->job);
    }
    return item->ticket;
}

static worker_item *find(tide_workers *w, const uint32_t ticket)
{
    for (worker_item *p = w ? w->items : NULL; p; p = p->next) if (p->ticket == ticket) return p;
    return NULL;
}

void *tide_worker_poll(tide_workers *w, const uint32_t ticket)
{
    worker_item *item = find(w, ticket);
    if (!item || item->forgotten) return NULL;
    if (!item->submitted) {
        item->submitted = true;
        item->job.work(item->job.context);
        __atomic_store_n(&item->job.done, 1u, __ATOMIC_RELEASE);
    }
    if (!__atomic_load_n(&item->job.done, __ATOMIC_ACQUIRE)) return NULL;
    tide_memory_sync();
    return item->context;
}

void tide_workers_collect(tide_workers *w)
{
    if (!w) return;
    worker_item **at = &w->items;
    while (*at) {
        worker_item *p = *at;
        if (p->forgotten && (!p->submitted || __atomic_load_n(&p->job.done, __ATOMIC_ACQUIRE))) {
            tide_memory_sync();
            *at = p->next;
            p->release(p->context);
            free(p);
        } else at = &p->next;
    }
}

void tide_worker_forget(tide_workers *w, const uint32_t ticket)
{
    if (!ticket) return;
    worker_item *p = find(w, ticket);
    if (p) __atomic_store_n(&p->forgotten, 1u, __ATOMIC_RELEASE);
    tide_workers_collect(w);
}

void tide_workers_free(tide_workers *w)
{
    if (!w) return;
    for (worker_item *p = w->items; p; p = p->next)
        __atomic_store_n(&p->forgotten, 1u, __ATOMIC_RELEASE);
    for (worker_item *p = w->items; p; p = p->next) {
        while (p->submitted && !__atomic_load_n(&p->job.done, __ATOMIC_ACQUIRE)) {
            if (p->jobs && p->jobs->help) p->jobs->help(p->jobs->self, false);
            tide_yield();
        }
    }
    tide_workers_collect(w);
    free(w);
}

typedef struct parallel_work {
    void (*work)(void *, uint32_t);
    void *context;
    uint32_t count;
    uint64_t next;
    tide_heap *match;
    tide_heap *local;
} parallel_work;

static void parallel_take(void *context)
{
    tide_memory_sync();
    parallel_work *p = context;
    const uint32_t mark = tide_scratch_mark();
    tide_heap *match = tide_world_heaps[TIDE_IN_MATCH], *local = tide_world_heaps[TIDE_IN_LOCAL];
    tide_text_use(p->match, p->local);
    for (;;) {
        const uint64_t i = __atomic_fetch_add(&p->next, 1u, __ATOMIC_RELAXED);
        if (i >= p->count) break;
        p->work(p->context, (uint32_t)i);
    }
    tide_text_use(match, local);
    if (!mark) tide_scratch_free();
    else tide_scratch_reset(mark);
}

bool tide_worker_parallel(const uint32_t count, void (*work)(void *, uint32_t), void *context, const uint64_t cost)
{
    const tide_jobs *jobs = worker_jobs;
    if (!jobs || !jobs->background || !jobs->help || jobs->threads < 2 || count < 2 || cost < 100000u) return false;
    parallel_work p = {work, context, count, 0, tide_world_heaps[TIDE_IN_MATCH], tide_world_heaps[TIDE_IN_LOCAL]};
    tide_background tasks[31] = {{0}};
    uint32_t others = jobs->threads - 1u;
    if (others > 31u) others = 31u;
    if (others >= count) others = count - 1u;
    for (uint32_t i = 0; i < others; i++) {
        tasks[i].work = parallel_take;
        tasks[i].context = &p;
        tasks[i].helper = true;
        jobs->background(jobs->self, &tasks[i]);
    }
    parallel_take(&p);
    // The caller does all available iterations itself. A provider must be able
    // to run queued helpers while another background callback waits for them.
    for (uint32_t i = 0; i < others; i++) {
        while (!__atomic_load_n(&tasks[i].done, __ATOMIC_ACQUIRE)) {
            if (!jobs->help(jobs->self, true)) tide_yield();
        }
    }
    tide_memory_sync();
    return true;
}
