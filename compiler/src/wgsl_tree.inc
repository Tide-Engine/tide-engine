// WGSL as tidec's translators read it (spirv.c, hlsl.c): the arena, the
// tokens, the tree and the types. Each includes it, then defines its own
// `struct translation`, whose first fields wgsl_read.inc reads, then
// includes that.

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

// SPIR-V's storage classes, which a pointer's type says
enum { SC_UNIFORM_CONSTANT = 0, SC_INPUT = 1, SC_UNIFORM = 2, SC_OUTPUT = 3, SC_PRIVATE = 6, SC_FUNCTION = 7 };

