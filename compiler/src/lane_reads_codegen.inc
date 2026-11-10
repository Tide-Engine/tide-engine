// ---------------------------------------------------------------------------
// The lanes of a component a view's code reads
//
// A view runs for each entity with a copy of the components it takes, blended
// between the last two ticks, and a view's query makes a row of each entity it
// goes through the same way. For a component of many fields, gathering it from
// its lanes, finding the entity in last tick's world and blending it there is
// most of the view's time when the view reads a few of them. So a view copies
// and blends only the lanes its code can read (tide_gather_some_<C>,
// tide_prev_some_<C>, tide_blend_some_<C>); the others are left as they were,
// as nothing reads them.
//
// This finds the bytes of a parameter, or of a query row's binding, the code
// can read, as code_writes finds what it writes: each place of it used as a
// value, narrowed through fields and swizzles, and through the callee's own
// code where a place of it is handed to a Tide function. What it can't narrow
// (the value whole, an element's container, a property, a method's receiver,
// a value handed to C or a built-in, a call back into a function being worked
// out) is all of that place. A local field is read from the entity's local
// part, never its lanes.

// What the code reads from: a parameter, or the binding of a query's row
// in a foreach over the query (`row.f` of `foreach (var row in all)`)
typedef struct reads_root {
    const param *param;
    const stmt *row;
    const field *binding;
    bool transient_match; // No whole-value helper narrowing; preserve opaque/captured values in full.
} reads_root;

// Whether a name's parameter is `p`. A view's frags and verts get what they
// read that the view doesn't take as more parameters after the view's code is
// checked (bind_views), which can move the parameters: the code's names then
// point at the copy from before, the same parameter by its name and type.
static bool same_param(const param *named, const param *p)
{
    return named == p
        || (named && p && str_eq(named->name, p->name) && named->type.kind == p->type.kind && named->type.decl == p->type.decl);
}

static writes_place reads_place_of(const expr *e, const reads_root *root)
{
    const writes_place none = {false, 0, 0, false};
    if (!e) return none;
    if (root->row && e->kind == E_MEMBER && e->field == root->binding && e->object && e->object->kind == E_NAME
        && e->object->bind == BIND_LOCAL && e->object->local == root->row) {
        return (writes_place){true, 0, type_layout(e->type).size, false};
    }
    switch (e->kind) {
    case E_NAME:
        if (!root->param || e->bind != BIND_PARAM || !same_param(e->param, root->param)) return none;
        return (writes_place){true, 0, type_layout(root->param->type).size, false};
    case E_MEMBER: {
        const writes_place p = reads_place_of(e->object, root);
        if (!p.rooted || p.element || e->property || e->local_field) return p;
        const type ot = e->object->type;
        if (e->swizzle_len > 0) {
            const int dims = type_dim(ot);
            if (dims <= 0 || p.size % dims) return p;
            const int each = p.size / dims;
            int lo = 4, hi = -1;
            for (int k = 0; k < e->swizzle_len; k++) {
                if (e->swizzle[k] < lo) lo = e->swizzle[k];
                if (e->swizzle[k] > hi) hi = e->swizzle[k];
            }
            if (lo < 0 || hi >= dims) return p;
            return (writes_place){true, p.off + lo * each, (hi - lo + 1) * each, false};
        }
        if (!e->field || (ot.kind != TY_STRUCT && ot.kind != TY_COMPONENT) || !ot.decl) return p;
        const int at = writes_field_offset(ot.decl, e->field);
        if (at < 0) return p;
        return (writes_place){true, p.off + at, type_layout(e->field->type).size, false};
    }
    case E_INDEX: {
        writes_place p = reads_place_of(e->object, root);
        p.element = p.rooted;
        return p;
    }
    default: return none;
    }
}

// Whether `e` is a place in a local field of it, `f.guards` or `f.guards.x`
static bool in_local_part(const expr *e, const reads_root *root)
{
    for (; e && (e->kind == E_MEMBER || e->kind == E_INDEX); e = e->object) {
        if (e->kind == E_MEMBER && e->local_field && reads_place_of(e->object, root).rooted) return true;
    }
    return false;
}

static const byte_writes *code_reads(const decl *code, int index);

// The bytes of a value of type `t`, from `base`, that hold text, lists, maps or grids: what a copy with lists of
// its own reads of it (tide_copy_<T>), whatever else is done with it. Marked in `r`.
static void heap_bytes(const type t, const int base, byte_writes *r)
{
    // (type_has_text answers for a struct's fields, not a component's or a singleton's)
    const bool fields = (t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_EVENT) && t.decl;
    if (!fields) {
        if (type_has_text(t)) writes_mark(r, base, type_layout(t).size);
        return;
    }
    for (int i = 0; i < t.decl->fields.count; i++) {
        const field *f = &t.decl->fields.items[i];
        const bool nested = (f->type.kind == TY_STRUCT || f->type.kind == TY_COMPONENT || f->type.kind == TY_SINGLETON) && f->type.decl;
        if (!nested && !type_has_text(f->type)) continue;
        if (nested && !decl_has_text(f->type.decl)) continue;
        const int at = writes_field_offset(t.decl, f);
        if (at < 0) {
            r->all = true;
            return;
        }
        heap_bytes(f->type, base + at, r);
    }
}

static void expr_reads(const expr *e, const reads_root *root, byte_writes *r);
static void stmt_reads(const stmt *s, const reads_root *root, byte_writes *r);

// The indexes along a place, `a[i].b[j]`: read as any expression is
static void index_reads(const expr *e, const reads_root *root, byte_writes *r)
{
    for (; e && (e->kind == E_MEMBER || e->kind == E_INDEX); e = e->object) {
        if (e->kind == E_INDEX) expr_reads(e->lhs, root, r);
    }
}

static bool reads_direct_data_path(const expr *e)
{
    for (; e && e->kind == E_MEMBER; e = e->object) {
        if (!e->field || e->property || e->local_field || e->swizzle_len) return false;
    }
    return e && e->kind == E_NAME;
}

static void expr_reads(const expr *e, const reads_root *root, byte_writes *r)
{
    if (!e || r->all) return;
    if (root->row && e->kind == E_NAME && e->bind == BIND_LOCAL && e->local == root->row) { // The row whole
        r->all = true;
        return;
    }
    if (root->row && e->kind == E_MEMBER && e->field != root->binding && e->object && e->object->kind == E_NAME
        && e->object->bind == BIND_LOCAL && e->object->local == root->row) {
        return; // Another of the row's fields: its entity, or another binding
    }
    if (in_local_part(e, root)) { // The entity's local part, `f.guards.x`: none of its lanes
        index_reads(e, root, r);
        return;
    }
    if (root->transient_match && e->kind == E_MEMBER && e->swizzle_len > 0) {
        const writes_place p = reads_place_of(e->object, root);
        const int dims = type_dim(e->object->type);
        if (p.rooted && !p.element && dims > 0 && p.size == type_layout(e->object->type).size
            && p.size % dims == 0 && reads_direct_data_path(e->object)) {
            const int each = p.size / dims;
            for (int k = 0; k < e->swizzle_len; k++) writes_mark(r, p.off + e->swizzle[k] * each, each);
            index_reads(e, root, r);
            return;
        }
        // A property or an earlier swizzle can reorder the receiver's lanes.
        // Read that receiver conservatively rather than interpreting its bytes
        // as the returned vector's layout.
        expr_reads(e->object, root, r);
        return;
    }
    if (e->kind == E_NAME || e->kind == E_MEMBER || e->kind == E_INDEX) {
        const writes_place p = reads_place_of(e, root);
        if (p.rooted) {
            writes_mark(r, p.off, p.size);
            index_reads(e, root, r);
            return;
        }
    }
    // A place of it handed to a Tide function: what that function's code reads of its parameter
    const decl *callee = e->method;
    const bool routine = !root->transient_match && callee && !callee->is_extern && callee->body && !callee->is_async && !callee->is_virtual && !callee->is_override
        && !callee->replaced_by && (e->call == CALL_FUNCTION || e->call == CALL_METHOD);
    for (int i = 0; i < e->args.count; i++) {
        const expr *arg = e->args.items[i];
        const writes_place p = reads_place_of(arg, root);
        const bool local = in_local_part(arg, root);
        if (!p.rooted || local || !routine || i >= callee->params.count) {
            expr_reads(arg, root, r);
            continue;
        }
        // A read-only argument holding lists the call could change is handed over as a copy with lists of its
        // own (gen_routine_call): its text and lists are read to make one, whatever the callee's code reads.
        // (By the types alone, which says so at least wherever the call does.)
        const param *taken = &callee->params.items[i];
        const bool copied = taken->mode != PARAM_MUT && type_has_list(taken->type) && !type_is_asset_collection(taken->type)
            && call_changes_lists_of(callee, e, taken->type, NULL, NULL);
        const byte_writes *sub = code_reads(callee, i);
        if (copied && p.size == type_layout(taken->type).size) heap_bytes(taken->type, p.off, r);
        else if (copied) sub = NULL;
        if (!sub || sub->all || sub->size != p.size) writes_mark(r, p.off, p.size);
        else {
            for (int b = 0; b < sub->size; b++) {
                if (sub->bytes[b]) writes_mark(r, p.off + b, 1);
            }
        }
        index_reads(arg, root, r);
    }
    expr_reads(e->object, root, r);
    expr_reads(e->lhs, root, r);
    expr_reads(e->rhs, root, r);
    expr_reads(e->cond, root, r);
    for (int i = 0; i < e->written.count; i++) expr_reads(e->written.items[i], root, r);
    for (int i = 0; i < e->inits.count; i++) expr_reads(e->inits.items[i].value, root, r);
    expr_reads(e->lambda_body, root, r);
    expr_reads(e->values, root, r);
    expr_reads(e->mesh_lod, root, r);
    expr_reads(e->skin_mesh, root, r);
    expr_reads(e->skin_pose, root, r);
    expr_reads(e->callable_receiver, root, r);
    expr_reads(e->task_source, root, r);
    expr_reads(e->binding_value, root, r);
    stmt_reads(e->block, root, r);
    stmt_reads(e->step, root, r);
    stmt_reads(e->compare[0], root, r);
    stmt_reads(e->compare[1], root, r);
    stmt_reads(e->out_local, root, r);
    stmt_reads(e->binding, root, r);
}

static void stmt_reads(const stmt *s, const reads_root *root, byte_writes *r)
{
    if (!s || r->all) return;
    for (int i = 0; i < s->stmts.count; i++) stmt_reads(s->stmts.items[i], root, r);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++) expr_reads(s->cases.items[i].labels.items[k], root, r);
        for (int k = 0; k < s->cases.items[i].body.count; k++) stmt_reads(s->cases.items[i].body.items[k], root, r);
    }
    expr_reads(s->cond, root, r);
    expr_reads(s->value, root, r);
    expr_reads(s->target, root, r); // What it assigns to, as read: a compound assignment reads it
    expr_reads(s->property_set, root, r);
    expr_reads(s->by, root, r);
    expr_reads(s->offset, root, r);
    stmt_reads(s->init, root, r);
    stmt_reads(s->step, root, r);
    stmt_reads(s->then_stmt, root, r);
    stmt_reads(s->else_stmt, root, r);
}

static VEC(writes_memo) read_memo;

// What `code` can read of its parameter `index`: all of it while it's being
// worked out, as a call back into it might read anything
static const byte_writes *code_reads(const decl *code, const int index)
{
    for (int i = 0; i < read_memo.count; i++) {
        writes_memo *m = &read_memo.items[i];
        if (m->code != code || m->index != index) continue;
        if (m->state == 1) return NULL;
        return &m->w;
    }
    const param *p = &code->params.items[index];
    const int size = type_layout(p->type).size;
    writes_memo m = {code, index, 1, {size, false, NULL}};
    if (size <= 0 || size > LANE_WRITES_MOST || !code->body) m.w.all = true;
    else {
        m.w.bytes = arena_alloc((size_t)size);
        memset(m.w.bytes, 0, (size_t)size);
    }
    vec_push(read_memo, m);
    const int at = read_memo.count - 1;
    byte_writes r = m.w;
    const reads_root root = {p, NULL, NULL, false};
    if (!r.all) stmt_reads(code->body, &root, &r);
    read_memo.items[at].w = r;
    read_memo.items[at].state = 2;
    return &read_memo.items[at].w;
}

// Views themselves do not carry the helper-only decl.draws summary. Find
// direct drawing and calls to drawing helpers, including nested Action blocks.
static bool stmt_has_drawing(const stmt *s);

static bool expr_has_drawing(const expr *e)
{
    if (!e) return false;
    if (e->call == CALL_DRAW || e->call == CALL_GUI || (e->method && e->method->draws)) return true;
    if (expr_has_drawing(e->object) || expr_has_drawing(e->lhs) || expr_has_drawing(e->rhs)
        || expr_has_drawing(e->cond)) return true;
    for (int i = 0; i < e->args.count; i++) if (expr_has_drawing(e->args.items[i])) return true;
    for (int i = 0; i < e->written.count; i++) if (expr_has_drawing(e->written.items[i])) return true;
    for (int i = 0; i < e->inits.count; i++) if (expr_has_drawing(e->inits.items[i].value)) return true;
    return expr_has_drawing(e->lambda_body) || expr_has_drawing(e->values) || expr_has_drawing(e->mesh_lod) || expr_has_drawing(e->skin_mesh)
        || expr_has_drawing(e->skin_pose) || expr_has_drawing(e->callable_receiver)
        || expr_has_drawing(e->task_source) || expr_has_drawing(e->binding_value) || stmt_has_drawing(e->block)
        || stmt_has_drawing(e->step) || stmt_has_drawing(e->compare[0]) || stmt_has_drawing(e->compare[1])
        || stmt_has_drawing(e->out_local) || stmt_has_drawing(e->binding);
}

static bool stmt_has_drawing(const stmt *s)
{
    if (!s) return false;
    for (int i = 0; i < s->stmts.count; i++) if (stmt_has_drawing(s->stmts.items[i])) return true;
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++)
            if (expr_has_drawing(s->cases.items[i].labels.items[k])) return true;
        for (int k = 0; k < s->cases.items[i].body.count; k++)
            if (stmt_has_drawing(s->cases.items[i].body.items[k])) return true;
    }
    return expr_has_drawing(s->cond) || expr_has_drawing(s->value) || expr_has_drawing(s->target)
        || expr_has_drawing(s->property_set) || expr_has_drawing(s->by) || expr_has_drawing(s->offset)
        || stmt_has_drawing(s->init) || stmt_has_drawing(s->step) || stmt_has_drawing(s->then_stmt)
        || stmt_has_drawing(s->else_stmt);
}

// Whether a view can copy only some of component `d`'s lanes: one whose
// blending, corrections and copies all go field by field
static bool reads_some_allowed(const gen *g, const decl *d, const decl *code)
{
    const int lanes = comp_lanes(d);
    // A frag or a vert takes a component from the entity the view draws for, through no name in the view's code
    // Non-drawing views have no implicit shader reads: binding attached local
    // material fields need not gather the material's numeric match lanes.
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *s = g->prog->decls.items[i];
        if (s->kind != DECL_SHADER) continue;
        for (int k = 0; k < s->params.count; k++) {
            if (s->params.items[k].type.decl == d && stmt_has_drawing(code->body)) return false;
        }
    }
    return lanes > 1 && lanes <= LANE_MASK_MOST && decl_layout(d).size <= LANE_WRITES_MOST && !d->interpolate
        && !blend_reads_heap(d) && !sharded(g);
}

static bool reads_lane_mask(const decl *d, const byte_writes *r, lane_mask *mask);

// The bytes of `d`'s fields a view's correction changes (tide_correct_view_<C>: its [Position] and [Rotation]
// fields, a struct's too), from `base` in the value: marked in `r`
static void correction_bytes(const decl *d, const int base, byte_writes *r)
{
    for (int i = 0; i < d->fields.count; i++) {
        const field *f = &d->fields.items[i];
        if (f->hidden || correction_snap(f)) continue;
        const int at = writes_field_offset(d, f);
        if (at < 0) {
            r->all = true;
            return;
        }
        if (f->view_correction) writes_mark(r, base + at, type_layout(f->type).size);
        else if (f->type.kind == TY_STRUCT && decl_corrects(f->type.decl)) correction_bytes(f->type.decl, base + at, r);
    }
}

// Bytes a view reads as lanes (reads_lane_mask), with the fields its correction changes: a view corrects the
// copy it reads whatever it reads of it, which reads and writes those. False when that's every lane.
static bool view_lane_mask(const decl *d, const byte_writes *r, lane_mask *mask)
{
    if (!r || r->all || !decl_corrects(d)) return reads_lane_mask(d, r, mask);
    byte_writes with = {r->size, false, arena_alloc((size_t)r->size)};
    memcpy(with.bytes, r->bytes, (size_t)r->size);
    correction_bytes(d, 0, &with);
    return reads_lane_mask(d, &with, mask);
}

// Bytes read as lanes, with the hidden fields (the counts of snaps blending
// compares) always: false when that's every lane
static bool reads_lane_mask(const decl *d, const byte_writes *r, lane_mask *mask)
{
    if (!r || r->all) return false;
    const int lanes = comp_lanes(d), width = comp_width(d);
    if (r->size != lanes * width) return false;
    *mask = (lane_mask){{0}};
    for (int k = 0; k < lanes; k++) {
        for (int b = 0; b < width; b++) {
            if (r->bytes[k * width + b]) lane_set(mask, k);
        }
    }
    for (int i = 0; i < d->fields.count; i++) {
        const field *f = &d->fields.items[i];
        if (!f->hidden) continue;
        const int at = writes_field_offset(d, f);
        if (at < 0) return false;
        const int size = type_layout(f->type).size;
        for (int k = at / width; k * width < at + size; k++) lane_set(mask, k);
    }
    const lane_mask every = lane_every(lanes);
    return !lane_same(mask, &every);
}

// The lanes view `sys` reads of its component parameter `i`, into `mask`:
// false when it reads all of them, or can't say
static bool view_param_lanes(const gen *g, const decl *sys, const int i, lane_mask *mask)
{
    const param *p = &sys->params.items[i];
    if (!sys->is_view || !sys->body || sys->starts_tasks || sys->is_async || p->type.kind != TY_COMPONENT
        || (p->mode != PARAM_READ && p->mode != PARAM_MUT) || (p->mode == PARAM_MUT && p->type.decl->is_local) // Put back whole
        || p->verified || joined(sys, p) || !reads_some_allowed(g, p->type.decl, sys)) {
        return false;
    }
    return view_lane_mask(p->type.decl, code_reads(sys, i), mask);
}

// How many times the code names parameter `q`, and how many of those are
// foreach statements going through it
typedef struct query_uses {
    const param *q;
    int named, looped;
} query_uses;

static void stmt_query_uses(const stmt *s, query_uses *u);

static void expr_query_uses(const expr *e, query_uses *u)
{
    if (!e) return;
    if (e->kind == E_NAME && e->bind == BIND_PARAM && same_param(e->param, u->q)) u->named++;
    expr_query_uses(e->object, u);
    expr_query_uses(e->lhs, u);
    expr_query_uses(e->rhs, u);
    expr_query_uses(e->cond, u);
    for (int i = 0; i < e->args.count; i++) expr_query_uses(e->args.items[i], u);
    for (int i = 0; i < e->inits.count; i++) expr_query_uses(e->inits.items[i].value, u);
    expr_query_uses(e->lambda_body, u);
    expr_query_uses(e->values, u);
    expr_query_uses(e->mesh_lod, u);
    expr_query_uses(e->skin_mesh, u);
    expr_query_uses(e->skin_pose, u);
    expr_query_uses(e->callable_receiver, u);
    expr_query_uses(e->task_source, u);
    expr_query_uses(e->binding_value, u);
    stmt_query_uses(e->block, u);
    stmt_query_uses(e->step, u);
    stmt_query_uses(e->compare[0], u);
    stmt_query_uses(e->compare[1], u);
    stmt_query_uses(e->out_local, u);
    stmt_query_uses(e->binding, u);
}

static void stmt_query_uses(const stmt *s, query_uses *u)
{
    if (!s) return;
    if (s->kind == S_FOREACH && same_param(s->query, u->q) && s->value && s->value->kind == E_NAME && same_param(s->value->param, u->q)) {
        u->looped++;
    }
    for (int i = 0; i < s->stmts.count; i++) stmt_query_uses(s->stmts.items[i], u);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++) expr_query_uses(s->cases.items[i].labels.items[k], u);
        for (int k = 0; k < s->cases.items[i].body.count; k++) stmt_query_uses(s->cases.items[i].body.items[k], u);
    }
    expr_query_uses(s->cond, u);
    expr_query_uses(s->value, u);
    expr_query_uses(s->target, u);
    expr_query_uses(s->property_set, u);
    expr_query_uses(s->by, u);
    expr_query_uses(s->offset, u);
    stmt_query_uses(s->init, u);
    stmt_query_uses(s->step, u);
    stmt_query_uses(s->then_stmt, u);
    stmt_query_uses(s->else_stmt, u);
}

// Whether a view makes its queries' rows itself, where its code first names one, rather than being handed them
// made: one that runs once a frame. (A view that runs for each entity makes them before the first, and keeps
// them for the rest.)
static bool view_rows_lazy(const gen *g, const decl *sys)
{
    return sys->is_view && !sys->per_entity && sys->body && !sys->is_async && !sys->starts_tasks && !sharded(g) && takes_query_rows(sys);
}

// The first of a view's statements that names one of its queries over the match: its count where none does
static int view_rows_from(const decl *sys)
{
    for (int i = 0; i < sys->body->stmts.count; i++) {
        for (int k = 0; k < sys->params.count; k++) {
            const param *q = &sys->params.items[k];
            if (q->type.kind != TY_QUERY || !q->query_rows) continue;
            query_uses u = {q, 0, 0};
            stmt_query_uses(sys->body->stmts.items[i], &u);
            if (u.named || u.looped) return i;
        }
    }
    return sys->body->stmts.count;
}

static bool rows_name_char(const char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// Whether generated C `text` names the rows of one of a view's queries, tide_q<i> or tide_q<i>n
static bool text_names_rows(const decl *sys, const char *text, const size_t n)
{
    for (size_t at = 0; at + 6 < n; at++) {
        if (memcmp(text + at, "tide_q", 6) != 0 || (at > 0 && rows_name_char(text[at - 1]))) continue;
        size_t end = at + 6;
        int index = 0;
        if (end >= n || text[end] < '0' || text[end] > '9') continue;
        while (end < n && text[end] >= '0' && text[end] <= '9' && index < 100000) index = index * 10 + (text[end++] - '0');
        if (end < n && text[end] == 'n') end++;
        if (end < n && rows_name_char(text[end])) continue;
        if (index < sys->params.count && sys->params.items[index].type.kind == TY_QUERY && sys->params.items[index].query_rows) return true;
    }
    return false;
}

// What the foreach statements over query `q` read of its row's `binding`,
// counting those it reaches: statements of statements, not of an expression's
// block
static void stmt_row_reads(const stmt *s, const param *q, const field *binding, byte_writes *r, int *reached)
{
    if (!s || r->all) return;
    if (s->kind == S_FOREACH && same_param(s->query, q)) {
        const reads_root root = {NULL, s, binding, false};
        stmt_reads(s->then_stmt, &root, r);
        (*reached)++;
    }
    for (int i = 0; i < s->stmts.count; i++) stmt_row_reads(s->stmts.items[i], q, binding, r, reached);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) stmt_row_reads(s->cases.items[i].body.items[k], q, binding, r, reached);
    }
    stmt_row_reads(s->init, q, binding, r, reached);
    stmt_row_reads(s->step, q, binding, r, reached);
    stmt_row_reads(s->then_stmt, q, binding, r, reached);
    stmt_row_reads(s->else_stmt, q, binding, r, reached);
}

// The lanes view `sys` reads of binding `b` of its query parameter `qi`, the
// row's field `binding`, into `mask`: false when it reads all of them, or
// can't say. Only a query the view does nothing with but go through it.
static bool view_query_lanes(const gen *g, const decl *sys, const int qi, const int b, const field *binding, lane_mask *mask)
{
    const param *q = &sys->params.items[qi];
    const param *p = &q->query.items[b];
    if (!sys->is_view || !sys->body || !q->query_rows || q->query_verified_entities || q->query_verified_join || p->verified
        || !reads_some_allowed(g, p->type.decl, sys)) {
        return false;
    }
    query_uses u = {q, 0, 0};
    stmt_query_uses(sys->body, &u);
    if (!u.looped || u.named != u.looped) return false;
    const int size = type_layout(binding->type).size;
    if (size != decl_layout(p->type.decl).size) return false;
    byte_writes r = {size, false, arena_alloc((size_t)size)};
    memset(r.bytes, 0, (size_t)size);
    int reached = 0;
    stmt_row_reads(sys->body, q, binding, &r, &reached);
    if (reached != u.looped) return false; // One in a block it didn't look in
    return view_lane_mask(p->type.decl, &r, mask);
}

// A transient match foreach row: never shorten a value that can escape into
// a task, a shard, a materialized parallel row or a whole-value consumer.
static bool match_query_lanes(const gen *g, const stmt *s, const param *p, const field *binding, uint64_t *words)
{
    const decl *code = own_routine(g) ? own_routine(g) : g->code;
    const decl *d = p->type.decl;
    if (!code || code->is_view || code->is_local || code->is_async || code->starts_tasks || sharded(g)
        || p->mode != PARAM_READ || p->type.kind != TY_COMPONENT || d->is_local
        || comp_lanes(d) <= 1 || comp_lanes(d) > LANE_MASK_MOST || decl_layout(d).size > LANE_WRITES_MOST) return false;
    const int size = type_layout(binding->type).size;
    if (size <= 0 || size != decl_layout(d).size) return false;
    byte_writes r = {size, false, arena_alloc((size_t)size)};
    memset(r.bytes, 0, (size_t)size);
    const reads_root root = {NULL, s, binding, true};
    stmt_reads(s->then_stmt, &root, &r);
    lane_mask mask;
    if (!reads_lane_mask(d, &r, &mask)) return false;
    memcpy(words, mask.w, sizeof mask.w);
    return true;
}

// Whether any view copies only some of component `d`'s lanes: then it has
// the helpers that do (gen_component_access, gen_blend_helpers)
static bool reads_some_used(const gen *g, const decl *d)
{
    const program *prog = g->prog;
    for (int v = 0; v < prog->views.count; v++) {
        const decl *sys = prog->views.items[v];
        for (int i = 0; i < sys->params.count; i++) {
            const param *p = &sys->params.items[i];
            lane_mask mask;
            if (p->type.kind == TY_COMPONENT && p->type.decl == d && view_param_lanes(g, sys, i, &mask)) return true;
            if (p->type.kind != TY_QUERY || !p->query_rows) continue;
            for (int k = 0, f = 1; k < p->query.count; k++) {
                const param *b = &p->query.items[k];
                if (!param_binding(b)) continue;
                const field *binding = &p->type.decl->fields.items[f++];
                if (b->type.decl == d && view_query_lanes(g, sys, i, k, binding, &mask)) return true;
            }
        }
    }
    return false;
}

// The mask as the C a `const uint64_t *some` takes: its words, as an array
static void lane_words_c(sb *o, const lane_mask *m, const int lanes)
{
    sb_put(o, "((const uint64_t[]){");
    for (int i = 0; i < lane_words(lanes); i++) sb_printf(o, "%sUINT64_C(0x%llx)", i ? ", " : "", (unsigned long long)m->w[i]);
    sb_put(o, "})");
}
