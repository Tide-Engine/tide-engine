// Borrowed queries have static sources. Specialization keeps their existing
// traversal and timing, without materializing a collection or runtime registry.
static int query_index(const decl *owner, const param *p)
{
    for (int i = 0; i < owner->params.count; i++)
        if (owner->params.items[i].mode == PARAM_QUERY && str_eq(owner->params.items[i].name, p->name)) return i;
    abort();
}

static const param *source_query(const borrow_source source)
{
    return &source.owner->params.items[source.index];
}

static bool same_borrow_source(const borrow_source a, const borrow_source b)
{
    if (a.owner == b.owner && a.index == b.index) return true;
    const param *p = source_query(a), *q = source_query(b);
    // Live sources with the same selection share a specialization. Recorded
    // row sources retain their distinct generated layouts and timing owners.
    return !p->query_rows && !q->query_rows && p->query_local == q->query_local
        && mask_eq(p->query_need_mask, q->query_need_mask) && mask_eq(p->query_without_mask, q->query_without_mask);
}

static borrow_source query_source(const decl *owner, const borrow_variant *variant, const param *p)
{
    const int index = query_index(owner, p);
    if (!p->function_param) return (borrow_source){owner, index};
    if (!variant || variant->routine != owner) abort();
    return variant->sources[index];
}

static borrow_variant *borrow_call(gen *g, const decl *owner, const borrow_variant *variant, const expr *e)
{
    const decl *fn = e->method;
    borrow_source *sources = arena_alloc((size_t)fn->params.count * sizeof *sources);
    for (int i = 0; i < fn->params.count; i++)
        if (fn->params.items[i].mode == PARAM_QUERY) sources[i] = query_source(owner, variant, e->args.items[i]->param);
    for (int i = 0; i < g->borrows.count; i++) {
        borrow_variant *v = g->borrows.items[i];
        if (v->routine != fn) continue;
        bool same = true;
        for (int k = 0; k < fn->params.count; k++) {
            if (fn->params.items[k].mode == PARAM_QUERY)
                same &= same_borrow_source(v->sources[k], sources[k]);
        }
        if (same) return v;
    }
    borrow_variant *v = arena_alloc(sizeof *v);
    *v = (borrow_variant){fn, sources, g->borrows.count};
    vec_push(g->borrows, v);
    return v;
}

static void collect_borrow_stmt(gen *g, const decl *owner, const borrow_variant *variant, const stmt *s);
static void collect_borrow_expr(gen *g, const decl *owner, const borrow_variant *variant, const expr *e)
{
    if (!e) return;
    if (e->method && e->method->borrows_queries) borrow_call(g, owner, variant, e);
    collect_borrow_expr(g, owner, variant, e->object);
    collect_borrow_expr(g, owner, variant, e->lhs);
    collect_borrow_expr(g, owner, variant, e->rhs);
    collect_borrow_expr(g, owner, variant, e->cond);
    collect_borrow_expr(g, owner, variant, e->skin_mesh);
    collect_borrow_expr(g, owner, variant, e->skin_pose);
    collect_borrow_expr(g, owner, variant, e->values);
    for (int i = 0; i < e->args.count; i++) collect_borrow_expr(g, owner, variant, e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) {
        collect_borrow_expr(g, owner, variant, e->inits.items[i].value);
    }
    collect_borrow_stmt(g, owner, variant, e->block);
    collect_borrow_stmt(g, owner, variant, e->step);
    collect_borrow_stmt(g, owner, variant, e->binding);
    collect_borrow_stmt(g, owner, variant, e->out_local);
}

static void collect_borrow_stmt(gen *g, const decl *owner, const borrow_variant *variant, const stmt *s)
{
    if (!s) return;
    collect_borrow_expr(g, owner, variant, s->cond);
    collect_borrow_expr(g, owner, variant, s->value);
    collect_borrow_expr(g, owner, variant, s->target);
    collect_borrow_expr(g, owner, variant, s->by);
    collect_borrow_expr(g, owner, variant, s->offset);
    collect_borrow_stmt(g, owner, variant, s->init);
    collect_borrow_stmt(g, owner, variant, s->step);
    collect_borrow_stmt(g, owner, variant, s->then_stmt);
    collect_borrow_stmt(g, owner, variant, s->else_stmt);
    for (int i = 0; i < s->stmts.count; i++) collect_borrow_stmt(g, owner, variant, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        const switch_case *part = &s->cases.items[i];
        for (int k = 0; k < part->labels.count; k++) collect_borrow_expr(g, owner, variant, part->labels.items[k]);
        for (int k = 0; k < part->body.count; k++) collect_borrow_stmt(g, owner, variant, part->body.items[k]);
    }
}

static void collect_borrows(gen *g)
{
    bool any = false;
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *d = g->prog->decls.items[i];
        any |= d->borrows_queries;
        for (int k = 0; k < d->methods.count; k++) any |= d->methods.items[k]->borrows_queries;
    }
    if (!any) return;
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *d = g->prog->decls.items[i];
        if (d->kind == DECL_SYSTEM) collect_borrow_stmt(g, d, NULL, d->body);
    }
    // Appended variants cover forwarding chains and recursion once per source.
    for (int i = 0; i < g->borrows.count; i++) {
        const borrow_variant *v = g->borrows.items[i];
        collect_borrow_stmt(g, v->routine, v, v->routine->body);
    }
}

static const char *borrow_cname(const borrow_variant *v)
{
    sb name = {0};
    sb_printf(&name, "tide_borrow%d_%s", v->index, routine_cname(v->routine));
    return name.data;
}

static const char *query_row_type(const decl *sys, int i);
static const char *borrow_count_name(const char *name)
{
    sb out = {0};
    sb_printf(&out, "tide_query_count_%s", name);
    return out.data;
}

static const char *borrow_arg_name(gen *g, const expr *arg, const bool count)
{
    const param *p = arg->param;
    const decl *owner = p->function_param ? g->routine : g->code;
    const borrow_source source = query_source(owner, g->borrow, p);
    const param *q = source_query(source);
    sb name = {0};
    if (p->function_param) sb_put(&name, count ? borrow_count_name(local_cname(g, p->name)) : local_cname(g, p->name));
    else if (q->query_rows) sb_printf(&name, "tide_q%d%s", source.index, count ? "n" : "");
    else sb_put(&name, q->query_local ? "tide_l" : "tide_w");
    return name.data;
}

static void gen_borrow_param(gen *g, sb *o, const param *p, const borrow_source source)
{
    const param *q = source_query(source);
    const char *name = local_cname(g, p->name);
    if (q->query_rows) sb_printf(o, "const %s *%s, const uint32_t %s", query_row_type(source.owner, source.index), name, borrow_count_name(name));
    else sb_printf(o, "const %s *%s", world_type(q->query_local), name);
    if (p->owner_world && !q->query_local)
        sb_printf(o, ", const tide_world *%s, const tide_world *%s", owner_name(g, p, "predicted"), owner_name(g, p, "verified"));
}

static const char *borrow_owner_world(gen *g, const expr *arg, const bool verified)
{
    const param *p = arg->param;
    if (p->function_param) return owner_name(g, p, verified ? "verified" : "predicted");
    if (!verified) return "tide_w";
    for (int i = 0; i < p->query.count; i++) if (p->query.items[i].verified) return "tide_verified";
    return "NULL";
}

static void gen_borrow_arg(gen *g, sb *o, const expr *arg, const bool owner_world)
{
    const param *p = arg->param;
    const decl *owner = p->function_param ? g->routine : g->code;
    const borrow_source source = query_source(owner, g->borrow, p);
    const param *q = source_query(source);
    sb_put(o, borrow_arg_name(g, arg, false));
    if (q->query_rows) sb_printf(o, ", %s", borrow_arg_name(g, arg, true));
    if (owner_world && !q->query_local) {
        sb_printf(o, ", %s, %s", borrow_owner_world(g, arg, false), borrow_owner_world(g, arg, true));
    }
}
