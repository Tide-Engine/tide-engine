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

// A list whose elements hold nothing in a heap: bytes another program's command can give as they are.
static bool embed_list_plain(const decl *list)
{
    return !type_has_text(list_elem(list));
}

// A value of `t` at `lvalue` (C) made the packet's: its text, lists, maps, sets and grids, wherever they are in it,
// the packet's handles of them (each list, map, set and grid packed by the packers below, which do the same for what
// they hold). `out` is the packet being written.
static void embed_fix_value(sb *o, const type t, const char *lvalue, const char *indent)
{
    if (!type_has_text(t)) return;
    switch (t.kind) {
    case TY_STRING:
        sb_printf(o, "%s{\n%s    const tide_str s = tide_text_view(%s);\n%s    %s.at = s.bytes > 0 ? tide_packet_text(out, s.ptr, (uint32_t)s.bytes) : 0u;\n%s}\n",
                  indent, indent, lvalue, indent, lvalue, indent);
        break;
    case TY_LIST:
        sb_printf(o, "%s%s.at = tide_embed_list%d(%s, out);\n", indent, lvalue, t.decl->index, lvalue);
        break;
    case TY_MAP:
    case TY_SET:
        // A map's value keeps lists and counts: the packet's handle is its first word, the rest zero
        sb_printf(o, "%s{\n%s    const uint32_t h = tide_embed_map%d(%s, out);\n%s    memset(&%s, 0, sizeof %s);\n"
                     "%s    memcpy(&%s, &h, sizeof h);\n%s}\n",
                  indent, indent, t.decl->index, lvalue, indent, lvalue, lvalue, indent, lvalue, indent);
        break;
    case TY_GRID:
        sb_printf(o, "%s%s.at = tide_embed_%s(%s, out);\n", indent, lvalue, grid_shape_name(t.decl), lvalue);
        break;
    case TY_STRUCT:
        sb_printf(o, "%stide_embed_fix_%s(&%s, out);\n", indent, type_cname(t.decl), lvalue);
        break;
    default:
        // An array or what else keeps text: zeroed, as the packet can't say it
        sb_printf(o, "%smemset(&%s, 0, sizeof %s);\n", indent, lvalue, lvalue);
        break;
    }
}

// Turns `d`'s heap fields in `v` (a pointer to its value, as C) into the packet's handles for them.
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
        const type t = hp->list ? (type){TY_LIST, (decl *)hp->list}
                     : hp->map ? (type){hp->map->is_set ? TY_SET : TY_MAP, (decl *)hp->map}
                     : hp->grid ? (type){TY_GRID, (decl *)hp->grid}
                     : (type){TY_STRING, NULL};
        sb lvalue = {0};
        sb_printf(&lvalue, "v->%s", hp->path);
        embed_fix_value(o, t, lvalue.data, "    ");
    }
    sb_put(o, "}\n\n");
}

static bool embed_has_fix(const decl *d)
{
    path_list paths = {0};
    text_paths(d, "", &paths);
    return paths.count > 0;
}

// The packers: each list, map, set and grid type's value into the packet's STRINGS, as tide/embed_game.h lays it out,
// what it holds made the packet's too (embed_fix_value): what a field of it holds there, 0 for empty. Their memory is
// the scratch area's, under the caller's mark.
static void gen_embed_packers(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const layout_decls types = layout_types(prog);
    sb_put(o, "// Lists, maps, sets and grids into a view packet's STRINGS (tide/embed_game.h)\n");
    for (int i = 0; i < prog->lists.count; i++) {
        sb_printf(o, "TIDE_HELPER uint32_t tide_embed_list%d(tide_list l, tide_packet *out);\n", prog->lists.items[i]->index);
    }
    for (int i = 0; i < prog->maps.count; i++) {
        sb_printf(o, "TIDE_HELPER uint32_t tide_embed_map%d(tide_map m, tide_packet *out);\n", prog->maps.items[i]->index);
    }
    for (int i = 0; i < prog->grids.count; i++) {
        sb_printf(o, "TIDE_HELPER uint32_t tide_embed_%s(tide_grid g, tide_packet *out);\n", grid_shape_name(prog->grids.items[i]));
    }
    // ...and the fixes of the types they hold, the layout's, the events' and the commands'
    layout_decls fixed = {0};
    for (int i = 0; i < types.count; i++) vec_push(fixed, types.items[i]);
    for (int i = 0; i < prog->events.count; i++) vec_push(fixed, prog->events.items[i]);
    for (int i = 0; i < fixed.count; i++) {
        if (!embed_has_fix(fixed.items[i])) continue;
        bool seen = false;
        for (int k = 0; k < i; k++) seen |= fixed.items[k] == fixed.items[i];
        if (seen) continue;
        sb_printf(o, "TIDE_HELPER void tide_embed_fix_%s(%s *v, tide_packet *out);\n", type_cname(fixed.items[i]),
                  type_cname(fixed.items[i]));
    }
    sb_put(o, "\n");
    for (int i = 0; i < fixed.count; i++) {
        bool seen = false;
        for (int k = 0; k < i; k++) seen |= fixed.items[k] == fixed.items[i];
        if (!seen) gen_embed_fix(g, fixed.items[i]);
    }
    // A list: its elements side by side
    for (int i = 0; i < prog->lists.count; i++) {
        const decl *list = prog->lists.items[i];
        const type element = list_elem(list);
        const char *c = elem_ctype(element);
        sb_printf(o, "TIDE_HELPER uint32_t tide_embed_list%d(tide_list l, tide_packet *out)\n{\n", list->index);
        if (!type_has_text(element)) {
            sb_printf(o, "    return tide_packet_list(out, l, (uint32_t)sizeof(%s));\n}\n\n", c);
            continue;
        }
        sb_printf(o, "    const int32_t count = tide_list_count(l);\n"
                     "    if (count <= 0 || (uint64_t)count * sizeof(%s) > UINT32_MAX / 4u) return 0u;\n"
                     "    uint8_t *items = tide_scratch_memory((size_t)count * sizeof(%s));\n"
                     "    for (int32_t i = 0; i < count; i++) {\n"
                     "        %s e;\n        const void *at = tide_list_at(l, i, (uint32_t)sizeof e);\n"
                     "        if (at) memcpy(&e, at, sizeof e);\n        else memset(&e, 0, sizeof e);\n",
                  c, c, c);
        embed_fix_value(o, element, "e", "        ");
        sb_printf(o, "        memcpy(items + (size_t)i * sizeof e, &e, sizeof e);\n    }\n"
                     "    return tide_packet_text(out, items, (uint32_t)((size_t)count * sizeof(%s)));\n}\n\n", c);
    }
    // A map: its count, 0, its keys side by side (to 8 bytes), then its values side by side, in insertion order; a set
    // its keys alone
    for (int i = 0; i < prog->maps.count; i++) {
        const decl *m = prog->maps.items[i];
        const bool set = m->is_set;
        const type key = m->fields.items[0].type;
        const type value = set ? (type){TY_VOID, NULL} : m->fields.items[1].type;
        const char *kc = elem_ctype(key), *vc = set ? "uint8_t" : elem_ctype(value);
        sb values = {0}, kept = {0};
        if (set) {
            sb_put(&values, "0u");
            sb_put(&kept, "0u");
        } else {
            sb_printf(&values, "(uint64_t)count * sizeof(%s)", vc);
            sb_printf(&kept, "(uint64_t)n * sizeof(%s)", vc);
        }
        sb_printf(o, "TIDE_HELPER uint32_t tide_embed_map%d(tide_map m, tide_packet *out)\n{\n"
                     "    const int32_t count = tide_map_count(m);\n    if (count <= 0) return 0u;\n"
                     "    const uint64_t keys = ((uint64_t)count * sizeof(%s) + 7u) & ~7ull;\n"
                     "    const uint64_t values = %s;\n"
                     "    if (8u + keys + values > UINT32_MAX / 4u) return 0u;\n"
                     "    uint8_t *blob = tide_scratch_memory((size_t)(8u + keys + values));\n"
                     "    memset(blob, 0, (size_t)(8u + keys + values));\n"
                     "    int64_t order = 0;\n    int32_t next = 0, n = 0;\n"
                     "    for (int32_t i; n < count && (i = tide_map_next(m, &tide_map%d_kind, &order, &next)) >= 0; n++) {\n"
                     "        const tide_map%d_entry *entry = tide_map_entry(m, i, &tide_map%d_kind);\n"
                     "        %s k = entry->%s;\n",
                  m->index, kc, values.data, m->index, m->index, m->index, kc, field_cname(&m->fields.items[0]));
        embed_fix_value(o, key, "k", "        ");
        sb_put(o, "        memcpy(blob + 8u + (size_t)n * sizeof k, &k, sizeof k);\n");
        if (!set) {
            sb_printf(o, "        %s v = entry->%s;\n", vc, field_cname(&m->fields.items[1]));
            embed_fix_value(o, value, "v", "        ");
            sb_put(o, "        memcpy(blob + 8u + keys + (size_t)n * sizeof v, &v, sizeof v);\n");
        }
        sb_printf(o, "    }\n    const uint32_t head[2] = {(uint32_t)n, 0u};\n    memcpy(blob, head, sizeof head);\n"
                     "    return tide_packet_text(out, blob, (uint32_t)(8u + keys + %s));\n}\n\n",
                  kept.data);
    }
    // A grid: its cells' size, dimensions, chunk shifts and size (0 where open), and each chunk it has: its position,
    // then its cells (to 8 bytes)
    for (int i = 0; i < prog->grids.count; i++) {
        const decl *grid = prog->grids.items[i];
        const char *shape = grid_shape_name(grid);
        sb_printf(o, "TIDE_HELPER uint32_t tide_embed_%s(tide_grid g, tide_packet *out)\n{\n", shape);
        if (type_has_text(grid->fields.items[0].type)) {
            sb_put(o, "    (void)g;\n    (void)out;\n    return 0u; // Cells that hold text: not in the packet\n}\n\n");
            continue;
        }
        sb_printf(o, "    const tide_grid_shape *s = &%s;\n    const tide_grid_dir *d = tide_grid_dir_of(g);\n"
                     "    if (!d) return 0u;\n"
                     "    const uint32_t cells = tide_grid_chunk_bytes(s), each = 16u + ((cells + 7u) & ~7u);\n"
                     "    const uint64_t size = 48u + (uint64_t)d->records * each;\n"
                     "    if (size > UINT32_MAX / 4u) return 0u;\n"
                     "    uint8_t *blob = tide_scratch_memory((size_t)size);\n    memset(blob, 0, (size_t)size);\n"
                     "    const int32_t head[12] = {(int32_t)s->dims, (int32_t)s->cell, (int32_t)s->shift[0], (int32_t)s->shift[1],\n"
                     "                              (int32_t)s->shift[2], d->size[0], d->size[1], d->size[2], (int32_t)d->records, 0, 0, 0};\n"
                     "    memcpy(blob, head, sizeof head);\n"
                     "    const tide_heap *heap = tide_heap_read(g.at >> 30);\n"
                     "    const tide_grid_record *records = tide_grid_records(d);\n"
                     "    for (uint32_t r = 0; r < d->records; r++) {\n"
                     "        uint8_t *at = blob + 48u + (size_t)r * each;\n"
                     "        memcpy(at, records[r].chunk, sizeof records[r].chunk);\n"
                     "        memcpy(at + 16u, tide_grid_chunk_at(heap, records[r].block), cells);\n    }\n"
                     "    return tide_packet_text(out, blob, (uint32_t)size);\n}\n\n",
                  shape);
    }
}

// Whether any of the match's archetypes keeps component `c`.
static bool embed_component_kept(const gen *g, const int c)
{
    for (int a = 0; a < g->prog->archetypes.count; a++) {
        if (!arch_local(g, a) && arch_stores(g, a, c)) return true;
    }
    return false;
}

// Whether the schema lists component `c` as one: every match component the game declares, whether or not an
// entity has it yet, as an editor can give it to one (a Unity prefab's TideEntity), which then has it
static bool embed_component_listed(const gen *g, const int c)
{
    const decl *d = g->prog->components.items[c];
    return embed_component_kept(g, c) || (!d->is_local && !d->builtin && !d->placement_of && d->unit);
}

// For each of the match's rows that have component `c`: `body`, with `e` the
// entity and `i` the row of archetype `a`.
// In a sharded game, for each world the frame draws (tide_frame_shards), with
// `w`, `previous` and `alpha` that world's, what it draws of its rows
static void gen_embed_worlds_begin(gen *g)
{
    if (!sharded(g)) return;
    sb_put(&g->c, "        for (int32_t tide_k = -1; tide_k < (int32_t)tide_view_more_count; tide_k++) {\n"
                  "        const tide_view_worlds *tide_v = tide_view_of(tide_k);\n"
                  "        if (!tide_v->current) continue;\n"
                  "        tide_view_switch(tide_k, (tide_local *)(uintptr_t)l);\n"
                  "        const tide_world *w = tide_v->current;\n        const tide_world *previous = tide_v->previous;\n"
                  "        const float alpha = tide_k < 0 ? home_alpha : tide_v->alpha;\n"
                  "        (void)previous;\n        (void)alpha;\n");
}

static void gen_embed_worlds_end(gen *g)
{
    if (sharded(g)) sb_put(&g->c, "        }\n        tide_view_switch(-1, (tide_local *)(uintptr_t)l);\n");
}

static void gen_embed_rows(gen *g, const int c, const char *body)
{
    sb *o = &g->c;
    gen_embed_worlds_begin(g);
    for (int a = 0; a < g->prog->archetypes.count; a++) {
        if (arch_local(g, a) || !arch_stores(g, a, c)) continue;
        sb_printf(o, "        for (uint32_t i = 0; i < w->%s.count; i++) {\n", arch_name(g, a));
        if (comp_sparse(g, c)) sb_printf(o, "            if (!%s) continue;\n", presence_cell(g, "w", a, c, "i", false));
        sb_printf(o, "            const tide_entity e = *%s;\n            (void)e;\n", arch_cell(g, "w", a, 0, "tide_entity", "i", false));
        if (sharded(g)) sb_put(o, "            if (tide_view_hides(w, e)) continue; // Another world drawn has it\n");
        sb_printf(o, "#define TIDE_EMBED_ROW tide_row_%s(&w->%s, %s, %d, i)\n", type_cname(g->prog->components.items[c]),
                  arch_name(g, a), arch_columns(g, a), arch_column(g, a, c));
        sb_put(o, body);
        sb_put(o, "#undef TIDE_EMBED_ROW\n        }\n");
    }
    gen_embed_worlds_end(g);
}

// The match's events tidec --embed taps (add_embed_taps in compile.c): those with a tap among their local handlers.
// The schema numbers them after the commands, in the game's order.
static bool embed_tapped(const decl *event)
{
    for (int i = 0; i < event->listeners.count; i++) {
        if (event->listeners.items[i]->embed_tap) return true;
    }
    return false;
}

static int embed_event_count(const program *prog)
{
    int n = 0;
    for (int i = 0; i < prog->events.count; i++) n += embed_tapped(prog->events.items[i]);
    return n;
}

static int embed_event_type(const program *prog, const layout_decls *types, const decl *event)
{
    int n = types->count + prog->command_types.count;
    for (int i = 0; i < prog->events.count; i++) {
        if (prog->events.items[i] == event) return n;
        n += embed_tapped(prog->events.items[i]);
    }
    return -1;
}

// What the taps call as they hear an event (gen_heard), before the frame's views: its record, kept until the next
// packet takes it, with its text and containers copied out of the heap it was heard with into the store's STRINGS. The
// packet takes those first, before anything of its own, so their handles are the packet's as they are.
static void gen_embed_heard(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_put(o, "#include \"tide/embed_game.h\"\n\n");
    gen_embed_packers(g);
    if (!embed_event_count(prog)) return;
    const layout_decls types = layout_types(prog);
    sb_put(o, "// The match's events this machine heard since the last view packet, as its local handlers would, for another\n"
              "// program (tide/embed.h): the packet's EVENTS records, and in its text what their heap fields held\n"
              "static tide_packet tide_embed_heard_store;\nstatic uint32_t tide_embed_heard_count;\n\n");
    for (int i = 0; i < prog->events.count; i++) {
        const decl *d = prog->events.items[i];
        if (!embed_tapped(d)) continue;
        const char *name = type_cname(d);
        const bool fix = embed_has_fix(d);
        sb_printf(o, "TIDE_HELPER void tide_embed_heard_%s(const %s *value, const bool verified, const tide_entity target, "
                     "const uint32_t tick)\n{\n    %s v = *value;\n", name, name, name);
        if (fix) {
            sb_printf(o, "    const uint32_t mark = tide_scratch_mark();\n    tide_embed_fix_%s(&v, &tide_embed_heard_store);\n"
                         "    tide_scratch_reset(mark);\n", name);
        }
        sb_printf(o, "    const uint32_t value_bytes = ((uint32_t)sizeof v + 7u) & ~7u;\n"
                     "    const uint32_t head[8] = {TIDE_PACKET_EVENT + value_bytes, %du, verified ? 2u : 1u, tick, target.index,\n"
                     "                              target.generation, TIDE_PACKET_NONE, 0u};\n"
                     "    uint8_t *at = tide_packet_grow(&tide_embed_heard_store, TIDE_PACKET_EVENT + value_bytes, true);\n"
                     "    memcpy(at, head, sizeof head);\n    memcpy(at + TIDE_PACKET_EVENT, &v, sizeof v);\n"
                     "    tide_embed_heard_count++;\n}\n\n",
                  embed_event_type(prog, &types, d));
    }
    sb_put(o, "static void tide_embed_heard_drop(void)\n{\n"
              "    tide_embed_heard_store.size = 0;\n    tide_embed_heard_store.text_size = 0;\n"
              "    tide_embed_heard_store.entry_count = 0;\n    tide_embed_heard_count = 0;\n}\n\n"
              "// The store's STRINGS into a packet that has none yet: its handles, entry for entry, are the packet's\n"
              "static void tide_embed_heard_strings(tide_packet *out)\n{\n"
              "    for (uint32_t i = 0; i < tide_embed_heard_store.entry_count; i++) {\n"
              "        const uint32_t *entry = tide_embed_heard_store.entries + (size_t)i * 2u;\n"
              "        tide_packet_text(out, tide_embed_heard_store.text + entry[0], entry[1]);\n    }\n}\n\n"
              "// The records, each with where its entity is among the packet's while it's the same one\n"
              "static void tide_embed_heard_write(tide_packet *out, const uint32_t *place, const uint32_t places)\n{\n"
              "    if (!tide_embed_heard_count) return;\n"
              "    const uint32_t size = tide_embed_heard_store.size;\n"
              "    const uint32_t section[6] = {TIDE_PACKET_EVENTS, 0u, tide_embed_heard_count, 0u, size, 0u};\n"
              "    uint8_t *records = tide_packet_grow(out, TIDE_PACKET_SECTION + size, true);\n"
              "    memcpy(records, section, sizeof section);\n    out->sections++;\n    records += TIDE_PACKET_SECTION;\n"
              "    memcpy(records, tide_embed_heard_store.bytes, size);\n"
              "    for (uint32_t at = 0; at + TIDE_PACKET_EVENT <= size;) {\n"
              "        uint32_t head[8];\n        memcpy(head, records + at, sizeof head);\n"
              "        if (head[4] < places && place[head[4]] != TIDE_PACKET_NONE) {\n"
              "            uint32_t row[2];\n"
              "            memcpy(row, out->bytes + TIDE_PACKET_HEADER + (size_t)place[head[4]] * TIDE_PACKET_ENTITY, sizeof row);\n"
              "            if (row[1] == head[5]) head[6] = place[head[4]];\n        }\n"
              "        memcpy(records + at, head, sizeof head);\n"
              "        at += head[0] ? head[0] : size;\n    }\n"
              "    tide_embed_heard_drop();\n}\n\n");
}

static void gen_embed(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const layout_decls types = layout_types(prog);
    sb_put(o, "// For another program (tide/embed.h): the view packet, commands and scenes by name\n\n");

    // The view packet: what views see this frame (tide/embed_game.h says its bytes)
    sb_put(o, "static void tide_embed_view(const void *world, const void *previous_world, float alpha, const void *local_state,\n"
              "                            uint32_t verified_tick, tide_packet *out)\n{\n"
              "    const tide_world *w = world;\n    const tide_world *previous = previous_world;\n"
              "    const tide_local *l = local_state;\n    (void)previous;\n    (void)l;\n"
              "    tide_packet_begin(out);\n");
    const bool heard = embed_event_count(prog) > 0;
    sb_printf(o, "    if (!w) {\n%s        tide_packet_end(out, 0x%016llXull, false, 0, verified_tick, 1.0f, 0);\n        return;\n    }\n",
              heard ? "        tide_embed_heard_drop(); // Nothing's heard outside a match\n" : "", (unsigned long long)g->game_hash);
    sb_put(o, "    if (!(alpha >= 0.0f && alpha <= 1.0f)) alpha = 1.0f;\n");
    if (heard) sb_put(o, "    tide_embed_heard_strings(out); // First, so the heard events' handles are the packet's\n");
    if (world_heap(prog, false) || world_heap(prog, true)) {
        sb_printf(o, "    tide_text_use(%s, %s);\n", world_heap(prog, false) ? "(tide_heap *)(uintptr_t)&w->heap" : "NULL",
                  world_heap(prog, true) ? "(tide_heap *)(uintptr_t)&l->heap" : "NULL");
    }
    sb_put(o, "    const uint32_t mark = tide_scratch_mark();\n    uint32_t places = w->entities.next_unused;\n");
    if (sharded(g)) { // Every world the frame drew: the host gave the others again (tide_frame_shards)
        sb_put(o, "    const float home_alpha = alpha;\n"
                  "    tide_view_home = (tide_view_worlds){.current = w, .previous = previous, .alpha = alpha};\n"
                  "    for (uint32_t k = 0; k < tide_view_more_count; k++) {\n"
                  "        const tide_world *o = tide_view_more[k].current;\n"
                  "        if (o && o->entities.next_unused > places) places = o->entities.next_unused;\n    }\n");
    }
    sb_put(o, "    uint32_t *place = tide_packet_places(out, places);\n"
              "    (void)place;\n    uint32_t n = 0;\n");
    const int owner = prog->owner ? prog->owner->index : -1;
    if (owner >= 0) {
        sb_printf(o, "    const tide_player_id me = l->%s.%s;\n    (void)me;\n", type_cname(prog->session),
                  field_cname(&prog->session->fields.items[1]));
    }
    // Every entity of the match, with its owner's flags
    gen_embed_worlds_begin(g);
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t i = 0; i < w->%s.count; i++) {\n", arch_name(g, a));
        sb_printf(o, "        const tide_entity e = *%s;\n        uint32_t flags = 0;\n", arch_cell(g, "w", a, 0, "tide_entity", "i", false));
        if (sharded(g)) sb_put(o, "        if (tide_view_hides(w, e)) continue; // Another world drawn has it\n");
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
        // ...and what it was spawned as, which tells another program's object for it from one for another entity a
        // rollback gave its handle
        sb_put(o, "        const uint32_t row[6] = {e.index, e.generation, flags, TIDE_PACKET_NONE, TIDE_PACKET_NONE,\n"
                  "                                 tide_entity_born(&w->entities, e)};\n"
                  "        memcpy(tide_packet_grow(out, TIDE_PACKET_ENTITY, false), row, sizeof row);\n"
                  "        if (e.index < places) place[e.index] = n;\n        n++;\n    }\n");
    }
    gen_embed_worlds_end(g);
    // Parents, now every entity has its place
    if (prog->hierarchy[0]) {
        // In a sharded game, as the world that owns each has it (tide_view_parent_of)
        sb_printf(o, "    for (uint32_t k = 0; k < n; k++) {\n"
                     "        uint32_t row[6];\n        uint8_t *at = out->bytes + TIDE_PACKET_HEADER + (size_t)k * TIDE_PACKET_ENTITY;\n"
                     "        memcpy(row, at, sizeof row);\n"
                     "        const tide_entity parent = tide_%sparent_of(w, (tide_entity){row[0], row[1]});\n"
                     "        if (tide_entity_is_null(parent) || parent.index >= places) continue;\n"
                     "        row[4] = place[parent.index];\n        memcpy(at, row, sizeof row);\n    }\n",
                  sharded(g) ? "view_" : "");
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
        sb_printf(&body, "            rows[k] = e.index < places ? place[e.index] : 0u;\n"
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
            sb_put(&local_body, "            rows[k] = e.index < places ? place[e.index] : 0u;\n"
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
    // What this machine heard since the last packet
    if (heard) sb_put(o, "    tide_embed_heard_write(out, place, places);\n");
    // The other worlds were for this packet alone, as for a frame
    if (sharded(g)) sb_put(o, "    tide_view_more_count = 0u;\n    tide_view_home = (tide_view_worlds){0};\n    tide_view_drop_grids();\n");
    sb_printf(o, "    tide_scratch_reset(mark);\n"
                 "    tide_packet_end(out, 0x%016llXull, true, (uint32_t)w->Time.tick, verified_tick, alpha, n);\n}\n\n",
              (unsigned long long)g->game_hash);

    // Commands and the events another program hears: their values' layout, after the layout's types; and sending a
    // command as local code does
    layout_decls extras = {0};
    for (int k = 0; k < prog->command_types.count; k++) vec_push(extras, prog->command_types.items[k]);
    for (int k = 0; k < prog->events.count; k++) {
        if (embed_tapped(prog->events.items[k])) vec_push(extras, prog->events.items[k]);
    }
    for (int k = 0; k < extras.count; k++) {
        const decl *d = extras.items[k];
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
    if (extras.count) {
        sb_put(o, "static const tide_layout_type tide_embed_extra_types[] = {\n");
        for (int k = 0; k < extras.count; k++) {
            const decl *d = extras.items[k];
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
    // A command's text and lists come after its value, as the packet's STRINGS has them (a count, its {offset, length}
    // entries, then their bytes), each field holding its entry's place + 1, 0 for empty
    bool any_text = false;
    for (int k = 0; k < prog->command_types.count; k++) any_text |= decl_has_text(prog->command_types.items[k]);
    if (any_text) {
        sb_put(o, "// Entry `handle` of a command's tail: its bytes, or false where the tail doesn't have it whole\n"
                  "TIDE_HELPER bool tide_embed_tail(const uint8_t *tail, const uint32_t size, const uint32_t handle, const uint8_t **bytes,\n"
                  "                            uint32_t *length)\n{\n"
                  "    *bytes = NULL;\n    *length = 0;\n    if (!handle) return true;\n"
                  "    uint32_t count;\n    if (size < sizeof count) return false;\n    memcpy(&count, tail, sizeof count);\n"
                  "    if (handle > count || 4u + (uint64_t)count * 8u > size) return false;\n"
                  "    uint32_t entry[2];\n    memcpy(entry, tail + 4u + (size_t)(handle - 1u) * 8u, sizeof entry);\n"
                  "    const uint64_t start = 4u + (uint64_t)count * 8u + entry[0];\n"
                  "    if (start + entry[1] > size || entry[1] > (uint32_t)INT32_MAX) return false;\n"
                  "    *bytes = tail + start;\n    *length = entry[1];\n    return true;\n}\n\n"
                  "// A command's text field, its entry made the scratch area's text: false for one that isn't there or isn't UTF-8\n"
                  "TIDE_HELPER bool tide_embed_command_text(const uint8_t *tail, const uint32_t size, tide_text *field)\n{\n"
                  "    const uint8_t *bytes;\n    uint32_t length;\n"
                  "    if (!tide_embed_tail(tail, size, field->at, &bytes, &length)) return false;\n"
                  "    *field = (tide_text){0};\n    if (!length) return true;\n"
                  "    const char *text = (const char *)bytes;\n"
                  "    if (!tide_utf8_valid(text, (int32_t)length)) return false;\n"
                  "    *field = tide_text_temp((tide_str){text, (int32_t)length, tide_utf8_chars(text, (int32_t)length)});\n"
                  "    return true;\n}\n\n"
                  "// ...and a list field of `element`-byte elements side by side\n"
                  "TIDE_HELPER bool tide_embed_command_list(const uint8_t *tail, const uint32_t size, tide_list *field, const uint32_t element)\n{\n"
                  "    const uint8_t *bytes;\n    uint32_t length;\n"
                  "    if (!tide_embed_tail(tail, size, field->at, &bytes, &length) || length % element) return false;\n"
                  "    *field = length ? tide_list_from(bytes, (int32_t)(length / element), element) : (tide_list){0};\n"
                  "    return true;\n}\n\n");
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
            sb_printf(o, "        if (size < sizeof(%s)) return false;\n        %s v;\n        memcpy(&v, value, sizeof v);\n"
                         "        const uint8_t *tail = (const uint8_t *)value + sizeof v;\n"
                         "        const uint32_t tail_size = size - (uint32_t)sizeof v;\n        (void)tail;\n        (void)tail_size;\n"
                         "        const uint32_t mark = tide_scratch_mark();\n        bool ok = true;\n", name, name);
            path_list paths = {0};
            text_paths(d, "", &paths);
            for (int i = 0; i < paths.count; i++) {
                const heap_path *hp = &paths.items[i];
                if (hp->list && embed_list_plain(hp->list)) {
                    sb_printf(o, "        ok = ok && tide_embed_command_list(tail, tail_size, &v.%s, (uint32_t)sizeof(%s));\n", hp->path,
                              elem_ctype(list_elem(hp->list)));
                } else if (hp->list || hp->map || hp->grid) {
                    // What another program's bytes can't hold: empty
                    sb_printf(o, "        ok = ok && !v.%s.at;\n        memset(&v.%s, 0, sizeof v.%s);\n", hp->path, hp->path, hp->path);
                } else {
                    sb_printf(o, "        ok = ok && tide_embed_command_text(tail, tail_size, &v.%s);\n", hp->path);
                }
            }
            // What local code's Send would refuse as too big for a command ends the program; this says false
            sb_put(o, "        if (ok) {\n            uint32_t room = TIDE_COMMAND_BYTES;\n");
            for (int i = 0; i < paths.count; i++) {
                const heap_path *hp = &paths.items[i];
                if (hp->list && type_has_bytes((type){TY_LIST, (decl *)hp->list})) {
                    sb_printf(o, "            room += 8u + (uint32_t)tide_list_count(v.%s);\n", hp->path);
                }
            }
            sb_printf(o, "            uint8_t *bytes = malloc(room);\n            if (!bytes) tide_out_of_memory();\n"
                         "            const uint32_t written = tide_write_command_%s(target, v, bytes, room);\n"
                         "            ok = written && tide_game_check_command(bytes, written, NULL);\n            free(bytes);\n        }\n"
                         "        if (ok) tide_local_send_%s(l, target, v);\n        tide_scratch_reset(mark);\n        return ok;\n    }\n",
                      name, name);
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
        if (!embed_component_listed(g, c)) continue;
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
    // ...and each command's, which another program's makes start at
    for (int k = 0; k < prog->command_types.count; k++) {
        const decl *d = prog->command_types.items[k];
        const char *name = type_cname(d);
        sb_printf(o, "    case %d: { // " STR_FMT "\n        if (size != sizeof(%s)) return false;\n", types.count + k,
                  STR_ARG(d->qualified), name);
        path_list paths = {0};
        text_paths(d, "", &paths);
        if (paths.count) sb_put(o, "        const uint32_t mark = tide_scratch_mark();\n");
        sb_printf(o, "        %s v = ", name);
        gen_value(g, o, d, NULL, 0);
        sb_put(o, ";\n");
        for (int i = 0; i < paths.count; i++) sb_printf(o, "        memset(&v.%s, 0, sizeof v.%s);\n", paths.items[i].path, paths.items[i].path);
        if (paths.count) sb_put(o, "        tide_scratch_reset(mark);\n");
        sb_put(o, "        memcpy(out, &v, sizeof v);\n        return true;\n    }\n");
    }
    sb_put(o, "    default:\n        return false;\n    }\n}\n\n");

    // The tables
    int components = 0;
    for (int c = 0; c < prog->components.count; c++) components += embed_component_listed(g, c);
    if (components) {
        sb_put(o, "static const int32_t tide_embed_components[] = {");
        for (int c = 0; c < prog->components.count; c++) {
            if (embed_component_listed(g, c)) sb_printf(o, "%d, ", layout_index(&types, prog->components.items[c]));
        }
        sb_put(o, "};\nstatic const int32_t tide_embed_component_locals[] = {");
        for (int c = 0; c < prog->components.count; c++) {
            const decl *d = prog->components.items[c];
            if (embed_component_listed(g, c)) sb_printf(o, "%d, ", d->local_part ? layout_index(&types, d->local_part) : -1);
        }
        sb_put(o, "};\nstatic const bool tide_embed_component_scenes[] = {");
        for (int c = 0; c < prog->components.count; c++) {
            if (embed_component_listed(g, c)) sb_put(o, prog->components.items[c]->is_scene ? "true, " : "false, ");
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
    const int events = embed_event_count(prog);
    if (events) {
        sb_put(o, "static const tide_embed_named tide_embed_events[] = {\n");
        for (int k = 0; k < prog->events.count; k++) {
            const decl *d = prog->events.items[k];
            if (!embed_tapped(d)) continue;
            const type t = {TY_STRUCT, (decl *)d};
            sb_printf(o, "    {\"%s\", %d},\n", layout_name(t), embed_event_type(prog, &types, d));
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
    // The layout's types', then the commands' and events' after them, by the schema's numbers
    layout_decls described = {0};
    for (int i = 0; i < types.count; i++) vec_push(described, types.items[i]);
    for (int i = 0; i < extras.count; i++) vec_push(described, extras.items[i]);
    int fields = 0;
    for (int i = 0; i < described.count; i++) {
        for (int k = 0; k < described.items[i]->fields.count; k++) fields += embed_field_kind(described.items[i]->fields.items[k].type) != 0;
    }
    if (fields) {
        sb_put(o, "static const tide_embed_field tide_embed_fields[] = {\n");
        for (int i = 0; i < described.count; i++) {
            const decl *d = described.items[i];
            for (int k = 0; k < d->fields.count; k++) {
                const field *f = &d->fields.items[k];
                const int kind = embed_field_kind(f->type);
                if (!kind) continue;
                if (kind == 2) {
                    // The element's size, as the packet has the elements (their heap fields its handles)
                    const type element = list_elem(f->type.decl);
                    sb_printf(o, "    {%d, \"" STR_FMT "\", 2, (uint32_t)sizeof(%s), ", i, STR_ARG(f->name), elem_ctype(element));
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
                 "    %d, %s, %s, %s,\n    %d, %s, %s,\n    %d, %s,\n    %d, %s,\n    %d, %s,\n    %d, %s,\n    %d, %s,\n"
                 "    tide_embed_view, tide_embed_command, tide_embed_start, tide_embed_schema, tide_embed_defaults,\n"
                 "    %d, %s,\n};\n",
              components, components ? "tide_embed_components" : "NULL", components ? "tide_embed_component_locals" : "NULL",
              components ? "tide_embed_component_scenes" : "NULL",
              singletons, singletons ? "tide_embed_singletons" : "NULL", singletons ? "tide_embed_singleton_locals" : "NULL",
              prog->command_types.count, prog->command_types.count ? "tide_embed_commands" : "NULL",
              prog->start_scenes.count, prog->start_scenes.count ? "tide_embed_scenes" : "NULL",
              prefabs, prefabs ? "tide_embed_prefabs" : "NULL",
              fields, fields ? "tide_embed_fields" : "NULL",
              extras.count, extras.count ? "tide_embed_extra_types" : "NULL", events, events ? "tide_embed_events" : "NULL");
}
