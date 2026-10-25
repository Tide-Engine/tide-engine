// Per-recipient state uses values rather than storage offsets. In particular,
// an otherwise identical client's heap can have a different allocation history.
// This file is included by codegen.c after the ordinary storage helpers.

static bool client_singleton(const program *prog, const decl *d)
{
    return !d->is_local && d != prog->visibility_state && d != prog->hierarchy[0] && d != prog->task_slots[0];
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
              "    const uint32_t mark = tide_scratch_mark();\n"
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
              "    tide_text_use((tide_heap *)&w->heap, NULL);\n"
              "    bool found[TIDE_GAME_PLAYERS + 1u] = {0};\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
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
              "    tide_text_use((tide_heap *)&w->heap, NULL);\n"
              "    // On the heap: an audience of the most players a game takes is 8 KiB\n"
              "    Audience *audience = malloc((size_t)slots * sizeof *audience);\n    bool *found = malloc(slots);\n"
              "    if (!audience || !found) tide_out_of_memory();\n"
              "    for (uint32_t slot = 0; slot < slots; slot++) {\n        audience[slot] = TIDE_AUDIENCE_ALL;\n        found[slot] = false;\n    }\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        if (arch_stores(g, a, prog->owner->index)) {
            sb_printf(o, "        const int32_t owner_index = tide_player_index(tide_get_%s(w, e).player);\n", type_cname(prog->owner));
            sb_put(o, "        const uint32_t slot = owner_index >= 0 && (uint32_t)owner_index < TIDE_GAME_PLAYERS ? (uint32_t)owner_index : TIDE_GAME_PLAYERS;\n");
        } else sb_put(o, "        const uint32_t slot = TIDE_GAME_PLAYERS;\n");
        sb_put(o, "        found[slot] = true;\n"
                  "        audience[slot] = tide_audience_intersect(audience[slot], tide_visibility_audience(w, e, -1));\n");
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            if (!d->visibility_optional) continue;
            sb_printf(o, "        if (tide_has_%s(w, e)) audience[slot] = tide_audience_intersect(audience[slot], tide_visibility_audience(w, e, %d));\n",
                      type_cname(d), c);
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
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        if (n > 64) {
            diag_error(prog->components.items[deniable[0]]->at, "an entity has more than 64 components whose visibility is set apart");
            break;
        }
        sb_printf(o, "TIDE_HELPER uint64_t tide_client_entity_hash%d(const tide_world *w, const tide_entity e, const uint64_t mask, tide_writer *out)\n{\n"
                     "    (void)mask;\n    out->size = 0;\n    tide_write_u32(out, %du);\n"
                     "    tide_write_u32(out, e.index);\n    tide_write_u32(out, e.generation);\n"
                     "    tide_write_u32(out, tide_entity_snaps(&w->entities, e));\n", a, (unsigned)a);
        if (arch_scene_column(g, a)) {
            sb_put(o, "    {\n        const tide_location loc = tide_entity_location(&w->entities, e);\n");
            sb_printf(o, "        const uint32_t row = loc.row;\n        const tide_entity scene = *%s;\n", arch_cell(g, "w", a, 1, "tide_entity", "row", false));
            sb_put(o, "        tide_write_bytes(out, &scene, sizeof scene);\n    }\n");
        }
        int k = 0;
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            sb_put(o, "    {\n");
            if (comp_sparse(g, c)) {
                if (d->visibility_optional) sb_printf(o, "        const bool present = ((mask >> %d) & 1u) != 0;\n", k++);
                else sb_printf(o, "        const bool present = tide_has_%s(w, e);\n", type_cname(d));
                sb_put(o, "        tide_write_u8(out, present ? 1u : 0u);\n        if (present) {\n");
            }
            sb_printf(o, "        const %s value = tide_get_%s(w, e);\n", type_cname(d), type_cname(d));
            sb_printf(o, "        tide_client_put%d(out, &value, 0);\n", client_codec_id(&codec, (type){TY_COMPONENT, (decl *)d}));
            if (comp_sparse(g, c)) sb_put(o, "        }\n");
            sb_put(o, "    }\n");
        }
        sb_put(o, "    if (out->overflow) tide_out_of_memory();\n    return tide_hash(out->data, out->size);\n}\n\n");
    }

    sb_put(o, "static void tide_game_client_hashes(const void *world, const tide_player_id *recipients, const uint32_t count, uint64_t *out)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)&w->heap, NULL);\n"
              "    const bool client = w->tide_visibility_state.client != 0;\n"
              "    tide_writer scratch = {.grows = true};\n"
              "    // The singletons, alike for every recipient but the commands, which are each one's own\n"
              "    tide_write_u32(&scratch, 0x32564954u);\n");
    const decl *commands = NULL;
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        if (d->builtin && str_eq_c(d->name, "tide_command_state")) {
            commands = d;
            continue;
        }
        sb_printf(o, "    tide_client_put%d(&scratch, &w->%s, 0);\n", client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d}), type_cname(d));
    }
    sb_put(o, "    if (scratch.overflow) tide_out_of_memory();\n"
              "    const uint64_t shared = tide_hash(scratch.data, scratch.size);\n"
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
    sb_put(o, "    // Each entity once: who may have it, who may have each component that can be denied, and its hash with all it has\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        sb_printf(o, "    const uint32_t rows%d = w->%s.count;\n", a, arch_name(g, a));
        sb_printf(o, "    Audience *audience%d = rows%d ? malloc((size_t)rows%d * sizeof *audience%d) : NULL;\n"
                     "    uint64_t *has%d = rows%d ? malloc((size_t)rows%d * 4u * sizeof *has%d) : NULL;\n"
                     "    if (rows%d && (!audience%d || !has%d)) tide_out_of_memory();\n", a, a, a, a, a, a, a, a, a, a, a);
        if (n) {
            sb_printf(o, "    Audience *denied%d = rows%d ? malloc((size_t)rows%d * %du * sizeof *denied%d) : NULL;\n"
                         "    if (rows%d && !denied%d) tide_out_of_memory();\n", a, a, a, (unsigned)n, a, a, a);
        }
        sb_printf(o, "    for (uint32_t row = 0; row < rows%d; row++) {\n", a);
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "        uint64_t mask = 0;\n");
        for (int k = 0; k < n; k++) sb_printf(o, "        if (tide_has_%s(w, e)) mask |= 1ull << %d;\n", type_cname(prog->components.items[deniable[k]]), k);
        sb_printf(o, "        // Its hash with all it has, and room for one with less\n"
                     "        has%d[row * 4u] = mask;\n        has%d[row * 4u + 1u] = tide_client_entity_hash%d(w, e, mask, &scratch);\n"
                     "        has%d[row * 4u + 2u] = UINT64_MAX;\n        has%d[row * 4u + 3u] = 0;\n", a, a, a, a, a);
        sb_printf(o, "        audience%d[row] = client ? TIDE_AUDIENCE_ALL : tide_visibility_audience(w, e, -1);\n", a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "        denied%d[row * %du + %du] = client || !((mask >> %d) & 1u) ? TIDE_AUDIENCE_ALL : tide_visibility_audience(w, e, %d);\n",
                      a, (unsigned)n, (unsigned)k, k, deniable[k]);
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
    sb_put(o, "    for (uint32_t i = 0; i < count; i++) {\n        const tide_player_id recipient = recipients[i];\n"
              "        uint64_t sum = 0, seen = 0;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        sb_printf(o, "        for (uint32_t row = 0; row < rows%d; row++) {\n", a);
        sb_printf(o, "            if (!client && !tide_audience_contains(audience%d[row], recipient)) continue;\n", a);
        sb_printf(o, "            uint64_t mask = has%d[row * 4u], h;\n", a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "            if (!client && ((mask >> %d) & 1u) && !tide_audience_contains(denied%d[row * %du + %du], recipient)) mask &= ~(1ull << %d);\n",
                      k, a, (unsigned)n, (unsigned)k, k);
        }
        sb_printf(o, "            if (mask == has%d[row * 4u]) h = has%d[row * 4u + 1u];\n"
                     "            else if (mask == has%d[row * 4u + 2u]) h = has%d[row * 4u + 3u];\n"
                     "            else {\n", a, a, a, a);
        sb_printf(o, "                const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "                h = tide_client_entity_hash%d(w, e, mask, &scratch);\n"
                     "                has%d[row * 4u + 2u] = mask;\n                has%d[row * 4u + 3u] = h;\n            }\n"
                     "            sum += tide_client_mix(h);\n            seen++;\n        }\n", a, a, a);
    }
    sb_put(o, "        out[i] = tide_client_mix(out[i] ^ tide_client_mix(sum + 0xbf58476d1ce4e5b9ull * seen));\n");
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
    sb_put(o, "    }\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    free(audience%d);\n    free(has%d);\n", a, a);
        if (client_deniable(g, a, deniable)) sb_printf(o, "    free(denied%d);\n", a);
    }
    if (prog->input) sb_put(o, "    free(slot_audience);\n    free(slot_found);\n    free(slot_hash);\n");
    sb_put(o, "    free(scratch.data);\n}\n\n"
              "static uint64_t tide_game_client_hash(const void *world, tide_player_id recipient)\n{\n"
              "    uint64_t hash;\n    tide_game_client_hashes(world, &recipient, 1u, &hash);\n    return hash;\n}\n\n");
    free(deniable);
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
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)&w->heap, NULL);\n"
              "    tide_writer out = {.grows = true};\n"
              "    tide_write_u32(&out, 0x31564954u);\n    tide_write_u32(&out, recipient.id);\n");
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
        } else sb_printf(o, "    tide_client_put%d(&out, &w->%s, 0);\n", client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d}), type_cname(d));
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
        sb_put(o, "    {\n        uint32_t count = 0;\n");
        sb_printf(o, "        for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "            const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "            if (tide_visibility_entity(w, e, recipient)) count++;\n"
                  "        }\n        tide_write_varint(&out, count);\n");
        sb_printf(o, "        for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "            const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "            if (!tide_visibility_entity(w, e, recipient)) continue;\n"
                  "            tide_write_u32(&out, e.index);\n            tide_write_u32(&out, e.generation);\n"
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
            sb_printf(o, "                const %s value = tide_get_%s(w, e);\n", type_cname(d), type_cname(d));
            sb_printf(o, "                tide_client_put%d(&out, &value, 0);\n", client_codec_id(&codec, (type){TY_COMPONENT, (decl *)d}));
            if (comp_sparse(g, c)) sb_put(o, "                }\n");
            sb_put(o, "            }\n");
        }
        if (prog->hierarchy[0]) {
            sb_put(o, "            const tide_hierarchy_node node = tide_client_hierarchy_node(w, e, recipient);\n"
                      "            tide_write_bytes(&out, &node, sizeof node);\n"
                      "            tide_visibility_anchor anchor = {0};\n"
                      "            const bool anchored = tide_client_hierarchy_anchor(w, e, recipient, &anchor);\n"
                      "            tide_write_u8(&out, anchored ? 1u : 0u);\n"
                      "            if (anchored) tide_write_bytes(&out, &anchor, sizeof anchor);\n");
        }
        sb_put(o, "        }\n    }\n");
    }
    sb_put(o, "    if (out.overflow) tide_out_of_memory();\n    *size = out.size;\n    return out.data;\n}\n\n");
    if (prog->hierarchy[0]) {
        sb_put(o, "static uint64_t tide_game_client_hash(const void *world, tide_player_id recipient)\n{\n"
                  "    uint32_t size;\n    uint8_t *bytes = tide_game_client_state(world, recipient, &size);\n"
                  "    const uint64_t hash = tide_hash(bytes, size);\n    free(bytes);\n    return hash;\n}\n\n");
    } else {
        gen_client_hashes(g);
    }

    sb_put(o, "TIDE_HELPER bool tide_client_reserve_entity(tide_entities *entities, tide_entity e, uint64_t *budget)\n{\n"
              "    if (!(e.generation & 1u) || (e.generation & TIDE_ENTITY_TEMPORARY) || e.index == UINT32_MAX) return false;\n"
              "    const uint32_t pages = (e.index >> TIDE_ENTITY_PAGE_SHIFT) + 1u;\n"
              "    if (pages > entities->room) {\n"
              "        if (!tide_client_budget(budget, ((uint64_t)pages * 2u + 1u) * sizeof(tide_page *))) return false;\n"
              "        tide_page **base = calloc((size_t)pages * 2u + 1u, sizeof *base);\n"
              "        if (!base) tide_out_of_memory();\n    tide_page **list = base + 1;\n"
              "        if (entities->pages) memcpy(list, entities->page, entities->pages * sizeof *list);\n"
              "        if (entities->page) free(entities->page - 1);\n        entities->page = list;\n        entities->room = pages;\n"
              "    }\n"
              "    if (entities->pages < pages) entities->pages = pages;\n"
              "    if (!entities->page[e.index >> TIDE_ENTITY_PAGE_SHIFT]) {\n"
              "        if (!tide_client_budget(budget, (1u << TIDE_ENTITY_PAGE_SHIFT) * sizeof(tide_entity_slot))) return false;\n"
              "        entities->page[e.index >> TIDE_ENTITY_PAGE_SHIFT] = tide_page_new((1u << TIDE_ENTITY_PAGE_SHIFT) * sizeof(tide_entity_slot));\n    }\n"
              "    if (e.index >= entities->next_unused) entities->next_unused = e.index + 1u;\n"
              "    tide_entity_slot *slot = (tide_entity_slot *)tide_page_data(entities->page[e.index >> TIDE_ENTITY_PAGE_SHIFT])\n"
              "        + (e.index & ((1u << TIDE_ENTITY_PAGE_SHIFT) - 1u));\n"
              "    if (slot->generation & 1u) return false;\n"
              "    slot->generation = e.generation;\n    return true;\n}\n\n"
              "static bool tide_game_load_client_state(void *world, const uint8_t *data, uint32_t size)\n{\n"
              "    tide_world *w = world;\n    tide_world next = {0};\n"
              "    tide_reader in = {data, size, 0, false};\n"
              "    uint64_t budget = SIZE_MAX;\n"
              "    if (tide_read_u32(&in) != 0x31564954u) return false;\n"
              "    const uint32_t recipient = tide_read_u32(&in);\n"
              "    if (!recipient || recipient > TIDE_GAME_PLAYERS) return false;\n"
              "    tide_text_use(&next.heap, NULL);\n    next.tide_visibility_state.client = (int32_t)recipient;\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        sb_printf(o, "    if (!tide_client_get%d(&in, &next.%s, TIDE_IN_MATCH, 0, &budget)) goto invalid;\n",
                  client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d}), type_cname(d));
    }
    if (prog->input) {
        const int input = client_codec_id(&codec, (type){TY_INPUT, prog->input});
        sb_printf(o, "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n"
                     "        if (!tide_client_get%d(&in, &next.inputs[slot], TIDE_IN_MATCH, 0, &budget)\n"
                     "            || !tide_client_get%d(&in, &next.previous_inputs[slot], TIDE_IN_MATCH, 0, &budget)) goto invalid;\n    }\n", input, input);
    }
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_put(o, "    {\n        const uint32_t count = tide_read_varint(&in);\n"
                  "        if (count > (in.size - in.at) / 12u) goto invalid;\n"
                  "        for (uint32_t i = 0; i < count; i++) {\n"
                  "            tide_entity e;\n            e.index = tide_read_u32(&in);\n            e.generation = tide_read_u32(&in);\n"
                  "            const uint32_t snaps = tide_read_u32(&in);\n"
                  "            if (in.failed || !tide_client_reserve_entity(&next.entities, e, &budget)) goto invalid;\n");
        sb_printf(o, "            const uint32_t row = tide_table_add(&next.%s, %s);\n", arch_name(g, a), arch_columns(g, a));
        sb_printf(o, "            *%s = e;\n", arch_cell(g, "(&next)", a, 0, "tide_entity", "row", true));
        sb_printf(o, "            tide_entity_set_location(&next.entities, e, (tide_location){%du, row});\n", a);
        sb_put(o, "            tide_entity_slot *slot = (tide_entity_slot *)tide_page_data(next.entities.page[e.index >> TIDE_ENTITY_PAGE_SHIFT])\n"
                  "                + (e.index & ((1u << TIDE_ENTITY_PAGE_SHIFT) - 1u));\n            slot->snaps = snaps;\n");
        if (arch_scene_column(g, a)) {
            sb_put(o, "            const uint8_t *scene = tide_read_bytes(&in, sizeof(tide_entity));\n            if (!scene) goto invalid;\n");
            sb_printf(o, "            memcpy(%s, scene, sizeof(tide_entity));\n", arch_cell(g, "(&next)", a, 1, "tide_entity", "row", true));
        }
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            sb_put(o, "            {\n");
            if (comp_sparse(g, c)) {
                sb_put(o, "                const uint8_t present = tide_read_u8(&in);\n                if (present > 1u) goto invalid;\n");
                sb_printf(o, "                %s = present;\n", presence_cell(g, "(&next)", a, c, "row", true));
                sb_put(o, "                if (present) {\n");
            }
            sb_printf(o, "                %s value = {0};\n", type_cname(d));
            sb_printf(o, "                if (!tide_client_get%d(&in, &value, TIDE_IN_MATCH, 0, &budget)) goto invalid;\n",
                      client_codec_id(&codec, (type){TY_COMPONENT, (decl *)d}));
            sb_printf(o, "                %s;\n", comp_put(g, "(&next)", a, c, "row", "value"));
            if (comp_sparse(g, c)) sb_put(o, "                }\n");
            sb_put(o, "            }\n");
        }
        if (prog->hierarchy[0]) {
            sb_put(o, "            const uint8_t *bytes = tide_read_bytes(&in, sizeof(tide_hierarchy_node));\n"
                      "            if (!bytes) goto invalid;\n"
                      "            tide_hierarchy_node node = {0};\n            memcpy(&node, bytes, sizeof node);\n"
                      "            tide_hierarchy_put(&next, e, node);\n"
                      "            const uint8_t anchored = tide_read_u8(&in);\n"
                      "            if (anchored > 1u || (anchored && !tide_entity_is_null(node.parent))) goto invalid;\n"
                      "            if (anchored) {\n"
                      "                const uint8_t *value = tide_read_bytes(&in, sizeof(tide_visibility_anchor));\n"
                      "                if (!value) goto invalid;\n"
                      "                tide_visibility_anchor anchor = {0};\n"
                      "                memcpy(&anchor, value, sizeof anchor);\n"
                      "                if (!tide_matrix_finite(anchor.matrix) || !tide_is_finite_f(anchor.rotation.value.x)\n"
                      "                    || !tide_is_finite_f(anchor.rotation.value.y) || !tide_is_finite_f(anchor.rotation.value.z)\n"
                      "                    || !tide_is_finite_f(anchor.rotation.value.w)) goto invalid;\n");
            sb_printf(o, "                tide_map%d_set(&next.tide_visibility_state.anchors, e, anchor, TIDE_IN_MATCH);\n            }\n",
                      prog->visibility_state->fields.items[3].type.decl->index);
        }
        sb_put(o, "        }\n    }\n");
    }
    if (prog->hierarchy[0]) sb_put(o, "    if (!tide_client_hierarchy_valid(&next)) goto invalid;\n");
    sb_put(o, "    if (in.failed || in.at != in.size) goto invalid;\n"
              "    next.entities.free_head = 0; // Unseen slots are reserved, not available for prediction.\n"
              "    tide_world_free(w);\n    *w = next;\n    tide_text_use(&w->heap, NULL);\n    return true;\n"
              "invalid:\n    tide_world_free(&next);\n    tide_text_use(&w->heap, NULL);\n    return false;\n}\n\n"
              "static uint32_t tide_game_client_destroyed(const void *world, const void *previous, tide_player_id recipient, tide_entity *out, uint32_t capacity)\n{\n"
              "    const tide_world *w = world, *had = previous;\n    if (!had) return 0;\n"
              "    tide_text_use((tide_heap *)&had->heap, NULL);\n    uint32_t count = 0;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < had->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "had", a, 0, "tide_entity", "row", false));
        sb_put(o, "        if (!tide_entity_alive(&w->entities, e) && tide_visibility_entity(had, e, recipient)) {\n"
                  "            if (out && count < capacity) out[count] = e;\n            count++;\n        }\n    }\n");
    }
    sb_put(o, "    return count;\n}\n\n");
}
