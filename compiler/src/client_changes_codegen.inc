// What changed for one recipient between two worlds of a match, entity by
// entity (tide_game.client_changes), and a recipient's world taking it
// (apply_client_changes): a repair that costs what changed, not what the
// recipient has. A world of chunks sends the chunks that came into sight, not
// its projection again. Included by codegen.c after client_state_codegen.inc;
// games with transform hierarchies have none, and repair as before.
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
        sb_printf(o, "                %s value = {0};\n", type_cname(d));
        sb_printf(o, "                if (!tide_client_get%d(&in, &value, TIDE_IN_MATCH, 0, &budget)) goto invalid;\n",
                  client_codec_id(codec, (type){TY_COMPONENT, (decl *)d}));
        sb_printf(o, "                %s;\n", comp_put(g, "next", a, c, "row", "value"));
        if (comp_sparse(g, c)) sb_put(o, "                }\n");
        sb_put(o, "            }\n");
    }
}

static void gen_client_changes(gen *g)
{
    const program *prog = g->prog;
    if (!prog->visibility_state || prog->hierarchy[0]) return;
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

    // The entities a recipient may have, each with what it hashes to as permitted
    sb_put(o, "typedef struct tide_client_seen { tide_entity e; uint64_t hash; uint32_t archetype, row; } tide_client_seen;\n"
              "typedef struct tide_client_seen_list { tide_client_seen *items; uint32_t count, room; } tide_client_seen_list;\n\n"
              "TIDE_HELPER void tide_client_seen_push(tide_client_seen_list *l, const tide_entity e, const uint32_t archetype, const uint32_t row,\n"
              "                                       const uint64_t hash)\n{\n"
              "    if (l->count == l->room) {\n        l->room = l->room ? l->room * 2u : 256u;\n"
              "        l->items = realloc(l->items, (size_t)l->room * sizeof *l->items);\n        if (!l->items) tide_out_of_memory();\n    }\n"
              "    l->items[l->count++] = (tide_client_seen){e, hash, archetype, row};\n}\n\n");
    sb_printf(o, "TIDE_HELPER void tide_client_see(const tide_world *w, const tide_player_id recipient, %shs, tide_client_seen_list *seen)\n{\n",
              hasher_type);
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        if (!tide_visibility_entity(w, e, recipient)) continue;\n        uint64_t mask = 0, terms[%d];\n", n ? n : 1);
        for (int k = 0; k < n; k++) sb_printf(o, "        if (tide_client_has_component(w, e, %du)) mask |= 1ull << %d;\n", deniable[k], k);
        sb_printf(o, "        uint64_t sum = tide_client_entity_hash%d(w, e, (tide_location){%du, row}, mask, hs, terms);\n", a, a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "        if (((mask >> %d) & 1u) && !tide_visibility_component(w, e, %du, recipient)) sum -= terms[%d];\n", k, deniable[k], k);
        }
        sb_printf(o, "        tide_client_seen_push(seen, e, %du, row, tide_client_mix(sum));\n    }\n", a);
    }
    sb_put(o, "}\n\n"
              "TIDE_HELPER uint32_t tide_client_seen_slot(const tide_entity e, const uint32_t slots)\n{\n"
              "    return (uint32_t)tide_client_mix(e.birth * 0x9e3779b97f4a7c15ull ^ e.lineage) & (slots - 1u);\n}\n\n");

    // The server's side: the plan, `now` and `was`
    sb_put(o, "static uint8_t *tide_game_client_changes(const void *world, const void *before, const void *lineage, const tide_player_id recipient,\n"
              "                                        uint32_t *plan_size, uint8_t **now, uint32_t *now_size, uint8_t **was, uint32_t *was_size,\n"
              "                                        uint32_t *changed_size)\n{\n"
              "    const tide_world *w = world, *b = before;\n    (void)lineage;\n"
              "    if (w->tide_visibility_state.client || b->tide_visibility_state.client) return NULL;\n"
              "    tide_client_seen_list ln = {0}, lb = {0};\n");
    if (heapy) {
        sb_put(o, "    // Both worlds are one match's, sharing their pages: one memo, the match's own (the world it ticks)\n"
                  "    tide_client_memo *memo = tide_client_memo_for(lineage);\n"
                  "    tide_client_hasher hasher = {.out = {.grows = true}, .memo = memo};\n    tide_client_hasher *hs = &hasher;\n"
                  "    tide_text_use((tide_heaps *)&b->heap, NULL);\n"
                  "    hs->heap = tide_heaps_of((tide_heaps *)(uintptr_t)&b->heap, 0u);\n    tide_client_see(b, recipient, hs, &lb);\n"
                  "    tide_text_use((tide_heaps *)&w->heap, NULL);\n"
                  "    hs->heap = tide_heaps_of((tide_heaps *)(uintptr_t)&w->heap, 0u);\n    tide_client_see(w, recipient, hs, &ln);\n"
                  "    tide_client_memo_done(memo);\n    free(hs->out.data);\n    free(hs->reach);\n");
    } else {
        sb_put(o, "    tide_writer scratch = {.grows = true};\n"
                  "    tide_text_use((tide_heaps *)&b->heap, NULL);\n    tide_client_see(b, recipient, &scratch, &lb);\n"
                  "    tide_text_use((tide_heaps *)&w->heap, NULL);\n    tide_client_see(w, recipient, &scratch, &ln);\n"
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
              "    tide_writer plan = {.grows = true}, nw = {.grows = true}, ws = {.grows = true};\n"
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
              "        if (kind[i] == 1u) changed += nw.size - at;\n    }\n"
              "    tide_text_use((tide_heaps *)&b->heap, NULL);\n"
              "    tide_client_put_shared(&ws, b, recipient);\n"
              "    for (uint32_t i = 0; i < ln.count; i++) {\n        if (kind[i] != 1u) continue;\n        const tide_client_seen *h = &lb.items[match[i]];\n"
              "        tide_client_put_record(&ws, b, h->e, h->archetype, h->row, recipient);\n    }\n"
              "    tide_text_use((tide_heaps *)&w->heap, NULL);\n"
              "    free(table);\n    free(left);\n    free(kind);\n    free(match);\n    free(ln.items);\n    free(lb.items);\n"
              "    if (plan.overflow || nw.overflow || ws.overflow) tide_out_of_memory();\n"
              "    *plan_size = plan.size;\n    *now = nw.data;\n    *now_size = nw.size;\n    *was = ws.data;\n    *was_size = ws.size;\n"
              "    if (changed_size) *changed_size = changed;\n    return plan.data;\n}\n\n");

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
              "        tide_client_put_record(&ws, w, e, loc.archetype, loc.row, recipient);\n    }\n"
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
                     "            if (!tide_client_get%d(&one, &next->%s, TIDE_IN_MATCH, 0, &budget) || one.at != in.at) goto invalid;\n"
                     "        }\n    }\n", name, name, id, name);
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
    sb_put(o, "        default: goto invalid;\n        }\n    }\n"
              "    if (in.failed || in.at != in.size) goto invalid;\n"
              "    const uint32_t gone = tide_read_varint(&p);\n"
              "    if (p.failed || gone > (p.size - p.at) / 16u) goto invalid;\n"
              "    for (uint32_t i = 0; i < gone; i++) {\n"
              "        tide_entity e;\n        e.birth = tide_read_u64(&p);\n        e.lineage = tide_read_u64(&p);\n"
              "        if (p.failed || !tide_entity_alive(&next->entities, e)) goto invalid;\n"
              "        tide_client_drop(next, e);\n    }\n"
              "    if (p.failed || p.at != p.size) goto invalid;\n"
              "    // What this machine's own ticks made that the server hides from it goes, as it does when a whole\n"
              "    // projection loads: the world is what the server says it has, and nothing else\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = next->%s.count; row-- > 0;) {\n        if (row >= next->%s.count) continue;\n",
                  arch_name(g, a), arch_name(g, a));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "next", a, 0, "tide_entity", "row", false));
        sb_put(o, "        if (!tide_row_gone(&e) && tide_visibility_client_hidden(next, e)) tide_client_drop(next, e);\n    }\n");
    }
    sb_put(o, "    tide_heaps_flush(&next->heap);\n");
    if (sharded(g)) sb_put(o, "    if (!tide_shard_words_valid(next)) goto invalid;\n");
    sb_put(o, "    tide_text_use(&next->heap, NULL);\n    return true;\n"
              "invalid:\n    tide_text_use(&next->heap, NULL);\n    return false;\n}\n\n");
    free(deniable);
}
