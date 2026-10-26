#pragma once

// Shared glTF JSON and deterministic decimal parsing. Internal to importers.
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "tide/page.h"

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

static bool gltf_number(const char *s, const char *end, float *out)
{
    double v;
    if (!parse_double(s, end, &v)) return false;
    *out = v >= -(double)FLT_MAX && v <= (double)FLT_MAX ? (float)v : 0.0f; // NaN and what's too big compare false
    return true;
}

// ---------------------------------------------------------------------------
// JSON, for a glTF's description: a tree in memory of its own, freed whole.

typedef enum jkind { J_NULL, J_BOOL, J_NUMBER, J_STRING, J_ARRAY, J_OBJECT } jkind;

// An object's key, for looking keys up by halves: `index` is its place among the object's.
typedef struct jkey {
    const char *key;
    uint32_t index;
} jkey;

typedef struct jnode {
    jkind kind;
    bool truth;
    const char *text; // A number's text as written; a string's with its escapes undone and a NUL after it
    uint32_t length;
    struct jnode **items; // An array's, or an object's values
    const char **keys;    // An object's
    const jkey *sorted;   // An object's keys in order, where it has more than JSORT_FROM: NULL otherwise
    uint32_t count;
} jnode;

// An object with more keys than this keeps them sorted as well, so a lookup is
// some halvings of them, not a walk through all of them: a file whose objects
// have a million keys, looked up once for each of a million others, would
// otherwise take hours. Fewer keys are walked, which is quicker for them.
#define JSORT_FROM 8u

// By the key as strcmp orders them, then by its place, so of keys that are the
// same the first comes first: a lookup finds what a walk would.
static bool jkey_before(const jkey *x, const jkey *y)
{
    const int by_key = strcmp(x->key, y->key);
    return by_key ? by_key < 0 : x->index < y->index;
}

// Sorts `count` keys by merging runs of them (`spare` is as many), which takes
// count times its logarithm comparisons whatever the keys are: the C library's
// qsort can take count squared for keys a file chose.
static jkey *jkey_sort(jkey *keys, jkey *spare, const uint32_t count)
{
    for (uint64_t run = 1; run < count; run *= 2u) {
        for (uint64_t from = 0; from < count; from += run * 2u) {
            const uint64_t middle = from + run < count ? from + run : count;
            const uint64_t end = from + run * 2u < count ? from + run * 2u : count;
            uint64_t a = from, b = middle, to = from;
            while (a < middle && b < end) spare[to++] = jkey_before(&keys[b], &keys[a]) ? keys[b++] : keys[a++];
            while (a < middle) spare[to++] = keys[a++];
            while (b < end) spare[to++] = keys[b++];
        }
        jkey *swap = keys;
        keys = spare;
        spare = swap;
    }
    return keys;
}

typedef struct jblock {
    struct jblock *next;
} jblock;

typedef struct jparser {
    const char *p, *end;
    jblock *blocks;
    char *room;
    size_t left;
    uint64_t values_left; // How many more values the importer reads: past them, bad and too_many
    bool bad, too_many;
} jparser;

static void *jalloc(jparser *j, const size_t size)
{
    const size_t need = (size + 15u) & ~(size_t)15u;
    if (need > j->left) {
        const size_t block = need > 65536u ? need : 65536u;
        jblock *b = tide_alloc(sizeof(jblock) + 16u + block);
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
    if (j->p >= j->end || depth > 64 || !j->values_left) {
        j->too_many = j->p < j->end && depth <= 64;
        j->bad = true;
        return NULL;
    }
    j->values_left--;
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
                const size_t old_room = room;
                room = room ? room * 2u : 8u;
                items = tide_realloc(items, old_room * sizeof *items, room * sizeof *items);
                keys = tide_realloc(keys, old_room * sizeof *keys, room * sizeof *keys);
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
                if (count > JSORT_FROM) {
                    jkey *sorted = jalloc(j, count * sizeof *sorted), *spare = jalloc(j, count * sizeof *spare);
                    for (uint32_t i = 0; i < count; i++) sorted[i] = (jkey){keys[i], i};
                    v->sorted = jkey_sort(sorted, spare, count);
                }
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
    if (object->sorted) { // The first of its keys not before `key`, which is the first `key` there is
        uint32_t low = 0, high = object->count;
        while (low < high) {
            const uint32_t middle = low + (high - low) / 2u;
            if (strcmp(object->sorted[middle].key, key) < 0) low = middle + 1u;
            else high = middle;
        }
        return low < object->count && strcmp(object->sorted[low].key, key) == 0 ? object->items[object->sorted[low].index] : NULL;
    }
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
    return v && v->kind == J_NUMBER && gltf_number(v->text, v->text + v->length, &f) ? f : otherwise;
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
