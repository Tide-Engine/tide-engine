// Indexes for indexed loops (tide/index.h) timed: building one over N entries,
// and a search of it for the entries within a radius of a point (about 10 of
// them) followed by the loop's own distance check on each candidate, against
// the plain loop checking every entry. Entries are spread evenly, one per unit
// of area or volume, in 1, 2 and 3 keys.
//
//   bench_index [--quick]
//
// Every run first checks that the indexed loop finds what the plain one does.
// --quick does only that, small, in a test's time.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/index.h"
#include "tide/time.h"

static uint64_t next(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static double unit(uint64_t *s)
{
    return (double)(next(s) >> 11) * (1.0 / 9007199254740992.0);
}

typedef struct world {
    uint32_t dims, n;
    float *pos; // dims each, as a game keeps them
    double *keys;
} world;

// The radius around a point holding about 10 entries at one per unit
static double radius_for(const uint32_t dims)
{
    return dims == 1 ? 5.0 : dims == 2 ? 1.784 : 1.337;
}

static bool near(const float *p, const float *c, const float r2, const uint32_t dims)
{
    float sum = 0;
    for (uint32_t d = 0; d < dims; d++) {
        const float x = p[d] - c[d];
        sum += x * x;
    }
    return sum <= r2;
}

// The plain loop: every entry checked. Its hits' sum of places, as a checksum.
static uint64_t plain(const world *w, const float *c, const float r2, uint32_t *hits)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < w->n; i++) {
        if (!near(w->pos + (size_t)i * w->dims, c, r2, w->dims)) continue;
        sum += i;
        (*hits)++;
    }
    return sum;
}

static uint64_t indexed(const world *w, const tide_index *ix, tide_index_hits *h, const float *c, const float r,
                        uint32_t *hits)
{
    double lo[3], hi[3];
    for (uint32_t d = 0; d < w->dims; d++) {
        lo[d] = (double)c[d] - (double)r;
        hi[d] = (double)c[d] + (double)r;
    }
    tide_index_find(ix, lo, hi, h);
    const float r2 = r * r;
    uint64_t sum = 0;
    for (uint32_t k = 0; k < h->count; k++) {
        const uint32_t i = (uint32_t)h->items[k];
        if (!near(w->pos + (size_t)i * w->dims, c, r2, w->dims)) continue;
        sum += i;
        (*hits)++;
    }
    return sum;
}

static void build(const world *w, tide_index *ix)
{
    tide_index_begin(ix, w->dims);
    for (uint32_t i = 0; i < w->n; i++) tide_index_add(ix, w->keys + (size_t)i * w->dims, i);
    tide_index_end(ix);
}

static world make_world(const uint32_t dims, const uint32_t n, uint64_t seed)
{
    world w = {dims, n, malloc((size_t)n * dims * sizeof(float)), malloc((size_t)n * dims * sizeof(double))};
    if (!w.pos || !w.keys) exit(1);
    double side = (double)n;
    if (dims == 2) {
        side = 1;
        while (side * side < (double)n) side += 1;
    } else if (dims == 3) {
        side = 1;
        while (side * side * side < (double)n) side += 1;
    }
    for (size_t i = 0; i < (size_t)n * dims; i++) {
        w.pos[i] = (float)(unit(&seed) * side);
        w.keys[i] = (double)w.pos[i];
    }
    return w;
}

static int run(const uint32_t dims, const uint32_t n, const bool quick)
{
    world w = make_world(dims, n, 0x1234u + n * 7u + dims);
    tide_index *ix = tide_index_make();
    tide_index_hits h;
    tide_index_hits_init(&h);
    const float r = (float)radius_for(dims);
    const float r2 = r * r;
    const uint32_t queries = quick ? 200u : 2000u;
    float *centres = malloc((size_t)queries * dims * sizeof(float));
    if (!centres) exit(1);
    uint64_t seed = 99;
    for (uint32_t q = 0; q < queries; q++) {
        const uint32_t at = (uint32_t)(next(&seed) % n);
        memcpy(centres + (size_t)q * dims, w.pos + (size_t)at * dims, dims * sizeof(float));
    }

    // The indexed loop finds what the plain one does
    build(&w, ix);
    uint32_t plain_hits = 0, indexed_hits = 0, candidates = 0;
    for (uint32_t q = 0; q < queries; q++) {
        const float *c = centres + (size_t)q * dims;
        const uint32_t before = indexed_hits;
        if (plain(&w, c, r2, &plain_hits) != indexed(&w, ix, &h, c, r, &indexed_hits) ||
            plain_hits != indexed_hits) {
            fprintf(stderr, "bench_index: %u keys, %u entries: search %u found %u, not %u\n", dims, n, q,
                    indexed_hits - before, plain_hits - before);
            return 1;
        }
        candidates += h.count;
    }
    if (quick) {
        printf("%u keys, %7u entries: %.1f hits, %.1f candidates a search\n", dims, n,
               (double)plain_hits / queries, (double)candidates / queries);
    } else {
        // Building, best of several
        uint64_t best_build = UINT64_MAX;
        const uint32_t builds = n >= 100000 ? 20u : 200u;
        for (uint32_t b = 0; b < builds; b++) {
            const uint64_t t0 = tide_time_now_ns();
            build(&w, ix);
            const uint64_t took = tide_time_now_ns() - t0;
            if (took < best_build) best_build = took;
        }
        uint64_t checksum = 0;
        uint32_t ignored = 0;
        const uint32_t plain_rounds = n >= 100000 ? 1u : n >= 10000 ? 5u : 50u;
        uint64_t t0 = tide_time_now_ns();
        for (uint32_t round = 0; round < plain_rounds; round++) {
            for (uint32_t q = 0; q < queries; q++) checksum += plain(&w, centres + (size_t)q * dims, r2, &ignored);
        }
        const double plain_ns = (double)(tide_time_now_ns() - t0) / ((double)plain_rounds * queries);
        const uint32_t rounds = 50;
        t0 = tide_time_now_ns();
        for (uint32_t round = 0; round < rounds; round++) {
            for (uint32_t q = 0; q < queries; q++) {
                checksum += indexed(&w, ix, &h, centres + (size_t)q * dims, r, &ignored);
            }
        }
        const double indexed_ns = (double)(tide_time_now_ns() - t0) / ((double)rounds * queries);
        printf("%u keys, %7u entries: build %8.1f us (%5.1f ns an entry) | search %7.1f ns vs scan %10.1f ns "
               "(%6.0fx) | %.1f hits, %.1f candidates (%llx)\n",
               dims, n, (double)best_build / 1000.0, (double)best_build / n, indexed_ns, plain_ns,
               plain_ns / indexed_ns, (double)plain_hits / queries, (double)candidates / queries,
               (unsigned long long)(checksum & 0xfff));
    }
    free(centres);
    tide_index_hits_free(&h);
    tide_index_free(ix);
    free(w.pos);
    free(w.keys);
    return 0;
}

int main(int argc, char **argv)
{
    bool quick = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) quick = true;
        else {
            fprintf(stderr, "usage: bench_index [--quick]\n");
            return 2;
        }
    }
    static const uint32_t sizes[] = {1000, 10000, 100000};
    for (uint32_t dims = 1; dims <= 3; dims++) {
        for (uint32_t s = 0; s < 3; s++) {
            if (run(dims, sizes[s], quick)) return 1;
        }
    }
    return 0;
}
