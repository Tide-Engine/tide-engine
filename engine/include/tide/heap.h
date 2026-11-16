#pragma once

#include <stdint.h>

#include "tide/delta.h"
#include "tide/net.h"
#include "tide/page.h"

// Memory a world keeps variable-length data in, such as the text of its
// components' string fields. It's part of the world, in pages its snapshots
// share (see tide/page.h), and it grows as it needs, in blocks of 128 MiB at
// most (the biggest size class). Its offsets are 64 bits, so it holds what
// memory does: past that, or past a block's size, tide_heap_alloc ends the
// program, as running out of memory does (tide_out_of_memory).
//
// Deterministic: a block's place is worked out from what the heap holds (see
// Places, below), which is the world's state, so the same allocations and
// releases give the same offsets on every machine, and so do the ones after
// a heap was read from bytes. A released block is only reused once the code
// running now is done (see tide_heap_flush), so text read before the release
// stays readable until then.
//
// One system at a time changes a heap, while others may read it on other
// threads: the heap's page table is read as a whole, and when it grows, the
// old one stays until tide_heap_settle, once only one thread runs.
//
// A world has several (tide_heaps, below): one for each component, singleton
// and event whose fields hold text, lists, maps, sets or grids, and one for
// the rest. So two systems only wait for each other over a heap when they
// change the same owner's.

#define TIDE_HEAP_CLASSES 24 // Block sizes 16 << class bytes

// A heap's runs (A heap in runs, below): how many, as bits of a handle.
#define TIDE_HEAP_RUN_BITS 5u
#define TIDE_HEAP_RUNS (1u << TIDE_HEAP_RUN_BITS)

// Bytes in each page: 1 << this. A block never crosses from one page into
// the next: a bigger block has a page of its own, as big as it needs.
#define TIDE_HEAP_PAGE_SHIFT 14u

// Every block starts with its header: what it holds after it.
typedef struct tide_block {
    // Its size class in the low 8 bits, and above them the next block, in the
    // pending list or (with 0x80 beside the class) its page's free blocks
    // (tide_block_class, tide_block_next)
    uint64_t link;
    uint32_t a; // Text: its bytes
    uint32_t b; // Text: its characters
} tide_block;

static inline uint32_t tide_block_class(const tide_block *b)
{
    return (uint32_t)(b->link & 255u);
}
static inline uint64_t tide_block_next(const tide_block *b)
{
    return b->link >> 8;
}
static inline uint64_t tide_block_link(const uint32_t size_class, const uint64_t next)
{
    return next << 8 | size_class;
}

// ---------------------------------------------------------------------------
// Places
//
// A heap is places of a page each, numbered from 1 (place 0 is never used:
// offset 0 means no block). A place is free, or has a page of blocks of one
// class (the classes below TIDE_HEAP_SMALL, at most half a page each), or is
// one of the places of a block's own page (a block of a page or more). A new
// block goes in the lowest place that has room for one of its class: the
// lowest page of its class with a block to spare, in which blocks never yet
// handed out go in order and freed ones last freed first, else the lowest free
// place, else a new place at the end; a block of several places takes the
// lowest run of free places that holds it. A page whose last block is freed
// makes its place free again, for any class, and free places at the end are
// no longer the heap's; a page whose last-made block is freed is in use only
// as far as the blocks before it. So what's handed out stays together at the
// heap's start, and a heap that held much and holds little is small again: in
// memory (a free place has no page), in its hash and in its bytes, which have
// a free block as its number alone.
//
// tide_heap_place is what's at each place, and with `pages` and `pending` it's
// all of the heap's state outside its pages: hashed, packed and sent as it is.

#define TIDE_HEAP_SMALL (TIDE_HEAP_PAGE_SHIFT - 4u) // The classes that share pages
#define TIDE_PLACE_FREE 0u                          // 1 + class: a page of that class's blocks
#define TIDE_PLACE_BLOCK 15u                        // The first place of a block's own page
#define TIDE_PLACE_MORE 16u                         // ...and each of its others
#define TIDE_HEAP_ROWS (TIDE_HEAP_SMALL + 1u)

typedef struct tide_heap_place {
    uint8_t kind;
    uint8_t shift; // A block's own page: 1 << this places
    uint16_t made; // A class's page: the blocks of it made from its start, in order, as far as one is still in use
    uint16_t free; // ...the last of them freed, as 1 + its number in the page (each links the one before); 0: none
    uint16_t out;  // ...and how many of them are handed out
} tide_heap_place;
// Hashed and packed as its bytes: no padding
_Static_assert(sizeof(tide_heap_place) == 8, "a place is 8 bytes");

typedef struct tide_heap {
    uint64_t pending;                 // Blocks released while code runs, freed when it's done
    // A heap in runs (below), beside `pending` as a flush looks at all three: the blocks of other runs' heaps
    // this run's rows released while runs are on threads, until its row is done (tide_heap_flush); and, an
    // owner's first heap's, which of its runs' heaps have blocks released outside runs, a bit each. Neither is
    // part of the world's state: `away` is empty whenever nothing runs
    uint64_t away;
    uint32_t runs_pending;
    uint32_t pages;                   // Places as far as it goes, place 0 counted (the last is never free); 0: none
    uint32_t room;                    // Places `page` and `place` have room for
    tide_heap_place *place;           // What's at each place (Places, above)
    // Worked out from `place`, and no state of its own. For each 64 places TIDE_HEAP_ROWS words, a bit a place:
    // the free places, then for each class the pages with a block to spare; `low` is the lowest of each (0:
    // none). `taken`: the bytes of the blocks handed out, and `most` the most that ever was (tide_heaps_used).
    uint64_t *index;
    uint32_t low[TIDE_HEAP_ROWS];
    uint64_t taken;
    uint64_t most;
    // The pages of places freed while code may still be reading the table (another thread can hold a page it
    // read a moment ago): handed out again, zeroed, for the next places taken, or let go of when it's settled
    tide_page **dropped;
    uint32_t dropped_count;
    uint32_t dropped_room;
    // Counts the times a block may have moved or gone: a page copied to be
    // changed, or blocks released; and the times a page's hash was asked for
    // (tide_heap_block_hash), after which a change has to say so again. Code
    // that keeps a block's address a while (a grid's chunk cache, see
    // tide/grid.h) checks it didn't change. It's no state of its own either,
    // and read atomically, as other threads may read it while one system
    // changes the heap.
    uint32_t moves;
    // The page at each place: NULL at a free one and at place 0. A page bigger
    // than a place is at each of its places, with a reference for each. Before
    // the first, page[-1] holds the table this one replaced, until it's settled.
    tide_page **page;
    // Immutable asset payloads named by content IDs in list blocks. These
    // pools are shared by snapshots. Logical descriptor leases are checked
    // by the heap hash; native pointers and optional origins are not.
    struct tide_asset_pool *assets;
    struct tide_asset_retired *assets_retired;
    struct tide_asset_origins *origins; // Optional serialization hints, shared by snapshots.
    // A heap in runs (below). An owner's first heap has the others, made when one is first asked for; each of
    // those says whose it is and which run's.
    struct tide_heap *runs;
    struct tide_heap *base;
    uint32_t run;
    uint32_t lock;         // Held to change its page table while runs are on threads
    // While runs are on threads, of this run's heap: the blocks of other runs' heaps its rows released, zeroed
    // once their row was done, until the runs join (tide_heap_runs_join); the pages it replaced with copies,
    // which keep their references until then; and the asset lists it let go of in other runs' heaps. All empty
    // whenever nothing runs: no part of the world's state.
    uint64_t held;
    tide_page **retired;
    uint32_t retired_count;
    uint32_t retired_room;
    struct tide_heap_later *later;
    uint32_t later_count;
    uint32_t later_room;
} tide_heap;

// The bytes of a page of `h` that are in use, which its hash covers and a
// packed heap has: a class's page as far as its blocks were handed out, a
// block's own page whole. The rest of a page is zeros.
static inline uint32_t tide_heap_page_used(const tide_heap *h, const tide_page *p)
{
    const tide_heap_place at = h->place[p->first];
    return at.kind - 1u < TIDE_HEAP_SMALL ? (uint32_t)at.made << (at.kind + 3u) : p->size;
}

// A block with room for `bytes` bytes after its header. Offsets are never 0.
// A zeroed heap is empty.
uint64_t tide_heap_alloc(tide_heap *h, uint32_t bytes);

// For snapshots: `to` becomes `from`, sharing its pages. The hash covers
// what's been handed out.
void tide_heap_copy(tide_heap *to, const tide_heap *from);
uint64_t tide_heap_hash(uint64_t h, const tide_heap *heap);
// The pages that hash would have to read again (tide/page.h), for whoever hashes them on threads first.
void tide_heap_stale(const tide_heap *heap, tide_stale_pages *s);

// Lets go of its pages, leaving it empty.
void tide_heap_free(tide_heap *h);

// The hash of the page `block` is in, as tide_heap_hash takes it: kept until
// the page changes, so asking again costs nothing while it hasn't. Code that
// keeps a block's address to write through (a grid's chunk cache) wouldn't
// say the page changed again, so this counts as a move: it looks its block
// up once more.
uint64_t tide_heap_block_hash(tide_heap *h, uint64_t block);

// The heap as bytes, for sending a world and carrying it over, and reading
// it back into an empty heap (false when the bytes aren't one).
uint32_t tide_heap_packed_size(const tide_heap *h);
void tide_heap_pack(const tide_heap *h, tide_writer *w);
bool tide_heap_unpack(tide_heap *h, tide_reader *r);

// The same as part of a delta (tide/delta.h), from `base` (NULL for none):
// its places and its pending blocks, what's at each place as a region, then
// its pages as regions, each paired with the base's page that starts at the
// same place.
// Unpacking `h` (empty) shares the pages that are the same as the base's.
void tide_heap_pack_delta(const tide_heap *h, const tide_heap *base, tide_delta_writer *d);
bool tide_heap_unpack_delta(tide_heap *h, const tide_heap *base, tide_delta_reader *d);
// Its pages' places and hashes, and which of them `base` lacks (tide_needs).
void tide_heap_hash_pages(const tide_heap *h, tide_writer *w);
void tide_heap_need_pages(const tide_heap *base, tide_needs *n);

// Gives a block back. It's freed at the next tide_heap_flush.
void tide_heap_release(tide_heap *h, uint64_t block);

// Frees the released blocks, zeroed, for reuse. Generated code calls it once
// the code that released them is done.
// Inline for its common case, as generated code flushes after every row of a
// system that makes text or lists: nothing released, nothing to do.
void tide_heap_flush_pending(tide_heap *h);
static inline void tide_heap_flush(tide_heap *h)
{
    if (h->pending | h->away | h->runs_pending) tide_heap_flush_pending(h);
}

// Frees the page tables the heap grew out of, which code on other threads may
// still have been reading. Generated code calls it once systems are done.
void tide_heap_settle(tide_heap *h);

// A block, to read. Its page is read as one, since the system changing the
// heap may be growing it or copying a page meanwhile.
static inline tide_block *tide_heap_block(const tide_heap *h, const uint64_t block)
{
    tide_page *const *pages = __atomic_load_n(&h->page, __ATOMIC_ACQUIRE);
    const tide_page *p = __atomic_load_n(&pages[block >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
    return (tide_block *)(uintptr_t)((const uint8_t *)tide_page_data(p) + (size_t)(block - ((uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT)));
}

// A block, to change: its page made this heap's own. Pages this heap owns
// already are changed in place, and the rest are copied first
// (tide_heap_write_shared).
tide_block *tide_heap_write_shared(tide_heap *h, uint64_t block);

// A block whose first `bytes` after its header are about to be written over,
// all of them: as tide_heap_write, but a page that's copied first is copied
// without them, as a big list's chunk would otherwise be copied only to be
// replaced. Until they're written they're whatever was in memory, so only for
// a block nothing else reads meanwhile.
tide_block *tide_heap_write_over(tide_heap *h, uint64_t block, uint32_t bytes);

// A block of page `p`, the one `block` is in (an offset anywhere in it), is
// about to be written: what it hashed to is out of date, and the lines its
// block is in say so (tide/page.h, Lines written). A page of blocks of one
// size knows their size (`grain`, set as its place takes it, the same in its
// copies); any other page is one block. Code that writes a block always says
// so first, here (tide_heap_write and its kin): never through a block it was
// handed for another.
static inline void tide_heap_wrote(tide_page *p, const uint64_t block)
{
    const uint32_t grain = p->grain;
    if (!grain) {
        tide_page_dirty(p);
        return;
    }
    const uint32_t in = (uint32_t)(block - ((uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT)) & ~((1u << grain) - 1u);
    tide_page_wrote(p, in, 1u << grain);
}

static inline tide_block *tide_heap_write(tide_heap *h, const uint64_t block)
{
    // Read as tide_heap_block reads them: while a system's runs are on threads, another run's thread may be
    // giving the heap a page, or a page a copy of its own
    tide_page *const *pages = __atomic_load_n(&h->page, __ATOMIC_ACQUIRE);
    tide_page *p = __atomic_load_n(&pages[block >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
    // A page bigger than one place has a reference for each (see tide_heap)
    const uint32_t places = p->size > (1u << TIDE_HEAP_PAGE_SHIFT) ? p->size >> TIDE_HEAP_PAGE_SHIFT : 1u;
    if (tide_page_refs(p) != places) return tide_heap_write_shared(h, block);
    tide_heap_wrote(p, block); // What it hashed to is out of date, where
    return (tide_block *)(uintptr_t)((uint8_t *)tide_page_data(p) + (size_t)(block - ((uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT)));
}

// ---------------------------------------------------------------------------
// A heap in runs
//
// A per-entity system that changes its rows' text, lists and grids goes
// through its entities in runs, which threads take (tidec's
// system_splits_in_order): a fixed share of their order each, or, where its
// rows queue nothing, each entity's own run, from its identity, so an entity
// is in the same run in every such system and tick and the newest aren't all
// in the last (tidec's system_heap_runs_scattered). Blocks handed out in the
// order threads got to them would be at other offsets on every machine, so
// each run has a heap of its own within its owner's: run 0 the owner's first,
// where everything else that runs allocates too. A block says which run's heap it
// is in, TIDE_HEAP_RUN_BITS of its handle and of a `where` (tide/text.h), so
// the offsets a run hands out depend on that run's rows alone, in their order,
// whichever thread has it and whatever the others do. The runs are the same
// on one thread: a world is the same bytes however many threads made it.
//
// While its runs are on threads (between tide_heap_run_enter and
// tide_heap_run_leave on each thread, until tide_heap_runs_join):
// - a run's thread hands out and takes back blocks of its own run's heap
//   alone;
// - a block of another run's heap that a row lets go of (its entity was in
//   that run when the block was made) is kept, zeroed once the row is done,
//   and goes back to its own heap when the runs join, in the runs' order;
// - any thread may write a block of any run's heap in place, as a row's
//   list can be anywhere: a page a snapshot shares is replaced with a copy by
//   whichever thread gets there first, and keeps its references until the
//   join, so that no thread still holding it takes it for the heap's own.
//
// Everything a value keeps in several blocks (a list in chunks, a grid) is in
// one run's heap, as its blocks name each other by offset: list.c and grid.c
// move such a value whole into the heap of the run that adds to it.

// The run this thread's code is on, as the bits of a `where`: 0 outside runs,
// which is run 0's too.
extern TIDE_THREAD_LOCAL uint32_t tide_heap_run_where;

// Where a `where` and a handle say the run (tide/text.h has the rest).
#define TIDE_WHERE_RUN_SHIFT 14u
#define TIDE_HANDLE_RUN_SHIFT 54u

// This thread is on run `run` of a system's entities from here on, and what it
// was on before, to hand to tide_heap_run_leave: generated code calls them
// around each run's rows.
uint32_t tide_heap_run_enter(uint32_t run);
void tide_heap_run_leave(uint32_t was);

// Once every run of a system is done, for each owner it changes (its first
// heap): the blocks the runs kept go back to their heaps, and the pages they
// replaced lose their references.
void tide_heap_runs_join(tide_heap *base);

// Whether this thread is on a run that isn't `h`'s, so that a block of `h`
// it lets go of is kept until the join; and an asset list's lease in such a
// heap to give up then (tide_asset_drop), not now, as that heap's leases are
// its own run's to change meanwhile.
bool tide_heap_away(const tide_heap *h);
void tide_heap_drop_later(tide_heap *h, const uint8_t id[32], uint64_t block);

// An owner's heap for a run past the first, made if nothing has yet. Not
// inline: every lookup of a heap has the call in it, and few take it.
tide_heap *tide_heap_run_of(tide_heap *base, uint32_t run);

// ---------------------------------------------------------------------------
// Free places
//
// Nothing reads a free place, which has no page: only a handle to a block
// that's handed out is read, and a place is free once it has none. Code that
// looks a page up by an offset that could be anything (one kept from before,
// whose block may have gone since) asks first:

// Whether `bytes` bytes from `offset` are in one page the heap has now.
static inline bool tide_heap_has(const tide_heap *h, const uint64_t offset, const uint64_t bytes)
{
    if (!offset || offset >> TIDE_HEAP_PAGE_SHIFT >= h->pages) return false;
    tide_page *const *pages = __atomic_load_n(&h->page, __ATOMIC_ACQUIRE);
    const tide_page *p = __atomic_load_n(&pages[offset >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
    if (!p) return false;
    const uint64_t in = offset - ((uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT);
    return bytes <= p->size && in <= p->size - bytes;
}

// Whether `block` is a block of class `size_class` that's handed out: where one
// starts in a page of that class's, and not free (it may be released, waiting
// to be freed). For checking what came as bytes; it goes through the page's
// free blocks.
bool tide_heap_handed_out(const tide_heap *h, uint64_t block, uint32_t size_class);

// ---------------------------------------------------------------------------
// A world's heaps
//
// What a component's, a singleton's or an event's fields hold is in that
// owner's heap, and nothing else is: a system that changes one owner's text
// or lists hands out and takes back that heap's blocks alone, in its own
// order, whatever other systems do to the other heaps meanwhile. Which heap a
// block is in is its owner's id, 12 bits of every handle and `where`
// (tide/text.h): a number tidec makes from the owner's name, so it's the same
// in every build of a game and a world carried over to new code (hot reload)
// keeps its handles. Id 0 is the world's own heap: its tasks', and every
// owner's that C can write, as C says TIDE_IN_MATCH for all of them. Two
// owners whose names make the same id share a heap.
//
// A game says which ids it has once, before any world is made
// (tide_heaps_shape, from a constructor in its generated C). Without one,
// every id is heap 0: a world of one heap, as the engine's own tests and a
// worker's private heaps are.

#define TIDE_HEAP_ID_BITS 12u
#define TIDE_HEAP_IDS (1u << TIDE_HEAP_ID_BITS)

typedef struct tide_heaps {
    // Its heaps, tide_heap_count of them, by slot: made, all empty, the first
    // time one is asked for, and read atomically, as two systems can ask at
    // once. NULL is every heap empty, so a zeroed world is an empty one.
    tide_heap *all;
} tide_heaps;

// By id, which of a world's heaps (its slot, 0 for the first); by slot, its
// id; and how many slots.
extern const uint16_t *tide_heap_slots;
extern const uint16_t *tide_heap_ids;
extern uint32_t tide_heap_count;

// `slots` has TIDE_HEAP_IDS entries, below `count` each, and `ids` has
// `count`, the first 0; both stay the program's. NULL `slots` is one heap
// again. Only while no world with more heaps is left: a world's other heaps
// are as many as the count was when they were made.
void tide_heaps_shape(const uint16_t *slots, const uint16_t *ids, uint32_t count);

// Makes `hs`'s heaps, if nothing has yet.
tide_heap *tide_heaps_make(tide_heaps *hs);

// The heap with that id: with a run above the id's bits, as a `where` and a
// handle have it, that run's.
static inline tide_heap *tide_heaps_of(tide_heaps *hs, const uint32_t id)
{
    tide_heap *all = __atomic_load_n(&hs->all, __ATOMIC_ACQUIRE);
    if (!all) all = tide_heaps_make(hs);
    tide_heap *h = all + tide_heap_slots[id & (TIDE_HEAP_IDS - 1u)];
    if (__builtin_expect(id >> TIDE_HEAP_ID_BITS != 0, 0)) return tide_heap_run_of(h, id >> TIDE_HEAP_ID_BITS);
    return h;
}

// What tide_heap's own do, for all of a world's: only while nothing else
// runs on it. A packed world's heaps say their ids, so one from another build
// of the game is read into the heaps of the same owners.
void tide_heaps_copy(tide_heaps *to, const tide_heaps *from);
void tide_heaps_free(tide_heaps *hs);
// One owner's heap of `from` as it is now, its runs too, into the same place
// of `to` (another set, of nothing else): a copy that shares its pages, as
// tide_heaps_copy makes of all of them. For reading that owner's blocks as
// they were while `from` goes on changing; tide_heaps_free lets go.
void tide_heaps_keep(tide_heaps *to, const tide_heaps *from, uint32_t id);
void tide_heaps_flush(tide_heaps *hs);
void tide_heaps_settle(tide_heaps *hs);
uint64_t tide_heaps_hash(uint64_t h, const tide_heaps *hs);
void tide_heaps_stale(const tide_heaps *hs, tide_stale_pages *s);
uint32_t tide_heaps_packed_size(const tide_heaps *hs);
void tide_heaps_pack(const tide_heaps *hs, tide_writer *w);
bool tide_heaps_unpack(tide_heaps *hs, tide_reader *r);
void tide_heaps_pack_delta(const tide_heaps *hs, const tide_heaps *base, tide_delta_writer *d);
bool tide_heaps_unpack_delta(tide_heaps *hs, const tide_heaps *base, tide_delta_reader *d);
void tide_heaps_hash_pages(const tide_heaps *hs, tide_writer *w);
void tide_heaps_need_pages(const tide_heaps *base, tide_needs *n);

// The most its heaps' blocks have taken at once, each heap's by itself, in
// bytes (it only grows, and not for a block that takes the room of ones
// freed), and whether any has blocks waiting to be freed: for code that keeps
// something worked out from a world's text and lists while neither changes,
// and for tests that nothing was left behind.
uint64_t tide_heaps_used(const tide_heaps *hs);
bool tide_heaps_pending(const tide_heaps *hs);

// How many places of its heaps have a page between them (tide_heaps_gone: how
// many are free, with places in use past them), and how many of its places
// aren't `other`'s pages in the same places: what a world and its snapshot
// don't share.
uint32_t tide_heaps_pages(const tide_heaps *hs);
uint32_t tide_heaps_gone(const tide_heaps *hs);
uint32_t tide_heaps_pages_apart(const tide_heaps *hs, const tide_heaps *other);

// The heaps the code this thread runs may change, for builds that check
// (TIDE_HEAP_CHECKS): a system's are its owners', which is all the schedule
// keeps other systems off. Generated code says which of `match`'s, by their
// ids, around each system's task, with the system's name as `who`, and any
// again after it (`who` NULL); a block handed out, taken back or made a heap's
// own in another of `match`'s heaps meanwhile ends the program, naming the
// system and the heap's id. Builds that don't check do nothing here.
void tide_heaps_allow(const tide_heaps *match, const uint16_t *ids, uint32_t count, const char *who);
