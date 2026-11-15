// Projection hashes that cost what changed (included by codegen.c before client_state_codegen.inc).
//
// tide_game_client_hashes works out, for every entity, what it hashes to with
// all it has (a sum: a term for what says which entity it is, and one for each
// component), and who may have it and each component that can be denied. A
// world of thousands of chunks has thousands of rows that were the same last
// tick, so what each row came to is kept (tide_client_kept, with the world's
// memo), at these grains:
//
// - A table chunk's rows are kept 64 at a time, a piece (tide_client_piece).
//   A table chunk none of whose columns' pages was written since (by their
//   hashes, which a page keeps until it changes: tide/page.h) has its pieces
//   kept by their heap pages alone; where some were, the runs of those columns
//   in each piece are hashed again, and a piece whose runs, rows' heap pages
//   (a grid's chunks, a list's blocks) and entity-table slot pages (their
//   snaps) all hash as they did is not read at all. What a row read is kept by
//   its blocks (tide_client_item): a heap page that hashes otherwise has the
//   blocks of it the piece's rows read hashed again, and where they're as they
//   were (another entity's block in the page changed) the piece is passed over
//   still, the page's hash now kept; a slot's page that does has its rows'
//   snaps looked at again.
// - Otherwise each row's components are: one none of whose columns' runs in
//   the piece changed, or whose lanes (and presence) hash as they did, is not
//   hashed again, and one that holds anything in the heap (a grid) is hashed
//   again only when its lanes changed or a heap page its row's values were
//   read from changed. So a chunk whose small `Seen` changed doesn't hash its
//   4,096 blocks again. A Visibility's policies are blocks in the heap too, in
//   every archetype that has one, though nothing a recipient is sent: a policy
//   set where it is changes a page of theirs, not the Visibility's lanes. A
//   row keeps every page it read, its first too where that's the last of the
//   row before (rows made together share pages).
// - Who may have a row goes by its groups' audiences, which are no part of the
//   row: the directory of groups is compared with the copy kept of it. A piece
//   whose rows' groups (their least and most, and a bit for each of 256) take
//   in none of those that changed, with no row restricted to a group or worked
//   out again, isn't looked at; in the others a row's audience is worked out
//   again only when its own group's, its scene's or a group one of its
//   components is restricted to changed, or when the row itself changed. Every
//   row's is when a scene's root came, went, or has another Visibility. The
//   copy of the directory is entry by entry, a chunk of its list compared only
//   where its page changed.
// - What a piece's rows add to each player's sum is kept with it while none of
//   them was read again or had its audience worked out again.
//
// What's kept is only ever a shortcut to what the rows come to:
// TIDE_CLIENT_SPANS_CHECK works every hash out both ways and stops the program
// where they differ, and TIDE_CLIENT_MEMO_OFF keeps nothing.

static int client_deniable(const gen *g, int a, int *out); // client_state_codegen.inc
static bool client_codec_heapy(const program *prog, type t, int depth);

static bool arch_scene_root(const gen *g, const int a)
{
    for (int c = 0; c < g->prog->components.count; c++) {
        if (g->prog->components.items[c]->is_scene && arch_stores(g, a, c)) return true;
    }
    return false;
}

// The components archetype `a` stores, in order: their count, and each one's index into the program's (`out`)
static int arch_parts(const gen *g, const int a, int *out)
{
    int n = 0;
    for (int c = 0; c < g->prog->components.count; c++) {
        if (arch_stores(g, a, c)) out[n++] = c;
    }
    return n;
}

// For each archetype, which of its components each of its columns is a lane (or the presence byte) of, by its place
// among the archetype's components; -1 for the entity's and the scene's columns: tide_client_part_of<a>
static void gen_client_part_tables(gen *g)
{
    sb *o = &g->c;
    const program *prog = g->prog;
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int columns = arch_column_count(g, a);
        int *part = malloc(((size_t)columns + 1u) * sizeof *part);
        if (!part) abort();
        for (int k = 0; k < columns; k++) part[k] = -1;
        int t = 0;
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const int from = arch_column(g, a, c), lanes = comp_lanes(prog->components.items[c]);
            for (int k = from; k < from + lanes && k < columns; k++) part[k] = t;
            if (comp_sparse(g, c)) part[arch_presence(g, a, c)] = t;
            t++;
        }
        sb_printf(o, "static const int32_t tide_client_part_of%d[%d] = {", a, columns > 0 ? columns : 1);
        for (int k = 0; k < columns; k++) sb_printf(o, "%s%d", k ? ", " : "", part[k]);
        if (!columns) sb_put(o, "-1");
        sb_put(o, "};\n");
        free(part);
    }
    sb_put(o, "\n");
}

// The types and helpers, once, after Audience is declared
static void gen_client_span_helpers(gen *g)
{
    sb *o = &g->c;
    const program *prog = g->prog;
    const int archetypes = prog->archetypes.count;
    const int groups = prog->visibility_state->fields.items[1].type.decl->index;
    sb_put(o, "// Flags of a kept row\n"
              "#define TIDE_CLIENT_HASHED 1u    // Its terms are what its components came to\n"
              "#define TIDE_CLIENT_SEEN 2u      // Its audience is what its groups came to\n"
              "#define TIDE_CLIENT_BY_GROUP 4u  // ...and a component of it is restricted to a group\n\n"
              "// A table chunk's rows are kept this many at a time\n"
              "#define TIDE_CLIENT_PIECE_SHIFT 6u\n#define TIDE_CLIENT_PIECE (1u << TIDE_CLIENT_PIECE_SHIFT)\n\n"
              "typedef struct tide_client_piece {\n"
              "    uint64_t *lanes;       // Each column's run of its rows, hashed, as they were last read\n"
              "    // The heap pages its rows' values were read from, by place (its heap and its first place there:\n"
              "    // tide_client_place), then its slots' pages\n"
              "    uint64_t *pages;\n"
              "    uint64_t *page_hashes;\n"
              "    uint32_t heap_pages, slot_pages, page_room, rows;\n"
              "    // Each row's own blocks and heap pages (tide_client_rows.page_from), with their hashes: where a page of the\n"
              "    // piece's changed, those of its blocks the rows read are hashed again before the piece is read\n"
              "    tide_client_item *row_pages;\n"
              "    uint32_t row_page_count, row_page_room;\n"
              "    // The groups its rows' audiences went by (their own and their scenes', those above 0): the least, the\n"
              "    // most, and a bit for each of 256 (tide_client_mix of its id); and whether a row of it is restricted to a\n"
              "    // group by one of its components' policies\n"
              "    int64_t low, high;\n    uint64_t bloom[4];\n    bool by_group;\n"
              "    bool kept;             // What the arrays hold for its rows is what they come to\n"
              "    bool fresh;            // ...a row of it worked out again in this call\n"
              "    // What its rows add to each player's sum (tide_client_hashes_work's routing): the players, and for each the\n"
              "    // sum of its rows' hashes as that player may have them and how many; kept while no row of it is read\n"
              "    // again or has its audience worked out again (`routed`)\n"
              "    uint32_t *route_players;\n    uint64_t *route_sums;\n    uint32_t route_count, route_room;\n    bool routed;\n"
              "} tide_client_piece;\n\n"
              "typedef struct tide_client_span {\n"
              "    uint64_t *lanes;       // Each column's page's hash, as the chunk's pieces were last read\n"
              "    uint32_t rows;         // ...with this many rows\n"
              "    bool kept;\n"
              "    tide_client_piece *pieces;\n    uint32_t piece_room;\n"
              "} tide_client_span;\n\n"
              "typedef struct tide_client_rows {\n"
              "    Audience *audience;    // Each row's: who may have it\n"
              "    uint64_t *has;         // ...which of the components that can be denied it has, and its hash's sum with all of it\n"
              "    Audience *denied;      // ...who may have each of those\n"
              "    uint64_t *terms;       // ...each component's term of the sum, `parts` a row\n"
              "    uint64_t *digests;     // ...and the hash of the lanes it was worked out from, `parts` a row\n"
              "    int64_t *groups;       // ...its own group and its scene's, as its audience was worked out\n"
              "    uint64_t *policies;    // ...its Visibility's policies, hashed (tide_client_policies_digest), 0 for none\n"
              "    uint32_t *page_from;   // ...its heap pages in its piece's row_pages; a count of UINT32_MAX: never kept\n"
              "    uint32_t *page_count;\n"
              "    uint32_t *fresh;       // ...the call something of it was last worked out in\n"
              "    uint32_t *snaps;       // ...its entity's snaps, as what says which entity it is was last hashed, and its slot\n"
              "    uint32_t *slot;\n"
              "    uint8_t *flags;\n"
              "    uint32_t room, parts, denials, was;  // (was: the table's rows the call before)\n    tide_client_span *spans;\n    uint32_t span_room;\n"
              "} tide_client_rows;\n\n");
    sb_printf(o, "struct tide_client_kept {\n    tide_client_rows rows[%d];\n"
                 "    uint64_t directory;     // The groups' audiences, as a hash of the pages they're kept in\n"
                 "    void *group_entries;    // ...and a copy of the directory's entries, as they are in its list, live and dead\n"
                 "    uint32_t group_count, group_room;\n"
                 "    uint64_t group_list;    // ...the list as it was (its handle), 0 where its chunks' hashes aren't kept\n"
                 "    uint64_t *chunk_hashes; // ...and the hash of each of its chunks' pages\n    uint32_t chunk_count, chunk_room;\n"
                 "    int64_t *changed;       // The groups whose audience changed since the call before, by id\n"
                 "    uint32_t changed_count, changed_room;\n"
                 "    uint64_t *route_sums;   // Scratch for a piece's routing: a sum and a count for each player, zero between pieces\n"
                 "    uint32_t *route_touched;\n"
                 "    uint32_t call;\n    uint32_t tick;          // The world's tick they were kept at\n    bool any;\n};\n\n",
              archetypes > 0 ? archetypes : 1);
    sb_printf(o, "TIDE_HELPER void tide_client_kept_free(struct tide_client_kept *k)\n{\n"
                 "    for (uint32_t a = 0; a < %du; a++) {\n        tide_client_rows *r = &k->rows[a];\n"
                 "        for (uint32_t i = 0; i < r->span_room; i++) {\n"
                 "            tide_client_span *s = &r->spans[i];\n"
                 "            for (uint32_t p = 0; p < s->piece_room; p++) {\n"
                 "                free(s->pieces[p].lanes);\n                free(s->pieces[p].pages);\n                free(s->pieces[p].page_hashes);\n"
                 "                free(s->pieces[p].row_pages);\n                free(s->pieces[p].route_players);\n                free(s->pieces[p].route_sums);\n            }\n"
                 "            free(s->pieces);\n            free(s->lanes);\n        }\n"
                 "        free(r->spans);\n        free(r->audience);\n        free(r->has);\n        free(r->denied);\n        free(r->terms);\n"
                 "        free(r->digests);\n        free(r->groups);\n        free(r->policies);\n        free(r->page_from);\n        free(r->page_count);\n"
                 "        free(r->fresh);\n        free(r->snaps);\n        free(r->slot);\n        free(r->flags);\n    }\n"
                 "    free(k->group_entries);\n    free(k->chunk_hashes);\n    free(k->changed);\n    free(k->route_sums);\n    free(k->route_touched);\n"
                 "    memset(k, 0, sizeof *k);\n}\n\n", archetypes > 0 ? archetypes : 1);
    sb_put(o, "TIDE_HELPER void *tide_client_grow(void *p, const size_t had, const size_t room, const size_t each)\n{\n"
              "    uint8_t *grown = realloc(p, room * each);\n    if (!grown) tide_out_of_memory();\n"
              "    memset(grown + had * each, 0, (room - had) * each);\n    return grown;\n}\n\n"
              "// Room for a table's rows and chunks, what's kept staying where it is, a new row kept as nothing\n"
              "TIDE_HELPER void tide_client_rows_room(tide_client_rows *r, const uint32_t rows, const uint32_t chunks, const uint32_t parts,\n"
              "                                       const uint32_t denials)\n{\n"
              "    r->parts = parts;\n    r->denials = denials;\n"
              "    if (rows > r->room) {\n"
              "        uint32_t room = r->room ? r->room : 256u;\n        while (room < rows) room *= 2u;\n"
              "        const size_t had = r->room;\n"
              "        r->audience = tide_client_grow(r->audience, had, room, sizeof *r->audience);\n"
              "        r->has = tide_client_grow(r->has, had * 2u, (size_t)room * 2u, sizeof *r->has);\n"
              "        if (denials) r->denied = tide_client_grow(r->denied, had * denials, (size_t)room * denials, sizeof *r->denied);\n"
              "        r->terms = tide_client_grow(r->terms, had * parts, (size_t)room * parts, sizeof *r->terms);\n"
              "        r->digests = tide_client_grow(r->digests, had * parts, (size_t)room * parts, sizeof *r->digests);\n"
              "        r->groups = tide_client_grow(r->groups, had * 2u, (size_t)room * 2u, sizeof *r->groups);\n"
              "        r->policies = tide_client_grow(r->policies, had, room, sizeof *r->policies);\n"
              "        r->page_from = tide_client_grow(r->page_from, had, room, sizeof *r->page_from);\n"
              "        r->page_count = tide_client_grow(r->page_count, had, room, sizeof *r->page_count);\n"
              "        r->fresh = tide_client_grow(r->fresh, had, room, sizeof *r->fresh);\n"
              "        r->snaps = tide_client_grow(r->snaps, had, room, sizeof *r->snaps);\n"
              "        r->slot = tide_client_grow(r->slot, had, room, sizeof *r->slot);\n"
              "        r->flags = tide_client_grow(r->flags, had, room, sizeof *r->flags);\n"
              "        r->room = room;\n    }\n"
              "    if (chunks > r->span_room) {\n"
              "        uint32_t room = r->span_room ? r->span_room : 4u;\n        while (room < chunks) room *= 2u;\n"
              "        r->spans = tide_client_grow(r->spans, r->span_room, room, sizeof *r->spans);\n"
              "        r->span_room = room;\n    }\n}\n\n");
    sb_put(o, "// A table chunk's pieces, room made for them: their count, the last one short where its rows end there\n"
              "TIDE_HELPER uint32_t tide_client_span_pieces(tide_client_span *s, const uint32_t rows)\n{\n"
              "    const uint32_t pieces = (rows + TIDE_CLIENT_PIECE - 1u) >> TIDE_CLIENT_PIECE_SHIFT;\n"
              "    if (pieces > s->piece_room) {\n"
              "        s->pieces = tide_client_grow(s->pieces, s->piece_room, pieces, sizeof *s->pieces);\n"
              "        s->piece_room = pieces;\n    }\n"
              "    return pieces;\n}\n\n"
              "// Which of a table chunk's columns were written since its pieces were last read (a flag each, in `written`):\n"
              "// false where none was. All of them where nothing's kept of the chunk, or its rows are another number\n"
              "TIDE_HELPER bool tide_client_span_written(const tide_client_span *s, const tide_table *t, const tide_columns *c, const uint32_t chunk,\n"
              "                                          const uint32_t rows, bool *written)\n{\n"
              "    bool all = !s->kept || s->rows != rows, any = false;\n"
              "#ifdef TIDE_CLIENT_MEMO_OFF // For tests: every row read again\n    all = true;\n#endif\n"
              "    for (uint32_t k = 0; k < c->count; k++) {\n"
              "        written[k] = c->sizes[k] && (all || tide_page_hash(t->pages[chunk * c->count + k], rows * c->sizes[k]) != s->lanes[k]);\n"
              "        any |= written[k];\n    }\n"
              "    return any || all;\n}\n\n"
              "// ...and, its pieces read, its columns' pages as they are now. A piece past its rows isn't kept\n"
              "TIDE_HELPER void tide_client_span_kept(tide_client_span *s, const tide_table *t, const tide_columns *c, const uint32_t chunk,\n"
              "                                       const uint32_t rows, const uint32_t pieces)\n{\n"
              "    if (!s->lanes) {\n        s->lanes = malloc((size_t)(c->count ? c->count : 1u) * sizeof *s->lanes);\n"
              "        if (!s->lanes) tide_out_of_memory();\n    }\n"
              "    for (uint32_t k = 0; k < c->count; k++) {\n"
              "        s->lanes[k] = c->sizes[k] ? tide_page_hash(t->pages[chunk * c->count + k], rows * c->sizes[k]) : 0u;\n    }\n"
              "    s->rows = rows;\n    s->kept = true;\n"
              "    for (uint32_t p = pieces; p < s->piece_room; p++) s->pieces[p].kept = s->pieces[p].routed = false;\n}\n\n"
              "// A table chunk past the table's end: nothing of it is kept\n"
              "TIDE_HELPER void tide_client_span_forget(tide_client_span *s)\n{\n"
              "    s->kept = false;\n    for (uint32_t p = 0; p < s->piece_room; p++) s->pieces[p].kept = s->pieces[p].routed = false;\n}\n\n"
              "// A column's run of rows in a table chunk, from row `at` of it, hashed\n"
              "TIDE_HELPER uint64_t tide_client_run_hash(const tide_table *t, const tide_columns *c, const uint32_t chunk, const uint32_t k,\n"
              "                                         const uint32_t at, const uint32_t rows)\n{\n"
              "    const uint8_t *data = tide_page_data(t->pages[chunk * c->count + k]);\n"
              "    return tide_hash_wide(data + (size_t)at * c->sizes[k], (size_t)rows * c->sizes[k]);\n}\n\n");
    sb_put(o, "// The hash of the heap page at a place (tide_client_place), where one starts there; else false\n"
              "TIDE_HELPER bool tide_client_place_same(tide_heaps *heaps, const uint64_t place, const uint64_t hash)\n{\n"
              "    uint32_t size;\n    uint64_t now;\n"
              "    return tide_client_place_hash(heaps, place, &size, &now) && now == hash;\n}\n\n");
    sb_put(o, "// Whether a piece's rows, and all they were read from, are as they were when what's kept of them was worked\n"
              "// out: their columns' runs (those of the columns `written`, the rest unwritten), their heap pages and their\n"
              "// slots' pages. Where they aren't, `changed` says which of the archetype's components a column of changed in\n"
              "// (`part_of`, `parts` of them), `heap_moved` whether one of their heap pages did, and `ids_moved` whether what\n"
              "// says which entity each row is might have (the entity's or the scene's column, or a slot's page: its snaps).\n"
              "// `*runs` is the written columns' runs as they are now, for tide_client_piece_keep; NULL where they weren't hashed.\n"
              "// A heap page that changed has the blocks of it the rows read hashed again (tide_client_item_same): where every one\n"
              "// is as it was, the page's hash now is kept in its place, and the piece isn't read for it\n"
              "TIDE_HELPER bool tide_client_piece_blocks_same(tide_client_piece *s, const tide_heap *h, const tide_page *page, const uint64_t place,\n"
              "                                               const uint64_t now)\n{\n"
              "    bool any = false;\n"
              "    for (uint32_t i = 0; i < s->row_page_count; i++) {\n"
              "        if (tide_client_key_place(s->row_pages[i].key) != place) continue;\n"
              "        if (!tide_client_item_same_in(h, page, now, &s->row_pages[i])) return false;\n"
              "        any = true;\n    }\n"
              "    return any;\n}\n\n");
    sb_put(o, "// ...and a slot's page that changed (an entity of it came or went) has the snaps of the piece's rows whose slots\n"
              "// are in it looked at again: where each one's are as they were kept, what says which entity it is hasn't moved\n"
              "// (its entity's column is as it was, or a run of it would have changed)\n"
              "TIDE_HELPER bool tide_client_piece_snaps_same(const tide_client_rows *r, const tide_world *w, const tide_table *t, const tide_columns *c,\n"
              "                                              const uint32_t first, const uint32_t rows, const uint32_t page)\n{\n"
              "    bool any = false;\n"
              "    for (uint32_t row = first; row < first + rows; row++) {\n"
              "        const tide_entity *e = (const tide_entity *)tide_table_get(t, c, row, 0u);\n"
              "        if (tide_row_gone(e)) continue;\n"
              "        if (!(r->flags[row] & TIDE_CLIENT_HASHED)) return false;\n"
              "        if (r->slot[row] >> TIDE_ENTITY_PAGE_SHIFT != page) continue;\n"
              "        // (The slot it was in, as its row says the same entity: still its own, with its snaps there)\n"
              "        const tide_entity_slot *slot = tide_entity_slot_of(&w->entities, r->slot[row]);\n"
              "        if (memcmp(&slot->entity, e, sizeof *e) != 0 || slot->snaps != r->snaps[row]) return false;\n"
              "        any = true;\n    }\n"
              "    return any;\n}\n\n");
    sb_put(o, "TIDE_HELPER bool tide_client_piece_same(tide_client_piece *s, const tide_client_rows *r, const tide_world *w, const tide_table *t,\n"
              "                                        const tide_columns *c, const uint32_t chunk, const uint32_t at, const uint32_t rows,\n"
              "                                        const bool *written, tide_heaps *heaps, const int32_t *part_of, const uint32_t parts,\n"
              "                                        bool *changed, bool *heap_moved, bool *ids_moved, uint64_t *run, const uint64_t **runs)\n{\n"
              "    *heap_moved = *ids_moved = true;\n    *runs = NULL;\n"
              "    for (uint32_t i = 0; i < parts; i++) changed[i] = true;\n"
              "#ifdef TIDE_CLIENT_MEMO_OFF // For tests: every row read again\n"
              "    (void)s; (void)r; (void)w; (void)t; (void)c; (void)chunk; (void)at; (void)rows; (void)written; (void)heaps; (void)part_of; (void)run;\n"
              "    return false;\n#else\n"
              "    TIDE_CLIENT_COUNT(1);\n"
              "    if (!s->kept || s->rows != rows) {\n        TIDE_CLIENT_COUNT(2);\n        return false;\n    }\n"
              "    for (uint32_t i = 0; i < parts; i++) changed[i] = false;\n"
              "    bool same = true, by_run = false;\n    *ids_moved = false;\n    (void)by_run;\n"
              "    for (uint32_t k = 0; written && k < c->count; k++) {\n"
              "        if (!written[k]) continue;\n"
              "        run[k] = tide_client_run_hash(t, c, chunk, k, at, rows);\n        TIDE_CLIENT_COUNT(17);\n        TIDE_CLIENT_COUNT_BY(18, rows * c->sizes[k]);\n"
              "        if (run[k] == s->lanes[k]) continue;\n"
              "        same = false;\n        by_run = true;\n"              "        if (part_of[k] >= 0) changed[part_of[k]] = true;\n"
              "        else *ids_moved = true; // The entity's or the scene's\n"
              "    }\n"
              "    if (written) *runs = run;\n"
              "    *heap_moved = false;\n"
              "    TIDE_CLIENT_COUNT_BY(15, s->heap_pages);\n    TIDE_CLIENT_COUNT_BY(16, s->slot_pages);\n"
              "    // (Its pages are in order, by heap: tide_client_piece_keep, so a heap is found once for its pages)\n"
              "    uint32_t id = UINT32_MAX;\n    tide_heap *h = NULL;\n"
              "    for (uint32_t i = 0; i < s->heap_pages; i++) {\n"
              "        const uint64_t place = s->pages[i];\n"
              "        if ((uint32_t)(place >> 32) != id) {\n            id = (uint32_t)(place >> 32);\n            h = tide_client_heap(heaps, id);\n        }\n"
              "        const uint32_t first = (uint32_t)place;\n"
              "        const tide_page *page = h && first < h->pages ? h->page[first] : NULL;\n"
              "        if (page && page->first == first) {\n"
              "            const uint64_t now = tide_client_page_hash(h, page);\n"
              "            if (now == s->page_hashes[i]) continue;\n"
              "            bool held;\n            TIDE_CLIENT_TIMED(8, held = tide_client_piece_blocks_same(s, h, page, s->pages[i], now));\n            if (held) {\n"
              "                s->page_hashes[i] = now;\n                continue;\n            }\n        }\n"
              "        *heap_moved = true;\n        same = false;\n        break;\n    }\n"
              "    for (uint32_t i = s->heap_pages; i < s->heap_pages + s->slot_pages; i++) {\n"
              "        const uint64_t now = tide_entities_slot_page_hash(&w->entities, (uint32_t)s->pages[i]);\n"
              "        if (now == s->page_hashes[i]) continue;\n"
              "        bool held = false;\n        if (!*ids_moved) TIDE_CLIENT_TIMED(9, held = tide_client_piece_snaps_same(r, w, t, c, (chunk << c->shift) + at, rows, (uint32_t)s->pages[i]));\n        if (held) {\n"
              "            s->page_hashes[i] = now;\n            continue;\n        }\n"
              "        *ids_moved = true;\n        same = false;\n        break;\n    }\n"
              "    if (!same) TIDE_CLIENT_COUNT(by_run ? 3 : *heap_moved ? 4 : 5);\n"
              "    return same;\n#endif\n}\n\n");
    sb_put(o, "// Whether a row's own blocks and heap pages are as they were (a row never kept, or that read outside the match's\n"
              "// heaps, never is): tide_client_item_same\n"
              "TIDE_HELPER bool tide_client_row_pages_same(tide_client_piece *s, const tide_client_rows *r, const uint32_t row, tide_heaps *heaps)\n{\n"
              "#ifdef TIDE_CLIENT_MEMO_OFF\n    (void)s; (void)r; (void)row; (void)heaps;\n    return false;\n#else\n"
              "    const uint32_t from = r->page_from[row], count = r->page_count[row];\n"
              "    if (!count) return true; // (Nothing of it in the heap)\n"
              "    if (count == UINT32_MAX || !s->row_pages || from > s->row_page_count || count > s->row_page_count - from) return false;\n"
              "    for (uint32_t i = from; i < from + count; i++) {\n"
              "        if (!tide_client_item_same(heaps, &s->row_pages[i])) return false;\n    }\n"
              "    return true;\n#endif\n}\n\n");
    sb_put(o, "// A component's lanes in a row (and its presence byte, or UINT32_MAX for none), hashed: what says it's as it was\n"
              "TIDE_HELPER uint64_t tide_client_lanes_digest(const tide_table *t, const tide_columns *c, const uint32_t row, const uint32_t first,\n"
              "                                             const uint32_t count, const uint32_t presence)\n{\n"
              "    uint64_t h = 0x9ae16a3b2f90404full;\n"
              "    if (presence != UINT32_MAX) h = tide_hash_more(h, tide_table_get(t, c, row, presence), 1u);\n"
              "    for (uint32_t k = first; k < first + count; k++) h = tide_hash_more(h, tide_table_get(t, c, row, k), c->sizes[k]);\n"
              "    return h;\n}\n\n");
    sb_put(o, "// What's gathered for a piece: the heap pages its rows reached (their places, as keys), its slots' pages, or its\n"
              "// rows' own blocks and pages (tide_client_item)\n"
              "typedef struct tide_client_gathered { tide_client_item *items; uint32_t count, room; } tide_client_gathered;\n\n"
              "TIDE_HELPER void tide_client_gathered_room(tide_client_gathered *g, const uint32_t count)\n{\n"
              "    if (g->count + count <= g->room) return;\n"
              "    uint32_t room = g->room ? g->room : 256u;\n    while (room < g->count + count) room *= 2u;\n"
              "    g->items = realloc(g->items, (size_t)room * sizeof *g->items);\n"
              "    if (!g->items) tide_out_of_memory();\n"
              "    g->room = room;\n}\n\n"
              "// `count` of them (`items` may be NULL where there are none), one alike the one before it dropped where that's at or\n"
              "// after `floor`: a row's own blocks keep their first where it's the row before's last (tide_client_row_keep)\n"
              "TIDE_HELPER void tide_client_gather(tide_client_gathered *g, const tide_client_item *items, const uint32_t count, const uint32_t floor)\n{\n"
              "    tide_client_gathered_room(g, count);\n"
              "    for (uint32_t i = 0; i < count; i++) {\n"
              "        if (g->count > floor && g->items[g->count - 1u].key == items[i].key) continue; // One after another alike, as rows made together are\n"
              "        g->items[g->count++] = items[i];\n    }\n}\n\n"
              "// ...numbers (a slot's page) as keys\n"
              "TIDE_HELPER void tide_client_gather_numbers(tide_client_gathered *g, const uint64_t *numbers, const uint32_t count)\n{\n"
              "    tide_client_gathered_room(g, count);\n"
              "    for (uint32_t i = 0; i < count; i++) {\n"
              "        if (g->count && g->items[g->count - 1u].key == numbers[i]) continue;\n"
              "        g->items[g->count++] = (tide_client_item){numbers[i], 0, 0};\n    }\n}\n\n"
              "// ...or the pages items are in, by their places\n"
              "TIDE_HELPER void tide_client_gather_pages(tide_client_gathered *g, const tide_client_item *items, const uint32_t count)\n{\n"
              "    tide_client_gathered_room(g, count);\n"
              "    for (uint32_t i = 0; i < count; i++) {\n"
              "        const uint64_t place = tide_client_key_place(items[i].key);\n"
              "        if (g->count && g->items[g->count - 1u].key == place) continue;\n"
              "        g->items[g->count++] = (tide_client_item){place, 0, 0};\n    }\n}\n\n"
              "// Each number gathered once, the first of each where it was (the order says nothing: they're only compared). By a\n"
              "// table on the stack for the few hundred a piece's rows reach, by sorting past that (a test may say fewer, so a\n"
              "// sort is what every piece goes by)\n"
              "#ifndef TIDE_CLIENT_ONCE_MOST\n#define TIDE_CLIENT_ONCE_MOST 512u\n#endif\n"
              "_Static_assert(TIDE_CLIENT_ONCE_MOST >= 2u && (TIDE_CLIENT_ONCE_MOST & (TIDE_CLIENT_ONCE_MOST - 1u)) == 0,\n"
              "               \"the table's slots are a power of two up to twice TIDE_CLIENT_ONCE_MOST\");\n"
              "TIDE_HELPER uint32_t tide_client_gathered_once(tide_client_gathered *g)\n{\n"
              "    if (g->count <= 1u) return g->count;\n"
              "    uint32_t unique = 0;\n"
              "    if (g->count > TIDE_CLIENT_ONCE_MOST) {\n"
              "        qsort(g->items, g->count, sizeof *g->items, tide_client_item_order);\n"
              "        for (uint32_t i = 0; i < g->count; i++) if (!unique || g->items[unique - 1u].key != g->items[i].key) g->items[unique++] = g->items[i];\n"
              "        g->count = unique;\n        return unique;\n    }\n"
              "    uint64_t seen[2u * TIDE_CLIENT_ONCE_MOST]; // Each key + 1 (a key is never all ones), 0 for an empty slot\n"
              "    uint32_t slots = 4u;\n    while (slots < 2u * g->count) slots *= 2u;\n"
              "    memset(seen, 0, (size_t)slots * sizeof *seen);\n"
              "    for (uint32_t i = 0; i < g->count; i++) {\n"
              "        const uint64_t item = g->items[i].key;\n"
              "        uint32_t s = (uint32_t)tide_client_mix(item) & (slots - 1u);\n"
              "        while (seen[s] && seen[s] != item + 1u) s = (s + 1u) & (slots - 1u);\n"
              "        if (seen[s]) continue;\n"
              "        seen[s] = item + 1u;\n        g->items[unique++] = g->items[i];\n    }\n"
              "    g->count = unique;\n    return unique;\n}\n\n");
    sb_put(o, "// Keeps a row's own blocks and heap pages (hs->reach, or its old ones) in `store`, where the piece's row pages go\n"
              "// next, their hashes worked out where they weren't. Every one of them: one alike the last of the row before is the\n"
              "// row's too (a bag's small list in the page the one before's is)\n"
              "TIDE_HELPER void tide_client_row_keep(tide_client_gathered *store, tide_client_rows *r, const uint32_t row, const tide_client_item *items,\n"
              "                                      const uint32_t count, tide_heaps *heaps)\n{\n"
              "    const uint32_t from = store->count;\n"
              "    tide_client_gather(store, items, count, from); // (dropping repeats next to each other in the row)\n"
              "    for (uint32_t i = from; i < store->count; i++) {\n"
              "        if (store->items[i].page || tide_client_item_fill(heaps, &store->items[i])) continue;\n"
              "        store->count = from; // A piece of a bigger page: the row isn't kept by its pages\n"
              "        r->page_count[row] = UINT32_MAX;\n        return;\n    }\n"
              "    r->page_from[row] = from;\n    r->page_count[row] = store->count - from;\n}\n\n"
              "// ...or the pages its piece kept of it, none of which changed\n"
              "TIDE_HELPER void tide_client_row_keep_kept(tide_client_gathered *store, tide_client_rows *r, const uint32_t row, const tide_client_piece *s,\n"
              "                                           tide_heaps *heaps)\n{\n"
              "    const uint32_t from = r->page_from[row], count = r->page_count[row];\n"
              "    if (!count) {\n        r->page_from[row] = store->count;\n        return;\n    }\n"
              "    if (count == UINT32_MAX || !s->row_pages || from > s->row_page_count || count > s->row_page_count - from) {\n"
              "        r->page_count[row] = UINT32_MAX; // (Never so: a row kept is in its piece's arrays)\n        return;\n    }\n"
              "    tide_client_row_keep(store, r, row, s->row_pages + from, count, heaps);\n}\n\n");
    sb_put(o, "// Keeps what a piece's rows were read from, now that they're worked out: its columns' runs (those written, or all\n"
              "// where nothing was kept), the union of its rows' heap pages and its slots' pages, and each row's own pages\n"
              "// (`store`, which the piece takes). A piece a row of which read outside the match's heaps (`poisoned`) isn't kept.\n"
              "// `runs`: the written columns' runs tide_client_piece_same hashed, or NULL\n"
              "TIDE_HELPER void tide_client_piece_keep(tide_client_piece *s, const tide_world *w, const tide_table *t, const tide_columns *c,\n"
              "                                        const uint32_t chunk, const uint32_t at, const uint32_t rows, const bool *written,\n"
              "                                        const uint64_t *runs, tide_heaps *heaps, tide_client_gathered *reached,\n"
              "                                        tide_client_gathered *slots, tide_client_gathered *store, const bool poisoned)\n{\n"
              "    const bool all = !s->kept || s->rows != rows || !s->lanes;\n"
              "    if (!s->lanes) {\n        s->lanes = malloc((size_t)(c->count ? c->count : 1u) * sizeof *s->lanes);\n"
              "        if (!s->lanes) tide_out_of_memory();\n    }\n"
              "    for (uint32_t k = 0; k < c->count; k++) {\n"
              "        if (!c->sizes[k]) s->lanes[k] = 0u;\n"
              "        else if (all) s->lanes[k] = tide_client_run_hash(t, c, chunk, k, at, rows);\n"
              "        else if (written && written[k]) s->lanes[k] = runs ? runs[k] : tide_client_run_hash(t, c, chunk, k, at, rows);\n    }\n"
              "    const uint32_t heap_pages = tide_client_gathered_once(reached), slot_pages = tide_client_gathered_once(slots);\n"
              "    // (By heap, then place: a few dozen, for the most part in order already)\n"
              "    if (heap_pages > 64u) qsort(reached->items, heap_pages, sizeof *reached->items, tide_client_item_order);\n"
              "    else for (uint32_t i = 1; i < heap_pages; i++) {\n"
              "        const tide_client_item item = reached->items[i];\n"
              "        uint32_t j = i;\n"
              "        for (; j && reached->items[j - 1u].key > item.key; j--) reached->items[j] = reached->items[j - 1u];\n"
              "        reached->items[j] = item;\n    }\n"
              "    if (heap_pages + slot_pages > s->page_room) {\n"
              "        s->page_room = heap_pages + slot_pages;\n"
              "        s->pages = realloc(s->pages, (size_t)s->page_room * sizeof *s->pages);\n"
              "        s->page_hashes = realloc(s->page_hashes, (size_t)s->page_room * sizeof *s->page_hashes);\n"
              "        if (!s->pages || !s->page_hashes) tide_out_of_memory();\n    }\n"
              "    bool whole = !poisoned;\n"
              "    for (uint32_t i = 0; i < heap_pages; i++) {\n"
              "        const uint64_t place = reached->items[i].key;\n"
              "        uint32_t size;\n"
              "        // Only a page that starts at its own place is kept by it: the pieces of a bigger one aren't read again\n"
              "        if (!tide_client_place_hash(heaps, place, &size, &s->page_hashes[i])) { whole = false; continue; }\n"
              "        s->pages[i] = place;\n    }\n"
              "    for (uint32_t i = 0; i < slot_pages; i++) {\n"
              "        s->pages[heap_pages + i] = slots->items[i].key;\n"
              "        s->page_hashes[heap_pages + i] = tide_entities_slot_page_hash(&w->entities, (uint32_t)slots->items[i].key);\n    }\n"
              "    s->heap_pages = heap_pages;\n    s->slot_pages = slot_pages;\n    s->rows = rows;\n"
              "    s->kept = whole;\n"
              "    // Its rows' own pages: the store's, and the store has the piece's old arrays to fill next time\n"
              "    tide_client_item *items = s->row_pages;\n    const uint32_t room = s->row_page_room;\n"
              "    s->row_pages = store->items;\n    s->row_page_count = store->count;\n    s->row_page_room = store->room;\n"
              "    store->items = items;\n    store->room = room;\n    store->count = 0;\n}\n\n");
    sb_put(o, "// A piece's groups, gathered again from its rows' as they're looked at\n"
              "TIDE_HELPER void tide_client_piece_groups_none(tide_client_piece *s)\n{\n"
              "    s->low = INT64_MAX;\n    s->high = 0;\n    memset(s->bloom, 0, sizeof s->bloom);\n    s->by_group = false;\n}\n\n"
              "TIDE_HELPER void tide_client_piece_groups_add(tide_client_piece *s, const uint8_t flags, const int64_t *groups)\n{\n"
              "    if (flags & TIDE_CLIENT_BY_GROUP) s->by_group = true;\n"
              "    for (uint32_t i = 0; i < 2u; i++) {\n"
              "        const int64_t id = groups[i];\n"
              "        if (id <= 0) continue; // All players, or a client's own: no group's audience\n"
              "        if (id < s->low) s->low = id;\n        if (id > s->high) s->high = id;\n"
              "        const uint32_t bit = (uint32_t)tide_client_mix((uint64_t)id) & 255u;\n"
              "        s->bloom[bit >> 6] |= 1ull << (bit & 63u);\n    }\n}\n\n");
    // A Visibility's policies, as what they say rather than the pages they're in
    const int overrides = prog->visibility->fields.items[1].type.decl->index;
    sb_printf(o, "// A Visibility's policies (its overrides), hashed: what a row's components' audiences go by besides its groups\n"
                 "TIDE_HELPER uint64_t tide_client_policies_digest(const tide_map m)\n{\n"
                 "    uint64_t h = tide_hash_more(0x6a09e667f3bcc909ull, &m.live, sizeof m.live);\n"
                 "    int64_t order = 0;\n    int32_t next = 0, i;\n"
                 "    while ((i = tide_map_next(m, &tide_map%d_kind, &order, &next)) >= 0) {\n"
                 "        const tide_map%d_entry *entry = tide_map_entry(m, i, &tide_map%d_kind);\n"
                 "        h = tide_hash_more(h, &entry->key, sizeof entry->key);\n"
                 "        h = tide_hash_more(h, &entry->value, sizeof entry->value);\n    }\n"
                 "    return h | 1u; // (Never 0, which is none)\n}\n\n",
              overrides, overrides, overrides);
    // The directory of groups against the copy kept of it
    sb_printf(o, "TIDE_HELPER void tide_client_changed_add(struct tide_client_kept *k, const int64_t id)\n{\n"
                 "    if (k->changed_count == k->changed_room) {\n"
                 "        k->changed_room = k->changed_room ? k->changed_room * 2u : 64u;\n"
                 "        k->changed = realloc(k->changed, (size_t)k->changed_room * sizeof *k->changed);\n"
                 "        if (!k->changed) tide_out_of_memory();\n    }\n"
                 "    k->changed[k->changed_count++] = id;\n}\n\n"
                 "static int tide_client_id_order(const void *a, const void *b)\n{\n"
                 "    const int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;\n    return (x > y) - (x < y);\n}\n\n"
                 "// The groups whose audience is another than the copy kept says, or that came or went, into k->changed by id, in\n"
                 "// order, and the copy made the directory's. Entry by entry, as they are in the directory's list (an entry keeps its\n"
                 "// place until the map packs its live ones together, when those that moved are taken as changed too: more rows'\n"
                 "// audiences worked out again, never fewer); where the list is in chunks and is the one kept, a chunk whose page\n"
                 "// hashes as it did has the entries it had, and isn't read\n"
                 "TIDE_HELPER void tide_client_groups_diff(struct tide_client_kept *k, const tide_world *w, tide_heaps *heaps)\n{\n"
                 "    typedef tide_map%d_entry entry;\n"
                 "    const tide_map m = w->tide_visibility_state.groups;\n"
                 "    const tide_list l = m.entries;\n"
                 "    const uint32_t n = (uint32_t)tide_map_entries(m), had = k->group_count, per = TIDE_LIST_CHUNK / (uint32_t)sizeof(entry);\n"
                 "    k->changed_count = 0;\n"
                 "    if (n > k->group_room) {\n"
                 "        k->group_entries = tide_client_grow(k->group_entries, k->group_room, n, sizeof(entry));\n        k->group_room = n;\n    }\n"
                 "    entry *kept = k->group_entries;\n"
                 "    tide_heap *h = NULL;\n    const tide_block *b = NULL;\n"
                 "    if (l.at && tide_handle_world(l.at) == TIDE_IN_MATCH && (h = tide_client_heap(heaps, tide_client_heap_of(l.at))) != NULL\n"
                 "        && tide_handle_offset(l.at) >> TIDE_HEAP_PAGE_SHIFT < h->pages) b = tide_heap_block(h, tide_handle_offset(l.at));\n"
                 "    const uint32_t chunks = b && b->b != TIDE_LIST_ASSET && (b->b & TIDE_LIST_CHUNKED) ? b->b & ~TIDE_LIST_CHUNKED : 0u;\n"
                 "    const bool same = chunks && l.at == k->group_list;\n"
                 "    if (chunks > k->chunk_room) {\n"
                 "        k->chunk_hashes = tide_client_grow(k->chunk_hashes, k->chunk_room, chunks, sizeof *k->chunk_hashes);\n        k->chunk_room = chunks;\n    }\n"
                 "    bool hashed = chunks > 0;\n"
                 "    for (uint32_t c = 0, lo = 0; lo < n; c++, lo += per) {\n"
                 "        const uint32_t hi = n - lo < per ? n : lo + per;\n"
                 "        if (c < chunks) {\n"
                 "            const uint64_t chunk = ((const uint64_t *)(const void *)(b + 1))[c];\n"
                 "            uint64_t hash = 0;\n"
                 "            const tide_page *page = chunk >> TIDE_HEAP_PAGE_SHIFT < h->pages ? h->page[chunk >> TIDE_HEAP_PAGE_SHIFT] : NULL;\n"
                 "            if (page) hash = tide_client_page_hash(h, page);\n"
                 "            else hashed = false;\n"
                 "            if (same && hashed && c < k->chunk_count && hi <= had && hash == k->chunk_hashes[c]) continue;\n"
                 "            k->chunk_hashes[c] = hash;\n"
                 "        } else hashed = false;\n"
                 "        for (uint32_t i = lo; i < hi; i++) {\n"
                 "            const entry *now = tide_map_entry(m, (int32_t)i, &tide_map%d_kind);\n"
                 "            if (!now) continue;\n"
                 "            if (i < had && memcmp(&kept[i], now, sizeof *now) == 0) continue;\n"
                 "            if (i < had && kept[i].tide_order > 0) tide_client_changed_add(k, kept[i].key); // Gone, or its audience another\n"
                 "            if (now->tide_order > 0) tide_client_changed_add(k, now->key); // New, or its audience another\n"
                 "            kept[i] = *now;\n"
                 "        }\n    }\n"
                 "    for (uint32_t i = n; i < had; i++) if (kept[i].tide_order > 0) tide_client_changed_add(k, kept[i].key);\n"
                 "    k->group_count = n;\n    k->group_list = hashed ? l.at : 0u;\n    k->chunk_count = chunks;\n"
                 "    if (k->changed_count > 1u) {\n"
                 "        qsort(k->changed, k->changed_count, sizeof *k->changed, tide_client_id_order);\n"
                 "        uint32_t unique = 0;\n"
                 "        for (uint32_t i = 0; i < k->changed_count; i++) if (!unique || k->changed[unique - 1u] != k->changed[i]) k->changed[unique++] = k->changed[i];\n"
                 "        k->changed_count = unique;\n    }\n}\n\n", groups, groups);
    sb_put(o, "TIDE_HELPER bool tide_client_changed_has(const struct tide_client_kept *k, const int64_t id)\n{\n"
                 "    if (id <= 0) return false; // All players, or a client's own: no group's audience\n"
                 "    uint32_t lo = 0, hi = k->changed_count;\n"
                 "    while (lo < hi) {\n        const uint32_t mid = lo + (hi - lo) / 2u;\n"
                 "        if (k->changed[mid] < id) lo = mid + 1u;\n        else hi = mid;\n    }\n"
                 "    return lo < k->changed_count && k->changed[lo] == id;\n}\n\n"
                 "// Whether a group that changed may be one a piece's rows go by: one between its least and most whose bit it has\n"
                 "// (k->changed is in order: tide_client_groups_diff)\n"
                 "TIDE_HELPER bool tide_client_changed_in(const struct tide_client_kept *k, const tide_client_piece *s)\n{\n"
                 "    if (s->high <= 0) return false;\n"
                 "    uint32_t lo = 0, hi = k->changed_count;\n"
                 "    while (lo < hi) {\n        const uint32_t mid = lo + (hi - lo) / 2u;\n"
                 "        if (k->changed[mid] < s->low) lo = mid + 1u;\n        else hi = mid;\n    }\n"
                 "    for (; lo < k->changed_count && k->changed[lo] <= s->high; lo++) {\n"
                 "        const uint32_t bit = (uint32_t)tide_client_mix((uint64_t)k->changed[lo]) & 255u;\n"
                 "        if (s->bloom[bit >> 6] >> (bit & 63u) & 1u) return true;\n    }\n"
                 "    return false;\n}\n\n");
    gen_client_part_tables(g);
}

// The rows' part of tide_game_client_hashes for a game whose values hold anything in the heap: declares, for each
// archetype `a`, rows<a>, audience<a>, has<a> and, where some component can be denied, denied<a> and terms<a> (every
// component's term, the archetype's parts a row), as the loops after it read them, and the input slots' audiences.
// In scope: w, hs, client, kept (a tide_client_kept), again. `deniable` and `parts` are scratch the size of the
// program's components.
static void gen_client_hash_rows(gen *g, int *deniable, int *parts)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    // Scratch for the walk: a flag for each column of a table chunk (written since its pieces were read) and for each
    // component of a piece (a column of it changed there)
    int most_columns = 1, most_parts = 1;
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        if (arch_column_count(g, a) > most_columns) most_columns = arch_column_count(g, a);
        const int m = arch_parts(g, a, parts);
        if (m > most_parts) most_parts = m;
    }
    sb_printf(o, "    // Each entity's hash and what it has: again only for what changed\n"
                 "    const uint32_t call = ++kept->call;\n    (void)call;\n"
                 "    tide_client_gathered reached = {0}, slots = {0}, store = {0};\n"
                 "    // A scene's root came, went, or has another Visibility: every entity of its scene goes by its group\n"
                 "    bool roots_fresh = false;\n"
                 "    bool written[%d], changed[%d];\n"
                 "    uint64_t run[%d]; // The written columns' runs, as a piece's check hashed them\n    (void)run;\n",
              most_columns, most_parts, most_columns);
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        const int m = arch_parts(g, a, parts);
        const char *table = arch_name(g, a);
        const char *columns = arch_columns(g, a);
        const bool root = arch_scene_root(g, a);
        // A scene's root: which of its components is the Visibility its scene's entities go by
        int root_visibility = -1;
        for (int t = 0; root && t < m; t++) {
            if (parts[t] == prog->visibility->index) root_visibility = t;
        }
        sb_printf(o, "    const uint32_t rows%d = w->%s.count;\n"
                     "    tide_client_rows *const kept%d = &kept->rows[%d];\n"
                     "    tide_client_rows_room(kept%d, rows%d, w->%s.chunks, %du, %du);\n"
                     "    Audience *const audience%d = kept%d->audience;\n    uint64_t *const has%d = kept%d->has;\n"
                     "    uint64_t *const terms%d = kept%d->terms;\n    (void)terms%d;\n",
                  a, table, a, a, a, a, table, (unsigned)(m ? m : 1), (unsigned)n, a, a, a, a, a, a, a);
        if (n) sb_printf(o, "    Audience *const denied%d = kept%d->denied;\n", a, a);
        sb_printf(o, "    for (uint32_t chunk = 0; chunk < w->%s.chunks; chunk++) {\n"
                     "        const uint32_t first = chunk << (%s)->shift, here = tide_table_rows(&w->%s, (%s)->shift, chunk);\n"
                     "        tide_client_span *span = &kept%d->spans[chunk];\n"
                     "        // (The chunk's columns first: where none was written, its pieces are kept by their pages alone)\n"
                     "        const bool *const moved = !again && tide_client_span_written(span, &w->%s, %s, chunk, here, written) ? written : NULL;\n"
                     "        const uint32_t pieces = tide_client_span_pieces(span, here);\n"
                     "        for (uint32_t p = 0; p < pieces; p++) {\n"
                     "            tide_client_piece *piece = &span->pieces[p];\n"
                     "            const uint32_t at = p << TIDE_CLIENT_PIECE_SHIFT, count = here - at < TIDE_CLIENT_PIECE ? here - at : TIDE_CLIENT_PIECE;\n"
                     "            bool heap_moved = true, ids_moved = true;\n            const uint64_t *runs = NULL;\n            piece->fresh = false;\n"
                     "            if (again) {\n                for (uint32_t i = 0; i < %du; i++) changed[i] = true;\n            }\n"
                     "            else if (tide_client_piece_same(piece, kept%d, w, &w->%s, %s, chunk, at, count, moved, hs->heaps, tide_client_part_of%d, %du,\n"
                     "                                            changed, &heap_moved, &ids_moved, run, &runs)) {\n                TIDE_CLIENT_MARK(5);\n                continue;\n            }\n            TIDE_CLIENT_MARK(5);\n"
                     "            piece->routed = false;\n            reached.count = slots.count = store.count = 0;\n            bool poisoned = false, any_fresh = false;\n"
                     "            // (Its rows are the entities they were, with the snaps they had: each one's sum moves by its terms')\n"
                     "            if (!ids_moved && piece->slot_pages) tide_client_gather_numbers(&slots, piece->pages + piece->heap_pages, piece->slot_pages);\n"
                     "            for (uint32_t row = first + at; row < first + at + count; row++) {\n",
                  table, columns, table, columns, a, table, columns, (unsigned)(m ? m : 1), a, table, columns, a, (unsigned)(m ? m : 1));
        sb_printf(o, "                if (tide_row_gone(%s)) { // A hole (tide_table_remove): nothing kept of it\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        if (root) sb_printf(o, "                    if (kept%d->flags[row]) roots_fresh = true;\n", a);
        sb_printf(o, "                    kept%d->flags[row] = 0;\n                    kept%d->page_count[row] = UINT32_MAX;\n                    continue;\n                }\n",
                  a, a);
        sb_printf(o, "                const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "                const tide_location loc = {%du, row};\n                uint64_t mask = 0;\n                TIDE_CLIENT_COUNT(6);\n", a);
        for (int k = 0; k < n; k++) {
            if (comp_sparse(g, deniable[k])) sb_printf(o, "                if (%s) mask |= 1ull << %d;\n", presence_cell(g, "w", a, deniable[k], "row", false), k);
            else sb_printf(o, "                mask |= 1ull << %d;\n", k);
        }
        sb_put(o, "                if (ids_moved) {\n"
                  "                    const uint64_t slot = tide_entity_slot_index(&w->entities, e) >> TIDE_ENTITY_PAGE_SHIFT;\n"
                  "                    tide_client_gather_numbers(&slots, &slot, 1u);\n                }\n");
        sb_printf(o, "                const bool all = again || !(kept%d->flags[row] & TIDE_CLIENT_HASHED);\n"
                     "                uint64_t *const term = &terms%d[(size_t)row * %du];\n"
                     "                uint64_t *const digest = &kept%d->digests[(size_t)row * %du];\n"
                     "                bool fresh = all;\n"
                     "                uint64_t moved_by = 0; // What the terms worked out again add to its sum\n",
                  a, a, (unsigned)(m ? m : 1), a, (unsigned)(m ? m : 1));
        if (root) sb_put(o, "                if (all) roots_fresh = true;\n");
        // Each component's lanes, hashed, where a column of it changed in the piece: else they're as they were
        bool any_heapy = false;
        for (int t = 0; t < m; t++) {
            const int c = parts[t];
            const decl *d = prog->components.items[c];
            const bool heapy = client_codec_heapy(prog, (type){TY_COMPONENT, (decl *)d}, 0);
            any_heapy |= heapy;
            // (a sparse component's presence byte is part of what it was worked out from)
            char presence[32];
            if (comp_sparse(g, c)) snprintf(presence, sizeof presence, "%du", (unsigned)arch_presence(g, a, c));
            else snprintf(presence, sizeof presence, "UINT32_MAX");
            sb_printf(o, "                const uint64_t lanes%d = all || changed[%d] ? tide_client_lanes_digest(&w->%s, %s, row, %du, %du, %s) : digest[%d];\n",
                      t, t, table, columns, (unsigned)arch_column(g, a, c), (unsigned)comp_lanes(d), presence, t);
        }
        // A Visibility's policies (its overrides) are blocks in the heap, though no part of what a recipient is sent of it
        // (they're private: client_codec_heapy says it holds nothing there), and who may have the row's components goes
        // by them: a policy set where it is changes nothing of its lanes, only a page of its heap
        int visibility = -1;
        for (int t = 0; t < m; t++) {
            if (parts[t] == prog->visibility->index) visibility = t;
        }
        // What holds anything in the heap is hashed again together, when its lanes or a page it was read from changed
        if (any_heapy || visibility >= 0) {
            // (A piece whose heap pages are as they were has every row's pages kept: tide_client_piece_keep)
            sb_printf(o, "                bool heap_again = all || kept%d->page_count[row] == UINT32_MAX", a);
            for (int t = 0; t < m; t++) {
                if (t == visibility || client_codec_heapy(prog, (type){TY_COMPONENT, prog->components.items[parts[t]]}, 0))
                    sb_printf(o, " || lanes%d != digest[%d]", t, t);
            }
            sb_printf(o, ";\n                if (!heap_again && heap_moved && !tide_client_row_pages_same(piece, kept%d, row, hs->heaps)) {\n"
                         "                    heap_again = true;\n                    TIDE_CLIENT_COUNT(8);\n                }\n", a);
            sb_put(o, "                if (heap_again) {\n                    TIDE_CLIENT_COUNT(7);\n"
                      "                    hs->reached = 0;\n                    hs->floor = 0;\n                    hs->poisoned = false;\n");
            for (int t = 0; t < m; t++) {
                const int c = parts[t];
                if (!client_codec_heapy(prog, (type){TY_COMPONENT, prog->components.items[c]}, 0)) continue;
                sb_printf(o, "                    {\n                        const uint64_t now = tide_client_term%d_%d(w, e, loc, %s, hs);\n",
                          a, c, comp_sparse(g, c) ? presence_cell(g, "w", a, c, "row", false) : "true");
                if (t == root_visibility) sb_printf(o, "                        if (now != term[%d] || lanes%d != digest[%d]) roots_fresh = true;\n", t, t, t);
                sb_printf(o, "                        moved_by += now - term[%d];\n                        term[%d] = now;\n                    }\n"
                             "                    digest[%d] = lanes%d;\n",
                          t, t, t, t);
            }
            if (visibility >= 0) {
                // A Visibility's own policies are read as who may have it is worked out: their pages are the row's too
                // (its lanes changing reads them again: the map made, grown or moved), and what they say is kept beside
                // it, so pages that changed under them (other blocks of theirs) don't make its audience go again
                sb_printf(o, "                    {\n                        Visibility policy = {0};\n                        uint64_t policies = 0;\n"
                             "                        if (tide_load_Visibility_at(w, loc, &policy) && policy.overrides.live) {\n"
                             "                            tide_client_reach_list(hs, policy.overrides.entries);\n"
                             "                            tide_client_reach_list(hs, policy.overrides.index);\n"
                             "                            policies = tide_client_policies_digest(policy.overrides);\n                        }\n"
                             "                        if (policies != kept%d->policies[row]) fresh = true;\n"
                             "                        kept%d->policies[row] = policies;\n                    }\n",
                          a, a);
            }
            // (Fresh where a term or a policy came out another, or the row is new: its sum says the first)
            sb_printf(o, "                    if (hs->poisoned) kept%d->page_count[row] = UINT32_MAX;\n"
                         "                    else tide_client_row_keep(&store, kept%d, row, hs->reach, hs->reached, hs->heaps);\n"
                         "                } else tide_client_row_keep_kept(&store, kept%d, row, piece, hs->heaps);\n"
                         "                // (A row whose pages aren't all kept leaves its piece read again each call)\n"
                         "                if (kept%d->page_count[row] == UINT32_MAX) poisoned = true;\n"
                         "                else if (kept%d->page_count[row]) tide_client_gather_pages(&reached, store.items + kept%d->page_from[row], kept%d->page_count[row]);\n",
                      a, a, a, a, a, a, a);
        } else {
            sb_printf(o, "                kept%d->page_count[row] = 0;\n                kept%d->page_from[row] = store.count;\n", a, a);
        }
        // ...and what doesn't, each on its own
        for (int t = 0; t < m; t++) {
            const int c = parts[t];
            if (client_codec_heapy(prog, (type){TY_COMPONENT, prog->components.items[c]}, 0)) continue;
            sb_printf(o, "                if (all || lanes%d != digest[%d]) {\n"
                         "                    const uint64_t now = tide_client_term%d_%d(w, e, loc, %s, &hs->out);\n"
                         "                    moved_by += now - term[%d];\n                    term[%d] = now;\n"
                         "                    digest[%d] = lanes%d;\n"
                         "                    fresh = true;\n",
                      t, t, a, c, comp_sparse(g, c) ? presence_cell(g, "w", a, c, "row", false) : "true", t, t, t, t);
            if (t == root_visibility) sb_put(o, "                    roots_fresh = true;\n");
            sb_put(o, "                }\n");
        }
        // Its sum: what says which entity it is and its terms; where that's as it was, the sum it had and what its
        // terms worked out again moved by (a kept row's sum is always its own: wrapping sums are exact)
        sb_printf(o, "                uint64_t sum = has%d[row * 2u + 1u] + moved_by;\n"
                     "                if (all || ids_moved) {\n"
                     "                    sum = tide_client_entity_id%d(w, e, loc, &hs->out);\n"
                     "                    kept%d->slot[row] = tide_entity_slot_index(&w->entities, e);\n"
                     "                    kept%d->snaps[row] = tide_entity_slot_of(&w->entities, kept%d->slot[row])->snaps;\n", a, a, a, a, a);
        for (int t = 0; t < m; t++) sb_printf(o, "                    sum += term[%d];\n", t);
        sb_put(o, "                }\n");
        sb_printf(o, "                if (sum != has%d[row * 2u + 1u] || mask != has%d[row * 2u]) fresh = true;\n"
                     "                has%d[row * 2u] = mask;\n                has%d[row * 2u + 1u] = sum;\n"
                     "                kept%d->flags[row] |= TIDE_CLIENT_HASHED;\n"
                     "                if (fresh) {\n                    kept%d->fresh[row] = call;\n                    any_fresh = true;\n                    TIDE_CLIENT_COUNT(11);\n                }\n"
                     "            }\n",
                  a, a, a, a, a, a);
        sb_printf(o, "            TIDE_CLIENT_MARK(6);\n            tide_client_piece_keep(piece, w, &w->%s, %s, chunk, at, count, moved, runs, hs->heaps, &reached, &slots, &store, poisoned);\n"
                     "            piece->fresh = any_fresh;\n            TIDE_CLIENT_MARK(7);\n"
                     "        }\n"
                     "        tide_client_span_kept(span, &w->%s, %s, chunk, here, pieces);\n"
                     "    }\n",
                  table, columns, table, columns);
        // What's kept of rows and chunks past the table's end now is nothing: they were worked out under groups no call
        // since has looked at, and a table that grows again reads them afresh
        sb_printf(o, "    for (uint32_t chunk = w->%s.chunks; chunk < kept%d->span_room; chunk++) tide_client_span_forget(&kept%d->spans[chunk]);\n"
                     "    for (uint32_t row = rows%d; row < kept%d->was && row < kept%d->room; row++) {\n",
                  table, a, a, a, a, a);
        if (root) sb_printf(o, "        if (kept%d->flags[row]) roots_fresh = true;\n", a);
        sb_printf(o, "        kept%d->flags[row] = 0;\n    }\n"
                     "    kept%d->was = rows%d;\n", a, a, a);
    }
    sb_put(o, "    free(reached.items);\n    free(slots.items);\n    free(store.items);\n    TIDE_CLIENT_MARK(1);\n"
              "    // Who may have what: again for the rows that changed, for the rows whose groups' audiences changed, and for every\n"
              "    // row when a scene's root came, went or has another Visibility\n"
              "    hs->reached = 0;\n    hs->floor = 0;\n    hs->poisoned = false;\n"
              "    tide_client_reach_list(hs, w->tide_visibility_state.groups.entries);\n"
              "    tide_client_reach_list(hs, w->tide_visibility_state.groups.index);\n"
              "    uint64_t directory = tide_client_mix((uint64_t)w->tide_visibility_state.groups.entries.at\n"
              "        ^ (uint64_t)w->tide_visibility_state.groups.index.at * 0x9e3779b97f4a7c15ull ^ w->tide_visibility_state.groups.live);\n"
              "    for (uint32_t i = 0; i < hs->reached; i++) {\n"
              "        uint32_t size = 0;\n        uint64_t hash = 0;\n"
              "        (void)tide_client_place_hash(hs->heaps, tide_client_key_place(hs->reach[i].key), &size, &hash);\n"
              "        directory = tide_client_mix(directory ^ hash ^ size) + hs->reach[i].key;\n    }\n"
              "    const bool groups_changed = !kept->any || directory != kept->directory;\n"
              "    bool everyone = again || !kept->any || hs->poisoned || roots_fresh;\n"
              "    if (groups_changed) tide_client_groups_diff(kept, w, hs->heaps);\n"
              "    kept->directory = directory;\n    kept->any = true;\n    kept->tick = (uint32_t)w->Time.tick;\n"
              "    TIDE_CLIENT_MARK(2);\n"
              "    tide_visibility_walk walk = {0};\n    tide_map_view_none(&walk.groups);\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        const char *table = arch_name(g, a);
        sb_printf(o, "    for (uint32_t chunk = 0; chunk < w->%s.chunks; chunk++) {\n"
                     "        const uint32_t first = chunk << (%s)->shift, here = tide_table_rows(&w->%s, (%s)->shift, chunk);\n"
                     "        for (uint32_t at = 0; at < here; at += TIDE_CLIENT_PIECE) {\n"
                     "            tide_client_piece *piece = &kept%d->spans[chunk].pieces[at >> TIDE_CLIENT_PIECE_SHIFT];\n"
                     "            if (!everyone && !piece->fresh && !(groups_changed && (piece->by_group || tide_client_changed_in(kept, piece)))) continue;\n"
                     "            const uint32_t end = first + (here - at < TIDE_CLIENT_PIECE ? here : at + TIDE_CLIENT_PIECE);\n"
                     "            tide_client_piece_groups_none(piece);\n            piece->routed = false;\n"
                     "            for (uint32_t row = first + at; row < end; row++) {\n",
                  table, arch_columns(g, a), table, arch_columns(g, a), a);
        sb_printf(o, "                if (tide_row_gone(%s)) continue;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "                const uint8_t flags = kept%d->flags[row];\n"
                     "                int64_t *const groups = &kept%d->groups[row * 2u];\n"
                     "                if (!everyone && (flags & TIDE_CLIENT_SEEN) && kept%d->fresh[row] != call\n"
                     "                    && !(groups_changed && ((flags & TIDE_CLIENT_BY_GROUP) || tide_client_changed_has(kept, groups[0])\n"
                     "                                            || tide_client_changed_has(kept, groups[1])))) {\n"
                     "                    tide_client_piece_groups_add(piece, flags, groups);\n                    continue;\n                }\n",
                  a, a, a);
        sb_printf(o, "                const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "                const tide_location loc = {%du, row};\n                Visibility policy = {0};\n                bool has_policy = false;\n"
                     "                audience%d[row] = !client ? tide_visibility_base_at(w, e, loc, &policy, &has_policy, &walk, groups)\n"
                     "                    : tide_visibility_client_hidden(w, e) ? (Audience){0} : TIDE_AUDIENCE_ALL;\n"
                     "                kept%d->flags[row] = (uint8_t)((flags & TIDE_CLIENT_HASHED) | TIDE_CLIENT_SEEN\n"
                     "                    | (!client && has_policy && tide_visibility_by_groups(&policy) ? TIDE_CLIENT_BY_GROUP : 0u));\n"
                     "                tide_client_piece_groups_add(piece, kept%d->flags[row], groups);\n",
                  a, a, a, a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "                denied%d[row * %du + %du] = client || !((has%d[row * 2u] >> %d) & 1u) ? TIDE_AUDIENCE_ALL\n"
                         "                    : tide_visibility_denied_at(w, &policy, has_policy, audience%d[row], %du);\n",
                      a, (unsigned)n, (unsigned)k, a, k, a, (unsigned)deniable[k]);
        }
        sb_put(o, "            }\n        }\n    }\n");
    }
    sb_put(o, "    TIDE_CLIENT_MARK(3);\n");
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
        // (a slot nobody may have already stays so: the rest of an archetype of unowned rows changes nothing)
        sb_printf(o, "        slot_found[slot] = true;\n"
                     "        if (memcmp(&slot_audience[slot], &(Audience){0}, sizeof(Audience)) == 0) %s;\n"
                     "        slot_audience[slot] = tide_audience_intersect(slot_audience[slot], audience%d[row]);\n",
                  arch_stores(g, a, prog->owner->index) ? "continue" : "break", a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "        slot_audience[slot] = tide_audience_intersect(slot_audience[slot], denied%d[row * %du + %du]);\n", a, (unsigned)n, (unsigned)k);
        }
        sb_put(o, "    }\n");
    }
}

// What the routing loop subtracts for a component denied a recipient: its term, as the rows' part keeps it. `k` is its
// place among the archetype's deniable components; the term's place among all its components is found here.
static int client_part_of(const gen *g, const int a, const int component)
{
    int t = 0;
    for (int c = 0; c < component; c++) t += arch_stores(g, a, c);
    return t;
}
