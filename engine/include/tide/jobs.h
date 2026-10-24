#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/entity.h"
#include "tide/profile.h"
#include "tide/table.h"

// Running a tick's systems on several threads, with the same results as on
// one.
//
// The threads are the platform layer's (tide_platform_jobs): all this needs
// is to run a function on each of them. A tick's systems become tasks: a
// system that splits its entities across threads has a task per chunk of
// them, any other one task. A system's tasks start once the systems it waits
// for are done (the schedule: see analyze_parallelism in tidec), with no
// barriers between stages. Each task records its structural changes and
// events in a queue of its own, and those go into the world's queue in the
// order one thread would have recorded them: system by system, entity by
// entity.
//
// Spawns hand out entity IDs, in order. A system that runs on one thread
// hands them out as it spawns, so systems that spawn that way wait for each
// other. One that splits its entities gets temporary handles instead
// (tide_entity_is_temporary), made from its task, never its thread, and once
// all its tasks are done it settles them: its entities get their IDs, in the
// order one thread would have given them, and its own data and what it
// recorded get the real handles. That step alone keeps the tick's order among
// systems that spawn, and systems waiting for it start after it, so they
// never see a temporary handle.

struct tide_background;
typedef struct tide_jobs {
    void *self;
    uint32_t threads; // That can run work: the pool's, and the caller's
    // Runs work(context, thread) on the caller's thread, as thread 0, and on
    // each other thread (1 and up) that's free to join while it runs, and
    // returns once it has returned on every one. The work has to get done on
    // the caller's thread alone if no other joins. Never called with one
    // thread, which needs none.
    void (*run)(void *self, void (*work)(void *context, uint32_t thread), void *context);
    // Where ticks say how long each system took (tide/profile.h), from its
    // first task's start to its last task's end, or NULL: nothing is measured.
    // The host that wants it gives one (tide_platform_jobs_profile).
    tide_profile *profile;
    // For a pool whose threads can sleep, or NULL. While `run` runs, asks up to
    // `count` of the threads that aren't in the work to run it: there's
    // something for that many more to do. With it, the work returns on a
    // thread other than the caller's once that thread has found nothing to do
    // for a while, and the pool lets it sleep until it's asked again, so a
    // thread can run the work several times in one `run`. Without it, a thread
    // with nothing to do stays in the work, awake, until it's all done.
    void (*more)(void *self, uint32_t count);
    // As `run`, calling in at most `others` of the threads beside the caller's,
    // or NULL. For work that's known to be little, like hashing what a tick
    // changed: waking every thread of a pool for it costs the caller more than
    // the threads save, while two or three take most of it off the caller.
    void (*run_some)(void *self, uint32_t others, void (*work)(void *context, uint32_t thread), void *context);
    // Queues independent local work without blocking the caller. NULL where
    // background threads are unavailable. `help` executes one queued item.
    // Helpers take priority over ordinary jobs. help(self, true) runs only
    // loop helpers, so a full pool can finish nested loops without recursing
    // into another whole calculation.
    void (*background)(void *self, struct tide_background *job);
    bool (*help)(void *self, bool helpers_only);
} tide_jobs;

// The entities a system's spawns got as it settled them: what a temporary
// handle of its tasks is now.
typedef struct tide_new_entities {
    uint32_t first_task;   // Its tasks, among the tick's
    uint32_t task_count;
    const uint32_t *start; // Each task's first entity in `entities`
    const tide_entity *entities;
} tide_new_entities;

// The real handle for `e` if it's one of these temporary ones, or else `e`.
tide_entity tide_settled(const tide_new_entities *settled, tide_entity e);

// A system, as the tick runs it: generated code has a table of them, in the
// tick's order.
typedef struct tide_system_tasks {
    // Its chunks this tick (at least 1), and roughly how much work they are:
    // the rows they go through, times what tidec reckons one costs, and its
    // parallel loops' steps. That tells whether there's enough to do for
    // threads to be worth it, and in how many pieces each chunk goes.
    uint32_t (*count)(const void *world, uint64_t *work);
    // Runs task `task` of `count * pieces`: chunk task / pieces, piece task %
    // pieces of its rows. A system that doesn't split its entities has one
    // task, in one piece; one with fewer chunks than there are threads gets
    // them in pieces, so a few heavy chunks still keep every thread busy.
    void (*run)(void *world, uint32_t task, uint32_t pieces);
    // Before its chunks run in more than one piece, once the systems it waits
    // for are done: the pages of what it changes made the world's to write,
    // since the pieces of one chunk write the same pages at once (see
    // tide_table_lanes_begin_pieces). NULL: it changes nothing.
    void (*own)(void *world);
    // After its chunks ran in more than one piece, before anything that waits
    // for it starts: what `own` began, ended (tide_table_lanes_end_pieces).
    // NULL: nothing to end.
    void (*done)(void *world);
    bool splits;           // Its entities go across threads, a chunk (in pieces) a task; else it's one task, whatever its work
    uint32_t wait_count;
    const uint32_t *waits; // Systems before it in the tick that it waits for
    bool spawns;           // It hands out entity IDs...
    bool settles;          // ...through temporary handles, settled once its tasks are done
    // A task's own data, with the real handles for the temporary ones it
    // stored: its mut components' entity fields. NULL when it has none.
    void (*settle)(void *world, uint32_t task, uint32_t pieces, const tide_new_entities *settled);
    const char *name; // Its name in Tide, for the profile (tide_profile)
} tide_system_tasks;

// What the tick runs its systems with.
typedef struct tide_tick_systems {
    const tide_system_tasks *systems;
    uint32_t count;
    void (*prepare)(void *world); // On each thread, before its first task
    tide_queue *queue;            // The world's
    tide_entities *entities;      // The world's
    // A recorded change or event, with the real handles for temporary ones
    void (*settle_item)(void *item, const tide_new_entities *settled);
    // Makes a new entity in the world, where a sharded world gives its spawns
    // handles of its own (tide/shard.h); NULL: tide_entity_create(entities)
    tide_entity (*create)(void *world);
} tide_tick_systems;

// Runs the tick's systems on `world`: on `jobs`' threads when there's enough
// to do, or else (and with no jobs) on this thread, in order. Either way, with
// the same results.
void tide_run_systems(void *world, const tide_tick_systems *tick, const tide_jobs *jobs);

// The queue code records into: the running task's own, on threads, or else
// `world`, the world's.
extern TIDE_THREAD_LOCAL tide_queue *tide_task_queue; // The running task's own, on threads
static inline tide_queue *tide_recording(tide_queue *world)
{
    return tide_task_queue ? tide_task_queue : world;
}

// The entity a Spawn makes: a temporary handle in a task of a system that
// settles its spawns, or else a new entity in `t`.
tide_entity tide_new_entity(tide_entities *t);
// The same, making a new entity with `create` (NULL: tide_entity_create).
tide_entity tide_new_entity_by(tide_entities *t, tide_entity (*create)(void *world), void *world);

// Runs work(context, task) for every task from 0 to count - 1, and returns once
// they're all done: a parallel loop's steps (see tide/grid.h). While systems
// run on threads, the threads with nothing else to do help, when `cost` (the
// loop's work, as tidec reckons it) is worth it; anywhere else, and for a
// small loop, it runs them on this thread, in order. The tasks never depend on
// each other, so the result is the same either way.
void tide_parallel_for(uint32_t count, void (*work)(void *context, uint32_t task), void *context, uint64_t cost);

// Jobs, for C that shares its own work out: a library with a task interface
// of its own (a physics solver's start and finish), which a loop that returns
// once it's done can't serve. Called from a system, or from what it calls.
//
// tide_job_start hands work(context) to another of the tick's threads and
// returns at once; tide_job_wait returns once it has run. A task nobody took
// by the time it's waited for runs there, so every task runs exactly once
// whatever the threads are doing, and with no other thread (a tick on one
// thread, a page without threads, code outside a tick) it runs inside
// tide_job_start, which then returns 0: nothing to wait for. Every task that
// was started is waited for, by the thread that started it, before the code
// that started it returns.
//
// tidec can't tell how much work C is, so a tick whose C started tasks runs on
// threads the next time, whatever its systems look like, for as long as its C
// goes on starting them: C that has little to do should start none.
//
// Tasks are the caller's to keep in step, as everything C does on threads is:
// they have to come out the same however many threads take them.
typedef uint32_t tide_job;
tide_job tide_job_start(void (*work)(void *context), void *context);
void tide_job_wait(tide_job job);

// The threads the running tick can have at work at once, the caller's
// included: 1 with no pool, and outside a tick. What a library sizes its
// workers by.
uint32_t tide_job_threads(void);

// For tasks that wait for each other inside their work, as a solver's workers
// wait for each stage it gives them. Such a wait is a loop, since it's mostly
// shorter than a sleep and a wake, with the CPU's pause in it and, after a
// while, the system's yield, which lets a thread with work have the core. The
// web has neither: a loop there holds its core for as long as it goes on, and
// with a thread on every core that keeps a thread with work off one. So a
// wait that has gone on long sleeps instead, where it has to.
//
// tide_job_sleep(word, seen) sleeps until tide_job_wake(word), unless *word
// isn't `seen` any more as it's called, and returns true. Where the thread
// can't sleep (a page's own), or needn't, as the pause and the yield are
// there (everywhere but the web), it returns false at once, and the caller
// goes on with its loop. Whoever changes the word calls tide_job_wake after:
// it costs a comparison while nobody sleeps. The word is 4 bytes that stay
// where they are while anyone may sleep on them.
bool tide_job_sleep(const uint32_t *word, uint32_t seen);
void tide_job_wake(const uint32_t *word);
