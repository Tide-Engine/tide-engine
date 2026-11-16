#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tide/index.h"
#include "tide_test.h"

// Indexes for indexed loops (tide/index.h), against a plain scan of every
// entry: a search gives every entry inside the box and every entry with a key
// that's no number, ascending, once each, and nothing much outside the box.

typedef struct rng {
    uint64_t s;
} rng;

static uint64_t next(rng *r)
{
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// In 0 .. 1
static double unit(rng *r)
{
    return (double)(next(r) >> 11) * (1.0 / 9007199254740992.0);
}

static double between(rng *r, const double a, const double b)
{
    return a + (b - a) * unit(r);
}

typedef struct entries {
    uint32_t dims, count;
    double *keys; // dims each
} entries;

// An entry's ordinal: not its place, so the index can't mistake one for the other
static uint64_t ordinal_of(const uint32_t i)
{
    return (uint64_t)i * 3u + 7u;
}

static bool all_finite(const entries *e, const uint32_t i)
{
    for (uint32_t d = 0; d < e->dims; d++) {
        if (!isfinite(e->keys[(size_t)i * e->dims + d])) return false;
    }
    return true;
}

static bool within(const double x, const double lo, const double hi)
{
    return x >= lo && x <= hi;
}

// The box widened more than the index widens it
static void loosen(const double lo, const double hi, double *wlo, double *whi)
{
    const double w = ((isfinite(lo) ? fabs(lo) : 0) + (isfinite(hi) ? fabs(hi) : 0)) * (1.0 / 16384.0) + 1e-28;
    *wlo = isfinite(lo) ? lo - w : lo;
    *whi = isfinite(hi) ? hi + w : hi;
}

static tide_index *build(const entries *e)
{
    tide_index *ix = tide_index_make();
    tide_index_begin(ix, e->dims);
    for (uint32_t i = 0; i < e->count; i++) tide_index_add(ix, e->keys + (size_t)i * e->dims, ordinal_of(i));
    tide_index_end(ix);
    return ix;
}

// Checks one search against the scan; the number of hits it gave
static uint32_t check_find(const tide_index *ix, const entries *e, const double *lo, const double *hi,
                           tide_index_hits *h)
{
    tide_index_find(ix, lo, hi, h);
    bool any_nan = false;
    for (uint32_t d = 0; d < e->dims; d++) any_nan |= isnan(lo[d]) || isnan(hi[d]);
    // Ascending, once each, and each an entry's
    bool ascending = true, known = true;
    for (uint32_t i = 0; i < h->count; i++) {
        if (i > 0 && h->items[i] <= h->items[i - 1]) ascending = false;
        if (h->items[i] < 7 || (h->items[i] - 7) % 3 != 0 || (h->items[i] - 7) / 3 >= e->count) known = false;
    }
    TIDE_CHECK(ascending);
    TIDE_CHECK(known);
    if (!ascending || !known) return h->count;
    bool *found = calloc(e->count ? e->count : 1, sizeof(bool));
    for (uint32_t i = 0; i < h->count; i++) found[(h->items[i] - 7) / 3] = true;
    uint32_t missing = 0, extra = 0;
    for (uint32_t i = 0; i < e->count; i++) {
        const double *k = e->keys + (size_t)i * e->dims;
        bool must = any_nan || !all_finite(e, i), may = must;
        if (!must) {
            must = may = true;
            for (uint32_t d = 0; d < e->dims; d++) {
                double wlo, whi;
                loosen(lo[d], hi[d], &wlo, &whi);
                must &= within(k[d], lo[d], hi[d]);
                may &= within(k[d], wlo, whi);
            }
        }
        if (must && !found[i]) missing++;
        if (found[i] && !may) extra++;
    }
    TIDE_CHECK(missing == 0);
    TIDE_CHECK(extra == 0);
    free(found);
    return h->count;
}

// Boxes of every kind over the entries: around an entry, small and large,
// open on a side, everything, inside out, and with a NaN bound.
static void random_box(rng *r, const entries *e, const double span, double *lo, double *hi)
{
    const uint32_t kind = (uint32_t)(next(r) % 16);
    const uint32_t at = e->count ? (uint32_t)(next(r) % e->count) : 0;
    for (uint32_t d = 0; d < e->dims; d++) {
        const double centre = e->count ? e->keys[(size_t)at * e->dims + d] : 0;
        const double c = isfinite(centre) ? centre : between(r, -span, span);
        double radius = span * 0.01 * unit(r);
        if (kind < 8) {
            lo[d] = c - radius;
            hi[d] = c + radius;
        } else if (kind < 10) {
            radius = span * unit(r);
            lo[d] = c - radius;
            hi[d] = c + radius;
        } else if (kind == 10) {
            lo[d] = -INFINITY;
            hi[d] = c;
        } else if (kind == 11) {
            lo[d] = c;
            hi[d] = INFINITY;
        } else if (kind == 12) {
            lo[d] = c + radius + 1;
            hi[d] = c - radius - 1;
        } else if (kind == 13) {
            lo[d] = c;
            hi[d] = c;
        } else if (kind == 14) {
            lo[d] = -INFINITY;
            hi[d] = INFINITY;
        } else {
            lo[d] = d == 0 ? NAN : c - radius;
            hi[d] = c + radius;
        }
    }
}

static entries make_entries(const uint32_t dims, const uint32_t count)
{
    entries e = {dims, count, calloc((size_t)count * dims + 1, sizeof(double))};
    return e;
}

static void check_random(const uint32_t dims, const uint32_t count, const uint64_t seed, const double odd_share,
                         const uint32_t searches)
{
    rng r = {seed};
    entries e = make_entries(dims, count);
    const double span = 1000;
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t d = 0; d < dims; d++) {
            double k = between(&r, -span, span);
            if (unit(&r) < odd_share) {
                const uint32_t which = (uint32_t)(next(&r) % 3);
                k = which == 0 ? NAN : which == 1 ? INFINITY : -INFINITY;
            }
            e.keys[(size_t)i * dims + d] = k;
        }
    }
    tide_index *ix = build(&e);
    TIDE_CHECK(tide_index_count(ix) == count);
    tide_index_hits h;
    tide_index_hits_init(&h);
    double lo[3], hi[3];
    for (uint32_t s = 0; s < searches; s++) {
        random_box(&r, &e, span, lo, hi);
        check_find(ix, &e, lo, hi, &h);
    }
    tide_index_hits_free(&h);
    tide_index_free(ix);
    free(e.keys);
}

TIDE_TEST(index_matches_a_scan_with_one_key)
{
    check_random(1, 1, 1, 0, 200);
    check_random(1, 30, 2, 0, 400);
    check_random(1, 3000, 3, 0, 400);
    check_random(1, 3000, 4, 0.05, 400);
}

TIDE_TEST(index_matches_a_scan_with_two_keys)
{
    check_random(2, 1, 11, 0, 200);
    check_random(2, 30, 12, 0, 400);
    check_random(2, 5000, 13, 0, 400);
    check_random(2, 5000, 14, 0.05, 400);
}

TIDE_TEST(index_matches_a_scan_with_three_keys)
{
    check_random(3, 1, 21, 0, 200);
    check_random(3, 30, 22, 0, 400);
    check_random(3, 5000, 23, 0, 400);
    check_random(3, 5000, 24, 0.05, 400);
}

TIDE_TEST(index_of_nothing_finds_nothing)
{
    for (uint32_t dims = 1; dims <= 3; dims++) {
        tide_index *ix = tide_index_make();
        tide_index_hits h;
        tide_index_hits_init(&h);
        const double lo[3] = {NAN, -INFINITY, 0}, hi[3] = {NAN, INFINITY, 0};
        tide_index_find(ix, lo, hi, &h); // Never begun
        TIDE_CHECK(h.count == 0);
        tide_index_begin(ix, dims);
        tide_index_end(ix);
        tide_index_find(ix, lo, hi, &h);
        TIDE_CHECK(h.count == 0);
        TIDE_CHECK(tide_index_count(ix) == 0);
        tide_index_hits_free(&h);
        tide_index_free(ix);
    }
    tide_index_free(NULL);
}

// Keys that are whole numbers, searched for one: exactly the entries with it,
// in the order added (tiles of a grid, a team number).
TIDE_TEST(index_finds_whole_numbers_exactly)
{
    rng r = {31};
    for (uint32_t dims = 1; dims <= 3; dims++) {
        entries e = make_entries(dims, 4000);
        for (uint32_t i = 0; i < e.count * dims; i++) e.keys[i] = (double)(int32_t)(next(&r) % 41) - 20;
        tide_index *ix = build(&e);
        tide_index_hits h;
        tide_index_hits_init(&h);
        for (uint32_t s = 0; s < 300; s++) {
            double at[3];
            for (uint32_t d = 0; d < dims; d++) at[d] = (double)(int32_t)(next(&r) % 45) - 22;
            check_find(ix, &e, at, at, &h);
            uint32_t exact = 0;
            for (uint32_t i = 0; i < e.count; i++) {
                bool same = true;
                for (uint32_t d = 0; d < dims; d++) same &= e.keys[(size_t)i * dims + d] == at[d];
                exact += same;
            }
            TIDE_CHECK(h.count == exact);
        }
        tide_index_hits_free(&h);
        tide_index_free(ix);
        free(e.keys);
    }
}

// Every entry at one place, and at a few: the grid's cells can't split them
TIDE_TEST(index_holds_entries_at_one_place)
{
    rng r = {41};
    for (uint32_t dims = 1; dims <= 3; dims++) {
        entries e = make_entries(dims, 2000);
        for (uint32_t i = 0; i < e.count; i++) {
            for (uint32_t d = 0; d < dims; d++) e.keys[(size_t)i * dims + d] = i < 1500 ? 5.0 : (double)(i % 3);
        }
        tide_index *ix = build(&e);
        tide_index_hits h;
        tide_index_hits_init(&h);
        const double five[3] = {5, 5, 5}, two[3] = {2, 2, 2}, zero[3] = {0, 0, 0}, six[3] = {6, 6, 6};
        TIDE_CHECK(check_find(ix, &e, five, five, &h) == 1500);
        TIDE_CHECK(check_find(ix, &e, two, two, &h) == 166);
        TIDE_CHECK(check_find(ix, &e, zero, two, &h) == 500);
        TIDE_CHECK(check_find(ix, &e, zero, six, &h) == 2000);
        double lo[3], hi[3];
        for (uint32_t s = 0; s < 100; s++) {
            random_box(&r, &e, 10, lo, hi);
            check_find(ix, &e, lo, hi, &h);
        }
        tide_index_hits_free(&h);
        tide_index_free(ix);
        free(e.keys);
    }
}

// Tight clusters far apart, keys of every size from denormals to near the
// largest double, and -0 beside 0.
TIDE_TEST(index_handles_clusters_and_extreme_keys)
{
    rng r = {51};
    static const double centres[] = {0, 1e-300, -1e-310, 3e5, -7e12, 1e300, -1.7e308, 1.7e308, 4.9e-324};
    const uint32_t centre_count = sizeof centres / sizeof centres[0];
    for (uint32_t dims = 1; dims <= 3; dims++) {
        entries e = make_entries(dims, 6000);
        for (uint32_t i = 0; i < e.count; i++) {
            const uint32_t c = (uint32_t)(next(&r) % centre_count);
            for (uint32_t d = 0; d < dims; d++) {
                const double centre = centres[(c + d) % centre_count];
                double k = centre + centre * 1e-9 * between(&r, -1, 1);
                if (next(&r) % 7 == 0) k = centre;
                if (next(&r) % 11 == 0) k = -0.0;
                if (next(&r) % 13 == 0) k = between(&r, -1, 1);
                if (next(&r) % 97 == 0) k = next(&r) % 2 ? NAN : INFINITY;
                e.keys[(size_t)i * dims + d] = k;
            }
        }
        tide_index *ix = build(&e);
        tide_index_hits h;
        tide_index_hits_init(&h);
        double lo[3], hi[3];
        for (uint32_t s = 0; s < 600; s++) {
            random_box(&r, &e, 1e6, lo, hi);
            if (s % 5 == 0) {
                for (uint32_t d = 0; d < dims; d++) {
                    const double c = centres[next(&r) % centre_count];
                    const double radius = fabs(c) * 1e-9 * unit(&r);
                    lo[d] = c - radius;
                    hi[d] = c + radius;
                }
            }
            if (s % 7 == 0) {
                for (uint32_t d = 0; d < dims; d++) {
                    lo[d] = -1.79e308;
                    hi[d] = 1.79e308;
                }
            }
            check_find(ix, &e, lo, hi, &h);
        }
        tide_index_hits_free(&h);
        tide_index_free(ix);
        free(e.keys);
    }
}

// One axis near the largest double wide and another narrower than the
// smallest: their ratio comes to 0, which would size the cells at 0, and the
// grid still has no more cells than twice its entries.
TIDE_TEST(index_grid_stays_small_for_any_widths)
{
    rng r = {71};
    for (uint32_t dims = 2; dims <= 3; dims++) {
        entries e = make_entries(dims, 3000);
        for (uint32_t i = 0; i < e.count; i++) {
            double *k = e.keys + (size_t)i * dims;
            k[0] = between(&r, -1.7e308, 1.7e308);
            k[1] = between(&r, 0, 1e-300);
            if (dims == 3) k[2] = (double)(i % 4);
        }
        tide_index *ix = build(&e);
        tide_index_hits h;
        tide_index_hits_init(&h);
        double lo[3], hi[3];
        for (uint32_t s = 0; s < 300; s++) {
            random_box(&r, &e, 1e300, lo, hi);
            check_find(ix, &e, lo, hi, &h);
        }
        tide_index_hits_free(&h);
        tide_index_free(ix);
        free(e.keys);
    }
}

// Every key no number: all candidates, always
TIDE_TEST(index_gives_entries_without_numbers_to_every_search)
{
    for (uint32_t dims = 1; dims <= 3; dims++) {
        entries e = make_entries(dims, 100);
        for (uint32_t i = 0; i < e.count * dims; i++) e.keys[i] = i % 2 ? NAN : -INFINITY;
        tide_index *ix = build(&e);
        tide_index_hits h;
        tide_index_hits_init(&h);
        const double lo[3] = {0, 0, 0}, hi[3] = {-1, -1, -1};
        TIDE_CHECK(check_find(ix, &e, lo, hi, &h) == 100);
        TIDE_CHECK(check_find(ix, &e, hi, lo, &h) == 100);
        tide_index_hits_free(&h);
        tide_index_free(ix);
        free(e.keys);
    }
}

// A search before tide_index_end gives every entry; an index begun again
// forgets its entries, with any number of keys.
TIDE_TEST(index_begun_again_starts_over)
{
    tide_index *ix = tide_index_make();
    tide_index_hits h;
    tide_index_hits_init(&h);
    tide_index_begin(ix, 3);
    for (uint32_t i = 0; i < 500; i++) {
        const double k[3] = {i, i, i};
        tide_index_add(ix, k, ordinal_of(i));
    }
    const double lo[3] = {10, 10, 10}, hi[3] = {12, 12, 12};
    tide_index_find(ix, lo, hi, &h);
    TIDE_CHECK(h.count == 500);
    tide_index_end(ix);
    tide_index_find(ix, lo, hi, &h);
    TIDE_CHECK(h.count == 3 && h.items[0] == ordinal_of(10) && h.items[2] == ordinal_of(12));
    uint32_t count = 0;
    const uint64_t *all = tide_index_ordinals(ix, &count);
    TIDE_CHECK(count == 500 && all[0] == ordinal_of(0) && all[499] == ordinal_of(499));

    tide_index_begin(ix, 1);
    for (uint32_t i = 0; i < 20; i++) {
        const double k = 100.0 - i;
        tide_index_add(ix, &k, ordinal_of(i));
    }
    tide_index_end(ix);
    const double one_lo = 90, one_hi = 95;
    tide_index_find(ix, &one_lo, &one_hi, &h);
    TIDE_CHECK(h.count == 6 && h.items[0] == ordinal_of(5) && h.items[5] == ordinal_of(10));
    TIDE_CHECK(tide_index_count(ix) == 20);

    tide_index_begin(ix, 7); // Taken as 3
    tide_index_end(ix);
    tide_index_find(ix, lo, hi, &h);
    TIDE_CHECK(h.count == 0);
    tide_index_hits_free(&h);
    tide_index_free(ix);
}

// Many entries: searches of a few, of most, and of every one of them, which
// order their hits in different ways.
TIDE_TEST(index_handles_many_entries)
{
    rng r = {61};
    for (uint32_t dims = 1; dims <= 3; dims++) {
        entries e = make_entries(dims, 200000);
        for (uint32_t i = 0; i < e.count * dims; i++) e.keys[i] = between(&r, 0, 1000);
        tide_index *ix = build(&e);
        tide_index_hits h;
        tide_index_hits_init(&h);
        static const double radii[] = {1, 5, 30, 100, 300, 600};
        for (uint32_t s = 0; s < 24; s++) {
            double lo[3], hi[3];
            const double radius = radii[s % 6];
            for (uint32_t d = 0; d < dims; d++) {
                const double c = between(&r, 0, 1000);
                lo[d] = c - radius;
                hi[d] = c + radius;
            }
            check_find(ix, &e, lo, hi, &h);
        }
        tide_index_hits_free(&h);
        tide_index_free(ix);
        free(e.keys);
    }
}

// ---------------------------------------------------------------------------
// Exact keys: whole numbers found by being equal, with doubles beside them

typedef struct exact_entries {
    uint32_t exact, dims, count;
    int64_t *whole; // exact each
    double *keys;   // dims each
} exact_entries;

static exact_entries make_exact(const uint32_t exact, const uint32_t dims, const uint32_t count)
{
    exact_entries e = {exact, dims, count, calloc((size_t)count * exact + 1, sizeof(int64_t)),
                       calloc((size_t)count * dims + 1, sizeof(double))};
    return e;
}

static void free_exact(exact_entries *e)
{
    free(e->whole);
    free(e->keys);
}

static tide_index *build_exact(const exact_entries *e)
{
    tide_index *ix = tide_index_make();
    tide_index_begin_exact(ix, e->exact, e->dims);
    for (uint32_t i = 0; i < e->count; i++) {
        tide_index_add_exact(ix, e->whole + (size_t)i * e->exact, e->keys + (size_t)i * e->dims, ordinal_of(i));
    }
    tide_index_end(ix);
    return ix;
}

// Checks one search against a scan: every entry whose whole numbers are those and whose doubles are inside the
// box (or no number), nothing whose whole numbers differ or whose doubles are outside a box widened 4 times more,
// ascending, once each. The number of hits.
static uint32_t check_exact(const tide_index *ix, const exact_entries *e, const int64_t *whole, const double *lo,
                            const double *hi, tide_index_hits *h)
{
    tide_index_find_exact(ix, whole, lo, hi, h);
    bool ascending = true, known = true;
    for (uint32_t i = 0; i < h->count; i++) {
        if (i > 0 && h->items[i] <= h->items[i - 1]) ascending = false;
        if (h->items[i] < 7 || (h->items[i] - 7) % 3 != 0 || (h->items[i] - 7) / 3 >= e->count) known = false;
    }
    TIDE_CHECK(ascending);
    TIDE_CHECK(known);
    if (!ascending || !known) return h->count;
    bool *found = calloc(e->count ? e->count : 1, sizeof(bool));
    for (uint32_t i = 0; i < h->count; i++) found[(h->items[i] - 7) / 3] = true;
    uint32_t missing = 0, extra = 0;
    for (uint32_t i = 0; i < e->count; i++) {
        bool same = true;
        for (uint32_t k = 0; k < e->exact; k++) same &= e->whole[(size_t)i * e->exact + k] == whole[k];
        bool must = same, may = same;
        for (uint32_t d = 0; d < e->dims; d++) {
            const double k = e->keys[(size_t)i * e->dims + d];
            if (!isfinite(k) || isnan(lo[d]) || isnan(hi[d])) continue;
            double wlo, whi;
            loosen(lo[d], hi[d], &wlo, &whi);
            must &= within(k, lo[d], hi[d]);
            may &= within(k, wlo, whi);
        }
        if (must && !found[i]) missing++;
        if (found[i] && !may) extra++;
    }
    TIDE_CHECK(missing == 0);
    TIDE_CHECK(extra == 0);
    free(found);
    return h->count;
}

// Whole numbers of every kind: few values (many entries alike), places in a box, both ends of a long, and longs
// a double can't tell apart
static int64_t random_whole(rng *r, const uint32_t kind)
{
    switch (kind % 5) {
    case 0: return (int64_t)(next(r) % 3);
    case 1: return (int64_t)(next(r) % 41) - 20;
    case 2: return next(r) % 2 ? INT64_MAX - (int64_t)(next(r) % 3) : INT64_MIN + (int64_t)(next(r) % 3);
    case 3: return ((int64_t)1 << 62) + (int64_t)(next(r) % 5);
    default: return (int64_t)next(r);
    }
}

static void check_exact_random(const uint32_t exact, const uint32_t dims, const uint32_t count, const uint64_t seed)
{
    rng r = {seed};
    exact_entries e = make_exact(exact, dims, count);
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t k = 0; k < exact; k++) e.whole[(size_t)i * exact + k] = random_whole(&r, k + (uint32_t)seed);
        for (uint32_t d = 0; d < dims; d++) {
            double v = between(&r, -100, 100);
            if (next(&r) % 20 == 0) v = next(&r) % 2 ? NAN : -INFINITY;
            e.keys[(size_t)i * dims + d] = v;
        }
    }
    tide_index *ix = build_exact(&e);
    TIDE_CHECK(tide_index_count(ix) == count);
    tide_index_hits h;
    tide_index_hits_init(&h);
    uint32_t hits = 0;
    for (uint32_t s = 0; s < 400; s++) {
        int64_t whole[TIDE_INDEX_EXACT];
        const uint32_t at = count ? (uint32_t)(next(&r) % count) : 0;
        for (uint32_t k = 0; k < exact; k++) {
            whole[k] = count && s % 4 != 3 ? e.whole[(size_t)at * exact + k] : random_whole(&r, k + (uint32_t)seed);
        }
        double lo[3], hi[3];
        for (uint32_t d = 0; d < dims; d++) {
            const uint32_t kind = (uint32_t)(next(&r) % 6);
            const double c = between(&r, -100, 100);
            lo[d] = kind == 0 ? -INFINITY : kind == 1 ? NAN : kind == 2 ? c + 5 : c - 30;
            hi[d] = kind == 0 ? INFINITY : kind == 3 ? c : kind == 2 ? c - 5 : c + 30;
        }
        hits += check_exact(ix, &e, whole, lo, hi, &h);
    }
    TIDE_CHECK(count == 0 || hits > 0);
    tide_index_hits_free(&h);
    tide_index_free(ix);
    free_exact(&e);
}

TIDE_TEST(index_finds_exact_keys_as_a_scan_does)
{
    for (uint32_t exact = 1; exact <= TIDE_INDEX_EXACT; exact++) {
        for (uint32_t dims = 0; dims <= 3; dims++) {
            check_exact_random(exact, dims, 1, 100u + exact * 4u + dims);
            check_exact_random(exact, dims, 40, 200u + exact * 4u + dims);
            check_exact_random(exact, dims, 3000, 300u + exact * 4u + dims);
        }
    }
}

// A world of chunks: places in a box, two dimensions, so each x is shared by hundreds. A search for a place
// gives exactly the one chunk there, and one for a long packed from it the same; doubles can't tell those longs
// apart.
TIDE_TEST(index_finds_one_chunk_among_many_with_its_x)
{
    const int32_t wide = 32, tall = 5;
    const uint32_t count = (uint32_t)(wide * wide * tall * 2);
    exact_entries places = make_exact(4, 0, count), packed = make_exact(1, 0, count);
    for (uint32_t i = 0; i < count; i++) {
        const int32_t p = (int32_t)(i / 2), x = p % wide - wide / 2, z = p / wide % wide - wide / 2, y = p / (wide * wide) - 2;
        int64_t *w = places.whole + (size_t)i * 4;
        w[0] = x;
        w[1] = y;
        w[2] = z;
        w[3] = i % 2;
        packed.whole[i] = (int64_t)((uint64_t)1 << 62 | (uint64_t)(x & 0xFFFFF) << 40 | (uint64_t)(y & 0xFFF) << 28 |
                                    (uint64_t)(z & 0xFFFFF) << 8 | (uint64_t)(i % 2));
    }
    tide_index *ix = build_exact(&places), *keyed = build_exact(&packed);
    tide_index_hits h;
    tide_index_hits_init(&h);
    rng r = {81};
    for (uint32_t s = 0; s < 500; s++) {
        const uint32_t i = (uint32_t)(next(&r) % count);
        TIDE_CHECK(check_exact(ix, &places, places.whole + (size_t)i * 4, NULL, NULL, &h) == 1);
        TIDE_CHECK(h.count == 1 && h.items[0] == ordinal_of(i));
        TIDE_CHECK(check_exact(keyed, &packed, packed.whole + i, NULL, NULL, &h) == 1);
        TIDE_CHECK(h.count == 1 && h.items[0] == ordinal_of(i));
        const int64_t nowhere[4] = {places.whole[(size_t)i * 4], 99, 0, 0};
        TIDE_CHECK(check_exact(ix, &places, nowhere, NULL, NULL, &h) == 0);
    }
    tide_index_hits_free(&h);
    tide_index_free(ix);
    tide_index_free(keyed);
    free_exact(&places);
    free_exact(&packed);
}

// Every entry alike (a bool, a team): one slot holds them all, in order. An index begun again with other counts,
// never built, of nothing, and searched the other way: every entry, never a wrong one.
TIDE_TEST(index_of_exact_keys_begins_again_and_mixes_safely)
{
    tide_index *ix = tide_index_make();
    tide_index_hits h;
    tide_index_hits_init(&h);
    const int64_t zero[TIDE_INDEX_EXACT] = {0}, one[TIDE_INDEX_EXACT] = {1};
    const double lo[3] = {-INFINITY, -INFINITY, -INFINITY}, hi[3] = {INFINITY, INFINITY, INFINITY};
    tide_index_find_exact(ix, zero, lo, hi, &h); // Never begun
    TIDE_CHECK(h.count == 0);
    tide_index_begin_exact(ix, 1, 0);
    tide_index_end(ix);
    tide_index_find_exact(ix, zero, lo, hi, &h);
    TIDE_CHECK(h.count == 0);

    tide_index_begin_exact(ix, 1, 1);
    for (uint32_t i = 0; i < 1000; i++) {
        const int64_t w = i % 10 == 0;
        const double k = (double)i;
        tide_index_add_exact(ix, &w, &k, ordinal_of(i));
    }
    tide_index_find_exact(ix, one, lo, hi, &h); // Not built: every entry
    TIDE_CHECK(h.count == 1000);
    tide_index_end(ix);
    tide_index_find_exact(ix, one, lo, hi, &h);
    TIDE_CHECK(h.count == 100 && h.items[0] == ordinal_of(0) && h.items[99] == ordinal_of(990));
    tide_index_find_exact(ix, zero, lo, hi, &h);
    TIDE_CHECK(h.count == 900 && h.items[0] == ordinal_of(1) && h.items[899] == ordinal_of(999));
    const double from = 95, to = 205;
    tide_index_find_exact(ix, one, &from, &to, &h);
    TIDE_CHECK(h.count == 11 && h.items[0] == ordinal_of(100) && h.items[10] == ordinal_of(200));
    tide_index_find_exact(ix, one, &to, &from, &h); // Inside out
    TIDE_CHECK(h.count == 0);
    tide_index_find(ix, lo, hi, &h); // Searched as doubles: every entry
    TIDE_CHECK(h.count == 1000);
    tide_index_add(ix, &from, ordinal_of(1000)); // Added as doubles: whole numbers of 0
    tide_index_end(ix);
    tide_index_find_exact(ix, zero, lo, hi, &h);
    TIDE_CHECK(h.count == 901 && h.items[900] == ordinal_of(1000));

    tide_index_begin_exact(ix, 99, 7); // Taken as TIDE_INDEX_EXACT and 3
    for (uint32_t i = 0; i < 300; i++) {
        int64_t w[TIDE_INDEX_EXACT];
        for (uint32_t k = 0; k < TIDE_INDEX_EXACT; k++) w[k] = (int64_t)(i % 3) * (int64_t)(k + 1);
        const double keys[3] = {i, -(double)i, 0};
        tide_index_add_exact(ix, w, keys, ordinal_of(i));
    }
    tide_index_end(ix);
    int64_t two[TIDE_INDEX_EXACT];
    for (uint32_t k = 0; k < TIDE_INDEX_EXACT; k++) two[k] = 2 * (int64_t)(k + 1);
    tide_index_find_exact(ix, two, lo, hi, &h);
    TIDE_CHECK(h.count == 100 && h.items[0] == ordinal_of(2) && h.items[99] == ordinal_of(299));

    tide_index_begin(ix, 2); // An index of doubles again: searched exactly, every entry
    for (uint32_t i = 0; i < 50; i++) {
        const double k[2] = {i, i};
        tide_index_add_exact(ix, zero, k, ordinal_of(i));
    }
    tide_index_end(ix);
    tide_index_find_exact(ix, zero, lo, hi, &h);
    TIDE_CHECK(h.count == 50);
    const double box_lo[2] = {10, 10}, box_hi[2] = {12, 12};
    tide_index_find(ix, box_lo, box_hi, &h);
    TIDE_CHECK(h.count == 3 && h.items[0] == ordinal_of(10));
    tide_index_hits_free(&h);
    tide_index_free(ix);
}

// ---------------------------------------------------------------------------
// Followed indexes: entries put, moved and dropped, against an index built whole from what's left

// What a followed index should hold: groups of `span` ordinals, the g-th from first_of(g), each ordinal with an
// entry or none
typedef struct followed_model {
    uint32_t exact, dims, groups, span;
    bool *has;
    int64_t *whole; // exact each
    double *keys;   // dims each
} followed_model;

// Groups of two tables, as a query's ordinals are, in no order of their own
static uint64_t first_of(const followed_model *m, const uint32_t g)
{
    return (uint64_t)(g % 2u ? 5u : 2u) << 32 | (uint64_t)(g / 2u) * m->span;
}

static followed_model make_model(const uint32_t exact, const uint32_t dims, const uint32_t groups, const uint32_t span)
{
    const size_t n = (size_t)groups * span;
    const followed_model m = {exact, dims, groups, span, calloc(n, sizeof(bool)), calloc(n * exact, sizeof(int64_t)),
                              calloc(n * dims + 1, sizeof(double))};
    return m;
}

static void free_model(followed_model *m)
{
    free(m->has);
    free(m->whole);
    free(m->keys);
}

// The model's entries as an index built whole, in ordinal order
static tide_index *build_model(const followed_model *m)
{
    tide_index *ix = tide_index_make();
    tide_index_begin_exact(ix, m->exact, m->dims);
    for (uint32_t parity = 0; parity < 2; parity++) {
        for (uint32_t g = parity; g < m->groups; g += 2) {
            for (uint32_t i = 0; i < m->span; i++) {
                const size_t at = (size_t)g * m->span + i;
                if (m->has[at]) tide_index_add_exact(ix, m->whole + at * m->exact, m->keys + at * m->dims, first_of(m, g) + i);
            }
        }
    }
    tide_index_end(ix);
    return ix;
}

static bool same_hits(const tide_index_hits *a, const tide_index_hits *b)
{
    return a->count == b->count && memcmp(a->items, b->items, (size_t)a->count * sizeof(uint64_t)) == 0;
}

// Searches for numbers entries have and for others, with every kind of bound: the followed index's hits are the
// whole-built one's, to the ordinal
static void check_followed(const tide_index *ix, const followed_model *m, rng *r, const uint32_t searches)
{
    tide_index *whole = build_model(m);
    TIDE_CHECK(tide_index_count(ix) == tide_index_count(whole));
    tide_index_hits a, b;
    tide_index_hits_init(&a);
    tide_index_hits_init(&b);
    const size_t n = (size_t)m->groups * m->span;
    uint32_t differ = 0;
    for (uint32_t s = 0; s < searches; s++) {
        int64_t key[TIDE_INDEX_EXACT] = {0};
        const size_t at = next(r) % n;
        memcpy(key, m->whole + at * m->exact, m->exact * sizeof(int64_t));
        if (next(r) % 8 == 0) { // Numbers no entry may have
            const uint64_t k = next(r) % m->exact;
            key[k] = (int64_t)((uint64_t)key[k] + 1u);
        }
        double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        for (uint32_t d = 0; d < m->dims; d++) {
            const double c = between(r, -100, 100), w = between(r, 0, 150);
            lo[d] = c - w;
            hi[d] = c + w;
            const uint64_t kind = next(r) % 12;
            if (kind == 0) lo[d] = NAN;
            if (kind == 1) hi[d] = INFINITY;
            if (kind == 2) lo[d] = hi[d] + 1;
        }
        tide_index_find_exact(ix, key, lo, hi, &a);
        tide_index_find_exact(whole, key, lo, hi, &b);
        differ += !same_hits(&a, &b);
    }
    TIDE_CHECK(differ == 0);
    // A search of doubles gives every entry, ascending
    const double none = 0;
    tide_index_find(ix, &none, &none, &a);
    tide_index_find(whole, &none, &none, &b);
    TIDE_CHECK(same_hits(&a, &b));
    tide_index_hits_free(&a);
    tide_index_hits_free(&b);
    tide_index_free(whole);
}

static void model_set(followed_model *m, rng *r, const size_t at, const uint32_t kind)
{
    for (uint32_t k = 0; k < m->exact; k++) m->whole[at * m->exact + k] = random_whole(r, kind + k);
    for (uint32_t d = 0; d < m->dims; d++) {
        double v = between(r, -100, 100);
        if (next(r) % 20 == 0) v = next(r) % 2 ? NAN : -INFINITY;
        m->keys[at * m->dims + d] = v;
    }
    m->has[at] = true;
}

// The index told of the model's group as it is: `all` puts every entry it has and drops every one it hasn't,
// as a chunk taken whole; otherwise only the places in `changed`
static void tell_group(tide_index *ix, const followed_model *m, const uint32_t g, const bool *changed)
{
    tide_index_group(ix, first_of(m, g), m->span);
    for (uint32_t i = 0; i < m->span; i++) {
        const size_t at = (size_t)g * m->span + i;
        if (changed && !changed[at]) continue;
        if (m->has[at]) tide_index_put(ix, m->whole + at * m->exact, m->keys + at * m->dims, first_of(m, g) + i);
        else tide_index_drop(ix, first_of(m, g) + i);
    }
}

static void check_followed_random(const uint32_t exact, const uint32_t dims, const uint32_t groups, const uint32_t span,
                                  const uint32_t kind, const uint64_t seed)
{
    rng r = {seed};
    followed_model m = make_model(exact, dims, groups, span);
    const size_t n = (size_t)groups * span;
    bool *changed = calloc(n, sizeof(bool));
    tide_index *ix = tide_index_make();
    TIDE_CHECK(!tide_index_followed(ix, exact, dims));
    tide_index_begin_followed(ix, exact, dims, seed % 2 ? (uint32_t)n : 0);
    // Half the groups to start with, some places holes, told whole in ascending order as a first build is
    const uint32_t started = groups / 2 ? groups / 2 : 1;
    for (uint32_t parity = 0; parity < 2; parity++) {
        for (uint32_t g = parity; g < started; g += 2) {
            for (uint32_t i = 0; i < span; i++) {
                if (next(&r) % 7) model_set(&m, &r, (size_t)g * span + i, kind);
            }
            tell_group(ix, &m, g, NULL);
        }
    }
    tide_index_end(ix);
    TIDE_CHECK(tide_index_followed(ix, exact, dims) && !tide_index_followed(ix, exact + 1, dims));
    check_followed(ix, &m, &r, 120);
    uint32_t known = started;
    for (uint32_t round = 0; round < 30; round++) {
        memset(changed, 0, n * sizeof(bool));
        const uint64_t before = tide_index_changes(ix);
        const uint32_t what = (uint32_t)(next(&r) % 6);
        if (what == 0 && known < groups) known++; // A group more, filled below as any other
        const uint32_t edits = what == 1 ? (uint32_t)(n / 2) : (uint32_t)(next(&r) % 24);
        for (uint32_t e = 0; e < edits; e++) {
            const size_t at = next(&r) % ((size_t)known * span);
            const uint64_t pick = next(&r) % 4;
            if (pick == 0) {
                m.has[at] = false; // Gone
            } else if (pick == 1 && m.has[at]) {
                // Its numbers again: no change
            } else {
                model_set(&m, &r, at, kind); // Made, or moved to other numbers
            }
            changed[at] = true;
        }
        if (what == 2) { // A group emptied whole, then some of it back
            const uint32_t g = (uint32_t)(next(&r) % known);
            tide_index_group(ix, first_of(&m, g), span);
            tide_index_clear(ix);
            for (uint32_t i = 0; i < span; i++) {
                const size_t at = (size_t)g * span + i;
                m.has[at] = false;
                if (next(&r) % 3 == 0) model_set(&m, &r, at, kind);
                changed[at] = true;
            }
        }
        // Told in any order of groups, and within a group from its end backwards on some rounds
        for (uint32_t k = 0; k < known; k++) {
            const uint32_t g = round % 2 ? known - 1 - k : (k + round * 5u) % known;
            if (round % 3 == 0) {
                tide_index_group(ix, first_of(&m, g), span);
                for (uint32_t i = span; i-- > 0;) {
                    const size_t at = (size_t)g * span + i;
                    if (!changed[at]) continue;
                    if (m.has[at]) tide_index_put(ix, m.whole + at * exact, m.keys + at * dims, first_of(&m, g) + i);
                    else tide_index_drop(ix, first_of(&m, g) + i);
                }
            } else {
                tell_group(ix, &m, g, round % 5 == 0 ? NULL : changed);
            }
        }
        // Until it's ended, a search gives every entry: nothing is missed
        if (tide_index_changes(ix) != before) {
            tide_index_hits h;
            tide_index_hits_init(&h);
            const int64_t key[TIDE_INDEX_EXACT] = {0};
            const double bound[3] = {0, 0, 0};
            tide_index_find_exact(ix, key, bound, bound, &h);
            TIDE_CHECK(h.count == tide_index_count(ix));
            tide_index_hits_free(&h);
        }
        tide_index_end(ix);
        TIDE_CHECK(tide_index_followed(ix, exact, dims));
        // No more was changed than was told: an entry put again as it was costs nothing
        uint32_t told = 0;
        for (size_t at = 0; at < n; at++) told += changed[at];
        TIDE_CHECK(tide_index_changes(ix) - before <= (uint64_t)told + (what == 2 ? span : 0));
        check_followed(ix, &m, &r, 60);
    }
    // Every entry put again as it is: nothing changes, and it's still built
    const uint64_t before = tide_index_changes(ix);
    for (uint32_t g = 0; g < known; g++) tell_group(ix, &m, g, NULL);
    TIDE_CHECK(tide_index_changes(ix) == before);
    TIDE_CHECK(tide_index_followed(ix, exact, dims));
    tide_index_free(ix);
    free(changed);
    free_model(&m);
}

TIDE_TEST(index_followed_gives_what_one_built_whole_gives)
{
    uint64_t seed = 1;
    for (uint32_t exact = 1; exact <= TIDE_INDEX_EXACT; exact += exact < 3 ? 1 : 2) {
        for (uint32_t dims = 0; dims <= 3; dims++) {
            check_followed_random(exact, dims, 6, 16, 0, seed++);   // Few values: most entries share a slot
            check_followed_random(exact, dims, 9, 64, 1, seed++);   // Places in a box
            check_followed_random(exact, dims, 3, 1024, 4, seed++); // Any long, in chunks as a table's
        }
    }
    check_followed_random(1, 0, 1, 1, 0, seed++);
    check_followed_random(2, 1, 40, 8, 3, seed++);
}

// 6,000 entries of two values, a third of them moved to the other each round: every slot's ring stays in order
TIDE_TEST(index_followed_keeps_its_order_when_most_entries_share_a_slot)
{
    rng r = {77};
    followed_model m = make_model(1, 0, 6, 1000);
    tide_index *ix = tide_index_make();
    tide_index_begin_followed(ix, 1, 0, 0);
    for (uint32_t g = 0; g < 6; g++) {
        for (uint32_t i = 0; i < 1000; i++) {
            m.has[g * 1000 + i] = true;
            m.whole[g * 1000 + i] = (int64_t)(next(&r) % 2);
        }
    }
    for (uint32_t g = 6; g-- > 0;) tell_group(ix, &m, g, NULL); // Backwards: nothing goes at a ring's end
    tide_index_end(ix);
    check_followed(ix, &m, &r, 8);
    for (uint32_t round = 0; round < 6; round++) {
        const uint32_t moved = round % 2 ? 2000 : 30; // Many (every entry linked again) and few (sorted in)
        for (uint32_t e = 0; e < moved; e++) {
            const size_t at = next(&r) % 6000;
            m.whole[at] ^= 1;
            tide_index_group(ix, first_of(&m, (uint32_t)(at / 1000)), 1000);
            tide_index_put(ix, m.whole + at, m.keys, first_of(&m, (uint32_t)(at / 1000)) + at % 1000);
        }
        tide_index_end(ix);
        check_followed(ix, &m, &r, 8);
    }
    tide_index_free(ix);
    free_model(&m);
}

// A copy of a followed index has its entries, and each is then changed apart from the other
TIDE_TEST(index_followed_copy_is_its_own)
{
    rng r = {4242};
    followed_model m = make_model(3, 1, 5, 64), kept = make_model(3, 1, 5, 64);
    const size_t n = (size_t)m.groups * m.span;
    tide_index *ix = tide_index_make();
    tide_index_begin_followed(ix, 3, 1, 0);
    for (uint32_t g = 0; g < 3; g++) {
        for (uint32_t i = 0; i < m.span; i++) {
            if (next(&r) % 5) model_set(&m, &r, (size_t)g * m.span + i, 1);
        }
        tell_group(ix, &m, g, NULL);
    }
    tide_index_end(ix);
    memcpy(kept.has, m.has, n * sizeof(bool));
    memcpy(kept.whole, m.whole, n * m.exact * sizeof(int64_t));
    memcpy(kept.keys, m.keys, n * m.dims * sizeof(double));
    tide_index *copy = tide_index_copy(ix);
    TIDE_CHECK(tide_index_followed(copy, 3, 1) && !tide_index_shared(copy) && !tide_index_shared(ix));
    check_followed(copy, &kept, &r, 100);
    // The first changes: rows move and go, and groups come, one of them below the others
    for (uint32_t e = 0; e < 60; e++) {
        const size_t at = next(&r) % n;
        if (next(&r) % 3) model_set(&m, &r, at, 1);
        else m.has[at] = false;
    }
    for (uint32_t g = m.groups; g-- > 0;) tell_group(ix, &m, g, NULL);
    tide_index_end(ix);
    check_followed(ix, &m, &r, 100);
    check_followed(copy, &kept, &r, 100);
    // ...then the copy, its own way, with a group the first never had its entries in
    for (uint32_t e = 0; e < 40; e++) {
        const size_t at = next(&r) % n;
        if (next(&r) % 2) model_set(&kept, &r, at, 1);
        else kept.has[at] = false;
    }
    for (uint32_t g = 0; g < kept.groups; g++) tell_group(copy, &kept, g, NULL);
    tide_index_end(copy);
    check_followed(copy, &kept, &r, 100);
    check_followed(ix, &m, &r, 100);
    tide_index_free(ix);
    check_followed(copy, &kept, &r, 20);
    // Of an index that isn't followed, and of a followed one with nothing: empty
    tide_index *plain = tide_index_make();
    tide_index_begin(plain, 1);
    const double key = 1;
    tide_index_add(plain, &key, 3);
    tide_index_end(plain);
    tide_index *none = tide_index_copy(plain);
    TIDE_CHECK(tide_index_count(none) == 0 && !tide_index_followed(none, 1, 0));
    tide_index_free(none);
    tide_index_begin_followed(plain, 2, 0, 0);
    tide_index_end(plain);
    none = tide_index_copy(plain);
    TIDE_CHECK(tide_index_count(none) == 0 && tide_index_followed(none, 2, 0));
    const int64_t two[2] = {1, 2};
    tide_index_group(none, 64, 8);
    tide_index_put(none, two, &key, 66);
    tide_index_end(none);
    TIDE_CHECK(tide_index_count(none) == 1 && tide_index_count(plain) == 0);
    tide_index_free(none);
    tide_index_free(plain);
    tide_index_free(copy);
    free_model(&m);
    free_model(&kept);
}

TIDE_TEST(index_followed_refuses_what_it_cant_take_and_begins_again)
{
    tide_index *ix = tide_index_make();
    tide_index_hits h;
    tide_index_hits_init(&h);
    const int64_t one[2] = {1, 2}, other[2] = {3, 4};
    const double no_keys[3] = {0, 0, 0};
    // Not followed: groups, puts and drops do nothing
    tide_index_begin_exact(ix, 2, 0);
    tide_index_group(ix, 0, 8);
    tide_index_put(ix, one, no_keys, 3);
    tide_index_drop(ix, 3);
    tide_index_clear(ix);
    tide_index_end(ix);
    TIDE_CHECK(tide_index_count(ix) == 0 && !tide_index_followed(ix, 2, 0));
    // Followed: nothing before a group, outside it, or in a group of no ordinals
    tide_index_begin_followed(ix, 2, 0, 1000);
    tide_index_put(ix, one, no_keys, 3);
    tide_index_group(ix, 100, 0);
    tide_index_put(ix, one, no_keys, 100);
    tide_index_group(ix, 100, 8);
    tide_index_put(ix, one, no_keys, 99);
    tide_index_put(ix, one, no_keys, 108);
    tide_index_put(ix, one, no_keys, UINT64_MAX);
    tide_index_drop(ix, 99);
    TIDE_CHECK(tide_index_count(ix) == 0);
    tide_index_put(ix, one, no_keys, 107);
    tide_index_add_exact(ix, one, no_keys, 100); // As a put
    tide_index_put(ix, other, no_keys, 103);
    tide_index_end(ix);
    TIDE_CHECK(tide_index_count(ix) == 3);
    tide_index_find_exact(ix, one, no_keys, no_keys, &h);
    TIDE_CHECK(h.count == 2 && h.items[0] == 100 && h.items[1] == 107);
    uint32_t listed = 9;
    TIDE_CHECK(tide_index_ordinals(ix, &listed) == NULL && listed == 0);
    // The group named again is the one it was; an ordinal's entry moves, and goes
    tide_index_group(ix, 200, 4);
    tide_index_group(ix, 100, 8);
    tide_index_put(ix, other, no_keys, 107);
    tide_index_drop(ix, 100);
    tide_index_drop(ix, 100);
    tide_index_end(ix);
    tide_index_find_exact(ix, one, no_keys, no_keys, &h);
    TIDE_CHECK(h.count == 0);
    tide_index_find_exact(ix, other, no_keys, no_keys, &h);
    TIDE_CHECK(h.count == 2 && h.items[0] == 103 && h.items[1] == 107);
    // More entries in a slot than a search's own room
    tide_index_group(ix, 1000, 500);
    for (uint64_t o = 1000; o < 1500; o++) tide_index_put(ix, other, no_keys, o);
    tide_index_end(ix);
    tide_index_find_exact(ix, other, no_keys, no_keys, &h);
    TIDE_CHECK(h.count == 502 && h.items[0] == 103 && h.items[2] == 1000 && h.items[501] == 1499);
    // Begun again as each other kind, and as a followed one: empty each time
    tide_index_begin_followed(ix, 1, 2, 0);
    tide_index_end(ix);
    TIDE_CHECK(tide_index_count(ix) == 0 && tide_index_followed(ix, 1, 2));
    tide_index_find_exact(ix, other, no_keys, no_keys, &h);
    TIDE_CHECK(h.count == 0);
    tide_index_begin(ix, 2);
    const double at[2] = {1, 1};
    tide_index_add(ix, at, 5);
    tide_index_end(ix);
    TIDE_CHECK(tide_index_count(ix) == 1 && !tide_index_followed(ix, 1, 2));
    tide_index_find(ix, at, at, &h);
    TIDE_CHECK(h.count == 1 && h.items[0] == 5);
    tide_index_hits_free(&h);
    tide_index_free(ix);
}

TIDE_TEST(index_held_by_several_lasts_until_the_last_lets_go)
{
    tide_index *ix = tide_index_make();
    TIDE_CHECK(!tide_index_shared(ix));
    tide_index_begin(ix, 1);
    const double key = 4;
    tide_index_add(ix, &key, 9);
    tide_index_end(ix);
    tide_index_share(ix);
    tide_index_share(ix);
    TIDE_CHECK(tide_index_shared(ix));
    tide_index_free(ix);
    TIDE_CHECK(tide_index_shared(ix));
    tide_index_free(ix);
    TIDE_CHECK(!tide_index_shared(ix));
    tide_index_hits h;
    tide_index_hits_init(&h);
    tide_index_find(ix, &key, &key, &h); // Still there for its last holder (the sanitizer's to say otherwise)
    TIDE_CHECK(h.count == 1 && h.items[0] == 9);
    tide_index_hits_free(&h);
    tide_index_free(ix);
}

TIDE_TEST(index_lock_opens_again)
{
    uint32_t lock = 0;
    tide_index_lock(&lock);
    tide_index_unlock(&lock);
    tide_index_lock(&lock);
    tide_index_unlock(&lock);
    TIDE_CHECK(lock == 0);
}
