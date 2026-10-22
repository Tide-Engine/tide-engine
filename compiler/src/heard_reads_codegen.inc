// Local journals retain only heap fields that local code can observe. Passing
// a binding to a synchronous helper propagates that helper's selected paths;
// an ordinary value copy consumes the whole value. This is a conservative
// static analysis: a field inside a conditional is still a possible read.
typedef struct heard_walk {
    gen *g;
    const decl *routine;
    bool changed;
    VEC(const void *) seen;
} heard_walk;

static bool heard_add_read(gen *g, const void *source, const char *path)
{
    for (int i = 0; i < g->heard_reads.count; i++) {
        const heard_read *r = &g->heard_reads.items[i];
        if (r->source == source && !strcmp(r->path, path)) return false;
    }
    vec_push(g->heard_reads, ((heard_read){source, path}));
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
    for (int i = 0; i < g->heard_reads.count; i++) {
        const heard_read *r = &g->heard_reads.items[i];
        if (r->source == source && heard_path_reads(r->path, path)) return true;
    }
    return false;
}

static void heard_read_expr(heard_walk *w, const expr *e, bool select);
static void heard_read_stmt(heard_walk *w, const stmt *s);

static bool heard_seen(heard_walk *w, const void *p)
{
    if (!p) return true;
    for (int i = 0; i < w->seen.count; i++) if (w->seen.items[i] == p) return true;
    vec_push(w->seen, p);
    return false;
}

static void heard_forward(heard_walk *w, const expr *arg, const void *source)
{
    // Appending to heard_reads can reallocate its vector.
    const int count = w->g->heard_reads.count;
    for (int i = 0; i < count; i++) {
        const heard_read r = w->g->heard_reads.items[i];
        if (r.source == source) heard_access(w, arg, r.path);
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
            for (int i = 0; i < count; i++) {
                const heard_read r = w->g->heard_reads.items[i];
                if (r.source == to) w->changed |= heard_add_read(w->g, w->routine, r.path);
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
                        for (int j = 0; j < count; j++) {
                            const heard_read r = w->g->heard_reads.items[j];
                            if (r.source == need) w->changed |= heard_add_read(w->g, given, r.path);
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

static void heard_read_routine(gen *g, const decl *d, bool *changed)
{
    heard_walk w = {.g = g, .routine = d};
    heard_read_stmt(&w, d->body);
    *changed |= w.changed;
    for (int i = 0; i < d->methods.count; i++) heard_read_routine(g, d->methods.items[i], changed);
}

static void collect_heard_reads(gen *g)
{
    if (!program_hears(g->prog)) return;
    // tidec --embed's taps give another program the whole event
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *d = g->prog->decls.items[i];
        if (d->embed_tap && d->params.count) heard_add_read(g, &d->params.items[0], "");
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < g->prog->decls.count; i++) heard_read_routine(g, g->prog->decls.items[i], &changed);
    }
}
