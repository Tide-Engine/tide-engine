// The game's C# for Unity's Tide package (unity_code.h).

#include "unity_code.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "unity_names.h"
#include "sys.h"
#include "tide/sha256.h"

// ---------------------------------------------------------------------------
// Text

typedef struct out {
    char *s;
    size_t n, capacity;
} out;

static void putn(out *o, const char *s, const size_t n)
{
    if (o->n + n + 1 > o->capacity) {
        o->capacity = (o->n + n + 1) * 2;
        o->s = realloc(o->s, o->capacity);
        if (!o->s) abort();
    }
    memcpy(o->s + o->n, s, n);
    o->n += n;
    o->s[o->n] = '\0';
}

static void puts_(out *o, const char *s)
{
    putn(o, s, strlen(s));
}

static void putf(out *o, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void putf(out *o, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    va_list again;
    va_copy(again, args);
    char line[512];
    const int n = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n < 0) {
        va_end(again);
        return;
    }
    if ((size_t)n < sizeof line) {
        putn(o, line, (size_t)n);
    } else {
        char *big = malloc((size_t)n + 1);
        if (!big) abort();
        vsnprintf(big, (size_t)n + 1, format, again);
        putn(o, big, (size_t)n);
        free(big);
    }
    va_end(again);
}

static char *copy(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (!c) abort();
    memcpy(c, s, n);
    return c;
}

// A C# string literal's inside: backslashes and quotes escaped.
static void put_escaped(out *o, const char *s)
{
    for (; s && *s; s++) {
        if (*s == '\\' || *s == '"') putn(o, "\\", 1);
        putn(o, s, 1);
    }
}

// ---------------------------------------------------------------------------
// Names: unity_names.h's

void unity_code_guid(const char *name, char guid[33])
{
    out o = {0};
    puts_(&o, "tide component ");
    puts_(&o, name);
    uint8_t digest[32];
    tide_sha256_of(o.s, o.n, digest);
    free(o.s);
    for (int i = 0; i < 16; i++) snprintf(guid + i * 2, 3, "%02x", (unsigned)digest[i]);
}

// ---------------------------------------------------------------------------
// The schema

typedef struct field {
    const char *name, *type, *kind;
    int ref;
    uint32_t offset, size, element;
} field;

typedef struct type {
    const char *name, *kind;
    uint32_t size;
    bool scene;
    uint8_t *defaults; // Its declared defaults' bytes, `size` of them; NULL without
    field *fields;
    int field_count;
    const char *bare, *ns, *cs; // Its name without " local", its C# namespace (NULL for none) and its C# name
    char *const *ids;           // Its fields' C# names (unity_names.h)
    char *const *local_ids;     // ...and its local part's fields' in its class, or NULL
    int local;            // A match component's or singleton's local fields: their type, or -1
    bool live;            // A struct Unity code has as a C# struct of its own (write_struct), its layout its Value
    // What class of the package it is, its value the class's nested Value: a TideComponent (an authored match
    // component), a TideSingleton, a TideEvent (an event this machine hears) or a TideCommand; or none, a struct
    const char *base;
    bool component;
} type;

typedef struct enumeration {
    const char *name;
    const json *members, *values;
    char *full;           // global::Tide.Game...
    const char *ns, *cs;  // Its C# namespace (NULL for none) and name
    char *const *ids;     // Its members' C# names, NULL for one left out
    int names_count;      // ...how many
} enumeration;

struct ctype;
struct shape;

typedef struct game {
    const unity_mappings *mappings; // What the game's packages say of their components in Unity
    unity_cs_names names;           // The C# names of its types, fields and members (unity_names.h)
    type *types;
    int type_count;
    enumeration *enums;
    int enum_count;
    const json *outline; // print_outline's declarations, or NULL
    const char *project;  // The Unity project's folder, or NULL
    const unity_package *packages; // The game's packages, whose folders hold their files
    int package_count;
    struct ctype **trees; // Every type tree made, freed with the game
    int tree_count;
    struct shape *shapes; // components.json's
    int shape_count;
} game;

static const char *str(const json *object, const char *key)
{
    const char *s = json_str(json_get(object, key));
    return s ? s : "";
}

static uint32_t number(const json *object, const char *key, const double otherwise)
{
    const json *v = json_get(object, key);
    const double d = v && v->kind == JSON_NUMBER ? v->number : otherwise;
    return d >= 0 && d <= 4294967295.0 ? (uint32_t)d : 0;
}

static bool starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int hex(const char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static bool read_game(const json *schema, game *g)
{
    const json *types = json_get(schema, "types"), *enums = json_get(schema, "enums");
    if (!types || types->kind != JSON_ARRAY || !unity_cs_read(schema, &g->names)) return false;
    g->types = calloc((size_t)types->count + 1, sizeof *g->types);
    if (!g->types) abort();
    g->type_count = types->count;
    for (int i = 0; i < types->count; i++) {
        const json *j = types->items[i];
        type *t = &g->types[i];
        t->name = str(j, "name");
        t->kind = str(j, "kind");
        t->size = number(j, "size", 0);
        const json *scene = json_get(j, "scene");
        t->scene = scene && scene->kind == JSON_TRUE;
        const char *defaults = str(j, "defaults");
        if (t->size && strlen(defaults) == (size_t)t->size * 2) {
            t->defaults = malloc(t->size);
            if (!t->defaults) abort();
            for (uint32_t k = 0; k < t->size; k++) {
                const int hi = hex(defaults[k * 2]), lo = hex(defaults[k * 2 + 1]);
                if (hi < 0 || lo < 0) {
                    free(t->defaults);
                    t->defaults = NULL;
                    break;
                }
                t->defaults[k] = (uint8_t)(hi * 16 + lo);
            }
        }
        const json *fields = json_get(j, "fields");
        const int count = fields && fields->kind == JSON_ARRAY ? fields->count : 0;
        t->fields = calloc((size_t)count + 1, sizeof *t->fields);
        if (!t->fields) abort();
        t->field_count = count;
        for (int k = 0; k < count; k++) {
            const json *f = fields->items[k];
            field *fd = &t->fields[k];
            fd->name = str(f, "name");
            fd->type = str(f, "type");
            fd->kind = str(f, "kind");
            const json *ref = json_get(f, "ref");
            fd->ref = ref && ref->kind == JSON_NUMBER ? (int)ref->number : -1;
            fd->offset = number(f, "offset", 0);
            fd->size = number(f, "size", 0);
            fd->element = number(f, "element", 0);
        }
        const json *local_of = json_get(j, "local");
        t->local = local_of && local_of->kind == JSON_NUMBER && local_of->number >= 0 ? (int)local_of->number : -1;
        // Its names, and which class of the package it is
        const unity_cs_type *n = &g->names.types[i];
        t->bare = n->bare;
        t->ns = n->ns;
        t->cs = n->cs;
        t->ids = n->ids;
        t->local_ids = n->local_ids;
        t->component = n->component;
        t->base = n->base;
        t->live = n->live;
    }
    for (int i = 0; i < g->type_count; i++) {
        type *t = &g->types[i];
        if (t->local >= g->type_count || (t->local >= 0 && strcmp(g->types[t->local].kind, "local") != 0)) t->local = -1;
    }
    g->enum_count = enums && enums->kind == JSON_ARRAY ? enums->count : 0;
    g->enums = calloc((size_t)g->enum_count + 1, sizeof *g->enums);
    if (!g->enums) abort();
    for (int i = 0; i < g->enum_count; i++) {
        enumeration *e = &g->enums[i];
        e->name = str(enums->items[i], "name");
        e->members = json_get(enums->items[i], "members");
        e->values = json_get(enums->items[i], "values");
        const unity_cs_enum *n = &g->names.enums[i];
        e->ns = n->ns;
        e->cs = n->cs;
        e->ids = n->ids;
        e->names_count = n->member_count;
        out o = {0};
        putf(&o, "global::Tide.Game.%s%s%s", e->ns ? e->ns : "", e->ns ? "." : "", e->cs);
        e->full = o.s;
    }
    return true;
}

static void free_game(game *g)
{
    for (int i = 0; i < g->type_count; i++) {
        free(g->types[i].defaults);
        free(g->types[i].fields);
    }
    free(g->types);
    for (int i = 0; i < g->enum_count; i++) free(g->enums[i].full);
    free(g->enums);
    unity_cs_free(&g->names);
}

// A type's C# name where a field holds it: a class's value is its Value. malloc'd.
static char *full_name(const type *t)
{
    out o = {0};
    putf(&o, "global::Tide.Game.%s%s%s%s", t->ns ? t->ns : "", t->ns ? "." : "", t->cs, t->base || t->live ? ".Value" : "");
    return o.s;
}

// ---------------------------------------------------------------------------
// Types, as the schema writes them ("List<Note>", "Map<string, int>#map-order-2", "int[4]"), as trees

typedef enum ckind {
    CK_UNKNOWN,
    CK_PRIM,
    CK_STRING,
    CK_ENTITY,
    CK_ENUM,
    CK_STRUCT,
    CK_LIST,
    CK_SET,
    CK_MAP,
    CK_GRID,
    CK_ARRAY,
} ckind;

typedef struct ctype {
    ckind kind;
    int prim;                   // CK_PRIM: its place in prims
    int ref;                    // CK_STRUCT: the type; CK_ENUM: the enum
    struct ctype *elem, *value; // A list's, set's, array's or grid's element, a map's key; a map's value
    int dims;                   // A grid's: 2 or 3
    int length;                 // An array's
    uint32_t size;              // Its bytes, as the game's C lays it out
    char *tide;                 // As the schema writes it, without what's after a '#'
} ctype;

// The scalars and vectors: C#'s type, and how the converter reads Unity's YAML of one ("form"). "literal" is one Unity
// holds as a Tide value in text.
static const struct prim {
    const char *tide, *cs;
    uint32_t size;
    const char *form;
} prims[] = {
    {"bool", "bool", 1, "bool"}, {"byte", "byte", 1, "number"}, {"int", "int", 4, "number"}, {"char", "int", 4, "char"},
    {"long", "long", 8, "number"}, {"float", "float", 4, "number"}, {"double", "double", 8, "number"},
    {"PlayerID", "uint", 4, "number"}, {"float2", "global::UnityEngine.Vector2", 8, "float2"},
    {"float3", "global::UnityEngine.Vector3", 12, "float3"}, {"float4", "global::UnityEngine.Vector4", 16, "float4"},
    {"int2", "global::UnityEngine.Vector2Int", 8, "int2"}, {"int3", "global::UnityEngine.Vector3Int", 12, "int3"},
    {"quaternion", "global::UnityEngine.Quaternion", 16, "quaternion"}, {"Color", "global::UnityEngine.Color", 16, "color"},
    {"Rect", "global::UnityEngine.Rect", 16, "literal"}, {"float4x4", "global::UnityEngine.Matrix4x4", 64, "literal"},
};

static ctype *new_ctype(game *g, const ckind kind, const char *tide, const size_t n)
{
    ctype *t = calloc(1, sizeof *t);
    if (!t) abort();
    t->kind = kind;
    t->prim = t->ref = -1;
    t->tide = malloc(n + 1);
    if (!t->tide) abort();
    memcpy(t->tide, tide, n);
    t->tide[n] = '\0';
    g->trees = realloc(g->trees, sizeof *g->trees * (size_t)(g->tree_count + 1));
    if (!g->trees) abort();
    g->trees[g->tree_count++] = t;
    return t;
}

static ctype *parse_ctype(game *g, const char *s, size_t n);

// A generic's arguments, `count` of them at most, split at its commas outside brackets.
static int ctype_args(game *g, const char *s, const size_t n, ctype **args, const int count)
{
    int found = 0, depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= n; i++) {
        const char c = i < n ? s[i] : ',';
        if (c == '<' || c == '[') depth++;
        else if (c == '>' || c == ']') depth--;
        else if (c == ',' && depth == 0) {
            if (found == count) return found + 1;
            args[found++] = parse_ctype(g, s + start, i - start);
            start = i + 1;
        }
    }
    return found;
}

static ctype *parse_ctype(game *g, const char *s, size_t n)
{
    const char *hash = memchr(s, '#', n);
    if (hash) n = (size_t)(hash - s);
    while (n && *s == ' ') s++, n--;
    while (n && s[n - 1] == ' ') n--;
    ctype *t = new_ctype(g, CK_UNKNOWN, s, n);
    if (n > 2 && s[n - 1] == ']') { // An array: T[n], the last [] of T[a][b] its outer length
        size_t open = n - 1;
        while (open > 0 && s[open] != '[') open--;
        const long length = strtol(s + open + 1, NULL, 10);
        ctype *elem = parse_ctype(g, s, open);
        if (length > 0 && length <= 1048576 && elem->kind != CK_UNKNOWN) {
            t->kind = CK_ARRAY;
            t->elem = elem;
            t->length = (int)length;
            t->size = elem->size * (uint32_t)length;
        }
        return t;
    }
    const char *lt = memchr(s, '<', n);
    if (lt && s[n - 1] == '>') {
        const size_t name = (size_t)(lt - s);
        ctype *args[3] = {0};
        const int count = ctype_args(g, lt + 1, n - name - 2, args, 2);
        for (int i = 0; i < count && i < 2; i++) {
            if (!args[i] || args[i]->kind == CK_UNKNOWN) return t;
        }
        if (name == 4 && strncmp(s, "List", 4) == 0 && count == 1) { // A handle: 8 bytes (tide/text.h)
            t->kind = CK_LIST;
            t->size = 8;
        } else if (name == 3 && strncmp(s, "Set", 3) == 0 && count == 1) { // tide_map
            t->kind = CK_SET;
            t->size = 32;
        } else if (name == 3 && strncmp(s, "Map", 3) == 0 && count == 2) {
            t->kind = CK_MAP;
            t->size = 32;
            t->value = args[1];
        } else if (name == 5 && (strncmp(s, "Grid2", 5) == 0 || strncmp(s, "Grid3", 5) == 0) && count == 1) {
            t->kind = CK_GRID;
            t->size = 8;
            t->dims = s[4] - '0';
        } else {
            return t;
        }
        t->elem = args[0];
        return t;
    }
    for (int i = 0; i < (int)(sizeof prims / sizeof prims[0]); i++) {
        if (strlen(prims[i].tide) == n && strncmp(s, prims[i].tide, n) == 0) {
            t->kind = CK_PRIM;
            t->prim = i;
            t->size = prims[i].size;
            return t;
        }
    }
    if ((n == 6 && strncmp(s, "string", 6) == 0)) {
        t->kind = CK_STRING;
        t->size = 8;
    } else if ((n == 6 && strncmp(s, "Entity", 6) == 0) || (n == 11 && strncmp(s, "LocalEntity", 11) == 0)) {
        t->kind = CK_ENTITY;
        t->size = 8;
    } else {
        for (int i = 0; i < g->enum_count; i++) {
            if (strlen(g->enums[i].name) == n && strncmp(g->enums[i].name, s, n) == 0) {
                t->kind = CK_ENUM;
                t->ref = i;
                t->size = 4;
                return t;
            }
        }
        for (int i = 0; i < g->type_count; i++) {
            if (strlen(g->types[i].name) == n && strncmp(g->types[i].name, s, n) == 0) {
                t->kind = CK_STRUCT;
                t->ref = i;
                t->size = g->types[i].size;
                return t;
            }
        }
    }
    return t;
}

// A field's type as a tree: unknown where the schema's size isn't the tree's.
static ctype *field_ctype(game *g, const field *f)
{
    ctype *t = parse_ctype(g, f->type, strlen(f->type));
    if (t->kind == CK_ENUM && f->size != 4) t->kind = CK_UNKNOWN;
    if (t->kind != CK_UNKNOWN && t->size != f->size) t->kind = CK_UNKNOWN;
    return t;
}

// The C# of a value as the game's C lays it out, in a Value or an element of the packet's STRINGS: NULL for one held
// as its bytes alone (an array). malloc'd.
static char *layout_cs(const game *g, const ctype *t)
{
    switch (t->kind) {
    case CK_PRIM: return copy(prims[t->prim].cs);
    case CK_STRING: return copy("global::Tide.TideText");
    case CK_ENTITY: return copy("global::Tide.TideEntityId");
    case CK_ENUM: return copy(g->enums[t->ref].full);
    case CK_STRUCT: return full_name(&g->types[t->ref]);
    case CK_LIST: return copy("global::Tide.TideList");
    case CK_SET:
    case CK_MAP: return copy("global::Tide.TideMap");
    case CK_GRID: return copy("global::Tide.TideGrid");
    default: return NULL;
    }
}

// The C# of a value as Unity code has it: text as a string, an entity as the GameObject showing it (a key's as its
// handle, as GameObjects come and go), a struct as its C# struct, containers as C#'s. NULL for none. malloc'd.
static char *live_cs(const game *g, const ctype *t, const bool key)
{
    char *elem = NULL, *value = NULL, *result = NULL;
    out o = {0};
    switch (t->kind) {
    case CK_PRIM: return copy(prims[t->prim].cs);
    case CK_STRING: return copy("string");
    case CK_ENTITY: return copy(key ? "global::Tide.TideEntityId" : "global::UnityEngine.GameObject");
    case CK_ENUM: return copy(g->enums[t->ref].full);
    case CK_STRUCT: {
        const type *s = &g->types[t->ref];
        if (!s->live) return full_name(s);
        putf(&o, "global::Tide.Game.%s%s%s", s->ns ? s->ns : "", s->ns ? "." : "", s->cs);
        return o.s;
    }
    case CK_LIST:
    case CK_SET:
    case CK_GRID:
    case CK_ARRAY:
        elem = live_cs(g, t->elem, t->kind == CK_SET);
        if (!elem || (t->kind == CK_ARRAY && t->elem->kind == CK_ARRAY)) break;
        if (t->kind == CK_LIST) putf(&o, "global::System.Collections.Generic.List<%s>", elem);
        else if (t->kind == CK_SET) putf(&o, "global::Tide.TideSet<%s>", elem);
        else if (t->kind == CK_GRID) putf(&o, "global::Tide.TideGrid%d<%s>", t->dims, elem);
        else putf(&o, "%s[]", elem);
        result = o.s;
        break;
    case CK_MAP:
        elem = live_cs(g, t->elem, true);
        value = live_cs(g, t->value, false);
        if (!elem || !value) break;
        putf(&o, "global::System.Collections.Generic.Dictionary<%s, %s>", elem, value);
        result = o.s;
        break;
    default:
        break;
    }
    free(elem);
    free(value);
    if (!result) free(o.s);
    return result;
}

// Converts `expr`, a value of `t` as laid out (`packet` and `runner` the names in scope), to Unity code's (live_cs),
// as a C# expression. Lambdas take nothing from around them, so C# makes each once.
static void put_conv(out *o, const game *g, const ctype *t, const char *expr, const char *packet, const char *runner,
                     const bool key, const int depth)
{
    char e[16], p[16], r[16];
    snprintf(e, sizeof e, "e%d", depth);
    snprintf(p, sizeof p, "p%d", depth);
    snprintf(r, sizeof r, "r%d", depth);
    switch (t->kind) {
    case CK_STRING: putf(o, "%s.Read(%s)", expr, packet); return;
    case CK_ENTITY:
        if (key) puts_(o, expr);
        else putf(o, "%s.GameObjectOf(%s)", runner, expr);
        return;
    case CK_STRUCT: {
        const type *s = &g->types[t->ref];
        if (!s->live) {
            puts_(o, expr);
            return;
        }
        char *live = live_cs(g, t, false);
        putf(o, "%s.From(%s, %s, %s)", live, expr, packet, runner);
        free(live);
        return;
    }
    case CK_LIST:
    case CK_SET:
    case CK_GRID:
    case CK_ARRAY: {
        char *layout = layout_cs(g, t->elem), *live = live_cs(g, t->elem, t->kind == CK_SET);
        static const char *const names[] = {"List", "Set", "Grid", "Array"};
        const char *name = names[t->kind == CK_LIST ? 0 : t->kind == CK_SET ? 1 : t->kind == CK_GRID ? 2 : 3];
        if (t->kind == CK_ARRAY) putf(o, "global::Tide.TideRead.Array<%s, %s>(%s, %s, %s, %d, (%s, %s, %s) => ", layout, live,
                                     packet, runner, expr, t->length, e, p, r);
        else if (t->kind == CK_GRID) putf(o, "global::Tide.TideRead.Grid%d<%s, %s>(%s, %s, %s.handle, (%s, %s, %s) => ", t->dims,
                                         layout, live, packet, runner, expr, e, p, r);
        else putf(o, "global::Tide.TideRead.%s<%s, %s>(%s, %s, %s.handle, (%s, %s, %s) => ", name, layout, live, packet, runner, expr,
                  e, p, r);
        put_conv(o, g, t->elem, e, p, r, t->kind == CK_SET, depth + 1);
        puts_(o, ")");
        free(layout);
        free(live);
        return;
    }
    case CK_MAP: {
        char *lk = layout_cs(g, t->elem), *lv = layout_cs(g, t->value);
        char *ck = live_cs(g, t->elem, true), *cv = live_cs(g, t->value, false);
        char k[16];
        snprintf(k, sizeof k, "k%d", depth);
        putf(o, "global::Tide.TideRead.Map<%s, %s, %s, %s>(%s, %s, %s.handle, (%s, %s, %s) => ", lk, lv, ck, cv, packet, runner, expr,
             k, p, r);
        put_conv(o, g, t->elem, k, p, r, true, depth + 1);
        putf(o, ", (%s, %s, %s) => ", e, p, r);
        put_conv(o, g, t->value, e, p, r, false, depth + 1);
        puts_(o, ")");
        free(lk);
        free(lv);
        free(ck);
        free(cv);
        return;
    }
    default: puts_(o, expr); return;
    }
}

// Whether Unity code can hold a value of `t` (live_cs).
static bool readable(const game *g, const ctype *t)
{
    char *cs = live_cs(g, t, false);
    const bool held = cs != NULL;
    free(cs);
    return held;
}

// Whether a command's value of `t` can be made of Unity code's (put_pack): what a command can hold.
static bool packs(const game *g, const ctype *t)
{
    switch (t->kind) {
    case CK_PRIM:
    case CK_STRING:
    case CK_ENTITY:
    case CK_ENUM: return true;
    case CK_STRUCT: return true;
    case CK_LIST: return t->elem->kind == CK_PRIM || t->elem->kind == CK_ENUM;
    default: (void)g; return false;
    }
}

// A command's value of `t` (put_conv's other way): `expr` Unity code's, `writer` the TideCommandWriter.
static void put_pack(out *o, const game *g, const ctype *t, const char *expr)
{
    switch (t->kind) {
    case CK_STRING: putf(o, "writer.Text(%s)", expr); return;
    case CK_ENTITY: putf(o, "writer.Entity(%s)", expr); return;
    case CK_STRUCT:
        if (g->types[t->ref].live) putf(o, "%s.Pack(writer)", expr);
        else puts_(o, expr);
        return;
    case CK_LIST: putf(o, "writer.List(%s)", expr); return;
    default: puts_(o, expr); return;
    }
}

// Whether Unity's serialization keeps a field of `t`, so a component's is authored in the inspector and baked: not a
// map or a grid, which the converter can't write as Tide (a map's literal is [] alone), nor a container of containers.
static bool authorable(const game *g, const ctype *t)
{
    switch (t->kind) {
    case CK_PRIM: return strcmp(prims[t->prim].form, "literal") != 0;
    case CK_STRING:
    case CK_ENTITY:
    case CK_ENUM: return true;
    case CK_STRUCT: return g->types[t->ref].live;
    case CK_LIST:
    case CK_ARRAY:
        return t->elem->kind != CK_LIST && t->elem->kind != CK_SET && t->elem->kind != CK_MAP && t->elem->kind != CK_GRID
            && t->elem->kind != CK_ARRAY && authorable(g, t->elem);
    case CK_SET: return t->elem->kind == CK_PRIM || t->elem->kind == CK_STRING || t->elem->kind == CK_ENUM;
    default: return false;
    }
}

// ---------------------------------------------------------------------------
// C#

static const char header[] = "// Written by tide unity from the game's schema. Do not edit: it changes with the game.\n"
                             "#pragma warning disable\n";

static void open_namespace(out *o, const char *ns)
{
    if (ns) putf(o, "namespace Tide.Game.%s\n{\n", ns);
    else puts_(o, "namespace Tide.Game\n{\n");
}

// A struct of the type, laid out as the game's C does: `nested` as a class's or struct's Value.
static void write_value(out *o, game *g, const type *t, const char *indent, const bool nested)
{
    const bool local = strcmp(t->kind, "local") == 0;
    putf(o, "%s// Tide's %s %s%s\n", indent, t->kind, t->bare, local ? "'s local fields" : nested ? ", as the game's C lays it out" : "");
    putf(o, "%s[global::Tide.TideType(\"", indent);
    put_escaped(o, t->bare);
    putf(o, "\"%s)]\n", local ? ", true" : "");
    putf(o, "%s[global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Explicit, Size = %u)]\n",
         indent, t->size ? t->size : 1u);
    putf(o, "%spublic unsafe partial struct %s\n%s{\n", indent, nested ? "Value" : t->cs, indent);
    for (int i = 0; i < t->field_count; i++) {
        const field *f = &t->fields[i];
        char *name = copy(t->ids[i]);
        char *cs = layout_cs(g, field_ctype(g, f));
        if (!cs) {
            if (f->size) {
                putf(o, "%s    [global::System.Runtime.InteropServices.FieldOffset(%u)] public fixed byte %s[%u]; // ", indent, f->offset, name, f->size);
                put_escaped(o, f->type);
                puts_(o, ": its bytes\n");
            }
        } else {
            putf(o, "%s    [global::System.Runtime.InteropServices.FieldOffset(%u)] public %s %s;\n", indent, f->offset, cs, name);
        }
        free(cs);
        free(name);
    }
    putf(o, "%s}\n", indent);
}

static void put_float(out *o, const float v)
{
    if (isnan(v)) puts_(o, "float.NaN");
    else if (isinf(v)) puts_(o, v > 0 ? "float.PositiveInfinity" : "float.NegativeInfinity");
    else putf(o, "%.9gf", (double)v);
}

static float float_at(const uint8_t *bytes, const uint32_t at)
{
    float v;
    memcpy(&v, bytes + at, sizeof v);
    return v;
}

static int32_t int_at(const uint8_t *bytes, const uint32_t at)
{
    int32_t v;
    memcpy(&v, bytes + at, sizeof v);
    return v;
}

// A value of `t`'s declared default as C#, from default bytes `d` at `at`: false where they don't say (text, lists).
static bool put_default(out *o, game *g, const ctype *t, const uint8_t *d, const uint32_t at)
{
    if (t->kind == CK_ENUM) {
        putf(o, "(%s)%d", g->enums[t->ref].full, (int)int_at(d, at));
        return true;
    }
    if (t->kind == CK_STRUCT && g->types[t->ref].live) {
        // Its fields that have one, by name
        const type *s = &g->types[t->ref];
        char *live = live_cs(g, t, false);
        out fields = {0};
        puts_(&fields, "");
        for (int i = 0; i < s->field_count; i++) {
            const field *f = &s->fields[i];
            char *id = copy(s->ids[i]);
            out value = {0};
            puts_(&value, "");
            if (put_default(&value, g, field_ctype(g, f), d, at + f->offset)) putf(&fields, "%s%s = %s", fields.n ? ", " : "", id, value.s);
            free(value.s);
            free(id);
        }
        putf(o, "new %s { %s }", live, fields.s);
        free(fields.s);
        free(live);
        return true;
    }
    if (t->kind != CK_PRIM) return false;
    const char *p = prims[t->prim].tide;
    static const char *const vectors[][2] = {
        {"float2", "Vector2"}, {"float3", "Vector3"}, {"float4", "Vector4"}, {"quaternion", "Quaternion"}, {"Color", "Color"},
    };
    for (size_t v = 0; v < sizeof vectors / sizeof vectors[0]; v++) {
        if (strcmp(p, vectors[v][0]) != 0) continue;
        const int count = v == 0 ? 2 : v == 1 ? 3 : 4;
        putf(o, "new global::UnityEngine.%s(", vectors[v][1]);
        for (int i = 0; i < count; i++) {
            if (i) puts_(o, ", ");
            put_float(o, float_at(d, at + (uint32_t)i * 4));
        }
        puts_(o, ")");
        return true;
    }
    if (strcmp(p, "int2") == 0 || strcmp(p, "int3") == 0) {
        const bool three = p[3] == '3';
        putf(o, "new global::UnityEngine.Vector%sInt(%d, %d", three ? "3" : "2", (int)int_at(d, at), (int)int_at(d, at + 4));
        if (three) putf(o, ", %d", (int)int_at(d, at + 8));
        puts_(o, ")");
        return true;
    }
    if (strcmp(p, "bool") == 0) puts_(o, d[at] ? "true" : "false");
    else if (strcmp(p, "float") == 0) put_float(o, float_at(d, at));
    else if (strcmp(p, "int") == 0 || strcmp(p, "char") == 0) putf(o, "%d", (int)int_at(d, at));
    else if (strcmp(p, "byte") == 0) putf(o, "%u", (unsigned)d[at]);
    else if (strcmp(p, "PlayerID") == 0) putf(o, "%uu", (unsigned)(uint32_t)int_at(d, at));
    else if (strcmp(p, "long") == 0) {
        int64_t v;
        memcpy(&v, d + at, sizeof v);
        putf(o, "%lldL", (long long)v);
    } else if (strcmp(p, "double") == 0) {
        double v;
        memcpy(&v, d + at, sizeof v);
        if (isnan(v)) puts_(o, "double.NaN");
        else if (isinf(v)) puts_(o, v > 0 ? "double.PositiveInfinity" : "double.NegativeInfinity");
        else putf(o, "%.17gd", v);
    } else return false;
    return true;
}

// A field's declaration, as print_outline says it, or NULL.
static const json *declared_field(const game *g, const type *t, const field *f)
{
    const json *declarations = json_get(g->outline, "declarations");
    for (int i = 0; declarations && declarations->kind == JSON_ARRAY && i < declarations->count; i++) {
        const json *d = declarations->items[i];
        if (strcmp(str(d, "name"), t->bare) != 0) continue;
        const json *fields = json_get(d, "fields");
        for (int k = 0; fields && fields->kind == JSON_ARRAY && k < fields->count; k++) {
            if (strcmp(str(fields->items[k], "name"), f->name) == 0) return fields->items[k];
        }
    }
    return NULL;
}

// A text field's default, as its declaration writes it between its quotes (print_outline's "text"), or NULL.
static const char *text_default(const game *g, const type *t, const field *f)
{
    return json_str(json_get(declared_field(g, t, f), "text"));
}

// A [Native] field: a C library's state, its package's own, which nothing in Unity shows, authors or reads.
static bool native_field(const game *g, const type *t, const field *f)
{
    const json *native = json_get(declared_field(g, t, f), "native");
    return native && native->kind == JSON_TRUE;
}

// A declaration of the game's, as print_outline says it, by its name and kind ("enum"), or any type's (NULL), or NULL.
static const json *declaration(const game *g, const char *name, const char *kind)
{
    const json *declarations = json_get(g->outline, "declarations");
    for (int i = 0; declarations && declarations->kind == JSON_ARRAY && i < declarations->count; i++) {
        const json *d = declarations->items[i];
        const char *of = str(d, "kind");
        const bool type = strcmp(of, "component") == 0 || strcmp(of, "singleton") == 0 || strcmp(of, "struct") == 0
                       || strcmp(of, "event") == 0 || strcmp(of, "command") == 0 || strcmp(of, "input") == 0;
        if (strcmp(str(d, "name"), name) == 0 && (kind ? strcmp(of, kind) == 0 : type)) return d;
    }
    return NULL;
}

// The part of `path` under `folder`, from it, or NULL: '/' and '\' alike, letters' case aside (Windows' paths).
static const char *under(const char *path, const char *folder)
{
    size_t n = strlen(folder);
    while (n && (folder[n - 1] == '/' || folder[n - 1] == '\\')) n--;
    if (!n || strlen(path) <= n) return NULL;
    for (size_t i = 0; i < n; i++) {
        const char a = path[i] == '\\' ? '/' : path[i], b = folder[i] == '\\' ? '/' : folder[i];
        const char la = a >= 'A' && a <= 'Z' ? (char)(a - 'A' + 'a') : a, lb = b >= 'A' && b <= 'Z' ? (char)(b - 'A' + 'a') : b;
        if (la != lb) return NULL;
    }
    return path[n] == '/' || path[n] == '\\' ? path + n + 1 : NULL;
}

// Where a file of the game's is, as a user of the project names it: from the project (tide unity names the game's
// files from Assets), or a package's file from its folder ("box3d package: box3d.tide"); NULL for tide's own
// declarations (Library/Tide's), and for nothing. malloc'd.
static char *where_declared(const game *g, const char *file)
{
    if (!file || !*file) return NULL;
    out o = {0};
    const char *in = NULL;
    for (int i = 0; !in && i < g->package_count; i++) {
        in = under(file, g->packages[i].folder);
        if (in) putf(&o, "the %s package's %s", g->packages[i].name, in);
    }
    const bool absolute = file[0] == '/' || file[0] == '\\' || (file[0] && file[1] == ':');
    if (!in && absolute) {
        in = g->project ? under(file, g->project) : NULL;
        if (!in || starts(in, "Library/") || starts(in, "Library\\")) return NULL;
        puts_(&o, in);
    } else if (!in) {
        // From the project (prefabs' and scenes' texts, tide's own Library/Tide's), else from Assets
        if (starts(file, "../") || starts(file, "..\\") || starts(file, "Library/") || starts(file, "Library\\")) return NULL;
        putf(&o, "%s%s", starts(file, "Assets/") || starts(file, "Assets\\") ? "" : "Assets/", file);
    }
    for (char *c = o.s; *c; c++) {
        if (*c == '\\') *c = '/';
    }
    return o.s;
}

// Text in XML: &, < and > as entities
static void put_xml(out *o, const char *s, const size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '&') puts_(o, "&amp;");
        else if (s[i] == '<') puts_(o, "&lt;");
        else if (s[i] == '>') puts_(o, "&gt;");
        else putn(o, s + i, 1);
    }
}

// A doc comment for Rider's and Visual Studio's hovers: a Tide declaration's doc (its comment lines), then `where`,
// a paragraph naming it and its file; nothing for neither.
static void put_summary(out *o, const char *indent, const char *doc, const char *where)
{
    if ((!doc || !*doc) && !where) return;
    if (!where && !strchr(doc, '\n')) {
        putf(o, "%s/// <summary>", indent);
        put_xml(o, doc, strlen(doc));
        puts_(o, "</summary>\n");
        return;
    }
    putf(o, "%s/// <summary>\n", indent);
    for (const char *line = doc; line && *line;) {
        const char *end = strchr(line, '\n');
        const size_t n = end ? (size_t)(end - line) : strlen(line);
        putf(o, "%s///%s", indent, n ? " " : "");
        put_xml(o, line, n);
        puts_(o, "\n");
        line = end ? end + 1 : NULL;
    }
    if (where) {
        putf(o, "%s/// <para>", indent);
        put_xml(o, where, strlen(where));
        puts_(o, "</para>\n");
    }
    putf(o, "%s/// </summary>\n", indent);
}

// A type's or an enum's doc comment: its doc, and where it's declared (none for tide's own)
static void put_type_summary(out *o, const game *g, const char *indent, const char *kind, const char *name, const char *lookup)
{
    const json *d = declaration(g, name, lookup);
    if (!d) return;
    char *file = where_declared(g, str(d, "file"));
    if (!file) return;
    out where = {0};
    putf(&where, "Tide's %s %s, declared in %s.", kind, name, file);
    put_summary(o, indent, json_str(json_get(d, "doc")), where.s);
    free(where.s);
    free(file);
}

// Whether a declaration is one of the game's or its packages', which have docs, not tide's own (where_declared)
static bool where_ok(const game *g, const json *d)
{
    char *file = where_declared(g, str(d, "file"));
    free(file);
    return file != NULL;
}

// A field's doc comment, where its declaration has a doc and is one of the game's (where_declared)
static void put_field_summary(out *o, const game *g, const type *t, const field *f, const char *indent)
{
    const json *d = declaration(g, t->bare, NULL);
    if (d && where_ok(g, d)) put_summary(o, indent, json_str(json_get(declared_field(g, t, f), "doc")), NULL);
}

// An authorable field's initial value: its declared default, else an empty container where it's one. False for none.
static bool put_initial(out *o, game *g, const type *t, const field *f, const ctype *ct)
{
    if (ct->kind == CK_STRING) {
        const char *text = text_default(g, t, f);
        putf(o, "\"%s\"", text ? text : "");
        return true;
    }
    if (ct->kind == CK_LIST || ct->kind == CK_SET) {
        char *live = live_cs(g, ct, false);
        putf(o, "new %s()", live);
        free(live);
        return true;
    }
    if (ct->kind == CK_ARRAY) {
        char *elem = live_cs(g, ct->elem, false);
        putf(o, "new %s[%d]", elem, ct->length);
        free(elem);
        return true;
    }
    return t->defaults && f->offset + f->size <= t->size && put_default(o, g, ct, t->defaults, f->offset);
}

// ---------------------------------------------------------------------------
// components.json: how Unity serializes each authored field, which the converter reads YAML by. A shape is a field's
// form, its Tide type, and what it's made of (a struct's fields, an element's shape); each is in "shapes" once, by its
// Tide type, and fields name their shape by its place there.

typedef struct shape {
    const char *tide, *form, *enum_name;
    char **names, **keys; // A struct's authored fields: Tide's names, Unity's keys...
    int *fields;          // ...and their shapes
    int field_count;
    int element, length; // A list's, set's or array's element, -1 for none; an array's length
} shape;

static int shape_of(game *g, const ctype *t);

static int find_shape(const game *g, const char *tide)
{
    for (int i = 0; i < g->shape_count; i++) {
        if (strcmp(g->shapes[i].tide, tide) == 0) return i;
    }
    return -1;
}

static int shape_of(game *g, const ctype *t)
{
    const int found = find_shape(g, t->tide);
    if (found >= 0) return found;
    g->shapes = realloc(g->shapes, sizeof *g->shapes * (size_t)(g->shape_count + 1));
    if (!g->shapes) abort();
    const int at = g->shape_count++;
    shape *s = &g->shapes[at];
    memset(s, 0, sizeof *s);
    s->tide = t->tide;
    s->element = -1;
    switch (t->kind) {
    case CK_PRIM: s->form = prims[t->prim].form; break;
    case CK_STRING: s->form = "string"; break;
    case CK_ENTITY: s->form = "entity"; break;
    case CK_ENUM: s->form = "enum"; s->enum_name = g->enums[t->ref].name; break;
    case CK_LIST:
    case CK_SET:
    case CK_ARRAY: {
        s->form = t->kind == CK_LIST ? "list" : t->kind == CK_SET ? "set" : "array";
        const int element = shape_of(g, t->elem);
        s = &g->shapes[at]; // The array may have moved
        s->element = element;
        s->length = t->length;
        break;
    }
    case CK_STRUCT: {
        s->form = "struct";
        const type *st = &g->types[t->ref];
        for (int i = 0; i < st->field_count; i++) {
            const field *f = &st->fields[i];
            char *id = copy(st->ids[i]);
            const ctype *ft = field_ctype(g, f);
            if (authorable(g, ft)) {
                const int fs = shape_of(g, ft);
                s = &g->shapes[at];
                s->names = realloc(s->names, sizeof *s->names * (size_t)(s->field_count + 1));
                s->keys = realloc(s->keys, sizeof *s->keys * (size_t)(s->field_count + 1));
                s->fields = realloc(s->fields, sizeof *s->fields * (size_t)(s->field_count + 1));
                if (!s->names || !s->keys || !s->fields) abort();
                s->names[s->field_count] = copy(f->name);
                s->keys[s->field_count] = copy(id[0] == '@' ? id + 1 : id);
                s->fields[s->field_count++] = fs;
            }
            free(id);
        }
        break;
    }
    default: s->form = "literal"; break;
    }
    return at;
}

static void free_trees(game *g)
{
    for (int i = 0; i < g->tree_count; i++) {
        free(g->trees[i]->tide);
        free(g->trees[i]);
    }
    free(g->trees);
    for (int i = 0; i < g->shape_count; i++) {
        for (int k = 0; k < g->shapes[i].field_count; k++) {
            free(g->shapes[i].names[k]);
            free(g->shapes[i].keys[k]);
        }
        free(g->shapes[i].names);
        free(g->shapes[i].keys);
        free(g->shapes[i].fields);
    }
    free(g->shapes);
}

static void put_json_text(out *o, const char *s)
{
    jbuf j = {0};
    jb_string(&j, s ? s : "");
    putn(o, j.data, j.len);
    jb_free(&j);
}

static void put_shapes(out *map, const game *g)
{
    puts_(map, "\"shapes\": [");
    for (int i = 0; i < g->shape_count; i++) {
        const shape *s = &g->shapes[i];
        puts_(map, i ? ",\n  {\"form\": " : "\n  {\"form\": ");
        put_json_text(map, s->form);
        puts_(map, ", \"type\": ");
        put_json_text(map, s->tide);
        if (s->enum_name) {
            puts_(map, ", \"enum\": ");
            put_json_text(map, s->enum_name);
        }
        if (s->element >= 0) putf(map, ", \"element\": %d", s->element);
        if (s->length) putf(map, ", \"length\": %d", s->length);
        if (strcmp(s->form, "struct") == 0) {
            puts_(map, ", \"fields\": [");
            for (int k = 0; k < s->field_count; k++) {
                puts_(map, k ? ", {\"name\": " : "{\"name\": ");
                put_json_text(map, s->names[k]);
                puts_(map, ", \"key\": ");
                put_json_text(map, s->keys[k]);
                putf(map, ", \"shape\": %d}", s->fields[k]);
            }
            puts_(map, "]");
        }
        puts_(map, "}");
    }
    puts_(map, g->shape_count ? "\n]" : "]");
}

// ---------------------------------------------------------------------------
// The classes and structs

// A field read live (a singleton's, an event's, a component's local or unauthored one): its declaration with
// `attribute`, and its line of ReadLive or From from `from` (`v.x`, `l.x`) into `self`.
static void put_live_field(out *o, out *live, game *g, const type *t, const field *f, const ctype *ct, const char *id,
                           const char *from, const char *self, const char *attribute, const char *indent)
{
    char *cs = live_cs(g, ct, false);
    if (!cs) return;
    put_field_summary(o, g, t, f, indent);
    putf(o, "%s%spublic %s %s;\n", indent, attribute, cs, id);
    putf(live, "%s    %s%s = ", indent, self, id);
    put_conv(live, g, ct, from, "packet", "runner", false, 0);
    puts_(live, ";\n");
    free(cs);
}

// The fields of a type's local part, after its own, and their lines of ReadLive: Unity doesn't keep them (they're
// this machine's, never authored), and the inspector shows them in Play.
static void put_local_fields(out *o, out *live, game *g, const type *t)
{
    if (t->local < 0 || !t->local_ids) return;
    const type *part = &g->types[t->local];
    out lines = {0};
    puts_(&lines, "");
    for (int i = 0; i < part->field_count; i++) {
        const field *f = &part->fields[i];
        char *id = copy(t->local_ids[i]);
        char *value = copy(part->ids[i]); // As write_value names its struct's
        out from = {0};
        putf(&from, "l.%s", value);
        put_live_field(o, &lines, g, part, f, field_ctype(g, f), id, from.s, "", "[global::System.NonSerialized] ", "        ");
        free(from.s);
        free(value);
        free(id);
    }
    if (lines.n) {
        char *name = full_name(part);
        putf(live, "            if (locals != null)\n            {\n                var l = *(%s*)locals;\n", name);
        // Indented once more
        for (const char *line = lines.s; *line;) {
            const char *end = strchr(line, '\n');
            const size_t n = end ? (size_t)(end - line) + 1 : strlen(line);
            puts_(live, "    ");
            putn(live, line, n);
            line += n;
        }
        puts_(live, "            }\n");
        free(name);
    }
    free(lines.s);
}

// A struct's C#: its fields as Unity code has them, Serializable so a component's field of it is authored in Unity,
// with From (its laid-out Value made Unity code's) and Pack (the other way, for a command), and its Value.
static void write_struct(out *o, game *g, const type *t)
{
    putf(o, "    // Tide's struct %s, as Unity code has it\n", t->bare);
    put_type_summary(o, g, "    ", "struct", t->bare, NULL);
    putf(o, "    [global::System.Serializable]\n    public unsafe partial struct %s\n    {\n", t->cs);
    out from = {0}, pack = {0};
    puts_(&from, "");
    puts_(&pack, "");
    for (int i = 0; i < t->field_count; i++) {
        const field *f = &t->fields[i];
        char *id = copy(t->ids[i]);
        const ctype *ct = field_ctype(g, f);
        char *cs = live_cs(g, ct, false);
        if (cs) {
            put_field_summary(o, g, t, f, "        ");
            putf(o, "        %spublic %s %s;\n", authorable(g, ct) ? "" : "[global::System.NonSerialized] ", cs, id);
            putf(&from, "            s.%s = ", id);
            out field_expr = {0};
            putf(&field_expr, "v.%s", id);
            put_conv(&from, g, ct, field_expr.s, "packet", "runner", false, 0);
            puts_(&from, ";\n");
            if (packs(g, ct)) {
                putf(&pack, "            v.%s = ", id);
                out self = {0};
                putf(&self, "%s", id);
                put_pack(&pack, g, ct, self.s);
                puts_(&pack, ";\n");
                free(self.s);
            }
            free(field_expr.s);
        }
        free(cs);
        free(id);
    }
    putf(o, "\n        public static %s From(Value v, global::Tide.TidePacket packet, global::Tide.TideRunner runner)\n        {\n"
            "            var s = new %s();\n%s            return s;\n        }\n\n", t->cs, t->cs, from.s);
    putf(o, "        public Value Pack(global::Tide.TideCommandWriter writer)\n        {\n            var v = new Value();\n%s"
            "            return v;\n        }\n\n", pack.s);
    free(from.s);
    free(pack.s);
    write_value(o, g, t, "        ", true);
    puts_(o, "    }\n");
}

// A class's C# (a TideComponent, TideSingleton, TideEvent or TideCommand, with its Value as the game's C lays it out),
// inside its namespace; a component's entry of components.json into `map`.
static void write_class(out *o, game *g, const type *t, out *map)
{
    const bool component = t->component, command = strcmp(t->base, "TideCommand") == 0;
    const bool event = strcmp(t->base, "TideEvent") == 0;
    const unity_component_rules *rules = component ? unity_rules_of(g->mappings, t->bare) : NULL;
    if (component) {
        putf(o, "    // Tide's component %s: its fields as authored outside Play, its entity's values in Play (TideComponent)\n", t->bare);
        put_type_summary(o, g, "    ", "component", t->bare, NULL);
        if (rules && rules->hidden) {
            puts_(o, "    // Its package's own (its unity.json): no GameObject is given it in Unity\n"
                     "    [global::UnityEngine.AddComponentMenu(\"\")]\n");
        } else {
            puts_(o, "    [global::UnityEngine.AddComponentMenu(\"Tide/");
            for (const char *c = t->bare; *c; c++) {
                if (*c == '.') puts_(o, "/");
                else if (*c == '"' || *c == '\\') {
                    putn(o, "\\", 1);
                    putn(o, c, 1);
                } else putn(o, c, 1);
            }
            puts_(o, "\")]\n");
        }
        puts_(o, "    [global::UnityEngine.DisallowMultipleComponent]\n"
                 "    [global::UnityEngine.Icon(global::Tide.TideIcon.Component)]\n");
        if (rules && rules->shape) {
            // What the Scene view draws of it, and edits with handles (com.tide.unity's TideShapes)
            putf(o, "    [global::Tide.TideShape(\"%s\", \"%s\", \"%s\", \"%s\")]\n", rules->shape, rules->size,
                 rules->center ? rules->center : "", rules->height ? rules->height : "");
        }
        if (rules && rules->vertices) {
            // Lists its inspector fills from a Mesh (com.tide.unity's TideMeshes), which the Scene view draws
            putf(o, "    [global::Tide.TideMesh(\"%s\", \"%s\", \"%s\", \"%s\")]\n", rules->vertices,
                 rules->indices ? rules->indices : "", rules->mesh_scale ? rules->mesh_scale : "", rules->placed ? rules->placed : "");
        }
        if (rules && rules->outline_count) {
            // A shape of points the Scene view draws, and edits with a handle each (com.tide.unity's TideShapes)
            puts_(o, "    [global::Tide.TideOutline(new[] { ");
            for (int k = 0; k < rules->outline_count; k++) putf(o, "%s\"%s\"", k ? ", " : "", rules->outline[k]);
            putf(o, " }, \"%s\", \"%s\", %s, %s)]\n", rules->outline_radius ? rules->outline_radius : "",
                 rules->outline_closed ? rules->outline_closed : "", rules->outline_loops ? "true" : "false",
                 rules->outline_hull ? "true" : "false");
        }
        if (rules && rules->joint_a) {
            // Its anchors the Scene view draws, and edits with handles (com.tide.unity's TideJoints)
            putf(o, "    [global::Tide.TideJoint(\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\")]\n", rules->joint_a, rules->joint_b,
                 rules->anchor_a, rules->anchor_b, rules->rotation_a ? rules->rotation_a : "",
                 rules->rotation_b ? rules->rotation_b : "");
        }
        if (rules && rules->heights) {
            // Fields its inspector fills from a Terrain (com.tide.unity's TideTerrains), which the Scene view draws
            putf(o, "    [global::Tide.TideTerrain(\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\")]\n", rules->heights,
                 rules->holes ? rules->holes : "", rules->columns, rules->rows, rules->terrain_scale,
                 rules->minimum ? rules->minimum : "", rules->maximum ? rules->maximum : "");
        }
        if (rules && rules->part_of) {
            // Its entity's shapes are on another's body: where they are is shown from it in Play (TideParts)
            putf(o, "    [global::Tide.TidePart(\"%s\")]\n", rules->part_of);
        }
    } else if (command) {
        putf(o, "    // Tide's command %s: TideRunner.Send sends it, as local code's Send does (TideCommand)\n", t->bare);
        put_type_summary(o, g, "    ", "command", t->bare, NULL);
    } else if (event) {
        putf(o, "    // Tide's event %s, as this machine's local handlers hear it: TideRunner.On, OnVerified (TideEvent)\n", t->bare);
        put_type_summary(o, g, "    ", "event", t->bare, NULL);
    } else {
        putf(o, "    // Tide's singleton %s, as Tide's views see it this frame: TideRunner.Get (TideSingleton)\n", t->bare);
        put_type_summary(o, g, "    ", "singleton", t->bare, NULL);
    }
    putf(o, "    public sealed partial class %s : global::Tide.%s\n    {\n", t->cs, t->base);
    if (component) {
        char guid[33];
        unity_code_guid(t->bare, guid);
        puts_(map, "{\"name\": ");
        put_json_text(map, t->bare);
        putf(map, ", \"guid\": \"%s\", \"fields\": [", guid);
    }
    out live = {0};
    puts_(&live, "");
    int mapped = 0;
    for (int i = 0; i < t->field_count; i++) {
        const field *f = &t->fields[i];
        char *id = copy(t->ids[i]);
        const char *key = id[0] == '@' ? id + 1 : id; // What Unity's YAML calls it
        const ctype *ct = field_ctype(g, f);
        out from = {0};
        putf(&from, "v.%s", id);
        if (!command && !event && native_field(g, t, f)) {
            // A C library's state: nothing of Unity's
        } else if (command) {
            // Its declared default, and its part of Pack
            char *cs = packs(g, ct) ? live_cs(g, ct, false) : NULL;
            if (cs) {
                put_field_summary(o, g, t, f, "        ");
                putf(o, "        public %s %s", cs, id);
                out initial = {0};
                puts_(&initial, "");
                if (put_initial(&initial, g, t, f, ct)) putf(o, " = %s", initial.s);
                free(initial.s);
                puts_(o, ";\n");
                putf(&live, "            v.%s = ", id);
                put_pack(&live, g, ct, id);
                puts_(&live, ";\n");
            }
            free(cs);
        } else if (!component) {
            put_live_field(o, &live, g, t, f, ct, id, from.s, "", "", "        ");
        } else if (unity_field_read_back(rules, f->name)) {
            // The package's answer, as the match has it: shown in Play, never authored
            put_live_field(o, &live, g, t, f, ct, id, from.s, "", "[global::System.NonSerialized, global::Tide.TideReadBack] ", "        ");
        } else if (authorable(g, ct)) {
            char *cs = live_cs(g, ct, false);
            const unity_scale by = unity_field_scale(rules, f->name);
            const bool point = unity_field_point(rules, f->name);
            put_field_summary(o, g, t, f, "        ");
            if (unity_field_hidden(rules, f->name)) puts_(o, "        [global::UnityEngine.HideInInspector] // Its package's own\n");
            if (by != UNITY_SCALE_NONE) puts_(o, "        [global::UnityEngine.Tooltip(\"By the GameObject's scale, as a collider's size is\")]\n");
            if (point) puts_(o, "        [global::UnityEngine.Tooltip(\"In the GameObject's own space, with its scale\")]\n");
            putf(o, "        public %s %s", cs, id);
            out initial = {0};
            puts_(&initial, "");
            if (put_initial(&initial, g, t, f, ct)) putf(o, " = %s", initial.s);
            free(initial.s);
            puts_(o, ";\n");
            putf(&live, "            %s = ", id);
            put_conv(&live, g, ct, from.s, "packet", "runner", false, 0);
            puts_(&live, ";\n");
            // The match has it by the scale: shown as authored, divided by it. Where it places the component on its
            // body, which a part's is another's, it's shown from where that is.
            char by_name[32] = "";
            if (by != UNITY_SCALE_NONE) {
                snprintf(by_name, sizeof by_name, "%s", unity_scale_name(by));
                by_name[0] = (char)(by_name[0] - 'a' + 'A');
            }
            if (point) {
                // The match has its points with the scale, a part's in its body's frame: shown in the GameObject's own
                putf(&live, "            %s = global::Tide.TideParts.Points(%s, this);\n", id, id);
            } else if (rules && rules->placed && strcmp(rules->placed, f->name) == 0 && ct->kind == CK_PRIM
                && (strcmp(prims[ct->prim].tide, "float2") == 0 || strcmp(prims[ct->prim].tide, "float3") == 0)) {
                putf(&live, "            %s = global::Tide.TideParts.Placed(%s, this%s%s);\n", id, id,
                     by_name[0] ? ", global::Tide.TideScale.By." : "", by_name);
            } else if (by_name[0]) {
                putf(&live, "            %s = global::Tide.TideScale.Unscale(%s, transform, global::Tide.TideScale.By.%s);\n", id, id, by_name);
            }
            free(cs);
            if (mapped++) puts_(map, ", ");
            puts_(map, "{\"name\": ");
            put_json_text(map, f->name);
            puts_(map, ", \"key\": ");
            put_json_text(map, key);
            putf(map, ", \"shape\": %d}", shape_of(g, ct));
        } else if (readable(g, ct)) {
            // Read in Play, never authored: a map, a grid, a container of containers
            put_live_field(o, &live, g, t, f, ct, id, from.s, "", "[global::System.NonSerialized] ", "        ");
        } else {
            // A Tide value as text: what Unity can't hold as itself
            put_field_summary(o, g, t, f, "        ");
            puts_(o, "        [global::UnityEngine.Tooltip(\"");
            put_escaped(o, f->type);
            puts_(o, ", as Tide code writes a value of it: empty for its default\")]\n");
            putf(o, "        public string %s = \"\";\n", id);
            if (mapped++) puts_(map, ", ");
            puts_(map, "{\"name\": ");
            put_json_text(map, f->name);
            puts_(map, ", \"key\": ");
            put_json_text(map, key);
            putf(map, ", \"shape\": %d}", shape_of(g, ct));
        }
        free(from.s);
        free(id);
    }
    out locals = {0};
    puts_(&locals, "");
    if (!command && !event) put_local_fields(o, &locals, g, t);
    if (component) puts_(map, "]}");
    puts_(o, "\n        public override string TideName => \"");
    put_escaped(o, t->bare);
    puts_(o, "\";\n\n");
    if (command) {
        puts_(o, "        protected override void Pack(global::Tide.TideCommandWriter writer)\n        {\n            var v = new Value();\n");
        puts_(o, live.s);
        puts_(o, "            writer.Value(v);\n        }\n\n");
    } else {
        putf(o, "        protected override unsafe void ReadLive(byte* values, %sglobal::Tide.TidePacket packet, global::Tide.TideRunner runner)\n        {\n",
             event ? "" : "byte* locals, ");
        if (live.n) {
            puts_(o, "            var v = *(Value*)values;\n");
            puts_(o, live.s);
        }
        puts_(o, locals.s);
        puts_(o, "        }\n\n");
    }
    free(locals.s);
    free(live.s);
    write_value(o, g, t, "        ", true);
    puts_(o, "    }\n");
}

// A component's file (Components/...): its class, and its entry of components.json into `map`.
static char *write_component(game *g, const type *t, out *map)
{
    out o = {0};
    puts_(&o, header);
    open_namespace(&o, t->ns);
    write_class(&o, g, t, map);
    puts_(&o, "}\n");
    return o.s;
}

static char *write_types(game *g, const json *schema)
{
    out o = {0};
    puts_(&o, header);
    puts_(&o, "namespace Tide.Game\n{\n    // What this game's C# was made from: a view packet of another build isn't read by it.\n"
              "    public static class TideGameSchema\n    {\n");
    puts_(&o, "        public const string Hash = \"");
    put_escaped(&o, str(schema, "hash"));
    puts_(&o, "\";\n        public const string Game = \"");
    put_escaped(&o, str(schema, "game"));
    putf(&o, "\";\n        public const int TickRate = %u;\n", number(schema, "tickRate", 0));
    static const char *const lists[] = {"Scenes", "Prefabs"};
    for (int l = 0; l < 2; l++) {
        putf(&o, "        public static readonly string[] %s = { ", lists[l]);
        const json *items = json_get(schema, l ? "prefabs" : "scenes");
        for (int i = 0; items && items->kind == JSON_ARRAY && i < items->count; i++) {
            putf(&o, "%s\"", i ? ", " : "");
            put_escaped(&o, json_str(items->items[i]));
            puts_(&o, "\"");
        }
        puts_(&o, " };\n");
    }
    puts_(&o, "    }\n\n    // The game's components, singletons and events as the package's classes, for the runner to read their values into\n"
              "    static class TideGameTypes\n    {\n"
              "        [global::UnityEngine.RuntimeInitializeOnLoadMethod(global::UnityEngine.RuntimeInitializeLoadType.SubsystemRegistration)]\n"
              "        static void Register()\n        {\n");
    for (int i = 0; i < g->type_count; i++) {
        const type *t = &g->types[i];
        if (!t->base || strcmp(t->base, "TideCommand") == 0) continue;
        puts_(&o, "            global::Tide.TideTypes.Register(TideGameSchema.Hash, \"");
        put_escaped(&o, t->bare);
        putf(&o, "\", typeof(global::Tide.Game.%s%s%s));\n", t->ns ? t->ns : "", t->ns ? "." : "", t->cs);
    }
    puts_(&o, "        }\n    }\n}\n");
    for (int i = 0; i < g->enum_count; i++) {
        const enumeration *e = &g->enums[i];
        open_namespace(&o, e->ns);
        put_type_summary(&o, g, "    ", "enum", e->name, "enum");
        putf(&o, "    public enum %s : int\n    {\n", e->cs);
        const json *declared = declaration(g, e->name, "enum");
        const json *docs = declared && where_ok(g, declared) ? json_get(declared, "memberDocs") : NULL;
        for (int k = 0; e->members && e->members->kind == JSON_ARRAY && k < e->members->count; k++) {
            const int value = e->values && e->values->kind == JSON_ARRAY && k < e->values->count ? json_int(e->values->items[k], k) : k;
            if (k >= e->names_count || !e->ids[k]) continue;
            if (docs && docs->kind == JSON_ARRAY && k < docs->count) put_summary(&o, "        ", json_str(docs->items[k]), NULL);
            putf(&o, "        %s = %d,\n", e->ids[k], value);
        }
        puts_(&o, "    }\n}\n");
    }
    for (int i = 0; i < g->type_count; i++) {
        const type *t = &g->types[i];
        if (t->component) continue;
        open_namespace(&o, t->ns);
        if (t->base) write_class(&o, g, t, NULL);
        else if (t->live) write_struct(&o, g, t);
        else write_value(&o, g, t, "    ", false);
        puts_(&o, "}\n");
    }
    return o.s;
}

// ---------------------------------------------------------------------------
// The files

static bool write_if_changed(const char *path, const char *text, bool *failed)
{
    const size_t size = strlen(text);
    size_t had = 0;
    char *old = sys_read_file(path, &had);
    const bool same = old && had == size && memcmp(old, text, size) == 0;
    free(old);
    if (same) return false;
    char *dir = path_dir(path);
    sys_mkdirs(dir);
    free(dir);
    if (!sys_write_file(path, text, size)) {
        fprintf(stderr, "tide: can't write %s\n", path);
        *failed = true;
    }
    return true;
}

typedef struct kept {
    char **paths; // Under Components/, from it, with '/'
    int count;
    const char *dir;
    bool failed;
} kept;

static bool is_kept(const kept *k, const char *relative)
{
    for (int i = 0; i < k->count; i++) {
        if (strcmp(k->paths[i], relative) == 0) return true;
    }
    return false;
}

typedef struct sweep {
    kept *k;
    const char *dir;      // The folder listed
    const char *relative; // ...from Components/, "" or ending in '/'
} sweep;

static void sweep_entry(void *user, const char *name, const bool is_dir)
{
    const sweep *s = user;
    if (name[0] == '.') return;
    char *path = path_join(s->dir, name);
    out relative = {0};
    putf(&relative, "%s%s%s", s->relative, name, is_dir ? "/" : "");
    if (is_dir) {
        const sweep inner = {s->k, path, relative.s};
        sys_list(path, sweep_entry, (void *)&inner);
    } else {
        const size_t n = strlen(relative.s);
        char *script = copy(relative.s);
        if (n > 5 && strcmp(script + n - 5, ".meta") == 0) script[n - 5] = '\0';
        if (!is_kept(s->k, script)) sys_remove(path); // A component the game no longer has
        free(script);
    }
    free(relative.s);
    free(path);
}

static const type *component_named(const game *g, const char *name)
{
    for (int i = 0; name && i < g->type_count; i++) {
        if (g->types[i].component && strcmp(g->types[i].bare, name) == 0) return &g->types[i];
    }
    return NULL;
}

// Whether every component and field the packages' rules name is one the game has: a rule that names none, left from
// a package's earlier version, is an error, as it would be nothing silently. False after saying which.
static bool rules_name_the_game(const game *g, const unity_mappings *m)
{
    bool ok = true;
    for (int i = 0; i < m->component_count; i++) {
        const unity_component_rules *r = &m->components[i];
        const type *t = component_named(g, r->name);
        if (!t) {
            fprintf(stderr, "%s: error: it has rules for %s, which isn't a component of the game's packages: take them out, "
                            "or name the component as its package declares it\n", r->file, r->name);
            ok = false;
            continue;
        }
        if (r->part_body && !component_named(g, r->part_body)) {
            fprintf(stderr, "%s: error: %s's \"part\" names the body %s, which isn't a component of the game's packages\n",
                    r->file, r->name, r->part_body);
            ok = false;
        }
        for (int k = 0; k < r->point_count; k++) {
            for (int f = 0; f < t->field_count; f++) {
                const char *type = t->fields[f].type;
                if (strcmp(t->fields[f].name, r->points[k]) != 0 || strcmp(type, "float2") == 0 || strcmp(type, "float3") == 0
                    || strcmp(type, "List<float2>") == 0 || strcmp(type, "List<float3>") == 0) {
                    continue;
                }
                fprintf(stderr, "%s: error: %s's \"points\" names '%s', a %s: points are a float2 or float3, or a list of them\n",
                        r->file, r->name, r->points[k], type);
                ok = false;
            }
        }
        const char *named[256];
        const int count = unity_rules_fields(r, named, (int)(sizeof named / sizeof named[0]));
        for (int k = 0; k < count; k++) {
            bool has = false;
            for (int f = 0; f < t->field_count && !has; f++) has = strcmp(t->fields[f].name, named[k]) == 0;
            if (!has) {
                fprintf(stderr, "%s: error: %s's rules name '%s', which isn't one of its fields\n", r->file, r->name, named[k]);
                ok = false;
            }
        }
    }
    return ok;
}

static bool write_code(const char *dir, const char *schema_text, const char *outline_text, const unity_package *packages,
                       const int package_count, const bool sweeps)
{
    // What the packages say of their components, read before the schema: JSON's trees go together
    unity_mappings mappings;
    if (!unity_mappings_read(packages, package_count, &mappings)) {
        unity_mappings_free(&mappings);
        return false;
    }
    json *schema = json_parse(schema_text, strlen(schema_text));
    if (!schema) {
        fprintf(stderr, "tide: the game's schema isn't JSON\n");
        unity_mappings_free(&mappings);
        return false;
    }
    json *outline = outline_text ? json_parse(outline_text, strlen(outline_text)) : NULL;
    game g = {0};
    g.outline = outline;
    char *above = path_dir(dir);
    char *project = above ? path_dir(above) : NULL;
    g.project = project;
    g.packages = packages;
    g.package_count = package_count;
    g.mappings = &mappings;
    if (!read_game(schema, &g)) {
        free(above);
        free(project);
        json_release();
        unity_mappings_free(&mappings);
        fprintf(stderr, "tide: the game's schema has no types\n");
        return false;
    }
    bool failed = !rules_name_the_game(&g, &mappings);
    char *components = path_join(dir, "Components");
    kept k = {NULL, 0, components, false};
    out map = {0};
    puts_(&map, "{\"hash\": ");
    jbuf hash = {0};
    jb_string(&hash, str(schema, "hash"));
    putn(&map, hash.data, hash.len);
    jb_free(&hash);
    puts_(&map, ", \"components\": [");
    int mapped = 0;
    for (int i = 0; i < g.type_count; i++) {
        const type *t = &g.types[i];
        if (!t->component) continue;
        out relative = {0};
        if (t->ns) {
            puts_(&relative, t->ns);
            for (char *c = relative.s; *c; c++) {
                if (*c == '.') *c = '/';
            }
            puts_(&relative, "/");
        }
        puts_(&relative, t->cs[0] == '@' ? t->cs + 1 : t->cs);
        puts_(&relative, ".cs");
        k.paths = realloc(k.paths, sizeof *k.paths * (size_t)(k.count + 1));
        if (!k.paths) abort();
        k.paths[k.count++] = copy(relative.s);
        puts_(&map, mapped++ ? ",\n  " : "\n  ");
        char *code = write_component(&g, t, &map);
        char *path = path_join(components, relative.s);
        write_if_changed(path, code, &failed);
        out meta = {0};
        char guid[33];
        unity_code_guid(t->bare, guid);
        putf(&meta, "fileFormatVersion: 2\nguid: %s\n", guid);
        out meta_path = {0};
        putf(&meta_path, "%s.meta", path);
        write_if_changed(meta_path.s, meta.s, &failed);
        free(meta_path.s);
        free(meta.s);
        free(path);
        free(code);
        free(relative.s);
    }
    puts_(&map, mapped ? "\n], " : "], ");
    put_shapes(&map, &g);
    puts_(&map, ", \"enums\": [");
    for (int i = 0; i < g.enum_count; i++) {
        const enumeration *e = &g.enums[i];
        puts_(&map, i ? ",\n  " : "\n  ");
        jbuf j = {0};
        jb_string(&j, e->name);
        puts_(&map, "{\"name\": ");
        putn(&map, j.data, j.len);
        jb_free(&j);
        jbuf members = {0}, values = {0};
        if (e->members) jb_json(&members, e->members);
        else jb_put(&members, "[]");
        if (e->values) jb_json(&values, e->values);
        else jb_put(&values, "[]");
        putf(&map, ", \"members\": %.*s, \"values\": %.*s}", (int)members.len, members.data, (int)values.len, values.data);
        jb_free(&members);
        jb_free(&values);
    }
    puts_(&map, g.enum_count ? "\n]}\n" : "]}\n");
    char *types = write_types(&g, schema);
    char *types_path = path_join(dir, "TideGame.cs");
    write_if_changed(types_path, types, &failed);
    char *meta_path = path_join(dir, "TideGame.cs.meta");
    char guid[33];
    unity_code_guid("Tide.Game types", guid);
    out meta = {0};
    putf(&meta, "fileFormatVersion: 2\nguid: %s\n", guid);
    write_if_changed(meta_path, meta.s, &failed);
    char *map_path = path_join(dir, "components.json");
    write_if_changed(map_path, map.s, &failed);
    // Scripts of components the game no longer has go, so Unity says Missing where they were
    const sweep s = {&k, components, ""};
    if (sweeps) sys_list(components, sweep_entry, (void *)&s);
    for (int i = 0; i < k.count; i++) free(k.paths[i]);
    free(k.paths);
    free(meta.s);
    free(meta_path);
    free(map_path);
    free(map.s);
    free(types_path);
    free(types);
    free(components);
    free_trees(&g);
    free_game(&g);
    free(above);
    free(project);
    json_release();
    unity_mappings_free(&mappings);
    return !failed;
}

bool unity_code_write(const char *dir, const char *schema, const char *outline, const unity_package *packages,
                      const int package_count)
{
    return write_code(dir, schema, outline, packages, package_count, true);
}

bool unity_code_add(const char *dir, const char *schema, const char *outline, const unity_package *packages,
                    const int package_count)
{
    return write_code(dir, schema, outline, packages, package_count, false);
}

// ---------------------------------------------------------------------------
// The record of renamed and removed names (unity_code_record)

typedef struct entry {
    char *was, *now, *file; // now NULL: removed; file: where `now` is declared, as the project opens it, or NULL
    int line;
} entry;

typedef struct entries {
    entry *items;
    int count, capacity;
} entries;

static char *copy_or_null(const char *s)
{
    return s ? copy(s) : NULL;
}

static void entry_add(entries *e, const char *was, const char *now, const char *file, const int line)
{
    if (e->count == e->capacity) {
        e->capacity = e->capacity ? e->capacity * 2 : 16;
        e->items = realloc(e->items, sizeof *e->items * (size_t)e->capacity);
        if (!e->items) abort();
    }
    e->items[e->count++] = (entry){copy(was), copy_or_null(now), copy_or_null(file), line};
}

static void entry_free(entry *e)
{
    free(e->was);
    free(e->now);
    free(e->file);
}

// Where a declaration's file is, as the project opens it: from the project's folder, else as tide named it
static char *open_path(const game *g, const char *file)
{
    if (!file || !*file) return NULL;
    out o = {0};
    const bool absolute = file[0] == '/' || file[0] == '\\' || (file[0] && file[1] == ':');
    const char *in = absolute && g->project ? under(file, g->project) : NULL;
    if (in) puts_(&o, in);
    else if (absolute || starts(file, "Assets/") || starts(file, "Library/")) puts_(&o, file);
    else putf(&o, "Assets/%s", file);
    for (char *c = o.s; *c; c++) {
        if (*c == '\\') *c = '/';
    }
    return o.s;
}

// A field of a declaration as print_outline says it, by its Tide name
static const json *outline_field(const json *declared, const char *name)
{
    const json *fields = json_get(declared, "fields");
    for (int k = 0; fields && fields->kind == JSON_ARRAY && k < fields->count; k++) {
        if (strcmp(str(fields->items[k], "name"), name) == 0) return fields->items[k];
    }
    return NULL;
}

// Whether a type's C# name is one of the record's: the game's own types, not tide's
static bool recorded(const unity_cs_type *t)
{
    return !t->scene && strcmp(t->kind, "local") != 0 && strncmp(t->name, "tide_", 5) != 0
        && strncmp(t->name, "UnityEngine.", 12) != 0;
}

static bool same_fields(const unity_cs_type *a, const unity_cs_type *b)
{
    if (a->field_count != b->field_count) return false;
    for (int k = 0; k < a->field_count; k++) {
        if (strcmp(a->fields[k], b->fields[k]) != 0) return false;
    }
    return true;
}

static const char *field_type(const json *schema, const int type, const int field)
{
    const json *t = json_get(schema, "types");
    const json *fields = t && type < t->count ? json_get(t->items[type], "fields") : NULL;
    return fields && field < fields->count ? str(fields->items[field], "type") : "";
}

static const char *member_name(const char *id)
{
    return id[0] == '@' ? id + 1 : id;
}

typedef struct differ {
    const game *g;
    const json *was_schema, *schema;
    const unity_cs_names *was, *now;
    entries found;
    bool removals;
} differ;

static void found_one(differ *d, const char *was, const char *now, const char *file, const int line)
{
    if (now && strcmp(was, now) == 0) return;
    if (!now && !d->removals) return;
    entry_add(&d->found, was, now, file, line);
}

// What became of a type's fields (`a_ids`, its own or its class's local ones), from the old type `a` (the schema's
// type `fa`) to the new `b` (`fb`): kept by name, renamed in place with its type, or the one left of its type; else
// removed
static void diff_fields(differ *d, const char *was_owner, const char *now_owner, const unity_cs_type *a, char *const *a_ids,
                        const int fa, const unity_cs_type *b, char *const *b_ids, const int fb, const json *declared)
{
    if (!a || !a_ids) return;
    const bool have = b && b_ids;
    bool *taken = calloc((size_t)(have ? b->field_count : 0) + 1, sizeof *taken);
    int *pair = malloc(sizeof *pair * ((size_t)a->field_count + 1));
    if (!taken || !pair) abort();
    for (int k = 0; k < a->field_count; k++) {
        pair[k] = -1;
        for (int m = 0; have && m < b->field_count && pair[k] < 0; m++) {
            if (!taken[m] && strcmp(a->fields[k], b->fields[m]) == 0) pair[k] = m;
        }
        if (pair[k] >= 0) taken[pair[k]] = true;
    }
    for (int k = 0; have && k < a->field_count; k++) {
        if (pair[k] >= 0 || k >= b->field_count || taken[k]) continue;
        if (strcmp(field_type(d->was_schema, fa, k), field_type(d->schema, fb, k)) == 0) {
            pair[k] = k; // In its place, of its type
            taken[k] = true;
        }
    }
    for (int k = 0; have && k < a->field_count; k++) {
        if (pair[k] >= 0) continue;
        const char *of = field_type(d->was_schema, fa, k);
        int alone = -1, gone = 0, come = 0;
        for (int x = 0; x < a->field_count; x++) gone += pair[x] < 0 && strcmp(field_type(d->was_schema, fa, x), of) == 0;
        for (int m = 0; m < b->field_count; m++) {
            if (taken[m] || strcmp(field_type(d->schema, fb, m), of) != 0) continue;
            come++;
            alone = m;
        }
        if (gone == 1 && come == 1) {
            pair[k] = alone;
            taken[alone] = true;
        }
    }
    for (int k = 0; k < a->field_count; k++) {
        out was = {0}, now = {0};
        putf(&was, "%s.%s", was_owner, member_name(a_ids[k]));
        if (pair[k] >= 0) {
            putf(&now, "%s.%s", now_owner, member_name(b_ids[pair[k]]));
            const json *line = json_get(outline_field(declared, b->fields[pair[k]]), "line");
            char *file = open_path(d->g, str(declared, "file"));
            found_one(d, was.s, now.s, file, line && line->kind == JSON_NUMBER ? (int)line->number : json_int(json_get(declared, "line"), 0));
            free(file);
        } else {
            found_one(d, was.s, NULL, NULL, 0);
        }
        free(was.s);
        free(now.s);
    }
    free(taken);
    free(pair);
}

static void diff_types(differ *d)
{
    const unity_cs_names *a = d->was, *b = d->now;
    int *pair = malloc(sizeof *pair * ((size_t)a->type_count + 1));
    bool *taken = calloc((size_t)b->type_count + 1, sizeof *taken);
    if (!pair || !taken) abort();
    for (int i = 0; i < a->type_count; i++) {
        pair[i] = -1;
        for (int j = 0; j < b->type_count && pair[i] < 0; j++) {
            if (strcmp(a->types[i].name, b->types[j].name) == 0) pair[i] = j;
        }
        if (pair[i] >= 0) taken[pair[i]] = true;
    }
    // Renamed: the one gone and the one come of its kind with its fields; else the only ones of their kind
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < a->type_count; i++) {
            if (pair[i] >= 0 || !recorded(&a->types[i])) continue;
            int alone = -1, come = 0, gone = 0;
            for (int j = 0; j < b->type_count; j++) {
                if (taken[j] || !recorded(&b->types[j]) || strcmp(b->types[j].kind, a->types[i].kind) != 0) continue;
                if (pass == 0 && !same_fields(&a->types[i], &b->types[j])) continue;
                come++;
                alone = j;
            }
            for (int x = 0; x < a->type_count; x++) {
                gone += pair[x] < 0 && recorded(&a->types[x]) && strcmp(a->types[x].kind, a->types[i].kind) == 0
                     && (pass == 1 || (alone >= 0 && same_fields(&a->types[x], &b->types[alone])));
            }
            if (come == 1 && gone == 1) {
                pair[i] = alone;
                taken[alone] = true;
            }
        }
    }
    for (int i = 0; i < a->type_count; i++) {
        const unity_cs_type *t = &a->types[i];
        if (!recorded(t)) continue;
        char *was = unity_cs_full(t->ns, t->cs);
        if (pair[i] < 0) {
            found_one(d, was, NULL, NULL, 0);
            free(was);
            continue;
        }
        const unity_cs_type *u = &b->types[pair[i]];
        char *now = unity_cs_full(u->ns, u->cs);
        const json *declared = declaration(d->g, u->bare, NULL);
        char *file = open_path(d->g, str(declared, "file"));
        found_one(d, was, now, file, json_int(json_get(declared, "line"), 0));
        free(file);
        diff_fields(d, was, now, t, t->ids, i, u, u->ids, pair[i], declared);
        // Its class's local fields, its local part's
        if (t->local >= 0 && u->local >= 0) {
            diff_fields(d, was, now, &a->types[t->local], t->local_ids, t->local, &b->types[u->local], u->local_ids, u->local, declared);
        }
        free(was);
        free(now);
    }
    free(pair);
    free(taken);
}

static int enum_named(const unity_cs_names *n, const char *name)
{
    for (int k = 0; k < n->enum_count; k++) {
        if (strcmp(n->enums[k].name, name) == 0) return k;
    }
    return -1;
}

static void diff_enums(differ *d)
{
    const unity_cs_names *a = d->was, *b = d->now;
    for (int i = 0; i < a->enum_count; i++) {
        const unity_cs_enum *e = &a->enums[i];
        int j = enum_named(b, e->name);
        if (j < 0) {
            // Renamed: the one enum gone and the one come
            int gone = 0, come = 0, alone = -1;
            for (int k = 0; k < a->enum_count; k++) gone += enum_named(b, a->enums[k].name) < 0;
            for (int m = 0; m < b->enum_count; m++) {
                if (enum_named(a, b->enums[m].name) >= 0) continue;
                come++;
                alone = m;
            }
            if (gone == 1 && come == 1) j = alone;
        }
        char *was = unity_cs_full(e->ns, e->cs);
        if (j < 0) {
            found_one(d, was, NULL, NULL, 0);
            free(was);
            continue;
        }
        const unity_cs_enum *f = &b->enums[j];
        char *now = unity_cs_full(f->ns, f->cs);
        const json *declared = declaration(d->g, f->name, "enum");
        char *file = open_path(d->g, str(declared, "file"));
        const int line = json_int(json_get(declared, "line"), 0);
        found_one(d, was, now, file, line);
        // Its members: by name, else in their place where the name there is new
        for (int k = 0; k < e->member_count; k++) {
            if (!e->ids[k]) continue;
            int m = -1;
            for (int x = 0; x < f->member_count && m < 0; x++) {
                if (f->ids[x] && e->members[k] && f->members[x] && strcmp(e->members[k], f->members[x]) == 0) m = x;
            }
            if (m < 0 && k < f->member_count && f->ids[k]) {
                bool named = false;
                for (int x = 0; x < e->member_count; x++) named = named || (e->members[x] && f->members[k] && strcmp(e->members[x], f->members[k]) == 0);
                if (!named) m = k;
            }
            out a_name = {0}, b_name = {0};
            putf(&a_name, "%s.%s", was, member_name(e->ids[k]));
            if (m >= 0) {
                putf(&b_name, "%s.%s", now, member_name(f->ids[m]));
                found_one(d, a_name.s, b_name.s, file, line);
            } else {
                found_one(d, a_name.s, NULL, NULL, 0);
            }
            free(a_name.s);
            free(b_name.s);
        }
        free(file);
        free(was);
        free(now);
    }
}

// Whether a C# name (after Tide.Game.) is one of the game's types', a field's or a member's
static bool has_name(const unity_cs_names *n, const char *name)
{
    bool found = false;
    for (int i = 0; i < n->type_count && !found; i++) {
        const unity_cs_type *t = &n->types[i];
        char *full = unity_cs_full(t->ns, t->cs);
        const size_t len = strlen(full);
        if (strcmp(full, name) == 0) {
            found = true;
        } else if (strncmp(full, name, len) == 0 && name[len] == '.') {
            const char *member = name + len + 1;
            for (int k = 0; k < t->field_count && !found; k++) found = strcmp(member_name(t->ids[k]), member) == 0;
            const int locals = t->local_ids ? n->types[t->local].field_count : 0;
            for (int k = 0; k < locals && !found; k++) found = strcmp(member_name(t->local_ids[k]), member) == 0;
        }
        free(full);
    }
    for (int i = 0; i < n->enum_count && !found; i++) {
        const unity_cs_enum *e = &n->enums[i];
        char *full = unity_cs_full(e->ns, e->cs);
        const size_t len = strlen(full);
        if (strcmp(full, name) == 0) {
            found = true;
        } else if (strncmp(full, name, len) == 0 && name[len] == '.') {
            for (int k = 0; k < e->member_count && !found; k++) found = e->ids[k] && strcmp(member_name(e->ids[k]), name + len + 1) == 0;
        }
        free(full);
    }
    return found;
}

bool unity_code_record(const char *record, const char *was, const char *schema, const char *outline, const char *dir,
                       const unity_package *packages, const int package_count, const bool removals)
{
    const json *a = was ? json_parse(was, strlen(was)) : NULL;
    const json *b = schema ? json_parse(schema, strlen(schema)) : NULL;
    const json *o = outline ? json_parse(outline, strlen(outline)) : NULL;
    unity_cs_names was_names = {0}, names = {0};
    if (!b || !unity_cs_read(b, &names)) {
        json_release();
        return true; // Nothing to say of
    }
    char *above = path_dir(dir);
    char *project = above ? path_dir(above) : NULL;
    game g = {0};
    g.outline = o;
    g.project = project;
    g.packages = packages;
    g.package_count = package_count;
    differ d = {&g, a, b, NULL, &names, {0}, removals};
    if (a && unity_cs_read(a, &was_names)) {
        d.was = &was_names;
        diff_types(&d);
        diff_enums(&d);
    }
    // The record so far, then what this build found
    entries all = {0};
    size_t size = 0;
    char *old = sys_read_file(record, &size);
    const json *kept = old ? json_get(json_parse(old, size), "renamed") : NULL;
    for (int i = 0; kept && kept->kind == JSON_ARRAY && i < kept->count; i++) {
        const json *e = kept->items[i];
        if (!json_str(json_get(e, "was"))) continue;
        entry_add(&all, str(e, "was"), json_str(json_get(e, "now")), json_str(json_get(e, "file")), json_int(json_get(e, "line"), 0));
    }
    for (int i = 0; i < d.found.count; i++) {
        const entry *f = &d.found.items[i];
        for (int k = 0; k < all.count; k++) {
            entry *x = &all.items[k];
            if (x->now && strcmp(x->now, f->was) == 0) { // A rename of a renamed name: this one's new name is its
                free(x->now);
                free(x->file);
                x->now = copy_or_null(f->now);
                x->file = copy_or_null(f->file);
                x->line = f->line;
            }
        }
        for (int k = 0; k < all.count; k++) {
            if (strcmp(all.items[k].was, f->was) != 0) continue;
            entry_free(&all.items[k]);
            memmove(&all.items[k], &all.items[k + 1], sizeof *all.items * (size_t)(all.count - k - 1));
            all.count--;
            k--;
        }
        entry_add(&all, f->was, f->now, f->file, f->line);
    }
    // Names the C# has again, and renames back to what they were, are nothing to explain; the newest 100 kept
    int kept_count = 0;
    for (int i = 0; i < all.count; i++) {
        entry *x = &all.items[i];
        const bool drop = has_name(&names, x->was) || (x->now && strcmp(x->was, x->now) == 0) || all.count - i > 100;
        if (drop) entry_free(x);
        else all.items[kept_count++] = *x;
    }
    all.count = kept_count;
    out text = {0};
    puts_(&text, "{\"renamed\": [");
    for (int i = 0; i < all.count; i++) {
        const entry *x = &all.items[i];
        puts_(&text, i ? ",\n  {\"was\": " : "\n  {\"was\": ");
        put_json_text(&text, x->was);
        if (x->now) {
            puts_(&text, ", \"now\": ");
            put_json_text(&text, x->now);
            if (x->file) {
                puts_(&text, ", \"file\": ");
                put_json_text(&text, x->file);
                putf(&text, ", \"line\": %d", x->line);
            }
        } else {
            puts_(&text, ", \"removed\": true");
        }
        puts_(&text, "}");
    }
    puts_(&text, all.count ? "\n]}\n" : "]}\n");
    bool ok = true;
    if (!old || size != text.n || memcmp(old, text.s, size) != 0) {
        ok = sys_write_file(record, text.s, text.n);
        if (!ok) fprintf(stderr, "tide: can't write %s\n", record);
    }
    for (int i = 0; i < all.count; i++) entry_free(&all.items[i]);
    free(all.items);
    for (int i = 0; i < d.found.count; i++) entry_free(&d.found.items[i]);
    free(d.found.items);
    free(text.s);
    free(old);
    free(above);
    free(project);
    unity_cs_free(&was_names);
    unity_cs_free(&names);
    json_release();
    return ok;
}
