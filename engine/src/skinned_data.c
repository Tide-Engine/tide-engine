#include "tide/skinned_data.h"

#include <float.h>
#include <string.h>

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool number(const uint8_t *p, float *out)
{
    const uint32_t bits = word(p);
    memcpy(out, &bits, sizeof *out);
    return *out >= -FLT_MAX && *out <= FLT_MAX;
}

static bool numbers(const uint8_t *p, uint32_t count, float *out)
{
    for (uint32_t i = 0; i < count; i++) {
        if (!number(p + (size_t)i * 4u, out + i)) return false;
    }
    return true;
}

// Tables start after the header, which is longer in version 2.
static bool span(uint32_t header, uint32_t size, uint32_t at, uint64_t count, uint32_t stride)
{
    return at >= header && !(at & 3u) && at <= size && count * stride <= (uint64_t)size - at;
}

static bool key_span(const tide_skinned_data_view *v, uint32_t at, uint64_t count)
{
    return !(at & 3u) && at >= v->key_at && at <= (uint64_t)v->key_at + v->key_size
        && count * 4u <= (uint64_t)v->key_at + v->key_size - at;
}

static const char *name(const tide_skinned_data_view *v, uint32_t at, uint32_t length)
{
    const uint64_t end = (uint64_t)v->string_at + v->string_size;
    return at >= v->string_at && at < end && length < end - at && !v->data[at + length]
        ? (const char *)v->data + at : NULL;
}

bool tide_skinned_data_open(const void *bytes, size_t size, tide_skinned_data_view *out)
{
    if (!out) return false;
    *out = (tide_skinned_data_view){0};
    if (!bytes || size < TIDE_SKINNED_HEADER_SIZE || size > UINT32_MAX) return false;
    const uint8_t *p = bytes;
    const uint32_t version = word(p + 4);
    if (word(p) != TIDE_SKINNED_DATA_MAGIC || (version != TIDE_SKINNED_DATA_VERSION && version != TIDE_SKINNED_SHAPED_VERSION)
        || word(p + 8) != size) return false;
    const uint32_t h = version == TIDE_SKINNED_SHAPED_VERSION ? TIDE_SKINNED_SHAPED_HEADER_SIZE : TIDE_SKINNED_HEADER_SIZE;
    if (size < h) return false;
    tide_skinned_data_view v = {.data = p, .size = (uint32_t)size, .flags = word(p + 12)};
    v.nodes = word(p + 16), v.node_at = word(p + 20);
    v.palettes = word(p + 24), v.palette_at = word(p + 28);
    v.vertices = word(p + 32), v.vertex_at = word(p + 36);
    v.indices = word(p + 40), v.index_at = word(p + 44);
    v.clips = word(p + 48), v.clip_at = word(p + 52);
    v.channels = word(p + 56), v.channel_at = word(p + 60);
    v.influences = word(p + 64), v.influence_at = word(p + 68);
    v.string_at = word(p + 72), v.string_size = word(p + 76);
    v.key_at = word(p + 80), v.key_size = word(p + 84);
    if (h == TIDE_SKINNED_SHAPED_HEADER_SIZE) {
        v.shapes = word(p + 120), v.shape_at = word(p + 124);
        v.shape_deltas = word(p + 128), v.shape_delta_at = word(p + 132);
        // Version 2 is only for a model with shapes, so each model has one form.
        if (!v.shapes || !span(h, v.size, v.shape_at, v.shapes, TIDE_SKINNED_SHAPE_SIZE)
            || !span(h, v.size, v.shape_delta_at, v.shape_deltas, TIDE_SKINNED_SHAPE_DELTA_SIZE)) return false;
    }
    if (!v.nodes || !span(h, v.size, v.node_at, v.nodes, TIDE_SKINNED_NODE_SIZE)
        || !span(h, v.size, v.palette_at, v.palettes, TIDE_SKINNED_PALETTE_SIZE)
        || !span(h, v.size, v.vertex_at, v.vertices, TIDE_SKINNED_VERTEX_SIZE)
        || !span(h, v.size, v.index_at, v.indices, 4u)
        || !span(h, v.size, v.clip_at, v.clips, TIDE_SKINNED_CLIP_SIZE)
        || !span(h, v.size, v.channel_at, v.channels, TIDE_SKINNED_CHANNEL_SIZE)
        || !span(h, v.size, v.influence_at, v.influences, TIDE_SKINNED_INFLUENCE_SIZE)
        || !span(h, v.size, v.string_at, v.string_size, 1u) || !span(h, v.size, v.key_at, v.key_size, 1u)) return false;
    memcpy(v.skeleton_hash, p + 88, sizeof v.skeleton_hash);
    *out = v;
    return true;
}

bool tide_skinned_data_node(const tide_skinned_data_view *v, uint32_t index, tide_skinned_node_data *out)
{
    if (!v || !out || index >= v->nodes) return false;
    const uint8_t *p = v->data + v->node_at + (size_t)index * TIDE_SKINNED_NODE_SIZE;
    const uint32_t parent = word(p);
    if (index ? parent >= index : parent != UINT32_MAX) return false;
    out->parent = index ? (int32_t)parent : -1;
    out->source_node = word(p + 4);
    out->name_length = word(p + 12);
    out->name = name(v, word(p + 8), out->name_length);
    return out->name && numbers(p + 16, 3u, out->position) && numbers(p + 28, 4u, out->rotation)
        && numbers(p + 44, 3u, out->scale);
}

bool tide_skinned_data_palette(const tide_skinned_data_view *v, uint32_t index, tide_skinned_palette_data *out)
{
    if (!v || !out || index >= v->palettes) return false;
    const uint8_t *p = v->data + v->palette_at + (size_t)index * TIDE_SKINNED_PALETTE_SIZE;
    out->node = word(p);
    return out->node < v->nodes && numbers(p + 4, 16u, out->inverse_bind);
}

bool tide_skinned_data_vertex(const tide_skinned_data_view *v, uint32_t index, tide_skinned_vertex_data *out)
{
    if (!v || !out || index >= v->vertices) return false;
    const uint8_t *p = v->data + v->vertex_at + (size_t)index * TIDE_SKINNED_VERTEX_SIZE;
    out->first_influence = word(p + 72);
    out->influences = word(p + 76);
    return out->first_influence <= v->influences && out->influences <= v->influences - out->first_influence
        && numbers(p, 3u, out->position) && numbers(p + 12, 3u, out->normal) && numbers(p + 24, 2u, out->uv)
        && numbers(p + 32, 2u, out->uv2) && numbers(p + 40, 4u, out->color) && numbers(p + 56, 4u, out->tangent);
}

bool tide_skinned_data_influence(const tide_skinned_data_view *v, uint32_t index, tide_skinned_influence_data *out)
{
    if (!v || !out || index >= v->influences) return false;
    const uint8_t *p = v->data + v->influence_at + (size_t)index * TIDE_SKINNED_INFLUENCE_SIZE;
    out->palette = word(p);
    return out->palette < v->palettes && number(p + 4, &out->weight) && out->weight >= 0.0f;
}

bool tide_skinned_data_index(const tide_skinned_data_view *v, uint32_t index, uint32_t *out)
{
    if (!v || !out || index >= v->indices) return false;
    *out = word(v->data + v->index_at + (size_t)index * 4u);
    return *out < v->vertices;
}

bool tide_skinned_data_clip(const tide_skinned_data_view *v, uint32_t index, tide_skinned_clip_data *out)
{
    if (!v || !out || index >= v->clips) return false;
    const uint8_t *p = v->data + v->clip_at + (size_t)index * TIDE_SKINNED_CLIP_SIZE;
    out->name_length = word(p + 4);
    out->name = name(v, word(p), out->name_length);
    out->first_channel = word(p + 8);
    out->channels = word(p + 12);
    return out->name && out->first_channel <= v->channels && out->channels <= v->channels - out->first_channel
        && number(p + 16, &out->length) && out->length >= 0.0f;
}

bool tide_skinned_data_channel(const tide_skinned_data_view *v, uint32_t index, tide_skinned_channel_data *out)
{
    if (!v || !out || index >= v->channels) return false;
    const uint8_t *p = v->data + v->channel_at + (size_t)index * TIDE_SKINNED_CHANNEL_SIZE;
    out->node = word(p), out->path = word(p + 4), out->interpolation = word(p + 8), out->keys = word(p + 12);
    out->times_at = word(p + 16), out->values_at = word(p + 20);
    const uint32_t width = out->path == TIDE_SKINNED_ROTATION ? 4u : 3u;
    const uint32_t copies = out->interpolation == TIDE_SKINNED_CUBICSPLINE ? 3u : 1u;
    return out->node < v->nodes && out->path <= TIDE_SKINNED_SCALE && out->interpolation <= TIDE_SKINNED_CUBICSPLINE
        && out->keys && (copies == 1u || out->keys >= 2u) && key_span(v, out->times_at, out->keys)
        && key_span(v, out->values_at, (uint64_t)out->keys * width * copies);
}

bool tide_skinned_data_key(const tide_skinned_data_view *v, const tide_skinned_channel_data *c,
                           uint32_t index, float *time, float value[4], float in[4], float out[4])
{
    if (!v || !c || !time || !value || !in || !out || index >= c->keys || c->path > TIDE_SKINNED_SCALE
        || c->interpolation > TIDE_SKINNED_CUBICSPLINE) return false;
    const uint32_t width = c->path == TIDE_SKINNED_ROTATION ? 4u : 3u;
    const uint32_t copies = c->interpolation == TIDE_SKINNED_CUBICSPLINE ? 3u : 1u;
    if (!key_span(v, c->times_at, c->keys) || !key_span(v, c->values_at, (uint64_t)c->keys * width * copies)) return false;
    memset(value, 0, 4u * sizeof *value);
    memset(in, 0, 4u * sizeof *in);
    memset(out, 0, 4u * sizeof *out);
    const uint8_t *p = v->data + c->values_at + (size_t)index * width * copies * 4u;
    if (!number(v->data + c->times_at + (size_t)index * 4u, time)) return false;
    return copies == 1u ? numbers(p, width, value)
        : numbers(p, width, in) && numbers(p + width * 4u, width, value) && numbers(p + width * 8u, width, out);
}

bool tide_skinned_data_shape(const tide_skinned_data_view *v, uint32_t index, tide_skinned_shape_data *out)
{
    if (!v || !out || index >= v->shapes) return false;
    const uint8_t *p = v->data + v->shape_at + (size_t)index * TIDE_SKINNED_SHAPE_SIZE;
    out->name_length = word(p + 4);
    out->name = name(v, word(p), out->name_length);
    out->first_delta = word(p + 8);
    out->deltas = word(p + 12);
    return out->name && out->first_delta <= v->shape_deltas && out->deltas <= v->shape_deltas - out->first_delta
        && number(p + 16, &out->weight);
}

bool tide_skinned_data_shape_delta(const tide_skinned_data_view *v, uint32_t index, tide_skinned_shape_delta_data *out)
{
    if (!v || !out || index >= v->shape_deltas) return false;
    const uint8_t *p = v->data + v->shape_delta_at + (size_t)index * TIDE_SKINNED_SHAPE_DELTA_SIZE;
    out->vertex = word(p);
    return out->vertex < v->vertices && numbers(p + 4, 3u, out->position) && numbers(p + 16, 3u, out->normal)
        && numbers(p + 28, 3u, out->tangent);
}

bool tide_skinned_data_validate(const void *bytes, size_t size)
{
    tide_skinned_data_view v;
    if (!tide_skinned_data_open(bytes, size, &v) || v.nodes > INT32_MAX || v.indices % 3u) return false;
    for (uint32_t i = 0; i < v.nodes; i++) {
        tide_skinned_node_data n;
        if (!tide_skinned_data_node(&v, i, &n)) return false;
        const float q = n.rotation[0] * n.rotation[0] + n.rotation[1] * n.rotation[1]
                      + n.rotation[2] * n.rotation[2] + n.rotation[3] * n.rotation[3];
        if (!(q > 0.0f) || q > FLT_MAX) return false;
    }
    for (uint32_t i = 0; i < v.palettes; i++) {
        tide_skinned_palette_data p;
        if (!tide_skinned_data_palette(&v, i, &p) || p.inverse_bind[3] != 0.0f || p.inverse_bind[7] != 0.0f
            || p.inverse_bind[11] != 0.0f || p.inverse_bind[15] != 1.0f) return false;
    }
    for (uint32_t i = 0; i < v.influences; i++) {
        tide_skinned_influence_data inf;
        if (!tide_skinned_data_influence(&v, i, &inf)) return false;
    }
    for (uint32_t i = 0; i < v.vertices; i++) {
        tide_skinned_vertex_data vertex;
        if (!tide_skinned_data_vertex(&v, i, &vertex) || !vertex.influences) return false;
        float sum = 0.0f;
        for (uint32_t k = 0; k < vertex.influences; k++) {
            tide_skinned_influence_data inf;
            if (!tide_skinned_data_influence(&v, vertex.first_influence + k, &inf)) return false;
            sum += inf.weight;
        }
        if (!(sum > 0.0f) || sum > FLT_MAX) return false;
    }
    for (uint32_t i = 0; i < v.indices; i++) {
        uint32_t index;
        if (!tide_skinned_data_index(&v, i, &index)) return false;
    }
    for (uint32_t i = 0; i < v.clips; i++) {
        tide_skinned_clip_data clip;
        if (!tide_skinned_data_clip(&v, i, &clip)) return false;
    }
    for (uint32_t i = 0; i < v.channels; i++) {
        tide_skinned_channel_data channel;
        if (!tide_skinned_data_channel(&v, i, &channel)) return false;
        float previous = -1.0f;
        for (uint32_t k = 0; k < channel.keys; k++) {
            float time, value[4], in[4], out[4];
            if (!tide_skinned_data_key(&v, &channel, k, &time, value, in, out) || !(time > previous)) return false;
            previous = time;
        }
    }
    uint32_t next = 0;
    for (uint32_t i = 0; i < v.shapes; i++) {
        tide_skinned_shape_data shape;
        if (!tide_skinned_data_shape(&v, i, &shape) || shape.first_delta != next) return false;
        next += shape.deltas;
        uint32_t after = 0; // Every delta's vertex is past the shape's one before
        for (uint32_t k = 0; k < shape.deltas; k++) {
            tide_skinned_shape_delta_data delta;
            if (!tide_skinned_data_shape_delta(&v, shape.first_delta + k, &delta) || (k && delta.vertex < after)) return false;
            after = delta.vertex + 1u;
        }
    }
    return next == v.shape_deltas;
}
