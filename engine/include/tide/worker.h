#pragma once

#include "tide/jobs.h"

// A submitted item remains alive until done is acquired. The provider publishes
// done only after work has returned, so game code can then be unloaded safely.
typedef struct tide_background {
    struct tide_background *next;
    void (*work)(void *context);
    void *context;
    uint32_t done;
    bool helper;
} tide_background;

typedef struct tide_workers tide_workers;

// Admission limits for generated local workers. Zero members select defaults:
// 64 pending invocations, 64 MiB of retained captures, 4 MiB transferred/frame.
// The first two are for each priority apart (tide_worker_reserve_at).
// Byte limits are soft: the operation crossing one finishes, then further work
// waits. A single large input/result can exceed them, including on threadless hosts.
typedef struct tide_worker_limits {
    uint32_t pending;
    uint64_t capture_bytes, transfer_bytes;
} tide_worker_limits;

void tide_workers_frame(tide_workers **workers, const tide_worker_limits *limits, tide_profile *profile);
// Reserve before evaluating arguments. A zero ticket asks the task to retry on
// a later frame. Reservations are owned/forgotten exactly like submitted work.
uint32_t tide_worker_reserve(tide_workers **workers, const tide_jobs *jobs);
bool tide_worker_can_reserve(const tide_workers *workers);
// An invocation's priority (Tide's WorkerPriority, by its members' numbers; any other number is normal). It says
// which queued calculation a provider's free thread gets: the most urgent level's oldest, and after 4 starts of
// more urgent work a waiting normal one, after 16 a background one, so nothing waits for ever. A group hands a
// provider as many calculations as it has threads beside the caller's (one at least) and keeps the rest in its
// queues. Work a provider has is never interrupted. Each priority is admitted by what's pending and captured of
// its own, so none keeps another waiting for room: the limits are normal work's, urgent work has as much again
// and background work half as much (a group can so hold two and a half times its limits, with all three). The
// frame's transfer limit is one for all. The functions above are these with TIDE_WORKER_NORMAL.
enum { TIDE_WORKER_NORMAL = 0, TIDE_WORKER_URGENT = 1, TIDE_WORKER_BACKGROUND = 2 };
uint32_t tide_worker_reserve_at(tide_workers **workers, const tide_jobs *jobs, uint32_t priority);
bool tide_worker_can_reserve_at(const tide_workers *workers, uint32_t priority);
void tide_worker_submit(tide_workers *workers, uint32_t ticket, void (*work)(void *),
                        void (*release)(void *), void *context, uint64_t capture_bytes);
bool tide_worker_transfer_ready(const tide_workers *workers);
uint64_t tide_worker_transfer_begin(const tide_workers *workers);
void tide_worker_transferred(tide_workers *workers, uint64_t bytes, uint64_t began_ns, bool result);

// Frame-thread operations. Captures and results belong to context; release runs
// on the frame thread, after the provider has finished using it.
uint32_t tide_worker_start(tide_workers **workers, const tide_jobs *jobs,
                           void (*work)(void *), void (*release)(void *), void *context);
void *tide_worker_poll(tide_workers *workers, uint32_t ticket);
void tide_worker_forget(tide_workers *workers, uint32_t ticket);
void tide_workers_collect(tide_workers *workers);
void tide_workers_free(tide_workers *workers);

// Safe points in generated CPU code. False outside a local worker; shared by
// all of a worker's loop helpers. C finishes its current call normally.
bool tide_worker_cancelled(void);

// A worker's parallel loops share its provider and private heaps. A loop's
// captures must contain no references to another thread's scratch area.
const tide_jobs *tide_worker_enter(const tide_jobs *jobs);
void tide_worker_leave(const tide_jobs *previous);
bool tide_worker_parallel(uint32_t count, void (*work)(void *, uint32_t), void *context, uint64_t cost);
