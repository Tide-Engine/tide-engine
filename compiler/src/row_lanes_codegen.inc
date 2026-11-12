// ---------------------------------------------------------------------------
// The lanes of a component a system's row copies, and when
//
// A system runs for each entity with a copy of the components it takes, made
// from their lanes, and puts back the lanes it can write. For a component of
// many fields (a party of 186 lanes) the copy is most of a row's time, and a
// system that returns on its first line for most rows paid it for all of them.
//
// So a row copies the lanes its system's code can read (code_reads, as a view's
// does) and those it can write, which are put back whether or not it wrote
// them. And where the code starts with `if (...) return;` on plain values
// (fields, constants, comparisons), those tests are a function of their own,
// tide_guard_<System>, which the loop calls with only the lanes they read
// copied: a row it returns true for is left as it is, the rest of its copies
// never made.

// Whether system `sys`'s rows may copy less than all of what they take: a
// system that runs for each entity, with code of its own that's all there is
// to it. Not in a build for a debugger (codegen_options.split_plumbing, the game's own code left as written):
// there a breakpoint on a system's first line stops for every entity, and every field it shows is the entity's.
static bool system_rows_plain(const gen *g, const decl *sys)
{
    return !g->opts->split_plumbing && !sys->is_view && !sys->is_handler && sys->per_entity && sys->body && !sys->extern_system && !sys->starts_tasks
        && !sys->is_async && !sys->is_virtual && !sys->is_override && !sys->replaced_by && !sys->is_local && !sys->preparation_cpu
        && !sys->preparation_owner && !sharded(g);
}

// Whether parameter `i`'s copy can be made of some of its lanes: a component nothing but the system's own code
// reads the copy of
static bool system_param_splits(const gen *g, const decl *sys, const int i)
{
    const param *p = &sys->params.items[i];
    if (!system_rows_plain(g, sys) || p->type.kind != TY_COMPONENT || (p->mode != PARAM_READ && p->mode != PARAM_MUT)) return false;
    const decl *d = p->type.decl;
    const int lanes = comp_lanes(d);
    return lanes > 1 && lanes <= LANE_MASK_MOST && decl_layout(d).size <= LANE_WRITES_MOST && !d->is_local
        && !d->has_preparation_state && !world_sets_component(sys, d) && !joined(sys, p);
}

static lane_mask lane_or(const lane_mask a, const lane_mask b)
{
    lane_mask m;
    for (int i = 0; i < LANE_MASK_WORDS; i++) m.w[i] = a.w[i] | b.w[i];
    return m;
}

static lane_mask lane_without(const lane_mask a, const lane_mask b)
{
    lane_mask m;
    for (int i = 0; i < LANE_MASK_WORDS; i++) m.w[i] = a.w[i] & ~b.w[i];
    return m;
}

static bool lane_none(const lane_mask *m)
{
    for (int i = 0; i < LANE_MASK_WORDS; i++) {
        if (m->w[i]) return false;
    }
    return true;
}

// The lanes a row of system `sys` copies of its component parameter `i`: what its code can read, and what it
// can write (put back from the copy). Every lane where it can't say.
static lane_mask system_param_lanes(const gen *g, const decl *sys, const int i)
{
    const param *p = &sys->params.items[i];
    const decl *d = p->type.decl;
    const lane_mask every = lane_every(comp_lanes(d));
    if (!system_param_splits(g, sys, i)) return every;
    lane_mask read;
    if (!reads_lane_mask(d, code_reads(sys, i), &read)) return every;
    if (p->mode == PARAM_MUT) read = lane_or(read, written_lanes(g, sys, d->index));
    // One it reads nothing of still gets a copy that's something: its code can hand it on as a value
    if (lane_none(&read)) lane_set(&read, 0);
    return read;
}

// The lanes of component `c` the rows of system `sys` copy in archetype `a`, whichever of its parameters takes
// them: the ones its loop is handed. Every lane of one it reads another way: the Owner its input is found by.
static lane_mask system_comp_lanes(const gen *g, const decl *sys, const int a, const int c)
{
    const decl *d = g->prog->components.items[c];
    const lane_mask every = lane_every(comp_lanes(d));
    if (c == g->prog->owner->index && (reads_owner(g, sys, a) || filters_owner(g, sys, a))) return every;
    lane_mask used = {{0}};
    bool taken = false;
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (p->type.kind != TY_COMPONENT || p->type.decl != d || (p->mode != PARAM_READ && p->mode != PARAM_MUT)) continue;
        used = lane_or(used, system_param_lanes(g, sys, i));
        taken = true;
    }
    return taken ? used : every;
}

// Whether `t` is a value a guard compares: no text, list, map or grid in it
static bool guard_value(const type t)
{
    switch (t.kind) {
    case TY_STRING: case TY_LIST: case TY_MAP: case TY_SET: case TY_GRID: case TY_QUERY: case TY_WORLD: return false;
    default: return !type_has_text(t);
    }
}

// Whether `e` is plain: reads of the system's parameters' fields, constants and operators on them, with nothing
// that calls, allocates, rolls or changes anything. Running it twice, or not at all, is the same as once.
static bool guard_plain(const expr *e)
{
    if (!e || e->hoisted || e->lambda) return false;
    switch (e->kind) {
    case E_INT: case E_FLOAT: case E_BOOL: return true;
    case E_DEFAULT: return guard_value(e->type);
    case E_NAME:
        if (e->bind == BIND_CONST) return guard_value(e->type);
        if (e->bind != BIND_PARAM || !e->param) return false;
        return e->param->type.kind == TY_COMPONENT || e->param->type.kind == TY_SINGLETON;
    case E_MEMBER:
        if (e->enum_member) return true;
        if (e->constant) return guard_value(e->type);
        if (e->property || e->local_field || e->edge != EDGE_NONE || e->c_constant || e->typed_text || e->raw_sampled
            || e->target_colors || e->asset_ref_value) return false;
        // A list's count, of a field: read where the list is
        if (!e->field && e->object && e->object->type.kind == TY_LIST && str_eq_c(e->member, "count")) {
            return e->object->kind == E_MEMBER && e->object->field && !e->object->property && !e->object->local_field
                && guard_plain(e->object->object);
        }
        if (!e->field && e->swizzle_len <= 0) return false;
        return guard_plain(e->object) && (guard_value(e->type) || e->type.kind == TY_LIST);
    case E_UNARY:
        return !e->method && guard_plain(e->lhs) && guard_value(e->type);
    case E_BINARY:
        return !e->method && guard_plain(e->lhs) && guard_plain(e->rhs) && guard_value(e->lhs->type) && guard_value(e->rhs->type)
            && guard_value(e->type);
    default: return false;
    }
}

// A value a guard's test can be: plain, and not a list by itself (guard_plain lets one through for its count)
static bool guard_term(const expr *e)
{
    return guard_plain(e) && e->type.kind == TY_BOOL;
}

// The tests system `sys`'s code starts with, any of which true returns before it does anything: each leading
// `if (a || b) return;`, as its `a` and `b`, up to the first that isn't plain. How many there are.
static int system_guard_terms(const decl *sys, const expr **terms)
{
    int n = 0;
    for (int i = 0; i < sys->body->stmts.count; i++) {
        const stmt *s = sys->body->stmts.items[i];
        if (s->kind != S_IF || s->else_stmt || !s->cond || !s->then_stmt) return n;
        const stmt *then = s->then_stmt;
        if (then->kind == S_BLOCK && then->stmts.count == 1) then = then->stmts.items[0];
        if (then->kind != S_RETURN || then->value) return n;
        // `a || b || c` is ((a || b) || c): its terms from the left, while they're plain
        const expr *chain[GUARD_TERMS_MOST];
        int links = 0;
        const expr *e = s->cond;
        while (e->kind == E_BINARY && e->op == T_OR && !e->method && links < GUARD_TERMS_MOST - 1) {
            chain[links++] = e->rhs;
            e = e->lhs;
        }
        chain[links++] = e;
        for (int k = links - 1; k >= 0; k--) {
            if (!guard_term(chain[k]) || n == GUARD_TERMS_MOST) return n;
            terms[n++] = chain[k];
        }
    }
    return n;
}

// A view's guard. A view that runs for each entity gathers, blends and corrects what it reads of the entity's
// components before its code runs, so one that goes through a thousand entities to act on six pays for a
// thousand. Where its code starts with `if (...) return;` tests that read nothing of the match's components,
// the row's tests run first (tide_view_guard_<View>) and a row they turn away is never copied.
//
// A test here reads this machine's own state alone, which needs no world between ticks and no correction:
// the entity's local fields (through a component parameter), a local singleton's fields, and constants.
static bool view_guard_base(const expr *e)
{
    if (!e || e->hoisted || e->lambda) return false;
    if (e->kind == E_NAME) { // A local singleton, whole: its fields are read below
        return e->bind == BIND_PARAM && e->param && e->param->type.kind == TY_SINGLETON && e->param->type.decl->is_local
            && !e->param->verified;
    }
    if (e->kind != E_MEMBER || e->property || e->edge != EDGE_NONE || e->c_constant || e->typed_text || e->raw_sampled
        || e->target_colors || e->asset_ref_value || !e->field || !guard_value(e->type)) return false;
    if (e->local_field) { // `t.wholeNow`: a field of the entity's local part, through the view's own parameter
        const expr *of = e->object;
        return of && of->kind == E_NAME && of->bind == BIND_PARAM && of->param && of->param->type.kind == TY_COMPONENT
            && !of->param->verified && (of->param->mode == PARAM_READ || of->param->mode == PARAM_MUT);
    }
    return view_guard_base(e->object);
}

static bool view_guard_plain(const expr *e)
{
    if (!e || e->hoisted || e->lambda) return false;
    switch (e->kind) {
    case E_INT: case E_FLOAT: case E_BOOL: return true;
    case E_DEFAULT: return guard_value(e->type);
    case E_NAME: return e->bind == BIND_CONST && guard_value(e->type);
    case E_MEMBER:
        if (e->enum_member) return true;
        if (e->constant) return guard_value(e->type);
        if (e->swizzle_len > 0 && !e->field) return view_guard_base(e->object) && guard_value(e->type);
        return view_guard_base(e);
    case E_UNARY:
        return !e->method && view_guard_plain(e->lhs) && guard_value(e->type);
    case E_BINARY:
        return !e->method && view_guard_plain(e->lhs) && view_guard_plain(e->rhs) && guard_value(e->lhs->type)
            && guard_value(e->rhs->type) && guard_value(e->type);
    default: return false;
    }
}

// Whether a test reads anything: one of constants alone is left to the code (it turns away every row or none)
static bool view_guard_reads(const expr *e)
{
    if (!e) return false;
    if (e->kind == E_MEMBER && !e->enum_member && !e->constant) return true;
    return view_guard_reads(e->lhs) || view_guard_reads(e->rhs);
}

// The tests view `sys`'s code starts with that its guard runs: as system_guard_terms, by a view's rule
static int view_guard_terms(const decl *sys, const expr **terms)
{
    int n = 0;
    bool reads = false;
    for (int i = 0; i < sys->body->stmts.count; i++) {
        const stmt *s = sys->body->stmts.items[i];
        if (s->kind != S_IF || s->else_stmt || !s->cond || !s->then_stmt) break;
        const stmt *then = s->then_stmt;
        if (then->kind == S_BLOCK && then->stmts.count == 1) then = then->stmts.items[0];
        if (then->kind != S_RETURN || then->value) break;
        const expr *chain[GUARD_TERMS_MOST];
        int links = 0;
        const expr *e = s->cond;
        while (e->kind == E_BINARY && e->op == T_OR && !e->method && links < GUARD_TERMS_MOST - 1) {
            chain[links++] = e->rhs;
            e = e->lhs;
        }
        chain[links++] = e;
        bool whole = true;
        for (int k = links - 1; k >= 0; k--) {
            if (!view_guard_plain(chain[k]) || chain[k]->type.kind != TY_BOOL || n == GUARD_TERMS_MOST) {
                whole = false;
                break;
            }
            reads = reads || view_guard_reads(chain[k]);
            terms[n++] = chain[k];
        }
        if (!whole) break;
    }
    return reads ? n : 0;
}

// Whether view `sys`'s rows are tested by tide_view_guard_<View> before anything of them is copied: one that
// runs for each entity of the world it draws alone (a `verified` parameter's row is found in another world)
static bool view_guarded(const decl *sys)
{
    if (!sys->is_view || !sys->per_entity || !sys->body || sys->is_async || sys->starts_tasks || sys->takes_verified
        || sys->verified_join || sys->verified_entities) return false;
    const expr *terms[GUARD_TERMS_MOST];
    return view_guard_terms(sys, terms) > 0;
}

// Whether system `sys`'s rows are tested by tide_guard_<System> before their copies are made
static bool system_guarded(const gen *g, const decl *sys)
{
    if (!system_rows_plain(g, sys) || batch_event(sys) || batch_destroy(sys)) return false;
    const expr *terms[GUARD_TERMS_MOST];
    return system_guard_terms(sys, terms) > 0;
}

// The lanes of parameter `i` the guard's tests read: none of one that can't be copied in parts, whose copy
// is whole before the guard
static lane_mask system_guard_lanes(const gen *g, const decl *sys, const int i)
{
    const param *p = &sys->params.items[i];
    const decl *d = p->type.decl;
    const lane_mask every = lane_every(comp_lanes(d));
    if (!system_param_splits(g, sys, i)) return every;
    const int size = decl_layout(d).size;
    byte_writes r = {size, false, arena_alloc((size_t)size)};
    memset(r.bytes, 0, (size_t)size);
    const expr *terms[GUARD_TERMS_MOST];
    const int n = system_guard_terms(sys, terms);
    const reads_root root = {p, NULL, NULL, false};
    for (int k = 0; k < n; k++) expr_reads(terms[k], &root, &r);
    if (r.all) return every;
    lane_mask mask = {{0}};
    const int lanes = comp_lanes(d), width = comp_width(d);
    for (int k = 0; k < lanes; k++) {
        for (int b = 0; b < width; b++) {
            if (r.bytes[k * width + b]) lane_set(&mask, k);
        }
    }
    return mask;
}

// What a row copies of parameter `i` before the guard (`first`), and for the rows that pass it (`rest`). With
// no guard, all of it is `first`. `apart`: the loop writes the lanes the system changes in another place than
// it reads them, so a row the guard leaves out still carries those across.
typedef struct row_lanes {
    lane_mask first, rest;
    bool whole; // `first` is every lane: the copy as it's always been
} row_lanes;

static row_lanes system_row_lanes(const gen *g, const decl *sys, const int i, const bool guarded, const bool apart)
{
    const param *p = &sys->params.items[i];
    const decl *d = p->type.decl;
    const lane_mask every = lane_every(comp_lanes(d));
    const lane_mask all = system_param_lanes(g, sys, i);
    row_lanes r = {all, {{0}}, false};
    if (guarded && system_param_splits(g, sys, i)) {
        lane_mask first = system_guard_lanes(g, sys, i);
        if (apart && p->mode == PARAM_MUT) first = lane_or(first, written_lanes(g, sys, d->index));
        // (The guard's tests are the code's, so what they read is in `all` already)
        r.first = lane_without(all, lane_without(all, first));
        r.rest = lane_without(all, r.first);
    }
    r.whole = lane_same(&r.first, &every);
    return r;
}

// Copies of lanes `mask` of row tide_i's component `d` into `value`, from the chunk's `lanes` of it
static void gen_gather_lanes(sb *o, const char *pad, const decl *d, const char *value, const char *lanes, const lane_mask *mask)
{
    const int count = comp_lanes(d), width = comp_width(d);
    for (int k = 0; k < count; k++) {
        if (!lane_has(mask, k)) continue;
        sb_printf(o, "%smemcpy((uint8_t *)&%s + %d, %s[%d] + (size_t)tide_i * %d, %d);\n", pad, value, k * width, lanes, k, width, width);
    }
}
