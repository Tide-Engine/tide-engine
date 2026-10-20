#include "tide/jobs.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/time.h"

// See tide/jobs.h.

// Below this much work in all (see tide_system_tasks.count), a tick runs on
// one thread: waking the others, and then reading on this one what they
// changed from their caches, would cost more than they'd save. It's about half
// a millisecond's work, or on the web, where waking a worker takes longer,
// one and a half.
#ifdef __wasm__
#define PARALLEL_WORK 60000000u
#else
#define PARALLEL_WORK 20000000u
#endif

// A parallel loop's steps go to the other threads past this much work: they're
// awake already, running the tick's tasks, so only a loop with next to nothing
// to do stays on its thread. tidec can't tell how long the loops inside a
// step go on, so a step can be far more work than it reckons: low, so a loop
// that's more work than it looks is shared too.
#ifdef __wasm__
#define LOOP_WORK 300000u
#else
#define LOOP_WORK 100000u
#endif

// A system with fewer chunks than there are threads runs each chunk in pieces
// (tide_system_tasks.run): about two tasks a thread, each worth at least this
// much work (a few microseconds), and up to this many pieces a chunk.
#define PIECE_WORK 250000u
#define MOST_PIECES 16u

// A thread takes a system's tasks a few at a time where each is less work
// than this, as many as come to it, so that a system of many chunks that are
// each next to nothing (a few rows of a cheap loop) costs a look for work and
// a count of what's done for every few, not every one. Never so many that the
// system has fewer takings than there are threads.
#define BATCH_WORK 250000u

// A thread that found nothing to do this many times in a row stops looking
// for a while: it yields its core, or leaves the tick where it can (see work).
#define IDLE_LOOKS 256u

#define NONE UINT32_MAX

// A system's pages for chunks that go in pieces (system_state.owned)
enum { OWNING = 1, OWNED };

// The task running on this thread, while systems run.
typedef struct task_context {
    tide_queue *queue; // Its own, on threads; NULL: the world's
    uint32_t task;     // Among the tick's
    uint32_t spawned;  // Temporary handles it gave
    bool settles;      // Its system's spawns give temporary handles
} task_context;

// The pieces each of a system's chunks runs in, from how many there are
// against the threads, and how much work they are.
static uint32_t pieces_for(const uint32_t chunks, const uint64_t work, const uint32_t threads)
{
    if (chunks == 0 || chunks >= threads) return 1u;
    uint64_t pieces = (2u * threads + chunks - 1u) / chunks;
    const uint64_t worth = work / chunks / PIECE_WORK;
    if (worth < pieces) pieces = worth;
    return pieces < 1u ? 1u : pieces > MOST_PIECES ? MOST_PIECES : (uint32_t)pieces;
}

static TIDE_THREAD_LOCAL task_context *running;
TIDE_THREAD_LOCAL tide_queue *tide_task_queue;

// Profiling (tide/profile.h): the jobs', when the host gave them one; the
// systems' first start and last end while a tick runs on threads.

// The least and the most of an atomic clock reading.
static void note_least(_Atomic uint64_t *least, const uint64_t t)
{
    uint64_t seen = atomic_load(least);
    while ((seen == 0 || t < seen) && !atomic_compare_exchange_weak(least, &seen, t)) {
    }
}

static void note_most(_Atomic uint64_t *most, const uint64_t t)
{
    uint64_t seen = atomic_load(most);
    while (t > seen && !atomic_compare_exchange_weak(most, &seen, t)) {
    }
}

tide_entity tide_new_entity(tide_entities *t)
{
    if (!running || !running->settles) return tide_entity_create(t);
    return (tide_entity){running->spawned++, TIDE_ENTITY_TEMPORARY | running->task};
}

tide_entity tide_settled(const tide_new_entities *settled, const tide_entity e)
{
    if (!tide_entity_is_temporary(e)) return e;
    const uint32_t task = (e.generation & ~TIDE_ENTITY_TEMPORARY) - settled->first_task;
    if (task >= settled->task_count || e.index >= settled->start[task + 1u] - settled->start[task]) return e;
    return settled->entities[settled->start[task] + e.index];
}

static void *zeroed(const size_t count, const size_t size)
{
    return tide_alloc_zeroed(count ? count : 1u, size);
}

static void run_task(void *world, const tide_system_tasks *system, task_context *context, const uint32_t task,
                     const uint32_t pieces)
{
    tide_memory_sync(); // Its queue's pages, which another thread may have made in an earlier tick
    running = context;
    tide_task_queue = context->queue;
    system->run(world, task, pieces);
    running = NULL;
    tide_task_queue = NULL;
}

// A system's spawns get their IDs, in the order one thread would have given
// them (task by task, spawn by spawn), and its tasks' data and what they
// recorded get the real handles: `queues` holds each task's, or with NULL,
// they're in the world's from `from` on.
static void settle_system(void *world, const tide_tick_systems *tick, const tide_system_tasks *system,
                          const uint32_t first_task, const uint32_t tasks, const uint32_t pieces, const uint32_t *spawned,
                          tide_queue *queues, tide_queue_place from)
{
    uint32_t *start = zeroed(tasks + 1u, sizeof *start);
    for (uint32_t k = 0; k < tasks; k++) start[k + 1u] = start[k] + spawned[k];
    if (start[tasks] == 0) {
        free(start);
        return;
    }
    tide_entity *entities = zeroed(start[tasks], sizeof *entities);
    for (uint32_t i = 0; i < start[tasks]; i++) entities[i] = tide_entity_create(tick->entities);
    const tide_new_entities settled = {first_task, tasks, start, entities};
    for (uint32_t k = 0; k < tasks; k++) {
        if (!spawned[k]) continue;
        if (system->settle) system->settle(world, k, pieces, &settled);
        if (!queues) continue;
        tide_queue_place at = {0, 0};
        for (void *item; (item = tide_queue_next(&queues[k], &at));) tick->settle_item(item, &settled);
    }
    for (void *item; !queues && (item = tide_queue_next(tick->queue, &from));) tick->settle_item(item, &settled);
    free(entities);
    free(start);
}

// ---------------------------------------------------------------------------
// On threads

// Each on a cache line of its own: threads take tasks of one system while
// others look at the next one's, which they'd otherwise take from each other
// with every task.
typedef struct system_state {
    _Alignas(64) atomic_uint next; // Its next task to start
    atomic_uint done;            // Its tasks finished
    atomic_uint waiting;         // Systems it waits for that aren't done
    atomic_uint settle_waiting;  // Before it settles: its tasks, and the system that spawns before it
    atomic_uint settle_claimed;
    atomic_uint owned;           // Its pages, for chunks that go in pieces (tide_system_tasks.own): not made, being made, made
    uint32_t tasks;
    uint32_t pieces;      // Each of its chunks runs in this many
    uint32_t first;       // Its first task among all the tick's
    uint32_t next_settle; // The system that spawns after it, if it settles its spawns
    uint32_t batch;       // Its tasks a thread takes at once, one after another (see BATCH_WORK)
    uint32_t batches;     // ...so how many times a thread takes some
    _Atomic uint64_t began; // Profiling: when its first task started, and its last ended
    _Atomic uint64_t ended;
} system_state;

// A loop a running system shares out (tide_parallel_for), on its stack.
typedef struct loop_job {
    void (*work)(void *context, uint32_t task);
    void *context;
    uint32_t count;
    atomic_uint next; // Its next task to start
    atomic_uint done;
} loop_job;

// Where threads find the loops running systems share out. The job lives on its
// system's thread's stack, so that thread waits for every thread looking at
// it (`users`, counted before they look) to be done before it goes.
#define LOOP_SLOTS 8u
typedef struct loop_slot {
    _Atomic(loop_job *) job;
    atomic_uint users;
} loop_slot;

// A task C started (tide_job_start), until the thread that started it has
// waited for it: free, being filled in, ready for a thread to take, running,
// done.
#define TASK_SLOTS 64u
enum { TASK_FREE, TASK_FILLING, TASK_READY, TASK_RUNNING, TASK_DONE };
typedef struct task_slot {
    atomic_uint state;
    void (*work)(void *context);
    void *context;
} task_slot;

typedef struct tick_run {
    loop_slot loops[LOOP_SLOTS];
    task_slot tasks[TASK_SLOTS];
    atomic_uint tasks_ready; // Those a thread could take now
    atomic_uint tasks_used;  // C started tasks in this tick
    uint32_t threads;        // The pool's, and the caller's
    const tide_jobs *jobs;
    void *world;
    const tide_tick_systems *tick;
    system_state *state;
    uint32_t *dependents;      // The systems that wait for each: system s's from first_dependent[s] to first_dependent[s + 1]
    uint32_t *first_dependent;
    tide_queue *queues;        // Each task's
    uint32_t *spawned;         // Each task's temporary handles
    atomic_uint finished;      // Systems done
    // The first system that may have something left to give out: every one
    // before it has given out all its tasks and its settling, so a thread
    // looking for work starts here rather than going through them again
    atomic_uint front;
    tide_profile *profile;     // The pool's, to time each system, or NULL
} tick_run;

// Waiting for another thread to finish what this one needs: a hint to the CPU.
static void relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
}

// There's something for `count` more threads to do: the pool's that left the
// tick for want of it (see work) are asked back.
static void ask(const tick_run *r, const uint32_t count)
{
    if (count && r->jobs->more) r->jobs->more(r->jobs->self, count);
}

// The system is done: those waiting for it are one closer to starting, and
// the next to settle its spawns one closer to it.
static void finish_system(tick_run *r, const uint32_t s)
{
    tide_memory_sync(); // What its tasks made on other threads (see tide/page.h)
    uint32_t ready = 0; // What this lets start
    for (uint32_t i = r->first_dependent[s]; i < r->first_dependent[s + 1u]; i++) {
        system_state *waited = &r->state[r->dependents[i]];
        if (atomic_fetch_sub(&waited->waiting, 1u) == 1u) ready += waited->batches;
    }
    if (r->state[s].next_settle != NONE && atomic_fetch_sub(&r->state[r->state[s].next_settle].settle_waiting, 1u) == 1u) ready++;
    atomic_fetch_add(&r->finished, 1u);
    // This thread goes on to one of them itself, so a system that only lets
    // one more task start wakes nobody
    if (ready > 1u) ask(r, ready - 1u);
}

// `count` of a system's tasks are done
static void finish_tasks(tick_run *r, const uint32_t s, const uint32_t count)
{
    system_state *st = &r->state[s];
    if (atomic_fetch_add(&st->done, count) + count != st->tasks) return;
    if (st->pieces > 1u && r->tick->systems[s].done) {
        tide_memory_sync(); // The pages its pieces wrote on other threads
        r->tick->systems[s].done(r->world);
    }
    if (r->tick->systems[s].settles) atomic_fetch_sub(&st->settle_waiting, 1u);
    else finish_system(r, s);
}

// Something this thread can do now: a task of the earliest system that has
// one ready, or a system's settling. False if there's nothing.
static bool work_once(tick_run *r)
{
    const tide_tick_systems *tick = r->tick;
    bool leading = true; // Every system looked at so far has nothing left to give out
    for (uint32_t s = atomic_load_explicit(&r->front, memory_order_relaxed); s < tick->count; s++) {
        system_state *st = &r->state[s];
        const tide_system_tasks *system = &tick->systems[s];
        if (system->settles && atomic_load(&st->settle_waiting) == 0 && !atomic_exchange(&st->settle_claimed, 1u)) {
            tide_memory_sync(); // What its tasks made on other threads (see tide/page.h)
            settle_system(r->world, tick, system, st->first, st->tasks, st->pieces, &r->spawned[st->first], &r->queues[st->first],
                          (tide_queue_place){0, 0});
            finish_system(r, s);
            return true;
        }
        if (atomic_load(&st->next) >= st->tasks) {
            // Its tasks are all given out, and its settling where it has one:
            // nothing more comes of it this tick, so the front can pass it
            if (leading && (!system->settles || atomic_load(&st->settle_claimed))) {
                unsigned at = s;
                atomic_compare_exchange_strong(&r->front, &at, s + 1u);
            } else {
                leading = false;
            }
            continue;
        }
        leading = false;
        if (atomic_load(&st->waiting)) continue;
        // Its pages, before its chunks' pieces write them at once: made by the
        // first thread that gets here, now that what it waits for is done and
        // has left them as this system finds them. The others look elsewhere
        // meanwhile
        if (st->pieces > 1u && system->own && atomic_load(&st->owned) != OWNED) {
            unsigned not_yet = 0u;
            if (!atomic_compare_exchange_strong(&st->owned, &not_yet, OWNING)) continue;
            tide_memory_sync(); // What the systems before it made on other threads
            system->own(r->world);
            atomic_store(&st->owned, OWNED);
            ask(r, st->batches - 1u); // The threads asked for its tasks may have left while there were none to take
        }
        const uint32_t first = atomic_fetch_add(&st->next, st->batch);
        if (first >= st->tasks) continue;
        const uint32_t end = st->tasks - first > st->batch ? first + st->batch : st->tasks;
        tide_memory_sync(); // What the systems before it made on other threads
        if (r->profile) note_least(&st->began, tide_time_now_ns());
        for (uint32_t task = first; task < end; task++) { // Each with its own queue and handles, as on its own
            task_context context = {&r->queues[st->first + task], st->first + task, 0, system->settles};
            run_task(r->world, system, &context, task, st->pieces);
            r->spawned[st->first + task] = context.spawned;
        }
        if (r->profile) note_most(&st->ended, tide_time_now_ns());
        finish_tasks(r, s, end - first);
        return true;
    }
    return false;
}

// The tick this thread works on, while systems run on threads: where its
// systems share their loops out.
static TIDE_THREAD_LOCAL tick_run *current;

// Whether this thread can sleep in the work it's given (tide_job_sleep): it's
// one of a pool's that can, in a tick. Only the web asks.
#if defined(__wasm__) && defined(__wasm_atomics__)
static TIDE_THREAD_LOCAL bool sleeps;
static void may_sleep(const bool can)
{
    sleeps = can;
}
#else
static void may_sleep(const bool can)
{
    (void)can;
}
#endif

// A task of a loop a running system shares out, if there's one to do.
static bool help_loop(tick_run *r)
{
    for (uint32_t i = 0; i < LOOP_SLOTS; i++) {
        loop_slot *slot = &r->loops[i];
        if (!atomic_load_explicit(&slot->job, memory_order_relaxed)) continue;
        atomic_fetch_add(&slot->users, 1u);
        loop_job *job = atomic_load(&slot->job);
        bool did = false;
        if (job) {
            const uint32_t task = atomic_fetch_add(&job->next, 1u);
            if (task < job->count) {
                tide_memory_sync(); // What its system made on its own thread
                job->work(job->context, task);
                atomic_fetch_add(&job->done, 1u);
                did = true;
            }
        }
        atomic_fetch_sub(&slot->users, 1u);
        if (did) return true;
    }
    return false;
}

// A task C started that no thread has taken, if there's one.
static bool help_task(tick_run *r)
{
    if (!atomic_load_explicit(&r->tasks_ready, memory_order_relaxed)) return false;
    for (uint32_t i = 0; i < TASK_SLOTS; i++) {
        task_slot *slot = &r->tasks[i];
        unsigned ready = TASK_READY;
        if (atomic_load_explicit(&slot->state, memory_order_relaxed) != TASK_READY) continue;
        if (!atomic_compare_exchange_strong(&slot->state, &ready, TASK_RUNNING)) continue;
        // The threads for the tasks still there are this one's to ask for:
        // whoever started them only asked for the first (tide_job_start). One
        // taken before it was counted leaves the count under what's there,
        // for a moment: nothing to ask for then
        const uint32_t ready_before = atomic_fetch_sub(&r->tasks_ready, 1u);
        if (ready_before > 1u && ready_before <= TASK_SLOTS) ask(r, ready_before - 1u);
        tide_memory_sync(); // What the thread that started it made
        slot->work(slot->context);
        atomic_store(&slot->state, TASK_DONE);
        return true;
    }
    return false;
}

static void work(void *context, const uint32_t thread)
{
    tick_run *r = context;
    current = r;
    if (r->tick->prepare) r->tick->prepare(r->world);
    // A thread with nothing to do spins a little, then lets the system run
    // something else on its core: with a thread on every core, one the
    // system pushes aside for a moment would otherwise hold the tick up for
    // a whole time slice, while the rest spin. Where the pool's threads can
    // sleep, it leaves the tick instead, and whatever makes more work than
    // the threads at it take asks it back (ask): the caller's thread stays, as
    // the tick is its to finish. What a running system shares out comes first,
    // its C's tasks and its loops: what waits for that system waits for them.
    const bool leaves = thread != 0u && r->jobs->more;
    may_sleep(leaves);
    uint32_t idle = 0;
    while (atomic_load(&r->finished) < r->tick->count) {
        if (help_task(r) || help_loop(r) || work_once(r)) {
            idle = 0;
        } else if (++idle < IDLE_LOOKS) {
            relax();
        } else if (leaves) {
            break;
        } else {
            tide_yield();
            idle = 0;
        }
    }
    may_sleep(false);
    current = NULL;
}

void tide_parallel_for(const uint32_t count, void (*work_fn)(void *context, uint32_t task), void *context,
                       const uint64_t cost)
{
    tick_run *r = current;
    loop_job job = {work_fn, context, count, 0u, 0u};
    atomic_init(&job.next, 0u);
    atomic_init(&job.done, 0u);
    loop_slot *slot = NULL;
    for (uint32_t i = 0; r && count > 1u && cost >= LOOP_WORK && i < LOOP_SLOTS && !slot; i++) {
        loop_job *none = NULL;
        if (atomic_compare_exchange_strong(&r->loops[i].job, &none, &job)) slot = &r->loops[i];
    }
    if (!slot) { // On this thread, in order
        for (uint32_t task = 0; task < count; task++) work_fn(context, task);
        return;
    }
    ask(r, count - 1u); // This thread takes its share
    for (;;) {
        const uint32_t task = atomic_fetch_add(&job.next, 1u);
        if (task >= count) break;
        work_fn(context, task);
        atomic_fetch_add(&job.done, 1u);
    }
    while (atomic_load(&job.done) < count) relax();
    atomic_store(&slot->job, NULL);
    while (atomic_load(&slot->users)) relax();
    tide_memory_sync(); // What the other threads made (see tide/page.h)
}

// While a tick runs on one thread: the threads it could have had, so its C
// knows there's a pool. And whether the last tick's C started tasks, which
// puts the next one on threads.
static TIDE_THREAD_LOCAL uint32_t serial_threads;
static TIDE_THREAD_LOCAL bool tasks_wanted;

tide_job tide_job_start(void (*work_fn)(void *context), void *context)
{
    tick_run *r = current;
    if (r) {
        atomic_store_explicit(&r->tasks_used, 1u, memory_order_relaxed);
        for (uint32_t i = 0; i < TASK_SLOTS; i++) {
            task_slot *slot = &r->tasks[i];
            unsigned none = TASK_FREE;
            if (!atomic_compare_exchange_strong(&slot->state, &none, TASK_FILLING)) continue;
            slot->work = work_fn;
            slot->context = context;
            atomic_store(&slot->state, TASK_READY);
            // A thread for the first of them, which asks for the rest's as it
            // takes it: code that starts a run of tasks pays for one call
            if (atomic_fetch_add(&r->tasks_ready, 1u) == 0u) ask(r, 1u);
            return i + 1u;
        }
    } else if (serial_threads > 1u) {
        tasks_wanted = true;
    }
    work_fn(context); // No thread to give it to, or no room to say so
    return 0;
}

void tide_job_wait(const tide_job job)
{
    tick_run *r = current;
    if (!job || !r) return;
    task_slot *slot = &r->tasks[job - 1u];
    unsigned ready = TASK_READY;
    if (atomic_compare_exchange_strong(&slot->state, &ready, TASK_RUNNING)) { // Nobody took it: here, then
        atomic_fetch_sub(&r->tasks_ready, 1u);
        slot->work(slot->context);
    } else {
        uint32_t idle = 0;
        while (atomic_load(&slot->state) != TASK_DONE) {
            if (++idle < 256u) {
                relax();
            } else {
                tide_yield();
                idle = 0;
            }
        }
    }
    atomic_store(&slot->state, TASK_FREE);
    tide_memory_sync(); // What the thread that ran it made (see tide/page.h)
}

uint32_t tide_job_threads(void)
{
    const tick_run *r = current;
    return r ? r->threads : serial_threads > 1u ? serial_threads : 1u;
}

#if defined(__wasm__) && defined(__wasm_atomics__)

// Threads in tide_job_sleep, or on their way in. A sleeper counts itself
// before the wait looks at the word, and whoever changed the word looks for
// sleepers after, so one of the two sees the other: a wake that finds no
// sleeper has nobody to wake.
static atomic_uint sleeping;

bool tide_job_sleep(const uint32_t *word, const uint32_t seen)
{
    if (!sleeps) return false; // The page's own thread can't wait
    atomic_fetch_add(&sleeping, 1u);
    __builtin_wasm_memory_atomic_wait32((int32_t *)(uintptr_t)word, (int32_t)seen, -1);
    atomic_fetch_sub(&sleeping, 1u);
    return true;
}

void tide_job_wake(const uint32_t *word)
{
    if (atomic_load(&sleeping)) __builtin_wasm_memory_atomic_notify((int32_t *)(uintptr_t)word, UINT32_MAX);
}

#else

bool tide_job_sleep(const uint32_t *word, const uint32_t seen)
{
    (void)word;
    (void)seen;
    return false; // A pause and a yield are there for the wait
}

void tide_job_wake(const uint32_t *word)
{
    (void)word;
}

#endif

// Each task's queue, kept from tick to tick by the thread that runs ticks, so
// their pages are made once rather than every tick.
static TIDE_THREAD_LOCAL tide_queue *kept_queues;
static TIDE_THREAD_LOCAL uint32_t kept_count;

static tide_queue *task_queues(const uint32_t tasks)
{
    if (tasks > kept_count) {
        kept_queues = tide_realloc(kept_queues, kept_count * sizeof *kept_queues, tasks * sizeof *kept_queues);
        memset(&kept_queues[kept_count], 0, (tasks - kept_count) * sizeof *kept_queues);
        kept_count = tasks;
    }
    return kept_queues;
}

static void run_on_threads(void *world, const tide_tick_systems *tick, const tide_jobs *jobs)
{
    const uint32_t count = tick->count;
    // The systems' states, from the first cache line that starts in their memory
    void *state_memory = zeroed((size_t)count + 1u, sizeof(system_state));
    const uintptr_t line = _Alignof(system_state);
    system_state *states = (system_state *)(((uintptr_t)state_memory + line - 1u) & ~(line - 1u));
    tick_run r = {.world = world, .tick = tick, .state = states,
                  .first_dependent = zeroed(count + 1u, sizeof(uint32_t)), .profile = jobs->profile,
                  .threads = jobs->threads, .jobs = jobs};
    for (uint32_t i = 0; i < LOOP_SLOTS; i++) {
        atomic_init(&r.loops[i].job, NULL);
        atomic_init(&r.loops[i].users, 0u);
    }
    for (uint32_t i = 0; i < TASK_SLOTS; i++) atomic_init(&r.tasks[i].state, TASK_FREE);
    atomic_init(&r.tasks_ready, 0u);
    atomic_init(&r.tasks_used, 0u);
    uint32_t tasks = 0;
    uint32_t edges = 0;
    uint32_t last_spawner = NONE;
    for (uint32_t s = 0; s < count; s++) {
        const tide_system_tasks *system = &tick->systems[s];
        system_state *st = &r.state[s];
        uint64_t work;
        const uint32_t chunks = system->count(world, &work);
        st->pieces = system->splits ? pieces_for(chunks, work, jobs->threads) : 1u;
        st->tasks = chunks * st->pieces;
        // A few at a time where each is next to nothing, but no fewer takings than threads
        const uint64_t each = work / st->tasks;
        const uint64_t worth = each ? (BATCH_WORK + each - 1u) / each : st->tasks;
        const uint32_t most = st->tasks / jobs->threads;
        st->batch = worth < most ? (uint32_t)worth : most ? most : 1u;
        st->batches = (st->tasks + st->batch - 1u) / st->batch;
        st->first = tasks;
        st->next_settle = NONE;
        tasks += st->tasks;
        atomic_init(&st->next, 0u);
        atomic_init(&st->done, 0u);
        atomic_init(&st->waiting, system->wait_count);
        atomic_init(&st->settle_claimed, 0u);
        atomic_init(&st->owned, 0u);
        atomic_init(&st->began, 0u);
        atomic_init(&st->ended, 0u);
        // Spawns hand out IDs in the tick's order: a system that settles its
        // spawns does so once its tasks are done and the one before it is
        // (one that hands them out as it runs waits for it in the schedule)
        atomic_init(&st->settle_waiting, 1u + (last_spawner != NONE ? 1u : 0u));
        if (system->spawns) {
            if (last_spawner != NONE && system->settles) r.state[last_spawner].next_settle = s;
            last_spawner = s;
        }
        edges += system->wait_count;
        for (uint32_t i = 0; i < system->wait_count; i++) r.first_dependent[system->waits[i] + 1u]++;
    }
    for (uint32_t s = 0; s < count; s++) r.first_dependent[s + 1u] += r.first_dependent[s];
    r.dependents = zeroed(edges, sizeof(uint32_t));
    uint32_t *filled = zeroed(count, sizeof(uint32_t));
    for (uint32_t s = 0; s < count; s++) {
        for (uint32_t i = 0; i < tick->systems[s].wait_count; i++) {
            const uint32_t on = tick->systems[s].waits[i];
            r.dependents[r.first_dependent[on] + filled[on]++] = s;
        }
    }
    free(filled);
    r.queues = task_queues(tasks);
    r.spawned = zeroed(tasks, sizeof(uint32_t));
    atomic_init(&r.finished, 0u);
    atomic_init(&r.front, 0u);

    jobs->run(jobs->self, work, &r);
    tide_memory_sync(); // What the other threads made (see tide/page.h)
    tasks_wanted = atomic_load(&r.tasks_used) != 0u;
    if (r.profile) {
        tide_profile_count(&r.profile->systems, count);
        for (uint32_t s = 0; s < count; s++) {
            tide_profile_add(&r.profile->systems, s, tick->systems[s].name, atomic_load(&r.state[s].began),
                             atomic_load(&r.state[s].ended));
        }
    }

    // What they recorded, in the order one thread would have
    for (uint32_t t = 0; t < tasks; t++) {
        tide_queue_append(tick->queue, &r.queues[t]); // Its pages go to the world's, for the empty ones the world kept
    }
    free(r.spawned);
    free(r.dependents);
    free(r.first_dependent);
    free(state_memory);
}

// ---------------------------------------------------------------------------

void tide_run_systems(void *world, const tide_tick_systems *tick, const tide_jobs *jobs)
{
    // C that started tasks in the last tick has work tidec can't see: on
    // threads again, until a tick starts none
    uint64_t work_in_all = tasks_wanted ? PARALLEL_WORK : 0;
    for (uint32_t s = 0; s < tick->count && jobs && jobs->threads > 1 && work_in_all < PARALLEL_WORK; s++) {
        uint64_t work = 0;
        tick->systems[s].count(world, &work);
        work_in_all += work;
    }
    if (jobs && jobs->threads > 1 && work_in_all >= PARALLEL_WORK) {
        run_on_threads(world, tick, jobs);
        tide_entities_settle(tick->entities);
        return;
    }
    if (jobs && jobs->threads > 1) tasks_wanted = false; // A tick with no threads to have says nothing of the next
    serial_threads = jobs ? jobs->threads : 1u;

    // One thread, in order, recording straight into the world's queue, with
    // the same tasks, temporary handles and settling as on threads
    if (tick->prepare) tick->prepare(world);
    tide_profile *profile = jobs ? jobs->profile : NULL;
    if (profile) tide_profile_count(&profile->systems, tick->count);
    uint32_t first = 0;
    for (uint32_t s = 0; s < tick->count; s++) {
        const tide_system_tasks *system = &tick->systems[s];
        uint64_t work;
        const uint32_t tasks = system->count(world, &work);
        uint32_t *spawned = system->settles ? zeroed(tasks, sizeof(uint32_t)) : NULL;
        const tide_queue_place from = tide_queue_end(tick->queue);
        const uint64_t began = profile ? tide_time_now_ns() : 0u;
        for (uint32_t task = 0; task < tasks; task++) {
            task_context context = {NULL, first + task, 0, system->settles};
            run_task(world, system, &context, task, 1u);
            if (spawned) spawned[task] = context.spawned;
        }
        if (spawned) settle_system(world, tick, system, first, tasks, 1u, spawned, NULL, from);
        if (profile) tide_profile_add(&profile->systems, s, system->name, began, tide_time_now_ns());
        free(spawned);
        first += tasks;
    }
    serial_threads = 0;
    tide_entities_settle(tick->entities);
}
