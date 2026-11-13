// ---------------------------------------------------------------------------
// What the schedule goes by: the bytes of a component or singleton a system's
// generated code touches (parallel.c, find_conflicts)
//
// Two systems that take the same component or singleton wait for each other
// only where one writes bytes the other reads or writes. What a system reads
// and writes is what its generated C does, so this asks the functions that
// decide that C, never an analysis of its own: a row's copy is the lanes
// system_param_lanes says and puts back the ones written_lanes says, a query's
// row the lanes match_query_lanes says, a singleton is read and written where
// it is, field by field (code_reads, code_writes). Wherever those say "all",
// or the code reaches the data some other way, so does this.
//
// A component's bytes are whole lanes: a lane is a page of its own in a chunk
// (tide/table.h), which one system replaces or writes at a time, so two fields
// in one lane are one thing to the schedule.

static gen schedule_gen(const program *prog, codegen_options *opts, const decl *sys)
{
    *opts = (codegen_options){0};
    opts->split_plumbing = prog->rows_whole; // A build for a debugger copies rows whole (system_rows_plain)
    gen g = {0};
    g.prog = (program *)(uintptr_t)prog;
    g.opts = opts;
    g.code = sys;
    return g;
}

static uint8_t *schedule_none(const int size)
{
    uint8_t *bytes = arena_alloc((size_t)(size > 0 ? size : 1));
    memset(bytes, 0, (size_t)(size > 0 ? size : 1));
    return bytes;
}

static uint8_t *schedule_lanes(const decl *d, const lane_mask *m)
{
    const int lanes = comp_lanes(d), width = comp_width(d);
    uint8_t *bytes = schedule_none(lanes * width);
    for (int k = 0; k < lanes && k < LANE_MASK_MOST; k++) {
        if (lane_has(m, k)) memset(bytes + k * width, 1, (size_t)width);
    }
    return bytes;
}

static uint8_t *schedule_bytes(const byte_writes *w)
{
    uint8_t *bytes = schedule_none(w->size);
    if (w->all) memset(bytes, 1, (size_t)w->size);
    else memcpy(bytes, w->bytes, (size_t)w->size);
    return bytes;
}

// A singleton's bytes by whole fields: a field any byte of which is touched is touched whole. What the analysis
// can tell apart inside one (an element of a fixed array, one field of a struct handed to a function) the
// generated C may still write as one place, and one field is as far as the schedule says anyway. False where a
// field's place can't be said.
static bool schedule_whole_fields(const decl *d, uint8_t *bytes, const int size)
{
    for (int i = 0; i < d->fields.count; i++) {
        const int at = writes_field_offset(d, &d->fields.items[i]);
        const int len = type_layout(d->fields.items[i].type).size;
        if (at < 0 || len <= 0 || len == LAYOUT_MOST || at + len > size) return false;
        bool any = false;
        for (int k = at; k < at + len; k++) any |= bytes[k] != 0;
        if (any) memset(bytes + at, 1, (size_t)len);
    }
    return true;
}

// Whether a system's code is all that touches the singletons it takes, where they are: code of its own that
// runs as written, in the tick
static bool schedule_singleton_plain(const program *prog, const decl *sys, const decl *d)
{
    return sys->body && !sys->is_view && !sys->is_handler && !sys->extern_system && !sys->starts_tasks && !sys->is_async
        && !sys->is_virtual && !sys->is_override && !sys->replaced_by && !sys->is_local && !sys->preparation_cpu
        && !sys->preparation_owner && !prog->shard_state && !d->builtin && !d->is_local && !d->has_preparation_state;
}

data_bytes system_param_bytes(const program *prog, const decl *sys, const int i)
{
    const data_bytes all = {0, NULL, NULL};
    const param *p = &sys->params.items[i];
    if ((p->mode != PARAM_READ && p->mode != PARAM_MUT) || !p->type.decl) return all;
    const decl *d = p->type.decl;
    if (p->type.kind == TY_COMPONENT) {
        codegen_options opts;
        const gen g = schedule_gen(prog, &opts, sys);
        if (!system_param_splits(&g, sys, i)) return all;
        const lane_mask read = system_param_lanes(&g, sys, i);
        lane_mask written = {{0}};
        if (p->mode == PARAM_MUT) written = written_lanes(&g, sys, d->index);
        return (data_bytes){comp_lanes(d) * comp_width(d), schedule_lanes(d, &read), schedule_lanes(d, &written)};
    }
    if (p->type.kind != TY_SINGLETON || !schedule_singleton_plain(prog, sys, d)) return all;
    const byte_writes *r = code_reads(sys, i);
    if (!r || r->size <= 0) return all;
    const byte_writes *w = p->mode == PARAM_MUT ? code_writes(sys, i) : NULL;
    if (p->mode == PARAM_MUT && (!w || w->size != r->size)) return all;
    if (r->all && (!w || w->all)) return all;
    uint8_t *reads = schedule_bytes(r), *writes = w ? schedule_bytes(w) : schedule_none(r->size);
    if (!schedule_whole_fields(d, reads, r->size) || !schedule_whole_fields(d, writes, r->size)) return all;
    return (data_bytes){r->size, reads, writes};
}

typedef struct schedule_rows {
    const gen *g;
    const param *q;
    const param *p;       // The binding
    int binding_index;    // ...its place in q->query
    const field *binding; // ...and its field of the row
    lane_mask lanes;
    int reached;
    bool all;
} schedule_rows;

// The loops over the query among a statement's own statements (as stmt_row_reads looks): the lanes each one's
// rows copy of the binding, and the lanes an index of it is made from
static void schedule_row_loops(const stmt *s, schedule_rows *w)
{
    if (!s || w->all) return;
    if ((s->kind == S_FOREACH || s->kind == S_PARALLEL) && same_param(s->query, w->q)) {
        w->reached++;
        uint64_t words[LANE_MASK_WORDS] = {0};
        // (A parallel loop gathers its rows whole: gen_query_walk with `gather`)
        if (s->kind == S_PARALLEL || !match_query_lanes(w->g, s, w->p, w->binding, words)) {
            w->all = true;
            return;
        }
        for (int i = 0; i < LANE_MASK_WORDS; i++) w->lanes.w[i] |= words[i];
        const loop_index *plan = s->index;
        const decl *comp = w->p->type.decl;
        const int width = comp_width(comp), lanes = comp_lanes(comp);
        for (int k = 0; plan && k < plan->count && k < 3; k++) {
            const ix_query x = {.q = w->q, .selection = w->q, .row = w->q->type.decl};
            int off = -1, size = 0;
            const int b = ix_key_binding(&x, &plan->keys[k], &off, &size);
            if (b < 0) {
                w->all = true;
                return;
            }
            if (b != w->binding_index) continue;
            if (off < 0 || size <= 0 || off + size > lanes * width) {
                w->all = true;
                return;
            }
            for (int lane = off / width; lane <= (off + size - 1) / width; lane++) lane_set(&w->lanes, lane);
        }
    }
    for (int i = 0; i < s->stmts.count; i++) schedule_row_loops(s->stmts.items[i], w);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) schedule_row_loops(s->cases.items[i].body.items[k], w);
    }
    schedule_row_loops(s->init, w);
    schedule_row_loops(s->step, w);
    schedule_row_loops(s->then_stmt, w);
    schedule_row_loops(s->else_stmt, w);
}

data_bytes system_query_bytes(const program *prog, const decl *sys, const decl *component)
{
    const data_bytes all = {0, NULL, NULL};
    const world_use *use = NULL;
    for (int i = 0; i < sys->world_uses.count; i++) {
        if (sys->world_uses.items[i].component == component) use = &sys->world_uses.items[i];
    }
    // Through its World it's any entity's, whole (a Get's copy); and code that isn't a system's own goes its own way
    if (!use || use->writes || use->through_world || !use->query_read || sys->preparation_reads || !sys->body || sys->is_view
        || sys->is_handler || sys->extern_system || sys->is_virtual || sys->is_override || sys->replaced_by || prog->rows_whole) {
        return all;
    }
    codegen_options opts;
    const gen g = schedule_gen(prog, &opts, sys);
    lane_mask lanes = {{0}};
    for (int i = 0; i < sys->params.count; i++) {
        const param *q = &sys->params.items[i];
        if (q->type.kind != TY_QUERY) continue;
        for (int b = 0, ordinal = 0; b < q->query.count; b++) {
            const param *p = &q->query.items[b];
            if (!param_binding(p)) continue;
            const int at = ordinal++;
            if (p->type.decl != component) continue;
            if (q->query_local || q->query_rows || !q->type.decl || 1 + at >= q->type.decl->fields.count) return all;
            // Every use of the query is a loop over it in the system's own code
            query_uses u = {q, 0, 0};
            stmt_query_uses(sys->body, &u);
            if (u.named != u.looped) return all;
            schedule_rows w = {&g, q, p, b, &q->type.decl->fields.items[1 + at], {{0}}, 0, false};
            schedule_row_loops(sys->body, &w);
            if (w.all || w.reached != u.looped) return all;
            lanes = lane_or(lanes, w.lanes);
        }
    }
    return (data_bytes){comp_lanes(component) * comp_width(component), schedule_lanes(component, &lanes),
                        schedule_none(comp_lanes(component) * comp_width(component))};
}

bool field_bytes(const decl *d, const field *f, int *off, int *size)
{
    const int at = writes_field_offset(d, f);
    if (at < 0) return false;
    *off = at;
    *size = type_layout(f->type).size;
    return *size > 0 && *size != LAYOUT_MOST;
}

static void gather_forget(void);

void schedule_bytes_reset(void)
{
    // All three borrow the compilation's arena, as codegen's own reset says
    memset(&lane_memo, 0, sizeof lane_memo);
    memset(&read_memo, 0, sizeof read_memo);
    gather_forget();
}
