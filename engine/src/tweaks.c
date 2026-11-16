#include "tide/tweaks.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A dev build's tweaks as a host takes them (tide/tweaks.h): the values tidec works out from the .tweaks files, and
// a value the dev menu changed, written back into its file as Tide writes it.

bool tide_tweaks_load(const tide_tweaks_api *api, void *values, const void *bytes, const size_t length)
{
    const size_t head = 8u + 8u + 4u + 4u;
    if (length != head + api->size || memcmp(bytes, TIDE_TWEAKS_MAGIC, 8) != 0) return false;
    uint64_t layout;
    uint32_t size;
    memcpy(&layout, (const uint8_t *)bytes + 8, 8);
    memcpy(&size, (const uint8_t *)bytes + 16, 4);
    if (layout != api->layout || size != api->size) return false;
    memcpy(values, (const uint8_t *)bytes + head, api->size);
    return true;
}

typedef struct writer {
    char *out;
    size_t room, length;
} writer;

static void put(writer *w, const char *format, ...)
{
    char text[256];
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

// A float or a double as a Tide literal: the shortest digits that read back the same, with no exponent, which
// Tide's numbers don't have; a double with its d
static void put_real(writer *w, const double v, const bool is_double)
{
    char text[400];
    if (v != v || v - v != 0.0) {
        put(w, is_double ? "0d" : "0"); // Not a number a file can hold
        return;
    }
    snprintf(text, sizeof text, is_double ? "%.17g" : "%.9g", v);
    if (strchr(text, 'e') || strchr(text, 'E')) {
        snprintf(text, sizeof text, "%.*f", is_double ? 17 : 9, v);
        char *dot = strchr(text, '.');
        if (dot) {
            char *end = text + strlen(text) - 1;
            while (end > dot && *end == '0') *end-- = '\0';
            if (end == dot) *end = '\0';
        }
    }
    put(w, "%s%s", text, is_double ? "d" : "");
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
        put_real(w, (double)x, false);
        break;
    }
    case TIDE_TWEAK_DOUBLE: {
        double x;
        memcpy(&x, at + 8u * i, 8);
        put_real(w, x, true);
        break;
    }
    default:
        break;
    }
}

size_t tide_tweaks_format(const tide_tweak *t, const void *values, char *out, const size_t capacity)
{
    writer w = {out, capacity, 0};
    const uint8_t *at = (const uint8_t *)values + t->offset;
    if (t->kind == TIDE_TWEAK_BOOL) put(&w, "%s", *at ? "true" : "false");
    else if (t->kind == TIDE_TWEAK_ENUM) {
        const int32_t x = enum_at(at, t->size);
        const char *member = NULL;
        for (uint32_t k = 0; k < t->member_count; k++) {
            if (t->member_values[k] == x) member = t->members[k];
        }
        if (member) put(&w, "%s.%s", t->type, member);
        else put(&w, "%s(%ld)", t->type, (long)x); // A value no member has: the enum's conversion
    } else if (t->count == 1) put_component(&w, t, at, 0);
    else {
        put(&w, "%s(", t->type);
        for (uint32_t k = 0; k < t->count; k++) {
            if (k) put(&w, ", ");
            put_component(&w, t, at, k);
        }
        put(&w, ")");
    }
    if (capacity) out[w.length < capacity ? w.length : capacity - 1u] = '\0';
    return w.length;
}

// Where `name` is set in `text`: the line that starts with it (after blanks), then '='; the value is from past the
// '=' and its blanks to the ';' that ends it. False when no line sets it.
static bool find_entry(const char *text, const size_t length, const char *name, size_t *from, size_t *to)
{
    const size_t n = strlen(name);
    size_t line = 0;
    while (line < length) {
        size_t at = line;
        while (at < length && (text[at] == ' ' || text[at] == '\t')) at++;
        if (at + n <= length && memcmp(text + at, name, n) == 0) {
            size_t after = at + n;
            while (after < length && (text[after] == ' ' || text[after] == '\t')) after++;
            if (after < length && text[after] == '=') {
                after++;
                while (after < length && (text[after] == ' ' || text[after] == '\t')) after++;
                size_t end = after;
                while (end < length && text[end] != ';') end++;
                if (end < length) {
                    while (end > after && (text[end - 1] == ' ' || text[end - 1] == '\t')) end--;
                    *from = after;
                    *to = end;
                    return true;
                }
            }
        }
        while (line < length && text[line] != '\n') line++;
        line++;
    }
    return false;
}

bool tide_tweaks_save(const tide_tweak *t, const void *values)
{
    if (!t->file || !t->file[0]) return false;
    char value[256];
    if (tide_tweaks_format(t, values, value, sizeof value) >= sizeof value) return false;
    char *text = NULL;
    size_t length = 0;
    FILE *f = fopen(t->file, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        const long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        text = malloc((size_t)(size > 0 ? size : 0) + 1);
        if (!text) {
            fclose(f);
            return false;
        }
        length = size > 0 ? fread(text, 1, (size_t)size, f) : 0;
        fclose(f);
    }
    size_t from, to;
    const bool found = text && find_entry(text, length, t->name, &from, &to);
    f = fopen(t->file, "wb");
    if (!f) {
        free(text);
        return false;
    }
    bool ok = true;
    if (found) {
        ok = fwrite(text, 1, from, f) == from && fputs(value, f) >= 0 && fwrite(text + to, 1, length - to, f) == length - to;
    } else {
        if (length) ok = fwrite(text, 1, length, f) == length;
        if (length && text[length - 1] != '\n') ok = ok && fputc('\n', f) != EOF;
        ok = ok && fprintf(f, "%s = %s;\n", t->name, value) > 0;
    }
    if (fclose(f) != 0) ok = false;
    free(text);
    return ok;
}
