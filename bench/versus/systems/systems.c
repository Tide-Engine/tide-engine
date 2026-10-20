// Systems in C: the same 64 loops, each over the arrays of every kind that
// has its component, called one after another. On threads, the loops go in
// the stages an expert works out once from what each reads and writes: a
// stage's loops touch nothing another of them writes, so they go at once, in
// pieces, and the next stage starts when they're all done. A stage with too
// little to do for threads to pay goes on this one, in order.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "kinds.h"
#include "tide/page.h" // tide_alloc: memory every thread can use on the web

// A tick goes on threads when its widest stages have this many rows (16,384
// entities of each kind), in pieces of at least PIECE rows: with less, waking
// the threads for each of its ten stages costs more than they save (measured
// on a 12-core Ryzen 9 7900X).
#define STAGE_ENOUGH 524288u
#define PIECE 8192u

typedef struct kind {
    float *a[KIND_COMPONENTS], *b[KIND_COMPONENTS];
    float *block; // The arrays, one after another
} kind;

// A system: the component it changes, and the one it reads besides, or -1
typedef struct loop {
    uint32_t writes;
    int32_t reads;
} loop;

// A piece of a loop on threads: rows `from` to `to` of a kind
typedef struct task {
    uint32_t system, kind, from, to;
} task;

typedef struct systems {
    uint32_t count;
    bool ready;
    kind kinds[KINDS];
    loop loops[SYSTEMS];
    uint32_t stage[SYSTEMS]; // How deep each is among those it waits for, from 0
    uint32_t stages;
    task *tasks;
} systems;

// Component `c`'s place among kind `q`'s, or -1
static int32_t place(const uint32_t q, const uint32_t c)
{
    for (uint32_t j = 0; j < KIND_COMPONENTS; j++) {
        if (kind_components[q][j] == c) return (int32_t)j;
    }
    return -1;
}

static bool conflict(const loop *x, const loop *y)
{
    return x->writes == y->writes || (int32_t)x->writes == y->reads || x->reads == (int32_t)y->writes;
}

static void *make(const versus_flag *flags)
{
    systems *s = tide_alloc_zeroed(1, sizeof *s);
    s->count = flags[0].value;
    for (uint32_t i = 0; i < SYSTEMS; i++) {
        const uint32_t c = i % COMPONENTS;
        s->loops[i] = (loop){c, c >= 1u && c <= 3u ? (int32_t)c - 1 : -1};
        for (uint32_t k = 0; k < i; k++) {
            if (conflict(&s->loops[i], &s->loops[k]) && s->stage[k] + 1u > s->stage[i]) s->stage[i] = s->stage[k] + 1u;
        }
        if (s->stage[i] + 1u > s->stages) s->stages = s->stage[i] + 1u;
    }
    s->tasks = tide_alloc_zeroed((size_t)SYSTEMS * 2u * ((s->count + PIECE - 1u) / PIECE + 1u), sizeof *s->tasks);
    return s;
}

static void setup(systems *s)
{
    // Arrays apart by a few cache lines more than their size, so no two
    // share their address's low bits (4K aliasing)
    const size_t stride = ((size_t)s->count + 15u) / 16u * 16u + 48u * 3u;
    for (uint32_t q = 0; q < KINDS; q++) {
        kind *k = &s->kinds[q];
        k->block = tide_alloc(2u * KIND_COMPONENTS * stride * sizeof(float));
        for (uint32_t j = 0; j < KIND_COMPONENTS; j++) {
            k->a[j] = k->block + 2u * j * stride;
            k->b[j] = k->a[j] + stride;
            const int32_t c = (int32_t)kind_components[q][j];
            for (uint32_t i = 0; i < s->count; i++) {
                k->a[j][i] = (float)(((int32_t)i + c) & 255) * (1.0f / 256.0f);
                k->b[j][i] = (float)(((int32_t)i * 3 + c) & 255) * (1.0f / 256.0f);
            }
        }
    }
}

// System `i`'s loop over rows `from` to `to` of kind `q`
static void run(const systems *s, const uint32_t i, const uint32_t q, const uint32_t from, const uint32_t to)
{
    const loop *l = &s->loops[i];
    const int32_t w = place(q, l->writes);
    if (w < 0) return;
    float *restrict a = s->kinds[q].a[w], *restrict b = s->kinds[q].b[w];
    if (l->reads < 0) {
        for (uint32_t r = from; r < to; r++) {
            const float x = a[r], y = b[r];
            a[r] = x * 0.75f + y * 0.25f;
            b[r] = y * 0.5f + 0.25f;
        }
        return;
    }
    const int32_t k = place(q, (uint32_t)l->reads);
    if (k < 0) return;
    const float *restrict ya = s->kinds[q].a[k], *restrict yb = s->kinds[q].b[k];
    for (uint32_t r = from; r < to; r++) {
        a[r] = a[r] * 0.75f + yb[r] * 0.25f;
        b[r] = b[r] * 0.5f + ya[r] * 0.5f;
    }
}

static void run_task(void *context, const uint32_t piece)
{
    const systems *s = context;
    const task *t = &s->tasks[piece];
    run(s, t->system, t->kind, t->from, t->to);
}

static void tick(void *state, const tide_jobs *jobs)
{
    systems *s = state;
    if (!s->ready) { // Tide's first tick spawns them, and the systems go through them from the next
        setup(s);
        s->ready = true;
        return;
    }
    // The widest stages have a loop for each component, each through two kinds' rows
    const uint64_t rows = (uint64_t)s->count * 2u * COMPONENTS;
    if (versus_pieces(jobs, rows < UINT32_MAX ? (uint32_t)rows : UINT32_MAX, STAGE_ENOUGH) <= 1u) {
        for (uint32_t i = 0; i < SYSTEMS; i++) {
            for (uint32_t q = 0; q < KINDS; q++) run(s, i, q, 0, s->count);
        }
        return;
    }
    const uint32_t pieces = (s->count + PIECE - 1u) / PIECE < jobs->threads ? (s->count + PIECE - 1u) / PIECE : jobs->threads;
    for (uint32_t stage = 0; stage < s->stages; stage++) {
        uint32_t tasks = 0;
        for (uint32_t i = 0; i < SYSTEMS; i++) {
            if (s->stage[i] != stage) continue;
            for (uint32_t q = 0; q < KINDS; q++) {
                if (place(q, s->loops[i].writes) < 0) continue;
                for (uint32_t p = 0; p < pieces; p++) {
                    s->tasks[tasks++] = (task){i, q, (uint32_t)((uint64_t)s->count * p / pieces), (uint32_t)((uint64_t)s->count * (p + 1u) / pieces)};
                }
            }
        }
        versus_for(jobs, tasks, run_task, s);
    }
}

static uint64_t digest(const void *state)
{
    const systems *s = state;
    uint64_t sum = 0;
    for (uint32_t q = 0; s->ready && q < KINDS; q++) {
        for (uint32_t i = 0; i < s->count; i++) {
            for (uint32_t j = 0; j < KIND_COMPONENTS; j++) sum += kind_item(q, i, j, s->kinds[q].a[j][i], s->kinds[q].b[j][i]);
        }
    }
    return sum;
}

static void destroy(void *state)
{
    systems *s = state;
    for (uint32_t q = 0; q < KINDS; q++) free(s->kinds[q].block);
    free(s->tasks);
    free(s);
}

const versus_way systems_c = {"C", make, tick, digest, destroy};
