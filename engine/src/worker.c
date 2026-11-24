#include "tide/worker.h"

#include <stdlib.h>

#include "tide/page.h"
#include "tide/text.h"
#include "tide/time.h"

typedef struct worker_item {
    tide_background job;
    struct worker_item *next, *previous, *hash_next, *retired_next;
    void (*work)(void *);
    void (*release)(void *);
    void *context;
    uint32_t ticket;
    uint32_t forgotten;
    bool submitted;         // Given to a provider, or run where it was polled
    bool queued;            // Given to a provider: the group hands it over in its turn
    bool waiting;           // In its level's queue, not handed over yet: under the group's lock
    uint8_t level;          // 0 urgent, 1 normal, 2 background
    struct worker_item *wait_next, *wait_previous;
    bool measured, recorded;
    uint64_t queued_ns, began_ns, ended_ns;
    uint64_t capture_bytes;
    struct tide_workers *group;
    const tide_jobs *jobs;
} worker_item;

// Work a provider hasn't been handed yet, a queue for each priority, oldest first. `passed` counts the starts of
// more urgent work since this level's oldest began to wait, or since its last start.
enum { LEVELS = 3 };
typedef struct worker_queue {
    worker_item *first, *last;
    uint32_t passed;
} worker_queue;

// After this many starts of more urgent work, a level's oldest starts next: nothing waits for ever. Under a
// steady stream of urgent work, every fifth start is a normal calculation and every seventeenth a background one.
static const uint32_t level_turn[LEVELS] = {0u, 4u, 16u};

struct tide_workers {
    // The lock is for what follows it down to the queues, which the provider's threads share: one hands over
    // the group's next calculation as its own ends. Everything else is the frame thread's.
    uint32_t lock;
    uint32_t flying, most; // Calculations the provider has and hasn't finished, and how many it's given at once
    bool closing;
    worker_queue waiting[LEVELS];
    worker_item *items;
    worker_item *retired;
    worker_item **buckets;
    uint32_t next, count, room;
    tide_worker_limits limits;
    uint64_t capture_bytes, transferred;
    // Admission is each level's own: what's pending of it, and the captures kept for it
    uint32_t level_count[LEVELS];
    uint64_t level_bytes[LEVELS];
    tide_profile *profile;
};

static TIDE_THREAD_LOCAL const tide_jobs *worker_jobs;
static TIDE_THREAD_LOCAL const worker_item *current_worker;

bool tide_worker_cancelled(void)
{
    return current_worker && __atomic_load_n(&current_worker->forgotten, __ATOMIC_ACQUIRE);
}

const tide_jobs *tide_worker_enter(const tide_jobs *jobs)
{
    const tide_jobs *previous = worker_jobs;
    worker_jobs = jobs;
    return previous;
}
void tide_worker_leave(const tide_jobs *previous) { worker_jobs = previous; }

static void acquire(tide_workers *w)
{
    while (__atomic_exchange_n(&w->lock, 1u, __ATOMIC_ACQUIRE)) {
    }
}
static void let_go(tide_workers *w) { __atomic_store_n(&w->lock, 0u, __ATOMIC_RELEASE); }

static uint8_t level_of(const uint32_t priority)
{
    return priority == TIDE_WORKER_URGENT ? 0u : priority == TIDE_WORKER_BACKGROUND ? 2u : 1u;
}

// Under the lock.
static void unlink_waiting(tide_workers *w, worker_item *p)
{
    worker_queue *q = &w->waiting[p->level];
    if (p->wait_previous) p->wait_previous->wait_next = p->wait_next;
    else q->first = p->wait_next;
    if (p->wait_next) p->wait_next->wait_previous = p->wait_previous;
    else q->last = p->wait_previous;
    p->wait_next = p->wait_previous = NULL;
    p->waiting = false;
    if (!q->first) q->passed = 0; // Nobody of this level has waited through anything
}

// Under the lock: the calculation the provider gets next, counted as its own, or NULL while it has as many as
// it runs at once. The most urgent level's oldest, but a level whose oldest has waited through its turn of more
// urgent starts goes first, the least urgent such level before the others. Priority is read here alone: work
// the provider has is never interrupted or passed.
static worker_item *hand_over(tide_workers *w)
{
    if (w->closing || w->flying >= w->most) return NULL;
    int pick = -1;
    for (int l = LEVELS - 1; l > 0 && pick < 0; l--)
        if (w->waiting[l].first && w->waiting[l].passed >= level_turn[l]) pick = l;
    for (int l = 0; l < LEVELS && pick < 0; l++)
        if (w->waiting[l].first) pick = l;
    if (pick < 0) return NULL;
    for (int l = pick + 1; l < LEVELS; l++)
        if (w->waiting[l].first) w->waiting[l].passed++;
    worker_item *p = w->waiting[pick].first;
    w->waiting[pick].passed = 0;
    unlink_waiting(w, p);
    w->flying++;
    return p;
}

static void worker_run(void *context)
{
    worker_item *item = context;
    // Abandoning queued work should not delay current work or reload. Once a
    // calculation has started it retains its private captures until completion.
    if (!__atomic_load_n(&item->forgotten, __ATOMIC_ACQUIRE)) {
        if (item->measured) item->began_ns = tide_time_now_ns();
        const worker_item *previous = current_worker;
        current_worker = item;
        item->work(item->context);
        current_worker = previous;
        if (item->measured) item->ended_ns = tide_time_now_ns();
    }
    if (!item->queued) return;
    // The provider's thread is free for the group's next. The item it gets stays alive until the provider says
    // it's done, and the group until then too (tide_workers_free waits for everything handed over).
    tide_workers *w = item->group;
    acquire(w);
    w->flying--;
    worker_item *next = hand_over(w);
    let_go(w);
    if (next) next->jobs->background(next->jobs->self, &next->job);
}

static void record(worker_item *p)
{
    if (p->recorded || !p->measured) return;
    p->recorded = true;
    // The host can toggle profiling while work is in flight. Never access the
    // profile from a background callback, or retain its storage across frames.
    tide_profile *profile = p->group->profile;
    if (!profile && p->jobs) profile = p->jobs->profile;
    if (!profile || !p->began_ns) return;
    tide_profile_count(&profile->workers, 4);
    tide_profile_add(&profile->workers, 0, "Queue wait", p->queued_ns, p->began_ns);
    tide_profile_add(&profile->workers, 1, "Compute", p->began_ns, p->ended_ns);
    profile->worker_runs++;
}

static uint32_t bucket(const uint32_t ticket, const uint32_t room)
{
    return (ticket * 2654435761u) & (room - 1u);
}

static worker_item *find(tide_workers *w, const uint32_t ticket)
{
    if (!w || !w->room || !ticket) return NULL;
    for (worker_item *p = w->buckets[bucket(ticket, w->room)]; p; p = p->hash_next)
        if (p->ticket == ticket) return p;
    return NULL;
}

static void grow(tide_workers *w)
{
    if (w->count < w->room) return;
    const uint32_t room = w->room ? w->room * 2u : 16u;
    if (room <= w->room || (uint64_t)room * sizeof *w->buckets > SIZE_MAX) tide_out_of_memory();
    worker_item **buckets = tide_alloc_zeroed(room, sizeof *buckets);
    for (worker_item *p = w->items; p; p = p->next) {
        const uint32_t at = bucket(p->ticket, room);
        p->hash_next = buckets[at];
        buckets[at] = p;
    }
    free(w->buckets);
    w->buckets = buckets;
    w->room = room;
}

static void dispose(tide_workers *w, worker_item *p)
{
    worker_item **at = &w->buckets[bucket(p->ticket, w->room)];
    while (*at != p) at = &(*at)->hash_next;
    *at = p->hash_next;
    if (p->previous) p->previous->next = p->next;
    else w->items = p->next;
    if (p->next) p->next->previous = p->previous;
    w->count--;
    w->capture_bytes -= p->capture_bytes;
    w->level_count[p->level]--;
    w->level_bytes[p->level] -= p->capture_bytes;
    tide_memory_sync();
    record(p);
    if (p->release) p->release(p->context);
    free(p);
}

static uint32_t reserve(tide_workers **workers, const tide_jobs *jobs, const uint32_t priority)
{
    if (!*workers) *workers = tide_alloc_zeroed(1, sizeof **workers);
    tide_workers *w = *workers;
    grow(w);
    worker_item *item = tide_alloc_zeroed(1, sizeof *item);
    // Never reuse a live ticket, including at the unsigned wrap boundary.
    do {
        if (++w->next == 0) ++w->next;
    } while (find(w, w->next));
    item->ticket = w->next;
    item->group = w;
    item->jobs = jobs;
    item->level = level_of(priority);
    item->job.work = worker_run;
    item->job.context = item;
    item->next = w->items;
    if (w->items) w->items->previous = item;
    w->items = item;
    const uint32_t at = bucket(item->ticket, w->room);
    item->hash_next = w->buckets[at];
    w->buckets[at] = item;
    w->count++;
    w->level_count[item->level]++;
    return item->ticket;
}

void tide_worker_submit(tide_workers *w, const uint32_t ticket, void (*work)(void *),
                        void (*release)(void *), void *context, const uint64_t capture_bytes)
{
    worker_item *item = find(w, ticket);
    if (!item || item->work || item->forgotten) {
        if (release) release(context);
        return;
    }
    item->work = work;
    item->release = release;
    item->context = context;
    item->capture_bytes = capture_bytes;
    w->capture_bytes += capture_bytes;
    w->level_bytes[item->level] += capture_bytes;
    const tide_jobs *jobs = item->jobs;
    item->measured = w->profile || (jobs && jobs->profile);
    if (item->measured) item->queued_ns = tide_time_now_ns();
    if (jobs && jobs->background) {
        item->submitted = item->queued = true;
        acquire(w);
        // A pool's threads but the caller's, which has its frame to make: nothing then waits in the provider's
        // own queue, where it would be in the order it came
        w->most = jobs->threads > 2u ? jobs->threads - 1u : 1u;
        worker_queue *q = &w->waiting[item->level];
        item->waiting = true;
        item->wait_previous = q->last;
        if (q->last) q->last->wait_next = item;
        else q->first = item;
        q->last = item;
        worker_item *next = hand_over(w);
        let_go(w);
        if (next) next->jobs->background(next->jobs->self, &next->job);
    }
}

uint32_t tide_worker_start(tide_workers **workers, const tide_jobs *jobs,
                           void (*work)(void *), void (*release)(void *), void *context)
{
    const uint32_t ticket = reserve(workers, jobs, TIDE_WORKER_NORMAL);
    tide_worker_submit(*workers, ticket, work, release, context, 0);
    return ticket;
}

void tide_workers_frame(tide_workers **workers, const tide_worker_limits *limits, tide_profile *profile)
{
    if (!*workers) *workers = tide_alloc_zeroed(1, sizeof **workers);
    tide_workers *w = *workers;
    w->limits = limits ? *limits : (tide_worker_limits){0};
    if (!w->limits.pending) w->limits.pending = 64u;
    if (!w->limits.capture_bytes) w->limits.capture_bytes = UINT64_C(64) * 1024u * 1024u;
    if (!w->limits.transfer_bytes) w->limits.transfer_bytes = UINT64_C(4) * 1024u * 1024u;
    w->transferred = 0;
    w->profile = profile;
    tide_workers_collect(w);
}

bool tide_worker_transfer_ready(const tide_workers *w)
{
    return !w || !w->limits.transfer_bytes || w->transferred < w->limits.transfer_bytes;
}

uint64_t tide_worker_transfer_begin(const tide_workers *w)
{
    return w && w->profile ? tide_time_now_ns() : 0;
}

void tide_worker_transferred(tide_workers *w, const uint64_t bytes, const uint64_t began_ns, const bool result)
{
    if (w) w->transferred = bytes > UINT64_MAX - w->transferred ? UINT64_MAX : w->transferred + bytes;
    if (w && w->profile && began_ns) {
        tide_profile_count(&w->profile->workers, 4);
        tide_profile_add(&w->profile->workers, result ? 3u : 2u, result ? "Result transfer" : "Input capture",
                         began_ns, tide_time_now_ns());
    }
}

// Each priority has room of its own, so none keeps another out: the limits are what normal work may have
// pending and keep captured, urgent work has as much again, and background work half as much.
static uint64_t limit_at(const uint64_t limit, const uint8_t level)
{
    return level == 2u && limit > 1u ? limit / 2u : limit;
}

bool tide_worker_can_reserve_at(const tide_workers *w, const uint32_t priority)
{
    const uint8_t level = level_of(priority);
    if (w && ((!tide_worker_transfer_ready(w))
              || (w->limits.pending && w->level_count[level] >= limit_at(w->limits.pending, level))
              || (w->limits.capture_bytes && w->level_bytes[level] >= limit_at(w->limits.capture_bytes, level)))) return false;
    return true;
}

bool tide_worker_can_reserve(const tide_workers *w) { return tide_worker_can_reserve_at(w, TIDE_WORKER_NORMAL); }

uint32_t tide_worker_reserve_at(tide_workers **workers, const tide_jobs *jobs, const uint32_t priority)
{
    if (!tide_worker_can_reserve_at(*workers, priority)) return 0;
    return reserve(workers, jobs, priority);
}

uint32_t tide_worker_reserve(tide_workers **workers, const tide_jobs *jobs)
{
    return tide_worker_reserve_at(workers, jobs, TIDE_WORKER_NORMAL);
}

void *tide_worker_poll(tide_workers *w, const uint32_t ticket)
{
    worker_item *item = find(w, ticket);
    if (!item || item->forgotten || !item->work) return NULL;
    if (!item->submitted) {
        item->submitted = true;
        item->job.work(item->job.context);
        __atomic_store_n(&item->job.done, 1u, __ATOMIC_RELEASE);
    }
    if (!__atomic_load_n(&item->job.done, __ATOMIC_ACQUIRE)) return NULL;
    tide_memory_sync();
    record(item);
    return item->context;
}

void tide_workers_collect(tide_workers *w)
{
    if (!w) return;
    worker_item **at = &w->retired;
    while (*at) {
        worker_item *p = *at;
        if (__atomic_load_n(&p->job.done, __ATOMIC_ACQUIRE)) {
            *at = p->retired_next;
            dispose(w, p);
        } else at = &p->retired_next;
    }
}

void tide_worker_forget(tide_workers *w, const uint32_t ticket)
{
    if (!ticket) return;
    worker_item *p = find(w, ticket);
    if (!p || p->forgotten) return;
    __atomic_store_n(&p->forgotten, 1u, __ATOMIC_RELEASE);
    // Work the provider was never handed goes at once, its captures with it
    bool waited = false;
    if (p->queued) {
        acquire(w);
        waited = p->waiting;
        if (waited) unlink_waiting(w, p);
        let_go(w);
    }
    if (waited || !p->submitted || __atomic_load_n(&p->job.done, __ATOMIC_ACQUIRE)) dispose(w, p);
    else {
        p->retired_next = w->retired;
        w->retired = p;
    }
}

void tide_workers_free(tide_workers *w)
{
    if (!w) return;
    w->profile = NULL;
    // Nothing more is handed over: what waits in the group never runs, and `waiting` stays as it is from here
    acquire(w);
    w->closing = true;
    let_go(w);
    for (worker_item *p = w->items; p; p = p->next)
        __atomic_store_n(&p->forgotten, 1u, __ATOMIC_RELEASE);
    for (worker_item *p = w->items; p; p = p->next) {
        while (p->submitted && !p->waiting && !__atomic_load_n(&p->job.done, __ATOMIC_ACQUIRE)) {
            if (p->jobs && p->jobs->help) p->jobs->help(p->jobs->self, false);
            tide_yield();
        }
    }
    while (w->items) dispose(w, w->items);
    free(w->buckets);
    free(w);
}

typedef struct parallel_work {
    void (*work)(void *, uint32_t);
    void *context;
    uint32_t count;
    uint64_t next;
    tide_heaps *match;
    tide_heaps *local;
    const worker_item *owner;
} parallel_work;

static void parallel_take(void *context)
{
    tide_memory_sync();
    parallel_work *p = context;
    const uint64_t mark = tide_scratch_mark();
    tide_heaps *match = tide_world_heaps[TIDE_IN_MATCH], *local = tide_world_heaps[TIDE_IN_LOCAL];
    tide_text_use(p->match, p->local);
    const worker_item *previous = current_worker;
    current_worker = p->owner;
    for (;;) {
        if (tide_worker_cancelled()) break;
        const uint64_t i = __atomic_fetch_add(&p->next, 1u, __ATOMIC_RELAXED);
        if (i >= p->count) break;
        p->work(p->context, (uint32_t)i);
    }
    current_worker = previous;
    tide_text_use(match, local);
    if (!mark) tide_scratch_free();
    else tide_scratch_reset(mark);
}

bool tide_worker_parallel(const uint32_t count, void (*work)(void *, uint32_t), void *context, const uint64_t cost)
{
    const tide_jobs *jobs = worker_jobs;
    if (!jobs || !jobs->background || !jobs->help || jobs->threads < 2 || count < 2 || cost < 100000u) {
        if (!current_worker) return false;
        for (uint32_t i = 0; i < count && !tide_worker_cancelled(); i++) work(context, i);
        return true;
    }
    parallel_work p = {work, context, count, 0, tide_world_heaps[TIDE_IN_MATCH], tide_world_heaps[TIDE_IN_LOCAL], current_worker};
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
