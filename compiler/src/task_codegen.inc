// First-class task identities are world values. Only indices/generations enter
// frames, snapshots or network bytes; result tables retain ordinary owned values.
static const char *task_side_name(const bool local) { return local ? "local" : "match"; }

static const char *task_store_name(const gen *g, const bool local)
{
    return type_cname(g->prog->task_slots[local]);
}

static const char *task_slot_name(const gen *g, const bool local)
{
    return c_type(list_elem(g->prog->task_slots[local]->fields.items[0].type.decl));
}

static uint64_t task_code(const decl *fn)
{
    const char *name = task_id(fn);
    uint64_t h = UINT64_C(14695981039346656037);
    for (const char *p = name; *p; p++) h = (h ^ (uint8_t)*p) * UINT64_C(1099511628211);
    return (h & INT64_MAX) | 1u;
}

static void gen_task_waiting(gen *g, const task_info *ti)
{
    if ((!g->prog->task_slots[0] && !g->prog->task_slots[1]) || task_is_completion(ti->fn)) return;
    sb *o = &g->c;
    sb_printf(o, "TIDE_HELPER bool tide_task_waiting_%s(const %s *f, int64_t *id, int32_t *slot)\n{\n"
                 "    if (f->tide_waiting_id) { *id = f->tide_waiting_id; *slot = f->tide_waiting_slot; return true; }\n",
              type_cname(ti->frame), type_cname(ti->frame));
    for (int i = 0; i < ti->fn->awaits.count; i++) {
        const task_info *child = task_of(g, ti->fn->awaits.items[i]);
        sb_printf(o, "    if (tide_task_waiting_%s(&f->tide_await_%s, id, slot)) return true;\n",
                  type_cname(child->frame), task_id(child->fn));
    }
    sb_put(o, "    return false;\n}\n\n");
}

static void gen_task_store_helpers(gen *g)
{
    sb *o = &g->c;
    for (int side = 0; side < 2; side++) {
        const bool local = side != 0;
        if (!g->prog->task_slots[side]) continue;
        const char *s = task_side_name(local), *w = world_type(local), *store = task_store_name(g, local);
        const char *slot = task_slot_name(g, local), *where = world_where(local);
        sb_printf(o, "TIDE_HELPER void tide_task_%s_cancel_work(%s *w, int64_t id, int32_t slot);\n", s, w);
        sb_printf(o, "TIDE_HELPER bool tide_task_%s_valid(const %s *w, const %s *v, int32_t slot);\n", s, w, slot);
        sb_printf(o, "TIDE_HELPER const %s *tide_task_%s_read(const %s *w, const int64_t id, const int32_t slot)\n{\n"
                     "    if (!w || id <= 0 || slot < 0 || slot >= tide_list_count(w->%s.slots)) return NULL;\n"
                     "    const %s *v = tide_list_at(w->%s.slots, slot, sizeof *v);\n"
                     "    return v->id == id ? v : NULL;\n}\n\n", slot, s, w, store, slot, store);
        sb_printf(o, "TIDE_HELPER %s *tide_task_%s_mut(%s *w, const int64_t id, const int32_t slot)\n{\n"
                     "    if (!tide_task_%s_read(w, id, slot)) return NULL;\n"
                     "    return tide_list_at_mut(w->%s.slots, slot, sizeof(%s));\n}\n\n", slot, s, w, s, store, slot);
        sb_printf(o, "TIDE_HELPER int32_t tide_task_%s_status(const %s *w, const int64_t id, const int32_t slot)\n{\n"
                     "    const %s *v = tide_task_%s_read(w, id, slot);\n"
                     "    return v && tide_task_%s_valid(w, v, slot) ? v->state : 3; // A default/stale handle is cancelled.\n}\n\n", s, w, slot, s, s);
        sb_printf(o, "TIDE_HELPER void tide_task_%s_new(%s *w, const int64_t code, const int32_t flags, int64_t *id, int32_t *slot)\n{\n"
                     "    %s *store = &w->%s;\n"
                     "    if (store->serial < 0 || store->serial == INT64_MAX) tide_out_of_memory();\n"
                     "    const int64_t serial = ++store->serial;\n"
                     "    %s *v;\n"
                     "    if (store->free) {\n"
                     "        if (store->free < 0 || store->free > tide_list_count(store->slots)) tide_out_of_memory();\n"
                     "        *slot = store->free - 1;\n"
                     "        v = tide_list_at_mut(store->slots, *slot, sizeof *v);\n"
                     "        if (v->id || v->next < 0 || v->next > tide_list_count(store->slots)) tide_out_of_memory();\n"
                     "        store->free = v->next;\n"
                     "    } else {\n"
                     "        *slot = tide_list_count(store->slots);\n"
                     "        if (*slot == INT32_MAX) tide_out_of_memory();\n"
                     "        v = tide_list_add(&store->slots, sizeof *v, %s);\n"
                     "    }\n"
                     "    *v = (%s){.id = serial, .code = code, .row = -1, .state = 1, .flags = flags | 2};\n"
                     "    *id = serial;\n}\n\n", s, w, store, store, slot, where, slot);
        sb_printf(o, "TIDE_HELPER void tide_task_%s_bind(%s *w, const int64_t id, const int32_t slot, const int32_t row)\n{\n"
                     "    %s *v = tide_task_%s_mut(w, id, slot);\n    if (v) { v->row = row; v->flags &= ~2; }\n}\n\n", s, w, slot, s);
        sb_printf(o, "TIDE_HELPER void tide_task_%s_cancel(%s *w, const int64_t id, const int32_t slot)\n{\n"
                     "    %s *v = tide_task_%s_mut(w, id, slot);\n"
                     "    if (!v || v->state != 1) return;\n"
                     "    v->state = 3;\n"
                     "    tide_task_%s_cancel_work(w, id, slot);\n}\n\n", s, w, slot, s, s);
    }
    if (g->prog->task_slots[0] || g->prog->task_slots[1]) {
        sb_put(o, "typedef struct tide_task_running {\n"
                  "    const tide_world *match; const tide_local *local; int64_t id; int32_t slot;\n"
                  "} tide_task_running;\n"
                  "static _Thread_local tide_task_running tide_task_running_now;\n"
                  "TIDE_HELPER bool tide_task_stopping(void)\n{\n"
                  "    const tide_task_running run = tide_task_running_now;\n"
                  "    if (!run.id) return false;\n");
        if (g->prog->task_slots[1]) sb_put(o, "    if (run.local) return tide_task_local_status(run.local, run.id, run.slot) == 3;\n");
        if (g->prog->task_slots[0]) sb_put(o, "    if (run.match) return tide_task_match_status(run.match, run.id, run.slot) == 3;\n");
        sb_put(o, "    return false;\n}\n\n");
    }
}

static void gen_task_running_enter(gen *g, const decl *fn, const bool local, const char *frame, const char *pad)
{
    if (!g->prog->task_slots[0] && !g->prog->task_slots[1]) return;
    sb_printf(&g->c, "%sconst tide_task_running tide_running_saved = tide_task_running_now;\n"
                    "%stide_task_running_now = (tide_task_running){.%s = %s",
              pad, pad, local ? "local" : "match", local ? "tide_l" : "tide_w");
    if (fn->handles_tasks) sb_printf(&g->c, ", .id = %s.tide_handle_id, .slot = %s.tide_handle_slot", frame, frame);
    sb_put(&g->c, "};\n");
}

static void gen_task_running_leave(gen *g, const char *pad)
{
    if (g->prog->task_slots[0] || g->prog->task_slots[1])
        sb_printf(&g->c, "%stide_task_running_now = tide_running_saved;\n", pad);
}

static void gen_task_result_helpers(gen *g)
{
    sb *o = &g->c;
    for (int side = 0; side < 2; side++) {
        const bool local = side != 0;
        if (!g->prog->task_slots[side]) continue;
        const char *s = task_side_name(local), *w = world_type(local), *slot = task_slot_name(g, local);
        sb_printf(o, "TIDE_HELPER bool tide_task_%s_valid(const %s *w, const %s *v, const int32_t slot)\n{\n"
                     "    (void)w; (void)slot;\n"
                     "    if (v->state == 3) return false;\n"
                     "    switch (v->code) {\n", s, w, slot);
        for (int i = 0; i < g->tasks.count; i++) {
            const task_info *ti = &g->tasks.items[i];
            const decl *fn = ti->fn;
            if (!fn->is_async || !fn->handles_tasks || !task_table_in(fn, local)) continue;
            sb_printf(o, "    case INT64_C(%llu): {\n"
                         "        if (v->state == 1 && v->row == -1 && (v->flags & 2)) return true;\n",
                      (unsigned long long)task_code(fn));
            for (int done = 0; done < 2; done++) {
                const char *id = task_id(done ? fn->task_result_store : fn);
                sb_printf(o, "        if (v->state == %d && v->row >= 0 && (uint32_t)v->row < w->tide_tasks_%s.count) {\n"
                             "            const tide_task_%s *r = tide_table_get(&w->tide_tasks_%s, &tide_columns_tasks_%s, (uint32_t)v->row, 0);\n"
                             "            return r->frame.tide_handle_id == v->id && r->frame.tide_handle_slot == slot%s;\n"
                             "        }\n", done ? 2 : 1, id, id, id, id, done ? "" : " && r->task.wake >= 0");
            }
            sb_put(o, "        return false;\n    }\n");
        }
        sb_put(o, "    default: return false;\n    }\n}\n\n");
    }
    for (int i = 0; i < g->tasks.count; i++) {
        const task_info *ti = &g->tasks.items[i];
        const decl *fn = ti->fn;
        if (!fn->is_async || !fn->handles_tasks || !fn->task_result_store) continue;
        const task_info *completed = task_of(g, fn->task_result_store);
        const char *result_id = task_id(completed->fn);
        for (int side = 0; side < 2; side++) {
            const bool local = side != 0;
            if (!task_table_in(fn, local) || !g->prog->task_slots[side]) continue;
            const char *s = task_side_name(local), *w = world_type(local), *slot = task_slot_name(g, local);
            const char *where = world_where(local);
            sb_printf(o, "TIDE_HELPER void tide_task_%s_complete_%s(%s *w, %s *f, const int32_t status)\n{\n"
                         "    if (!f->tide_handle_id) return;\n"
                         "    %s *v = tide_task_%s_mut(w, f->tide_handle_id, f->tide_handle_slot);\n"
                         "    if (!v) return;\n"
                         "    if (status == TIDE_TASK_GONE || v->state == 3) { v->state = 3; v->row = -1; return; }\n"
                         "    %s result = {0};\n"
                         "    result.tide_handle_id = f->tide_handle_id;\n"
                         "    result.tide_handle_slot = f->tide_handle_slot;\n",
                      s, task_id(fn), w, type_cname(ti->frame), slot, s, type_cname(completed->frame));
            if (fn->result.kind != TY_VOID) {
                const int indent_before = g->indent;
                g->indent = 1;
                line(g, o, "%s value;", c_type(fn->result));
                gen_reload(g, "value", "f->tide_result", fn->result);
                gen_spill(g, "result.tide_result", "value", fn->result, where);
                g->indent = indent_before;
            }
            sb_printf(o, "    if (w->tide_tasks_%s.count >= INT32_MAX) tide_out_of_memory();\n"
                         "    const uint32_t row = tide_table_add(&w->tide_tasks_%s, &tide_columns_tasks_%s);\n"
                         "    tide_task_%s *out = tide_table_cell(&w->tide_tasks_%s, &tide_columns_tasks_%s, row, 0);\n"
                         "    out->task = (tide_task){.wake = TIDE_TASK_DONE};\n    out->frame = result;\n"
                         "    v = tide_task_%s_mut(w, f->tide_handle_id, f->tide_handle_slot);\n"
                         "    v->state = 2; v->row = (int32_t)row;\n}\n\n",
                      result_id, result_id, result_id, result_id, result_id, result_id, s);
        }
    }
    for (int t = 0; t < g->prog->task_handles.count; t++) {
        const decl *handle = g->prog->task_handles.items[t];
        for (int side = 0; side < 2; side++) {
        const bool local = side != 0;
        const bool either = handle->task_origin && handle->task_origin->task_side == TASK_EITHER;
        if (!either && local != handle->task_local) continue;
        if (!g->prog->task_slots[local]) continue;
        const char *s = task_side_name(local), *w = world_type(local), *slot = task_slot_name(g, local);
        const type value = handle->task_value;
        sb_printf(o, "TIDE_HELPER int32_t tide_task_take_%s_%s(const %s *w, const int64_t id, const int32_t slot",
                  type_cname(handle), s, w);
        if (value.kind != TY_VOID) sb_printf(o, ", %s *out", c_type(value));
        sb_put(o, ")\n{\n");
        if (value.kind != TY_VOID) sb_put(o, "    (void)out;\n");
        sb_printf(o, "    const %s *v = tide_task_%s_read(w, id, slot);\n"
                     "    if (!v || !tide_task_%s_valid(w, v, slot)) return -1;\n    if (v->state != 2) return 0;\n"
                     "    switch (v->code) {\n", slot, s, s);
        for (int i = 0; i < g->tasks.count; i++) {
            const task_info *ti = &g->tasks.items[i];
            const decl *fn = ti->fn;
            if (!fn->is_async || !fn->handles_tasks || !fn->task_result_store || !task_table_in(fn, local)
                || fn->result.kind != value.kind || fn->result.decl != value.decl) continue;
            const task_info *completed = task_of(g, fn->task_result_store);
            const char *id = task_id(completed->fn);
            sb_printf(o, "    case INT64_C(%llu): {\n"
                         "        if (v->row < 0 || (uint32_t)v->row >= w->tide_tasks_%s.count) return -1;\n"
                         "        const tide_task_%s *r = tide_table_get(&w->tide_tasks_%s, &tide_columns_tasks_%s, (uint32_t)v->row, 0);\n"
                         "        if (r->frame.tide_handle_id != id || r->frame.tide_handle_slot != slot) return -1;\n",
                      (unsigned long long)task_code(fn), id, id, id, id);
            if (value.kind != TY_VOID) {
                const int indent_before = g->indent;
                g->indent = 2;
                gen_reload(g, "(*out)", "r->frame.tide_result", value);
                g->indent = indent_before;
            }
            sb_put(o, "        return 1;\n    }\n");
        }
        sb_put(o, "    default: return -1;\n    }\n}\n\n");
        }
    }
    for (int side = 0; side < 2; side++) {
        const bool local = side != 0;
        if (!g->prog->task_slots[side]) continue;
        const char *s = task_side_name(local), *w = world_type(local), *slot = task_slot_name(g, local);
        sb_printf(o, "TIDE_HELPER void tide_task_%s_cancel_work(%s *w, const int64_t id, const int32_t slot)\n{\n"
                     "    const %s *v = tide_task_%s_read(w, id, slot);\n"
                     "    if (!v || v->row < 0) return;\n    switch (v->code) {\n", s, w, slot, s);
        for (int i = 0; i < g->tasks.count; i++) {
            const task_info *ti = &g->tasks.items[i];
            const decl *fn = ti->fn;
            if (!fn->is_async || !fn->handles_tasks || !task_table_in(fn, local)) continue;
            const char *id = task_id(fn);
            sb_printf(o, "    case INT64_C(%llu): {\n"
                         "        if ((uint32_t)v->row >= w->tide_tasks_%s.count) return;\n"
                         "        tide_task_%s *r = tide_table_cell(&w->tide_tasks_%s, &tide_columns_tasks_%s, (uint32_t)v->row, 0);\n"
                         "        if (r->frame.tide_handle_id != id || r->frame.tide_handle_slot != slot) return;\n",
                      (unsigned long long)task_code(fn), id, id, id, id);
            if (local && fn->uses_workers)
                sb_printf(o, "        tide_forget_workers_%s(&r->frame, w->tide_workers);\n", type_cname(ti->frame));
            if (local && fn->uses_io)
                sb_printf(o, "        tide_forget_io_%s(&r->frame, w->tide_io);\n", type_cname(ti->frame));
            sb_put(o, "        return;\n    }\n");
        }
        sb_put(o, "    default: return;\n    }\n}\n\n");
    }
}

static void gen_task_handle_await(gen *g, expr *e, const char *name)
{
    sb *o = &g->c;
    const decl *handle = e->lhs->type.decl;
    const bool local = task_handle_local(g, handle);
    const int mark = task_mark(g);
    const char *value = made_up(g, "awaiting_task");
    hoist_spawns(g, e->lhs);
    indent(g, o);
    sb_printf(o, "%s %s = ", c_type(e->lhs->type), value);
    gen_expr(g, o, e->lhs);
    sb_put(o, ";\n");
    task_declare(g, value, e->lhs->type);
    const int k = ++g->task->points;
    line(g, o, "tide_task_handle_resume%d:;", k);
    line(g, o, "tide_f->tide_waiting_id = %s.tide_id;", value);
    line(g, o, "tide_f->tide_waiting_slot = %s.tide_slot;", value);
    if (e->type.kind != TY_VOID) line(g, o, "%s %s;", c_type(e->type), name);
    const char *status = made_up(g, "task_status");
    line(g, o, "const int32_t %s = tide_task_take_%s_%s(%s, %s.tide_id, %s.tide_slot%s%s);", status,
         type_cname(handle), task_side_name(local), local ? "tide_l" : "tide_w", value, value,
         e->type.kind == TY_VOID ? "" : ", &", e->type.kind == TY_VOID ? "" : name);
    line(g, o, "if (%s < 0) return TIDE_TASK_GONE;", status);
    line(g, o, "if (!%s) {", status);
    g->indent++;
    gen_spill_live(g);
    line(g, o, "tide_f->tide_state = %d;", k);
    line(g, o, "return %s;", g->task->local ? "tide_l->tide_frames" : "tide_w->Time.tick");
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_f->tide_waiting_id = 0;");
    line(g, o, "tide_f->%s = (%s){0};", task_slot_for(g, value, e->lhs->type), c_type(e->lhs->type));
    line(g, o, "if (0) {");
    line(g, o, "tide_resume%d:;", k);
    g->indent++;
    gen_reload_live(g);
    line(g, o, "goto tide_task_handle_resume%d;", k);
    g->indent--;
    line(g, o, "}");
    task_pop(g, mark);
    e->hoisted = e->type.kind == TY_VOID ? "0" : name;
    if (e->type.kind != TY_VOID) task_declare(g, name, e->type);
}
