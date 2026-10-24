// Worker boundaries contain owned CPU values, never bindings or resources.
static bool worker_value_type(const type t, const int depth)
{
    if (depth > 128) return false;
    switch (t.kind) {
    case TY_WORLD: case TY_QUERY: case TY_COMPONENT: case TY_SINGLETON:
    case TY_INPUT: case TY_RECORD: case TY_ACTION: case TY_DRAW_LIST:
    case TY_TEXTURE: case TY_DEPTH_TEXTURE: case TY_CUBEMAP: case TY_RENDER_TARGET:
    case TY_CLIP: case TY_RESIDENT: case TY_GRID: case TY_MESH: return false;
    case TY_STRUCT: case TY_EVENT: case TY_ARRAY: case TY_LIST: case TY_MAP:
    case TY_SET: case TY_OPTIONAL: case TY_FAILABLE:
        for (int i = 0; i < t.decl->fields.count; i++) {
            if (!worker_value_type(t.decl->fields.items[i].type, depth + 1)) return false;
        }
        return true;
    default: return true;
    }
}

static bool worker_borrows_stmt(const stmt *s);

static bool worker_borrows_expr(const expr *e)
{
    if (!e) return false;
    if (e->bind == BIND_ASSET) return true;
    if (worker_borrows_expr(e->object) || worker_borrows_expr(e->lhs)
        || worker_borrows_expr(e->rhs) || worker_borrows_expr(e->cond) || worker_borrows_stmt(e->block)) return true;
    for (int i = 0; i < e->args.count; i++) if (worker_borrows_expr(e->args.items[i])) return true;
    for (int i = 0; i < e->inits.count; i++) if (worker_borrows_expr(e->inits.items[i].value)) return true;
    return false;
}

static bool worker_borrows_stmt(const stmt *s)
{
    if (!s) return false;
    if (worker_borrows_expr(s->value) || worker_borrows_expr(s->target) || worker_borrows_expr(s->cond)
        || worker_borrows_stmt(s->init) || worker_borrows_stmt(s->step)
        || worker_borrows_stmt(s->then_stmt) || worker_borrows_stmt(s->else_stmt)) return true;
    for (int i = 0; i < s->stmts.count; i++) if (worker_borrows_stmt(s->stmts.items[i])) return true;
    for (int i = 0; i < s->cases.count; i++) {
        for (int j = 0; j < s->cases.items[i].labels.count; j++)
            if (worker_borrows_expr(s->cases.items[i].labels.items[j])) return true;
        for (int j = 0; j < s->cases.items[i].body.count; j++)
            if (worker_borrows_stmt(s->cases.items[i].body.items[j])) return true;
    }
    return false;
}

static const char *worker_effect(const decl *d)
{
    if (d->draws || d->plays || d->render_resources) return "draw, play audio or access rendering resources";
    if (d->uses_io) return "access files or the network";
    if (d->frame_time || d->frame_devices || d->frame_cursor) return "access frame services";
    if (d->world_local || d->world_match || d->uses_this || d->world_uses.count) return "access a live world";
    if (d->acts[0] || d->acts[1] || d->starts_tasks || d->is_async) return "send events, change entities or start tasks";
    if (d->rolls || d->reads_seed) return "read the match's random generator or seed";
    if (d->borrows_asset_collections || d->gpu_only || d->is_audio) return "access frame assets, GPU or audio code";
    if (worker_borrows_stmt(d->body)) return "read live assets; capture their data before starting the worker";
    return NULL;
}

static void check_workers(const checker *c)
{
    for (int i = 0; i < c->prog->decls.count; i++) {
        decl *root = c->prog->decls.items[i];
        if (!root->is_worker) continue;
        if (!worker_value_type(root->result, 0)) diag_error(root->at, "a worker returns owned CPU values, not %s", type_name(root->result));
        for (int k = 0; k < root->params.count; k++) {
            const param *p = &root->params.items[k];
            if (!worker_value_type(p->type, 0)) diag_error(p->at, "a worker takes owned CPU values, not %s; pass the data it needs instead", type_name(p->type));
        }
        VEC(const decl *) seen = {0};
        vec_push(seen, root);
        for (int at = 0; at < seen.count; at++) {
            const decl *d = seen.items[at];
            const char *effect = worker_effect(d);
            if (effect) {
                diag_error(root->worker_at, "local worker '" STR_FMT "' can't %s", STR_ARG(root->name), effect);
                if (d != root) diag_note_at(d->at, "the worker calls '" STR_FMT "', which needs this access", STR_ARG(d->name));
                break;
            }
            for (int k = 0; k < c->calls.count; k++) {
                const call_site *site = &c->calls.items[k];
                if (site->from != d || site->to->is_extern) continue; // C remains trusted.
                bool known = false;
                for (int j = 0; j < seen.count; j++) known |= seen.items[j] == site->to;
                if (!known) vec_push(seen, site->to);
            }
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < c->prog->decls.count; i++) {
            decl *d = c->prog->decls.items[i];
            for (int k = 0; k < d->awaits.count; k++) {
                if (!d->uses_workers && d->awaits.items[k]->uses_workers) d->uses_workers = changed = true;
            }
        }
    }
}
