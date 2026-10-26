// Worker boundaries contain owned CPU values, never bindings or resources.
static bool worker_value_type(const type t, const int depth)
{
    if (depth > 128) return false;
    if (t.decl && (t.decl->callable || t.decl->binding_of || t.decl->asset_ref_of || t.decl->task_handle)) return false;
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

static const char *worker_invalid_stmt(const stmt *s);

static const char *worker_invalid_expr(const expr *e)
{
    if (!e) return NULL;
    if (e->bind == BIND_ASSET || e->asset_ref_value)
        return "read live assets; capture their data before starting the worker";
    const expr *children[] = {e->object, e->lhs, e->rhs, e->cond, e->values, e->skin_mesh, e->skin_pose};
    for (size_t i = 0; i < sizeof children / sizeof *children; i++) {
        const char *error = worker_invalid_expr(children[i]);
        if (error) return error;
    }
    const char *error = worker_invalid_stmt(e->step);
    if (!error) error = worker_invalid_stmt(e->block);
    if (error) return error;
    for (int i = 0; i < e->args.count; i++) {
        error = worker_invalid_expr(e->args.items[i]);
        if (error) return error;
    }
    for (int i = 0; i < e->inits.count; i++) {
        error = worker_invalid_expr(e->inits.items[i].value);
        if (error) return error;
    }
    return NULL;
}

static const char *worker_invalid_stmt(const stmt *s)
{
    if (!s) return NULL;
    if (s->kind == S_VAR && s->type.kind == TY_GRID)
        return "keep grid locals; read or forward the grid parameter directly";
    const expr *values[] = {s->value, s->target, s->cond, s->by, s->offset};
    for (size_t i = 0; i < sizeof values / sizeof *values; i++) {
        const char *error = worker_invalid_expr(values[i]);
        if (error) return error;
    }
    const stmt *children[] = {s->init, s->step, s->then_stmt, s->else_stmt};
    for (size_t i = 0; i < sizeof children / sizeof *children; i++) {
        const char *error = worker_invalid_stmt(children[i]);
        if (error) return error;
    }
    for (int i = 0; i < s->stmts.count; i++) {
        const char *error = worker_invalid_stmt(s->stmts.items[i]);
        if (error) return error;
    }
    for (int i = 0; i < s->cases.count; i++) {
        for (int j = 0; j < s->cases.items[i].labels.count; j++) {
            const char *error = worker_invalid_expr(s->cases.items[i].labels.items[j]);
            if (error) return error;
        }
        for (int j = 0; j < s->cases.items[i].body.count; j++) {
            const char *error = worker_invalid_stmt(s->cases.items[i].body.items[j]);
            if (error) return error;
        }
    }
    return NULL;
}

static const char *worker_effect(const decl *d)
{
    if (d->draws || d->plays || d->render_resources) return "draw, play audio or access rendering resources";
    if (d->uses_io) return "access files or the network";
    if (d->frame_time || d->frame_devices || d->frame_cursor) return "access frame services";
    if (d->world_local || d->world_match || d->uses_this || d->world_uses.count) return "access a live world";
    if (d->acts[0] || d->acts[1] || d->starts_tasks || d->is_async) return "send events, change entities or start tasks";
    if (d->rolls || d->reads_seed) return "read the match's random generator or seed";
    if (d->borrows_asset_collections || d->gpu_sampling || (d->gpu_only && !d->worker_cpu) || d->is_audio)
        return "access frame assets, GPU or audio code";
    return worker_invalid_stmt(d->body);
}

static void check_workers(const checker *c)
{
    for (int i = 0; i < c->prog->decls.count; i++) {
        decl *root = c->prog->decls.items[i];
        if (!root->is_worker) continue;
        if (!worker_value_type(root->result, 0)) diag_error(root->at, "a worker returns owned CPU values, not %s", type_name(root->result));
        for (int k = 0; k < root->params.count; k++) {
            const param *p = &root->params.items[k];
            if (p->type.kind != TY_GRID && !worker_value_type(p->type, 0))
                diag_error(p->at, "a worker takes owned CPU values, not %s; pass the data it needs instead", type_name(p->type));
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
