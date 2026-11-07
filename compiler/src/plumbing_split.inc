// The generated C in two files (codegen_options.split_plumbing): the game's own code, the functions whose lines
// are its .tide files' (systems, views, handlers, functions, methods), stays in <name>.c, which a debug build
// compiles unoptimized for its debugger; everything else, the plumbing (copies, hashes, packing, serializing,
// queries' rows, dispatch, assets, shaders), goes in <name>_plumbing.c, which it compiles as it does the engine.
// clang can't optimize one function of a file compiled at -O0, so the file is cut.
//
// It works on the finished text, a top-level item at a time (generated C starts each at the start of a line):
// - a function's definition goes to one file, the code's when its lines are a .tide file's;
// - a variable goes to the plumbing's, with three exceptions. A small `static const` one both files name goes to both,
//   so its size and value stay known to the code reading it. A static one only the code names goes to the code's. So
//   do those codegen lists (`code_at`), which an edit of the code changes: the game's table, with the simulation's
//   identity, and the profile's .tide lines. An edit of the code alone then leaves the plumbing's file, and its
//   object, as they were;
// - everything else (directives, types, prototypes, extern declarations) goes to both, in the same order, but the
//   #line directives naming .tide files before the code's functions, which the plumbing's file has no use for.
// A function or variable one file names and the other defines takes external linkage (TIDE_SPLIT: hidden), and the
// other file declares it where the definition was. Everything else keeps its linkage: `static` functions only the
// plumbing calls stay its own to inline. A static name of the plumbing's that takes external linkage is renamed
// tide_split_<name> in both files, outside strings and comments: as a static it could share its name with a symbol
// of the program's, as tide_game_pack, the world's packing, does with the pack's bytes (`tidec --pack-c`). The
// game's code keeps its names, which debuggers show.

typedef enum split_kind {
    SPLIT_BOTH,      // Comments, directives, types, prototypes: in both files
    SPLIT_FUNCTION,  // A function's definition
    SPLIT_VARIABLE,  // A variable's definition (or a tentative one)
} split_kind;

typedef struct split_item {
    size_t start, end; // [start, end) in the text
    size_t brace;      // A function's: where its body's { is
    size_t init;       // A variable's: where its = is, or its ;
    size_t name, name_len;
    split_kind kind;
    bool code;     // A function of the game's own code
    bool constant; // A variable whose object is const
    bool is_static;
    bool prototype; // SPLIT_BOTH: a function's declaration, whose linkage may change
    bool define;    // SPLIT_BOTH: a #define (its name is the macro's), whose names a file using the macro uses
    bool tide_line; // SPLIT_BOTH: a #line naming a .tide file
} split_item;

typedef VEC(split_item) split_item_list;

typedef struct split_name {
    const char *name;
    size_t len;
    bool function, variable;
    bool code;        // A function or variable defined in the code's file
    bool shared;      // A variable both files define (small static const)
    bool by_code;     // Named by the code's file
    bool by_plumbing; // ...by the plumbing's
    bool declared;    // The file that doesn't define this variable has had its extern declaration
    bool is_static;   // Defined static (or with a macro meaning it)
    bool renamed;     // Written tide_split_<name> (above)
    bool field;       // Also a field's name somewhere: never renamed, as a struct declaring it would be
    bool data;        // A large constant SPLIT_DATA puts in the second file, whoever names it
    int first_item;   // split_prune: the first item defining it (+1, 0 for none), the rest through its next list
    bool reached;     // ...and whether what it keeps reaches it
} split_name;

typedef struct split_names {
    split_name *slots;
    size_t cap, count;
    bool renames; // Some name is renamed
} split_names;

static uint64_t split_hash(const char *s, const size_t n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 0x100000001B3ull;
    return h;
}

static split_name *split_find(const split_names *t, const char *s, const size_t n)
{
    if (!t->cap) return NULL;
    for (size_t i = (size_t)split_hash(s, n) & (t->cap - 1);; i = (i + 1) & (t->cap - 1)) {
        split_name *e = &t->slots[i];
        if (!e->name) return NULL;
        if (e->len == n && memcmp(e->name, s, n) == 0) return e;
    }
}

static split_name *split_add(split_names *t, const char *s, const size_t n)
{
    split_name *found = split_find(t, s, n);
    if (found) return found;
    if ((t->count + 1) * 2 > t->cap) {
        const size_t cap = t->cap ? t->cap * 2 : 1024;
        split_name *slots = arena_alloc(cap * sizeof *slots);
        for (size_t i = 0; i < t->cap; i++) {
            if (!t->slots[i].name) continue;
            size_t k = (size_t)split_hash(t->slots[i].name, t->slots[i].len) & (cap - 1);
            while (slots[k].name) k = (k + 1) & (cap - 1);
            slots[k] = t->slots[i];
        }
        t->slots = slots;
        t->cap = cap;
    }
    size_t i = (size_t)split_hash(s, n) & (t->cap - 1);
    while (t->slots[i].name) i = (i + 1) & (t->cap - 1);
    t->slots[i] = (split_name){.name = s, .len = n};
    t->count++;
    return &t->slots[i];
}

static bool split_ident_start(const char c)
{
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool split_ident_char(const char c)
{
    return split_ident_start(c) || (c >= '0' && c <= '9');
}

// The end of the line at `at` (its \n, or the text's end), following backslashes that continue it.
static size_t split_line_end(const char *t, const size_t n, size_t at)
{
    for (;;) {
        while (at < n && t[at] != '\n') at++;
        if (at < n && at > 0 && t[at - 1] == '\\') {
            at++;
            continue;
        }
        return at;
    }
}

// Whether the directive [at, end) is a #line naming a .tide file (1), naming another (0), or no #line (-1).
static int split_line_names_tide(const char *t, const size_t at, const size_t end)
{
    size_t i = at + 1;
    while (i < end && (t[i] == ' ' || t[i] == '\t')) i++;
    if (end - i < 4 || memcmp(t + i, "line", 4) != 0) return -1;
    const char *close = NULL;
    for (size_t k = end; k > i; k--) {
        if (t[k - 1] == '"') {
            close = t + k - 1;
            break;
        }
    }
    if (!close || close - t < 5) return 0;
    return memcmp(close - 5, ".tide", 5) == 0;
}

// A copy of [from, to) with comments, strings, characters and directive lines as spaces, the same length, so
// offsets in it are the text's.
static char *split_clean(const char *t, const size_t from, const size_t to)
{
    char *c = arena_alloc(to - from + 1);
    memcpy(c, t + from, to - from);
    for (size_t i = 0; i < to - from;) {
        const bool line_start = i == 0 || c[i - 1] == '\n';
        if (line_start) {
            size_t k = i;
            while (k < to - from && (c[k] == ' ' || c[k] == '\t')) k++;
            if (k < to - from && c[k] == '#') {
                const size_t end = split_line_end(c, to - from, k);
                for (size_t m = k; m < end; m++) c[m] = ' ';
                i = end;
                continue;
            }
        }
        if (c[i] == '/' && i + 1 < to - from && c[i + 1] == '/') {
            while (i < to - from && c[i] != '\n') c[i++] = ' ';
        } else if (c[i] == '/' && i + 1 < to - from && c[i + 1] == '*') {
            while (i < to - from && !(c[i] == '*' && i + 1 < to - from && c[i + 1] == '/')) c[i++] = ' ';
            for (int m = 0; m < 2 && i < to - from; m++) c[i++] = ' ';
        } else if (c[i] == '"' || c[i] == '\'') {
            const char q = c[i++];
            while (i < to - from && c[i] != q && c[i] != '\n') {
                if (c[i] == '\\' && i + 1 < to - from) c[i++] = ' ';
                c[i++] = ' ';
            }
            if (i < to - from) i++;
        } else {
            i++;
        }
    }
    return c;
}

static bool split_word_is(const char *s, const size_t n, const char *word)
{
    return strlen(word) == n && memcmp(s, word, n) == 0;
}

// In the cleaned head c[0, n): whether it ends in a function's declarator, `name(...)` after any attributes, and
// where that name is.
static bool split_function_head(const char *c, size_t n, size_t *name, size_t *name_len)
{
    for (;;) {
        while (n > 0 && (c[n - 1] == ' ' || c[n - 1] == '\t' || c[n - 1] == '\n' || c[n - 1] == '\r')) n--;
        if (n == 0 || c[n - 1] != ')') return false;
        int depth = 0;
        size_t open = n;
        while (open > 0) {
            open--;
            if (c[open] == ')') depth++;
            else if (c[open] == '(' && --depth == 0) break;
        }
        if (depth != 0) return false;
        size_t e = open;
        while (e > 0 && (c[e - 1] == ' ' || c[e - 1] == '\t' || c[e - 1] == '\n' || c[e - 1] == '\r')) e--;
        size_t s = e;
        while (s > 0 && split_ident_char(c[s - 1])) s--;
        if (s == e || !split_ident_start(c[s])) return false;
        if (split_word_is(c + s, e - s, "__attribute__") || split_word_is(c + s, e - s, "__declspec")
            || split_word_is(c + s, e - s, "_Alignas") || split_word_is(c + s, e - s, "__asm__")
            || split_word_is(c + s, e - s, "asm")) {
            n = s;
            continue;
        }
        *name = s;
        *name_len = e - s;
        return true;
    }
}

static bool split_first_word_is(const char *c, const size_t n, const char *word)
{
    size_t i = 0;
    while (i < n && (c[i] == ' ' || c[i] == '\t' || c[i] == '\n' || c[i] == '\r')) i++;
    size_t e = i;
    while (e < n && split_ident_char(c[e])) e++;
    return split_word_is(c + i, e - i, word);
}

// Whether the cleaned declaration c[0, n) names `word` before position `before` at depth 0.
static bool split_has_word(const char *c, const size_t before, const char *word)
{
    const size_t len = strlen(word);
    for (size_t i = 0; i + len <= before; i++) {
        if ((i == 0 || !split_ident_char(c[i - 1])) && memcmp(c + i, word, len) == 0
            && (i + len == before || !split_ident_char(c[i + len])))
            return true;
    }
    return false;
}

// Whether a declaration's specifiers, c[0, before), make it static: `static`, or the generated C's macros meaning it.
static bool split_is_static(const char *c, const size_t before)
{
    static const char *const words[] = {"static", "TIDE_HELPER", "TIDE_CODE", "TIDE_ROWS", "TIDE_SHARD_FN"};
    for (size_t k = 0; k < sizeof words / sizeof words[0]; k++) {
        if (split_has_word(c, before, words[k])) return true;
    }
    return false;
}

// A variable's declaration c[0, n), its = or ; at n: its name (the identifier before any [..] at the end) and
// whether the object is const (a const after its last *, or anywhere without one). False if it isn't one this
// understands.
static bool split_variable(const char *c, size_t n, size_t *name, size_t *name_len, bool *constant)
{
    for (;;) {
        while (n > 0 && (c[n - 1] == ' ' || c[n - 1] == '\t' || c[n - 1] == '\n' || c[n - 1] == '\r')) n--;
        if (n > 0 && c[n - 1] == ']') {
            while (n > 0 && c[n - 1] != '[') n--;
            if (n == 0) return false;
            n--;
            continue;
        }
        if (n > 0 && c[n - 1] == ')') { // Attributes after the name
            int depth = 0;
            size_t open = n;
            while (open > 0) {
                open--;
                if (c[open] == ')') depth++;
                else if (c[open] == '(' && --depth == 0) break;
            }
            size_t s = open;
            while (s > 0 && split_ident_char(c[s - 1])) s--;
            if (!split_word_is(c + s, open - s, "__attribute__")) return false; // A function pointer, or a call
            n = s;
            continue;
        }
        break;
    }
    size_t s = n;
    while (s > 0 && split_ident_char(c[s - 1])) s--;
    if (s == n || !split_ident_start(c[s])) return false;
    *name = s;
    *name_len = n - s;
    // Its specifiers and pointers, outside a struct defined in them and attributes' parentheses: the object is const
    // with a const after its last *
    *constant = false;
    for (size_t i = 0; i < s; i++) {
        if (c[i] == '{' || c[i] == '(') {
            const char open = c[i], close = open == '{' ? '}' : ')';
            int depth = 0;
            for (; i < s; i++) {
                if (c[i] == open) depth++;
                else if (c[i] == close && --depth == 0) break;
            }
            if (depth != 0) return false;
        } else if (c[i] == '*') {
            *constant = false;
        } else if ((i == 0 || !split_ident_char(c[i - 1])) && i + 5 <= s && memcmp(c + i, "const", 5) == 0
                   && !split_ident_char(c[i + 5])) {
            *constant = true;
        }
    }
    return true;
}

// Why the last split couldn't be made, and where: an offset in the text.
static const char *split_why;
static size_t split_where;

static bool split_fail(const char *why, const size_t where)
{
    split_why = why;
    split_where = where;
    return false;
}

// The items of `t`. False where it finds something it can't place.
static bool split_items(const char *t, const size_t n, split_item_list *out)
{
    bool tide = false; // The #line in force names a .tide file
    size_t i = 0;
    while (i < n) {
        while (i < n && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) i++;
        if (i >= n) break;
        const size_t start = i;
        if (t[i] == '#') {
            const size_t end = split_line_end(t, n, i);
            const int names = split_line_names_tide(t, i, end);
            if (names >= 0) tide = names == 1;
            split_item item = {.start = start, .end = end, .kind = SPLIT_BOTH, .tide_line = names == 1};
            if (end - i > 7 && memcmp(t + i, "#define", 7) == 0) { // And the macro's name
                size_t k = i + 7;
                while (k < end && (t[k] == ' ' || t[k] == '\t')) k++;
                item.name = k;
                while (k < end && split_ident_char(t[k])) k++;
                item.name_len = k - item.name;
                item.define = item.name_len > 0;
            }
            vec_push(*out, item);
            i = end;
            continue;
        }
        if (t[i] == '/' && i + 1 < n && t[i + 1] == '/') {
            while (i < n && t[i] != '\n') i++;
            vec_push(*out, ((split_item){.start = start, .end = i, .kind = SPLIT_BOTH}));
            continue;
        }
        if (t[i] == '/' && i + 1 < n && t[i + 1] == '*') {
            const char *close = strstr(t + i + 2, "*/");
            if (!close) return split_fail("a comment with no end", i);
            i = (size_t)(close - t) + 2;
            vec_push(*out, ((split_item){.start = start, .end = i, .kind = SPLIT_BOTH}));
            continue;
        }
        int depth = 0, parens = 0;
        size_t brace = 0, eq = 0; // Where its body starts, and a variable's initializer
        bool body_tide = false, done = false;
        while (i < n && !done) {
            const char ch = t[i];
            if (ch == '\n') {
                size_t k = i + 1;
                while (k < n && (t[k] == ' ' || t[k] == '\t')) k++;
                if (k < n && t[k] == '#') {
                    const size_t end = split_line_end(t, n, k);
                    if (split_line_names_tide(t, k, end) == 1) body_tide = true;
                    i = end;
                    continue;
                }
                i++;
            } else if (ch == '"' || ch == '\'') {
                i++;
                while (i < n && t[i] != ch) {
                    if (t[i] == '\n') return split_fail("a string across lines", i);
                    if (t[i] == '\\') i++;
                    i++;
                }
                i++;
            } else if (ch == '/' && i + 1 < n && t[i + 1] == '/') {
                while (i < n && t[i] != '\n') i++;
            } else if (ch == '/' && i + 1 < n && t[i + 1] == '*') {
                const char *close = strstr(t + i + 2, "*/");
                if (!close) return split_fail("a comment with no end", i);
                i = (size_t)(close - t) + 2;
            } else if (ch == '(') {
                parens++;
                i++;
            } else if (ch == ')') {
                parens--;
                i++;
            } else if (ch == '{') {
                if (depth == 0 && !brace) brace = i;
                depth++;
                i++;
            } else if (ch == '}') {
                depth--;
                i++;
                if (depth < 0) return split_fail("a } closing nothing", i);
                if (depth == 0 && parens == 0) {
                    const char *c = split_clean(t, start, brace);
                    size_t name, len;
                    if (!split_first_word_is(c, brace - start, "typedef") && split_function_head(c, brace - start, &name, &len)) {
                        vec_push(*out, ((split_item){.start = start, .end = i, .brace = brace, .name = start + name, .name_len = len,
                                                     .kind = SPLIT_FUNCTION, .code = tide || body_tide,
                                                     .is_static = split_is_static(c, name)}));
                        done = true;
                    }
                }
            } else if (ch == ';' && depth == 0 && parens == 0) {
                i++;
                const size_t end = i;
                // Its declaration, up to an initializer, which may be megabytes of a shader's code
                const size_t head = eq ? eq : end - 1;
                const char *c = split_clean(t, start, head);
                const size_t len = head - start;
                size_t name, name_len;
                if (split_first_word_is(c, len, "typedef") || split_first_word_is(c, len, "extern")
                    || split_first_word_is(c, len, "_Static_assert")) {
                    vec_push(*out, ((split_item){.start = start, .end = end, .kind = SPLIT_BOTH}));
                } else if (!eq && split_function_head(c, len, &name, &name_len)) {
                    vec_push(*out, ((split_item){.start = start, .end = end, .name = start + name, .name_len = name_len,
                                                 .kind = SPLIT_BOTH, .prototype = true}));
                } else {
                    // A struct, union or enum declared alone has no declarator: its last word follows the keyword
                    // or the braces
                    bool tag = false;
                    if (!eq) {
                        size_t e = len;
                        while (e > 0 && (c[e - 1] == ' ' || c[e - 1] == '\t' || c[e - 1] == '\n' || c[e - 1] == '\r')) e--;
                        if (e > 0 && c[e - 1] == '}') tag = true;
                        size_t s = e;
                        while (s > 0 && split_ident_char(c[s - 1])) s--;
                        size_t p = s;
                        while (p > 0 && (c[p - 1] == ' ' || c[p - 1] == '\t' || c[p - 1] == '\n')) p--;
                        size_t w = p;
                        while (w > 0 && split_ident_char(c[w - 1])) w--;
                        if (s < e && (split_word_is(c + w, p - w, "struct") || split_word_is(c + w, p - w, "union")
                                      || split_word_is(c + w, p - w, "enum")))
                            tag = true;
                    }
                    bool constant = false;
                    if (tag) {
                        vec_push(*out, ((split_item){.start = start, .end = end, .kind = SPLIT_BOTH}));
                    } else if (split_variable(c, len, &name, &name_len, &constant)) {
                        vec_push(*out, ((split_item){.start = start, .end = end, .init = head, .name = start + name,
                                                     .name_len = name_len, .kind = SPLIT_VARIABLE, .constant = constant,
                                                     .is_static = split_is_static(c, name)}));
                    } else {
                        return split_fail("a declaration of no variable this follows", start);
                    }
                }
                done = true;
            } else {
                if (ch == '=' && depth == 0 && parens == 0 && !eq) eq = i;
                i++;
            }
        }
        if (!done) return split_fail("a declaration with no end", start);
    }
    return true;
}

// Whether the name at t[at] follows `.` or `->` in t[from, at): a field's, or a designator's.
static bool split_after_member(const char *t, const size_t from, size_t at)
{
    while (at > from && (t[at - 1] == ' ' || t[at - 1] == '\t' || t[at - 1] == '\n')) at--;
    return at > from && (t[at - 1] == '.' || (at > from + 1 && t[at - 1] == '>' && t[at - 2] == '-'));
}

// Marks the names of `names` that t[from, to) uses, as the code's file's (`code`) or the plumbing's.
static void split_uses(const char *t, const size_t from, const size_t to, split_names *names, const bool code)
{
    for (size_t i = from; i < to;) {
        const char ch = t[i];
        if (ch == '"' || ch == '\'') {
            i++;
            while (i < to && t[i] != ch) {
                if (t[i] == '\\') i++;
                i++;
            }
            i++;
        } else if (ch == '/' && i + 1 < to && t[i + 1] == '/') {
            while (i < to && t[i] != '\n') i++;
        } else if (ch == '/' && i + 1 < to && t[i + 1] == '*') {
            const char *close = strstr(t + i + 2, "*/");
            i = close ? (size_t)(close - t) + 2 : to;
        } else if (ch == '#' && (i == 0 || t[i - 1] == '\n') && !(to - i > 7 && memcmp(t + i, "#define", 7) == 0)) {
            i = split_line_end(t, to, i); // #line's file names no function
        } else if (split_ident_start(ch) && (i == 0 || !split_ident_char(t[i - 1]))) {
            size_t e = i;
            while (e < to && split_ident_char(t[e])) e++;
            split_name *found = split_find(names, t + i, e - i);
            if (found && split_after_member(t, from, i)) found->field = true; // A field's name, no use of it
            else if (found && code) found->by_code = true;
            else if (found) found->by_plumbing = true;
            i = e;
        } else if (ch >= '0' && ch <= '9') {
            while (i < to && split_ident_char(t[i])) i++;
        } else {
            i++;
        }
    }
}

// Where a split writes: a builder, or with none, a count of what it would write, so the builder can be made the size
// it takes. The arena never frees, and a big game's generated C is a hundred megabytes, which growing by doubling
// would take twice over.
typedef struct split_out {
    sb *b;
    size_t n;
    const split_names *names; // Whose renamed names it writes renamed
} split_out;

static void split_write(split_out *o, const char *s, const size_t n)
{
    if (o->b) sb_putn(o->b, s, n);
    else o->n += n;
}

// Writes s[0, n), its renamed names as tide_split_<name>, but in strings, comments and directives other than
// #define. A name that is also a field's isn't renamed (split_plumbing). Pieces of items start outside a string or
// comment, at a line's start when at a directive.
static void split_putn(split_out *o, const char *s, const size_t n)
{
    if (!o->names || !o->names->renames) {
        split_write(o, s, n);
        return;
    }
    size_t at = 0; // Written up to here
    for (size_t i = 0; i < n;) {
        const char ch = s[i];
        if (ch == '"' || ch == '\'') {
            i++;
            while (i < n && s[i] != ch) {
                if (s[i] == '\\') i++;
                i++;
            }
            i++;
        } else if (ch == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') i++;
        } else if (ch == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i += 2;
        } else if (ch == '#' && (i == 0 || s[i - 1] == '\n') && !(n - i > 7 && memcmp(s + i, "#define", 7) == 0)) {
            i = split_line_end(s, n, i);
        } else if (split_ident_start(ch) && (i == 0 || !split_ident_char(s[i - 1]))) {
            size_t e = i;
            while (e < n && split_ident_char(s[e])) e++;
            const split_name *found = split_find(o->names, s + i, e - i);
            if (found && found->renamed) {
                split_write(o, s + at, i - at);
                split_write(o, "tide_split_", 11);
                at = i;
            }
            i = e;
        } else if (ch >= '0' && ch <= '9') {
            while (i < n && split_ident_char(s[i])) i++;
        } else {
            i++;
        }
    }
    split_write(o, s + at, n - at);
}

static void split_put(split_out *o, const char *s)
{
    split_putn(o, s, strlen(s));
}

// Writes item [from, to) with its linkage made external, as `name` is the other file's too: `static` (or the
// macro meaning it) in its specifiers, before its name at `name_at`, becomes TIDE_SPLIT.
static void split_put_external(split_out *o, const char *t, const size_t from, const size_t to, const size_t name_at)
{
    static const struct {
        const char *word, *as;
    } swaps[] = {{"static", "TIDE_SPLIT"},
                 {"TIDE_CODE", "TIDE_SPLIT"},
                 {"TIDE_HELPER", "TIDE_SPLIT_HELPER"},
                 {"TIDE_SHARD_FN", "TIDE_SPLIT"},
                 {"TIDE_ROWS", "TIDE_SPLIT __attribute__((noinline))"},
                 {"inline", ""}};
    const char *c = split_clean(t, from, name_at);
    size_t at = from;
    for (size_t i = 0; i < name_at - from;) {
        if (!split_ident_start(c[i]) || (i > 0 && split_ident_char(c[i - 1]))) {
            i++;
            continue;
        }
        size_t e = i;
        while (e < name_at - from && split_ident_char(c[e])) e++;
        for (size_t k = 0; k < sizeof swaps / sizeof swaps[0]; k++) {
            if (!split_word_is(c + i, e - i, swaps[k].word)) continue;
            split_putn(o, t + at, from + i - at);
            split_put(o, swaps[k].as);
            at = from + e;
            break;
        }
        i = e;
    }
    split_putn(o, t + at, to - at);
}

// A variable's declaration for the file that doesn't define it: `extern`, without `static` or its initializer.
static void split_put_extern(split_out *o, const char *t, const split_item *it)
{
    split_put(o, it->is_static ? "extern TIDE_SPLIT " : "extern ");
    const char *c = split_clean(t, it->start, it->name);
    size_t at = it->start;
    for (size_t i = 0; i + 6 <= it->name - it->start; i++) {
        if ((i == 0 || !split_ident_char(c[i - 1])) && memcmp(c + i, "static", 6) == 0 && !split_ident_char(c[i + 6])) {
            split_putn(o, t + at, it->start + i - at);
            at = it->start + i + 6;
            break;
        }
    }
    size_t end = it->init;
    while (end > at && (t[end - 1] == ' ' || t[end - 1] == '\t' || t[end - 1] == '\n')) end--;
    split_putn(o, t + at, end - at);
    split_put(o, ";");
}

// Writes the conditional directives (#if, #else, #endif...) at the starts of the lines of t[from, to): what a file
// keeps of an item it leaves out, as an #endif in a function's head may close an #if before it.
static void split_put_conditionals(split_out *o, const char *t, const size_t from, const size_t to)
{
    for (size_t i = from; i < to; i++) {
        if (i != from && t[i - 1] != '\n') continue;
        size_t k = i;
        while (k < to && (t[k] == ' ' || t[k] == '\t')) k++;
        if (k >= to || t[k] != '#') continue;
        const size_t end = split_line_end(t, to, k);
        size_t w = k + 1;
        while (w < end && (t[w] == ' ' || t[w] == '\t')) w++;
        if (end - w >= 2 && (memcmp(t + w, "if", 2) == 0 || memcmp(t + w, "el", 2) == 0 || memcmp(t + w, "endif", 5) == 0)) {
            split_put(o, "\n");
            split_putn(o, t + k, end - k);
            split_put(o, "\n");
        }
        i = end;
    }
}

// Larger static consts than this are the plumbing's alone (shaders' code, tables of rows), not copied.
#define SPLIT_SHARED_CONST 16384

// split_plumbing's `partition` that cuts out the generated C's data: its large constants (shaders' code, tables) go
// to the second file, everything else to the first. Most of a big game's C is that data, which an edit of its code
// doesn't change: the parts of the code are then cut from what's left, a fraction of the text.
#define SPLIT_DATA (-2)

// The most parts the code is cut into (codegen_options.split_code_parts)
#define SPLIT_MOST_PARTS 16

static const char split_macros[] =
    "// This build's generated C is in two files, the game's code and its plumbing; what one names of the other's\n"
    "// is linked by name, inside the game\n"
    "#if defined(_WIN32)\n#define TIDE_SPLIT __attribute__((unused))\n#else\n"
    "#define TIDE_SPLIT __attribute__((unused, visibility(\"hidden\")))\n#endif\n"
    "#ifdef TIDE_DEBUG_PLUMBING\n#define TIDE_SPLIT_HELPER TIDE_SPLIT\n#else\n"
    "#define TIDE_SPLIT_HELPER TIDE_SPLIT __attribute__((nodebug))\n#endif\n\n";

// Where a variable is defined: the plumbing's file, the code's alone (a static one only it names), or both (a
// small static const both name).
typedef enum split_place { SPLIT_IN_PLUMBING, SPLIT_IN_CODE, SPLIT_IN_BOTH } split_place;

static split_place split_place_of(const split_name *e, const bool is_static)
{
    if (e->data) return SPLIT_IN_PLUMBING;
    if (e->code || (is_static && e->by_code && !e->by_plumbing)) return SPLIT_IN_CODE;
    if (e->shared && e->by_code && e->by_plumbing) return SPLIT_IN_BOTH;
    return SPLIT_IN_PLUMBING;
}

static size_t split_count_uses(const split_names *names)
{
    size_t n = 0;
    for (size_t k = 0; k < names->cap; k++) n += (size_t)names->slots[k].by_code + names->slots[k].by_plumbing;
    return n;
}

// Writes the two files of the text `t`, `n` long, whose items and names are known.
static void split_emit(const char *t, const size_t n, const split_item_list *items, const split_names *names,
                       split_out *code, split_out *plumbing)
{
    for (size_t k = 0; k < names->cap; k++) names->slots[k].declared = false;
    split_put(code, split_macros);
    split_put(plumbing, split_macros);
    size_t at = 0;
    for (int i = 0; i < items->count; i++) {
        const split_item *it = &items->items[i];
        // What's between items (blank lines) goes to both, so their lines stay apart
        split_putn(code, t + at, it->start - at);
        split_putn(plumbing, t + at, it->start - at);
        at = it->end;
        split_name *e = it->kind == SPLIT_BOTH && !it->prototype ? NULL : split_find(names, t + it->name, it->name_len);
        if (it->kind == SPLIT_BOTH && !(e && e->function)) {
            split_putn(code, t + it->start, it->end - it->start);
            if (!it->tide_line) split_putn(plumbing, t + it->start, it->end - it->start);
        } else if (it->kind == SPLIT_BOTH) { // A prototype of a function defined here
            const bool external = e->code ? e->by_plumbing : e->by_code;
            for (int k = 0; k < 2; k++) {
                split_out *o = k ? plumbing : code;
                const bool own = (k == 0) == e->code;
                if (external) split_put_external(o, t, it->start, it->end, it->name);
                else if (own) split_putn(o, t + it->start, it->end - it->start);
                else split_put_conditionals(o, t, it->start, it->end); // A file that never names it doesn't declare it
            }
        } else if (it->kind == SPLIT_FUNCTION) {
            const bool external = e->code ? e->by_plumbing : e->by_code;
            split_out *own = e->code ? code : plumbing, *other = e->code ? plumbing : code;
            if (external) {
                split_put_external(own, t, it->start, it->end, it->name);
                size_t head = it->brace;
                while (head > it->start && (t[head - 1] == ' ' || t[head - 1] == '\t' || t[head - 1] == '\n')) head--;
                split_put_external(other, t, it->start, head, it->name);
                split_put(other, "\n;"); // A trailing // comment in the signature must end first.
            } else {
                split_putn(own, t + it->start, it->end - it->start);
                split_put_conditionals(other, t, it->start, it->brace);
            }
        } else {
            const split_place place = split_place_of(e, it->is_static);
            if (place == SPLIT_IN_BOTH) {
                // Dependency propagation is conservative: a shared initializer can move after it has
                // already retained a constant here. Keep those harmless copies warning-clean.
                split_put(code, "__attribute__((unused)) ");
                split_put(plumbing, "__attribute__((unused)) ");
                split_putn(code, t + it->start, it->end - it->start);
                split_putn(plumbing, t + it->start, it->end - it->start);
            } else if (place == SPLIT_IN_CODE) {
                if (e->shared) split_put(code, "__attribute__((unused)) ");
                if (e->by_plumbing && it->is_static) split_put_external(code, t, it->start, it->end, it->name);
                else split_putn(code, t + it->start, it->end - it->start);
                if (e->by_plumbing && !e->declared) {
                    split_put_extern(plumbing, t, it);
                    e->declared = true;
                } else {
                    split_put_conditionals(plumbing, t, it->start, it->init);
                }
            } else {
                if (e->shared) split_put(plumbing, "__attribute__((unused)) ");
                if (e->by_code && it->is_static) split_put_external(plumbing, t, it->start, it->end, it->name);
                else split_putn(plumbing, t + it->start, it->end - it->start);
                if (e->by_code && !e->declared) {
                    split_put_extern(code, t, it);
                    e->declared = true;
                } else {
                    split_put_conditionals(code, t, it->start, it->init);
                }
            }
        }
    }
    split_putn(code, t + at, n - at);
    split_putn(plumbing, t + at, n - at);
}

// Splits `c` into `code` and `plumbing`, with the variables starting at the `code_count` offsets `code_at` the
// code's (`partition` -1); or by bit `partition` of each name's hash (0 and up), or with only the large constants
// in `plumbing` (SPLIT_DATA). False, writing neither, where it can't.
static bool split_plumbing(const sb *c, const size_t *code_at, const int code_count, sb *code, sb *plumbing, const int partition)
{
    const char *t = c->data ? c->data : "";
    split_item_list items = {0};
    split_why = NULL;
    if (!split_items(t, c->len, &items)) return false;
    if (partition != -1) {
        for (int i = 0; i < items.count; i++) {
            split_item *it = &items.items[i];
            if (it->kind == SPLIT_FUNCTION)
                it->code = partition == SPLIT_DATA || ((split_hash(t + it->name, it->name_len) >> partition) & 1) == 0;
        }
    }
    split_names names = {0};
    for (int i = 0; i < items.count; i++) {
        const split_item *it = &items.items[i];
        if (it->define) split_add(&names, t + it->name, it->name_len);
        if (it->kind == SPLIT_BOTH) continue;
        split_name *e = split_add(&names, t + it->name, it->name_len);
        if (it->kind == SPLIT_FUNCTION) {
            if (e->variable) return split_fail("a function with a variable's name", it->start);
            if (e->function && e->code != it->code)
                return split_fail("a function defined as the game's code and as plumbing", it->start);
            e->function = true;
            e->code = it->code;
            e->is_static = e->is_static || it->is_static;
        } else {
            if (e->function) return split_fail("a variable with a function's name", it->start);
            const bool shared = it->is_static && it->constant && it->end - it->start <= SPLIT_SHARED_CONST;
            e->shared = e->variable ? e->shared && shared : shared;
            e->variable = true;
            e->is_static = e->is_static || it->is_static;
            for (int k = 0; k < code_count; k++) e->code = e->code || code_at[k] == it->start;
            if (partition == SPLIT_DATA && !shared) {
                e->data = !split_word_is(t + it->name, it->name_len, "tide_game_api") && it->constant
                       && it->end - it->start > SPLIT_SHARED_CONST;
                e->code = !e->data;
            } else if (partition >= 0 && !shared)
                e->code = split_word_is(t + it->name, it->name_len, "tide_game_api")
                       || ((split_hash(t + it->name, it->name_len) >> partition) & 1) == 0;
        }
    }
    // What each file names: a function's body is its file's, and a variable's initializer and a macro's body are
    // the files' that define the variable or use the macro, which depends on who names them. Names only gain
    // users, so this ends.
    for (int i = 0; i < items.count; i++) {
        const split_item *it = &items.items[i];
        if (it->kind == SPLIT_FUNCTION) split_uses(t, it->start, it->end, &names, it->code);
    }
    for (size_t before = 0, after = 1; after != before;) {
        before = split_count_uses(&names);
        for (int i = 0; i < items.count; i++) {
            const split_item *it = &items.items[i];
            if (it->define) {
                const split_name *e = split_find(&names, t + it->name, it->name_len);
                const bool both = e->function || e->variable; // A macro named as something else: anyone's
                if (both || e->by_code) split_uses(t, it->name + it->name_len, it->end, &names, true);
                if (both || e->by_plumbing) split_uses(t, it->name + it->name_len, it->end, &names, false);
                continue;
            }
            if (it->kind != SPLIT_VARIABLE) continue;
            const split_place place = split_place_of(split_find(&names, t + it->name, it->name_len), it->is_static);
            if (place != SPLIT_IN_CODE) split_uses(t, it->init, it->end, &names, false);
            if (place != SPLIT_IN_PLUMBING) split_uses(t, it->init, it->end, &names, true);
        }
        after = split_count_uses(&names);
    }
    // The plumbing's static names the code's file names, which become external (split_emit)
    for (size_t k = 0; k < names.cap; k++) {
        split_name *e = &names.slots[k];
        if (partition != -1) {
            if (!e->name || !e->is_static || e->field || (e->code ? !e->by_plumbing : !e->by_code)) continue;
            if (e->variable && split_place_of(e, true) == SPLIT_IN_BOTH) continue;
            e->renamed = true;
            names.renames = true;
            continue;
        }
        if (!e->name || e->code || !e->is_static || !e->by_code || e->field) continue;
        if (e->variable) {
            // A small static const both files have stays each one's own
            if (split_place_of(e, e->is_static) != SPLIT_IN_PLUMBING) continue;
        } else if (!e->function) {
            continue;
        }
        e->renamed = true;
        names.renames = true;
    }
    split_out count_code = {.names = &names}, count_plumbing = {.names = &names};
    split_emit(t, c->len, &items, &names, &count_code, &count_plumbing);
    *code = (sb){.data = arena_alloc(count_code.n + 1), .cap = count_code.n + 1, .line = 1};
    *plumbing = (sb){.data = arena_alloc(count_plumbing.n + 1), .cap = count_plumbing.n + 1, .line = 1};
    split_out to_code = {code, 0, &names}, to_plumbing = {plumbing, 0, &names};
    split_emit(t, c->len, &items, &names, &to_code, &to_plumbing);
    return true;
}

// ---------------------------------------------------------------------------
// What a program uses of its generated C: build code's program (build_code.c) runs its main alone, which calls the
// roots' code and loads their assets, while the text has the whole game. split_prune keeps the functions and
// variables `root` reaches, through function bodies, variables' initializers and macros' bodies, and everything
// that defines no function or variable (types, prototypes, directives), so what's kept compiles as it did. A name
// counts wherever it's written outside strings, comments and #line, but after `.` or `->` (a field's).

typedef VEC(split_name *) split_reach;

static void split_prune_uses(const char *t, const size_t from, const size_t to, split_names *names, split_reach *more)
{
    for (size_t i = from; i < to;) {
        const char ch = t[i];
        if (ch == '"' || ch == '\'') {
            i++;
            while (i < to && t[i] != ch) {
                if (t[i] == '\\') i++;
                i++;
            }
            i++;
        } else if (ch == '/' && i + 1 < to && t[i + 1] == '/') {
            while (i < to && t[i] != '\n') i++;
        } else if (ch == '/' && i + 1 < to && t[i + 1] == '*') {
            const char *close = strstr(t + i + 2, "*/");
            i = close ? (size_t)(close - t) + 2 : to;
        } else if (ch == '#' && (i == 0 || t[i - 1] == '\n') && !(to - i > 7 && memcmp(t + i, "#define", 7) == 0)) {
            i = split_line_end(t, to, i);
        } else if (split_ident_start(ch) && (i == 0 || !split_ident_char(t[i - 1]))) {
            size_t e = i;
            while (e < to && split_ident_char(t[e])) e++;
            split_name *found = split_find(names, t + i, e - i);
            if (found && !found->reached && !split_after_member(t, from, i)) {
                found->reached = true;
                vec_push(*more, found);
            }
            i = e;
        } else if (ch >= '0' && ch <= '9') {
            while (i < to && split_ident_char(t[i])) i++;
        } else {
            i++;
        }
    }
}

// `c` with only what `root` reaches, into `out`. False, writing nothing, where it can't be cut.
static bool split_prune(const sb *c, const char *root, sb *out)
{
    const char *t = c->data ? c->data : "";
    split_item_list items = {0};
    split_why = NULL;
    if (!split_items(t, c->len, &items)) return false;
    // Each name's definitions: functions, variables and macros, in a list through `next`
    split_names names = {0};
    int *next = arena_alloc(sizeof(int) * (size_t)(items.count ? items.count : 1));
    for (int i = items.count - 1; i >= 0; i--) {
        const split_item *it = &items.items[i];
        if (it->kind == SPLIT_BOTH && !it->define) continue;
        split_name *e = split_add(&names, t + it->name, it->name_len);
        next[i] = e->first_item;
        e->first_item = i + 1;
    }
    split_reach more = {0};
    split_name *start = split_find(&names, root, strlen(root));
    if (!start) return split_fail("no function to start from", 0);
    start->reached = true;
    vec_push(more, start);
    while (more.count) {
        const split_name *e = more.items[--more.count];
        for (int i = e->first_item; i; i = next[i - 1]) {
            const split_item *it = &items.items[i - 1];
            const size_t from = it->kind == SPLIT_FUNCTION ? it->start : it->define ? it->name + it->name_len : it->start;
            split_prune_uses(t, from, it->end, &names, &more);
        }
    }
    // What's kept, in its order; what isn't keeps its conditional directives, which may open or close around it
    size_t size = 0, at = 0;
    for (int pass = 0; pass < 2; pass++) {
        split_out o = {pass ? out : NULL, 0, NULL};
        if (pass) *out = (sb){.data = arena_alloc(size + 1), .cap = size + 1, .line = 1};
        at = 0;
        for (int i = 0; i < items.count; i++) {
            const split_item *it = &items.items[i];
            split_write(&o, t + at, it->start - at);
            at = it->end;
            const split_name *e = it->kind == SPLIT_BOTH && !it->define ? NULL : split_find(&names, t + it->name, it->name_len);
            if (!e || e->reached || it->define) split_write(&o, t + it->start, it->end - it->start);
            else split_put_conditionals(&o, t, it->start, it->end);
        }
        split_write(&o, t + at, c->len - at);
        size = o.n;
    }
    return true;
}
