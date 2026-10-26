// Mesh files as `asset Mesh<V>` reads them (see mesh_files.h): glTF 2.0 and
// Wavefront OBJ, plus build-time FBX import through ufbx, with a JSON reader, a base64
// reader and a number reader of their own. A file is somebody else's bytes,
// and the language server reads one at every keystroke: every offset and
// count a file says is checked against what's there before it's used.

#include "mesh_files.h"

#include "fbx_files.h"

#include "assets.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/internal/count_text.h"
#include "tide/math.h"
#include "tide/sha256.h"

#pragma STDC FP_CONTRACT OFF

// What one mesh holds at the most (the limits, mesh_files.h), as the shared glTF readers name them.
#define VERTICES_MOST TIDE_MESH_MAX_VERTICES
#define INDICES_MOST TIDE_MESH_MAX_INDICES
#define INDICES_MOST_NAME "TIDE_MESH_MAX_INDICES"
#define READS_MOST TIDE_MESH_MAX_READS
#define READS_MOST_NAME "TIDE_MESH_MAX_READS"

// What's wrong with the file being read: said once, the first thing found.
typedef struct reader {
    char *why;
    char *note;
    bool failed;
    mesh_side_fn side;
    void *user;
} reader;

static bool fail(reader *r, const char *note, const char *fmt, ...)
{
    if (r->failed) return false;
    r->failed = true;
    va_list args;
    va_start(args, fmt);
    vsnprintf(r->why, 256, fmt, args);
    va_end(args);
    snprintf(r->note, 256, "%s", note ? note : "");
    return false;
}

// A limit of mesh_files.h refused the file. The message says what it has
// against the limit and ends with the limit's name, so a report says which:
// "has 20,000,000 vertices, more than the 16,777,216 one Mesh holds
// (TIDE_MESH_MAX_VERTICES)". `have` is UINT64_MAX where the count stopped at
// the limit.
static bool past_limit(reader *r, const char *name, const uint64_t most, const char *what, const uint64_t have)
{
    char counted[32], limit[32];
    const char *note = "a mesh that big is better split into several files, each a Mesh of its own";
    if (have == UINT64_MAX) {
        return fail(r, note, "has more %s than the %s one Mesh holds (%s)", what, tide_count_text(most, limit), name);
    }
    return fail(r, note, "has %s %s, more than the %s one Mesh holds (%s)", tide_count_text(have, counted), what,
                tide_count_text(most, limit), name);
}

static void *zeroed(const size_t count, const size_t size)
{
    void *p = calloc(count ? count : 1u, size);
    if (!p) abort();
    return p;
}

#include "tide/internal/gltf_json.h"
#include "tide/internal/gltf_extensions.h"

bool mesh_number(const char *s, const char *end, float *out)
{
    return gltf_number(s, end, out);
}

// ---------------------------------------------------------------------------
// The mesh being made: every part of the file after the last, as one.

typedef struct builder {
    mesh_file m;
    uint32_t vertex_room, index_room;
    reader *r;
} builder;

static bool more_vertices(builder *b, const uint64_t more)
{
    const uint64_t need = (uint64_t)b->m.vertices + more;
    if (need > VERTICES_MOST) return past_limit(b->r, "TIDE_MESH_MAX_VERTICES", VERTICES_MOST, "vertices", need);
    if (need <= b->vertex_room) return true;
    uint32_t room = b->vertex_room ? b->vertex_room : 256u;
    while (room < need) room *= 2u;
    b->m.position = realloc(b->m.position, (size_t)room * 3u * sizeof(float));
    b->m.normal = realloc(b->m.normal, (size_t)room * 3u * sizeof(float));
    b->m.uv = realloc(b->m.uv, (size_t)room * 2u * sizeof(float));
    b->m.uv2 = realloc(b->m.uv2, (size_t)room * 2u * sizeof(float));
    b->m.color = realloc(b->m.color, (size_t)room * 4u * sizeof(float));
    b->m.tangent = realloc(b->m.tangent, (size_t)room * 4u * sizeof(float));
    b->m.sources = realloc(b->m.sources, (size_t)room * sizeof *b->m.sources);
    if (!b->m.position || !b->m.normal || !b->m.uv || !b->m.uv2 || !b->m.color || !b->m.tangent || !b->m.sources) abort();
    b->vertex_room = room;
    return true;
}

static bool more_indices(builder *b, const uint64_t more)
{
    const uint64_t need = (uint64_t)b->m.indices + more;
    if (need > INDICES_MOST) return past_limit(b->r, "TIDE_MESH_MAX_INDICES", INDICES_MOST / 3u, "triangles", need / 3u);
    if (need <= b->index_room) return true;
    uint32_t room = b->index_room ? b->index_room : 512u;
    while (room < need) room *= 2u;
    b->m.index = realloc(b->m.index, (size_t)room * sizeof(uint32_t));
    if (!b->m.index) abort();
    b->index_room = room;
    return true;
}

// One part's vertices, as the file has them once they're in Tide's space.
typedef struct part {
    uint32_t count;
    float *position, *normal, *uv, *uv2, *color, *tangent; // normal, uv, uv2 and tangent NULL where the part has none
    uint32_t *index; // Its triangles, each wound clockwise from its front
    uint32_t indices;
} part;

static float length3(const float *v)
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// As long as 1, where it has a length. A zero comes out a plain one, whatever sign arithmetic left it.
static void normalize3(float *v)
{
    const float length = length3(v);
    if (length > 0.0f) {
        v[0] = v[0] / length + 0.0f;
        v[1] = v[1] / length + 0.0f;
        v[2] = v[2] / length + 0.0f;
    }
}

// Copies vertex `from` of the part to the mesh's vertex `to`, whose room is there.
static void put_vertex(builder *b, const part *p, const uint32_t from, const uint32_t to)
{
    mesh_file *m = &b->m;
    memcpy(m->position + (size_t)to * 3u, p->position + (size_t)from * 3u, 3u * sizeof(float));
    if (p->normal) memcpy(m->normal + (size_t)to * 3u, p->normal + (size_t)from * 3u, 3u * sizeof(float));
    else memset(m->normal + (size_t)to * 3u, 0, 3u * sizeof(float));
    if (p->uv) memcpy(m->uv + (size_t)to * 2u, p->uv + (size_t)from * 2u, 2u * sizeof(float));
    else memset(m->uv + (size_t)to * 2u, 0, 2u * sizeof(float));
    if (p->uv2) memcpy(m->uv2 + (size_t)to * 2u, p->uv2 + (size_t)from * 2u, 2u * sizeof(float));
    else memset(m->uv2 + (size_t)to * 2u, 0, 2u * sizeof(float));
    memcpy(m->color + (size_t)to * 4u, p->color + (size_t)from * 4u, 4u * sizeof(float));
    if (p->tangent) memcpy(m->tangent + (size_t)to * 4u, p->tangent + (size_t)from * 4u, 4u * sizeof(float));
    else memset(m->tangent + (size_t)to * 4u, 0, 4u * sizeof(float));
    b->m.sources[to] = (p->tangent ? 1u : 0u) | (p->uv ? 2u : 0u);
}

// Adds a part to the mesh. One with normals keeps its vertices as they are,
// shared between its triangles. One with none gets each triangle's own: three
// vertices a triangle, whose normal is the way its front faces, which is how
// the format says a mesh with no normals is drawn.
static bool add_part(builder *b, const part *p)
{
    mesh_file *m = &b->m;
    if (!p->indices) return true;
    if (!more_indices(b, p->indices)) return false;
    if (p->normal) {
        if (!more_vertices(b, p->count)) return false;
        for (uint32_t i = 0; i < p->count; i++) put_vertex(b, p, i, m->vertices + i);
        for (uint32_t i = 0; i < p->indices; i++) m->index[m->indices + i] = m->vertices + p->index[i];
        m->vertices += p->count;
    } else {
        if (!more_vertices(b, p->indices)) return false;
        for (uint32_t t = 0; t + 2u < p->indices; t += 3u) {
            const float *a = p->position + (size_t)p->index[t] * 3u;
            const float *c1 = p->position + (size_t)p->index[t + 1u] * 3u;
            const float *c2 = p->position + (size_t)p->index[t + 2u] * 3u;
            const float u[3] = {c1[0] - a[0], c1[1] - a[1], c1[2] - a[2]};
            const float v[3] = {c2[0] - a[0], c2[1] - a[1], c2[2] - a[2]};
            float facing[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
            normalize3(facing);
            for (uint32_t k = 0; k < 3u; k++) {
                const uint32_t to = m->vertices + t + k;
                put_vertex(b, p, p->index[t + k], to);
                memcpy(m->normal + (size_t)to * 3u, facing, sizeof facing);
                m->index[m->indices + t + k] = to;
            }
        }
        m->vertices += p->indices;
    }
    m->indices += p->indices;
    m->has_normals |= p->normal != NULL;
    m->has_uvs |= p->uv != NULL;
    m->has_uvs2 |= p->uv2 != NULL;
    m->has_tangents |= p->tangent != NULL;
    m->parts++;
    return true;
}

// A color a file means as light, as the screen shows it, which is what a Color is in Tide.
static float to_display(const float linear)
{
    if (!(linear > 0.0f)) return 0.0f;
    if (linear >= 1.0f) return 1.0f;
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * tide_pow_f(linear, 1.0f / 2.4f) - 0.055f;
}

static float unit(const float v)
{
    return !(v > 0.0f) ? 0.0f : v >= 1.0f ? 1.0f : v;
}

// Missing static tangents use the usual UV derivatives and Gram-Schmidt basis
// (https://terathon.com/blog/tangent-space.html), with corner-angle weighting.
// Authored streams keep their existing import/transform path. No vertices are
// welded: UV/normal/material seams already present in the source stay separate.
// Only opposite handedness at one shared vertex needs an additional vertex.
typedef struct tangent_sum {
    double direction[2][3];
    uint32_t split;
    uint8_t sides;
} tangent_sum;

static double tangent_dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool tangent_unit(double v[3])
{
    const double length = sqrt(tangent_dot(v, v));
    if (!(length > 0.0) || !isfinite(length)) return false;
    for (uint32_t k = 0; k < 3u; k++) v[k] /= length;
    return true;
}

static void tangent_normal(const mesh_file *m, const uint32_t vertex, double n[3])
{
    for (uint32_t k = 0; k < 3u; k++) n[k] = m->normal[(size_t)vertex * 3u + k];
    // A zero/invalid authored normal or a collapsed triangle has no plane.
    // Pick a fixed plane for its tangent without changing the normal stream.
    if (!tangent_unit(n)) n[0] = n[1] = 0.0, n[2] = 1.0;
}

static bool tangent_project(double t[3], const double n[3])
{
    const double before = tangent_dot(t, t);
    // A second projection removes cancellation residue before normalization.
    // Nearly parallel directions have no reliable tangent plane projection.
    for (uint32_t pass = 0; pass < 2u; pass++) {
        const double along = tangent_dot(n, t);
        for (uint32_t k = 0; k < 3u; k++) t[k] -= n[k] * along;
    }
    if (!(tangent_dot(t, t) > before * 1e-24)) return false;
    return tangent_unit(t);
}

// A triangle's U/V directions, and its geometric edges. Differences/products
// of finite floats fit in doubles, including very small UV/geometry scales;
// normalizing the signed numerators avoids division by a tiny UV determinant.
static bool tangent_triangle(const builder *b, const uint32_t at, double edge[3][3], double u[3], double v[3])
{
    const mesh_file *m = &b->m;
    const uint32_t *index = m->index + at;
    const float *position[3], *uv[3];
    for (uint32_t j = 0; j < 3u; j++) {
        if (!(b->m.sources[index[j]] & 2u)) return false;
        position[j] = m->position + (size_t)index[j] * 3u;
        uv[j] = m->uv + (size_t)index[j] * 2u;
        for (uint32_t k = 0; k < 3u; k++) if (!isfinite(position[j][k])) return false;
        for (uint32_t k = 0; k < 2u; k++) if (!isfinite(uv[j][k])) return false;
    }
    for (uint32_t k = 0; k < 3u; k++) {
        edge[0][k] = (double)position[1][k] - position[0][k];
        edge[1][k] = (double)position[2][k] - position[0][k];
        edge[2][k] = (double)position[2][k] - position[1][k];
    }
    double facing[3] = {edge[0][1] * edge[1][2] - edge[0][2] * edge[1][1],
                        edge[0][2] * edge[1][0] - edge[0][0] * edge[1][2],
                        edge[0][0] * edge[1][1] - edge[0][1] * edge[1][0]};
    if (!tangent_unit(facing)) return false;
    const double du1 = (double)uv[1][0] - uv[0][0], dv1 = (double)uv[1][1] - uv[0][1];
    const double du2 = (double)uv[2][0] - uv[0][0], dv2 = (double)uv[2][1] - uv[0][1];
    const double determinant = du1 * dv2 - du2 * dv1;
    if (determinant == 0.0) return false;
    const double side = determinant < 0.0 ? -1.0 : 1.0;
    for (uint32_t k = 0; k < 3u; k++) {
        u[k] = (edge[0][k] * dv2 - edge[1][k] * dv1) * side;
        v[k] = (edge[1][k] * du1 - edge[0][k] * du2) * side;
    }
    return tangent_unit(u) && tangent_unit(v);
}

static uint32_t tangent_side(const mesh_file *m, const uint32_t vertex, const double u[3], const double v[3], double t[3])
{
    double n[3];
    tangent_normal(m, vertex, n);
    memcpy(t, u, 3u * sizeof(double));
    if (!tangent_project(t, n)) return 0u;
    const double bitangent[3] = {n[1] * t[2] - n[2] * t[1], n[2] * t[0] - n[0] * t[2], n[0] * t[1] - n[1] * t[0]};
    const double side = tangent_dot(bitangent, v);
    return side < 0.0 ? 2u : side > 0.0 ? 1u : 0u;
}

static void tangent_write(mesh_file *m, const uint32_t vertex, const double direction[3], const uint32_t side)
{
    double n[3], t[3];
    tangent_normal(m, vertex, n);
    memcpy(t, direction, sizeof t);
    if (!tangent_project(t, n)) {
        // Project the least-aligned coordinate axis: a finite unit direction
        // even with no UVs, a collapsed mapping, or cancelling contributions.
        uint32_t axis = 0;
        if (fabs(n[1]) < fabs(n[axis])) axis = 1u;
        if (fabs(n[2]) < fabs(n[axis])) axis = 2u;
        t[0] = t[1] = t[2] = 0.0;
        t[axis] = 1.0;
        (void)tangent_project(t, n);
    }
    float *out = m->tangent + (size_t)vertex * 4u;
    for (uint32_t k = 0; k < 3u; k++) out[k] = (float)t[k] + 0.0f;
    out[3] = side == 2u ? -1.0f : 1.0f;
}

static double tangent_angle(const double a[3], const double b[3], const bool opposite)
{
    const double cross[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    const double sine = sqrt(tangent_dot(cross, cross));
    const double cosine = tangent_dot(a, b) * (opposite ? -1.0 : 1.0);
    // acos(float(dot)) loses narrow valid corners when the cosine rounds to
    // one. atan2 keeps their sine; below float's range the angle equals the
    // sine to much better than float precision, so retain that double weight.
    const double angle = tide_atan2_f((float)sine, (float)cosine);
    return angle > 0.0 ? angle : sine;
}

static bool make_tangents(builder *b)
{
    mesh_file *m = &b->m;
    const uint32_t vertices = m->vertices;
    bool missing = false;
    for (uint32_t i = 0; i < vertices; i++) missing |= !(b->m.sources[i] & 1u);
    if (!missing) return true;
    tangent_sum *sums = zeroed(vertices, sizeof *sums);
    for (uint32_t at = 0; at < m->indices; at += 3u) {
        if (b->m.sources[m->index[at]] & 1u) continue;
        double edge[3][3], u[3], v[3];
        if (!tangent_triangle(b, at, edge, u, v)) continue;
        for (uint32_t j = 0; j < 3u; j++) (void)tangent_unit(edge[j]);
        // At corners 0, 1, 2: edges (01,02), (-01,12), (-02,-12).
        for (uint32_t j = 0; j < 3u; j++) {
            const uint32_t vertex = m->index[at + j];
            double t[3];
            const uint32_t side = tangent_side(m, vertex, u, v, t);
            if (!side || (b->m.sources[vertex] & 1u)) continue;
            tangent_sum *sum = sums + vertex;
            sum->sides |= (uint8_t)side;
            const double weight = tangent_angle(edge[j == 2u ? 1u : 0u], edge[j == 0u ? 1u : 2u], j == 1u);
            for (uint32_t k = 0; k < 3u; k++) sum->direction[side - 1u][k] += t[k] * weight;
        }
    }
    uint32_t splits = 0;
    for (uint32_t i = 0; i < vertices; i++) splits += sums[i].sides == 3u;
    if (!more_vertices(b, splits)) {
        free(sums);
        return false;
    }
    for (uint32_t i = 0; i < vertices; i++) {
        if (b->m.sources[i] & 1u) continue;
        tangent_sum *sum = sums + i;
        if (sum->sides == 3u) {
            const uint32_t to = m->vertices++;
            sum->split = to;
            memcpy(m->position + (size_t)to * 3u, m->position + (size_t)i * 3u, 3u * sizeof(float));
            memcpy(m->normal + (size_t)to * 3u, m->normal + (size_t)i * 3u, 3u * sizeof(float));
            memcpy(m->uv + (size_t)to * 2u, m->uv + (size_t)i * 2u, 2u * sizeof(float));
            memcpy(m->uv2 + (size_t)to * 2u, m->uv2 + (size_t)i * 2u, 2u * sizeof(float));
            memcpy(m->color + (size_t)to * 4u, m->color + (size_t)i * 4u, 4u * sizeof(float));
            b->m.sources[to] = b->m.sources[i];
            tangent_write(m, to, sum->direction[1], 2u);
        }
        const uint32_t side = sum->sides == 2u ? 2u : 1u;
        tangent_write(m, i, sum->direction[side - 1u], side);
    }
    if (splits) {
        for (uint32_t at = 0; at < m->indices; at += 3u) {
            if (b->m.sources[m->index[at]] & 1u) continue;
            double edge[3][3], u[3], v[3];
            if (!tangent_triangle(b, at, edge, u, v)) continue;
            for (uint32_t j = 0; j < 3u; j++) {
                const uint32_t vertex = m->index[at + j];
                double t[3];
                if (sums[vertex].split && tangent_side(m, vertex, u, v, t) == 2u) m->index[at + j] = sums[vertex].split;
            }
        }
    }
    free(sums);
    return true;
}

static void finish(builder *b)
{
    mesh_file *m = &b->m;
    tide_sha256 s;
    tide_sha256_init(&s);
    const uint32_t counts[2] = {m->vertices, m->indices};
    tide_sha256_add(&s, counts, sizeof counts);
    tide_sha256_add(&s, m->position, (size_t)m->vertices * 3u * sizeof(float));
    tide_sha256_add(&s, m->normal, (size_t)m->vertices * 3u * sizeof(float));
    tide_sha256_add(&s, m->uv, (size_t)m->vertices * 2u * sizeof(float));
    tide_sha256_add(&s, m->uv2, (size_t)m->vertices * 2u * sizeof(float));
    tide_sha256_add(&s, m->color, (size_t)m->vertices * 4u * sizeof(float));
    tide_sha256_add(&s, m->tangent, (size_t)m->vertices * 4u * sizeof(float));
    tide_sha256_add(&s, m->index, (size_t)m->indices * sizeof(uint32_t));
    tide_sha256_end(&s, m->hash);
}

void mesh_file_free(mesh_file *m)
{
    free(m->position);
    free(m->normal);
    free(m->uv);
    free(m->uv2);
    free(m->color);
    free(m->tangent);
    free(m->index);
    free(m->sources);
    *m = (mesh_file){0};
}

// ---------------------------------------------------------------------------
// glTF 2.0

typedef struct gltf_buffer {
    const uint8_t *data;
    uint64_t size;
    uint8_t *own; // A data URI's bytes, decoded: freed with the reader
    bool tried;
} gltf_buffer;

typedef struct gltf {
    reader *r;
    builder *b;
    const jnode *root;
    const uint8_t *bin; // A .glb's own bytes, its first buffer's
    uint64_t bin_size;
    gltf_buffer *buffers;
    uint32_t buffer_count;
    bool *placed; // A node the scene has placed: a node is in it once
    uint32_t node_count;
    uint64_t reads; // Numbers read from accessors so far, against READS_MOST
} gltf;

#include "tide/internal/gltf_read.h"

typedef struct vertex_pick {
    uint32_t count;
    uint32_t *source; // Original accessor indices, in their original order
    uint32_t *place; // Original index to compact index + 1, or 0 for an unused vertex
} vertex_pick;

static bool read_component(gltf *g, const uint8_t *bytes, const accessor *a, float *out, const char *what)
{
    if (a->type == 5126) {
        uint32_t bits;
        memcpy(&bits, bytes, sizeof bits);
        if ((bits & 0x7f800000u) == 0x7f800000u) return fail(g->r, NULL, "has non-finite %s", what);
    }
    *out = component(bytes, a->type, a->normalized);
    return true;
}

// The selected accessor elements as floats, `want` numbers each into `out`:
// the numbers it has, and `fill` for a fourth it hasn't (a color's alpha).
// The whole accessor still has to have `expect` elements and fit its views.
static bool read_floats(gltf *g, const jnode *index, const uint32_t want, const float fill, const uint64_t expect, float *out,
                        const vertex_pick *pick, const char *what)
{
    accessor a;
    if (!accessor_of(g, index, &a, what)) return false;
    if (a.count != expect) {
        return fail(g->r, NULL, "has %llu %s for %llu positions: a mesh has one of each for every vertex", (unsigned long long)a.count,
                    what, (unsigned long long)expect);
    }
    if (a.width != want && !(want == 4u && fill == 1.0f && a.width == 3u && strcmp(what, "colors") == 0)) {
        return fail(g->r, NULL, "has %s with the wrong number of components", what);
    }
    if (!spend_reads(g, (uint64_t)pick->count * want)) return false;
    const uint32_t each = component_bytes(a.type);
    const uint32_t element = each * a.width;
    const uint32_t take = a.width < want ? a.width : want;
    for (uint32_t i = 0; i < pick->count; i++) {
        for (uint32_t c = 0; c < want; c++) out[i * want + c] = c == 3u ? fill : 0.0f;
    }
    if (a.view >= 0 && a.count) {
        uint64_t room, stride;
        const uint8_t *at = view_at(g, a.view, a.offset, &room, &stride);
        if (!at) return false;
        if (!stride) stride = element;
        if (stride < element || (a.count - 1u) * stride + element > room) {
            return fail(g->r, NULL, "has %s that run past their buffer view's end", what);
        }
        for (uint32_t i = 0; i < pick->count; i++) {
            const uint64_t from = pick->source[i];
            for (uint32_t c = 0; c < take; c++) {
                if (!read_component(g, at + from * stride + (uint64_t)c * each, &a, &out[(uint64_t)i * want + c], what)) return false;
            }
        }
    }
    // A sparse accessor: some elements, by their numbers, given apart from the rest
    sparse_accessor sparse;
    if (!sparse_of(g, &a, &sparse, what)) return false;
    for (uint64_t k = 0; k < sparse.count; k++) {
        const uint32_t i = whole_component(sparse.indices + k * sparse.index_bytes, sparse.index_type);
        if (i >= a.count) return fail(g->r, NULL, "has sparse %s for a vertex it hasn't got", what);
        if (!pick->place[i]) continue;
        const uint64_t to = pick->place[i] - 1u;
        for (uint32_t c = 0; c < take; c++) {
            if (!read_component(g, sparse.values + k * sparse.element_bytes + (uint64_t)c * each, &a, &out[to * want + c], what)) return false;
        }
    }
    return true;
}


// 4 by 4 matrices as the format writes them: a column after another.
static void multiply(const float a[16], const float b[16], float out[16])
{
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) sum += a[k * 4 + row] * b[column * 4 + k];
            out[column * 4 + row] = sum;
        }
    }
}

// A node's own transform: its matrix, or its translation, rotation and scale.
static void node_matrix(const jnode *node, float out[16])
{
    if (jfloats(jget(node, "matrix"), 16u, out)) return;
    float t[3] = {0.0f, 0.0f, 0.0f}, q[4] = {0.0f, 0.0f, 0.0f, 1.0f}, s[3] = {1.0f, 1.0f, 1.0f};
    float read[4];
    if (jfloats(jget(node, "translation"), 3u, read)) memcpy(t, read, sizeof t);
    if (jfloats(jget(node, "rotation"), 4u, read)) memcpy(q, read, sizeof q);
    if (jfloats(jget(node, "scale"), 3u, read)) memcpy(s, read, sizeof s);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float turned[9] = {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w),        2.0f * (x * z - y * w),
                             2.0f * (x * y - z * w),        1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w),
                             2.0f * (x * z + y * w),        2.0f * (y * z - x * w),        1.0f - 2.0f * (x * x + y * y)};
    for (int column = 0; column < 3; column++) {
        for (int row = 0; row < 3; row++) out[column * 4 + row] = turned[column * 3 + row] * s[column];
        out[column * 4 + 3] = 0.0f;
    }
    out[12] = t[0], out[13] = t[1], out[14] = t[2], out[15] = 1.0f;
}

// Adds one primitive of a mesh, placed by `world`.
static bool add_primitive(gltf *g, const jnode *primitive, const float world[16])
{
    reader *r = g->r;
    const int64_t mode = jwhole_or(primitive, "mode", 4);
    if (mode >= 0 && mode < 4) return true; // Points and lines: nothing a Mesh draws
    if (mode < 4 || mode > 6) return fail(r, NULL, "has an invalid primitive mode");
    // Optional Draco data has ordinary accessors as a core fallback.
    const jnode *attributes = jget(primitive, "attributes");
    const jnode *positions = jget(attributes, "POSITION");
    if (!positions) return fail(r, NULL, "has a triangle primitive with no POSITION attribute");
    accessor counted;
    if (!accessor_of(g, positions, &counted, "positions")) return false;
    const uint64_t count = counted.count;
    if (count > VERTICES_MOST) return past_limit(r, "TIDE_MESH_MAX_VERTICES", VERTICES_MOST, "vertices in a part", count);
    part p = {0};
    vertex_pick pick = {0};
    uint32_t *listed = NULL;
    bool ok = true;
    // Its triangles: the corners its indices name, or every three vertices in turn.
    // Pick only the vertices complete triangles use. A shared accessor can cover
    // all of a model's materials, while this primitive uses a small part of it.
    uint64_t listed_count = count;
    // What's made and walked for the accessor's every vertex below, whichever the triangles use
    if (!spend_reads(g, count)) return false;
    if (jget(primitive, "indices")) {
        listed = read_indices(g, jget(primitive, "indices"), &listed_count);
        ok = listed != NULL;
    } else {
        listed = zeroed((size_t)count, sizeof *listed);
        for (uint64_t i = 0; i < count; i++) listed[i] = (uint32_t)i;
    }
    for (uint64_t i = 0; ok && i < listed_count; i++) {
        if (listed[i] >= count) ok = fail(r, NULL, "has a triangle with a corner it has no vertex for");
    }
    const uint64_t triangles = !ok ? 0u : mode == 4 ? listed_count / 3u : listed_count >= 3u ? listed_count - 2u : 0u;
    if (ok && triangles * 3u > INDICES_MOST) {
        ok = past_limit(r, "TIDE_MESH_MAX_INDICES", INDICES_MOST / 3u, "triangles in a part", triangles);
    }
    if (ok) {
        const uint64_t used = mode == 4 ? triangles * 3u : triangles ? listed_count : 0u;
        pick.place = zeroed((size_t)count, sizeof *pick.place);
        for (uint64_t i = 0; i < used; i++) pick.place[listed[i]] = 1u;
        for (uint64_t i = 0; i < count; i++) pick.count += pick.place[i];
        pick.source = zeroed(pick.count, sizeof *pick.source);
        uint32_t next = 0;
        for (uint64_t i = 0; i < count; i++) {
            if (!pick.place[i]) continue;
            pick.source[next] = (uint32_t)i;
            pick.place[i] = ++next;
        }
        for (uint64_t i = 0; i < used; i++) listed[i] = pick.place[listed[i]] - 1u;
        p.count = pick.count;
        p.position = zeroed(p.count, 3u * sizeof(float));
        p.color = zeroed(p.count, 4u * sizeof(float));
        ok = read_floats(g, positions, 3u, 0.0f, count, p.position, &pick, "positions");
    }
    const jnode *normals = jget(attributes, "NORMAL"), *tangents = jget(attributes, "TANGENT");
    const jnode *uvs = jget(attributes, "TEXCOORD_0"), *uvs2 = jget(attributes, "TEXCOORD_1"), *colors = jget(attributes, "COLOR_0");
    if (ok && normals) {
        p.normal = zeroed(p.count, 3u * sizeof(float));
        ok = read_floats(g, normals, 3u, 0.0f, count, p.normal, &pick, "normals");
    }
    if (ok && tangents) {
        p.tangent = zeroed(p.count, 4u * sizeof(float));
        ok = read_floats(g, tangents, 4u, 1.0f, count, p.tangent, &pick, "tangents");
    }
    if (ok && uvs) {
        p.uv = zeroed(p.count, 2u * sizeof(float));
        ok = read_floats(g, uvs, 2u, 0.0f, count, p.uv, &pick, "uvs");
    }
    if (ok && uvs2) {
        p.uv2 = zeroed(p.count, 2u * sizeof(float));
        ok = read_floats(g, uvs2, 2u, 0.0f, count, p.uv2, &pick, "second uvs");
    }
    if (ok && colors) {
        ok = read_floats(g, colors, 4u, 1.0f, count, p.color, &pick, "colors");
        g->b->m.has_colors = true;
    } else {
        for (uint64_t i = 0; i < (uint64_t)p.count * 4u; i++) p.color[i] = 1.0f;
    }
    if (ok && triangles) {
        // The part of the node's matrix that turns and scales, and whether it mirrors
        const float *m = world;
        const float cofactor[9] = {m[5] * m[10] - m[6] * m[9], m[6] * m[8] - m[4] * m[10], m[4] * m[9] - m[5] * m[8],
                                   m[2] * m[9] - m[1] * m[10], m[0] * m[10] - m[2] * m[8], m[1] * m[8] - m[0] * m[9],
                                   m[1] * m[6] - m[2] * m[5],  m[2] * m[4] - m[0] * m[6],  m[0] * m[5] - m[1] * m[4]};
        const float determinant = m[0] * cofactor[0] + m[1] * cofactor[1] + m[2] * cofactor[2];
        const bool mirrors = determinant < 0.0f;
        const float side = mirrors ? -1.0f : 1.0f;
        // The material's color, which a color at each vertex is times
        float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        const jnode *material = jitem(jget(g->root, "materials"), jwhole_or(primitive, "material", -1));
        if (material) {
            float read[4];
            if (jfloats(jget(jget(material, "pbrMetallicRoughness"), "baseColorFactor"), 4u, read)) memcpy(tint, read, sizeof tint);
            g->b->m.has_colors = true;
        }
        for (uint32_t i = 0; i < p.count; i++) {
            float *at = p.position + i * 3u;
            const float x = at[0], y = at[1], z = at[2];
            // Through the node, then x turned around: the file's world is right-handed, and Tide's isn't
            at[0] = 0.0f - (m[0] * x + m[4] * y + m[8] * z + m[12]);
            at[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
            at[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
            if (p.normal) { // A normal goes through the matrix's inverse, transposed: its cofactors, by the determinant's sign
                float *n = p.normal + i * 3u;
                const float nx = n[0], ny = n[1], nz = n[2];
                n[0] = 0.0f - side * (cofactor[0] * nx + cofactor[3] * ny + cofactor[6] * nz);
                n[1] = side * (cofactor[1] * nx + cofactor[4] * ny + cofactor[7] * nz);
                n[2] = side * (cofactor[2] * nx + cofactor[5] * ny + cofactor[8] * nz);
                normalize3(n);
            }
            if (p.tangent) {
                float *t = p.tangent + i * 4u;
                const float tx = t[0], ty = t[1], tz = t[2];
                t[0] = 0.0f - (m[0] * tx + m[4] * ty + m[8] * tz);
                t[1] = m[1] * tx + m[5] * ty + m[9] * tz;
                t[2] = m[2] * tx + m[6] * ty + m[10] * tz;
                normalize3(t);
                t[3] = 0.0f - side * t[3]; // A mirror turns the bitangent's side around, and x turned is one more
            }
            float *c = p.color + i * 4u;
            for (int k = 0; k < 3; k++) c[k] = to_display(c[k] * tint[k]);
            c[3] = unit(c[3] * tint[3]);
        }
        // Triangles, each from its front: the format's fronts go counterclockwise in a right-handed world, which
        // x turned around makes clockwise in Tide's as they are, unless the node mirrors too
        p.index = zeroed((size_t)triangles * 3u, sizeof *p.index);
        for (uint64_t t = 0; t < triangles; t++) {
            uint32_t a, b, c;
            if (mode == 4) {
                a = listed[t * 3u], b = listed[t * 3u + 1u], c = listed[t * 3u + 2u];
            } else if (mode == 5) { // A strip: every other triangle is wound the other way
                a = listed[t], b = listed[t + 1u + (t & 1u)], c = listed[t + 2u - (t & 1u)];
            } else { // A fan, around its first vertex
                a = listed[0], b = listed[t + 1u], c = listed[t + 2u];
            }
            p.index[t * 3u] = a;
            p.index[t * 3u + 1u] = mirrors ? b : c;
            p.index[t * 3u + 2u] = mirrors ? c : b;
        }
        p.indices = (uint32_t)(triangles * 3u);
        ok = add_part(g->b, &p);
    }
    free(listed);
    free(pick.source);
    free(pick.place);
    free(p.index);
    free(p.position);
    free(p.normal);
    free(p.uv);
    free(p.uv2);
    free(p.color);
    free(p.tangent);
    return ok;
}

static bool place_node(gltf *g, const int64_t index, const float parent[16], const int depth)
{
    const jnode *node = jitem(jget(g->root, "nodes"), index);
    if (!node) return fail(g->r, NULL, "names a node it hasn't got");
    if (g->placed[index] || depth > 256) return fail(g->r, NULL, "has a node that's in its scene twice, or inside itself");
    g->placed[index] = true;
    float own[16], world[16];
    node_matrix(node, own);
    multiply(parent, own, world);
    int64_t mesh;
    if (jwhole(jget(node, "mesh"), &mesh)) {
        const jnode *primitives = jget(jitem(jget(g->root, "meshes"), mesh), "primitives");
        if (!primitives) return fail(g->r, NULL, "names a mesh it hasn't got");
        for (uint32_t i = 0; i < jcount(primitives); i++) {
            if (!add_primitive(g, primitives->items[i], world)) return false;
        }
    }
    const jnode *children = jget(node, "children");
    for (uint32_t i = 0; i < jcount(children); i++) {
        int64_t child;
        if (!jwhole(children->items[i], &child)) return fail(g->r, NULL, "names a node it hasn't got");
        if (!place_node(g, child, world, depth + 1)) return false;
    }
    return true;
}

static bool read_gltf(reader *r, builder *b, const char *text, const size_t length, const uint8_t *bin, const uint64_t bin_size)
{
    jparser j = {.p = text, .end = text + length, .values_left = TIDE_MESH_MAX_JSON_VALUES};
    const jnode *root = jvalue(&j, 0);
    jspace(&j);
    gltf g = {.r = r, .b = b, .root = root, .bin = bin, .bin_size = bin_size};
    bool ok = true;
    if (j.too_many) {
        ok = past_limit(r, "TIDE_MESH_MAX_JSON_VALUES", TIDE_MESH_MAX_JSON_VALUES, "JSON values in its description", UINT64_MAX);
    }
    if (ok && (!root || j.bad || j.p != j.end || root->kind != J_OBJECT)) {
        ok = fail(r, NULL, "isn't a glTF that reads: its description isn't JSON");
    }
    if (ok) {
        const jnode *version = jget(jget(root, "asset"), "version");
        if (!version || version->kind != J_STRING || strcmp(version->text, "2.0") != 0) {
            ok = fail(r, "export it as glTF 2.0, which is what tools write today",
                      "is glTF %.20s, and Tide reads glTF 2.0", version && version->kind == J_STRING ? version->text : "of no version");
        }
    }
    const jnode *required = ok ? jget(root, "extensionsRequired") : NULL;
    if (required && required->kind != J_ARRAY) ok = fail(r, NULL, "has extensionsRequired that isn't an array");
    for (uint32_t i = 0; ok && i < jcount(required); i++) {
        const jnode *extension = required->items[i];
        char why[256], note[256];
        if (extension->kind != J_STRING) ok = fail(r, NULL, "has a required glTF extension that isn't text");
        else if (gltf_required_extension_error(extension->text, why, note)) ok = fail(r, note, "%s", why);
    }
    if (ok) {
        g.buffer_count = jcount(jget(root, "buffers"));
        g.buffers = zeroed(g.buffer_count, sizeof *g.buffers);
        g.node_count = jcount(jget(root, "nodes"));
        g.placed = zeroed(g.node_count, sizeof *g.placed);
        static const float identity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        const jnode *scenes = jget(root, "scenes");
        const jnode *nodes = jget(root, "nodes");
        if (jcount(scenes)) { // The scene it opens with, or its first
            const jnode *roots = jget(jitem(scenes, jwhole_or(root, "scene", 0)), "nodes");
            for (uint32_t i = 0; ok && i < jcount(roots); i++) {
                int64_t node;
                ok = jwhole(roots->items[i], &node) ? place_node(&g, node, identity, 0) : fail(r, NULL, "names a node it hasn't got");
            }
        } else if (g.node_count) { // No scene: every node that's nobody's child
            bool *child = zeroed(g.node_count, sizeof *child);
            for (uint32_t i = 0; i < g.node_count; i++) {
                const jnode *children = jget(nodes->items[i], "children");
                for (uint32_t k = 0; k < jcount(children); k++) {
                    int64_t at;
                    if (jwhole(children->items[k], &at) && at >= 0 && at < (int64_t)g.node_count) child[at] = true;
                }
            }
            for (uint32_t i = 0; ok && i < g.node_count; i++) {
                if (!child[i]) ok = place_node(&g, i, identity, 0);
            }
            free(child);
        } else { // No nodes either: its meshes as they are
            const jnode *meshes = jget(root, "meshes");
            for (uint32_t i = 0; ok && i < jcount(meshes); i++) {
                const jnode *primitives = jget(meshes->items[i], "primitives");
                for (uint32_t k = 0; ok && k < jcount(primitives); k++) ok = add_primitive(&g, primitives->items[k], identity);
            }
        }
    }
    for (uint32_t i = 0; i < g.buffer_count; i++) free(g.buffers[i].own);
    free(g.buffers);
    free(g.placed);
    jfree(&j);
    return ok && !r->failed;
}

// A .glb: a header, the description as JSON, and the bytes of its first buffer.
static bool read_glb(reader *r, builder *b, const uint8_t *data, const size_t size)
{
    uint32_t header[3];
    if (size < 20u) return fail(r, NULL, "isn't a .glb that reads: it ends before its header does");
    memcpy(header, data, sizeof header);
    if (header[0] != 0x46546c67u) return fail(r, "export a GLB 2 file, or use .gltf for a JSON glTF file", "has an invalid GLB header: its magic isn't glTF");
    if (header[1] != 2u) {
        return fail(r, "export it as glTF 2.0, which is what tools write today", "is a .glb of version %u, and Tide reads glTF 2.0",
                    (unsigned)header[1]);
    }
    if (header[2] != size) return fail(r, NULL, "isn't a .glb that reads: its declared length doesn't match its bytes");
    const uint8_t *json = NULL, *bin = NULL;
    uint32_t json_size = 0, bin_size = 0;
    for (uint64_t at = 12u; at < size;) {
        if (size - at < 8u) return fail(r, NULL, "isn't a .glb that reads: it ends inside a chunk header");
        uint32_t chunk[2];
        memcpy(chunk, data + at, sizeof chunk);
        if ((uint64_t)chunk[0] > header[2] - at - 8u) return fail(r, NULL, "isn't a .glb that reads: a chunk runs past its end");
        if (chunk[0] & 3u) return fail(r, NULL, "isn't a .glb that reads: a chunk lacks four-byte alignment");
        if (at == 12u && chunk[1] != 0x4E4F534Au) return fail(r, NULL, "isn't a .glb that reads: its first chunk isn't JSON");
        if (chunk[1] == 0x4E4F534Au) {
            if (json) return fail(r, NULL, "isn't a .glb that reads: it has more than one JSON chunk");
            json = data + at + 8u;
            json_size = chunk[0];
        } else if (chunk[1] == 0x004E4942u) {
            if (bin) return fail(r, NULL, "isn't a .glb that reads: it has more than one binary chunk");
            bin = data + at + 8u;
            bin_size = chunk[0];
        }
        at += 8u + chunk[0];
    }
    if (!json) return fail(r, NULL, "isn't a .glb that reads: it has no description");
    return read_gltf(r, b, (const char *)json, json_size, bin, bin_size);
}

// ---------------------------------------------------------------------------
// Wavefront OBJ: text, a line a vertex, a normal, a uv or a face. A face's
// corners name one of each by its number, so a vertex of the mesh is each
// combination a face uses, once.

typedef struct floats {
    float *items;
    uint32_t count, room;
} floats;

// Most numbers a list keeps: the room doubles up to it and no further. Each takes a line of a file.
#define FLOATS_MOST (1u << 30)

// False for a list that would have more than FLOATS_MOST.
static bool push_floats(floats *list, const float *values, const uint32_t count)
{
    if ((uint64_t)list->count + count > FLOATS_MOST) return false;
    if (list->count + count > list->room) {
        list->room = list->room ? list->room * 2u : 1024u;
        while (list->room < list->count + count) list->room *= 2u;
        list->items = realloc(list->items, (size_t)list->room * sizeof(float));
        if (!list->items) abort();
    }
    memcpy(list->items + list->count, values, (size_t)count * sizeof(float));
    list->count += count;
    return true;
}

// A corner as a face names it: a position, a uv and a normal by their places, from 0, or -1 for none.
typedef struct corner {
    int32_t position, uv, normal;
} corner;

typedef struct obj {
    reader *r;
    builder *b;
    floats positions, colors, uvs, normals; // As the file has them: 3, 3, 2 and 3 floats each
    bool colored; // Some vertex line had a color after its position
    // The mesh's vertices so far by the corner each is: a table of a power of two of places, under half taken
    corner *made;
    uint32_t *made_vertex;
    uint32_t made_count, made_places;
} obj;

static uint32_t corner_hash(const corner c)
{
    uint32_t h = (uint32_t)c.position * 0x9E3779B1u;
    h = (h ^ (uint32_t)c.uv) * 0x85EBCA77u;
    h = (h ^ (uint32_t)c.normal) * 0xC2B2AE3Du;
    return h ^ h >> 15;
}

// A corner's values, in Tide's space: x turned around, and a uv counted from the top.
static void corner_values(const obj *o, const corner c, float position[3], float normal[3], float uv[2], float color[4])
{
    const float *p = o->positions.items + (size_t)c.position * 3u;
    position[0] = 0.0f - p[0], position[1] = p[1], position[2] = p[2];
    normal[0] = normal[1] = normal[2] = 0.0f;
    if (c.normal >= 0) {
        const float *n = o->normals.items + (size_t)c.normal * 3u;
        normal[0] = 0.0f - n[0], normal[1] = n[1], normal[2] = n[2];
        normalize3(normal);
    }
    uv[0] = uv[1] = 0.0f;
    if (c.uv >= 0) {
        const float *t = o->uvs.items + (size_t)c.uv * 2u;
        uv[0] = t[0], uv[1] = 1.0f - t[1];
    }
    const float *tint = o->colors.items + (size_t)c.position * 3u;
    color[0] = unit(tint[0]), color[1] = unit(tint[1]), color[2] = unit(tint[2]), color[3] = 1.0f;
}

// A new vertex of the mesh for a corner: its number, or UINT32_MAX when the mesh is full (which is said).
static uint32_t new_vertex(obj *o, const corner c, const float *flat)
{
    builder *b = o->b;
    if (!more_vertices(b, 1u)) return UINT32_MAX;
    const uint32_t at = b->m.vertices++;
    float uv[2];
    corner_values(o, c, b->m.position + (size_t)at * 3u, b->m.normal + (size_t)at * 3u, uv, b->m.color + (size_t)at * 4u);
    if (flat) memcpy(b->m.normal + (size_t)at * 3u, flat, 3u * sizeof(float));
    memcpy(b->m.uv + (size_t)at * 2u, uv, sizeof uv);
    memset(b->m.uv2 + (size_t)at * 2u, 0, 2u * sizeof(float));
    memset(b->m.tangent + (size_t)at * 4u, 0, 4u * sizeof(float));
    b->m.sources[at] = c.uv >= 0 ? 2u : 0u;
    return at;
}

// The mesh's vertex for a corner that has a normal: the one made for it before, or a new one.
static uint32_t shared_vertex(obj *o, const corner c)
{
    if ((o->made_count + 1u) * 2u > o->made_places) {
        const uint32_t places = o->made_places ? o->made_places * 2u : 1024u;
        corner *made = zeroed(places, sizeof *made);
        uint32_t *vertex = zeroed(places, sizeof *vertex);
        for (uint32_t i = 0; i < o->made_places; i++) {
            if (!o->made_vertex[i]) continue;
            uint32_t at = corner_hash(o->made[i]) & (places - 1u);
            while (vertex[at]) at = (at + 1u) & (places - 1u);
            made[at] = o->made[i];
            vertex[at] = o->made_vertex[i];
        }
        free(o->made);
        free(o->made_vertex);
        o->made = made;
        o->made_vertex = vertex;
        o->made_places = places;
    }
    uint32_t at = corner_hash(c) & (o->made_places - 1u);
    while (o->made_vertex[at]) {
        const corner had = o->made[at];
        if (had.position == c.position && had.uv == c.uv && had.normal == c.normal) return o->made_vertex[at] - 1u;
        at = (at + 1u) & (o->made_places - 1u);
    }
    const uint32_t vertex = new_vertex(o, c, NULL);
    if (vertex == UINT32_MAX) return UINT32_MAX;
    o->made[at] = c;
    o->made_vertex[at] = vertex + 1u;
    o->made_count++;
    return vertex;
}

// A triangle of a face, its corners in the file's order, which goes counterclockwise from its front.
static bool add_triangle(obj *o, const corner a, const corner b, const corner c)
{
    builder *out = o->b;
    if (!more_indices(out, 3u)) return false;
    // Clockwise from its front, as Tide winds one: with x turned around, that's the file's order with two swapped
    const corner wound[3] = {a, c, b};
    uint32_t vertex[3];
    if (a.normal >= 0 && b.normal >= 0 && c.normal >= 0) {
        for (int k = 0; k < 3; k++) {
            vertex[k] = shared_vertex(o, wound[k]);
            if (vertex[k] == UINT32_MAX) return false;
        }
    } else { // No normal said: the triangle's own, for three vertices of its own
        float at[3][3], unused_normal[3], unused_uv[2], unused_color[4];
        for (int k = 0; k < 3; k++) corner_values(o, wound[k], at[k], unused_normal, unused_uv, unused_color);
        const float u[3] = {at[1][0] - at[0][0], at[1][1] - at[0][1], at[1][2] - at[0][2]};
        const float v[3] = {at[2][0] - at[0][0], at[2][1] - at[0][1], at[2][2] - at[0][2]};
        float facing[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        normalize3(facing);
        for (int k = 0; k < 3; k++) {
            vertex[k] = new_vertex(o, wound[k], facing);
            if (vertex[k] == UINT32_MAX) return false;
        }
    }
    for (int k = 0; k < 3; k++) out->m.index[out->m.indices++] = vertex[k];
    return true;
}

static bool is_blank(const char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

// The next word of a line, from `*p`: where it starts and ends, or false at the line's end.
static bool next_word(const char **p, const char *end, const char **from, const char **to)
{
    while (*p < end && is_blank(**p)) (*p)++;
    if (*p >= end) return false;
    *from = *p;
    while (*p < end && !is_blank(**p)) (*p)++;
    *to = *p;
    return true;
}

// A place a face names, from 1, or from the end with a minus, as a place from 0: -1 for none written, and -2
// for one that isn't a number or that names nothing there is.
static int32_t place_named(const char *from, const char *to, const uint32_t there)
{
    if (from == to) return -1;
    bool back = false;
    if (*from == '-') back = true, from++;
    int64_t n = 0;
    if (from == to) return -2;
    for (; from < to; from++) {
        if (*from < '0' || *from > '9' || n > INT32_MAX) return -2;
        n = n * 10 + (*from - '0');
    }
    const int64_t at = back ? (int64_t)there - n : n - 1;
    return at >= 0 && at < (int64_t)there ? (int32_t)at : -2;
}

static bool read_obj(reader *r, builder *b, const char *text, const size_t length)
{
    obj o = {.r = r, .b = b};
    const char *end = text + length;
    unsigned long long line_number = 0;
    bool ok = true;
    corner *face = NULL;
    uint32_t face_room = 0;
    for (const char *line = text; ok && line < end;) {
        const char *line_end = memchr(line, '\n', (size_t)(end - line));
        if (!line_end) line_end = end;
        line_number++;
        const char *p = line, *from, *to;
        line = line_end < end ? line_end + 1 : end;
        if (!next_word(&p, line_end, &from, &to)) continue;
        const size_t word = (size_t)(to - from);
        const bool position = word == 1u && from[0] == 'v';
        const bool normal = word == 2u && from[0] == 'v' && from[1] == 'n';
        const bool uv = word == 2u && from[0] == 'v' && from[1] == 't';
        if (position || normal || uv) {
            float numbers[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            uint32_t count = 0;
            while (ok && count < 7u && next_word(&p, line_end, &from, &to) && *from != '#') {
                if (!mesh_number(from, to, &numbers[count++])) {
                    ok = fail(r, NULL, "isn't an OBJ that reads: line %llu has '%.*s' where a number goes", line_number,
                              (int)(to - from > 40 ? 40 : to - from), from);
                }
            }
            if (ok && count < (uv ? 1u : 3u)) {
                ok = fail(r, NULL, "isn't an OBJ that reads: line %llu has %u numbers, and a %s has %s", line_number, (unsigned)count,
                          uv ? "uv" : position ? "vertex" : "normal", uv ? "one or two" : "three");
            }
            if (!ok) break;
            if (position) {
                // A color after the position, which some tools write: x y z r g b, or with a weight before it
                static const float white[3] = {1.0f, 1.0f, 1.0f};
                ok = push_floats(&o.positions, numbers, 3u)
                  && push_floats(&o.colors, count >= 6u ? numbers + (count == 7u ? 4 : 3) : white, 3u);
                o.colored |= count >= 6u;
            } else if (normal) {
                ok = push_floats(&o.normals, numbers, 3u);
            } else {
                ok = push_floats(&o.uvs, numbers, 2u);
            }
            if (!ok) {
                ok = fail(r, "a mesh that big is better split into several files",
                          "isn't an OBJ Tide reads: line %llu has more vertex, normal or uv lines before it than a mesh file holds", line_number);
            }
            continue;
        }
        if (word != 1u || from[0] != 'f') continue; // Groups, materials, lines and the rest: nothing of a Mesh's
        uint32_t corners = 0;
        while (ok && next_word(&p, line_end, &from, &to) && *from != '#') {
            // v, v/vt, v//vn or v/vt/vn
            const char *first = memchr(from, '/', (size_t)(to - from));
            const char *second = first ? memchr(first + 1, '/', (size_t)(to - first - 1)) : NULL;
            const corner c = {place_named(from, first ? first : to, o.positions.count / 3u),
                              place_named(first ? first + 1 : to, second ? second : to, o.uvs.count / 2u),
                              place_named(second ? second + 1 : to, to, o.normals.count / 3u)};
            if (c.position < 0 || c.uv == -2 || c.normal == -2) {
                ok = fail(r, NULL, "isn't an OBJ that reads: the face on line %llu names '%.*s', and there's no such vertex, uv or "
                          "normal before it", line_number, (int)(to - from > 40 ? 40 : to - from), from);
                break;
            }
            if (corners == face_room) {
                face_room = face_room ? face_room * 2u : 16u;
                face = realloc(face, (size_t)face_room * sizeof *face);
                if (!face) abort();
            }
            face[corners++] = c;
        }
        // A face of more than three corners is a fan around its first
        for (uint32_t k = 1; ok && k + 1u < corners; k++) ok = add_triangle(&o, face[0], face[k], face[k + 1u]);
    }
    if (ok) {
        b->m.has_normals = o.normals.count > 0;
        b->m.has_uvs = o.uvs.count > 0;
        b->m.has_colors = o.colored;
        b->m.parts = b->m.indices ? 1u : 0u;
    }
    free(face);
    free(o.positions.items);
    free(o.colors.items);
    free(o.uvs.items);
    free(o.normals.items);
    free(o.made);
    free(o.made_vertex);
    return ok && !r->failed;
}

// ---------------------------------------------------------------------------

static bool ends_with(const char *name, const char *extension)
{
    size_t n = strlen(name);
    if (asset_path_is_url(name, n)) {
        // Only the URL's path names its format. Its query and fragment still
        // belong to the complete URL used for fetching, caching and map keys.
        const char *authority = strstr(name, "://") + 3;
        const char *path = authority + strcspn(authority, "/?#");
        if (*path != '/') return false;
        n = (size_t)(path - name) + strcspn(path, "?#");
    }
    const size_t e = strlen(extension);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++) {
        const char c = name[n - e + i];
        if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != extension[i]) return false;
    }
    return true;
}

bool mesh_file_read(const uint8_t *data, const size_t size, const char *name, const mesh_side_fn side, void *user, mesh_file *out,
                    char why[256], char note[256])
{
    why[0] = note[0] = '\0';
    reader r = {.why = why, .note = note, .side = side, .user = user};
    builder b = {.r = &r};
    more_vertices(&b, 1u); // So every part is there to hash and to free, whatever is read
    more_indices(&b, 1u);
    bool ok;
    if (size >= 4u && memcmp(data, "glTF", 4) == 0) {
        ok = read_glb(&r, &b, data, size);
    } else if (fbx_file_is(data, size, name)) {
        uint8_t *glb = NULL;
        size_t glb_size = 0;
        ok = fbx_file_to_glb(data, size, false, &glb, &glb_size, why, note);
        if (ok) ok = read_glb(&r, &b, glb, glb_size);
        free(glb);
    } else if (ends_with(name, ".glb")) {
        ok = read_glb(&r, &b, data, size);
    } else if (ends_with(name, ".gltf")) {
        ok = read_gltf(&r, &b, (const char *)data, size, NULL, 0);
    } else if (ends_with(name, ".obj")) {
        ok = read_obj(&r, &b, (const char *)data, size);
    } else {
        ok = fail(&r, "a Mesh asset is glTF (.glb or .gltf), Wavefront .obj, or FBX (.fbx): export it as one of those",
                  "isn't a mesh file Tide reads");
    }
    if (ok && !b.m.indices) ok = fail(&r, NULL, "has no triangles: there's nothing in it to draw");
    if (!ok) {
        mesh_file_free(&b.m);
        return false;
    }
    finish(&b);
    *out = b.m;
    return true;
}

bool mesh_file_with_tangents(const mesh_file *source, mesh_file *out, char why[256], char note[256])
{
    why[0] = note[0] = '\0';
    *out = (mesh_file){0};
    reader r = {.why = why, .note = note};
    builder b = {.m = *source, .r = &r};
    b.m.position = b.m.normal = b.m.uv = b.m.uv2 = b.m.color = b.m.tangent = NULL;
    b.m.index = NULL;
    b.m.sources = NULL;
    if (!source->sources) return fail(&r, NULL, "isn't a canonical static mesh import for tangent generation");
    if (!more_vertices(&b, 0u) || !more_indices(&b, 0u)) {
        mesh_file_free(&b.m);
        return false;
    }
    memcpy(b.m.position, source->position, (size_t)source->vertices * 3u * sizeof(float));
    memcpy(b.m.normal, source->normal, (size_t)source->vertices * 3u * sizeof(float));
    memcpy(b.m.uv, source->uv, (size_t)source->vertices * 2u * sizeof(float));
    memcpy(b.m.uv2, source->uv2, (size_t)source->vertices * 2u * sizeof(float));
    memcpy(b.m.color, source->color, (size_t)source->vertices * 4u * sizeof(float));
    memcpy(b.m.tangent, source->tangent, (size_t)source->vertices * 4u * sizeof(float));
    memcpy(b.m.index, source->index, (size_t)source->indices * sizeof(uint32_t));
    memcpy(b.m.sources, source->sources, (size_t)source->vertices * sizeof *source->sources);
    if (!make_tangents(&b)) {
        mesh_file_free(&b.m);
        return false;
    }
    finish(&b);
    *out = b.m;
    return true;
}
