// A dev build's inspector (tide/inspect.h): the match's and the local world's state as view packets, and changes to
// a field of either. A change of the match's is a command (TideSetField, inspect_prelude), which the server applies
// with tide_inspect_apply in its handler's place.

// The prelude's TideSetField, or NULL in a build without dev
static const decl *inspect_set_command(const program *prog)
{
    for (int i = 0; i < prog->events.count; i++) {
        if (prog->events.items[i]->inspect_set) return prog->events.items[i];
    }
    return NULL;
}

// TideSetField's field `name`, as C names it
static const char *inspect_field(const decl *set, const char *name)
{
    for (int i = 0; i < set->fields.count; i++) {
        if (str_eq_c(set->fields.items[i].name, name)) return field_cname(&set->fields.items[i]);
    }
    return name;
}

// What TideSetField's dispatcher calls in its handler's place: the change written, if it's one tide_inspect_writable
// allows, into the component of the target, if it still has it, or into a singleton of the match (no target).
// Before the dispatchers, which call it.
static void gen_inspect_apply(gen *g)
{
    const program *prog = g->prog;
    const decl *set = inspect_set_command(prog);
    if (!set) return;
    sb *o = &g->c;
    const layout_decls types = layout_types(prog);
    const char *name = type_cname(set);
    sb_printf(o, "// A dev build's inspector's change of a field of the match (tide/inspect.h), which TideSetField takes to the server\n"
                 "#include \"tide/inspect.h\"\n\nextern const tide_layout tide_game_layout;\n\n"
                 "TIDE_HELPER void tide_inspect_apply(tide_world *w, const tide_entity target, const %s *f)\n{\n"
                 "    const int64_t words[4] = {f->%s, f->%s, f->%s, f->%s};\n"
                 "    uint8_t bytes[sizeof words];\n    memcpy(bytes, words, sizeof bytes);\n"
                 "    if (f->%s <= 0 || f->%s > (int32_t)sizeof bytes || f->%s < 0) return;\n"
                 "    const uint32_t offset = (uint32_t)f->%s, size = (uint32_t)f->%s;\n"
                 "    if (!tide_inspect_writable(&tide_game_layout, f->%s, offset, bytes, size)) return;\n"
                 "    switch (f->%s) {\n",
              name, inspect_field(set, "a"), inspect_field(set, "b"), inspect_field(set, "c"), inspect_field(set, "d"),
              inspect_field(set, "size"), inspect_field(set, "size"), inspect_field(set, "offset"),
              inspect_field(set, "offset"), inspect_field(set, "size"), inspect_field(set, "type"),
              inspect_field(set, "type"));
    for (int c = 0; c < prog->components.count; c++) {
        const decl *d = prog->components.items[c];
        if (d->is_local || !embed_component_kept(g, c)) continue;
        const char *cname = type_cname(d);
        sb_printf(o, "    case %d: { // " STR_FMT "\n        %s v;\n        if (!tide_load_%s(w, target, &v)) return;\n"
                     "        memcpy((uint8_t *)&v + offset, bytes, size);\n        tide_store_%s(w, target, &v);\n        return;\n    }\n",
                  layout_index(&types, d), STR_ARG(d->qualified), cname, cname, cname);
    }
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (d->is_local || layout_index(&types, d) < 0) continue;
        sb_printf(o, "    case %d: // " STR_FMT "\n        if (tide_entity_is_null(target)) memcpy((uint8_t *)&w->%s + offset, bytes, size);\n"
                     "        return;\n",
                  layout_index(&types, d), STR_ARG(d->qualified), type_cname(d));
    }
    sb_put(o, "    default:\n        return;\n    }\n}\n\n");
}

// For each row of the local world's archetype `a`: `body`, with `e` the entity and `i` its row
static void gen_inspect_local_rows(gen *g, const int a, const int c, const char *body)
{
    sb *o = &g->c;
    sb_printf(o, "        for (uint32_t i = 0; i < l->%s.count; i++) {\n", arch_name(g, a));
    sb_printf(o, "            if (tide_row_gone(%s)) continue;\n", arch_cell(g, "l", a, 0, "tide_entity", "i", false));
    if (c >= 0 && comp_sparse(g, c)) sb_printf(o, "            if (!%s) continue;\n", presence_cell(g, "l", a, c, "i", false));
    sb_printf(o, "            const tide_entity e = *%s;\n            (void)e;\n", arch_cell(g, "l", a, 0, "tide_entity", "i", false));
    if (c >= 0) {
        sb_printf(o, "#define TIDE_INSPECT_ROW tide_row_%s(&l->%s, %s, %d, i)\n", type_cname(g->prog->components.items[c]),
                  arch_name(g, a), arch_columns(g, a), arch_column(g, a, c));
    }
    sb_put(o, body);
    if (c >= 0) sb_put(o, "#undef TIDE_INSPECT_ROW\n");
    sb_put(o, "        }\n");
}

// The inspector's table (tide_game_inspect): the packets, and changes sent to the match or written here
static void gen_inspect(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const decl *set = inspect_set_command(prog);
    if (!set) {
        sb_put(o, "const struct tide_inspect_api *tide_game_inspect(void)\n{\n    return NULL;\n}\n\n");
        return;
    }
    const layout_decls types = layout_types(prog);
    sb_put(o, "// A dev build's inspector (tide/inspect.h)\n\n");
    if (!g->opts->embed) gen_embed_packers(g); // What the packets' text and containers are written with
    gen_packet_view(g, "tide_inspect_match", false);

    // The local world's packet: its entities, their components, and its singletons (the match's singletons' local
    // fields among them)
    sb_put(o, "static void tide_inspect_local(const tide_local *l, tide_packet *out)\n{\n    tide_packet_begin(out);\n");
    if (world_heap(prog, true)) sb_put(o, "    tide_text_use(NULL, (tide_heaps *)(uintptr_t)&l->heap);\n");
    sb_put(o, "    const uint64_t mark = tide_scratch_mark();\n    uint32_t n = 0;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (!arch_local(g, a)) continue;
        sb_put(o, "    {\n");
        gen_inspect_local_rows(g, a, -1, "            const uint32_t row[8] = {(uint32_t)e.birth, (uint32_t)(e.birth >> 32), (uint32_t)e.lineage,\n"
                                         "                                     (uint32_t)(e.lineage >> 32), 0u, TIDE_PACKET_NONE, TIDE_PACKET_NONE, 0u};\n"
                                         "            memcpy(tide_packet_grow(out, TIDE_PACKET_ENTITY, false), row, sizeof row);\n"
                                         "            tide_packet_entity_place(out, e, n);\n            n++;\n");
        sb_put(o, "    }\n");
    }
    for (int c = 0; c < prog->components.count; c++) {
        const decl *d = prog->components.items[c];
        bool kept = false;
        for (int a = 0; a < prog->archetypes.count; a++) kept |= arch_local(g, a) && arch_stores(g, a, c);
        if (!d->is_local || !kept || layout_index(&types, d) < 0) continue;
        const char *name = type_cname(d);
        sb_printf(o, "    { // %s\n        uint32_t count = 0;\n", name);
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a) && arch_stores(g, a, c)) gen_inspect_local_rows(g, a, c, "            count++;\n");
        }
        sb_printf(o, "        uint32_t *rows;\n"
                     "        uint8_t *values = tide_packet_section(out, TIDE_PACKET_COMPONENT, %du, count, (uint32_t)sizeof(%s), &rows);\n"
                     "        uint32_t k = 0;\n", layout_index(&types, d), name);
        sb body = {0};
        sb_printf(&body, "            %s value = TIDE_INSPECT_ROW;\n", name);
        if (embed_has_fix(d)) sb_printf(&body, "            tide_embed_fix_%s(&value, out);\n", name);
        sb_put(&body, "            rows[k] = tide_packet_find_entity(out, e);\n"
                      "            memcpy(values + (size_t)k * sizeof value, &value, sizeof value);\n            k++;\n");
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a) && arch_stores(g, a, c)) gen_inspect_local_rows(g, a, c, body.data);
        }
        sb_put(o, "    }\n");
    }
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!d->is_local || layout_index(&types, d) < 0) continue;
        const char *name = type_cname(d);
        sb_printf(o, "    { // %s\n        %s value = l->%s;\n", name, name, name);
        if (embed_has_fix(d)) sb_printf(o, "        tide_embed_fix_%s(&value, out);\n", name);
        sb_printf(o, "        memcpy(tide_packet_section(out, TIDE_PACKET_SINGLETON, %du, 1, (uint32_t)sizeof value, NULL), &value, sizeof value);\n    }\n",
                  layout_index(&types, d));
    }
    sb_printf(o, "    tide_scratch_reset(mark);\n    tide_packet_end(out, 0x%016llXull, false, 0, 0, 1.0f, n);\n}\n\n",
              (unsigned long long)g->game_hash);

    sb_put(o, "static void tide_inspect_view(const void *world, const void *local_state, tide_packet *match, tide_packet *local)\n{\n"
              "    tide_inspect_match(world, NULL, 1.0f, local_state, 0u, match);\n"
              "    tide_inspect_local(local_state, local);\n}\n\n");

    // A change of the match's, to the server
    const char *name = type_cname(set);
    sb_printf(o, "static void tide_inspect_set(void *local_state, const tide_entity target, const int32_t type, const uint32_t offset,\n"
                 "                             const void *bytes, const uint32_t size)\n{\n"
                 "    if (!bytes || size == 0 || size > TIDE_INSPECT_BYTES || offset > INT32_MAX) return;\n"
                 "    int64_t words[4] = {0, 0, 0, 0};\n    memcpy(words, bytes, size);\n"
                 "    %s v;\n    memset(&v, 0, sizeof v);\n"
                 "    v.%s = type;\n    v.%s = (int32_t)offset;\n    v.%s = (int32_t)size;\n"
                 "    v.%s = words[0];\n    v.%s = words[1];\n    v.%s = words[2];\n    v.%s = words[3];\n"
                 "    tide_local_send_%s((tide_local *)local_state, target, v);\n}\n\n",
              name, inspect_field(set, "type"), inspect_field(set, "offset"), inspect_field(set, "size"),
              inspect_field(set, "a"), inspect_field(set, "b"), inspect_field(set, "c"), inspect_field(set, "d"), name);

    // ...and one of the local world's, here
    sb_put(o, "static bool tide_inspect_set_local(void *local_state, const tide_entity target, const int32_t type, const uint32_t offset,\n"
              "                                   const void *bytes, const uint32_t size)\n{\n"
              "    tide_local *l = local_state;\n    (void)l;\n    (void)target;\n"
              "    if (!tide_inspect_writable(&tide_game_layout, type, offset, bytes, size)) return false;\n"
              "    switch (type) {\n");
    for (int c = 0; c < prog->components.count; c++) {
        const decl *d = prog->components.items[c];
        if (!d->is_local || layout_index(&types, d) < 0) continue;
        const char *cname = type_cname(d);
        sb_printf(o, "    case %d: { // " STR_FMT "\n        %s v;\n        if (!tide_load_%s(l, target, &v)) return false;\n"
                     "        memcpy((uint8_t *)&v + offset, bytes, size);\n        return tide_store_%s(l, target, &v);\n    }\n",
                  layout_index(&types, d), STR_ARG(d->qualified), cname, cname, cname);
    }
    for (int i = 0; i < prog->local_parts.count; i++) { // The local fields of the match's entities
        const decl *part = prog->local_parts.items[i];
        if (part->kind != DECL_COMPONENT || !part->part_of || layout_index(&types, part) < 0) continue;
        const char *owner = type_cname(part->part_of), *pname = type_cname(part);
        sb_printf(o, "    case %d: { // " STR_FMT "'s local fields\n        if (tide_entity_is_null(target)) return false;\n"
                     "        %s v = tide_get_%s_local(l, target);\n        memcpy((uint8_t *)&v + offset, bytes, size);\n"
                     "        tide_set_%s_local(l, target, v);\n        return true;\n    }\n",
                  layout_index(&types, part), STR_ARG(part->part_of->qualified), pname, owner, owner);
    }
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!d->is_local || layout_index(&types, d) < 0) continue;
        sb_printf(o, "    case %d: // " STR_FMT "\n        if (!tide_entity_is_null(target)) return false;\n"
                     "        memcpy((uint8_t *)&l->%s + offset, bytes, size);\n        return true;\n",
                  layout_index(&types, d), STR_ARG(d->qualified), type_cname(d));
    }
    sb_put(o, "    default:\n        return false;\n    }\n}\n\n"
              "static const tide_inspect_api tide_inspect = {&tide_game_layout, tide_inspect_view, tide_inspect_set, tide_inspect_set_local};\n"
              "const struct tide_inspect_api *tide_game_inspect(void)\n{\n    return &tide_inspect;\n}\n\n");
}
