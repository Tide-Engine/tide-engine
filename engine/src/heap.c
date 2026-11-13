#include "tide/heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asset_store.h"
#include "delta_asset.h"

// See tide/heap.h.

#define PAGE (1u << TIDE_HEAP_PAGE_SHIFT)
#define FIRST 16u          // Offset 0 means no block, so blocks start after it
// As far as its places reach: 2^32 of a page each (64 TiB), which memory reaches first
#define LIMIT (UINT64_C(1) << (32u + TIDE_HEAP_PAGE_SHIFT))

// ---------------------------------------------------------------------------
// The heaps this thread's code may change

#ifdef TIDE_HEAP_CHECKS
static TIDE_THREAD_LOCAL const uint16_t *allowed_ids;
static TIDE_THREAD_LOCAL uint32_t allowed_count;
static TIDE_THREAD_LOCAL const tide_heaps *tide_world_heaps_checked; // The world whose heaps they are, to name one
static TIDE_THREAD_LOCAL const char *allowed_who;

// Ends the program when this thread's code isn't allowed to change `h`: a
// system changed the text or lists of an owner the schedule doesn't know it
// changes, which another system may be changing at the same time.
static void check_allowed(const tide_heap *h)
{
    if (!allowed_who) return;
    if (h->base) h = h->base; // A run's heap is its owner's
    // Which heap: its id among the system's world's. Another world's (a
    // worker's own, a snapshot being read) is none of the schedule's
    int id = -1;
    const tide_heaps *match = tide_world_heaps_checked;
    for (uint32_t slot = 0; match && match->all && slot < tide_heap_count; slot++) {
        if (h == &match->all[slot]) id = tide_heap_ids[slot];
    }
    if (id < 0) return;
    for (uint32_t i = 0; i < allowed_count; i++) {
        if (allowed_ids[i] == id) return;
    }
    fprintf(stderr, "tide: the system '%s' changed the heap with id %d, which isn't one of its owners' (TIDE_HEAP_CHECKS): "
                    "the schedule lets another system change it at the same time\n", allowed_who, id);
    abort();
}
#else
#define check_allowed(h) ((void)(h))
#endif

void tide_heaps_allow(const tide_heaps *match, const uint16_t *ids, const uint32_t count, const char *who)
{
#ifdef TIDE_HEAP_CHECKS
    tide_world_heaps_checked = match;
    allowed_ids = ids;
    allowed_count = count;
    allowed_who = who;
#else
    (void)match;
    (void)ids;
    (void)count;
    (void)who;
#endif
}

// The references a heap has to `p`: one for each of its places.
static uint32_t places(const tide_page *p)
{
    return p->size > PAGE ? p->size >> TIDE_HEAP_PAGE_SHIFT : 1u;
}

// ---------------------------------------------------------------------------
// A heap in runs (tide/heap.h)

TIDE_THREAD_LOCAL uint32_t tide_heap_run_where;
static TIDE_THREAD_LOCAL bool in_runs; // This thread is on a run of a system whose runs may be on other threads

// An asset list's lease to give up when the runs join.
typedef struct tide_heap_later {
    tide_heap *home;
    uint64_t block;
    uint8_t id[32];
} tide_heap_later;

// A block of a run's heap, as a run's `away` and `held` list them: the run above its offset.
#define AWAY_RUN_SHIFT 48u

static uint32_t run_now(void)
{
    return tide_heap_run_where >> TIDE_WHERE_RUN_SHIFT;
}

uint32_t tide_heap_run_enter(const uint32_t run)
{
    const uint32_t was = tide_heap_run_where | (in_runs ? 1u : 0u);
    tide_heap_run_where = (run & (TIDE_HEAP_RUNS - 1u)) << TIDE_WHERE_RUN_SHIFT;
    in_runs = true;
    return was;
}

void tide_heap_run_leave(const uint32_t was)
{
    tide_heap_run_where = was & ~1u;
    in_runs = was & 1u;
}

static tide_heap *runs_make(tide_heap *base);

tide_heap *tide_heap_run_of(tide_heap *base, const uint32_t run)
{
    const uint32_t which = run & (TIDE_HEAP_RUNS - 1u);
    if (!which) return base;
    tide_heap *runs = __atomic_load_n(&base->runs, __ATOMIC_ACQUIRE);
    if (!runs) runs = runs_make(base);
    return runs + (which - 1u);
}

static tide_heap *runs_make(tide_heap *base)
{
    tide_heap *runs = __atomic_load_n(&base->runs, __ATOMIC_ACQUIRE);
    if (runs) return runs;
    tide_heap *made = tide_alloc_zeroed(TIDE_HEAP_RUNS - 1u, sizeof *made);
    for (uint32_t i = 0; i < TIDE_HEAP_RUNS - 1u; i++) {
        made[i].base = base;
        made[i].run = i + 1u;
    }
    if (__atomic_compare_exchange_n(&base->runs, &runs, made, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return made;
    free(made); // Another thread's are the heap's
    return runs;
}

// The heap of run `run` of the owner `h` is one of.
static tide_heap *run_of(tide_heap *h, const uint32_t run)
{
    tide_heap *base = h->base ? h->base : h;
    return run ? tide_heap_run_of(base, run) : base;
}

// ...where there is one: NULL for a run whose heap was never made.
static tide_heap *run_made(const tide_heap *base, const uint32_t run)
{
    if (!base || !run) return (tide_heap *)(uintptr_t)base;
    tide_heap *runs = __atomic_load_n(&base->runs, __ATOMIC_ACQUIRE);
    return runs ? runs + (run - 1u) : NULL;
}

bool tide_heap_away(const tide_heap *h)
{
    return in_runs && h->run != run_now();
}

static void relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

// A heap's page table is one thread's to change at a time while runs are on threads: a page given a copy of
// its own by whichever run writes it first, and the table grown by the run whose heap it is.
static void lock(tide_heap *h)
{
    while (__atomic_exchange_n(&h->lock, 1u, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&h->lock, __ATOMIC_RELAXED)) relax();
    }
}

static void unlock(tide_heap *h)
{
    __atomic_store_n(&h->lock, 0u, __ATOMIC_RELEASE);
}

// A block of a page a snapshot shares, to change, while runs are on threads: the page is replaced with a copy,
// by this thread or by another that got there first. The page it replaces keeps the heap's references until the
// runs join: another thread may have read it from the table a moment ago, and would take it for the heap's own
// if a snapshot's were the only ones left.
static tide_block *write_shared_in_runs(tide_heap *h, const uint64_t block)
{
    const uint64_t place = block >> TIDE_HEAP_PAGE_SHIFT;
    for (;;) {
        tide_page *const *pages = __atomic_load_n(&h->page, __ATOMIC_ACQUIRE);
        tide_page *p = __atomic_load_n(&pages[place], __ATOMIC_ACQUIRE);
        if (p->refs == places(p)) {
            p->hashed = UINT32_MAX;
            break;
        }
        // As far as the heap went when the snapshot took the page, at least: nothing was written past that since
        const uint64_t left = __atomic_load_n(&h->used, __ATOMIC_RELAXED) - ((uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT);
        tide_page *copy = tide_page_copy(p, places(p), left < p->size ? (uint32_t)left : p->size);
        lock(h);
        const bool mine = h->page[place] == p;
        if (mine) {
            for (uint32_t k = 0; k < places(copy); k++) __atomic_store_n(&h->page[copy->first + k], copy, __ATOMIC_RELEASE);
            if (h->retired_count == h->retired_room) {
                const uint32_t room = h->retired_room ? h->retired_room * 2u : 16u;
                h->retired = tide_realloc(h->retired, h->retired_room * sizeof *h->retired, room * sizeof *h->retired);
                h->retired_room = room;
            }
            h->retired[h->retired_count++] = p;
        }
        unlock(h);
        if (mine) {
            __atomic_add_fetch(&h->moves, 1u, __ATOMIC_RELAXED);
            break;
        }
        copy->refs = 0;
        tide_page_give(copy); // Another thread's copy is the heap's
    }
    return tide_heap_block(h, block);
}

// Bytes of `p` in use: as far as `used` goes.
static uint32_t in_use(const tide_heap *h, const tide_page *p)
{
    const uint64_t left = h->used - ((uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT);
    return left < p->size ? (uint32_t)left : p->size;
}

// A bigger page table, which takes the old one's place as a whole. The old
// one stays, linked from page[-1], for code on other threads still reading it,
// until tide_heap_settle.
static void make_room(tide_heap *h, const uint32_t pages)
{
    if (pages <= h->room) return;
    uint32_t room = h->room ? h->room : 4u;
    while (room < pages) room *= 2u;
    tide_page **base = tide_alloc((room + 1u) * sizeof *base);
    tide_page **grown = base + 1;
    if (h->pages) memcpy(grown, h->page, h->pages * sizeof *grown);
    base[0] = h->page ? (tide_page *)(void *)(h->page - 1) : NULL;
    __atomic_store_n(&h->page, grown, __ATOMIC_RELEASE);
    h->room = room;
}

void tide_heap_settle(tide_heap *h)
{
    tide_asset_settle(h);
    if (!h->page) return;
    void *old = h->page[-1];
    while (old) {
        tide_page **base = old;
        old = base[0];
        free(base);
    }
    h->page[-1] = NULL;
}

// A page for the heap's next `count` places.
static void add_page(tide_heap *h, const uint32_t count)
{
    make_room(h, h->pages + count);
    tide_page *p = tide_page_new(count << TIDE_HEAP_PAGE_SHIFT);
    p->first = h->pages;
    p->refs = count;
    for (uint32_t k = 0; k < count; k++) h->page[h->pages + k] = p;
    h->pages += count;
}

tide_block *tide_heap_write_shared(tide_heap *h, const uint64_t block)
{
    check_allowed(h);
    if (in_runs) return write_shared_in_runs(h, block);
    tide_page *p = h->page[block >> TIDE_HEAP_PAGE_SHIFT];
    tide_page *own = tide_page_own(p, places(p), in_use(h, p));
    if (own != p) { // Code reading the old page meanwhile reads the same bytes
        for (uint32_t k = 0; k < places(own); k++) __atomic_store_n(&h->page[own->first + k], own, __ATOMIC_RELEASE);
        __atomic_add_fetch(&h->moves, 1u, __ATOMIC_RELAXED);
    }
    return tide_heap_block(h, block);
}

tide_block *tide_heap_write_over(tide_heap *h, const uint64_t block, const uint32_t bytes)
{
    check_allowed(h);
    if (in_runs) return tide_heap_write(h, block); // Copied whole: the page it replaces is kept (write_shared_in_runs)
    tide_page *p = h->page[block >> TIDE_HEAP_PAGE_SHIFT];
    if (p->refs == places(p)) return tide_heap_write(h, block);
    const uint32_t from = (uint32_t)(block - ((uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT)) + (uint32_t)sizeof(tide_block);
    tide_page *own = tide_page_own_copy_around(p, places(p), in_use(h, p), from, bytes);
    for (uint32_t k = 0; k < places(own); k++) __atomic_store_n(&h->page[own->first + k], own, __ATOMIC_RELEASE);
    __atomic_add_fetch(&h->moves, 1u, __ATOMIC_RELAXED);
    return tide_heap_block(h, block);
}

uint64_t tide_heap_alloc(tide_heap *h, const uint32_t bytes)
{
    check_allowed(h);
#ifdef TIDE_HEAP_CHECKS
    if (in_runs && h->run != run_now()) {
        fprintf(stderr, "tide: a block was asked of run %u's heap by code on run %u (TIDE_HEAP_CHECKS): "
                        "a run's rows make their blocks in their own run's heap\n", h->run, run_now());
        abort();
    }
#endif
    const uint64_t size = (uint64_t)sizeof(tide_block) + bytes;
    uint32_t c = 0;
    while (c + 1 < TIDE_HEAP_CLASSES && ((uint64_t)16 << c) < size) c++;
    const uint64_t block_size = (uint64_t)16 << c;
    if (block_size < size) tide_out_of_memory(); // Bigger than any block
    if (h->free[c]) {
        const uint64_t block = h->free[c];
        tide_block *b = tide_heap_write(h, block);
        h->free[c] = tide_block_next(b);
        b->link = tide_block_link(c, 0);
        return block;
    }
    if (h->used < FIRST) h->used = FIRST;
    // A block that would cross into the next page starts there instead
    uint64_t at = h->used;
    if ((at & (PAGE - 1u)) + block_size > PAGE) at = (at + PAGE - 1u) & ~(uint64_t)(PAGE - 1u);
    if (at + block_size > LIMIT) tide_out_of_memory();
    // Every place before the block's has its page too: a block of a page or
    // more starts past an empty heap's first, which nothing else is in yet
    if (at >> TIDE_HEAP_PAGE_SHIFT >= h->pages) {
        // Other runs' threads give this heap's pages copies of their own meanwhile (write_shared_in_runs)
        const bool shared = in_runs;
        if (shared) lock(h);
        while (at >> TIDE_HEAP_PAGE_SHIFT > h->pages) add_page(h, 1u);
        if (at >> TIDE_HEAP_PAGE_SHIFT == h->pages) {
            add_page(h, block_size > PAGE ? (uint32_t)(block_size >> TIDE_HEAP_PAGE_SHIFT) : 1u);
        }
        if (shared) unlock(h);
    }
    __atomic_store_n(&h->used, at + block_size, __ATOMIC_RELAXED); // Read by those threads, for how much of a page to copy
    tide_block *b = tide_heap_write(h, at);
    b->link = tide_block_link(c, 0);
    return at;
}

void tide_heap_release(tide_heap *h, const uint64_t block)
{
    if (!block) return;
    check_allowed(h);
    if (tide_heap_away(h)) {
        // Another run's block: that heap's lists are its own run's thread's. Kept by this run's heap instead
        tide_heap *mine = run_of(h, run_now());
        tide_block *b = tide_heap_write(h, block);
        b->link = tide_block_link(tide_block_class(b), mine->away);
        mine->away = (uint64_t)h->run << AWAY_RUN_SHIFT | block;
        __atomic_add_fetch(&h->moves, 1u, __ATOMIC_RELAXED);
        return;
    }
    tide_asset_origin_clear(h, block);
    tide_block *b = tide_heap_write(h, block);
    b->link = tide_block_link(tide_block_class(b), h->pending);
    h->pending = block;
    if (h->base && !in_runs) h->base->runs_pending |= 1u << h->run; // For the owner's flush to find
    __atomic_add_fetch(&h->moves, 1u, __ATOMIC_RELAXED);
}

void tide_heap_flush_pending(tide_heap *h)
{
    check_allowed(h);
    while (h->pending) {
        const uint64_t block = h->pending;
        tide_block *b = tide_heap_write(h, block);
        h->pending = tide_block_next(b);
        const uint32_t c = tide_block_class(b);
        memset(b, 0, (size_t)16u << c); // Freed memory is zero, so it hashes the same everywhere
        b->link = tide_block_link(c, h->free[c]);
        h->free[c] = block;
    }
    // Other runs' blocks this run's rows let go of: zeroed here, on its thread, and kept for the join
    while (h->away) {
        const uint64_t ref = h->away;
        tide_heap *home = run_of(h, (uint32_t)(ref >> AWAY_RUN_SHIFT));
        tide_block *b = tide_heap_write(home, ref & ((UINT64_C(1) << AWAY_RUN_SHIFT) - 1u));
        h->away = tide_block_next(b);
        const uint32_t c = tide_block_class(b);
        memset(b, 0, (size_t)16u << c);
        b->link = tide_block_link(c, h->held);
        h->held = ref;
    }
    // Outside runs, an owner's flush is all of its runs' heaps'
    if (h->runs_pending && !in_runs) {
        const uint32_t which = h->runs_pending;
        h->runs_pending = 0u;
        for (uint32_t run = 1; run < TIDE_HEAP_RUNS; run++) {
            if (which >> run & 1u) tide_heap_flush_pending(run_of(h, run));
        }
    }
}

void tide_heap_drop_later(tide_heap *h, const uint8_t id[32], const uint64_t block)
{
    tide_heap *mine = run_of(h, run_now());
    if (mine->later_count == mine->later_room) {
        const uint32_t room = mine->later_room ? mine->later_room * 2u : 8u;
        mine->later = tide_realloc(mine->later, mine->later_room * sizeof *mine->later, room * sizeof *mine->later);
        mine->later_room = room;
    }
    tide_heap_later *item = &mine->later[mine->later_count++];
    item->home = h;
    item->block = block;
    memcpy(item->id, id, sizeof item->id);
}

void tide_heap_runs_join(tide_heap *base)
{
    // In the runs' order, which is the same whichever threads had them
    for (uint32_t run = 0; run < TIDE_HEAP_RUNS; run++) {
        tide_heap *h = run_made(base, run);
        if (!h) break;
        if (h->pending | h->away) tide_heap_flush_pending(h); // A row always flushes; a run that ended otherwise
        for (uint32_t i = 0; i < h->later_count; i++) tide_asset_drop(h->later[i].home, h->later[i].id, h->later[i].block);
        h->later_count = 0u;
        while (h->held) {
            tide_heap *home = run_of(base, (uint32_t)(h->held >> AWAY_RUN_SHIFT));
            const uint64_t block = h->held & ((UINT64_C(1) << AWAY_RUN_SHIFT) - 1u);
            tide_asset_origin_clear(home, block);
            tide_block *b = tide_heap_write(home, block);
            h->held = tide_block_next(b);
            const uint32_t c = tide_block_class(b);
            b->link = tide_block_link(c, home->free[c]);
            home->free[c] = block;
        }
    }
    for (uint32_t run = 0; run < TIDE_HEAP_RUNS; run++) {
        tide_heap *h = run_made(base, run);
        if (!h) break;
        for (uint32_t i = 0; i < h->retired_count; i++) tide_page_release(h->retired[i], places(h->retired[i]));
        h->retired_count = 0u;
    }
}

void tide_heap_copy(tide_heap *to, const tide_heap *from)
{
    if (to == from) return;
    tide_asset_origins_retain(from->origins);
    tide_asset_origins_release(to->origins);
    to->origins = from->origins;
    tide_asset_pool_retain(from->assets);
    tide_asset_pool_release(to->assets);
    to->assets = from->assets;
    // Its pages first, then letting go of to's: they can be the same ones.
    for (uint32_t i = 0; i < from->pages; i++) tide_page_retain(from->page[i]);
    for (uint32_t i = 0; i < to->pages; i++) tide_page_release(to->page[i], 1);
    make_room(to, from->pages);
    tide_heap_settle(to); // Snapshots are taken while nothing else runs on it
    if (from->pages) memcpy(to->page, from->page, from->pages * sizeof *to->page);
    to->pages = from->pages;
    to->used = from->used;
    memcpy(to->free, from->free, sizeof to->free);
    to->pending = from->pending;
}

uint64_t tide_heap_hash(uint64_t h, const tide_heap *heap)
{
    h = tide_hash_more(h, &heap->used, sizeof heap->used);
    h = tide_hash_more(h, heap->free, sizeof heap->free);
    h = tide_hash_more(h, &heap->pending, sizeof heap->pending);
    for (uint32_t i = 0; i < heap->pages; i += places(heap->page[i])) {
        const uint64_t page = tide_page_hash(heap->page[i], in_use(heap, heap->page[i]));
        h = tide_hash_more(h, &page, sizeof page);
    }
    // Authenticate the descriptor registry too: omitting a lease from received
    // metadata must not pass merely because the descriptor's page is unchanged.
    // This commutative sum is independent of outside-state hash-table ordering.
    if (heap->assets && heap->assets->count) {
        h = tide_hash_more(h, &heap->assets->leases, sizeof heap->assets->leases);
        h = tide_hash_more(h, &heap->assets->descriptors, sizeof heap->assets->descriptors);
    }
    return h;
}

void tide_heap_stale(const tide_heap *heap, tide_stale_pages *s)
{
    for (uint32_t i = 0; i < heap->pages; i += places(heap->page[i])) tide_page_stale(s, heap->page[i], in_use(heap, heap->page[i]));
}

uint64_t tide_heap_block_hash(tide_heap *h, const uint64_t block)
{
    tide_page *p = h->page[block >> TIDE_HEAP_PAGE_SHIFT];
    const uint64_t hash = tide_page_hash(p, in_use(h, p));
    __atomic_add_fetch(&h->moves, 1u, __ATOMIC_RELAXED);
    return hash;
}

void tide_heap_free(tide_heap *h)
{
    tide_asset_origins_release(h->origins);
    tide_asset_pool_release(h->assets);
    for (uint32_t i = 0; i < h->pages; i++) tide_page_release(h->page[i], 1);
    tide_heap_settle(h);
    if (h->page) free(h->page - 1);
    for (uint32_t i = 0; i < h->retired_count; i++) tide_page_release(h->retired[i], places(h->retired[i]));
    free(h->retired);
    free(h->later);
    // Its runs' heaps with it; a run's heap is still its owner's
    for (uint32_t i = 0; h->runs && i < TIDE_HEAP_RUNS - 1u; i++) tide_heap_free(&h->runs[i]);
    free(h->runs);
    tide_heap *base = h->base;
    const uint32_t run = h->run;
    memset(h, 0, sizeof *h);
    h->base = base;
    h->run = run;
}

// Packed: what it's handed out, then each of its pages: how many places it
// takes and the bytes of it in use.

uint32_t tide_heap_packed_size(const tide_heap *h)
{
    uint64_t size = (uint64_t)tide_asset_packed_size(h) + sizeof h->used + sizeof h->free + sizeof h->pending;
    for (uint32_t i = 0; i < h->pages; i += places(h->page[i])) size += 4u + in_use(h, h->page[i]);
    if (size > UINT32_MAX) tide_out_of_memory();
    return (uint32_t)size;
}

void tide_heap_pack(const tide_heap *h, tide_writer *w)
{
    tide_asset_pack_heap(h, w, false, NULL);
    tide_write_u64(w, h->used);
    for (uint32_t c = 0; c < TIDE_HEAP_CLASSES; c++) tide_write_u64(w, h->free[c]);
    tide_write_u64(w, h->pending);
    for (uint32_t i = 0; i < h->pages; i += places(h->page[i])) {
        const tide_page *p = h->page[i];
        tide_write_u32(w, places(p));
        tide_write_bytes(w, tide_page_data(p), in_use(h, p));
    }
}

// What it's handed out holds together: true of any heap, so one that came as
// bytes isn't one without it.
static bool numbers_hold(const tide_heap *h)
{
    if (h->used > LIMIT || (h->used && h->used < FIRST)) return false;
    for (uint32_t c = 0; c < TIDE_HEAP_CLASSES; c++) {
        if (h->free[c] && (h->free[c] < FIRST || h->free[c] >= h->used)) return false;
    }
    return !(h->pending && (h->pending < FIRST || h->pending >= h->used));
}

// ---------------------------------------------------------------------------
// An owner's heaps, one for each run (tide/heap.h, A heap in runs), as the
// functions for a world's heaps go through them: a world's heaps are numbered
// slot by slot, a slot's runs in a row.

static bool blank(const tide_heap *h);

// `to`'s runs' heaps become `from`'s (NULL: none), sharing their pages.
static void copy_runs(tide_heap *to, const tide_heap *from)
{
    for (uint32_t run = 1; run < TIDE_HEAP_RUNS; run++) {
        const tide_heap *theirs = run_made(from, run);
        tide_heap *ours = run_made(to, run);
        if (blank(theirs) && !ours) continue;
        if (!ours) ours = run_of(to, run);
        if (blank(theirs)) tide_heap_free(ours);
        else tide_heap_copy(ours, theirs);
    }
    to->runs_pending = from ? from->runs_pending : 0u;
}

// After its runs' heaps were read from bytes: which of them have blocks waiting to be freed.
static void mark_pending(tide_heap *base)
{
    base->runs_pending = 0u;
    for (uint32_t run = 1; run < TIDE_HEAP_RUNS; run++) {
        const tide_heap *h = run_made(base, run);
        if (h && h->pending) base->runs_pending |= 1u << run;
    }
}

// Whether a page `pages` places in can take `count` of them: one place, or a
// block's own, a power of two of them no more than the biggest block's,
// within the heap's limit.
static bool places_hold(const uint32_t pages, const uint32_t count)
{
    return count != 0 && !(count & (count - 1u)) && (uint64_t)count << TIDE_HEAP_PAGE_SHIFT <= (uint64_t)16u << (TIDE_HEAP_CLASSES - 1u)
        && count <= (LIMIT >> TIDE_HEAP_PAGE_SHIFT) - 1u - pages;
}

bool tide_heap_unpack(tide_heap *h, tide_reader *r)
{
    if (!tide_asset_unpack_heap(h, r, NULL)) { r->failed = true; return false; }
    h->used = tide_read_u64(r);
    for (uint32_t c = 0; c < TIDE_HEAP_CLASSES; c++) h->free[c] = tide_read_u64(r);
    h->pending = tide_read_u64(r);
    // Its pages' bytes in use come to what it's handed out, all of them here
    if (!numbers_hold(h) || h->used > r->size - r->at) r->failed = true;
    while (!r->failed && ((uint64_t)h->pages << TIDE_HEAP_PAGE_SHIFT) < h->used) {
        const uint32_t count = tide_read_u32(r);
        if (!places_hold(h->pages, count)) {
            r->failed = true;
            break;
        }
        add_page(h, count);
        tide_page *p = h->page[h->pages - 1u];
        const uint32_t bytes = in_use(h, p);
        const uint8_t *data = tide_read_bytes(r, bytes);
        if (data) memcpy(tide_page_data(p), data, bytes);
    }
    tide_heap_settle(h);
    if (!r->failed && !tide_asset_validate_heap(h)) r->failed = true;
    return !r->failed;
}

// Packed as part of a delta: its numbers, each page's places as runs of pages
// that take as many, then its pages as regions.

// The base's page that starts at place `at`, or NULL.
static tide_page *base_page(const tide_heap *base, const uint32_t at)
{
    if (!base || at >= base->pages) return NULL;
    tide_page *b = base->page[at];
    return b->first == at ? b : NULL;
}

void tide_heap_pack_delta(const tide_heap *h, const tide_heap *base, tide_delta_writer *d)
{
    tide_asset_pack_heap(h, &d->bytes, d->assets, d->xor ? base : NULL);
    tide_delta_number64(d, h->used);
    for (uint32_t c = 0; c < TIDE_HEAP_CLASSES; c++) tide_delta_number64(d, h->free[c]);
    tide_delta_number64(d, h->pending);
    uint32_t run = 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < h->pages; i += places(h->page[i])) {
        if (run && places(h->page[i]) == count) {
            run++;
            continue;
        }
        if (run) {
            tide_delta_number(d, run);
            tide_delta_number(d, count);
        }
        run = 1;
        count = places(h->page[i]);
    }
    if (run) {
        tide_delta_number(d, run);
        tide_delta_number(d, count);
    }
    for (uint32_t i = 0; i < h->pages; i += places(h->page[i])) {
        const tide_page *p = h->page[i];
        const tide_page *b = base_page(base, i);
        tide_delta_asset_region(d, tide_page_data(p), in_use(h, p), b ? tide_page_data(b) : NULL, b ? in_use(base, b) : 0u,
                          b == p, tide_asset_origin_page(h, i));
    }
    tide_delta_close(d);
}

bool tide_heap_unpack_delta(tide_heap *h, const tide_heap *base, tide_delta_reader *d)
{
    if (!tide_asset_unpack_heap(h, &d->bytes, d->xor ? base : NULL)) return false;
    h->used = tide_delta_get_number64(d);
    for (uint32_t c = 0; c < TIDE_HEAP_CLASSES; c++) h->free[c] = tide_delta_get_number64(d);
    h->pending = tide_delta_get_number64(d);
    if (d->bytes.failed || !numbers_hold(h)) return false;
    // Each page past the base's takes a byte of the delta at least
    if ((h->used >> TIDE_HEAP_PAGE_SHIFT) > (uint64_t)(base ? base->pages : 0u) + (d->bytes.size - d->bytes.at)) return false;
    // The runs of places, read once to get past them, then again with the pages
    tide_reader runs = d->bytes;
    uint32_t run = 0;
    uint32_t count = 0;
    for (uint32_t pages = 0; ((uint64_t)pages << TIDE_HEAP_PAGE_SHIFT) < h->used; pages += count, run--) {
        if (run == 0) {
            run = tide_delta_get_number(d);
            count = tide_delta_get_number(d);
        }
        if (d->bytes.failed || run == 0 || !places_hold(pages, count)) return false;
    }
    if (run) return false; // More pages than it's handed out
    while (((uint64_t)h->pages << TIDE_HEAP_PAGE_SHIFT) < h->used) {
        if (run == 0) {
            run = tide_read_varint(&runs);
            count = tide_read_varint(&runs);
        }
        run--;
        make_room(h, h->pages + count);
        tide_page *b = base_page(base, h->pages);
        const uint32_t size = count << TIDE_HEAP_PAGE_SHIFT;
        const uint64_t left = h->used - ((uint64_t)h->pages << TIDE_HEAP_PAGE_SHIFT);
        const tide_asset_origin *previous = base ? tide_asset_origin_page(base, h->pages) : NULL;
        tide_asset_origin origin = previous ? *previous : (tide_asset_origin){0};
        tide_page *p = tide_delta_asset_page(d, left < size ? (uint32_t)left : size, size, count, b, b ? in_use(base, b) : 0u,
                                       b && b->size == size, &origin);
        if (!p) {
            tide_heap_settle(h);
            return false;
        }
        if (origin.data) tide_asset_origin_set(h, h->pages, origin);
        if (p != b) p->first = h->pages;
        for (uint32_t k = 0; k < count; k++) h->page[h->pages + k] = p;
        h->pages += count;
    }
    tide_heap_settle(h);
    return tide_delta_closed(d) && tide_asset_validate_heap(h);
}

void tide_heap_hash_pages(const tide_heap *h, tide_writer *w)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < h->pages; i += places(h->page[i])) count++;
    tide_write_varint(w, count);
    for (uint32_t i = 0; i < h->pages; i += places(h->page[i])) {
        tide_write_varint(w, places(h->page[i]));
        tide_write_u64(w, tide_page_hash(h->page[i], in_use(h, h->page[i])));
    }
}

void tide_heap_need_pages(const tide_heap *base, tide_needs *n)
{
    const uint32_t count = tide_needs_count(n);
    uint32_t at = 0; // The place their page starts at
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t taking = tide_read_varint(&n->list);
        if (!places_hold(at, taking)) n->list.failed = true;
        tide_page *b = n->list.failed ? NULL : base_page(base, at);
        tide_needs_put(n, b != NULL, b ? tide_page_hash(b, in_use(base, b)) : 0u);
        if (!n->list.failed) at += taking;
    }
}

// ---------------------------------------------------------------------------
// A world's heaps

static const uint16_t no_slots[TIDE_HEAP_IDS]; // Every id is the first heap
static const uint16_t no_ids[1];
const uint16_t *tide_heap_slots = no_slots;
const uint16_t *tide_heap_ids = no_ids;
uint32_t tide_heap_count = 1u;

void tide_heaps_shape(const uint16_t *slots, const uint16_t *ids, const uint32_t count)
{
    tide_heap_slots = slots ? slots : no_slots;
    tide_heap_ids = slots ? ids : no_ids;
    tide_heap_count = slots ? count : 1u;
}

tide_heap *tide_heaps_make(tide_heaps *hs)
{
    tide_heap *all = __atomic_load_n(&hs->all, __ATOMIC_ACQUIRE);
    if (all) return all;
    tide_heap *made = tide_alloc_zeroed(tide_heap_count, sizeof *made);
    if (__atomic_compare_exchange_n(&hs->all, &all, made, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return made;
    free(made); // Another thread's are the world's
    return all;
}

static const tide_heap nothing; // An empty heap, to pack and hash in the place of one that isn't made

// The heap in `slot`, or NULL when the world's heaps aren't made: all empty.
static tide_heap *slot_heap(const tide_heaps *hs, const uint32_t slot)
{
    tide_heap *all = hs ? __atomic_load_n(&hs->all, __ATOMIC_ACQUIRE) : NULL;
    return all ? all + slot : NULL;
}

// ...to read: an empty one for a world whose heaps aren't made.
static const tide_heap *slot_read(const tide_heaps *hs, const uint32_t slot)
{
    const tide_heap *heap = slot_heap(hs, slot);
    return heap ? heap : &nothing;
}

// A world's heaps by number: a slot's first, then its runs', slot by slot. NULL
// for one that isn't made.
#define NUMBERS (tide_heap_count * TIDE_HEAP_RUNS)

static tide_heap *numbered(const tide_heaps *hs, const uint32_t number)
{
    return run_made(slot_heap(hs, number / TIDE_HEAP_RUNS), number % TIDE_HEAP_RUNS);
}

// ...made if it isn't.
static tide_heap *numbered_make(tide_heaps *hs, const uint32_t number)
{
    return run_of(tide_heaps_make(hs) + number / TIDE_HEAP_RUNS, number % TIDE_HEAP_RUNS);
}

// The number after `number` that either of two worlds' heaps (NULL: none) may have a heap for: the next, or the
// next slot's first where neither has made the runs' heaps of this one. Most owners never have.
static uint32_t after(const tide_heaps *a, const tide_heaps *b, const uint32_t number)
{
    const uint32_t slot = number / TIDE_HEAP_RUNS;
    const tide_heap *ours = slot_heap(a, slot), *theirs = slot_heap(b, slot);
    const bool runs = (ours && __atomic_load_n(&ours->runs, __ATOMIC_ACQUIRE)) || (theirs && __atomic_load_n(&theirs->runs, __ATOMIC_ACQUIRE));
    return runs ? number + 1u : (slot + 1u) * TIDE_HEAP_RUNS;
}

// What a packed world and a hash call it: its id, and its run above that.
static uint32_t number_name(const uint32_t number)
{
    return tide_heap_ids[number / TIDE_HEAP_RUNS] | number % TIDE_HEAP_RUNS << TIDE_HEAP_ID_BITS;
}

// Nothing was ever handed out of it: it's as a heap that isn't there.
static bool blank(const tide_heap *h)
{
    return !h || h->used == 0;
}

// Whether a heap is its base's as it stands: what it's handed out, and every
// page the same page.
static bool same_heap(const tide_heap *h, const tide_heap *base)
{
    if (blank(h) || blank(base)) return blank(h) && blank(base);
    if (h->used != base->used || h->pending != base->pending || h->pages != base->pages || h->assets != base->assets
        || memcmp(h->free, base->free, sizeof h->free) != 0) {
        return false;
    }
    for (uint32_t i = 0; i < h->pages; i++) {
        if (h->page[i] != base->page[i]) return false;
    }
    return true;
}

void tide_heaps_copy(tide_heaps *to, const tide_heaps *from)
{
    if (to == from) return;
    const tide_heap *theirs = slot_heap(from, 0);
    tide_heap *ours = slot_heap(to, 0);
    if (!theirs && !ours) return;
    if (!ours) ours = tide_heaps_make(to);
    for (uint32_t i = 0; i < tide_heap_count; i++) {
        if (theirs) tide_heap_copy(&ours[i], &theirs[i]);
        else tide_heap_free(&ours[i]);
        if (theirs && (theirs[i].runs || ours[i].runs)) copy_runs(&ours[i], &theirs[i]);
    }
}

void tide_heaps_keep(tide_heaps *to, const tide_heaps *from, const uint32_t id)
{
    if (to == from) return;
    const tide_heap *theirs = slot_heap(from, 0);
    tide_heap *ours = slot_heap(to, 0);
    if (!theirs && !ours) return;
    if (!ours) ours = tide_heaps_make(to);
    const uint32_t i = tide_heap_slots[id & (TIDE_HEAP_IDS - 1u)];
    if (theirs) tide_heap_copy(&ours[i], &theirs[i]);
    else tide_heap_free(&ours[i]);
    if (theirs && (theirs[i].runs || ours[i].runs)) copy_runs(&ours[i], &theirs[i]);
}

void tide_heaps_free(tide_heaps *hs)
{
    tide_heap *all = slot_heap(hs, 0);
    for (uint32_t i = 0; all && i < tide_heap_count; i++) tide_heap_free(&all[i]);
    free(all);
    hs->all = NULL;
}

void tide_heaps_flush(tide_heaps *hs)
{
    tide_heap *all = slot_heap(hs, 0);
    for (uint32_t i = 0; all && i < tide_heap_count; i++) tide_heap_flush(&all[i]);
}

void tide_heaps_settle(tide_heaps *hs)
{
    tide_heap *all = slot_heap(hs, 0);
    for (uint32_t i = 0; all && i < tide_heap_count; i++) {
        tide_heap_settle(&all[i]);
        for (uint32_t run = 1; all[i].runs && run < TIDE_HEAP_RUNS; run++) tide_heap_settle(&all[i].runs[run - 1u]);
    }
}

// The first heap as a world of one heap hashes, then each of the others that
// has handed anything out, with its id and run: the same whichever were made.
uint64_t tide_heaps_hash(uint64_t h, const tide_heaps *hs)
{
    h = tide_heap_hash(h, slot_read(hs, 0));
    for (uint32_t number = 1; number < NUMBERS; number = after(hs, NULL, number)) {
        const tide_heap *heap = numbered(hs, number);
        if (blank(heap)) continue;
        const uint32_t name = number_name(number);
        h = tide_hash_more(h, &name, sizeof name);
        h = tide_heap_hash(h, heap);
    }
    return h;
}

void tide_heaps_stale(const tide_heaps *hs, tide_stale_pages *s)
{
    for (uint32_t number = 0; number < NUMBERS; number = after(hs, NULL, number)) {
        const tide_heap *heap = numbered(hs, number);
        if (!blank(heap)) tide_heap_stale(heap, s);
    }
}

// Packed: the first heap, as a world of one heap is, then how many of the
// others have handed anything out, and for each its id (its run above it), its
// packed size and the heap.

uint32_t tide_heaps_packed_size(const tide_heaps *hs)
{
    uint64_t size = (uint64_t)tide_heap_packed_size(slot_read(hs, 0)) + sizeof(uint32_t);
    for (uint32_t number = 1; number < NUMBERS; number = after(hs, NULL, number)) {
        const tide_heap *heap = numbered(hs, number);
        if (!blank(heap)) size += 2u * sizeof(uint32_t) + tide_heap_packed_size(heap);
    }
    if (size > UINT32_MAX) tide_out_of_memory();
    return (uint32_t)size;
}

void tide_heaps_pack(const tide_heaps *hs, tide_writer *w)
{
    tide_heap_pack(slot_read(hs, 0), w);
    uint32_t count = 0;
    for (uint32_t number = 1; number < NUMBERS; number = after(hs, NULL, number)) count += !blank(numbered(hs, number));
    tide_write_u32(w, count);
    for (uint32_t number = 1; number < NUMBERS; number = after(hs, NULL, number)) {
        const tide_heap *heap = numbered(hs, number);
        if (blank(heap)) continue;
        tide_write_u32(w, number_name(number));
        tide_write_u32(w, tide_heap_packed_size(heap));
        tide_heap_pack(heap, w);
    }
}

bool tide_heaps_unpack(tide_heaps *hs, tide_reader *r)
{
    if (!tide_heap_unpack(tide_heaps_of(hs, 0u), r)) return false;
    const uint32_t count = tide_read_u32(r);
    for (uint32_t i = 0; i < count && !r->failed; i++) {
        const uint32_t name = tide_read_u32(r);
        const uint32_t size = tide_read_u32(r);
        const uint8_t *bytes = tide_read_bytes(r, size);
        const uint32_t id = name & (TIDE_HEAP_IDS - 1u);
        if (!bytes || name == 0 || name >= TIDE_HEAP_IDS * TIDE_HEAP_RUNS) {
            r->failed = true;
            break;
        }
        // An owner this build of the game hasn't (the world is another build's,
        // carried over): no field is left to say where its blocks are
        const uint32_t slot = tide_heap_slots[id];
        if (id && (!slot || tide_heap_ids[slot] != id)) continue;
        tide_heap *heap = tide_heaps_of(hs, name);
        tide_reader part = {bytes, size, 0, false};
        if (!blank(heap) || !tide_heap_unpack(heap, &part) || part.at != size) r->failed = true;
    }
    tide_heap *all = slot_heap(hs, 0);
    for (uint32_t slot = 0; all && slot < tide_heap_count; slot++) mark_pending(&all[slot]);
    return !r->failed;
}

// In a delta: the first heap, then how many of the others aren't their
// base's as it stands, and for each its number and the heap. The rest are the
// base's, shared.

void tide_heaps_pack_delta(const tide_heaps *hs, const tide_heaps *base, tide_delta_writer *d)
{
    tide_heap_pack_delta(slot_read(hs, 0), slot_heap(base, 0), d);
    uint32_t count = 0;
    for (uint32_t number = 1; number < NUMBERS; number = after(hs, base, number)) count += !same_heap(numbered(hs, number), numbered(base, number));
    tide_delta_number(d, count);
    for (uint32_t number = 1; number < NUMBERS; number = after(hs, base, number)) {
        const tide_heap *heap = numbered(hs, number), *from = numbered(base, number);
        if (same_heap(heap, from)) continue;
        tide_delta_number(d, number);
        tide_heap_pack_delta(heap ? heap : &nothing, blank(from) ? NULL : from, d);
    }
}

bool tide_heaps_unpack_delta(tide_heaps *hs, const tide_heaps *base, tide_delta_reader *d)
{
    if (!tide_heap_unpack_delta(tide_heaps_of(hs, 0u), slot_heap(base, 0), d)) return false;
    const uint32_t count = tide_delta_get_number(d);
    if (d->bytes.failed || count >= NUMBERS) return !d->bytes.failed && count == 0;
    uint32_t next = 1; // The first number not yet read or taken from the base
    for (uint32_t i = 0; i <= count; i++) {
        const uint32_t number = i < count ? tide_delta_get_number(d) : NUMBERS;
        if (d->bytes.failed || number < next || number > NUMBERS || (i < count && number == NUMBERS)) return false;
        for (; next < number; next = after(base, NULL, next)) { // As the base's are
            const tide_heap *from = numbered(base, next);
            if (!blank(from)) tide_heap_copy(numbered_make(hs, next), from);
        }
        if (i == count) break;
        const tide_heap *from = numbered(base, number);
        if (!tide_heap_unpack_delta(numbered_make(hs, number), blank(from) ? NULL : from, d)) return false;
        next = number + 1u;
    }
    tide_heap *all = slot_heap(hs, 0);
    for (uint32_t slot = 0; all && slot < tide_heap_count; slot++) mark_pending(&all[slot]);
    return true;
}

uint64_t tide_heaps_used(const tide_heaps *hs)
{
    uint64_t used = 0;
    for (uint32_t number = 0; number < NUMBERS; number = after(hs, NULL, number)) {
        const tide_heap *heap = numbered(hs, number);
        if (heap) used += heap->used;
    }
    return used;
}

bool tide_heaps_pending(const tide_heaps *hs)
{
    bool pending = false;
    const tide_heap *all = slot_heap(hs, 0);
    for (uint32_t i = 0; all && i < tide_heap_count; i++) pending |= all[i].pending != 0 || all[i].runs_pending != 0;
    return pending;
}

uint32_t tide_heaps_pages(const tide_heaps *hs)
{
    uint32_t pages = 0;
    for (uint32_t number = 0; number < NUMBERS; number = after(hs, NULL, number)) {
        const tide_heap *heap = numbered(hs, number);
        if (heap) pages += heap->pages;
    }
    return pages;
}

uint32_t tide_heaps_pages_apart(const tide_heaps *hs, const tide_heaps *other)
{
    uint32_t apart = 0;
    for (uint32_t number = 0; number < NUMBERS; number = after(hs, other, number)) {
        const tide_heap *heap = numbered(hs, number), *theirs = numbered(other, number);
        for (uint32_t i = 0; heap && i < heap->pages; i++) apart += !theirs || i >= theirs->pages || heap->page[i] != theirs->page[i];
    }
    return apart;
}

// Each slot's first heap's pages, then how many of its runs' heaps have handed
// anything out, and each of those with its run before it.

void tide_heaps_hash_pages(const tide_heaps *hs, tide_writer *w)
{
    for (uint32_t slot = 0; slot < tide_heap_count; slot++) {
        const tide_heap *first = slot_heap(hs, slot);
        tide_heap_hash_pages(first ? first : &nothing, w);
        uint32_t count = 0;
        for (uint32_t run = 1; run < TIDE_HEAP_RUNS; run++) count += !blank(run_made(first, run));
        tide_write_varint(w, count);
        for (uint32_t run = 1; run < TIDE_HEAP_RUNS; run++) {
            const tide_heap *heap = run_made(first, run);
            if (blank(heap)) continue;
            tide_write_varint(w, run);
            tide_heap_hash_pages(heap, w);
        }
    }
}

void tide_heaps_need_pages(const tide_heaps *base, tide_needs *n)
{
    for (uint32_t slot = 0; slot < tide_heap_count; slot++) {
        const tide_heap *first = slot_heap(base, slot);
        tide_heap_need_pages(first, n);
        const uint32_t count = tide_read_varint(&n->list);
        uint32_t past = 0; // Runs in order, each once
        for (uint32_t i = 0; i < count && !n->list.failed; i++) {
            const uint32_t run = tide_read_varint(&n->list);
            if (run <= past || run >= TIDE_HEAP_RUNS) {
                n->list.failed = true;
                break;
            }
            past = run;
            const tide_heap *heap = run_made(first, run);
            tide_heap_need_pages(blank(heap) ? NULL : heap, n);
        }
    }
}
