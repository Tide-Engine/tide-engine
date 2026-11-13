// `cached` functions (docs/spec.md, Pure functions; notes/pure-functions.md).
//
// A cached function's code is generated under another name
// (tide_cached_run_<name>), and its own name is a wrapper: it makes a key of
// its arguments, looks the key up in the function's table, and gives the
// result kept there, or runs the code and keeps what it gives.
//
// The table is this program's alone: a static of the generated C, in no
// world, snapshot, packed world or hash. Any world's code on any thread uses
// it, so its entries are found by what the arguments are, never by whose they
// are, and each set of entries has a lock.
//
// The key is bytes: the arguments' own (a number, a vector, a struct), and
// for what an argument holds elsewhere,
// - text it was handed as text: two hashes of its bytes and its length;
// - text, a list, a map, a set or a grid in a world's heap: its handle, and a
//   version of that owner's heap, the hashes its pages keep while they don't
//   change (tide/heap.h). A page that changed since it was last hashed has
//   none, and then the call isn't looked up: the code runs, as if it weren't
//   cached;
// - one in the pack: its handle, with the pack's generation, which changes
//   when the pack does;
// - one the code made as it ran, in the scratch area: nothing says what's in
//   it but reading it, so that call isn't looked up either.
//
// A result that holds text or lists is kept in a heap of the table's own, and
// each call that finds it gets a copy in its scratch area, as a call that ran
// the code would have made it.
//
// Generated C calls no runtime function that wasn't there: the table's code is
// the helpers below.

static bool cached_storage(const type t)
{
    if (t.kind == TY_STRING || t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET || t.kind == TY_GRID) return true;
    if (t.kind != TY_STRUCT && t.kind != TY_EVENT && t.kind != TY_ARRAY && t.kind != TY_OPTIONAL && t.kind != TY_FAILABLE
        && t.kind != TY_COMPONENT && t.kind != TY_SINGLETON) return false;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (cached_storage(t.decl->fields.items[i].type)) return true;
    }
    return false;
}

// Whether `m` gets a table: a cached function the CPU runs, whose result the
// table can keep (the checker refused the rest).
static bool cached_routine(const gen *g, const decl *m)
{
    if (!m->is_cached || !m->pure || g->opts->no_cached || m->kind != DECL_FUNCTION || m->result.kind == TY_VOID) return false;
    if (cached_storage(m->result) && !worker_has_storage(m->result)) return false; // A component or a grid: refused
    for (int i = 0; i < m->params.count; i++) {
        const param *p = &m->params.items[i];
        if (p->mode != PARAM_READ || p->type.kind == TY_INPUT || p->type.kind == TY_QUERY || p->type.kind == TY_WORLD
            || p->type.kind == TY_RECORD || has_fields_ptr(p) || p->owner_context) return false;
    }
    return true;
}

static const char *cached_name(const decl *m, const char *what)
{
    sb b = {0};
    sb_printf(&b, "tide_cached_%s_%s", what, routine_cname(m) + strlen("tide_"));
    return b.data;
}

static bool cached_any(const gen *g)
{
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *d = g->prog->decls.items[i];
        if (d->kind == DECL_FUNCTION && cached_routine(g, d) && cpu_routine(d)) return true;
    }
    return false;
}

// The helpers, and each cached function's table.
static void gen_cached_tables(gen *g)
{
    if (!cached_any(g)) return;
    sb *o = &g->c;
    sb_put(o,
        "// Cached functions (notes/pure-functions.md): each one's results, by its arguments. This program's own,\n"
        "// outside every world: nothing here is copied with one, hashed, packed or sent.\n"
        "#define TIDE_CACHED_SETS 64u\n"
        "#define TIDE_CACHED_WAYS 4u\n"
        "#define TIDE_CACHED_HEAD 64u // A key's first bytes: the pack's generation, its text's two hashes and length, four heaps' versions\n\n"
        "uint64_t tide_pack_generation(void); // tide/pack.h\n\n"
        "typedef struct tide_cached_set {\n"
        "    uint32_t lock;  // Held to read or change its entries, and these\n"
        "    uint32_t padding;\n"
        "    uint64_t clock; // Counts its entries' uses: the one used longest ago makes room\n"
        "    uint64_t hits;\n"
        "    uint64_t misses;\n"
        "} tide_cached_set;\n\n"
        "typedef struct tide_cached {\n"
        "    tide_cached_set sets[TIDE_CACHED_SETS];\n"
        "    // TIDE_CACHED_SETS * TIDE_CACHED_WAYS entries of `stride` bytes, made at the first call: when it was\n"
        "    // last used (0: empty), its key's hash, its key, its result\n"
        "    uint8_t *entries;\n"
        "    uint32_t key, value, stride;\n"
        "    uint32_t holds;   // Its results hold text or lists, in `heaps`: the first set's lock is the only one then\n"
        "    uint64_t dropped; // Entries let go of to make room\n"
        "    tide_heaps heaps;\n"
        "} tide_cached;\n\n"
        "typedef struct tide_cached_key {\n"
        "    uint8_t *bytes;\n"
        "    uint32_t used;\n"
        "    bool ok; // false: something of the arguments has no version, so this call runs the code\n"
        "    uint32_t heaps;\n"
        "    uint32_t owners[4];\n"
        "    uint64_t text[3];\n"
        "} tide_cached_key;\n\n"
        "TIDE_HELPER void tide_cached_lock(tide_cached_set *set)\n{\n"
        "    while (__atomic_exchange_n(&set->lock, 1u, __ATOMIC_ACQUIRE)) {\n"
        "        while (__atomic_load_n(&set->lock, __ATOMIC_RELAXED)) {}\n"
        "    }\n}\n\n");
    sb_put(o,
        "TIDE_HELPER void tide_cached_unlock(tide_cached_set *set)\n{\n"
        "    __atomic_store_n(&set->lock, 0u, __ATOMIC_RELEASE);\n}\n\n");
    sb_put(o,
        "TIDE_HELPER void tide_cached_begin(tide_cached_key *k, uint8_t *bytes, const uint32_t size)\n{\n"
        "    memset(bytes, 0, size);\n"
        "    *k = (tide_cached_key){.bytes = bytes, .used = TIDE_CACHED_HEAD, .ok = true,\n"
        "                           .text = {TIDE_HASH_START, 0xC2B2AE3D27D4EB4Full, 0u}};\n}\n\n");
    sb_put(o,
        "// An argument's own bytes\n"
        "TIDE_HELPER void tide_cached_value(tide_cached_key *k, const void *value, const uint32_t size)\n{\n"
        "    memcpy(k->bytes + k->used, value, size);\n"
        "    k->used += size;\n}\n\n");
    sb_put(o,
        "// Text an argument is, or holds as text: what it says\n"
        "TIDE_HELPER void tide_cached_text(tide_cached_key *k, const tide_str s)\n{\n"
        "    const int32_t bytes = s.bytes > 0 ? s.bytes : 0;\n"
        "    for (int i = 0; i < 2; i++) {\n"
        "        k->text[i] = tide_hash_more(k->text[i], &bytes, sizeof bytes);\n"
        "        if (bytes) k->text[i] = tide_hash_more(k->text[i], s.ptr, (size_t)bytes);\n"
        "    }\n"
        "    k->text[2] += (uint64_t)bytes + 1u;\n}\n\n");
    sb_put(o,
        "// A heap's pages as a number, while none of them changed since it was last hashed (tide/page.h): false\n"
        "// when one did. Nothing is hashed here, and nothing written: other threads read the same pages.\n"
        "TIDE_HELPER bool tide_cached_pages(const tide_heap *heap, uint64_t *h)\n{\n"
        "    const uint32_t count = __atomic_load_n(&heap->pages, __ATOMIC_ACQUIRE);\n"
        "    tide_page *const *pages = __atomic_load_n(&heap->page, __ATOMIC_ACQUIRE);\n"
        "    const uint64_t used = __atomic_load_n(&heap->used, __ATOMIC_RELAXED);\n"
        "    uint64_t mixed = used;\n"
        "    for (uint32_t i = 0; pages && i < count; i++) {\n"
        "        const tide_page *p = __atomic_load_n(&pages[i], __ATOMIC_ACQUIRE);\n"
        "        if (!p) return false;\n"
        "        const uint32_t hashed = __atomic_load_n(&p->hashed, __ATOMIC_ACQUIRE);\n"
        "        if (hashed == UINT32_MAX) return false;\n"
        "        // Each page's hash is one already: two multiplications fold it in, in the pages' order\n"
        "        mixed = (mixed ^ __atomic_load_n(&p->hash, __ATOMIC_RELAXED)) * 0x9E3779B97F4A7C15ull;\n"
        "        mixed = (mixed ^ hashed ^ (mixed >> 29)) * 0xC2B2AE3D27D4EB4Full;\n"
        "    }\n"
        "    *h = tide_hash_more(*h, &mixed, sizeof mixed);\n"
        "    return true;\n}\n\n");
    sb_put(o,
        "// What an argument holds elsewhere, by its handle (which the argument's own bytes have): the version of\n"
        "// the heap it's in, once for each heap\n"
        "TIDE_HELPER void tide_cached_handle(tide_cached_key *k, const tide_handle at)\n{\n"
        "    if (!at || !k->ok) return;\n"
        "    const uint32_t where = tide_handle_where(at);\n"
        "    if (where == TIDE_IN_SCRATCH) {\n"
        "        if (!tide_pack_holds(at)) k->ok = false; // Made as the code ran\n"
        "        return;\n"
        "    }\n"
        "    const uint32_t owner = where & TIDE_WHERE_OWNER;\n"
        "    for (uint32_t i = 0; i < k->heaps; i++) {\n"
        "        if (k->owners[i] == owner) return;\n"
        "    }\n"
        "    const tide_heap *heap = k->heaps < 4u ? tide_heap_read(owner) : NULL;\n"
        "    uint64_t h = tide_hash_more(TIDE_HASH_START, &owner, sizeof owner);\n"
        "    if (!heap || !tide_cached_pages(heap, &h)) {\n"
        "        k->ok = false;\n"
        "        return;\n"
        "    }\n"
        "    const tide_heap *runs = __atomic_load_n(&heap->runs, __ATOMIC_ACQUIRE); // A heap in runs: all of them\n"
        "    for (uint32_t r = 0; runs && r < TIDE_HEAP_RUNS - 1u; r++) {\n"
        "        if (!tide_cached_pages(&runs[r], &h)) {\n"
        "            k->ok = false;\n"
        "            return;\n"
        "        }\n"
        "    }\n"
        "    h = tide_hash_end(h);\n"
        "    memcpy(k->bytes + 32u + 8u * k->heaps, &h, sizeof h);\n"
        "    k->owners[k->heaps++] = owner;\n}\n\n");
    sb_put(o,
        "TIDE_HELPER uint8_t *tide_cached_entries(tide_cached *t, const uint32_t key, const uint32_t value, const bool holds)\n{\n"
        "    uint8_t *entries = __atomic_load_n(&t->entries, __ATOMIC_ACQUIRE);\n"
        "    if (entries) return entries;\n"
        "    const uint32_t stride = (16u + key + value + 7u) & ~7u;\n"
        "    uint8_t *made = tide_alloc_zeroed((size_t)TIDE_CACHED_SETS * TIDE_CACHED_WAYS, stride);\n"
        "    tide_cached_lock(&t->sets[0]);\n"
        "    entries = t->entries;\n"
        "    if (!entries) {\n"
        "        t->key = key;\n"
        "        t->value = value;\n"
        "        t->stride = stride;\n"
        "        t->holds = holds;\n"
        "        __atomic_store_n(&t->entries, made, __ATOMIC_RELEASE);\n"
        "        entries = made;\n"
        "        made = NULL;\n"
        "    }\n"
        "    tide_cached_unlock(&t->sets[0]);\n"
        "    free(made);\n"
        "    return entries;\n}\n\n");
    sb_put(o,
        "// The result kept for a key, or NULL, with `*locked` held either way: the caller lets go of it once it\n"
        "// has read the result. Finishes the key first.\n"
        "TIDE_HELPER const uint8_t *tide_cached_find(tide_cached *t, tide_cached_key *k, const uint32_t value, const bool holds,\n"
        "                                            tide_cached_set **locked, uint64_t *hash)\n{\n"
        "    const uint64_t generation = tide_pack_generation();\n"
        "    memcpy(k->bytes, &generation, sizeof generation);\n"
        "    memcpy(k->bytes + 8u, k->text, sizeof k->text);\n"
        "    uint8_t *entries = tide_cached_entries(t, k->used, value, holds);\n"
        "    *hash = tide_hash(k->bytes, k->used);\n"
        "    const uint32_t s = (uint32_t)(*hash % TIDE_CACHED_SETS);\n"
        "    tide_cached_set *set = &t->sets[s];\n"
        "    *locked = holds ? &t->sets[0] : set;\n"
        "    tide_cached_lock(*locked);\n"
        "    uint8_t *at = entries + (size_t)s * TIDE_CACHED_WAYS * t->stride;\n"
        "    for (uint32_t way = 0; way < TIDE_CACHED_WAYS; way++, at += t->stride) {\n"
        "        uint64_t head[2];\n"
        "        memcpy(head, at, sizeof head);\n"
        "        if (!head[0] || head[1] != *hash || memcmp(at + 16u, k->bytes, k->used) != 0) continue;\n"
        "        head[0] = ++set->clock;\n"
        "        memcpy(at, head, sizeof head[0]);\n"
        "        set->hits++;\n"
        "        return at + 16u + k->used;\n"
        "    }\n"
        "    set->misses++;\n"
        "    return NULL;\n}\n\n");
    sb_put(o,
        "// Where a key's result goes, with `*locked` held: an empty entry of its set, or the one used longest\n"
        "// ago. NULL when another call kept one for the key meanwhile, which stays. A table whose results hold\n"
        "// text or lists lets go of all of them when a set is full: its heap goes whole.\n"
        "TIDE_HELPER uint8_t *tide_cached_place(tide_cached *t, const tide_cached_key *k, const uint64_t hash, tide_cached_set **locked)\n{\n"
        "    uint8_t *entries = t->entries;\n"
        "    const uint32_t s = (uint32_t)(hash % TIDE_CACHED_SETS);\n"
        "    tide_cached_set *set = &t->sets[s];\n"
        "    *locked = t->holds ? &t->sets[0] : set;\n"
        "    tide_cached_lock(*locked);\n"
        "    uint8_t *first = entries + (size_t)s * TIDE_CACHED_WAYS * t->stride, *to = NULL;\n"
        "    uint64_t oldest = UINT64_MAX;\n"
        "    for (uint32_t way = 0; way < TIDE_CACHED_WAYS; way++) {\n"
        "        uint8_t *at = first + (size_t)way * t->stride;\n"
        "        uint64_t head[2];\n"
        "        memcpy(head, at, sizeof head);\n"
        "        if (head[0] && head[1] == hash && memcmp(at + 16u, k->bytes, k->used) == 0) return NULL;\n"
        "        if (head[0] < oldest) {\n"
        "            oldest = head[0];\n"
        "            to = at;\n"
        "        }\n"
        "    }\n"
        "    if (oldest) {\n"
        "        if (t->holds) {\n"
        "            tide_heaps_free(&t->heaps);\n"
        "            memset(entries, 0, (size_t)TIDE_CACHED_SETS * TIDE_CACHED_WAYS * t->stride);\n"
        "            t->dropped += TIDE_CACHED_SETS * TIDE_CACHED_WAYS;\n"
        "            to = first;\n"
        "        } else {\n"
        "            __atomic_add_fetch(&t->dropped, 1u, __ATOMIC_RELAXED);\n"
        "        }\n"
        "    }\n"
        "    const uint64_t head[2] = {++set->clock, hash};\n"
        "    memcpy(to, head, sizeof head);\n"
        "    memcpy(to + 16u, k->bytes, k->used);\n"
        "    return to + 16u + k->used;\n}\n\n");
    sb_put(o,
        "TIDE_HELPER void tide_cached_let_go(tide_cached *t)\n{\n"
        "    tide_heaps_free(&t->heaps);\n"
        "    free(t->entries);\n"
        "    memset(t, 0, sizeof *t);\n}\n\n");
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *d = g->prog->decls.items[i];
        if (d->kind != DECL_FUNCTION || !cached_routine(g, d) || !cpu_routine(d)) continue;
        sb_printf(o, "static tide_cached %s;\n", cached_name(d, "table"));
    }
    sb_put(o, "\n// Before the game's library is unloaded (tide_run_unload), and for whoever wants the memory back: every\n"
              "// cached function's table, empty again\n"
              "void tide_cached_free(void)\n{\n");
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *d = g->prog->decls.items[i];
        if (d->kind != DECL_FUNCTION || !cached_routine(g, d) || !cpu_routine(d)) continue;
        sb_printf(o, "    tide_cached_let_go(&%s);\n", cached_name(d, "table"));
    }
    sb_put(o, "}\n\n"
              "// How often cached functions found their result kept, how often they ran, and how many results they\n"
              "// let go of to make room, since the program started or tide_cached_free: for tests and tools\n"
              "void tide_cached_counts(uint64_t *hits, uint64_t *misses, uint64_t *dropped)\n{\n"
              "    *hits = *misses = *dropped = 0;\n");
    for (int i = 0; i < g->prog->decls.count; i++) {
        const decl *d = g->prog->decls.items[i];
        if (d->kind != DECL_FUNCTION || !cached_routine(g, d) || !cpu_routine(d)) continue;
        const char *table = cached_name(d, "table");
        sb_printf(o, "    for (uint32_t i = 0; i < TIDE_CACHED_SETS; i++) {\n"
                     "        *hits += %s.sets[i].hits;\n        *misses += %s.sets[i].misses;\n    }\n"
                     "    *dropped += %s.dropped;\n", table, table, table);
    }
    sb_put(o, "}\n\n");
}

// The handles a value of type `t` at `place` (C) has, each added to the key.
// Only its own: what a list's elements hold is in the same owner's heap, whose
// version covers it.
static void gen_cached_handles(gen *g, const type t, const char *place, const int depth)
{
    sb *o = &g->c;
    if (!cached_storage(t)) return;
    if (t.kind == TY_STRING || t.kind == TY_LIST || t.kind == TY_GRID) {
        sb_printf(o, "    tide_cached_handle(&tide_key, %s.at);\n", place);
    } else if (t.kind == TY_MAP || t.kind == TY_SET) {
        sb_printf(o, "    tide_cached_handle(&tide_key, %s.entries.at);\n    tide_cached_handle(&tide_key, %s.index.at);\n", place, place);
    } else if (t.kind == TY_ARRAY) {
        sb_printf(o, "    for (int32_t tide_a%d = 0; tide_a%d < %d; tide_a%d++) {\n", depth, depth, t.decl->array_length, depth);
        sb child = {0};
        sb_printf(&child, "%s.at[tide_a%d]", place, depth);
        gen_cached_handles(g, t.decl->fields.items[0].type, child.data, depth + 1);
        sb_put(o, "    }\n");
    } else {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            sb child = {0};
            sb_printf(&child, "%s.%s", place, field_cname(f));
            // A T?'s or a result's text is text as code has it, not a handle
            if ((t.kind == TY_OPTIONAL || t.kind == TY_FAILABLE) && f->type.kind == TY_STRING) {
                sb_printf(o, "    tide_cached_text(&tide_key, %s);\n", child.data);
            } else {
                gen_cached_handles(g, f->type, child.data, depth + 1);
            }
        }
    }
}

// A cached function's parameters as its code's are called: by the names its
// own signature gives them (gen_routine_signature).
static const char *cached_param_name(gen *g, const decl *m, const param *p)
{
    const char *name = local_cname(g, p->name);
    if (!typed_list_param(g, m, p)) return name;
    sb b = {0};
    sb_printf(&b, "tide_arg_%s", name);
    return b.data;
}

// The wrapper that has the function's name, after its code (gen_routine).
static void gen_cached_wrapper(gen *g, const decl *m)
{
    sb *o = &g->c;
    const char *table = cached_name(m, "table");
    const char *result = c_type(m->result);
    const bool text = m->result.kind == TY_STRING; // Kept as a tide_text; code has it as a tide_str
    const bool holds = cached_storage(m->result);
    sb_printf(o, "// " STR_FMT ", cached: its results by its arguments\n", STR_ARG(m->qualified));
    g->typing_params = true;
    gen_routine_signature(g, o, m);
    g->typing_params = false;
    sb_put(o, "\n{\n    _Alignas(8) uint8_t tide_key_bytes[TIDE_CACHED_HEAD");
    for (int i = 0; i < m->params.count; i++) {
        const param *p = &m->params.items[i];
        if (p->type.kind != TY_STRING) sb_printf(o, " + sizeof(%s)", c_type(p->type));
    }
    sb_put(o, "];\n    tide_cached_key tide_key;\n    tide_cached_begin(&tide_key, tide_key_bytes, sizeof tide_key_bytes);\n");
    sb call = {0};
    sb_printf(&call, "%s(", cached_name(m, "run"));
    for (int i = 0; i < m->params.count; i++) {
        const param *p = &m->params.items[i];
        const char *name = cached_param_name(g, m, p);
        sb_printf(&call, "%s%s", i ? ", " : "", name);
        if (p->type.kind == TY_STRING) {
            sb_printf(o, "    tide_cached_text(&tide_key, %s);\n", name);
            continue;
        }
        sb value = {0};
        sb_printf(&value, p->c_indirect ? "(*%s)" : "%s", name);
        sb_printf(o, "    tide_cached_value(&tide_key, &%s, sizeof(%s));\n", value.data, c_type(p->type));
        gen_cached_handles(g, p->type, value.data, 0);
    }
    sb_put(&call, ")");
    sb_printf(o, "    if (!tide_key.ok) return %s; // Something of them has no version: as if it weren't cached\n", call.data);
    sb_put(o, "    tide_cached_set *tide_set;\n    uint64_t tide_hash_of;\n");
    const char *kept = text ? "tide_text" : result;
    sb_printf(o, "    const uint8_t *tide_found = tide_cached_find(&%s, &tide_key, sizeof(%s), %s, &tide_set, &tide_hash_of);\n", table, kept,
              holds ? "true" : "false");
    sb_put(o, "    if (tide_found) {\n");
    if (!holds) {
        sb_printf(o, "        %s tide_value;\n        memcpy(&tide_value, tide_found, sizeof tide_value);\n"
                     "        tide_cached_unlock(tide_set);\n        return tide_value;\n    }\n", result);
    } else {
        // Out of the table's heap, which stands in for the local world's while it's read (as a worker's does)
        sb_printf(o, "        %s tide_kept;\n        memcpy(&tide_kept, tide_found, sizeof tide_kept);\n", kept);
        sb_printf(o, "        tide_heaps *const tide_match = tide_world_heaps[TIDE_IN_MATCH], *const tide_local = tide_world_heaps[TIDE_IN_LOCAL];\n"
                     "        const uint32_t tide_run = tide_heap_run_enter(0u); // The table's heap is in no system's runs\n"
                     "        tide_heap_run_leave(0u);\n"
                     "        tide_text_use(tide_match, &%s.heaps);\n", table);
        if (text) sb_put(o, "        const tide_str tide_value = tide_text_view(tide_text_temp(tide_text_view(tide_kept)));\n");
        else gen_worker_snapshot(g, m->result, "tide_kept", 0);
        sb_put(o, "        tide_text_use(tide_match, tide_local);\n        tide_heap_run_leave(tide_run);\n        tide_cached_unlock(tide_set);\n");
        sb_printf(o, "        return %s;\n    }\n", text ? "tide_value" : "tide_kept");
    }
    sb_printf(o, "    tide_cached_unlock(tide_set);\n    const %s tide_value = %s;\n", result, call.data);
    if (!holds) {
        sb_printf(o, "    uint8_t *tide_to = tide_cached_place(&%s, &tide_key, tide_hash_of, &tide_set);\n"
                     "    if (tide_to) memcpy(tide_to, &tide_value, sizeof tide_value);\n"
                     "    tide_cached_unlock(tide_set);\n    return tide_value;\n}\n\n", table);
        return;
    }
    // A copy of its own first, all of it in the scratch area: what the result borrows of a world is read
    // while that world's heaps are this thread's, and then it goes into the table's heap
    if (text) {
        sb_put(o, "    tide_text tide_kept = {0};\n");
    } else {
        sb_printf(o, "    %s tide_kept = tide_value;\n", result);
        gen_worker_snapshot(g, m->result, "tide_kept", 0);
    }
    sb_printf(o, "    uint8_t *tide_to = tide_cached_place(&%s, &tide_key, tide_hash_of, &tide_set);\n    if (tide_to) {\n", table);
    sb_printf(o, "        tide_heaps *const tide_match = tide_world_heaps[TIDE_IN_MATCH], *const tide_local = tide_world_heaps[TIDE_IN_LOCAL];\n"
                 "        const uint32_t tide_run = tide_heap_run_enter(0u); // The table's heap is in no system's runs\n"
                 "        tide_heap_run_leave(0u);\n"
                 "        tide_text_use(tide_match, &%s.heaps);\n", table);
    if (text) sb_put(o, "        tide_text_set(&tide_kept, tide_value, TIDE_IN_LOCAL);\n");
    else gen_worker_own(g, m->result, "tide_kept", "TIDE_IN_LOCAL");
    sb_put(o, "        tide_text_use(tide_match, tide_local);\n        tide_heap_run_leave(tide_run);\n"
              "        memcpy(tide_to, &tide_kept, sizeof tide_kept);\n    }\n"
              "    tide_cached_unlock(tide_set);\n    return tide_value;\n}\n\n");
}
