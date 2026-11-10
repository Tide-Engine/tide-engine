// Local journals retain only heap fields that local code can observe. Passing
// a binding to a synchronous helper propagates that helper's selected paths;
// an ordinary value copy consumes the whole value. This is a conservative
// static analysis: a field inside a conditional is still a possible read.
// The expressions and statements one routine's walk has been through: a set, as a big game's routine has tens of
// thousands and each asks. One serves every routine: `walk` says whose an entry is, so starting a routine's walk
// empties it.
typedef struct heard_set {
    const void **items;
    uint32_t *walks;
    uint32_t mask, count, walk;
} heard_set;

typedef struct heard_walk {
    gen *g;
    const decl *routine;
    bool changed;
    heard_set *seen;
} heard_walk;

static uint32_t heard_place(const void *p, const uint32_t mask)
{
    uint64_t n = (uint64_t)(uintptr_t)p;
    n ^= n >> 33;
    n *= UINT64_C(0xff51afd7ed558ccd);
    n ^= n >> 33;
    return (uint32_t)n & mask;
}

// The first of `source`'s reads in g->heard_reads, or -1: g->heard_next has the one after each, in the order they
// were added. Every expression of the game asks what a source reads, so the reads are found by their source.
static int heard_first(const gen *g, const void *source)
{
    const heard_sources *s = &g->heard_by;
    if (!s->keys || !source) return -1;
    uint32_t at = heard_place(source, s->mask);
    while (s->keys[at] && s->keys[at] != source) at = (at + 1u) & s->mask;
    return s->keys[at] ? s->first[at] : -1;
}

static bool heard_add_read(gen *g, const void *source, const char *path)
{
    if (!source) return false; // Nothing reads through no source
    for (int i = heard_first(g, source); i >= 0; i = g->heard_next.items[i]) {
        if (!strcmp(g->heard_reads.items[i].path, path)) return false;
    }
    const int index = g->heard_reads.count;
    vec_push(g->heard_reads, ((heard_read){source, path}));
    vec_push(g->heard_next, -1);
    heard_sources *s = &g->heard_by;
    if (!s->keys || (uint64_t)(s->count + 1u) * 2u > s->mask) {
        const uint32_t mask = s->keys ? s->mask * 2u + 1u : 255u;
        const void **keys = arena_alloc(((size_t)mask + 1u) * sizeof *keys);
        int *first = arena_alloc(((size_t)mask + 1u) * sizeof *first);
        int *last = arena_alloc(((size_t)mask + 1u) * sizeof *last);
        memset(keys, 0, ((size_t)mask + 1u) * sizeof *keys);
        for (uint32_t i = 0; s->keys && i <= s->mask; i++) {
            if (!s->keys[i]) continue;
            uint32_t at = heard_place(s->keys[i], mask);
            while (keys[at]) at = (at + 1u) & mask;
            keys[at] = s->keys[i];
            first[at] = s->first[i];
            last[at] = s->last[i];
        }
        s->keys = keys;
        s->first = first;
        s->last = last;
        s->mask = mask;
    }
    uint32_t at = heard_place(source, s->mask);
    while (s->keys[at] && s->keys[at] != source) at = (at + 1u) & s->mask;
    if (s->keys[at]) g->heard_next.items[s->last[at]] = index;
    else {
        s->keys[at] = source;
        s->first[at] = index;
        s->count++;
    }
    s->last[at] = index;
    return true;
}

static const char *heard_member_path(const char *member, const char *rest)
{
    sb path = {0};
    sb_printf(&path, "%s%s%s", member, *rest ? "." : "", rest);
    return path.data;
}

static bool heard_container(const type t)
{
    return t.kind == TY_STRING || t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET || t.kind == TY_GRID;
}

static bool heard_access(heard_walk *w, const expr *e, const char *path)
{
    if (!e || e->local_field) return false;
    if (heard_container(e->type)) path = ""; // An element read needs its enclosing container.
    const param *binding = query_component(e);
    if (binding) { w->changed |= heard_add_read(w->g, binding, path); return true; }
    if (e->kind == E_NAME && e->bind == BIND_PARAM) {
        w->changed |= heard_add_read(w->g, e->param, path);
        return true;
    }
    if (e->kind == E_THIS || (e->kind == E_NAME && e->bind == BIND_FIELD)) {
        if (w->routine->owner) {
            if (e->kind == E_NAME) path = heard_member_path(field_cname(e->field), path);
            w->changed |= heard_add_read(w->g, w->routine, path);
        }
        return true;
    }
    if (e->kind == E_MEMBER) {
        if (e->field) path = heard_member_path(field_cname(e->field), path);
        return heard_access(w, e->object, path);
    }
    if (e->kind == E_INDEX) {
        if (e->object->type.kind == TY_ARRAY) path = heard_member_path("at[*]", path);
        return heard_access(w, e->object, path);
    }
    return false;
}

static bool heard_path_reads(const char *read, const char *leaf)
{
    if (!*read) return true;
    while (*read && *leaf) {
        if (!strncmp(read, "[*]", 3) && *leaf == '[') {
            read += 3;
            do { leaf++; } while (*leaf && *leaf != ']');
            if (*leaf) leaf++;
        } else if (*read++ != *leaf++) return false;
    }
    return !*read && (!*leaf || *leaf == '.' || *leaf == '[');
}

static bool heard_reads(const gen *g, const void *source, const char *path)
{
    for (int i = heard_first(g, source); i >= 0; i = g->heard_next.items[i]) {
        if (heard_path_reads(g->heard_reads.items[i].path, path)) return true;
    }
    return false;
}

static void heard_read_expr(heard_walk *w, const expr *e, bool select);
static void heard_read_stmt(heard_walk *w, const stmt *s);

static bool heard_seen(heard_walk *w, const void *p)
{
    if (!p) return true;
    heard_set *set = w->seen;
    if (!set->items || (uint64_t)(set->count + 1u) * 2u > set->mask) {
        // Room for twice this walk's entries; those of earlier walks are left behind
        const uint32_t mask = set->items ? set->mask * 2u + 1u : 1023u;
        const void **items = arena_alloc(((size_t)mask + 1u) * sizeof *items);
        uint32_t *walks = arena_alloc(((size_t)mask + 1u) * sizeof *walks);
        memset(items, 0, ((size_t)mask + 1u) * sizeof *items);
        for (uint32_t i = 0; set->items && i <= set->mask; i++) {
            if (!set->items[i] || set->walks[i] != set->walk) continue;
            uint32_t at = heard_place(set->items[i], mask);
            while (items[at]) at = (at + 1u) & mask;
            items[at] = set->items[i];
            walks[at] = set->walk;
        }
        set->items = items;
        set->walks = walks;
        set->mask = mask;
    }
    // An earlier walk's entry is a free place: nothing of this walk was put past it before it was this walk's
    uint32_t at = heard_place(p, set->mask);
    while (set->items[at] && set->walks[at] == set->walk) {
        if (set->items[at] == p) return true;
        at = (at + 1u) & set->mask;
    }
    set->items[at] = p;
    set->walks[at] = set->walk;
    set->count++;
    return false;
}

static void heard_forward(heard_walk *w, const expr *arg, const void *source)
{
    // Appending to heard_reads can reallocate its vector, and the source's later reads are after `count`.
    const int count = w->g->heard_reads.count;
    for (int i = heard_first(w->g, source); i >= 0 && i < count; i = w->g->heard_next.items[i]) {
        heard_access(w, arg, w->g->heard_reads.items[i].path);
    }
}

static void heard_read_expr(heard_walk *w, const expr *e, const bool select)
{
    if (!e) return;
    if (select) heard_access(w, e, "");
    if (heard_seen(w, e)) return;
    const decl *to = e->method;
    const bool user = to && !to->builtin && !to->is_extern;
    if (user && (e->kind == E_CALL || e->kind == E_METHOD)) {
        if (e->kind == E_METHOD && !to->is_operator) heard_forward(w, e->object, to);
        else if (to->owner && !to->is_operator && w->routine->owner) {
            const int count = w->g->heard_reads.count;
            for (int i = heard_first(w->g, to); i >= 0 && i < count; i = w->g->heard_next.items[i]) {
                w->changed |= heard_add_read(w->g, w->routine, w->g->heard_reads.items[i].path);
            }
        }
        for (int i = 0; i < e->args.count && i < to->params.count; i++) {
            const param *p = &to->params.items[i];
            const expr *arg = e->args.items[i];
            if (p->type.kind == TY_QUERY && arg->param) {
                for (int n = 0; n < p->query.count; n++) {
                    const param *need = &p->query.items[n];
                    if (!param_binding(need)) continue;
                    for (int k = 0; k < arg->param->query.count; k++) {
                        const param *given = &arg->param->query.items[k];
                        if (!param_binding(given) || need->type.kind != given->type.kind || need->type.decl != given->type.decl) continue;
                        const int count = w->g->heard_reads.count;
                        for (int j = heard_first(w->g, need); j >= 0 && j < count; j = w->g->heard_next.items[j]) {
                            w->changed |= heard_add_read(w->g, given, w->g->heard_reads.items[j].path);
                        }
                    }
                }
            } else heard_forward(w, arg, p);
        }
    } else if (user && (e->kind == E_BINARY || e->kind == E_UNARY)) {
        if (to->params.count) heard_forward(w, e->lhs, &to->params.items[0]);
        if (to->params.count > 1) heard_forward(w, e->rhs, &to->params.items[1]);
    }
    const bool member = e->kind == E_MEMBER || e->kind == E_INDEX;
    heard_read_expr(w, e->object, !member && !user);
    heard_read_expr(w, e->lhs, !user);
    heard_read_expr(w, e->rhs, !user);
    heard_read_expr(w, e->cond, true);
    heard_read_expr(w, e->values, true);
    heard_read_expr(w, e->mesh_lod, true);
    heard_read_expr(w, e->skin_mesh, true);
    heard_read_expr(w, e->skin_pose, true);
    for (int i = 0; i < e->args.count; i++) heard_read_expr(w, e->args.items[i], !user);
    for (int i = 0; i < e->inits.count; i++) heard_read_expr(w, e->inits.items[i].value, true);
    heard_read_stmt(w, e->step);
    heard_read_stmt(w, e->block);
}

static void heard_read_stmt(heard_walk *w, const stmt *s)
{
    if (heard_seen(w, s)) return;
    heard_read_expr(w, s->value, true);
    heard_read_expr(w, s->target, true);
    heard_read_expr(w, s->cond, true);
    heard_read_expr(w, s->by, true);
    heard_read_expr(w, s->offset, true);
    heard_read_stmt(w, s->then_stmt);
    heard_read_stmt(w, s->else_stmt);
    heard_read_stmt(w, s->init);
    heard_read_stmt(w, s->step);
    for (int i = 0; i < s->stmts.count; i++) heard_read_stmt(w, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++) heard_read_expr(w, s->cases.items[i].labels.items[k], true);
        for (int k = 0; k < s->cases.items[i].body.count; k++) heard_read_stmt(w, s->cases.items[i].body.items[k]);
    }
}

static void heard_read_routine(gen *g, heard_set *seen, const decl *d, bool *changed)
{
    seen->walk++; // This routine's walk: the set is empty
    seen->count = 0;
    heard_walk w = {.g = g, .routine = d, .seen = seen};
    heard_read_stmt(&w, d->body);
    *changed |= w.changed;
    for (int i = 0; i < d->methods.count; i++) heard_read_routine(g, seen, d->methods.items[i], changed);
}

static void collect_heard_reads(gen *g)
{
    if (!program_hears(g->prog)) return;
    // tidec --embed's taps give another program the whole event
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *d = g->prog->decls.items[i];
        if (d->embed_tap && d->params.count) heard_add_read(g, &d->params.items[0], "");
    }
    heard_set seen = {0};
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < g->prog->decls.count; i++) heard_read_routine(g, &seen, g->prog->decls.items[i], &changed);
    }
}
