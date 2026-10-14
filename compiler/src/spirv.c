// WGSL to SPIR-V (see spirv.h): the text is read into a tree, which is then
// written out as SPIR-V, its types worked out on the way, as a WGSL compiler
// would. It takes the WGSL that WebGPU takes, as far as frags, verts and the
// renderer's shaders go: no storage buffers, atomics, workgroups or compute.
//
// Everything a translation makes comes from one arena of its own, freed as it
// ends, but the words it hands back.

#include "spirv.h"

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Memory

typedef struct block {
    struct block *next;
    size_t used, size;
    _Alignas(16) unsigned char data[];
} block;

typedef struct arena {
    block *first;
} arena;

// The state of one translation: what it reads, where it failed, what it made.
typedef struct translation translation;
static void fail(translation *x, int line, int col, const char *format, ...);

static void *grab(arena *a, const size_t size)
{
    const size_t rounded = (size + 15u) & ~(size_t)15u;
    if (!a->first || a->first->size - a->first->used < rounded) {
        const size_t room = rounded > 65536u ? rounded : 65536u;
        block *b = malloc(sizeof(block) + room);
        if (!b) abort();
        b->next = a->first;
        b->used = 0;
        b->size = room;
        a->first = b;
    }
    void *at = a->first->data + a->first->used;
    a->first->used += rounded;
    memset(at, 0, rounded);
    return at;
}

static void release(arena *a)
{
    for (block *b = a->first; b;) {
        block *next = b->next;
        free(b);
        b = next;
    }
    a->first = NULL;
}

// A growable array of words.
typedef struct words {
    uint32_t *w;
    size_t n, cap;
} words;

static void put(words *b, const uint32_t word)
{
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2u : 256u;
        b->w = realloc(b->w, b->cap * sizeof(uint32_t));
        if (!b->w) abort();
    }
    b->w[b->n++] = word;
}

static void put_all(words *to, const words *from)
{
    for (size_t i = 0; i < from->n; i++) put(to, from->w[i]);
}

// ---------------------------------------------------------------------------
// Tokens

typedef enum tok_kind { T_END, T_NAME, T_INT, T_FLOAT, T_PUNCT } tok_kind;

typedef enum punct {
    P_NONE,
    P_LBRACE, P_RBRACE, P_LPAREN, P_RPAREN, P_LBRACKET, P_RBRACKET,
    P_COMMA, P_SEMI, P_COLON, P_DOT, P_AT, P_ARROW,
    P_ASSIGN, P_EQ, P_NE, P_LT, P_LE, P_GT, P_GE,
    P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PERCENT, P_AMP, P_PIPE, P_CARET, P_TILDE, P_BANG,
    P_ANDAND, P_OROR, P_SHL, P_SHR,
    P_PLUSEQ, P_MINUSEQ, P_STAREQ, P_SLASHEQ, P_PERCENTEQ, P_AMPEQ, P_PIPEEQ, P_CARETEQ, P_SHLEQ, P_SHREQ,
    P_INC, P_DEC,
} punct;

typedef struct token {
    tok_kind kind;
    punct p;
    const char *at;
    int len;
    int line, col;
    char suffix; // A number's: 'i', 'u', 'f', 'h', or 0 for an abstract one
} token;

static const struct {
    const char *text;
    punct p;
} puncts[] = { // Longest first
    {">>=", P_SHREQ}, {"<<=", P_SHLEQ},
    {"->", P_ARROW}, {"==", P_EQ}, {"!=", P_NE}, {"<=", P_LE}, {">=", P_GE}, {"&&", P_ANDAND}, {"||", P_OROR},
    {"<<", P_SHL}, {">>", P_SHR}, {"+=", P_PLUSEQ}, {"-=", P_MINUSEQ}, {"*=", P_STAREQ}, {"/=", P_SLASHEQ},
    {"%=", P_PERCENTEQ}, {"&=", P_AMPEQ}, {"|=", P_PIPEEQ}, {"^=", P_CARETEQ}, {"++", P_INC}, {"--", P_DEC},
    {"{", P_LBRACE}, {"}", P_RBRACE}, {"(", P_LPAREN}, {")", P_RPAREN}, {"[", P_LBRACKET}, {"]", P_RBRACKET},
    {",", P_COMMA}, {";", P_SEMI}, {":", P_COLON}, {".", P_DOT}, {"@", P_AT}, {"=", P_ASSIGN}, {"<", P_LT},
    {">", P_GT}, {"+", P_PLUS}, {"-", P_MINUS}, {"*", P_STAR}, {"/", P_SLASH}, {"%", P_PERCENT}, {"&", P_AMP},
    {"|", P_PIPE}, {"^", P_CARET}, {"~", P_TILDE}, {"!", P_BANG},
};

// ---------------------------------------------------------------------------
// The tree

// A type as it's written: a name and what's between its angle brackets,
// types or numbers (an array's count, a pointer's address space).
typedef struct tnode {
    const char *name;
    int len;
    int line, col;
    struct tnode *args[3];
    int nargs;
    int64_t number; // For a number among a template's arguments: name is NULL
} tnode;

typedef enum xkind {
    X_INT, X_FLOAT, X_BOOL, X_NAME, X_CALL, X_MEMBER, X_INDEX, X_UNARY, X_BINARY, X_DEREF, X_ADDR,
} xkind;

typedef struct expr {
    xkind kind;
    int line, col;
    punct op;
    struct expr *a, *b;
    const char *name; // A name's, a member's, a call's
    int len;
    tnode *type;      // A call's, with its template: bitcast<f32>(...), array<f32, 3>(...)
    struct expr **args;
    int nargs;
    int64_t i;        // An int's, or a bool's
    double f;         // A float's
    char suffix;
} expr;

typedef enum skind {
    S_VAR, S_LET, S_CONST, S_ASSIGN, S_INC, S_DEC, S_CALL, S_IF, S_FOR, S_WHILE, S_LOOP, S_SWITCH,
    S_BREAK, S_BREAK_IF, S_CONTINUE, S_RETURN, S_DISCARD, S_BLOCK, S_PHONY,
} skind;

typedef struct scase scase;

typedef struct stmt {
    skind kind;
    int line, col;
    const char *name;
    int len;
    tnode *type;
    expr *e, *e2;       // var: its value; assignment: the place, then the value
    punct op;           // An assignment's: P_ASSIGN, or P_PLUSEQ...
    struct stmt *body, *other, *init, *update, *continuing;
    struct stmt **list; // A block's
    int count;
    scase *cases;
    int ncases;
} stmt;

struct scase {
    expr **selectors;
    int count;
    bool fallback; // `default` is among them
    stmt *body;
};

// What attributes say of a variable, a member, a parameter or a result.
typedef struct attrs {
    int location;  // -1 for none
    int builtin;   // A SPIR-V built-in, or -1
    bool flat;
    int group, binding; // -1 for none
    int stage;     // A function's: 0, or 1 for @vertex and 2 for @fragment
} attrs;

typedef struct member {
    const char *name;
    int len;
    tnode *type;
    attrs at;
} member;

typedef struct sdecl {
    const char *name;
    int len;
    member *members;
    int count;
    int type; // Its type, once it's resolved: 0 until then
    int *member_types;
    uint32_t *offsets; // In a uniform block, where each member starts
    uint32_t align, size;
    bool resolving;
} sdecl;

typedef enum space { SPACE_FUNCTION, SPACE_PRIVATE, SPACE_UNIFORM, SPACE_HANDLE } space;

typedef struct gdecl {
    const char *name;
    int len;
    int line, col;
    space space;
    tnode *type;
    expr *init;
    attrs at;
    bool is_const;
} gdecl;

typedef struct param {
    const char *name;
    int len;
    tnode *type;
    attrs at;
} param;

typedef struct fdecl {
    const char *name;
    int len;
    int line, col;
    param *params;
    int count;
    tnode *result;
    attrs result_at;
    stmt *body;
    attrs at;
    uint32_t id, type_id; // The SPIR-V function and its type
    int result_type;
    int *param_types;
    bool typed;
} fdecl;

// ---------------------------------------------------------------------------
// Types

typedef enum kind {
    K_NONE, K_VOID, K_BOOL, K_I32, K_U32, K_F32, K_AINT, K_AFLOAT, K_VEC, K_MAT, K_ARRAY, K_STRUCT, K_PTR,
    K_SAMPLER, K_SAMPLER_CMP, K_TEXTURE,
} kind;

typedef enum dim { DIM_2D, DIM_CUBE } dim;

typedef struct type {
    kind kind;
    int elem;  // A vector's, a matrix's column's (a vector), an array's, a pointer's, a texture's sampled scalar
    int n;     // A vector's size, a matrix's columns, an array's count
    int sc;    // A pointer's storage class
    sdecl *s;  // A struct's
    dim dim;   // A texture's
    bool depth, multisampled;
    uint32_t id, laid; // Its SPIR-V type, and the one laid out for a uniform block (structs and arrays)
} type;

// ---------------------------------------------------------------------------
// A value as an expression gives it

typedef struct val {
    int t;          // Its type, or for a reference what's stored there
    uint32_t id;    // The SPIR-V value, or for a reference its pointer
    bool ref;       // A place: `id` points at it, in storage class `sc`
    int sc;
    bool constant;  // An abstract number not made yet (`id` is 0): its value is `i` or `f`
    int64_t i;
    double f;
    const char *text; // An abstract float straight from its literal, to read as an f32 exactly
    int len;
    bool negated;     // ...with a minus before it
    bool is_const_id; // `id` is a constant's
} val;

// A name in scope: a value, a place, or a constant.
typedef struct binding {
    const char *name;
    int len;
    val v;
} binding;

// A loop or a switch that `break` and `continue` reach.
typedef struct target {
    bool loop;
    uint32_t merge, cont;
    bool broken; // A break went to its merge, which is reached that way
} target;

// The function being written.
typedef struct fn_state {
    fdecl *f;
    words vars; // Its variables, which SPIR-V wants in its first block
    words body;
    uint32_t label; // The block being written
    bool ended;     // ...has its branch: what's written next is dead
    target targets[64];
    int depth;
    binding *scope;
    int scope_count, scope_cap;
    // An entry point's outputs: where `return` stores what it returns
    uint32_t outputs[16];
    int output_types[16];
    int output_count;
    bool whole_output; // A single value, not a struct's members
    uint32_t inputs[32];
    int input_count;
    bool uses_frag_depth;
} fn_state;

struct translation {
    arena a;
    const char *text;
    token *toks;
    int ntok, pos;
    jmp_buf failed;
    char *error;
    size_t error_size;

    sdecl **structs;
    int nstructs;
    gdecl **globals;
    int nglobals;
    fdecl **fns;
    int nfns;

    type *types;
    int ntypes, cap_types;

    uint32_t next_id;
    words caps, imports, memory, entries, modes, decorations, globals_words, functions;
    uint32_t glsl; // GLSL.std.450's id
    bool cap_image_query, cap_derivative_control;
    // Constants already made, by their type and bits
    struct {
        uint32_t type, bits, id;
        int op;
    } *consts;
    int nconsts, cap_consts;
    // Pointer and function types already made
    struct {
        uint32_t a, b, id;
    } *pointers;
    int npointers, cap_pointers;
    struct {
        uint32_t *operands;
        int n;
        uint32_t id;
    } *functypes;
    int nfunctypes, cap_functypes;
    uint32_t sampled_images[8][2]; // By the image type: the sampled image type
    int nsampled;

    binding *globals_scope;
    int nglobal_bindings, cap_global_bindings;
    fn_state *fn;
};

static void fail(translation *x, const int line, const int col, const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (x->error && x->error_size) snprintf(x->error, x->error_size, "%d:%d: %s", line, col, text);
    longjmp(x->failed, 1);
}

static bool same(const char *a, const int len, const char *b)
{
    return (int)strlen(b) == len && memcmp(a, b, (size_t)len) == 0;
}

// ---------------------------------------------------------------------------
// Reading the text into tokens

static bool name_char(const char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool digit(const char c)
{
    return c >= '0' && c <= '9';
}

static bool hex_digit(const char c)
{
    return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static void tokenize(translation *x)
{
    int cap = 1024, line = 1;
    const char *line_start = x->text;
    x->toks = malloc((size_t)cap * sizeof(token));
    if (!x->toks) abort();
    for (const char *p = x->text;;) {
        // Space and comments
        while (*p) {
            if (*p == '\n') {
                line++;
                line_start = ++p;
            } else if (*p == ' ' || *p == '\t' || *p == '\r') {
                p++;
            } else if (p[0] == '/' && p[1] == '/') {
                while (*p && *p != '\n') p++;
            } else if (p[0] == '/' && p[1] == '*') {
                int nested = 1;
                p += 2;
                while (*p && nested) {
                    if (p[0] == '/' && p[1] == '*') nested++, p += 2;
                    else if (p[0] == '*' && p[1] == '/') nested--, p += 2;
                    else if (*p == '\n') line++, line_start = ++p;
                    else p++;
                }
            } else {
                break;
            }
        }
        if (x->ntok + 1 >= cap) {
            cap *= 2;
            x->toks = realloc(x->toks, (size_t)cap * sizeof(token));
            if (!x->toks) abort();
        }
        token *t = &x->toks[x->ntok++];
        *t = (token){.at = p, .line = line, .col = (int)(p - line_start) + 1};
        if (!*p) {
            t->kind = T_END;
            return;
        }
        if (digit(*p) || (*p == '.' && digit(p[1]))) {
            const char *start = p;
            bool is_float = false;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
                while (hex_digit(*p)) p++;
                if (*p == '.') is_float = true, p++;
                while (hex_digit(*p)) p++;
                if (*p == 'p' || *p == 'P') {
                    is_float = true;
                    p++;
                    if (*p == '+' || *p == '-') p++;
                    while (digit(*p)) p++;
                }
            } else {
                while (digit(*p)) p++;
                if (*p == '.') is_float = true, p++;
                while (digit(*p)) p++;
                if ((*p == 'e' || *p == 'E') && (digit(p[1]) || ((p[1] == '+' || p[1] == '-') && digit(p[2])))) {
                    is_float = true;
                    p++;
                    if (*p == '+' || *p == '-') p++;
                    while (digit(*p)) p++;
                }
            }
            t->len = (int)(p - start);
            if (*p == 'f' || *p == 'h') is_float = true, t->suffix = *p++;
            else if (!is_float && (*p == 'i' || *p == 'u')) t->suffix = *p++;
            t->kind = is_float ? T_FLOAT : T_INT;
            if (name_char(*p)) fail(x, t->line, t->col, "a number runs into a name");
            continue;
        }
        if (name_char(*p)) {
            const char *start = p;
            while (name_char(*p)) p++;
            t->kind = T_NAME;
            t->len = (int)(p - start);
            continue;
        }
        bool found = false;
        for (size_t i = 0; i < sizeof puncts / sizeof puncts[0]; i++) {
            const size_t n = strlen(puncts[i].text);
            if (strncmp(p, puncts[i].text, n) == 0) {
                t->kind = T_PUNCT;
                t->p = puncts[i].p;
                t->len = (int)n;
                p += n;
                found = true;
                break;
            }
        }
        if (!found) fail(x, t->line, t->col, "'%c' isn't WGSL", *p);
    }
}

// ---------------------------------------------------------------------------
// Parsing

static token *peek(translation *x)
{
    return &x->toks[x->pos];
}

static token *peek_at(translation *x, const int ahead)
{
    const int at = x->pos + ahead;
    return &x->toks[at < x->ntok ? at : x->ntok - 1];
}

static token *next(translation *x)
{
    token *t = &x->toks[x->pos];
    if (t->kind != T_END) x->pos++;
    return t;
}

static bool is_punct(translation *x, const punct p)
{
    return peek(x)->kind == T_PUNCT && peek(x)->p == p;
}

static bool is_word(translation *x, const char *word)
{
    const token *t = peek(x);
    return t->kind == T_NAME && same(t->at, t->len, word);
}

static bool accept(translation *x, const punct p)
{
    if (!is_punct(x, p)) return false;
    next(x);
    return true;
}

static bool accept_word(translation *x, const char *word)
{
    if (!is_word(x, word)) return false;
    next(x);
    return true;
}

static void expect(translation *x, const punct p, const char *what)
{
    if (!accept(x, p)) {
        const token *t = peek(x);
        fail(x, t->line, t->col, "expected %s, found '%.*s'", what, t->kind == T_END ? 3 : t->len,
             t->kind == T_END ? "end" : t->at);
    }
}

// A `>` that closes a template, out of `>>`, `>=` or `>>=` if it's there.
static void expect_close(translation *x)
{
    token *t = peek(x);
    if (t->kind == T_PUNCT) {
        if (t->p == P_GT) {
            next(x);
            return;
        }
        if (t->p == P_SHR || t->p == P_GE || t->p == P_SHREQ) {
            t->p = t->p == P_SHR ? P_GT : t->p == P_GE ? P_ASSIGN : P_GE;
            t->at++, t->len--, t->col++;
            return;
        }
    }
    fail(x, t->line, t->col, "expected '>'");
}

static token *expect_name(translation *x, const char *what)
{
    token *t = peek(x);
    if (t->kind != T_NAME) fail(x, t->line, t->col, "expected %s", what);
    return next(x);
}

// Names whose template list WGSL reads after them.
static bool takes_template(const char *name, const int len)
{
    static const char *const names[] = {
        "vec2", "vec3", "vec4", "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3",
        "mat4x4", "array", "ptr", "bitcast", "texture_2d", "texture_cube", "texture_multisampled_2d",
        "texture_2d_array", "atomic",
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (same(name, len, names[i])) return true;
    }
    return false;
}

static int64_t parse_int_text(translation *x, const token *t)
{
    const char *p = t->at;
    int64_t value = 0;
    if (t->len > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        for (int i = 2; i < t->len; i++) {
            const char c = p[i];
            value = value * 16 + (digit(c) ? c - '0' : (c | 32) - 'a' + 10);
            if (value > 0xFFFFFFFFll * 16) fail(x, t->line, t->col, "the number is too big");
        }
    } else {
        for (int i = 0; i < t->len; i++) {
            value = value * 10 + (p[i] - '0');
            if (value > 0xFFFFFFFFll * 16) fail(x, t->line, t->col, "the number is too big");
        }
    }
    return value;
}

static tnode *parse_type(translation *x);

static tnode *parse_template_arg(translation *x)
{
    token *t = peek(x);
    if (t->kind == T_INT) {
        next(x);
        tnode *n = grab(&x->a, sizeof(tnode));
        n->number = parse_int_text(x, t);
        n->line = t->line, n->col = t->col;
        return n;
    }
    return parse_type(x);
}

static tnode *parse_type_named(translation *x, token *name)
{
    tnode *n = grab(&x->a, sizeof(tnode));
    n->name = name->at, n->len = name->len, n->line = name->line, n->col = name->col;
    if (is_punct(x, P_LT) && takes_template(name->at, name->len)) {
        next(x);
        do {
            if (n->nargs == 3) fail(x, name->line, name->col, "too many template arguments");
            n->args[n->nargs++] = parse_template_arg(x);
        } while (accept(x, P_COMMA) && !is_punct(x, P_GT));
        expect_close(x);
    }
    return n;
}

static tnode *parse_type(translation *x)
{
    return parse_type_named(x, expect_name(x, "a type"));
}

static expr *new_expr(translation *x, const xkind kind, const token *at)
{
    expr *e = grab(&x->a, sizeof(expr));
    e->kind = kind;
    e->line = at->line, e->col = at->col;
    return e;
}

static expr *parse_expr(translation *x);

static expr **parse_args(translation *x, int *count)
{
    expr *items[64];
    int n = 0;
    expect(x, P_LPAREN, "'('");
    while (!is_punct(x, P_RPAREN)) {
        if (n == 64) fail(x, peek(x)->line, peek(x)->col, "too many arguments");
        items[n++] = parse_expr(x);
        if (!accept(x, P_COMMA)) break;
    }
    expect(x, P_RPAREN, "')'");
    expr **args = grab(&x->a, sizeof(expr *) * (size_t)(n ? n : 1));
    memcpy(args, items, sizeof(expr *) * (size_t)n);
    *count = n;
    return args;
}

static expr *parse_primary(translation *x)
{
    token *t = peek(x);
    if (t->kind == T_INT) {
        next(x);
        expr *e = new_expr(x, X_INT, t);
        e->i = parse_int_text(x, t);
        e->suffix = t->suffix;
        return e;
    }
    if (t->kind == T_FLOAT) {
        next(x);
        expr *e = new_expr(x, X_FLOAT, t);
        char text[128];
        const int len = t->len < 127 ? t->len : 127;
        memcpy(text, t->at, (size_t)len);
        text[len] = '\0';
        e->f = strtod(text, NULL);
        e->name = t->at, e->len = t->len;
        e->suffix = t->suffix;
        return e;
    }
    if (accept(x, P_LPAREN)) {
        expr *e = parse_expr(x);
        expect(x, P_RPAREN, "')'");
        return e;
    }
    if (t->kind == T_NAME) {
        next(x);
        if (same(t->at, t->len, "true") || same(t->at, t->len, "false")) {
            expr *e = new_expr(x, X_BOOL, t);
            e->i = t->at[0] == 't';
            return e;
        }
        if (is_punct(x, P_LT) && takes_template(t->at, t->len)) { // A type with its template, called
            expr *e = new_expr(x, X_CALL, t);
            e->type = parse_type_named(x, t);
            e->name = t->at, e->len = t->len;
            e->args = parse_args(x, &e->nargs);
            return e;
        }
        if (is_punct(x, P_LPAREN)) {
            expr *e = new_expr(x, X_CALL, t);
            e->name = t->at, e->len = t->len;
            e->args = parse_args(x, &e->nargs);
            return e;
        }
        expr *e = new_expr(x, X_NAME, t);
        e->name = t->at, e->len = t->len;
        return e;
    }
    fail(x, t->line, t->col, "expected an expression, found '%.*s'", t->kind == T_END ? 3 : t->len,
         t->kind == T_END ? "end" : t->at);
    return NULL;
}

static expr *parse_postfix(translation *x, expr *e)
{
    for (;;) {
        token *t = peek(x);
        if (accept(x, P_DOT)) {
            token *name = expect_name(x, "a member's name");
            expr *m = new_expr(x, X_MEMBER, t);
            m->a = e;
            m->name = name->at, m->len = name->len;
            e = m;
        } else if (accept(x, P_LBRACKET)) {
            expr *i = new_expr(x, X_INDEX, t);
            i->a = e;
            i->b = parse_expr(x);
            expect(x, P_RBRACKET, "']'");
            e = i;
        } else {
            return e;
        }
    }
}

static expr *parse_unary(translation *x)
{
    token *t = peek(x);
    if (t->kind == T_PUNCT && (t->p == P_MINUS || t->p == P_BANG || t->p == P_TILDE || t->p == P_STAR || t->p == P_AMP)) {
        next(x);
        expr *operand = parse_unary(x);
        expr *e = new_expr(x, t->p == P_STAR ? X_DEREF : t->p == P_AMP ? X_ADDR : X_UNARY, t);
        e->op = t->p;
        e->a = operand;
        return e;
    }
    return parse_postfix(x, parse_primary(x));
}

static int precedence(const punct p)
{
    switch (p) {
    case P_OROR: return 1;
    case P_ANDAND: return 2;
    case P_PIPE: return 3;
    case P_CARET: return 4;
    case P_AMP: return 5;
    case P_EQ: case P_NE: return 6;
    case P_LT: case P_LE: case P_GT: case P_GE: return 7;
    case P_SHL: case P_SHR: return 8;
    case P_PLUS: case P_MINUS: return 9;
    case P_STAR: case P_SLASH: case P_PERCENT: return 10;
    default: return 0;
    }
}

static expr *parse_binary(translation *x, const int least)
{
    expr *left = parse_unary(x);
    for (;;) {
        token *t = peek(x);
        const int level = t->kind == T_PUNCT ? precedence(t->p) : 0;
        if (!level || level < least) return left;
        next(x);
        expr *right = parse_binary(x, level + 1);
        expr *e = new_expr(x, X_BINARY, t);
        e->op = t->p;
        e->a = left, e->b = right;
        left = e;
    }
}

static expr *parse_expr(translation *x)
{
    return parse_binary(x, 1);
}

static attrs no_attrs(void)
{
    return (attrs){.location = -1, .builtin = -1, .group = -1, .binding = -1};
}

static int builtin_named(translation *x, const token *t)
{
    static const struct {
        const char *name;
        int builtin;
    } builtins[] = {
        {"position", 0}, {"vertex_index", 42}, {"instance_index", 43}, {"front_facing", 17}, {"frag_depth", 22},
        {"sample_index", 18}, {"sample_mask", 20},
    };
    for (size_t i = 0; i < sizeof builtins / sizeof builtins[0]; i++) {
        if (same(t->at, t->len, builtins[i].name)) return builtins[i].builtin;
    }
    fail(x, t->line, t->col, "@builtin(%.*s) isn't one this takes", t->len, t->at);
    return -1;
}

static int attr_number(translation *x)
{
    expect(x, P_LPAREN, "'('");
    token *t = peek(x);
    if (t->kind != T_INT) fail(x, t->line, t->col, "expected a number");
    next(x);
    const int64_t n = parse_int_text(x, t);
    accept(x, P_COMMA);
    expect(x, P_RPAREN, "')'");
    return (int)n;
}

static attrs parse_attrs(translation *x)
{
    attrs at = no_attrs();
    while (accept(x, P_AT)) {
        token *name = expect_name(x, "an attribute");
        if (same(name->at, name->len, "location")) {
            at.location = attr_number(x);
        } else if (same(name->at, name->len, "group")) {
            at.group = attr_number(x);
        } else if (same(name->at, name->len, "binding")) {
            at.binding = attr_number(x);
        } else if (same(name->at, name->len, "builtin")) {
            expect(x, P_LPAREN, "'('");
            at.builtin = builtin_named(x, expect_name(x, "a built-in's name"));
            accept(x, P_COMMA);
            expect(x, P_RPAREN, "')'");
        } else if (same(name->at, name->len, "interpolate")) {
            expect(x, P_LPAREN, "'('");
            token *how = expect_name(x, "how it's interpolated");
            if (same(how->at, how->len, "flat")) at.flat = true;
            else if (!same(how->at, how->len, "perspective")) {
                fail(x, how->line, how->col, "@interpolate(%.*s) isn't one this takes", how->len, how->at);
            }
            while (accept(x, P_COMMA) && !is_punct(x, P_RPAREN)) expect_name(x, "how it's sampled");
            expect(x, P_RPAREN, "')'");
        } else if (same(name->at, name->len, "vertex")) {
            at.stage = 1;
        } else if (same(name->at, name->len, "fragment")) {
            at.stage = 2;
        } else if (same(name->at, name->len, "must_use") || same(name->at, name->len, "invariant")) {
            // Nothing to do here
        } else {
            fail(x, name->line, name->col, "@%.*s isn't an attribute this takes", name->len, name->at);
        }
    }
    return at;
}

static stmt *new_stmt(translation *x, const skind kind, const token *at)
{
    stmt *s = grab(&x->a, sizeof(stmt));
    s->kind = kind;
    s->line = at->line, s->col = at->col;
    return s;
}

static stmt *parse_block(translation *x);
static stmt *parse_statement(translation *x);

// Statements as they're read, grown in the arena: blocks nest, so none of
// them is on the stack.
typedef struct stmt_list {
    stmt **items;
    int n, cap;
} stmt_list;

static void add_stmt(translation *x, stmt_list *l, stmt *s)
{
    if (l->n == l->cap) {
        const int cap = l->cap ? l->cap * 2 : 8;
        stmt **items = grab(&x->a, sizeof(stmt *) * (size_t)cap);
        if (l->n) memcpy(items, l->items, sizeof(stmt *) * (size_t)l->n);
        l->items = items;
        l->cap = cap;
    }
    l->items[l->n++] = s;
}

// A statement that can go where a `for` has its first and third parts: no `;` of its own.
static stmt *parse_simple(translation *x)
{
    token *t = peek(x);
    if (is_word(x, "var") || is_word(x, "let") || is_word(x, "const")) {
        const bool is_var = is_word(x, "var"), is_let = is_word(x, "let");
        next(x);
        if (is_var && accept(x, P_LT)) { // var<function>
            expect_name(x, "an address space");
            expect_close(x);
        }
        token *name = expect_name(x, "a name");
        stmt *s = new_stmt(x, is_var ? S_VAR : is_let ? S_LET : S_CONST, t);
        s->name = name->at, s->len = name->len;
        if (accept(x, P_COLON)) s->type = parse_type(x);
        if (accept(x, P_ASSIGN)) s->e = parse_expr(x);
        else if (!is_var) fail(x, t->line, t->col, "'%.*s' needs a value", name->len, name->at);
        return s;
    }
    if (is_word(x, "_") && peek_at(x, 1)->kind == T_PUNCT && peek_at(x, 1)->p == P_ASSIGN) {
        next(x), next(x);
        stmt *s = new_stmt(x, S_PHONY, t);
        s->e = parse_expr(x);
        return s;
    }
    expr *e = parse_expr(x);
    token *op = peek(x);
    if (op->kind == T_PUNCT) {
        switch (op->p) {
        case P_INC:
        case P_DEC: {
            next(x);
            stmt *s = new_stmt(x, op->p == P_INC ? S_INC : S_DEC, t);
            s->e = e;
            return s;
        }
        case P_ASSIGN: case P_PLUSEQ: case P_MINUSEQ: case P_STAREQ: case P_SLASHEQ: case P_PERCENTEQ:
        case P_AMPEQ: case P_PIPEEQ: case P_CARETEQ: case P_SHLEQ: case P_SHREQ: {
            next(x);
            stmt *s = new_stmt(x, S_ASSIGN, t);
            s->op = op->p;
            s->e = e;
            s->e2 = parse_expr(x);
            return s;
        }
        default:
            break;
        }
    }
    if (e->kind != X_CALL) fail(x, t->line, t->col, "an expression alone isn't a statement");
    stmt *s = new_stmt(x, S_CALL, t);
    s->e = e;
    return s;
}

static stmt *parse_if(translation *x, token *t)
{
    stmt *s = new_stmt(x, S_IF, t);
    s->e = parse_expr(x);
    s->body = parse_block(x);
    if (accept_word(x, "else")) {
        token *after = peek(x);
        s->other = accept_word(x, "if") ? parse_if(x, after) : parse_block(x);
    }
    return s;
}

static stmt *parse_statement(translation *x)
{
    token *t = peek(x);
    if (accept(x, P_SEMI)) return new_stmt(x, S_BLOCK, t);
    if (is_punct(x, P_LBRACE)) return parse_block(x);
    if (accept_word(x, "if")) return parse_if(x, t);
    if (accept_word(x, "for")) {
        stmt *s = new_stmt(x, S_FOR, t);
        expect(x, P_LPAREN, "'('");
        if (!is_punct(x, P_SEMI)) s->init = parse_simple(x);
        expect(x, P_SEMI, "';'");
        if (!is_punct(x, P_SEMI)) s->e = parse_expr(x);
        expect(x, P_SEMI, "';'");
        if (!is_punct(x, P_RPAREN)) s->update = parse_simple(x);
        expect(x, P_RPAREN, "')'");
        s->body = parse_block(x);
        return s;
    }
    if (accept_word(x, "while")) {
        stmt *s = new_stmt(x, S_WHILE, t);
        s->e = parse_expr(x);
        s->body = parse_block(x);
        return s;
    }
    if (accept_word(x, "loop")) {
        stmt *s = new_stmt(x, S_LOOP, t);
        // Its body, with `continuing` in it as its last statement
        expect(x, P_LBRACE, "'{'");
        stmt_list items = {0};
        while (!is_punct(x, P_RBRACE)) {
            if (peek(x)->kind == T_END) fail(x, t->line, t->col, "this loop isn't closed");
            if (is_word(x, "continuing")) {
                token *c = next(x);
                stmt *cont = new_stmt(x, S_BLOCK, c);
                expect(x, P_LBRACE, "'{'");
                stmt_list inner = {0};
                while (!is_punct(x, P_RBRACE)) {
                    token *b = peek(x);
                    if (b->kind == T_END) fail(x, c->line, c->col, "this continuing isn't closed");
                    if (is_word(x, "break") && peek_at(x, 1)->kind == T_NAME && same(peek_at(x, 1)->at, peek_at(x, 1)->len, "if")) {
                        next(x), next(x);
                        stmt *bi = new_stmt(x, S_BREAK_IF, b);
                        bi->e = parse_expr(x);
                        expect(x, P_SEMI, "';'");
                        add_stmt(x, &inner, bi);
                        continue;
                    }
                    add_stmt(x, &inner, parse_statement(x));
                }
                expect(x, P_RBRACE, "'}'");
                cont->list = inner.items;
                cont->count = inner.n;
                s->continuing = cont;
                continue;
            }
            add_stmt(x, &items, parse_statement(x));
        }
        expect(x, P_RBRACE, "'}'");
        stmt *body = new_stmt(x, S_BLOCK, t);
        body->list = items.items;
        body->count = items.n;
        s->body = body;
        return s;
    }
    if (accept_word(x, "switch")) {
        stmt *s = new_stmt(x, S_SWITCH, t);
        s->e = parse_expr(x);
        expect(x, P_LBRACE, "'{'");
        scase *cases = NULL;
        int n = 0, room = 0;
        while (!is_punct(x, P_RBRACE)) {
            if (peek(x)->kind == T_END) fail(x, t->line, t->col, "this switch isn't closed");
            if (n == room) {
                room = room ? room * 2 : 8;
                scase *grown = grab(&x->a, sizeof(scase) * (size_t)room);
                if (n) memcpy(grown, cases, sizeof(scase) * (size_t)n);
                cases = grown;
            }
            scase *c = &cases[n++];
            *c = (scase){0};
            expr *selectors[64];
            if (accept_word(x, "default")) {
                c->fallback = true;
            } else {
                if (!accept_word(x, "case")) fail(x, peek(x)->line, peek(x)->col, "expected 'case' or 'default'");
                do {
                    if (is_punct(x, P_COLON) || is_punct(x, P_LBRACE)) break;
                    if (accept_word(x, "default")) {
                        c->fallback = true;
                        continue;
                    }
                    if (c->count == 64) fail(x, t->line, t->col, "too many selectors");
                    selectors[c->count++] = parse_expr(x);
                } while (accept(x, P_COMMA));
            }
            accept(x, P_COLON);
            c->selectors = grab(&x->a, sizeof(expr *) * (size_t)(c->count ? c->count : 1));
            memcpy(c->selectors, selectors, sizeof(expr *) * (size_t)c->count);
            c->body = parse_block(x);
        }
        expect(x, P_RBRACE, "'}'");
        s->cases = cases;
        s->ncases = n;
        return s;
    }
    if (accept_word(x, "break")) {
        expect(x, P_SEMI, "';'");
        return new_stmt(x, S_BREAK, t);
    }
    if (accept_word(x, "continue")) {
        expect(x, P_SEMI, "';'");
        return new_stmt(x, S_CONTINUE, t);
    }
    if (accept_word(x, "discard")) {
        expect(x, P_SEMI, "';'");
        return new_stmt(x, S_DISCARD, t);
    }
    if (accept_word(x, "return")) {
        stmt *s = new_stmt(x, S_RETURN, t);
        if (!is_punct(x, P_SEMI)) s->e = parse_expr(x);
        expect(x, P_SEMI, "';'");
        return s;
    }
    stmt *s = parse_simple(x);
    expect(x, P_SEMI, "';'");
    return s;
}

static stmt *parse_block(translation *x)
{
    token *t = peek(x);
    expect(x, P_LBRACE, "'{'");
    stmt_list items = {0};
    while (!is_punct(x, P_RBRACE)) {
        if (peek(x)->kind == T_END) fail(x, t->line, t->col, "this '{' isn't closed");
        add_stmt(x, &items, parse_statement(x));
    }
    expect(x, P_RBRACE, "'}'");
    stmt *s = new_stmt(x, S_BLOCK, t);
    s->list = items.items;
    s->count = items.n;
    return s;
}

static void add_struct(translation *x, sdecl *s)
{
    x->structs = realloc(x->structs, sizeof(sdecl *) * (size_t)(x->nstructs + 1));
    if (!x->structs) abort();
    x->structs[x->nstructs++] = s;
}

static void add_global(translation *x, gdecl *g)
{
    x->globals = realloc(x->globals, sizeof(gdecl *) * (size_t)(x->nglobals + 1));
    if (!x->globals) abort();
    x->globals[x->nglobals++] = g;
}

static void add_fn(translation *x, fdecl *f)
{
    x->fns = realloc(x->fns, sizeof(fdecl *) * (size_t)(x->nfns + 1));
    if (!x->fns) abort();
    x->fns[x->nfns++] = f;
}

static void parse_module(translation *x)
{
    while (peek(x)->kind != T_END) {
        if (accept(x, P_SEMI)) continue;
        token *start = peek(x);
        if (accept_word(x, "enable") || accept_word(x, "requires") || accept_word(x, "diagnostic")) {
            while (!accept(x, P_SEMI)) {
                if (peek(x)->kind == T_END) fail(x, start->line, start->col, "expected ';'");
                next(x);
            }
            continue;
        }
        const attrs at = parse_attrs(x);
        token *t = peek(x);
        if (accept_word(x, "struct")) {
            token *name = expect_name(x, "the struct's name");
            sdecl *s = grab(&x->a, sizeof(sdecl));
            s->name = name->at, s->len = name->len;
            expect(x, P_LBRACE, "'{'");
            member items[128];
            int n = 0;
            while (!is_punct(x, P_RBRACE)) {
                if (n == 128) fail(x, t->line, t->col, "too many members");
                member *m = &items[n++];
                m->at = parse_attrs(x);
                token *mname = expect_name(x, "a member's name");
                m->name = mname->at, m->len = mname->len;
                expect(x, P_COLON, "':'");
                m->type = parse_type(x);
                if (!accept(x, P_COMMA)) break;
            }
            expect(x, P_RBRACE, "'}'");
            accept(x, P_SEMI);
            s->members = grab(&x->a, sizeof(member) * (size_t)(n ? n : 1));
            memcpy(s->members, items, sizeof(member) * (size_t)n);
            s->count = n;
            add_struct(x, s);
        } else if (is_word(x, "var") || is_word(x, "const")) {
            const bool is_const = is_word(x, "const");
            next(x);
            gdecl *g = grab(&x->a, sizeof(gdecl));
            g->at = at;
            g->line = t->line, g->col = t->col;
            g->is_const = is_const;
            g->space = SPACE_HANDLE;
            if (!is_const && accept(x, P_LT)) {
                token *space = expect_name(x, "an address space");
                if (same(space->at, space->len, "private")) g->space = SPACE_PRIVATE;
                else if (same(space->at, space->len, "uniform")) g->space = SPACE_UNIFORM;
                else fail(x, space->line, space->col, "var<%.*s> isn't one this takes", space->len, space->at);
                expect_close(x);
            }
            token *name = expect_name(x, "a name");
            g->name = name->at, g->len = name->len;
            if (accept(x, P_COLON)) g->type = parse_type(x);
            if (accept(x, P_ASSIGN)) g->init = parse_expr(x);
            expect(x, P_SEMI, "';'");
            if (!g->type && !g->init) fail(x, t->line, t->col, "'%.*s' needs a type", g->len, g->name);
            add_global(x, g);
        } else if (accept_word(x, "fn")) {
            token *name = expect_name(x, "the function's name");
            fdecl *f = grab(&x->a, sizeof(fdecl));
            f->name = name->at, f->len = name->len;
            f->line = t->line, f->col = t->col;
            f->at = at;
            expect(x, P_LPAREN, "'('");
            param items[64];
            int n = 0;
            while (!is_punct(x, P_RPAREN)) {
                if (n == 64) fail(x, t->line, t->col, "too many parameters");
                param *p = &items[n++];
                p->at = parse_attrs(x);
                token *pname = expect_name(x, "a parameter's name");
                p->name = pname->at, p->len = pname->len;
                expect(x, P_COLON, "':'");
                p->type = parse_type(x);
                if (!accept(x, P_COMMA)) break;
            }
            expect(x, P_RPAREN, "')'");
            f->params = grab(&x->a, sizeof(param) * (size_t)(n ? n : 1));
            memcpy(f->params, items, sizeof(param) * (size_t)n);
            f->count = n;
            f->result_at = no_attrs();
            if (accept(x, P_ARROW)) {
                f->result_at = parse_attrs(x);
                f->result = parse_type(x);
            }
            f->body = parse_block(x);
            add_fn(x, f);
        } else if (accept_word(x, "alias")) {
            fail(x, t->line, t->col, "aliases aren't taken here");
        } else {
            fail(x, t->line, t->col, "expected a declaration, found '%.*s'", t->kind == T_END ? 3 : t->len,
                 t->kind == T_END ? "end" : t->at);
        }
    }
}

// ---------------------------------------------------------------------------
// SPIR-V's numbers, as SPIRV-Headers' spirv.h and GLSL.std.450.h have them

enum {
    OP_EXT_INST_IMPORT = 11, OP_EXT_INST = 12, OP_MEMORY_MODEL = 14, OP_ENTRY_POINT = 15, OP_EXECUTION_MODE = 16,
    OP_CAPABILITY = 17, OP_TYPE_VOID = 19, OP_TYPE_BOOL = 20, OP_TYPE_INT = 21, OP_TYPE_FLOAT = 22,
    OP_TYPE_VECTOR = 23, OP_TYPE_MATRIX = 24, OP_TYPE_IMAGE = 25, OP_TYPE_SAMPLER = 26, OP_TYPE_SAMPLED_IMAGE = 27,
    OP_TYPE_ARRAY = 28, OP_TYPE_STRUCT = 30, OP_TYPE_POINTER = 32, OP_TYPE_FUNCTION = 33, OP_CONSTANT_TRUE = 41,
    OP_CONSTANT_FALSE = 42, OP_CONSTANT = 43, OP_CONSTANT_COMPOSITE = 44, OP_CONSTANT_NULL = 46, OP_FUNCTION = 54,
    OP_FUNCTION_PARAMETER = 55, OP_FUNCTION_END = 56, OP_FUNCTION_CALL = 57, OP_VARIABLE = 59, OP_LOAD = 61,
    OP_STORE = 62, OP_ACCESS_CHAIN = 65, OP_DECORATE = 71, OP_MEMBER_DECORATE = 72, OP_VECTOR_EXTRACT_DYNAMIC = 77,
    OP_VECTOR_SHUFFLE = 79, OP_COMPOSITE_CONSTRUCT = 80, OP_COMPOSITE_EXTRACT = 81, OP_TRANSPOSE = 84,
    OP_SAMPLED_IMAGE = 86, OP_IMAGE_SAMPLE_IMPLICIT_LOD = 87, OP_IMAGE_SAMPLE_EXPLICIT_LOD = 88,
    OP_IMAGE_SAMPLE_DREF_IMPLICIT_LOD = 89, OP_IMAGE_SAMPLE_DREF_EXPLICIT_LOD = 90, OP_IMAGE_FETCH = 95,
    OP_IMAGE_QUERY_SIZE_LOD = 103, OP_IMAGE_QUERY_SIZE = 104, OP_IMAGE_QUERY_LEVELS = 106,
    OP_IMAGE_QUERY_SAMPLES = 107, OP_CONVERT_F_TO_U = 109, OP_CONVERT_F_TO_S = 110, OP_CONVERT_S_TO_F = 111,
    OP_CONVERT_U_TO_F = 112, OP_QUANTIZE_TO_F16 = 116, OP_BITCAST = 124, OP_S_NEGATE = 126, OP_F_NEGATE = 127,
    OP_I_ADD = 128, OP_F_ADD = 129, OP_I_SUB = 130, OP_F_SUB = 131, OP_I_MUL = 132, OP_F_MUL = 133, OP_U_DIV = 134,
    OP_S_DIV = 135, OP_F_DIV = 136, OP_U_MOD = 137, OP_S_REM = 138, OP_F_REM = 140, OP_VECTOR_TIMES_SCALAR = 142,
    OP_MATRIX_TIMES_SCALAR = 143, OP_VECTOR_TIMES_MATRIX = 144, OP_MATRIX_TIMES_VECTOR = 145,
    OP_MATRIX_TIMES_MATRIX = 146, OP_DOT = 148, OP_ANY = 154, OP_ALL = 155, OP_IS_NAN = 156, OP_IS_INF = 157,
    OP_LOGICAL_EQUAL = 164, OP_LOGICAL_NOT_EQUAL = 165, OP_LOGICAL_OR = 166, OP_LOGICAL_AND = 167,
    OP_LOGICAL_NOT = 168, OP_SELECT = 169, OP_I_EQUAL = 170, OP_I_NOT_EQUAL = 171, OP_U_GREATER_THAN = 172,
    OP_S_GREATER_THAN = 173, OP_U_GREATER_THAN_EQUAL = 174, OP_S_GREATER_THAN_EQUAL = 175, OP_U_LESS_THAN = 176,
    OP_S_LESS_THAN = 177, OP_U_LESS_THAN_EQUAL = 178, OP_S_LESS_THAN_EQUAL = 179, OP_F_ORD_EQUAL = 180,
    OP_F_UNORD_NOT_EQUAL = 183, OP_F_ORD_LESS_THAN = 184, OP_F_ORD_GREATER_THAN = 186,
    OP_F_ORD_LESS_THAN_EQUAL = 188, OP_F_ORD_GREATER_THAN_EQUAL = 190, OP_SHIFT_RIGHT_LOGICAL = 194,
    OP_SHIFT_RIGHT_ARITHMETIC = 195, OP_SHIFT_LEFT_LOGICAL = 196, OP_BITWISE_OR = 197, OP_BITWISE_XOR = 198,
    OP_BITWISE_AND = 199, OP_NOT = 200, OP_BIT_FIELD_INSERT = 201, OP_BIT_FIELD_S_EXTRACT = 202,
    OP_BIT_FIELD_U_EXTRACT = 203, OP_BIT_REVERSE = 204, OP_BIT_COUNT = 205, OP_DPDX = 207, OP_DPDY = 208,
    OP_FWIDTH = 209, OP_DPDX_FINE = 210, OP_DPDY_FINE = 211, OP_FWIDTH_FINE = 212, OP_DPDX_COARSE = 213,
    OP_DPDY_COARSE = 214, OP_FWIDTH_COARSE = 215, OP_PHI = 245, OP_LOOP_MERGE = 246, OP_SELECTION_MERGE = 247,
    OP_LABEL = 248, OP_BRANCH = 249, OP_BRANCH_CONDITIONAL = 250, OP_SWITCH = 251, OP_KILL = 252, OP_RETURN = 253,
    OP_RETURN_VALUE = 254, OP_UNREACHABLE = 255,
};

enum { SC_UNIFORM_CONSTANT = 0, SC_INPUT = 1, SC_UNIFORM = 2, SC_OUTPUT = 3, SC_PRIVATE = 6, SC_FUNCTION = 7 };

enum {
    DEC_BLOCK = 2, DEC_COL_MAJOR = 5, DEC_ARRAY_STRIDE = 6, DEC_MATRIX_STRIDE = 7, DEC_BUILTIN = 11, DEC_FLAT = 14,
    DEC_LOCATION = 30, DEC_BINDING = 33, DEC_DESCRIPTOR_SET = 34, DEC_OFFSET = 35,
};

enum { BUILTIN_POSITION = 0, BUILTIN_FRAG_COORD = 15, BUILTIN_FRAG_DEPTH = 22 };

enum { CAP_SHADER = 1, CAP_IMAGE_QUERY = 50, CAP_DERIVATIVE_CONTROL = 51 };

enum { IMAGE_BIAS = 1, IMAGE_LOD = 2, IMAGE_GRAD = 4, IMAGE_SAMPLE = 0x40 };

enum {
    GLSL_ROUND_EVEN = 2, GLSL_TRUNC = 3, GLSL_F_ABS = 4, GLSL_S_ABS = 5, GLSL_F_SIGN = 6, GLSL_S_SIGN = 7,
    GLSL_FLOOR = 8, GLSL_CEIL = 9, GLSL_FRACT = 10, GLSL_RADIANS = 11, GLSL_DEGREES = 12, GLSL_SIN = 13,
    GLSL_COS = 14, GLSL_TAN = 15, GLSL_ASIN = 16, GLSL_ACOS = 17, GLSL_ATAN = 18, GLSL_SINH = 19, GLSL_COSH = 20,
    GLSL_TANH = 21, GLSL_ASINH = 22, GLSL_ACOSH = 23, GLSL_ATANH = 24, GLSL_ATAN2 = 25, GLSL_POW = 26,
    GLSL_EXP = 27, GLSL_LOG = 28, GLSL_EXP2 = 29, GLSL_LOG2 = 30, GLSL_SQRT = 31, GLSL_INVERSE_SQRT = 32,
    GLSL_DETERMINANT = 33, GLSL_F_MIN = 37, GLSL_U_MIN = 38, GLSL_S_MIN = 39, GLSL_F_MAX = 40, GLSL_U_MAX = 41,
    GLSL_S_MAX = 42, GLSL_F_CLAMP = 43, GLSL_U_CLAMP = 44, GLSL_S_CLAMP = 45, GLSL_F_MIX = 46, GLSL_STEP = 48,
    GLSL_SMOOTH_STEP = 49, GLSL_FMA = 50, GLSL_LDEXP = 53, GLSL_PACK_SNORM4X8 = 54, GLSL_PACK_UNORM4X8 = 55,
    GLSL_PACK_SNORM2X16 = 56, GLSL_PACK_UNORM2X16 = 57, GLSL_PACK_HALF2X16 = 58, GLSL_UNPACK_SNORM2X16 = 60,
    GLSL_UNPACK_UNORM2X16 = 61, GLSL_UNPACK_HALF2X16 = 62, GLSL_UNPACK_SNORM4X8 = 63, GLSL_UNPACK_UNORM4X8 = 64,
    GLSL_LENGTH = 66, GLSL_DISTANCE = 67, GLSL_CROSS = 68, GLSL_NORMALIZE = 69, GLSL_FACE_FORWARD = 70,
    GLSL_REFLECT = 71, GLSL_REFRACT = 72, GLSL_FIND_I_LSB = 73, GLSL_FIND_S_MSB = 74, GLSL_FIND_U_MSB = 75,
};

// ---------------------------------------------------------------------------
// Writing instructions

static uint32_t new_id(translation *x)
{
    return ++x->next_id;
}

static void ins(words *b, const uint32_t op, const uint32_t *operands, const int n)
{
    put(b, op | (uint32_t)(n + 1) << 16);
    for (int i = 0; i < n; i++) put(b, operands[i]);
}

#define OPS(...) ((const uint32_t[]){__VA_ARGS__}), (int)(sizeof((const uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t))
#define INS(b, op, ...) ins((b), (op), OPS(__VA_ARGS__))

// A string as SPIR-V has one: its bytes, a 0 after them, in whole words.
static void put_string(words *b, const char *s, const int len)
{
    for (int i = 0; i <= len; i += 4) {
        uint32_t w = 0;
        for (int j = 0; j < 4; j++) {
            const int at = i + j;
            const uint32_t c = at < len ? (unsigned char)s[at] : 0u;
            w |= c << (8 * j);
        }
        put(b, w);
    }
}

static int string_words(const int len)
{
    return len / 4 + 1;
}

// ---------------------------------------------------------------------------
// Types, each made once

static type *T(translation *x, const int t)
{
    return &x->types[t];
}

static int add_type(translation *x, const type t)
{
    for (int i = 1; i < x->ntypes; i++) {
        const type *o = &x->types[i];
        if (o->kind == t.kind && o->elem == t.elem && o->n == t.n && o->sc == t.sc && o->s == t.s && o->dim == t.dim
            && o->depth == t.depth && o->multisampled == t.multisampled) {
            return i;
        }
    }
    if (x->ntypes == x->cap_types) {
        x->cap_types = x->cap_types ? x->cap_types * 2 : 64;
        x->types = realloc(x->types, sizeof(type) * (size_t)x->cap_types);
        if (!x->types) abort();
    }
    x->types[x->ntypes] = t;
    x->types[x->ntypes].id = 0;
    x->types[x->ntypes].laid = 0;
    return x->ntypes++;
}

static int t_simple(translation *x, const kind k)
{
    return add_type(x, (type){.kind = k});
}

static int t_vec(translation *x, const int elem, const int n)
{
    return n == 1 ? elem : add_type(x, (type){.kind = K_VEC, .elem = elem, .n = n});
}

static int t_mat(translation *x, const int cols, const int rows)
{
    return add_type(x, (type){.kind = K_MAT, .elem = t_vec(x, t_simple(x, K_F32), rows), .n = cols});
}

static int t_array(translation *x, const int elem, const int n)
{
    return add_type(x, (type){.kind = K_ARRAY, .elem = elem, .n = n});
}

static int t_ptr(translation *x, const int sc, const int elem)
{
    return add_type(x, (type){.kind = K_PTR, .elem = elem, .sc = sc});
}

static bool is_scalar(translation *x, const int t)
{
    const kind k = T(x, t)->kind;
    return k == K_BOOL || k == K_I32 || k == K_U32 || k == K_F32 || k == K_AINT || k == K_AFLOAT;
}

// A scalar itself, a vector's elements', a matrix's numbers'.
static int scalar_of(translation *x, const int t)
{
    const type *ty = T(x, t);
    if (ty->kind == K_VEC) return ty->elem;
    if (ty->kind == K_MAT) return T(x, ty->elem)->elem;
    return t;
}

static int width_of(translation *x, const int t)
{
    const type *ty = T(x, t);
    return ty->kind == K_VEC ? ty->n : 1;
}

static bool is_float(translation *x, const int t)
{
    const kind k = T(x, scalar_of(x, t))->kind;
    return k == K_F32 || k == K_AFLOAT;
}

static bool is_int(translation *x, const int t)
{
    const kind k = T(x, scalar_of(x, t))->kind;
    return k == K_I32 || k == K_U32 || k == K_AINT;
}

static bool is_signed(translation *x, const int t)
{
    const kind k = T(x, scalar_of(x, t))->kind;
    return k == K_I32 || k == K_AINT;
}

static bool is_bool(translation *x, const int t)
{
    return T(x, scalar_of(x, t))->kind == K_BOOL;
}

// The same shape as `t` with `scalar` for its numbers.
static int with_scalar(translation *x, const int t, const int scalar)
{
    const type *ty = T(x, t);
    if (ty->kind == K_VEC) return t_vec(x, scalar, ty->n);
    return scalar;
}

static void type_name(translation *x, const int t, char *out, const size_t size)
{
    const type *ty = T(x, t);
    char inner[96];
    switch (ty->kind) {
    case K_VOID: snprintf(out, size, "nothing"); break;
    case K_BOOL: snprintf(out, size, "bool"); break;
    case K_I32: snprintf(out, size, "i32"); break;
    case K_U32: snprintf(out, size, "u32"); break;
    case K_F32: snprintf(out, size, "f32"); break;
    case K_AINT: snprintf(out, size, "an int"); break;
    case K_AFLOAT: snprintf(out, size, "a float"); break;
    case K_VEC:
        type_name(x, ty->elem, inner, sizeof inner);
        snprintf(out, size, "vec%d<%s>", ty->n, inner);
        break;
    case K_MAT: snprintf(out, size, "mat%dx%df", ty->n, T(x, ty->elem)->n); break;
    case K_ARRAY:
        type_name(x, ty->elem, inner, sizeof inner);
        snprintf(out, size, "array<%s, %d>", inner, ty->n);
        break;
    case K_STRUCT: snprintf(out, size, "%.*s", ty->s->len, ty->s->name); break;
    case K_PTR:
        type_name(x, ty->elem, inner, sizeof inner);
        snprintf(out, size, "ptr<%s>", inner);
        break;
    case K_SAMPLER: snprintf(out, size, "sampler"); break;
    case K_SAMPLER_CMP: snprintf(out, size, "sampler_comparison"); break;
    case K_TEXTURE: snprintf(out, size, "a texture"); break;
    default: snprintf(out, size, "nothing"); break;
    }
}

// WGSL's layout of the uniform address space: what a value's offset is a
// multiple of, and how many bytes it takes.
static void layout_of(translation *x, const int t, uint32_t *align, uint32_t *size)
{
    const type *ty = T(x, t);
    switch (ty->kind) {
    case K_VEC:
        *align = ty->n == 2 ? 8u : 16u;
        *size = (uint32_t)ty->n * 4u;
        return;
    case K_MAT: {
        uint32_t a, s;
        layout_of(x, ty->elem, &a, &s);
        *align = a;
        *size = (uint32_t)ty->n * ((s + a - 1u) / a * a);
        return;
    }
    case K_ARRAY: {
        uint32_t a, s;
        layout_of(x, ty->elem, &a, &s);
        if (a < 16u) a = 16u; // An array's elements are 16 bytes apart at least, in a uniform block
        *align = a;
        *size = (s + a - 1u) / a * a * (uint32_t)ty->n;
        return;
    }
    case K_STRUCT: {
        const sdecl *s = ty->s;
        *align = s->align < 16u ? 16u : s->align;
        *size = s->size;
        return;
    }
    default:
        *align = 4u;
        *size = 4u;
        return;
    }
}

static uint32_t spv_type(translation *x, int t);
static uint32_t const_u32(translation *x, uint32_t v);

// What a uniform block has: its structs with each member's offset, its arrays
// with how far apart their elements are.
static uint32_t spv_laid(translation *x, const int t)
{
    const type ty = *T(x, t);
    if (ty.kind != K_STRUCT && ty.kind != K_ARRAY) return spv_type(x, t);
    if (ty.laid) return ty.laid;
    uint32_t id;
    if (ty.kind == K_ARRAY) {
        const uint32_t elem = spv_laid(x, ty.elem);
        const uint32_t count_id = const_u32(x, (uint32_t)ty.n);
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_ARRAY, id, elem, count_id);
        uint32_t align, size;
        layout_of(x, t, &align, &size);
        INS(&x->decorations, OP_DECORATE, id, DEC_ARRAY_STRIDE, ty.n ? size / (uint32_t)ty.n : 16u);
    } else {
        const sdecl *s = ty.s;
        uint32_t members[128];
        for (int i = 0; i < s->count; i++) members[i] = spv_laid(x, s->member_types[i]);
        id = new_id(x);
        put(&x->globals_words, OP_TYPE_STRUCT | (uint32_t)(s->count + 2) << 16);
        put(&x->globals_words, id);
        for (int i = 0; i < s->count; i++) put(&x->globals_words, members[i]);
        for (int i = 0; i < s->count; i++) {
            INS(&x->decorations, OP_MEMBER_DECORATE, id, (uint32_t)i, DEC_OFFSET, s->offsets[i]);
            int m = s->member_types[i];
            while (T(x, m)->kind == K_ARRAY) m = T(x, m)->elem;
            if (T(x, m)->kind == K_MAT) {
                uint32_t align, size;
                layout_of(x, T(x, m)->elem, &align, &size);
                INS(&x->decorations, OP_MEMBER_DECORATE, id, (uint32_t)i, DEC_COL_MAJOR);
                INS(&x->decorations, OP_MEMBER_DECORATE, id, (uint32_t)i, DEC_MATRIX_STRIDE, align);
            }
        }
    }
    T(x, t)->laid = id;
    return id;
}

static uint32_t spv_type(translation *x, const int t)
{
    if (T(x, t)->id) return T(x, t)->id;
    const type ty = *T(x, t);
    uint32_t id = 0;
    switch (ty.kind) {
    case K_VOID:
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_VOID, id);
        break;
    case K_BOOL:
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_BOOL, id);
        break;
    case K_I32:
    case K_U32:
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_INT, id, 32, ty.kind == K_I32 ? 1u : 0u);
        break;
    case K_F32:
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_FLOAT, id, 32);
        break;
    case K_VEC: {
        const uint32_t elem = spv_type(x, ty.elem);
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_VECTOR, id, elem, (uint32_t)ty.n);
        break;
    }
    case K_MAT: {
        const uint32_t column = spv_type(x, ty.elem);
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_MATRIX, id, column, (uint32_t)ty.n);
        break;
    }
    case K_ARRAY: {
        const uint32_t elem = spv_type(x, ty.elem);
        const uint32_t count_id = const_u32(x, (uint32_t)ty.n);
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_ARRAY, id, elem, count_id);
        break;
    }
    case K_STRUCT: {
        const sdecl *s = ty.s;
        uint32_t members[128];
        for (int i = 0; i < s->count; i++) members[i] = spv_type(x, s->member_types[i]);
        id = new_id(x);
        put(&x->globals_words, OP_TYPE_STRUCT | (uint32_t)(s->count + 2) << 16);
        put(&x->globals_words, id);
        for (int i = 0; i < s->count; i++) put(&x->globals_words, members[i]);
        break;
    }
    case K_PTR: {
        const uint32_t elem = ty.sc == SC_UNIFORM ? spv_laid(x, ty.elem) : spv_type(x, ty.elem);
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_POINTER, id, (uint32_t)ty.sc, elem);
        break;
    }
    case K_SAMPLER:
    case K_SAMPLER_CMP: {
        const int other = t_simple(x, ty.kind == K_SAMPLER ? K_SAMPLER_CMP : K_SAMPLER);
        if (T(x, other)->id) {
            id = T(x, other)->id; // One SPIR-V type: Vulkan's sampler says whether it compares
        } else {
            id = new_id(x);
            INS(&x->globals_words, OP_TYPE_SAMPLER, id);
        }
        break;
    }
    case K_TEXTURE: {
        const uint32_t sampled = spv_type(x, ty.elem);
        id = new_id(x);
        INS(&x->globals_words, OP_TYPE_IMAGE, id, sampled, ty.dim == DIM_CUBE ? 3u : 1u, ty.depth ? 1u : 0u, 0u,
            ty.multisampled ? 1u : 0u, 1u, 0u);
        break;
    }
    default:
        fail(x, 0, 0, "a type that SPIR-V has nothing for");
    }
    T(x, t)->id = id;
    return id;
}

static uint32_t sampled_image_type(translation *x, const uint32_t image)
{
    for (int i = 0; i < x->nsampled; i++) {
        if (x->sampled_images[i][0] == image) return x->sampled_images[i][1];
    }
    const uint32_t id = new_id(x);
    INS(&x->globals_words, OP_TYPE_SAMPLED_IMAGE, id, image);
    if (x->nsampled < 8) {
        x->sampled_images[x->nsampled][0] = image;
        x->sampled_images[x->nsampled][1] = id;
        x->nsampled++;
    }
    return id;
}

static uint32_t function_type(translation *x, const uint32_t result, const uint32_t *params, const int n)
{
    for (int i = 0; i < x->nfunctypes; i++) {
        if (x->functypes[i].n == n + 1 && x->functypes[i].operands[0] == result
            && (n == 0 || memcmp(x->functypes[i].operands + 1, params, sizeof(uint32_t) * (size_t)n) == 0)) {
            return x->functypes[i].id;
        }
    }
    const uint32_t id = new_id(x);
    put(&x->globals_words, OP_TYPE_FUNCTION | (uint32_t)(n + 3) << 16);
    put(&x->globals_words, id);
    put(&x->globals_words, result);
    for (int i = 0; i < n; i++) put(&x->globals_words, params[i]);
    if (x->nfunctypes == x->cap_functypes) {
        x->cap_functypes = x->cap_functypes ? x->cap_functypes * 2 : 16;
        x->functypes = realloc(x->functypes, sizeof(*x->functypes) * (size_t)x->cap_functypes);
        if (!x->functypes) abort();
    }
    uint32_t *operands = grab(&x->a, sizeof(uint32_t) * (size_t)(n + 1));
    operands[0] = result;
    if (n) memcpy(operands + 1, params, sizeof(uint32_t) * (size_t)n);
    x->functypes[x->nfunctypes].operands = operands;
    x->functypes[x->nfunctypes].n = n + 1;
    x->functypes[x->nfunctypes].id = id;
    x->nfunctypes++;
    return id;
}

// ---------------------------------------------------------------------------
// Constants, each made once

static uint32_t constant_bits(translation *x, const int t, const uint32_t bits)
{
    const uint32_t type_id = spv_type(x, t);
    const int op = T(x, t)->kind == K_BOOL ? (bits ? OP_CONSTANT_TRUE : OP_CONSTANT_FALSE) : OP_CONSTANT;
    for (int i = 0; i < x->nconsts; i++) {
        if (x->consts[i].type == type_id && x->consts[i].bits == bits && x->consts[i].op == op) return x->consts[i].id;
    }
    const uint32_t id = new_id(x);
    if (op == OP_CONSTANT) INS(&x->globals_words, OP_CONSTANT, type_id, id, bits);
    else INS(&x->globals_words, (uint32_t)op, type_id, id);
    if (x->nconsts == x->cap_consts) {
        x->cap_consts = x->cap_consts ? x->cap_consts * 2 : 64;
        x->consts = realloc(x->consts, sizeof(*x->consts) * (size_t)x->cap_consts);
        if (!x->consts) abort();
    }
    x->consts[x->nconsts].type = type_id;
    x->consts[x->nconsts].bits = bits;
    x->consts[x->nconsts].op = op;
    x->consts[x->nconsts].id = id;
    x->nconsts++;
    return id;
}

static uint32_t const_i32(translation *x, const int32_t v)
{
    return constant_bits(x, t_simple(x, K_I32), (uint32_t)v);
}

static uint32_t const_u32(translation *x, const uint32_t v)
{
    return constant_bits(x, t_simple(x, K_U32), v);
}

static uint32_t const_f32(translation *x, const float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    return constant_bits(x, t_simple(x, K_F32), bits);
}

static uint32_t const_bool(translation *x, const bool v)
{
    return constant_bits(x, t_simple(x, K_BOOL), v ? 1u : 0u);
}

static uint32_t const_null(translation *x, const int t)
{
    const uint32_t type_id = spv_type(x, t);
    for (int i = 0; i < x->nconsts; i++) {
        if (x->consts[i].type == type_id && x->consts[i].op == OP_CONSTANT_NULL) return x->consts[i].id;
    }
    const uint32_t id = new_id(x);
    INS(&x->globals_words, OP_CONSTANT_NULL, type_id, id);
    if (x->nconsts == x->cap_consts) {
        x->cap_consts = x->cap_consts ? x->cap_consts * 2 : 64;
        x->consts = realloc(x->consts, sizeof(*x->consts) * (size_t)x->cap_consts);
        if (!x->consts) abort();
    }
    x->consts[x->nconsts].type = type_id;
    x->consts[x->nconsts].bits = 0;
    x->consts[x->nconsts].op = OP_CONSTANT_NULL;
    x->consts[x->nconsts].id = id;
    x->nconsts++;
    return id;
}

// A scalar of type `t` that's `value` (a number of any kind): for constants a
// shader works with, as a vector's whole of them when `t` is one.
static uint32_t const_of(translation *x, const int t, const double value)
{
    const int scalar = scalar_of(x, t);
    uint32_t id;
    switch (T(x, scalar)->kind) {
    case K_F32: id = const_f32(x, (float)value); break;
    case K_I32: id = const_i32(x, (int32_t)value); break;
    case K_U32: id = const_u32(x, (uint32_t)value); break;
    case K_BOOL: id = const_bool(x, value != 0.0); break;
    default: fail(x, 0, 0, "a constant of a type that has none"); return 0;
    }
    if (T(x, t)->kind != K_VEC) return id;
    const int n = T(x, t)->n;
    const uint32_t type_id = spv_type(x, t);
    const uint32_t composite = new_id(x);
    put(&x->globals_words, OP_CONSTANT_COMPOSITE | (uint32_t)(n + 3) << 16);
    put(&x->globals_words, type_id);
    put(&x->globals_words, composite);
    for (int i = 0; i < n; i++) put(&x->globals_words, id);
    return composite;
}

// ---------------------------------------------------------------------------
// Types as they're written

static sdecl *find_struct(translation *x, const char *name, const int len)
{
    for (int i = 0; i < x->nstructs; i++) {
        if (x->structs[i]->len == len && memcmp(x->structs[i]->name, name, (size_t)len) == 0) return x->structs[i];
    }
    return NULL;
}

static int resolve(translation *x, const tnode *n);

static int resolve_struct(translation *x, sdecl *s, const int line, const int col)
{
    if (s->type) return s->type;
    if (s->resolving) fail(x, line, col, "'%.*s' holds itself", s->len, s->name);
    s->resolving = true;
    s->member_types = grab(&x->a, sizeof(int) * (size_t)(s->count ? s->count : 1));
    s->offsets = grab(&x->a, sizeof(uint32_t) * (size_t)(s->count ? s->count : 1));
    uint32_t at = 0, most = 4;
    for (int i = 0; i < s->count; i++) {
        s->member_types[i] = resolve(x, s->members[i].type);
        uint32_t align, size;
        layout_of(x, s->member_types[i], &align, &size);
        at = (at + align - 1u) / align * align;
        s->offsets[i] = at;
        at += size;
        if (align > most) most = align;
    }
    s->align = most;
    s->size = (at + most - 1u) / most * most;
    s->resolving = false;
    s->type = add_type(x, (type){.kind = K_STRUCT, .s = s});
    return s->type;
}

// The scalar a vector's or a matrix's short name ends in: f, i, u or h.
static int suffix_scalar(translation *x, const char c, const tnode *n)
{
    switch (c) {
    case 'f': return t_simple(x, K_F32);
    case 'i': return t_simple(x, K_I32);
    case 'u': return t_simple(x, K_U32);
    case 'h': fail(x, n->line, n->col, "f16 isn't taken here"); return 0;
    default: fail(x, n->line, n->col, "'%.*s' isn't a type", n->len, n->name); return 0;
    }
}

static int resolve_scalar_arg(translation *x, const tnode *n, const int i)
{
    if (n->nargs <= i || !n->args[i]->name) fail(x, n->line, n->col, "'%.*s' needs a type in its <>", n->len, n->name);
    const int t = resolve(x, n->args[i]);
    if (!is_scalar(x, t)) fail(x, n->line, n->col, "'%.*s' holds a scalar", n->len, n->name);
    return t;
}

// The type a name with its template names, or 0 when the name isn't a type.
static int resolve_maybe(translation *x, const tnode *n)
{
    const char *s = n->name;
    const int len = n->len;
    if (!s) return 0;
    if (same(s, len, "f32")) return t_simple(x, K_F32);
    if (same(s, len, "i32")) return t_simple(x, K_I32);
    if (same(s, len, "u32")) return t_simple(x, K_U32);
    if (same(s, len, "bool")) return t_simple(x, K_BOOL);
    if (same(s, len, "f16")) fail(x, n->line, n->col, "f16 isn't taken here");
    if (len >= 4 && memcmp(s, "vec", 3) == 0 && s[3] >= '2' && s[3] <= '4') {
        const int size = s[3] - '0';
        if (len == 4) return n->nargs ? t_vec(x, resolve_scalar_arg(x, n, 0), size) : 0;
        if (len == 5) return t_vec(x, suffix_scalar(x, s[4], n), size);
        return 0;
    }
    if (len >= 6 && memcmp(s, "mat", 3) == 0 && s[3] >= '2' && s[3] <= '4' && s[4] == 'x' && s[5] >= '2' && s[5] <= '4') {
        const int cols = s[3] - '0', rows = s[5] - '0';
        if (len == 6) {
            if (!n->nargs) return 0;
            if (T(x, resolve_scalar_arg(x, n, 0))->kind != K_F32) fail(x, n->line, n->col, "matrices are of f32");
            return t_mat(x, cols, rows);
        }
        if (len == 7) {
            if (T(x, suffix_scalar(x, s[6], n))->kind != K_F32) fail(x, n->line, n->col, "matrices are of f32");
            return t_mat(x, cols, rows);
        }
        return 0;
    }
    if (same(s, len, "array")) {
        if (!n->nargs) return 0;
        if (n->nargs < 2 || n->args[1]->name) fail(x, n->line, n->col, "an array here has a count, which is a number");
        if (n->args[1]->number <= 0 || n->args[1]->number > 65536) fail(x, n->line, n->col, "an array's count is from 1 to 65536");
        return t_array(x, resolve(x, n->args[0]), (int)n->args[1]->number);
    }
    if (same(s, len, "ptr")) {
        if (n->nargs < 2 || !n->args[0]->name) fail(x, n->line, n->col, "a pointer says its address space and type");
        const tnode *space = n->args[0];
        int sc;
        if (same(space->name, space->len, "function")) sc = SC_FUNCTION;
        else if (same(space->name, space->len, "private")) sc = SC_PRIVATE;
        else fail(x, n->line, n->col, "ptr<%.*s, ...> isn't taken here", space->len, space->name), sc = 0;
        return t_ptr(x, sc, resolve(x, n->args[1]));
    }
    if (same(s, len, "sampler")) return t_simple(x, K_SAMPLER);
    if (same(s, len, "sampler_comparison")) return t_simple(x, K_SAMPLER_CMP);
    if (same(s, len, "texture_2d") || same(s, len, "texture_cube") || same(s, len, "texture_multisampled_2d")) {
        const int sampled = resolve_scalar_arg(x, n, 0);
        if (T(x, sampled)->kind == K_BOOL) fail(x, n->line, n->col, "a texture holds numbers");
        return add_type(x, (type){.kind = K_TEXTURE, .elem = sampled, .dim = same(s, len, "texture_cube") ? DIM_CUBE : DIM_2D,
                                  .multisampled = same(s, len, "texture_multisampled_2d")});
    }
    if (same(s, len, "texture_depth_2d") || same(s, len, "texture_depth_cube") || same(s, len, "texture_depth_multisampled_2d")) {
        return add_type(x, (type){.kind = K_TEXTURE, .elem = t_simple(x, K_F32), .depth = true,
                                  .dim = same(s, len, "texture_depth_cube") ? DIM_CUBE : DIM_2D,
                                  .multisampled = same(s, len, "texture_depth_multisampled_2d")});
    }
    sdecl *st = find_struct(x, s, len);
    if (st) return resolve_struct(x, st, n->line, n->col);
    return 0;
}

static int resolve(translation *x, const tnode *n)
{
    if (!n->name) fail(x, n->line, n->col, "expected a type, found a number");
    const int t = resolve_maybe(x, n);
    if (!t) fail(x, n->line, n->col, "'%.*s' isn't a type", n->len, n->name);
    return t;
}

// ---------------------------------------------------------------------------
// Names in scope

static void bind_in(translation *x, binding **list, int *count, int *cap, const char *name, const int len, const val v)
{
    if (*count == *cap) {
        *cap = *cap ? *cap * 2 : 64;
        *list = realloc(*list, sizeof(binding) * (size_t)*cap);
        if (!*list) abort();
    }
    (*list)[(*count)++] = (binding){.name = name, .len = len, .v = v};
    (void)x;
}

static void bind(translation *x, const char *name, const int len, const val v)
{
    if (x->fn) bind_in(x, &x->fn->scope, &x->fn->scope_count, &x->fn->scope_cap, name, len, v);
    else bind_in(x, &x->globals_scope, &x->nglobal_bindings, &x->cap_global_bindings, name, len, v);
}

static const binding *lookup(translation *x, const char *name, const int len)
{
    if (x->fn) {
        for (int i = x->fn->scope_count - 1; i >= 0; i--) {
            const binding *b = &x->fn->scope[i];
            if (b->len == len && memcmp(b->name, name, (size_t)len) == 0) return b;
        }
    }
    for (int i = x->nglobal_bindings - 1; i >= 0; i--) {
        const binding *b = &x->globals_scope[i];
        if (b->len == len && memcmp(b->name, name, (size_t)len) == 0) return b;
    }
    return NULL;
}

static fdecl *find_fn(translation *x, const char *name, const int len)
{
    for (int i = 0; i < x->nfns; i++) {
        if (x->fns[i]->len == len && memcmp(x->fns[i]->name, name, (size_t)len) == 0) return x->fns[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Writing a function's code

// An instruction that makes a value of type `t`, with `n` operands after that: its id.
static uint32_t emit(translation *x, const uint32_t op, const int t, const uint32_t *operands, const int n)
{
    const uint32_t id = new_id(x);
    words *b = &x->fn->body;
    put(b, op | (uint32_t)(n + 3) << 16);
    put(b, spv_type(x, t));
    put(b, id);
    for (int i = 0; i < n; i++) put(b, operands[i]);
    return id;
}

#define EMIT(x, op, t, ...) emit((x), (op), (t), OPS(__VA_ARGS__))

static uint32_t glsl(translation *x, const int t, const uint32_t inst, const uint32_t *args, const int n)
{
    uint32_t operands[8] = {x->glsl, inst};
    for (int i = 0; i < n; i++) operands[2 + i] = args[i];
    return emit(x, OP_EXT_INST, t, operands, n + 2);
}

static void start_block(translation *x, const uint32_t label)
{
    INS(&x->fn->body, OP_LABEL, label);
    x->fn->label = label;
    x->fn->ended = false;
}

static void branch(translation *x, const uint32_t to)
{
    if (x->fn->ended) return;
    INS(&x->fn->body, OP_BRANCH, to);
    x->fn->ended = true;
}

// A variable of the function's, in its first block.
static uint32_t local_var(translation *x, const int t)
{
    const uint32_t ptr = spv_type(x, t_ptr(x, SC_FUNCTION, t));
    const uint32_t id = new_id(x);
    INS(&x->fn->vars, OP_VARIABLE, ptr, id, SC_FUNCTION);
    return id;
}

// A value of a uniform block's struct or array, as one of the same type
// anywhere else: the block's are laid out, so they're other SPIR-V types.
static uint32_t unlaid(translation *x, const int t, const uint32_t id)
{
    const type ty = *T(x, t);
    if (ty.kind != K_STRUCT && ty.kind != K_ARRAY) return id;
    const int count = ty.kind == K_STRUCT ? ty.s->count : ty.n;
    uint32_t parts[128];
    if (count > 128) fail(x, 0, 0, "a uniform block's array is too long to read whole");
    for (int i = 0; i < count; i++) {
        const int part = ty.kind == K_STRUCT ? ty.s->member_types[i] : ty.elem;
        const uint32_t laid = new_id(x);
        INS(&x->fn->body, OP_COMPOSITE_EXTRACT, spv_laid(x, part), laid, id, (uint32_t)i);
        parts[i] = unlaid(x, part, laid);
    }
    return emit(x, OP_COMPOSITE_CONSTRUCT, t, parts, count);
}

static val concrete(translation *x, val v, int want);

// What a reference holds, now; anything else as it is, and an abstract number made concrete.
static val load(translation *x, val v)
{
    if (v.constant) return concrete(x, v, 0);
    if (!v.ref) return v;
    if (T(x, v.t)->kind == K_TEXTURE || T(x, v.t)->kind == K_SAMPLER || T(x, v.t)->kind == K_SAMPLER_CMP) {
        return (val){.t = v.t, .id = EMIT(x, OP_LOAD, v.t, v.id)};
    }
    uint32_t id;
    if (v.sc == SC_UNIFORM && (T(x, v.t)->kind == K_STRUCT || T(x, v.t)->kind == K_ARRAY)) {
        id = new_id(x);
        INS(&x->fn->body, OP_LOAD, spv_laid(x, v.t), id, v.id);
        id = unlaid(x, v.t, id);
    } else {
        id = EMIT(x, OP_LOAD, v.t, v.id);
    }
    return (val){.t = v.t, .id = id};
}

// An abstract number as a concrete one: of the scalar `want` (or `want`'s
// scalar), or for 0 what it is by default, an i32 or an f32.
static val concrete(translation *x, val v, const int want)
{
    if (!v.constant) return v;
    const kind from = T(x, v.t)->kind;
    const int target = want ? scalar_of(x, want) : t_simple(x, from == K_AINT ? K_I32 : K_F32);
    val out = {.t = target, .is_const_id = true};
    switch (T(x, target)->kind) {
    case K_F32: {
        float f;
        if (from == K_AFLOAT && v.text) {
            char text[128];
            const int len = v.len < 127 ? v.len : 127;
            memcpy(text, v.text, (size_t)len);
            text[len] = '\0';
            f = strtof(text, NULL);
            if (v.negated) f = -f; // The literal's own f32, the other way
        } else {
            f = from == K_AINT ? (float)v.i : (float)v.f;
        }
        out.id = const_f32(x, f);
        break;
    }
    case K_I32:
        if (from != K_AINT) fail(x, 0, 0, "a float where an i32 goes");
        if (v.i < -2147483648ll || v.i > 2147483647ll) fail(x, 0, 0, "%lld is past what an i32 holds", (long long)v.i);
        out.id = const_i32(x, (int32_t)v.i);
        break;
    case K_U32:
        if (from != K_AINT) fail(x, 0, 0, "a float where a u32 goes");
        if (v.i < 0 || v.i > 4294967295ll) fail(x, 0, 0, "%lld is past what a u32 holds", (long long)v.i);
        out.id = const_u32(x, (uint32_t)v.i);
        break;
    default:
        fail(x, 0, 0, "a number where something else goes");
    }
    return out;
}

static val abstract_int(translation *x, const int64_t i)
{
    return (val){.t = t_simple(x, K_AINT), .constant = true, .i = i};
}

static val abstract_float(translation *x, const double f)
{
    return (val){.t = t_simple(x, K_AFLOAT), .constant = true, .f = f};
}

// `v`, which has to be of type `t`, an abstract number becoming one of it.
static val coerce(translation *x, val v, const int t, const int line, const int col)
{
    if (v.constant) {
        if (!is_scalar(x, t) || T(x, t)->kind == K_BOOL) {
            char want[96];
            type_name(x, t, want, sizeof want);
            fail(x, line, col, "a number where %s goes", want);
        }
        if (T(x, v.t)->kind == K_AFLOAT && !is_float(x, t)) fail(x, line, col, "a float where an int goes");
        return concrete(x, v, t);
    }
    v = load(x, v);
    if (v.t != t) {
        char want[96], found[96];
        type_name(x, t, want, sizeof want);
        type_name(x, v.t, found, sizeof found);
        fail(x, line, col, "expected %s, found %s", want, found);
    }
    return v;
}

// A scalar `v` as a vector of `n` of it.
static val splat(translation *x, const val v, const int n)
{
    if (n == 1) return v;
    const uint32_t parts[4] = {v.id, v.id, v.id, v.id};
    return (val){.t = t_vec(x, v.t, n), .id = emit(x, OP_COMPOSITE_CONSTRUCT, t_vec(x, v.t, n), parts, n)};
}

// A number's (or a vector's numbers') conversion to `scalar`, as WGSL's
// f32(), i32(), u32() and bool() do: a float past an int's range is the
// nearest it holds, as WebGPU has it.
static val convert(translation *x, val v, const int scalar, const int line, const int col)
{
    if (v.constant) {
        if (T(x, scalar)->kind == K_BOOL) {
            const bool b = T(x, v.t)->kind == K_AINT ? v.i != 0 : v.f != 0.0;
            return (val){.t = scalar, .id = const_bool(x, b), .is_const_id = true};
        }
        if (T(x, v.t)->kind == K_AFLOAT && T(x, scalar)->kind != K_F32) {
            double f = v.f;
            if (v.text) {
                char text[128];
                const int len = v.len < 127 ? v.len : 127;
                memcpy(text, v.text, (size_t)len);
                text[len] = '\0';
                f = strtod(text, NULL);
                if (v.negated) f = -f;
            }
            const double low = T(x, scalar)->kind == K_I32 ? -2147483648.0 : 0.0;
            const double high = T(x, scalar)->kind == K_I32 ? 2147483520.0 : 4294967040.0;
            f = f < low ? low : f > high ? high : trunc(f);
            return concrete(x, abstract_int(x, (int64_t)f), scalar);
        }
        return concrete(x, v, scalar);
    }
    v = load(x, v);
    const int from = scalar_of(x, v.t);
    const int n = width_of(x, v.t);
    const int to = with_scalar(x, v.t, scalar);
    const kind fk = T(x, from)->kind, tk = T(x, scalar)->kind;
    if (T(x, v.t)->kind != K_VEC && !is_scalar(x, v.t)) {
        char found[96];
        type_name(x, v.t, found, sizeof found);
        fail(x, line, col, "%s doesn't convert to a number", found);
    }
    if (fk == tk) return v;
    if (tk == K_BOOL) {
        const uint32_t zero = const_of(x, v.t, 0.0);
        const uint32_t op = fk == K_F32 ? OP_F_UNORD_NOT_EQUAL : OP_I_NOT_EQUAL;
        return (val){.t = to, .id = EMIT(x, op, to, v.id, zero)};
    }
    if (fk == K_BOOL) {
        return (val){.t = to, .id = EMIT(x, OP_SELECT, to, v.id, const_of(x, to, 1.0), const_of(x, to, 0.0))};
    }
    if (fk == K_F32) {
        // Into the int's range first: past it, SPIR-V's conversion gives anything
        const bool sign = tk == K_I32;
        const uint32_t low = const_of(x, v.t, sign ? -2147483648.0 : 0.0);
        const uint32_t high = const_of(x, v.t, sign ? 2147483520.0 : 4294967040.0);
        const uint32_t args[3] = {v.id, low, high};
        const uint32_t clamped = glsl(x, v.t, GLSL_F_CLAMP, args, 3);
        return (val){.t = to, .id = EMIT(x, sign ? OP_CONVERT_F_TO_S : OP_CONVERT_F_TO_U, to, clamped)};
    }
    if (tk == K_F32) return (val){.t = to, .id = EMIT(x, fk == K_I32 ? OP_CONVERT_S_TO_F : OP_CONVERT_U_TO_F, to, v.id)};
    (void)n;
    return (val){.t = to, .id = EMIT(x, OP_BITCAST, to, v.id)}; // i32 and u32: the same bits
}

// ---------------------------------------------------------------------------
// Expressions

static val eval(translation *x, const expr *e);

// An expression's value, read now if it's a place: what's evaluated later
// doesn't change it. Abstract numbers stay abstract.
static val rv(translation *x, const expr *e)
{
    val v = eval(x, e);
    if (v.ref) v = load(x, v);
    return v;
}

static val bool_value(translation *x, const expr *e)
{
    val v = rv(x, e);
    if (v.constant || T(x, v.t)->kind != K_BOOL) fail(x, e->line, e->col, "expected a bool");
    return v;
}

// A reference's place, through a pointer value too (`p.x` for `(*p).x`).
static val deref(translation *x, val v)
{
    if (!v.ref && !v.constant && T(x, v.t)->kind == K_PTR) {
        const type p = *T(x, v.t);
        return (val){.t = p.elem, .id = v.id, .ref = true, .sc = p.sc};
    }
    return v;
}

static val access(translation *x, const val base, const int elem, const uint32_t index)
{
    const int ptr = t_ptr(x, base.sc, elem);
    return (val){.t = elem, .id = EMIT(x, OP_ACCESS_CHAIN, ptr, base.id, index), .ref = true, .sc = base.sc};
}

static int swizzle_component(const char c)
{
    switch (c) {
    case 'x': case 'r': return 0;
    case 'y': case 'g': return 1;
    case 'z': case 'b': return 2;
    case 'w': case 'a': return 3;
    default: return -1;
    }
}

static val member_expr(translation *x, const expr *e)
{
    const val base = deref(x, eval(x, e->a));
    const type bt = *T(x, base.t);
    if (bt.kind == K_STRUCT) {
        for (int i = 0; i < bt.s->count; i++) {
            if (bt.s->members[i].len != e->len || memcmp(bt.s->members[i].name, e->name, (size_t)e->len) != 0) continue;
            const int mt = bt.s->member_types[i];
            if (base.ref) return access(x, base, mt, const_u32(x, (uint32_t)i));
            return (val){.t = mt, .id = EMIT(x, OP_COMPOSITE_EXTRACT, mt, base.id, (uint32_t)i)};
        }
        fail(x, e->line, e->col, "'%.*s' has no member '%.*s'", bt.s->len, bt.s->name, e->len, e->name);
    }
    if (bt.kind == K_VEC) {
        if (e->len < 1 || e->len > 4) fail(x, e->line, e->col, "'.%.*s' isn't a swizzle", e->len, e->name);
        uint32_t parts[4];
        for (int i = 0; i < e->len; i++) {
            const int c = swizzle_component(e->name[i]);
            if (c < 0 || c >= bt.n) fail(x, e->line, e->col, "'.%.*s' isn't a swizzle of a vec%d", e->len, e->name, bt.n);
            parts[i] = (uint32_t)c;
        }
        if (e->len == 1) {
            if (base.ref) return access(x, base, bt.elem, const_u32(x, parts[0]));
            return (val){.t = bt.elem, .id = EMIT(x, OP_COMPOSITE_EXTRACT, bt.elem, base.id, parts[0])};
        }
        const val v = load(x, base);
        const int t = t_vec(x, bt.elem, e->len);
        uint32_t operands[6] = {v.id, v.id};
        for (int i = 0; i < e->len; i++) operands[2 + i] = parts[i];
        return (val){.t = t, .id = emit(x, OP_VECTOR_SHUFFLE, t, operands, 2 + e->len)};
    }
    char found[96];
    type_name(x, base.t, found, sizeof found);
    fail(x, e->line, e->col, "%s has no members", found);
    return base;
}

// An index past the end, as WebGPU has it: the last element instead, so
// nothing past what's there is read or written.
static uint32_t clamped_index(translation *x, val i, const int count, const int line, const int col)
{
    i = load(x, i);
    if (!is_int(x, i.t) || !is_scalar(x, i.t)) fail(x, line, col, "an index is an i32 or a u32");
    const int u32 = t_simple(x, K_U32);
    uint32_t id = i.id;
    if (T(x, i.t)->kind == K_I32) id = EMIT(x, OP_BITCAST, u32, id);
    const uint32_t args[2] = {id, const_u32(x, (uint32_t)(count - 1))};
    return glsl(x, u32, GLSL_U_MIN, args, 2);
}

static val index_expr(translation *x, const expr *e)
{
    const val base = deref(x, eval(x, e->a));
    const val i = rv(x, e->b);
    const type bt = *T(x, base.t);
    int elem, count;
    switch (bt.kind) {
    case K_VEC: elem = bt.elem, count = bt.n; break;
    case K_MAT: elem = bt.elem, count = bt.n; break;
    case K_ARRAY: elem = bt.elem, count = bt.n; break;
    default: {
        char found[96];
        type_name(x, base.t, found, sizeof found);
        fail(x, e->line, e->col, "%s isn't indexed", found);
        return base;
    }
    }
    if (i.constant) {
        if (T(x, i.t)->kind != K_AINT) fail(x, e->line, e->col, "an index is an int");
        const int64_t at = i.i < 0 ? 0 : i.i >= count ? count - 1 : i.i;
        if (base.ref) return access(x, base, elem, const_u32(x, (uint32_t)at));
        return (val){.t = elem, .id = EMIT(x, OP_COMPOSITE_EXTRACT, elem, base.id, (uint32_t)at)};
    }
    const uint32_t at = clamped_index(x, i, count, e->line, e->col);
    if (base.ref) return access(x, base, elem, at);
    if (bt.kind == K_VEC) return (val){.t = elem, .id = EMIT(x, OP_VECTOR_EXTRACT_DYNAMIC, elem, base.id, at)};
    // A value's element by a number worked out as it runs: from a copy in a variable
    const uint32_t copy = local_var(x, base.t);
    INS(&x->fn->body, OP_STORE, copy, base.id);
    const val place = access(x, (val){.t = base.t, .id = copy, .ref = true, .sc = SC_FUNCTION}, elem, at);
    return load(x, place);
}

static double as_double(translation *x, const val v)
{
    if (T(x, v.t)->kind == K_AINT) return (double)v.i;
    if (v.text) {
        char text[128];
        const int len = v.len < 127 ? v.len : 127;
        memcpy(text, v.text, (size_t)len);
        text[len] = '\0';
        const double f = strtod(text, NULL);
        return v.negated ? -f : f;
    }
    return v.f;
}

// Two abstract numbers' result, worked out here.
static val fold(translation *x, const punct op, const val a, const val b, const int line, const int col)
{
    const bool floats = T(x, a.t)->kind == K_AFLOAT || T(x, b.t)->kind == K_AFLOAT;
    if (floats) {
        const double l = as_double(x, a), r = as_double(x, b);
        switch (op) {
        case P_PLUS: return abstract_float(x, l + r);
        case P_MINUS: return abstract_float(x, l - r);
        case P_STAR: return abstract_float(x, l * r);
        case P_SLASH: return abstract_float(x, l / r);
        case P_PERCENT: return abstract_float(x, fmod(l, r));
        case P_EQ: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l == r)};
        case P_NE: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l != r)};
        case P_LT: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l < r)};
        case P_LE: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l <= r)};
        case P_GT: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l > r)};
        case P_GE: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l >= r)};
        default: fail(x, line, col, "that isn't done to floats");
        }
    }
    const int64_t l = a.i, r = b.i;
    switch (op) {
    case P_PLUS: return abstract_int(x, l + r);
    case P_MINUS: return abstract_int(x, l - r);
    case P_STAR: return abstract_int(x, l * r);
    case P_SLASH:
        if (!r) fail(x, line, col, "a division by zero");
        return abstract_int(x, l / r);
    case P_PERCENT:
        if (!r) fail(x, line, col, "a remainder of a division by zero");
        return abstract_int(x, l % r);
    case P_AMP: return abstract_int(x, l & r);
    case P_PIPE: return abstract_int(x, l | r);
    case P_CARET: return abstract_int(x, l ^ r);
    case P_SHL:
        if (r < 0 || r > 62) fail(x, line, col, "a shift past the number's bits");
        return abstract_int(x, (int64_t)((uint64_t)l << r));
    case P_SHR:
        if (r < 0 || r > 62) fail(x, line, col, "a shift past the number's bits");
        return abstract_int(x, l >> r);
    case P_EQ: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l == r)};
    case P_NE: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l != r)};
    case P_LT: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l < r)};
    case P_LE: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l <= r)};
    case P_GT: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l > r)};
    case P_GE: return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, l >= r)};
    default: fail(x, line, col, "that isn't done to ints");
    }
    return a;
}

// Two values of one shape, a scalar spread to the other's vector.
static void same_shape(translation *x, val *a, val *b)
{
    const int wa = width_of(x, a->t), wb = width_of(x, b->t);
    if (wa == wb) return;
    if (wa == 1 && is_scalar(x, a->t)) *a = splat(x, *a, wb);
    else if (wb == 1 && is_scalar(x, b->t)) *b = splat(x, *b, wa);
}

static val binop(translation *x, const punct op, val a, val b, const int line, const int col)
{
    if (a.constant && b.constant) return fold(x, op, a, b, line, col);
    const bool shift = op == P_SHL || op == P_SHR;
    if (shift) {
        if (a.constant) a = concrete(x, a, 0);
        if (b.constant) b = concrete(x, b, t_simple(x, K_U32));
    } else {
        if (a.constant) a = coerce(x, a, scalar_of(x, b.t), line, col);
        if (b.constant) b = coerce(x, b, scalar_of(x, a.t), line, col);
    }
    a = load(x, a), b = load(x, b);
    const kind ka = T(x, a.t)->kind, kb = T(x, b.t)->kind;
    char la[96], lb[96];
    type_name(x, a.t, la, sizeof la);
    type_name(x, b.t, lb, sizeof lb);

    // Matrices
    if (ka == K_MAT || kb == K_MAT) {
        if (op == P_STAR) {
            if (ka == K_MAT && kb == K_VEC) {
                return (val){.t = T(x, a.t)->elem, .id = EMIT(x, OP_MATRIX_TIMES_VECTOR, T(x, a.t)->elem, a.id, b.id)};
            }
            if (ka == K_VEC && kb == K_MAT) {
                const int t = t_vec(x, t_simple(x, K_F32), T(x, b.t)->n);
                return (val){.t = t, .id = EMIT(x, OP_VECTOR_TIMES_MATRIX, t, a.id, b.id)};
            }
            if (ka == K_MAT && kb == K_MAT) {
                const int t = t_mat(x, T(x, b.t)->n, T(x, T(x, a.t)->elem)->n);
                return (val){.t = t, .id = EMIT(x, OP_MATRIX_TIMES_MATRIX, t, a.id, b.id)};
            }
            if (ka == K_MAT && kb == K_F32) return (val){.t = a.t, .id = EMIT(x, OP_MATRIX_TIMES_SCALAR, a.t, a.id, b.id)};
            if (ka == K_F32 && kb == K_MAT) return (val){.t = b.t, .id = EMIT(x, OP_MATRIX_TIMES_SCALAR, b.t, b.id, a.id)};
        }
        if ((op == P_PLUS || op == P_MINUS) && a.t == b.t) { // Column by column
            const int column = T(x, a.t)->elem, cols = T(x, a.t)->n;
            uint32_t parts[4];
            for (int i = 0; i < cols; i++) {
                const uint32_t l = EMIT(x, OP_COMPOSITE_EXTRACT, column, a.id, (uint32_t)i);
                const uint32_t r = EMIT(x, OP_COMPOSITE_EXTRACT, column, b.id, (uint32_t)i);
                parts[i] = EMIT(x, op == P_PLUS ? OP_F_ADD : OP_F_SUB, column, l, r);
            }
            return (val){.t = a.t, .id = emit(x, OP_COMPOSITE_CONSTRUCT, a.t, parts, cols)};
        }
        fail(x, line, col, "%s and %s don't go together that way", la, lb);
    }
    if (!(is_scalar(x, a.t) || ka == K_VEC) || !(is_scalar(x, b.t) || kb == K_VEC)) {
        fail(x, line, col, "%s and %s don't go together that way", la, lb);
    }

    if (shift) {
        if (!is_int(x, a.t) || !is_int(x, b.t)) fail(x, line, col, "only ints shift");
        same_shape(x, &a, &b);
        // By the count's low 5 bits, as WebGPU shifts: past 31, SPIR-V's give anything
        const int count_t = with_scalar(x, b.t, scalar_of(x, b.t));
        const uint32_t masked = EMIT(x, OP_BITWISE_AND, count_t, b.id, const_of(x, count_t, 31.0));
        const uint32_t sop = op == P_SHL ? OP_SHIFT_LEFT_LOGICAL : is_signed(x, a.t) ? OP_SHIFT_RIGHT_ARITHMETIC : OP_SHIFT_RIGHT_LOGICAL;
        return (val){.t = a.t, .id = EMIT(x, sop, a.t, a.id, masked)};
    }

    if (scalar_of(x, a.t) != scalar_of(x, b.t)) fail(x, line, col, "%s and %s don't go together", la, lb);
    const int scalar = scalar_of(x, a.t);
    const kind sk = T(x, scalar)->kind;

    // Comparisons
    if (op == P_EQ || op == P_NE || op == P_LT || op == P_LE || op == P_GT || op == P_GE) {
        same_shape(x, &a, &b);
        if (a.t != b.t) fail(x, line, col, "%s and %s don't compare", la, lb);
        const int result = with_scalar(x, a.t, t_simple(x, K_BOOL));
        uint32_t o;
        if (sk == K_BOOL) {
            if (op != P_EQ && op != P_NE) fail(x, line, col, "bools are equal or not, no more");
            o = op == P_EQ ? OP_LOGICAL_EQUAL : OP_LOGICAL_NOT_EQUAL;
        } else if (sk == K_F32) {
            o = op == P_EQ ? OP_F_ORD_EQUAL : op == P_NE ? OP_F_UNORD_NOT_EQUAL : op == P_LT ? OP_F_ORD_LESS_THAN
              : op == P_LE ? OP_F_ORD_LESS_THAN_EQUAL : op == P_GT ? OP_F_ORD_GREATER_THAN : OP_F_ORD_GREATER_THAN_EQUAL;
        } else if (sk == K_I32) {
            o = op == P_EQ ? OP_I_EQUAL : op == P_NE ? OP_I_NOT_EQUAL : op == P_LT ? OP_S_LESS_THAN
              : op == P_LE ? OP_S_LESS_THAN_EQUAL : op == P_GT ? OP_S_GREATER_THAN : OP_S_GREATER_THAN_EQUAL;
        } else {
            o = op == P_EQ ? OP_I_EQUAL : op == P_NE ? OP_I_NOT_EQUAL : op == P_LT ? OP_U_LESS_THAN
              : op == P_LE ? OP_U_LESS_THAN_EQUAL : op == P_GT ? OP_U_GREATER_THAN : OP_U_GREATER_THAN_EQUAL;
        }
        return (val){.t = result, .id = EMIT(x, o, result, a.id, b.id)};
    }

    // Bits, and bools' & and |
    if (op == P_AMP || op == P_PIPE || op == P_CARET) {
        same_shape(x, &a, &b);
        if (sk == K_BOOL) {
            const uint32_t o = op == P_AMP ? OP_LOGICAL_AND : op == P_PIPE ? OP_LOGICAL_OR : OP_LOGICAL_NOT_EQUAL;
            return (val){.t = a.t, .id = EMIT(x, o, a.t, a.id, b.id)};
        }
        if (sk == K_F32) fail(x, line, col, "floats have no bits to do that to");
        const uint32_t o = op == P_AMP ? OP_BITWISE_AND : op == P_PIPE ? OP_BITWISE_OR : OP_BITWISE_XOR;
        return (val){.t = a.t, .id = EMIT(x, o, a.t, a.id, b.id)};
    }

    if (sk == K_BOOL) fail(x, line, col, "bools don't do arithmetic");
    // A float vector times a scalar has an instruction of its own
    if (op == P_STAR && sk == K_F32 && (ka == K_VEC) != (kb == K_VEC)) {
        const val v = ka == K_VEC ? a : b, s = ka == K_VEC ? b : a;
        return (val){.t = v.t, .id = EMIT(x, OP_VECTOR_TIMES_SCALAR, v.t, v.id, s.id)};
    }
    same_shape(x, &a, &b);
    if (a.t != b.t) fail(x, line, col, "%s and %s don't go together", la, lb);
    const int t = a.t;
    if (sk == K_F32) {
        const uint32_t o = op == P_PLUS ? OP_F_ADD : op == P_MINUS ? OP_F_SUB : op == P_STAR ? OP_F_MUL
                         : op == P_SLASH ? OP_F_DIV : OP_F_REM;
        return (val){.t = t, .id = EMIT(x, o, t, a.id, b.id)};
    }
    if (op == P_PLUS || op == P_MINUS || op == P_STAR) {
        const uint32_t o = op == P_PLUS ? OP_I_ADD : op == P_MINUS ? OP_I_SUB : OP_I_MUL;
        return (val){.t = t, .id = EMIT(x, o, t, a.id, b.id)};
    }
    // Division and remainder, as WebGPU has them: by zero, or the least i32 by -1, as by 1
    const int bools = with_scalar(x, t, t_simple(x, K_BOOL));
    uint32_t odd = EMIT(x, OP_I_EQUAL, bools, b.id, const_of(x, t, 0.0));
    if (sk == K_I32) {
        const uint32_t least = EMIT(x, OP_I_EQUAL, bools, a.id, const_of(x, t, -2147483648.0));
        const uint32_t minus = EMIT(x, OP_I_EQUAL, bools, b.id, const_of(x, t, -1.0));
        odd = EMIT(x, OP_LOGICAL_OR, bools, odd, EMIT(x, OP_LOGICAL_AND, bools, least, minus));
    }
    const uint32_t by = EMIT(x, OP_SELECT, t, odd, const_of(x, t, 1.0), b.id);
    const uint32_t o = op == P_SLASH ? (sk == K_I32 ? OP_S_DIV : OP_U_DIV) : (sk == K_I32 ? OP_S_REM : OP_U_MOD);
    return (val){.t = t, .id = EMIT(x, o, t, a.id, by)};
}

// Whether an expression may do something besides giving its value: calls a
// function of the module's, which may write through a pointer or discard.
static bool has_effects(translation *x, const expr *e)
{
    if (!e) return false;
    if (e->kind == X_CALL && !e->type && find_fn(x, e->name, e->len)) return true;
    if (has_effects(x, e->a) || has_effects(x, e->b)) return true;
    for (int i = 0; i < e->nargs; i++) {
        if (has_effects(x, e->args[i])) return true;
    }
    return false;
}

// && and ||, which only work out their right side where the left doesn't decide.
static val logical(translation *x, const expr *e)
{
    const bool and = e->op == P_ANDAND;
    const val a = bool_value(x, e->a);
    if (!has_effects(x, e->b)) {
        const val b = bool_value(x, e->b);
        if (a.t != b.t) fail(x, e->line, e->col, "&& and || take two bools");
        return (val){.t = a.t, .id = EMIT(x, and ? OP_LOGICAL_AND : OP_LOGICAL_OR, a.t, a.id, b.id)};
    }
    if (T(x, a.t)->kind != K_BOOL) fail(x, e->line, e->col, "&& and || take two bools");
    const uint32_t from = x->fn->label, right = new_id(x), merge = new_id(x);
    INS(&x->fn->body, OP_SELECTION_MERGE, merge, 0);
    if (and) INS(&x->fn->body, OP_BRANCH_CONDITIONAL, a.id, right, merge);
    else INS(&x->fn->body, OP_BRANCH_CONDITIONAL, a.id, merge, right);
    x->fn->ended = true;
    start_block(x, right);
    const val b = bool_value(x, e->b);
    if (T(x, b.t)->kind != K_BOOL) fail(x, e->line, e->col, "&& and || take two bools");
    const uint32_t right_end = x->fn->label;
    branch(x, merge);
    start_block(x, merge);
    return (val){.t = a.t, .id = EMIT(x, OP_PHI, a.t, const_bool(x, !and), from, b.id, right_end)};
}

static val call(translation *x, const expr *e);

static val unary(translation *x, const expr *e)
{
    val v = rv(x, e->a);
    if (e->op == P_MINUS) {
        if (v.constant) {
            if (T(x, v.t)->kind == K_AINT) v.i = -v.i;
            else if (v.text) v.negated = !v.negated;
            else v.f = -v.f;
            return v;
        }
        const type ty = *T(x, v.t);
        if (ty.kind == K_MAT) {
            uint32_t parts[4];
            for (int i = 0; i < ty.n; i++) {
                const uint32_t c = EMIT(x, OP_COMPOSITE_EXTRACT, ty.elem, v.id, (uint32_t)i);
                parts[i] = EMIT(x, OP_F_NEGATE, ty.elem, c);
            }
            return (val){.t = v.t, .id = emit(x, OP_COMPOSITE_CONSTRUCT, v.t, parts, ty.n)};
        }
        if (is_float(x, v.t)) return (val){.t = v.t, .id = EMIT(x, OP_F_NEGATE, v.t, v.id)};
        if (is_int(x, v.t)) return (val){.t = v.t, .id = EMIT(x, OP_S_NEGATE, v.t, v.id)};
        fail(x, e->line, e->col, "that has no negative");
    }
    if (e->op == P_BANG) {
        if (v.constant || !is_bool(x, v.t)) fail(x, e->line, e->col, "'!' takes a bool");
        return (val){.t = v.t, .id = EMIT(x, OP_LOGICAL_NOT, v.t, v.id)};
    }
    if (v.constant) {
        if (T(x, v.t)->kind != K_AINT) fail(x, e->line, e->col, "'~' takes an int");
        v.i = ~v.i;
        return v;
    }
    if (!is_int(x, v.t)) fail(x, e->line, e->col, "'~' takes an int");
    return (val){.t = v.t, .id = EMIT(x, OP_NOT, v.t, v.id)};
}

static val eval(translation *x, const expr *e)
{
    switch (e->kind) {
    case X_INT:
        if (e->suffix == 'i') {
            if (e->i > 2147483647ll) fail(x, e->line, e->col, "%lld is past what an i32 holds", (long long)e->i);
            return (val){.t = t_simple(x, K_I32), .id = const_i32(x, (int32_t)e->i), .is_const_id = true};
        }
        if (e->suffix == 'u') {
            if (e->i > 4294967295ll) fail(x, e->line, e->col, "%lld is past what a u32 holds", (long long)e->i);
            return (val){.t = t_simple(x, K_U32), .id = const_u32(x, (uint32_t)e->i), .is_const_id = true};
        }
        return abstract_int(x, e->i);
    case X_FLOAT: {
        if (e->suffix == 'h') fail(x, e->line, e->col, "f16 isn't taken here");
        val v = abstract_float(x, e->f);
        v.text = e->name, v.len = e->len;
        if (e->suffix == 'f') return concrete(x, v, t_simple(x, K_F32));
        return v;
    }
    case X_BOOL:
        return (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, e->i != 0), .is_const_id = true};
    case X_NAME: {
        const binding *b = lookup(x, e->name, e->len);
        if (!b) fail(x, e->line, e->col, "'%.*s' isn't declared", e->len, e->name);
        return b->v;
    }
    case X_MEMBER:
        return member_expr(x, e);
    case X_INDEX:
        return index_expr(x, e);
    case X_UNARY:
        return unary(x, e);
    case X_DEREF: {
        const val p = rv(x, e->a);
        if (p.constant || T(x, p.t)->kind != K_PTR) fail(x, e->line, e->col, "'*' takes a pointer");
        return deref(x, p);
    }
    case X_ADDR: {
        const val r = eval(x, e->a);
        if (!r.ref) fail(x, e->line, e->col, "'&' takes a variable");
        return (val){.t = t_ptr(x, r.sc, r.t), .id = r.id};
    }
    case X_BINARY: {
        if (e->op == P_ANDAND || e->op == P_OROR) return logical(x, e);
        const val a = rv(x, e->a);
        const val b = rv(x, e->b);
        return binop(x, e->op, a, b, e->line, e->col);
    }
    case X_CALL:
        return call(x, e);
    }
    fail(x, e->line, e->col, "an expression this doesn't take");
    return (val){0};
}

// ---------------------------------------------------------------------------
// Calls

// The scalar a call's numbers come to: the first concrete one's, or by
// default an f32 if any is a float (or `floats` says the function takes
// floats alone), else an i32.
static int common_scalar(translation *x, const val *args, const int n, const bool floats)
{
    bool any_float = floats;
    for (int i = 0; i < n; i++) {
        if (!args[i].constant) return scalar_of(x, args[i].t);
        if (T(x, args[i].t)->kind == K_AFLOAT) any_float = true;
    }
    return t_simple(x, any_float ? K_F32 : K_I32);
}

// Each of the arguments a number of `scalar`, and of one width: the widest's.
static int unify(translation *x, val *args, const int n, const int scalar, const int line, const int col)
{
    int width = 1;
    for (int i = 0; i < n; i++) {
        if (args[i].constant) args[i] = coerce(x, args[i], scalar, line, col);
        args[i] = load(x, args[i]);
        if (width_of(x, args[i].t) > width) width = width_of(x, args[i].t);
    }
    for (int i = 0; i < n; i++) {
        if (scalar_of(x, args[i].t) != scalar) {
            char want[96], found[96];
            type_name(x, scalar, want, sizeof want);
            type_name(x, args[i].t, found, sizeof found);
            fail(x, line, col, "expected %s, found %s", want, found);
        }
        if (width_of(x, args[i].t) != width) {
            if (!is_scalar(x, args[i].t)) fail(x, line, col, "vectors of different sizes");
            args[i] = splat(x, args[i], width);
        }
    }
    return t_vec(x, scalar, width);
}

static void signature(translation *x, fdecl *f)
{
    if (f->typed) return;
    f->result_type = f->result ? resolve(x, f->result) : t_simple(x, K_VOID);
    f->param_types = grab(&x->a, sizeof(int) * (size_t)(f->count ? f->count : 1));
    uint32_t params[64];
    for (int i = 0; i < f->count; i++) {
        f->param_types[i] = resolve(x, f->params[i].type);
        params[i] = spv_type(x, f->param_types[i]);
    }
    if (f->at.stage) f->type_id = function_type(x, spv_type(x, t_simple(x, K_VOID)), NULL, 0);
    else f->type_id = function_type(x, spv_type(x, f->result_type), params, f->count);
    f->typed = true;
}

static val call_user(translation *x, fdecl *f, const expr *e)
{
    signature(x, f);
    if (f->at.stage) fail(x, e->line, e->col, "'%.*s' is an entry point, which nothing calls", f->len, f->name);
    if (e->nargs != f->count) {
        fail(x, e->line, e->col, "'%.*s' takes %d arguments, not %d", f->len, f->name, f->count, e->nargs);
    }
    uint32_t operands[65];
    operands[0] = f->id;
    for (int i = 0; i < f->count; i++) {
        const int pt = f->param_types[i];
        if (T(x, pt)->kind == K_PTR) {
            const val p = eval(x, e->args[i]);
            if (p.ref || p.constant || p.t != pt) fail(x, e->args[i]->line, e->args[i]->col, "expected a pointer of the parameter's type");
            operands[1 + i] = p.id;
        } else {
            operands[1 + i] = coerce(x, rv(x, e->args[i]), pt, e->args[i]->line, e->args[i]->col).id;
        }
    }
    return (val){.t = f->result_type, .id = emit(x, OP_FUNCTION_CALL, f->result_type, operands, f->count + 1)};
}

static void eval_args(translation *x, const expr *e, val *args, const int most)
{
    if (e->nargs > most) fail(x, e->line, e->col, "'%.*s' takes %d arguments at most", e->len, e->name, most);
    for (int i = 0; i < e->nargs; i++) args[i] = rv(x, e->args[i]);
}

static val zero(translation *x, const int t)
{
    return (val){.t = t, .id = const_null(x, t), .is_const_id = true};
}

static val construct_vector(translation *x, const expr *e, int scalar, const int n)
{
    val args[16];
    eval_args(x, e, args, 16);
    if (!scalar) scalar = e->nargs ? common_scalar(x, args, e->nargs, false) : t_simple(x, K_F32);
    const int t = t_vec(x, scalar, n);
    if (e->nargs == 0) return zero(x, t);
    if (e->nargs == 1 && (args[0].constant || is_scalar(x, args[0].t))) { // One number for every element
        return splat(x, convert(x, args[0], scalar, e->line, e->col), n);
    }
    if (e->nargs == 1 && width_of(x, args[0].t) == n) return convert(x, args[0], scalar, e->line, e->col);
    uint32_t parts[16];
    int count = 0;
    for (int i = 0; i < e->nargs; i++) {
        const val v = convert(x, args[i], scalar, e->args[i]->line, e->args[i]->col);
        if (T(x, v.t)->kind != K_VEC && !is_scalar(x, v.t)) fail(x, e->line, e->col, "a vector is made of numbers and vectors");
        count += width_of(x, v.t);
        parts[i] = v.id;
    }
    if (count != n) fail(x, e->line, e->col, "a vec%d needs %d numbers, not %d", n, n, count);
    return (val){.t = t, .id = emit(x, OP_COMPOSITE_CONSTRUCT, t, parts, e->nargs)};
}

static val construct_matrix(translation *x, const expr *e, const int cols, const int rows)
{
    const int t = t_mat(x, cols, rows);
    val args[16];
    eval_args(x, e, args, 16);
    if (e->nargs == 0) return zero(x, t);
    if (e->nargs == 1 && !args[0].constant && args[0].t == t) return args[0];
    const int f32 = t_simple(x, K_F32);
    const int column = t_vec(x, f32, rows);
    uint32_t columns[4];
    if (e->nargs == cols) {
        for (int i = 0; i < cols; i++) columns[i] = coerce(x, args[i], column, e->line, e->col).id;
    } else if (e->nargs == cols * rows) {
        for (int c = 0; c < cols; c++) {
            uint32_t parts[4];
            for (int r = 0; r < rows; r++) parts[r] = coerce(x, args[c * rows + r], f32, e->line, e->col).id;
            columns[c] = emit(x, OP_COMPOSITE_CONSTRUCT, column, parts, rows);
        }
    } else {
        fail(x, e->line, e->col, "a mat%dx%df is made of %d columns or %d numbers", cols, rows, cols, cols * rows);
    }
    return (val){.t = t, .id = emit(x, OP_COMPOSITE_CONSTRUCT, t, columns, cols)};
}

static val construct_array(translation *x, const expr *e, int elem, const int count)
{
    if (e->nargs == 0) {
        if (!elem || !count) fail(x, e->line, e->col, "an empty array says its type and count");
        return zero(x, t_array(x, elem, count));
    }
    if (count && count != e->nargs) fail(x, e->line, e->col, "an array of %d is given %d elements", count, e->nargs);
    uint32_t *parts = grab(&x->a, sizeof(uint32_t) * (size_t)e->nargs);
    for (int i = 0; i < e->nargs; i++) {
        val v = rv(x, e->args[i]);
        if (!elem) {
            v = load(x, v);
            elem = v.t;
        }
        parts[i] = coerce(x, v, elem, e->args[i]->line, e->args[i]->col).id;
    }
    const int t = t_array(x, elem, e->nargs);
    return (val){.t = t, .id = emit(x, OP_COMPOSITE_CONSTRUCT, t, parts, e->nargs)};
}

static val construct_struct(translation *x, const expr *e, const int t)
{
    const sdecl *s = T(x, t)->s;
    if (e->nargs == 0) return zero(x, t);
    if (e->nargs != s->count) fail(x, e->line, e->col, "'%.*s' has %d members, not %d", s->len, s->name, s->count, e->nargs);
    uint32_t *parts = grab(&x->a, sizeof(uint32_t) * (size_t)e->nargs);
    for (int i = 0; i < e->nargs; i++) {
        parts[i] = coerce(x, rv(x, e->args[i]), s->member_types[i], e->args[i]->line, e->args[i]->col).id;
    }
    return (val){.t = t, .id = emit(x, OP_COMPOSITE_CONSTRUCT, t, parts, e->nargs)};
}

// A call of a type, which makes a value of it, if `e` is one.
static bool construct(translation *x, const expr *e, val *out)
{
    const tnode *n = e->type;
    tnode plain = {.name = e->name, .len = e->len, .line = e->line, .col = e->col};
    if (!n) n = &plain;
    const char *s = n->name;
    const int len = n->len;
    // Those whose template may be left out: vec3(...), array(...), mat3x3(...)
    if (!n->nargs) {
        if (len == 4 && memcmp(s, "vec", 3) == 0 && s[3] >= '2' && s[3] <= '4') {
            *out = construct_vector(x, e, 0, s[3] - '0');
            return true;
        }
        if (same(s, len, "array")) {
            *out = construct_array(x, e, 0, 0);
            return true;
        }
        if (len == 6 && memcmp(s, "mat", 3) == 0 && s[4] == 'x') {
            *out = construct_matrix(x, e, s[3] - '0', s[5] - '0');
            return true;
        }
    }
    const int t = resolve_maybe(x, n);
    if (!t) return false;
    const type ty = *T(x, t);
    switch (ty.kind) {
    case K_BOOL:
    case K_I32:
    case K_U32:
    case K_F32: {
        val args[1];
        eval_args(x, e, args, 1);
        *out = e->nargs ? convert(x, args[0], t, e->line, e->col) : zero(x, t);
        return true;
    }
    case K_VEC: *out = construct_vector(x, e, ty.elem, ty.n); return true;
    case K_MAT: *out = construct_matrix(x, e, ty.n, T(x, ty.elem)->n); return true;
    case K_ARRAY: *out = construct_array(x, e, ty.elem, ty.n); return true;
    case K_STRUCT: *out = construct_struct(x, e, t); return true;
    default: fail(x, e->line, e->col, "a value of that type isn't made by a call");
    }
    return false;
}

static val bitcast(translation *x, const expr *e)
{
    if (!e->type || e->type->nargs != 1) fail(x, e->line, e->col, "bitcast says the type it gives");
    const int to = resolve(x, e->type->args[0]);
    val args[1];
    eval_args(x, e, args, 1);
    if (e->nargs != 1) fail(x, e->line, e->col, "bitcast takes one value");
    val v = args[0];
    if (v.constant) {
        if (T(x, v.t)->kind == K_AFLOAT) v = concrete(x, v, t_simple(x, K_F32));
        else v = concrete(x, v, t_simple(x, v.i > 2147483647ll ? K_U32 : K_I32));
    }
    v = load(x, v);
    if (v.t == to) return v;
    if (width_of(x, v.t) != width_of(x, to) || (!is_scalar(x, to) && T(x, to)->kind != K_VEC)) {
        fail(x, e->line, e->col, "bitcast takes a value of as many bits");
    }
    return (val){.t = to, .id = EMIT(x, OP_BITCAST, to, v.id)};
}

// A math function GLSL.std.450 has, by the kind of numbers it's given.
typedef struct math_fn {
    const char *name;
    int args;
    int f, s, u;  // Its instruction for floats, i32s and u32s: 0 for none, -1 to give the value back as it is
    bool floats;  // Its abstract numbers are floats
    int result;   // 0: the arguments' type; 1: a scalar of theirs
} math_fn;

static const math_fn math_fns[] = {
    {"abs", 1, GLSL_F_ABS, GLSL_S_ABS, -1, false, 0},
    {"sign", 1, GLSL_F_SIGN, GLSL_S_SIGN, 0, false, 0},
    {"min", 2, GLSL_F_MIN, GLSL_S_MIN, GLSL_U_MIN, false, 0},
    {"max", 2, GLSL_F_MAX, GLSL_S_MAX, GLSL_U_MAX, false, 0},
    {"clamp", 3, GLSL_F_CLAMP, GLSL_S_CLAMP, GLSL_U_CLAMP, false, 0},
    {"floor", 1, GLSL_FLOOR, 0, 0, true, 0},
    {"ceil", 1, GLSL_CEIL, 0, 0, true, 0},
    {"round", 1, GLSL_ROUND_EVEN, 0, 0, true, 0},
    {"trunc", 1, GLSL_TRUNC, 0, 0, true, 0},
    {"fract", 1, GLSL_FRACT, 0, 0, true, 0},
    {"sqrt", 1, GLSL_SQRT, 0, 0, true, 0},
    {"inverseSqrt", 1, GLSL_INVERSE_SQRT, 0, 0, true, 0},
    {"radians", 1, GLSL_RADIANS, 0, 0, true, 0},
    {"degrees", 1, GLSL_DEGREES, 0, 0, true, 0},
    {"sin", 1, GLSL_SIN, 0, 0, true, 0},
    {"cos", 1, GLSL_COS, 0, 0, true, 0},
    {"tan", 1, GLSL_TAN, 0, 0, true, 0},
    {"asin", 1, GLSL_ASIN, 0, 0, true, 0},
    {"acos", 1, GLSL_ACOS, 0, 0, true, 0},
    {"atan", 1, GLSL_ATAN, 0, 0, true, 0},
    {"sinh", 1, GLSL_SINH, 0, 0, true, 0},
    {"cosh", 1, GLSL_COSH, 0, 0, true, 0},
    {"tanh", 1, GLSL_TANH, 0, 0, true, 0},
    {"asinh", 1, GLSL_ASINH, 0, 0, true, 0},
    {"acosh", 1, GLSL_ACOSH, 0, 0, true, 0},
    {"atanh", 1, GLSL_ATANH, 0, 0, true, 0},
    {"atan2", 2, GLSL_ATAN2, 0, 0, true, 0},
    {"pow", 2, GLSL_POW, 0, 0, true, 0},
    {"exp", 1, GLSL_EXP, 0, 0, true, 0},
    {"exp2", 1, GLSL_EXP2, 0, 0, true, 0},
    {"log", 1, GLSL_LOG, 0, 0, true, 0},
    {"log2", 1, GLSL_LOG2, 0, 0, true, 0},
    {"step", 2, GLSL_STEP, 0, 0, true, 0},
    {"smoothstep", 3, GLSL_SMOOTH_STEP, 0, 0, true, 0},
    {"fma", 3, GLSL_FMA, 0, 0, true, 0},
    {"normalize", 1, GLSL_NORMALIZE, 0, 0, true, 0},
    {"length", 1, GLSL_LENGTH, 0, 0, true, 1},
    {"distance", 2, GLSL_DISTANCE, 0, 0, true, 1},
    {"cross", 2, GLSL_CROSS, 0, 0, true, 0},
    {"reflect", 2, GLSL_REFLECT, 0, 0, true, 0},
    {"faceForward", 3, GLSL_FACE_FORWARD, 0, 0, true, 0},
};

static val texture_call(translation *x, const expr *e);

static val builtin(translation *x, const expr *e)
{
    const char *n = e->name;
    const int len = e->len;
    if (len > 7 && memcmp(n, "texture", 7) == 0) return texture_call(x, e);
    val args[8];
    eval_args(x, e, args, 8);
    const int count = e->nargs;
    const int line = e->line, col = e->col;
    for (size_t i = 0; i < sizeof math_fns / sizeof math_fns[0]; i++) {
        const math_fn *m = &math_fns[i];
        if (!same(n, len, m->name)) continue;
        if (count != m->args) fail(x, line, col, "%s takes %d arguments", m->name, m->args);
        const int scalar = common_scalar(x, args, count, m->floats);
        const int t = unify(x, args, count, scalar, line, col);
        const kind k = T(x, scalar)->kind;
        const int inst = k == K_F32 ? m->f : k == K_I32 ? m->s : k == K_U32 ? m->u : 0;
        if (inst == -1) return args[0];
        if (!inst) fail(x, line, col, "%s doesn't take that kind of number", m->name);
        uint32_t ids[3];
        for (int a = 0; a < count; a++) ids[a] = args[a].id;
        const int result = m->result ? scalar : t;
        return (val){.t = result, .id = glsl(x, result, (uint32_t)inst, ids, count)};
    }
    if (same(n, len, "saturate")) {
        if (count != 1) fail(x, line, col, "saturate takes one value");
        const int t = unify(x, args, 1, t_simple(x, K_F32), line, col);
        const uint32_t ids[3] = {args[0].id, const_of(x, t, 0.0), const_of(x, t, 1.0)};
        return (val){.t = t, .id = glsl(x, t, GLSL_F_CLAMP, ids, 3)};
    }
    if (same(n, len, "mix")) {
        if (count != 3) fail(x, line, col, "mix takes three values");
        const int t = unify(x, args, 3, t_simple(x, K_F32), line, col);
        const uint32_t ids[3] = {args[0].id, args[1].id, args[2].id};
        return (val){.t = t, .id = glsl(x, t, GLSL_F_MIX, ids, 3)};
    }
    if (same(n, len, "refract")) {
        if (count != 3) fail(x, line, col, "refract takes three values");
        const int f32 = t_simple(x, K_F32);
        const val eta = coerce(x, args[2], f32, line, col);
        const int t = unify(x, args, 2, f32, line, col);
        const uint32_t ids[3] = {args[0].id, args[1].id, eta.id};
        return (val){.t = t, .id = glsl(x, t, GLSL_REFRACT, ids, 3)};
    }
    if (same(n, len, "ldexp")) {
        if (count != 2) fail(x, line, col, "ldexp takes two values");
        const val a = load(x, args[0].constant ? concrete(x, args[0], t_simple(x, K_F32)) : args[0]);
        const val b = load(x, args[1].constant ? concrete(x, args[1], t_simple(x, K_I32)) : args[1]);
        const uint32_t ids[2] = {a.id, b.id};
        return (val){.t = a.t, .id = glsl(x, a.t, GLSL_LDEXP, ids, 2)};
    }
    if (same(n, len, "dot")) {
        if (count != 2) fail(x, line, col, "dot takes two vectors");
        const int scalar = common_scalar(x, args, 2, false);
        unify(x, args, 2, scalar, line, col);
        if (T(x, scalar)->kind == K_F32) return (val){.t = scalar, .id = EMIT(x, OP_DOT, scalar, args[0].id, args[1].id)};
        const int w = width_of(x, args[0].t);
        const uint32_t product = EMIT(x, OP_I_MUL, args[0].t, args[0].id, args[1].id);
        uint32_t sum = EMIT(x, OP_COMPOSITE_EXTRACT, scalar, product, 0);
        for (int i = 1; i < w; i++) sum = EMIT(x, OP_I_ADD, scalar, sum, EMIT(x, OP_COMPOSITE_EXTRACT, scalar, product, (uint32_t)i));
        return (val){.t = scalar, .id = sum};
    }
    if (same(n, len, "select")) {
        if (count != 3) fail(x, line, col, "select takes three values");
        val c = load(x, args[2]);
        if (!is_bool(x, c.t)) fail(x, line, col, "select's last value is a bool");
        val f = args[0], t = args[1];
        if (f.constant && t.constant) {
            const int scalar = common_scalar(x, args, 2, false);
            f = concrete(x, f, scalar), t = concrete(x, t, scalar);
        }
        if (f.constant) f = concrete(x, f, load(x, t).t);
        if (t.constant) t = concrete(x, t, f.t);
        f = load(x, f), t = load(x, t);
        if (f.t != t.t) fail(x, line, col, "select picks between two values of one type");
        if (!is_scalar(x, f.t) && T(x, f.t)->kind != K_VEC) fail(x, line, col, "select picks between scalars or vectors");
        if (width_of(x, c.t) != width_of(x, f.t)) c = splat(x, c, width_of(x, f.t));
        return (val){.t = f.t, .id = EMIT(x, OP_SELECT, f.t, c.id, t.id, f.id)};
    }
    if (same(n, len, "all") || same(n, len, "any")) {
        if (count != 1) fail(x, line, col, "%.*s takes one value", len, n);
        const val v = load(x, args[0]);
        if (!is_bool(x, v.t)) fail(x, line, col, "%.*s takes bools", len, n);
        if (is_scalar(x, v.t)) return v;
        const int b = t_simple(x, K_BOOL);
        return (val){.t = b, .id = EMIT(x, n[1] == 'l' ? OP_ALL : OP_ANY, b, v.id)};
    }
    if (same(n, len, "transpose") || same(n, len, "determinant")) {
        if (count != 1) fail(x, line, col, "%.*s takes a matrix", len, n);
        const val m = load(x, args[0]);
        if (T(x, m.t)->kind != K_MAT) fail(x, line, col, "%.*s takes a matrix", len, n);
        if (n[0] == 'd') {
            const int f32 = t_simple(x, K_F32);
            const uint32_t ids[1] = {m.id};
            return (val){.t = f32, .id = glsl(x, f32, GLSL_DETERMINANT, ids, 1)};
        }
        const int t = t_mat(x, T(x, T(x, m.t)->elem)->n, T(x, m.t)->n);
        return (val){.t = t, .id = EMIT(x, OP_TRANSPOSE, t, m.id)};
    }
    static const struct {
        const char *name;
        int op;
    } derivatives[] = {
        {"dpdx", OP_DPDX}, {"dpdy", OP_DPDY}, {"fwidth", OP_FWIDTH}, {"dpdxFine", OP_DPDX_FINE},
        {"dpdyFine", OP_DPDY_FINE}, {"fwidthFine", OP_FWIDTH_FINE}, {"dpdxCoarse", OP_DPDX_COARSE},
        {"dpdyCoarse", OP_DPDY_COARSE}, {"fwidthCoarse", OP_FWIDTH_COARSE},
    };
    for (size_t i = 0; i < sizeof derivatives / sizeof derivatives[0]; i++) {
        if (!same(n, len, derivatives[i].name)) continue;
        if (count != 1) fail(x, line, col, "%s takes one value", derivatives[i].name);
        const int t = unify(x, args, 1, t_simple(x, K_F32), line, col);
        if (derivatives[i].op >= OP_DPDX_FINE) x->cap_derivative_control = true;
        return (val){.t = t, .id = EMIT(x, (uint32_t)derivatives[i].op, t, args[0].id)};
    }
    if (same(n, len, "countOneBits") || same(n, len, "reverseBits")) {
        if (count != 1) fail(x, line, col, "%.*s takes one value", len, n);
        const int t = unify(x, args, 1, common_scalar(x, args, 1, false), line, col);
        if (!is_int(x, t)) fail(x, line, col, "%.*s takes ints", len, n);
        return (val){.t = t, .id = EMIT(x, n[0] == 'c' ? OP_BIT_COUNT : OP_BIT_REVERSE, t, args[0].id)};
    }
    if (same(n, len, "firstLeadingBit") || same(n, len, "firstTrailingBit")) {
        if (count != 1) fail(x, line, col, "%.*s takes one value", len, n);
        const int t = unify(x, args, 1, common_scalar(x, args, 1, false), line, col);
        if (!is_int(x, t)) fail(x, line, col, "%.*s takes ints", len, n);
        const uint32_t inst = n[5] == 'T' ? GLSL_FIND_I_LSB : is_signed(x, t) ? GLSL_FIND_S_MSB : GLSL_FIND_U_MSB;
        const uint32_t ids[1] = {args[0].id};
        return (val){.t = t, .id = glsl(x, t, inst, ids, 1)};
    }
    if (same(n, len, "extractBits") || same(n, len, "insertBits")) {
        const bool insert = n[0] == 'i';
        if (count != (insert ? 4 : 3)) fail(x, line, col, "%.*s takes %d values", len, n, insert ? 4 : 3);
        const int u32 = t_simple(x, K_U32);
        const int values = insert ? 2 : 1;
        const int t = unify(x, args, values, common_scalar(x, args, values, false), line, col);
        if (!is_int(x, t)) fail(x, line, col, "%.*s takes ints", len, n);
        const val offset = coerce(x, args[values], u32, line, col);
        const val bits = coerce(x, args[values + 1], u32, line, col);
        if (insert) {
            return (val){.t = t, .id = EMIT(x, OP_BIT_FIELD_INSERT, t, args[0].id, args[1].id, offset.id, bits.id)};
        }
        return (val){.t = t, .id = EMIT(x, is_signed(x, t) ? OP_BIT_FIELD_S_EXTRACT : OP_BIT_FIELD_U_EXTRACT, t,
                                         args[0].id, offset.id, bits.id)};
    }
    static const struct {
        const char *name;
        int inst, from, to; // From and to: a vec4f, vec2f, or u32 (0)
    } packs[] = {
        {"pack4x8snorm", GLSL_PACK_SNORM4X8, 4, 0}, {"pack4x8unorm", GLSL_PACK_UNORM4X8, 4, 0},
        {"pack2x16snorm", GLSL_PACK_SNORM2X16, 2, 0}, {"pack2x16unorm", GLSL_PACK_UNORM2X16, 2, 0},
        {"pack2x16float", GLSL_PACK_HALF2X16, 2, 0}, {"unpack4x8snorm", GLSL_UNPACK_SNORM4X8, 0, 4},
        {"unpack4x8unorm", GLSL_UNPACK_UNORM4X8, 0, 4}, {"unpack2x16snorm", GLSL_UNPACK_SNORM2X16, 0, 2},
        {"unpack2x16unorm", GLSL_UNPACK_UNORM2X16, 0, 2}, {"unpack2x16float", GLSL_UNPACK_HALF2X16, 0, 2},
    };
    for (size_t i = 0; i < sizeof packs / sizeof packs[0]; i++) {
        if (!same(n, len, packs[i].name)) continue;
        if (count != 1) fail(x, line, col, "%s takes one value", packs[i].name);
        const int f32 = t_simple(x, K_F32), u32 = t_simple(x, K_U32);
        const int from = packs[i].from ? t_vec(x, f32, packs[i].from) : u32;
        const int to = packs[i].to ? t_vec(x, f32, packs[i].to) : u32;
        const uint32_t ids[1] = {coerce(x, args[0], from, line, col).id};
        return (val){.t = to, .id = glsl(x, to, (uint32_t)packs[i].inst, ids, 1)};
    }
    if (same(n, len, "quantizeToF16")) {
        if (count != 1) fail(x, line, col, "quantizeToF16 takes one value");
        const int t = unify(x, args, 1, t_simple(x, K_F32), line, col);
        return (val){.t = t, .id = EMIT(x, OP_QUANTIZE_TO_F16, t, args[0].id)};
    }
    fail(x, line, col, "'%.*s' isn't a function", len, n);
    return args[0];
}

static val call(translation *x, const expr *e)
{
    if (e->type && same(e->type->name, e->type->len, "bitcast")) return bitcast(x, e);
    if (!e->type) {
        fdecl *f = find_fn(x, e->name, e->len);
        if (f) return call_user(x, f, e);
    }
    val made;
    if (construct(x, e, &made)) return made;
    return builtin(x, e);
}

// ---------------------------------------------------------------------------
// Textures

// A texture or sampler argument: a module's variable of one, loaded.
static val handle(translation *x, const expr *e, const kind want)
{
    const val v = eval(x, e);
    if (!v.ref || T(x, v.t)->kind != want) {
        if (!(want == K_SAMPLER && v.ref && T(x, v.t)->kind == K_SAMPLER_CMP)) {
            fail(x, e->line, e->col, want == K_TEXTURE ? "expected a texture" : "expected a sampler");
        }
    }
    return load(x, v);
}

static val float_value(translation *x, const expr *e, const int t)
{
    return coerce(x, rv(x, e), t, e->line, e->col);
}

// An int (or ints) of either signedness, as an image's coordinates and levels are.
static val int_value(translation *x, const expr *e, const int width)
{
    val v = rv(x, e);
    if (v.constant) v = concrete(x, v, t_simple(x, K_I32));
    v = load(x, v);
    if (!is_int(x, v.t) || width_of(x, v.t) != width || (width == 1 && !is_scalar(x, v.t))) {
        fail(x, e->line, e->col, width == 1 ? "expected an int" : "expected a vector of ints");
    }
    return v;
}

// A level as sampling takes one: a float, from an int too (a depth texture's level is one).
static uint32_t level_value(translation *x, const expr *e)
{
    val v = rv(x, e);
    const int f32 = t_simple(x, K_F32);
    if (v.constant) return concrete(x, v, f32).id;
    v = load(x, v);
    if (T(x, v.t)->kind == K_F32) return v.id;
    return convert(x, v, f32, e->line, e->col).id;
}

static val texture_call(translation *x, const expr *e)
{
    const char *n = e->name;
    const int len = e->len, count = e->nargs, line = e->line, col = e->col;
    if (count < 1) fail(x, line, col, "'%.*s' takes a texture", len, n);
    const val image = handle(x, e->args[0], K_TEXTURE);
    const type tex = *T(x, image.t);
    const int f32 = t_simple(x, K_F32);
    const int coords = t_vec(x, f32, tex.dim == DIM_CUBE ? 3 : 2);
    const int result = tex.depth ? f32 : t_vec(x, tex.elem, 4);
    const int vec4 = t_vec(x, tex.depth ? f32 : tex.elem, 4);
    const bool sample = same(n, len, "textureSample"), level = same(n, len, "textureSampleLevel");
    const bool grad = same(n, len, "textureSampleGrad"), bias = same(n, len, "textureSampleBias");
    const bool compare = same(n, len, "textureSampleCompare"), compare_level = same(n, len, "textureSampleCompareLevel");
    if (sample || level || grad || bias || compare || compare_level) {
        const int wanted = sample || compare || compare_level ? (compare || compare_level ? 4 : 3) : grad ? 5 : 4;
        if (count != wanted) fail(x, line, col, "'%.*s' takes %d arguments here", len, n, wanted);
        if (tex.multisampled) fail(x, line, col, "a multisampled texture is loaded, not sampled");
        const val s = handle(x, e->args[1], K_SAMPLER);
        // Everything worked out before the sampled image, which is in the block that samples it
        const uint32_t at = float_value(x, e->args[2], coords).id;
        uint32_t extra[2] = {0, 0};
        if (compare || compare_level || bias) extra[0] = float_value(x, e->args[3], f32).id;
        else if (level) extra[0] = level_value(x, e->args[3]);
        else if (grad) extra[0] = float_value(x, e->args[3], coords).id, extra[1] = float_value(x, e->args[4], coords).id;
        const uint32_t sampled_type = sampled_image_type(x, spv_type(x, image.t));
        const uint32_t sampled = new_id(x);
        INS(&x->fn->body, OP_SAMPLED_IMAGE, sampled_type, sampled, image.id, s.id);
        if (compare) return (val){.t = f32, .id = EMIT(x, OP_IMAGE_SAMPLE_DREF_IMPLICIT_LOD, f32, sampled, at, extra[0])};
        if (compare_level) {
            return (val){.t = f32, .id = EMIT(x, OP_IMAGE_SAMPLE_DREF_EXPLICIT_LOD, f32, sampled, at, extra[0], IMAGE_LOD,
                                              const_f32(x, 0.0f))};
        }
        uint32_t id;
        if (sample) id = EMIT(x, OP_IMAGE_SAMPLE_IMPLICIT_LOD, vec4, sampled, at);
        else if (bias) id = EMIT(x, OP_IMAGE_SAMPLE_IMPLICIT_LOD, vec4, sampled, at, IMAGE_BIAS, extra[0]);
        else if (level) id = EMIT(x, OP_IMAGE_SAMPLE_EXPLICIT_LOD, vec4, sampled, at, IMAGE_LOD, extra[0]);
        else id = EMIT(x, OP_IMAGE_SAMPLE_EXPLICIT_LOD, vec4, sampled, at, IMAGE_GRAD, extra[0], extra[1]);
        if (tex.depth) id = EMIT(x, OP_COMPOSITE_EXTRACT, f32, id, 0);
        return (val){.t = result, .id = id};
    }
    if (same(n, len, "textureLoad")) {
        if (count != 3) fail(x, line, col, "textureLoad takes a texture, where and a level or sample");
        if (tex.dim == DIM_CUBE) fail(x, line, col, "a cube isn't loaded");
        const val at = int_value(x, e->args[1], 2);
        const val third = int_value(x, e->args[2], 1);
        uint32_t id = EMIT(x, OP_IMAGE_FETCH, vec4, image.id, at.id, tex.multisampled ? IMAGE_SAMPLE : IMAGE_LOD, third.id);
        if (tex.depth) id = EMIT(x, OP_COMPOSITE_EXTRACT, f32, id, 0);
        return (val){.t = result, .id = id};
    }
    x->cap_image_query = true;
    const int u32 = t_simple(x, K_U32);
    if (same(n, len, "textureDimensions")) {
        const int t = t_vec(x, u32, 2);
        if (tex.multisampled) {
            if (count != 1) fail(x, line, col, "a multisampled texture has one size");
            return (val){.t = t, .id = EMIT(x, OP_IMAGE_QUERY_SIZE, t, image.id)};
        }
        const uint32_t at_level = count > 1 ? int_value(x, e->args[1], 1).id : const_u32(x, 0);
        return (val){.t = t, .id = EMIT(x, OP_IMAGE_QUERY_SIZE_LOD, t, image.id, at_level)};
    }
    if (same(n, len, "textureNumLevels")) return (val){.t = u32, .id = EMIT(x, OP_IMAGE_QUERY_LEVELS, u32, image.id)};
    if (same(n, len, "textureNumSamples")) return (val){.t = u32, .id = EMIT(x, OP_IMAGE_QUERY_SAMPLES, u32, image.id)};
    fail(x, line, col, "'%.*s' isn't a function", len, n);
    return image;
}

// ---------------------------------------------------------------------------
// Statements

static void statement(translation *x, const stmt *s);

static void statements(translation *x, const stmt *block)
{
    const int scope = x->fn->scope_count;
    for (int i = 0; i < block->count; i++) {
        if (x->fn->ended) break; // What follows a return, a break or a discard never runs
        statement(x, block->list[i]);
    }
    x->fn->scope_count = scope;
}

static void store(translation *x, const val place, const val v)
{
    if (place.sc == SC_UNIFORM || place.sc == SC_INPUT || place.sc == SC_UNIFORM_CONSTANT) {
        fail(x, 0, 0, "that's read-only");
    }
    INS(&x->fn->body, OP_STORE, place.id, v.id);
}

static val place_of(translation *x, const expr *e)
{
    const val v = eval(x, e);
    if (!v.ref) fail(x, e->line, e->col, "that isn't a variable, or a part of one");
    if (v.sc == SC_UNIFORM || v.sc == SC_INPUT || v.sc == SC_UNIFORM_CONSTANT) fail(x, e->line, e->col, "that's read-only");
    return v;
}

static punct compound_op(const punct p)
{
    switch (p) {
    case P_PLUSEQ: return P_PLUS;
    case P_MINUSEQ: return P_MINUS;
    case P_STAREQ: return P_STAR;
    case P_SLASHEQ: return P_SLASH;
    case P_PERCENTEQ: return P_PERCENT;
    case P_AMPEQ: return P_AMP;
    case P_PIPEEQ: return P_PIPE;
    case P_CARETEQ: return P_CARET;
    case P_SHLEQ: return P_SHL;
    case P_SHREQ: return P_SHR;
    default: return P_NONE;
    }
}

static void var_statement(translation *x, const stmt *s)
{
    int t = s->type ? resolve(x, s->type) : 0;
    val v = {0};
    if (s->e) {
        v = rv(x, s->e);
        if (!t) {
            v = load(x, v);
            t = v.t;
        }
        v = coerce(x, v, t, s->line, s->col);
    }
    const uint32_t id = local_var(x, t);
    // Each time it's declared, it's its value, or zeros: in a loop too
    INS(&x->fn->body, OP_STORE, id, s->e ? v.id : const_null(x, t));
    bind(x, s->name, s->len, (val){.t = t, .id = id, .ref = true, .sc = SC_FUNCTION});
}

static void let_statement(translation *x, const stmt *s)
{
    val v = rv(x, s->e);
    if (s->type) v = coerce(x, v, resolve(x, s->type), s->line, s->col);
    else if (s->kind == S_LET) v = load(x, v); // A let is concrete; a const may stay abstract
    bind(x, s->name, s->len, v);
}

static target *innermost(translation *x, const bool loop)
{
    for (int i = x->fn->depth - 1; i >= 0; i--) {
        if (!loop || x->fn->targets[i].loop) return &x->fn->targets[i];
    }
    return NULL;
}

static void push_target(translation *x, const bool loop, const uint32_t merge, const uint32_t cont)
{
    if (x->fn->depth == 64) fail(x, 0, 0, "loops and switches nested too deep");
    x->fn->targets[x->fn->depth++] = (target){.loop = loop, .merge = merge, .cont = cont};
}

// The end of a construct whose merge block nothing may reach: it says so.
static void after_merge(translation *x, const bool reached)
{
    if (reached) return;
    ins(&x->fn->body, OP_UNREACHABLE, NULL, 0);
    x->fn->ended = true;
}

static void if_statement(translation *x, const stmt *s)
{
    const val c = bool_value(x, s->e);
    if (!is_scalar(x, c.t)) fail(x, s->line, s->col, "an if's condition is a bool");
    const uint32_t then = new_id(x), merge = new_id(x);
    const uint32_t other = s->other ? new_id(x) : merge;
    INS(&x->fn->body, OP_SELECTION_MERGE, merge, 0);
    INS(&x->fn->body, OP_BRANCH_CONDITIONAL, c.id, then, other);
    x->fn->ended = true;
    start_block(x, then);
    statement(x, s->body);
    bool reached = !x->fn->ended;
    branch(x, merge);
    if (s->other) {
        start_block(x, other);
        statement(x, s->other);
        reached |= !x->fn->ended;
        branch(x, merge);
    } else {
        reached = true;
    }
    start_block(x, merge);
    after_merge(x, reached);
}

// A loop: its header, which says where it ends and where `continue` goes,
// then its condition, its body, and its continuing part, which goes back.
static void loop_statement(translation *x, const stmt *s)
{
    const int scope = x->fn->scope_count;
    if (s->kind == S_FOR && s->init) statement(x, s->init);
    const uint32_t header = new_id(x), check = new_id(x), body = new_id(x), cont = new_id(x), merge = new_id(x);
    branch(x, header);
    start_block(x, header);
    INS(&x->fn->body, OP_LOOP_MERGE, merge, cont, 0);
    INS(&x->fn->body, OP_BRANCH, check);
    x->fn->ended = true;
    start_block(x, check);
    if (s->kind != S_LOOP && s->e) {
        const val c = bool_value(x, s->e);
        INS(&x->fn->body, OP_BRANCH_CONDITIONAL, c.id, body, merge);
        x->fn->ended = true;
    } else {
        branch(x, body);
    }
    start_block(x, body);
    push_target(x, true, merge, cont);
    // The body's names are in scope in its continuing part
    for (int i = 0; i < s->body->count; i++) {
        if (x->fn->ended) break;
        statement(x, s->body->list[i]);
    }
    branch(x, cont);
    x->fn->depth--;
    start_block(x, cont);
    if (s->kind == S_FOR && s->update) statement(x, s->update);
    if (s->continuing) { // Neither break nor continue is taken in it: only break if, last
        for (int i = 0; i < s->continuing->count; i++) {
            const stmt *c = s->continuing->list[i];
            if (c->kind == S_BREAK_IF) {
                const val v = bool_value(x, c->e);
                INS(&x->fn->body, OP_BRANCH_CONDITIONAL, v.id, merge, header);
                x->fn->ended = true;
                break;
            }
            statement(x, c);
        }
    }
    branch(x, header);
    x->fn->scope_count = scope;
    start_block(x, merge);
}

// A switch selector's number, which is a constant.
static int64_t case_value(translation *x, const expr *e)
{
    if (e->kind == X_INT) return e->i;
    if (e->kind == X_UNARY && e->op == P_MINUS) return -case_value(x, e->a);
    const val v = eval(x, e);
    if (v.constant && T(x, v.t)->kind == K_AINT) return v.i;
    fail(x, e->line, e->col, "a case is a constant int");
    return 0;
}

static void switch_statement(translation *x, const stmt *s)
{
    val selector = rv(x, s->e);
    if (selector.constant) selector = concrete(x, selector, 0);
    if (!is_int(x, selector.t) || !is_scalar(x, selector.t)) fail(x, s->line, s->col, "a switch's selector is an int");
    const uint32_t merge = new_id(x);
    uint32_t *labels = grab(&x->a, sizeof(uint32_t) * (size_t)(s->ncases ? s->ncases : 1));
    uint32_t fallback = merge;
    int selectors = 0;
    for (int i = 0; i < s->ncases; i++) {
        labels[i] = new_id(x);
        if (s->cases[i].fallback) fallback = labels[i];
        selectors += s->cases[i].count;
    }
    INS(&x->fn->body, OP_SELECTION_MERGE, merge, 0);
    put(&x->fn->body, OP_SWITCH | (uint32_t)(3 + 2 * selectors) << 16);
    put(&x->fn->body, selector.id);
    put(&x->fn->body, fallback);
    for (int i = 0; i < s->ncases; i++) {
        for (int j = 0; j < s->cases[i].count; j++) {
            put(&x->fn->body, (uint32_t)case_value(x, s->cases[i].selectors[j]));
            put(&x->fn->body, labels[i]);
        }
    }
    x->fn->ended = true;
    bool reached = fallback == merge;
    push_target(x, false, merge, 0);
    for (int i = 0; i < s->ncases; i++) {
        start_block(x, labels[i]);
        statement(x, s->cases[i].body);
        reached |= !x->fn->ended;
        branch(x, merge);
    }
    reached |= x->fn->targets[x->fn->depth - 1].broken;
    x->fn->depth--;
    start_block(x, merge);
    after_merge(x, reached);
}

static void return_statement(translation *x, const stmt *s)
{
    fn_state *fn = x->fn;
    const fdecl *f = fn->f;
    if (f->at.stage) { // An entry point's result goes to its outputs
        if (s->e) {
            const val v = coerce(x, rv(x, s->e), f->result_type, s->line, s->col);
            if (fn->whole_output) {
                INS(&fn->body, OP_STORE, fn->outputs[0], v.id);
            } else {
                for (int i = 0; i < fn->output_count; i++) {
                    const uint32_t part = EMIT(x, OP_COMPOSITE_EXTRACT, fn->output_types[i], v.id, (uint32_t)i);
                    INS(&fn->body, OP_STORE, fn->outputs[i], part);
                }
            }
        }
        ins(&fn->body, OP_RETURN, NULL, 0);
    } else if (s->e) {
        const val v = coerce(x, rv(x, s->e), f->result_type, s->line, s->col);
        INS(&fn->body, OP_RETURN_VALUE, v.id);
    } else {
        ins(&fn->body, OP_RETURN, NULL, 0);
    }
    fn->ended = true;
}

static void statement(translation *x, const stmt *s)
{
    switch (s->kind) {
    case S_VAR: var_statement(x, s); return;
    case S_LET:
    case S_CONST: let_statement(x, s); return;
    case S_ASSIGN: {
        const val place = place_of(x, s->e);
        val v;
        if (s->op == P_ASSIGN) {
            v = rv(x, s->e2);
        } else {
            const val now = load(x, place);
            v = binop(x, compound_op(s->op), now, rv(x, s->e2), s->line, s->col);
        }
        store(x, place, coerce(x, v, place.t, s->line, s->col));
        return;
    }
    case S_INC:
    case S_DEC: {
        const val place = place_of(x, s->e);
        const val now = load(x, place);
        if (!is_int(x, now.t) || !is_scalar(x, now.t)) fail(x, s->line, s->col, "only ints count up and down");
        const uint32_t one = const_of(x, now.t, 1.0);
        store(x, place, (val){.t = now.t, .id = EMIT(x, s->kind == S_INC ? OP_I_ADD : OP_I_SUB, now.t, now.id, one)});
        return;
    }
    case S_CALL:
    case S_PHONY:
        eval(x, s->e);
        return;
    case S_IF: if_statement(x, s); return;
    case S_FOR:
    case S_WHILE:
    case S_LOOP: loop_statement(x, s); return;
    case S_SWITCH: switch_statement(x, s); return;
    case S_BREAK: {
        target *t = innermost(x, false);
        if (!t) fail(x, s->line, s->col, "break outside a loop or a switch");
        t->broken = true;
        branch(x, t->merge);
        return;
    }
    case S_CONTINUE: {
        const target *t = innermost(x, true);
        if (!t) fail(x, s->line, s->col, "continue outside a loop");
        branch(x, t->cont);
        return;
    }
    case S_BREAK_IF: fail(x, s->line, s->col, "'break if' ends a loop's continuing part"); return;
    case S_RETURN: return_statement(x, s); return;
    case S_DISCARD:
        ins(&x->fn->body, OP_KILL, NULL, 0);
        x->fn->ended = true;
        return;
    case S_BLOCK: statements(x, s); return;
    }
}

// ---------------------------------------------------------------------------
// Functions and entry points

// An entry point's input or output: a variable of the module's, with where it is.
static uint32_t interface_var(translation *x, const int sc, const int t, const attrs at, const bool fragment)
{
    const uint32_t id = new_id(x);
    INS(&x->globals_words, OP_VARIABLE, spv_type(x, t_ptr(x, sc, t)), id, (uint32_t)sc);
    if (at.builtin >= 0) {
        const int builtin = at.builtin == BUILTIN_POSITION && fragment && sc == SC_INPUT ? BUILTIN_FRAG_COORD : at.builtin;
        INS(&x->decorations, OP_DECORATE, id, DEC_BUILTIN, (uint32_t)builtin);
        if (builtin == BUILTIN_FRAG_DEPTH) x->fn->uses_frag_depth = true;
    } else if (at.location >= 0) {
        INS(&x->decorations, OP_DECORATE, id, DEC_LOCATION, (uint32_t)at.location);
        // Ints between the stages aren't blended across a triangle: WGSL says so, Vulkan needs it said
        const bool between = (fragment && sc == SC_INPUT) || (!fragment && sc == SC_OUTPUT);
        if (between && (at.flat || is_int(x, t))) INS(&x->decorations, OP_DECORATE, id, DEC_FLAT);
    } else {
        fail(x, 0, 0, "an entry point's input or output says where it is: @location or @builtin");
    }
    if (x->fn->input_count < 32) x->fn->inputs[x->fn->input_count++] = id;
    return id;
}

static void emit_function(translation *x, fdecl *f)
{
    fn_state state = {.f = f};
    x->fn = &state;
    signature(x, f);
    words *out = &x->functions;
    const bool entry = f->at.stage != 0, fragment = f->at.stage == 2;
    const int void_t = t_simple(x, K_VOID);
    INS(out, OP_FUNCTION, spv_type(x, entry ? void_t : f->result_type), f->id, 0, f->type_id);
    if (!entry) {
        for (int i = 0; i < f->count; i++) {
            const uint32_t id = new_id(x);
            INS(out, OP_FUNCTION_PARAMETER, spv_type(x, f->param_types[i]), id);
            bind(x, f->params[i].name, f->params[i].len, (val){.t = f->param_types[i], .id = id});
        }
    }
    const uint32_t first = new_id(x);
    INS(out, OP_LABEL, first);
    state.label = first;
    if (entry) {
        // Its parameters are its inputs, a struct's members each one of them
        for (int i = 0; i < f->count; i++) {
            const int t = f->param_types[i];
            const param *p = &f->params[i];
            if (T(x, t)->kind == K_STRUCT && p->at.location < 0 && p->at.builtin < 0) {
                const sdecl *sd = T(x, t)->s;
                uint32_t parts[128];
                for (int m = 0; m < sd->count; m++) {
                    const uint32_t var = interface_var(x, SC_INPUT, sd->member_types[m], sd->members[m].at, fragment);
                    parts[m] = EMIT(x, OP_LOAD, sd->member_types[m], var);
                }
                bind(x, p->name, p->len, (val){.t = t, .id = emit(x, OP_COMPOSITE_CONSTRUCT, t, parts, sd->count)});
            } else {
                const uint32_t var = interface_var(x, SC_INPUT, t, p->at, fragment);
                bind(x, p->name, p->len, (val){.t = t, .id = EMIT(x, OP_LOAD, t, var)});
            }
        }
        // ...and its result, its outputs
        const int r = f->result_type;
        if (T(x, r)->kind == K_STRUCT && f->result_at.location < 0 && f->result_at.builtin < 0) {
            const sdecl *sd = T(x, r)->s;
            if (sd->count > 16) fail(x, f->line, f->col, "too many outputs");
            for (int m = 0; m < sd->count; m++) {
                state.outputs[m] = interface_var(x, SC_OUTPUT, sd->member_types[m], sd->members[m].at, fragment);
                state.output_types[m] = sd->member_types[m];
            }
            state.output_count = sd->count;
        } else if (T(x, r)->kind != K_VOID) {
            state.outputs[0] = interface_var(x, SC_OUTPUT, r, f->result_at, fragment);
            state.output_types[0] = r;
            state.output_count = 1;
            state.whole_output = true;
        }
    }
    statements(x, f->body);
    if (!state.ended) {
        if (entry || T(x, f->result_type)->kind == K_VOID) ins(&state.body, OP_RETURN, NULL, 0);
        else ins(&state.body, OP_UNREACHABLE, NULL, 0);
    }
    put_all(out, &state.vars);
    put_all(out, &state.body);
    ins(out, OP_FUNCTION_END, NULL, 0);
    if (entry) {
        put(&x->entries, OP_ENTRY_POINT | (uint32_t)(3 + string_words(f->len) + state.input_count) << 16);
        put(&x->entries, fragment ? 4u : 0u);
        put(&x->entries, f->id);
        put_string(&x->entries, f->name, f->len);
        for (int i = 0; i < state.input_count; i++) put(&x->entries, state.inputs[i]);
        if (fragment) {
            INS(&x->modes, OP_EXECUTION_MODE, f->id, 7); // OriginUpperLeft
            if (state.uses_frag_depth) INS(&x->modes, OP_EXECUTION_MODE, f->id, 12); // DepthReplacing
        }
    }
    free(state.vars.w);
    free(state.body.w);
    free(state.scope);
    x->fn = NULL;
}

// A module's variables: its textures, samplers and uniform blocks, which the
// pipeline binds, and its private ones.
static void emit_global(translation *x, const gdecl *g)
{
    if (g->is_const) {
        if (!g->init) fail(x, g->line, g->col, "'%.*s' needs a value", g->len, g->name);
        // A module's constant is a number: what it's given has to be one
        const expr *e = g->init;
        val v;
        if (e->kind == X_INT || e->kind == X_FLOAT || e->kind == X_BOOL
            || (e->kind == X_UNARY && e->op == P_MINUS && (e->a->kind == X_INT || e->a->kind == X_FLOAT))) {
            const bool minus = e->kind == X_UNARY;
            const expr *literal = minus ? e->a : e;
            if (literal->kind == X_BOOL) {
                v = (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, literal->i != 0)};
            } else if (literal->kind == X_INT) {
                v = abstract_int(x, minus ? -literal->i : literal->i);
                if (literal->suffix) v = concrete(x, v, t_simple(x, literal->suffix == 'u' ? K_U32 : K_I32));
            } else {
                v = abstract_float(x, minus ? -literal->f : literal->f);
                v.text = literal->name, v.len = literal->len, v.negated = minus;
                if (literal->suffix == 'f') v = concrete(x, v, t_simple(x, K_F32));
            }
        } else {
            fail(x, g->line, g->col, "a module's constant here is a number");
            return;
        }
        if (g->type) v = coerce(x, v, resolve(x, g->type), g->line, g->col);
        bind(x, g->name, g->len, v);
        return;
    }
    const int t = g->type ? resolve(x, g->type) : 0;
    if (!t) fail(x, g->line, g->col, "'%.*s' needs a type", g->len, g->name);
    const uint32_t id = new_id(x);
    int sc;
    switch (g->space) {
    case SPACE_PRIVATE: {
        sc = SC_PRIVATE;
        uint32_t init = const_null(x, t);
        if (g->init) {
            if (g->init->kind == X_INT || g->init->kind == X_FLOAT || g->init->kind == X_BOOL) {
                val v = g->init->kind == X_BOOL ? (val){.t = t_simple(x, K_BOOL), .id = const_bool(x, g->init->i != 0)}
                      : g->init->kind == X_INT ? abstract_int(x, g->init->i) : abstract_float(x, g->init->f);
                if (g->init->kind == X_FLOAT) v.text = g->init->name, v.len = g->init->len;
                init = coerce(x, v, t, g->line, g->col).id;
            } else {
                fail(x, g->line, g->col, "a private variable here starts as a number or as zeros");
            }
        }
        INS(&x->globals_words, OP_VARIABLE, spv_type(x, t_ptr(x, sc, t)), id, (uint32_t)sc, init);
        break;
    }
    case SPACE_UNIFORM:
        sc = SC_UNIFORM;
        if (T(x, t)->kind != K_STRUCT) fail(x, g->line, g->col, "a uniform block is a struct");
        INS(&x->globals_words, OP_VARIABLE, spv_type(x, t_ptr(x, sc, t)), id, (uint32_t)sc);
        INS(&x->decorations, OP_DECORATE, spv_laid(x, t), DEC_BLOCK);
        break;
    default: {
        sc = SC_UNIFORM_CONSTANT;
        const kind k = T(x, t)->kind;
        if (k != K_TEXTURE && k != K_SAMPLER && k != K_SAMPLER_CMP) {
            fail(x, g->line, g->col, "a module's variable here is a texture, a sampler, var<uniform> or var<private>");
        }
        INS(&x->globals_words, OP_VARIABLE, spv_type(x, t_ptr(x, sc, t)), id, (uint32_t)sc);
        break;
    }
    }
    if (sc == SC_UNIFORM || sc == SC_UNIFORM_CONSTANT) {
        if (g->at.group < 0 || g->at.binding < 0) fail(x, g->line, g->col, "'%.*s' says its @group and @binding", g->len, g->name);
        INS(&x->decorations, OP_DECORATE, id, DEC_DESCRIPTOR_SET, (uint32_t)g->at.group);
        INS(&x->decorations, OP_DECORATE, id, DEC_BINDING, (uint32_t)g->at.binding);
    }
    bind(x, g->name, g->len, (val){.t = t, .id = id, .ref = true, .sc = sc});
}

uint32_t *tide_wgsl_to_spirv(const char *wgsl, size_t *count, char *error, size_t error_size)
{
    translation *x = calloc(1, sizeof(translation));
    if (!x) abort();
    x->text = wgsl;
    x->error = error;
    x->error_size = error_size;
    if (error && error_size) error[0] = '\0';
    uint32_t *result = NULL;
    *count = 0;
    if (!setjmp(x->failed)) {
        add_type(x, (type){.kind = K_NONE}); // Type 0 is none
        tokenize(x);
        parse_module(x);
        x->glsl = new_id(x);
        for (int i = 0; i < x->nfns; i++) x->fns[i]->id = new_id(x);
        for (int i = 0; i < x->nglobals; i++) emit_global(x, x->globals[i]);
        for (int i = 0; i < x->nfns; i++) emit_function(x, x->fns[i]);
        words module = {0};
        put(&module, 0x07230203u); // SPIR-V's magic number
        put(&module, 0x00010300u); // Version 1.3
        put(&module, 0u);          // Made by: nobody registered
        put(&module, x->next_id + 1u);
        put(&module, 0u);
        INS(&module, OP_CAPABILITY, CAP_SHADER);
        if (x->cap_image_query) INS(&module, OP_CAPABILITY, CAP_IMAGE_QUERY);
        if (x->cap_derivative_control) INS(&module, OP_CAPABILITY, CAP_DERIVATIVE_CONTROL);
        put(&module, OP_EXT_INST_IMPORT | (uint32_t)(2 + string_words(12)) << 16);
        put(&module, x->glsl);
        put_string(&module, "GLSL.std.450", 12);
        INS(&module, OP_MEMORY_MODEL, 0, 1); // Logical, GLSL450
        put_all(&module, &x->entries);
        put_all(&module, &x->modes);
        put_all(&module, &x->decorations);
        put_all(&module, &x->globals_words);
        put_all(&module, &x->functions);
        result = module.w;
        *count = module.n;
    }
    free(x->toks);
    free(x->structs);
    free(x->globals);
    free(x->fns);
    free(x->types);
    free(x->consts);
    free(x->functypes);
    free(x->globals_scope);
    free(x->caps.w);
    free(x->imports.w);
    free(x->memory.w);
    free(x->entries.w);
    free(x->modes.w);
    free(x->decorations.w);
    free(x->globals_words.w);
    free(x->functions.w);
    if (x->fn) { // It failed inside a function
        free(x->fn->vars.w);
        free(x->fn->body.w);
        free(x->fn->scope);
    }
    release(&x->a);
    free(x);
    return result;
}

