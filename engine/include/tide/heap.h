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
// Deterministic: blocks come from size classes, reused in a fixed order, so
// the same allocations and releases give the same offsets on every machine.
// A released block is only reused once the code running now is done (see
// tide_heap_flush), so text read before the release stays readable until then.
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

// Bytes in each page: 1 << this. A block never crosses from one page into
// the next: a bigger block has a page of its own, as big as it needs.
#define TIDE_HEAP_PAGE_SHIFT 14u

// Every block starts with its header: what it holds after it.
typedef struct tide_block {
    // Its size class in the low 8 bits, and above them the next block, in a
    // free list or the pending list (tide_block_class, tide_block_next)
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

typedef struct tide_heap {
    uint64_t used;                    // Bytes handed out from the start, headers included
    uint64_t free[TIDE_HEAP_CLASSES]; // Released blocks of each class, most recent first
    uint64_t pending;                 // Blocks released while code runs, freed when it's done
    uint32_t pages;                   // Pages as far as `used` goes
    uint32_t room;                    // Pages `page` has room for
    // Counts the times a block may have moved or gone: a page copied to be
    // changed, or blocks released; and the times a page's hash was asked for
    // (tide_heap_block_hash), after which a change has to say so again. Code
    // that keeps a block's address a while (a grid's chunk cache, see
    // tide/grid.h) checks it didn't change. It's no state of its own either,
    // and read atomically, as other threads may read it while one system
    // changes the heap.
    uint32_t moves;
    // The page each page-sized piece of the heap is in. A page bigger than
    // that is at each of its places, with a reference for each. Before the
    // first, page[-1] holds the table this one replaced, until it's settled.
    tide_page **page;
    // Immutable asset payloads named by content IDs in list blocks. These
    // pools are shared by snapshots. Logical descriptor leases are checked
    // by the heap hash; native pointers and optional origins are not.
    struct tide_asset_pool *assets;
    struct tide_asset_retired *assets_retired;
    struct tide_asset_origins *origins; // Optional serialization hints, shared by snapshots.
} tide_heap;

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
// what it's handed out, how many places each page takes, then its pages as
// regions, each paired with the base's page that starts at the same place.
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
    if (h->pending) tide_heap_flush_pending(h);
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

static inline tide_block *tide_heap_write(tide_heap *h, const uint64_t block)
{
    tide_page *p = h->page[block >> TIDE_HEAP_PAGE_SHIFT];
    // A page bigger than one place has a reference for each (see tide_heap)
    const uint32_t places = p->size > (1u << TIDE_HEAP_PAGE_SHIFT) ? p->size >> TIDE_HEAP_PAGE_SHIFT : 1u;
    if (p->refs != places) return tide_heap_write_shared(h, block);
    p->hashed = UINT32_MAX; // What it hashed to is out of date
    return (tide_block *)(uintptr_t)((uint8_t *)tide_page_data(p) + (size_t)(block - ((uint64_t)p->first << TIDE_HEAP_PAGE_SHIFT)));
}

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

// The heap with that id.
static inline tide_heap *tide_heaps_of(tide_heaps *hs, const uint32_t id)
{
    tide_heap *all = __atomic_load_n(&hs->all, __ATOMIC_ACQUIRE);
    if (!all) all = tide_heaps_make(hs);
    return all + tide_heap_slots[id & (TIDE_HEAP_IDS - 1u)];
}

// What tide_heap's own do, for all of a world's: only while nothing else
// runs on it. A packed world's heaps say their ids, so one from another build
// of the game is read into the heaps of the same owners.
void tide_heaps_copy(tide_heaps *to, const tide_heaps *from);
void tide_heaps_free(tide_heaps *hs);
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

// What its heaps have handed out between them, and whether any has blocks
// waiting to be freed: for code that keeps something worked out from a world's
// text and lists while neither changes.
uint64_t tide_heaps_used(const tide_heaps *hs);
bool tide_heaps_pending(const tide_heaps *hs);

// How many pages its heaps have between them, and how many of them aren't
// `other`'s pages in the same places: what a world and its snapshot don't
// share.
uint32_t tide_heaps_pages(const tide_heaps *hs);
uint32_t tide_heaps_pages_apart(const tide_heaps *hs, const tide_heaps *other);

// The heaps the code this thread runs may change, for builds that check
// (TIDE_HEAP_CHECKS): a system's are its owners', which is all the schedule
// keeps other systems off. Generated code says which of `match`'s, by their
// ids, around each system's task, with the system's name as `who`, and any
// again after it (`who` NULL); a block handed out, taken back or made a heap's
// own in another of `match`'s heaps meanwhile ends the program, naming the
// system and the heap's id. Builds that don't check do nothing here.
void tide_heaps_allow(const tide_heaps *match, const uint16_t *ids, uint32_t count, const char *who);
