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

// ---------------------------------------------------------------------------
// The game's C# types, from the schema: what a value C# holds is

typedef enum vkind {
    V_NONE,
    V_TYPE, // One of the schema's types: a class (a component, singleton, event, command), a struct, a layout struct
    V_ENUM,
} vkind;

typedef struct value_type {
    vkind kind;
    int index;  // In the names' types or enums
    bool value; // A class's or struct's nested Value, which has its fields but not a class's local ones
    bool many;  // A list or an array of it, whose element an indexer or a foreach gives
} value_type;

typedef struct game_types {
    unity_cs_names names;  // As the C# has them now
    char **full;           // Each type's C# name after Tide.Game. ("Combat.Health")
    char **enum_full;
    value_type **field_types; // Each type's fields' types
    // The rename: each type's and enum's new C# name, each field's, local field's and member's, or NULL for unchanged
    char **type_new, **enum_new;
    char ***field_new, ***local_new, ***member_new;
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
    return (value_type){V_NONE, -1, false, false};
}

static const char *tide_name(const char *s)
{
    return s ? s : "";
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

    // The names again, with the rename
    unity_cs_names renamed;
    if (!unity_cs_read(schema, &renamed)) return false;
    const char *dot = strrchr(type, '.');
    jbuf new_type = {0}, local = {0}, new_local = {0};
    jb_printf(&new_type, "%.*s%s", dot ? (int)(dot - type + 1) : 0, type, new_name);
    jb_printf(&local, "%s local", type);
    jb_printf(&new_local, "%s local", new_type.data);
    for (int i = 0; i < renamed.type_count; i++) {
        unity_cs_type *t = &renamed.types[i];
        const bool own = strcmp(t->name, type) == 0, part = strcmp(t->name, local.data) == 0;
        if (what == CSHARP_TYPE && own) t->name = new_type.data;
        if (what == CSHARP_TYPE && part) t->name = new_local.data;
        for (int k = 0; what == CSHARP_FIELD && (own || part) && k < t->field_count; k++) {
            if (strcmp(t->fields[k], name) == 0) t->fields[k] = new_name;
        }
    }
    for (int i = 0; i < renamed.enum_count; i++) {
        unity_cs_enum *e = &renamed.enums[i];
        if (strcmp(e->name, type) != 0) continue;
        if (what == CSHARP_TYPE) e->name = new_type.data;
        for (int k = 0; what == CSHARP_MEMBER && k < e->member_count; k++) {
            if (strcmp(tide_name(e->members[k]), name) == 0) e->members[k] = new_name;
        }
    }
    unity_cs_assign(&renamed);
    bool changed = false;
    g->type_new = calloc((size_t)n->type_count + 1, sizeof *g->type_new);
    g->field_new = calloc((size_t)n->type_count + 1, sizeof *g->field_new);
    g->local_new = calloc((size_t)n->type_count + 1, sizeof *g->local_new);
    g->enum_new = calloc((size_t)n->enum_count + 1, sizeof *g->enum_new);
    g->member_new = calloc((size_t)n->enum_count + 1, sizeof *g->member_new);
    if (!g->type_new || !g->field_new || !g->local_new || !g->enum_new || !g->member_new) abort();
    for (int i = 0; i < n->type_count; i++) {
        const unity_cs_type *was = &n->types[i], *now = &renamed.types[i];
        // Only its own name changes: its namespace is the same
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
    unity_cs_free(&g->names);
}

// ---------------------------------------------------------------------------
// C#'s tokens: names (a verbatim one's without its @), punctuation, and the rest. Comments, strings, characters and
// preprocessor lines are none, but an interpolated string's holes are code.

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

typedef struct lexer {
    const char *s;
    int n, i;
    token *items;
    int count, capacity;
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
            while (l->i < l->n && l->s[l->i] != '\n') l->i++;
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
// A file of C#

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

typedef struct cs_file {
    const char *text;
    token *t;
    int n;
    int *match;     // Each bracket's other one, or -1
    int *within;    // The innermost bracket each token is in, or -1
    bool *body;     // A '{' opening a type's body
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
    bool collecting; // The first pass: the project's names alone
    bool quiet;      // Working out declarations' types: no edits
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
static int block_of(const cs_file *f, int i)
{
    for (int b = f->within[i]; b >= 0; b = f->within[b]) {
        if (is(f, b, "{")) return b;
    }
    return -1;
}

static void match_brackets(cs_file *f)
{
    f->match = malloc(sizeof *f->match * (size_t)(f->n + 1));
    f->within = malloc(sizeof *f->within * (size_t)(f->n + 1));
    f->body = calloc((size_t)f->n + 1, sizeof *f->body);
    int *stack = malloc(sizeof *stack * (size_t)(f->n + 1));
    if (!f->match || !f->within || !f->body || !stack) abort();
    int depth = 0;
    for (int i = 0; i < f->n; i++) {
        f->match[i] = -1;
        f->within[i] = depth ? stack[depth - 1] : -1;
        if (is(f, i, "(") || is(f, i, "[") || is(f, i, "{")) {
            stack[depth++] = i;
            if (is(f, i, "{")) {
                // A type's body: `class`, `struct`, `interface` or `record` since the statement before
                for (int k = i - 1; k >= 0 && !is(f, k, ";") && !is(f, k, "}") && !is(f, k, "{"); k--) {
                    if (is(f, k, "class") || is(f, k, "struct") || is(f, k, "interface") || is(f, k, "record")) f->body[i] = true;
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

static const scan *current; // The scan the file is part of: its game's types and the project's names

// Where a variable named by token `i` is declared, in scope at token `at`: the innermost, or NULL (and *ambiguous
// where two of different types are equally inner)
static const declared *variable_at(const cs_file *f, const int i, const int at, bool *ambiguous)
{
    const char *s;
    int n;
    name_of(f, i, &s, &n);
    const declared *found = NULL;
    *ambiguous = false;
    for (int k = 0; k < f->decl_count; k++) {
        const declared *d = &f->decls[k];
        const token *t = &f->t[d->name];
        if (t->name_len != n || strncmp(f->text + t->name_at, s, (size_t)n) != 0 || at < d->from || at > d->to) continue;
        if (!found || d->to - d->from < found->to - found->from) {
            found = d;
            *ambiguous = false;
        } else if (d->to - d->from == found->to - found->from
                   && (d->type.kind != found->type.kind || d->type.index != found->type.index || d->type.value != found->type.value)) {
            *ambiguous = true;
        }
    }
    return found;
}

// The game's type a dotted name at `i` names: how many of its names (`*used`) make the type, a class's or struct's
// Value counted; V_NONE where it names none, or might name something else (a type or variable of the project's).
// `strong`: the place is one only a type can be.
static value_type resolve(const cs_file *f, const int i, int *used, const bool strong)
{
    *used = 0;
    const game_types *g = current->g;
    bool global = false;
    int first = i;
    if (name_is(f, i, "global") && is(f, i + 1, "::")) {
        global = true;
        first = i + 2;
    }
    if (!plain_name(f, first)) return (value_type){V_NONE, -1, false, false};
    int segs[32];
    int count = 0;
    for (int k = first; count < 32 && plain_name(f, k); k += 2) {
        segs[count++] = k;
        if (!is(f, k + 1, ".")) break;
    }
    const char *s0;
    int n0;
    name_of(f, segs[0], &s0, &n0);
    if (!global && !(n0 == 4 && strncmp(s0, "Tide", 4) == 0)) {
        // The first name might be the project's own: a type of its own, or a variable here
        if (set_has(&current->names->types, s0, n0)) return (value_type){V_NONE, -1, false, false};
        bool ambiguous;
        if (variable_at(f, segs[0], segs[0], &ambiguous)) return (value_type){V_NONE, -1, false, false};
        if (is(f, segs[count - 1] + 1, "=") && count == 1) return (value_type){V_NONE, -1, false, false}; // Assigned: no type
    }
    for (int k = count; k >= 1; k--) {
        jbuf name = {0};
        for (int s = 0; s < k; s++) {
            const char *p;
            int n;
            name_of(f, segs[s], &p, &n);
            jb_printf(&name, "%s%.*s", s ? "." : "", n, p);
        }
        // Its full name: as written; by an alias; under a namespace the file sees
        const char *candidates[64];
        int candidate_count = 0;
        candidates[candidate_count++] = name.data;
        jbuf expanded[66] = {{0}};
        int expanded_count = 0;
        for (int a = 0; !global && a < f->alias_count; a++) {
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
        value_type found = {V_NONE, -1, false, false};
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
        // A single name the project also declares as a variable or member somewhere: only where only a type can be,
        // or before a member only the type has
        if (k == 1 && !global && !(n0 == 4 && strncmp(s0, "Tide", 4) == 0) && set_has(&current->names->declared, s0, n0)
            && !strong && !(count > 1)) {
            return (value_type){V_NONE, -1, false, false};
        }
        *used = k;
        if (found.kind == V_TYPE && k < count && name_is(f, segs[k], "Value")
            && (g->names.types[found.index].base || g->names.types[found.index].live)) {
            found.value = true;
            (*used)++;
        }
        return found;
    }
    return (value_type){V_NONE, -1, false, false};
}

// A type as written from `i` (`Health`, `Tide.Game.Health.Value`, `List<Health>`, `Health[]`, `int?`): the token
// after it, or -1 where it's none; `type` what it is of the game's
static int skip_type(const cs_file *f, int i, value_type *type)
{
    *type = (value_type){V_NONE, -1, false, false};
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
            if (found.kind != V_NONE && used == names) *type = found;
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
    return f->match[open] >= 0 ? f->match[open] + 1 : -1;
}

static void add_edit(scan *s, const int place, const char *text)
{
    if (s->quiet || s->collecting) return;
    const token *t = &s->f->t[place];
    for (int i = 0; i < s->edit_count; i++) {
        if (s->edits[i].at == t->at) return;
    }
    s->edits = grown(s->edits, &s->edit_capacity, s->edit_count, sizeof *s->edits);
    s->edits[s->edit_count++] = (edit){t->at, t->len, text};
}

// Member `token` of a value of `v`: its field's type, or none; the edit where the rename changes its name
static value_type member(scan *s, const value_type v, const int place)
{
    const value_type none = {V_NONE, -1, false, false};
    if (v.kind != V_TYPE || v.many) return none;
    const game_types *g = s->g;
    const unity_cs_type *t = &g->names.types[v.index];
    const char *name;
    int n;
    name_of(s->f, place, &name, &n);
    for (int k = 0; k < t->field_count; k++) {
        const char *id = t->ids[k][0] == '@' ? t->ids[k] + 1 : t->ids[k];
        if (strlen(id) != (size_t)n || strncmp(id, name, (size_t)n) != 0) continue;
        if (g->field_new[v.index][k]) add_edit(s, place, g->field_new[v.index][k]);
        return g->field_types[v.index][k];
    }
    if (!v.value && t->local_ids) {
        const unity_cs_type *part = &g->names.types[t->local];
        for (int k = 0; k < part->field_count; k++) {
            const char *id = t->local_ids[k][0] == '@' ? t->local_ids[k] + 1 : t->local_ids[k];
            if (strlen(id) != (size_t)n || strncmp(id, name, (size_t)n) != 0) continue;
            if (g->local_new[v.index] && g->local_new[v.index][k]) add_edit(s, place, g->local_new[v.index][k]);
            return g->field_types[t->local][k];
        }
    }
    return none;
}

// An enum's member `token`, after the enum's name: the edit where the rename changes it
static void enum_member(scan *s, const int e, const int place)
{
    const unity_cs_enum *en = &s->g->names.enums[e];
    const char *name;
    int n;
    name_of(s->f, place, &name, &n);
    for (int k = 0; k < en->member_count; k++) {
        if (!en->ids[k]) continue;
        const char *id = en->ids[k][0] == '@' ? en->ids[k] + 1 : en->ids[k];
        if (strlen(id) == (size_t)n && strncmp(id, name, (size_t)n) == 0 && s->g->member_new[e][k]) {
            add_edit(s, place, s->g->member_new[e][k]);
        }
    }
}

// The members of an object initializer `{ a = 1, b = 2 }` at `open`, of a value of `v`
static void initializer(scan *s, const value_type v, const int open)
{
    const cs_file *f = s->f;
    const int close = f->match[open];
    for (int i = open + 1; i < close; i++) {
        if (f->within[i] != open) continue;
        if ((is(f, i - 1, "{") || is(f, i - 1, ",")) && plain_name(f, i) && is(f, i + 1, "=")) member(s, v, i);
    }
}

// Postfix steps from `i` on a value of `v`: members, generic calls (whose type argument is what they give: Get<T>(),
// GetComponent<T>()), calls and indexers, null-forgiving `!`. Where the chain ends, and what it gives.
static int postfix(scan *s, int i, value_type v, value_type *gives)
{
    const cs_file *f = s->f;
    const value_type none = {V_NONE, -1, false, false};
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
                v = none;
                i = args_end(f, name + 1);
                if (i < 0) break;
                continue;
            }
            v = member(s, v, name);
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
    if (gives) *gives = v;
    return i;
}

// The expression at `i` (a name, a generic call, `new`, `this.`): what it gives, and the token after it
static int expression(scan *s, int i, value_type *gives)
{
    const cs_file *f = s->f;
    const value_type none = {V_NONE, -1, false, false};
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
    if (is(f, i, "this") && is(f, i + 1, ".") && plain_name(f, i + 2)) {
        bool ambiguous;
        const declared *d = variable_at(f, i + 2, i + 2, &ambiguous);
        const value_type v = d && d->known && !ambiguous ? d->type : none;
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
    // A variable
    bool ambiguous;
    const declared *d = name_is(f, i, "global") ? NULL : variable_at(f, i, i, &ambiguous);
    if (d) return postfix(s, i + 1, d->known && !ambiguous ? d->type : none, gives);
    // A type of the game's, and what follows it: its Value, an enum's member
    const bool strong = is(f, i - 1, "new") || is(f, i - 1, "is") || is(f, i - 1, "as") || is(f, i - 1, "<")
                     || (is(f, i - 1, "(") && name_is(f, i - 2, "typeof")) || (is(f, i - 1, ",") && f->within[i] >= 0 && is(f, f->within[i], "<"))
                     || plain_name(f, i + 1);
    int used;
    const value_type type = resolve(f, i, &used, strong);
    if (type.kind == V_NONE) return i + 1;
    int names[33];
    int count_names = 0;
    for (int k = name_is(f, i, "global") ? i + 2 : i; plain_name(f, k) && count_names < 33; k += 2) {
        names[count_names++] = k;
        if (!is(f, k + 1, ".")) break;
    }
    const int type_name = names[used - 1 - (type.value ? 1 : 0)];
    const char *renamed = type.kind == V_TYPE ? s->g->type_new[type.index] : s->g->enum_new[type.index];
    // Named by its own name, not an alias's
    const char *old = type.kind == V_TYPE ? s->g->names.types[type.index].cs : s->g->names.enums[type.index].cs;
    if (old[0] == '@') old++;
    const char *written;
    int written_len;
    name_of(f, type_name, &written, &written_len);
    if (renamed && strlen(old) == (size_t)written_len && strncmp(old, written, (size_t)written_len) == 0) add_edit(s, type_name, renamed);
    if (type.kind == V_ENUM && used < count_names) {
        enum_member(s, type.index, names[used]);
        return names[used] + 1;
    }
    return names[used - 1] + 1;
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
    if (f->body[block]) *from = block;
    *to = f->match[block] >= 0 ? f->match[block] : f->n - 1;
}

// The generic call whose argument list `open` is: its one type argument, or none
static value_type call_type(const cs_file *f, const int open)
{
    const value_type none = {V_NONE, -1, false, false};
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

static void declarations(scan *s)
{
    cs_file *f = s->f;
    const value_type none = {V_NONE, -1, false, false};
    for (int i = 0; i < f->n; i++) {
        // Types the project declares
        if ((is(f, i, "class") || is(f, i, "struct") || is(f, i, "interface") || is(f, i, "enum") || is(f, i, "record"))
            && plain_name(f, i + 1)) {
            const char *name;
            int n;
            name_of(f, i + 1, &name, &n);
            set_add(&s->names->types, name, n);
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
        if (is(f, i - 1, "new") || is(f, i - 1, "class") || is(f, i - 1, "struct") || is(f, i - 1, "enum") || is(f, i - 1, "interface")) continue;
        value_type type;
        const int end = skip_type(f, i, &type);
        if (end < 0 || !plain_name(f, end) || name_is(f, end, "in") || name_is(f, end, "is") || name_is(f, end, "as")) continue;
        if (!(is(f, end + 1, "=") || is(f, end + 1, ";") || is(f, end + 1, ",") || is(f, end + 1, ")") || is(f, end + 1, "=>")
              || is(f, end + 1, "{") || is(f, end + 1, ":") || name_is(f, end + 1, "in"))) {
            continue;
        }
        const char *name;
        int n;
        name_of(f, end, &name, &n);
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
            const int block = block_of(f, end);
            from = end;
            to = block >= 0 && f->match[block] >= 0 ? f->match[block] : f->n - 1;
        }
        add_decl(f, end, type, known, from, to);
        // A list of declarators: `Health a, b;`
        for (int k = end + 1; is(f, k, ",") && plain_name(f, k + 1) && (is(f, k + 2, ",") || is(f, k + 2, ";") || is(f, k + 2, "="));) {
            name_of(f, k + 1, &name, &n);
            set_add(&s->names->declared, name, n);
            add_decl(f, k + 1, type, true, from, to);
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
    lexer l = {text, (int)strlen(text), 0, NULL, 0, 0};
    lex_code(&l, 0);
    cs_file f = {0};
    f.text = text;
    f.t = l.items;
    f.n = l.count;
    match_brackets(&f);
    usings(&f);
    s->f = &f;
    s->edit_count = 0;
    s->quiet = true;
    declarations(s);
    s->quiet = false;
    if (!s->collecting) {
        for (int i = 0; i < f.n; i++) {
            const bool start = (plain_name(&f, i) && chain_start(&f, i)) || is(&f, i, "new") || (is(&f, i, "this") && is(&f, i + 1, "."))
                            || (name_is(&f, i, "global") && is(&f, i + 1, "::"));
            if (!start) continue;
            value_type gives;
            expression(s, i, &gives);
        }
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
    // The project's own names first, then the edits
    project_names names = {0};
    scan s = {.g = &g, .names = &names, .out = out, .files_written = files_written, .collecting = true};
    current = &s;
    for (int pass = 0; pass < 2; pass++) {
        s.collecting = pass == 0;
        for (int i = 0; i < list.count; i++) folder_find(list.items[i], ".cs", scan_file, &s);
    }
    current = NULL;
    free(s.edits);
    for (int i = 0; i < list.count; i++) free(list.items[i]);
    free(list.items);
    set_free(&names.types);
    set_free(&names.declared);
    free_types(&g);
    free(text);
}
