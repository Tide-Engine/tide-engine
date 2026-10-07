#include <string.h>

#include "ast.h"

// Definite assignment, as C#'s, for the kinds of variable that start without
// a value: a function's or method's `out` parameters, what a frag and a vert
// write (a frag's `out Color`, the structs a vert takes as `out` for its
// frag), and the locals `out` arguments declare (`out var health`).
//
// An `out` parameter is assigned before the code reads it, and on every path
// that returns: `fail` owes it nothing, as a call that fails gives its `out`
// variables their type's default (codegen), and neither does a frag's path
// that ends in `pixel.Discard()`, which draws no pixel. A local an `out`
// argument declares is assigned once the call it's written in has run, which
// `&&`, `||`, `?:` and `??` can skip: it's only read where the call ran.
//
// A struct is assigned whole, or a field at a time, as in C#: it has a value
// once each of its fields does, and a field is read once it's assigned. That's
// its own fields: a field of a struct in it is a part of that field.
//
// The state is a byte per slot, a variable's, or one for each field of a
// struct, set where it's assigned on every path there. Where no path leads
// (after a return, a break, a `while (true)` with no break), every slot counts
// as assigned, as in C#, so joining such a place with another changes nothing.
// Assignments only add, so a loop's start is as what's before the loop left
// it, and one pass over the code is enough.

typedef struct flow {
    program *prog;             // For the fixes editors offer
    const decl *code;          // The function, method, frag or vert, for its `out` parameters; NULL for other code
    VEC(const void *) vars;    // Each variable: a param of `code`, or the S_VAR an `out` argument declares
    VEC(const decl *) structs; // ...the struct it is, assigned a field at a time, or NULL
    VEC(int) firsts;           // ...and its first slot
    int slots;                 // A slot a variable, or one for each field of a struct
    int params;                // The first of the variables are the parameters
    bool *reported;            // By variable: a read before its assignment was said
    VEC(uint8_t *) breaks;     // By loop and switch being gone through: what's assigned at each of its breaks
    VEC(uint8_t *) continues;  // By loop: at each of its continues
    int comparing;             // In a Sort's comparison block, whose returns are its own
} flow;

static uint8_t *state_new(const flow *f, const uint8_t value)
{
    uint8_t *s = arena_alloc((size_t)(f->slots ? f->slots : 1));
    memset(s, value, (size_t)f->slots);
    return s;
}

static uint8_t *state_copy(const flow *f, const uint8_t *from)
{
    uint8_t *s = arena_alloc((size_t)(f->slots ? f->slots : 1));
    memcpy(s, from, (size_t)f->slots);
    return s;
}

// What's assigned on both of two paths, into `into`.
static void state_meet(const flow *f, uint8_t *into, const uint8_t *other)
{
    for (int i = 0; i < f->slots; i++) into[i] &= other[i];
}

static void state_set(const flow *f, uint8_t *into, const uint8_t *from)
{
    memcpy(into, from, (size_t)f->slots);
}

// No path leads here: everything counts as assigned.
static void state_end(const flow *f, uint8_t *s)
{
    memset(s, 1, (size_t)f->slots);
}

// A variable that starts without a value: its slots, one for each field where
// it's a struct.
static void add_var(flow *f, const void *var, const type t)
{
    const decl *of = t.kind == TY_STRUCT && t.decl && t.decl->fields.count > 0 ? t.decl : NULL;
    vec_push(f->vars, var);
    vec_push(f->structs, of);
    vec_push(f->firsts, f->slots);
    f->slots += of ? of->fields.count : 1;
}

static int slot_count(const flow *f, const int v)
{
    return f->structs.items[v] ? f->structs.items[v]->fields.count : 1;
}

// Whether all of variable `v` is assigned. `missing` gets its first field
// that isn't, where it's a struct with some that are.
static bool var_assigned(const flow *f, const int v, const uint8_t *st, const field **missing)
{
    const int first = f->firsts.items[v], count = slot_count(f, v);
    int assigned = 0, lacks = -1;
    for (int i = 0; i < count; i++) {
        if (st[first + i]) assigned++;
        else if (lacks < 0) lacks = i;
    }
    const decl *of = f->structs.items[v];
    int hidden = 0; // The layout's own fields, which start assigned
    for (int i = 0; of && i < of->fields.count; i++) hidden += of->fields.items[i].hidden;
    if (missing) *missing = lacks >= 0 && assigned > hidden && of ? &of->fields.items[lacks] : NULL;
    return lacks < 0;
}

static void assign_var(const flow *f, const int v, uint8_t *st)
{
    memset(st + f->firsts.items[v], 1, (size_t)slot_count(f, v));
}

// A frag or a vert, which writes what a function assigns.
static bool gpu(const flow *f)
{
    return f->code && f->code->kind == DECL_SHADER;
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

// The slot `e` stands for where it's one field of a struct variable,
// `lamp.place`, or -1.
static int field_slot(const flow *f, const expr *e)
{
    if (!e || e->kind != E_MEMBER || !e->field || e->swizzle_len > 0) return -1;
    const int v = var_of(f, e->object);
    const decl *of = v < 0 ? NULL : f->structs.items[v];
    for (int i = 0; of && i < of->fields.count; i++) {
        if (&of->fields.items[i] == e->field) return f->firsts.items[v] + i;
    }
    return -1;
}

// What an `out` argument or a plain assignment's target names: a whole
// variable, or one field of a struct.
static bool names_place(const flow *f, const expr *e)
{
    return var_of(f, e) >= 0 || field_slot(f, e) >= 0;
}

static void assign_place(const flow *f, const expr *e, uint8_t *st)
{
    const int v = var_of(f, e);
    if (v >= 0) assign_var(f, v, st);
    else if (field_slot(f, e) >= 0) st[field_slot(f, e)] = 1;
}

static void find_stmt(flow *f, const stmt *s);

// The locals `out` arguments declare in an expression, and in the blocks
// written after its calls.
static void find_expr(flow *f, const expr *e)
{
    if (!e) return;
    if (e->kind == E_NAME && e->out_local && !e->out_local->discard) add_var(f, e->out_local, e->out_local->type);
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

// A name read, or one field of a struct (`part`, of `name.part`): it has to
// be assigned on every path here.
static void flow_read(flow *f, const expr *e, const field *part, uint8_t *st)
{
    const expr *name = part ? e->object : e;
    const int v = var_of(f, name);
    if (v < 0) return;
    const field *missing = NULL;
    if (part) {
        const int slot = field_slot(f, e);
        if (st[slot]) return;
        st[slot] = 1; // Said once along this path
    } else {
        if (var_assigned(f, v, st, &missing)) return;
        assign_var(f, v, st);
    }
    if (f->reported[v]) return;
    f->reported[v] = true;
    if (v >= f->params) {
        diag_error(e->at, "'" STR_FMT "' isn't assigned here: the call that makes it may not have run", STR_ARG(name->name));
        diag_note("it's assigned once the call that takes 'out " STR_FMT "' has run, which '&&', '||', '?:' and '?\?' can "
                  "skip: read '" STR_FMT "' where that call ran, like in the 'if' it's the condition of",
                  STR_ARG(name->name), STR_ARG(name->name));
        return;
    }
    const param *p = name->param;
    const char *done = gpu(f) ? "written" : "assigned";
    if (part) {
        diag_error(e->at, "'" STR_FMT "." STR_FMT "' is read before it's %s", STR_ARG(name->name), STR_ARG(part->name), done);
    } else if (missing) {
        diag_error(e->at, "'" STR_FMT "' is read before its '" STR_FMT "' is %s", STR_ARG(name->name), STR_ARG(missing->name), done);
    } else {
        diag_error(e->at, "'" STR_FMT "' is read before it's %s", STR_ARG(name->name), done);
    }
    if (gpu(f) && f->code->is_frag) {
        diag_note("a frag's 'out Color' starts with no value, where 'mut Color' started white: write it first, like '" STR_FMT
                  " = Color.white;'", STR_ARG(name->name));
    } else if (gpu(f)) {
        diag_note("what a vert writes for its frag starts with no value: write it before reading it");
    } else {
        diag_note("an 'out' parameter starts unassigned: what the caller's variable held isn't passed in. Assign it first, "
                  "or take it as 'mut " STR_FMT " " STR_FMT "' to read what the caller has",
                  STR_ARG(p->type_name), STR_ARG(name->name));
    }
    const fix first = {.kind = FIX_ASSIGN_FIRST, .at = e->at, .param = p, .method = f->code};
    vec_push(f->prog->fixes, first);
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
        if (arg->is_out && names_place(f, arg)) continue; // Written, not read
        flow_expr(f, arg, st);
    }
    const bool sort = e->call == CALL_LIST && str_eq_c(e->name, "Sort");
    flow_block(f, e->block, st, sort);
    for (int i = 0; i < count; i++) {
        if (args[i]->is_out) assign_place(f, args[i], st);
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
        flow_read(f, e, NULL, st);
        break;
    case E_MEMBER:
        if (field_slot(f, e) >= 0) flow_read(f, e, e->field, st); // One field of a struct: its others aren't read
        else flow_expr(f, e->object, st);
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

// A function's or method's `out` parameters at a place it returns from, and
// what a frag or a vert writes at a place it ends.
static void flow_leave(const flow *f, const loc at, const uint8_t *st, const bool at_end)
{
    for (int i = 0; i < f->params; i++) {
        const field *missing = NULL;
        if (var_assigned(f, i, st, &missing)) continue;
        const param *p = f->vars.items[i];
        sb what = {0}; // The parameter, or the field of it that's left: 'lamp.place'
        if (missing) sb_printf(&what, "'" STR_FMT "." STR_FMT "'", STR_ARG(p->name), STR_ARG(missing->name));
        else sb_printf(&what, "%s'" STR_FMT "'", gpu(f) ? "" : "its ", STR_ARG(p->name));
        const char *ends = at_end ? "can end" : "returns here";
        if (gpu(f) && f->code->is_frag && p->output > 0) { // One of its outputs after the pixel's color
            static const char *const which[] = {"first", "second", "third", "fourth"};
            diag_error(at, "frag '" STR_FMT "' %s without writing %s, its %s output", STR_ARG(f->code->name), ends, what.data,
                       which[p->output < 4 ? p->output : 3]);
            diag_note("a frag writes each of its outputs on every path that ends; one that draws nothing leaves the pixel "
                      "with 'pixel.Discard();', through a 'Pixel pixel' parameter");
        } else if (gpu(f) && f->code->is_frag) {
            diag_error(at, "frag '" STR_FMT "' %s without writing its pixel's color, %s", STR_ARG(f->code->name), ends, what.data);
            diag_note("a frag writes its color on every path that ends; one that draws nothing leaves the pixel with "
                      "'pixel.Discard();', through a 'Pixel pixel' parameter");
        } else if (gpu(f)) {
            diag_error(at, "vert '" STR_FMT "' %s without writing %s", STR_ARG(f->code->name), ends, what.data);
            diag_note("what a vert writes for its frag, it writes on every path, each field of it: '" STR_FMT " = default;' "
                      "fills it where there's nothing to give", STR_ARG(p->name));
        } else {
            diag_error(at, "'" STR_FMT "' %s without assigning %s", STR_ARG(f->code->name), ends, what.data);
            diag_note("an 'out' parameter is assigned on every path that returns: '" STR_FMT " = default;' fills it where "
                      "there's nothing to give", STR_ARG(p->name));
        }
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
        flow_expr(f, s->value, st);
        break;
    case S_EXPR:
        flow_expr(f, s->value, st);
        // pixel.Discard(): no pixel is drawn, so nothing is owed the frag's color from here
        if (s->value && s->value->kind == E_METHOD && s->value->call == CALL_DISCARD) state_end(f, st);
        break;
    case S_ASSIGN:
        if (s->op == T_ASSIGN && names_place(f, s->target)) { // The whole variable, or a struct's field: not read
            flow_expr(f, s->value, st);
            assign_place(f, s->target, st);
            break;
        }
        flow_expr(f, s->target, st); // A part of it, or what it holds changed: it has a value first
        flow_expr(f, s->value, st);
        break;
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
        if (code->params.items[i].is_out) add_var(&f, &code->params.items[i], code->params.items[i].type);
    }
    f.params = f.vars.count;
    find_stmt(&f, body);
    if (f.vars.count == 0) return;
    f.reported = arena_alloc(sizeof(bool) * (size_t)f.vars.count);
    uint8_t *st = state_new(&f, 0);
    for (int v = 0; v < f.vars.count; v++) { // What a struct keeps for its layout is no field to assign
        const decl *of = f.structs.items[v];
        for (int k = 0; of && k < of->fields.count; k++) st[f.firsts.items[v] + k] = of->fields.items[k].hidden;
    }
    flow_stmt(&f, body, st);
    // One that returns a value ends at a return, on every path, or the checker says so
    if (code && code->return_type.kind == TY_VOID) flow_leave(&f, code->at, st, true);
}
