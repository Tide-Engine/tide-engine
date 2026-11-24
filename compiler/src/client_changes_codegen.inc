// What changed for one recipient between two worlds of a match, entity by
// entity (tide_game.client_changes), and a recipient's world taking it
// (apply_client_changes): a repair that costs what changed, not what the
// recipient has. A world of chunks sends the chunks that came into sight, not
// its projection again. Included by codegen.c after client_state_codegen.inc.
// A game with transform hierarchies sends its recipient's links and anchors as
// one section after the records, whole each time (tide_client_put_hierarchy).
//
// Three byte strings. The plan names the entities whose permitted values differ
// (by their projection hashes, kept from the tick's own: tide_client_hv) or that
// came into the recipient's sight, and those that left it. `now` is what's alike
// for every such repair (the clock, singletons and inputs, as a projection
// starts) and then each named entity's record; `was` is the same of the world
// the recipient last confirmed, which it makes again itself, so the session
// sends `now` as what differs from `was` (engine/src/recipient_delta.h).

// A row's record: its snaps, scene and components as the recipient may have them. In scope: out
// (tide_writer *), w, e, row, recipient.
static void gen_client_put_row(gen *g, const client_codec *codec, const int a)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_put(o, "        tide_write_u32(out, tide_entity_snaps(&w->entities, e));\n");
    if (arch_scene_column(g, a)) {
        sb_printf(o, "        {\n            const tide_entity scene = *%s;\n", arch_cell(g, "w", a, 1, "tide_entity", "row", false));
        sb_put(o, "            tide_write_bytes(out, &scene, sizeof scene);\n        }\n");
    }
    for (int c = 0; c < prog->components.count; c++) {
        if (!arch_stores(g, a, c)) continue;
        const decl *d = prog->components.items[c];
        sb_put(o, "        {\n");
        if (comp_sparse(g, c)) {
            sb_printf(o, "            const bool present = tide_visibility_component(w, e, %du, recipient);\n", c);
            sb_put(o, "            tide_write_u8(out, present ? 1u : 0u);\n            if (present) {\n");
        }
        if (client_natives(d)) {
            char from[256];
            snprintf(from, sizeof from, "tide_get_%s(w, e)", type_cname(d));
            gen_client_bare(g, d, "value", from, "            ");
        } else sb_printf(o, "            const %s value = tide_get_%s(w, e);\n", type_cname(d), type_cname(d));
        sb_printf(o, "            tide_client_put%d(out, &value, 0);\n", client_codec_id(codec, (type){TY_COMPONENT, (decl *)d}));
        if (comp_sparse(g, c)) sb_put(o, "            }\n");
        sb_put(o, "        }\n");
    }
}

// Reading such a record into a new row of `next` for `e`, as a projection's rows load (gen_client_load_rows).
// In scope: next, in (a tide_reader), budget, e; `invalid` is where bytes that aren't one go.
static void gen_client_take_row(gen *g, const client_codec *codec, const int a)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_put(o, "            const uint32_t snaps = tide_read_u32(&in);\n"
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

static void gen_client_changes(gen *g)
{
    const program *prog = g->prog;
    if (!prog->visibility_state) return;
    sb *o = &g->c;
    client_codec codec = g->value_codec;
    const bool heapy = client_hash_heapy(g);
    const char *hasher_type = heapy ? "tide_client_hasher *" : "tide_writer *";
    int *deniable = malloc(((size_t)prog->components.count + 1u) * sizeof *deniable);
    if (!deniable) abort();

    // What every such repair starts with, as a projection does: whose it is, the clock, the singletons, the inputs
    sb_put(o, "TIDE_HELPER void tide_client_put_shared(tide_writer *out_, const tide_world *w, const tide_player_id recipient)\n{\n"
              "    tide_writer out = *out_;\n"
              "    tide_write_u32(&out, recipient.id);\n    tide_write_u64(&out, w->tide_identity_clock);\n"
              "    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++) tide_write_u64(&out, w->tide_lifecycle_occurrences[i]);\n"
              "    tide_write_u64(&out, w->tide_main_occurrence);\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        if (d->builtin && str_eq_c(d->name, "tide_command_state")) {
            sb_printf(o, "    {\n    %s permitted_commands = {0};\n", type_cname(d));
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
            sb_printf(o, "    tide_client_put%d(&out, &permitted_commands, 0);\n    }\n", client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d}));
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
    sb_put(o, "    *out_ = out;\n}\n\n");

    // One entity's record
    sb_put(o, "TIDE_HELPER void tide_client_put_record(tide_writer *out, const tide_world *w, const tide_entity e, const uint32_t archetype,\n"
              "                                        const uint32_t row, const tide_player_id recipient)\n{\n"
              "    (void)row;\n    (void)recipient;\n    switch (archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    case %d: {\n", a);
        gen_client_put_row(g, &codec, a);
        sb_put(o, "        break;\n    }\n");
    }
    sb_put(o, "    default: break;\n    }\n}\n\n");

    // For tests (a build that says TIDE_CLIENT_CHANGES_STATS): the repairs worked out from the rows written down, those
    // that walked every row, and the rows the first looked at
    sb_put(o, "#ifdef TIDE_CLIENT_CHANGES_STATS\nuint64_t tide_client_changes_stats[3];\n#endif\n\n");
    // The entities a recipient may have, each with what it hashes to as permitted
    sb_put(o, "typedef struct tide_client_seen { tide_entity e; uint64_t hash; uint32_t archetype, row; } tide_client_seen;\n"
              "typedef struct tide_client_seen_list { tide_client_seen *items; uint32_t count, room; } tide_client_seen_list;\n\n"
              "TIDE_HELPER void tide_client_seen_push(tide_client_seen_list *l, const tide_entity e, const uint32_t archetype, const uint32_t row,\n"
              "                                       const uint64_t hash)\n{\n"
              "    if (l->count == l->room) {\n        l->room = l->room ? l->room * 2u : 256u;\n"
              "        l->items = realloc(l->items, (size_t)l->room * sizeof *l->items);\n        if (!l->items) tide_out_of_memory();\n    }\n"
              "    l->items[l->count++] = (tide_client_seen){e, hash, archetype, row};\n}\n\n");
    // One row, read where it is: who may have it and each component that can be denied, through the walk's views
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        sb_printf(o, "TIDE_HELPER void tide_client_see_row%d(const tide_world *w, const tide_player_id recipient, const uint32_t row, %shs,\n"
                     "                                      tide_visibility_walk *walk, tide_client_seen_list *seen)\n{\n", a, hasher_type);
        sb_printf(o, "    const tide_entity e = *%s;\n    const tide_location loc = {%du, row};\n",
                  arch_cell(g, "w", a, 0, "tide_entity", "row", false), a);
        sb_put(o, "    Visibility policy = {0};\n    bool has_policy = false;\n    int64_t groups[2];\n"
                  "    const Audience base = tide_visibility_base_at(w, e, loc, &policy, &has_policy, walk, groups);\n"
                  "    if (!tide_audience_contains(base, recipient)) return;\n");
        sb_printf(o, "    uint64_t mask = 0, terms[%d];\n", n ? n : 1);
        for (int k = 0; k < n; k++) {
            if (comp_sparse(g, deniable[k])) sb_printf(o, "    if (%s) mask |= 1ull << %d;\n", presence_cell(g, "w", a, deniable[k], "row", false), k);
            else sb_printf(o, "    mask |= 1ull << %d;\n", k);
        }
        sb_printf(o, "    uint64_t sum = tide_client_entity_hash%d(w, e, loc, mask, hs, terms);\n", a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "    if (((mask >> %d) & 1u) && !tide_audience_contains(tide_visibility_denied_at(w, &policy, has_policy, base, %du), recipient))\n"
                         "        sum -= terms[%d];\n", k, (unsigned)deniable[k], k);
        }
        sb_printf(o, "    tide_client_seen_push(seen, e, %du, row, tide_client_mix(sum));\n}\n\n", a);
    }
    // The rows to look at where only some are (`bits`, a bit a row for each archetype): the first at or after `from`,
    // or `end`
    sb_put(o, "TIDE_HELPER uint32_t tide_client_bit_from(const uint64_t *bits, uint32_t from, const uint32_t end)\n{\n"
              "    while (from < end) {\n"
              "        const uint64_t word = bits[from >> 6] >> (from & 63u);\n"
              "        if (word) {\n            from += (uint32_t)__builtin_ctzll(word);\n            return from < end ? from : end;\n        }\n"
              "        if ((from | 63u) == UINT32_MAX) return end;\n"
              "        from = (from | 63u) + 1u;\n    }\n"
              "    return end;\n}\n\n");
    sb_printf(o, "TIDE_HELPER void tide_client_see(const tide_world *w, const tide_player_id recipient, %shs, tide_client_seen_list *seen,\n"
                 "                                 uint64_t *const *bits)\n{\n"
                 "    tide_visibility_walk walk = {0};\n    tide_map_view_none(&walk.groups);\n", hasher_type);
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (bits && (row = tide_client_bit_from(bits[%d], row, w->%s.count)) >= w->%s.count) break;\n", a, arch_name(g, a), arch_name(g, a));
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        tide_client_see_row%d(w, recipient, row, hs, &walk, seen);\n    }\n", a);
    }
    sb_put(o, "}\n\n");
    if (heapy) {
        // The world the tick's check last hashed, with what it kept of each row (client_spans_codegen.inc): who may
        // have it and the terms of its sum, read rather than worked out again. A row with nothing kept is read.
        sb_put(o, "TIDE_HELPER void tide_client_see_kept(const tide_world *w, const tide_player_id recipient, struct tide_client_kept *kept,\n"
                  "                                      tide_client_hasher *hs, tide_client_seen_list *seen, uint64_t *const *bits)\n{\n"
                  "    tide_visibility_walk walk = {0};\n    tide_map_view_none(&walk.groups);\n");
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a)) continue;
            const int n = client_deniable(g, a, deniable);
            int m = 0;
            for (int c = 0; c < prog->components.count; c++) m += arch_stores(g, a, c);
            if (!m) m = 1;
            sb_printf(o, "    {\n        const tide_client_rows *r = &kept->rows[%d];\n", a);
            sb_printf(o, "        for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
            sb_printf(o, "            if (bits && (row = tide_client_bit_from(bits[%d], row, w->%s.count)) >= w->%s.count) break;\n", a, arch_name(g, a), arch_name(g, a));
            sb_printf(o, "            if (tide_row_gone(%s)) continue;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
            sb_printf(o, "            if (row >= r->room || r->parts != %du || (r->flags[row] & (TIDE_CLIENT_HASHED | TIDE_CLIENT_SEEN)) != (TIDE_CLIENT_HASHED | TIDE_CLIENT_SEEN)) {\n"
                         "                tide_client_see_row%d(w, recipient, row, hs, &walk, seen);\n                continue;\n            }\n", (unsigned)m, a);
            sb_put(o, "            if (!tide_audience_contains(r->audience[row], recipient)) continue;\n"
                      "            uint64_t sum = r->has[row * 2u + 1u];\n");
            for (int k = 0; k < n; k++) {
                sb_printf(o, "            if (((r->has[row * 2u] >> %d) & 1u) && !tide_audience_contains(r->denied[(size_t)row * %du + %du], recipient))\n"
                             "                sum -= r->terms[(size_t)row * %du + %du];\n",
                          k, (unsigned)n, (unsigned)k, (unsigned)m, (unsigned)client_part_of(g, a, deniable[k]));
            }
            sb_printf(o, "            tide_client_seen_push(seen, *%s, %du, row, tide_client_mix(sum));\n        }\n    }\n",
                      arch_cell(g, "w", a, 0, "tide_entity", "row", false), a);
        }
        sb_put(o, "}\n\n");
    }
    sb_put(o, "TIDE_HELPER uint32_t tide_client_seen_slot(const tide_entity e, const uint32_t slots)\n{\n"
              "    return (uint32_t)tide_client_mix(e.birth * 0x9e3779b97f4a7c15ull ^ e.lineage) & (slots - 1u);\n}\n\n");

    // The server's side: the plan, `now` and `was`. Every row of both worlds is looked at (`walk`, or nothing better is
    // known), or only the rows the ticks' checks wrote down as changed between the two (`found`): the lists of what
    // the recipient may have then leave out rows that are the same in both, which no plan names, so the plan, `now`
    // and `was` are the same bytes either way
    const bool masked = heapy && client_changes_masked(prog);
    const int archetypes = prog->archetypes.count > 0 ? prog->archetypes.count : 1; // (C has no empty arrays)
    if (heapy) {
        // Where two worlds are in the journal (by their hashes, so by what they hold: not a world's address or its
        // tick, which a world changed outside a tick would still have), the nodes between them: every row that differs
        // for anyone is in those after the earlier, up to the later. Not where a check between them wrote none (one of
        // a run that each wrote most of the world's rows), or they're more in all than it's worth. The world the match ticks, at the tick its check last kept its
        // rows at (`current`), is the last node, as it is the world those rows are trusted to be (tide_client_see_kept)
        sb_put(o, "#ifdef TIDE_CLIENT_COUNTS\n#define TIDE_CLIENT_WALKED(i) __atomic_add_fetch(&tide_client_counts[i], 1u, __ATOMIC_RELAXED)\n"
                  "#else\n#define TIDE_CLIENT_WALKED(i) ((void)0)\n#endif\n");
        sb_put(o, "TIDE_HELPER bool tide_client_journal_between(const tide_client_journal *j, const tide_world *w, const tide_world *b,\n"
                  "                                             const bool current, const uint64_t rows, uint32_t *after, uint32_t *upto)\n{\n"
                  "    if (!j->node_count) return false;\n"
                  "    int64_t at = current ? (int64_t)j->node_count - 1 : -1, base = -1;\n"
                  "    if (at < 0) {\n"
                  "        const uint64_t now = tide_world_hash(w);\n"
                  "        for (uint32_t i = j->node_count; i-- > 0 && at < 0;) if (j->nodes[i].world == now) at = i;\n    }\n"
                  "    if (at < 0) {\n        TIDE_CLIENT_WALKED(28);\n        return false;\n    }\n"
                  "    const uint64_t was = tide_world_hash(b);\n"
                  "    for (uint32_t i = j->node_count; i-- > 0 && base < 0;) if (j->nodes[i].world == was) base = i;\n"
                  "    if (base < 0) {\n        TIDE_CLIENT_WALKED(29);\n        return false;\n    }\n"
                  "    *after = (uint32_t)(at < base ? at : base);\n    *upto = (uint32_t)(at < base ? base : at);\n"
                  "    uint64_t all = 0;\n"
                  "    for (uint32_t i = *after + 1u; i <= *upto; i++) {\n"
                  "        if (j->nodes[i].all) all = UINT64_MAX;\n        else if (all != UINT64_MAX) all += j->nodes[i].count;\n    }\n"
                  "    if (all > 8u * rows + 4096u) {\n        TIDE_CLIENT_WALKED(30);\n        return false;\n    }\n"
                  "    return true;\n}\n\n");
        sb_printf(o, "// ...and a bit for each of their rows that could be another for `recipient` (`bits`, of `rows` rows an archetype)\n"
                     "TIDE_HELPER void tide_client_journal_mark(const tide_client_journal *j, const uint32_t after, const uint32_t upto,\n"
                     "                                          const tide_player_id recipient, uint64_t *const *bits, const uint32_t *rows)\n{\n"
                     "    (void)recipient;\n"
                     "    const tide_client_change *c = j->items + j->nodes[after + 1u].from;\n"
                     "    const tide_client_change *const end = j->items + j->nodes[upto].from + j->nodes[upto].count;\n"
                     "    for (; c < end; c++) {\n"
                     "        if (c->archetype >= %du || c->row >= rows[c->archetype]%s) continue;\n"
                     "        bits[c->archetype][c->row >> 6] |= 1ull << (c->row & 63u);\n    }\n}\n\n",
                  archetypes, masked ? " || !tide_audience_contains(c->who, recipient)" : "");
    }
    sb_put(o, "static bool tide_client_changes_work(const void *world, const void *before, const void *lineage, const tide_player_id recipient,\n"
              "                                     tide_writer *plan_out, tide_writer *now_out, tide_writer *was_out, uint32_t *changed_size,\n"
              "                                     const bool walk, bool *found)\n{\n"
              "    const tide_world *w = world, *b = before;\n    (void)lineage;\n    (void)walk;\n    *found = false;\n"
              "    if (w->tide_visibility_state.client || b->tide_visibility_state.client) return false;\n"
              "    tide_client_seen_list ln = {0}, lb = {0};\n");
    if (heapy) {
        sb_printf(o, "    // Both worlds are one match's, sharing their pages: one memo, the match's own (the world it ticks), read and\n"
                     "    // not written, so the server can read several players' repairs at once (each on its own thread)\n"
                     "    const tide_client_memo *memo = tide_client_memo_share(lineage);\n"
                     "    tide_client_hasher hasher = {.out = {.grows = true}, .shared = memo, .reading = true};\n    tide_client_hasher *hs = &hasher;\n"
                     "    // The rows to look at, where the match's checks wrote down what changed between the two worlds\n"
                     "    uint64_t *bits[%d] = {0}, *marks = NULL;\n    uint32_t after = 0, upto = 0;\n"
                     "    const uint32_t rows[%d] = {", archetypes, archetypes);
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a)) sb_printf(o, "%s0u", a ? ", " : "");
            else sb_printf(o, "%sw->%s.count > b->%s.count ? w->%s.count : b->%s.count", a ? ", " : "", arch_name(g, a), arch_name(g, a),
                           arch_name(g, a), arch_name(g, a));
        }
        if (!prog->archetypes.count) sb_put(o, "0u");
        sb_printf(o, "};\n    uint64_t every = 0;\n    size_t words = 0;\n"
                     "    for (uint32_t a = 0; a < %du; a++) {\n        every += rows[a];\n        words += (rows[a] + 63u) >> 6;\n    }\n"
                     "    // (The world the match ticks, as this tick's check left it: below)\n"
                     "    const bool current = w == lineage && memo && memo->world == lineage && memo->rows && memo->rows->any\n"
                     "        && memo->rows->tick == (uint32_t)w->Time.tick;\n"
                     "    if (!walk && memo && memo->world == lineage && memo->rows\n"
                     "        && tide_client_journal_between(&memo->rows->journal, w, b, current, every, &after, &upto)) {\n"
                     "        marks = calloc(words ? words : 1u, sizeof *marks);\n        if (!marks) tide_out_of_memory();\n"
                     "        words = 0;\n"
                     "        for (uint32_t a = 0; a < %du; a++) {\n            bits[a] = marks + words;\n            words += (rows[a] + 63u) >> 6;\n        }\n"
                     "        if (upto > after) tide_client_journal_mark(&memo->rows->journal, after, upto, recipient, bits, rows);\n"
                     "        *found = true;\n"
                     "#if defined(TIDE_CLIENT_COUNTS) || defined(TIDE_CLIENT_CHANGES_STATS)\n"
                     "        uint64_t looked = 0;\n        for (size_t i = 0; i < words; i++) looked += (uint64_t)__builtin_popcountll(marks[i]);\n#endif\n"
                     "#ifdef TIDE_CLIENT_COUNTS\n        __atomic_add_fetch(&tide_client_counts[26], looked, __ATOMIC_RELAXED);\n#endif\n"
                     "#ifdef TIDE_CLIENT_CHANGES_STATS\n        __atomic_add_fetch(&tide_client_changes_stats[2], looked, __ATOMIC_RELAXED);\n#endif\n"
                     "    }\n"
                     "    uint64_t *const *const some = *found ? bits : NULL;\n",
                  archetypes, archetypes);
        sb_put(o, "    tide_text_use((tide_heaps *)&b->heap, NULL);\n"
                  "    hs->heaps = (tide_heaps *)(uintptr_t)&b->heap;\n    tide_client_see(b, recipient, hs, &lb, some);\n"
                  "    tide_text_use((tide_heaps *)&w->heap, NULL);\n"
                  "    hs->heaps = (tide_heaps *)(uintptr_t)&w->heap;\n"
                  "    // The world the match ticks, as this tick's check left it: what it kept of each row. Kept at another tick\n"
                  "    // (no check since the world last ticked), what it kept is of another world, and each row is read\n"
                  "    if (current) tide_client_see_kept(w, recipient, memo->rows, hs, &ln, some);\n"
                  "    else tide_client_see(w, recipient, hs, &ln, some);\n"
                  "    free(marks);\n"
                  "    tide_client_memo_unshare(memo);\n    free(hs->out.data);\n    free(hs->reach);\n");
    } else {
        sb_put(o, "    tide_writer scratch = {.grows = true};\n"
                  "    tide_text_use((tide_heaps *)&b->heap, NULL);\n    tide_client_see(b, recipient, &scratch, &lb, NULL);\n"
                  "    tide_text_use((tide_heaps *)&w->heap, NULL);\n    tide_client_see(w, recipient, &scratch, &ln, NULL);\n"
                  "    free(scratch.data);\n");
    }
    sb_put(o, "    // Each of the world's against the one it was: 0 the same, 1 another value of it, 2 new to the recipient\n"
              "    uint32_t slots = 16u;\n    while (slots < lb.count * 2u) slots *= 2u;\n"
              "    uint32_t *table = calloc(slots, sizeof *table);\n"
              "    uint8_t *left = calloc(lb.count ? lb.count : 1u, 1), *kind = calloc(ln.count ? ln.count : 1u, 1);\n"
              "    uint32_t *match = calloc(ln.count ? ln.count : 1u, sizeof *match);\n"
              "    if (!table || !left || !kind || !match) tide_out_of_memory();\n"
              "    for (uint32_t i = 0; i < lb.count; i++) {\n"
              "        uint32_t s = tide_client_seen_slot(lb.items[i].e, slots);\n"
              "        while (table[s]) s = (s + 1u) & (slots - 1u);\n        table[s] = i + 1u;\n    }\n"
              "    uint32_t records = 0, gone = lb.count;\n"
              "    for (uint32_t i = 0; i < ln.count; i++) {\n"
              "        const tide_client_seen *n = &ln.items[i];\n        uint32_t found = 0;\n"
              "        for (uint32_t s = tide_client_seen_slot(n->e, slots); table[s]; s = (s + 1u) & (slots - 1u)) {\n"
              "            if (tide_entity_equal(lb.items[table[s] - 1u].e, n->e)) { found = table[s]; break; }\n        }\n"
              "        if (!found) { kind[i] = 2u; records++; continue; }\n"
              "        const tide_client_seen *h = &lb.items[found - 1u];\n"
              "        left[found - 1u] = 1u;\n        gone--;\n        match[i] = found - 1u;\n"
              "        if (h->archetype != n->archetype || h->hash != n->hash) { kind[i] = 1u; records++; }\n    }\n"
              "    // Into the caller's writers, from their starts, in the room they have\n"
              "    tide_writer plan = *plan_out, nw = *now_out, ws = *was_out;\n"
              "    plan.size = nw.size = ws.size = 0;\n    plan.overflow = nw.overflow = ws.overflow = false;\n"
              "    plan.grows = nw.grows = ws.grows = true;\n"
              "    tide_write_u32(&plan, 0x32445654u);\n    tide_write_varint(&plan, records);\n"
              "    for (uint32_t i = 0; i < ln.count; i++) {\n        if (!kind[i]) continue;\n"
              "        tide_write_u8(&plan, kind[i] == 1u ? 1u : 0u);\n        tide_write_varint(&plan, ln.items[i].archetype);\n"
              "        tide_write_u64(&plan, ln.items[i].e.birth);\n        tide_write_u64(&plan, ln.items[i].e.lineage);\n    }\n"
              "    tide_write_varint(&plan, gone);\n"
              "    for (uint32_t i = 0; i < lb.count; i++) {\n        if (left[i]) continue;\n"
              "        tide_write_u64(&plan, lb.items[i].e.birth);\n        tide_write_u64(&plan, lb.items[i].e.lineage);\n    }\n"
              "    uint32_t changed = 0;\n"
              "    tide_client_put_shared(&nw, w, recipient);\n"
              "    for (uint32_t i = 0; i < ln.count; i++) {\n        if (!kind[i]) continue;\n        const uint32_t at = nw.size;\n"
              "        tide_client_put_record(&nw, w, ln.items[i].e, ln.items[i].archetype, ln.items[i].row, recipient);\n"
              "        if (kind[i] == 1u) changed += nw.size - at;\n    }\n");
    if (prog->hierarchy[0]) sb_put(o, "    tide_client_put_hierarchy(&nw, w, recipient);\n");
    sb_put(o, ""
              "    tide_text_use((tide_heaps *)&b->heap, NULL);\n"
              "    tide_client_put_shared(&ws, b, recipient);\n"
              "    for (uint32_t i = 0; i < ln.count; i++) {\n        if (kind[i] != 1u) continue;\n        const tide_client_seen *h = &lb.items[match[i]];\n"
              "        tide_client_put_record(&ws, b, h->e, h->archetype, h->row, recipient);\n    }\n");
    if (prog->hierarchy[0]) sb_put(o, "    tide_client_put_hierarchy(&ws, b, recipient);\n");
    sb_put(o, ""
              "    tide_text_use((tide_heaps *)&w->heap, NULL);\n"
              "    free(table);\n    free(left);\n    free(kind);\n    free(match);\n    free(ln.items);\n    free(lb.items);\n"
              "    *plan_out = plan;\n    *now_out = nw;\n    *was_out = ws;\n"
              "    if (plan.overflow || nw.overflow || ws.overflow) tide_out_of_memory();\n"
              "    if (changed_size) *changed_size = changed;\n    return true;\n}\n\n");
    // TIDE_CLIENT_SPANS_CHECK works a repair from the rows written down out again from every row, and stops the program
    // where a byte differs, as it does a tick's hashes
    sb_put(o, "static bool tide_game_client_changes(const void *world, const void *before, const void *lineage, const tide_player_id recipient,\n"
              "                                     tide_writer *plan, tide_writer *now, tide_writer *was, uint32_t *changed_size)\n{\n"
              "    bool found = false;\n"
              "    const bool made = tide_client_changes_work(world, before, lineage, recipient, plan, now, was, changed_size, false, &found);\n"
              "#ifdef TIDE_CLIENT_COUNTS\n    if (made) __atomic_add_fetch(&tide_client_counts[found ? 24 : 25], 1u, __ATOMIC_RELAXED);\n#endif\n"
              "#ifdef TIDE_CLIENT_CHANGES_STATS\n    if (made) __atomic_add_fetch(&tide_client_changes_stats[found ? 0 : 1], 1u, __ATOMIC_RELAXED);\n#endif\n"
              "#ifdef TIDE_CLIENT_SPANS_CHECK\n"
              "    if (made && found) {\n"
              "        uint32_t changed_all = 0;\n        tide_writer plan_all = {0}, now_all = {0}, was_all = {0};\n        bool again = false;\n"
              "        const bool all = tide_client_changes_work(world, before, lineage, recipient, &plan_all, &now_all, &was_all, &changed_all, true,\n"
              "                                                  &again);\n"
              "        if (!all || plan_all.size != plan->size || memcmp(plan_all.data, plan->data, plan->size) != 0 || now_all.size != now->size\n"
              "            || memcmp(now_all.data, now->data, now->size) != 0 || was_all.size != was->size\n"
              "            || memcmp(was_all.data, was->data, was->size) != 0 || (changed_size && changed_all != *changed_size)) {\n"
              "            fprintf(stderr, \"tide: player %u's repair from the rows written down isn't the one from every row\\n\", recipient.id);\n"
              "            abort();\n        }\n"
              "        free(plan_all.data);\n        free(now_all.data);\n        free(was_all.data);\n    }\n"
              "#endif\n"
              "    return made;\n}\n\n");

    // The recipient's side: `was` again, from the world it confirmed
    sb_put(o, "static uint8_t *tide_game_client_changes_base(const void *world, const tide_player_id recipient, const uint8_t *plan,\n"
              "                                             const uint32_t plan_size, uint32_t *size)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heaps *)&w->heap, NULL);\n"
              "    tide_reader p = {plan, plan_size, 0, false};\n"
              "    if (tide_read_u32(&p) != 0x32445654u) return NULL;\n"
              "    const uint32_t records = tide_read_varint(&p);\n"
              "    if (p.failed || records > (p.size - p.at) / 18u) return NULL;\n"
              "    tide_writer ws = {.grows = true};\n    tide_client_put_shared(&ws, w, recipient);\n"
              "    for (uint32_t i = 0; i < records; i++) {\n"
              "        const uint8_t had = tide_read_u8(&p);\n        (void)tide_read_varint(&p);\n"
              "        tide_entity e;\n        e.birth = tide_read_u64(&p);\n        e.lineage = tide_read_u64(&p);\n"
              "        if (p.failed || had > 1u) { free(ws.data); return NULL; }\n        if (!had) continue;\n"
              "        const tide_location loc = tide_entity_location(&w->entities, e);\n"
              "        if (!tide_entity_alive(&w->entities, e) || loc.archetype == TIDE_ARCHETYPE_NONE || !tide_visibility_entity(w, e, recipient)) {\n"
              "            free(ws.data);\n            return NULL;\n        }\n"
              "        tide_client_put_record(&ws, w, e, loc.archetype, loc.row, recipient);\n    }\n");
    if (prog->hierarchy[0]) sb_put(o, "    tide_client_put_hierarchy(&ws, w, recipient);\n");
    sb_put(o, ""
              "    if (ws.overflow) tide_out_of_memory();\n    *size = ws.size;\n"
              "    if (!ws.data) ws.data = calloc(1, 1);\n    return ws.data;\n}\n\n");

    // Taking an entity out of a recipient's world with nothing happening to it: what it held in the heap goes
    sb_put(o, "TIDE_HELPER void tide_client_drop(tide_world *w, const tide_entity e)\n{\n"
              "    const tide_location loc = tide_entity_location(&w->entities, e);\n    switch (loc.archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    case %d: {\n", a);
        for (int c = 0; c < prog->components.count; c++) {
            const decl *comp = prog->components.items[c];
            if (!arch_stores(g, a, c) || !decl_has_text(comp)) continue;
            if (comp_sparse(g, c)) sb_printf(o, "        if (%s)", presence_cell(g, "w", a, c, "loc.row", false));
            else sb_put(o, "       ");
            sb_printf(o, " {\n            %s tide_old;\n            %s;\n", type_cname(comp), comp_take(g, "w", a, c, "loc.row", "tide_old"));
            gen_client_release(g, comp, "tide_old", "            ");
            sb_put(o, "        }\n");
        }
        sb_printf(o, "        tide_table_remove(&w->%s, %s, loc.row, &w->entities, %d);\n        break;\n    }\n", arch_name(g, a), arch_columns(g, a), a);
    }
    sb_put(o, "    default: break;\n    }\n    (void)tide_entity_erase(&w->entities, e);\n}\n\n");

    // ...and its world, a copy of the one it confirmed, becoming the world the server says it has now
    sb_put(o, "static bool tide_game_apply_client_changes(void *world, const uint8_t *plan, const uint32_t plan_size, const uint8_t *now,\n"
              "                                           const uint32_t now_size, const uint8_t *was, const uint32_t was_size)\n{\n"
              "    tide_world *next = world;\n    if (!next->tide_visibility_state.client) return false;\n"
              "    tide_text_use(&next->heap, NULL);\n    uint64_t budget = SIZE_MAX;\n"
              "    tide_reader in = {now, now_size, 0, false}, old = {was, was_size, 0, false}, p = {plan, plan_size, 0, false};\n"
              "    if (tide_read_u32(&in) != (uint32_t)next->tide_visibility_state.client || tide_read_u32(&old) != (uint32_t)next->tide_visibility_state.client) return false;\n"
              "    const uint64_t clock = tide_read_u64(&in);\n    if (!clock || tide_read_u64(&old) != next->tide_identity_clock) return false;\n"
              "    next->tide_identity_clock = clock;\n"
              "    for (uint32_t i = 0; i < TIDE_GAME_PLAYERS; i++) {\n        next->tide_lifecycle_occurrences[i] = tide_read_u64(&in);\n        (void)tide_read_u64(&old);\n    }\n"
              "    next->tide_main_occurrence = tide_read_u64(&in);\n    (void)tide_read_u64(&old);\n"
              "    if (in.failed || old.failed) return false;\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        const int id = client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d});
        const char *name = type_cname(d);
        sb_printf(o, "    {\n        const uint32_t from = in.at, had_from = old.at;\n"
                     "        if (!tide_client_skip%d(&in, 0) || !tide_client_skip%d(&old, 0)) goto invalid;\n"
                     "        if (in.at - from != old.at - had_from || memcmp(now + from, was + had_from, in.at - from) != 0) {\n", id, id);
        char place[256];
        snprintf(place, sizeof place, "next->%s", name);
        if (decl_has_text(d)) gen_client_release(g, d, place, "            ");
        sb_printf(o, "            memset(&next->%s, 0, sizeof next->%s);\n"
                     "            tide_reader one = {now, in.at, from, false};\n"
                     "            if (!tide_client_get%d(&one, &next->%s, %s, 0, &budget) || one.at != in.at) goto invalid;\n"
                     "        }\n    }\n", name, name, id, name, decl_where(d));
    }
    if (prog->input) {
        const int input = client_codec_id(&codec, (type){TY_INPUT, prog->input});
        sb_printf(o, "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n"
                     "        if (!tide_client_get%d(&in, &next->inputs[slot], TIDE_IN_MATCH, 0, &budget)\n"
                     "            || !tide_client_get%d(&in, &next->previous_inputs[slot], TIDE_IN_MATCH, 0, &budget)) goto invalid;\n    }\n",
                  input, input);
    }
    sb_put(o, "    if (tide_read_u32(&p) != 0x32445654u) goto invalid;\n"
              "    const uint32_t records = tide_read_varint(&p);\n"
              "    if (p.failed || records > (p.size - p.at) / 18u) goto invalid;\n"
              "    for (uint32_t i = 0; i < records; i++) {\n"
              "        const uint8_t had = tide_read_u8(&p);\n        const uint32_t archetype = tide_read_varint(&p);\n"
              "        tide_entity e;\n        e.birth = tide_read_u64(&p);\n        e.lineage = tide_read_u64(&p);\n"
              "        if (p.failed || had > 1u) goto invalid;\n"
              "        // Its old row goes: the one it had, or one this machine's own ticks made that the server hid from it\n"
              "        if (tide_entity_alive(&next->entities, e)) tide_client_drop(next, e);\n"
              "        else if (had) goto invalid;\n"
              "        switch (archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "        case %d: {\n", a);
        gen_client_take_row(g, &codec, a);
        sb_put(o, "            break;\n        }\n");
    }
    sb_printf(o, "        default: goto invalid;\n        }\n    }\n"
                 "    if (in.failed%s) goto invalid;\n"
                 "    const uint32_t hierarchy_from = in.at; // Its links and anchors, taken once the entities are all there\n"
                 "    (void)hierarchy_from;\n", prog->hierarchy[0] ? "" : " || in.at != in.size");
    sb_put(o, ""
              "    const uint32_t gone = tide_read_varint(&p);\n"
              "    if (p.failed || gone > (p.size - p.at) / 16u) goto invalid;\n"
              "    for (uint32_t i = 0; i < gone; i++) {\n"
              "        tide_entity e;\n        e.birth = tide_read_u64(&p);\n        e.lineage = tide_read_u64(&p);\n"
              "        if (p.failed || !tide_entity_alive(&next->entities, e)) goto invalid;\n"
              "        tide_client_drop(next, e);\n    }\n"
              "    if (p.failed || p.at != p.size) goto invalid;\n"
              "    // What this machine's own ticks made that the server hides from it goes, as it does when a whole\n"
              "    // projection loads: the world is what the server says it has, and nothing else. A scene's members first, while\n"
              "    // their scene's own entity still says whether they're hidden; scenes' own entities after, as a scene unloads\n");
    for (int pass = 0; pass < 2; pass++) {
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a) || arch_scene_root(g, a) != (pass == 1)) continue;
            sb_printf(o, "    for (uint32_t row = next->%s.count; row-- > 0;) {\n        if (row >= next->%s.count) continue;\n",
                      arch_name(g, a), arch_name(g, a));
            sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "next", a, 0, "tide_entity", "row", false));
            sb_put(o, "        if (!tide_row_gone(&e) && tide_visibility_client_hidden(next, e)) tide_client_drop(next, e);\n    }\n");
        }
    }
    if (prog->hierarchy[0]) {
        sb_put(o, "    {\n        tide_reader links = {now, now_size, hierarchy_from, false};\n"
                  "        if (!tide_client_take_hierarchy(next, &links) || links.at != links.size || !tide_client_hierarchy_valid(next)) goto invalid;\n"
                  "    }\n");
    }
    sb_put(o, "    tide_heaps_flush(&next->heap);\n");
    if (sharded(g)) sb_put(o, "    if (!tide_shard_words_valid(next)) goto invalid;\n");
    sb_put(o, "    tide_text_use(&next->heap, NULL);\n    return true;\n"
              "invalid:\n    tide_text_use(&next->heap, NULL);\n    return false;\n}\n\n");
    free(deniable);
}
