// ---------------------------------------------------------------------------
// Gathering systems: a per-entity system that changes a singleton, its
// entities on threads
//
// A system that runs for each entity and takes a singleton `mut` sees, in
// each row, what the rows before it wrote there: so its entities went in
// order on one thread. Most such systems never look: they count, sum, keep a
// least or a most, add to a list, or hand the singleton to a function that
// does. Each statement that touches the singleton then only changes it, from
// values of the row, and nothing else in the row reads it back.
//
// For those the rows go to threads in runs of the entities' order, as a
// system that queues changes does (system_splits_in_order). A run doesn't
// run those statements: it logs each one it comes to, its number and the
// row's values it takes (tide/jobs.h, tide_gather). Once every run is done,
// one thread runs the logs in the runs' order (tide_replay_<System>, from
// tide_done_<System>): the same statements with the same values in the order
// one thread runs them, so the singleton, its lists and its heap come out bit
// for bit as on one thread, float sums included. Nothing is regrouped.
//
// What makes a system one (system_gather_plan): every statement that names a
// singleton it takes `mut` is an assignment into it, or a call that changes
// it, and can be cut in two:
// - what it takes from the row, the largest parts of it that don't name the
//   singleton: plain values (fields, locals, constants, operators, Math and
//   constructors of those; no text, list, call of the game's or anything that
//   changes something), worked out in the row and logged. Text written out
//   or interpolated from such values is made again from them, from the log;
// - the rest, which names the singleton: its fields, indexes, operators, list
//   and map methods, and functions of the game's that don't queue, spawn,
//   roll, start tasks or reach C. That part runs from the log.
// A read-only singleton, or a field of one, may stay in the second part as it
// is: nothing changes it while the system runs. Anywhere else in the code a
// `mut` singleton may only be read where the system's code never writes it
// (its bytes by field, as the schedule has them): those reads are the same
// in the row as in order.
//
// With one piece (a tick on one thread, or too little work to share) nothing
// is logged: tide_task_gather is NULL and each statement runs where it is.
//
// A system whose rows do little else than add stays in order on one thread
// (gather_plan.little): the adding is its work, and that's one thread's
// either way.

typedef struct gather_capture {
    const expr *value; // Worked out in the row
    expr *named;       // What stands for it in the statement run from the log: a name, `tide_gc<n>`
} gather_capture;

typedef struct gather_stmt {
    const stmt *from; // The statement, in the system's code
    stmt *replay;     // ...and with its captures named
    VEC(gather_capture) captures;
} gather_stmt;

typedef struct gather_plan {
    const decl *sys;
    bool ok;
    bool little; // It could, but what's left to its rows is less work than what they add: it stays in order
    VEC(gather_stmt) stmts;
} gather_plan;

static VEC(gather_plan) gather_plans;

// The plans are the last compilation's, in its arena, which is gone
static void gather_forget(void)
{
    memset(&gather_plans, 0, sizeof gather_plans);
}

typedef struct gather_walk {
    const program *prog;
    const decl *sys;
    data_bytes written[64]; // By parameter: what the system's code writes of each singleton it gathers
    gather_stmt *into;      // The statement being cut
    bool failed;
} gather_walk;

static bool gather_param(const decl *sys, const param *p)
{
    return p && p >= sys->params.items && p < sys->params.items + sys->params.count && p->type.kind == TY_SINGLETON
        && p->mode == PARAM_MUT;
}

static bool gather_refs_stmt(const stmt *s, const decl *sys);

// Whether anything in `e` names a singleton the system takes `mut`
static bool gather_refs(const expr *e, const decl *sys)
{
    if (!e) return false;
    if (e->kind == E_NAME && e->bind == BIND_PARAM && gather_param(sys, e->param)) return true;
    if (gather_refs(e->object, sys) || gather_refs(e->lhs, sys) || gather_refs(e->rhs, sys) || gather_refs(e->cond, sys)) return true;
    for (int i = 0; i < e->args.count; i++) if (gather_refs(e->args.items[i], sys)) return true;
    for (int i = 0; i < e->written.count; i++) if (gather_refs(e->written.items[i], sys)) return true;
    for (int i = 0; i < e->inits.count; i++) if (gather_refs(e->inits.items[i].value, sys)) return true;
    return gather_refs(e->lambda_body, sys) || gather_refs(e->values, sys) || gather_refs(e->mesh_lod, sys)
        || gather_refs(e->skin_mesh, sys) || gather_refs(e->skin_pose, sys) || gather_refs(e->callable_receiver, sys)
        || gather_refs(e->task_source, sys) || gather_refs(e->binding_value, sys) || gather_refs_stmt(e->block, sys)
        || gather_refs_stmt(e->step, sys) || gather_refs_stmt(e->compare[0], sys) || gather_refs_stmt(e->compare[1], sys)
        || gather_refs_stmt(e->out_local, sys) || gather_refs_stmt(e->binding, sys);
}

static bool gather_refs_stmt(const stmt *s, const decl *sys)
{
    if (!s) return false;
    for (int i = 0; i < s->stmts.count; i++) if (gather_refs_stmt(s->stmts.items[i], sys)) return true;
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++) if (gather_refs(s->cases.items[i].labels.items[k], sys)) return true;
        for (int k = 0; k < s->cases.items[i].body.count; k++) if (gather_refs_stmt(s->cases.items[i].body.items[k], sys)) return true;
    }
    return gather_refs(s->cond, sys) || gather_refs(s->value, sys) || gather_refs(s->target, sys) || gather_refs(s->property_set, sys)
        || gather_refs(s->by, sys) || gather_refs(s->offset, sys) || gather_refs_stmt(s->init, sys) || gather_refs_stmt(s->step, sys)
        || gather_refs_stmt(s->then_stmt, sys) || gather_refs_stmt(s->else_stmt, sys);
}

// A value that's the same wherever it's worked out: a number, a constant, an enum's member
static bool gather_constant(const expr *e)
{
    if (!e || e->hoisted || e->lambda) return false;
    switch (e->kind) {
    case E_INT: case E_FLOAT: case E_BOOL: return true;
    case E_NAME: return e->bind == BIND_CONST && guard_value(e->type);
    case E_MEMBER: return e->enum_member != NULL || (e->constant && guard_value(e->type));
    case E_UNARY: return !e->method && e->op == T_MINUS && gather_constant(e->lhs);
    default: return false;
    }
}

// A read-only singleton the system takes, or a field of one: nothing changes it while the system runs
static bool gather_settled(const decl *sys, const expr *e)
{
    if (!e || e->hoisted) return false;
    if (e->kind == E_NAME) {
        return e->bind == BIND_PARAM && e->param >= sys->params.items && e->param < sys->params.items + sys->params.count
            && e->param->type.kind == TY_SINGLETON && e->param->mode == PARAM_READ && !e->param->type.decl->is_local;
    }
    return e->kind == E_MEMBER && e->field && !e->property && !e->local_field && e->swizzle_len <= 0 && gather_settled(sys, e->object);
}

// What a built-in is called on, `Math` in Math.Clamp(...): a name, no value
static bool gather_static(const expr *e)
{
    return e && e->kind == E_NAME && !e->hoisted && (e->bind == BIND_TYPE || e->bind == BIND_NAMESPACE || e->bind == BIND_NONE);
}

// Whether `e` is a plain value of the row: fields of what the system takes, its locals, constants, operators,
// Math and constructors of those. Nothing that calls the game's code, allocates, rolls or changes anything.
static bool gather_plain(const decl *sys, const expr *e)
{
    if (!e || e->hoisted || e->lambda || e->block || e->is_out) return false;
    switch (e->kind) {
    case E_INT: case E_FLOAT: case E_BOOL: return true;
    case E_DEFAULT: return guard_value(e->type);
    case E_THIS: return e->type.kind == TY_ENTITY;
    case E_NAME:
        if (e->bind == BIND_CONST) return guard_value(e->type);
        if (e->bind == BIND_LOCAL) return e->local && !e->local->out_decl && !e->local->is_shared;
        if (e->bind != BIND_PARAM || !e->param || gather_param(sys, e->param)) return false;
        return (e->param->type.kind == TY_COMPONENT || e->param->type.kind == TY_SINGLETON) && !e->param->type.decl->is_local;
    case E_MEMBER:
        if (e->enum_member) return true;
        if (e->constant) return guard_value(e->type);
        if (e->property || e->local_field || e->edge != EDGE_NONE || e->c_constant || e->typed_text || e->raw_sampled
            || e->target_colors || e->asset_ref_value) return false;
        if (!e->field && e->swizzle_len <= 0) return false;
        return gather_plain(sys, e->object);
    case E_INDEX: // An element of a fixed array
        return e->object && e->object->type.kind == TY_ARRAY && gather_plain(sys, e->object) && gather_plain(sys, e->lhs)
            && guard_value(e->lhs->type);
    case E_UNARY: return !e->method && gather_plain(sys, e->lhs) && guard_value(e->type);
    case E_BINARY:
        return !e->method && gather_plain(sys, e->lhs) && gather_plain(sys, e->rhs) && guard_value(e->lhs->type)
            && guard_value(e->rhs->type) && guard_value(e->type);
    case E_CONDITIONAL: return gather_plain(sys, e->cond) && gather_plain(sys, e->lhs) && gather_plain(sys, e->rhs);
    case E_LITERAL:
        if (e->prefab || !guard_value(e->type)) return false;
        for (int i = 0; i < e->inits.count; i++) {
            if (!gather_plain(sys, e->inits.items[i].value) || !guard_value(e->inits.items[i].value->type)) return false;
        }
        return true;
    case E_METHOD:
        if (e->call != CALL_BUILTIN || !gather_static(e->object)) return false;
        // fallthrough
    case E_CALL:
        if ((e->call != CALL_CONSTRUCT && e->call != CALL_BUILTIN) || e->arg_order.count || e->arg_mut || e->arg_out) return false;
        for (int i = 0; i < e->args.count; i++) {
            if (!gather_plain(sys, e->args.items[i]) || !guard_value(e->args.items[i]->type)) return false;
        }
        return guard_value(e->type);
    default: return false;
    }
}

// A function or method of the game's that a statement run from the log may call: its code is all it does, and
// what it does is to what it's handed
static bool gather_callee(const decl *m)
{
    return m && m->body && !m->is_extern && !m->reaches_extern && !m->is_async && !m->starts_tasks && !m->is_virtual && !m->is_override
        && !m->replaced_by && !m->is_worker && !m->acts[0] && !m->acts[1] && !m->spawns && !m->rolls && !m->draws
        && m->world_uses.count == 0;
}

static void gather_cut(gather_walk *w, expr **slot);

// A part of the statement that names the singleton: kept, with what it takes from the row cut out of it
static expr *gather_keep(gather_walk *w, const expr *e)
{
    if (e->hoisted || e->lambda || e->lambda_body || e->block || e->step || e->compare[0] || e->compare[1] || e->out_local || e->binding
        || e->values || e->mesh_lod || e->skin_mesh || e->skin_pose || e->callable_receiver || e->task_source || e->binding_value
        || e->prefab || e->is_out || e->arg_order.count || e->property || e->local_field || e->gpu) {
        w->failed = true;
        return (expr *)(uintptr_t)e;
    }
    expr *n = NEW(expr);
    *n = *e;
    switch (e->kind) {
    case E_NAME: return n;
    case E_MEMBER:
        if (!e->field && e->swizzle_len <= 0 && !(e->object && e->object->type.kind == TY_LIST && str_eq_c(e->member, "count"))) w->failed = true;
        gather_cut(w, &n->object);
        return n;
    case E_INDEX:
        gather_cut(w, &n->object);
        gather_cut(w, &n->lhs);
        return n;
    case E_UNARY: case E_BINARY: case E_CONDITIONAL:
        if (e->method && !gather_callee(e->method)) w->failed = true;
        for (int i = 0; e->method && i < e->method->params.count; i++) w->failed |= e->method->params.items[i].mode == PARAM_MUT;
        if (n->cond) gather_cut(w, &n->cond);
        if (n->lhs) gather_cut(w, &n->lhs);
        if (n->rhs) gather_cut(w, &n->rhs);
        return n;
    case E_LITERAL:
        if (e->inits.count) {
            n->inits.items = arena_alloc(sizeof *n->inits.items * (size_t)e->inits.count);
            memcpy(n->inits.items, e->inits.items, sizeof *n->inits.items * (size_t)e->inits.count);
        }
        for (int i = 0; i < n->inits.count; i++) gather_cut(w, &n->inits.items[i].value);
        return n;
    case E_CALL: case E_METHOD: {
        const bool routine = e->call == CALL_FUNCTION || e->call == CALL_METHOD;
        if (!routine && e->call != CALL_CONSTRUCT && e->call != CALL_BUILTIN && e->call != CALL_LIST && e->call != CALL_MAP) w->failed = true;
        if (routine && !gather_callee(e->method)) w->failed = true;
        if (e->arg_mut || e->arg_out) w->failed = true;
        // What a call changes is the singleton's: a list or a map of its own, a `mut` argument that's a place in it
        if ((e->call == CALL_LIST || e->call == CALL_MAP || (routine && e->method && e->method->is_mut_method)) && e->kind == E_METHOD
            && !gather_refs(e->object, w->sys)) w->failed = true;
        if (e->args.count) {
            n->args.items = arena_alloc(sizeof *n->args.items * (size_t)e->args.count);
            memcpy(n->args.items, e->args.items, sizeof *n->args.items * (size_t)e->args.count);
        }
        for (int i = 0; i < n->args.count && !w->failed; i++) {
            const bool mut = routine && e->method && i < e->method->params.count && e->method->params.items[i].mode == PARAM_MUT;
            if (mut && !gather_refs(e->args.items[i], w->sys)) w->failed = true; // It would change something of the row's
            gather_cut(w, &n->args.items[i]);
        }
        if (n->object && !(e->call == CALL_BUILTIN && gather_static(e->object))) gather_cut(w, &n->object);
        return n;
    }
    default:
        w->failed = true;
        return n;
    }
}

static void gather_cut(gather_walk *w, expr **slot)
{
    const expr *e = *slot;
    if (!e || w->failed) return;
    if (gather_refs(e, w->sys)) {
        *slot = gather_keep(w, e);
        return;
    }
    if (gather_constant(e) || gather_settled(w->sys, e)) return; // The same from the log as in the row
    // Text isn't logged: it's made again from the log, of the row's plain values, where one thread's row made it
    if (e->kind == E_STRING && !e->hoisted) return;
    if (e->kind == E_INTERP && !e->hoisted && !e->lambda && !e->block) {
        expr *n = NEW(expr);
        *n = *e;
        if (e->args.count) {
            n->args.items = arena_alloc(sizeof *n->args.items * (size_t)e->args.count);
            memcpy(n->args.items, e->args.items, sizeof *n->args.items * (size_t)e->args.count);
        }
        for (int i = 0; i < n->args.count; i++) gather_cut(w, &n->args.items[i]);
        *slot = n;
        return;
    }
    if (!gather_plain(w->sys, e) || !guard_value(e->type) || e->type.kind == TY_VOID) {
        w->failed = true;
        return;
    }
    expr *named = NEW(expr);
    *named = (expr){.kind = E_NAME, .at = e->at, .type = e->type};
    char *name = arena_alloc(24);
    snprintf(name, 24, "tide_gc%d", w->into->captures.count);
    named->hoisted = name;
    const gather_capture c = {e, named};
    vec_push(w->into->captures, c);
    *slot = named;
}

// The parameter a place is in, `crowd.sum[i].x` in `crowd`: NULL where it's no place of a parameter
static const param *gather_root(const expr *e)
{
    while (e && (e->kind == E_MEMBER || e->kind == E_INDEX)) e = e->object;
    return e && e->kind == E_NAME && e->bind == BIND_PARAM ? e->param : NULL;
}

// Code that runs in the row: it may name a singleton the system gathers only to read what the system's code
// never writes of it
static void gather_free(gather_walk *w, const expr *e)
{
    if (!e || w->failed || !gather_refs(e, w->sys)) return;
    if (e->kind == E_NAME || e->kind == E_MEMBER || e->kind == E_INDEX) {
        const param *root = gather_root(e);
        if (root && gather_param(w->sys, root)) {
            const data_bytes *written = &w->written[root - w->sys->params.items];
            const writes_place p = writes_place_of(e, root);
            if (!p.rooted || written->size <= 0 || p.off < 0 || p.size <= 0 || p.off + p.size > written->size) {
                w->failed = true;
                return;
            }
            // By whole fields, as the schedule has what's written
            const decl *d = root->type.decl;
            for (int i = 0; i < d->fields.count && !w->failed; i++) {
                int off = 0, size = 0;
                if (!field_bytes(d, &d->fields.items[i], &off, &size)) w->failed = true;
                else if (off < p.off + p.size && p.off < off + size) {
                    for (int k = off; k < off + size; k++) w->failed |= written->writes[k] != 0;
                }
            }
            for (const expr *part = e; part && (part->kind == E_MEMBER || part->kind == E_INDEX); part = part->object) {
                if (part->property || part->local_field) w->failed = true;
                if (part->kind == E_INDEX) gather_free(w, part->lhs);
            }
            return;
        }
    }
    if (e->lambda || e->lambda_body || e->block || e->step || e->compare[0] || e->compare[1] || e->out_local || e->binding || e->values
        || e->mesh_lod || e->skin_mesh || e->skin_pose || e->callable_receiver || e->task_source || e->binding_value) {
        w->failed = true; // Names it somewhere this doesn't follow
        return;
    }
    gather_free(w, e->object);
    gather_free(w, e->lhs);
    gather_free(w, e->rhs);
    gather_free(w, e->cond);
    for (int i = 0; i < e->args.count; i++) gather_free(w, e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) gather_free(w, e->inits.items[i].value);
}

// An assignment into a singleton the system gathers, cut in two
static void gather_assign(gather_walk *w, gather_plan *plan, const stmt *from, const stmt *assign)
{
    const param *root = gather_root(assign->target);
    if (!root || !gather_param(w->sys, root) || assign->property_set || assign->kind != S_ASSIGN) {
        w->failed = true; // It reads the singleton into something of the row's
        return;
    }
    gather_stmt made = {from, NEW(stmt), {0}};
    *made.replay = *assign;
    w->into = &made;
    gather_cut(w, &made.replay->target);
    gather_cut(w, &made.replay->value);
    w->into = NULL;
    if (!w->failed) vec_push(plan->stmts, made);
}

static void gather_statement(gather_walk *w, gather_plan *plan, const stmt *s)
{
    if (!s || w->failed) return;
    switch (s->kind) {
    case S_ASSIGN: {
        const param *root = gather_root(s->target);
        if (root && gather_param(w->sys, root)) {
            gather_assign(w, plan, s, s);
            return;
        }
        break; // Into something of the row's: what it reads of the singleton is checked below
    }
    case S_EXPR: {
        const expr *e = s->value;
        if (!gather_refs(e, w->sys)) return;
        if (e->kind == E_STEP && e->step) { // crowd.count++ as a statement: its assignment
            gather_assign(w, plan, s, e->step);
            return;
        }
        // A call that changes the singleton: a method of a place in it, or a function handed one `mut`
        bool changes = e->kind == E_METHOD && gather_refs(e->object, w->sys);
        const bool routine = e->call == CALL_FUNCTION || e->call == CALL_METHOD;
        for (int i = 0; routine && e->method && i < e->args.count && i < e->method->params.count; i++) {
            changes |= e->method->params.items[i].mode == PARAM_MUT && gather_refs(e->args.items[i], w->sys);
        }
        if ((e->kind != E_CALL && e->kind != E_METHOD) || !changes) break; // It only reads it: checked below
        gather_stmt made = {s, NEW(stmt), {0}};
        *made.replay = *s;
        w->into = &made;
        gather_cut(w, &made.replay->value);
        w->into = NULL;
        if (!w->failed) vec_push(plan->stmts, made);
        return;
    }
    case S_PARALLEL: // Its steps are on other threads again
        if (gather_refs_stmt(s, w->sys)) w->failed = true;
        return;
    case S_FOR: // What starts and steps a loop is written into the loop's own line, not as a statement
        if (gather_refs_stmt(s->init, w->sys) || gather_refs_stmt(s->step, w->sys)) w->failed = true;
        break;
    default: break;
    }
    gather_free(w, s->cond);
    gather_free(w, s->value);
    gather_free(w, s->target);
    gather_free(w, s->property_set);
    gather_free(w, s->by);
    gather_free(w, s->offset);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++) gather_free(w, s->cases.items[i].labels.items[k]);
        for (int k = 0; k < s->cases.items[i].body.count; k++) gather_statement(w, plan, s->cases.items[i].body.items[k]);
    }
    for (int i = 0; i < s->stmts.count; i++) gather_statement(w, plan, s->stmts.items[i]);
    gather_statement(w, plan, s->init);
    gather_statement(w, plan, s->step);
    gather_statement(w, plan, s->then_stmt);
    gather_statement(w, plan, s->else_stmt);
}

// A system's plan: `ok` when it gathers. Worked out once for a compilation (schedule_bytes_reset forgets it).
static const gather_plan *system_gather_plan(const program *prog, const decl *sys)
{
    for (int i = 0; i < gather_plans.count; i++) {
        if (gather_plans.items[i].sys == sys) return &gather_plans.items[i];
    }
    gather_plan plan = {sys, false, false, {0}};
    gather_walk w = {.prog = prog, .sys = sys};
    int gathered = 0;
    // (C a system calls ran on one thread, in its entities' order, for as long as the singleton kept it there: a
    // physics world's joints made as its rows come. It stays so.)
    bool plain = sys->per_entity && sys->body && !sys->is_view && !sys->is_handler && !sys->extern_system && !sys->reaches_extern && !sys->starts_tasks
        && !sys->is_async && !sys->is_virtual && !sys->is_override && !sys->replaced_by && !sys->is_local && !sys->preparation_cpu
        && !sys->preparation_owner && !prog->shard_state && !prog->rows_whole
        && sys->params.count <= 64 && !((sys->task_access[0] | sys->task_access[1]) & 2u) && !world_reads_own_writes(sys);
    for (int i = 0; plain && i < sys->world_uses.count; i++) plain = !sys->world_uses.items[i].writes;
    for (int i = 0; plain && i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (!gather_param(sys, p)) continue;
        const decl *d = p->type.decl;
        w.written[i] = system_param_bytes(prog, sys, i);
        plain = !d->builtin && !d->is_local && !d->has_preparation_state && !p->local_use && !p->task_own && !p->task_ref;
        gathered++;
    }
    // Its text and lists are the singletons': what's changed of them is changed from the log, in order
    for (int i = 0; plain && sys->writes_text && i < sys->heap_owners.count; i++) {
        bool own = false;
        for (int k = 0; k < sys->params.count; k++) {
            own |= gather_param(sys, &sys->params.items[k]) && sys->params.items[k].type.decl == sys->heap_owners.items[i];
        }
        plain = own;
    }
    if (plain && gathered) {
        gather_statement(&w, &plan, sys->body);
        plan.ok = !w.failed && plan.stmts.count > 0;
        // Worth it only where the rows keep work for the threads: what's run from the log is still one thread's,
        // and logging it costs about what a cheap statement does. By tidec's reckoning of the code as written
        // (the functions called counted in), the rows keep at least as much as the log takes. Measured on a
        // system that only adds (a thousand rows: a count, a sum and a list's Add each): 0.24 ms in order,
        // 0.28 logged and run after.
        if (plan.ok) {
            const uint64_t whole = step_cost(sys->body);
            uint64_t logged = 0;
            for (int i = 0; i < plan.stmts.count; i++) logged += step_cost(plan.stmts.items[i].from);
            plan.little = whole < logged * 2u;
            plan.ok = !plan.little;
        }
    }
    vec_push(gather_plans, plan);
    return &gather_plans.items[gather_plans.count - 1];
}

bool system_gather_check(const program *prog, const decl *sys, bool *little)
{
    const gather_plan *plan = system_gather_plan(prog, sys);
    *little = plan->little;
    return plan->ok;
}

// ---------------------------------------------------------------------------
// Generated C

// Which of the plan's statements `s` is, or -1
static int gather_index(const gather_plan *plan, const stmt *s)
{
    for (int i = 0; plan && i < plan->stmts.count; i++) {
        if (plan->stmts.items[i].from == s) return i;
    }
    return -1;
}

// struct tide_gather_<System>_<n>: what statement n takes from its row
static void gen_gather_types(gen *g, const decl *sys, const gather_plan *plan)
{
    sb *o = &g->c;
    for (int i = 0; i < plan->stmts.count; i++) {
        const gather_stmt *gs = &plan->stmts.items[i];
        if (!gs->captures.count) continue;
        sb_printf(o, "struct tide_gather_%s_%d {\n", decl_cname(sys), i);
        for (int k = 0; k < gs->captures.count; k++) sb_printf(o, "    %s c%d;\n", c_type(gs->captures.items[k].value->type), k);
        sb_put(o, "};\n");
    }
}

static void gen_stmt(gen *g, const stmt *s);

// In the system's code: the row's values, then the statement where it is, or its entry in the task's log
static void gen_gather_stmt(gen *g, const decl *sys, const gather_plan *plan, const int index)
{
    sb *o = &g->c;
    const gather_stmt *gs = &plan->stmts.items[index];
    line_directive(g, gs->from->at);
    if (index < 64) g->gather_hit |= UINT64_C(1) << index;
    line(g, o, "{");
    g->indent++;
    for (int k = 0; k < gs->captures.count; k++) {
        const gather_capture *c = &gs->captures.items[k];
        indent(g, o);
        sb_printf(o, "const %s %s = ", c_type(c->value->type), c->named->hoisted);
        gen_expr(g, o, c->value);
        sb_put(o, ";\n");
    }
    line(g, o, "if (tide_task_gather) {");
    g->indent++;
    if (gs->captures.count) {
        line(g, o, "struct tide_gather_%s_%d tide_ge;", decl_cname(sys), index);
        line(g, o, "memset(&tide_ge, 0, sizeof tide_ge);");
        for (int k = 0; k < gs->captures.count; k++) line(g, o, "tide_ge.c%d = %s;", k, gs->captures.items[k].named->hoisted);
        line(g, o, "tide_gather_push(%du, &tide_ge, (uint32_t)sizeof tide_ge);", index + 1);
    } else {
        line(g, o, "tide_gather_push(%du, NULL, 0u);", index + 1);
    }
    g->indent--;
    line(g, o, "} else {");
    g->indent++;
    gen_stmt(g, gs->replay);
    g->indent--;
    line(g, o, "}");
    g->indent--;
    line(g, o, "}");
}

// In tide_replay_<System>: the logs of the system's tasks in order, each entry's statement run with the values
// its row logged, and a row's end where the system's code would have let go of what it made
static void gen_gather_replay(gen *g, const decl *sys, const gather_plan *plan, const char *row_end)
{
    sb *o = &g->c;
    line(g, o, "uint32_t tide_gn = 0u;");
    line(g, o, "const tide_gather *const tide_gs = tide_gathered(&tide_gn);");
    line(g, o, "for (uint32_t tide_gt = 0; tide_gt < tide_gn; tide_gt++) {");
    g->indent++;
    line(g, o, "for (uint32_t tide_gat = 0; tide_gat < tide_gs[tide_gt].used;) {");
    g->indent++;
    line(g, o, "uint32_t tide_gh[2];");
    line(g, o, "memcpy(tide_gh, tide_gs[tide_gt].bytes + tide_gat, sizeof tide_gh);");
    line(g, o, "const uint8_t *const tide_gv = tide_gs[tide_gt].bytes + tide_gat + 8u;");
    line(g, o, "tide_gat += 8u + ((tide_gh[1] + 7u) & ~7u);");
    line(g, o, "(void)tide_gv;");
    line(g, o, "switch (tide_gh[0]) {");
    line(g, o, "case 0u: {%s } break; // A row's end", row_end);
    for (int i = 0; i < plan->stmts.count; i++) {
        const gather_stmt *gs = &plan->stmts.items[i];
        line(g, o, "case %du: {", i + 1);
        g->indent++;
        if (gs->captures.count) {
            line(g, o, "struct tide_gather_%s_%d tide_ge;", decl_cname(sys), i);
            line(g, o, "memcpy(&tide_ge, tide_gv, sizeof tide_ge);");
            for (int k = 0; k < gs->captures.count; k++) {
                line(g, o, "const %s %s = tide_ge.c%d;", c_type(gs->captures.items[k].value->type), gs->captures.items[k].named->hoisted, k);
            }
        }
        gen_stmt(g, gs->replay);
        g->indent--;
        line(g, o, "} break;");
    }
    line(g, o, "default: break;");
    line(g, o, "}");
    g->indent--;
    line(g, o, "}");
    g->indent--;
    line(g, o, "}");
}

static int gather_count(const gather_plan *plan)
{
    return plan->stmts.count;
}

// tide_replay_<System>(tide_w): the system's singletons where they are, and its tasks' logs run in order. Called
// by tide_done_<System>, on one thread, before anything that waits for the system starts.
static void gen_gather_function(gen *g, const decl *sys, const char *row_end)
{
    sb *o = &g->c;
    const gather_plan *plan = system_gather_plan(g->prog, sys);
    const char *name = decl_cname(sys);
    sb_printf(o, "// system " STR_FMT ": what its rows logged for its singletons, in the rows' order\n", STR_ARG(sys->qualified));
    sb_printf(o, "static void tide_replay_%s(tide_world *tide_w)\n{\n", name);
    g->indent = 1;
    g->spawn_temps = 0;
    g->call_temps = 0;
    g->routine = NULL;
    g->code = sys;
    g->has_scene = false;
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (p->type.kind != TY_SINGLETON || (p->mode != PARAM_READ && p->mode != PARAM_MUT) || p->type.decl->is_local) continue;
        line(g, o, "%s%s *restrict %s = &tide_w->%s;", p->mode == PARAM_MUT ? "" : "const ", c_type(p->type), local_cname(g, p->name),
             type_cname(p->type.decl));
        line(g, o, "(void)%s;", local_cname(g, p->name));
    }
    {
        sb path = {0};
        sb_printf(&path, "tide_causal_entity(UINT64_C(%llu), (tide_entity){0})", (unsigned long long)causal_code(sys));
        gen_causal_local(g, sys, path.data);
    }
    if (g->prog->uses_text) line(g, o, "const uint64_t tide_mark = tide_scratch_mark(); (void)tide_mark;");
    // Builds that check whose heaps a system changes (TIDE_HEAP_CHECKS): the log's are the system's
    if (g->prog->uses_heap && sys->writes_text) {
        sb ids = {0};
        bool done[TIDE_HEAP_IDS] = {false};
        int allowed = 0;
        for (int k = 0; k < sys->heap_owners.count; k++) {
            const decl *owner = sys->heap_owners.items[k];
            const uint32_t id = owner ? owner->heap_id : 0u;
            if (done[id]) continue;
            done[id] = true;
            sb_printf(&ids, "%s%u", allowed++ ? ", " : "", id);
        }
        sb who = {0};
        gen_c_literal(&who, diagnostic_name(g, tool_name(sys), "system", sys->index), false);
        sb_put(o, "#ifdef TIDE_HEAP_CHECKS\n");
        line(g, o, "static const uint16_t tide_replay_allowed[] = {%s};", ids.data ? ids.data : "0");
        line(g, o, "tide_heaps_allow(&tide_w->heap, tide_replay_allowed, %d, %s);", allowed, who.data);
        sb_put(o, "#endif\n");
    }
    gen_grid_caches(g);
    gen_gather_replay(g, sys, plan, row_end);
    close_list_caches(g);
    if (g->prog->uses_heap && sys->writes_text) {
        sb_put(o, "#ifdef TIDE_HEAP_CHECKS\n");
        line(g, o, "tide_heaps_allow(NULL, NULL, 0, NULL);");
        sb_put(o, "#endif\n");
    }
    g->indent = 0;
    sb_put(o, "}\n\n");
    line_reset(g);
    g->code = NULL;
}
