// Task results are retained by reachable handles, including handles in another
// task's result. Both the value walk and the task graph use explicit worklists:
// recursive value types and result cycles never recurse on the C stack.
typedef struct task_trace_type { type value; bool live; } task_trace_type;
typedef VEC(task_trace_type) task_trace_types;

static int task_trace_index(task_trace_types *types, const type t)
{
    if (!t.decl) return -1;
    switch (t.kind) {
    case TY_STRUCT: case TY_COMPONENT: case TY_SINGLETON: case TY_EVENT: case TY_INPUT:
    case TY_ARRAY: case TY_LIST: case TY_MAP: case TY_SET: case TY_GRID: case TY_OPTIONAL: case TY_FAILABLE: break;
    default: return -1;
    }
    for (int i = 0; i < types->count; i++)
        if (types->items[i].value.kind == t.kind && types->items[i].value.decl == t.decl) return i;
    const int i = types->count;
    const task_trace_type item = {t, t.decl->task_handle};
    vec_push(*types, item);
    if (!t.decl->task_handle)
        for (int k = 0; k < t.decl->fields.count; k++) task_trace_index(types, t.decl->fields.items[k].type);
    return i;
}

static int task_trace_live(task_trace_types *types, const type t)
{
    const int i = task_trace_index(types, t);
    return i >= 0 && types->items[i].live ? i : -1;
}

static void gen_task_trace_push(gen *g, task_trace_types *types, const type t, const char *address)
{
    const int i = task_trace_live(types, t);
    if (i >= 0) sb_printf(&g->c, "        tide_task_trace_push(trace, %s, %du);\n", address, i);
}

static void gen_task_trace_root(gen *g, task_trace_types *types, const type t, const char *address, const char *side)
{
    const int i = task_trace_live(types, t);
    if (i >= 0) sb_printf(&g->c, "    tide_task_%s_trace(w, &trace, %s, %du);\n", side, address, i);
}

static void gen_task_trace(gen *g)
{
    program *p = g->prog;
    if (!p->task_slots[0] && !p->task_slots[1]) return;
    task_trace_types types = {0};
    for (int i = 0; i < p->singletons.count; i++) {
        decl *d = p->singletons.items[i];
        if (d != p->task_slots[0] && d != p->task_slots[1]) task_trace_index(&types, (type){TY_SINGLETON, d});
    }
    for (int i = 0; i < p->components.count; i++) task_trace_index(&types, (type){TY_COMPONENT, p->components.items[i]});
    for (int i = 0; i < p->local_parts.count; i++) task_trace_index(&types, (type){TY_STRUCT, p->local_parts.items[i]});
    if (p->input) task_trace_index(&types, (type){TY_INPUT, p->input});
    for (int i = 0; i < g->tasks.count; i++) task_trace_index(&types, (type){TY_STRUCT, g->tasks.items[i].frame});
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < types.count; i++) {
            if (types.items[i].live) continue;
            const decl *d = types.items[i].value.decl;
            for (int k = 0; k < d->fields.count; k++) {
                const int child = task_trace_index(&types, d->fields.items[k].type);
                if (child >= 0 && types.items[child].live) types.items[i].live = changed = true;
            }
        }
    }
    sb *o = &g->c;
    sb_put(o, "typedef struct tide_task_trace_node { const void *value; uint32_t type; } tide_task_trace_node;\n"
              "typedef struct tide_task_trace {\n"
              "    tide_task_trace_node *nodes; size_t count, room;\n"
              "    uint8_t *marked; int32_t *pending; int32_t slots, used;\n"
              "} tide_task_trace;\n"
              "TIDE_HELPER void tide_task_trace_push(tide_task_trace *trace, const void *value, uint32_t type)\n{\n"
              "    if (trace->count == trace->room) {\n"
              "        size_t room = trace->room ? trace->room * 2u : 32u;\n"
              "        if (room <= trace->room || room > SIZE_MAX / sizeof *trace->nodes) tide_out_of_memory();\n"
              "        void *nodes = realloc(trace->nodes, room * sizeof *trace->nodes);\n"
              "        if (!nodes) tide_out_of_memory();\n"
              "        trace->nodes = nodes; trace->room = room;\n"
              "    }\n"
              "    trace->nodes[trace->count++] = (tide_task_trace_node){value, type};\n}\n\n");
    for (int side = 0; side < 2; side++) {
        const bool local = side != 0;
        if (!p->task_slots[side]) continue;
        const char *s = task_side_name(local), *w = world_type(local), *store = task_store_name(g, local);
        const char *slot = task_slot_name(g, local), *where = world_where(local);
        sb_printf(o, "TIDE_HELPER void tide_task_%s_mark(const %s *w, tide_task_trace *trace, int64_t id, int32_t index)\n{\n"
                     "    const %s *slot = tide_task_%s_read(w, id, index);\n"
                     "    if (!slot || index >= trace->slots || trace->marked[index]) return;\n"
                     "    trace->marked[index] = 1; trace->pending[trace->used++] = index;\n}\n\n", s, w, slot, s);
        sb_printf(o, "TIDE_HELPER void tide_task_%s_trace(const %s *w, tide_task_trace *trace, const void *root, uint32_t type)\n{\n"
                     "    (void)w;\n"
                     "    tide_task_trace_push(trace, root, type);\n"
                     "    while (trace->count) {\n"
                     "        const tide_task_trace_node node = trace->nodes[--trace->count];\n"
                     "        switch (node.type) {\n", s, w);
        for (int i = 0; i < types.count; i++) {
            const type t = types.items[i].value;
            if (!types.items[i].live) continue;
            sb_printf(o, "        case %du: {\n        const %s *v = node.value; (void)v;\n", i, c_type(t));
            if (t.decl->task_handle) {
                if (t.decl->task_local == local || (t.decl->task_origin && t.decl->task_origin->task_side == TASK_EITHER))
                    sb_printf(o, "        tide_task_%s_mark(w, trace, v->tide_id, v->tide_slot);\n", s);
            } else if (t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET) {
                const bool list = t.kind == TY_LIST;
                sb entry_name = {0}; sb_printf(&entry_name, "tide_map%d_entry", t.decl->index);
                const char *entry = list ? elem_ctype(list_elem(t.decl)) : entry_name.data;
                sb_printf(o, "        for (int32_t i = 0; i < tide_list_count(%s); i++) {\n"
                             "            const %s *entry = tide_list_at(%s, i, sizeof *entry);\n", list ? "*v" : "v->entries", entry, list ? "*v" : "v->entries");
                if (list) gen_task_trace_push(g, &types, list_elem(t.decl), "entry");
                else {
                    sb_put(o, "            if (!tide_map_live(entry)) continue;\n");
                    gen_task_trace_push(g, &types, t.decl->fields.items[0].type, "&entry->key");
                    if (!t.decl->is_set) gen_task_trace_push(g, &types, t.decl->fields.items[1].type, "&entry->value");
                }
                sb_put(o, "        }\n");
            } else if (t.kind == TY_ARRAY) {
                sb_printf(o, "        for (int32_t i = 0; i < %d; i++) {\n", t.decl->array_length);
                gen_task_trace_push(g, &types, t.decl->fields.items[0].type, "&v->at[i]");
                sb_put(o, "        }\n");
            } else if (t.kind == TY_GRID) {
                sb_printf(o, "        const tide_grid_dir *dir = tide_grid_dir_of(*v);\n"
                             "        if (dir) for (uint32_t i = 0; i < dir->records; i++) {\n"
                             "            const uint8_t *cells = tide_grid_chunk_at(tide_heap_read(v->at >> 30), tide_grid_records(dir)[i].block);\n"
                             "            for (uint32_t j = 0; j < tide_grid_chunk_bytes(&%s); j += sizeof(%s)) {\n", grid_shape_name(t.decl), elem_ctype(t.decl->fields.items[0].type));
                gen_task_trace_push(g, &types, t.decl->fields.items[0].type, "cells + j");
                sb_put(o, "            }\n        }\n");
            } else {
                for (int k = 0; k < t.decl->fields.count; k++) {
                    const field *f = &t.decl->fields.items[k];
                    sb address = {0}; sb_printf(&address, "&v->%s", field_cname(f));
                    gen_task_trace_push(g, &types, f->type, address.data);
                }
            }
            sb_put(o, "        break;\n        }\n");
        }
        sb_put(o, "        default: break;\n        }\n    }\n}\n\n");
        sb_printf(o, "static void tide_task_%s_collect(%s *w)\n{\n"
                     "    const int32_t slots = tide_list_count(w->%s.slots);\n"
                     "    if (!slots) return;\n"
                     "    if ((uint64_t)(uint32_t)slots > SIZE_MAX / sizeof(int32_t)) tide_out_of_memory();\n"
                     "    tide_task_trace trace = {.slots = slots};\n"
                     "    trace.marked = calloc((size_t)slots, 1); trace.pending = malloc((size_t)slots * sizeof(int32_t));\n"
                     "    if (!trace.marked || !trace.pending) tide_out_of_memory();\n"
                     "    for (int32_t i = 0; i < slots; i++) {\n"
                     "        const %s *slot = tide_list_at(w->%s.slots, i, sizeof *slot);\n"
                     "        if (slot->id && slot->state != 3 && !tide_task_%s_valid(w, slot, i)) {\n"
                     "            %s *dead = tide_list_at_mut(w->%s.slots, i, sizeof *dead);\n"
                     "            dead->state = 3; dead->row = -1;\n"
                     "        }\n"
                     "    }\n", s, w, store, slot, store, s, slot, store);
        for (int i = 0; i < p->singletons.count; i++) {
            const decl *d = p->singletons.items[i];
            if (d->is_local != local || d == p->task_slots[side]) continue;
            sb address = {0}; sb_printf(&address, "&w->%s", type_cname(d));
            gen_task_trace_root(g, &types, (type){TY_SINGLETON, (decl *)d}, address.data, s);
        }
        for (int a = 0; a < p->archetypes.count; a++) {
            if (arch_local(g, a) != local) continue;
            for (int c = 0; c < p->components.count; c++) {
                const decl *d = p->components.items[c];
                const type t = {TY_COMPONENT, (decl *)d};
                if (!arch_stores(g, a, c) || task_trace_live(&types, t) < 0) continue;
                sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
                if (comp_sparse(g, c)) sb_printf(o, "        if (!%s) continue;\n", presence_cell(g, "w", a, c, "row", false));
                sb_printf(o, "        %s value; %s;\n", type_cname(d), comp_take(g, "w", a, c, "row", "value"));
                gen_task_trace_root(g, &types, t, "&value", s);
                sb_put(o, "    }\n");
            }
        }
        for (int i = 0; local && i < p->local_parts.count; i++) {
            const decl *d = p->local_parts.items[i];
            const type t = {TY_STRUCT, (decl *)d};
            if (d->kind != DECL_COMPONENT || task_trace_live(&types, t) < 0) continue;
            sb_printf(o, "    for (uint32_t row = 0; row < w->%s.rows.count; row++) {\n", fields_table(d));
            sb address = {0}; sb_printf(&address, "tide_attached_get(&w->%s, %s, row)", fields_table(d), fields_columns(d));
            gen_task_trace_root(g, &types, t, address.data, s);
            sb_put(o, "    }\n");
        }
        if (!local && p->input && task_trace_live(&types, (type){TY_INPUT, p->input}) >= 0) {
            sb_put(o, "    for (size_t i = 0; i < sizeof w->inputs / sizeof w->inputs[0]; i++) {\n");
            gen_task_trace_root(g, &types, (type){TY_INPUT, p->input}, "&w->inputs[i]", s);
            gen_task_trace_root(g, &types, (type){TY_INPUT, p->input}, "&w->previous_inputs[i]", s);
            sb_put(o, "    }\n");
        }
        for (int i = 0; i < g->tasks.count; i++) {
            const task_info *ti = &g->tasks.items[i];
            const decl *fn = ti->fn;
            if (task_is_completion(fn) || !task_table_in(fn, local)) continue;
            const char *id = task_id(fn);
            sb_printf(o, "    for (uint32_t row = 0; row < w->tide_tasks_%s.count; row++) {\n"
                         "        const tide_task_%s *value = tide_table_get(&w->tide_tasks_%s, &tide_columns_tasks_%s, row, 0);\n"
                         "        if (value->task.wake < 0) continue;\n", id, id, id, id);
            if (fn->worker_adapter) sb_put(o, "        continue; // A worker's consumers root its input frame.\n");
            else {
                if (fn->handles_tasks) sb_printf(o, "        tide_task_%s_mark(w, &trace, value->frame.tide_handle_id, value->frame.tide_handle_slot);\n", s);
                gen_task_trace_root(g, &types, (type){TY_STRUCT, ti->frame}, "&value->frame", s);
            }
            sb_put(o, "    }\n");
        }
        sb_put(o, "    for (int32_t head = 0; head < trace.used; head++) {\n");
        sb_printf(o, "        const %s *slot = tide_list_at(w->%s.slots, trace.pending[head], sizeof *slot);\n"
                     "        if (slot->row < 0) continue;\n        switch (slot->code) {\n", slot, store);
        for (int i = 0; i < g->tasks.count; i++) {
            const task_info *ti = &g->tasks.items[i];
            const decl *fn = ti->fn;
            if (!fn->is_async || !fn->handles_tasks || !task_table_in(fn, local)) continue;
            sb_printf(o, "        case INT64_C(%llu): {\n", (unsigned long long)task_code(fn));
            for (int completed = 0; completed < 2; completed++) {
                const task_info *target = completed ? task_of(g, fn->task_result_store) : ti;
                const char *id = task_id(target->fn);
                sb_printf(o, "            if (slot->state == %d && (uint32_t)slot->row < w->tide_tasks_%s.count) {\n"
                             "                const tide_task_%s *value = tide_table_get(&w->tide_tasks_%s, &tide_columns_tasks_%s, (uint32_t)slot->row, 0);\n"
                             "                if (value->frame.tide_handle_id == slot->id && value->frame.tide_handle_slot == trace.pending[head]) {\n", completed ? 2 : 1, id, id, id, id);
                gen_task_trace_root(g, &types, (type){TY_STRUCT, target->frame}, "&value->frame", s);
                sb_put(o, "                }\n            }\n");
            }
            sb_put(o, "            break;\n        }\n");
        }
        sb_put(o, "        default: break;\n        }\n    }\n");
        // Typed completion tables are compacted from the end, then their slot
        // is recycled with a new generation at the next start.
        for (int i = 0; i < g->tasks.count; i++) {
            const task_info *ti = &g->tasks.items[i];
            if (!task_is_completion(ti->fn) || !task_table_in(ti->fn, local)) continue;
            const char *id = task_id(ti->fn);
            sb_printf(o, "    for (uint32_t row = w->tide_tasks_%s.count; row-- > 0;) {\n"
                         "        tide_task_%s *value = tide_table_cell(&w->tide_tasks_%s, &tide_columns_tasks_%s, row, 0);\n"
                         "        const int32_t index = value->frame.tide_handle_slot;\n"
                         "        %s *slot = tide_task_%s_mut(w, value->frame.tide_handle_id, index);\n"
                         "        const bool owns = slot && slot->state == 2 && slot->code == INT64_C(%llu) && slot->row == (int32_t)row;\n"
                         "        if (owns && index >= 0 && index < slots && trace.marked[index]) continue;\n"
                         "        if (owns) { slot->state = 3; slot->row = -1; }\n"
                         "        tide_release_%s(&value->frame, %s);\n"
                         "        if (row + 1u < w->tide_tasks_%s.count) {\n"
                         "            const tide_task_%s *last = tide_table_get(&w->tide_tasks_%s, &tide_columns_tasks_%s, w->tide_tasks_%s.count - 1u, 0);\n"
                         "            tide_task_%s_bind(w, last->frame.tide_handle_id, last->frame.tide_handle_slot, (int32_t)row);\n"
                         "        }\n"
                         "        tide_table_remove_row(&w->tide_tasks_%s, &tide_columns_tasks_%s, row);\n    }\n",
                      id, id, id, id, slot, s, (unsigned long long)task_code(ti->fn->task_result_store),
                      type_cname(ti->frame), where, id, id, id, id, id, s, id, id);
        }
        sb_printf(o, "    for (int32_t i = 0; i < slots; i++) {\n"
                     "        const %s *slot = tide_list_at(w->%s.slots, i, sizeof *slot);\n"
                     "        if (trace.marked[i] || !slot->id) continue;\n", slot, store);
        // Only worker adapter starts can be abandoned merely because their
        // last handle vanished. Ordinary async calls retain fire-and-forget.
        for (int i = 0; i < g->tasks.count; i++) {
            const decl *fn = g->tasks.items[i].fn;
            if (fn->is_async && fn->worker_adapter && task_table_in(fn, local))
                sb_printf(o, "        if (slot->state == 1 && slot->code == INT64_C(%llu)) tide_task_%s_cancel(w, slot->id, i);\n", (unsigned long long)task_code(fn), s);
        }
        sb_printf(o, "        slot = tide_list_at(w->%s.slots, i, sizeof *slot);\n"
                     "        if (slot->state != 3 || slot->row >= 0) continue;\n"
                     "        %s *dead = tide_list_at_mut(w->%s.slots, i, sizeof *dead);\n"
                     "        *dead = (%s){.row = -1, .next = w->%s.free};\n"
                     "        w->%s.free = i + 1;\n    }\n"
                     "    int32_t kept = slots;\n"
                     "    while (kept) {\n"
                     "        const %s *slot = tide_list_at(w->%s.slots, kept - 1, sizeof *slot);\n"
                     "        if (slot->id) break;\n"
                     "        kept--;\n"
                     "    }\n"
                     "    if (kept != slots) {\n"
                     "        tide_list_resize(&w->%s.slots, kept, sizeof(%s), %s);\n"
                     "        w->%s.free = 0;\n"
                     "        for (int32_t i = kept; i-- > 0;) {\n"
                     "            const %s *slot = tide_list_at(w->%s.slots, i, sizeof *slot);\n"
                     "            if (slot->id) continue;\n"
                     "            %s *dead = tide_list_at_mut(w->%s.slots, i, sizeof *dead);\n"
                     "            dead->next = w->%s.free; w->%s.free = i + 1;\n"
                     "        }\n"
                     "    }\n"
                     "    free(trace.nodes); free(trace.marked); free(trace.pending);\n}\n\n",
                  store, slot, store, slot, store, store, slot, store, store, slot, where,
                  store, slot, store, slot, store, store, store);
    }
}
