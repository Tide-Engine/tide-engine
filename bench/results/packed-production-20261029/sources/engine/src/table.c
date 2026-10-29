#include "tide/table.h"

#include <stdlib.h>
#include <string.h>

#if TIDE_PACKED_TABLES
typedef struct tide_table_cache {
    tide_table source, logical;
} tide_table_cache;
static void cache_free(tide_table *t, const tide_columns *c)
{
    if (!t->canonical) return;
    tide_table_free(&t->canonical->source, c);
    tide_table_free(&t->canonical->logical, c);
    free(t->canonical);
    t->canonical = NULL;
}

static const uint32_t mapping_sizes[] = {4u, 4u};
static const tide_columns mapping_columns = {2u, 11u, mapping_sizes};

#endif

#define FIRST_ROOM 4u // Rows the first chunk starts with room for

static uint32_t full_room(const tide_columns *c)
{
    return 1u << c->shift;
}

static void make_room(tide_table *t, const tide_columns *c, const uint32_t chunks)
{
    if (chunks <= t->room) return;
    uint32_t room = t->room ? t->room * 2u : 4u;
    while (room < chunks) room *= 2u;
    t->pages = tide_realloc(t->pages, (size_t)t->room * c->count * sizeof *t->pages, (size_t)room * c->count * sizeof *t->pages);
    t->room = room;
}

// A byte count that has to fit in 32 bits: a world's packed size, and what's in a page.
static uint32_t bytes_of(const uint64_t n)
{
    if (n > UINT32_MAX) tide_out_of_memory();
    return (uint32_t)n;
}

void *tide_table_cell(tide_table *t, const tide_columns *c, const uint32_t row, const uint32_t column)
{
    const uint32_t physical = tide_table_physical_row(t, row);
    uint8_t *rows = tide_table_column_mut(t, c, physical >> c->shift, column);
    return rows + (size_t)(physical & (full_room(c) - 1u)) * c->sizes[column];
}

void tide_table_gather(const tide_table *t, const tide_columns *c, const uint32_t row, const uint32_t first, const uint32_t lanes,
                       const uint32_t width, void *value)
{
    const uint32_t physical = tide_table_physical_row(t, row);
    const uint32_t chunk = physical >> c->shift;
    const size_t at = (size_t)(physical & (full_room(c) - 1u)) * width;
    uint8_t *out = value;
    for (uint32_t k = 0; k < lanes; k++) {
        memcpy(out + (size_t)k * width, (const uint8_t *)tide_table_column(t, c, chunk, first + k) + at, width);
    }
}

void tide_table_scatter(tide_table *t, const tide_columns *c, const uint32_t row, const uint32_t first, const uint32_t lanes,
                        const uint32_t width, const void *value)
{
    const uint32_t physical = tide_table_physical_row(t, row);
    const uint32_t chunk = physical >> c->shift;
    const size_t at = (size_t)(physical & (full_room(c) - 1u)) * width;
    const uint8_t *in = value;
    for (uint32_t k = 0; k < lanes; k++) {
        // A lane that's that already stays as it is: its page isn't made the
        // table's own for it, nor hashed again, and code that puts back a
        // component it changed a field of writes that field's lanes
        const uint8_t *now = (const uint8_t *)tide_table_column(t, c, chunk, first + k) + at;
        const uint8_t *next = in + (size_t)k * width;
        if (width == 4 ? memcmp(now, next, 4) == 0 : memcmp(now, next, width) == 0) continue;
        memcpy((uint8_t *)tide_table_column_mut(t, c, chunk, first + k) + at, next, width);
    }
}

// In pieces: the page a lane is written into is in the table from the start,
// where every piece finds it, and says which page it's read from (tide_page.was).

void tide_table_lanes_begin_pieces(tide_table *t, const tide_columns *c, const uint32_t chunk, const uint32_t first,
                                   const uint32_t lanes, const bool apart)
{
    const uint32_t rows = tide_table_rows(t, c->shift, chunk);
    for (uint32_t k = 0; k < lanes; k++) {
        tide_page **p = &t->pages[chunk * c->count + first + k];
        if (!apart) { // The table's alone, or else a copy that is
            *p = tide_page_own(*p, 1, rows * c->sizes[first + k]);
            continue;
        }
        tide_page *own = tide_page_take((*p)->size);
        *own = (tide_page){1, (*p)->size, (*p)->first, UINT32_MAX, 0, (uint64_t)(uintptr_t)*p};
        *p = own; // Its reference to the page it was goes with `was`
    }
}

bool tide_table_lanes_piece(const tide_table *t, const tide_columns *c, const uint32_t chunk, const uint32_t first,
                            const uint32_t lanes, const uint8_t **in, uint8_t **out)
{
    bool apart = false;
    for (uint32_t k = 0; k < lanes; k++) {
        const tide_page *p = t->pages[chunk * c->count + first + k];
        out[k] = tide_page_data(p);
        in[k] = out[k];
        if (!p->was) continue;
        in[k] = tide_page_data((const tide_page *)(uintptr_t)p->was);
        apart = true;
    }
    return apart;
}

void tide_table_lanes_end_pieces(tide_table *t, const tide_columns *c, const uint32_t chunk, const uint32_t first,
                                 const uint32_t lanes)
{
    const uint32_t rows = tide_table_rows(t, c->shift, chunk);
    for (uint32_t k = lanes; k-- > 0;) {
        tide_page **p = &t->pages[chunk * c->count + first + k];
        tide_page *own = *p;
        if (!own->was) continue; // Written in place
        tide_page *was = (tide_page *)(uintptr_t)own->was;
        own->was = 0;
        const size_t bytes = (size_t)rows * c->sizes[first + k];
        if (tide_same_bytes(tide_page_data(own), tide_page_data(was), bytes)) { // No row changed: still shared, still hashed
            *p = was;
            tide_page_give(own);
            continue;
        }
        memset((uint8_t *)tide_page_data(own) + bytes, 0, own->size - bytes); // Past its rows a page is zeros
        if (was->refs == 1u) tide_page_give(was);
        else was->refs--;
    }
}

uint32_t tide_table_add(tide_table *t, const tide_columns *c)
{
    if (t->count == UINT32_MAX) tide_out_of_memory();
    const uint32_t row = t->count;
    const uint32_t chunk = row >> c->shift;
    const uint32_t at = row & (full_room(c) - 1u);
    if (chunk == t->chunks) { // A new chunk: the first starts small
        make_room(t, c, chunk + 1u);
        const uint32_t room = chunk == 0 && FIRST_ROOM < full_room(c) ? FIRST_ROOM : full_room(c);
        for (uint32_t k = 0; k < c->count; k++) t->pages[chunk * c->count + k] = tide_page_new(bytes_of((uint64_t)room * c->sizes[k]));
        t->chunks++;
        t->last_room = room;
    } else if (at == t->last_room) { // The first chunk, full but not as big as the others: twice the room
        const uint32_t room = t->last_room * 2u;
        for (uint32_t k = 0; k < c->count; k++) {
            tide_page *old = t->pages[chunk * c->count + k];
            tide_page *grown = tide_page_new(bytes_of((uint64_t)room * c->sizes[k]));
            memcpy(tide_page_data(grown), tide_page_data(old), (size_t)at * c->sizes[k]);
            tide_page_release(old, 1);
            t->pages[chunk * c->count + k] = grown;
        }
        t->last_room = room;
    }
#if TIDE_PACKED_TABLES
    if (t->mapping) {
        const uint32_t added = tide_table_add(t->mapping, &mapping_columns);
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, added, 0u) = added;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, added, 1u) = added;
    }
#endif
    t->count++;
    for (uint32_t k = 0; k < c->count; k++) memset(tide_table_cell(t, c, row, k), 0, c->sizes[k]);
    return row;
}

#if TIDE_PACKED_TABLES
static void remove_physical(tide_table *t, const tide_columns *c, uint32_t logical)
{
    const uint32_t last = t->count - 1u;
    const uint32_t physical = tide_table_physical_row(t, logical);
    if (physical != last) {
        for (uint32_t k = 0; k < c->count; k++) {
            uint8_t *gap = tide_table_column_mut(t, c, physical >> c->shift, k);
            const uint8_t *tail = tide_table_column(t, c, last >> c->shift, k);
            memcpy(gap + (size_t)(physical & (full_room(c) - 1u)) * c->sizes[k], tail + (size_t)(last & (full_room(c) - 1u)) * c->sizes[k],
                   c->sizes[k]);
        }
    }
    if (t->mapping) {
        const uint32_t tail_logical = tide_table_logical_row(t, last);
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, tail_logical, 0u) = physical;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, physical, 1u) = tail_logical;
        if (logical != last) {
            const uint32_t last_physical = tide_table_physical_row(t, last);
            *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, logical, 0u) = last_physical;
            *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, last_physical, 1u) = logical;
        }
        tide_table_remove_row(t->mapping, &mapping_columns, last);
    }
    t->count = last;
    if (t->count == (t->chunks - 1u) << c->shift) {
        t->chunks--;
        for (uint32_t k = 0; k < c->count; k++) tide_page_release(t->pages[t->chunks * c->count + k], 1u);
        t->last_room = t->chunks ? full_room(c) : 0u;
    }
}

void tide_table_remove(tide_table *t, const tide_columns *c, uint32_t row, tide_entities *entities, uint32_t archetype)
{
    tide_entity moved = {0};
    const bool moves = row != t->count - 1u;
    if (moves) memcpy(&moved, tide_table_get(t, c, t->count - 1u, 0u), sizeof moved);
    remove_physical(t, c, row);
    if (moves) tide_entity_set_location(entities, moved, (tide_location){archetype, row});
}

void tide_table_remove_row(tide_table *t, const tide_columns *c, uint32_t row) { remove_physical(t, c, row); }

#else
void tide_table_remove(tide_table *t, const tide_columns *c, const uint32_t row, tide_entities *entities,
                       const uint32_t archetype)
{
    const uint32_t last = t->count - 1u;
    if (row != last) {
        for (uint32_t k = 0; k < c->count; k++) {
            void *gap = tide_table_cell(t, c, row, k);
            memcpy(gap, tide_table_get(t, c, last, k), c->sizes[k]);
        }
        tide_entity moved;
        memcpy(&moved, tide_table_get(t, c, row, 0), sizeof moved);
        tide_entity_set_location(entities, moved, (tide_location){archetype, row});
    }
    t->count = last;
    // A last chunk left empty goes
    if (t->count == (t->chunks - 1u) << c->shift) {
        t->chunks--;
        for (uint32_t k = 0; k < c->count; k++) tide_page_release(t->pages[t->chunks * c->count + k], 1);
        t->last_room = t->chunks ? full_room(c) : 0u;
    }
}

void tide_table_remove_row(tide_table *t, const tide_columns *c, const uint32_t row)
{
    const uint32_t last = t->count - 1u;
    if (row != last) {
        for (uint32_t k = 0; k < c->count; k++) memcpy(tide_table_cell(t, c, row, k), tide_table_get(t, c, last, k), c->sizes[k]);
    }
    t->count = last;
    // A last chunk left empty goes
    if (t->count == (t->chunks - 1u) << c->shift) {
        t->chunks--;
        for (uint32_t k = 0; k < c->count; k++) tide_page_release(t->pages[t->chunks * c->count + k], 1);
        t->last_room = t->chunks ? full_room(c) : 0u;
    }
}

#endif

uint32_t tide_table_move(tide_table *from, const tide_columns *from_columns, const uint32_t row,
                         const uint32_t from_archetype, tide_table *to, const tide_columns *to_columns,
                         const uint32_t to_archetype, const int32_t *map, tide_entities *entities)
{
    const uint32_t dst = tide_table_add(to, to_columns);
    for (uint32_t k = 0; k < to_columns->count; k++) {
        if (map[k] < 0) continue;
        memcpy(tide_table_cell(to, to_columns, dst, k), tide_table_get(from, from_columns, row, (uint32_t)map[k]),
               to_columns->sizes[k]);
    }
    tide_entity e;
    memcpy(&e, tide_table_get(from, from_columns, row, 0), sizeof e);
    tide_entity_set_location(entities, e, (tide_location){to_archetype, dst});
    tide_table_remove(from, from_columns, row, entities, from_archetype);
    return dst;
}

void tide_table_copy(tide_table *to, const tide_table *from, const tide_columns *c)
{
    if (to == from) return;
#if TIDE_PACKED_TABLES
    if (from->mapping) {
        if (!to->mapping) to->mapping = tide_alloc_zeroed(1u, sizeof *to->mapping);
        tide_table_copy(to->mapping, from->mapping, &mapping_columns);
    } else if (to->mapping) {
        tide_table_free(to->mapping, &mapping_columns);
        free(to->mapping);
        to->mapping = NULL;
    }
    cache_free(to, c);
#endif
    // Its pages first, then letting go of to's: they can be the same ones.
    const uint32_t pages = from->chunks * c->count;
    for (uint32_t i = 0; i < pages; i++) tide_page_retain(from->pages[i]);
    for (uint32_t i = 0; i < to->chunks * c->count; i++) tide_page_release(to->pages[i], 1);
    make_room(to, c, from->chunks);
    if (pages) memcpy(to->pages, from->pages, pages * sizeof *to->pages);
    to->count = from->count;
    to->chunks = from->chunks;
    to->last_room = from->last_room;
}

#if TIDE_PACKED_TABLES
static void canonical_copy(tide_table *to, const tide_table *t, const tide_columns *c)
{
    make_room(to, c, t->chunks);
    to->count = t->count;
    to->chunks = t->chunks;
    to->last_room = t->last_room;
    for (uint32_t chunk = 0; chunk < t->chunks; chunk++) {
        const uint32_t rows = tide_table_rows(t, c->shift, chunk);
        for (uint32_t k = 0; k < c->count; k++) {
            tide_page *page = tide_page_new(t->pages[(size_t)chunk * c->count + k]->size);
            to->pages[(size_t)chunk * c->count + k] = page;
            uint8_t *dst = tide_page_data(page);
            memcpy(dst, tide_table_column(t, c, chunk, k), (size_t)rows * c->sizes[k]);
            for (uint32_t i = 0; i < rows; i++) {
                const uint32_t logical = (chunk << c->shift) + i;
                if (tide_table_physical_row(t, logical) == logical) continue;
                memcpy(dst + (size_t)i * c->sizes[k], tide_table_get(t, c, logical, k), c->sizes[k]);
            }
        }
    }
}

static const tide_table *canonical_view(const tide_table *table, const tide_columns *c)
{
    if (!table->mapping) return table;
    // As with a page's memoized hash, derived bytes change during serial hashing/packing only.
    tide_table *t = (tide_table *)table;
    if (!t->canonical) t->canonical = tide_alloc_zeroed(1u, sizeof *t->canonical);
    tide_table_cache *cache = t->canonical;
    const tide_table *old = &cache->source;
    bool same = old->count == t->count && old->mapping && old->mapping->chunks == t->mapping->chunks;
    for (uint32_t i = 0; same && i < t->mapping->chunks * 2u; i++) same = old->mapping->pages[i] == t->mapping->pages[i];
    if (!same) {
        tide_table_free(&cache->logical, c);
        canonical_copy(&cache->logical, t, c);
    } else {
        for (uint32_t chunk = 0; chunk < t->chunks; chunk++)
            for (uint32_t k = 0; k < c->count; k++) {
                if (old->pages[(size_t)chunk * c->count + k] == t->pages[(size_t)chunk * c->count + k]) continue;
                const uint8_t *src = tide_table_column(t, c, chunk, k);
                const uint32_t rows = tide_table_rows(t, c->shift, chunk);
                for (uint32_t i = 0; i < rows; i++) {
                    const uint32_t logical = tide_table_logical_row(t, (chunk << c->shift) + i);
                    const uint8_t *value = src + (size_t)i * c->sizes[k];
                    const uint32_t lc = logical >> c->shift, li = logical & ((1u << c->shift) - 1u);
                    if (c->sizes[k] == 4u) {
                        uint32_t next, was;
                        memcpy(&next, value, 4u);
                        memcpy(&was, (const uint8_t *)tide_table_column(&cache->logical, c, lc, k) + (size_t)li * 4u, 4u);
                        if (next == was) continue;
                        memcpy((uint8_t *)tide_table_column_mut(&cache->logical, c, lc, k) + (size_t)li * 4u, &next, 4u);
                    } else {
                        if (memcmp(tide_table_get(&cache->logical, c, logical, k), value, c->sizes[k]) == 0) continue;
                        memcpy(tide_table_cell(&cache->logical, c, logical, k), value, c->sizes[k]);
                    }
                }
            }
    }
    tide_table_copy(&cache->source, t, c);
    return &cache->logical;
}

#endif

uint64_t tide_table_hash(uint64_t h, const tide_table *t, const tide_columns *c)
{
#if TIDE_PACKED_TABLES
    if (t->mapping) {
        const tide_table *logical=canonical_view(t,c);
        return tide_table_hash(h, logical, c);
    }
#endif

    h = tide_hash_more(h, &t->count, sizeof t->count);
    for (uint32_t chunk = 0; chunk < t->chunks; chunk++) {
        const uint32_t rows = tide_table_rows(t, c->shift, chunk);
        for (uint32_t k = 0; k < c->count; k++) {
            const uint64_t page = tide_page_hash(t->pages[chunk * c->count + k], rows * c->sizes[k]);
            h = tide_hash_more(h, &page, sizeof page);
        }
    }
    return h;
}

void tide_table_stale(const tide_table *t, const tide_columns *c, tide_stale_pages *s)
{
#if TIDE_PACKED_TABLES
    if (t->mapping) {
        tide_table_stale(canonical_view(t, c), c, s);
        return;
    }
#endif

    for (uint32_t chunk = 0; chunk < t->chunks; chunk++) {
        const uint32_t rows = tide_table_rows(t, c->shift, chunk);
        for (uint32_t k = 0; k < c->count; k++) tide_page_stale(s, t->pages[chunk * c->count + k], rows * c->sizes[k]);
    }
}

void tide_table_free(tide_table *t, const tide_columns *c)
{
    for (uint32_t i = 0; i < t->chunks * c->count; i++) tide_page_release(t->pages[i], 1);
#if TIDE_PACKED_TABLES
    cache_free(t, c);
    if (t->mapping) { tide_table_free(t->mapping, &mapping_columns); free(t->mapping); }
#endif
    free(t->pages);
    memset(t, 0, sizeof *t);
}

uint32_t tide_table_packed_size(const tide_table *t, const tide_columns *c)
{
    uint64_t row = 0;
    for (uint32_t k = 0; k < c->count; k++) row += c->sizes[k];
    return bytes_of(4u + row * t->count);
}

void tide_table_pack(const tide_table *t, const tide_columns *c, tide_writer *w)
{
#if TIDE_PACKED_TABLES
    if (t->mapping) {
        const tide_table *logical=canonical_view(t,c);
        tide_table_pack(logical, c, w);
        return;
    }
#endif

    tide_write_u32(w, t->count);
    for (uint32_t k = 0; k < c->count; k++) {
        for (uint32_t chunk = 0; chunk < t->chunks; chunk++) {
            tide_write_bytes(w, tide_table_column(t, c, chunk, k), tide_table_rows(t, c->shift, chunk) * c->sizes[k]);
        }
    }
}

// The room `count` rows' chunks have, as adding them one by one would have
// made: all but the first, alone, have room for 1 << shift.
static uint32_t room_for(const tide_columns *c, const uint32_t count)
{
    if (count > full_room(c)) return full_room(c);
    uint32_t room = FIRST_ROOM < full_room(c) ? FIRST_ROOM : full_room(c);
    while (room < count) room *= 2u;
    return room;
}

bool tide_table_unpack(tide_table *t, const tide_columns *c, tide_reader *r)
{
    const uint32_t count = tide_read_u32(r);
    uint64_t row = 0;
    for (uint32_t k = 0; k < c->count; k++) row += c->sizes[k];
    if (r->failed || (uint64_t)count * row > r->size - r->at) return false;
    const uint32_t chunks = (uint32_t)(((uint64_t)count + full_room(c) - 1u) >> c->shift);
    make_room(t, c, chunks);
    const uint32_t room = room_for(c, count);
    for (uint32_t chunk = 0; chunk < chunks; chunk++) {
        for (uint32_t k = 0; k < c->count; k++) {
            t->pages[chunk * c->count + k] = tide_page_new(bytes_of((uint64_t)room * c->sizes[k]));
        }
    }
    t->count = count;
    t->chunks = chunks;
    t->last_room = chunks ? room : 0u;
    for (uint32_t k = 0; k < c->count; k++) {
        for (uint32_t chunk = 0; chunk < chunks; chunk++) {
            const uint32_t bytes = tide_table_rows(t, c->shift, chunk) * c->sizes[k];
            memcpy(tide_page_data(t->pages[chunk * c->count + k]), tide_read_bytes(r, bytes), bytes);
        }
    }
    return !r->failed;
}

// Packed as part of a delta: its count, then each chunk's columns as regions,
// in the order of `pages`.

void tide_table_pack_delta(const tide_table *t, const tide_table *base, const tide_columns *c, tide_delta_writer *d)
{
#if TIDE_PACKED_TABLES
    if (t->mapping || (base && base->mapping)) {
        const tide_table *logical=canonical_view(t,c);
        tide_table_pack_delta(logical, base ? canonical_view(base, c) : NULL, c, d);
        return;
    }
#endif

    tide_delta_number(d, t->count);
    for (uint32_t chunk = 0; chunk < t->chunks; chunk++) {
        const bool based = base && chunk < base->chunks;
        for (uint32_t k = 0; k < c->count; k++) {
            const tide_page *p = t->pages[chunk * c->count + k];
            const tide_page *b = based ? base->pages[chunk * c->count + k] : NULL;
            tide_delta_region(d, tide_page_data(p), tide_table_rows(t, c->shift, chunk) * c->sizes[k],
                              b ? tide_page_data(b) : NULL, b ? tide_table_rows(base, c->shift, chunk) * c->sizes[k] : 0u,
                              b == p, c->sizes[k]);
        }
    }
    tide_delta_close(d);
}

bool tide_table_unpack_delta(tide_table *t, const tide_table *base, const tide_columns *c, tide_delta_reader *d)
{
#if TIDE_PACKED_TABLES
    if (base && base->mapping) {
        return tide_table_unpack_delta(t, canonical_view(base, c), c, d);
    }
#endif

    const uint32_t count = tide_delta_get_number(d);
    if (d->bytes.failed) return false;
    // Each page is the base's, or bytes of the delta
    const uint32_t chunks = (uint32_t)(((uint64_t)count + full_room(c) - 1u) >> c->shift);
    const uint64_t pages = (uint64_t)chunks * c->count;
    if (pages > (uint64_t)(base ? base->chunks * c->count : 0u) + (d->bytes.size - d->bytes.at)) return false;
    make_room(t, c, chunks);
    const uint32_t room = room_for(c, count);
    t->count = count; // For tide_table_rows: its chunks follow
    t->last_room = chunks ? room : 0u;
    for (uint32_t chunk = 0; chunk < chunks; chunk++) {
        const bool based = base && chunk < base->chunks;
        for (uint32_t k = 0; k < c->count; k++) {
            tide_page *b = based ? base->pages[chunk * c->count + k] : NULL;
            const uint32_t page_size = bytes_of((uint64_t)room * c->sizes[k]);
            tide_page *p = tide_delta_page(d, tide_table_rows(t, c->shift, chunk) * c->sizes[k], page_size, 1, b,
                                           b ? tide_table_rows(base, c->shift, chunk) * c->sizes[k] : 0u,
                                           b && b->size == page_size);
            if (!p) { // Its chunks so far stay, for tide_table_free
                for (uint32_t m = 0; m < k; m++) tide_page_release(t->pages[chunk * c->count + m], 1);
                return false;
            }
            t->pages[chunk * c->count + k] = p;
        }
        t->chunks = chunk + 1u;
    }
    return tide_delta_closed(d);
}

void tide_table_hash_pages(const tide_table *t, const tide_columns *c, tide_writer *w)
{
#if TIDE_PACKED_TABLES
    if (t->mapping) {
        const tide_table *logical=canonical_view(t,c);
        tide_table_hash_pages(logical, c, w);
        return;
    }
#endif

    tide_write_varint(w, t->chunks * c->count);
    for (uint32_t chunk = 0; chunk < t->chunks; chunk++) {
        const uint32_t rows = tide_table_rows(t, c->shift, chunk);
        for (uint32_t k = 0; k < c->count; k++) {
            tide_write_u64(w, tide_page_hash(t->pages[chunk * c->count + k], rows * c->sizes[k]));
        }
    }
}

void tide_table_need_pages(const tide_table *base, const tide_columns *c, tide_needs *n)
{
#if TIDE_PACKED_TABLES
    if (base && base->mapping) {
        tide_table_need_pages(canonical_view(base, c), c, n);
        return;
    }
#endif

    const uint32_t count = tide_needs_count(n);
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t chunk = i / c->count;
        const uint32_t k = i % c->count;
        const bool have = chunk < base->chunks;
        const uint32_t bytes = have ? tide_table_rows(base, c->shift, chunk) * c->sizes[k] : 0u;
        tide_needs_put(n, have, have ? tide_page_hash(base->pages[chunk * c->count + k], bytes) : 0u);
    }
}

// ---------------------------------------------------------------------------
// The queue

// Room in its table of pages for one more: the table has room for as many
// pages as the next power of two, and grows to twice that when it's full.
static void queue_room(tide_queue *q)
{
    if (q->kept == 0 || (q->kept & (q->kept - 1u)) == 0) {
        q->page = tide_realloc(q->page, q->kept * sizeof *q->page, (q->kept ? 2u * q->kept : 1u) * sizeof *q->page);
    }
}

static uint8_t *queue_items(tide_queue_page *page)
{
    return (uint8_t *)(page + 1);
}

void *tide_queue_push_page(tide_queue *q, const uint32_t size)
{
    if (q->count == UINT32_MAX) tide_out_of_memory(); // More items than it counts
    tide_queue_page *page = q->pages < q->kept ? q->page[q->pages] : NULL;
    if (!page || page->room < size) {
        // A page of its own for an item bigger than a page. It goes before
        // the empty ones it kept.
        const uint32_t room = size > TIDE_QUEUE_PAGE ? size : TIDE_QUEUE_PAGE;
        page = tide_alloc(sizeof *page + room);
        page->room = room;
        queue_room(q);
        memmove(&q->page[q->pages + 1u], &q->page[q->pages], (q->kept - q->pages) * sizeof *q->page);
        q->page[q->pages] = page;
        q->kept++;
    }
    page->used = size;
    q->pages++;
    q->count++;
    void *item = queue_items(page);
    memset(item, 0, size);
    memcpy(item, &size, sizeof size);
    return item;
}

void tide_queue_clear(tide_queue *q)
{
    // It keeps the pages this tick used, for the next, and lets go of the
    // rest: what a burst of changes needed goes once it's over.
    const uint32_t keep = q->pages ? q->pages : 1u;
    for (uint32_t p = keep; p < q->kept; p++) free(q->page[p]);
    if (q->kept > keep) q->kept = keep;
    for (uint32_t p = 0; p < q->pages; p++) q->page[p]->used = 0;
    q->pages = 0;
    q->count = 0;
}

// Pages with this little in them are copied onto the end of `to`'s last
// rather than given to it: a task that recorded a change or two keeps its
// page, and `to` doesn't fill up with pages that are nearly empty.
#define QUEUE_COPY_MOST 512u

void tide_queue_append(tide_queue *to, tide_queue *from)
{
    for (uint32_t p = 0; p < from->pages; p++) {
        tide_queue_page *page = from->page[p];
        tide_queue_page *last = to->pages ? to->page[to->pages - 1u] : NULL;
        if (page->used <= QUEUE_COPY_MOST && last && last->room - last->used >= page->used) {
            memcpy(queue_items(last) + last->used, queue_items(page), page->used); // Whole items, so they stay whole
            last->used += page->used;
            page->used = 0;
            continue;
        }
        // The page itself, for an empty one of `to`'s if it has one
        tide_queue_page *spare = NULL;
        if (to->pages < to->kept) {
            spare = to->page[to->pages];
        } else {
            queue_room(to);
            to->kept++;
        }
        to->page[to->pages++] = page;
        from->page[p] = spare;
    }
    to->count += from->count;
    // What `from` has left: the pages it kept and the ones it got back, as
    // many as it used this time (as clearing keeps them)
    const uint32_t keep = from->pages ? from->pages : 1u;
    uint32_t kept = 0;
    for (uint32_t p = 0; p < from->kept; p++) {
        if (!from->page[p]) continue;
        if (kept < keep) from->page[kept++] = from->page[p];
        else free(from->page[p]);
    }
    from->kept = kept;
    from->pages = 0;
    from->count = 0;
}

void tide_queue_copy(tide_queue *to, const tide_queue *from)
{
    if (to == from) return;
    tide_queue_clear(to);
    tide_queue_place at = {0, 0};
    for (const uint8_t *item; (item = tide_queue_next(from, &at));) {
        uint32_t size;
        memcpy(&size, item, sizeof size);
        memcpy(tide_queue_push(to, size), item, size);
    }
}

uint64_t tide_queue_hash(uint64_t h, const tide_queue *q)
{
    // Item by item: the same whatever pages they're in, which depends on how
    // they got there
    h = tide_hash_more(h, &q->count, sizeof q->count);
    tide_queue_place at = {0, 0};
    for (const uint8_t *item; (item = tide_queue_next(q, &at));) {
        uint32_t size;
        memcpy(&size, item, sizeof size);
        h = tide_hash_more(h, item, size);
    }
    return h;
}

void tide_queue_free(tide_queue *q)
{
    for (uint32_t p = 0; p < q->kept; p++) free(q->page[p]);
    free(q->page);
    memset(q, 0, sizeof *q);
}

uint32_t tide_queue_packed_size(const tide_queue *q)
{
    uint64_t size = 4u;
    for (uint32_t p = 0; p < q->pages; p++) size += q->page[p]->used;
    return bytes_of(size);
}

void tide_queue_pack(const tide_queue *q, tide_writer *w)
{
    tide_write_u32(w, q->count);
    for (uint32_t p = 0; p < q->pages; p++) tide_write_bytes(w, queue_items(q->page[p]), q->page[p]->used);
}

bool tide_queue_unpack(tide_queue *q, tide_reader *r, bool (*fits)(const void *item))
{
    const uint32_t count = tide_read_u32(r);
    for (uint32_t i = 0; i < count; i++) {
        // Each says its size first: a multiple of 8, and no more than what's left
        const uint8_t *head = tide_read_bytes(r, 4u);
        if (r->failed) return false;
        uint32_t size;
        memcpy(&size, head, sizeof size);
        if (size < 8u || (size & 7u) || size - 4u > r->size - r->at) return false;
        uint8_t *item = tide_queue_push(q, size);
        memcpy(item + 4u, tide_read_bytes(r, size - 4u), size - 4u);
        if (r->failed || (fits && !fits(item))) return false;
    }
    return !r->failed;
}

#if TIDE_PACKED_TABLES
uint32_t tide_table_partition(tide_table *t, const tide_columns *c, uint32_t presence, bool need)
{
    if (presence >= c->count || c->sizes[presence] != 1u) return 0u;
    if (!t->count) return 0u;
    uint32_t lo = 0u, hi = t->count;
    // No mapping/allocation for a table already in the requested order.
    while (lo < hi) {
        const uint8_t *v = tide_table_column(t, c, lo >> c->shift, presence);
        if ((v[lo & (full_room(c) - 1u)] != 0u) != need) break;
        lo++;
    }
    while (lo < hi) {
        const uint32_t at = hi - 1u;
        const uint8_t *v = tide_table_column(t, c, at >> c->shift, presence);
        if ((v[at & (full_room(c) - 1u)] != 0u) == need) break;
        hi--;
    }
    if (lo == hi) return lo;
    if (!t->mapping) {
        t->mapping = tide_alloc_zeroed(1u, sizeof *t->mapping);
        for (uint32_t r = 0; r < t->count; r++) {
            tide_table_add(t->mapping, &mapping_columns);
            *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, r, 0u) = r;
            *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, r, 1u) = r;
        }
    }
    uint32_t width = 1u;
    for (uint32_t k = 0; k < c->count; k++)
        if (c->sizes[k] > width) width = c->sizes[k];
    uint8_t *scratch = tide_alloc(width);
    while (lo < hi) {
        const uint32_t a = lo, b = hi - 1u;
        for (uint32_t k = 0; k < c->count; k++) {
            uint8_t *x = tide_table_column_mut(t, c, a >> c->shift, k);
            uint8_t *y = tide_table_column_mut(t, c, b >> c->shift, k);
            x += (size_t)(a & (full_room(c) - 1u)) * c->sizes[k];
            y += (size_t)(b & (full_room(c) - 1u)) * c->sizes[k];
            memcpy(scratch, x, c->sizes[k]);
            memcpy(x, y, c->sizes[k]);
            memcpy(y, scratch, c->sizes[k]);
        }
        const uint32_t la = tide_table_logical_row(t, a), lb = tide_table_logical_row(t, b);
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, la, 0u) = b;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, lb, 0u) = a;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, a, 1u) = lb;
        *(uint32_t *)tide_table_cell(t->mapping, &mapping_columns, b, 1u) = la;
        do {
            lo++;
            if (lo == hi) break;
            const uint8_t *v = tide_table_column(t, c, lo >> c->shift, presence);
            if ((v[lo & (full_room(c) - 1u)] != 0u) != need) break;
        } while (lo < hi);
        while (lo < hi) {
            const uint32_t at = hi - 1u;
            const uint8_t *v = tide_table_column(t, c, at >> c->shift, presence);
            if ((v[at & (full_room(c) - 1u)] != 0u) == need) break;
            hi--;
        }
    }
    free(scratch);
    return lo;
}

#endif
