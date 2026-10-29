// Experimental transient partitions. Ordinary readers always see identity rows.
// This file is compiled only by a compiler built with TIDE_PACKED_TABLES.
#if TIDE_PACKED_TABLES
static bool packed_plain(const type t, const int depth)
{
    if (depth > 32) return false;
    if (destroy_row_number(t)) return true;
    if (t.kind != TY_STRUCT && t.kind != TY_COMPONENT && t.kind != TY_SINGLETON) return false;
    if (!t.decl || t.decl->callable || t.decl->task_handle || t.decl->skin_kind) return false;
    for (int i = 0; i < t.decl->fields.count; i++)
        if (!packed_plain(t.decl->fields.items[i].type, depth + 1)) return false;
    return true;
}

static bool packed_statement(const stmt *s, int depth, bool system);

static bool packed_function(const decl *d, const int depth)
{
    if (!d || depth > 32 || d->kind != DECL_FUNCTION || d->owner || d->is_extern || d->is_async || !d->body
        || d->calls_c || d->rolls || d->starts_tasks || d->spawns || d->acts[0] || d->acts[1]
        || d->world_uses.count || touches_text(d)) return false;
    if (d->return_type.kind != TY_VOID && !packed_plain(d->return_type, 0)) return false;
    for (int i = 0; i < d->params.count; i++)
        if (!packed_plain(d->params.items[i].type, 0) || d->params.items[i].local_use) return false;
    return packed_statement(d->body, depth + 1, false);
}

static bool packed_expression(const expr *e, const int depth)
{
    if (!e) return true;
    if (depth > 64 || e->property || e->local_field || e->edge != EDGE_NONE || e->block
        || e->resource_operation || e->io_operation || e->path_operation || e->skin_operation
        || type_has_text(e->type)) return false;
    if (e->bind == BIND_CONST) return packed_expression(e->constant->value, depth + 1);
    switch (e->call) {
    case CALL_NONE: break;
    case CALL_CONSTRUCT:
        if (!destroy_row_number(e->type)) return false;
        break;
    case CALL_BUILTIN:
        // Numeric builtins only: no Debug, GUI, resource, or hidden effects.
        if (!destroy_row_number(e->type) || e->arg_mut || !e->c_callee
            || !e->object || e->object->kind != E_NAME || !str_eq_c(e->object->name, "Math")) return false;
        break;
    case CALL_FUNCTION:
        if (e->object || !packed_function(e->method, depth + 1)) return false;
        break;
    default: return false;
    }
    switch (e->kind) {
    case E_INT: case E_FLOAT: case E_BOOL: break;
    case E_NAME:
        if (e->bind != BIND_PARAM && e->bind != BIND_LOCAL && e->bind != BIND_TYPE) return false;
        break;
    case E_MEMBER: case E_CALL: case E_METHOD: case E_BINARY: case E_UNARY: case E_CONDITIONAL: break;
    default: return false;
    }
    if (e->method && e->call != CALL_FUNCTION) return false;
    if ((e->call == CALL_NONE && !packed_expression(e->object, depth + 1)) || !packed_expression(e->lhs, depth + 1)
        || !packed_expression(e->rhs, depth + 1) || !packed_expression(e->cond, depth + 1)) return false;
    for (int i = 0; i < e->args.count; i++)
        if (!packed_expression(e->args.items[i], depth + 1)) return false;
    return true;
}

static bool packed_target(const expr *e, const bool system)
{
    if (!e || !packed_plain(e->type, 0) || !packed_expression(e, 0)) return false;
    while (e->kind == E_MEMBER) e = e->object;
    if (e->kind != E_NAME) return false;
    return e->bind == BIND_LOCAL || (e->bind == BIND_PARAM && e->param->mode == PARAM_MUT
        && (!system || e->param->type.kind == TY_COMPONENT));
}

static bool packed_statement(const stmt *s, const int depth, const bool system)
{
    if (!s) return true;
    if (depth > 64) return false;
    switch (s->kind) {
    case S_BLOCK:
        for (int i = 0; i < s->stmts.count; i++)
            if (!packed_statement(s->stmts.items[i], depth + 1, system)) return false;
        return true;
    case S_VAR: return packed_plain(s->type, 0) && packed_expression(s->value, depth + 1);
    case S_ASSIGN: return !s->operator_decl && !s->property_set && packed_target(s->target, system)
                          && packed_expression(s->value, depth + 1);
    case S_EXPR: case S_RETURN: return packed_expression(s->value, depth + 1);
    case S_IF: return packed_expression(s->cond, depth + 1)
                      && packed_statement(s->then_stmt, depth + 1, system)
                      && packed_statement(s->else_stmt, depth + 1, system);
    default: return false;
    }
}

static bool packed_system(const gen *g, const decl *sys)
{
    if (sharded(g) || sys->is_view || sys->is_handler || sys->is_local || sys->entity_local
        || !sys->per_entity || !sys->body || sys->extern_system || sys->calls_c || sys->rolls
        || sys->spawns || sys->starts_tasks || sys->world_uses.count || touches_text(sys)
        || sys->acts[0] || sys->acts[1] || !system_splits(sys)) return false;
    bool writes = false;
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (p->mode == PARAM_WITH || p->mode == PARAM_WITHOUT) continue;
        if (!packed_plain(p->type, 0) || p->local_use
            || (p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON)
            || (p->mode == PARAM_MUT && p->type.kind != TY_COMPONENT)) return false;
        writes |= p->mode == PARAM_MUT;
    }
    return writes && packed_statement(sys->body, 0, true);
}

// One presence predicate initially. All other filters retain their ordinary loop.
static int packed_column(const gen *g, const decl *sys, const int a)
{
    if (!runs_on(g, sys, a) || !packed_system(g, sys) || scene_filtered(g, sys, a)) return -1;
    int result = -1;
    for (int c = 0; c < g->prog->components.count; c++) {
        if (!arch_stores(g, a, c) || !comp_sparse(g, c)
            || (!mask_has(sys->need_mask, c) && !mask_has(sys->without_mask, c))) continue;
        if (result != -1) return -1;
        result = c;
    }
    return result;
}

static bool packed_candidate(const gen *g, const decl *sys)
{
    for (int a = 0; a < g->prog->archetypes.count; a++) if (packed_column(g, sys, a) >= 0) return true;
    return false;
}

static void gen_packed_helpers(gen *g)
{
    sb *o = &g->c;
    sb_put(o, "// Private experimental controls: change mode only between ticks.\n"
              "int tide_packed_rows_mode = 0; // 0 off, 1 guarded, 2 forced\n"
              "uint64_t tide_packed_rows_passes, tide_packed_rows_count;\n"
              "static TIDE_THREAD_LOCAL int tide_packed_active;\n"
              "TIDE_HELPER void tide_packed_report(void)\n{\n"
              "    static TIDE_THREAD_LOCAL uint64_t previous;\n"
              "    static TIDE_THREAD_LOCAL bool reported;\n"
              "    const uint64_t passes = __atomic_load_n(&tide_packed_rows_passes, __ATOMIC_RELAXED);\n"
              "    if (!getenv(\"TIDE_PACKED_REPORT\") || (reported && previous == passes)) return;\n"
              "    previous = passes; reported = true;\n"
              "    fprintf(stderr, \"packed rows: %llu partitions, %llu rows\\n\",\n"
              "        (unsigned long long)passes,\n"
              "        (unsigned long long)__atomic_load_n(&tide_packed_rows_count, __ATOMIC_RELAXED));\n}\n"
              "TIDE_HELPER void tide_packed_configure(void)\n{\n"
              "    const char *mode = getenv(\"TIDE_PACKED_MODE\");\n"
              "    tide_packed_active = mode ? (strcmp(mode, \"force\") == 0 ? 2 : strcmp(mode, \"auto\") == 0 ? 1 : 0) : tide_packed_rows_mode;\n"
              "}\n\n"
              "TIDE_HELPER bool tide_packed_begin(tide_table *t, const tide_columns *c, uint32_t column, bool need)\n{\n"
              "    if (!tide_packed_active || t->count < 2u) return false;\n"
              "    if (tide_packed_active != 2) {\n"
              "        if (t->count < 65536u) return false;\n"
              "        uint32_t pass = 0u;\n"
              "        for (uint32_t row = 0u; row < t->count; row++)\n"
              "            pass += (*(const uint8_t *)tide_table_get(t, c, row, column) != 0) == need;\n"
              "        if (!pass || pass > t->count / 16u) return false;\n"
              "    }\n"
              "    (void)tide_table_partition(t, c, column, need);\n"
              "    if (!t->mapping) return false;\n"
              "    __atomic_add_fetch(&tide_packed_rows_passes, 1u, __ATOMIC_RELAXED);\n"
              "    __atomic_add_fetch(&tide_packed_rows_count, t->count, __ATOMIC_RELAXED);\n"
              "    return true;\n}\n\n"
              "TIDE_HELPER void tide_packed_end(tide_table *t, const tide_columns *c)\n{\n"
              "    if (!t->mapping) return;\n"
              "    tide_table logical = {0};\n"
              "    for (uint32_t row = 0u; row < t->count; row++) (void)tide_table_add(&logical, c);\n"
              "    for (uint32_t column = 0u; column < c->count; column++) {\n"
              "        const uint32_t width = c->sizes[column];\n"
              "        for (uint32_t chunk = 0u; chunk < logical.chunks; chunk++) {\n"
              "            uint8_t *out = tide_table_column_mut(&logical, c, chunk, column);\n"
              "            const uint32_t n = tide_table_rows(&logical, c->shift, chunk);\n"
              "            for (uint32_t i = 0u; i < n; i++) {\n"
              "                const void *in = tide_table_get(t, c, (chunk << c->shift) + i, column);\n"
              "                if (width == 4u) memcpy(out + (size_t)i * 4u, in, 4u);\n"
              "                else memcpy(out + (size_t)i * width, in, width);\n"
              "            }\n        }\n    }\n"
              "    tide_table_free(t, c);\n    *t = logical;\n}\n\n");
    for (int i = 0; i < g->prog->systems.count; i++) {
        const decl *sys = g->prog->systems.items[i];
        if (!packed_candidate(g, sys)) continue;
        const char *name = decl_cname(sys);
        sb_printf(o, "static uint32_t tide_packed_tasks_%s(const void *world, uint64_t *work)\n"
                     "{ (void)tide_tasks_%s(world, work); return 1u; }\n\n", name, name);
        sb_printf(o, "static void tide_packed_task_%s(void *world, uint32_t task, uint32_t pieces)\n{\n"
                     "    tide_world *w = world;\n    (void)task; (void)pieces;\n", name);
        for (int a = 0; a < g->prog->archetypes.count; a++) {
            const int c = packed_column(g, sys, a);
            if (c < 0) continue;
            sb_printf(o, "    (void)tide_packed_begin(&w->%s, %s, %du, %s);\n", arch_name(g, a),
                      arch_columns(g, a), arch_presence(g, a, c), mask_has(sys->need_mask, c) ? "true" : "false");
        }
        sb_printf(o, "    uint64_t work;\n    const uint32_t count = tide_tasks_%s(w, &work);\n"
                     "    for (uint32_t k = 0u; k < count; k++) tide_task_%s(w, k, 1u);\n", name, name);
        for (int a = 0; a < g->prog->archetypes.count; a++)
            if (packed_column(g, sys, a) >= 0)
                sb_printf(o, "    tide_packed_end(&w->%s, %s);\n", arch_name(g, a), arch_columns(g, a));
        sb_put(o, "}\n\n");
    }
}
#endif
