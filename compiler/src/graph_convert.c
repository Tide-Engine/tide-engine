// A .tide file as a graph (notes/visual-scripting.md): its syntax tree as parsed, written as the graph that stands
// for the same Tide, with its comments, and laid out to be read.

#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "lexer.h"

// ---------------------------------------------------------------------------
// JSON being built

typedef struct jv jv;
struct jv {
    graph_json_kind kind;
    bool boolean;
    double number;
    str text;
    VEC(str) keys;
    VEC(jv *) items;
    bool one_line; // Written on one line: a node, a parameter
};

static jv *jv_new(const graph_json_kind kind)
{
    jv *v = NEW(jv);
    v->kind = kind;
    return v;
}

static jv *jv_obj(void) { return jv_new(GJ_OBJECT); }
static jv *jv_arr(void) { return jv_new(GJ_ARRAY); }

static jv *jv_s(const str s)
{
    jv *v = jv_new(GJ_STRING);
    v->text = s.ptr ? s : (str){"", 0};
    return v;
}

static jv *jv_c(const char *s) { return jv_s(str_from(s)); }

static jv *jv_n(const double n)
{
    jv *v = jv_new(GJ_NUMBER);
    v->number = n;
    return v;
}

static jv *jv_b(const bool b)
{
    jv *v = jv_new(GJ_BOOL);
    v->boolean = b;
    return v;
}

static void set(jv *o, const char *key, jv *v)
{
    if (!v) return;
    for (int i = 0; i < o->keys.count; i++) {
        if (str_eq_c(o->keys.items[i], key)) {
            o->items.items[i] = v;
            return;
        }
    }
    vec_push(o->keys, str_from(key));
    vec_push(o->items, v);
}

static void set_s(jv *o, const char *key, const str s)
{
    if (s.len) set(o, key, jv_s(s));
}

static void set_true(jv *o, const char *key, const bool b)
{
    if (b) set(o, key, jv_b(true));
}

static void push(jv *a, jv *v)
{
    vec_push(a->items, v);
}

static void put_json_string(sb *out, const str s)
{
    sb_put(out, "\"");
    for (int i = 0; i < s.len; i++) {
        const unsigned char c = (unsigned char)s.ptr[i];
        if (c == '"') sb_put(out, "\\\"");
        else if (c == '\\') sb_put(out, "\\\\");
        else if (c == '\n') sb_put(out, "\\n");
        else if (c == '\r') sb_put(out, "\\r");
        else if (c == '\t') sb_put(out, "\\t");
        else if (c < 0x20) sb_printf(out, "\\u%04x", c);
        else sb_putn(out, s.ptr + i, 1);
    }
    sb_put(out, "\"");
}

static bool nested(const jv *v)
{
    for (int i = 0; i < v->items.count; i++) {
        const jv *item = v->items.items[i];
        if (item->kind == GJ_OBJECT) return true;
        if (item->kind == GJ_ARRAY && nested(item)) return true;
    }
    return false;
}

static void put_indent(sb *out, const int depth)
{
    for (int i = 0; i < depth; i++) sb_put(out, "  ");
}

static void print_jv(sb *out, const jv *v, const int depth, const bool one_line)
{
    switch (v->kind) {
    case GJ_NULL: sb_put(out, "null"); return;
    case GJ_BOOL: sb_put(out, v->boolean ? "true" : "false"); return;
    case GJ_NUMBER: sb_printf(out, "%d", (int)v->number); return;
    case GJ_STRING: put_json_string(out, v->text); return;
    case GJ_ARRAY:
    case GJ_OBJECT: break;
    }
    const bool object = v->kind == GJ_OBJECT;
    const bool flat = one_line || v->one_line || !nested(v);
    sb_put(out, object ? "{" : "[");
    if (!v->items.count) {
        sb_put(out, object ? "}" : "]");
        return;
    }
    bool first = true;
    for (int i = 0; i < v->items.count; i++) {
        // An element's comment slot that nothing was said in
        if (object && str_eq_c(v->keys.items[i], "comment") && v->items.items[i]->kind == GJ_STRING && !v->items.items[i]->text.len) {
            continue;
        }
        if (!first) sb_put(out, flat ? ", " : ",");
        first = false;
        if (!flat) {
            sb_put(out, "\n");
            put_indent(out, depth + 1);
        }
        if (object) {
            put_json_string(out, v->keys.items[i]);
            sb_put(out, ": ");
        }
        print_jv(out, v->items.items[i], depth + 1, flat);
    }
    if (!flat) {
        sb_put(out, "\n");
        put_indent(out, depth);
    }
    sb_put(out, object ? "}" : "]");
}

// ---------------------------------------------------------------------------
// The converter

// A node of the graph being made, with what its layout needs
typedef struct lnode {
    int id;
    jv *obj;
    bool exec;
    VEC(int) inputs; // Data inputs, in the order its text reads them
    int next;
    VEC(int) bodies; // Exec outputs other than next, in order
    int w, h;
    int x, y;
    bool placed;
} lnode;

typedef struct comment {
    int line;
    str text;
    bool trailing; // After code on its line: about what's on that line
} comment;

typedef struct scoped {
    str name;
    int id; // 0: a name that isn't a local's (a lambda's parameter)
} scoped;

typedef struct cv {
    const source *src;
    VEC(int) line_starts;
    int next_id;
    // The graph being made
    VEC(lnode *) pool;
    VEC(scoped) scope;
    VEC(comment) comments;
    int comment_at;
    jv *last_comment; // The comment of the element last given one, and the line it starts on
    int last_line;
    bool failed;
    const char *why; // What the converter can't write as a graph
} cv;

static int new_id(cv *c)
{
    return ++c->next_id;
}

static int offset_of(const cv *c, const loc at)
{
    if (at.line < 1 || at.line > c->line_starts.count) return 0;
    const int off = c->line_starts.items[at.line - 1] + at.col - 1;
    return off < (int)c->src->len ? off : (int)c->src->len;
}

// The source between two places, one line, spaces as written
static str slice(const cv *c, const loc from, const loc to)
{
    const int a = offset_of(c, from), b = offset_of(c, to);
    if (b <= a) return (str){NULL, 0};
    sb out = {0};
    bool space = false;
    for (int i = a; i < b; i++) {
        const char ch = c->src->text[i];
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
            space = true;
            continue;
        }
        if (space && out.len) sb_put(&out, " ");
        space = false;
        sb_putn(&out, &ch, 1);
    }
    return (str){out.data, (int)out.len};
}

static str type_text(const cv *c, const type_ref *t, const str fallback)
{
    if (t && t->end.line) {
        const str s = slice(c, t->at, t->end);
        if (s.len) return s;
    }
    if (t && t->text.len) return t->text;
    return fallback;
}

static const char *op_text(const tok_kind op)
{
    switch (op) {
    case T_OR: return "||";
    case T_AND: return "&&";
    case T_PIPE: return "|";
    case T_CARET: return "^";
    case T_AMP: return "&";
    case T_EQ: return "==";
    case T_NE: return "!=";
    case T_LT: return "<";
    case T_LE: return "<=";
    case T_GT: return ">";
    case T_GE: return ">=";
    case T_SHL: return "<<";
    case T_SHR: return ">>";
    case T_PLUS: return "+";
    case T_MINUS: return "-";
    case T_STAR: return "*";
    case T_SLASH: return "/";
    case T_PERCENT: return "%";
    case T_NOT: return "!";
    case T_TILDE: return "~";
    case T_ASSIGN: return "=";
    case T_PLUS_ASSIGN: return "+=";
    case T_MINUS_ASSIGN: return "-=";
    case T_STAR_ASSIGN: return "*=";
    case T_SLASH_ASSIGN: return "/=";
    case T_PERCENT_ASSIGN: return "%=";
    case T_SHL_ASSIGN: return "<<=";
    case T_SHR_ASSIGN: return ">>=";
    case T_AMP_ASSIGN: return "&=";
    case T_PIPE_ASSIGN: return "|=";
    case T_CARET_ASSIGN: return "^=";
    case T_PLUS_PLUS: return "++";
    case T_MINUS_MINUS: return "--";
    default: return "?";
    }
}

static void cant(cv *c, const char *why)
{
    if (!c->failed) c->why = why;
    c->failed = true;
}

static void append_text(jv *to, const str more)
{
    sb out = {0};
    if (to->text.len) {
        sb_putn(&out, to->text.ptr, (size_t)to->text.len);
        sb_put(&out, "\n");
    }
    sb_putn(&out, more.ptr, (size_t)more.len);
    to->text = (str){out.data, (int)out.len};
}

// Comments: every one before a line, which goes on the element that starts there; one after code on its line goes
// on the element that line is in
static str comments_before(cv *c, const int line)
{
    sb out = {0};
    while (c->comment_at < c->comments.count && c->comments.items[c->comment_at].line < line) {
        comment *k = &c->comments.items[c->comment_at++];
        if (k->trailing && c->last_comment && k->line >= c->last_line) {
            append_text(c->last_comment, k->text);
            continue;
        }
        if (out.len) sb_put(&out, "\n");
        sb_putn(&out, k->text.ptr, (size_t)k->text.len);
    }
    return (str){out.data, (int)out.len};
}

// The comment of an element that starts on `line`: what's before it, and what's said after code on its lines,
// which comes later. Written only if it's said something
static jv *comment_slot(cv *c, const int line)
{
    jv *slot = jv_s((str){"", 0});
    if (line > 0) {
        slot->text = comments_before(c, line);
        c->last_comment = slot;
        c->last_line = line;
    }
    return slot;
}

static void comment_on(cv *c, jv *o, const int line)
{
    set(o, "comment", comment_slot(c, line));
}

static void find_comments(cv *c)
{
    const char *t = c->src->text;
    const int n = (int)c->src->len;
    int line = 1;
    for (int i = 0; i < n; i++) {
        const char ch = t[i];
        if (ch == '\n') {
            line++;
            continue;
        }
        if (ch == '"' || ch == '\'') { // Strings and characters aren't comments
            const bool interp = ch == '"' && i > 0 && t[i - 1] == '$';
            int depth = 0;
            for (i++; i < n; i++) {
                if (t[i] == '\n') line++;
                if (t[i] == '\\') {
                    i++;
                    continue;
                }
                if (interp && t[i] == '{') {
                    if (i + 1 < n && t[i + 1] == '{' && !depth) {
                        i++;
                        continue;
                    }
                    depth++;
                }
                if (interp && t[i] == '}' && depth) depth--;
                if (t[i] == ch && !depth) break;
            }
            continue;
        }
        if (ch == '/' && i + 1 < n && t[i + 1] == '/') {
            int start = i + 2;
            int end = start;
            while (end < n && t[end] != '\n' && t[end] != '\r') end++;
            if (start < end && t[start] == ' ') start++;
            int before = i - 1;
            while (before >= 0 && (t[before] == ' ' || t[before] == '\t')) before--;
            const bool trailing = before >= 0 && t[before] != '\n' && t[before] != '\r';
            const comment k = {line, {t + start, end - start}, trailing};
            vec_push(c->comments, k);
            i = end - 1;
            continue;
        }
        if (ch == '/' && i + 1 < n && t[i + 1] == '*') {
            const int first = line;
            int end = i + 2;
            while (end + 1 < n && !(t[end] == '*' && t[end + 1] == '/')) {
                if (t[end] == '\n') line++;
                end++;
            }
            int start = i + 2;
            int stop = end;
            while (start < stop && (t[start] == ' ' || t[start] == '\n' || t[start] == '\r')) start++;
            while (stop > start && (t[stop - 1] == ' ' || t[stop - 1] == '\n' || t[stop - 1] == '\r')) stop--;
            int before = i - 1;
            while (before >= 0 && (t[before] == ' ' || t[before] == '\t')) before--;
            const bool trailing = before >= 0 && t[before] != '\n' && t[before] != '\r';
            const comment k = {first, {t + start, stop - start}, trailing};
            vec_push(c->comments, k);
            i = end + 1;
        }
    }
}

// ---------------------------------------------------------------------------
// Graph pools

typedef struct pool_mark {
    int pool_count;
} pool_mark;

static lnode *add_node(cv *c, const char *k, const bool exec)
{
    lnode *n = NEW(lnode);
    n->id = new_id(c);
    n->exec = exec;
    n->obj = jv_obj();
    n->obj->one_line = true;
    set(n->obj, "id", jv_n(n->id));
    set(n->obj, "k", jv_c(k));
    vec_push(c->pool, n);
    return n;
}

static lnode *node_by_id(const cv *c, const int id)
{
    for (int i = c->pool.count - 1; i >= 0; i--) {
        if (c->pool.items[i]->id == id) return c->pool.items[i];
    }
    return NULL;
}

static void wire(lnode *n, const char *key, const int id)
{
    if (!id) return;
    set(n->obj, key, jv_n(id));
    vec_push(n->inputs, id);
}

static void exec_out(lnode *n, const char *key, const int id)
{
    if (!id) return;
    set(n->obj, key, jv_n(id));
    vec_push(n->bodies, id);
}

static int scope_mark(const cv *c)
{
    return c->scope.count;
}

static void scope_add(cv *c, const str name, const int id)
{
    const scoped s = {name, id};
    vec_push(c->scope, s);
}

static int scope_find(const cv *c, const str name)
{
    for (int i = c->scope.count - 1; i >= 0; i--) {
        if (str_eq(c->scope.items[i].name, name)) return c->scope.items[i].id;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Expressions

static int ex(cv *c, const expr *e);
static int chain(cv *c, const stmt *s);
static int chain_list(cv *c, stmt *const *items, int count);

static jv *types_array(const cv *c, const expr *e)
{
    if (!e->type_args.count) return NULL;
    jv *a = jv_arr();
    for (int i = 0; i < e->type_args.count; i++) push(a, jv_s(type_text(c, e->type_args.items[i], (str){"", 0})));
    return a;
}

static void call_args(cv *c, lnode *n, const expr *e)
{
    jv *args = jv_arr();
    for (int i = 0; i < e->args.count; i++) {
        const int a = ex(c, e->args.items[i]);
        push(args, jv_n(a));
        vec_push(n->inputs, a);
    }
    set(n->obj, "args", args);
    bool named = false;
    for (int i = 0; i < e->arg_names.count; i++) named |= e->arg_names.items[i].len > 0;
    if (named) {
        jv *names = jv_arr();
        for (int i = 0; i < e->args.count; i++) push(names, jv_s(i < e->arg_names.count ? e->arg_names.items[i] : (str){"", 0}));
        set(n->obj, "argNames", names);
    }
}

// A value written with `out` before it
static int out_arg(cv *c, const expr *e)
{
    if (e->out_local) {
        const stmt *v = e->out_local;
        lnode *n = add_node(c, "outVar", false);
        set_true(n->obj, "mut", v->is_mut);
        if (v->type_ref || v->type_name.len) set_s(n->obj, "type", type_text(c, v->type_ref, v->type_name));
        set_s(n->obj, "name", v->name);
        scope_add(c, v->name, n->id);
        return n->id;
    }
    expr copy = *e;
    copy.is_out = false;
    const int target = ex(c, &copy);
    lnode *n = add_node(c, "out", false);
    wire(n, "target", target);
    return n->id;
}

static bool synthesized_position(const expr *index)
{
    const expr *p = index->lhs;
    return p && p->kind == E_CALL && (str_eq_c(p->name, "int2") || str_eq_c(p->name, "int3")) && p->args.count >= 2
           && p->at.line == p->args.items[0]->at.line && p->at.col == p->args.items[0]->at.col;
}

static int ex(cv *c, const expr *e)
{
    if (!e) return 0;
    if (e->is_out) return out_arg(c, e);
    lnode *n;
    switch (e->kind) {
    case E_INT:
        n = add_node(c, e->is_char ? "char" : "int", false);
        set_s(n->obj, "text", e->text);
        return n->id;
    case E_FLOAT:
        n = add_node(c, "float", false);
        set_s(n->obj, "text", e->text);
        return n->id;
    case E_BOOL:
        n = add_node(c, "bool", false);
        set(n->obj, "value", jv_b(e->bool_value));
        return n->id;
    case E_STRING:
        n = add_node(c, "string", false);
        set(n->obj, "text", jv_s(e->text));
        return n->id;
    case E_THIS: return add_node(c, "this", false)->id;
    case E_DEFAULT: return add_node(c, "default", false)->id;
    case E_NULL: return add_node(c, "null", false)->id;
    case E_NAME: {
        const int local = e->type_arg_ref ? 0 : scope_find(c, e->name);
        if (local) {
            n = add_node(c, "local", false);
            set(n->obj, "ref", jv_n(local));
            return n->id;
        }
        n = add_node(c, "name", false);
        set(n->obj, "name", jv_s(e->type_arg_ref ? type_text(c, e->type_arg_ref, e->name) : e->name));
        set_true(n->obj, "local", e->placement_local);
        return n->id;
    }
    case E_MEMBER: {
        if (e->enum_shorthand) {
            n = add_node(c, "member", false);
            set(n->obj, "shorthand", jv_b(true));
            set_s(n->obj, "member", e->member);
            return n->id;
        }
        const int object = ex(c, e->object);
        n = add_node(c, "member", false);
        wire(n, "object", object);
        set_s(n->obj, "member", e->member);
        return n->id;
    }
    case E_CALL: {
        n = add_node(c, "call", false);
        set_s(n->obj, "name", e->name);
        set(n->obj, "typeArgs", types_array(c, e));
        set_true(n->obj, "local", e->placement_local);
        call_args(c, n, e);
        return n->id;
    }
    case E_METHOD: {
        const int object = ex(c, e->object);
        n = add_node(c, "method", false);
        wire(n, "object", object);
        set_s(n->obj, "name", e->name);
        set(n->obj, "typeArgs", types_array(c, e));
        call_args(c, n, e);
        return n->id;
    }
    case E_BINARY: {
        const int lhs = ex(c, e->lhs), rhs = ex(c, e->rhs);
        n = add_node(c, "binary", false);
        set(n->obj, "op", jv_c(op_text(e->op)));
        wire(n, "lhs", lhs);
        wire(n, "rhs", rhs);
        return n->id;
    }
    case E_UNARY: {
        const int value = ex(c, e->lhs);
        n = add_node(c, "unary", false);
        set(n->obj, "op", jv_c(op_text(e->op)));
        wire(n, "value", value);
        return n->id;
    }
    case E_STEP: {
        const int target = ex(c, e->lhs);
        n = add_node(c, "step", false);
        set(n->obj, "op", jv_c(op_text(e->op)));
        set_true(n->obj, "postfix", e->postfix);
        wire(n, "target", target);
        return n->id;
    }
    case E_LITERAL: {
        if (e->lambda) {
            const int mark = scope_mark(c);
            jv *params = jv_arr();
            for (int i = 0; i < e->lambda_params.count; i++) {
                const param *p = &e->lambda_params.items[i];
                jv *o = jv_obj();
                set_true(o, "mut", p->mode == PARAM_MUT);
                set_s(o, "type", type_text(c, p->type_ref, p->type_name));
                set_s(o, "name", p->name);
                push(params, o);
                scope_add(c, p->name, 0);
            }
            const int body = ex(c, e->lambda_body);
            c->scope.count = mark;
            n = add_node(c, "lambda", false);
            set(n->obj, "params", params);
            wire(n, "body", body);
            return n->id;
        }
        jv *inits = jv_arr();
        VEC(int) values = {0};
        for (int i = 0; i < e->inits.count; i++) {
            const int v = ex(c, e->inits.items[i].value);
            vec_push(values, v);
        }
        n = add_node(c, "literal", false);
        set_s(n->obj, "type", e->type_arg_ref ? type_text(c, e->type_arg_ref, e->name) : e->name);
        for (int i = 0; i < e->inits.count; i++) {
            jv *o = jv_obj();
            set_s(o, "name", e->inits.items[i].name);
            set(o, "value", jv_n(values.items[i]));
            vec_push(n->inputs, values.items[i]);
            push(inits, o);
        }
        set(n->obj, "inits", inits);
        return n->id;
    }
    case E_CONDITIONAL: {
        const int cond = ex(c, e->cond), then = ex(c, e->lhs), otherwise = ex(c, e->rhs);
        n = add_node(c, "conditional", false);
        wire(n, "cond", cond);
        wire(n, "then", then);
        wire(n, "else", otherwise);
        return n->id;
    }
    case E_SWITCH: {
        const int value = ex(c, e->object);
        VEC(int) ids = {0};
        for (int i = 0; i < e->inits.count; i++) {
            const int pattern = e->inits.items[i].value ? ex(c, e->inits.items[i].value) : 0;
            const int v = i < e->args.count ? ex(c, e->args.items[i]) : 0;
            vec_push(ids, pattern);
            vec_push(ids, v);
        }
        n = add_node(c, "switchExpr", false);
        wire(n, "value", value);
        jv *arms = jv_arr();
        for (int i = 0; i < e->inits.count; i++) {
            jv *o = jv_obj();
            set(o, "pattern", ids.items[2 * i] ? jv_n(ids.items[2 * i]) : jv_new(GJ_NULL));
            set(o, "value", jv_n(ids.items[2 * i + 1]));
            if (ids.items[2 * i]) vec_push(n->inputs, ids.items[2 * i]);
            vec_push(n->inputs, ids.items[2 * i + 1]);
            push(arms, o);
        }
        set(n->obj, "arms", arms);
        return n->id;
    }
    case E_INTERP: {
        VEC(int) ids = {0};
        for (int i = 0; i < e->args.count; i++) {
            const int v = ex(c, e->args.items[i]);
            vec_push(ids, v);
        }
        n = add_node(c, "interp", false);
        jv *parts = jv_arr(), *formats = jv_arr(), *values = jv_arr();
        bool any_format = false;
        for (int i = 0; i < e->parts.count; i++) push(parts, jv_s(e->parts.items[i]));
        for (int i = 0; i < e->formats.count; i++) {
            push(formats, jv_s(e->formats.items[i]));
            any_format |= e->formats.items[i].len > 0;
        }
        for (int i = 0; i < ids.count; i++) {
            push(values, jv_n(ids.items[i]));
            vec_push(n->inputs, ids.items[i]);
        }
        set(n->obj, "parts", parts);
        if (any_format) set(n->obj, "formats", formats);
        set(n->obj, "values", values);
        return n->id;
    }
    case E_INDEX: {
        const int object = ex(c, e->object);
        VEC(int) ids = {0};
        if (synthesized_position(e)) {
            for (int i = 0; i < e->lhs->args.count; i++) {
                const int v = ex(c, e->lhs->args.items[i]);
                vec_push(ids, v);
            }
        } else {
            const int v = ex(c, e->lhs);
            vec_push(ids, v);
        }
        n = add_node(c, "index", false);
        wire(n, "object", object);
        jv *index = jv_arr();
        for (int i = 0; i < ids.count; i++) {
            push(index, jv_n(ids.items[i]));
            vec_push(n->inputs, ids.items[i]);
        }
        set(n->obj, "index", index);
        return n->id;
    }
    case E_LIST: {
        VEC(int) ids = {0};
        for (int i = 0; i < e->args.count; i++) {
            const int v = ex(c, e->args.items[i]);
            vec_push(ids, v);
        }
        n = add_node(c, "list", false);
        jv *items = jv_arr();
        for (int i = 0; i < ids.count; i++) {
            push(items, jv_n(ids.items[i]));
            vec_push(n->inputs, ids.items[i]);
        }
        set(n->obj, "items", items);
        return n->id;
    }
    case E_COALESCE: {
        const int lhs = ex(c, e->lhs), rhs = ex(c, e->rhs);
        n = add_node(c, "coalesce", false);
        wire(n, "lhs", lhs);
        wire(n, "rhs", rhs);
        return n->id;
    }
    case E_IS: {
        const int value = ex(c, e->lhs);
        n = add_node(c, "is", false);
        wire(n, "value", value);
        set_s(n->obj, "type", type_text(c, e->pattern_ref, e->pattern));
        if (e->binding) {
            set_s(n->obj, "binding", e->binding->name);
            scope_add(c, e->binding->name, n->id);
        }
        return n->id;
    }
    case E_HAS: {
        n = add_node(c, "has", false);
        set_s(n->obj, "subject", e->lhs ? e->lhs->name : (str){NULL, 0});
        set_s(n->obj, "type", type_text(c, e->pattern_ref, e->pattern));
        set_s(n->obj, "field", e->pattern_field);
        return n->id;
    }
    case E_TRY:
    case E_DEFAULTED:
    case E_AWAIT: {
        const int value = ex(c, e->lhs);
        n = add_node(c, e->kind == E_TRY ? "try" : e->kind == E_AWAIT ? "await" : "defaulted", false);
        wire(n, "value", value);
        return n->id;
    }
    }
    cant(c, "an expression of a kind the converter doesn't know");
    return add_node(c, "default", false)->id;
}

// ---------------------------------------------------------------------------
// Statements

static int expr_line(const expr *e)
{
    int line = e ? e->at.line : 0;
    for (const expr *p = e; p; ) {
        if (p->at.line && p->at.line < line) line = p->at.line;
        p = p->kind == E_MEMBER || p->kind == E_METHOD || p->kind == E_INDEX || p->kind == E_SWITCH ? p->object
            : p->kind == E_BINARY || p->kind == E_COALESCE || p->kind == E_IS || p->kind == E_DEFAULTED || p->kind == E_HAS
                ? p->lhs
            : p->kind == E_CONDITIONAL ? p->cond
            : p->kind == E_STEP && p->postfix ? p->lhs
                                              : NULL;
    }
    return line;
}

static int stmt_line(const stmt *s)
{
    if (s->kind == S_ASSIGN) {
        const int t = expr_line(s->target);
        return t && t < s->at.line ? t : s->at.line;
    }
    if (s->kind == S_EXPR) return expr_line(s->value);
    if (s->kind == S_FOREACH || s->kind == S_PARALLEL) return s->indexed ? s->indexed_at.line : s->at.line;
    return s->at.line;
}

static int one_stmt(cv *c, const stmt *s);

// A statement's code: a block's statements, or one statement as a block of its own
static int chain(cv *c, const stmt *s)
{
    if (!s) return 0;
    if (s->kind == S_BLOCK) {
        const int mark = scope_mark(c);
        const int first = chain_list(c, s->stmts.items, s->stmts.count);
        c->scope.count = mark;
        return first;
    }
    const int mark = scope_mark(c);
    const int first = one_stmt(c, s);
    c->scope.count = mark;
    return first;
}

static int chain_list(cv *c, stmt *const *items, const int count)
{
    int first = 0;
    lnode *last = NULL;
    for (int i = 0; i < count; i++) {
        const int id = one_stmt(c, items[i]);
        if (!id) continue;
        if (last) {
            set(last->obj, "next", jv_n(id));
            last->next = id;
        } else {
            first = id;
        }
        last = node_by_id(c, id);
    }
    return first;
}

static void loop_var(cv *c, lnode *n, const stmt *s)
{
    if (s->type_ref || s->type_name.len) set_s(n->obj, "type", type_text(c, s->type_ref, s->type_name));
    set_s(n->obj, "name", s->name);
}

static int eval_stmt(cv *c, const stmt *s, const bool stacked_continuation)
{
    const int value = ex(c, s->value);
    lnode *n = add_node(c, "eval", true);
    wire(n, "value", value);
    set_true(n->obj, "stacked", stacked_continuation);
    const expr *call = s->value;
    if (call && call->block) {
        int block;
        if (call->stacked && call->block->kind == S_EXPR) {
            block = eval_stmt(c, call->block, true);
        } else {
            const int mark = scope_mark(c);
            block = chain(c, call->block);
            c->scope.count = mark;
        }
        exec_out(n, "block", block);
        set_true(n->obj, "withBlock", !block);
    }
    return n->id;
}

static int one_stmt(cv *c, const stmt *s)
{
    lnode *n;
    const int line = stmt_line(s);
    jv *comment_text = comment_slot(c, line);
    int id = 0;
    switch (s->kind) {
    case S_BLOCK: {
        const int body = chain(c, s);
        n = add_node(c, "block", true);
        exec_out(n, "body", body);
        id = n->id;
        break;
    }
    case S_IF: {
        const int cond = ex(c, s->cond);
        const int then = chain(c, s->then_stmt);
        const int otherwise = chain(c, s->else_stmt);
        n = add_node(c, "if", true);
        wire(n, "cond", cond);
        exec_out(n, "then", then);
        exec_out(n, "else", otherwise);
        id = n->id;
        break;
    }
    case S_RETURN:
    case S_FAIL: {
        const int value = ex(c, s->value);
        n = add_node(c, s->kind == S_RETURN ? "return" : "fail", true);
        wire(n, "value", value);
        id = n->id;
        break;
    }
    case S_BREAK: id = add_node(c, "break", true)->id; break;
    case S_CONTINUE: id = add_node(c, "continue", true)->id; break;
    case S_VAR: {
        if (s->discard) {
            const int value = ex(c, s->value);
            n = add_node(c, "discard", true);
            wire(n, "value", value);
            id = n->id;
            break;
        }
        // A shared local the parser gave its zero value, as it does one written without
        const bool zeroed = s->is_shared && s->value && s->value->kind == E_DEFAULT && s->value->at.line == s->name_at.line
                            && s->value->at.col == s->name_at.col;
        const int value = zeroed ? 0 : ex(c, s->value);
        n = add_node(c, "var", true);
        set_true(n->obj, "mut", s->is_mut && !s->is_shared);
        loop_var(c, n, s);
        wire(n, "value", value);
        set_true(n->obj, "shared", s->is_shared);
        scope_add(c, s->name, n->id);
        id = n->id;
        break;
    }
    case S_ASSIGN: {
        const int target = ex(c, s->target);
        const bool step = s->op == T_PLUS_PLUS || s->op == T_MINUS_MINUS;
        const int value = step ? 0 : ex(c, s->value);
        n = add_node(c, "assign", true);
        set(n->obj, "op", jv_c(op_text(s->op)));
        wire(n, "target", target);
        wire(n, "value", value);
        id = n->id;
        break;
    }
    case S_EXPR: id = eval_stmt(c, s, false); break;
    case S_WHILE: {
        const int cond = ex(c, s->cond);
        const int body = chain(c, s->then_stmt);
        n = add_node(c, "while", true);
        wire(n, "cond", cond);
        exec_out(n, "body", body);
        id = n->id;
        break;
    }
    case S_FOR: {
        const int mark = scope_mark(c);
        int init = 0;
        if (s->init) init = one_stmt(c, s->init);
        const int cond = ex(c, s->cond);
        const int step = s->step ? one_stmt(c, s->step) : 0;
        const int body = chain(c, s->then_stmt);
        c->scope.count = mark;
        n = add_node(c, "for", true);
        exec_out(n, "init", init);
        wire(n, "cond", cond);
        exec_out(n, "step", step);
        exec_out(n, "body", body);
        id = n->id;
        break;
    }
    case S_FOREACH:
    case S_PARALLEL: {
        const bool parallel = s->kind == S_PARALLEL;
        const int collection = ex(c, s->value);
        const int by = parallel ? ex(c, s->by) : 0;
        const int offset = parallel ? ex(c, s->offset) : 0;
        n = add_node(c, parallel ? "parallel" : "foreach", true);
        set_true(n->obj, "indexed", s->indexed);
        loop_var(c, n, s);
        wire(n, parallel ? "grid" : "collection", collection);
        wire(n, "by", by);
        wire(n, "offset", offset);
        const int mark = scope_mark(c);
        scope_add(c, s->name, n->id);
        const int body = chain(c, s->then_stmt);
        c->scope.count = mark;
        exec_out(n, "body", body);
        id = n->id;
        break;
    }
    case S_SWITCH: {
        const int value = ex(c, s->cond);
        jv *cases = jv_arr();
        VEC(int) label_ids = {0};
        VEC(int) bodies = {0};
        for (int i = 0; i < s->cases.count; i++) {
            const switch_case *k = &s->cases.items[i];
            jv *labels = jv_arr();
            for (int l = 0; l < k->labels.count; l++) {
                const int label = k->labels.items[l] ? ex(c, k->labels.items[l]) : 0;
                push(labels, label ? jv_n(label) : jv_new(GJ_NULL));
                if (label) vec_push(label_ids, label);
            }
            const int mark = scope_mark(c);
            const int body = chain_list(c, k->body.items, k->body.count);
            c->scope.count = mark;
            vec_push(bodies, body);
            jv *o = jv_obj();
            set(o, "labels", labels);
            if (body) set(o, "body", jv_n(body));
            push(cases, o);
        }
        n = add_node(c, "switch", true);
        wire(n, "value", value);
        for (int i = 0; i < label_ids.count; i++) vec_push(n->inputs, label_ids.items[i]);
        set(n->obj, "cases", cases);
        for (int i = 0; i < bodies.count; i++) {
            if (bodies.items[i]) vec_push(n->bodies, bodies.items[i]);
        }
        id = n->id;
        break;
    }
    }
    if (!id) {
        cant(c, "a statement of a kind the converter doesn't know");
        return 0;
    }
    lnode *made = node_by_id(c, id);
    set(made->obj, "comment", comment_text);
    return id;
}

// ---------------------------------------------------------------------------
// Layout: statements left to right along `next`, a statement's values to the left of it, and the code a
// statement holds below it. A row that grows too wide goes on below the rest.

enum { NODE_GAP = 40, ROW_GAP = 60, MAX_ROW = 3200 };

static int text_width(const str s)
{
    return s.len * 8;
}

static void measure(cv *c, lnode *n)
{
    (void)c;
    const jv *o = n->obj;
    int widest = 0;
    for (int i = 0; i < o->keys.count; i++) {
        const jv *v = o->items.items[i];
        if (v->kind == GJ_STRING && text_width(v->text) > widest) widest = text_width(v->text);
    }
    n->w = 140 + (widest > 260 ? 260 : widest);
    const int ports = n->inputs.count + n->bodies.count + 1;
    n->h = 48 + 24 * ports;
}

// A value's tree, its right edge at `right` and its top at `top`: the height it takes
static int place_value(cv *c, const int id, const int right, const int top)
{
    lnode *n = node_by_id(c, id);
    if (!n || n->placed) return 0;
    n->placed = true;
    n->x = right - n->w;
    n->y = top;
    int height = 0;
    for (int i = 0; i < n->inputs.count; i++) {
        const int h = place_value(c, n->inputs.items[i], n->x - NODE_GAP, top + height);
        height += h ? h + 16 : 0;
    }
    if (height) height -= 16;
    return height > n->h ? height : n->h;
}

// How wide a value's tree is, from its node leftwards
static int value_width(cv *c, const int id)
{
    lnode *n = node_by_id(c, id);
    if (!n) return 0;
    int widest = 0;
    for (int i = 0; i < n->inputs.count; i++) {
        const int w = value_width(c, n->inputs.items[i]);
        if (w > widest) widest = w;
    }
    return n->w + (widest ? NODE_GAP + widest : 0);
}

typedef struct extent {
    int right, bottom;
} extent;

// Statements from `id` on, starting at (x, y): how far right and down they go
static extent place_chain(cv *c, int id, int x, int y)
{
    extent e = {x, y};
    const int row_x = x;
    int row_top = y;
    int row_bottom = y;
    while (id) {
        lnode *n = node_by_id(c, id);
        if (!n || n->placed) break;
        int inputs_w = 0;
        for (int i = 0; i < n->inputs.count; i++) {
            const int w = value_width(c, n->inputs.items[i]);
            if (w > inputs_w) inputs_w = w;
        }
        if (x > row_x && x + inputs_w + n->w > row_x + MAX_ROW) { // A new row, below everything so far
            x = row_x;
            row_top = (row_bottom > e.bottom ? row_bottom : e.bottom) + ROW_GAP;
            row_bottom = row_top;
        }
        const int node_x = x + (inputs_w ? inputs_w + NODE_GAP : 0);
        n->placed = true;
        n->x = node_x;
        n->y = row_top;
        // Its values below it, to its left
        int values_h = 0;
        for (int i = 0; i < n->inputs.count; i++) {
            const int h = place_value(c, n->inputs.items[i], node_x - NODE_GAP, row_top + n->h + 16 + values_h);
            values_h += h ? h + 16 : 0;
        }
        int bottom = row_top + n->h + 16 + values_h;
        int right = node_x + n->w;
        // The code it holds, below
        for (int i = 0; i < n->bodies.count; i++) {
            const extent b = place_chain(c, n->bodies.items[i], node_x + 60, bottom + ROW_GAP / 2);
            if (b.bottom > bottom) bottom = b.bottom;
            if (b.right > right) right = b.right;
        }
        if (bottom > row_bottom) row_bottom = bottom;
        if (right > e.right) e.right = right;
        if (row_bottom > e.bottom) e.bottom = row_bottom;
        x = node_x + n->w + NODE_GAP * 2;
        id = n->next;
    }
    return e;
}

static jv *finish_pool(cv *c, const int first_pool, const int root, const bool body)
{
    for (int i = first_pool; i < c->pool.count; i++) measure(c, c->pool.items[i]);
    if (body) {
        place_chain(c, root, 0, 0);
    } else if (root) {
        place_value(c, root, 0, 0);
    }
    // What nothing reached (a for's start and step are its bodies): on a row of their own below
    int stray_x = 0, stray_y = 0;
    for (int i = first_pool; i < c->pool.count; i++) {
        const lnode *n = c->pool.items[i];
        if (n->placed && n->y + n->h > stray_y) stray_y = n->y + n->h;
    }
    stray_y += ROW_GAP;
    jv *g = jv_obj();
    set(g, body ? "entry" : "root", root ? jv_n(root) : jv_new(GJ_NULL));
    jv *nodes = jv_arr();
    for (int i = first_pool; i < c->pool.count; i++) {
        lnode *n = c->pool.items[i];
        if (!n->placed) {
            n->x = stray_x;
            n->y = stray_y;
            stray_x += n->w + NODE_GAP;
        }
        // id, k, x, y first
        jv *o = jv_obj();
        o->one_line = true;
        set(o, "id", n->obj->items.items[0]);
        set(o, "k", n->obj->items.items[1]);
        set(o, "x", jv_n(n->x));
        set(o, "y", jv_n(n->y));
        for (int k = 2; k < n->obj->keys.count; k++) {
            vec_push(o->keys, n->obj->keys.items[k]);
            vec_push(o->items, n->obj->items.items[k]);
        }
        push(nodes, o);
    }
    set(g, "nodes", nodes);
    c->pool.count = first_pool;
    return g;
}

static jv *value_graph(cv *c, const expr *e)
{
    if (!e) return NULL;
    const int first = c->pool.count;
    const int mark = scope_mark(c);
    const int root = ex(c, e);
    c->scope.count = mark;
    return finish_pool(c, first, root, false);
}

static jv *body_graph(cv *c, const stmt *s, const param *params, const int param_count)
{
    if (!s) return NULL;
    const int first = c->pool.count;
    const int mark = scope_mark(c);
    for (int i = 0; i < param_count; i++) {
        if (params[i].name.len) scope_add(c, params[i].name, 0);
    }
    const int entry = chain(c, s);
    c->scope.count = mark;
    // Comments left before the body's end go on the body
    jv *g = finish_pool(c, first, entry, true);
    if (s->kind == S_BLOCK && s->end.line) {
        const str rest = comments_before(c, s->end.line);
        if (rest.len) {
            jv *notes = jv_arr();
            jv *note = jv_obj();
            set(note, "id", jv_n(new_id(c)));
            set(note, "x", jv_n(0));
            set(note, "y", jv_n(-120));
            set(note, "w", jv_n(400));
            set(note, "h", jv_n(80));
            set(note, "text", jv_s(rest));
            push(notes, note);
            set(g, "notes", notes);
        }
    }
    return g;
}

// ---------------------------------------------------------------------------
// Declarations

// What's between an attribute's parentheses, as written; NULL str for none
static str attribute_args(const cv *c, const attribute *a)
{
    int i = offset_of(c, a->at) + a->name.len;
    const char *t = c->src->text;
    const int n = (int)c->src->len;
    while (i < n && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) i++;
    if (i >= n || t[i] != '(') return (str){NULL, 0};
    const int open = i;
    int depth = 0;
    for (; i < n; i++) {
        if (t[i] == '"') {
            for (i++; i < n && t[i] != '"'; i++) {
                if (t[i] == '\\') i++;
            }
            continue;
        }
        if (t[i] == '(') depth++;
        if (t[i] == ')' && --depth == 0) break;
    }
    sb out = {0};
    bool space = false;
    for (int k = open + 1; k < i; k++) {
        const char ch = t[k];
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
            space = true;
            continue;
        }
        if (space && out.len) sb_put(&out, " ");
        space = false;
        sb_putn(&out, &ch, 1);
    }
    return (str){out.data ? out.data : "", (int)out.len};
}

static jv *attributes(cv *c, const attribute *items, const int count)
{
    if (!count) return NULL;
    jv *a = jv_arr();
    for (int i = 0; i < count; i++) {
        jv *o = jv_obj();
        set(o, "id", jv_n(new_id(c)));
        set_s(o, "name", items[i].name);
        const str args = attribute_args(c, &items[i]);
        if (args.ptr) set(o, "args", jv_s(args));
        push(a, o);
    }
    return a;
}

static jv *param_json(cv *c, const param *p)
{
    jv *o = jv_obj();
    o->one_line = true;
    set(o, "id", jv_n(new_id(c)));
    const char *mode = "read";
    switch (p->mode) {
    case PARAM_MUT: mode = p->is_out ? "out" : "mut"; break;
    case PARAM_WITH: mode = "with"; break;
    case PARAM_WITHOUT: mode = "without"; break;
    case PARAM_IN: mode = "in"; break;
    case PARAM_QUERY: mode = "query"; break;
    default: break;
    }
    set(o, "mode", jv_c(mode));
    if (p->mode == PARAM_QUERY) {
        jv *q = jv_arr();
        for (int i = 0; i < p->query.count; i++) push(q, param_json(c, &p->query.items[i]));
        set(o, "query", q);
        set_s(o, "name", p->name);
        return o;
    }
    if (p->ownership) {
        set(o, "ownership", jv_b(true));
        return o;
    }
    set_true(o, "local", p->placement_local);
    set_true(o, "verified", p->verified);
    set_true(o, "previous", p->previous);
    set_true(o, "scene", p->scene);
    if (p->linear_role) set(o, "role", jv_c("Linear"));
    else if (p->sample_role.len) {
        sb role = {0};
        sb_printf(&role, "Raw(" STR_FMT ")", STR_ARG(p->sample_role));
        set(o, "role", jv_s((str){role.data, (int)role.len}));
    } else if (p->raw_role) {
        set(o, "role", jv_c("Raw"));
    }
    set_s(o, "type", type_text(c, p->type_ref, p->type_name));
    set_s(o, "name", p->name);
    if (p->default_value) set(o, "default", value_graph(c, p->default_value));
    return o;
}

static jv *params_json(cv *c, const param *items, const int from, const int count)
{
    jv *a = jv_arr();
    for (int i = from; i < count; i++) push(a, param_json(c, &items[i]));
    return a;
}

static void modifiers(jv *o, const decl *d)
{
    jv *m = jv_arr();
    if (d->is_dev) push(m, jv_c("dev"));
    if (d->is_local) push(m, jv_c("local"));
    if (d->is_async) push(m, jv_c("async"));
    if (d->is_worker) push(m, jv_c("worker"));
    if (d->is_build) push(m, jv_c("build"));
    if (d->is_cached) push(m, jv_c("cached"));
    if (d->is_virtual) push(m, jv_c("virtual"));
    if (d->is_override) push(m, jv_c("override"));
    if (d->is_tweak) push(m, jv_c("tweak"));
    if (m->items.count) set(o, "modifiers", m);
}

static void name_json(jv *o, const decl *d)
{
    set_s(o, "name", d->name);
    bool qualified = false;
    for (int i = 0; i < d->override_name.len; i++) qualified |= d->override_name.ptr[i] == '.';
    if (d->is_override && qualified) set_s(o, "overrides", d->override_name);
}

static void type_params(jv *o, const decl *d)
{
    if (d->type_params.count) {
        jv *a = jv_arr();
        for (int i = 0; i < d->type_params.count; i++) push(a, jv_s(d->type_params.items[i]));
        set(o, "typeParams", a);
    }
    if (d->type_rules.count) {
        jv *where = jv_arr();
        jv *group = NULL;
        str last = {NULL, 0};
        for (int i = 0; i < d->type_rules.count; i++) {
            const type_rule *r = &d->type_rules.items[i];
            if (!group || !str_eq(last, r->param)) {
                group = jv_obj();
                set_s(group, "param", r->param);
                set(group, "rules", jv_arr());
                push(where, group);
                last = r->param;
            }
            push(group->items.items[1], jv_s(r->rule));
        }
        set(o, "where", where);
    }
}

static int decl_line(const decl *d)
{
    int line = d->at.line;
    for (int i = 0; i < d->attributes.count; i++) {
        if (d->attributes.items[i].at.line && d->attributes.items[i].at.line < line) line = d->attributes.items[i].at.line;
    }
    const loc words[] = {d->local_at, d->dev_at, d->async_at, d->worker_at, d->build_at, d->cached_at, d->virtual_at,
                         d->override_at, d->tweak_at};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        if (words[i].line && words[i].line < line) line = words[i].line;
    }
    if (d->return_type_ref && d->return_type_ref->at.line && d->return_type_ref->at.line < line) line = d->return_type_ref->at.line;
    return line;
}

static jv *routine_json(cv *c, const decl *d, const char *kind, const int first_param)
{
    jv *o = jv_obj();
    set(o, "kind", jv_c(kind));
    set(o, "id", jv_n(new_id(c)));
    comment_on(c, o, decl_line(d));
    name_json(o, d);
    modifiers(o, d);
    set(o, "attributes", attributes(c, d->attributes.items, d->attributes.count));
    set_true(o, "mut", d->is_mut_method);
    if (d->return_type_ref || (strcmp(kind, "function") == 0 || strcmp(kind, "method") == 0)) {
        set_s(o, "returnType", type_text(c, d->return_type_ref, d->return_type_name.len ? d->return_type_name : str_from("void")));
    }
    type_params(o, d);
    set(o, "params", params_json(c, d->params.items, first_param, d->params.count));
    if (d->fails_ref || d->fails_name.len) set_s(o, "fails", type_text(c, d->fails_ref, d->fails_name));
    if (d->is_extern || d->extern_system) set(o, "extern", jv_b(true));
    else set(o, "body", body_graph(c, d->body, d->params.items, d->params.count));
    return o;
}

typedef struct member_item {
    int line, col;
    int kind; // 0 field, 1 property, 2 method, 3 tweak, 4 sample, 5 sanitize, 6 predict, 7 prepare
    int index;
} member_item;

static int member_order(const void *a, const void *b)
{
    const member_item *x = a, *y = b;
    if (x->line != y->line) return x->line < y->line ? -1 : 1;
    if (x->col != y->col) return x->col < y->col ? -1 : 1;
    return 0;
}

static str word_at(const cv *c, const loc at)
{
    const int start = offset_of(c, at);
    int end = start;
    const char *t = c->src->text;
    while (end < (int)c->src->len && (t[end] == '_' || (t[end] >= 'a' && t[end] <= 'z') || (t[end] >= 'A' && t[end] <= 'Z')
                                      || (t[end] >= '0' && t[end] <= '9'))) {
        end++;
    }
    return (str){t + start, end - start};
}

static jv *members_json(cv *c, const decl *d)
{
    VEC(member_item) items = {0};
    for (int i = 0; i < d->fields.count; i++) {
        const loc at = d->fields.items[i].type_qual_at.line ? d->fields.items[i].type_qual_at : d->fields.items[i].at;
        const member_item m = {at.line, at.col, 0, i};
        vec_push(items, m);
    }
    for (int i = 0; i < d->properties.count; i++) {
        const member_item m = {d->properties.items[i]->at.line, d->properties.items[i]->at.col, 1, i};
        vec_push(items, m);
    }
    for (int i = 0; i < d->methods.count; i++) {
        if (d->methods.items[i]->accessor) continue;
        const member_item m = {decl_line(d->methods.items[i]), d->methods.items[i]->at.col, 2, i};
        vec_push(items, m);
    }
    for (int i = 0; i < d->generic_methods.count; i++) {
        const member_item m = {decl_line(d->generic_methods.items[i]), d->generic_methods.items[i]->at.col, 8, i};
        vec_push(items, m);
    }
    for (int i = 0; i < d->tweaks.count; i++) {
        const member_item m = {d->tweaks.items[i]->tweak_at.line, d->tweaks.items[i]->tweak_at.col, 3, i};
        vec_push(items, m);
    }
    if (d->kind == DECL_INPUT && d->body) {
        const member_item m = {d->body_at.line, d->body_at.col, 4, 0};
        vec_push(items, m);
    }
    if (d->sanitize) {
        const member_item m = {d->sanitize_at.line, d->sanitize_at.col, 5, 0};
        vec_push(items, m);
    }
    if (d->predict) {
        const member_item m = {d->predict_at.line, d->predict_at.col, 6, 0};
        vec_push(items, m);
    }
    if (d->preparation) {
        const member_item m = {d->preparation->at.line, d->preparation->at.col, 7, 0};
        vec_push(items, m);
    }
    if (items.count) qsort(items.items, (size_t)items.count, sizeof(member_item), member_order);
    jv *a = jv_arr();
    for (int i = 0; i < items.count; i++) {
        const member_item *m = &items.items[i];
        jv *o = NULL;
        switch (m->kind) {
        case 0: {
            const field *f = &d->fields.items[m->index];
            o = jv_obj();
            set(o, "kind", jv_c("field"));
            set(o, "id", jv_n(new_id(c)));
            int line = m->line;
            for (int k = 0; k < f->attributes.count; k++) {
                if (f->attributes.items[k].at.line && f->attributes.items[k].at.line < line) line = f->attributes.items[k].at.line;
            }
            if (f->is_local && f->local_at.line < line) line = f->local_at.line;
            comment_on(c, o, line);
            set_true(o, "local", f->is_local);
            set_s(o, "type", type_text(c, f->type_ref, f->type_name));
            set_s(o, "name", f->name);
            set(o, "attributes", attributes(c, f->attributes.items, f->attributes.count));
            if (f->default_value) set(o, "default", value_graph(c, f->default_value));
            break;
        }
        case 1: {
            const property *p = d->properties.items[m->index];
            o = jv_obj();
            set(o, "kind", jv_c("property"));
            set(o, "id", jv_n(new_id(c)));
            comment_on(c, o, p->type_ref ? p->type_ref->at.line : m->line);
            set_s(o, "type", type_text(c, p->type_ref, (str){NULL, 0}));
            set_s(o, "name", p->name);
            jv *accessors = jv_arr();
            const decl *first = p->get, *second = p->set;
            if (first && second && (second->body_at.line < first->body_at.line
                                    || (second->body_at.line == first->body_at.line && second->body_at.col < first->body_at.col))) {
                first = p->set;
                second = p->get;
            }
            const decl *both[2] = {first, second};
            for (int k = 0; k < 2; k++) {
                if (!both[k]) continue;
                jv *acc = jv_obj();
                set(acc, "id", jv_n(new_id(c)));
                set(acc, "word", jv_c(both[k] == p->set ? "set" : "get"));
                set(acc, "body", body_graph(c, both[k]->body, both[k]->params.items, both[k]->params.count));
                push(accessors, acc);
            }
            set(o, "accessors", accessors);
            break;
        }
        case 2:
        case 8: {
            const decl *md = m->kind == 2 ? d->methods.items[m->index] : d->generic_methods.items[m->index];
            if (md->is_operator) {
                o = jv_obj();
                set(o, "kind", jv_c("operator"));
                set(o, "id", jv_n(new_id(c)));
                comment_on(c, o, decl_line(md));
                set_s(o, "returnType", type_text(c, md->return_type_ref, md->return_type_name));
                set(o, "op", jv_c(op_text(md->op)));
                set(o, "params", params_json(c, md->params.items, 0, md->params.count));
                if (md->fails_ref || md->fails_name.len) set_s(o, "fails", type_text(c, md->fails_ref, md->fails_name));
                set(o, "body", body_graph(c, md->body, md->params.items, md->params.count));
            } else {
                o = routine_json(c, md, "method", 0);
            }
            break;
        }
        case 3: {
            const decl *t = d->tweaks.items[m->index];
            o = jv_obj();
            set(o, "kind", jv_c("tweak"));
            set(o, "id", jv_n(new_id(c)));
            comment_on(c, o, m->line);
            set_s(o, "type", type_text(c, t->return_type_ref, t->return_type_name));
            set_s(o, "name", t->name);
            set(o, "value", value_graph(c, t->value));
            break;
        }
        case 4: {
            o = jv_obj();
            set(o, "kind", jv_c("sample"));
            set(o, "id", jv_n(new_id(c)));
            comment_on(c, o, m->line);
            set_s(o, "name", word_at(c, d->body_at));
            jv *params = jv_arr();
            for (int k = 0; k < d->params.count; k++) {
                jv *p = jv_obj();
                p->one_line = true;
                set(p, "id", jv_n(new_id(c)));
                set_s(p, "type", d->params.items[k].type_name);
                set_s(p, "name", d->params.items[k].name);
                push(params, p);
            }
            set(o, "params", params);
            set(o, "body", body_graph(c, d->body, d->params.items, d->params.count));
            break;
        }
        case 5:
        case 6: {
            o = jv_obj();
            set(o, "kind", jv_c(m->kind == 5 ? "sanitize" : "predict"));
            set(o, "id", jv_n(new_id(c)));
            comment_on(c, o, m->line);
            set(o, "body", body_graph(c, m->kind == 5 ? d->sanitize : d->predict, NULL, 0));
            break;
        }
        case 7: {
            const decl *h = d->preparation;
            o = jv_obj();
            set(o, "kind", jv_c("prepare"));
            set(o, "id", jv_n(new_id(c)));
            comment_on(c, o, m->line);
            set(o, "params", params_json(c, h->params.items, 2, h->params.count));
            set(o, "body", body_graph(c, h->body, h->params.items, h->params.count));
            break;
        }
        default: break;
        }
        if (o) push(a, o);
    }
    return a;
}

static jv *prefab_json(cv *c, const prefab_node *p, const bool root)
{
    jv *o = jv_obj();
    set(o, "id", jv_n(new_id(c)));
    if (!root) {
        set_s(o, "name", p->name);
        set_s(o, "base", p->base);
    }
    jv *components = jv_arr();
    for (int i = 0; i < p->components.count; i++) push(components, value_graph(c, p->components.items[i]));
    set(o, "components", components);
    jv *children = jv_arr();
    for (int i = 0; i < p->children.count; i++) push(children, prefab_json(c, p->children.items[i], false));
    set(o, "children", children);
    return o;
}

static int param_order(const void *a, const void *b)
{
    const param *x = a, *y = b;
    if (x->at.line != y->at.line) return x->at.line < y->at.line ? -1 : 1;
    if (x->at.col != y->at.col) return x->at.col < y->at.col ? -1 : 1;
    return 0;
}

static jv *decl_json(cv *c, const decl *d)
{
    switch (d->kind) {
    case DECL_FUNCTION:
    case DECL_GENERIC:
        return routine_json(c, d, d->is_audio ? "audio" : "function", 0);
    case DECL_SYSTEM: {
        if (d->is_handler && d->is_command) {
            jv *o = jv_obj();
            set(o, "kind", jv_c("command"));
            set(o, "id", jv_n(new_id(c)));
            comment_on(c, o, decl_line(d));
            name_json(o, d);
            modifiers(o, d);
            // Its attributes as written: those of how it's delivered went to its payload's type
            VEC(attribute) attrs = {0};
            const decl *payload_type = d->written_payload;
            for (int i = 0; payload_type && i < payload_type->attributes.count; i++) vec_push(attrs, payload_type->attributes.items[i]);
            for (int i = 0; i < d->attributes.count; i++) vec_push(attrs, d->attributes.items[i]);
            for (int i = 1; i < attrs.count; i++) {
                for (int k = i; k > 0 && (attrs.items[k].at.line < attrs.items[k - 1].at.line
                                          || (attrs.items[k].at.line == attrs.items[k - 1].at.line
                                              && attrs.items[k].at.col < attrs.items[k - 1].at.col)); k--) {
                    const attribute t = attrs.items[k];
                    attrs.items[k] = attrs.items[k - 1];
                    attrs.items[k - 1] = t;
                }
            }
            set(o, "attributes", attributes(c, attrs.items, attrs.count));
            jv *payload = jv_arr();
            const decl *t = d->written_payload;
            for (int i = 0; t && i < t->fields.count; i++) {
                const field *f = &t->fields.items[i];
                jv *p = jv_obj();
                p->one_line = true;
                set(p, "id", jv_n(new_id(c)));
                set(p, "attributes", attributes(c, f->attributes.items, f->attributes.count));
                set_s(p, "type", type_text(c, f->type_ref, f->type_name));
                set_s(p, "name", f->name);
                if (f->default_value) set(p, "default", value_graph(c, f->default_value));
                push(payload, p);
            }
            set(o, "payload", payload);
            // The sender as written, among the rest
            param *rest = arena_alloc(sizeof(param) * (size_t)(d->params.count ? d->params.count : 1));
            int count = 0;
            for (int i = 1; i < d->params.count; i++) rest[count++] = d->params.items[i];
            qsort(rest, (size_t)count, sizeof(param), param_order);
            for (int i = 0; i < count; i++) {
                if (rest[i].mode == PARAM_SENDER) rest[i].mode = PARAM_READ;
            }
            set(o, "params", params_json(c, rest, 0, count));
            set(o, "body", body_graph(c, d->body, d->params.items, d->params.count));
            return o;
        }
        if (d->is_handler) {
            jv *o = jv_obj();
            set(o, "kind", jv_c("handler"));
            set(o, "id", jv_n(new_id(c)));
            comment_on(c, o, decl_line(d));
            name_json(o, d);
            modifiers(o, d);
            set(o, "attributes", attributes(c, d->attributes.items, d->attributes.count));
            const param *t = &d->params.items[0];
            jv *trigger = jv_obj();
            trigger->one_line = true;
            set_true(trigger, "verified", t->verified);
            set_true(trigger, "changed", t->changed);
            set_s(trigger, "type", t->type_name);
            set_s(trigger, "name", t->name);
            set(o, "trigger", trigger);
            set(o, "params", params_json(c, d->params.items, 1, d->params.count));
            set(o, "body", body_graph(c, d->body, d->params.items, d->params.count));
            return o;
        }
        jv *o = routine_json(c, d, d->is_view ? "view" : "system", 0);
        return o;
    }
    case DECL_SHADER:
        return routine_json(c, d, d->is_compute ? "compute" : d->is_frag ? "frag" : "vert", 0);
    case DECL_COMPONENT:
    case DECL_SINGLETON:
    case DECL_STRUCT:
    case DECL_GENERIC_STRUCT:
    case DECL_EVENT:
    case DECL_INPUT: {
        jv *o = jv_obj();
        const char *kind = d->kind == DECL_COMPONENT ? (d->is_scene ? "scene" : "component")
                           : d->kind == DECL_SINGLETON ? "singleton"
                           : d->kind == DECL_EVENT ? "event"
                           : d->kind == DECL_INPUT ? "input" : "struct";
        set(o, "kind", jv_c(kind));
        set(o, "id", jv_n(new_id(c)));
        comment_on(c, o, decl_line(d));
        set_s(o, "name", d->name);
        modifiers(o, d);
        set(o, "attributes", attributes(c, d->attributes.items, d->attributes.count));
        type_params(o, d);
        // `scene Arena;`: ends where it starts, with no braces
        const bool bodyless = d->is_scene && d->end.line && offset_of(c, d->end) < (int)c->src->len
                              && c->src->text[offset_of(c, d->end)] == ';';
        if (bodyless) set(o, "members", jv_new(GJ_NULL));
        else set(o, "members", members_json(c, d));
        return o;
    }
    case DECL_ENUM: {
        jv *o = jv_obj();
        set(o, "kind", jv_c("enum"));
        set(o, "id", jv_n(new_id(c)));
        comment_on(c, o, decl_line(d));
        set_s(o, "name", d->name);
        modifiers(o, d);
        set(o, "attributes", attributes(c, d->attributes.items, d->attributes.count));
        if (d->backing_ref || d->backing.len) set_s(o, "backing", type_text(c, d->backing_ref, d->backing));
        jv *members = jv_arr();
        for (int i = 0; i < d->members.count; i++) {
            const enum_member *m = &d->members.items[i];
            jv *e = jv_obj();
            set(e, "id", jv_n(new_id(c)));
            comment_on(c, e, m->at.line);
            set_s(e, "name", m->name);
            if (m->value) set(e, "value", value_graph(c, m->value));
            push(members, e);
        }
        set(o, "members", members);
        return o;
    }
    case DECL_CONST:
    case DECL_ASSET: {
        jv *o = jv_obj();
        set(o, "kind", jv_c(d->kind == DECL_CONST ? "const" : "asset"));
        set(o, "id", jv_n(new_id(c)));
        comment_on(c, o, decl_line(d));
        set_s(o, "name", d->name);
        modifiers(o, d);
        set(o, "attributes", attributes(c, d->attributes.items, d->attributes.count));
        set_s(o, "type", type_text(c, d->return_type_ref, d->return_type_name));
        set(o, "value", value_graph(c, d->value));
        return o;
    }
    case DECL_ALIAS: {
        jv *o = jv_obj();
        set(o, "kind", jv_c("alias"));
        set(o, "id", jv_n(new_id(c)));
        comment_on(c, o, decl_line(d));
        set_s(o, "name", d->name);
        set(o, "attributes", attributes(c, d->attributes.items, d->attributes.count));
        set_s(o, "type", type_text(c, d->alias_ref, (str){NULL, 0}));
        return o;
    }
    case DECL_SETTINGS: {
        jv *o = jv_obj();
        set(o, "kind", jv_c("settings"));
        set(o, "id", jv_n(new_id(c)));
        comment_on(c, o, d->at.line);
        jv *entries = jv_arr();
        for (int i = 0; i < d->fields.count; i++) {
            jv *e = jv_obj();
            set(e, "id", jv_n(new_id(c)));
            comment_on(c, e, d->fields.items[i].at.line);
            set_s(e, "name", d->fields.items[i].name);
            set(e, "value", value_graph(c, d->fields.items[i].default_value));
            push(entries, e);
        }
        set(o, "entries", entries);
        return o;
    }
    case DECL_PREFAB: {
        jv *o = jv_obj();
        set(o, "kind", jv_c("prefab"));
        set(o, "id", jv_n(new_id(c)));
        comment_on(c, o, decl_line(d));
        name_json(o, d);
        modifiers(o, d);
        set(o, "attributes", attributes(c, d->attributes.items, d->attributes.count));
        set(o, "root", prefab_json(c, d->prefab_root, true));
        return o;
    }
    default:
        cant(c, "a declaration of a kind the converter doesn't know");
        return NULL;
    }
}

// ---------------------------------------------------------------------------
// The file

typedef struct top_item {
    const decl *d;
    int line, col;
} top_item;

static int top_order(const void *a, const void *b)
{
    const top_item *x = a, *y = b;
    if (x->line != y->line) return x->line < y->line ? -1 : 1;
    if (x->col != y->col) return x->col < y->col ? -1 : 1;
    return 0;
}

// Whether the parser made a declaration of another's, which that one writes: a command's payload, a type's tweak,
// a scene's preparation
static bool made_by_another(const decl *d)
{
    return (d->kind == DECL_EVENT && d->is_command) || d->tweak_owner || d->preparation_owner;
}

static jv *text_decl(cv *c, const str text)
{
    jv *o = jv_obj();
    set(o, "kind", jv_c("text"));
    set(o, "id", jv_n(new_id(c)));
    set(o, "text", jv_s(text));
    return o;
}

// The unit chain from the file down to `u`
static int unit_depth(const unit *u)
{
    int depth = 0;
    for (; u && u->outer; u = u->outer) depth++;
    return depth;
}

static const unit *unit_at_depth(const unit *u, const int depth)
{
    while (unit_depth(u) > depth) u = u->outer;
    return u;
}

// The last line a declaration's text is on
static int decl_end_line(const decl *d)
{
    int line = d->at.line;
    if (d->end.line > line) line = d->end.line;
    if (d->body && d->body->end.line > line) line = d->body->end.line;
    return line;
}

static void quiet_sink(void *user, const diag_severity severity, const loc at, const char *message)
{
    (void)user;
    (void)severity;
    (void)at;
    (void)message;
}

// The declarations of a parsed file, in the order written, each namespace block's in it, into `list`
static void convert_decls(cv *c, const program *prog, jv *list, const int from_line, const int to_line)
{
    const unit *file_unit = prog->units.count ? prog->units.items[0] : NULL;
    VEC(top_item) items = {0};
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (made_by_another(d)) continue;
        const top_item t = {d, decl_line(d), d->at.col};
        if (t.line >= from_line && t.line <= to_line) vec_push(items, t);
    }
    for (int i = 0; i < prog->settings.count; i++) {
        const decl *d = prog->settings.items[i];
        const top_item t = {d, d->at.line, d->at.col};
        if (t.line >= from_line && t.line <= to_line) vec_push(items, t);
    }
    if (items.count) qsort(items.items, (size_t)items.count, sizeof(top_item), top_order);
    VEC(jv *) open_lists = {0};
    VEC(const unit *) open_units = {0};
    vec_push(open_lists, list);
    vec_push(open_units, file_unit);
    for (int i = 0; i < items.count && !c->failed; i++) {
        const decl *d = items.items[i].d;
        const unit *u = d->unit ? d->unit : file_unit;
        const int depth = unit_depth(u);
        // Close blocks it isn't in, then open the ones it is
        while (open_units.count - 1 > depth || unit_at_depth(u, open_units.count - 1) != open_units.items[open_units.count - 1]) {
            open_units.count--;
            open_lists.count--;
        }
        while (open_units.count - 1 < depth) {
            const unit *block = unit_at_depth(u, open_units.count);
            jv *ns = jv_obj();
            set(ns, "kind", jv_c("namespace"));
            set(ns, "id", jv_n(new_id(c)));
            comment_on(c, ns, block->block_at.line);
            set_s(ns, "name", block->ns_written);
            jv *inner = jv_arr();
            set(ns, "decls", inner);
            push(open_lists.items[open_lists.count - 1], ns);
            vec_push(open_lists, inner);
            vec_push(open_units, block);
        }
        jv *o = decl_json(c, d);
        if (o) push(open_lists.items[open_lists.count - 1], o);
    }
}

static void line_starts_of(cv *c)
{
    const int zero = 0;
    vec_push(c->line_starts, zero);
    for (size_t i = 0; i < c->src->len; i++) {
        if (c->src->text[i] == '\n') {
            const int next = (int)i + 1;
            vec_push(c->line_starts, next);
        }
    }
}

// Lines `first` to `last` of a file, as written
static str lines_text(const cv *c, const int first, const int last)
{
    const int a = offset_of(c, (loc){first, 1, 0});
    int b = last < c->line_starts.count ? c->line_starts.items[last] : (int)c->src->len;
    while (b > a && (c->src->text[b - 1] == '\n' || c->src->text[b - 1] == '\r')) b--;
    return (str){c->src->text + a, b - a};
}

// The lines an #if leaves out, `first` to `last`, as declarations of their own: as a graph where they parse
// alone, as text where they don't (code for another tide, say)
static void convert_left_out(cv *c, const int first, const int last, jv *list)
{
    const str text = lines_text(c, first, last);
    bool directive = false; // An #if inside what's left out: read as text, as the lexer would read its lines
    for (int i = 0; i < text.len; i++) {
        if (i == 0 || text.ptr[i - 1] == '\n') {
            int k = i;
            while (k < text.len && (text.ptr[k] == ' ' || text.ptr[k] == '\t')) k++;
            if (k < text.len && text.ptr[k] == '#') directive = true;
        }
    }
    if (!directive) {
        // The same lines in a file of their own, the others blank, so their places are the file's
        source *alone = NEW(source);
        alone->path = c->src->path;
        char *copy = arena_alloc(c->src->len + 1);
        for (size_t i = 0; i < c->src->len; i++) copy[i] = c->src->text[i] == '\n' ? '\n' : ' ';
        const int a = offset_of(c, (loc){first, 1, 0});
        memcpy(copy + a, text.ptr, (size_t)text.len);
        alone->text = copy;
        alone->len = c->src->len;
        diag_add_source(alone);
        const int errors = diag_error_count();
        diag_set_sink(quiet_sink, NULL);
        token *toks = lex(alone);
        program *prog = toks ? program_new() : NULL;
        const bool parsed = prog && parse_file(prog, alone, toks, false) && diag_error_count() == errors;
        diag_set_sink(NULL, NULL);
        bool blocks = false;
        for (int i = 0; parsed && i < prog->units.count; i++) blocks |= prog->units.items[i]->outer != NULL;
        if (parsed && !blocks && !(prog->units.count && (prog->units.items[0]->ns.len || prog->units.items[0]->usings.count))) {
            cv sub = {.src = alone, .next_id = c->next_id};
            line_starts_of(&sub);
            find_comments(&sub);
            convert_decls(&sub, prog, list, first, last);
            const str rest = comments_before(&sub, 1 << 30);
            c->next_id = sub.next_id;
            if (!sub.failed) {
                if (rest.len) {
                    sb lines = {0};
                    for (int i = 0; i < rest.len; i++) {
                        if (i == 0 || rest.ptr[i - 1] == '\n') sb_put(&lines, "// ");
                        sb_putn(&lines, rest.ptr + i, 1);
                    }
                    push(list, text_decl(c, (str){lines.data, (int)lines.len}));
                }
                return;
            }
            list->items.count = 0; // What it made goes: the lines are text, as written
        }
    }
    push(list, text_decl(c, text));
}

bool graph_from_tide(const char *path, sb *out, const char **why)
{
    source *src = NEW(source);
    src->path = path;
    src->text = graph_read_file(path, &src->len);
    if (!src->text) {
        fprintf(stderr, "tidec: can't read '%s'; check that it exists and is readable\n", path);
        return false;
    }
    diag_add_source(src);
    cv c = {.src = src};
    line_starts_of(&c);
    jv *root = jv_obj();
    set(root, "tidegraph", jv_n(1));
    set(root, "nextId", jv_n(0)); // Set at the end
    jv *list = jv_arr();
    lex_spans spans = {0};
    token *all = lex_all_spans(src, &spans);
    token *toks = all ? lex(src) : NULL;
    program *prog = toks ? program_new() : NULL;
    const bool parsed = prog && parse_file(prog, src, toks, false);
    if (!parsed) {
        if (why) *why = "it doesn't parse";
        return false;
    }
    // #if and the lines it leaves out between declarations are their own declarations: the directive's lines as
    // text, the lines left out converted on their own. A file whose #if is inside a declaration or a namespace
    // block, or that #defines, is one text declaration: as written, which the editor shows as text
    bool whole_text = false;
    for (int s = 0; s < spans.count && !whole_text; s++) {
        const lex_span *span = &spans.items[s];
        for (int i = 0; i < prog->decls.count && !whole_text; i++) {
            const decl *d = prog->decls.items[i];
            if (made_by_another(d)) continue;
            whole_text = span->first <= decl_end_line(d) && span->last >= decl_line(d);
        }
        for (int i = 0; i < prog->settings.count && !whole_text; i++) {
            const decl *d = prog->settings.items[i];
            whole_text = span->first <= decl_end_line(d) && span->last >= d->at.line;
        }
        if (span->directive) {
            const str text = lines_text(&c, span->first, span->last);
            for (int i = 0; i + 6 < text.len && !whole_text; i++) {
                whole_text = text.ptr[i] == '#' && (strncmp(text.ptr + i, "#define", 7) == 0 || strncmp(text.ptr + i, "#undef", 6) == 0);
            }
        }
    }
    for (int i = 0; spans.count && i < prog->units.count && !whole_text; i++) whole_text = prog->units.items[i]->outer != NULL;
    // Before the namespace or using lines, which the graph writes first
    const unit *file_unit = prog->units.count ? prog->units.items[0] : NULL;
    if (spans.count && file_unit && (file_unit->ns.len || file_unit->usings.count)) {
        const int header = file_unit->usings.count ? file_unit->using_at.items[file_unit->usings.count - 1].line
                                                   : file_unit->ns_at.line;
        whole_text |= spans.items[0].first <= header;
    }
    if (whole_text) {
        push(list, text_decl(&c, (str){src->text, (int)src->len}));
        set(root, "decls", list);
        root->items.items[1] = jv_n(c.next_id + 1);
        print_jv(out, root, 0, false);
        sb_put(out, "\n");
        return true;
    }
    find_comments(&c);
    // Comments on lines an #if leaves out are theirs
    int kept = 0;
    for (int i = 0; i < c.comments.count; i++) {
        bool out_of_code = false;
        for (int s = 0; s < spans.count && !out_of_code; s++) {
            out_of_code = c.comments.items[i].line >= spans.items[s].first && c.comments.items[i].line <= spans.items[s].last;
        }
        if (!out_of_code) c.comments.items[kept++] = c.comments.items[i];
    }
    c.comments.count = kept;
    if (file_unit && file_unit->ns.len) set_s(root, "namespace", file_unit->ns);
    if (file_unit && file_unit->usings.count) {
        jv *usings = jv_arr();
        for (int i = 0; i < file_unit->usings.count; i++) push(usings, jv_s(file_unit->usings.items[i]));
        set(root, "using", usings);
    }
    // The code between the #if lines, and the lines themselves, in order
    int from = 1;
    for (int s = 0; s <= spans.count && !c.failed; s++) {
        const int to = s < spans.count ? spans.items[s].first - 1 : 1 << 30;
        convert_decls(&c, prog, list, from, to);
        if (s == spans.count) break;
        const lex_span *span = &spans.items[s];
        if (span->directive) {
            for (int line = span->first; line <= span->last; line++) push(list, text_decl(&c, lines_text(&c, line, line)));
        } else {
            jv *left_out = jv_arr();
            convert_left_out(&c, span->first, span->last, left_out);
            for (int i = 0; i < left_out->items.count; i++) push(list, left_out->items.items[i]);
        }
        from = span->last + 1;
    }
    if (c.failed) {
        if (why) *why = c.why;
        return false;
    }
    // Comments after the last declaration
    const str rest = comments_before(&c, 1 << 30);
    if (rest.len) {
        sb text = {0};
        for (int i = 0; i < rest.len; i++) {
            if (i == 0 || rest.ptr[i - 1] == '\n') sb_put(&text, "// ");
            sb_putn(&text, rest.ptr + i, 1);
        }
        push(list, text_decl(&c, (str){text.data, (int)text.len}));
    }
    set(root, "decls", list);
    root->items.items[1] = jv_n(c.next_id + 1);
    print_jv(out, root, 0, false);
    sb_put(out, "\n");
    return true;
}
