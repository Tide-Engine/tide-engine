// Projection hashes that cost what changed (included by client_state_codegen.inc's user, codegen.c, before it).
//
// tide_game_client_hashes works out, for every entity, what it hashes to with
// all it has, the term of each component that can be denied, and who may have
// it and each of those. A world of thousands of chunks has thousands of rows
// that were the same last tick. So what a table chunk's rows came to is kept
// (tide_client_kept, with the world's memo), with what it was read from:
//
// - the chunk's own pages, a column each, by their hashes, which a page keeps
//   until it changes (tide/page.h): the rows' entities, scenes and components;
// - the heap pages its rows' values were read from (a grid's chunks, a list, a
//   Visibility's own policies), by place and hash;
// - the entity table's pages its rows' slots are in, which hold their snaps.
//
// A chunk whose pages all hash as they did is not read again. Who may have a
// row also goes by its group's audience, which is no part of the row: those
// are worked out again for every row when the directory of groups changed, or
// a scene's own row did (its group limits its entities), and for the rows read
// again. What's kept is only ever a shortcut to what the rows come to:
// TIDE_CLIENT_SPANS_CHECK works every hash out both ways and stops the program
// where they differ, and TIDE_CLIENT_MEMO_OFF keeps nothing.

static int client_deniable(const gen *g, int a, int *out); // client_state_codegen.inc

static bool arch_scene_root(const gen *g, const int a)
{
    for (int c = 0; c < g->prog->components.count; c++) {
        if (g->prog->components.items[c]->is_scene && arch_stores(g, a, c)) return true;
    }
    return false;
}

// The types and helpers, once, after Audience is declared
static void gen_client_span_helpers(gen *g)
{
    sb *o = &g->c;
    int archetypes = g->prog->archetypes.count;
    sb_put(o, "typedef struct tide_client_span {\n"
              "    uint64_t *lanes;       // Each column's page's hash, as its rows were last read\n"
              "    uint32_t *pages;       // The heap pages their values were read from, by place, then the entity table's pages their slots are in\n"
              "    uint64_t *page_hashes;\n"
              "    uint32_t heap_pages, slot_pages, page_room, rows;\n"
              "    bool kept;             // What the arrays hold for its rows is what they come to\n"
              "    bool fresh;            // ...worked out in this call\n"
              "} tide_client_span;\n\n"
              "typedef struct tide_client_rows {\n"
              "    Audience *audience;    // Each row's: who may have it\n"
              "    uint64_t *has;         // ...which of the components that can be denied it has, and its hash's sum with all of it\n"
              "    Audience *denied;      // ...who may have each of those\n"
              "    uint64_t *terms;       // ...and each one's term of the sum\n"
              "    uint32_t room;\n    tide_client_span *spans;\n    uint32_t span_room;\n"
              "} tide_client_rows;\n\n");
    sb_printf(o, "struct tide_client_kept {\n    tide_client_rows rows[%d];\n"
                 "    uint64_t directory; // The groups' audiences, as a hash of the pages they're kept in\n"
                 "    bool any;\n};\n\n", archetypes > 0 ? archetypes : 1);
    sb_printf(o, "TIDE_HELPER void tide_client_kept_free(struct tide_client_kept *k)\n{\n"
                 "    for (uint32_t a = 0; a < %du; a++) {\n        tide_client_rows *r = &k->rows[a];\n"
                 "        for (uint32_t i = 0; i < r->span_room; i++) {\n"
                 "            free(r->spans[i].lanes);\n            free(r->spans[i].pages);\n            free(r->spans[i].page_hashes);\n        }\n"
                 "        free(r->spans);\n        free(r->audience);\n        free(r->has);\n        free(r->denied);\n        free(r->terms);\n    }\n"
                 "    memset(k, 0, sizeof *k);\n}\n\n", archetypes > 0 ? archetypes : 1);
    sb_put(o, "// Room for a table's rows and chunks, what's kept staying where it is\n"
              "TIDE_HELPER void tide_client_rows_room(tide_client_rows *r, const uint32_t rows, const uint32_t chunks, const uint32_t denials)\n{\n"
              "    if (rows > r->room) {\n"
              "        uint32_t room = r->room ? r->room : 256u;\n        while (room < rows) room *= 2u;\n"
              "        r->audience = realloc(r->audience, (size_t)room * sizeof *r->audience);\n"
              "        r->has = realloc(r->has, (size_t)room * 2u * sizeof *r->has);\n"
              "        if (!r->audience || !r->has) tide_out_of_memory();\n"
              "        if (denials) {\n"
              "            r->denied = realloc(r->denied, (size_t)room * denials * sizeof *r->denied);\n"
              "            r->terms = realloc(r->terms, (size_t)room * denials * sizeof *r->terms);\n"
              "            if (!r->denied || !r->terms) tide_out_of_memory();\n        }\n"
              "        r->room = room;\n    }\n"
              "    if (chunks > r->span_room) {\n"
              "        uint32_t room = r->span_room ? r->span_room : 4u;\n        while (room < chunks) room *= 2u;\n"
              "        r->spans = realloc(r->spans, (size_t)room * sizeof *r->spans);\n"
              "        if (!r->spans) tide_out_of_memory();\n"
              "        memset(r->spans + r->span_room, 0, (size_t)(room - r->span_room) * sizeof *r->spans);\n"
              "        r->span_room = room;\n    }\n}\n\n");
    sb_put(o, "// Whether a table chunk's rows, and all they were read from, are as they were when what's kept of them was worked out\n"
              "TIDE_HELPER bool tide_client_span_same(const tide_client_span *s, const tide_world *w, const tide_table *t, const tide_columns *c,\n"
              "                                       const uint32_t chunk, const uint32_t rows, tide_heap *heap)\n{\n"
              "#ifdef TIDE_CLIENT_MEMO_OFF // For tests: every row read again\n"
              "    (void)s; (void)w; (void)t; (void)c; (void)chunk; (void)rows; (void)heap;\n    return false;\n#else\n"
              "    if (!s->kept || s->rows != rows) return false;\n"
              "    for (uint32_t k = 0; k < c->count; k++) {\n"
              "        if (c->sizes[k] && tide_page_hash(t->pages[chunk * c->count + k], rows * c->sizes[k]) != s->lanes[k]) return false;\n    }\n"
              "    for (uint32_t i = 0; i < s->heap_pages; i++) {\n"
              "        const uint32_t place = s->pages[i];\n"
              "        if (place >= heap->pages || !heap->page[place] || heap->page[place]->first != place\n"
              "            || tide_heap_block_hash(heap, (uint64_t)place << TIDE_HEAP_PAGE_SHIFT) != s->page_hashes[i]) return false;\n    }\n"
              "    for (uint32_t i = s->heap_pages; i < s->heap_pages + s->slot_pages; i++) {\n"
              "        if (tide_entities_slot_page_hash(&w->entities, s->pages[i]) != s->page_hashes[i]) return false;\n    }\n"
              "    return true;\n#endif\n}\n\n");
    sb_put(o, "// Numbers gathered for a table chunk: the heap pages its rows reached, or its slots' pages\n"
              "typedef struct tide_client_gathered { uint32_t *items; uint32_t count, room; } tide_client_gathered;\n\n"
              "TIDE_HELPER void tide_client_gather(tide_client_gathered *g, const uint32_t *items, const uint32_t count)\n{\n"
              "    if (g->count + count > g->room) {\n"
              "        uint32_t room = g->room ? g->room : 256u;\n        while (room < g->count + count) room *= 2u;\n"
              "        g->items = realloc(g->items, (size_t)room * sizeof *g->items);\n        if (!g->items) tide_out_of_memory();\n"
              "        g->room = room;\n    }\n"
              "    for (uint32_t i = 0; i < count; i++) {\n"
              "        if (g->count && g->items[g->count - 1u] == items[i]) continue; // One after another alike, as rows made together are\n"
              "        g->items[g->count++] = items[i];\n    }\n}\n\n"
              "TIDE_HELPER uint32_t tide_client_gathered_once(tide_client_gathered *g)\n{\n"
              "    if (g->count > 1u) qsort(g->items, g->count, sizeof *g->items, tide_client_place_order);\n"
              "    uint32_t unique = 0;\n"
              "    for (uint32_t i = 0; i < g->count; i++) if (!unique || g->items[unique - 1u] != g->items[i]) g->items[unique++] = g->items[i];\n"
              "    g->count = unique;\n    return unique;\n}\n\n");
    sb_put(o, "// Keeps what a table chunk's rows were read from, now that they're worked out\n"
              "TIDE_HELPER void tide_client_span_keep(tide_client_span *s, const tide_world *w, const tide_table *t, const tide_columns *c,\n"
              "                                       const uint32_t chunk, const uint32_t rows, tide_heap *heap, tide_client_gathered *reached,\n"
              "                                       tide_client_gathered *slots, const bool poisoned)\n{\n"
              "    if (!s->lanes) {\n        s->lanes = malloc((size_t)(c->count ? c->count : 1u) * sizeof *s->lanes);\n"
              "        if (!s->lanes) tide_out_of_memory();\n    }\n"
              "    for (uint32_t k = 0; k < c->count; k++) {\n"
              "        s->lanes[k] = c->sizes[k] ? tide_page_hash(t->pages[chunk * c->count + k], rows * c->sizes[k]) : 0u;\n    }\n"
              "    const uint32_t heap_pages = tide_client_gathered_once(reached), slot_pages = tide_client_gathered_once(slots);\n"
              "    if (heap_pages + slot_pages > s->page_room) {\n"
              "        s->page_room = heap_pages + slot_pages;\n"
              "        s->pages = realloc(s->pages, (size_t)s->page_room * sizeof *s->pages);\n"
              "        s->page_hashes = realloc(s->page_hashes, (size_t)s->page_room * sizeof *s->page_hashes);\n"
              "        if (!s->pages || !s->page_hashes) tide_out_of_memory();\n    }\n"
              "    bool whole = !poisoned;\n"
              "    for (uint32_t i = 0; i < heap_pages; i++) {\n"
              "        const uint32_t place = reached->items[i];\n"
              "        // Only a page that starts at its own place is kept by it: the pieces of a bigger one aren't read again\n"
              "        if (place >= heap->pages || !heap->page[place] || heap->page[place]->first != place) { whole = false; continue; }\n"
              "        s->pages[i] = place;\n"
              "        s->page_hashes[i] = tide_heap_block_hash(heap, (uint64_t)place << TIDE_HEAP_PAGE_SHIFT);\n    }\n"
              "    for (uint32_t i = 0; i < slot_pages; i++) {\n"
              "        s->pages[heap_pages + i] = slots->items[i];\n"
              "        s->page_hashes[heap_pages + i] = tide_entities_slot_page_hash(&w->entities, slots->items[i]);\n    }\n"
              "    s->heap_pages = heap_pages;\n    s->slot_pages = slot_pages;\n    s->rows = rows;\n"
              "    s->kept = whole;\n    s->fresh = true;\n}\n\n");
}

// The rows' part of tide_game_client_hashes for a game whose values hold anything in the heap: declares, for each
// archetype `a`, rows<a>, audience<a>, has<a> and, where some component can be denied, denied<a> and terms<a>, as
// the loops after it read them, and the input slots' audiences. In scope: w, hs, client, kept (a tide_client_kept).
static void gen_client_hash_rows(gen *g, int *deniable)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_put(o, "    // Each entity's hash and what it has: again only for the table chunks something of changed\n"
              "    tide_client_gathered reached = {0}, slots = {0};\n    bool roots_fresh = false;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        const char *table = arch_name(g, a);
        sb_printf(o, "    const uint32_t rows%d = w->%s.count;\n"
                     "    tide_client_rows_room(&kept->rows[%d], rows%d, w->%s.chunks, %du);\n"
                     "    Audience *const audience%d = kept->rows[%d].audience;\n    uint64_t *const has%d = kept->rows[%d].has;\n",
                  a, table, a, a, table, (unsigned)n, a, a, a, a);
        if (n) sb_printf(o, "    Audience *const denied%d = kept->rows[%d].denied;\n    uint64_t *const terms%d = kept->rows[%d].terms;\n", a, a, a, a);
        sb_printf(o, "    for (uint32_t chunk = 0; chunk < w->%s.chunks; chunk++) {\n"
                     "        const uint32_t first = chunk << (%s)->shift, here = tide_table_rows(&w->%s, (%s)->shift, chunk);\n"
                     "        tide_client_span *span = &kept->rows[%d].spans[chunk];\n        span->fresh = false;\n"
                     "        if (tide_client_span_same(span, w, &w->%s, %s, chunk, here, hs->heap)) continue;\n"
                     "        reached.count = slots.count = 0;\n        bool poisoned = false;\n"
                     "        for (uint32_t row = first; row < first + here; row++) {\n",
                  table, arch_columns(g, a), table, arch_columns(g, a), a, table, arch_columns(g, a));
        sb_printf(o, "            if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "            const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "            const tide_location loc = {%du, row};\n            uint64_t mask = 0;\n", a);
        for (int k = 0; k < n; k++) {
            if (comp_sparse(g, deniable[k])) sb_printf(o, "            if (%s) mask |= 1ull << %d;\n", presence_cell(g, "w", a, deniable[k], "row", false), k);
            else sb_printf(o, "            mask |= 1ull << %d;\n", k);
        }
        char terms[64];
        if (n) snprintf(terms, sizeof terms, "&terms%d[row * %du]", a, (unsigned)n);
        else snprintf(terms, sizeof terms, "NULL");
        sb_printf(o, "            has%d[row * 2u] = mask;\n            has%d[row * 2u + 1u] = tide_client_entity_hash%d(w, e, loc, mask, hs, %s);\n",
                  a, a, a, terms);
        if (arch_stores(g, a, prog->visibility->index)) {
            // Its own policies are read when who may have it is worked out: their pages are the row's too
            sb_put(o, "            {\n                Visibility policy = {0};\n"
                      "                if (tide_load_Visibility_at(w, loc, &policy) && policy.overrides.live) {\n"
                      "                    tide_client_reach_list(hs, policy.overrides.entries);\n"
                      "                    tide_client_reach_list(hs, policy.overrides.index);\n                }\n            }\n");
        }
        sb_put(o, "            poisoned |= hs->poisoned;\n"
                  "            tide_client_gather(&reached, hs->reach, hs->reached);\n"
                  "            const uint32_t slot = tide_entity_slot_index(&w->entities, e) >> TIDE_ENTITY_PAGE_SHIFT;\n"
                  "            tide_client_gather(&slots, &slot, 1u);\n        }\n");
        sb_printf(o, "        tide_client_span_keep(span, w, &w->%s, %s, chunk, here, hs->heap, &reached, &slots, poisoned);\n", table, arch_columns(g, a));
        if (arch_scene_root(g, a)) sb_put(o, "        roots_fresh = true; // A scene's own row: its group limits every entity of its scene\n");
        sb_put(o, "    }\n");
    }
    sb_put(o, "    free(reached.items);\n    free(slots.items);\n"
              "    // Who may have what: for the rows read again, and for every row when a group's audience may have changed\n"
              "    hs->reached = 0;\n    hs->floor = 0;\n    hs->poisoned = false;\n"
              "    tide_client_reach_list(hs, w->tide_visibility_state.groups.entries);\n"
              "    tide_client_reach_list(hs, w->tide_visibility_state.groups.index);\n"
              "    uint64_t directory = tide_client_mix((uint64_t)w->tide_visibility_state.groups.entries.at\n"
              "        ^ (uint64_t)w->tide_visibility_state.groups.index.at * 0x9e3779b97f4a7c15ull ^ w->tide_visibility_state.groups.live);\n"
              "    for (uint32_t i = 0; i < hs->reached; i++) {\n"
              "        directory = tide_client_mix(directory ^ tide_heap_block_hash(hs->heap, (uint64_t)hs->reach[i] << TIDE_HEAP_PAGE_SHIFT)) + hs->reach[i];\n    }\n"
              "    const bool everyone = !kept->any || hs->poisoned || directory != kept->directory || roots_fresh;\n"
              "    kept->directory = directory;\n    kept->any = true;\n"
              "    tide_visibility_walk walk = {0};\n    tide_map_view_none(&walk.groups);\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        const char *table = arch_name(g, a);
        sb_printf(o, "    for (uint32_t chunk = 0; chunk < w->%s.chunks; chunk++) {\n"
                     "        if (!everyone && !kept->rows[%d].spans[chunk].fresh) continue;\n"
                     "        const uint32_t first = chunk << (%s)->shift, here = tide_table_rows(&w->%s, (%s)->shift, chunk);\n"
                     "        for (uint32_t row = first; row < first + here; row++) {\n",
                  table, a, arch_columns(g, a), table, arch_columns(g, a));
        sb_printf(o, "            if (tide_row_gone(%s)) continue;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "            const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "            const tide_location loc = {%du, row};\n            Visibility policy = {0};\n            bool has_policy = false;\n"
                     "            audience%d[row] = !client ? tide_visibility_base_at(w, e, loc, &policy, &has_policy, &walk)\n"
                     "                : tide_visibility_client_hidden(w, e) ? (Audience){0} : TIDE_AUDIENCE_ALL;\n", a, a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "            denied%d[row * %du + %du] = client || !((has%d[row * 2u] >> %d) & 1u) ? TIDE_AUDIENCE_ALL\n"
                         "                : tide_visibility_denied_at(w, &policy, has_policy, audience%d[row], %du);\n",
                      a, (unsigned)n, (unsigned)k, a, k, a, (unsigned)deniable[k]);
        }
        sb_put(o, "        }\n    }\n");
    }
    if (!prog->input) return;
    // Each input slot's audience: every row's, which are kept, so this only reads them
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        sb_printf(o, "    for (uint32_t row = 0; row < rows%d; row++) {\n", a);
        sb_printf(o, "        if (tide_row_gone(%s)) continue;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        if (arch_stores(g, a, prog->owner->index)) {
            sb_printf(o, "        %s owner;\n        memset(&owner, 0, sizeof owner);\n        (void)tide_load_%s_at(w, (tide_location){%du, row}, &owner);\n"
                         "        const int32_t owner_index = tide_player_index(owner.player);\n",
                      type_cname(prog->owner), type_cname(prog->owner), a);
            sb_put(o, "        const uint32_t slot = owner_index >= 0 && (uint32_t)owner_index < TIDE_GAME_PLAYERS ? (uint32_t)owner_index : TIDE_GAME_PLAYERS;\n");
        } else sb_put(o, "        const uint32_t slot = TIDE_GAME_PLAYERS;\n");
        sb_printf(o, "        slot_found[slot] = true;\n        slot_audience[slot] = tide_audience_intersect(slot_audience[slot], audience%d[row]);\n", a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "        slot_audience[slot] = tide_audience_intersect(slot_audience[slot], denied%d[row * %du + %du]);\n", a, (unsigned)n, (unsigned)k);
        }
        sb_put(o, "    }\n");
    }
}
