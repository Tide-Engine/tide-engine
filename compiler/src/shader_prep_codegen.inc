// Reuse completed shader bindings only inside a closed, read-only view run.
// This is generated-C state, not a new draw/runtime interface. Each invocation
// owns its entries, before the per-row scratch mark, and restores the previous
// TLS scope when it finishes. No source storage is borrowed by a cache entry.

typedef struct shader_prep_walk {
    VEC(const decl *) seen;
    bool *shaders;
    bool safe;
} shader_prep_walk;

static void shader_prep_stmt(shader_prep_walk *w, const stmt *s);
static void shader_prep_routine(shader_prep_walk *w, const decl *d);
static void shader_prep_expr(shader_prep_walk *w, const expr *e);

static void shader_prep_defaults(shader_prep_walk *w, const decl *d, const expr *literal)
{
    if (!d || d->optional_of) return;
    for (int i = 0; i < d->fields.count; i++) {
        const field *f = &d->fields.items[i];
        bool given = false;
        for (int j = 0; literal && j < literal->inits.count; j++) {
            given |= literal->inits.items[j].field == f || (d->callable && str_eq(literal->inits.items[j].name, f->name));
        }
        if (given) continue;
        if (declared_default(f)) shader_prep_expr(w, declared_default(f));
        else if (f->type.kind == TY_STRUCT) shader_prep_defaults(w, f->type.decl, NULL);
    }
}

static bool shader_prep_interpolates(const type t)
{
    if (t.kind != TY_STRUCT && t.kind != TY_COMPONENT && t.kind != TY_SINGLETON && t.kind != TY_ARRAY) return false;
    if (t.decl->interpolate) return true;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (shader_prep_interpolates(t.decl->fields.items[i].type)) return true;
    }
    return false;
}

static bool shader_prep_param(const param *p)
{
    if (p->type.kind == TY_QUERY || p->mode == PARAM_MUT || p->is_out || p->local_written || p->match_write || p->heap_written
        || p->verified || shader_prep_interpolates(p->type)) return false;
    for (int i = 0; i < p->query.count; i++) if (!shader_prep_param(&p->query.items[i])) return false;
    return true;
}

static void shader_prep_expr(shader_prep_walk *w, const expr *e)
{
    if (!e || !w->safe) return;
    if (e->resource_operation || e->io_operation || e->web_fetch || e->storage_flush || e->task_start_value
        || e->kind == E_AWAIT || e->kind == E_LIST || e->kind == E_INTERP) { w->safe = false; return; }
    switch (e->call) {
    case CALL_NONE: case CALL_CONSTRUCT: case CALL_ENUM_COUNT:
    case CALL_METHOD: case CALL_FUNCTION: case CALL_TRANSFORM: case CALL_SNAP:
        break;
    case CALL_DRAW:
        // Text measurement calls the host's font-measure callback immediately.
        if (e->c_callee && strcmp(e->c_callee, "tide_draw_measure_text") == 0) w->safe = false;
        break;
    case CALL_BUILTIN: {
        // These owners are arithmetic-only. Other host APIs can invoke C
        // callbacks even when their Tide signature takes no mutable argument.
        const expr *owner = e->object;
        if (e->type.kind == TY_VOID || !owner || owner->kind != E_NAME
            || (!str_eq_c(owner->name, "Math") && !str_eq_c(owner->name, "quaternion")
                && !str_eq_c(owner->name, "float2x2") && !str_eq_c(owner->name, "float3x3")
                && !str_eq_c(owner->name, "float4x4"))) w->safe = false;
        break;
    }
    // Get can invoke a component's custom interpolation implicitly, beyond the
    // authored helper graph. Keep only the existence test in this first path.
    case CALL_WORLD:
        if (!str_eq_c(e->name, "Has")) w->safe = false;
        break;
    default: w->safe = false; return;
    }
    if (e->bind == BIND_CONST && e->constant) {
        bool seen = false;
        for (int i = 0; i < w->seen.count; i++) seen |= w->seen.items[i] == e->constant;
        if (!seen) {
            vec_push(w->seen, e->constant);
            shader_prep_expr(w, e->constant->value); // Constants expand at each CPU use.
        }
    }
    if (e->shader) w->shaders[e->shader->index] = true;
    if (e->method) shader_prep_routine(w, e->method); // Also operators and lowered property getters.
    if (e->property && e->property->get) shader_prep_routine(w, e->property->get);
    if (e->kind == E_LITERAL) shader_prep_defaults(w, e->type_decl, e);
    if ((e->kind == E_DEFAULT || e->kind == E_DEFAULTED)
        && (e->type.kind == TY_STRUCT || e->type.kind == TY_COMPONENT || e->type.kind == TY_SINGLETON
            || e->type.kind == TY_INPUT || e->type.kind == TY_EVENT)) shader_prep_defaults(w, e->type.decl, NULL);
    shader_prep_expr(w, e->object);
    shader_prep_expr(w, e->lhs);
    shader_prep_expr(w, e->rhs);
    shader_prep_expr(w, e->cond);
    for (int i = 0; i < e->args.count; i++) shader_prep_expr(w, e->args.items[i]);
    for (int i = 0; i < e->written.count; i++) shader_prep_expr(w, e->written.items[i]);
    for (int i = 0; i < e->inits.count; i++) shader_prep_expr(w, e->inits.items[i].value);
    // Constructing a configured GPU callback captures values; its body runs on
    // the GPU. A CPU callable invocation is rejected by its generated dispatcher.
    if (!e->lambda) shader_prep_expr(w, e->lambda_body);
    shader_prep_expr(w, e->values);
    shader_prep_expr(w, e->mesh_lod);
    shader_prep_expr(w, e->skin_mesh);
    shader_prep_expr(w, e->skin_pose);
    shader_prep_expr(w, e->callable_receiver);
    shader_prep_expr(w, e->task_source);
    shader_prep_expr(w, e->binding_value);
    shader_prep_stmt(w, e->block);
    shader_prep_stmt(w, e->step);
    shader_prep_stmt(w, e->compare[0]);
    shader_prep_stmt(w, e->compare[1]);
    shader_prep_stmt(w, e->out_local);
    shader_prep_stmt(w, e->binding);
}

static void shader_prep_stmt(shader_prep_walk *w, const stmt *s)
{
    if (!s || !w->safe) return;
    if (s->parallel) { w->safe = false; return; }
    if (s->kind == S_VAR && (s->delayed || !s->value)
        && (s->type.kind == TY_STRUCT || s->type.kind == TY_COMPONENT || s->type.kind == TY_SINGLETON
            || s->type.kind == TY_INPUT || s->type.kind == TY_EVENT)) shader_prep_defaults(w, s->type.decl, NULL);
    // Numeric temporaries can change. Indexed writes may change a retained
    // container without changing its descriptor, so the whole view falls back.
    for (const expr *p = s->target; p; p = p->object) {
        if (p->kind == E_INDEX) { w->safe = false; return; }
    }
    if (s->operator_decl) shader_prep_routine(w, s->operator_decl);
    for (int i = 0; i < s->stmts.count; i++) shader_prep_stmt(w, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        for (int j = 0; j < s->cases.items[i].labels.count; j++) shader_prep_expr(w, s->cases.items[i].labels.items[j]);
        for (int j = 0; j < s->cases.items[i].body.count; j++) shader_prep_stmt(w, s->cases.items[i].body.items[j]);
    }
    shader_prep_expr(w, s->cond);
    shader_prep_expr(w, s->value);
    shader_prep_expr(w, s->target);
    shader_prep_expr(w, s->property_set);
    shader_prep_expr(w, s->by);
    shader_prep_expr(w, s->offset);
    shader_prep_stmt(w, s->init);
    shader_prep_stmt(w, s->step);
    shader_prep_stmt(w, s->then_stmt);
    shader_prep_stmt(w, s->else_stmt);
}

static void shader_prep_routine(shader_prep_walk *w, const decl *d)
{
    if (!d || !w->safe) return;
    for (int i = 0; i < w->seen.count; i++) if (w->seen.items[i] == d) return;
    vec_push(w->seen, d);
    if (!d->body || d->is_extern || d->extern_system || d->is_async || d->is_worker || d->uses_workers
        || d->is_virtual || d->is_override || d->replaced_by
        || d->starts_tasks || d->takes_action || d->generated_callable || d->is_mut_method
        || d->plays || d->uses_io || d->world_sets || d->acts[0] || d->acts[1] || d->spawns || d->rolls
        || d->writes_text || d->takes_verified || mask_any(d->query_mut_fields)) { w->safe = false; return; }
    for (int i = 0; i < d->params.count; i++) {
        if (!shader_prep_param(&d->params.items[i])) { w->safe = false; return; }
    }
    // calls_c alone also includes Texture.size. Keep evaluating those reads in
    // their original order, and reject genuine externs through this AST walk.
    shader_prep_stmt(w, d->body);
}

static bool shader_prep_supported(const shader *sh)
{
    if (sh->skinned || sh->instanced || sh->blit || sh->sizes.count) return false;
    if (!sh->textures.count) return false; // Binding resolution is what this optimization saves.
    for (int i = 0; i < sh->textures.count; i++) {
        const shader_texture *t = &sh->textures.items[i];
        const type source = t->asset ? t->asset->return_type : t->field ? t->field->type : (type){TY_TEXTURE, NULL};
        if (t->skin || t->read == READ_CUBE || (source.kind != TY_TEXTURE && source.kind != TY_DEPTH_TEXTURE
            && !(source.kind == TY_LIST && t->read == READ_DATA))) return false;
    }
    return true;
}

static void gen_shader_prep_plan(gen *g)
{
    const program *p = g->prog;
    g->shader_prep_views = arena_alloc((size_t)p->views.count * sizeof *g->shader_prep_views);
    g->shader_prep_shaders = arena_alloc((size_t)p->shaders.count * sizeof *g->shader_prep_shaders);
    for (int v = 0; v < p->views.count; v++) {
        shader_prep_walk w = {.shaders = arena_alloc((size_t)p->shaders.count * sizeof(bool)), .safe = true};
        shader_prep_routine(&w, p->views.items[v]);
        // An absent entity's attached fields are initialized in the runner,
        // outside the authored body but inside this scope's lifetime.
        const decl *view = p->views.items[v];
        for (int i = 0; i < view->params.count; i++) {
            const param *param = &view->params.items[i];
            if (param->type.kind == TY_COMPONENT && has_fields_ptr(param) && !attaches_first(param))
                shader_prep_defaults(&w, local_part_of(param->type), NULL);
        }
        if (!w.safe) continue;
        bool any = false;
        for (int s = 0; s < p->shaders.count; s++) {
            w.shaders[s] &= shader_prep_supported(p->shaders.items[s]);
            any |= w.shaders[s];
            g->shader_prep_shaders[s] |= w.shaders[s];
            g->shader_prep_active |= w.shaders[s];
        }
        if (any) g->shader_prep_views[v] = w.shaders;
    }
}

static void gen_shader_prep_types(gen *g)
{
    sb *o = &g->c;
    bool any = false;
    for (int s = 0; s < g->prog->shaders.count; s++) any |= g->shader_prep_shaders[s];
    if (!any) return;
    sb_put(o,
        "// Completed resource bindings live only for one proven read-only view invocation.\n"
        "typedef struct tide_shader_prep_stamp {\n"
        "    uint64_t id, version, kind;\n"
        "    uint32_t space, format, frame, epoch;\n"
        "    int32_t width, height;\n"
        "} tide_shader_prep_stamp;\n"
        "typedef struct tide_shader_prep_slot { int shader; void *cache; } tide_shader_prep_slot;\n"
        "typedef struct tide_shader_prep_scope {\n"
        "    const tide_shader_prep_slot *slots; uint32_t count;\n"
        "} tide_shader_prep_scope;\n"
        "static TIDE_THREAD_LOCAL const tide_shader_prep_scope *tide_shader_prep_current;\n"
        "TIDE_HELPER tide_int2 tide_shader_prep_size(const tide_texture_ref texture)\n{\n"
        "    // The host target-pixel resolver is trusted C, not a pure source.\n"
        "    // Disable reuse for the rest of this scope before it can run.\n"
        "    if (texture.target.id) tide_shader_prep_current = NULL;\n"
        "    return tide_texture_size(texture);\n}\n"
        "TIDE_HELPER void *tide_shader_prep_find(const int shader)\n{\n"
        "    if (!tide_shader_prep_current) return NULL;\n"
        "    for (uint32_t i = 0; i < tide_shader_prep_current->count; i++)\n"
        "        if (tide_shader_prep_current->slots[i].shader == shader) return tide_shader_prep_current->slots[i].cache;\n"
        "    return NULL;\n}\n"
        "TIDE_HELPER bool tide_shader_prep_handle(const tide_handle at)\n{\n"
        "    return !at || tide_handle_where(at) != TIDE_IN_SCRATCH || tide_pack_holds(at);\n}\n"
        "TIDE_HELPER tide_shader_prep_stamp tide_shader_prep_texture(const tide_draw_list *draw, const uint32_t slot)\n{\n"
        "    if (!slot || slot > draw->texture_count) return (tide_shader_prep_stamp){0};\n"
        "    const tide_draw_texture *t = &draw->textures[slot - 1u];\n"
        "    return (tide_shader_prep_stamp){.id = t->id, .version = t->version, .kind = t->changes ? t->changes->kind : 0u,\n"
        "        .space = t->space, .format = t->format, .frame = t->frame, .epoch = t->epoch,\n"
        "        .width = t->width, .height = t->height};\n}\n"
        "TIDE_HELPER bool tide_shader_prep_valid(const tide_draw_list *draw, const uint32_t *slots,\n"
        "    const tide_shader_prep_stamp *stamps, const uint32_t count)\n{\n"
        "    for (uint32_t i = 0; i < count; i++) {\n"
        "        if (!slots[i]) continue;\n"
        "        if (slots[i] > draw->texture_count) return false;\n"
        "        const tide_shader_prep_stamp now = tide_shader_prep_texture(draw, slots[i]), was = stamps[i];\n"
        "        if (now.id != was.id || now.version != was.version || now.kind != was.kind || now.space != was.space\n"
        "            || now.format != was.format || now.frame != was.frame || now.epoch != was.epoch\n"
        "            || now.width != was.width || now.height != was.height) return false;\n"
        "    }\n    return true;\n}\n\n");
    for (int s = 0; s < g->prog->shaders.count; s++) {
        if (!g->shader_prep_shaders[s]) continue;
        const shader *sh = g->prog->shaders.items[s];
        const int bytes = ((sh->uniform_floats + 3) / 4 + (sh->uniform_ints + 3) / 4) * 16;
        sb_printf(o, "typedef struct tide_shader_prep_%d_entry {\n"
            "    tide_draw_list *draw;\n    uint32_t frame, epoch;\n    uint64_t sources[%d];\n"
            "    uint8_t key[%d], uniforms[%d];\n    uint32_t textures[%d];\n"
            "    tide_shader_prep_stamp stamps[%d];\n    bool ready;\n} tide_shader_prep_%d_entry;\n"
            "typedef struct tide_shader_prep_%d { uint32_t next; tide_shader_prep_%d_entry entries[2]; } tide_shader_prep_%d;\n\n",
            s, 13 + 8 * sh->textures.count, bytes ? bytes : 1, bytes ? bytes : 1, sh->textures.count,
            sh->textures.count, s, s, s, s);
    }
}

static void gen_shader_prep_enter(gen *g, const decl *view, const int v)
{
    (void)view;
    if (!g->shader_prep_views) return;
    bool any = false;
    for (int s = 0; s < g->prog->shaders.count; s++) any |= g->shader_prep_shaders[s];
    if (!any) return;
    sb *o = &g->c;
    // A reentrant frame must not lend the outer proven region's caches to an
    // ineligible view that may mutate its sources.
    sb_put(o, "    const tide_shader_prep_scope *tide_prep_previous = tide_shader_prep_current;\n"
        "    tide_shader_prep_current = NULL;\n");
    if (!g->shader_prep_views[v]) return;
    // One scratch allocation before any row mark. Its fixed generated size is
    // bounded by the shaders this view can reach, with no stack-size cliff.
    sb_put(o, "    const uint64_t tide_prep_mark = tide_scratch_mark();\n    struct {\n");
    for (int s = 0; s < g->prog->shaders.count; s++) {
        if (g->shader_prep_views[v][s]) sb_printf(o, "        tide_shader_prep_%d s%d;\n", s, s);
    }
    sb_put(o, "    } *tide_prep_data = tide_scratch_memory(sizeof *tide_prep_data);\n"
        "    memset(tide_prep_data, 0, sizeof *tide_prep_data);\n    const tide_shader_prep_slot tide_prep_slots[] = {\n");
    for (int s = 0; s < g->prog->shaders.count; s++) {
        if (g->shader_prep_views[v][s]) sb_printf(o, "        {%d, &tide_prep_data->s%d},\n", s, s);
    }
    sb_put(o, "    };\n    const tide_shader_prep_scope tide_prep_scope = {tide_prep_slots, sizeof tide_prep_slots / sizeof *tide_prep_slots};\n"
        "    tide_shader_prep_current = &tide_prep_scope;\n");
}

static void gen_shader_prep_leave(gen *g, const int v)
{
    if (!g->shader_prep_views) return;
    bool any = false;
    for (int s = 0; s < g->prog->shaders.count; s++) any |= g->shader_prep_shaders[s];
    if (!any) return;
    sb_put(&g->c, "    tide_shader_prep_current = tide_prep_previous;\n");
    if (g->shader_prep_views[v]) sb_put(&g->c, "    tide_scratch_reset(tide_prep_mark);\n");
}

static const char *shader_prep_from(const shader *sh, const shader_texture *t)
{
    if (t->asset) return asset_cname(t->asset);
    if (!t->source) return "tide_texture";
    sb from = {0};
    if (sh->values_type && t->source == sh->values_type) sb_printf(&from, "tide_values.%s", field_cname(t->field));
    else {
        int b = 0;
        while (sh->bound.items[b] != t->source) b++;
        sb_printf(&from, "tide_%sb%d->%s", t->field->is_local ? "l" : "", b, field_cname(t->field));
    }
    return from.data;
}

static void gen_shader_prep_begin(gen *g, const shader *sh)
{
    if (!g->shader_prep_shaders[sh->index]) return;
    sb *o = &g->c;
    const int s = sh->index;
    const bool uniforms = sh->uniform_floats + sh->uniform_ints != 0;
    sb_printf(o, "    tide_shader_prep_%d *tide_prep = tide_shader_prep_find(%d);\n"
        "    tide_shader_prep_%d_entry *tide_prep_hit = NULL;\n"
        "    uint64_t tide_prep_sources[%d];\n    bool tide_prep_safe = tide_prep != NULL && tide_draw != NULL;\n",
        s, s, s, 13 + 8 * sh->textures.count);
    sb_printf(o, "    uint8_t tide_prep_key[%d];\n", uniforms ? ((sh->uniform_floats + 3) / 4 + (sh->uniform_ints + 3) / 4) * 16 : 1);
    sb_put(o, "    if (tide_prep_safe) {\n    memset(tide_prep_sources, 0, sizeof tide_prep_sources);\n");
    for (int i = 0; i < 4; i++) sb_printf(o,
        "    tide_prep_sources[%d] = (uintptr_t)tide_world_heaps[%d];\n"
        "    tide_prep_sources[%d] = tide_world_heaps[%d] ? tide_world_heaps[%d]->used : 0;\n"
        "    tide_prep_sources[%d] = tide_world_heaps[%d] ? tide_world_heaps[%d]->pending : 0;\n",
        i * 3, i, i * 3 + 1, i, i, i * 3 + 2, i, i);
    sb_put(o, "    tide_prep_safe = tide_prep_safe && !tide_prep_sources[2] && !tide_prep_sources[5]\n"
        "        && !tide_prep_sources[8] && !tide_prep_sources[11];\n");
    sb_put(o, "    tide_prep_sources[12] = (uintptr_t)tide_pack_memory;\n");
    for (int t = 0; t < sh->textures.count; t++) {
        const shader_texture *tex = &sh->textures.items[t];
        const type source = tex->asset ? tex->asset->return_type : tex->field ? tex->field->type : (type){TY_TEXTURE, NULL};
        const char *from = shader_prep_from(sh, tex);
        const int at = 13 + 8 * t;
        if (source.kind == TY_LIST) {
            sb_printf(o, "    tide_prep_sources[%d] = (%s).at;\n"
                "    tide_prep_safe = tide_prep_safe && tide_shader_prep_handle((%s).at);\n", at, from, from);
        } else {
            if (source.kind == TY_DEPTH_TEXTURE) sb_printf(o, "    const tide_texture_ref tide_prep_t%d = {.target = (%s).target};\n", t, from);
            else sb_printf(o, "    const tide_texture_ref tide_prep_t%d = %s;\n", t, from);
            sb_printf(o,
                "    tide_prep_sources[%d] = (uintptr_t)tide_prep_t%d.image.pixels;\n"
                "    tide_prep_sources[%d] = tide_prep_t%d.image.version;\n"
                "    tide_prep_sources[%d] = (uint64_t)(uint32_t)tide_prep_t%d.image.width | (uint64_t)(uint32_t)tide_prep_t%d.image.height << 32;\n"
                "    tide_prep_sources[%d] = tide_prep_t%d.target.id;\n"
                "    memcpy(&tide_prep_sources[%d], &tide_prep_t%d.target.size.width, sizeof(float));\n"
                "    memcpy((uint8_t *)&tide_prep_sources[%d] + 4, &tide_prep_t%d.target.size.height, sizeof(float));\n"
                "    memcpy(&tide_prep_sources[%d], &tide_prep_t%d.target.size.scale, sizeof(float));\n"
                "    memcpy((uint8_t *)&tide_prep_sources[%d] + 4, &tide_prep_t%d.target.size.window, sizeof(float));\n"
                "    tide_grid tide_prep_g%d; tide_grid_shape tide_prep_shape%d;\n"
                "    if (tide_texture_grid(tide_prep_t%d, &tide_prep_g%d, &tide_prep_shape%d))\n"
                "        tide_prep_safe = tide_prep_safe && tide_shader_prep_handle(tide_prep_g%d.at);\n",
                at, t, at + 1, t, at + 2, t, t, at + 3, t, at + 4, t, at + 4, t, at + 5, t, at + 5, t,
                t, t, t, t, t, t);
        }
    }
    if (uniforms) sb_put(o, "    memcpy(tide_prep_key, &tide_uniforms, sizeof tide_uniforms);\n");
    else sb_put(o, "    tide_prep_key[0] = 0;\n");
    sb_put(o, "    if (tide_prep_safe) for (uint32_t tide_p = 0; tide_p < 2u; tide_p++) {\n");
    sb_printf(o, "        tide_shader_prep_%d_entry *entry = &tide_prep->entries[tide_p];\n", s);
    sb_printf(o, "        if (entry->ready && entry->draw == tide_draw && entry->frame == tide_draw->frame && entry->epoch == tide_draw->epoch\n"
        "            && memcmp(entry->sources, tide_prep_sources, sizeof tide_prep_sources) == 0\n"
        "            && memcmp(entry->key, tide_prep_key, sizeof tide_prep_key) == 0\n"
        "            && tide_shader_prep_valid(tide_draw, entry->textures, entry->stamps, %du)) { tide_prep_hit = entry; break; }\n"
        "    }\n    }\n    if (tide_prep_hit) {\n", sh->textures.count);
    if (uniforms) sb_put(o, "        memcpy(&tide_uniforms, tide_prep_hit->uniforms, sizeof tide_uniforms);\n");
    sb_put(o, "        memcpy(tide_textures, tide_prep_hit->textures, sizeof tide_textures);\n    } else {\n");
}

static void gen_shader_prep_end(gen *g, const shader *sh)
{
    if (!g->shader_prep_shaders[sh->index]) return;
    sb *o = &g->c;
    sb_printf(o, "        if (tide_prep_safe) {\n"
        "            tide_shader_prep_%d_entry *entry = &tide_prep->entries[tide_prep->next++ %% 2u];\n"
        "            entry->draw = tide_draw; entry->frame = tide_draw->frame; entry->epoch = tide_draw->epoch;\n"
        "            memcpy(entry->sources, tide_prep_sources, sizeof tide_prep_sources);\n"
        "            memcpy(entry->key, tide_prep_key, sizeof tide_prep_key);\n", sh->index);
    if (sh->uniform_floats + sh->uniform_ints) sb_put(o, "            memcpy(entry->uniforms, &tide_uniforms, sizeof tide_uniforms);\n");
    sb_printf(o, "            memcpy(entry->textures, tide_textures, sizeof tide_textures);\n"
        "            for (uint32_t tide_p = 0; tide_p < %du; tide_p++) entry->stamps[tide_p] = tide_shader_prep_texture(tide_draw, tide_textures[tide_p]);\n"
        "            entry->ready = true;\n        }\n    }\n", sh->textures.count);
}
