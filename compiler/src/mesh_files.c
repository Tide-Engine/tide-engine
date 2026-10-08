// Mesh files as `asset Mesh<V>` reads them (see mesh_files.h): glTF 2.0 and
// Wavefront OBJ, from their specifications, with a JSON reader, a base64
// reader and a number reader of their own. A file is somebody else's bytes,
// and the language server reads one at every keystroke: every offset and
// count a file says is checked against what's there before it's used.

#include "mesh_files.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/math.h"
#include "tide/sha256.h"

#pragma STDC FP_CONTRACT OFF

// What one mesh holds at the most: its indices are ints, and its vertices go to the GPU side by side.
#define VERTICES_MOST (1u << 24)
#define INDICES_MOST (3u << 25)

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

static void *zeroed(const size_t count, const size_t size)
{
    void *p = calloc(count ? count : 1u, size);
    if (!p) abort();
    return p;
}

// ---------------------------------------------------------------------------
// Numbers. A decimal number becomes a double by one multiplication or
// division by a power of ten that a double holds exactly, where the number's
// digits and its exponent allow (every number a mesh file has), and by several
// otherwise: basic operations in a fixed order, so the same bits everywhere,
// which the C library's strtod doesn't promise.

static bool parse_double(const char *s, const char *end, double *out)
{
    static const double tens[] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                  1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const char *p = s;
    bool negative = false;
    if (p < end && (*p == '-' || *p == '+')) negative = *p++ == '-';
    uint64_t mantissa = 0;
    int digits = 0; // Of the mantissa, from its first that isn't 0: 19 fit
    int64_t exponent = 0;
    bool any = false;
    for (; p < end && *p >= '0' && *p <= '9'; p++) {
        any = true;
        if (digits < 19) {
            mantissa = mantissa * 10u + (uint64_t)(*p - '0');
            if (mantissa) digits++;
        } else {
            exponent++;
        }
    }
    if (p < end && *p == '.') {
        for (p++; p < end && *p >= '0' && *p <= '9'; p++) {
            any = true;
            if (digits >= 19) continue;
            mantissa = mantissa * 10u + (uint64_t)(*p - '0');
            if (mantissa) digits++;
            exponent--;
        }
    }
    if (!any) return false;
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        bool down = false;
        if (p < end && (*p == '-' || *p == '+')) down = *p++ == '-';
        if (p >= end || *p < '0' || *p > '9') return false;
        int64_t said = 0;
        for (; p < end && *p >= '0' && *p <= '9'; p++) {
            if (said < 100000) said = said * 10 + (*p - '0');
        }
        exponent += down ? -said : said;
    }
    if (p != end) return false;
    double v = (double)mantissa;
    if (mantissa) {
        if (exponent > 400) exponent = 400;
        if (exponent < -400) exponent = -400;
        while (exponent > 0) {
            const int step = exponent > 22 ? 22 : (int)exponent;
            v *= tens[step];
            exponent -= step;
        }
        while (exponent < 0) {
            const int step = exponent < -22 ? 22 : (int)-exponent;
            v /= tens[step];
            exponent += step;
        }
    }
    *out = negative ? -v : v;
    return true;
}

bool mesh_number(const char *s, const char *end, float *out)
{
    double v;
    if (!parse_double(s, end, &v)) return false;
    *out = v >= -(double)FLT_MAX && v <= (double)FLT_MAX ? (float)v : 0.0f; // NaN and what's too big compare false
    return true;
}

// ---------------------------------------------------------------------------
// JSON, for a glTF's description: a tree in memory of its own, freed whole.

typedef enum jkind { J_NULL, J_BOOL, J_NUMBER, J_STRING, J_ARRAY, J_OBJECT } jkind;

typedef struct jnode {
    jkind kind;
    bool truth;
    const char *text; // A number's text as written; a string's with its escapes undone and a NUL after it
    uint32_t length;
    struct jnode **items; // An array's, or an object's values
    const char **keys;    // An object's
    uint32_t count;
} jnode;

typedef struct jblock {
    struct jblock *next;
} jblock;

typedef struct jparser {
    const char *p, *end;
    jblock *blocks;
    char *room;
    size_t left;
    bool bad;
} jparser;

static void *jalloc(jparser *j, const size_t size)
{
    const size_t need = (size + 15u) & ~(size_t)15u;
    if (need > j->left) {
        const size_t block = need > 65536u ? need : 65536u;
        jblock *b = malloc(sizeof(jblock) + 16u + block);
        if (!b) abort();
        b->next = j->blocks;
        j->blocks = b;
        j->room = (char *)b + ((sizeof(jblock) + 15u) & ~(size_t)15u);
        j->left = block;
    }
    void *out = j->room;
    j->room += need;
    j->left -= need;
    memset(out, 0, need);
    return out;
}

static void jfree(jparser *j)
{
    while (j->blocks) {
        jblock *next = j->blocks->next;
        free(j->blocks);
        j->blocks = next;
    }
}

static void jspace(jparser *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int hex_digit(const char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// Four hex digits at `p`, or -1.
static int hex4(const char *p, const char *end)
{
    if (end - p < 4) return -1;
    int v = 0;
    for (int i = 0; i < 4; i++) {
        const int d = hex_digit(p[i]);
        if (d < 0) return -1;
        v = v * 16 + d;
    }
    return v;
}

// A string, from its opening quote: its text, unescaped. NULL for one that's wrong.
static const char *jstring(jparser *j, uint32_t *length)
{
    if (j->p >= j->end || *j->p != '"') return NULL;
    const char *from = ++j->p;
    const char *close = from;
    while (close < j->end && *close != '"') close += *close == '\\' && close + 1 < j->end ? 2 : 1;
    if (close >= j->end) return NULL;
    char *out = jalloc(j, (size_t)(close - from) + 1u); // Unescaped, it's no longer
    uint32_t n = 0;
    for (const char *p = from; p < close; p++) {
        if (*p != '\\') {
            out[n++] = *p;
            continue;
        }
        if (++p >= close) return NULL;
        switch (*p) {
        case '"': case '\\': case '/': out[n++] = *p; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case 'n': out[n++] = '\n'; break;
        case 'r': out[n++] = '\r'; break;
        case 't': out[n++] = '\t'; break;
        case 'u': {
            int code = hex4(p + 1, close);
            if (code < 0) return NULL;
            p += 4;
            if (code >= 0xD800 && code < 0xDC00 && close - p >= 7 && p[1] == '\\' && p[2] == 'u') { // Half of a pair
                const int low = hex4(p + 3, close);
                if (low >= 0xDC00 && low < 0xE000) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    p += 6;
                }
            }
            // As UTF-8, which is never more bytes than the escape was
            if (code < 0x80) {
                out[n++] = (char)code;
            } else if (code < 0x800) {
                out[n++] = (char)(0xC0 | code >> 6);
                out[n++] = (char)(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out[n++] = (char)(0xE0 | code >> 12);
                out[n++] = (char)(0x80 | ((code >> 6) & 0x3F));
                out[n++] = (char)(0x80 | (code & 0x3F));
            } else {
                out[n++] = (char)(0xF0 | code >> 18);
                out[n++] = (char)(0x80 | ((code >> 12) & 0x3F));
                out[n++] = (char)(0x80 | ((code >> 6) & 0x3F));
                out[n++] = (char)(0x80 | (code & 0x3F));
            }
            break;
        }
        default: return NULL;
        }
    }
    out[n] = '\0';
    *length = n;
    j->p = close + 1;
    return out;
}

static jnode *jvalue(jparser *j, const int depth)
{
    jspace(j);
    if (j->p >= j->end || depth > 64) {
        j->bad = true;
        return NULL;
    }
    jnode *v = jalloc(j, sizeof *v);
    const char c = *j->p;
    if (c == '{' || c == '[') {
        const bool object = c == '{';
        const char closing = object ? '}' : ']';
        v->kind = object ? J_OBJECT : J_ARRAY;
        j->p++;
        jnode **items = NULL;
        const char **keys = NULL;
        uint32_t count = 0, room = 0;
        jspace(j);
        if (j->p < j->end && *j->p == closing) {
            j->p++;
            return v;
        }
        while (!j->bad) {
            const char *key = NULL;
            if (object) {
                jspace(j);
                uint32_t key_length;
                key = jstring(j, &key_length);
                jspace(j);
                if (!key || j->p >= j->end || *j->p != ':') {
                    j->bad = true;
                    break;
                }
                j->p++;
            }
            jnode *item = jvalue(j, depth + 1);
            if (!item) break;
            if (count == room) {
                room = room ? room * 2u : 8u;
                items = realloc(items, room * sizeof *items);
                keys = realloc(keys, room * sizeof *keys);
                if (!items || !keys) abort();
            }
            items[count] = item;
            keys[count++] = key;
            jspace(j);
            if (j->p < j->end && *j->p == ',') {
                j->p++;
                continue;
            }
            if (j->p < j->end && *j->p == closing) {
                j->p++;
                break;
            }
            j->bad = true;
        }
        if (!j->bad && count) {
            v->items = jalloc(j, count * sizeof *items);
            memcpy(v->items, items, count * sizeof *items);
            if (object) {
                v->keys = jalloc(j, count * sizeof *keys);
                memcpy(v->keys, keys, count * sizeof *keys);
            }
            v->count = count;
        }
        free(items);
        free(keys);
        return j->bad ? NULL : v;
    }
    if (c == '"') {
        v->kind = J_STRING;
        v->text = jstring(j, &v->length);
        if (!v->text) j->bad = true;
        return j->bad ? NULL : v;
    }
    const char *from = j->p;
    while (j->p < j->end && *j->p != ',' && *j->p != '}' && *j->p != ']' && *j->p != ' ' && *j->p != '\t' && *j->p != '\n'
           && *j->p != '\r') j->p++;
    const size_t length = (size_t)(j->p - from);
    if (length == 4 && memcmp(from, "true", 4) == 0) {
        v->kind = J_BOOL;
        v->truth = true;
    } else if (length == 5 && memcmp(from, "false", 5) == 0) {
        v->kind = J_BOOL;
    } else if (length == 4 && memcmp(from, "null", 4) == 0) {
        v->kind = J_NULL;
    } else {
        double number;
        if (!length || !parse_double(from, j->p, &number)) {
            j->bad = true;
            return NULL;
        }
        v->kind = J_NUMBER;
        v->text = from;
        v->length = (uint32_t)length;
    }
    return v;
}

static const jnode *jget(const jnode *object, const char *key)
{
    if (!object || object->kind != J_OBJECT) return NULL;
    for (uint32_t i = 0; i < object->count; i++) {
        if (strcmp(object->keys[i], key) == 0) return object->items[i];
    }
    return NULL;
}

static uint32_t jcount(const jnode *array)
{
    return array && array->kind == J_ARRAY ? array->count : 0u;
}

static const jnode *jitem(const jnode *array, const int64_t i)
{
    return array && array->kind == J_ARRAY && i >= 0 && i < (int64_t)array->count ? array->items[i] : NULL;
}

// A whole number from 0 up that a file can mean as an offset or a count; false for anything else.
static bool jwhole(const jnode *v, int64_t *out)
{
    double d;
    if (!v || v->kind != J_NUMBER || !parse_double(v->text, v->text + v->length, &d)) return false;
    if (!(d >= 0.0) || d > 4503599627370496.0) return false;
    const int64_t whole = (int64_t)d;
    if ((double)whole != d) return false;
    *out = whole;
    return true;
}

// The number at `key`, or `otherwise` where there's none.
static int64_t jwhole_or(const jnode *object, const char *key, const int64_t otherwise)
{
    int64_t v;
    return jwhole(jget(object, key), &v) ? v : otherwise;
}

static float jfloat(const jnode *v, const float otherwise)
{
    float f;
    return v && v->kind == J_NUMBER && mesh_number(v->text, v->text + v->length, &f) ? f : otherwise;
}

// `count` floats of an array that has exactly as many numbers; false otherwise.
static bool jfloats(const jnode *array, const uint32_t count, float *out)
{
    if (jcount(array) != count) return false;
    for (uint32_t i = 0; i < count; i++) {
        if (array->items[i]->kind != J_NUMBER) return false;
        out[i] = jfloat(array->items[i], 0.0f);
    }
    return true;
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
    if (need > VERTICES_MOST) {
        return fail(b->r, "a mesh that big is better split into several files, each a Mesh of its own",
                    "has more than %u vertices, the most one Mesh holds", (unsigned)VERTICES_MOST);
    }
    if (need <= b->vertex_room) return true;
    uint32_t room = b->vertex_room ? b->vertex_room : 256u;
    while (room < need) room *= 2u;
    b->m.position = realloc(b->m.position, (size_t)room * 3u * sizeof(float));
    b->m.normal = realloc(b->m.normal, (size_t)room * 3u * sizeof(float));
    b->m.uv = realloc(b->m.uv, (size_t)room * 2u * sizeof(float));
    b->m.uv2 = realloc(b->m.uv2, (size_t)room * 2u * sizeof(float));
    b->m.color = realloc(b->m.color, (size_t)room * 4u * sizeof(float));
    b->m.tangent = realloc(b->m.tangent, (size_t)room * 4u * sizeof(float));
    if (!b->m.position || !b->m.normal || !b->m.uv || !b->m.uv2 || !b->m.color || !b->m.tangent) abort();
    b->vertex_room = room;
    return true;
}

static bool more_indices(builder *b, const uint64_t more)
{
    const uint64_t need = (uint64_t)b->m.indices + more;
    if (need > INDICES_MOST) {
        return fail(b->r, "a mesh that big is better split into several files, each a Mesh of its own",
                    "has more than %u triangles, the most one Mesh holds", (unsigned)(INDICES_MOST / 3u));
    }
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
} gltf;

static int base64_digit(const char c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
         : c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
}

// What base64 text says, to free(): NULL for text that isn't base64.
static uint8_t *base64(const char *text, const size_t length, uint64_t *size)
{
    uint8_t *out = malloc(length / 4u * 3u + 3u);
    if (!out) abort();
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
            char *path = malloc((size_t)uri->length + 1u);
            if (!path) abort();
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
             : strcmp(name, "VEC4") == 0 ? 4u : 0u;
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

// An accessor's elements as floats, `want` numbers each into `out`: the
// numbers it has, and `fill` for a fourth it hasn't (a color's alpha). It has
// to have `expect` elements.
static bool read_floats(gltf *g, const jnode *index, const uint32_t want, const float fill, const uint64_t expect, float *out,
                        const char *what)
{
    accessor a;
    if (!accessor_of(g, index, &a, what)) return false;
    if (a.count != expect) {
        return fail(g->r, NULL, "has %llu %s for %llu positions: a mesh has one of each for every vertex", (unsigned long long)a.count,
                    what, (unsigned long long)expect);
    }
    const uint32_t each = component_bytes(a.type);
    const uint32_t element = each * a.width;
    const uint32_t take = a.width < want ? a.width : want;
    for (uint64_t i = 0; i < a.count; i++) {
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
        for (uint64_t i = 0; i < a.count; i++) {
            for (uint32_t c = 0; c < take; c++) out[i * want + c] = component(at + i * stride + (uint64_t)c * each, a.type, a.normalized);
        }
    }
    // A sparse accessor: some elements, by their numbers, given apart from the rest
    const jnode *sparse = jget(a.json, "sparse");
    if (sparse) {
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
        const uint32_t index_bytes = component_bytes(index_type);
        if ((uint64_t)count > a.count || (uint64_t)count * index_bytes > index_room || (uint64_t)count * element > value_room) {
            return fail(g->r, NULL, "has sparse %s that run past their buffer view's end", what);
        }
        for (uint64_t k = 0; k < (uint64_t)count; k++) {
            const uint32_t i = whole_component(index_at + k * index_bytes, index_type);
            if (i >= a.count) return fail(g->r, NULL, "has sparse %s for a vertex it hasn't got", what);
            for (uint32_t c = 0; c < take; c++) {
                out[(uint64_t)i * want + c] = component(value_at + k * element + (uint64_t)c * each, a.type, a.normalized);
            }
        }
    }
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
    *count = a.count;
    return out;
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
    if (mode < 4 || mode > 6) return true; // Points and lines: nothing a Mesh draws
    if (jget(jget(primitive, "extensions"), "KHR_draco_mesh_compression")) {
        return fail(r, "export it again with compression off: Tide reads a mesh's numbers as they are",
                    "has its geometry compressed with Draco, which Tide doesn't read");
    }
    const jnode *attributes = jget(primitive, "attributes");
    const jnode *positions = jget(attributes, "POSITION");
    if (!positions) return true; // Nothing to draw
    accessor counted;
    if (!accessor_of(g, positions, &counted, "positions")) return false;
    const uint64_t count = counted.count;
    if (count > VERTICES_MOST) {
        return fail(r, "a mesh that big is better split into several files, each a Mesh of its own",
                    "has more than %u vertices, the most one Mesh holds", (unsigned)VERTICES_MOST);
    }
    part p = {.count = (uint32_t)count};
    uint32_t *listed = NULL;
    bool ok = true;
    p.position = zeroed((size_t)count, 3u * sizeof(float));
    p.color = zeroed((size_t)count, 4u * sizeof(float));
    ok = read_floats(g, positions, 3u, 0.0f, count, p.position, "positions");
    const jnode *normals = jget(attributes, "NORMAL"), *tangents = jget(attributes, "TANGENT");
    const jnode *uvs = jget(attributes, "TEXCOORD_0"), *uvs2 = jget(attributes, "TEXCOORD_1"), *colors = jget(attributes, "COLOR_0");
    if (ok && normals) {
        p.normal = zeroed((size_t)count, 3u * sizeof(float));
        ok = read_floats(g, normals, 3u, 0.0f, count, p.normal, "normals");
    }
    if (ok && tangents) {
        p.tangent = zeroed((size_t)count, 4u * sizeof(float));
        ok = read_floats(g, tangents, 4u, 1.0f, count, p.tangent, "tangents");
    }
    if (ok && uvs) {
        p.uv = zeroed((size_t)count, 2u * sizeof(float));
        ok = read_floats(g, uvs, 2u, 0.0f, count, p.uv, "uvs");
    }
    if (ok && uvs2) {
        p.uv2 = zeroed((size_t)count, 2u * sizeof(float));
        ok = read_floats(g, uvs2, 2u, 0.0f, count, p.uv2, "second uvs");
    }
    if (ok && colors) {
        ok = read_floats(g, colors, 4u, 1.0f, count, p.color, "colors");
        g->b->m.has_colors = true;
    } else {
        for (uint64_t i = 0; i < count * 4u; i++) p.color[i] = 1.0f;
    }
    // Its triangles: the corners its indices name, or every three vertices in turn
    uint64_t listed_count = count;
    if (ok && jget(primitive, "indices")) {
        listed = read_indices(g, jget(primitive, "indices"), &listed_count);
        ok = listed != NULL;
    } else if (ok) {
        listed = zeroed((size_t)count, sizeof *listed);
        for (uint64_t i = 0; i < count; i++) listed[i] = (uint32_t)i;
    }
    for (uint64_t i = 0; ok && i < listed_count; i++) {
        if (listed[i] >= count) ok = fail(r, NULL, "has a triangle with a corner it has no vertex for");
    }
    const uint64_t triangles = !ok ? 0u : mode == 4 ? listed_count / 3u : listed_count >= 3u ? listed_count - 2u : 0u;
    if (ok && triangles * 3u > INDICES_MOST) {
        ok = fail(r, "a mesh that big is better split into several files, each a Mesh of its own",
                  "has more than %u triangles, the most one Mesh holds", (unsigned)(INDICES_MOST / 3u));
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
        for (uint64_t i = 0; i < count; i++) {
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
    jparser j = {.p = text, .end = text + length};
    const jnode *root = jvalue(&j, 0);
    jspace(&j);
    gltf g = {.r = r, .b = b, .root = root, .bin = bin, .bin_size = bin_size};
    bool ok = true;
    if (!root || j.bad || j.p != j.end || root->kind != J_OBJECT) {
        ok = fail(r, NULL, "isn't a glTF that reads: its description isn't JSON");
    }
    if (ok) {
        const jnode *version = jget(jget(root, "asset"), "version");
        if (!version || version->kind != J_STRING || version->text[0] != '2') {
            ok = fail(r, "export it as glTF 2.0, which is what tools write today",
                      "is glTF %.20s, and Tide reads glTF 2.0", version && version->kind == J_STRING ? version->text : "of no version");
        }
    }
    const jnode *required = ok ? jget(root, "extensionsRequired") : NULL;
    for (uint32_t i = 0; ok && i < jcount(required); i++) {
        const char *name = required->items[i]->kind == J_STRING ? required->items[i]->text : "";
        const char *by = strcmp(name, "KHR_draco_mesh_compression") == 0 ? "Draco"
                       : strcmp(name, "EXT_meshopt_compression") == 0 || strcmp(name, "KHR_meshopt_compression") == 0 ? "meshopt" : NULL;
        if (by) {
            ok = fail(r, "export it again with compression off: Tide reads a mesh's numbers as they are",
                      "has its geometry compressed with %s, which Tide doesn't read", by);
        }
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
                    if (jwhole(children->items[k], &at) && at < (int64_t)g.node_count) child[at] = true;
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
    if (header[1] != 2u) {
        return fail(r, "export it as glTF 2.0, which is what tools write today", "is a .glb of version %u, and Tide reads glTF 2.0",
                    (unsigned)header[1]);
    }
    if (header[2] > size) return fail(r, NULL, "isn't a .glb that reads: it says it's longer than it is");
    const uint8_t *json = NULL, *bin = NULL;
    uint32_t json_size = 0, bin_size = 0;
    for (uint64_t at = 12u; at + 8u <= header[2];) {
        uint32_t chunk[2];
        memcpy(chunk, data + at, sizeof chunk);
        if ((uint64_t)chunk[0] > header[2] - at - 8u) return fail(r, NULL, "isn't a .glb that reads: a chunk runs past its end");
        if (chunk[1] == 0x4E4F534Au && !json) {
            json = data + at + 8u;
            json_size = chunk[0];
        } else if (chunk[1] == 0x004E4942u && !bin) {
            bin = data + at + 8u;
            bin_size = chunk[0];
        }
        at += 8u + (((uint64_t)chunk[0] + 3u) & ~(uint64_t)3u);
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

static void push_floats(floats *list, const float *values, const uint32_t count)
{
    if (list->count + count > list->room) {
        list->room = list->room ? list->room * 2u : 1024u;
        while (list->room < list->count + count) list->room *= 2u;
        list->items = realloc(list->items, (size_t)list->room * sizeof(float));
        if (!list->items) abort();
    }
    memcpy(list->items + list->count, values, (size_t)count * sizeof(float));
    list->count += count;
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
    int line_number = 0;
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
                    ok = fail(r, NULL, "isn't an OBJ that reads: line %d has '%.*s' where a number goes", line_number,
                              (int)(to - from > 40 ? 40 : to - from), from);
                }
            }
            if (ok && count < (uv ? 1u : 3u)) {
                ok = fail(r, NULL, "isn't an OBJ that reads: line %d has %u numbers, and a %s has %s", line_number, (unsigned)count,
                          uv ? "uv" : position ? "vertex" : "normal", uv ? "one or two" : "three");
            }
            if (!ok) break;
            if (position) {
                // A color after the position, which some tools write: x y z r g b, or with a weight before it
                static const float white[3] = {1.0f, 1.0f, 1.0f};
                push_floats(&o.positions, numbers, 3u);
                push_floats(&o.colors, count >= 6u ? numbers + (count == 7u ? 4 : 3) : white, 3u);
                o.colored |= count >= 6u;
            } else if (normal) {
                push_floats(&o.normals, numbers, 3u);
            } else {
                push_floats(&o.uvs, numbers, 2u);
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
                ok = fail(r, NULL, "isn't an OBJ that reads: the face on line %d names '%.*s', and there's no such vertex, uv or "
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
    const size_t n = strlen(name), e = strlen(extension);
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
    } else if (ends_with(name, ".gltf")) {
        ok = read_gltf(&r, &b, (const char *)data, size, NULL, 0);
    } else if (ends_with(name, ".obj")) {
        ok = read_obj(&r, &b, (const char *)data, size);
    } else {
        ok = fail(&r, "a Mesh asset is glTF, a .glb or a .gltf, or a Wavefront .obj: export it as one of those",
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
