#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tide/entity.h"
#include "tide/net.h"
#include "tide/page.h"

// Archetype storage and the queue of structural changes, as worlds keep them.
// Generated code calls these with each archetype's tide_columns.
//
// A table holds the entities with one combination of components, as columns:
// the entities themselves, the scene each is in (in a world with scenes),
// then each component. Its rows are in chunks of 1 << shift, and a chunk's
// rows of one column are one page, which the world's snapshots share until
// one of them changes it (see tide/page.h): changing a column of a chunk
// copies that page alone. The first chunk starts small and grows, so an
// archetype with a few entities takes little memory. Tables grow with no limit
// but memory.

typedef struct tide_columns {
    uint32_t count;        // Columns
    uint32_t shift;        // Each chunk has room for 1 << shift rows
    const uint32_t *sizes; // Each column's element size
} tide_columns;

typedef struct tide_table {
    uint32_t count;     // Rows in use
    uint32_t chunks;    // Chunks with pages: every one has a row in use
    uint32_t last_room; // Rows the last chunk has room for: only the first chunk, alone, has room for fewer than 1 << shift
    uint32_t room;      // Chunks `pages` has room for
    tide_page **pages;  // Chunk c's column k: pages[c * columns + k]
} tide_table;

// Rows in use in a chunk.
static inline uint32_t tide_table_rows(const tide_table *t, const uint32_t shift, const uint32_t chunk)
{
    const uint32_t left = t->count - (chunk << shift);
    return left < (1u << shift) ? left : 1u << shift;
}

// A chunk's rows of a column, to read.
static inline const void *tide_table_column(const tide_table *t, const tide_columns *c, const uint32_t chunk,
                                            const uint32_t column)
{
    return tide_page_data(t->pages[chunk * c->count + column]);
}

// A row's value in a column, to read.
static inline const void *tide_table_get(const tide_table *t, const tide_columns *c, const uint32_t row,
                                         const uint32_t column)
{
    const uint8_t *rows = tide_table_column(t, c, row >> c->shift, column);
    return rows + (size_t)(row & ((1u << c->shift) - 1u)) * c->sizes[column];
}

// A component in an archetype is in lanes: its bytes `width` at a time, each a
// column of its own, so a loop over a chunk's rows reads and writes each field
// side by side, as SIMD wants them. The width is 4 bytes, or 2 or 1 for a
// component whose size isn't a multiple of 4 (or 2). Generated code knows
// every component's lanes; this is what hosts and tools that read a world's
// bytes go by.
static inline uint32_t tide_lane_width(const uint32_t component_size)
{
    return component_size % 4u == 0 ? 4u : component_size % 2u == 0 ? 2u : 1u;
}

// A row's component, from its `lanes` columns of `width` bytes starting at
// column `first`, into `value`; and to change, from `value` into them, their
// pages made this table's own.
void tide_table_gather(const tide_table *t, const tide_columns *c, uint32_t row, uint32_t first, uint32_t lanes, uint32_t width,
                       void *value);
void tide_table_scatter(tide_table *t, const tide_columns *c, uint32_t row, uint32_t first, uint32_t lanes, uint32_t width,
                        const void *value);

// A chunk's lanes of a component (columns `first` on, `lanes` of them), for a
// loop over its rows to change: where to write each lane's rows, into `out`.
// A page this table alone has is written in place. One its snapshots share is
// copied into memory of this thread's first, so that a loop which changes
// nothing leaves the page shared, with its hash good: tide_table_lanes_end
// puts each copy whose rows changed into a page of the table's own, and lets
// the rest go. Ends come in the reverse order of their begins, and
// tide_table_lanes_reserve comes before a chunk's begins with the most bytes
// they'll copy, so the memory never moves under them.
void tide_table_lanes_reserve(uint32_t bytes);
void tide_table_lanes_begin(tide_table *t, const tide_columns *c, uint32_t chunk, uint32_t first, uint32_t lanes, uint8_t **out);
void tide_table_lanes_end(tide_table *t, const tide_columns *c, uint32_t chunk, uint32_t first, uint32_t lanes,
                          uint8_t *const *out);

// The same to change: the page is made this table's own.
static inline void *tide_table_column_mut(tide_table *t, const tide_columns *c, const uint32_t chunk, const uint32_t column)
{
    tide_page **p = &t->pages[chunk * c->count + column];
    *p = tide_page_own(*p, 1, tide_table_rows(t, c->shift, chunk) * c->sizes[column]);
    return tide_page_data(*p);
}
void *tide_table_cell(tide_table *t, const tide_columns *c, uint32_t row, uint32_t column);

// A new row at the end, zeroed: its number.
uint32_t tide_table_add(tide_table *t, const tide_columns *c);

// Takes a row out, moving the last row into its place, and tells the entity
// table where the moved one went. Release its text and lists first.
void tide_table_remove(tide_table *t, const tide_columns *c, uint32_t row, tide_entities *entities, uint32_t archetype);

// Takes a row out of a table with no entities, like a world's tasks, moving
// the last row into its place.
void tide_table_remove_row(tide_table *t, const tide_columns *c, uint32_t row);

// Moves a row to another table, as an entity gains or loses a component:
// `map` gives, for each of `to`'s columns, `from`'s column with its values, or
// -1 to leave it zero. Returns its row in `to`, where the entity table finds it.
uint32_t tide_table_move(tide_table *from, const tide_columns *from_columns, uint32_t row, uint32_t from_archetype,
                         tide_table *to, const tide_columns *to_columns, uint32_t to_archetype, const int32_t *map,
                         tide_entities *entities);

// For snapshots: `to` becomes `from`, sharing its pages. The hash covers the
// rows in use, the same for the same rows however the table got there.
void tide_table_copy(tide_table *to, const tide_table *from, const tide_columns *c);
uint64_t tide_table_hash(uint64_t h, const tide_table *t, const tide_columns *c);

// Lets go of its pages, leaving it empty.
void tide_table_free(tide_table *t, const tide_columns *c);

// The table as bytes, for sending a world and carrying it over: its count,
// then each column's rows in use, one column after another. Reading them back
// into an empty table is false when the bytes aren't a table.
uint32_t tide_table_packed_size(const tide_table *t, const tide_columns *c);
void tide_table_pack(const tide_table *t, const tide_columns *c, tide_writer *w);
bool tide_table_unpack(tide_table *t, const tide_columns *c, tide_reader *r);

// The same as part of a delta (tide/delta.h), from `base` (NULL for none):
// its count, then each chunk's columns as regions. Unpacking `t` (empty)
// shares the pages that are the same as the base's.
void tide_table_pack_delta(const tide_table *t, const tide_table *base, const tide_columns *c, tide_delta_writer *d);
bool tide_table_unpack_delta(tide_table *t, const tide_table *base, const tide_columns *c, tide_delta_reader *d);
// Its pages' hashes, and which of them `base` lacks (tide_needs).
void tide_table_hash_pages(const tide_table *t, const tide_columns *c, tide_writer *w);
void tide_table_need_pages(const tide_table *base, const tide_columns *c, tide_needs *n);

// ---------------------------------------------------------------------------
// The queue of structural changes and events a tick or frame records, applied
// at its end. Items stay where they are as more are added, so the one being
// applied can be read while what it does adds more. Snapshots copy it: it's
// empty between ticks but for the players joining and leaving.
//
// An item takes the bytes it's pushed with, so an event of a few bytes costs
// those, whatever the biggest change a game records is. Each starts with its
// size, a uint32_t that pushing sets, which is how the queue goes from one to
// the next: its struct's first member. Sizes are multiples of 8, as items are
// aligned to 8.
//
// Its pages hold whole items, back to back. A task's queue gives the world's
// its pages rather than copies of its items (tide_queue_append).

#define TIDE_QUEUE_PAGE 4080u // Bytes of items in each of its pages: with its header, just under 4 KiB

typedef struct tide_queue_page {
    uint32_t used; // Bytes of items in it
    uint32_t room; // Bytes of items it holds: TIDE_QUEUE_PAGE, or the size of an item bigger than that
} tide_queue_page; // ...and then its items

typedef struct tide_queue {
    uint32_t count; // Items
    uint32_t pages; // Pages with items in them, in order: pushing fills the last
    uint32_t kept;  // Pages it has, those after `pages` empty: clearing keeps the ones it used
    tide_queue_page **page;
} tide_queue;

// A new item at the end, zeroed, on another page: `size` is a multiple of 8.
void *tide_queue_push_page(tide_queue *q, uint32_t size);

// A new item of `size` bytes at the end, zeroed but for its size.
static inline void *tide_queue_push(tide_queue *q, uint32_t size)
{
    size = size < 8u ? 8u : (size + 7u) & ~7u;
    tide_queue_page *last = q->pages ? q->page[q->pages - 1u] : NULL;
    if (!last || last->room - last->used < size) return tide_queue_push_page(q, size);
    void *item = (uint8_t *)(last + 1) + last->used;
    last->used += size;
    q->count++;
    memset(item, 0, size);
    memcpy(item, &size, sizeof size);
    return item;
}

// Up to *count consecutive zeroed items of one size, on the last page.
// Returns their first item and the number it pushed. The caller fills them
// before pushing anything else; their ordinary size headers are already set.
static inline void *tide_queue_push_run(tide_queue *q, uint32_t size, uint32_t *count)
{
    if (!*count) return NULL;
    size = size < 8u ? 8u : (size + 7u) & ~7u;
    tide_queue_page *last = q->pages ? q->page[q->pages - 1u] : NULL;
    uint32_t n = last ? (last->room - last->used) / size : 0u;
    if (!n) {
        *count = 1u;
        return tide_queue_push_page(q, size);
    }
    if (n > *count) n = *count;
    if (n > UINT32_MAX - q->count) tide_out_of_memory();
    uint8_t *items = (uint8_t *)(last + 1) + last->used;
    last->used += n * size;
    q->count += n;
    memset(items, 0, n * size);
    for (uint32_t i = 0; i < n; i++) memcpy(items + i * size, &size, sizeof size);
    *count = n;
    return items;
}

// A place in a queue: an item's, or the end's.
typedef struct tide_queue_place {
    uint32_t page;
    uint32_t offset;
} tide_queue_place;

// Where the next item pushed will be found from: tide_queue_next from here
// gives what's pushed after this.
static inline tide_queue_place tide_queue_end(const tide_queue *q)
{
    tide_queue_place end = {0u, 0u};
    if (q->pages) {
        end.page = q->pages - 1u;
        end.offset = q->page[q->pages - 1u]->used;
    }
    return end;
}

// The item at `at`, which moves on to the one after it, or NULL at the end:
// `at` stays there, so asking again once more is pushed gives that. Start from
// a zeroed place for the first item.
static inline void *tide_queue_next(const tide_queue *q, tide_queue_place *at)
{
    while (at->page < q->pages) {
        tide_queue_page *page = q->page[at->page];
        if (at->offset < page->used) {
            void *item = (uint8_t *)(page + 1) + at->offset;
            uint32_t size;
            memcpy(&size, item, sizeof size);
            at->offset += size;
            return item;
        }
        if (at->page + 1u == q->pages) return NULL;
        at->page++;
        at->offset = 0;
    }
    return NULL;
}

// Empties it. It keeps the pages it used, for the next tick, and lets go of
// the rest.
void tide_queue_clear(tide_queue *q);

void tide_queue_copy(tide_queue *to, const tide_queue *from);

// Puts `from`'s items at the end of `to`, in order, and leaves `from` empty.
// A page with more than a few items goes to `to` as it is, for one of the
// empty pages `to` kept, so that nothing is copied and, once both have had a
// tick like this one, nothing is allocated either.
void tide_queue_append(tide_queue *to, tide_queue *from);
// Of its items, whatever pages they're in.
uint64_t tide_queue_hash(uint64_t h, const tide_queue *q);
void tide_queue_free(tide_queue *q);

uint32_t tide_queue_packed_size(const tide_queue *q);
void tide_queue_pack(const tide_queue *q, tide_writer *w);
// The bytes may come from another machine: `fits` says whether an item is as
// big as what it says it is needs, and one that isn't fails the unpacking.
bool tide_queue_unpack(tide_queue *q, tide_reader *r, bool (*fits)(const void *item));
