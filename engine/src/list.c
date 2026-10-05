#include "tide/list.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "tide/text.h"

// See tide/list.h.

#define OFFSET(at) ((at) & 0x3FFFFFFFu)

static void *items(tide_block *b)
{
    return b + 1;
}

// Whether a list at `where` (see tide/text.h) has its own block in a world's heap.
static bool in_world(const uint32_t where)
{
    return tide_heap_of(where) != NULL;
}

// Whether a list at `where` has a block of its own to change: in its world's
// heap, or in the scratch area for a list that's no world's. One that doesn't
// borrows another's, which stays as it is.
static bool own_block(const tide_list l, const uint32_t where)
{
    return in_world(where) ? l.at >> 30 == where : l.at >> 30 == TIDE_IN_SCRATCH;
}

// A list in chunks: where each chunk is, and how many it has room to list.
static uint32_t *directory(tide_block *b)
{
    return (uint32_t *)(uintptr_t)(b + 1);
}

static uint32_t directory_room(const tide_block *b)
{
    return (uint32_t)((((uint64_t)16u << b->size_class) - sizeof(tide_block)) / sizeof(uint32_t));
}

const void *tide_list_cache_fill(tide_list_cache *c, const tide_list l, const int32_t i, const uint32_t size)
{
    *c = (tide_list_cache){0};
    const tide_heap *heap = l.at ? tide_heap_read(l.at >> 30) : NULL;
    if (!heap) return tide_list_at(l, i, size); // A list in the scratch area moves as it grows: never cached
    const uint32_t moves = __atomic_load_n(&heap->moves, __ATOMIC_RELAXED); // Before finding anything
    const void *found = tide_list_at(l, i, size);
    if (!found) return NULL;
    tide_page *const *pages = __atomic_load_n(&heap->page, __ATOMIC_ACQUIRE);
    const tide_page *p = __atomic_load_n(&pages[OFFSET(l.at) >> TIDE_HEAP_PAGE_SHIFT], __ATOMIC_ACQUIRE);
    const tide_block *b =
        (const tide_block *)(const void *)((const uint8_t *)tide_page_data(p) + (OFFSET(l.at) - (p->first << TIDE_HEAP_PAGE_SHIFT)));
    *c = (tide_list_cache){l.at, moves, heap, b, 0, b->a, (const uint8_t *)(b + 1)};
    if (b->b & TIDE_LIST_CHUNKED) { // Its chunk's elements
        const uint32_t per = TIDE_LIST_CHUNK / size;
        c->first = (uint32_t)i / per * per;
        c->count = per;
        c->elements = (const uint8_t *)found - (size_t)((uint32_t)i - c->first) * size;
    }
    return found;
}

int32_t tide_list_read_count(const tide_heap *heap, const tide_list l)
{
    return l.at ? (int32_t)tide_heap_block(heap, OFFSET(l.at))->a : 0;
}

const void *tide_list_read(const tide_heap *heap, const tide_list l, const int32_t i, const uint32_t size)
{
    if (i < 0 || i >= tide_list_read_count(heap, l)) return NULL;
    const tide_block *b = tide_heap_block(heap, OFFSET(l.at));
    if (!(b->b & TIDE_LIST_CHUNKED)) return (const char *)(b + 1) + (size_t)i * size;
    uint32_t place;
    const uint32_t chunk = tide_list_chunk(b, (uint32_t)i, size, &place);
    return (const char *)(tide_heap_block(heap, chunk) + 1) + place;
}

// The elements from `i` on that are side by side with it, as far as the end
// of the list or of i's chunk: where they start, and how many there are.
static char *run(const tide_list l, const uint32_t i, const uint32_t size, const bool write, uint32_t *n)
{
    const tide_block *b = tide_block_at(l.at);
    const uint32_t count = b->a;
    if (b->b & TIDE_LIST_CHUNKED) {
        const uint32_t per = TIDE_LIST_CHUNK / size;
        const uint32_t left = per - i % per;
        *n = left < count - i ? left : count - i;
    } else {
        *n = count - i;
    }
    return write ? tide_list_at_mut(l, (int32_t)i, size) : tide_list_at(l, (int32_t)i, size);
}

// Copies the first `count` elements of `from` over those of `to`, which has
// that many already.
static void copy_elements(const tide_list to, const tide_list from, const uint32_t count, const uint32_t size)
{
    uint32_t i = 0;
    while (i < count) {
        uint32_t room, have;
        char *dst = run(to, i, size, true, &room); // First: it can give a page a copy of its own
        const char *src = run(from, i, size, false, &have);
        uint32_t n = room < have ? room : have;
        if (n > count - i) n = count - i;
        memmove(dst, src, (size_t)n * size);
        i += n;
    }
}

// A new block for `capacity` elements side by side, where a list at `where`
// keeps them: its world's heap, or the scratch area. Returns its tagged
// offset, or 0 when they'd take more than an offset reaches.
static uint32_t new_block(const uint32_t where, const uint32_t capacity, const uint32_t size)
{
    const uint64_t bytes = (uint64_t)capacity * size;
    if (bytes > 0x40000000u) return 0;
    tide_heap *heap = tide_heap_of(where);
    uint32_t at = 0;
    tide_block *b;
    if (heap) {
        const uint32_t block = tide_heap_alloc(heap, (uint32_t)bytes);
        b = tide_heap_write(heap, block);
        at = where << 30 | block;
    } else {
        b = tide_scratch_block((uint32_t)bytes, "a list", &at);
    }
    b->a = 0;
    b->b = capacity;
    return at;
}

// Gives back a block that was a list's own, with its chunks: freed with the
// world's others, or left in the scratch area, which goes back in one piece.
static void drop_block(const uint32_t where, const uint32_t at)
{
    tide_heap *heap = tide_heap_of(where);
    if (!heap || !at || at >> 30 != where) return;
    const tide_block *b = tide_heap_block(heap, OFFSET(at));
    if (b->b & TIDE_LIST_CHUNKED) {
        const uint32_t chunks = b->b & ~TIDE_LIST_CHUNKED;
        for (uint32_t k = 0; k < chunks; k++) tide_heap_release(heap, directory((tide_block *)(uintptr_t)b)[k]);
    }
    tide_heap_release(heap, OFFSET(at));
}

// Room for `need` elements in chunks, for a world's list: more chunks, listed
// in the list's block while it has room, or else in a new block that takes
// its place. A list that wasn't in chunks of its own has its elements copied
// into them.
static void grow_chunks(tide_list *l, const uint32_t need, const uint32_t size, const uint32_t where, const bool own)
{
    tide_heap *heap = tide_heap_of(where);
    const tide_block *b = tide_block_at(l->at);
    const uint32_t count = b ? b->a : 0;
    const uint32_t per = TIDE_LIST_CHUNK / size;
    const uint32_t chunks = (need + per - 1u) / per;
    const uint32_t kept = b && own && (b->b & TIDE_LIST_CHUNKED) ? b->b & ~TIDE_LIST_CHUNKED : 0u;

    uint32_t block = OFFSET(l->at);
    if (!kept || directory_room(b) < chunks) {
        uint32_t room = 4u;
        while (room < chunks) room *= 2u;
        block = tide_heap_alloc(heap, room * (uint32_t)sizeof(uint32_t));
        tide_block *h = tide_heap_write(heap, block);
        h->a = count;
        h->b = TIDE_LIST_CHUNKED | kept;
        // The old block's list of chunks, read after the new one is made: it
        // can give the heap another page
        if (kept) memcpy(directory(h), directory(tide_heap_block(heap, OFFSET(l->at))), kept * sizeof(uint32_t));
    }
    for (uint32_t k = kept; k < chunks; k++) {
        const uint32_t chunk = tide_heap_alloc(heap, TIDE_LIST_CHUNK);
        tide_block *h = tide_heap_write(heap, block);
        directory(h)[k] = chunk;
        h->b = TIDE_LIST_CHUNKED | (k + 1u);
    }

    const tide_list grown = {where << 30 | block};
    if (grown.at != l->at) {
        if (!kept && count) copy_elements(grown, *l, count, size);
        if (kept) tide_heap_release(heap, OFFSET(l->at)); // Its chunks are the new block's
        else if (own) drop_block(where, l->at);
        *l = grown;
    }
}

// Room for `more` elements: in the list's own block, grown if it's full, and
// moved into a block of its own if it borrowed one. A world's list keeps its
// elements in chunks once they take more than a page. False when the list is
// as big as one gets.
static bool reserve(tide_list *l, const uint32_t more, const uint32_t size, const uint32_t where)
{
    const tide_block *b = tide_block_at(l->at);
    const uint32_t count = b ? b->a : 0;
    const bool own = !b || own_block(*l, where);
    const uint32_t per = size <= TIDE_LIST_CHUNK ? TIDE_LIST_CHUNK / size : 0u;
    const uint64_t capacity = !b                            ? 0u
                              : b->b & TIDE_LIST_CHUNKED ? (uint64_t)(b->b & ~TIDE_LIST_CHUNKED) * per
                                                          : b->b;
    const uint64_t need = (uint64_t)count + more;
    if (b && own && need <= capacity) return true;
    if (need * size > 0x40000000u) return false;
    if (in_world(where) && per && need > per) {
        grow_chunks(l, (uint32_t)need, size, where, own);
        return true;
    }
    uint32_t room = b && !(b->b & TIDE_LIST_CHUNKED) && b->b ? b->b : 4u;
    while (room < need) room *= 2u;
    if (in_world(where) && per && room > per) room = per; // As many as fit in a page, before it's in chunks
    const uint32_t at = new_block(in_world(where) ? where : TIDE_IN_SCRATCH, room, size);
    if (!at) return false;
    tide_block_write(at)->a = count;
    if (count) copy_elements((tide_list){at}, *l, count, size);
    if (own) drop_block(where, l->at);
    l->at = at;
    return true;
}

void *tide_list_add(tide_list *l, const uint32_t size, const uint32_t where)
{
    return tide_list_insert(l, tide_list_count(*l), size, where);
}

void *tide_list_insert(tide_list *l, int32_t i, const uint32_t size, const uint32_t where)
{
    if (!reserve(l, 1, size, where)) return NULL;
    tide_block *b = tide_block_write(l->at);
    const int32_t count = (int32_t)b->a;
    if (i < 0) i = 0;
    if (i > count) i = count;
    b->a++;
    if (b->b & TIDE_LIST_CHUNKED) {
        // The ones after it move along one by one, from the last, across chunks
        for (int32_t k = count; k > i; k--) {
            char *dst = tide_list_at_mut(*l, k, size);
            memcpy(dst, tide_list_at(*l, k - 1, size), size);
        }
        char *at = tide_list_at_mut(*l, i, size);
        memset(at, 0, size);
        return at;
    }
    char *at = (char *)items(b) + (size_t)i * size;
    memmove(at + size, at, (size_t)(count - i) * size);
    memset(at, 0, size);
    return at;
}

void tide_list_remove_at(tide_list *l, const int32_t i, const uint32_t size)
{
    const int32_t count = tide_list_count(*l);
    if (i < 0 || i >= count) return;
    tide_block *b = tide_block_write(l->at);
    if (b->b & TIDE_LIST_CHUNKED) {
        for (int32_t k = i; k + 1 < count; k++) {
            char *dst = tide_list_at_mut(*l, k, size);
            memcpy(dst, tide_list_at(*l, k + 1, size), size);
        }
        memset(tide_list_at_mut(*l, count - 1, size), 0, size); // Freed space is zero, as in the heap
        b->a--;
        return;
    }
    char *at = (char *)items(b) + (size_t)i * size;
    memmove(at, at + size, (size_t)(count - i - 1) * size);
    b->a--;
    memset((char *)items(b) + (size_t)b->a * size, 0, size);
}

void tide_list_clear(tide_list *l, const uint32_t size)
{
    const uint32_t count = (uint32_t)tide_list_count(*l);
    if (count == 0) return;
    // It keeps its block, and its chunks, zeroed, for what's added next.
    for (uint32_t i = 0; i < count;) {
        uint32_t n;
        char *at = run(*l, i, size, true, &n);
        memset(at, 0, (size_t)n * size);
        i += n;
    }
    tide_block_write(l->at)->a = 0;
}

// Leaves a list its first `count` elements, of the more it has. In a world,
// what the rest took is zero afterwards, as freed space is in the heap, whose
// pages' hashes cover it: the end of the last chunk it keeps is zeroed, the
// chunks past it go back to the heap, and the list's block goes with them
// when it keeps none.
static void cut(tide_list *l, const uint32_t count, const uint32_t size, const uint32_t where)
{
    if (!own_block(*l, where)) { // Another's block: a block of its own, with what it keeps
        const tide_list borrowed = *l;
        l->at = 0;
        if (!count || !reserve(l, count, size, where)) return;
        tide_block_write(l->at)->a = count;
        copy_elements(*l, borrowed, count, size);
        return;
    }
    tide_heap *heap = tide_heap_of(where);
    if (!heap) { // The scratch area's memory is whatever was there, and nothing reads past the count
        tide_block_write(l->at)->a = count;
        return;
    }
    tide_block *b = tide_heap_write(heap, OFFSET(l->at));
    const uint32_t had = b->a;
    if (!(b->b & TIDE_LIST_CHUNKED)) {
        memset((char *)items(b) + (size_t)count * size, 0, (size_t)(had - count) * size);
        b->a = count;
        return;
    }
    if (!count) {
        tide_list_release(l, where);
        return;
    }
    const uint32_t per = TIDE_LIST_CHUNK / size;
    const uint32_t chunks = b->b & ~TIDE_LIST_CHUNKED;
    const uint32_t keep = (count + per - 1u) / per;
    const uint32_t end = had < keep * per ? had : keep * per; // As far as its elements went in the chunks it keeps
    if (end > count) memset(tide_list_at_mut(*l, (int32_t)count, size), 0, (size_t)(end - count) * size);
    for (uint32_t k = keep; k < chunks; k++) {
        tide_heap_release(heap, directory(b)[k]);
        directory(b)[k] = 0;
    }
    b->b = TIDE_LIST_CHUNKED | keep;
    b->a = count;
}

void tide_list_resize(tide_list *l, const int32_t count, const uint32_t size, const uint32_t where)
{
    const uint32_t want = count > 0 ? (uint32_t)count : 0u;
    const uint32_t had = (uint32_t)tide_list_count(*l);
    if (want < had) {
        cut(l, want, size, where);
        return;
    }
    if (want == had || !reserve(l, want - had, size, where)) return;
    tide_block *b = tide_block_write(l->at);
    // A world's list has zeros past its count: its heap's blocks start zeroed,
    // and whatever takes elements out zeroes where they were. The scratch
    // area's memory is whatever was there.
    if (!in_world(where)) memset((char *)items(b) + (size_t)had * size, 0, (size_t)(want - had) * size);
    b->a = want;
}

tide_list tide_list_copy(const tide_list l, const uint32_t size)
{
    tide_list out = {0};
    const int32_t count = tide_list_count(l);
    if (count <= 0) return out;
    uint32_t at;
    tide_block *b = tide_scratch_block((uint32_t)count * size, "a list", &at);
    b->a = (uint32_t)count;
    b->b = (uint32_t)count;
    out.at = at;
    copy_elements(out, l, (uint32_t)count, size);
    return out;
}

tide_list tide_list_from(const void *elements, const int32_t count, const uint32_t size)
{
    tide_list out = {0};
    if (count <= 0) return out;
    uint32_t at;
    tide_block *b = tide_scratch_block((uint32_t)count * size, "a list", &at);
    memcpy(items(b), elements, (size_t)count * size);
    b->a = (uint32_t)count;
    b->b = (uint32_t)count;
    out.at = at;
    return out;
}

void tide_list_set(tide_list *to, const tide_list value, const uint32_t size, const uint32_t where)
{
    if (!in_world(where)) {
        *to = value;
        return;
    }
    const int32_t count = tide_list_count(value);
    tide_list_clear(to, size);
    if (count == 0 || !reserve(to, (uint32_t)count, size, where)) return;
    tide_block_write(to->at)->a = (uint32_t)count;
    copy_elements(*to, value, (uint32_t)count, size);
}

void tide_list_own(tide_list *l, const uint32_t size, const uint32_t where)
{
    if (!in_world(where) || !l->at) return;
    const tide_list borrowed = *l;
    l->at = 0;
    tide_list_set(l, borrowed, size, where);
}

void tide_list_release(tide_list *l, const uint32_t where)
{
    drop_block(where, l->at);
    l->at = 0;
}

void tide_list_sort(tide_list *l, const uint32_t size, bool (*before)(const void *ctx, const void *a, const void *b),
                    const void *ctx)
{
    const uint32_t n = (uint32_t)tide_list_count(*l);
    if (n < 2) return;
    bool copied;
    char *flat = tide_list_flatten(*l, size, true, &copied);
    char *temp = tide_alloc((size_t)n * size);
    // Bottom-up merges of runs twice as long each pass, between the two
    // buffers; a tie takes the left run's element, so equal ones keep their order
    char *src = flat;
    char *dst = temp;
    for (uint32_t width = 1; width < n; width *= 2) {
        for (uint32_t lo = 0; lo < n; lo += 2 * width) {
            const uint32_t mid = lo + width < n ? lo + width : n;
            const uint32_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            uint32_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) {
                const bool right_first = before(ctx, src + (size_t)j * size, src + (size_t)i * size);
                memcpy(dst + (size_t)k++ * size, src + (size_t)(right_first ? j++ : i++) * size, size);
            }
            if (i < mid) memcpy(dst + (size_t)k * size, src + (size_t)i * size, (size_t)(mid - i) * size);
            if (j < hi) memcpy(dst + (size_t)(k + (mid - i)) * size, src + (size_t)j * size, (size_t)(hi - j) * size);
        }
        char *swap = src;
        src = dst;
        dst = swap;
    }
    if (src != flat) memcpy(flat, src, (size_t)n * size);
    free(temp);
    tide_list_unflatten(*l, flat, size, true, copied);
}

// The copy C gets of a list in chunks is megabytes, and so is the room C
// fills for a list it makes (tide_list_out_room). A list that goes to C every
// tick would have them allocated, and their pages faulted in, every tick. So
// the last one is kept for the next call, whichever thread makes it: taken
// out while it's in use, so two calls at once each have their own. Each says
// how much room it has in the bytes before it, as the one that's kept isn't
// always the last one made.
#define FLAT_HEAD 16u // As malloc aligns: what follows is aligned for any element

static _Atomic(char *) flat_kept;

static void flat_free(char *flat)
{
    if (flat) free(flat - FLAT_HEAD);
}

static char *flat_take(const size_t room)
{
    char *kept = atomic_exchange(&flat_kept, NULL);
    if (kept) {
        tide_memory_sync(); // Another thread may have made it
        size_t has;
        memcpy(&has, kept - FLAT_HEAD, sizeof has);
        if (has >= room) return kept;
        flat_free(kept);
    }
    char *made = tide_alloc(FLAT_HEAD + room);
    memcpy(made, &room, sizeof room);
    return made + FLAT_HEAD;
}

static void flat_give(char *flat)
{
    flat_free(atomic_exchange(&flat_kept, flat));
}

// Writes `count` elements side by side over a list's first ones, run by run.
// Of a world's list, only the runs that differ: a chunk that's written
// becomes the world's own and is hashed again, which one that's as it was
// needn't be, so what a snapshot shares of the list stays shared.
// Where the `n` elements from `i` on go, which are a run (see `run`) about to
// be written over, all of them. A chunk a snapshot shares is made the list's
// own without them: megabytes a tick, for a big list C fills, that would be
// copied only to be replaced.
static char *run_over(const tide_list l, const uint32_t i, const uint32_t n, const uint32_t size)
{
    tide_heap *heap = l.at >> 30 == TIDE_IN_SCRATCH ? NULL : tide_heap_of(l.at >> 30);
    const tide_block *b = tide_block_at(l.at);
    uint32_t place = 0;
    const uint32_t chunk = heap && (b->b & TIDE_LIST_CHUNKED) ? tide_list_chunk(b, i, size, &place) : 0;
    if (!chunk || place) return tide_list_at_mut(l, (int32_t)i, size);
    return (char *)(tide_heap_write_over(heap, chunk, n * size) + 1);
}

static void put_elements(const tide_list l, const char *from, const uint32_t count, const uint32_t size)
{
    const bool compare = l.at >> 30 != TIDE_IN_SCRATCH;
    for (uint32_t i = 0; i < count;) {
        uint32_t n;
        const char *as_it_was = run(l, i, size, false, &n); // First: it says how many
        if (n > count - i) n = count - i;
        const char *mine = from + (size_t)i * size;
        if (!compare || memcmp(as_it_was, mine, (size_t)n * size) != 0) memcpy(run_over(l, i, n, size), mine, (size_t)n * size);
        i += n;
    }
}

void *tide_list_flatten(const tide_list l, const uint32_t size, const bool changes, bool *copied)
{
    *copied = false;
    const tide_block *b = tide_block_at(l.at);
    if (!b || !b->a) return NULL;
    if (!(b->b & TIDE_LIST_CHUNKED)) return changes ? tide_list_at_mut(l, 0, size) : tide_list_at(l, 0, size);
    char *flat = flat_take((size_t)b->a * size);
    for (uint32_t i = 0; i < b->a;) {
        uint32_t n;
        const char *at = run(l, i, size, false, &n);
        memcpy(flat + (size_t)i * size, at, (size_t)n * size);
        i += n;
    }
    *copied = true;
    return flat;
}

void tide_list_unflatten(const tide_list l, void *flat, const uint32_t size, const bool changes, const bool copied)
{
    if (!copied) return;
    if (changes) put_elements(l, flat, (uint32_t)tide_list_count(l), size); // Chunk by chunk, and only the ones C changed
    flat_give(flat);
}

// A list C makes (see tide/list.h)

void tide_list_out_begin(tide_list_out *out, const uint32_t size)
{
    *out = (tide_list_out){.size = size};
}

void *tide_list_out_room(tide_list_out *out, const uint32_t count)
{
    if (out->room) flat_give(out->room); // The next take's, when it's big enough
    out->room = NULL;
    out->asked = 0;
    out->count = 0; // What was said of the room before isn't of this one
    const uint64_t bytes = (uint64_t)count * out->size;
    if (!count || bytes > 0x40000000u) return NULL;
    out->room = flat_take((size_t)bytes);
    out->asked = count;
    return out->room;
}

void tide_list_out_count(tide_list_out *out, const uint32_t count)
{
    out->count = count < out->asked ? count : out->asked;
}

void tide_list_out_end(tide_list_out *out, tide_list *l, const uint32_t where)
{
    if (l->at && !own_block(*l, where)) l->at = 0; // Another's block, which stays as it is: none of it is kept
    const uint32_t had = (uint32_t)tide_list_count(*l);
    uint32_t count = out->count;
    if (count < had) {
        cut(l, count, out->size, where);
    } else if (count > had) {
        if (reserve(l, count - had, out->size, where)) tide_block_write(l->at)->a = count;
        else count = had;
    }
    if (count) put_elements(*l, out->room, count, out->size);
    if (out->room) flat_give(out->room);
    *out = (tide_list_out){0};
}

// Parallel loops (see tide/list.h)

// `b` is above 0.
static int32_t floor_div(const int32_t a, const int32_t b)
{
    return a / b - (a % b != 0 && a < 0 ? 1 : 0);
}

// Element `i` of a world's list, to read, and how many elements from it sit
// side by side (to the end of its chunk, or of the list).
static const uint8_t *list_run(const tide_list l, const int32_t i, const uint32_t size, int32_t *run)
{
    const uint8_t *at = tide_list_at(l, i, size);
    const tide_block *b = tide_heap_block(tide_heap_of(l.at >> 30), OFFSET(l.at));
    *run = (int32_t)b->a - i;
    if (b->b & TIDE_LIST_CHUNKED) {
        const uint32_t per = TIDE_LIST_CHUNK / size;
        const int32_t in_chunk = (int32_t)(per - (uint32_t)i % per);
        if (in_chunk < *run) *run = in_chunk;
    }
    return at;
}

void tide_par_list_begin(tide_par_list *l, tide_list *list, const uint32_t size, const int32_t block, const int32_t offset)
{
    *l = (tide_par_list){.list = list, .size = size, .block = block > 0 ? block : 1};
    // Blocks start at the offset plus any multiple of their size, so an
    // offset, which is any int a game says, is as good as what's left of it
    // by that size: from 0 up to a block, which nothing below overflows with
    l->offset = (int32_t)((int64_t)offset - (int64_t)floor_div(offset, l->block) * l->block);
    const int32_t count = tide_list_count(*list);
    l->lo = 0;                                                       // The first block from 0 on...
    l->hi = floor_div(count - l->offset - l->block, l->block) + 1; // ...and past the last one wholly inside
    if (!list->at || !tide_heap_of(list->at >> 30) || l->hi <= l->lo) return; // Nothing to go through
    // Tasks of 16 blocks or more, so that steps that each take long share
    // out well and steps that take little aren't swamped by their tasks
    const int64_t blocks = (int64_t)l->hi - l->lo;
    l->per_task = (int32_t)((blocks + 1023) / 1024);
    if (l->per_task < 16) l->per_task = 16;
    l->tasks = (uint32_t)((blocks + l->per_task - 1) / l->per_task);
    l->steps = (uint64_t)blocks;
    l->copies = tide_alloc_zeroed(l->tasks, sizeof *l->copies);
}

void *tide_par_list_task(tide_par_list *l, const uint32_t task, int32_t *from, int32_t *to)
{
    *from = l->lo + (int32_t)task * l->per_task;
    *to = *from + l->per_task < l->hi ? *from + l->per_task : l->hi;
    const int32_t first = l->offset + *from * l->block;
    const int32_t count = (*to - *from) * l->block;
    uint8_t *copy = tide_alloc((size_t)count * l->size);
    for (int32_t i = 0; i < count;) { // Run by run
        int32_t run;
        const uint8_t *now = list_run(*l->list, first + i, l->size, &run);
        if (run > count - i) run = count - i;
        memcpy(copy + (size_t)i * l->size, now, (size_t)run * l->size);
        i += run;
    }
    l->copies[task] = copy;
    return copy;
}

void tide_par_list_end(tide_par_list *l)
{
    tide_memory_sync(); // What the steps made on other threads
    for (uint32_t task = 0; task < l->tasks; task++) {
        uint8_t *copy = l->copies[task];
        if (!copy) continue;
        const int32_t from = l->lo + (int32_t)task * l->per_task;
        const int32_t to = from + l->per_task < l->hi ? from + l->per_task : l->hi;
        const int32_t first = l->offset + from * l->block;
        const int32_t count = (to - from) * l->block;
        for (int32_t i = 0; i < count;) { // Run by run: a chunk nothing changed stays as it is
            int32_t run;
            const uint8_t *now = list_run(*l->list, first + i, l->size, &run);
            if (run > count - i) run = count - i;
            const uint8_t *mine = copy + (size_t)i * l->size;
            if (memcmp(now, mine, (size_t)run * l->size) != 0) {
                memcpy(tide_list_at_mut(*l->list, first + i, l->size), mine, (size_t)run * l->size);
            }
            i += run;
        }
        free(copy);
    }
    free(l->copies);
    l->copies = NULL;
}
