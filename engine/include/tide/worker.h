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

// Frame-thread operations. Captures and results belong to context; release runs
// on the frame thread, after the provider has finished using it.
uint32_t tide_worker_start(tide_workers **workers, const tide_jobs *jobs,
                           void (*work)(void *), void (*release)(void *), void *context);
void *tide_worker_poll(tide_workers *workers, uint32_t ticket);
void tide_worker_forget(tide_workers *workers, uint32_t ticket);
void tide_workers_collect(tide_workers *workers);
void tide_workers_free(tide_workers *workers);

// A worker's parallel loops share its provider and private heaps. A loop's
// captures must contain no references to another thread's scratch area.
const tide_jobs *tide_worker_enter(const tide_jobs *jobs);
void tide_worker_leave(const tide_jobs *previous);
bool tide_worker_parallel(uint32_t count, void (*work)(void *, uint32_t), void *context, uint64_t cost);
