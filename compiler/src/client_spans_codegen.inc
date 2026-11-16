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
// - A piece keeps a copy of its columns' runs beside their hashes: a run that
//   hashes otherwise is compared with it value by value, which says which rows
//   changed, and in which component (or the entity's or the scene's column).
//   Only those rows are looked at, and the piece's own arrays (its rows'
//   blocks, its pages) stand while none of them reads the heap again or is
//   another entity: the first that does has them made again from there.
// - Of a row looked at, a component no lane of which (nor its presence)
//   changed in the row is not hashed again, and one that holds anything in the
//   heap (a grid) is hashed again only when its lanes changed or a block its
//   row's values were read from changed. So a chunk whose small `Seen` changed
//   doesn't hash its 4,096 blocks again. A Visibility's policies are blocks in
//   the heap too, in every archetype that has one, though nothing a recipient
//   is sent: a policy set where it is changes a page of theirs, not the
//   Visibility's lanes. A row keeps every page it read, its first too where
//   that's the last of the row before (rows made together share pages).
// - Who may have a row goes by its groups' audiences, which are no part of the
//   row: the directory of groups is compared with the copy kept of it. A piece
//   whose rows' groups (their least and most, and a bit for each of 256) take
//   in none of those that changed, with no row restricted to a group or worked
//   out again, isn't looked at; in the others a row's audience is worked out
//   again only when its own group's, its scene's or a group one of its
//   components is restricted to changed, or when what it goes by in the row
//   did (its scene, its Visibility, which components it has). Every
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

// Whether a change written down for the repairs says which players it could be another for: where an Audience is
// small enough to keep one with each (128 players). Past that every change is every recipient's to look at.
static bool client_changes_masked(const program *prog)
{
    return prog->audience && prog->audience->fields.count <= 4;
}

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
              "    // Each column's run of its rows as they were last read: a copy, column after column, each its size times\n"
              "    // TIDE_CLIENT_PIECE (tide_client_runs_bytes). What says which rows of a piece changed, and in which component\n"
              "    uint8_t *lanes;\n"
              "    uint64_t *runs;        // ...and each run's hash: a written column's run that hashes as it did isn't compared\n"
              "    // The heap pages its rows' values were read from, by place (its heap and its first place there:\n"
              "    // tide_client_place), then its slots' pages\n"
              "    uint64_t *pages;\n"
              "    uint64_t *page_hashes;\n"
              "    // ...and of each, the lines (tide/page.h, Lines written) its rows read: their blocks', their slots'; 0 where\n"
              "    // nothing says\n"
              "    uint64_t *page_lines;\n"
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
              "    bool fresh;            // ...a row of it has its audience to work out again in this call\n"
              "    // What its rows add to each player's sum (tide_client_hashes_work's routing): the players, and for each the\n"
              "    // sum of its rows' hashes as that player may have them and how many; kept while no row of it is read\n"
              "    // again or has its audience worked out again (`routed`)\n"
              "    uint32_t *route_players;\n    uint64_t *route_sums;\n    uint32_t route_count, route_room;\n    bool routed;\n"
              "    // The walk (tide_client_walk) as of which `page_hashes` were every one its page's: a page the walks found\n"
              "    // changed in none since is as it was, and isn't looked at. 0: none\n"
              "    uint64_t valid;\n"
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
              "    int64_t *groups;       // ...its own group and its scene's, as its audience was worked out\n"
              "    uint64_t *policies;    // ...its Visibility's policies, hashed (tide_client_policies_digest), 0 for none\n"
              "    uint32_t *page_from;   // ...its heap pages in its piece's row_pages; a count of UINT32_MAX: never kept\n"
              "    uint32_t *page_count;\n"
              "    uint32_t *fresh;       // ...the call its audience was last to be worked out again in (what it goes by changed)\n"
              "    uint32_t *snaps;       // ...its entity's snaps, as what says which entity it is was last hashed, and its slot\n"
              "    uint32_t *slot;\n"
              "    uint8_t *flags;\n"
              "    uint32_t room, parts, denials, was;  // (was: the table's rows the call before)\n    tide_client_span *spans;\n    uint32_t span_room;\n"
              "} tide_client_rows;\n\n");
    // What each check found changed, written down for the repairs (client_changes_codegen.inc)
    const bool masked = client_changes_masked(prog);
    if (masked) {
        static const char *const ops[2][2] = {{"or", "|="}, {"xor", "^="}};
        for (int op = 0; op < 2; op++) {
            sb_printf(o, "TIDE_HELPER Audience tide_client_audience_%s(Audience a, const Audience b)\n{\n", ops[op][0]);
            for (int i = 0; i < prog->audience->fields.count; i++) {
                const char *name = str_to_cstr(prog->audience->fields.items[i].name);
                sb_printf(o, "    a.%s %s b.%s;\n", name, ops[op][1], name);
            }
            sb_put(o, "    return a;\n}\n\n");
        }
        sb_put(o, "TIDE_HELPER bool tide_client_audience_any(const Audience a)\n{\n    return (");
        for (int i = 0; i < prog->audience->fields.count; i++)
            sb_printf(o, "%sa.%s", i ? " | " : "", str_to_cstr(prog->audience->fields.items[i].name));
        sb_put(o, ") != 0;\n}\n\n"
                  "// Who had a kept row: its audience, everyone where that wasn't worked out, nobody where nothing was kept of it\n"
                  "#define TIDE_CLIENT_WAS(r, row) ((r)->flags[row] & TIDE_CLIENT_SEEN ? (r)->audience[row] : (r)->flags[row] ? TIDE_AUDIENCE_ALL : (Audience){0})\n\n");
    }
    sb_printf(o, "// The rows a check found changed since the check before, for a repair to look at them alone (tide_game_client_changes):\n"
                 "// a row is written down where anything a recipient's part of it goes by came out another (which entity it is, a\n"
                 "// term of its sum, which components it has, who may have it or a component of it), or it came or went%s.\n"
                 "// A check is a node: the world's hash as the check left it, and its rows. Two worlds that hash as two nodes do\n"
                 "// differ, for a recipient, only in the rows written down between them. A check writes two rows at most for each\n"
                 "// row of the world; a world most of whose rows change every tick (a battle) would write them all each check for\n"
                 "// repairs that then walk anyway, so after four checks in a row that each wrote more than half the world's rows\n"
                 "// (and TIDE_CLIENT_CHANGES_LEAST), a check writes none and says only that everything may have changed, but\n"
                 "// each sixteenth, which writes, and ends the run if it wrote few. One or two such checks (a player joining,\n"
                 "// a whole view asked for) are kept as they are: most of their rows are one player's\n"
                 "#ifndef TIDE_CLIENT_NODES\n#define TIDE_CLIENT_NODES 256u\n#endif\n"
                 "#ifndef TIDE_CLIENT_CHANGES_MOST\n#define TIDE_CLIENT_CHANGES_MOST (1u << 19)\n#endif\n"
                 "#ifndef TIDE_CLIENT_CHANGES_LEAST\n#define TIDE_CLIENT_CHANGES_LEAST 64u\n#endif\n"
                 "typedef struct tide_client_change { uint32_t archetype, row;%s } tide_client_change;\n"
                 "// ...and one of the rows' part of a check, until who may have it is worked out: the components that can be denied\n"
                 "// whose terms moved (`parts`, a bit each), whether anything else of it did (`whole`), and who had it\n"
                 "typedef struct tide_client_pending { uint64_t parts; uint32_t archetype, row; bool whole;%s } tide_client_pending;\n"
                 "typedef struct tide_client_node { uint64_t world; uint32_t from, count; bool all; } tide_client_node;\n"
                 "typedef struct tide_client_journal {\n"
                 "    tide_client_change *items;\n    uint32_t count, room;\n"
                 "    tide_client_pending *pending;\n    uint32_t pending_count, pending_room;\n"
                 "    tide_client_node nodes[TIDE_CLIENT_NODES];\n    uint32_t node_count;\n"
                 "    uint32_t from, heavy; // This check's: where its rows start, and how many are half the world's and some\n"
                 "    bool on, lost;        // ...whether it writes any (a server's, after its first), or none for the run it's in\n"
                 "    uint32_t heavy_run;   // The checks in a row that wrote more, or none for it\n"
                 "} tide_client_journal;\n\n",
              masked ? ", each with the players it could be another for" : "",
              masked ? " Audience who;" : "", masked ? " Audience was;" : "");
    sb_put(o, "TIDE_HELPER void tide_client_journal_begin(tide_client_journal *j, const bool on, const uint64_t rows)\n{\n"
              "    j->on = on && j->node_count;\n    j->from = j->count;\n    j->pending_count = 0;\n"
              "    j->lost = j->on && j->heavy_run >= 4u && (j->heavy_run & 15u) != 0u;\n"
              "    j->heavy = (uint32_t)(rows / 2u < UINT32_MAX / 4u ? rows / 2u : UINT32_MAX / 4u) + TIDE_CLIENT_CHANGES_LEAST;\n}\n\n");
    sb_printf(o, "TIDE_HELPER void tide_client_journal_row(tide_client_journal *j, const uint32_t archetype, const uint32_t row, const uint64_t parts,\n"
                 "                                         const bool whole%s)\n{\n"
                 "    if (!j->on || j->lost) return;\n"
                 "    if (j->pending_count == j->pending_room) {\n"
                 "        j->pending_room = j->pending_room ? j->pending_room * 2u : 256u;\n"
                 "        j->pending = realloc(j->pending, (size_t)j->pending_room * sizeof *j->pending);\n"
                 "        if (!j->pending) tide_out_of_memory();\n    }\n"
                 "    j->pending[j->pending_count++] = (tide_client_pending){.parts = parts, .archetype = archetype, .row = row, .whole = whole%s};\n}\n\n",
              masked ? ", const Audience was" : "", masked ? ", .was = was" : "");
    sb_printf(o, "TIDE_HELPER void tide_client_journal_add(tide_client_journal *j, const uint32_t archetype, const uint32_t row%s)\n{\n"
                 "    if (!j->on || j->lost) return;\n"
                 "    if (j->count == j->room) {\n"
                 "        j->room = j->room ? j->room * 2u : 256u;\n"
                 "        j->items = realloc(j->items, (size_t)j->room * sizeof *j->items);\n"
                 "        if (!j->items) tide_out_of_memory();\n    }\n"
                 "    j->items[j->count++] = (tide_client_change){.archetype = archetype, .row = row%s};\n}\n\n",
              masked ? ", const Audience who" : "", masked ? ", .who = who" : "");
    sb_put(o, "// The check is a node, the world hashing to `world` as it left it; the oldest go once there are too many nodes or\n"
              "// rows\n"
              "TIDE_HELPER void tide_client_journal_end(tide_client_journal *j, const uint64_t world)\n{\n"
              "    const bool all = j->lost;\n"
              "    // (The same world checked again, as a hash asked for one player at a time is: the node it is already, and no\n"
              "    // check of a run's)\n"
              "    if (j->node_count && j->count == j->from && j->nodes[j->node_count - 1u].world == world) return;\n"
              "    if (j->on) j->heavy_run = !all && j->count - j->from <= j->heavy ? 0u : j->heavy_run == UINT32_MAX ? 16u : j->heavy_run + 1u;\n"
              "    uint32_t drop = j->node_count == TIDE_CLIENT_NODES ? TIDE_CLIENT_NODES / 2u : 0u;\n"
              "    if (j->count > TIDE_CLIENT_CHANGES_MOST) {\n"
              "        while (drop < j->node_count && j->count - (drop ? j->nodes[drop].from : 0u) > TIDE_CLIENT_CHANGES_MOST / 2u) drop++;\n    }\n"
              "    if (drop) {\n"
              "        // (What the node before the first kept wrote is gone with it: a node's rows are how it differs from the one before)\n"
              "        const uint32_t base = drop < j->node_count ? j->nodes[drop].from : j->from;\n"
              "        if (j->count > base) memmove(j->items, j->items + base, (size_t)(j->count - base) * sizeof *j->items); // (No rows written yet: no items)\n"
              "        j->count -= base;\n        j->from -= base;\n"
              "        for (uint32_t i = drop; i < j->node_count; i++) {\n"
              "            j->nodes[i - drop] = j->nodes[i];\n            j->nodes[i - drop].from -= base;\n        }\n"
              "        j->node_count -= drop;\n    }\n"
              "#ifdef TIDE_CLIENT_COUNTS\n    __atomic_add_fetch(&tide_client_counts[27], j->count - j->from, __ATOMIC_RELAXED);\n#endif\n"
              "    j->nodes[j->node_count++] = (tide_client_node){.world = world, .from = j->from, .count = j->count - j->from, .all = all};\n}\n\n");
    sb_put(o, "// One heap's pages, or the slots' pages, by place: each one's hash as the last walk found it (0: none there), and\n"
              "// the walk that last found it changed (tide_client_walk)\n"
              "typedef struct tide_client_walked { uint64_t *hash, *at; uint32_t room; } tide_client_walked;\n\n");
    sb_printf(o, "struct tide_client_kept {\n    tide_client_rows rows[%d];\n    tide_client_journal journal;\n"
                 "    uint64_t directory;     // The groups' audiences, as a hash of the pages they're kept in\n"
                 "    Audience came;          // The players a group's audience gained since they were last asked for (tide_game_client_came)\n"
                 "    void *group_entries;    // ...and a copy of the directory's entries, as they are in its list, live and dead\n"
                 "    uint32_t group_count, group_room;\n"
                 "    uint64_t group_list;    // ...the list as it was (its handle), 0 where its chunks' hashes aren't kept\n"
                 "    uint64_t *chunk_hashes; // ...and the hash of each of its chunks' pages\n    uint32_t chunk_count, chunk_room;\n"
                 "    int64_t *changed;       // The groups whose audience changed since the call before, by id\n"
                 "    uint32_t changed_count, changed_room;\n"
                 "    uint64_t *route_sums;   // Scratch for a piece's routing: a sum and a count for each player, zero between pieces\n"
                 "    uint32_t *route_touched;\n"
                 "    // The world's heap pages (by its heaps' numbers: a slot's runs in a row) and its slots' pages as the walks\n"
                 "    // found them (tide_client_walk), and the walks\n"
                 "    tide_client_walked *walked;\n    uint32_t walked_count;\n    tide_client_walked slots_walked;\n    uint64_t walks;\n"
                 "    uint32_t call;\n    uint32_t tick;          // The world's tick they were kept at\n    bool any;\n};\n\n",
              archetypes > 0 ? archetypes : 1);
    sb_printf(o, "TIDE_HELPER void tide_client_kept_free(struct tide_client_kept *k)\n{\n"
                 "    for (uint32_t a = 0; a < %du; a++) {\n        tide_client_rows *r = &k->rows[a];\n"
                 "        for (uint32_t i = 0; i < r->span_room; i++) {\n"
                 "            tide_client_span *s = &r->spans[i];\n"
                 "            for (uint32_t p = 0; p < s->piece_room; p++) {\n"
                 "                free(s->pieces[p].lanes);\n                free(s->pieces[p].runs);\n                free(s->pieces[p].pages);\n                free(s->pieces[p].page_hashes);\n                free(s->pieces[p].page_lines);\n"
                 "                free(s->pieces[p].row_pages);\n                free(s->pieces[p].route_players);\n                free(s->pieces[p].route_sums);\n            }\n"
                 "            free(s->pieces);\n            free(s->lanes);\n        }\n"
                 "        free(r->spans);\n        free(r->audience);\n        free(r->has);\n        free(r->denied);\n        free(r->terms);\n"
                 "        free(r->groups);\n        free(r->policies);\n        free(r->page_from);\n        free(r->page_count);\n"
                 "        free(r->fresh);\n        free(r->snaps);\n        free(r->slot);\n        free(r->flags);\n    }\n"
                 "    free(k->journal.items);\n    free(k->journal.pending);\n"
                 "    free(k->group_entries);\n    free(k->chunk_hashes);\n    free(k->changed);\n    free(k->route_sums);\n    free(k->route_touched);\n"
                 "    for (uint32_t n = 0; n < k->walked_count; n++) {\n        free(k->walked[n].hash);\n        free(k->walked[n].at);\n    }\n"
                 "    free(k->walked);\n    free(k->slots_walked.hash);\n    free(k->slots_walked.at);\n"
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
    sb_put(o, "// The pages of a world that changed since the last walk: each one's hash now (tide_client_page_hash, as a piece keeps\n"
              "// it) against the one the walk before found at its place, and where they differ, or a page came or went there, this\n"
              "// walk is the one it changed in. A piece whose pages were all its pages' as of a walk (tide_client_piece.valid) then\n"
              "// passes over each that no walk since found changed, without reading it: it hashes as it did then. Every check\n"
              "// walks, so each change is found by the first walk after it (a page's hash is of its bytes, whichever world's\n"
              "// page it is, as everything kept here goes by)\n"
              "TIDE_HELPER void tide_client_walked_room(tide_client_walked *s, const uint32_t places)\n{\n"
              "    if (places <= s->room) return;\n"
              "    uint32_t room = s->room ? s->room : 64u;\n    while (room < places) room *= 2u;\n"
              "    s->hash = tide_client_grow(s->hash, s->room, room, sizeof *s->hash);\n"
              "    s->at = tide_client_grow(s->at, s->room, room, sizeof *s->at);\n"
              "    s->room = room;\n}\n\n"
              "TIDE_HELPER void tide_client_walked_set(tide_client_walked *s, const uint32_t place, const uint64_t hash, const uint64_t walk)\n{\n"
              "    if (s->hash[place] == hash) return;\n"
              "    s->hash[place] = hash;\n    s->at[place] = walk;\n}\n\n"
              "TIDE_HELPER void tide_client_walk(struct tide_client_kept *k, tide_heaps *heaps, const tide_entities *entities)\n{\n"
              "    const uint64_t walk = ++k->walks;\n"
              "    const uint32_t numbers = tide_heap_count * TIDE_HEAP_RUNS;\n"
              "    if (k->walked_count < numbers) {\n"
              "        k->walked = tide_client_grow(k->walked, k->walked_count, numbers, sizeof *k->walked);\n"
              "        k->walked_count = numbers;\n    }\n"
              "    tide_heap *all = __atomic_load_n(&heaps->all, __ATOMIC_ACQUIRE);\n"
              "    for (uint32_t n = 0; n < numbers; n++) {\n"
              "        tide_client_walked *s = &k->walked[n];\n"
              "        tide_heap *h = NULL;\n"
              "        if (all) {\n"
              "            tide_heap *base = all + n / TIDE_HEAP_RUNS;\n"
              "            tide_heap *runs = n % TIDE_HEAP_RUNS ? __atomic_load_n(&base->runs, __ATOMIC_ACQUIRE) : NULL;\n"
              "            h = n % TIDE_HEAP_RUNS ? (runs ? runs + (n % TIDE_HEAP_RUNS - 1u) : NULL) : base;\n        }\n"
              "        const uint32_t pages = h ? h->pages : 0u;\n"
              "        if (!pages && !s->room) continue; // (Most runs' heaps are never made)\n"
              "        tide_client_walked_room(s, pages);\n"
              "        for (uint32_t i = 1; i < pages; i++) {\n"
              "            const tide_page *p = h->page[i];\n"
              "            tide_client_walked_set(s, i, p && p->first == i ? tide_client_page_hash(h, p) : 0u, walk);\n        }\n"
              "        for (uint32_t i = pages; i < s->room; i++) tide_client_walked_set(s, i, 0u, walk); // (Past its end now)\n"
              "    }\n"
              "    tide_client_walked *s = &k->slots_walked;\n"
              "    tide_client_walked_room(s, entities->pages);\n"
              "    for (uint32_t i = 0; i < s->room; i++) {\n"
              "        tide_client_walked_set(s, i, i < entities->pages ? tide_entities_slot_page_hash(entities, i) : 0u, walk);\n    }\n}\n\n"
              "// Whether a walk after `since` found the heap page at a place (tide_client_place) changed; so where nothing's known\n"
              "TIDE_HELPER bool tide_client_walked_since(const struct tide_client_kept *k, const uint64_t place, const uint64_t since)\n{\n"
              "    const uint32_t heap = (uint32_t)(place >> 32), first = (uint32_t)place;\n"
              "    const uint32_t n = (uint32_t)tide_heap_slots[heap & (TIDE_HEAP_IDS - 1u)] * TIDE_HEAP_RUNS + (heap >> TIDE_HEAP_ID_BITS & (TIDE_HEAP_RUNS - 1u));\n"
              "    return n >= k->walked_count || first >= k->walked[n].room || k->walked[n].at[first] > since;\n}\n\n"
              "// ...or a slots' page\n"
              "TIDE_HELPER bool tide_client_slots_walked_since(const struct tide_client_kept *k, const uint32_t page, const uint64_t since)\n{\n"
              "    return page >= k->slots_walked.room || k->slots_walked.at[page] > since;\n}\n\n");
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
              "// A column's run of rows in a table chunk, from row `at` of it, where it is\n"
              "TIDE_HELPER const uint8_t *tide_client_run(const tide_table *t, const tide_columns *c, const uint32_t chunk, const uint32_t k,\n"
              "                                           const uint32_t at)\n{\n"
              "    return (const uint8_t *)tide_page_data(t->pages[chunk * c->count + k]) + (size_t)at * c->sizes[k];\n}\n\n"
              "// Which of `rows` values of `size` bytes side by side differ between two runs: a bit a row\n"
              "TIDE_HELPER uint64_t tide_client_rows_differ(const uint8_t *now, const uint8_t *was, const uint32_t rows, const uint32_t size)\n{\n"
              "    uint64_t differ = 0;\n"
              "    if (size == 4u) {\n"
              "        for (uint32_t i = 0; i < rows; i++) {\n            uint32_t a, b;\n"
              "            memcpy(&a, now + (size_t)i * 4u, 4u);\n            memcpy(&b, was + (size_t)i * 4u, 4u);\n"
              "            differ |= (uint64_t)(a != b) << i;\n        }\n"
              "    } else if (size == 8u) {\n"
              "        for (uint32_t i = 0; i < rows; i++) {\n            uint64_t a, b;\n"
              "            memcpy(&a, now + (size_t)i * 8u, 8u);\n            memcpy(&b, was + (size_t)i * 8u, 8u);\n"
              "            differ |= (uint64_t)(a != b) << i;\n        }\n"
              "    } else if (size == 1u) {\n"
              "        for (uint32_t i = 0; i < rows; i++) differ |= (uint64_t)(now[i] != was[i]) << i;\n"
              "    } else {\n"
              "        for (uint32_t i = 0; i < rows; i++) differ |= (uint64_t)(memcmp(now + (size_t)i * size, was + (size_t)i * size, size) != 0) << i;\n"
              "    }\n"
              "    return differ;\n}\n\n"
              "// The bytes a piece's copy of its columns' runs takes: each column's size, TIDE_CLIENT_PIECE rows of it\n"
              "TIDE_HELPER size_t tide_client_runs_bytes(const tide_columns *c)\n{\n"
              "    size_t bytes = 0;\n    for (uint32_t k = 0; k < c->count; k++) bytes += (size_t)c->sizes[k] << TIDE_CLIENT_PIECE_SHIFT;\n"
              "    return bytes ? bytes : 1u;\n}\n\n");
    sb_put(o, "// The hash of the heap page at a place (tide_client_place), where one starts there; else false\n"
              "TIDE_HELPER bool tide_client_place_same(tide_heaps *heaps, const uint64_t place, const uint64_t hash)\n{\n"
              "    uint32_t size;\n    uint64_t now;\n"
              "    return tide_client_place_hash(heaps, place, &size, &now) && now == hash;\n}\n\n");
    sb_put(o, "// Whether a piece's rows, and all they were read from, are as they were when what's kept of them was worked\n"
              "// out: their columns' runs (those of the columns `written`, the rest unwritten), their heap pages and their\n"
              "// slots' pages. A written column's run that hashes otherwise than it did is compared with the piece's copy of it,\n"
              "// row by row: `changed` says, for each of the archetype's components (`part_of`, `parts` of them), which\n"
              "// rows of the piece a lane of it changed in (a bit a row), and `ids_rows` the same for the entity's and the scene's\n"
              "// columns; the copy is then the run as it is now, so the piece's rows must be read (those rows of it, at least).\n"
              "// `heap_moved` says whether one of their heap pages changed, and `slots_moved` whether one of their slots' pages\n"
              "// did (a row's snaps then say whether it's the entity it was: tide_client_row_slot_same).\n"
              "// A heap page that changed has the blocks of it the rows read hashed again (tide_client_item_same): where every one\n"
              "// is as it was, the page's hash now is kept in its place, and the piece isn't read for it\n"
              "TIDE_HELPER bool tide_client_piece_blocks_same(tide_client_piece *s, const tide_heap *h, const tide_page *page, const uint64_t place,\n"
              "                                               const uint64_t now, const uint64_t kept)\n{\n"
              "    bool any = false;\n"
              "    for (uint32_t i = 0; i < s->row_page_count; i++) {\n"
              "        if (tide_client_key_place(s->row_pages[i].key) != place) continue;\n"
              "        if (!tide_client_item_same_by(h, page, now, &s->row_pages[i], kept)) return false;\n"
              "        any = true;\n    }\n"
              "    return any;\n}\n\n"
              "// ...for several such pages at once (`at`: each one's among the piece's pages), the rows' blocks gone through once:\n"
              "// every block of them as it was, and each page with one\n"
              "#define TIDE_CLIENT_MOVED_MOST 16u\n"
              "typedef struct tide_client_moved { const tide_heap *h; const tide_page *page; uint64_t now; uint32_t at; } tide_client_moved;\n\n"
              "TIDE_HELPER bool tide_client_piece_blocks_moved(tide_client_piece *s, const tide_client_moved *moved, const uint32_t count)\n{\n"
              "    uint32_t found = 0; // A bit for each page with a block\n"
              "    const uint64_t low = s->pages[moved[0].at], high = s->pages[moved[count - 1u].at]; // (In order, as the pages are)\n"
              "    for (uint32_t i = 0; i < s->row_page_count; i++) {\n"
              "        const uint64_t place = tide_client_key_place(s->row_pages[i].key);\n"
              "        if (place < low || place > high) continue;\n"
              "        for (uint32_t j = 0; j < count; j++) {\n"
              "            if (s->pages[moved[j].at] != place) continue;\n"
              "            if (!tide_client_item_same_by(moved[j].h, moved[j].page, moved[j].now, &s->row_pages[i], s->page_hashes[moved[j].at])) return false;\n"
              "            found |= 1u << j;\n            break;\n        }\n    }\n"
              "    return found == (count == 32u ? UINT32_MAX : (1u << count) - 1u);\n}\n\n");
    sb_put(o, "// ...and a slot's page that changed (an entity of it came or went) has the snaps of the piece's rows whose slots\n"
              "// are in it looked at again: where each one's are as they were kept, what says which entity it is hasn't moved\n"
              "// (its entity's column is as it was, or a run of it would have changed). A row's slot the page's lines written since\n"
              "// it hashed to `kept` (its hash before, tide/page.h) don't take in is as it was, and isn't read\n"
              "TIDE_HELPER bool tide_client_piece_snaps_same(const tide_client_rows *r, const tide_world *w, const tide_table *t, const tide_columns *c,\n"
              "                                              const uint32_t first, const uint32_t rows, const uint32_t page, const uint64_t kept)\n{\n"
              "    bool any = false;\n"
              "    const tide_page *slots = page < w->entities.pages ? w->entities.page[page] : NULL;\n"
              "    for (uint32_t row = first; row < first + rows; row++) {\n"
              "        const tide_entity *e = (const tide_entity *)tide_table_get(t, c, row, 0u);\n"
              "        if (tide_row_gone(e)) continue;\n"
              "        if (!(r->flags[row] & TIDE_CLIENT_HASHED)) return false;\n"
              "        if (r->slot[row] >> TIDE_ENTITY_PAGE_SHIFT != page) continue;\n"
              "        any = true;\n"
              "        const uint32_t in = (r->slot[row] & ((1u << TIDE_ENTITY_PAGE_SHIFT) - 1u)) * (uint32_t)sizeof(tide_entity_slot);\n"
              "        if (slots && kept && tide_page_same_since(slots, kept, in, (uint32_t)sizeof(tide_entity_slot))) {\n"
              "            TIDE_CLIENT_COUNT(23);\n            continue;\n        }\n"
              "        // (The slot it was in, as its row says the same entity: still its own, with its snaps there)\n"
              "        const tide_entity_slot *slot = tide_entity_slot_of(&w->entities, r->slot[row]);\n"
              "        if (memcmp(&slot->entity, e, sizeof *e) != 0 || slot->snaps != r->snaps[row]) return false;\n    }\n"
              "    return any;\n}\n\n");
    sb_put(o, "TIDE_HELPER bool tide_client_piece_same(tide_client_piece *s, const tide_client_rows *r, const tide_world *w, const tide_table *t,\n"
              "                                        const tide_columns *c, const uint32_t chunk, const uint32_t at, const uint32_t rows,\n"
              "                                        const bool *written, tide_heaps *heaps, const int32_t *part_of, const uint32_t parts,\n"
              "                                        uint64_t *changed, uint64_t *ids_rows, bool *heap_moved, bool *slots_moved,\n"
              "                                        const struct tide_client_kept *k)\n{\n"
              "    *heap_moved = *slots_moved = true;\n    *ids_rows = UINT64_MAX;\n"
              "    for (uint32_t i = 0; i < parts; i++) changed[i] = UINT64_MAX;\n"
              "#ifdef TIDE_CLIENT_MEMO_OFF // For tests: every row read again\n"
              "    (void)s; (void)r; (void)w; (void)t; (void)c; (void)chunk; (void)at; (void)rows; (void)written; (void)heaps; (void)part_of; (void)k;\n"
              "    return false;\n#else\n"
              "    TIDE_CLIENT_COUNT(1);\n"
              "    if (!s->lanes) {\n        TIDE_CLIENT_COUNT(2);\n        return false;\n    }\n"
              "    // A piece that isn't kept whole (its rows are more or fewer, or a row of it can't be kept by its pages) still\n"
              "    // has its copy: the rows it had are compared, every column of them, and each then says for itself whether its\n"
              "    // own blocks and its slot are as they were (heap_moved, slots_moved)\n"
              "    const bool whole = s->kept && s->rows == rows;\n"
              "    const uint32_t had = s->rows < rows ? s->rows : rows;\n"
              "    const uint64_t fresh = had < rows ? UINT64_MAX << had : 0u; // (Rows it hadn't: everything of them)\n"
              "    for (uint32_t i = 0; i < parts; i++) changed[i] = fresh;\n"
              "    bool same = true, by_run = false;\n    *ids_rows = fresh;\n    (void)by_run;\n"
              "    size_t from = 0;\n"
              "    for (uint32_t k = 0; k < c->count; from += (size_t)c->sizes[k] << TIDE_CLIENT_PIECE_SHIFT, k++) {\n"
              "        const uint32_t size = c->sizes[k];\n"
              "        if (!size || (whole && !(written && written[k]))) continue;\n"
              "        const uint8_t *now = tide_client_run(t, c, chunk, k, at);\n"
              "        uint8_t *was = s->lanes + from;\n"
              "        TIDE_CLIENT_COUNT(17);\n        TIDE_CLIENT_COUNT_BY(18, rows * size);\n"
              "        // (Its hash first: the copy of a run that didn't change isn't read)\n"
              "        const uint64_t run = tide_hash_wide(now, (size_t)rows * size);\n"
              "        if (whole && run == s->runs[k]) continue;\n"
              "        const uint64_t rows_of = tide_client_rows_differ(now, was, had, size);\n"
              "        memcpy(was, now, (size_t)rows * size);\n"
              "        s->runs[k] = run;\n"
              "        if (!rows_of) continue;\n"
              "        same = false;\n        by_run = true;\n"
              "        if (part_of[k] >= 0) changed[part_of[k]] |= rows_of;\n"
              "        else *ids_rows |= rows_of; // The entity's or the scene's\n"
              "    }\n"
              "    if (!whole) {\n        TIDE_CLIENT_COUNT(2);\n        return false;\n    }\n");
    sb_put(o, "    *heap_moved = false;\n"
              "    // A page no walk found changed since the one its hash was kept as of (`valid`) isn't looked at\n"
              "    const uint64_t since = s->valid;\n"
              "    // The pages that hash otherwise than kept, a few at a time, their blocks then looked at in one go\n"
              "    tide_client_moved moved[TIDE_CLIENT_MOVED_MOST];\n    uint32_t moved_count = 0;\n"
              "    // (Its pages are in order, by heap: tide_client_piece_keep, so a heap is found once for its pages)\n"
              "    uint32_t id = UINT32_MAX;\n    tide_heap *h = NULL;\n"
              "    for (uint32_t i = 0; i < s->heap_pages; i++) {\n"
              "        const uint64_t place = s->pages[i];\n"
              "        if (since && !tide_client_walked_since(k, place, since)) continue;\n"
              "        TIDE_CLIENT_COUNT(15);\n"
              "        if ((uint32_t)(place >> 32) != id) {\n            id = (uint32_t)(place >> 32);\n            h = tide_client_heap(heaps, id);\n        }\n"
              "        const uint32_t first = (uint32_t)place;\n"
              "        const tide_page *page = h && first < h->pages ? h->page[first] : NULL;\n"
              "        if (page && page->first == first) {\n"
              "            const uint64_t now = tide_client_page_hash(h, page);\n"
              "            if (now == s->page_hashes[i]) continue;\n"
              "            // Written since in none of the lines its blocks are in (another entity's block of the page changed)\n"
              "            if (s->page_lines[i] && page->size == 1u << TIDE_HEAP_PAGE_SHIFT\n"
              "                && tide_page_lines_same_since(page, s->page_hashes[i], s->page_lines[i])) {\n"
              "                TIDE_CLIENT_COUNT(32);\n                s->page_hashes[i] = now;\n                continue;\n            }\n"
              "            if (moved_count < TIDE_CLIENT_MOVED_MOST) {\n"
              "                moved[moved_count++] = (tide_client_moved){h, page, now, i};\n                continue;\n            }\n"
              "            bool held;\n            TIDE_CLIENT_TIMED(8, held = tide_client_piece_blocks_same(s, h, page, s->pages[i], now, s->page_hashes[i]));\n            if (held) {\n"
              "                s->page_hashes[i] = now;\n                continue;\n            }\n        }\n"
              "        *heap_moved = true;\n        same = false;\n        break;\n    }\n"
              "    if (!*heap_moved && moved_count) {\n"
              "        bool held;\n        TIDE_CLIENT_TIMED(8, held = tide_client_piece_blocks_moved(s, moved, moved_count));\n"
              "        if (held) for (uint32_t j = 0; j < moved_count; j++) s->page_hashes[moved[j].at] = moved[j].now;\n"
              "        else {\n            *heap_moved = true;\n            same = false;\n        }\n    }\n");
    sb_put(o, "    *slots_moved = false;\n"
              "    for (uint32_t i = s->heap_pages; i < s->heap_pages + s->slot_pages; i++) {\n"
              "        if (since && !tide_client_slots_walked_since(k, (uint32_t)s->pages[i], since)) continue;\n"
              "        TIDE_CLIENT_COUNT(16);\n"
              "        const uint64_t now = tide_entities_slot_page_hash(&w->entities, (uint32_t)s->pages[i]);\n"
              "        if (now == s->page_hashes[i]) continue;\n"
              "        // Written since in none of the lines its rows' slots are in (other entities came or went there): its rows are\n"
              "        // the entities they were, with their snaps, their entities' column being as it was\n"
              "        const tide_page *slots = s->pages[i] < w->entities.pages ? w->entities.page[s->pages[i]] : NULL;\n"
              "        if (!*ids_rows && s->page_lines[i] && slots && slots->size == (1u << TIDE_ENTITY_PAGE_SHIFT) * (uint32_t)sizeof(tide_entity_slot)\n"
              "            && tide_page_lines_same_since(slots, s->page_hashes[i], s->page_lines[i])) {\n"
              "            TIDE_CLIENT_COUNT(33);\n            s->page_hashes[i] = now;\n            continue;\n        }\n"
              "        bool held = false;\n        if (!*ids_rows) TIDE_CLIENT_TIMED(9, held = tide_client_piece_snaps_same(r, w, t, c, (chunk << c->shift) + at, rows, (uint32_t)s->pages[i], s->page_hashes[i]));\n        if (held) {\n"
              "            s->page_hashes[i] = now;\n            continue;\n        }\n"
              "        *slots_moved = true;\n        same = false;\n        break;\n    }\n"
              "    // Every page's hash is kept as it is now, as of this walk\n"
              "    if (!*heap_moved && !*slots_moved) s->valid = k->walks;\n"
              "    if (!same) TIDE_CLIENT_COUNT(by_run ? 3 : *heap_moved ? 4 : 5);\n"
              "    return same;\n#endif\n}\n\n");
    sb_put(o, "// The hash a kept piece has of one of its heap pages (its items are as of it), or `or` where it hasn't one\n"
              "TIDE_HELPER uint64_t tide_client_piece_page_hash(const tide_client_piece *s, const uint64_t place, const uint64_t or)\n{\n"
              "    if (!s->kept || !s->page_hashes) return or;\n"
              "    uint32_t lo = 0, hi = s->heap_pages;\n"
              "    while (lo < hi) {\n        const uint32_t mid = lo + (hi - lo) / 2u;\n"
              "        if (s->pages[mid] < place) lo = mid + 1u;\n        else hi = mid;\n    }\n"
              "    return lo < s->heap_pages && s->pages[lo] == place ? s->page_hashes[lo] : or;\n}\n\n");
    sb_put(o, "// Whether a row's own blocks and heap pages are as they were (a row never kept, or that read outside the match's\n"
              "// heaps, never is): tide_client_item_same\n"
              "TIDE_HELPER bool tide_client_row_pages_same(tide_client_piece *s, const tide_client_rows *r, const uint32_t row, tide_heaps *heaps)\n{\n"
              "#ifdef TIDE_CLIENT_MEMO_OFF\n    (void)s; (void)r; (void)row; (void)heaps;\n    return false;\n#else\n"
              "    const uint32_t from = r->page_from[row], count = r->page_count[row];\n"
              "    if (!count) return true; // (Nothing of it in the heap)\n"
              "    if (count == UINT32_MAX || !s->row_pages || from > s->row_page_count || count > s->row_page_count - from) return false;\n"
              "    for (uint32_t i = from; i < from + count; i++) {\n"
              "        tide_client_item *it = &s->row_pages[i];\n"
              "        if (!tide_client_item_same_as(heaps, it, tide_client_piece_page_hash(s, tide_client_key_place(it->key), it->page))) return false;\n"
              "    }\n"
              "    return true;\n#endif\n}\n\n");
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
    sb_put(o, "#define TIDE_CLIENT_LINES_TABLE 256u\n");
    sb_put(o, "// The lines of its slots' pages its rows' slots are in (each row's slot kept, tide_client_rows.slot)\n"
              "TIDE_HELPER void tide_client_piece_slot_lines(tide_client_piece *s, const tide_client_rows *r, const uint32_t first, const uint32_t rows)\n{\n"
              "    const uint32_t size = (1u << TIDE_ENTITY_PAGE_SHIFT) * (uint32_t)sizeof(tide_entity_slot);\n"
              "    const uint32_t end = s->heap_pages + s->slot_pages;\n"
              "    uint32_t at = end;\n"
              "    for (uint32_t row = first; row < first + rows; row++) {\n"
              "        if (!(r->flags[row] & TIDE_CLIENT_HASHED) || r->slot[row] == UINT32_MAX) continue;\n"
              "        const uint64_t page = r->slot[row] >> TIDE_ENTITY_PAGE_SHIFT;\n"
              "        if (at == end || s->pages[at] != page) {\n"
              "            for (at = s->heap_pages; at < end && s->pages[at] != page; at++) {\n            }\n"
              "            if (at == end) continue; // (Never so: its page is the piece's)\n        }\n"
              "        s->page_lines[at] |= tide_page_lines(size, (r->slot[row] & ((1u << TIDE_ENTITY_PAGE_SHIFT) - 1u)) * (uint32_t)sizeof(tide_entity_slot),\n"
              "                                             (uint32_t)sizeof(tide_entity_slot));\n"
              "    }\n}\n\n");
    sb_put(o, "// Keeps what a piece's rows were read from, now that they're worked out: its columns' runs (copied where nothing\n"
              "// was kept; the written ones of a kept piece are tide_client_piece_same's already), the union of its rows' heap\n"
              "// pages and its slots' pages, and each row's own pages (`store`, which the piece takes). A piece a row of which\n"
              "// read outside the match's heaps (`poisoned`) isn't kept\n"
              "TIDE_HELPER void tide_client_piece_keep(tide_client_piece *s, const tide_client_rows *r, const tide_world *w, const tide_table *t,\n"
              "                                        const tide_columns *c,\n"
              "                                        const uint32_t chunk, const uint32_t at, const uint32_t rows,\n"
              "                                        tide_heaps *heaps, tide_client_gathered *reached,\n"
              "                                        tide_client_gathered *slots, tide_client_gathered *store, const bool poisoned,\n"
              "                                        const uint64_t walk)\n{\n"
              "#ifndef TIDE_CLIENT_MEMO_OFF // (With nothing kept, nothing is compared with the copy: none is made)\n"
              "    if (!s->kept || s->rows != rows || !s->lanes) {\n"
              "        if (!s->lanes) {\n            s->lanes = malloc(tide_client_runs_bytes(c));\n"
              "            s->runs = malloc((size_t)(c->count ? c->count : 1u) * sizeof *s->runs);\n"
              "            if (!s->lanes || !s->runs) tide_out_of_memory();\n        }\n"
              "        size_t from = 0;\n"
              "        for (uint32_t k = 0; k < c->count; from += (size_t)c->sizes[k] << TIDE_CLIENT_PIECE_SHIFT, k++) {\n"
              "            s->runs[k] = 0;\n"
              "            if (!c->sizes[k]) continue;\n"
              "            const uint8_t *now = tide_client_run(t, c, chunk, k, at);\n"
              "            memcpy(s->lanes + from, now, (size_t)rows * c->sizes[k]);\n"
              "            s->runs[k] = tide_hash_wide(now, (size_t)rows * c->sizes[k]);\n        }\n    }\n"
              "#else\n    (void)t; (void)c; (void)chunk; (void)at;\n#endif\n"
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
              "        s->page_lines = realloc(s->page_lines, (size_t)s->page_room * sizeof *s->page_lines);\n"
              "        if (!s->pages || !s->page_hashes || !s->page_lines) tide_out_of_memory();\n    }\n"
              "    if (heap_pages + slot_pages) memset(s->page_lines, 0, (size_t)(heap_pages + slot_pages) * sizeof *s->page_lines);\n"
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
              "    s->kept = whole;\n    s->valid = whole ? walk : 0u;\n");
    sb_put(o, "    // The lines each page's blocks are in, which are in one heap page's place (a page's whole: all of them)\n"
              "    // (Each page's number found by a table on the stack where there are a few hundred, else by halves)\n"
              "    uint16_t numbers[TIDE_CLIENT_LINES_TABLE]; // Its number + 1 by its place, 0 for an empty slot\n"
              "    const bool table = whole && heap_pages <= TIDE_CLIENT_LINES_TABLE / 2u;\n"
              "    if (table) {\n        memset(numbers, 0, sizeof numbers);\n"
              "        for (uint32_t i = 0; i < heap_pages; i++) {\n"
              "            uint32_t to = (uint32_t)tide_client_mix(s->pages[i]) & (TIDE_CLIENT_LINES_TABLE - 1u);\n"
              "            while (numbers[to]) to = (to + 1u) & (TIDE_CLIENT_LINES_TABLE - 1u);\n"
              "            numbers[to] = (uint16_t)(i + 1u);\n        }\n    }\n"
              "    uint32_t lo = 0; // (A row's blocks are mostly in the page the one before's was)\n"
              "    for (uint32_t k = 0; whole && k < store->count; k++) {\n"
              "        const uint64_t key = store->items[k].key, place = tide_client_key_place(key);\n"
              "        if (lo >= heap_pages || s->pages[lo] != place) {\n"
              "            if (table) {\n"
              "                uint32_t to = (uint32_t)tide_client_mix(place) & (TIDE_CLIENT_LINES_TABLE - 1u);\n"
              "                while (numbers[to] && s->pages[numbers[to] - 1u] != place) to = (to + 1u) & (TIDE_CLIENT_LINES_TABLE - 1u);\n"
              "                if (!numbers[to]) continue; // (Never so: its page is the piece's)\n"
              "                lo = numbers[to] - 1u;\n"
              "            } else {\n"
              "                uint32_t hi = heap_pages;\n                lo = 0;\n"
              "                while (lo < hi) {\n                    const uint32_t mid = lo + (hi - lo) / 2u;\n"
              "                    if (s->pages[mid] < place) lo = mid + 1u;\n                    else hi = mid;\n                }\n"
              "                if (lo == heap_pages || s->pages[lo] != place) continue;\n            }\n        }\n"
              "        // (A page of its heap is 64 lines of 256 bytes: tide_page_lines)\n"
              "        const uint32_t tag = (uint32_t)(key >> 10) & 15u, from = (uint32_t)(key & 1023u) << 4;\n"
              "        s->page_lines[lo] |= tag ? tide_page_lines(1u << TIDE_HEAP_PAGE_SHIFT, from, 16u << (tag - 1u)) : TIDE_PAGE_ALL_LINES;\n    }\n"
              "    tide_client_piece_slot_lines(s, r, (chunk << c->shift) + at, rows);\n"
              "    // Its rows' own pages: the store's, and the store has the piece's old arrays to fill next time\n"
              "    tide_client_item *items = s->row_pages;\n    const uint32_t room = s->row_page_room;\n"
              "    s->row_pages = store->items;\n    s->row_page_count = store->count;\n    s->row_page_room = store->room;\n"
              "    store->items = items;\n    store->room = room;\n    store->count = 0;\n}\n\n");
    sb_put(o, "// A piece read again keeps its own arrays (its rows' blocks, its pages) while no row of it is read from the heap\n"
              "// again or is another entity than it was: a row whose small lanes changed (who has it) moves its terms where they\n"
              "// are, and the rows nothing of which changed aren't looked at. Whether a row is the entity it was, with the snaps\n"
              "// it had, where a slot's page of the piece's changed: its slot says so (its entity's and its scene's columns are\n"
              "// compared with the piece's copy)\n"
              "TIDE_HELPER bool tide_client_row_slot_same(const tide_client_rows *r, const tide_world *w, const uint32_t row, const tide_entity e)\n{\n"
              "    if (r->slot[row] == UINT32_MAX) return false;\n"
              "    const tide_entity_slot *slot = tide_entity_slot_of(&w->entities, r->slot[row]);\n"
              "    return memcmp(&slot->entity, &e, sizeof e) == 0 && slot->snaps == r->snaps[row];\n}\n\n"
              "// ...and the first row that is read again, or is another entity, has the piece's arrays made again: the rows\n"
              "// before it (`from` up to `to`), which are as they were, give the blocks they had and their slots' pages. True\n"
              "// where one of them can't be kept by its pages (never so: the piece was kept)\n"
              "TIDE_HELPER bool tide_client_piece_rows_again(tide_client_gathered *store, tide_client_gathered *reached, tide_client_gathered *slots,\n"
              "                                              tide_client_rows *r, const tide_client_piece *s, const uint32_t from, const uint32_t to,\n"
              "                                              tide_heaps *heaps)\n{\n"
              "    bool poisoned = false;\n"
              "    for (uint32_t row = from; row < to; row++) {\n"
              "        if (!(r->flags[row] & TIDE_CLIENT_HASHED)) continue; // A hole\n"
              "        tide_client_row_keep_kept(store, r, row, s, heaps);\n"
              "        if (r->page_count[row] == UINT32_MAX) poisoned = true;\n"
              "        else if (r->page_count[row]) tide_client_gather_pages(reached, store->items + r->page_from[row], r->page_count[row]);\n"
              "        if (r->slot[row] == UINT32_MAX) poisoned = true;\n"
              "        else {\n            const uint64_t slot = r->slot[row] >> TIDE_ENTITY_PAGE_SHIFT;\n"
              "            tide_client_gather_numbers(slots, &slot, 1u);\n        }\n    }\n"
              "    return poisoned;\n}\n\n"
              "// ...and a piece whose arrays stood keeps its slots' pages as they are now: every row of it is the entity it\n"
              "// was, with its snaps (its columns' runs are tide_client_piece_same's already), and its heap pages' were each found\n"
              "// as they are (tide_client_piece_same went through them all): every one of its pages' hashes is as of this walk\n"
              "TIDE_HELPER void tide_client_piece_keep_slots(tide_client_piece *s, const tide_world *w, const uint64_t walk)\n{\n"
              "    for (uint32_t i = s->heap_pages; i < s->heap_pages + s->slot_pages; i++)\n"
              "        s->page_hashes[i] = tide_entities_slot_page_hash(&w->entities, (uint32_t)s->pages[i]);\n"
              "    s->valid = walk;\n"
              "    TIDE_CLIENT_COUNT(19);\n}\n\n");
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
                 "    bool hashed = chunks > 0;\n    tide_list_cache cache = {0};\n"
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
                 "            const entry *now = tide_list_cached_at(&cache, l, (int32_t)i, sizeof *now);\n"
                 "            if (!now) continue;\n"
                 "            if (i < had && memcmp(&kept[i], now, sizeof *now) == 0) continue;\n",
              groups);
    sb_put(o,
                 "            if (i < had && kept[i].tide_order > 0) tide_client_changed_add(k, kept[i].key); // Gone, or its audience another\n"
                 "            if (now->tide_order > 0) {\n"
                 "                tide_client_changed_add(k, now->key); // New, or its audience another\n"
                 "                // Who it came to: what a group's entities are now theirs that weren't. (An entry that moved as the\n"
                 "                // map packed counts whole: said of more players, never of fewer)\n"
                 "                const bool was = i < had && kept[i].tide_order > 0 && kept[i].key == now->key;\n"
                 "                const uint8_t *to = (const uint8_t *)&now->value, *from = (const uint8_t *)&kept[i].value;\n"
                 "                uint8_t *came = (uint8_t *)&k->came;\n"
                 "                for (size_t at = 0; at < sizeof(Audience); at++) came[at] |= (uint8_t)(to[at] & (was ? ~from[at] : 0xFFu));\n"
                 "            }\n"
                 "            kept[i] = *now;\n"
                 "        }\n    }\n"
                 "    for (uint32_t i = n; i < had; i++) if (kept[i].tide_order > 0) tide_client_changed_add(k, kept[i].key);\n"
                 "    k->group_count = n;\n    k->group_list = hashed ? l.at : 0u;\n    k->chunk_count = chunks;\n"
                 "    if (k->changed_count > 1u) {\n"
                 "        qsort(k->changed, k->changed_count, sizeof *k->changed, tide_client_id_order);\n"
                 "        uint32_t unique = 0;\n"
                 "        for (uint32_t i = 0; i < k->changed_count; i++) if (!unique || k->changed[unique - 1u] != k->changed[i]) k->changed[unique++] = k->changed[i];\n"
                 "        k->changed_count = unique;\n    }\n}\n\n");
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

// Where component `c` is among an archetype's components that can be denied (`deniable`, `n` of them), or -1
static int client_deniable_at(const int *deniable, const int n, const int c)
{
    for (int k = 0; k < n; k++) {
        if (deniable[k] == c) return k;
    }
    return -1;
}

// A term worked out again (`now`, against term[t]) that came out another, for the repairs: a component that can be
// denied (`k` among them) is another for those who may have it, any other for everyone who has the row
static void gen_client_term_moved(sb *o, const int t, const int k, const char *pad)
{
    if (k >= 0) sb_printf(o, "%sif (now != term[%d]) parts_moved |= 1ull << %d;\n", pad, t, k);
    else sb_printf(o, "%sif (now != term[%d]) whole_moved = true;\n", pad, t);
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
    const bool masked = client_changes_masked(prog);
    // What changed is written down for the repairs: by a server's check with its world's memo
    sb_put(o, "    tide_client_journal *const journal = &kept->journal;\n"
              "    const bool journaling = !own && !client && !again;\n"
              "    tide_client_journal_begin(journal, journaling, 0u");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (!arch_local(g, a)) sb_printf(o, " + (uint64_t)w->%s.count", arch_name(g, a));
    }
    sb_put(o, ");\n");
    sb_printf(o, "    // Each entity's hash and what it has: again only for what changed\n"
                 "    const uint32_t call = ++kept->call;\n    (void)call;\n"
                 "    // Which pages changed since the check before (the pieces go by it)\n"
                 "#ifndef TIDE_CLIENT_MEMO_OFF\n    TIDE_CLIENT_TIMED(15, tide_client_walk(kept, hs->heaps, &w->entities));\n#endif\n"
                 "    tide_client_gathered reached = {0}, slots = {0}, store = {0};\n"
                 "    // A scene's root came, went, or has another Visibility: every entity of its scene goes by its group\n"
                 "    bool roots_fresh = false;\n"
                 "    bool written[%d];\n"
                 "    uint64_t changed[%d]; // For each component of a piece's archetype, the rows of the piece it changed in\n",
              most_columns, most_parts);
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
                     "            bool heap_moved = true, slots_moved = true;\n            uint64_t ids_rows = UINT64_MAX;\n            piece->fresh = false;\n"
                     "            if (again) {\n                for (uint32_t i = 0; i < %du; i++) changed[i] = UINT64_MAX;\n            }\n"
                     "            else if (tide_client_piece_same(piece, kept%d, w, &w->%s, %s, chunk, at, count, moved, hs->heaps, tide_client_part_of%d, %du,\n"
                     "                                            changed, &ids_rows, &heap_moved, &slots_moved, kept)) {\n                TIDE_CLIENT_MARK(5);\n                continue;\n            }\n            TIDE_CLIENT_MARK(5);\n"
                     "            piece->routed = false;\n            reached.count = slots.count = store.count = 0;\n            bool poisoned = false, any_fresh = false;\n"
                     "            // Its arrays stand while its rows read nothing from the heap again and are the entities they were (a\n"
                     "            // piece with nothing kept, or a heap page of whose rows changed, says heap_moved: made again), and\n"
                     "            // while they do, only the rows something changed in are looked at (every one, where a slot's page did)\n"
                     "            bool inplace = !again && !heap_moved;\n"
                     "            uint64_t look = slots_moved ? UINT64_MAX : ids_rows;\n"
                     "            for (uint32_t i = 0; i < %du; i++) look |= changed[i];\n"
                     "            for (uint32_t row = first + at; row < first + at + count; row++) {\n"
                     "                const uint32_t bit = row - (first + at);\n"
                     "                if (inplace && !(look >> bit & 1u)) continue;\n",
                  table, columns, table, columns, a, table, columns, (unsigned)(m ? m : 1), a, table, columns, a, (unsigned)(m ? m : 1),
                  (unsigned)(m ? m : 1));
        sb_printf(o, "                if (tide_row_gone(%s)) { // A hole (tide_table_remove): nothing kept of it\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        if (root) sb_printf(o, "                    if (kept%d->flags[row]) roots_fresh = true;\n", a);
        // (The entity that was there went: whoever had it)
        if (masked) sb_printf(o, "                    if (kept%d->flags[row]) tide_client_journal_row(journal, %du, row, 0u, true, TIDE_CLIENT_WAS(kept%d, row));\n", a, a, a);
        else sb_printf(o, "                    if (kept%d->flags[row]) tide_client_journal_row(journal, %du, row, 0u, true);\n", a, a);
        sb_printf(o, "                    kept%d->flags[row] = 0;\n                    kept%d->page_count[row] = UINT32_MAX;\n                    continue;\n                }\n",
                  a, a);
        sb_printf(o, "                const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "                const tide_location loc = {%du, row};\n                uint64_t mask = 0;\n                TIDE_CLIENT_COUNT(6);\n", a);
        for (int k = 0; k < n; k++) {
            if (comp_sparse(g, deniable[k])) sb_printf(o, "                if (%s) mask |= 1ull << %d;\n", presence_cell(g, "w", a, deniable[k], "row", false), k);
            else sb_printf(o, "                mask |= 1ull << %d;\n", k);
        }
        // What says which entity a row is is hashed again where its entity's or its scene's column changed in the row, or
        // a slot's page of the piece's changed and the row's slot says another entity, or other snaps
        sb_printf(o, "                const bool all = again || !(kept%d->flags[row] & TIDE_CLIENT_HASHED);\n"
                     "                uint64_t *const term = &terms%d[(size_t)row * %du];\n"
                     "                bool fresh = all;\n"
                     "                uint64_t moved_by = 0; // What the terms worked out again add to its sum\n"
                     "                const bool ids_again = all || (ids_rows >> bit & 1u) || (slots_moved && !tide_client_row_slot_same(kept%d, w, row, e));\n"
                     "                // For the repairs: whether anything of it every recipient's part goes by came out another, and\n"
                     "                // which of the components that can be denied had their terms move\n"
                     "                bool whole_moved = ids_again;\n                uint64_t parts_moved = 0;\n",
                  a, a, (unsigned)(m ? m : 1), a);
        if (root) sb_put(o, "                if (all) roots_fresh = true;\n");
        // Each component: whether a lane of it (or its presence byte, for a sparse one) changed in the row
        bool any_heapy = false;
        for (int t = 0; t < m; t++) {
            const int c = parts[t];
            const decl *d = prog->components.items[c];
            const bool heapy = client_codec_heapy(prog, (type){TY_COMPONENT, (decl *)d}, 0);
            any_heapy |= heapy;
            sb_printf(o, "                const bool lanes%d = all || (changed[%d] >> bit & 1u);\n", t, t);
        }
        // Who may have it is worked out again (`seen`) only where what that goes by in the row changed: its scene (its
        // entity's and scene's columns), its Visibility (lanes or policies), or which components it has; a row whose
        // other values changed has the audience it had (its groups' audiences are the directory's: tide_client_groups_diff)
        sb_put(o, "                bool seen = ids_again;\n");
        for (int t = 0; t < m; t++) {
            if (parts[t] == prog->visibility->index) sb_printf(o, "                if (lanes%d) seen = true;\n", t);
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
                    sb_printf(o, " || lanes%d", t);
            }
            sb_printf(o, ";\n                if (!heap_again && heap_moved && !tide_client_row_pages_same(piece, kept%d, row, hs->heaps)) {\n"
                         "                    heap_again = true;\n                    TIDE_CLIENT_COUNT(8);\n                }\n"
                         "                if (inplace && (heap_again || ids_again)) {\n                    inplace = false;\n"
                         "                    poisoned |= tide_client_piece_rows_again(&store, &reached, &slots, kept%d, piece, first + at, row, hs->heaps);\n"
                         "                }\n", a, a);
            sb_put(o, "                if (heap_again) {\n                    TIDE_CLIENT_COUNT(7); TIDE_CLIENT_FROM(read_from);\n"
                      "                    hs->reached = 0;\n                    hs->floor = 0;\n                    hs->poisoned = false;\n");
            for (int t = 0; t < m; t++) {
                const int c = parts[t];
                if (!client_codec_heapy(prog, (type){TY_COMPONENT, prog->components.items[c]}, 0)) continue;
                sb_printf(o, "                    {\n                        const uint64_t now = tide_client_term%d_%d(w, e, loc, %s, hs);\n",
                          a, c, comp_sparse(g, c) ? presence_cell(g, "w", a, c, "row", false) : "true");
                if (t == root_visibility) sb_printf(o, "                        if (now != term[%d] || lanes%d) roots_fresh = true;\n", t, t);
                gen_client_term_moved(o, t, client_deniable_at(deniable, n, c), "                        ");
                sb_printf(o, "                        moved_by += now - term[%d];\n                        term[%d] = now;\n                    }\n",
                          t, t);
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
                             "                        if (policies != kept%d->policies[row]) fresh = seen = true;\n"
                             "                        kept%d->policies[row] = policies;\n                    }\n",
                          a, a);
            }
            // (Fresh where a term or a policy came out another, or the row is new: its sum says the first)
            sb_printf(o, "                    if (hs->poisoned) kept%d->page_count[row] = UINT32_MAX;\n"
                         "                    else tide_client_row_keep(&store, kept%d, row, hs->reach, hs->reached, hs->heaps); TIDE_CLIENT_TO(13, read_from);\n"
                         "                } else if (!inplace) tide_client_row_keep_kept(&store, kept%d, row, piece, hs->heaps);\n"
                         "                // (A row whose pages aren't all kept leaves its piece read again each call)\n"
                         "                if (!inplace && kept%d->page_count[row] == UINT32_MAX) poisoned = true;\n"
                         "                else if (!inplace && kept%d->page_count[row]) tide_client_gather_pages(&reached, store.items + kept%d->page_from[row], kept%d->page_count[row]);\n",
                      a, a, a, a, a, a, a);
        } else {
            sb_printf(o, "                if (inplace && ids_again) {\n                    inplace = false;\n"
                         "                    poisoned |= tide_client_piece_rows_again(&store, &reached, &slots, kept%d, piece, first + at, row, hs->heaps);\n"
                         "                }\n"
                         "                kept%d->page_count[row] = 0;\n                kept%d->page_from[row] = store.count;\n", a, a, a);
        }
        // ...and what doesn't, each on its own
        for (int t = 0; t < m; t++) {
            const int c = parts[t];
            if (client_codec_heapy(prog, (type){TY_COMPONENT, prog->components.items[c]}, 0)) continue;
            sb_printf(o, "                if (lanes%d) {\n"
                         "                    const uint64_t now = tide_client_term%d_%d(w, e, loc, %s, &hs->out);\n",
                      t, a, c, comp_sparse(g, c) ? presence_cell(g, "w", a, c, "row", false) : "true");
            gen_client_term_moved(o, t, client_deniable_at(deniable, n, c), "                    ");
            sb_printf(o, "                    moved_by += now - term[%d];\n                    term[%d] = now;\n"
                         "                    fresh = true;\n",
                      t, t);
            if (t == root_visibility) sb_put(o, "                    roots_fresh = true;\n");
            sb_put(o, "                }\n");
        }
        // Its sum: what says which entity it is and its terms; where that's as it was, the sum it had and what its
        // terms worked out again moved by (a kept row's sum is always its own: wrapping sums are exact)
        sb_printf(o, "                uint64_t sum = has%d[row * 2u + 1u] + moved_by;\n"
                     "                if (ids_again) {\n"
                     "                    sum = tide_client_entity_id%d(w, e, loc, &hs->out);\n"
                     "                    kept%d->slot[row] = tide_entity_slot_index(&w->entities, e);\n"
                     "                    kept%d->snaps[row] = tide_entity_slot_of(&w->entities, kept%d->slot[row])->snaps;\n", a, a, a, a, a);
        for (int t = 0; t < m; t++) sb_printf(o, "                    sum += term[%d];\n", t);
        // (A row whose entity has no slot here has no slot page to keep it by: its piece isn't kept, as one that read
        // outside the heaps isn't, so the slot it gets later is seen)
        sb_printf(o, "                }\n"
                     "                if (!inplace && kept%d->slot[row] == UINT32_MAX) poisoned = true;\n"
                     "                else if (!inplace) {\n"
                     "                    const uint64_t slot = kept%d->slot[row] >> TIDE_ENTITY_PAGE_SHIFT;\n"
                     "                    tide_client_gather_numbers(&slots, &slot, 1u);\n                }\n", a, a);
        // (Which of those components it has, changing, is their terms' and their audiences' to say: a term moves, and who
        // may have each is worked out again below)
        if (masked) sb_printf(o, "                if (whole_moved || parts_moved) tide_client_journal_row(journal, %du, row, parts_moved, whole_moved, TIDE_CLIENT_WAS(kept%d, row));\n", a, a);
        else sb_printf(o, "                if (whole_moved || parts_moved) tide_client_journal_row(journal, %du, row, parts_moved, whole_moved);\n", a);
        sb_printf(o, "                if (sum != has%d[row * 2u + 1u]) fresh = true;\n"
                     "                if (mask != has%d[row * 2u]) fresh = seen = true;\n"
                     "                has%d[row * 2u] = mask;\n                has%d[row * 2u + 1u] = sum;\n"
                     "                kept%d->flags[row] |= TIDE_CLIENT_HASHED;\n"
                     "                if (fresh) TIDE_CLIENT_COUNT(11);\n"
                     "                if (seen) {\n                    kept%d->fresh[row] = call;\n                    any_fresh = true;\n                }\n"
                     "            }\n",
                  a, a, a, a, a, a);
        sb_printf(o, "            TIDE_CLIENT_MARK(6);\n"
                     "            if (inplace) tide_client_piece_keep_slots(piece, w, kept->walks);\n"
                     "            else tide_client_piece_keep(piece, kept%d, w, &w->%s, %s, chunk, at, count, hs->heaps, &reached, &slots, &store, poisoned, kept->walks);\n"
                     "            piece->fresh = any_fresh;\n            TIDE_CLIENT_MARK(7);\n"
                     "        }\n"
                     "        tide_client_span_kept(span, &w->%s, %s, chunk, here, pieces);\n"
                     "    }\n",
                  a, table, columns, table, columns);
        // What's kept of rows and chunks past the table's end now is nothing: they were worked out under groups no call
        // since has looked at, and a table that grows again reads them afresh
        sb_printf(o, "    for (uint32_t chunk = w->%s.chunks; chunk < kept%d->span_room; chunk++) tide_client_span_forget(&kept%d->spans[chunk]);\n"
                     "    for (uint32_t row = rows%d; row < kept%d->was && row < kept%d->room; row++) {\n",
                  table, a, a, a, a, a);
        if (root) sb_printf(o, "        if (kept%d->flags[row]) roots_fresh = true;\n", a);
        if (masked) sb_printf(o, "        if (kept%d->flags[row]) tide_client_journal_row(journal, %du, row, 0u, true, TIDE_CLIENT_WAS(kept%d, row));\n", a, a, a);
        else sb_printf(o, "        if (kept%d->flags[row]) tide_client_journal_row(journal, %du, row, 0u, true);\n", a, a);
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
                     "                const Audience seen_was = audience%d[row];\n"
                     "                audience%d[row] = !client ? tide_visibility_base_at(w, e, loc, &policy, &has_policy, &walk, groups)\n"
                     "                    : tide_visibility_client_hidden(w, e) ? (Audience){0} : TIDE_AUDIENCE_ALL;\n"
                     "                kept%d->flags[row] = (uint8_t)((flags & TIDE_CLIENT_HASHED) | TIDE_CLIENT_SEEN\n"
                     "                    | (!client && has_policy && tide_visibility_by_groups(&policy) ? TIDE_CLIENT_BY_GROUP : 0u));\n"
                     "                tide_client_piece_groups_add(piece, kept%d->flags[row], groups);\n",
                  a, a, a, a, a);
        // For the repairs: the players it came to or left, or a component of it did. (A row never seen is written down
        // already, as its terms were worked out: who has it now is added to it once every row's audience is known)
        if (masked) sb_printf(o, "                Audience who = tide_client_audience_xor(seen_was, audience%d[row]);\n", a);
        else sb_printf(o, "                bool who = journal->on && (flags & TIDE_CLIENT_SEEN) && !tide_audience_equal(seen_was, audience%d[row]);\n", a);
        for (int k = 0; k < n; k++) {
            sb_printf(o, "                {\n                    const Audience to = client || !((has%d[row * 2u] >> %d) & 1u) ? TIDE_AUDIENCE_ALL\n"
                         "                        : tide_visibility_denied_at(w, &policy, has_policy, audience%d[row], %du);\n",
                      a, k, a, (unsigned)deniable[k]);
            if (masked) sb_printf(o, "                    who = tide_client_audience_or(who, tide_client_audience_xor(denied%d[row * %du + %du], to));\n", a, (unsigned)n, (unsigned)k);
            else sb_printf(o, "                    who = who || (journal->on && (flags & TIDE_CLIENT_SEEN) && !tide_audience_equal(denied%d[row * %du + %du], to));\n", a, (unsigned)n, (unsigned)k);
            sb_printf(o, "                    denied%d[row * %du + %du] = to;\n                }\n", a, (unsigned)n, (unsigned)k);
        }
        if (masked) sb_printf(o, "                if ((flags & TIDE_CLIENT_SEEN) && tide_client_audience_any(who)) tide_client_journal_add(journal, %du, row, who);\n", a);
        else sb_printf(o, "                if (who) tide_client_journal_add(journal, %du, row);\n", a);
        sb_put(o, "            }\n        }\n    }\n");
    }
    // The rows written down as their terms were worked out, now that who may have each is: the players each could be
    // another for. Then the check is a node of the journal, by the world's hash
    sb_put(o, "    for (uint32_t i = 0; journal->on && !journal->lost && i < journal->pending_count; i++) {\n"
              "        const tide_client_pending *c = &journal->pending[i];\n        switch (c->archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const int n = client_deniable(g, a, deniable);
        sb_printf(o, "        case %d: {\n", a);
        if (masked) {
            sb_printf(o, "            const bool here = c->row < rows%d && (kept%d->flags[c->row] & TIDE_CLIENT_SEEN);\n"
                         "            const Audience now = here ? audience%d[c->row] : (Audience){0};\n"
                         "            Audience who = c->whole ? tide_client_audience_or(c->was, now) : tide_client_audience_xor(c->was, now);\n",
                      a, a, a);
            // (A term that moved is another's who may have that component, and nobody else's)
            for (int k = 0; k < n; k++) {
                sb_printf(o, "            if (here && (c->parts >> %d & 1u)) who = tide_client_audience_or(who, denied%d[c->row * %du + %du]);\n",
                          k, a, (unsigned)n, (unsigned)k);
            }
            sb_printf(o, "            if (tide_client_audience_any(who)) tide_client_journal_add(journal, %du, c->row, who);\n", a);
        } else sb_printf(o, "            tide_client_journal_add(journal, %du, c->row);\n", a);
        sb_put(o, "            break;\n        }\n");
    }
    sb_put(o, "        default: break;\n        }\n    }\n"
              "    if (journaling) {\n        uint64_t node;\n        TIDE_CLIENT_TIMED(14, node = tide_world_hash(w));\n"
              "        tide_client_journal_end(journal, node);\n    }\n"
              "    TIDE_CLIENT_MARK(3);\n");
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
