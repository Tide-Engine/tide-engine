#pragma once

#include "tide/mesh.h"

// Derived, bounded, frame-local metadata only. No source storage is retained:
// heap is compared as an identity after resolving the current handle again.
enum { TIDE_LIST_DATA_CACHE_ENTRIES = 8 };
typedef struct tide_list_data_entry {
    uintptr_t heap;
    tide_handle at;
    uint64_t heap_used, page_hash, id, version, kind;
    uint32_t hashed, element_size, words, layout, slot;
    int32_t held, count, width, height;
    tide_list_words_fn *write;
} tide_list_data_entry;

typedef struct tide_list_data_cache {
    uint32_t next;
    tide_list_data_entry entries[TIDE_LIST_DATA_CACHE_ENTRIES];
} tide_list_data_cache;

// Instance lists the draw list keeps (tide_draw_mesh_shaded_list), by list handle, item size and layout: a list
// is drawn from its numbers while it stays as it was, and they're kept in a region of a page, a texture of
// numbers (TIDE_PIXELS_LIST of TIDE_TEXELS_INTS, TIDE_LIST_DATA_ACROSS pixels a row) that many lists share, so
// the runs of lists drawn one after another with one mesh and shader are still one run. Numbers alone: no
// pointer.
//
// A list's region stays while frames skip it (a list drawn every other frame, one that left the view) within an
// idle budget: TIDE_DRAW_LISTED_IDLE_FRAMES frames, and TIDE_DRAW_LISTED_IDLE_BYTES of regions not drawn this
// frame or the last, the longest unused going first (by page and place where they're as old). Where a new region
// finds no room, every idle one goes. A page with a region that stays is kept by frames that draw none of it
// (tide_draw_texture.held), and so is the renderer's copy.
enum {
    TIDE_DRAW_LISTED_MOST = 4096, TIDE_DRAW_LISTED_SLOTS = TIDE_DRAW_LISTED_MOST * 2,
    TIDE_DRAW_LISTED_PAGES = 4,    // Pages at most...
    TIDE_DRAW_LISTED_ROWS = 2048,  // ...each of at most these rows: 32 MiB of numbers (TIDE_LIST_DATA_MOST)
    TIDE_DRAW_LISTED_FIRST_ROWS = 16,
    TIDE_DRAW_LISTED_IDLE_FRAMES = 36000,
};
#define TIDE_DRAW_LISTED_IDLE_BYTES (UINT64_C(32) * 1024u * 1024u)
typedef struct tide_draw_listed_entry {
    tide_handle at; // 0: a free slot
    uint64_t version;
    uint32_t item_size, layout;
    uint32_t since;   // The frame its version was first seen
    uint32_t seen;    // ...the last that asked
    uint32_t wait;    // ...and the one from which its version is asked again, after it changed again and again
    uint32_t changes; // How many frames in a row it changed
    // Where its numbers are, at version `written`: page `page` - 1 (0: nowhere), as it was in generation `made`,
    // from pixel `start` (a whole number of its instances' packs, `packs`), `count` instances
    uint32_t page, made, start, count, packs;
    uint64_t written;
} tide_draw_listed_entry;

typedef struct tide_draw_listed_page {
    uint64_t version; // Its texture's: one more each time a region is written
    uint32_t rows;    // ...its rows, 0 before it's made
    uint32_t made;    // Counts the times its pixels were lost (its texture let go, or another made): what regions of
                      // an earlier generation are in is gone
    uint32_t frame;   // The frame its texture was last found in, which keeps it
    uint32_t end;     // The pixel past every region given out in this generation: room after it is free
    // The frame's own lists (a list that changed, or one made along the way) are this frame's region alone, from
    // `transient_start` to `transient_end`, after the others, in frame `transient_frame`
    uint32_t transient_frame, transient_start, transient_end;
} tide_draw_listed_page;

// A page's regions in order of place, as a frame found them, with those it placed since: where a region that
// doesn't fit past them all looks for a gap, without going through every list's entry again.
typedef struct tide_draw_listed_span {
    uint32_t start, end;
} tide_draw_listed_span;
typedef struct tide_draw_listed_spans {
    tide_draw_listed_span *spans;
    uint32_t count, capacity;
    uint32_t frame; // 1 + the frame they're of: 0 where they're to be found again
} tide_draw_listed_spans;

typedef struct tide_draw_listed {
    uint32_t count;
    uint32_t packed; // 1 + the frame whose first listed draw packed its pages' regions together, where it had to
    tide_draw_listed_page pages[TIDE_DRAW_LISTED_PAGES];
    tide_draw_listed_spans spans[TIDE_DRAW_LISTED_PAGES];
    tide_draw_listed_entry entries[TIDE_DRAW_LISTED_SLOTS];
} tide_draw_listed;

// Everything forgotten (tide_draw_forget), or freed with the list: the spans' memory goes too.
void tide_draw_listed_clear(tide_draw_list *d, bool release);

// `count` instances' bytes, `packs` 16-byte packs each, put in the frame's own room of the page that's texture
// `slot` (tide_draw_mesh_shaded_list's), where it has room without growing (so no page pixels move): the pixel
// they start at, a multiple of `packs`, or UINT32_MAX.
uint32_t tide_draw_listed_put(tide_draw_list *d, uint32_t slot, const uint8_t *bytes, uint32_t count, uint32_t packs);

// As the frame ends (tide_draw_reset, before textures go): the pages with regions that stay are held.
void tide_draw_listed_hold(tide_draw_list *d);
