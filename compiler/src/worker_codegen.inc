static bool program_workers(const program *p)
{
    for (int i = 0; i < p->decls.count; i++) if (p->decls.items[i]->is_worker) return true;
    return false;
}

static bool worker_has_storage(const type t)
{
    if (t.kind == TY_STRING || t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET) return true;
    if (t.kind != TY_STRUCT && t.kind != TY_EVENT && t.kind != TY_ARRAY
        && t.kind != TY_OPTIONAL && t.kind != TY_FAILABLE) return false;
    for (int i = 0; i < t.decl->fields.count; i++)
        if (worker_has_storage(t.decl->fields.items[i].type)) return true;
    return false;
}

// Fully materialize even packed/immutable lists. Worker captures may outlive a
// pack replacement, the current scratch arena and every source world.
static void gen_worker_list_copy(gen *g, const char *place, const char *element, const int depth)
{
    sb *o = &g->c;
    sb_printf(o, "    { bool tide_copied%d; const tide_list tide_source%d = %s;\n", depth, depth, place);
    sb_printf(o, "      void *tide_flat%d = tide_list_flatten(tide_source%d, sizeof(%s), false, &tide_copied%d);\n", depth, depth, element, depth);
    sb_printf(o, "      %s = tide_list_from(tide_flat%d, tide_list_count(tide_source%d), sizeof(%s));\n", place, depth, depth, element);
    sb_printf(o, "      tide_list_unflatten(tide_source%d, tide_flat%d, sizeof(%s), false, tide_copied%d); }\n", depth, depth, element, depth);
}

static void gen_worker_snapshot(gen *g, const type t, const char *place, const int depth)
{
    sb *o = &g->c;
    if (!worker_has_storage(t)) return;
    if (t.kind == TY_STRING) {
        sb_printf(o, "    %s = tide_text_temp(tide_text_view(%s));\n", place, place);
    } else if (t.kind == TY_ARRAY) {
        sb_printf(o, "    for (int32_t tide_a%d = 0; tide_a%d < %d; tide_a%d++) {\n", depth, depth, t.decl->array_length, depth);
        sb child = {0}; sb_printf(&child, "%s.at[tide_a%d]", place, depth);
        gen_worker_snapshot(g, t.decl->fields.items[0].type, child.data, depth + 1);
        sb_put(o, "    }\n");
    } else if (t.kind == TY_STRUCT || t.kind == TY_EVENT || t.kind == TY_OPTIONAL || t.kind == TY_FAILABLE) {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            sb child = {0}; sb_printf(&child, "%s.%s", place, field_cname(f));
            if ((t.kind == TY_OPTIONAL || t.kind == TY_FAILABLE) && f->type.kind == TY_STRING)
                sb_printf(o, "    %s = tide_text_view(tide_text_temp(%s));\n", child.data, child.data);
            else gen_worker_snapshot(g, f->type, child.data, depth + 1);
        }
    } else if (t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET) {
        const bool list = t.kind == TY_LIST;
        sb entry = {0};
        if (list) sb_put(&entry, elem_ctype(list_elem(t.decl)));
        else sb_printf(&entry, "tide_map%d_entry", t.decl->index);
        sb entries = {0}; sb_printf(&entries, "%s%s", place, list ? "" : ".entries");
        gen_worker_list_copy(g, entries.data, entry.data, depth);
        if (!list) {
            sb index = {0}; sb_printf(&index, "%s.index", place);
            gen_worker_list_copy(g, index.data, "int32_t", depth);
        }
        if (list ? !worker_has_storage(list_elem(t.decl)) :
            (!worker_has_storage(t.decl->fields.items[0].type) &&
             (t.decl->is_set || !worker_has_storage(t.decl->fields.items[1].type)))) return;
        sb_printf(o, "    for (int32_t tide_i%d = 0; tide_i%d < tide_list_count(%s); tide_i%d++) {\n", depth, depth, entries.data, depth);
        sb_printf(o, "        %s *tide_e%d = tide_list_at(%s, tide_i%d, sizeof(%s));\n", entry.data, depth, entries.data, depth, entry.data);
        if (!list) sb_printf(o, "        if (!tide_map_live(tide_e%d)) continue;\n", depth);
        sb child = {0}; sb_printf(&child, list ? "(*tide_e%d)" : "tide_e%d->key", depth);
        gen_worker_snapshot(g, t.decl->fields.items[0].type, child.data, depth + 1);
        if (!list && !t.decl->is_set) {
            child = (sb){0}; sb_printf(&child, "tide_e%d->value", depth);
            gen_worker_snapshot(g, t.decl->fields.items[1].type, child.data, depth + 1);
        }
        sb_put(o, "    }\n");
    }
}

static void gen_worker_own(gen *g, const type t, const char *place, const char *where)
{
    if (!worker_has_storage(t)) return;
    sb *o = &g->c;
    if (t.kind == TY_STRING) sb_printf(o, "    tide_text_own(&%s, %s);\n", place, where);
    else if (t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET) {
        const bool list = t.kind == TY_LIST;
        sb element = {0}, entries = {0};
        if (list) {
            sb_put(&element, elem_ctype(list_elem(t.decl)));
            sb_printf(o, "    tide_list_own(&%s, sizeof(%s), %s);\n", place, element.data, where);
        } else {
            sb_printf(&element, "tide_map%d_entry", t.decl->index);
            sb_printf(o, "    tide_map_own(&%s, &tide_map%d_kind, %s);\n", place, t.decl->index, where);
        }
        sb_printf(&entries, "%s%s", place, list ? "" : ".entries");
        if (list ? !worker_has_storage(list_elem(t.decl)) :
            (!worker_has_storage(t.decl->fields.items[0].type) &&
             (t.decl->is_set || !worker_has_storage(t.decl->fields.items[1].type)))) return;
        const char *index = made_up(g, "worker_i"), *entry = made_up(g, "worker_e");
        sb_printf(o, "    for (int32_t %s = 0; %s < tide_list_count(%s); %s++) {\n", index, index, entries.data, index);
        sb_printf(o, "        %s *%s = tide_list_at(%s, %s, sizeof(%s));\n", element.data, entry, entries.data, index, element.data);
        if (!list) sb_printf(o, "        if (!tide_map_live(%s)) continue;\n", entry);
        sb child = {0}; sb_printf(&child, list ? "(*%s)" : "%s->key", entry);
        gen_worker_own(g, t.decl->fields.items[0].type, child.data, where);
        if (!list && !t.decl->is_set) {
            child = (sb){0}; sb_printf(&child, "%s->value", entry);
            gen_worker_own(g, t.decl->fields.items[1].type, child.data, where);
        }
        sb_put(o, "    }\n");
    }
    else if (t.kind == TY_ARRAY) {
        sb_put(o, "    {\n");
        const char *index = made_up(g, "worker_i");
        sb_printf(o, "    for (int32_t %s = 0; %s < %d; %s++) {\n", index, index, t.decl->array_length, index);
        sb child = {0}; sb_printf(&child, "%s.at[%s]", place, index);
        gen_worker_own(g, t.decl->fields.items[0].type, child.data, where);
        sb_put(o, "    }\n");
        sb_put(o, "    }\n");
    } else if (t.kind == TY_OPTIONAL || t.kind == TY_FAILABLE || t.kind == TY_STRUCT || t.kind == TY_EVENT) {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            sb child = {0}; sb_printf(&child, "%s.%s", place, field_cname(f));
            if ((t.kind == TY_OPTIONAL || t.kind == TY_FAILABLE) && f->type.kind == TY_STRING) {
                sb_printf(o, "    { tide_text owned = tide_text_temp(%s);\n", child.data);
                sb_printf(o, "      tide_text_own(&owned, %s);\n      %s = tide_text_view(owned); }\n", where, child.data);
            } else gen_worker_own(g, f->type, child.data, where);
        }
    }
    else sb_printf(o, "    tide_own_%s(&%s, %s);\n", type_cname(t.decl), place, where);
}

static void gen_workers(gen *g)
{
    sb *o = &g->c;
    if (program_workers(g->prog) && g->prog->grids.count) {
        sb_put(o, "TIDE_HELPER tide_grid tide_worker_grid_capture(tide_grid value, const tide_grid_shape *shape, tide_heap *target, uint32_t where)\n{\n"
                  "    const tide_grid_dir *source = tide_grid_dir_of(value);\n"
                  "    if (!source) return (tide_grid){0};\n"
                  "    const tide_heap *heap = tide_heap_read(tide_handle_where(value.at));\n"
                  "    const uint64_t bytes = sizeof(*source) + (uint64_t)source->room * sizeof(tide_grid_record) + (uint64_t)source->slots * sizeof(uint32_t);\n"
                  "    if (bytes > UINT32_MAX - sizeof(tide_block)) tide_out_of_memory();\n"
                  "    const uint64_t at = tide_heap_alloc(target, (uint32_t)bytes);\n"
                  "    void *directory = tide_heap_write(target, at) + 1;\n"
                  "    source = tide_grid_dir_of(value);\n"
                  "    memcpy(directory, source, (size_t)bytes);\n"
                  "    const uint32_t chunk_bytes = tide_grid_chunk_bytes(shape);\n"
                  "    const uint32_t records = source->records;\n"
                  "    for (uint32_t i = 0; i < records; i++) {\n"
                  "        const uint64_t original = tide_grid_records(tide_grid_dir_of(value))[i].block;\n"
                  "        const uint64_t cells = tide_heap_alloc(target, chunk_bytes - (uint32_t)sizeof(tide_block));\n"
                  "        void *data = tide_heap_write(target, cells);\n"
                  "        memcpy(data, tide_grid_chunk_at(heap, original), chunk_bytes);\n"
                  "        tide_grid_dir *copy = (tide_grid_dir *)(void *)(tide_heap_write(target, at) + 1);\n"
                  "        tide_grid_records(copy)[i].block = cells;\n"
                  "    }\n"
                  "    return (tide_grid){tide_handle_of(where, at)};\n}\n\n");
    }
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *fn = g->prog->decls.items[i];
        if (!fn->is_worker) continue;
        const char *id = routine_cname(fn);
        sb_printf(o, "typedef struct tide_worker_%s {\n    tide_heap input_heap, output_heap;\n    const tide_jobs *jobs;\n", id);
        for (int k = 0; k < fn->params.count; k++) sb_printf(o, "    %s input%d;\n", elem_ctype(fn->params.items[k].type), k);
        if (fn->result.kind != TY_VOID) sb_printf(o, "    %s result;\n", elem_ctype(fn->result));
        sb_printf(o, "} tide_worker_%s;\n\n", id);
        sb_printf(o, "static void tide_worker_release_%s(void *context)\n{\n    tide_worker_%s *v = context;\n    tide_heap_free(&v->input_heap);\n    tide_heap_free(&v->output_heap);\n    free(v);\n}\n\n", id, id);
        sb_printf(o, "static void tide_worker_run_%s(void *context)\n{\n    tide_worker_%s *v = context;\n", id, id);
        sb_put(o, "    tide_heap *saved_match = tide_world_heaps[TIDE_IN_MATCH], *saved_local = tide_world_heaps[TIDE_IN_LOCAL];\n"
                  "    const uint64_t mark = tide_scratch_mark();\n    tide_text_use(&v->input_heap, &v->output_heap);\n"
                  "    const tide_jobs *previous = tide_worker_enter(v->jobs);\n");
        if (fn->result.kind == TY_STRING) sb_put(o, "    v->result = tide_text_temp(");
        else if (fn->result.kind != TY_VOID) sb_put(o, "    v->result = ");
        else sb_put(o, "    ");
        sb_printf(o, "%s(", id);
        for (int k = 0; k < fn->params.count; k++) {
            if (k) sb_put(o, ", ");
            if (fn->params.items[k].type.kind == TY_STRING) sb_printf(o, "tide_text_view(v->input%d)", k);
            else sb_printf(o, "v->input%d", k);
        }
        sb_put(o, fn->result.kind == TY_STRING ? "));\n" : ");\n");
        if (fn->result.kind != TY_VOID) {
            sb_put(o, "    if (!tide_worker_cancelled()) {\n");
            gen_worker_snapshot(g, fn->result, "v->result", 0);
            gen_worker_own(g, fn->result, "v->result", "TIDE_IN_LOCAL");
            sb_put(o, "    }\n");
        }
        sb_put(o, "    tide_worker_leave(previous);\n    if (!mark) tide_scratch_free();\n    else tide_scratch_reset(mark);\n    tide_text_use(saved_match, saved_local);\n}\n\n");
        sb_printf(o, "TIDE_HELPER void tide_worker_begin_%s(tide_local *local, uint32_t ticket", id);
        for (int k = 0; k < fn->params.count; k++) sb_printf(o, ", %s input%d", c_type(fn->params.items[k].type), k);
        sb_printf(o, ")\n{\n    const uint64_t began = tide_worker_transfer_begin(local->tide_workers);\n    tide_worker_%s *v = tide_alloc_zeroed(1, sizeof *v);\n", id);
        sb_put(o, "    const uint64_t mark = tide_scratch_mark();\n"
                  "    tide_heap *saved_match = tide_world_heaps[TIDE_IN_MATCH], *saved_local = tide_world_heaps[TIDE_IN_LOCAL];\n"
                  "    v->jobs = local->tide_worker_jobs;\n");
        for (int k = 0; k < fn->params.count; k++) {
            const type t = fn->params.items[k].type;
            if (t.kind == TY_GRID) {
                sb_printf(o, "    v->input%d = tide_worker_grid_capture(input%d, &%s, &v->input_heap, TIDE_IN_MATCH);\n", k, k, grid_shape_name(t.decl));
                continue;
            }
            sb_printf(o, t.kind == TY_STRING ? "    v->input%d = tide_text_temp(input%d);\n" : "    v->input%d = input%d;\n", k, k);
            sb place = {0}; sb_printf(&place, "v->input%d", k);
            gen_worker_snapshot(g, t, place.data, 0);
        }
        sb_put(o, "    tide_text_use(&v->input_heap, NULL);\n");
        for (int k = 0; k < fn->params.count; k++) {
            sb place = {0}; sb_printf(&place, "v->input%d", k);
            gen_worker_own(g, fn->params.items[k].type, place.data, "TIDE_IN_MATCH");
        }
        sb_put(o, "    tide_text_use(saved_match, saved_local);\n    tide_scratch_reset(mark);\n");
        sb_printf(o, "    tide_worker_transferred(local->tide_workers, sizeof *v + (uint64_t)v->input_heap.used, began, false);\n    tide_worker_submit(local->tide_workers, ticket, tide_worker_run_%s, tide_worker_release_%s, v, sizeof *v + (uint64_t)v->input_heap.used);\n}\n\n", id, id);
        sb_printf(o, "TIDE_HELPER bool tide_worker_take_%s(tide_local *local, uint32_t ticket", id);
        if (fn->result.kind != TY_VOID) sb_printf(o, ", %s *out", c_type(fn->result));
        sb_printf(o, ")\n{\n    if (!tide_worker_transfer_ready(local->tide_workers)) return false;\n    tide_worker_%s *v = tide_worker_poll(local->tide_workers, ticket);\n    if (!v) return false;\n    const uint64_t began = tide_worker_transfer_begin(local->tide_workers);\n", id);
        if (fn->result.kind != TY_VOID) {
            sb_put(o, "    tide_heap *saved_match = tide_world_heaps[TIDE_IN_MATCH], *saved_local = tide_world_heaps[TIDE_IN_LOCAL];\n"
                      "    tide_text_use(&v->input_heap, &v->output_heap);\n");
            sb_printf(o, "    %s result = v->result;\n", elem_ctype(fn->result));
            gen_worker_snapshot(g, fn->result, "result", 0);
            sb_put(o, fn->result.kind == TY_STRING ? "    *out = tide_text_view(result);\n" : "    *out = result;\n");
            sb_put(o, "    tide_text_use(saved_match, saved_local);\n");
        }
        sb_put(o, "    tide_worker_transferred(local->tide_workers, v->output_heap.used, began, true);\n    tide_worker_forget(local->tide_workers, ticket);\n    return true;\n}\n\n");
    }
}

static bool worker_argument_waits(const expr *e)
{
    if (!e) return false;
    if (e->kind == E_AWAIT) return true;
    if (worker_argument_waits(e->object) || worker_argument_waits(e->lhs)
        || worker_argument_waits(e->rhs) || worker_argument_waits(e->cond)) return true;
    for (int i = 0; i < e->args.count; i++) if (worker_argument_waits(e->args.items[i])) return true;
    for (int i = 0; i < e->inits.count; i++) if (worker_argument_waits(e->inits.items[i].value)) return true;
    return false;
}

static void gen_worker_await(gen *g, expr *e, const char *name)
{
    sb *o = &g->c;
    gen_task *t = g->task;
    const expr *call = e->lhs;
    const decl *fn = call->method;
    bool grids = false, ordered = false, waits = false;
    for (int i = 0; i < call->args.count; i++) {
        grids |= fn->params.items[i].type.kind == TY_GRID;
        waits |= worker_argument_waits(call->args.items[i]);
        ordered |= holds_writer(call->args.items[i]) || waits;
    }
    const int k = ++t->points;
    sb ticket_name = {0};
    sb_printf(&ticket_name, "tide_worker_ticket%d", k);
    const char *ticket = task_slot_for(g, ticket_name.data, (type){TY_INT, NULL});
    line(g, o, "tide_f->%s = 0;", ticket);
    line(g, o, "tide_worker_resume%d:;", k);
    line(g, o, "if (!tide_f->%s) {", ticket);
    g->indent++;
    if (waits) line(g, o, "if (!tide_worker_can_reserve(tide_l->tide_workers)) {");
    else {
        line(g, o, "tide_f->%s = (int32_t)tide_worker_reserve(&tide_l->tide_workers, tide_l->tide_worker_jobs);", ticket);
        line(g, o, "if (!tide_f->%s) {", ticket);
    }
    g->indent++;
    gen_spill_live(g);
    line(g, o, "tide_f->tide_state = %d;", k);
    line(g, o, "return tide_l->tide_frames + 1;");
    g->indent--;
    line(g, o, "}");
    const int mark = task_mark(g);
    const char **inputs = NULL;
    if ((grids && ordered) || waits) {
        // Freeze each argument in written order. A grid captured before a later
        // await belongs to a hidden frame slot until the invocation is ready.
        inputs = arena_alloc(sizeof *inputs * (size_t)call->args.count);
        for (int turn = 0; turn < call->args.count; turn++) {
            const int i = arg_in_turn(call, turn);
            expr *arg = call->args.items[i];
            const type input = fn->params.items[i].type;
            const char *temp = made_up(g, "worker_input");
            inputs[i] = temp;
            hoist_spawns(g, arg);
            indent(g, o);
            sb_printf(o, "%s %s = ", c_type(input), temp);
            if (input.kind == TY_GRID) {
                sb_put(o, "tide_worker_grid_capture(");
                gen_as(g, o, arg, input);
                sb_printf(o, ", &%s, &tide_l->heap, TIDE_IN_LOCAL);\n", grid_shape_name(input.decl));
                line(g, o, "tide_f->%s = %s;", task_slot_for(g, temp, input), temp);
            } else {
                gen_value_of(g, o, arg, input);
                sb_put(o, ";\n");
            }
            task_declare(g, temp, input);
        }
    } else hoist_arguments(g, call);
    if (waits) {
        // Restoring a frame abandons transient reservations. Arguments already
        // evaluated before a later await stay in its frame and are not replayed.
        const int r = ++t->points;
        line(g, o, "tide_worker_submit%d:;", r);
        line(g, o, "if (!tide_f->%s) {", ticket);
        g->indent++;
        line(g, o, "tide_f->%s = (int32_t)tide_worker_reserve(&tide_l->tide_workers, tide_l->tide_worker_jobs);", ticket);
        line(g, o, "if (!tide_f->%s) {", ticket);
        g->indent++;
        gen_spill_live(g);
        line(g, o, "tide_f->tide_state = %d;", r);
        line(g, o, "return tide_l->tide_frames + 1;");
        g->indent--;
        line(g, o, "}");
        g->indent--;
        line(g, o, "}");
        line(g, o, "if (0) {");
        line(g, o, "tide_resume%d:;", r);
        g->indent++;
        gen_reload_live(g);
        line(g, o, "goto tide_worker_submit%d;", r);
        g->indent--;
        line(g, o, "}");
    }
    indent(g, o);
    sb_printf(o, "tide_worker_begin_%s(tide_l, (uint32_t)tide_f->%s", routine_cname(fn), ticket);
    for (int i = 0; i < call->args.count; i++) {
        sb_put(o, ", ");
        if (inputs) sb_put(o, inputs[i]);
        else gen_as(g, o, call->args.items[i], call->arg_want.items[i]);
    }
    sb_put(o, ");\n");
    if (inputs) {
        for (int i = 0; i < call->args.count; i++) {
            const type input = fn->params.items[i].type;
            if (input.kind != TY_GRID) continue;
            line(g, o, "tide_grid_release(&tide_f->%s, &%s, TIDE_IN_LOCAL);",
                 task_slot_for(g, inputs[i], input), grid_shape_name(input.decl));
        }
    }
    task_pop(g, mark);
    g->indent--;
    line(g, o, "}");
    if (e->type.kind != TY_VOID) line(g, o, "%s %s;", c_type(e->type), name);
    line(g, o, "if (!tide_worker_take_%s(tide_l, (uint32_t)tide_f->%s%s%s)) {", routine_cname(fn), ticket,
         e->type.kind == TY_VOID ? "" : ", &", e->type.kind == TY_VOID ? "" : name);
    g->indent++;
    gen_spill_live(g);
    line(g, o, "tide_f->tide_state = %d;", k);
    line(g, o, "return tide_l->tide_frames + 1;");
    g->indent--;
    line(g, o, "}");
    line(g, o, "tide_f->%s = 0;", ticket);
    e->hoisted = e->type.kind == TY_VOID ? "0" : name;
    line(g, o, "if (0) {");
    line(g, o, "tide_resume%d:;", k);
    g->indent++;
    gen_reload_live(g);
    line(g, o, "goto tide_worker_resume%d;", k);
    g->indent--;
    line(g, o, "}");
    if (e->type.kind != TY_VOID) task_declare(g, name, e->type);
}

static void gen_frame_workers(gen *g, const task_info *ti)
{
    if (!ti->fn->uses_workers) return;
    const char *name = type_cname(ti->frame);
    sb *o = &g->c;
    for (int forget = 0; forget < 2; forget++) {
        sb_printf(o, "TIDE_HELPER void tide_%s_workers_%s(%s *v%s)\n{\n",
                  forget ? "forget" : "reset", name, name, forget ? ", tide_workers *workers" : "");
        for (int k = 0; k < ti->frame->fields.count; k++) {
            const field *f = &ti->frame->fields.items[k];
            if (f->name.len < 18 || memcmp(f->name.ptr, "tide_worker_ticket", 18) != 0) continue;
            if (forget) sb_printf(o, "    tide_worker_forget(workers, (uint32_t)v->%s);\n", field_cname(f));
            sb_printf(o, "    v->%s = 0;\n", field_cname(f));
        }
        for (int i = 0; i < ti->fn->awaits.count; i++) {
            const task_info *child = task_of(g, ti->fn->awaits.items[i]);
            if (!child->fn->uses_workers) continue;
            sb_printf(o, "    tide_%s_workers_%s(&v->tide_await_%s%s);\n",
                      forget ? "forget" : "reset", type_cname(child->frame), task_id(child->fn), forget ? ", workers" : "");
        }
        sb_put(o, "}\n\n");
    }
}

static void gen_reset_worker_tables(gen *g, const char *local, const char *pad, const bool forget)
{
    for (int i = 0; i < g->tasks.count; i++) {
        const task_info *ti = &g->tasks.items[i];
        if (!task_table_in(ti->fn, true) || !ti->fn->uses_workers) continue;
        const char *id = task_id(ti->fn);
        sb *o = &g->c;
        sb_printf(o, "%sfor (uint32_t i = 0; i < %s->tide_tasks_%s.count; i++) {\n", pad, local, id);
        sb_printf(o, "%s    tide_task_%s *task = tide_table_cell(&%s->tide_tasks_%s, &tide_columns_tasks_%s, i, 0);\n", pad, id, local, id, id);
        if (forget) sb_printf(o, "%s    tide_forget_workers_%s(&task->frame, %s->tide_workers);\n", pad, type_cname(ti->frame), local);
        else sb_printf(o, "%s    tide_reset_workers_%s(&task->frame);\n", pad, type_cname(ti->frame));
        sb_printf(o, "%s}\n", pad);
    }
}
