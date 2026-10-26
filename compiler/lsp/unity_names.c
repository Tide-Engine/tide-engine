// The names a game's types have in C# (unity_names.h).

#include "unity_names.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const csharp_keywords[] = {
    "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue",
    "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally",
    "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock", "long",
    "namespace", "new", "null", "object", "operator", "out", "override", "params", "private", "protected", "public",
    "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string", "struct",
    "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using",
    "virtual", "void", "volatile", "while", "var", NULL};

// What a class's or a C# struct's fields keep clear of: the members the package's classes and generated structs have
static const char *const members[] = {"Value", "TideName", "ReadLive", "Pack", "From", "Tick", "Verified", "Target", "TargetEntity"};

static char *grow(char *s, size_t *n, size_t *cap, const char *add, const size_t m)
{
    if (*n + m + 1 > *cap) {
        *cap = (*n + m + 1) * 2;
        s = realloc(s, *cap);
        if (!s) abort();
    }
    memcpy(s + *n, add, m);
    *n += m;
    s[*n] = '\0';
    return s;
}

static char *copy(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (!c) abort();
    memcpy(c, s, n);
    return c;
}

static char *with_after(const char *s, const char *after)
{
    const size_t a = strlen(s), b = strlen(after);
    char *c = malloc(a + b + 1);
    if (!c) abort();
    memcpy(c, s, a);
    memcpy(c + a, after, b + 1);
    return c;
}

char *unity_cs_identifier(const char *name, const size_t n)
{
    size_t len = 0, cap = 0;
    char *o = grow(NULL, &len, &cap, "", 0);
    for (size_t i = 0; i < n; i++) {
        const char c = name[i];
        if (c == '?') o = grow(o, &len, &cap, "Optional", 8);
        else {
            const bool kept = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
                           || (unsigned char)c >= 0x80;
            o = grow(o, &len, &cap, kept ? &c : "_", 1);
        }
    }
    if (!len || (o[0] >= '0' && o[0] <= '9')) {
        char *p = with_after("_", o);
        free(o);
        o = p;
    }
    for (int i = 0; csharp_keywords[i]; i++) {
        if (strcmp(o, csharp_keywords[i]) == 0) {
            char *p = with_after("@", o);
            free(o);
            return p;
        }
    }
    return o;
}

char *unity_cs_namespace(const char *name, const size_t n)
{
    if (!n) return NULL;
    size_t len = 0, cap = 0;
    char *o = grow(NULL, &len, &cap, "", 0);
    for (size_t at = 0; at < n;) {
        size_t end = at;
        while (end < n && name[end] != '.') end++;
        char *part = unity_cs_identifier(name + at, end - at);
        if (len) o = grow(o, &len, &cap, ".", 1);
        o = grow(o, &len, &cap, part, strlen(part));
        free(part);
        at = end + 1;
    }
    return o;
}

char *unity_cs_full(const char *ns, const char *cs)
{
    if (!ns) return copy(cs);
    char *dotted = with_after(ns, ".");
    char *full = with_after(dotted, cs);
    free(dotted);
    return full;
}

static const char *text(const json *object, const char *key)
{
    const char *s = json_str(json_get(object, key));
    return s ? s : "";
}

static bool starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Names taken in a type: each field's identifier is the first of its name, with _ after, that isn't
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
    *t = (taken){0};
}

static char *field_id(taken *t, const char *name)
{
    char *id = unity_cs_identifier(name, strlen(name));
    while (!take(t, id)) {
        char *more = with_after(id, "_");
        free(id);
        id = more;
    }
    return id;
}

static void reserve(taken *t, const unity_cs_type *ty)
{
    take(t, ty->cs);
    if (!ty->members) return;
    for (size_t i = 0; i < sizeof members / sizeof members[0]; i++) take(t, members[i]);
}

static void free_names(unity_cs_names *n)
{
    for (int i = 0; i < n->type_count; i++) {
        unity_cs_type *t = &n->types[i];
        free(t->bare);
        free(t->ns);
        free(t->cs);
        for (int k = 0; t->ids && k < t->field_count; k++) free(t->ids[k]);
        free(t->ids);
        const int locals = t->local >= 0 ? n->types[t->local].field_count : 0;
        for (int k = 0; t->local_ids && k < locals; k++) free(t->local_ids[k]);
        free(t->local_ids);
        t->bare = t->ns = t->cs = NULL;
        t->ids = t->local_ids = NULL;
    }
    for (int i = 0; i < n->enum_count; i++) {
        unity_cs_enum *e = &n->enums[i];
        free(e->ns);
        free(e->cs);
        for (int k = 0; e->ids && k < e->member_count; k++) free(e->ids[k]);
        free(e->ids);
        e->ns = e->cs = NULL;
        e->ids = NULL;
    }
}

void unity_cs_assign(unity_cs_names *n)
{
    free_names(n);
    for (int i = 0; i < n->type_count; i++) {
        unity_cs_type *t = &n->types[i];
        // Its names: a local part's ("Health local") is its component's, with Local after
        const bool local = strcmp(t->kind, "local") == 0;
        size_t length = strlen(t->name);
        if (local && length > 6 && strcmp(t->name + length - 6, " local") == 0) length -= 6;
        t->bare = malloc(length + 1);
        if (!t->bare) abort();
        memcpy(t->bare, t->name, length);
        t->bare[length] = '\0';
        const char *dot = strrchr(t->bare, '.');
        t->ns = dot ? unity_cs_namespace(t->bare, (size_t)(dot - t->bare)) : NULL;
        const char *last = dot ? dot + 1 : t->bare;
        char *cs = unity_cs_identifier(last, strlen(last));
        if (local) {
            char *with = with_after(cs, "Local");
            free(cs);
            cs = with;
        }
        // Two of one C# name in one namespace: the second gets a _
        for (bool clash = true; clash;) {
            clash = false;
            for (int k = 0; k < i && !clash; k++) {
                const unity_cs_type *other = &n->types[k];
                clash = strcmp(other->cs, cs) == 0 && ((!other->ns && !t->ns) || (other->ns && t->ns && strcmp(other->ns, t->ns) == 0));
            }
            if (clash) {
                char *more = with_after(cs, "_");
                free(cs);
                cs = more;
            }
        }
        t->cs = cs;
        // Which class of the package it is: an authored match component (not a scene's tag, tide's own UnityEngine.*,
        // nor Transform, which is Unity's), a singleton not tide's own, an event or a command
        t->component = strcmp(t->kind, "component") == 0 && !t->scene && !starts(t->name, "UnityEngine.")
                    && strcmp(t->name, "Transform") != 0;
        t->base = t->component ? "TideComponent"
                : strcmp(t->kind, "singleton") == 0 && !starts(t->name, "tide_") ? "TideSingleton"
                : strcmp(t->kind, "event") == 0 ? "TideEvent"
                : strcmp(t->kind, "command") == 0 ? "TideCommand"
                : NULL;
        t->live = strcmp(t->kind, "struct") == 0 && !starts(t->name, "tide_");
        t->members = t->base || t->live;
    }
    for (int i = 0; i < n->type_count; i++) {
        unity_cs_type *t = &n->types[i];
        taken names = {0};
        reserve(&names, t);
        t->ids = calloc((size_t)t->field_count + 1, sizeof *t->ids);
        if (!t->ids) abort();
        for (int k = 0; k < t->field_count; k++) t->ids[k] = field_id(&names, t->fields[k]);
        // A class's local fields come after its own, named among them
        if (t->local >= 0 && t->base && strcmp(t->base, "TideEvent") != 0 && strcmp(t->base, "TideCommand") != 0) {
            const unity_cs_type *part = &n->types[t->local];
            t->local_ids = calloc((size_t)part->field_count + 1, sizeof *t->local_ids);
            if (!t->local_ids) abort();
            for (int k = 0; k < part->field_count; k++) t->local_ids[k] = field_id(&names, part->fields[k]);
        }
        free_taken(&names);
    }
    for (int i = 0; i < n->enum_count; i++) {
        unity_cs_enum *e = &n->enums[i];
        const char *dot = strrchr(e->name, '.');
        e->ns = dot ? unity_cs_namespace(e->name, (size_t)(dot - e->name)) : NULL;
        const char *last = dot ? dot + 1 : e->name;
        e->cs = unity_cs_identifier(last, strlen(last));
        e->ids = calloc((size_t)e->member_count + 1, sizeof *e->ids);
        if (!e->ids) abort();
        taken used = {0};
        for (int k = 0; k < e->member_count; k++) {
            const char *member = e->members[k] ? e->members[k] : "_";
            char *id = unity_cs_identifier(member, strlen(member));
            if (take(&used, id)) e->ids[k] = id;
            else free(id);
        }
        free_taken(&used);
    }
}

bool unity_cs_read(const json *schema, unity_cs_names *out)
{
    *out = (unity_cs_names){0};
    const json *types = json_get(schema, "types"), *enums = json_get(schema, "enums");
    if (!types || types->kind != JSON_ARRAY) return false;
    out->types = calloc((size_t)types->count + 1, sizeof *out->types);
    if (!out->types) abort();
    out->type_count = types->count;
    for (int i = 0; i < types->count; i++) {
        const json *j = types->items[i];
        unity_cs_type *t = &out->types[i];
        t->name = text(j, "name");
        t->kind = text(j, "kind");
        const json *scene = json_get(j, "scene");
        t->scene = scene && scene->kind == JSON_TRUE;
        const json *fields = json_get(j, "fields");
        t->field_count = fields && fields->kind == JSON_ARRAY ? fields->count : 0;
        t->fields = calloc((size_t)t->field_count + 1, sizeof *t->fields);
        if (!t->fields) abort();
        for (int k = 0; k < t->field_count; k++) t->fields[k] = text(fields->items[k], "name");
        const json *local = json_get(j, "local");
        t->local = local && local->kind == JSON_NUMBER && local->number >= 0 ? (int)local->number : -1;
    }
    for (int i = 0; i < out->type_count; i++) {
        unity_cs_type *t = &out->types[i];
        if (t->local >= out->type_count || (t->local >= 0 && strcmp(out->types[t->local].kind, "local") != 0)) t->local = -1;
    }
    out->enum_count = enums && enums->kind == JSON_ARRAY ? enums->count : 0;
    out->enums = calloc((size_t)out->enum_count + 1, sizeof *out->enums);
    if (!out->enums) abort();
    for (int i = 0; i < out->enum_count; i++) {
        unity_cs_enum *e = &out->enums[i];
        e->name = text(enums->items[i], "name");
        const json *list = json_get(enums->items[i], "members");
        e->member_count = list && list->kind == JSON_ARRAY ? list->count : 0;
        e->members = calloc((size_t)e->member_count + 1, sizeof *e->members);
        if (!e->members) abort();
        for (int k = 0; k < e->member_count; k++) e->members[k] = json_str(list->items[k]);
    }
    unity_cs_assign(out);
    return true;
}

void unity_cs_free(unity_cs_names *n)
{
    free_names(n);
    for (int i = 0; i < n->type_count; i++) free((void *)n->types[i].fields);
    for (int i = 0; i < n->enum_count; i++) free((void *)n->enums[i].members);
    free(n->types);
    free(n->enums);
    *n = (unity_cs_names){0};
}
