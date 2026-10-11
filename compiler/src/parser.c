#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/math.h"

#include "ast.h"
#include "types.h"

// Recursive descent. For the compiler, stops at the first syntax error: later
// errors after a bad parse are usually noise. For editors (`recover`), skips the
// broken statement, field or declaration and carries on, so a half-typed file
// still gives most of its structure.

typedef struct parser {
    token *toks;
    int pos;
    jmp_buf fail; // Where a syntax error goes: the innermost place that can recover.
    bool recover;
    // File level. In the struct rather than locals: they change between setjmp and longjmp.
    program *prog;
    unit *unit; // The file's, or that of the namespace block being read
    VEC(attribute) pending; // Attributes waiting for the next declaration
    VEC(attribute) field_pending; // Attributes waiting for the next field
    bool seen_decl;
    // Parsing a declaration written after `override`: its name is the one it
    // replaces, which can be qualified, `Combat.Move` or `Combat.Stats.Hurt`
    bool overriding;
    // A '>>' (or '>>=') of which a type's '>' was read (close_angle): what's
    // left of it is the next token, before the one at `pos`
    token rest;
    bool has_rest;
} parser;

static token *peek_at(const parser *p, int offset)
{
    if (p->has_rest) {
        if (offset == 0) return (token *)&p->rest;
        offset--;
    }
    token *t = &p->toks[p->pos];
    for (int i = 0; i < offset && t->kind != T_EOF; i++) t++;
    return t;
}

static token *peek(const parser *p)
{
    return peek_at(p, 0);
}

static bool at(const parser *p, const tok_kind kind)
{
    return peek(p)->kind == kind;
}

static token *advance(parser *p)
{
    if (p->has_rest) {
        p->has_rest = false;
        return &p->rest;
    }
    token *t = &p->toks[p->pos];
    if (t->kind != T_EOF) p->pos++;
    return t;
}

static bool accept(parser *p, const tok_kind kind)
{
    if (!at(p, kind)) return false;
    advance(p);
    return true;
}

static _Noreturn void fail_at(parser *p, const token *t, const char *what)
{
    if (t->kind == T_EOF) {
        diag_error(t->at, "expected %s, found end of file", what);
    } else {
        diag_error(t->at, "expected %s, found '" STR_FMT "'", what, STR_ARG(t->text));
    }
    longjmp(p->fail, 1);
}

static token *expect(parser *p, const tok_kind kind, const char *what)
{
    if (!at(p, kind)) fail_at(p, peek(p), what);
    return advance(p);
}

// The '>' that closes a type's <>, and where it is. Two that close one inside
// another, as in List<Grid2<Color>>, lex as one '>>' token (and with '=' after
// them as '>>='): the first '>' is read here, and what's left of the token is
// read next, as C#'s parser reads it.
static loc close_angle(parser *p, const char *what)
{
    const token *t = peek(p);
    const tok_kind left = t->kind == T_SHR ? T_GT : t->kind == T_SHR_ASSIGN ? T_GE : t->kind == T_GE ? T_ASSIGN : T_EOF;
    if (left == T_EOF) return expect(p, T_GT, what)->at;
    const token whole = *advance(p);
    p->rest = whole;
    p->rest.kind = left;
    p->rest.text = (str){whole.text.ptr + 1, whole.text.len - 1};
    p->rest.at.col++;
    p->has_rest = true;
    return whole.at;
}

static token *expect_ident(parser *p, const char *what)
{
    return expect(p, T_IDENT, what);
}

// ---------------------------------------------------------------------------
// Names

// "a" and "b" as "a.b", in the arena.
static str join_names(const str a, const str b)
{
    char *text = arena_alloc((size_t)a.len + (size_t)b.len + 2);
    memcpy(text, a.ptr, (size_t)a.len);
    text[a.len] = '.';
    memcpy(text + a.len + 1, b.ptr, (size_t)b.len);
    return (str){text, a.len + 1 + b.len};
}

// Name or Namespace.Name, as in `mut Combat.Health health` or `[After(Physics.Integrate)]`.
static qname parse_qname(parser *p, const char *what)
{
    const token *first = expect_ident(p, what);
    qname q = {.text = first->text, .at = first->at, .name_at = first->at};
    while (at(p, T_DOT) && peek_at(p, 1)->kind == T_IDENT) {
        advance(p);
        const token *part = advance(p);
        q.text = join_names(q.text, part->text);
        q.name_at = part->at;
    }
    return q;
}

// ---------------------------------------------------------------------------
// Expressions

// Generic types, which take a type in <>.
static bool generic_name(const str name)
{
    return generic_form_named(name) != NULL;
}

// Where a type that starts at the token `i` ahead ends: the token after it,
// or -1 for no type there. A name, maybe qualified, what it takes in <> (whose
// '>>' is one token), and '?': List<Item>, Map<int, List<int>>, int?. For the
// lookaheads that tell a declaration from an expression; the type's own
// parser decides what it means.
static int skip_type(const parser *p, int i)
{
    if (peek_at(p, i)->kind != T_IDENT) return -1;
    i++;
    while (peek_at(p, i)->kind == T_DOT && peek_at(p, i + 1)->kind == T_IDENT) i += 2;
    if (peek_at(p, i)->kind == T_LT) {
        int depth = 1;
        i++;
        while (depth > 0) {
            const tok_kind k = peek_at(p, i)->kind;
            if (k == T_LT) depth++;
            else if (k == T_GT) depth--;
            else if (k == T_SHR) depth -= 2;
            else if (k != T_IDENT && k != T_DOT && k != T_COMMA && k != T_QUESTION) return -1;
            i++;
        }
        if (depth < 0) return -1;
    }
    while (peek_at(p, i)->kind == T_LBRACKET) { // int[32], int[CAP * 2]: a constant between the brackets
        int depth = 0;
        for (;; i++) {
            const tok_kind k = peek_at(p, i)->kind;
            if (k == T_LBRACKET) depth++;
            else if (k == T_RBRACKET && --depth == 0) break;
            else if (k == T_EOF || k == T_SEMI || k == T_LBRACE || k == T_RBRACE || k == T_ASSIGN) return -1;
        }
        i++;
    }
    if (peek_at(p, i)->kind == T_QUESTION) i++;
    return i;
}

static expr *parse_expr(parser *p);

// An array's length as its text says it: a number, a name, or the expression
// written out ("CAP * 2"), as messages and editors write the type.
static void put_size_text(sb *out, const expr *e)
{
    switch (e->kind) {
    case E_INT: sb_printf(out, "%lld", (long long)e->int_value); break;
    case E_NAME: sb_printf(out, STR_FMT, STR_ARG(e->name)); break;
    case E_MEMBER:
        put_size_text(out, e->object);
        sb_printf(out, "." STR_FMT, STR_ARG(e->member));
        break;
    case E_BINARY:
        put_size_text(out, e->lhs);
        sb_printf(out, " %s ", e->op == T_PLUS ? "+" : e->op == T_MINUS ? "-" : e->op == T_STAR ? "*" : e->op == T_SLASH ? "/"
                                : e->op == T_PERCENT ? "%" : e->op == T_SHL ? "<<" : e->op == T_SHR ? ">>" : "...");
        put_size_text(out, e->rhs);
        break;
    default: sb_put(out, "..."); break;
    }
}

// `r`'s text from its parts: "Map<K, V>", with that comma and space, and '?' after.
static void set_type_text(type_ref *r)
{
    if (r->args.count == 0 && r->sizes.count == 0 && !r->optional) {
        r->text = r->name;
        return;
    }
    sb text = {0};
    sb_printf(&text, STR_FMT, STR_ARG(r->name));
    for (int i = 0; i < r->args.count; i++) {
        sb_printf(&text, "%s" STR_FMT, i == 0 ? "<" : ", ", STR_ARG(r->args.items[i]->text));
    }
    if (r->args.count > 0) sb_put(&text, ">");
    for (int i = 0; i < r->sizes.count; i++) {
        sb_put(&text, "[");
        put_size_text(&text, r->sizes.items[i]);
        sb_put(&text, "]");
    }
    if (r->optional) sb_put(&text, "?");
    r->text = (str){text.data, (int)text.len};
}

// How long the last part of a name is: 6 for Combat.Health.
static int last_name_len(const str name)
{
    int dot = name.len - 1;
    while (dot >= 0 && name.ptr[dot] != '.') dot--;
    return name.len - dot - 1;
}

// A type: a name, maybe qualified, and for a generic one what it takes in <>,
// as a tree (type_ref): List<T>, Map<K, V>, List<Grid2<Color>>. What may go
// in the <> is the checker's to say (resolve_type), so the tree holds whatever
// is written there.
static type_ref *parse_type(parser *p, const char *what)
{
    const qname q = parse_qname(p, what);
    type_ref *r = NEW(type_ref);
    r->name = q.text;
    r->at = q.at;
    r->name_at = q.name_at;
    r->end = (loc){q.name_at.line, q.name_at.col + last_name_len(q.text), q.name_at.file};
    const generic_form *form = at(p, T_LT) ? generic_form_named(q.text) : NULL;
    if (form) {
        advance(p);
        for (int i = 0; i < form->arity; i++) {
            if (i == 1 && str_eq_c(q.text, "SkinnedMesh") && !at(p, T_COMMA)) break;
            if (i) expect(p, T_COMMA, form->comma);
            vec_push(r->args, parse_type(p, form->hints[i]));
        }
        const loc close = close_angle(p, form->close);
        r->end = (loc){close.line, close.col + 1, close.file};
    }
    while (at(p, T_LBRACKET)) { // int[32]: an array of that many, N a constant expression
        advance(p);
        if (at(p, T_RBRACKET)) {
            diag_error(peek(p)->at, "an array's length goes between the brackets, a constant: 'int[32] cells'");
            diag_note("a list grows as it needs: 'List<int> cells'");
            longjmp(p->fail, 1);
        }
        vec_push(r->sizes, parse_expr(p));
        const token *close = expect(p, T_RBRACKET, "']' after the array's length");
        r->end = (loc){close->at.line, close->at.col + 1, close->at.file};
    }
    set_type_text(r);
    return r;
}

// A declared type, which can be T?: a value or nothing. Its text ends with '?'.
static type_ref *parse_decl_type(parser *p, const char *what)
{
    type_ref *r = parse_type(p, what);
    if (!at(p, T_QUESTION)) return r;
    r->question_at = advance(p)->at;
    r->optional = true;
    r->end = (loc){r->question_at.line, r->question_at.col + 1, r->question_at.file};
    set_type_text(r);
    return r;
}

static expr *new_expr(const expr_kind kind, const loc at)
{
    expr *e = NEW(expr);
    e->kind = kind;
    e->at = at;
    e->spawn_archetype = -1;
    return e;
}

static expr *parse_expr(parser *p);
static expr *parse_out_arg(parser *p);

// `out` before an argument, as in C#: `out health`, `out var health`, `out
// Health health`, `out mut var health`. No expression has two names in a row,
// so the word is a name like any other everywhere else.
static bool at_out_arg(const parser *p)
{
    if (!at(p, T_IDENT) || !str_eq_c(peek(p)->text, "out")) return false;
    const tok_kind next = peek_at(p, 1)->kind;
    return next == T_IDENT || next == T_VAR || next == T_MUT;
}

// (a, b), or with names as in C#: (a, crit: true). No expression starts with a
// name and a ':', so that's an argument's name. An argument for an `out`
// parameter says so: (key, out var value), (found: out health).
static void parse_args(parser *p, expr *call)
{
    expect(p, T_LPAREN, "'('");
    if (!at(p, T_RPAREN)) {
        do {
            const bool named = at(p, T_IDENT) && peek_at(p, 1)->kind == T_COLON;
            if (named || call->arg_names.count > 0) {
                while (call->arg_names.count < call->args.count) { // The ones before the first name
                    vec_push(call->arg_names, ((str){"", 0}));
                    vec_push(call->arg_name_at, ((loc){0}));
                }
                const token *name = named ? advance(p) : NULL;
                if (named) advance(p);
                vec_push(call->arg_names, name ? name->text : ((str){"", 0}));
                vec_push(call->arg_name_at, name ? name->at : ((loc){0}));
            }
            if (at(p, T_MUT)) { // TextField("Code", mut lobby.code): the word is the parameter's
                diag_error(peek(p)->at, "an argument takes no 'mut': a 'mut' parameter changes the variable it's given as it is");
                diag_note("remove 'mut'; only 'out' is written at a call, as its argument can declare a variable");
                longjmp(p->fail, 1);
            }
            vec_push(call->args, at_out_arg(p) ? parse_out_arg(p) : parse_expr(p));
        } while (accept(p, T_COMMA));
    }
    expect(p, T_RPAREN, "')' after arguments");
}

// Transform { position = float3(0, 1, 0), scale = float3(1, 1, 1) }
static void parse_literal_inits(parser *p, expr *lit)
{
    expect(p, T_LBRACE, "'{'");
    while (!at(p, T_RBRACE)) {
        const token *name = expect_ident(p, "field name");
        expect(p, T_ASSIGN, "'=' after field name");
        const field_init init = {name->text, name->at, parse_expr(p), NULL};
        vec_push(lit->inits, init);
        if (!accept(p, T_COMMA)) break;
    }
    expect(p, T_RBRACE, "'}' after fields");
}

static expr *parse_primary(parser *p)
{
    const token *t = peek(p);
    switch (t->kind) {
    case T_DOT: {
        advance(p);
        const token *name = expect(p, T_IDENT, "an enum member after '.'");
        expr *e = new_expr(E_MEMBER, name->at);
        e->member = name->text;
        e->enum_shorthand = true;
        e->object = new_expr(E_NAME, t->at);
        e->object->name = str_from("");
        return e;
    }
    case T_INT: {
        advance(p);
        expr *e = new_expr(E_INT, t->at);
        e->text = t->text;
        str digits = t->text;
        int base = 10;
        if (digits.len > 2 && digits.ptr[0] == '0' && (digits.ptr[1] == 'x' || digits.ptr[1] == 'X')) base = 16;
        if (digits.len > 2 && digits.ptr[0] == '0' && (digits.ptr[1] == 'b' || digits.ptr[1] == 'B')) base = 2;
        if (base != 10) {
            digits.ptr += 2;
            digits.len -= 2;
        }
        // An L suffix makes a long, and so does a value past int's range.
        // Decimal literals go up to INT64_MAX. Hex and binary literals can use
        // all 32 bits of an int, or all 64 of a long, and are read as its bit
        // pattern: 0xFFFFFFFF is -1, and 0xFFFFFFFFL is 4294967295.
        bool suffix = false;
        if (digits.len > 0 && (digits.ptr[digits.len - 1] == 'L' || digits.ptr[digits.len - 1] == 'l')) {
            suffix = true;
            digits.len--;
        }
        const uint64_t limit = base == 10 ? 9223372036854775807ull : UINT64_MAX;
        uint64_t v = 0;
        for (int i = 0; i < digits.len; i++) {
            const char ch = digits.ptr[i];
            if (ch == '_') continue; // Digit separator.
            const uint64_t d = (uint64_t)(ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
            if (v > (limit - d) / (uint64_t)base) {
                diag_error(t->at, "integer '" STR_FMT "' is too large for long", STR_ARG(t->text));
                longjmp(p->fail, 1);
            }
            v = v * (uint64_t)base + d;
        }
        const uint64_t int_limit = base == 10 ? 2147483647ull : 4294967295ull;
        e->is_long = suffix || v > int_limit;
        if (e->is_long) e->int_value = (int64_t)v;
        else e->int_value = base == 10 ? (int64_t)v : (int64_t)(int32_t)(uint32_t)v;
        return e;
    }
    case T_FLOAT: {
        advance(p);
        expr *e = new_expr(E_FLOAT, t->at);
        e->text = t->text;
        if (t->text.len >= 3 && memcmp(t->text.ptr + t->text.len - 3, "deg", 3) == 0) {
            // Read directly as binary32, then use the same binary32 multiply
            // as Math.Radians. Keep the result, so C and shaders get one value
            // and the original spelling stays available to editor tools.
            char *digits = arena_alloc((size_t)t->text.len - 2);
            int count = 0;
            for (int i = 0; i < t->text.len - 3; i++) {
                if (t->text.ptr[i] != '_') digits[count++] = t->text.ptr[i];
            }
            digits[count] = '\0';
            const float degrees = strtof(digits, NULL);
            if (!isfinite(degrees)) {
                diag_error(t->at, "degree literal '" STR_FMT "' is too large for float", STR_ARG(t->text));
                longjmp(p->fail, 1);
            }
            e->is_degrees = true;
            e->float_value = tide_radians_f(degrees);
        }
        return e;
    }
    case T_TRUE:
    case T_FALSE: {
        advance(p);
        expr *e = new_expr(E_BOOL, t->at);
        e->bool_value = t->kind == T_TRUE;
        return e;
    }
    case T_STRING: {
        advance(p);
        expr *e = new_expr(E_STRING, t->at);
        e->text = t->text;
        return e;
    }
    case T_CHAR: {
        // 'a': a number that's a char, its code point, so what reads an int literal reads it
        advance(p);
        expr *e = new_expr(E_INT, t->at);
        e->text = t->text;
        e->is_char = true;
        const unsigned char *s = (const unsigned char *)t->text.ptr + 1; // Past the quote; the lexer checked what's between
        const int bytes = t->text.len - 2;
        if (s[0] == '\\') {
            const char esc = (char)s[1];
            e->int_value = esc == 'n' ? '\n' : esc == 'r' ? '\r' : esc == 't' ? '\t' : esc == '0' ? 0 : esc;
        } else if (bytes == 1) {
            e->int_value = s[0];
        } else if (bytes == 2) {
            e->int_value = (s[0] & 0x1F) << 6 | (s[1] & 0x3F);
        } else if (bytes == 3) {
            e->int_value = (s[0] & 0x0F) << 12 | (s[1] & 0x3F) << 6 | (s[2] & 0x3F);
        } else {
            e->int_value = (s[0] & 0x07) << 18 | (s[1] & 0x3F) << 12 | (s[2] & 0x3F) << 6 | (s[3] & 0x3F);
        }
        return e;
    }
    case T_THIS:
        advance(p);
        return new_expr(E_THIS, t->at);
    case T_DEFAULT:
        advance(p);
        return new_expr(E_DEFAULT, t->at);
    case T_NULL:
        advance(p);
        return new_expr(E_NULL, t->at);
    case T_INTERP: {
        // $"a {x} b {y:F2} c": the tokens are `$"a {`, x, `} b {`, y, `:F2`, `} c"`.
        advance(p);
        expr *e = new_expr(E_INTERP, t->at);
        vec_push(e->parts, ((str){t->text.ptr + 2, t->text.len - 3}));
        bool open = t->text.ptr[t->text.len - 1] == '{';
        while (open) {
            vec_push(e->args, parse_expr(p));
            str format = {"", 0};
            if (at(p, T_INTERP_FORMAT)) {
                const token *f = advance(p);
                format = (str){f->text.ptr + 1, f->text.len - 1};
            }
            vec_push(e->formats, format);
            const token *part = expect(p, T_INTERP_PART, "'}' to end the value in the text");
            vec_push(e->parts, ((str){part->text.ptr + 1, part->text.len - 2}));
            open = part->text.ptr[part->text.len - 1] == '{';
        }
        return e;
    }
    case T_LPAREN: {
        advance(p);
        expr *e = parse_expr(p);
        expect(p, T_RPAREN, "')'");
        return e;
    }
    case T_LBRACKET: { // [a, b, c]: a list
        advance(p);
        expr *e = new_expr(E_LIST, t->at);
        if (!at(p, T_RBRACKET)) {
            do {
                vec_push(e->args, parse_expr(p));
            } while (accept(p, T_COMMA) && !at(p, T_RBRACKET));
        }
        expect(p, T_RBRACKET, "']' after the list's elements");
        return e;
    }
    case T_IDENT: {
        if (str_eq_c(t->text, "SkinnedMesh") && peek_at(p, 1)->kind == T_LT) {
            const type_ref *model = parse_type(p, "a skinned mesh type");
            expr *e = new_expr(E_NAME, t->at);
            e->name = model->text;
            e->type_arg_ref = model;
            return e;
        }
        advance(p);
        if (at(p, T_LPAREN)) {
            expr *e = new_expr(E_CALL, t->at);
            e->name = t->text;
            parse_args(p, e);
            return e;
        }
        if (at(p, T_LBRACE)) {
            expr *e = new_expr(E_LITERAL, t->at);
            e->name = t->text;
            e->qual_at = t->at;
            parse_literal_inits(p, e);
            return e;
        }
        expr *e = new_expr(E_NAME, t->at);
        e->name = t->text;
        return e;
    }
    default:
        fail_at(p, t, "an expression");
    }
}

// Whether the '<' the parser is at starts a method's type, as in
// world.Get<Health>(e), rather than a comparison: a name, maybe qualified,
// then '>' and the call's '('. As in C#, what follows the '>' decides, and
// no comparison reads that way (it would compare a bool). The type can take
// types of its own, as in Json.Read<List<Score>>(text): names, dots and
// commas up to the '>' that closes the first '<', which a '>>' can be part of.
static bool at_type_argument(const parser *p)
{
    int i = 1;
    if (peek_at(p, i)->kind != T_IDENT) return false;
    int depth = 1;
    while (depth > 0) {
        const tok_kind k = peek_at(p, i)->kind;
        if (k == T_LT) depth++;
        else if (k == T_GT) depth--;
        else if (k == T_SHR) depth -= 2;
        else if (k != T_IDENT && k != T_DOT && !(k == T_COMMA && depth > 1)) return false; // Map<string, int>'s comma
        i++;
    }
    return depth == 0 && peek_at(p, i)->kind == T_LPAREN;
}

static stmt *new_stmt(stmt_kind kind, loc at);

// ++i, i++, --i or i--, as a value: with the assignment it makes, `i += 1`,
// which is the whole of it where it's a statement (parse_simple).
static expr *new_step(const token *op, expr *target, const bool postfix)
{
    expr *e = new_expr(E_STEP, op->at);
    e->op = op->kind;
    e->lhs = target;
    e->postfix = postfix;
    stmt *s = new_stmt(S_ASSIGN, op->at);
    s->target = target;
    s->op = op->kind;
    s->value = new_expr(E_INT, op->at);
    s->value->int_value = 1;
    e->step = s;
    return e;
}

static expr *parse_postfix(parser *p)
{
    expr *e = parse_primary(p);
    while (at(p, T_DOT) || at(p, T_LBRACKET) || at(p, T_NOT) || at(p, T_PLUS_PLUS) || at(p, T_MINUS_MINUS)) {
        if (at(p, T_PLUS_PLUS) || at(p, T_MINUS_MINUS)) { // i++: i's value, which then goes up by 1
            e = new_step(advance(p), e, true);
            continue;
        }
        if (at(p, T_NOT)) { // Parse(text)!: its value, or the default when it fails
            const token *bang = advance(p);
            expr *value = new_expr(E_DEFAULTED, bang->at);
            value->lhs = e;
            e = value;
            continue;
        }
        if (at(p, T_LBRACKET)) { // items[i], or a grid's cells[x, y]: cells[int2(x, y)]
            const token *open = advance(p);
            expr *index = new_expr(E_INDEX, open->at);
            index->object = e;
            index->lhs = parse_expr(p);
            if (at(p, T_COMMA)) {
                expr *position = new_expr(E_CALL, index->lhs->at);
                vec_push(position->args, index->lhs);
                while (accept(p, T_COMMA)) vec_push(position->args, parse_expr(p));
                position->name = str_from(position->args.count == 2 ? "int2" : "int3");
                index->lhs = position;
            }
            expect(p, T_RBRACKET, "']' after the index");
            e = index;
            continue;
        }
        advance(p);
        const token *name = expect_ident(p, "member name after '.'");
        if (at(p, T_LPAREN) || (at(p, T_LT) && at_type_argument(p))) {
            expr *m = new_expr(E_METHOD, name->at);
            m->object = e;
            m->name = name->text;
            if (accept(p, T_LT)) { // world.Get<Health>(e)
                const type_ref *arg = parse_type(p, "a type in '<>'");
                m->type_arg = arg->text;
                m->type_arg_at = arg->name_at;
                m->type_arg_qual_at = arg->at;
                m->type_arg_ref = arg;
                close_angle(p, "'>' after the type");
            }
            parse_args(p, m);
            e = m;
        } else {
            expr *m = new_expr(E_MEMBER, name->at);
            m->object = e;
            m->member = name->text;
            e = m;
        }
    }
    // Combat.Health { ... }: a component literal with a qualified name.
    if (at(p, T_LBRACE) && e->kind == E_MEMBER) {
        str text = {NULL, 0};
        const expr *first = e;
        bool names_only = true;
        for (const expr *part = e; part; part = part->kind == E_MEMBER ? part->object : NULL) {
            if (part->kind != E_MEMBER && part->kind != E_NAME) names_only = false;
            first = part;
        }
        if (names_only) {
            text = first->name;
            // Rebuild "A.B.C" from the chain, innermost first.
            const expr *chain[16];
            int n = 0;
            for (const expr *part = e; part->kind == E_MEMBER && n < 16; part = part->object) chain[n++] = part;
            for (int i = n - 1; i >= 0; i--) text = join_names(text, chain[i]->member);
            expr *lit = new_expr(E_LITERAL, e->at);
            lit->name = text;
            lit->qual_at = first->at;
            parse_literal_inits(p, lit);
            return lit;
        }
    }
    return e;
}

static expr *parse_unary(parser *p)
{
    // (int)value or (Game.Phase)value: unmistakable cast syntax. Do not
    // consume operators that can follow a valid parenthesized expression,
    // such as (phase) - 1, (value)++, (items)[i] or (result)!.
    if (at(p, T_LPAREN) && peek_at(p, 1)->kind == T_IDENT) {
        int close = 2;
        while (peek_at(p, close)->kind == T_DOT && peek_at(p, close + 1)->kind == T_IDENT) close += 2;
        if (peek_at(p, close)->kind == T_RPAREN) {
            const tok_kind next = peek_at(p, close + 1)->kind;
            if (next == T_IDENT || next == T_THIS || next == T_INT || next == T_FLOAT || next == T_CHAR
                || next == T_TRUE || next == T_FALSE || next == T_DEFAULT || next == T_NULL
                || next == T_LPAREN || next == T_TILDE) {
                const token *open = advance(p);
                const token *name = peek(p);
                const token *end = peek_at(p, close - 2);
                const str target = {name->text.ptr, (int)(end->text.ptr + end->text.len - name->text.ptr)};
                for (int i = 0; i < close; i++) advance(p);
                const token *value = peek(p);
                parse_unary(p);
                const token *last = &p->toks[p->pos - 1];
                const str operand = {value->text.ptr, (int)(last->text.ptr + last->text.len - value->text.ptr)};
                diag_error(open->at, "Tide converts values with '" STR_FMT "(value)', not '(" STR_FMT ")value'",
                           STR_ARG(target), STR_ARG(target));
                diag_note("write '" STR_FMT "(" STR_FMT ")' instead", STR_ARG(target), STR_ARG(operand));
                longjmp(p->fail, 1);
            }
        }
    }
    // try Parse(text): binds like a unary operator, as C#'s await does.
    if (at(p, T_TRY)) {
        const token *keyword = advance(p);
        if (at(p, T_LBRACE)) {
            diag_error(keyword->at, "Tide has no 'try { } catch': errors are values, which code handles where it calls");
            diag_note("'try' goes before a call that can fail and passes its error on: 'var score = try Parse(text);'");
            longjmp(p->fail, 1);
        }
        expr *e = new_expr(E_TRY, keyword->at);
        e->lhs = parse_unary(p);
        return e;
    }
    // await Load(name): binds like try. A `!` after the call is the awaited
    // value's, as in `await Load(name)!`: its default when the task failed.
    if (at(p, T_AWAIT)) {
        const token *keyword = advance(p);
        expr *e = new_expr(E_AWAIT, keyword->at);
        e->lhs = parse_unary(p);
        if (e->lhs->kind != E_DEFAULTED) return e;
        expr *outer = e->lhs;
        while (outer->lhs->kind == E_DEFAULTED) outer = outer->lhs;
        expr *top = e->lhs;
        e->lhs = outer->lhs;
        outer->lhs = e;
        return top;
    }
    if (at(p, T_PLUS_PLUS) || at(p, T_MINUS_MINUS)) { // ++i: i goes up by 1, and that's the value
        const token *op = advance(p);
        return new_step(op, parse_unary(p), false);
    }
    if (at(p, T_NOT) || at(p, T_MINUS) || at(p, T_TILDE)) {
        const token *op = advance(p);
        expr *e = new_expr(E_UNARY, op->at);
        e->op = op->kind;
        e->lhs = parse_unary(p);
        return e;
    }
    return parse_postfix(p);
}

// C# precedence, lowest first.
static int binary_precedence(const tok_kind kind)
{
    switch (kind) {
    case T_OR: return 1;
    case T_AND: return 2;
    case T_PIPE: return 3;
    case T_CARET: return 4;
    case T_AMP: return 5;
    case T_EQ:
    case T_NE: return 6;
    case T_LT:
    case T_LE:
    case T_GT:
    case T_GE: return 7;
    case T_SHL:
    case T_SHR: return 8;
    case T_PLUS:
    case T_MINUS: return 9;
    case T_STAR:
    case T_SLASH:
    case T_PERCENT: return 10;
    default: return 0;
    }
}

#define IS_PRECEDENCE 7 // `is` is relational, as in C#

// x is int score, x is ParseError why, x is ParseError.Empty, or without the name.
static expr *parse_is(parser *p, expr *lhs)
{
    const token *keyword = advance(p);
    expr *e = new_expr(E_IS, keyword->at);
    e->lhs = lhs;
    const type_ref *type = parse_type(p, "a type after 'is', like 'is int score'");
    e->pattern = type->text;
    e->pattern_at = type->name_at;
    e->pattern_qual_at = type->at;
    e->pattern_ref = type;
    if (at(p, T_IDENT)) {
        const token *name = advance(p);
        stmt *binding = NEW(stmt);
        binding->kind = S_VAR;
        binding->at = type->at;
        binding->name = name->text;
        binding->name_at = name->at;
        binding->type_name = type->text;
        binding->type_at = type->name_at;
        binding->type_qual_at = type->at;
        binding->type_ref = type;
        e->binding = binding;
    }
    return e;
}

// Precedence climbing; all binary operators are left-associative.
static expr *parse_binary(parser *p, const int min_prec)
{
    expr *lhs = parse_unary(p);
    for (;;) {
        if (at(p, T_SWITCH) && min_prec <= 11) {
            const token *word = advance(p);
            expr *e = new_expr(E_SWITCH, word->at);
            e->object = lhs;
            expect(p, T_LBRACE, "'{' and the switch expression's arms");
            while (!at(p, T_RBRACE) && !at(p, T_EOF)) {
                field_init arm = {.at = peek(p)->at};
                if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "_")) advance(p);
                else arm.value = parse_expr(p);
                expect(p, T_ARROW, "'=>' after a switch expression's case");
                vec_push(e->inits, arm);
                vec_push(e->args, parse_expr(p));
                if (!accept(p, T_COMMA)) break;
            }
            const token *close = expect(p, T_RBRACE, "'}' after the switch expression's arms");
            e->end = close->at;
            e->end.col++;
            lhs = e;
            continue;
        }
        if (at(p, T_IS) && IS_PRECEDENCE >= min_prec) {
            lhs = parse_is(p, lhs);
            continue;
        }
        const int prec = binary_precedence(peek(p)->kind);
        if (prec == 0 || prec < min_prec) return lhs;
        const token *op = advance(p);
        expr *e = new_expr(E_BINARY, op->at);
        e->op = op->kind;
        e->lhs = lhs;
        e->rhs = parse_binary(p, prec + 1);
        lhs = e;
    }
}

// a ?? b binds looser than || and tighter than ?:, and groups to the right,
// as in C#: a ?? b ?? 0 is a ?? (b ?? 0).
static expr *parse_coalesce(parser *p)
{
    expr *lhs = parse_binary(p, 1);
    if (!at(p, T_COALESCE)) return lhs;
    const token *op = advance(p);
    expr *e = new_expr(E_COALESCE, op->at);
    e->lhs = lhs;
    e->rhs = parse_coalesce(p);
    return e;
}

// cond ? a : b binds looser than every binary operator and groups to the
// right, as in C#: a ? b : c ? d : e is a ? b : (c ? d : e).
static expr *parse_expr(parser *p)
{
    expr *cond = parse_coalesce(p);
    if (!at(p, T_QUESTION)) return cond;
    const token *question = advance(p);
    expr *e = new_expr(E_CONDITIONAL, question->at);
    e->cond = cond;
    e->lhs = parse_expr(p);
    if (at(p, T_INTERP_FORMAT)) { // $"{a ? b : c}": the ':' started a format
        diag_error(peek(p)->at, "in text, '?:' goes in parentheses: '{(a ? b : c)}'");
        diag_note("a ':' after a value in text starts its format, like '{x:F2}'");
        longjmp(p->fail, 1);
    }
    expect(p, T_COLON, "':' and the value for when the condition is false");
    e->rhs = parse_expr(p);
    return e;
}

// ---------------------------------------------------------------------------
// Statements

static stmt *new_stmt(const stmt_kind kind, const loc at)
{
    stmt *s = NEW(stmt);
    s->kind = kind;
    s->at = at;
    return s;
}

static stmt *parse_stmt(parser *p);

// After `out`: `Health health`, `Combat.Health health`, `List<int> found` or
// `int? best`, a type and the name of the local the argument declares, rather
// than a variable the code has. What follows the name ends the argument.
static bool at_out_decl(const parser *p)
{
    if (!at(p, T_IDENT) || (peek_at(p, 1)->kind == T_LT && !generic_name(peek(p)->text))) return false;
    int i = skip_type(p, 0);
    if (i < 0 || peek_at(p, i)->kind != T_IDENT) return false;
    const tok_kind after = peek_at(p, i + 1)->kind;
    return after == T_COMMA || after == T_RPAREN;
}

// An argument after `out`. `out var health`, `out Health health` and, for a
// local the code changes later, `out mut var health` declare the variable
// where they're written: an E_NAME of it, with the S_VAR it declares, which
// has no value. Anything else is a variable the code has, or a part of one,
// which the checker says whether the call can make; `out _` among them, which
// leaves what the call makes out when nothing is named `_`.
static expr *parse_out_arg(parser *p)
{
    const token *word = advance(p);
    if (!at(p, T_MUT) && !at(p, T_VAR) && !at_out_decl(p)) {
        expr *e = parse_postfix(p);
        e->is_out = true;
        e->out_at = word->at;
        return e;
    }
    stmt *local = new_stmt(S_VAR, peek(p)->at);
    local->is_mut = accept(p, T_MUT);
    local->out_decl = true;
    if (!accept(p, T_VAR)) {
        const type_ref *type = parse_decl_type(p, "'var' or the type of the variable 'out' declares, like 'out var health'");
        local->type_name = type->text;
        local->type_at = type->name_at;
        local->type_qual_at = type->at;
        local->type_ref = type;
    }
    const token *name = expect_ident(p, "the name of the variable 'out' declares, like 'out var health'");
    local->name = name->text;
    local->name_at = name->at;
    expr *e = new_expr(E_NAME, name->at);
    e->name = name->text;
    e->is_out = true;
    e->out_at = word->at;
    e->out_local = local;
    local->out_arg = e;
    return e;
}

// Contextual keywords: special only where a declaration or a file header can
// start, so they can still name parameters and locals.
static bool is_decl_word(const str text)
{
    return str_eq_c(text, "input") || str_eq_c(text, "view") || str_eq_c(text, "struct") || str_eq_c(text, "event") || str_eq_c(text, "command")
        || str_eq_c(text, "enum") || str_eq_c(text, "scene") || str_eq_c(text, "local") || str_eq_c(text, "namespace")
        || str_eq_c(text, "using") || str_eq_c(text, "extern") || str_eq_c(text, "const") || str_eq_c(text, "frag")
        || str_eq_c(text, "vert") || str_eq_c(text, "asset") || str_eq_c(text, "audio");
}

// Keywords that only start declarations, and the contextual ones at the start
// of a line: where recovery can safely pick up again.
bool attributes_before_field(const token *toks, int i)
{
    do { // [A(...)] [B] ...: skip to after the last ']'
        int depth = 0;
        for (; toks[i].kind != T_EOF; i++) {
            if (toks[i].kind == T_LBRACKET) depth++;
            else if (toks[i].kind == T_RBRACKET && --depth == 0) break;
        }
        if (toks[i].kind == T_EOF) return false;
        i++;
    } while (toks[i].kind == T_LBRACKET);
    const token *t = &toks[i];
    // [Snap] local float x;: a local field, where `local` then a declaration's word starts one
    if (t->kind == T_IDENT && str_eq_c(t->text, "local") && toks[i + 1].kind == T_IDENT && !is_decl_word(toks[i + 1].text)) {
        const tok_kind after = toks[i + 2].kind;
        return after == T_IDENT || after == T_DOT || after == T_LT || after == T_QUESTION;
    }
    return t->kind == T_IDENT && (toks[i + 1].kind == T_IDENT || toks[i + 1].kind == T_DOT || toks[i + 1].kind == T_QUESTION)
        && !is_decl_word(t->text);
}

// [mut] Type Name(, `from` tokens on: a method or function, rather than a
// field or a local. With `qualified`, the name can be: `float Combat.Damage(`,
// an override's, which names what it replaces.
static bool at_method_from(const parser *p, const int from, const bool qualified)
{
    int i = from + (peek_at(p, from)->kind == T_MUT ? 1 : 0);
    if (peek_at(p, i)->kind != T_IDENT || is_decl_word(peek_at(p, i)->text)) return false; // view Name(...)
    i = skip_type(p, i); // List<Item> Name(...), Map<int, Item> Name(...), int? Find(...)
    if (i < 0 || peek_at(p, i)->kind != T_IDENT) return false;
    i++;
    while (qualified && peek_at(p, i)->kind == T_DOT && peek_at(p, i + 1)->kind == T_IDENT) i += 2;
    return peek_at(p, i)->kind == T_LPAREN;
}

// [mut] Type Name(: a method or function, rather than a field or a local.
static bool at_method(const parser *p)
{
    return at_method_from(p, 0, false);
}

static bool is_replace_word(const str text)
{
    return str_eq_c(text, "virtual") || str_eq_c(text, "override");
}

static str word_and_space(const token *word);
static void refuse_replace_word(const token *word, const char *why);

// `virtual` and `override` before a declaration with code, as in C#. Words of
// their own only there: before a declaration's word (`virtual system`,
// `override view`, `virtual event(`, `virtual local event(`, `virtual async
// void`) or a function's or method's type and name (`virtual float Damage(`,
// `override bool Stats.IsDead(`). Anywhere else they're names like any other,
// a function's type too (`virtual Make(`). In a type's body (`in_type`), only
// a method or an operator follows one.
static bool at_replace_word(const parser *p, const bool in_type)
{
    const token *t = peek(p);
    if (t->kind != T_IDENT || !is_replace_word(t->text)) return false;
    const token *next = peek_at(p, 1);
    if (next->kind == T_SYSTEM || next->kind == T_COMPONENT || next->kind == T_SINGLETON) return !in_type;
    if (next->kind == T_MUT) return true; // virtual mut void Hurt(...)
    if (next->kind != T_IDENT) return false;
    const tok_kind after = peek_at(p, 2)->kind;
    if (str_eq_c(next->text, "operator")) return false; // A type named so, with an operator
    if (!in_type && after == T_LPAREN) return str_eq_c(next->text, "event") || str_eq_c(next->text, "command"); // virtual event(Hit hit) ...
    if (!in_type && (is_decl_word(next->text) || str_eq_c(next->text, "async") || is_replace_word(next->text))) {
        return after == T_IDENT || after == T_LPAREN || after == T_MUT || after == T_SYSTEM || after == T_COMPONENT
            || after == T_SINGLETON;
    }
    // A type and a name, then '(': a function's or a method's. An operator's is `Type operator`.
    if (after == T_IDENT && str_eq_c(peek_at(p, 2)->text, "operator")) return true;
    for (int i = 2; peek_at(p, i)->kind == T_DOT && peek_at(p, i + 1)->kind == T_IDENT; i += 2) {
        if (peek_at(p, i + 2)->kind == T_IDENT && str_eq_c(peek_at(p, i + 2)->text, "operator")) return true;
    }
    return at_method_from(p, 1, true);
}

// Where recovery can pick up again: a declaration's start. `Type Name(` at
// column 1 starts a function, except in a type's body (`functions` false),
// where it's a method. Column 1 is a file's: inside a namespace block it's the
// column the block's declarations are at (unit.decl_col).
static bool at_decl_start_or_function(const parser *p, const bool functions)
{
    const token *t = peek(p);
    const int col = p->unit->decl_col; // 1, or where the namespace block being read has its declarations
    if (t->kind == T_COMPONENT || t->kind == T_SINGLETON || t->kind == T_SYSTEM) return true;
    if (t->kind == T_LBRACKET && t->at.col == col) return !attributes_before_field(p->toks, p->pos);
    if (t->kind == T_IDENT && t->at.col == col && peek_at(p, 1)->kind == T_IDENT && is_decl_word(t->text)) {
        // `local float x;` is a field; `local event Blink` starts a declaration
        if (!str_eq_c(t->text, "local")) return true;
        const str next = peek_at(p, 1)->text;
        if (peek_at(p, 2)->kind != T_SEMI && (is_decl_word(next) || str_eq_c(next, "async"))) return true;
    }
    if (t->kind == T_IDENT && t->at.col == col && peek_at(p, 1)->kind == T_LPAREN
        && (str_eq_c(t->text, "event") || str_eq_c(t->text, "command"))) return true;
    if (t->kind == T_IDENT && t->at.col == col && peek_at(p, 1)->kind == T_LBRACE && str_eq_c(t->text, "settings")) return true;
    // `virtual system ...`, `override float Combat.Damage(...)`. In a type's body a method can be virtual,
    // so there only a declaration's own word after it starts one
    if (t->kind == T_IDENT && t->at.col == col && is_replace_word(t->text) && at_replace_word(p, false)) {
        const token *next = peek_at(p, 1);
        if (functions || next->kind == T_SYSTEM || next->kind == T_COMPONENT || next->kind == T_SINGLETON) return true;
        if (next->kind == T_IDENT && (is_decl_word(next->text) || str_eq_c(next->text, "async"))) return true;
    }
    if (t->kind == T_IDENT && t->at.col == col && peek_at(p, 1)->kind == T_IDENT && str_eq_c(t->text, "async")) return true;
    if (t->kind == T_IDENT && t->at.col == col && peek_at(p, 1)->kind == T_SYSTEM && str_eq_c(t->text, "extern")) return true; // extern system
    if (t->kind == T_IDENT && t->at.col == col && str_eq_c(t->text, "local")
        && (peek_at(p, 1)->kind == T_COMPONENT || peek_at(p, 1)->kind == T_SINGLETON || peek_at(p, 1)->kind == T_SYSTEM)) {
        return true;
    }
    return functions && (t->kind == T_IDENT || t->kind == T_MUT) && t->at.col == col && at_method(p);
}

static bool at_decl_start(const parser *p)
{
    return at_decl_start_or_function(p, true);
}

// After a syntax error in a statement or field: skips to the end of it (a ';'
// at this level), stopping before the '}' that closes the enclosing block and
// before the next declaration.
static void skip_statement(parser *p)
{
    int depth = 0;
    while (!at(p, T_EOF) && !at_decl_start(p)) {
        const tok_kind kind = peek(p)->kind;
        if (kind == T_RBRACE && depth == 0) return;
        advance(p);
        if (kind == T_LBRACE) depth++;
        else if (kind == T_RBRACE) depth--;
        else if (kind == T_SEMI && depth == 0) return;
    }
}

// Runs one statement or field parser, recovering from a syntax error in it.
// Returns false if it failed; the parser then stands after the broken part.
#define RECOVERING(p, action)                                                  \
    do {                                                                       \
        jmp_buf outer_;                                                        \
        memcpy(outer_, (p)->fail, sizeof outer_);                              \
        if (setjmp((p)->fail) == 0) {                                          \
            action;                                                            \
        } else {                                                               \
            (p)->has_rest = false;                                             \
            skip_statement(p);                                                 \
        }                                                                      \
        memcpy((p)->fail, outer_, sizeof outer_);                              \
    } while (0)

static stmt *parse_block(parser *p)
{
    const token *open = expect(p, T_LBRACE, "'{'");
    stmt *s = new_stmt(S_BLOCK, open->at);
    while (!at(p, T_RBRACE)) {
        if (at(p, T_EOF) || (p->recover && at_decl_start(p))) {
            if (!p->recover) fail_at(p, peek(p), "'}'");
            diag_error(peek(p)->at, "expected '}' to close the block on line %d", open->at.line);
            s->end = peek(p)->at;
            return s;
        }
        if (p->recover) RECOVERING(p, vec_push(s->stmts, parse_stmt(p)));
        else vec_push(s->stmts, parse_stmt(p));
    }
    s->end = peek(p)->at;
    advance(p);
    return s;
}

static bool is_assign_op(const tok_kind kind)
{
    return kind == T_ASSIGN || kind == T_PLUS_ASSIGN || kind == T_MINUS_ASSIGN
        || kind == T_STAR_ASSIGN || kind == T_SLASH_ASSIGN || kind == T_PERCENT_ASSIGN
        || kind == T_SHL_ASSIGN || kind == T_SHR_ASSIGN || kind == T_AMP_ASSIGN
        || kind == T_PIPE_ASSIGN || kind == T_CARET_ASSIGN;
}

// [mut] (var | Type) name = value;
static stmt *parse_var(parser *p)
{
    const token *first = peek(p);
    stmt *s = new_stmt(S_VAR, first->at);
    s->is_mut = accept(p, T_MUT);
    if (!accept(p, T_VAR)) {
        const type_ref *type = parse_decl_type(p, "'var' or a type");
        s->type_name = type->text;
        s->type_at = type->name_at;
        s->type_qual_at = type->at;
        s->type_ref = type;
    }
    const token *name = expect_ident(p, "variable name");
    s->name = name->text;
    s->name_at = name->at;
    if (at(p, T_SEMI) && s->type_name.len > 0) {
        advance(p);
        s->delayed = true;
        return s;
    }
    if (!at(p, T_ASSIGN)) {
        diag_error(name->at, "local variable '" STR_FMT "' needs an initial value", STR_ARG(s->name));
        longjmp(p->fail, 1);
    }
    advance(p);
    s->value = parse_expr(p);
    expect(p, T_SEMI, "';'");
    return s;
}

// switch (value) { case A: ... break; case B: case C: ... break; default: ... break; }
// Labels in a row share one section.
static stmt *parse_switch(parser *p)
{
    const token *keyword = advance(p);
    stmt *s = new_stmt(S_SWITCH, keyword->at);
    expect(p, T_LPAREN, "'(' after 'switch'");
    s->cond = parse_expr(p);
    expect(p, T_RPAREN, "')' after the switch's value");
    const token *open = expect(p, T_LBRACE, "'{' and the switch's cases");
    while (!at(p, T_RBRACE)) {
        if (at(p, T_EOF) || (p->recover && at_decl_start(p))) {
            if (!p->recover) fail_at(p, peek(p), "'}'");
            diag_error(peek(p)->at, "expected '}' to close the switch on line %d", open->at.line);
            s->end = peek(p)->at;
            return s;
        }
        if (!at(p, T_CASE) && !at(p, T_DEFAULT)) fail_at(p, peek(p), "'case' or 'default'");
        switch_case section = {0};
        while (at(p, T_CASE) || at(p, T_DEFAULT)) {
            const token *label = advance(p);
            vec_push(section.label_at, label->at);
            expr *value = label->kind == T_CASE ? parse_expr(p) : NULL;
            vec_push(section.labels, value);
            expect(p, T_COLON, label->kind == T_CASE ? "':' after the case's value" : "':' after 'default'");
        }
        while (!at(p, T_CASE) && !at(p, T_DEFAULT) && !at(p, T_RBRACE) && !at(p, T_EOF)
               && !(p->recover && at_decl_start(p))) {
            if (p->recover) RECOVERING(p, vec_push(section.body, parse_stmt(p)));
            else vec_push(section.body, parse_stmt(p));
        }
        vec_push(s->cases, section);
    }
    s->end = peek(p)->at;
    advance(p);
    return s;
}

// `Type name`, `Combat.Stats name` or `int? name =`: a local's declaration,
// rather than an expression.
static bool at_local_decl(const parser *p)
{
    const token *t = peek(p);
    if (t->kind != T_IDENT) return false;
    if (generic_name(t->text) && peek_at(p, 1)->kind == T_LT) {
        const int end = skip_type(p, 0);
        return end > 0 && peek_at(p, end)->kind == T_IDENT;
    }
    int next = 1;
    while (peek_at(p, next)->kind == T_DOT && peek_at(p, next + 1)->kind == T_IDENT) next += 2;
    if (peek_at(p, next)->kind == T_IDENT) return true;
    if (peek_at(p, next)->kind == T_LBRACKET) { // int[32] cells = ..., rather than cells[i] = ...
        const int end = skip_type(p, 0);
        return end > 0 && peek_at(p, end)->kind == T_IDENT && peek_at(p, end - 1)->kind != T_QUESTION;
    }
    const tok_kind after = peek_at(p, next + 2)->kind;
    return peek_at(p, next)->kind == T_QUESTION && peek_at(p, next + 1)->kind == T_IDENT
        && (after == T_ASSIGN || after == T_SEMI);
}

// i++, --i, x = y, x += y or a call: a statement without its ';', as a for's
// step and as the start of most statements.
static stmt *parse_simple(parser *p)
{
    expr *e = parse_expr(p);
    if (e->kind == E_STEP) return e->step; // ++i or i++ alone: its assignment, as nothing takes its value
    if (e->kind == E_NAME && str_eq_c(e->name, "_") && at(p, T_ASSIGN)) {
        // `_ = Compute();`: the value is worked out and dropped, on purpose. It's a variable no code
        // can name, so everything a variable's value goes through is this one's too.
        advance(p);
        stmt *s = new_stmt(S_VAR, e->at);
        char *name = arena_alloc(48);
        snprintf(name, 48, "_dropped_%d_%d", e->at.line, e->at.col);
        s->name = str_from(name);
        s->name_at = e->at;
        s->discard = true;
        s->read = true; // Nothing reads it, which is the point
        s->value = parse_expr(p);
        return s;
    }
    if (is_assign_op(peek(p)->kind)) {
        const token *op = advance(p);
        stmt *s = new_stmt(S_ASSIGN, op->at);
        s->target = e;
        s->op = op->kind;
        s->value = parse_expr(p);
        return s;
    }
    stmt *s = new_stmt(S_EXPR, e->at);
    s->value = e;
    return s;
}

// for (init; cond; step) body, each part optional: for (var i = 0; i < n; i++)
static stmt *parse_for(parser *p)
{
    const token *keyword = advance(p);
    stmt *s = new_stmt(S_FOR, keyword->at);
    expect(p, T_LPAREN, "'(' after 'for'");
    if (!accept(p, T_SEMI)) {
        const token *t = peek(p);
        if (t->kind == T_MUT || t->kind == T_VAR || at_local_decl(p)) {
            s->init = parse_var(p); // Takes the ';'
            s->init->loop_var = true;
        } else {
            s->init = parse_simple(p);
            expect(p, T_SEMI, "';' after the for's start");
        }
    }
    if (!at(p, T_SEMI)) s->cond = parse_expr(p);
    expect(p, T_SEMI, "';' after the for's condition");
    if (!at(p, T_RPAREN)) s->step = parse_simple(p);
    expect(p, T_RPAREN, "')' after the for's step");
    s->then_stmt = parse_stmt(p);
    return s;
}

static stmt *parse_stmt(parser *p)
{
    const token *t = peek(p);
    switch (t->kind) {
    case T_LBRACE:
        return parse_block(p);

    case T_IF: {
        advance(p);
        stmt *s = new_stmt(S_IF, t->at);
        expect(p, T_LPAREN, "'(' after 'if'");
        s->cond = parse_expr(p);
        expect(p, T_RPAREN, "')' after condition");
        s->then_stmt = parse_stmt(p);
        if (accept(p, T_ELSE)) s->else_stmt = parse_stmt(p);
        return s;
    }

    case T_RETURN: {
        advance(p);
        stmt *s = new_stmt(S_RETURN, t->at);
        if (!at(p, T_SEMI)) s->value = parse_expr(p); // Only methods return values; the checker says so
        expect(p, T_SEMI, "';'");
        return s;
    }

    case T_FAIL: { // fail ParseError.Empty;
        advance(p);
        stmt *s = new_stmt(S_FAIL, t->at);
        if (at(p, T_SEMI)) {
            diag_error(t->at, "'fail' needs the error, like 'fail ParseError.Empty;'");
            longjmp(p->fail, 1);
        }
        s->value = parse_expr(p);
        expect(p, T_SEMI, "';'");
        return s;
    }

    case T_MUT:
    case T_VAR:
        return parse_var(p);

    case T_SWITCH:
        return parse_switch(p);

    case T_BREAK:
    case T_CONTINUE: {
        advance(p);
        stmt *s = new_stmt(t->kind == T_BREAK ? S_BREAK : S_CONTINUE, t->at);
        expect(p, T_SEMI, "';'");
        return s;
    }

    case T_WHILE: {
        advance(p);
        stmt *s = new_stmt(S_WHILE, t->at);
        expect(p, T_LPAREN, "'(' after 'while'");
        s->cond = parse_expr(p);
        expect(p, T_RPAREN, "')' after the condition");
        s->then_stmt = parse_stmt(p);
        return s;
    }

    case T_FOR:
        return parse_for(p);

    case T_FOREACH: {
        // foreach (var name in list) or foreach (Type name in list)
        advance(p);
        stmt *s = new_stmt(S_FOREACH, t->at);
        expect(p, T_LPAREN, "'(' after 'foreach'");
        if (!accept(p, T_VAR)) {
            const type_ref *type = parse_type(p, "'var' or the elements' type");
            s->type_name = type->text;
            s->type_at = type->name_at;
            s->type_qual_at = type->at;
            s->type_ref = type;
        }
        const token *name = expect_ident(p, "the variable for each element");
        s->name = name->text;
        s->name_at = name->at;
        if (!at(p, T_IDENT) || !str_eq_c(peek(p)->text, "in")) fail_at(p, peek(p), "'in' and the list");
        advance(p);
        s->value = parse_expr(p);
        expect(p, T_RPAREN, "')' after the list");
        s->then_stmt = parse_stmt(p);
        return s;
    }

    case T_PARALLEL: {
        // parallel (var at in cells), or by blocks: parallel (var at in cells by 2 offset shift)
        advance(p);
        stmt *s = new_stmt(S_PARALLEL, t->at);
        expect(p, T_LPAREN, "'(' after 'parallel'");
        if (!accept(p, T_VAR)) {
            const type_ref *type = parse_type(p, "'var' or the position's type");
            s->type_name = type->text;
            s->type_at = type->name_at;
            s->type_qual_at = type->at;
            s->type_ref = type;
        }
        const token *name = expect_ident(p, "the variable for each step's place");
        s->name = name->text;
        s->name_at = name->at;
        if (!at(p, T_IDENT) || !str_eq_c(peek(p)->text, "in")) fail_at(p, peek(p), "'in' and the grid");
        advance(p);
        s->value = parse_expr(p);
        if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "by")) {
            advance(p);
            s->by = parse_expr(p);
        }
        if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "offset")) {
            advance(p);
            s->offset = parse_expr(p);
        }
        expect(p, T_RPAREN, "')' after the grid");
        s->then_stmt = parse_stmt(p);
        return s;
    }

    default: {
        // `Type name = ...` declares a local: two identifiers in a row, the
        // first maybe qualified, as in `Combat.Stats stats = ...`.
        if (t->kind == T_IDENT && str_eq_c(t->text, "const") && peek_at(p, 1)->kind == T_IDENT
            && peek_at(p, 2)->kind == T_IDENT) {
            diag_error(t->at, "constants go at the top of a file, outside any code");
            diag_note("a local is read-only already: '" STR_FMT " " STR_FMT " = ...;'", STR_ARG(peek_at(p, 1)->text),
                      STR_ARG(peek_at(p, 2)->text));
            longjmp(p->fail, 1);
        }
        if (at_local_decl(p)) return parse_var(p);

        stmt *s = parse_simple(p);
        // A call's block: Foldout("Audio") { ... }, GUILayout.Horizontal() { ... }
        if (s->kind == S_EXPR && at(p, T_LBRACE) && (s->value->kind == E_CALL || s->value->kind == E_METHOD)) {
            s->value->block = parse_block(p);
            return s;
        }
        // Calls listed with commas before one block: Draw.Blend(Blend.Add), Draw.Clip(panel) { ... }. It
        // means what nesting them in that order does: each one's block is the call after it.
        if (s->kind == S_EXPR && at(p, T_COMMA) && (s->value->kind == E_CALL || s->value->kind == E_METHOD)) {
            stmt *last = s;
            while (accept(p, T_COMMA)) {
                const token *start = peek(p);
                stmt *next = parse_simple(p);
                if (next->kind != S_EXPR || (next->value->kind != E_CALL && next->value->kind != E_METHOD)) {
                    diag_error(start->at, "only calls that take a block are listed with commas, before the block they share");
                    diag_note("like 'Draw.Blend(Blend.Add), Draw.Clip(panel) { ... }'; a statement ends with ';'");
                    longjmp(p->fail, 1);
                }
                last->value->block = next;
                last->value->stacked = true;
                last = next;
            }
            last->value->stacked = true;
            if (!at(p, T_LBRACE)) {
                diag_error(peek(p)->at, "calls listed with commas share the block after the last one: expected '{'");
                diag_note("a call that takes no block ends with ';'");
                longjmp(p->fail, 1);
            }
            last->value->block = parse_block(p);
            return s;
        }
        expect(p, T_SEMI, "';'");
        return s;
    }
    }
}

// ---------------------------------------------------------------------------
// Declarations

static decl *new_decl(const decl_kind kind, const token *name)
{
    decl *d = NEW(decl);
    d->kind = kind;
    d->name = name->text;
    d->at = name->at;
    return d;
}

// A declaration's name where it's declared: one word. After `override` it's
// the name of what the declaration replaces, written as any reference to it
// is, so it can be qualified: `Combat.Move`, or a method's `Combat.Stats.Hurt`.
// The declaration is named by the last part, and keeps the whole.
static decl *new_named_decl(parser *p, const decl_kind kind, const char *what)
{
    if (!p->overriding) return new_decl(kind, expect_ident(p, what));
    const qname q = parse_qname(p, what);
    decl *d = NEW(decl);
    d->kind = kind;
    int dot = q.text.len - 1;
    while (dot >= 0 && q.text.ptr[dot] != '.') dot--;
    d->name = (str){q.text.ptr + dot + 1, q.text.len - dot - 1};
    d->at = q.name_at;
    d->override_name = q.text;
    d->override_qual_at = q.at;
    return d;
}

// Sample() { ... } inside an input declaration, maybe taking local singletons.
// The input's own name instead of Sample is the constructor it used to be.
static void parse_sample(parser *p, decl *d, const token *name)
{
    const bool spelled_sample = str_eq_c(name->text, "Sample") || str_eq_c(name->text, "sample");
    if (d->kind != DECL_INPUT) {
        if (spelled_sample) diag_error(name->at, "only inputs have Sample");
        else diag_error(name->at, "only inputs have methods; give fields defaults instead of a constructor");
        longjmp(p->fail, 1);
    }
    if (d->body) {
        diag_error(name->at, "an input has one Sample");
        longjmp(p->fail, 1);
    }
    if (str_eq_c(name->text, "sample")) {
        diag_error(name->at, "methods use PascalCase: 'Sample()'");
    } else if (!spelled_sample) {
        diag_error(name->at, "inputs read the devices in a method: 'Sample()'");
    }
    d->body_at = name->at;
    expect(p, T_LPAREN, "'('");
    if (!at(p, T_RPAREN)) {
        do {
            param prm = {0};
            prm.at = peek(p)->at;
            prm.type_at = peek(p)->at;
            prm.type_name = expect_ident(p, "parameter type")->text;
            prm.name_at = peek(p)->at;
            prm.name = expect_ident(p, "parameter name")->text;
            vec_push(d->params, prm);
        } while (accept(p, T_COMMA));
    }
    expect(p, T_RPAREN, "')' after parameters");
    d->body = parse_block(p);
}

// Sanitize() { ... } inside an input declaration.
static void parse_sanitize(parser *p, decl *d, const token *name)
{
    if (d->kind != DECL_INPUT) {
        diag_error(name->at, "only inputs have Sanitize");
        longjmp(p->fail, 1);
    }
    if (d->sanitize) {
        diag_error(name->at, "an input has one Sanitize");
        longjmp(p->fail, 1);
    }
    if (name->text.ptr[0] == 's') diag_error(name->at, "methods use PascalCase: 'Sanitize()'");
    d->sanitize_at = name->at;
    expect(p, T_LPAREN, "'('");
    expect(p, T_RPAREN, "')': Sanitize takes no parameters, it works on the input's fields");
    d->sanitize = parse_block(p);
}

// [Clamp(-1, 1)] before a field. Unlike a declaration's attributes, the
// arguments are values.
static void parse_field_attributes(parser *p)
{
    expect(p, T_LBRACKET, "'['");
    do {
        attribute a = {0};
        const token *name = expect_ident(p, "attribute name");
        a.name = name->text;
        a.at = name->at;
        if (accept(p, T_LPAREN)) {
            if (!at(p, T_RPAREN)) {
                do {
                    vec_push(a.values, parse_expr(p));
                } while (accept(p, T_COMMA));
            }
            expect(p, T_RPAREN, "')' after the attribute's arguments");
        }
        vec_push(p->field_pending, a);
    } while (accept(p, T_COMMA));
    expect(p, T_RBRACKET, "']' after attributes");
}

// `local` before a field: the word, then the field's type and its name.
// Anywhere else it's a name like any other, a type's too.
static bool at_local_field(const parser *p)
{
    if (!at(p, T_IDENT) || !str_eq_c(peek(p)->text, "local") || peek_at(p, 1)->kind != T_IDENT) return false;
    const tok_kind after = peek_at(p, 2)->kind;
    return after == T_IDENT || after == T_DOT || after == T_LT || after == T_QUESTION;
}

// [local] Type name; [= default];
static void parse_field(parser *p, decl *d)
{
    // `local List<Vertex3> verts;`: this machine's, outside the match
    const token *local = at_local_field(p) ? advance(p) : NULL;
    const type_ref *type = parse_decl_type(p, "field type or '}'");
    const token *field_name = expect_ident(p, "field name");
    field f = {0};
    if (local) {
        f.is_local = true;
        f.local_at = local->at;
    }
    f.name = field_name->text;
    f.at = field_name->at;
    f.type_name = type->text;
    f.type_at = type->name_at;
    f.type_qual_at = type->at;
    f.type_ref = type;
    f.attributes.items = p->field_pending.items;
    f.attributes.count = p->field_pending.count;
    f.attributes.cap = p->field_pending.cap;
    p->field_pending.items = NULL;
    p->field_pending.count = p->field_pending.cap = 0;
    if (accept(p, T_ASSIGN)) f.default_value = parse_expr(p);
    expect(p, T_SEMI, "';' after field");
    vec_push(d->fields, f);
}

// (Type name, ...): a method's, function's or operator's parameters.
static void parse_routine_params(parser *p, decl *m)
{
    expect(p, T_LPAREN, "'('");
    if (!at(p, T_RPAREN)) {
        do {
            param prm = {0};
            prm.at = peek(p)->at;
            const token *mut = at(p, T_MUT) ? advance(p) : NULL;
            prm.mode = mut ? PARAM_MUT : PARAM_READ;
            // `in Stats stats`: `in` before a type and a name (the checker says it's only for extern functions)
            const tok_kind after = peek_at(p, 2)->kind;
            if (prm.mode == PARAM_READ && at(p, T_IDENT) && str_eq_c(peek(p)->text, "in") && peek_at(p, 1)->kind == T_IDENT
                && (after == T_IDENT || after == T_DOT || after == T_LT)) {
                advance(p);
                prm.mode = PARAM_IN;
            }
            // `out Health health`: `out` before a type and a name, as in C#. The call makes the caller's variable,
            // so it's a `mut` parameter to everything but what checks that the code assigns it
            if (prm.mode != PARAM_IN && at(p, T_IDENT) && str_eq_c(peek(p)->text, "out") && peek_at(p, 1)->kind == T_IDENT
                && (after == T_IDENT || after == T_DOT || after == T_LT || after == T_QUESTION)) {
                if (mut) {
                    diag_error(mut->at, "'out' says the call makes the variable, which 'mut' would only change: drop 'mut'");
                    diag_note("like 'bool TryFind(Entity target, out Health health)'");
                    const token *word = peek(p);
                    if (word->at.line == mut->at.line) { // What editors offer: `mut out` as `out`
                        suggest_replacement((str){mut->text.ptr, (int)(word->text.ptr + word->text.len - mut->text.ptr)}, "out");
                    }
                    longjmp(p->fail, 1);
                }
                prm.out_at = advance(p)->at;
                prm.is_out = true;
                prm.mode = PARAM_MUT;
            }
            // `verified Health health` is a view's parameter: a function takes the value it's passed
            if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "verified") && peek_at(p, 1)->kind == T_IDENT
                && (after == T_IDENT || after == T_DOT || after == T_LT)) {
                diag_error(peek(p)->at, "'verified' goes on a view's parameter, or on a local handler's event: a function takes "
                                        "the value it's passed");
                diag_note("the view takes it 'verified', and passes it on: 'view Name(verified Health health) { Show(health); }'");
                longjmp(p->fail, 1);
            }
            prm.function_param = true;
            const type_ref *type = parse_decl_type(p, "parameter type");
            prm.type_name = type->text;
            prm.type_qual_at = type->at;
            prm.type_ref = type;
            prm.type_at = type->name_at;
            prm.name_at = peek(p)->at;
            prm.name = expect_ident(p, "parameter name")->text;
            if (accept(p, T_ASSIGN)) { // float multiplier = 1
                const token *first = peek(p);
                prm.default_value = parse_expr(p);
                const token *last = &p->toks[p->pos - 1];
                // As written, with the quotes a text token leaves out
                const char *start = first->text.ptr - (first->kind == T_STRING ? 1 : 0);
                const char *end = last->text.ptr + last->text.len + (last->kind == T_STRING ? 1 : 0);
                prm.default_text = (str){start, (int)(end - start)};
            }
            vec_push(m->params, prm);
        } while (accept(p, T_COMMA));
    }
    expect(p, T_RPAREN, "')' after parameters");
    // `fails ParseError`: the error it can end with. Only a keyword here.
    if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "fails")) {
        m->fails_at = advance(p)->at;
        const type_ref *error = parse_type(p, "the error it fails with, like 'fails ParseError'");
        m->fails_name = error->text;
        m->fails_type_at = error->name_at;
        m->fails_type_qual_at = error->at;
        m->fails_ref = error;
    }
}

// (Type name, ...) { ... }: the rest of a method, function or operator.
static void parse_routine_rest(parser *p, decl *m, const token *name)
{
    parse_routine_params(p, m);
    m->body_at = name->at;
    m->body = parse_block(p);
    m->end = m->body->end;
}

// extern float Noise(float x, float y);: a function written in C, which the
// game's C files or libraries define.
static decl *parse_extern(parser *p)
{
    // `extern view Name(...)` and `extern event(Hit hit) Name(...)`: C can be a system, and nothing else the engine runs
    const bool view = str_eq_c(peek(p)->text, "view") && peek_at(p, 1)->kind == T_IDENT && peek_at(p, 2)->kind == T_LPAREN;
    const bool handler = (str_eq_c(peek(p)->text, "event") || str_eq_c(peek(p)->text, "command")) && peek_at(p, 1)->kind == T_LPAREN;
    if (view || handler) {
        diag_error(peek(p)->at, "%s can't be written in C yet: only a system can, 'extern system Name(...);'",
                   view ? "a view" : "an event handler");
        diag_note("write it in Tide, and call C from it: 'extern' functions take what it reads");
        longjmp(p->fail, 1);
    }
    const type_ref *ret = parse_decl_type(p, "return type");
    const token *name = expect_ident(p, "function name");
    decl *m = new_decl(DECL_FUNCTION, name);
    m->unit = p->unit;
    m->is_extern = true;
    m->return_type_name = ret->text;
    m->return_type_at = ret->name_at;
    m->return_type_qual_at = ret->at;
    m->return_type_ref = ret;
    parse_routine_params(p, m);
    m->body_at = name->at;
    if (at(p, T_LBRACE)) {
        diag_error(peek(p)->at, "an extern function has no body: its code is in C");
        diag_note("end it with ';', like 'extern float Noise(float x);', or remove 'extern' to write it in Tide");
        longjmp(p->fail, 1);
    }
    m->end = expect(p, T_SEMI, "';' after the extern function: its code is in C")->at;
    return m;
}

// const int MAX_HEALTH = 100;: a constant, at the top of a file.
static decl *parse_const(parser *p)
{
    if (peek(p)->kind == T_IDENT && peek_at(p, 1)->kind == T_ASSIGN) {
        diag_error(peek(p)->at, "a constant's type is written out: 'const int " STR_FMT " = ...;'", STR_ARG(peek(p)->text));
        longjmp(p->fail, 1);
    }
    const type_ref *type = parse_type(p, "the constant's type");
    const token *name = expect_ident(p, "constant name");
    decl *d = new_decl(DECL_CONST, name);
    d->unit = p->unit;
    d->return_type_name = type->text;
    d->return_type_at = type->name_at;
    d->return_type_qual_at = type->at;
    d->return_type_ref = type;
    if (!at(p, T_ASSIGN)) {
        diag_error(peek(p)->at, "a constant needs a value: 'const " STR_FMT " " STR_FMT " = ...;'", STR_ARG(type->text),
                   STR_ARG(name->text));
        longjmp(p->fail, 1);
    }
    advance(p);
    d->value = parse_expr(p);
    d->end = expect(p, T_SEMI, "';' after the constant")->at;
    return d;
}

// asset Grid2<Color> hero = "art/hero.png";: a file's contents under a name,
// at the top of a file (see docs/spec.md, Assets). Its value is its path, a
// constant text, which the checker resolves.
static decl *parse_asset(parser *p)
{
    if (peek(p)->kind == T_IDENT && peek_at(p, 1)->kind == T_ASSIGN) {
        diag_error(peek(p)->at, "an asset's type is written out: 'asset Grid2<Color> " STR_FMT " = \"art/" STR_FMT ".png\";'",
                   STR_ARG(peek(p)->text), STR_ARG(peek(p)->text));
        diag_note("it's what the file's contents are read as: string, List<byte> or Grid2<Color>, or a List or Map<string, ...> of one for a glob");
        longjmp(p->fail, 1);
    }
    const type_ref *type = parse_type(p, "the asset's type");
    const token *name = expect_ident(p, "asset name");
    decl *d = new_decl(DECL_ASSET, name);
    d->unit = p->unit;
    d->return_type_name = type->text;
    d->return_type_at = type->name_at;
    d->return_type_qual_at = type->at;
    d->return_type_ref = type;
    if (!at(p, T_ASSIGN)) {
        diag_error(peek(p)->at, "an asset needs its file's path: 'asset " STR_FMT " " STR_FMT " = \"...\";', from the folder this file is in",
                   STR_ARG(type->text), STR_ARG(name->text));
        longjmp(p->fail, 1);
    }
    advance(p);
    d->value = parse_expr(p);
    d->end = expect(p, T_SEMI, "';' after the asset's path")->at;
    return d;
}

// name = value;: one of the settings in a settings block, which the checker
// knows the types of.
static void parse_setting(parser *p, decl *d)
{
    int past = 1; // `Type name =`: past a qualified type's parts
    while (peek_at(p, past)->kind == T_DOT && peek_at(p, past + 1)->kind == T_IDENT) past += 2;
    if (at(p, T_IDENT) && peek_at(p, past)->kind == T_IDENT) {
        diag_error(peek(p)->at, "settings are set without a type: '" STR_FMT " = ...;'", STR_ARG(peek_at(p, past)->text));
        diag_note("they're the engine's; a game's own values are constants, at the top of a file: 'const " STR_FMT
                  " " STR_FMT " = ...;'", STR_ARG(peek(p)->text), STR_ARG(peek_at(p, past)->text));
        longjmp(p->fail, 1);
    }
    const token *name = expect_ident(p, "a setting's name, like 'tickRate', or '}'");
    field f = {0};
    f.name = name->text;
    f.at = name->at;
    expect(p, T_ASSIGN, "'=' and its value, like 'tickRate = 30;'");
    f.default_value = parse_expr(p);
    expect(p, T_SEMI, "';' after the setting");
    vec_push(d->fields, f);
}

// settings { tickRate = 30; }: the engine's settings for the game.
static decl *parse_settings(parser *p, const token *keyword)
{
    decl *d = new_decl(DECL_SETTINGS, keyword);
    d->unit = p->unit;
    expect(p, T_LBRACE, "'{'");
    while (!at(p, T_RBRACE)) {
        if (p->recover && (at(p, T_EOF) || at_decl_start(p))) {
            diag_error(peek(p)->at, "expected '}' to close 'settings'");
            d->end = peek(p)->at;
            return d;
        }
        if (p->recover) RECOVERING(p, parse_setting(p, d));
        else parse_setting(p, d);
    }
    d->end = peek(p)->at;
    advance(p);
    return d;
}

// [mut] ReturnType Name(Type name, ...) { ... }: a method of `owner`, or with
// no owner, a function. The checker says where methods are allowed.
static decl *parse_method(parser *p, decl *owner)
{
    const bool is_mut = accept(p, T_MUT);
    const type_ref *ret = parse_decl_type(p, "return type");
    decl *m = new_named_decl(p, owner ? DECL_METHOD : DECL_FUNCTION, owner ? "method name" : "function name");
    m->unit = p->unit;
    m->owner = owner;
    m->is_mut_method = is_mut;
    m->return_type_name = ret->text;
    m->return_type_at = ret->name_at;
    m->return_type_qual_at = ret->at;
    m->return_type_ref = ret;
    parse_routine_params(p, m);
    m->body_at = m->at;
    m->body = parse_block(p);
    m->end = m->body->end;
    return m;
}

// Type operator: an operator of a struct, rather than a field or method.
static bool at_operator(const parser *p)
{
    if (peek(p)->kind != T_IDENT) return false;
    int i = 1;
    while (peek_at(p, i)->kind == T_DOT && peek_at(p, i + 1)->kind == T_IDENT) i += 2;
    return peek_at(p, i)->kind == T_IDENT && str_eq_c(peek_at(p, i)->text, "operator");
}

// The operators a struct can declare, as in C#.
static bool is_overloadable(const tok_kind kind)
{
    switch (kind) {
    case T_PLUS: case T_MINUS: case T_STAR: case T_SLASH: case T_PERCENT: case T_AMP: case T_PIPE: case T_CARET:
    case T_SHL: case T_SHR: case T_EQ: case T_NE: case T_LT: case T_LE: case T_GT: case T_GE: case T_NOT: case T_TILDE:
        return true;
    default:
        return false;
    }
}

// ReturnType operator +(Type a, Type b) { ... } in a struct.
static decl *parse_operator(parser *p, decl *owner)
{
    const type_ref *ret = parse_decl_type(p, "return type");
    const token *keyword = advance(p);
    const token *op = advance(p);
    if (!is_overloadable(op->kind)) {
        diag_error(op->at, "expected an operator a struct can declare after 'operator'");
        diag_note("these can: + - * / %% & | ^ << >> == != < <= > >= ! ~");
        longjmp(p->fail, 1);
    }
    decl *m = new_decl(DECL_METHOD, keyword);
    sb name = {0};
    sb_printf(&name, "operator " STR_FMT, STR_ARG(op->text));
    m->name = (str){name.data, (int)name.len};
    m->unit = p->unit;
    m->owner = owner;
    m->is_operator = true;
    m->op = op->kind;
    m->return_type_name = ret->text;
    m->return_type_at = ret->name_at;
    m->return_type_qual_at = ret->at;
    m->return_type_ref = ret;
    parse_routine_rest(p, m, keyword);
    return m;
}

// component Name { Type field; ... }, and the same for singletons, inputs and structs.
static decl *parse_data_decl(parser *p, const decl_kind kind)
{
    const char *what = kind == DECL_COMPONENT   ? "component or scene name"
                       : kind == DECL_SINGLETON ? "singleton name"
                       : kind == DECL_STRUCT    ? "struct name"
                       : kind == DECL_EVENT     ? "event name"
                                                : "input name";
    const token *name = expect_ident(p, what);
    decl *d = new_decl(kind, name);
    expect(p, T_LBRACE, "'{'");
    while (!at(p, T_RBRACE)) {
        if (p->recover && (at(p, T_EOF) || at_decl_start_or_function(p, false))) {
            diag_error(peek(p)->at, "expected '}' to close '" STR_FMT "'", STR_ARG(name->text));
            d->end = peek(p)->at;
            return d;
        }
        if (at(p, T_LBRACKET)) {
            if (p->recover) RECOVERING(p, parse_field_attributes(p));
            else parse_field_attributes(p);
            continue;
        }
        if (p->field_pending.count > 0 && ((at(p, T_IDENT) && peek_at(p, 1)->kind == T_LPAREN) || at_method(p))) {
            diag_error(p->field_pending.items[0].at, "field attributes go right before a field");
            p->field_pending.count = 0;
        }
        // Sample(...), or a constructor: the type's own name followed by '('.
        if (at(p, T_IDENT) && peek_at(p, 1)->kind == T_LPAREN
            && (str_eq_c(peek(p)->text, "Sample") || str_eq_c(peek(p)->text, "sample")
                || str_eq(peek(p)->text, name->text))) {
            parse_sample(p, d, advance(p));
            continue;
        }
        if (at(p, T_IDENT) && peek_at(p, 1)->kind == T_LPAREN
            && (str_eq_c(peek(p)->text, "Sanitize") || str_eq_c(peek(p)->text, "sanitize"))) {
            parse_sanitize(p, d, advance(p));
            continue;
        }
        if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "extern") && peek_at(p, 1)->kind == T_IDENT) {
            diag_error(peek(p)->at, "extern functions go at the top of a file, outside '" STR_FMT "'", STR_ARG(name->text));
            if (!p->recover) longjmp(p->fail, 1);
            skip_statement(p);
            continue;
        }
        if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "const") && peek_at(p, 1)->kind == T_IDENT) {
            diag_error(peek(p)->at, "constants go at the top of a file, outside '" STR_FMT "'", STR_ARG(name->text));
            diag_note("for a field every value starts with, give it a default: 'int lives = 3;'");
            if (!p->recover) longjmp(p->fail, 1);
            skip_statement(p);
            continue;
        }
        if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "async") && (peek_at(p, 1)->kind == T_IDENT || peek_at(p, 1)->kind == T_MUT)) {
            diag_error(peek(p)->at, "methods can't be async yet; functions can");
            diag_note("write it as a function outside '" STR_FMT "' that takes the value: 'async void Name(...) { ... }'",
                      STR_ARG(name->text));
            if (!p->recover) longjmp(p->fail, 1);
            advance(p);
            continue;
        }
        // `virtual bool IsDead() { ... }`: a method another package can replace. Its override is written
        // outside the type, in that package: `override bool Stats.IsDead() { ... }`
        const token *virtual = NULL;
        if (at_replace_word(p, true)) {
            const token *word = advance(p);
            if (at_operator(p)) {
                refuse_replace_word(word, "operators aren't replaced: an operator can call a virtual method, which can be");
            } else if (str_eq_c(word->text, "override")) {
                diag_error(word->at, "an override is written outside the type, in the package that replaces the method");
                diag_note("after the type's name, like 'override bool " STR_FMT ".IsDead() { ... }', where the type's fields "
                          "are in scope as they are here", STR_ARG(name->text));
                suggest_replacement(word_and_space(word), "");
            } else {
                virtual = word;
            }
        } else if (at(p, T_IDENT) && is_replace_word(peek(p)->text) && peek_at(p, 1)->kind == T_IDENT
                   && peek_at(p, 2)->kind == T_LPAREN
                   && (str_eq_c(peek_at(p, 1)->text, "Sample") || str_eq_c(peek_at(p, 1)->text, "Sanitize"))) {
            refuse_replace_word(advance(p), "an input's Sample and Sanitize are the game's own, as its input is");
            continue;
        }
        if (at_operator(p)) {
            if (p->recover) RECOVERING(p, vec_push(d->methods, parse_operator(p, d)));
            else vec_push(d->methods, parse_operator(p, d));
            continue;
        }
        if (at_method(p)) {
            const int before = d->methods.count;
            if (p->recover) RECOVERING(p, vec_push(d->methods, parse_method(p, d)));
            else vec_push(d->methods, parse_method(p, d));
            if (virtual && d->methods.count > before) {
                d->methods.items[before]->is_virtual = true;
                d->methods.items[before]->virtual_at = virtual->at;
            }
            continue;
        }
        if (p->recover) RECOVERING(p, parse_field(p, d));
        else parse_field(p, d);
    }
    d->end = peek(p)->at;
    advance(p);
    return d;
}

// (Time time, mut Transform trs, with Player, without Dead) { ... }: the rest
// of a system, view or event handler.
static void parse_query_params(parser *p, decl *d)
{
    expect(p, T_LPAREN, "'('");
    if (!at(p, T_RPAREN)) {
        do {
            param prm = {0};
            prm.at = peek(p)->at;
            const char *what = "parameter type";
            if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "query") && peek_at(p, 1)->kind == T_LPAREN) {
                advance(p);
                prm.mode = PARAM_QUERY;
                decl nested = {0};
                parse_query_params(p, &nested);
                prm.query.items = nested.params.items;
                prm.query.count = nested.params.count;
                prm.query.cap = nested.params.cap;
                prm.name_at = peek(p)->at;
                prm.name = expect_ident(p, "query parameter name")->text;
                vec_push(d->params, prm);
                continue;
            }
            // `chunk mut Field.cells cells` was a chunk system's grid
            const token *first = peek(p);
            if (first->kind == T_IDENT && str_eq_c(first->text, "chunk")
                && (peek_at(p, 1)->kind == T_MUT || (peek_at(p, 1)->kind == T_IDENT && peek_at(p, 2)->kind != T_COMMA
                                                     && peek_at(p, 2)->kind != T_RPAREN))) {
                diag_error(first->at, "chunk systems are gone: a system takes the grid's component or singleton, and a "
                                      "parallel loop goes through its cells at once");
                diag_note("like 'system Fall(mut Field field) { parallel (var at in field.cells) { ... } }'");
                longjmp(p->fail, 1);
            }
            if (accept(p, T_WITH)) {
                prm.mode = PARAM_WITH;
                what = "component name after 'with'";
            } else if (accept(p, T_WITHOUT)) {
                prm.mode = PARAM_WITHOUT;
                what = "component name after 'without'";
            } else {
                const token *mut = at(p, T_MUT) ? advance(p) : NULL;
                prm.mode = mut ? PARAM_MUT : PARAM_READ;
                if (mut) prm.mode_word = mut->text;
                // `lookup Health health` and `mut lookup` were any entity's component, by its handle
                if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "lookup") && peek_at(p, 1)->kind == T_IDENT
                    && (peek_at(p, 2)->kind == T_IDENT || peek_at(p, 2)->kind == T_DOT)) {
                    const token *keyword = advance(p);
                    const qname component = parse_qname(p, "component name");
                    const str name = at(p, T_IDENT) ? peek(p)->text : str_from("health");
                    diag_error(keyword->at, "'lookup' parameters are gone: code takes the World, and reads any entity's "
                                            "components through it");
                    if (prm.mode == PARAM_MUT) {
                        diag_note("write 'mut World world' instead, 'world.Get<" STR_FMT ">(entity)' where the code has '"
                                  STR_FMT "[entity]', and 'world.Set<" STR_FMT ">(entity, value)' where it has '" STR_FMT
                                  "[entity] = value'", STR_ARG(component.text), STR_ARG(name), STR_ARG(component.text),
                                  STR_ARG(name));
                    } else {
                        diag_note("write 'World world' instead, 'world.Get<" STR_FMT ">(entity)' where the code has '"
                                  STR_FMT "[entity]', and 'world.Has<" STR_FMT ">(entity)' where it has '" STR_FMT
                                  ".Has(entity)'", STR_ARG(component.text), STR_ARG(name), STR_ARG(component.text),
                                  STR_ARG(name));
                    }
                    longjmp(p->fail, 1);
                }
                const bool out = at(p, T_IDENT) && str_eq_c(peek(p)->text, "out") && peek_at(p, 1)->kind == T_IDENT
                              && (peek_at(p, 2)->kind == T_IDENT || peek_at(p, 2)->kind == T_DOT || peek_at(p, 2)->kind == T_LT
                                  || peek_at(p, 2)->kind == T_QUESTION);
                // `out Color color`, `out FromLamp lamp`: what a frag or a vert writes, which the GPU takes
                if (out && d->kind == DECL_SHADER) {
                    if (mut) {
                        diag_error(mut->at, "'out' says the %s writes it, which 'mut' would only change: drop 'mut'",
                                   d->is_frag ? "frag" : "vert");
                        diag_note(d->is_frag ? "like 'frag Shade(BoxVertex vertex, out Color color)'"
                                             : "like 'vert Place(mut BoxVertex vertex, out FromLamp lamp)'");
                        const token *word = peek(p);
                        if (word->at.line == mut->at.line) { // What editors offer: `mut out` as `out`
                            suggest_replacement((str){mut->text.ptr, (int)(word->text.ptr + word->text.len - mut->text.ptr)}, "out");
                        }
                        longjmp(p->fail, 1);
                    }
                    prm.mode_word = peek(p)->text;
                    prm.out_at = advance(p)->at;
                    prm.is_out = true;
                    prm.mode = PARAM_MUT;
                } else if (out) { // An `out` parameter is for the code that calls, and the engine calls these
                    diag_error(peek(p)->at, "the engine calls this itself, so nothing would take what an 'out' parameter "
                                            "makes: functions and methods have them");
                    if (peek_at(p, 2)->kind == T_IDENT) {
                        diag_note("this takes what it changes as 'mut': 'mut " STR_FMT " " STR_FMT "'", STR_ARG(peek_at(p, 1)->text),
                                  STR_ARG(peek_at(p, 2)->text));
                    } else {
                        diag_note("this takes what it changes as 'mut', like 'mut Health health'");
                    }
                    if (prm.mode == PARAM_READ) suggest_replacement(peek(p)->text, "mut"); // What editors offer
                    longjmp(p->fail, 1);
                }
            }
            const bool filter = prm.mode == PARAM_WITH || prm.mode == PARAM_WITHOUT;
            // `verified Health health`, `with verified Dead`: a word of its own
            // before a parameter's type, and a name like any other everywhere
            // else, a type's too (`verified score`, `with verified`)
            if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "verified") && peek_at(p, 1)->kind == T_IDENT
                && (filter || peek_at(p, 2)->kind == T_IDENT || peek_at(p, 2)->kind == T_DOT || peek_at(p, 2)->kind == T_LT)) {
                prm.verified = true;
                prm.verified_at = advance(p)->at;
            }
            // `with ownership`, `without ownership`: a word of its own right after
            // them, and a name like any other everywhere else, a namespace's too
            // (`with ownership.Flag`)
            if (filter && at(p, T_IDENT) && str_eq_c(peek(p)->text, "ownership") && peek_at(p, 1)->kind != T_DOT) {
                const token *word = advance(p);
                prm.ownership = true;
                prm.type_name = word->text;
                prm.type_qual_at = word->at;
                prm.type_at = word->at;
                vec_push(d->params, prm);
                continue;
            }
            const type_ref *type = parse_decl_type(p, what);
            prm.type_name = type->text;
            prm.type_qual_at = type->at;
            prm.type_ref = type;
            prm.type_at = type->name_at;
            if (!filter) {
                prm.name_at = peek(p)->at;
                prm.name = expect_ident(p, "parameter name")->text;
            }
            if (at(p, T_ASSIGN)) { // Nothing calls it, so nothing leaves a parameter out
                diag_error(peek(p)->at, "the engine supplies these parameters, so they take no default");
                diag_note("a function's or method's parameter can have one: 'float Damage(float base, float multiplier = 1)'");
                longjmp(p->fail, 1);
            }
            vec_push(d->params, prm);
        } while (accept(p, T_COMMA));
    }
    expect(p, T_RPAREN, "')' after parameters");
}

static void parse_query_rest(parser *p, decl *d)
{
    parse_query_params(p, d);
    d->body = parse_block(p);
    d->end = d->body->end;
}

// enum Page { Title, Options = 3, Credits }, with an optional ',' after the last.
static decl *parse_enum(parser *p)
{
    const token *name = expect_ident(p, "enum name");
    decl *d = new_decl(DECL_ENUM, name);
    if (accept(p, T_COLON)) { // enum Voxel : byte, as in C#
        const token *backing = expect_ident(p, "what the enum is stored as: 'byte', 'ushort' or 'int'");
        d->backing = backing->text;
        d->backing_at = backing->at;
    }
    expect(p, T_LBRACE, "'{'");
    while (!at(p, T_RBRACE)) {
        const token *member = expect_ident(p, "a member name or '}'");
        enum_member m = {member->text, member->at, NULL, 0};
        if (accept(p, T_ASSIGN)) m.value = parse_expr(p);
        vec_push(d->members, m);
        if (!accept(p, T_COMMA)) break;
    }
    d->end = peek(p)->at;
    expect(p, T_RBRACE, "',' or '}' after the member");
    return d;
}

// system Name(Time time, mut Transform trs, with Player, without Dead) { ... }
// Views have the same shape: view Name(Transform trs, with Player) { ... }
static decl *parse_system(parser *p, const bool is_view)
{
    decl *d = new_named_decl(p, DECL_SYSTEM, is_view ? "view name" : "system name");
    d->is_view = is_view;
    parse_query_rest(p, d);
    return d;
}

// extern system Move(mut Position position, Velocity velocity, Time time);: a
// system written in C, which the game's C files or libraries define. It's
// declared as a system is, with no body: C gets its rows' components as arrays
// (tide/rows.h). The `system` keyword is next.
static decl *parse_extern_system(parser *p)
{
    advance(p);
    const token *name = expect_ident(p, "system name");
    decl *d = new_decl(DECL_SYSTEM, name);
    d->extern_system = true;
    parse_query_params(p, d);
    if (at(p, T_LBRACE)) {
        diag_error(peek(p)->at, "an extern system has no body: its code is in C");
        diag_note("end it with ';', like 'extern system Move(mut Position position, Time time);', or remove 'extern' to write "
                  "it in Tide");
        longjmp(p->fail, 1);
    }
    d->end = expect(p, T_SEMI, "';' after the extern system: its code is in C")->at;
    return d;
}

// frag Shade(BoxVertex vertex, out Color color, Sun sun) { ... } and
// vert Wave(mut BoxVertex vertex, Time time) { ... }: code the GPU runs.
static decl *parse_shader(parser *p, const bool is_frag)
{
    decl *d = new_named_decl(p, DECL_SHADER, is_frag ? "frag name" : "vert name");
    d->is_frag = is_frag;
    d->return_type_name = str_from("void");
    parse_query_rest(p, d);
    return d;
}

// audio Lowpass(mut Samples samples, mut State state, Knobs knobs) { ... }: a
// function the audio thread runs once a block, on a voice's or a bus's samples.
static decl *parse_audio(parser *p)
{
    decl *m = new_named_decl(p, DECL_FUNCTION, "audio name");
    m->unit = p->unit;
    m->is_audio = true;
    m->audio_state = m->audio_knobs = -1;
    m->return_type_name = str_from("void");
    parse_routine_params(p, m);
    m->body_at = m->at;
    m->body = parse_block(p);
    m->end = m->body->end;
    return m;
}

// event(Hit hit) TakeHit(mut Health health) { ... }: runs when a Hit is sent.
// The trigger is the first parameter; an event without fields needs no name.
static decl *parse_handler(parser *p, const bool command)
{
    expect(p, T_LPAREN, "'(' and the event it handles");
    param trigger = {0};
    trigger.mode = PARAM_EVENT;
    trigger.at = peek(p)->at;
    // `event(verified Died died)`: the word before the event, where an event follows it
    if (!command && at(p, T_IDENT) && str_eq_c(peek(p)->text, "verified") && peek_at(p, 1)->kind == T_IDENT) {
        trigger.verified = true;
        trigger.verified_at = advance(p)->at;
    }
    const qname type = parse_qname(p, "the event it handles, like 'event(Hit hit)'");
    trigger.type_name = type.text;
    trigger.type_qual_at = type.at;
    trigger.type_at = type.name_at;
    if (at(p, T_IDENT)) {
        trigger.name_at = peek(p)->at;
        trigger.name = advance(p)->text;
    }
    param sender = {0};
    if (command) {
        expect(p, T_COMMA, "',' and the command sender: 'PlayerID sender'");
        sender.mode = PARAM_SENDER;
        sender.at = peek(p)->at;
        const qname sender_type = parse_qname(p, "PlayerID for the command sender");
        sender.type_name = sender_type.text;
        sender.type_qual_at = sender_type.at;
        sender.type_at = sender_type.name_at;
        const token *sender_name = expect_ident(p, "command sender name");
        sender.name = sender_name->text;
        sender.name_at = sender_name->at;
    }
    expect(p, T_RPAREN, "')' after the trigger");
    decl *d = new_named_decl(p, DECL_SYSTEM, "handler name, like 'event(Hit hit) TakeHit(...)'");
    d->is_handler = true;
    d->is_command = command;
    vec_push(d->params, trigger);
    if (command) vec_push(d->params, sender);
    parse_query_rest(p, d);
    return d;
}

// After a syntax error outside any body: skips to the next declaration. A
// failed declaration always consumed its first token, so this makes progress.
// Inside a namespace block it stops at the block's '}' too, which is the one
// under the block's `namespace` word, as the formatter puts it: the braces of
// a broken declaration don't pair, so counting them tells nothing.
static void skip_declaration(parser *p)
{
    while (!at(p, T_EOF) && !at_decl_start(p)) {
        if (p->unit->outer && at(p, T_RBRACE) && peek(p)->at.col == p->unit->block_at.col) break;
        advance(p);
    }
}

// (Position, Velocity, with Player) after a name in [Before] and [After]: the
// parameters of the declaration it means, among those that share the name.
// Types only, as C#'s `cref` writes them: no names, and no `mut`.
static void parse_signature(parser *p, sig_items *out)
{
    expect(p, T_LPAREN, "'('");
    if (!at(p, T_RPAREN)) {
        do {
            sig_item item = {0};
            item.at = peek(p)->at;
            item.mode = PARAM_READ;
            if (at(p, T_MUT)) {
                diag_error(peek(p)->at, "a declaration is named by its parameters' types: leave out 'mut'");
                longjmp(p->fail, 1);
            }
            if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "query") && peek_at(p, 1)->kind == T_LPAREN) {
                advance(p);
                item.mode = PARAM_QUERY;
                sig_items nested = {0};
                parse_signature(p, &nested);
                item.query.items = nested.items;
                item.query.count = nested.count;
                item.query.cap = nested.cap;
                vec_push(*out, item);
                continue;
            }
            if (accept(p, T_WITH)) item.mode = PARAM_WITH;
            else if (accept(p, T_WITHOUT)) item.mode = PARAM_WITHOUT;
            // `verified Health`: the word, then the type. No names are written here, so a name after it is one.
            if (at(p, T_IDENT) && str_eq_c(peek(p)->text, "verified") && peek_at(p, 1)->kind == T_IDENT) {
                advance(p);
                item.verified = true;
            }
            const type_ref *type = parse_decl_type(p, "a parameter's type, like 'Move(Position, with Player)'");
            item.type_name = type->text;
            item.type_at = type->name_at;
            item.type_qual_at = type->at;
            item.type_ref = type;
            if (at(p, T_IDENT)) {
                diag_error(peek(p)->at, "a declaration is named by its parameters' types: leave out their names");
                diag_note("write '" STR_FMT "' alone", STR_ARG(type->text));
                longjmp(p->fail, 1);
            }
            vec_push(*out, item);
        } while (accept(p, T_COMMA));
    }
    expect(p, T_RPAREN, "')' after the parameters' types");
}

// [Before(A.B), After(C)], one or more groups before a declaration.
static void parse_attributes(parser *p)
{
    expect(p, T_LBRACKET, "'['");
    do {
        attribute a = {0};
        const token *name = expect_ident(p, "attribute name");
        a.name = name->text;
        a.at = name->at;
        if (accept(p, T_LPAREN)) {
            if (!at(p, T_RPAREN)) {
                if (str_eq_c(a.name, "Stage")) {
                    vec_push(a.args, parse_qname(p, "an enum member, like 'RenderStages.Objects'"));
                    while (accept(p, T_COMMA)) vec_push(a.values, parse_expr(p));
                } else {
                    const bool orders = str_eq_c(a.name, "Before") || str_eq_c(a.name, "After");
                    do {
                        // [NativeName("stb_perlin_noise3")] takes text
                        const bool call = !orders && at(p, T_IDENT) && peek_at(p, 1)->kind == T_LPAREN;
                        if (at(p, T_STRING) || at(p, T_INT) || at(p, T_MINUS) || call) {
                            vec_push(a.values, parse_expr(p));
                            continue;
                        }
                        qname name = parse_qname(p, "a name");
                        if (orders && at(p, T_LPAREN)) { // [After(Move(Position, with Player))]
                            name.has_signature = true;
                            parse_signature(p, &name.signature);
                        }
                        vec_push(a.args, name);
                    } while (accept(p, T_COMMA));
                }
            }
            expect(p, T_RPAREN, "')' after the attribute's arguments");
        }
        vec_push(p->pending, a);
    } while (accept(p, T_COMMA));
    expect(p, T_RBRACKET, "']' after attributes");
}

// `namespace Game.Combat;` and `using Physics;` come before a file's
// declarations. `namespace Combat { ... }` goes anywhere a declaration does,
// and is a unit of its own until its '}': its namespace is its name inside
// the one it's written in.
static void parse_file_header(parser *p, const token *keyword)
{
    const bool is_namespace = str_eq_c(keyword->text, "namespace");
    if (is_namespace) { // namespace Name {: a block
        int i = 1;
        while (peek_at(p, i)->kind == T_DOT && peek_at(p, i + 1)->kind == T_IDENT) i += 2;
        if (peek_at(p, i)->kind == T_LBRACE) {
            const qname name = parse_qname(p, "a namespace name");
            unit *block = NEW(unit);
            block->src = p->unit->src;
            block->outer = p->unit;
            block->usings = p->unit->usings; // Complete: `using` comes before any declaration, and a block is one
            block->using_at = p->unit->using_at;
            block->ns_written = name.text;
            block->ns_at = name.at;
            block->block_at = keyword->at;
            block->open_at = advance(p)->at;
            const token *first = peek(p);
            block->decl_col = first->kind != T_EOF && first->kind != T_RBRACE && first->at.line > block->open_at.line
                                ? first->at.col : keyword->at.col + 4;
            if (p->unit->ns.len > 0) {
                sb ns = {0};
                sb_printf(&ns, STR_FMT "." STR_FMT, STR_ARG(p->unit->ns), STR_ARG(name.text));
                block->ns = (str){ns.data, (int)ns.len};
            } else {
                block->ns = name.text;
            }
            vec_push(p->prog->units, block);
            p->unit = block;
            p->seen_decl = true;
            return;
        }
    }
    if (p->seen_decl) {
        diag_error(keyword->at, "'" STR_FMT "' goes at the top of the file, before any declaration", STR_ARG(keyword->text));
        if (is_namespace) {
            diag_note("a namespace for some of a file's declarations is a block, 'namespace Name { ... }', which goes anywhere a "
                      "declaration does");
        } else if (p->unit->outer) {
            diag_note("a file's 'using' lines are its namespace blocks' too");
        }
        longjmp(p->fail, 1);
    }
    const qname name = parse_qname(p, "a namespace name");
    expect(p, T_SEMI, is_namespace ? "';', or '{' and the namespace's declarations" : "';'");
    if (is_namespace) {
        if (p->unit->ns.len > 0) {
            diag_error(keyword->at, "a file has one namespace; it's already '" STR_FMT "'", STR_ARG(p->unit->ns));
            diag_note("put the declarations of another namespace in a block, 'namespace Name { ... }', which is inside '" STR_FMT "'",
                      STR_ARG(p->unit->ns));
            return;
        }
        p->unit->ns = name.text;
        p->unit->ns_at = name.at;
    } else {
        vec_push(p->unit->usings, name.text);
        vec_push(p->unit->using_at, name.at);
    }
}

// The word and the spaces after it, as source: what editors remove with it.
static str word_and_space(const token *word)
{
    const char *end = word->text.ptr + word->text.len;
    while (*end == ' ' || *end == '\t') end++;
    return (str){word->text.ptr, (int)(end - word->text.ptr)};
}

// `virtual` or `override` where it means nothing: what the words are for, and
// why this isn't it. Editors offer to remove the word.
static void refuse_replace_word(const token *word, const char *why)
{
    if (str_eq_c(word->text, "virtual")) {
        diag_error(word->at, "'virtual' goes before a declaration with code, which another package can then replace: a system, "
                             "a view, a handler, a function, a method, a frag or a vert");
    } else {
        diag_error(word->at, "'override' goes before a declaration with code, which replaces the virtual one it names: a "
                             "system, a view, a handler, a function, a method, a frag or a vert");
    }
    diag_note("%s", why);
    suggest_replacement(word_and_space(word), "");
}

// What `virtual` or `override` can't go before, though it's a declaration:
// data, which no package replaces, and what isn't Tide's code. NULL for code.
static const char *replace_refusal(const parser *p)
{
    const token *t = peek(p);
    if (t->kind == T_COMPONENT) {
        return "a component is data, and data isn't replaced: a package adds components of its own to the game's entities";
    }
    if (t->kind == T_SINGLETON) return "a singleton is data, and data isn't replaced: only code is";
    if (t->kind == T_IDENT && str_eq_c(t->text, "extern") && peek_at(p, 1)->kind == T_SYSTEM) {
        return "an extern system's code is C, which Tide can't replace: a system written in Tide can be virtual";
    }
    if (t->kind != T_IDENT || peek_at(p, 1)->kind != T_IDENT) return NULL;
    if (str_eq_c(t->text, "struct")) return "a struct is data, and data isn't replaced: its methods can be, each marked 'virtual'";
    if (str_eq_c(t->text, "event")) {
        return "an event is data, and data isn't replaced: its handlers can be, like 'virtual event(Hit hit) TakeHit(...)'";
    }
    if (str_eq_c(t->text, "command")) {
        return "a command is data, and data isn't replaced: its handler can be, like 'virtual command(Buy buy, PlayerID sender) "
               "OnBuy(...)'";
    }
    if (str_eq_c(t->text, "enum")) return "an enum is data, and data isn't replaced: only code is";
    if (str_eq_c(t->text, "scene")) return "a scene is data, and data isn't replaced: only code is";
    if (str_eq_c(t->text, "input")) return "the input is data, and the game's own: only code is replaced";
    if (str_eq_c(t->text, "const")) return "a constant is a value, the same on every machine, with no code to replace";
    if (str_eq_c(t->text, "asset")) return "an asset is data, the file it names, and data isn't replaced: only code is";
    if (str_eq_c(t->text, "extern")) {
        return "an extern function's code is C, which Tide can't replace: a virtual function can call it, and be replaced";
    }
    return NULL;
}

// `virtual` and `override` on the declaration they were written before.
static void mark_replace_words(decl *d, const token *virtual, const token *override)
{
    if (virtual && override) {
        diag_error(virtual->at, "an override can't be virtual too: a declaration is replaced once");
        diag_note("two overrides of one declaration are an error, so nothing could replace this one: drop 'virtual'");
        suggest_replacement(word_and_space(virtual), "");
        virtual = NULL;
    }
    if (virtual) {
        d->is_virtual = true;
        d->virtual_at = virtual->at;
    }
    if (override) {
        d->is_override = true;
        d->override_at = override->at;
    }
}

program *program_new(void)
{
    return NEW(program);
}

bool parse_file(program *prog, const source *src, token *toks, const bool recover)
{
    parser p = {.toks = toks, .recover = recover, .prog = prog};
    p.unit = NEW(unit);
    p.unit->src = src;
    p.unit->decl_col = 1;
    vec_push(prog->units, p.unit);

    if (!recover && setjmp(p.fail)) return false;

    while (!at(&p, T_EOF)) {
        if (recover && setjmp(p.fail)) {
            p.pending.count = 0;
            p.has_rest = false;
            skip_declaration(&p);
            continue;
        }
        const token *t = peek(&p);
        // '}' ends the namespace block it's in
        if (t->kind == T_RBRACE && p.unit->outer) {
            if (p.pending.count > 0) {
                diag_error(p.pending.items[0].at, "attributes go right before a declaration");
                p.pending.count = 0;
                if (!recover) return false;
            }
            p.unit->close_at = advance(&p)->at;
            p.unit = (unit *)p.unit->outer;
            continue;
        }
        // `local` before a declaration: it belongs to this machine, not the match.
        // `async` before a function or an event handler: it runs as a task.
        // `virtual` before a declaration with code: another package can replace it.
        // `override` before one: it replaces the virtual declaration it names.
        const token *local = NULL;
        const token *async = NULL;
        const token *virtual = NULL;
        const token *override = NULL;
        p.overriding = false;
        for (;;) {
            const tok_kind after = peek_at(&p, 1)->kind;
            if (!local && t->kind == T_IDENT && str_eq_c(t->text, "local")
                && (after == T_IDENT || after == T_COMPONENT || after == T_SINGLETON || after == T_SYSTEM)) {
                local = advance(&p);
            } else if (!async && t->kind == T_IDENT && str_eq_c(t->text, "async") && (after == T_IDENT || after == T_MUT)) {
                async = advance(&p);
            } else if (!virtual && t->kind == T_IDENT && str_eq_c(t->text, "virtual") && at_replace_word(&p, false)) {
                virtual = advance(&p);
            } else if (!override && t->kind == T_IDENT && str_eq_c(t->text, "override") && at_replace_word(&p, false)) {
                override = advance(&p);
                p.overriding = true;
            } else {
                break;
            }
            t = peek(&p);
        }
        const token *replace_word = virtual ? virtual : override;
        // Contextual keywords: only special at the start of a declaration or a
        // file, so they can still name parameters and locals.
        const bool followed_by_name = peek_at(&p, 1)->kind == T_IDENT;
        if (t->kind == T_IDENT && followed_by_name && (str_eq_c(t->text, "namespace") || str_eq_c(t->text, "using"))) {
            advance(&p);
            if (local) diag_error(local->at, "'local' goes before a declaration, like 'local component Spark { ... }'");
            if (replace_word) refuse_replace_word(replace_word, "a namespace is where names are, and has no code of its own");
            if (p.pending.count > 0 && str_eq_c(t->text, "namespace")) {
                diag_error(p.pending.items[0].at, "attributes go right before a declaration, and a namespace takes none");
                diag_note("put them before the declarations inside it that they're for");
                p.pending.count = 0;
                if (!recover) return false;
            }
            parse_file_header(&p, t);
            continue;
        }
        if (t->kind == T_LBRACKET) {
            parse_attributes(&p);
            continue;
        }
        // settings { ... }: kept apart from the declarations, as it names nothing
        if (t->kind == T_IDENT && str_eq_c(t->text, "settings") && peek_at(&p, 1)->kind == T_LBRACE) {
            advance(&p);
            if (local) diag_error(local->at, "the settings are the whole game's, so they can't be local");
            if (replace_word) refuse_replace_word(replace_word, "the settings are the game's own, and have no code to replace");
            if (p.pending.count > 0) {
                diag_error(p.pending.items[0].at, "settings take no attributes");
                p.pending.count = 0;
            }
            vec_push(prog->settings, parse_settings(&p, t));
            p.seen_decl = true;
            continue;
        }
        // Data, constants and C functions aren't replaced: said before the
        // declaration is read, which then goes as it would without the word
        const char *refusal = replace_word ? replace_refusal(&p) : NULL;
        if (refusal) {
            refuse_replace_word(replace_word, refusal);
            p.overriding = false;
            virtual = override = NULL;
        }
        decl *d;
        const bool function = at_method_from(&p, 0, p.overriding);
        if (!function) advance(&p);
        if (function) d = parse_method(&p, NULL);
        else if (t->kind == T_COMPONENT) d = parse_data_decl(&p, DECL_COMPONENT);
        else if (t->kind == T_SINGLETON) d = parse_data_decl(&p, DECL_SINGLETON);
        else if (t->kind == T_SYSTEM) d = parse_system(&p, false);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "input")) d = parse_data_decl(&p, DECL_INPUT);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "view")) d = parse_system(&p, true);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "frag")) d = parse_shader(&p, true);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "vert")) d = parse_shader(&p, false);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "audio")) d = parse_audio(&p);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "struct")) d = parse_data_decl(&p, DECL_STRUCT);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "event")) d = parse_data_decl(&p, DECL_EVENT);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "command")) {
            d = parse_data_decl(&p, DECL_EVENT);
            d->is_command = true;
        }
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "enum")) d = parse_enum(&p);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "scene")) {
            d = parse_data_decl(&p, DECL_COMPONENT);
            d->is_scene = true;
        }
        else if (t->kind == T_IDENT && at(&p, T_LPAREN) && str_eq_c(t->text, "event")) d = parse_handler(&p, false);
        else if (t->kind == T_IDENT && at(&p, T_LPAREN) && str_eq_c(t->text, "command")) d = parse_handler(&p, true);
        else if (t->kind == T_IDENT && at(&p, T_SYSTEM) && str_eq_c(t->text, "extern")) d = parse_extern_system(&p);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "extern")) d = parse_extern(&p);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "const")) d = parse_const(&p);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "asset")) d = parse_asset(&p);
        else if (t->kind == T_IDENT && followed_by_name && str_eq_c(t->text, "external")) {
            diag_error(t->at, "did you mean 'extern'? It declares a function written in C: 'extern float Noise(float x);'");
            longjmp(p.fail, 1);
        }
        else fail_at(&p, t, "'component', 'scene', 'singleton', 'struct', 'enum', 'event', 'input', 'system', 'view', 'frag', 'vert', 'extern', 'const', 'asset' or a function"); // Consumed, so recovery skips it
        d->unit = p.unit;
        p.overriding = false;
        if (local) {
            d->is_local = true;
            d->local_at = local->at;
        }
        if (virtual || override) mark_replace_words(d, virtual, override);
        if (async && (d->kind == DECL_FUNCTION || (d->kind == DECL_SYSTEM && d->is_handler))) {
            d->is_async = true;
            d->async_at = async->at;
        } else if (async) {
            diag_error(async->at, "'async' goes before a function or an event handler, like 'async void Countdown() { ... }'");
            if (d->kind == DECL_SYSTEM) diag_note("%s runs again every %s, so it can't wait; it can start a task by calling an async function",
                                                  d->is_view ? "a view" : "a system", d->is_view ? "frame" : "tick");
        }
        d->attributes.items = p.pending.items;
        d->attributes.count = p.pending.count;
        d->attributes.cap = p.pending.cap;
        p.pending.items = NULL;
        p.pending.count = p.pending.cap = 0;
        p.seen_decl = true;
        vec_push(prog->decls, d);
    }
    if (p.unit->outer) {
        diag_error(peek(&p)->at, "expected '}' to end the namespace '" STR_FMT "', which opens at line %d", STR_ARG(p.unit->ns_written),
                   p.unit->open_at.line);
        if (!recover) return false;
    }
    if (p.pending.count > 0) {
        diag_error(p.pending.items[0].at, "attributes go right before a declaration");
        if (!recover) return false;
    }
    return true;
}
