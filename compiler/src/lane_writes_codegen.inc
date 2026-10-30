// ---------------------------------------------------------------------------
// The lanes of a component a system's code can write
//
// A system that takes `mut Body body` may change only some of Body's fields:
// position and velocity, say, and never its radius. Each lane it can't write
// stays where it was when the system's loop runs apart from a snapshot (no
// page taken, copied, compared or hashed again) and isn't stored back.
//
// This finds, for a parameter of a system or a function, the bytes its code
// can write: each assignment to it or to a field of it, each place of it a
// call takes `mut` or `out` (through the callee's own code where it's a Tide
// function, all of that place where it's C or built in), each method called on
// a place of it, and everything the code holds inside it (blocks, lambdas,
// steps). What it can't tell exactly it takes as written: a property, an
// element, a swizzle across its vector, a value it can't place. A component
// whose lanes it can't say (more than LANE_MASK_MOST, or a code that reaches it
// another way: through its World, or in a task) is written whole.

#define LANE_WRITES_MOST 4096 // Bytes of a parameter this follows; past that, all of it

// A component's lanes, a bit each (lane k is bit k % 64 of word k / 64). Up to
// 64 lanes the generated C takes the first word as it did; wider components
// (a fighter of 77 lanes, say) take the words as an array.
typedef struct lane_mask {
    uint64_t w[LANE_MASK_WORDS];
} lane_mask;

static bool lane_has(const lane_mask *m, const int k)
{
    return m->w[k >> 6] >> (k & 63) & 1u;
}

static void lane_set(lane_mask *m, const int k)
{
    m->w[k >> 6] |= UINT64_C(1) << (k & 63);
}

static lane_mask lane_every(const int lanes)
{
    lane_mask m = {{0}};
    for (int k = 0; k < lanes && k < LANE_MASK_MOST; k++) lane_set(&m, k);
    return m;
}

static bool lane_same(const lane_mask *a, const lane_mask *b)
{
    for (int i = 0; i < LANE_MASK_WORDS; i++) {
        if (a->w[i] != b->w[i]) return false;
    }
    return true;
}

// The words a component of `lanes` lanes needs
static int lane_words(const int lanes)
{
    return (lanes + 63) / 64;
}

typedef struct byte_writes {
    int size;       // The value's bytes
    bool all;       // Every one of them
    uint8_t *bytes; // A byte each: 1 for one the code can write
} byte_writes;

// A place inside the parameter: its bytes from `off`, `size` of them
typedef struct writes_place {
    bool rooted; // It's the parameter, or a part of it
    int off, size;
    bool element; // Through an element: all of what holds it, which a field after can't narrow (pair[1].item isn't pair[0]'s)
} writes_place;

// What each function's code writes of each of its parameters, worked out once
typedef struct writes_memo {
    const decl *code;
    int index;
    int state; // 1 while it's being worked out (a call back into it writes all), 2 once it is
    byte_writes w;
} writes_memo;

static VEC(writes_memo) lane_memo;

static void writes_mark(byte_writes *w, const int off, const int size)
{
    if (w->all) return;
    if (off < 0 || size <= 0 || (int64_t)off + size > w->size) {
        w->all = true;
        return;
    }
    memset(w->bytes + off, 1, (size_t)size);
}

// Where field `f` starts in a struct or component's bytes, as decl_layout lays it out; -1 if it isn't one of its fields
static int writes_field_offset(const decl *d, const field *f)
{
    int64_t at = 0;
    for (int i = 0; i < d->fields.count; i++) {
        const layout l = type_layout(d->fields.items[i].type);
        if (l.size == LAYOUT_MOST) return -1;
        at = (at + l.align - 1) / l.align * l.align;
        if (&d->fields.items[i] == f) return at > INT32_MAX ? -1 : (int)at;
        at += l.size;
    }
    return -1;
}

static writes_place writes_place_of(const expr *e, const param *root)
{
    const writes_place none = {false, 0, 0, false};
    if (!e) return none;
    switch (e->kind) {
    case E_NAME:
        if (e->bind != BIND_PARAM || e->param != root) return none;
        return (writes_place){true, 0, type_layout(root->type).size, false};
    case E_MEMBER: {
        const writes_place p = writes_place_of(e->object, root);
        if (!p.rooted || p.element || e->property || e->local_field) return p; // A property, a local part or an element's part: all of what it's on
        const type ot = e->object->type;
        if (e->swizzle_len > 0) { // Components of a vector, from the first to the last it names
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
    case E_INDEX: { // An element: all of what it's in
        writes_place p = writes_place_of(e->object, root);
        p.element = p.rooted;
        return p;
    }
    default: return none;
    }
}

static const byte_writes *code_writes(const decl *code, int index);

// A call's argument `arg`, given for parameter `index` of `callee` (NULL for a
// built-in one), as `mut` when `mut` says so: what that writes of the root
static void writes_through(const expr *arg, const decl *callee, const int index, const bool mut, const param *root, byte_writes *w)
{
    const writes_place p = writes_place_of(arg, root);
    if (!p.rooted) return;
    if (arg->is_out) {
        writes_mark(w, p.off, p.size);
        return;
    }
    if (!mut) return;
    const byte_writes *sub = callee && !callee->is_extern && callee->body ? code_writes(callee, index) : NULL;
    if (!sub || sub->all || sub->size != p.size) {
        writes_mark(w, p.off, p.size);
        return;
    }
    for (int b = 0; b < sub->size; b++) {
        if (sub->bytes[b]) writes_mark(w, p.off + b, 1);
    }
}

static void stmt_writes(const stmt *s, const param *root, byte_writes *w);

static void expr_writes(const expr *e, const param *root, byte_writes *w)
{
    if (!e || w->all) return;
    // What a call or an operator takes `mut` or `out`
    const decl *callee = e->method;
    const bool routine = callee && (e->call == CALL_FUNCTION || e->call == CALL_METHOD || e->kind == E_BINARY || e->kind == E_UNARY);
    for (int i = 0; i < e->args.count; i++) {
        bool mut = false;
        if (routine) mut = i < callee->params.count && callee->params.items[i].mode == PARAM_MUT;
        else if (e->kind == E_CALL || e->kind == E_METHOD) mut = i < 32 && (e->arg_mut >> i & 1u);
        writes_through(e->args.items[i], routine ? callee : NULL, i, mut, root, w);
    }
    if (routine && (e->kind == E_BINARY || e->kind == E_UNARY)) {
        writes_through(e->lhs, callee, 0, callee->params.count > 0 && callee->params.items[0].mode == PARAM_MUT, root, w);
        writes_through(e->rhs, callee, 1, callee->params.count > 1 && callee->params.items[1].mode == PARAM_MUT, root, w);
    }
    // A Snap of a field counts it in a hidden field beside the fields, which no code names: all of it
    if (e->call == CALL_SNAP && writes_place_of(e->object, root).rooted) {
        w->all = true;
        return;
    }
    // A method on a place of it may change that place: a list's, a map's, a Random's, a struct's own
    if (e->kind == E_METHOD) {
        const writes_place p = writes_place_of(e->object, root);
        if (p.rooted) writes_mark(w, p.off, p.size);
    }
    expr_writes(e->object, root, w);
    expr_writes(e->lhs, root, w);
    expr_writes(e->rhs, root, w);
    expr_writes(e->cond, root, w);
    for (int i = 0; i < e->args.count; i++) expr_writes(e->args.items[i], root, w);
    for (int i = 0; i < e->written.count; i++) expr_writes(e->written.items[i], root, w);
    for (int i = 0; i < e->inits.count; i++) expr_writes(e->inits.items[i].value, root, w);
    expr_writes(e->lambda_body, root, w);
    expr_writes(e->values, root, w);
    expr_writes(e->skin_mesh, root, w);
    expr_writes(e->skin_pose, root, w);
    expr_writes(e->callable_receiver, root, w);
    expr_writes(e->task_source, root, w);
    expr_writes(e->binding_value, root, w);
    stmt_writes(e->block, root, w);
    stmt_writes(e->step, root, w);
    stmt_writes(e->compare[0], root, w);
    stmt_writes(e->compare[1], root, w);
    stmt_writes(e->out_local, root, w);
    stmt_writes(e->binding, root, w);
}

static void stmt_writes(const stmt *s, const param *root, byte_writes *w)
{
    if (!s || w->all) return;
    if (s->kind == S_ASSIGN) {
        const writes_place p = writes_place_of(s->target, root);
        if (p.rooted) writes_mark(w, p.off, p.size);
    }
    for (int i = 0; i < s->stmts.count; i++) stmt_writes(s->stmts.items[i], root, w);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++) expr_writes(s->cases.items[i].labels.items[k], root, w);
        for (int k = 0; k < s->cases.items[i].body.count; k++) stmt_writes(s->cases.items[i].body.items[k], root, w);
    }
    expr_writes(s->cond, root, w);
    expr_writes(s->value, root, w);
    expr_writes(s->target, root, w);
    expr_writes(s->property_set, root, w);
    expr_writes(s->by, root, w);
    expr_writes(s->offset, root, w);
    stmt_writes(s->init, root, w);
    stmt_writes(s->step, root, w);
    stmt_writes(s->then_stmt, root, w);
    stmt_writes(s->else_stmt, root, w);
}

// What `code` can write of its parameter `index`: all of it while it's being
// worked out, as a call back into it might write anything
static const byte_writes *code_writes(const decl *code, const int index)
{
    for (int i = 0; i < lane_memo.count; i++) {
        writes_memo *m = &lane_memo.items[i];
        if (m->code != code || m->index != index) continue;
        if (m->state == 1) return NULL;
        return &m->w;
    }
    const param *root = &code->params.items[index];
    const int size = type_layout(root->type).size;
    writes_memo m = {code, index, 1, {size, false, NULL}};
    if (size <= 0 || size > LANE_WRITES_MOST || !code->body) m.w.all = true;
    else {
        m.w.bytes = arena_alloc((size_t)size);
        memset(m.w.bytes, 0, (size_t)size);
    }
    vec_push(lane_memo, m);
    const int at = lane_memo.count - 1;
    byte_writes w = m.w;
    if (!w.all) stmt_writes(code->body, root, &w);
    lane_memo.items[at].w = w;
    lane_memo.items[at].state = 2;
    return &lane_memo.items[at].w;
}

static bool rows_alone(const decl *sys);

// The lanes of component `c` a system's code can write, a bit each (lane k is
// bit k): every lane of one it takes `mut` when this can't say less
static lane_mask written_lanes(const gen *g, const decl *sys, const int c)
{
    const decl *d = g->prog->components.items[c];
    const int lanes = comp_lanes(d), width = comp_width(d);
    const lane_mask every = lane_every(lanes);
    if (lanes > LANE_MASK_MOST || !rows_alone(sys) || world_sets_component(sys, d) || sys->is_view || !sys->body) return every;
    int index = -1;
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (p->type.kind == TY_COMPONENT && p->type.decl == d && p->mode == PARAM_MUT) index = index < 0 ? i : -2;
    }
    if (index < 0) return every; // None, or two of it (an alias the analysis doesn't follow)
    const byte_writes *w = code_writes(sys, index);
    if (!w || w->all || w->size != lanes * width) return every;
    lane_mask mask = {{0}};
    for (int k = 0; k < lanes; k++) {
        for (int b = 0; b < width; b++) {
            if (w->bytes[k * width + b]) lane_set(&mask, k);
        }
    }
    return mask;
}

// Whether a system leaves some lanes of component `c` it takes `mut` unwritten
static bool lanes_partial(const gen *g, const decl *sys, const int c)
{
    const int lanes = comp_lanes(g->prog->components.items[c]);
    if (lanes > LANE_MASK_MOST) return false;
    const lane_mask every = lane_every(lanes);
    const lane_mask written = written_lanes(g, sys, c);
    return !lane_same(&written, &every);
}

// The mask as C: for up to 64 lanes the word, `UINT64_C(0x...)`; wider, the
// words as an array, `((const uint64_t[]){UINT64_C(0x...), ...})`
static void lane_mask_c(sb *o, const lane_mask *m, const int lanes)
{
    if (lanes <= 64) {
        sb_printf(o, "UINT64_C(0x%llx)", (unsigned long long)m->w[0]);
        return;
    }
    sb_put(o, "((const uint64_t[]){");
    for (int i = 0; i < lane_words(lanes); i++) sb_printf(o, "%sUINT64_C(0x%llx)", i ? ", " : "", (unsigned long long)m->w[i]);
    sb_put(o, "})");
}

// C that's true when lane `k` (a C expression) is in the mask
static void lane_test_c(sb *o, const lane_mask *m, const int lanes, const char *k)
{
    if (lanes <= 64) {
        sb_printf(o, "(UINT64_C(0x%llx) >> %s & 1u)", (unsigned long long)m->w[0], k);
        return;
    }
    sb_put(o, "(");
    lane_mask_c(o, m, lanes);
    sb_printf(o, "[(%s) >> 6] >> ((%s) & 63u) & 1u)", k, k);
}

// The runs of neighboring lanes of component `c` a system writes, into `runs`
// (room for LANE_MASK_MOST): how many there are. Every lane is one run when it
// writes all.
typedef struct lane_run {
    int first, count;
} lane_run;

static int written_runs(const gen *g, const decl *sys, const int c, lane_run *runs)
{
    const int lanes = comp_lanes(g->prog->components.items[c]);
    if (lanes > LANE_MASK_MOST || !lanes_partial(g, sys, c)) {
        runs[0] = (lane_run){0, lanes};
        return 1;
    }
    const lane_mask mask = written_lanes(g, sys, c);
    int n = 0;
    for (int k = 0; k < lanes;) {
        if (!lane_has(&mask, k)) {
            k++;
            continue;
        }
        const int first = k;
        while (k < lanes && lane_has(&mask, k)) k++;
        runs[n++] = (lane_run){first, k - first};
    }
    return n;
}
