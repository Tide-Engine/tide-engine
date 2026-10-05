#include <string.h>

#include "ast.h"

// Definite assignment, as C#'s, for the two kinds of variable that start
// without a value: a function's or method's `out` parameters, and the locals
// `out` arguments declare (`out var health`).
//
// An `out` parameter is assigned before the code reads it, and on every path
// that returns: `fail` owes it nothing, as a call that fails gives its `out`
// variables their type's default (codegen). A local an `out` argument declares
// is assigned once the call it's written in has run, which `&&`, `||`, `?:`
// and `??` can skip: it's only read where the call ran.
//
// The state is a byte per variable, set where it's assigned on every path
// there. Where no path leads (after a return, a break, a `while (true)` with no
// break), every variable counts as assigned, as in C#, so joining such a place
// with another changes nothing. Assignments only add, so a loop's start is as
// what's before the loop left it, and one pass over the code is enough.

typedef struct flow {
    program *prog;            // For the fixes editors offer
    const decl *code;         // The function or method, for its `out` parameters; NULL for other code
    VEC(const void *) vars;   // Each variable: a param of `code`, or the S_VAR an `out` argument declares
    int params;               // ...the first of them are the parameters
    bool *reported;           // By variable: a read before its assignment was said
    VEC(uint8_t *) breaks;    // By loop and switch being gone through: what's assigned at each of its breaks
    VEC(uint8_t *) continues; // By loop: at each of its continues
    int comparing;            // In a Sort's comparison block, whose returns are its own
} flow;

static uint8_t *state_new(const flow *f, const uint8_t value)
{
    uint8_t *s = arena_alloc((size_t)(f->vars.count ? f->vars.count : 1));
    memset(s, value, (size_t)f->vars.count);
    return s;
}

static uint8_t *state_copy(const flow *f, const uint8_t *from)
{
    uint8_t *s = arena_alloc((size_t)(f->vars.count ? f->vars.count : 1));
    memcpy(s, from, (size_t)f->vars.count);
    return s;
}

// What's assigned on both of two paths, into `into`.
static void state_meet(const flow *f, uint8_t *into, const uint8_t *other)
{
    for (int i = 0; i < f->vars.count; i++) into[i] &= other[i];
}

static void state_set(const flow *f, uint8_t *into, const uint8_t *from)
{
    memcpy(into, from, (size_t)f->vars.count);
}

// No path leads here: everything counts as assigned.
static void state_end(const flow *f, uint8_t *s)
{
    memset(s, 1, (size_t)f->vars.count);
}

// Which variable a name is, or -1 for one that always has a value.
static int var_of(const flow *f, const expr *e)
{
    if (!e || e->kind != E_NAME) return -1;
    const void *var = NULL;
    if (e->bind == BIND_PARAM && e->param && e->param->is_out) var = e->param;
    else if (e->bind == BIND_LOCAL && e->local && e->local->out_decl && !e->local->discard) var = e->local;
    for (int i = 0; var && i < f->vars.count; i++) {
        if (f->vars.items[i] == var) return i;
    }
    return -1;
}

static void find_stmt(flow *f, const stmt *s);

// The locals `out` arguments declare in an expression, and in the blocks
// written after its calls.
static void find_expr(flow *f, const expr *e)
{
    if (!e) return;
    if (e->kind == E_NAME && e->out_local && !e->out_local->discard) vec_push(f->vars, e->out_local);
    find_expr(f, e->object);
    find_expr(f, e->lhs);
    find_expr(f, e->rhs);
    find_expr(f, e->cond);
    const int count = e->settled ? e->written.count : e->args.count;
    expr *const *args = e->settled ? e->written.items : e->args.items;
    for (int i = 0; i < count; i++) find_expr(f, args[i]);
    for (int i = 0; i < e->inits.count; i++) find_expr(f, e->inits.items[i].value);
    find_stmt(f, e->block);
}

static void find_stmt(flow *f, const stmt *s)
{
    if (!s) return;
    for (int i = 0; i < s->stmts.count; i++) find_stmt(f, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) find_stmt(f, s->cases.items[i].body.items[k]);
    }
    find_expr(f, s->cond);
    find_expr(f, s->value);
    find_expr(f, s->target);
    find_expr(f, s->by);
    find_expr(f, s->offset);
    find_stmt(f, s->init);
    find_stmt(f, s->step);
    find_stmt(f, s->then_stmt);
    find_stmt(f, s->else_stmt);
}

static void flow_stmt(flow *f, const stmt *s, uint8_t *st);
static void flow_expr(flow *f, const expr *e, uint8_t *st);

// A name read: it has to be assigned on every path here.
static void flow_read(flow *f, const expr *e, uint8_t *st)
{
    const int v = var_of(f, e);
    if (v < 0 || st[v]) return;
    st[v] = 1; // Said once along this path
    if (f->reported[v]) return;
    f->reported[v] = true;
    if (v < f->params) {
        diag_error(e->at, "'" STR_FMT "' is read before it's assigned", STR_ARG(e->name));
        diag_note("an 'out' parameter starts unassigned: what the caller's variable held isn't passed in. Assign it first, "
                  "or take it as 'mut " STR_FMT " " STR_FMT "' to read what the caller has",
                  STR_ARG(e->param->type_name), STR_ARG(e->name));
        const fix first = {.kind = FIX_ASSIGN_FIRST, .at = e->at, .param = e->param, .method = f->code};
        vec_push(f->prog->fixes, first);
    } else {
        diag_error(e->at, "'" STR_FMT "' isn't assigned here: the call that makes it may not have run", STR_ARG(e->name));
        diag_note("it's assigned once the call that takes 'out " STR_FMT "' has run, which '&&', '||', '?:' and '?\?' can "
                  "skip: read '" STR_FMT "' where that call ran, like in the 'if' it's the condition of",
                  STR_ARG(e->name), STR_ARG(e->name));
    }
}

// A block written after a call, which the call runs when it does, as often as
// it does: what it assigns isn't assigned after the call.
static void flow_block(flow *f, const stmt *block, const uint8_t *st, const bool comparing)
{
    if (!block) return;
    uint8_t *inside = state_copy(f, st);
    f->comparing += comparing;
    flow_stmt(f, block, inside);
    f->comparing -= comparing;
}

// A call: the value a method is called on, then its arguments as written.
// What `out` arguments name is assigned once the call has run.
static void flow_call(flow *f, const expr *e, uint8_t *st)
{
    if (e->kind == E_METHOD) flow_expr(f, e->object, st);
    const int count = e->settled ? e->written.count : e->args.count;
    expr *const *args = e->settled ? e->written.items : e->args.items;
    for (int i = 0; i < count; i++) {
        const expr *arg = args[i];
        if (arg->is_out && var_of(f, arg) >= 0) continue; // Written, not read
        flow_expr(f, arg, st);
    }
    const bool sort = e->call == CALL_LIST && str_eq_c(e->name, "Sort");
    flow_block(f, e->block, st, sort);
    for (int i = 0; i < count; i++) {
        const int v = args[i]->is_out ? var_of(f, args[i]) : -1;
        if (v >= 0) st[v] = 1;
    }
}

// A condition: what's assigned where it's true, and where it's false.
static void flow_cond(flow *f, const expr *e, const uint8_t *st, uint8_t *when_true, uint8_t *when_false)
{
    if (e->kind == E_BINARY && (e->op == T_AND || e->op == T_OR) && !e->method) {
        uint8_t *left_true = state_new(f, 0);
        uint8_t *left_false = state_new(f, 0);
        flow_cond(f, e->lhs, st, left_true, left_false);
        uint8_t *right_true = state_new(f, 0);
        uint8_t *right_false = state_new(f, 0);
        if (e->op == T_AND) { // The right side runs where the left is true
            flow_cond(f, e->rhs, left_true, right_true, right_false);
            state_set(f, when_true, right_true);
            state_set(f, when_false, left_false);
            state_meet(f, when_false, right_false);
        } else {
            flow_cond(f, e->rhs, left_false, right_true, right_false);
            state_set(f, when_true, left_true);
            state_meet(f, when_true, right_true);
            state_set(f, when_false, right_false);
        }
        return;
    }
    if (e->kind == E_UNARY && e->op == T_NOT && !e->method) {
        flow_cond(f, e->lhs, st, when_false, when_true);
        return;
    }
    if (e->kind == E_BOOL) { // No path leads where a constant isn't
        state_set(f, when_true, st);
        state_set(f, when_false, st);
        state_end(f, e->bool_value ? when_false : when_true);
        return;
    }
    if (e->kind == E_CONDITIONAL) {
        uint8_t *cond_true = state_new(f, 0);
        uint8_t *cond_false = state_new(f, 0);
        flow_cond(f, e->cond, st, cond_true, cond_false);
        uint8_t *other_true = state_new(f, 0);
        uint8_t *other_false = state_new(f, 0);
        flow_cond(f, e->lhs, cond_true, when_true, when_false);
        flow_cond(f, e->rhs, cond_false, other_true, other_false);
        state_meet(f, when_true, other_true);
        state_meet(f, when_false, other_false);
        return;
    }
    uint8_t *after = state_copy(f, st);
    flow_expr(f, e, after);
    state_set(f, when_true, after);
    state_set(f, when_false, after);
}

static void flow_expr(flow *f, const expr *e, uint8_t *st)
{
    if (!e) return;
    switch (e->kind) {
    case E_NAME:
        flow_read(f, e, st);
        break;
    case E_MEMBER:
        flow_expr(f, e->object, st);
        break;
    case E_INDEX:
        flow_expr(f, e->object, st);
        flow_expr(f, e->lhs, st);
        break;
    case E_CALL:
    case E_METHOD:
        flow_call(f, e, st);
        break;
    case E_BINARY:
        if ((e->op == T_AND || e->op == T_OR) && !e->method) { // Its right side may not run
            uint8_t *when_true = state_new(f, 0);
            uint8_t *when_false = state_new(f, 0);
            flow_cond(f, e, st, when_true, when_false);
            state_set(f, st, when_true);
            state_meet(f, st, when_false);
            break;
        }
        flow_expr(f, e->lhs, st);
        flow_expr(f, e->rhs, st);
        break;
    case E_UNARY:
    case E_IS:
    case E_TRY: // Where it fails, the code ends there, with nothing owed
    case E_DEFAULTED:
    case E_AWAIT:
        flow_expr(f, e->lhs, st);
        break;
    case E_CONDITIONAL: {
        uint8_t *when_true = state_new(f, 0);
        uint8_t *when_false = state_new(f, 0);
        flow_cond(f, e->cond, st, when_true, when_false);
        flow_expr(f, e->lhs, when_true);
        flow_expr(f, e->rhs, when_false);
        state_set(f, st, when_true);
        state_meet(f, st, when_false);
        break;
    }
    case E_COALESCE: { // Its right side only runs when the left has no value
        flow_expr(f, e->lhs, st);
        uint8_t *right = state_copy(f, st);
        flow_expr(f, e->rhs, right);
        break;
    }
    case E_LITERAL:
        for (int i = 0; i < e->inits.count; i++) flow_expr(f, e->inits.items[i].value, st);
        break;
    case E_INTERP:
    case E_LIST:
        for (int i = 0; i < e->args.count; i++) flow_expr(f, e->args.items[i], st);
        break;
    default:
        break;
    }
}

// A function's or method's `out` parameters at a place it returns from.
static void flow_leave(const flow *f, const loc at, const uint8_t *st, const bool at_end)
{
    for (int i = 0; i < f->params; i++) {
        if (st[i]) continue;
        const param *p = f->vars.items[i];
        if (at_end) {
            diag_error(at, "'" STR_FMT "' can end without assigning its '" STR_FMT "'", STR_ARG(f->code->name), STR_ARG(p->name));
        } else {
            diag_error(at, "'" STR_FMT "' returns here without assigning its '" STR_FMT "'", STR_ARG(f->code->name), STR_ARG(p->name));
        }
        diag_note("an 'out' parameter is assigned on every path that returns: '" STR_FMT " = default;' fills it where there's "
                  "nothing to give", STR_ARG(p->name));
        const fix assign = {.kind = FIX_ASSIGN_OUT, .at = at, .param = p, .method = f->code};
        vec_push(f->prog->fixes, assign);
    }
}

// A loop's body, and what's assigned at its breaks (`at_break`) and where its
// next round starts (`at_next`): its end, and its continues.
static void flow_loop_body(flow *f, const stmt *body, uint8_t *st, uint8_t *at_break, uint8_t *at_next)
{
    state_end(f, at_break);
    state_end(f, at_next);
    vec_push(f->breaks, at_break);
    vec_push(f->continues, at_next);
    flow_stmt(f, body, st);
    f->breaks.count--;
    f->continues.count--;
    state_meet(f, at_next, st);
}

static void flow_stmt(flow *f, const stmt *s, uint8_t *st)
{
    if (!s) return;
    switch (s->kind) {
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) flow_stmt(f, s->stmts.items[i], st);
        break;
    case S_IF: {
        uint8_t *when_true = state_new(f, 0);
        uint8_t *when_false = state_new(f, 0);
        flow_cond(f, s->cond, st, when_true, when_false);
        flow_stmt(f, s->then_stmt, when_true);
        flow_stmt(f, s->else_stmt, when_false);
        state_set(f, st, when_true);
        state_meet(f, st, when_false);
        break;
    }
    case S_WHILE:
    case S_FOR: {
        flow_stmt(f, s->init, st);
        uint8_t *when_true = state_copy(f, st);
        uint8_t *when_false = state_copy(f, st);
        if (s->cond) flow_cond(f, s->cond, st, when_true, when_false);
        else state_end(f, when_false); // for (;;) only ends at a break
        uint8_t *at_break = state_new(f, 1);
        uint8_t *at_next = state_new(f, 1);
        flow_loop_body(f, s->then_stmt, when_true, at_break, at_next);
        flow_stmt(f, s->step, at_next);
        state_set(f, st, when_false);
        state_meet(f, st, at_break);
        break;
    }
    case S_FOREACH:
    case S_PARALLEL: { // It may have nothing to go through: what its steps assign isn't assigned after it
        flow_expr(f, s->value, st);
        flow_expr(f, s->by, st);
        flow_expr(f, s->offset, st);
        uint8_t *inside = state_copy(f, st);
        uint8_t *at_break = state_new(f, 1);
        uint8_t *at_next = state_new(f, 1);
        flow_loop_body(f, s->then_stmt, inside, at_break, at_next);
        break;
    }
    case S_SWITCH: {
        flow_expr(f, s->cond, st);
        uint8_t *after = state_new(f, 1);
        bool has_default = false;
        vec_push(f->breaks, after);
        for (int i = 0; i < s->cases.count; i++) {
            const switch_case *section = &s->cases.items[i];
            for (int k = 0; k < section->labels.count; k++) has_default |= section->labels.items[k] == NULL;
            uint8_t *inside = state_copy(f, st);
            for (int k = 0; k < section->body.count; k++) flow_stmt(f, section->body.items[k], inside);
            state_meet(f, after, inside); // A section that runs on is an error of its own
        }
        f->breaks.count--;
        if (!has_default) state_meet(f, after, st); // No case may be the value's
        state_set(f, st, after);
        break;
    }
    case S_VAR:
    case S_EXPR:
        flow_expr(f, s->value, st);
        break;
    case S_ASSIGN: {
        const int v = var_of(f, s->target);
        if (v >= 0 && s->op == T_ASSIGN) { // The whole variable, which isn't read
            flow_expr(f, s->value, st);
            st[v] = 1;
            break;
        }
        flow_expr(f, s->target, st); // A part of it, or what it holds changed: it has a value first
        flow_expr(f, s->value, st);
        break;
    }
    case S_RETURN:
        flow_expr(f, s->value, st);
        if (f->comparing == 0) flow_leave(f, s->at, st, false);
        state_end(f, st);
        break;
    case S_FAIL: // Owes its `out` parameters nothing
        flow_expr(f, s->value, st);
        state_end(f, st);
        break;
    case S_BREAK:
        if (f->breaks.count > 0) state_meet(f, f->breaks.items[f->breaks.count - 1], st);
        state_end(f, st);
        break;
    case S_CONTINUE:
        if (f->continues.count > 0) state_meet(f, f->continues.items[f->continues.count - 1], st);
        state_end(f, st);
        break;
    }
}

void check_assigned(program *prog, const decl *code, const stmt *body)
{
    if (!body) return;
    flow f = {0};
    f.prog = prog;
    f.code = code;
    for (int i = 0; code && i < code->params.count; i++) {
        if (code->params.items[i].is_out) vec_push(f.vars, &code->params.items[i]);
    }
    f.params = f.vars.count;
    find_stmt(&f, body);
    if (f.vars.count == 0) return;
    f.reported = arena_alloc(sizeof(bool) * (size_t)f.vars.count);
    uint8_t *st = state_new(&f, 0);
    flow_stmt(&f, body, st);
    // One that returns a value ends at a return, on every path, or the checker says so
    if (code && code->return_type.kind == TY_VOID) flow_leave(&f, code->at, st, true);
}
