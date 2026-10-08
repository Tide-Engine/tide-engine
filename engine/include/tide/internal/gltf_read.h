#pragma once

// Shared glTF buffers/accessors. Included by an importer after it declares
// reader and gltf, and supplies fail() and zeroed().

#include "tide/page.h"

static int base64_digit(const char c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
         : c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
}

// What base64 text says, to free(): NULL for text that isn't base64.
static uint8_t *base64(const char *text, const size_t length, uint64_t *size)
{
    uint8_t *out = tide_alloc(length / 4u * 3u + 3u);
    uint64_t n = 0;
    uint32_t bits = 0;
    int have = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '=') break;
        const int d = base64_digit(text[i]);
        if (d < 0) {
            free(out);
            return NULL;
        }
        bits = bits << 6 | (uint32_t)d;
        have += 6;
        if (have >= 8) {
            have -= 8;
            out[n++] = (uint8_t)(bits >> have);
        }
    }
    *size = n;
    return out;
}

// A buffer's bytes: the .glb's own, a data URI's, or a file's beside the .gltf.
static bool buffer_bytes(gltf *g, const int64_t index, const uint8_t **data, uint64_t *size)
{
    if (index < 0 || index >= (int64_t)g->buffer_count) return fail(g->r, NULL, "names a buffer it hasn't got");
    gltf_buffer *buffer = &g->buffers[index];
    if (!buffer->tried) {
        buffer->tried = true;
        const jnode *described = jitem(jget(g->root, "buffers"), index);
        const jnode *uri = jget(described, "uri");
        const jnode *extensions = jget(described, "extensions");
        if (jget(extensions, "EXT_meshopt_compression") || jget(extensions, "KHR_meshopt_compression")) {
            return fail(g->r, "export it again with compression off: Tide reads a mesh's numbers as they are",
                        "has its geometry compressed with meshopt, which Tide doesn't read");
        }
        if (!uri) {
            if (index != 0 || !g->bin) return fail(g->r, NULL, "has a buffer with no bytes: it names no file, and isn't a .glb's own");
            buffer->data = g->bin;
            buffer->size = g->bin_size;
        } else if (uri->kind != J_STRING) {
            return fail(g->r, NULL, "has a buffer whose 'uri' isn't text");
        } else if (uri->length >= 5 && memcmp(uri->text, "data:", 5) == 0) {
            const char *comma = memchr(uri->text, ',', uri->length);
            const bool encoded = comma && comma - uri->text >= 7 && memcmp(comma - 7, ";base64", 7) == 0;
            if (encoded) buffer->own = base64(comma + 1, uri->length - (size_t)(comma + 1 - uri->text), &buffer->size);
            if (!buffer->own) return fail(g->r, NULL, "has a buffer inside it that isn't base64");
            buffer->data = buffer->own;
        } else if (strstr(uri->text, "://")) {
            return fail(g->r, "keep the buffer in a file beside the .gltf, or export a .glb, which is one file",
                        "keeps a buffer at a URL, '%.120s'", uri->text);
        } else {
            // A path from the .gltf's folder, with %20 for a space and the like
            char *path = tide_alloc((size_t)uri->length + 1u);
            uint32_t n = 0;
            for (uint32_t i = 0; i < uri->length; i++) {
                const int high = uri->text[i] == '%' && i + 2u < uri->length ? hex_digit(uri->text[i + 1u]) : -1;
                const int low = high >= 0 ? hex_digit(uri->text[i + 2u]) : -1;
                if (low >= 0 && (high || low)) {
                    path[n++] = (char)(high * 16 + low);
                    i += 2u;
                } else {
                    path[n++] = uri->text[i];
                }
            }
            path[n] = '\0';
            size_t side_size = 0;
            const uint8_t *side = g->r->side ? g->r->side(g->r->user, path, &side_size) : NULL;
            if (!side) {
                fail(g->r, "a .gltf's buffers are files beside it, by the paths it names; a .glb is the same mesh as one file",
                     "names a buffer file that isn't there, '%.120s'", path);
                free(path);
                return false;
            }
            free(path);
            buffer->data = side;
            buffer->size = side_size;
        }
        const int64_t said = jwhole_or(described, "byteLength", -1);
        if (said < 0 || (uint64_t)said > buffer->size) {
            buffer->data = NULL;
            return fail(g->r, NULL, "says a buffer is longer than its bytes are");
        }
        buffer->size = (uint64_t)said;
    }
    if (!buffer->data) return fail(g->r, NULL, "has a buffer that can't be read");
    *data = buffer->data;
    *size = buffer->size;
    return true;
}

// The bytes of a buffer view from `offset` in it on: how many there are, and
// the view's stride (0 where it says none).
static const uint8_t *view_at(gltf *g, const int64_t view, const uint64_t offset, uint64_t *room, uint64_t *stride)
{
    const jnode *v = jitem(jget(g->root, "bufferViews"), view);
    int64_t buffer, length;
    if (!v || !jwhole(jget(v, "buffer"), &buffer) || !jwhole(jget(v, "byteLength"), &length)) {
        fail(g->r, NULL, "names a buffer view it hasn't got, or one that doesn't say its buffer and its length");
        return NULL;
    }
    const jnode *extensions = jget(v, "extensions");
    if (jget(extensions, "EXT_meshopt_compression") || jget(extensions, "KHR_meshopt_compression")) {
        fail(g->r, "export it again with compression off: Tide reads a mesh's numbers as they are",
             "has its geometry compressed with meshopt, which Tide doesn't read");
        return NULL;
    }
    const uint64_t from = (uint64_t)jwhole_or(v, "byteOffset", 0);
    const uint8_t *data;
    uint64_t size;
    if (!buffer_bytes(g, buffer, &data, &size)) return NULL;
    if (from > size || (uint64_t)length > size - from || offset > (uint64_t)length) {
        fail(g->r, NULL, "has a buffer view that runs past its buffer's end");
        return NULL;
    }
    *room = (uint64_t)length - offset;
    *stride = (uint64_t)jwhole_or(v, "byteStride", 0);
    if (*stride > 255u) { // The format's most is 252: more would be an element past any count's reach
        fail(g->r, NULL, "has a buffer view with a stride no mesh has");
        return NULL;
    }
    return data + from + offset;
}

static uint32_t component_bytes(const int64_t type)
{
    return type == 5120 || type == 5121 ? 1u : type == 5122 || type == 5123 ? 2u : type == 5125 || type == 5126 ? 4u : 0u;
}

// One number of an accessor, as a float: a float as it is (one no float should be is 0), a whole number as
// its value, or where the accessor says `normalized`, as the part of its range the format says it is.
static float component(const uint8_t *p, const int64_t type, const bool normalized)
{
    switch (type) {
    case 5126: {
        float f;
        memcpy(&f, p, sizeof f);
        return f >= -FLT_MAX && f <= FLT_MAX ? f : 0.0f;
    }
    case 5121: return normalized ? (float)p[0] / 255.0f : (float)p[0];
    case 5120: {
        const float f = (float)(int8_t)p[0];
        return !normalized ? f : f / 127.0f < -1.0f ? -1.0f : f / 127.0f;
    }
    case 5123: {
        uint16_t v;
        memcpy(&v, p, sizeof v);
        return normalized ? (float)v / 65535.0f : (float)v;
    }
    case 5122: {
        int16_t v;
        memcpy(&v, p, sizeof v);
        const float f = (float)v;
        return !normalized ? f : f / 32767.0f < -1.0f ? -1.0f : f / 32767.0f;
    }
    default: {
        uint32_t v;
        memcpy(&v, p, sizeof v);
        return (float)v;
    }
    }
}

static uint32_t whole_component(const uint8_t *p, const int64_t type)
{
    if (type == 5121) return p[0];
    if (type == 5123) {
        uint16_t v;
        memcpy(&v, p, sizeof v);
        return v;
    }
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

typedef struct accessor {
    const jnode *json;
    int64_t view; // -1: none, so zeros
    uint64_t offset, count;
    int64_t type; // 5120 to 5126
    uint32_t width; // Numbers an element: 1 to 4 (a matrix is none of a mesh's)
    bool normalized;
} accessor;

static bool accessor_of(gltf *g, const jnode *index, accessor *a, const char *what)
{
    int64_t i, count;
    const jnode *json = jwhole(index, &i) ? jitem(jget(g->root, "accessors"), i) : NULL;
    if (!json || !jwhole(jget(json, "count"), &count) || !jwhole(jget(json, "componentType"), &a->type)) {
        return fail(g->r, NULL, "names an accessor for its %s that it hasn't got, or one that doesn't say its count and its type", what);
    }
    const jnode *type = jget(json, "type");
    const char *name = type && type->kind == J_STRING ? type->text : "";
    a->width = strcmp(name, "SCALAR") == 0 ? 1u : strcmp(name, "VEC2") == 0 ? 2u : strcmp(name, "VEC3") == 0 ? 3u
             : strcmp(name, "VEC4") == 0 ? 4u
#ifdef GLTF_ALLOW_MATRICES
             : strcmp(name, "MAT4") == 0 ? 16u
#endif
             : 0u;
    if (!a->width || !component_bytes(a->type)) return fail(g->r, NULL, "has %s of a type no mesh's are ('%.40s')", what, name);
    if ((uint64_t)count > INDICES_MOST) return fail(g->r, NULL, "has more %s than one Mesh holds", what);
    a->json = json;
    a->count = (uint64_t)count;
    a->view = jwhole_or(json, "bufferView", -1);
    a->offset = (uint64_t)jwhole_or(json, "byteOffset", 0);
    const jnode *normalized = jget(json, "normalized");
    a->normalized = normalized && normalized->kind == J_BOOL && normalized->truth;
    return true;
}

typedef struct sparse_accessor {
    uint64_t count;
    const uint8_t *indices, *values;
    int64_t index_type;
    uint32_t index_bytes, element_bytes;
} sparse_accessor;

// The elements a sparse accessor replaces, with both views checked against
// their buffers. Vertex attributes and triangle indices have the same sparse
// representation: the selectors address elements of the accessor itself.
static bool sparse_of(gltf *g, const accessor *a, sparse_accessor *out, const char *what)
{
    *out = (sparse_accessor){0};
    const jnode *sparse = jget(a->json, "sparse");
    if (!sparse) return true;
    const jnode *indices = jget(sparse, "indices"), *values = jget(sparse, "values");
    int64_t count, index_view, value_view;
    const int64_t index_type = jwhole_or(indices, "componentType", 0);
    if (!jwhole(jget(sparse, "count"), &count) || !jwhole(jget(indices, "bufferView"), &index_view)
        || !jwhole(jget(values, "bufferView"), &value_view) || (index_type != 5121 && index_type != 5123 && index_type != 5125)) {
        return fail(g->r, NULL, "has sparse %s that don't say where their numbers are", what);
    }
    uint64_t index_room, value_room, unused;
    const uint8_t *index_at = view_at(g, index_view, (uint64_t)jwhole_or(indices, "byteOffset", 0), &index_room, &unused);
    if (!index_at) return false;
    const uint8_t *value_at = view_at(g, value_view, (uint64_t)jwhole_or(values, "byteOffset", 0), &value_room, &unused);
    if (!value_at) return false;
    const uint32_t index_bytes = component_bytes(index_type), element_bytes = component_bytes(a->type) * a->width;
    if ((uint64_t)count > a->count || (uint64_t)count * index_bytes > index_room || (uint64_t)count * element_bytes > value_room) {
        return fail(g->r, NULL, "has sparse %s that run past their buffer view's end", what);
    }
    *out = (sparse_accessor){(uint64_t)count, index_at, value_at, index_type, index_bytes, element_bytes};
    return true;
}


// An accessor's elements as whole numbers: a primitive's indices. To free().
static uint32_t *read_indices(gltf *g, const jnode *index, uint64_t *count)
{
    accessor a;
    if (!accessor_of(g, index, &a, "indices")) return NULL;
    if (a.width != 1u || (a.type != 5121 && a.type != 5123 && a.type != 5125)) {
        fail(g->r, NULL, "has indices that aren't whole numbers");
        return NULL;
    }
    uint32_t *out = zeroed((size_t)a.count, sizeof *out);
    if (a.view >= 0 && a.count) {
        const uint32_t each = component_bytes(a.type);
        uint64_t room, stride;
        const uint8_t *at = view_at(g, a.view, a.offset, &room, &stride);
        if (at && !stride) stride = each;
        if (at && (stride < each || (a.count - 1u) * stride + each > room)) {
            fail(g->r, NULL, "has indices that run past their buffer view's end");
            at = NULL;
        }
        if (!at) {
            free(out);
            return NULL;
        }
        for (uint64_t i = 0; i < a.count; i++) out[i] = whole_component(at + i * stride, a.type);
    }
    sparse_accessor sparse;
    if (!sparse_of(g, &a, &sparse, "indices")) {
        free(out);
        return NULL;
    }
    for (uint64_t k = 0; k < sparse.count; k++) {
        const uint32_t i = whole_component(sparse.indices + k * sparse.index_bytes, sparse.index_type);
        if (i >= a.count) {
            fail(g->r, NULL, "has sparse indices for an element it hasn't got");
            free(out);
            return NULL;
        }
        out[i] = whole_component(sparse.values + k * sparse.element_bytes, a.type);
    }
    *count = a.count;
    return out;
}
