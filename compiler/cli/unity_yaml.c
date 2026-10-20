// Unity's text assets, read as a tree of maps, sequences and scalars (unity_yaml.h).

#include "unity_yaml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct line {
    const char *text; // Where it starts
    int length;       // Without its line break
    int indent;       // Spaces before what it says
} line;

typedef struct reader {
    line *lines;
    int count;
    int at; // The next line
} reader;

static void *grab(const size_t size)
{
    void *p = calloc(1, size ? size : 1);
    if (!p) abort();
    return p;
}

static char *copy(const char *text, const size_t n)
{
    char *out = grab(n + 1);
    memcpy(out, text, n);
    out[n] = '\0';
    return out;
}

static yaml_node *scalar(char *text)
{
    yaml_node *n = grab(sizeof *n);
    n->kind = YAML_SCALAR;
    n->text = text;
    return n;
}

static void add(yaml_node *n, char *key, yaml_node *item)
{
    n->keys = realloc(n->keys, sizeof *n->keys * (size_t)(n->count + 1));
    n->items = realloc(n->items, sizeof *n->items * (size_t)(n->count + 1));
    if (!n->keys || !n->items) abort();
    n->keys[n->count] = key;
    n->items[n->count++] = item;
}

static void free_node(yaml_node *n)
{
    if (!n) return;
    free(n->text);
    for (int i = 0; i < n->count; i++) {
        if (n->keys) free(n->keys[i]);
        free_node(n->items[i]);
    }
    free(n->keys);
    free(n->items);
    free(n);
}

static bool blank(const line *l)
{
    for (int i = l->indent; i < l->length; i++) {
        if (l->text[i] == '#') return true;
        if (l->text[i] != ' ' && l->text[i] != '\r') return false;
    }
    return true;
}

// The next line that says something, or NULL at the document's end.
static const line *peek(reader *r)
{
    while (r->at < r->count && blank(&r->lines[r->at])) r->at++;
    return r->at < r->count ? &r->lines[r->at] : NULL;
}

static bool is_item(const line *l)
{
    const char *t = l->text + l->indent;
    const int left = l->length - l->indent;
    return left >= 1 && t[0] == '-' && (left == 1 || t[1] == ' ');
}

// Where a map entry's `:` is in `t` (n characters), outside quotes and flow, or -1.
static int colon_of(const char *t, const int n)
{
    if (n > 0 && (t[0] == '\'' || t[0] == '"' || t[0] == '{' || t[0] == '[')) return -1;
    for (int i = 0; i < n; i++) {
        if (t[i] == ':' && (i + 1 == n || t[i + 1] == ' ')) return i;
    }
    return -1;
}

static int trimmed_length(const char *t, int n)
{
    while (n > 0 && (t[n - 1] == ' ' || t[n - 1] == '\r')) n--;
    return n;
}

// A flow value from `*t`: {a: b, ...}, [a, ...] or a scalar ending at `,`, `}` or `]`.
static yaml_node *flow(const char **t, const char *end);

static char *flow_scalar(const char **t, const char *end)
{
    while (*t < end && **t == ' ') (*t)++;
    if (*t < end && (**t == '\'' || **t == '"')) {
        const char quote = **t;
        (*t)++;
        const char *start = *t;
        char *out = grab((size_t)(end - start) + 1);
        int n = 0;
        while (*t < end) {
            if (**t == quote) {
                if (quote == '\'' && *t + 1 < end && (*t)[1] == '\'') {
                    out[n++] = '\'';
                    *t += 2;
                    continue;
                }
                (*t)++;
                break;
            }
            if (quote == '"' && **t == '\\' && *t + 1 < end) {
                (*t)++;
                const char e = **t;
                out[n++] = e == 'n' ? '\n' : e == 't' ? '\t' : e;
                (*t)++;
                continue;
            }
            out[n++] = *(*t)++;
        }
        out[n] = '\0';
        return out;
    }
    const char *start = *t;
    while (*t < end && **t != ',' && **t != '}' && **t != ']') (*t)++;
    return copy(start, (size_t)trimmed_length(start, (int)(*t - start)));
}

static yaml_node *flow(const char **t, const char *end)
{
    while (*t < end && **t == ' ') (*t)++;
    if (*t < end && (**t == '{' || **t == '[')) {
        const bool map = **t == '{';
        const char close = map ? '}' : ']';
        (*t)++;
        yaml_node *n = grab(sizeof *n);
        n->kind = map ? YAML_MAP : YAML_SEQ;
        for (;;) {
            while (*t < end && **t == ' ') (*t)++;
            if (*t >= end) break;
            if (**t == close) {
                (*t)++;
                break;
            }
            char *key = NULL;
            if (map) {
                const char *start = *t;
                while (*t < end && **t != ':' && **t != ',' && **t != close) (*t)++;
                key = copy(start, (size_t)trimmed_length(start, (int)(*t - start)));
                if (*t < end && **t == ':') (*t)++;
            }
            add(n, key, flow(t, end));
            while (*t < end && **t == ' ') (*t)++;
            if (*t < end && **t == ',') (*t)++;
        }
        return n;
    }
    return scalar(flow_scalar(t, end));
}

// A value written after `key:` or `- ` on line `l` from `from`, and the lines that go on with it, which are
// further in than `indent`.
static yaml_node *inline_value(reader *r, const line *l, const int from, const int indent)
{
    const char *t = l->text + from;
    int n = trimmed_length(t, l->length - from);
    while (n > 0 && *t == ' ') {
        t++;
        n--;
    }
    if (n > 0 && (*t == '{' || *t == '[')) {
        const char *at = t;
        return flow(&at, t + n);
    }
    // A scalar, which goes on over the lines further in than its key
    size_t capacity = (size_t)n + 1;
    char *joined = grab(capacity);
    memcpy(joined, t, (size_t)n);
    size_t size = (size_t)n;
    while (r->at < r->count) {
        const line *next = &r->lines[r->at];
        if (blank(next) || next->indent <= indent) break;
        const char *u = next->text + next->indent;
        const int m = trimmed_length(u, next->length - next->indent);
        if (size + (size_t)m + 2 > capacity) {
            capacity = (size + (size_t)m + 2) * 2;
            joined = realloc(joined, capacity);
            if (!joined) abort();
        }
        joined[size++] = ' ';
        memcpy(joined + size, u, (size_t)m);
        size += (size_t)m;
        r->at++;
    }
    joined[size] = '\0';
    if (size && (joined[0] == '\'' || joined[0] == '"')) {
        const char *at = joined;
        char *unquoted = flow_scalar(&at, joined + size);
        free(joined);
        return scalar(unquoted);
    }
    return scalar(joined);
}

static yaml_node *block(reader *r, int indent);

// The value after `key:` on line `l`: on the line, or the block under it.
static yaml_node *entry_value(reader *r, const line *l, const int colon, const int indent)
{
    const char *rest = l->text + colon + 1;
    if (trimmed_length(rest, l->length - colon - 1) > 0) {
        int spaces = 0;
        while (rest[spaces] == ' ') spaces++;
        if (colon + 1 + spaces < l->length && trimmed_length(rest + spaces, l->length - colon - 1 - spaces) > 0) {
            return inline_value(r, l, colon + 1, indent);
        }
    }
    const line *next = peek(r);
    if (next && (next->indent > indent || (next->indent == indent && is_item(next)))) return block(r, next->indent);
    return scalar(copy("", 0));
}

static yaml_node *block(reader *r, const int indent)
{
    const line *first = peek(r);
    if (!first) return scalar(copy("", 0));
    yaml_node *n = grab(sizeof *n);
    if (is_item(first)) {
        n->kind = YAML_SEQ;
        const line *l;
        while ((l = peek(r)) && l->indent == indent && is_item(l)) {
            r->at++;
            const int content = l->indent + 2;
            const char *t = l->text + content;
            const int left = l->length > content ? l->length - content : 0;
            const int colon = colon_of(t, trimmed_length(t, left));
            if (colon >= 0) {
                // A map whose first entry is on the dash's line, the rest under it
                yaml_node *map = grab(sizeof *map);
                map->kind = YAML_MAP;
                add(map, copy(t, (size_t)colon), entry_value(r, l, content + colon, content));
                const line *more;
                while ((more = peek(r)) && more->indent == content && !is_item(more)) {
                    r->at++;
                    const char *u = more->text + more->indent;
                    const int c = colon_of(u, trimmed_length(u, more->length - more->indent));
                    if (c < 0) continue;
                    add(map, copy(u, (size_t)c), entry_value(r, more, more->indent + c, content));
                }
                add(n, NULL, map);
            } else {
                add(n, NULL, left > 0 ? inline_value(r, l, content, indent) : block(r, indent + 2));
            }
        }
        return n;
    }
    n->kind = YAML_MAP;
    const line *l;
    while ((l = peek(r)) && l->indent == indent && !is_item(l)) {
        r->at++;
        const char *t = l->text + l->indent;
        const int colon = colon_of(t, trimmed_length(t, l->length - l->indent));
        if (colon < 0) continue;
        add(n, copy(t, (size_t)colon), entry_value(r, l, l->indent + colon, indent));
    }
    return n;
}

bool yaml_read(const char *text, yaml_file *out)
{
    *out = (yaml_file){0};
    if (strncmp(text, "%YAML", 5) != 0) return false; // Binary, or not Unity's
    // The lines, each with its indent
    int capacity = 1024, count = 0;
    line *lines = grab(sizeof *lines * (size_t)capacity);
    for (const char *p = text; *p;) {
        const char *end = strchr(p, '\n');
        const int length = end ? (int)(end - p) : (int)strlen(p);
        if (count == capacity) {
            capacity *= 2;
            lines = realloc(lines, sizeof *lines * (size_t)capacity);
            if (!lines) abort();
        }
        int indent = 0;
        while (indent < length && p[indent] == ' ') indent++;
        lines[count++] = (line){p, length, indent};
        p = end ? end + 1 : p + length;
    }
    // Each document from its `--- !u!` line to the next
    for (int i = 0; i < count;) {
        if (strncmp(lines[i].text, "--- !u!", 7) != 0) {
            i++;
            continue;
        }
        yaml_document d = {0};
        char header[128];
        const int n = lines[i].length < (int)sizeof header - 1 ? lines[i].length : (int)sizeof header - 1;
        memcpy(header, lines[i].text, (size_t)n);
        header[n] = '\0';
        long long file_id = 0;
        sscanf(header, "--- !u!%d &%lld", &d.class_id, &file_id);
        d.file_id = (int64_t)file_id;
        d.stripped = strstr(header, " stripped") != NULL;
        int end = i + 1;
        while (end < count && strncmp(lines[end].text, "--- ", 4) != 0) end++;
        reader r = {lines + i + 1, end - i - 1, 0};
        yaml_node *top = block(&r, 0);
        if (top->kind == YAML_MAP && top->count >= 1) {
            d.type = top->keys[0];
            d.body = top->items[0];
            top->keys[0] = NULL;
            top->items[0] = NULL;
        }
        free_node(top);
        out->documents = realloc(out->documents, sizeof *out->documents * (size_t)(out->count + 1));
        if (!out->documents) abort();
        out->documents[out->count++] = d;
        i = end;
    }
    free(lines);
    return true;
}

void yaml_free(yaml_file *file)
{
    for (int i = 0; i < file->count; i++) {
        free(file->documents[i].type);
        free_node(file->documents[i].body);
    }
    free(file->documents);
    *file = (yaml_file){0};
}

const yaml_node *yaml_get(const yaml_node *map, const char *key)
{
    if (!map || map->kind != YAML_MAP) return NULL;
    for (int i = 0; i < map->count; i++) {
        if (map->keys[i] && strcmp(map->keys[i], key) == 0) return map->items[i];
    }
    return NULL;
}

const char *yaml_text(const yaml_node *map, const char *key, const char *otherwise)
{
    const yaml_node *n = yaml_get(map, key);
    return n && n->kind == YAML_SCALAR ? n->text : otherwise;
}

double yaml_number(const yaml_node *map, const char *key, const double otherwise)
{
    const char *t = yaml_text(map, key, NULL);
    if (!t || !*t) return otherwise;
    char *end = NULL;
    const double v = strtod(t, &end);
    return end != t ? v : otherwise;
}

int64_t yaml_reference(const yaml_node *node, char guid[33])
{
    guid[0] = '\0';
    if (!node || node->kind != YAML_MAP) return 0;
    const char *g = yaml_text(node, "guid", "");
    snprintf(guid, 33, "%s", g);
    const char *id = yaml_text(node, "fileID", "0");
    return (int64_t)strtoll(id, NULL, 10);
}
