// ---------------------------------------------------------------------------
// `previous` bindings: a query reading components as the tick started
// (docs/spec.md, `previous` in a query; notes/schedule-and-threads.md)
//
// A component some system's query binds `previous` (decl.kept_previous) is
// kept for the length of the tick's systems: before they run, on one thread,
// tide_previous_keep takes a reference to each page of its lanes in every
// archetype that stores it, with the archetype's entity column and presence
// bytes (tide_table_keep), and a copy of the heap its text and lists are in
// (tide_heaps_keep), which shares its pages. Whoever writes one of those
// pages copies it first, as beside any snapshot, so what was kept is what the
// tick started with for as long as it's kept: until the systems have joined
// (tide_previous_release), before the tick's queued changes apply.
//
// It's beside the world, never of it: a pointer the world's struct ends with,
// which copies, hashes, packs and snapshots leave out, as they do the loops'
// indexes (index_codegen.inc).
//
// While systems run no row moves (structural changes are queued), so a row of
// the world's table is the same row of the kept one: a query's loop goes
// through the world as it is and reads a `previous` binding's lanes from the
// kept table instead. What such a value holds in a heap is read from the kept
// heap: its handles are retagged as a `verified` value's are (tide_confirmed,
// TIDE_IN_VERIFIED), and each of the tick's threads says the kept heaps are
// what that names (tide_text_use_verified), which a tick has no other use for.

// Whether archetype `a` (the match's) stores a component the tick keeps.
static bool arch_keeps(const gen *g, const int a)
{
    if (!g->prog->keeps_previous || arch_local(g, a)) return false;
    for (int c = 0; c < g->prog->components.count; c++) {
        if (g->prog->components.items[c]->kept_previous && arch_stores(g, a, c)) return true;
    }
    return false;
}

// Whether the tick keeps a heap: some kept component holds text, lists, maps, sets or grids.
static bool previous_heap(const gen *g)
{
    if (!g->prog->keeps_previous || !g->prog->uses_heap) return false;
    for (int c = 0; c < g->prog->components.count; c++) {
        const decl *d = g->prog->components.items[c];
        if (d->kept_previous && decl_has_text(d)) return true;
    }
    return false;
}

// Whether binding `i` of query `q` reads the tick's start: its own word, or, for a query a function was handed,
// its source's binding of the same component.
static bool query_binding_previous(const param *q, const param *selection, const int i)
{
    const param *p = &q->query.items[i];
    if (!param_binding(p)) return false;
    if (selection == q || !selection) return p->previous;
    for (int k = 0; k < selection->query.count; k++) {
        const param *from = &selection->query.items[k];
        if (param_binding(from) && from->type.decl == p->type.decl) return from->previous;
    }
    return false;
}

// The kept table of archetype `a` in world `world`, as C.
static const char *previous_table(const gen *g, const char *world, const int a)
{
    sb b = {0};
    sb_printf(&b, "&%s->tide_previous->%s", world, arch_name(g, a));
    return b.data;
}

// As many spaces as the code being written is indented by, for generators that take their padding as text.
static const char *indent_pad(const gen *g)
{
    sb b = {0};
    for (int i = 0; i < g->indent; i++) sb_put(&b, "    ");
    sb_put(&b, "");
    return b.data ? b.data : "";
}

static void gen_confirmed_copy(sb *o, const decl *d, const char *value, const char *pad);

// After a row's `previous` binding was read into `value`: what it holds in a heap is the kept heap's.
static void gen_previous_value(gen *g, const decl *comp, const char *value)
{
    if (!decl_has_text(comp)) return;
    gen_confirmed_copy(&g->c, comp, value, indent_pad(g));
}

// The struct, and keeping and letting go, once the archetypes' columns are written.
static void gen_previous_helpers(gen *g)
{
    const program *prog = g->prog;
    if (!prog->keeps_previous) return;
    sb *o = &g->c;
    const bool heap = previous_heap(g);
    // What the struct holds, as a number: a world carried over to new code (hot reload) may come with one the code
    // before made, of other tables
    sb shape = {0};
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_keeps(g, a)) sb_printf(&shape, "%s %d;", arch_name(g, a), arch_column_count(g, a));
    }
    sb_printf(&shape, "heap %d", heap);
    uint64_t plan = 0xCBF29CE484222325ull;
    for (const char *c = shape.data; *c; c++) plan = (plan ^ (uint8_t)*c) * 0x100000001B3ull;
    sb_put(o, "// Components as the tick started, for queries' `previous` bindings: pages kept while the tick's systems run\n"
              "struct tide_previous {\n    uint64_t plan; // Which tables these are\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_keeps(g, a)) sb_printf(o, "    tide_table %s;\n", arch_name(g, a));
    }
    if (heap) sb_put(o, "    tide_heaps heap; // ...and the heaps their text and lists are in, as they were\n");
    sb_put(o, "    bool kept;\n};\n\n");

    sb_put(o, "// Serial, before the tick's systems: what they read as the tick started\n"
              "TIDE_HELPER void tide_previous_keep(tide_world *w)\n{\n"
              "    struct tide_previous *p = w->tide_previous;\n");
    // Another build's holds no page between ticks, only its tables' room, which goes with it unknown
    sb_printf(o, "    if (p && p->plan != %lluull) {\n        free(p);\n        p = NULL;\n    }\n"
                 "    if (!p) {\n        w->tide_previous = p = tide_alloc_zeroed(1u, sizeof *p);\n        p->plan = %lluull;\n    }\n",
              (unsigned long long)plan, (unsigned long long)plan);
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (!arch_keeps(g, a)) continue;
        sb_printf(o, "    {\n        static const uint32_t tide_kept[] = {0u");
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            if (comp_sparse(g, c)) sb_printf(o, ", %du", arch_presence(g, a, c));
            if (!d->kept_previous) continue;
            for (int k = 0; k < comp_lanes(d); k++) sb_printf(o, ", %du", arch_column(g, a, c) + k);
        }
        sb_printf(o, "};\n        tide_table_keep(&p->%s, &w->%s, %s, tide_kept, (uint32_t)(sizeof tide_kept / sizeof *tide_kept));\n    }\n",
                  arch_name(g, a), arch_name(g, a), arch_columns(g, a));
    }
    if (heap) {
        // Each kept owner's heap, once: those C can write, and every owner of a game of one heap, are in the match's
        uint32_t ids[64];
        int count = 0;
        for (int c = 0; c < prog->components.count; c++) {
            const decl *d = prog->components.items[c];
            if (!d->kept_previous || !decl_has_text(d)) continue;
            bool seen = false;
            for (int k = 0; k < count; k++) seen |= ids[k] == (uint32_t)d->heap_id;
            if (seen || count == 64) continue;
            ids[count++] = (uint32_t)d->heap_id;
            sb_printf(o, "    tide_heaps_keep(&p->heap, &w->heap, %uu); // %s\n", (unsigned)d->heap_id, type_cname(d));
        }
        sb_put(o, "    tide_text_use_verified(&p->heap);\n");
    }
    sb_put(o, "    p->kept = true;\n}\n\n");

    sb_put(o, "// Serial, once the systems have joined: the pages are the world's alone again\n"
              "TIDE_HELPER void tide_previous_release(tide_world *w, const bool all)\n{\n"
              "    struct tide_previous *p = w->tide_previous;\n");
    sb_printf(o, "    if (!p) return;\n    if (p->plan != %lluull) { // Another build's (a reload): nothing of it is known\n"
                 "        if (all) {\n            free(p);\n            w->tide_previous = NULL;\n        }\n        return;\n    }\n",
              (unsigned long long)plan);
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_keeps(g, a)) sb_printf(o, "    tide_table_unkeep(&p->%s, %s, all);\n", arch_name(g, a), arch_columns(g, a));
    }
    if (heap) sb_put(o, "    if (p->kept || all) tide_heaps_free(&p->heap);\n");
    sb_put(o, "    p->kept = false;\n"
              "    if (all) {\n        free(p);\n        w->tide_previous = NULL;\n    }\n}\n\n");
}
