// .tidegraph files: reading one's JSON and writing the Tide it stands for (notes/visual-scripting.md).

#include "graph.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

void graph_binary_stdout(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

bool graph_is_path(const char *path)
{
    const size_t n = strlen(path);
    return n >= 10 && strcmp(path + n - 10, ".tidegraph") == 0;
}

// ---------------------------------------------------------------------------
// JSON

typedef struct json_reader {
    const source *src;
    const char *p, *start, *end;
    int line;
    const char *line_start;
    int depth;
    bool failed;
} json_reader;

static loc reader_loc(const json_reader *r)
{
    return (loc){r->line, (int)(r->p - r->line_start) + 1, r->src->file};
}

static void json_fail(json_reader *r, const char *what)
{
    if (!r->failed) diag_error(reader_loc(r), "this graph's file isn't JSON tide can read: %s", what);
    r->failed = true;
}

static void json_space(json_reader *r)
{
    while (r->p < r->end) {
        const char c = *r->p;
        if (c == '\n') {
            r->line++;
            r->line_start = r->p + 1;
        } else if (c != ' ' && c != '\t' && c != '\r') {
            break;
        }
        r->p++;
    }
}

static graph_json *json_value(json_reader *r);

static int hex_value(const char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool json_hex4(json_reader *r, unsigned *out)
{
    if (r->end - r->p < 4) return false;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        const int h = hex_value(r->p[i]);
        if (h < 0) return false;
        v = v * 16u + (unsigned)h;
    }
    r->p += 4;
    *out = v;
    return true;
}

static void put_utf8(sb *b, const unsigned cp)
{
    char tmp[4];
    size_t n;
    if (cp < 0x80) {
        tmp[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        tmp[0] = (char)(0xF0 | (cp >> 18));
        tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    sb_putn(b, tmp, n);
}

static bool json_string(json_reader *r, str *out)
{
    r->p++; // The quote
    sb b = {0};
    while (r->p < r->end && *r->p != '"') {
        const char c = *r->p;
        if ((unsigned char)c < 0x20) {
            json_fail(r, "a control character inside a string; write it as an escape, like \\n");
            return false;
        }
        if (c != '\\') {
            sb_putn(&b, r->p, 1);
            r->p++;
            continue;
        }
        r->p++;
        if (r->p >= r->end) break;
        const char e = *r->p++;
        switch (e) {
        case '"': sb_put(&b, "\""); break;
        case '\\': sb_put(&b, "\\"); break;
        case '/': sb_put(&b, "/"); break;
        case 'b': sb_put(&b, "\b"); break;
        case 'f': sb_put(&b, "\f"); break;
        case 'n': sb_put(&b, "\n"); break;
        case 'r': sb_put(&b, "\r"); break;
        case 't': sb_put(&b, "\t"); break;
        case 'u': {
            unsigned cp;
            if (!json_hex4(r, &cp)) {
                json_fail(r, "\\u takes four hexadecimal digits");
                return false;
            }
            if (cp >= 0xD800 && cp < 0xDC00 && r->end - r->p >= 6 && r->p[0] == '\\' && r->p[1] == 'u') {
                r->p += 2;
                unsigned low;
                if (!json_hex4(r, &low) || low < 0xDC00 || low >= 0xE000) {
                    json_fail(r, "a \\u surrogate pair's second half is missing");
                    return false;
                }
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            }
            put_utf8(&b, cp);
            break;
        }
        default:
            json_fail(r, "an escape JSON doesn't have");
            return false;
        }
    }
    if (r->p >= r->end) {
        json_fail(r, "a string that doesn't end");
        return false;
    }
    r->p++;
    *out = (str){b.data ? b.data : "", (int)b.len};
    return true;
}

static graph_json *json_new(json_reader *r, const graph_json_kind kind)
{
    graph_json *v = NEW(graph_json);
    v->kind = kind;
    v->at = reader_loc(r);
    return v;
}

static graph_json *json_container(json_reader *r, const bool object)
{
    graph_json *v = json_new(r, object ? GJ_OBJECT : GJ_ARRAY);
    if (++r->depth > 256) {
        json_fail(r, "values nested more than 256 deep");
        return NULL;
    }
    r->p++;
    VEC(graph_json *) items = {0};
    VEC(str) keys = {0};
    json_space(r);
    if (r->p < r->end && *r->p == (object ? '}' : ']')) {
        r->p++;
        r->depth--;
        return v;
    }
    for (;;) {
        json_space(r);
        if (object) {
            if (r->p >= r->end || *r->p != '"') {
                json_fail(r, "an object's key goes in quotes");
                return NULL;
            }
            str key;
            if (!json_string(r, &key)) return NULL;
            json_space(r);
            if (r->p >= r->end || *r->p != ':') {
                json_fail(r, "':' after an object's key");
                return NULL;
            }
            r->p++;
            vec_push(keys, key);
        }
        graph_json *item = json_value(r);
        if (!item) return NULL;
        vec_push(items, item);
        json_space(r);
        if (r->p < r->end && *r->p == ',') {
            r->p++;
            continue;
        }
        if (r->p < r->end && *r->p == (object ? '}' : ']')) {
            r->p++;
            break;
        }
        json_fail(r, object ? "',' or '}' after an object's value" : "',' or ']' after an array's item");
        return NULL;
    }
    v->count = items.count;
    v->items = items.items;
    v->keys = keys.items;
    r->depth--;
    return v;
}

static bool word_at(const json_reader *r, const char *word)
{
    const size_t n = strlen(word);
    return (size_t)(r->end - r->p) >= n && memcmp(r->p, word, n) == 0;
}

static graph_json *json_value(json_reader *r)
{
    json_space(r);
    if (r->p >= r->end) {
        json_fail(r, "a value is missing at the end");
        return NULL;
    }
    const char c = *r->p;
    if (c == '{' || c == '[') return json_container(r, c == '{');
    if (c == '"') {
        graph_json *v = json_new(r, GJ_STRING);
        return json_string(r, &v->text) ? v : NULL;
    }
    if (word_at(r, "true") || word_at(r, "false")) {
        graph_json *v = json_new(r, GJ_BOOL);
        v->boolean = c == 't';
        r->p += v->boolean ? 4 : 5;
        return v;
    }
    if (word_at(r, "null")) {
        graph_json *v = json_new(r, GJ_NULL);
        r->p += 4;
        return v;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        graph_json *v = json_new(r, GJ_NUMBER);
        const char *start = r->p;
        if (*r->p == '-') r->p++;
        while (r->p < r->end && ((*r->p >= '0' && *r->p <= '9') || *r->p == '.' || *r->p == 'e' || *r->p == 'E'
                                 || *r->p == '+' || *r->p == '-')) {
            r->p++;
        }
        v->text = (str){start, (int)(r->p - start)};
        char *digits = str_to_cstr(v->text);
        char *after = NULL;
        v->number = strtod(digits, &after);
        if (!after || *after) {
            json_fail(r, "a number JSON doesn't write");
            return NULL;
        }
        return v;
    }
    json_fail(r, "a value: an object, an array, a string, a number, true, false or null");
    return NULL;
}

graph_json *graph_json_parse(const source *src)
{
    json_reader r = {.src = src, .p = src->text, .start = src->text, .end = src->text + src->len, .line = 1,
                     .line_start = src->text};
    if (src->len >= 3 && memcmp(r.p, "\xEF\xBB\xBF", 3) == 0) r.p += 3;
    graph_json *root = json_value(&r);
    if (!root) return NULL;
    json_space(&r);
    if (r.p < r.end) {
        json_fail(&r, "more after the file's value");
        return NULL;
    }
    return root;
}

const graph_json *graph_json_get(const graph_json *object, const char *key)
{
    if (!object || object->kind != GJ_OBJECT) return NULL;
    for (int i = 0; i < object->count; i++) {
        if (str_eq_c(object->keys[i], key)) return object->items[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// The map from what's written to where it came from

int graph_id_at(const graph_map *map, const loc at)
{
    if (!map || at.line < 1 || at.line > map->line_starts.count) return 0;
    const int offset = map->line_starts.items[at.line - 1] + (at.col > 0 ? at.col - 1 : 0);
    int best = 0, best_len = 0;
    for (int i = 0; i < map->spans.count; i++) {
        const graph_span *s = &map->spans.items[i];
        if (offset < s->start || offset >= s->end) continue;
        const int len = s->end - s->start;
        if (!best || len <= best_len) {
            best = s->id;
            best_len = len;
        }
    }
    return best;
}

static bool loc_before(const loc a, const loc b)
{
    return a.line < b.line || (a.line == b.line && a.col <= b.col);
}

int graph_id_at_json(const graph_map *map, const loc at)
{
    if (!map) return 0;
    int best = 0;
    loc best_at = {0};
    for (int i = 0; i < map->places.count; i++) {
        const graph_place *p = &map->places.items[i];
        if (!loc_before(p->at, at)) continue;
        if (!best || loc_before(best_at, p->at)) {
            best = p->id;
            best_at = p->at;
        }
    }
    return best;
}

loc graph_json_at(const graph_map *map, const int id)
{
    if (!map) return (loc){0};
    for (int i = 0; i < map->places.count; i++) {
        if (map->places.items[i].id == id) return map->places.items[i].at;
    }
    return (loc){0};
}

// ---------------------------------------------------------------------------
// Writing the Tide

// Precedence of what an expression's text is, lowest first, and what each place takes without parentheses
// (parser.c's grammar: a lambda's body and ?: are loosest, then ??, then the binary operators as C#'s, then a
// switch expression, prefix operators, postfix ones and plain values).
enum {
    P_LAMBDA,
    P_COND,
    P_COALESCE,
    P_OR,
    P_AND,
    P_BOR,
    P_XOR,
    P_BAND,
    P_EQ,
    P_REL,
    P_SHIFT,
    P_ADD,
    P_MUL,
    P_SWITCH,
    P_UNARY,
    P_POSTFIX,
    P_PRIMARY,
};

typedef struct id_table {
    int *ids;
    const graph_json **values;
    int cap, count;
} id_table;

static void table_put(id_table *t, const int id, const graph_json *v)
{
    if ((t->count + 1) * 2 > t->cap) {
        id_table bigger = {0};
        bigger.cap = t->cap ? t->cap * 2 : 64;
        bigger.ids = arena_alloc(sizeof(int) * (size_t)bigger.cap);
        bigger.values = arena_alloc(sizeof(graph_json *) * (size_t)bigger.cap);
        for (int i = 0; i < t->cap; i++) {
            if (t->values[i]) table_put(&bigger, t->ids[i], t->values[i]);
        }
        *t = bigger;
    }
    unsigned h = (unsigned)id * 2654435761u;
    for (;;) {
        const int i = (int)(h % (unsigned)t->cap);
        if (!t->values[i] || t->ids[i] == id) {
            if (!t->values[i]) t->count++;
            t->ids[i] = id;
            t->values[i] = v;
            return;
        }
        h++;
    }
}

static const graph_json *table_get(const id_table *t, const int id)
{
    if (!t->cap) return NULL;
    unsigned h = (unsigned)id * 2654435761u;
    for (;;) {
        const int i = (int)(h % (unsigned)t->cap);
        if (!t->values[i]) return NULL;
        if (t->ids[i] == id) return t->values[i];
        h++;
    }
}

typedef struct id_set {
    VEC(int) items;
} id_set;

static bool set_has(const id_set *s, const int id)
{
    for (int i = 0; i < s->items.count; i++) {
        if (s->items.items[i] == id) return true;
    }
    return false;
}

// An input with no wire: said once the text is written, at its place in it
typedef struct hole {
    int offset;
    char *message;
    const char *port;
    int index;
} hole;

typedef struct lw {
    source *src;
    graph_map *map;
    sb out;
    int indent;
    bool failed;
    VEC(hole) holes;
    // The graph being written: its nodes by id
    id_table nodes;
    const graph_json *graph;
    id_set exec_seen;   // Statements written, each from one place only
    id_set declared;    // Nodes that declare locals, written so far in this body
    id_set kept;        // eval nodes whose value another node reads: written as a local
    id_set visiting;    // Data nodes on the path being written, which a wire back to is a loop
} lw;

static int json_id(const graph_json *v)
{
    const graph_json *id = graph_json_get(v, "id");
    return id && id->kind == GJ_NUMBER ? (int)id->number : 0;
}

static void gerror(lw *w, const graph_json *at, const char *fmt, ...)
{
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof message, fmt, args);
    va_end(args);
    diag_error(at ? at->at : (loc){1, 1, w->src->file}, "%s", message);
    w->failed = true;
}

static const char *hole_port; // The hole being said: graph_hole_port
static int hole_index;

const char *graph_hole_port(int *index)
{
    if (index) *index = hole_index;
    return hole_port;
}

static void put(lw *w, const char *text);

// An input nothing is wired to: the node says so, and the rest of the file is still read around it. `index` is
// its place among the key's items, or -1.
static void put_hole(lw *w, const char *port, const int index, const char *fmt, ...)
{
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof message, fmt, args);
    va_end(args);
    const size_t len = strlen(message);
    char *kept = arena_alloc(len + 1);
    memcpy(kept, message, len + 1);
    const hole h = {(int)w->out.len, kept, port, index};
    vec_push(w->holes, h);
    put(w, GRAPH_HOLE);
}

static str jstr(const graph_json *v, const char *key)
{
    const graph_json *s = graph_json_get(v, key);
    return s && s->kind == GJ_STRING ? s->text : (str){NULL, 0};
}

static bool jbool(const graph_json *v, const char *key)
{
    const graph_json *b = graph_json_get(v, key);
    return b && b->kind == GJ_BOOL && b->boolean;
}

static const graph_json *jarr(const graph_json *v, const char *key)
{
    const graph_json *a = graph_json_get(v, key);
    return a && a->kind == GJ_ARRAY ? a : NULL;
}

// A wire: the id an input names, or 0 for none
static int jwire(const graph_json *v, const char *key)
{
    const graph_json *n = graph_json_get(v, key);
    return n && n->kind == GJ_NUMBER ? (int)n->number : 0;
}

static int item_wire(const graph_json *v)
{
    return v && v->kind == GJ_NUMBER ? (int)v->number : 0;
}

static void put(lw *w, const char *s)
{
    sb_put(&w->out, s);
}

static void put_str(lw *w, const str s)
{
    if (s.len) sb_putn(&w->out, s.ptr, (size_t)s.len);
}

static void line(lw *w)
{
    put(w, "\n");
    for (int i = 0; i < w->indent; i++) put(w, "    ");
}

static int mark(const lw *w)
{
    return (int)w->out.len;
}

static void span(lw *w, const int id, const int start)
{
    if (!id) return;
    const graph_span s = {start, (int)w->out.len, id};
    vec_push(w->map->spans, s);
}

// A required string: written, or said missing
static void put_required(lw *w, const graph_json *v, const char *key, const char *what)
{
    const str s = jstr(v, key);
    if (!s.len) {
        gerror(w, v, "%s has no %s", what, key);
        put(w, "_");
        return;
    }
    put_str(w, s);
}

static void put_comment(lw *w, const graph_json *v)
{
    const str c = jstr(v, "comment");
    if (!c.len) return;
    put(w, "//");
    if (c.ptr[0] != ' ') put(w, " ");
    for (int i = 0; i < c.len; i++) {
        if (c.ptr[i] == '\n') {
            line(w);
            put(w, "//");
            if (i + 1 < c.len && c.ptr[i + 1] != ' ' && c.ptr[i + 1] != '\n') put(w, " ");
        } else if (c.ptr[i] != '\r') {
            sb_putn(&w->out, c.ptr + i, 1);
        }
    }
    line(w);
}

static const graph_json *node_of(lw *w, const int id, const graph_json *from)
{
    const graph_json *n = table_get(&w->nodes, id);
    if (!n) gerror(w, from, "a wire goes to node %d, which this graph doesn't have", id);
    return n;
}

static bool kind_is(const graph_json *n, const char *k)
{
    return str_eq_c(jstr(n, "k"), k);
}

// What a wire comes from past the reroutes on it (knots, which pass their value on)
static int past_knots(lw *w, int id)
{
    for (int depth = 0; id && depth < 256; depth++) {
        const graph_json *n = table_get(&w->nodes, id);
        if (!n || !kind_is(n, "reroute")) break;
        id = jwire(n, "value");
    }
    return id;
}

// The schema (graph_schema.json, which the build embeds): every kind of node and its ports. Read once, into memory
// that lasts as long as tidec or the language server (arena_keep): it's the same each time, and lookups of a kind
// once read all of it again each, which in a long run (tidec --graph-roundtrip of every file) never let go
extern const unsigned char tide_graph_schema[];
extern const unsigned int tide_graph_schema_size;

const char *graph_schema_text(size_t *len)
{
    *len = tide_graph_schema_size;
    return (const char *)tide_graph_schema;
}

static const graph_json *schema_nodes; // Its "nodes", while a graph is read (graph_lower, graph_schema_hold), else NULL
static int schema_held;

static const graph_json *schema_root;  // ...and all of it

static atomic_flag schema_reading = ATOMIC_FLAG_INIT;
static const graph_json *schema_kept;

static const graph_json *read_schema_root(void)
{
    while (atomic_flag_test_and_set(&schema_reading)) {}
    if (!schema_kept) {
        source src = {.path = "<graph schema>"};
        src.text = graph_schema_text(&src.len);
        arena_keep(true);
        schema_kept = graph_json_parse(&src);
        arena_keep(false);
    }
    const graph_json *root = schema_kept;
    atomic_flag_clear(&schema_reading);
    return root;
}

static const graph_json *read_schema(void)
{
    return graph_json_get(read_schema_root(), "nodes");
}

void graph_schema_hold(const bool hold)
{
    if (hold) {
        if (!schema_held++ && !schema_nodes) {
            schema_root = read_schema_root();
            schema_nodes = graph_json_get(schema_root, "nodes");
        }
    } else if (!--schema_held) {
        schema_nodes = NULL;
        schema_root = NULL;
    }
}

bool graph_schema_numbers(const str name)
{
    const graph_json *shown = graph_json_get(schema_root ? schema_root : read_schema_root(), "inline");
    const graph_json *names = shown ? graph_json_get(shown, "numbersOf") : NULL;
    for (int i = 0; names && names->kind == GJ_ARRAY && i < names->count; i++) {
        if (names->items[i]->kind == GJ_STRING && str_eq(names->items[i]->text, name)) return true;
    }
    return false;
}

bool graph_schema_titled(const str kind)
{
    const graph_json *shown = graph_json_get(schema_root ? schema_root : read_schema_root(), "inline");
    const graph_json *kinds = shown ? graph_json_get(shown, "titled") : NULL;
    for (int i = 0; kinds && kinds->kind == GJ_ARRAY && i < kinds->count; i++) {
        if (kinds->items[i]->kind == GJ_STRING && str_eq(kinds->items[i]->text, kind)) return true;
    }
    return false;
}

int graph_schema_inline(const str kind)
{
    static const char *const lists[] = {"always", "path", "negative", "numbers", "named"};
    const graph_json *shown = graph_json_get(schema_root ? schema_root : read_schema_root(), "inline");
    for (int l = 0; shown && l < 5; l++) {
        const graph_json *kinds = graph_json_get(shown, lists[l]);
        for (int i = 0; kinds && kinds->kind == GJ_ARRAY && i < kinds->count; i++) {
            if (kinds->items[i]->kind == GJ_STRING && str_eq(kinds->items[i]->text, kind)) return l + 1;
        }
    }
    return 0;
}

static const graph_json *node_spec(const str k)
{
    const graph_json *nodes = schema_nodes ? schema_nodes : read_schema();
    for (int i = 0; nodes && i < nodes->count; i++) {
        const graph_json *spec = nodes->items[i];
        const graph_json *kind = graph_json_get(spec, "k");
        if (kind && str_eq(kind->text, k)) return spec;
    }
    return NULL;
}

static bool exec_kind(const str k)
{
    const graph_json *spec = node_spec(k);
    const graph_json *exec = spec ? graph_json_get(spec, "exec") : NULL;
    return exec && exec->kind == GJ_BOOL && exec->boolean;
}

// What a data node's text is, for parentheses
static int binary_prec(const str op)
{
    static const struct {
        const char *op;
        int prec;
    } ops[] = {{"||", P_OR}, {"&&", P_AND}, {"|", P_BOR}, {"^", P_XOR}, {"&", P_BAND}, {"==", P_EQ}, {"!=", P_EQ},
               {"<", P_REL}, {"<=", P_REL}, {">", P_REL}, {">=", P_REL}, {"<<", P_SHIFT}, {">>", P_SHIFT},
               {"+", P_ADD}, {"-", P_ADD}, {"*", P_MUL}, {"/", P_MUL}, {"%", P_MUL}};
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        if (str_eq_c(op, ops[i].op)) return ops[i].prec;
    }
    return -1;
}

static int prec_of(const graph_json *n)
{
    const str k = jstr(n, "k");
    if (str_eq_c(k, "lambda")) return P_LAMBDA;
    if (str_eq_c(k, "conditional")) return P_COND;
    if (str_eq_c(k, "coalesce")) return P_COALESCE;
    if (str_eq_c(k, "binary")) {
        const int p = binary_prec(jstr(n, "op"));
        return p < 0 ? P_PRIMARY : p;
    }
    if (str_eq_c(k, "is") || str_eq_c(k, "has")) return P_REL;
    if (str_eq_c(k, "switchExpr")) return P_SWITCH;
    if (str_eq_c(k, "unary") || str_eq_c(k, "try") || str_eq_c(k, "await")) return P_UNARY;
    if (str_eq_c(k, "step")) return jbool(n, "postfix") ? P_POSTFIX : P_UNARY;
    if (str_eq_c(k, "member") || str_eq_c(k, "call") || str_eq_c(k, "method") || str_eq_c(k, "index")
        || str_eq_c(k, "defaulted")) {
        return P_POSTFIX;
    }
    if (str_eq_c(k, "int") || str_eq_c(k, "float")) {
        const str t = jstr(n, "text");
        return t.len && t.ptr[0] == '-' ? P_UNARY : P_PRIMARY;
    }
    return P_PRIMARY;
}

static void expr(lw *w, int id, int min, const graph_json *from, const char *what);
static void stmts(lw *w, int id, const graph_json *from);

// A data input: the expression its wire comes from, or an error for none
static void input(lw *w, const graph_json *n, const char *key, const int min)
{
    const int id = jwire(n, key);
    if (!id) {
        put_hole(w, key, -1, "node %d's '%s' isn't connected", json_id(n), key);
        return;
    }
    expr(w, id, min, n, key);
}

static void put_type_args(lw *w, const graph_json *n)
{
    const graph_json *types = jarr(n, "typeArgs");
    if (!types || !types->count) return;
    put(w, "<");
    for (int i = 0; i < types->count; i++) {
        if (i) put(w, ", ");
        if (types->items[i]->kind == GJ_STRING) put_str(w, types->items[i]->text);
    }
    put(w, ">");
}

static void put_args(lw *w, const graph_json *n)
{
    const graph_json *args = jarr(n, "args");
    const graph_json *names = jarr(n, "argNames");
    put(w, "(");
    for (int i = 0; args && i < args->count; i++) {
        if (i) put(w, ", ");
        if (names && i < names->count && names->items[i]->kind == GJ_STRING && names->items[i]->text.len) {
            put_str(w, names->items[i]->text);
            put(w, ": ");
        }
        const int arg = item_wire(args->items[i]);
        if (!arg) {
            put_hole(w, "args", i, "node %d's argument %d isn't connected", json_id(n), i + 1);
            continue;
        }
        expr(w, arg, P_LAMBDA, n, "args");
    }
    put(w, ")");
}

static str local_name(const graph_json *decl)
{
    return kind_is(decl, "is") ? jstr(decl, "binding") : jstr(decl, "name");
}

// A text node's text, with each input put where its name stands as a name: in parentheses, so that it reads as
// one value wherever it goes
static void put_text(lw *w, const graph_json *n)
{
    const str text = jstr(n, "text");
    const graph_json *inputs = graph_json_get(n, "inputs");
    const char *p = text.ptr, *end = text.ptr + text.len;
    // Strings and comments aren't code; an interpolated string's values are, between its braces
    int interp_depth = 0; // Braces open in an interpolated string's value
    bool in_interp = false;
    char prev = 0; // The last character of code that isn't space, for `a.b`
    while (p && p < end) {
        const char c = *p;
        if (!in_interp || interp_depth > 0) {
            if (c == '/' && p + 1 < end && p[1] == '/') {
                while (p < end && *p != '\n') sb_putn(&w->out, p++, 1);
                continue;
            }
            if (c == '/' && p + 1 < end && p[1] == '*') {
                const char *close = p + 2;
                while (close + 1 < end && !(close[0] == '*' && close[1] == '/')) close++;
                close = close + 1 < end ? close + 2 : end;
                sb_putn(&w->out, p, (size_t)(close - p));
                p = close;
                continue;
            }
            if (c == '"' || c == '\'') {
                const char *q = p + 1;
                while (q < end && *q != c) q += *q == '\\' ? 2 : 1;
                if (q < end) q++;
                if (q > end) q = end;
                sb_putn(&w->out, p, (size_t)(q - p));
                p = q;
                prev = c;
                continue;
            }
            if (c == '$' && p + 1 < end && p[1] == '"' && !in_interp) {
                sb_putn(&w->out, p, 2);
                p += 2;
                in_interp = true;
                interp_depth = 0;
                continue;
            }
            if (in_interp && c == '{') interp_depth++;
            if (in_interp && c == '}') interp_depth--;
            if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                const char *q = p;
                while (q < end && (*q == '_' || (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9'))) q++;
                const str word = {p, (int)(q - p)};
                int wire = 0;
                for (int i = 0; inputs && inputs->kind == GJ_OBJECT && i < inputs->count && prev != '.'; i++) {
                    if (str_eq(inputs->keys[i], word)) wire = item_wire(inputs->items[i]);
                }
                if (wire) {
                    put(w, "(");
                    expr(w, wire, P_LAMBDA, n, "inputs");
                    put(w, ")");
                } else {
                    put_str(w, word);
                }
                p = q;
                prev = 'a';
                continue;
            }
            if (c == '\n') line(w); // Its lines at the indent of where it goes
            else if (c != '\r') sb_putn(&w->out, p, 1);
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') prev = c;
            p++;
            continue;
        }
        // Inside an interpolated string's text
        if (c == '\\' && p + 1 < end) {
            sb_putn(&w->out, p, 2);
            p += 2;
            continue;
        }
        if (c == '{' && p + 1 < end && p[1] == '{') {
            sb_putn(&w->out, p, 2);
            p += 2;
            continue;
        }
        if (c == '{') interp_depth = 1;
        if (c == '"') in_interp = false;
        sb_putn(&w->out, p, 1);
        prev = c;
        p++;
    }
}

static void expr_node(lw *w, const graph_json *n, const int id)
{
    const str k = jstr(n, "k");
    if (str_eq_c(k, "int") || str_eq_c(k, "float") || str_eq_c(k, "char")) {
        put_required(w, n, "text", "a number");
    } else if (str_eq_c(k, "string")) {
        put(w, "\"");
        put_str(w, jstr(n, "text"));
        put(w, "\"");
    } else if (str_eq_c(k, "bool")) {
        put(w, jbool(n, "value") ? "true" : "false");
    } else if (str_eq_c(k, "this") || str_eq_c(k, "default") || str_eq_c(k, "null")) {
        put_str(w, k);
    } else if (str_eq_c(k, "name")) {
        if (jbool(n, "local")) put(w, "local ");
        put_required(w, n, "name", "a name node");
    } else if (str_eq_c(k, "local")) {
        const int ref = jwire(n, "ref");
        const graph_json *decl = ref ? node_of(w, ref, n) : NULL;
        if (!decl) {
            if (!ref) gerror(w, n, "node %d reads a local, but not which", id);
            put(w, "_");
        } else {
            if (!set_has(&w->declared, ref)) {
                gerror(w, n, "node %d reads '" STR_FMT "' where it isn't declared: what declares it runs after this, or "
                             "not on the way here", id, STR_ARG(local_name(decl)));
            }
            put_str(w, local_name(decl));
        }
    } else if (str_eq_c(k, "member")) {
        if (jbool(n, "shorthand")) {
            put(w, ".");
        } else {
            input(w, n, "object", P_POSTFIX);
            put(w, ".");
        }
        put_required(w, n, "member", "a member node");
    } else if (str_eq_c(k, "call")) {
        if (jbool(n, "local")) put(w, "local ");
        put_required(w, n, "name", "a call");
        put_type_args(w, n);
        put_args(w, n);
    } else if (str_eq_c(k, "method")) {
        input(w, n, "object", P_POSTFIX);
        put(w, ".");
        put_required(w, n, "name", "a method call");
        put_type_args(w, n);
        put_args(w, n);
    } else if (str_eq_c(k, "outVar")) {
        put(w, "out ");
        if (jbool(n, "mut")) put(w, "mut ");
        const str type = jstr(n, "type");
        if (type.len) put_str(w, type);
        else put(w, "var");
        put(w, " ");
        put_required(w, n, "name", "an out variable");
        vec_push(w->declared.items, id);
    } else if (str_eq_c(k, "out")) {
        put(w, "out ");
        input(w, n, "target", P_POSTFIX);
    } else if (str_eq_c(k, "binary")) {
        const str op = jstr(n, "op");
        const int prec = binary_prec(op);
        if (prec < 0) {
            gerror(w, n, "node %d's operator '" STR_FMT "' isn't one of Tide's", id, STR_ARG(op));
            put(w, "default");
            return;
        }
        input(w, n, "lhs", prec);
        put(w, " ");
        put_str(w, op);
        put(w, " ");
        input(w, n, "rhs", prec + 1);
    } else if (str_eq_c(k, "unary")) {
        const str op = jstr(n, "op");
        if (!str_eq_c(op, "!") && !str_eq_c(op, "-") && !str_eq_c(op, "~")) {
            gerror(w, n, "node %d's operator '" STR_FMT "' isn't one of Tide's prefix operators: !, - and ~", id, STR_ARG(op));
        }
        put_str(w, op);
        // `- -x` and `-(-1)`, never `--x`
        const int operand = jwire(n, "value");
        const graph_json *o = operand ? table_get(&w->nodes, past_knots(w, operand)) : NULL;
        const bool clash = o && (prec_of(o) == P_UNARY || kind_is(o, "unary"));
        input(w, n, "value", clash ? P_POSTFIX : P_UNARY);
    } else if (str_eq_c(k, "step")) {
        const str op = jstr(n, "op");
        if (!str_eq_c(op, "++") && !str_eq_c(op, "--")) gerror(w, n, "node %d's step is '++' or '--'", id);
        if (!jbool(n, "postfix")) put_str(w, op);
        input(w, n, "target", jbool(n, "postfix") ? P_POSTFIX : P_UNARY);
        if (jbool(n, "postfix")) put_str(w, op);
    } else if (str_eq_c(k, "conditional")) {
        input(w, n, "cond", P_COALESCE);
        put(w, " ? ");
        input(w, n, "then", P_LAMBDA);
        put(w, " : ");
        input(w, n, "else", P_LAMBDA);
    } else if (str_eq_c(k, "switchExpr")) {
        input(w, n, "value", P_UNARY);
        put(w, " switch { ");
        const graph_json *arms = jarr(n, "arms");
        for (int i = 0; arms && i < arms->count; i++) {
            if (i) put(w, ", ");
            const graph_json *arm = arms->items[i];
            if (jwire(arm, "pattern")) expr(w, jwire(arm, "pattern"), P_COALESCE, n, "arms");
            else put(w, "_");
            put(w, " => ");
            if (jwire(arm, "value")) expr(w, jwire(arm, "value"), P_LAMBDA, n, "arms");
            else {
                gerror(w, n, "node %d's arm %d has no value", id, i + 1);
                put(w, "default");
            }
        }
        put(w, " }");
    } else if (str_eq_c(k, "interp")) {
        const graph_json *parts = jarr(n, "parts");
        const graph_json *formats = jarr(n, "formats");
        const graph_json *values = jarr(n, "values");
        const int count = values ? values->count : 0;
        if (!parts || parts->count != count + 1) {
            gerror(w, n, "node %d's text has %d values, so it needs %d parts around them", id, count, count + 1);
            put(w, "\"\"");
            return;
        }
        put(w, "$\"");
        for (int i = 0; i <= count; i++) {
            if (parts->items[i]->kind == GJ_STRING) put_str(w, parts->items[i]->text);
            if (i == count) break;
            put(w, "{");
            const int v = item_wire(values->items[i]);
            if (v) expr(w, v, P_COALESCE, n, "values");
            else {
                put_hole(w, "values", i, "node %d's value %d isn't connected", id, i + 1);
            }
            if (formats && i < formats->count && formats->items[i]->kind == GJ_STRING && formats->items[i]->text.len) {
                put(w, ":");
                put_str(w, formats->items[i]->text);
            }
            put(w, "}");
        }
        put(w, "\"");
    } else if (str_eq_c(k, "index")) {
        input(w, n, "object", P_POSTFIX);
        put(w, "[");
        const graph_json *index = jarr(n, "index");
        if (!index || !index->count) gerror(w, n, "node %d has no index", id);
        for (int i = 0; index && i < index->count; i++) {
            if (i) put(w, ", ");
            const int v = item_wire(index->items[i]);
            if (v) expr(w, v, P_LAMBDA, n, "index");
            else put(w, "0");
        }
        put(w, "]");
    } else if (str_eq_c(k, "list")) {
        put(w, "[");
        const graph_json *items = jarr(n, "items");
        for (int i = 0; items && i < items->count; i++) {
            if (i) put(w, ", ");
            const int v = item_wire(items->items[i]);
            if (v) expr(w, v, P_LAMBDA, n, "items");
            else {
                put_hole(w, "items", i, "node %d's item %d isn't connected", id, i + 1);
            }
        }
        put(w, "]");
    } else if (str_eq_c(k, "literal")) {
        put_required(w, n, "type", "a value of a type");
        const graph_json *inits = jarr(n, "inits");
        if (!inits || !inits->count) {
            put(w, " { }");
            return;
        }
        put(w, " { ");
        for (int i = 0; i < inits->count; i++) {
            if (i) put(w, ", ");
            const graph_json *init = inits->items[i];
            put_required(w, init, "name", "a field's value");
            put(w, " = ");
            if (jwire(init, "value")) expr(w, jwire(init, "value"), P_LAMBDA, n, "inits");
            else {
                put_hole(w, "inits", i, "node %d's field '" STR_FMT "' isn't connected", id, STR_ARG(jstr(init, "name")));
            }
        }
        put(w, " }");
    } else if (str_eq_c(k, "lambda")) {
        put(w, "(");
        const graph_json *params = jarr(n, "params");
        for (int i = 0; params && i < params->count; i++) {
            if (i) put(w, ", ");
            const graph_json *prm = params->items[i];
            if (jbool(prm, "mut")) put(w, "mut ");
            put_required(w, prm, "type", "a lambda's parameter");
            put(w, " ");
            put_required(w, prm, "name", "a lambda's parameter");
        }
        put(w, ") => ");
        input(w, n, "body", P_LAMBDA);
    } else if (str_eq_c(k, "coalesce")) {
        input(w, n, "lhs", P_OR);
        put(w, " ?? ");
        input(w, n, "rhs", P_COALESCE);
    } else if (str_eq_c(k, "is")) {
        input(w, n, "value", P_REL);
        put(w, " is ");
        put_required(w, n, "type", "an 'is'");
        const str binding = jstr(n, "binding");
        if (binding.len) {
            put(w, " ");
            put_str(w, binding);
            vec_push(w->declared.items, id);
        }
    } else if (str_eq_c(k, "has")) {
        put_required(w, n, "subject", "a 'has'");
        put(w, " has ");
        put_required(w, n, "type", "a 'has'");
        put(w, " ");
        put_required(w, n, "field", "a 'has'");
    } else if (str_eq_c(k, "try")) {
        put(w, "try ");
        input(w, n, "value", P_UNARY);
    } else if (str_eq_c(k, "await")) {
        put(w, "await ");
        input(w, n, "value", P_UNARY);
    } else if (str_eq_c(k, "defaulted")) {
        input(w, n, "value", P_POSTFIX);
        put(w, "!");
    } else if (str_eq_c(k, "text")) {
        put_text(w, n);
    } else if (str_eq_c(k, "eval")) {
        // A statement's value, kept when it ran
        if (!set_has(&w->declared, id)) {
            gerror(w, n, "node %d's value is read before it runs: what reads it must come after it", id);
        }
        sb_printf(&w->out, "_graph%d", id);
    } else if (exec_kind(k)) {
        gerror(w, n, "node %d is a statement, which has no value to wire into another node", id);
        put(w, "default");
    } else {
        gerror(w, n, "node %d is a '" STR_FMT "', a kind of node tide doesn't know", id, STR_ARG(k));
        put(w, "default");
    }
}

static void expr(lw *w, const int id, const int min, const graph_json *from, const char *what)
{
    (void)what;
    const graph_json *n = node_of(w, id, from);
    if (!n) {
        put(w, "default");
        return;
    }
    if (set_has(&w->visiting, id)) {
        gerror(w, n, "node %d's value goes round in a loop back to itself", id);
        put(w, "default");
        return;
    }
    vec_push(w->visiting.items, id);
    if (kind_is(n, "reroute")) { // A knot on a wire: what's wired into it, as it would be without the knot
        const int start = mark(w);
        input(w, n, "value", min);
        span(w, id, start);
        w->visiting.items.count--;
        return;
    }
    // A text node's text is wrapped as a whole, whatever's in it
    const bool parens = kind_is(n, "text") || prec_of(n) < min;
    const int start = mark(w);
    if (parens) put(w, "(");
    expr_node(w, n, id);
    if (parens) put(w, ")");
    span(w, id, start);
    w->visiting.items.count--;
}

// ---------------------------------------------------------------------------
// Statements

static bool exec_once(lw *w, const int id, const graph_json *n)
{
    if (set_has(&w->exec_seen, id)) {
        gerror(w, n, "node %d is reached from two places: a statement runs from one place, so copy it for the other", id);
        return false;
    }
    vec_push(w->exec_seen.items, id);
    return true;
}

static void stmt_body(lw *w, const graph_json *n, const char *key)
{
    put(w, "{");
    w->indent++;
    const int first = jwire(n, key);
    if (first) stmts(w, first, n);
    w->indent--;
    line(w);
    put(w, "}");
}

// A statement inside a for's parentheses: its start, without the ';' a var takes, or its step
static void inline_stmt(lw *w, const int id, const graph_json *from);

static void put_var(lw *w, const graph_json *n, const int id)
{
    if (jbool(n, "shared")) put(w, "shared ");
    if (jbool(n, "mut")) put(w, "mut ");
    const str type = jstr(n, "type");
    if (type.len) put_str(w, type);
    else put(w, "var");
    put(w, " ");
    put_required(w, n, "name", "a local");
    if (jwire(n, "value")) {
        put(w, " = ");
        input(w, n, "value", P_LAMBDA);
    } else if (!type.len) {
        gerror(w, n, "node %d declares a local with 'var' and no value: give it its type, or a value", id);
    }
    vec_push(w->declared.items, id);
}

static void put_assign(lw *w, const graph_json *n)
{
    const str op = jstr(n, "op");
    input(w, n, "target", P_LAMBDA);
    if (str_eq_c(op, "++") || str_eq_c(op, "--")) {
        put_str(w, op);
        return;
    }
    put(w, " ");
    put_str(w, op.len ? op : str_from("="));
    put(w, " ");
    input(w, n, "value", P_LAMBDA);
}

static void put_eval(lw *w, const graph_json *n, const int id)
{
    if (set_has(&w->kept, id)) sb_printf(&w->out, "var _graph%d = ", id);
    input(w, n, "value", P_LAMBDA);
    const int block = jwire(n, "block");
    if (!block && jbool(n, "withBlock")) { // A call that takes a block, with nothing in it
        put(w, " { }");
        return;
    }
    if (!block) {
        if (set_has(&w->kept, id)) vec_push(w->declared.items, id);
        return;
    }
    const graph_json *b = node_of(w, block, n);
    // Calls listed before the block they share: the next is the block's only statement, itself stacked
    if (b && kind_is(b, "eval") && jbool(b, "stacked") && !jwire(b, "next")
        && exec_once(w, block, b)) {
        put(w, ", ");
        const int start = mark(w);
        put_eval(w, b, block);
        span(w, block, start);
        return;
    }
    put(w, " ");
    stmt_body(w, n, "block");
}

static void inline_stmt(lw *w, const int id, const graph_json *from)
{
    const graph_json *n = node_of(w, id, from);
    if (!n || !exec_once(w, id, n)) return;
    const int start = mark(w);
    if (kind_is(n, "var")) put_var(w, n, id);
    else if (kind_is(n, "assign")) put_assign(w, n);
    else if (kind_is(n, "eval")) input(w, n, "value", P_LAMBDA);
    else gerror(w, n, "node %d can't be a for's start or step: those are a local, an assignment or a call", id);
    span(w, id, start);
    if (jwire(n, "next")) gerror(w, n, "a for's start and step are one statement each: node %d has another after it", id);
}

static void stmt(lw *w, const graph_json *n, const int id)
{
    const str k = jstr(n, "k");
    if (str_eq_c(k, "var")) {
        put_var(w, n, id);
        put(w, ";");
    } else if (str_eq_c(k, "discard")) {
        put(w, "_ = ");
        input(w, n, "value", P_LAMBDA);
        put(w, ";");
    } else if (str_eq_c(k, "assign")) {
        put_assign(w, n);
        put(w, ";");
    } else if (str_eq_c(k, "eval")) {
        put_eval(w, n, id);
        if (!jwire(n, "block") && !jbool(n, "withBlock")) put(w, ";");
    } else if (str_eq_c(k, "if")) {
        put(w, "if (");
        input(w, n, "cond", P_LAMBDA);
        put(w, ") ");
        stmt_body(w, n, "then");
        const int otherwise = jwire(n, "else");
        if (otherwise) {
            const graph_json *e = node_of(w, otherwise, n);
            put(w, " else ");
            if (e && kind_is(e, "if") && !jwire(e, "next") && !jstr(e, "comment").len) {
                if (exec_once(w, otherwise, e)) {
                    const int start = mark(w);
                    stmt(w, e, otherwise);
                    span(w, otherwise, start);
                }
            } else {
                stmt_body(w, n, "else");
            }
        }
    } else if (str_eq_c(k, "while")) {
        put(w, "while (");
        input(w, n, "cond", P_LAMBDA);
        put(w, ") ");
        stmt_body(w, n, "body");
    } else if (str_eq_c(k, "for")) {
        put(w, "for (");
        if (jwire(n, "init")) inline_stmt(w, jwire(n, "init"), n);
        put(w, ";");
        if (jwire(n, "cond")) {
            put(w, " ");
            input(w, n, "cond", P_LAMBDA);
        }
        put(w, ";");
        if (jwire(n, "step")) {
            put(w, " ");
            inline_stmt(w, jwire(n, "step"), n);
        }
        put(w, ") ");
        stmt_body(w, n, "body");
    } else if (str_eq_c(k, "foreach") || str_eq_c(k, "parallel")) {
        const bool parallel = str_eq_c(k, "parallel");
        if (jbool(n, "indexed")) put(w, "indexed ");
        put(w, parallel ? "parallel (" : "foreach (");
        const str type = jstr(n, "type");
        if (type.len) put_str(w, type);
        else put(w, "var");
        put(w, " ");
        put_required(w, n, "name", parallel ? "a parallel loop" : "a foreach");
        put(w, " in ");
        input(w, n, parallel ? "grid" : "collection", P_LAMBDA);
        if (parallel && jwire(n, "by")) {
            put(w, " by ");
            input(w, n, "by", P_LAMBDA);
        }
        if (parallel && jwire(n, "offset")) {
            put(w, " offset ");
            input(w, n, "offset", P_LAMBDA);
        }
        put(w, ") ");
        vec_push(w->declared.items, id);
        stmt_body(w, n, "body");
    } else if (str_eq_c(k, "switch")) {
        put(w, "switch (");
        input(w, n, "value", P_LAMBDA);
        put(w, ") {");
        const graph_json *cases = jarr(n, "cases");
        for (int i = 0; cases && i < cases->count; i++) {
            const graph_json *c = cases->items[i];
            const graph_json *labels = jarr(c, "labels");
            if (!labels || !labels->count) gerror(w, n, "node %d's case %d has no label: 'case' and a value, or 'default'", id, i + 1);
            for (int l = 0; labels && l < labels->count; l++) {
                line(w);
                const int label = item_wire(labels->items[l]);
                if (label) {
                    put(w, "case ");
                    expr(w, label, P_LAMBDA, n, "cases");
                    put(w, ":");
                } else {
                    put(w, "default:");
                }
            }
            w->indent++;
            if (jwire(c, "body")) stmts(w, jwire(c, "body"), n);
            w->indent--;
        }
        line(w);
        put(w, "}");
    } else if (str_eq_c(k, "return")) {
        put(w, "return");
        if (jwire(n, "value")) {
            put(w, " ");
            input(w, n, "value", P_LAMBDA);
        }
        put(w, ";");
    } else if (str_eq_c(k, "fail")) {
        put(w, "fail ");
        input(w, n, "value", P_LAMBDA);
        put(w, ";");
    } else if (str_eq_c(k, "break") || str_eq_c(k, "continue")) {
        put_str(w, k);
        put(w, ";");
    } else if (str_eq_c(k, "block")) {
        stmt_body(w, n, "body");
    } else if (str_eq_c(k, "code")) {
        put_text(w, n);
    } else if (k.len) {
        gerror(w, n, "node %d is a '" STR_FMT "', which is no statement: a statement's 'next' goes to one", id, STR_ARG(k));
    } else {
        gerror(w, n, "node %d has no kind ('k')", id);
    }
}

static void stmts(lw *w, int id, const graph_json *from)
{
    while (id && !w->failed) {
        const graph_json *n = node_of(w, id, from);
        if (!n || !exec_once(w, id, n)) return;
        line(w);
        put_comment(w, n);
        const int start = mark(w);
        stmt(w, n, id);
        span(w, id, start);
        from = n;
        id = jwire(n, "next");
    }
}

// ---------------------------------------------------------------------------
// Graphs: a body's or an expression's nodes

static void wire_key(const graph_json *n, const char *key, graph_ids *out)
{
    const int id = jwire(n, key);
    if (id) vec_push(*out, id);
}

static void wire_array(const graph_json *n, const char *key, graph_ids *out)
{
    const graph_json *a = jarr(n, key);
    for (int i = 0; a && i < a->count; i++) {
        const int id = item_wire(a->items[i]);
        if (id) vec_push(*out, id);
    }
}

void graph_exec_wires(const graph_json *n, graph_ids *out)
{
    const graph_json *spec = node_spec(jstr(n, "k"));
    if (!spec) return;
    const graph_json *outputs = graph_json_get(spec, "outputs");
    for (int i = 0; outputs && i < outputs->count; i++) wire_key(n, str_to_cstr(outputs->items[i]->text), out);
    const graph_json *items = graph_json_get(spec, "items");
    for (int i = 0; items && i < items->count; i++) {
        const str exec = jstr(items->items[i], "exec");
        if (!exec.len) continue;
        const graph_json *list = jarr(n, str_to_cstr(jstr(items->items[i], "key")));
        for (int k = 0; list && k < list->count; k++) wire_key(list->items[k], str_to_cstr(exec), out);
    }
}

void graph_data_wires(const graph_json *n, graph_ids *out)
{
    // As the schema says each kind's data inputs are: single ones, lists of them, items with them, a map
    const graph_json *spec = node_spec(jstr(n, "k"));
    if (!spec) return;
    const graph_json *inputs = graph_json_get(spec, "inputs");
    for (int i = 0; inputs && i < inputs->count; i++) wire_key(n, str_to_cstr(inputs->items[i]->text), out);
    const graph_json *lists = graph_json_get(spec, "lists");
    for (int i = 0; lists && i < lists->count; i++) wire_array(n, str_to_cstr(jstr(lists->items[i], "key")), out);
    const graph_json *items = graph_json_get(spec, "items");
    for (int i = 0; items && i < items->count; i++) {
        const graph_json *list = jarr(n, str_to_cstr(jstr(items->items[i], "key")));
        const graph_json *wires = graph_json_get(items->items[i], "wires");
        for (int k = 0; list && k < list->count; k++) {
            for (int w = 0; wires && w < wires->count; w++) {
                const str key = wires->items[w]->text;
                if (key.len > 2 && key.ptr[key.len - 2] == '[') { // "labels[]": an array of them
                    wire_array(list->items[k], str_to_cstr((str){key.ptr, key.len - 2}), out);
                } else {
                    wire_key(list->items[k], str_to_cstr(key), out);
                }
            }
        }
    }
    const str map = jstr(spec, "map");
    if (map.len) {
        const graph_json *named = graph_json_get(n, str_to_cstr(map));
        for (int i = 0; named && named->kind == GJ_OBJECT && i < named->count; i++) {
            const int id = item_wire(named->items[i]);
            if (id) vec_push(*out, id);
        }
    }
}

static bool use_graph(lw *w, const graph_json *g, const graph_json *owner, const char *what)
{
    w->nodes = (id_table){0};
    w->graph = g;
    w->exec_seen.items.count = 0;
    w->kept.items.count = 0;
    w->visiting.items.count = 0;
    if (!g || g->kind != GJ_OBJECT) {
        gerror(w, owner, "%s isn't a graph: an object with its nodes", what);
        return false;
    }
    const graph_json *nodes = jarr(g, "nodes");
    for (int i = 0; nodes && i < nodes->count; i++) {
        const graph_json *n = nodes->items[i];
        const int id = json_id(n);
        if (!id) {
            gerror(w, n, "a node has no id");
            continue;
        }
        if (table_get(&w->nodes, id)) gerror(w, n, "two nodes have id %d", id);
        table_put(&w->nodes, id, n);
    }
    // Statements whose value another node reads: their value is kept as a local
    for (int i = 0; nodes && i < nodes->count; i++) {
        graph_ids wires = {0};
        graph_data_wires(nodes->items[i], &wires);
        for (int k = 0; k < wires.count; k++) {
            const int from = past_knots(w, wires.items[k]);
            const graph_json *target = table_get(&w->nodes, from);
            if (target && kind_is(target, "eval") && !set_has(&w->kept, from)) vec_push(w->kept.items, from);
        }
    }
    return true;
}

static int written_value; // graph_lower_with: a value node to write even where nothing reads it

static bool has_span(const lw *w, const int id)
{
    for (int i = 0; i < w->map->spans.count; i++) {
        if (w->map->spans.items[i].id == id) return true;
    }
    return false;
}

static void body(lw *w, const graph_json *owner, const char *key)
{
    const graph_json *g = graph_json_get(owner, key);
    put(w, " {");
    if (g && g->kind != GJ_NULL && use_graph(w, g, owner, "a body")) {
        w->declared.items.count = 0;
        w->indent++;
        const int entry = jwire(g, "entry");
        if (entry) stmts(w, entry, g);
        // The value asked about, which nothing reads: at the body's end, as a statement of its own
        const graph_json *asked = written_value ? table_get(&w->nodes, written_value) : NULL;
        if (asked && !exec_kind(jstr(asked, "k")) && !has_span(w, written_value)) {
            line(w);
            put(w, "_ = ");
            expr(w, written_value, P_LAMBDA, g, "asked");
            put(w, ";");
        }
        w->indent--;
    }
    line(w);
    put(w, "}");
}

static void value_graph(lw *w, const graph_json *g, const graph_json *owner)
{
    if (!use_graph(w, g, owner, "a value")) {
        put(w, "default");
        return;
    }
    w->declared.items.count = 0;
    const int root = jwire(g, "root");
    if (!root) {
        gerror(w, owner, "a value's graph has no root: the node whose value it is");
        put(w, "default");
        return;
    }
    expr(w, root, P_LAMBDA, g, "root");
}

static void value_slot(lw *w, const graph_json *owner, const char *key, const bool required)
{
    const graph_json *g = graph_json_get(owner, key);
    if (!g || g->kind == GJ_NULL) {
        if (required) {
            gerror(w, owner, "this has no %s", key);
            put(w, "default");
        }
        return;
    }
    value_graph(w, g, owner);
}

// ---------------------------------------------------------------------------
// Declarations

static void decls(lw *w, const graph_json *list);

static void put_attributes(lw *w, const graph_json *v, const bool inline_)
{
    const graph_json *attrs = jarr(v, "attributes");
    for (int i = 0; attrs && i < attrs->count; i++) {
        const graph_json *a = attrs->items[i];
        const int start = mark(w);
        put(w, "[");
        put_required(w, a, "name", "an attribute");
        const graph_json *args = graph_json_get(a, "args");
        if (args && args->kind == GJ_STRING) {
            put(w, "(");
            put_str(w, args->text);
            put(w, ")");
        }
        put(w, "]");
        span(w, json_id(a), start);
        if (inline_) put(w, " ");
        else line(w);
    }
}

static bool has_modifier(const graph_json *v, const char *word)
{
    const graph_json *mods = jarr(v, "modifiers");
    for (int i = 0; mods && i < mods->count; i++) {
        if (mods->items[i]->kind == GJ_STRING && str_eq_c(mods->items[i]->text, word)) return true;
    }
    return false;
}

static void put_modifiers(lw *w, const graph_json *v)
{
    static const char *const order[] = {"dev", "local", "async", "worker", "build", "cached", "virtual", "override", "tweak"};
    const graph_json *mods = jarr(v, "modifiers");
    for (int i = 0; mods && i < mods->count; i++) {
        bool known = false;
        for (size_t k = 0; k < sizeof order / sizeof order[0]; k++) known |= mods->items[i]->kind == GJ_STRING && str_eq_c(mods->items[i]->text, order[k]);
        if (!known) gerror(w, v, "'" STR_FMT "' isn't a word that goes before a declaration", STR_ARG(mods->items[i]->text));
    }
    for (size_t k = 0; k < sizeof order / sizeof order[0]; k++) {
        if (has_modifier(v, order[k])) {
            put(w, order[k]);
            put(w, " ");
        }
    }
}

// A declaration's name, or the qualified one an override replaces
static void put_decl_name(lw *w, const graph_json *v)
{
    const str overrides = jstr(v, "overrides");
    if (overrides.len) put_str(w, overrides);
    else put_required(w, v, "name", "a declaration");
}

static void put_type_params(lw *w, const graph_json *v)
{
    const graph_json *tps = jarr(v, "typeParams");
    if (!tps || !tps->count) return;
    put(w, "<");
    for (int i = 0; i < tps->count; i++) {
        if (i) put(w, ", ");
        if (tps->items[i]->kind == GJ_STRING) put_str(w, tps->items[i]->text);
    }
    put(w, ">");
}

static void put_where(lw *w, const graph_json *v)
{
    const graph_json *where = jarr(v, "where");
    for (int i = 0; where && i < where->count; i++) {
        const graph_json *rule = where->items[i];
        put(w, " where ");
        put_required(w, rule, "param", "a 'where'");
        put(w, " : ");
        const graph_json *rules = jarr(rule, "rules");
        for (int r = 0; rules && r < rules->count; r++) {
            if (r) put(w, ", ");
            if (rules->items[r]->kind == GJ_STRING) put_str(w, rules->items[r]->text);
        }
    }
}

static void put_param(lw *w, const graph_json *p);

static void put_params(lw *w, const graph_json *v, const char *key)
{
    put(w, "(");
    const graph_json *params = jarr(v, key);
    for (int i = 0; params && i < params->count; i++) {
        if (i) put(w, ", ");
        const int start = mark(w);
        put_param(w, params->items[i]);
        span(w, json_id(params->items[i]), start);
    }
    put(w, ")");
}

static void put_param(lw *w, const graph_json *p)
{
    const str mode = jstr(p, "mode");
    const str role = jstr(p, "role");
    if (role.len) {
        put(w, "[");
        put_str(w, role);
        put(w, "] ");
    }
    if (str_eq_c(mode, "query")) {
        put(w, "query ");
        put_params(w, p, "query");
        put(w, " ");
        put_required(w, p, "name", "a query parameter");
        return;
    }
    if (str_eq_c(mode, "with") || str_eq_c(mode, "without")) {
        put_str(w, mode);
        put(w, " ");
        if (jbool(p, "ownership")) {
            put(w, "ownership");
            return;
        }
        if (jbool(p, "verified")) put(w, "verified ");
        if (jbool(p, "previous")) put(w, "previous ");
        if (jbool(p, "local")) put(w, "local ");
        if (jbool(p, "scene")) put(w, "scene ");
        put_required(w, p, "type", "a filter");
        return;
    }
    if (jbool(p, "local")) put(w, "local ");
    if (str_eq_c(mode, "mut")) put(w, "mut ");
    else if (str_eq_c(mode, "in")) put(w, "in ");
    else if (str_eq_c(mode, "out")) put(w, "out ");
    else if (mode.len && !str_eq_c(mode, "read")) gerror(w, p, "a parameter's mode is read, mut, in, out, with, without or query");
    if (jbool(p, "verified")) put(w, "verified ");
    if (jbool(p, "previous")) put(w, "previous ");
    put_required(w, p, "type", "a parameter");
    put(w, " ");
    put_required(w, p, "name", "a parameter");
    if (graph_json_get(p, "default") && graph_json_get(p, "default")->kind != GJ_NULL) {
        put(w, " = ");
        value_slot(w, p, "default", false);
    }
}

static void put_fails(lw *w, const graph_json *v)
{
    const str fails = jstr(v, "fails");
    if (!fails.len) return;
    put(w, " fails ");
    put_str(w, fails);
}

// The shape a function, a method and the like share: [mut] Ret Name<T>(params) fails E where ... { body }
static void put_routine(lw *w, const graph_json *v)
{
    if (jbool(v, "mut")) put(w, "mut ");
    put_required(w, v, "returnType", "a function");
    put(w, " ");
    put_decl_name(w, v);
    put_type_params(w, v);
    put_params(w, v, "params");
    put_fails(w, v);
    put_where(w, v);
    if (jbool(v, "extern")) {
        put(w, ";");
        return;
    }
    body(w, v, "body");
}

static void put_fields_list(lw *w, const graph_json *v, const char *key)
{
    const graph_json *fields = jarr(v, key);
    for (int i = 0; fields && i < fields->count; i++) {
        if (i) put(w, ", ");
        const graph_json *f = fields->items[i];
        const int start = mark(w);
        put_attributes(w, f, true);
        put_required(w, f, "type", "a value");
        put(w, " ");
        put_required(w, f, "name", "a value");
        if (graph_json_get(f, "default") && graph_json_get(f, "default")->kind != GJ_NULL) {
            put(w, " = ");
            value_slot(w, f, "default", false);
        }
        span(w, json_id(f), start);
    }
}

static void member(lw *w, const graph_json *m)
{
    const str k = jstr(m, "kind");
    if (str_eq_c(k, "text")) {
        put_str(w, jstr(m, "text"));
    } else if (str_eq_c(k, "field")) {
        put_attributes(w, m, true);
        if (jbool(m, "local")) put(w, "local ");
        put_required(w, m, "type", "a field");
        put(w, " ");
        put_required(w, m, "name", "a field");
        if (graph_json_get(m, "default") && graph_json_get(m, "default")->kind != GJ_NULL) {
            put(w, " = ");
            value_slot(w, m, "default", false);
        }
        put(w, ";");
    } else if (str_eq_c(k, "property")) {
        put_required(w, m, "type", "a property");
        put(w, " ");
        put_required(w, m, "name", "a property");
        put(w, " {");
        w->indent++;
        const graph_json *accessors = jarr(m, "accessors");
        for (int i = 0; accessors && i < accessors->count; i++) {
            const graph_json *a = accessors->items[i];
            line(w);
            const int start = mark(w);
            const str word = jstr(a, "word");
            if (!str_eq_c(word, "get") && !str_eq_c(word, "set")) gerror(w, a, "an accessor is 'get' or 'set'");
            put_str(w, word);
            body(w, a, "body");
            span(w, json_id(a), start);
        }
        w->indent--;
        line(w);
        put(w, "}");
    } else if (str_eq_c(k, "method")) {
        put_modifiers(w, m);
        put_routine(w, m);
    } else if (str_eq_c(k, "operator")) {
        put_required(w, m, "returnType", "an operator");
        put(w, " operator ");
        put_required(w, m, "op", "an operator");
        put_params(w, m, "params");
        put_fails(w, m);
        body(w, m, "body");
    } else if (str_eq_c(k, "tweak")) {
        put(w, "tweak ");
        put_required(w, m, "type", "a tweak");
        put(w, " ");
        put_required(w, m, "name", "a tweak");
        put(w, " = ");
        value_slot(w, m, "value", true);
        put(w, ";");
    } else if (str_eq_c(k, "sample")) {
        put_required(w, m, "name", "an input's Sample");
        put(w, "(");
        const graph_json *params = jarr(m, "params");
        for (int i = 0; params && i < params->count; i++) {
            if (i) put(w, ", ");
            const int start = mark(w);
            put_required(w, params->items[i], "type", "a parameter");
            put(w, " ");
            put_required(w, params->items[i], "name", "a parameter");
            span(w, json_id(params->items[i]), start);
        }
        put(w, ")");
        body(w, m, "body");
    } else if (str_eq_c(k, "sanitize") || str_eq_c(k, "predict")) {
        put(w, str_eq_c(k, "sanitize") ? "Sanitize()" : "Predict()");
        body(w, m, "body");
    } else if (str_eq_c(k, "prepare")) {
        put(w, "prepare");
        put_params(w, m, "params");
        body(w, m, "body");
    } else {
        gerror(w, m, "a member is a field, a property, a method, an operator, a tweak, a Sample, a Sanitize, a Predict, "
                     "a prepare or text");
    }
}

static void prefab_node(lw *w, const graph_json *p, const bool root)
{
    const int start = mark(w);
    if (!root) {
        put(w, "entity ");
        put_required(w, p, "name", "a prefab's entity");
        const str base = jstr(p, "base");
        if (base.len) {
            put(w, " : ");
            put_str(w, base);
        }
    }
    put(w, root ? " {" : " {");
    w->indent++;
    const graph_json *components = jarr(p, "components");
    for (int i = 0; components && i < components->count; i++) {
        line(w);
        value_graph(w, components->items[i], p);
        put(w, ";");
    }
    const graph_json *children = jarr(p, "children");
    for (int i = 0; children && i < children->count; i++) {
        line(w);
        prefab_node(w, children->items[i], false);
    }
    w->indent--;
    line(w);
    put(w, "}");
    span(w, json_id(p), start);
}

static void decl(lw *w, const graph_json *d)
{
    const str k = jstr(d, "kind");
    if (str_eq_c(k, "text")) {
        put_str(w, jstr(d, "text"));
        return;
    }
    if (str_eq_c(k, "namespace")) {
        put(w, "namespace ");
        put_required(w, d, "name", "a namespace");
        put(w, " {");
        w->indent++;
        decls(w, jarr(d, "decls"));
        w->indent--;
        line(w);
        put(w, "}");
        return;
    }
    if (str_eq_c(k, "settings")) {
        put(w, "settings {");
        w->indent++;
        const graph_json *entries = jarr(d, "entries");
        for (int i = 0; entries && i < entries->count; i++) {
            const graph_json *e = entries->items[i];
            line(w);
            put_comment(w, e);
            const int start = mark(w);
            put_required(w, e, "name", "a setting");
            put(w, " = ");
            value_slot(w, e, "value", true);
            put(w, ";");
            span(w, json_id(e), start);
        }
        w->indent--;
        line(w);
        put(w, "}");
        return;
    }
    put_attributes(w, d, false);
    const bool data = str_eq_c(k, "component") || str_eq_c(k, "singleton") || str_eq_c(k, "struct") || str_eq_c(k, "event")
                      || str_eq_c(k, "input") || str_eq_c(k, "scene");
    if (data) {
        put_modifiers(w, d);
        put_str(w, k);
        put(w, " ");
        put_required(w, d, "name", "a type");
        put_type_params(w, d);
        put_where(w, d);
        const graph_json *members = graph_json_get(d, "members");
        if (str_eq_c(k, "scene") && (!members || members->kind == GJ_NULL)) {
            put(w, ";");
            return;
        }
        put(w, " {");
        w->indent++;
        for (int i = 0; members && members->kind == GJ_ARRAY && i < members->count; i++) {
            const graph_json *m = members->items[i];
            line(w);
            put_comment(w, m);
            const int start = mark(w);
            member(w, m);
            span(w, json_id(m), start);
        }
        w->indent--;
        line(w);
        put(w, "}");
        return;
    }
    if (str_eq_c(k, "enum")) {
        put_modifiers(w, d);
        put(w, "enum ");
        put_required(w, d, "name", "an enum");
        const str backing = jstr(d, "backing");
        if (backing.len) {
            put(w, " : ");
            put_str(w, backing);
        }
        put(w, " {");
        w->indent++;
        const graph_json *members = jarr(d, "members");
        for (int i = 0; members && i < members->count; i++) {
            const graph_json *m = members->items[i];
            line(w);
            put_comment(w, m);
            const int start = mark(w);
            put_required(w, m, "name", "an enum's member");
            if (graph_json_get(m, "value") && graph_json_get(m, "value")->kind != GJ_NULL) {
                put(w, " = ");
                value_slot(w, m, "value", false);
            }
            put(w, ",");
            span(w, json_id(m), start);
        }
        w->indent--;
        line(w);
        put(w, "}");
        return;
    }
    if (str_eq_c(k, "const")) {
        const bool tweak = has_modifier(d, "tweak");
        put_modifiers(w, d);
        if (!tweak) put(w, "const ");
        put_required(w, d, "type", "a constant");
        put(w, " ");
        put_required(w, d, "name", "a constant");
        put(w, " = ");
        value_slot(w, d, "value", true);
        put(w, ";");
        return;
    }
    if (str_eq_c(k, "asset")) {
        put_modifiers(w, d);
        put(w, "asset ");
        put_required(w, d, "type", "an asset");
        put(w, " ");
        put_required(w, d, "name", "an asset");
        put(w, " = ");
        value_slot(w, d, "value", true);
        put(w, ";");
        return;
    }
    if (str_eq_c(k, "alias")) {
        put(w, "using ");
        put_required(w, d, "name", "an alias");
        put(w, " = ");
        put_required(w, d, "type", "an alias");
        put(w, ";");
        return;
    }
    if (str_eq_c(k, "function")) {
        put_modifiers(w, d);
        if (jbool(d, "extern")) put(w, "extern ");
        put_routine(w, d);
        return;
    }
    if (str_eq_c(k, "audio") || str_eq_c(k, "frag") || str_eq_c(k, "vert") || str_eq_c(k, "compute")
        || str_eq_c(k, "system") || str_eq_c(k, "view")) {
        put_modifiers(w, d);
        const bool external = jbool(d, "extern");
        if (external) put(w, "extern ");
        put_str(w, k);
        put(w, " ");
        put_decl_name(w, d);
        put_params(w, d, "params");
        if (external) put(w, ";");
        else body(w, d, "body");
        return;
    }
    if (str_eq_c(k, "handler")) {
        put_modifiers(w, d);
        const graph_json *t = graph_json_get(d, "trigger");
        put(w, "event(");
        if (jbool(t, "verified")) put(w, "verified ");
        if (jbool(t, "changed")) {
            put(w, "Changed<");
            put_required(w, t, "type", "a handler's trigger");
            put(w, ">");
        } else {
            put_required(w, t, "type", "a handler's trigger");
        }
        const str name = jstr(t, "name");
        if (name.len) {
            put(w, " ");
            put_str(w, name);
        }
        put(w, ") ");
        put_decl_name(w, d);
        put_params(w, d, "params");
        body(w, d, "body");
        return;
    }
    if (str_eq_c(k, "command")) {
        put_modifiers(w, d);
        put(w, "command(");
        put_fields_list(w, d, "payload");
        put(w, ") ");
        put_decl_name(w, d);
        put_params(w, d, "params");
        body(w, d, "body");
        return;
    }
    if (str_eq_c(k, "prefab")) {
        put_modifiers(w, d);
        put(w, "prefab ");
        put_decl_name(w, d);
        const graph_json *root = graph_json_get(d, "root");
        if (!root || root->kind != GJ_OBJECT) {
            gerror(w, d, "a prefab has a root entity");
            put(w, " { }");
            return;
        }
        prefab_node(w, root, true);
        return;
    }
    gerror(w, d, "a declaration's kind is '" STR_FMT "', which tide doesn't know", STR_ARG(k));
}

static void decls(lw *w, const graph_json *list)
{
    for (int i = 0; list && i < list->count && !w->failed; i++) {
        const graph_json *d = list->items[i];
        if (d->kind != GJ_OBJECT) {
            gerror(w, d, "a declaration is an object");
            continue;
        }
        line(w);
        put_comment(w, d);
        const int start = mark(w);
        decl(w, d);
        span(w, json_id(d), start);
    }
}

// Every element with an id, for diagnostics found at a place in the file
static void collect_places(graph_map *map, const graph_json *v)
{
    if (v->kind == GJ_OBJECT) {
        const int id = json_id(v);
        if (id) {
            const graph_place p = {id, v->at};
            vec_push(map->places, p);
        }
    }
    if (v->kind == GJ_OBJECT || v->kind == GJ_ARRAY) {
        for (int i = 0; i < v->count; i++) collect_places(map, v->items[i]);
    }
}

static bool lower(source *src);

bool graph_lower(source *src)
{
    return graph_lower_with(src, 0);
}

bool graph_lower_with(source *src, const int value)
{
    graph_schema_hold(true);
    written_value = value;
    const bool ok = lower(src);
    graph_schema_hold(false);
    written_value = 0;
    return ok;
}

static bool lower(source *src)
{
    graph_map *map = NEW(graph_map);
    src->graph = map;
    src->graph_json = src->text;
    src->graph_json_len = src->len;
    graph_json *root = graph_json_parse(src);
    if (!root) return false;
    collect_places(map, root);
    lw w = {.src = src, .map = map};
    const graph_json *version = graph_json_get(root, "tidegraph");
    if (root->kind != GJ_OBJECT || !version || version->kind != GJ_NUMBER) {
        gerror(&w, root, "a graph's file starts with its format's version: '{ \"tidegraph\": 1, ... }'");
        return false;
    }
    if (version->number != 1) {
        gerror(&w, version, "this graph is in version %g of the format, and this tide reads version 1: upgrade tide",
               version->number);
        return false;
    }
    const str ns = jstr(root, "namespace");
    if (ns.len) {
        put(&w, "namespace ");
        put_str(&w, ns);
        put(&w, ";");
        line(&w);
    }
    const graph_json *usings = jarr(root, "using");
    for (int i = 0; usings && i < usings->count; i++) {
        if (usings->items[i]->kind != GJ_STRING) continue;
        put(&w, "using ");
        put_str(&w, usings->items[i]->text);
        put(&w, ";");
        line(&w);
    }
    decls(&w, jarr(root, "decls"));
    put(&w, "\n");
    if (w.failed) return false;
    // Where each line starts, for diagnostics' places
    const int first = 0;
    vec_push(map->line_starts, first);
    for (size_t i = 0; i < w.out.len; i++) {
        if (w.out.data[i] == '\n') {
            const int next = (int)i + 1;
            vec_push(map->line_starts, next);
        }
    }
    src->text = w.out.data ? w.out.data : "";
    src->len = w.out.len;
    map->lowered = true;
    // What isn't wired, said at its place in the text, which is its node's
    map->holes = w.holes.count;
    for (int i = 0, at_line = 0; i < w.holes.count; i++) {
        const hole *h = &w.holes.items[i];
        while (at_line + 1 < map->line_starts.count && map->line_starts.items[at_line + 1] <= h->offset) at_line++;
        hole_port = h->port;
        hole_index = h->index;
        diag_error((loc){at_line + 1, h->offset - map->line_starts.items[at_line] + 1, src->file}, "%s", h->message);
    }
    hole_port = NULL;
    hole_index = -1;
    return true;
}

// ---------------------------------------------------------------------------
// Files

char *graph_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    char *text = arena_alloc((size_t)size + 1);
    *len = fread(text, 1, (size_t)size, f);
    fclose(f);
    return text;
}

bool graph_text_of(const char *path, sb *out)
{
    source *src = NEW(source);
    src->path = path;
    src->text = graph_read_file(path, &src->len);
    if (!src->text) {
        fprintf(stderr, "tidec: can't read '%s'; check that it exists and is readable\n", path);
        return false;
    }
    diag_add_source(src);
    if (!graph_lower(src)) return false;
    // Without the blank line the writer starts with
    const char *text = src->text;
    size_t len = src->len;
    while (len && *text == '\n') {
        text++;
        len--;
    }
    sb_putn(out, text, len);
    return true;
}
