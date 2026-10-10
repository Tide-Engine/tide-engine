#include "tide/skinned_data.h"

#include "tide/math.h"
#include "tide/page.h"
#include "tide/sha256.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

#define VERTICES_MOST (1u << 24)
#define INDICES_MOST (3u << 25)
#define DECODE_BYTES_MOST (512u << 20)
#define NODES_MOST 65536u

typedef struct reader {
    char *why, *note;
    bool failed, runtime;
    tide_skinned_side_fn side;
    void *user;
} reader;

static bool fail(reader *r, const char *note, const char *fmt, ...)
{
    if (!r->failed) {
        r->failed = true;
        va_list args;
        va_start(args, fmt);
        vsnprintf(r->why, 256u, fmt, args);
        va_end(args);
        snprintf(r->note, 256u, "%s", note ? note : "");
    }
    return false;
}

static void *zeroed(size_t count, size_t size)
{
    return tide_alloc_zeroed(count ? count : 1u, size);
}

#include "tide/internal/gltf_json.h"
#include "tide/internal/gltf_extensions.h"

typedef struct gltf_buffer {
    const uint8_t *data;
    uint8_t *own;
    uint64_t size;
    bool tried;
} gltf_buffer;

typedef struct gltf {
    reader *r;
    const jnode *root;
    const uint8_t *bin;
    uint64_t bin_size;
    gltf_buffer *buffers;
    uint32_t buffer_count;
} gltf;

#define GLTF_ALLOW_MATRICES
#include "tide/internal/gltf_read.h"
#undef GLTF_ALLOW_MATRICES

typedef struct skin {
    uint32_t first, count;
} skin;

typedef struct decoded {
    gltf *g;
    tide_skinned_node_data *nodes;
    uint32_t node_count, source_nodes;
    int32_t *parents;
    uint32_t *remap;
    uint8_t *visiting;
    float *world;
    skin *skins;
    uint32_t skin_count;
    tide_skinned_palette_data *palettes;
    uint32_t palette_count, palette_room;
    tide_skinned_vertex_data *vertices;
    uint32_t vertex_count, vertex_room;
    tide_skinned_influence_data *influences;
    uint32_t influence_count, influence_room;
    uint32_t *indices;
    uint32_t index_count, index_room;
    tide_skinned_clip_data *clips;
    uint32_t clip_count;
    tide_skinned_channel_data *channels;
    uint32_t channel_count, channel_room;
    float *keys;
    uint32_t key_count, key_room;
    uint32_t flags;
} decoded;

static bool grow(reader *r, void **data, uint32_t *room, uint64_t count, size_t each)
{
    if (count > UINT32_MAX || count > DECODE_BYTES_MOST / each) return fail(r, NULL, "needs more than 512 MiB of decoded model data");
    if (count <= *room) return true;
    uint64_t next = *room ? (uint64_t)*room * 2u : 16u;
    if (next < count) next = count;
    if (next > DECODE_BYTES_MOST / each) next = count;
    void *p = tide_realloc(*data, (size_t)*room * each, (size_t)next * each);
    *data = p;
    *room = (uint32_t)next;
    return true;
}

static uint32_t read_word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool floating(gltf *g, const uint8_t *p, int64_t type, bool normalized, float *out)
{
    if (type == 5126 && (read_word(p) & 0x7f800000u) == 0x7f800000u) {
        return fail(g->r, NULL, "has a non-finite accessor value");
    }
    *out = component(p, type, normalized);
    return true;
}

// Full accessor reads are used for skeleton and animation data as well as
// geometry. Matrix accessors have float components, so their columns need
// no byte/short padding rules.
static bool floats(gltf *g, const jnode *index, uint32_t want, uint64_t count, float *out, bool color, const char *what)
{
    accessor a;
    if (!accessor_of(g, index, &a, what)) return false;
    if (a.count != count || (a.width != want && !(color && a.width == 3u && want == 4u))) {
        return fail(g->r, NULL, "has %s of the wrong count or width", what);
    }
    if (a.width == 16u && a.type != 5126) return fail(g->r, NULL, "has inverse bind matrices that aren't floats");
    for (uint64_t i = 0; i < count * want; i++) out[i] = color && i % want == 3u ? 1.0f : 0.0f;
    const uint32_t each = component_bytes(a.type), element = each * a.width;
    if (a.view >= 0 && count) {
        uint64_t room, stride;
        const uint8_t *at = view_at(g, a.view, a.offset, &room, &stride);
        if (!at) return false;
        if (!stride) stride = element;
        if (stride < element || (count - 1u) * stride + element > room) return fail(g->r, NULL, "has %s past its buffer view's end", what);
        for (uint64_t i = 0; i < count; i++) {
            for (uint32_t c = 0; c < a.width; c++) {
                if (!floating(g, at + i * stride + (uint64_t)c * each, a.type, a.normalized, out + i * want + c)) return false;
            }
        }
    }
    sparse_accessor sparse;
    if (!sparse_of(g, &a, &sparse, what)) return false;
    uint32_t previous = 0;
    for (uint64_t k = 0; k < sparse.count; k++) {
        const uint32_t i = whole_component(sparse.indices + k * sparse.index_bytes, sparse.index_type);
        if (i >= count || (k && i <= previous)) return fail(g->r, NULL, "has invalid or unordered sparse %s selectors", what);
        previous = i;
        for (uint32_t c = 0; c < a.width; c++) {
            if (!floating(g, sparse.values + k * sparse.element_bytes + (uint64_t)c * each, a.type, a.normalized,
                          out + (uint64_t)i * want + c)) return false;
        }
    }
    return true;
}

static bool json_floats(reader *r, const jnode *array, uint32_t count, float *out, const char *what)
{
    if (!jfloats(array, count, out)) return fail(r, NULL, "has a %s of the wrong width", what);
    for (uint32_t i = 0; i < count; i++) {
        double d;
        if (!parse_double(array->items[i]->text, array->items[i]->text + array->items[i]->length, &d)
            || !(d >= -FLT_MAX && d <= FLT_MAX)) return fail(r, NULL, "has a non-finite %s", what);
    }
    return true;
}

static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static void multiply(const float a[16], const float b[16], float out[16])
{
    for (uint32_t c = 0; c < 4u; c++) {
        for (uint32_t r = 0; r < 4u; r++) {
            float sum = 0.0f;
            for (uint32_t k = 0; k < 4u; k++) sum += a[k * 4u + r] * b[c * 4u + k];
            out[c * 4u + r] = sum;
        }
    }
}

static bool normalize(float *v, uint32_t count)
{
    float squared = 0.0f;
    for (uint32_t i = 0; i < count; i++) squared += v[i] * v[i];
    if (!(squared > 0.0f) || squared > FLT_MAX) return false;
    const float length = sqrtf(squared);
    for (uint32_t i = 0; i < count; i++) v[i] = v[i] / length + 0.0f;
    return true;
}

static void trs(const tide_skinned_node_data *n, float out[16])
{
    const float x = n->rotation[0], y = n->rotation[1], z = n->rotation[2], w = n->rotation[3];
    const float rotation[9] = {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w), 2.0f * (x * z - y * w),
        2.0f * (x * y - z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w),
        2.0f * (x * z + y * w), 2.0f * (y * z - x * w), 1.0f - 2.0f * (x * x + y * y)};
    for (uint32_t c = 0; c < 3u; c++) {
        for (uint32_t r = 0; r < 3u; r++) out[c * 4u + r] = rotation[c * 3u + r] * n->scale[c];
        out[c * 4u + 3u] = 0.0f;
    }
    memcpy(out + 12, n->position, sizeof n->position);
    out[15] = 1.0f;
}

static bool decompose(reader *r, const float m[16], tide_skinned_node_data *n)
{
    if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f || m[15] != 1.0f) return fail(r, NULL, "has a node matrix that isn't affine");
    memcpy(n->position, m + 12, sizeof n->position);
    float a[9];
    for (uint32_t c = 0; c < 3u; c++) {
        n->scale[c] = sqrtf(m[c * 4u] * m[c * 4u] + m[c * 4u + 1u] * m[c * 4u + 1u] + m[c * 4u + 2u] * m[c * 4u + 2u]);
        if (!(n->scale[c] > 0.0f) || n->scale[c] > FLT_MAX) {
            return fail(r, "write this node with translation, rotation and scale instead", "has a singular node matrix whose rotation can't be recovered");
        }
        for (uint32_t row = 0; row < 3u; row++) a[c * 3u + row] = m[c * 4u + row] / n->scale[c];
    }
    const float determinant = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[3] * (a[1] * a[8] - a[2] * a[7])
                            + a[6] * (a[1] * a[5] - a[2] * a[4]);
    if (determinant < 0.0f) {
        n->scale[0] = 0.0f - n->scale[0];
        for (uint32_t i = 0; i < 3u; i++) a[i] = 0.0f - a[i];
    }
    for (uint32_t c = 0; c < 3u; c++) {
        for (uint32_t k = c + 1u; k < 3u; k++) {
            const float dot = a[c * 3u] * a[k * 3u] + a[c * 3u + 1u] * a[k * 3u + 1u] + a[c * 3u + 2u] * a[k * 3u + 2u];
            if (dot < -0.0001f || dot > 0.0001f) return fail(r, NULL, "has a node matrix with shear, which isn't translation, rotation and scale");
        }
    }
    const float trace = a[0] + a[4] + a[8];
    float *q = n->rotation;
    if (trace > 0.0f) {
        const float s = 2.0f * sqrtf(trace + 1.0f);
        q[0] = (a[5] - a[7]) / s, q[1] = (a[6] - a[2]) / s, q[2] = (a[1] - a[3]) / s, q[3] = s * 0.25f;
    } else {
        const uint32_t i = a[0] > a[4] ? (a[0] > a[8] ? 0u : 2u) : (a[4] > a[8] ? 1u : 2u);
        const uint32_t j = (i + 1u) % 3u, k = (i + 2u) % 3u;
        const float s = 2.0f * sqrtf(1.0f + a[i * 3u + i] - a[j * 3u + j] - a[k * 3u + k]);
        if (!(s > 0.0f)) return fail(r, NULL, "has a node matrix with no valid rotation");
        q[i] = s * 0.25f;
        q[j] = (a[j * 3u + i] + a[i * 3u + j]) / s;
        q[k] = (a[k * 3u + i] + a[i * 3u + k]) / s;
        q[3] = (a[j * 3u + k] - a[k * 3u + j]) / s;
    }
    return normalize(q, 4u) || fail(r, NULL, "has a node matrix with no valid rotation");
}

static bool node_transform(reader *r, const jnode *json, tide_skinned_node_data *n)
{
    n->rotation[3] = 1.0f;
    n->scale[0] = n->scale[1] = n->scale[2] = 1.0f;
    const jnode *matrix = jget(json, "matrix");
    if (matrix) {
        if (jget(json, "translation") || jget(json, "rotation") || jget(json, "scale")) return fail(r, NULL, "has a node with both a matrix and translation, rotation or scale");
        float m[16];
        if (!json_floats(r, matrix, 16u, m, "node matrix")) return false;
        for (uint32_t c = 0; c < 4u; c++) {
            for (uint32_t row = 0; row < 4u; row++) if ((c == 0u) != (row == 0u)) m[c * 4u + row] = 0.0f - m[c * 4u + row];
        }
        return decompose(r, m, n);
    }
    const jnode *position = jget(json, "translation"), *rotation = jget(json, "rotation"), *scale = jget(json, "scale");
    if (position && !json_floats(r, position, 3u, n->position, "node translation")) return false;
    if (rotation && !json_floats(r, rotation, 4u, n->rotation, "node rotation")) return false;
    if (scale && !json_floats(r, scale, 3u, n->scale, "node scale")) return false;
    if (!normalize(n->rotation, 4u)) return fail(r, NULL, "has a node rotation with no valid quaternion");
    n->position[0] = 0.0f - n->position[0];
    n->rotation[1] = 0.0f - n->rotation[1], n->rotation[2] = 0.0f - n->rotation[2];
    return true;
}

static bool order_node(decoded *d, uint32_t source, uint32_t depth)
{
    if (d->visiting[source] == 2u) return true;
    if (d->visiting[source] == 1u || depth > 256u) return fail(d->g->r, NULL, "has a node inside itself, or a hierarchy deeper than 256 nodes");
    d->visiting[source] = 1u;
    const int32_t parent = d->parents[source];
    if (parent >= 0 && !order_node(d, (uint32_t)parent, depth + 1u)) return false;
    const uint32_t index = d->node_count++;
    d->remap[source] = index;
    tide_skinned_node_data *n = d->nodes + index;
    n->parent = parent < 0 ? 0 : (int32_t)d->remap[parent];
    n->source_node = source;
    const jnode *json = jitem(jget(d->g->root, "nodes"), source), *named = jget(json, "name");
    n->name = named && named->kind == J_STRING ? named->text : "";
    n->name_length = named && named->kind == J_STRING ? named->length : 0u;
    if (!node_transform(d->g->r, json, n)) return false;
    float own[16];
    trs(n, own);
    multiply(d->world + (size_t)n->parent * 16u, own, d->world + (size_t)index * 16u);
    d->visiting[source] = 2u;
    return true;
}

static bool read_nodes(decoded *d)
{
    const jnode *nodes = jget(d->g->root, "nodes");
    d->source_nodes = jcount(nodes);
    if (d->source_nodes > NODES_MOST) return fail(d->g->r, NULL, "has more than 65536 skeleton nodes");
    d->nodes = zeroed((size_t)d->source_nodes + 1u, sizeof *d->nodes);
    d->parents = zeroed(d->source_nodes, sizeof *d->parents);
    d->remap = zeroed(d->source_nodes, sizeof *d->remap);
    d->visiting = zeroed(d->source_nodes, sizeof *d->visiting);
    d->world = zeroed((size_t)d->source_nodes + 1u, 16u * sizeof *d->world);
    d->nodes[0] = (tide_skinned_node_data){.parent = -1, .source_node = UINT32_MAX, .name = "", .rotation = {0, 0, 0, 1}, .scale = {1, 1, 1}};
    memcpy(d->world, identity, sizeof identity);
    d->node_count = 1u;
    for (uint32_t i = 0; i < d->source_nodes; i++) d->parents[i] = -1;
    for (uint32_t i = 0; i < d->source_nodes; i++) {
        const jnode *children = jget(nodes->items[i], "children");
        for (uint32_t k = 0; k < jcount(children); k++) {
            int64_t child;
            if (!jwhole(children->items[k], &child) || child >= d->source_nodes || d->parents[child] >= 0) {
                return fail(d->g->r, NULL, "has a child node that is missing or has several parents");
            }
            d->parents[child] = (int32_t)i;
        }
    }
    for (uint32_t i = 0; i < d->source_nodes; i++) if (!order_node(d, i, 0u)) return false;
    return true;
}

static bool palette(decoded *d, uint32_t node, const float matrix[16], uint32_t *index)
{
    if (!grow(d->g->r, (void **)&d->palettes, &d->palette_room, (uint64_t)d->palette_count + 1u, sizeof *d->palettes)) return false;
    *index = d->palette_count++;
    d->palettes[*index].node = node;
    memcpy(d->palettes[*index].inverse_bind, matrix, 16u * sizeof(float));
    return true;
}

static bool read_skins(decoded *d)
{
    const jnode *skins = jget(d->g->root, "skins");
    d->skin_count = jcount(skins);
    if (d->skin_count > NODES_MOST) return fail(d->g->r, NULL, "has more than 65536 skins");
    d->skins = zeroed(d->skin_count, sizeof *d->skins);
    for (uint32_t i = 0; i < d->skin_count; i++) {
        const jnode *json = skins->items[i], *joints = jget(json, "joints");
        skin *s = d->skins + i;
        s->first = d->palette_count, s->count = jcount(joints);
        if (!s->count || s->count > d->source_nodes) return fail(d->g->r, NULL, "has a skin with no joints or more joints than nodes");
        float *matrices = zeroed(s->count, 16u * sizeof(float));
        const jnode *inverse = jget(json, "inverseBindMatrices");
        bool ok = true;
        if (inverse) {
            accessor a;
            ok = accessor_of(d->g, inverse, &a, "inverse bind matrices");
            if (ok && (a.count < s->count || a.count > NODES_MOST)) ok = fail(d->g->r, NULL, "has too few or too many inverse bind matrices");
            if (ok && a.count != s->count) {
                free(matrices);
                matrices = zeroed((size_t)a.count, 16u * sizeof(float));
            }
            if (ok) ok = floats(d->g, inverse, 16u, a.count, matrices, false, "inverse bind matrices");
        } else {
            for (uint32_t k = 0; k < s->count; k++) memcpy(matrices + (size_t)k * 16u, identity, sizeof identity);
        }
        uint8_t *used = zeroed(d->source_nodes, 1u);
        for (uint32_t k = 0; ok && k < s->count; k++) {
            int64_t node;
            if (!jwhole(joints->items[k], &node) || node >= d->source_nodes || used[node]) {
                ok = fail(d->g->r, NULL, "has a skin joint that is missing or repeated");
                break;
            }
            used[node] = 1u;
            float *m = matrices + (size_t)k * 16u;
            if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f || m[15] != 1.0f) {
                ok = fail(d->g->r, NULL, "has an inverse bind matrix that isn't affine");
                break;
            }
            for (uint32_t c = 0; c < 4u; c++) {
                for (uint32_t row = 0; row < 4u; row++) if ((c == 0u) != (row == 0u)) m[c * 4u + row] = 0.0f - m[c * 4u + row];
            }
            uint32_t unused;
            ok = palette(d, d->remap[node], m, &unused);
        }
        free(used);
        free(matrices);
        if (!ok) return false;
    }
    return true;
}

static float display(float linear)
{
    if (!(linear > 0.0f)) return 0.0f;
    if (linear >= 1.0f) return 1.0f;
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * tide_pow_f(linear, 1.0f / 2.4f) - 0.055f;
}

static bool influence(decoded *d, uint32_t id, float weight)
{
    if (!grow(d->g->r, (void **)&d->influences, &d->influence_room, (uint64_t)d->influence_count + 1u, sizeof *d->influences)) return false;
    d->influences[d->influence_count++] = (tide_skinned_influence_data){id, weight};
    return true;
}

static bool skin_weights(decoded *d, const jnode *attributes, const skin *s, uint32_t count, tide_skinned_vertex_data *vertices)
{
    uint32_t sets = 0u, weight_sets = 0u;
    if (attributes && attributes->kind == J_OBJECT) {
        for (uint32_t i = 0; i < attributes->count; i++) {
            if (strncmp(attributes->keys[i], "JOINTS_", 7u) == 0) sets++;
            if (strncmp(attributes->keys[i], "WEIGHTS_", 8u) == 0) weight_sets++;
        }
    }
    if (!sets || sets != weight_sets || sets > DECODE_BYTES_MOST / (sizeof(float) * 8u)) {
        return fail(d->g->r, NULL, "has a skinned primitive without matching joint and weight sets");
    }
    if ((uint64_t)count * sets > DECODE_BYTES_MOST / (8u * sizeof(float))) {
        return fail(d->g->r, NULL, "needs more than 512 MiB of vertex influence data");
    }
    float *joints = zeroed((size_t)count * sets, 4u * sizeof(float));
    float *weights = zeroed((size_t)count * sets, 4u * sizeof(float));
    bool ok = true;
    for (uint32_t k = 0; ok && k < sets; k++) {
        char joint_name[32], weight_name[32];
        snprintf(joint_name, sizeof joint_name, "JOINTS_%u", k);
        snprintf(weight_name, sizeof weight_name, "WEIGHTS_%u", k);
        const jnode *ji = jget(attributes, joint_name), *wi = jget(attributes, weight_name);
        accessor ja, wa;
        ok = accessor_of(d->g, ji, &ja, "joint indices") && accessor_of(d->g, wi, &wa, "joint weights");
        if (ok && (ja.normalized || (ja.type != 5121 && ja.type != 5123)
            || (wa.type != 5126 && !((wa.type == 5121 || wa.type == 5123) && wa.normalized)))) {
            ok = fail(d->g->r, NULL, "has joint indices or weights of an invalid component type");
        }
        if (ok) ok = floats(d->g, ji, 4u, count, joints + (size_t)k * count * 4u, false, "joint indices")
                  && floats(d->g, wi, 4u, count, weights + (size_t)k * count * 4u, false, "joint weights");
    }
    for (uint32_t i = 0; ok && i < count; i++) {
        tide_skinned_vertex_data *v = vertices + i;
        v->first_influence = d->influence_count;
        float sum = 0.0f;
        for (uint32_t k = 0; ok && k < sets; k++) {
            for (uint32_t c = 0; c < 4u; c++) {
                const size_t at = ((size_t)k * count + i) * 4u + c;
                const float joint = joints[at], weight = weights[at];
                if (!(joint >= 0.0f && joint < (float)s->count) || weight < 0.0f) {
                    ok = fail(d->g->r, NULL, "has a joint index outside its skin, or a negative weight");
                    break;
                }
                if (weight > 0.0f) {
                    sum += weight;
                    if (!influence(d, s->first + (uint32_t)joint, weight)) { ok = false; break; }
                }
            }
        }
        v->influences = d->influence_count - v->first_influence;
        if (ok && (!(sum > 0.0f) || sum > FLT_MAX)) ok = fail(d->g->r, NULL, "has a vertex with zero or non-finite total joint weight");
        for (uint32_t k = 0; ok && k < v->influences; k++) d->influences[v->first_influence + k].weight /= sum;
    }
    free(joints);
    free(weights);
    return ok;
}

static bool primitive(decoded *d, const jnode *json, uint32_t node, int64_t skin_index)
{
    gltf *g = d->g;
    const int64_t mode = jwhole_or(json, "mode", 4);
    if (mode >= 0 && mode < 4) return true;
    if (mode > 6 || mode < 0) return fail(g->r, NULL, "has a primitive mode glTF doesn't define");
    // Optional Draco data is ignored in favor of its core glTF accessors.
    if (jcount(jget(json, "targets"))) return fail(g->r, "export the model without morph targets", "has morph targets, which skinned models don't read yet");
    const jnode *attributes = jget(json, "attributes"), *position = jget(attributes, "POSITION");
    accessor counted;
    if (!accessor_of(g, position, &counted, "positions")) return false;
    if (!counted.count || counted.count > VERTICES_MOST || counted.count > DECODE_BYTES_MOST / sizeof(tide_skinned_vertex_data)) {
        return fail(g->r, NULL, "has no positions, or more positions than a decoded model holds");
    }
    const uint32_t count = (uint32_t)counted.count;
    tide_skinned_vertex_data *vertices = zeroed(count, sizeof *vertices);
    float *stream = zeroed(count, 4u * sizeof *stream);
    bool ok = floats(g, position, 3u, count, stream, false, "positions");
    for (uint32_t i = 0; ok && i < count; i++) {
        memcpy(vertices[i].position, stream + (size_t)i * 3u, sizeof vertices[i].position);
        vertices[i].position[0] = 0.0f - vertices[i].position[0];
        for (uint32_t c = 0; c < 4u; c++) vertices[i].color[c] = 1.0f;
    }
    static const struct { const char *name; uint32_t width, flag; } fields[] = {
        {"NORMAL", 3u, TIDE_SKINNED_HAS_NORMALS}, {"TEXCOORD_0", 2u, TIDE_SKINNED_HAS_UVS},
        {"TEXCOORD_1", 2u, TIDE_SKINNED_HAS_UVS2}, {"COLOR_0", 4u, TIDE_SKINNED_HAS_COLORS},
        {"TANGENT", 4u, TIDE_SKINNED_HAS_TANGENTS}
    };
    const bool has_normal = jget(attributes, "NORMAL") != NULL;
    for (uint32_t k = 0; ok && k < sizeof fields / sizeof fields[0]; k++) {
        const jnode *attribute = jget(attributes, fields[k].name);
        if (!attribute) continue;
        ok = floats(g, attribute, fields[k].width, count, stream, k == 3u, fields[k].name);
        if (!ok) break;
        d->flags |= fields[k].flag;
        for (uint32_t i = 0; i < count; i++) {
            float *to = k == 0u ? vertices[i].normal : k == 1u ? vertices[i].uv : k == 2u ? vertices[i].uv2
                      : k == 3u ? vertices[i].color : vertices[i].tangent;
            memcpy(to, stream + (size_t)i * fields[k].width, fields[k].width * sizeof(float));
            if (k == 0u || k == 4u) {
                to[0] = 0.0f - to[0];
                normalize(to, 3u);
                if (k == 4u) to[3] = 0.0f - to[3];
            }
        }
    }
    float tint[4] = {1, 1, 1, 1};
    const jnode *material = jitem(jget(g->root, "materials"), jwhole_or(json, "material", -1));
    const jnode *factor = jget(jget(material, "pbrMetallicRoughness"), "baseColorFactor");
    if (ok && factor) {
        ok = json_floats(g->r, factor, 4u, tint, "base color factor");
        d->flags |= TIDE_SKINNED_HAS_COLORS;
    }
    for (uint32_t i = 0; ok && i < count; i++) {
        for (uint32_t c = 0; c < 3u; c++) vertices[i].color[c] = display(vertices[i].color[c] * tint[c]);
        const float alpha = vertices[i].color[3] * tint[3];
        vertices[i].color[3] = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
    }
    if (ok && skin_index >= 0) {
        if (skin_index >= d->skin_count) ok = fail(g->r, NULL, "names a skin it hasn't got");
        else ok = skin_weights(d, attributes, d->skins + skin_index, count, vertices);
    } else if (ok) {
        uint32_t id;
        ok = palette(d, node, identity, &id);
        const uint32_t first = d->influence_count;
        if (ok) ok = influence(d, id, 1.0f);
        for (uint32_t i = 0; ok && i < count; i++) vertices[i].first_influence = first, vertices[i].influences = 1u;
    }
    uint64_t listed_count = count;
    uint32_t *listed = NULL;
    if (ok && jget(json, "indices")) {
        listed = read_indices(g, jget(json, "indices"), &listed_count);
        ok = listed != NULL;
    } else if (ok) {
        listed = zeroed(count, sizeof *listed);
        for (uint32_t i = 0; i < count; i++) listed[i] = i;
    }
    for (uint64_t i = 0; ok && i < listed_count; i++) {
        if (listed[i] >= count) ok = fail(g->r, NULL, "has a triangle index past its positions");
    }
    if (ok && mode == 4 && listed_count % 3u) ok = fail(g->r, NULL, "has a triangle primitive with an incomplete triangle");
    const uint64_t triangles = mode == 4 ? listed_count / 3u : listed_count < 3u ? 0u : listed_count - 2u;
    const uint64_t new_indices = triangles * 3u;
    const uint64_t new_vertices = has_normal ? count : new_indices;
    if (ok && ((uint64_t)d->index_count + new_indices > INDICES_MOST || (uint64_t)d->vertex_count + new_vertices > VERTICES_MOST)) {
        ok = fail(g->r, NULL, "has more vertices or triangles than a decoded model holds");
    }
    if (ok) ok = grow(g->r, (void **)&d->vertices, &d->vertex_room, (uint64_t)d->vertex_count + new_vertices, sizeof *d->vertices)
              && grow(g->r, (void **)&d->indices, &d->index_room, (uint64_t)d->index_count + new_indices, sizeof *d->indices);
    bool mirrors = false;
    if (skin_index < 0) {
        const float *m = d->world + (size_t)node * 16u;
        const float determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9])
                                + m[8] * (m[1] * m[6] - m[2] * m[5]);
        mirrors = determinant < 0.0f;
    }
    if (ok && has_normal) memcpy(d->vertices + d->vertex_count, vertices, (size_t)count * sizeof *vertices);
    for (uint64_t t = 0; ok && t < triangles; t++) {
        uint32_t corners[3];
        if (mode == 4) corners[0] = listed[t * 3u], corners[1] = listed[t * 3u + 1u], corners[2] = listed[t * 3u + 2u];
        else if (mode == 5) corners[0] = listed[t], corners[1] = listed[t + 1u + (t & 1u)], corners[2] = listed[t + 2u - (t & 1u)];
        else corners[0] = listed[0], corners[1] = listed[t + 1u], corners[2] = listed[t + 2u];
        if (!mirrors) { const uint32_t swap = corners[1]; corners[1] = corners[2]; corners[2] = swap; }
        float normal[3] = {0};
        if (!has_normal) {
            const float *a = vertices[corners[0]].position, *b = vertices[corners[1]].position, *c = vertices[corners[2]].position;
            const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            normal[0] = u[1] * v[2] - u[2] * v[1], normal[1] = u[2] * v[0] - u[0] * v[2], normal[2] = u[0] * v[1] - u[1] * v[0];
            normalize(normal, 3u);
        }
        for (uint32_t k = 0; k < 3u; k++) {
            const uint32_t index = has_normal ? d->vertex_count + corners[k] : d->vertex_count + (uint32_t)t * 3u + k;
            d->indices[d->index_count + (uint32_t)t * 3u + k] = index;
            if (!has_normal) {
                d->vertices[index] = vertices[corners[k]];
                memcpy(d->vertices[index].normal, normal, sizeof normal);
            }
        }
    }
    if (ok) d->vertex_count += (uint32_t)new_vertices, d->index_count += (uint32_t)new_indices;
    free(listed);
    free(stream);
    free(vertices);
    return ok;
}

static bool place_geometry(decoded *d, uint32_t source, uint8_t *placed, uint32_t depth)
{
    if (source >= d->source_nodes || placed[source] || depth > 256u) return fail(d->g->r, NULL, "has a missing, repeated or recursive scene node");
    placed[source] = 1u;
    const jnode *json = jitem(jget(d->g->root, "nodes"), source);
    const jnode *mesh = jget(json, "mesh");
    if (mesh) {
        int64_t index;
        const jnode *model = jwhole(mesh, &index) ? jitem(jget(d->g->root, "meshes"), index) : NULL;
        const jnode *primitives = jget(model, "primitives");
        if (!jcount(primitives)) return fail(d->g->r, NULL, "names a mesh with no primitives");
        const int64_t skin_index = jwhole_or(json, "skin", -1);
        for (uint32_t k = 0; k < jcount(primitives); k++) {
            if (!primitive(d, primitives->items[k], d->remap[source], skin_index)) return false;
        }
    }
    const jnode *children = jget(json, "children");
    for (uint32_t k = 0; k < jcount(children); k++) {
        int64_t child;
        if (!jwhole(children->items[k], &child) || child >= d->source_nodes
            || !place_geometry(d, (uint32_t)child, placed, depth + 1u)) return false;
    }
    return true;
}

static bool read_geometry(decoded *d)
{
    const jnode *scenes = jget(d->g->root, "scenes");
    uint8_t *placed = zeroed(d->source_nodes, 1u);
    bool ok = true;
    if (jcount(scenes)) {
        const int64_t scene = jwhole_or(d->g->root, "scene", 0);
        const jnode *selected = jitem(scenes, scene), *roots = jget(selected, "nodes");
        if (!selected) ok = fail(d->g->r, NULL, "names a default scene it hasn't got");
        for (uint32_t i = 0; ok && i < jcount(roots); i++) {
            int64_t node;
            if (!jwhole(roots->items[i], &node) || node >= d->source_nodes) ok = fail(d->g->r, NULL, "names a scene node it hasn't got");
            else ok = place_geometry(d, (uint32_t)node, placed, 0u);
        }
    } else if (d->source_nodes) {
        for (uint32_t i = 0; ok && i < d->source_nodes; i++) if (d->parents[i] < 0) ok = place_geometry(d, i, placed, 0u);
    } else {
        const jnode *meshes = jget(d->g->root, "meshes");
        for (uint32_t i = 0; ok && i < jcount(meshes); i++) {
            const jnode *primitives = jget(meshes->items[i], "primitives");
            for (uint32_t k = 0; ok && k < jcount(primitives); k++) ok = primitive(d, primitives->items[k], 0u, -1);
        }
    }
    free(placed);
    return ok;
}

static bool append_keys(decoded *d, const float *values, uint64_t count, uint32_t *at)
{
    if (!grow(d->g->r, (void **)&d->keys, &d->key_room, (uint64_t)d->key_count + count, sizeof *d->keys)) return false;
    *at = d->key_count;
    memcpy(d->keys + d->key_count, values, (size_t)count * sizeof *values);
    d->key_count += (uint32_t)count;
    return true;
}

static bool read_animations(decoded *d)
{
    const jnode *animations = jget(d->g->root, "animations");
    d->clip_count = jcount(animations);
    if (d->clip_count > NODES_MOST) return fail(d->g->r, NULL, "has more than 65536 animations");
    d->clips = zeroed(d->clip_count, sizeof *d->clips);
    uint8_t *seen = zeroed(d->node_count, 3u);
    bool ok = true;
    for (uint32_t i = 0; ok && i < d->clip_count; i++) {
        memset(seen, 0, (size_t)d->node_count * 3u);
        const jnode *json = animations->items[i], *named = jget(json, "name"), *channels = jget(json, "channels");
        const jnode *samplers = jget(json, "samplers");
        tide_skinned_clip_data *clip = d->clips + i;
        clip->name = named && named->kind == J_STRING ? named->text : "";
        clip->name_length = named && named->kind == J_STRING ? named->length : 0u;
        clip->first_channel = d->channel_count;
        if (!jcount(channels)) { ok = fail(d->g->r, NULL, "has an animation with no channels"); break; }
        for (uint32_t k = 0; ok && k < jcount(channels); k++) {
            const jnode *json_channel = channels->items[k], *target = jget(json_channel, "target");
            const jnode *json_node = jget(target, "node");
            if (!json_node) continue; // An extension can provide a target; core glTF ignores these channels.
            int64_t source;
            if (!jwhole(json_node, &source) || source >= d->source_nodes) { ok = fail(d->g->r, NULL, "has an animation targeting a node it hasn't got"); break; }
            const jnode *json_path = jget(target, "path");
            const char *path_name = json_path && json_path->kind == J_STRING ? json_path->text : "";
            const uint32_t path = strcmp(path_name, "translation") == 0 ? TIDE_SKINNED_TRANSLATION
                                : strcmp(path_name, "rotation") == 0 ? TIDE_SKINNED_ROTATION
                                : strcmp(path_name, "scale") == 0 ? TIDE_SKINNED_SCALE : UINT32_MAX;
            if (path == UINT32_MAX) {
                ok = fail(d->g->r, "export the animation without morph-weight channels", "has an unsupported animation target path ('%.40s')", path_name);
                break;
            }
            const uint32_t node = d->remap[source];
            if (seen[(size_t)node * 3u + path] || jget(jitem(jget(d->g->root, "nodes"), source), "matrix")) {
                ok = fail(d->g->r, NULL, "has repeated animation channels or animates a node declared with a matrix");
                break;
            }
            seen[(size_t)node * 3u + path] = 1u;
            int64_t sampler_index;
            const jnode *sampler = jwhole(jget(json_channel, "sampler"), &sampler_index) ? jitem(samplers, sampler_index) : NULL;
            if (!sampler) { ok = fail(d->g->r, NULL, "names an animation sampler it hasn't got"); break; }
            const jnode *interpolation = jget(sampler, "interpolation");
            const char *how = !interpolation ? "LINEAR" : interpolation->kind == J_STRING ? interpolation->text : "";
            const uint32_t method = strcmp(how, "STEP") == 0 ? TIDE_SKINNED_STEP
                                  : strcmp(how, "LINEAR") == 0 ? TIDE_SKINNED_LINEAR
                                  : strcmp(how, "CUBICSPLINE") == 0 ? TIDE_SKINNED_CUBICSPLINE : UINT32_MAX;
            if (method == UINT32_MAX) { ok = fail(d->g->r, NULL, "has an unknown animation interpolation ('%.40s')", how); break; }
            accessor input, output;
            ok = accessor_of(d->g, jget(sampler, "input"), &input, "animation times")
              && accessor_of(d->g, jget(sampler, "output"), &output, "animation values");
            if (!ok) break;
            const uint32_t copies = method == TIDE_SKINNED_CUBICSPLINE ? 3u : 1u;
            const uint32_t width = path == TIDE_SKINNED_ROTATION ? 4u : 3u;
            if (!input.count || input.type != 5126 || input.normalized || input.width != 1u
                || output.count != input.count * copies || output.width != width
                || (copies == 3u && input.count < 2u)
                || (output.type != 5126 && !(path == TIDE_SKINNED_ROTATION && output.normalized
                    && (output.type == 5120 || output.type == 5121 || output.type == 5122 || output.type == 5123)))) {
                ok = fail(d->g->r, NULL, "has animation times or values of an invalid type, width or count");
                break;
            }
            if (input.count > DECODE_BYTES_MOST / sizeof(float) || output.count * width > DECODE_BYTES_MOST / sizeof(float)) {
                ok = fail(d->g->r, NULL, "needs more than 512 MiB of animation key data");
                break;
            }
            float *times = zeroed((size_t)input.count, sizeof(float));
            float *values = zeroed((size_t)output.count, width * sizeof(float));
            ok = floats(d->g, jget(sampler, "input"), 1u, input.count, times, false, "animation times")
              && floats(d->g, jget(sampler, "output"), width, output.count, values, false, "animation values");
            float previous = -1.0f;
            for (uint64_t key = 0; ok && key < input.count; key++) {
                if (!(times[key] > previous)) { ok = fail(d->g->r, NULL, "has negative or unordered animation key times"); break; }
                previous = times[key];
                if (path == TIDE_SKINNED_ROTATION) {
                    const float *q = values + (key * copies + (copies == 3u ? 1u : 0u)) * width;
                    const float length = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
                    if (!(length > 0.0f) || length > FLT_MAX) { ok = fail(d->g->r, NULL, "has an animation key with no valid rotation quaternion"); break; }
                }
            }
            for (uint64_t at = 0; ok && at < output.count; at++) {
                float *v = values + at * width;
                if (path == TIDE_SKINNED_TRANSLATION) v[0] = 0.0f - v[0];
                else if (path == TIDE_SKINNED_ROTATION) v[1] = 0.0f - v[1], v[2] = 0.0f - v[2];
            }
            tide_skinned_channel_data channel = {.node = node, .path = path, .interpolation = method, .keys = (uint32_t)input.count};
            if (ok) ok = append_keys(d, times, input.count, &channel.times_at)
                      && append_keys(d, values, output.count * width, &channel.values_at)
                      && grow(d->g->r, (void **)&d->channels, &d->channel_room, (uint64_t)d->channel_count + 1u, sizeof *d->channels);
            if (ok) {
                d->channels[d->channel_count++] = channel;
                if (times[input.count - 1u] > clip->length) clip->length = times[input.count - 1u];
            }
            free(times);
            free(values);
        }
        clip->channels = d->channel_count - clip->first_channel;
    }
    free(seen);
    return ok;
}

static void put_word(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value, p[1] = (uint8_t)(value >> 8), p[2] = (uint8_t)(value >> 16), p[3] = (uint8_t)(value >> 24);
}

static void put_number(uint8_t *p, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    put_word(p, bits);
}

static void put_numbers(uint8_t *p, const float *values, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) put_number(p + (size_t)i * 4u, values[i]);
}

static uint32_t put_name(uint8_t *bytes, uint32_t *at, const char *text, uint32_t length)
{
    const uint32_t first = *at;
    memcpy(bytes + first, text, length);
    bytes[first + length] = 0;
    *at += length + 1u;
    return first;
}

static bool encode(decoded *d, uint8_t **out, uint32_t *out_size)
{
    const uint64_t node_at = TIDE_SKINNED_HEADER_SIZE;
    const uint64_t palette_at = node_at + (uint64_t)d->node_count * TIDE_SKINNED_NODE_SIZE;
    const uint64_t vertex_at = palette_at + (uint64_t)d->palette_count * TIDE_SKINNED_PALETTE_SIZE;
    const uint64_t index_at = vertex_at + (uint64_t)d->vertex_count * TIDE_SKINNED_VERTEX_SIZE;
    const uint64_t clip_at = index_at + (uint64_t)d->index_count * 4u;
    const uint64_t channel_at = clip_at + (uint64_t)d->clip_count * TIDE_SKINNED_CLIP_SIZE;
    const uint64_t influence_at = channel_at + (uint64_t)d->channel_count * TIDE_SKINNED_CHANNEL_SIZE;
    const uint64_t string_at = influence_at + (uint64_t)d->influence_count * TIDE_SKINNED_INFLUENCE_SIZE;
    uint64_t string_size = 0u;
    for (uint32_t i = 0; i < d->node_count; i++) string_size += (uint64_t)d->nodes[i].name_length + 1u;
    for (uint32_t i = 0; i < d->clip_count; i++) string_size += (uint64_t)d->clips[i].name_length + 1u;
    const uint64_t key_at = (string_at + string_size + 3u) & ~(uint64_t)3u;
    const uint64_t size = key_at + (uint64_t)d->key_count * 4u;
    if (size > DECODE_BYTES_MOST) return fail(d->g->r, NULL, "needs more than 512 MiB of decoded model data");
    uint8_t *bytes = zeroed((size_t)size, 1u);
    const uint32_t header[] = {TIDE_SKINNED_DATA_MAGIC, TIDE_SKINNED_DATA_VERSION, (uint32_t)size, d->flags,
        d->node_count, (uint32_t)node_at, d->palette_count, (uint32_t)palette_at, d->vertex_count, (uint32_t)vertex_at,
        d->index_count, (uint32_t)index_at, d->clip_count, (uint32_t)clip_at, d->channel_count, (uint32_t)channel_at,
        d->influence_count, (uint32_t)influence_at, (uint32_t)string_at, (uint32_t)string_size,
        (uint32_t)key_at, d->key_count * 4u};
    for (uint32_t i = 0; i < sizeof header / sizeof header[0]; i++) put_word(bytes + (size_t)i * 4u, header[i]);
    uint32_t text = (uint32_t)string_at;
    tide_sha256 hash;
    tide_sha256_init(&hash);
    for (uint32_t i = 0; i < d->node_count; i++) {
        const tide_skinned_node_data *n = d->nodes + i;
        uint8_t *p = bytes + node_at + (size_t)i * TIDE_SKINNED_NODE_SIZE;
        put_word(p, (uint32_t)n->parent), put_word(p + 4, n->source_node);
        put_word(p + 8, put_name(bytes, &text, n->name, n->name_length)), put_word(p + 12, n->name_length);
        put_numbers(p + 16, n->position, 3u), put_numbers(p + 28, n->rotation, 4u), put_numbers(p + 44, n->scale, 3u);
        // Absolute string offsets depend on unrelated geometry; omit them.
        tide_sha256_add(&hash, p, 8u);
        tide_sha256_add(&hash, p + 12, 44u);
        tide_sha256_add(&hash, n->name, n->name_length);
    }
    tide_sha256_end(&hash, bytes + 88);
    for (uint32_t i = 0; i < d->palette_count; i++) {
        uint8_t *p = bytes + palette_at + (size_t)i * TIDE_SKINNED_PALETTE_SIZE;
        put_word(p, d->palettes[i].node), put_numbers(p + 4, d->palettes[i].inverse_bind, 16u);
    }
    for (uint32_t i = 0; i < d->vertex_count; i++) {
        const tide_skinned_vertex_data *v = d->vertices + i;
        uint8_t *p = bytes + vertex_at + (size_t)i * TIDE_SKINNED_VERTEX_SIZE;
        put_numbers(p, v->position, 3u), put_numbers(p + 12, v->normal, 3u), put_numbers(p + 24, v->uv, 2u);
        put_numbers(p + 32, v->uv2, 2u), put_numbers(p + 40, v->color, 4u), put_numbers(p + 56, v->tangent, 4u);
        put_word(p + 72, v->first_influence), put_word(p + 76, v->influences);
    }
    for (uint32_t i = 0; i < d->index_count; i++) put_word(bytes + index_at + (size_t)i * 4u, d->indices[i]);
    for (uint32_t i = 0; i < d->clip_count; i++) {
        const tide_skinned_clip_data *c = d->clips + i;
        uint8_t *p = bytes + clip_at + (size_t)i * TIDE_SKINNED_CLIP_SIZE;
        put_word(p, put_name(bytes, &text, c->name, c->name_length)), put_word(p + 4, c->name_length);
        put_word(p + 8, c->first_channel), put_word(p + 12, c->channels), put_number(p + 16, c->length);
    }
    for (uint32_t i = 0; i < d->channel_count; i++) {
        const tide_skinned_channel_data *c = d->channels + i;
        uint8_t *p = bytes + channel_at + (size_t)i * TIDE_SKINNED_CHANNEL_SIZE;
        put_word(p, c->node), put_word(p + 4, c->path), put_word(p + 8, c->interpolation), put_word(p + 12, c->keys);
        put_word(p + 16, (uint32_t)key_at + c->times_at * 4u), put_word(p + 20, (uint32_t)key_at + c->values_at * 4u);
    }
    for (uint32_t i = 0; i < d->influence_count; i++) {
        uint8_t *p = bytes + influence_at + (size_t)i * TIDE_SKINNED_INFLUENCE_SIZE;
        put_word(p, d->influences[i].palette), put_number(p + 4, d->influences[i].weight);
    }
    put_numbers(bytes + key_at, d->keys, d->key_count);
    if (!tide_skinned_data_validate(bytes, (size_t)size)) {
        free(bytes);
        return fail(d->g->r, NULL, "produces model data with an invalid transform or geometry");
    }
    *out = bytes;
    *out_size = (uint32_t)size;
    return true;
}

static void decoded_free(decoded *d)
{
    free(d->nodes), free(d->parents), free(d->remap), free(d->visiting), free(d->world), free(d->skins);
    free(d->palettes), free(d->vertices), free(d->indices), free(d->influences), free(d->clips), free(d->channels), free(d->keys);
}

bool tide_skinned_data_decode(const void *input, size_t size, bool runtime, tide_skinned_side_fn side, void *user,
                              uint8_t **out, uint32_t *out_size, char why[256], char note[256])
{
    if (!out || !out_size || !why || !note) return false;
    *out = NULL, *out_size = 0u, why[0] = 0, note[0] = 0;
    reader r = {.why = why, .note = note, .runtime = runtime, .side = runtime ? NULL : side, .user = user};
    if (!input || !size || size > DECODE_BYTES_MOST) return fail(&r, NULL, "has no bytes, or more than 512 MiB of model bytes");
    const uint8_t *bytes = input;
    const char *json = input;
    size_t json_size = size;
    const uint8_t *bin = NULL;
    uint64_t bin_size = 0u;
    const bool glb = size >= 4u && memcmp(bytes, "glTF", 4u) == 0;
    if (runtime && !glb) return fail(&r, "export a self-contained GLB, then fetch its bytes before decoding", "isn't a self-contained GLB");
    if (glb) {
        if (size < 20u || read_word(bytes + 4) != 2u || read_word(bytes + 8) != size) return fail(&r, NULL, "has an invalid GLB header");
        uint64_t at = 12u;
        uint32_t chunks = 0u;
        json = NULL;
        while (at < size) {
            if (size - at < 8u) return fail(&r, NULL, "ends inside a GLB chunk header");
            const uint32_t length = read_word(bytes + at), kind = read_word(bytes + at + 4u);
            at += 8u;
            if ((length & 3u) || length > size - at) return fail(&r, NULL, "has a GLB chunk past its bytes or without four-byte alignment");
            if (!chunks && kind != 0x4e4f534au) return fail(&r, NULL, "has no JSON as its first GLB chunk");
            if (kind == 0x4e4f534au) {
                if (json) return fail(&r, NULL, "has more than one GLB JSON chunk");
                json = (const char *)bytes + at, json_size = length;
            } else if (kind == 0x004e4942u) {
                if (bin) return fail(&r, NULL, "has more than one GLB binary chunk");
                bin = bytes + at, bin_size = length;
            }
            chunks++, at += length;
        }
        if (!json) return fail(&r, NULL, "has no GLB JSON chunk");
    }
    jparser parser = {.p = json, .end = json + json_size};
    const jnode *root = jvalue(&parser, 0);
    jspace(&parser);
    bool ok = root && !parser.bad && parser.p == parser.end && root->kind == J_OBJECT;
    if (!ok) fail(&r, NULL, "isn't a glTF JSON object that reads");
    const jnode *version = jget(jget(root, "asset"), "version");
    if (ok && (!version || version->kind != J_STRING || strcmp(version->text, "2.0") != 0)) {
        ok = fail(&r, "export as glTF 2.0", "isn't a glTF 2.0 model");
    }
    const jnode *required = jget(root, "extensionsRequired");
    if (ok && required && required->kind != J_ARRAY) ok = fail(&r, NULL, "has extensionsRequired that isn't an array");
    for (uint32_t i = 0; ok && i < jcount(required); i++) {
        const jnode *extension = required->items[i];
        char why[256], note[256];
        if (extension->kind != J_STRING) ok = fail(&r, NULL, "has a required glTF extension that isn't text");
        else if (gltf_required_extension_error(extension->text, why, note)) ok = fail(&r, note, "%s", why);
    }
    gltf g = {.r = &r, .root = root, .bin = bin, .bin_size = bin_size};
    g.buffer_count = jcount(jget(root, "buffers"));
    if (g.buffer_count > NODES_MOST) ok = fail(&r, NULL, "has more than 65536 buffers");
    const uint32_t allocated_buffers = ok ? g.buffer_count : 0u;
    g.buffers = zeroed(allocated_buffers, sizeof *g.buffers);
    for (uint32_t i = 0; ok && i < g.buffer_count; i++) {
        const jnode *uri = jget(jitem(jget(root, "buffers"), i), "uri");
        if (runtime && uri && (uri->kind != J_STRING || uri->length < 5u || memcmp(uri->text, "data:", 5u) != 0)) {
            ok = fail(&r, "embed all buffers in the GLB", "has an external buffer in a runtime model");
            break;
        }
        const uint8_t *data;
        uint64_t buffer_size;
        ok = buffer_bytes(&g, i, &data, &buffer_size);
    }
    const jnode *images = jget(root, "images");
    for (uint32_t i = 0; ok && runtime && i < jcount(images); i++) {
        const jnode *uri = jget(images->items[i], "uri");
        if (uri && (uri->kind != J_STRING || uri->length < 5u || memcmp(uri->text, "data:", 5u) != 0)) {
            ok = fail(&r, "embed all resources in the GLB", "has an external image in a runtime model");
        }
    }
    decoded d = {.g = &g};
    if (ok) ok = read_nodes(&d) && read_skins(&d) && read_geometry(&d) && read_animations(&d) && encode(&d, out, out_size);
    decoded_free(&d);
    for (uint32_t i = 0; i < allocated_buffers; i++) free(g.buffers[i].own);
    free(g.buffers);
    jfree(&parser);
    return ok && !r.failed;
}

void tide_skinned_data_free(uint8_t *bytes)
{
    free(bytes);
}
