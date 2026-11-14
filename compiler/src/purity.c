#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "purity.h"
#include "types.h"

// Pure functions: see purity.h, docs/spec.md (Pure functions) and
// notes/pure-functions.md.
//
// Purity is a whitelist. A body is gone through once every body is checked,
// and anything in it this file doesn't know to be free of effects and to read
// only what the code was given makes its function not pure: a construct added
// to the language later is impure until it's listed here. Writes need no rule
// of their own: with no `mut` or `out` parameter (and a method not `mut`),
// the checker already refuses every write but one to a `mut` local, which is
// the function's own.
//
// The flags the checker keeps of what code does through its calls (acts,
// rolls, calls_c and the rest) are checked as well, after the walk: they're
// what the rest of the compiler goes by, so a function this file's walk would
// let through that one of them marks is not pure either.

// ---------------------------------------------------------------------------
// What keeps a function from being pure

typedef struct called {
    decl *to;
    loc at;
} called;

typedef struct pctx {
    const program *prog;
    decl *code;
    VEC(called) calls; // The functions and methods the code calls, where it first calls each
} pctx;

static const char *text(const char *fmt, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    return str_to_cstr(str_from(buffer));
}

// The first thing found is the one said.
static void impure(pctx *c, const loc at, const char *why)
{
    if (!c->code->pure) return;
    c->code->pure = false;
    c->code->impure_why = why;
    c->code->impure_at = at;
    c->code->impure_through = NULL;
}

static void calls(pctx *c, decl *to, const loc at)
{
    if (!to) return;
    for (int i = 0; i < c->calls.count; i++) {
        if (c->calls.items[i].to == to) return;
    }
    const called call = {to, at};
    vec_push(c->calls, call);
}

// A value that is this machine's handle to something outside the code: what
// it names can change with no write the code makes.
static const char *outside_type(const type t)
{
    switch (t.kind) {
    case TY_WORLD: return "reads the World";
    case TY_QUERY: return "reads a query's entities";
    case TY_ACTION: return "runs an Action";
    case TY_DRAW_LIST: return "uses the frame's draw list";
    case TY_TEXTURE3D: case TY_TEXTURE: case TY_DEPTH_TEXTURE: case TY_CUBEMAP: case TY_TEXTURE_ARRAY: case TY_DEPTH_TEXTURE_ARRAY:
        return "uses a texture, which is this machine's";
    case TY_RENDER_TARGET: return "uses a render target, which is this machine's";
    case TY_GPU_LIST: return "uses a GPU list, which is this machine's";
    case TY_FONT: return "uses a font, which is this machine's";
    case TY_RESIDENT: return "uses a C library's state";
    case TY_RECORD: return "reads Devices";
    default: return NULL;
    }
}

// The names whose functions only work a value out of their arguments: Math,
// Path, and the numeric types' own (quaternion.AxisAngle, float4x4.TRS).
static bool pure_owner(const str name)
{
    if (str_eq_c(name, "Math") || str_eq_c(name, "Path") || str_eq_c(name, "string")) return true;
    type t;
    if (!builtin_type_named(name, &t)) return false;
    return type_dim(t) > 0 || matrix_dim(t) > 0 || t.kind == TY_QUATERNION || t.kind == TY_COLOR || t.kind == TY_RECT
        || t.kind == TY_BOOL || t.kind == TY_LONG || t.kind == TY_CHAR || t.kind == TY_STRING;
}

// A name that is no variable: Math, a type, a namespace.
static bool owner_name(const expr *e)
{
    return e && e->kind == E_NAME && (e->bind == BIND_NONE || e->bind == BIND_TYPE || e->bind == BIND_NAMESPACE);
}

static void walk_stmt(pctx *c, const stmt *s);

static void walk_expr(pctx *c, const expr *e)
{
    if (!e || !c->code->pure) return;
    const char *outside = outside_type(e->type);
    if (outside) {
        impure(c, e->at, outside);
        return;
    }
    if (e->lambda) {
        impure(c, e->at, "makes a function value");
        return;
    }
    if (e->gpu) {
        impure(c, e->at, "uses a GPU list, which is this machine's");
        return;
    }
    switch (e->kind) {
    case E_INT: case E_FLOAT: case E_BOOL: case E_STRING: case E_DEFAULT: case E_NULL: case E_HAS: case E_THIS:
        return;
    case E_NAME:
        if (e->bind == BIND_DEVICES) impure(c, e->at, "reads Devices");
        else if (e->bind == BIND_FIELD) impure(c, e->at, "reads the input's fields");
        return;
    case E_MEMBER:
        if (e->local_field) {
            impure(c, e->at, text("reads the local field '" STR_FMT "', which is this machine's", STR_ARG(e->member)));
            return;
        }
        if (e->edge != EDGE_NONE || e->typed_text) {
            impure(c, e->at, "reads Devices");
            return;
        }
        if (e->task_completion) {
            impure(c, e->at, "reads a task's state");
            return;
        }
        if (e->property) { // A getter not yet a call: its code is the getter's
            if (!e->property->get) {
                impure(c, e->at, text("reads the property '" STR_FMT "'", STR_ARG(e->member)));
                return;
            }
            calls(c, e->property->get, e->at);
        }
        if (owner_name(e->object)) {
            // Page.Title and quaternion.identity are constants; Frame.dt, Random.seed and Cursor.locked aren't
            if (e->enum_member || e->bind == BIND_CONST || e->bind == BIND_ASSET || e->object->bind == BIND_NAMESPACE) return;
            if (e->object->bind == BIND_NONE && !pure_owner(e->object->name)) {
                impure(c, e->at, text("reads " STR_FMT "." STR_FMT, STR_ARG(e->object->name), STR_ARG(e->member)));
            }
            return;
        }
        walk_expr(c, e->object);
        return;
    case E_INDEX:
        walk_expr(c, e->object);
        walk_expr(c, e->lhs);
        return;
    case E_UNARY:
    case E_BINARY:
        if (e->method) calls(c, e->method, e->at);
        walk_expr(c, e->lhs);
        walk_expr(c, e->rhs);
        return;
    case E_CONDITIONAL:
        walk_expr(c, e->cond);
        walk_expr(c, e->lhs);
        walk_expr(c, e->rhs);
        return;
    case E_COALESCE: case E_TRY: case E_DEFAULTED: case E_IS:
        walk_expr(c, e->lhs);
        walk_expr(c, e->rhs);
        return;
    case E_STEP: // ++i: of a `mut` local, the only thing the code could write
        walk_expr(c, e->lhs);
        walk_stmt(c, e->step);
        return;
    case E_SWITCH:
        walk_expr(c, e->object);
        for (int i = 0; i < e->inits.count; i++) walk_expr(c, e->inits.items[i].value);
        for (int i = 0; i < e->args.count; i++) walk_expr(c, e->args.items[i]);
        return;
    case E_INTERP:
    case E_LIST:
        for (int i = 0; i < e->args.count; i++) walk_expr(c, e->args.items[i]);
        return;
    case E_LITERAL:
        for (int i = 0; i < e->inits.count; i++) walk_expr(c, e->inits.items[i].value);
        return;
    case E_AWAIT:
        impure(c, e->at, "waits");
        return;
    case E_CALL:
    case E_METHOD:
        break;
    }
    if (e->task_start_value) {
        impure(c, e->at, "starts a task");
        return;
    }
    if (e->resource_operation || e->io_operation || e->web_fetch || e->storage_flush) {
        impure(c, e->at, e->io_operation ? "reads or writes files" : "uses this machine's resources");
        return;
    }
    if (e->block) { // A widget's or an Action's: code of the caller's that the call runs
        impure(c, e->at, text("runs a block with '" STR_FMT "'", STR_ARG(e->name)));
        return;
    }
    switch (e->call) {
    case CALL_CONSTRUCT:
    case CALL_ENUM_COUNT:
    case CALL_TEXT:
    case CALL_LIST:  // A list, a map, a set or a grid it could change is a `mut` local of its own
    case CALL_MAP:
    case CALL_GRID:
    case CALL_NEW_GRID:
    case CALL_NEW_MESH:
    case CALL_MESH_DECODE:
    case CALL_JSON:
    case CALL_RANDOM: // A generator of its own, from a seed it was given
    case CALL_SKINNED:
        break;
    case CALL_BUILTIN:
        if (e->kind == E_CALL) { // Random(seed): a generator from its seed alone
            if (e->c_callee && strcmp(e->c_callee, "tide_random_seeded") == 0) break;
            impure(c, e->at, text("calls '" STR_FMT "', which the engine runs", STR_ARG(e->name)));
            return;
        }
        if (owner_name(e->object)) {
            if (str_eq_c(e->object->name, "Debug") && str_eq_c(e->name, "Assert")) break; // Says nothing while it holds
            if (!pure_owner(e->object->name)) {
                impure(c, e->at, str_eq_c(e->object->name, "Debug") ? "writes to the log"
                                 : text("calls " STR_FMT "." STR_FMT, STR_ARG(e->object->name), STR_ARG(e->name)));
                return;
            }
            for (int i = 0; i < e->args.count; i++) walk_expr(c, e->args.items[i]);
            return;
        }
        break; // A value's own: position.Normalized()
    case CALL_FUNCTION:
    case CALL_METHOD:
        if (!e->method) {
            impure(c, e->at, "calls through a function value");
            return;
        }
        if (e->method->is_async || e->method->is_worker) { // Awaited or not: an await is refused where it's written
            impure(c, e->at, text("starts '" STR_FMT "', which runs as a task", STR_ARG(e->method->name)));
            return;
        }
        calls(c, e->method, e->at);
        break;
    case CALL_ROLL: impure(c, e->at, "rolls with Random"); return;
    case CALL_SPAWN: case CALL_LOAD: impure(c, e->at, "spawns"); return;
    case CALL_SEND: impure(c, e->at, "sends an event"); return;
    case CALL_ADD: case CALL_REMOVE: case CALL_DESTROY: case CALL_PARENT: case CALL_UNLOAD: case CALL_SNAP:
        impure(c, e->at, "changes entities");
        return;
    case CALL_WORLD: case CALL_TRANSFORM: case CALL_IS_PREPARED: impure(c, e->at, "reads the World"); return;
    case CALL_DRAW: case CALL_GUI: impure(c, e->at, "draws"); return;
    case CALL_AUDIO: case CALL_EFFECT: impure(c, e->at, "plays sound"); return;
    case CALL_SESSION: impure(c, e->at, "uses the Session"); return;
    case CALL_ACTION: impure(c, e->at, "runs an Action"); return;
    case CALL_WAIT: case CALL_TASK: impure(c, e->at, "waits"); return;
    default:
        impure(c, e->at, text("calls '" STR_FMT "', which does more than give a value", STR_ARG(e->name)));
        return;
    }
    if (!owner_name(e->object)) walk_expr(c, e->object);
    for (int i = 0; i < e->args.count; i++) walk_expr(c, e->args.items[i]);
    walk_expr(c, e->skin_mesh);
    walk_expr(c, e->skin_pose);
    walk_expr(c, e->mesh_lod);
    walk_expr(c, e->values);
    for (int i = 0; i < 2; i++) walk_stmt(c, e->compare[i]);
}

static void walk_stmt(pctx *c, const stmt *s)
{
    if (!s || !c->code->pure) return;
    if (s->kind == S_PARALLEL || s->parallel) {
        impure(c, s->at, "runs a parallel loop");
        return;
    }
    if (s->query) {
        impure(c, s->at, "reads a query's entities");
        return;
    }
    if (s->operator_decl) calls(c, s->operator_decl, s->at);
    walk_expr(c, s->cond);
    walk_expr(c, s->value);
    walk_expr(c, s->target);
    walk_expr(c, s->property_set);
    walk_expr(c, s->by);
    walk_expr(c, s->offset);
    walk_stmt(c, s->init);
    walk_stmt(c, s->step);
    walk_stmt(c, s->then_stmt);
    walk_stmt(c, s->else_stmt);
    for (int i = 0; i < s->stmts.count; i++) walk_stmt(c, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        const switch_case *part = &s->cases.items[i];
        for (int k = 0; k < part->labels.count; k++) walk_expr(c, part->labels.items[k]);
        for (int k = 0; k < part->body.count; k++) walk_stmt(c, part->body.items[k]);
    }
}

static bool has_code(const decl *d)
{
    return (d->kind == DECL_FUNCTION || d->kind == DECL_METHOD) && d->body && !d->is_extern;
}

// What the declaration says of itself, before its body is read.
static void check_signature(pctx *c)
{
    decl *d = c->code;
    if (d->is_async) impure(c, d->async_at, "runs as a task ('async')");
    else if (d->is_worker) impure(c, d->worker_at, "is a worker");
    else if (d->is_audio) impure(c, d->at, "runs on the audio thread");
    else if (d->is_mut_method) impure(c, d->at, "is a 'mut' method: it changes the value it's called on");
    else if (d->takes_action) impure(c, d->at, "runs an Action");
    else if (d->borrows_queries) impure(c, d->at, "reads a query's entities");
    for (int i = 0; i < d->params.count && d->pure; i++) {
        const param *p = &d->params.items[i];
        if (p->is_out) {
            impure(c, p->at, text("makes its 'out' parameter '" STR_FMT "'", STR_ARG(p->name)));
        } else if (p->mode != PARAM_READ) {
            impure(c, p->at, text("takes '" STR_FMT "' as 'mut', which it can change for its caller", STR_ARG(p->name)));
        } else if (outside_type(p->type)) {
            impure(c, p->at, outside_type(p->type));
        } else if (p->type.kind == TY_SINGLETON && p->type.decl == c->prog->time) {
            impure(c, p->at, "reads Time");
        } else if (p->type.kind == TY_SINGLETON && p->type.decl == c->prog->session) {
            impure(c, p->at, "reads the Session");
        }
    }
}

// What the checker found the code to do, itself or through its calls.
static void check_flags(pctx *c)
{
    decl *d = c->code;
    if (d->acts[0] || d->acts[1]) {
        const int side = d->acts[0] ? 0 : 1;
        impure(c, d->acts_at[side], d->acts_what[side] ? d->acts_what[side] : "sends, spawns or changes entities");
    }
    if (d->rolls) impure(c, d->rolls_at, "rolls with Random");
    if (d->reads_seed) impure(c, d->reads_seed_at, "reads Random.seed");
    if (d->frame_time) impure(c, d->frame_time_at, "reads Frame.dt");
    if (d->draws) impure(c, d->draws_at, d->frame_devices ? "reads Devices" : d->frame_cursor ? "uses the Cursor" : "draws");
    if (d->plays) impure(c, d->plays_at, "plays sound");
    if (d->uses_io) impure(c, d->io_at, d->io_network ? "reaches the network" : "reads or writes files");
    if (d->render_resources) impure(c, d->render_resources_at, "uses this machine's rendering resources");
    if (d->starts_tasks) impure(c, d->starts_at, d->starts_what ? d->starts_what : "starts a task");
    if (d->uses_workers) impure(c, d->worker_at, "awaits a worker");
    if (d->reaches_extern) impure(c, d->at, "reaches a function written in C");
    if (d->calls_c) impure(c, d->at, "calls into the engine in a way that has to keep its order");
    if (d->world_match || d->world_local || d->world_sets || d->world_uses.count) impure(c, d->at, "reads the World");
    if (d->uses_this || d->uses_owner_world) impure(c, d->at, "reads the entity it runs for");
    if (d->loads_scenes || d->preparation_reads) impure(c, d->at, "loads or reads scenes");
    if (d->reads_texture_size) impure(c, d->at, "reads a texture's size");
    for (int i = 0; i < DEVICE_WORDS; i++) {
        if (d->device_uses[i]) impure(c, d->at, "reads Devices");
    }
}

static void say_cached_errors(const program *prog);

void infer_purity(program *prog)
{
    VEC(decl *) code = {0};
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (has_code(d)) vec_push(code, d);
        for (int k = 0; k < d->methods.count; k++) {
            decl *m = d->methods.items[k];
            if (has_code(m)) vec_push(code, m);
            if (m->placement_method && has_code(m->placement_method)) vec_push(code, m->placement_method);
        }
    }
    pctx *each = code.count ? arena_alloc((size_t)code.count * sizeof *each) : NULL;
    for (int i = 0; i < code.count; i++) {
        pctx *c = &each[i];
        c->prog = prog;
        c->code = code.items[i];
        c->code->pure = true; // Until something says otherwise
        c->code->impure_why = NULL;
        c->code->impure_through = NULL;
        check_signature(c);
        walk_stmt(c, c->code->body);
    }
    // A function that calls one that isn't pure isn't either. One that calls
    // itself, or others that call it back, is as pure as the rest of what
    // they all do. Then what the checker's own marks say of the ones still
    // pure, which is nothing unless this file's walk missed something, and
    // the calls again for those.
    for (int pass = 0; pass < 2; pass++) {
        for (bool changed = true; changed;) {
            changed = false;
            for (int i = 0; i < code.count; i++) {
                pctx *c = &each[i];
                if (!c->code->pure) continue;
                for (int k = 0; k < c->calls.count; k++) {
                    const decl *to = c->calls.items[k].to;
                    if (to->pure && has_code(to)) continue;
                    c->code->pure = false;
                    c->code->impure_at = c->calls.items[k].at;
                    c->code->impure_through = to;
                    c->code->impure_why = to->is_extern ? text("calls '" STR_FMT "', which is written in C", STR_ARG(to->name))
                                        : has_code(to)  ? text("calls '" STR_FMT "', which isn't pure", STR_ARG(to->name))
                                                        : text("calls '" STR_FMT "', which the engine runs", STR_ARG(to->name));
                    changed = true;
                    break;
                }
            }
        }
        for (int i = 0; pass == 0 && i < code.count; i++) check_flags(&each[i]);
    }
    say_cached_errors(prog);
}

// ---------------------------------------------------------------------------
// `cached`

// Whether a value of the type holds a grid: a cached function's result is
// copied out to each caller, and a grid is never copied.
static bool holds_grid(const type t, const int depth)
{
    if (t.kind == TY_GRID) return true;
    if (depth > 16 || !t.decl) return false;
    switch (t.kind) {
    case TY_STRUCT: case TY_LIST: case TY_MAP: case TY_SET: case TY_ARRAY: case TY_OPTIONAL: case TY_FAILABLE:
        for (int i = 0; i < t.decl->fields.count; i++) {
            if (holds_grid(t.decl->fields.items[i].type, depth + 1)) return true;
        }
        return false;
    default: return false;
    }
}

static void say_cached_errors(const program *prog)
{
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (!d->is_cached || d->kind != DECL_FUNCTION || !d->body) continue;
        if (d->return_type.kind == TY_VOID) {
            diag_error(d->cached_at, "'" STR_FMT "' is cached, but it gives no result to remember", STR_ARG(d->name));
            diag_note("a cached function is called for its value: give it a return type, or drop 'cached'");
            suggest_replacement(str_from("cached "), "");
            continue;
        }
        if (!d->pure) {
            // What makes it so, which may be in a function it calls, and the line of each call on the way there
            const decl *cause = d;
            for (int depth = 0; depth < 8 && cause->impure_through && cause->impure_through->impure_why && has_code(cause->impure_through);
                 depth++) {
                cause = cause->impure_through;
            }
            diag_error(d->cached_at, "'" STR_FMT "' is cached, but it isn't pure: it %s", STR_ARG(d->name),
                       cause->impure_why ? cause->impure_why : "does more than give a value");
            const decl *at = d;
            for (int depth = 0; depth < 8 && at != cause; depth++) {
                diag_note_at(at->impure_at, "%s calls '" STR_FMT "' on line %d", at == d ? "it" : "which",
                             STR_ARG(at->impure_through->name), at->impure_at.line);
                at = at->impure_through;
            }
            diag_note_at(cause->impure_at, cause == d ? "on line %d" : "which does on line %d", cause->impure_at.line);
            diag_note("a cached function's result comes from its arguments alone, so that remembering it changes nothing: "
                      "pass it what it needs as read-only parameters, or drop 'cached'");
            continue;
        }
        bool input = false;
        for (int k = 0; k < d->params.count && !input; k++) {
            const param *p = &d->params.items[k];
            if (p->type.kind != TY_INPUT) continue;
            input = true;
            diag_error(p->at, "'" STR_FMT "' is cached, but it takes the input, which is this tick's beside the last one's",
                       STR_ARG(d->name));
            diag_note("pass it the fields it reads, as values: 'cached float Turn(float2 move)'");
        }
        if (input) continue;
        if (holds_grid(d->return_type, 0)) {
            diag_error(d->cached_at, "'" STR_FMT "' is cached, but it gives a grid, which is never copied", STR_ARG(d->name));
            diag_note("each call of a cached function gets a copy of what it remembers: return what the caller needs of "
                      "the grid, or drop 'cached'");
        }
    }
}

const char *purity_summary(const decl *d)
{
    if (!d || !has_code(d)) return NULL;
    if (d->pure) {
        return d->is_cached ? "Pure, and cached: called again with the same arguments, it gives the result it remembers."
                            : "Pure: its result comes from its arguments alone, so repeated calls with the same arguments run once.";
    }
    if (!d->impure_why) return NULL;
    return text("Not pure: it %s (line %d).", d->impure_why, d->impure_at.line);
}

// ---------------------------------------------------------------------------
// The same expression

static bool same_type(const type a, const type b)
{
    return a.kind == b.kind && a.decl == b.decl;
}

static bool same_args(const expr *a, const expr *b)
{
    if (a->args.count != b->args.count) return false;
    for (int i = 0; i < a->args.count; i++) {
        if (!pure_same_expr(a->args.items[i], b->args.items[i])) return false;
    }
    if (a->arg_want.count != b->arg_want.count) return false;
    for (int i = 0; i < a->arg_want.count; i++) {
        if (!same_type(a->arg_want.items[i], b->arg_want.items[i])) return false;
    }
    return true;
}

static bool same_text(const char *a, const char *b)
{
    return a == b || (a && b && strcmp(a, b) == 0);
}

bool pure_same_expr(const expr *a, const expr *b)
{
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind || !same_type(a->type, b->type)) return false;
    switch (a->kind) {
    case E_INT: return a->int_value == b->int_value && a->is_long == b->is_long && a->is_char == b->is_char;
    case E_FLOAT: return str_eq(a->text, b->text) && a->is_double == b->is_double && a->is_degrees == b->is_degrees;
    case E_BOOL: return a->bool_value == b->bool_value;
    case E_STRING: return str_eq(a->text, b->text);
    case E_DEFAULT: case E_NULL: case E_THIS: return true;
    case E_NAME:
        if (a->bind != b->bind) return false;
        if (a->bind == BIND_PARAM) return a->param == b->param;
        if (a->bind == BIND_LOCAL) return a->local == b->local;
        if (a->bind == BIND_CONST || a->bind == BIND_ASSET) return a->constant == b->constant && a->asset_reference == b->asset_reference;
        return str_eq(a->name, b->name) && a->type_decl == b->type_decl;
    case E_MEMBER:
        return a->field == b->field && a->swizzle_len == b->swizzle_len && memcmp(a->swizzle, b->swizzle, sizeof a->swizzle) == 0
            && a->enum_member == b->enum_member && same_text(a->c_constant, b->c_constant) && str_eq(a->member, b->member)
            && a->bind == b->bind && a->constant == b->constant && a->asset_ref_value == b->asset_ref_value
            && a->mesh_part == b->mesh_part && a->property == b->property && pure_same_expr(a->object, b->object);
    case E_INDEX:
        return pure_same_expr(a->object, b->object) && pure_same_expr(a->lhs, b->lhs);
    case E_UNARY:
    case E_BINARY:
        return a->op == b->op && a->method == b->method && pure_same_expr(a->lhs, b->lhs)
            && (a->kind == E_UNARY || pure_same_expr(a->rhs, b->rhs));
    case E_CONDITIONAL:
        return pure_same_expr(a->cond, b->cond) && pure_same_expr(a->lhs, b->lhs) && pure_same_expr(a->rhs, b->rhs);
    case E_CALL:
    case E_METHOD:
        if (a->call != b->call || a->method != b->method || a->ctor != b->ctor || !same_text(a->c_callee, b->c_callee)
            || !same_text(a->c_pack, b->c_pack) || !str_eq(a->name, b->name) || !str_eq(a->type_arg, b->type_arg)
            || a->type_decl != b->type_decl || a->skin_operation != b->skin_operation || a->path_operation != b->path_operation) {
            return false;
        }
        if ((a->object == NULL) != (b->object == NULL)) return false;
        if (a->object && !pure_same_expr(a->object, b->object)) return false;
        return same_args(a, b);
    case E_LITERAL:
        if (a->type_decl != b->type_decl || a->inits.count != b->inits.count) return false;
        for (int i = 0; i < a->inits.count; i++) {
            if (a->inits.items[i].field != b->inits.items[i].field
                || !pure_same_expr(a->inits.items[i].value, b->inits.items[i].value)) return false;
        }
        return true;
    case E_LIST:
        return same_args(a, b);
    case E_INTERP:
        if (a->parts.count != b->parts.count || a->formats.count != b->formats.count) return false;
        for (int i = 0; i < a->parts.count; i++) {
            if (!str_eq(a->parts.items[i], b->parts.items[i])) return false;
        }
        for (int i = 0; i < a->formats.count; i++) {
            if (!str_eq(a->formats.items[i], b->formats.items[i])) return false;
        }
        return same_args(a, b);
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Values that stay what they are

// Whether the code can change the world it reads while it runs: a component or
// singleton it was handed read-only is then not sure to stay as it was.
static bool code_changes_world(const decl *code)
{
    if (code->world_sets || code->reaches_extern || code->starts_tasks || code->is_async) return true;
    for (int i = 0; i < code->world_uses.count; i++) {
        if (code->world_uses.items[i].writes) return true;
    }
    for (int i = 0; i < code->params.count; i++) {
        const param *p = &code->params.items[i];
        if (p->type.kind == TY_WORLD && p->mode == PARAM_MUT) return true;
        // A query whose rows it can write: one of them may be the entity it runs for
        for (int k = 0; k < p->query.count; k++) {
            if (p->query.items[k].mode == PARAM_MUT) return true;
        }
    }
    return false;
}

static bool plain_number(const type t)
{
    return type_dim(t) > 0 || matrix_dim(t) > 0 || t.kind == TY_BOOL || t.kind == TY_LONG || t.kind == TY_CHAR || t.kind == TY_ENUM
        || t.kind == TY_QUATERNION || t.kind == TY_COLOR || t.kind == TY_RECT || t.kind == TY_ENTITY || t.kind == TY_PLAYER;
}

// Whether a call is of a pure function or method of the game's, one whose
// value can stand in for another call's with the same arguments.
static bool pure_call(const expr *e)
{
    if ((e->kind != E_CALL && e->kind != E_METHOD) || (e->call != CALL_FUNCTION && e->call != CALL_METHOD) || !e->method) return false;
    const decl *m = e->method;
    return m->pure && has_code(m) && m->fails.kind == TY_VOID && !e->block && !e->task_start_value && !e->lambda;
}

static bool stable(const decl *code, const expr *e, const bool world_changes)
{
    if (!e) return true;
    if (e->lambda || e->gpu || e->is_out || e->window || outside_type(e->type)) return false;
    switch (e->kind) {
    case E_INT: case E_FLOAT: case E_BOOL: case E_STRING: case E_NULL:
        return true;
    case E_DEFAULT:
        return plain_number(e->type);
    case E_THIS:
        return e->type.kind == TY_ENTITY || e->type.kind == TY_LOCAL_ENTITY || (!code->is_mut_method && !world_changes);
    case E_NAME:
        if (e->bind == BIND_CONST || e->bind == BIND_ASSET) return true;
        if (e->bind == BIND_LOCAL) {
            const stmt *local = e->local;
            // One that isn't `mut` never changes. Not a for's own variable, which its step changes, a query's
            // row, which reads the world as it is, or one an `is` or an `out` argument declares partway through
            // a statement.
            if (!local || local->is_mut || local->loop_var || local->query || local->out_decl || local->delayed || local->is_shared
                || local->discard) return false;
            if (local->kind == S_VAR) return local->value != NULL;
            return local->kind == S_FOREACH && !local->parallel;
        }
        if (e->bind == BIND_PARAM) {
            const param *p = e->param;
            if (!p || p->mode != PARAM_READ || p->is_out || p->task_ref || p->verified || p->placement_local) return false;
            // A function's own copy; but a list may be its caller's, and a system's component or singleton is
            // the world's, which stays as it was unless the code changes the world
            if (plain_number(p->type) || p->type.kind == TY_STRING) return true;
            return !world_changes;
        }
        return false;
    case E_MEMBER:
        if (e->property || e->local_field || e->edge != EDGE_NONE || e->typed_text || e->task_completion || e->target_colors
            || e->raw_sampled || e->mesh_part) return false;
        if (owner_name(e->object)) {
            if (e->enum_member || e->bind == BIND_CONST || e->bind == BIND_ASSET) return true;
            return e->object->bind == BIND_NONE && pure_owner(e->object->name) && e->c_constant != NULL;
        }
        return e->object && stable(code, e->object, world_changes);
    case E_INDEX:
        return e->object && stable(code, e->object, world_changes) && stable(code, e->lhs, world_changes);
    case E_UNARY:
        return (!e->method || (e->method->pure && has_code(e->method))) && stable(code, e->lhs, world_changes);
    case E_BINARY:
        return (!e->method || (e->method->pure && has_code(e->method))) && stable(code, e->lhs, world_changes)
            && stable(code, e->rhs, world_changes);
    case E_CONDITIONAL:
        return stable(code, e->cond, world_changes) && stable(code, e->lhs, world_changes) && stable(code, e->rhs, world_changes);
    case E_LITERAL:
        for (int i = 0; i < e->inits.count; i++) {
            if (!stable(code, e->inits.items[i].value, world_changes)) return false;
        }
        return true;
    case E_LIST:
    case E_INTERP:
        for (int i = 0; i < e->args.count; i++) {
            if (!stable(code, e->args.items[i], world_changes)) return false;
        }
        return true;
    case E_CALL:
    case E_METHOD: {
        if (e->block || e->task_start_value || e->resource_operation || e->io_operation) return false;
        bool ok = false;
        if (e->call == CALL_CONSTRUCT) ok = true;
        else if (e->call == CALL_BUILTIN) ok = e->kind == E_METHOD && owner_name(e->object) && pure_owner(e->object->name);
        else if (e->call == CALL_TEXT) ok = true;
        else if (e->call == CALL_LIST) ok = str_eq_c(e->name, "Contains") || str_eq_c(e->name, "IndexOf");
        else if (e->call == CALL_MAP) ok = str_eq_c(e->name, "Has") || str_eq_c(e->name, "Get");
        else ok = pure_call(e);
        if (!ok) return false;
        if (e->object && !owner_name(e->object) && !stable(code, e->object, world_changes)) return false;
        for (int i = 0; i < e->args.count; i++) {
            if (!stable(code, e->args.items[i], world_changes)) return false;
        }
        return true;
    }
    default:
        return false;
    }
}

bool pure_stable_expr(const decl *code, const expr *e)
{
    return stable(code, e, code_changes_world(code));
}

// ---------------------------------------------------------------------------
// One call for several
//
// The body is gone through in the order it runs. A call of a pure function
// whose arguments stay what they are (a candidate) is remembered where it's
// sure to run: not on the right of `&&` or `||`, in a side of `?:`, or in an
// expression's switch. A later candidate that is the same expression, within
// the block the first one's statement is in, reads the first one's value: the
// first is worked out into a read-only local just before its statement, which
// both then name. Nothing between them can change what the call reads, as its
// arguments name only what the code never changes; and working a pure call
// out a little earlier in its own statement is no change either, since it
// reads nothing that statement writes and does nothing but give its value.
//
// A loop's body that other threads run (a parallel loop, a query's rows, a
// grid's cells) and a call's block start over: they share no local with the
// code around them that the code didn't write itself.

typedef struct available {
    expr *first;     // The call, where it's sure to run: once it has a local, the E_NAME it became
    expr *call;      // ...and the call itself, the local's value
    stmt *local;     // The local it's worked out into, once a second one asked for it
    stmt *block;     // The block its statement is in
    int statement;   // ...and which statement of it, as the block was before anything was added to it
    int found;       // When it was found, among all of them: one found inside another's arguments comes first
} available;

typedef struct insertion {
    stmt *block;
    int before; // The statement it goes before, as the block was
    int order;  // Among the ones before the same statement
    stmt *local;
} insertion;

typedef struct mctx {
    program *prog;
    decl *code;
    bool world_changes;
    VEC(available) known;
    VEC(insertion) insertions;
    stmt *block;   // The block being gone through
    int statement; // ...and its statement
    int unsure;    // Inside something that may not run: nothing found there is remembered
    int frozen;    // Inside a loop's guards an index was planned from: nothing there changes
    int found;
    int temps;
    int merged;
} mctx;

static bool mergeable_type(const type t)
{
    if (t.kind == TY_VOID || t.kind == TY_ERROR || t.kind == TY_GRID || t.kind == TY_OPTIONAL || t.kind == TY_FAILABLE
        || outside_type(t) || t.kind == TY_CLIP || t.kind == TY_MESH) return false;
    return !holds_grid(t, 0);
}

static bool candidate(const mctx *m, const expr *e)
{
    return pure_call(e) && !e->hoisted && mergeable_type(e->type) && stable(m->code, e, m->world_changes);
}

static stmt *make_local(mctx *m, const expr *call)
{
    stmt *local = NEW(stmt);
    local->kind = S_VAR;
    local->at = call->at;
    local->name_at = call->at;
    local->type_at = call->at;
    local->type_qual_at = call->at;
    local->name = str_from(text("tide_same%d", ++m->temps));
    local->type = call->type;
    local->read = true;
    return local;
}

static void name_local(expr *e, stmt *local)
{
    const loc at = e->at;
    const uint32_t site = e->causal_site;
    memset(e, 0, sizeof *e);
    e->kind = E_NAME;
    e->at = at;
    e->causal_site = site;
    e->name = local->name;
    e->bind = BIND_LOCAL;
    e->local = local;
    e->type = local->type;
    e->spawn_archetype = -1;
}

static void merge_stmt(mctx *m, stmt *s);
static void merge_block(mctx *m, stmt *s, int skip);

// A region of its own: what's known outside isn't inside, and the other way.
// `skip` of a block's first statements stay as they are.
static void merge_apart(mctx *m, stmt *s, const int skip)
{
    if (!s) return;
    available *const items = m->known.items;
    const int count = m->known.count, cap = m->known.cap;
    stmt *const block = m->block;
    const int statement = m->statement, unsure = m->unsure;
    m->known.items = NULL;
    m->known.count = m->known.cap = 0;
    m->unsure = 0;
    if (s->kind == S_BLOCK) {
        merge_block(m, s, skip);
    } else { // One statement with no braces: nothing can go before it, so nothing of it is remembered
        m->unsure = 1;
        merge_stmt(m, s);
    }
    m->known.items = items;
    m->known.count = count;
    m->known.cap = cap;
    m->block = block;
    m->statement = statement;
    m->unsure = unsure;
}

// Whether a call wrote its argument `i` itself. One it left out is its
// parameter's own default, the same expression at every such call, in
// whichever code: it's read, never changed.
static bool written_arg(const expr *e, const int i)
{
    if (!e->settled) return true;
    for (int k = 0; k < e->written.count; k++) {
        if (e->written.items[k] == e->args.items[i]) return true;
    }
    return false;
}

static void merge_expr(mctx *m, expr *e)
{
    if (!e || e->pure_seen) return;
    e->pure_seen = true;
    if (e->lambda) return; // Its body is another function's
    if (!m->frozen && candidate(m, e)) {
        // The arguments first: a call inside them that's the first of its own kind goes before this one
        if (e->object) merge_expr(m, e->object);
        for (int i = 0; i < e->args.count; i++) {
            if (written_arg(e, i)) merge_expr(m, e->args.items[i]);
        }
        for (int i = 0; i < m->known.count; i++) {
            available *known = &m->known.items[i];
            if (!pure_same_expr(known->call, e)) continue;
            if (!known->local) {
                known->local = make_local(m, known->call);
                expr *value = NEW(expr);
                *value = *known->first;
                known->local->value = value;
                known->call = value;
                name_local(known->first, known->local);
                known->first->pure_seen = true;
                // Among the locals that go before one statement, in the order their calls were found: a call
                // inside another's arguments was found first, and the other's local reads its local
                const insertion add = {known->block, known->statement, known->found, known->local};
                vec_push(m->insertions, add);
            }
            name_local(e, known->local);
            e->pure_seen = true;
            m->merged++;
            return;
        }
        if (!m->unsure && m->block) {
            const available found = {.first = e, .call = e, .block = m->block, .statement = m->statement, .found = m->found++};
            vec_push(m->known, found);
        }
        return;
    }
    switch (e->kind) {
    case E_BINARY:
        merge_expr(m, e->lhs);
        if (e->op == T_AND || e->op == T_OR) m->unsure++;
        merge_expr(m, e->rhs);
        if (e->op == T_AND || e->op == T_OR) m->unsure--;
        return;
    case E_COALESCE:
        merge_expr(m, e->lhs);
        m->unsure++;
        merge_expr(m, e->rhs);
        m->unsure--;
        return;
    case E_CONDITIONAL:
        merge_expr(m, e->cond);
        m->unsure++;
        merge_expr(m, e->lhs);
        merge_expr(m, e->rhs);
        m->unsure--;
        return;
    case E_SWITCH:
        merge_expr(m, e->object);
        m->unsure++;
        for (int i = 0; i < e->inits.count; i++) merge_expr(m, e->inits.items[i].value);
        for (int i = 0; i < e->args.count; i++) merge_expr(m, e->args.items[i]);
        m->unsure--;
        return;
    case E_STEP: // Its assignment names the same place again: left as written
    case E_AWAIT:
        return;
    default:
        break;
    }
    merge_expr(m, e->object);
    merge_expr(m, e->lhs);
    merge_expr(m, e->rhs);
    merge_expr(m, e->cond);
    for (int i = 0; i < e->args.count; i++) {
        if (written_arg(e, i)) merge_expr(m, e->args.items[i]);
    }
    for (int i = 0; i < e->inits.count; i++) merge_expr(m, e->inits.items[i].value);
    merge_expr(m, e->skin_mesh);
    merge_expr(m, e->skin_pose);
    merge_expr(m, e->mesh_lod);
    merge_expr(m, e->values);
    if (e->block) merge_apart(m, e->block, 0);
}

// A statement inside another: a block of its own, or one statement, where
// nothing can go before it.
static void merge_inner(mctx *m, stmt *s)
{
    if (!s) return;
    if (s->kind == S_BLOCK) {
        merge_block(m, s, 0);
        return;
    }
    m->unsure++;
    merge_stmt(m, s);
    m->unsure--;
}

// How many of a loop's first statements are its guards, which an index was
// planned from (indexing.c, walk_guards): they stay as they are.
static int guards(const stmt *loop)
{
    if (!loop->index && !loop->indexed && !loop->index_guarded) return 0;
    const stmt *body = loop->then_stmt;
    if (!body || body->kind != S_BLOCK) return -1;
    int n = 0;
    while (n < body->stmts.count && (body->stmts.items[n]->kind == S_VAR || body->stmts.items[n]->kind == S_IF)) n++;
    return n;
}

static void merge_stmt(mctx *m, stmt *s)
{
    if (!s) return;
    switch (s->kind) {
    case S_BLOCK:
        merge_block(m, s, 0);
        return;
    case S_IF:
        merge_expr(m, s->cond);
        merge_inner(m, s->then_stmt);
        merge_inner(m, s->else_stmt);
        return;
    case S_WHILE: // Its condition runs again each time round: after the body, which nothing known in is known to
        m->unsure++;
        merge_expr(m, s->cond);
        m->unsure--;
        merge_inner(m, s->then_stmt);
        return;
    case S_FOR: {
        // Its own variable's scope: what's first found in its parts has nowhere to go
        const int known = m->known.count;
        m->unsure++;
        merge_stmt(m, s->init);
        merge_expr(m, s->cond);
        m->unsure--;
        merge_inner(m, s->then_stmt);
        m->unsure++;
        merge_stmt(m, s->step);
        m->unsure--;
        m->known.count = known;
        return;
    }
    case S_FOREACH:
    case S_PARALLEL: {
        merge_expr(m, s->value);
        merge_expr(m, s->by);
        merge_expr(m, s->offset);
        const int skip = guards(s);
        if (s->kind == S_PARALLEL || s->parallel || s->query || s->value == NULL
            || (s->value && s->value->type.kind == TY_GRID)) {
            if (skip >= 0) merge_apart(m, s->then_stmt, skip);
            return;
        }
        if (skip == 0) merge_inner(m, s->then_stmt);
        else if (skip > 0) merge_block(m, s->then_stmt, skip);
        return;
    }
    case S_SWITCH:
        merge_expr(m, s->cond);
        for (int i = 0; i < s->cases.count; i++) {
            switch_case *part = &s->cases.items[i];
            // A section's statements are in the switch's braces, with no block of their own to add a local to
            const int known = m->known.count;
            m->unsure++;
            for (int k = 0; k < part->body.count; k++) merge_stmt(m, part->body.items[k]);
            m->unsure--;
            m->known.count = known;
        }
        return;
    case S_ASSIGN:
        // A property's assignment is its setter's call, which reads the getter's own call as its place: as written
        if (s->property_set || (s->target && s->target->property)) return;
        if (s->target) { // The place stays a place; what works out where it is (an index) is a value like any
            s->target->pure_seen = true;
            merge_expr(m, s->target->object);
            merge_expr(m, s->target->lhs);
        }
        merge_expr(m, s->value);
        return;
    case S_VAR:
    case S_EXPR:
    case S_RETURN:
    case S_FAIL:
        merge_expr(m, s->value);
        return;
    default:
        return;
    }
}

static void merge_block(mctx *m, stmt *s, const int skip)
{
    stmt *const outer_block = m->block;
    const int outer_statement = m->statement;
    const int known = m->known.count;
    const int unsure = m->unsure;
    m->unsure = 0; // Its statements run in order, once it does
    for (int i = 0; i < s->stmts.count; i++) {
        m->block = s;
        m->statement = i;
        if (i < skip) m->frozen++;
        merge_stmt(m, s->stmts.items[i]);
        if (i < skip) m->frozen--;
    }
    m->block = outer_block;
    m->statement = outer_statement;
    m->unsure = unsure;
    m->known.count = known; // Its locals end with it
}

static int by_place(const void *a, const void *b)
{
    const insertion *x = a, *y = b;
    if (x->block != y->block) return (uintptr_t)x->block < (uintptr_t)y->block ? -1 : 1;
    if (x->before != y->before) return x->before - y->before;
    return x->order - y->order;
}

static void merge_code(program *prog, decl *code, stmt *body, int *merged)
{
    if (!body || body->kind != S_BLOCK) return;
    mctx m = {.prog = prog, .code = code, .world_changes = code_changes_world(code)};
    merge_block(&m, body, 0);
    *merged += m.merged;
    if (!m.insertions.count) return;
    // Each block's new locals go in from its end, so the places the others were found at still hold
    qsort(m.insertions.items, (size_t)m.insertions.count, sizeof *m.insertions.items, by_place);
    for (int i = m.insertions.count - 1; i >= 0; i--) {
        const insertion *add = &m.insertions.items[i];
        stmt *block = add->block;
        vec_push(block->stmts, NULL);
        for (int k = block->stmts.count - 1; k > add->before; k--) block->stmts.items[k] = block->stmts.items[k - 1];
        block->stmts.items[add->before] = add->local;
    }
}

// The functions, methods, operators and getters some code calls, each once, onto `out`.
typedef VEC(const decl *) decl_set;

static void reaches(decl_set *out, const decl *to)
{
    if (!to) return;
    for (int i = 0; i < out->count; i++) {
        if (out->items[i] == to) return;
    }
    vec_push(*out, to);
}

static void reached_stmt(decl_set *out, const stmt *s);
static void reached_expr(decl_set *out, const expr *e)
{
    if (!e) return;
    reaches(out, e->method);
    if (e->property) {
        reaches(out, e->property->get);
        reaches(out, e->property->set);
    }
    reached_expr(out, e->object);
    reached_expr(out, e->lhs);
    reached_expr(out, e->rhs);
    reached_expr(out, e->cond);
    reached_expr(out, e->lambda_body);
    reached_expr(out, e->skin_mesh);
    reached_expr(out, e->skin_pose);
    reached_expr(out, e->mesh_lod);
    reached_expr(out, e->values);
    for (int i = 0; i < e->args.count; i++) reached_expr(out, e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) reached_expr(out, e->inits.items[i].value);
    reached_stmt(out, e->block);
    reached_stmt(out, e->step);
}

static void reached_stmt(decl_set *out, const stmt *s)
{
    if (!s) return;
    reaches(out, s->operator_decl);
    reached_expr(out, s->cond);
    reached_expr(out, s->value);
    reached_expr(out, s->target);
    reached_expr(out, s->property_set);
    reached_expr(out, s->by);
    reached_expr(out, s->offset);
    reached_stmt(out, s->init);
    reached_stmt(out, s->step);
    reached_stmt(out, s->then_stmt);
    reached_stmt(out, s->else_stmt);
    for (int i = 0; i < s->stmts.count; i++) reached_stmt(out, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        const switch_case *part = &s->cases.items[i];
        for (int k = 0; k < part->body.count; k++) reached_stmt(out, part->body.items[k]);
    }
}

int merge_pure_calls(program *prog)
{
    if (prog->calls_merged) return 0;
    prog->calls_merged = true;
    // What the GPU runs is left as written: its compilers merge their own, and a function a frag calls is read
    // by the shader's writer as much as by C's. So: every shader, every function only the GPU runs, and
    // everything their code reaches through calls, methods, operators and getters
    decl_set gpu = {0};
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (d->kind == DECL_SHADER || d->gpu_only) reaches(&gpu, d);
        for (int k = 0; k < d->methods.count; k++) {
            if (d->methods.items[k]->gpu_only) reaches(&gpu, d->methods.items[k]);
        }
    }
    for (int i = 0; i < gpu.count; i++) {
        const decl *d = gpu.items[i];
        for (int k = 0; k < d->callees.count; k++) reaches(&gpu, d->callees.items[k]);
        reached_stmt(&gpu, d->body);
    }
    int merged = 0;
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        for (int k = -1; k < d->methods.count; k++) {
            decl *code = k < 0 ? d : d->methods.items[k];
            if (k < 0 && d->kind != DECL_FUNCTION && d->kind != DECL_SYSTEM && d->kind != DECL_METHOD) continue;
            if (d->kind == DECL_INPUT || d->kind == DECL_SHADER) continue;
            if (code->is_extern || code->is_async || code->is_worker || code->is_audio || code->is_build || code->gpu_only
                || code->builtin || code->takes_action || code->extern_system) continue;
            bool on_gpu = false;
            for (int j = 0; j < gpu.count && !on_gpu; j++) on_gpu = gpu.items[j] == code;
            if (on_gpu) continue;
            merge_code(prog, code, code->body, &merged);
            if (code->placement_method && code->placement_method->body) {
                merge_code(prog, code->placement_method, code->placement_method->body, &merged);
            }
        }
    }
    return merged;
}
