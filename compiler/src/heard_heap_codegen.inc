static type heard_path_type(const heap_path *p)
{
    if (p->list) return (type){TY_LIST, (decl *)p->list};
    if (p->map) return (type){p->map->is_set ? TY_SET : TY_MAP, (decl *)p->map};
    if (p->grid) return (type){TY_GRID, (decl *)p->grid};
    if (p->array) return (type){TY_ARRAY, (decl *)p->array};
    return (type){TY_STRING, NULL};
}

static uint64_t heard_field_readers(const gen *g, const decl *event, const decl *value, const char *path)
{
    uint64_t readers = 0;
    for (int i = 0; i < event->listeners.count; i++) {
        const decl *h = event->listeners.items[i];
        for (int k = 0; k < h->params.count; k++) {
            const param *p = &h->params.items[k];
            if (p->type.decl == value && heard_reads(g, p, path)) readers |= UINT64_C(1) << i;
        }
    }
    return readers;
}

static bool heard_query_reads(const gen *g, const param *shape, const int binding, const char *path)
{
    int number = 0;
    for (int i = 0; i < binding; i++) if (param_binding(&shape->query.items[i])) number++;
    for (int i = 0; i < g->prog->handlers.count; i++) {
        const decl *h = g->prog->handlers.items[i];
        for (int k = 0; h->listens && k < h->params.count; k++) {
            const param *q = &h->params.items[k];
            if (q->type.kind != TY_QUERY || !q->query_rows || !same_rows(shape, q)) continue;
            int at = 0;
            for (int n = 0; n < q->query.count; n++) {
                const param *p = &q->query.items[n];
                if (!param_binding(p)) continue;
                if (at++ == number && heard_reads(g, p, path)) return true;
            }
        }
    }
    return false;
}

static bool heard_value_heap(const gen *g, const decl *event, const decl *value)
{
    path_list paths = {0}; text_paths(value, "", &paths);
    for (int i = 0; i < paths.count; i++) if (heard_field_readers(g, event, value, paths.items[i].path)) return true;
    return false;
}

static bool heard_event_heap(const gen *g, const decl *event)
{
    if (heard_value_heap(g, event, event)) return true;
    const type_kind kinds[] = {TY_COMPONENT, TY_SINGLETON};
    for (int n = 0; n < 2; n++) {
        const decl_list values = heard_data(event, kinds[n]);
        for (int i = 0; i < values.count; i++) if (heard_value_heap(g, event, values.items[i])) return true;
    }
    return false;
}

static bool heard_query_heap(const gen *g, const param *q)
{
    for (int n = 0; n < q->query.count; n++) {
        const param *p = &q->query.items[n];
        if (!param_binding(p)) continue;
        path_list paths = {0}; text_paths(p->type.decl, "", &paths);
        for (int i = 0; i < paths.count; i++) if (heard_query_reads(g, q, n, paths.items[i].path)) return true;
    }
    return false;
}

static void collect_heard_codecs(gen *g)
{
    const program *prog = g->prog;
    if (prog->visibility_state) {
        for (int i = 0; i < prog->singletons.count; i++) {
            const decl *d = prog->singletons.items[i];
            if (!d->is_local && d != prog->visibility_state && d != prog->hierarchy[0])
                client_codec_type(&g->value_codec, (type){TY_SINGLETON, (decl *)d});
        }
        for (int i = 0; i < prog->components.count; i++) {
            const decl *d = prog->components.items[i];
            if (!d->is_local) client_codec_type(&g->value_codec, (type){TY_COMPONENT, (decl *)d});
        }
        if (prog->input) client_codec_type(&g->value_codec, (type){TY_INPUT, prog->input});
    }
    for (int e = 0; e < prog->events.count; e++) {
        const decl *event = prog->events.items[e];
        if (!event->listeners.count) continue;
        const type_kind kinds[] = {TY_EVENT, TY_COMPONENT, TY_SINGLETON};
        for (int n = 0; n < 3; n++) {
            decl_list values = {0};
            if (!n) vec_push(values, event);
            else values = heard_data(event, kinds[n]);
            for (int k = 0; k < values.count; k++) {
                path_list paths = {0}; text_paths(values.items[k], "", &paths);
                for (int i = 0; i < paths.count; i++) if (heard_field_readers(g, event, values.items[k], paths.items[i].path)) {
                    client_codec_type(&g->value_codec, heard_path_type(&paths.items[i]));
                    g->heard_heap = true;
                }
            }
        }
    }
    for (int n = 0; n < g->heard_rows.count; n++) {
        const param *q = g->heard_rows.items[n];
        for (int k = 0; k < q->query.count; k++) {
            const param *p = &q->query.items[k];
            if (!param_binding(p)) continue;
            path_list paths = {0}; text_paths(p->type.decl, "", &paths);
            for (int i = 0; i < paths.count; i++) if (heard_query_reads(g, q, k, paths.items[i].path)) {
                client_codec_type(&g->value_codec, heard_path_type(&paths.items[i]));
                g->heard_heap = true;
            }
        }
    }
}

// The fixed record contains no handles. Its optional tail owns serialized
// values, so ordinary journal copy/take/replay/free already owns their lifetime.
static void gen_heard_capture(gen *g, sb *o, const decl *event, const decl *d, const char *value,
                              const param *query, const int binding, const bool decode)
{
    path_list paths = {0}; text_paths(d, "", &paths);
    for (int i = 0; i < paths.count; i++) {
        const heap_path *p = &paths.items[i];
        uint64_t readers = event ? heard_field_readers(g, event, d, p->path) : 0;
        if (query ? !heard_query_reads(g, query, binding, p->path) : !readers) continue;
        const int codec = client_codec_id(&g->value_codec, heard_path_type(p));
        sb when = {0};
        if (query || d == event) sb_put(&when, "true");
        else sb_printf(&when, "!!(handlers & UINT64_C(0x%llX))", (unsigned long long)readers);
        if (decode) {
            sb_printf(o, "    { const uint8_t present = tide_read_u8(&in);\n"
                         "      if (present > 1u || (write ? ((%s) && !present) : present != (uint8_t)(%s))) return false;\n",
                      when.data, when.data);
            sb_printf(o, "      if (present && !tide_client_get%d(&in, &%s.%s, TIDE_IN_MATCH, 0u, budget)) return false; }\n",
                      codec, value, p->path);
        } else {
            sb_printf(o, "    tide_write_u8(&tide_captured, (uint8_t)(%s));\n", when.data);
            sb_printf(o, "    if (%s) tide_client_put%d(&tide_captured, &%s.%s, 0u);\n", when.data, codec, value, p->path);
        }
    }
}

static void gen_heard_tail(sb *o, const char *fixed)
{
    sb_printf(o, "    if (tide_captured.overflow || (uint64_t)%s + 4u + tide_captured.size > UINT32_MAX - 7u) tide_out_of_memory();\n", fixed);
}

static void gen_heard_tail_copy(sb *o, const char *fixed)
{
    sb_printf(o, "    memcpy((uint8_t *)r + %s, &tide_captured.size, 4u);\n", fixed);
    sb_printf(o, "    if (tide_captured.size) memcpy((uint8_t *)r + %s + 4u, tide_captured.data, tide_captured.size);\n"
                 "    free(tide_captured.data);\n", fixed);
}

static void gen_heard_capture_schema(const gen *g, sb *types)
{
    if (!g->heard_heap) return;
    sb_put(types, "// Heap capture codec 1\n");
    for (int e = 0; e < g->prog->events.count; e++) {
        const decl *event = g->prog->events.items[e];
        if (!event->listeners.count) continue;
        const type_kind kinds[] = {TY_EVENT, TY_COMPONENT, TY_SINGLETON};
        for (int n = 0; n < 3; n++) {
            decl_list values = {0};
            if (!n) vec_push(values, event);
            else values = heard_data(event, kinds[n]);
            for (int k = 0; k < values.count; k++) {
                path_list paths = {0}; text_paths(values.items[k], "", &paths);
                for (int i = 0; i < paths.count; i++) {
                    const uint64_t readers = heard_field_readers(g, event, values.items[k], paths.items[i].path);
                    if (readers) sb_printf(types, "// Capture %d %s.%s %llx\n", e, type_cname(values.items[k]),
                                           paths.items[i].path, (unsigned long long)readers);
                }
            }
        }
    }
    for (int n = 0; n < g->heard_rows.count; n++) {
        const param *q = g->heard_rows.items[n];
        for (int k = 0; k < q->query.count; k++) {
            if (!param_binding(&q->query.items[k])) continue;
            path_list paths = {0}; text_paths(q->query.items[k].type.decl, "", &paths);
            for (int i = 0; i < paths.count; i++) if (heard_query_reads(g, q, k, paths.items[i].path))
                sb_printf(types, "// Capture rows%d binding%d %s\n", n, k, paths.items[i].path);
        }
    }
}

static void gen_heard_heap_decode(gen *g)
{
    if (!g->heard_heap) return;
    sb *o = &g->c;
    sb_put(o, "// Decode into a delivery-only heap; never resolve a journal offset in a live match.\n"
              "TIDE_HELPER bool tide_heard_heap_decode(tide_heard h, const uint8_t *data, uint8_t *write, uint64_t *budget)\n{\n"
              "    if (h.size < sizeof h) return false;\n"
              "    uint64_t fixed = 0;\n    switch (h.event) {\n");
    for (int i = 0; i < g->prog->events.count; i++) {
        const decl *e = g->prog->events.items[i];
        if (e->listeners.count && heard_event_heap(g, e))
            sb_printf(o, "    case %d: fixed = sizeof h + sizeof(tide_heard_%s); break;\n", e->index, type_cname(e));
    }
    sb_put(o, "    case TIDE_HEARD_ROWS: switch (h.handlers) {\n");
    for (int n = 0; n < g->heard_rows.count; n++) if (heard_query_heap(g, g->heard_rows.items[n]))
        sb_printf(o, "    case %d: if (h.same > (h.size - sizeof h) / sizeof(tide_heard_rows%d)) return false;\n"
                     "        fixed = sizeof h + h.same * sizeof(tide_heard_rows%d); break;\n", n, n, n);
    sb_put(o, "    default: return true;\n    } break;\n    default: return true;\n    }\n"
              "    if (fixed + 4u > h.size) return false;\n"
              "    uint32_t bytes; memcpy(&bytes, data + (size_t)fixed, 4u);\n"
              "    if (bytes > h.size - fixed - 4u || h.size - fixed - 4u - bytes > 7u) return false;\n"
              "    for (uint64_t i = fixed + 4u + bytes; i < h.size; i++) if (data[i]) return false;\n"
              "    tide_reader in = {data + (size_t)fixed + 4u, bytes, 0, false};\n"
              "    const uint64_t handlers = h.handlers; (void)handlers;\n    switch (h.event) {\n");
    for (int i = 0; i < g->prog->events.count; i++) {
        const decl *e = g->prog->events.items[i];
        if (!e->listeners.count || !heard_event_heap(g, e)) continue;
        sb_printf(o, "    case %d: {\n    tide_heard_%s v; memcpy(&v, data + sizeof h, sizeof v);\n", e->index, type_cname(e));
        gen_heard_capture(g, o, e, e, "v.event", NULL, 0, true);
        const type_kind kinds[] = {TY_COMPONENT, TY_SINGLETON};
        for (int n = 0; n < 2; n++) {
            const decl_list values = heard_data(e, kinds[n]);
            for (int k = 0; k < values.count; k++) {
                sb place = {0}; sb_printf(&place, "v.tide_%c_%s", n ? 's' : 'c', type_cname(values.items[k]));
                gen_heard_capture(g, o, e, values.items[k], place.data, NULL, 0, true);
            }
        }
        sb_put(o, "    if (write) memcpy(write + sizeof h, &v, sizeof v);\n    break;\n    }\n");
    }
    sb_put(o, "    case TIDE_HEARD_ROWS: switch (h.handlers) {\n");
    for (int n = 0; n < g->heard_rows.count; n++) {
        const param *q = g->heard_rows.items[n];
        if (!heard_query_heap(g, q)) continue;
        sb_printf(o, "    case %d: for (uint64_t i = 0; i < h.same; i++) {\n"
                     "    tide_heard_rows%d row; const size_t at = sizeof h + (size_t)i * sizeof row;\n"
                     "    memcpy(&row, data + at, sizeof row);\n", n, n);
        for (int k = 0; k < q->query.count; k++) {
            const param *p = &q->query.items[k];
            if (!param_binding(p)) continue;
            sb place = {0}; sb_printf(&place, "row.tide_b%d", k);
            gen_heard_capture(g, o, NULL, p->type.decl, place.data, q, k, true);
        }
        sb_put(o, "    if (write) memcpy(write + at, &row, sizeof row);\n    } break;\n");
    }
    sb_put(o, "    default: break;\n    } break;\n    default: break;\n    }\n"
              "    return !in.failed && in.at == in.size;\n}\n\n");
}
