// Levels of detail for a mesh (see mesh_lod.h): edges collapsed onto one of
// their ends, the cheapest first, by a quadric of the planes and of what's
// on them that each position has gathered (Garland and Heckbert's surface
// simplification, with Hoppe's attributes).

#include "mesh_lod.h"

#include "folders.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

// What a difference of 1 in each counts as, against the mesh's whole extent:
// a uv that slides that far is the texture gone that far over the surface.
#define WEIGHT_NORMAL 0.5
#define WEIGHT_UV 1.0
#define WEIGHT_COLOR 0.5
// How much more an open edge's own line counts than a triangle's plane
#define WEIGHT_BORDER 10.0
// A triangle may turn this much (the cosine between where it faced and where it would) and no more
#define TURN_LEAST 0.25
// The most corners of one position a collapse looks at: past it, the position stays
#define FAN_MOST 96u

typedef struct quadric {
    double a00, a11, a22, a10, a20, a21, b0, b1, b2, c, w;
} quadric;

typedef struct candidate {
    double error;
    uint32_t from, to;         // Positions
    uint32_t from_at, to_at;   // ...and how often each had changed when this was worked out
} candidate;

typedef struct maker {
    const mesh_lod_source *source;
    uint32_t vertices, triangles, positions;
    uint32_t parts;     // Floats of `values` a vertex
    uint32_t hard;      // ...of which the first ones tell a seam (uvs, colors)
    double *point;      // 3 a position, the mesh's extent as 1
    double extent;
    uint32_t *place;    // A vertex's position
    uint32_t *chart;    // ...and what tells it from the others there: a number for its position and its hard values
    float *values;      // `parts` a vertex, each times its weight
    uint32_t *corner_vertex, *corner_place, *corner_next; // 3 a triangle
    uint32_t *first;    // A position's first corner + 1
    uint8_t *alive;     // A triangle
    uint8_t *border, *locked, *gone; // A position
    uint32_t *changed;  // A position: counts what happened to it
    uint32_t *seen;     // A position: `mark` where it's been met
    uint32_t mark;
    quadric *plane;     // A position's
    quadric *held;      // A vertex's: what its values were over the triangles it was a corner of
    float *slope;       // 4 a part a vertex: how each value changed over them, and where from
    candidate *heap;
    uint32_t heap_count, heap_room;
    uint32_t left;      // Triangles alive
    double worst;       // The dearest collapse so far, squared, the extent as 1
} maker;

static void *room(const size_t count, const size_t size)
{
    void *p = calloc(count ? count : 1u, size);
    if (!p) {
        fprintf(stderr, "out of memory making a mesh's levels of detail\n");
        abort();
    }
    return p;
}

static void quadric_add(quadric *q, const quadric *r)
{
    q->a00 += r->a00, q->a11 += r->a11, q->a22 += r->a22;
    q->a10 += r->a10, q->a20 += r->a20, q->a21 += r->a21;
    q->b0 += r->b0, q->b1 += r->b1, q->b2 += r->b2;
    q->c += r->c, q->w += r->w;
}

// The plane n.p + d = 0, counted `w` times.
static void quadric_plane(quadric *q, const double n[3], const double d, const double w)
{
    q->a00 += w * n[0] * n[0], q->a11 += w * n[1] * n[1], q->a22 += w * n[2] * n[2];
    q->a10 += w * n[1] * n[0], q->a20 += w * n[2] * n[0], q->a21 += w * n[2] * n[1];
    q->b0 += w * n[0] * d, q->b1 += w * n[1] * d, q->b2 += w * n[2] * d;
    q->c += w * d * d, q->w += w;
}

static double quadric_at(const quadric *q, const double p[3])
{
    const double x = p[0], y = p[1], z = p[2];
    return q->a00 * x * x + q->a11 * y * y + q->a22 * z * z + 2.0 * (q->a10 * x * y + q->a20 * x * z + q->a21 * y * z)
         + 2.0 * (q->b0 * x + q->b1 * y + q->b2 * z) + q->c;
}

// What vertex `v` gathered of its values, were it at `p` with the values of vertex `as`.
static double held_at(const maker *m, const uint32_t v, const double p[3], const uint32_t as)
{
    double e = quadric_at(&m->held[v], p);
    const float *s = &m->values[(size_t)as * m->parts];
    const float *g = &m->slope[(size_t)v * m->parts * 4u];
    for (uint32_t k = 0; k < m->parts; k++) {
        const double value = s[k];
        const double there = (double)g[k * 4u] * p[0] + (double)g[k * 4u + 1u] * p[1] + (double)g[k * 4u + 2u] * p[2]
                           + (double)g[k * 4u + 3u];
        e += value * value * m->held[v].w - 2.0 * value * there;
    }
    return e;
}

static void sub(double out[3], const double a[3], const double b[3])
{
    out[0] = a[0] - b[0], out[1] = a[1] - b[1], out[2] = a[2] - b[2];
}

static void cross(double out[3], const double a[3], const double b[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static double dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static uint64_t mix(uint64_t h, const void *bytes, const size_t count)
{
    const uint8_t *b = bytes;
    for (size_t i = 0; i < count; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

// A number for each vertex that's the same for those whose `count` bytes at `stride` apart in `bytes`
// are, with `also` the same: 0, 1, 2... in the order they're first met.
static uint32_t number_alike(uint32_t *out, const uint32_t vertices, const void *bytes, const size_t stride,
                             const size_t count, const uint32_t *also)
{
    uint32_t slots = 16;
    while (slots < vertices * 2u && slots < (1u << 31)) slots *= 2u;
    uint32_t *table = room(slots, sizeof(uint32_t)); // A vertex + 1
    uint32_t next = 0;
    for (uint32_t v = 0; v < vertices; v++) {
        const uint8_t *mine = (const uint8_t *)bytes + (size_t)v * stride;
        uint64_t h = mix(14695981039346656037ull, mine, count);
        if (also) h = mix(h, &also[v], sizeof(uint32_t));
        uint32_t at = (uint32_t)(h ^ h >> 32) & (slots - 1u);
        for (;; at = (at + 1u) & (slots - 1u)) {
            if (!table[at]) {
                table[at] = v + 1u;
                out[v] = next++;
                break;
            }
            const uint32_t other = table[at] - 1u;
            if ((!also || also[other] == also[v]) && memcmp((const uint8_t *)bytes + (size_t)other * stride, mine, count) == 0) {
                out[v] = out[other];
                break;
            }
        }
    }
    free(table);
    return next;
}

static bool heap_before(const candidate *a, const candidate *b)
{
    if (a->error != b->error) return a->error < b->error;
    if (a->from != b->from) return a->from < b->from;
    return a->to < b->to;
}

static void heap_push(maker *m, const candidate c)
{
    if (m->heap_count == m->heap_room) {
        m->heap_room = m->heap_room ? m->heap_room * 2u : 1024u;
        m->heap = realloc(m->heap, (size_t)m->heap_room * sizeof(candidate));
        if (!m->heap) abort();
    }
    uint32_t i = m->heap_count++;
    while (i > 0) {
        const uint32_t up = (i - 1u) / 2u;
        if (!heap_before(&c, &m->heap[up])) break;
        m->heap[i] = m->heap[up];
        i = up;
    }
    m->heap[i] = c;
}

static candidate heap_pop(maker *m)
{
    const candidate top = m->heap[0];
    const candidate last = m->heap[--m->heap_count];
    uint32_t i = 0;
    for (;;) {
        uint32_t down = i * 2u + 1u;
        if (down >= m->heap_count) break;
        if (down + 1u < m->heap_count && heap_before(&m->heap[down + 1u], &m->heap[down])) down++;
        if (!heap_before(&m->heap[down], &last)) break;
        m->heap[i] = m->heap[down];
        i = down;
    }
    if (m->heap_count) m->heap[i] = last;
    return top;
}

// A position's corners that are still some triangle's, into `out`: how many, or FAN_MOST + 1 for
// more than that. The ones whose triangles are gone are let go of on the way.
static uint32_t fan(maker *m, const uint32_t place, uint32_t out[FAN_MOST])
{
    uint32_t count = 0;
    uint32_t *link = &m->first[place];
    while (*link) {
        const uint32_t corner = *link - 1u;
        if (!m->alive[corner / 3u] || m->corner_place[corner] != place) {
            *link = m->corner_next[corner];
            continue;
        }
        if (count == FAN_MOST) return FAN_MOST + 1u;
        out[count++] = corner;
        link = &m->corner_next[corner];
    }
    return count;
}

// The corner of `corner`'s triangle that's at `place`, or UINT32_MAX.
static uint32_t corner_at(const maker *m, const uint32_t corner, const uint32_t place)
{
    const uint32_t base = corner - corner % 3u;
    for (uint32_t k = 0; k < 3u; k++) {
        if (m->corner_place[base + k] == place) return base + k;
    }
    return UINT32_MAX;
}

// What moving position `from` onto `to` would do, and whether it may: `targets` gets, for each of
// `from`'s corners in `corners` whose triangle stays, the vertex of `to`'s it would become.
static bool weigh(maker *m, const uint32_t from, const uint32_t to, double *error, uint32_t corners[FAN_MOST],
                  uint32_t targets[FAN_MOST], uint32_t *count)
{
    if (m->locked[from] || m->gone[from] || m->gone[to]) return false;
    const uint32_t n = fan(m, from, corners);
    if (n == 0 || n > FAN_MOST) return false;
    *count = n;
    // The triangles the edge is a side of go. An open edge's position only moves along the open edge, and one
    // inside the surface along an edge two triangles share
    uint32_t dying = 0;
    for (uint32_t i = 0; i < n; i++) {
        targets[i] = UINT32_MAX;
        const uint32_t other = corner_at(m, corners[i], to);
        if (other == UINT32_MAX) continue;
        targets[i] = m->corner_vertex[other] | 0x80000000u; // Marked: this corner's triangle goes
        dying++;
    }
    if (dying == 0 || dying > 2u || (m->border[from] ? dying != 1u : dying != 2u)) return false;
    // No other position may be next to both but the ones across those triangles, or the surface would fold onto itself
    m->mark++;
    uint32_t shared = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t base = corners[i] - corners[i] % 3u;
        for (uint32_t k = 0; k < 3u; k++) m->seen[m->corner_place[base + k]] = m->mark;
    }
    uint32_t theirs[FAN_MOST];
    const uint32_t there = fan(m, to, theirs);
    if (there > FAN_MOST) return false;
    for (uint32_t i = 0; i < there; i++) {
        const uint32_t base = theirs[i] - theirs[i] % 3u;
        for (uint32_t k = 0; k < 3u; k++) {
            const uint32_t place = m->corner_place[base + k];
            if (place == from || place == to || m->seen[place] != m->mark) continue;
            m->seen[place] = m->mark - 1u; // Counted once
            shared++;
        }
    }
    m->mark++;
    if (shared != dying) return false;
    // Each vertex of `from`'s becomes one of `to`'s: the one it shared a triangle with, or the one that vertex's
    // kind (its uvs, its colors) is beside along the edge. A kind the edge isn't a side of is a seam the collapse
    // would tear
    const uint32_t parts = m->parts;
    for (uint32_t i = 0; i < n; i++) {
        if (targets[i] != UINT32_MAX) continue;
        const uint32_t mine = m->corner_vertex[corners[i]];
        uint32_t found = UINT32_MAX, kind = UINT32_MAX;
        for (uint32_t j = 0; j < n; j++) {
            if (targets[j] == UINT32_MAX || !(targets[j] & 0x80000000u)) continue;
            const uint32_t their_vertex = m->corner_vertex[corners[j]];
            const uint32_t becomes = targets[j] & 0x7FFFFFFFu;
            if (their_vertex == mine) {
                if (found != UINT32_MAX && m->chart[found] != m->chart[becomes]) return false;
                if (found == UINT32_MAX || becomes < found) found = becomes;
                kind = m->chart[becomes];
            } else if (m->chart[their_vertex] == m->chart[mine]) {
                if (kind != UINT32_MAX && kind != m->chart[becomes]) return false;
                kind = m->chart[becomes];
            }
        }
        if (kind == UINT32_MAX) return false;
        if (found == UINT32_MAX) {
            // Of that kind at `to`, the one whose other values (its normal) are nearest this vertex's
            double least = 0.0;
            const float *a = &m->values[(size_t)mine * parts];
            for (uint32_t j = 0; j < there + n; j++) {
                uint32_t candidate_vertex;
                if (j < there) {
                    candidate_vertex = m->corner_vertex[theirs[j]];
                } else {
                    if (targets[j - there] == UINT32_MAX || !(targets[j - there] & 0x80000000u)) continue;
                    candidate_vertex = targets[j - there] & 0x7FFFFFFFu;
                }
                if (m->chart[candidate_vertex] != kind) continue;
                const float *b = &m->values[(size_t)candidate_vertex * parts];
                double apart = 0.0;
                for (uint32_t k = m->hard; k < parts; k++) apart += ((double)a[k] - b[k]) * ((double)a[k] - b[k]);
                if (found == UINT32_MAX || apart < least || (apart == least && candidate_vertex < found)) {
                    found = candidate_vertex;
                    least = apart;
                }
            }
            if (found == UINT32_MAX) return false;
        }
        targets[i] = found;
    }
    // No triangle that stays may turn over, or go flat
    const double *p = &m->point[(size_t)to * 3u];
    for (uint32_t i = 0; i < n; i++) {
        if (targets[i] & 0x80000000u) continue;
        const uint32_t base = corners[i] - corners[i] % 3u;
        const uint32_t k = corners[i] - base;
        const double *a = &m->point[(size_t)from * 3u];
        const double *b = &m->point[(size_t)m->corner_place[base + (k + 1u) % 3u] * 3u];
        const double *c = &m->point[(size_t)m->corner_place[base + (k + 2u) % 3u] * 3u];
        double ab[3], ac[3], pb[3], pc[3], was[3], is[3];
        sub(ab, b, a), sub(ac, c, a), sub(pb, b, p), sub(pc, c, p);
        cross(was, ab, ac), cross(is, pb, pc);
        const double facing = dot(was, is);
        if (!(facing > TURN_LEAST * sqrt(dot(was, was) * dot(is, is)))) return false;
    }
    // What it costs: how far `to` is from the planes both have gathered, and how far `to`'s values are from
    // what `from`'s vertices held
    quadric both = m->plane[from];
    quadric_add(&both, &m->plane[to]);
    double cost = both.w > 0.0 ? quadric_at(&both, p) / both.w : 0.0;
    if (cost < 0.0) cost = 0.0;
    if (parts) {
        double held = 0.0, weight = 0.0;
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t mine = m->corner_vertex[corners[i]];
            bool counted = false; // A vertex is counted once, however many corners it is
            for (uint32_t j = 0; j < i && !counted; j++) counted = m->corner_vertex[corners[j]] == mine;
            if (counted) continue;
            held += held_at(m, mine, p, targets[i] & 0x7FFFFFFFu);
            weight += m->held[mine].w;
        }
        if (weight > 0.0 && held > 0.0) cost += held / weight;
    }
    if (m->source->apart) {
        double most = 0.0;
        for (uint32_t i = 0; i < n; i++) {
            const double share = m->source->apart(m->source->user, m->corner_vertex[corners[i]], targets[i] & 0x7FFFFFFFu);
            if (share > most) most = share;
        }
        double along[3];
        sub(along, p, &m->point[(size_t)from * 3u]);
        cost += most * most * dot(along, along);
    }
    *error = cost;
    return true;
}

static void offer(maker *m, const uint32_t from, const uint32_t to)
{
    uint32_t corners[FAN_MOST], targets[FAN_MOST], count;
    double error;
    if (!weigh(m, from, to, &error, corners, targets, &count)) return;
    heap_push(m, (candidate){.error = error, .from = from, .to = to, .from_at = m->changed[from], .to_at = m->changed[to]});
}

// Every edge there is, each way.
static void offer_all(maker *m)
{
    m->heap_count = 0;
    for (uint32_t t = 0; t < m->triangles; t++) {
        if (!m->alive[t]) continue;
        for (uint32_t k = 0; k < 3u; k++) {
            const uint32_t a = m->corner_place[t * 3u + k], b = m->corner_place[t * 3u + (k + 1u) % 3u];
            offer(m, a, b);
            offer(m, b, a);
        }
    }
}

static bool collapse(maker *m, const candidate *c)
{
    if (m->changed[c->from] != c->from_at || m->changed[c->to] != c->to_at) return false;
    uint32_t corners[FAN_MOST], targets[FAN_MOST], count;
    double error;
    if (!weigh(m, c->from, c->to, &error, corners, targets, &count)) return false;
    uint32_t last = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t corner = corners[i];
        if (targets[i] & 0x80000000u) {
            m->alive[corner / 3u] = 0;
            m->left--;
            continue;
        }
        const uint32_t mine = m->corner_vertex[corner], becomes = targets[i];
        if (mine != becomes && m->parts) {
            bool merged = false; // Once a vertex
            for (uint32_t j = 0; j < i && !merged; j++) merged = m->corner_vertex[corners[j]] == mine && !(targets[j] & 0x80000000u);
            if (!merged) {
                quadric_add(&m->held[becomes], &m->held[mine]);
                float *to = &m->slope[(size_t)becomes * m->parts * 4u];
                const float *from = &m->slope[(size_t)mine * m->parts * 4u];
                for (uint32_t k = 0; k < m->parts * 4u; k++) to[k] += from[k];
            }
        }
        last = corner + 1u;
    }
    // Now that every corner has been looked at as it was
    for (uint32_t i = 0; i < count; i++) {
        if (targets[i] & 0x80000000u) continue;
        m->corner_vertex[corners[i]] = targets[i];
        m->corner_place[corners[i]] = c->to;
    }
    // `from`'s corners after `to`'s: the list still goes through the ones whose triangles went, which `fan` drops
    if (last) {
        uint32_t *link = &m->first[c->to];
        while (*link) link = &m->corner_next[*link - 1u];
        *link = m->first[c->from];
    }
    m->first[c->from] = 0;
    quadric_add(&m->plane[c->to], &m->plane[c->from]);
    m->gone[c->from] = 1;
    m->changed[c->from]++;
    m->changed[c->to]++;
    if (error > m->worst) m->worst = error;
    // What's around `to` is other than it was
    uint32_t around[FAN_MOST];
    const uint32_t n = fan(m, c->to, around);
    if (n <= FAN_MOST) {
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t base = around[i] - around[i] % 3u;
            for (uint32_t k = 0; k < 3u; k++) {
                const uint32_t other = m->corner_place[base + k];
                if (other == c->to) continue;
                offer(m, other, c->to);
                offer(m, c->to, other);
            }
        }
    }
    return true;
}

// An edge of two positions, for counting the triangles it's a side of.
typedef struct edge {
    uint32_t low, high; // high + 1, or 0 for a free one
    uint32_t count;
    uint32_t corner; // Of one of them, at `low` or `high`: the one the other follows in its triangle
} edge;

static void gather(maker *m)
{
    // The planes of each position's triangles, by their area, and what each vertex's values do over them
    for (uint32_t t = 0; t < m->triangles; t++) {
        if (!m->alive[t]) continue;
        const uint32_t *place = &m->corner_place[t * 3u];
        const double *a = &m->point[(size_t)place[0] * 3u], *b = &m->point[(size_t)place[1] * 3u],
                     *c = &m->point[(size_t)place[2] * 3u];
        double ab[3], ac[3], n[3];
        sub(ab, b, a), sub(ac, c, a);
        cross(n, ab, ac);
        const double twice = sqrt(dot(n, n));
        if (!(twice > 0.0)) continue;
        n[0] /= twice, n[1] /= twice, n[2] /= twice;
        const double area = twice * 0.5;
        for (uint32_t k = 0; k < 3u; k++) quadric_plane(&m->plane[place[k]], n, 0.0 - dot(n, a), area);
        // A value that's s0, s1, s2 at the corners is g.p + d over the triangle
        const double d11 = dot(ab, ab), d12 = dot(ab, ac), d22 = dot(ac, ac);
        const double span = d11 * d22 - d12 * d12;
        if (!(span > 0.0)) continue;
        const uint32_t *vertex = &m->corner_vertex[t * 3u];
        for (uint32_t part = 0; part < m->parts; part++) {
            const double s0 = m->values[(size_t)vertex[0] * m->parts + part], s1 = m->values[(size_t)vertex[1] * m->parts + part],
                         s2 = m->values[(size_t)vertex[2] * m->parts + part];
            const double u = ((s1 - s0) * d22 - (s2 - s0) * d12) / span, v = ((s2 - s0) * d11 - (s1 - s0) * d12) / span;
            const double g[3] = {u * ab[0] + v * ac[0], u * ab[1] + v * ac[1], u * ab[2] + v * ac[2]};
            const double d = s0 - dot(g, a);
            for (uint32_t k = 0; k < 3u; k++) {
                quadric *q = &m->held[vertex[k]];
                q->a00 += area * g[0] * g[0], q->a11 += area * g[1] * g[1], q->a22 += area * g[2] * g[2];
                q->a10 += area * g[1] * g[0], q->a20 += area * g[2] * g[0], q->a21 += area * g[2] * g[1];
                q->b0 += area * g[0] * d, q->b1 += area * g[1] * d, q->b2 += area * g[2] * d;
                q->c += area * d * d;
                float *slope = &m->slope[((size_t)vertex[k] * m->parts + part) * 4u];
                slope[0] += (float)(area * g[0]), slope[1] += (float)(area * g[1]), slope[2] += (float)(area * g[2]);
                slope[3] += (float)(area * d);
            }
        }
        for (uint32_t k = 0; k < 3u && m->parts; k++) m->held[vertex[k]].w += area;
    }
    // Edges by how many triangles they're a side of: one is open, and keeps to its line; more than two, or two
    // that go the same way round, is no surface a collapse knows what to do with, and its ends stay
    uint32_t slots = 16;
    while (slots < m->triangles * 6u && slots < (1u << 31)) slots *= 2u;
    edge *edges = room(slots, sizeof(edge));
    for (uint32_t t = 0; t < m->triangles; t++) {
        if (!m->alive[t]) continue;
        for (uint32_t k = 0; k < 3u; k++) {
            const uint32_t a = m->corner_place[t * 3u + k], b = m->corner_place[t * 3u + (k + 1u) % 3u];
            const uint32_t low = a < b ? a : b, high = a < b ? b : a;
            uint64_t h = ((uint64_t)low << 32 | high) * 0x9E3779B97F4A7C15ull;
            uint32_t at = (uint32_t)(h >> 32) & (slots - 1u);
            while (edges[at].high && (edges[at].low != low || edges[at].high != high + 1u)) at = (at + 1u) & (slots - 1u);
            edge *e = &edges[at];
            if (!e->high) {
                *e = (edge){.low = low, .high = high + 1u, .count = 1, .corner = t * 3u + k};
                continue;
            }
            e->count++;
            // Its other triangle goes the other way along it, on a surface with two sides
            if (e->count > 2u || m->corner_place[e->corner] == a) m->locked[low] = m->locked[high] = 1;
        }
    }
    for (uint32_t at = 0; at < slots; at++) {
        const edge *e = &edges[at];
        if (!e->high || e->count != 1u) continue;
        const uint32_t t = e->corner / 3u, k = e->corner % 3u;
        const uint32_t from = m->corner_place[e->corner], to = m->corner_place[t * 3u + (k + 1u) % 3u];
        const uint32_t across = m->corner_place[t * 3u + (k + 2u) % 3u];
        m->border[from] = m->border[to] = 1;
        // A plane through the edge, square to its triangle: moving along the edge costs nothing, and off it does
        const double *a = &m->point[(size_t)from * 3u], *b = &m->point[(size_t)to * 3u], *c = &m->point[(size_t)across * 3u];
        double along[3], over[3], face[3], out[3];
        sub(along, b, a), sub(over, c, a);
        cross(face, along, over);
        cross(out, along, face);
        const double length = sqrt(dot(out, out));
        if (!(length > 0.0)) continue;
        out[0] /= length, out[1] /= length, out[2] /= length;
        const double weight = WEIGHT_BORDER * dot(along, along);
        quadric_plane(&m->plane[from], out, 0.0 - dot(out, a), weight);
        quadric_plane(&m->plane[to], out, 0.0 - dot(out, a), weight);
    }
    free(edges);
}

void mesh_lod_make(const mesh_lod_source *s, mesh_lod *out)
{
    *out = (mesh_lod){0};
    if (s->vertices == 0 || !s->position) return;
    // The ball, and the box the mesh's extent is the longest side of
    double low[3], high[3];
    for (uint32_t k = 0; k < 3u; k++) low[k] = high[k] = s->position[k];
    for (uint32_t v = 1; v < s->vertices; v++) {
        for (uint32_t k = 0; k < 3u; k++) {
            const double x = s->position[(size_t)v * 3u + k];
            if (x < low[k]) low[k] = x;
            if (x > high[k]) high[k] = x;
        }
    }
    const double center[3] = {(low[0] + high[0]) * 0.5, (low[1] + high[1]) * 0.5, (low[2] + high[2]) * 0.5};
    double reach = 0.0;
    for (uint32_t v = 0; v < s->vertices; v++) {
        const double p[3] = {s->position[(size_t)v * 3u], s->position[(size_t)v * 3u + 1u], s->position[(size_t)v * 3u + 2u]};
        double from[3];
        sub(from, p, center);
        const double away = dot(from, from);
        if (away > reach) reach = away;
    }
    for (uint32_t k = 0; k < 3u; k++) out->center[k] = (float)center[k];
    out->radius = (float)sqrt(reach);
    // A float rounds either way: the ball has to hold the mesh
    out->radius = nextafterf(out->radius, INFINITY);
    double extent = high[0] - low[0];
    if (high[1] - low[1] > extent) extent = high[1] - low[1];
    if (high[2] - low[2] > extent) extent = high[2] - low[2];
    if (!(extent > 0.0) || !isfinite(extent) || !isfinite(reach) || s->indices < 3u || !s->index) return;

    maker m = {.source = s, .vertices = s->vertices, .triangles = s->indices / 3u, .extent = extent};
    const uint32_t hard = (s->uv ? 2u : 0u) + (s->uv2 ? 2u : 0u) + (s->color ? 4u : 0u);
    m.hard = hard;
    m.parts = hard + (s->normal ? 3u : 0u);
    m.place = room(m.vertices, sizeof(uint32_t));
    m.chart = room(m.vertices, sizeof(uint32_t));
    m.positions = number_alike(m.place, m.vertices, s->position, 3u * sizeof(float), 3u * sizeof(float), NULL);
    m.values = room((size_t)m.vertices * (m.parts ? m.parts : 1u), sizeof(float));
    for (uint32_t v = 0; v < m.vertices; v++) {
        float *to = &m.values[(size_t)v * m.parts];
        if (s->uv) *to++ = (float)(s->uv[(size_t)v * 2u] * WEIGHT_UV), *to++ = (float)(s->uv[(size_t)v * 2u + 1u] * WEIGHT_UV);
        if (s->uv2) *to++ = (float)(s->uv2[(size_t)v * 2u] * WEIGHT_UV), *to++ = (float)(s->uv2[(size_t)v * 2u + 1u] * WEIGHT_UV);
        for (uint32_t k = 0; s->color && k < 4u; k++) *to++ = (float)(s->color[(size_t)v * 4u + k] * WEIGHT_COLOR);
        for (uint32_t k = 0; s->normal && k < 3u; k++) *to++ = (float)(s->normal[(size_t)v * 3u + k] * WEIGHT_NORMAL);
    }
    if (hard) {
        number_alike(m.chart, m.vertices, m.values, (size_t)m.parts * sizeof(float), (size_t)hard * sizeof(float), m.place);
    } else {
        memcpy(m.chart, m.place, (size_t)m.vertices * sizeof(uint32_t));
    }
    m.point = room((size_t)m.positions * 3u, sizeof(double));
    for (uint32_t v = 0; v < m.vertices; v++) {
        for (uint32_t k = 0; k < 3u; k++) m.point[(size_t)m.place[v] * 3u + k] = (s->position[(size_t)v * 3u + k] - low[k]) / extent;
    }
    m.corner_vertex = room((size_t)m.triangles * 3u, sizeof(uint32_t));
    m.corner_place = room((size_t)m.triangles * 3u, sizeof(uint32_t));
    m.corner_next = room((size_t)m.triangles * 3u, sizeof(uint32_t));
    m.first = room(m.positions, sizeof(uint32_t));
    m.alive = room(m.triangles, 1);
    m.border = room(m.positions, 1);
    m.locked = room(m.positions, 1);
    m.gone = room(m.positions, 1);
    m.changed = room(m.positions, sizeof(uint32_t));
    m.seen = room(m.positions, sizeof(uint32_t));
    m.mark = 1;
    m.plane = room(m.positions, sizeof(quadric));
    m.held = room(m.parts ? m.vertices : 1u, sizeof(quadric));
    m.slope = room((size_t)(m.parts ? m.vertices : 1u) * (m.parts ? m.parts : 1u) * 4u, sizeof(float));
    for (uint32_t t = m.triangles; t-- > 0;) { // From the last, so each position's corners are in their triangles' order
        const uint32_t *i = &s->index[(size_t)t * 3u];
        if (i[0] >= m.vertices || i[1] >= m.vertices || i[2] >= m.vertices) continue;
        const uint32_t a = m.place[i[0]], b = m.place[i[1]], c = m.place[i[2]];
        if (a == b || b == c || a == c) continue; // No triangle at all
        m.alive[t] = 1;
        m.left++;
        for (uint32_t k = 3u; k-- > 0;) {
            const uint32_t corner = t * 3u + k;
            m.corner_vertex[corner] = i[k];
            m.corner_place[corner] = m.place[i[k]];
            m.corner_next[corner] = m.first[m.place[i[k]]];
            m.first[m.place[i[k]]] = corner + 1u;
        }
    }
    gather(&m);

    uint32_t room_for = 0;
    uint32_t before = m.left;
    while (out->levels < MESH_LOD_MOST && before >= 2u) {
        const uint32_t aim = before / 2u;
        for (;;) {
            offer_all(&m);
            uint32_t done = 0;
            while (m.left > aim && m.heap_count) {
                const candidate c = heap_pop(&m);
                if (collapse(&m, &c)) done++;
            }
            if (m.left <= aim || done == 0) break; // Or again: what one collapse refused, the ones since may allow
        }
        // A level that can't take a quarter of what's left is the last there's any use in, and so is one that's
        // off by half the mesh's own reach: what's drawn that small is a few pixels whatever its triangles are
        if (m.left > before - before / 4u) break;
        if (sqrt(m.worst) * extent > 0.5 * (double)out->radius) break;
        const uint32_t count = m.left * 3u;
        if (out->indices + count > room_for) {
            room_for = (out->indices + count) * 2u;
            out->index = realloc(out->index, (size_t)room_for * sizeof(uint32_t));
            if (!out->index) abort();
        }
        const uint32_t level = out->levels++;
        out->first[level] = out->indices;
        out->count[level] = count;
        out->error[level] = (float)(sqrt(m.worst) * extent);
        for (uint32_t t = 0; t < m.triangles; t++) {
            if (!m.alive[t]) continue;
            memcpy(&out->index[out->indices], &m.corner_vertex[t * 3u], 3u * sizeof(uint32_t));
            out->indices += 3u;
        }
        before = m.left;
    }
    free(m.place), free(m.chart), free(m.values), free(m.point);
    free(m.corner_vertex), free(m.corner_place), free(m.corner_next), free(m.first);
    free(m.alive), free(m.border), free(m.locked), free(m.gone), free(m.changed), free(m.seen);
    free(m.plane), free(m.held), free(m.slope), free(m.heap);
}

void mesh_lod_free(mesh_lod *lod)
{
    free(lod->index);
    *lod = (mesh_lod){0};
}

// ---------------------------------------------------------------------------
// The cache

typedef struct kept {
    uint8_t key[32];
    mesh_lod lod;
} kept;

static kept *kept_levels;
static uint32_t kept_count, kept_room;
static uint32_t made_count;

uint32_t mesh_lod_made(void)
{
    return made_count;
}

// A file of levels: these words, each level's first index, count and error, then the indices.
enum { FILE_MAGIC = 0x444F4C54, FILE_VERSION = 1, FILE_HEAD = 8 + MESH_LOD_MOST * 3 };

static bool levels_hold(const mesh_lod *lod, const uint32_t vertices)
{
    if (lod->levels > MESH_LOD_MOST || !(lod->radius >= 0.0f) || !isfinite(lod->radius)) return false;
    float before = 0.0f;
    for (uint32_t i = 0; i < lod->levels; i++) {
        if (lod->count[i] == 0 || lod->count[i] % 3u || lod->first[i] > lod->indices || lod->count[i] > lod->indices - lod->first[i]
            || !(lod->error[i] >= before) || !isfinite(lod->error[i])) {
            return false;
        }
        before = lod->error[i];
    }
    for (uint32_t i = 0; i < lod->indices; i++) {
        if (lod->index[i] >= vertices) return false;
    }
    return isfinite(lod->center[0]) && isfinite(lod->center[1]) && isfinite(lod->center[2]);
}

static bool read_levels(const char *path, const uint32_t vertices, mesh_lod *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint32_t head[FILE_HEAD];
    mesh_lod lod = {0};
    bool ok = fread(head, sizeof(uint32_t), FILE_HEAD, f) == FILE_HEAD && head[0] == FILE_MAGIC && head[1] == FILE_VERSION
           && head[6] <= MESH_LOD_MOST && head[7] <= (3u << 25) * MESH_LOD_MOST;
    if (ok) {
        memcpy(lod.center, &head[2], sizeof lod.center);
        memcpy(&lod.radius, &head[5], sizeof lod.radius);
        lod.levels = head[6];
        lod.indices = head[7];
        for (uint32_t i = 0; i < MESH_LOD_MOST; i++) {
            lod.first[i] = head[8u + i * 3u];
            lod.count[i] = head[9u + i * 3u];
            memcpy(&lod.error[i], &head[10u + i * 3u], sizeof(float));
        }
        if (lod.indices) {
            lod.index = room(lod.indices, sizeof(uint32_t));
            ok = fread(lod.index, sizeof(uint32_t), lod.indices, f) == lod.indices;
        }
        ok = ok && fgetc(f) == EOF && levels_hold(&lod, vertices);
    }
    fclose(f);
    if (!ok) {
        free(lod.index);
        return false;
    }
    *out = lod;
    return true;
}

static void write_levels(const char *path, const mesh_lod *lod)
{
    char fresh[2400];
    if (snprintf(fresh, sizeof fresh, "%s.new", path) >= (int)sizeof fresh) return;
    FILE *f = fopen(fresh, "wb");
    if (!f) return; // Nothing's lost: they're made again
    uint32_t head[FILE_HEAD] = {FILE_MAGIC, FILE_VERSION};
    memcpy(&head[2], lod->center, sizeof lod->center);
    memcpy(&head[5], &lod->radius, sizeof lod->radius);
    head[6] = lod->levels;
    head[7] = lod->indices;
    for (uint32_t i = 0; i < MESH_LOD_MOST; i++) {
        head[8u + i * 3u] = lod->first[i];
        head[9u + i * 3u] = lod->count[i];
        memcpy(&head[10u + i * 3u], &lod->error[i], sizeof(float));
    }
    bool ok = fwrite(head, sizeof(uint32_t), FILE_HEAD, f) == FILE_HEAD;
    if (ok && lod->indices) ok = fwrite(lod->index, sizeof(uint32_t), lod->indices, f) == lod->indices;
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path); // Windows doesn't rename onto a file that's there
        ok = rename(fresh, path) == 0;
    }
    if (!ok) remove(fresh);
}

const mesh_lod *mesh_lod_cached(const uint8_t key[32], const mesh_lod_source *source, const char *game_folder)
{
    for (uint32_t i = 0; i < kept_count; i++) {
        if (memcmp(kept_levels[i].key, key, 32) == 0) return &kept_levels[i].lod;
    }
    char path[2300] = {0};
    if (game_folder) {
        char dir[2048];
        if (snprintf(dir, sizeof dir, "%s/.tide/assets/lod", game_folder) < (int)sizeof dir) {
            folder_make(dir);
            char hex[65];
            for (uint32_t i = 0; i < 32u; i++) snprintf(hex + i * 2u, 3, "%02x", key[i]);
            snprintf(path, sizeof path, "%s/%s.lod", dir, hex);
        }
    }
    mesh_lod lod;
    if (!*path || !read_levels(path, source->vertices, &lod)) {
        mesh_lod_make(source, &lod);
        made_count++;
        if (*path) write_levels(path, &lod);
    }
    if (kept_count == kept_room) {
        kept_room = kept_room ? kept_room * 2u : 16u;
        kept_levels = realloc(kept_levels, (size_t)kept_room * sizeof(kept));
        if (!kept_levels) abort();
    }
    kept *k = &kept_levels[kept_count++];
    memcpy(k->key, key, 32);
    k->lod = lod;
    return &k->lod;
}
