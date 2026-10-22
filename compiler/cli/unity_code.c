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
    bool component;       // A TideComponent: its value is the class's nested Value
} type;

typedef struct enumeration {
    const char *name;
    const json *members, *values;
    char *full; // global::Tide.Game...
} enumeration;

typedef struct game {
    type *types;
    int type_count;
    enumeration *enums;
    int enum_count;
    const json *outline; // print_outline's declarations, or NULL
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
        t->component = authored(t);
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

// A type's C# name where a field holds it: a TideComponent's value is its Value. malloc'd.
static char *full_name(const type *t)
{
    out o = {0};
    putf(&o, "global::Tide.Game.%s%s%s%s", t->ns ? t->ns : "", t->ns ? "." : "", t->cs, t->component ? ".Value" : "");
    return o.s;
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

// The C# type a field of a value reads as, or NULL for its bytes alone. malloc'd.
static char *field_type(const game *g, const field *f)
{
    if (strcmp(f->kind, "text") == 0) return copy("global::Tide.TideText");
    if (strcmp(f->kind, "list") == 0) return copy("global::Tide.TideList");
    if (strcmp(f->kind, "entity") == 0) return copy("global::Tide.TideEntityId");
    if (strcmp(f->kind, "map") == 0 || strcmp(f->kind, "grid") == 0) return NULL;
    if (strcmp(f->kind, "struct") == 0 && f->ref >= 0 && f->ref < g->type_count) return full_name(&g->types[f->ref]);
    if (strcmp(f->kind, "enum") == 0 && f->ref >= 0 && f->ref < g->enum_count && f->size == 4) return copy(g->enums[f->ref].full);
    static const struct { const char *tide, *cs; uint32_t size; } plain[] = {
        {"bool", "bool", 1}, {"byte", "byte", 1}, {"int", "int", 4}, {"char", "int", 4}, {"long", "long", 8},
        {"float", "float", 4}, {"double", "double", 8}, {"PlayerID", "uint", 4},
        {"float2", "global::UnityEngine.Vector2", 8}, {"float3", "global::UnityEngine.Vector3", 12},
        {"float4", "global::UnityEngine.Vector4", 16}, {"int2", "global::UnityEngine.Vector2Int", 8},
        {"int3", "global::UnityEngine.Vector3Int", 12}, {"quaternion", "global::UnityEngine.Quaternion", 16},
        {"Color", "global::UnityEngine.Color", 16}, {"Rect", "global::UnityEngine.Rect", 16},
        {"float4x4", "global::UnityEngine.Matrix4x4", 64},
    };
    for (size_t i = 0; i < sizeof plain / sizeof plain[0]; i++) {
        if (strcmp(f->type, plain[i].tide) == 0) return f->size == plain[i].size ? copy(plain[i].cs) : NULL;
    }
    return NULL;
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

// A component's members, which its fields' names keep clear of, as its Value's do, whose ReadLive reads it by them
static void reserve(taken *t, const type *ty, const bool component)
{
    take(t, ty->cs);
    if (component) {
        take(t, "Value");
        take(t, "TideName");
        take(t, "ReadLive");
    }
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

// A struct of the type, laid out as the game's C does: `nested` as a TideComponent's Value.
static void write_value(out *o, const game *g, const type *t, const char *indent, const bool nested)
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
        char *cs = field_type(g, f);
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

// How a component's field is authored: its C# type, how the converter reads its YAML ("form"), and how its live
// value is written. A field Unity can't hold as itself is a Tide value as text ("literal").
typedef struct plan {
    char form[16];
    char cs[64];
    char element[16]; // A list's elements' form
    char element_type[16]; // ...and Tide type, whose size Unity's hex of a list of numbers goes by
    const char *enum_name;
    const char *enum_full;
} plan;

static const struct { const char *tide, *cs; uint32_t size; const char *form; } authored_types[] = {
    {"float", "float", 4, "number"}, {"double", "double", 8, "number"}, {"int", "int", 4, "number"},
    {"long", "long", 8, "number"}, {"byte", "byte", 1, "number"}, {"char", "int", 4, "number"},
    {"PlayerID", "uint", 4, "number"}, {"bool", "bool", 1, "bool"},
    {"float2", "global::UnityEngine.Vector2", 8, "float2"}, {"float3", "global::UnityEngine.Vector3", 12, "float3"},
    {"float4", "global::UnityEngine.Vector4", 16, "float4"}, {"int2", "global::UnityEngine.Vector2Int", 8, "int2"},
    {"int3", "global::UnityEngine.Vector3Int", 12, "int3"}, {"quaternion", "global::UnityEngine.Quaternion", 16, "quaternion"},
    {"Color", "global::UnityEngine.Color", 16, "color"},
};

static int authored_type(const char *tide)
{
    for (int i = 0; i < (int)(sizeof authored_types / sizeof authored_types[0]); i++) {
        if (strcmp(tide, authored_types[i].tide) == 0) return i;
    }
    return -1;
}

static plan plan_of(const game *g, const field *f)
{
    plan p = {0};
    if (strcmp(f->kind, "text") == 0) {
        snprintf(p.form, sizeof p.form, "string");
        snprintf(p.cs, sizeof p.cs, "string");
    } else if (strcmp(f->kind, "entity") == 0) {
        snprintf(p.form, sizeof p.form, "entity");
        snprintf(p.cs, sizeof p.cs, "global::UnityEngine.GameObject");
    } else if (strcmp(f->kind, "enum") == 0 && f->size == 4 && f->ref >= 0 && f->ref < g->enum_count) {
        snprintf(p.form, sizeof p.form, "enum");
        p.enum_name = g->enums[f->ref].name;
        p.enum_full = g->enums[f->ref].full;
    } else if (strcmp(f->kind, "list") == 0) {
        const size_t n = strlen(f->type);
        char inner[32] = "";
        if (n > 6 && n - 6 < sizeof inner && starts(f->type, "List<") && f->type[n - 1] == '>') {
            memcpy(inner, f->type + 5, n - 6);
            inner[n - 6] = '\0';
        }
        const int at = authored_type(inner);
        if (at >= 0 && authored_types[at].size == f->element && strcmp(inner, "char") != 0) {
            snprintf(p.form, sizeof p.form, "list");
            snprintf(p.element, sizeof p.element, "%s", authored_types[at].form);
            snprintf(p.element_type, sizeof p.element_type, "%s", inner);
            snprintf(p.cs, sizeof p.cs, "%s", authored_types[at].cs);
        }
    } else if (strcmp(f->kind, "struct") != 0) {
        const int at = authored_type(f->type);
        if (at >= 0 && authored_types[at].size == f->size) {
            snprintf(p.form, sizeof p.form, "%s", authored_types[at].form);
            snprintf(p.cs, sizeof p.cs, "%s", authored_types[at].cs);
        }
    }
    if (!p.form[0]) snprintf(p.form, sizeof p.form, "literal");
    return p;
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

// A field's declared default as C#, from its component's default bytes: false where they don't say.
static bool put_default(out *o, const type *t, const field *f, const plan *p)
{
    if (!t->defaults || f->offset + f->size > t->size) return false;
    const uint8_t *d = t->defaults;
    const uint32_t at = f->offset;
    static const char *const vectors[][2] = {
        {"float2", "Vector2"}, {"float3", "Vector3"}, {"float4", "Vector4"}, {"quaternion", "Quaternion"}, {"color", "Color"},
    };
    for (size_t v = 0; v < sizeof vectors / sizeof vectors[0]; v++) {
        if (strcmp(p->form, vectors[v][0]) != 0) continue;
        const int count = v == 0 ? 2 : v == 1 ? 3 : 4;
        putf(o, "new global::UnityEngine.%s(", vectors[v][1]);
        for (int i = 0; i < count; i++) {
            if (i) puts_(o, ", ");
            put_float(o, float_at(d, at + (uint32_t)i * 4));
        }
        puts_(o, ")");
        return true;
    }
    if (strcmp(p->form, "int2") == 0 || strcmp(p->form, "int3") == 0) {
        const bool three = p->form[3] == '3';
        putf(o, "new global::UnityEngine.Vector%sInt(%d, %d", three ? "3" : "2", (int)int_at(d, at), (int)int_at(d, at + 4));
        if (three) putf(o, ", %d", (int)int_at(d, at + 8));
        puts_(o, ")");
        return true;
    }
    if (strcmp(p->form, "bool") == 0) {
        puts_(o, d[at] ? "true" : "false");
        return true;
    }
    if (strcmp(p->form, "enum") == 0) {
        putf(o, "(%s)%d", p->enum_full, (int)int_at(d, at));
        return true;
    }
    if (strcmp(p->form, "number") != 0) return false;
    if (strcmp(f->type, "float") == 0) put_float(o, float_at(d, at));
    else if (strcmp(f->type, "int") == 0 || strcmp(f->type, "char") == 0) putf(o, "%d", (int)int_at(d, at));
    else if (strcmp(f->type, "byte") == 0) putf(o, "%u", (unsigned)d[at]);
    else if (strcmp(f->type, "PlayerID") == 0) putf(o, "%uu", (unsigned)(uint32_t)int_at(d, at));
    else if (strcmp(f->type, "long") == 0) {
        int64_t v;
        memcpy(&v, d + at, sizeof v);
        putf(o, "%lldL", (long long)v);
    } else if (strcmp(f->type, "double") == 0) {
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
        if (strcmp(str(d, "name"), t->bare) != 0 || strcmp(str(d, "kind"), "component") != 0) continue;
        const json *fields = json_get(d, "fields");
        for (int k = 0; fields && fields->kind == JSON_ARRAY && k < fields->count; k++) {
            if (strcmp(str(fields->items[k], "name"), f->name) == 0) return json_str(json_get(fields->items[k], "text"));
        }
    }
    return NULL;
}

// A component's C# (its TideComponent, with its Value), and its entry of components.json into `map`.
static char *write_component(const game *g, const type *t, out *map)
{
    out o = {0};
    puts_(&o, header);
    open_namespace(&o, t->ns);
    putf(&o, "    // Tide's component %s: its fields as authored outside Play, its entity's values in Play (TideComponent)\n", t->bare);
    puts_(&o, "    [global::UnityEngine.AddComponentMenu(\"Tide/");
    for (const char *c = t->bare; *c; c++) {
        if (*c == '.') puts_(&o, "/");
        else if (*c == '"' || *c == '\\') {
            putn(&o, "\\", 1);
            putn(&o, c, 1);
        } else putn(&o, c, 1);
    }
    puts_(&o, "\")]\n    [global::UnityEngine.DisallowMultipleComponent]\n");
    putf(&o, "    public sealed partial class %s : global::Tide.TideComponent\n    {\n", t->cs);
    char guid[33];
    unity_code_guid(t->bare, guid);
    putf(map, "{\"name\": ");
    jbuf name = {0};
    jb_string(&name, t->bare);
    putn(map, name.data, name.len);
    jb_free(&name);
    putf(map, ", \"guid\": \"%s\", \"fields\": [", guid);
    out live = {0};
    puts_(&live, "");
    taken names = {0};
    reserve(&names, t, true);
    int mapped = 0;
    for (int i = 0; i < t->field_count; i++) {
        const field *f = &t->fields[i];
        char *id = field_name(&names, f->name);
        const char *key = id[0] == '@' ? id + 1 : id; // What Unity's YAML calls it
        const plan p = plan_of(g, f);
        const bool literal = strcmp(p.form, "literal") == 0;
        if (literal) {
            puts_(&o, "        [global::UnityEngine.Tooltip(\"");
            put_escaped(&o, f->type);
            puts_(&o, ", as Tide code writes a value of it: empty for its default\")]\n");
            putf(&o, "        public string %s = \"\";\n", id);
        } else if (strcmp(p.form, "string") == 0) {
            const char *text = text_default(g, t, f);
            putf(&o, "        public string %s = \"%s\";\n", id, text ? text : "");
            putf(&live, "            %s = v.%s.Read(packet);\n", id, id);
        } else if (strcmp(p.form, "entity") == 0) {
            putf(&o, "        public global::UnityEngine.GameObject %s;\n", id);
            putf(&live, "            %s = runner.GameObjectOf(v.%s);\n", id, id);
        } else if (strcmp(p.form, "list") == 0) {
            putf(&o, "        public global::System.Collections.Generic.List<%s> %s = new global::System.Collections.Generic.List<%s>();\n",
                 p.cs, id, p.cs);
            putf(&live, "            %s.Clear();\n            %s.AddRange(v.%s.ToArray<%s>(packet));\n", id, id, id, p.cs);
        } else {
            putf(&o, "        public %s %s", strcmp(p.form, "enum") == 0 ? p.enum_full : p.cs, id);
            out initial = {0};
            puts_(&initial, "");
            if (put_default(&initial, t, f, &p)) putf(&o, " = %s", initial.s);
            free(initial.s);
            puts_(&o, ";\n");
            putf(&live, "            %s = v.%s;\n", id, id);
        }
        if (mapped++) puts_(map, ", ");
        jbuf j = {0};
        jb_string(&j, f->name);
        putf(map, "{\"name\": %.*s, \"key\": \"%s\", \"form\": \"%s\"", (int)j.len, j.data, key, p.form);
        jb_free(&j);
        if (p.enum_name) {
            jbuf e = {0};
            jb_string(&e, p.enum_name);
            putf(map, ", \"enum\": %.*s", (int)e.len, e.data);
            jb_free(&e);
        }
        if (p.element[0]) putf(map, ", \"element\": \"%s\", \"elementType\": \"%s\"", p.element, p.element_type);
        puts_(map, "}");
        free(id);
    }
    free_taken(&names);
    puts_(map, "]}");
    puts_(&o, "\n        public override string TideName => \"");
    put_escaped(&o, t->bare);
    puts_(&o, "\";\n\n        protected override unsafe void ReadLive(byte* values, global::Tide.TidePacket packet, global::Tide.TideRunner runner)\n        {\n");
    if (live.n) {
        puts_(&o, "            var v = *(Value*)values;\n");
        puts_(&o, live.s);
    }
    free(live.s);
    puts_(&o, "        }\n\n");
    write_value(&o, g, t, "        ", true);
    puts_(&o, "    }\n}\n");
    return o.s;
}

static char *write_types(const game *g, const json *schema)
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
    puts_(&o, "    }\n\n    // The game's components as TideComponents, for the runner to write its entities' values into\n"
              "    static class TideGameComponents\n    {\n"
              "        [global::UnityEngine.RuntimeInitializeOnLoadMethod(global::UnityEngine.RuntimeInitializeLoadType.SubsystemRegistration)]\n"
              "        static void Register()\n        {\n");
    for (int i = 0; i < g->type_count; i++) {
        const type *t = &g->types[i];
        if (!t->component) continue;
        puts_(&o, "            global::Tide.TideComponents.Register(TideGameSchema.Hash, \"");
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
        write_value(&o, g, t, "    ", false);
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

bool unity_code_write(const char *dir, const char *schema_text, const char *outline_text)
{
    json *schema = json_parse(schema_text, strlen(schema_text));
    if (!schema) {
        fprintf(stderr, "tide: the game's schema isn't JSON\n");
        return false;
    }
    json *outline = outline_text ? json_parse(outline_text, strlen(outline_text)) : NULL;
    game g = {0};
    g.outline = outline;
    if (!read_game(schema, &g)) {
        json_release();
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
    puts_(&map, mapped ? "\n], \"enums\": [" : "], \"enums\": [");
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
    free_game(&g);
    json_release();
    return !failed;
}
