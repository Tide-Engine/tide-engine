// A cache belongs to one world, but none of its derived bytes is simulation
// state. Retaining presence pages makes pointer identity an exact proof of
// unchanged membership. Only the tick's caller changes references or plans.
static bool rows_cacheable(const gen *g, const decl *sys, const int a)
{
    return sys->per_entity && !sys->extern_system && !arch_local(g, a) && runs_on(g, sys, a)
        && !rows_batched(g, sys, a) && rows_selectable(g, sys, a) && !scene_filtered(g, sys, a)
        && arch_shift(g, a) <= 11;
}

// A build's ordinals are lookup hints, never cache validity: a same-layout
// reload can change either a system's filters or its place in this list.
static int rows_cache_id(const gen *g, const decl *wanted, const int archetype)
{
    int id = 0;
    for (int s = 0; s < g->prog->systems.count; s++) {
        const decl *sys = g->prog->systems.items[s];
        for (int a = 0; a < g->prog->archetypes.count; a++) {
            if (!rows_cacheable(g, sys, a)) continue;
            if (sys == wanted && a == archetype) return id;
            id++;
        }
    }
    return id;
}

static int rows_cache_count(const gen *g)
{
    return rows_cache_id(g, NULL, -1);
}

static bool gen_rows_cache_prefetchable(const gen *g, const decl *sys, const int a)
{
    int bytes = 0;
    bool writes = false;
    for (int c = 0; c < g->prog->components.count; c++) {
        if (!rows_take(g, sys, a, c)) continue;
        const int size = decl_layout(g->prog->components.items[c]).size;
        if (size > 128 - bytes) return false;
        bytes += size;
        writes |= changes_component(sys, c, false);
    }
    return writes && bytes >= 32;
}

static void gen_rows_cache_prefetch(gen *g, const decl *sys, const int a, const bool warm)
{
    sb *o = &g->c;
    sb_put(o, "#if !defined(__wasm__)\n");
    if (warm) sb_put(o, "    const bool tide_prefetch = tide_selection->pass < (tide_selection->rows + 63u) / 64u;\n"
                       "    for (uint32_t tide_warm = tide_from; tide_prefetch && tide_warm < tide_to && tide_warm - tide_from < 4u; tide_warm++) {\n"
                       "        const uint32_t tide_prefetch_i = tide_selected[tide_warm];\n");
    else sb_put(o, "        if (tide_prefetch && tide_to - tide_pick > 4u) {\n"
                   "            const uint32_t tide_prefetch_i = tide_selected[tide_pick + 4u];\n");
    for (int c = 0; c < g->prog->components.count; c++) {
        if (!rows_take(g, sys, a, c)) continue;
        const decl *d = g->prog->components.items[c];
        for (int k = 0; k < comp_lanes(d); k++) {
            sb_printf(o, "%s__builtin_prefetch(tide_col_%s[%d] + (size_t)tide_prefetch_i * %du, %d, 3);\n",
                      warm ? "        " : "            ", type_cname(d), k, comp_width(d), changes_component(sys, c, false) ? 1 : 0);
        }
    }
    sb_printf(o, "%s}\n#endif\n", warm ? "    " : "        ");
}

static int gen_rows_cache_filters(gen *g, const decl *sys, const int a)
{
    int count = 0;
    sb_put(&g->c, "(const tide_rows_filter[]){");
    for (int c = 0; c < g->prog->components.count; c++) {
        if (!comp_sparse(g, c) || !arch_stores(g, a, c)) continue;
        if (!mask_has(sys->need_mask, c) && !mask_has(sys->without_mask, c)) continue;
        sb_printf(&g->c, "%s{%du, %du}", count++ ? ", " : "", arch_presence(g, a, c), mask_has(sys->need_mask, c) ? 1 : 0);
    }
    sb_put(&g->c, "}");
    return count;
}

// Generic owned payloads contain neither executable addresses nor pointers to
// static descriptors. Keep this layout stable within TIDE_ROWS_CACHE_ABI.
static void gen_rows_cache_helpers(gen *g)
{
    const int plans = rows_cache_count(g);
    if (!plans) return;
    sb *o = &g->c;
    sb_put(o,
        "typedef struct tide_rows_filter { uint32_t column, need; } tide_rows_filter;\n"
        "typedef struct tide_rows_chunk {\n"
        "    uint32_t rows, pass;\n"
        "    bool ready;\n"
        "    uint16_t selected[128];\n"
        "} tide_rows_chunk;\n"
        "typedef struct tide_rows_plan {\n"
        "    uint32_t archetype, shift, filters, chunks;\n"
        "    tide_rows_filter *filter;\n"
        "    tide_page **pages;\n"
        "    tide_rows_chunk *chunk;\n"
        "} tide_rows_plan;\n"
        "struct tide_rows_cache {\n"
        "    uint32_t abi, plans;\n"
        "    tide_rows_plan *plan;\n"
        "};\n\n"
        "TIDE_HELPER void tide_rows_plan_free(tide_rows_plan *p)\n{\n"
        "    for (size_t i = 0; i < (size_t)p->chunks * p->filters; i++) tide_page_release(p->pages[i], 1u);\n"
        "    free(p->filter);\n    free(p->pages);\n    free(p->chunk);\n    memset(p, 0, sizeof *p);\n}\n\n"
        "TIDE_HELPER void tide_rows_cache_free(struct tide_rows_cache *cache)\n{\n"
        "    if (!cache) return;\n"
        "    for (uint32_t i = 0; i < cache->plans; i++) tide_rows_plan_free(&cache->plan[i]);\n"
        "    free(cache->plan);\n    free(cache);\n}\n\n"
        "TIDE_HELPER void tide_rows_plan_prepare(tide_rows_plan *p, const uint32_t archetype,\n"
        "    const tide_table *t, const tide_columns *c, const tide_rows_filter *filter, const uint32_t filters)\n{\n"
        "    if (p->archetype != archetype || p->shift != c->shift || p->filters != filters\n"
        "        || memcmp(p->filter, filter, (size_t)filters * sizeof *filter)) {\n"
        "        tide_rows_plan_free(p);\n"
        "        p->archetype = archetype;\n        p->shift = c->shift;\n        p->filters = filters;\n"
        "        p->filter = tide_alloc_zeroed(filters, sizeof *p->filter);\n"
        "        memcpy(p->filter, filter, (size_t)filters * sizeof *filter);\n    }\n"
        "    if (p->chunks == t->chunks) return;\n"
        "    if (filters && (size_t)t->chunks > SIZE_MAX / sizeof *p->pages / filters) tide_out_of_memory();\n"
        "    tide_rows_chunk *chunk = t->chunks ? tide_alloc_zeroed(t->chunks, sizeof *chunk) : NULL;\n"
        "    tide_page **pages = t->chunks ? tide_alloc_zeroed((size_t)t->chunks * filters, sizeof *pages) : NULL;\n"
        "    const uint32_t keep = p->chunks < t->chunks ? p->chunks : t->chunks;\n"
        "    if (keep) {\n        memcpy(chunk, p->chunk, (size_t)keep * sizeof *chunk);\n"
        "        memcpy(pages, p->pages, (size_t)keep * filters * sizeof *pages);\n    }\n"
        "    for (size_t i = (size_t)keep * filters; i < (size_t)p->chunks * filters; i++) tide_page_release(p->pages[i], 1u);\n"
        "    free(p->chunk);\n    free(p->pages);\n    p->chunk = chunk;\n    p->pages = pages;\n    p->chunks = t->chunks;\n}\n\n");
    sb_put(o,
        "TIDE_HELPER bool tide_rows_chunk_hold(tide_rows_plan *p, const tide_table *t, const tide_columns *c, const uint32_t chunk)\n{\n"
        "    tide_rows_chunk *entry = &p->chunk[chunk];\n"
        "    tide_page **held = p->pages + (size_t)chunk * p->filters;\n"
        "    const uint32_t rows = tide_table_rows(t, c->shift, chunk);\n"
        "    bool same = entry->ready && entry->rows == rows;\n"
        "    for (uint32_t f = 0; same && f < p->filters; f++)\n"
        "        same = held[f] == t->pages[(size_t)chunk * c->count + p->filter[f].column];\n"
        "    if (same) return false;\n"
        "    for (uint32_t f = 0; f < p->filters; f++) { tide_page_release(held[f], 1u); held[f] = NULL; }\n"
        "    entry->ready = false;\n    entry->rows = rows;\n    entry->pass = 0u;\n"
        "    for (uint32_t f = 0; f < p->filters; f++) {\n"
        "        tide_page *page = t->pages[(size_t)chunk * c->count + p->filter[f].column];\n"
        "        if (page->refs == UINT32_MAX) {\n"
        "            for (uint32_t k = 0; k < f; k++) { tide_page_release(held[k], 1u); held[k] = NULL; }\n"
        "            return false; // A cache must never wrap a page's reference count.\n        }\n"
        "        tide_page_retain(page);\n        held[f] = page;\n    }\n    return true;\n}\n\n"
        "TIDE_HELPER const tide_rows_chunk *tide_rows_cache_find(const struct tide_rows_cache *cache, const uint32_t id,\n"
        "    const uint32_t archetype, const tide_table *t, const tide_columns *c, const uint32_t chunk,\n"
        "    const tide_rows_filter *filter, const uint32_t filters)\n{\n"
        "    if (!cache || cache->abi != TIDE_ROWS_CACHE_ABI || id >= cache->plans) return NULL;\n"
        "    const tide_rows_plan *p = &cache->plan[id];\n"
        "    if (p->archetype != archetype || p->shift != c->shift || p->filters != filters\n"
        "        || chunk >= p->chunks || chunk >= t->chunks || memcmp(p->filter, filter, (size_t)filters * sizeof *filter)) return NULL;\n"
        "    const tide_rows_chunk *entry = &p->chunk[chunk];\n"
        "    if (!entry->ready || entry->rows != tide_table_rows(t, c->shift, chunk)) return NULL;\n"
        "    for (uint32_t f = 0; f < filters; f++)\n"
        "        if (p->pages[(size_t)chunk * filters + f] != t->pages[(size_t)chunk * c->count + filter[f].column]) return NULL;\n"
        "    return entry;\n}\n\n"
        "TIDE_HELPER uint32_t tide_rows_cache_lower(const tide_rows_chunk *entry, const uint32_t row)\n{\n"
        "    uint32_t lo = 0u, hi = entry->pass;\n"
        "    while (lo < hi) {\n        const uint32_t mid = lo + (hi - lo) / 2u;\n"
        "        if (entry->selected[mid] < row) lo = mid + 1u;\n        else hi = mid;\n    }\n    return lo;\n}\n\n");

    sb_put(o, "static void tide_rows_cache_prepare(tide_world *w)\n{\n");
    sb_printf(o, "    if (w->tide_rows_cache && w->tide_rows_cache->plans != %du) {\n"
                 "        tide_rows_cache_free(w->tide_rows_cache);\n        w->tide_rows_cache = NULL;\n    }\n"
                 "    if (!w->tide_rows_cache) {\n        w->tide_rows_cache = tide_alloc_zeroed(1u, sizeof *w->tide_rows_cache);\n"
                 "        w->tide_rows_cache->abi = TIDE_ROWS_CACHE_ABI;\n        w->tide_rows_cache->plans = %du;\n"
                 "        w->tide_rows_cache->plan = tide_alloc_zeroed(%du, sizeof *w->tide_rows_cache->plan);\n    }\n",
              plans, plans, plans);
    int id = 0;
    for (int s = 0; s < g->prog->systems.count; s++) {
        const decl *sys = g->prog->systems.items[s];
        for (int a = 0; a < g->prog->archetypes.count; a++) {
            if (!rows_cacheable(g, sys, a)) continue;
            sb_printf(o, "    {\n        tide_table *tide_t = &w->%s;\n"
                         "        tide_rows_plan *tide_plan = &w->tide_rows_cache->plan[%d];\n"
                         "        const tide_rows_filter *tide_filter = ", arch_name(g, a), id++);
            const int filters = gen_rows_cache_filters(g, sys, a);
            sb_printf(o, ";\n        tide_rows_plan_prepare(tide_plan, %du, tide_t, %s, tide_filter, %du);\n"
                         "        for (uint32_t tide_c = 0u; tide_c < tide_t->chunks; tide_c++) {\n"
                         "            if (!tide_rows_chunk_hold(tide_plan, tide_t, %s, tide_c)) continue;\n"
                         "            tide_rows_chunk *tide_entry = &tide_plan->chunk[tide_c];\n",
                      a, arch_columns(g, a), filters, arch_columns(g, a));
            for (int c = 0; c < g->prog->components.count; c++) {
                if (!comp_sparse(g, c) || !arch_stores(g, a, c)) continue;
                if (!mask_has(sys->need_mask, c) && !mask_has(sys->without_mask, c)) continue;
                sb_printf(o, "            const uint8_t *tide_pres_%s = tide_table_column(tide_t, %s, tide_c, %d);\n",
                          type_cname(g->prog->components.items[c]), arch_columns(g, a), arch_presence(g, a, c));
            }
            sb_put(o, "            uint32_t tide_pass = 0u;\n"
                      "            for (uint32_t tide_i = 0u; tide_i < tide_entry->rows; tide_i++) {\n"
                      "                if (tide_pass < 128u) tide_entry->selected[tide_pass] = (uint16_t)tide_i;\n"
                      "                tide_pass += ");
            gen_rows_pass(g, sys, a, "tide_i", "tide_scn");
            sb_put(o, ";\n            }\n            tide_entry->pass = tide_pass;\n            tide_entry->ready = true;\n        }\n    }\n");
        }
    }
    sb_put(o, "}\n\n");
}

static void gen_rows_cache_lookup(gen *g, const decl *sys, const int a, const char *pad)
{
    sb_printf(&g->c, "%sconst tide_rows_chunk *tide_cached = tide_rows_cache_find(tide_w->tide_rows_cache, %du, %du, tide_t, %s, tide_c, ",
              pad, rows_cache_id(g, sys, a), a, arch_columns(g, a));
    const int filters = gen_rows_cache_filters(g, sys, a);
    sb_printf(&g->c, ", %du);\n", filters);
}
