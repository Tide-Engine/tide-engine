#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/heap.h"
#include "tide/text.h"

// Tide's `List<T>`: a list of values, as a value. Generated code calls the
// functions below with each element's size, and handles the text in elements
// itself (see tide/text.h).
//
// A tide_list is where its elements are: a block in a world's heap, or in the
// scratch area. As with text, generated code says where the list is with
// `where` (see tide/text.h): a list that's part of a world (a component's
// field, a singleton's, the command queue's) has its own block in that
// world's heap, and any other list has one in the scratch area. A list is
// a value: taking one out of a field or variable copies it logically.
// Immutable asset payloads may stay shared until tide_list_prepare before
// writable element access. Other lists retain their ordinary copying path.
//
// Nothing fails: past the end, reads find nothing and writes do nothing. A
// world's lists grow with its heap, and a list in the scratch area with the
// area, which adds pieces as it needs (see tide/text.h); only running out of
// memory ends the program.
//
// A list's block starts with a header: the count in `a`, and the capacity in
// `b`. The elements follow it, side by side, unless the list is a world's and
// they take more than a page: then they're in chunks, heap blocks of a page
// each, and the list's block lists where each chunk is, `b` saying how many
// there are (with TIDE_LIST_CHUNKED). A world and its snapshots share pages
// until one of them changes one (see tide/page.h), and hashes cover each page
// on its own, so changing an element of a big list copies and hashes its
// chunk, not the whole list. Growing adds chunks, without moving elements.

typedef struct tide_list {
    uint32_t at; // 0: empty. Otherwise its block's offset, and where in the top two bits
} tide_list;

// Bytes of elements in a chunk: a heap block of a page, less its header.
#define TIDE_LIST_CHUNK ((1u << TIDE_HEAP_PAGE_SHIFT) - (uint32_t)sizeof(tide_block))
// In a list's header's `b`: the elements are in chunks, as many as the rest says.
#define TIDE_LIST_CHUNKED 0x80000000u
// Immutable asset descriptor: count in a, SHA-256 after the block header.
#define TIDE_LIST_ASSET 0x40000000u
const void *tide_list_asset_at(tide_list l, int32_t i, uint32_t size);
// Before writable access, make borrowed asset storage this value's own. Most
// lists are a world's own or the scratch area's already, which is told here,
// inline, from the list's block: only an asset's or the pack's goes further
// (tide_list_prepare_borrowed).
void tide_list_prepare_borrowed(tide_list *l, uint32_t size, uint32_t where);
static inline void tide_list_prepare(tide_list *l, const uint32_t size, const uint32_t where)
{
    if (!l || !l->at) return;
    if (l->at >> 30 == TIDE_IN_SCRATCH) {
        if (!tide_pack_holds(l->at)) return;
    } else {
        const tide_block *b = tide_block_at(l->at);
        if (!b || b->b != TIDE_LIST_ASSET) return;
    }
    tide_list_prepare_borrowed(l, size, where);
}

// Reading and writing elements is inline, as code does it for every element
// it touches.

static inline int32_t tide_list_count(const tide_list l)
{
    const tide_block *b = tide_block_at(l.at);
    return b ? (int32_t)b->a : 0;
}

// Element `i` of a list in chunks: its chunk (a heap offset), and where in
// its chunk it is.
static inline uint32_t tide_list_chunk(const tide_block *b, const uint32_t i, const uint32_t size, uint32_t *place)
{
    const uint32_t per = TIDE_LIST_CHUNK / size;
    *place = i % per * size;
    return ((const uint32_t *)(b + 1))[i / per];
}

// The element at `i`, or NULL past the end. tide_list_at is to read it, and
// tide_list_at_mut to change it in place (a world's page is made its own,
// apart from its snapshots: see tide/page.h). It moves when a list with its
// elements side by side grows; one in chunks keeps them where they are.
//
// A world's list is found through its heap's page table, which other threads
// may be reading meanwhile (see tide/heap.h), once for its block and its
// chunk alike. A chunk is a page of its own, so its elements start right
// after the header at the page's start.
static inline void *tide_list_at(const tide_list l, const int32_t i, const uint32_t size)
{
    if (!l.at) return NULL;
    const uint32_t offset = l.at & 0x3FFFFFFFu;
    if (l.at >> 30 == TIDE_IN_SCRATCH) {
        tide_block *b = (tide_block *)(uintptr_t)tide_scratch_at(offset);
        return i >= 0 && (uint32_t)i < b->a ? (char *)(b + 1) + (size_t)i * size : NULL;
    }
    const tide_heap *heap = tide_heap_read(l.at >> 30);
    if (!heap) return NULL;
    tide_page *const *pages = __atomic_load_n(&heap->page, __ATOMIC_ACQUIRE);
    const tide_page *p = __atomic_load_n(&pages[offset >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
    tide_block *b = (tide_block *)(uintptr_t)((const uint8_t *)tide_page_data(p) + (offset - (p->first << TIDE_HEAP_PAGE_SHIFT)));
    if (i < 0 || (uint32_t)i >= b->a) return NULL;
    if (b->b == TIDE_LIST_ASSET) return (void *)(uintptr_t)tide_list_asset_at(l, i, size);
    if (!(b->b & TIDE_LIST_CHUNKED)) return (char *)(b + 1) + (size_t)i * size;
    uint32_t place;
    const uint32_t chunk = tide_list_chunk(b, (uint32_t)i, size, &place);
    const tide_page *c = __atomic_load_n(&pages[chunk >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
    return (char *)tide_page_data(c) + sizeof(tide_block) + place;
}

// A list found once, to read many of its elements at random, as a map's
// lookup does with its index and entries: what tide_list_at finds again for
// every element (the heap, its page table, the list's block) found the once,
// and the first TIDE_LIST_VIEW_CHUNKS chunks' elements each found the first
// time one of theirs is read. tide_list_view_find makes one for code that
// reads a while and lets it go, as a lookup does; tide_list_view_hold keeps
// one across code that may change the list meanwhile, as generated code keeps
// a map's for a whole function (tide_map_view): it finds the list again only
// when what it found may have changed.
//
// What it found holds while the list is the same (its tide_list.at), its
// heap hasn't moved or released a block or copied a page since
// (tide_heap.moves), and its count is what it was: elements added or removed
// in place change the count; growing a block or a list's chunks, or a page
// made a world's own to be changed, moves the heap. A view found in a page
// table that's since been replaced is still read rightly till the heap
// settles, after the code (see tide/heap.h). A view of a list in the scratch
// area is of the place, which is the list's as long as the list has it.
#define TIDE_LIST_VIEW_CHUNKS 32u

typedef struct tide_list_view {
    const uint8_t *elements;   // Side by side from here; NULL in chunks or when there are none to read
    const uint32_t *directory; // In chunks: each chunk's heap offset
    tide_page *const *pages;   // ...in this page table
    uint32_t count;            // Elements to read: 0 for none
    uint32_t at;               // The list (its tide_list.at), 0 for none
    const tide_heap *heap;     // ...in this heap, NULL in the scratch area
    uint32_t moves;            // ...whose moves were these
    uint32_t known;            // Which of the first chunks `chunk` has found, a bit each
    const tide_block *header;  // The list's block, whose count says it still holds; NULL with none
    bool borrowed;             // An asset's elements or the pack's: made this value's own before it's written (tide_list_prepare)
    const uint8_t *chunk[TIDE_LIST_VIEW_CHUNKS]; // Each chunk's first element, where `known` says
} tide_list_view;

static inline void tide_list_view_find(tide_list_view *v, const tide_list l, const uint32_t size)
{
    v->elements = NULL;
    v->directory = NULL;
    v->pages = NULL;
    v->count = 0u;
    v->at = l.at;
    v->heap = NULL;
    v->moves = 0u;
    v->known = 0u;
    v->header = NULL;
    v->borrowed = false;
    if (!l.at) return;
    const uint32_t offset = l.at & 0x3FFFFFFFu;
    if (l.at >> 30 == TIDE_IN_SCRATCH) {
        const tide_block *b = (const tide_block *)(uintptr_t)tide_scratch_at(offset);
        v->elements = (const uint8_t *)(b + 1);
        v->count = b->a;
        v->header = b;
        v->borrowed = tide_pack_holds(l.at);
        return;
    }
    const tide_heap *heap = tide_heap_read(l.at >> 30);
    if (!heap) {
        v->at = 0u; // Nothing to hold
        return;
    }
    v->heap = heap;
    v->moves = __atomic_load_n(&heap->moves, __ATOMIC_RELAXED);
    tide_page *const *pages = __atomic_load_n(&heap->page, __ATOMIC_ACQUIRE);
    const tide_page *p = __atomic_load_n(&pages[offset >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
    const tide_block *b = (const tide_block *)(uintptr_t)((const uint8_t *)tide_page_data(p) + (offset - (p->first << TIDE_HEAP_PAGE_SHIFT)));
    v->header = b;
    if (b->b == TIDE_LIST_ASSET) { // The asset's bytes, where they're the right shape (as tide_list_asset_at)
        v->elements = (const uint8_t *)tide_list_asset_at(l, 0, size);
        v->count = v->elements ? b->a : 0u;
        v->borrowed = true;
        return;
    }
    v->count = b->a;
    if (b->b & TIDE_LIST_CHUNKED) {
        v->directory = (const uint32_t *)(const void *)(b + 1);
        v->pages = pages;
    } else {
        v->elements = (const uint8_t *)(b + 1);
    }
}

// Whether a view found of `l` still holds.
static inline __attribute__((always_inline)) bool tide_list_view_holds(const tide_list_view *v, const tide_list l)
{
    if (v->at != l.at || !v->at) return false;
    if (v->heap && __atomic_load_n(&v->heap->moves, __ATOMIC_RELAXED) != v->moves) return false;
    // An asset's count is its descriptor's, which never changes; any other's is in its block
    return (v->borrowed && v->heap) || v->header->a == v->count;
}

// A view of `l`, found again only if the one `v` has doesn't still hold.
static inline __attribute__((always_inline)) void tide_list_view_hold(tide_list_view *v, const tide_list l, const uint32_t size)
{
    if (!tide_list_view_holds(v, l)) tide_list_view_find(v, l, size);
}

// Element `i` of a view, as tide_list_at would have it: NULL past the end.
static inline __attribute__((always_inline)) const void *tide_list_view_at(tide_list_view *v, const uint32_t i, const uint32_t size)
{
    if (i >= v->count) return NULL;
    if (v->elements) return v->elements + (size_t)i * size;
    const uint32_t per = TIDE_LIST_CHUNK / size;
    const uint32_t n = i / per;
    const size_t place = sizeof(tide_block) + (size_t)(i - n * per) * size;
    if (n < TIDE_LIST_VIEW_CHUNKS) {
        if (!(v->known >> n & 1u)) {
            const uint32_t chunk = v->directory[n];
            const tide_page *c = __atomic_load_n(&v->pages[chunk >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
            v->chunk[n] = (const uint8_t *)tide_page_data(c);
            v->known |= 1u << n;
        }
        return v->chunk[n] + place;
    }
    const uint32_t chunk = v->directory[n];
    const tide_page *c = __atomic_load_n(&v->pages[chunk >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
    return (const char *)tide_page_data(c) + place;
}

// Only the code changing a heap writes it, so it reads the page table as it
// left it (see tide_heap_write).
static inline void *tide_list_at_mut(const tide_list l, const int32_t i, const uint32_t size)
{
    if (!l.at) return NULL;
    const uint32_t offset = l.at & 0x3FFFFFFFu;
    if (l.at >> 30 == TIDE_IN_SCRATCH) {
        if (tide_pack_holds(l.at)) return NULL; // Prepare the value before writing a packed asset.
        tide_block *b = (tide_block *)(uintptr_t)tide_scratch_at(offset);
        return i >= 0 && (uint32_t)i < b->a ? (char *)(b + 1) + (size_t)i * size : NULL;
    }
    tide_heap *heap = tide_heap_of(l.at >> 30);
    if (!heap) return NULL;
    const tide_page *p = heap->page[offset >> TIDE_HEAP_PAGE_SHIFT];
    const tide_block *b = (const tide_block *)(uintptr_t)((const uint8_t *)tide_page_data(p) + (offset - (p->first << TIDE_HEAP_PAGE_SHIFT)));
    if (i < 0 || (uint32_t)i >= b->a) return NULL;
    if (b->b == TIDE_LIST_ASSET) return NULL; // A value's handle must detach first.
    if (!(b->b & TIDE_LIST_CHUNKED)) return (char *)(tide_heap_write(heap, offset) + 1) + (size_t)i * size;
    uint32_t place;
    const uint32_t chunk = tide_list_chunk(b, (uint32_t)i, size, &place);
    return (char *)(tide_heap_write(heap, chunk) + 1) + place;
}

// The run of elements code last read from a world's list, so a loop over its
// elements finds each chunk once rather than at every element: generated code
// keeps one for each list type in each function. It holds while the list's
// heap hasn't moved or released a block since (tide_heap.moves), and only for
// elements below the list's count as it is then, so whatever the code does to
// the list meanwhile, it never reads where it shouldn't.
typedef struct tide_list_cache {
    uint32_t at;              // The list (its tide_list.at), 0 for none
    uint32_t moves;           // Its heap's moves when it was looked up
    const tide_heap *heap;    // ...that heap
    const tide_block *header; // The list's block, whose `a` is its count
    uint32_t first;           // The run: elements from `first`, `count` of them, side by side from `elements`
    uint32_t count;
    const uint8_t *elements;
    // The run's page is the list's world's own (tide_list_cache_fill_mut made it so), so its elements are
    // written where they are, as long as the cache holds: nothing takes a page back but a snapshot, between
    // ticks, and a page copied to be another's own moves the heap
    bool writable;
} tide_list_cache;

// tide_list_at, filling the cache with the run element `i` is in.
const void *tide_list_cache_fill(tide_list_cache *c, tide_list l, int32_t i, uint32_t size);

// tide_list_at_mut, filling the cache with the run element `i` is in, writable.
void *tide_list_cache_fill_mut(tide_list_cache *c, tide_list l, int32_t i, uint32_t size);

// tide_list_at, through a cache.
static inline const void *tide_list_cached_at(tide_list_cache *c, const tide_list l, const int32_t i, const uint32_t size)
{
    if (c->at == l.at && c->at && (uint32_t)i - c->first < c->count && (uint32_t)i < c->header->a
        && __atomic_load_n(&c->heap->moves, __ATOMIC_RELAXED) == c->moves) {
        return c->elements + (size_t)((uint32_t)i - c->first) * size;
    }
    return tide_list_cache_fill(c, l, i, size);
}

// tide_list_at_mut, through a cache: what code writes element by element finds
// its run's page once, and writes the rest where they are.
static inline void *tide_list_cached_at_mut(tide_list_cache *c, const tide_list l, const int32_t i, const uint32_t size)
{
    if (c->writable && c->at == l.at && c->at && (uint32_t)i - c->first < c->count && (uint32_t)i < c->header->a
        && __atomic_load_n(&c->heap->moves, __ATOMIC_RELAXED) == c->moves) {
        return (uint8_t *)(uintptr_t)c->elements + (size_t)((uint32_t)i - c->first) * size;
    }
    return tide_list_cache_fill_mut(c, l, i, size);
}

// A world's list, read from outside, like a host reading a component: its
// count, and its element at `i` (NULL past the end).
int32_t tide_list_read_count(const tide_heap *heap, tide_list l);
const void *tide_list_read(const tide_heap *heap, tide_list l, int32_t i, uint32_t size);

// A new element at the end, zeroed, or NULL when the list is as big as one
// gets (1 GiB of elements: what a block's offsets reach).
void *tide_list_add(tide_list *l, uint32_t size, uint32_t where);

// A new element at `i` (clamped to the list), zeroed, the ones after it moved
// along; or NULL when the list is as big as one gets.
void *tide_list_insert(tide_list *l, int32_t i, uint32_t size, uint32_t where);

// Removes the element at `i`, moving the ones after it back. Past the end, it
// does nothing. Release its text first.
void tide_list_remove_at(tide_list *l, int32_t i, uint32_t size);

// Removes every element. Release their text first.
void tide_list_clear(tide_list *l, uint32_t size);

// Makes the list `count` elements long (none, for a count under 0), in one
// go. New ones are zeroed. With fewer, the rest go: release their text first.
// A world's list in chunks gives back the chunks it no longer needs. A list
// that would be bigger than one gets stays as it is.
void tide_list_resize(tide_list *l, int32_t count, uint32_t size, uint32_t where);

// A copy in the scratch area: a list taken out of a field or variable.
tide_list tide_list_copy(tide_list l, uint32_t size);

// A list in the scratch area of `count` elements: [a, b, c].
tide_list tide_list_from(const void *items, int32_t count, uint32_t size);

// Assigning: `value`'s elements, a copy if `to` is part of a world, or `value`
// itself otherwise. Release the old elements' text first, and own the new ones'
// after.
void tide_list_set(tide_list *to, tide_list value, uint32_t size, uint32_t where);

// A list just copied into a world, like a spawn's component into the command
// queue: its elements in a block of the world's own. Own their text after.
void tide_list_own(tide_list *l, uint32_t size, uint32_t where);

// A world's list leaving it. Release its elements' text first.
void tide_list_release(tide_list *l, uint32_t where);

// Text and bytes (engine/src/text.c). tide_str_to_bytes is a List<byte> of
// the text's UTF-8, and tide_str_from_bytes the text those bytes are, a copy,
// or false when they aren't UTF-8 (tide_utf8_valid). tide_str_split is a
// List<string> of the pieces of `a` between each `separator` and the next,
// empty ones too, so joining them with the separator gives `a` back: one
// piece, the whole text, when the separator isn't in it or is empty. All
// three are in the scratch area.
tide_list tide_str_to_bytes(tide_str a);
bool tide_str_from_bytes(tide_list bytes, tide_str *text);
tide_list tide_str_split(tide_str a, tide_str separator);
tide_list tide_str_split_char(tide_str a, int32_t separator);

// Sorts the elements, stable: `before(ctx, a, b)` says whether `a` goes before
// `b` (a strict order), and elements it doesn't order keep their order. The
// elements are compared where they are, or in a copy when they're in chunks,
// which goes back once sorted. Merge sort, so the same calls in the same order
// on every machine.
void tide_list_sort(tide_list *l, uint32_t size, bool (*before)(const void *ctx, const void *a, const void *b),
                    const void *ctx);

// For C, which takes a list as a pointer to its elements side by side (NULL
// for none): the list's own elements when they are, made its world's own when
// C `changes` them, or else a copy, which tide_list_unflatten lets go of,
// first writing it back into the list when C changes it. A copy is in memory
// of its own, as big as the list, rather than the scratch area. `copied` says
// which it was.
void *tide_list_flatten(tide_list l, uint32_t size, bool changes, bool *copied);
void tide_list_unflatten(tide_list l, void *flat, uint32_t size, bool changes, bool copied);

// ---------------------------------------------------------------------------
// A list C makes: an extern function's `out List<T>` parameter, which C gets
// as a tide_list_out. C asks for room, fills it, and says how many elements
// there are; once it returns, the caller's list is exactly those. What the
// list held before isn't passed, so nothing of it is copied for C.
//
//     int WriteImage(tide_list_out *image)
//     {
//         uint8_t *bytes = tide_list_out_room(image, 4096);
//         const uint32_t written = fill(bytes, 4096);
//         tide_list_out_count(image, written);
//         return (int)written;
//     }
//
// C that knows which elements changed can say only those, and the list keeps
// the rest: nothing of what it keeps is written, compared or hashed again,
// which is what a big list that changes a little every tick wants.
//
//     int PatchImage(tide_list_out *image)
//     {
//         tide_list_out_keep(image, 4096);             // As it was, this long
//         tide_list_out_part(image, 128, changed, 16); // ...but for these 16
//         return 4096;
//     }

// Elements C gives for part of a list that keeps the rest.
typedef struct tide_list_part {
    uint32_t first;       // The first element they go to
    uint32_t count;       // ...and how many there are
    const void *elements; // Side by side, in C's memory
} tide_list_part;

typedef struct tide_list_out {
    void *room;     // What C fills: memory of the engine's, kept for the next call
    uint32_t size;  // An element's bytes
    uint32_t asked; // The elements there's room for
    uint32_t count; // ...and how many of them the list gets
    bool keeps;     // The list keeps what it holds, is `count` long, and has `parts` written over it
    uint32_t parts_count;
    tide_list_part *parts; // Memory of the engine's too
} tide_list_out;

// Memory for `count` elements side by side, for C to fill; what's in it is
// unspecified. Asking again gives memory for the new count and the earlier
// one is no longer good, so C that finds it needs more asks again and starts
// over. NULL for no elements, and for more than a list holds (1 GiB of them).
void *tide_list_out_room(tide_list_out *out, uint32_t count);

// How many of those elements, from the first, the list has once C returns: at
// most the room last asked for. Never called: the list is empty.
void tide_list_out_count(tide_list_out *out, uint32_t count);

// The other way: the list keeps the elements it holds, and is `count` long
// once C returns. The ones past that go, and the ones it gains are zeros.
// Room asked for before is let go, as asking for room lets this go: of the
// two, what C said last stands.
void tide_list_out_keep(tide_list_out *out, uint32_t count);

// After tide_list_out_keep: the `count` elements from `first` on are the ones
// at `elements`, side by side. They're read once C has returned, so they're
// in memory that's still good then, which nothing changes meanwhile: C's own,
// not the function's variables. Parts are written in the order they're given,
// a later one over an earlier one, and what's past the list's count is left
// out.
void tide_list_out_part(tide_list_out *out, uint32_t first, const void *elements, uint32_t count);

// For generated code, around the call: begin sets `out` up for elements of
// `size` bytes, and end makes the list `l`, which is at `where`, what C said,
// and lets the room go. A world's list is written chunk by chunk and only
// where its bytes differ, so a chunk that comes out the same stays shared
// with the world's snapshots and isn't hashed again; the chunks past the new
// count go back to the heap. A list that keeps what it holds has only its
// parts compared, each with the elements it goes over.
void tide_list_out_begin(tide_list_out *out, uint32_t size);
void tide_list_out_end(tide_list_out *out, tide_list *l, uint32_t where);

// ---------------------------------------------------------------------------
// Parallel loops over a list's elements, by index: `parallel (var i in items)`
// and `parallel (var i in items by 2 offset o)` (see docs/spec.md, Lists).
// Their steps run at once, on threads (tide_parallel_for): each reads the list
// as the loop found it, and writes only its own element or block, into its
// task's copy of its blocks' elements. The loop's end puts what changed into
// the list, chunk by chunk, so the result is the same however the steps were
// shared out, and a chunk nothing changed stays as it was. A block that would
// go past the list's end is left out.

typedef struct tide_par_list {
    tide_list *list;
    uint32_t size;      // An element's bytes
    int32_t block;      // Each step's block, in elements
    int32_t offset;     // Where the blocks start
    int32_t lo, hi;     // Blocks within the list, by index: from lo up to hi
    int32_t per_task;   // Blocks in a task; the last has what's left
    uint32_t tasks;
    uint8_t **copies;   // Each task's copy of its blocks' elements, once it's run
    uint64_t steps;     // Blocks in all: how much work it is
} tide_par_list;

// Sets `l` up to go over a world's list `list` of elements of `size` bytes, in
// blocks of `block` elements starting at `offset`.
void tide_par_list_begin(tide_par_list *l, tide_list *list, uint32_t size, int32_t block, int32_t offset);

// Task `task`'s blocks, by index: from *from up to *to. Its copy of their
// elements, as the loop found them, which its steps write: the first is the
// element `offset + *from * block`.
void *tide_par_list_task(tide_par_list *l, uint32_t task, int32_t *from, int32_t *to);

// Puts what the steps changed into the list and lets the copies go.
void tide_par_list_end(tide_par_list *l);
