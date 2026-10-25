// The game's C# for Unity's Tide package (unity_code.h).

#include "unity_code.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
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
// Names

static const char *const csharp_keywords[] = {
    "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue",
    "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally",
    "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock", "long",
    "namespace", "new", "null", "object", "operator", "out", "override", "params", "private", "protected", "public",
    "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string", "struct",
    "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using",
    "virtual", "void", "volatile", "while", "var", NULL};

// A name C# takes: what an identifier can't hold as _, `?` as Optional, a keyword with @. malloc'd.
static char *identifier(const char *name, const size_t n)
{
    out o = {0};
    puts_(&o, "");
    for (size_t i = 0; i < n; i++) {
        const char c = name[i];
        if (c == '?') puts_(&o, "Optional");
        else putn(&o, (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || (unsigned char)c >= 0x80 ? &c : "_", 1);
    }
    if (!o.n || (o.s[0] >= '0' && o.s[0] <= '9')) {
        out p = {0};
        puts_(&p, "_");
        puts_(&p, o.s ? o.s : "");
        free(o.s);
        o = p;
    }
    for (int i = 0; csharp_keywords[i]; i++) {
        if (strcmp(o.s, csharp_keywords[i]) == 0) {
            out p = {0};
            puts_(&p, "@");
            puts_(&p, o.s);
            free(o.s);
            return p.s;
        }
    }
    return o.s;
}

// A dotted Tide namespace ("Combat.Melee") as C#'s, each part an identifier; NULL for none. malloc'd.
static char *namespace_of(const char *name, const size_t n)
{
    if (!n) return NULL;
    out o = {0};
    puts_(&o, "");
    for (size_t at = 0; at < n;) {
        size_t end = at;
        while (end < n && name[end] != '.') end++;
        char *part = identifier(name + at, end - at);
        if (o.n) puts_(&o, ".");
        puts_(&o, part);
        free(part);
        at = end + 1;
    }
    return o.s;
}

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
    char *bare, *ns, *cs; // Its name without " local", its C# namespace (NULL for none) and its C# name
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
    char *full; // global::Tide.Game...
} enumeration;

struct ctype;
struct shape;

typedef struct game {
    const unity_mappings *mappings; // What the game's packages say of their components in Unity
    type *types;
    int type_count;
    enumeration *enums;
    int enum_count;
    const json *outline; // print_outline's declarations, or NULL
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

static int hex(const char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static bool starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Whether a type is one the package makes a TideComponent of: a match component an entity can be given in Unity, not
// a scene's tag, tide's own (UnityEngine.*) nor Transform, which is Unity's.
static bool authored(const type *t)
{
    return strcmp(t->kind, "component") == 0 && !t->scene && !starts(t->name, "UnityEngine.") && strcmp(t->name, "Transform") != 0;
}

static bool read_game(const json *schema, game *g)
{
    const json *types = json_get(schema, "types"), *enums = json_get(schema, "enums");
    if (!types || types->kind != JSON_ARRAY) return false;
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
        // Its names: a local part's ("Health local") is its component's, with Local after
        const bool local = strcmp(t->kind, "local") == 0;
        size_t length = strlen(t->name);
        if (local && length > 6 && strcmp(t->name + length - 6, " local") == 0) length -= 6;
        t->bare = malloc(length + 1);
        if (!t->bare) abort();
        memcpy(t->bare, t->name, length);
        t->bare[length] = '\0';
        const char *dot = strrchr(t->bare, '.');
        t->ns = dot ? namespace_of(t->bare, (size_t)(dot - t->bare)) : NULL;
        char *cs = identifier(dot ? dot + 1 : t->bare, strlen(dot ? dot + 1 : t->bare));
        if (local) {
            out o = {0};
            puts_(&o, cs);
            puts_(&o, "Local");
            free(cs);
            cs = o.s;
        }
        // Two of one C# name in one namespace: the second gets a _
        for (bool clash = true; clash;) {
            clash = false;
            for (int k = 0; k < i && !clash; k++) {
                const type *other = &g->types[k];
                clash = strcmp(other->cs, cs) == 0 && ((!other->ns && !t->ns) || (other->ns && t->ns && strcmp(other->ns, t->ns) == 0));
            }
            if (clash) {
                out o = {0};
                puts_(&o, cs);
                puts_(&o, "_");
                free(cs);
                cs = o.s;
            }
        }
        t->cs = cs;
        const json *local_of = json_get(j, "local");
        t->local = local_of && local_of->kind == JSON_NUMBER && local_of->number >= 0 ? (int)local_of->number : -1;
        t->component = authored(t);
        t->base = t->component ? "TideComponent"
                : strcmp(t->kind, "singleton") == 0 && !starts(t->name, "tide_") ? "TideSingleton"
                : strcmp(t->kind, "event") == 0 ? "TideEvent"
                : strcmp(t->kind, "command") == 0 ? "TideCommand"
                : NULL;
        t->live = strcmp(t->kind, "struct") == 0 && !starts(t->name, "tide_");
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
        const char *dot = strrchr(e->name, '.');
        char *ns = dot ? namespace_of(e->name, (size_t)(dot - e->name)) : NULL;
        char *cs = identifier(dot ? dot + 1 : e->name, strlen(dot ? dot + 1 : e->name));
        out o = {0};
        putf(&o, "global::Tide.Game.%s%s%s", ns ? ns : "", ns ? "." : "", cs);
        e->full = o.s;
        free(ns);
        free(cs);
    }
    return true;
}

static void free_game(game *g)
{
    for (int i = 0; i < g->type_count; i++) {
        free(g->types[i].defaults);
        free(g->types[i].fields);
        free(g->types[i].bare);
        free(g->types[i].ns);
        free(g->types[i].cs);
    }
    free(g->types);
    for (int i = 0; i < g->enum_count; i++) free(g->enums[i].full);
    free(g->enums);
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
        if (name == 4 && strncmp(s, "List", 4) == 0 && count == 1) {
            t->kind = CK_LIST;
            t->size = 4;
        } else if (name == 3 && strncmp(s, "Set", 3) == 0 && count == 1) {
            t->kind = CK_SET;
            t->size = 24;
        } else if (name == 3 && strncmp(s, "Map", 3) == 0 && count == 2) {
            t->kind = CK_MAP;
            t->size = 24;
            t->value = args[1];
        } else if (name == 5 && (strncmp(s, "Grid2", 5) == 0 || strncmp(s, "Grid3", 5) == 0) && count == 1) {
            t->kind = CK_GRID;
            t->size = 4;
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
        t->size = 4;
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

// Names taken in a type: each field's identifier is the first of its name, with _ after, that isn't.
typedef struct taken {
    char **names;
    int count;
} taken;

static bool take(taken *t, const char *name)
{
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->names[i], name) == 0) return false;
    }
    t->names = realloc(t->names, sizeof *t->names * (size_t)(t->count + 1));
    if (!t->names) abort();
    t->names[t->count++] = copy(name);
    return true;
}

static void free_taken(taken *t)
{
    for (int i = 0; i < t->count; i++) free(t->names[i]);
    free(t->names);
    t->names = NULL;
    t->count = 0;
}

// A class's or struct's members, which its fields' names keep clear of, as its Value's do, whose ReadLive, From and
// Pack read and write it by them
static void reserve(taken *t, const type *ty, const bool nested)
{
    take(t, ty->cs);
    if (!nested || (!ty->base && !ty->live)) return;
    static const char *const members[] = {"Value", "TideName", "ReadLive", "Pack", "From", "Tick", "Verified", "Target", "TargetEntity"};
    for (size_t i = 0; i < sizeof members / sizeof members[0]; i++) take(t, members[i]);
}

// The identifier of a field among `t`'s: malloc'd.
static char *field_name(taken *t, const char *name)
{
    char *id = identifier(name, strlen(name));
    while (!take(t, id)) {
        out o = {0};
        puts_(&o, id);
        puts_(&o, "_");
        free(id);
        id = o.s;
    }
    return id;
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
    taken names = {0};
    reserve(&names, t, nested);
    for (int i = 0; i < t->field_count; i++) {
        const field *f = &t->fields[i];
        char *name = field_name(&names, f->name);
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
    free_taken(&names);
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
        taken names = {0};
        reserve(&names, s, true);
        for (int i = 0; i < s->field_count; i++) {
            const field *f = &s->fields[i];
            char *id = field_name(&names, f->name);
            out value = {0};
            puts_(&value, "");
            if (put_default(&value, g, field_ctype(g, f), d, at + f->offset)) putf(&fields, "%s%s = %s", fields.n ? ", " : "", id, value.s);
            free(value.s);
            free(id);
        }
        free_taken(&names);
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

// A text field's default, as its declaration writes it between its quotes (print_outline's "text"), or NULL.
static const char *text_default(const game *g, const type *t, const field *f)
{
    const json *declarations = json_get(g->outline, "declarations");
    for (int i = 0; declarations && declarations->kind == JSON_ARRAY && i < declarations->count; i++) {
        const json *d = declarations->items[i];
        if (strcmp(str(d, "name"), t->bare) != 0) continue;
        const json *fields = json_get(d, "fields");
        for (int k = 0; fields && fields->kind == JSON_ARRAY && k < fields->count; k++) {
            if (strcmp(str(fields->items[k], "name"), f->name) == 0) return json_str(json_get(fields->items[k], "text"));
        }
    }
    return NULL;
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
        taken names = {0};
        reserve(&names, st, true);
        for (int i = 0; i < st->field_count; i++) {
            const field *f = &st->fields[i];
            char *id = field_name(&names, f->name);
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
        free_taken(&names);
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
static void put_live_field(out *o, out *live, game *g, const ctype *ct, const char *id, const char *from, const char *self,
                           const char *attribute, const char *indent)
{
    char *cs = live_cs(g, ct, false);
    if (!cs) return;
    putf(o, "%s%spublic %s %s;\n", indent, attribute, cs, id);
    putf(live, "%s    %s%s = ", indent, self, id);
    put_conv(live, g, ct, from, "packet", "runner", false, 0);
    puts_(live, ";\n");
    free(cs);
}

// The fields of a type's local part, after its own, and their lines of ReadLive: Unity doesn't keep them (they're
// this machine's, never authored), and the inspector shows them in Play.
static void put_local_fields(out *o, out *live, game *g, const type *t, taken *names)
{
    if (t->local < 0) return;
    const type *part = &g->types[t->local];
    taken own = {0};
    reserve(&own, part, false); // As write_value names its struct's
    out lines = {0};
    puts_(&lines, "");
    for (int i = 0; i < part->field_count; i++) {
        const field *f = &part->fields[i];
        char *id = field_name(names, f->name);
        char *value = field_name(&own, f->name);
        out from = {0};
        putf(&from, "l.%s", value);
        put_live_field(o, &lines, g, field_ctype(g, f), id, from.s, "", "[global::System.NonSerialized] ", "        ");
        free(from.s);
        free(value);
        free(id);
    }
    free_taken(&own);
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
    putf(o, "    // Tide's struct %s, as Unity code has it\n    [global::System.Serializable]\n    public unsafe partial struct %s\n    {\n",
         t->bare, t->cs);
    out from = {0}, pack = {0};
    puts_(&from, "");
    puts_(&pack, "");
    taken names = {0};
    reserve(&names, t, true);
    for (int i = 0; i < t->field_count; i++) {
        const field *f = &t->fields[i];
        char *id = field_name(&names, f->name);
        const ctype *ct = field_ctype(g, f);
        char *cs = live_cs(g, ct, false);
        if (cs) {
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
    free_taken(&names);
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
    } else if (command) {
        putf(o, "    // Tide's command %s: TideRunner.Send sends it, as local code's Send does (TideCommand)\n", t->bare);
    } else if (event) {
        putf(o, "    // Tide's event %s, as this machine's local handlers hear it: TideRunner.On, OnVerified (TideEvent)\n", t->bare);
    } else {
        putf(o, "    // Tide's singleton %s, as Tide's views see it this frame: TideRunner.Get (TideSingleton)\n", t->bare);
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
    taken names = {0};
    reserve(&names, t, true);
    int mapped = 0;
    for (int i = 0; i < t->field_count; i++) {
        const field *f = &t->fields[i];
        char *id = field_name(&names, f->name);
        const char *key = id[0] == '@' ? id + 1 : id; // What Unity's YAML calls it
        const ctype *ct = field_ctype(g, f);
        out from = {0};
        putf(&from, "v.%s", id);
        if (command) {
            // Its declared default, and its part of Pack
            char *cs = packs(g, ct) ? live_cs(g, ct, false) : NULL;
            if (cs) {
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
            put_live_field(o, &live, g, ct, id, from.s, "", "", "        ");
        } else if (authorable(g, ct)) {
            char *cs = live_cs(g, ct, false);
            const unity_scale by = unity_field_scale(rules, f->name);
            if (unity_field_hidden(rules, f->name)) puts_(o, "        [global::UnityEngine.HideInInspector] // Its package's own\n");
            if (by != UNITY_SCALE_NONE) puts_(o, "        [global::UnityEngine.Tooltip(\"By the GameObject's scale, as a collider's size is\")]\n");
            putf(o, "        public %s %s", cs, id);
            out initial = {0};
            puts_(&initial, "");
            if (put_initial(&initial, g, t, f, ct)) putf(o, " = %s", initial.s);
            free(initial.s);
            puts_(o, ";\n");
            putf(&live, "            %s = ", id);
            put_conv(&live, g, ct, from.s, "packet", "runner", false, 0);
            puts_(&live, ";\n");
            // The match has it by the scale: shown as authored, divided by it
            if (by != UNITY_SCALE_NONE) {
                char by_name[32];
                snprintf(by_name, sizeof by_name, "%s", unity_scale_name(by));
                by_name[0] = (char)(by_name[0] - 'a' + 'A');
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
            put_live_field(o, &live, g, ct, id, from.s, "", "[global::System.NonSerialized] ", "        ");
        } else {
            // A Tide value as text: what Unity can't hold as itself
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
    if (!command && !event) put_local_fields(o, &locals, g, t, &names);
    free_taken(&names);
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
        const char *dot = strrchr(e->name, '.');
        char *ns = dot ? namespace_of(e->name, (size_t)(dot - e->name)) : NULL;
        char *cs = identifier(dot ? dot + 1 : e->name, strlen(dot ? dot + 1 : e->name));
        open_namespace(&o, ns);
        putf(&o, "    public enum %s : int\n    {\n", cs);
        taken used = {0};
        for (int k = 0; e->members && e->members->kind == JSON_ARRAY && k < e->members->count; k++) {
            const char *member = json_str(e->members->items[k]);
            char *id = identifier(member ? member : "_", member ? strlen(member) : 1);
            const int value = e->values && e->values->kind == JSON_ARRAY && k < e->values->count ? json_int(e->values->items[k], k) : k;
            if (take(&used, id)) putf(&o, "        %s = %d,\n", id, value);
            free(id);
        }
        free_taken(&used);
        puts_(&o, "    }\n}\n");
        free(ns);
        free(cs);
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

bool unity_code_write(const char *dir, const char *schema_text, const char *outline_text, const unity_package *packages,
                      const int package_count)
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
    g.mappings = &mappings;
    if (!read_game(schema, &g)) {
        json_release();
        unity_mappings_free(&mappings);
        fprintf(stderr, "tide: the game's schema has no types\n");
        return false;
    }
    bool failed = false;
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
    sys_list(components, sweep_entry, (void *)&s);
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
    json_release();
    unity_mappings_free(&mappings);
    return !failed;
}
