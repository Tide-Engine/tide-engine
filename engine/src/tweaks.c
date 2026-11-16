#include "tide/tweaks.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// tweaks.json: an object of values by the tweaks' names, as tidec reads it too (compiler/src/tweaks.c). A value
// read here goes to the match through the server (TideSetTweaks), so what each machine makes of the text is the
// sender's alone: strtod's reading is fine.

typedef struct cursor {
    const char *at, *end;
} cursor;

static void blank(cursor *c)
{
    while (c->at < c->end && (*c->at == ' ' || *c->at == '\t' || *c->at == '\r' || *c->at == '\n')) c->at++;
}

static bool take(cursor *c, const char ch)
{
    blank(c);
    if (c->at >= c->end || *c->at != ch) return false;
    c->at++;
    return true;
}

// A string, no escapes: its characters into `out` (cut to fit), false when there's no string here
static bool string(cursor *c, char *out, const size_t room)
{
    if (!take(c, '"')) return false;
    size_t n = 0;
    while (c->at < c->end && *c->at != '"') {
        if (*c->at == '\\' || *c->at == '\n') return false;
        if (n + 1 < room) out[n++] = *c->at;
        c->at++;
    }
    out[n] = '\0';
    return take(c, '"');
}

static bool number(cursor *c, double *v)
{
    blank(c);
    char text[64];
    size_t n = 0;
    while (c->at < c->end && strchr("+-0123456789.eE", *c->at) && n + 1 < sizeof text) text[n++] = *c->at++;
    text[n] = '\0';
    if (!n) return false;
    char *stop = NULL;
    *v = strtod(text, &stop);
    return stop == text + n && isfinite(*v);
}

static bool word(cursor *c, const char *w)
{
    blank(c);
    const size_t n = strlen(w);
    if ((size_t)(c->end - c->at) < n || memcmp(c->at, w, n) != 0) return false;
    c->at += n;
    return true;
}

// Past one value of any shape
static bool skip(cursor *c)
{
    char text[256];
    double v;
    blank(c);
    if (c->at >= c->end) return false;
    if (*c->at == '"') return string(c, text, sizeof text);
    if (*c->at == '[') {
        c->at++;
        if (take(c, ']')) return true;
        do {
            if (!skip(c)) return false;
        } while (take(c, ','));
        return take(c, ']');
    }
    if (word(c, "true") || word(c, "false") || word(c, "null")) return true;
    return number(c, &v);
}

static void store(const tide_tweak *t, uint8_t *at, const uint32_t i, const double v)
{
    switch (t->kind) {
    case TIDE_TWEAK_INT: {
        const int32_t x = v >= 2147483647.0 ? INT32_MAX : v <= -2147483648.0 ? INT32_MIN : (int32_t)v;
        memcpy(at + 4u * i, &x, 4);
        break;
    }
    case TIDE_TWEAK_LONG: {
        const int64_t x = v >= 9.2233720368547758e18 ? INT64_MAX : v <= -9.2233720368547758e18 ? INT64_MIN : (int64_t)v;
        memcpy(at + 8u * i, &x, 8);
        break;
    }
    case TIDE_TWEAK_BYTE: {
        const uint8_t x = v >= 255.0 ? 255u : v <= 0.0 ? 0u : (uint8_t)v;
        at[i] = x;
        break;
    }
    case TIDE_TWEAK_FLOAT: {
        const float x = (float)v;
        memcpy(at + 4u * i, &x, 4);
        break;
    }
    case TIDE_TWEAK_DOUBLE:
        memcpy(at + 8u * i, &v, 8);
        break;
    default:
        break;
    }
}

// An enum's value, as its storage is: a uint8_t, a uint16_t or an int32_t
static void put_enum(uint8_t *at, const uint32_t size, const int32_t v)
{
    if (size == 1) {
        const uint8_t x = (uint8_t)v;
        memcpy(at, &x, 1);
    } else if (size == 2) {
        const uint16_t x = (uint16_t)v;
        memcpy(at, &x, 2);
    } else memcpy(at, &v, 4);
}

static int32_t enum_at(const uint8_t *at, const uint32_t size)
{
    if (size == 1) return at[0];
    if (size == 2) {
        uint16_t x;
        memcpy(&x, at, 2);
        return x;
    }
    int32_t x;
    memcpy(&x, at, 4);
    return x;
}

// One tweak's value, into `values`; false, with nothing changed, when it isn't of the tweak's shape
static bool value(const tide_tweak *t, cursor *c, uint8_t *values)
{
    uint8_t *at = values + t->offset;
    if (t->kind == TIDE_TWEAK_BOOL) {
        const bool yes = word(c, "true");
        if (!yes && !word(c, "false")) return skip(c) && false;
        *at = yes ? 1u : 0u;
        return true;
    }
    if (t->kind == TIDE_TWEAK_ENUM) {
        char name[128];
        if (!string(c, name, sizeof name)) return skip(c) && false;
        for (uint32_t i = 0; i < t->member_count; i++) {
            if (strcmp(t->members[i], name) != 0) continue;
            put_enum(at, t->size, t->member_values[i]);
            return true;
        }
        return false;
    }
    double v[4];
    if (t->count == 1) {
        if (!number(c, &v[0])) return skip(c) && false;
    } else {
        if (!take(c, '[')) return skip(c) && false;
        for (uint32_t i = 0; i < t->count; i++) {
            if ((i && !take(c, ',')) || !number(c, &v[i])) return false;
        }
        if (!take(c, ']')) return false;
    }
    for (uint32_t i = 0; i < t->count; i++) store(t, at, i, v[i]);
    return true;
}

bool tide_tweaks_read(const tide_tweaks_api *api, void *values, const char *text, const size_t length)
{
    memcpy(values, api->code(), api->size);
    cursor c = {text, text + length};
    if (length >= 3 && (unsigned char)text[0] == 0xEF) c.at += 3; // A byte-order mark
    if (!take(&c, '{')) return false;
    if (take(&c, '}')) return true;
    do {
        char name[256];
        if (!string(&c, name, sizeof name) || !take(&c, ':')) {
            memcpy(values, api->code(), api->size);
            return false;
        }
        const tide_tweak *t = NULL;
        for (uint32_t i = 0; i < api->count && !t; i++) {
            if (strcmp(api->tweaks[i].name, name) == 0) t = &api->tweaks[i];
        }
        if (!t) {
            if (!skip(&c)) {
                memcpy(values, api->code(), api->size);
                return false;
            }
            continue;
        }
        (void)value(t, &c, values);
    } while (take(&c, ','));
    if (!take(&c, '}')) {
        memcpy(values, api->code(), api->size);
        return false;
    }
    return true;
}

typedef struct writer {
    char *out;
    size_t room, length;
} writer;

static void put(writer *w, const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (n <= 0) return;
    const size_t len = (size_t)n < sizeof text ? (size_t)n : sizeof text - 1u;
    if (w->room && w->length < w->room - 1u) {
        const size_t fits = w->room - 1u - w->length;
        memcpy(w->out + w->length, text, len < fits ? len : fits);
    }
    w->length += len;
}

static void put_component(writer *w, const tide_tweak *t, const uint8_t *at, const uint32_t i)
{
    switch (t->kind) {
    case TIDE_TWEAK_INT: {
        int32_t x;
        memcpy(&x, at + 4u * i, 4);
        put(w, "%ld", (long)x);
        break;
    }
    case TIDE_TWEAK_LONG: {
        int64_t x;
        memcpy(&x, at + 8u * i, 8);
        put(w, "%lld", (long long)x);
        break;
    }
    case TIDE_TWEAK_BYTE:
        put(w, "%u", (unsigned)at[i]);
        break;
    case TIDE_TWEAK_FLOAT: {
        float x;
        memcpy(&x, at + 4u * i, 4);
        put(w, "%.9g", (double)x);
        break;
    }
    case TIDE_TWEAK_DOUBLE: {
        double x;
        memcpy(&x, at + 8u * i, 8);
        put(w, "%.17g", x);
        break;
    }
    default:
        break;
    }
}

static size_t component_size(const tide_tweak *t)
{
    return t->size; // One component's bytes, as tidec says
}

size_t tide_tweaks_write(const tide_tweaks_api *api, const void *values, char *out, const size_t capacity)
{
    writer w = {out, capacity, 0};
    const uint8_t *now = values;
    const uint8_t *code = api->code();
    put(&w, "{");
    bool first = true;
    for (uint32_t i = 0; i < api->count; i++) {
        const tide_tweak *t = &api->tweaks[i];
        const size_t size = component_size(t) * t->count;
        if (memcmp(now + t->offset, code + t->offset, size) == 0) continue; // As the code has it
        put(&w, "%s\n    \"%s\": ", first ? "" : ",", t->name);
        first = false;
        const uint8_t *at = now + t->offset;
        if (t->kind == TIDE_TWEAK_BOOL) put(&w, "%s", *at ? "true" : "false");
        else if (t->kind == TIDE_TWEAK_ENUM) {
            const int32_t x = enum_at(at, t->size);
            const char *member = "";
            for (uint32_t k = 0; k < t->member_count; k++) {
                if (t->member_values[k] == x) member = t->members[k];
            }
            put(&w, "\"%s\"", member);
        } else if (t->count == 1) put_component(&w, t, at, 0);
        else {
            put(&w, "[");
            for (uint32_t k = 0; k < t->count; k++) {
                if (k) put(&w, ", ");
                put_component(&w, t, at, k);
            }
            put(&w, "]");
        }
    }
    put(&w, first ? "}\n" : "\n}\n");
    if (capacity) out[w.length < capacity ? w.length : capacity - 1u] = '\0';
    return w.length;
}
