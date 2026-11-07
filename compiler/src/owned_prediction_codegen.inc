// Prediction.Owned's conservative, directed data dependency closure. The
// schedule still orders every selected system; ordering alone is not a root.
typedef struct owned_uses {
    VEC(const decl *) reads;
    VEC(const decl *) writes;
    VEC(const decl *) visited;
    bool writes_all;
    bool spawns_owned;
} owned_uses;
typedef struct owned_plan { owned_uses *uses; } owned_plan;

static void owned_read(owned_uses *u, const decl *d, bool write)
{
    if (!d || d->is_local || (d->kind != DECL_COMPONENT && d->kind != DECL_SINGLETON)) return;
    bool found = false;
    for (int i = 0; i < u->reads.count; i++) found |= u->reads.items[i] == d;
    if (!found) vec_push(u->reads, d);
    if (!write) return;
    found = false;
    for (int i = 0; i < u->writes.count; i++) found |= u->writes.items[i] == d;
    if (!found) vec_push(u->writes, d);
}
static void owned_code(const program *p, owned_uses *u, const decl *d);
static void owned_stmt(const program *p, owned_uses *u, const stmt *s);
static void owned_event(const program *p, owned_uses *u, const decl *event)
{
    for (int i = 0; i < p->handlers.count; i++) {
        const decl *h = p->handlers.items[i];
        if (!h->is_local && h->event == event) owned_code(p, u, h);
    }
}
static void owned_expr(const program *p, owned_uses *u, const expr *e)
{
    if (!e) return;
    if (!e->local_world) {
        if (e->call == CALL_SEND) owned_event(p, u, e->type_decl);
        if (e->call == CALL_SPAWN) {
            if (p->owner && p->owner->index >= 0 && mask_has(e->spawn_mask, p->owner->index)) u->spawns_owned = true;
            for (int c = 0; c < p->components.count; c++) if (mask_has(e->spawn_mask, c)) owned_read(u, p->components.items[c], true);
            for (int h = 0; h < p->handlers.count; h++) {
                const decl *handler = p->handlers.items[h];
                if (!handler->is_local && handler->event == p->spawned && mask_subset(handler->need_mask, e->spawn_mask)
                    && !mask_intersects(handler->without_mask, e->spawn_mask)) owned_code(p, u, handler);
            }
        }
        if (e->call == CALL_ADD || e->call == CALL_REMOVE || e->call == CALL_PARENT) {
            // Add/Remove lifecycle and dynamically selected entity targets can
            // change later row admission. Keep these writers conservatively.
            u->writes_all = true;
            for (int h = 0; h < p->handlers.count; h++) {
                const decl *handler = p->handlers.items[h];
                if (!handler->is_local && handler->event->builtin) owned_code(p, u, handler);
            }
        }
        if (e->call == CALL_DESTROY || e->call == CALL_LOAD || e->call == CALL_UNLOAD) {
            u->writes_all = true;
            owned_event(p, u, p->destroyed);
            if (e->call != CALL_DESTROY) owned_event(p, u, p->spawned);
        }
    }
    if (e->method && !e->method->is_extern) owned_code(p, u, e->method);
    const decl *callable = e->object ? e->object->type.decl : NULL;
    for (int i = 0; callable && i < callable->call_targets.count; i++) owned_code(p, u, callable->call_targets.items[i]);
    owned_expr(p, u, e->object); owned_expr(p, u, e->lhs); owned_expr(p, u, e->rhs); owned_expr(p, u, e->cond);
    for (int i = 0; i < e->args.count; i++) owned_expr(p, u, e->args.items[i]);
    for (int i = 0; i < e->inits.count; i++) owned_expr(p, u, e->inits.items[i].value);
    owned_stmt(p, u, e->block);
    owned_expr(p, u, e->lambda_body);
}
static void owned_stmt(const program *p, owned_uses *u, const stmt *s)
{
    if (!s) return;
    owned_expr(p, u, s->cond); owned_expr(p, u, s->value); owned_expr(p, u, s->target);
    owned_expr(p, u, s->property_set); owned_expr(p, u, s->by); owned_expr(p, u, s->offset);
    if (s->operator_decl) owned_code(p, u, s->operator_decl);
    owned_stmt(p, u, s->init); owned_stmt(p, u, s->step); owned_stmt(p, u, s->then_stmt); owned_stmt(p, u, s->else_stmt);
    for (int i = 0; i < s->stmts.count; i++) owned_stmt(p, u, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        for (int k = 0; k < s->cases.items[i].labels.count; k++) owned_expr(p, u, s->cases.items[i].labels.items[k]);
        for (int k = 0; k < s->cases.items[i].body.count; k++) owned_stmt(p, u, s->cases.items[i].body.items[k]);
    }
}
static void owned_code(const program *p, owned_uses *u, const decl *d)
{
    for (int i = 0; i < u->visited.count; i++) if (u->visited.items[i] == d) return;
    vec_push(u->visited, d);
    if (d->per_entity) {
        owned_read(u, p->owner, false); // Input routing and predicted membership.
        owned_read(u, p->hierarchy[0], false);
    }
    for (int i = 0; i < d->params.count; i++) {
        const param *a = &d->params.items[i];
        if (a->mode == PARAM_READ || a->mode == PARAM_MUT) owned_read(u, a->type.decl, a->mode == PARAM_MUT);
    }
    for (int i = 0; i < d->world_uses.count; i++) owned_read(u, d->world_uses.items[i].component, d->world_uses.items[i].writes);
    // Task registries are not yet partitioned by causal ownership.
    if (d->starts_tasks || d->task_access[0]) u->writes_all = true;
    owned_stmt(p, u, d->body);
}
static owned_plan *owned_plan_of(gen *g)
{
    if (g->owned_plan) return g->owned_plan;
    owned_plan *plan = NEW(owned_plan);
    const size_t count = g->prog->systems.count ? (size_t)g->prog->systems.count : 1u;
    plan->uses = arena_alloc(sizeof(owned_uses) * count);
    memset(plan->uses, 0, sizeof(owned_uses) * count);
    for (int i = 0; i < g->prog->systems.count; i++) owned_code(g->prog, &plan->uses[i], g->prog->systems.items[i]);
    return g->owned_plan = plan;
}
static bool owned_produces(const owned_uses *producer, const owned_uses *consumer)
{
    if (producer->writes_all && consumer->reads.count) return true;
    for (int i = 0; i < producer->writes.count; i++) for (int j = 0; j < consumer->reads.count; j++)
        if (producer->writes.items[i] == consumer->reads.items[j]) return true;
    return false;
}
static void gen_owned_selection(gen *g)
{
    const program *p = g->prog;
    if (!p->owned_prediction || !p->systems.count) return;
    owned_plan *plan = owned_plan_of(g);
    sb *o = &g->c;
    sb_printf(o, "static void tide_owned_select(tide_world *w)\n{\n    bool eligible[%d] = {0};\n    (void)eligible;\n    memset(w->tide_owned_selected, 0, sizeof w->tide_owned_selected);\n", p->systems.count);
    sb_printf(o, "    bool has_near[%d] = {0}, has_far[%d] = {0};\n    bool any_owned = false;\n    (void)has_near; (void)has_far; (void)any_owned;\n", p->archetypes.count ? p->archetypes.count : 1, p->archetypes.count ? p->archetypes.count : 1);
    for (int a = 0; a < p->archetypes.count; a++) {
        if (arch_local(g, a) || !arch_stores(g, a, p->predict_far->index)) continue;
        sb_printf(o, "    { const tide_table *t = &w->%s;\n        for (uint32_t chunk = 0; !(has_near[%d] && has_far[%d]) && chunk < t->chunks; chunk++) {\n"
                     "            const uint32_t n = tide_table_rows(t, %d, chunk);\n"
                     "            const tide_entity *e = tide_table_column(t, %s, chunk, 0);\n"
                     "            const uint8_t *far = tide_table_column(t, %s, chunk, %d);\n"
                     "            for (uint32_t row = 0; row < n; row++) if (!tide_row_gone(&e[row])) { if (far[row]) has_far[%d] = true; else has_near[%d] = true; }\n"
                     "        }\n    }\n", arch_name(g, a), a, a, arch_shift(g, a), arch_columns(g, a), arch_columns(g, a), arch_presence(g, a, p->predict_far->index), a, a);
        sb_printf(o, "    any_owned |= has_near[%d];\n", a);
    }
    for (int i = 0; i < p->systems.count; i++) {
        const decl *s = p->systems.items[i];
        if (!s->per_entity) {
            sb_printf(o, "    eligible[%d] = true;\n", i);
            if (s->task_access[0]) sb_printf(o, "    w->tide_owned_selected[%d] = true; // Shared task registry\n", i);
            if (plan->uses[i].spawns_owned) sb_printf(o, "    w->tide_owned_selected[%d] |= any_owned; // May create an owned entity\n", i);
            continue;
        }
        const bool wants_far = mask_has(s->need_mask, p->predict_far->index);
        for (int a = 0; a < p->archetypes.count; a++) {
            if (!runs_on(g, s, a)) continue;
            sb_printf(o, "    eligible[%d] |= has_%s[%d];\n", i, wants_far ? "far" : "near", a);
        }
        if (!wants_far) sb_printf(o, "    w->tide_owned_selected[%d] = eligible[%d];\n", i, i);
    }
    // Keep large dependency graphs as data. Unrolling every edge makes a huge
    // control-flow graph and can exhaust embedded Clang's compilation stack.
    bool edges = false;
    for (int consumer = 0; consumer < p->systems.count; consumer++) for (int producer = 0; producer < p->systems.count; producer++) {
        if (consumer == producer || !owned_produces(&plan->uses[producer], &plan->uses[consumer])) continue;
        if (!edges) sb_put(o, "    static const uint32_t edges[][2] = {\n");
        edges = true;
        sb_printf(o, "        {%d, %d},\n", consumer, producer);
    }
    if (edges) {
        sb_put(o, "    };\n    bool changed;\n    do {\n        changed = false;\n"
                  "        for (size_t edge = 0; edge < sizeof edges / sizeof edges[0]; edge++) {\n"
                  "            const uint32_t consumer = edges[edge][0], producer = edges[edge][1];\n"
                  "            if (w->tide_owned_selected[consumer] && eligible[producer] && !w->tide_owned_selected[producer])\n"
                  "                w->tide_owned_selected[producer] = changed = true;\n"
                  "        }\n    } while (changed);\n");
    }
    sb_put(o, "}\n\n");
}
static void gen_owned_singleton_guard(gen *g, const decl *d)
{
    owned_plan *plan = owned_plan_of(g);
    sb_put(&g->c, "    if (");
    bool first = true;
    for (int i = 0; i < g->prog->systems.count; i++) {
        bool used = false;
        for (int k = 0; k < plan->uses[i].reads.count; k++) used |= plan->uses[i].reads.items[k] == d;
        if (used) { sb_printf(&g->c, "%sp->tide_owned_selected[%d]", first ? "" : " || ", i); first = false; }
    }
    if (first) sb_put(&g->c, "false");
    sb_put(&g->c, ") {\n");
}
