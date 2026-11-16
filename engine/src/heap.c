#include "tide/heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asset_store.h"
#include "delta_asset.h"

// See tide/heap.h.

#define PAGE (1u << TIDE_HEAP_PAGE_SHIFT)

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
// Places (tide/heap.h)

#define SMALL TIDE_HEAP_SMALL
#define ROWS TIDE_HEAP_ROWS
#define ROW_FREE 0u // The index's row of free places; a class's pages with a block to spare are row 1 + class
// Beside its class in a free block's link: what no block handed out or released has there, as a class is below
// TIDE_HEAP_CLASSES, so a block freed already is never freed again
#define FREE_MARK 0x80u

// A class's blocks in a page of their own, and whether a place's kind is a class's page.
static uint32_t per_page(const uint32_t c)
{
    return PAGE >> (c + 4u);
}

static bool is_class(const uint32_t kind)
{
    return kind - 1u < SMALL;
}

// The places from `at` that what's there takes: a block's own page's, or one.
static uint32_t span(const tide_heap *h, const uint32_t at)
{
    return h->place[at].kind == TIDE_PLACE_BLOCK ? 1u << h->place[at].shift : 1u;
}

// The bytes in use of the page at `at` (tide_heap_page_used), by its place alone.
static uint32_t used_at(const tide_heap *h, const uint32_t at)
{
    const tide_heap_place pl = h->place[at];
    return is_class(pl.kind) ? (uint32_t)pl.made << (pl.kind + 3u) : span(h, at) << TIDE_HEAP_PAGE_SHIFT;
}

// The page at `at` of `h` (NULL: none) where one starts there, else NULL.
static tide_page *page_at(const tide_heap *h, const uint32_t at)
{
    if (!h || at >= h->pages) return NULL;
    tide_page *p = h->page[at];
    return p && p->first == at ? p : NULL;
}

static void set_pages(tide_heap *h, const uint32_t pages)
{
    __atomic_store_n(&h->pages, pages, __ATOMIC_RELAXED); // Read by other runs' threads (tide_heap_has)
}

// The index (tide_heap.index): a word for each 64 places in each row.
static uint32_t groups(const uint32_t count)
{
    return (count + 63u) / 64u;
}

static uint64_t *word_of(tide_heap *h, const uint32_t row, const uint32_t at)
{
    return &h->index[(size_t)(at >> 6) * ROWS + row];
}

// The lowest place from `from` on in `row`, or 0.
static uint32_t marked_from(const tide_heap *h, const uint32_t row, const uint32_t from)
{
    const uint32_t end = groups(h->pages);
    for (uint32_t g = from >> 6; g < end; g++) {
        uint64_t w = h->index[(size_t)g * ROWS + row];
        if (g == from >> 6) w &= ~UINT64_C(0) << (from & 63u);
        if (w) return g << 6 | (uint32_t)__builtin_ctzll(w);
    }
    return 0;
}

static void mark(tide_heap *h, const uint32_t row, const uint32_t at)
{
    *word_of(h, row, at) |= UINT64_C(1) << (at & 63u);
    if (!h->low[row] || at < h->low[row]) h->low[row] = at;
}

static void unmark(tide_heap *h, const uint32_t row, const uint32_t at)
{
    *word_of(h, row, at) &= ~(UINT64_C(1) << (at & 63u));
    if (h->low[row] == at) h->low[row] = marked_from(h, row, at + 1u);
}

// The index again from the places, for a heap read from bytes.
static void index_places(tide_heap *h)
{
    memset(h->low, 0, sizeof h->low);
    if (!h->pages) return;
    memset(h->index, 0, (size_t)groups(h->pages) * ROWS * sizeof *h->index);
    for (uint32_t i = 1; i < h->pages; i++) {
        const tide_heap_place pl = h->place[i];
        if (pl.kind == TIDE_PLACE_FREE) mark(h, ROW_FREE, i);
        else if (is_class(pl.kind) && pl.out < per_page(pl.kind - 1u)) mark(h, pl.kind, i);
    }
}

// What's at each place holds together, as it does in any heap: false for places that came as bytes and aren't a
// heap's. The bytes of its blocks handed out, in `taken`.
static bool places_hold(const tide_heap_place *place, const uint32_t pages, uint64_t *taken)
{
    *taken = 0;
    if (!pages) return true;
    const tide_heap_place none = {0};
    if (pages < 2u || memcmp(&place[0], &none, sizeof none) != 0 || place[pages - 1u].kind == TIDE_PLACE_FREE) return false;
    for (uint32_t i = 1; i < pages; i++) {
        const tide_heap_place pl = place[i];
        if (pl.kind == TIDE_PLACE_FREE) {
            if (memcmp(&pl, &none, sizeof none) != 0) return false;
        } else if (is_class(pl.kind)) {
            const uint32_t c = pl.kind - 1u;
            if (pl.shift || pl.made > per_page(c) || !pl.out || pl.out > pl.made || pl.free > pl.made
                || (pl.free == 0) != (pl.out == pl.made)) {
                return false;
            }
            *taken += (uint64_t)pl.out << (c + 4u);
        } else if (pl.kind == TIDE_PLACE_BLOCK) {
            if (pl.shift >= TIDE_HEAP_CLASSES - SMALL || pl.made || pl.free || pl.out) return false;
            const uint32_t count = 1u << pl.shift;
            if (count > pages - i) return false;
            for (uint32_t k = 1; k < count; k++) {
                const tide_heap_place more = {TIDE_PLACE_MORE, pl.shift, 0, 0, 0};
                if (memcmp(&place[i + k], &more, sizeof more) != 0) return false;
            }
            *taken += (uint64_t)PAGE << pl.shift;
            i += count - 1u;
        } else {
            return false;
        }
    }
    return true;
}

// Whether `block` is where a block of `h` starts, handed out or released: in a class's page among the blocks it
// has made, or a block's own page.
static bool block_starts(const tide_heap *h, const uint64_t block)
{
    if (!block || block >> TIDE_HEAP_PAGE_SHIFT >= h->pages) return false;
    const tide_heap_place pl = h->place[block >> TIDE_HEAP_PAGE_SHIFT];
    if (pl.kind == TIDE_PLACE_BLOCK) return !(block & (PAGE - 1u));
    if (!is_class(pl.kind)) return false;
    const uint32_t shift = pl.kind + 3u;
    return !(block & ((UINT64_C(1) << shift) - 1u)) && (uint32_t)(block & (PAGE - 1u)) >> shift < pl.made;
}

// The next of a page's free blocks after the one at `index` (as 1 + its number, 0 for none), or past `made` for a
// link that isn't one.
static uint32_t free_after(const tide_page *p, const uint32_t index, const uint32_t shift, const uint32_t made)
{
    const tide_block *b = (const tide_block *)(const void *)((const uint8_t *)tide_page_data(p) + ((size_t)index << shift));
    const uint64_t next = tide_block_next(b);
    return next > made || tide_block_class(b) != ((shift - 4u) | FREE_MARK) ? made + 1u : (uint32_t)next;
}

// Each page's free blocks, and the blocks waiting to be freed, hold together: false for a heap that came as bytes
// and isn't one.
static bool chains_hold(const tide_heap *h)
{
    for (uint32_t i = 1; i < h->pages; i += span(h, i)) {
        const tide_heap_place pl = h->place[i];
        if (!is_class(pl.kind)) continue;
        // Exactly as many as aren't handed out, the last linking none: so none twice
        uint32_t next = pl.free;
        for (uint32_t left = pl.made - pl.out; left; left--) {
            if (!next || next > pl.made) return false;
            next = free_after(h->page[i], next - 1u, pl.kind + 3u, pl.made);
        }
        if (next) return false;
    }
    uint64_t left = h->taken / 16u + 1u; // More than there are blocks: a list that goes round
    for (uint64_t at = h->pending; at; at = tide_block_next(tide_heap_block(h, at))) {
        if (!left-- || !block_starts(h, at) || tide_block_class(tide_heap_block(h, at)) >= TIDE_HEAP_CLASSES) return false;
    }
    return true;
}

bool tide_heap_handed_out(const tide_heap *h, const uint64_t block, const uint32_t size_class)
{
    if (size_class >= SMALL || !block_starts(h, block)) return false;
    const uint32_t at = (uint32_t)(block >> TIDE_HEAP_PAGE_SHIFT);
    const tide_heap_place pl = h->place[at];
    if (pl.kind != 1u + size_class) return false;
    const uint32_t shift = size_class + 4u, index = (uint32_t)(block & (PAGE - 1u)) >> shift;
    uint32_t next = pl.free;
    for (uint32_t left = pl.made - pl.out; left && next && next <= pl.made; left--) {
        if (next - 1u == index) return false;
        next = free_after(h->page[at], next - 1u, shift, pl.made);
    }
    return true;
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

// A page of `h` is about to be replaced with a copy: counted in `moves` before the copy goes in the table. The
// copy is put there with a release store, and code finds a page with an acquire load (tide_heap_block,
// tide_heap_write), so a thread that finds the copy then reads `moves` as counted, or later. A cache (a grid's
// chunk, tide/grid.h; a list's block, tide/list.h) reads `moves` before it finds its block and holds while that
// hasn't changed, so the order is what keeps it from holding a pointer into the old page: a thread that took
// `moves` and then the old page, and has since found the copy itself (writing its own row's grid through another
// function's cache, say), reads `moves` changed and finds its block again, where with the count after the store
// it could read it unchanged and go on reading the old page, without the write. A count is a relaxed RMW, as
// every reader's is a relaxed load: the release store after it is what orders it.
static void moved(tide_heap *h)
{
    __atomic_add_fetch(&h->moves, 1u, __ATOMIC_RELAXED);
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
        // Whole: how much of it is in use is the heap's own run's thread's to know (its places), which may be
        // handing out blocks of it meanwhile, into the page it's copying first too
        tide_page *copy = tide_page_copy(p, places(p), p->size);
        lock(h);
        const bool mine = h->page[place] == p;
        if (mine) {
            moved(h); // Before the copy is in the table (moved)
            for (uint32_t k = 0; k < places(copy); k++) __atomic_store_n(&h->page[copy->first + k], copy, __ATOMIC_RELEASE);
            if (h->retired_count == h->retired_room) {
                const uint32_t room = h->retired_room ? h->retired_room * 2u : 16u;
                h->retired = tide_realloc(h->retired, h->retired_room * sizeof *h->retired, room * sizeof *h->retired);
                h->retired_room = room;
            }
            h->retired[h->retired_count++] = p;
        }
        unlock(h);
        if (mine) break;
        copy->refs = 0;
        tide_page_give(copy); // Another thread's copy is the heap's
    }
    return tide_heap_block(h, block);
}

// Bytes of `p` in use (tide_heap_page_used).
static uint32_t in_use(const tide_heap *h, const tide_page *p)
{
    return tide_heap_page_used(h, p);
}

// A bigger page table, which takes the old one's place as a whole. The old
// one stays, linked from page[-1], for code on other threads still reading it,
// until tide_heap_settle. What's at each place, and the index, grow with it.
static void make_room(tide_heap *h, const uint32_t count)
{
    if (count <= h->room) return;
    uint32_t room = h->room ? h->room : 4u;
    while (room < count) room = room > UINT32_MAX / 2u ? UINT32_MAX : room * 2u;
    tide_page **base = tide_alloc_zeroed((size_t)room + 1u, sizeof *base);
    tide_page **grown = base + 1;
    if (h->room) memcpy(grown, h->page, h->room * sizeof *grown);
    base[0] = h->page ? (tide_page *)(void *)(h->page - 1) : NULL;
    __atomic_store_n(&h->page, grown, __ATOMIC_RELEASE);
    h->place = tide_realloc(h->place, h->room * sizeof *h->place, (size_t)room * sizeof *h->place);
    memset(h->place + h->room, 0, (size_t)(room - h->room) * sizeof *h->place);
    const size_t had = (size_t)groups(h->room) * ROWS, has = (size_t)groups(room) * ROWS;
    h->index = tide_realloc(h->index, had * sizeof *h->index, has * sizeof *h->index);
    memset(h->index + had, 0, (has - had) * sizeof *h->index);
    h->room = room;
}

// `p` (or NULL) at `count` places from `at`, as code on other threads reads them.
static void put_pages(tide_heap *h, const uint32_t at, const uint32_t count, tide_page *p)
{
    for (uint32_t k = 0; k < count; k++) __atomic_store_n(&h->page[at + k], p, __ATOMIC_RELEASE);
}

// A page for `count` places: one of a place dropped, zeroed, where there's one of that size, else a new one.
static tide_page *new_page(tide_heap *h, const uint32_t count)
{
    const uint32_t size = count << TIDE_HEAP_PAGE_SHIFT;
    for (uint32_t i = h->dropped_count; count == 1u && i-- > 0;) {
        tide_page *p = h->dropped[i];
        if (p->size != size) continue;
        h->dropped[i] = h->dropped[--h->dropped_count];
        memset(tide_page_data(p), 0, size);
        p->hashed = UINT32_MAX;
        p->refs = count;
        return p;
    }
    tide_page *p = tide_page_new(size);
    p->refs = count;
    return p;
}

// Lets go of the page of `count` freed places: at once where a snapshot has it too, which keeps it; else when
// the heap is settled, as another thread may still hold it from the table (one that only reads it, as
// cached_codegen.inc's keys do), unless a place taken first has it again.
static void drop(tide_heap *h, tide_page *p, const uint32_t count)
{
    if (p->refs != count) {
        tide_page_release(p, count);
        return;
    }
    if (h->dropped_count == h->dropped_room) {
        const uint32_t room = h->dropped_room ? h->dropped_room * 2u : 8u;
        h->dropped = tide_realloc(h->dropped, h->dropped_room * sizeof *h->dropped, room * sizeof *h->dropped);
        h->dropped_room = room;
    }
    h->dropped[h->dropped_count++] = p;
}

// The lowest run of `count` free places, taken, or as many new ones at the end.
static uint32_t take_places(tide_heap *h, const uint32_t count)
{
    uint32_t at = 0;
    if (count == 1u) {
        at = h->low[ROW_FREE];
    } else {
        for (uint32_t p = h->low[ROW_FREE]; p && !at;) {
            uint32_t k = 1; // The last place is never free, so a run of them ends before it
            while (k < count && p + k < h->pages && h->place[p + k].kind == TIDE_PLACE_FREE) k++;
            if (k == count) at = p;
            else p = marked_from(h, ROW_FREE, p + k);
        }
    }
    if (at) {
        for (uint32_t k = 0; k < count; k++) unmark(h, ROW_FREE, at + k);
        return at;
    }
    at = h->pages ? h->pages : 1u; // Place 0 is no block's
    if ((uint64_t)at + count > UINT32_MAX) tide_out_of_memory();
    // Other runs' threads give this heap's pages copies of their own meanwhile (write_shared_in_runs)
    const bool shared = in_runs;
    if (shared) lock(h);
    make_room(h, at + count);
    if (shared) unlock(h);
    set_pages(h, at + count);
    return at;
}

// A page at `count` places from `at`, taken for it.
static tide_page *place_page(tide_heap *h, const uint32_t at, const uint32_t count)
{
    tide_page *p = new_page(h, count);
    p->first = at;
    const bool shared = in_runs;
    if (shared) lock(h);
    put_pages(h, at, count, p);
    if (shared) unlock(h);
    return p;
}

// `count` places from `at` are free again, their page let go of, and those at the end no longer the heap's.
static void free_places(tide_heap *h, const uint32_t at, const uint32_t count)
{
    tide_page *p = h->page[at];
    const tide_heap_place pl = h->place[at];
    if (is_class(pl.kind)) unmark(h, pl.kind, at); // Its one block left: it had blocks to spare
    const bool shared = in_runs;
    if (shared) lock(h);
    put_pages(h, at, count, NULL);
    if (shared) unlock(h);
    for (uint32_t k = 0; k < count; k++) {
        h->place[at + k] = (tide_heap_place){0};
        mark(h, ROW_FREE, at + k);
    }
    drop(h, p, count);
    while (h->pages > 1u && h->place[h->pages - 1u].kind == TIDE_PLACE_FREE) {
        unmark(h, ROW_FREE, h->pages - 1u);
        set_pages(h, h->pages - 1u);
    }
    if (h->pages == 1u) set_pages(h, 0u); // Nothing left: as an empty heap
}

// The block at `block` is free: zeroed, among its page's free blocks, or its page's places free with it where it
// was the last. Anything that isn't where a block of the heap starts is left as it is.
static void free_block(tide_heap *h, const uint64_t block)
{
    if (!block_starts(h, block)) return;
    const uint32_t at = (uint32_t)(block >> TIDE_HEAP_PAGE_SHIFT);
    tide_heap_place *pl = &h->place[at];
    if (pl->kind == TIDE_PLACE_BLOCK) {
        h->taken -= (uint64_t)PAGE << pl->shift;
        free_places(h, at, 1u << pl->shift);
        return;
    }
    const uint32_t c = pl->kind - 1u;
    h->taken -= (uint64_t)16u << c;
    if (pl->out == 1u) {
        free_places(h, at, 1u);
        return;
    }
    tide_block *b = tide_heap_write(h, block);
    memset(b, 0, (size_t)16u << c); // Freed memory is zero, so it hashes the same everywhere
    if (pl->out-- == per_page(c)) mark(h, pl->kind, at);
    const uint32_t index = (uint32_t)(block & (PAGE - 1u)) >> (c + 4u);
    if (index + 1u != pl->made) {
        b->link = tide_block_link(c | FREE_MARK, pl->free);
        pl->free = (uint16_t)(index + 1u);
        return;
    }
    // The last it made: the page is in use only as far as the block before, and so on down the free blocks that
    // were freed last, while each is the last again
    pl->made--;
    while (pl->free && pl->free == pl->made) {
        tide_block *top = tide_heap_write(h, (uint64_t)at << TIDE_HEAP_PAGE_SHIFT | (uint64_t)(pl->made - 1u) << (c + 4u));
        pl->free = (uint16_t)tide_block_next(top);
        top->link = 0;
        pl->made--;
    }
}

void tide_heap_settle(tide_heap *h)
{
    tide_asset_settle(h);
    for (uint32_t i = 0; i < h->dropped_count; i++) tide_page_release(h->dropped[i], h->dropped[i]->refs);
    h->dropped_count = 0;
    if (!h->page) return;
    void *old = h->page[-1];
    while (old) {
        tide_page **base = old;
        old = base[0];
        free(base);
    }
    h->page[-1] = NULL;
}

tide_block *tide_heap_write_shared(tide_heap *h, const uint64_t block)
{
    check_allowed(h);
    if (in_runs) return write_shared_in_runs(h, block);
    tide_page *p = h->page[block >> TIDE_HEAP_PAGE_SHIFT];
    tide_page *own = tide_page_own(p, places(p), in_use(h, p));
    if (own != p) { // Code reading the old page meanwhile reads the same bytes
        moved(h); // Before the copy is in the table (moved)
        for (uint32_t k = 0; k < places(own); k++) __atomic_store_n(&h->page[own->first + k], own, __ATOMIC_RELEASE);
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
    moved(h); // Before the copy is in the table (moved)
    for (uint32_t k = 0; k < places(own); k++) __atomic_store_n(&h->page[own->first + k], own, __ATOMIC_RELEASE);
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
    h->taken += block_size;
    if (h->taken > h->most) h->most = h->taken;
    if (c >= SMALL) { // A page of its own, in the lowest places free
        const uint32_t shift = c - SMALL, count = 1u << shift;
        const uint32_t at = take_places(h, count);
        h->place[at] = (tide_heap_place){TIDE_PLACE_BLOCK, (uint8_t)shift, 0, 0, 0};
        for (uint32_t k = 1; k < count; k++) h->place[at + k] = (tide_heap_place){TIDE_PLACE_MORE, (uint8_t)shift, 0, 0, 0};
        tide_page *p = place_page(h, at, count);
        ((tide_block *)tide_page_data(p))->link = tide_block_link(c, 0);
        return (uint64_t)at << TIDE_HEAP_PAGE_SHIFT;
    }
    // The lowest page of its class with one to spare, else a page in the lowest place free
    const uint32_t kind = 1u + c;
    uint32_t at = h->low[kind];
    if (!at) {
        at = take_places(h, 1u);
        h->place[at] = (tide_heap_place){(uint8_t)kind, 0, 0, 0, 0};
        place_page(h, at, 1u);
        mark(h, kind, at);
    }
    tide_heap_place *pl = &h->place[at];
    const uint32_t shift = c + 4u;
    uint64_t block = (uint64_t)at << TIDE_HEAP_PAGE_SHIFT;
    tide_block *b;
    if (pl->free) { // The last of its blocks freed
        block |= (uint64_t)(pl->free - 1u) << shift;
        b = tide_heap_write(h, block);
        const uint64_t next = tide_block_next(b); // Never past what it's made, in any heap that holds together
        pl->free = next <= pl->made ? (uint16_t)next : 0u;
    } else { // The next it hasn't made
        block |= (uint64_t)pl->made++ << shift;
        b = tide_heap_write(h, block);
    }
    b->link = tide_block_link(c, 0);
    if (++pl->out == per_page(c)) unmark(h, kind, at);
    return block;
}

void tide_heap_release(tide_heap *h, const uint64_t block)
{
    if (!block) return;
    check_allowed(h);
    // Not a block of this heap's, or one freed already (a handle that came as bytes): nothing to do
    if (!tide_heap_has(h, block, sizeof(tide_block)) || tide_block_class(tide_heap_block(h, block)) >= TIDE_HEAP_CLASSES) return;
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
        if (!block_starts(h, block)) { // Only a list that came as bytes and went round: the rest isn't the heap's
            h->pending = 0;
            break;
        }
        h->pending = tide_block_next(tide_heap_block(h, block));
        free_block(h, block);
    }
    // Other runs' blocks this run's rows let go of: zeroed here, on its thread, and kept for the join
    while (h->away) {
        const uint64_t ref = h->away;
        tide_heap *home = run_of(h, (uint32_t)(ref >> AWAY_RUN_SHIFT));
        const uint64_t block = ref & ((UINT64_C(1) << AWAY_RUN_SHIFT) - 1u);
        tide_block *b = tide_heap_write(home, block);
        h->away = tide_block_next(b);
        const uint32_t c = tide_block_class(b);
        // Its header's class (below TIDE_HEAP_CLASSES, tide_heap_release), as far as its page goes
        const tide_page *p = __atomic_load_n(&__atomic_load_n(&home->page, __ATOMIC_ACQUIRE)[block >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
        const uint64_t room = ((uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT) + p->size - block;
        memset(b, 0, (size_t)((uint64_t)16u << c < room ? (uint64_t)16u << c : room));
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
            h->held = tide_block_next(tide_heap_block(home, block));
            free_block(home, block);
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
    // Its pages first, then letting go of to's: they can be the same ones
    for (uint32_t i = 0; i < from->pages; i++) {
        if (from->page[i]) tide_page_retain(from->page[i]);
    }
    for (uint32_t i = 0; i < to->pages; i++) tide_page_release(to->page[i], 1);
    make_room(to, from->pages);
    tide_heap_settle(to); // Snapshots are taken while nothing else runs on it
    const uint32_t had = to->pages, has = from->pages;
    if (has) {
        memcpy(to->page, from->page, has * sizeof *to->page);
        memcpy(to->place, from->place, has * sizeof *to->place);
        memcpy(to->index, from->index, (size_t)groups(has) * ROWS * sizeof *to->index);
    }
    if (had > has) { // Past its places, as a heap's are: nothing
        memset(to->page + has, 0, (had - has) * sizeof *to->page);
        memset(to->place + has, 0, (had - has) * sizeof *to->place);
        memset(to->index + (size_t)groups(has) * ROWS, 0, (size_t)(groups(had) - groups(has)) * ROWS * sizeof *to->index);
    }
    memcpy(to->low, from->low, sizeof to->low);
    to->pages = has;
    to->taken = from->taken;
    to->most = from->most;
    to->pending = from->pending;
}

uint64_t tide_heap_hash(uint64_t h, const tide_heap *heap)
{
    h = tide_hash_more(h, &heap->pages, sizeof heap->pages);
    if (heap->pages) h = tide_hash_more(h, heap->place, heap->pages * sizeof *heap->place);
    h = tide_hash_more(h, &heap->pending, sizeof heap->pending);
    for (uint32_t i = 1; i < heap->pages; i += span(heap, i)) {
        tide_page *p = heap->page[i];
        if (!p) continue;
        const uint64_t page = tide_page_hash(p, in_use(heap, p));
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
    for (uint32_t i = 1; i < heap->pages; i += span(heap, i)) {
        if (heap->page[i]) tide_page_stale(s, heap->page[i], in_use(heap, heap->page[i]));
    }
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
    free(h->dropped);
    free(h->place);
    free(h->index);
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

// Packed: its blocks waiting to be freed and its places, what's at each, and
// each page's bytes in use: a class's page as its free blocks' numbers, last
// freed first, and the bytes of the rest, so a free block takes two bytes.
// A free place is its 8 bytes there, and nothing else.

static uint32_t packed_page_size(const tide_heap *h, const uint32_t at)
{
    const tide_heap_place pl = h->place[at];
    if (!is_class(pl.kind)) return used_at(h, at);
    return (uint32_t)(pl.made - pl.out) * 2u + ((uint32_t)pl.out << (pl.kind + 3u));
}

uint32_t tide_heap_packed_size(const tide_heap *h)
{
    uint64_t size = (uint64_t)tide_asset_packed_size(h) + sizeof h->pending + sizeof h->pages
        + (uint64_t)h->pages * sizeof *h->place;
    for (uint32_t i = 1; i < h->pages; i += span(h, i)) {
        if (h->page[i]) size += packed_page_size(h, i);
    }
    if (size > UINT32_MAX) tide_out_of_memory();
    return (uint32_t)size;
}

// A class's page's blocks, a bit each, in words of 64.
#define PAGE_WORDS (PAGE / 16u / 64u)

static bool is_set(const uint64_t *bits, const uint32_t i)
{
    return bits[i >> 6] >> (i & 63u) & 1u;
}

static void pack_page(const tide_heap *h, const uint32_t at, tide_writer *w)
{
    const tide_heap_place pl = h->place[at];
    const uint8_t *data = tide_page_data(h->page[at]);
    if (!is_class(pl.kind)) {
        tide_write_bytes(w, data, used_at(h, at));
        return;
    }
    const uint32_t shift = pl.kind + 3u;
    uint64_t freed[PAGE_WORDS] = {0};
    uint32_t next = pl.free;
    for (uint32_t left = pl.made - pl.out; left && next; left--) {
        tide_write_u16(w, (uint16_t)next);
        freed[(next - 1u) >> 6] |= UINT64_C(1) << ((next - 1u) & 63u);
        next = free_after(h->page[at], next - 1u, shift, pl.made);
    }
    // The blocks handed out, in runs of them
    for (uint32_t i = 0; i < pl.made;) {
        if (is_set(freed, i)) {
            i++;
            continue;
        }
        uint32_t end = i + 1u;
        while (end < pl.made && !is_set(freed, end)) end++;
        tide_write_bytes(w, data + ((size_t)i << shift), (end - i) << shift);
        i = end;
    }
}

void tide_heap_pack(const tide_heap *h, tide_writer *w)
{
    tide_asset_pack_heap(h, w, false, NULL);
    tide_write_u64(w, h->pending);
    tide_write_u32(w, h->pages);
    if (h->pages) tide_write_bytes(w, h->place, h->pages * sizeof *h->place);
    for (uint32_t i = 1; i < h->pages; i += span(h, i)) {
        if (h->page[i]) pack_page(h, i, w);
    }
}

// The page at a place `pl` of a heap being read, from what pack_page wrote: false where it isn't one. Its free
// blocks' numbers say each once, the first the place's, below what it's made; each is zeros and its link again.
static bool unpack_page(const tide_heap_place pl, tide_page *p, tide_reader *r)
{
    uint8_t *data = tide_page_data(p);
    if (!is_class(pl.kind)) {
        const uint8_t *bytes = tide_read_bytes(r, p->size);
        if (bytes) memcpy(data, bytes, p->size);
        return bytes != NULL;
    }
    const uint32_t c = pl.kind - 1u, shift = c + 4u, count = pl.made - pl.out;
    uint64_t freed[PAGE_WORDS] = {0};
    uint32_t last = 0;
    for (uint32_t k = 0; k < count; k++) {
        const uint32_t next = tide_read_u16(r);
        if (r->failed || !next || next > pl.made || (k == 0 && next != pl.free) || is_set(freed, next - 1u)) return false;
        freed[(next - 1u) >> 6] |= UINT64_C(1) << ((next - 1u) & 63u);
        if (last) ((tide_block *)(void *)(data + ((size_t)(last - 1u) << shift)))->link = tide_block_link(c | FREE_MARK, next);
        last = next;
    }
    if (last) ((tide_block *)(void *)(data + ((size_t)(last - 1u) << shift)))->link = tide_block_link(c | FREE_MARK, 0);
    for (uint32_t i = 0; i < pl.made;) {
        if (is_set(freed, i)) {
            i++;
            continue;
        }
        uint32_t end = i + 1u;
        while (end < pl.made && !is_set(freed, end)) end++;
        const uint8_t *bytes = tide_read_bytes(r, (end - i) << shift);
        if (!bytes) return false;
        memcpy(data + ((size_t)i << shift), bytes, (size_t)(end - i) << shift);
        i = end;
    }
    return true;
}

// `pages` places of an empty heap, what's at them read already: false, and
// nothing kept of them, where they aren't a heap's.
static bool take_places_read(tide_heap *h, const uint32_t pages, const uint64_t pending)
{
    uint64_t taken = 0;
    if (!places_hold(h->place, pages, &taken)) {
        if (pages) memset(h->place, 0, pages * sizeof *h->place);
        return false;
    }
    h->pages = pages;
    h->pending = pending;
    h->taken = taken;
    h->most = taken;
    return true;
}

bool tide_heap_unpack(tide_heap *h, tide_reader *r)
{
    if (!tide_asset_unpack_heap(h, r, NULL)) { r->failed = true; return false; }
    const uint64_t pending = tide_read_u64(r);
    const uint32_t pages = tide_read_u32(r);
    if (r->failed || (uint64_t)pages * sizeof *h->place > r->size - r->at) {
        r->failed = true;
        return false;
    }
    if (pages) {
        make_room(h, pages);
        memcpy(h->place, tide_read_bytes(r, pages * (uint32_t)sizeof *h->place), pages * sizeof *h->place);
    }
    if (!take_places_read(h, pages, pending)) r->failed = true;
    for (uint32_t i = 1; !r->failed && i < h->pages; i += span(h, i)) {
        if (h->place[i].kind == TIDE_PLACE_FREE) continue;
        const uint32_t count = span(h, i);
        // What it says of its page is there to read, at the least (a free block is two bytes of it)
        const uint64_t least = packed_page_size(h, i);
        if (r->size - r->at < least) {
            r->failed = true;
            break;
        }
        tide_page *p = tide_page_new(count << TIDE_HEAP_PAGE_SHIFT);
        p->first = i;
        p->refs = count;
        put_pages(h, i, count, p);
        if (!unpack_page(h->place[i], p, r)) r->failed = true;
    }
    if (!r->failed && !chains_hold(h)) r->failed = true;
    if (!r->failed) index_places(h);
    tide_heap_settle(h);
    if (!r->failed && !tide_asset_validate_heap(h)) r->failed = true;
    return !r->failed;
}

// Packed as part of a delta: its places and blocks waiting to be freed as
// numbers, what's at each place as a region, then its pages as regions.

void tide_heap_pack_delta(const tide_heap *h, const tide_heap *base, tide_delta_writer *d)
{
    tide_asset_pack_heap(h, &d->bytes, d->assets, d->xor ? base : NULL);
    tide_delta_number(d, h->pages);
    tide_delta_number64(d, h->pending);
    const uint32_t had = base ? base->pages : 0u;
    if (h->pages) {
        tide_delta_region(d, h->place, h->pages * (uint32_t)sizeof *h->place, had ? base->place : NULL,
                          had * (uint32_t)sizeof *h->place, false, sizeof *h->place);
    }
    for (uint32_t i = 1; i < h->pages; i += span(h, i)) {
        const tide_page *p = h->page[i];
        if (!p) continue;
        const tide_page *b = page_at(base, i);
        tide_delta_asset_region(d, tide_page_data(p), in_use(h, p), b ? tide_page_data(b) : NULL, b ? in_use(base, b) : 0u,
                                b == p, tide_asset_origin_page(h, i));
    }
    tide_delta_close(d);
}

bool tide_heap_unpack_delta(tide_heap *h, const tide_heap *base, tide_delta_reader *d)
{
    if (!tide_asset_unpack_heap(h, &d->bytes, d->xor ? base : NULL)) return false;
    const uint32_t pages = tide_delta_get_number(d);
    const uint64_t pending = tide_delta_get_number64(d);
    if (d->bytes.failed) return false;
    // What's at its places is a region, which can be all zeros in a few bytes: past the base's, a byte of the
    // delta for every 1,024 places at least, so that bytes saying any number can't take more memory than that
    const uint32_t had = base ? base->pages : 0u;
    if (pages > had && (uint64_t)(pages - had) > ((uint64_t)(d->bytes.size - d->bytes.at) + 1u) * 1024u) return false;
    if (pages) {
        make_room(h, pages);
        if (!tide_delta_get(d, h->place, pages * (uint32_t)sizeof *h->place, had ? base->place : NULL,
                            had * (uint32_t)sizeof *h->place)) {
            memset(h->place, 0, pages * sizeof *h->place);
            return false;
        }
    }
    if (!take_places_read(h, pages, pending)) return false;
    for (uint32_t i = 1; i < h->pages; i += span(h, i)) {
        if (h->place[i].kind == TIDE_PLACE_FREE) continue;
        const uint32_t count = span(h, i), size = count << TIDE_HEAP_PAGE_SHIFT;
        tide_page *b = page_at(base, i); // Shared where it's the same
        const tide_asset_origin *previous = base ? tide_asset_origin_page(base, i) : NULL;
        tide_asset_origin origin = previous ? *previous : (tide_asset_origin){0};
        tide_page *p = tide_delta_asset_page(d, used_at(h, i), size, count, b, b ? in_use(base, b) : 0u,
                                             b && b->size == size, &origin);
        if (!p) {
            tide_heap_settle(h);
            return false;
        }
        if (origin.data) tide_asset_origin_set(h, i, origin);
        if (p != b) p->first = i;
        put_pages(h, i, count, p);
    }
    const bool held = chains_hold(h);
    if (held) index_places(h);
    tide_heap_settle(h);
    return held && tide_delta_closed(d) && tide_asset_validate_heap(h);
}

// Its regions' hashes, as a delta has them: what's at its places, then each
// page with how far its place is past the one before.

void tide_heap_hash_pages(const tide_heap *h, tide_writer *w)
{
    uint32_t count = 0;
    for (uint32_t i = 1; i < h->pages; i += span(h, i)) count += h->page[i] != NULL;
    tide_write_varint(w, h->pages ? count + 1u : 0u);
    if (!h->pages) return;
    tide_write_u64(w, tide_hash(h->place, h->pages * sizeof *h->place));
    uint32_t at = 0;
    for (uint32_t i = 1; i < h->pages; i += span(h, i)) {
        tide_page *p = h->page[i];
        if (!p) continue;
        tide_write_varint(w, i - at);
        tide_write_u64(w, tide_page_hash(p, in_use(h, p)));
        at = i;
    }
}

void tide_heap_need_pages(const tide_heap *base, tide_needs *n)
{
    const uint32_t count = tide_needs_count(n);
    if (!count) return;
    const bool any = base && base->pages;
    tide_needs_put(n, any, any ? tide_hash(base->place, base->pages * sizeof *base->place) : 0u);
    uint64_t at = 0; // The place their page starts at
    for (uint32_t i = 1; i < count; i++) {
        const uint32_t gap = tide_read_varint(&n->list);
        if (!gap) n->list.failed = true;
        at += gap;
        tide_page *b = !n->list.failed && at < UINT32_MAX ? page_at(base, (uint32_t)at) : NULL;
        tide_needs_put(n, b != NULL, b ? tide_page_hash(b, in_use(base, b)) : 0u);
    }
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

// It holds nothing: it's as a heap that isn't there.
static bool blank(const tide_heap *h)
{
    return !h || h->pages == 0;
}

// Whether a heap is its base's as it stands: what's at each place, and every
// page the same page.
static bool same_heap(const tide_heap *h, const tide_heap *base)
{
    if (blank(h) || blank(base)) return blank(h) && blank(base);
    if (h->pending != base->pending || h->pages != base->pages || h->assets != base->assets
        || memcmp(h->place, base->place, h->pages * sizeof *h->place) != 0) {
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
        if (heap) used += heap->most;
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
        for (uint32_t i = 0; heap && i < heap->pages; i++) pages += heap->page[i] != NULL;
    }
    return pages;
}

uint32_t tide_heaps_gone(const tide_heaps *hs)
{
    uint32_t gone = 0;
    for (uint32_t number = 0; number < NUMBERS; number = after(hs, NULL, number)) {
        const tide_heap *heap = numbered(hs, number);
        for (uint32_t i = 1; heap && i < heap->pages; i++) gone += heap->place[i].kind == TIDE_PLACE_FREE;
    }
    return gone;
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
