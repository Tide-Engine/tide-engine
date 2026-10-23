// Shards' messages (tide/shard.h): the outbox, moving and lending entities,
// events to other shards' entities and deliveries that wait, and what a shard
// takes from the others. Included by codegen.c after shard_codegen.inc.

// The outbox, and messages into it: a record of where each goes (route,
// target, entity), its number and size, then the message.
static void gen_shard_outbox(gen *g)
{
    sb *o = &g->c;
    sb_put(o, "TIDE_HELPER tide_writer tide_shard_begin(tide_world *w, const uint8_t kind, const bool numbered)\n{\n"
              "    tide_writer out = {.grows = true};\n"
              "    tide_shard_header(&out, (tide_shard_kind)kind, (uint32_t)tide_shard_self(w),\n"
              "                      numbered ? (uint32_t)++w->tide_shard_state.sent : 0u);\n"
              "    return out;\n}\n\n"
              "TIDE_HELPER void tide_shard_post(tide_world *w, const uint32_t route, const uint32_t target, const tide_entity entity,\n"
              "                                 tide_writer *message)\n{\n"
              "    if (message->overflow || message->size < 9u) tide_out_of_memory();\n"
              "    const uint32_t number = (uint32_t)message->data[5] | (uint32_t)message->data[6] << 8\n"
              "                          | (uint32_t)message->data[7] << 16 | (uint32_t)message->data[8] << 24;\n"
              "    tide_writer record = {.grows = true};\n"
              "    tide_write_u32(&record, route);\n    tide_write_u32(&record, target);\n"
              "    tide_write_u32(&record, entity.index);\n    tide_write_u32(&record, entity.generation);\n"
              "    tide_write_u32(&record, number);\n    tide_write_u32(&record, message->size);\n"
              "    tide_write_bytes(&record, message->data, message->size);\n"
              "    if (record.overflow) tide_out_of_memory();\n"
              "    tide_shard_bytes_append(&w->tide_shard_state.outbox, record.data, record.size, TIDE_IN_MATCH);\n"
              "    free(record.data);\n    free(message->data);\n    *message = (tide_writer){0};\n}\n\n"
              // Acknowledged messages go
              "TIDE_HELPER void tide_shard_acknowledged(tide_world *w, const uint32_t through)\n{\n"
              "    tide_shard_state *s = &w->tide_shard_state;\n"
              "    uint32_t size;\n    bool copied;\n"
              "    const uint8_t *flat = tide_shard_bytes_flat(s->outbox, &size, &copied);\n"
              "    tide_writer keep = {.grows = true};\n"
              "    tide_reader r = {flat, size, 0, false};\n    tide_shard_outgoing m;\n"
              "    while (tide_shard_next_outgoing(&r, &m)) {\n"
              "        if (m.number && m.number <= through) continue;\n"
              "        tide_write_u32(&keep, m.route);\n        tide_write_u32(&keep, m.target);\n"
              "        tide_write_u32(&keep, m.entity.index);\n        tide_write_u32(&keep, m.entity.generation);\n"
              "        tide_write_u32(&keep, m.number);\n        tide_write_u32(&keep, m.size);\n"
              "        tide_write_bytes(&keep, m.data, m.size);\n    }\n"
              "    tide_shard_bytes_done(s->outbox, flat, copied);\n"
              "    if (keep.overflow) tide_out_of_memory();\n"
              "    tide_shard_bytes_set(&s->outbox, keep.data, keep.size, TIDE_IN_MATCH);\n"
              "    free(keep.data);\n"
              "    if ((int32_t)through > s->acked) s->acked = (int32_t)through;\n}\n\n"
              "TIDE_SHARD_FN uint8_t *tide_shard_outbox(const void *world, uint32_t *size)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n"
              "    bool copied;\n    uint32_t n;\n"
              "    const uint8_t *flat = tide_shard_bytes_flat(w->tide_shard_state.outbox, &n, &copied);\n"
              "    *size = n;\n    if (!n) return NULL;\n"
              "    uint8_t *bytes = malloc(n);\n    if (!bytes) tide_out_of_memory();\n"
              "    memcpy(bytes, flat, n);\n    tide_shard_bytes_done(w->tide_shard_state.outbox, flat, copied);\n"
              "    return bytes;\n}\n\n");
}

// Pins: what this shard borrowed, kept a while (its key, then the tick it may go)
static void gen_shard_pins(gen *g)
{
    sb *o = &g->c;
    sb_put(o, "#define TIDE_SHARD_PIN_TICKS 30 // What a shard borrowed stays its own this long, at least, once it has it\n\n"
              "TIDE_HELPER bool tide_shard_pinned(const tide_world *w, const int64_t key)\n{\n"
              "    const int32_t at = tide_shard_find(w->tide_shard_state.pins, key, 2);\n"
              "    return at >= 0 && tide_shard_long(w->tide_shard_state.pins, at + 1) > (int64_t)w->Time.tick;\n}\n\n"
              "TIDE_HELPER void tide_shard_pin(tide_world *w, const int64_t key)\n{\n"
              "    tide_shard_state *s = &w->tide_shard_state;\n"
              "    const int64_t until = (int64_t)w->Time.tick + TIDE_SHARD_PIN_TICKS;\n"
              "    const int32_t at = tide_shard_find(s->pins, key, 2);\n"
              "    if (at >= 0) *(int64_t *)tide_list_at_mut(s->pins, at + 1, 8u) = until;\n"
              "    else {\n        tide_shard_push(&s->pins, key);\n        tide_shard_push(&s->pins, until);\n    }\n}\n\n"
              "TIDE_HELPER void tide_shard_unask(tide_world *w, const tide_entity e)\n{\n"
              "    const int32_t at = tide_shard_find(w->tide_shard_state.asked, tide_shard_key(e), 1);\n"
              "    if (at >= 0) tide_shard_drop(&w->tide_shard_state.asked, at, 1);\n}\n\n");
}

// Singletons: each the root's, read everywhere as copies, lent to be changed

// Releases what a singleton's value holds on the heap (`value`: the C that
// names it, ending in '.'), as one statement or none: the engine's own
// singletons (the visibility directory) have no release function of their own
static void put_singleton_release(sb *o, const decl *d, const char *value)
{
    if (!decl_has_text(d)) return;
    path_list paths = {0};
    text_paths(d, "", &paths);
    for (int k = 0; k < paths.count; k++) {
        const heap_path *hp = &paths.items[k];
        if (hp->list) sb_printf(o, " tide_list%d_release(&%s%s, TIDE_IN_MATCH);", hp->list->index, value, hp->path);
        else if (hp->map) sb_printf(o, " tide_map%d_release(&%s%s, TIDE_IN_MATCH);", hp->map->index, value, hp->path);
        else if (hp->grid) sb_printf(o, " tide_grid_release(&%s%s, &%s, TIDE_IN_MATCH);", value, hp->path, grid_shape_name(hp->grid));
        else sb_printf(o, " tide_text_release(&%s%s, TIDE_IN_MATCH);", value, hp->path);
    }
}

static void gen_shard_singletons(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const client_codec *codec = &g->value_codec;
    sb_put(o, "TIDE_HELPER void tide_shard_put_singleton(tide_writer *out, const tide_world *w, const uint32_t i)\n{\n"
              "    (void)out;\n    (void)w;\n    switch (i) {\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!shard_shares(prog, d)) continue;
        sb_printf(o, "    case %du: tide_client_put%d(out, &w->%s, 0); break;\n", shard_singleton(prog, d),
                  client_codec_id(codec, (type){TY_SINGLETON, (decl *)d}), type_cname(d));
    }
    sb_put(o, "    default: break;\n    }\n}\n\n");
    // Read into the world (`keep`), what it had going; or read past. False,
    // changing nothing, for bytes that aren't one
    sb_put(o, "TIDE_HELPER bool tide_shard_take_singleton(tide_world *w, tide_reader *in, const uint32_t i, const bool keep)\n{\n"
              "    uint64_t budget = SIZE_MAX;\n    (void)budget;\n    (void)w;\n    (void)in;\n    (void)keep;\n    switch (i) {\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!shard_shares(prog, d)) continue;
        const char *name = type_cname(d);
        sb_printf(o, "    case %du: {\n        %s v;\n        memset(&v, 0, sizeof v);\n"
                     "        if (!tide_client_get%d(in, &v, TIDE_IN_MATCH, 0, &budget)) {", shard_singleton(prog, d), name,
                  client_codec_id(codec, (type){TY_SINGLETON, (decl *)d}));
        put_singleton_release(o, d, "v.");
        sb_put(o, " return false; }\n        if (!keep) {");
        put_singleton_release(o, d, "v.");
        sb_put(o, " return true; }\n        ");
        char current[160];
        snprintf(current, sizeof current, "w->%s.", name);
        put_singleton_release(o, d, current);
        sb_put(o, "\n");
        sb_printf(o, "        w->%s = v;\n        return true;\n    }\n", name);
    }
    sb_put(o, "    default: return false;\n    }\n}\n\n");
    // The fields of singletons holding a C library's state ([Native]) but those, the root's, as their bytes
    sb_put(o, "TIDE_HELPER void tide_shard_put_shared(tide_writer *out, const tide_world *w)\n{\n    (void)out;\n    (void)w;\n");
    sb shared_size = {0};
    sb_put(&shared_size, "0u");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!d->native_state) continue;
        for (int k = 0; k < d->fields.count; k++) {
            const field *f = &d->fields.items[k];
            if (f->native || f->hidden || f->is_local) continue;
            sb_printf(o, "    tide_write_bytes(out, &w->%s.%s, sizeof w->%s.%s);\n", type_cname(d), field_cname(f), type_cname(d), field_cname(f));
            sb_printf(&shared_size, " + sizeof(((tide_world *)0)->%s.%s)", type_cname(d), field_cname(f));
        }
    }
    sb_put(o, "}\n\n");
    // ...and taken back into the world, on any shard but the root
    sb_printf(o, "TIDE_HELPER void tide_shard_apply_shared(tide_world *w)\n{\n"
                 "    const tide_shard_state *s = &w->tide_shard_state;\n"
                 "    if (!s->shard || s->role == TIDE_SHARD_ROOT_ROLE || tide_list_count(s->shared) != (int32_t)(%s)) return;\n"
                 "    uint32_t size;\n    bool copied;\n"
                 "    const uint8_t *flat = tide_shard_bytes_flat(s->shared, &size, &copied);\n"
                 "    tide_reader r = {flat, size, 0, false};\n    (void)r;\n", shared_size.data);
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!d->native_state) continue;
        for (int k = 0; k < d->fields.count; k++) {
            const field *f = &d->fields.items[k];
            if (f->native || f->hidden || f->is_local) continue;
            sb_printf(o, "    memcpy(&w->%s.%s, tide_read_bytes(&r, sizeof w->%s.%s), sizeof w->%s.%s);\n", type_cname(d),
                      field_cname(f), type_cname(d), field_cname(f), type_cname(d), field_cname(f));
        }
    }
    sb_put(o, "    tide_shard_bytes_done(s->shared, flat, copied);\n}\n\n");
    sb_printf(o, "TIDE_SHARD_FN uint8_t *tide_shard_singletons_of(const void *world, uint32_t *size)\n{\n"
                 "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n"
                 "    const uint64_t owned = (uint64_t)w->tide_shard_state.singletons;\n"
                 "    *size = 0;\n    if (!w->tide_shard_state.shard) return NULL;\n"
                 "    tide_writer out = {.grows = true};\n    uint32_t count = 0;\n"
                 "    for (uint32_t i = 0; i < %du; i++) count += (owned >> i) & 1u;\n"
                 "    tide_write_u32(&out, count);\n"
                 "    for (uint32_t i = 0; i < %du; i++) {\n"
                 "        if (!((owned >> i) & 1u)) continue;\n"
                 "        tide_write_u32(&out, i);\n        tide_shard_put_singleton(&out, w, i);\n    }\n"
                 "    // Then the root's fields of its singletons holding [Native] state: u32 their size, and their bytes\n"
                 "    const uint32_t at = out.size;\n    tide_write_u32(&out, 0u);\n    tide_shard_put_shared(&out, w);\n"
                 "    if (!out.overflow) tide_shard_patch_u32(out.data + at, out.size - at - 4u);\n"
                 "    if (out.overflow) tide_out_of_memory();\n    *size = out.size;\n    return out.data;\n}\n\n",
              shard_singleton_count(prog), shard_singleton_count(prog));
}

// Whether an archetype's entities have a position: a [Position] or a Transform
static bool shard_placed(gen *g, const int a)
{
    const program *prog = g->prog;
    if (shard_uses(prog, prog->transform) && arch_stores(g, a, prog->transform->index)) return true;
    for (int k = 0; k < prog->positions.count; k++) {
        const decl *d = prog->positions.items[k].component;
        if (shard_uses(prog, d) && arch_stores(g, a, d->index)) return true;
    }
    return false;
}

// The Entity fields of an archetype's components, the engine's aside, in
// order: one with no position goes with the first of them that names one. In
// `out`, the C that reads each, by entity `e`; empty for none
static void shard_named(gen *g, const int a, sb *out)
{
    const program *prog = g->prog;
    for (int c = 0; c < prog->components.count; c++) {
        const decl *comp = prog->components.items[c];
        if (!arch_stores(g, a, c) || comp->builtin || comp->is_local || comp->is_scene) continue;
        for (int k = 0; k < comp->fields.count; k++) {
            const field *f = &comp->fields.items[k];
            if (f->type.kind != TY_ENTITY || f->is_local || f->hidden) continue;
            sb_put(out, "    ");
            if (comp_sparse(g, c)) sb_printf(out, "if (%s) ", presence_cell(g, "w", a, c, "row", false));
            sb_printf(out, "{\n        const tide_entity n = tide_get_%s(w, e).%s;\n"
                           "        if (!tide_entity_is_null(n) && !tide_entity_equal(n, e) && (!here || tide_entity_alive(&w->entities, n))) return n;\n    }\n",
                      type_cname(comp), field_cname(f));
        }
    }
}

// Lending what this shard owns to the shard that asked, and moving what's
// left its region: either goes as an ARRIVE, and what went stays here as a
// ghost of the shard it went to (on the root, which holds no copies, it goes)
static void gen_shard_moves(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_put(o, "TIDE_HELPER void tide_shard_send_tree(tide_world *w, const tide_entity root, const int32_t to, const uint8_t flags)\n{\n"
              "    tide_writer m = tide_shard_begin(w, TIDE_SHARD_ARRIVE, true);\n"
              "    tide_write_u8(&m, flags);\n    tide_write_u32(&m, (uint32_t)tide_shard_self(w));\n"
              "    tide_shard_put_tree(&m, w, root);\n"
              "    tide_shard_send_tasks(&m, w, root); // Its tasks go on where it goes\n"
              "    tide_shard_post(w, TIDE_SHARD_TO_SHARD, (uint32_t)to, root, &m);\n"
              "    w->tide_shard_state.moves++; // It left\n"
              "    if (w->tide_shard_state.role == TIDE_SHARD_ROOT_ROLE && !tide_shard_regional(w)) tide_shard_remove_tree(w, root, true);\n"
              "    else tide_shard_mark_tree(w, root, true, to);\n}\n\n"
              "TIDE_HELPER void tide_shard_lend(tide_world *w, const tide_entity e, const int32_t to)\n{\n"
              "    if (tide_shard_is_singleton(e)) {\n"
              "        tide_writer m = tide_shard_begin(w, TIDE_SHARD_SINGLETON, true);\n"
              "        tide_write_u32(&m, e.index);\n        tide_write_u32(&m, (uint32_t)tide_shard_self(w));\n"
              "        tide_shard_put_singleton(&m, w, e.index);\n"
              "        tide_shard_post(w, TIDE_SHARD_TO_SHARD, (uint32_t)to, (tide_entity){0}, &m);\n"
              "        w->tide_shard_state.singletons = (int64_t)((uint64_t)w->tide_shard_state.singletons & ~((uint64_t)1 << e.index));\n"
              "        return;\n    }\n"
              "    tide_shard_send_tree(w, e, to, TIDE_SHARD_BORROWED);\n}\n\n"
              // An entity with no position, to the shard that owns the entity
              // it goes with, whichever that is now: it goes from here
              "TIDE_HELPER void tide_shard_send_tree_with(tide_world *w, const tide_entity root, const tide_entity anchor)\n{\n"
              "    tide_writer m = tide_shard_begin(w, TIDE_SHARD_ARRIVE, true);\n"
              "    tide_write_u8(&m, 0u);\n    tide_write_u32(&m, (uint32_t)tide_shard_self(w));\n"
              "    tide_shard_put_tree(&m, w, root);\n"
              "    tide_shard_send_tasks(&m, w, root);\n"
              "    tide_shard_post(w, TIDE_SHARD_TO_OWNER, 0u, anchor, &m);\n"
              "    w->tide_shard_state.moves++;\n"
              "    tide_shard_remove_tree(w, root, true);\n}\n\n"
              "\n");
    // Entities kept together: each [Together] field joins the entity that has
    // it and the one it names, and an entity with no position joins the first
    // entity it names. Each group goes where its lead is: the member with a
    // position, then the lowest handle, so every shard that sees the group
    // picks the same one
    sb_put(o, "typedef struct tide_shard_tie {\n    int64_t key;\n    tide_entity e;\n    uint32_t up, lead;\n    bool placed;\n} tide_shard_tie;\n\n"
              "typedef struct tide_shard_ties {\n    tide_shard_tie *at;\n    uint32_t count, room, *slots, slot_count;\n} tide_shard_ties;\n\n"
              "TIDE_HELPER uint32_t tide_shard_tie_slot(const int64_t key, const uint32_t slot_count)\n{\n"
              "    return (uint32_t)(((uint64_t)key * 0x9E3779B97F4A7C15ull) >> 32) & (slot_count - 1u);\n}\n\n"
              "TIDE_HELPER uint32_t tide_shard_tie_find(const tide_shard_ties *l, const tide_entity e)\n{\n"
              "    if (!l->slot_count) return UINT32_MAX;\n"
              "    const int64_t key = tide_shard_key(e);\n"
              "    for (uint32_t h = tide_shard_tie_slot(key, l->slot_count);; h = (h + 1u) & (l->slot_count - 1u)) {\n"
              "        const uint32_t at = l->slots[h];\n"
              "        if (!at) return UINT32_MAX;\n"
              "        if (l->at[at - 1u].key == key) return at - 1u;\n    }\n}\n\n"
              "TIDE_HELPER uint32_t tide_shard_tie_add(tide_shard_ties *l, const tide_world *w, const tide_entity e)\n{\n"
              "    const uint32_t found = tide_shard_tie_find(l, e);\n"
              "    if (found != UINT32_MAX) return found;\n"
              "    if (l->count == l->room) {\n"
              "        l->room = l->room ? l->room * 2u : 16u;\n"
              "        l->at = realloc(l->at, l->room * sizeof *l->at);\n        if (!l->at) tide_out_of_memory();\n    }\n"
              "    if ((l->count + 1u) * 2u > l->slot_count) { // The slots again, twice as many\n"
              "        free(l->slots);\n"
              "        l->slot_count = l->slot_count ? l->slot_count * 2u : 32u;\n"
              "        l->slots = calloc(l->slot_count, sizeof *l->slots);\n        if (!l->slots) tide_out_of_memory();\n"
              "        for (uint32_t i = 0; i < l->count; i++) {\n"
              "            uint32_t h = tide_shard_tie_slot(l->at[i].key, l->slot_count);\n"
              "            while (l->slots[h]) h = (h + 1u) & (l->slot_count - 1u);\n"
              "            l->slots[h] = i + 1u;\n        }\n    }\n"
              "    const uint32_t n = l->count++;\n"
              "    double p[3];\n"
              "    l->at[n] = (tide_shard_tie){tide_shard_key(e), e, n, n, tide_shard_where(w, e, p)};\n"
              "    uint32_t h = tide_shard_tie_slot(l->at[n].key, l->slot_count);\n"
              "    while (l->slots[h]) h = (h + 1u) & (l->slot_count - 1u);\n"
              "    l->slots[h] = n + 1u;\n    return n;\n}\n\n"
              "TIDE_HELPER uint32_t tide_shard_tie_top(tide_shard_ties *l, uint32_t i)\n{\n"
              "    while (l->at[i].up != i) {\n        l->at[i].up = l->at[l->at[i].up].up;\n        i = l->at[i].up;\n    }\n"
              "    return i;\n}\n\n"
              // The lower handle tops a group, so its lead starts as the lowest
              "TIDE_HELPER void tide_shard_tie_join(tide_shard_ties *l, const tide_world *w, const tide_entity a, const tide_entity b)\n{\n"
              "    if (tide_entity_is_null(b) || tide_entity_equal(a, b) || !tide_entity_alive(&w->entities, b)) return;\n"
              "    uint32_t x = tide_shard_tie_top(l, tide_shard_tie_add(l, w, a));\n"
              "    uint32_t y = tide_shard_tie_top(l, tide_shard_tie_add(l, w, b));\n"
              "    if (x == y) return;\n"
              "    if (l->at[y].key < l->at[x].key) {\n        const uint32_t t = x;\n        x = y;\n        y = t;\n    }\n"
              "    l->at[y].up = x;\n}\n\n"
              // The entity the group of `e` goes with, or null for one in none
              "TIDE_HELPER tide_entity tide_shard_tie_lead(tide_shard_ties *l, const tide_entity e)\n{\n"
              "    const uint32_t at = tide_shard_tie_find(l, e);\n"
              "    if (at == UINT32_MAX) return (tide_entity){0};\n"
              "    return l->at[l->at[tide_shard_tie_top(l, at)].lead].e;\n}\n\n");
    // The first entity an entity with no position names (only one alive here, with `here`)
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a) || shard_placed(g, a)) continue;
        sb named = {0};
        shard_named(g, a, &named);
        if (named.len) {
            sb_printf(o, "TIDE_HELPER tide_entity tide_shard_first_named_%d(const tide_world *w, const tide_entity e, const bool here)\n{\n"
                         "    const tide_location loc = tide_entity_location(&w->entities, e);\n"
                         "    const uint32_t row = loc.row;\n    (void)row;\n%s    return (tide_entity){0};\n}\n\n", a, named.data);
        }
    }
    sb_put(o, "TIDE_HELPER void tide_shard_ties_of(tide_shard_ties *l, const tide_world *w)\n{\n    (void)l;\n    (void)w;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb body = {0};
        for (int m = 0; m < prog->together.count; m++) {
            const struct field_mark *mark = &prog->together.items[m];
            const int c = mark->component->index;
            if (!shard_uses(prog, mark->component) || !arch_stores(g, a, c)) continue;
            sb_put(&body, "        ");
            if (comp_sparse(g, c)) sb_printf(&body, "if (%s) ", presence_cell(g, "w", a, c, "row", false));
            sb_printf(&body, "tide_shard_tie_join(l, w, e, tide_get_%s(w, e).%s);\n", type_cname(mark->component),
                      field_cname(&mark->component->fields.items[mark->field]));
        }
        if (!shard_placed(g, a)) { // With no position: with the first entity it names that's here
            sb named = {0};
            shard_named(g, a, &named);
            if (named.len) {
                if (prog->hierarchy[0]) sb_put(&body, "        if (tide_entity_is_null(tide_parent_of(w, e))) {\n");
                else sb_put(&body, "        {\n");
                sb_printf(&body, "            const tide_entity first = tide_shard_first_named_%d(w, e, true);\n"
                                 "            if (!tide_entity_is_null(first)) tide_shard_tie_join(l, w, e, first);\n        }\n", a);
            }
        }
        if (body.len) {
            sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
            sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
            sb_put(o, body.data);
            sb_put(o, "    }\n");
        }
    }
    // Each group's lead: a member with a position before one without, then the lowest handle
    sb_put(o, "    for (uint32_t i = 0; i < l->count; i++) {\n"
              "        const uint32_t top = tide_shard_tie_top(l, i);\n"
              "        const tide_shard_tie *lead = &l->at[l->at[top].lead];\n"
              "        if (l->at[i].placed && (!lead->placed || l->at[i].key < lead->key)) l->at[top].lead = i;\n    }\n}\n\n");
    // Each entity this shard owns whose position is in another shard's region
    // (a quarter of its range past its own, so one on the border doesn't go
    // back and forth), unless it's borrowed for now or held; one in a group
    // where its group's lead is; one with no position where what it names is,
    // or the entity whose code spawned it (tide_follows); and on a retiring
    // shard, everything. What a group this shard leads has elsewhere is asked for
    sb_put(o, "TIDE_SHARD_FN void tide_shard_handoff(tide_world *w)\n{\n"
              "    tide_shard_state *s = &w->tide_shard_state;\n"
              "    if (!s->shard || !tide_list_count(s->regions)) return;\n"
              "    tide_shard_ties links = {0};\n    tide_shard_ties_of(&links, w);\n"
              "    uint32_t count = 0, room = 0;\n    tide_entity *going = NULL, *with = NULL;\n    int32_t *to = NULL;\n"
              // What stays with a lead that's here: it goes along if the lead goes now
              "    uint32_t staying = 0, staying_room = 0;\n    tide_entity *stays = NULL, *led_by = NULL;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (%s) continue; // A ghost: another shard's\n", presence_cell(g, "w", a, prog->shard_ghost->index, "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "        double p[3];\n"
                  "        const bool placed = tide_shard_where(w, e, p);\n"
                  "        if (tide_shard_pinned(w, tide_shard_key(e))) continue;\n"
                  "        if (tide_shard_find(s->held, tide_shard_key(e), 1) >= 0) continue; // Code that waits here needs it\n");
        if (prog->hierarchy[0]) sb_put(o, "        if (!placed && !tide_entity_is_null(tide_parent_of(w, e))) continue; // It goes with its root\n");
        sb_put(o, "        int32_t there = -1;\n        tide_entity anchor = {0};\n        bool led = false;\n"
                  "        const tide_entity lead = tide_shard_tie_lead(&links, e);\n"
                  "        if (!tide_entity_is_null(lead) && !tide_entity_equal(lead, e)) {\n"
                  "            if (tide_has_tide_ghost(w, lead)) { // Its group's lead is another shard's: it goes there\n"
                  "                led = true;\n                there = tide_get_tide_ghost(w, lead).source;\n"
                  "            } else { // Here: it stays with it, and goes along if it goes\n"
                  "                if (staying == staying_room) {\n"
                  "                    staying_room = staying_room ? staying_room * 2u : 16u;\n"
                  "                    stays = realloc(stays, staying_room * sizeof *stays);\n"
                  "                    led_by = realloc(led_by, staying_room * sizeof *led_by);\n"
                  "                    if (!stays || !led_by) tide_out_of_memory();\n                }\n"
                  "                stays[staying] = e;\n                led_by[staying++] = lead;\n"
                  "                continue;\n            }\n        }\n");
        sb named = {0};
        if (!shard_placed(g, a)) shard_named(g, a, &named);
        sb_put(o, "        if (led) {\n"
                  "        } else if (placed) {\n"
                  "            if (tide_shard_mine(w, p, TIDE_SHARD_REACH * 0.25)) continue; // Its own region's: the root's too, when it has one\n"
                  "            there = tide_shard_region_of(w, p);\n        }");
        if (named.len) { // What it names isn't here, and isn't known to be gone: it goes to whichever shard has that
            sb_printf(o, " else if (!tide_entity_is_null(tide_shard_first_named_%d(w, e, false))\n"
                         "                   && !tide_entity_alive(&w->entities, tide_shard_first_named_%d(w, e, false))\n"
                         "                   && !tide_shard_owned(w, tide_shard_first_named_%d(w, e, false))) {\n"
                         "            anchor = tide_shard_first_named_%d(w, e, false);\n        }", a, a, a, a);
        }
        sb_printf(o, " else if (%s) {\n"
                     "            anchor = tide_get_tide_follows(w, e).of;\n"
                     "            if (tide_shard_owned(w, anchor)) anchor = (tide_entity){0}; // It's here\n"
                     "            else if (tide_entity_alive(&w->entities, anchor) && tide_has_tide_ghost(w, anchor)) {\n"
                     "                there = tide_get_tide_ghost(w, anchor).source;\n                anchor = (tide_entity){0};\n            }\n"
                     "        }\n",
                  presence_cell(g, "w", a, prog->shard_anchor->index, "row", false));
        sb_put(o, "        if (there < 0 && tide_entity_is_null(anchor) && s->into) there = s->into - 1; // Retiring\n"
                  "        if (tide_entity_is_null(anchor) && (there < 0 || there == tide_shard_self(w))) continue;\n"
                  "        if (count == room) {\n"
                  "            room = room ? room * 2u : 16u;\n"
                  "            going = realloc(going, room * sizeof *going);\n            to = realloc(to, room * sizeof *to);\n"
                  "            with = realloc(with, room * sizeof *with);\n"
                  "            if (!going || !to || !with) tide_out_of_memory();\n        }\n"
                  "        going[count] = e;\n        with[count] = anchor;\n        to[count++] = there;\n    }\n");
    }
    // What stays with a lead that goes now goes with it, in the same tick: a
    // body, its parts, its joint and the joint's other body move together
    sb_put(o, "    for (uint32_t k = 0; k < staying; k++) {\n"
              "        for (uint32_t i = 0; i < count; i++) {\n"
              "            if (!tide_entity_equal(going[i], led_by[k])) continue;\n"
              "            if (count == room) {\n"
              "                room = room ? room * 2u : 16u;\n"
              "                going = realloc(going, room * sizeof *going);\n                to = realloc(to, room * sizeof *to);\n"
              "                with = realloc(with, room * sizeof *with);\n"
              "                if (!going || !to || !with) tide_out_of_memory();\n            }\n"
              "            going[count] = stays[k];\n            with[count] = with[i];\n            to[count++] = to[i];\n"
              "            break;\n        }\n    }\n"
              "    free(stays);\n    free(led_by);\n");
    // A group this shard leads and stays with: what it has on other shards comes here
    sb_put(o, "    for (uint32_t i = 0; i < links.count; i++) {\n"
              "        const tide_entity e = links.at[i].e, lead = tide_shard_tie_lead(&links, e);\n"
              "        if (!tide_has_tide_ghost(w, e) || tide_has_tide_ghost(w, lead)) continue;\n"
              "        bool leaving = false;\n"
              "        for (uint32_t k = 0; k < count && !leaving; k++) leaving = tide_entity_equal(going[k], lead);\n"
              "        if (leaving || tide_shard_find(s->asked, tide_shard_key(e), 1) >= 0) continue;\n"
              "        tide_shard_push(&s->asked, tide_shard_key(e));\n"
              "        tide_writer m = tide_shard_begin(w, TIDE_SHARD_BORROW, true);\n"
              "        tide_write_u32(&m, (uint32_t)tide_shard_self(w));\n"
              "        tide_write_u32(&m, e.index);\n        tide_write_u32(&m, e.generation);\n"
              "        tide_shard_post(w, TIDE_SHARD_TO_OWNER, 0u, e, &m);\n    }\n"
              "    free(links.at);\n    free(links.slots);\n");
    sb_put(o, "    for (uint32_t i = 0; i < count; i++) {\n"
              "        if (!tide_entity_alive(&w->entities, going[i]) || tide_has_tide_ghost(w, going[i])) continue;\n"
              "        if (tide_entity_is_null(with[i])) tide_shard_send_tree(w, going[i], to[i], 0u);\n"
              "        else tide_shard_send_tree_with(w, going[i], with[i]);\n"
              "    }\n    free(going);\n    free(with);\n    free(to);\n}\n\n");
}

// An event to another shard's entity goes there; a delivery that waits is
// kept with its event and what it touched, and runs when this shard owns all of it
static void gen_shard_events(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const client_codec *codec = &g->value_codec;
    for (int i = 0; i < prog->events.count; i++) {
        const decl *d = prog->events.items[i];
        if (d->is_local || !has_handlers(d, false)) continue;
        const char *name = type_cname(d);
        const bool queued = is_queued(prog, d);
        if (queued) {
            sb_printf(o, "TIDE_SHARD_FN void tide_shard_forward_%s(tide_world *w, const tide_entity target, const %s *value, const tide_player_id *sender)\n{\n"
                         "    tide_writer m = tide_shard_begin(w, TIDE_SHARD_EVENT, true);\n"
                         "    tide_write_u32(&m, target.index);\n    tide_write_u32(&m, target.generation);\n"
                         "    tide_write_u32(&m, %du);\n    tide_write_u32(&m, sender ? sender->id : 0u);\n"
                         "    tide_client_put%d(&m, value, 0);\n"
                         "    tide_shard_post(w, TIDE_SHARD_TO_OWNER, 0u, target, &m);\n}\n\n",
                      name, name, d->index, client_codec_id(codec, (type){TY_EVENT, (decl *)d}));
        }
        if (!shard_parks(prog, d)) continue;
        sb_printf(o, "TIDE_SHARD_FN void tide_shard_park_%s(tide_world *w, const tide_entity target, const %s *value, const tide_player_id *sender)\n{\n"
                     "    (void)value;\n"
                     "    tide_writer r = {.grows = true};\n    tide_write_u32(&r, 0u); // Its size, once written\n"
                     "    tide_write_u32(&r, tide_shard_code);\n    tide_write_u32(&r, target.index);\n    tide_write_u32(&r, target.generation);\n"
                     "    tide_write_u32(&r, (uint32_t)(tide_shard_since ? tide_shard_since : w->Time.tick));\n"
                     "    tide_write_u32(&r, sender ? sender->id : 0u);\n"
                     "    const uint32_t needs = tide_shard_touched(w->tide_shard_now, tide_shard_code, target, tide_shard_occurrence, NULL, 0u);\n"
                     "    tide_entity *touched = needs ? malloc(needs * sizeof *touched) : NULL;\n"
                     "    if (needs && !touched) tide_out_of_memory();\n"
                     "    tide_shard_touched(w->tide_shard_now, tide_shard_code, target, tide_shard_occurrence, touched, needs);\n"
                     "    tide_write_u32(&r, needs);\n"
                     "    for (uint32_t k = 0; k < needs; k++) {\n"
                     "        tide_write_u32(&r, touched[k].index);\n        tide_write_u32(&r, touched[k].generation);\n    }\n"
                     "    free(touched);\n", name, name);
        if (queued) sb_printf(o, "    tide_client_put%d(&r, value, 0);\n", client_codec_id(codec, (type){TY_EVENT, (decl *)d}));
        sb_put(o, "    if (r.overflow) tide_out_of_memory();\n    tide_shard_patch(&r, 0u, r.size);\n"
                  "    tide_shard_bytes_append(&w->tide_shard_state.waits, r.data, r.size, TIDE_IN_MATCH);\n    free(r.data);\n}\n\n");
    }

    // A destroy whose Destroyed handlers touched what this shard doesn't own
    // waits with them: the entity and its tree stay, marked tide_dying, which
    // nothing runs for, until this shard owns what they touched
    const int dying = prog->shard_dying->index;
    sb_put(o, "TIDE_HELPER void tide_shard_dying_one(tide_world *w, const tide_entity e, const bool dying)\n{\n"
              "    const tide_location loc = tide_entity_location(&w->entities, e);\n"
              "    const tide_dying v = {dying ? (tide_shard_since ? tide_shard_since : w->Time.tick) : 0};\n    (void)v;\n"
              "    switch (loc.archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    case %d: %s = dying ? 1u : 0u; %s; break;\n", a, presence_cell(g, "w", a, dying, "loc.row", true),
                  comp_put(g, "w", a, dying, "loc.row", "v"));
    }
    sb_put(o, "    default: break;\n    }\n}\n\n"
              "TIDE_HELPER void tide_shard_dying_tree(tide_world *w, const tide_entity root, const bool dying)\n{\n");
    if (prog->hierarchy[0]) {
        sb_put(o, "    tide_entity e = root;\n    uint64_t left = (uint64_t)w->entities.next_unused + 1u;\n"
                  "    while (!tide_entity_is_null(e) && left--) {\n"
                  "        tide_shard_dying_one(w, e, dying);\n"
                  "        tide_hierarchy_node n = tide_hierarchy_get(w, e);\n"
                  "        if (!tide_entity_is_null(n.first)) { e = n.first; continue; }\n"
                  "        while (!tide_entity_equal(e, root) && tide_entity_is_null(n.next)) {\n"
                  "            e = n.parent;\n            n = tide_hierarchy_get(w, e);\n        }\n"
                  "        e = tide_entity_equal(e, root) ? (tide_entity){0} : n.next;\n    }\n}\n\n");
    } else {
        sb_put(o, "    tide_shard_dying_one(w, root, dying);\n}\n\n");
    }
    sb_printf(o, "TIDE_SHARD_FN bool tide_shard_destroy_waits(tide_world *w, const tide_entity e)\n{\n"
                 "    if (!w->tide_shard_state.shard || !tide_entity_alive(&w->entities, e)\n"
                 "        || !tide_shard_skips(w->tide_shard_now, %du, e, 0u)) return false;\n"
                 "    const uint32_t needs = tide_shard_touched(w->tide_shard_now, %du, e, 0u, NULL, 0u);\n"
                 "    tide_entity *touched = malloc((needs + 1u) * sizeof *touched);\n"
                 "    if (!touched) tide_out_of_memory();\n"
                 "    tide_shard_touched(w->tide_shard_now, %du, e, 0u, touched, needs);\n"
                 "    touched[needs] = e; // ...and the entity itself, which may be lent meanwhile\n"
                 "    tide_writer r = {.grows = true};\n    tide_write_u32(&r, 0u);\n"
                 "    tide_write_u32(&r, %du);\n    tide_write_u32(&r, e.index);\n    tide_write_u32(&r, e.generation);\n"
                 "    tide_write_u32(&r, (uint32_t)(tide_shard_since ? tide_shard_since : w->Time.tick));\n    tide_write_u32(&r, 0u);\n"
                 "    tide_write_u32(&r, needs + 1u);\n"
                 "    for (uint32_t k = 0; k <= needs; k++) {\n"
                 "        tide_write_u32(&r, touched[k].index);\n        tide_write_u32(&r, touched[k].generation);\n    }\n"
                 "    free(touched);\n"
                 "    if (r.overflow) tide_out_of_memory();\n    tide_shard_patch(&r, 0u, r.size);\n"
                 "    tide_shard_bytes_append(&w->tide_shard_state.waits, r.data, r.size, TIDE_IN_MATCH);\n    free(r.data);\n"
                 "    tide_shard_dying_tree(w, e, true);\n"
                 "    return true;\n}\n\n",
              shard_code_event(prog, prog->destroyed), shard_code_event(prog, prog->destroyed),
              shard_code_event(prog, prog->destroyed), shard_code_event(prog, prog->destroyed));

    // The deliveries that waited and can run now, as the tick's changes start:
    // each in full, as when it was sent, knowing how long it waited (Shards.delay)
    sb_put(o, "TIDE_SHARD_FN void tide_shard_resume(tide_world *w)\n{\n"
              "    tide_shard_state *s = &w->tide_shard_state;\n"
              "    if (!s->shard || !tide_list_count(s->waits)) return;\n"
              "    uint32_t size;\n    bool copied;\n"
              "    const uint8_t *flat = tide_shard_bytes_flat(s->waits, &size, &copied);\n"
              "    uint8_t *all = malloc(size ? size : 1u);\n    if (!all) tide_out_of_memory();\n"
              "    memcpy(all, flat, size);\n    tide_shard_bytes_done(s->waits, flat, copied);\n"
              "    tide_writer keep = {.grows = true};\n"
              "    uint32_t *ready = NULL;\n    uint32_t count = 0;\n"
              "    for (uint32_t at = 0; at + 4u <= size;) {\n"
              "        tide_reader r = {all + at, size - at, 0, false};\n"
              "        const uint32_t record = tide_read_u32(&r);\n"
              "        if (record < 4u || record > size - at) break;\n"
              "        const uint32_t code = tide_read_u32(&r);\n"
              "        tide_read_u32(&r);\n        tide_read_u32(&r);\n        tide_read_u32(&r);\n        tide_read_u32(&r);\n"
              "        const uint32_t needs = tide_read_u32(&r);\n"
              "        bool go = code != TIDE_SHARD_LEND && !r.failed;\n"
              "        for (uint32_t k = 0; k < needs && go; k++) {\n"
              "            tide_entity e;\n            e.index = tide_read_u32(&r);\n            e.generation = tide_read_u32(&r);\n"
              "            go = !r.failed && tide_shard_owned(w, e);\n        }\n"
              "        if (go) {\n"
              "            uint32_t *grown = realloc(ready, (count + 1u) * sizeof *ready);\n"
              "            if (!grown) tide_out_of_memory();\n            ready = grown;\n            ready[count++] = at;\n"
              "        } else tide_write_bytes(&keep, all + at, record);\n"
              "        at += record;\n    }\n"
              "    if (keep.overflow) tide_out_of_memory();\n"
              "    if (count) tide_shard_bytes_set(&s->waits, keep.data, keep.size, TIDE_IN_MATCH);\n"
              "    free(keep.data);\n"
              "    for (uint32_t i = 0; i < count; i++) {\n"
              "        tide_reader r = {all + ready[i], size - ready[i], 0, false};\n"
              "        r.size = tide_read_u32(&r);\n"
              "        const uint32_t code = tide_read_u32(&r);\n"
              "        tide_entity target;\n        target.index = tide_read_u32(&r);\n        target.generation = tide_read_u32(&r);\n"
              "        const int32_t since = (int32_t)tide_read_u32(&r);\n"
              "        const tide_player_id sender = {tide_read_u32(&r)};\n        (void)sender;\n"
              "        const uint32_t needs = tide_read_u32(&r);\n"
              "        for (uint32_t k = 0; k < needs; k++) {\n            tide_read_u32(&r);\n            tide_read_u32(&r);\n        }\n"
              "        if (r.failed) continue;\n"
              "        tide_shard_since = since;\n        tide_shard_delay_now = w->Time.tick - since;\n"
              "        uint64_t budget = SIZE_MAX;\n        (void)budget;\n"
              "        switch (code) {\n");
    for (int i = 0; i < prog->events.count; i++) {
        const decl *d = prog->events.items[i];
        if (d->is_local || !has_handlers(d, false) || !shard_parks(prog, d)) continue;
        const char *name = type_cname(d);
        sb_printf(o, "        case %du: {\n", shard_code_event(prog, d));
        if (is_queued(prog, d)) {
            sb_printf(o, "            %s value;\n            memset(&value, 0, sizeof value);\n"
                         "            if (tide_client_get%d(&r, &value, TIDE_IN_MATCH, 0, &budget)) tide_dispatch_%s(w, target, &value%s);\n",
                      name, client_codec_id(codec, (type){TY_EVENT, (decl *)d}), name, d->is_command ? ", &sender" : "");
            if (decl_has_text(d)) sb_printf(o, "            tide_release_%s(&value, TIDE_IN_MATCH);\n", name);
        } else {
            sb_printf(o, "            tide_dispatch_%s(w, target, &(%s){0});\n", name, name);
        }
        sb_put(o, "            break;\n        }\n");
    }
    // A destroy that waited: its entity isn't dying any more, and goes as any destroyed does
    sb_printf(o, "        case %du:\n"
                 "            if (tide_entity_alive(&w->entities, target)) {\n"
                 "                tide_shard_dying_tree(w, target, false);\n                tide_destroy_now(w, target);\n            }\n"
                 "            break;\n",
              shard_code_event(prog, prog->destroyed));
    sb_put(o, "        default: break;\n        }\n"
              "        tide_shard_since = 0;\n        tide_shard_delay_now = 0;\n    }\n"
              "    free(ready);\n    free(all);\n}\n\n");
}
