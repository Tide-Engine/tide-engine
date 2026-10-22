// Reading WGSL into the tree of wgsl_tree.inc, and its types, for tidec's
// translators: the includer's `struct translation` has `a`, `text`, `toks`,
// `ntok`, `pos`, `failed`, `error`, `error_size`, the module's `structs`,
// `globals` and `fns` with their counts, and `types`, `ntypes`, `cap_types`.

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
            if (accept(x, P_COMMA) && !is_punct(x, P_RPAREN)) {
                token *sampling = expect_name(x, "how it's sampled");
                if (!same(sampling->at, sampling->len, "center")) {
                    fail(x, sampling->line, sampling->col, "@interpolate(..., %.*s) isn't taken here", sampling->len, sampling->at);
                }
                accept(x, P_COMMA);
            }
            expect(x, P_RPAREN, "')'");
        } else if (same(name->at, name->len, "vertex")) {
            at.stage = 1;
        } else if (same(name->at, name->len, "fragment")) {
            at.stage = 2;
        } else if (same(name->at, name->len, "must_use")) {
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
