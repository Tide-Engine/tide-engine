// Buckets in C: one array of CAP cells for each bucket, a ring, with where
// each bucket's oldest value is and how many it holds beside it. A tick puts
// a value into each bucket, over its oldest once it's full: no element ever
// moves. On threads, each piece takes a run of buckets.

#include <stdlib.h>

#include "cap.h"
#include "tide/math.h" // tide_hash_i2: the random numbers Tide's Math.Hash gives
#include "tide/page.h" // tide_alloc: memory every thread can use on the web
#include "versus.h"

typedef struct buckets {
    uint32_t count;
    uint32_t tick;
    int32_t *cells;
    int32_t *heads;
    int32_t *counts;
} buckets;

static void *make(const versus_flag *flags)
{
    buckets *b = tide_alloc_zeroed(1, sizeof *b);
    b->count = flags[0].value;
    b->cells = tide_alloc_zeroed((size_t)b->count * CAP, sizeof(int32_t));
    b->heads = tide_alloc_zeroed(b->count, sizeof(int32_t));
    b->counts = tide_alloc_zeroed(b->count, sizeof(int32_t));
    return b;
}

typedef struct push_job {
    buckets *b;
    uint32_t pieces;
} push_job;

static void push(void *context, const uint32_t piece)
{
    const push_job *job = context;
    buckets *b = job->b;
    const uint32_t from = (uint32_t)((uint64_t)b->count * piece / job->pieces);
    const uint32_t to = (uint32_t)((uint64_t)b->count * (piece + 1) / job->pieces);
    const int32_t tick = (int32_t)b->tick;
    for (uint32_t i = from; i < to; i++) {
        const int32_t value = tide_hash_i2((tide_int2){tick, (int32_t)i}) & 0xFFFF;
        int32_t *cells = b->cells + (size_t)i * CAP;
        if (b->counts[i] < CAP) {
            cells[(b->heads[i] + b->counts[i]) % CAP] = value;
            b->counts[i]++;
        } else {
            cells[b->heads[i]] = value;
            b->heads[i] = (b->heads[i] + 1) % CAP;
        }
    }
}

static void tick(void *state, const tide_jobs *jobs)
{
    buckets *b = state;
    push_job job = {b, versus_pieces(jobs, b->count, 2048)};
    versus_for(jobs, job.pieces, push, &job);
    b->tick++; // As Tide's Time.tick: 0 in the first tick
}

typedef struct held {
    int32_t bucket;
    int32_t at;
    int32_t value;
} held;

static uint64_t digest(const void *state)
{
    const buckets *b = state;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < b->count; i++) {
        for (int32_t k = 0; k < b->counts[i]; k++) {
            const held h = {(int32_t)i, k, b->cells[(size_t)i * CAP + (uint32_t)(b->heads[i] + k) % CAP]};
            sum += versus_item(&h, sizeof h);
        }
    }
    return sum;
}

static void destroy(void *state)
{
    buckets *b = state;
    free(b->cells);
    free(b->heads);
    free(b->counts);
    free(b);
}

const versus_way buckets_c = {"C", make, tick, digest, destroy};
