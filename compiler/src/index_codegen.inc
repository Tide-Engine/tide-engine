// ---------------------------------------------------------------------------
// Indexed loops: the code (notes/indexed-loops.md, indexing.h)
//
// A loop whose guard bounds a few numbers of each row (stmt.index) visits only
// the rows an index (tide/index.h) gives for the bounds, in the loop's own
// order, with its body, guard and all, run for each as the plain loop runs it.
// The index gives every row that can pass and maybe more, never fewer, so
// what the loop does is what the plain loop does, to the bit.
//
// An index is kept where it can be known to still hold, at a place of its
// own (a site, tide_ix_site), and checked each time the loop starts against
// what it was made from (its facts, a run of numbers):
// - A world's query: each admitted table's chunks, each chunk's rows and the
//   pages its index read (the entity column, the presence bytes the query's
//   filters read, the lanes holding the keys). The site holds a reference to
//   each page, so whoever writes one copies it first (tide/page.h): a page
//   that's the same page holds the same bytes. In the world's own cache
//   (tide_index_cache), never copied, hashed or packed.
// - A list in a singleton a system or a view reads: the list and its count,
//   during one run of the tick (or frame), when nothing that can change it runs
//   (the scheduler's promise: the code only reads it).
// - A view's rows (gen_query_fill): the rows themselves, made once a view's
//   run, counted (tide_ix_made).
// Anything else is gone through plainly.
//
// Many threads may run one per-entity system at once: an index is checked and
// built under its site's lock, once, and searched outside it, which tide/index.h
// allows. A search running when the facts say a site's index no longer holds
// leaves it be and builds one of its own.

#define IX_LEAST 32 // Rows below which a loop goes through them all: an index costs more than it saves

static bool ix_stmt_any(const stmt *s);
static bool ix_expr_any(const expr *e)
{
    if (!e) return false;
    if (ix_expr_any(e->object) || ix_expr_any(e->lhs) || ix_expr_any(e->rhs) || ix_expr_any(e->cond)) return true;
    for (int i = 0; i < e->args.count; i++) if (ix_expr_any(e->args.items[i])) return true;
    return ix_stmt_any(e->block);
}

static bool ix_stmt_any(const stmt *s)
{
    if (!s) return false;
    if ((s->kind == S_FOREACH || s->kind == S_PARALLEL) && s->index) return true;
    if (ix_expr_any(s->cond) || ix_expr_any(s->value)) return true;
    if (ix_stmt_any(s->init) || ix_stmt_any(s->step) || ix_stmt_any(s->then_stmt) || ix_stmt_any(s->else_stmt)) return true;
    for (int i = 0; i < s->stmts.count; i++) if (ix_stmt_any(s->stmts.items[i])) return true;
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].body.count; k++) if (ix_stmt_any(s->cases.items[i].body.items[k])) return true;
    }
    return false;
}

// Whether the game has a loop that goes through an index: its worlds then have a cache of them.
static bool ix_program(const gen *g)
{
    if (g->opts->no_loop_indexes) return false;
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *d = g->prog->decls.items[i];
        if ((d->kind == DECL_SYSTEM || d->kind == DECL_FUNCTION) && ix_stmt_any(d->body)) return true;
        for (int k = 0; k < d->methods.count; k++) if (ix_stmt_any(d->methods.items[k]->body)) return true;
    }
    return false;
}

// The loop's place in its world's cache, for the code being generated (a borrowed query's for its source).
static int ix_site(gen *g, const stmt *s)
{
    for (int i = 0; i < g->ix_loops.count; i++) {
        if (g->ix_loops.items[i] == s && g->ix_variants.items[i] == (const void *)g->borrow) return i;
    }
    vec_push(g->ix_loops, s);
    vec_push(g->ix_variants, (const void *)g->borrow);
    return g->ix_loops.count - 1;
}

// Whether the code being generated is a system's own, for one run of it a tick: what its loops read can't change
// while it runs, as only it could, and it doesn't (indexing.c), so an index found to hold holds till it's done.
static bool ix_fast(const gen *g)
{
    const decl *code = g->code;
    return code && code->kind == DECL_SYSTEM && !code->is_handler && !code->is_view && !code->is_local && !g->borrow
        && !g->routine && !g->frame && !g->task && !g->par_loop;
}

// What the generated code needs of every cached index, once (after the columns: they read tables).
static void gen_index_helpers(gen *g)
{
    if (!g->ix_any) return;
    sb *o = &g->c;
    sb_printf(o, "// Indexed loops (notes/indexed-loops.md): where an index is kept, and what it was made from\n"
                 "#define TIDE_INDEX_CACHE_ABI 1u\n#define TIDE_IX_LEAST %du\n", IX_LEAST);
    sb_put(o,
        "typedef struct tide_ix_site {\n"
        "    uint32_t lock;      // Checking and building (tide_index_lock)\n"
        "    uint32_t readers;   // Searches of `ix` running now\n"
        "    uint32_t ready;     // `ix` is of what `fact` says\n"
        "    uint32_t facts, fact_room, holds, hold_room;\n"
        "    uint64_t plan;      // What the code that made it reads, which a reload can change\n"
        "    uint64_t valid;     // A system's own loop's: the tick's run (epoch) its index was found to hold in\n"
        "    uint64_t *fact;     // What it was made from: numbers, and pages as numbers, in the order they're checked\n"
        "    tide_page **held;   // The pages among them, each with a reference of the site's (NULL: none taken)\n"
        "    tide_index *ix;\n"
        "} tide_ix_site;\n"
        "struct tide_index_cache {\n"
        "    uint32_t abi, sites;\n"
        "    uint64_t epoch; // The tick's run (a world's) or the frame (the local state's) whose lists a site holds for\n"
        "    tide_ix_site *site;\n"
        "};\n"
        "static uint32_t tide_index_sites(void);\n"
        "static uint32_t tide_ix_refs; // Page references the sites take or let go of, one at a time\n"
        "static uint64_t tide_ix_made __attribute__((unused)); // Rows made for views, counted: an index of a view's rows holds while they're the last\n\n");
    sb_put(o,
        "TIDE_HELPER double tide_ix_min(const double a, const double b)\n{\n"
        "    return a != a || b != b ? __builtin_nan(\"\") : a < b ? a : b;\n}\n\n"
        "TIDE_HELPER double tide_ix_max(const double a, const double b)\n{\n"
        "    return a != a || b != b ? __builtin_nan(\"\") : a > b ? a : b;\n}\n\n"
        "TIDE_HELPER void tide_ix_let_go(tide_page **pages, const uint32_t from, const uint32_t to)\n{\n"
        "    if (from >= to) return;\n"
        "    tide_index_lock(&tide_ix_refs);\n"
        "    for (uint32_t i = from; i < to; i++) if (pages[i]) tide_page_release(pages[i], 1u);\n"
        "    tide_index_unlock(&tide_ix_refs);\n}\n\n"
        "TIDE_HELPER void tide_ix_site_free(tide_ix_site *s)\n{\n"
        "    tide_ix_let_go(s->held, 0u, s->holds);\n"
        "    free(s->held);\n    free(s->fact);\n    tide_index_free(s->ix);\n    memset(s, 0, sizeof *s);\n}\n\n"
        "TIDE_HELPER void tide_index_cache_free(struct tide_index_cache *c)\n{\n"
        "    if (!c) return;\n"
        "    for (uint32_t i = 0; i < c->sites; i++) tide_ix_site_free(&c->site[i]);\n"
        "    free(c->site);\n    free(c);\n}\n\n"
        "// Serial, before code runs: a cache for this build's sites, and the next run of the tick or frame\n"
        "TIDE_HELPER void tide_index_cache_prepare(struct tide_index_cache **slot)\n{\n"
        "    const uint32_t sites = tide_index_sites();\n"
        "    struct tide_index_cache *c = *slot;\n"
        "    if (c && (c->abi != TIDE_INDEX_CACHE_ABI || c->sites != sites)) {\n"
        "        tide_index_cache_free(c);\n        c = NULL;\n    }\n"
        "    if (!c) {\n"
        "        c = tide_alloc_zeroed(1u, sizeof *c);\n"
        "        c->abi = TIDE_INDEX_CACHE_ABI;\n        c->sites = sites;\n"
        "        c->site = tide_alloc_zeroed(sites ? sites : 1u, sizeof *c->site);\n"
        "        *slot = c;\n    }\n"
        "    c->epoch++;\n}\n\n"
        "TIDE_HELPER tide_ix_site *tide_ix_site_at(struct tide_index_cache *c, const uint32_t id)\n{\n"
        "    return c && c->abi == TIDE_INDEX_CACHE_ABI && id < c->sites ? &c->site[id] : NULL;\n}\n\n");
    sb_put(o,
        "// Checking a site's facts, under its lock: they're compared as they come, and from the first that differs\n"
        "// they're taken anew, its pages held\n"
        "typedef struct tide_ix_walk {\n    tide_ix_site *s;\n    uint32_t at, held;\n    bool same, full;\n} tide_ix_walk;\n\n"
        "TIDE_HELPER tide_ix_walk tide_ix_walk_begin(tide_ix_site *s, const uint64_t plan)\n{\n"
        "    if (s->plan != plan) {\n"
        "        tide_ix_let_go(s->held, 0u, s->holds);\n"
        "        s->holds = s->facts = s->ready = 0u;\n        s->plan = plan;\n"
        "        __atomic_store_n(&s->valid, 0u, __ATOMIC_RELEASE);\n    }\n"
        "    return (tide_ix_walk){s, 0u, 0u, true, false};\n}\n\n"
        "TIDE_HELPER void tide_ix_differ(tide_ix_walk *w)\n{\n"
        "    tide_ix_site *s = w->s;\n"
        "    w->same = false;\n"
        "    tide_ix_let_go(s->held, w->held, s->holds);\n"
        "    s->holds = w->held;\n    s->facts = w->at;\n}\n\n"
        "TIDE_HELPER void tide_ix_fact(tide_ix_walk *w, const uint64_t v)\n{\n"
        "    tide_ix_site *s = w->s;\n"
        "    if (s->facts == s->fact_room) {\n"
        "        const uint32_t room = s->fact_room ? s->fact_room * 2u : 16u;\n"
        "        if (room <= s->fact_room) tide_out_of_memory();\n"
        "        s->fact = tide_realloc(s->fact, (size_t)s->fact_room * sizeof *s->fact, (size_t)room * sizeof *s->fact);\n"
        "        s->fact_room = room;\n    }\n"
        "    s->fact[s->facts++] = v;\n    w->at++;\n}\n\n"
        "TIDE_HELPER void tide_ix_num(tide_ix_walk *w, const uint64_t v)\n{\n"
        "    if (w->same) {\n"
        "        if (w->at < w->s->facts && w->s->fact[w->at] == v) {\n            w->at++;\n            return;\n        }\n"
        "        tide_ix_differ(w);\n    }\n"
        "    tide_ix_fact(w, v);\n}\n\n"
        "TIDE_HELPER void tide_ix_page(tide_ix_walk *w, tide_page *p)\n{\n"
        "    tide_ix_site *s = w->s;\n"
        "    const uint64_t v = (uint64_t)(uintptr_t)p;\n"
        "    if (w->same) {\n"
        "        if (w->at < s->facts && s->fact[w->at] == v && w->held < s->holds) {\n"
        "            w->at++;\n            w->held++;\n            return;\n        }\n"
        "        tide_ix_differ(w);\n    }\n"
        "    tide_ix_fact(w, v);\n"
        "    if (s->holds == s->hold_room) {\n"
        "        const uint32_t room = s->hold_room ? s->hold_room * 2u : 16u;\n"
        "        if (room <= s->hold_room) tide_out_of_memory();\n"
        "        s->held = tide_realloc(s->held, (size_t)s->hold_room * sizeof *s->held, (size_t)room * sizeof *s->held);\n"
        "        s->hold_room = room;\n    }\n"
        "    tide_index_lock(&tide_ix_refs);\n"
        "    const bool room = p->refs < UINT32_MAX - 1u; // A site never wraps a page's references: it just can't hold this one\n"
        "    if (room) tide_page_retain(p);\n"
        "    tide_index_unlock(&tide_ix_refs);\n"
        "    w->full |= !room;\n"
        "    s->held[s->holds++] = room ? p : NULL;\n    w->held++;\n}\n\n");
    sb_put(o,
        "// Whether the site's index is of what was walked\n"
        "TIDE_HELPER bool tide_ix_walk_end(tide_ix_walk *w)\n{\n"
        "    if (w->same && w->at != w->s->facts) tide_ix_differ(w);\n"
        "    return w->same && w->s->ready;\n}\n\n"
        "// Under the lock (with a site): the index to search, and whether to build it first. The site's, or with\n"
        "// searches of the site's running, or no site, one of the loop's own\n"
        "TIDE_HELPER tide_index *tide_ix_take(tide_ix_site *s, tide_ix_walk *w, bool *own, bool *build)\n{\n"
        "    *own = true;\n    *build = true;\n"
        "    if (!s) return tide_index_make();\n"
        "    if (tide_ix_walk_end(w)) {\n        *own = *build = false;\n        return s->ix;\n    }\n"
        "    __atomic_store_n(&s->valid, 0u, __ATOMIC_RELEASE);\n"
        "    if (__atomic_load_n(&s->readers, __ATOMIC_ACQUIRE) == 0u) {\n"
        "        if (!s->ix) s->ix = tide_index_make();\n"
        "        s->ready = !w->full;\n        *own = false;\n        return s->ix;\n    }\n"
        "    s->ready = 0u;\n    return tide_index_make();\n}\n\n"
        "// Whether a site's index is to be searched as it is, with no lock: with `fast`, a system's own loop's that\n"
        "// was found to hold in this run of the tick, which nothing can change till the system is done (the tick's\n"
        "// schedule: the system only reads what the index is of). Otherwise its lock is taken, for checking it\n"
        "TIDE_HELPER bool tide_ix_hold(tide_ix_site *s, const struct tide_index_cache *c, const bool fast)\n{\n"
        "    if (!s) return false;\n"
        "    if (fast && __atomic_load_n(&s->valid, __ATOMIC_ACQUIRE) == c->epoch) return true;\n"
        "    tide_index_lock(&s->lock);\n"
        "    if (!fast || __atomic_load_n(&s->valid, __ATOMIC_ACQUIRE) != c->epoch) return false;\n"
        "    tide_index_unlock(&s->lock);\n    return true;\n}\n\n"
        "// Built: searched outside the lock\n"
        "TIDE_HELPER void tide_ix_searching(tide_ix_site *s, const bool own, const struct tide_index_cache *c, const bool fast)\n{\n"
        "    if (!s) return;\n"
        "    if (!own) __atomic_add_fetch(&s->readers, 1u, __ATOMIC_ACQ_REL);\n"
        "    if (fast && !own && s->ready) __atomic_store_n(&s->valid, c->epoch, __ATOMIC_RELEASE);\n"
        "    tide_index_unlock(&s->lock);\n}\n\n"
        "TIDE_HELPER void tide_ix_searched(tide_ix_site *s, tide_index *ix, const bool own, const bool held)\n{\n"
        "    if (held) return;\n"
        "    if (own) tide_index_free(ix);\n"
        "    else __atomic_sub_fetch(&s->readers, 1u, __ATOMIC_ACQ_REL);\n}\n\n");
}

// The number of sites, now that every loop has been written.
static void gen_index_sites(gen *g)
{
    if (!g->ix_any) return;
    sb_printf(&g->c, "static uint32_t tide_index_sites(void)\n{\n    return %du;\n}\n\n", g->ix_loops.count);
}

// A bound, as a double worked out before the loop (indexing.h)
static void gen_ix_bound(gen *g, sb *o, const bound *b)
{
    switch (b->op) {
    case BOUND_LEAF:
        if (b->comp >= 0) sb_printf(o, "(double)((%s).%c)", expr_text(g, b->leaf), "xyzw"[b->comp & 3]);
        else sb_printf(o, "(double)(%s)", expr_text(g, b->leaf));
        return;
    case BOUND_ADD:
    case BOUND_SUB:
        sb_put(o, "(");
        gen_ix_bound(g, o, b->a);
        sb_put(o, b->op == BOUND_ADD ? " + " : " - ");
        gen_ix_bound(g, o, b->b);
        sb_put(o, ")");
        return;
    case BOUND_SQRT:
        sb_put(o, "__builtin_sqrt(");
        gen_ix_bound(g, o, b->a);
        sb_put(o, ")");
        return;
    case BOUND_MIN:
    case BOUND_MAX:
        sb_put(o, b->op == BOUND_MIN ? "tide_ix_min(" : "tide_ix_max(");
        gen_ix_bound(g, o, b->a);
        sb_put(o, ", ");
        gen_ix_bound(g, o, b->b);
        sb_put(o, ")");
        return;
    }
}

// The bounds as `<lo>[k]` and `<hi>[k]`, from the code around the loop: a missing one is infinite.
static void gen_ix_bounds(gen *g, const loop_index *plan, const char *lo, const char *hi)
{
    sb *o = &g->c;
    line(g, o, "double %s[%d], %s[%d];", lo, plan->count, hi, plan->count);
    for (int k = 0; k < plan->count; k++) {
        const index_key *key = &plan->keys[k];
        indent(g, o);
        sb_printf(o, "%s[%d] = ", lo, k);
        if (key->lo) gen_ix_bound(g, o, key->lo);
        else sb_put(o, "-__builtin_inf()");
        sb_put(o, ";\n");
        indent(g, o);
        sb_printf(o, "%s[%d] = ", hi, k);
        if (key->hi) gen_ix_bound(g, o, key->hi);
        else sb_put(o, "__builtin_inf()");
        sb_put(o, ";\n");
    }
}

// A key of a row, as C: its path from `root` (the row, or a list's element), through the fields named, then its
// component.
static void ix_path_text(sb *o, const expr *path, const char *root)
{
    if (path->kind == E_MEMBER && path->object && path->field) {
        ix_path_text(o, path->object, root);
        sb_printf(o, ".%s", field_cname(path->field));
        return;
    }
    sb_put(o, root);
}

static const char *ix_key_text(const index_key *key, const char *root)
{
    sb b = {0};
    sb_put(&b, "(double)(");
    ix_path_text(&b, key->path, root);
    if (key->comp >= 0) sb_printf(&b, ".%c", "xyzw"[key->comp & 3]);
    sb_put(&b, ")");
    return b.data;
}

static void gen_ix_keys(gen *g, const loop_index *plan, const char *root, const char *keys)
{
    sb *o = &g->c;
    indent(g, o);
    sb_printf(o, "const double %s[%d] = {", keys, plan->count);
    for (int k = 0; k < plan->count; k++) sb_printf(o, "%s%s", k ? ", " : "", ix_key_text(&plan->keys[k], root));
    sb_put(o, "};\n");
}

static uint64_t ix_hash(const char *text)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (const char *p = text; p && *p; p++) h = (h ^ (uint8_t)*p) * 0x100000001B3ull;
    return h ? h : 1u;
}

// Where `f` starts in `d`'s bytes, as decl_layout lays it out; -1 when it can't say.
static int ix_field_offset(const decl *d, const field *f)
{
    int64_t at = 0;
    for (int i = 0; d && i < d->fields.count; i++) {
        const layout l = type_layout(d->fields.items[i].type);
        if (l.size == LAYOUT_MOST || l.align <= 0) return -1;
        at = (at + l.align - 1) / l.align * l.align;
        if (&d->fields.items[i] == f) return at > INT32_MAX ? -1 : (int)at;
        at += l.size;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// A query's loop over a world's tables

typedef struct ix_query {
    const param *q, *selection;
    const decl *row;
    const char *world;     // The world's pointer, as C
    int binding[3];        // The query binding each key is in (its place in q->query)
    int lane_lo[3], lane_hi[3]; // ...and the lanes of its component holding it
} ix_query;

// The binding a key's path starts with, and the bytes of its component the key is (off, size: -1 unknown).
static int ix_key_binding(const ix_query *x, const index_key *key, int *off, int *size)
{
    // The path's members from the row: the row's field (a binding), then fields of its component and inside
    const expr *members[32];
    int n = 0;
    for (const expr *e = key->path; e && e->kind == E_MEMBER && n < 32; e = e->object) members[n++] = e;
    if (n == 0 || n == 32 || !members[n - 1]->field) return -1;
    const field *rowfield = members[n - 1]->field;
    int binding = -1;
    for (int i = 0, f = 1; i < x->q->query.count; i++) {
        const param *p = &x->q->query.items[i];
        if (!param_binding(p)) continue;
        const field *value = &x->row->fields.items[f++];
        if (value == rowfield || str_eq(value->name, rowfield->name)) binding = i;
    }
    if (binding < 0) return -1;
    *off = 0;
    for (int k = n - 2; k >= 0 && *off >= 0; k--) {
        const expr *m = members[k];
        const type ot = m->object->type;
        const int at = m->field ? ix_field_offset(ot.decl, m->field) : -1;
        *off = at < 0 ? -1 : *off + at;
    }
    const type kt = key->comp >= 0 ? type_scalar(key->path->type) : key->path->type;
    *size = type_layout(kt).size;
    if (*off >= 0 && key->comp >= 0) *off += key->comp * *size;
    return binding;
}

// The rows of each admitted table, in the loop's order, those a query's filters let through: with `build`, each
// added to the index tide_xi by its keys, else to tide_xall, the loop's candidates (fewer than TIDE_IX_LEAST)
static void gen_ix_query_rows(gen *g, const ix_query *x, const loop_index *plan, const bool build)
{
    sb *o = &g->c;
    for (int a = 0; a < g->prog->archetypes.count; a++) {
        if (!query_admits(g, x->selection, a)) continue;
        line(g, o, "{ // %s", arch_label(g, a));
        g->indent++;
        line(g, o, "const tide_table *tide_qt = &%s->%s;", x->world, arch_name(g, a));
        line(g, o, "for (uint32_t tide_qc = 0; tide_qc < tide_qt->chunks; tide_qc++) {");
        g->indent++;
        line(g, o, "const uint32_t tide_qn = tide_table_rows(tide_qt, %d, tide_qc);", arch_shift(g, a));
        line(g, o, "const tide_entity *tide_qe = tide_table_column(tide_qt, %s, tide_qc, 0);", arch_columns(g, a));
        for (int c = 0; c < g->prog->components.count; c++) {
            if (!comp_sparse(g, c) || !arch_stores(g, a, c)) continue;
            if (!mask_has(x->selection->query_need_mask, c) && !mask_has(x->selection->query_without_mask, c)) continue;
            line(g, o, "const uint8_t *tide_qpres%d = tide_table_column(tide_qt, %s, tide_qc, %d);", c, arch_columns(g, a),
                 arch_presence(g, a, c));
        }
        bool gathered[64] = {false};
        for (int k = 0; build && k < plan->count; k++) {
            const int b = x->binding[k];
            if (b >= 64 || gathered[b]) continue;
            gathered[b] = true;
            const decl *comp = x->q->query.items[b].type.decl;
            indent(g, o);
            sb_printf(o, "const uint8_t *const tide_xcol%d[%d] = {", b, comp_lanes(comp));
            for (int l = 0; l < comp_lanes(comp); l++) {
                sb_printf(o, "%s(const uint8_t *)tide_table_column(tide_qt, %s, tide_qc, %d)", l ? ", " : "", arch_columns(g, a),
                          arch_column(g, a, comp->index) + l);
            }
            sb_put(o, "};\n");
        }
        line(g, o, "for (uint32_t tide_qi = 0; tide_qi < tide_qn; tide_qi++) {");
        g->indent++;
        line(g, o, "if (tide_row_gone(&tide_qe[tide_qi])) continue;");
        for (int c = 0; c < g->prog->components.count; c++) {
            if (!comp_sparse(g, c) || !arch_stores(g, a, c)) continue;
            if (mask_has(x->selection->query_need_mask, c)) line(g, o, "if (!tide_qpres%d[tide_qi]) continue;", c);
            else if (mask_has(x->selection->query_without_mask, c)) line(g, o, "if (tide_qpres%d[tide_qi]) continue;", c);
        }
        line(g, o, "const uint64_t tide_xord = (uint64_t)%du << 32 | (uint64_t)((tide_qc << %d) + tide_qi);", a, arch_shift(g, a));
        if (build) {
            line(g, o, "struct {");
            g->indent++;
            for (int i = 0; i < x->row->fields.count; i++) {
                const field *f = &x->row->fields.items[i];
                line(g, o, "%s %s;", c_type(f->type), field_cname(f));
            }
            g->indent--;
            line(g, o, "} tide_xrow;");
            for (int i = 0, f = 1; i < x->q->query.count; i++) {
                const param *p = &x->q->query.items[i];
                if (!param_binding(p)) continue;
                const field *value = &x->row->fields.items[f++];
                if (i < 64 && gathered[i]) line(g, o, "tide_gather_%s(&tide_xrow.%s, tide_xcol%d, tide_qi);", type_cname(p->type.decl),
                                                field_cname(value), i);
            }
            gen_ix_keys(g, plan, "tide_xrow", "tide_xkey");
            line(g, o, "tide_index_add(tide_xi, tide_xkey, tide_xord);");
        } else {
            line(g, o, "if (tide_xn < TIDE_IX_LEAST) tide_xall[tide_xn++] = tide_xord;");
        }
        g->indent--;
        line(g, o, "}");
        g->indent--;
        line(g, o, "}");
        g->indent--;
        line(g, o, "}");
    }
}

// What the index of a query's rows is made from, in the order it's checked.
static void gen_ix_query_facts(gen *g, const ix_query *x, const loop_index *plan)
{
    sb *o = &g->c;
    for (int a = 0; a < g->prog->archetypes.count; a++) {
        if (!query_admits(g, x->selection, a)) continue;
        line(g, o, "{");
        g->indent++;
        line(g, o, "const tide_table *tide_qt = &%s->%s;", x->world, arch_name(g, a));
        line(g, o, "tide_ix_num(&tide_xw, tide_qt->chunks);");
        line(g, o, "for (uint32_t tide_qc = 0; tide_qc < tide_qt->chunks; tide_qc++) {");
        g->indent++;
        line(g, o, "tide_page *const *tide_xp = tide_qt->pages + (size_t)tide_qc * (%s)->count;", arch_columns(g, a));
        line(g, o, "tide_ix_num(&tide_xw, tide_table_rows(tide_qt, %d, tide_qc));", arch_shift(g, a));
        line(g, o, "tide_ix_page(&tide_xw, tide_xp[0]);");
        for (int c = 0; c < g->prog->components.count; c++) {
            if (!comp_sparse(g, c) || !arch_stores(g, a, c)) continue;
            if (!mask_has(x->selection->query_need_mask, c) && !mask_has(x->selection->query_without_mask, c)) continue;
            line(g, o, "tide_ix_page(&tide_xw, tide_xp[%d]);", arch_presence(g, a, c));
        }
        for (int k = 0; k < plan->count; k++) {
            const decl *comp = x->q->query.items[x->binding[k]].type.decl;
            for (int l = x->lane_lo[k]; l <= x->lane_hi[k]; l++) {
                line(g, o, "tide_ix_page(&tide_xw, tide_xp[%d]);", arch_column(g, a, comp->index) + l);
            }
        }
        g->indent--;
        line(g, o, "}");
        g->indent--;
        line(g, o, "}");
    }
}

// The site's plan: what its code reads, which a reload that keeps the cache may have changed.
static uint64_t ix_query_plan(const gen *g, const ix_query *x, const loop_index *plan)
{
    sb d = {0};
    sb_printf(&d, "query %s %d", x->world, plan->count);
    for (int a = 0; a < g->prog->archetypes.count; a++) {
        if (!query_admits(g, x->selection, a)) continue;
        sb_printf(&d, " [%d %s %d %d", a, arch_name(g, a), arch_shift(g, a), arch_column_count(g, a));
        for (int c = 0; c < g->prog->components.count; c++) {
            if (!comp_sparse(g, c) || !arch_stores(g, a, c)) continue;
            if (mask_has(x->selection->query_need_mask, c)) sb_printf(&d, " +%d", arch_presence(g, a, c));
            else if (mask_has(x->selection->query_without_mask, c)) sb_printf(&d, " -%d", arch_presence(g, a, c));
        }
        for (int k = 0; k < plan->count; k++) {
            const decl *comp = x->q->query.items[x->binding[k]].type.decl;
            sb_printf(&d, " %s@%d:%d-%d %s", type_cname(comp), arch_column(g, a, comp->index), x->lane_lo[k], x->lane_hi[k],
                      ix_key_text(&plan->keys[k], "r"));
        }
        sb_put(&d, "]");
    }
    return ix_hash(d.data);
}

static void gen_query_fresh(gen *g, const param *q, const char *name);

// A foreach over a world's query with an index: its candidates, then the body for each. With `gather` (a parallel
// loop's, its row type), each candidate row copied onto the end of tide_rows instead, as tide_rown of tide_rowcap,
// for the loop's steps (gen_parallel_query_call). False: it isn't one.
static bool gen_indexed_query(gen *g, const stmt *s, const char *gather)
{
    if (!g->ix_any || !s->index || g->task || s->kind != (gather ? S_PARALLEL : S_FOREACH)) return false;
    const loop_index *plan = s->index;
    if (plan->count < 1 || plan->count > 3) return false;
    ix_query x = {0};
    x.q = s->query;
    x.row = x.q->type.decl;
    const bool borrowed = x.q->function_param;
    const borrow_source source = borrowed ? query_source(g->routine, g->borrow, x.q) : (borrow_source){0};
    x.selection = borrowed ? source_query(source) : x.q;
    if (x.selection->query_rows) return false; // gen_indexed_rows
    x.world = borrowed ? local_cname(g, x.q->name) : x.q->query_local ? "tide_l" : "tide_w";
    for (int k = 0; k < plan->count; k++) {
        int off = -1, size = 0;
        x.binding[k] = ix_key_binding(&x, &plan->keys[k], &off, &size);
        if (x.binding[k] < 0 || x.binding[k] >= 64) return false;
        const decl *comp = x.q->query.items[x.binding[k]].type.decl;
        const int width = comp_width(comp), lanes = comp_lanes(comp);
        if (off < 0 || size <= 0 || off + size > lanes * width) {
            x.lane_lo[k] = 0;
            x.lane_hi[k] = lanes - 1;
        } else {
            x.lane_lo[k] = off / width;
            x.lane_hi[k] = (off + size - 1) / width;
        }
    }
    bool any = false;
    for (int a = 0; a < g->prog->archetypes.count; a++) any |= query_admits(g, x.selection, a);
    if (!any) return false;

    sb *o = &g->c;
    const int site = ix_site(g, s);
    const char *name = local_cname(g, s->name);
    const char *end = made_up(g, "query_end");
    const char *hits = made_up(g, "xh");
    line(g, o, "{ // Through an index: %s (notes/indexed-loops.md)", loop_index_summary(s));
    g->indent++;
    gen_query_fresh(g, x.q, name);
    gen_ix_bounds(g, plan, "tide_xlo", "tide_xhi");
    line(g, o, "tide_index_hits %s;", hits);
    line(g, o, "tide_index_hits_init(&%s);", hits);
    line(g, o, "uint64_t tide_xall[TIDE_IX_LEAST];");
    line(g, o, "const uint64_t *tide_xo = tide_xall;");
    line(g, o, "uint32_t tide_xn = 0u;");
    indent(g, o);
    sb_put(o, "const uint64_t tide_xrows = 0u");
    for (int a = 0; a < g->prog->archetypes.count; a++) {
        if (query_admits(g, x.selection, a)) sb_printf(o, " + %s->%s.count", x.world, arch_name(g, a));
    }
    sb_put(o, ";\n");
    line(g, o, "if (tide_xrows >= TIDE_IX_LEAST) {");
    g->indent++;
    const char *fast = ix_fast(g) && strcmp(x.world, "tide_w") == 0 ? "true" : "false";
    line(g, o, "tide_ix_site *const tide_xs = tide_ix_site_at(%s->tide_index_cache, %du);", x.world, site);
    line(g, o, "const bool tide_xheld = tide_ix_hold(tide_xs, %s->tide_index_cache, %s);", x.world, fast);
    line(g, o, "tide_index *tide_xi = tide_xheld ? tide_xs->ix : NULL;");
    line(g, o, "bool tide_xown = false;");
    line(g, o, "if (!tide_xheld) {");
    g->indent++;
    line(g, o, "tide_ix_walk tide_xw = {0};");
    line(g, o, "if (tide_xs) {");
    g->indent++;
    line(g, o, "tide_xw = tide_ix_walk_begin(tide_xs, %lluull);", (unsigned long long)ix_query_plan(g, &x, plan));
    gen_ix_query_facts(g, &x, plan);
    g->indent--;
    line(g, o, "}");
    line(g, o, "bool tide_xbuild;");
    line(g, o, "tide_xi = tide_ix_take(tide_xs, &tide_xw, &tide_xown, &tide_xbuild);");
    line(g, o, "if (tide_xbuild) {");
    g->indent++;
    line(g, o, "tide_index_begin(tide_xi, %du);", plan->count);
    gen_ix_query_rows(g, &x, plan, true);
    line(g, o, "tide_index_end(tide_xi);");
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_ix_searching(tide_xs, tide_xown, %s->tide_index_cache, %s);", x.world, fast);
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_index_find(tide_xi, tide_xlo, tide_xhi, &%s);", hits);
    line(g, o, "tide_ix_searched(tide_xs, tide_xi, tide_xown, tide_xheld);");
    line(g, o, "tide_xo = %s.items;", hits);
    line(g, o, "tide_xn = %s.count;", hits);
    g->indent--;
    line(g, o, "} else {");
    g->indent++;
    gen_ix_query_rows(g, &x, plan, false);
    g->indent--;
    line(g, o, "}");
    line(g, o, "(void)tide_xlo;");
    line(g, o, "(void)tide_xhi;");
    // Its hits go however the loop is left
    sb cleanup = {0};
    sb_printf(&cleanup, "tide_index_hits_free(&%s);", hits);
    const gen_open open = {.cleanup = cleanup.data};
    vec_push(g->containers, open);
    const int containers = g->containers.count;
    line(g, o, "for (uint32_t tide_xk = 0; tide_xk < tide_xn; tide_xk++) {");
    g->indent++;
    line(g, o, "const uint32_t tide_xa = (uint32_t)(tide_xo[tide_xk] >> 32), tide_xr = (uint32_t)tide_xo[tide_xk];");
    line(g, o, "(void)tide_xr;");
    if (gather) {
        line(g, o, "%s %s;", gather, name);
    } else {
        line(g, o, "struct {");
        g->indent++;
        for (int i = 0; i < x.row->fields.count; i++) {
            const field *f = &x.row->fields.items[i];
            line(g, o, "%s %s;", c_type(f->type), field_cname(f));
        }
        if (x.q->local_use) line(g, o, "uint32_t tide_born;");
        g->indent--;
        line(g, o, "} %s;", name);
    }
    bool first = true;
    for (int a = 0; a < g->prog->archetypes.count; a++) {
        if (!query_admits(g, x.selection, a)) continue;
        line(g, o, "%sif (tide_xa == %du) {", first ? "" : "} else ", a);
        first = false;
        g->indent++;
        line(g, o, "const tide_table *tide_qt = &%s->%s;", x.world, arch_name(g, a));
        line(g, o, "if (tide_xr >= tide_qt->count) continue;");
        line(g, o, "const tide_entity tide_xe = *(const tide_entity *)tide_table_get(tide_qt, %s, tide_xr, 0);", arch_columns(g, a));
        line(g, o, "if (tide_row_gone(&tide_xe)) continue;");
        for (int c = 0; c < g->prog->components.count; c++) {
            if (!comp_sparse(g, c) || !arch_stores(g, a, c)) continue;
            if (mask_has(x.selection->query_need_mask, c)) {
                line(g, o, "if (!*(const uint8_t *)tide_table_get(tide_qt, %s, tide_xr, %d)) continue;", arch_columns(g, a),
                     arch_presence(g, a, c));
            } else if (mask_has(x.selection->query_without_mask, c)) {
                line(g, o, "if (*(const uint8_t *)tide_table_get(tide_qt, %s, tide_xr, %d)) continue;", arch_columns(g, a),
                     arch_presence(g, a, c));
            }
        }
        line(g, o, "%s.%s = tide_xe;", name, field_cname(&x.row->fields.items[0]));
        if (x.q->local_use) line(g, o, "%s.tide_born = tide_entity_born(&%s->entities, tide_xe);", name, x.world);
        for (int i = 0, f = 1; i < x.q->query.count; i++) {
            const param *p = &x.q->query.items[i];
            if (!param_binding(p)) continue;
            const field *value = &x.row->fields.items[f++];
            line(g, o, "tide_table_gather(tide_qt, %s, tide_xr, %d, %du, %du, &%s.%s);", arch_columns(g, a),
                 arch_column(g, a, p->type.decl->index), comp_lanes(p->type.decl), comp_width(p->type.decl), name, field_cname(value));
        }
        g->indent--;
    }
    line(g, o, "} else continue;");
    if (gather) { // Onto the end of the rows the parallel loop goes through
        line(g, o, "if (tide_rown == tide_rowcap) {");
        g->indent++;
        line(g, o, "const uint32_t tide_more = tide_rowcap ? 2u * tide_rowcap : 64u;");
        line(g, o, "tide_rows = tide_realloc(tide_rows, (size_t)tide_rowcap * sizeof *tide_rows, (size_t)tide_more * sizeof *tide_rows);");
        line(g, o, "tide_rowcap = tide_more;");
        g->indent--;
        line(g, o, "}");
        line(g, o, "tide_rows[tide_rown++] = %s;", name);
        g->indent--;
        line(g, o, "}");
        g->containers.count = containers - 1;
        line(g, o, "%s", cleanup.data);
        g->indent--;
        line(g, o, "}");
        return true;
    }
    line(g, o, "(void)%s;", name);
    gen_query_field_refs(g, x.q, name);
    const gen_target loop = {g->frame, containers, true, true, end, false, made_up(g, "next"), false, true};
    vec_push(g->targets, loop);
    gen_body_stmt(g, s->then_stmt);
    const gen_target done = g->targets.items[--g->targets.count];
    if (done.next_used) line(g, o, "%s:;", done.next);
    g->indent--;
    line(g, o, "}");
    if (done.end_used) line(g, o, "%s:;", end);
    g->containers.count = containers - 1;
    line(g, o, "%s", cleanup.data);
    g->indent--;
    line(g, o, "}");
    return true;
}

// ---------------------------------------------------------------------------
// A view's rows (or a borrowed query's, from a view): made once a view's run

static bool gen_indexed_rows(gen *g, const stmt *s)
{
    if (!g->ix_any || !s->index || g->task || s->kind != S_FOREACH) return false;
    const loop_index *plan = s->index;
    if (plan->count < 1 || plan->count > 3) return false;
    const param *q = s->query;
    const bool borrowed = q->function_param;
    const borrow_source source = borrowed ? query_source(g->routine, g->borrow, q) : (borrow_source){0};
    const param *selection = borrowed ? source_query(source) : q;
    const decl *owner = borrowed ? source.owner : g->code;
    if (!selection->query_rows || !owner || !owner->is_view) return false; // A local handler's rows are a tick's: plainly
    const decl *row = q->type.decl;
    sb *o = &g->c;
    const int qi = borrowed ? source.index : query_index(g->code, q);
    sb array = {0}, count = {0};
    if (borrowed) {
        sb_put(&array, local_cname(g, q->name));
        sb_put(&count, borrow_count_name(array.data));
    } else {
        sb_printf(&array, "tide_q%d", qi);
        sb_printf(&count, "tide_q%dn", qi);
    }
    const char *name = local_cname(g, s->name);
    const char *end = made_up(g, "query_end");
    const char *hits = made_up(g, "xh");
    line(g, o, "{ // Through an index: %s (notes/indexed-loops.md)", loop_index_summary(s));
    g->indent++;
    gen_query_fresh(g, q, name);
    gen_ix_bounds(g, plan, "tide_xlo", "tide_xhi");
    line(g, o, "tide_index_hits %s;", hits);
    line(g, o, "tide_index_hits_init(&%s);", hits);
    line(g, o, "const uint64_t *tide_xo = NULL; // NULL: every row");
    line(g, o, "uint32_t tide_xn = %s;", count.data);
    line(g, o, "if (%s >= TIDE_IX_LEAST) {", count.data);
    g->indent++;
    line(g, o, "static tide_ix_site tide_xsite; // Views run one at a time");
    line(g, o, "tide_ix_site *const tide_xs = &tide_xsite;");
    line(g, o, "tide_index_lock(&tide_xs->lock);");
    line(g, o, "tide_ix_walk tide_xw = tide_ix_walk_begin(tide_xs, 1u);");
    line(g, o, "tide_ix_num(&tide_xw, tide_ix_made);");
    line(g, o, "tide_ix_num(&tide_xw, (uint64_t)(uintptr_t)%s);", array.data);
    line(g, o, "tide_ix_num(&tide_xw, %s);", count.data);
    line(g, o, "bool tide_xown, tide_xbuild;");
    line(g, o, "tide_index *const tide_xi = tide_ix_take(tide_xs, &tide_xw, &tide_xown, &tide_xbuild);");
    line(g, o, "if (tide_xbuild) {");
    g->indent++;
    line(g, o, "tide_index_begin(tide_xi, %du);", plan->count);
    line(g, o, "for (uint32_t tide_qi = 0; tide_qi < %s; tide_qi++) {", count.data);
    g->indent++;
    // A row's key: the path from the query's row type, which the rows array's own (a borrowed one's source) shares
    // binding names with only when it's the source's own; else through the source's binding of the same component
    if (!borrowed) {
        line(g, o, "const %s tide_xr = %s[tide_qi];", query_row_type(g->code, qi), array.data);
        gen_ix_keys(g, plan, "tide_xr", "tide_xkey");
    } else {
        // The helper's row, filled from the source's as gen_query_foreach does
        line(g, o, "struct {");
        g->indent++;
        for (int i = 0; i < row->fields.count; i++) line(g, o, "%s %s;", c_type(row->fields.items[i].type), field_cname(&row->fields.items[i]));
        g->indent--;
        line(g, o, "} tide_xrow;");
        const decl *source_row = selection->type.decl;
        for (int i = 0; i < row->fields.count; i++) {
            const field *f = &row->fields.items[i];
            const field *from = &source_row->fields.items[0];
            if (i) for (int k = 1; k < source_row->fields.count; k++) {
                if (f->type.kind == source_row->fields.items[k].type.kind && f->type.decl == source_row->fields.items[k].type.decl) {
                    from = &source_row->fields.items[k];
                    break;
                }
            }
            line(g, o, "tide_xrow.%s = %s[tide_qi].%s;", field_cname(f), array.data, field_cname(from));
        }
        gen_ix_keys(g, plan, "tide_xrow", "tide_xkey");
    }
    line(g, o, "tide_index_add(tide_xi, tide_xkey, tide_qi);");
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_index_end(tide_xi);");
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_ix_searching(tide_xs, tide_xown, NULL, false);");
    line(g, o, "tide_index_find(tide_xi, tide_xlo, tide_xhi, &%s);", hits);
    line(g, o, "tide_ix_searched(tide_xs, tide_xi, tide_xown, false);");
    line(g, o, "tide_xo = %s.items;", hits);
    line(g, o, "tide_xn = %s.count;", hits);
    g->indent--;
    line(g, o, "}");
    line(g, o, "(void)tide_xlo;");
    line(g, o, "(void)tide_xhi;");
    sb cleanup = {0};
    sb_printf(&cleanup, "tide_index_hits_free(&%s);", hits);
    const gen_open open = {.cleanup = cleanup.data};
    vec_push(g->containers, open);
    const int containers = g->containers.count;
    line(g, o, "for (uint32_t tide_xk = 0; tide_xk < tide_xn; tide_xk++) {");
    g->indent++;
    line(g, o, "const uint32_t tide_qi = tide_xo ? (uint32_t)tide_xo[tide_xk] : tide_xk;");
    line(g, o, "if (tide_qi >= %s) continue;", count.data);
    if (!borrowed) {
        line(g, o, "%s %s = %s[tide_qi];", query_row_type(g->code, qi), name, array.data);
    } else {
        line(g, o, "struct {");
        g->indent++;
        for (int i = 0; i < row->fields.count; i++) line(g, o, "%s %s;", c_type(row->fields.items[i].type), field_cname(&row->fields.items[i]));
        if (q->local_use) line(g, o, "uint32_t tide_born;");
        g->indent--;
        line(g, o, "} %s;", name);
        const decl *source_row = selection->type.decl;
        for (int i = 0; i < row->fields.count; i++) {
            const field *f = &row->fields.items[i];
            const field *from = &source_row->fields.items[0];
            if (i) for (int k = 1; k < source_row->fields.count; k++) {
                if (f->type.kind == source_row->fields.items[k].type.kind && f->type.decl == source_row->fields.items[k].type.decl) {
                    from = &source_row->fields.items[k];
                    break;
                }
            }
            line(g, o, "%s.%s = %s[tide_qi].%s;", name, field_cname(f), array.data, field_cname(from));
        }
        if (q->local_use) line(g, o, "%s.tide_born = %s[tide_qi].tide_born;", name, array.data);
    }
    line(g, o, "(void)%s;", name);
    gen_query_field_refs(g, q, name);
    const gen_target loop = {g->frame, containers, true, true, end, false, made_up(g, "next"), false, true};
    vec_push(g->targets, loop);
    gen_body_stmt(g, s->then_stmt);
    const gen_target done = g->targets.items[--g->targets.count];
    if (done.next_used) line(g, o, "%s:;", done.next);
    g->indent--;
    line(g, o, "}");
    if (done.end_used) line(g, o, "%s:;", end);
    g->containers.count = containers - 1;
    line(g, o, "%s", cleanup.data);
    g->indent--;
    line(g, o, "}");
    return true;
}

// ---------------------------------------------------------------------------
// A list in a singleton that a system (or a view) only reads

// The world the list is read from for a site to hold for the code's run: a system's (the match), a view's (the
// local state's, by the frame), or NULL when the list could change while the code runs.
static const char *ix_list_world(const gen *g, const stmt *s)
{
    const decl *code = g->code;
    if (!code || g->routine || g->frame || g->task || g->par_loop || code->kind != DECL_SYSTEM) return NULL;
    if (code->is_handler || code->is_local || code->extern_system) return NULL;
    const expr *root = s->value;
    while (root && root->kind == E_MEMBER && root->object && root->field && !root->property && !root->local_field) root = root->object;
    if (!root || root->kind != E_NAME || root->bind != BIND_PARAM) return NULL;
    const param *p = root->param;
    if (p->mode != PARAM_READ || p->type.kind != TY_SINGLETON || !p->type.decl || p->type.decl->is_local || p->verified) return NULL;
    return code->is_view ? "tide_l" : "tide_w";
}

static bool gen_indexed_list(gen *g, const stmt *s)
{
    if (!g->ix_any || !s->index || g->task || s->kind != S_FOREACH || s->value->type.kind != TY_LIST) return false;
    const loop_index *plan = s->index;
    if (plan->count < 1 || plan->count > 3) return false;
    const char *world = ix_list_world(g, s);
    if (!world) return false;
    sb *o = &g->c;
    const int site = ix_site(g, s);
    const bool borrows = foreach_borrows(g, s, s->type);
    const int k = s->value->type.decl->index;
    const char *hits = made_up(g, "xh");
    const char *end = made_up(g, "loop_end");
    line(g, o, "{ // Through an index: %s (notes/indexed-loops.md)", loop_index_summary(s));
    g->indent++;
    const char *list = foreach_value(g, s->value);
    gen_ix_bounds(g, plan, "tide_xlo", "tide_xhi");
    line(g, o, "tide_index_hits %s;", hits);
    line(g, o, "tide_index_hits_init(&%s);", hits);
    line(g, o, "const int32_t tide_xcount = tide_list_count(%s);", list);
    line(g, o, "const uint64_t *tide_xo = NULL; // NULL: every element");
    line(g, o, "uint32_t tide_xn = (uint32_t)tide_xcount;");
    line(g, o, "tide_ix_site *const tide_xs = tide_xcount >= (int32_t)TIDE_IX_LEAST ? tide_ix_site_at(%s->tide_index_cache, %du) : NULL;",
         world, site);
    const char *fast = ix_fast(g) && strcmp(world, "tide_w") == 0 ? "true" : "false";
    line(g, o, "const bool tide_xheld = tide_ix_hold(tide_xs, %s->tide_index_cache, %s);", world, fast);
    line(g, o, "if (tide_xheld) {");
    g->indent++;
    line(g, o, "tide_index_find(tide_xs->ix, tide_xlo, tide_xhi, &%s);", hits);
    line(g, o, "tide_xo = %s.items;", hits);
    line(g, o, "tide_xn = %s.count;", hits);
    g->indent--;
    line(g, o, "} else if (tide_xs) {");
    g->indent++;
    line(g, o, "tide_ix_walk tide_xw = tide_ix_walk_begin(tide_xs, %lluull);",
         (unsigned long long)ix_hash(c_type(s->type)) ^ (unsigned long long)plan->count);
    line(g, o, "tide_ix_num(&tide_xw, %s->tide_index_cache->epoch);", world);
    if (strcmp(world, "tide_l") == 0) line(g, o, "tide_ix_num(&tide_xw, (uint64_t)(uintptr_t)tide_w);");
    line(g, o, "tide_ix_num(&tide_xw, (uint64_t)(%s).at);", list);
    line(g, o, "tide_ix_num(&tide_xw, (uint64_t)tide_xcount);");
    line(g, o, "bool tide_xown, tide_xbuild;");
    line(g, o, "tide_index *const tide_xi = tide_ix_take(tide_xs, &tide_xw, &tide_xown, &tide_xbuild);");
    line(g, o, "if (tide_xbuild) {");
    g->indent++;
    line(g, o, "tide_index_begin(tide_xi, %du);", plan->count);
    line(g, o, "for (int32_t tide_xe = 0; tide_xe < tide_xcount; tide_xe++) {");
    g->indent++;
    line(g, o, "const %s tide_xel = tide_list%d_get(%s, tide_xe);", elem_vtype(s->type), k, list);
    gen_ix_keys(g, plan, "tide_xel", "tide_xkey");
    line(g, o, "tide_index_add(tide_xi, tide_xkey, (uint64_t)tide_xe);");
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_index_end(tide_xi);");
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_ix_searching(tide_xs, tide_xown, %s->tide_index_cache, %s);", world, fast);
    line(g, o, "tide_index_find(tide_xi, tide_xlo, tide_xhi, &%s);", hits);
    line(g, o, "tide_ix_searched(tide_xs, tide_xi, tide_xown, false);");
    line(g, o, "tide_xo = %s.items;", hits);
    line(g, o, "tide_xn = %s.count;", hits);
    g->indent--;
    line(g, o, "}");
    line(g, o, "(void)tide_xlo;");
    line(g, o, "(void)tide_xhi;");
    sb cleanup = {0};
    sb_printf(&cleanup, "tide_index_hits_free(&%s);", hits);
    const gen_open open = {.cleanup = cleanup.data};
    vec_push(g->containers, open);
    const int containers = g->containers.count;
    line(g, o, "for (uint32_t tide_xk = 0; tide_xk < tide_xn; tide_xk++) {");
    g->indent++;
    const char *index = made_up(g, "i");
    line(g, o, "const int32_t %s = tide_xo ? (int32_t)tide_xo[tide_xk] : (int32_t)tide_xk;", index);
    const bool copy = type_has_list(s->type) && !borrows;
    line(g, o, "const %s %s = %stide_list%d_get(%s, %s)%s;", elem_vtype(s->type), local_cname(g, s->name),
         copy ? element_copy_open(s->type) : "", k, list, index, copy ? ")" : "");
    line(g, o, "(void)%s;", local_cname(g, s->name));
    const gen_target loop = {g->frame, containers, true, true, end, false, made_up(g, "next"), false, false};
    vec_push(g->targets, loop);
    gen_body_stmt(g, s->then_stmt);
    const gen_target done = g->targets.items[--g->targets.count];
    if (done.next_used) line(g, o, "%s:;", done.next);
    g->indent--;
    line(g, o, "}");
    if (done.end_used) line(g, o, "%s:;", end);
    g->containers.count = containers - 1;
    line(g, o, "%s", cleanup.data);
    g->indent--;
    line(g, o, "}");
    return true;
}

// ---------------------------------------------------------------------------
// A parallel loop over a list: its steps for the elements the index gives

static bool ix_par_list(const gen *g, const stmt *s)
{
    return g->ix_any && s->kind == S_PARALLEL && s->index && s->index->count >= 1 && s->index->count <= 3 && s->value
        && s->value->type.kind == TY_LIST;
}

// Before the steps: tide_xm, a bit for each element the index gives (NULL: every element, for a short list), of
// tide_xmn. The steps change elements, so the index is the loop's own, made from the list as the loop starts.
static void gen_ix_par_marks(gen *g, const stmt *s)
{
    sb *o = &g->c;
    const loop_index *plan = s->index;
    const int k = s->value->type.decl->index;
    const char *list = expr_text(g, s->value);
    line(g, o, "uint64_t *tide_xm = NULL;");
    line(g, o, "int32_t tide_xmn = 0;");
    line(g, o, "{ // Through an index: %s (notes/indexed-loops.md)", loop_index_summary(s));
    g->indent++;
    gen_ix_bounds(g, plan, "tide_xlo", "tide_xhi");
    line(g, o, "const int32_t tide_xcount = tide_list_count(%s);", list);
    line(g, o, "if (tide_xcount >= (int32_t)TIDE_IX_LEAST) {");
    g->indent++;
    line(g, o, "tide_index *const tide_xi = tide_index_make();");
    line(g, o, "tide_index_begin(tide_xi, %du);", plan->count);
    line(g, o, "for (int32_t tide_xe = 0; tide_xe < tide_xcount; tide_xe++) {");
    g->indent++;
    line(g, o, "const %s tide_xel = tide_list%d_get(%s, tide_xe);", elem_vtype(list_elem(s->value->type.decl)), k, list);
    gen_ix_keys(g, plan, "tide_xel", "tide_xkey");
    line(g, o, "tide_index_add(tide_xi, tide_xkey, (uint64_t)tide_xe);");
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_index_end(tide_xi);");
    line(g, o, "tide_index_hits tide_xh;");
    line(g, o, "tide_index_hits_init(&tide_xh);");
    line(g, o, "tide_index_find(tide_xi, tide_xlo, tide_xhi, &tide_xh);");
    line(g, o, "tide_xm = tide_alloc_zeroed(((size_t)tide_xcount + 63u) / 64u, sizeof *tide_xm);");
    line(g, o, "for (uint32_t tide_xk = 0; tide_xk < tide_xh.count; tide_xk++) {");
    g->indent++;
    line(g, o, "const uint64_t tide_xo = tide_xh.items[tide_xk];");
    line(g, o, "if (tide_xo < (uint64_t)tide_xcount) tide_xm[tide_xo >> 6] |= (uint64_t)1 << (tide_xo & 63u);");
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_xmn = tide_xcount;");
    line(g, o, "tide_index_hits_free(&tide_xh);");
    line(g, o, "tide_index_free(tide_xi);");
    g->indent--;
    line(g, o, "}");
    line(g, o, "(void)tide_xlo;");
    line(g, o, "(void)tide_xhi;");
    g->indent--;
    line(g, o, "}");
}
