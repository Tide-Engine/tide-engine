// What a game gives tide/embed.h (codegen_options.embed, tidec --embed):
// tide_game_embed (tide/embed_game.h), in the .c alone, so a game built for
// another program is the same game, by its hash, as one built otherwise.

// What another language reads a field as, where the layout says plain.
static int embed_field_kind(const type t)
{
    switch (t.kind) {
    case TY_STRING: return 1; // TIDE_EMBED_TEXT
    case TY_LIST: return 2;   // TIDE_EMBED_LIST
    case TY_MAP:
    case TY_SET: return 3;    // TIDE_EMBED_MAP
    case TY_GRID: return 4;   // TIDE_EMBED_GRID
    case TY_ENTITY:
    case TY_LOCAL_ENTITY: return 5; // TIDE_EMBED_ENTITY
    default: return 0;
    }
}

// A list's elements go in the packet when they hold nothing in a heap.
static bool embed_list_plain(const decl *list)
{
    return !type_has_text(list_elem(list));
}

// Turns `d`'s text and list fields in `v` (a pointer to its value, as C) into
// the packet's handles for them, and empties the heap values it can't carry.
static void gen_embed_fix(gen *g, const decl *d)
{
    sb *o = &g->c;
    path_list paths = {0};
    text_paths(d, "", &paths);
    if (!paths.count) return;
    const char *name = type_cname(d);
    sb_printf(o, "TIDE_HELPER void tide_embed_fix_%s(%s *v, tide_packet *out)\n{\n    (void)out;\n", name, name);
    for (int i = 0; i < paths.count; i++) {
        const heap_path *hp = &paths.items[i];
        if (hp->list) {
            if (embed_list_plain(hp->list)) {
                sb_printf(o, "    v->%s.at = tide_packet_list(out, v->%s, (uint32_t)sizeof(%s));\n", hp->path, hp->path,
                          elem_ctype(list_elem(hp->list)));
            } else {
                sb_printf(o, "    v->%s.at = 0; // Its elements hold text or lists: not in the packet\n", hp->path);
            }
        } else if (hp->map || hp->grid) {
            sb_printf(o, "    memset(&v->%s, 0, sizeof v->%s); // Not in the packet\n", hp->path, hp->path);
        } else {
            sb_printf(o, "    {\n        const tide_str s = tide_text_view(v->%s);\n"
                         "        v->%s.at = s.bytes > 0 ? tide_packet_text(out, s.ptr, (uint32_t)s.bytes) : 0u;\n    }\n",
                      hp->path, hp->path);
        }
    }
    sb_put(o, "}\n\n");
}

static bool embed_has_fix(const decl *d)
{
    path_list paths = {0};
    text_paths(d, "", &paths);
    return paths.count > 0;
}

// Whether any of the match's archetypes keeps component `c`.
static bool embed_component_kept(const gen *g, const int c)
{
    for (int a = 0; a < g->prog->archetypes.count; a++) {
        if (!arch_local(g, a) && arch_stores(g, a, c)) return true;
    }
    return false;
}

// For each of the match's rows that have component `c`: `body`, with `e` the
// entity and `i` the row of archetype `a`.
static void gen_embed_rows(gen *g, const int c, const char *body)
{
    sb *o = &g->c;
    for (int a = 0; a < g->prog->archetypes.count; a++) {
        if (arch_local(g, a) || !arch_stores(g, a, c)) continue;
        sb_printf(o, "        for (uint32_t i = 0; i < w->%s.count; i++) {\n", arch_name(g, a));
        if (comp_sparse(g, c)) sb_printf(o, "            if (!%s) continue;\n", presence_cell(g, "w", a, c, "i", false));
        sb_printf(o, "            const tide_entity e = *%s;\n            (void)e;\n", arch_cell(g, "w", a, 0, "tide_entity", "i", false));
        sb_printf(o, "#define TIDE_EMBED_ROW tide_row_%s(&w->%s, %s, %d, i)\n", type_cname(g->prog->components.items[c]),
                  arch_name(g, a), arch_columns(g, a), arch_column(g, a, c));
        sb_put(o, body);
        sb_put(o, "#undef TIDE_EMBED_ROW\n        }\n");
    }
}

static void gen_embed(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const layout_decls types = layout_types(prog);
    sb_put(o, "// For another program (tide/embed.h): the view packet, commands and scenes by name\n\n"
              "#include \"tide/embed_game.h\"\n\n");

    for (int i = 0; i < types.count; i++) {
        if (embed_has_fix(types.items[i])) gen_embed_fix(g, types.items[i]);
    }

    // The view packet: what views see this frame (tide/embed_game.h says its bytes)
    sb_put(o, "static void tide_embed_view(const void *world, const void *previous_world, float alpha, const void *local_state,\n"
              "                            uint32_t verified_tick, tide_packet *out)\n{\n"
              "    const tide_world *w = world;\n    const tide_world *previous = previous_world;\n"
              "    const tide_local *l = local_state;\n    (void)previous;\n    (void)l;\n"
              "    tide_packet_begin(out);\n");
    sb_printf(o, "    if (!w) {\n        tide_packet_end(out, 0x%016llXull, false, 0, verified_tick, 1.0f, 0);\n        return;\n    }\n",
              (unsigned long long)g->game_hash);
    sb_put(o, "    if (!(alpha >= 0.0f && alpha <= 1.0f)) alpha = 1.0f;\n");
    if (world_heap(prog, false) || world_heap(prog, true)) {
        sb_printf(o, "    tide_text_use(%s, %s);\n", world_heap(prog, false) ? "(tide_heap *)(uintptr_t)&w->heap" : "NULL",
                  world_heap(prog, true) ? "(tide_heap *)(uintptr_t)&l->heap" : "NULL");
    }
    sb_put(o, "    const uint32_t mark = tide_scratch_mark();\n"
              "    uint32_t *place = tide_packet_places(out, w->entities.next_unused);\n"
              "    (void)place;\n    uint32_t n = 0;\n");
    const int owner = prog->owner ? prog->owner->index : -1;
    if (owner >= 0) {
        sb_printf(o, "    const tide_player_id me = l->%s.%s;\n    (void)me;\n", type_cname(prog->session),
                  field_cname(&prog->session->fields.items[1]));
    }
    // Every entity of the match, with its owner's flags
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t i = 0; i < w->%s.count; i++) {\n", arch_name(g, a));
        sb_printf(o, "        const tide_entity e = *%s;\n        uint32_t flags = 0;\n", arch_cell(g, "w", a, 0, "tide_entity", "i", false));
        if (owner >= 0 && arch_stores(g, a, owner)) {
            if (comp_sparse(g, owner)) sb_printf(o, "        if (%s) {\n", presence_cell(g, "w", a, owner, "i", false));
            else sb_put(o, "        {\n");
            sb_printf(o, "            const %s o = tide_row_%s(&w->%s, %s, %d, i);\n", type_cname(prog->owner), type_cname(prog->owner),
                      arch_name(g, a), arch_columns(g, a), arch_column(g, a, owner));
            sb_printf(o, "            if (!tide_player_is_null(o.%s)) flags |= 2u;\n"
                         "            if (!tide_player_is_null(o.%s) && tide_player_equal(o.%s, me)) flags |= 1u;\n        }\n",
                      field_cname(&prog->owner->fields.items[0]), field_cname(&prog->owner->fields.items[0]),
                      field_cname(&prog->owner->fields.items[0]));
        }
        sb_put(o, "        const uint32_t row[5] = {e.index, e.generation, flags, TIDE_PACKET_NONE, TIDE_PACKET_NONE};\n"
                  "        memcpy(tide_packet_grow(out, TIDE_PACKET_ENTITY, false), row, sizeof row);\n"
                  "        if (e.index < w->entities.next_unused) place[e.index] = n;\n        n++;\n    }\n");
    }
    // Parents, now every entity has its place
    if (prog->hierarchy[0]) {
        sb_put(o, "    for (uint32_t k = 0; k < n; k++) {\n"
                  "        uint32_t row[5];\n        uint8_t *at = out->bytes + TIDE_PACKET_HEADER + (size_t)k * TIDE_PACKET_ENTITY;\n"
                  "        memcpy(row, at, sizeof row);\n"
                  "        const tide_entity parent = tide_parent_of(w, (tide_entity){row[0], row[1]});\n"
                  "        if (tide_entity_is_null(parent) || parent.index >= w->entities.next_unused) continue;\n"
                  "        row[4] = place[parent.index];\n        memcpy(at, row, sizeof row);\n    }\n");
    }
    // Each component's rows and values, blended as views see them; then its local fields
    for (int c = 0; c < prog->components.count; c++) {
        const decl *d = prog->components.items[c];
        if (!embed_component_kept(g, c)) continue;
        const char *name = type_cname(d);
        sb_printf(o, "    { // %s\n        uint32_t count = 0;\n", name);
        gen_embed_rows(g, c, "            count++;\n");
        sb_printf(o, "        uint32_t *rows;\n"
                     "        uint8_t *values = tide_packet_section(out, TIDE_PACKET_COMPONENT, %du, count, (uint32_t)sizeof(%s), &rows);\n"
                     "        uint32_t k = 0;\n", layout_index(&types, d), name);
        sb body = {0};
        sb_printf(&body, "            %s value = TIDE_EMBED_ROW;\n", name);
        if (!d->is_local && decl_blends(d)) {
            sb_printf(&body, "            %s before_value;\n"
                             "            const %s *before = previous ? tide_prev_%s(previous, w, e, &before_value) : NULL;\n",
                      name, name, name);
            if (blend_reads_heap(d)) sb_printf(&body, "            tide_blend_owned_%s(&value, before, alpha, previous, w);\n", name);
            else sb_printf(&body, "            tide_blend_%s(&value, before, alpha);\n", name);
        }
        if (embed_has_fix(d)) sb_printf(&body, "            tide_embed_fix_%s(&value, out);\n", name);
        sb_printf(&body, "            rows[k] = e.index < w->entities.next_unused ? place[e.index] : 0u;\n"
                         "            memcpy(values + (size_t)k * sizeof value, &value, sizeof value);\n            k++;\n");
        gen_embed_rows(g, c, body.data);
        sb_put(o, "    }\n");
        if (d->local_part) {
            const decl *part = d->local_part;
            const char *part_name = type_cname(part);
            sb_printf(o, "    { // %s's local fields\n        uint32_t count = 0;\n", name);
            gen_embed_rows(g, c, "            count++;\n");
            sb_printf(o, "        uint32_t *rows;\n"
                         "        uint8_t *values = tide_packet_section(out, TIDE_PACKET_COMPONENT_LOCAL, %du, count, (uint32_t)sizeof(%s), &rows);\n"
                         "        uint32_t k = 0;\n", layout_index(&types, part), part_name);
            sb local_body = {0};
            sb_printf(&local_body, "            %s value = tide_get_%s_local(l, e);\n", part_name, name);
            if (embed_has_fix(part)) sb_printf(&local_body, "            tide_embed_fix_%s(&value, out);\n", part_name);
            sb_put(&local_body, "            rows[k] = e.index < w->entities.next_unused ? place[e.index] : 0u;\n"
                                "            memcpy(values + (size_t)k * sizeof value, &value, sizeof value);\n            k++;\n");
            gen_embed_rows(g, c, local_body.data);
            sb_put(o, "    }\n");
        }
    }
    // The match's singletons, blended, and their local fields
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (d->is_local || layout_index(&types, d) < 0) continue;
        const char *name = type_cname(d);
        sb_printf(o, "    { // %s\n        %s value = w->%s;\n", name, name, name);
        if (decl_blends(d)) {
            if (blend_reads_heap(d)) sb_printf(o, "        tide_blend_owned_%s(&value, previous ? &previous->%s : NULL, alpha, previous, w);\n", name, name);
            else sb_printf(o, "        tide_blend_%s(&value, previous ? &previous->%s : NULL, alpha);\n", name, name);
        }
        if (embed_has_fix(d)) sb_printf(o, "        tide_embed_fix_%s(&value, out);\n", name);
        sb_printf(o, "        memcpy(tide_packet_section(out, TIDE_PACKET_SINGLETON, %du, 1, (uint32_t)sizeof value, NULL), &value, sizeof value);\n    }\n",
                  layout_index(&types, d));
        if (d->local_part && layout_index(&types, d->local_part) >= 0) {
            const char *part_name = type_cname(d->local_part);
            sb_printf(o, "    { // %s's local fields\n        %s value = l->%s_local;\n", name, part_name, name);
            if (embed_has_fix(d->local_part)) sb_printf(o, "        tide_embed_fix_%s(&value, out);\n", part_name);
            sb_printf(o, "        memcpy(tide_packet_section(out, TIDE_PACKET_SINGLETON_LOCAL, %du, 1, (uint32_t)sizeof value, NULL), &value, sizeof value);\n    }\n",
                      layout_index(&types, d->local_part));
        }
    }
    sb_printf(o, "    tide_scratch_reset(mark);\n"
                 "    tide_packet_end(out, 0x%016llXull, true, (uint32_t)w->Time.tick, verified_tick, alpha, n);\n}\n\n",
              (unsigned long long)g->game_hash);

    // Commands: their values' layout, after the layout's types, and sending one as local code does
    for (int k = 0; k < prog->command_types.count; k++) {
        const decl *d = prog->command_types.items[k];
        const char *name = type_cname(d);
        if (!d->fields.count) continue;
        sb_printf(o, "static const tide_layout_field tide_embed_command_fields%d[] = {\n", k);
        for (int i = 0; i < d->fields.count; i++) {
            const field *f = &d->fields.items[i];
            const type t = f->type;
            const int dim = type_dim(t) > 0 ? type_dim(t) : t.kind == TY_LONG ? 1 : 0;
            sb_printf(o, "    {\"" STR_FMT "\", \"%s\", %s, %d, %d, offsetof(%s, %s), sizeof(((%s *)0)->%s)},\n", STR_ARG(f->name),
                      layout_field_name(t), layout_kind(t), dim, t.kind == TY_STRUCT ? layout_index(&types, t.decl) : -1, name,
                      field_cname(f), name, field_cname(f));
        }
        sb_put(o, "};\n\n");
    }
    if (prog->command_types.count) {
        sb_put(o, "static const tide_layout_type tide_embed_extra_types[] = {\n");
        for (int k = 0; k < prog->command_types.count; k++) {
            const decl *d = prog->command_types.items[k];
            const type t = {TY_STRUCT, (decl *)d};
            if (d->fields.count) {
                sb_printf(o, "    {\"%s\", sizeof(%s), false, %d, tide_embed_command_fields%d, NULL},\n", layout_name(t), type_cname(d),
                          d->fields.count, k);
            } else {
                sb_printf(o, "    {\"%s\", sizeof(%s), false, 0, NULL, NULL},\n", layout_name(t), type_cname(d));
            }
        }
        sb_put(o, "};\n\n");
    }
    sb_put(o, "static bool tide_embed_command(void *local_state, int32_t type, uint32_t target_index, uint32_t target_generation,\n"
              "                               const void *value, uint32_t size)\n{\n"
              "    tide_local *l = local_state;\n    const tide_entity target = {target_index, target_generation};\n"
              "    (void)l;\n    (void)target;\n    (void)value;\n    (void)size;\n    switch (type) {\n");
    for (int k = 0; k < prog->command_types.count; k++) {
        const decl *d = prog->command_types.items[k];
        const char *name = type_cname(d);
        sb_printf(o, "    case %d: { // " STR_FMT "\n", types.count + k, STR_ARG(d->qualified));
        if (decl_has_text(d)) {
            sb_put(o, "        return false; // It holds text or lists, which another program's bytes can't\n    }\n");
            continue;
        }
        sb_printf(o, "        if (size != sizeof(%s)) return false;\n        %s v;\n        memcpy(&v, value, sizeof v);\n"
                     "        tide_local_send_%s(l, target, v);\n        return true;\n    }\n",
                  name, name, name);
    }
    sb_put(o, "    default:\n        return false;\n    }\n}\n\n");

    // Scenes a match can start in, by name
    sb_put(o, "static bool tide_embed_start(void *start, const char *scene)\n{\n"
              "    tide_start *s = start;\n    memset(s, 0, sizeof *s);\n    s->scene = -1;\n    if (!scene) return true;\n");
    for (int i = 0; i < prog->start_scenes.count; i++) {
        const decl *d = prog->start_scenes.items[i];
        const type t = {TY_COMPONENT, (decl *)d};
        sb_printf(o, "    if (strcmp(scene, \"%s\") == 0) {\n        s->scene = %d;\n", layout_name(t), i);
        path_list paths = {0};
        text_paths(d, "", &paths);
        if (paths.count) sb_put(o, "        const uint32_t mark = tide_scratch_mark();\n");
        sb_printf(o, "        s->value.%s = ", type_cname(d));
        gen_value(g, o, d, NULL, 0);
        sb_put(o, ";\n");
        for (int k = 0; k < paths.count; k++) {
            sb_printf(o, "        memset(&s->value.%s.%s, 0, sizeof s->value.%s.%s);\n", type_cname(d), paths.items[k].path,
                      type_cname(d), paths.items[k].path);
        }
        if (paths.count) sb_put(o, "        tide_scratch_reset(mark);\n");
        sb_put(o, "        return true;\n    }\n");
    }
    sb_put(o, "    return false;\n}\n\n");

    // Each match component's declared defaults, which an editor shows for a field nothing sets
    sb_put(o, "static bool tide_embed_defaults(int32_t type, void *out, uint32_t size)\n{\n"
              "    (void)out;\n    (void)size;\n    switch (type) {\n");
    for (int c = 0; c < prog->components.count; c++) {
        const decl *d = prog->components.items[c];
        if (!embed_component_kept(g, c)) continue;
        const char *name = type_cname(d);
        sb_printf(o, "    case %d: { // " STR_FMT "\n        if (size != sizeof(%s)) return false;\n", layout_index(&types, d),
                  STR_ARG(d->qualified), name);
        path_list paths = {0};
        text_paths(d, "", &paths);
        if (paths.count) sb_put(o, "        const uint32_t mark = tide_scratch_mark();\n");
        sb_printf(o, "        %s v = ", name);
        gen_value(g, o, d, NULL, 0);
        sb_put(o, ";\n");
        for (int k = 0; k < paths.count; k++) sb_printf(o, "        memset(&v.%s, 0, sizeof v.%s);\n", paths.items[k].path, paths.items[k].path);
        if (paths.count) sb_put(o, "        tide_scratch_reset(mark);\n");
        sb_put(o, "        memcpy(out, &v, sizeof v);\n        return true;\n    }\n");
    }
    sb_put(o, "    default:\n        return false;\n    }\n}\n\n");

    // The tables
    int components = 0;
    for (int c = 0; c < prog->components.count; c++) components += embed_component_kept(g, c);
    if (components) {
        sb_put(o, "static const int32_t tide_embed_components[] = {");
        for (int c = 0; c < prog->components.count; c++) {
            if (embed_component_kept(g, c)) sb_printf(o, "%d, ", layout_index(&types, prog->components.items[c]));
        }
        sb_put(o, "};\nstatic const int32_t tide_embed_component_locals[] = {");
        for (int c = 0; c < prog->components.count; c++) {
            const decl *d = prog->components.items[c];
            if (embed_component_kept(g, c)) sb_printf(o, "%d, ", d->local_part ? layout_index(&types, d->local_part) : -1);
        }
        sb_put(o, "};\n");
    }
    int singletons = 0;
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        singletons += !d->is_local && layout_index(&types, d) >= 0;
    }
    if (singletons) {
        sb_put(o, "static const int32_t tide_embed_singletons[] = {");
        for (int i = 0; i < prog->singletons.count; i++) {
            const decl *d = prog->singletons.items[i];
            if (!d->is_local && layout_index(&types, d) >= 0) sb_printf(o, "%d, ", layout_index(&types, d));
        }
        sb_put(o, "};\nstatic const int32_t tide_embed_singleton_locals[] = {");
        for (int i = 0; i < prog->singletons.count; i++) {
            const decl *d = prog->singletons.items[i];
            if (!d->is_local && layout_index(&types, d) >= 0) {
                sb_printf(o, "%d, ", d->local_part ? layout_index(&types, d->local_part) : -1);
            }
        }
        sb_put(o, "};\n");
    }
    if (prog->command_types.count) {
        sb_put(o, "static const tide_embed_named tide_embed_commands[] = {\n");
        for (int k = 0; k < prog->command_types.count; k++) {
            const type t = {TY_STRUCT, (decl *)prog->command_types.items[k]};
            sb_printf(o, "    {\"%s\", %d},\n", layout_name(t), types.count + k);
        }
        sb_put(o, "};\n");
    }
    if (prog->start_scenes.count) {
        sb_put(o, "static const char *const tide_embed_scenes[] = {");
        for (int i = 0; i < prog->start_scenes.count; i++) {
            const type t = {TY_COMPONENT, (decl *)prog->start_scenes.items[i]};
            sb_printf(o, "\"%s\", ", layout_name(t));
        }
        sb_put(o, "};\n");
    }
    int prefabs = 0;
    for (int i = 0; i < prog->decls.count; i++) prefabs += prog->decls.items[i]->kind == DECL_PREFAB;
    if (prefabs) {
        sb_put(o, "static const char *const tide_embed_prefabs[] = {");
        for (int i = 0; i < prog->decls.count; i++) {
            const decl *d = prog->decls.items[i];
            if (d->kind == DECL_PREFAB) sb_printf(o, "\"" STR_FMT "\", ", STR_ARG(d->qualified));
        }
        sb_put(o, "};\n");
    }
    int fields = 0;
    for (int i = 0; i < types.count; i++) {
        for (int k = 0; k < types.items[i]->fields.count; k++) fields += embed_field_kind(types.items[i]->fields.items[k].type) != 0;
    }
    if (fields) {
        sb_put(o, "static const tide_embed_field tide_embed_fields[] = {\n");
        for (int i = 0; i < types.count; i++) {
            const decl *d = types.items[i];
            for (int k = 0; k < d->fields.count; k++) {
                const field *f = &d->fields.items[k];
                const int kind = embed_field_kind(f->type);
                if (!kind) continue;
                if (kind == 2) {
                    // The element's size where the packet has the elements, 0 where it hasn't
                    const type element = list_elem(f->type.decl);
                    sb_printf(o, "    {%d, \"" STR_FMT "\", 2, ", i, STR_ARG(f->name));
                    if (embed_list_plain(f->type.decl)) sb_printf(o, "(uint32_t)sizeof(%s), ", elem_ctype(element));
                    else sb_put(o, "0u, ");
                    sb_printf(o, "%d},\n", element.kind == TY_STRUCT ? layout_index(&types, element.decl) : -1);
                } else {
                    sb_printf(o, "    {%d, \"" STR_FMT "\", %d, 0u, -1},\n", i, STR_ARG(f->name), kind);
                }
            }
        }
        sb_put(o, "};\n");
    }
    sb_put(o, "\nextern const tide_embed_game tide_game_embed;\n\n"
              "static uint32_t tide_embed_schema(char *out, uint32_t capacity)\n{\n"
              "    return tide_schema_json(&tide_game_layout, &tide_game_embed, tide_game_api.hash,\n"
              "                            tide_game_api.title ? tide_game_api.title : \"\", tide_game_api.tick_rate, out, capacity);\n}\n");
    sb_printf(o, "\nconst tide_embed_game tide_game_embed = {\n"
                 "    %d, %s, %s,\n    %d, %s, %s,\n    %d, %s,\n    %d, %s,\n    %d, %s,\n    %d, %s,\n    %d, %s,\n"
                 "    tide_embed_view, tide_embed_command, tide_embed_start, tide_embed_schema, tide_embed_defaults,\n};\n",
              components, components ? "tide_embed_components" : "NULL", components ? "tide_embed_component_locals" : "NULL",
              singletons, singletons ? "tide_embed_singletons" : "NULL", singletons ? "tide_embed_singleton_locals" : "NULL",
              prog->command_types.count, prog->command_types.count ? "tide_embed_commands" : "NULL",
              prog->start_scenes.count, prog->start_scenes.count ? "tide_embed_scenes" : "NULL",
              prefabs, prefabs ? "tide_embed_prefabs" : "NULL",
              fields, fields ? "tide_embed_fields" : "NULL",
              prog->command_types.count, prog->command_types.count ? "tide_embed_extra_types" : "NULL");
}
