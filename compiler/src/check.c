#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "builtins.h"
#include "tide/devices.h"
#include "tide/player.h" // How many players a game can take
#include "types.h"

// Semantic analysis: resolves names and types, enforces access and mutability
// rules, and derives the archetype list from spawn, add and remove sites.

typedef struct checker {
    program *prog;
    const unit *unit;         // The file being checked: its namespace and `using`s decide what names mean.
    decl *system;             // Whose parameters are in scope: a system or view, a method, or the input when checking its Sample.
    decl *method;             // The method or function being checked: a method's type's fields are in scope.
    bool in_input;      // Checking the input's constructor, which runs outside the simulation.
    bool in_sanitize;         // Checking the input's Sanitize; in_input is set too, for the fields.
    int short_circuit_depth;  // Inside the right side of &&, || or ??, which may not run.
    const char *short_circuit_side; // ...as messages say it: "on the right side of && or ||"
    int switch_depth;         // Inside a switch's sections, where `break` ends one.
    int loop_depth;           // Inside a loop's body, where `break` and `continue` work.
    int loop_header;          // Inside a loop's condition or a for's step, which run again and again.
    const stmt *loop_var;     // Checking a for's step: the variable the for declared, which it may change
    int branch_depth;         // Inside a side of ?:, which may not run.
    VEC(stmt *) locals;       // S_VAR statements currently in scope.
    VEC(int) scope_marks;
    const expr *write_root;   // Checking an assignment's target: the name it writes through, which that doesn't read
    VEC(stmt *) unused;       // Locals nothing read by the end of their scope, warned about once every body is checked
    VEC(expr *) spawns;       // Spawn calls, patched with archetype indices at the end.
    VEC(expr *) sends;        // Send calls, checked against the handlers at the end.
    VEC(struct call_site) calls; // Calls of functions and methods: which draw, and which read devices.
    const expr *device_whole_ok; // A device record about to be checked that isn't read whole: a.b's a, or an argument
    const expr *awaiting;     // The call an `await` is checking: an async call it waits for, not a task it starts
    const expr *statement_call; // The call a statement makes: an async one starts a task
    int block_depth;          // Inside a block written after a call, which runs where the function runs it
    VEC(struct task_call) task_calls; // Calls of async functions: awaited, or starting tasks
    const stmt *parallel;     // Checking a parallel loop's step: the loop
    int grid_loops;           // Foreach loops over grids being checked, which go through cells without waiting
    int parallel_locals;      // ...and how many locals were in scope before it: the rest are the step's own
    const expr *compare;      // Checking a Sort's comparison block: the call
    int compare_locals;       // ...and how many locals were in scope before its own
    const expr *member_object; // a.b's a, about to be checked: a Forgotten handler's component is only named for its local fields
    const expr *query_value; // A foreach's direct query parameter name, which is only iterable
} checker;

// A call of an async function: awaited, its frame part of the caller's, or
// starting a task of its own.
typedef struct task_call {
    decl *from; // A system, view or handler, or an async function
    decl *to;
    const expr *call;
    bool awaited;
} task_call;

// A call of a function, and the code it's in: a function, method, system,
// view, handler or the input.
typedef struct call_site {
    decl *from;
    decl *to;
    const expr *call;
    bool parallel;
    bool comparing;
} call_site;

static const type T_ERR = {TY_ERROR, NULL};
static const type T_VOID_ = {TY_VOID, NULL};
static const type T_BOOL_ = {TY_BOOL, NULL};
static const type T_INT_ = {TY_INT, NULL};
static const type T_LONG_ = {TY_LONG, NULL};
static const type T_FLOAT_ = {TY_FLOAT, NULL};
static const type T_ENTITY_ = {TY_ENTITY, NULL};

// The type a declaration names: a component, singleton, input, record or struct.
static type decl_type(decl *d)
{
    switch (d->kind) {
    case DECL_COMPONENT: return (type){TY_COMPONENT, d};
    case DECL_SINGLETON: return (type){TY_SINGLETON, d};
    case DECL_INPUT: return (type){TY_INPUT, d};
    case DECL_RECORD: return (type){TY_RECORD, d};
    case DECL_STRUCT: return (type){TY_STRUCT, d};
    case DECL_EVENT: return (type){TY_EVENT, d};
    case DECL_ENUM: return (type){TY_ENUM, d};
    default: return T_ERR;
    }
}

// Does this type have fields (components, singletons, inputs, records, structs, events)?
static bool has_fields(const type t)
{
    return t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_RECORD
        || t.kind == TY_STRUCT || t.kind == TY_EVENT;
}

// A local field of a match component or singleton, by name: one of its local
// part's (decl.local_part).
static field *local_field_of(const decl *d, const str name)
{
    const decl *part = d ? d->local_part : NULL;
    for (int i = 0; part && i < part->fields.count; i++) {
        if (str_eq(part->fields.items[i].name, name)) return &part->fields.items[i];
    }
    return NULL;
}

// A name for a parameter of type `d` in a message's example: its name with a
// small first letter.
static const char *example_name(const decl *d)
{
    char *name = str_to_cstr(d->name);
    name[0] = (char)tolower((unsigned char)name[0]);
    return name;
}

// byte, int or float, not a vector.
static bool is_scalar_number(const type t)
{
    return t.kind == TY_BYTE || t.kind == TY_INT || t.kind == TY_FLOAT;
}

// After a type mismatch that a conversion written out settles: how to convert.
// Nothing converts to or from a long by itself but an int going in, and an
// enum and its number convert neither way by themselves.
static void note_conversion(const type want, const type have)
{
    if (want.kind == TY_LONG && have.kind == TY_FLOAT) {
        diag_note("long(x) makes a float a long, truncated toward zero");
    } else if (want.kind == TY_FLOAT && have.kind == TY_LONG) {
        diag_note("float(x) makes a long a float, rounded to the nearest");
    } else if (want.kind == TY_INT && have.kind == TY_LONG) {
        diag_note("int(x) makes a long an int, keeping its low 32 bits");
    } else if (want.kind == TY_ENUM && (have.kind == TY_INT || have.kind == TY_BYTE)) {
        diag_note("%s(x) makes an int the %s with that number", type_name(want), type_name(want));
    } else if (have.kind == TY_ENUM && (want.kind == TY_INT || want.kind == TY_LONG)) {
        diag_note("%s(x) makes it its number", type_name(want));
    }
}

// ---------------------------------------------------------------------------
// Names and namespaces

static const str global_ns = {"", 0};

static str decl_ns(const decl *d)
{
    return d->unit ? d->unit->ns : global_ns;
}

// Splits "Game.Combat.Health" into "Game.Combat" and "Health". False if there's no dot.
static bool split_qualified(const str text, str *ns, str *name)
{
    int dot = text.len - 1;
    while (dot >= 0 && text.ptr[dot] != '.') dot--;
    if (dot < 0) return false;
    *ns = (str){text.ptr, dot};
    *name = (str){text.ptr + dot + 1, text.len - dot - 1};
    return true;
}

// Does some file declare this namespace, or one inside it?
static bool is_namespace(const program *prog, const str ns)
{
    for (int i = 0; i < prog->units.count; i++) {
        const str u = prog->units.items[i]->ns;
        if (str_eq(u, ns) || (u.len > ns.len && str_starts_with_c(u, str_to_cstr(ns)) && u.ptr[ns.len] == '.')) return true;
    }
    return false;
}

// What a name can refer to where it's written.
typedef enum name_kind {
    NAME_TYPE,     // Components, singletons, inputs and structs
    NAME_SYSTEM,   // Systems and views, in [Before] and [After]
    NAME_FUNCTION,
    NAME_CONST,
    NAME_SHADER,   // Frags and verts, where Draw2D.Mesh names them
} name_kind;

static bool is_kind(const decl *d, const name_kind kind)
{
    if (kind == NAME_SYSTEM) return d->kind == DECL_SYSTEM;
    if (kind == NAME_FUNCTION) return d->kind == DECL_FUNCTION;
    if (kind == NAME_CONST) return d->kind == DECL_CONST;
    if (kind == NAME_SHADER) return d->kind == DECL_SHADER;
    return d->kind != DECL_SYSTEM && d->kind != DECL_FUNCTION && d->kind != DECL_CONST && d->kind != DECL_SHADER;
}

static void c_name_of(const decl *d, sb *out);

// FNV-1a, going on from `h`.
#define NAME_HASH 2166136261u
static uint32_t name_hash(uint32_t h, const char *bytes, const size_t len)
{
    for (size_t i = 0; i < len; i++) h = (h ^ (uint8_t)bytes[i]) * 16777619u;
    return h;
}

// A game has thousands of declarations, and every name written is looked up
// among them: the program keeps them by a hash of their namespace and name,
// and of their name in generated C, each bucket's in decls' order
// (decl_index). It's made the first time it's asked for, and again if decls
// has another count.
static const decl_index *decls_by(const program *prog, const bool c_names)
{
    decl_index *index = c_names ? &((program *)prog)->by_c_name : &((program *)prog)->by_name;
    const int n = prog->decls.count;
    if (index->first && index->count == n) return index;
    int buckets = 64;
    while (buckets < n * 2) buckets *= 2;
    index->first = arena_alloc(sizeof(int) * (size_t)buckets);
    index->next = arena_alloc(sizeof(int) * (size_t)(n ? n : 1));
    index->buckets = buckets;
    index->count = n;
    for (int i = 0; i < buckets; i++) index->first[i] = -1;
    for (int i = n - 1; i >= 0; i--) { // Backwards, so the first of a bucket is the first in decls
        const decl *d = prog->decls.items[i];
        const str ns = decl_ns(d);
        uint32_t h = name_hash(name_hash(NAME_HASH, ns.ptr, (size_t)ns.len), d->name.ptr, (size_t)d->name.len);
        if (c_names) {
            sb name = {0};
            c_name_of(d, &name);
            h = name_hash(NAME_HASH, name.data, name.len);
        }
        index->next[i] = index->first[h & (uint32_t)(buckets - 1)];
        index->first[h & (uint32_t)(buckets - 1)] = i;
    }
    return index;
}

// The first declaration in the bucket of a name in generated C: -1 for none.
static int first_c_named(const decl_index *index, const char *bytes, const size_t len)
{
    return index->first[name_hash(NAME_HASH, bytes, len) & (uint32_t)(index->buckets - 1)];
}

// A declaration of that kind named exactly `name` in `ns`.
static decl *find_in(const program *prog, const str ns, const str name, const name_kind kind)
{
    const decl_index *index = decls_by(prog, false);
    const uint32_t h = name_hash(name_hash(NAME_HASH, ns.ptr, (size_t)ns.len), name.ptr, (size_t)name.len);
    for (int i = index->first[h & (uint32_t)(index->buckets - 1)]; i >= 0; i = index->next[i]) {
        decl *d = prog->decls.items[i];
        if (is_kind(d, kind) && str_eq(d->name, name) && str_eq(decl_ns(d), ns)) {
            if (d == prog->random) d->named = true; // Generated C only has it in games that name it
            return d;
        }
    }
    return NULL;
}

// What a name means as written in `from`. A qualified name (Combat.Health) is
// exact. A plain one is looked up in the file's namespace, then its parents,
// then the `using` namespaces, then the global namespace. If two `using`
// namespaces both have it, *other gets the second: the name is ambiguous.
static decl *lookup(const program *prog, const unit *from, const str text, const name_kind kind, decl **other)
{
    *other = NULL;
    str ns;
    str name;
    if (split_qualified(text, &ns, &name)) return find_in(prog, ns, name, kind);

    for (ns = from ? from->ns : global_ns; ns.len > 0;) {
        decl *d = find_in(prog, ns, text, kind);
        if (d) return d;
        int dot = ns.len - 1;
        while (dot >= 0 && ns.ptr[dot] != '.') dot--;
        ns.len = dot < 0 ? 0 : dot;
    }
    decl *found = NULL;
    for (int i = 0; from && i < from->usings.count; i++) {
        decl *d = find_in(prog, from->usings.items[i], text, kind);
        if (d && !found) found = d;
        else if (d && d != found && !*other) *other = d;
    }
    return found ? found : find_in(prog, global_ns, text, kind);
}

static decl *find_named(const checker *c, const str name, const loc at, const name_kind kind)
{
    decl *other;
    decl *d = lookup(c->prog, c->unit, name, kind, &other);
    if (other) {
        diag_error(at, "'" STR_FMT "' is ambiguous: both " STR_FMT " and " STR_FMT " have it", STR_ARG(name),
                   STR_ARG(decl_ns(d)), STR_ARG(decl_ns(other)));
        diag_note("write which one you mean, like '" STR_FMT "'", STR_ARG(d->qualified));
    }
    return d;
}

static decl *find_type(const checker *c, const str name, const loc at)
{
    return find_named(c, name, at, NAME_TYPE);
}

// `a.b.c` as the text "a.b.c", if the expression is only names and members.
static bool qualified_text(const expr *e, str *out)
{
    if (e->kind == E_NAME) {
        *out = e->name;
        return true;
    }
    if (e->kind != E_MEMBER) return false;
    str left;
    if (!qualified_text(e->object, &left)) return false;
    char *text = arena_alloc((size_t)left.len + (size_t)e->member.len + 2);
    memcpy(text, left.ptr, (size_t)left.len);
    text[left.len] = '.';
    memcpy(text + left.len + 1, e->member.ptr, (size_t)e->member.len);
    *out = (str){text, left.len + 1 + e->member.len};
    return true;
}

// The innermost name of a.b.c.
static const expr *chain_root(const expr *e)
{
    while (e->kind == E_MEMBER) e = e->object;
    return e;
}

// Marks the namespace parts of Game.Combat.Health, for editors.
static void mark_namespaces(expr *e)
{
    for (; e && (e->kind == E_MEMBER || e->kind == E_NAME); e = e->kind == E_MEMBER ? e->object : NULL) {
        e->bind = BIND_NAMESPACE;
    }
}

// "did you mean" candidates: declared types of the kinds asked for.
static void suggest_decls(suggestion *s, const program *prog, const bool components, const bool singletons,
                          const bool inputs)
{
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if ((components && d->kind == DECL_COMPONENT) || (singletons && d->kind == DECL_SINGLETON)
            || (inputs && d->kind == DECL_INPUT)) {
            suggest_consider(s, d->name);
        }
    }
}

static void suggest_structs(suggestion *s, const program *prog)
{
    for (int i = 0; i < prog->decls.count; i++) {
        if (prog->decls.items[i]->kind == DECL_STRUCT) suggest_consider(s, prog->decls.items[i]->name);
    }
}

static void suggest_events(suggestion *s, const program *prog)
{
    for (int i = 0; i < prog->decls.count; i++) {
        if (prog->decls.items[i]->kind == DECL_EVENT) suggest_consider(s, prog->decls.items[i]->name);
    }
}

// "a component", "an event": what a declaration is, for messages.
static const char *decl_what(const decl *d)
{
    switch (d->kind) {
    case DECL_COMPONENT: return "a component";
    case DECL_SINGLETON: return "a singleton";
    case DECL_INPUT: return "the input";
    case DECL_STRUCT: return "a struct";
    case DECL_EVENT: return d->is_command ? "a command" : "an event";
    case DECL_ENUM: return "an enum";
    default: return "not a type";
    }
}

static void suggest_fields(suggestion *s, const decl *d)
{
    for (int i = 0; i < d->fields.count; i++) suggest_consider(s, d->fields.items[i].name);
}

static bool check_reserved(const str name, const loc at)
{
    if (str_starts_with_c(name, "tide_")) {
        diag_error(at, "names starting with 'tide_' are reserved for generated code");
        return false;
    }
    return true;
}

// `World` written where another type goes, when no type of the game's has the
// name: says where it belongs. Returns whether it did.
static bool world_misplaced(const str name, const loc at)
{
    if (!str_eq_c(name, "World")) return false;
    diag_error(at, "the World is a parameter of systems, views and event handlers: 'system Name(World world)'");
    diag_note("read what's needed there, like 'world.Get<Health>(entity)', and pass the value on");
    return true;
}

// ---------------------------------------------------------------------------
// Scopes

static void push_scope(checker *c)
{
    vec_push(c->scope_marks, c->locals.count);
}

// A local that a statement declared and nothing read by the end of its scope
// is noted, for a warning once every body is checked (warn_unused_locals). A
// foreach's or a parallel loop's variable, a name after `is` and a Sort's two
// elements aren't: their code needs a name there, used or not.
static void pop_scope(checker *c)
{
    const int kept = c->scope_marks.items[--c->scope_marks.count];
    for (int i = kept; i < c->locals.count; i++) {
        stmt *s = c->locals.items[i];
        if (s->kind != S_VAR || !s->value || s->read || s->unused) continue;
        if (s->type.kind == TY_ERROR || diag_error_count() != s->errors_before) continue; // Errors hide uses
        s->unused = true;
        vec_push(c->unused, s);
    }
    c->locals.count = kept;
}

static stmt *find_local(const checker *c, const str name)
{
    for (int i = c->locals.count - 1; i >= 0; i--) {
        if (str_eq(c->locals.items[i]->name, name)) return c->locals.items[i];
    }
    return NULL;
}

static bool field_named(const decl *d, const str name)
{
    for (int i = 0; d && i < d->fields.count; i++) {
        if (str_eq(d->fields.items[i].name, name)) return true;
    }
    return false;
}

// The fields named directly by name: the input's in its Sample and Sanitize,
// and a type's in its methods.
static field *field_in_scope(const checker *c, const str name)
{
    decl *const d = c->method ? (c->method->is_operator || c->method->is_interpolate ? NULL : c->method->owner)
                    : c->in_input ? c->system : NULL;
    for (int i = 0; d && i < d->fields.count; i++) {
        if (str_eq(d->fields.items[i].name, name) && !d->fields.items[i].hidden) return &d->fields.items[i];
    }
    return NULL;
}

static decl *find_method(const decl *d, const str name)
{
    for (int i = 0; d && i < d->methods.count; i++) {
        if (str_eq(d->methods.items[i]->name, name)) return d->methods.items[i];
    }
    return NULL;
}

static param *find_param(const checker *c, const str name)
{
    if (!c->system) return NULL; // Field defaults are checked outside any system.
    if (c->in_sanitize) return NULL; // Sample's parameters aren't Sanitize's
    for (int i = 0; i < c->system->params.count; i++) {
        param *p = &c->system->params.items[i];
        if (p->name.len > 0 && str_eq(p->name, name)) return p;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Expressions

static type check_expr(checker *c, expr *e);
static type check_expr_any(checker *c, expr *e);
static bool same_place(const expr *a, const expr *b);
static bool gpu_refuses(const checker *c, loc at, const char *what, const char *instead);
static type check_snap(checker *c, expr *e, decl *singleton);
static void c_name_of(const decl *d, sb *out);

// The code whose device reads are being recorded: a function or method, or a
// system, handler, view or the input.
static decl *reading_code(const checker *c)
{
    return c->method ? c->method : c->system;
}

// An async function being checked: it runs as a task, in a world, so it can
// spawn, send and change entities the way systems do. What it changes decides
// which world (see note_task_side).
static bool in_async_function(const checker *c)
{
    return c->method && c->method->is_async;
}

// A plain function or method being checked, which belongs to no world.
static bool in_routine(const checker *c)
{
    return c->method && !c->method->is_async;
}

// Async code, which can wait: an async function, or an async handler.
static bool async_code(const checker *c)
{
    return in_async_function(c) || (!c->method && c->system && c->system->is_async);
}

// The async function being checked changes the match (TASK_MATCH) or uses
// local state (TASK_LOCAL) at `at`: that's its world. One that does both is an
// error. Returns false after reporting it.
static bool note_side(decl *fn, const int side, const loc at)
{
    if (fn->task_side == side) return true;
    if (fn->task_side == TASK_EITHER) {
        fn->task_side = side;
        fn->task_side_at = at;
        return true;
    }
    diag_error(at, "'" STR_FMT "' %s here, and %s on line %d, but a task runs in one world", STR_ARG(fn->name),
               side == TASK_MATCH ? "changes the match" : "uses local state",
               side == TASK_MATCH ? "uses local state" : "changes the match", fn->task_side_at.line);
    diag_note("split it in two: the match's part, and a local one that reads the match");
    return false;
}

static bool note_task_side(const checker *c, const int side, const loc at)
{
    return note_side(c->method, side, at);
}

// Code that uses a struct's operator calls it, so what the operator's code
// does (calling C) counts for the code that uses it too.
static void note_operator_call(checker *c, decl *op, const expr *e)
{
    decl *from = reading_code(c);
    if (!op || !from) return;
    const call_site site = {from, op, e, c->parallel != NULL, c->compare != NULL};
    vec_push(c->calls, site);
}

static bool device_group_type(const type t)
{
    return t.kind == TY_RECORD && t.decl->device_group;
}

static bool reads_this_machine(const expr *e)
{
    while (e->kind == E_MEMBER || e->kind == E_INDEX) e = e->object;
    return e->kind == E_NAME && e->bind == BIND_DEVICES;
}

// Where device values are in program.device_leaves: `times` runs of `count`,
// `stride` apart.
typedef struct leaf_range {
    int first, count, stride, times;
} leaf_range;

// Where `e`, a device record or value, is when it's named from a Devices
// parameter. False when it's a copy, or a function's parameter, which could be
// any place its record has.
static bool device_range(const program *prog, const expr *e, leaf_range *r)
{
    if (e->kind == E_NAME) {
        if (e->bind != BIND_PARAM || e->type.kind != TY_RECORD || e->type.decl != prog->devices) return false;
        *r = (leaf_range){0, prog->devices->leaves_count, 0, 1};
        return true;
    }
    if (e->kind == E_MEMBER) {
        if (!e->field || !e->field->leaf || !device_range(prog, e->object, r)) return false;
        r->first += e->field->leaf - 1;
        r->count = device_group_type(e->field->type) ? e->field->type.decl->leaves_count : 1;
        return true;
    }
    if (e->kind == E_INDEX) {
        const type array = e->object->type;
        if (array.kind != TY_RECORD || !array.decl->array_of || !device_range(prog, e->object, r)) return false;
        const int size = array.decl->array_of->leaves_count;
        r->count = size;
        if (e->lhs->kind == E_INT && e->lhs->int_value >= 0 && e->lhs->int_value < array.decl->array_length) {
            r->first += (int)e->lhs->int_value * size;
        } else { // Any of them
            if (r->times > 1) return false;
            r->stride = size;
            r->times = array.decl->array_length;
        }
        return true;
    }
    return false;
}

// Match code reads the device values in `r`: the input sends them. A record
// read whole, as a copy, leaves out its positions in the window: reading one
// of the copy's counts.
static void note_device_range(const checker *c, const leaf_range r, const bool whole)
{
    decl *code = reading_code(c);
    if (!code) return;
    for (int t = 0; t < r.times; t++) {
        for (int i = 0; i < r.count; i++) {
            const int leaf = r.first + t * r.stride + i;
            if (whole && c->prog->device_leaves.items[leaf].field->window) continue;
            code->device_uses[leaf / 64] |= (uint64_t)1 << (leaf % 64);
        }
    }
}

// Notes the device record `record`, or a value in it (`f`, or NULL for all of
// it), at every place `record` has in the devices.
static void note_device_places(const checker *c, const decl *record, const field *f)
{
    for (int i = 0; i < record->instances.count; i++) {
        const int at = record->instances.items[i];
        const leaf_range r = f ? (leaf_range){at + f->leaf - 1, 1, 0, 1} : (leaf_range){at, record->leaves_count, 0, 1};
        note_device_range(c, r, !f);
    }
}

// Whose position in the window a value is, for the error that says the match
// can't read it.
static const char *window_value(const decl *record, const field *f)
{
    if (str_eq_c(record->name, "Mouse")) return "the mouse's position";
    if (str_eq_c(record->name, "Pointer")) return "the pointer's position";
    return str_eq_c(f->name, "startPosition") ? "a touch's start position" : "a touch's position";
}

// `e` names a device value, like 'devices.keyboard.space': match code reads
// it. Reads of this machine's `Devices` don't count.
static void note_device_member(const checker *c, const expr *e)
{
    decl *code = reading_code(c);
    if (!code || reads_this_machine(e)) return;
    const decl *record = e->object->type.decl;
    if (e->field->window && !code->position_at.line) {
        code->position_at = e->at;
        code->position_what = window_value(record, e->field);
    }
    leaf_range r;
    if (device_range(c->prog, e, &r)) note_device_range(c, r, false);
    else note_device_places(c, record, e->field);
}

// A device record used whole, like 'var pad = devices.gamepad;': every value
// in it counts as read.
static void note_device_value(const checker *c, const expr *e, const type t)
{
    if (!device_group_type(t) || reads_this_machine(e)) return;
    leaf_range r;
    if (device_range(c->prog, e, &r)) note_device_range(c, r, true);
    else note_device_places(c, t.decl, NULL);
}
static bool holds_text(type t);

// The system being checked writes text into its world, so it changes the
// world's heap: other systems that do too wait for it.
static void note_text_write(const checker *c, const bool text)
{
    if (text && c->system && c->system->kind == DECL_SYSTEM && !c->method) c->system->writes_text = true;
}

// The system being checked spawns: it hands out entity IDs, which go in order,
// so a later system that hands them out as it runs waits for it. One that
// splits its entities across threads gives temporary handles instead.
static void note_spawn(const checker *c)
{
    if (c->system && c->system->kind == DECL_SYSTEM && !c->method) c->system->spawns = true;
}

static bool holds_list(type t);

// Rendering handles belong to this machine. Structs can group them, but any
// place that keeps the struct must obey the same local-state restriction.
typedef struct resource_outer {
    const decl *type;
    const struct resource_outer *previous;
} resource_outer;

static bool holds_render_resource_in(const type t, const resource_outer *outer)
{
    if (t.kind == TY_TEXTURE || t.kind == TY_RENDER_TARGET) return true;
    if (t.kind != TY_STRUCT && t.kind != TY_LIST && t.kind != TY_MAP && t.kind != TY_SET
        && t.kind != TY_GRID && t.kind != TY_OPTIONAL && t.kind != TY_FAILABLE) return false;
    for (const resource_outer *p = outer; p; p = p->previous) {
        if (p->type == t.decl) return false;
    }
    const resource_outer here = {t.decl, outer};
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (holds_render_resource_in(t.decl->fields.items[i].type, &here)) return true;
    }
    return false;
}

static bool holds_render_resource(const type t)
{
    return holds_render_resource_in(t, NULL);
}

// Text or a list: what a world keeps in its heap.
static bool holds_heap(const type t)
{
    return holds_text(t) || holds_list(t);
}

static bool decl_holds_text(const decl *d)
{
    for (int i = 0; i < d->fields.count; i++) {
        if (holds_heap(d->fields.items[i].type)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Lists

// List<T>'s element type.
static type list_element(const type t)
{
    return t.decl->fields.items[0].type;
}

// List<T> for element type `element`: one declaration for each element type,
// so two lists of ints are the same type.
static type list_of(program *prog, const type element)
{
    for (int i = 0; i < prog->lists.count; i++) {
        const type t = prog->lists.items[i]->fields.items[0].type;
        if (t.kind == element.kind && t.decl == element.decl) return (type){TY_LIST, prog->lists.items[i]};
    }
    decl *d = NEW(decl);
    d->kind = DECL_LIST;
    d->builtin = true;
    sb name = {0};
    sb_printf(&name, "List<%s>", type_name(element));
    d->name = (str){name.data, (int)name.len};
    const field item = {str_from("item"), str_from(""), {0, 0, 0}, element, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    vec_push(d->fields, item);
    d->index = prog->lists.count;
    vec_push(prog->lists, d);
    return (type){TY_LIST, d};
}

// ---------------------------------------------------------------------------
// Maps and sets

// Map<K, V>'s key and value types; a Set<K>'s key.
static type map_key(const type t)
{
    return t.decl->fields.items[0].type;
}

static type map_value(const type t)
{
    return t.decl->fields.items[1].type;
}

// What a map's or a set's key can be: a value with a hash and `==`, and a
// fixed size in an entry.
static bool map_key_ok(const type t, const loc at, const bool set)
{
    switch (t.kind) {
    case TY_ERROR: return false;
    case TY_BYTE: case TY_INT: case TY_LONG: case TY_ENUM: case TY_ENTITY: case TY_LOCAL_ENTITY: case TY_PLAYER: case TY_STRING:
        return true;
    default:
        diag_error(at, "a %s's key is an int, a long, an enum, an Entity, a PlayerID or text, not %s", set ? "set" : "map",
                   type_name(t));
        if (t.kind == TY_FLOAT) diag_note("floats that look equal can differ in a bit; key by an int, like 'int(x * 100)'");
        else if (t.kind == TY_STRUCT) diag_note("key by one of its fields");
        return false;
    }
}

static bool list_element_ok(type t, loc at);

// Map<K, V> or Set<K> for those types: one declaration for each, so two maps
// from ints to text are the same type. A map's `entry` is the struct a foreach
// over it gives, with `key` and `value`.
static type map_of(program *prog, const type key, const type value, const bool set)
{
    for (int i = 0; i < prog->maps.count; i++) {
        const decl *m = prog->maps.items[i];
        const type k = m->fields.items[0].type;
        if (m->is_set != set || k.kind != key.kind || k.decl != key.decl) continue;
        if (set) return (type){TY_SET, prog->maps.items[i]};
        const type v = m->fields.items[1].type;
        if (v.kind == value.kind && v.decl == value.decl) return (type){TY_MAP, prog->maps.items[i]};
    }
    decl *d = NEW(decl);
    d->kind = DECL_MAP;
    d->builtin = true;
    d->is_set = set;
    sb name = {0};
    if (set) sb_printf(&name, "Set<%s>", type_name(key));
    else sb_printf(&name, "Map<%s, %s>", type_name(key), type_name(value));
    d->name = (str){name.data, (int)name.len};
    const field k = {str_from("key"), str_from(""), {0, 0, 0}, key, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    vec_push(d->fields, k);
    if (!set) {
        const field v = {str_from("value"), str_from(""), {0, 0, 0}, value, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
        vec_push(d->fields, v);
        decl *entry = NEW(decl);
        entry->kind = DECL_STRUCT;
        entry->builtin = true;
        sb entry_name = {0};
        sb_printf(&entry_name, "Entry<%s, %s>", type_name(key), type_name(value));
        entry->name = (str){entry_name.data, (int)entry_name.len};
        vec_push(entry->fields, k);
        vec_push(entry->fields, v);
        entry->index = 2; // Checked, as a struct is once its fields are ordered
        d->entry = entry;
    }
    d->index = prog->maps.count;
    vec_push(prog->maps, d);
    return (type){set ? TY_SET : TY_MAP, d};
}

// "Map<K, V>" or "Set<K>": the type, in `*out`; false for text that isn't one.
static bool resolve_map_type(const checker *c, const str text, const loc at, type *out)
{
    const bool set = str_starts_with_c(text, "Set<");
    if (!set && !str_starts_with_c(text, "Map<")) return false;
    if (text.ptr[text.len - 1] != '>') return false;
    str inner = {text.ptr + 4, text.len - 5};
    str key_text = inner;
    str value_text = {NULL, 0};
    if (!set) {
        int comma = -1;
        for (int i = 0; i < inner.len && comma < 0; i++) {
            if (inner.ptr[i] == ',') comma = i;
        }
        if (comma < 0) return false;
        key_text = (str){inner.ptr, comma};
        value_text = (str){inner.ptr + comma + 1, inner.len - comma - 1};
        while (value_text.len > 0 && value_text.ptr[0] == ' ') {
            value_text.ptr++;
            value_text.len--;
        }
    }
    type parts[2] = {T_ERR, T_ERR};
    for (int i = 0; i < (set ? 1 : 2); i++) {
        const str name = i ? value_text : key_text;
        if (str_eq_c(name, "string")) {
            parts[i] = (type){TY_STRING, NULL};
        } else if (!builtin_type_named(name, &parts[i])) {
            decl *d = find_type(c, name, at);
            if (d) {
                parts[i] = decl_type(d);
            } else {
                diag_error(at, "unknown type '" STR_FMT "'", STR_ARG(name));
                suggestion sg = suggest_start(name);
                suggest_builtin_types(&sg);
                suggest_structs(&sg, c->prog);
                suggest_note(&sg);
                parts[i] = T_ERR;
            }
        }
    }
    bool ok = map_key_ok(parts[0], at, set);
    if (!set && !list_element_ok(parts[1], at)) ok = false;
    *out = ok ? map_of(c->prog, parts[0], parts[1], set) : T_ERR;
    return true;
}

// ---------------------------------------------------------------------------
// Results: T? and `T fails E`

// One declaration for each combination, so two calls of functions that return
// the same have the same type. `error` is TY_VOID for T?.
static type result_of(program *prog, const type value, const type error, const bool optional)
{
    const type_kind kind = optional ? TY_OPTIONAL : TY_FAILABLE;
    for (int i = 0; i < prog->results.count; i++) {
        decl *d = prog->results.items[i];
        const type v = d->fields.items[0].type;
        const type e = d->fields.count > 1 ? d->fields.items[1].type : (type){TY_VOID, NULL};
        const bool is_optional = d->fields.count == 1;
        if (is_optional == optional && v.kind == value.kind && v.decl == value.decl && e.kind == error.kind && e.decl == error.decl) {
            return (type){kind, d};
        }
    }
    decl *d = NEW(decl);
    d->kind = DECL_RESULT;
    d->builtin = true;
    sb name = {0};
    if (optional) sb_printf(&name, "%s?", type_name(value));
    else sb_printf(&name, "%s fails %s", value.kind == TY_VOID ? "void" : type_name(value), type_name(error));
    d->name = (str){name.data, (int)name.len};
    field f = {0};
    f.name = str_from("value");
    f.type = value;
    vec_push(d->fields, f);
    if (!optional) {
        f.name = str_from("error");
        f.type = error;
        vec_push(d->fields, f);
    }
    d->index = prog->results.count;
    vec_push(prog->results, d);
    return (type){kind, d};
}

// T? or `T fails E`: something to unwrap before its value is used.
static bool is_wrapped(const type t)
{
    return t.kind == TY_OPTIONAL || t.kind == TY_FAILABLE;
}

// "int?": a T?, whose T goes in `*inner`.
static bool optional_name(const str name, str *inner)
{
    if (name.len < 2 || name.ptr[name.len - 1] != '?') return false;
    *inner = (str){name.ptr, name.len - 1};
    return true;
}

// Whether a T? or a failable call can give a value of type `t`: values a
// function can return, but the devices or code.
static bool result_value_ok(const type t, const loc at, const char *what)
{
    if (t.kind == TY_RECORD || t.kind == TY_ACTION || t.kind == TY_SINGLETON) {
        diag_error(at, "%s can't be %s", what, type_name(t));
        return false;
    }
    return t.kind != TY_ERROR;
}

// T? for a declared type `t`.
static type optional_type(const checker *c, const type t, const loc at)
{
    if (t.kind == TY_VOID) {
        diag_error(at, "'void?' isn't a type: a function that returns nothing has nothing to leave out");
        return T_ERR;
    }
    if (!result_value_ok(t, at, "a T?'s value")) return T_ERR;
    return result_of(c->prog, t, (type){TY_VOID, NULL}, true);
}

// A T?'s or a failable call's value, and a failable call's error.
static type wrapped_value(const type t)
{
    return t.decl->fields.items[0].type;
}

static type wrapped_error(const type t)
{
    return t.decl->fields.items[1].type;
}

// "int score" in the notes: the name `is` would give a value of type `t`.
static const char *pattern_example(const type t, const bool error)
{
    static char buf[2][160];
    static int next;
    char *b = buf[next++ % 2];
    snprintf(b, sizeof buf[0], "%s %s", type_name(t), error ? "why" : "value");
    return b;
}

// What an expression that has to be unwrapped is, for messages: "'Parse'"
// for a call, "'score'" for a variable.
static const char *wrapped_what(const expr *e)
{
    static char buf[160];
    if ((e->kind == E_CALL || e->kind == E_METHOD) && e->method) {
        snprintf(buf, sizeof buf, "'" STR_FMT "'", STR_ARG(e->method->name));
    } else if (e->kind == E_NAME) {
        snprintf(buf, sizeof buf, "'" STR_FMT "'", STR_ARG(e->name));
    } else {
        snprintf(buf, sizeof buf, "this");
    }
    return buf;
}

// The quick fixes for a failable call's or T?'s value that nothing handles:
// `!` after it, and `try` before a call whose error the function it's in
// fails with too.
static void unwrap_fix(const checker *c, const expr *e, const type t)
{
    const decl *m = c->method;
    const type error = t.kind == TY_FAILABLE ? wrapped_error(t) : T_ERR;
    const bool passes_on = t.kind == TY_FAILABLE && e->kind != E_AWAIT && m && m->fails.kind != TY_VOID
                        && !m->takes_action && m->fails.kind == error.kind && m->fails.decl == error.decl;
    const fix f = {.kind = FIX_UNWRAP, .at = e->at, .method = passes_on ? m : NULL, .call = e};
    vec_push(c->prog->fixes, f);
}

// A failable call's or T?'s value used without unwrapping it: says how to.
static void unwrap_error(const expr *e, const type t)
{
    const char *what = wrapped_what(e);
    const bool call = what[0] == '\'' && (e->kind == E_CALL || e->kind == E_METHOD);
    const type value = wrapped_value(t);
    if (t.kind == TY_OPTIONAL) {
        diag_error(e->at, "%s %s %s, a value or nothing, so it needs unwrapping first", what, call ? "returns" : "is",
                   type_name(t));
        diag_note("write '?? fallback' for a value when there's none, 'is %s' to use it only when it's there, or "
                  "'!' for the default when there's none", pattern_example(value, false));
        return;
    }
    const type error = wrapped_error(t);
    if (value.kind == TY_VOID) {
        diag_error(e->at, "%s %s and gives no value", what, call ? "can fail" : "holds a call that can fail,");
        diag_note("handle its error with 'is %s', pass it on with 'try', or carry on without it with '!'",
                  pattern_example(error, true));
        return;
    }
    diag_error(e->at, "%s %s, so its value needs unwrapping first", what,
               call ? "can fail" : "holds the result of a call that can fail");
    diag_note("write '?? fallback' for a value when it fails, 'is %s' to use it only when it succeeds, '!' for "
              "%s's default when it fails, or 'try' to pass the %s on", pattern_example(value, false), type_name(value),
              type_name(error));
}

// What a list can hold: values, never ECS data, Actions or other lists.
static bool list_element_ok(const type t, const loc at)
{
    switch (t.kind) {
    case TY_ERROR: return false;
    case TY_BOOL: case TY_BYTE: case TY_INT: case TY_LONG: case TY_INT2: case TY_INT3: case TY_INT4: case TY_FLOAT: case TY_FLOAT2: case TY_FLOAT3:
    case TY_FLOAT4: case TY_QUATERNION: case TY_FLOAT2X2: case TY_FLOAT3X3: case TY_FLOAT4X4: case TY_ENTITY:
    case TY_LOCAL_ENTITY: case TY_PLAYER: case TY_COLOR: case TY_RECT: case TY_STRING: case TY_ENUM:
        return true;
    case TY_STRUCT:
        if (holds_render_resource(t)) {
            diag_error(at, "a list or a map can't hold rendering resources, including inside a struct");
            return false;
        }
        if (!holds_list(t)) return true;
        diag_error(at, "a list or a map can't hold '" STR_FMT "': it has a list or a map in it", STR_ARG(t.decl->name));
        diag_note("lists and maps can't hold lists or maps yet");
        return false;
    case TY_LIST:
    case TY_MAP:
    case TY_SET:
        diag_error(at, "a list or a map can't hold lists or maps yet");
        return false;
    default:
        diag_error(at, "a list or a map holds values, like numbers, text, enums and structs, not %s", type_name(t));
        if (t.kind == TY_COMPONENT) diag_note("to keep entities in a list, keep their Entity");
        return false;
    }
}

// "List<X>": the list type, in `*out`; false for text that isn't one.
static bool resolve_list_type(const checker *c, const str text, const loc at, type *out)
{
    if (text.len < 7 || memcmp(text.ptr, "List<", 5) != 0 || text.ptr[text.len - 1] != '>') return false;
    const str inner = {text.ptr + 5, text.len - 6};
    type element = T_ERR;
    if (str_eq_c(inner, "string")) {
        element = (type){TY_STRING, NULL};
    } else if (!builtin_type_named(inner, &element)) {
        decl *d = find_type(c, inner, at);
        if (d) {
            element = decl_type(d);
        } else {
            diag_error(at, "unknown type '" STR_FMT "'", STR_ARG(inner));
            suggestion sg = suggest_start(inner);
            suggest_builtin_types(&sg);
            suggest_structs(&sg, c->prog);
            suggest_note(&sg);
        }
    }
    *out = list_element_ok(element, at) ? list_of(c->prog, element) : T_ERR;
    return true;
}

// ---------------------------------------------------------------------------
// Grids

// Grid2<T>'s or Grid3<T>'s cell type.
static type grid_cell(const type t)
{
    return t.decl->fields.items[0].type;
}

// Grid2<T> or Grid3<T> for cell type `cell`: one declaration for each cell
// type and dimensions, so two grids of ints are the same type.
static type grid_of(program *prog, const type cell, const int dims)
{
    for (int i = 0; i < prog->grids.count; i++) {
        const decl *g = prog->grids.items[i];
        const type t = g->fields.items[0].type;
        if (g->dims == dims && t.kind == cell.kind && t.decl == cell.decl) return (type){TY_GRID, prog->grids.items[i]};
    }
    decl *d = NEW(decl);
    d->kind = DECL_GRID;
    d->builtin = true;
    d->dims = dims;
    sb name = {0};
    sb_printf(&name, "Grid%d<%s>", dims, type_name(cell));
    d->name = (str){name.data, (int)name.len};
    const field item = {str_from("cell"), str_from(""), {0, 0, 0}, cell, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    vec_push(d->fields, item);
    d->index = prog->grids.count;
    vec_push(prog->grids, d);
    return (type){TY_GRID, d};
}

static bool holds_grid(type t);

// What a grid's cells can be: plain values, which a chunk keeps side by side.
static bool grid_cell_ok(const type t, const loc at)
{
    switch (t.kind) {
    case TY_ERROR: return false;
    case TY_BOOL: case TY_BYTE: case TY_INT: case TY_LONG: case TY_INT2: case TY_INT3: case TY_INT4: case TY_FLOAT: case TY_FLOAT2: case TY_FLOAT3:
    case TY_FLOAT4: case TY_QUATERNION: case TY_FLOAT2X2: case TY_FLOAT3X3: case TY_FLOAT4X4: case TY_ENTITY:
    case TY_LOCAL_ENTITY: case TY_PLAYER: case TY_COLOR: case TY_RECT: case TY_ENUM:
        return true;
    case TY_STRUCT:
        if (holds_render_resource(t)) {
            diag_error(at, "a grid's cells can't hold rendering resources, including inside a struct");
            return false;
        }
        if (!holds_heap(t) && !holds_grid(t)) return true;
        diag_error(at, "a grid's cells can't be '" STR_FMT "': it holds text or lists", STR_ARG(t.decl->name));
        diag_note("cells are plain values, side by side in their chunk: keep the text or list elsewhere, and a number for it in the cell");
        return false;
    case TY_STRING:
        diag_error(at, "a grid's cells can't be text");
        diag_note("cells are plain values, side by side in their chunk: keep the text elsewhere, and a number for it in the cell");
        return false;
    case TY_LIST:
    case TY_MAP:
    case TY_SET:
    case TY_GRID:
        diag_error(at, "a grid's cells can't be %s", t.kind == TY_LIST ? "lists" : t.kind == TY_GRID ? "grids" : "maps");
        return false;
    default:
        diag_error(at, "a grid's cells are values, like numbers, enums and structs, not %s", type_name(t));
        if (t.kind == TY_COMPONENT) diag_note("to keep entities in a grid, keep their Entity");
        return false;
    }
}

// "Grid2<X>" or "Grid3<X>": the grid type, in `*out`; false for text that isn't one.
static bool resolve_grid_type(const checker *c, const str text, const loc at, type *out)
{
    if (text.len < 8 || memcmp(text.ptr, "Grid", 4) != 0 || (text.ptr[4] != '2' && text.ptr[4] != '3') || text.ptr[5] != '<'
        || text.ptr[text.len - 1] != '>') {
        return false;
    }
    const str inner = {text.ptr + 6, text.len - 7};
    type cell = T_ERR;
    if (str_eq_c(inner, "string")) {
        cell = (type){TY_STRING, NULL};
    } else if (!builtin_type_named(inner, &cell)) {
        decl *d = find_type(c, inner, at);
        if (d) {
            cell = decl_type(d);
        } else {
            diag_error(at, "unknown type '" STR_FMT "'", STR_ARG(inner));
            suggestion sg = suggest_start(inner);
            suggest_builtin_types(&sg);
            suggest_structs(&sg, c->prog);
            suggest_note(&sg);
        }
    }
    *out = grid_cell_ok(cell, at) ? grid_of(c->prog, cell, text.ptr[4] - '0') : T_ERR;
    return true;
}

// Whether `e`, or what it's a member of, is a list's element or a grid's
// cell: a copy, which can't be changed where it is.
static bool through_element(const expr *e)
{
    for (; e && (e->kind == E_MEMBER || e->kind == E_INDEX); e = e->object) {
        if (e->kind == E_INDEX
            && (e->object->type.kind == TY_LIST || e->object->type.kind == TY_MAP || e->object->type.kind == TY_GRID)) {
            return true;
        }
    }
    return false;
}

// Types `==` compares: what a list's Contains, IndexOf and Remove can look for.
static bool equatable(const type t)
{
    switch (t.kind) {
    case TY_BYTE: case TY_INT: case TY_LONG: case TY_FLOAT: case TY_BOOL: case TY_ENUM: case TY_ENTITY: case TY_LOCAL_ENTITY: case TY_PLAYER:
    case TY_STRING:
        return true;
    default:
        return false;
    }
}

static type check_list_literal(checker *c, expr *e, type want);
static type check_list_method(checker *c, expr *e, type list);
static type check_map_method(checker *c, expr *e, type map);
static bool always_returns(const stmt *s);
static void check_stmt(checker *c, stmt *s);
static type check_grid_method(checker *c, expr *e, type grid);
static type check_grid_sample(const checker *c, expr *e, type grid);
static type check_new_grid(checker *c, expr *e, type want);
static bool is_new_grid(const expr *e);

// `default` where a value of `want` goes: that type's default value, the one
// a field of it starts at. Types with fields take their declared defaults, as
// in `Stats { }`; everything else is zero, false, empty or null.
static type check_default_value(checker *c, expr *e, const type want)
{
    e->type = T_ERR;
    if (want.kind == TY_ERROR || want.kind == TY_VOID) return T_ERR; // Reported where it goes
    if (want.kind == TY_RECORD || want.kind == TY_ACTION) {
        diag_error(e->at, "'default' can't be %s", type_name(want));
        if (want.kind == TY_RECORD) diag_note("only this machine's devices make one");
        return T_ERR;
    }
    e->type = want;
    if (want.kind == TY_STRING || want.kind == TY_LIST || want.kind == TY_MAP || want.kind == TY_SET) c->prog->uses_text = true;
    return want;
}

// `null` where a value of `want` goes that isn't a T?.
static void null_error(const expr *e, const type want)
{
    if (want.kind == TY_ERROR) return;
    diag_error(e->at, "'null' is the nothing of a T? value, and this is %s", type_name(want));
    if (want.kind == TY_ENTITY || want.kind == TY_LOCAL_ENTITY || want.kind == TY_PLAYER) {
        diag_note("the null %s is 'default'", want.kind == TY_PLAYER ? "player" : "entity");
    } else if (want.kind != TY_VOID && want.kind != TY_FAILABLE) {
        diag_note("to hold a value or nothing, make it '%s?'", type_name(want));
    }
}

static bool fold_int(const expr *e, int64_t *out);

// An expression where a value of `want` goes, which may be a T? or a failable
// call's value: [a, b], `default` and `null` take their type from it.
static type check_expr_want_any(checker *c, expr *e, const type want)
{
    // As in C#, `default` of a T? is nothing.
    if (want.kind == TY_OPTIONAL && (e->kind == E_NULL || e->kind == E_DEFAULT)) {
        e->type = want;
        return want;
    }
    const type inner = want.kind == TY_OPTIONAL ? wrapped_value(want) : want;
    if (e->kind == E_LIST && (inner.kind == TY_LIST || inner.kind == TY_SET || inner.kind == TY_MAP || inner.kind == TY_ERROR)) {
        return check_list_literal(c, e, inner);
    }
    if (is_new_grid(e)) return check_new_grid(c, e, inner);
    if (e->kind == E_DEFAULT) return check_default_value(c, e, inner);
    if (e->kind == E_NULL) {
        null_error(e, want);
        return e->type = T_ERR;
    }
    const type t = check_expr_any(c, e);
    // As in C#, int constants fit a byte where the expected type supplies it.
    int64_t value;
    if (inner.kind == TY_BYTE && t.kind == TY_INT && fold_int(e, &value) && value >= 0 && value <= 255) {
        e->type = inner;
        return inner;
    }
    return t;
}

// An expression where a value of `want` goes: [a, b] and `default` take their
// type from it. A T? or a failable call's value only goes where its own type
// does; elsewhere it has to be unwrapped.
static type check_expr_want(checker *c, expr *e, const type want)
{
    const type t = check_expr_want_any(c, e, want);
    if (is_wrapped(t) && !(t.kind == want.kind && t.decl == want.decl)) {
        unwrap_error(e, t);
        unwrap_fix(c, e, t);
        return e->type = T_ERR; // Reported: what takes it doesn't again
    }
    return t;
}

// The `default` arguments of a built-in call, which resolving it left
// unchecked: they take the types of the version it picked.
static void check_default_args(checker *c, expr *e)
{
    for (int i = 0; i < e->args.count && i < e->arg_want.count; i++) {
        if (e->args.items[i]->kind == E_DEFAULT) check_default_value(c, e->args.items[i], e->arg_want.items[i]);
    }
}

static bool check_component_side(const checker *c, const expr *arg, const decl *d);
static bool check_writable(checker *c, expr *target, const decl *called, const param *arg_of);
static type check_random_seeded(checker *c, expr *e);

static comp_mask bit(const decl *component)
{
    return mask_bit(component->index);
}

// Checks `Transform { position = ... }`, `Stats { armor = 2 }` and `Hit { damage = 5 }`.
static type check_literal(checker *c, expr *e)
{
    decl *d = find_type(c, e->name, e->at);
    if (!d || (d->kind != DECL_COMPONENT && d->kind != DECL_STRUCT && d->kind != DECL_EVENT)) {
        if (d) {
            diag_error(e->at, "'" STR_FMT "' isn't a component, struct or event; only those have values like this",
                       STR_ARG(e->name));
        } else {
            diag_error(e->at, "unknown component, struct or event '" STR_FMT "'", STR_ARG(e->name));
            suggestion s = suggest_start(e->name);
            suggest_decls(&s, c->prog, true, false, false);
            suggest_structs(&s, c->prog);
            suggest_events(&s, c->prog);
            suggest_note(&s);
        }
        for (int i = 0; i < e->inits.count; i++) check_expr_want(c, e->inits.items[i].value, T_ERR);
        return T_ERR;
    }
    e->type_decl = d;

    for (int i = 0; i < e->inits.count; i++) {
        field_init *init = &e->inits.items[i];
        for (int j = 0; j < d->fields.count; j++) {
            if (str_eq(d->fields.items[j].name, init->name) && !d->fields.items[j].hidden) init->field = &d->fields.items[j];
        }
        const type value = check_expr_want(c, init->value, init->field ? init->field->type : T_ERR);
        if (!init->field && local_field_of(d, init->name)) {
            diag_error(init->at, "'" STR_FMT "' is a local field of '" STR_FMT "', so a value of it doesn't have it",
                       STR_ARG(init->name), STR_ARG(d->name));
            diag_note("local fields are this machine's, outside the match: each starts at its default, and a view sets it "
                      "through its parameter, like 'view Name(mut " STR_FMT " %s) { %s." STR_FMT " = ...; }'",
                      STR_ARG(d->name), example_name(d), example_name(d), STR_ARG(init->name));
            continue;
        }
        if (!init->field) {
            diag_error(init->at, "%s '" STR_FMT "' has no field '" STR_FMT "'",
                       d->kind == DECL_STRUCT ? "struct" : d->kind == DECL_EVENT ? "event" : "component", STR_ARG(d->name),
                       STR_ARG(init->name));
            suggestion s = suggest_start(init->name);
            suggest_fields(&s, d);
            suggest_note(&s);
            continue;
        }
        for (int j = 0; j < i; j++) {
            if (str_eq(e->inits.items[j].name, init->name)) {
                diag_error(init->at, "field '" STR_FMT "' is set twice", STR_ARG(init->name));
            }
        }
        if (!type_assignable(init->field->type, value)) {
            diag_error(init->value->at, "field '" STR_FMT "' is %s, not %s", STR_ARG(init->name),
                       type_name(init->field->type), type_name(value));
            note_conversion(init->field->type, value);
        }
    }
    return decl_type(d);
}

// A scene where Spawn, Add or Remove wants a component: scenes are loaded and
// unloaded with their own calls.
static void scene_not_component(const expr *arg, const decl *d, const char *fn)
{
    diag_error(arg->at, "'" STR_FMT "' is a scene, so %s can't take it", STR_ARG(d->name), fn);
    if (str_eq_c(str_from(fn), "Remove")) diag_note("unload the scene instead: 'Scene.Unload(scene)'");
    else diag_note("load it instead: 'Scene.Load(" STR_FMT " { ... })'", STR_ARG(d->name));
}

// A component argument to Spawn or Add: `Player` (defaults) or `Player { ... }`.
// Returns the component, or NULL after reporting an error.
static decl *check_component_arg(checker *c, expr *arg, const char *fn)
{
    if (arg->kind == E_LITERAL) {
        const type t = check_literal(c, arg);
        if (t.kind == TY_COMPONENT && t.decl->is_scene) {
            scene_not_component(arg, t.decl, fn);
            return NULL;
        }
        if (t.kind == TY_STRUCT) {
            diag_error(arg->at, "%s takes components, and '" STR_FMT "' is a struct", fn, STR_ARG(t.decl->name));
            diag_note("put it in a component, like 'component Name { " STR_FMT " value; }'", STR_ARG(t.decl->name));
        } else if (t.kind == TY_EVENT) {
            diag_error(arg->at, "%s takes components, and '" STR_FMT "' is an event", fn, STR_ARG(t.decl->name));
            diag_note("send it instead, like 'Send(" STR_FMT " { ... })'", STR_ARG(t.decl->name));
        }
        return t.kind == TY_COMPONENT ? t.decl : NULL;
    }
    // Player, or Combat.Health
    str name;
    const expr *root = arg->kind == E_NAME || arg->kind == E_MEMBER ? chain_root(arg) : NULL;
    if (root && root->kind == E_NAME && !find_local(c, root->name) && !find_param(c, root->name)
        && qualified_text(arg, &name)) {
        decl *d = find_type(c, name, arg->at);
        if (d && d->kind == DECL_COMPONENT) {
            if (arg->kind == E_MEMBER) mark_namespaces(arg->object);
            arg->bind = BIND_TYPE;
            arg->type_decl = d;
            arg->type = (type){TY_COMPONENT, d};
            if (d->is_scene) {
                scene_not_component(arg, d, fn);
                return NULL;
            }
            return d;
        }
        if (d && (d->kind == DECL_STRUCT || d->kind == DECL_EVENT)) {
            if (arg->kind == E_MEMBER) mark_namespaces(arg->object);
            arg->bind = BIND_TYPE;
            arg->type_decl = d;
            diag_error(arg->at, "%s takes components, and '" STR_FMT "' is %s", fn, STR_ARG(d->name), decl_what(d));
            if (d->kind == DECL_STRUCT) {
                diag_note("put it in a component, like 'component Name { " STR_FMT " value; }'", STR_ARG(d->name));
            } else {
                diag_note("send it instead, like 'Send(" STR_FMT ")'", STR_ARG(d->name));
            }
            return NULL;
        }
    }
    const type t = check_expr(c, arg);
    if (t.kind != TY_ERROR) {
        diag_error(arg->at, "%s takes components, like 'Player' or 'Player { speed = ... }'", fn);
    }
    return NULL;
}

// Component list shared by Spawn and Add. Returns the mask of components.
// An Add or Remove as a change site (ast.h): whose entity it changes, as far
// as the code says. `this` is the entity the system, handler or view runs for,
// which its filters place; a local that isn't mut, set to a Spawn or
// Scene.Load, is the entity that made, exactly; anything else may be any
// entity of its world.
static void record_change(const checker *c, const expr *e, const comp_mask comps, change_sites *sites)
{
    change_site s = {comps, NULL, NULL, e->local_world};
    const expr *o = e->object;
    if (o->kind == E_THIS) {
        if (c->system && c->system->kind == DECL_SYSTEM && c->system->per_entity) s.from = c->system;
    } else if (o->kind == E_NAME && o->bind == BIND_LOCAL && o->local && !o->local->is_mut && o->local->value) {
        const expr *v = o->local->value;
        if (v->kind == E_CALL && (v->call == CALL_SPAWN || v->call == CALL_LOAD)) s.spawn = v;
    }
    vec_push(*sites, s);
}

static comp_mask check_component_list(checker *c, const expr *call, const char *fn)
{
    comp_mask mask = mask_none();
    for (int i = 0; i < call->args.count; i++) {
        decl *d = check_component_arg(c, call->args.items[i], fn);
        if (!d || !check_component_side(c, call->args.items[i], d)) continue;
        if (mask_has(mask, d->index)) {
            diag_error(call->args.items[i]->at, "'" STR_FMT "' appears twice; an entity has at most one of each component",
                       STR_ARG(d->name));
        }
        mask = mask_or(mask, bit(d));
    }
    return mask;
}

// Constructors of built-in types: float(x), int3(1), float4(v.xy, 0, 1),
// quaternion(x, y, z, w), float3x3(c0, c1, c2) and friends.
static type check_construct(checker *c, expr *e, const type target)
{
    bool any_error = false;
    for (int i = 0; i < e->args.count; i++) {
        if (check_expr(c, e->args.items[i]).kind == TY_ERROR) any_error = true;
    }
    if (any_error) return T_ERR;

    e->call = CALL_CONSTRUCT;
    const int argc = e->args.count;
    const char *name = type_name(target);
    const type first = argc > 0 ? e->args.items[0]->type : T_VOID_;

    if (target.kind == TY_RENDER_TARGET) {
        if (gpu_refuses(c, e->at, "create a RenderTarget", "the view creates it and supplies its color image as a Texture")) return T_ERR;
        if (!reading_code(c)) {
            diag_error(e->at, "a RenderTarget is created in local code, not a field default or constant");
            diag_note("leave the field at default and create its target in a view or local handler");
            return T_ERR;
        }
        if (argc != 2 || !type_assignable(T_INT_, first) || !type_assignable(T_INT_, e->args.items[1]->type)) {
            diag_error(e->at, "RenderTarget takes its width and height in pixels, two ints: 'RenderTarget(width, height)'");
            return T_ERR;
        }
        e->call = CALL_BUILTIN;
        e->c_callee = "tide_render_target_make";
        e->resource_operation = true;
        reading_code(c)->calls_c = true;
        vec_push(e->arg_want, T_INT_);
        vec_push(e->arg_want, T_INT_);
        return target;
    }
    if (target.kind == TY_TEXTURE) {
        diag_error(e->at, "%s has no constructor yet", name);
        return T_ERR;
    }

    // PlayerID(0): a player by index, for local play and tests.
    if (target.kind == TY_PLAYER) {
        if (argc == 1 && type_assignable(T_INT_, first)) {
            e->ctor = CTOR_PLAYER;
            return target;
        }
        diag_error(e->at, "PlayerID(...) takes a player index, an int");
        return T_ERR;
    }

    // Color(r, g, b) with alpha 1, or Color(r, g, b, a).
    if (target.kind == TY_COLOR) {
        bool scalars = argc == 3 || argc == 4;
        for (int i = 0; i < argc; i++) {
            if (!is_scalar_number(e->args.items[i]->type)) scalars = false;
        }
        if (scalars) {
            e->ctor = CTOR_COLOR;
            return target;
        }
        diag_error(e->at, "Color takes (r, g, b) or (r, g, b, a), numbers from 0 to 1");
        return T_ERR;
    }

    // Rect(x, y, width, height), from the top left.
    if (target.kind == TY_RECT) {
        bool scalars = argc == 4;
        for (int i = 0; i < argc; i++) {
            if (!is_scalar_number(e->args.items[i]->type)) scalars = false;
        }
        if (scalars) {
            e->ctor = CTOR_RECT;
            return target;
        }
        diag_error(e->at, "Rect takes (x, y, width, height), from the top left of the screen with y down");
        return T_ERR;
    }

    // int(page), long(page): an enum member's value.
    if (type_is_integer(target) && argc == 1 && first.kind == TY_ENUM) {
        e->ctor = CTOR_SCALAR;
        return target;
    }

    // float(x), int(x), long(x): conversions between the number types.
    if (is_scalar_number(target) || target.kind == TY_LONG) {
        if (argc == 1 && (is_scalar_number(first) || first.kind == TY_LONG)) {
            e->ctor = CTOR_SCALAR;
            return target;
        }
        diag_error(e->at, "%s(...) converts one number", name);
        return T_ERR;
    }

    const int dim = type_dim(target);
    if (dim >= 2) {
        if (argc == 1 && is_scalar_number(first)) {
            e->ctor = CTOR_SPLAT;
            return target;
        }
        if (argc == 1 && type_dim(first) == dim && first.kind != target.kind) {
            e->ctor = CTOR_CONVERT;
            return target;
        }
        int total = 0;
        for (int i = 0; i < argc; i++) {
            const type t = e->args.items[i]->type;
            if (!type_is_numeric(t)) {
                diag_error(e->args.items[i]->at, "%s takes numbers and vectors, not %s", name, type_name(t));
                return T_ERR;
            }
            if (type_is_int_based(target) && type_is_float_based(t)) {
                diag_error(e->args.items[i]->at, "%s takes ints, not %s", name, type_name(t));
                diag_note("convert explicitly, for example int(x) or %s(v)", name);
                return T_ERR;
            }
            total += type_dim(t);
        }
        if (total != dim) {
            diag_error(e->at, "%s needs %d components, got %d", name, dim, total);
            return T_ERR;
        }
        e->ctor = CTOR_COMPONENTS;
        return target;
    }

    if (target.kind == TY_QUATERNION) {
        bool scalars = argc == 4;
        for (int i = 0; i < argc; i++) {
            if (!is_scalar_number(e->args.items[i]->type)) scalars = false;
        }
        if (scalars) {
            e->ctor = CTOR_COMPONENTS;
            return target;
        }
        if (argc == 1 && first.kind == TY_FLOAT4) {
            e->ctor = CTOR_QUAT_FROM_F4;
            return target;
        }
        if (argc == 1 && first.kind == TY_FLOAT3X3) {
            e->ctor = CTOR_QUAT_FROM_MAT;
            return target;
        }
        diag_error(e->at, "quaternion takes (x, y, z, w), a float4 or a float3x3");
        return T_ERR;
    }

    const int n = matrix_dim(target);
    if (n > 0) {
        const type column = vector_type(true, n);
        bool columns = argc == n;
        bool scalars = argc == n * n;
        for (int i = 0; i < argc; i++) {
            const type t = e->args.items[i]->type;
            if (!type_assignable(column, t)) columns = false;
            if (!is_scalar_number(t)) scalars = false;
        }
        if (columns) {
            e->ctor = CTOR_MAT_COLUMNS;
            return target;
        }
        if (scalars) {
            e->ctor = CTOR_MAT_SCALARS;
            return target;
        }
        if (n == 3 && argc == 1 && first.kind == TY_QUATERNION) {
            e->ctor = CTOR_MAT_FROM_QUAT;
            return target;
        }
        if (n == 4 && argc == 2 && first.kind == TY_FLOAT3X3 && type_assignable((type){TY_FLOAT3, NULL}, e->args.items[1]->type)) {
            e->ctor = CTOR_MAT_FROM_ROT_T;
            return target;
        }
        diag_error(e->at, "%s takes %d float%d columns or %d numbers row by row", name, n, n, n * n);
        if (n == 3) diag_note("float3x3(quaternion) builds a rotation matrix");
        if (n == 4) diag_note("float4x4(float3x3 rotation, float3 translation) builds a transform");
        return T_ERR;
    }

    diag_error(e->at, "%s has no constructor", name);
    return T_ERR;
}

static bool names_only(const expr *e);

// Card(2), or Combat.Stance(2): the value of enum `d` with that number, as
// int(card) is a value's number. A number none of its members has is kept, as
// C#'s cast keeps it, cut to what the enum is stored as the way byte(x) cuts
// an int to a byte.
static type check_enum_conversion(checker *c, expr *e, decl *d)
{
    // Its name is the enum's, for editors, whatever its arguments are: it's a call only once they're right
    e->type_decl = d;
    e->ctor = CTOR_ENUM;
    const type target = {TY_ENUM, d};
    bool any_error = false;
    for (int i = 0; i < e->args.count; i++) {
        if (check_expr_want(c, e->args.items[i], T_INT_).kind == TY_ERROR) any_error = true;
    }
    if (any_error) return T_ERR;
    const char *name = type_name(target);
    if (e->args.count != 1) {
        diag_error(e->at, "%s(...) converts one int, a member's number", name);
        if (d->members.count > 0) {
            diag_note("a member is written with its enum, like '%s." STR_FMT "'", name, STR_ARG(d->members.items[0].name));
        }
        return T_ERR;
    }
    const expr *arg = e->args.items[0];
    const type from = arg->type;
    if (!type_assignable(T_INT_, from)) {
        diag_error(arg->at, "%s(...) converts an int, not %s", name, type_name(from));
        if (from.kind == TY_FLOAT) diag_note("convert it to an int first, which drops its fraction: '%s(int(x))'", name);
        else if (from.kind == TY_LONG) diag_note("convert it to an int first, which keeps its low 32 bits: '%s(int(x))'", name);
        else if (from.kind == TY_ENUM && from.decl == d) diag_note("it already is one: use it as it is");
        else if (from.kind == TY_ENUM) diag_note("go through its number: '%s(int(x))'", name);
        return T_ERR;
    }
    e->call = CALL_CONSTRUCT;
    return target;
}

// The enum a call converts an int to, going by the name it's written with:
// Card in Card(2), Combat.Stance in Combat.Stance(2). NULL for any other call.
static decl *converted_enum(const checker *c, const expr *e)
{
    decl *other;
    str name = e->name;
    if (e->kind == E_METHOD) {
        str ns;
        if (!names_only(e->object) || !qualified_text(e->object, &ns)) return NULL;
        char *text = arena_alloc((size_t)ns.len + (size_t)e->name.len + 2);
        memcpy(text, ns.ptr, (size_t)ns.len);
        text[ns.len] = '.';
        memcpy(text + ns.len + 1, e->name.ptr, (size_t)e->name.len);
        name = (str){text, ns.len + 1 + e->name.len};
    }
    decl *d = lookup(c->prog, c->unit, name, NAME_TYPE, &other);
    return d && d->kind == DECL_ENUM ? d : NULL;
}

// The input code being checked, for messages.
static const char *input_code(const checker *c)
{
    return c->in_sanitize ? "Sanitize" : "Sample";
}

static bool in_view(const checker *c)
{
    return c->system && c->system->is_view;
}

// Local code runs on this machine: views, and local event handlers.
static bool is_local_code(const decl *d)
{
    return d && d->kind == DECL_SYSTEM && (d->is_view || d->is_local);
}

// "a view" or "a local handler", for messages about local code.
static const char *local_code_what(const decl *d)
{
    return d->is_view ? "a view" : "a local handler";
}


// Whether the code being checked is local.
static bool local_code(const checker *c)
{
    return is_local_code(c->system);
}

// Whether code on this side may create, change or remove component `d`. Local
// code only changes local state; match code never touches it.
static bool check_component_side(const checker *c, const expr *arg, const decl *d)
{
    if (in_async_function(c)) return note_task_side(c, d->is_local ? TASK_LOCAL : TASK_MATCH, arg->at);
    if (d->is_local == local_code(c)) return true;
    if (d->is_local) {
        diag_error(arg->at, "'" STR_FMT "' is local, and the match can't use local state", STR_ARG(d->name));
        diag_note("local state belongs to one machine, and the match runs the same on every one");
    } else {
        diag_error(arg->at, "%s can't change the match, and '" STR_FMT "' belongs to it", local_code_what(c->system),
                   STR_ARG(d->name));
        diag_note("put what it wants in the input, and change the match in a system");
    }
    return false;
}

static const char *routines(const checker *c);

// The frag or vert being checked, which the GPU runs, or NULL.
static const decl *gpu_code(const checker *c)
{
    return c->method && c->method->kind == DECL_SHADER ? c->method : NULL;
}

static const char *shader_word(const decl *d)
{
    return d->is_frag ? "frag" : "vert";
}

// Something only the CPU does, in a frag or vert: reports it, with what to
// write instead, and returns true. False in any other code.
static bool gpu_refuses(const checker *c, const loc at, const char *what, const char *instead)
{
    const decl *s = gpu_code(c);
    if (!s) return false;
    diag_error(at, "a %s runs on the GPU, for each %s, so it can't %s", shader_word(s), s->is_frag ? "pixel" : "vertex", what);
    if (instead) diag_note("%s", instead);
    return true;
}

// Draw2D, Draw3D, GUI, GUILayout and Screen belong to a frame: views have one, and the
// functions they call use theirs. A function that uses them draws, and only
// views and other functions can call it (see check_drawing_calls).
static bool check_frame_use(checker *c, const loc at, const char *what)
{
    if (gpu_code(c)) {
        const bool devices = strcmp(what, "Devices") == 0;
        const bool screen = strcmp(what, "Screen") == 0;
        gpu_refuses(c, at, devices ? "read the devices" : screen ? "read Screen"
                    : (builtin_draw_owner(str_from(what)) || strcmp(what, "Draw") == 0) ? "draw" : "use the GUI",
                    devices || screen ? "the view puts what it needs in a component or a singleton, which it takes"
                                      : "it writes its pixel or changes its vertex: a view draws, and names it at Draw2D.Mesh or Draw3D.Mesh");
        return false;
    }
    if (in_async_function(c)) {
        const bool devices = strcmp(what, "Devices") == 0;
        diag_error(at, "a task runs between frames, so it can't %s", devices ? "read this frame's Devices" : "draw");
        diag_note(devices ? "read them in the view that starts it, and pass it what it needs"
                          : "set what to show in local state, and draw that in a view");
        return false;
    }
    if (c->method && c->method->kind == DECL_FUNCTION) {
        if (!c->method->draws) {
            c->method->draws = true;
            c->method->draws_at = at;
            c->method->frame_devices = strcmp(what, "Devices") == 0;
        }
        return true;
    }
    if (!c->method && !c->in_input && in_view(c)) return true;
    diag_error(at, "%s can only be used in views, and in functions they call", what);
    if (c->method) diag_note("methods can't; a function can, like 'void Show(Stats stats) { ... }'");
    else if (c->in_input) diag_note("%s reads the devices; views draw, once per frame", input_code(c));
    else diag_note("views run once per frame and only read the world: 'view Name(...) { ... }'");
    return false;
}

// Storage belongs to one machine. Ordinary functions carry the local provider
// through their callers; async functions also become local tasks.
static bool check_io_use(checker *c, const loc at, const char *what)
{
    if (gpu_refuses(c, at, "access the filesystem", "read files in local code, and pass what the shader needs in its components")) return false;
    decl *code = c->method ? c->method : c->system;
    if (c->in_input || !code || (code->kind != DECL_FUNCTION && code->kind != DECL_SYSTEM)
        || (code->kind == DECL_SYSTEM && !is_local_code(code))) {
        diag_error(at, "%s can only be used in local code, and in functions it calls", what);
        diag_note("files belong to this machine; the match runs the same on every machine");
        return false;
    }
    if (!code->uses_io) code->io_at = at;
    code->uses_io = true;
    code->calls_c = true; // Ordered side effects, including reads beside writes
    if (code->is_async && code->kind == DECL_FUNCTION) note_side(code, TASK_LOCAL, at);
    return true;
}

// Frame.dt: how long this machine's frame is, which its host says before each
// one and its local state keeps. Local code reads it, and the functions it
// calls, which get the local state from their callers; an async function that
// reads it is a local task. The match's time is Time's.
static bool check_frame_time_use(checker *c, const loc at)
{
    if (gpu_refuses(c, at, "read Frame.dt", "the view puts what it needs in a component or a singleton, which it takes")) return false;
    if (c->in_input) {
        diag_error(at, "%s runs once per tick, not once per frame, so it can't read Frame.dt", input_code(c));
        diag_note(c->in_sanitize ? "a frame's length is this machine's, and Sanitize runs on every machine"
                                 : "a view keeps what the input needs in local state, which Sample reads");
        return false;
    }
    decl *code = c->method ? c->method : c->system;
    if (code && code->kind == DECL_SYSTEM && !is_local_code(code)) {
        diag_error(at, "the match runs the same on every machine, so it can't read Frame.dt");
        diag_note("a frame's length is this machine's: the match takes 'Time time' and reads 'time.dt', a tick's length");
        return false;
    }
    if (!code || (code->kind != DECL_FUNCTION && code->kind != DECL_SYSTEM)) {
        diag_error(at, "Frame.dt can only be read in local code, and in functions it calls");
        if (c->method) diag_note("methods can't; a function can, like 'void Fade(mut Glow glow) { ... }'");
        else diag_note("views, local handlers and local tasks read it: it's how long this machine's frame is");
        return false;
    }
    if (!code->frame_time) code->frame_time_at = at;
    code->frame_time = true;
    if (code->is_async && code->kind == DECL_FUNCTION) return note_side(code, TASK_LOCAL, at);
    return true;
}

// `devices.keyboard.text`: what was typed since the last frame. It's a
// frame's, as this frame's Devices are, so views read it, and the functions
// they call. The input never sends it: Sample runs once per tick, not once per
// frame, and the match can't read it.
static bool check_typed_use(checker *c, const loc at)
{
    if (c->in_input && !c->method) {
        diag_error(at, "%s can't read what's typed: it's a frame's, and %s runs once per tick", input_code(c), input_code(c));
        diag_note("views take 'Devices devices' and read 'devices.keyboard.text' once per frame, keeping what they need in local state");
        return false;
    }
    const decl *code = c->method ? NULL : c->system;
    if (code && code->kind == DECL_SYSTEM && !code->is_view && !code->is_local) {
        diag_error(at, "the match can't read what's typed: it stays on this machine, and the input never sends it");
        diag_note("views take 'Devices devices' and read 'devices.keyboard.text' once per frame, keeping what they need in local state");
        return false;
    }
    return check_frame_use(c, at, "Devices");
}

// Cursor.Lock() and Cursor.locked: the mouse's cursor is this machine's, so
// the match never calls or reads either. A view asks for the lock each frame
// it wants the mouse, as do the functions it calls, and they read whether the
// window holds it. The input's Sample reads that too, once per tick, with the
// devices it's sampling.
static bool check_cursor_use(checker *c, const loc at, const bool lock)
{
    const char *what = lock ? "call Cursor.Lock" : "read Cursor.locked";
    if (gpu_refuses(c, at, what, "the cursor is the view's: it puts what the GPU needs in a component or a singleton")) return false;
    if (c->in_input && !c->method) {
        if (!lock && !c->in_sanitize) return true;
        if (c->in_sanitize) {
            diag_error(at, "Sanitize runs on every machine for every player's input, so it can't %s", what);
            diag_note(lock ? "a view calls it, every frame the game wants the mouse"
                           : "Sample reads it, on the machine the input comes from");
        } else {
            diag_error(at, "Sample makes this machine's input once per tick, so it can't call Cursor.Lock");
            diag_note("a view calls it, every frame the game wants the mouse; Sample reads 'Cursor.locked'");
        }
        return false;
    }
    const decl *code = c->method ? NULL : c->system;
    if (code && code->kind == DECL_SYSTEM && !code->is_view && !code->is_local) {
        diag_error(at, "the match runs the same on every machine, so it can't %s", what);
        diag_note(lock ? "the cursor is this machine's: a view calls it, every frame the game wants the mouse"
                       : "the cursor is this machine's: the input's Sample reads it, and puts what the match needs in "
                         "the input, like 'if (Cursor.locked) look = devices.mouse.delta;'");
        return false;
    }
    if (in_async_function(c)) {
        diag_error(at, "a task runs between frames, so it can't %s", what);
        diag_note(lock ? "a view calls it, every frame the game wants the mouse"
                       : "read it in the view that starts it, and pass it what it needs");
        return false;
    }
    if (c->method && c->method->kind == DECL_FUNCTION) {
        if (!c->method->draws) {
            c->method->draws = true;
            c->method->draws_at = at;
            c->method->frame_cursor = true;
        }
        return true;
    }
    if (!c->method && in_view(c)) return true;
    diag_error(at, lock ? "Cursor.Lock can only be called in views, and in functions they call"
                        : "Cursor.locked can only be read in views, in functions they call, and in the input's Sample");
    if (c->method) diag_note("methods can't; a function can, like 'void Look(Camera camera) { ... }'");
    else if (lock) diag_note("it's asked every frame the game wants the mouse, and views run once per frame");
    else diag_note("views run once per frame: keep what a handler needs in local state");
    return false;
}

// The event Send takes: `RoundOver` with its defaults, `Hit { ... }`, or any
// value of an event type, like a handler's own event passed on. Returns the
// event, or NULL after reporting an error.
static decl *check_event_arg(checker *c, expr *arg)
{
    if (arg->kind == E_LITERAL) {
        const type t = check_literal(c, arg);
        if (t.kind == TY_EVENT) return t.decl;
        if (t.kind != TY_ERROR) {
            diag_error(arg->at, "Send takes events, and '" STR_FMT "' is %s", STR_ARG(t.decl->name), decl_what(t.decl));
            diag_note("declare what happened as 'event Name { ... }'");
        }
        return NULL;
    }
    // RoundOver, or Game.RoundOver
    str name;
    const expr *root = arg->kind == E_NAME || arg->kind == E_MEMBER ? chain_root(arg) : NULL;
    if (root && root->kind == E_NAME && !find_local(c, root->name) && !find_param(c, root->name)
        && qualified_text(arg, &name)) {
        decl *d = find_type(c, name, arg->at);
        if (d) {
            if (arg->kind == E_MEMBER) mark_namespaces(arg->object);
            arg->bind = BIND_TYPE;
            arg->type_decl = d;
            if (d->kind == DECL_EVENT) {
                arg->type = (type){TY_EVENT, d};
                return d;
            }
            diag_error(arg->at, "Send takes events, and '" STR_FMT "' is %s", STR_ARG(d->name), decl_what(d));
            diag_note("declare what happened as 'event Name { ... }'");
            return NULL;
        }
    }
    const type t = check_expr(c, arg);
    if (t.kind == TY_EVENT) return t.decl;
    if (t.kind != TY_ERROR) {
        diag_error(arg->at, "Send takes an event, like 'Send(RoundOver)' or 'target.Send(Hit { damage = 5 })', not %s",
                   type_name(t));
    }
    return NULL;
}

// Whether the entity `e` is called on (e.Add, e.Destroy, e.Send) is of the
// code's world: local code changes local entities, match code the match's.
static bool check_entity_side(const checker *c, const expr *e)
{
    const type_kind kind = e->object->type.kind;
    if (kind == TY_ERROR) return false;
    if (in_async_function(c)) return note_task_side(c, kind == TY_LOCAL_ENTITY ? TASK_LOCAL : TASK_MATCH, e->at);
    if (local_code(c) && kind == TY_ENTITY) {
        diag_error(e->at, "%s can't change the match, and this entity belongs to it", local_code_what(c->system));
        diag_note("put what it wants in the input, and change the match in a system");
        return false;
    }
    if (!local_code(c) && kind == TY_LOCAL_ENTITY) {
        diag_error(e->at, "a LocalEntity is local, and the match can't use local state");
        return false;
    }
    return true;
}

// Send(RoundOver { ... }) to the whole world, or target.Send(Hit { ... }) to an
// entity. Whether an event needs an entity depends on its handlers, so sends
// are checked against them once every handler is known (see check_sends).
static type check_send(checker *c, expr *e)
{
    const char *error = NULL;
    if (in_routine(c)) error = "%s can't send events; systems and event handlers do";
    else if (c->in_input) error = "%s runs outside the simulation, so it can't send events";
    if (error) {
        diag_error(e->at, error, c->method ? routines(c) : input_code(c));
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        return T_ERR;
    }
    e->call = CALL_SEND;
    e->local_world = local_code(c);
    if (e->args.count != 1) {
        diag_error(e->at, "Send takes one event, like 'Send(RoundOver)' or 'target.Send(Hit { damage = 5 })'");
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        return T_ERR;
    }
    decl *event = check_event_arg(c, e->args.items[0]);
    if (!event) return T_ERR;
    if (event->is_command) {
        if (!local_code(c)) {
            diag_error(e->at, "commands are sent from views and local handlers to the match");
            diag_note("match systems and handlers send events; a command carries a player's untrusted request");
            return T_ERR;
        }
        if (e->kind == E_METHOD && e->object->type.kind != TY_ENTITY) {
            diag_error(e->object->at, "a command target must be an Entity of the match");
            return T_ERR;
        }
        e->type_decl = event;
        e->local_world = true;
        vec_push(c->sends, e);
        return T_VOID_;
    }
    if (e->kind == E_METHOD && !check_entity_side(c, e)) return T_ERR;
    if (event->builtin) {
        diag_error(e->args.items[0]->at, "the engine sends '" STR_FMT "'; games can't", STR_ARG(event->name));
        if (event == c->prog->spawned) diag_note("it's sent to each entity as it's spawned");
        else if (event == c->prog->destroyed) diag_note("it's sent to each entity as it's destroyed");
        else if (event == c->prog->forgotten) diag_note("it's sent to an entity of the match as its local fields go");
        else if (event->is_local) diag_note("it's sent when this machine joins or leaves a match");
        else diag_note("it's sent when a player joins or leaves");
        return T_ERR;
    }
    if (in_async_function(c)) {
        if (!note_task_side(c, event->is_local ? TASK_LOCAL : TASK_MATCH, e->args.items[0]->at)) return T_ERR;
        e->local_world = event->is_local;
    }
    if (event->is_local != e->local_world) {
        if (event->is_local) {
            diag_error(e->args.items[0]->at, "'" STR_FMT "' is local, and the match can't use local state", STR_ARG(event->name));
            diag_note("local state belongs to one machine, and the match runs the same on every one");
        } else {
            diag_error(e->args.items[0]->at, "%s can't change the match, and '" STR_FMT "' is a match event",
                       local_code_what(c->system), STR_ARG(event->name));
            diag_note("put what it wants in the input, and send '" STR_FMT "' from a system", STR_ARG(event->name));
        }
        return T_ERR;
    }
    e->type_decl = event;
    note_text_write(c, decl_holds_text(event));
    vec_push(c->sends, e);
    return T_VOID_;
}

// The arguments of a call of method or function `m`, against its parameters.
// A mut parameter takes the caller's variable itself, which it changes.
static void check_method_args(checker *c, expr *e, decl *m)
{
    e->call = m->kind == DECL_FUNCTION ? CALL_FUNCTION : CALL_METHOD;
    e->method = m;
    decl *from = reading_code(c);
    if (from) {
        const call_site site = {from, m, e, c->parallel != NULL, c->compare != NULL};
        vec_push(c->calls, site);
    }
    for (int i = 0; i < e->args.count; i++) {
        // Devices passed on are read where they go: the callee's reads count.
        c->device_whole_ok = e->args.items[i];
        check_expr_want(c, e->args.items[i], i < m->params.count ? m->params.items[i].type : T_ERR);
    }
    for (int i = 0; i < m->params.count; i++) vec_push(e->arg_want, m->params.items[i].type);
    // An Action isn't an argument: it's written after the call, in braces.
    const int count = m->params.count - (m->takes_action ? 1 : 0);
    if (m->takes_action && !e->block) {
        diag_error(e->at, "'" STR_FMT "' takes a block, written after the call: '" STR_FMT "(...) { ... }'",
                   STR_ARG(m->name), STR_ARG(m->name));
    }
    if (e->args.count != count) {
        diag_error(e->at, "'" STR_FMT "' takes %d argument%s, not %d", STR_ARG(m->name), count, count == 1 ? "" : "s",
                   e->args.count);
        if (m->takes_action) diag_note("its Action isn't one: it's the code in braces after the call");
        return;
    }
    for (int i = 0; i < e->args.count; i++) {
        const param *p = &m->params.items[i];
        expr *arg = e->args.items[i];
        const bool exact = p->type.kind == arg->type.kind && p->type.decl == arg->type.decl;
        if (p->mode == PARAM_MUT && arg->type.kind != TY_ERROR && p->type.kind != TY_ERROR && !exact) {
            diag_error(arg->at, "'" STR_FMT "' changes its '" STR_FMT "', which must be %s, not %s", STR_ARG(m->name),
                       STR_ARG(p->name), type_name(p->type), type_name(arg->type));
        } else if (!type_assignable(p->type, arg->type)) {
            diag_error(arg->at, "'" STR_FMT "' takes %s for '" STR_FMT "', not %s", STR_ARG(m->name), type_name(p->type),
                       STR_ARG(p->name), type_name(arg->type));
            if (p->type.kind == TY_DRAW_LIST) diag_note("pass the frame's draw list: 'Draw2D.list'");
            note_conversion(p->type, arg->type);
        } else if (p->mode == PARAM_MUT) {
            if (through_element(arg)) {
                diag_error(arg->at, "a list's element is a copy, so '" STR_FMT "' can't change it", STR_ARG(m->name));
                diag_note("take it out, change it, and put it back: 'var e = items[i]; ...; items[i] = e;'");
            } else {
                check_writable(c, arg, m, p);
            }
        }
    }
}

// A call of async function `fn`: awaited, its frame part of the caller's, or
// as a statement, starting a task. A task belongs to a world, so plain
// functions and methods can't start one. Its components and singletons are
// looked up again each time it goes on, so they're the caller's own.
static void check_task_call(checker *c, expr *e, decl *fn)
{
    const bool awaited = c->awaiting == e;
    decl *from = reading_code(c);
    if (!awaited && c->statement_call != e) {
        diag_error(e->at, "'" STR_FMT "' is async: 'await' it for its %s, or call it as a statement to start it as a task",
                   STR_ARG(fn->name), fn->return_type.kind == TY_VOID ? "end" : "value");
        diag_note("a task goes on by itself, and a statement that starts one doesn't wait for it");
        return;
    }
    if (!awaited && (in_routine(c) || c->in_input || !from)) {
        diag_error(e->at, "'" STR_FMT "' is async, so calling it starts a task, which belongs to a world, and %s can't",
                   STR_ARG(fn->name), in_routine(c) ? routines(c) : c->in_input ? input_code(c) : "this code");
        if (in_routine(c) && c->method->kind == DECL_FUNCTION) {
            diag_note("make '" STR_FMT "' async too, or start it from the system, view or handler that calls it",
                      STR_ARG(c->method->name));
        }
        return;
    }
    // A system that starts tasks makes them in order, on one thread
    if (!awaited && !c->method && from->kind == DECL_SYSTEM) {
        if (!from->starts_tasks) from->starts_at = e->at;
        from->starts_tasks = true;
        note_text_write(c, true);
        note_spawn(c);
    }
    for (int i = 0; i < e->args.count && i < fn->params.count; i++) {
        const param *p = &fn->params.items[i];
        const expr *arg = e->args.items[i];
        if (!p->task_ref || arg->type.kind == TY_ERROR) continue;
        const bool own = arg->kind == E_NAME && arg->bind == BIND_PARAM && (!arg->param->function_param || arg->param->task_ref);
        if (own) continue;
        diag_error(arg->at, "'" STR_FMT "' gets its '" STR_FMT "' again each time it goes on, so it takes the caller's own: "
                   "a parameter", STR_ARG(fn->name), STR_ARG(p->name));
        diag_note("a task gets its components and singletons as they are: a copy would miss what changes while it waits");
    }
    if (awaited && from && from->kind == DECL_FUNCTION) {
        bool known = false;
        for (int i = 0; i < from->awaits.count; i++) known |= from->awaits.items[i] == fn;
        if (!known) {
            vec_push(from->awaits, fn);
            vec_push(from->await_at, e->at);
        }
    }
    if (from) {
        const task_call call = {from, fn, e, awaited};
        vec_push(c->task_calls, call);
    }
}

// Heal(unit.stats, 5), or Combat.Heal(...) from elsewhere.
static type check_function_call(checker *c, expr *e, decl *fn)
{
    check_method_args(c, e, fn);
    if (fn->is_async) check_task_call(c, e, fn);
    if (c->method && c->method->kind == DECL_FUNCTION) {
        bool known = false;
        for (int i = 0; i < c->method->callees.count; i++) known |= c->method->callees.items[i] == fn;
        if (!known) {
            vec_push(c->method->callees, fn);
            vec_push(c->method->callee_at, e->at);
        }
    }
    return fn->result;
}

// content(): runs the Action the function was given, where it was written.
static type check_block_call(const checker *c, expr *e, param *p)
{
    (void)c;
    e->call = CALL_ACTION;
    e->bind = BIND_PARAM;
    e->param = p;
    p->read = true;
    if (e->args.count > 0) {
        diag_error(e->at, "an Action takes no arguments: '" STR_FMT "();'", STR_ARG(e->name));
        diag_note("it's code the caller wrote, and sees the caller's variables itself");
    }
    return T_VOID_;
}

// "methods" or "functions", for messages about the code being checked.
static const char *routines(const checker *c)
{
    if (c->method->kind == DECL_SHADER) return c->method->is_frag ? "frags" : "verts";
    return c->method->kind == DECL_FUNCTION ? "functions" : "methods";
}

// IsDead() inside another of the type's methods: the same value.
static type check_self_call(checker *c, expr *e, decl *m)
{
    if (m->is_mut_method && !c->method->is_mut_method) {
        diag_error(e->at, "'" STR_FMT "' changes the fields, so only a mut method can call it", STR_ARG(m->name));
        diag_note("declare '" STR_FMT "' as 'mut' too", STR_ARG(c->method->name));
        const fix f = {.kind = FIX_MUT_METHOD, .at = e->at, .method = c->method};
        vec_push(c->prog->fixes, f);
    }
    check_method_args(c, e, m);
    return m->result;
}

static type check_call(checker *c, expr *e)
{
    param *const block = find_local(c, e->name) ? NULL : find_param(c, e->name);
    if (block && block->type.kind == TY_ACTION) return check_block_call(c, e, block);

    decl *const own = c->method && !c->method->is_operator && !c->method->is_interpolate ? find_method(c->method->owner, e->name) : NULL;
    if (own) return check_self_call(c, e, own);

    type builtin;
    if (builtin_type_named(e->name, &builtin)) return check_construct(c, e, builtin);
    if (is_new_grid(e)) return check_new_grid(c, e, T_VOID_); // Where nothing says its type: an error

    if (str_eq_c(e->name, "Send")) return check_send(c, e);

    if (str_eq_c(e->name, "Spawn")) {
        if (in_routine(c)) {
            diag_error(e->at, "%s can't spawn entities; systems do", routines(c));
            return T_ERR;
        }
        if (c->in_input) {
            diag_error(e->at, "%s runs outside the simulation, so it can't spawn entities", input_code(c));
            return T_ERR;
        }
        // Expressions evaluate left to right, so codegen runs a statement's spawns
        // first, in order. On the right of && or ||, or in a side of ?:, that would
        // spawn even when that part is skipped.
        if (c->loop_header > 0) {
            diag_error(e->at, "Spawn can't be in a loop's condition or a for's step");
            diag_note("spawn in the loop's body instead");
        } else if (c->branch_depth > 0) {
            diag_error(e->at, "Spawn can't be inside '?:'");
            diag_note("only one side runs; spawn in an if/else instead");
        } else if (c->short_circuit_depth > 0) {
            diag_error(e->at, "Spawn can't be %s", c->short_circuit_side);
            diag_note("that side only runs sometimes; spawn into a local before the condition");
        }
        e->call = CALL_SPAWN;
        e->local_world = local_code(c);
        e->spawn_mask = check_component_list(c, e, "Spawn");
        if (in_async_function(c)) e->local_world = c->method->task_side == TASK_LOCAL;
        for (int i = 0; i < c->prog->components.count; i++) {
            if (mask_has(e->spawn_mask, i)) note_text_write(c, decl_holds_text(c->prog->components.items[i]));
        }
        c->prog->spawned_mask = mask_or(c->prog->spawned_mask, e->spawn_mask);
        vec_push(c->spawns, e);
        note_spawn(c);
        return e->local_world ? (type){TY_LOCAL_ENTITY, NULL} : T_ENTITY_;
    }

    decl *const fn = find_named(c, e->name, e->at, NAME_FUNCTION);
    if (fn) return check_function_call(c, e, fn);

    decl *const d = find_type(c, e->name, e->at);
    if (d && d == c->prog->random) return check_random_seeded(c, e);
    if (d && d->kind == DECL_ENUM) return check_enum_conversion(c, e, d); // Card(2)

    for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);

    if (d && (d->kind == DECL_COMPONENT || d->kind == DECL_STRUCT || d->kind == DECL_EVENT)) {
        diag_error(e->at, "write '" STR_FMT " { ... }' to make %s value", STR_ARG(e->name),
                   d->kind == DECL_STRUCT ? "a struct" : d->kind == DECL_EVENT ? "an event" : "a component");
        return T_ERR;
    }
    const decl *shader = find_named(c, e->name, e->at, NAME_SHADER);
    if (shader) {
        diag_error(e->at, "'" STR_FMT "' is a %s: the GPU runs it, and nothing calls it", STR_ARG(e->name), shader_word(shader));
        diag_note("a view's Draw2D.Mesh or Draw3D.Mesh names it, last: the engine gives it what its parameters say");
        return T_ERR;
    }
    diag_error(e->at, "unknown function '" STR_FMT "'", STR_ARG(e->name));
    const fix f = {.kind = FIX_CREATE_FUNCTION, .at = e->at, .call = e};
    vec_push(c->prog->fixes, f);
    const char *owner = builtin_function_owner(e->name);
    if (owner) {
        diag_note("it's '%s." STR_FMT "'", owner, STR_ARG(e->name)); // Sin(x) for Math.Sin(x)
    } else {
        suggestion s = suggest_start(e->name);
        suggest_consider_c(&s, "Spawn");
        suggest_consider_c(&s, "Send");
        suggest_builtin_types(&s);
        for (int i = 0; i < c->prog->decls.count; i++) { // The game's functions, and its enums, which convert an int
            const decl *other = c->prog->decls.items[i];
            if (other->kind == DECL_FUNCTION || other->kind == DECL_ENUM) suggest_consider(&s, other->name);
        }
        suggest_note(&s);
    }
    return T_ERR;
}

// Random(seed): a generator for a seed, an int or a long.
static type check_random_seeded(checker *c, expr *e)
{
    for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
    if (gpu_refuses(c, e->at, "make a Random", "the view rolls what it needs and keeps it in a field the GPU reads")) return T_ERR;
    const type result = decl_type(c->prog->random);
    if (e->args.count != 1) {
        diag_error(e->at, "Random takes a seed, like 'Random(42)', not %d arguments", e->args.count);
        diag_note("'Random { }' is one nothing seeded yet, which the engine seeds as its entity spawns");
        return result;
    }
    const type seed = e->args.items[0]->type;
    if (seed.kind != TY_ERROR && !type_assignable(T_LONG_, seed)) {
        diag_error(e->args.items[0]->at, "a Random's seed is an int or a long, not %s", type_name(seed));
        if (seed.kind == TY_FLOAT) diag_note("'int(x)' makes a float an int");
        else diag_note("'Math.Hash(value)' makes an int of an int2, int3 or int4");
        return result;
    }
    e->call = CALL_BUILTIN;
    e->c_callee = "tide_random_seeded";
    e->arg_want.count = 0;
    vec_push(e->arg_want, T_LONG_);
    return result;
}

// Does `e` name a built-in owner like Math or quaternion, rather than a variable?
static bool names_builtin_owner(const checker *c, const expr *e)
{
    return e->kind == E_NAME && builtin_owner(e->name) && !find_local(c, e->name) && !find_param(c, e->name);
}

// Scene.Load(Arena { ... }), Scene.Load(Hand { ... }, SceneVisibility.Private),
// Scene.Unload(scene), Scene.AddPlayer(scene, player) and
// Scene.RemovePlayer(scene, player).
// The scene Session.Start starts a match in: one of the
// match's, named with its defaults or written with values.
static decl *check_start_scene(checker *c, expr *arg, const char *call)
{
    decl *scene = NULL;
    if (arg->kind == E_LITERAL) {
        const type t = check_literal(c, arg);
        if (t.kind == TY_COMPONENT && t.decl->is_scene) scene = t.decl;
        else if (t.kind != TY_ERROR) diag_error(arg->at, "%s takes a scene, and '" STR_FMT "' is %s", call, STR_ARG(t.decl->name), decl_what(t.decl));
    } else {
        str name;
        decl *d = (arg->kind == E_NAME || arg->kind == E_MEMBER) && qualified_text(arg, &name) ? find_type(c, name, arg->at) : NULL;
        if (d) {
            if (arg->kind == E_MEMBER) mark_namespaces(arg->object);
            arg->bind = BIND_TYPE;
            arg->type_decl = d;
            arg->type = decl_type(d);
            if (d->kind == DECL_COMPONENT && d->is_scene) scene = d;
            else diag_error(arg->at, "%s takes a scene, and '" STR_FMT "' is %s", call, STR_ARG(d->name), decl_what(d));
        } else if (check_expr(c, arg).kind != TY_ERROR) {
            diag_error(arg->at, "%s takes a scene, like '%s(Arena)' or '%s(Arena { size = 30 })'", call, call, call);
        }
    }
    if (!scene) return NULL;
    if (scene->is_local) {
        diag_error(arg->at, "'" STR_FMT "' is local, and a match starts in one of the match's scenes", STR_ARG(scene->name));
        diag_note("declare the scene without 'local', like 'scene " STR_FMT " { ... }'", STR_ARG(scene->name));
        return NULL;
    }
    if (decl_holds_text(scene)) {
        diag_error(arg->at, "a match can't start in '" STR_FMT "' yet: it holds text or lists", STR_ARG(scene->name));
        diag_note("fill them in the scene's Spawned handler instead");
        return NULL;
    }
    return scene;
}

// Whether a literal is a room's code: 6 letters and digits, which leave out
// look-alikes. Case and spaces don't matter.
static bool room_code(const str text)
{
    int n = 0;
    for (int i = 0; i < text.len; i++) {
        const char ch = text.ptr[i];
        if (ch == ' ') continue;
        const char upper = ch >= 'a' && ch <= 'z' ? (char)(ch - 'a' + 'A') : ch;
        if (!upper || !strchr("23456789ABCDEFGHJKLMNPQRSTUVWXYZ", upper)) return false;
        n++;
    }
    return n == 6;
}

// Session.Start(Arena), Session.Join(code), Session.Connect(address, port)
// and Session.Leave(): which match this machine is in; Session.Open(port)
// and Session.Close(): whether others can join the one it runs;
// Session.Kick(player, message) and Session.KickAll(message): sending players
// out of it; and Session.End(): ending it for everyone. Only local code
// decides.
static type check_session_call(checker *c, expr *e)
{
    const bool start = str_eq_c(e->name, "Start");
    const bool join = str_eq_c(e->name, "Join");
    const bool connect = str_eq_c(e->name, "Connect");
    const bool leave = str_eq_c(e->name, "Leave");
    const bool open = str_eq_c(e->name, "Open");
    const bool close = str_eq_c(e->name, "Close");
    const bool kick = str_eq_c(e->name, "Kick");
    const bool kick_all = str_eq_c(e->name, "KickAll");
    const bool end = str_eq_c(e->name, "End");
    if (!start && !join && !connect && !leave && !open && !close && !kick && !kick_all && !end) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        if (str_eq_c(e->name, "Play") || str_eq_c(e->name, "Host")) {
            diag_error(e->at, "Session." STR_FMT " is Session.Start now", STR_ARG(e->name));
            diag_note(str_eq_c(e->name, "Play") ? "'Session.Start(Arena)' starts a match on this machine"
                                                : "'Session.Start(Arena); Session.Open();' starts a match others can join");
            return T_ERR;
        }
        diag_error(e->at, "Session has no '" STR_FMT "'; it has Start, Open, Close, Kick, KickAll, End, Join, Connect and Leave",
                   STR_ARG(e->name));
        suggestion s = suggest_start(e->name);
        suggest_consider_c(&s, "Start");
        suggest_consider_c(&s, "Open");
        suggest_consider_c(&s, "Close");
        suggest_consider_c(&s, "Kick");
        suggest_consider_c(&s, "KickAll");
        suggest_consider_c(&s, "Join");
        suggest_consider_c(&s, "Connect");
        suggest_consider_c(&s, "Leave");
        suggest_consider_c(&s, "End");
        suggest_note(&s);
        return T_ERR;
    }
    if (in_async_function(c) && !note_task_side(c, TASK_LOCAL, e->at)) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        return T_ERR;
    }
    if (in_routine(c) || c->in_input || (!in_async_function(c) && !local_code(c))) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        if (in_routine(c)) {
            diag_error(e->at, "%s can't call Session." STR_FMT " yet; views and local handlers do", routines(c),
                       STR_ARG(e->name));
            diag_note("call it in the view, and pass what the function decides back, like a 'mut bool' or its result");
        } else if (c->in_input) {
            diag_error(e->at, "%s makes this machine's input, so it can't call Session." STR_FMT, input_code(c),
                       STR_ARG(e->name));
        } else {
            diag_error(e->at, "the match runs the same on every machine, so it can't call Session." STR_FMT,
                       STR_ARG(e->name));
            diag_note("call it from a view or a local handler, like a menu's button");
        }
        return T_ERR;
    }
    if (c->loop_header > 0 || c->branch_depth > 0 || c->short_circuit_depth > 0) {
        diag_error(e->at, "Session." STR_FMT " is a statement of its own", STR_ARG(e->name));
    }
    e->call = CALL_SESSION;
    const int least = start || join || connect || kick ? 1 : 0;
    const int most = connect || kick ? 2 : open || kick_all ? 1 : least;
    if (e->args.count < least || e->args.count > most) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "%s", start     ? "Session.Start takes the scene the match starts in: 'Session.Start(Arena)'"
                               : open    ? "Session.Open takes nothing, or the port to take players on: "
                                           "'Session.Open()' or 'Session.Open(7777)'"
                               : join    ? "Session.Join takes the room's code: 'Session.Join(\"K7QF2M\")'"
                               : connect ? "Session.Connect takes the server's address, and maybe a port: "
                                           "'Session.Connect(\"192.168.1.5\")' or 'Session.Connect(\"192.168.1.5\", 7777)'"
                               : close   ? "Session.Close takes nothing: 'Session.Close()'"
                               : kick    ? "Session.Kick takes the player, and maybe a message: "
                                           "'Session.Kick(player)' or 'Session.Kick(player, \"Be nice\")'"
                               : kick_all ? "Session.KickAll takes nothing, or a message: "
                                            "'Session.KickAll()' or 'Session.KickAll(\"The party's over\")'"
                               : end     ? "Session.End takes nothing: 'Session.End()'"
                                         : "Session.Leave takes nothing: 'Session.Leave()'");
        if (start && e->args.count == 2) diag_note("a match takes players once it's opened: 'Session.Open(7777)'");
        return T_ERR;
    }
    if (join || connect) {
        const expr *arg = e->args.items[0];
        const type t = check_expr(c, e->args.items[0]);
        if (t.kind != TY_ERROR && t.kind != TY_STRING) {
            diag_error(arg->at, "%s", join ? "Session.Join takes the room's code as text, like \"K7QF2M\""
                                           : "Session.Connect takes the server's address as text, like \"192.168.1.5\"");
        } else if (join && arg->kind == E_STRING && !room_code(arg->text)) {
            diag_error(arg->at, "\"" STR_FMT "\" isn't a room's code: they're 6 letters and digits, like \"K7QF2M\"",
                       STR_ARG(arg->text));
            if (memchr(arg->text.ptr, '.', (size_t)arg->text.len) || memchr(arg->text.ptr, ':', (size_t)arg->text.len)
                || str_eq_c(arg->text, "localhost")) {
                diag_note("to join a machine by its address, call Session.Connect(\"" STR_FMT "\")", STR_ARG(arg->text));
            }
        }
        if (connect && e->args.count == 2) {
            const type port = check_expr(c, e->args.items[1]);
            if (!type_assignable(T_INT_, port)) diag_error(e->args.items[1]->at, "a port is an int, like 7777");
        }
        return T_VOID_;
    }
    if (open && e->args.count == 1) {
        const type t = check_expr(c, e->args.items[0]);
        if (!type_assignable(T_INT_, t)) diag_error(e->args.items[0]->at, "a port is an int, like 7777");
    }
    if (kick) {
        const type t = check_expr(c, e->args.items[0]);
        if (t.kind != TY_ERROR && t.kind != TY_PLAYER) {
            diag_error(e->args.items[0]->at, "Session.Kick takes the player to send away, a PlayerID, not %s", type_name(t));
        }
    }
    if ((kick && e->args.count == 2) || (kick_all && e->args.count == 1)) {
        const expr *message = e->args.items[e->args.count - 1];
        const type t = check_expr(c, e->args.items[e->args.count - 1]);
        if (t.kind != TY_ERROR && t.kind != TY_STRING) {
            diag_error(message->at, "a kick's message is text, like \"Be nice\"");
            diag_note("put a value in text with '$', like '$\"{value}\"'");
        }
    }
    if (!start) return T_VOID_;
    decl *scene = check_start_scene(c, e->args.items[0], "Session.Start");
    if (!scene) return T_ERR;
    // The match's world starts with it: it needs an archetype there.
    e->type_decl = scene;
    e->spawn_mask = bit(scene);
    e->local_world = false;
    vec_push(c->spawns, e);
    bool known = false;
    for (int i = 0; i < c->prog->start_scenes.count; i++) known |= c->prog->start_scenes.items[i] == scene;
    if (!known) vec_push(c->prog->start_scenes, scene);
    return T_VOID_;
}

static type check_scene_call(checker *c, expr *e)
{
    const bool load = str_eq_c(e->name, "Load");
    const bool unload = str_eq_c(e->name, "Unload");
    const bool players = str_eq_c(e->name, "AddPlayer") || str_eq_c(e->name, "RemovePlayer");
    if (!load && !unload && !players) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "Scene has no '" STR_FMT "'; it has Load, Unload, AddPlayer and RemovePlayer", STR_ARG(e->name));
        suggestion s = suggest_start(e->name);
        suggest_consider_c(&s, "Load");
        suggest_consider_c(&s, "Unload");
        suggest_consider_c(&s, "AddPlayer");
        suggest_consider_c(&s, "RemovePlayer");
        suggest_note(&s);
        return T_ERR;
    }
    const char *error = NULL;
    if (in_routine(c)) error = "%s can't load or unload scenes; systems, views and event handlers do";
    else if (c->in_input) error = "%s runs outside the simulation, so it can't load or unload scenes";
    if (error) {
        diag_error(e->at, error, c->method ? routines(c) : input_code(c));
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        return T_ERR;
    }
    bool local = local_code(c);
    e->local_world = local;

    if (load) {
        if (e->args.count < 1 || e->args.count > 2) {
            for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
            diag_error(e->at, "Scene.Load takes a scene, like 'Scene.Load(Arena { size = 30 })', and maybe its visibility");
            return T_ERR;
        }
        // Like Spawn, it creates an entity, so it runs first in its statement (see Evaluation order).
        if (c->loop_header > 0) {
            diag_error(e->at, "Scene.Load can't be in a loop's condition or a for's step");
            diag_note("load in the loop's body instead");
        } else if (c->branch_depth > 0 || c->short_circuit_depth > 0) {
            diag_error(e->at, "Scene.Load can't be %s", c->branch_depth > 0 ? "inside '?:'" : c->short_circuit_side);
            diag_note("that part only runs sometimes; load in an if/else instead");
        }
        expr *arg = e->args.items[0];
        decl *scene = NULL;
        if (arg->kind == E_LITERAL) {
            const type t = check_literal(c, arg);
            if (t.kind == TY_COMPONENT && t.decl->is_scene) scene = t.decl;
            else if (t.kind != TY_ERROR) diag_error(arg->at, "Scene.Load takes a scene, and '" STR_FMT "' is %s", STR_ARG(t.decl->name), decl_what(t.decl));
        } else {
            str name;
            decl *d = (arg->kind == E_NAME || arg->kind == E_MEMBER) && qualified_text(arg, &name) ? find_type(c, name, arg->at) : NULL;
            if (d) {
                if (arg->kind == E_MEMBER) mark_namespaces(arg->object);
                arg->bind = BIND_TYPE;
                arg->type_decl = d;
                arg->type = decl_type(d);
                if (d->kind == DECL_COMPONENT && d->is_scene) scene = d;
                else diag_error(arg->at, "Scene.Load takes a scene, and '" STR_FMT "' is %s", STR_ARG(d->name), decl_what(d));
            } else if (check_expr(c, arg).kind != TY_ERROR) {
                diag_error(arg->at, "Scene.Load takes a scene, like 'Scene.Load(Arena)' or 'Scene.Load(Arena { size = 30 })'");
            }
        }
        if (e->args.count == 2) {
            const type t = check_expr(c, e->args.items[1]);
            if (t.kind != TY_ERROR && !(t.kind == TY_ENUM && t.decl == c->prog->scene_visibility)) {
                diag_error(e->args.items[1]->at, "a scene's visibility is 'SceneVisibility.Public' or 'SceneVisibility.Private', not %s",
                           type_name(t));
            } else if (scene && scene->is_local) {
                diag_error(e->args.items[1]->at, "'" STR_FMT "' is local, and only the match's scenes have a visibility",
                           STR_ARG(scene->name));
                diag_note("local scenes are only on this machine, so no other player sees them anyway");
            }
        }
        if (!scene) return T_ERR;
        if (in_async_function(c)) { // An async function's world is the scene's
            if (!note_task_side(c, scene->is_local ? TASK_LOCAL : TASK_MATCH, arg->at)) return T_ERR;
            local = e->local_world = scene->is_local;
        }
        if (scene->is_local != local) {
            if (scene->is_local) {
                diag_error(arg->at, "'" STR_FMT "' is local, and the match can't use local state", STR_ARG(scene->name));
            } else {
                diag_error(arg->at, "%s can't change the match, and '" STR_FMT "' is one of its scenes",
                           local_code_what(c->system), STR_ARG(scene->name));
                diag_note("start a match with it through the session instead");
            }
            return T_ERR;
        }
        e->call = CALL_LOAD;
        e->type_decl = scene;
        note_text_write(c, decl_holds_text(scene));
        e->spawn_mask = bit(scene);
        vec_push(c->spawns, e);
        note_spawn(c);
        return local ? (type){TY_LOCAL_ENTITY, NULL} : T_ENTITY_;
    }

    const int want = unload ? 1 : 2;
    for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
    if (e->args.count != want) {
        diag_error(e->at, unload ? "Scene.Unload takes the scene's entity: 'Scene.Unload(scene)'"
                                 : "Scene.%s takes the scene's entity and a player: 'Scene." STR_FMT "(scene, player)'",
                   str_to_cstr(e->name), STR_ARG(e->name));
        return T_ERR;
    }
    const type scene = e->args.items[0]->type;
    if (in_async_function(c) && (scene.kind == TY_ENTITY || scene.kind == TY_LOCAL_ENTITY)) {
        if (!note_task_side(c, scene.kind == TY_LOCAL_ENTITY ? TASK_LOCAL : TASK_MATCH, e->args.items[0]->at)) return T_ERR;
        local = e->local_world = scene.kind == TY_LOCAL_ENTITY;
    }
    const type_kind own = local ? TY_LOCAL_ENTITY : TY_ENTITY;
    if (scene.kind != TY_ERROR && scene.kind != own) {
        if (scene.kind == TY_ENTITY && local) {
            diag_error(e->args.items[0]->at, "%s can't change the match, and this entity belongs to it", local_code_what(c->system));
        } else if (scene.kind == TY_LOCAL_ENTITY && !local) {
            diag_error(e->args.items[0]->at, "a LocalEntity is local, and the match can't use local state");
        } else {
            const expr *arg = e->args.items[0];
            diag_error(arg->at, "Scene." STR_FMT " takes the scene's entity, not %s", STR_ARG(e->name), type_name(scene));
            // The scene's data, which the code runs for: its entity is `this`.
            if (scene.kind == TY_COMPONENT && scene.decl->is_scene && arg->kind == E_NAME && arg->bind == BIND_PARAM
                && !arg->param->function_param) {
                diag_note("'" STR_FMT "' is the scene's data, and its entity is 'this': 'Scene." STR_FMT "(this%s)'",
                          STR_ARG(arg->name), STR_ARG(e->name), unload ? "" : ", player");
            }
        }
        return T_ERR;
    }
    if (players) {
        if (local) {
            diag_error(e->at, "which players see a scene is the match's to decide, so local code can't change it");
            return T_ERR;
        }
        const type player = e->args.items[1]->type;
        if (player.kind != TY_ERROR && player.kind != TY_PLAYER) {
            diag_error(e->args.items[1]->at, "Scene." STR_FMT " takes a PlayerID, not %s", STR_ARG(e->name), type_name(player));
            return T_ERR;
        }
        e->call = CALL_SCENE_PLAYER;
        return T_VOID_;
    }
    e->call = CALL_UNLOAD;
    return T_VOID_;
}

// GUI.Button(rect, text), GUILayout.Toggle(text, settings.fullscreen), GUILayout.Area(anchor) { ... }
static type check_gui_call(checker *c, expr *e, const type result)
{
    e->call = CALL_GUI;
    sb name = {0};
    sb_printf(&name, STR_FMT "." STR_FMT, STR_ARG(e->object->name), STR_ARG(e->name));
    for (int i = 0; i < e->args.count; i++) {
        if (!(e->arg_mut & (1u << i))) continue;
        // Its messages name the widget and what it changes.
        decl *widget = NEW(decl);
        widget->name = (str){name.data, (int)name.len};
        param *value = NEW(param);
        value->name = str_from("value");
        check_writable(c, e->args.items[i], widget, value);
    }
    if (e->gui & GUI_CONTAINER) {
        if (!e->block) {
            diag_error(e->at, "%s takes a block: '%s(...) { ... }'", name.data, name.data);
            diag_note("its widgets go in the braces after it");
        }
    } else if (result.kind != TY_VOID && c->loop_header > 0) {
        diag_error(e->at, "%s can't be in a loop's condition or a for's step", name.data);
        diag_note("call it in the loop's body");
    } else if (result.kind != TY_VOID && (c->branch_depth > 0 || c->short_circuit_depth > 0)) {
        // Widgets draw as they're called, in order, like Spawn.
        diag_error(e->at, "%s can't be %s", name.data, c->branch_depth > 0 ? "inside '?:'" : c->short_circuit_side);
        diag_note("that part only runs sometimes, so the widget would come and go; call it in an 'if' of its own");
    }
    return result;
}

static type check_session_call(checker *c, expr *e);

// Wait.Ticks(n), Wait.Frames(n) and Wait.Seconds(s), after `await`: the task
// goes on n ticks or frames, or s seconds, later. Ticks are the match's, and
// frames this machine's.
static type check_wait(checker *c, expr *e)
{
    const bool ticks = str_eq_c(e->name, "Ticks");
    const bool frames = str_eq_c(e->name, "Frames");
    const bool seconds = str_eq_c(e->name, "Seconds");
    for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
    if (!ticks && !frames && !seconds) {
        diag_error(e->at, "Wait has no '" STR_FMT "'; it has Ticks, Frames and Seconds", STR_ARG(e->name));
        suggestion s = suggest_start(e->name);
        suggest_consider_c(&s, "Ticks");
        suggest_consider_c(&s, "Frames");
        suggest_consider_c(&s, "Seconds");
        suggest_note(&s);
        return T_ERR;
    }
    const type want = seconds ? T_FLOAT_ : T_INT_;
    if (e->args.count != 1) {
        diag_error(e->at, "Wait." STR_FMT " takes how many %s to wait: 'await Wait." STR_FMT "(%s);'", STR_ARG(e->name),
                   ticks ? "ticks" : frames ? "frames" : "seconds", STR_ARG(e->name), seconds ? "0.5" : "30");
        return T_ERR;
    }
    const expr *arg = e->args.items[0];
    if (arg->type.kind != TY_ERROR && !type_assignable(want, arg->type)) {
        diag_error(arg->at, "Wait." STR_FMT " takes %s, not %s", STR_ARG(e->name), seconds ? "seconds, a float" : "an int",
                   type_name(arg->type));
        return T_ERR;
    }
    // The match counts ticks, and this machine frames
    if (ticks || frames) {
        if (in_async_function(c)) {
            if (!note_task_side(c, frames ? TASK_LOCAL : TASK_MATCH, e->at)) return T_ERR;
        } else if (local_code(c) != frames) {
            diag_error(e->at, frames ? "frames are this machine's, and the match counts ticks: 'await Wait.Ticks(n);'"
                                     : "ticks are the match's, and local code counts frames: 'await Wait.Frames(n);'");
            return T_ERR;
        }
    }
    e->call = CALL_WAIT;
    vec_push(e->arg_want, want);
    return T_VOID_;
}

// Whether `e` names the built-in Wait, not a variable or a namespace.
static bool names_wait(const checker *c, const expr *e)
{
    return e->kind == E_NAME && str_eq_c(e->name, "Wait") && !find_local(c, e->name) && !find_param(c, e->name)
        && !is_namespace(c->prog, e->name);
}

// Clipboard.Copy(text): text onto this machine's clipboard, which the host
// does after the frame, as it does what Session's calls ask. Local code's,
// like those.
static type check_clipboard_call(checker *c, expr *e)
{
    if (!str_eq_c(e->name, "Copy")) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "Clipboard has no '" STR_FMT "'; it has Copy", STR_ARG(e->name));
        if (str_eq_c(e->name, "Paste") || str_eq_c(e->name, "text")) {
            diag_note("what's pasted comes as typing: a text field takes it");
        }
        return T_ERR;
    }
    if (in_async_function(c) && !note_task_side(c, TASK_LOCAL, e->at)) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        return T_ERR;
    }
    if (in_routine(c) || c->in_input || (!in_async_function(c) && !local_code(c))) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        if (in_routine(c)) {
            diag_error(e->at, "%s can't call Clipboard.Copy yet; views and local handlers do", routines(c));
            diag_note("call it in the view, and pass what the function decides back, like a 'mut bool' or its result");
        } else if (c->in_input) {
            diag_error(e->at, "%s makes this machine's input, so it can't call Clipboard.Copy", input_code(c));
        } else {
            diag_error(e->at, "the match runs the same on every machine, so it can't call Clipboard.Copy");
            diag_note("call it from a view or a local handler, like a button's");
        }
        return T_ERR;
    }
    if (c->loop_header > 0 || c->branch_depth > 0 || c->short_circuit_depth > 0) {
        diag_error(e->at, "Clipboard.Copy is a statement of its own");
    }
    e->call = CALL_CLIPBOARD;
    if (e->args.count != 1) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "Clipboard.Copy takes the text to copy: 'Clipboard.Copy(session.room)'");
        return T_ERR;
    }
    const type t = check_expr(c, e->args.items[0]);
    if (t.kind != TY_ERROR && t.kind != TY_STRING) {
        diag_error(e->args.items[0]->at, "Clipboard.Copy takes text, not %s", type_name(t));
        diag_note("put a value in text with '$', like '$\"{value}\"'");
    }
    return T_VOID_;
}

// Draw2D.Mesh and Draw3D.Mesh take the program's own vertex lists and
// List<int>, its Grid2<Color> if it has one, and its Filter.
static void use_mesh_types(checker *c)
{
    const decl *pixels = NULL;
    for (int i = 0; i < c->prog->grids.count; i++) {
        const decl *g = c->prog->grids.items[i];
        if (g->dims == 2 && g->fields.items[0].type.kind == TY_COLOR) pixels = g;
    }
    builtins_use_mesh(list_of(c->prog, decl_type(c->prog->vertex)).decl,
                      list_of(c->prog, decl_type(c->prog->vertex3)).decl, list_of(c->prog, (type){TY_INT, NULL}).decl,
                      pixels, c->prog->filter, list_of(c->prog, (type){TY_FLOAT4X4, NULL}).decl);
}

// The frag or vert an argument of Draw2D.Mesh names, or NULL: `Shade`, or
// `Effects.Shade`.
static decl *named_shader(const checker *c, expr *arg)
{
    str text;
    const expr *root = chain_root(arg);
    if (root->kind != E_NAME || !qualified_text(arg, &text)) return NULL;
    if (find_local(c, root->name) || find_param(c, root->name) || field_in_scope(c, root->name)) return NULL;
    decl *other;
    decl *d = lookup(c->prog, c->unit, text, NAME_SHADER, &other);
    if (!d) return NULL;
    if (arg->kind == E_MEMBER) mark_namespaces(arg->object);
    arg->bind = BIND_SHADER;
    arg->type_decl = d;
    arg->type = T_VOID_;
    return d;
}

// What a mesh's vertex struct has to be: a `position`, a float2 for a 2D mesh
// or a float3 for a 3D one, and fields the GPU reads from a list as they are:
// floats, ints, their vectors and colors. Its position's size, or 0 after an
// error at `at`.
static int vertex_dims(const decl *s, const loc at, const bool report)
{
    int dims = 0;
    for (int i = 0; i < s->fields.count; i++) {
        const field *f = &s->fields.items[i];
        const bool number = (type_is_numeric(f->type) && f->type.kind != TY_BYTE) || f->type.kind == TY_COLOR;
        if (!number && f->type.kind != TY_ERROR) {
            if (report) {
                diag_error(at, "'" STR_FMT "' can't be a mesh's vertex: its '" STR_FMT "' is %s", STR_ARG(s->name), STR_ARG(f->name),
                           type_name(f->type));
                diag_note("a vertex's fields are floats, ints, their vectors and colors, which the GPU reads from the list as "
                          "they are");
            }
            return 0;
        }
        if (!str_eq_c(f->name, "position")) continue;
        if (f->type.kind == TY_FLOAT2) dims = 2;
        else if (f->type.kind == TY_FLOAT3) dims = 3;
        else if (f->type.kind != TY_ERROR) {
            if (report) {
                diag_error(at, "'" STR_FMT "' can't be a mesh's vertex: its 'position' is %s", STR_ARG(s->name), type_name(f->type));
                diag_note("a vertex's 'position' is a float2 for a 2D mesh, or a float3 for a 3D one");
            }
            return 0;
        }
    }
    if (dims == 0 && report) {
        diag_error(at, "'" STR_FMT "' can't be a mesh's vertex: it has no 'position'", STR_ARG(s->name));
        diag_note("a vertex says where it is: 'float2 position;' for a 2D mesh, or 'float3 position;' for a 3D one");
    }
    if (dims != 0 && s->fields.count > 12) {
        if (report) {
            diag_error(at, "'" STR_FMT "' has %d fields, and a mesh's vertex has 12 at most", STR_ARG(s->name), s->fields.count);
            diag_note("a GPU reads 16 values for each vertex, and a draw's transform and its entity's fields take the rest");
        }
        return 0;
    }
    return dims;
}

static int shader_dims(const decl *vertex)
{
    return vertex_dims(vertex, (loc){0, 0, 0}, false);
}

static const field *field_called(const decl *d, const char *name)
{
    for (int i = 0; i < d->fields.count; i++) {
        if (str_eq_c(d->fields.items[i].name, name)) return &d->fields.items[i];
    }
    return NULL;
}

static void note_mesh_forms(const expr *e)
{
    if (e->targeted) {
        diag_note(e->instanced ? "target.Instanced(vertices, indices, instances) draws a 2D or 3D mesh; then come a texture and its filter, or a vert and a frag by name"
                               : "target.Mesh(vertices, indices[, transform]) draws a 2D or 3D mesh; then come a texture and its filter, or a vert and a frag by name");
        return;
    }
    const str owner = e->object->name;
    if (e->instanced) {
        diag_note(STR_FMT ".Instanced(vertices, indices, instances) draws a %dD mesh; then come a texture and its filter, "
                  "or a vert and a frag by name", STR_ARG(owner), str_eq_c(owner, "Draw3D") ? 3 : 2);
        return;
    }
    diag_note(STR_FMT ".Mesh(vertices, indices%s) draws a %dD mesh; then come a texture and its filter, or a vert and a frag by name",
              STR_ARG(owner), str_eq_c(owner, "Draw3D") ? ", transform" : "", str_eq_c(owner, "Draw3D") ? 3 : 2);
}

// A view's Draw2D.Mesh or Draw3D.Mesh through a frag or a vert: the components they read are
// those of the entity the view runs for, so it has to have them.
static void check_shader_binding(const checker *c, const expr *e, const expr *name, const decl *shader)
{
    decl *view = c->system;
    for (int i = 1; i < shader->params.count; i++) {
        const param *p = &shader->params.items[i];
        if (p->type.kind == TY_STRUCT && p->mode != PARAM_MUT) {
            if (!e->instanced) {
                diag_error(name->at, "%s '" STR_FMT "' reads instance data of '" STR_FMT "': draw it with " STR_FMT ".Instanced and a List<" STR_FMT ">",
                           shader_word(shader), STR_ARG(shader->name), STR_ARG(p->type.decl->name), STR_ARG(e->object->name), STR_ARG(p->type.decl->name));
            } else if (p->type.decl != e->instance_type) {
                diag_error(name->at, "%s '" STR_FMT "' reads instance data of '" STR_FMT "', which this draw's instances don't supply",
                           shader_word(shader), STR_ARG(shader->name), STR_ARG(p->type.decl->name));
                diag_note("its read-only struct parameter has to be the element type of the draw's instance list");
            }
            continue;
        }
        if (p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON) continue;
        const decl *d = p->type.decl;
        for (int k = 0; k < view->params.count; k++) { // What a frag reads of it is a use of it
            param *own = &view->params.items[k];
            if (own->type.decl == d && (own->type.kind == TY_COMPONENT || own->type.kind == TY_SINGLETON)
                && (own->mode == PARAM_READ || own->mode == PARAM_MUT)) {
                own->read = true;
            }
        }
        if (p->type.kind != TY_COMPONENT || mask_has(view->need_mask, d->index)) continue;
        diag_error(name->at, "view '" STR_FMT "' doesn't guarantee its entity has a '" STR_FMT "', which %s '" STR_FMT "' reads",
                   STR_ARG(view->name), STR_ARG(d->name), shader_word(shader), STR_ARG(shader->name));
        if (!view->per_entity) {
            diag_note("the view runs once per frame, for no entity: take the component, or require it, so it runs for each "
                      "entity that has one: 'view " STR_FMT "(with " STR_FMT ")'", STR_ARG(view->name), STR_ARG(d->name));
        } else {
            diag_note("a %s reads the components of the entity the view runs for: add 'with " STR_FMT "' to the view's "
                      "parameters", shader_word(shader), STR_ARG(d->name));
        }
    }
    (void)e;
}

static bool shader_takes_texture(const decl *code)
{
    for (int i = 1; code && i < code->params.count; i++) {
        if (code->params.items[i].type.kind == TY_TEXTURE) return true;
    }
    return false;
}

// A blit has a fixed full-destination Vertex quad. Its frag receives the
// explicit source as a Texture, alongside the usual entity and singleton data.
static type check_blit(checker *c, expr *e)
{
    e->blit = true;
    decl *frag = e->args.count == 3 ? named_shader(c, e->args.items[2]) : NULL;
    for (int i = 0; i < e->args.count; i++) {
        if (i != 2 || !frag) check_expr(c, e->args.items[i]);
    }
    if (!check_frame_use(c, e->at, "Draw")) return T_ERR;
    if (e->args.count != 3) {
        diag_error(e->at, "Blit takes an explicit source texture, destination target and frag: 'Draw.Blit(source, destination, Blur)'");
        return T_ERR;
    }
    const type source = e->args.items[0]->type;
    const bool grid = source.kind == TY_GRID && source.decl->dims == 2 && source.decl->fields.items[0].type.kind == TY_COLOR;
    bool ok = true;
    if (source.kind != TY_TEXTURE && !grid && source.kind != TY_ERROR) {
        diag_error(e->args.items[0]->at, "Blit's source is a Texture or Grid2<Color>, not %s", type_name(source));
        ok = false;
    }
    if (e->args.items[1]->type.kind != TY_RENDER_TARGET && e->args.items[1]->type.kind != TY_ERROR) {
        diag_error(e->args.items[1]->at, "Blit's destination is a RenderTarget, like Draw.window, not %s", type_name(e->args.items[1]->type));
        ok = false;
    }
    const expr *source_expr = e->args.items[0];
    if (source_expr->kind == E_MEMBER && str_eq_c(source_expr->member, "color")
        && source_expr->object->type.kind == TY_RENDER_TARGET && same_place(source_expr->object, e->args.items[1])) {
        diag_error(source_expr->at, "Blit can't sample the color image of its own destination");
        ok = false;
    }
    if (!frag || !frag->is_frag) {
        diag_error(e->args.items[2]->at, "Blit names a frag last, like 'Draw.Blit(source, destination, Blur)'");
        return T_ERR;
    }
    e->frag = frag;
    if (!frag->params.count || frag->params.items[0].type.kind != TY_STRUCT
        || frag->params.items[0].type.decl != c->prog->vertex) {
        diag_error(e->args.items[2]->at, "Blit's frag runs on Vertex, its full-destination quad");
        ok = false;
    }
    if (!shader_takes_texture(frag)) {
        diag_error(e->args.items[2]->at, "Blit's frag takes its source as a read-only Texture parameter");
        ok = false;
    }
    if (c->method || c->in_input || !in_view(c)) {
        diag_error(e->at, "a Blit that names a frag goes in a view");
        ok = false;
    } else if (ok) {
        const int errors = diag_error_count();
        check_shader_binding(c, e, e->args.items[2], frag);
        ok = diag_error_count() == errors;
    }
    if (!ok || source.kind == TY_ERROR || e->args.items[1]->type.kind == TY_ERROR) return T_ERR;
    e->call = CALL_DRAW;
    e->draw_dims = 2;
    vec_push(e->arg_want, source);
    vec_push(e->arg_want, e->args.items[1]->type);
    const mesh_draw draw = {e, c->system};
    vec_push(c->prog->mesh_draws, draw);
    return T_VOID_;
}

// Draw2D.Mesh(vertices, indices[, transform][, texture[, filter]][, vert][, frag]).
// A List<Vertex> or a List<Vertex3> with no vert and no frag is the built-in
// call it always was. Anything else draws through a shader tidec writes (see
// shader.c): a vertex struct of the game's, a vert, a frag.
static type check_mesh_draw(checker *c, expr *e)
{
    program *prog = c->prog;
    const str owner = e->object->name;
    const int wanted_dims = e->targeted ? 0 : str_eq_c(owner, "Draw3D") ? 3 : 2;
    const bool instanced = e->instanced = str_eq_c(e->name, "Instanced");
    const char *draw_name = e->targeted ? (instanced ? "RenderTarget.Instanced" : "RenderTarget.Mesh")
                           : wanted_dims == 3 ? (instanced ? "Draw3D.Instanced" : "Draw3D.Mesh")
                                          : (instanced ? "Draw2D.Instanced" : "Draw2D.Mesh");
    // The vert and the frag it names come last, the vert first
    int count = e->args.count;
    bool order_said = false;
    while (count > 0) {
        expr *name = e->args.items[count - 1];
        decl *shader = named_shader(c, name);
        if (!shader) break;
        decl **slot = shader->is_frag ? &e->frag : &e->vert;
        if (*slot) {
            diag_error(name->at, "%s takes one %s, and this names two", draw_name, shader_word(shader));
        } else if (!shader->is_frag && e->frag && !order_said) {
            // Peeled from the end: a vert found after a frag was written before it, as it should be
        } else if (shader->is_frag && e->vert && !order_said) {
            diag_error(name->at, "the vert comes before the frag, as the GPU runs them: '%s(..., " STR_FMT ", " STR_FMT ")'",
                       draw_name, STR_ARG(e->vert->name), STR_ARG(shader->name));
            order_said = true;
        }
        *slot = shader;
        count--;
    }
    const bool shaded = e->vert || e->frag;
    if (!check_frame_use(c, e->at, e->targeted ? "Draw2D" : str_to_cstr(owner))) {
        for (int i = 0; i < count; i++) {
            if (e->args.items[i]->kind != E_DEFAULT) check_expr(c, e->args.items[i]);
        }
        return T_ERR;
    }
    if (count < (instanced ? 3 : 2)) {
        for (int i = 0; i < count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, instanced ? "%s takes a mesh's vertices, its indices, and a list of instances first"
                                  : "%s takes a mesh's vertices and its indices first", draw_name);
        note_mesh_forms(e);
        return T_ERR;
    }

    // The vertices: a list of the struct its vert or frag runs on, or the one
    // the list's first value names
    expr *vertices = e->args.items[0];
    decl *vertex = NULL;
    if (vertices->kind == E_LIST) {
        const decl *shader = e->vert ? e->vert : e->frag;
        const expr *first = vertices->args.count ? vertices->args.items[0] : NULL;
        if (shader && shader->params.count && shader->params.items[0].type.kind == TY_STRUCT) {
            vertex = shader->params.items[0].type.decl;
        } else if (first && first->kind == E_LITERAL) {
            decl *named = find_type(c, first->name, first->at);
            if (named && named->kind == DECL_STRUCT) vertex = named;
        }
        if (!vertex) vertex = wanted_dims == 3 ? prog->vertex3 : prog->vertex;
        check_expr_want(c, vertices, list_of(prog, decl_type(vertex)));
    } else {
        const type t = check_expr(c, vertices);
        if (t.kind == TY_LIST && t.decl->fields.items[0].type.kind == TY_STRUCT) {
            vertex = t.decl->fields.items[0].type.decl;
        } else if (t.kind != TY_ERROR) {
            diag_error(vertices->at, "%s draws a list of vertices, and this is %s", draw_name, type_name(t));
            diag_note("a List<Vertex> in 2D, a List<Vertex3> in 3D, or a list of a struct of the game's with a 'position'");
        }
    }
    expr *indices = e->args.items[1];
    const type index_list = list_of(prog, T_INT_);
    const type index_type = indices->kind == E_LIST ? check_expr_want(c, indices, index_list) : check_expr(c, indices);
    for (int i = 2; i < count; i++) {
        expr *arg = e->args.items[i];
        if (instanced && i == 2 && arg->kind == E_LIST) {
            type element = {TY_FLOAT4X4, NULL};
            const decl *stages[] = {e->vert, e->frag};
            bool supplied = false;
            for (int stage = 0; stage < 2 && !supplied; stage++) {
                const decl *code = stages[stage];
                for (int p = 1; code && p < code->params.count; p++) {
                    const param *data = &code->params.items[p];
                    if (data->type.kind == TY_STRUCT && data->mode != PARAM_MUT) {
                        element = data->type;
                        supplied = true;
                        break;
                    }
                }
            }
            if (!supplied && arg->args.count && arg->args.items[0]->kind == E_LITERAL) {
                const expr *first = arg->args.items[0];
                decl *named = find_type(c, first->name, first->at);
                if (named && named->kind == DECL_STRUCT) element = decl_type(named);
            }
            check_expr_want(c, arg, list_of(prog, element));
        } else if (arg->kind != E_DEFAULT) {
            check_expr(c, arg);
        }
    }
    if (!vertex) return T_ERR;

    const int dims = vertex_dims(vertex, vertices->at, true);
    if (dims == 0) return T_ERR;
    e->draw_dims = dims;
    if (wanted_dims && dims != wanted_dims) {
        diag_error(vertices->at, "%s draws %dD vertices, and '" STR_FMT "' has a float%d position",
                   draw_name, wanted_dims, STR_ARG(vertex->name), dims);
        diag_note("use Draw%dD." STR_FMT " for this vertex type", dims, STR_ARG(e->name));
        return T_ERR;
    }

    if ((vertex == prog->vertex || vertex == prog->vertex3) && !shaded && !instanced) {
        use_mesh_types(c);
        const type result = resolve_builtin_call(str_from(dims == 3 ? "Draw3D" : "Draw2D"), e);
        if (result.kind == TY_ERROR) return result;
        check_default_args(c, e);
        e->call = CALL_DRAW;
        return result;
    }

    bool ok = true;
    if (index_type.kind != TY_ERROR && (index_type.kind != TY_LIST || index_type.decl != index_list.decl)) {
        diag_error(indices->at, "a mesh's indices are a List<int>, three for each triangle, and this is %s", type_name(index_type));
        ok = false;
    }
    for (int i = 2; i < count; i++) {
        if (e->args.items[i]->kind == E_DEFAULT) {
            diag_error(e->args.items[i]->at, "'default' doesn't say what it stands for here: write the value itself");
            return T_ERR;
        }
        if (e->args.items[i]->type.kind == TY_ERROR) return T_ERR;
    }
    // A 3D mesh's transform, or an explicit draw's list of instances
    int at = 2;
    const type f4x4 = {TY_FLOAT4X4, NULL};
    bool transforms = false;
    if (instanced) {
        const expr *instances = e->args.items[at++];
        const type list = instances->type;
        const type element = list.kind == TY_LIST ? list.decl->fields.items[0].type : T_ERR;
        if (list.kind != TY_LIST || (element.kind != TY_STRUCT && element.kind != TY_FLOAT4X4)) {
            diag_error(instances->at, "%s takes a List<float4x4> of transforms or a List<T> of instance structs, and this is %s",
                       draw_name, type_name(list));
            return T_ERR;
        }
        transforms = element.kind == TY_FLOAT4X4;
        if (transforms && dims != 3) {
            diag_error(instances->at, "a List<float4x4> places 3D instances: a 2D draw takes instance structs and a vert that places them");
            return T_ERR;
        }
        e->instance_type = transforms ? NULL : element.decl;
        if (e->instance_type && dims == 3) {
            const field *transform = field_called(e->instance_type, "transform");
            if (transform && transform->type.kind != TY_FLOAT4X4) {
                diag_error(instances->at, "a 3D instance struct's 'transform' is a float4x4, and '" STR_FMT "'s is %s",
                           STR_ARG(e->instance_type->name), type_name(transform->type));
                return T_ERR;
            }
        }
    } else if (dims == 3) {
        if (at >= count || e->args.items[at]->type.kind != TY_FLOAT4X4) {
            diag_error(at < count ? e->args.items[at]->at : e->at, "a 3D mesh takes the transform that places it in the world, "
                       "a float4x4, after its indices");
            diag_note("like 'Draw3D.Mesh(vertices, indices, float4x4.Translate(position));': '" STR_FMT "' is a 3D mesh's vertex, "
                      "as its 'position' is a float3", STR_ARG(vertex->name));
            return T_ERR;
        }
        at++;
    } else if (at < count && e->args.items[at]->type.kind == TY_FLOAT4X4) {
        diag_error(e->args.items[at]->at, "a 2D mesh is drawn where its vertices are, through the 2D camera: it takes no transform");
        diag_note("'" STR_FMT "' is a 2D mesh's vertex, as its 'position' is a float2", STR_ARG(vertex->name));
        return T_ERR;
    }
    // A texture and its filter: grids retain their original no-frag form,
    // while an explicit Texture also supplies a shader's Texture parameter.
    const expr *texture = NULL;
    const expr *draw_filter = NULL;
    if (at < count && (e->args.items[at]->type.kind == TY_GRID || e->args.items[at]->type.kind == TY_TEXTURE)) {
        texture = e->args.items[at++];
        const decl *grid = texture->type.decl;
        if (texture->type.kind == TY_GRID && (grid->dims != 2 || grid->fields.items[0].type.kind != TY_COLOR)) {
            diag_error(texture->at, "a texture is a Grid2<Color>, and this is a %s", type_name(texture->type));
            ok = false;
        }
        if (at < count && e->args.items[at]->type.kind == TY_ENUM && e->args.items[at]->type.decl == prog->filter) {
            draw_filter = e->args.items[at++];
        }
    }
    if (at < count) {
        diag_error(e->args.items[at]->at, "%s doesn't take %s here", draw_name, type_name(e->args.items[at]->type));
        note_mesh_forms(e);
        return T_ERR;
    }
    if (texture && e->frag && texture->type.kind == TY_GRID) {
        diag_error(texture->at, "a frag samples the grids it takes itself, so this draw takes no texture");
        diag_note("put the grid in a component or a singleton that '" STR_FMT "' takes, and read it there: "
                  "'color = models.wood.Sample(vertex.uv);'", STR_ARG(e->frag->name));
        ok = false;
    }
    const bool direct = shader_takes_texture(e->vert) || shader_takes_texture(e->frag);
    if (direct && (!texture || texture->type.kind != TY_TEXTURE)) {
        diag_error(e->at, "this mesh's shader takes a Texture source: supply one after the mesh arguments");
        ok = false;
    } else if (texture && texture->type.kind == TY_TEXTURE && e->frag && !direct) {
        diag_error(texture->at, "this mesh's frag has no Texture parameter for the explicit source");
        ok = false;
    }
    if (e->frag && draw_filter) {
        diag_error(draw_filter->at, "a frag chooses its texture filter at Sample: leave the draw's filter out");
        ok = false;
    }
    // With no frag, a pixel is its vertex's color times the texture at its uv
    if (!e->frag) {
        const field *color = field_called(vertex, "color");
        const field *uv = field_called(vertex, "uv");
        if (color && color->type.kind != TY_COLOR && color->type.kind != TY_ERROR) {
            diag_error(vertices->at, "with no frag, a mesh's pixels are its vertices' 'Color color', and " STR_FMT "'s 'color' is %s",
                       STR_ARG(vertex->name), type_name(color->type));
            diag_note("make it a Color, or name a frag that reads it, last in the call");
            ok = false;
        }
        if (texture && (!uv || uv->type.kind != TY_FLOAT2)) {
            diag_error(texture->at, "a texture is read at a vertex's 'float2 uv', which " STR_FMT " %s", STR_ARG(vertex->name),
                       uv ? "has as another type" : "doesn't have");
            ok = false;
        }
    }
    // The vert and the frag run on this mesh's vertex
    for (int i = count; i < e->args.count; i++) {
        const expr *name = e->args.items[i];
        const decl *shader = name->type_decl;
        const type on = shader->params.count ? shader->params.items[0].type : T_ERR;
        if (on.kind == TY_STRUCT && on.decl != vertex) {
            diag_error(name->at, "%s '" STR_FMT "' runs on a " STR_FMT ", and this mesh's vertices are a List<" STR_FMT ">",
                       shader_word(shader), STR_ARG(shader->name), STR_ARG(on.decl->name), STR_ARG(vertex->name));
            diag_note("a %s's first parameter is the vertex of the meshes it's drawn with", shader_word(shader));
            ok = false;
        } else if (on.kind != TY_STRUCT) {
            ok = false; // Its declaration said why
        }
    }
    if (shaded && (c->method || c->in_input || !in_view(c))) {
        diag_error(e->at, "a %s that names a vert or a frag goes in a view", draw_name);
        diag_note("the components they read are those of the entity the view runs for; a function can draw the mesh "
                  "without them");
        ok = false;
    } else if (shaded && ok) {
        const int errors = diag_error_count();
        for (int i = count; i < e->args.count; i++) check_shader_binding(c, e, e->args.items[i], e->args.items[i]->type_decl);
        ok = diag_error_count() == errors;
    }
    if (!ok) return T_ERR;

    if (instanced && transforms && vertex == prog->vertex3 && !shaded) {
        use_mesh_types(c);
        const type result = resolve_builtin_call(str_from("Draw3D"), e);
        if (result.kind == TY_ERROR) return result;
        check_default_args(c, e);
        e->call = CALL_DRAW;
        return result;
    }

    e->call = CALL_DRAW;
    e->c_callee = NULL;
    e->arg_want.count = 0;
    vec_push(e->arg_want, vertices->type);
    vec_push(e->arg_want, index_list);
    for (int i = 2; i < count; i++) vec_push(e->arg_want, i == 2 && dims == 3 && !instanced ? f4x4 : e->args.items[i]->type);
    const mesh_draw draw = {e, c->method ? c->method : c->system};
    vec_push(prog->mesh_draws, draw);
    return T_VOID_;
}

static const char *system_what(const decl *sys);

// A component code reaches through its World: a use the schedule counts as
// access to every entity's (parallel.c).
static void note_world_use(const checker *c, decl *component, const bool writes, const loc at)
{
    decl *code = c->system;
    for (int i = 0; i < code->world_uses.count; i++) {
        world_use *u = &code->world_uses.items[i];
        if (u->component != component) continue;
        u->through_world = true;
        if (writes && !u->writes) {
            u->writes = true;
            u->write_at = at;
        }
        return;
    }
    const world_use u = {component, writes, at, writes ? at : (loc){0, 0, 0}, false, true};
    vec_push(code->world_uses, u);
}

// world.Get<Health>(e), world.Has<Health>(e) and world.Set<Health>(e, value):
// any entity's component by its handle, through the World parameter `world`.
// Get gives a copy, or the component's zero value where the entity has none,
// which Has tells; Set, which `mut World` allows, puts one back and changes
// nothing there.
static type check_world_call(checker *c, expr *e, param *world)
{
    e->object->bind = BIND_PARAM;
    e->object->param = world;
    e->object->type = world->type;
    world->read = true;
    const decl *code = c->system;
    const bool set = str_eq_c(e->name, "Set");
    if (set) world->written = true;
    if (!set && !str_eq_c(e->name, "Get") && !str_eq_c(e->name, "Has")) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "the World has no method '" STR_FMT "'; it has Get, Has and Set", STR_ARG(e->name));
        suggestion s = suggest_start(e->name);
        suggest_consider_c(&s, "Get");
        suggest_consider_c(&s, "Has");
        suggest_consider_c(&s, "Set");
        suggest_note(&s);
        return T_ERR;
    }
    const char *usage = set ? "(entity, value)" : "(entity)";
    if (e->type_arg.len == 0) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, STR_FMT " takes the component in '<>': '" STR_FMT "." STR_FMT "<Health>%s'", STR_ARG(e->name),
                   STR_ARG(world->name), STR_ARG(e->name), usage);
        return T_ERR;
    }
    decl *d = find_type(c, e->type_arg, e->type_arg_at);
    e->type_decl = d;
    if (!d || d->kind != DECL_COMPONENT) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        if (!d) {
            diag_error(e->type_arg_at, "unknown component '" STR_FMT "'", STR_ARG(e->type_arg));
            suggestion s = suggest_start(e->type_arg);
            suggest_decls(&s, c->prog, true, false, false);
            suggest_note(&s);
            const fix create = {.kind = FIX_CREATE_COMPONENT, .at = e->type_arg_at, .name = e->type_arg};
            vec_push(c->prog->fixes, create);
        } else {
            diag_error(e->type_arg_at, STR_FMT " is of a component, any entity's by its handle, and '" STR_FMT "' is %s",
                       STR_ARG(e->name), STR_ARG(d->name), decl_what(d));
            if (d->kind == DECL_SINGLETON) {
                diag_note("a %s takes a singleton as a parameter, like '" STR_FMT " name'", system_what(code), STR_ARG(d->name));
            }
        }
        return T_ERR;
    }
    // Which world it's in: the match can't use local state, and a local
    // handler has no World of the match to read.
    const bool local_in_match = d->is_local && !is_local_code(code);
    if (local_in_match || (!d->is_local && code->is_handler && code->is_local)) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        if (local_in_match) {
            diag_error(e->type_arg_at, "'" STR_FMT "' is local, and the match can't use local state", STR_ARG(d->name));
            diag_note("local state belongs to one machine, and the match runs the same on every one");
        } else {
            diag_error(e->type_arg_at, "'" STR_FMT "' belongs to the match, and a local handler's World is this machine's",
                       STR_ARG(d->name));
            diag_note("a view reads any entity's components through its World; a local handler of a match event takes the "
                      "components of the entity the event was sent to");
        }
        return T_ERR;
    }
    // The entity, then Set's value
    const type_kind want = d->is_local ? TY_LOCAL_ENTITY : TY_ENTITY;
    bool ok = true;
    if (e->args.count > 0) {
        const type handle = check_expr(c, e->args.items[0]);
        if (handle.kind != TY_ERROR && handle.kind != want) {
            diag_error(e->args.items[0]->at, STR_FMT "<" STR_FMT "> takes an entity's handle, %s, not %s", STR_ARG(e->name),
                       STR_ARG(d->name), want == TY_ENTITY ? "an Entity" : "a LocalEntity", type_name(handle));
        }
        ok = handle.kind == want;
    }
    if (set && e->args.count > 1) {
        const type value = check_expr_want(c, e->args.items[1], decl_type(d));
        const bool fits = value.kind != TY_ERROR && type_assignable(decl_type(d), value);
        if (value.kind != TY_ERROR && !fits) {
            diag_error(e->args.items[1]->at, "Set<" STR_FMT "> takes the entity's new " STR_FMT ", not %s", STR_ARG(d->name),
                       STR_ARG(d->name), type_name(value));
        }
        ok = ok && fits;
    }
    for (int i = set ? 2 : 1; i < e->args.count; i++) check_expr(c, e->args.items[i]);
    if (e->args.count != (set ? 2 : 1)) {
        diag_error(e->at, set ? STR_FMT " takes an entity's handle and its new component: '" STR_FMT "." STR_FMT "<" STR_FMT ">%s'"
                              : STR_FMT " takes one entity's handle: '" STR_FMT "." STR_FMT "<" STR_FMT ">%s'",
                   STR_ARG(e->name), STR_ARG(world->name), STR_ARG(e->name), STR_ARG(d->name), usage);
        return T_ERR;
    }
    // A view only reads the match
    if (set && !d->is_local && code->is_view) {
        diag_error(e->at, "a view reads the match, and 'Set<" STR_FMT ">' would change it", STR_ARG(d->name));
        diag_note("views draw: a system changes the match");
        return T_ERR;
    }
    if (set && world->mode != PARAM_MUT) {
        diag_error(e->object->at, "'" STR_FMT "' is read-only", STR_ARG(world->name));
        diag_note("Set changes an entity's component: declare the parameter as 'mut World " STR_FMT "' to call it",
                  STR_ARG(world->name));
        const fix f = {.kind = FIX_ADD_MUT, .at = e->object->at, .param = world};
        vec_push(c->prog->fixes, f);
        return T_ERR;
    }
    if (set && c->parallel) {
        diag_error(e->at, "a parallel loop's steps run at once, so each changes only its own place (a grid's cell "
                          "or block, a list's element or block), and the variables it declares");
        diag_note("work it out in the step, or in a 'for' loop, which goes in order");
        return T_ERR;
    }
    if (set && c->compare) {
        diag_error(e->at, "a comparison runs again and again as the list sorts, so it changes nothing outside it");
        diag_note("work it out before the sort, or in the variables the comparison declares");
        return T_ERR;
    }
    // Set changes any entity's component, this one's included, which its
    // parameter would then put back as it was: one way or the other.
    for (int i = 0; set && i < code->params.count; i++) {
        const param *q = &code->params.items[i];
        if (q->type.kind != TY_COMPONENT || q->type.decl != d || (q->mode != PARAM_READ && q->mode != PARAM_MUT)) continue;
        diag_error(e->at, "Set<" STR_FMT "> changes any entity's " STR_FMT ", and the %s takes this entity's too",
                   STR_ARG(d->name), STR_ARG(d->name), system_what(code));
        diag_note("change this entity's through '" STR_FMT "', and read the others with '" STR_FMT ".Get<" STR_FMT
                  ">(entity)', or send them an event", STR_ARG(q->name), STR_ARG(world->name), STR_ARG(d->name));
        return T_ERR;
    }
    e->call = CALL_WORLD;
    if (ok) note_world_use(c, d, set, e->at);
    if (set) note_text_write(c, decl_holds_text(d)); // Its text and lists go into the world's heap
    if (set) return T_VOID_;
    return str_eq_c(e->name, "Has") ? T_BOOL_ : decl_type(d);
}

// Target receivers share the two drawing APIs: the argument's dimension picks
// an overloaded camera or conversion, and the other methods belong to one API.
static str target_draw_owner(const expr *e)
{
    bool three = str_eq_c(e->name, "Cube") || str_eq_c(e->name, "Sphere")
        || str_eq_c(e->name, "Cylinder") || str_eq_c(e->name, "Plane")
        || str_eq_c(e->name, "ScreenRayOrigin") || str_eq_c(e->name, "ScreenRayDirection");
    if (e->args.count && (str_eq_c(e->name, "Camera") || str_eq_c(e->name, "WorldToScreen")
                         || str_eq_c(e->name, "ScreenToWorld"))) {
        const type_kind kind = e->args.items[0]->type.kind;
        three = kind == TY_FLOAT3 || kind == TY_FLOAT4X4 || (str_eq_c(e->name, "Camera") && e->args.count == 3);
        if (str_eq_c(e->name, "Camera") && e->args.count == 2
            && e->args.items[1]->type.kind == TY_FLOAT4X4) three = true;
    }
    return str_from(three ? "Draw3D" : "Draw2D");
}

static type check_method(checker *c, expr *e)
{
    // world.Get<Health>(e): the World parameter's methods
    if (e->object->kind == E_NAME && !find_local(c, e->object->name)) {
        param *world = find_param(c, e->object->name);
        if (world && world->type.kind == TY_WORLD) return check_world_call(c, e, world);
    }
    if (e->type_arg.len > 0) {
        diag_error(e->type_arg_at, "'" STR_FMT "' takes no type in '<>'", STR_ARG(e->name));
        diag_note("the World's methods do: 'world.Get<Health>(entity)', in code that takes 'World world'");
    }
    if (names_wait(c, e->object)) {
        if (c->awaiting == e) return check_wait(c, e);
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "Wait." STR_FMT " is waited for: 'await Wait." STR_FMT "(...);'", STR_ARG(e->name), STR_ARG(e->name));
        return T_ERR;
    }
    if (e->object->kind == E_NAME && str_eq_c(e->object->name, "Scene") && !find_local(c, e->object->name)
        && !find_param(c, e->object->name)) {
        return check_scene_call(c, e);
    }
    if (e->object->kind == E_NAME && str_eq_c(e->object->name, "Session") && !find_local(c, e->object->name)
        && !find_param(c, e->object->name)) {
        return check_session_call(c, e);
    }
    if (e->object->kind == E_NAME && str_eq_c(e->object->name, "Clipboard") && !find_local(c, e->object->name)
        && !find_param(c, e->object->name)) {
        return check_clipboard_call(c, e);
    }

    // Math.Dot(a, b), quaternion.AxisAngle(axis, angle), Draw2D.Circle(center,
    // radius, color), GUILayout.Button(text)
    if (names_builtin_owner(c, e->object)) {
        const str owner = e->object->name;
        const bool io = str_eq_c(owner, "File") || str_eq_c(owner, "Directory");
        const bool flush = str_eq_c(owner, "Storage");
        if (io || flush) {
            if (!check_io_use(c, e->at, str_to_cstr(owner))) return T_ERR;
            if (flush && c->awaiting != e) {
                diag_error(e->at, "Storage.Flush is awaited: 'await Storage.Flush()'");
                return T_ERR;
            }
            if (io) {
                type bytes, paths;
                resolve_list_type(c, str_from("List<byte>"), e->at, &bytes);
                resolve_list_type(c, str_from("List<string>"), e->at, &paths);
                builtins_use_io(bytes.decl, paths.decl);
            }
        }
        if (str_eq_c(owner, "Draw") && str_eq_c(e->name, "Blit")) return check_blit(c, e);
        if (str_eq_c(owner, "Draw")) {
            diag_error(e->object->at, "unknown name 'Draw'");
            diag_note("drawing uses Draw2D or Draw3D; Draw has only Blit and window");
            return T_ERR;
        }
        if (builtin_draw_owner(owner) && (str_eq_c(e->name, "Mesh") || str_eq_c(e->name, "Instanced"))) return check_mesh_draw(c, e);
        // `default` takes its type from the version of the function the other arguments pick,
        // and [a, b, c] is the list the function takes there.
        for (int i = 0; i < e->args.count; i++) {
            expr *arg = e->args.items[i];
            type list;
            if (arg->kind == E_LIST && builtin_list_param(owner, e->name, i, arg, &list)) check_expr_want(c, arg, list);
            else if (arg->kind != E_DEFAULT) check_expr(c, arg);
        }
        const bool gui = str_eq_c(owner, "GUI") || str_eq_c(owner, "GUILayout");
        const bool cursor = str_eq_c(owner, "Cursor");
        if (cursor && !str_eq_c(e->name, "Lock")) {
            diag_error(e->at, "Cursor has no '" STR_FMT "'; it has Lock", STR_ARG(e->name));
            // Nothing lets the cursor go: it's asked for, frame by frame
            diag_note("the cursor comes back once no view calls Cursor.Lock: call it only in the frames the game wants "
                      "the mouse, like 'if (!menu.paused) Cursor.Lock();'");
            return T_ERR;
        }
        if ((gui || builtin_draw_owner(owner)) && !check_frame_use(c, e->at, gui ? "The GUI" : str_to_cstr(owner))) return T_ERR;
        const type result = resolve_builtin_call(owner, e);
        if (result.kind == TY_ERROR) return result;
        if (cursor && !check_cursor_use(c, e->at, true)) return T_ERR;
        check_default_args(c, e);
        if (io || flush) {
            e->io_operation = io;
            e->storage_flush = flush;
            c->prog->uses_text = true;
            if (str_eq_c(e->name, "Exists")) return result;
            if (io) vec_push(c->prog->io_calls, e);
            return result_of(c->prog, result, (type){TY_ENUM, c->prog->io_error}, false);
        }
        if (str_eq_c(owner, "Path")) {
            if (gpu_refuses(c, e->at, "work with paths", "work with paths in CPU code")) return T_ERR;
            e->path_operation = true;
            c->prog->uses_text = true;
        }
        if (builtin_draw_owner(owner)) {
            e->call = CALL_DRAW;
            e->draw_dims = str_eq_c(owner, "Draw3D") ? 3 : 2;
        }
        if (gui || cursor) return check_gui_call(c, e, result); // The cursor's lock is a frame's ask, which the GUI collects
        return result;
    }

    // Combat.Heal(...): a function in a namespace.
    const expr *root = chain_root(e->object);
    str ns;
    if (root->kind == E_NAME && !find_local(c, root->name) && !find_param(c, root->name)
        && !field_in_scope(c, root->name) && qualified_text(e->object, &ns) && is_namespace(c->prog, ns)) {
        char *text = arena_alloc((size_t)ns.len + (size_t)e->name.len + 2);
        memcpy(text, ns.ptr, (size_t)ns.len);
        text[ns.len] = '.';
        memcpy(text + ns.len + 1, e->name.ptr, (size_t)e->name.len);
        const str qualified = {text, ns.len + 1 + e->name.len};
        decl *const fn = find_named(c, qualified, e->at, NAME_FUNCTION);
        if (fn) {
            mark_namespaces(e->object);
            return check_function_call(c, e, fn);
        }
        // Combat.Stance(2): an enum in a namespace, converting an int.
        decl *const named = find_type(c, qualified, e->at);
        if (named && named->kind == DECL_ENUM) {
            mark_namespaces(e->object);
            return check_enum_conversion(c, e, named);
        }
        if (!named && !is_namespace(c->prog, qualified)) { // Nothing there to call: maybe misspelled
            mark_namespaces(e->object);
            for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
            diag_error(e->at, "namespace '" STR_FMT "' has no function '" STR_FMT "'", STR_ARG(ns), STR_ARG(e->name));
            suggestion s = suggest_start(e->name);
            for (int i = 0; i < c->prog->decls.count; i++) {
                const decl *other = c->prog->decls.items[i];
                if ((other->kind == DECL_FUNCTION || other->kind == DECL_ENUM) && str_eq(decl_ns(other), ns)) {
                    suggest_consider(&s, other->name);
                }
            }
            suggest_note(&s);
            return T_ERR;
        }
    }

    const type obj = check_expr(c, e->object);
    if (obj.kind == TY_ERROR) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        return T_ERR;
    }
    if (obj.kind == TY_RECORD && obj.decl == c->prog->pixel) { // pixel.Discard(): the frag's pixel isn't drawn
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        if (!str_eq_c(e->name, "Discard") || e->args.count) {
            diag_error(e->at, "a Pixel has one method, 'Discard()', which leaves the pixel undrawn; the rest are its fields");
            return T_ERR;
        }
        if (c->statement_call != e || c->short_circuit_depth > 0 || c->branch_depth > 0) {
            diag_error(e->at, "'" STR_FMT ".Discard()' is a statement of its own: it ends the frag there", STR_ARG(e->object->name));
            return T_ERR;
        }
        e->call = CALL_DISCARD;
        return T_VOID_;
    }
    if (obj.kind == TY_LIST) return check_list_method(c, e, obj);
    if (obj.kind == TY_MAP || obj.kind == TY_SET) return check_map_method(c, e, obj);
    if (obj.kind == TY_GRID) return check_grid_method(c, e, obj);
    if (obj.kind == TY_TEXTURE) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        if (str_eq_c(e->name, "Sample")) return check_grid_sample(c, e, obj);
        diag_error(e->at, "a Texture has Sample(uv[, filter]), not '" STR_FMT "'", STR_ARG(e->name));
        return T_ERR;
    }
    if (obj.kind == TY_RENDER_TARGET) {
        if (str_eq_c(e->name, "Resize") || str_eq_c(e->name, "Dispose")) {
            const bool resize = str_eq_c(e->name, "Resize");
            for (int i = 0; i < e->args.count; i++) check_expr_want(c, e->args.items[i], T_INT_);
            if (e->object->kind == E_MEMBER && e->object->c_constant && str_eq_c(e->object->member, "window")) {
                diag_error(e->at, "Draw.window is owned by the platform: it can't be resized or disposed");
                return T_ERR;
            }
            if (gpu_refuses(c, e->at, resize ? "resize a RenderTarget" : "dispose a RenderTarget", "the view owns the target")) return T_ERR;
            if (e->args.count != (resize ? 2 : 0)) {
                diag_error(e->at, resize ? "Resize takes width and height in pixels: 'target.Resize(width, height)'"
                                       : "Dispose takes no arguments: 'target.Dispose()'");
                return T_ERR;
            }
            for (int i = 0; i < e->args.count; i++) {
                const type t = e->args.items[i]->type;
                if (!type_assignable(T_INT_, t)) {
                    diag_error(e->args.items[i]->at, "a render target's size is in pixels, an int, not %s", type_name(t));
                }
                vec_push(e->arg_want, T_INT_);
            }
            e->call = CALL_BUILTIN;
            e->c_callee = resize ? "tide_render_target_resize" : "tide_render_target_dispose";
            e->resource_operation = true;
            decl *code = reading_code(c);
            if (code) code->calls_c = true;
            return T_VOID_;
        }
        e->targeted = true;
        if (str_eq_c(e->name, "Mesh") || str_eq_c(e->name, "Instanced")) return check_mesh_draw(c, e);
        for (int i = 0; i < e->args.count; i++) {
            if (e->args.items[i]->kind != E_DEFAULT) check_expr(c, e->args.items[i]);
        }
        const str owner = target_draw_owner(e);
        if (!check_frame_use(c, e->at, str_to_cstr(owner))) return T_ERR;
        const type result = resolve_builtin_call(owner, e);
        if (result.kind == TY_ERROR) return result;
        check_default_args(c, e);
        e->call = CALL_DRAW;
        e->draw_dims = str_eq_c(owner, "Draw3D") ? 3 : 2;
        return result;
    }
    // name.Contains("cat"), name.Substring(0, 3): text's methods
    if (obj.kind == TY_STRING) {
        for (int i = 0; i < e->args.count; i++) {
            if (e->args.items[i]->kind != E_DEFAULT) check_expr(c, e->args.items[i]);
        }
        const type result = resolve_builtin_call(str_from("string"), e);
        if (result.kind != TY_ERROR) {
            e->call = CALL_TEXT;
            check_default_args(c, e);
        }
        return result;
    }
    // random.NextInt(6): a Random's methods. Each steps the generator, so it's
    // called on one the code can change, as a struct's mut methods are.
    if (obj.kind == TY_STRUCT && obj.decl == c->prog->random) {
        for (int i = 0; i < e->args.count; i++) {
            if (e->args.items[i]->kind != E_DEFAULT) check_expr(c, e->args.items[i]);
        }
        if (gpu_refuses(c, e->at, "roll random numbers", "the view rolls what it needs and keeps it in a field the GPU reads")) {
            return T_ERR;
        }
        const type result = resolve_builtin_call(str_from("Random"), e);
        if (result.kind == TY_ERROR) return result;
        check_default_args(c, e);
        e->call = CALL_RANDOM;
        decl called = {0}; // For what the messages call it
        called.name = e->name;
        if (through_element(e->object)) {
            diag_error(e->at, "a list's element is a copy, so '" STR_FMT "' can't step it", STR_ARG(e->name));
            diag_note("take it out, roll, and put it back: 'var r = items[i]; r." STR_FMT "(...); items[i] = r;'", STR_ARG(e->name));
        } else {
            check_writable(c, e->object, &called, NULL);
        }
        return result;
    }
    // camera.Snap(): a singleton that jumped, like a camera cut.
    if (obj.kind == TY_SINGLETON && str_eq_c(e->name, "Snap")) return check_snap(c, e, obj.decl);
    // stats.IsDead(), unit.Heal(5): a struct's or component's method.
    if (obj.kind == TY_STRUCT || obj.kind == TY_COMPONENT) {
        decl *const m = find_method(obj.decl, e->name);
        if (!m) {
            for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
            diag_error(e->at, "%s has no method '" STR_FMT "'", type_name(obj), STR_ARG(e->name));
            if (obj.kind == TY_COMPONENT && str_eq_c(e->name, "Snap")) {
                diag_note("an entity jumps as a whole: snap it, like 'this.Snap()'");
                return T_ERR;
            }
            suggestion s = suggest_start(e->name);
            for (int i = 0; i < obj.decl->methods.count; i++) suggest_consider(&s, obj.decl->methods.items[i]->name);
            suggest_note(&s);
            return T_ERR;
        }
        if (m->is_interpolate) {
            for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
            diag_error(e->at, "Interpolate is how views blend %s between ticks; code doesn't call it", type_name(obj));
            return T_ERR;
        }
        if (m->is_mut_method && through_element(e->object)) {
            diag_error(e->at, "a list's element is a copy, so '" STR_FMT "' can't change it", STR_ARG(m->name));
            diag_note("take it out, change it, and put it back: 'var e = items[i]; e." STR_FMT "(...); items[i] = e;'",
                      STR_ARG(m->name));
        } else if (m->is_mut_method) {
            check_writable(c, e->object, m, NULL);
        }
        check_method_args(c, e, m);
        return m->result;
    }
    if (obj.kind != TY_ENTITY && obj.kind != TY_LOCAL_ENTITY) {
        diag_error(e->at, "%s has no method '" STR_FMT "'", type_name(obj), STR_ARG(e->name));
        return T_ERR;
    }
    if (str_eq_c(e->name, "Send")) return check_send(c, e);
    if (in_routine(c)) {
        diag_error(e->at, "%s can't change entities; systems do", routines(c));
        return T_ERR;
    }
    if (c->in_input) {
        diag_error(e->at, "%s runs outside the simulation, so it can't change entities", input_code(c));
        return T_ERR;
    }
    if (!check_entity_side(c, e)) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        return T_ERR;
    }
    e->local_world = in_async_function(c) ? obj.kind == TY_LOCAL_ENTITY : local_code(c);

    if (str_eq_c(e->name, "Add")) {
        e->call = CALL_ADD;
        if (e->args.count == 0) diag_error(e->at, "Add needs at least one component");
        const comp_mask added = check_component_list(c, e, "Add");
        c->prog->added_mask = mask_or(c->prog->added_mask, added);
        record_change(c, e, added, &c->prog->adds);
        for (int i = 0; i < c->prog->components.count; i++) {
            if (mask_has(added, i)) note_text_write(c, decl_holds_text(c->prog->components.items[i]));
        }
        return T_VOID_;
    }

    if (str_eq_c(e->name, "Remove")) {
        e->call = CALL_REMOVE;
        if (e->args.count == 0) diag_error(e->at, "Remove needs at least one component");
        comp_mask mask = mask_none();
        for (int i = 0; i < e->args.count; i++) {
            expr *arg = e->args.items[i];
            if (arg->kind == E_LITERAL) {
                diag_error(arg->at, "Remove takes component types, like 'Stunned', not values");
                continue;
            }
            decl *d = check_component_arg(c, arg, "Remove");
            if (!d || !check_component_side(c, arg, d)) continue;
            if (mask_has(mask, d->index)) diag_error(arg->at, "'" STR_FMT "' appears twice", STR_ARG(d->name));
            mask = mask_or(mask, bit(d));
        }
        c->prog->removed_mask = mask_or(c->prog->removed_mask, mask);
        record_change(c, e, mask, &c->prog->removes);
        return T_VOID_;
    }

    if (str_eq_c(e->name, "Destroy")) {
        e->call = CALL_DESTROY;
        if (e->args.count != 0) diag_error(e->at, "Destroy takes no arguments");
        c->prog->uses_destroy = true;
        return T_VOID_;
    }

    if (str_eq_c(e->name, "Snap")) return check_snap(c, e, NULL);

    diag_error(e->at, "%s has no method '" STR_FMT "'; it has Add, Remove, Destroy, Send and Snap", type_name(obj),
               STR_ARG(e->name));
    suggestion s = suggest_start(e->name);
    suggest_consider_c(&s, "Add");
    suggest_consider_c(&s, "Remove");
    suggest_consider_c(&s, "Destroy");
    suggest_consider_c(&s, "Send");
    suggest_consider_c(&s, "Snap");
    suggest_note(&s);
    return T_ERR;
}

// entity.Snap() and singleton.Snap(): it jumped this tick (a respawn, a
// portal, a camera cut), so views draw it as it is instead of blending it from
// where it was. The match says so; `singleton` is NULL for an entity.
static type check_snap(checker *c, expr *e, decl *singleton)
{
    for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
    if (e->args.count != 0) diag_error(e->at, "Snap takes no arguments");
    if (in_routine(c) || c->in_input) {
        diag_error(e->at, "%s can't snap what views draw; systems and event handlers do",
                   c->method ? routines(c) : input_code(c));
        return T_ERR;
    }
    const bool local = singleton ? singleton->is_local : e->object->type.kind == TY_LOCAL_ENTITY;
    if (local) {
        diag_error(e->at, "local state is never blended: views see it as it is");
        return T_ERR;
    }
    if (in_async_function(c)) {
        if (!note_task_side(c, TASK_MATCH, e->at)) return T_ERR;
    } else if (local_code(c)) {
        diag_error(e->at, "%s can't change the match, and snapping is part of it", local_code_what(c->system));
        diag_note("snap it in the system that moves it, so every machine draws the jump the same");
        return T_ERR;
    }
    if (singleton) {
        if (!check_writable(c, e->object, NULL, NULL)) return T_ERR;
        singleton->snapped = true;
    }
    e->call = CALL_SNAP;
    e->type_decl = singleton;
    return T_VOID_;
}

static const char *op_str(const tok_kind op)
{
    return tok_kind_name(op);
}

// The operator itself, without op_str's quotes: + for '+'.
static const char *op_symbol(const tok_kind op)
{
    static char buf[4][8];
    static int next;
    char *b = buf[next++ % 4];
    const char *quoted = tok_kind_name(op);
    snprintf(b, sizeof buf[0], "%s", quoted[0] == '\'' ? quoted + 1 : quoted);
    const size_t n = strlen(b);
    if (n > 0 && b[n - 1] == '\'') b[n - 1] = '\0';
    return b;
}

// Result type of `lhs op rhs`, or TY_ERROR after reporting.
static bool same_type(const type a, const type b)
{
    return a.kind == b.kind && a.decl == b.decl;
}

// The operator `op` a struct declares for operands of these types (`r` unused
// when `unary`), found in either operand's struct. Exact matches win over ones
// that need int to float.
static decl *find_operator(const tok_kind op, const type l, const type r, const bool unary, const loc at)
{
    const decl *owners[2] = {l.kind == TY_STRUCT ? l.decl : NULL,
                             !unary && r.kind == TY_STRUCT && r.decl != l.decl ? r.decl : NULL};
    decl *best = NULL;
    int best_score = -1;
    bool ambiguous = false;
    for (int o = 0; o < 2; o++) {
        for (int i = 0; owners[o] && i < owners[o]->methods.count; i++) {
            decl *m = owners[o]->methods.items[i];
            if (!m->is_operator || m->op != op || m->params.count != (unary ? 1 : 2)) continue;
            if (!type_assignable(m->params.items[0].type, l)) continue;
            if (!unary && !type_assignable(m->params.items[1].type, r)) continue;
            const int score = same_type(m->params.items[0].type, l) + (!unary && same_type(m->params.items[1].type, r));
            if (score > best_score) {
                best = m;
                best_score = score;
                ambiguous = false;
            } else if (score == best_score) {
                ambiguous = true;
            }
        }
    }
    if (ambiguous) {
        if (unary) diag_error(at, "operator %s is ambiguous for %s", op_str(op), type_name(l));
        else diag_error(at, "operator %s is ambiguous for %s and %s", op_str(op), type_name(l), type_name(r));
        diag_note("more than one operator takes these types; convert an operand so one fits exactly");
    }
    return best;
}

// What to declare when a struct has no operator for this.
static void note_missing_operator(const tok_kind op, const type l, const type r, const bool unary)
{
    const type t = l.kind == TY_STRUCT ? l : r;
    const bool comparison = op == T_EQ || op == T_NE || op == T_LT || op == T_LE || op == T_GT || op == T_GE;
    if (t.decl && t.decl->builtin && t.decl->fields.count) { // Random, Vertex: nowhere to declare an operator
        diag_note("%s is the engine's, and has no operators: work with its fields, like 'a." STR_FMT " %s b." STR_FMT "'",
                  type_name(t), STR_ARG(t.decl->fields.items[0].name), op_symbol(op), STR_ARG(t.decl->fields.items[0].name));
    } else if (unary) {
        diag_note("declare it in the struct, like '%s operator %s(%s value)'", type_name(t), op_symbol(op), type_name(t));
    } else if (op == T_EQ || op == T_NE) {
        diag_note("compare their fields instead, or declare 'bool operator %s(%s a, %s b)' in %s", op_symbol(op),
                  type_name(l), type_name(r), type_name(t));
    } else {
        diag_note("declare it in %s, like '%s operator %s(%s a, %s b)'", type_name(t), comparison ? "bool" : type_name(t),
                  op_symbol(op), type_name(l), type_name(r));
    }
}

// What text shows as it is: numbers, vectors and the like.
static bool text_shows_value(const type t)
{
    switch (t.kind) {
    case TY_STRING: case TY_BYTE: case TY_INT: case TY_LONG: case TY_FLOAT: case TY_BOOL: case TY_ENUM: case TY_ENTITY: case TY_LOCAL_ENTITY:
    case TY_PLAYER: case TY_INT2: case TY_INT3: case TY_INT4: case TY_FLOAT2: case TY_FLOAT3: case TY_FLOAT4:
    case TY_QUATERNION: case TY_COLOR: case TY_RECT: case TY_ERROR:
        return true;
    default:
        return false;
    }
}

// What text shows by what's in it: values with fields, as C# shows records
// (`Name { a = 1, b = 2 }`), and lists (`[1, 2]`). Not the device records.
static bool text_shows_inside(const type t)
{
    return t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_STRUCT
        || t.kind == TY_EVENT || t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET;
}

#define TEXT_DEPTH 32

// Whether text can show everything in a value of type `t`; if not, `why` gets
// the type in it that it can't. `outer` holds the types being looked into, so
// a struct with a list of itself ends.
static bool text_shows_all(const type t, type *why, const decl **outer, const int depth)
{
    if (text_shows_value(t)) return true;
    if (!text_shows_inside(t) || depth == TEXT_DEPTH) {
        *why = t;
        return false;
    }
    for (int i = 0; i < depth; i++) {
        if (outer[i] == t.decl) return true;
    }
    outer[depth] = t.decl;
    for (int i = 0; i < t.decl->fields.count; i++) {
        const field *f = &t.decl->fields.items[i];
        if (!f->hidden && !text_shows_all(f->type, why, outer, depth + 1)) return false;
    }
    return true;
}

// `t` and the types in it are shown: codegen writes their text helpers.
static void mark_shown(const type t)
{
    if (!text_shows_inside(t) || t.decl->shown) return;
    t.decl->shown = true;
    for (int i = 0; i < t.decl->fields.count; i++) mark_shown(t.decl->fields.items[i].type);
}

// What text can show: $"{x}", or "a" + x.
static bool text_can_hold(const type t)
{
    const decl *outer[TEXT_DEPTH];
    type why;
    if (!text_shows_all(t, &why, outer, 0)) return false;
    mark_shown(t);
    return true;
}

// Session's room is beside its stored value, in the local world. An ordinary
// helper receives only that value, so showing it must not look the room up.
static bool check_text_context(const checker *c, const expr *value)
{
    if (!in_routine(c) || c->method->kind == DECL_SHADER || value->type.kind != TY_SINGLETON
        || value->type.decl != c->prog->session) return true;
    diag_error(value->at, "functions and methods can't show a whole Session value: its room belongs to the local world");
    diag_note("show its stored fields, or pass the room separately as a string from the view or local handler");
    return false;
}

// Text can't show `t`: says what in it is why, and what to show instead.
static void text_cant_show(const loc at, const type t)
{
    const decl *outer[TEXT_DEPTH];
    type why = t;
    text_shows_all(t, &why, outer, 0);
    if (why.kind == t.kind && why.decl == t.decl) diag_error(at, "text can't show %s yet", type_name(t));
    else diag_error(at, "text can't show %s yet: it holds a %s", type_name(t), type_name(why));
    if (matrix_dim(why) > 0) diag_note("show its columns instead, like 'm.c0'");
    else if (has_fields(why)) diag_note("show its fields instead, like 'value.field'");
}

// A value's format in text, "F2" in $"{x:F2}", as tide/text.h takes it: 0
// for none, and after an error.
static int32_t text_format(const expr *value, const str format)
{
    if (format.len == 0) return 0;
    const type t = value->type;
    const char letter = format.ptr[0];
    bool digits_ok = format.len <= 3;
    int digits = 0;
    for (int i = 1; i < format.len && digits_ok; i++) {
        digits_ok = format.ptr[i] >= '0' && format.ptr[i] <= '9';
        digits = digits * 10 + format.ptr[i] - '0';
    }
    const bool floats = type_is_float_based(t) || t.kind == TY_QUATERNION || t.kind == TY_COLOR || t.kind == TY_RECT;
    const bool ints = type_is_int_based(t) || t.kind == TY_LONG;
    bool ok = false;
    if (letter == 'F' || letter == 'f') ok = (floats || ints) && digits <= 9;
    if (letter == 'D' || letter == 'd') ok = ints && digits <= 32;
    if (letter == 'X' || letter == 'x') ok = ints && digits <= (t.kind == TY_LONG ? 16 : 8);
    if (ok && digits_ok) return (int32_t)letter << 8 | digits;
    if (t.kind == TY_ERROR) return 0;
    diag_error(value->at, "'" STR_FMT "' isn't a format for %s", STR_ARG(format), type_name(t));
    if (floats) diag_note("floats take F and how many decimals, like '{x:F2}', up to F9");
    else if (ints) diag_note("ints and longs take D and how many digits ('{n:D3}' shows 007), X for hex, or F for decimals");
    else diag_note("only numbers have formats; %s shows as it is", type_name(t));
    return 0;
}

static type binary_result(const tok_kind op, const type l, const type r, const loc at, decl **overload)
{
    *overload = NULL;
    if (l.kind == TY_ERROR || r.kind == TY_ERROR) return T_ERR;
    // Text joins with anything it can show, and compares with text.
    if (l.kind == TY_STRING || r.kind == TY_STRING) {
        const type other = l.kind == TY_STRING ? r : l;
        if (op == T_PLUS && text_can_hold(other)) return (type){TY_STRING, NULL};
        if ((op == T_EQ || op == T_NE) && l.kind == TY_STRING && r.kind == TY_STRING) return T_BOOL_;
        if (op == T_PLUS) {
            text_cant_show(at, other);
        } else {
            diag_error(at, "operator %s can't be used with %s and %s", op_str(op), type_name(l), type_name(r));
            diag_note("text joins with '+' and compares with '==' and '!='");
        }
        return T_ERR;
    }
    if (l.kind == TY_STRUCT || r.kind == TY_STRUCT) {
        *overload = find_operator(op, l, r, false, at);
        if (*overload) return (*overload)->return_type;
    }
    // Byte operands promote before built-in arithmetic, comparison and bitwise work.
    if (l.kind == TY_BYTE || r.kind == TY_BYTE) {
        return binary_result(op, l.kind == TY_BYTE ? T_INT_ : l, r.kind == TY_BYTE ? T_INT_ : r, at, overload);
    }

    // Longs: with ints and longs, which widen to long. Never with floats by
    // themselves: float(x) and long(x) say which way the value goes.
    if (l.kind == TY_LONG || r.kind == TY_LONG) {
        const type other = l.kind == TY_LONG ? r : l;
        const bool integers = type_is_integer(l) && type_is_integer(r);
        switch (op) {
        case T_PLUS: case T_MINUS: case T_STAR: case T_SLASH: case T_PERCENT: case T_AMP: case T_PIPE: case T_CARET:
            if (integers) return T_LONG_;
            break;
        case T_SHL: case T_SHR:
            if (l.kind == TY_LONG && r.kind == TY_INT) return T_LONG_;
            if (integers) {
                diag_error(at, "a shift count is an int, not long");
                diag_note("convert it with int(n), which keeps its low 32 bits; the shift uses the low 6");
                return T_ERR;
            }
            break;
        case T_LT: case T_LE: case T_GT: case T_GE: case T_EQ: case T_NE:
            if (integers) return T_BOOL_;
            break;
        default:
            break;
        }
        diag_error(at, "operator %s can't be used with %s and %s", op_str(op), type_name(l), type_name(r));
        if (other.kind == TY_FLOAT) {
            diag_note("a long and a float don't mix by themselves: float(x) makes the long a float, and long(x) makes the "
                      "float a long, truncated toward zero");
        } else if (type_is_numeric(other)) {
            diag_note("there are no long vectors: convert the long with int(x), which keeps its low 32 bits");
        }
        return T_ERR;
    }

    // Colors: channel by channel with another, and scaled by a number.
    if (l.kind == TY_COLOR || r.kind == TY_COLOR) {
        const type other = l.kind == TY_COLOR ? r : l;
        if ((op == T_PLUS || op == T_MINUS || op == T_STAR) && other.kind == TY_COLOR) return l;
        if (op == T_STAR && is_scalar_number(other)) return (type){TY_COLOR, NULL};
        if (op == T_SLASH && l.kind == TY_COLOR && is_scalar_number(r)) return l;
        diag_error(at, "operator %s can't be used with %s and %s", op_str(op), type_name(l), type_name(r));
        diag_note("colors add, subtract and multiply channel by channel, and scale by a number: 'color * tint * 0.5'");
        return T_ERR;
    }

    switch (op) {
    case T_PLUS:
    case T_MINUS:
    case T_STAR:
    case T_SLASH:
    case T_PERCENT: {
        if (l.kind == TY_QUATERNION || r.kind == TY_QUATERNION) {
            diag_error(at, "operator %s can't be used with quaternions", op_str(op));
            diag_note("combine rotations with Math.Mul(a, b) and rotate vectors with Math.Rotate(q, v)");
            return T_ERR;
        }
        // Matrices: + and - with the same type, * and / by a number.
        const int ml = matrix_dim(l);
        const int mr = matrix_dim(r);
        if (ml || mr) {
            if ((op == T_PLUS || op == T_MINUS) && l.kind == r.kind) return l;
            if ((op == T_STAR || op == T_SLASH) && ml && is_scalar_number(r)) return l;
            if (op == T_STAR && mr && is_scalar_number(l)) return r;
            if (op == T_STAR) {
                diag_error(at, "operator '*' doesn't multiply matrices");
                diag_note("use Math.Mul(a, b) for a matrix product, or with a vector");
                return T_ERR;
            }
            break;
        }
        // Numbers and vectors, component-wise. A scalar widens to the vector's
        // size, and int widens to float.
        if (type_is_numeric(l) && type_is_numeric(r)) {
            const int dl = type_dim(l);
            const int dr = type_dim(r);
            const bool is_float = type_is_float_based(l) || type_is_float_based(r);
            if (dl > 1 && dr > 1 && dl != dr) break;
            if (op == T_PERCENT && is_float) break;
            return vector_type(is_float, dl > dr ? dl : dr);
        }
        break;
    }
    case T_SHL:
    case T_SHR:
    case T_AMP:
    case T_PIPE:
    case T_CARET:
        if (l.kind == TY_INT && r.kind == TY_INT) return T_INT_;
        break;
    case T_LT:
    case T_LE:
    case T_GT:
    case T_GE:
        if (is_scalar_number(l) && is_scalar_number(r)) return T_BOOL_;
        break;
    case T_EQ:
    case T_NE:
        if (is_scalar_number(l) && is_scalar_number(r)) return T_BOOL_;
        if (l.kind == TY_BOOL && r.kind == TY_BOOL) return T_BOOL_;
        if (l.kind == TY_ENTITY && r.kind == TY_ENTITY) return T_BOOL_;
        if (l.kind == TY_LOCAL_ENTITY && r.kind == TY_LOCAL_ENTITY) return T_BOOL_;
        if (l.kind == TY_ENUM && same_type(l, r)) return T_BOOL_;
        if (l.kind == TY_PLAYER && r.kind == TY_PLAYER) return T_BOOL_;
        break;
    case T_AND:
    case T_OR:
        if (l.kind == TY_BOOL && r.kind == TY_BOOL) return T_BOOL_;
        break;
    default:
        break;
    }
    diag_error(at, "operator %s can't be used with %s and %s", op_str(op), type_name(l), type_name(r));
    if (l.kind == TY_STRUCT || r.kind == TY_STRUCT) note_missing_operator(op, l, r, false);
    return T_ERR;
}

// Reads a swizzle such as `xz` or `wzyx` into e->swizzle. False if a letter
// isn't one of the vector's components.
static bool parse_swizzle(expr *e, const int dim)
{
    if (e->member.len < 1 || e->member.len > 4) return false;
    for (int i = 0; i < e->member.len; i++) {
        const char ch = e->member.ptr[i];
        const int index = ch == 'x' ? 0 : ch == 'y' ? 1 : ch == 'z' ? 2 : ch == 'w' ? 3 : -1;
        if (index < 0 || index >= dim) return false;
        e->swizzle[i] = index;
    }
    e->swizzle_len = e->member.len;
    return true;
}

static type use_constant(checker *c, expr *e, decl *k);

// Combat.Health or Game.Combat where a value belongs: says what the name is.
static type check_namespace_member(checker *c, expr *e)
{
    str text;
    qualified_text(e, &text);
    mark_namespaces(e->object);
    decl *k = find_named(c, text, e->at, NAME_CONST);
    if (k) return use_constant(c, e, k);
    decl *d = find_type(c, text, e->at);
    if (d) {
        e->bind = BIND_TYPE;
        e->type_decl = d;
        diag_error(e->at, "'" STR_FMT "' is a type, not a value", STR_ARG(text));
        return T_ERR;
    }
    if (is_namespace(c->prog, text)) {
        diag_error(e->at, "'" STR_FMT "' is a namespace, not a value", STR_ARG(text));
        return T_ERR;
    }
    str ns;
    str name;
    split_qualified(text, &ns, &name);
    diag_error(e->at, "namespace '" STR_FMT "' has no '" STR_FMT "'", STR_ARG(ns), STR_ARG(name));
    suggestion s = suggest_start(name);
    for (int i = 0; i < c->prog->decls.count; i++) {
        const decl *other = c->prog->decls.items[i];
        if (other->kind != DECL_SYSTEM && str_eq(decl_ns(other), ns)) suggest_consider(&s, other->name);
    }
    suggest_note(&s);
    return T_ERR;
}

// input.jump or input.buttons.jump: a field of an input parameter, maybe
// inside structs.
static bool is_input_field(const expr *e)
{
    if (e->kind != E_MEMBER || !e->field) return false;
    for (e = e->object; e->kind == E_MEMBER; e = e->object) {
        if (!e->field) return false;
    }
    return e->kind == E_NAME && e->bind == BIND_PARAM && e->param->type.kind == TY_INPUT;
}

// Page.Title, or Game.Page.Title: one of an enum's members.
static type check_enum_member(expr *e, decl *d)
{
    expr *type_name_expr = e->object;
    type_name_expr->bind = BIND_TYPE;
    type_name_expr->type_decl = d;
    if (type_name_expr->kind == E_MEMBER) mark_namespaces(type_name_expr->object);
    e->type_decl = d;
    for (int i = 0; i < d->members.count; i++) {
        if (str_eq(d->members.items[i].name, e->member)) {
            e->enum_member = &d->members.items[i];
            return (type){TY_ENUM, d};
        }
    }
    diag_error(e->at, "enum '" STR_FMT "' has no member '" STR_FMT "'", STR_ARG(d->name), STR_ARG(e->member));
    suggestion s = suggest_start(e->member);
    for (int i = 0; i < d->members.count; i++) suggest_consider(&s, d->members.items[i].name);
    suggest_note(&s);
    return T_ERR;
}

// The enum a chain of names like `Game.Page` names, or NULL.
static decl *named_enum(const checker *c, const expr *e)
{
    const expr *root = chain_root(e);
    str text;
    if (root->kind != E_NAME || find_local(c, root->name) || find_param(c, root->name) || field_in_scope(c, root->name)
        || !qualified_text(e, &text)) {
        return NULL;
    }
    decl *d = find_type(c, text, e->at);
    return d && d->kind == DECL_ENUM ? d : NULL;
}

// The parameter local fields are reached through: `box` in box.verts, a
// view's, a Forgotten handler's or a function's, of the component or singleton
// itself. Anything else of its type is a copy of its value, the match's part,
// which has no local fields.
static param *local_fields_param(const expr *object, const decl *d)
{
    if (object->kind != E_NAME || object->bind != BIND_PARAM) return NULL;
    param *p = object->param;
    const bool data = p->type.kind == TY_COMPONENT || p->type.kind == TY_SINGLETON;
    return data && p->type.decl == d && !p->task_ref ? p : NULL;
}

// Whether a place is in a local field: box.verts, box.verts[i], box.stats.hp.
static bool through_local_field(const expr *target)
{
    for (const expr *e = target; e->kind == E_MEMBER || e->kind == E_INDEX; e = e->object) {
        if (e->kind == E_MEMBER && e->local_field) return true;
    }
    return false;
}

// Whether a value is the match's as a local handler hears it: its event, or a
// component or singleton of the match it takes, or a field of one. They're
// copies from the event's turn, without what the match keeps in its heap.
static bool heard_value(const checker *c, const expr *e)
{
    const decl *sys = c->method ? NULL : c->system;
    if (!sys || sys->kind != DECL_SYSTEM || !sys->listens) return false;
    while (e->kind == E_MEMBER && !e->local_field) e = e->object;
    if (e->kind != E_NAME || e->bind != BIND_PARAM) return false;
    const type t = e->param->type;
    if (t.kind == TY_EVENT) return true;
    return (t.kind == TY_COMPONENT || t.kind == TY_SINGLETON) && !t.decl->is_local;
}

// Whether a value is a view's `verified` parameter, or a field of one: the
// confirmed world's, whose heap isn't the one the view reads text from.
static bool verified_value(const checker *c, const expr *e)
{
    const decl *sys = c->method ? NULL : c->system;
    if (!sys || sys->kind != DECL_SYSTEM || !sys->is_view) return false;
    while (e->kind == E_MEMBER && !e->local_field) e = e->object;
    return e->kind == E_NAME && e->bind == BIND_PARAM && e->param->verified;
}

// Text, a list, a map, a set or a grid: what a world keeps in its heap.
static bool kept_in_heap(const type t)
{
    return t.kind == TY_STRING || t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET || t.kind == TY_GRID;
}

static const char *heap_what(const type t)
{
    if (t.kind == TY_STRING) return "text";
    return t.kind == TY_LIST ? "a list" : t.kind == TY_MAP ? "a map" : t.kind == TY_SET ? "a set" : "a grid";
}

// A Forgotten handler's component or singleton: its entity is gone from the
// match, so only its local fields are left to read.
static bool is_forgotten_param(const checker *c, const param *p)
{
    const decl *sys = c->method ? NULL : c->system;
    return sys && sys->kind == DECL_SYSTEM && sys->forgets && (p->type.kind == TY_COMPONENT || p->type.kind == TY_SINGLETON)
        && p->type.decl == sys->forgets;
}

// box.verts, where `verts` is a local field of the match's `Box`: this
// machine's, read and written in place through the parameter of a view, of a
// local handler of Forgotten, or of a function they pass it to. The match
// never sees it, and a copy of the component hasn't got it.
static type check_local_field(const checker *c, expr *e, field *f, const type obj)
{
    const decl *d = obj.decl;
    const decl *code = c->method ? NULL : c->system;
    if (code && code->kind == DECL_SYSTEM && !is_local_code(code)) {
        diag_error(e->at, "'" STR_FMT "' is a local field of '" STR_FMT "': it's this machine's, and the match can't see it",
                   STR_ARG(f->name), STR_ARG(d->name));
        diag_note("the match runs the same on every machine; read and write local fields in a view, like 'view Name(mut "
                  STR_FMT " %s) { ... }'", STR_ARG(d->name), example_name(d));
        return T_ERR;
    }
    if (async_code(c)) {
        diag_error(e->at, "a task can't use local fields yet, and '" STR_FMT "' is one of '" STR_FMT "''s", STR_ARG(f->name),
                   STR_ARG(d->name));
        diag_note("read and write them in a view, and keep what the task needs in local state of its own");
        return T_ERR;
    }
    if (c->method && c->method->kind != DECL_FUNCTION && c->method->kind != DECL_SHADER) {
        diag_error(e->at, "'" STR_FMT "' is a local field of '" STR_FMT "', and methods work on values, which have none",
                   STR_ARG(f->name), STR_ARG(d->name));
        diag_note("write a function that takes the component instead: 'void Name(mut " STR_FMT " %s) { ... }'",
                  STR_ARG(d->name), example_name(d));
        return T_ERR;
    }
    param *p = local_fields_param(e->object, d);
    if (!p) {
        diag_error(e->at, "'" STR_FMT "' is a local field of '" STR_FMT "', and this is a copy of its value, which has none",
                   STR_ARG(f->name), STR_ARG(d->name));
        diag_note("local fields stay with the entity on this machine: read them through the parameter of a view, or of a "
                  "function the view passes it to");
        return T_ERR;
    }
    // Local fields are the entity's, whichever world a parameter's value comes
    // from: a view that takes the component both ways reaches them one way
    if (p->verified && code && code->kind == DECL_SYSTEM) {
        for (int i = 0; i < code->params.count; i++) {
            const param *twin = &code->params.items[i];
            if (twin == p || twin->verified || twin->type.decl != d || (twin->mode != PARAM_READ && twin->mode != PARAM_MUT)) continue;
            diag_error(e->at, "'" STR_FMT "' is a local field of '" STR_FMT "', which is the entity's whatever the server "
                       "confirmed: read it through '" STR_FMT "'", STR_ARG(f->name), STR_ARG(d->name), STR_ARG(twin->name));
            diag_note("'" STR_FMT "' is '" STR_FMT "' as the server confirmed it, for its match fields", STR_ARG(p->name),
                      STR_ARG(d->name));
            return T_ERR;
        }
    }
    if (!p->local_use) p->local_use_at = e->at;
    p->local_use = true;
    e->field = f;
    e->local_field = true;
    return f->type;
}

static type check_member(checker *c, expr *e)
{
    // quaternion.identity, Math.PI
    if (names_builtin_owner(c, e->object)) {
        if (str_eq_c(e->object->name, "Application") && !check_io_use(c, e->at, "Application paths")) return T_ERR;
        if (str_eq_c(e->object->name, "Draw") && !str_eq_c(e->member, "window")) {
            diag_error(e->object->at, "unknown name 'Draw'");
            diag_note("drawing uses Draw2D or Draw3D; Draw has only Blit and window");
            return T_ERR;
        }
        if (str_eq_c(e->object->name, "Screen") && !check_frame_use(c, e->at, "Screen")) return T_ERR;
        if (builtin_draw_owner(e->object->name) && !check_frame_use(c, e->at, str_to_cstr(e->object->name))) return T_ERR;
        const type member = resolve_builtin_member(e->object->name, e);
        if (str_eq_c(e->object->name, "Frame") && member.kind == TY_ERROR) {
            diag_note("'Frame.dt' is the seconds since this machine's previous frame started");
        } else if (str_eq_c(e->object->name, "Frame")) {
            if (!check_frame_time_use(c, e->at)) return T_ERR;
        } else if (str_eq_c(e->object->name, "Cursor") && member.kind == TY_ERROR) {
            diag_note("'Cursor.locked' is whether the window holds the cursor, hidden, which 'Cursor.Lock()' asks for");
        } else if (str_eq_c(e->object->name, "Cursor")) {
            if (!check_cursor_use(c, e->at, false)) return T_ERR;
            // Sample reads it of the devices it samples; views of their frame's
            if (c->in_input && !c->method) e->c_constant = "tide_dev->mouse.locked";
        }
        return member;
    }

    decl *const enum_decl = named_enum(c, e->object);
    if (enum_decl) return check_enum_member(e, enum_decl);

    // Combat.Health: a namespace, not a variable, on the left. Combat.ORIGIN.x
    // is a member of a constant, which checking Combat.ORIGIN finds.
    const expr *root = chain_root(e);
    str text;
    str object_text;
    decl *ignored;
    if (root->kind == E_NAME && !find_local(c, root->name) && !find_param(c, root->name)
        && !field_in_scope(c, root->name) && is_namespace(c->prog, root->name)
        && qualified_text(e, &text)
        && !(qualified_text(e->object, &object_text) && lookup(c->prog, c->unit, object_text, NAME_CONST, &ignored))) {
        return check_namespace_member(c, e);
    }

    c->device_whole_ok = e->object; // devices.gamepad reads what it names, not all of devices
    c->member_object = e->object;
    const type obj = check_expr(c, e->object);
    if (obj.kind == TY_ERROR) return T_ERR;

    if (obj.kind == TY_RENDER_TARGET) {
        if (gpu_code(c)) {
            gpu_refuses(c, e->at, "read a RenderTarget", "the view supplies its color image as a Texture instead");
            return T_ERR;
        }
        if (str_eq_c(e->member, "color")) {
            if (e->object->kind == E_MEMBER && e->object->c_constant && str_eq_c(e->object->member, "window")) {
                diag_error(e->at, "Draw.window has no sampleable color image: draw into a RenderTarget first");
                return T_ERR;
            }
            return (type){TY_TEXTURE, NULL};
        }
        diag_error(e->at, "a RenderTarget has 'color', not '" STR_FMT "'", STR_ARG(e->member));
        return T_ERR;
    }

    const int dim = type_dim(obj);
    if (dim >= 2) {
        static const char *components[] = {"", "", "x, y", "x, y, z", "x, y, z, w"};
        if (!parse_swizzle(e, dim)) {
            diag_error(e->at, "'" STR_FMT "' doesn't match %s's components (%s)", STR_ARG(e->member), type_name(obj),
                       components[dim]);
            return T_ERR;
        }
        return vector_type(type_is_float_based(obj), e->swizzle_len);
    }
    if (obj.kind == TY_QUATERNION) {
        if (str_eq_c(e->member, "value")) return (type){TY_FLOAT4, NULL};
        diag_error(e->at, "quaternion has one member, 'value' (a float4), not '" STR_FMT "'", STR_ARG(e->member));
        return T_ERR;
    }
    if (obj.kind == TY_COLOR) {
        if (str_eq_c(e->member, "r") || str_eq_c(e->member, "g") || str_eq_c(e->member, "b") || str_eq_c(e->member, "a")) {
            return T_FLOAT_;
        }
        diag_error(e->at, "Color has r, g, b and a, not '" STR_FMT "'", STR_ARG(e->member));
        return T_ERR;
    }
    if (obj.kind == TY_MAP || obj.kind == TY_SET) {
        if (str_eq_c(e->member, "count")) return T_INT_;
        diag_error(e->at, "a %s has 'count' and methods like %s, not '" STR_FMT "'", obj.kind == TY_SET ? "set" : "map",
                   obj.kind == TY_SET ? "Add(...)" : "Has(...)", STR_ARG(e->member));
        suggestion sg = suggest_start(e->member);
        suggest_consider_c(&sg, "count");
        suggest_note(&sg);
        return T_ERR;
    }
    if (obj.kind == TY_LIST) {
        if (str_eq_c(e->member, "count")) return T_INT_;
        diag_error(e->at, "a list has 'count' and methods like Add(...), not '" STR_FMT "'", STR_ARG(e->member));
        suggestion sg = suggest_start(e->member);
        suggest_consider_c(&sg, "count");
        suggest_note(&sg);
        return T_ERR;
    }
    if (obj.kind == TY_GRID) {
        if (str_eq_c(e->member, "size")) return obj.decl->dims == 2 ? (type){TY_INT2, NULL} : (type){TY_INT3, NULL};
        diag_error(e->at, "a grid has 'size' and Clear(), not '" STR_FMT "'", STR_ARG(e->member));
        suggestion sg = suggest_start(e->member);
        suggest_consider_c(&sg, "size");
        suggest_note(&sg);
        return T_ERR;
    }
    if (obj.kind == TY_STRING) {
        if (str_eq_c(e->member, "length")) return T_INT_;
        diag_error(e->at, "text has 'length' and methods like Contains(...), not '" STR_FMT "'", STR_ARG(e->member));
        suggestion s = suggest_start(e->member);
        suggest_consider_c(&s, "length");
        suggest_note(&s);
        return T_ERR;
    }
    if (obj.kind == TY_RECT) {
        if (str_eq_c(e->member, "x") || str_eq_c(e->member, "y") || str_eq_c(e->member, "width")
            || str_eq_c(e->member, "height")) {
            return T_FLOAT_;
        }
        diag_error(e->at, "Rect has x, y, width and height, not '" STR_FMT "'", STR_ARG(e->member));
        suggestion s = suggest_start(e->member);
        suggest_consider_c(&s, "x");
        suggest_consider_c(&s, "y");
        suggest_consider_c(&s, "width");
        suggest_consider_c(&s, "height");
        suggest_note(&s);
        return T_ERR;
    }
    const int n = matrix_dim(obj);
    if (n > 0) {
        if (e->member.len == 2 && e->member.ptr[0] == 'c' && e->member.ptr[1] >= '0' && e->member.ptr[1] < '0' + n) {
            return vector_type(true, n);
        }
        diag_error(e->at, "%s has columns c0 to c%d, not '" STR_FMT "'", type_name(obj), n - 1, STR_ARG(e->member));
        return T_ERR;
    }

    // input.jump.down: a bool field of an input parameter, compared with last tick.
    if (obj.kind == TY_BOOL) {
        const bool on_input_field = is_input_field(e->object);
        const bool edge = str_eq_c(e->member, "down") || str_eq_c(e->member, "up");
        if (edge && on_input_field) {
            e->edge = str_eq_c(e->member, "down") ? EDGE_DOWN : EDGE_UP;
            return T_BOOL_;
        }
        if (edge) {
            diag_error(e->at, "only input fields have '." STR_FMT "', like 'input.jump." STR_FMT "'",
                       STR_ARG(e->member), STR_ARG(e->member));
            if (c->in_input && !c->in_sanitize) {
                diag_note("in Sample, take 'Devices devices' and read 'devices.keyboard.space.down'");
            }
            return T_ERR;
        }
        if (on_input_field && (str_eq_c(e->member, "pressed") || str_eq_c(e->member, "released"))) {
            diag_error(e->at, "input fields have '.down' and '.up', not '." STR_FMT "'", STR_ARG(e->member));
            diag_note(str_eq_c(e->member, "pressed")
                          ? "the field is true the whole time it's held; '.down' is true on the tick it went down"
                          : "'.up' is true on the tick it went up");
            return T_ERR;
        }
    }

    // touches.count: how many a device array holds, in use or not.
    if (obj.kind == TY_RECORD && obj.decl->array_of) {
        if (str_eq_c(e->member, "count")) {
            sb count = {0};
            sb_printf(&count, "%d", obj.decl->array_length);
            e->c_constant = count.data;
            return T_INT_;
        }
        diag_error(e->at, "%s has 'count', and its elements by index, not '" STR_FMT "'", type_name(obj), STR_ARG(e->member));
        suggestion s = suggest_start(e->member);
        suggest_consider_c(&s, "count");
        if (s.distance <= 1) suggest_note(&s);
        else diag_note("its elements are 'touches[i]', or 'foreach (var touch in touches)'");
        return T_ERR;
    }

    // session.room: the code of the room the match is in, and gone.message: a
    // kick's message, for Disconnected. The host keeps them beside the local
    // state (tide_local's tide_room and tide_message), so they need no heap.
    const bool session = has_fields(obj) && obj.decl == c->prog->session;
    const bool disconnected = has_fields(obj) && obj.decl == c->prog->disconnected;
    if ((session && str_eq_c(e->member, "room")) || (disconnected && str_eq_c(e->member, "message"))) {
        if (session && in_routine(c) && c->method->kind != DECL_SHADER) {
            diag_error(e->at, "functions and methods can't read Session.room: it belongs to the local world, not the Session value");
            diag_note("pass the room separately as a string from the view or local handler");
            return T_ERR;
        }
        e->c_constant = session ? "tide_str_from_cstr(tide_l->tide_room)" : "tide_str_from_cstr(tide_l->tide_message)";
        return (type){TY_STRING, NULL};
    }

    if (has_fields(obj)) {
        for (int i = 0; i < obj.decl->fields.count; i++) {
            field *f = &obj.decl->fields.items[i];
            if (str_eq(f->name, e->member) && !f->hidden) {
                if (f->typed) { // keyboard.text: made into text where it's read, so no field of text
                    e->typed_text = true;
                    return check_typed_use(c, e->at) ? f->type : T_ERR;
                }
                // A Forgotten handler's entity is gone from the match
                if (e->object->kind == E_NAME && e->object->bind == BIND_PARAM && is_forgotten_param(c, e->object->param)) {
                    diag_error(e->at, "'" STR_FMT "' is a match field of '" STR_FMT "', and the entity is gone: only its "
                               "local fields are left", STR_ARG(f->name), STR_ARG(obj.decl->name));
                    diag_note("keep what the handler needs of the match in a local field, set while the entity was there");
                    return T_ERR;
                }
                if (heard_value(c, e->object) && kept_in_heap(f->type)) {
                    diag_error(e->at, "'" STR_FMT "' is %s of the match's, which a local handler doesn't hear yet",
                               STR_ARG(f->name), heap_what(f->type));
                    diag_note("a local handler gets what it takes of the match as the event left it, but text, lists, maps, "
                              "sets and grids stay in the match: send what it needs of them in a field of another type, or "
                              "read them in a view");
                    return T_ERR;
                }
                if (verified_value(c, e->object) && kept_in_heap(f->type)) {
                    diag_error(e->at, "'" STR_FMT "' is %s of the confirmed match's, which a view doesn't read yet",
                               STR_ARG(f->name), heap_what(f->type));
                    diag_note("a 'verified' parameter gives the confirmed world's numbers, vectors, enums and entities; its "
                              "text, lists, maps, sets and grids are read through a parameter that isn't 'verified'");
                    return T_ERR;
                }
                e->field = f;
                if (f == c->prog->seed) c->prog->uses_seed = true; // Time.seed: the world has it for the code that reads it
                if (obj.decl->query_row && e->object->bind == BIND_LOCAL && e->object->local->query) {
                    param *query = e->object->local->query;
                    for (int k = 0; k < query->query.count; k++) {
                        param *binding = &query->query.items[k];
                        if (str_eq(binding->name, f->name)) binding->read = true;
                    }
                }
                if (f->leaf && !device_group_type(f->type)) note_device_member(c, e);
                return f->type;
            }
        }
        field *local = (obj.kind == TY_COMPONENT || obj.kind == TY_SINGLETON) ? local_field_of(obj.decl, e->member) : NULL;
        if (local) return check_local_field(c, e, local, obj);
        diag_error(e->at, "%s has no field '" STR_FMT "'", type_name(obj), STR_ARG(e->member));
        if (obj.kind == TY_RECORD && str_eq_c(obj.decl->name, "Button") && str_eq_c(e->member, "released")) {
            diag_note("'up' is true on the tick the button went up");
            return T_ERR;
        }
        suggestion s = suggest_start(e->member);
        suggest_fields(&s, obj.decl);
        if (session) suggest_consider_c(&s, "room");
        if (disconnected) suggest_consider_c(&s, "message");
        suggest_note(&s);
        return T_ERR;
    }
    diag_error(e->at, "%s has no members", type_name(obj));
    return T_ERR;
}

// Devices are always explicit parameters. Their source depends on the code:
// this machine's sample or frame, or the owning player's networked sample.
static bool check_devices_use(checker *c, const loc at)
{
    if (gpu_refuses(c, at, "read the devices", "the view puts what it needs in a component or a singleton, which it takes")) return false;
    if (c->in_sanitize) {
        diag_error(at, "Sanitize runs on every machine for every player's input, so it can't read this machine's Devices");
        diag_note("Sample reads them, on the machine the input comes from");
        return false;
    }
    diag_error(at, "'Devices' is a type, not a value: take a 'Devices devices' parameter");
    diag_note("Sample and views receive this machine's devices; match code receives the entity owner's, or the server's");
    return false;
}

static type check_name(checker *c, expr *e)
{
    stmt *local = find_local(c, e->name);
    if (local) {
        e->bind = BIND_LOCAL;
        e->local = local;
        if (e == c->write_root) local->assigned = true; // `count += 1` alone never uses count
        else local->read = true;
        if (local->query && (c->parallel || c->block_depth || c->compare)) {
            diag_error(e->at, "a query row can't be captured by parallel loops or callback blocks");
            diag_note("copy the entity handle or a component binding before the loop or callback");
            return T_ERR;
        }
        if (local->query && c->member_object != e) {
            diag_error(e->at, "a query row is only read through its entity and named component fields");
            diag_note("read '" STR_FMT ".entity' or one of its component bindings; query rows can't be stored or passed on",
                      STR_ARG(e->name));
            return T_ERR;
        }
        return local->type;
    }
    param *p = find_param(c, e->name);
    if (p) {
        e->bind = BIND_PARAM;
        e->param = p;
        p->read = true;
        if (p->type.kind == TY_QUERY && c->query_value != e) {
            diag_error(e->at, "a query is only used directly by foreach");
            diag_note("write 'foreach (var row in " STR_FMT ") { ... }'; queries can't be stored or passed on", STR_ARG(e->name));
            return T_ERR;
        }
        if (p->type.kind == TY_ACTION) {
            diag_error(e->at, "an Action can only be run: '" STR_FMT "();'", STR_ARG(e->name));
            diag_note("it's the code the caller wrote after the call, and it runs where it's run");
            return T_ERR;
        }
        if (c->member_object != e && is_forgotten_param(c, p)) {
            diag_error(e->at, "the entity is gone from the match, so '" STR_FMT "' is only here for its local fields",
                       STR_ARG(e->name));
            diag_note("read them one by one, like '" STR_FMT "." STR_FMT "', and pass those on", STR_ARG(e->name),
                      STR_ARG(p->type.decl->local_part->fields.items[0].name));
            return T_ERR;
        }
        if (p->type.kind == TY_WORLD) {
            diag_error(e->at, "the World is no value: code calls it, like '" STR_FMT ".Get<Health>(entity)'", STR_ARG(e->name));
            diag_note("Get gives a copy of an entity's component, Has tells whether it has one, and Set, with 'mut World', "
                      "puts one back");
            return T_ERR;
        }
        return p->type;
    }
    if (str_eq_c(e->name, "Devices")) {
        e->bind = BIND_DEVICES;
        return check_devices_use(c, e->at) ? (type){TY_RECORD, c->prog->devices} : T_ERR;
    }
    // Inside the input's Sample and Sanitize, and a type's methods, the fields
    // are in scope by name.
    field *f = field_in_scope(c, e->name);
    if (f) {
        e->bind = BIND_FIELD;
        e->field = f;
        return f->type;
    }
    // A method's own local fields aren't in scope: a value has none
    if (c->method && c->method->owner && local_field_of(c->method->owner, e->name)) {
        const decl *owner = c->method->owner;
        diag_error(e->at, "'" STR_FMT "' is a local field of '" STR_FMT "', and methods work on values, which have none",
                   STR_ARG(e->name), STR_ARG(owner->name));
        diag_note("write a function that takes the component instead: 'void Name(mut " STR_FMT " %s) { ... }'",
                  STR_ARG(owner->name), example_name(owner));
        return T_ERR;
    }
    decl *k = find_named(c, e->name, e->at, NAME_CONST);
    if (k) return use_constant(c, e, k);
    decl *shader = find_named(c, e->name, e->at, NAME_SHADER);
    if (shader) {
        e->bind = BIND_SHADER;
        e->type_decl = shader;
        diag_error(e->at, "'" STR_FMT "' is a %s: the GPU runs it, for the meshes a Draw2D.Mesh or Draw3D.Mesh names it for", STR_ARG(e->name),
                   shader_word(shader));
        const bool three_d = shader->params.count && shader->params.items[0].type.kind == TY_STRUCT
                         && shader_dims(shader->params.items[0].type.decl) == 3;
        diag_note("name it last in a view's Draw2D.Mesh or Draw3D.Mesh, like 'Draw%dD.Mesh(vertices, indices, %s" STR_FMT ");'",
                  three_d ? 3 : 2, three_d ? "transform, " : "",
                  STR_ARG(e->name));
        return T_ERR;
    }
    const decl *d = find_type(c, e->name, e->at);
    if (d) {
        diag_error(e->at, "'" STR_FMT "' is a type, not a value", STR_ARG(e->name));
        if (d->kind == DECL_SINGLETON) {
            char lower[64];
            snprintf(lower, sizeof lower, STR_FMT, STR_ARG(d->name));
            lower[0] = (char)(lower[0] >= 'A' && lower[0] <= 'Z' ? lower[0] - 'A' + 'a' : lower[0]);
            diag_note("singletons are parameters: '(" STR_FMT " %s)'", STR_ARG(d->name), lower);
        }
        if (d == c->prog->random) { // Random.NextInt(6), as an engine with one generator for everything has it
            diag_note("a Random is a generator's state, which lives where the game puts it: keep one in a component "
                      "or a singleton, 'Random random;', and roll with it, 'dice.random.NextInt(6)'");
        }
        return T_ERR;
    }
    if (is_namespace(c->prog, e->name)) {
        e->bind = BIND_NAMESPACE;
        diag_error(e->at, "'" STR_FMT "' is a namespace, not a value", STR_ARG(e->name));
        return T_ERR;
    }
    diag_error(e->at, "unknown name '" STR_FMT "'", STR_ARG(e->name));
    // What's in scope, and the built-in names that start expressions (math. for Math.)
    suggestion s = suggest_start(e->name);
    for (int i = 0; i < c->locals.count; i++) suggest_consider(&s, c->locals.items[i]->name);
    for (int i = 0; c->system && i < c->system->params.count; i++) suggest_consider(&s, c->system->params.items[i].name);
    if (c->in_input) suggest_fields(&s, c->system);
    if (c->method && c->method->owner) suggest_fields(&s, c->method->owner);
    for (int i = 0; i < c->prog->decls.count; i++) {
        if (c->prog->decls.items[i]->kind == DECL_CONST) suggest_consider(&s, c->prog->decls.items[i]->name);
    }
    suggest_consider_c(&s, "Math");
    suggest_consider_c(&s, "Draw2D");
    suggest_consider_c(&s, "Draw3D");
    suggest_builtin_types(&s);
    suggest_note(&s);
    return T_ERR;
}

// `this`: the entity the code runs for. Systems, views and handlers that run
// once per entity have one, and so do a component's methods: the entity whose
// component they're called on (see check_this_calls).
static type check_this(const checker *c, const expr *e)
{
    decl *m = c->method;
    if (gpu_refuses(c, e->at, "name the entity",
                    "it takes the entity's components as parameters, and reads their fields")) {
        return T_ERR;
    }
    if (m && m->kind == DECL_FUNCTION) {
        diag_error(e->at, "functions don't run for an entity, so they have no 'this'");
        diag_note("pass it the entity as a parameter, like 'Entity entity'");
        return T_ERR;
    }
    if (m && m->owner->kind != DECL_COMPONENT) {
        diag_error(e->at, "'" STR_FMT "' is %s, which belongs to no entity, so its methods have no 'this'",
                   STR_ARG(m->owner->name), decl_what(m->owner));
        diag_note("a component's methods have one: the entity whose component they're called on");
        return T_ERR;
    }
    if (m && m->is_interpolate) {
        diag_error(e->at, "Interpolate blends values between ticks, which belong to no entity, so it has no 'this'");
        return T_ERR;
    }
    if (m) {
        if (!m->uses_this) m->this_at = e->at;
        m->uses_this = true;
        return m->owner->is_local ? (type){TY_LOCAL_ENTITY, NULL} : T_ENTITY_;
    }
    if (c->in_input) {
        diag_error(e->at, "%s works on a player's input, not on an entity, so it has no 'this'", input_code(c));
        return T_ERR;
    }
    const decl *sys = c->system;
    if (!sys) {
        diag_error(e->at, "'this' is the entity code runs for, and a default value runs for none");
        return T_ERR;
    }
    if (sys->per_entity) return sys->entity_local ? (type){TY_LOCAL_ENTITY, NULL} : T_ENTITY_;
    if (sys->is_handler && sys->event && sys->event->world_event) {
        diag_error(e->at, "'" STR_FMT "' is sent to the world, not to an entity, so '" STR_FMT "' has no 'this'",
                   STR_ARG(sys->event->name), STR_ARG(sys->name));
    } else if (sys->is_handler && sys->event) {
        diag_error(e->at, "'" STR_FMT "' takes nothing from the entity '" STR_FMT "' is sent to, so it runs once per "
                   "event and has no 'this'", STR_ARG(sys->name), STR_ARG(sys->event->name));
        diag_note("take a component of that entity, or only require one: 'event(" STR_FMT " ...) " STR_FMT "(with Name)'",
                  STR_ARG(sys->event->name), STR_ARG(sys->name));
    } else if (sys->is_handler) {
        diag_error(e->at, "'" STR_FMT "' runs once per event, not for an entity, so it has no 'this'", STR_ARG(sys->name));
    } else {
        diag_error(e->at, "'" STR_FMT "' takes no components, so it runs once per %s, not for an entity, and has no 'this'",
                   STR_ARG(sys->name), sys->is_view ? "frame" : "tick");
        diag_note("take a component, or only require one: '%s " STR_FMT "(with Name)'", sys->is_view ? "view" : "system",
                  STR_ARG(sys->name));
    }
    return T_ERR;
}

// A side of cond ? a : b that has to be a value.
static bool check_side(const expr *side, const type t)
{
    if (t.kind == TY_VOID) {
        diag_error(side->at, "this side of '?:' doesn't produce a value");
        return false;
    }
    return t.kind != TY_ERROR;
}

// cond ? a : b. As in C#, the sides need the same type, or one that converts
// to the other's (int to float). A side that's `default` takes the other's.
static type check_conditional(checker *c, expr *e)
{
    const type cond = check_expr(c, e->cond);
    if (cond.kind != TY_ERROR && cond.kind != TY_BOOL) {
        diag_error(e->cond->at, "the condition of '?:' must be bool, not %s", type_name(cond));
        if (type_is_numeric(cond) && type_dim(cond) == 1) diag_note("compare it, for example 'x != 0 ? a : b'");
    }
    c->branch_depth++;
    type a;
    type b;
    if (e->lhs->kind == E_DEFAULT && e->rhs->kind != E_DEFAULT) {
        b = check_expr(c, e->rhs);
        a = check_expr_want(c, e->lhs, b);
    } else {
        a = check_expr(c, e->lhs);
        b = e->rhs->kind == E_DEFAULT ? check_expr_want(c, e->rhs, a) : check_expr(c, e->rhs);
    }
    c->branch_depth--;
    const bool a_ok = check_side(e->lhs, a);
    const bool b_ok = check_side(e->rhs, b);
    if (!a_ok || !b_ok || cond.kind != TY_BOOL) return T_ERR;
    if (type_assignable(a, b)) return a;
    if (type_assignable(b, a)) return b;
    diag_error(e->at, "the two sides of '?:' have different types: %s and %s", type_name(a), type_name(b));
    diag_note("convert one side so both have the same type");
    return T_ERR;
}

// items[i]: a list's element, a copy.
static type check_index(checker *c, expr *e)
{
    c->device_whole_ok = e->object; // devices.touchscreen.touches[0] reads the one it names
    const type obj = check_expr(c, e->object);
    if (obj.kind == TY_GRID) { // cells[int2(x, y)], or cells[x, y], which the parser makes the same
        const bool flat = obj.decl->dims == 2;
        const type position = check_expr(c, e->lhs);
        if (position.kind != TY_ERROR && position.kind != (flat ? TY_INT2 : TY_INT3)) {
            diag_error(e->lhs->at, "a Grid%d's cells are at %s positions, not %s", obj.decl->dims, flat ? "int2" : "int3",
                       type_name(position));
            diag_note(flat ? "like 'cells[x, y]', or 'cells[p]' for an int2 'p'" : "like 'cells[x, y, z]', or 'cells[p]' for an int3 'p'");
        }
        return grid_cell(obj);
    }
    if (obj.kind == TY_MAP) { // scores[key]: its value, or the value type's zero when the key isn't there
        const type key = check_expr_want(c, e->lhs, map_key(obj));
        if (key.kind != TY_ERROR && !type_assignable(map_key(obj), key)) {
            diag_error(e->lhs->at, "%s is keyed by %s, not %s", type_name(obj), type_name(map_key(obj)), type_name(key));
        }
        return map_value(obj);
    }
    if (obj.kind == TY_SET) {
        check_expr(c, e->lhs);
        diag_error(e->at, "a set has no values to index: it holds keys");
        diag_note("ask whether a key is in it with 'Has(key)', or go through its keys with foreach");
        return T_ERR;
    }
    const type index = check_expr(c, e->lhs);
    if (obj.kind == TY_ERROR) return T_ERR;
    if (index.kind != TY_ERROR && !type_assignable(T_INT_, index)) diag_error(e->lhs->at, "a list's index is an int, not %s", type_name(index));
    if (obj.kind == TY_RECORD && obj.decl->array_of) return (type){TY_RECORD, obj.decl->array_of}; // touches[i]
    if (obj.kind != TY_LIST) {
        diag_error(e->at, "only lists and maps have elements to index, and this is %s", type_name(obj));
        if (obj.kind == TY_STRING) diag_note("a piece of text is 'text.Substring(start, length)'");
        return T_ERR;
    }
    return list_element(obj);
}

// [a, b, c] where a List<T> or a Set<T> goes.
static type check_list_literal(checker *c, expr *e, const type want)
{
    if (want.kind == TY_MAP) {
        diag_error(e->at, "a map starts empty; there's no map literal yet");
        diag_note("add entries one by one: 'scores[key] = value;'");
        for (int i = 0; i < e->args.count; i++) check_expr_want(c, e->args.items[i], T_ERR);
        return T_ERR;
    }
    if (want.kind != TY_LIST && want.kind != TY_SET) {
        for (int i = 0; i < e->args.count; i++) check_expr_want(c, e->args.items[i], T_ERR);
        return T_ERR;
    }
    const bool set = want.kind == TY_SET;
    const type element = set ? map_key(want) : list_element(want);
    for (int i = 0; i < e->args.count; i++) {
        expr *item = e->args.items[i];
        const type t = check_expr_want(c, item, element);
        if (t.kind == TY_VOID) diag_error(item->at, "this doesn't produce a value to put in the %s", set ? "set" : "list");
        else if (!type_assignable(element, t)) diag_error(item->at, "this %s holds %s, not %s", set ? "set" : "list", type_name(element), type_name(t));
    }
    c->prog->uses_text = true; // Made in the scratch area
    e->type = want;
    return want;
}

// scores.Has(k), scores.Get(k), scores.Remove(k), scores.Clear(), and a
// set's Add(k): a map's or a set's methods. Those that change it need it to
// be a variable or field that can be changed. Reading and writing values is
// indexing: scores[k], scores[k] = v.
static type check_map_method(checker *c, expr *e, const type map)
{
    static const struct {
        const char *name;
        const char *form;
        bool set_only;
        bool map_only;
        bool key; // Takes a key
        bool changes;
        type_kind result;
    } methods[] = {
        {"Has", "Has(key)", false, false, true, false, TY_BOOL},
        {"Get", "Get(key)", false, true, true, false, TY_OPTIONAL},
        {"Add", "Add(key)", true, false, true, true, TY_BOOL},
        {"Remove", "Remove(key)", false, false, true, true, TY_BOOL},
        {"Clear", "Clear()", false, false, false, true, TY_VOID},
    };
    const bool set = map.kind == TY_SET;
    const char *what = set ? "set" : "map";
    int m = -1;
    for (int i = 0; i < (int)(sizeof methods / sizeof methods[0]); i++) {
        if (str_eq_c(e->name, methods[i].name) && !(set && methods[i].map_only) && !(!set && methods[i].set_only)) m = i;
    }
    if (m < 0) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        if (!set && str_eq_c(e->name, "Add")) {
            diag_error(e->at, "a map takes values by key: '" STR_FMT "[key] = value;'", STR_ARG(e->object->kind == E_NAME ? e->object->name : str_from("scores")));
        } else if (set && str_eq_c(e->name, "Get")) {
            diag_error(e->at, "a set has no values to get: it holds keys, which Has(key) looks for");
        } else {
            diag_error(e->at, "a %s has no method '" STR_FMT "'", what, STR_ARG(e->name));
            suggestion sg = suggest_start(e->name);
            for (size_t i = 0; i < sizeof methods / sizeof methods[0]; i++) {
                if (!(set && methods[i].map_only) && !(!set && methods[i].set_only)) suggest_consider_c(&sg, methods[i].name);
            }
            suggest_note(&sg);
        }
        return T_ERR;
    }
    e->call = CALL_MAP;
    e->type_decl = map.decl;
    const type key = map_key(map);
    const int argc = methods[m].key ? 1 : 0;
    for (int i = 0; i < e->args.count; i++) check_expr_want(c, e->args.items[i], key);
    if (e->args.count != argc) {
        diag_error(e->at, "%s takes %d argument%s: '%s'", methods[m].name, argc, argc == 1 ? "" : "s", methods[m].form);
        return T_ERR;
    }
    if (argc == 1 && !type_assignable(key, e->args.items[0]->type)) {
        diag_error(e->args.items[0]->at, "%s is keyed by %s, not %s", type_name(map), type_name(key), type_name(e->args.items[0]->type));
    }
    if (methods[m].changes) {
        decl *changer = NEW(decl);
        sb name = {0};
        sb_printf(&name, "%s.%s", set ? "Set" : "Map", methods[m].name);
        changer->name = (str){name.data, (int)name.len};
        check_writable(c, e->object, changer, NULL);
    }
    if (methods[m].result == TY_OPTIONAL) { // Get: the value, or nothing
        const type optional = optional_type(c, map_value(map), e->at);
        if (optional.kind != TY_ERROR) map.decl->optional = optional.decl;
        return optional;
    }
    return (type){methods[m].result, NULL};
}

// "a.x < b.x" for the notes about a Sort's comparison, with its elements' names.
static const char *compare_example(const expr *sort)
{
    static char buf[2][160];
    static int next;
    char *b = buf[next++ % 2];
    const str a = sort->compare[0] ? sort->compare[0]->name : str_from("a");
    const str z = sort->compare[1] ? sort->compare[1]->name : str_from("b");
    snprintf(b, sizeof buf[0], STR_FMT " < " STR_FMT, STR_ARG(a), STR_ARG(z));
    return b;
}

static void check_compare_stmt(checker *c, const stmt *s);

// What a call the GUI takes does, as messages say it: Cursor.Lock is one, as the
// GUI collects the frame's ask.
static const char *gui_call_what(const expr *e)
{
    const bool cursor = e->object && e->object->kind == E_NAME && str_eq_c(e->object->name, "Cursor");
    return cursor ? "lock the cursor" : "use the GUI";
}

// What a comparison can't do: anything with an effect, since it runs again
// and again as the list sorts, in an order nothing promises.
static void check_compare_expr(checker *c, const expr *e)
{
    if (!e) return;
    const char *what = e->io_operation ? "access the filesystem" : NULL;
    switch (e->call) {
    case CALL_SPAWN: what = "spawn"; break;
    case CALL_SEND: what = "send events"; break;
    case CALL_ADD: case CALL_REMOVE: case CALL_DESTROY: what = "change entities"; break;
    case CALL_LOAD: case CALL_UNLOAD: case CALL_SCENE_PLAYER: what = "load or unload scenes"; break;
    case CALL_SESSION: what = "change the session"; break;
    case CALL_CLIPBOARD: what = "use the clipboard"; break;
    case CALL_DRAW: what = "draw"; break;
    case CALL_GUI: what = gui_call_what(e); break;
    case CALL_SNAP: what = "snap"; break;
    case CALL_WAIT: what = "wait"; break;
    case CALL_ACTION: what = "run a block"; break;
    case CALL_NEW_GRID: what = "make grids"; break;
    default: break;
    }
    if (!what && e->kind == E_AWAIT) what = "wait";
    if (e->resource_operation) what = "change rendering resources";
    if (!what && (e->call == CALL_FUNCTION || e->call == CALL_METHOD) && e->method && e->method->is_extern) what = "call C";
    if (!what && (e->call == CALL_FUNCTION || e->call == CALL_METHOD) && e->method && e->method->is_async) what = "start tasks";
    if (what) {
        diag_error(e->at, "a comparison can't %s: it runs again and again as the list sorts, in an order nothing promises", what);
        diag_note("it only compares its two elements, and returns whether the first goes before the second");
    }
    check_compare_expr(c, e->object);
    check_compare_expr(c, e->lhs);
    check_compare_expr(c, e->rhs);
    check_compare_expr(c, e->cond);
    for (int i = 0; i < e->args.count; i++) check_compare_expr(c, e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) check_compare_expr(c, e->inits.items[i].value);
    if (e->block) check_compare_stmt(c, e->block);
}

static void check_compare_stmt(checker *c, const stmt *s)
{
    if (!s) return;
    for (int i = 0; i < s->stmts.count; i++) check_compare_stmt(c, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++) check_compare_expr(c, s->cases.items[i].labels.items[k]);
        for (int k = 0; k < s->cases.items[i].body.count; k++) check_compare_stmt(c, s->cases.items[i].body.items[k]);
    }
    check_compare_expr(c, s->cond);
    check_compare_expr(c, s->value);
    check_compare_expr(c, s->target);
    check_compare_stmt(c, s->init);
    check_compare_stmt(c, s->step);
    check_compare_stmt(c, s->then_stmt);
    check_compare_stmt(c, s->else_stmt);
}

// items.Sort(a, b) { return a.x < b.x; }: sorts the list, stable, by a
// comparison written after the call, whose two names are the elements it
// compares, read-only copies. It's checked as a function of its own: it
// returns a bool on every path, and changes nothing but what it declares.
static type check_sort(checker *c, expr *e, const type list)
{
    const type element = list_element(list);
    e->call = CALL_LIST;
    e->type_decl = list.decl;
    if (!e->block || e->args.count != 2 || e->args.items[0]->kind != E_NAME || e->args.items[1]->kind != E_NAME) {
        for (int i = 0; i < e->args.count; i++) {
            if (e->args.items[i]->kind != E_NAME) check_expr(c, e->args.items[i]);
        }
        diag_error(e->at, "Sort names the two elements it compares, then says in a block whether the first goes before the second");
        diag_note("like 'items.Sort(a, b) { return a.x < b.x; }'");
        if (e->block) check_stmt(c, e->block);
        return T_ERR;
    }
    const char *refused = c->method && c->method->owner        ? "a struct's or component's method"
                        : c->method && c->method->takes_action ? "a function that takes an Action"
                        : async_code(c)                        ? "async code"
                        : c->block_depth > 0                   ? "a block written after a call"
                        : c->in_input                          ? input_code(c)
                                                               : NULL;
    if (refused) {
        diag_error(e->at, "%s can't sort with a comparison yet", refused);
        diag_note("sort in a system, a view, a handler or a plain function");
    }
    decl *changer = NEW(decl);
    changer->name = str_from("List.Sort");
    check_writable(c, e->object, changer, NULL);
    push_scope(c);
    for (int i = 0; i < 2; i++) {
        expr *arg = e->args.items[i];
        stmt *local = NEW(stmt);
        local->kind = S_VAR;
        local->at = arg->at;
        local->name = arg->name;
        local->name_at = arg->at;
        local->type = element;
        check_reserved(arg->name, arg->at);
        if (find_local(c, arg->name) || find_param(c, arg->name)) {
            diag_error(arg->at, "'" STR_FMT "' is already declared", STR_ARG(arg->name));
        } else if (i == 1 && str_eq(arg->name, e->args.items[0]->name)) {
            diag_error(arg->at, "the two elements need two names");
        }
        vec_push(c->locals, local);
        arg->bind = BIND_LOCAL;
        arg->local = local;
        arg->type = element;
        e->compare[i] = local;
    }
    const expr *outer = c->compare;
    const int outer_locals = c->compare_locals;
    const int loops = c->loop_depth;
    const int switches = c->switch_depth;
    c->compare = e;
    c->compare_locals = c->locals.count;
    c->loop_depth = 0;
    c->switch_depth = 0;
    c->block_depth++;
    check_stmt(c, e->block);
    c->block_depth--;
    check_compare_stmt(c, e->block);
    c->compare = outer;
    c->compare_locals = outer_locals;
    c->loop_depth = loops;
    c->switch_depth = switches;
    pop_scope(c);
    if (!always_returns(e->block)) {
        diag_error(e->block->at, "the comparison doesn't return on every path");
        diag_note("end every path with 'return %s;' or the like: whether the first element goes before the second",
                  compare_example(e));
    }
    return T_VOID_;
}

// items.Add(x), items.Contains(x), ...: a list's methods. Those that change it
// need it to be a variable or field that can be changed.
static type check_list_method(checker *c, expr *e, const type list)
{
    if (str_eq_c(e->name, "Sort")) return check_sort(c, e, list);
    static const struct {
        const char *name;
        const char *form;
        int argc;
        bool index;   // The first argument is an index
        bool element; // The last argument is an element
        bool changes;
        bool compares;
        type_kind result;
    } methods[] = {
        {"Add", "items.Add(item)", 1, false, true, true, false, TY_VOID},
        {"Insert", "items.Insert(index, item)", 2, true, true, true, false, TY_VOID},
        {"RemoveAt", "items.RemoveAt(index)", 1, true, false, true, false, TY_VOID},
        {"Remove", "items.Remove(item)", 1, false, true, true, true, TY_BOOL},
        {"Clear", "items.Clear()", 0, false, false, true, false, TY_VOID},
        {"Contains", "items.Contains(item)", 1, false, true, false, true, TY_BOOL},
        {"IndexOf", "items.IndexOf(item)", 1, false, true, false, true, TY_INT},
    };
    const type element = list_element(list);
    int m = -1;
    for (int i = 0; i < (int)(sizeof methods / sizeof methods[0]); i++) {
        if (str_eq_c(e->name, methods[i].name)) m = i;
    }
    if (m < 0) {
        for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
        diag_error(e->at, "a list has no method '" STR_FMT "'", STR_ARG(e->name));
        suggestion sg = suggest_start(e->name);
        for (size_t i = 0; i < sizeof methods / sizeof methods[0]; i++) suggest_consider_c(&sg, methods[i].name);
        suggest_note(&sg);
        return T_ERR;
    }
    e->call = CALL_LIST;
    e->type_decl = list.decl;
    for (int i = 0; i < e->args.count; i++) {
        const bool is_element = methods[m].element && i == methods[m].argc - 1;
        check_expr_want(c, e->args.items[i], is_element ? element : T_INT_);
    }
    if (e->args.count != methods[m].argc) {
        diag_error(e->at, "%s takes %d argument%s: '%s'", methods[m].name, methods[m].argc, methods[m].argc == 1 ? "" : "s",
                   methods[m].form);
        return T_ERR;
    }
    for (int i = 0; i < e->args.count; i++) {
        const expr *arg = e->args.items[i];
        const bool is_element = methods[m].element && i == methods[m].argc - 1;
        const type want = is_element ? element : T_INT_;
        if (!type_assignable(want, arg->type)) {
            diag_error(arg->at, "%s takes %s here, not %s: '%s'", methods[m].name, type_name(want), type_name(arg->type),
                       methods[m].form);
        }
    }
    if (methods[m].compares && element.kind != TY_ERROR && !equatable(element)) {
        diag_error(e->at, "%s compares elements with '==', which %s doesn't have", methods[m].name, type_name(element));
        diag_note("go through the list with 'foreach' and compare what matters");
    }
    if (methods[m].changes) {
        decl *changer = NEW(decl);
        sb name = {0};
        sb_printf(&name, "List.%s", methods[m].name);
        changer->name = (str){name.data, (int)name.len};
        check_writable(c, e->object, changer, NULL);
    }
    return (type){methods[m].result, NULL};
}

static bool is_new_grid(const expr *e)
{
    return e->kind == E_CALL && (str_eq_c(e->name, "Grid2") || str_eq_c(e->name, "Grid3"));
}

// Grid2(1024, 1024), Grid3(0, 384, 0) or Grid2(): a grid of that size, open
// on the axes given 0 or left out. Its cell type comes from where it goes.
static type check_new_grid(checker *c, expr *e, const type want)
{
    const int dims = e->name.ptr[4] - '0';
    for (int i = 0; i < e->args.count; i++) {
        const type t = check_expr_want(c, e->args.items[i], T_INT_);
        if (!type_assignable(T_INT_, t)) {
            diag_error(e->args.items[i]->at, "a grid's size is in cells, an int, not %s", type_name(t));
        }
    }
    if (e->args.count > dims) {
        diag_error(e->at, "Grid%d takes up to %d sizes, one per axis: 'Grid%d(%s)'", dims, dims, dims,
                   dims == 2 ? "width, height" : "width, height, depth");
        return e->type = T_ERR;
    }
    if (want.kind == TY_ERROR) return e->type = T_ERR;
    if (want.kind != TY_GRID) {
        diag_error(e->at, "a grid's cell type comes from where it goes");
        diag_note("make it in a field, like 'Grid%d<Color> pixels = Grid%d(%s);'", dims, dims, dims == 2 ? "256, 256" : "16, 16, 16");
        return e->type = T_ERR;
    }
    if (want.decl->dims != dims) {
        diag_error(e->at, "this is a %s, made with Grid%d(...)", type_name(want), want.decl->dims);
        return e->type = T_ERR;
    }
    e->call = CALL_NEW_GRID;
    e->type_decl = want.decl;
    c->prog->uses_text = true; // It's made in the scratch area, which code clears after it runs
    return e->type = want;
}

// cells.Clear(): a grid's one method, every cell back to zero.
// texture.Sample(uv) and texture.Sample(uv, Filter.Point): a grid of colors
// read as a texture, in a frag or vert. The grid is a field of one of its
// parameters, and the filter is written there: the GPU is told both before
// any pixel runs.
static type check_grid_sample(const checker *c, expr *e, const type grid)
{
    const decl *shader = gpu_code(c);
    if (!shader) {
        diag_error(e->at, grid.kind == TY_TEXTURE ? "Sample reads a Texture on the GPU: in a frag or a vert"
                                               : "Sample reads a grid as a texture, which the GPU does: in a frag or a vert");
        if (grid.kind == TY_GRID) diag_note("here, read a cell by its place: 'grid[x, y]'");
        return T_ERR;
    }
    if (grid.kind == TY_GRID && (grid.decl->dims != 2 || grid.decl->fields.items[0].type.kind != TY_COLOR)) {
        diag_error(e->at, "a texture is a Grid2<Color>, and this is a %s", type_name(grid));
        return T_ERR;
    }
    const expr *of = e->object;
    const bool direct = grid.kind == TY_TEXTURE && of->kind == E_NAME && of->bind == BIND_PARAM
                        && of->param->type.kind == TY_TEXTURE;
    if (!direct && (of->kind != E_MEMBER || !of->field || of->object->kind != E_NAME || of->object->bind != BIND_PARAM
                    || of->object->param->function_param)) {
        diag_error(of->at, "a %s samples a %s that's a field of a component or singleton it takes, like 'models.wood.Sample(uv)'%s",
                   shader_word(shader), grid.kind == TY_TEXTURE ? "Texture" : "grid",
                   grid.kind == TY_TEXTURE ? ", or its direct Texture parameter" : "");
        return T_ERR;
    }
    if (!direct && of->object->param->type.kind == TY_STRUCT) {
        diag_error(of->at, "a texture is shared by an instanced draw: keep the sampled %s in a component or singleton",
                   grid.kind == TY_TEXTURE ? "Texture" : "grid");
        return T_ERR;
    }
    const type uv = e->args.count >= 1 ? e->args.items[0]->type : T_ERR;
    const bool filter_ok = e->args.count < 2 || (e->args.items[1]->type.kind == TY_ENUM && e->args.items[1]->type.decl == c->prog->filter);
    if (e->args.count < 1 || e->args.count > 2 || (uv.kind != TY_ERROR && uv.kind != TY_FLOAT2) || !filter_ok) {
        if (uv.kind != TY_ERROR || e->args.count != 1) {
            diag_error(e->at, "Sample takes where to read, a float2 from (0, 0) to (1, 1), and how, a Filter: "
                              "'texture.Sample(uv)' or 'texture.Sample(uv, Filter.Point)'");
        }
        return T_ERR;
    }
    if (e->args.count == 2 && !e->args.items[1]->enum_member) {
        diag_error(e->args.items[1]->at, "the filter is written here, 'Filter.Point' or 'Filter.Bilinear': the GPU is told how a "
                                         "texture is read before any pixel runs");
        return T_ERR;
    }
    e->call = CALL_GRID;
    e->type_decl = grid.decl;
    return (type){TY_COLOR, NULL};
}

static type check_grid_method(checker *c, expr *e, const type grid)
{
    for (int i = 0; i < e->args.count; i++) check_expr(c, e->args.items[i]);
    if (str_eq_c(e->name, "Sample")) return check_grid_sample(c, e, grid);
    if (!str_eq_c(e->name, "Clear")) {
        diag_error(e->at, "a grid has no method '" STR_FMT "'", STR_ARG(e->name));
        suggestion sg = suggest_start(e->name);
        suggest_consider_c(&sg, "Sample");
        suggest_consider_c(&sg, "Clear");
        suggest_note(&sg);
        return T_ERR;
    }
    if (e->args.count) {
        diag_error(e->at, "Clear takes no arguments: 'cells.Clear()'");
        return T_ERR;
    }
    e->call = CALL_GRID;
    e->type_decl = grid.decl;
    decl *changer = NEW(decl);
    changer->name = str_from("Grid.Clear");
    check_writable(c, e->object, changer, NULL);
    return T_VOID_;
}

// The code being checked, for messages about where errors go: "a system".
// NULL in a method or function.
static const char *code_what(const checker *c)
{
    if (c->method) return NULL;
    if (c->in_input) return c->in_sanitize ? "Sanitize" : "Sample";
    if (!c->system) return "a default value";
    return c->system->is_view ? "a view" : c->system->is_handler ? "an event handler" : "a system";
}

// try call: its value, or its error passed on to the caller, which fails with
// the same error. Only functions and methods have a caller to take it.
static type check_try(checker *c, expr *e)
{
    const type t = check_expr_any(c, e->lhs);
    if (t.kind == TY_ERROR) return T_ERR;
    if (t.kind == TY_OPTIONAL) {
        diag_error(e->at, "'try' passes an error on, and %s has none: it's a value or nothing", type_name(t));
        diag_note("write '?? fallback' for a value when there's none, or 'is %s' to use it only when it's there",
                  pattern_example(wrapped_value(t), false));
        return T_ERR;
    }
    if (t.kind != TY_FAILABLE) {
        diag_error(e->at, "'try' passes on the error of a call that can fail, and %s can't fail", wrapped_what(e->lhs));
        return T_ERR;
    }
    const type error = wrapped_error(t);
    const decl *m = c->method;
    if (!m) {
        diag_error(e->at, "'try' passes the error to the caller, and %s has none", code_what(c));
        diag_note("handle it here: '?? fallback', 'is %s', or '!' to carry on with the default",
                  pattern_example(error, true));
        return T_ERR;
    }
    if (m->fails.kind == TY_VOID && m->fails_name.len > 0) return T_ERR; // Its `fails` is wrong, and said so
    if (m->fails.kind == TY_VOID) {
        diag_error(e->at, "'" STR_FMT "' doesn't fail, so 'try' can't pass the error on", STR_ARG(m->name));
        if (m->takes_action) {
            diag_note("a function that takes an Action can't fail yet; handle it here with '?\?', 'is' or '!'");
        } else {
            diag_note("say it fails after its parameters, '" STR_FMT " " STR_FMT "(...) fails %s', or handle it here "
                      "with '?\?', 'is' or '!'", STR_ARG(m->return_type_name), STR_ARG(m->name), type_name(error));
        }
        return T_ERR;
    }
    if (!same_type(m->fails, error)) {
        diag_error(e->at, "%s fails with %s, and '" STR_FMT "' fails with %s", wrapped_what(e->lhs), type_name(error),
                   STR_ARG(m->name), type_name(m->fails));
        diag_note("'try' passes the error on as it is; handle this one here with '?\?', 'is' or '!'");
        return T_ERR;
    }
    return wrapped_value(t);
}

// value!: its value, or its type's default when it fails or is nothing.
static type check_defaulted(checker *c, expr *e)
{
    const type t = check_expr_any(c, e->lhs);
    if (t.kind == TY_ERROR) return T_ERR;
    if (!is_wrapped(t)) {
        diag_error(e->at, "'!' after a call carries on with the default when it fails, and %s can't fail",
                   wrapped_what(e->lhs));
        if (t.kind == TY_BOOL) diag_note("'!' before a bool negates it: '!value'");
        return T_ERR;
    }
    const type value = wrapped_value(t);
    if (value.kind == TY_STRING || value.kind == TY_LIST || value.kind == TY_MAP || value.kind == TY_SET) c->prog->uses_text = true;
    return value;
}

// a ?? b: a's value, or b when a fails or is nothing. b only runs then.
static type check_coalesce(checker *c, expr *e)
{
    const type l = check_expr_any(c, e->lhs);
    const type value = is_wrapped(l) ? wrapped_value(l) : T_ERR;
    const char *outer_side = c->short_circuit_side;
    c->short_circuit_depth++;
    c->short_circuit_side = "on the right side of '?\?'";
    const type r = check_expr_want_any(c, e->rhs, value.kind == TY_VOID ? T_ERR : value);
    c->short_circuit_depth--;
    c->short_circuit_side = outer_side;
    if (l.kind == TY_ERROR || r.kind == TY_ERROR) return T_ERR;
    if (!is_wrapped(l)) {
        diag_error(e->at, "'?\?' falls back when a call fails or a T? is nothing, and %s is %s, which always has a value",
                   wrapped_what(e->lhs), type_name(l));
        return T_ERR;
    }
    if (value.kind == TY_VOID) {
        diag_error(e->at, "%s gives no value, so '?\?' has nothing to fall back from", wrapped_what(e->lhs));
        diag_note("carry on without it with '!', or handle its error with 'is %s'", pattern_example(wrapped_error(l), true));
        return T_ERR;
    }
    if (r.kind == TY_VOID) {
        diag_error(e->rhs->at, "this gives no value to fall back to");
        return T_ERR;
    }
    if (is_wrapped(r)) {
        if (same_type(wrapped_value(r), value)) return r; // a ?? b, where b can fail or be nothing too
    } else if (type_assignable(value, r)) {
        return value;
    } else if (type_assignable(r, value)) {
        return r; // int ?? 0.5 is a float
    }
    diag_error(e->rhs->at, "%s gives %s, so '?\?' falls back to %s too, not %s", wrapped_what(e->lhs), type_name(value),
               type_name(value), type_name(r));
    return T_ERR;
}

// What's after `is`: a type, or one of an enum's members. Returns the type
// (the enum's, for a member, which goes in `*member`), or T_ERR.
static type is_pattern_type(checker *c, const expr *e, const enum_member **member)
{
    *member = NULL;
    type t;
    const loc at = e->pattern_qual_at.line ? e->pattern_qual_at : e->pattern_at;
    if (builtin_type_named(e->pattern, &t)) return t;
    if (str_eq_c(e->pattern, "string")) return (type){TY_STRING, NULL};
    if (resolve_list_type(c, e->pattern, at, &t)) return t;
    if (resolve_map_type(c, e->pattern, at, &t)) return t;
    if (resolve_grid_type(c, e->pattern, at, &t)) {
        if (t.kind != TY_ERROR) diag_error(at, "'is' can't look for a grid");
        return T_ERR;
    }
    decl *d = find_type(c, e->pattern, at);
    if (d) return decl_type(d);
    str ns;
    str name;
    if (split_qualified(e->pattern, &ns, &name)) {
        decl *const enum_decl = find_type(c, ns, at);
        for (int i = 0; enum_decl && enum_decl->kind == DECL_ENUM && i < enum_decl->members.count; i++) {
            if (!str_eq(enum_decl->members.items[i].name, name)) continue;
            *member = &enum_decl->members.items[i];
            return decl_type(enum_decl);
        }
    }
    diag_error(e->pattern_at, "unknown type '" STR_FMT "'", STR_ARG(e->pattern));
    suggestion s = suggest_start(e->pattern);
    suggest_builtin_types(&s);
    suggest_structs(&s, c->prog);
    for (int i = 0; i < c->prog->decls.count; i++) {
        if (c->prog->decls.items[i]->kind == DECL_ENUM) suggest_consider(&s, c->prog->decls.items[i]->name);
    }
    suggest_note(&s);
    return T_ERR;
}

// x is int score, x is ParseError why, x is ParseError.Empty: whether a failable
// call's or T?'s result holds a value (or that error). A name after it is a
// local, in scope where the test is true: an if's or a loop's condition, joined
// by &&, holds it, so the code it guards can use it.
static type check_is(checker *c, expr *e)
{
    const type t = check_expr_any(c, e->lhs);
    const enum_member *member = NULL;
    const type pattern = is_pattern_type(c, e, &member);
    if (e->binding) {
        stmt *b = e->binding;
        b->type = member ? T_ERR : pattern;
        if (member) {
            diag_error(b->name_at, "'" STR_FMT "' is one error, so it takes no name", STR_ARG(e->pattern));
        } else if (!e->binding_ok) {
            diag_error(b->name_at, "a name after 'is' only goes in an if's or a loop's condition, joined by '&&', where "
                                   "the code it guards can use it");
            diag_note("test without the name, '... is " STR_FMT "', or unwrap the value with '?\?' or '!'", STR_ARG(e->pattern));
        }
        check_reserved(b->name, b->name_at);
        if (find_local(c, b->name) || find_param(c, b->name)) {
            diag_error(b->name_at, "'" STR_FMT "' is already declared", STR_ARG(b->name));
        }
        vec_push(c->locals, b);
    }
    if (t.kind == TY_ERROR || pattern.kind == TY_ERROR) return T_ERR;
    if (!is_wrapped(t)) {
        diag_error(e->at, "'is' unwraps a call that can fail or a T? value, and %s is %s", wrapped_what(e->lhs),
                   type_name(t));
        diag_note("a value's type is known where it's written, so Tide has no type tests");
        return T_ERR;
    }
    const type value = wrapped_value(t);
    const type error = t.kind == TY_FAILABLE ? wrapped_error(t) : T_VOID_;
    if (member) {
        if (t.kind == TY_OPTIONAL) {
            diag_error(e->pattern_at, "%s is %s or nothing, and has no error to compare", wrapped_what(e->lhs),
                       type_name(value));
            return T_ERR;
        }
        if (!same_type(pattern, error)) {
            diag_error(e->pattern_at, "%s fails with %s, not %s", wrapped_what(e->lhs), type_name(error), type_name(pattern));
            return T_ERR;
        }
        e->looks_for = IS_MEMBER;
        e->enum_member = member;
        e->type_decl = pattern.decl;
        return T_BOOL_;
    }
    if (value.kind != TY_VOID && same_type(pattern, value)) {
        e->looks_for = IS_VALUE;
    } else if (t.kind == TY_FAILABLE && same_type(pattern, error)) {
        e->looks_for = IS_ERROR;
    } else if (t.kind == TY_OPTIONAL) {
        diag_error(e->pattern_at, "%s is %s or nothing, so 'is' takes '%s', not %s", wrapped_what(e->lhs),
                   type_name(value), type_name(value), type_name(pattern));
        return T_ERR;
    } else if (value.kind == TY_VOID) {
        diag_error(e->pattern_at, "%s gives no value and fails with %s, so 'is' takes '%s', not %s", wrapped_what(e->lhs),
                   type_name(error), type_name(error), type_name(pattern));
        return T_ERR;
    } else {
        diag_error(e->pattern_at, "%s gives %s or fails with %s, so 'is' takes one of those, not %s", wrapped_what(e->lhs),
                   type_name(value), type_name(error), type_name(pattern));
        return T_ERR;
    }
    if (pattern.kind == TY_STRING || pattern.kind == TY_LIST || pattern.kind == TY_MAP || pattern.kind == TY_SET) c->prog->uses_text = true;
    return T_BOOL_;
}

// await call: waits for an async function's call to finish and gives its
// value, or for Wait.Ticks(n) and the like. Only async code waits.
static type check_await(checker *c, expr *e)
{
    expr *call = e->lhs;
    const expr *outer = c->awaiting;
    c->awaiting = call;
    const type t = check_expr_any(c, call);
    c->awaiting = outer;
    if (!async_code(c)) {
        diag_error(e->at, "'await' waits, and only async functions and handlers can");
        if (in_routine(c) && c->method->kind == DECL_FUNCTION) {
            diag_note("make it async: 'async " STR_FMT " " STR_FMT "(...)'", STR_ARG(c->method->return_type_name),
                      STR_ARG(c->method->name));
        } else if (!c->method && c->system && c->system->is_handler) {
            diag_note("make the handler async: 'async event(...) " STR_FMT "(...)'", STR_ARG(c->system->name));
        } else if (!c->method && c->system && c->system->kind == DECL_SYSTEM) {
            diag_note("a %s runs again every %s; to wait, start a task: call an async function without 'await'",
                      c->system->is_view ? "view" : "system", c->system->is_view ? "frame" : "tick");
        }
        return T_ERR;
    }
    if (c->block_depth > 0) {
        diag_error(e->at, "a task can't wait inside a block written after a call: the function runs it");
        return T_ERR;
    }
    if (c->grid_loops > 0) {
        diag_error(e->at, "a task can't wait inside a foreach over a grid, which goes through its cells as they are");
        diag_note("go through them with 'for' loops, which can wait: 'for (var y = 0; y < cells.size.y; y++) { ... }'");
        return T_ERR;
    }
    if (t.kind == TY_ERROR) return T_ERR;
    if (call->call == CALL_WAIT) return T_VOID_;
    if (call->storage_flush) return t;
    if ((call->kind == E_CALL || call->kind == E_METHOD) && call->call == CALL_FUNCTION && call->method->is_async) return t;
    diag_error(e->at, "'await' waits for an async function's call, or for Wait, like 'await Wait.Ticks(30)'");
    if ((call->kind == E_CALL || call->kind == E_METHOD) && call->call == CALL_FUNCTION) {
        diag_note("'" STR_FMT "' isn't async: it runs to its end as it's called, so there's nothing to wait for",
                  STR_ARG(call->method->name));
    }
    return T_ERR;
}

// An expression whose value is used: a failable call's or a T? has to be
// unwrapped first.
static type check_expr(checker *c, expr *e)
{
    const type t = check_expr_any(c, e);
    if (!is_wrapped(t)) return t;
    unwrap_error(e, t);
    unwrap_fix(c, e, t);
    return e->type = T_ERR; // Reported: what takes it doesn't again
}

// An expression of any type, failable calls' and T? values too: what `??`,
// `is`, `!`, `try` and `var` take.
static type check_expr_any(checker *c, expr *e)
{
    const bool whole_ok = c->device_whole_ok == e;
    c->device_whole_ok = NULL;
    type t = T_ERR;
    switch (e->kind) {
    case E_INT: t = e->is_long ? T_LONG_ : T_INT_; break;
    case E_FLOAT: t = T_FLOAT_; break;
    case E_BOOL: t = T_BOOL_; break;
    case E_STRING: t = (type){TY_STRING, NULL}; break;
    case E_NAME: t = check_name(c, e); break;
    case E_THIS: t = check_this(c, e); break;
    case E_MEMBER: t = check_member(c, e); break;
    case E_CALL: t = check_call(c, e); break;
    case E_METHOD: t = check_method(c, e); break;
    case E_LITERAL: t = check_literal(c, e); break;
    case E_BINARY: {
        const bool short_circuit = e->op == T_AND || e->op == T_OR;
        const bool compared = e->op == T_EQ || e->op == T_NE;
        // `found == null`: whether a T? is nothing, as in C#.
        expr *const null_side = e->lhs->kind == E_NULL ? e->lhs : e->rhs->kind == E_NULL ? e->rhs : NULL;
        if (null_side) {
            expr *other = null_side == e->lhs ? e->rhs : e->lhs;
            const type o = other->kind == E_NULL ? T_ERR : check_expr_any(c, other);
            null_side->type = o;
            if (other->kind == E_NULL) {
                diag_error(e->at, "nothing says which T? these nulls are");
            } else if (!compared) {
                diag_error(null_side->at, "'null' can't be used with operator %s", op_str(e->op));
                diag_note("a T? is compared with null, with == and !=");
            } else if (o.kind == TY_OPTIONAL) {
                t = T_BOOL_;
            } else if (o.kind == TY_FAILABLE) {
                diag_error(null_side->at, "%s can fail, but is never null", wrapped_what(other));
                diag_note("'is %s' is true when it fails", pattern_example(wrapped_error(o), true));
            } else {
                null_error(null_side, o);
            }
            break;
        }
        // `x == default`: it takes the other side's type. As in C#, it's only ever compared.
        expr *const dflt = e->lhs->kind == E_DEFAULT ? e->lhs : e->rhs->kind == E_DEFAULT ? e->rhs : NULL;
        if (dflt && !compared) {
            diag_error(dflt->at, "'default' can't be used with operator %s", op_str(e->op));
            diag_note("it's only compared, with == and !=; write the value itself here");
        }
        type l = e->lhs == dflt ? T_ERR : check_expr(c, e->lhs);
        const char *outer_side = c->short_circuit_side;
        if (short_circuit) {
            c->short_circuit_depth++;
            c->short_circuit_side = "on the right side of && or ||";
        }
        type r = e->rhs == dflt ? T_ERR : check_expr(c, e->rhs);
        if (short_circuit) {
            c->short_circuit_depth--;
            c->short_circuit_side = outer_side;
        }
        if (dflt == e->lhs && compared) l = check_default_value(c, dflt, r);
        else if (dflt && compared) r = check_default_value(c, dflt, l);
        if (e->op == T_PLUS && (l.kind == TY_STRING || r.kind == TY_STRING)) {
            check_text_context(c, l.kind == TY_STRING ? e->rhs : e->lhs);
        }
        t = binary_result(e->op, l, r, e->at, &e->method);
        note_operator_call(c, e->method, e);
        break;
    }
    case E_CONDITIONAL: t = check_conditional(c, e); break;
    case E_INTERP:
        t = (type){TY_STRING, NULL};
        for (int i = 0; i < e->args.count; i++) {
            expr *value = e->args.items[i];
            const type vt = check_expr(c, value);
            if (vt.kind == TY_VOID) {
                diag_error(value->at, "this doesn't produce a value to show in the text");
            } else if (!text_can_hold(vt)) {
                text_cant_show(value->at, vt);
            } else {
                check_text_context(c, value);
            }
            vec_push(e->format_codes, text_format(value, e->formats.items[i]));
        }
        break;
    case E_INDEX: t = check_index(c, e); break;
    case E_LIST:
        diag_error(e->at, "a list's type comes from where it goes: 'List<int> scores = [1, 2];'");
        for (int i = 0; i < e->args.count; i++) check_expr_want(c, e->args.items[i], T_ERR);
        break;
    case E_DEFAULT:
        diag_error(e->at, "'default' takes its type from where it goes, and nothing here says which");
        diag_note("use it where a value of one type goes, like 'float2 center = default;' or an argument");
        break;
    case E_NULL:
        diag_error(e->at, "'null' is the nothing of a T? value, and nothing here says which type");
        diag_note("use it where a T? goes, like 'int? best = null;', or 'return null;' in a function that returns 'int?'");
        break;
    case E_COALESCE: t = check_coalesce(c, e); break;
    case E_IS: t = check_is(c, e); break;
    case E_TRY: t = check_try(c, e); break;
    case E_DEFAULTED: t = check_defaulted(c, e); break;
    case E_AWAIT: t = check_await(c, e); break;
    case E_UNARY: {
        const type operand = check_expr(c, e->lhs);
        if (operand.kind == TY_ERROR) break;
        if (operand.kind == TY_STRUCT) {
            e->method = find_operator(e->op, operand, T_ERR, true, e->at);
            note_operator_call(c, e->method, e);
            if (e->method) {
                t = e->method->return_type;
            } else {
                diag_error(e->at, "operator %s can't be used with %s", op_str(e->op), type_name(operand));
                note_missing_operator(e->op, operand, T_ERR, true);
            }
        } else if (e->op == T_NOT && operand.kind == TY_BOOL) t = T_BOOL_;
        else if (e->op == T_TILDE && type_is_integer(operand)) t = operand.kind == TY_BYTE ? T_INT_ : operand;
        else if (e->op == T_MINUS && (type_is_numeric(operand) || operand.kind == TY_LONG || matrix_dim(operand))) t = operand.kind == TY_BYTE ? T_INT_ : operand;
        else diag_error(e->at, "operator %s can't be used with %s", op_str(e->op), type_name(operand));
        break;
    }
    }
    if (holds_render_resource(t) && !gpu_code(c)) {
        decl *code = reading_code(c);
        if (code) {
            if (!code->render_resources) code->render_resources_at = e->at;
            code->render_resources = true;
            if (!c->method && !local_code(c)) {
                diag_error(e->at, "rendering resources belong to this machine, so the match can't use them");
                t = T_ERR;
            }
        }
    }
    e->type = t;
    // Text and lists that code makes go in the scratch area.
    if ((t.kind == TY_STRING && e->kind != E_STRING) || t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET) c->prog->uses_text = true;
    if (!whole_ok) note_device_value(c, e, t);
    return t;
}

// ---------------------------------------------------------------------------
// Statements

// Finds the variable an assignment target writes through, or NULL if the
// target isn't something that can be assigned.
static expr *assign_root(expr *target)
{
    while (target->kind == E_MEMBER || target->kind == E_INDEX) target = target->object;
    return target->kind == E_NAME ? target : NULL;
}

// Whether `target` can be changed: by an assignment, by calling mut method
// `called` on it, or passed to `called`'s mut parameter `arg_of`. Reports why not.
static bool same_place(const expr *a, const expr *b);
static bool own_offset(const expr *e, const stmt *loop, int dims, int64_t out[3]);
static bool own_index(const expr *e, const stmt *loop, int64_t *out);

// In a parallel loop's step: whether it may write `target`, its own cell or
// block of the grid the loop goes over, its own element or block of the list,
// or a variable it declares itself.
static bool check_step_write(checker *c, const expr *target)
{
    const stmt *loop = c->parallel;
    if (target->kind == E_INDEX && target->object->type.kind == TY_LIST && same_place(target->object, loop->value)) {
        int64_t at = 0;
        if (own_index(target->lhs, loop, &at) && at >= 0 && at < loop->block[0]) return true;
        if (loop->by) {
            diag_error(target->at, "a parallel loop's step changes only its own block: elements from '" STR_FMT "' up to "
                                   "the block's size", STR_ARG(loop->name));
            diag_note("write each element as '" STR_FMT " + 1', with constants from 0 up to the block's size",
                      STR_ARG(loop->name));
        } else {
            diag_error(target->at, "a parallel loop's step changes only its own element: '" STR_FMT "'", STR_ARG(loop->name));
            diag_note("steps run at once and read the list as the loop found it; a 'for' loop goes in order");
        }
        return false;
    }
    if (target->kind == E_INDEX && target->object->type.kind == TY_GRID && same_place(target->object, loop->value)) {
        const int dims = target->object->type.decl->dims;
        int64_t at[3] = {0, 0, 0};
        bool own = own_offset(target->lhs, loop, dims, at);
        for (int i = 0; own && i < dims; i++) own = at[i] >= 0 && at[i] < loop->block[i];
        if (own) return true;
        if (loop->by) {
            diag_error(target->at, "a parallel loop's step changes only its own block: cells from '" STR_FMT "' up to "
                                   "the block's size", STR_ARG(loop->name));
            diag_note("write each cell as '" STR_FMT " + %s(...)', with constants from 0 up to the block's size",
                      STR_ARG(loop->name), dims == 2 ? "int2" : "int3");
        } else {
            diag_error(target->at, "a parallel loop's step changes only its own cell: '" STR_FMT "'", STR_ARG(loop->name));
            diag_note("steps run at once and read the grid as the loop found it; to move something, the cell it "
                      "goes to takes it, or a 'for' loop goes in order");
        }
        return false;
    }
    const expr *root = assign_root((expr *)target);
    if (root && root->bind == BIND_LOCAL) {
        for (int i = c->parallel_locals; i < c->locals.count; i++) {
            if (c->locals.items[i] == root->local) return true;
        }
    }
    diag_error(target->at, "a parallel loop's steps run at once, so each changes only its own place (a grid's cell "
                           "or block, a list's element or block), and the variables it declares");
    diag_note("work it out in the step, or in a 'for' loop, which goes in order");
    return false;
}

// In a Sort's comparison block: whether it may write `target`, a variable it
// declares itself. It runs again and again as the list sorts, so it changes
// nothing outside it.
static bool check_compare_write(checker *c, const expr *target)
{
    const expr *root = assign_root((expr *)target);
    if (root && root->bind == BIND_LOCAL) {
        for (int i = c->compare_locals; i < c->locals.count; i++) {
            if (c->locals.items[i] == root->local) return true;
        }
    }
    diag_error(target->at, "a comparison runs again and again as the list sorts, so it changes nothing outside it");
    diag_note("work it out before the sort, or in the variables the comparison declares");
    return false;
}

static bool check_writable(checker *c, expr *target, const decl *called, const param *arg_of)
{
    if (c->parallel && !check_step_write(c, target)) return false;
    if (c->compare && !check_compare_write(c, target)) return false;
    if (target->kind == E_THIS) {
        diag_error(target->at, "'this' is the entity the code runs for, so it can't change");
        if (called) diag_note("store it in a 'mut var' first");
        return false;
    }
    for (const expr *part = target; part->kind == E_MEMBER || part->kind == E_INDEX; part = part->object) {
        if (part->kind == E_INDEX && part->object->type.kind == TY_RECORD && part->object->type.decl->array_of) {
            // touches[i], even of a copy
            diag_error(target->at, "devices can only be read");
            diag_note("copy the touch into a 'mut var' to change it");
            return false;
        }
    }
    for (const expr *part = target;; part = part->object) {
        if (part->bind == BIND_CONST) {
            diag_error(target->at, "'" STR_FMT "' is a constant, so it can't change", STR_ARG(part->constant->name));
            if (called) diag_note("copy it into a 'mut var' first");
            return false;
        }
        if (part->kind != E_MEMBER && part->kind != E_INDEX) break;
    }
    expr *root = assign_root(target);
    if (root && root->bind == BIND_LOCAL && root->local->query) {
        diag_error(root->at, "query row '" STR_FMT "' is read-only", STR_ARG(root->name));
        if (arg_of) diag_note("'" STR_FMT "' changes its '" STR_FMT "'", STR_ARG(called->name), STR_ARG(arg_of->name));
        else if (called) diag_note("'" STR_FMT "' is a mut method: it changes what it's called on", STR_ARG(called->name));
        diag_note("copy a component binding into a 'mut var' to change the copy, or send an event to the row's entity");
        return false;
    }
    // Into a component or singleton: a write to the world's own memory. Text,
    // a list, or an element of one, is in its heap.
    const bool world_place = root && root->bind == BIND_PARAM && !root->param->function_param
                          && (root->param->type.kind == TY_COMPONENT || root->param->type.kind == TY_SINGLETON);
    bool in_list = false;
    for (const expr *e = target; e->kind == E_MEMBER || e->kind == E_INDEX; e = e->object) in_list |= e->kind == E_INDEX;
    const bool passed_holder = world_place && arg_of && target == root && decl_holds_text(root->param->type.decl);
    note_text_write(c, world_place && (holds_heap(target->type) || target->type.kind == TY_GRID || in_list || passed_holder));
    if (!root || root->bind == BIND_NONE || root->bind == BIND_TYPE || root->bind == BIND_NAMESPACE) {
        if (arg_of) {
            diag_error(target->at, "'" STR_FMT "' changes its '" STR_FMT "', so pass it a variable or field",
                       STR_ARG(called->name), STR_ARG(arg_of->name));
            diag_note("store the value in a 'mut var' first");
        } else if (called) {
            diag_error(target->at, "'" STR_FMT "' changes what it's called on, so that needs to be a variable or field",
                       STR_ARG(called->name));
            diag_note("store the value in a 'mut var' first");
        } else {
            diag_error(target->at, "can't assign to this expression");
        }
        return false;
    }
    if (root->bind == BIND_PARAM) root->param->written = true;
    if (root->bind == BIND_DEVICES) {
        diag_error(root->at, "devices can only be read");
        return false;
    }
    if (root->bind == BIND_PARAM && root->param->type.kind == TY_RECORD) {
        diag_error(root->at, "'" STR_FMT "' is read-only", STR_ARG(root->name));
        diag_note("devices can only be read");
        return false;
    }

    if (root->bind == BIND_PARAM && root->param->mode != PARAM_MUT) {
        diag_error(root->at, "'" STR_FMT "' is read-only", STR_ARG(root->name));
        if (arg_of) diag_note("'" STR_FMT "' changes its '" STR_FMT "'", STR_ARG(called->name), STR_ARG(arg_of->name));
        else if (called) diag_note("'" STR_FMT "' is a mut method: it changes what it's called on", STR_ARG(called->name));
        if (gpu_code(c) && root->param == &c->method->params.items[0]) {
            diag_note("a frag gets its vertex blended across the triangle, to read; a vert changes it: 'vert Name(mut "
                      STR_FMT " " STR_FMT ")'", STR_ARG(root->param->type_name), STR_ARG(root->name));
        } else if (gpu_code(c)) {
            diag_note("a %s reads what it's given: %s", shader_word(c->method),
                      c->method->is_frag ? "it writes its pixel's color, 'mut Color color'" : "it changes its vertex");
        } else if (root->param->function_param) {
            diag_note("declare the parameter as 'mut " STR_FMT " " STR_FMT "' to change the caller's variable, or copy "
                      "it into a 'mut var'", STR_ARG(root->param->type_name), STR_ARG(root->name));
            const fix f = {.kind = FIX_ADD_MUT, .at = root->at, .param = root->param};
            vec_push(c->prog->fixes, f);
        } else if (root->param->type.kind == TY_ENTITY) {
            diag_note("entity handles can't be reassigned");
        } else if (root->param->type.kind == TY_INPUT) {
            diag_note("input comes from the players; the simulation can only read it");
        } else if (root->param->type.kind == TY_EVENT) {
            diag_note("%s can't change once it's sent; copy it into a 'mut var' to send a changed one",
                      root->param->type.decl->is_command ? "a command" : "an event");
        } else if (root->param->mode == PARAM_SENDER) {
            diag_note("the connection supplies a command's sender; its handler can't change that identity");
        } else if (root->param->type.kind == TY_SINGLETON && root->param->type.decl->builtin) {
            diag_note("'" STR_FMT "' is managed by the engine", STR_ARG(root->param->type_name));
        } else if (in_view(c) && root->param->type.decl && !root->param->type.decl->is_local
                   && !through_local_field(target) && !(arg_of && target == root && root->param->type.decl->local_part)) {
            diag_note("a view can't change the match: put what it wants in the input, and change it in a system");
        } else {
            diag_note("declare the parameter as 'mut " STR_FMT " " STR_FMT "' to write to it",
                      STR_ARG(root->param->type_name), STR_ARG(root->name));
            const fix f = {.kind = FIX_ADD_MUT, .at = root->at, .param = root->param};
            vec_push(c->prog->fixes, f);
        }
        return false;
    }
    // A match component or singleton with local fields. Local code that takes
    // it as mut writes those, never the match's; a function says which it
    // writes, for the code that calls it (see check_local_field_calls).
    if (root->bind == BIND_PARAM && (root->param->type.kind == TY_COMPONENT || root->param->type.kind == TY_SINGLETON)
        && !root->param->type.decl->is_local && root->param->type.decl->local_part) {
        param *p = root->param;
        const bool local = through_local_field(target);
        const bool passed_on = arg_of && target == root; // To another function, whose parameter says what it writes
        if (local) {
            p->local_written = true;
        } else if (p->function_param) {
            if (!passed_on && !p->match_write) {
                p->match_write = true;
                p->match_write_at = target->at;
            }
        } else if (!c->method && is_local_code(c->system) && !passed_on) {
            const char *what = local_code_what(c->system);
            const field *changed = NULL; // The component's own field the place is in: `pos` in box.pos.x
            for (const expr *e = target; e->kind == E_MEMBER || e->kind == E_INDEX; e = e->object) {
                if (e->kind == E_MEMBER && e->object == root) changed = e->field;
            }
            if (changed) {
                diag_error(target->at, "%s can't change the match, and '" STR_FMT "' is a match field of '" STR_FMT "'", what,
                           STR_ARG(changed->name), STR_ARG(p->type.decl->name));
            } else {
                diag_error(target->at, "%s can't change the match, and this changes '" STR_FMT "'", what,
                           STR_ARG(p->type.decl->name));
            }
            diag_note("'mut " STR_FMT " " STR_FMT "' lets it write the local fields of '" STR_FMT "', like '" STR_FMT "'; put "
                      "what it wants of the match in the input, and change the match in a system",
                      STR_ARG(p->type_name), STR_ARG(p->name), STR_ARG(p->type.decl->name),
                      STR_ARG(p->type.decl->local_part->fields.items[0].name));
            return false;
        }
    }
    if (root->bind == BIND_LOCAL && !root->local->is_mut && root->local == c->loop_var) return true; // A for's step
    if (root->bind == BIND_LOCAL && !root->local->is_mut && root->local->loop_var) {
        diag_error(root->at, "'" STR_FMT "' is read-only here", STR_ARG(root->name));
        diag_note("the variable a for declares changes in its step; declare it 'mut var' to change it in the body too");
        const fix f = {.kind = FIX_MUT_LOCAL, .at = root->at, .local = root->local};
        vec_push(c->prog->fixes, f);
        return false;
    }
    if (root->bind == BIND_LOCAL && (root->local->kind == S_PARALLEL
                                     || (root->local->kind == S_FOREACH && root->local->value->type.kind == TY_GRID))) {
        diag_error(root->at, "'" STR_FMT "' is the step's place in the %s, so it can't change", STR_ARG(root->name),
                   root->local->value->type.kind == TY_LIST ? "list" : "grid");
        diag_note("copy it into a 'mut var' to work with another place");
        return false;
    }
    if (root->bind == BIND_LOCAL && root->local->kind == S_FOREACH) {
        diag_error(root->at, "'" STR_FMT "' is a copy of the list's element, so it can't be changed", STR_ARG(root->name));
        diag_note("change the list itself: 'for (var i = 0; i < items.count; i++) { ... items[i] = ...; }'");
        return false;
    }
    if (root->bind == BIND_LOCAL && !root->local->is_mut) {
        const stmt *local = root->local;
        diag_error(root->at, "'" STR_FMT "' is read-only", STR_ARG(root->name));
        if (arg_of) diag_note("'" STR_FMT "' changes its '" STR_FMT "'", STR_ARG(called->name), STR_ARG(arg_of->name));
        else if (called) diag_note("'" STR_FMT "' is a mut method: it changes what it's called on", STR_ARG(called->name));
        if (!local->value) { // x is int name
            diag_note("a name after 'is' is read-only; copy it into a 'mut var' to change it");
            return false;
        }
        if (local->type_name.len > 0) {
            diag_note("declare it as 'mut " STR_FMT " " STR_FMT " = ...' to change it", STR_ARG(local->type_name), STR_ARG(local->name));
        } else {
            diag_note("declare it as 'mut var " STR_FMT " = ...' to change it", STR_ARG(local->name));
        }
        const fix f = {.kind = FIX_MUT_LOCAL, .at = root->at, .local = local};
        vec_push(c->prog->fixes, f);
        return false;
    }
    if (root->bind == BIND_FIELD && c->method && !c->method->is_mut_method) {
        diag_error(root->at, "'" STR_FMT "' is read-only in '" STR_FMT "'", STR_ARG(root->name), STR_ARG(c->method->name));
        diag_note("declare the method as 'mut " STR_FMT " " STR_FMT "(...)' to change the fields",
                  STR_ARG(c->method->return_type_name), STR_ARG(c->method->name));
        const fix f = {.kind = FIX_MUT_METHOD, .at = root->at, .method = c->method};
        vec_push(c->prog->fixes, f);
        return false;
    }
    return true;
}

static bool fold_int(const expr *e, int64_t *out);
static bool fold_cell(const expr *e, int dims, int64_t out[3]);
static void check_stmt(checker *c, stmt *s);
static type local_type(checker *c, stmt *s);

// Whether `local` is declared somewhere in `s`.
static bool stmt_inside(const stmt *s, const stmt *local)
{
    if (!s) return false;
    if (s == local) return true;
    if (s->kind == S_BLOCK) {
        for (int i = 0; i < s->stmts.count; i++) {
            if (stmt_inside(s->stmts.items[i], local)) return true;
        }
        return false;
    }
    if (s->kind == S_SWITCH) {
        for (int i = 0; i < s->cases.count; i++) {
            for (int k = 0; k < s->cases.items[i].body.count; k++) {
                if (stmt_inside(s->cases.items[i].body.items[k], local)) return true;
            }
        }
        return false;
    }
    return stmt_inside(s->init, local) || stmt_inside(s->step, local) || stmt_inside(s->then_stmt, local)
        || stmt_inside(s->else_stmt, local);
}

// Whether two places are the same field: field.cells and field.cells.
static bool same_place(const expr *a, const expr *b)
{
    if (a->kind != b->kind) return false;
    if (a->kind == E_NAME) return str_eq(a->name, b->name) && a->bind == b->bind;
    if (a->kind == E_MEMBER) return str_eq(a->member, b->member) && same_place(a->object, b->object);
    if (a->kind == E_THIS) return true;
    return false;
}

// at.x, at.x + 1 or at.x - 1 (constants): how far from the step's place along `axis`.
static bool own_component(const expr *e, const stmt *loop, const int axis, int64_t *out)
{
    if (e->kind == E_MEMBER && e->object->kind == E_NAME && e->object->bind == BIND_LOCAL && e->object->local == loop) {
        if (e->member.len != 1 || e->member.ptr[0] != "xyz"[axis]) return false;
        *out = 0;
        return true;
    }
    int64_t k;
    if (e->kind == E_BINARY && (e->op == T_PLUS || e->op == T_MINUS) && own_component(e->lhs, loop, axis, out)
        && fold_int(e->rhs, &k)) {
        *out += e->op == T_PLUS ? k : -k;
        return true;
    }
    if (e->kind == E_BINARY && e->op == T_PLUS && fold_int(e->lhs, &k) && own_component(e->rhs, loop, axis, out)) {
        *out += k;
        return true;
    }
    return false;
}

// How far a cell's index is from the step's place: at, at + int2(1, 0),
// int2(at.x + 1, at.y), cells[at.x, at.y + 1]. False when it isn't that.
static bool own_offset(const expr *e, const stmt *loop, const int dims, int64_t out[3])
{
    if (e->kind == E_NAME && e->bind == BIND_LOCAL && e->local == loop) {
        out[0] = out[1] = out[2] = 0;
        return true;
    }
    int64_t k[3] = {0, 0, 0};
    if (e->kind == E_BINARY && (e->op == T_PLUS || e->op == T_MINUS) && own_offset(e->lhs, loop, dims, out)
        && fold_cell(e->rhs, dims, k)) {
        for (int i = 0; i < dims; i++) out[i] += e->op == T_PLUS ? k[i] : -k[i];
        return true;
    }
    if (e->kind == E_BINARY && e->op == T_PLUS && fold_cell(e->lhs, dims, k) && own_offset(e->rhs, loop, dims, out)) {
        for (int i = 0; i < dims; i++) out[i] += k[i];
        return true;
    }
    if (e->kind == E_CALL && e->call == CALL_CONSTRUCT && e->ctor == CTOR_COMPONENTS && e->args.count == dims) {
        out[2] = 0;
        for (int i = 0; i < dims; i++) {
            if (!own_component(e->args.items[i], loop, i, &out[i])) return false;
        }
        return true;
    }
    return false;
}

// How far an index is from a parallel loop's step's place in a list: i,
// i + 1, 1 + i, i - 1 (constants). False when it isn't that.
static bool own_index(const expr *e, const stmt *loop, int64_t *out)
{
    if (e->kind == E_NAME && e->bind == BIND_LOCAL && e->local == loop) {
        *out = 0;
        return true;
    }
    int64_t k;
    if (e->kind == E_BINARY && (e->op == T_PLUS || e->op == T_MINUS) && own_index(e->lhs, loop, out) && fold_int(e->rhs, &k)) {
        *out += e->op == T_PLUS ? k : -k;
        return true;
    }
    if (e->kind == E_BINARY && e->op == T_PLUS && fold_int(e->lhs, &k) && own_index(e->rhs, loop, out)) {
        *out += k;
        return true;
    }
    return false;
}

static bool own_cell_only_expr(const expr *e, const stmt *loop);

// Whether a foreach over a grid only reads and writes its own cell, and
// changes nothing else, so its steps could run in any order, at once: its
// variables, and nothing that leaves the loop or reaches outside.
static bool own_cell_only(const stmt *s, const stmt *loop, const int depth)
{
    if (!s) return true;
    switch (s->kind) {
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) {
            if (!own_cell_only(s->stmts.items[i], loop, depth)) return false;
        }
        return true;
    case S_IF:
        return own_cell_only_expr(s->cond, loop) && own_cell_only(s->then_stmt, loop, depth)
            && own_cell_only(s->else_stmt, loop, depth);
    case S_WHILE:
    case S_FOR:
        return own_cell_only(s->init, loop, depth + 1) && (!s->cond || own_cell_only_expr(s->cond, loop))
            && own_cell_only(s->step, loop, depth + 1) && own_cell_only(s->then_stmt, loop, depth + 1);
    case S_FOREACH:
    case S_PARALLEL:
        return false; // Another loop over a grid: in order
    case S_SWITCH:
        if (!own_cell_only_expr(s->cond, loop)) return false;
        for (int i = 0; i < s->cases.count; i++) {
            for (int k = 0; k < s->cases.items[i].body.count; k++) {
                if (!own_cell_only(s->cases.items[i].body.items[k], loop, depth + 1)) return false;
            }
        }
        return true;
    case S_VAR:
        return !s->value || own_cell_only_expr(s->value, loop);
    case S_EXPR:
        return own_cell_only_expr(s->value, loop);
    case S_ASSIGN: {
        const expr *t = s->target;
        if (t->kind == E_INDEX && t->object->type.kind == TY_GRID && same_place(t->object, loop->value)) {
            int64_t at[3];
            if (!own_offset(t->lhs, loop, t->object->type.decl->dims, at) || at[0] || at[1] || at[2]) return false;
            return own_cell_only_expr(t->lhs, loop) && own_cell_only_expr(s->value, loop);
        }
        // Only its own variables: a total or a flag outside it goes in order
        const expr *root = t;
        while (root->kind == E_MEMBER || root->kind == E_INDEX) root = root->object;
        if (root->kind != E_NAME || root->bind != BIND_LOCAL || !stmt_inside(loop->then_stmt, root->local)) return false;
        return own_cell_only_expr(t, loop) && own_cell_only_expr(s->value, loop);
    }
    case S_CONTINUE:
        return true;
    case S_BREAK:
        return depth > 0;
    case S_RETURN:
    case S_FAIL:
        return false;
    }
    return false;
}

// Whether `e` is in a variable the loop's step declares: what it changes
// through a call stays in the step.
static bool step_own(const expr *e, const stmt *loop)
{
    while (e->kind == E_MEMBER || e->kind == E_INDEX) e = e->object;
    return e->kind == E_NAME && e->bind == BIND_LOCAL && stmt_inside(loop->then_stmt, e->local);
}

static bool own_cell_only_expr(const expr *e, const stmt *loop)
{
    if (!e) return true;
    if (e->io_operation || e->storage_flush) return false;
    if (e->resource_operation) return false;
    switch (e->call) {
    case CALL_NONE: case CALL_BUILTIN: case CALL_CONSTRUCT: case CALL_TEXT:
        break;
    case CALL_FUNCTION:
        if (e->method && (e->method->calls_c || e->method->is_async || e->method->draws)) return false;
        // What it changes, its mut arguments and the value a mut method is called on, is the step's own
        for (int i = 0; e->method && i < e->args.count && i < e->method->params.count; i++) {
            if (e->method->params.items[i].mode == PARAM_MUT && !step_own(e->args.items[i], loop)) return false;
        }
        if (e->method && e->method->is_mut_method && (!e->object || !step_own(e->object, loop))) return false;
        break;
    default:
        return false; // Spawns, sends, draws, the GUI, list and grid changes, C: in order
    }
    if (e->kind == E_AWAIT || e->kind == E_TRY || e->type.kind == TY_DRAW_LIST) return false;
    if (e->kind == E_INDEX && e->object->type.kind == TY_GRID && same_place(e->object, loop->value)) {
        int64_t at[3];
        if (!own_offset(e->lhs, loop, e->object->type.decl->dims, at) || at[0] || at[1] || at[2]) return false;
    }
    if (e->block) return false;
    if (!own_cell_only_expr(e->object, loop) || !own_cell_only_expr(e->lhs, loop) || !own_cell_only_expr(e->rhs, loop)
        || !own_cell_only_expr(e->cond, loop)) {
        return false;
    }
    for (int i = 0; i < e->args.count; i++) {
        if (!own_cell_only_expr(e->args.items[i], loop)) return false;
    }
    for (int i = 0; i < e->inits.count; i++) {
        if (!own_cell_only_expr(e->inits.items[i].value, loop)) return false;
    }
    return true;
}

// How far from its place a step's window reaches, at most, along each axis.
#define WINDOW_REACH 64

static void window_stmt(stmt *s, stmt *loop);

// The cells of the loop's grid a step reads or writes at its place plus
// constants, which go through its window, and how far that window reaches.
static void window_expr(expr *e, stmt *loop)
{
    if (!e) return;
    if (e->kind == E_INDEX && e->object->type.kind == TY_GRID && same_place(e->object, loop->value)) {
        const int dims = e->object->type.decl->dims;
        int64_t at[3] = {0, 0, 0};
        bool near = own_offset(e->lhs, loop, dims, at);
        for (int i = 0; near && i < dims; i++) near = at[i] >= -WINDOW_REACH && at[i] <= WINDOW_REACH;
        if (near) {
            e->window = true;
            for (int i = 0; i < 3; i++) {
                e->window_offset[i] = (int)at[i];
                if (at[i] < loop->window_lo[i]) loop->window_lo[i] = (int)at[i];
                if (at[i] > loop->window_hi[i]) loop->window_hi[i] = (int)at[i];
            }
        }
    }
    window_expr(e->object, loop);
    window_expr(e->lhs, loop);
    window_expr(e->rhs, loop);
    window_expr(e->cond, loop);
    for (int i = 0; i < e->args.count; i++) window_expr(e->args.items[i], loop);
    for (int i = 0; i < e->inits.count; i++) window_expr(e->inits.items[i].value, loop);
    window_stmt(e->block, loop);
}

static void window_stmt(stmt *s, stmt *loop)
{
    if (!s) return;
    for (int i = 0; i < s->stmts.count; i++) window_stmt(s->stmts.items[i], loop);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++) window_expr(s->cases.items[i].labels.items[k], loop);
        for (int k = 0; k < s->cases.items[i].body.count; k++) window_stmt(s->cases.items[i].body.items[k], loop);
    }
    window_expr(s->cond, loop);
    window_expr(s->value, loop);
    window_expr(s->target, loop);
    if (s->kind == S_ASSIGN && s->target->kind == E_INDEX && s->target->object->type.kind == TY_LIST
        && same_place(s->target->object, loop->value)) { // Its own element, in its task's copy
        int64_t at = 0;
        if (own_index(s->target->lhs, loop, &at)) {
            s->target->window = true;
            s->target->window_offset[0] = (int)at;
        } else {
            loop->windowed = false; // The checker said why already
        }
    }
    if (s->kind == S_ASSIGN && s->target->kind == E_INDEX && s->target->object->type.kind == TY_GRID
        && same_place(s->target->object, loop->value) && !s->target->window) {
        loop->windowed = false; // A step only writes its own cells, but a write the window can't take goes the slow way
    }
    window_stmt(s->init, loop);
    window_stmt(s->step, loop);
    window_stmt(s->then_stmt, loop);
    window_stmt(s->else_stmt, loop);
}

// A parallel loop's steps go through a window when they can: its block, and
// the cells around it that they read at their place plus constants.
static void plan_window(stmt *loop)
{
    if (loop->value->type.kind == TY_LIST) { // Its steps write their own elements into their task's copy
        loop->windowed = true;
        window_stmt(loop->then_stmt, loop);
        return;
    }
    if (loop->value->type.kind != TY_GRID) return;
    loop->windowed = true;
    for (int i = 0; i < 3; i++) {
        loop->window_lo[i] = 0;
        loop->window_hi[i] = loop->block[i] - 1;
    }
    window_stmt(loop->then_stmt, loop);
}

// What a parallel loop's step can't do, which runs at once with the others:
// end the loop, wait, or do anything whose order counts.
static void check_step_stmt(checker *c, const stmt *s, int depth);

static void check_step_expr(checker *c, const expr *e)
{
    if (!e) return;
    const char *what = e->io_operation ? "access the filesystem" : NULL;
    switch (e->call) {
    case CALL_SPAWN: what = "spawn"; break;
    case CALL_ADD: case CALL_REMOVE: case CALL_DESTROY: what = "add, remove or destroy"; break;
    case CALL_SEND: what = "send events"; break;
    case CALL_DRAW: what = "draw"; break;
    case CALL_GUI: what = gui_call_what(e); break;
    case CALL_SESSION: case CALL_CLIPBOARD: what = "make requests of the host"; break;
    case CALL_LOAD: case CALL_UNLOAD: case CALL_SCENE_PLAYER: what = "load and unload scenes"; break;
    case CALL_SNAP: what = "snap"; break;
    default: break;
    }
    if (e->kind == E_AWAIT) what = "wait";
    if (e->resource_operation) what = "change rendering resources";
    if (e->type.kind == TY_DRAW_LIST) what = "draw";
    if (e->call == CALL_FUNCTION && e->method && e->method->is_async) what = "start tasks";
    if (e->call == CALL_GRID && same_place(e->object, c->parallel->value)) what = "change the whole grid";
    if (what) {
        diag_error(e->at, "a parallel loop's steps run at once, so they can't %s", what);
        diag_note("do it before or after the loop, or in a 'for' loop, which goes in order");
        return;
    }
    check_step_expr(c, e->object);
    check_step_expr(c, e->lhs);
    check_step_expr(c, e->rhs);
    check_step_expr(c, e->cond);
    for (int i = 0; i < e->args.count; i++) check_step_expr(c, e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) check_step_expr(c, e->inits.items[i].value);
    if (e->block) check_step_stmt(c, e->block, 0);
}

static void check_step_stmt(checker *c, const stmt *s, const int depth)
{
    if (!s) return;
    switch (s->kind) {
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) check_step_stmt(c, s->stmts.items[i], depth);
        break;
    case S_IF:
        check_step_expr(c, s->cond);
        check_step_stmt(c, s->then_stmt, depth);
        check_step_stmt(c, s->else_stmt, depth);
        break;
    case S_WHILE:
    case S_FOR:
    case S_FOREACH:
        check_step_stmt(c, s->init, depth);
        check_step_expr(c, s->cond);
        check_step_expr(c, s->value);
        check_step_stmt(c, s->step, depth);
        check_step_stmt(c, s->then_stmt, depth + 1);
        break;
    case S_PARALLEL:
        diag_error(s->at, "a parallel loop's step can't hold another parallel loop");
        diag_note("its steps already run at once: write the inner one as a 'for' or 'foreach'");
        break;
    case S_SWITCH:
        check_step_expr(c, s->cond);
        for (int i = 0; i < s->cases.count; i++) {
            for (int k = 0; k < s->cases.items[i].body.count; k++) check_step_stmt(c, s->cases.items[i].body.items[k], depth + 1);
        }
        break;
    case S_VAR:
    case S_EXPR:
        check_step_expr(c, s->value);
        break;
    case S_ASSIGN:
        check_step_expr(c, s->target);
        check_step_expr(c, s->value);
        break;
    case S_BREAK:
        if (depth == 0) {
            diag_error(s->at, "a parallel loop's steps run at once, so one can't end the loop");
            diag_note("'continue' ends this step; a 'for' loop can stop part way");
        }
        break;
    case S_RETURN:
    case S_FAIL:
        diag_error(s->at, "a parallel loop's step can't return: its steps run at once");
        diag_note("'continue' ends this step");
        break;
    case S_CONTINUE:
        break;
    }
}

// parallel (var at in cells) { ... }, or by blocks: parallel (var at in cells by 2 offset shift)
static void check_parallel(checker *c, stmt *s)
{
    const type grid = check_expr(c, s->value);
    const bool list = grid.kind == TY_LIST; // Its places are its elements' indices
    if (grid.kind != TY_GRID && !list && grid.kind != TY_ERROR) {
        diag_error(s->value->at, "parallel goes through a grid's cells or a list's elements, and this is %s", type_name(grid));
    }
    const int dims = grid.kind == TY_GRID ? grid.decl->dims : list ? 1 : 2;
    const type position = list ? (type){TY_INT, NULL} : dims == 2 ? (type){TY_INT2, NULL} : (type){TY_INT3, NULL};
    for (int i = 0; i < 3; i++) s->block[i] = 1;
    if (grid.kind == TY_GRID || list) check_writable(c, s->value, NULL, NULL); // It changes the grid or list
    if (list && holds_text(list_element(grid))) {
        diag_error(s->value->at, "a parallel loop goes through a list of plain values, and this one's elements hold text");
        diag_note("steps run on threads, where text can't be made: keep a number for it in the list, or use a 'for' loop");
    }
    const expr *root = s->value;
    while (root->kind == E_MEMBER) root = root->object;
    if (list && root->kind == E_NAME && root->bind == BIND_LOCAL) {
        diag_error(s->value->at, "a parallel loop goes through a list the world keeps: a component's or a singleton's, "
                                 "and '" STR_FMT "' is a variable", STR_ARG(root->name));
        diag_note("a variable's list lives on the thread that made it; keep the list in a component or singleton, or "
                  "use a 'for' loop");
    }
    if (s->by) {
        const type t = check_expr(c, s->by);
        int64_t size[3] = {0, 0, 0};
        bool known = false;
        if (t.kind == TY_INT || t.kind == TY_BYTE) {
            int64_t v;
            known = fold_int(s->by, &v);
            for (int i = 0; i < dims; i++) size[i] = v;
        } else if (t.kind == position.kind) {
            known = fold_cell(s->by, dims, size);
        }
        if (t.kind != TY_ERROR && !known) {
            if (list) diag_error(s->by->at, "a parallel loop's blocks are a size known while compiling: an int");
            else diag_error(s->by->at, "a parallel loop's blocks are a size known while compiling: an int, or %s",
                            dims == 2 ? "an int2" : "an int3");
            diag_note(list ? "like 'by 2'" : "like 'by 2' or 'by int2(2, 1)'");
        }
        for (int i = 0; known && i < dims; i++) {
            if (size[i] < 1 || size[i] > 64) {
                if (list) diag_error(s->by->at, "a block is 1 to 64 elements, not %lld", (long long)size[i]);
                else diag_error(s->by->at, "a block is 1 to 64 cells along each axis, not %lld", (long long)size[i]);
                break;
            }
            s->block[i] = (int)size[i];
        }
    }
    if (s->offset) {
        const type t = check_expr(c, s->offset);
        if (!type_assignable(T_INT_, t) && t.kind != position.kind) {
            if (list) diag_error(s->offset->at, "a parallel loop's offset is where its blocks start: an int, not %s", type_name(t));
            else diag_error(s->offset->at, "a parallel loop's offset is where its blocks start: an int, or %s, not %s",
                            dims == 2 ? "an int2" : "an int3", type_name(t));
        }
    }
    if (s->type_name.len > 0) {
        const type declared = local_type(c, s);
        if (declared.kind != TY_ERROR && !same_type(declared, position)) {
            diag_error(s->type_at, "a %s's places are %s, not %s", list ? "list" : "grid", type_name(position),
                       type_name(declared));
            if (list) diag_note("a parallel loop over a list goes through its indices: read the element as 'items[i]'");
        }
    }
    s->type = position;
    s->parallel = true;
    if (c->parallel) {
        diag_error(s->at, "a parallel loop's step can't hold another parallel loop");
        return;
    }
    if (in_routine(c) || async_code(c) || c->in_input) {
        diag_error(s->at, "a parallel loop goes in a system, view or handler%s", async_code(c) ? " that isn't async" : "");
        diag_note("its steps run on threads, as a system's do");
    }
    check_reserved(s->name, s->name_at);
    if (find_local(c, s->name) || find_param(c, s->name)) {
        diag_error(s->name_at, "'" STR_FMT "' is already declared", STR_ARG(s->name));
    }
    push_scope(c);
    vec_push(c->locals, s);
    const int switches = c->switch_depth;
    const int loops = c->loop_depth;
    c->switch_depth = 0;
    c->loop_depth = 1; // `continue` ends the step
    c->parallel = s;
    c->parallel_locals = c->locals.count;
    push_scope(c);
    check_stmt(c, s->then_stmt);
    pop_scope(c);
    check_step_stmt(c, s->then_stmt, 0);
    plan_window(s);
    c->parallel = NULL;
    c->loop_depth = loops;
    c->switch_depth = switches;
    pop_scope(c);
}

static void check_assign(checker *c, const stmt *s)
{
    // What an assignment writes through isn't read by it: the x of `x = 1`, `x.y += 2` and `x[i]++`
    const expr *written = s->target;
    while (written->kind == E_MEMBER || written->kind == E_INDEX) written = written->object;
    const expr *outer_root = c->write_root;
    c->write_root = written->kind == E_NAME ? written : NULL;
    const type target = check_expr_any(c, s->target); // A T? local, or one that holds a failable call's result
    c->write_root = outer_root;
    const type value = check_expr_want(c, s->value, target);
    if (target.kind == TY_ERROR) return;
    // world.Get<Health>(e).value = 5: a copy of the entity's component
    const expr *got = s->target;
    while (got->kind == E_MEMBER || got->kind == E_INDEX) got = got->object;
    if (got != s->target && got->kind == E_METHOD && got->call == CALL_WORLD) {
        const str world = got->object->name;
        const str component = got->type_decl->name;
        diag_error(s->target->at, "Get gives a copy of the entity's component, so it can't be changed where it is");
        diag_note("take it out, change it, and put it back: 'mut var c = " STR_FMT ".Get<" STR_FMT ">(e); c.value = 5; "
                  STR_FMT ".Set<" STR_FMT ">(e, c);'", STR_ARG(world), STR_ARG(component), STR_ARG(world), STR_ARG(component));
        return;
    }
    if (s->target->kind != E_INDEX && through_element(s->target)) {
        const expr *at = s->target;
        while (at->kind != E_INDEX) at = at->object;
        if (at->object->type.kind == TY_GRID) {
            diag_error(s->target->at, "a grid's cell is a copy, so it can't be changed where it is");
            diag_note("take it out, change it, and put it back: 'var c = cells[p]; c.x = 5; cells[p] = c;'");
        } else {
            diag_error(s->target->at, "a list's element is a copy, so it can't be changed where it is");
            diag_note("take it out, change it, and put it back: 'var e = items[i]; e.x = 5; items[i] = e;'");
        }
        return;
    }
    if (target.kind == TY_GRID && value.kind != TY_ERROR && !is_new_grid(s->value)) {
        diag_error(s->value->at, "a grid can't be copied: every chunk would be");
        diag_note("make a new, empty one with 'Grid%d(...)', or change cells one by one", target.decl->dims);
        return;
    }
    if (holds_grid(target) && target.kind != TY_GRID && value.kind != TY_ERROR && s->value->kind != E_LITERAL) {
        diag_error(s->value->at, "'" STR_FMT "' has a grid in it, which can't be copied", STR_ARG(target.decl->name));
        diag_note("set its other fields one by one, or give it a new value like '" STR_FMT " { ... }'", STR_ARG(target.decl->name));
        return;
    }
    if (!check_writable(c, s->target, NULL, NULL)) return;

    // Swizzles can be written (v.xz = ...) as long as no component repeats, but
    // only as the last step: in v.xy.x the swizzle is a temporary copy.
    for (const expr *m = s->target->object; s->target->kind == E_MEMBER && m->kind == E_MEMBER; m = m->object) {
        if (m->swizzle_len > 1) {
            diag_error(m->at, "can't assign through the swizzle '" STR_FMT "'", STR_ARG(m->member));
            diag_note("assign the components directly instead");
            return;
        }
    }
    if (s->target->kind == E_MEMBER && s->target->swizzle_len > 1) {
        for (int i = 0; i < s->target->swizzle_len; i++) {
            for (int j = 0; j < i; j++) {
                if (s->target->swizzle[i] == s->target->swizzle[j]) {
                    diag_error(s->target->at, "'" STR_FMT "' repeats a component, so it can't be assigned",
                               STR_ARG(s->target->member));
                    return;
                }
            }
        }
    }

    if ((s->op == T_PLUS_PLUS || s->op == T_MINUS_MINUS) && !type_is_integer(target) && target.kind != TY_FLOAT) {
        diag_error(s->at, "'%s' works on ints, longs and floats, not %s", s->op == T_PLUS_PLUS ? "++" : "--", type_name(target));
        diag_note("write '%s= 1' instead", s->op == T_PLUS_PLUS ? "+" : "-");
        return;
    }
    type result = value;
    if (s->op != T_ASSIGN) {
        if (compound_op(s->op) == T_PLUS && target.kind == TY_STRING) check_text_context(c, s->value);
        result = binary_result(compound_op(s->op), target, value, s->at, &((stmt *)s)->operator_decl);
        note_operator_call(c, s->operator_decl, s->value);
        if (result.kind == TY_ERROR) return;
    }
    // Compound byte assignments explicitly narrow the promoted integer result.
    if (!type_assignable(target, result) && !(target.kind == TY_BYTE && s->op != T_ASSIGN && type_is_integer(result))) {
        diag_error(s->value->at, "can't assign %s to %s", type_name(result), type_name(target));
        note_conversion(target, result);
    }
}

static void check_stmt(checker *c, stmt *s);

static bool fold_bool(const expr *e, bool *out);
static bool fold_int(const expr *e, int64_t *out);

// Card(2) while compiling: the number, in as many bits as the enum is stored
// in. Going by what it says it's stored as, as an enum's member can be worked
// out from one declared after it, which isn't checked yet.
static bool fold_enum(const expr *e, int64_t *out)
{
    int64_t number;
    if (e->args.count != 1 || !fold_int(e->args.items[0], &number)) return false;
    const str backing = e->type_decl->backing;
    *out = str_eq_c(backing, "byte") ? (uint8_t)number : str_eq_c(backing, "ushort") ? (uint16_t)number : (int32_t)number;
    return true;
}

// An int known while compiling, from a checked expression: a literal, an
// enum's member, constants, integer conversions, conditionals and operators
// on them, which give what they would at run time (tide/math.h). A case's
// value, an enum member's, and a Stage offset.
static bool fold_int(const expr *e, int64_t *out)
{
    int64_t l;
    int64_t r;
    switch (e->kind) {
    case E_INT:
        *out = e->int_value; // Within int's range unless it's a long
        return true;
    case E_DEFAULT:
        if (!type_is_integer(e->type) && e->type.kind != TY_ENUM) return false;
        *out = 0;
        return true;
    case E_NAME:
    case E_MEMBER:
        if (e->bind == BIND_CONST) {
            const decl *k = e->constant;
            return k->index == 2 && (type_is_integer(k->return_type) || k->return_type.kind == TY_ENUM)
                && fold_int(k->value, out);
        }
        if (e->kind == E_MEMBER && e->enum_member) {
            *out = e->enum_member->number;
            return true;
        }
        return false;
    case E_CONDITIONAL: {
        if (!type_is_integer(e->type) && e->type.kind != TY_ENUM) return false;
        bool cond;
        return fold_bool(e->cond, &cond) && fold_int(cond ? e->lhs : e->rhs, out);
    }
    case E_CALL:
        if (e->call == CALL_CONSTRUCT && e->ctor == CTOR_ENUM) return fold_enum(e, out);
        if (e->call != CALL_CONSTRUCT || e->ctor != CTOR_SCALAR || !type_is_integer(e->type)
            || e->args.count != 1 || !fold_int(e->args.items[0], &l)) return false;
        *out = e->type.kind == TY_LONG ? l : e->type.kind == TY_BYTE ? (uint8_t)l : tide_i_from_l(l);
        return true;
    case E_METHOD: {
        if (e->call == CALL_CONSTRUCT && e->ctor == CTOR_ENUM) return fold_enum(e, out); // Combat.Stance(2)
        if (e->call != CALL_BUILTIN || !type_is_integer(e->type) || e->args.count < 1 || e->args.count > 3) return false;
        int64_t args[3];
        for (int i = 0; i < e->args.count; i++) {
            if (!fold_int(e->args.items[i], &args[i])) return false;
        }
        l = args[0];
        const bool wide = e->type.kind == TY_LONG;
        if (str_eq_c(e->name, "Abs")) *out = wide ? tide_abs_l(l) : tide_abs_i((int32_t)l);
        else if (str_eq_c(e->name, "Sign")) *out = wide ? tide_sign_l(l) : tide_sign_i((int32_t)l);
        else if (str_eq_c(e->name, "Min")) *out = wide ? tide_min_l(l, args[1]) : tide_min_i((int32_t)l, (int32_t)args[1]);
        else if (str_eq_c(e->name, "Max")) *out = wide ? tide_max_l(l, args[1]) : tide_max_i((int32_t)l, (int32_t)args[1]);
        else if (str_eq_c(e->name, "Clamp")) {
            *out = wide ? tide_clamp_l(l, args[1], args[2]) : tide_clamp_i((int32_t)l, (int32_t)args[1], (int32_t)args[2]);
        } else return false;
        return true;
    }
    case E_UNARY:
        if (e->method || !type_is_integer(e->type) || !fold_int(e->lhs, &l)) return false;
        if (e->type.kind == TY_LONG) {
            if (e->op == T_MINUS) *out = (int64_t)(0ull - (uint64_t)l);
            else if (e->op == T_TILDE) *out = ~l;
            else return false;
            return true;
        }
        if (e->op == T_MINUS) *out = (int32_t)(0u - (uint32_t)l);
        else if (e->op == T_TILDE) *out = ~(int32_t)l;
        else return false;
        return true;
    case E_BINARY: {
        if (e->method || !type_is_integer(e->type) || !fold_int(e->lhs, &l) || !fold_int(e->rhs, &r)) return false;
        if (e->type.kind == TY_LONG) { // As tide/math.h's _l helpers work them out
            switch (e->op) {
            case T_PLUS: *out = (int64_t)((uint64_t)l + (uint64_t)r); return true;
            case T_MINUS: *out = (int64_t)((uint64_t)l - (uint64_t)r); return true;
            case T_STAR: *out = (int64_t)((uint64_t)l * (uint64_t)r); return true;
            case T_SLASH: *out = r == 0 ? 0 : r == -1 ? (int64_t)(0ull - (uint64_t)l) : l / r; return true;
            case T_PERCENT: *out = r == 0 || r == -1 ? 0 : l % r; return true;
            case T_SHL: *out = (int64_t)((uint64_t)l << (r & 63)); return true;
            case T_SHR: *out = l < 0 ? ~(~l >> (r & 63)) : l >> (r & 63); return true;
            case T_AMP: *out = l & r; return true;
            case T_PIPE: *out = l | r; return true;
            case T_CARET: *out = l ^ r; return true;
            default: return false;
            }
        }
        const int32_t a = (int32_t)l;
        const int32_t b = (int32_t)r;
        switch (e->op) {
        case T_PLUS: *out = (int32_t)((uint32_t)a + (uint32_t)b); return true;
        case T_MINUS: *out = (int32_t)((uint32_t)a - (uint32_t)b); return true;
        case T_STAR: *out = (int32_t)((uint32_t)a * (uint32_t)b); return true;
        case T_SLASH: *out = b == 0 ? 0 : b == -1 ? (int32_t)(0u - (uint32_t)a) : a / b; return true;
        case T_PERCENT: *out = b == 0 || b == -1 ? 0 : a % b; return true;
        case T_SHL: *out = (int32_t)((uint32_t)a << (b & 31)); return true;
        case T_SHR: *out = a < 0 ? ~(~a >> (b & 31)) : a >> (b & 31); return true;
        case T_AMP: *out = a & b; return true;
        case T_PIPE: *out = a | b; return true;
        case T_CARET: *out = a ^ b; return true;
        default: return false;
        }
    }
    default:
        return false;
    }
}

// Conditions of compile-time integer choices: bool constants, comparisons
// of known integers and bools, and the same short-circuit logic as at runtime.
static bool fold_bool(const expr *e, bool *out)
{
    if (e->kind == E_BOOL || (e->kind == E_DEFAULT && e->type.kind == TY_BOOL)) {
        *out = e->kind == E_BOOL && e->bool_value;
        return true;
    }
    if ((e->kind == E_NAME || e->kind == E_MEMBER) && e->bind == BIND_CONST) {
        const decl *k = e->constant;
        return k->index == 2 && k->return_type.kind == TY_BOOL && fold_bool(k->value, out);
    }
    bool l;
    bool r;
    if (e->kind == E_CONDITIONAL) return fold_bool(e->cond, &l) && fold_bool(l ? e->lhs : e->rhs, out);
    if (e->kind == E_UNARY && !e->method && e->op == T_NOT && fold_bool(e->lhs, &l)) {
        *out = !l;
        return true;
    }
    if (e->kind != E_BINARY || e->method || e->type.kind != TY_BOOL) return false;
    if (e->lhs->type.kind == TY_BOOL && fold_bool(e->lhs, &l)) {
        if ((e->op == T_AND && !l) || (e->op == T_OR && l)) {
            *out = l;
            return true;
        }
        if (!fold_bool(e->rhs, &r)) return false;
        switch (e->op) {
        case T_EQ: *out = l == r; return true;
        case T_NE: *out = l != r; return true;
        case T_AND: *out = l && r; return true;
        case T_OR: *out = l || r; return true;
        default: return false;
        }
    }
    int64_t a;
    int64_t b;
    if (!fold_int(e->lhs, &a) || !fold_int(e->rhs, &b)) return false;
    switch (e->op) {
    case T_EQ: *out = a == b; return true;
    case T_NE: *out = a != b; return true;
    case T_LT: *out = a < b; return true;
    case T_LE: *out = a <= b; return true;
    case T_GT: *out = a > b; return true;
    case T_GE: *out = a >= b; return true;
    default: return false;
    }
}

// Whether `s` has a `break` that ends the loop or switch it's in (depth 0),
// rather than one inside it.
static bool breaks_out(const stmt *s, const int depth)
{
    if (!s) return false;
    switch (s->kind) {
    case S_BREAK: return depth == 0;
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) {
            if (breaks_out(s->stmts.items[i], depth)) return true;
        }
        return false;
    case S_IF: return breaks_out(s->then_stmt, depth) || breaks_out(s->else_stmt, depth);
    case S_WHILE:
    case S_FOR:
    case S_FOREACH: return breaks_out(s->then_stmt, depth + 1);
    case S_SWITCH:
        for (int i = 0; i < s->cases.count; i++) {
            for (int k = 0; k < s->cases.items[i].body.count; k++) {
                if (breaks_out(s->cases.items[i].body.items[k], depth + 1)) return true;
            }
        }
        return false;
    case S_EXPR: return s->value->block && breaks_out(s->value->block, depth); // A call's block is the caller's code
    default: return false;
    }
}

// `while (true)` or `for (;;)` with no break of its own: it never finishes.
static bool never_ends(const stmt *s)
{
    if (s->kind != S_WHILE && s->kind != S_FOR) return false;
    const bool forever = s->kind == S_FOR ? !s->cond || (s->cond->kind == E_BOOL && s->cond->bool_value)
                                          : s->cond->kind == E_BOOL && s->cond->bool_value;
    return forever && !breaks_out(s->then_stmt, 0);
}

// Whether a statement always leaves its switch section: with break, continue
// or return, in every path.
static bool always_exits(const stmt *s)
{
    if (!s) return false;
    if (never_ends(s)) return true;
    switch (s->kind) {
    case S_RETURN:
    case S_FAIL:
    case S_BREAK:
    case S_CONTINUE: return true;
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) {
            if (always_exits(s->stmts.items[i])) return true;
        }
        return false;
    case S_IF: return always_exits(s->then_stmt) && always_exits(s->else_stmt);
    default: return false;
    }
}

// switch (value) { case A: ... break; }: on an int or an enum, as in C#. Each
// section ends with break or return; none runs into the next.
static void check_switch(checker *c, stmt *s)
{
    const type value = check_expr(c, s->cond);
    const bool switchable = type_is_integer(value) || value.kind == TY_ENUM;
    if (value.kind != TY_ERROR && !switchable) {
        diag_error(s->cond->at, "switch works on ints, longs and enums, not %s", type_name(value));
        if (value.kind == TY_BOOL) diag_note("use if/else for a bool");
    }
    bool has_default = false;
    VEC(int64_t) seen = {0};
    c->switch_depth++;
    for (int i = 0; i < s->cases.count; i++) {
        switch_case *section = &s->cases.items[i];
        for (int k = 0; k < section->labels.count; k++) {
            expr *label = section->labels.items[k];
            if (!label) {
                if (has_default) diag_error(section->label_at.items[k], "a switch has one 'default'");
                has_default = true;
                continue;
            }
            const type t = check_expr(c, label);
            if (t.kind == TY_ERROR || !switchable) continue;
            int64_t number;
            if (!fold_int(label, &number)) {
                diag_error(label->at, "a case is an int or one of an enum's members, like 'case %s:'",
                           value.kind == TY_ENUM ? "Page.Title" : "3");
                diag_note("constants work too, and operators on them, like 'case MAX_LEVEL + 1:'");
                continue;
            }
            label->int_value = number; // What codegen writes, unless it's a literal or a member
            if (!same_type(value, t) && !(value.kind == TY_LONG && (t.kind == TY_INT || t.kind == TY_BYTE))
                && !(value.kind == TY_INT && t.kind == TY_BYTE)
                && !(value.kind == TY_BYTE && t.kind == TY_INT && number >= 0 && number <= 255)) {
                diag_error(label->at, "the switch is on %s, so its cases are too, not %s", type_name(value), type_name(t));
                continue;
            }
            for (int j = 0; j < seen.count; j++) {
                if (seen.items[j] == number) {
                    diag_error(label->at, "this case is already handled above");
                    break;
                }
            }
            vec_push(seen, number);
        }
        push_scope(c);
        for (int k = 0; k < section->body.count; k++) check_stmt(c, section->body.items[k]);
        pop_scope(c);
        bool exits = false;
        for (int k = 0; k < section->body.count && !exits; k++) exits = always_exits(section->body.items[k]);
        if (!exits) {
            diag_error(section->label_at.items[0], "this case runs on into what's after it; end it with 'break;' or 'return;'");
            diag_note("as in C#, a switch's sections never fall through");
        }
    }
    c->switch_depth--;
}

// `return;` everywhere, and `return value;` in a method that returns one.
static void check_return(checker *c, const stmt *s)
{
    if (c->compare) { // A comparison's answer: whether the first element goes before the second
        const type value = s->value ? check_expr_want(c, s->value, T_BOOL_) : T_VOID_;
        if (!s->value) {
            diag_error(s->at, "a comparison returns whether its first element goes before the second: 'return %s;'",
                       compare_example(c->compare));
        } else if (value.kind != TY_ERROR && value.kind != TY_BOOL) {
            diag_error(s->value->at, "a comparison returns a bool, whether its first element goes before the second, not %s",
                       type_name(value));
            diag_note("compare the two, like 'return %s;'", compare_example(c->compare));
        }
        return;
    }
    const decl *m = c->method;
    const type value = s->value ? check_expr_want(c, s->value, m ? m->return_type : T_ERR) : T_VOID_;
    if (!m) {
        if (s->value) {
            if (c->in_input) diag_error(s->value->at, "%s doesn't return a value; use 'return;'", input_code(c));
            else if (c->system && c->system->is_command) diag_error(s->value->at, "command handlers don't return values; use 'return;'");
            else diag_error(s->value->at, "systems don't return values; use 'return;'");
        }
        return;
    }
    if (m->kind == DECL_SHADER && s->value) {
        diag_error(s->value->at, "a %s returns nothing: it %s", shader_word(m),
                   m->is_frag ? "writes its pixel's color through its 'mut Color' parameter" : "changes its vertex in place");
        for (int i = 0; m->is_frag && i < m->params.count; i++) {
            const param *p = &m->params.items[i];
            if (p->mode == PARAM_MUT && p->type.kind == TY_COLOR) {
                diag_note("write '" STR_FMT " = ...;', then 'return;' to end it here", STR_ARG(p->name));
            }
        }
        return;
    }
    if (m->return_type.kind == TY_VOID) {
        if (s->value) diag_error(s->value->at, "'" STR_FMT "' returns nothing; use 'return;'", STR_ARG(m->name));
        return;
    }
    if (!s->value) {
        diag_error(s->at, "'" STR_FMT "' returns %s; write 'return value;'", STR_ARG(m->name), type_name(m->return_type));
        return;
    }
    if (!type_assignable(m->return_type, value)) {
        diag_error(s->value->at, "'" STR_FMT "' returns %s, not %s", STR_ARG(m->name), type_name(m->return_type),
                   type_name(value));
        note_conversion(m->return_type, value);
    }
}

// `fail error;`: ends a function or method that says it `fails`, with an error
// of that type.
static void check_fail(checker *c, const stmt *s)
{
    const decl *m = c->method;
    const type value = check_expr_want(c, s->value, m && m->fails.kind != TY_VOID ? m->fails : T_ERR);
    if (!m) {
        diag_error(s->at, "%s can't fail: nothing calls it to take the error", code_what(c));
        if (!c->in_input && c->system) {
            diag_note("'return;' ends it; to tell other code what went wrong, send an event");
        }
        return;
    }
    if (m->fails.kind == TY_VOID && m->fails_name.len > 0) return; // Its `fails` is wrong, and said so
    if (m->fails.kind == TY_VOID) {
        diag_error(s->at, "'" STR_FMT "' doesn't say it can fail", STR_ARG(m->name));
        if (m->takes_action) diag_note("a function that takes an Action can't fail yet");
        else if (value.kind != TY_ERROR && value.kind != TY_VOID) {
            diag_note("say what it fails with after its parameters: '" STR_FMT " " STR_FMT "(...) fails %s'",
                      STR_ARG(m->return_type_name), STR_ARG(m->name), type_name(value));
        }
        return;
    }
    if (!type_assignable(m->fails, value)) {
        diag_error(s->value->at, "'" STR_FMT "' fails with %s, not %s", STR_ARG(m->name), type_name(m->fails),
                   type_name(value));
    }
}

// Whether every path through `s` ends in a return.
static bool always_returns(const stmt *s)
{
    if (!s) return false;
    if (never_ends(s)) return true;
    switch (s->kind) {
    case S_RETURN:
    case S_FAIL: return true;
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++) {
            if (always_returns(s->stmts.items[i])) return true;
        }
        return false;
    case S_IF: return always_returns(s->then_stmt) && always_returns(s->else_stmt);
    case S_SWITCH: {
        bool has_default = false;
        for (int i = 0; i < s->cases.count; i++) {
            const switch_case *section = &s->cases.items[i];
            bool returns = false;
            for (int k = 0; k < section->labels.count; k++) has_default |= section->labels.items[k] == NULL;
            for (int k = 0; k < section->body.count && !returns; k++) returns = always_returns(section->body.items[k]);
            if (!returns) return false;
        }
        return has_default;
    }
    default: return false;
    }
}

static void check_var(checker *c, stmt *s)
{
    s->errors_before = diag_error_count();
    // A typed local's value is checked once its type is known: `default`, a
    // list and `null` take it, and a T? takes a value or nothing.
    type value = T_ERR;
    if (s->value->kind == E_DEFAULT && s->type_name.len == 0) {
        diag_error(s->value->at, "'var' takes its type from the value, and 'default' takes its type from the variable");
        diag_note("write the type instead, like 'float2 " STR_FMT " = default;'", STR_ARG(s->name));
    } else if (s->value->kind == E_NULL && s->type_name.len == 0) {
        diag_error(s->value->at, "'var' takes its type from the value, and 'null' takes its type from the variable");
        diag_note("write the type instead, like 'int? " STR_FMT " = null;'", STR_ARG(s->name));
    } else if (s->type_name.len == 0) {
        value = check_expr_any(c, s->value); // `var` holds a failable call's result, or a T?, as it is
        if (s->value->kind == E_NAME && s->value->bind == BIND_TYPE) value = T_ERR;
    }

    if (s->type_name.len == 0) {
        if (value.kind == TY_VOID) {
            diag_error(s->value->at, "this expression doesn't produce a value");
            value = T_ERR;
        }
        if (value.kind == TY_DRAW_LIST) {
            diag_error(s->value->at, "the draw list is only passed on, to an extern function that takes a DrawList");
            diag_note("write 'Draw2D.list' where it goes, like 'DrawUI(Draw2D.list);'");
            value = T_ERR;
        }
        s->type = value;
    } else {
        str inner_name;
        const bool optional = optional_name(s->type_name, &inner_name);
        const str type_name_ = optional ? inner_name : s->type_name;
        decl *d = str_starts_with_c(type_name_, "List<") || str_starts_with_c(type_name_, "Grid2<")
                       || str_starts_with_c(type_name_, "Grid3<")
                    ? NULL
                    : find_type(c, type_name_, s->type_at);
        if (builtin_type_named(type_name_, &s->type)) {
        } else if (resolve_list_type(c, type_name_, s->type_qual_at.line ? s->type_qual_at : s->at, &s->type)) {
        } else if (resolve_map_type(c, type_name_, s->type_qual_at.line ? s->type_qual_at : s->at, &s->type)) {
        } else if (resolve_grid_type(c, type_name_, s->type_qual_at.line ? s->type_qual_at : s->at, &s->type)) {
        } else if (str_eq_c(type_name_, "string")) {
            s->type = (type){TY_STRING, NULL};
        } else if (str_eq_c(type_name_, "Action")) {
            diag_error(s->type_at, "an Action is only ever a function's last parameter, run with 'content();'");
            s->type = T_ERR;
        } else if (str_eq_c(type_name_, "DrawList")) {
            diag_error(s->type_at, "the draw list is only passed on, to an extern function that takes a DrawList");
            diag_note("write 'Draw2D.list' where it goes, like 'DrawUI(Draw2D.list);'");
            s->type = T_ERR;
        } else if (d) {
            s->type = decl_type(d);
        } else if (world_misplaced(type_name_, s->type_at.line ? s->type_at : s->at)) {
            s->type = T_ERR;
        } else {
            diag_error(s->type_at.line ? s->type_at : s->at, "unknown type '" STR_FMT "'", STR_ARG(type_name_));
            suggestion sg = suggest_start(type_name_);
            suggest_builtin_types(&sg);
            suggest_decls(&sg, c->prog, true, true, true);
            suggest_structs(&sg, c->prog);
            suggest_note(&sg);
            const fix create = {.kind = FIX_CREATE_STRUCT, .at = s->type_qual_at.line ? s->type_qual_at : s->at, .name = type_name_};
            vec_push(c->prog->fixes, create);
            s->type = T_ERR;
        }
        if (optional) s->type = optional_type(c, s->type, s->type_qual_at.line ? s->type_qual_at : s->at);
        if (s->value->kind == E_DEFAULT || s->value->kind == E_NULL || (optional && s->value->kind == E_LIST)) {
            value = check_expr_want(c, s->value, s->type);
        } else if (s->value->kind == E_LIST) {
            if (s->type.kind != TY_LIST && s->type.kind != TY_SET && s->type.kind != TY_MAP && s->type.kind != TY_ERROR) {
                diag_error(s->value->at, "can't initialize %s with a list", type_name(s->type));
                const type_kind k = s->type.kind;
                const bool holdable = k != TY_COMPONENT && k != TY_SINGLETON && k != TY_INPUT && k != TY_RECORD
                                   && k != TY_EVENT && !holds_list(s->type);
                if (holdable) diag_note("declare a list of them as 'List<%s> " STR_FMT " = [...];'", type_name(s->type), STR_ARG(s->name));
                else diag_note("a list's type is 'List<T>', like 'List<int> " STR_FMT " = [1, 2];'", STR_ARG(s->name));
            }
            value = check_list_literal(c, s->value, s->type);
        } else {
            value = check_expr_want(c, s->value, s->type);
            if (s->value->kind == E_NAME && s->value->bind == BIND_TYPE) value = T_ERR;
        }
        if (!type_assignable(s->type, value)) {
            diag_error(s->value->at, "can't initialize %s with %s", type_name(s->type), type_name(value));
            note_conversion(s->type, value);
        }
    }

    // A grid stays where it's made: a local would be a copy of every chunk
    if (holds_grid(s->type)) {
        const loc at = s->type_at.line ? s->type_at : s->value->at;
        if (s->type.kind == TY_GRID) diag_error(at, "a local can't hold a grid");
        else diag_error(at, "a local can't hold '" STR_FMT "': it has a grid in it", STR_ARG(s->type.decl->name));
        diag_note("grids stay in their component, singleton or scene: read and change cells through it, like 'canvas.pixels[p]'");
        s->type = T_ERR;
    }

    if (holds_render_resource(s->type) && !gpu_code(c)) {
        decl *code = reading_code(c);
        if (code) {
            if (!code->render_resources) code->render_resources_at = s->at;
            code->render_resources = true;
        }
        if (!c->method && !local_code(c)) {
            diag_error(s->type_at.line ? s->type_at : s->at, "rendering resources belong to this machine, so the match can't keep them in a local");
            diag_note("keep the Texture or RenderTarget in local state and use it in a view");
            s->type = T_ERR;
        }
    }

    check_reserved(s->name, s->at);
    if (find_local(c, s->name) || find_param(c, s->name)) {
        diag_error(s->at, "'" STR_FMT "' is already declared", STR_ARG(s->name));
    }
    vec_push(c->locals, s);
}

// The `is` tests in a condition whose names are in scope where it's true: the
// condition itself, or its parts joined by &&.
static void allow_bindings(expr *cond)
{
    if (!cond) return;
    if (cond->kind == E_IS) cond->binding_ok = true;
    if (cond->kind != E_BINARY || cond->op != T_AND) return;
    allow_bindings(cond->lhs);
    allow_bindings(cond->rhs);
}

// The type a foreach's variable declares, for the list's elements to be checked against.
static type local_type(checker *c, stmt *s)
{
    type t;
    if (builtin_type_named(s->type_name, &t)) return t;
    const loc at = s->type_qual_at.line ? s->type_qual_at : s->at;
    const int errors = diag_error_count();
    if (resolve_list_type(c, s->type_name, at, &t) || resolve_map_type(c, s->type_name, at, &t)) {
        if (diag_error_count() > errors) s->type_name = str_from(""); // Reported: it takes the element's type, like var
        return t;
    }
    if (resolve_grid_type(c, s->type_name, at, &t)) {
        if (t.kind != TY_ERROR) diag_error(at, "a local can't hold a grid");
        s->type_name = str_from("");
        return T_ERR;
    }
    decl *d = find_type(c, s->type_name, s->type_at);
    return d ? decl_type(d) : T_ERR;
}

// Whether a checked expression can be a statement of its own: a call that's
// there for what it does, with `!` after or `try` before the ones that fail.
static bool stands_alone(const expr *e)
{
    const expr *called = e->kind == E_DEFAULTED || e->kind == E_TRY ? e->lhs : e;
    const builtin_call call = called->call;
    return (called->kind == E_METHOD && (call == CALL_ADD || call == CALL_REMOVE || call == CALL_DESTROY
                                         // A Draw call that gives a value only measures or converts
                                         || (call == CALL_DRAW && called->type.kind == TY_VOID)))
        || (called->kind == E_CALL && call == CALL_SPAWN) || call == CALL_METHOD || call == CALL_FUNCTION
        || call == CALL_SEND || call == CALL_LOAD || call == CALL_UNLOAD || call == CALL_SCENE_PLAYER
        || call == CALL_GUI || call == CALL_ACTION || call == CALL_SESSION || call == CALL_CLIPBOARD
        || call == CALL_SNAP || call == CALL_DISCARD
        || called->io_operation
        || (call == CALL_LIST && !str_eq_c(called->name, "Contains") && !str_eq_c(called->name, "IndexOf"))
        || (call == CALL_MAP && !str_eq_c(called->name, "Has") && !str_eq_c(called->name, "Get"))
        || (call == CALL_GRID && !str_eq_c(called->name, "Sample"))
        || (call == CALL_WORLD && str_eq_c(called->name, "Set"))
        || (call == CALL_BUILTIN && called->type.kind == TY_VOID) // Debug.Log and Debug.Assert
        || e->kind == E_TRY // Passes an error on, even from a variable
        || called->kind == E_AWAIT;
}

static void check_stmt(checker *c, stmt *s)
{
    switch (s->kind) {
    case S_BLOCK:
        push_scope(c);
        for (int i = 0; i < s->stmts.count; i++) check_stmt(c, s->stmts.items[i]);
        pop_scope(c);
        break;
    case S_IF: {
        // Names after `is` in the condition are in scope where it's true.
        allow_bindings(s->cond);
        push_scope(c);
        const type cond = check_expr(c, s->cond);
        if (cond.kind != TY_ERROR && cond.kind != TY_BOOL) {
            diag_error(s->cond->at, "condition must be bool, not %s", type_name(cond));
        }
        push_scope(c);
        check_stmt(c, s->then_stmt);
        pop_scope(c);
        pop_scope(c);
        if (s->else_stmt) {
            push_scope(c);
            check_stmt(c, s->else_stmt);
            pop_scope(c);
        }
        break;
    }
    case S_RETURN:
        check_return(c, s);
        break;
    case S_FAIL:
        check_fail(c, s);
        break;
    case S_VAR:
        check_var(c, s);
        break;
    case S_ASSIGN:
        check_assign(c, s);
        break;
    case S_SWITCH:
        check_switch(c, s);
        break;
    case S_BREAK:
        if (c->switch_depth == 0 && c->loop_depth == 0) {
            diag_error(s->at, "'break' ends a loop or a switch's section, and it's in neither");
            diag_note("use 'return;' to end %s here", c->method ? "it" : "the system");
        }
        break;
    case S_CONTINUE:
        if (c->loop_depth == 0) {
            diag_error(s->at, "'continue' goes on to a loop's next round, and it's not in a loop");
            diag_note("use 'return;' to end %s here", c->method ? "it" : "the system");
        }
        break;
    case S_PARALLEL:
        check_parallel(c, s);
        break;
    case S_FOREACH: {
        c->device_whole_ok = s->value; // Touches count as read by what the loop reads of them
        c->query_value = s->value->kind == E_NAME ? s->value : NULL;
        const type list = check_expr(c, s->value);
        c->query_value = NULL;
        type element = T_ERR;
        if (list.kind == TY_QUERY) {
            s->query = s->value->param;
            element = (type){TY_STRUCT, list.decl};
            if (c->parallel || c->block_depth || c->compare) {
                diag_error(s->at, "a query foreach must run directly in its system, outside parallel loops and callback blocks");
            }
        } else if (list.kind == TY_LIST) {
            element = list_element(list);
        } else if (list.kind == TY_MAP) { // Its entries, in the order their keys were added: each a key and a value
            element = (type){TY_STRUCT, list.decl->entry};
        } else if (list.kind == TY_SET) { // Its keys, in the order they were added
            element = map_key(list);
        } else if (list.kind == TY_GRID) { // Its cells' positions, in order
            element = list.decl->dims == 2 ? (type){TY_INT2, NULL} : (type){TY_INT3, NULL};
        } else if (list.kind == TY_RECORD && list.decl->array_of) { // touches: every one, in use or not
            element = (type){TY_RECORD, list.decl->array_of};
        } else if (list.kind != TY_ERROR) {
            diag_error(s->value->at, "foreach goes through a list, a map, a set, a grid or an entity query, and this is %s", type_name(list));
        }
        if (s->type_name.len > 0) {
            if (s->query) {
                diag_error(s->type_at, "a query row takes its type from the query: write 'foreach (var " STR_FMT " in ...)'", STR_ARG(s->name));
            }
            const type declared = s->query ? T_ERR : local_type(c, s);
            if (declared.kind != TY_ERROR && element.kind != TY_ERROR && !same_type(declared, element)) {
                diag_error(s->type_at, "the list holds %s, not %s", type_name(element), type_name(declared));
                diag_note("write 'foreach (var " STR_FMT " in ...)' to take the elements as they are", STR_ARG(s->name));
            }
        }
        s->type = element;
        check_reserved(s->name, s->name_at);
        if (find_local(c, s->name) || find_param(c, s->name)) {
            diag_error(s->name_at, "'" STR_FMT "' is already declared", STR_ARG(s->name));
        }
        push_scope(c);
        vec_push(c->locals, s);
        const int switches = c->switch_depth;
        c->switch_depth = 0;
        c->loop_depth++;
        c->grid_loops += list.kind == TY_GRID;
        push_scope(c);
        check_stmt(c, s->then_stmt);
        pop_scope(c);
        c->grid_loops -= list.kind == TY_GRID;
        c->loop_depth--;
        c->switch_depth = switches;
        pop_scope(c);
        // Its steps at once, where a parallel loop could be and nothing they
        // do depends on their order
        if (list.kind == TY_GRID) {
            for (int i = 0; i < 3; i++) s->block[i] = 1;
            s->parallel = c->system && !c->method && !async_code(c) && !c->in_input && !c->parallel
                       && own_cell_only(s->then_stmt, s, 0);
            if (s->parallel) plan_window(s);
            if (!s->parallel) c->prog->uses_text = true; // In order, it keeps where it is in the scratch area
        }
        break;
    }
    case S_WHILE:
    case S_FOR: {
        push_scope(c);
        if (s->init) {
            if (s->init->kind == S_EXPR && s->init->value->block) {
                diag_error(s->init->at, "a for starts with a variable, an assignment or a call, not a block");
            }
            check_stmt(c, s->init);
        }
        c->loop_header++;
        if (s->cond) {
            allow_bindings(s->cond); // In scope in the step and the body
            const type cond = check_expr(c, s->cond);
            if (cond.kind != TY_ERROR && cond.kind != TY_BOOL) {
                diag_error(s->cond->at, "a loop's condition must be bool, not %s", type_name(cond));
            }
        }
        if (s->step) {
            const stmt *outer = c->loop_var;
            c->loop_var = s->init && s->init->kind == S_VAR ? s->init : NULL;
            check_stmt(c, s->step);
            c->loop_var = outer;
        }
        c->loop_header--;
        // A switch inside the loop has its own sections; `break` in one ends it.
        const int switches = c->switch_depth;
        c->switch_depth = 0;
        c->loop_depth++;
        push_scope(c);
        check_stmt(c, s->then_stmt);
        pop_scope(c);
        c->loop_depth--;
        c->switch_depth = switches;
        pop_scope(c);
        break;
    }
    case S_EXPR: {
        expr *e = s->value;
        // An async call alone starts a task: `Load(name);`, or `Load(name)!;`
        const expr *outer_call = c->statement_call;
        c->statement_call = e->kind == E_DEFAULTED ? e->lhs : e;
        check_expr_any(c, e);
        c->statement_call = outer_call;
        // `Open()!` and `try Open()` are statements when what they unwrap is a call.
        const expr *called = e->kind == E_DEFAULTED || e->kind == E_TRY ? e->lhs : e;
        const builtin_call call = called->call;
        const bool effect = stands_alone(e);
        if (!effect && e->type.kind != TY_ERROR) diag_error(e->at, "this expression does nothing on its own");
        // A call that can fail, whose error nothing looks at.
        if (e->type.kind == TY_FAILABLE && effect) {
            const expr *named = e->kind == E_AWAIT ? e->lhs : e;
            diag_warning(e->at, "%s can fail, and nothing handles its error here", wrapped_what(named));
            diag_note("handle it with 'is %s', pass it on with 'try', or carry on without it: '%s" STR_FMT "(...)!'",
                      pattern_example(wrapped_error(e->type), true), e->kind == E_AWAIT ? "await " : "",
                      STR_ARG(named->method ? named->method->name : named->name));
            unwrap_fix(c, e, e->type);
        }
        if (e->block && call == CALL_LIST && str_eq_c(called->name, "Sort")) {
            // A Sort's comparison: checked with the call, as code of its own (check_sort)
        } else if (e->block) {
            // The block after a call: the caller's own code, run where the function runs it.
            const bool takes = (call == CALL_GUI && (e->gui & GUI_CONTAINER)) || (call == CALL_FUNCTION && e->method->takes_action);
            if (!takes && e->type.kind != TY_ERROR) {
                if (e->kind == E_METHOD && e->object->kind == E_NAME) {
                    diag_error(e->block->at, "'" STR_FMT "." STR_FMT "' doesn't take a block", STR_ARG(e->object->name),
                               STR_ARG(e->name));
                } else {
                    diag_error(e->block->at, "'" STR_FMT "' doesn't take a block", STR_ARG(e->name));
                }
                diag_note("is a ';' missing after the call?");
            }
            c->block_depth++;
            check_stmt(c, e->block);
            c->block_depth--;
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Declarations

static bool all_constant(const checker *c, const expr *e);

// Whether `e` is only names and members, like Page.Title: maybe an enum's member.
static bool names_only(const expr *e)
{
    while (e->kind == E_MEMBER) e = e->object;
    return e->kind == E_NAME;
}

// Constant expressions: literals, constants, constructors of built-in types,
// Math functions, built-in constants like quaternion.identity, members of any
// of these, and operators on them. Nothing that reads fields, singletons or
// Time, so a default never depends on other state.
static bool is_constant(const checker *c, const expr *e)
{
    type ignored;
    decl *other;
    switch (e->kind) {
    case E_INT:
    case E_FLOAT:
    case E_BOOL:
    case E_STRING:
    case E_DEFAULT:
        return true;
    case E_NAME:
        return lookup(c->prog, c->unit, e->name, NAME_CONST, &other) != NULL;
    case E_UNARY:
        return is_constant(c, e->lhs);
    case E_BINARY:
        return is_constant(c, e->lhs) && is_constant(c, e->rhs);
    case E_CONDITIONAL:
        return is_constant(c, e->cond) && is_constant(c, e->lhs) && is_constant(c, e->rhs);
    case E_CALL: // A built-in type's constructor, an enum's conversion, a grid's size, and a Random's seed
        return (builtin_type_named(e->name, &ignored) || is_new_grid(e) || converted_enum(c, e)
                || lookup(c->prog, c->unit, e->name, NAME_TYPE, &other) == c->prog->random)
            && all_constant(c, e);
    case E_METHOD: // Math.Max(...), and Combat.Stance(2)
        return ((e->object->kind == E_NAME && builtin_owner(e->object->name)) || converted_enum(c, e)) && all_constant(c, e);
    case E_MEMBER: // Checking tells a member of an enum or a constant from anything else
        return (e->object->kind == E_NAME && builtin_owner(e->object->name)) || is_constant(c, e->object) || names_only(e);
    case E_LITERAL:
        for (int i = 0; i < e->inits.count; i++) {
            if (!is_constant(c, e->inits.items[i].value)) return false;
        }
        return true;
    case E_LIST:
        return all_constant(c, e);
    default:
        return false;
    }
}

static bool all_constant(const checker *c, const expr *e)
{
    for (int i = 0; i < e->args.count; i++) {
        if (!is_constant(c, e->args.items[i])) return false;
    }
    return true;
}

// The first name in `e` that isn't a constant, or NULL. Chains of names like
// Math.PI or Combat.MAX are left out: they're what they name.
static const expr *first_variable(const checker *c, const expr *e)
{
    if (!e) return NULL;
    decl *other;
    if (e->kind == E_NAME) return lookup(c->prog, c->unit, e->name, NAME_CONST, &other) ? NULL : e;
    const expr *found = (e->kind == E_MEMBER || e->kind == E_METHOD) && !names_only(e->object) ? first_variable(c, e->object) : NULL;
    if (!found) found = first_variable(c, e->cond);
    if (!found) found = first_variable(c, e->lhs);
    if (!found) found = first_variable(c, e->rhs);
    for (int i = 0; !found && i < e->args.count; i++) found = first_variable(c, e->args.items[i]);
    for (int i = 0; !found && i < e->inits.count; i++) found = first_variable(c, e->inits.items[i].value);
    return found;
}

// After saying a value isn't constant: the constant a name in it may have
// meant, if one is spelled like it.
static void suggest_constant(const checker *c, const expr *value)
{
    const expr *name = first_variable(c, value);
    if (!name) return;
    suggestion s = suggest_start(name->name);
    for (int i = 0; i < c->prog->decls.count; i++) {
        if (c->prog->decls.items[i]->kind == DECL_CONST) suggest_consider(&s, c->prog->decls.items[i]->name);
    }
    suggest_note(&s);
}

// A constant expression, where only constants are in scope, whatever code is
// being checked: a field's default, a bound, a constant's value. With `typed`,
// it's a value of `want`, which [a, b] and `default` take their type from.
static type check_constant_expr(checker *c, expr *value, const bool typed, const type want)
{
    decl *const system = c->system;
    decl *const method = c->method;
    const bool in_input = c->in_input;
    const bool in_sanitize = c->in_sanitize;
    const int locals = c->locals.count;
    c->system = c->method = NULL;
    c->in_input = c->in_sanitize = false;
    c->locals.count = 0;
    const type t = typed ? check_expr_want(c, value, want) : check_expr(c, value);
    c->system = system;
    c->method = method;
    c->in_input = in_input;
    c->in_sanitize = in_sanitize;
    c->locals.count = locals;
    return t;
}

// A constant's type: a value with nothing in it a constant can't be.
static type constant_type(const checker *c, const decl *k)
{
    const loc at = k->return_type_qual_at.line ? k->return_type_qual_at : k->return_type_at;
    type t;
    if (builtin_type_named(k->return_type_name, &t)) {
        if (holds_render_resource(t)) {
            diag_error(at, "a constant can't hold rendering resources");
            return T_ERR;
        }
        if (t.kind == TY_ENTITY || t.kind == TY_LOCAL_ENTITY) {
            diag_error(at, "an %s constant would always be the null entity", type_name(t));
            return T_ERR;
        }
        return t;
    }
    if (str_eq_c(k->return_type_name, "string")) return (type){TY_STRING, NULL};
    if (str_starts_with_c(k->return_type_name, "List<")) {
        diag_error(at, "a constant can't be a list yet");
        return T_ERR;
    }
    decl *const d = find_type(c, k->return_type_name, at);
    if (d && (d->kind == DECL_STRUCT || d->kind == DECL_ENUM)) {
        if (holds_render_resource(decl_type(d))) {
            diag_error(at, "a constant can't hold rendering resources, including inside a struct");
            return T_ERR;
        }
        if (holds_list(decl_type(d))) {
            diag_error(at, "a constant can't hold a list yet, and '" STR_FMT "' has one", STR_ARG(d->name));
            return T_ERR;
        }
        return decl_type(d);
    }
    if (d) {
        diag_error(at, "a constant is a built-in type, text, a struct or an enum, not %s", decl_what(d));
        diag_note("for the values a component starts with, give its fields defaults: 'int lives = 3;'");
        return T_ERR;
    }
    diag_error(at, "unknown type '" STR_FMT "'", STR_ARG(k->return_type_name));
    suggestion s = suggest_start(k->return_type_name);
    suggest_builtin_types(&s);
    suggest_consider_c(&s, "string");
    suggest_structs(&s, c->prog);
    suggest_note(&s);
    return T_ERR;
}

// const int MAX_HEALTH = 100;: its type, and a value worked out from constants
// alone. A constant is checked before anything reads it: one that names
// another checks that one first (index: 0 not yet, 1 checking, 2 done).
static void check_constant(checker *c, decl *k)
{
    if (k->index != 0) return;
    k->index = 1;
    const unit *const outer = c->unit;
    c->unit = k->unit;
    check_reserved(k->name, k->at);
    k->return_type = constant_type(c, k);
    if (!is_constant(c, k->value)) {
        diag_error(k->value->at, "a constant's value must be a constant expression");
        diag_note("use literals, other constants, constructors like float3(...), struct values of constants, Math "
                  "functions and operators; a constant can't read fields, singletons or Time");
        suggest_constant(c, k->value);
    } else {
        const type t = check_constant_expr(c, k->value, true, k->return_type);
        if (k->return_type.kind != TY_ERROR && t.kind != TY_ERROR && !type_assignable(k->return_type, t)) {
            diag_error(k->value->at, "constant '" STR_FMT "' is %s, not %s", STR_ARG(k->name), type_name(k->return_type),
                       type_name(t));
            note_conversion(k->return_type, t);
        }
    }
    c->unit = outer;
    k->index = 2;
}

// The text a constant expression is, through the constants it names: the
// "Asteroids" in title = NAME. NULL for text made some other way.
static const expr *text_literal(const expr *e)
{
    if (e->kind == E_STRING) return e;
    const decl *k = e->bind == BIND_CONST ? e->constant : NULL;
    return k && k->index == 2 && k->return_type.kind == TY_STRING ? text_literal(k->value) : NULL;
}

// The true or false a constant expression is, through the constants it names:
// the true in hostMigration = MIGRATES. NULL for one made some other way.
static const expr *bool_literal(const expr *e)
{
    while (e->bind == BIND_CONST && e->constant->index == 2) e = e->constant->value;
    return e->kind == E_BOOL ? e : NULL;
}

// An app ID both Android (a Java package) and iOS (a bundle ID) take: two
// parts or more, of ASCII letters and digits, each starting with a letter.
static bool valid_app_id(const str id)
{
    int parts = 0;
    bool start = true;
    for (int i = 0; i < id.len; i++) {
        const char ch = id.ptr[i];
        const bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        if (ch == '.') {
            if (start) return false;
            start = true;
        } else if (start) {
            if (!letter) return false;
            start = false;
            parts++;
        } else if (!letter && !(ch >= '0' && ch <= '9')) {
            return false;
        }
    }
    return !start && parts >= 2 && id.len <= 200;
}

// A version both Android and iOS take: one to three numbers between dots, as
// iOS's CFBundleShortVersionString has to be.
static bool valid_version(const str v)
{
    int parts = 1, digits = 0;
    for (int i = 0; i < v.len; i++) {
        const char ch = v.ptr[i];
        if (ch == '.') {
            if (digits == 0) return false;
            parts++;
            digits = 0;
        } else if (ch >= '0' && ch <= '9') {
            if (++digits > 9) return false;
        } else {
            return false;
        }
    }
    return digits > 0 && parts <= 3;
}

// settings { tickRate = 30; }: the engine's settings for the game (builtins.h),
// each set to a constant of its type. A game has one block.
static void check_settings(checker *c)
{
    program *prog = c->prog;
    for (int b = 0; b < prog->settings.count; b++) {
        const decl *block = prog->settings.items[b];
        c->unit = block->unit;
        if (b > 0) {
            diag_error(block->at, "a game has one 'settings' block");
            const decl *first = prog->settings.items[0];
            const source *src = diag_source(first->at.file);
            if (src) diag_note_at(first->at, "the other one is in %s, on line %d", src->path, first->at.line);
        }
        for (int i = 0; i < block->fields.count; i++) {
            field *f = &block->fields.items[i];
            const setting *s = setting_named(f->name);
            if (!s) {
                diag_error(f->at, "'" STR_FMT "' isn't one of the engine's settings", STR_ARG(f->name));
                int count;
                const setting *all = settings_list(&count);
                suggestion sg = suggest_start(f->name);
                for (int k = 0; k < count; k++) suggest_consider_c(&sg, all[k].name);
                suggest_note(&sg);
                diag_note("a game's own values are constants, at the top of a file: 'const int " STR_FMT " = ...;'",
                          STR_ARG(f->name));
                continue;
            }
            // An enum setting is one of the engine's enums: `prediction` a Prediction
            f->type = (type){s->kind, s->kind == TY_ENUM ? prog->prediction : NULL};
            bool twice = false;
            for (int j = 0; j < i; j++) twice |= str_eq(block->fields.items[j].name, f->name);
            if (twice) {
                diag_error(f->at, "'" STR_FMT "' is set twice", STR_ARG(f->name));
                continue;
            }
            expr *value = f->default_value;
            if (!is_constant(c, value)) {
                diag_error(value->at, "a setting's value must be a constant expression");
                diag_note("use literals, constants and operators: settings are part of the build, the same on every machine");
                suggest_constant(c, value);
                continue;
            }
            const type t = check_constant_expr(c, value, true, f->type);
            if (t.kind == TY_ERROR) continue;
            if (!type_assignable(f->type, t)) {
                diag_error(value->at, "'" STR_FMT "' is %s, not %s", STR_ARG(f->name), type_name(f->type), type_name(t));
                continue;
            }
            if (b > 0) continue; // Only the first block counts
            if (str_eq_c(f->name, "tickRate")) {
                int64_t rate;
                if (!fold_int(value, &rate)) {
                    diag_error(value->at, "tickRate is an int known while compiling, like 'tickRate = 30;'");
                } else if (rate < 1 || rate > 1000) {
                    diag_error(value->at, "tickRate is from 1 to 1000 ticks a second, not %lld", (long long)rate);
                } else {
                    prog->tick_rate = (uint32_t)rate;
                }
            } else if (str_eq_c(f->name, "maxPlayers")) {
                int64_t players;
                if (!fold_int(value, &players)) {
                    diag_error(value->at, "maxPlayers is an int known while compiling, like 'maxPlayers = 64;'");
                } else if (players < 1 || players > TIDE_PLAYERS_MOST) {
                    diag_error(value->at, "maxPlayers is from 1 to %u players, not %lld", TIDE_PLAYERS_MOST, (long long)players);
                    if (players > TIDE_PLAYERS_MOST) {
                        diag_note("every player's input goes to every other each tick, so a match's traffic grows with the "
                                  "square of its players");
                    }
                } else {
                    prog->max_players = (uint32_t)players;
                }
            } else if (str_eq_c(f->name, "hostMigration")) {
                const expr *known = bool_literal(value);
                if (!known) diag_error(value->at, "hostMigration is true or false, or a constant that is");
                else prog->host_migration = known->bool_value;
            } else if (str_eq_c(f->name, "stats")) {
                const expr *known = bool_literal(value);
                if (!known) diag_error(value->at, "stats is true or false, or a constant that is");
                else prog->stats = known->bool_value;
            } else if (str_eq_c(f->name, "debugViews")) {
                const expr *known = bool_literal(value);
                if (!known) diag_error(value->at, "debugViews is true or false, or a constant that is");
                else prog->debug_views = known->bool_value;
            } else if (str_eq_c(f->name, "prediction")) {
                int64_t member;
                if (!fold_int(value, &member)) {
                    diag_error(value->at, "prediction is 'Prediction.Rollback' or 'Prediction.Delay', or a constant that is");
                } else {
                    prog->delay_prediction = member == 1;
                }
            } else if (str_eq_c(f->name, "title")) {
                prog->title = text_literal(value);
                if (!prog->title) {
                    diag_error(value->at, "the title is text written out, like 'title = \"Asteroids\";', or a constant that is");
                }
            } else if (str_eq_c(f->name, "appId")) {
                prog->app_id = text_literal(value);
                if (!prog->app_id) {
                    diag_error(value->at, "the app ID is text written out, like 'appId = \"com.studio.game\";', or a "
                                          "constant that is");
                } else if (!valid_app_id(prog->app_id->text)) {
                    diag_error(value->at, "'" STR_FMT "' isn't an app ID that Android and iOS both take",
                               STR_ARG(prog->app_id->text));
                    diag_note("it's parts of letters and digits, each starting with a letter, between dots, like "
                              "'com.studio.game'");
                    prog->app_id = NULL;
                }
            } else if (str_eq_c(f->name, "version")) {
                prog->version = text_literal(value);
                if (!prog->version) {
                    diag_error(value->at, "the version is text written out, like 'version = \"1.2.0\";', or a constant "
                                          "that is");
                } else if (!valid_version(prog->version->text)) {
                    diag_error(value->at, "'" STR_FMT "' isn't a version that Android and iOS both take",
                               STR_ARG(prog->version->text));
                    diag_note("it's one to three numbers between dots, like '1.2.0'");
                    prog->version = NULL;
                }
            }
        }
    }
}

// MAX_HEALTH, or Combat.MAX_HEALTH: a constant, which stands for its value.
static type use_constant(checker *c, expr *e, decl *k)
{
    e->bind = BIND_CONST;
    e->constant = k;
    check_constant(c, k);
    if (k->index == 1) {
        diag_error(e->at, "'" STR_FMT "' is worked out from itself", STR_ARG(k->name));
        diag_note("a constant's value can name other constants, but not, through them, its own");
        return T_ERR;
    }
    return k->return_type;
}

static void check_default(checker *c, const field *f)
{
    expr *value = f->default_value;
    if ((f->type.kind == TY_ENTITY || f->type.kind == TY_LOCAL_ENTITY) && value->kind != E_DEFAULT) {
        diag_error(value->at, "%s fields always start as the null entity; they can't have a default value",
                   type_name(f->type));
        return;
    }
    if (!is_constant(c, value)) {
        diag_error(value->at, "default values must be constant expressions");
        diag_note("use literals, constants, constructors like float3(...), struct values of constants, Math "
                  "functions and operators; defaults can't read fields, singletons or Time");
        suggest_constant(c, value);
        return;
    }
    const type t = check_constant_expr(c, value, true, f->type);
    if (f->type.kind != TY_ERROR && !type_assignable(f->type, t)) {
        diag_error(value->at, "field '" STR_FMT "' is %s, not %s", STR_ARG(f->name), type_name(f->type), type_name(t));
        note_conversion(f->type, t);
    }
}

// A bound of [Clamp], [Min] or [Max] on field `f`: a constant of the field's
// type, or a number for every component of a vector.
static void check_bound(checker *c, const field *f, expr *value)
{
    if (!is_constant(c, value)) {
        diag_error(value->at, "attribute bounds must be constants");
        diag_note("use literals, constants, constructors like float2(...), Math constants and operators");
        suggest_constant(c, value);
        return;
    }
    const type t = check_constant_expr(c, value, f->type.kind == TY_BYTE, f->type.kind == TY_BYTE ? f->type : T_ERR);
    if (t.kind == TY_ERROR || f->type.kind == TY_ERROR) return;
    const bool splat = type_dim(t) == 1 && type_is_numeric(t) && type_dim(f->type) > 1
                    && (type_is_float_based(f->type) || type_is_int_based(t));
    if (!type_assignable(f->type, t) && !splat) {
        diag_error(value->at, "the bound is %s, but field '" STR_FMT "' is %s", type_name(t), STR_ARG(f->name),
                   type_name(f->type));
    }
}

// [Snap] on a field views would see blended between ticks: they see it as
// it is at the latest tick instead, like an angle that wraps from 359 to 0.
static void check_snap_attribute(const decl *d, const field *f, const attribute *a)
{
    if (a->values.count > 0) {
        diag_error(a->at, "[Snap] takes nothing: '[Snap] float heading;'");
    } else if (d->kind == DECL_INPUT || d->kind == DECL_EVENT) {
        diag_error(a->at, "%s isn't drawn, so there's nothing to snap", d->kind == DECL_INPUT ? "an input" : "an event");
        diag_note("[Snap] goes on the fields of components, singletons and structs");
    } else if (d->is_local) {
        diag_error(a->at, "local state is never blended: views see it as it is");
        diag_note("views see the match blended between its last two ticks; [Snap] keeps a match field out of that");
    } else if (f->type.kind != TY_ERROR && !type_blends(f->type)) {
        diag_error(a->at, "views never blend %s, so '" STR_FMT "' snaps already", type_name(f->type), STR_ARG(f->name));
        diag_note("views blend floats, vectors, quaternions, colors and rects between ticks; [Snap] is for those");
    }
}

// [Clamp(lo, hi)], [Min(x)] and [Max(x)] on input and struct fields. The
// engine applies them to every input before Sanitize, the fields of structs in
// it included, so they're a quick way to bound what players send. Elsewhere,
// a struct field's bounds only describe it. And [Snap] (see check_snap).
static void check_field_attributes(checker *c, const decl *d, field *f)
{
    if (f->attributes.count == 0) return;
    bool has_clamp = false;
    bool has_min = false;
    bool has_max = false;
    bool has_snap = false;
    for (int i = 0; i < f->attributes.count; i++) has_snap |= str_eq_c(f->attributes.items[i].name, "Snap");
    for (int i = 0; i < f->attributes.count; i++) {
        attribute *a = &f->attributes.items[i];
        const bool clamp = str_eq_c(a->name, "Clamp");
        const bool min = str_eq_c(a->name, "Min");
        const bool max = str_eq_c(a->name, "Max");
        const bool position = str_eq_c(a->name, "Position");
        const bool rotation = str_eq_c(a->name, "Rotation");
        if (position || rotation) {
            if (a->values.count || a->args.count) {
                diag_error(a->at, "[" STR_FMT "] takes no arguments", STR_ARG(a->name));
            } else if ((d->kind != DECL_COMPONENT && d->kind != DECL_SINGLETON && d->kind != DECL_STRUCT) || d->is_local
                       || d->part_of || f->is_local) {
                diag_error(a->at, "[" STR_FMT "] describes a match field, not local state, input or events", STR_ARG(a->name));
                diag_note("put it on a match component or singleton field, or a field of an ordinary struct they hold");
            } else if (has_snap) {
                diag_error(a->at, "[Snap] keeps a field immediate, so it can't also have [" STR_FMT "]", STR_ARG(a->name));
            } else if (f->view_correction) {
                diag_error(a->at, "field '" STR_FMT "' already has a view correction attribute", STR_ARG(f->name));
            } else if (f->type.kind != TY_ERROR && (position ? !type_is_float_based(f->type) || type_dim(f->type) < 1
                                                             : f->type.kind != TY_FLOAT && f->type.kind != TY_QUATERNION)) {
                diag_error(a->at, position ? "[Position] works on float and floating-point vectors, not %s"
                                          : "[Rotation] works on float radians and quaternion, not %s", type_name(f->type));
            } else {
                f->view_correction = position ? 1 : 2;
            }
            continue;
        }
        if (str_eq_c(a->name, "Snap")) {
            check_snap_attribute(d, f, a);
            continue;
        }
        if (!clamp && !min && !max) {
            diag_error(a->at, "unknown field attribute '" STR_FMT "'", STR_ARG(a->name));
            suggestion s = suggest_start(a->name);
            suggest_consider_c(&s, "Clamp");
            suggest_consider_c(&s, "Min");
            suggest_consider_c(&s, "Max");
            suggest_consider_c(&s, "Snap");
            suggest_consider_c(&s, "Position");
            suggest_consider_c(&s, "Rotation");
            suggest_note(&s);
            continue;
        }
        if (d->kind != DECL_INPUT && d->kind != DECL_STRUCT && !d->is_command) {
            diag_error(a->at, "[" STR_FMT "] only works on input and struct fields for now", STR_ARG(a->name));
            diag_note("in an input, it bounds what players send: [Clamp(lo, hi)], [Min(x)] and [Max(x)]");
            continue;
        }
        const int want = clamp ? 2 : 1;
        if (a->values.count != want) {
            diag_error(a->at, clamp ? "[Clamp] takes two bounds: [Clamp(lo, hi)]"
                                    : min ? "[Min] takes one bound: [Min(x)]" : "[Max] takes one bound: [Max(x)]");
            continue;
        }
        if ((clamp && has_clamp) || (min && has_min) || (max && has_max)) {
            diag_error(a->at, "field '" STR_FMT "' already has [" STR_FMT "]", STR_ARG(f->name), STR_ARG(a->name));
            continue;
        }
        if ((clamp && (has_min || has_max)) || ((min || max) && has_clamp)) {
            diag_error(a->at, "[Clamp] already sets both bounds; use it, or [Min] and [Max]");
            continue;
        }
        has_clamp |= clamp;
        has_min |= min;
        has_max |= max;
        if (f->type.kind != TY_ERROR && !type_is_numeric(f->type) && f->type.kind != TY_LONG) {
            diag_error(a->at, "[" STR_FMT "] works on numbers and vectors, not %s", STR_ARG(a->name), type_name(f->type));
            continue;
        }
        for (int k = 0; k < a->values.count; k++) check_bound(c, f, a->values.items[k]);
    }
}

// A field's type: a built-in type or a struct.
static void resolve_field_types(const checker *c, const decl *d)
{
    for (int i = 0; i < d->fields.count; i++) {
        field *f = &d->fields.items[i];
        const loc at = f->type_qual_at.line ? f->type_qual_at : f->at;
        if (builtin_type_named(f->type_name, &f->type)) continue;
        f->type = T_ERR;
        str inner;
        if (optional_name(f->type_name, &inner)) {
            diag_error(at, "fields can't be T? yet: they always hold a value");
            diag_note("keep a bool beside it that says whether it's set, like 'bool hasTarget;'");
            continue;
        }
        if (resolve_list_type(c, f->type_name, at, &f->type)) continue;
        if (resolve_map_type(c, f->type_name, at, &f->type)) continue;
        if (resolve_grid_type(c, f->type_name, at, &f->type)) continue;
        if (str_eq_c(f->type_name, "string")) {
            f->type = (type){TY_STRING, NULL};
            continue;
        }
        if (str_eq_c(f->type_name, "Action")) {
            diag_error(at, "fields can't hold an Action; it's only ever a function's last parameter");
            continue;
        }
        if (str_eq_c(f->type_name, "DrawList")) {
            diag_error(at, "fields can't hold a DrawList; it's the frame's, which a view passes to C as 'Draw2D.list'");
            continue;
        }
        decl *const t = find_type(c, f->type_name, at);
        if (t && (t->kind == DECL_STRUCT || t->kind == DECL_ENUM)) {
            f->type = decl_type(t);
        } else if (t) {
            const char *what = t->kind == DECL_COMPONENT   ? "components"
                               : t->kind == DECL_SINGLETON ? "singletons"
                               : t->kind == DECL_EVENT     ? "events"
                                                           : "inputs";
            diag_error(at, "fields can't hold %s", what);
            diag_note("to share fields between types, declare a struct, like 'struct Name { ... }', and use it in both");
        } else if (!world_misplaced(f->type_name, at)) {
            diag_error(at, "unknown type '" STR_FMT "'", STR_ARG(f->type_name));
            suggestion s = suggest_start(f->type_name);
            suggest_builtin_types(&s);
            suggest_structs(&s, c->prog);
            suggest_note(&s);
            const fix create = {.kind = FIX_CREATE_STRUCT, .at = at, .name = f->type_name};
            vec_push(c->prog->fixes, create);
        }
    }
}

// An enum's members: their names and values. A member without a value is one
// more than the one before it, and the first is 0, as in C#. In generated C,
// Page.Title is Page_Title.
static void check_enum(checker *c, decl *d)
{
    if (d->members.count == 0) diag_error(d->at, "enum '" STR_FMT "' needs at least one member", STR_ARG(d->name));
    // What it's stored as: an int, or as C# writes it, `enum Voxel : byte`
    d->width = 4;
    int64_t most = INT32_MAX;
    if (d->backing.len) {
        if (str_eq_c(d->backing, "byte")) {
            d->width = 1;
            most = 255;
        } else if (str_eq_c(d->backing, "ushort")) {
            d->width = 2;
            most = 65535;
        } else if (!str_eq_c(d->backing, "int")) {
            diag_error(d->backing_at, "an enum is stored as a byte, a ushort or an int, not '" STR_FMT "'", STR_ARG(d->backing));
            diag_note("like 'enum Voxel : byte', for members from 0 to 255; 'ushort' goes to 65535");
        }
    }
    int64_t next = 0;
    for (int i = 0; i < d->members.count; i++) {
        enum_member *m = &d->members.items[i];
        check_reserved(m->name, m->at);
        for (int j = 0; j < i; j++) {
            if (str_eq(d->members.items[j].name, m->name)) {
                diag_error(m->at, "'" STR_FMT "' already has a member '" STR_FMT "'", STR_ARG(d->name), STR_ARG(m->name));
            }
        }
        if (m->value) {
            int64_t number;
            const type t = is_constant(c, m->value) ? check_constant_expr(c, m->value, false, T_ERR) : T_INT_;
            if (t.kind == TY_ERROR) {
                // Reported
            } else if (!type_assignable(T_INT_, t) || !fold_int(m->value, &number)) {
                diag_error(m->value->at, "a member's value is an int, like '" STR_FMT " = 3'", STR_ARG(m->name));
                diag_note("constants work too, and operators on them, like '" STR_FMT " = BASE + 1'", STR_ARG(m->name));
            } else {
                next = number;
            }
        }
        if (d->width < 4 && (next < 0 || next > most)) {
            diag_error(m->value ? m->value->at : m->at, "'" STR_FMT "' is %lld, and a %s holds 0 to %lld", STR_ARG(m->name),
                       (long long)next, d->width == 1 ? "byte" : "ushort", (long long)most);
            diag_note("store the enum as a wider type, like 'enum " STR_FMT " : %s'", STR_ARG(d->name),
                      d->width == 1 ? "ushort" : "int");
        }
        m->number = next++;
        // Other declarations' names in C, like Page_Title for a struct Page.Title
        sb mine = {0};
        c_name_of(d, &mine);
        sb_printf(&mine, "_" STR_FMT, STR_ARG(m->name));
        const decl_index *by_c_name = decls_by(c->prog, true);
        for (int k = first_c_named(by_c_name, mine.data, mine.len); k >= 0; k = by_c_name->next[k]) {
            const decl *other = c->prog->decls.items[k];
            sb theirs = {0};
            c_name_of(other, &theirs);
            if (strcmp(mine.data, theirs.data) == 0) {
                diag_error(m->at, "'" STR_FMT "." STR_FMT "' and '" STR_FMT "' would have the same name in generated C, %s",
                           STR_ARG(d->qualified), STR_ARG(m->name), STR_ARG(other->qualified), mine.data);
                diag_note("rename one of them");
            }
        }
    }
}

// Puts `d` in prog->structs after the structs it contains. A struct can't
// contain itself, even through others: it would be infinitely big. `index`
// tracks the visit: 0 not yet, 1 on the way down, 2 done.
static void order_struct(program *prog, decl *d)
{
    if (d->index == 2) return;
    d->index = 1;
    for (int i = 0; i < d->fields.count; i++) {
        field *f = &d->fields.items[i];
        if (f->type.kind != TY_STRUCT) continue;
        decl *const inner = f->type.decl;
        if (inner->index == 1) {
            if (inner == d) {
                diag_error(f->type_at, "struct '" STR_FMT "' can't contain itself", STR_ARG(d->name));
            } else {
                diag_error(f->type_at, "struct '" STR_FMT "' can't contain '" STR_FMT "': '" STR_FMT "' already contains '"
                           STR_FMT "'", STR_ARG(d->name), STR_ARG(inner->name), STR_ARG(inner->name), STR_ARG(d->name));
            }
            diag_note("it would be infinitely big");
            f->type = T_ERR; // Breaks the loop for everything that walks fields
            continue;
        }
        order_struct(prog, inner);
    }
    d->index = 2;
    vec_push(prog->structs, d);
}

// Whether a value of type `t` has a list in it, or is one.
static bool holds_list(const type t)
{
    if (t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET) return true;
    if (t.kind != TY_STRUCT) return false;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (holds_list(t.decl->fields.items[i].type)) return true;
    }
    return false;
}

// Whether a value of type `t` has a grid in it, or is one: a component's or a
// singleton's, as only those hold grids.
static bool holds_grid(const type t)
{
    if (t.kind == TY_GRID) return true;
    if (t.kind != TY_COMPONENT && t.kind != TY_SINGLETON) return false;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (t.decl->fields.items[i].type.kind == TY_GRID) return true;
    }
    return false;
}

// Whether a value of type `t` has text in it, in its own fields or its structs'.
static bool holds_text(const type t)
{
    if (t.kind == TY_STRING) return true;
    if (t.kind != TY_STRUCT) return false;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (holds_text(t.decl->fields.items[i].type)) return true;
    }
    return false;
}

static void check_fields(checker *c, const decl *d)
{
    for (int i = 0; i < d->fields.count; i++) {
        field *f = &d->fields.items[i];
        if (f->hidden) continue;
        if (d->is_command && (holds_list(f->type) || f->type.kind == TY_GRID)) {
            diag_error(f->type_at.line ? f->type_at : f->at, "a command can't hold a list, map, set or grid");
            diag_note("send numbers, bools, enums, strings or structs containing those");
        }
        if (holds_render_resource(f->type)) {
            const loc at = f->type_qual_at.line ? f->type_qual_at : f->type_at.line ? f->type_at : f->at;
            if (d->kind != DECL_STRUCT && !d->is_local) {
                diag_error(at, "'" STR_FMT "' can't hold rendering resources in match state", STR_ARG(d->name));
                diag_note("Texture and RenderTarget belong to this machine: keep the field local, or use a local component or singleton");
            } else if (f->type.kind == TY_LIST || f->type.kind == TY_MAP || f->type.kind == TY_SET || f->type.kind == TY_GRID) {
                diag_error(at, "containers can't hold rendering resources, including inside a struct");
            }
        }
        if (holds_heap(f->type)) {
            if (d->kind == DECL_INPUT) {
                const loc at = f->type_qual_at.line ? f->type_qual_at : f->at;
                diag_error(at, holds_text(f->type) ? "an input can't hold text" : "an input can't hold a list");
                diag_note("players send what they do each tick, as numbers, bools and enums");
            } else if (d->part_of) {
                c->prog->local_heap = true; // The match's local fields: the local world's heap alone, as they're outside the match
            } else {
                c->prog->uses_heap = true; // Worlds keep their text and lists in a heap
            }
        }
        if (f->type.kind == TY_GRID) {
            if (d->part_of) c->prog->local_heap = true;
            else c->prog->uses_heap = true; // Grids keep their chunks in the heap
            if (d->kind != DECL_COMPONENT && d->kind != DECL_SINGLETON) {
                const loc at = f->type_qual_at.line ? f->type_qual_at : f->at;
                diag_error(at, "%s can't hold a grid", d->kind == DECL_STRUCT  ? "a struct"
                                                       : d->kind == DECL_EVENT ? "an event"
                                                       : d->kind == DECL_INPUT ? "an input"
                                                                               : "this");
                diag_note("grids stay where they're made: in components, singletons and scenes");
            }
        }
        if (f->type.kind == TY_LIST && holds_list(list_element(f->type))) {
            const loc at = f->type_qual_at.line ? f->type_qual_at : f->at;
            diag_error(at, "a list can't hold '" STR_FMT "': it has a list or a map in it", STR_ARG(list_element(f->type).decl->name));
            diag_note("lists and maps can't hold lists or maps yet");
        }
        check_reserved(f->name, f->at);
        for (int j = 0; j < i; j++) {
            if (str_eq(d->fields.items[j].name, f->name)) {
                diag_error(f->at, "field '" STR_FMT "' is declared twice", STR_ARG(f->name));
            }
        }
        if (f->default_value) check_default(c, f);
        check_field_attributes(c, d, f);
        if (f->type.kind == TY_LOCAL_ENTITY && (!d->is_local || d->kind == DECL_STRUCT)) {
            const loc at = f->type_qual_at.line ? f->type_qual_at : f->type_at.line ? f->type_at : f->at;
            if (d->kind == DECL_STRUCT) {
                diag_error(at, "structs belong to neither side, so they can't hold a LocalEntity");
                diag_note("keep it in a local component or singleton");
            } else {
                diag_error(at, "'" STR_FMT "' belongs to the match, so it can't hold a LocalEntity", STR_ARG(d->name));
                diag_note("local entities only exist on this machine");
            }
        }
    }
}

// A local part's fields share their component's or singleton's names: one
// name, one field. And its struct in generated C, <Name>_local, is no other
// declaration's.
static void check_local_part(const checker *c, const decl *part)
{
    const decl *of = part->part_of;
    for (int i = 0; i < part->fields.count; i++) {
        const field *f = &part->fields.items[i];
        for (int j = 0; j < of->fields.count; j++) {
            if (str_eq(of->fields.items[j].name, f->name)) diag_error(f->at, "field '" STR_FMT "' is declared twice", STR_ARG(f->name));
        }
        for (int j = 0; j < of->methods.count; j++) {
            if (str_eq(of->methods.items[j]->name, f->name)) {
                diag_error(f->at, "'" STR_FMT "' is already a method of '" STR_FMT "'", STR_ARG(f->name), STR_ARG(of->name));
            }
        }
    }
    sb mine = {0};
    c_name_of(of, &mine);
    sb_put(&mine, "_local");
    const decl_index *by_c_name = decls_by(c->prog, true);
    for (int k = first_c_named(by_c_name, mine.data, mine.len); k >= 0; k = by_c_name->next[k]) {
        const decl *other = c->prog->decls.items[k];
        sb theirs = {0};
        c_name_of(other, &theirs);
        if (strcmp(mine.data, theirs.data) != 0 || other->kind == DECL_SYSTEM) continue;
        diag_error(other->at, "'" STR_FMT "' and the local fields of '" STR_FMT "' would have the same name in generated C, %s",
                   STR_ARG(other->qualified), STR_ARG(of->qualified), mine.data);
        diag_note("rename one of them");
    }
}

// A method's or function's parameter or return type: a built-in type, struct,
// enum or component; singletons only as parameters, and void only as a return.
static type method_type(const checker *c, const str name, const loc at, const bool is_return)
{
    type t;
    str inner;
    if (optional_name(name, &inner)) return optional_type(c, method_type(c, inner, at, is_return), at); // int?
    if (is_return && str_eq_c(name, "void")) return T_VOID_;
    if (builtin_type_named(name, &t)) return t;
    if (resolve_list_type(c, name, at, &t)) return t;
    if (resolve_map_type(c, name, at, &t)) return t;
    if (resolve_grid_type(c, name, at, &t)) {
        if (t.kind == TY_ERROR) return t;
        diag_error(at, "functions can't %s grids yet", is_return ? "return" : "take");
        diag_note("change a grid's cells in the system that has its component or singleton");
        return T_ERR;
    }
    if (str_eq_c(name, "string")) return (type){TY_STRING, NULL};
    if (str_eq_c(name, "Action")) {
        if (!is_return) return (type){TY_ACTION, NULL};
        diag_error(at, "a function can't return an Action; it takes one, as its last parameter");
        return T_ERR;
    }
    if (str_eq_c(name, "DrawList")) {
        if (!is_return) return (type){TY_DRAW_LIST, NULL};
        diag_error(at, "a function can't return a DrawList; an extern function takes one, passed 'Draw2D.list'");
        return T_ERR;
    }
    for (int i = 0; i < c->prog->records.count; i++) { // Devices, Keyboard, ..., Button
        if (str_eq(c->prog->records.items[i]->name, name)) return (type){TY_RECORD, c->prog->records.items[i]};
    }
    decl *const d = find_type(c, name, at);
    if (d && (d->kind == DECL_STRUCT || d->kind == DECL_COMPONENT || d->kind == DECL_ENUM
              || (!is_return && d->kind == DECL_SINGLETON))) return decl_type(d);
    if (d) {
        if (d->kind == DECL_SINGLETON) diag_error(at, "functions and methods can't return singletons");
        else diag_error(at, "methods take and return built-in types, structs, enums and components, not %s",
                        d->kind == DECL_EVENT ? "events" : "inputs");
    } else if (str_eq_c(name, "void")) {
        diag_error(at, "'void' only goes before a method that returns nothing");
    } else if (!world_misplaced(name, at)) {
        diag_error(at, "unknown type '" STR_FMT "'", STR_ARG(name));
        suggestion s = suggest_start(name);
        suggest_builtin_types(&s);
        suggest_structs(&s, c->prog);
        if (is_return) suggest_consider_c(&s, "void");
        suggest_note(&s);
        const fix create = {.kind = FIX_CREATE_STRUCT, .at = at, .name = name};
        vec_push(c->prog->fixes, create);
    }
    return T_ERR;
}

// `fails ParseError` after a function's or method's parameters: the error it
// can end with, which makes what a call gives `T fails ParseError`.
static void check_fails(const checker *c, decl *m)
{
    m->fails = T_VOID_;
    m->result = m->return_type;
    if (m->fails_name.len == 0) return;
    const loc at = m->fails_type_qual_at.line ? m->fails_type_qual_at : m->fails_at;
    const type error = method_type(c, m->fails_name, at, false);
    if (error.kind == TY_ERROR || m->return_type.kind == TY_ERROR) return;
    if (m->is_operator) {
        diag_error(m->fails_at, "operators can't fail");
        diag_note("a method or function can: 'Money Parse(string text) fails ParseError'");
    } else if (m->is_extern) {
        diag_error(m->fails_at, "C functions can't fail: they return what C returns");
        diag_note("check what C returns in a Tide function that fails, and call that");
    } else if (m->takes_action) {
        diag_error(m->fails_at, "a function that takes an Action can't fail yet");
    } else if (error.kind == TY_OPTIONAL) {
        diag_error(at, "an error is a type of its own, not a T?");
        diag_note("an enum names each way it can fail: 'enum ParseError { Empty, NotANumber }'");
    } else if (m->return_type.kind == TY_OPTIONAL) {
        diag_error(m->fails_at, "a function that can fail can't return a T? too");
        diag_note("fail when there's nothing, or return '%s' without 'fails'", type_name(m->return_type));
    } else if (same_type(error, m->return_type)) {
        diag_error(at, "'" STR_FMT "' can't fail with %s, the type it returns: 'is' couldn't tell them apart",
                   STR_ARG(m->name), type_name(error));
        diag_note("give the error a type of its own, like 'enum ParseError { Empty, NotANumber }'");
    } else if (result_value_ok(error, at, "an error") && result_value_ok(m->return_type, m->return_type_qual_at, "the value of a function that can fail")) {
        m->fails = error;
        m->result = result_of(c->prog, m->return_type, error, false);
    }
}

// The types in a method's or function's signature.
static void check_signature(const checker *c, decl *m)
{
    m->return_type = method_type(c, m->return_type_name, m->return_type_qual_at, true);
    if (holds_render_resource(m->return_type)) {
        m->render_resources = true;
        m->render_resources_at = m->return_type_at;
    }
    for (int k = 0; k < m->params.count; k++) {
        param *p = &m->params.items[k];
        check_reserved(p->name, p->at);
        for (int j = 0; j < k; j++) {
            if (str_eq(m->params.items[j].name, p->name)) {
                diag_error(p->name_at, "parameter '" STR_FMT "' is declared twice", STR_ARG(p->name));
            }
        }
        p->type = method_type(c, p->type_name, p->type_qual_at, false);
        if (holds_render_resource(p->type)) {
            if (!m->render_resources) m->render_resources_at = p->type_at;
            m->render_resources = true;
        }
        if (p->mode == PARAM_IN && !m->is_extern) {
            diag_error(p->at, "only extern functions take 'in' parameters: C gets a read-only pointer to the value");
            diag_note("a Tide %s's parameters are read-only already; drop 'in'", m->owner ? "method" : "function");
            p->mode = PARAM_READ;
        }
        if (p->type.kind == TY_RECORD && p->mode == PARAM_MUT) {
            diag_error(p->at, "devices can only be read, so '" STR_FMT "' can't be 'mut'", STR_ARG(p->name));
        }
        if (p->type.kind == TY_DRAW_LIST && !m->is_extern) {
            diag_error(p->type_at, "only extern functions take a DrawList: C draws into it with tide/draw.h");
            diag_note("a Tide function draws with Draw2D or Draw3D themselves, like 'Draw2D.Mesh(vertices, indices);'");
            p->type = T_ERR;
        } else if (p->type.kind == TY_DRAW_LIST && p->mode != PARAM_READ) {
            diag_error(p->at, "a DrawList goes to C as it is, a 'tide_draw_list *'; drop '%s'",
                       p->mode == PARAM_MUT ? "mut" : "in");
            p->mode = PARAM_READ;
        }
        if (p->type.kind != TY_ACTION) continue;
        if (m->kind != DECL_FUNCTION) {
            diag_error(p->type_at, "only functions take an Action, not methods");
        } else if (k != m->params.count - 1) {
            diag_error(p->type_at, "an Action is always the last parameter");
            diag_note("its code is written after the call, in braces: 'Section(\"Audio\") { ... }'");
        } else if (p->mode == PARAM_MUT) {
            diag_error(p->at, "an Action can't be 'mut'; it's code, run with '" STR_FMT "();'", STR_ARG(p->name));
        } else {
            m->takes_action = true;
        }
    }
    if (m->takes_action && m->return_type.kind != TY_VOID && m->return_type.kind != TY_ERROR) {
        diag_error(m->return_type_qual_at, "a function that takes an Action returns nothing");
        diag_note("its call is a statement, with the block after it; change what the caller passes as 'mut' instead");
        m->takes_action = false;
    }
    check_fails(c, m);
}

static bool is_comparison(const tok_kind op)
{
    return op == T_EQ || op == T_NE || op == T_LT || op == T_LE || op == T_GT || op == T_GE;
}

// Whether two operators take the same types.
static bool same_params(const decl *a, const decl *b)
{
    if (a->params.count != b->params.count) return false;
    for (int i = 0; i < a->params.count; i++) {
        if (!same_type(a->params.items[i].type, b->params.items[i].type)) return false;
    }
    return true;
}

// `Money operator +(Money a, Money b)`: in a struct, taking it, with C#'s rules.
static void check_operator_decl(const checker *c, const decl *d, decl *m, const int index)
{
    if (d->kind != DECL_STRUCT) diag_error(m->at, "operators go in structs");
    check_signature(c, m);
    const bool one = m->op == T_NOT || m->op == T_TILDE;
    const int n = m->params.count;
    if (one && n != 1) {
        diag_error(m->at, "'" STR_FMT "' takes one parameter", STR_ARG(m->name));
    } else if (m->op == T_MINUS && n != 1 && n != 2) {
        diag_error(m->at, "'operator -' takes one parameter, to negate, or two, to subtract");
    } else if (!one && m->op != T_MINUS && n != 2) {
        diag_error(m->at, "'" STR_FMT "' takes two parameters", STR_ARG(m->name));
    }
    bool takes_it = false;
    for (int i = 0; i < n; i++) {
        const param *p = &m->params.items[i];
        if (p->type.kind == TY_ERROR || same_type(p->type, (type){TY_STRUCT, (decl *)d})) takes_it = true;
        if (p->mode == PARAM_MUT) diag_error(p->at, "operator parameters can't be 'mut'");
    }
    if (!takes_it && n > 0) {
        diag_error(m->at, "an operator of '" STR_FMT "' takes a '" STR_FMT "'", STR_ARG(d->name), STR_ARG(d->name));
        diag_note("at least one of its parameters is the struct it's in");
    }
    if (is_comparison(m->op) && m->return_type.kind != TY_BOOL && m->return_type.kind != TY_ERROR) {
        diag_error(m->return_type_qual_at, "'" STR_FMT "' compares, so it returns bool", STR_ARG(m->name));
    }
    for (int j = 0; j < index; j++) {
        const decl *other = d->methods.items[j];
        if (other->is_operator && other->op == m->op && same_params(other, m)) {
            diag_error(m->at, "'" STR_FMT "' is already declared for these types", STR_ARG(m->name));
        }
    }
}

// As in C#, == and != come in pairs, and so do < and >, and <= and >=.
static void check_operator_pairs(const decl *d)
{
    static const tok_kind pairs[][2] = {{T_EQ, T_NE}, {T_NE, T_EQ}, {T_LT, T_GT}, {T_GT, T_LT}, {T_LE, T_GE}, {T_GE, T_LE}};
    for (int i = 0; i < d->methods.count; i++) {
        const decl *m = d->methods.items[i];
        if (!m->is_operator) continue;
        for (size_t k = 0; k < sizeof pairs / sizeof pairs[0]; k++) {
            if (m->op != pairs[k][0]) continue;
            bool found = false;
            for (int j = 0; j < d->methods.count && !found; j++) {
                const decl *other = d->methods.items[j];
                found = other->is_operator && other->op == pairs[k][1] && same_params(other, m);
            }
            if (!found) {
                diag_error(m->at, "'" STR_FMT "' needs a matching 'operator %s', as in C#", STR_ARG(m->name),
                           op_symbol(pairs[k][1]));
                diag_note("declare 'bool operator %s' with the same parameters", op_symbol(pairs[k][1]));
            }
        }
    }
}

// `Angle Interpolate(Angle from, Angle to, float t)`: how views blend values
// of its type between the last two ticks, instead of field by field. Like an
// operator, it has no value of its own.
static void check_interpolate_decl(const checker *c, decl *d, decl *m)
{
    check_signature(c, m);
    m->is_interpolate = true;
    if (m->fails.kind != TY_VOID) {
        diag_error(m->fails_at, "Interpolate can't fail: views blend with whatever it returns");
        m->fails = T_VOID_;
        m->result = m->return_type;
    }
    const type self = d->kind == DECL_COMPONENT ? (type){TY_COMPONENT, d} : (type){TY_STRUCT, d};
    const bool ok = m->params.count == 3 && same_type(m->params.items[0].type, self) && same_type(m->params.items[1].type, self)
                 && m->params.items[2].type.kind == TY_FLOAT && same_type(m->return_type, self) && !m->is_mut_method;
    bool mut = false;
    for (int i = 0; i < m->params.count; i++) mut |= m->params.items[i].mode == PARAM_MUT;
    if (!ok || mut) {
        diag_error(m->at, "Interpolate takes last tick's value, this tick's, and how far between them, and returns "
                          "the value between: '" STR_FMT " Interpolate(" STR_FMT " from, " STR_FMT " to, float t)'",
                   STR_ARG(d->name), STR_ARG(d->name), STR_ARG(d->name));
        return;
    }
    if (d->interpolate) {
        diag_error(m->at, "'" STR_FMT "' already has an Interpolate", STR_ARG(d->name));
        return;
    }
    d->interpolate = m;
}

// A custom interpolation replaces the whole type's field traversal. Correction
// roles below it would otherwise be accepted but silently never applied.
static const field *correction_field(const decl *d, const int depth)
{
    if (depth > 64) return NULL; // A recursive type has its own diagnostic.
    for (int i = 0; i < d->fields.count; i++) {
        const field *f = &d->fields.items[i];
        if (f->view_correction) return f;
        if (f->type.kind == TY_STRUCT) {
            const field *nested = correction_field(f->type.decl, depth + 1);
            if (nested) return nested;
        }
    }
    return NULL;
}

static void check_correction_interpolates(const program *prog)
{
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (!d->interpolate) continue;
        const field *f = correction_field(d, 0);
        if (!f) continue;
        diag_error(f->at, "view correction attributes under '" STR_FMT "' are not supported with its custom Interpolate",
                   STR_ARG(d->name));
        diag_note("custom interpolation replaces the whole type; correction smoothing for custom Interpolate is deferred");
    }
}

// Where methods go (structs and components), their names, and their signatures.
static void check_method_decls(const checker *c, decl *d)
{
    for (int i = 0; i < d->methods.count; i++) {
        decl *m = d->methods.items[i];
        if (m->is_operator) {
            check_operator_decl(c, d, m, i);
            continue;
        }
        if (str_eq_c(m->name, "Interpolate") && (d->kind == DECL_STRUCT || d->kind == DECL_COMPONENT)) {
            check_interpolate_decl(c, d, m);
            continue;
        }
        if (d->kind != DECL_STRUCT && d->kind != DECL_COMPONENT) {
            diag_error(m->at, "methods work on structs and components");
            if (d->kind == DECL_INPUT) diag_note("an input has Sample and Sanitize");
            else diag_note("put the data and its methods in a struct, and the struct in the singleton");
        }
        check_reserved(m->name, m->at);
        if (field_named(d, m->name)) {
            diag_error(m->at, "'" STR_FMT "' already has a field '" STR_FMT "'", STR_ARG(d->name), STR_ARG(m->name));
        }
        for (int j = 0; j < i; j++) {
            if (str_eq(d->methods.items[j]->name, m->name)) {
                diag_error(m->at, "'" STR_FMT "' already has a method '" STR_FMT "'", STR_ARG(d->name), STR_ARG(m->name));
                diag_note("methods can't share a name, even with different parameters");
            }
        }
        check_signature(c, m);
    }
    if (diag_error_count() == 0) check_operator_pairs(d);
}

static void c_name_of(const decl *d, sb *out);

// C's keywords, which can't name a C function.
static const char *const c_keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern",
    "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "alignas",
    "alignof", "bool", "constexpr", "false", "nullptr", "static_assert", "thread_local", "true", "typeof",
    "typeof_unqual", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn",
    "_Static_assert", "_Thread_local",
};

// Why C can't take a value of type `t`, or NULL if it can: plain data, with
// nothing in the world's heap.
static const char *c_refuses(const type t)
{
    switch (t.kind) {
    case TY_STRING: return "text";
    case TY_LIST: return "lists";
    case TY_MAP: return "maps";
    case TY_SET: return "sets";
    case TY_ACTION: return "an Action";
    case TY_RECORD: return "the devices";
    case TY_SINGLETON: return "singletons";
    case TY_OPTIONAL: return "T? values";
    case TY_STRUCT:
    case TY_COMPONENT: return decl_holds_text(t.decl) ? "text or lists" : NULL;
    default: return NULL;
    }
}

// The C function an extern names: [NativeName("f")], or its own name.
static const char *extern_c_name(const decl *fn)
{
    const attribute *native = NULL;
    for (int i = 0; i < fn->attributes.count; i++) {
        const attribute *a = &fn->attributes.items[i];
        if (!str_eq_c(a->name, "NativeName")) continue;
        if (native) {
            diag_error(a->at, "'" STR_FMT "' already has a NativeName", STR_ARG(fn->name));
            continue;
        }
        native = a;
        if (a->values.count != 1 || a->args.count != 0 || a->values.items[0]->kind != E_STRING) {
            diag_error(a->at, "NativeName takes the C function's name, in quotes: [NativeName(\"stb_perlin_noise3\")]");
            return NULL;
        }
    }
    const str name = native ? native->values.items[0]->text : fn->name;
    const loc at = native ? native->values.items[0]->at : fn->at;
    bool identifier = name.len > 0 && !(name.ptr[0] >= '0' && name.ptr[0] <= '9');
    for (int i = 0; i < name.len; i++) {
        const char ch = name.ptr[i];
        identifier &= (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
    }
    if (!identifier) {
        diag_error(at, "'" STR_FMT "' isn't a C function's name", STR_ARG(name));
        diag_note("C names are letters, digits and '_', and don't start with a digit");
        return NULL;
    }
    for (size_t i = 0; i < sizeof c_keywords / sizeof c_keywords[0]; i++) {
        if (!str_eq_c(name, c_keywords[i])) continue;
        diag_error(at, "'" STR_FMT "' is a keyword in C, so no C function has that name", STR_ARG(name));
        if (!native) diag_note("give the C function's name with [NativeName(\"...\")]");
        return NULL;
    }
    if (native && str_starts_with_c(name, "tide_")) {
        diag_error(at, "names starting with 'tide_' belong to the engine");
        return NULL;
    }
    return str_to_cstr(name);
}

// An extern function's parameter: plain data, by value, or by address when
// it's `mut` or `in`; text as C text; or a list as a pointer to its elements,
// which are plain data.
static void check_extern_param(const checker *c, decl *fn, const param *p)
{
    if (p->type.kind == TY_STRING) {
        c->prog->uses_text = true; // C text that isn't already can be a copy
        if (p->mode == PARAM_MUT) {
            diag_error(p->at, "C can't change text, so a string it takes can't be 'mut'");
            diag_note("return the new text instead, as C's 'const char *': 'extern string Name(...);'");
        } else if (p->mode == PARAM_IN) {
            diag_error(p->at, "text already goes to C by address, as a 'const char *'; drop 'in'");
        }
        return;
    }
    if (p->type.kind == TY_LIST) {
        const type element = list_element(p->type);
        const char *refused = element.kind == TY_STRING ? "text" : c_refuses(element);
        if (refused) {
            diag_error(p->type_at, "C functions can't take lists of %s yet", refused);
            diag_note("a list goes to C as a pointer to its elements: numbers, vectors, enums, and structs of them");
        } else if (p->mode == PARAM_IN) {
            diag_error(p->at, "a list already goes to C by address, as a pointer to its elements; drop 'in'");
        }
        return;
    }
    const char *refused = c_refuses(p->type);
    if (!refused) return;
    diag_error(p->type_at, "C functions can't take %s yet", refused);
    if (p->type.kind == TY_ACTION) diag_note("an Action is Tide code, which only Tide functions run");
    else diag_note("pass numbers, vectors, enums, text, lists, and structs of them");
    fn->takes_action = false; // It's no longer inlined
}

// `extern float Noise(float x);`: a function whose code is in C. It takes and
// returns plain data, a `mut` or `in` parameter by address, text as C text
// and lists as pointers to their elements, and its C name must be its own.
static void check_extern_decl(const checker *c, decl *fn)
{
    for (int i = 0; i < fn->params.count; i++) check_extern_param(c, fn, &fn->params.items[i]);
    if (fn->return_type.kind == TY_STRING) {
        c->prog->uses_text = true; // Copied into the scratch area
    } else if (fn->return_type.kind == TY_LIST) {
        diag_error(fn->return_type_at, "C functions can't return lists");
        diag_note("C can fill the elements of a list the game passes it as 'mut List<T>'");
    } else if (c_refuses(fn->return_type)) {
        diag_error(fn->return_type_at, "C functions can't return %s yet", c_refuses(fn->return_type));
        diag_note("return numbers, vectors, enums, text, or structs of them");
    }
    fn->c_name = extern_c_name(fn);
    if (!fn->c_name) return;
    for (int i = 0; i < c->prog->decls.count; i++) {
        const decl *other = c->prog->decls.items[i];
        if (other == fn) break;
        if (other->is_extern && other->c_name && strcmp(other->c_name, fn->c_name) == 0) {
            diag_error(fn->at, "'" STR_FMT "' and '" STR_FMT "' are the same C function, %s", STR_ARG(fn->qualified),
                       STR_ARG(other->qualified), fn->c_name);
            diag_note("declare it once");
            return;
        }
    }
    const decl_index *by_c_name = decls_by(c->prog, true);
    for (int i = first_c_named(by_c_name, fn->c_name, strlen(fn->c_name)); i >= 0; i = by_c_name->next[i]) {
        const decl *other = c->prog->decls.items[i];
        if (other->kind == DECL_FUNCTION || other->kind == DECL_SYSTEM || other->builtin) continue;
        sb theirs = {0};
        c_name_of(other, &theirs);
        if (strcmp(theirs.data, fn->c_name) != 0) continue;
        diag_error(fn->at, "the C function %s has the same name as '" STR_FMT "' in generated C", fn->c_name,
                   STR_ARG(other->qualified));
        diag_note("rename '" STR_FMT "'", STR_ARG(other->qualified));
        return;
    }
}

// An async function's parameters: copies of values, which its task keeps,
// and components and singletons, which it gets again each time it goes on (a
// world's pages move as its snapshots copy them), so a task never holds an
// address. Local state and changing the match decide its world.
static void check_async_decl(decl *fn)
{
    if (fn->is_extern) {
        diag_error(fn->async_at, "C functions can't be async: C runs to its end");
        fn->is_async = false;
        return;
    }
    if (fn->takes_action) diag_error(fn->async_at, "a function that takes an Action can't be async yet");
    for (int i = 0; i < fn->params.count; i++) {
        param *p = &fn->params.items[i];
        if (p->type.kind == TY_COMPONENT || p->type.kind == TY_SINGLETON) {
            p->task_ref = true;
            const decl *d = p->type.decl;
            if (d->is_local) note_side(fn, TASK_LOCAL, p->type_at.line ? p->type_at : p->at);
            else if (p->mode == PARAM_MUT) note_side(fn, TASK_MATCH, p->at);
            continue;
        }
        if (p->type.kind == TY_RECORD) {
            diag_error(p->at, "a task runs between frames, so it can't take the devices");
            diag_note("pass it what it needs of them instead, like 'bool jumped'");
        } else if (p->mode == PARAM_MUT && p->type.kind != TY_ERROR) {
            diag_error(p->at, "a task can't keep the caller's '" STR_FMT "' while it waits, so it can't change it",
                       STR_ARG(p->name));
            diag_note("return the new value, or take the component or singleton it's in");
            p->mode = PARAM_READ;
        }
    }
}

static void check_function_decl(const checker *c, decl *fn)
{
    if (fn->is_mut_method) {
        diag_error(fn->at, "only methods are 'mut': they change their struct's fields");
        diag_note("a function changes what's passed to its 'mut' parameters, like 'void Heal(mut Stats stats)'");
    }
    check_signature(c, fn);
    if (fn->is_extern) check_extern_decl(c, fn);
    if (fn->is_async) check_async_decl(fn);
}

// What a frag or vert takes that isn't the game's state: a plain value.
static void refuse_shader_value(const decl *d, const param *p)
{
    const char *word = shader_word(d);
    diag_error(p->type_at.line ? p->type_at : p->at, "a %s takes no plain values: '" STR_FMT "' is %s", word, STR_ARG(p->name),
               type_name(p->type));
    diag_note("what a %s reads lives in a component, the entity's, or in a singleton, which it takes as a parameter and "
              "the engine supplies: 'singleton Sun { float3 direction; }' and '%s " STR_FMT "(..., Sun sun)'",
              word, word, STR_ARG(d->name));
}

// frag Shade(BoxVertex vertex, mut Color color, Box box, Sun sun) and
// vert Wave(mut BoxVertex vertex, Time time): the vertex first, a struct; a
// frag's pixel, its one `mut Color`; then what the engine supplies as it does
// to a view: the entity's components, singletons of either side, and Pixel
// and Instance.
static void check_shader_decl(const checker *c, decl *d)
{
    const program *prog = c->prog;
    const char *word = shader_word(d);
    const char *vertex_form = d->is_frag ? "frag Name(BoxVertex vertex, mut Color color, ...)" : "vert Name(mut BoxVertex vertex, ...)";
    d->return_type = T_VOID_;
    d->result = T_VOID_;
    d->fails = T_VOID_;
    const param *color = NULL;
    const param *instance_data = NULL;
    const param *source_texture = NULL;
    for (int i = 0; i < d->params.count; i++) {
        param *p = &d->params.items[i];
        const loc at = p->type_at.line ? p->type_at : p->at;
        p->type = T_ERR;
        if (p->verified) {
            diag_error(p->verified_at, "a %s reads what the view that draws its mesh is given, so 'verified' isn't its to say", word);
            diag_note("a view of the confirmed world's entities, whose components are all 'verified', draws them as confirmed");
            p->verified = false;
        }
        if (p->mode == PARAM_WITH || p->mode == PARAM_WITHOUT) {
            diag_error(p->at, "a %s has no entities to filter: it takes what it reads, and the view that draws the mesh says "
                              "which entities", word);
            continue;
        }
        if (str_eq_c(p->type_name, "World") && !find_type(c, p->type_name, p->type_at)) {
            diag_error(at, "a %s can't reach other entities: the GPU is sent the components of the one being drawn", word);
            diag_note("the view copies what it needs of the others into a component of this entity, or into a singleton");
            continue;
        }
        if (p->name.len > 0) {
            check_reserved(p->name, p->at);
            for (int j = 0; j < i; j++) {
                if (str_eq(d->params.items[j].name, p->name)) {
                    diag_error(p->name_at, "parameter '" STR_FMT "' is declared twice", STR_ARG(p->name));
                }
            }
        }
        decl *t = find_type(c, p->type_name, p->type_at);
        type value = T_ERR;
        const bool plain = !t && builtin_type_named(p->type_name, &value);

        if (i == 0) { // The vertex
            if (!t || t->kind != DECL_STRUCT) {
                diag_error(at, "a %s's first parameter is the vertex it runs on, a struct, and '" STR_FMT "' is %s", word,
                           STR_ARG(p->type_name), t ? decl_what(t) : plain ? type_name(value) : "unknown");
                diag_note("like '%s': the struct is what the mesh's list holds, a 'List<BoxVertex>'", vertex_form);
                continue;
            }
            if (vertex_dims(t, at, true) == 0) continue;
            p->type = decl_type(t);
            p->function_param = true;
            if (d->is_frag && p->mode == PARAM_MUT) {
                diag_error(p->at, "a frag reads its vertex, blended across the triangle; a vert changes it");
                diag_note("drop 'mut'; the pixel it writes is its 'mut Color' parameter");
                p->mode = PARAM_READ;
            } else if (!d->is_frag && p->mode != PARAM_MUT) {
                diag_error(p->at, "a vert changes its vertex in place, so it takes it 'mut': '%s'", vertex_form);
                p->mode = PARAM_MUT;
            }
            continue;
        }

        if (plain || (t && (t->kind == DECL_STRUCT || t->kind == DECL_ENUM))) {
            if (t) value = decl_type(t);
            p->type = value;
            p->function_param = true;
            if (value.kind == TY_TEXTURE) {
                if (source_texture) {
                    diag_error(p->at, "a %s takes one explicit Texture source, and this names two", word);
                } else {
                    source_texture = p;
                }
                if (p->mode == PARAM_MUT) {
                    diag_error(p->at, "a %s reads its Texture source: drop 'mut'", word);
                    p->mode = PARAM_READ;
                }
                p->function_param = false;
            } else if (d->is_frag && p->mode == PARAM_MUT && value.kind == TY_COLOR && !color) {
                color = p; // The pixel it writes
            } else if (t && t->kind == DECL_STRUCT && p->mode != PARAM_MUT) {
                if (instance_data) {
                    const type vertex = d->params.items[0].type;
                    const bool three_d = vertex.kind == TY_STRUCT && shader_dims(vertex.decl) == 3;
                    diag_error(p->at, "a %s takes one instance struct, supplied by Draw%dD.Instanced, and this names two",
                               word, three_d ? 3 : 2);
                } else {
                    instance_data = p;
                }
                p->function_param = false; // Supplied by each element of the explicit instance list
            } else if (d->is_frag && p->mode == PARAM_MUT) {
                diag_error(p->at, value.kind == TY_COLOR ? "a frag writes one color, its pixel's, for now"
                                                         : "a frag writes its pixel's color, a 'mut Color', for now");
                diag_note("%s isn't there yet",
                          value.kind == TY_COLOR ? "a second output" : "an output of another type than Color");
            } else if (p->mode == PARAM_MUT) {
                diag_error(p->at, "a vert changes its vertex, its first parameter, and nothing else");
                diag_note("put what it works out in a field of the vertex, which the frag reads blended");
            } else {
                refuse_shader_value(d, p);
            }
            continue;
        }
        if (!t && str_eq_c(p->type_name, "Pixel")) {
            if (!d->is_frag) {
                diag_error(at, "a vert runs for a vertex, before there are pixels: 'Pixel' is a frag's");
                continue;
            }
            t = prog->pixel;
        } else if (!t && str_eq_c(p->type_name, "Instance")) {
            t = prog->instance;
        }
        if (!t) {
            diag_error(at, "unknown component or singleton '" STR_FMT "'", STR_ARG(p->type_name));
            suggestion s = suggest_start(p->type_name);
            suggest_decls(&s, prog, true, true, false);
            suggest_consider_c(&s, "Instance");
            if (d->is_frag) suggest_consider_c(&s, "Pixel");
            suggest_note(&s);
            continue;
        }
        if (t->kind != DECL_COMPONENT && t->kind != DECL_SINGLETON && t->kind != DECL_RECORD) {
            diag_error(at, "'" STR_FMT "' is %s; a %s takes components and singletons", STR_ARG(t->name), decl_what(t), word);
            continue;
        }
        for (int j = 0; j < i; j++) {
            if (d->params.items[j].type.decl == t) diag_error(p->at, "'" STR_FMT "' appears twice", STR_ARG(t->name));
        }
        p->type = decl_type(t);
        if (p->mode == PARAM_MUT) {
            diag_error(p->at, "a %s reads '" STR_FMT "', and can't change it: drop 'mut'", word, STR_ARG(t->name));
            diag_note(d->is_frag ? "what it writes is its pixel's color, 'mut Color color'" : "what it changes is its vertex");
            p->mode = PARAM_READ;
        }
    }
    if (d->params.count == 0) {
        diag_error(d->at, "a %s takes the vertex it runs on first: '%s'", word, vertex_form);
    } else if (d->is_frag && !color) {
        diag_error(d->at, "frag '" STR_FMT "' has no pixel to write: it takes the pixel's color as 'mut Color color'", STR_ARG(d->name));
        diag_note("like 'frag " STR_FMT "(" STR_FMT " " STR_FMT ", mut Color color, ...) { color = ...; }': what 'color' holds "
                  "when it ends is the pixel", STR_ARG(d->name), STR_ARG(d->params.items[0].type_name),
                  STR_ARG(d->params.items[0].name));
    }
}

static void check_method_body(checker *c, decl *m)
{
    c->method = m;
    c->system = m; // For its parameters
    c->unit = m->unit;
    check_stmt(c, m->body);
    const type_kind ret = m->return_type.kind;
    if (ret != TY_VOID && ret != TY_ERROR && !always_returns(m->body)) {
        diag_error(m->at, "'" STR_FMT "' doesn't return a value on every path", STR_ARG(m->name));
        if (m->fails.kind != TY_VOID) {
            diag_note("end every path with 'return value;' or 'fail error;', as it returns %s or fails with %s",
                      type_name(m->return_type), type_name(m->fails));
        } else {
            diag_note("end every path with 'return value;', as the method returns %s", type_name(m->return_type));
        }
    }
    c->method = NULL;
    c->system = NULL;
}

// `event(verified Died died)`: the handler hears the event once the server has
// confirmed the tick that sent it. Only a local handler of one of the match's
// events can: the match has no "confirmed", and a local event is this
// machine's alone.
static void check_verified_trigger(const decl *handler, param *p, const decl *d)
{
    if (!handler->is_local) {
        diag_error(p->verified_at, "the match is the same on every machine, so a match handler has no 'verified' events");
        diag_note("what the server has confirmed is one machine's to know: a local handler hears it, 'local event(verified "
                  STR_FMT " ...) " STR_FMT "(...)'", STR_ARG(d->name), STR_ARG(handler->name));
        p->verified = false;
    } else if (d->is_local) {
        diag_error(p->verified_at, "'" STR_FMT "' is a local event: it's this machine's own, and no server confirms it",
                   STR_ARG(d->name));
        diag_note("'verified' is for the match's events, which a client hears as it predicts them first");
        p->verified = false;
    }
}

// A handler's trigger, `event(Hit hit)`: the event it handles.
static void check_trigger(const checker *c, decl *handler, param *p, decl *d)
{
    const loc at = p->type_at.line ? p->type_at : p->at;
    if (!d || d->kind != DECL_EVENT) {
        if (d) {
            diag_error(at, "'" STR_FMT "' is %s; a handler handles an event", STR_ARG(d->name), decl_what(d));
            diag_note("declare what happened as 'event Name { ... }' and send it with Send");
        } else {
            diag_error(at, "unknown event '" STR_FMT "'", STR_ARG(p->type_name));
            suggestion s = suggest_start(p->type_name);
            suggest_events(&s, c->prog);
            suggest_note(&s);
        }
        p->type = T_ERR;
        return;
    }
    p->type = (type){TY_EVENT, d};
    handler->event = d;
    if (handler->is_command != d->is_command) {
        diag_error(at, "'" STR_FMT "' is %s; receive it with '%s(...)'", STR_ARG(d->name),
                   d->is_command ? "a command" : "an event", d->is_command ? "command" : "event");
        return;
    }
    if (handler->is_command) {
        if (handler->is_local) diag_error(handler->local_at, "command handlers run in the match, so they can't be local");
        if (handler->is_async) diag_error(handler->async_at, "a command handler can't be async; it validates and handles one submission immediately");
        return;
    }
    if (p->verified) check_verified_trigger(handler, p, d);
    // Spawned and Destroyed come from both worlds; the handler's side picks one
    // (check_listener). A local handler of another of the match's events hears
    // it: it's a listener (decl.listens).
    if (d == c->prog->spawned || d == c->prog->destroyed) return;
    if (d->is_local && !handler->is_local) {
        diag_error(at, "'" STR_FMT "' is local, so its handlers are too: 'local event(" STR_FMT " ...) " STR_FMT "(...)'",
                   STR_ARG(d->name), STR_ARG(d->name), STR_ARG(handler->name));
    }
}

// A component or singleton `d` in the parameters of `sys`, from the other side:
// match code can't use local state, and local code can't change the match.
// A handler of a local event only takes local state. Returns false after an
// error.
static const char *system_what(const decl *sys);

static bool check_param_side(const decl *sys, const param *p, const decl *d)
{
    const loc at = p->type_at.line ? p->type_at : p->at;
    if (d->is_local && !is_local_code(sys)) {
        diag_error(at, "'" STR_FMT "' is local, and the match can't use local state", STR_ARG(d->name));
        diag_note("local state belongs to one machine, and the match runs the same on every one");
        return false;
    }
    // A local handler of Forgotten takes the match component or singleton
    // whose local fields are going (see check_forgotten)
    const bool forgets = sys->is_handler && sys->is_local && sys->event && sys->event->builtin
                      && str_eq_c(sys->event->name, "Forgotten");
    if (!d->is_local && forgets && (p->mode == PARAM_READ || p->mode == PARAM_MUT)) return true;
    // A local handler of one of the match's events hears it with what it takes
    // of the match, as the event's turn left it: to read, and a component's
    // local fields to write, as a view does
    const bool hears = sys->is_handler && sys->is_local && sys->event && !sys->event->is_local && !sys->is_command;
    if (!d->is_local && hears) {
        if (p->mode == PARAM_MUT && !d->local_part) {
            diag_error(p->at, "a local handler can't change the match, and '" STR_FMT "' belongs to it", STR_ARG(d->name));
            diag_note("it hears what happened, to show it: take '" STR_FMT "' without 'mut'. What only this machine keeps for "
                      "it goes in local fields, which 'mut' lets a handler write: 'local float timer;'", STR_ARG(d->name));
            return false;
        }
        return true;
    }
    if (!d->is_local && sys->is_handler && sys->is_local) {
        diag_error(at, "'" STR_FMT "' belongs to the match, and a handler of a local event only takes local state",
                   STR_ARG(d->name));
        if (forgets) diag_note("a handler of Forgotten takes the one whose local fields are going, as a parameter");
        else diag_note("a local handler of one of the match's events takes what it reads of the match, like 'local event(Hit "
                       "hit) Name(" STR_FMT " %s) { ... }'", STR_ARG(d->name), example_name(d));
        return false;
    }
    if (!d->is_local && sys->is_view && p->mode == PARAM_MUT && !d->local_part) {
        diag_error(p->at, "a view can't change the match, and '" STR_FMT "' belongs to it", STR_ARG(d->name));
        diag_note("put what the view wants in the input, and change '" STR_FMT "' in a system; what only this machine "
                  "keeps for it goes in local fields, which a view writes: 'local float timer;'", STR_ARG(d->name));
        return false;
    }
    return true;
}

// `with ownership` and `without ownership`: whether this machine's player owns
// the entity, by its Owner. Only local code has a machine to ask about, and of
// it only views run for the match's entities. `with` needs an Owner, as `with
// Owner` does; `without` takes entities with one and with none, and which
// rows pass is the loop's to check, like a sparse component (gen_row_filter).
static void check_ownership(const checker *c, decl *sys, param *p)
{
    const bool with = p->mode == PARAM_WITH;
    const char *word = with ? "with" : "without";
    p->type = (type){TY_VOID, NULL};
    if (!is_local_code(sys)) {
        diag_error(p->type_at, "the match runs the same on every machine, so %s can't filter by '%s ownership'",
                   sys->is_handler ? "an event handler" : "a system", word);
        diag_note("ownership is whether this machine's player owns the entity, which views ask; for entities that have "
                  "%s owner, write '%s Owner'", with ? "any" : "no", word);
        suggest_replacement(p->type_name, "Owner");
        return;
    }
    if (!sys->is_view) {
        diag_error(p->type_at, "'%s ownership' filters the entities a view runs for, and this is a handler", word);
        diag_note("a local handler of a match event takes the entity's 'Owner owner' and the local 'Session session', and "
                  "compares 'owner.player' with 'session.player'");
        return;
    }
    const int ownership = with ? OWNERSHIP_WITH : OWNERSHIP_WITHOUT;
    if (sys->ownership) {
        if (sys->ownership == ownership) diag_error(p->at, "'%s ownership' appears twice", word);
        else diag_error(p->at, "view '" STR_FMT "' takes entities both 'with ownership' and 'without ownership', and none is both",
                        STR_ARG(sys->name));
        return;
    }
    sys->ownership = ownership;
    if (with) sys->need_mask = mask_or(sys->need_mask, bit(c->prog->owner));
}

// A local handler of Forgotten: `local event(Forgotten) Free(Box box)`. It
// runs as an entity's local fields go, the entity gone from the match (or the
// match's, for a singleton), with the one component or singleton it takes of
// the match there for its local fields alone, to let go of what they hold.
static void check_forgotten(decl *sys)
{
    bool said = false; // Something about its parameters already
    if (sys->is_async) {
        diag_error(sys->async_at, "a handler of Forgotten can't be async: the local fields go as soon as it's done");
        diag_note("start a task from it instead, with what the task needs of them as arguments");
    }
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        said |= p->type.kind == TY_ERROR;
        if ((p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON) || p->type.decl->is_local) continue;
        if (p->mode != PARAM_READ && p->mode != PARAM_MUT) continue;
        const decl *d = p->type.decl;
        const loc at = p->type_at.line ? p->type_at : p->at;
        if (!d->local_part) {
            said = true;
            diag_error(at, "'" STR_FMT "' has no local fields, so this machine keeps nothing for it to forget", STR_ARG(d->name));
            diag_note("Forgotten is sent as an entity's local fields go, like 'local List<Vertex3> verts;' in '" STR_FMT "'",
                      STR_ARG(d->name));
        } else if (sys->forgets) {
            diag_error(at, "a handler of Forgotten hears one component's or singleton's local fields go, and '" STR_FMT
                       "' is already it", STR_ARG(sys->forgets->name));
            diag_note("write another handler for '" STR_FMT "'", STR_ARG(d->name));
        } else {
            sys->forgets = p->type.decl;
        }
    }
    if (!sys->forgets && !said) {
        diag_error(sys->at, "a handler of Forgotten takes the match component or singleton whose local fields are going");
        diag_note("like 'local event(Forgotten) " STR_FMT "(Box box) { ... }', which runs for each entity's 'Box' as this "
                  "machine forgets it", STR_ARG(sys->name));
    }
}

// A local handler of one of the match's events, `local event(Hit hit)
// ShowHit(Body body, mut Fx fx)`: a listener. It runs at the start of a frame
// for what the ticks since the last one sent, with what it takes of the match
// as the event's turn left it, so nothing it takes is the world's as it is
// now. Spawned and Destroyed come from both worlds: a local handler of one
// hears the match's when it takes something of the match, or is `verified`.
static void check_listener(const program *prog, decl *sys)
{
    const param *trigger = &sys->params.items[0];
    const bool either = sys->event == prog->spawned || sys->event == prog->destroyed;
    const param *of_match = NULL; // Something it takes of the match
    const param *of_local = NULL; // A local component: an entity of this machine's
    for (int i = 1; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON) continue;
        if (!p->type.decl->is_local && !of_match) of_match = p;
        if (p->type.decl->is_local && p->type.kind == TY_COMPONENT && !of_local) of_local = p;
    }
    if (either && !of_match && !trigger->verified) return; // The local world's entities
    if (either && of_local) {
        const loc at = trigger->verified ? trigger->verified_at : of_match->type_at.line ? of_match->type_at : of_match->at;
        diag_error(at, "'" STR_FMT "' hears this machine's entities, as it takes '" STR_FMT "', which is local, and the "
                   "match's, as it %s", STR_ARG(sys->name), STR_ARG(of_local->type_name),
                   trigger->verified ? "is 'verified'" : "takes something of the match");
        diag_note("an entity is in one world: write a handler for each");
        return;
    }
    sys->listens = true;
    sys->listens_verified = trigger->verified;
    if (sys->is_async) {
        diag_error(sys->async_at, "a local handler of a match event can't be async yet: what it takes of the match is as the "
                   "event left it, which a task can't get again");
        diag_note("start a task from it instead, with what the task needs as arguments");
    }
    for (int i = 1; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (p->type.kind != TY_WORLD) continue;
        diag_error(p->at, "a local handler hears the match as the event left it, and the World is the match as it is now");
        diag_note("take what it reads of the entity the event was sent to as parameters, or put it in the event's fields");
    }
}

// `verified Health health`, `with verified Dead`: from the world the server has
// confirmed. A view's parameter says it, of the match's components and
// singletons, to read. The match has no "confirmed", and a local handler's
// parameters already come from one tick, the one its event was sent in.
// Returns false after an error.
static bool check_verified_param(const decl *sys, const param *p, const decl *d)
{
    const loc at = p->verified_at;
    if (!is_local_code(sys)) {
        diag_error(at, "the match is the same on every machine, so %s has nothing 'verified' to read",
                   sys->is_handler ? "a match handler" : sys->is_async ? "a task" : "a system");
        diag_note("what the server has confirmed is one machine's to know: a view reads it, 'view Name(verified " STR_FMT
                  " %s) { ... }', and a local handler hears it, 'local event(verified Hit hit) Name(...)'", STR_ARG(p->type_name),
                  d ? example_name(d) : "value");
        return false;
    }
    if (sys->is_handler) {
        const param *trigger = &sys->params.items[0];
        if (trigger->verified) {
            diag_error(at, "'" STR_FMT "' hears a confirmed tick, and what it takes of the match is that tick's already",
                       STR_ARG(sys->name));
            diag_note("drop 'verified' here: 'event(verified ...)' says it for all of them");
        } else {
            diag_error(at, "a local handler takes the match as its event's tick left it, so it has nothing 'verified' to read yet");
            diag_note("to hear the event once the server confirmed it, with everything as that tick left it, write 'local "
                      "event(verified " STR_FMT " ...) " STR_FMT "(...)'", STR_ARG(trigger->type_name), STR_ARG(sys->name));
        }
        return false;
    }
    if (!d || (d->kind != DECL_COMPONENT && d->kind != DECL_SINGLETON)) {
        if (!d && str_eq_c(p->type_name, "World")) {
            diag_error(at, "the World of the confirmed match isn't built yet");
            diag_note("take the components the view reads of it as parameters, like 'verified Health health'");
        } else {
            diag_error(at, "'verified' goes before a component or singleton of the match, and '" STR_FMT "' isn't one",
                       STR_ARG(p->type_name));
        }
        return false;
    }
    if (d->is_local) {
        diag_error(at, "'" STR_FMT "' is local: it's this machine's own, and no server confirms it", STR_ARG(d->name));
        diag_note("'verified' is for the match's components and singletons, which a client predicts ahead of the server");
        return false;
    }
    return true;
}

// A second candidate set belongs to the system, but never to its outer
// entity filter. Its row is compiler-only data: code reads its named fields.
static void check_entity_query(const checker *c, decl *sys, param *query)
{
    if (sys->is_view || sys->is_handler || sys->is_async) {
        diag_error(query->at, "query parameters are only supported in ordinary systems");
    }
    decl *row = arena_alloc(sizeof(decl));
    row->kind = DECL_STRUCT;
    row->name = str_from("query row");
    row->query_row = true;
    row->query_source = query;
    row->unit = sys->unit;
    row->at = query->at;
    query->type = (type){TY_QUERY, row};
    field entity = {.name = str_from("entity"), .type = T_ENTITY_, .at = query->at};
    vec_push(row->fields, entity);
    comp_mask seen = mask_none();
    const decl *side_of = NULL;
    for (int i = 0; i < query->query.count; i++) {
        param *p = &query->query.items[i];
        p->query_source = query;
        if (p->mode == PARAM_QUERY) {
            diag_error(p->at, "a query can't contain another query");
            p->type = T_ERR;
            continue;
        }
        if (p->mode == PARAM_MUT) {
            diag_error(p->at, "query component bindings are read-only and can't be 'mut'");
            p->mode = PARAM_READ;
        }
        if (p->verified) {
            diag_error(p->verified_at, "the match is the same on every machine, so a query has nothing 'verified' to read");
            p->verified = false;
        }
        if (p->ownership) {
            diag_error(p->at, "a query filters by components, so 'ownership' isn't a query filter");
            p->type = T_ERR;
            continue;
        }
        if (p->name.len) {
            check_reserved(p->name, p->name_at);
            if (str_eq_c(p->name, "entity")) {
                diag_error(p->name_at, "'entity' is the query row's entity handle, so it can't name a component binding");
            }
            for (int j = 0; j < i; j++) {
                if (str_eq(p->name, query->query.items[j].name)) {
                    diag_error(p->name_at, "query binding '" STR_FMT "' is declared twice", STR_ARG(p->name));
                }
            }
        }
        decl *d = find_type(c, p->type_name, p->type_at);
        if (!d || d->kind != DECL_COMPONENT) {
            diag_error(p->type_at, "a query takes only components, and '" STR_FMT "' isn't a component", STR_ARG(p->type_name));
            p->type = T_ERR;
            continue;
        }
        if (!check_param_side(sys, p, d)) {
            p->type = T_ERR;
            continue;
        }
        p->type = (type){TY_COMPONENT, d};
        if (mask_has(seen, d->index)) diag_error(p->at, "'" STR_FMT "' appears twice in the query", STR_ARG(d->name));
        seen = mask_or(seen, bit(d));
        if (p->mode == PARAM_WITHOUT) query->query_without_mask = mask_or(query->query_without_mask, bit(d));
        else query->query_need_mask = mask_or(query->query_need_mask, bit(d));
        if (side_of && side_of->is_local != d->is_local) {
            diag_error(p->type_at, "a query can't mix local and match components: no entity has both");
        } else if (!side_of) side_of = d;
        if (p->mode != PARAM_READ) continue;
        field binding = {.name = p->name, .type_name = p->type_name, .type = p->type, .at = p->name_at,
                         .type_at = p->type_at, .type_qual_at = p->type_qual_at};
        vec_push(row->fields, binding);
        world_use *use = NULL;
        for (int j = 0; j < sys->world_uses.count; j++) {
            if (sys->world_uses.items[j].component == d) use = &sys->world_uses.items[j];
        }
        if (use) use->query_read = true;
        else {
            const world_use u = {.component = d, .at = p->at, .query_read = true};
            vec_push(sys->world_uses, u);
        }
    }
    if (mask_intersects(query->query_need_mask, query->query_without_mask)) {
        diag_error(query->at, "a query both requires and excludes the same component");
    }
    query->query_local = side_of && side_of->is_local;
    if (query->query_local) row->fields.items[0].type = (type){TY_LOCAL_ENTITY, NULL};
}

static void check_params(const checker *c, decl *sys)
{
    const program *prog = c->prog;
    const param *entity = NULL; // The Entity or LocalEntity parameter
    bool has_input = false;
    bool has_devices = false;
    bool has_world = false;
    comp_mask seen = mask_none();
    comp_mask seen_verified = mask_none(); // A view's `verified` ones: of the entity as the server confirmed it
    const decl *side_of = NULL; // The first component: which world the entities are in

    for (int i = 0; i < sys->params.count; i++) {
        param *p = &sys->params.items[i];
        if (p->ownership) {
            check_ownership(c, sys, p);
            continue;
        }
        decl *d = p->mode == PARAM_QUERY ? NULL : find_type(c, p->type_name, p->type_at);
        const bool is_entity = str_eq_c(p->type_name, "Entity");
        const bool is_local_entity = str_eq_c(p->type_name, "LocalEntity");

        if (p->name.len > 0) {
            check_reserved(p->name, p->at);
            for (int j = 0; j < i; j++) {
                if (str_eq(sys->params.items[j].name, p->name)) {
                    diag_error(p->at, "parameter '" STR_FMT "' is declared twice", STR_ARG(p->name));
                }
            }
        }

        if (p->verified && p->mode != PARAM_EVENT && !check_verified_param(sys, p, d)) p->verified = false;
        if (p->mode == PARAM_SENDER) {
            if (!str_eq_c(p->type_name, "PlayerID")) {
                diag_error(p->type_at, "a command sender is PlayerID, supplied by the connection");
                p->type = T_ERR;
            } else p->type = (type){TY_PLAYER, NULL};
            continue;
        }
        if (p->mode == PARAM_EVENT) {
            check_trigger(c, sys, p, d);
            continue;
        }
        if (p->mode == PARAM_QUERY) {
            check_entity_query(c, sys, p);
            continue;
        }

        // The entity the code runs for is `this`. The parameter still names it,
        // so its uses don't report errors of their own.
        if (is_entity || is_local_entity) {
            diag_error(p->type_at.line ? p->type_at : p->at, "the entity %s runs for is 'this', not a parameter",
                       sys->is_view ? "a view" : sys->is_handler ? "an event handler" : "a system");
            if (p->name.len > 0) {
                diag_note("remove the parameter, and write 'this' where the code uses '" STR_FMT "'", STR_ARG(p->name));
                const fix f = {.kind = FIX_USE_THIS, .at = p->type_at.line ? p->type_at : p->at, .param = p};
                vec_push(c->prog->fixes, f);
            }
            entity = p;
            p->type = is_entity ? T_ENTITY_ : (type){TY_LOCAL_ENTITY, NULL};
            continue;
        }

        // Views receive this frame's masked devices. Match code receives the
        // owner's devices, or the server's: the input sends what it reads.
        if (str_eq_c(p->type_name, "Devices")) {
            if (!sys->is_view && is_local_code(sys)) {
                diag_error(p->at, "local handlers can't read the devices yet");
            }
            if (p->mode != PARAM_READ) {
                diag_error(p->at, "devices can't be 'mut', 'with' or 'without'; code can only read them");
            }
            if (has_devices) diag_error(p->at, "a %s can only have one Devices parameter", system_what(sys));
            has_devices = true;
            p->type = (type){TY_RECORD, prog->devices};
            continue;
        }

        // `World world`: any entity's components by its handle, through
        // world.Get<Health>(e), Has and, with mut, Set. What the body reaches
        // that way is access to every entity's, and it filters nothing. A
        // game's own World, in its namespace, comes first.
        if (!d && str_eq_c(p->type_name, "World")) {
            if (p->mode == PARAM_WITH || p->mode == PARAM_WITHOUT) {
                diag_error(p->at, "'with' and 'without' filter entities by component, and the World is every entity's");
                diag_note("take it as a parameter, 'World world', and ask it: 'world.Has<Health>(entity)'");
                p->type = T_ERR;
                continue;
            }
            if (has_world) diag_error(p->at, "a %s can only have one World parameter", system_what(sys));
            has_world = true;
            p->type = (type){TY_WORLD, NULL};
            continue;
        }

        str inner;
        if (!d && optional_name(p->type_name, &inner)) {
            diag_error(p->type_at.line ? p->type_at : p->at, "a %s's parameters are there or it doesn't run, so they can't be T?",
                       system_what(sys));
            diag_note("to run for entities with or without a component, write two %ss, one 'with' it and one 'without'",
                      system_what(sys));
            p->type = T_ERR;
            continue;
        }
        if (!d) {
            diag_error(p->type_at.line ? p->type_at : p->at, "unknown component or singleton '" STR_FMT "'",
                       STR_ARG(p->type_name));
            suggestion s = suggest_start(p->type_name);
            suggest_decls(&s, prog, true, p->mode != PARAM_WITH && p->mode != PARAM_WITHOUT, true);
            if (sys->is_view && (p->mode == PARAM_WITH || p->mode == PARAM_WITHOUT)) suggest_consider_c(&s, "ownership");
            suggest_note(&s);
            const fix create = {.kind = FIX_CREATE_COMPONENT, .at = p->type_at.line ? p->type_at : p->at, .name = p->type_name};
            vec_push(c->prog->fixes, create);
            p->type = T_ERR;
            continue;
        }

        if (d->kind == DECL_EVENT) {
            diag_error(p->type_at.line ? p->type_at : p->at, "'" STR_FMT "' is an event; systems take components and singletons",
                       STR_ARG(d->name));
            if (sys->is_handler) {
                diag_note("a handler handles one event, named before it: 'event(" STR_FMT " ...) " STR_FMT "(...)'",
                          STR_ARG(d->name), STR_ARG(sys->name));
            } else {
                diag_note("handle it instead: 'event(" STR_FMT " ...) Name(...) { ... }'", STR_ARG(d->name));
            }
            p->type = T_ERR;
            continue;
        }

        if (d->kind == DECL_ENUM) {
            diag_error(p->type_at.line ? p->type_at : p->at, "'" STR_FMT "' is an enum; systems take components and singletons",
                       STR_ARG(d->name));
            diag_note("keep its value in a component or singleton, like 'singleton Name { " STR_FMT " value; }'",
                      STR_ARG(d->name));
            p->type = T_ERR;
            continue;
        }

        if (d->kind == DECL_STRUCT) {
            diag_error(p->type_at.line ? p->type_at : p->at, "'" STR_FMT "' is a struct; systems take components and singletons",
                       STR_ARG(d->name));
            diag_note("keep it in a component or singleton, like 'component Name { " STR_FMT " value; }'",
                      STR_ARG(d->name));
            p->type = T_ERR;
            continue;
        }

        // An input parameter gives the input of the player who owns the entity,
        // or the server's input when no player does. It doesn't filter entities.
        if (d->kind == DECL_INPUT) {
            if (sys->is_view) {
                diag_error(p->at, "views can't read input yet");
                diag_note("read the state the input changed instead, like a component the systems update");
            } else if (is_local_code(sys)) {
                diag_error(p->at, "local handlers can't read input");
            }
            if (p->mode != PARAM_READ) {
                diag_error(p->at, "input can't be 'mut', 'with' or 'without'; the simulation can only read it");
            }
            if (has_input) diag_error(p->at, "a system can only have one input parameter");
            has_input = true;
            p->type = (type){TY_INPUT, d};
            continue;
        }

        if (!check_param_side(sys, p, d)) {
            p->type = T_ERR;
            continue;
        }

        if (d->kind == DECL_SINGLETON) {
            if (p->mode == PARAM_WITH || p->mode == PARAM_WITHOUT) {
                diag_error(p->at, "'with' and 'without' filter entities by component; '" STR_FMT "' is a singleton",
                           STR_ARG(d->name));
            }
            if (p->mode == PARAM_MUT && d->builtin) {
                diag_error(p->at, "'" STR_FMT "' is managed by the engine and is read-only", STR_ARG(d->name));
            }
            for (int j = 0; j < i; j++) { // Once from each world at most: as it's drawn, and `verified`
                if (sys->params.items[j].type.decl == d && sys->params.items[j].verified == p->verified) {
                    diag_error(p->at, "'%s" STR_FMT "' appears twice", p->verified ? "verified " : "", STR_ARG(d->name));
                }
            }
            p->type = (type){TY_SINGLETON, d};
            continue;
        }

        p->type = (type){TY_COMPONENT, d};
        if (p->verified) { // Of the entity as the server confirmed it
            if (mask_has(seen_verified, d->index)) diag_error(p->at, "'verified " STR_FMT "' appears twice", STR_ARG(d->name));
            seen_verified = mask_or(seen_verified, bit(d));
            if (p->mode == PARAM_WITHOUT) sys->verified_without = mask_or(sys->verified_without, bit(d));
            else sys->verified_need = mask_or(sys->verified_need, bit(d));
        } else {
            if (mask_has(seen, d->index)) diag_error(p->at, "'" STR_FMT "' appears twice", STR_ARG(d->name));
            seen = mask_or(seen, bit(d));
            if (p->mode == PARAM_WITHOUT) sys->without_mask = mask_or(sys->without_mask, bit(d));
            else sys->need_mask = mask_or(sys->need_mask, bit(d));
        }
        // An entity is in one world, so its components are all of one side.
        if (side_of && side_of->is_local != d->is_local) {
            const decl *local = d->is_local ? d : side_of;
            const decl *match = d->is_local ? side_of : d;
            diag_error(p->type_at.line ? p->type_at : p->at, "'" STR_FMT "' is local and '" STR_FMT "' belongs to the match, "
                       "and no entity has both", STR_ARG(local->name), STR_ARG(match->name));
            diag_note("local entities live on this machine, and the match's in the match");
        } else if (!side_of) {
            side_of = d;
        }
    }
    // Owners are the match's: local entities have none to filter by.
    for (int i = 0; sys->ownership && side_of && side_of->is_local && i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (!p->ownership) continue;
        diag_error(p->type_at, "'%s ownership' filters the match's entities, and '" STR_FMT "' is local",
                   p->mode == PARAM_WITH ? "with" : "without", STR_ARG(side_of->name));
        diag_note("local entities live on this machine, and no player owns them");
        break;
    }

    if (sys->ownership == OWNERSHIP_WITH && mask_has(sys->without_mask, prog->owner->index)) {
        diag_error(sys->at, "view '" STR_FMT "' takes entities 'with ownership' and 'without Owner', and an entity "
                   "without an Owner is nobody's", STR_ARG(sys->name));
    } else if (mask_intersects(sys->need_mask, sys->without_mask)) {
        diag_error(sys->at, "system '" STR_FMT "' both requires and excludes the same component", STR_ARG(sys->name));
    }
    // A view's `verified` components and filters. With none of the world it
    // draws, the confirmed world's entities are the ones it runs for, and its
    // masks are theirs; with some of both, it runs for the drawn world's
    // entities that are in the confirmed one too.
    if (mask_intersects(sys->verified_need, sys->verified_without)) {
        diag_error(sys->at, "view '" STR_FMT "' both requires and excludes the same 'verified' component", STR_ARG(sys->name));
    }
    for (int i = 0; i < sys->params.count; i++) sys->takes_verified |= sys->params.items[i].verified && !sys->is_handler;
    if (mask_any(seen_verified) && !mask_any(seen) && !entity) {
        sys->verified_entities = true;
        sys->need_mask = mask_or(sys->need_mask, sys->verified_need);
        sys->without_mask = mask_or(sys->without_mask, sys->verified_without);
        sys->verified_need = mask_none();
        sys->verified_without = mask_none();
    } else if (mask_any(seen_verified)) {
        sys->verified_join = true;
    }
    sys->per_entity = entity || mask_any(seen) || mask_any(seen_verified) || sys->ownership;
    sys->entity_local = side_of ? side_of->is_local : entity && entity->type.kind == TY_LOCAL_ENTITY;
    if (sys->is_handler && sys->is_local && sys->event == prog->forgotten) check_forgotten(sys);
    if (sys->is_handler && sys->is_local && sys->event && !sys->event->is_local && !sys->is_command) check_listener(prog, sys);

    // A handler that takes components reads the entity its event is sent to,
    // so every Send of that event must name one (see check_sends).
    if (sys->is_handler && sys->event && sys->per_entity) {
        if (sys->event->world_event) {
            diag_error(sys->at, "'" STR_FMT "' is sent to the world, not to an entity, so '" STR_FMT "' can't take "
                       "components", STR_ARG(sys->event->name), STR_ARG(sys->name));
            diag_note("read what the event carries instead, like its 'player'");
        } else if (!sys->event->needs_target) {
            sys->event->needs_target = sys;
        }
    }

}

// Access a system declares but doesn't use makes other systems wait for
// nothing, so it's worth a warning.
static void warn_unused_params(checker *c, const decl *sys)
{
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        const type_kind kind = p->type.kind;
        if (kind == TY_QUERY && !p->read) {
            diag_warning(p->name_at, "'" STR_FMT "' is never used", STR_ARG(p->name));
            diag_note("remove the query parameter");
        } else if (kind == TY_QUERY) {
            decl nested = *sys;
            nested.params.items = p->query.items;
            nested.params.count = p->query.count;
            nested.params.cap = p->query.cap;
            warn_unused_params(c, &nested);
        }
        if (kind == TY_WORLD && !p->read) {
            diag_warning(p->name_at, "'" STR_FMT "' is never used", STR_ARG(p->name));
            diag_note("remove it");
        } else if (kind == TY_WORLD && p->mode == PARAM_MUT && !p->written) {
            diag_warning(p->at, "'" STR_FMT "' is declared mut but never calls Set", STR_ARG(p->name));
            diag_note("remove 'mut': the %s only reads through it", system_what(sys));
            const fix f = {.kind = FIX_REMOVE_MUT, .at = p->at, .param = p};
            vec_push(c->prog->fixes, f);
        }
        if (p->name.len == 0 || (kind != TY_COMPONENT && kind != TY_SINGLETON)) continue;
        // A Forgotten handler's component says whose local fields it hears go, read or not
        if (sys->forgets && p->type.decl == sys->forgets && !p->read) continue;
        const str type = p->type_name;
        if (sys->kind == DECL_SHADER) { // What it takes, the engine sends to the GPU: nothing waits for it
            if (!p->read) {
                diag_warning(p->name_at, "'" STR_FMT "' is never used", STR_ARG(p->name));
                diag_note("remove it");
            }
            continue;
        }
        if (!p->read && kind == TY_COMPONENT) {
            diag_warning(p->name_at, "'" STR_FMT "' is never used", STR_ARG(p->name));
            if (p->verified) {
                diag_note("to only require the component in the confirmed world, write 'with verified " STR_FMT "'", STR_ARG(type));
                continue; // The quick fix writes 'with', which would ask the drawn world instead
            }
            if (sys->is_handler || sys->is_view) {
                diag_note("to only require the component, write 'with " STR_FMT "'", STR_ARG(type));
            } else {
                diag_note("to only require the component, write 'with " STR_FMT "': a filter doesn't make other "
                          "systems wait",
                          STR_ARG(type));
            }
            const fix f = {.kind = FIX_USE_WITH, .at = p->name_at, .param = p};
            vec_push(c->prog->fixes, f);
        } else if (!p->read) {
            diag_warning(p->name_at, "'" STR_FMT "' is never used", STR_ARG(p->name));
            if (sys->is_handler || sys->is_view) diag_note("remove it");
            else diag_note("remove it: systems that write " STR_FMT " wait for this one while it's declared", STR_ARG(type));
        } else if (p->mode == PARAM_MUT && !p->written) {
            diag_warning(p->at, "'" STR_FMT "' is declared mut but never written", STR_ARG(p->name));
            if (sys->is_handler || sys->is_view) diag_note("remove 'mut': it only reads " STR_FMT, STR_ARG(type));
            else diag_note("without 'mut', systems that read " STR_FMT " can run alongside this one", STR_ARG(type));
            const fix f = {.kind = FIX_REMOVE_MUT, .at = p->at, .param = p};
            vec_push(c->prog->fixes, f);
        }
    }
}

// Whether a call does something besides giving its value: it spawns, sends,
// draws, waits, changes what it's called on or passed, or calls C. The calls
// that draw or call C through others are known once every body is checked.
static bool call_acts(const expr *e)
{
    if (e->kind == E_AWAIT || e->kind == E_TRY) return true; // `try` passes an error on
    if (e->kind != E_CALL && e->kind != E_METHOD) return false;
    switch (e->call) {
    case CALL_SPAWN: case CALL_LOAD: case CALL_UNLOAD: case CALL_SCENE_PLAYER: case CALL_SEND: case CALL_ADD:
    case CALL_REMOVE: case CALL_DESTROY: case CALL_GUI: case CALL_ACTION: case CALL_SESSION:
    case CALL_CLIPBOARD: case CALL_SNAP: case CALL_GRID: case CALL_RANDOM:
        return true;
    case CALL_DRAW: return e->type.kind == TY_VOID; // Draw2D.MeasureText and the conversions only give a value
    case CALL_LIST: return !str_eq_c(e->name, "Contains") && !str_eq_c(e->name, "IndexOf");
    case CALL_MAP: return !str_eq_c(e->name, "Has") && !str_eq_c(e->name, "Get");
    case CALL_WORLD: return str_eq_c(e->name, "Set");
    case CALL_FUNCTION:
    case CALL_METHOD: {
        const decl *m = e->method;
        if (!m) return false;
        if (m->is_extern || m->calls_c || m->draws || m->is_async || m->is_mut_method || m->takes_action) return true;
        for (int i = 0; i < m->params.count; i++) {
            if (m->params.items[i].mode == PARAM_MUT) return true; // It changes the caller's variable
        }
        return false;
    }
    default:
        return false;
    }
}

// The first call in a checked expression that does something, or NULL.
static const expr *acting_call(const expr *e)
{
    if (!e) return NULL;
    if (e->kind == E_AWAIT || e->kind == E_TRY) return e; // With the call it waits for, or whose error it passes on
    const expr *found = acting_call(e->object);
    if (!found) found = acting_call(e->cond);
    if (!found) found = acting_call(e->lhs);
    if (!found) found = acting_call(e->rhs);
    for (int i = 0; !found && i < e->args.count; i++) found = acting_call(e->args.items[i]);
    for (int i = 0; !found && i < e->inits.count; i++) found = acting_call(e->inits.items[i].value);
    return found ? found : call_acts(e) ? e : NULL;
}

// A call as a note names it: Spawn(...), GUI.Button(...), await Load(...).
static void put_call(sb *out, const expr *e)
{
    if (e->kind == E_AWAIT || e->kind == E_TRY) {
        sb_put(out, e->kind == E_AWAIT ? "await " : "try ");
        e = e->lhs;
    }
    if (e->kind == E_NAME) {
        sb_printf(out, STR_FMT, STR_ARG(e->name)); // try result
    } else if (e->kind == E_METHOD && e->object->kind == E_NAME) {
        sb_printf(out, STR_FMT "." STR_FMT "(...)", STR_ARG(e->object->name), STR_ARG(e->name));
    } else if (e->kind == E_CALL || e->kind == E_METHOD) {
        sb_printf(out, STR_FMT "(...)", STR_ARG(e->name));
    } else {
        sb_put(out, "its value");
    }
}

static int unused_order(const void *a, const void *b)
{
    const stmt *x = *(stmt *const *)a;
    const stmt *y = *(stmt *const *)b;
    if (x->name_at.file != y->name_at.file) return x->name_at.file - y->name_at.file;
    if (x->name_at.line != y->name_at.line) return x->name_at.line - y->name_at.line;
    return x->name_at.col - y->name_at.col;
}

// A local that's never read is a mistake more often than not: a value worked
// out and then not used, as `var fwd = Math.Mul(rot, move);` followed by code
// that moves by `move`. Assigned but never read counts as never read. The
// note says what to do, and editors offer it: when the value is a call that
// does something, the call stays, as a statement of its own.
static void warn_unused_locals(checker *c)
{
    if (c->unused.count > 1) qsort(c->unused.items, (size_t)c->unused.count, sizeof(stmt *), unused_order);
    for (int i = 0; i < c->unused.count; i++) {
        const stmt *s = c->unused.items[i];
        diag_warning(s->name_at, "local '" STR_FMT "' is never used", STR_ARG(s->name));
        const expr *acts = acting_call(s->value);
        fix f = {.kind = FIX_UNUSED_LOCAL, .at = s->name_at, .local = s};
        if (s->assigned) {
            diag_note("it's assigned, but nothing reads its value: remove it and what assigns it, or use it where it "
                      "was meant");
        } else if (acts) {
            sb call = {0};
            put_call(&call, acts);
            diag_note("nothing reads its value, but '%s' does something: keep the call as a statement of its own, "
                      "without the local",
                      call.data);
            if (!s->loop_var && stands_alone(s->value)) f.kind = FIX_KEEP_CALL;
        } else {
            diag_note("nothing reads its value: remove it, or use it where it was meant");
            if (!s->loop_var) f.kind = FIX_REMOVE_LOCAL;
        }
        vec_push(c->prog->fixes, f);
    }
}

static decl *builtin_event(const char *name, const bool world)
{
    decl *d = NEW(decl);
    d->kind = DECL_EVENT;
    d->name = str_from(name);
    d->builtin = true;
    d->world_event = world;
    return d;
}

static void add_builtins(program *prog)
{
    // singleton Time { float dt; int tick; float t; int tickRate; }: t is
    // tick * dt, the seconds since the match started, which the tick sets, and
    // tickRate the ticks a second, 1 / dt to the nearest, set as the match
    // starts (gen_api). t is a float of the match's, so views and what they
    // draw through see it blended.
    decl *time = NEW(decl);
    time->kind = DECL_SINGLETON;
    time->name = str_from("Time");
    time->builtin = true;
    const field dt = {str_from("dt"), str_from("float"), {0, 0, 0}, {0}, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    const field tick = {str_from("tick"), str_from("int"), {0, 0, 0}, {0}, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    const field t = {str_from("t"), str_from("float"), {0, 0, 0}, {0}, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    vec_push(time->fields, dt);
    vec_push(time->fields, tick);
    const field rate = {str_from("tickRate"), str_from("int"), {0, 0, 0}, {0}, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    vec_push(time->fields, t);
    vec_push(time->fields, rate);
    // int seed: the match's seed, from 0 to 2147483647, which the machine that
    // starts the match picks. Last, as it's only there in games that use it
    // (see drop_unused_seed).
    field seed_field = {0};
    seed_field.name = str_from("seed");
    seed_field.type_name = str_from("int");
    vec_push(time->fields, seed_field);
    prog->seed = &time->fields.items[time->fields.count - 1];
    prog->time = time;

    // struct Random { long state; }: a random number generator's state, as
    // tide/random.h's tide_random, which generated C uses for it. Its methods
    // are built in (builtins.h), and so is Random(seed).
    decl *random = NEW(decl);
    random->kind = DECL_STRUCT;
    random->name = str_from("Random");
    random->builtin = true;
    random->c_name = "tide_random";
    field state_field = {0};
    state_field.name = str_from("state");
    state_field.type_name = str_from("long");
    vec_push(random->fields, state_field);
    prog->random = random;

    // What a frag and a vert take beside the game's state, as parameters:
    // Pixel { float2 position; float depth; float3 world; }, where a frag's
    // pixel is, and Instance { float4x4 transform; }, how the mesh was drawn.
    // Not declarations: a game's own Pixel or Instance is the game's.
    static const char *const pixel_fields[][2] = {{"position", "float2"}, {"depth", "float"}, {"world", "float3"}};
    decl *pixel = NEW(decl);
    pixel->kind = DECL_RECORD;
    pixel->name = str_from("Pixel");
    pixel->builtin = true;
    for (int i = 0; i < (int)(sizeof pixel_fields / sizeof pixel_fields[0]); i++) {
        field f = {0};
        f.name = str_from(pixel_fields[i][0]);
        f.type_name = str_from(pixel_fields[i][1]);
        builtin_type_named(f.type_name, &f.type);
        vec_push(pixel->fields, f);
    }
    prog->pixel = pixel;
    decl *instance = NEW(decl);
    instance->kind = DECL_RECORD;
    instance->name = str_from("Instance");
    instance->builtin = true;
    field transform = {0};
    transform.name = str_from("transform");
    transform.type_name = str_from("float4x4");
    transform.type = (type){TY_FLOAT4X4, NULL};
    vec_push(instance->fields, transform);
    prog->instance = instance;

    // component Owner { PlayerID player; }: ties an entity to a player.
    decl *owner = NEW(decl);
    owner->kind = DECL_COMPONENT;
    owner->name = str_from("Owner");
    owner->builtin = true;
    const field player = {str_from("player"), str_from("PlayerID"), {0, 0, 0}, {0}, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    vec_push(owner->fields, player);
    prog->owner = owner;

    // The events the engine sends: to each entity as it's spawned or destroyed,
    // and to the world as players join and leave.
    prog->spawned = builtin_event("Spawned", false);
    prog->destroyed = builtin_event("Destroyed", false);
    prog->player_joined = builtin_event("PlayerJoined", true);
    prog->player_left = builtin_event("PlayerLeft", true);
    vec_push(prog->player_joined->fields, player);
    vec_push(prog->player_left->fields, player);

    // enum SceneVisibility { Public, Private }: who sees a scene the match loads.
    decl *visibility = NEW(decl);
    visibility->kind = DECL_ENUM;
    visibility->name = str_from("SceneVisibility");
    visibility->builtin = true;
    const enum_member public_member = {str_from("Public"), {0, 0, 0}, NULL, 0};
    const enum_member private_member = {str_from("Private"), {0, 0, 0}, NULL, 1};
    vec_push(visibility->members, public_member);
    vec_push(visibility->members, private_member);
    prog->scene_visibility = visibility;

    // enum Prediction { Rollback, Delay }: how clients keep up with the match,
    // the `prediction` setting. tide/session.h's tide_prediction has the same values.
    decl *prediction = NEW(decl);
    prediction->kind = DECL_ENUM;
    prediction->name = str_from("Prediction");
    prediction->builtin = true;
    const enum_member rollback_member = {str_from("Rollback"), {0, 0, 0}, NULL, 0};
    const enum_member delay_member = {str_from("Delay"), {0, 0, 0}, NULL, 1};
    vec_push(prediction->members, rollback_member);
    vec_push(prediction->members, delay_member);
    prog->prediction = prediction;

    static const char *const io_errors[] = {"None", "NotFound", "AlreadyExists", "AccessDenied", "InvalidPath", "NotDirectory",
                                            "DirectoryNotEmpty", "OutOfSpace", "StorageUnavailable", "InvalidData", "Failed"};
    decl *io_error = NEW(decl);
    io_error->kind = DECL_ENUM;
    io_error->name = str_from("IOError");
    io_error->builtin = true;
    for (int i = 0; i < (int)(sizeof io_errors / sizeof io_errors[0]); i++) {
        const enum_member m = {str_from(io_errors[i]), {0, 0, 0}, NULL, i};
        vec_push(io_error->members, m);
    }
    prog->io_error = io_error;

    // enum Anchor { UpperLeft, ..., LowerRight }: where GUILayout.Area goes, as
    // Unity's TextAnchor. tide/gui.h's tide_anchor has the same values.
    static const char *const anchors[] = {"UpperLeft", "UpperCenter", "UpperRight", "MiddleLeft", "MiddleCenter",
                                          "MiddleRight", "LowerLeft", "LowerCenter", "LowerRight"};
    decl *anchor = NEW(decl);
    anchor->kind = DECL_ENUM;
    anchor->name = str_from("Anchor");
    anchor->builtin = true;
    for (int i = 0; i < (int)(sizeof anchors / sizeof anchors[0]); i++) {
        const enum_member m = {str_from(anchors[i]), {0, 0, 0}, NULL, i};
        vec_push(anchor->members, m);
    }
    prog->anchor = anchor;

    // struct Vertex { float2 position; float2 uv; Color color = Color.white; }:
    // a corner of what Draw2D.Mesh draws, as tide/draw.h's tide_vertex. And
    // enum Filter { Point, Bilinear }: how it reads its texture between cells,
    // with tide_filter's values, which are Unity's FilterMode's.
    decl *vertex = NEW(decl);
    vertex->kind = DECL_STRUCT;
    vertex->name = str_from("Vertex");
    vertex->builtin = true;
    static const char *const vertex_fields[][2] = {{"position", "float2"}, {"uv", "float2"}, {"color", "Color"}};
    for (int i = 0; i < (int)(sizeof vertex_fields / sizeof vertex_fields[0]); i++) {
        const field f = {str_from(vertex_fields[i][0]), str_from(vertex_fields[i][1]), {0, 0, 0}, {0}, NULL, {0, 0, 0},
                         {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
        vec_push(vertex->fields, f);
    }
    expr *white = NEW(expr);
    white->kind = E_MEMBER;
    white->member = str_from("white");
    white->object = NEW(expr);
    white->object->kind = E_NAME;
    white->object->name = str_from("Color");
    vertex->fields.items[2].default_value = white;
    prog->vertex = vertex;
    // struct Vertex3 { float3 position; float2 uv; Color color = Color.white; }:
    // a 3D mesh's corner, as tide/draw.h's tide_vertex3.
    decl *vertex3 = NEW(decl);
    vertex3->kind = DECL_STRUCT;
    vertex3->name = str_from("Vertex3");
    vertex3->builtin = true;
    static const char *const vertex3_fields[][2] = {{"position", "float3"}, {"uv", "float2"}, {"color", "Color"}};
    for (int i = 0; i < (int)(sizeof vertex3_fields / sizeof vertex3_fields[0]); i++) {
        const field f = {str_from(vertex3_fields[i][0]), str_from(vertex3_fields[i][1]), {0, 0, 0}, {0}, NULL, {0, 0, 0},
                         {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
        vec_push(vertex3->fields, f);
    }
    vertex3->fields.items[2].default_value = white;
    prog->vertex3 = vertex3;
    // struct Shape { float2 center; float2 size; float rotation; float radius;
    // bool ellipse; Color color = Color.white; Color color2 = Color.none; float
    // outline; Color outlineColor = Color.white; float soft; }: what
    // Draw2D.Shape draws, which generated C hands tide/draw.h as a tide_shape.
    decl *shape = NEW(decl);
    shape->kind = DECL_STRUCT;
    shape->name = str_from("Shape");
    shape->builtin = true;
    static const char *const shape_fields[][2] = {
        {"center", "float2"}, {"size", "float2"}, {"rotation", "float"}, {"radius", "float"}, {"ellipse", "bool"},
        {"color", "Color"}, {"color2", "Color"}, {"outline", "float"}, {"outlineColor", "Color"}, {"soft", "float"}};
    for (int i = 0; i < (int)(sizeof shape_fields / sizeof shape_fields[0]); i++) {
        const field f = {str_from(shape_fields[i][0]), str_from(shape_fields[i][1]), {0, 0, 0}, {0}, NULL, {0, 0, 0},
                         {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
        vec_push(shape->fields, f);
    }
    expr *none = NEW(expr);
    none->kind = E_MEMBER;
    none->member = str_from("none");
    none->object = NEW(expr);
    none->object->kind = E_NAME;
    none->object->name = str_from("Color");
    shape->fields.items[5].default_value = white;
    shape->fields.items[6].default_value = none;
    shape->fields.items[8].default_value = white;
    prog->shape = shape;
    static const char *const filters[] = {"Point", "Bilinear"};
    decl *filter = NEW(decl);
    filter->kind = DECL_ENUM;
    filter->name = str_from("Filter");
    filter->builtin = true;
    for (int i = 0; i < (int)(sizeof filters / sizeof filters[0]); i++) {
        const enum_member m = {str_from(filters[i]), {0, 0, 0}, NULL, i};
        vec_push(filter->members, m);
    }
    prog->filter = filter;

    // This machine's part in a match (see tide/session.h, whose enums have the
    // same values): local singleton Session { SessionState state; PlayerID
    // player; int ping; bool server; bool open; }, its room (see check_member),
    // and local events Connected and Disconnected { DisconnectReason reason; },
    // with a kick's message (see check_member).
    static const char *const states[] = {"Offline", "Connecting", "Connected"};
    static const char *const reasons[] = {"Left", "TimedOut", "Refused", "ServerLeft", "Failed", "Ended", "Kicked"};
    decl *state = NEW(decl);
    state->kind = DECL_ENUM;
    state->name = str_from("SessionState");
    state->builtin = true;
    for (int i = 0; i < (int)(sizeof states / sizeof states[0]); i++) {
        const enum_member m = {str_from(states[i]), {0, 0, 0}, NULL, i};
        vec_push(state->members, m);
    }
    decl *reason = NEW(decl);
    reason->kind = DECL_ENUM;
    reason->name = str_from("DisconnectReason");
    reason->builtin = true;
    for (int i = 0; i < (int)(sizeof reasons / sizeof reasons[0]); i++) {
        const enum_member m = {str_from(reasons[i]), {0, 0, 0}, NULL, i};
        vec_push(reason->members, m);
    }
    decl *session = NEW(decl);
    session->kind = DECL_SINGLETON;
    session->name = str_from("Session");
    session->builtin = true;
    session->is_local = true;
    static const char *const session_fields[][2] = {
        {"state", "SessionState"}, {"player", "PlayerID"}, {"ping", "int"}, {"server", "bool"}, {"open", "bool"}};
    for (int i = 0; i < (int)(sizeof session_fields / sizeof session_fields[0]); i++) {
        const field f = {str_from(session_fields[i][0]), str_from(session_fields[i][1]), {0, 0, 0}, {0}, NULL, {0, 0, 0},
                         {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
        vec_push(session->fields, f);
    }
    prog->session = session;
    prog->connected = builtin_event("Connected", true);
    prog->disconnected = builtin_event("Disconnected", true);
    prog->connected->is_local = true;
    prog->disconnected->is_local = true;
    const field reason_field = {str_from("reason"), str_from("DisconnectReason"), {0, 0, 0}, {0}, NULL, {0, 0, 0}, {0}, 0,
                                {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    vec_push(prog->disconnected->fields, reason_field);
    // Forgotten: sent to a match entity (or the match) whose local fields are
    // going, for local handlers to let go of what they hold (see check_forgotten).
    prog->forgotten = builtin_event("Forgotten", false);
    prog->forgotten->is_local = true;

    VEC(decl *) decls = {0};
    vec_push(decls, time);
    vec_push(decls, random);
    vec_push(decls, owner);
    vec_push(decls, visibility);
    vec_push(decls, prediction);
    vec_push(decls, io_error);
    vec_push(decls, anchor);
    vec_push(decls, vertex);
    vec_push(decls, vertex3);
    vec_push(decls, shape);
    vec_push(decls, filter);
    vec_push(decls, state);
    vec_push(decls, reason);
    vec_push(decls, session);
    vec_push(decls, prog->connected);
    vec_push(decls, prog->disconnected);
    vec_push(decls, prog->spawned);
    vec_push(decls, prog->destroyed);
    vec_push(decls, prog->player_joined);
    vec_push(decls, prog->player_left);
    vec_push(decls, prog->forgotten); // After the others, whose numbers the match's queue keeps
    for (int i = 0; i < prog->decls.count; i++) vec_push(decls, prog->decls.items[i]);
    prog->decls.items = decls.items;
    prog->decls.count = decls.count;
    prog->decls.cap = decls.cap;
}

static decl *new_record(program *prog, const char *name, const char *c_name)
{
    decl *d = NEW(decl);
    d->kind = DECL_RECORD;
    d->name = str_from(name);
    d->c_name = c_name;
    d->builtin = true;
    vec_push(prog->records, d);
    return d;
}

static void record_field(decl *d, const char *name, const type t)
{
    const field f = {str_from(name), str_from(""), {0, 0, 0}, t, NULL, {0, 0, 0}, {0}, 0, {0, 0, 0}, false, 0, false, false, false, {0, 0, 0}};
    vec_push(d->fields, f);
}

// A record of device values, rather than a value: a Button is one.
static bool device_group_field(const field *f)
{
    return f->type.kind == TY_RECORD && !str_eq_c(f->type.decl->name, "Button");
}

// Lays out the device values in record `d`: its own first, then its records',
// so each record's are in a row, and an array's elements one after another.
// Each field's leaf is 1 + where its values start among `d`'s.
static void layout_leaves(decl *d)
{
    if (d->device_group) return; // A record in several places is laid out once
    d->device_group = true;
    if (d->array_of) {
        layout_leaves(d->array_of);
        d->leaves_count = d->array_length * d->array_of->leaves_count;
        return;
    }
    int at = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < d->fields.count; i++) {
            field *f = &d->fields.items[i];
            const bool group = device_group_field(f);
            if (group != (pass == 1) || f->typed) continue;
            f->leaf = 1 + at;
            if (group) layout_leaves(f->type.decl);
            at += group ? f->type.decl->leaves_count : 1;
        }
    }
    d->leaves_count = at;
}

// Lists the device values of record `d` in the place reached from
// tide_devices by `path`, in program.device_leaves, in the order
// layout_leaves gave them.
static void list_leaves(program *prog, decl *d, const char *path)
{
    vec_push(d->instances, prog->device_leaves.count);
    if (d->array_of) {
        for (int k = 0; k < d->array_length; k++) {
            sb at = {0};
            sb_printf(&at, "%s.at[%d]", path, k);
            list_leaves(prog, d->array_of, at.data);
        }
        return;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < d->fields.count; i++) {
            field *f = &d->fields.items[i];
            const bool group = device_group_field(f);
            if (group != (pass == 1) || f->typed) continue;
            sb at = {0};
            sb_printf(&at, "%s%s" STR_FMT, path, path[0] ? "." : "", STR_ARG(f->name));
            if (group) {
                list_leaves(prog, f->type.decl, at.data);
                continue;
            }
            if (prog->device_leaves.count == DEVICE_WORDS * 64) {
                fprintf(stderr, "tidec: too many device values (raise DEVICE_WORDS)\n");
                exit(1);
            }
            const device_leaf leaf = {at.data, f};
            vec_push(prog->device_leaves, leaf);
        }
    }
}

// A field that's a position in this machine's window.
static void window_field(decl *d, const char *name)
{
    for (int i = 0; i < d->fields.count; i++) {
        if (str_eq_c(d->fields.items[i].name, name)) d->fields.items[i].window = true;
    }
}

// The device records: `Devices`, which views and the input's Sample read, and
// systems take as a parameter. Their members come from the X-macros in
// tide/devices.h, so Tide and the C structs always match.
static void add_device_records(program *prog)
{
    const type t_bool = {TY_BOOL, NULL};
    const type t_float = {TY_FLOAT, NULL};
    const type t_float2 = {TY_FLOAT2, NULL};

    decl *button = new_record(prog, "Button", "tide_button");
    record_field(button, "pressed", t_bool);
    record_field(button, "down", t_bool);
    record_field(button, "up", t_bool);
    const type t_button = {TY_RECORD, button};

    decl *dpad = new_record(prog, "Dpad", "tide_dpad");
    decl *keyboard = new_record(prog, "Keyboard", "tide_keyboard");
    decl *mouse = new_record(prog, "Mouse", "tide_mouse");
    decl *gamepad = new_record(prog, "Gamepad", "tide_gamepad");

#define KEY(name) record_field(keyboard, #name, t_button);
#define MOUSE_AXIS(name) record_field(mouse, #name, t_float2);
#define MOUSE_BUTTON(name) record_field(mouse, #name, t_button);
#define DPAD_BUTTON(name) record_field(dpad, #name, t_button);
#define STICK(name) record_field(gamepad, #name, t_float2);
#define TRIGGER(name) record_field(gamepad, #name, t_float);
#define GAMEPAD_BUTTON(name) record_field(gamepad, #name, t_button);
    TIDE_KEYBOARD_KEYS(KEY)
    // What was typed since the last frame: text, which views read. It's no
    // device value: the input never sends it.
    record_field(keyboard, "text", (type){TY_STRING, NULL});
    keyboard->fields.items[keyboard->fields.count - 1].typed = true;
    TIDE_MOUSE_AXES(MOUSE_AXIS)
    TIDE_MOUSE_BUTTONS(MOUSE_BUTTON)
    TIDE_DPAD_BUTTONS(DPAD_BUTTON)
    record_field(gamepad, "connected", t_bool);
    TIDE_GAMEPAD_STICKS(STICK)
    TIDE_GAMEPAD_TRIGGERS(TRIGGER)
    TIDE_GAMEPAD_BUTTONS(GAMEPAD_BUTTON)
    record_field(gamepad, "dpad", (type){TY_RECORD, dpad});
#undef KEY
#undef MOUSE_AXIS
#undef MOUSE_BUTTON
#undef DPAD_BUTTON
#undef STICK
#undef TRIGGER
#undef GAMEPAD_BUTTON

    decl *touch = new_record(prog, "Touch", "tide_touch");
    record_field(touch, "press", t_button);
    record_field(touch, "id", (type){TY_INT, NULL});
#define TOUCH_AXIS(name) record_field(touch, #name, t_float2);
    TIDE_TOUCH_AXES(TOUCH_AXIS)
#undef TOUCH_AXIS
    window_field(touch, "position");
    window_field(touch, "startPosition");
    // A touchscreen's fingers, a slot each: an array, whose type Tide has no
    // name for yet, so it's not among the records code names.
    decl *touches = NEW(decl);
    touches->kind = DECL_RECORD;
    sb touches_name = {0};
    sb_printf(&touches_name, "Touch[%d]", TIDE_TOUCHES);
    touches->name = str_from(touches_name.data);
    touches->c_name = "tide_touches";
    touches->builtin = true;
    touches->array_of = touch;
    touches->array_length = TIDE_TOUCHES;
    decl *touchscreen = new_record(prog, "Touchscreen", "tide_touchscreen");
    record_field(touchscreen, "connected", t_bool);
    record_field(touchscreen, "primaryTouch", (type){TY_RECORD, touch});
    record_field(touchscreen, "touches", (type){TY_RECORD, touches});

    decl *pointer = new_record(prog, "Pointer", "tide_pointer");
#define POINTER_AXIS(name) record_field(pointer, #name, t_float2);
    TIDE_POINTER_AXES(POINTER_AXIS)
#undef POINTER_AXIS
    record_field(pointer, "press", t_button);
    window_field(mouse, "position");
    window_field(pointer, "position");

    decl *devices = new_record(prog, "Devices", "tide_devices");
    record_field(devices, "keyboard", (type){TY_RECORD, keyboard});
    record_field(devices, "mouse", (type){TY_RECORD, mouse});
    record_field(devices, "gamepad", (type){TY_RECORD, gamepad});
    record_field(devices, "touchscreen", (type){TY_RECORD, touchscreen});
    record_field(devices, "pointer", (type){TY_RECORD, pointer});
    prog->devices = devices;
    layout_leaves(devices);
    list_leaves(prog, devices, "");
}

// Names the language reserves: built-in types and function groups.
static bool is_builtin_name(const str name)
{
    type dummy;
    return builtin_type_named(name, &dummy) || str_eq_c(name, "Math") || builtin_draw_owner(name) || str_eq_c(name, "Draw")
        || str_eq_c(name, "Devices") || str_eq_c(name, "Spawn") || str_eq_c(name, "Send") || str_eq_c(name, "Scene")
        || str_eq_c(name, "GUI") || str_eq_c(name, "GUILayout") || str_eq_c(name, "Screen") || str_eq_c(name, "Cursor")
        || str_eq_c(name, "Frame") || str_eq_c(name, "File") || str_eq_c(name, "Directory") || str_eq_c(name, "Path")
        || str_eq_c(name, "Application") || str_eq_c(name, "Storage")
        || str_eq_c(name, "Action") || str_eq_c(name, "DrawList") || str_eq_c(name, "string") || str_eq_c(name, "Debug");
}

// What a declaration is called in generated C: Combat_Health for Combat.Health.
static void c_name_of(const decl *d, sb *out)
{
    const str ns = decl_ns(d);
    for (int i = 0; i < ns.len; i++) sb_putn(out, ns.ptr[i] == '.' ? "_" : &ns.ptr[i], 1);
    if (ns.len > 0) sb_put(out, "_");
    sb_putn(out, d->name.ptr, (size_t)d->name.len);
}

// The package a declaration is in, by name, or NULL for the game's own (and
// built-ins).
static const char *package_of(const decl *d)
{
    return d->unit ? d->unit->src->package : NULL;
}

// A match component's or singleton's `local` fields leave it for a declaration
// of their own, its local part: this machine's, outside the match, so nothing
// that lays the match out, hashes it or sends it ever sees them. The part is a
// local declaration of the same kind and name, which no name finds: its
// fields are reached through the component's (see check_local_field).
// Anywhere else `local` on a field is an error that says why.
static void split_local_fields(program *prog, decl *d)
{
    int kept = 0;
    for (int i = 0; i < d->fields.count; i++) {
        field *f = &d->fields.items[i];
        if (!f->is_local) {
            d->fields.items[kept++] = *f;
            continue;
        }
        const bool data = d->kind == DECL_COMPONENT || d->kind == DECL_SINGLETON;
        if (data && !d->is_local) {
            if (!d->local_part) {
                decl *part = NEW(decl);
                part->kind = d->kind;
                part->name = d->name;
                part->qualified = d->qualified;
                part->at = d->at;
                part->end = d->end;
                part->unit = d->unit;
                part->is_local = true;
                part->local_at = f->local_at;
                part->part_of = d;
                d->local_part = part;
                vec_push(prog->local_parts, part);
            }
            vec_push(d->local_part->fields, *f);
            continue;
        }
        if (data) {
            diag_error(f->local_at, "'" STR_FMT "' is local already, and so is every field of it", STR_ARG(d->name));
            diag_note("'local' before a field is for the match's components and singletons: it keeps that field on this "
                      "machine, outside the match");
            const fix remove = {.kind = FIX_LOCAL_WORD, .at = f->local_at};
            vec_push(prog->fixes, remove);
        } else if (d->kind == DECL_STRUCT) {
            diag_error(f->local_at, "structs belong to neither side, so a struct's field can't be local");
            diag_note("make the field that holds the struct local, in a component or singleton of the match: 'local " STR_FMT
                      " value;'", STR_ARG(d->name));
        } else if (d->kind == DECL_INPUT) {
            diag_error(f->local_at, "the input is what players send to the match, so none of it can stay on this machine");
            diag_note("what Sample needs between ticks goes in a local singleton, which it reads");
        } else {
            diag_error(f->local_at, "an event is sent whole, so none of its fields can be local");
            diag_note("'local' before a field is for the match's components and singletons: it keeps that field on this "
                      "machine, outside the match");
        }
        f->is_local = false;
        d->fields.items[kept++] = *f;
    }
    d->fields.count = kept;
}

static void collect_decls(program *prog)
{
    const decl_index *by_c_name = decls_by(prog, true);
    const int declared = prog->decls.count;
    for (int i = 0; i < declared; i++) {
        decl *d = prog->decls.items[i];
        const str ns = decl_ns(d);
        if (ns.len > 0) {
            sb qualified = {0};
            sb_printf(&qualified, STR_FMT "." STR_FMT, STR_ARG(ns), STR_ARG(d->name));
            d->qualified = (str){qualified.data, (int)qualified.len};
        } else {
            d->qualified = d->name;
        }
        if (!d->builtin) split_local_fields(prog, d);

        if (!d->builtin) {
            check_reserved(d->name, d->at);
            if (is_builtin_name(d->name)) {
                diag_error(d->at, "'" STR_FMT "' is built into the language", STR_ARG(d->name));
            }
            // The World a system takes, 'World world': a game's own goes in a namespace
            if (ns.len == 0 && str_eq_c(d->name, "World") && is_kind(d, NAME_TYPE)) {
                diag_error(d->at, "'" STR_FMT "' is built into the engine", STR_ARG(d->name));
            }
            // Against those before it that it could clash with: the ones with
            // its name in generated C (which the same name in the same
            // namespace has too), in decls' order
            sb mine = {0};
            c_name_of(d, &mine);
            for (int j = first_c_named(by_c_name, mine.data, mine.len); j >= 0 && j < i; j = by_c_name->next[j]) {
                const decl *other = prog->decls.items[j];
                const bool both_types = is_kind(d, NAME_TYPE) && is_kind(other, NAME_TYPE);
                const bool both_systems = d->kind == DECL_SYSTEM && other->kind == DECL_SYSTEM;
                const bool function = d->kind == DECL_FUNCTION || other->kind == DECL_FUNCTION || d->kind == DECL_SHADER
                                   || other->kind == DECL_SHADER;
                const bool constant = d->kind == DECL_CONST || other->kind == DECL_CONST;
                if (!both_types && !both_systems && !function && !constant) continue;
                if (str_eq(d->name, other->name) && str_eq(ns, decl_ns(other))) {
                    if (other->builtin) {
                        diag_error(d->at, "'" STR_FMT "' is built into the engine", STR_ARG(d->name));
                    } else {
                        diag_error(d->at, "'" STR_FMT "' is already declared", STR_ARG(d->qualified));
                        const source *src = diag_source(other->at.file);
                        if (src && other->at.file != d->at.file) {
                            diag_note_at(other->at, "the other one is in %s", src->path);
                        }
                    }
                    continue;
                }
                // Different names that generated C would spell the same: Combat.Health and Combat_Health.
                sb theirs = {0};
                c_name_of(other, &theirs);
                if (strcmp(mine.data, theirs.data) == 0) {
                    diag_error(d->at, "'" STR_FMT "' and '" STR_FMT "' would have the same name in generated C, %s",
                               STR_ARG(d->qualified), STR_ARG(other->qualified), mine.data);
                    diag_note("rename one of them");
                }
            }
        }

        if (d->is_local && !(d->kind == DECL_COMPONENT || d->kind == DECL_SINGLETON || d->kind == DECL_EVENT
                             || (d->kind == DECL_SYSTEM && d->is_handler))) {
            switch (d->kind) {
            case DECL_STRUCT:
                diag_error(d->local_at, "structs belong to neither side: the match and local state both hold them");
                break;
            case DECL_SYSTEM:
                if (d->is_view) diag_error(d->local_at, "views are always local; drop 'local'");
                else diag_error(d->local_at, "systems run the match; for code that runs every frame on this machine, write a view");
                break;
            case DECL_INPUT:
                diag_error(d->local_at, "the input is what players send to the match, so it can't be local");
                break;
            case DECL_CONST:
                diag_error(d->local_at, "constants belong to neither side: every machine has the same ones");
                break;
            case DECL_SHADER:
                diag_error(d->local_at, "a %s runs on the GPU for the meshes a view draws, so it's local already; drop 'local'",
                           d->is_frag ? "frag" : "vert");
                break;
            default:
                diag_error(d->local_at, "functions belong to neither side, so they can't be local");
                break;
            }
            diag_note("'local' goes before components, singletons, events and event handlers");
            d->is_local = false;
        }

        switch (d->kind) {
        case DECL_COMPONENT:
            if (d->is_scene) {
                // A scene's visibility and the players who see it, in generated C only.
                field visibility = {0};
                visibility.name = str_from("tide_visibility");
                visibility.type_name = str_from("int");
                visibility.hidden = true;
                field players = visibility;
                players.name = str_from("tide_players");
                vec_push(d->fields, visibility);
                vec_push(d->fields, players);
            }
            if (d->is_scene && str_eq_c(d->name, "Main") && !package_of(d)) { // A package's is an error of its own
                if (prog->main) {
                    diag_error(d->at, "there can only be one 'scene Main'");
                    const source *src = diag_source(prog->main->at.file);
                    if (src) diag_note_at(prog->main->at, "the other one is in %s", src->path);
                } else {
                    prog->main = d;
                }
            }
            d->index = prog->components.count;
            if (d->index == MAX_COMPONENTS) {
                diag_error(d->at, "too many components; the limit is %d for now", MAX_COMPONENTS);
            }
            vec_push(prog->components, d);
            break;
        case DECL_SINGLETON:
            d->index = prog->singletons.count;
            vec_push(prog->singletons, d);
            if (d->local_part) { // Its local fields: a local singleton of their own
                d->local_part->index = prog->singletons.count;
                vec_push(prog->singletons, d->local_part);
            }
            break;
        case DECL_EVENT:
            d->index = prog->events.count;
            vec_push(prog->events, d);
            if (d->is_command) {
                if (d->is_local) diag_error(d->local_at, "commands are sent to the match, so they can't be local");
                vec_push(prog->command_types, d);
            }
            break;
        case DECL_INPUT:
            if (prog->input) diag_error(d->at, "a game has one input declaration; '" STR_FMT "' is already it",
                                        STR_ARG(prog->input->name));
            else prog->input = d;
            break;
        case DECL_RECORD:
        case DECL_ENUM:
        case DECL_STRUCT: // Ordered once their fields are known; see order_struct
        case DECL_METHOD: // Not in prog->decls
        case DECL_LIST:
        case DECL_MAP:
        case DECL_GRID:
        case DECL_RESULT:
        case DECL_FUNCTION:
        case DECL_CONST:
        case DECL_SHADER:
        case DECL_SETTINGS: // Not in prog->decls
            break;
        case DECL_SYSTEM:
            if (d->is_view) {
                d->index = prog->views.count;
                vec_push(prog->views, d);
            } else if (d->is_handler) {
                d->index = prog->handlers.count;
                vec_push(prog->handlers, d);
            } else {
                d->index = prog->systems.count;
                vec_push(prog->systems, d);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Archetypes

// The match and the local world each have their own archetypes.
static int find_archetype(const program *prog, const comp_mask mask, const bool local)
{
    for (int i = 0; i < prog->archetypes.count; i++) {
        if (mask_eq(prog->archetypes.items[i], mask) && prog->archetype_local.items[i] == local) return i;
    }
    return -1;
}

// What each archetype's entities could still have added or removed this tick
// (see expand_archetypes), beside the sites that reach it.
typedef struct pending {
    mask_list adds;
    mask_list removes;
} pending;

// The archetype with `mask` (the components that aren't sparse), made if it's
// new: its index, or -1 past the limit.
static int archetype_index(program *prog, pending *p, const comp_mask mask, const bool local)
{
    const int found = find_archetype(prog, mask, local);
    if (found >= 0) return found;
    if (prog->archetypes.count == MAX_ARCHETYPES) return -1;
    vec_push(prog->archetypes, mask);
    vec_push(prog->possible, mask_none());
    vec_push(prog->archetype_local, local);
    vec_push(p->adds, mask_none());
    vec_push(p->removes, mask_none());
    return prog->archetypes.count - 1;
}

// `m` without the sparse components, which no archetype is made of.
static comp_mask solid(const program *prog, const comp_mask m)
{
    return mask_andnot(m, prog->sparse_mask);
}

// Whether a sparse component is one: [Sparse], or a tag (no fields) that any
// Add or Remove names, which would otherwise double the archetypes it can
// reach. A tag only set at Spawn stays part of its archetype, so filtering on
// it costs nothing a row.
static bool sparse_component(const program *prog, const decl *d)
{
    if (d->is_scene) return false;
    for (int a = 0; a < d->attributes.count; a++) {
        if (str_eq_c(d->attributes.items[a].name, "Sparse")) return true;
    }
    return d->fields.count == 0 && (mask_has(prog->added_mask, d->index) || mask_has(prog->removed_mask, d->index));
}

// Whether a system's (or handler's, or view's) filters admit archetype `mask`:
// the components it needs that aren't sparse are there, and none it excludes.
// Its sparse needs go by `possible`, when given: what those entities can have.
static bool filters_admit(const program *prog, const decl *sys, const comp_mask mask, const comp_mask *possible)
{
    if (!mask_subset(solid(prog, sys->need_mask), mask) || mask_intersects(mask, solid(prog, sys->without_mask))) return false;
    return !possible || mask_subset(mask_and(sys->need_mask, prog->sparse_mask), *possible);
}

// The components the sites that reach archetype `mask` add, or remove.
// Sparse components in a site's filters don't tell it apart from the
// archetype (its entities may or may not have them), so they're not asked.
static comp_mask reaching(const program *prog, const change_sites *sites, const comp_mask mask, const bool local)
{
    comp_mask all = mask_none();
    for (int i = 0; i < sites->count; i++) {
        const change_site *s = &sites->items[i];
        if (s->local != local) continue;
        if (s->spawn && !mask_eq(mask, solid(prog, s->spawn->spawn_mask))) continue;
        if (s->from && !filters_admit(prog, s->from, mask, NULL)) continue;
        all = mask_or(all, s->comps);
    }
    return all;
}

// What Add and Remove can turn each archetype from `from` on into. The
// changes recorded for an entity in X apply at the end of the tick in any
// order, so from X every combination of the components of the sites reaching
// X is reachable, a component at a time; and since X with a component more or
// less may be past a site's filters while the change it recorded still
// applies, the archetypes derived from X inherit what X could still apply
// (`pending`). Again until nothing changes. False past the limit.
// Archetype `i`'s entities can have the sparse components `more`: it and
// every archetype they can move to keep a presence byte for each
// (`possible`). True if that's new.
static bool allow_sparse(program *prog, const int i, const comp_mask more)
{
    if (mask_subset(more, prog->possible.items[i])) return false;
    prog->possible.items[i] = mask_or(prog->possible.items[i], more);
    return true;
}

static bool expand_archetypes(program *prog, pending *p, const int from)
{
    bool again = true;
    while (again) {
        again = false;
        for (int i = from; i < prog->archetypes.count; i++) {
            const comp_mask current = prog->archetypes.items[i];
            const bool local = prog->archetype_local.items[i];
            const comp_mask adds = mask_or(reaching(prog, &prog->adds, current, local), p->adds.items[i]);
            const comp_mask removes = mask_or(reaching(prog, &prog->removes, current, local), p->removes.items[i]);
            // A sparse component added here is one its entities can have, in place
            allow_sparse(prog, i, mask_and(adds, prog->sparse_mask));
            for (int bit_index = 0; bit_index < prog->components.count; bit_index++) {
                if (prog->components.items[bit_index]->is_local != local || mask_has(prog->sparse_mask, bit_index)) continue;
                const comp_mask b = mask_bit(bit_index);
                for (int pass = 0; pass < 2; pass++) {
                    if (!mask_has(pass ? removes : adds, bit_index)) continue;
                    const int j = archetype_index(prog, p, pass ? mask_andnot(current, b) : mask_or(current, b), local);
                    if (j < 0) return false;
                    // Entities move both ways between the two: what either can have, both keep room for
                    const bool grew_j = allow_sparse(prog, j, prog->possible.items[i]);
                    const bool grew_i = allow_sparse(prog, i, prog->possible.items[j]);
                    if ((grew_j || grew_i) && j < i) again = true;
                    if (mask_subset(adds, p->adds.items[j]) && mask_subset(removes, p->removes.items[j])) continue;
                    p->adds.items[j] = mask_or(p->adds.items[j], adds);
                    p->removes.items[j] = mask_or(p->removes.items[j], removes);
                    if (j < i) again = true; // Expanded already, with less
                }
            }
        }
    }
    return true;
}

static void append_component_names(const program *prog, const comp_mask mask, char *buf, const size_t size)
{
    size_t len = strlen(buf);
    bool first = true;
    for (int i = 0; i < prog->components.count && len + 1 < size; i++) {
        if (!mask_has(mask, i)) continue;
        const int n = snprintf(buf + len, size - len, "%s" STR_FMT, first ? "" : ", ", STR_ARG(prog->components.items[i]->name));
        if (n > 0) len += (size_t)n;
        first = false;
    }
}

// Every spawn creates an archetype, and the Adds and Removes that can reach
// each archetype's entities (change_site) make the rest, until nothing new
// appears.
static void derive_archetypes(const checker *c)
{
    program *prog = c->prog;
    pending p = {{0}, {0}};
    // The sparse components: a presence byte in every archetype they can reach, not archetypes of their own
    for (int i = 0; i < prog->components.count; i++) {
        decl *d = prog->components.items[i];
        d->is_sparse = sparse_component(prog, d);
        if (d->is_sparse) prog->sparse_mask = mask_or(prog->sparse_mask, bit(d));
    }
    for (int i = 0; i < c->spawns.count; i++) {
        const expr *spawn = c->spawns.items[i];
        const int a = archetype_index(prog, &p, solid(prog, spawn->spawn_mask), spawn->local_world);
        if (a < 0) goto too_many;
        allow_sparse(prog, a, mask_and(spawn->spawn_mask, prog->sparse_mask));
    }
    if (!expand_archetypes(prog, &p, 0)) goto too_many;
    // The Main scene, which the engine loads, after every other archetype so
    // theirs keep their order. What Add and Remove derive from it comes after.
    // A package on its own, as editors analyze one, has none.
    if (prog->main && archetype_index(prog, &p, bit(prog->main), prog->main->is_local) < 0) goto too_many;
    prog->main_archetype = prog->main ? find_archetype(prog, bit(prog->main), prog->main->is_local) : prog->archetypes.count;
    if (!expand_archetypes(prog, &p, prog->main_archetype)) goto too_many;
    for (int i = 0; i < prog->archetypes.count; i++) vec_push(prog->spawn_target, false);
    if (prog->main) prog->spawn_target.items[prog->main_archetype] = true;
    for (int i = 0; i < c->spawns.count; i++) {
        const int a = find_archetype(prog, solid(prog, c->spawns.items[i]->spawn_mask), c->spawns.items[i]->local_world);
        c->spawns.items[i]->spawn_archetype = a;
        prog->spawn_target.items[a] = true;
    }
    return;

too_many:
    diag_error((loc){1, 1, 0}, "the program can create more than %d different component combinations", MAX_ARCHETYPES);
    diag_note("an Add or Remove on an entity read from a field or a parameter can apply to any entity, so combinations "
              "multiply; one on 'this', or on an entity a Spawn in the same code made, only reaches what it can");
}

// "system", "view" or "event handler", for messages.
static const char *system_what(const decl *sys)
{
    return sys->is_view ? "view" : sys->is_handler ? "event handler" : "system";
}

static void warn_unmatched(const program *prog, const decl *const *list, const int count)
{
    for (int i = 0; i < count; i++) {
        const decl *sys = list[i];
        // A package's systems run on what the game makes, or don't: it may use only part of it.
        if (!sys->per_entity || package_of(sys)) continue;
        bool matched = false;
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (prog->archetype_local.items[a] != sys->entity_local) continue;
            if (filters_admit(prog, sys, prog->archetypes.items[a], &prog->possible.items[a])) matched = true;
        }
        if (!matched) {
            diag_warning(sys->at, "%s '" STR_FMT "' never runs: no entity matches its parameters", system_what(sys),
                         STR_ARG(sys->name));
            if (mask_any(sys->need_mask)) {
                char names[512] = "";
                append_component_names(prog, sys->need_mask, names, sizeof names);
                diag_note("nothing spawns or adds an entity with %s", names);
            }
        }
    }
}

// A Send without an entity, of an event whose handler reads the entity it's sent to.
static void check_sends(const checker *c)
{
    for (int i = 0; i < c->sends.count; i++) {
        const expr *e = c->sends.items[i];
        const decl *event = e->type_decl;
        if (e->kind == E_METHOD || !event->needs_target) continue;
        diag_error(e->at, "'" STR_FMT "' needs the entity a " STR_FMT " is sent to, and this Send has none",
                   STR_ARG(event->needs_target->name), STR_ARG(event->name));
        diag_note("send it to an entity: 'entity.Send(" STR_FMT " { ... })'", STR_ARG(event->name));
    }
}

// A handler of an event nothing sends never runs.
static void warn_unsent(const checker *c)
{
    for (int i = 0; i < c->prog->handlers.count; i++) {
        const decl *h = c->prog->handlers.items[i];
        if (!h->event || h->event->builtin || h->is_command || package_of(h)) continue; // Commands may also come from C hosts
        bool sent = false;
        for (int k = 0; k < c->sends.count && !sent; k++) sent = c->sends.items[k]->type_decl == h->event;
        if (sent) continue;
        diag_warning(h->at, "event handler '" STR_FMT "' never runs: nothing sends '" STR_FMT "'", STR_ARG(h->name),
                     STR_ARG(h->event->name));
        diag_note("send it with 'Send(" STR_FMT " { ... })', or to an entity with 'entity.Send(...)'",
                  STR_ARG(h->event->name));
    }
}

// input PlayerInput { ...; Sample(Devices devices) { ... } }: Sample runs on
// the client, outside the simulation. It takes this machine's devices and local
// singletons it takes, and assigns the input's fields, which start at their
// defaults.
static void check_sample(checker *c, decl *input)
{
    if (!input->body) return; // Without Sample, sampling gives the defaults.

    bool has_devices = false;
    for (int i = 0; i < input->params.count; i++) {
        param *p = &input->params.items[i];
        const loc at = p->type_at.line ? p->type_at : p->at;
        check_reserved(p->name, p->at);
        p->type = T_ERR;
        for (int j = 0; j < i; j++) {
            if (str_eq(input->params.items[j].name, p->name)) {
                diag_error(p->at, "parameter '" STR_FMT "' is declared twice", STR_ARG(p->name));
            }
        }
        if (str_eq_c(p->type_name, "Devices")) {
            if (p->mode != PARAM_READ) diag_error(p->at, "devices can't be 'mut', 'with' or 'without'; code can only read them");
            if (has_devices) diag_error(p->at, "Sample can only have one Devices parameter");
            has_devices = true;
            p->type = (type){TY_RECORD, c->prog->devices};
            continue;
        }
        decl *d = find_type(c, p->type_name, at);
        if (!d || d->kind != DECL_SINGLETON || !d->is_local) {
            if (d && d->kind == DECL_SINGLETON) {
                diag_error(at, "'" STR_FMT "' belongs to the match, and Sample only reads local singletons",
                           STR_ARG(d->name));
                diag_note("the input is what this machine sends the match; systems read the match");
            } else if (d) {
                diag_error(at, "Sample takes local singletons, and '" STR_FMT "' is %s", STR_ARG(d->name), decl_what(d));
            } else if (builtin_type_named(p->type_name, &(type){0}) || str_eq_c(p->type_name, "string")) {
                diag_error(at, "Sample takes local singletons, and '" STR_FMT "' is a built-in type", STR_ARG(p->type_name));
                diag_note("keep what it reads in one, like 'local singleton Settings { ... }', and take that");
            } else {
                diag_error(at, "unknown local singleton '" STR_FMT "'", STR_ARG(p->type_name));
            }
            continue;
        }
        if (p->mode != PARAM_READ) {
            diag_error(p->at, "Sample only reads local state, so '" STR_FMT "' can't be 'mut', 'with' or 'without'",
                       STR_ARG(d->name));
        }
        p->mode = PARAM_READ;
        p->type = (type){TY_SINGLETON, d};
    }

    c->system = input;
    c->in_input = true;
    check_stmt(c, input->body);
    c->in_input = false;
    c->system = NULL;
}

// Sanitize() { ... }: runs on every input before the simulation reads it, so
// systems can rely on what it guarantees. It assigns the input's fields by
// name, like Sample, and reads nothing else.
static void check_sanitize(checker *c, decl *input)
{
    if (!input->sanitize) return;
    c->system = input;
    c->in_input = true;
    c->in_sanitize = true;
    check_stmt(c, input->sanitize);
    c->in_sanitize = false;
    c->in_input = false;
    c->system = NULL;
}

// `namespace` and `using` at the top of each file.
static void check_units(const program *prog)
{
    for (int i = 0; i < prog->units.count; i++) {
        const unit *u = prog->units.items[i];
        if (u->ns.len > 0) {
            const char *dot = memchr(u->ns.ptr, '.', (size_t)u->ns.len);
            const str root = {u->ns.ptr, dot ? (int)(dot - u->ns.ptr) : u->ns.len};
            if (is_builtin_name(root)) {
                diag_error(u->ns_at, "'" STR_FMT "' is built into the language, so it can't name a namespace", STR_ARG(root));
            }
            check_reserved(u->ns, u->ns_at);
        }
        for (int k = 0; k < u->usings.count; k++) {
            const str used = u->usings.items[k];
            if (is_namespace(prog, used)) continue;
            diag_error(u->using_at.items[k], "no file declares the namespace '" STR_FMT "'", STR_ARG(used));
            suggestion s = suggest_start(used);
            for (int j = 0; j < prog->units.count; j++) suggest_consider(&s, prog->units.items[j]->ns);
            suggest_note(&s);
        }
    }
}

// Whether every file is a package's: one package on its own, as the language
// server analyzes it, with no game to have Main.
static bool only_packages(const program *prog)
{
    for (int i = 0; i < prog->units.count; i++) {
        if (!prog->units.items[i]->src->package) return false;
    }
    return prog->units.count > 0;
}

// A package's files put what they declare in its namespace, or one inside it,
// and leave what a game has one of (Main, its input and its settings) to the
// game.
static void check_packages(const program *prog)
{
    for (int i = 0; i < prog->units.count; i++) {
        const unit *u = prog->units.items[i];
        const char *package = u->src->package;
        if (!package) continue;
        const int n = (int)strlen(package);
        if (u->ns.len >= n && memcmp(u->ns.ptr, package, (size_t)n) == 0 && (u->ns.len == n || u->ns.ptr[n] == '.')) {
            continue;
        }
        if (u->ns.len > 0) {
            diag_error(u->ns_at, "a file of package %s is in its namespace, or one inside it, not '" STR_FMT "'", package,
                       STR_ARG(u->ns));
            diag_note("write 'namespace %s;', or one inside it like 'namespace %s." STR_FMT ";'", package, package,
                      STR_ARG(u->ns));
            continue;
        }
        loc first = {0};
        for (int k = 0; k < prog->decls.count && !first.line; k++) {
            if (prog->decls.items[k]->unit == u) first = prog->decls.items[k]->at;
        }
        for (int k = 0; k < prog->settings.count && !first.line; k++) {
            if (prog->settings.items[k]->at.file == u->src->file) first = prog->settings.items[k]->at;
        }
        if (!first.line) continue; // Nothing declared
        diag_error(first, "what package %s declares goes in its namespace, %s", package, package);
        diag_note("start the file with 'namespace %s;'", package);
    }
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        const char *package = package_of(d);
        if (!package) continue;
        if (d->kind == DECL_COMPONENT && d->is_scene && str_eq_c(d->name, "Main")) {
            diag_error(d->at, "package %s can't declare Main: the game that uses it does", package);
            diag_note("give the package's scene another name, for the game to load");
        } else if (d->kind == DECL_INPUT) {
            diag_error(d->at, "package %s can't declare the input: the game that uses it does", package);
            diag_note("a package's systems can take 'Devices', which the game's input sends with its own fields");
        }
    }
    for (int i = 0; i < prog->settings.count; i++) {
        const source *src = diag_source(prog->settings.items[i]->at.file);
        if (!src || !src->package) continue;
        diag_error(prog->settings.items[i]->at, "package %s can't have settings: they're the game's", src->package);
    }
}

// A cell written as constants, int2(0, -1) or a constant that's one, into `out`.
static bool fold_cell(const expr *e, const int dims, int64_t out[3])
{
    if ((e->kind == E_NAME || e->kind == E_MEMBER) && e->bind == BIND_CONST) {
        return e->constant->value && fold_cell(e->constant->value, dims, out);
    }
    if (e->kind != E_CALL || e->call != CALL_CONSTRUCT) return false;
    if (e->ctor == CTOR_SPLAT && e->args.count == 1) {
        int64_t v;
        if (!fold_int(e->args.items[0], &v)) return false;
        for (int i = 0; i < dims; i++) out[i] = v;
        return true;
    }
    if (e->ctor != CTOR_COMPONENTS || e->args.count != dims) return false;
    for (int i = 0; i < dims; i++) {
        if (!fold_int(e->args.items[i], &out[i])) return false;
    }
    return true;
}

// An ordering attribute can name an enum's member as well as a declaration.
// The enum name uses the same namespace and using rules as an ordinary type.
// With `required`, this is Stage's argument, so every other name is an error.
static bool resolve_order_member(checker *c, const decl *d, qname *q, const bool required)
{
    str enum_name;
    str member_name;
    if (!split_qualified(q->text, &enum_name, &member_name)) {
        if (!required) return false;
        diag_error(q->name_at, "Stage names an enum member, like [Stage(RenderStages.Objects)]");
        return true;
    }
    decl *other;
    decl *enumeration = lookup(c->prog, d->unit, enum_name, NAME_TYPE, &other);
    if (!enumeration) {
        if (!required) return false;
        diag_error(q->at, "unknown enum '" STR_FMT "'", STR_ARG(enum_name));
        suggestion s = suggest_start(enum_name);
        for (int i = 0; i < c->prog->decls.count; i++) {
            if (c->prog->decls.items[i]->kind == DECL_ENUM) suggest_consider(&s, c->prog->decls.items[i]->name);
        }
        suggest_note(&s);
        return true;
    }
    if (other) {
        diag_error(q->at, "'" STR_FMT "' is ambiguous: both " STR_FMT " and " STR_FMT " have it", STR_ARG(enum_name),
                   STR_ARG(decl_ns(enumeration)), STR_ARG(decl_ns(other)));
        diag_note("write which one you mean, like '" STR_FMT "." STR_FMT "'", STR_ARG(enumeration->qualified),
                  STR_ARG(member_name));
        return true;
    }
    if (enumeration->kind != DECL_ENUM) {
        if (!required) return false;
        diag_error(q->at, "'" STR_FMT "' is %s, not an enum", STR_ARG(enum_name), decl_what(enumeration));
        return true;
    }
    for (int i = 0; i < enumeration->members.count; i++) {
        if (!str_eq(enumeration->members.items[i].name, member_name)) continue;
        q->decl = enumeration;
        q->enum_member = &enumeration->members.items[i];
        return true;
    }
    diag_error(q->name_at, "enum '" STR_FMT "' has no member '" STR_FMT "'", STR_ARG(enumeration->name),
               STR_ARG(member_name));
    suggestion s = suggest_start(member_name);
    for (int i = 0; i < enumeration->members.count; i++) suggest_consider(&s, enumeration->members.items[i].name);
    suggest_note(&s);
    return true;
}

static void check_stage_attribute(checker *c, decl *d, attribute *attr)
{
    if (d->kind != DECL_SYSTEM) {
        diag_error(attr->at, "Stage orders systems, views and event handlers; '" STR_FMT "' is none of them", STR_ARG(d->name));
        return;
    }
    if (attr->args.count != 1 || attr->values.count > 1) {
        diag_error(attr->at, "Stage takes an enum member and an optional int offset: [Stage(RenderStages.Objects, 10)]");
        return;
    }
    qname *q = &attr->args.items[0];
    resolve_order_member(c, d, q, true);
    int64_t offset = 0;
    if (attr->values.count) {
        expr *value = attr->values.items[0];
        if (!is_constant(c, value)) {
            diag_error(value->at, "a Stage offset must be a constant int");
            suggest_constant(c, value);
            return;
        }
        const type t = check_constant_expr(c, value, true, T_INT_);
        if (t.kind == TY_ERROR) return;
        if (!type_assignable(T_INT_, t)) {
            diag_error(value->at, "a Stage offset must be a constant int");
            return;
        }
        if (!fold_int(value, &offset)) {
            diag_error(value->at, "a Stage offset must be an int known while compiling");
            diag_note("use int literals, named constants, integer conversions and operators, conditionals, or integer Math functions");
            return;
        }
    }
    if (!q->enum_member) return;
    d->order_enum = q->decl;
    d->order_member = q->enum_member;
    d->order_offset = (int)offset;
    d->order_at = attr->at;
}

static void order_after(decl *later, decl *earlier)
{
    for (int i = 0; i < later->after.count; i++) {
        if (later->after.items[i] == earlier) return;
    }
    vec_push(later->after, earlier);
}

// [Before(X)] and [After(X)] order declarations or whole enum phases.
// Stage assigns one of those phases and an offset within it.
static void check_attributes(checker *c)
{
    for (int i = 0; i < c->prog->decls.count; i++) {
        decl *d = c->prog->decls.items[i];
        c->unit = d->unit;
        bool stage_seen = false;
        for (int a = 0; a < d->attributes.count; a++) {
            attribute *attr = &d->attributes.items[a];
            if (str_eq_c(attr->name, "Stage")) {
                if (stage_seen) {
                    diag_error(attr->at, "'" STR_FMT "' already has a Stage", STR_ARG(d->name));
                } else {
                    check_stage_attribute(c, d, attr);
                }
                stage_seen = true;
                continue;
            }
            if (str_eq_c(attr->name, "NativeName")) { // Checked with the extern's signature
                if (!d->is_extern) {
                    diag_error(attr->at, "NativeName names the C function of an extern function, and '" STR_FMT
                               "' isn't one", STR_ARG(d->name));
                    diag_note("declare a C function as 'extern float Noise(float x);'");
                }
                continue;
            }
            if (str_eq_c(attr->name, "Sparse")) { // Read as the archetypes derive (sparse_component)
                if (d->kind != DECL_COMPONENT || d->is_scene) {
                    diag_error(attr->at, "Sparse marks a component that entities gain and lose, and '" STR_FMT "' isn't one",
                               STR_ARG(d->name));
                    diag_note("a sparse component is a presence bit and its value in every archetype it can reach, rather than "
                              "an archetype of its own: '[Sparse] component Stunned { float left; }'");
                } else if (attr->values.count > 0 || attr->args.count > 0) {
                    diag_error(attr->at, "Sparse takes nothing: '[Sparse]'");
                }
                continue;
            }
            const bool before = str_eq_c(attr->name, "Before");
            if (!before && !str_eq_c(attr->name, "After")) {
                diag_error(attr->at, "unknown attribute '" STR_FMT "'", STR_ARG(attr->name));
                suggestion s = suggest_start(attr->name);
                suggest_consider_c(&s, "Before");
                suggest_consider_c(&s, "After");
                suggest_consider_c(&s, "Stage");
                suggest_consider_c(&s, "NativeName");
                suggest_consider_c(&s, "Sparse");
                suggest_note(&s);
                diag_note("the attributes are Before and After, which order systems: [After(Physics.Integrate)], "
                          "Stage, which assigns an enum phase: [Stage(RenderStages.Objects)], "
                          "NativeName, which names an extern function's C function, and Sparse, for a component that "
                          "entities gain and lose");
                continue;
            }
            if (attr->values.count > 0) {
                diag_error(attr->values.items[0]->at, "'" STR_FMT "' takes names, not text: [" STR_FMT "(Movement)]",
                           STR_ARG(attr->name), STR_ARG(attr->name));
                continue;
            }
            if (d->kind != DECL_SYSTEM) {
                diag_error(attr->at, "'" STR_FMT "' orders systems, views and event handlers; '" STR_FMT "' is none of them",
                           STR_ARG(attr->name), STR_ARG(d->name));
                continue;
            }
            const char *kind = system_what(d);
            if (attr->args.count == 0) {
                diag_error(attr->at, "'" STR_FMT "' needs the %ss it runs %s, like [" STR_FMT "(Movement)]",
                           STR_ARG(attr->name), kind, before ? "before" : "after", STR_ARG(attr->name));
                continue;
            }
            for (int k = 0; k < attr->args.count; k++) {
                qname *q = &attr->args.items[k];
                decl *other;
                decl *target = lookup(c->prog, d->unit, q->text, NAME_SYSTEM, &other);
                if (!target) {
                    if (resolve_order_member(c, d, q, false)) continue;
                    diag_error(q->name_at, "unknown %s '" STR_FMT "'", kind, STR_ARG(q->text));
                    suggestion s = suggest_start(q->text);
                    for (int j = 0; j < c->prog->decls.count; j++) {
                        const decl *candidate = c->prog->decls.items[j];
                        if (candidate->kind == DECL_SYSTEM && candidate->is_view == d->is_view
                            && candidate->is_handler == d->is_handler) {
                            suggest_consider(&s, candidate->name);
                        }
                    }
                    suggest_note(&s);
                    continue;
                }
                if (other) {
                    diag_error(q->name_at, "'" STR_FMT "' is ambiguous: both " STR_FMT " and " STR_FMT " have it",
                               STR_ARG(q->text), STR_ARG(decl_ns(target)), STR_ARG(decl_ns(other)));
                    diag_note("write which one you mean, like '" STR_FMT "'", STR_ARG(target->qualified));
                    continue;
                }
                q->decl = target;
                if (target == d) {
                    diag_error(q->name_at, "a %s can't run %s itself", kind, before ? "before" : "after");
                } else if (target->is_handler != d->is_handler) {
                    diag_error(q->name_at, "%s and event handlers are ordered separately: handlers run when their event "
                                           "is sent, at the end of the tick", d->is_view || target->is_view ? "views" : "systems");
                } else if (d->is_handler && d->event && target->event && d->event != target->event) {
                    diag_error(q->name_at, "'" STR_FMT "' handles " STR_FMT " and '" STR_FMT "' handles " STR_FMT
                               "; only handlers of the same event run in an order", STR_ARG(d->name), STR_ARG(d->event->name),
                               STR_ARG(target->name), STR_ARG(target->event->name));
                } else if (target->is_view != d->is_view) {
                    diag_error(q->name_at, "systems and views are ordered separately: views draw once per frame, "
                                           "after the ticks");
                } else if (before) {
                    order_after(target, d);
                } else {
                    order_after(d, target);
                }
            }
        }
    }
}

static bool has_phase_order(const decl *d)
{
    if (d->order_enum) return true;
    for (int a = 0; a < d->attributes.count; a++) {
        const attribute *attr = &d->attributes.items[a];
        for (int k = 0; k < attr->args.count; k++) {
            if (attr->args.items[k].enum_member) return true;
        }
    }
    return false;
}

typedef struct order_node {
    VEC(int) next;
} order_node;

typedef struct order_phase {
    const decl *enumeration;
    const decl *event; // Handlers of different events have independent phases.
    int first;        // Two boundary nodes per member, in declaration order.
} order_phase;

typedef struct order_graph {
    VEC(order_node) nodes;
    VEC(order_phase) phases;
} order_graph;

static void order_edge(order_graph *g, const int from, const int to)
{
    vec_push(g->nodes.items[from].next, to);
}

// A phase has a beginning and an end even when no declaration belongs to it.
// This preserves constraints such as After(Effects) plus Before(Objects),
// whose contradiction must still be caught when either phase is empty.
static int phase_begin(order_graph *g, const decl *enumeration, const enum_member *member, const decl *event)
{
    const int index = (int)(member - enumeration->members.items);
    for (int i = 0; i < g->phases.count; i++) {
        const order_phase *p = &g->phases.items[i];
        if (p->enumeration == enumeration && p->event == event) return p->first + 2 * index;
    }
    const int first = g->nodes.count;
    const order_phase phase = {enumeration, event, first};
    vec_push(g->phases, phase);
    for (int i = 0; i < enumeration->members.count * 2; i++) {
        const order_node node = {0};
        vec_push(g->nodes, node);
    }
    for (int i = 0; i < enumeration->members.count; i++) {
        const int begin = first + 2 * i;
        order_edge(g, begin, begin + 1);
        if (i) order_edge(g, begin - 1, begin);
    }
    return first + 2 * index;
}

// Phase barriers produce many after links. Keep membership as bits while
// adding them so duplicate detection costs O(1), even for large phases.
static void phase_after(decl **list, const int later, const int earlier, uint64_t *edges, const size_t words)
{
    uint64_t *word = edges + (size_t)later * words + ((size_t)earlier >> 6);
    const uint64_t bit = (uint64_t)1 << (earlier & 63);
    if (*word & bit) return;
    *word |= bit;
    vec_push(list[later]->after, list[earlier]);
}

// Project phase boundaries onto declarations' existing after links, so both
// the stable scheduler and parallel system waits obey the same constraints.
// Enums are independent timelines; declaration Before/After can join them.
static void order_phases(decl **list, const int n)
{
    bool any = false;
    for (int i = 0; i < n && !any; i++) any = has_phase_order(list[i]);
    if (!any) return;
    const size_t words = ((size_t)n + 63u) / 64u;
    uint64_t *edges = arena_alloc(sizeof(uint64_t) * words * (size_t)n);
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < list[i]->after.count; k++) {
            const decl *earlier = list[i]->after.items[k];
            const int index = earlier->index;
            if (index >= 0 && index < n && list[index] == earlier) {
                edges[(size_t)i * words + ((size_t)index >> 6)] |= (uint64_t)1 << (index & 63);
            }
        }
    }
    order_graph g = {0};
    for (int i = 0; i < n; i++) {
        const order_node node = {0};
        vec_push(g.nodes, node);
    }
    for (int i = 0; i < n; i++) {
        decl *d = list[i];
        const decl *event = d->is_handler ? d->event : NULL;
        if (d->order_enum) {
            const int begin = phase_begin(&g, d->order_enum, d->order_member, event);
            order_edge(&g, begin, i);
            order_edge(&g, i, begin + 1);
        }
        for (int a = 0; a < d->attributes.count; a++) {
            const attribute *attr = &d->attributes.items[a];
            const bool before = str_eq_c(attr->name, "Before");
            if (!before && !str_eq_c(attr->name, "After")) continue;
            for (int k = 0; k < attr->args.count; k++) {
                const qname *q = &attr->args.items[k];
                if (!q->enum_member) continue;
                const int begin = phase_begin(&g, q->decl, q->enum_member, event);
                order_edge(&g, before ? i : begin + 1, before ? begin : i);
            }
        }
        // Offsets stay inside one phase; ties add no constraints and retain
        // source order unless explicit declaration dependencies say otherwise.
        for (int j = 0; d->order_enum && j < i; j++) {
            decl *other = list[j];
            if (other->order_enum != d->order_enum || other->order_member != d->order_member
                || (d->is_handler && other->event != d->event)) continue;
            if (other->order_offset < d->order_offset) phase_after(list, i, j, edges, words);
            else if (other->order_offset > d->order_offset) phase_after(list, j, i, edges, words);
        }
    }
    if (!g.phases.count) return;
    bool *seen = arena_alloc(sizeof(bool) * (size_t)g.nodes.count);
    int *stack = arena_alloc(sizeof(int) * (size_t)g.nodes.count);
    for (int i = 0; i < n; i++) {
        memset(seen, 0, sizeof(bool) * (size_t)g.nodes.count);
        int count = 0;
        stack[count++] = i;
        while (count) {
            const order_node *node = &g.nodes.items[stack[--count]];
            for (int k = 0; k < node->next.count; k++) {
                const int to = node->next.items[k];
                if (to < n) {
                    phase_after(list, to, i, edges, words);
                } else if (!seen[to]) {
                    seen[to] = true;
                    stack[count++] = to;
                }
            }
        }
    }
}

// Whether `from` calls `target`, itself or through other functions.
static bool calls(const decl *from, const decl *target, const decl **seen, int *seen_count, const decl **through)
{
    for (int i = 0; i < from->callees.count; i++) {
        const decl *to = from->callees.items[i];
        if (to == target) return true;
        bool visited = false;
        for (int k = 0; k < *seen_count; k++) visited |= seen[k] == to;
        if (visited) continue;
        seen[(*seen_count)++] = to;
        if (calls(to, target, seen, seen_count, through)) {
            if (!*through) *through = to;
            return true;
        }
    }
    return false;
}

// Whether async function `from` awaits `target`, itself or through others.
static bool awaits(const decl *from, const decl *target, const decl **seen, int *seen_count)
{
    for (int i = 0; i < from->awaits.count; i++) {
        const decl *to = from->awaits.items[i];
        if (to == target) return true;
        bool visited = false;
        for (int k = 0; k < *seen_count; k++) visited |= seen[k] == to;
        if (visited) continue;
        seen[(*seen_count)++] = to;
        if (awaits(to, target, seen, seen_count)) return true;
    }
    return false;
}

// The world a call of an async function is made in: a system's or handler's,
// a view's, or an async function's own (TASK_EITHER while nothing decides).
static int caller_side(const decl *from)
{
    if (from->kind == DECL_FUNCTION) return from->task_side;
    return is_local_code(from) ? TASK_LOCAL : TASK_MATCH;
}

// After every body: the world each async function runs in. What an async
// function awaits or starts runs in its world too, so a callee's side is its
// caller's; a function whose side nothing decides runs in whichever world
// starts it, and codegen makes it for each. A function awaiting itself would
// hold its own frame.
static void resolve_tasks(checker *c)
{
    program *prog = c->prog;
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < c->task_calls.count; i++) {
            const task_call *t = &c->task_calls.items[i];
            if (t->from->kind != DECL_FUNCTION || t->from->task_side != TASK_EITHER || t->to->task_side == TASK_EITHER) continue;
            t->from->task_side = t->to->task_side;
            t->from->task_side_at = t->call->at;
            changed = true;
        }
    }
    for (int i = 0; i < c->task_calls.count; i++) {
        const task_call *t = &c->task_calls.items[i];
        const int from = caller_side(t->from);
        const int to = t->to->task_side;
        if (from == TASK_EITHER || to == TASK_EITHER || from == to) continue;
        const char *does = t->awaited ? "await" : "start";
        if (to == TASK_LOCAL) {
            diag_error(t->call->at, "'" STR_FMT "' uses local state, on line %d, so the match can't %s it",
                       STR_ARG(t->to->name), t->to->task_side_at.line, does);
            diag_note("local state belongs to one machine; %s it from local code, like a view", does);
        } else {
            diag_error(t->call->at, "'" STR_FMT "' changes the match, on line %d, so local code can't %s it",
                       STR_ARG(t->to->name), t->to->task_side_at.line, does);
            diag_note("put what it wants in the input, and %s it from a system", does);
        }
    }
    for (int i = 0; i < prog->decls.count; i++) {
        decl *h = prog->decls.items[i];
        if (h->kind == DECL_SYSTEM && h->is_async) h->runs_in[h->is_local] = true;
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < c->task_calls.count; i++) {
            const task_call *t = &c->task_calls.items[i];
            if (!t->awaited) t->to->task_root = true;
            for (int w = 0; w < 2; w++) {
                const bool runs = t->from->kind == DECL_FUNCTION ? t->from->runs_in[w] : is_local_code(t->from) == (w == 1);
                if (!runs || t->to->runs_in[w]) continue;
                t->to->runs_in[w] = true;
                changed = true;
            }
        }
    }
    const decl **seen = arena_alloc(sizeof(decl *) * (size_t)(prog->decls.count + 1));
    for (int i = 0; i < prog->decls.count; i++) {
        decl *fn = prog->decls.items[i];
        if (fn->kind != DECL_FUNCTION || !fn->is_async) continue;
        int seen_count = 0;
        if (awaits(fn, fn, seen, &seen_count)) {
            diag_error(fn->at, "'" STR_FMT "' awaits itself, so its task would hold itself while it waits", STR_ARG(fn->name));
            diag_note("loop instead, or start it again without 'await', as a task of its own");
        }
    }
    // Tasks keep their text and lists in their world's heap, and make them in
    // the scratch area as they go
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (!d->is_async || (d->kind == DECL_FUNCTION && !d->runs_in[0] && !d->runs_in[1])) continue;
        prog->uses_heap = true;
        prog->uses_text = true;
    }
}

// Code that calls C, itself or through the functions and methods it calls,
// may change what C keeps: its calls are side effects, which codegen runs in
// source order like spawns (see hoist_spawns), whatever order C picks.
static void mark_c_callers(const checker *c)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < c->calls.count; i++) {
            const call_site *site = &c->calls.items[i];
            if (site->from->calls_c || !(site->to->is_extern || site->to->calls_c)) continue;
            site->from->calls_c = true;
            changed = true;
        }
    }
}

// A function that calls one that draws draws too. Only views and functions
// can call them: they need the frame. A function that takes an Action is copied
// into each call, so it can't call itself.
static void check_drawing_calls(checker *c)
{
    const program *prog = c->prog;
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < prog->decls.count; i++) {
            decl *fn = prog->decls.items[i];
            if (fn->kind != DECL_FUNCTION || fn->draws || fn->is_async) continue;
            for (int k = 0; k < fn->callees.count && !fn->draws; k++) {
                if (!fn->callees.items[k]->draws) continue;
                fn->draws = true;
                fn->draws_at = fn->callee_at.items[k];
                fn->frame_devices = fn->callees.items[k]->frame_devices;
                fn->frame_cursor = fn->callees.items[k]->frame_cursor;
                changed = true;
            }
        }
    }
    for (int i = 0; i < c->calls.count; i++) {
        const call_site *site = &c->calls.items[i];
        if (!site->to->draws || (site->from->kind == DECL_FUNCTION && !site->from->is_async)) continue;
        if (site->from->kind == DECL_SYSTEM && site->from->is_view) continue;
        const bool devices = site->to->frame_devices;
        const bool cursor = site->to->frame_cursor;
        const char *does = devices ? "reads this frame's Devices" : cursor ? "uses this frame's Cursor" : "draws";
        if (site->from->is_async) {
            diag_error(site->call->at, "'" STR_FMT "' %s, and a task runs between frames", STR_ARG(site->to->name), does);
            diag_note(cursor ? "a view locks the cursor, every frame the game wants the mouse, and reads whether it holds"
                             : "set what to show in local state, and draw that in a view");
            continue;
        }
        diag_error(site->call->at, "'" STR_FMT "' %s, so only views and the functions they call can call it",
                   STR_ARG(site->to->name), does);
        diag_note_at(site->to->draws_at, "it %s on line %d, and %s", devices ? "reads them" : cursor ? "uses Cursor" : "draws",
                     site->to->draws_at.line,
                     site->from->kind == DECL_METHOD ? "methods can't"
                     : site->from->kind == DECL_INPUT ? "the input's code runs without a frame"
                                                      : "systems and handlers run in the tick, without a frame");
        if (cursor && site->from->kind == DECL_INPUT) {
            diag_note("Sample reads 'Cursor.locked' itself: pass the function what it needs, like 'bool locked'");
        } else if (devices && site->from->kind == DECL_INPUT) {
            diag_note("typed text is a frame's: Sample passes devices to helpers that only read sampled controls");
        } else if (devices && site->from->kind == DECL_SYSTEM) {
            diag_note("pass it the owner's instead: take 'Devices devices' in both");
        }
    }

    const decl **seen = arena_alloc(sizeof(decl *) * (size_t)(prog->decls.count + 1));
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *fn = prog->decls.items[i];
        if (fn->kind != DECL_FUNCTION || !fn->takes_action) continue;
        int seen_count = 0;
        const decl *through = NULL;
        if (!calls(fn, fn, seen, &seen_count, &through)) continue;
        diag_error(fn->at, "'" STR_FMT "' takes an Action, so it's copied into each call, and it can't call itself",
                   STR_ARG(fn->name));
        if (through) diag_note("it calls itself through '" STR_FMT "'", STR_ARG(through->name));
    }
}

// Resource values can pass through ordinary helpers or C without drawing.
// Propagate that local-only contract too, including helpers that discard the
// value and return a plain number to their caller.
static void check_render_resource_calls(const checker *c)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < c->calls.count; i++) {
            const call_site *site = &c->calls.items[i];
            if (!site->to->render_resources || site->from->render_resources) continue;
            site->from->render_resources = true;
            site->from->render_resources_at = site->call->at;
            changed = true;
        }
    }
    for (int i = 0; i < c->calls.count; i++) {
        const call_site *site = &c->calls.items[i];
        if (!site->to->render_resources || site->from->kind == DECL_FUNCTION || site->from->kind == DECL_METHOD
            || site->from->kind == DECL_SHADER || is_local_code(site->from)) continue;
        diag_error(site->call->at, "'" STR_FMT "' uses rendering resources, which belong to this machine: the match can't call it",
                   STR_ARG(site->to->name));
        diag_note_at(site->to->render_resources_at, "it uses a Texture or RenderTarget on line %d", site->to->render_resources_at.line);
    }
    for (int i = 0; i < c->prog->decls.count; i++) {
        decl *d = c->prog->decls.items[i];
        if (d->kind == DECL_FUNCTION && d->is_async && d->render_resources) note_side(d, TASK_LOCAL, d->render_resources_at);
    }
}

static void mark_io_callers(const checker *c)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < c->calls.count; i++) {
            const call_site *site = &c->calls.items[i];
            if (!site->to->uses_io || site->from->uses_io) continue;
            site->from->uses_io = true;
            site->from->io_at = site->call->at;
            site->from->calls_c = true;
            changed = true;
        }
    }
}

static void check_io_calls(const checker *c)
{
    mark_io_callers(c);
    for (int i = 0; i < c->calls.count; i++) {
        const call_site *site = &c->calls.items[i];
        if (!site->to->uses_io) continue;
        if (site->parallel || site->comparing) {
            diag_error(site->call->at, "'" STR_FMT "' accesses the filesystem, so it can't run in %s", STR_ARG(site->to->name),
                       site->parallel ? "a parallel loop" : "a sort comparison");
            continue;
        }
        if (site->from->kind == DECL_FUNCTION || site->from->kind == DECL_SHADER || is_local_code(site->from)) continue;
        diag_error(site->call->at, "'" STR_FMT "' accesses the filesystem, which only local code can use", STR_ARG(site->to->name));
        diag_note_at(site->to->io_at, "it accesses this machine's files on line %d", site->to->io_at.line);
    }
    for (int i = 0; i < c->prog->decls.count; i++) {
        decl *d = c->prog->decls.items[i];
        if (d->kind == DECL_FUNCTION && d->is_async && d->uses_io) note_side(d, TASK_LOCAL, d->io_at);
    }
}

// A function that calls one that reads Frame.dt reads it too: each is handed
// the local state, so only local code calls them, and an async one is a local
// task. Reading changes nothing, so a comparison and a parallel loop's steps
// can.
static void check_frame_time_calls(const checker *c)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < c->calls.count; i++) {
            const call_site *site = &c->calls.items[i];
            if (!site->to->frame_time || site->from->frame_time) continue;
            site->from->frame_time = true;
            site->from->frame_time_at = site->call->at;
            changed = true;
        }
    }
    for (int i = 0; i < c->calls.count; i++) {
        const call_site *site = &c->calls.items[i];
        // An async one is a local task: starting it from the match is reported as that (resolve_tasks)
        if (!site->to->frame_time || site->to->is_async) continue;
        if (site->from->kind == DECL_FUNCTION || site->from->kind == DECL_SHADER || is_local_code(site->from)) continue;
        diag_error(site->call->at, "'" STR_FMT "' reads Frame.dt, which only local code can read", STR_ARG(site->to->name));
        diag_note_at(site->to->frame_time_at, "it reads it on line %d, and %s", site->to->frame_time_at.line,
                     site->from->kind == DECL_METHOD ? "methods can't"
                     : site->from->kind == DECL_INPUT ? "the input's code runs once per tick"
                                                      : "the match's time is Time's: 'time.dt' is a tick's length");
    }
    for (int i = 0; i < c->prog->decls.count; i++) {
        decl *d = c->prog->decls.items[i];
        if (d->kind == DECL_FUNCTION && d->is_async && d->frame_time) note_side(d, TASK_LOCAL, d->frame_time_at);
    }
}

// A function that uses a component's or singleton's local fields through a parameter is
// given them with it, and so is one that passes the parameter on to it. So
// only code that has them calls it: views, local handlers of Forgotten and
// functions like it, with a parameter of their own, never a copy. And a view
// can't pass match state to a function or method that changes its match fields.
static void check_local_field_calls(const checker *c)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < c->calls.count; i++) {
            const call_site *site = &c->calls.items[i];
            if ((site->to->kind != DECL_FUNCTION && site->to->kind != DECL_METHOD)
                || (site->from->kind != DECL_FUNCTION && site->from->kind != DECL_METHOD) || site->from->is_async) continue;
            for (int k = 0; k < site->call->args.count && k < site->to->params.count; k++) {
                const param *q = &site->to->params.items[k];
                const expr *arg = site->call->args.items[k];
                if ((q->type.kind != TY_COMPONENT && q->type.kind != TY_SINGLETON) || !q->type.decl->local_part) continue;
                if (arg->kind != E_NAME || arg->bind != BIND_PARAM || !arg->param->function_param) continue;
                param *p = arg->param;
                if (p->type.kind != q->type.kind || p->type.decl != q->type.decl) continue;
                if (q->local_use && !p->local_use) {
                    p->local_use = true;
                    p->local_use_at = arg->at;
                    changed = true;
                }
                if (q->local_written && !p->local_written) {
                    p->local_written = true;
                    changed = true;
                }
                if (q->mode == PARAM_MUT && q->match_write && !p->match_write) {
                    p->match_write = true;
                    p->match_write_at = arg->at;
                    changed = true;
                }
            }
        }
    }
    for (int i = 0; i < c->prog->decls.count; i++) {
        const decl *fn = c->prog->decls.items[i];
        if (fn->kind != DECL_FUNCTION) continue;
        for (int k = 0; k < fn->params.count; k++) {
            const param *p = &fn->params.items[k];
            if (!p->local_use || !p->match_write) continue;
            diag_error(p->at, "'" STR_FMT "' changes the match's fields of its '" STR_FMT "' and uses its local fields, so "
                       "nothing could call it", STR_ARG(fn->name), STR_ARG(p->name));
            diag_note_at(p->match_write_at, "it changes the match on line %d, which views can't", p->match_write_at.line);
            diag_note_at(p->local_use_at, "and uses a local field on line %d, which the match can't", p->local_use_at.line);
        }
    }
    for (int i = 0; i < c->calls.count; i++) {
        const call_site *site = &c->calls.items[i];
        const decl *from = site->from;
        const decl *to = site->to;
        if (to->kind != DECL_FUNCTION && to->kind != DECL_METHOD) continue;
        const bool local_code = from->kind == DECL_SYSTEM && is_local_code(from);
        for (int k = 0; k < site->call->args.count && k < to->params.count; k++) {
            const param *q = &to->params.items[k];
            const expr *arg = site->call->args.items[k];
            if ((q->type.kind != TY_COMPONENT && q->type.kind != TY_SINGLETON) || !q->type.decl->local_part
                || arg->type.kind == TY_ERROR) continue;
            const bool own = arg->kind == E_NAME && arg->bind == BIND_PARAM && !arg->param->task_ref
                          && arg->param->type.kind == q->type.kind && arg->param->type.decl == q->type.decl;
            if (q->mode == PARAM_MUT && q->match_write && local_code && own) {
                diag_error(arg->at, "%s can't change the match, and '" STR_FMT "' changes its '" STR_FMT "''s match fields",
                           local_code_what(from), STR_ARG(to->name), STR_ARG(q->name));
                diag_note_at(q->match_write_at, "it does on line %d", q->match_write_at.line);
                diag_note("put what it wants of the match in the input, and change the match in a system");
                continue;
            }
            if (!q->local_use) continue;
            const decl *d = q->type.decl;
            if (from->kind == DECL_SYSTEM && !local_code) {
                diag_error(site->call->at, "'" STR_FMT "' uses the local fields of '" STR_FMT "', which are this machine's, "
                           "and the match can't see them", STR_ARG(to->name), STR_ARG(d->name));
                diag_note_at(q->local_use_at, "it does on line %d", q->local_use_at.line);
                diag_note("the match runs the same on every machine: call it from a view");
            } else if (from->is_async) {
                diag_error(site->call->at, "'" STR_FMT "' uses the local fields of '" STR_FMT "', and a task can't yet",
                           STR_ARG(to->name), STR_ARG(d->name));
                diag_note("call it from a view, and keep what the task needs in local state of its own");
            } else if (from->kind != DECL_SYSTEM && from->kind != DECL_FUNCTION) {
                diag_error(site->call->at, "'" STR_FMT "' uses the local fields of '" STR_FMT "', which only views, local "
                           "handlers and the functions they call have", STR_ARG(to->name), STR_ARG(d->name));
            } else if (!own) {
                diag_error(arg->at, "'" STR_FMT "' uses its '" STR_FMT "''s local fields, and this is a copy of the "
                           "%s's value, which has none", STR_ARG(to->name), STR_ARG(q->name),
                           q->type.kind == TY_SINGLETON ? "singleton" : "component");
                diag_note("pass the parameter of the view, or of the function, itself");
            } else if (from->kind == DECL_SYSTEM) { // The view's own: its local fields go with it
                if (!arg->param->local_use) arg->param->local_use_at = arg->at;
                arg->param->local_use = true;
                arg->param->local_written |= q->local_written;
            }
        }
    }
}

// A component's method that uses `this` is given the entity its component
// belongs to, and so is one that calls it on the same value. The entity is only
// known for the components code runs for, its parameters: a copy belongs to no
// entity.
static void check_this_calls(const checker *c)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < c->calls.count; i++) {
            const call_site *site = &c->calls.items[i];
            if (site->call->kind != E_CALL || site->from->kind != DECL_METHOD || !site->to->uses_this) continue;
            if (site->from->uses_this) continue;
            site->from->uses_this = true;
            site->from->this_at = site->call->at;
            changed = true;
        }
    }
    for (int i = 0; i < c->calls.count; i++) {
        const call_site *site = &c->calls.items[i];
        const expr *call = site->call;
        if (!site->to->uses_this || call->kind != E_METHOD) continue;
        const expr *object = call->object;
        if (object->kind == E_NAME && object->bind == BIND_PARAM && !object->param->function_param) continue;
        const decl *m = site->to;
        char *name = str_to_cstr(m->owner->name);
        name[0] = (char)tolower((unsigned char)name[0]);
        diag_error(call->at, "'" STR_FMT "' uses 'this', the entity its " STR_FMT " belongs to, and this one belongs to none",
                   STR_ARG(m->name), STR_ARG(m->owner->name));
        diag_note("a component belongs to an entity where code takes it as a parameter, like 'system Name(" STR_FMT
                  " %s)'; a copy doesn't",
                  STR_ARG(m->owner->name), name);
        diag_note_at(m->this_at, "'" STR_FMT "' uses 'this' on line %d", STR_ARG(m->name), m->this_at.line);
    }
}

// Match code that takes Devices reads the devices of the player who owns the
// entity: the input sends what it reads of them, through the functions and
// methods it calls too. A game without an input declaration gets one that
// only sends the devices.
static void collect_device_uses(checker *c)
{
    program *prog = c->prog;
    VEC(decl *) reached = {0};
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (d->kind != DECL_SYSTEM || d->is_view || d->is_local) continue;
        vec_push(reached, d);
        for (int k = 0; k < d->params.count; k++) {
            const type t = d->params.items[k].type;
            if (t.kind == TY_RECORD && t.decl == prog->devices) prog->match_devices = true;
        }
    }
    if (!prog->match_devices) return;
    for (int r = 0; r < reached.count; r++) {
        const decl *from = reached.items[r];
        for (int i = 0; i < c->calls.count; i++) {
            if (c->calls.items[i].from != from) continue;
            decl *to = (decl *)c->calls.items[i].to;
            bool known = false;
            for (int k = 0; k < reached.count && !known; k++) known = reached.items[k] == to;
            if (!known) vec_push(reached, to);
        }
    }
    for (int r = 0; r < reached.count; r++) {
        const decl *d = reached.items[r];
        for (int w = 0; w < DEVICE_WORDS; w++) prog->device_uses[w] |= d->device_uses[w];
        if (d->position_at.line) {
            diag_error(d->position_at, "the match can't read %s: it's in this machine's window", d->position_what);
            diag_note("work out what the match needs from it in the input's Sample, like an aim direction, and "
                      "read that from the input");
        }
    }

    decl *input = prog->input;
    if (!input) {
        input = NEW(decl);
        input->kind = DECL_INPUT;
        input->name = str_from("tide_devices_input");
        input->qualified = input->name;
        input->builtin = true;
        prog->input = input;
    }
    field devices = {0};
    devices.name = str_from("tide_dev");
    devices.type = (type){TY_RECORD, prog->devices};
    devices.hidden = true;
    vec_push(input->fields, devices);
}

static bool placed_all(const decl *d, const bool *placed, const decl *const *list, const int n)
{
    for (int i = 0; i < d->after.count; i++) {
        const decl *earlier = d->after.items[i];
        const int index = earlier->index;
        // Before scheduling, indexes are their original places in this list.
        // Whole-phase barriers can have many predecessors: find each in O(1).
        if (index >= 0 && index < n && list[index] == earlier) {
            if (!placed[index]) return false;
            continue;
        }
        for (int k = 0; k < n; k++) {
            if (list[k] == earlier && !placed[k]) return false;
        }
    }
    return true;
}

// Orders systems (or views): each after the ones it must follow, and otherwise
// in the order they're written, file by file with files sorted by path. The
// order is part of the program, the same on every platform.
static void schedule(decl **list, const int n)
{
    if (n == 0) return;
    bool *placed = arena_alloc(sizeof(bool) * (size_t)n);
    decl **order = arena_alloc(sizeof(decl *) * (size_t)n);
    int count = 0;
    while (count < n) {
        int next = -1;
        for (int i = 0; i < n && next < 0; i++) {
            if (!placed[i] && placed_all(list[i], placed, (const decl *const *)list, n)) next = i;
        }
        if (next < 0) break; // A cycle
        placed[next] = true;
        order[count++] = list[next];
    }

    if (count < n) {
        // Follow "must run after" links among the unplaced until one repeats.
        const decl *path[64];
        int length = 0;
        const decl *d = NULL;
        for (int i = 0; i < n && !d; i++) {
            if (!placed[i]) d = list[i];
        }
        for (;;) {
            int seen = -1;
            for (int i = 0; i < length; i++) {
                if (path[i] == d) seen = i;
            }
            if (seen >= 0 || length == 64) {
                // Each system in the path runs after the next one.
                sb chain = {0};
                for (int i = seen >= 0 ? seen : 0; i < length; i++) {
                    sb_printf(&chain, STR_FMT " runs after ", STR_ARG(path[i]->qualified));
                }
                sb_printf(&chain, STR_FMT, STR_ARG(d->qualified));
                diag_error(d->at, "these %ss must each run after the next, which can't happen: %s", system_what(d),
                           chain.data);
                bool phases = has_phase_order(d);
                for (int i = seen >= 0 ? seen : 0; i < length; i++) phases |= has_phase_order(path[i]);
                diag_note(phases ? "remove one of the Stage, Before or After attributes that form the loop"
                                 : "remove one of the Before or After attributes that form the loop");
                break;
            }
            path[length++] = d;
            const decl *next = NULL;
            for (int i = 0; i < d->after.count && !next; i++) {
                for (int k = 0; k < n; k++) {
                    if (list[k] == d->after.items[i] && !placed[k]) next = list[k];
                }
            }
            d = next;
        }
        return;
    }
    for (int i = 0; i < n; i++) {
        list[i] = order[i];
        list[i]->index = i;
    }
}

// Whether a value of type `t` is a Random or has one in it, in its own fields
// or its structs': a generator the engine seeds where it's the match's or an
// entity's (see gen_seeding in codegen.c). One in a list is the game's to seed.
static bool holds_random(const program *prog, const type t)
{
    if (t.kind != TY_STRUCT) return false;
    if (t.decl == prog->random) return true;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (holds_random(prog, t.decl->fields.items[i].type)) return true;
    }
    return false;
}

// Time.seed is in the world only when something uses it: code that reads it,
// or a Random in the match's components or singletons, which the engine seeds
// from it. Every other game's world, hash and header are what they'd be
// without it. It's Time's last field, so the others stay where they are.
static void drop_unused_seed(program *prog)
{
    for (int i = 0; i < prog->decls.count && !prog->uses_seed; i++) {
        const decl *d = prog->decls.items[i];
        if ((d->kind != DECL_COMPONENT && d->kind != DECL_SINGLETON) || d->is_local) continue;
        for (int k = 0; k < d->fields.count; k++) prog->uses_seed |= holds_random(prog, d->fields.items[k].type);
    }
    if (!prog->uses_seed) prog->time->fields.count--;
}

// The players who see a scene are bits of its hidden tide_players, 32 a word:
// a game that takes more players than a word has gets the words after it,
// tide_players1 and on, once its maxPlayers is known.
static void add_scene_player_words(program *prog)
{
    const uint32_t players = prog->max_players ? prog->max_players : TIDE_PLAYERS_DEFAULT;
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (d->kind != DECL_COMPONENT || !d->is_scene) continue;
        for (uint32_t word = 1; word * 32u < players; word++) {
            char *name = arena_alloc(24);
            snprintf(name, 24, "tide_players%u", (unsigned)word);
            field more = {0};
            more.name = str_from(name);
            more.type_name = str_from("int");
            more.type = T_INT_;
            more.hidden = true;
            vec_push(d->fields, more);
        }
    }
}

bool check(program *prog)
{
    checker c = {0};
    c.prog = prog;

    add_builtins(prog);
    builtins_use(prog->anchor, prog->shape);
    add_device_records(prog);
    collect_decls(prog);
    check_units(prog);
    check_packages(prog);

    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        c.unit = d->unit;
        if (d->kind != DECL_SYSTEM) resolve_field_types(&c, d);
    }
    for (int i = 0; i < prog->local_parts.count; i++) {
        c.unit = prog->local_parts.items[i]->unit;
        resolve_field_types(&c, prog->local_parts.items[i]);
    }
    for (int i = 0; i < prog->decls.count; i++) {
        if (prog->decls.items[i]->kind == DECL_CONST) check_constant(&c, prog->decls.items[i]);
    }
    check_settings(&c);
    add_scene_player_words(prog);
    diag_phase("declarations");
    for (int i = 0; i < prog->decls.count; i++) {
        // Random is among the program's structs once something names it (below), or a struct holds one
        if (prog->decls.items[i]->kind == DECL_STRUCT && prog->decls.items[i] != prog->random) order_struct(prog, prog->decls.items[i]);
        if (prog->decls.items[i]->kind == DECL_ENUM) {
            c.unit = prog->decls.items[i]->unit;
            check_enum(&c, prog->decls.items[i]);
        }
    }
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        c.unit = d->unit;
        if (d->kind != DECL_SYSTEM) check_fields(&c, d);
    }
    for (int i = 0; i < prog->local_parts.count; i++) {
        c.unit = prog->local_parts.items[i]->unit;
        check_fields(&c, prog->local_parts.items[i]);
        check_local_part(&c, prog->local_parts.items[i]);
    }
    // Every signature first, so bodies can call any method or function.
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        c.unit = d->unit;
        if (d->kind == DECL_FUNCTION) check_function_decl(&c, d);
        else if (d->kind == DECL_SHADER) check_shader_decl(&c, d);
        else if (d->kind != DECL_SYSTEM) check_method_decls(&c, d);
    }
    check_correction_interpolates(prog);
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (d->kind == DECL_FUNCTION && !d->is_extern) check_method_body(&c, d);
        if (d->kind == DECL_SHADER) {
            const int errors = diag_error_count();
            check_method_body(&c, d);
            if (diag_error_count() == errors) warn_unused_params(&c, d);
        }
        for (int k = 0; k < d->methods.count; k++) check_method_body(&c, d->methods.items[k]);
    }
    if (prog->input) {
        c.unit = prog->input->unit;
        check_sample(&c, prog->input);
        check_sanitize(&c, prog->input);
    }

    // Every helper body is known before systems and views are checked. Their
    // filesystem effects must already exclude automatic foreach threading.
    mark_io_callers(&c);

    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (d->kind != DECL_SYSTEM) continue;
        c.unit = d->unit;
        check_params(&c, d);
        c.system = d;
        const int errors = diag_error_count();
        check_stmt(&c, d->body);
        if (diag_error_count() == errors) warn_unused_params(&c, d); // Errors hide uses
    }
    diag_phase("checking code");
    drop_unused_seed(prog);
    if (prog->random->named) order_struct(prog, prog->random);
    check_sends(&c);
    check_render_resource_calls(&c);
    check_io_calls(&c);
    check_frame_time_calls(&c);
    resolve_tasks(&c);
    check_this_calls(&c);
    check_drawing_calls(&c);
    check_local_field_calls(&c);
    if (diag_error_count() == 0) plan_shaders(prog);
    mark_c_callers(&c);
    warn_unused_locals(&c); // Once it's known which calls draw and call C
    collect_device_uses(&c);
    // A singleton something snaps counts its snaps: views blend it between ticks with the same count.
    for (int i = 0; i < prog->decls.count; i++) {
        decl *d = prog->decls.items[i];
        if (d->kind != DECL_SINGLETON || !d->snapped) continue;
        field snaps = {0};
        snaps.name = str_from("tide_snaps");
        snaps.type = (type){TY_INT, NULL};
        snaps.hidden = true;
        vec_push(d->fields, snaps);
    }

    check_attributes(&c);
    if (diag_error_count() == 0) {
        order_phases(prog->systems.items, prog->systems.count);
        order_phases(prog->views.items, prog->views.count);
        order_phases(prog->handlers.items, prog->handlers.count);
        schedule(prog->systems.items, prog->systems.count);
        schedule(prog->views.items, prog->views.count);
        schedule(prog->handlers.items, prog->handlers.count);
        for (int i = 0; i < prog->handlers.count; i++) {
            decl *h = prog->handlers.items[i];
            if (h->is_command && h->event->handlers.count) {
                diag_error(h->at, "command '" STR_FMT "' has more than one handler", STR_ARG(h->event->name));
                diag_note("validate and handle it in one handler; that handler can send ordinary events");
            }
            // The tick runs an event's handlers; its listeners hear it at the next frame
            if (h->listens) {
                if (h->event->listeners.count == 64) {
                    diag_error(h->at, "'" STR_FMT "' has more than 64 local handlers", STR_ARG(h->event->name));
                    diag_note("each is a bit in what a tick records of the event: do more in one handler");
                    continue;
                }
                vec_push(h->event->listeners, h);
                continue;
            }
            vec_push(h->event->handlers, h);
        }
    }

    if (!prog->main && diag_error_count() == 0 && !only_packages(prog)) {
        // Something named like it: a scene spelled main, or anything else named Main.
        const decl *near = NULL;
        for (int i = 0; i < prog->decls.count; i++) {
            const decl *d = prog->decls.items[i];
            const str name = d->name;
            const bool main = name.len == 4 && (name.ptr[0] == 'm' || name.ptr[0] == 'M') && memcmp(name.ptr + 1, "ain", 3) == 0;
            if (main && (!near || (d->kind == DECL_COMPONENT && d->is_scene))) near = d;
        }
        diag_error(near ? near->at : (loc){1, 1, 0}, "the program has no entry point");
        if (near && near->kind == DECL_COMPONENT && near->is_scene) {
            diag_note("the entry point is spelled 'Main', with a capital M");
        } else if (near && str_eq_c(near->name, "Main")) {
            diag_note("it's the scene named Main, 'scene Main { }'; a %s named Main is an ordinary one",
                      near->kind == DECL_SYSTEM ? "system" : "declaration");
        } else if (near) {
            diag_note("it's the scene named 'Main', with a capital M: 'scene Main { }'");
        } else {
            diag_note("add 'scene Main { }', the scene the program starts in, and create what it starts with in "
                      "'event(Spawned) Setup(with Main) { ... }'");
        }
    }

    if (diag_error_count() > 0) return false;

    diag_phase("checking the rest");
    derive_archetypes(&c);
    diag_phase("deriving archetypes");
    if (diag_error_count() > 0) return false;

    warn_unmatched(prog, (const decl *const *)prog->systems.items, prog->systems.count);
    warn_unmatched(prog, (const decl *const *)prog->views.items, prog->views.count);
    warn_unmatched(prog, (const decl *const *)prog->handlers.items, prog->handlers.count);
    warn_unsent(&c);
    diag_phase("warnings");
    analyze_parallelism(prog);
    diag_phase("scheduling");
    return true;
}
