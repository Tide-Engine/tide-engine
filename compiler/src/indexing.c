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
    VEC(const decl *) world_sets; // Components the loop sets through a World
    bool world_sets_any;          // ...or a call that may set any
    VEC(const stmt *) inner;      // Locals the loop's body declares
    const void *env_from[MOST_ENV]; // A read-only local or a parameter of an inlined function...
    const expr *env_to[MOST_ENV];   // ...and what it stands for
    int env_count;
    const decl *inlining[MOST_INLINE];
    int inline_depth;
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
    if (t.kind == TY_ENUM || t.kind == TY_BOOL || t.kind == TY_CHAR) return true;
    return type_dim(t) == 1 && (type_is_float_based(t) || type_is_double_based(t) || type_is_integer(t));
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

static const expr *env_find(const ictx *c, const expr *e)
{
    const void *target = name_target(e);
    if (!target) return NULL;
    for (int i = c->env_count - 1; i >= 0; i--) {
        if (c->env_from[i] == target) return c->env_to[i];
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

// Whether the loop changes anything of the variable (a local or a parameter) that `read` reads; `read` NULL: anything of it.
static bool loop_changes(const ictx *c, const void *variable, const expr *read)
{
    for (int i = 0; i < c->written.count; i++) {
        const expr *target = c->written.items[i];
        if (name_target(place_root(target)) != variable) continue;
        if (!read || places_overlap(target, read)) return true;
    }
    return false;
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
            if (d->params.items[i].mode == PARAM_MUT || d->params.items[i].is_out) note_written(c, e->args.items[i]);
            if (e->args.items[i]->type.kind == TY_WORLD) note_world_sets(c, d);
        }
        if (e->call == CALL_METHOD && d->is_mut_method) note_written(c, e->object);
    }
    if (e->call == CALL_LIST || e->call == CALL_GUI) note_written(c, e->object);
    if (e->call == CALL_WORLD && str_eq_c(e->name, "Set")) {
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

static void add_constraint(constraints *set, const expr *path, const int comp, const type t, const bound *lo,
                           const bound *hi)
{
    for (int i = 0; i < set->count; i++) {
        constraint *k = &set->items[i];
        if (k->comp != comp || !same_place(k->path, path)) continue;
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
    if (!relation(c, a, op, b, &out)) relation(c, b, flipped(op), a, &out);
    return out;
}

// Bounds that hold whenever the condition is `want`.
static constraints derive(ictx *c, const expr *e, const bool want)
{
    constraints none = {0};
    e = look(c, e);
    if (!e) return none;
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
            if ((d->params.items[i].mode == PARAM_MUT || d->params.items[i].is_out) && names_param(e->args.items[i], p)) {
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

// Whether `from` calls `target`, directly or through other functions.
typedef VEC(const decl *) decl_list;
static bool calls_expr(const expr *e, const decl *target, decl_list *seen);
static bool calls_stmt(const stmt *s, const decl *target, decl_list *seen)
{
    if (!s) return false;
    if (calls_expr(s->cond, target, seen) || calls_expr(s->value, target, seen) || calls_expr(s->target, target, seen)) return true;
    if (calls_stmt(s->init, target, seen) || calls_stmt(s->step, target, seen)) return true;
    if (calls_stmt(s->then_stmt, target, seen) || calls_stmt(s->else_stmt, target, seen)) return true;
    for (int i = 0; i < s->stmts.count; i++) {
        if (calls_stmt(s->stmts.items[i], target, seen)) return true;
    }
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) {
            if (calls_stmt(s->cases.items[i].body.items[k], target, seen)) return true;
        }
    }
    return false;
}

static bool calls_expr(const expr *e, const decl *target, decl_list *seen)
{
    if (!e) return false;
    if ((e->call == CALL_FUNCTION || e->call == CALL_METHOD) && e->method) {
        if (e->method == target) return true;
        bool visited = false;
        for (int i = 0; i < seen->count; i++) visited |= seen->items[i] == e->method;
        if (!visited) {
            vec_push(*seen, e->method);
            if (calls_stmt(e->method->body, target, seen)) return true;
        }
    }
    if (calls_expr(e->object, target, seen) || calls_expr(e->lhs, target, seen) || calls_expr(e->rhs, target, seen)
        || calls_expr(e->cond, target, seen)) {
        return true;
    }
    for (int i = 0; i < e->args.count; i++) {
        if (calls_expr(e->args.items[i], target, seen)) return true;
    }
    return calls_stmt(e->block, target, seen) || calls_stmt(e->step, target, seen);
}

// A query's key must keep its value while the code it's in runs, between one
// time the loop starts and the next: the index made at one is used at the
// next while nothing changes the pages it was made from.
static bool query_source_still(ictx *c, const loop_index *plan)
{
    const decl *component = plan->component;
    const field *f = path_field(c, plan->keys[0].path);
    if (!component) {
        because(c, c->loop->at, "its key isn't a component's field");
        return false;
    }
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
        decl_list seen = {0};
        const bool reaches = calls_stmt(d->body, c->code, &seen);
        if (reaches && changes_own_field(d, component, f, &at)) {
            because(c, at, "'" STR_FMT "' changes " STR_FMT "%s" STR_FMT " of its own entity as it goes, and calls this "
                           "code, whose loop finds rows by it: change it in a system of its own", STR_ARG(d->name),
                    STR_ARG(component->name), dot, STR_ARG(fname));
            return false;
        }
    }
    return true;
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

// Picks the keys: two or three bounded both ways (a grid), else an equality,
// else one bounded both ways, else one bounded one way.
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
        plan->component = query ? path_component(&c, plan->keys[0].path) : NULL;
        const bool still = query ? query_source_still(&c, plan) : list_source_still(&c);
        if (still) loop->index = plan;
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
    for (int i = 0; i < plan->count; i++) {
        const index_key *k = &plan->keys[i];
        if (i) sb_put(&out, ", ");
        describe_path(&out, k->path);
        if (k->comp >= 0) sb_printf(&out, ".%c", "xyzw"[k->comp & 3]);
    }
    return out.data;
}

typedef struct loop_visit {
    loop_visitor fn;
    void *user;
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
    if ((s->kind == S_FOREACH || s->kind == S_PARALLEL) && (s->index_guarded || s->indexed) && loop_index_summary(s)) {
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

void each_guarded_loop(const program *prog, const loop_visitor fn, void *user)
{
    const loop_visit v = {fn, user};
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
