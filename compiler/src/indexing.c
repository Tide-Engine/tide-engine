// Indexed loops: what a loop's guard says about the rows it can run for
// (indexing.h, notes/indexed-loops.md).
//
// A loop over a query's rows or a list's elements is looked at from the start
// of its body. Statements that do nothing anyone can see (read-only locals of
// pure values) are passed over, and each `if (C) continue;`, or a last
// `if (C) { ... }` holding the rest, is a guard: a row that fails it does
// nothing at all. What the guards say together is turned into bounds on
// numbers of the row, each bound worked out before the loop from values the
// loop doesn't change. The bounds only ever take in more rows than can pass,
// never fewer: the guard still runs for every row the index gives.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "indexing.h"
#include "types.h"

#define MOST_CONSTRAINTS 16
#define MOST_ENV 128
#define MOST_INLINE 4

typedef struct constraint {
    const expr *path;
    int comp;
    type type;
    const bound *lo;
    const bound *hi;
} constraint;

typedef struct constraints {
    constraint items[MOST_CONSTRAINTS];
    int count;
} constraints;

typedef struct ictx {
    const program *prog;
    const decl *code;  // The system, view, handler, function or method holding the loop
    const stmt *loop;
    bool list_steps;   // A parallel loop over a list: its variable is an index, and its rows are list[i]
    VEC(const expr *) written;  // What the loop changes: places rooted at locals and parameters
    VEC(const expr *) written_local; // ...and places of which it changes the local fields alone (changes_local_fields_alone)
    VEC(const decl *) world_sets; // Components the loop sets through a World
    bool world_sets_any;          // ...or a call that may set any
    VEC(const stmt *) inner;      // Locals the loop's body declares
    const void *env_from[MOST_ENV]; // A read-only local or a parameter of an inlined function...
    const expr *env_to[MOST_ENV];   // ...and what it stands for
    int env_count;
    const decl *inlining[MOST_INLINE];
    int inline_depth;
    // Conversions seen past (a name or an inlined call standing for a value of another type): a count, its value
    // as the comparison being read began, and how many conditions being read went through one
    uint32_t converted, converted_mark;
    int converting;
    const char *why;
    loc why_at;
} ictx;

static void because(ictx *c, const loc at, const char *fmt, ...)
{
    if (c->why) return;
    va_list args;
    va_start(args, fmt);
    char buf[512];
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    char *text = arena_alloc(strlen(buf) + 1);
    memcpy(text, buf, strlen(buf));
    c->why = text;
    c->why_at = at;
}

static bool same_kind(const type a, const type b)
{
    return a.kind == b.kind && a.decl == b.decl;
}

static bool is_scalar_key(const type t)
{
    if (t.kind == TY_ENUM || t.kind == TY_BOOL || t.kind == TY_CHAR || t.kind == TY_LONG) return true;
    return type_dim(t) == 1 && (type_is_float_based(t) || type_is_double_based(t) || type_is_integer(t));
}

// Whether values of the type are whole numbers a 64-bit integer holds as they are, compared as integers: an
// equality of two of them is one of int64_ts (Tide has no unsigned integer but byte, which promotes exactly)
static bool is_exact_type(const type t)
{
    return t.kind == TY_BYTE || t.kind == TY_INT || t.kind == TY_LONG || t.kind == TY_CHAR || t.kind == TY_ENUM
        || t.kind == TY_BOOL;
}

static bool is_real(const type t) // float or double, scalar or vector: arithmetic on it rounds, never wraps
{
    return type_is_float_based(t) || type_is_double_based(t);
}

static bool is_real_vector(const type t)
{
    const int dims = type_dim(t);
    return dims >= 2 && dims <= 3 && is_real(t);
}

static bool is_comparison(const tok_kind op)
{
    return op == T_LT || op == T_LE || op == T_GT || op == T_GE || op == T_EQ || op == T_NE;
}

static tok_kind negated(const tok_kind op)
{
    switch (op) {
    case T_LT: return T_GE;
    case T_LE: return T_GT;
    case T_GT: return T_LE;
    case T_GE: return T_LT;
    case T_EQ: return T_NE;
    default: return T_EQ;
    }
}

static tok_kind flipped(const tok_kind op) // a OP b as b OP' a
{
    switch (op) {
    case T_LT: return T_GT;
    case T_LE: return T_GE;
    case T_GT: return T_LT;
    case T_GE: return T_LE;
    default: return op;
    }
}

// ---------------------------------------------------------------------------
// Names that stand for something: the guard's read-only locals, and an
// inlined function's parameters

static const void *name_target(const expr *e)
{
    if (e->kind != E_NAME) return NULL;
    if (e->bind == BIND_LOCAL) return e->local;
    if (e->bind == BIND_PARAM) return e->param;
    return NULL;
}

static const expr *env_find(ictx *c, const expr *e)
{
    const void *target = name_target(e);
    if (!target) return NULL;
    for (int i = c->env_count - 1; i >= 0; i--) {
        if (c->env_from[i] != target) continue;
        // `float f = row.n;`: the name's a float, what it stands for an int, and C compares f as a float
        if (!same_kind(e->type, c->env_to[i]->type)) c->converted++;
        return c->env_to[i];
    }
    return NULL;
}

static bool env_push(ictx *c, const void *from, const expr *to)
{
    if (c->env_count == MOST_ENV) return false;
    c->env_from[c->env_count] = from;
    c->env_to[c->env_count] = to;
    c->env_count++;
    return true;
}

static const expr *subst(ictx *c, const expr *e);

static bool math_call(const expr *e, const char *name)
{
    if (e->kind != E_METHOD || e->call != CALL_BUILTIN || !e->object) return false;
    const expr *owner = e->object;
    return owner->kind == E_NAME && str_eq_c(owner->name, "Math") && (!name || str_eq_c(e->name, name));
}

static bool pure(ictx *c, const expr *e);

// A function the analysis can see through: read-only locals of pure values, then a return.
static const expr *inlinable(ictx *c, const expr *call)
{
    if (call->kind != E_CALL || call->call != CALL_FUNCTION || !call->method) return NULL;
    const decl *d = call->method;
    if (d->kind != DECL_FUNCTION || d->is_extern || d->is_async || d->is_worker || d->fails.kind != TY_VOID) return NULL;
    if (!d->body || d->body->kind != S_BLOCK || d->body->stmts.count == 0) return NULL;
    if (call->args.count != d->params.count) return NULL;
    for (int i = 0; i < c->inline_depth; i++) {
        if (c->inlining[i] == d) return NULL; // Recursion
    }
    if (c->inline_depth == MOST_INLINE) return NULL;
    for (int i = 0; i < d->params.count; i++) {
        const param *p = &d->params.items[i];
        if (p->mode != PARAM_READ || p->is_out || !same_kind(p->type, call->args.items[i]->type)) return NULL;
    }
    const stmt *last = d->body->stmts.items[d->body->stmts.count - 1];
    if (last->kind != S_RETURN || !last->value) return NULL;
    for (int i = 0; i + 1 < d->body->stmts.count; i++) {
        const stmt *s = d->body->stmts.items[i];
        if (s->kind != S_VAR || s->is_mut || !s->value || s->out_decl || s->is_shared) return NULL;
    }
    return last->value;
}

// A call of an inlinable function, as its returned value with the arguments
// in place of the parameters (a copy), or NULL.
static const expr *inline_call(ictx *c, const expr *call)
{
    const expr *value = inlinable(c, call);
    if (!value) return NULL;
    const decl *d = call->method;
    const int mark = c->env_count;
    bool ok = true;
    for (int i = 0; ok && i < d->params.count; i++) {
        ok = env_push(c, &d->params.items[i], call->args.items[i]);
    }
    c->inlining[c->inline_depth++] = d;
    for (int i = 0; ok && i + 1 < d->body->stmts.count; i++) {
        const stmt *s = d->body->stmts.items[i];
        ok = pure(c, s->value) && env_push(c, s, s->value);
    }
    const expr *out = ok && pure(c, value) ? subst(c, value) : NULL;
    c->inline_depth--;
    c->env_count = mark;
    if (out && !same_kind(call->type, out->type)) c->converted++; // `float AsF(int x) { return x; }`
    return out;
}

// What an expression stands for: through the guard's locals and calls of
// inlinable functions, without copying anything else.
static const expr *look(ictx *c, const expr *e)
{
    for (int guard = 0; guard < 64 && e; guard++) {
        const expr *to = env_find(c, e);
        if (to) {
            e = to;
            continue;
        }
        if (e->kind == E_CALL && e->call == CALL_FUNCTION) {
            const expr *inlined = inline_call(c, e);
            if (inlined) {
                e = inlined;
                continue;
            }
        }
        break;
    }
    return e;
}

static expr *copy_of(const expr *e)
{
    expr *out = arena_alloc(sizeof *out);
    *out = *e;
    return out;
}

// The expression with the names standing for something replaced by what
// they stand for, copied only where something changed; NULL for an
// expression the analysis doesn't copy.
static const expr *subst(ictx *c, const expr *e)
{
    if (!e) return NULL;
    const expr *to = env_find(c, e);
    if (to) return subst(c, to);
    switch (e->kind) {
    case E_INT:
    case E_FLOAT:
    case E_BOOL:
    case E_THIS:
    case E_DEFAULT:
    case E_NAME:
        return e;
    case E_MEMBER: {
        if (e->property) return NULL; // A getter is code of its own
        if (!e->object) return e;     // An enum member or a static member
        const expr *object = subst(c, e->object);
        if (!object) return NULL;
        if (object == e->object) return e;
        expr *out = copy_of(e);
        out->object = (expr *)object;
        return out;
    }
    case E_UNARY: {
        const expr *lhs = subst(c, e->lhs);
        if (!lhs) return NULL;
        if (lhs == e->lhs) return e;
        expr *out = copy_of(e);
        out->lhs = (expr *)lhs;
        return out;
    }
    case E_BINARY:
    case E_INDEX: {
        const expr *lhs = subst(c, e->lhs);
        const expr *rhs = e->kind == E_BINARY ? subst(c, e->rhs) : NULL;
        const expr *object = e->kind == E_INDEX ? subst(c, e->object) : NULL;
        if (!lhs || (e->kind == E_BINARY && !rhs) || (e->kind == E_INDEX && !object)) return NULL;
        if (lhs == e->lhs && rhs == e->rhs && object == e->object) return e;
        expr *out = copy_of(e);
        out->lhs = (expr *)lhs;
        if (e->kind == E_BINARY) out->rhs = (expr *)rhs;
        else out->object = (expr *)object;
        return out;
    }
    case E_CONDITIONAL: {
        const expr *cond = subst(c, e->cond), *lhs = subst(c, e->lhs), *rhs = subst(c, e->rhs);
        if (!cond || !lhs || !rhs) return NULL;
        if (cond == e->cond && lhs == e->lhs && rhs == e->rhs) return e;
        expr *out = copy_of(e);
        out->cond = (expr *)cond;
        out->lhs = (expr *)lhs;
        out->rhs = (expr *)rhs;
        return out;
    }
    case E_CALL:
    case E_METHOD: {
        if (e->kind == E_CALL && e->call == CALL_FUNCTION) {
            const expr *inlined = inline_call(c, e);
            return inlined;
        }
        if (e->call != CALL_CONSTRUCT && !math_call(e, NULL)) return NULL;
        if (e->block) return NULL;
        bool same = true;
        const expr **args = e->args.count ? arena_alloc((size_t)e->args.count * sizeof *args) : NULL;
        for (int i = 0; i < e->args.count; i++) {
            args[i] = subst(c, e->args.items[i]);
            if (!args[i]) return NULL;
            same &= args[i] == e->args.items[i];
        }
        if (same) return e;
        expr *out = copy_of(e);
        memset(&out->args, 0, sizeof out->args);
        for (int i = 0; i < e->args.count; i++) vec_push(out->args, (expr *)args[i]);
        return out;
    }
    default:
        return NULL;
    }
}

// Whether evaluating the expression can do nothing anyone sees, so a row
// that never evaluates it is the same as one that does.
static bool pure(ictx *c, const expr *e)
{
    if (!e) return true;
    switch (e->kind) {
    case E_INT:
    case E_FLOAT:
    case E_BOOL:
    case E_THIS:
    case E_DEFAULT:
    case E_NULL:
    case E_NAME:
    case E_HAS:
        return true;
    case E_MEMBER:
        return !e->property && pure(c, e->object);
    case E_UNARY:
        return !e->method && pure(c, e->lhs);
    case E_BINARY:
        return !e->method && pure(c, e->lhs) && pure(c, e->rhs);
    case E_INDEX:
        return pure(c, e->object) && pure(c, e->lhs);
    case E_CONDITIONAL:
        return pure(c, e->cond) && pure(c, e->lhs) && pure(c, e->rhs);
    case E_CALL:
    case E_METHOD: {
        if (e->block) return false;
        bool ok = e->call == CALL_CONSTRUCT || math_call(e, NULL)
               || (e->call == CALL_WORLD && !str_eq_c(e->name, "Set"));
        if (!ok && e->call == CALL_FUNCTION) ok = inlinable(c, e) != NULL;
        for (int i = 0; ok && i < e->args.count; i++) ok = pure(c, e->args.items[i]);
        return ok;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// What the loop changes

static const expr *place_root(const expr *e)
{
    while (e && (e->kind == E_MEMBER || e->kind == E_INDEX) && e->object) e = e->object;
    return e;
}

static void note_written(ictx *c, const expr *target)
{
    const expr *root = place_root(target);
    if (root && name_target(root)) vec_push(c->written, target);
}

// A place as the steps from its variable: each a field, a component (by its
// swizzle) or, for an element of a list, any element.
typedef struct place_step {
    const void *field;
    int swizzle; // -1: none; 0-3 one component; 4: several, which overlap any
    bool any;
} place_step;

static int place_steps(const expr *e, place_step *steps, const int most)
{
    int n = 0;
    place_step reversed[16];
    while (e && (e->kind == E_MEMBER || e->kind == E_INDEX) && e->object && n < 16) {
        if (e->kind == E_INDEX) reversed[n++] = (place_step){NULL, -1, true};
        else if (e->swizzle_len) reversed[n++] = (place_step){NULL, e->swizzle_len == 1 ? e->swizzle[0] : 4, false};
        else reversed[n++] = (place_step){e->field, -1, !e->field};
        e = e->object;
    }
    const int count = n < most ? n : most;
    for (int i = 0; i < count; i++) steps[i] = reversed[n - 1 - i];
    return count;
}

// Whether two places of one variable can share any memory: one holds the other.
static bool places_overlap(const expr *a, const expr *b)
{
    place_step x[16], y[16];
    const int nx = place_steps(a, x, 16), ny = place_steps(b, y, 16);
    for (int i = 0; i < nx && i < ny; i++) {
        if (x[i].any || y[i].any || x[i].swizzle == 4 || y[i].swizzle == 4) return true;
        if (x[i].field != y[i].field || x[i].swizzle != y[i].swizzle) return false;
    }
    return true;
}

// Whether a place is read through a local field of a match component or singleton: `chunk.faces[i]`.
static bool through_local_field(const expr *read)
{
    for (const expr *e = read; e && (e->kind == E_MEMBER || e->kind == E_INDEX); e = e->object) {
        if (e->kind == E_MEMBER && e->local_field) return true;
    }
    return false;
}

// Whether the loop changes anything of the variable (a local or a parameter) that `read` reads; `read` NULL: anything of it.
static bool loop_changes(const ictx *c, const void *variable, const expr *read)
{
    for (int i = 0; i < c->written.count; i++) {
        const expr *target = c->written.items[i];
        if (name_target(place_root(target)) != variable) continue;
        if (!read || places_overlap(target, read)) return true;
    }
    // A component handed on to change its local fields alone changes what's read through them (a loop over a local
    // field's list), never its match fields
    for (int i = 0; i < c->written_local.count; i++) {
        if (name_target(place_root(c->written_local.items[i])) != variable) continue;
        if (!read || through_local_field(read)) return true;
    }
    return false;
}

// Whether a call's `mut` parameter changes the local fields alone of the match component or singleton it's given:
// one with local fields whose function (a task's among them) writes none of its match fields, itself or through
// what it hands it to (param.match_write, spread_local_field_use). Local code hands its own to no other (a view
// can't change the match: check_local_field_calls), and a local task writes those alone. No key or bound is a local
// field (row_path, invariant_read), so such a call changes nothing a query's loop finds rows by; a loop over a list
// that's a local field of it is another matter (loop_changes, written_local). C writes what it's handed, and what it
// writes isn't the compiler's to know: an extern's parameter, and one its function hands on to C itself or through
// others (param.c_passed, settle_c_lends), changes the whole of it.
static bool changes_local_fields_alone(const decl *d, const param *q)
{
    return q->mode == PARAM_MUT && !q->is_out && (q->type.kind == TY_COMPONENT || q->type.kind == TY_SINGLETON)
        && q->type.decl && !q->type.decl->is_local && q->type.decl->local_part && !q->match_write
        && !d->is_extern && !q->c_passed;
}

static void note_world_sets(ictx *c, const decl *d)
{
    for (int i = 0; i < d->world_uses.count; i++) {
        if (d->world_uses.items[i].writes) vec_push(c->world_sets, d->world_uses.items[i].component);
    }
}

static void walk_writes_stmt(ictx *c, const stmt *s);
static void walk_writes_expr(ictx *c, const expr *e)
{
    if (!e) return;
    if (e->kind == E_STEP) note_written(c, e->lhs);
    if ((e->call == CALL_FUNCTION || e->call == CALL_METHOD) && e->method) {
        const decl *d = e->method;
        for (int i = 0; i < d->params.count && i < e->args.count; i++) {
            const param *q = &d->params.items[i];
            const expr *arg = e->args.items[i];
            if (changes_local_fields_alone(d, q)) {
                const expr *root = place_root(arg);
                if (root && name_target(root)) vec_push(c->written_local, arg);
            } else if (q->mode == PARAM_MUT || q->is_out) {
                note_written(c, arg);
            }
            if (arg->type.kind == TY_WORLD) note_world_sets(c, d);
        }
        if (e->call == CALL_METHOD && d->is_mut_method) note_written(c, e->object);
    }
    if (e->call == CALL_LIST || e->call == CALL_GUI) note_written(c, e->object);
    if (e->call == CALL_WORLD && (str_eq_c(e->name, "Set") || str_eq_c(e->name, "Edit"))) {
        if (e->type_decl) vec_push(c->world_sets, e->type_decl);
        else c->world_sets_any = true;
    }
    walk_writes_expr(c, e->object);
    walk_writes_expr(c, e->lhs);
    walk_writes_expr(c, e->rhs);
    walk_writes_expr(c, e->cond);
    for (int i = 0; i < e->args.count; i++) walk_writes_expr(c, e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) walk_writes_expr(c, e->inits.items[i].value);
    walk_writes_stmt(c, e->block);
    walk_writes_stmt(c, e->step);
}

static void walk_writes_stmt(ictx *c, const stmt *s)
{
    if (!s) return;
    if (s->kind == S_ASSIGN) note_written(c, s->target);
    if (s->kind == S_VAR || s->kind == S_FOREACH || s->kind == S_PARALLEL) vec_push(c->inner, s);
    walk_writes_expr(c, s->cond);
    walk_writes_expr(c, s->value);
    walk_writes_expr(c, s->target);
    walk_writes_stmt(c, s->init);
    walk_writes_stmt(c, s->step);
    walk_writes_stmt(c, s->then_stmt);
    walk_writes_stmt(c, s->else_stmt);
    for (int i = 0; i < s->stmts.count; i++) walk_writes_stmt(c, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        const switch_case *part = &s->cases.items[i];
        for (int k = 0; k < part->body.count; k++) walk_writes_stmt(c, part->body.items[k]);
    }
}

static bool world_sets(const ictx *c, const decl *component)
{
    if (c->world_sets_any) return true;
    for (int i = 0; i < c->world_sets.count; i++) {
        if (c->world_sets.items[i] == component) return true;
    }
    return false;
}

static bool declared_inside(const ictx *c, const stmt *local)
{
    if (local == c->loop) return true;
    for (int i = 0; i < c->inner.count; i++) {
        if (c->inner.items[i] == local) return true;
    }
    return false;
}

static bool invariant_read(ictx *c, const expr *e, const expr *read);

// Whether a (substituted) expression has the same value at every row: it
// reads nothing the loop changes. Says why not.
static bool invariant(ictx *c, const expr *e)
{
    return invariant_read(c, e, NULL);
}

// ...where `read` is the place being read through a name, when it's a part of the name's variable.
static bool invariant_read(ictx *c, const expr *e, const expr *read)
{
    if (!e) return true;
    switch (e->kind) {
    case E_INT:
    case E_FLOAT:
    case E_BOOL:
    case E_THIS:
    case E_DEFAULT:
        return true;
    case E_NAME:
        if (e->bind == BIND_LOCAL) {
            if (declared_inside(c, e->local)) {
                because(c, e->at, "its bound reads '" STR_FMT "', which is different for each %s", STR_ARG(e->name),
                        e->local == c->loop ? "row" : "round of the loop");
                return false;
            }
            if (loop_changes(c, e->local, read)) {
                because(c, e->at, "its bound reads '" STR_FMT "', which the loop changes", STR_ARG(e->name));
                return false;
            }
            return true;
        }
        if (e->bind == BIND_PARAM) {
            const param *p = e->param;
            if (p->mode == PARAM_QUERY) {
                because(c, e->at, "its bound reads a query");
                return false;
            }
            if (loop_changes(c, p, read)) {
                because(c, e->at, "its bound reads '" STR_FMT "', which the loop changes", STR_ARG(e->name));
                return false;
            }
            if (p->mode == PARAM_MUT && p->type.kind == TY_COMPONENT && world_sets(c, p->type.decl)) {
                because(c, e->at, "its bound reads '" STR_FMT "', which the loop sets through its World", STR_ARG(e->name));
                return false;
            }
            return true;
        }
        return e->bind == BIND_CONST || e->bind == BIND_ASSET || e->bind == BIND_TYPE || e->bind == BIND_NAMESPACE
            || e->bind == BIND_DEVICES || e->bind == BIND_FIELD;
    case E_MEMBER:
        if (e->property || e->local_field) {
            because(c, e->at, "its bound reads '" STR_FMT "', which the index can't read before the loop", STR_ARG(e->member));
            return false;
        }
        return invariant_read(c, e->object, read ? read : e);
    case E_UNARY:
        return invariant(c, e->lhs);
    case E_BINARY:
        if (e->method) {
            because(c, e->at, "its bound uses a struct's operator, which the index can't call before the loop");
            return false;
        }
        return invariant(c, e->lhs) && invariant(c, e->rhs);
    case E_INDEX:
        return invariant_read(c, e->object, read ? read : e) && invariant(c, e->lhs);
    case E_CONDITIONAL:
        return invariant(c, e->cond) && invariant(c, e->lhs) && invariant(c, e->rhs);
    case E_CALL:
    case E_METHOD:
        if (e->call != CALL_CONSTRUCT && !math_call(e, NULL)) {
            because(c, e->at, "its bound calls '" STR_FMT "', which the index can't call before the loop", STR_ARG(e->name));
            return false;
        }
        for (int i = 0; i < e->args.count; i++) {
            if (!invariant(c, e->args.items[i])) return false;
        }
        return true;
    default:
        because(c, e->at, "its bound is an expression the index can't work out before the loop");
        return false;
    }
}

// ---------------------------------------------------------------------------
// The row's numbers

static bool same_place(const expr *a, const expr *b)
{
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
    case E_NAME:
        return a->bind == b->bind && name_target(a) == name_target(b) && a->constant == b->constant
            && (name_target(a) || str_eq(a->name, b->name));
    case E_MEMBER:
        return a->field == b->field && a->swizzle_len == b->swizzle_len
            && memcmp(a->swizzle, b->swizzle, sizeof a->swizzle) == 0 && str_eq(a->member, b->member)
            && same_place(a->object, b->object);
    case E_INDEX:
        return same_place(a->object, b->object) && same_place(a->lhs, b->lhs);
    case E_THIS:
        return true;
    default:
        return false;
    }
}

// The row (or, through fields, a value in it), as an expression on the loop's variable; NULL when it's not one.
static const expr *row_path(ictx *c, const expr *e)
{
    e = look(c, e);
    if (!e) return NULL;
    if (e->kind == E_NAME && e->bind == BIND_LOCAL && e->local == c->loop && !c->list_steps) return e;
    if (e->kind == E_INDEX && c->list_steps) {
        const expr *index = look(c, e->lhs);
        const expr *list = subst(c, e->object);
        if (index && index->kind == E_NAME && index->bind == BIND_LOCAL && index->local == c->loop && list
            && same_place(list, c->loop->value)) {
            return e;
        }
        return NULL;
    }
    if (e->kind == E_MEMBER && e->field && e->object && !e->property && !e->local_field && e->swizzle_len == 0) {
        const expr *object = row_path(c, e->object);
        if (!object) return NULL;
        if (object == e->object) return e;
        expr *out = copy_of(e);
        out->object = (expr *)object;
        return out;
    }
    return NULL;
}

// A number of the row: a scalar field (path, -1), or one component of a vector field (path, comp).
static bool row_key(ictx *c, const expr *e, const expr **path, int *comp, type *t)
{
    e = look(c, e);
    if (!e) return false;
    if (e->kind == E_MEMBER && e->swizzle_len == 1 && e->object) {
        const expr *vector = row_path(c, e->object);
        if (!vector || type_dim(vector->type) < 2 || type_dim(vector->type) > 4) return false;
        *path = vector;
        *comp = e->swizzle[0];
        *t = type_scalar(vector->type);
        return is_scalar_key(*t);
    }
    const expr *scalar = row_path(c, e);
    if (!scalar || !is_scalar_key(scalar->type)) return false;
    *path = scalar;
    *comp = -1;
    *t = scalar->type;
    return true;
}

// ---------------------------------------------------------------------------
// Bounds

static const bound *leaf(ictx *c, const expr *e, const int comp)
{
    const expr *value = subst(c, e);
    if (!value) {
        because(c, e->at, "its bound is an expression the index can't work out before the loop");
        return NULL;
    }
    if (!invariant(c, value)) return NULL;
    bound *b = arena_alloc(sizeof *b);
    b->op = BOUND_LEAF;
    b->leaf = value;
    b->comp = comp;
    b->inexact = c->converting > 0 || c->converted != c->converted_mark;
    return b;
}

static const bound *op2(const bound_op op, const bound *a, const bound *b)
{
    if (!a || (!b && op != BOUND_SQRT)) return NULL;
    bound *out = arena_alloc(sizeof *out);
    out->op = op;
    out->a = a;
    out->b = b;
    out->comp = -1;
    return out;
}

// Whether bounds say a key of the type is one value, which is a whole number compared as one: the key and the
// value are both of exact types (a byte against a float isn't: C compares those as floats). An exact key finds it.
static bool exact_bound(const type t, const bound *lo, const bound *hi)
{
    if (!lo || lo != hi || lo->op != BOUND_LEAF || !lo->leaf || lo->inexact || !is_exact_type(t)) return false;
    // A component of a vector value (an int3's), or the value itself
    return lo->comp >= 0 ? type_is_int_based(lo->leaf->type) : is_exact_type(lo->leaf->type);
}

static void add_constraint(constraints *set, const expr *path, const int comp, const type t, const bound *lo,
                           const bound *hi)
{
    for (int i = 0; i < set->count; i++) {
        constraint *k = &set->items[i];
        if (k->comp != comp || !same_place(k->path, path)) continue;
        // A whole number that's one value stays one: other bounds on it would narrow nothing an exact key finds,
        // and leaving out a bound only ever takes in more rows
        if (exact_bound(k->type, k->lo, k->hi)) return;
        if (exact_bound(t, lo, hi)) {
            k->lo = k->hi = lo;
            return;
        }
        if (lo) k->lo = k->lo ? op2(BOUND_MAX, k->lo, lo) : lo;
        if (hi) k->hi = k->hi ? op2(BOUND_MIN, k->hi, hi) : hi;
        return;
    }
    if (set->count == MOST_CONSTRAINTS || (!lo && !hi)) return;
    set->items[set->count++] = (constraint){path, comp, t, lo, hi};
}

static constraints both(const constraints *a, const constraints *b)
{
    constraints out = *a;
    for (int i = 0; i < b->count; i++) {
        const constraint *k = &b->items[i];
        add_constraint(&out, k->path, k->comp, k->type, k->lo, k->hi);
    }
    return out;
}

static constraints either(const constraints *a, const constraints *b)
{
    constraints out = {0};
    for (int i = 0; i < a->count; i++) {
        const constraint *x = &a->items[i];
        for (int k = 0; k < b->count; k++) {
            const constraint *y = &b->items[k];
            if (x->comp != y->comp || !same_place(x->path, y->path)) continue;
            const bound *lo = x->lo && y->lo ? op2(BOUND_MIN, x->lo, y->lo) : NULL;
            const bound *hi = x->hi && y->hi ? op2(BOUND_MAX, x->hi, y->hi) : NULL;
            add_constraint(&out, x->path, x->comp, x->type, lo, hi);
        }
    }
    return out;
}

// The axes a distance between the row's vector and a fixed one runs along:
// each a component of the row's vector and the fixed value's same component.
typedef struct axis {
    const expr *path;
    int comp;
    type type;
    const expr *center; // The fixed value, a scalar or a vector (center_comp picks its component)
    int center_comp;
} axis;

typedef struct axes {
    axis items[8];
    int count;
} axes;

static void add_axis(axes *out, const axis a)
{
    for (int i = 0; i < out->count; i++) {
        if (out->items[i].comp == a.comp && same_place(out->items[i].path, a.path)) return; // Bounded once is enough
    }
    if (out->count < 8) out->items[out->count++] = a;
}

// A difference of the row's vector and a fixed vector, either way round: its axes.
static bool vector_difference(ictx *c, const expr *e, axes *out)
{
    e = look(c, e);
    if (!e || e->kind != E_BINARY || e->op != T_MINUS || !is_real_vector(e->type)) return false;
    for (int side = 0; side < 2; side++) {
        const expr *row = row_path(c, side ? e->rhs : e->lhs);
        const expr *fixed = side ? e->lhs : e->rhs;
        if (!row || !same_kind(row->type, e->type)) continue;
        for (int k = 0; k < type_dim(row->type); k++) add_axis(out, (axis){row, k, type_scalar(row->type), fixed, k});
        return true;
    }
    return false;
}

// Two vectors of which one is the row's and one fixed: their axes.
static bool vector_pair(ictx *c, const expr *a, const expr *b, axes *out)
{
    for (int side = 0; side < 2; side++) {
        const expr *row = row_path(c, side ? b : a);
        const expr *fixed = side ? a : b;
        if (!row || !is_real_vector(row->type)) continue;
        for (int k = 0; k < type_dim(row->type); k++) add_axis(out, (axis){row, k, type_scalar(row->type), fixed, k});
        return true;
    }
    return false;
}

// A difference of one of the row's numbers and a fixed number, either way round.
static bool scalar_difference(ictx *c, const expr *e, axes *out)
{
    e = look(c, e);
    if (!e || e->kind != E_BINARY || e->op != T_MINUS || type_dim(e->type) != 1 || !is_real(e->type)) return false;
    for (int side = 0; side < 2; side++) {
        const expr *path;
        int comp;
        type t;
        if (!row_key(c, side ? e->rhs : e->lhs, &path, &comp, &t) || !is_real(t)) continue;
        add_axis(out, (axis){path, comp, t, side ? e->lhs : e->rhs, -1});
        return true;
    }
    return false;
}

// A sum of squares, each of a difference between the row and a fixed value:
// every square is at most the sum, so each difference is at most its square root.
static bool squares(ictx *c, const expr *e, axes *out)
{
    e = look(c, e);
    if (!e) return false;
    if (e->kind == E_BINARY && e->op == T_PLUS && type_dim(e->type) == 1) {
        return squares(c, e->lhs, out) && squares(c, e->rhs, out);
    }
    if (e->kind == E_BINARY && e->op == T_STAR && type_dim(e->type) == 1) {
        const expr *a = look(c, e->lhs), *b = look(c, e->rhs);
        if (!a || !b) return false;
        const expr *sa = subst(c, a), *sb = subst(c, b);
        if (!sa || !sb || !(same_place(sa, sb) || (sa->kind == E_BINARY && sb->kind == E_BINARY && sa->op == sb->op
                                                 && same_place(sa->lhs, sb->lhs) && same_place(sa->rhs, sb->rhs)))) {
            return false;
        }
        return scalar_difference(c, a, out);
    }
    if (math_call(e, "LengthSq") && e->args.count == 1) return vector_difference(c, e->args.items[0], out);
    if (math_call(e, "DistanceSq") && e->args.count == 2) return vector_pair(c, e->args.items[0], e->args.items[1], out);
    if (math_call(e, "Dot") && e->args.count == 2) {
        const expr *a = subst(c, look(c, e->args.items[0])), *b = subst(c, look(c, e->args.items[1]));
        if (!a || !b || a->kind != E_BINARY || b->kind != E_BINARY || !same_place(a->lhs, b->lhs)
            || !same_place(a->rhs, b->rhs)) {
            return false;
        }
        return vector_difference(c, e->args.items[0], out);
    }
    return false;
}

// A distance (squared or not) from the row to a fixed place.
static bool distance(ictx *c, const expr *e, axes *out, bool *squared)
{
    e = look(c, e);
    if (!e) return false;
    *squared = false;
    if (math_call(e, "Distance") && e->args.count == 2) return vector_pair(c, e->args.items[0], e->args.items[1], out);
    if (math_call(e, "Length") && e->args.count == 1) return vector_difference(c, e->args.items[0], out);
    if (math_call(e, "Abs") && e->args.count == 1 && type_dim(e->type) == 1) return scalar_difference(c, e->args.items[0], out);
    if (math_call(e, "Sqrt") && e->args.count == 1) return squares(c, e->args.items[0], out);
    *squared = true;
    return squares(c, e, out);
}

// What `K op V` says, with V fixed and K the row's side; nothing when K isn't one the analysis knows.
static bool relation(ictx *c, const expr *k, const tok_kind op, const expr *v, constraints *out)
{
    const expr *value = look(c, v);
    if (!value) return false;
    const expr *path;
    int comp;
    type t;
    if (row_key(c, k, &path, &comp, &t)) {
        const bound *b = leaf(c, value, -1);
        if (!b) return true; // The row's side, but a bound that isn't fixed: it says why
        if (op == T_EQ) add_constraint(out, path, comp, t, b, b);
        else if (op == T_LT || op == T_LE) add_constraint(out, path, comp, t, NULL, b);
        else add_constraint(out, path, comp, t, b, NULL);
        return true;
    }
    const expr *ke = look(c, k);
    if (!ke) return false;
    // A vector the row has, equal to a fixed one: each component equal
    if (op == T_EQ) {
        const expr *row = row_path(c, ke);
        if (row && type_dim(row->type) >= 2 && type_dim(row->type) <= 3 && is_scalar_key(type_scalar(row->type))) {
            for (int i = 0; i < type_dim(row->type); i++) {
                const bound *b = leaf(c, value, i);
                if (!b) return true;
                add_constraint(out, row, i, type_scalar(row->type), b, b);
            }
            return true;
        }
    }
    // key + fixed, key - fixed and fixed - key, for floats and doubles, which round and never wrap
    if (ke->kind == E_BINARY && (ke->op == T_PLUS || ke->op == T_MINUS) && type_dim(ke->type) == 1 && is_real(ke->type)) {
        for (int side = 0; side < 2; side++) {
            const expr *row_side = side ? ke->rhs : ke->lhs, *fixed = side ? ke->lhs : ke->rhs;
            if (!row_key(c, row_side, &path, &comp, &t) || !is_real(t)) continue;
            const bound *f = leaf(c, fixed, -1), *b = leaf(c, value, -1);
            if (!f || !b) return true;
            if (ke->op == T_PLUS) { // key + f op v: key op v - f
                const bound *limit = op2(BOUND_SUB, b, f);
                if (op == T_EQ) add_constraint(out, path, comp, t, limit, limit);
                else if (op == T_LT || op == T_LE) add_constraint(out, path, comp, t, NULL, limit);
                else add_constraint(out, path, comp, t, limit, NULL);
            } else if (!side) { // key - f op v: key op v + f
                const bound *limit = op2(BOUND_ADD, b, f);
                if (op == T_EQ) add_constraint(out, path, comp, t, limit, limit);
                else if (op == T_LT || op == T_LE) add_constraint(out, path, comp, t, NULL, limit);
                else add_constraint(out, path, comp, t, limit, NULL);
            } else { // f - key op v: key op' f - v
                const bound *limit = op2(BOUND_SUB, f, b);
                if (op == T_EQ) add_constraint(out, path, comp, t, limit, limit);
                else if (op == T_LT || op == T_LE) add_constraint(out, path, comp, t, limit, NULL);
                else add_constraint(out, path, comp, t, NULL, limit);
            }
            return true;
        }
    }
    // A distance at most a radius
    if (op == T_LT || op == T_LE || op == T_EQ) {
        axes found = {0};
        bool squared;
        if (distance(c, ke, &found, &squared)) {
            const bound *r = leaf(c, value, -1);
            if (!r) return true;
            if (squared) r = op2(BOUND_SQRT, r, NULL);
            for (int i = 0; i < found.count; i++) {
                const axis *a = &found.items[i];
                const bound *center = leaf(c, a->center, a->center_comp);
                if (!center) return true;
                add_constraint(out, a->path, a->comp, a->type, op2(BOUND_SUB, center, r), op2(BOUND_ADD, center, r));
            }
            return true;
        }
    }
    return false;
}

static constraints derive(ictx *c, const expr *e, bool want);

static constraints compare(ictx *c, const expr *a, tok_kind op, const expr *b, const bool want)
{
    constraints out = {0};
    if (!want) op = negated(op); // NaN makes a negated comparison true: rows of NaN keys are candidates always
    if (op == T_NE) return out;
    // A conversion either side, seen past from here on, makes its bounds inexact (leaf)
    const uint32_t mark = c->converted_mark;
    c->converted_mark = c->converted;
    if (!relation(c, a, op, b, &out)) {
        c->converted_mark = c->converted; // The other way round reads both sides again, and counts its own
        relation(c, b, flipped(op), a, &out);
    }
    c->converted_mark = mark;
    return out;
}

static constraints derive_seen(ictx *c, const expr *e, bool want);

// Bounds that hold whenever the condition is `want`. A condition seen through a conversion (an inlined helper
// whose float local holds an int) makes every bound of it inexact.
static constraints derive(ictx *c, const expr *e, const bool want)
{
    const uint32_t before = c->converted;
    e = look(c, e);
    if (!e) return (constraints){0};
    const bool converted = c->converted != before;
    c->converting += converted;
    const constraints out = derive_seen(c, e, want);
    c->converting -= converted;
    return out;
}

static constraints derive_seen(ictx *c, const expr *e, const bool want)
{
    constraints none = {0};
    if (e->kind == E_UNARY && e->op == T_NOT) return derive(c, e->lhs, !want);
    if (e->kind == E_BINARY && (e->op == T_AND || e->op == T_OR)) {
        const constraints a = derive(c, e->lhs, want), b = derive(c, e->rhs, want);
        return (e->op == T_AND) == want ? both(&a, &b) : either(&a, &b);
    }
    if (e->kind == E_BINARY && is_comparison(e->op)) return compare(c, e->lhs, e->op, e->rhs, want);
    // A bool of the row's, alone
    const expr *path;
    int comp;
    type t;
    if (e->type.kind == TY_BOOL && row_key(c, e, &path, &comp, &t)) {
        expr *literal = arena_alloc(sizeof *literal);
        literal->kind = E_BOOL;
        literal->type = (type){TY_BOOL, NULL};
        literal->bool_value = want;
        literal->at = e->at;
        bound *b = arena_alloc(sizeof *b);
        b->op = BOUND_LEAF;
        b->leaf = literal;
        b->comp = -1;
        b->inexact = c->converting > 0;
        constraints out = {0};
        add_constraint(&out, path, comp, t, b, b);
        return out;
    }
    return none;
}

// ---------------------------------------------------------------------------
// The loop's guards

static bool only_continue(const stmt *s)
{
    if (!s) return false;
    if (s->kind == S_CONTINUE) return true;
    return s->kind == S_BLOCK && s->stmts.count == 1 && s->stmts.items[0]->kind == S_CONTINUE;
}

// Walks the guards the body starts with, adding what each says.
static void walk_guards(ictx *c, const stmt *body, constraints *out, bool *found)
{
    if (!body) return;
    stmt *const single[1] = {(stmt *)body};
    stmt *const *items = body->kind == S_BLOCK ? body->stmts.items : single;
    const int count = body->kind == S_BLOCK ? body->stmts.count : 1;
    for (int i = 0; i < count; i++) {
        const stmt *s = items[i];
        if (s->kind == S_VAR && !s->is_mut && s->value && !s->out_decl && !s->is_shared && pure(c, s->value)) {
            if (!env_push(c, s, s->value)) return;
            continue;
        }
        if (s->kind == S_IF && !s->else_stmt && pure(c, s->cond)) {
            if (only_continue(s->then_stmt)) {
                const constraints g = derive(c, s->cond, false);
                *out = both(out, &g);
                *found = true;
                continue;
            }
            if (i == count - 1) {
                const constraints g = derive(c, s->cond, true);
                *out = both(out, &g);
                *found = true;
                walk_guards(c, s->then_stmt, out, found);
                return;
            }
        }
        return;
    }
}

// The component a query row's path reads, from the binding it starts with.
static const decl *path_component(const ictx *c, const expr *path)
{
    const expr *member = NULL;
    while (path && path->kind == E_MEMBER) {
        if (path->object && path->object->kind == E_NAME && path->object->local == c->loop) {
            member = path;
            break;
        }
        path = path->object;
    }
    if (!member || !member->field) return NULL;
    const type t = member->field->type;
    return t.kind == TY_COMPONENT ? t.decl : NULL;
}

// Whether a query row's path starts at a binding the query takes `previous`.
static bool path_previous(const ictx *c, const expr *path)
{
    const param *q = c->loop->query;
    while (path && path->kind == E_MEMBER) {
        if (path->object && path->object->kind == E_NAME && path->object->local == c->loop) break;
        path = path->object;
    }
    if (!q || !path || path->kind != E_MEMBER || !path->field) return false;
    for (int i = 0; i < q->query.count; i++) {
        const param *p = &q->query.items[i];
        if (p->previous && param_binding(p) && str_eq(p->name, path->field->name)) return true;
    }
    return false;
}

// The component's field a query row's path reads first: `b.body.position.x` reads Body's position.
static const field *path_field(const ictx *c, const expr *path)
{
    const expr *inner = NULL;
    while (path && path->kind == E_MEMBER && path->object) {
        if (path->object->kind == E_MEMBER && path->object->object && path->object->object->kind == E_NAME
            && path->object->object->local == c->loop) {
            inner = path;
            break;
        }
        path = path->object;
    }
    return inner ? inner->field : NULL;
}

// Whether code changes the field of its own entity's component, as it goes, between one loop and the next.
static bool changes_own_field(const decl *code, const decl *component, const field *f, loc *at);

static bool expr_changes_param(const expr *e, const param *p, const field *f, loc *at);
static bool stmt_changes_param(const stmt *s, const param *p, const field *f, loc *at)
{
    if (!s) return false;
    if (s->kind == S_ASSIGN) {
        const expr *t = s->target;
        const expr *first = NULL;
        while (t && (t->kind == E_MEMBER || t->kind == E_INDEX) && t->object) {
            first = t;
            t = t->object;
        }
        if (t && t->kind == E_NAME && t->bind == BIND_PARAM && t->param == p
            && (!first || first->kind != E_MEMBER || !first->field || !f || first->field == f)) {
            *at = s->at;
            return true;
        }
    }
    if (expr_changes_param(s->cond, p, f, at) || expr_changes_param(s->value, p, f, at)) return true;
    if (stmt_changes_param(s->init, p, f, at) || stmt_changes_param(s->step, p, f, at)) return true;
    if (stmt_changes_param(s->then_stmt, p, f, at) || stmt_changes_param(s->else_stmt, p, f, at)) return true;
    for (int i = 0; i < s->stmts.count; i++) {
        if (stmt_changes_param(s->stmts.items[i], p, f, at)) return true;
    }
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) {
            if (stmt_changes_param(s->cases.items[i].body.items[k], p, f, at)) return true;
        }
    }
    return false;
}

static bool names_param(const expr *e, const param *p)
{
    while (e && (e->kind == E_MEMBER || e->kind == E_INDEX) && e->object) e = e->object;
    return e && e->kind == E_NAME && e->bind == BIND_PARAM && e->param == p;
}

static bool expr_changes_param(const expr *e, const param *p, const field *f, loc *at)
{
    if (!e) return false;
    if (e->kind == E_STEP && names_param(e->lhs, p)) {
        *at = e->at;
        return true;
    }
    if ((e->call == CALL_FUNCTION || e->call == CALL_METHOD) && e->method) {
        const decl *d = e->method;
        for (int i = 0; i < d->params.count && i < e->args.count; i++) {
            const param *q = &d->params.items[i];
            if ((q->mode == PARAM_MUT || q->is_out) && !changes_local_fields_alone(d, q) && names_param(e->args.items[i], p)) {
                *at = e->at;
                return true;
            }
        }
        if (e->call == CALL_METHOD && d->is_mut_method && names_param(e->object, p)) {
            *at = e->at;
            return true;
        }
    }
    if (expr_changes_param(e->object, p, f, at) || expr_changes_param(e->lhs, p, f, at)
        || expr_changes_param(e->rhs, p, f, at) || expr_changes_param(e->cond, p, f, at)) {
        return true;
    }
    for (int i = 0; i < e->args.count; i++) {
        if (expr_changes_param(e->args.items[i], p, f, at)) return true;
    }
    return stmt_changes_param(e->block, p, f, at) || stmt_changes_param(e->step, p, f, at);
}

static bool changes_own_field(const decl *code, const decl *component, const field *f, loc *at)
{
    for (int i = 0; i < code->world_uses.count; i++) {
        const world_use *u = &code->world_uses.items[i];
        if (u->writes && u->through_world && u->component == component) {
            *at = u->write_at;
            return true;
        }
    }
    for (int i = 0; i < code->params.count; i++) {
        const param *p = &code->params.items[i];
        if (p->mode != PARAM_MUT || p->type.kind != TY_COMPONENT || p->type.decl != component) continue;
        if (stmt_changes_param(code->body, p, f, at)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Which code calls which: every function and method the game's code calls, found once a program. A big game's
// loops asked, for each system, whether it reaches their code, and each answer walked everything the system
// calls: thousands of walks of the same bodies.

typedef struct call_graph {
    const program *prog;
    const decl **nodes;     // The game's declarations, their methods, and whatever those call
    int count, room;
    int listed;             // The first `listed` are the program's own (decls and their methods), in their order
    int *first, *callees;   // Node n calls callees[first[n] .. first[n + 1])
    const decl **keys;      // A declaration's node, by its address: 1 + the node, or 0
    int *slots;
    uint32_t mask;
    VEC(int) edges;         // While it's built: caller, callee, caller, callee...
    // Who reaches a target, asked before: a byte a node
    VEC(const decl *) asked;
    VEC(uint8_t *) reaching;
} call_graph;

static call_graph calls_of;

static uint32_t call_place(const decl *d, const uint32_t mask)
{
    uint64_t n = (uint64_t)(uintptr_t)d;
    n ^= n >> 33;
    n *= UINT64_C(0xff51afd7ed558ccd);
    n ^= n >> 33;
    return (uint32_t)n & mask;
}

static int call_node(call_graph *g, const decl *d, const bool add)
{
    if (g->keys) {
        uint32_t at = call_place(d, g->mask);
        while (g->keys[at] && g->keys[at] != d) at = (at + 1u) & g->mask;
        if (g->keys[at]) return g->slots[at] - 1;
    }
    if (!add) return -1;
    if (!g->keys || (uint64_t)(g->count + 1) * 2u > g->mask) {
        const uint32_t mask = g->keys ? g->mask * 2u + 1u : 4095u;
        const decl **keys = arena_alloc(((size_t)mask + 1u) * sizeof *keys);
        int *slots = arena_alloc(((size_t)mask + 1u) * sizeof *slots);
        memset(keys, 0, ((size_t)mask + 1u) * sizeof *keys);
        for (int i = 0; i < g->count; i++) {
            uint32_t at = call_place(g->nodes[i], mask);
            while (keys[at]) at = (at + 1u) & mask;
            keys[at] = g->nodes[i];
            slots[at] = i + 1;
        }
        g->keys = keys;
        g->slots = slots;
        g->mask = mask;
    }
    if (g->count == g->room) {
        const int room = g->room ? g->room * 2 : 1024;
        const decl **nodes = arena_alloc((size_t)room * sizeof *nodes);
        if (g->count) memcpy(nodes, g->nodes, (size_t)g->count * sizeof *nodes);
        g->nodes = nodes;
        g->room = room;
    }
    uint32_t at = call_place(d, g->mask);
    while (g->keys[at]) at = (at + 1u) & g->mask;
    g->keys[at] = d;
    g->slots[at] = g->count + 1;
    g->nodes[g->count] = d;
    return g->count++;
}

// The calls in code, as an edge each from node `from`
static void calls_expr(call_graph *g, int from, const expr *e);
static void calls_stmt(call_graph *g, const int from, const stmt *s)
{
    if (!s) return;
    calls_expr(g, from, s->cond);
    calls_expr(g, from, s->value);
    calls_expr(g, from, s->target);
    calls_stmt(g, from, s->init);
    calls_stmt(g, from, s->step);
    calls_stmt(g, from, s->then_stmt);
    calls_stmt(g, from, s->else_stmt);
    for (int i = 0; i < s->stmts.count; i++) calls_stmt(g, from, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) calls_stmt(g, from, s->cases.items[i].body.items[k]);
    }
}

static void calls_expr(call_graph *g, const int from, const expr *e)
{
    if (!e) return;
    if ((e->call == CALL_FUNCTION || e->call == CALL_METHOD) && e->method) {
        vec_push(g->edges, from);
        vec_push(g->edges, call_node(g, e->method, true));
    }
    calls_expr(g, from, e->object);
    calls_expr(g, from, e->lhs);
    calls_expr(g, from, e->rhs);
    calls_expr(g, from, e->cond);
    for (int i = 0; i < e->args.count; i++) calls_expr(g, from, e->args.items[i]);
    calls_stmt(g, from, e->block);
    calls_stmt(g, from, e->step);
}

static void call_list(call_graph *g, const decl *d)
{
    call_node(g, d, true);
    for (int k = 0; k < d->methods.count; k++) call_list(g, d->methods.items[k]);
}

// The program's graph, made at the first ask: the arena's, which a compilation doesn't outlive (call_graph_reset)
static call_graph *calls_graph(const program *prog)
{
    call_graph *g = &calls_of;
    if (g->prog == prog) return g;
    memset(g, 0, sizeof *g);
    g->prog = prog;
    for (int i = 0; i < prog->decls.count; i++) call_list(g, prog->decls.items[i]);
    g->listed = g->count;
    for (int n = 0; n < g->count; n++) calls_stmt(g, n, g->nodes[n]->body); // What a body calls joins the nodes, and is walked too
    // By caller: a count each, then each one's place
    g->first = arena_alloc(((size_t)g->count + 1u) * sizeof *g->first);
    memset(g->first, 0, ((size_t)g->count + 1u) * sizeof *g->first);
    for (int i = 0; i < g->edges.count; i += 2) g->first[g->edges.items[i] + 1]++;
    for (int n = 0; n < g->count; n++) g->first[n + 1] += g->first[n];
    g->callees = arena_alloc(((size_t)(g->edges.count / 2) + 1u) * sizeof *g->callees);
    int *next = arena_alloc(((size_t)g->count + 1u) * sizeof *next);
    memcpy(next, g->first, ((size_t)g->count + 1u) * sizeof *next);
    for (int i = 0; i < g->edges.count; i += 2) g->callees[next[g->edges.items[i]]++] = g->edges.items[i + 1];
    return g;
}

void call_graph_reset(void)
{
    memset(&calls_of, 0, sizeof calls_of);
}

// Whether `from` calls `target`, directly or through other functions.
static bool calls_code(const program *prog, const decl *from, const decl *target)
{
    call_graph *g = calls_graph(prog);
    const int t = call_node(g, target, false), f = call_node(g, from, false);
    if (t < 0 || f < 0) return false; // Code nothing lists or calls
    uint8_t *reaching = NULL;
    for (int i = 0; i < g->asked.count && !reaching; i++) {
        if (g->asked.items[i] == target) reaching = g->reaching.items[i];
    }
    if (!reaching) {
        // Every node that calls the target, then every node that calls one of those, until none is new
        reaching = arena_alloc((size_t)g->count + 1u);
        memset(reaching, 0, (size_t)g->count + 1u);
        uint8_t *is = arena_alloc((size_t)g->count + 1u);
        memset(is, 0, (size_t)g->count + 1u);
        is[t] = 1;
        for (bool more = true; more;) {
            more = false;
            for (int n = 0; n < g->count; n++) {
                if (reaching[n]) continue;
                for (int k = g->first[n]; k < g->first[n + 1]; k++) {
                    if (!is[g->callees[k]] && !reaching[g->callees[k]]) continue;
                    reaching[n] = 1;
                    more = true;
                    break;
                }
            }
        }
        vec_push(g->asked, target);
        vec_push(g->reaching, reaching);
    }
    return reaching[f] != 0;
}

const decl **call_graph_bottom_up(const program *prog, int *count)
{
    call_graph *g = calls_graph(prog);
    // Each listed node after everything it calls that's listed, where the calls don't go round: a walk from each in
    // the program's order, a node given its place once what it calls has theirs
    const decl **order = arena_alloc(((size_t)g->listed + 1u) * sizeof *order);
    uint8_t *state = arena_alloc((size_t)g->count + 1u); // 0 not met, 1 being walked, 2 placed
    memset(state, 0, (size_t)g->count + 1u);
    int *stack = arena_alloc(((size_t)g->count + 1u) * sizeof *stack), *at = arena_alloc(((size_t)g->count + 1u) * sizeof *at);
    int placed = 0;
    for (int root = 0; root < g->listed; root++) {
        if (state[root]) continue;
        int depth = 0;
        stack[0] = root;
        at[0] = g->first[root];
        state[root] = 1;
        while (depth >= 0) {
            const int n = stack[depth];
            if (at[depth] < g->first[n + 1]) {
                const int to = g->callees[at[depth]++];
                if (state[to]) continue;
                state[to] = 1;
                stack[++depth] = to;
                at[depth] = g->first[to];
            } else {
                state[n] = 2;
                if (n < g->listed) order[placed++] = g->nodes[n];
                depth--;
            }
        }
    }
    *count = placed;
    return order;
}

// A query's key must keep its value while the code it's in runs, between one
// time the loop starts and the next: the index made at one is used at the
// next while nothing changes the pages it was made from.
static bool key_still(ictx *c, const decl *component, const field *f);

static bool query_source_still(ictx *c, const loop_index *plan)
{
    for (int k = 0; k < plan->count; k++) {
        if (!path_component(c, plan->keys[k].path)) {
            because(c, c->loop->at, "its key isn't a component's field");
            return false;
        }
    }
    // As the tick started: nothing the code or any other system does changes it
    if (plan->previous) return true;
    // Each field a key is in, once (an exact key's may be in several fields, of several components)
    for (int k = 0; k < plan->count; k++) {
        const decl *component = path_component(c, plan->keys[k].path);
        const field *f = path_field(c, plan->keys[k].path);
        bool seen = false;
        for (int j = 0; j < k; j++) {
            seen |= path_component(c, plan->keys[j].path) == component && path_field(c, plan->keys[j].path) == f;
        }
        if (!seen && !key_still(c, component, f)) return false;
    }
    return true;
}

// ...for the key in `f` of `component` (NULL: the component whole)
static bool key_still(ictx *c, const decl *component, const field *f)
{
    if (world_sets(c, component)) {
        because(c, c->loop->at, "the loop sets " STR_FMT " through its World, which changes rows as it goes",
                STR_ARG(component->name));
        return false;
    }
    loc at = {0};
    const str fname = f ? f->name : str_from("");
    const char *dot = f ? "." : "";
    if (c->code->kind == DECL_SYSTEM) {
        if (changes_own_field(c->code, component, f, &at)) {
            because(c, at, "this %s changes " STR_FMT "%s" STR_FMT " of its own entity as it goes, which the loop finds "
                           "rows by: change it in a system of its own", c->code->is_handler ? "handler" : "system",
                    STR_ARG(component->name), dot, STR_ARG(fname));
            return false;
        }
        return true;
    }
    // A borrowed query: every system that can call this code
    for (int i = 0; i < c->prog->decls.count; i++) {
        const decl *d = c->prog->decls.items[i];
        if (d->kind != DECL_SYSTEM || !d->body) continue;
        // One whose queries take the component `previous` alone hands this code the tick's start, which its own
        // writes don't change
        bool before = false, now = false;
        for (int k = 0; k < d->previous_reads.count; k++) before |= d->previous_reads.items[k] == component;
        for (int k = 0; k < d->world_uses.count; k++) now |= d->world_uses.items[k].component == component && d->world_uses.items[k].query_read;
        now |= !before;
        const bool reaches = now && calls_code(c->prog, d, c->code);
        if (reaches && changes_own_field(d, component, f, &at)) {
            because(c, at, "'" STR_FMT "' changes " STR_FMT "%s" STR_FMT " of its own entity as it goes, and calls this "
                           "code, whose loop finds rows by it: change it in a system of its own", STR_ARG(d->name),
                    STR_ARG(component->name), dot, STR_ARG(fname));
            return false;
        }
    }
    return true;
}

// A query's keys are read from one kind of table: the tick's start's (`previous` bindings) or the world's as it
// is. An exact key's whole numbers and doubles can be in several bindings; those of another kind than the first
// key's are left to the guard.
static void keep_alike(const ictx *c, loop_index *plan)
{
    const bool first = path_previous(c, plan->keys[0].path);
    int n = 1, exact = plan->exact ? 1 : 0;
    for (int k = 1; k < plan->count; k++) {
        if (path_previous(c, plan->keys[k].path) != first) continue;
        plan->keys[n++] = plan->keys[k];
        exact += k < plan->exact;
    }
    plan->count = n;
    plan->exact = exact;
}

// The loop's list must keep its elements while the loop runs.
static bool list_source_still(ictx *c)
{
    if (c->list_steps) return true; // A parallel loop's steps change their own elements, after their guards
    const expr *root = c->loop->value;
    while (root && (root->kind == E_MEMBER || root->kind == E_INDEX) && root->object) root = root->object;
    const void *place = root ? name_target(root) : NULL;
    if (place && loop_changes(c, place, c->loop->value)) {
        because(c, c->loop->value->at, "the loop changes the list it goes through");
        return false;
    }
    return true;
}

// Picks the keys: two or three bounded both ways (a grid), else every whole
// number that's one value as one exact key, with up to three other bounds
// beside it, else an equality, else one bounded both ways, else one bounded
// one way.
static bool choose(const constraints *set, loop_index *plan)
{
    int best = -1;
    // A grid: bounded both ways, float or double, preferring components of one vector
    for (int i = 0; i < set->count; i++) {
        const constraint *k = &set->items[i];
        if (!k->lo || !k->hi || !is_real(k->type)) continue;
        int n = 0;
        index_key keys[3];
        keys[n++] = (index_key){k->path, k->comp, k->type, k->lo, k->hi};
        for (int j = 0; j < set->count && n < 3; j++) {
            const constraint *o = &set->items[j];
            if (j == i || !o->lo || !o->hi || !is_real(o->type) || o->comp < 0 || k->comp < 0) continue;
            if (!same_place(o->path, k->path)) continue;
            keys[n++] = (index_key){o->path, o->comp, o->type, o->lo, o->hi};
        }
        if (n >= 2 && n > plan->count) {
            plan->count = n;
            memcpy(plan->keys, keys, sizeof keys);
        }
    }
    if (plan->count >= 2) return true;
    // An exact key: the whole numbers that are each one value (a chunk's place's three components and its
    // dimension, a team), in the order the guard says them, then other bounds beside them, both ways first
    int n = 0;
    for (int i = 0; i < set->count && n < INDEX_EXACT_MOST; i++) {
        const constraint *k = &set->items[i];
        if (exact_bound(k->type, k->lo, k->hi)) plan->keys[n++] = (index_key){k->path, k->comp, k->type, k->lo, k->hi};
    }
    if (n > 0) {
        plan->exact = n;
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < set->count && n < plan->exact + 3; i++) {
                const constraint *k = &set->items[i];
                if (exact_bound(k->type, k->lo, k->hi) || (pass == 0) != (k->lo && k->hi)) continue;
                plan->keys[n++] = (index_key){k->path, k->comp, k->type, k->lo, k->hi};
            }
        }
        plan->count = n;
        return true;
    }
    for (int pass = 0; pass < 3 && best < 0; pass++) {
        for (int i = 0; i < set->count && best < 0; i++) {
            const constraint *k = &set->items[i];
            if (pass == 0 && k->lo && k->lo == k->hi) best = i;
            if (pass == 1 && k->lo && k->hi) best = i;
            if (pass == 2) best = i;
        }
    }
    if (best < 0) return false;
    const constraint *k = &set->items[best];
    plan->keys[0] = (index_key){k->path, k->comp, k->type, k->lo, k->hi};
    plan->count = 1;
    return true;
}

// Whether code waits anywhere in it (await).
static bool expr_waits(const expr *e);
static bool stmt_waits(const stmt *s)
{
    if (!s) return false;
    if (expr_waits(s->cond) || expr_waits(s->value) || expr_waits(s->target)) return true;
    if (stmt_waits(s->init) || stmt_waits(s->step) || stmt_waits(s->then_stmt) || stmt_waits(s->else_stmt)) return true;
    for (int i = 0; i < s->stmts.count; i++) {
        if (stmt_waits(s->stmts.items[i])) return true;
    }
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) {
            if (stmt_waits(s->cases.items[i].body.items[k])) return true;
        }
    }
    return false;
}

static bool expr_waits(const expr *e)
{
    if (!e) return false;
    if (e->kind == E_AWAIT) return true;
    if (expr_waits(e->object) || expr_waits(e->lhs) || expr_waits(e->rhs) || expr_waits(e->cond)) return true;
    for (int i = 0; i < e->args.count; i++) {
        if (expr_waits(e->args.items[i])) return true;
    }
    for (int i = 0; i < e->inits.count; i++) {
        if (expr_waits(e->inits.items[i].value)) return true;
    }
    return stmt_waits(e->block) || stmt_waits(e->step);
}

// The local a loop's list is in (the list itself or a field of it), when it's a local declared in the code
// (not a parameter, a loop's variable or an `out` one) that nothing in the code writes after declaring it: then
// the list it holds is the same for as long as the declaration is.
static const stmt *unwritten_local(const decl *code, const expr *list)
{
    const expr *root = list;
    while (root && root->kind == E_MEMBER && root->object && root->field && !root->property && !root->local_field) {
        root = root->object;
    }
    if (!root || root->kind != E_NAME || root->bind != BIND_LOCAL || !root->local) return NULL;
    const stmt *local = root->local;
    if (local->kind != S_VAR || local->out_decl || local->is_shared || !code->body) return NULL;
    ictx c = {0};
    c.code = code;
    walk_writes_stmt(&c, code->body);
    return loop_changes(&c, local, NULL) ? NULL : local;
}

// Whether a list's index lasts beyond one run of its loop without a local's stamp: a field of a singleton
// that a system or a view takes read-only (not this machine's own, not `verified`), which nothing changes
// while the code runs (index_codegen.inc, ix_list_world).
static bool list_kept(const decl *code, const expr *list)
{
    if (!code || code->kind != DECL_SYSTEM || code->is_handler || code->is_local || code->extern_system) return false;
    const expr *root = list;
    while (root && root->kind == E_MEMBER && root->object && root->field && !root->property && !root->local_field) root = root->object;
    if (!root || root->kind != E_NAME || root->bind != BIND_PARAM) return false;
    const param *p = root->param;
    return p->mode == PARAM_READ && p->type.kind == TY_SINGLETON && p->type.decl && !p->type.decl->is_local && !p->verified;
}

static void plan_loop(const program *prog, const decl *code, stmt *loop)
{
    const type source = loop->value ? loop->value->type : (type){TY_ERROR, NULL};
    const bool query = source.kind == TY_QUERY;
    const bool list = source.kind == TY_LIST;
    if (!query && !list) {
        if (loop->indexed && source.kind != TY_ERROR) {
            diag_error(loop->indexed_at, "an indexed loop goes through a query or a list, and this one goes through %s",
                       source.kind == TY_GRID ? "a grid" : "something else");
        }
        return;
    }
    ictx c = {0};
    c.prog = prog;
    c.code = code;
    c.loop = loop;
    c.list_steps = loop->kind == S_PARALLEL && list;
    walk_writes_stmt(&c, loop->then_stmt);
    constraints found = {0};
    bool guarded = false;
    walk_guards(&c, loop->then_stmt, &found, &guarded);
    loop->index_guarded = guarded;
    loop_index *plan = arena_alloc(sizeof *plan);
    if (!guarded) {
        because(&c, loop->at, "its body doesn't start by skipping rows: begin it with 'if (...) continue;' or wrap it in "
                              "one 'if (...) { ... }'");
    } else if (!found.count) {
        because(&c, loop->at, "its guard puts no bound on a number of the row: compare one of the row's numbers, or its "
                              "distance to a place, with a value the loop doesn't change");
    } else if (choose(&found, plan)) {
        if (query) keep_alike(&c, plan);
        plan->component = query ? path_component(&c, plan->keys[0].path) : NULL;
        plan->previous = query && path_previous(&c, plan->keys[0].path);
        const bool still = query ? query_source_still(&c, plan) : list_source_still(&c);
        if (still && plan->previous && code->is_handler) {
            // (The tick's index is of rows as they were, and a handler's entities have moved since)
            because(&c, loop->at, "a handler finds each entity's 'previous' values by the entity, once rows have moved: a "
                                  "system's loop goes through the tick's index");
        } else if (still && code->is_async && stmt_waits(loop->then_stmt)) {
            because(&c, loop->at, "its body waits (await), and the index's search can't be kept across a wait");
        } else if (still) {
            loop->index = plan;
            if (list && !c.list_steps) plan->list_local = unwritten_local(code, loop->value);
            // A list nothing shows to be the same from one run to the next would get an index for each run: a
            // pass over every element for its keys and a sort, where the plain loop is one pass that tests them.
            // Asked for by name (`indexed foreach`), it's made all the same.
            if (list && !c.list_steps && !loop->indexed && !plan->list_local && !list_kept(code, loop->value)) {
                loop->index = NULL;
                because(&c, loop->at, "its list could change between one run of the loop and the next, so its index would be "
                                      "made again each run, which costs more than going through every element: an index is "
                                      "kept for a list of a singleton the code only reads, or one in a local nothing writes");
            }
        }
    } else {
        because(&c, loop->at, "its guard bounds no number of the row on either side");
    }
    if (!loop->index) {
        loop->index_why = c.why ? c.why : "its guard bounds no number of the row";
        loop->index_why_at = c.why ? c.why_at : loop->at;
        if (loop->indexed) {
            diag_error(loop->indexed_at, "this loop can't be indexed: %s", loop->index_why);
            if (loop->index_why_at.line != loop->indexed_at.line || loop->index_why_at.col != loop->indexed_at.col) {
                diag_note_at(loop->index_why_at, "here");
            }
            diag_note("an indexed loop starts by skipping the rows that can't pass, on a condition the index can "
                      "work out before the loop, like 'if (Math.Distance(row.body.position, center) > range) continue;'");
        }
    }
}

static void plan_stmt(const program *prog, const decl *code, stmt *s);
static void plan_expr(const program *prog, const decl *code, const expr *e)
{
    if (!e) return;
    plan_expr(prog, code, e->object);
    plan_expr(prog, code, e->lhs);
    plan_expr(prog, code, e->rhs);
    plan_expr(prog, code, e->cond);
    for (int i = 0; i < e->args.count; i++) plan_expr(prog, code, e->args.items[i]);
    if (e->block) plan_stmt(prog, code, e->block);
}

static void plan_stmt(const program *prog, const decl *code, stmt *s)
{
    if (!s) return;
    if (s->kind == S_FOREACH || s->kind == S_PARALLEL) plan_loop(prog, code, s);
    plan_expr(prog, code, s->cond);
    plan_expr(prog, code, s->value);
    plan_stmt(prog, code, s->init);
    plan_stmt(prog, code, s->step);
    plan_stmt(prog, code, s->then_stmt);
    plan_stmt(prog, code, s->else_stmt);
    for (int i = 0; i < s->stmts.count; i++) plan_stmt(prog, code, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) plan_stmt(prog, code, s->cases.items[i].body.items[k]);
    }
}

void plan_loop_indexes(program *prog)
{
    call_graph_reset(); // Another program's, or this one's before its code was checked
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if ((d->kind == DECL_SYSTEM || d->kind == DECL_FUNCTION) && d->body) plan_stmt(prog, d, d->body);
        for (int k = 0; k < d->methods.count; k++) {
            const decl *m = d->methods.items[k];
            if (m->body) plan_stmt(prog, m, m->body);
        }
    }
}

static void describe_path(sb *out, const expr *path)
{
    if (!path) return;
    if (path->kind == E_MEMBER && path->object) {
        describe_path(out, path->object);
        sb_printf(out, "." STR_FMT, STR_ARG(path->member));
    } else if (path->kind == E_INDEX && path->object) {
        describe_path(out, path->object);
        sb_put(out, "[");
        describe_path(out, path->lhs);
        sb_put(out, "]");
    } else if (path->kind == E_NAME) {
        sb_printf(out, STR_FMT, STR_ARG(path->name));
    }
}

const char *loop_index_summary(const stmt *loop)
{
    const type source = loop->value ? loop->value->type : (type){TY_ERROR, NULL};
    if (source.kind != TY_QUERY && source.kind != TY_LIST) return NULL;
    sb out = {0};
    if (!loop->index) {
        sb_printf(&out, "not indexed: %s", loop->index_why ? loop->index_why : "no guard");
        return out.data;
    }
    const loop_index *plan = loop->index;
    sb_put(&out, "indexed by ");
    if (plan->exact) { // "a, b and c as one exact key, and by d and e"
        for (int part = 0; part < 2; part++) {
            const int from = part ? plan->exact : 0, to = part ? plan->count : plan->exact;
            if (from == to) continue;
            if (part) sb_put(&out, ", and by ");
            for (int i = from; i < to; i++) {
                const index_key *k = &plan->keys[i];
                if (i > from) sb_put(&out, i + 1 == to ? " and " : ", ");
                describe_path(&out, k->path);
                if (k->comp >= 0) sb_printf(&out, ".%c", "xyzw"[k->comp & 3]);
            }
            if (!part) sb_put(&out, " as one exact key");
        }
    }
    for (int i = 0; i < plan->count && !plan->exact; i++) {
        const index_key *k = &plan->keys[i];
        if (i) sb_put(&out, ", ");
        describe_path(&out, k->path);
        if (k->comp >= 0) sb_printf(&out, ".%c", "xyzw"[k->comp & 3]);
    }
    if (plan->previous) sb_put(&out, plan->exact ? ", as the tick started: its index is made once a tick"
                                                 : " as the tick started: its index is made once a tick");
    return out.data;
}

typedef struct loop_visit {
    loop_visitor fn;
    void *user;
    bool parallel; // Every parallel loop (each_parallel_loop), rather than the loops that start with a guard
} loop_visit;

static void visit_stmt(const loop_visit *v, const decl *code, const stmt *s);
static void visit_expr(const loop_visit *v, const decl *code, const expr *e)
{
    if (!e) return;
    visit_expr(v, code, e->object);
    visit_expr(v, code, e->lhs);
    visit_expr(v, code, e->rhs);
    visit_expr(v, code, e->cond);
    for (int i = 0; i < e->args.count; i++) visit_expr(v, code, e->args.items[i]);
    visit_stmt(v, code, e->block);
}

static void visit_stmt(const loop_visit *v, const decl *code, const stmt *s)
{
    if (!s) return;
    if (v->parallel) {
        if (s->kind == S_PARALLEL || (s->kind == S_FOREACH && s->parallel)) v->fn(v->user, code, s);
    } else if ((s->kind == S_FOREACH || s->kind == S_PARALLEL) && (s->index_guarded || s->indexed) && loop_index_summary(s)) {
        v->fn(v->user, code, s);
    }
    visit_expr(v, code, s->cond);
    visit_expr(v, code, s->value);
    visit_stmt(v, code, s->init);
    visit_stmt(v, code, s->step);
    visit_stmt(v, code, s->then_stmt);
    visit_stmt(v, code, s->else_stmt);
    for (int i = 0; i < s->stmts.count; i++) visit_stmt(v, code, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) visit_stmt(v, code, s->cases.items[i].body.items[k]);
    }
}

static void each_loop(const program *prog, const loop_visit v);

void each_guarded_loop(const program *prog, const loop_visitor fn, void *user)
{
    each_loop(prog, (loop_visit){fn, user, false});
}

void each_parallel_loop(const program *prog, const loop_visitor fn, void *user)
{
    each_loop(prog, (loop_visit){fn, user, true});
}

static void each_loop(const program *prog, const loop_visit v)
{
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if ((d->kind == DECL_SYSTEM || d->kind == DECL_FUNCTION) && d->body) visit_stmt(&v, d, d->body);
        for (int k = 0; k < d->methods.count; k++) {
            if (d->methods.items[k]->body) visit_stmt(&v, d->methods.items[k], d->methods.items[k]->body);
        }
    }
}

static void print_loop(void *user, const decl *code, const stmt *s)
{
    sb *out = user;
    sb_printf(out, "         " STR_FMT ", line %d: %s\n", STR_ARG(code->qualified.len ? code->qualified : code->name),
              s->at.line, loop_index_summary(s));
}

void print_loop_indexes(const program *prog, sb *out)
{
    sb lines = {0};
    each_guarded_loop(prog, print_loop, &lines);
    if (!lines.len) return;
    sb_put(out, "\nLoops that skip rows, and whether an index finds the rest:\n");
    sb_put(out, lines.data);
}
