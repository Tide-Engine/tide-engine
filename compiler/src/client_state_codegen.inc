// Per-recipient state uses values rather than storage offsets. In particular,
// an otherwise identical client's heap can have a different allocation history.
// This file is included by codegen.c after the ordinary storage helpers.

static bool client_singleton(const program *prog, const decl *d)
{
    return !d->is_local && d != prog->visibility_state && d != prog->hierarchy[0] && d != prog->task_slots[0]
        && d != prog->preparation_order;
}

// A C library's own fields ([Native]: its handles, and its bookkeeping of
// them) go with its Resident, which a projection hasn't: so they're 0 in one,
// as wherever an entity arrives (a shard zeroes them too), and the library
// makes its state again from the rest on the client. Their values, which may
// say what's hidden (a physics world's bodies), stay on the server. The
// value codecs keep them: local handlers' records and shards use those too.
static bool client_natives(const decl *d)
{
    for (int k = 0; k < d->fields.count; k++) if (d->fields.items[k].native) return true;
    return false;
}

// A copy of `from` named `name` with its [Native] fields 0, for a projection
static void gen_client_bare(gen *g, const decl *d, const char *name, const char *from, const char *indent)
{
    sb *o = &g->c;
    sb_printf(o, "%s%s %s = %s;\n", indent, type_cname(d), name, from);
    for (int k = 0; k < d->fields.count; k++) {
        const field *f = &d->fields.items[k];
        if (f->native) sb_printf(o, "%smemset(&%s.%s, 0, sizeof %s.%s);\n", indent, name, field_cname(f), name, field_cname(f));
    }
}

// A singleton `w->d` put by `writer` with its [Native] fields 0: copied to the
// heap, as a singleton can be bigger than a stack has room for
static void gen_client_put_singleton(gen *g, const client_codec *codec, const decl *d, const char *writer)
{
    sb *o = &g->c;
    const char *name = type_cname(d);
    if (!client_natives(d)) {
        sb_printf(o, "    tide_client_put%d(%s, &w->%s, 0);\n", client_codec_id(codec, (type){TY_SINGLETON, (decl *)d}), writer, name);
        return;
    }
    sb_printf(o, "    {\n        %s *bare = malloc(sizeof *bare);\n        if (!bare) tide_out_of_memory();\n"
                 "        *bare = w->%s;\n", name, name);
    for (int k = 0; k < d->fields.count; k++) {
        const field *f = &d->fields.items[k];
        if (f->native) sb_printf(o, "        memset(&bare->%s, 0, sizeof bare->%s);\n", field_cname(f), field_cname(f));
    }
    sb_printf(o, "        tide_client_put%d(%s, bare, 0);\n        free(bare);\n    }\n",
              client_codec_id(codec, (type){TY_SINGLETON, (decl *)d}), writer);
}

static void gen_client_hierarchy_codec(gen *g)
{
    const program *prog = g->prog;
    if (!prog->hierarchy[0]) return;
    sb *o = &g->c;
    sb_printf(o, "TIDE_HELPER bool tide_client_hierarchy_cut(const tide_world *w, tide_entity e, tide_player_id recipient)\n{\n"
                 "    const tide_entity parent = tide_hierarchy_get(w, e).parent;\n"
                 "    return !tide_entity_is_null(parent) && (!tide_visibility_entity(w, parent, recipient)\n"
                 "        || (tide_has_Transform(w, parent) && !tide_visibility_component(w, parent, %du, recipient)));\n}\n\n",
              prog->transform->index);
    sb_put(o, "TIDE_HELPER tide_entity tide_client_visible_sibling(const tide_world *w, tide_entity e, tide_player_id recipient, bool next)\n{\n"
              "    for (uint32_t left = w->entities.next_unused; !tide_entity_is_null(e) && left; left--) {\n"
              "        if (tide_visibility_entity(w, e, recipient) && !tide_client_hierarchy_cut(w, e, recipient)) return e;\n"
              "        const tide_hierarchy_node node = tide_hierarchy_get(w, e);\n"
              "        e = next ? node.next : node.previous;\n    }\n    return (tide_entity){0};\n}\n\n"
              "TIDE_HELPER tide_hierarchy_node tide_client_hierarchy_node(const tide_world *w, tide_entity e, tide_player_id recipient)\n{\n"
              "    tide_hierarchy_node node = tide_hierarchy_get(w, e);\n"
              "    node.first = tide_client_visible_sibling(w, node.first, recipient, true);\n"
              "    if (tide_entity_is_null(node.parent) || tide_client_hierarchy_cut(w, e, recipient)) {\n"
              "        node.parent = node.next = node.previous = (tide_entity){0};\n"
              "    } else {\n"
              "        node.next = tide_client_visible_sibling(w, node.next, recipient, true);\n"
              "        node.previous = tide_client_visible_sibling(w, node.previous, recipient, false);\n    }\n    return node;\n}\n\n");
    sb_printf(o, "TIDE_HELPER bool tide_client_hierarchy_needs_anchor(const tide_world *w, tide_entity root, tide_player_id recipient)\n{\n"
                 "    tide_entity at = root;\n    uint64_t left = (uint64_t)w->entities.next_unused * 2u + 1u;\n"
                 "    while (left--) {\n"
                 "        if (tide_visibility_component(w, at, %du, recipient)) return true;\n"
                 "        tide_hierarchy_node node = tide_client_hierarchy_node(w, at, recipient);\n"
                 "        if (!tide_entity_is_null(node.first)) { at = node.first; continue; }\n"
                 "        while (!tide_entity_equal(at, root) && tide_entity_is_null(node.next)) {\n"
                 "            if (!left || tide_entity_is_null(node.parent)) return false;\n"
                 "            left--;\n            at = node.parent;\n            node = tide_client_hierarchy_node(w, at, recipient);\n        }\n"
                 "        if (tide_entity_equal(at, root)) return false;\n        at = node.next;\n    }\n    return false;\n}\n\n",
              prog->transform->index);
    const int anchors = prog->visibility_state->fields.items[3].type.decl->index;
    sb_put(o, "TIDE_HELPER tide_float4x4 tide_client_parent_transform(const tide_world *w, tide_entity e, tide_quaternion *rotation)\n{\n"
              "    tide_float4x4 result = tide_identity_f4x4();\n"
              "    tide_quaternion q = tide_identity_q();\n"
              "    uint32_t count = 0;\n"
              "    for (tide_entity at = e; !tide_entity_is_null(at); at = tide_parent_of(w, at)) {\n"
              "        if (!tide_entity_alive(&w->entities, at) || count == w->entities.next_unused) {\n"
              "            *rotation = q;\n            return result;\n        }\n        count++;\n    }\n"
              "    if ((uint64_t)count * sizeof(tide_entity) > SIZE_MAX) { *rotation = q; return result; }\n"
              "    const uint64_t mark = tide_scratch_mark();\n"
              "    tide_entity *chain = tide_scratch_memory((size_t)count * sizeof *chain);\n"
              "    tide_entity at = e;\n"
              "    for (uint32_t i = 0; i < count; i++) { chain[i] = at; at = tide_parent_of(w, at); }\n"
              "    if (count) (void)tide_hierarchy_anchor(w, chain[count - 1u], &result, &q);\n"
              "    for (uint32_t i = count; i-- > 0;) {\n"
              "        Transform t;\n        if (!tide_load_Transform(w, chain[i], &t)) continue;\n"
              "        const tide_quaternion local = tide_normalize_q(t.localRotation);\n"
              "        result = tide_mul_f4x4(result, tide_trs_f4x4(t.localPosition, local, t.localScale));\n"
              "        q = tide_normalize_q(tide_mul_q(q, local));\n    }\n"
              "    tide_scratch_reset(mark);\n    *rotation = q;\n    return result;\n}\n\n");
    sb_printf(o, "TIDE_HELPER bool tide_client_hierarchy_anchor(const tide_world *w, tide_entity e, tide_player_id recipient, tide_visibility_anchor *anchor)\n{\n"
                 "    if (tide_client_hierarchy_cut(w, e, recipient)) {\n"
                 "        if (!tide_client_hierarchy_needs_anchor(w, e, recipient)) return false;\n"
                 "        anchor->matrix = tide_client_parent_transform(w, tide_hierarchy_get(w, e).parent, &anchor->rotation);\n"
                 "        return true;\n    }\n"
                 "    if (!tide_map%d_has(w->tide_visibility_state.anchors, e)) return false;\n"
                 "    *anchor = tide_map%d_get(w->tide_visibility_state.anchors, e);\n    return true;\n}\n\n", anchors, anchors);
    // The recipient's hierarchy, apart from its entities' records, so that a record, a row's hash and a repair of
    // one entity are its own alone: what a recipient may know of the links (a parent it may see, siblings it may see)
    // and an anchor where its parent is hidden depend on other entities. The entities that have links or an anchor
    // are those in the two maps, few in a world of chunks; the section lists the visible ones whose links or anchor
    // say anything, by their identities, so a server's and a recipient's are alike byte for byte.
    const int nodes = prog->hierarchy[0]->fields.items[0].type.decl->index;
    sb_put(o, "TIDE_HELPER int tide_client_entity_order(const void *a_, const void *b_)\n{\n"
              "    const tide_entity *a = a_, *b = b_;\n"
              "    if (a->birth != b->birth) return a->birth < b->birth ? -1 : 1;\n"
              "    return a->lineage < b->lineage ? -1 : a->lineage > b->lineage ? 1 : 0;\n}\n\n");
    sb_printf(o, "// The entities that have links or an anchor, in the order of their identities, each once: to free()\n"
                 "TIDE_HELPER tide_entity *tide_client_hierarchy_keys(const tide_world *w, uint32_t *count)\n{\n"
                 "    const tide_map nodes = w->tide_hierarchy.nodes, anchors = w->tide_visibility_state.anchors;\n"
                 "    const uint64_t most = (uint64_t)tide_map_entries(nodes) + (uint64_t)tide_map_entries(anchors);\n"
                 "    if (most > SIZE_MAX / sizeof(tide_entity) - 1u) tide_out_of_memory();\n"
                 "    tide_entity *keys = malloc(((size_t)most + 1u) * sizeof *keys);\n    if (!keys) tide_out_of_memory();\n"
                 "    uint32_t n = 0;\n"
                 "    for (int32_t i = 0; i < tide_map_entries(nodes); i++) {\n"
                 "        const tide_map%d_entry *entry = tide_map_entry(nodes, i, &tide_map%d_kind);\n"
                 "        if (tide_map_live(entry)) keys[n++] = entry->key;\n    }\n"
                 "    for (int32_t i = 0; i < tide_map_entries(anchors); i++) {\n"
                 "        const tide_map%d_entry *entry = tide_map_entry(anchors, i, &tide_map%d_kind);\n"
                 "        if (tide_map_live(entry)) keys[n++] = entry->key;\n    }\n"
                 "    qsort(keys, n, sizeof *keys, tide_client_entity_order);\n"
                 "    uint32_t kept = 0;\n"
                 "    for (uint32_t i = 0; i < n; i++) {\n"
                 "        if (kept && tide_entity_equal(keys[kept - 1u], keys[i])) continue;\n        keys[kept++] = keys[i];\n    }\n"
                 "    *count = kept;\n    return keys;\n}\n\n", nodes, nodes, anchors, anchors);
    sb_put(o, "TIDE_HELPER void tide_client_put_hierarchy_of(tide_writer *out, const tide_world *w, const tide_player_id recipient,\n"
              "                                             const tide_entity *keys, const uint32_t count)\n{\n"
              "    tide_writer items = {.grows = true};\n    uint32_t written = 0;\n"
              "    for (uint32_t i = 0; i < count; i++) {\n"
              "        const tide_entity e = keys[i];\n"
              "        if (!tide_entity_alive(&w->entities, e) || !tide_visibility_entity(w, e, recipient)) continue;\n"
              "        const tide_hierarchy_node node = tide_client_hierarchy_node(w, e, recipient);\n"
              "        tide_visibility_anchor anchor = {0};\n"
              "        const bool anchored = tide_client_hierarchy_anchor(w, e, recipient, &anchor);\n"
              "        if (!anchored && tide_entity_is_null(node.parent) && tide_entity_is_null(node.first)\n"
              "            && tide_entity_is_null(node.next) && tide_entity_is_null(node.previous)) continue;\n"
              "        tide_write_u64(&items, e.birth);\n        tide_write_u64(&items, e.lineage);\n"
              "        tide_write_bytes(&items, &node, sizeof node);\n"
              "        tide_write_u8(&items, anchored ? 1u : 0u);\n"
              "        if (anchored) tide_write_bytes(&items, &anchor, sizeof anchor);\n"
              "        written++;\n    }\n"
              "    if (items.overflow) tide_out_of_memory();\n"
              "    tide_write_varint(out, written);\n    if (items.size) tide_write_bytes(out, items.data, items.size);\n"
              "    free(items.data);\n}\n\n"
              "TIDE_HELPER void tide_client_put_hierarchy(tide_writer *out, const tide_world *w, const tide_player_id recipient)\n{\n"
              "    uint32_t count;\n    tide_entity *keys = tide_client_hierarchy_keys(w, &count);\n"
              "    tide_client_put_hierarchy_of(out, w, recipient, keys, count);\n    free(keys);\n}\n\n");
    // ...and a recipient's world taking it: its links and anchors are the section's and nothing else's
    sb_printf(o, "TIDE_HELPER bool tide_client_take_hierarchy(tide_world *next, tide_reader *in)\n{\n"
                 "    tide_map%d_clear(&next->tide_hierarchy.nodes, TIDE_IN_MATCH);\n"
                 "    tide_map%d_clear(&next->tide_visibility_state.anchors, TIDE_IN_MATCH);\n"
                 "    const uint32_t count = tide_read_varint(in);\n"
                 "    if (in->failed || count > (in->size - in->at) / (16u + sizeof(tide_hierarchy_node) + 1u)) return false;\n"
                 "    tide_entity last = {0};\n"
                 "    for (uint32_t i = 0; i < count; i++) {\n"
                 "        tide_entity e;\n        e.birth = tide_read_u64(in);\n        e.lineage = tide_read_u64(in);\n"
                 "        const uint8_t *bytes = tide_read_bytes(in, sizeof(tide_hierarchy_node));\n"
                 "        if (!bytes) return false;\n"
                 "        tide_hierarchy_node node = {0};\n        memcpy(&node, bytes, sizeof node);\n"
                 "        const uint8_t anchored = tide_read_u8(in);\n"
                 "        // Each once, in their order, of the world's entities; an anchor only for one with no parent\n"
                 "        if (in->failed || anchored > 1u || !tide_entity_alive(&next->entities, e)\n"
                 "            || (i && tide_client_entity_order(&last, &e) >= 0) || (anchored && !tide_entity_is_null(node.parent))) return false;\n"
                 "        last = e;\n"
                 "        tide_hierarchy_put(next, e, node);\n"
                 "        if (!anchored) continue;\n"
                 "        const uint8_t *value = tide_read_bytes(in, sizeof(tide_visibility_anchor));\n"
                 "        if (!value) return false;\n"
                 "        tide_visibility_anchor anchor = {0};\n        memcpy(&anchor, value, sizeof anchor);\n"
                 "        if (!tide_matrix_finite(anchor.matrix) || !tide_is_finite_f(anchor.rotation.value.x)\n"
                 "            || !tide_is_finite_f(anchor.rotation.value.y) || !tide_is_finite_f(anchor.rotation.value.z)\n"
                 "            || !tide_is_finite_f(anchor.rotation.value.w)) return false;\n"
                 "        tide_map%d_set(&next->tide_visibility_state.anchors, e, anchor, TIDE_IN_MATCH);\n    }\n"
                 "    return !in->failed;\n}\n\n", nodes, anchors, anchors);
    sb_put(o,
              "TIDE_HELPER size_t tide_client_entity_number(const tide_world *w, tide_entity e, const size_t *offsets)\n{\n"
              "    const tide_location loc = tide_entity_location(&w->entities, e);\n"
              "    if (loc.archetype == TIDE_ARCHETYPE_NONE) return SIZE_MAX;\n"
              "    return offsets[loc.archetype] + loc.row;\n}\n\n"
              "TIDE_HELPER bool tide_client_hierarchy_valid(const tide_world *w)\n{\n");
    sb_printf(o, "    size_t offsets[%d] = {0};\n    size_t total = 0;\n", prog->archetypes.count ? prog->archetypes.count : 1);
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    offsets[%d] = total;\n    total += w->%s.count;\n", a, arch_name(g, a));
    }
    sb_put(o, "    uint8_t *colors = total ? calloc(total, 1) : NULL;\n"
              "    if (total && !colors) tide_out_of_memory();\n    bool valid = true;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; valid && row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "        const tide_hierarchy_node node = tide_hierarchy_get(w, e);\n"
                  "        if (!tide_entity_is_null(node.parent) && !tide_entity_alive(&w->entities, node.parent)) { valid = false; break; }\n"
                  "        if (tide_entity_is_null(node.parent) && (!tide_entity_is_null(node.next) || !tide_entity_is_null(node.previous))) { valid = false; break; }\n"
                  "        if (!tide_entity_is_null(node.first)) {\n"
                  "            const tide_hierarchy_node child = tide_hierarchy_get(w, node.first);\n"
                  "            if (!tide_entity_alive(&w->entities, node.first) || !tide_entity_equal(child.parent, e) || !tide_entity_is_null(child.previous)) { valid = false; break; }\n        }\n"
                  "        if (!tide_entity_is_null(node.previous)) {\n"
                  "            const tide_hierarchy_node before = tide_hierarchy_get(w, node.previous);\n"
                  "            if (!tide_entity_alive(&w->entities, node.previous) || !tide_entity_equal(before.parent, node.parent) || !tide_entity_equal(before.next, e)) { valid = false; break; }\n"
                  "        } else if (!tide_entity_is_null(node.parent) && !tide_entity_equal(tide_hierarchy_get(w, node.parent).first, e)) { valid = false; break; }\n"
                  "        if (!tide_entity_is_null(node.next)) {\n"
                  "            const tide_hierarchy_node after = tide_hierarchy_get(w, node.next);\n"
                  "            if (!tide_entity_alive(&w->entities, node.next) || !tide_entity_equal(after.parent, node.parent) || !tide_entity_equal(after.previous, e)) { valid = false; break; }\n        }\n"
                  "        tide_entity at = e;\n"
                  "        while (!tide_entity_is_null(at)) {\n"
                  "            const size_t n = tide_client_entity_number(w, at, offsets);\n"
                  "            if (n == SIZE_MAX || n >= total || (colors[n] & 3u) == 1u) { valid = false; break; }\n"
                  "            if ((colors[n] & 3u) == 2u) break;\n"
                  "            colors[n] = (uint8_t)((colors[n] & 4u) | 1u);\n"
                  "            at = tide_hierarchy_get(w, at).parent;\n        }\n"
                  "        at = e;\n"
                  "        while (valid && !tide_entity_is_null(at)) {\n"
                  "            const size_t n = tide_client_entity_number(w, at, offsets);\n"
                  "            if ((colors[n] & 3u) != 1u) break;\n"
                  "            colors[n] = (uint8_t)((colors[n] & 4u) | 2u);\n"
                  "            at = tide_hierarchy_get(w, at).parent;\n        }\n"
                  "        at = node.first;\n"
                  "        while (valid && !tide_entity_is_null(at)) {\n"
                  "            const size_t n = tide_client_entity_number(w, at, offsets);\n"
                  "            if (n == SIZE_MAX || n >= total || (colors[n] & 4u)) { valid = false; break; }\n"
                  "            colors[n] |= 4u;\n            at = tide_hierarchy_get(w, at).next;\n        }\n    }\n");
    }
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; valid && row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        if (!tide_entity_is_null(tide_hierarchy_get(w, e).parent) && !(colors[offsets[%d] + row] & 4u)) valid = false;\n    }\n", a);
    }
    sb_put(o, "    free(colors);\n    return valid;\n}\n\n");
}

static void gen_client_input_filter(gen *g)
{
    const program *prog = g->prog;
    if (!prog->input) return;
    sb *o = &g->c;
    sb_put(o, "TIDE_HELPER void tide_client_input_mask(const tide_world *w, tide_player_id recipient, bool allowed[TIDE_GAME_PLAYERS + 1u])\n{\n"
              "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) allowed[slot] = true;\n"
              "    if (w->tide_visibility_state.client) return;\n"
              "    tide_text_use((tide_heaps *)&w->heap, NULL);\n"
              "    bool found[TIDE_GAME_PLAYERS + 1u] = {0};\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        if (arch_stores(g, a, prog->owner->index)) {
            sb_printf(o, "        const %s owner = tide_get_%s(w, e);\n", type_cname(prog->owner), type_cname(prog->owner));
            sb_put(o, "        const int32_t owner_index = tide_player_index(owner.player);\n"
                      "        const uint32_t slot = owner_index >= 0 && (uint32_t)owner_index < TIDE_GAME_PLAYERS ? (uint32_t)owner_index : TIDE_GAME_PLAYERS;\n");
        } else sb_put(o, "        const uint32_t slot = TIDE_GAME_PLAYERS;\n");
        sb_put(o, "        found[slot] = true;\n"
                  "        if (slot == (uint32_t)tide_player_index(recipient) || !allowed[slot]) continue;\n"
                  "        if (!tide_visibility_entity(w, e, recipient)) { allowed[slot] = false; continue; }\n");
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            if (!d->visibility_optional) continue;
            sb_printf(o, "        if (tide_has_%s(w, e) && !tide_visibility_component(w, e, %du, recipient)) allowed[slot] = false;\n",
                      type_cname(d), c);
        }
        sb_put(o, "    }\n");
    }
    sb_put(o, "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++)\n"
              "        allowed[slot] = slot == (uint32_t)tide_player_index(recipient) || (found[slot] && allowed[slot]);\n}\n\n"
              "static void tide_game_filter_client_input(const void *world, tide_player_id recipient, uint32_t slot, void *input)\n{\n"
              "    if (slot > TIDE_GAME_PLAYERS) return;\n"
              "    bool allowed[TIDE_GAME_PLAYERS + 1u];\n"
              "    tide_client_input_mask(world, recipient, allowed);\n"
              "    if (allowed[slot]) return;\n    *(" );
    sb_printf(o, "%s *)input = ", type_cname(prog->input));
    gen_value(g, o, prog->input, NULL, 0);
    sb_put(o, ";\n}\n\n");
    // Every recipient's at once (tide_game.client_input_masks): who may have each
    // slot's owner's entities, worked out once, then each recipient's bytes
    sb_printf(o, "static void tide_game_client_input_masks(const void *world, const tide_player_id *recipients, const uint32_t count,\n"
                 "                                         uint8_t *allowed, void *none)\n{\n"
                 "    const tide_world *w = world;\n    *(%s *)none = ", type_cname(prog->input));
    gen_value(g, o, prog->input, NULL, 0);
    sb_put(o, ";\n    const uint32_t slots = TIDE_GAME_PLAYERS + 1u;\n"
              "    if (w->tide_visibility_state.client) {\n        memset(allowed, 1, (size_t)count * slots);\n        return;\n    }\n"
              "    tide_text_use((tide_heaps *)&w->heap, NULL);\n"
              "    // On the heap: an audience of the most players a game takes is 8 KiB\n"
              "    Audience *audience = malloc((size_t)slots * sizeof *audience);\n    bool *found = malloc(slots);\n"
              "    if (!audience || !found) tide_out_of_memory();\n"
              "    for (uint32_t slot = 0; slot < slots; slot++) {\n        audience[slot] = TIDE_AUDIENCE_ALL;\n        found[slot] = false;\n    }\n"
              "    // Each row where it is, the directory through one view; and a slot nobody may have already is left alone (a\n"
              "    // world of chunks each a few players have withholds the server's slot after two of them)\n"
              "    static const Audience nobody = {0};\n"
              "    tide_visibility_walk walk = {0};\n    tide_map_view_none(&walk.groups);\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const bool owned = arch_stores(g, a, prog->owner->index);
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n        const tide_location loc = {%du, row};\n",
                  arch_cell(g, "w", a, 0, "tide_entity", "row", false), a);
        if (owned) {
            sb_printf(o, "        %s owner;\n        memset(&owner, 0, sizeof owner);\n        (void)tide_load_%s_at(w, loc, &owner);\n"
                         "        const int32_t owner_index = tide_player_index(owner.player);\n", type_cname(prog->owner), type_cname(prog->owner));
            sb_put(o, "        const uint32_t slot = owner_index >= 0 && (uint32_t)owner_index < TIDE_GAME_PLAYERS ? (uint32_t)owner_index : TIDE_GAME_PLAYERS;\n");
        } else sb_put(o, "        const uint32_t slot = TIDE_GAME_PLAYERS;\n");
        sb_printf(o, "        found[slot] = true;\n"
                     "        if (memcmp(&audience[slot], &nobody, sizeof nobody) == 0) %s;\n"
                     "        Visibility policy = {0};\n        bool has_policy = false;\n        int64_t groups[2];\n"
                     "        const Audience base = tide_visibility_base_at(w, e, loc, &policy, &has_policy, &walk, groups);\n"
                     "        audience[slot] = tide_audience_intersect(audience[slot], base);\n",
                  owned ? "continue" : "break");
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            if (!d->visibility_optional) continue;
            sb_printf(o, "        if (%s) audience[slot] = tide_audience_intersect(audience[slot], tide_visibility_denied_at(w, &policy, has_policy, base, %du));\n",
                      comp_sparse(g, c) ? presence_cell(g, "w", a, c, "row", false) : "true", (unsigned)c);
        }
        sb_put(o, "    }\n");
    }
    sb_put(o, "    for (uint32_t i = 0; i < count; i++) {\n"
              "        const int32_t own = tide_player_index(recipients[i]);\n"
              "        for (uint32_t slot = 0; slot < slots; slot++)\n"
              "            allowed[(size_t)i * slots + slot] = (own >= 0 && slot == (uint32_t)own)\n"
              "                || (found[slot] && tide_audience_contains(audience[slot], recipients[i]));\n    }\n"
              "    free(audience);\n    free(found);\n}\n\n");
    sb_printf(o, "static void tide_game_filter_client_inputs(const void *world, tide_player_id recipient, void *all_slots)\n{\n"
                 "    %s *inputs = all_slots;\n"
                 "    bool allowed[TIDE_GAME_PLAYERS + 1u];\n"
                 "    tide_client_input_mask(world, recipient, allowed);\n"
                 "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n"
                 "        if (!allowed[slot]) inputs[slot] = ", type_cname(prog->input));
    gen_value(g, o, prog->input, NULL, 0);
    sb_put(o, ";\n    }\n}\n\n");
}

// The components of archetype `a` a recipient can be denied while it has the
// entity (visibility.Set<T>), in component order: bit k of a mask is the k-th
static int client_deniable(const gen *g, const int a, int *out)
{
    int n = 0;
    for (int c = 0; c < g->prog->components.count; c++) {
        if (arch_stores(g, a, c) && g->prog->components.items[c]->visibility_optional) out[n++] = c;
    }
    return n;
}

// The projection hash of the singletons, as a tree (tide_client_hv<id>): a
// value's bytes are its codec's (tide_client_put<id>), but each list, map, set
// and grid in it is the 8 bytes of its own hash, of its bytes so made. So what
// a container hashes to is a function of its value alone, as the whole was,
// and it can be kept: a container at a place in the heap hashes to what it did
// while every heap page its value was read from (its blocks, its elements' and
// all it holds) still hashes to what it did then. Pages keep their hashes until
// they change (tide/page.h), and the server has just hashed its world, so a
// container nothing changed costs a comparison a page. What's kept, by heap
// place and codec, is for the eight worlds last hashed, whichever thread hashes
// them; it's only ever a shortcut to the hash the value has, never another one.
// Values read from outside the match's heaps (the pack's) aren't kept.
//
// A world has a heap for each owner, and an owner's has runs (tide/heap.h), so
// a page is named by its place in the heap its block's handle says: the heap's
// id and run (a `where` without its world, as tide_heaps_of takes them) above
// 32 bits, the page's first place below (tide_client_place). A value's blocks
// can be in several heaps (a row's grid in a run's heap, its text in another's).
static void gen_client_hash_helpers(gen *g)
{
    sb *o = &g->c;
    // For measuring (a build that says TIDE_CLIENT_COUNTS): what a server's checks read again, and why, printed at
    // the program's end. Nothing of it is in any other build
    sb_put(o, "#ifdef TIDE_CLIENT_COUNTS\n#include \"tide/time.h\"\n"
              "static TIDE_THREAD_LOCAL bool tide_client_counting; // A server's check, not one worked out twice\n"
              "static uint64_t tide_client_counts[20];\n"
              "#define TIDE_CLIENT_COUNT_BY(i, n) do { if (tide_client_counting) __atomic_add_fetch(&tide_client_counts[i], (uint64_t)(n), __ATOMIC_RELAXED); } while (0)\n"
              "#define TIDE_CLIENT_COUNT(i) TIDE_CLIENT_COUNT_BY(i, 1u)\n"
              "// ...and how long each part of a check took, in nanoseconds: from the mark before\n"
              "static uint64_t tide_client_times[10];\n"
              "static TIDE_THREAD_LOCAL uint64_t tide_client_mark_at;\n"
              "#define tide_client_now tide_time_now_ns\n"
              "#define TIDE_CLIENT_TIMED(i, what) do { const uint64_t from_ = tide_client_counting ? tide_client_now() : 0u; what; if (tide_client_counting) __atomic_add_fetch(&tide_client_times[i], tide_client_now() - from_, __ATOMIC_RELAXED); } while (0)\n"
              "#define TIDE_CLIENT_MARK(i) do { if (tide_client_counting) { const uint64_t now_ = tide_client_now();\\\n"
              "    if ((i) >= 0) __atomic_add_fetch(&tide_client_times[(i) < 0 ? 0 : (i)], now_ - tide_client_mark_at, __ATOMIC_RELAXED); tide_client_mark_at = now_; } } while (0)\n"
              "static void tide_client_counts_print(void);\n"
              "__attribute__((constructor)) static void tide_client_counts_at_exit(void)\n{\n    atexit(tide_client_counts_print);\n}\n"
              "static void tide_client_counts_print(void)\n{\n"
              "    static const char *const names[20] = {\"calls\", \"pieces looked at\", \"read: nothing kept\", \"read: a column's run\",\n"
              "        \"read: a heap page\", \"read: a slot's page\", \"rows in pieces read\", \"rows read from the heap again\",\n"
              "        \"...for their pages alone\", \"grids encoded\", \"containers encoded\", \"rows fresh\", \"blocks hashed again\",\n"
              "        \"...and found the same\", \"bytes of blocks hashed\", \"heap pages looked at\",\n"
              "        \"slot pages looked at\", \"columns' runs hashed\", \"their bytes\", \"\"};\n"
              "    for (uint32_t i = 0; i < 20u; i++) if (names[i][0]) fprintf(stderr, \"tide client counts: %s %llu\\n\", names[i], (unsigned long long)tide_client_counts[i]);\n"
              "    static const char *const parts[10] = {\"singletons\", \"rows (the rest)\", \"groups\", \"audiences\", \"sums\", \"pieces checked\",\n"
              "        \"rows of pieces read\", \"pieces kept\", \"...of which pages by their blocks\", \"...slots by their snaps\"};\n"
              "    for (uint32_t i = 0; i < 10u; i++) if (parts[i][0]) fprintf(stderr, \"tide client counts: %s %.3f ms\\n\", parts[i], (double)tide_client_times[i] / 1e6);\n"
              "}\n"
              "#else\n#define TIDE_CLIENT_TIMED(i, what) what\n#define TIDE_CLIENT_COUNT(i) ((void)0)\n#define TIDE_CLIENT_COUNT_BY(i, n) ((void)0)\n#define TIDE_CLIENT_MARK(i) ((void)0)\n#endif\n\n");
    sb_put(o, "// What a value was read from, by its key (tide_client_key): a block smaller than a page, or a page whole; the\n"
              "// hash its page had (0: not looked at yet), and a block's own bytes' hash. A block's page that hashes as it did\n"
              "// says the block is as it was; one that doesn't has the block's bytes hashed again (another entity's block in the\n"
              "// page changed, not this one), and where they're as they were, the page's hash now is kept in its place\n"
              "typedef struct tide_client_item { uint64_t key, page, block; } tide_client_item;\n"
              "typedef struct tide_client_memo_entry {\n"
              "    uint64_t at; // The container's handle; 0: an empty slot\n"
              "    uint64_t hash;\n    tide_client_item *items; // Every block and page its value was read from, and their hashes then\n"
              "    uint32_t type, used, count, room;\n    bool broken; // Read from a piece of a bigger page: it never holds\n} tide_client_memo_entry;\n"
              "struct tide_client_kept; // What the world's rows came to, 64 rows of a table chunk at a time (client_spans_codegen.inc)\n"
              "TIDE_HELPER void tide_client_kept_free(struct tide_client_kept *k);\n"
              "typedef struct tide_client_memo {\n    const void *world;\n    tide_client_memo_entry *slots;\n    struct tide_client_kept *rows; // What its rows came to\n"
              "    uint32_t room, count, call, kept; // kept: entries this call used\n"
              "    uint32_t swept; // Entries after the last sweep of those whose pages changed\n"
              "    bool busy; // A thread is hashing with it\n} tide_client_memo;\n"
              "typedef struct tide_client_hasher {\n    tide_writer out;\n    tide_heaps *heaps; // The world's\n    tide_client_memo *memo;\n"
              "    tide_client_item *reach; // The blocks and pages read so far (their hashes where a memo's entry had them)\n"
              "    uint32_t reached, reach_room;\n    uint32_t floor; // Where the container being read started its pages\n    bool poisoned; // Something was read from outside the heaps\n} tide_client_hasher;\n\n"
              "// A page's place: its heap (the id and run of a `where`) above 32 bits, its first place in that heap below\n"
              "TIDE_HELPER uint64_t tide_client_place(const uint32_t heap, const uint32_t first)\n{\n"
              "    return (uint64_t)heap << 32 | first;\n}\n\n"
              "// The heap a handle's block is in, as a page's place names it (tide_heaps_of's id and run)\n"
              "TIDE_HELPER uint32_t tide_client_heap_of(const tide_handle at)\n{\n"
              "    return tide_handle_where(at) >> TIDE_WHERE_HEAP_SHIFT;\n}\n\n"
              "// A world's heap with that id and run, where there is one: none is made here (the world is only read)\n"
              "TIDE_HELPER tide_heap *tide_client_heap(tide_heaps *heaps, const uint32_t heap)\n{\n"
              "    tide_heap *all = __atomic_load_n(&heaps->all, __ATOMIC_ACQUIRE);\n"
              "    if (!all) return NULL;\n"
              "    tide_heap *h = all + tide_heap_slots[heap & (TIDE_HEAP_IDS - 1u)];\n"
              "    const uint32_t run = heap >> TIDE_HEAP_ID_BITS & (TIDE_HEAP_RUNS - 1u);\n"
              "    if (!run) return h;\n"
              "    tide_heap *runs = __atomic_load_n(&h->runs, __ATOMIC_ACQUIRE);\n"
              "    return runs ? runs + (run - 1u) : NULL;\n}\n\n"
              "");
    sb_put(o, "// The page at a place (tide_client_place), and its heap, where a page starts there; else NULL\n"
              "TIDE_HELPER const tide_page *tide_client_place_page(tide_heaps *heaps, const uint64_t place, tide_heap **heap)\n{\n"
              "    tide_heap *h = tide_client_heap(heaps, (uint32_t)(place >> 32));\n"
              "    const uint32_t first = (uint32_t)place;\n"
              "    if (!h || first >= h->pages) return NULL;\n"
              "    const tide_page *page = h->page[first];\n"
              "    if (!page || page->first != first) return NULL;\n"
              "    *heap = h;\n    return page;\n}\n\n"
              "// A heap page's hash, as tide_heap_block_hash gives it: the one the page keeps where it's of the bytes in use (the\n"
              "// world's hash has just asked for every page's), read here as cached_codegen.inc's keys read it; else asked for\n"
              "TIDE_HELPER uint64_t tide_client_page_hash(tide_heap *h, const tide_page *page)\n{\n"
              "    const uint64_t start = (uint64_t)page->first << TIDE_HEAP_PAGE_SHIFT;\n"
              "    const uint64_t left = h->used > start ? h->used - start : 0u;\n"
              "    const uint32_t bytes = left < page->size ? (uint32_t)left : page->size;\n"
              "    if (__atomic_load_n(&page->hashed, __ATOMIC_ACQUIRE) == bytes) return __atomic_load_n(&page->hash, __ATOMIC_RELAXED);\n"
              "    return tide_heap_block_hash(h, start);\n}\n\n"
              "// ...and its size and hash\n"
              "TIDE_HELPER bool tide_client_place_hash(tide_heaps *heaps, const uint64_t place, uint32_t *size, uint64_t *hash)\n{\n"
              "    tide_heap *h = NULL;\n"
              "    const tide_page *page = tide_client_place_page(heaps, place, &h);\n"
              "    if (!page) return false;\n"
              "    *size = page->size;\n"
              "    *hash = tide_client_page_hash(h, page);\n"
              "    return true;\n}\n\n");
    sb_put(o, "// What a block is kept by: its page's place (tide_client_place) above 15 bits, and below them, for a block smaller\n"
              "// than a page in a page of one place, its size class plus one (4 bits) and its offset in the page in 16 bytes (10\n"
              "// bits); 0 there for a page whole (a block of a page or more, a list's chunk). A place is below 49 bits (a heap's\n"
              "// id and run are 17), so a key is never all ones. A block's size is its header's, or `bytes` where its reader\n"
              "// says (a grid's chunk, whose header is cells: tide/grid.h): what the reader reads of it, from its start\n"
              "#define TIDE_CLIENT_KEY_SHIFT 15u\n"
              "TIDE_HELPER uint64_t tide_client_key_place(const uint64_t key)\n{\n    return key >> TIDE_CLIENT_KEY_SHIFT;\n}\n\n"
              "TIDE_HELPER uint64_t tide_client_key(const tide_heap *h, const uint32_t heap, const uint64_t block, const uint32_t bytes)\n{\n"
              "    const tide_page *p = h->page[block >> TIDE_HEAP_PAGE_SHIFT];\n"
              "    const uint64_t key = tide_client_place(heap, p->first) << TIDE_CLIENT_KEY_SHIFT;\n"
              "    const uint64_t start = (uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT;\n"
              "    if (p->size > (1u << TIDE_HEAP_PAGE_SHIFT) || (block & 15u) || block < start) return key;\n"
              "    uint32_t size_class = 0;\n"
              "    if (!bytes) size_class = tide_block_class(tide_heap_block(h, block));\n"
              "    else while (size_class + 4u < TIDE_HEAP_PAGE_SHIFT && (16u << size_class) < bytes) size_class++;\n"
              "    const uint32_t at = (uint32_t)(block - start);\n"
              "    if (size_class + 4u >= TIDE_HEAP_PAGE_SHIFT || at + (16u << size_class) > p->size) return key;\n"
              "    return key | (uint64_t)(size_class + 1u) << 10 | at >> 4;\n}\n\n"
              "// A block's bytes hashed, in its page (tide_client_place_page), where the key is a block's and the page has room for\n"
              "// it as far as its heap has handed out; else false\n"
              "TIDE_HELPER bool tide_client_page_block(const tide_heap *h, const tide_page *page, const uint64_t key, uint64_t *hash)\n{\n"
              "    const uint32_t tag = (uint32_t)(key >> 10) & 15u;\n"
              "    if (!tag) return false;\n"
              "    const uint32_t at = (uint32_t)(key & 1023u) << 4, bytes = 16u << (tag - 1u);\n"
              "    const uint64_t start = (uint64_t)page->first << TIDE_HEAP_PAGE_SHIFT;\n"
              "    if (at + bytes > page->size || h->used < start + at + bytes) return false;\n"
              "    *hash = tide_hash_wide((const uint8_t *)tide_page_data(page) + at, bytes);\n    TIDE_CLIENT_COUNT_BY(14, bytes);\n"
              "    return true;\n}\n\n");
    sb_put(o, "// An item's hashes as its block and page are now; false where its page doesn't start at its place\n"
              "TIDE_HELPER bool tide_client_item_fill(tide_heaps *heaps, tide_client_item *it)\n{\n"
              "    tide_heap *h = NULL;\n"
              "    const tide_page *page = tide_client_place_page(heaps, tide_client_key_place(it->key), &h);\n"
              "    if (!page) return false;\n"
              "    it->page = tide_client_page_hash(h, page);\n"
              "    it->block = 0;\n"
              "    return !(it->key >> 10 & 15u) || tide_client_page_block(h, page, it->key, &it->block);\n}\n\n"
              "// Whether an item's block (or page) is as it was, its page being `page` (of heap `h`) and hashing to `now`: the\n"
              "// page's hash, else its block's bytes', the page's hash now then kept as the item's\n"
              "TIDE_HELPER bool tide_client_item_same_in(const tide_heap *h, const tide_page *page, const uint64_t now, tide_client_item *it)\n{\n"
              "    if (now == it->page) return true;\n"
              "    if (!(it->key >> 10 & 15u)) return false;\n"
              "    TIDE_CLIENT_COUNT(12);\n"
              "    uint64_t block;\n"
              "    if (!tide_client_page_block(h, page, it->key, &block) || block != it->block) return false;\n"
              "    TIDE_CLIENT_COUNT(13);\n"
              "    it->page = now;\n    return true;\n}\n\n"
              "TIDE_HELPER bool tide_client_item_same(tide_heaps *heaps, tide_client_item *it)\n{\n"
              "    tide_heap *h = NULL;\n"
              "    const tide_page *page = tide_client_place_page(heaps, tide_client_key_place(it->key), &h);\n"
              "    return page && tide_client_item_same_in(h, page, tide_client_page_hash(h, page), it);\n}\n\n");
    sb_put(o, ""
              "// The process's, one for each world hashed (a server's, or a player's machine's, whose worlds share one): a\n"
              "// world is hashed on one thread at a time, and a memo is one thread's while it hashes (busy). Made as they're\n"
              "// asked for; one nobody asked for in a while goes to another world. Globals, so they're never lost with a thread\n"
              "#define TIDE_CLIENT_MEMOS_MOST 1024u\n"
              "static tide_client_memo **tide_client_memos;\n"
              "static uint32_t tide_client_memo_count, tide_client_memo_calls, tide_client_memo_lock;\n\n");
    sb_put(o, "TIDE_HELPER void tide_client_memo_clear(tide_client_memo *m)\n{\n"
              "    for (uint32_t i = 0; i < m->room; i++) free(m->slots[i].items);\n"
              "    if (m->rows) {\n        tide_client_kept_free(m->rows);\n        free(m->rows);\n    }\n"
              "    free(m->slots);\n    memset(m, 0, sizeof *m);\n}\n\n");
    sb_put(o, "// The world's memo; or one nobody asked for in a while (four times as long as it would take every memo to be\n"
              "// asked for once), emptied, for it; or a new one. NULL while every one of the most there are is busy\n"
              "TIDE_HELPER tide_client_memo *tide_client_memo_for(const void *world)\n{\n"
              "    while (__atomic_exchange_n(&tide_client_memo_lock, 1u, __ATOMIC_ACQUIRE)) {\n    }\n"
              "    tide_client_memo *pick = NULL, *idle = NULL;\n"
              "    for (uint32_t i = 0; i < tide_client_memo_count && !pick; i++) {\n"
              "        tide_client_memo *m = tide_client_memos[i];\n"
              "        if (m->world == world && !m->busy) pick = m;\n"
              "        else if (!m->busy && m->world != world && (!idle || m->call < idle->call)) idle = m;\n    }\n"
              "    const bool stale = idle && tide_client_memo_calls - idle->call > 4u * tide_client_memo_count;\n"
              "    if (!pick && idle && (stale || tide_client_memo_count >= TIDE_CLIENT_MEMOS_MOST)) {\n"
              "        tide_client_memo_clear(idle);\n        idle->world = world;\n        pick = idle;\n"
              "    } else if (!pick && tide_client_memo_count < TIDE_CLIENT_MEMOS_MOST) {\n"
              "        tide_client_memo **grown = realloc(tide_client_memos, ((size_t)tide_client_memo_count + 1u) * sizeof *grown);\n"
              "        tide_client_memo *made = calloc(1, sizeof *made);\n"
              "        if (!grown || !made) tide_out_of_memory();\n"
              "        tide_client_memos = grown;\n        tide_client_memos[tide_client_memo_count++] = made;\n"
              "        made->world = world;\n        pick = made;\n    }\n"
              "    if (pick) {\n        pick->busy = true;\n        pick->call = ++tide_client_memo_calls;\n        pick->kept = 0;\n    }\n"
              "    __atomic_store_n(&tide_client_memo_lock, 0u, __ATOMIC_RELEASE);\n"
              "    return pick;\n}\n\n"
              "// A world that goes takes its memo with it (tide_world_free): a world made where it was is another, which what\n"
              "// was kept of the first would be wrong about\n"
              "TIDE_HELPER void tide_client_memo_forget(const void *world)\n{\n"
              "    while (__atomic_exchange_n(&tide_client_memo_lock, 1u, __ATOMIC_ACQUIRE)) {\n    }\n"
              "    for (uint32_t i = 0; i < tide_client_memo_count; i++) {\n"
              "        if (tide_client_memos[i]->world == world && !tide_client_memos[i]->busy) tide_client_memo_clear(tide_client_memos[i]);\n    }\n"
              "    __atomic_store_n(&tide_client_memo_lock, 0u, __ATOMIC_RELEASE);\n}\n\n"
              "// Once its entries have doubled since the last time, those whose pages changed go (what they were kept for is\n"
              "// gone or another value); then the memo is free again\n"
              "TIDE_HELPER void tide_client_memo_done(tide_client_memo *m, tide_heaps *heaps);\n\n");
    sb_put(o, "TIDE_HELPER uint32_t tide_client_memo_slot(const tide_client_memo *m, const uint64_t at, const uint32_t type)\n{\n"
              "    uint32_t i = (uint32_t)tide_client_mix(at * 0x9e3779b97f4a7c15ull + type) & (m->room - 1u);\n"
              "    while (m->slots[i].at && (m->slots[i].at != at || m->slots[i].type != type)) i = (i + 1u) & (m->room - 1u);\n"
              "    return i;\n}\n\n");
    sb_put(o, "// Whether every block and page an entry's value was read from is as it was\n"
              "TIDE_HELPER bool tide_client_memo_holds(tide_client_memo_entry *e, tide_heaps *heaps)\n{\n"
              "    if (e->broken) return false;\n"
              "    for (uint32_t i = 0; i < e->count; i++) {\n"
              "        if (!tide_client_item_same(heaps, &e->items[i])) return false;\n    }\n"
              "    return true;\n}\n\n");
    sb_put(o, "// Into `room` slots: every entry, or (with heaps) those whose pages are as they were\n");
    sb_put(o, "TIDE_HELPER void tide_client_memo_rehash(tide_client_memo *m, const uint32_t room, tide_heaps *heaps)\n{\n"
              "    tide_client_memo_entry *was = m->slots;\n    const uint32_t had = m->room;\n"
              "    m->slots = calloc(room, sizeof *m->slots);\n    if (!m->slots) tide_out_of_memory();\n"
              "    m->room = room;\n    m->count = 0;\n"
              "    for (uint32_t i = 0; i < had; i++) {\n"
              "        if (!was[i].at) continue;\n"
              "        if (heaps && !tide_client_memo_holds(&was[i], heaps)) {\n            free(was[i].items);\n            continue;\n        }\n"
              "        m->slots[tide_client_memo_slot(m, was[i].at, was[i].type)] = was[i];\n        m->count++;\n    }\n"
              "    free(was);\n}\n\n");
    sb_put(o, "TIDE_HELPER void tide_client_memo_done(tide_client_memo *m, tide_heaps *heaps)\n{\n"
              "    if (!m) return;\n"
              "    // (Not by what this call used: a table chunk kept whole uses none of its rows' entries, which still hold)\n"
              "    if (heaps && m->count > 64u && m->count > m->swept * 2u) {\n"
              "        uint32_t live = 0;\n"
              "        for (uint32_t i = 0; i < m->room; i++) live += m->slots[i].at && tide_client_memo_holds(&m->slots[i], heaps);\n"
              "        uint32_t room = 128u;\n        while (room < live * 2u) room *= 2u;\n"
              "        tide_client_memo_rehash(m, room, heaps);\n"
              "        m->swept = m->count;\n    }\n"
              "    while (__atomic_exchange_n(&tide_client_memo_lock, 1u, __ATOMIC_ACQUIRE)) {\n    }\n"
              "    m->busy = false;\n    __atomic_store_n(&tide_client_memo_lock, 0u, __ATOMIC_RELEASE);\n}\n\n");
    sb_put(o, "TIDE_HELPER void tide_client_reach_item(tide_client_hasher *hs, const tide_client_item it)\n{\n"
              "    if (hs->reached > hs->floor && hs->reach[hs->reached - 1u].key == it.key) return;\n"
              "    if (hs->reached == hs->reach_room) {\n"
              "        hs->reach_room = hs->reach_room ? hs->reach_room * 2u : 256u;\n"
              "        hs->reach = realloc(hs->reach, (size_t)hs->reach_room * sizeof *hs->reach);\n"
              "        if (!hs->reach) tide_out_of_memory();\n    }\n"
              "    hs->reach[hs->reached++] = it;\n}\n\n");
    sb_put(o, "// The block read (tide_client_key: its page whole where it's a page or more); false (and nothing kept) for one\n"
              "// outside the match's heaps. By the heap it's in and its offset there, as a list's chunks and a grid's are kept\n"
              "TIDE_HELPER bool tide_client_reach_block(tide_client_hasher *hs, const uint32_t heap, const uint64_t block, const uint32_t bytes)\n{\n"
              "    const tide_heap *h = tide_client_heap(hs->heaps, heap);\n"
              "    const uint64_t place = block >> TIDE_HEAP_PAGE_SHIFT;\n"
              "    if (!block || !h || place >= h->pages) {\n        hs->poisoned = true;\n        return false;\n    }\n"
              "    tide_client_reach_item(hs, (tide_client_item){tide_client_key(h, heap, block, bytes), 0, 0});\n    return true;\n}\n\n"
              "TIDE_HELPER bool tide_client_reach(tide_client_hasher *hs, const tide_handle at)\n{\n"
              "    if (!at) return false;\n"
              "    if (tide_handle_world(at) != TIDE_IN_MATCH) {\n        hs->poisoned = true;\n        return false;\n    }\n"
              "    return tide_client_reach_block(hs, tide_client_heap_of(at), tide_handle_offset(at), 0u);\n}\n\n");
    sb_put(o, "// A list's block and its chunks', which are in its block's heap (tide/list.h)\n");
    sb_put(o, "TIDE_HELPER void tide_client_reach_list(tide_client_hasher *hs, const tide_list l)\n{\n"
              "    if (!tide_client_reach(hs, l.at)) return;\n"
              "    const uint32_t heap = tide_client_heap_of(l.at);\n"
              "    const tide_block *b = tide_heap_block(tide_client_heap(hs->heaps, heap), tide_handle_offset(l.at));\n"
              "    if (b->b == TIDE_LIST_ASSET || !(b->b & TIDE_LIST_CHUNKED)) return;\n"
              "    const uint32_t chunks = b->b & ~TIDE_LIST_CHUNKED;\n"
              "    const uint64_t *at = (const uint64_t *)(const void *)(b + 1);\n"
              "    for (uint32_t i = 0; i < chunks; i++) (void)tide_client_reach_block(hs, heap, at[i], 1u << TIDE_HEAP_PAGE_SHIFT);\n}\n\n");
    sb_put(o, "// The container at `at`'s hash, when every block and page it was read from is as it was\n");
    sb_put(o, "TIDE_HELPER bool tide_client_memo_get(tide_client_hasher *hs, const uint64_t at, const uint32_t type, uint64_t *hash)\n{\n"
              "#ifdef TIDE_CLIENT_MEMO_OFF // For tests: every hash made again\n    (void)hs; (void)at; (void)type; (void)hash;\n    return false;\n#endif\n"
              "    tide_client_memo *m = hs->memo;\n    if (!m || !m->room) return false;\n"
              "    tide_client_memo_entry *e = &m->slots[tide_client_memo_slot(m, at, type)];\n    if (!e->at) return false;\n"
              "    if (!tide_client_memo_holds(e, hs->heaps)) return false;\n"
              "    if (e->used != m->call) {\n        e->used = m->call;\n        m->kept++;\n    }\n"
              "    for (uint32_t i = 0; i < e->count; i++) tide_client_reach_item(hs, e->items[i]);\n"
              "    *hash = e->hash;\n    return true;\n}\n\n");
    sb_put(o, "static int tide_client_item_order(const void *a, const void *b)\n{\n"
              "    const uint64_t x = ((const tide_client_item *)a)->key, y = ((const tide_client_item *)b)->key;\n"
              "    return (x > y) - (x < y);\n}\n\n");
    sb_put(o, "// Keeps the container at `at`'s hash, with the blocks and pages read since `from`, which are left once each, their\n"
              "// hashes worked out (so a row that reads the container has them)\n");
    sb_put(o, "TIDE_HELPER void tide_client_memo_put(tide_client_hasher *hs, const uint64_t at, const uint32_t type, const uint64_t hash, const uint32_t from)\n{\n"
              "    tide_client_item *r = hs->reach + from;\n    const uint32_t n = hs->reached - from;\n"
              "    if (n > 1u) qsort(r, n, sizeof *r, tide_client_item_order);\n"
              "    uint32_t unique = 0;\n"
              "    for (uint32_t i = 0; i < n; i++) {\n"
              "        if (unique && r[unique - 1u].key == r[i].key) {\n"
              "            if (!r[unique - 1u].page) r[unique - 1u] = r[i]; // (One a memo's entry had: its hashes)\n"
              "            continue;\n        }\n"
              "        r[unique++] = r[i];\n    }\n"
              "    hs->reached = from + unique;\n"
              "    bool broken = false;\n"
              "    for (uint32_t i = 0; i < unique; i++) {\n"
              "        // (A piece of a bigger page, whose own place isn't the page's: the entry never holds)\n"
              "        if (!r[i].page && !tide_client_item_fill(hs->heaps, &r[i])) {\n            r[i].page = r[i].block = 0;\n            broken = true;\n        }\n    }\n"
              "    tide_client_memo *m = hs->memo;\n    if (!m) return;\n"
              "    if ((m->count + 1u) * 2u > m->room) tide_client_memo_rehash(m, m->room ? m->room * 2u : 128u, NULL);\n"
              "    tide_client_memo_entry *e = &m->slots[tide_client_memo_slot(m, at, type)];\n"
              "    if (!e->at) {\n        e->at = at;\n        e->type = type;\n        m->count++;\n    }\n"
              "    if (e->room < unique) {\n"
              "        free(e->items);\n        e->items = malloc((size_t)unique * sizeof *e->items);\n"
              "        if (!e->items) tide_out_of_memory();\n        e->room = unique;\n    }\n"
              "    if (unique) memcpy(e->items, r, (size_t)unique * sizeof *r);\n"
              "    e->count = unique;\n    e->hash = hash;\n    e->broken = broken;\n"
              "    if (e->used != m->call) {\n        e->used = m->call;\n        m->kept++;\n    }\n}\n\n");
    sb_put(o, "// A container's hash: from the memo when nothing it was read from changed, else made from\n");
    sb_put(o, "// its bytes, written from `mark` on, which make way for it\n");
    sb_put(o, "TIDE_HELPER void tide_client_hv_end(tide_client_hasher *hs, const uint64_t at, const uint32_t type, const bool keep,\n"
              "                                    const uint32_t mark, const uint32_t from, const uint32_t floor, const bool poisoned)\n{\n"
              "    if (hs->out.overflow) tide_out_of_memory();\n"
              "    const uint64_t hash = tide_hash(hs->out.data + mark, hs->out.size - mark);\n"
              "    hs->out.size = mark;\n"
              "    if (keep && !hs->poisoned) tide_client_memo_put(hs, at, type, hash, from);\n"
              "    hs->poisoned |= poisoned;\n    hs->floor = floor;\n"
              "    tide_client_write(&hs->out, &hash, sizeof hash);\n}\n\n");
}

// A value of `t` at `place` (a pointer expression) into the hasher `hs`: by its tree hash if it holds
// anything in the heap, else its codec's bytes
static void gen_client_hv_call(gen *g, const client_codec *codec, const type t, const char *place, const char *indent)
{
    sb *o = &g->c;
    const int id = client_codec_id(codec, t);
    if (client_codec_heapy(g->prog, t, 0)) sb_printf(o, "%stide_client_hv%d(hs, %s, depth + 1u);\n", indent, id, place);
    else sb_printf(o, "%stide_client_put%d(&hs->out, %s, depth + 1u);\n", indent, id, place);
}

// What a container's tide_client_hv starts with: its hash kept, when it's big enough to keep
static void gen_client_hv_begin(gen *g, const int id, const char *handle, const char *keep)
{
    sb_printf(&g->c, "    const bool keep = %s && tide_handle_world(%s) == TIDE_IN_MATCH && (%s);\n"
                     "    uint64_t kept;\n"
                     "    if (keep && tide_client_memo_get(hs, %s, %du, &kept)) {\n"
                     "        tide_client_write(&hs->out, &kept, sizeof kept);\n        return;\n    }\n"
                     "    if (keep) TIDE_CLIENT_COUNT(10);\n"
                     "    const uint32_t mark = hs->out.size, from = hs->reached, floor = hs->floor;\n"
                     "    const bool poisoned = hs->poisoned;\n    hs->poisoned = false;\n    hs->floor = from;\n",
              handle, handle, keep, handle, id);
}

static void gen_client_hv(gen *g, const client_codec *codec, const int id)
{
    const type t = codec->items[id];
    const char *ct = elem_ctype(t);
    sb *o = &g->c;
    sb_printf(o, "TIDE_HELPER void tide_client_hv%d(tide_client_hasher *hs, const %s *v, uint32_t depth)\n{\n", id, ct);
    sb_put(o, "    if (depth > 256u) { fputs(\"tide: visibility value nesting exceeds 256; split recursive data across entities\\n\", stderr); abort(); }\n");
    if (t.kind == TY_STRING) {
        sb_put(o, "    (void)tide_client_reach(hs, v->at);\n");
        sb_printf(o, "    tide_client_put%d(&hs->out, v, depth);\n", id);
    } else if (t.kind == TY_RECORD && t.decl->array_of) {
        sb_printf(o, "    for (uint32_t i = 0; i < %du; i++) {\n", t.decl->array_length);
        gen_client_hv_call(g, codec, (type){TY_RECORD, t.decl->array_of}, "&v->at[i]", "        ");
        sb_put(o, "    }\n");
    } else if (t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_EVENT || t.kind == TY_RECORD) {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            // [Native] fields aren't a recipient's (client_natives): a projection has them 0
            if (f->typed || recipient_private_field(g->prog, t.decl, f)
                || ((t.kind == TY_SINGLETON || t.kind == TY_COMPONENT) && f->native)) continue;
            char place[256];
            snprintf(place, sizeof place, "&v->%s", field_cname(f));
            gen_client_hv_call(g, codec, f->type, place, "    ");
        }
    } else if (t.kind == TY_ARRAY) {
        sb_printf(o, "    for (uint32_t i = 0; i < %du; i++) {\n", t.decl->array_length);
        gen_client_hv_call(g, codec, t.decl->fields.items[0].type, "&v->at[i]", "        ");
        sb_put(o, "    }\n");
    } else if (t.kind == TY_LIST) {
        const type et = t.decl->fields.items[0].type;
        sb_put(o, "    const uint32_t count = (uint32_t)tide_list_count(*v);\n");
        char keep[128];
        snprintf(keep, sizeof keep, "(uint64_t)count * sizeof(%s) >= 256u", elem_ctype(et));
        gen_client_hv_begin(g, id, "v->at", keep);
        sb_put(o, "    tide_client_reach_list(hs, *v);\n"
                  "    tide_write_varint(&hs->out, count);\n"
                  "    tide_list_cache cache = {0};\n");
        if (client_codec_raw(g->prog, et)) {
            sb_printf(o, "    for (uint32_t i = 0; i < count;) {\n"
                         "        const %s *element = tide_list_cached_at(&cache, *v, (int32_t)i, sizeof *element);\n"
                         "        uint32_t n = cache.at == v->at && cache.at && i - cache.first < cache.count ? cache.first + cache.count - i : 1u;\n"
                         "        if (n > count - i) n = count - i;\n"
                         "        if (n > (1u << 30) / sizeof *element) n = (uint32_t)((1u << 30) / sizeof *element);\n"
                         "        tide_client_write(&hs->out, element, (uint32_t)(n * sizeof *element));\n"
                         "        i += n;\n    }\n", elem_ctype(et));
        } else {
            char place[256];
            snprintf(place, sizeof place, "(const %s *)tide_list_cached_at(&cache, *v, (int32_t)i, sizeof(%s))", elem_ctype(et), elem_ctype(et));
            sb_put(o, "    for (uint32_t i = 0; i < count; i++) {\n");
            gen_client_hv_call(g, codec, et, place, "        ");
            sb_put(o, "    }\n");
        }
        sb_printf(o, "    tide_client_hv_end(hs, v->at, %du, keep, mark, from, floor, poisoned);\n", id);
    } else if (t.kind == TY_MAP || t.kind == TY_SET) {
        const int map = t.decl->index;
        char keep[128];
        snprintf(keep, sizeof keep, "(uint64_t)v->live * sizeof(tide_map%d_entry) >= 256u", map);
        // Its count and its last order are the map's own, not its pages': outside what's kept by its pages (a
        // player's machine makes a map again with its live entries alone, and the same pages can then go with
        // another last order)
        sb_put(o, "    tide_write_varint(&hs->out, v->live);\n"
                  "    tide_write_u64(&hs->out, (uint64_t)v->last_order);\n");
        gen_client_hv_begin(g, id, "v->entries.at", keep);
        sb_put(o, "    tide_client_reach_list(hs, v->entries);\n    tide_client_reach_list(hs, v->index);\n"
                  "    int64_t order = 0;\n    int32_t next = 0;\n    int32_t i;\n");
        sb_printf(o, "    while ((i = tide_map_next(*v, &tide_map%d_kind, &order, &next)) >= 0) {\n", map);
        sb_printf(o, "        const tide_map%d_entry *entry = tide_map_entry(*v, i, &tide_map%d_kind);\n", map, map);
        sb_put(o, "        tide_write_u64(&hs->out, (uint64_t)order);\n");
        gen_client_hv_call(g, codec, t.decl->fields.items[0].type, "&entry->key", "        ");
        if (t.kind == TY_MAP) gen_client_hv_call(g, codec, t.decl->fields.items[1].type, "&entry->value", "        ");
        sb_put(o, "    }\n");
        sb_printf(o, "    tide_client_hv_end(hs, v->entries.at, %du, keep, mark, from, floor, poisoned);\n", id);
    } else if (t.kind == TY_GRID) {
        const type cell = t.decl->fields.items[0].type;
        sb_put(o, "    const tide_grid_dir *dir = tide_grid_dir_of(*v);\n");
        gen_client_hv_begin(g, id, "v->at", "dir && dir->records");
        sb_put(o, "    if (keep) TIDE_CLIENT_COUNT(9);\n");
        sb_put(o, "    (void)tide_client_reach(hs, v->at);\n"
                  "    int32_t size[3] = {0};\n    tide_grid_size(*v, size);\n"
                  "    tide_write_bytes(&hs->out, size, sizeof size);\n"
                  "    tide_write_varint(&hs->out, dir ? dir->records : 0u);\n"
                  "    if (dir) {\n"
                  "        if ((uint64_t)dir->records * sizeof(tide_grid_record) > SIZE_MAX) tide_out_of_memory();\n"
                  "        tide_grid_record *records = malloc((size_t)dir->records * sizeof *records);\n"
                  "        if (!records && dir->records) tide_out_of_memory();\n"
                  "        if (dir->records) memcpy(records, tide_grid_records(dir), (size_t)dir->records * sizeof *records);\n"
                  "        if (dir->records > 1u) qsort(records, dir->records, sizeof *records, tide_client_grid_compare);\n"
                  "        for (uint32_t i = 0; i < dir->records; i++) {\n"
                  "            // (Its chunks are in its directory's heap: tide/grid.h. A chunk's header is cells, so its size is said)\n");
        sb_printf(o, "            if (tide_handle_world(v->at) == TIDE_IN_MATCH)\n"
                     "                (void)tide_client_reach_block(hs, tide_client_heap_of(v->at), records[i].block, tide_grid_chunk_bytes(&%s));\n",
                  grid_shape_name(t.decl));
        sb_put(o, "            tide_write_bytes(&hs->out, records[i].chunk, sizeof records[i].chunk);\n");
        sb_printf(o, "            const uint8_t *cells = tide_grid_chunk_at(tide_heap_read(tide_handle_where(v->at)), records[i].block);\n"
                     "            for (uint32_t k = 0; k < tide_grid_chunk_bytes(&%s) / sizeof(%s); k++) {\n",
                  grid_shape_name(t.decl), elem_ctype(cell));
        char place[256];
        snprintf(place, sizeof place, "(const %s *)(const void *)(cells + (size_t)k * sizeof(%s))", elem_ctype(cell), elem_ctype(cell));
        gen_client_hv_call(g, codec, cell, place, "                ");
        sb_put(o, "            }\n        }\n        free(records);\n    }\n");
        sb_printf(o, "    tide_client_hv_end(hs, v->at, %du, keep, mark, from, floor, poisoned);\n", id);
    }
    sb_put(o, "}\n\n");
}

// The tree hashes of the codec's types that hold anything in the heap
static void gen_client_hvs(gen *g, const client_codec *codec)
{
    sb *o = &g->c;
    gen_client_hash_helpers(g);
    for (int i = 0; i < codec->count; i++) {
        if (!client_codec_heapy(g->prog, codec->items[i], 0)) continue;
        sb_printf(o, "TIDE_HELPER void tide_client_hv%d(tide_client_hasher *, const %s *, uint32_t);\n", i, elem_ctype(codec->items[i]));
    }
    sb_put(o, "\n");
    for (int i = 0; i < codec->count; i++) {
        if (client_codec_heapy(g->prog, codec->items[i], 0)) gen_client_hv(g, codec, i);
    }
}

// Whether the game's projection hashes go by trees (tide_client_hv<id>): singletons holding anything in the heap
// go in as their tree hashes, kept while nothing they were read from changed, and so do entities' components, so
// a chunk's grid is hashed again only when one of its pages changed.
static bool client_hash_heapy(const gen *g)
{
    const program *prog = g->prog;
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (client_singleton(prog, d) && !(d->builtin && str_eq_c(d->name, "tide_command_state"))
            && client_codec_heapy(prog, (type){TY_SINGLETON, (decl *)d}, 0)) return true;
    }
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        for (int c = 0; c < prog->components.count; c++) {
            if (arch_stores(g, a, c) && client_codec_heapy(prog, (type){TY_COMPONENT, prog->components.items[c]}, 0)) return true;
        }
    }
    return false;
}

// An entity's hash is the sum of a term for what says which entity it is and a term for each component it has (0
// for one it hasn't), so what it hashes to for a recipient denied some of its components is that sum less their
// terms, and a term can be kept while its component's lanes are as they were (client_spans_codegen.inc). Generated
// for archetype `a`: tide_client_entity_id<a>, tide_client_term<a>_<c> for each component it stores (a component
// holding anything in the heap, in a game whose hashes go by trees, takes the hasher and reaches its pages), and
// tide_client_entity_hash<a>, the sum, with the terms of the components that can be denied in `terms`.
// (`loc` is where the entity is, which a walk over rows knows: nothing of it is looked up by its handle but its snaps)
static void gen_client_entity_parts(gen *g, const client_codec *codec, const int a, const bool heapy, const int *deniable, const int n)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_printf(o, "TIDE_HELPER uint64_t tide_client_entity_id%d(const tide_world *w, const tide_entity e, const tide_location loc, tide_writer *out)\n{\n"
                 "    (void)loc;\n    out->size = 0;\n    tide_write_u32(out, %du);\n"
                 "    tide_write_u64(out, e.birth);\n    tide_write_u64(out, e.lineage);\n"
                 "    tide_write_u32(out, tide_entity_snaps(&w->entities, e));\n", a, (unsigned)a);
    if (arch_scene_column(g, a)) {
        sb_printf(o, "    {\n        const tide_entity scene = *%s;\n", arch_cell(g, "w", a, 1, "tide_entity", "loc.row", false));
        sb_put(o, "        tide_write_bytes(out, &scene, sizeof scene);\n    }\n");
    }
    sb_put(o, "    if (out->overflow) tide_out_of_memory();\n    return tide_hash(out->data, out->size);\n}\n\n");
    for (int c = 0; c < prog->components.count; c++) {
        if (!arch_stores(g, a, c)) continue;
        const decl *d = prog->components.items[c];
        const bool tree = heapy && client_codec_heapy(prog, (type){TY_COMPONENT, (decl *)d}, 0);
        sb_printf(o, "TIDE_HELPER uint64_t tide_client_term%d_%d(const tide_world *w, const tide_entity e, const tide_location loc, const bool present, %s)\n{\n"
                     "    (void)e;\n    if (!present) return 0;\n",
                  a, c, tree ? "tide_client_hasher *hs" : "tide_writer *out");
        if (tree) sb_put(o, "    tide_writer *out = &hs->out;\n");
        sb_printf(o, "    out->size = 0;\n    %s value;\n    memset(&value, 0, sizeof value);\n    (void)tide_load_%s_at(w, loc, &value);\n",
                  type_cname(d), type_cname(d));
        for (int f = 0; f < d->fields.count; f++) { // [Native] fields aren't a recipient's (client_natives)
            const field *fd = &d->fields.items[f];
            if (fd->native) sb_printf(o, "    memset(&value.%s, 0, sizeof value.%s);\n", field_cname(fd), field_cname(fd));
        }
        if (tree) sb_printf(o, "    tide_client_hv%d(hs, &value, 1u);\n", client_codec_id(codec, (type){TY_COMPONENT, (decl *)d}));
        else sb_printf(o, "    tide_client_put%d(out, &value, 0);\n", client_codec_id(codec, (type){TY_COMPONENT, (decl *)d}));
        sb_printf(o, "    if (out->overflow) tide_out_of_memory();\n"
                     "    return tide_client_mix(tide_hash(out->data, out->size) + 0x9e3779b97f4a7c15ull * %du);\n}\n\n", (unsigned)c + 1u);
    }
    sb_printf(o, "TIDE_HELPER uint64_t tide_client_entity_hash%d(const tide_world *w, const tide_entity e, const tide_location loc,\n"
                 "                                              const uint64_t mask, %s, uint64_t *terms)\n{\n",
              a, heapy ? "tide_client_hasher *hs" : "tide_writer *out");
    if (heapy) sb_put(o, "    tide_writer *out = &hs->out;\n    hs->reached = 0;\n    hs->floor = 0;\n    hs->poisoned = false;\n");
    sb_printf(o, "    (void)mask;\n    (void)terms;\n    uint64_t sum = tide_client_entity_id%d(w, e, loc, out);\n", a);
    for (int c = 0; c < prog->components.count; c++) {
        if (!arch_stores(g, a, c)) continue;
        const decl *d = prog->components.items[c];
        const bool tree = heapy && client_codec_heapy(prog, (type){TY_COMPONENT, (decl *)d}, 0);
        int k = -1;
        for (int i = 0; i < n; i++) if (deniable[i] == c) k = i;
        char present[256];
        if (k >= 0) snprintf(present, sizeof present, "((mask >> %d) & 1u) != 0", k);
        else if (comp_sparse(g, c)) snprintf(present, sizeof present, "%s != 0", presence_cell(g, "w", a, c, "loc.row", false));
        else snprintf(present, sizeof present, "true");
        sb_printf(o, "    {\n        const uint64_t term = tide_client_term%d_%d(w, e, loc, %s, %s);\n        sum += term;\n",
                  a, c, present, tree ? "hs" : "out");
        if (k >= 0) sb_printf(o, "        terms[%d] = term;\n", k);
        sb_put(o, "    }\n");
    }
    sb_put(o, "    return sum;\n}\n\n");
}

// Every recipient's projection hash at once (tide_game.client_hashes), and one
// recipient's as the same sum. What's alike for every recipient is worked out
// once: the singletons, each input slot, and each entity, both its audiences
// and what it hashes to with everything it has. A recipient's hash adds up the
// entities it may have, as their hashes are with the components it may have,
// and its inputs, as permitted. The sum doesn't depend on where rows are, which
// a client's tables, missing hidden rows, needn't agree with the server's on.
// Games with transform hierarchies hash their projection's bytes, as each
// recipient's cut links differ.
static void gen_client_hashes(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    client_codec codec = g->value_codec;
    int *deniable = malloc(((size_t)prog->components.count + 1u) * sizeof *deniable);
    if (!deniable) abort();
    sb_put(o, "TIDE_HELPER uint64_t tide_client_mix(uint64_t h)\n{\n"
              "    h ^= h >> 33;\n    h *= 0xff51afd7ed558ccdull;\n    h ^= h >> 33;\n    h *= 0xc4ceb9fe1a85ec53ull;\n"
              "    h ^= h >> 33;\n    return h;\n}\n\n");
    const bool heapy = client_hash_heapy(g);
    if (heapy) gen_client_hvs(g, &codec);
    if (heapy) gen_client_span_helpers(g);
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        if (n > 64) {
            diag_error(prog->components.items[deniable[0]]->at, "an entity has more than 64 components whose visibility is set apart");
            break;
        }
        gen_client_entity_parts(g, &codec, a, heapy, deniable, n);
    }

    // (`again`: nothing kept is used or left: every row read, for TIDE_CLIENT_SPANS_CHECK)
    sb_put(o, "static void tide_client_hashes_work(const void *world, const tide_player_id *recipients, const uint32_t count, uint64_t *out,\n"
              "                                    const bool again)\n{\n"
              "    (void)again;\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heaps *)&w->heap, NULL);\n"
              "    const bool client = w->tide_visibility_state.client != 0;\n"
              "    tide_writer scratch = {.grows = true};\n"
              "    // The singletons, alike for every recipient but the commands, which are each one's own\n");
    if (heapy) {
        // A player's machine hashes a different world each tick, the one it confirmed, of the worlds it keeps: they
        // share their pages, and a kept hash goes by its pages alone, so they share one memo, the player's
        sb_put(o, "    const void *memo_of = client ? (const void *)(uintptr_t)(uint32_t)w->tide_visibility_state.client : world;\n"
                  "    tide_client_hasher hasher = {.out = {.grows = true}, .heaps = (tide_heaps *)(uintptr_t)&w->heap, .memo = again ? NULL : tide_client_memo_for(memo_of)};\n"
                  "    tide_client_hasher *hs = &hasher;\n    const uint32_t depth = 0;\n"
                  "#ifdef TIDE_CLIENT_COUNTS\n    tide_client_counting = !client && !again;\n    TIDE_CLIENT_COUNT(0);\n    TIDE_CLIENT_MARK(-1);\n#endif\n"
                  "    tide_write_u32(&hs->out, 0x33564954u);\n    tide_write_u64(&hs->out, w->tide_identity_clock);\n    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++) tide_write_u64(&hs->out, w->tide_lifecycle_occurrences[i]);\n    tide_write_u64(&hs->out, w->tide_main_occurrence);\n");
    } else {
        sb_put(o, "    tide_write_u32(&scratch, 0x32564954u);\n    tide_write_u64(&scratch, w->tide_identity_clock);\n    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++) tide_write_u64(&scratch, w->tide_lifecycle_occurrences[i]);\n    tide_write_u64(&scratch, w->tide_main_occurrence);\n");
    }
    const decl *commands = NULL;
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        if (d->builtin && str_eq_c(d->name, "tide_command_state")) {
            commands = d;
            continue;
        }
        char place[256];
        snprintf(place, sizeof place, "&w->%s", type_cname(d));
        // (depth + 1u, as the codecs' fields are: 1). A tree hash leaves [Native] fields out (gen_client_hv)
        if (heapy) gen_client_hv_call(g, &codec, (type){TY_SINGLETON, (decl *)d}, place, "    ");
        else gen_client_put_singleton(g, &codec, d, "&scratch");
    }
    if (heapy) {
        sb_put(o, "    if (hs->out.overflow) tide_out_of_memory();\n"
                  "    const uint64_t shared = tide_hash(hs->out.data, hs->out.size);\n    TIDE_CLIENT_MARK(0);\n");
    } else {
        sb_put(o, "    if (scratch.overflow) tide_out_of_memory();\n"
                  "    const uint64_t shared = tide_hash(scratch.data, scratch.size);\n");
    }
    sb_put(o, ""
              "    for (uint32_t i = 0; i < count; i++) out[i] = tide_client_mix(shared + 0x9e3779b97f4a7c15ull * ((uint64_t)recipients[i].id + 1u));\n");
    if (commands) {
        sb_put(o, "    for (uint32_t i = 0; i < count; i++) {\n        const tide_player_id recipient = recipients[i];\n"
                  "        scratch.size = 0;\n");
        sb_printf(o, "        %s permitted_commands = {0};\n", type_cname(commands));
        for (int k = 0; k < commands->fields.count; k++) {
            const field *f = &commands->fields.items[k];
            uint32_t player = 0;
            int start = 0;
            while (start < f->name.len && (f->name.ptr[start] < '0' || f->name.ptr[start] > '9')) start++;
            for (; start < f->name.len && f->name.ptr[start] >= '0' && f->name.ptr[start] <= '9'; start++)
                player = player * 10u + (uint32_t)(f->name.ptr[start] - '0');
            sb_printf(o, "        if (recipient.id == %uu) permitted_commands.%s = w->%s.%s;\n", player + 1u, field_cname(f),
                      type_cname(commands), field_cname(f));
        }
        sb_printf(o, "        tide_client_put%d(&scratch, &permitted_commands, 0);\n", client_codec_id(&codec, (type){TY_SINGLETON, (decl *)commands}));
        sb_put(o, "        if (scratch.overflow) tide_out_of_memory();\n"
                  "        out[i] = tide_client_mix(out[i] ^ tide_hash(scratch.data, scratch.size));\n    }\n");
    }
    if (prog->input) {
        sb_put(o, "    // Each input slot's owner's entities, and who may have all of them: who's sent that slot's input.\n"
                  "    // On the heap: an audience of the most players a game takes is 8 KiB\n"
                  "    Audience *slot_audience = malloc((TIDE_GAME_PLAYERS + 1u) * sizeof *slot_audience);\n"
                  "    bool *slot_found = malloc(TIDE_GAME_PLAYERS + 1u);\n"
                  "    uint64_t *slot_hash = malloc((TIDE_GAME_PLAYERS + 1u) * sizeof *slot_hash);\n"
                  "    if (!slot_audience || !slot_found || !slot_hash) tide_out_of_memory();\n"
                  "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n"
                  "        slot_audience[slot] = TIDE_AUDIENCE_ALL;\n        slot_found[slot] = false;\n    }\n");
    }
    if (heapy) {
        sb_put(o, "    // What the rows came to last time, with the world's memo: its own, or nothing kept where it has none\n"
                  "    struct tide_client_kept *own = NULL, *kept = again || !hs->memo ? NULL : hs->memo->rows;\n"
                  "    if (!kept && !again && hs->memo) kept = hs->memo->rows = calloc(1, sizeof *kept);\n"
                  "    if (!kept) kept = own = calloc(1, sizeof *kept);\n"
                  "    if (!kept) tide_out_of_memory();\n");
        int *parts = malloc(((size_t)prog->components.count + 1u) * sizeof *parts);
        if (!parts) abort();
        gen_client_hash_rows(g, deniable, parts);
        free(parts);
    } else sb_put(o, "    // Each entity once: who may have it, who may have each component that can be denied, and its hash with all it has\n"
              "    tide_visibility_walk walk = {0};\n    tide_map_view_none(&walk.groups);\n");
    for (int a = 0; a < prog->archetypes.count && !heapy; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        sb_printf(o, "    const uint32_t rows%d = w->%s.count;\n", a, arch_name(g, a));
        sb_printf(o, "    Audience *audience%d = rows%d ? malloc((size_t)rows%d * sizeof *audience%d) : NULL;\n"
                     "    uint64_t *has%d = rows%d ? malloc((size_t)rows%d * 2u * sizeof *has%d) : NULL;\n"
                     "    if (rows%d && (!audience%d || !has%d)) tide_out_of_memory();\n", a, a, a, a, a, a, a, a, a, a, a);
        if (n) {
            sb_printf(o, "    Audience *denied%d = rows%d ? malloc((size_t)rows%d * %du * sizeof *denied%d) : NULL;\n"
                         "    uint64_t *terms%d = rows%d ? malloc((size_t)rows%d * %du * sizeof *terms%d) : NULL;\n"
                         "    if (rows%d && (!denied%d || !terms%d)) tide_out_of_memory();\n", a, a, a, (unsigned)n, a, a, a, a, (unsigned)n, a,
                      a, a, a);
        }
        sb_printf(o, "    for (uint32_t row = 0; row < rows%d; row++) {\n", a);
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        const tide_location loc = {%du, row};\n        uint64_t mask = 0;\n", a);
        for (int k = 0; k < n; k++) {
            if (comp_sparse(g, deniable[k])) sb_printf(o, "        if (%s) mask |= 1ull << %d;\n", presence_cell(g, "w", a, deniable[k], "row", false), k);
            else sb_printf(o, "        mask |= 1ull << %d;\n", k);
        }
        char terms[64];
        if (n) snprintf(terms, sizeof terms, "&terms%d[row * %du]", a, (unsigned)n);
        else snprintf(terms, sizeof terms, "NULL");
        sb_printf(o, "        // What it has, and its hash's sum with all of it\n"
                     "        has%d[row * 2u] = mask;\n        has%d[row * 2u + 1u] = tide_client_entity_hash%d(w, e, loc, mask, %s, %s);\n",
                  a, a, a, heapy ? "hs" : "&scratch", terms);
        sb_printf(o, "        Visibility policy = {0};\n        bool has_policy = false;\n        int64_t groups[2];\n"
                     "        audience%d[row] = !client ? tide_visibility_base_at(w, e, loc, &policy, &has_policy, &walk, groups)\n"
                     "            : tide_visibility_client_hidden(w, e) ? (Audience){0} : TIDE_AUDIENCE_ALL;\n", a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "        denied%d[row * %du + %du] = client || !((mask >> %d) & 1u) ? TIDE_AUDIENCE_ALL\n"
                         "            : tide_visibility_denied_at(w, &policy, has_policy, audience%d[row], %du);\n",
                      a, (unsigned)n, (unsigned)k, k, a, (unsigned)deniable[k]);
        }
        if (prog->input) {
            if (arch_stores(g, a, prog->owner->index)) {
                sb_printf(o, "        const int32_t owner_index = tide_player_index(tide_get_%s(w, e).player);\n", type_cname(prog->owner));
                sb_put(o, "        const uint32_t slot = owner_index >= 0 && (uint32_t)owner_index < TIDE_GAME_PLAYERS ? (uint32_t)owner_index : TIDE_GAME_PLAYERS;\n");
            } else sb_put(o, "        const uint32_t slot = TIDE_GAME_PLAYERS;\n");
            sb_printf(o, "        slot_found[slot] = true;\n        slot_audience[slot] = tide_audience_intersect(slot_audience[slot], audience%d[row]);\n", a);
            for (int k = 0; k < n; k++) {
                sb_printf(o, "        slot_audience[slot] = tide_audience_intersect(slot_audience[slot], denied%d[row * %du + %du]);\n", a, (unsigned)n, (unsigned)k);
            }
        }
        sb_put(o, "    }\n");
    }
    if (prog->input) {
        const int input = client_codec_id(&codec, (type){TY_INPUT, prog->input});
        sb_put(o, "    // Each slot's input as any recipient but its own player gets it (its previous input the current one), and none\n"
                  "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n        scratch.size = 0;\n");
        sb_printf(o, "        tide_client_put%d(&scratch, &w->inputs[slot], 0);\n        tide_client_put%d(&scratch, &w->inputs[slot], 0);\n", input, input);
        sb_put(o, "        if (scratch.overflow) tide_out_of_memory();\n        slot_hash[slot] = tide_hash(scratch.data, scratch.size);\n    }\n"
                  "    uint64_t none_hash;\n    {\n        scratch.size = 0;\n");
        sb_printf(o, "        const %s none = ", type_cname(prog->input));
        gen_value(g, o, prog->input, NULL, 0);
        sb_printf(o, ";\n        tide_client_put%d(&scratch, &none, 0);\n        tide_client_put%d(&scratch, &none, 0);\n", input, input);
        sb_put(o, "        if (scratch.overflow) tide_out_of_memory();\n        none_hash = tide_hash(scratch.data, scratch.size);\n    }\n");
    }
    // Each row goes to the recipients who may have it, found from its audience's bits: a world of chunks a few
    // players each have costs its rows, not its rows for every player
    sb_put(o, "    uint64_t *sums = calloc(count ? count : 1u, 2u * sizeof *sums);\n"
              "    int32_t *asked = malloc(TIDE_GAME_PLAYERS * sizeof *asked); // Where each player is in `recipients`, its first place\n"
              "    if (!sums || !asked) tide_out_of_memory();\n"
              "    for (uint32_t p = 0; p < TIDE_GAME_PLAYERS; p++) asked[p] = -1;\n"
              "    for (uint32_t i = 0; i < count; i++) {\n        const int32_t p = tide_player_index(recipients[i]);\n"
              "        if (p >= 0 && (uint32_t)p < TIDE_GAME_PLAYERS && asked[p] < 0) asked[p] = (int32_t)i;\n    }\n");
    // Where rows are kept 64 at a time, what a piece's rows add to each player's sum is kept with it while none of
    // them was read again or had its audience worked out again (client_spans_codegen.inc): a piece adds what it
    // kept, for the players asked
    if (heapy) {
        sb_put(o, "    if (!kept->route_sums) {\n"
                  "        kept->route_sums = calloc(TIDE_GAME_PLAYERS, 2u * sizeof *kept->route_sums);\n"
                  "        kept->route_touched = malloc(TIDE_GAME_PLAYERS * sizeof *kept->route_touched);\n"
                  "        if (!kept->route_sums || !kept->route_touched) tide_out_of_memory();\n    }\n");
    }
    for (int a = 0; a < prog->archetypes.count && heapy; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        const char *table = arch_name(g, a);
        sb_printf(o, "    for (uint32_t chunk = 0; chunk < w->%s.chunks; chunk++) {\n"
                     "        const uint32_t first = chunk << (%s)->shift, here = tide_table_rows(&w->%s, (%s)->shift, chunk);\n"
                     "        for (uint32_t at = 0; at < here; at += TIDE_CLIENT_PIECE) {\n"
                     "            tide_client_piece *piece = &kept%d->spans[chunk].pieces[at >> TIDE_CLIENT_PIECE_SHIFT];\n"
                     "            if (!piece->routed) {\n"
                     "                const uint32_t end = first + (here - at < TIDE_CLIENT_PIECE ? here : at + TIDE_CLIENT_PIECE);\n"
                     "                uint32_t touched = 0;\n"
                     "                for (uint32_t row = first + at; row < end; row++) {\n",
                  table, arch_columns(g, a), table, arch_columns(g, a), a);
        sb_printf(o, "                    if (tide_row_gone(%s)) continue;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "                    uint32_t words[sizeof(Audience) / sizeof(uint32_t)];\n                    memcpy(words, &audience%d[row], sizeof words);\n", a);
        sb_put(o, "                    for (uint32_t word = 0; word < sizeof words / sizeof words[0]; word++) {\n"
                  "                        for (uint32_t bits = words[word]; bits; bits &= bits - 1u) {\n"
                  "                            const uint32_t p = word * 32u + (uint32_t)__builtin_ctz(bits);\n"
                  "                            if (p >= TIDE_GAME_PLAYERS) continue;\n"
                  "                            const tide_player_id recipient = {p + 1u};\n"
                  "                            (void)recipient;\n");
        sb_printf(o, "                            uint64_t h = has%d[row * 2u + 1u];\n", a);
        int stride = 0;
        for (int c = 0; c < prog->components.count; c++) stride += arch_stores(g, a, c);
        if (!stride) stride = 1;
        for (int k = 0; k < n; k++) {
            sb_printf(o, "                            if (!client && ((has%d[row * 2u] >> %d) & 1u) && !tide_audience_contains(denied%d[row * %du + %du], recipient))\n"
                         "                                h -= terms%d[(size_t)row * %du + %du];\n",
                      a, k, a, (unsigned)n, (unsigned)k, a, (unsigned)stride, (unsigned)client_part_of(g, a, deniable[k]));
        }
        sb_put(o, "                            uint64_t *const to = &kept->route_sums[p * 2u];\n"
                  "                            if (!to[1]) kept->route_touched[touched++] = p;\n"
                  "                            to[0] += tide_client_mix(h);\n                            to[1]++;\n"
                  "                        }\n                    }\n                }\n"
                  "                if (touched > piece->route_room) {\n"
                  "                    piece->route_players = realloc(piece->route_players, (size_t)touched * sizeof *piece->route_players);\n"
                  "                    piece->route_sums = realloc(piece->route_sums, (size_t)touched * 2u * sizeof *piece->route_sums);\n"
                  "                    if (!piece->route_players || !piece->route_sums) tide_out_of_memory();\n"
                  "                    piece->route_room = touched;\n                }\n"
                  "                for (uint32_t j = 0; j < touched; j++) {\n"
                  "                    uint64_t *const from = &kept->route_sums[kept->route_touched[j] * 2u];\n"
                  "                    piece->route_players[j] = kept->route_touched[j];\n"
                  "                    piece->route_sums[j * 2u] = from[0];\n                    piece->route_sums[j * 2u + 1u] = from[1];\n"
                  "                    from[0] = from[1] = 0;\n                }\n"
                  "                piece->route_count = touched;\n"
                  "#ifndef TIDE_CLIENT_MEMO_OFF\n                piece->routed = true;\n#endif\n"
                  "            }\n"
                  "            for (uint32_t j = 0; j < piece->route_count; j++) {\n"
                  "                const int32_t i = asked[piece->route_players[j]];\n"
                  "                if (i < 0) continue;\n"
                  "                sums[(uint32_t)i * 2u] += piece->route_sums[j * 2u];\n"
                  "                sums[(uint32_t)i * 2u + 1u] += piece->route_sums[j * 2u + 1u];\n"
                  "            }\n        }\n    }\n");
    }
    for (int a = 0; a < prog->archetypes.count && !heapy; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        sb_printf(o, "    for (uint32_t row = 0; row < rows%d; row++) {\n", a);
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove): nothing kept of it above\n",
                  arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        uint32_t words[sizeof(Audience) / sizeof(uint32_t)];\n        memcpy(words, &audience%d[row], sizeof words);\n", a);
        sb_put(o, "        for (uint32_t word = 0; word < sizeof words / sizeof words[0]; word++) {\n"
                  "            for (uint32_t bits = words[word]; bits; bits &= bits - 1u) {\n"
                  "                const uint32_t p = word * 32u + (uint32_t)__builtin_ctz(bits);\n"
                  "                if (p >= TIDE_GAME_PLAYERS || asked[p] < 0) continue;\n"
                  "                const uint32_t i = (uint32_t)asked[p];\n                const tide_player_id recipient = recipients[i];\n"
                  "                (void)recipient;\n");
        sb_printf(o, "                uint64_t h = has%d[row * 2u + 1u];\n", a);
        int stride = n;
        if (heapy) {
            stride = 0;
            for (int c = 0; c < prog->components.count; c++) stride += arch_stores(g, a, c);
            if (!stride) stride = 1;
        }
        for (int k = 0; k < n; k++) {
            const int part = heapy ? client_part_of(g, a, deniable[k]) : k;
            sb_printf(o, "                if (!client && ((has%d[row * 2u] >> %d) & 1u) && !tide_audience_contains(denied%d[row * %du + %du], recipient)) h -= terms%d[(size_t)row * %du + %du];\n",
                      a, k, a, (unsigned)n, (unsigned)k, a, (unsigned)stride, (unsigned)part);
        }
        sb_put(o, "                sums[i * 2u] += tide_client_mix(h);\n                sums[i * 2u + 1u]++;\n            }\n        }\n    }\n");
    }
    if (prog->hierarchy[0]) sb_put(o, "    uint32_t hierarchy_count;\n    tide_entity *hierarchy_keys = tide_client_hierarchy_keys(w, &hierarchy_count);\n");
    sb_put(o, "    for (uint32_t i = 0; i < count; i++) {\n        const tide_player_id recipient = recipients[i];\n"
              "        // A player asked for twice has its first's\n"
              "        const int32_t p = tide_player_index(recipient);\n"
              "        const uint32_t first = p >= 0 && (uint32_t)p < TIDE_GAME_PLAYERS ? (uint32_t)asked[p] : i;\n"
              "        const uint64_t sum = sums[first * 2u], seen = sums[first * 2u + 1u];\n"
              "        out[i] = tide_client_mix(out[i] ^ tide_client_mix(sum + 0xbf58476d1ce4e5b9ull * seen));\n");
    if (prog->input) {
        const int input = client_codec_id(&codec, (type){TY_INPUT, prog->input});
        sb_put(o, "        const int32_t own = tide_player_index(recipient);\n        uint64_t inputs = 0;\n"
                  "        for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n            uint64_t h;\n"
                  "            if (own >= 0 && slot == (uint32_t)own) {\n                scratch.size = 0;\n");
        sb_printf(o, "                tide_client_put%d(&scratch, &w->inputs[slot], 0);\n                tide_client_put%d(&scratch, &w->previous_inputs[slot], 0);\n",
                  input, input);
        sb_put(o, "                if (scratch.overflow) tide_out_of_memory();\n                h = tide_hash(scratch.data, scratch.size);\n"
                  "            } else {\n"
                  "                h = client || (slot_found[slot] && tide_audience_contains(slot_audience[slot], recipient)) ? slot_hash[slot] : none_hash;\n"
                  "            }\n"
                  "            inputs += tide_client_mix(h + 0x94d049bb133111ebull * (slot + 1u));\n        }\n"
                  "        out[i] = tide_client_mix(out[i] ^ inputs);\n");
    }
    if (prog->hierarchy[0]) {
        sb_put(o, "        // Its links and anchors, as the section a projection and a repair carry\n"
                  "        scratch.size = 0;\n"
                  "        tide_client_put_hierarchy_of(&scratch, w, recipient, hierarchy_keys, hierarchy_count);\n"
                  "        if (scratch.overflow) tide_out_of_memory();\n"
                  "        out[i] = tide_client_mix(out[i] ^ (tide_hash(scratch.data, scratch.size) + 0x2545f4914f6cdd1dull));\n");
    }
    sb_put(o, "    }\n");
    if (prog->hierarchy[0]) sb_put(o, "    free(hierarchy_keys);\n");
    for (int a = 0; a < prog->archetypes.count && !heapy; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    free(audience%d);\n    free(has%d);\n", a, a);
        if (client_deniable(g, a, deniable)) sb_printf(o, "    free(denied%d);\n    free(terms%d);\n", a, a);
    }
    if (prog->input) sb_put(o, "    free(slot_audience);\n    free(slot_found);\n    free(slot_hash);\n");
    if (heapy) sb_put(o, "    if (own) {\n        tide_client_kept_free(own);\n        free(own);\n    }\n"
                         "    TIDE_CLIENT_MARK(4);\n    tide_client_memo_done(hs->memo, hs->heaps);\n    free(hs->out.data);\n    free(hs->reach);\n"
                         "#ifdef TIDE_CLIENT_COUNTS\n    tide_client_counting = false;\n#endif\n");
    sb_put(o, "    free(sums);\n    free(asked);\n    free(scratch.data);\n}\n\n"
              "static void tide_game_client_hashes(const void *world, const tide_player_id *recipients, const uint32_t count, uint64_t *out)\n{\n"
              "    tide_client_hashes_work(world, recipients, count, out, false);\n"
              "#ifdef TIDE_CLIENT_SPANS_CHECK // Every hash again from the rows themselves: what's kept is only ever a shortcut to it\n"
              "    uint64_t *rows = malloc((size_t)(count ? count : 1u) * sizeof *rows);\n    if (!rows) tide_out_of_memory();\n"
              "    tide_client_hashes_work(world, recipients, count, rows, true);\n"
              "    for (uint32_t i = 0; i < count; i++) {\n        if (rows[i] == out[i]) continue;\n"
              "        fprintf(stderr, \"tide: player %u's projection hash from what's kept (%016llx) isn't its rows' own (%016llx)\\n\",\n"
              "                recipients[i].id, (unsigned long long)out[i], (unsigned long long)rows[i]);\n        abort();\n    }\n"
              "    free(rows);\n#endif\n}\n\n"
              "static uint64_t tide_game_client_hash(const void *world, tide_player_id recipient)\n{\n"
              "    uint64_t hash;\n    tide_game_client_hashes(world, &recipient, 1u, &hash);\n    return hash;\n}\n\n");
    free(deniable);
}

// Reading archetype `a`'s rows from `in` into `next`, as tide_game_load_client_state
// does: opens a block and the loop over rows, which the caller closes ("        }\n    }\n").
static void gen_client_load_rows(gen *g, const client_codec *codec, const int a)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_put(o, "    {\n        const uint32_t count = tide_read_varint(&in);\n"
              "        if (count > (in.size - in.at) / 20u) goto invalid;\n"
              "        for (uint32_t i = 0; i < count; i++) {\n"
              "            tide_entity e;\n            e.birth = tide_read_u64(&in);\n            e.lineage = tide_read_u64(&in);\n"
              "            const uint32_t snaps = tide_read_u32(&in);\n"
              "            if (in.failed || !tide_client_reserve_entity(&next->entities, e, &budget)) goto invalid;\n");
    sb_printf(o, "            const uint32_t row = tide_table_add(&next->%s, %s);\n", arch_name(g, a), arch_columns(g, a));
    sb_printf(o, "            *%s = e;\n", arch_cell(g, "next", a, 0, "tide_entity", "row", true));
    sb_printf(o, "            tide_entity_set_location(&next->entities, e, (tide_location){%du, row});\n", a);
    sb_put(o, "            tide_entity_set_snaps(&next->entities, e, snaps);\n");
    if (arch_scene_column(g, a)) {
        sb_put(o, "            const uint8_t *scene = tide_read_bytes(&in, sizeof(tide_entity));\n            if (!scene) goto invalid;\n");
        sb_printf(o, "            memcpy(%s, scene, sizeof(tide_entity));\n", arch_cell(g, "next", a, 1, "tide_entity", "row", true));
    }
    for (int c = 0; c < prog->components.count; c++) {
        if (!arch_stores(g, a, c)) continue;
        const decl *d = prog->components.items[c];
        sb_put(o, "            {\n");
        if (comp_sparse(g, c)) {
            sb_put(o, "                const uint8_t present = tide_read_u8(&in);\n                if (present > 1u) goto invalid;\n");
            sb_printf(o, "                %s = present;\n", presence_cell(g, "next", a, c, "row", true));
            sb_put(o, "                if (present) {\n");
        }
        // Into its owner's heap, as its own code would make it (decl_where)
        sb_printf(o, "                %s value = {0};\n", type_cname(d));
        sb_printf(o, "                if (!tide_client_get%d(&in, &value, %s, 0, &budget)) goto invalid;\n",
                  client_codec_id(codec, (type){TY_COMPONENT, (decl *)d}), decl_where(d));
        sb_printf(o, "                %s;\n", comp_put(g, "next", a, c, "row", "value"));
        if (comp_sparse(g, c)) sb_put(o, "                }\n");
        sb_put(o, "            }\n");
    }
}

// Loading a projection over the one before it (tide_game.reload_client_state),
// for games without transform hierarchies: the world as it was loaded, shared
// page by page (tide_world_copy), keeps each singleton and each archetype's
// table whose bytes are the same in both; the others are let go of, what they
// held in the heap with them, and read as tide_game_load_client_state reads
// them. The entity slots are made again: of a table kept, from its rows, with
// their snaps as they were. The values come out as a fresh load's would; only
// where the heap keeps them differs, which nothing reads.
// Lets go of what a value of `d` at `value` (a place, "next->X" or "value")
// holds in its owner's heap (decl_where: whichever run's, each block through
// its own), as tide_release_<d> does: built-in singletons and components have
// no such function of their own.
static void gen_client_release(gen *g, const decl *d, const char *value, const char *indent)
{
    sb *o = &g->c;
    path_list paths = {0};
    text_paths(d, "", &paths);
    const char *where = decl_where(d);
    for (int k = 0; k < paths.count; k++) {
        const heap_path *hp = &paths.items[k];
        if (hp->list) sb_printf(o, "%stide_list%d_release(&%s.%s, %s);\n", indent, hp->list->index, value, hp->path, where);
        else if (hp->array) sb_printf(o, "%stide_array%d_release(&%s.%s, %s);\n", indent, hp->array->index, value, hp->path, where);
        else if (hp->map) sb_printf(o, "%stide_map%d_release(&%s.%s, %s);\n", indent, hp->map->index, value, hp->path, where);
        else if (hp->grid) sb_printf(o, "%stide_grid_release(&%s.%s, &%s, %s);\n", indent, value, hp->path, grid_shape_name(hp->grid), where);
        else sb_printf(o, "%stide_text_release(&%s.%s, %s);\n", indent, value, hp->path, where);
    }
}

// ...once what's gone is let go of: the released blocks freed for reuse, and
// the singletons that aren't alike read again from their bytes.
static void gen_client_reload_values(gen *g, const client_codec *codec)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_put(o, "    tide_heaps_flush(&next->heap);\n");
    int n = 0;
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        sb_printf(o, "    if (!value_same[%d]) {\n        tide_reader in = {data, value_to[%d], value_from[%d], false};\n"
                     "        if (!tide_client_get%d(&in, &next->%s, %s, 0, &budget) || in.at != value_to[%d]) goto invalid;\n    }\n",
                  n, n, n, client_codec_id(codec, (type){TY_SINGLETON, (decl *)d}), type_cname(d), decl_where(d), n);
        n++;
    }
}

static void gen_client_reload(gen *g, const client_codec *codec)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    int archetypes = 0;
    for (int a = 0; a < prog->archetypes.count; a++) if (!arch_local(g, a)) archetypes++;
    // Each archetype's bytes, skipped: where they end
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "TIDE_HELPER bool tide_client_skip_rows%d(tide_reader *in)\n{\n"
                     "    const uint32_t count = tide_read_varint(in);\n"
                     "    if (count > (in->size - in->at) / 12u) return false;\n"
                     "    for (uint32_t i = 0; i < count; i++) {\n"
                     "        if (!tide_client_read(in, 20u)) return false;\n", a);
        if (arch_scene_column(g, a)) sb_put(o, "        if (!tide_client_read(in, sizeof(tide_entity))) return false;\n");
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const int id = client_codec_id(codec, (type){TY_COMPONENT, prog->components.items[c]});
            if (comp_sparse(g, c)) {
                sb_printf(o, "        {\n            const uint8_t *present = tide_client_read(in, 1u);\n"
                             "            if (!present || *present > 1u || (*present && !tide_client_skip%d(in, 0))) return false;\n        }\n", id);
            } else {
                sb_printf(o, "        if (!tide_client_skip%d(in, 0)) return false;\n", id);
            }
        }
        sb_put(o, "    }\n    return !in->failed;\n}\n\n");
    }
    sb_put(o, "static bool tide_game_reload_client_state(void *world, const uint8_t *data, uint32_t size, const uint8_t *had, uint32_t had_size)\n{\n"
              "    tide_world *w = world;\n"
              "    tide_reader whole = {data, size, 0, false}, old = {had, had_size, 0, false};\n"
              "    uint64_t budget = SIZE_MAX;\n"
              "    if (tide_read_u32(&whole) != 0x31564954u || tide_read_u32(&old) != 0x31564954u) return false;\n"
              "    const uint32_t recipient = tide_read_u32(&whole);\n"
              "    if (!recipient || recipient > TIDE_GAME_PLAYERS || tide_read_u32(&old) != recipient\n"
              "        || w->tide_visibility_state.client != (int32_t)recipient) return false;\n"
              "    const uint64_t clock = tide_read_u64(&whole);\n    if (!clock || tide_read_u64(&old) != w->tide_identity_clock) return false;\n"
              "    tide_world *next = calloc(1, sizeof *next);\n    if (!next) tide_out_of_memory();\n    tide_entities kept = {0};\n    bool kept_taken = false;\n    tide_world_copy(next, w);\n    next->tide_identity_clock = clock;\n    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++) {\n        next->tide_lifecycle_occurrences[i] = tide_read_u64(&whole);\n        if (tide_read_u64(&old) != w->tide_lifecycle_occurrences[i]) goto invalid;\n    }\n    next->tide_main_occurrence = tide_read_u64(&whole);\n    if (tide_read_u64(&old) != w->tide_main_occurrence) goto invalid;\n    tide_text_use(&next->heap, NULL);\n"
              );
    // The singletons: which are alike, and what those that aren't held in the heap goes
    int singletons = 0;
    for (int i = 0; i < prog->singletons.count; i++) singletons += client_singleton(prog, prog->singletons.items[i]);
    if (singletons) sb_printf(o, "    uint32_t value_from[%d], value_to[%d];\n    bool value_same[%d];\n", singletons, singletons, singletons);
    int n = 0;
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        const int id = client_codec_id(codec, (type){TY_SINGLETON, (decl *)d});
        const char *name = type_cname(d);
        sb_printf(o, "    {\n        const uint32_t had_from = old.at;\n        value_from[%d] = whole.at;\n"
                     "        if (!tide_client_skip%d(&whole, 0) || !tide_client_skip%d(&old, 0)) goto invalid;\n"
                     "        value_to[%d] = whole.at;\n"
                     "        value_same[%d] = whole.at - value_from[%d] == old.at - had_from\n"
                     "            && memcmp(data + value_from[%d], had + had_from, whole.at - value_from[%d]) == 0;\n",
                  n, id, id, n, n, n, n, n);
        sb_printf(o, "        if (!value_same[%d]) {\n", n);
        char place[256];
        snprintf(place, sizeof place, "next->%s", name);
        if (decl_has_text(d)) gen_client_release(g, d, place, "            ");
        sb_printf(o, "            memset(&next->%s, 0, sizeof next->%s);\n        }\n    }\n", name, name);
        n++;
    }
    if (prog->input) {
        const int input = client_codec_id(codec, (type){TY_INPUT, prog->input});
        sb_printf(o, "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n"
                     "        if (!tide_client_get%d(&whole, &next->inputs[slot], TIDE_IN_MATCH, 0, &budget)\n"
                     "            || !tide_client_get%d(&whole, &next->previous_inputs[slot], TIDE_IN_MATCH, 0, &budget)\n"
                     "            || !tide_client_skip%d(&old, 0) || !tide_client_skip%d(&old, 0)) goto invalid;\n    }\n",
                  input, input, input, input);
    }
    if (archetypes) {
        // Which tables are alike, from where each one's bytes start and end
        sb_printf(o, "    uint32_t from[%d], to[%d];\n    bool same[%d];\n", archetypes, archetypes, archetypes);
        int k = 0;
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a)) continue;
            sb_printf(o, "    {\n        const uint32_t had_from = old.at;\n        from[%d] = whole.at;\n"
                         "        if (!tide_client_skip_rows%d(&whole) || !tide_client_skip_rows%d(&old)) goto invalid;\n"
                         "        to[%d] = whole.at;\n"
                         "        same[%d] = whole.at - from[%d] == old.at - had_from && memcmp(data + from[%d], had + had_from, whole.at - from[%d]) == 0;\n    }\n",
                      k, a, a, k, k, k, k, k);
            k++;
        }
        sb_put(o, "    if (whole.failed || whole.at != whole.size || old.failed || old.at != old.size) goto invalid;\n");
        // What the tables read again held in the heap goes, while their entities can still be found
        k = 0;
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a)) continue;
            bool heap = false;
            for (int c = 0; c < prog->components.count; c++) heap |= arch_stores(g, a, c) && decl_has_text(prog->components.items[c]);
            sb_printf(o, "    if (!same[%d]) {\n", k);
            if (heap) {
                sb_printf(o, "        for (uint32_t row = 0; row < next->%s.count; row++) {\n", arch_name(g, a));
                sb_printf(o, "            if (tide_row_gone(%s)) continue;\n", arch_cell(g, "next", a, 0, "tide_entity", "row", false));
                sb_printf(o, "            const tide_entity e = *%s;\n", arch_cell(g, "next", a, 0, "tide_entity", "row", false));
                for (int c = 0; c < prog->components.count; c++) {
                    const decl *d = prog->components.items[c];
                    if (!arch_stores(g, a, c) || !decl_has_text(d)) continue;
                    sb_printf(o, "            if (tide_has_%s(next, e)) {\n                %s value = tide_get_%s(next, e);\n",
                              type_cname(d), type_cname(d), type_cname(d));
                    gen_client_release(g, d, "value", "                ");
                    sb_put(o, "            }\n");
                }
                sb_put(o, "        }\n");
            }
            sb_printf(o, "        tide_table_free(&next->%s, %s);\n        memset(&next->%s, 0, sizeof next->%s);\n    }\n",
                      arch_name(g, a), arch_columns(g, a), arch_name(g, a), arch_name(g, a));
            k++;
        }
        gen_client_reload_values(g, codec);
        sb_put(o, "    kept = next->entities;\n    kept_taken = true;\n    memset(&next->entities, 0, sizeof next->entities);\n");
        k = 0;
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a)) continue;
            sb_printf(o, "    if (same[%d]) {\n", k);
            sb_printf(o, "        for (uint32_t row = 0; row < next->%s.count; row++) {\n", arch_name(g, a));
            sb_printf(o, "            if (tide_row_gone(%s)) goto invalid;\n", arch_cell(g, "next", a, 0, "tide_entity", "row", false));
            sb_printf(o, "            const tide_entity e = *%s;\n", arch_cell(g, "next", a, 0, "tide_entity", "row", false));
            sb_printf(o, "            if (!tide_client_reserve_entity(&next->entities, e, &budget)) goto invalid;\n"
                         "            tide_entity_set_location(&next->entities, e, (tide_location){%du, row});\n"
                         "            tide_entity_set_snaps(&next->entities, e, tide_entity_snaps(&kept, e));\n        }\n", a);
            sb_printf(o, "    } else {\n        tide_reader in = {data, to[%d], from[%d], false};\n", k, k);
            gen_client_load_rows(g, codec, a);
            sb_printf(o, "        }\n    }\n        if (in.failed || in.at != to[%d]) goto invalid;\n    }\n", k);
            k++;
        }
    } else {
        sb_put(o, "    if (whole.failed || whole.at != whole.size || old.failed || old.at != old.size) goto invalid;\n");
        gen_client_reload_values(g, codec);
    }
    sb_put(o, "    if (kept_taken) tide_entities_free(&kept);\n"
              "    next->entities.free_head = 0; // Unseen slots are reserved, not available for prediction.\n"
              "    tide_world_free(w);\n    *w = *next;\n    free(next);\n    // What reading it let go of (a list outgrown as it was read) is free before the world ticks: a tick that\n    // starts with blocks waiting would have whatever system flushes first take them, on whichever thread\n    tide_heaps_flush(&w->heap);\n    tide_text_use(&w->heap, NULL);\n    return true;\n"
              "invalid:\n    if (kept_taken) tide_entities_free(&kept);\n    tide_world_free(next);\n    free(next);\n    tide_text_use(&w->heap, NULL);\n    return false;\n}\n\n");
}

static void gen_client_state(gen *g)
{
    const program *prog = g->prog;
    if (!prog->visibility_state) return;
    sb *o = &g->c;
    client_codec codec = g->value_codec;
    gen_client_hierarchy_codec(g);
    gen_client_input_filter(g);

    sb_put(o, "static uint8_t *tide_game_client_state(const void *world, tide_player_id recipient, uint32_t *size)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heaps *)&w->heap, NULL);\n"
              "    tide_writer out = {.grows = true};\n"
              "    tide_write_u32(&out, 0x31564954u);\n    tide_write_u32(&out, recipient.id);\n    tide_write_u64(&out, w->tide_identity_clock);\n    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++) tide_write_u64(&out, w->tide_lifecycle_occurrences[i]);\n    tide_write_u64(&out, w->tide_main_occurrence);\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        if (d->builtin && str_eq_c(d->name, "tide_command_state")) {
            sb_printf(o, "    %s permitted_commands = {0};\n", type_cname(d));
            for (int k = 0; k < d->fields.count; k++) {
                const field *f = &d->fields.items[k];
                uint32_t player = 0;
                int start = 0;
                while (start < f->name.len && (f->name.ptr[start] < '0' || f->name.ptr[start] > '9')) start++;
                for (; start < f->name.len && f->name.ptr[start] >= '0' && f->name.ptr[start] <= '9'; start++)
                    player = player * 10u + (uint32_t)(f->name.ptr[start] - '0');
                sb_printf(o, "    if (recipient.id == %uu) permitted_commands.%s = w->%s.%s;\n", player + 1u,
                          field_cname(f), type_cname(d), field_cname(f));
            }
            sb_printf(o, "    tide_client_put%d(&out, &permitted_commands, 0);\n", client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d}));
        } else gen_client_put_singleton(g, &codec, d, "&out");
    }
    if (prog->input) {
        const int input = client_codec_id(&codec, (type){TY_INPUT, prog->input});
        sb_put(o, "    bool allowed[TIDE_GAME_PLAYERS + 1u];\n    tide_client_input_mask(w, recipient, allowed);\n"
                  "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n");
        sb_printf(o, "        %s current = w->inputs[slot], previous = w->previous_inputs[slot];\n", type_cname(prog->input));
        sb_put(o, "        if (!allowed[slot]) { current = ");
        gen_value(g, o, prog->input, NULL, 0);
        sb_put(o, "; previous = current; }\n"
                  "        if (slot != (uint32_t)tide_player_index(recipient)) previous = current;\n");
        sb_printf(o, "        tide_client_put%d(&out, &current, 0);\n        tide_client_put%d(&out, &previous, 0);\n    }\n", input, input);
    }
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        // Its entities in the order of their identities, not of their rows: the bytes say what the recipient
        // has, the same wherever each world keeps it (a recipient's rows are where its own ticks and its
        // repairs put them, tide_game_apply_client_changes)
        sb_printf(o, "    {\n        uint32_t count = 0, tide_key_count = 0;\n        const uint32_t tide_archetype = %du;\n"
                     "        const tide_entity *tide_keys = tide_entities_ordered_archetypes(&w->entities, &tide_archetype, 1u, &tide_key_count);\n", a);
        sb_put(o, "        for (uint32_t tide_k = 0; tide_k < tide_key_count; tide_k++) {\n"
                  "            if (tide_visibility_entity(w, tide_keys[tide_k], recipient)) count++;\n"
                  "        }\n        tide_write_varint(&out, count);\n");
        sb_put(o, "        for (uint32_t tide_k = 0; tide_k < tide_key_count; tide_k++) {\n"
                  "            const tide_entity e = tide_keys[tide_k];\n"
                  "            if (!tide_visibility_entity(w, e, recipient)) continue;\n"
                  "            const uint32_t row = tide_entity_location(&w->entities, e).row;\n            (void)row;\n"
                  "            tide_write_u64(&out, e.birth);\n            tide_write_u64(&out, e.lineage);\n"
                  "            tide_write_u32(&out, tide_entity_snaps(&w->entities, e));\n");
        if (arch_scene_column(g, a)) {
            sb_printf(o, "            const tide_entity scene = *%s;\n", arch_cell(g, "w", a, 1, "tide_entity", "row", false));
            sb_put(o, "            tide_write_bytes(&out, &scene, sizeof scene);\n");
        }
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            sb_put(o, "            {\n");
            if (comp_sparse(g, c)) {
                sb_printf(o, "                const bool present = tide_visibility_component(w, e, %du, recipient);\n", c);
                sb_put(o, "                tide_write_u8(&out, present ? 1u : 0u);\n                if (present) {\n");
            }
            if (client_natives(d)) {
                char from[256];
                snprintf(from, sizeof from, "tide_get_%s(w, e)", type_cname(d));
                gen_client_bare(g, d, "value", from, "                ");
            } else sb_printf(o, "                const %s value = tide_get_%s(w, e);\n", type_cname(d), type_cname(d));
            sb_printf(o, "                tide_client_put%d(&out, &value, 0);\n", client_codec_id(&codec, (type){TY_COMPONENT, (decl *)d}));
            if (comp_sparse(g, c)) sb_put(o, "                }\n");
            sb_put(o, "            }\n");
        }
        sb_put(o, "        }\n    }\n");
    }
    if (prog->hierarchy[0]) sb_put(o, "    tide_client_put_hierarchy(&out, w, recipient);\n");
    sb_put(o, "    if (out.overflow) tide_out_of_memory();\n    *size = out.size;\n    return out.data;\n}\n\n");
    gen_client_hashes(g);

    sb_put(o, "TIDE_HELPER bool tide_client_reserve_entity(tide_entities *entities, tide_entity e, uint64_t *budget)\n{\n"
              "    if (tide_entity_is_null(e) || !e.birth) return false;\n"
              "    if (!tide_client_budget(budget, sizeof(tide_entity_slot) + 64u)) return false;\n"
              "    return tide_entity_install(entities, e);\n}\n\n"
              "static bool tide_game_load_client_state(void *world, const uint8_t *data, uint32_t size)\n{\n"
              "    tide_world *w = world;\n"
              "    tide_reader in = {data, size, 0, false};\n"
              "    uint64_t budget = SIZE_MAX;\n"
              "    if (tide_read_u32(&in) != 0x31564954u) return false;\n"
              "    const uint32_t recipient = tide_read_u32(&in);\n"
              "    if (!recipient || recipient > TIDE_GAME_PLAYERS) return false;\n"
              "    tide_world *next = calloc(1, sizeof *next);\n    if (!next) tide_out_of_memory();\n"
              "    tide_text_use(&next->heap, NULL);\n    next->tide_visibility_state.client = (int32_t)recipient;\n    next->tide_identity_clock = tide_read_u64(&in);\n    if (!next->tide_identity_clock) goto invalid;\n    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++) next->tide_lifecycle_occurrences[i] = tide_read_u64(&in);\n    next->tide_main_occurrence = tide_read_u64(&in);\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        sb_printf(o, "    if (!tide_client_get%d(&in, &next->%s, %s, 0, &budget)) goto invalid;\n",
                  client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d}), type_cname(d), decl_where(d));
    }
    if (prog->input) {
        const int input = client_codec_id(&codec, (type){TY_INPUT, prog->input});
        sb_printf(o, "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n"
                     "        if (!tide_client_get%d(&in, &next->inputs[slot], TIDE_IN_MATCH, 0, &budget)\n"
                     "            || !tide_client_get%d(&in, &next->previous_inputs[slot], TIDE_IN_MATCH, 0, &budget)) goto invalid;\n    }\n", input, input);
    }
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        gen_client_load_rows(g, &codec, a);
        sb_put(o, "        }\n    }\n");
    }
    if (sharded(g)) sb_put(o, "    if (!tide_shard_words_valid(next)) goto invalid;\n");
    if (prog->hierarchy[0]) sb_put(o, "    if (!tide_client_take_hierarchy(next, &in) || !tide_client_hierarchy_valid(next)) goto invalid;\n");
    sb_put(o, "    if (in.failed || in.at != in.size) goto invalid;\n"
              "    next->entities.free_head = 0; // Unseen slots are reserved, not available for prediction.\n"
              "    tide_world_free(w);\n    *w = *next;\n    free(next);\n    // What reading it let go of (a list outgrown as it was read) is free before the world ticks: a tick that\n    // starts with blocks waiting would have whatever system flushes first take them, on whichever thread\n    tide_heaps_flush(&w->heap);\n    tide_text_use(&w->heap, NULL);\n    return true;\n"
              "invalid:\n    tide_world_free(next);\n    free(next);\n    tide_text_use(&w->heap, NULL);\n    return false;\n}\n\n"
              "static uint32_t tide_game_client_destroyed(const void *world, const void *previous, tide_player_id recipient, tide_entity *out, uint32_t capacity)\n{\n"
              "    const tide_world *w = world, *had = previous;\n    if (!had) return 0;\n"
              "    tide_text_use((tide_heaps *)&had->heap, NULL);\n    uint32_t count = 0;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < had->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n", arch_cell(g, "had", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "had", a, 0, "tide_entity", "row", false));
        sb_put(o, "        if (!tide_entity_alive(&w->entities, e) && tide_visibility_entity(had, e, recipient)) {\n"
                  "            if (out && count < capacity) out[count] = e;\n            count++;\n        }\n    }\n");
    }
    sb_put(o, "    return count;\n}\n\n");
    if (!prog->hierarchy[0]) gen_client_reload(g, &codec);
}
