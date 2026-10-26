// A rename reaching a Unity project's C# (csharp.h).

#include "csharp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "folders.h"
#include "unity_names.h"

static void *grown(void *items, int *capacity, const int count, const size_t size)
{
    if (count < *capacity) return items;
    *capacity = *capacity ? *capacity * 2 : 16;
    items = realloc(items, size * (size_t)*capacity);
    if (!items) abort();
    return items;
}

static char *copy_n(const char *s, const size_t n)
{
    char *c = malloc(n + 1);
    if (!c) abort();
    memcpy(c, s, n);
    c[n] = '\0';
    return c;
}

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    jbuf text = {0};
    char chunk[16384];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) jb_putn(&text, chunk, n);
    fclose(f);
    if (!text.data) jb_put(&text, "");
    return text.data;
}

static bool same(const char *a, const int an, const char *b)
{
    return strlen(b) == (size_t)an && strncmp(a, b, (size_t)an) == 0;
}

// An identifier as C# reads it: a verbatim one's without its @
static const char *bare(const char *id)
{
    return id[0] == '@' ? id + 1 : id;
}

// ---------------------------------------------------------------------------
// The game's C# types, from the schema: what a value C# holds is

typedef enum vkind {
    V_NONE,
    V_TYPE, // One of the schema's types: a class (a component, singleton, event, command), a struct, a layout struct
    V_ENUM,
    V_USER, // A class or struct of the project's own C#, by its name: its fields', properties' and methods' types
} vkind;

typedef struct value_type {
    vkind kind;
    int index;  // In the names' types or enums, or the project's classes
    bool value; // A class's or struct's nested Value, which has its fields but not a class's local ones
    bool many;  // A list or an array of it, whose element an indexer or a foreach gives
} value_type;

static const value_type none = {V_NONE, -1, false, false};

static bool same_type(const value_type a, const value_type b)
{
    return a.kind == b.kind && a.index == b.index && a.value == b.value && a.many == b.many;
}

typedef struct game_types {
    unity_cs_names names;  // As the C# has them now
    char **full;           // Each type's C# name after Tide.Game. ("Combat.Health")
    char **enum_full;
    value_type **field_types; // Each type's fields' types
    // The rename: each type's and enum's new C# name, each field's, local field's and member's, or NULL for unchanged
    char **type_new, **enum_new;
    char ***field_new, ***local_new, ***member_new;
    // A namespace's rename: its C# name ("Tide.Game.Combat.Melee"), how many names it is, its last one and its new one
    char *space, *space_last, *space_new;
    int space_names;
} game_types;

// A type string of the schema's ("List<Trail>", "Trail[4]", "Set<int>#map-order-2") as what C# holds of it
static value_type value_of(const game_types *g, const char *s)
{
    size_t n = strlen(s);
    const char *hash = memchr(s, '#', n);
    if (hash) n = (size_t)(hash - s);
    while (n && s[n - 1] == ' ') n--;
    bool many = false;
    if (n > 5 && strncmp(s, "List<", 5) == 0 && s[n - 1] == '>') {
        s += 5;
        n -= 6;
        many = true;
    } else if (n > 2 && s[n - 1] == ']') {
        while (n && s[n - 1] != '[') n--;
        if (n) n--;
        many = true;
    }
    for (int i = 0; i < g->names.type_count; i++) {
        if (strlen(g->names.types[i].name) == n && strncmp(g->names.types[i].name, s, n) == 0) {
            return (value_type){V_TYPE, i, false, many};
        }
    }
    for (int i = 0; i < g->names.enum_count; i++) {
        if (strlen(g->names.enums[i].name) == n && strncmp(g->names.enums[i].name, s, n) == 0) {
            return (value_type){V_ENUM, i, false, many};
        }
    }
    return none;
}

static const char *tide_name(const char *s)
{
    return s ? s : "";
}

// Whether a schema's name ("Combat.Melee.Health", "Combat.Melee.Health local") is the dotted `space` or in it
static bool in_space(const char *name, const char *space)
{
    const size_t n = strlen(space);
    return strncmp(name, space, n) == 0 && (!name[n] || name[n] == '.' || name[n] == ' ');
}

// `name` with the dotted `from` it's in as `to` instead, into `out`; false where it isn't in it
static bool moved(const char *name, const char *from, const char *to, jbuf *out)
{
    if (!in_space(name, from)) return false;
    jb_printf(out, "%s%s", to, name + strlen(from));
    return true;
}

// Reads the schema and works out what the rename makes of each name: false where it changes none.
static bool read_types(const json *schema, game_types *g, const csharp_what what, const char *type, const char *name,
                       const char *new_name)
{
    if (!unity_cs_read(schema, &g->names)) return false;
    const unity_cs_names *n = &g->names;
    g->full = calloc((size_t)n->type_count + 1, sizeof *g->full);
    g->enum_full = calloc((size_t)n->enum_count + 1, sizeof *g->enum_full);
    g->field_types = calloc((size_t)n->type_count + 1, sizeof *g->field_types);
    if (!g->full || !g->enum_full || !g->field_types) abort();
    const json *types = json_get(schema, "types");
    for (int i = 0; i < n->type_count; i++) {
        g->full[i] = unity_cs_full(n->types[i].ns, n->types[i].cs);
        g->field_types[i] = calloc((size_t)n->types[i].field_count + 1, sizeof **g->field_types);
        if (!g->field_types[i]) abort();
    }
    for (int i = 0; i < n->type_count; i++) {
        const json *fields = json_get(types->items[i], "fields");
        for (int k = 0; k < n->types[i].field_count; k++) {
            const char *of = json_str(json_get(fields->items[k], "type"));
            g->field_types[i][k] = value_of(g, of ? of : "");
        }
    }
    for (int i = 0; i < n->enum_count; i++) g->enum_full[i] = unity_cs_full(n->enums[i].ns, n->enums[i].cs);

    const char *dot = strrchr(type, '.');
    jbuf new_type = {0};
    jb_printf(&new_type, "%.*s%s", dot ? (int)(dot - type + 1) : 0, type, new_name);
    bool changed = false;
    if (what == CSHARP_NAMESPACE) {
        // Every type's and enum's names under it are as C# has them: only the namespace's last name changes in C#
        bool used = false;
        for (int i = 0; i < n->type_count; i++) used = used || in_space(n->types[i].name, type);
        for (int i = 0; i < n->enum_count; i++) used = used || in_space(n->enums[i].name, type);
        char *space = unity_cs_namespace(type, strlen(type));
        jbuf full = {0};
        jb_printf(&full, "Tide.Game.%s", space);
        free(space);
        g->space = full.data;
        g->space_names = 1;
        for (const char *c = g->space; *c; c++) g->space_names += *c == '.';
        const char *last = strrchr(g->space, '.');
        g->space_last = copy_n(last + 1, strlen(last + 1));
        g->space_new = unity_cs_identifier(new_name, strlen(new_name));
        changed = used && strcmp(g->space_last, g->space_new) != 0;
    }

    // The names again, with the rename
    unity_cs_names renamed;
    if (!unity_cs_read(schema, &renamed)) return false;
    jbuf local = {0}, new_local = {0};
    jb_printf(&local, "%s local", type);
    jb_printf(&new_local, "%s local", new_type.data);
    jbuf *names_moved = calloc((size_t)renamed.type_count + (size_t)renamed.enum_count + 1, sizeof *names_moved);
    if (!names_moved) abort();
    for (int i = 0; i < renamed.type_count; i++) {
        unity_cs_type *t = &renamed.types[i];
        const bool own = strcmp(t->name, type) == 0, part = strcmp(t->name, local.data) == 0;
        if (what == CSHARP_TYPE && own) t->name = new_type.data;
        if (what == CSHARP_TYPE && part) t->name = new_local.data;
        if (what == CSHARP_NAMESPACE && moved(t->name, type, new_type.data, &names_moved[i])) t->name = names_moved[i].data;
        for (int k = 0; what == CSHARP_FIELD && (own || part) && k < t->field_count; k++) {
            if (strcmp(t->fields[k], name) == 0) t->fields[k] = new_name;
        }
    }
    for (int i = 0; i < renamed.enum_count; i++) {
        unity_cs_enum *e = &renamed.enums[i];
        jbuf *m = &names_moved[renamed.type_count + i];
        if (what == CSHARP_NAMESPACE && moved(e->name, type, new_type.data, m)) e->name = m->data;
        if (strcmp(e->name, type) != 0) continue;
        if (what == CSHARP_TYPE) e->name = new_type.data;
        for (int k = 0; what == CSHARP_MEMBER && k < e->member_count; k++) {
            if (strcmp(tide_name(e->members[k]), name) == 0) e->members[k] = new_name;
        }
    }
    unity_cs_assign(&renamed);
    g->type_new = calloc((size_t)n->type_count + 1, sizeof *g->type_new);
    g->field_new = calloc((size_t)n->type_count + 1, sizeof *g->field_new);
    g->local_new = calloc((size_t)n->type_count + 1, sizeof *g->local_new);
    g->enum_new = calloc((size_t)n->enum_count + 1, sizeof *g->enum_new);
    g->member_new = calloc((size_t)n->enum_count + 1, sizeof *g->member_new);
    if (!g->type_new || !g->field_new || !g->local_new || !g->enum_new || !g->member_new) abort();
    for (int i = 0; i < n->type_count; i++) {
        const unity_cs_type *was = &n->types[i], *now = &renamed.types[i];
        // Only its own name changes: a namespace's rename is the namespace's
        if (strcmp(was->cs, now->cs) != 0) {
            g->type_new[i] = copy_n(now->cs, strlen(now->cs));
            changed = true;
        }
        g->field_new[i] = calloc((size_t)was->field_count + 1, sizeof **g->field_new);
        if (!g->field_new[i]) abort();
        for (int k = 0; k < was->field_count; k++) {
            if (strcmp(was->ids[k], now->ids[k]) == 0) continue;
            g->field_new[i][k] = copy_n(now->ids[k], strlen(now->ids[k]));
            changed = true;
        }
        if (was->local_ids && now->local_ids) {
            const int count = n->types[was->local].field_count;
            g->local_new[i] = calloc((size_t)count + 1, sizeof **g->local_new);
            if (!g->local_new[i]) abort();
            for (int k = 0; k < count; k++) {
                if (strcmp(was->local_ids[k], now->local_ids[k]) == 0) continue;
                g->local_new[i][k] = copy_n(now->local_ids[k], strlen(now->local_ids[k]));
                changed = true;
            }
        }
    }
    for (int i = 0; i < n->enum_count; i++) {
        const unity_cs_enum *was = &n->enums[i], *now = &renamed.enums[i];
        if (strcmp(was->cs, now->cs) != 0) {
            g->enum_new[i] = copy_n(now->cs, strlen(now->cs));
            changed = true;
        }
        g->member_new[i] = calloc((size_t)was->member_count + 1, sizeof **g->member_new);
        if (!g->member_new[i]) abort();
        for (int k = 0; k < was->member_count; k++) {
            if (!was->ids[k] || !now->ids[k] || strcmp(was->ids[k], now->ids[k]) == 0) continue;
            g->member_new[i][k] = copy_n(now->ids[k], strlen(now->ids[k]));
            changed = true;
        }
    }
    unity_cs_free(&renamed);
    for (int i = 0; i < n->type_count + n->enum_count; i++) jb_free(&names_moved[i]);
    free(names_moved);
    jb_free(&new_type);
    jb_free(&local);
    jb_free(&new_local);
    return changed;
}

static void free_types(game_types *g)
{
    const unity_cs_names *n = &g->names;
    for (int i = 0; i < n->type_count; i++) {
        free(g->full ? g->full[i] : NULL);
        free(g->field_types ? g->field_types[i] : NULL);
        free(g->type_new ? g->type_new[i] : NULL);
        for (int k = 0; g->field_new && g->field_new[i] && k < n->types[i].field_count; k++) free(g->field_new[i][k]);
        free(g->field_new ? g->field_new[i] : NULL);
        const int locals = n->types[i].local >= 0 ? n->types[n->types[i].local].field_count : 0;
        for (int k = 0; g->local_new && g->local_new[i] && k < locals; k++) free(g->local_new[i][k]);
        free(g->local_new ? g->local_new[i] : NULL);
    }
    for (int i = 0; i < n->enum_count; i++) {
        free(g->enum_full ? g->enum_full[i] : NULL);
        free(g->enum_new ? g->enum_new[i] : NULL);
        for (int k = 0; g->member_new && g->member_new[i] && k < n->enums[i].member_count; k++) free(g->member_new[i][k]);
        free(g->member_new ? g->member_new[i] : NULL);
    }
    free(g->full);
    free(g->enum_full);
    free(g->field_types);
    free(g->type_new);
    free(g->field_new);
    free(g->local_new);
    free(g->enum_new);
    free(g->member_new);
    free(g->space);
    free(g->space_last);
    free(g->space_new);
    unity_cs_free(&g->names);
}

// ---------------------------------------------------------------------------
// C#'s tokens: names (a verbatim one's without its @), punctuation, and the rest. Comments, strings, characters and
// preprocessor lines are none, but an interpolated string's holes are code, and a doc comment's `cref="..."` values
// are kept apart.

typedef enum tkind {
    TK_NAME,
    TK_PUNCT,
    TK_OTHER, // A number, a string, a character
} tkind;

typedef struct token {
    tkind kind;
    int at, len;           // The whole token: a verbatim name with its @
    int name_at, name_len; // A name's, without its @
} token;

typedef struct span {
    int at, len;
} span;

typedef struct lexer {
    const char *s;
    int n, i;
    token *items;
    int count, capacity;
    span *crefs; // Doc comments' cref values
    int cref_count, cref_capacity;
} lexer;

static void add(lexer *l, const tkind kind, const int at, const int len, const int name_at, const int name_len)
{
    l->items = grown(l->items, &l->capacity, l->count, sizeof *l->items);
    l->items[l->count++] = (token){kind, at, len, name_at, name_len};
}

static bool name_start(const char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (unsigned char)c >= 0x80;
}

static bool name_char(const char c)
{
    return name_start(c) || (c >= '0' && c <= '9');
}

static void lex_code(lexer *l, int hole);

// A string from just after its opening quote: `interpolated`'s holes lexed as code
static void lex_string(lexer *l, const bool interpolated, const bool verbatim)
{
    while (l->i < l->n) {
        const char c = l->s[l->i];
        if (verbatim && c == '"') {
            if (l->i + 1 < l->n && l->s[l->i + 1] == '"') {
                l->i += 2;
                continue;
            }
            l->i++;
            return;
        }
        if (!verbatim && c == '\\') {
            l->i += 2;
            continue;
        }
        if (!verbatim && c == '"') {
            l->i++;
            return;
        }
        if (!verbatim && c == '\n') return; // Not ended
        if (interpolated && (c == '{' || c == '}')) {
            if (l->i + 1 < l->n && l->s[l->i + 1] == c) {
                l->i += 2; // {{ and }}: the characters
                continue;
            }
            l->i++;
            if (c == '{') lex_code(l, 1);
            continue;
        }
        l->i++;
    }
}

// A doc comment's line (`/// <see cref="Health.hp"/>`): its cref values, which name code
static void doc_crefs(lexer *l, const int from, const int to)
{
    for (int i = from; i + 6 < to; i++) {
        if (strncmp(l->s + i, "cref", 4) != 0) continue;
        int k = i + 4;
        while (k < to && l->s[k] == ' ') k++;
        if (k >= to || l->s[k] != '=') continue;
        k++;
        while (k < to && l->s[k] == ' ') k++;
        if (k >= to || (l->s[k] != '"' && l->s[k] != '\'')) continue;
        const char quote = l->s[k++];
        int end = k;
        while (end < to && l->s[end] != quote) end++;
        if (end >= to) continue;
        l->crefs = grown(l->crefs, &l->cref_capacity, l->cref_count, sizeof *l->crefs);
        l->crefs[l->cref_count++] = (span){k, end - k};
        i = end;
    }
}

// Tokens up to the end, or in a hole (`hole`) to its '}' at its depth, past a format or alignment at that depth
static void lex_code(lexer *l, const int hole)
{
    int depth = 0;
    bool line_start = true;
    while (l->i < l->n) {
        const char c = l->s[l->i];
        const char next = l->i + 1 < l->n ? l->s[l->i + 1] : '\0';
        if (c == '\n') {
            line_start = true;
            l->i++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            l->i++;
            continue;
        }
        const bool was_line_start = line_start;
        line_start = false;
        if (c == '/' && next == '/') {
            const int from = l->i;
            while (l->i < l->n && l->s[l->i] != '\n') l->i++;
            if (from + 2 < l->n && l->s[from + 2] == '/' && !(from + 3 < l->n && l->s[from + 3] == '/')) doc_crefs(l, from, l->i);
            continue;
        }
        if (c == '/' && next == '*') {
            l->i += 2;
            while (l->i + 1 < l->n && !(l->s[l->i] == '*' && l->s[l->i + 1] == '/')) l->i++;
            l->i += 2;
            continue;
        }
        if (c == '#' && was_line_start && !hole) {
            while (l->i < l->n && l->s[l->i] != '\n') l->i++;
            continue;
        }
        const int at = l->i;
        if (c == '"' || (c == '@' && next == '"') || (c == '$' && next == '"') || (c == '$' && next == '@')
            || (c == '@' && next == '$')) {
            const bool interpolated = c == '$' || next == '$', verbatim = c == '@' || next == '@';
            while (l->i < l->n && l->s[l->i] != '"') l->i++;
            l->i++;
            lex_string(l, interpolated, verbatim);
            add(l, TK_OTHER, at, l->i - at, at, 0);
            continue;
        }
        if (c == '\'') {
            l->i++;
            if (l->i < l->n && l->s[l->i] == '\\') l->i++;
            if (l->i < l->n) l->i++;
            while (l->i < l->n && l->s[l->i] != '\'' && l->s[l->i] != '\n' && l->i - at < 12) l->i++;
            if (l->i < l->n && l->s[l->i] == '\'') l->i++;
            add(l, TK_OTHER, at, l->i - at, at, 0);
            continue;
        }
        if (name_start(c) || (c == '@' && name_start(next))) {
            const int from = c == '@' ? at + 1 : at;
            l->i = from;
            while (l->i < l->n && name_char(l->s[l->i])) l->i++;
            add(l, TK_NAME, at, l->i - at, from, l->i - from);
            continue;
        }
        if (c >= '0' && c <= '9') {
            while (l->i < l->n && (name_char(l->s[l->i])
                                   || (l->s[l->i] == '.' && l->i + 1 < l->n && l->s[l->i + 1] >= '0' && l->s[l->i + 1] <= '9'))) {
                l->i++;
            }
            add(l, TK_OTHER, at, l->i - at, at, 0);
            continue;
        }
        if (hole && depth == 0 && (c == '}' || c == ':' || c == ',') && !(c == ':' && next == ':')) {
            // The hole's end, or its format or alignment, which runs to its end
            while (l->i < l->n && l->s[l->i] != '}') l->i++;
            l->i++;
            return;
        }
        static const char *const pairs[] = {"::", "?.", "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "++", "--", "+=", "-=", "*=", "/=", "->"};
        int len = 1;
        for (size_t p = 0; p < sizeof pairs / sizeof pairs[0]; p++) {
            if (c == pairs[p][0] && next == pairs[p][1]) len = 2;
        }
        if (len == 1 && (c == '(' || c == '[' || c == '{')) depth++;
        if (len == 1 && (c == ')' || c == ']' || c == '}')) depth--;
        add(l, TK_PUNCT, at, len, at, len);
        l->i += len;
    }
}

// ---------------------------------------------------------------------------
// A file of C#, and the project's own types

static const char *const keywords[] = {
    "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue",
    "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally",
    "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock", "long",
    "namespace", "new", "null", "object", "operator", "out", "override", "params", "private", "protected", "public",
    "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string", "struct",
    "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort", "using",
    "virtual", "void", "volatile", "while", "await", "yield", "when", "where", "record", "nameof", NULL};

// Types C# names with a keyword (and `var`): a declaration's type, none of the game's
static const char *const keyword_types[] = {"bool", "byte", "char", "decimal", "double", "float", "int", "long", "object",
                                            "sbyte", "short", "string", "uint", "ulong", "ushort", "var", "dynamic", NULL};

typedef struct declared {
    int name;      // The token naming it
    value_type type;
    bool known;    // Its type is known (one of the game's, or something else); false: written with var we can't tell
    int from, to;  // The tokens where it's in scope
} declared;

typedef struct alias {
    int name;      // `using H = Tide.Game.Health;`: H's token
    char *target;  // "Tide.Game.Health"
} alias;

typedef struct names_set {
    char **items;
    int count, capacity;
} names_set;

// A member of a class or struct of the project's, by its name: a field, a property or a method (its result)
typedef struct user_member {
    char *name;
    value_type type;
    bool method, ambiguous; // Ambiguous: two of the name, of different types (in classes of one name, or overloads)
} user_member;

typedef struct user_class {
    char *name; // Its own name alone: the project's classes of one name are one here
    user_member *members;
    int count, capacity;
} user_class;

typedef struct cs_file {
    const char *text;
    token *t;
    int n;
    span *crefs;
    int cref_count;
    int *match;     // Each bracket's other one, or -1
    int *within;    // The innermost bracket each token is in, or -1
    int *body;      // A '{' opening a type's body: the token naming the type, else -1
    char **open;    // Namespaces whose names the file sees: its usings', its namespace's and theirs above it
    int open_count, open_capacity;
    alias *aliases;
    int alias_count, alias_capacity;
    declared *decls;
    int decl_count, decl_capacity;
} cs_file;

typedef struct project_names {
    names_set types;    // Types the project's C# declares
    names_set declared; // Variables, parameters, fields and properties it declares
    user_class *classes;
    int class_count, class_capacity;
} project_names;

typedef struct edit {
    int at, len;
    const char *text;
} edit;

typedef struct scan {
    game_types *g;
    project_names *names;
    jbuf *out;
    int *files_written;
    int pass;   // 0: the project's types' names; 1: their members; 2: the edits
    bool quiet; // Working out declarations' types: no edits
    cs_file *f;
    edit *edits;
    int edit_count, edit_capacity;
} scan;

static bool set_has(const names_set *s, const char *name, const int n)
{
    for (int i = 0; i < s->count; i++) {
        if (strlen(s->items[i]) == (size_t)n && strncmp(s->items[i], name, (size_t)n) == 0) return true;
    }
    return false;
}

static void set_add(names_set *s, const char *name, const int n)
{
    if (set_has(s, name, n)) return;
    s->items = grown(s->items, &s->capacity, s->count, sizeof *s->items);
    s->items[s->count++] = copy_n(name, (size_t)n);
}

static void set_free(names_set *s)
{
    for (int i = 0; i < s->count; i++) free(s->items[i]);
    free(s->items);
}

static int class_named(const project_names *p, const char *name, const int n)
{
    for (int i = 0; i < p->class_count; i++) {
        if (same(name, n, p->classes[i].name)) return i;
    }
    return -1;
}

static int class_add(project_names *p, const char *name, const int n)
{
    const int found = class_named(p, name, n);
    if (found >= 0) return found;
    p->classes = grown(p->classes, &p->class_capacity, p->class_count, sizeof *p->classes);
    p->classes[p->class_count] = (user_class){copy_n(name, (size_t)n), NULL, 0, 0};
    return p->class_count++;
}

static void member_add(project_names *p, const int c, const char *name, const int n, const value_type type, const bool method)
{
    user_class *k = &p->classes[c];
    for (int i = 0; i < k->count; i++) {
        user_member *m = &k->members[i];
        if (!same(name, n, m->name) || m->method != method) continue;
        if (!same_type(m->type, type)) m->ambiguous = true;
        return;
    }
    k->members = grown(k->members, &k->capacity, k->count, sizeof *k->members);
    k->members[k->count++] = (user_member){copy_n(name, (size_t)n), type, method, false};
}

// A member of the project's class `c`: its type, or none for none or an ambiguous one
static value_type user_member_type(const project_names *p, const int c, const char *name, const int n, const bool method)
{
    if (c < 0 || c >= p->class_count) return none;
    const user_class *k = &p->classes[c];
    for (int i = 0; i < k->count; i++) {
        const user_member *m = &k->members[i];
        if (same(name, n, m->name) && m->method == method) return m->ambiguous ? none : m->type;
    }
    return none;
}

static bool user_has_member(const project_names *p, const int c, const char *name, const int n)
{
    if (c < 0) return false;
    const user_class *k = &p->classes[c];
    for (int i = 0; i < k->count; i++) {
        if (same(name, n, k->members[i].name)) return true;
    }
    return false;
}

static bool is(const cs_file *f, const int i, const char *text)
{
    if (i < 0 || i >= f->n) return false;
    const token *t = &f->t[i];
    const int at = t->kind == TK_NAME ? t->name_at : t->at, len = t->kind == TK_NAME ? t->name_len : t->len;
    return strlen(text) == (size_t)len && strncmp(f->text + at, text, (size_t)len) == 0;
}

static bool in_list(const cs_file *f, const int i, const char *const *list)
{
    for (int k = 0; list[k]; k++) {
        if (is(f, i, list[k])) return true;
    }
    return false;
}

// A name that isn't a keyword
static bool plain_name(const cs_file *f, const int i)
{
    return i >= 0 && i < f->n && f->t[i].kind == TK_NAME && (f->t[i].at != f->t[i].name_at || !in_list(f, i, keywords));
}

static bool name_is(const cs_file *f, const int i, const char *name)
{
    return i >= 0 && i < f->n && f->t[i].kind == TK_NAME && is(f, i, name);
}

static void name_of(const cs_file *f, const int i, const char **s, int *n)
{
    *s = f->text + f->t[i].name_at;
    *n = f->t[i].name_len;
}

// Whether a name at `i` starts a dotted name rather than continuing one
static bool chain_start(const cs_file *f, const int i)
{
    return !is(f, i - 1, ".") && !is(f, i - 1, "?.") && !is(f, i - 1, "::") && !is(f, i - 1, "->");
}

// The innermost '{' a token is in, or -1
static int block_of(const cs_file *f, const int i)
{
    for (int b = f->within[i]; b >= 0; b = f->within[b]) {
        if (is(f, b, "{")) return b;
    }
    return -1;
}

static const scan *current; // The scan the file is part of: its game's types and the project's names

// The project's class a token is inside, the innermost, or -1
static int class_of(const cs_file *f, const int i)
{
    for (int b = f->within[i]; b >= 0; b = f->within[b]) {
        if (!is(f, b, "{") || f->body[b] < 0) continue;
        const char *name;
        int n;
        name_of(f, f->body[b], &name, &n);
        return class_named(current->names, name, n);
    }
    return -1;
}

static void match_brackets(cs_file *f)
{
    f->match = malloc(sizeof *f->match * (size_t)(f->n + 1));
    f->within = malloc(sizeof *f->within * (size_t)(f->n + 1));
    f->body = malloc(sizeof *f->body * (size_t)(f->n + 1));
    int *stack = malloc(sizeof *stack * (size_t)(f->n + 1));
    if (!f->match || !f->within || !f->body || !stack) abort();
    int depth = 0;
    for (int i = 0; i < f->n; i++) {
        f->match[i] = -1;
        f->body[i] = -1;
        f->within[i] = depth ? stack[depth - 1] : -1;
        if (is(f, i, "(") || is(f, i, "[") || is(f, i, "{")) {
            stack[depth++] = i;
            if (is(f, i, "{")) {
                // A type's body: `class`, `struct`, `interface` or `record` since the statement before, and its name
                for (int k = i - 1; k >= 0 && !is(f, k, ";") && !is(f, k, "}") && !is(f, k, "{"); k--) {
                    if ((is(f, k, "class") || is(f, k, "struct") || is(f, k, "interface") || is(f, k, "record")) && plain_name(f, k + 1)) {
                        f->body[i] = k + 1;
                    }
                }
            }
        } else if (is(f, i, ")") || is(f, i, "]") || is(f, i, "}")) {
            const char open = is(f, i, ")") ? '(' : is(f, i, "]") ? '[' : '{';
            int k = depth;
            while (k > 0 && f->text[f->t[stack[k - 1]].at] != open) k--;
            if (k > 0) {
                f->match[i] = stack[k - 1];
                f->match[stack[k - 1]] = i;
                depth = k - 1;
                f->within[i] = depth ? stack[depth - 1] : -1;
            }
        }
    }
    free(stack);
}

// A generic's arguments from its '<' at `i`: the token after its '>', or -1 where it's no list of types
static int skip_type(const cs_file *f, int i, value_type *type);

static int skip_type_args(const cs_file *f, int i, value_type *args, int *count)
{
    if (!is(f, i, "<")) return -1;
    *count = 0;
    for (i++;;) {
        value_type arg;
        const int end = skip_type(f, i, &arg);
        if (end < 0) return -1;
        if (*count < 4) args[*count] = arg;
        (*count)++;
        if (is(f, end, ",")) i = end + 1;
        else if (is(f, end, ">")) return end + 1;
        else return -1;
    }
}

static bool open_namespace(const cs_file *f, const char *ns, const size_t n)
{
    for (int i = 0; i < f->open_count; i++) {
        if (strlen(f->open[i]) == n && strncmp(f->open[i], ns, n) == 0) return true;
    }
    return false;
}

// Where a variable named `s` is declared, in scope at token `at`: the innermost, or NULL (and *ambiguous where two of
// different types are equally inner)
static const declared *variable_named(const cs_file *f, const char *s, const int n, const int at, bool *ambiguous)
{
    const declared *found = NULL;
    *ambiguous = false;
    for (int k = 0; k < f->decl_count; k++) {
        const declared *d = &f->decls[k];
        const token *t = &f->t[d->name];
        if (t->name_len != n || strncmp(f->text + t->name_at, s, (size_t)n) != 0 || at < d->from || at > d->to) continue;
        if (!found || d->to - d->from < found->to - found->from) {
            found = d;
            *ambiguous = false;
        } else if (d->to - d->from == found->to - found->from && !same_type(d->type, found->type)) {
            *ambiguous = true;
        }
    }
    return found;
}

static const declared *variable_at(const cs_file *f, const int i, const int at, bool *ambiguous)
{
    const char *s;
    int n;
    name_of(f, i, &s, &n);
    return variable_named(f, s, n, at, ambiguous);
}

// ---------------------------------------------------------------------------
// Dotted names: as tokens of the file, or as the text of a doc comment's cref

typedef struct segment {
    int at, len; // The name's own characters
    int token;   // Its token, or -1 for a cref's
} segment;

// A dotted name from token `i`: its names (`global::` aside); false for none
static int token_segments(const cs_file *f, const int i, segment *segs, const int most, bool *global)
{
    int first = i;
    *global = false;
    if (name_is(f, i, "global") && is(f, i + 1, "::")) {
        *global = true;
        first = i + 2;
    }
    int count = 0;
    for (int k = first; count < most && plain_name(f, k); k += 2) {
        segs[count++] = (segment){f->t[k].name_at, f->t[k].name_len, k};
        if (!is(f, k + 1, ".")) break;
    }
    return count;
}

// A cref's names ("T:Tide.Game.Health.hp", "global::Mode.Fast", "Health.Heal(int)")
static int cref_segments(const char *text, const span c, segment *segs, const int most, bool *global)
{
    int at = c.at, end = c.at + c.len;
    if (end - at > 2 && text[at + 1] == ':' && text[at] >= 'A' && text[at] <= 'Z') at += 2; // A documentation ID's kind
    *global = false;
    if (end - at > 8 && strncmp(text + at, "global::", 8) == 0) {
        *global = true;
        at += 8;
    }
    int count = 0;
    while (at < end && count < most) {
        int k = at;
        if (text[k] == '@') at = ++k;
        while (k < end && name_char(text[k])) k++;
        if (k == at) break;
        segs[count++] = (segment){at, k - at, -1};
        if (k >= end || text[k] != '.') break;
        at = k + 1;
    }
    return count;
}

// The names joined with dots, the first `k` of them
static void joined(const cs_file *f, const segment *segs, const int k, jbuf *out)
{
    for (int s = 0; s < k; s++) jb_printf(out, "%s%.*s", s ? "." : "", segs[s].len, f->text + segs[s].at);
}

// Whether the first of a dotted name might be the project's own, at token `at` (-1 for a cref's): a type of its own, or
// a variable there; only then is it no name of the game's
static bool project_own(const cs_file *f, const segment *segs, const bool global, const int at)
{
    if (global || same(f->text + segs[0].at, segs[0].len, "Tide")) return false;
    if (set_has(&current->names->types, f->text + segs[0].at, segs[0].len)) return true;
    bool ambiguous;
    return at >= 0 && variable_named(f, f->text + segs[0].at, segs[0].len, at, &ambiguous);
}

// The game's type the first names of a dotted name make: how many (`*used`, a class's or struct's Value counted); none
// where they make none or might be the project's own (project_own). `strong`: the place is one only a type can be.
static value_type resolve_names(const cs_file *f, const segment *segs, const int count, const bool global, const int at,
                                int *used, const bool strong)
{
    *used = 0;
    if (!count || project_own(f, segs, global, at)) return none;
    const game_types *g = current->g;
    const char *s0 = f->text + segs[0].at;
    const int n0 = segs[0].len;
    for (int k = count; k >= 1; k--) {
        jbuf name = {0};
        joined(f, segs, k, &name);
        // Its full name: as written; by an alias; under a namespace the file sees
        const char *candidates[64];
        int candidate_count = 0;
        candidates[candidate_count++] = name.data;
        jbuf expanded[66] = {{0}};
        int expanded_count = 0;
        for (int a = 0; !global && a < f->alias_count && expanded_count < 32; a++) {
            const char *an;
            int al;
            name_of(f, f->aliases[a].name, &an, &al);
            if (al != n0 || strncmp(an, s0, (size_t)n0) != 0) continue;
            jb_printf(&expanded[expanded_count], "%s%s", f->aliases[a].target, name.data + n0);
            candidates[candidate_count++] = expanded[expanded_count++].data;
        }
        for (int o = 0; !global && o < f->open_count && candidate_count < 64 && expanded_count < 66; o++) {
            jb_printf(&expanded[expanded_count], "%s.%s", f->open[o], name.data);
            candidates[candidate_count++] = expanded[expanded_count++].data;
        }
        value_type found = none;
        for (int c = 0; c < candidate_count && found.kind == V_NONE; c++) {
            if (strncmp(candidates[c], "Tide.Game.", 10) != 0) continue;
            const char *rest = candidates[c] + 10;
            for (int t = 0; t < g->names.type_count && found.kind == V_NONE; t++) {
                if (strcmp(g->full[t], rest) == 0) found = (value_type){V_TYPE, t, false, false};
            }
            for (int e = 0; e < g->names.enum_count && found.kind == V_NONE; e++) {
                if (strcmp(g->enum_full[e], rest) == 0) found = (value_type){V_ENUM, e, false, false};
            }
        }
        jb_free(&name);
        for (int x = 0; x < expanded_count; x++) jb_free(&expanded[x]);
        if (found.kind == V_NONE) continue;
        // A single name the project also declares as a variable or member somewhere: only where only a type can be
        if (k == 1 && count == 1 && !global && !same(s0, n0, "Tide") && set_has(&current->names->declared, s0, n0) && !strong) return none;
        *used = k;
        if (found.kind == V_TYPE && k < count && same(f->text + segs[k].at, segs[k].len, "Value")
            && (g->names.types[found.index].base || g->names.types[found.index].live)) {
            found.value = true;
            (*used)++;
        }
        return found;
    }
    return none;
}

static value_type resolve(const cs_file *f, const int i, int *used, const bool strong)
{
    segment segs[32];
    bool global;
    const int count = token_segments(f, i, segs, 32, &global);
    if (!count) return none;
    // Assigned (`Mode = ...`): no type
    if (count == 1 && !global && is(f, segs[0].token + 1, "=")) return none;
    return resolve_names(f, segs, count, global, segs[0].token, used, strong);
}

// The project's class a type's last name names, or -1
static int user_class_at(const cs_file *f, const int last)
{
    const char *name;
    int n;
    name_of(f, last, &name, &n);
    return class_named(current->names, name, n);
}

// A type as written from `i` (`Health`, `Tide.Game.Health.Value`, `List<Health>`, `Health[]`, `int?`, a class of the
// project's): the token after it, or -1 where it's none; `type` what it is
static int skip_type(const cs_file *f, int i, value_type *type)
{
    *type = none;
    if (in_list(f, i, keyword_types)) {
        i++;
    } else {
        const int start = i;
        if (name_is(f, i, "global") && is(f, i + 1, "::")) i += 2;
        if (!plain_name(f, i)) return -1;
        while (plain_name(f, i) && is(f, i + 1, ".") && plain_name(f, i + 2)) i += 2;
        const int last = i;
        i++;
        value_type args[4];
        int count = 0;
        const int after = is(f, i, "<") ? skip_type_args(f, i, args, &count) : -1;
        if (after >= 0) {
            i = after;
            static const char *const lists[] = {"List", "IList", "IReadOnlyList", "IEnumerable", "ICollection", "IReadOnlyCollection", NULL};
            if (count == 1 && args[0].kind != V_NONE && !args[0].many && in_list(f, last, lists)) {
                *type = args[0];
                type->many = true;
            }
        } else if (current) {
            int used;
            const value_type found = resolve(f, start, &used, true);
            // All of its names are the type's
            int names = 0;
            for (int k = start; k <= last; k++) names += f->t[k].kind == TK_NAME && !name_is(f, k, "global");
            if (found.kind != V_NONE && used == names) {
                *type = found;
            } else if (found.kind == V_NONE) {
                const int c = user_class_at(f, last);
                if (c >= 0) *type = (value_type){V_USER, c, false, false};
            }
        }
    }
    while (is(f, i, "?") || is(f, i, "*") || (is(f, i, "[") && (is(f, i + 1, "]") || is(f, i + 1, ",")))) {
        if (is(f, i, "[")) {
            while (i < f->n && !is(f, i, "]")) i++;
            if (type->kind != V_NONE) type->many = true;
        }
        i++;
    }
    return i;
}

// ---------------------------------------------------------------------------
// What an expression is: a chain of members, calls and indexers from where it starts

static int args_end(const cs_file *f, const int open)
{
    return open >= 0 && open < f->n && f->match[open] >= 0 ? f->match[open] + 1 : -1;
}

static void add_edit_at(scan *s, const int at, const int len, const char *text)
{
    if (s->quiet || s->pass != 2) return;
    for (int i = 0; i < s->edit_count; i++) {
        if (s->edits[i].at == at) return;
    }
    s->edits = grown(s->edits, &s->edit_capacity, s->edit_count, sizeof *s->edits);
    s->edits[s->edit_count++] = (edit){at, len, text};
}

// An edit of a name: its whole token (a verbatim name's @ too), or a cref's characters
static void add_edit(scan *s, const segment name, const char *text)
{
    if (name.token >= 0) add_edit_at(s, s->f->t[name.token].at, s->f->t[name.token].len, text);
    else add_edit_at(s, name.at, name.len, text);
}

static segment segment_of(const cs_file *f, const int i)
{
    return (segment){f->t[i].name_at, f->t[i].name_len, i};
}

// Member `name` of a value of `v` (not a call): its type, or none; the edit where the rename changes its name
static value_type member(scan *s, const value_type v, const segment name)
{
    if (v.many) return none;
    const char *text = s->f->text + name.at;
    const int n = name.len;
    if (v.kind == V_USER) return user_member_type(s->names, v.index, text, n, false);
    if (v.kind != V_TYPE) return none;
    const game_types *g = s->g;
    const unity_cs_type *t = &g->names.types[v.index];
    for (int k = 0; k < t->field_count; k++) {
        if (!same(text, n, bare(t->ids[k]))) continue;
        if (g->field_new[v.index][k]) add_edit(s, name, g->field_new[v.index][k]);
        return g->field_types[v.index][k];
    }
    if (!v.value && t->local_ids) {
        const unity_cs_type *part = &g->names.types[t->local];
        for (int k = 0; k < part->field_count; k++) {
            if (!same(text, n, bare(t->local_ids[k]))) continue;
            if (g->local_new[v.index] && g->local_new[v.index][k]) add_edit(s, name, g->local_new[v.index][k]);
            return g->field_types[t->local][k];
        }
    }
    return none;
}

// An enum's member `name`, after the enum's name: the edit where the rename changes it
static void enum_member(scan *s, const int e, const segment name)
{
    const unity_cs_enum *en = &s->g->names.enums[e];
    for (int k = 0; k < en->member_count; k++) {
        if (en->ids[k] && same(s->f->text + name.at, name.len, bare(en->ids[k])) && s->g->member_new[e][k]) {
            add_edit(s, name, s->g->member_new[e][k]);
        }
    }
}

// A type's name written in a dotted name: the edit where the rename changes it, where it's the type's own name (not
// an alias's)
static void type_name(scan *s, const value_type type, const segment name)
{
    const char *renamed = type.kind == V_TYPE ? s->g->type_new[type.index] : s->g->enum_new[type.index];
    const char *old = bare(type.kind == V_TYPE ? s->g->names.types[type.index].cs : s->g->names.enums[type.index].cs);
    if (renamed && same(s->f->text + name.at, name.len, old)) add_edit(s, name, renamed);
}

// A namespace's rename in a dotted name: where it, as written, as an alias expands it or under a namespace the file
// sees, is the renamed namespace or in it, the namespace's last name where it's written there
static void namespace_name(scan *s, const segment *segs, const int count, const bool global, const int at)
{
    const game_types *g = s->g;
    const cs_file *f = s->f;
    if (!g->space || !count || project_own(f, segs, global, at)) return;
    jbuf name = {0};
    joined(f, segs, count, &name);
    // The namespace's last name is the space_names-th of the full name: the written names come after `before` of it
    // (an alias's name standing for its target's)
    for (int c = -1; c < (global ? 0 : f->alias_count + f->open_count); c++) {
        jbuf full = {0};
        int written = g->space_names - 1, lowest = 0;
        if (c >= 0 && c < f->alias_count) {
            const char *an;
            int al;
            name_of(f, f->aliases[c].name, &an, &al);
            if (al != segs[0].len || strncmp(an, f->text + segs[0].at, (size_t)al) != 0) continue;
            jb_printf(&full, "%s%s", f->aliases[c].target, name.data + al);
            int target = 1;
            for (const char *p = f->aliases[c].target; *p; p++) target += *p == '.';
            written -= target - 1;
            lowest = 1; // Not the alias's own name, which its using renames
        } else if (c >= f->alias_count) {
            const char *open = f->open[c - f->alias_count];
            jb_printf(&full, "%s.%s", open, name.data);
            int names = 1;
            for (const char *p = open; *p; p++) names += *p == '.';
            written -= names;
        } else {
            jb_put(&full, name.data);
        }
        const bool in = in_space(full.data, g->space);
        jb_free(&full);
        if (!in) continue;
        if (written >= lowest && written < count && same(f->text + segs[written].at, segs[written].len, g->space_last)) {
            add_edit(s, segs[written], g->space_new);
        }
        break;
    }
    jb_free(&name);
}

// The members of an object initializer `{ a = 1, b = 2 }` at `open`, of a value of `v`
static void initializer(scan *s, const value_type v, const int open)
{
    const cs_file *f = s->f;
    const int close = f->match[open];
    for (int i = open + 1; i < close; i++) {
        if (f->within[i] != open) continue;
        if ((is(f, i - 1, "{") || is(f, i - 1, ",")) && plain_name(f, i) && is(f, i + 1, "=")) member(s, v, segment_of(f, i));
    }
}

// Postfix steps from `i` on a value of `v`: members, generic calls (whose type argument is what they give: Get<T>(),
// GetComponent<T>()), calls of the project's methods, indexers, null-forgiving `!`, then `as T`. Where the chain ends,
// and what it gives.
static int postfix(scan *s, int i, value_type v, value_type *gives)
{
    const cs_file *f = s->f;
    for (;;) {
        if ((is(f, i, ".") || is(f, i, "?.")) && plain_name(f, i + 1)) {
            const int name = i + 1;
            value_type args[4];
            int count = 0;
            const int after = is(f, name + 1, "<") ? skip_type_args(f, name + 1, args, &count) : -1;
            if (after >= 0 && is(f, after, "(")) {
                v = count == 1 ? args[0] : none;
                v.many = false;
                if (v.kind != V_NONE && is(f, args_end(f, after), "[")) v.many = true; // GetComponents<T>()[0]
                i = args_end(f, after);
                if (i < 0) break;
                continue;
            }
            if (is(f, name + 1, "(")) {
                const char *method;
                int n;
                name_of(f, name, &method, &n);
                v = v.kind == V_USER && !v.many ? user_member_type(s->names, v.index, method, n, true) : none;
                i = args_end(f, name + 1);
                if (i < 0) break;
                continue;
            }
            v = member(s, v, segment_of(f, name));
            i = name + 1;
            continue;
        }
        if (is(f, i, "[") && f->match[i] >= 0) {
            v = v.many ? (value_type){v.kind, v.index, v.value, false} : none;
            i = f->match[i] + 1;
            continue;
        }
        if (is(f, i, "!") && !is(f, i + 1, "=")) {
            i++;
            continue;
        }
        break;
    }
    if (is(f, i, "as")) {
        // `x as Health`: what it's taken as
        value_type as;
        const int end = skip_type(f, i + 1, &as);
        if (end >= 0) {
            v = as;
            i = end;
        }
    }
    if (gives) *gives = v;
    return i;
}

static int expression(scan *s, int i, value_type *gives);

// Whether a token can start what a cast casts
static bool operand_start(const cs_file *f, const int i)
{
    return plain_name(f, i) || is(f, i, "(") || is(f, i, "this") || is(f, i, "new") || is(f, i, "base")
        || (name_is(f, i, "global") && is(f, i + 1, "::"));
}

// The expression at `i` (a name, a generic call, a call of the project's method, `new`, `this.`, a cast, a
// parenthesized expression): what it gives, and the token after it
static int expression(scan *s, int i, value_type *gives)
{
    const cs_file *f = s->f;
    *gives = none;
    if (is(f, i, "new")) {
        value_type v;
        const int end = skip_type(f, i + 1, &v);
        if (end < 0) return i + 1;
        int after = end;
        if (is(f, after, "(")) after = args_end(f, after);
        if (after >= 0 && is(f, after, "{") && f->match[after] >= 0) {
            initializer(s, v, after);
            after = f->match[after] + 1;
        }
        if (after < 0) return i + 1;
        return postfix(s, after, v, gives);
    }
    if (is(f, i, "(") && f->match[i] >= 0) {
        const int close = f->match[i];
        // A cast, `(Health)x`: what it gives is the type, its operand an expression of its own
        value_type cast;
        const int end = skip_type(f, i + 1, &cast);
        if (end == close && cast.kind != V_NONE && operand_start(f, close + 1)) {
            value_type operand;
            const int stop = expression(s, close + 1, &operand);
            *gives = cast;
            return stop;
        }
        // Parenthesized: `((Health)x).hp`, `(x as Health).hp`
        value_type inner;
        const int stop = expression(s, i + 1, &inner);
        if (stop == close) return postfix(s, close + 1, inner, gives);
        return i + 1;
    }
    const int class_at = class_of(f, i);
    if (is(f, i, "this") && is(f, i + 1, ".") && plain_name(f, i + 2)) {
        // A member of its class: one this file declares, else one any file of the class declares (a partial class)
        const char *name;
        int n;
        name_of(f, i + 2, &name, &n);
        if (is(f, i + 3, "(")) {
            return postfix(s, args_end(f, i + 3), user_member_type(s->names, class_at, name, n, true), gives);
        }
        bool ambiguous;
        const declared *d = variable_at(f, i + 2, i + 2, &ambiguous);
        const value_type v = d ? (d->known && !ambiguous ? d->type : none) : user_member_type(s->names, class_at, name, n, false);
        return postfix(s, i + 3, v, gives);
    }
    if (!plain_name(f, i) && !(name_is(f, i, "global") && is(f, i + 1, "::"))) return i + 1;
    // A generic call: what its one type argument names
    value_type args[4];
    int count = 0;
    const int after = is(f, i + 1, "<") ? skip_type_args(f, i + 1, args, &count) : -1;
    if (after >= 0 && is(f, after, "(") && args_end(f, after) >= 0) {
        value_type v = count == 1 ? args[0] : none;
        v.many = false;
        if (v.kind != V_NONE && is(f, args_end(f, after), "[")) v.many = true;
        return postfix(s, args_end(f, after), v, gives);
    }
    // A variable, then a member of the class it's in (declared in this file, or another of a partial class's)
    bool ambiguous;
    const declared *d = name_is(f, i, "global") ? NULL : variable_at(f, i, i, &ambiguous);
    if (d) return postfix(s, i + 1, d->known && !ambiguous ? d->type : none, gives);
    const char *own;
    int own_len;
    name_of(f, i, &own, &own_len);
    if (!name_is(f, i, "global") && user_has_member(s->names, class_at, own, own_len)) {
        if (is(f, i + 1, "(")) return postfix(s, args_end(f, i + 1), user_member_type(s->names, class_at, own, own_len, true), gives);
        return postfix(s, i + 1, user_member_type(s->names, class_at, own, own_len, false), gives);
    }
    // A type of the game's, and what follows it: its Value, an enum's member
    const bool strong = is(f, i - 1, "new") || is(f, i - 1, "is") || is(f, i - 1, "as") || is(f, i - 1, "<")
                     || (is(f, i - 1, "(") && name_is(f, i - 2, "typeof")) || (is(f, i - 1, ",") && f->within[i] >= 0 && is(f, f->within[i], "<"))
                     || plain_name(f, i + 1);
    segment segs[33];
    bool global;
    const int names = token_segments(f, i, segs, 33, &global);
    namespace_name(s, segs, names, global, i);
    int used;
    const value_type type = resolve(f, i, &used, strong);
    if (type.kind == V_NONE) {
        // A class of the project's: its static members
        const int c = !global && is(f, i + 1, ".") ? user_class_at(f, i) : -1;
        if (c >= 0) return postfix(s, i + 1, (value_type){V_USER, c, false, false}, gives);
        return i + 1;
    }
    type_name(s, type, segs[used - 1 - (type.value ? 1 : 0)]);
    if (type.kind == V_ENUM && used < names) {
        enum_member(s, type.index, segs[used]);
        return segs[used].token + 1;
    }
    return segs[used - 1].token + 1;
}

// A doc comment's cref: the type, its member and the namespace it names, as code naming them would be
static void cref(scan *s, const span c)
{
    const cs_file *f = s->f;
    segment segs[32];
    bool global;
    const int count = cref_segments(f->text, c, segs, 32, &global);
    if (!count) return;
    namespace_name(s, segs, count, global, -1);
    int used;
    const value_type type = resolve_names(f, segs, count, global, -1, &used, true);
    if (type.kind == V_NONE) return;
    type_name(s, type, segs[used - 1 - (type.value ? 1 : 0)]);
    if (used >= count) return;
    if (type.kind == V_ENUM) enum_member(s, type.index, segs[used]);
    else member(s, type, segs[used]);
}

// ---------------------------------------------------------------------------
// Declarations: what each name is, and where

static void add_decl(cs_file *f, const int name, const value_type type, const bool known, const int from, const int to)
{
    f->decls = grown(f->decls, &f->decl_capacity, f->decl_count, sizeof *f->decls);
    f->decls[f->decl_count++] = (declared){name, type, known, from, to};
}

// The end of the statement from `i`: a block's '}', else its ';', else the end of what it's in
static int statement_end(const cs_file *f, int i)
{
    if (i >= f->n) return f->n - 1;
    if (is(f, i, "{") && f->match[i] >= 0) return f->match[i];
    const int at = f->within[i];
    const int limit = at >= 0 && f->match[at] >= 0 ? f->match[at] : f->n - 1;
    for (; i < limit; i++) {
        if (f->within[i] == at && is(f, i, ";")) return i;
    }
    return limit;
}

// Where a declaration of the name at `name` is in scope
static void scope_of(const cs_file *f, const int name, int *from, int *to)
{
    const int in = f->within[name];
    *from = name;
    *to = f->n - 1;
    if (in >= 0 && is(f, in, "(")) {
        const int close = f->match[in];
        if (name_is(f, in - 1, "foreach") || name_is(f, in - 1, "for") || name_is(f, in - 1, "using")
            || name_is(f, in - 1, "fixed") || name_is(f, in - 1, "catch")) {
            *from = in;
            *to = close >= 0 ? statement_end(f, close + 1) : f->n - 1;
            return;
        }
        // A pattern's variable in an if's or a while's condition: the rest of the block it's in, as C# has it
        if (name_is(f, in - 1, "if") || name_is(f, in - 1, "while")) {
            const int block = block_of(f, name);
            *from = in;
            *to = block >= 0 && f->match[block] >= 0 ? f->match[block] : f->n - 1;
            return;
        }
        // A method's or lambda's parameters: through its body
        int k = close >= 0 ? close + 1 : -1;
        while (k >= 0 && k < f->n && !is(f, k, "{") && !is(f, k, "=>") && !is(f, k, ";") && !is(f, k, ")") && !is(f, k, ",")) k++;
        if (k >= 0 && is(f, k, "{") && f->match[k] >= 0) {
            *from = in;
            *to = f->match[k];
            return;
        }
        if (k >= 0 && is(f, k, "=>")) {
            *from = in;
            const int outer = f->within[in];
            *to = outer >= 0 && f->match[outer] >= 0 ? f->match[outer] : statement_end(f, k + 1);
            return;
        }
    }
    // A field's or property's: its type's body; a local's: the rest of its block
    const int block = block_of(f, name);
    if (block < 0) return;
    if (f->body[block] >= 0) *from = block;
    *to = f->match[block] >= 0 ? f->match[block] : f->n - 1;
}

// The generic call whose argument list `open` is: its one type argument, or none
static value_type call_type(const cs_file *f, const int open)
{
    if (!is(f, open - 1, ">")) return none;
    int depth = 0, k = open - 1;
    for (; k >= 0; k--) {
        if (is(f, k, ">")) depth++;
        else if (is(f, k, "<") && --depth == 0) break;
    }
    value_type args[4];
    int count = 0;
    if (k < 1 || !plain_name(f, k - 1) || skip_type_args(f, k, args, &count) != open || count != 1) return none;
    return args[0];
}

// What may follow a declared name: `Health h;`, `= ...`, `(Health h)`, `foreach (Health h in`, `is Health h &&`
static bool after_declared(const cs_file *f, const int i)
{
    return is(f, i, "=") || is(f, i, ";") || is(f, i, ",") || is(f, i, ")") || is(f, i, "=>") || is(f, i, "{") || is(f, i, ":")
        || is(f, i, "&&") || is(f, i, "||") || name_is(f, i, "in") || name_is(f, i, "when");
}

static void declarations(scan *s)
{
    cs_file *f = s->f;
    for (int i = 0; i < f->n; i++) {
        // Types the project declares
        if ((is(f, i, "class") || is(f, i, "struct") || is(f, i, "interface") || is(f, i, "enum") || is(f, i, "record"))
            && plain_name(f, i + 1)) {
            const char *name;
            int n;
            name_of(f, i + 1, &name, &n);
            set_add(&s->names->types, name, n);
            if (!is(f, i, "enum")) class_add(s->names, name, n);
        }
        if (is(f, i, "delegate") && !is(f, i + 1, "(")) {
            int k = i + 1;
            while (k < f->n && !is(f, k, "(") && !is(f, k, ";")) k++;
            if (plain_name(f, k - 1)) {
                const char *name;
                int n;
                name_of(f, k - 1, &name, &n);
                set_add(&s->names->types, name, n);
            }
        }
        if (s->pass == 0) continue;
        // A lambda's parameter in a generic call: On<Hit>(hit => ...), On<Hit>((hit) => ...)
        if (plain_name(f, i) && (is(f, i + 1, "=>") || (is(f, i + 1, ")") && is(f, i + 2, "=>") && is(f, i - 1, "(")))) {
            int open = is(f, i - 1, "(") ? i - 1 : -1;
            if (open >= 0 && is(f, i + 1, ")")) open = is(f, open - 1, "(") ? open - 1 : is(f, open - 1, ",") ? f->within[open] : -1;
            else if (open < 0 && is(f, i - 1, ",")) open = f->within[i];
            const value_type v = open >= 0 && is(f, open, "(") ? call_type(f, open) : none;
            const char *name;
            int n;
            name_of(f, i, &name, &n);
            set_add(&s->names->declared, name, n);
            add_decl(f, i, v, v.kind != V_NONE, open >= 0 ? open : i, open >= 0 && f->match[open] >= 0 ? f->match[open] : statement_end(f, i));
            continue;
        }
        // `Type name` followed by what follows a declared name
        if (!(in_list(f, i, keyword_types) || (plain_name(f, i) && chain_start(f, i)) || (name_is(f, i, "global") && is(f, i + 1, "::")))) continue;
        if (is(f, i - 1, "new") || is(f, i - 1, "class") || is(f, i - 1, "struct") || is(f, i - 1, "enum") || is(f, i - 1, "interface")
            || is(f, i - 1, "record")) {
            continue;
        }
        value_type type;
        const int end = skip_type(f, i, &type);
        if (end < 0 || !plain_name(f, end) || name_is(f, end, "in") || name_is(f, end, "is") || name_is(f, end, "as")) continue;
        const char *name;
        int n;
        name_of(f, end, &name, &n);
        // A method of a class of the project's: what it gives (`Health Find() { ... }`)
        const int block = block_of(f, end);
        const int owner = block >= 0 && f->body[block] >= 0 ? user_class_at(f, f->body[block]) : -1;
        if (is(f, end + 1, "(")) {
            if (owner >= 0 && s->pass == 1 && !is(f, i, "var")) member_add(s->names, owner, name, n, type, true);
            continue;
        }
        if (!after_declared(f, end + 1)) continue;
        set_add(&s->names->declared, name, n);
        bool known = true;
        if (is(f, i, "var")) {
            // What it's given: `var h = GetComponent<Health>();`, `foreach (var h in healths)`, `out var h` in a
            // generic call
            known = false;
            if (is(f, end + 1, "=") || name_is(f, end + 1, "in")) {
                value_type gives;
                const int stop = expression(s, end + 2, &gives);
                const bool whole = is(f, stop, ";") || is(f, stop, ")") || is(f, stop, ",");
                // foreach over a list of it, or a generic call's (GetComponents<T>())
                if (whole && gives.kind != V_NONE && (name_is(f, end + 1, "in") || !gives.many)) {
                    type = gives;
                    type.many = false;
                    known = true;
                } else if (whole && gives.kind == V_NONE) {
                    known = true; // Something not of the game's
                }
            } else if (name_is(f, i - 1, "out") && f->within[i] >= 0 && is(f, f->within[i], "(")) {
                type = call_type(f, f->within[i]);
                known = type.kind != V_NONE;
            }
        }
        int from, to;
        scope_of(f, end, &from, &to);
        if (name_is(f, i - 1, "out")) {
            const int out_block = block_of(f, end);
            from = end;
            to = out_block >= 0 && f->match[out_block] >= 0 ? f->match[out_block] : f->n - 1;
        }
        add_decl(f, end, type, known, from, to);
        // A field or property of a class of the project's
        if (owner >= 0 && from == block && s->pass == 1 && known) member_add(s->names, owner, name, n, type, false);
        // A list of declarators: `Health a, b;`
        for (int k = end + 1; is(f, k, ",") && plain_name(f, k + 1) && (is(f, k + 2, ",") || is(f, k + 2, ";") || is(f, k + 2, "="));) {
            name_of(f, k + 1, &name, &n);
            set_add(&s->names->declared, name, n);
            add_decl(f, k + 1, type, true, from, to);
            if (owner >= 0 && from == block && s->pass == 1) member_add(s->names, owner, name, n, type, false);
            k += 2;
            if (is(f, k, "=")) break;
        }
    }
}

// The usings, aliases and namespaces the file has
static void usings(cs_file *f)
{
    for (int i = 0; i < f->n; i++) {
        const bool space = is(f, i, "namespace");
        if (!is(f, i, "using") && !space) continue;
        if (is(f, i + 1, "static") || is(f, i + 1, "(") || is(f, i + 1, "var")) continue;
        int k = i + 1;
        const int named = plain_name(f, k) && is(f, k + 1, "=") ? k : -1;
        if (named >= 0) k += 2;
        jbuf name = {0};
        for (; plain_name(f, k); k += 2) {
            const char *p;
            int n;
            name_of(f, k, &p, &n);
            jb_printf(&name, "%s%.*s", name.len ? "." : "", n, p);
            if (!is(f, k + 1, ".")) break;
        }
        if (!name.data || !(is(f, k + 1, ";") || (space && is(f, k + 1, "{")))) {
            jb_free(&name);
            continue;
        }
        if (named >= 0) {
            f->aliases = grown(f->aliases, &f->alias_capacity, f->alias_count, sizeof *f->aliases);
            f->aliases[f->alias_count++] = (alias){named, name.data};
            continue;
        }
        // A namespace sees its own names and those of the namespaces it's in
        for (size_t len = strlen(name.data);;) {
            if (!open_namespace(f, name.data, len)) {
                f->open = grown(f->open, &f->open_capacity, f->open_count, sizeof *f->open);
                f->open[f->open_count++] = copy_n(name.data, len);
            }
            if (!space) break;
            while (len && name.data[len - 1] != '.') len--;
            if (!len) break;
            len--;
        }
        jb_free(&name);
    }
}

static void free_file(cs_file *f)
{
    free(f->t);
    free(f->crefs);
    free(f->match);
    free(f->within);
    free(f->body);
    for (int i = 0; i < f->open_count; i++) free(f->open[i]);
    free(f->open);
    for (int i = 0; i < f->alias_count; i++) free(f->aliases[i].target);
    free(f->aliases);
    free(f->decls);
}

// A byte offset's line and column as LSP counts them: UTF-16 code units
static void position(const char *text, const int at, int *line, int *column)
{
    *line = 0;
    int start = 0;
    for (int i = 0; i < at; i++) {
        if (text[i] == '\n') {
            (*line)++;
            start = i + 1;
        }
    }
    *column = 0;
    for (int i = start; i < at; i++) {
        const unsigned char c = (unsigned char)text[i];
        if ((c & 0xC0) == 0x80) continue;
        *column += c >= 0xF0 ? 2 : 1;
    }
}

static int by_place(const void *a, const void *b)
{
    return ((const edit *)a)->at - ((const edit *)b)->at;
}

static void scan_file(void *user, const char *path)
{
    scan *s = user;
    if (strstr(path, "~/")) return; // A folder Unity leaves out
    char *text = read_all(path);
    if (!text) return;
    lexer l = {text, (int)strlen(text), 0, NULL, 0, 0, NULL, 0, 0};
    lex_code(&l, 0);
    cs_file f = {0};
    f.text = text;
    f.t = l.items;
    f.n = l.count;
    f.crefs = l.crefs;
    f.cref_count = l.cref_count;
    match_brackets(&f);
    usings(&f);
    s->f = &f;
    s->edit_count = 0;
    s->quiet = true;
    declarations(s);
    s->quiet = false;
    if (s->pass == 2) {
        for (int i = 0; i < f.n; i++) {
            if (is(&f, i - 1, "namespace")) continue; // The project's own namespaces, which the game's rename isn't
            const bool start = (plain_name(&f, i) && chain_start(&f, i)) || is(&f, i, "new") || is(&f, i, "(")
                            || (is(&f, i, "this") && is(&f, i + 1, ".")) || (name_is(&f, i, "global") && is(&f, i + 1, "::"));
            if (!start) continue;
            value_type gives;
            expression(s, i, &gives);
        }
        for (int i = 0; i < f.cref_count; i++) cref(s, f.crefs[i]);
    }
    if (s->edit_count) {
        qsort(s->edits, (size_t)s->edit_count, sizeof *s->edits, by_place);
        jbuf uri = {0};
        jb_put(&uri, path[0] == '/' ? "file://" : "file:///");
        for (const char *c = path; *c; c++) {
            if (*c == ' ' || *c == '%' || *c == '#' || *c == '?') jb_printf(&uri, "%%%02X", (unsigned char)*c);
            else jb_putn(&uri, c, 1);
        }
        if ((*s->files_written)++) jb_put(s->out, ",");
        jb_string(s->out, uri.data);
        jb_put(s->out, ":[");
        for (int i = 0; i < s->edit_count; i++) {
            const edit *e = &s->edits[i];
            int line, from, to;
            position(text, e->at, &line, &from);
            position(text, e->at + e->len, &line, &to);
            jb_printf(s->out, "%s{\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}},\"newText\":",
                      i ? "," : "", line, from, line, to);
            jb_string(s->out, e->text);
            jb_put(s->out, "}");
        }
        jb_put(s->out, "]");
        jb_free(&uri);
    }
    s->f = NULL;
    free_file(&f);
    free(text);
}

// ---------------------------------------------------------------------------
// The project's C#: Assets, and its embedded and local packages but the game's own

typedef struct folders {
    char **items;
    int count, capacity;
    const char *project;
} folders;

static void add_folder(folders *list, const char *path)
{
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], path) == 0) return;
    }
    list->items = grown(list->items, &list->capacity, list->count, sizeof *list->items);
    list->items[list->count++] = copy_n(path, strlen(path));
}

// Whether a package folder is tide's own com.tide.game
static bool game_package(const char *folder)
{
    jbuf path = {0};
    jb_printf(&path, "%spackage.json", folder);
    char *text = read_all(path.data);
    jb_free(&path);
    const bool game = text && strstr(text, "\"com.tide.game\"");
    free(text);
    return game;
}

static void embedded(void *user, const char *name, const bool is_folder)
{
    folders *list = user;
    if (!is_folder) return;
    jbuf path = {0};
    jb_printf(&path, "%s/Packages/%s/", list->project, name);
    if (!game_package(path.data)) add_folder(list, path.data);
    jb_free(&path);
}

void csharp_renames(const char *project, const csharp_what what, const char *type, const char *name, const char *new_name,
                    jbuf *out, int *files_written)
{
    jbuf path = {0};
    jb_printf(&path, "%s/Packages/com.tide.game/schema.json", project);
    char *text = read_all(path.data);
    jb_free(&path);
    if (!text) return;
    json *schema = json_parse(text, strlen(text));
    game_types g = {0};
    if (!schema || !read_types(schema, &g, what, type, name ? name : "", new_name)) {
        free_types(&g);
        free(text);
        return;
    }
    folders list = {NULL, 0, 0, project};
    jbuf assets = {0};
    jb_printf(&assets, "%s/Assets/", project);
    add_folder(&list, assets.data);
    jb_free(&assets);
    jbuf packages = {0};
    jb_printf(&packages, "%s/Packages/", project);
    folder_list(packages.data, embedded, &list);
    // Local packages: the manifest's "file:" dependencies
    jbuf manifest_path = {0};
    jb_printf(&manifest_path, "%smanifest.json", packages.data);
    char *manifest_text = read_all(manifest_path.data);
    jb_free(&manifest_path);
    const json *manifest = manifest_text ? json_parse(manifest_text, strlen(manifest_text)) : NULL;
    const json *dependencies = json_get(manifest, "dependencies");
    for (int i = 0; dependencies && dependencies->kind == JSON_OBJECT && i < dependencies->count; i++) {
        const char *value = json_str(dependencies->items[i]);
        if (!value || strncmp(value, "file:", 5) != 0) continue;
        const char *where = value + 5;
        jbuf folder = {0};
        const bool absolute = where[0] == '/' || where[0] == '\\' || (where[0] && where[1] == ':');
        if (absolute) jb_printf(&folder, "%s/", where);
        else jb_printf(&folder, "%s%s/", packages.data, where);
        for (char *c = folder.data; *c; c++) {
            if (*c == '\\') *c = '/';
        }
        // Without its "x/../"s, as the editor names the files
        for (char *up; (up = strstr(folder.data, "/../"));) {
            char *above = up;
            while (above > folder.data && above[-1] != '/') above--;
            if (above == folder.data || strncmp(above, "../", 3) == 0) break;
            memmove(above, up + 4, strlen(up + 4) + 1);
        }
        if (strcmp(dependencies->keys[i], "com.tide.game") != 0) {
            jbuf package_json = {0};
            jb_printf(&package_json, "%spackage.json", folder.data);
            if (folder_file_info(package_json.data, NULL, NULL) && !game_package(folder.data)) add_folder(&list, folder.data);
            jb_free(&package_json);
        }
        jb_free(&folder);
    }
    jb_free(&packages);
    free(manifest_text);
    // The project's types' names, then their members, then the edits
    project_names names = {0};
    scan s = {.g = &g, .names = &names, .out = out, .files_written = files_written};
    current = &s;
    for (s.pass = 0; s.pass < 3; s.pass++) {
        for (int i = 0; i < list.count; i++) folder_find(list.items[i], ".cs", scan_file, &s);
    }
    current = NULL;
    free(s.edits);
    for (int i = 0; i < list.count; i++) free(list.items[i]);
    free(list.items);
    set_free(&names.types);
    set_free(&names.declared);
    for (int i = 0; i < names.class_count; i++) {
        for (int k = 0; k < names.classes[i].count; k++) free(names.classes[i].members[k].name);
        free(names.classes[i].members);
        free(names.classes[i].name);
    }
    free(names.classes);
    free_types(&g);
    free(text);
}
