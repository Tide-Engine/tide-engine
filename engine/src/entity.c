#include "tide/entity.h"

#include <stdlib.h>
#include <string.h>

// See tide/entity.h.

#define SLOTS (1u << TIDE_ENTITY_PAGE_SHIFT)
#define PAGE_BYTES (SLOTS * (uint32_t)sizeof(tide_entity_slot))
#define BORN_BYTES (SLOTS * (uint32_t)sizeof(uint32_t))

// Read as one, since the system making entities may be growing the table or
// copying a page meanwhile.
static const tide_entity_slot *slot_of(const tide_entities *t, const uint32_t index)
{
    return tide_entity_slot_of(t, index);
}

// Slots in use on page `p`.
static uint32_t used_on(const tide_entities *t, const uint32_t p)
{
    const uint32_t left = t->next_unused - (p << TIDE_ENTITY_PAGE_SHIFT);
    return left < SLOTS ? left : SLOTS;
}

// A slot to change: its page made this table's own.
static tide_entity_slot *slot_to_change(tide_entities *t, const uint32_t index)
{
    const uint32_t p = index >> TIDE_ENTITY_PAGE_SHIFT;
    tide_page *own = t->page[p] ? tide_page_own(t->page[p], 1, used_on(t, p) * (uint32_t)sizeof(tide_entity_slot))
                              : tide_page_new(PAGE_BYTES);
    if (own != t->page[p]) __atomic_store_n(&t->page[p], own, __ATOMIC_RELEASE); // Code reading the old page meanwhile reads the same slots
    tide_entity_slot *slots = tide_page_data(own);
    return &slots[index & (SLOTS - 1u)];
}

// A bigger table of pages, which takes the old one's place as a whole. The
// old one stays, linked from page[-1], for code on other threads still reading
// it, until tide_entities_settle.
static void make_room(tide_entities *t, const uint32_t pages)
{
    if (pages <= t->room) return;
    uint32_t room = t->room ? t->room : 4u;
    while (room < pages) room *= 2u;
    // The slots' pages, then as many places for the pages of what their
    // entities were born as (tide_entity_set_born), none until one is set
    tide_page **base = tide_alloc((2u * (size_t)room + 1u) * sizeof *base);
    tide_page **grown = base + 1;
    memset(grown, 0, (size_t)room * 2u * sizeof *grown);
    if (t->pages) {
        memcpy(grown, t->page, t->pages * sizeof *grown);
        memcpy(grown + room, t->page + t->room, t->pages * sizeof *grown);
    }
    base[0] = t->page ? (tide_page *)(void *)(t->page - 1) : NULL;
    __atomic_store_n(&t->page, grown, __ATOMIC_RELEASE);
    t->room = room;
}

void tide_entities_settle(tide_entities *t)
{
    if (!t->page) return;
    void *old = t->page[-1];
    while (old) {
        tide_page **base = old;
        old = base[0];
        free(base);
    }
    t->page[-1] = NULL;
}

tide_entity tide_entity_create(tide_entities *t)
{
    uint32_t index;
    if (t->free_head) {
        index = t->free_head - 1u;
    } else {
        if (t->next_unused == UINT32_MAX) tide_out_of_memory();
        index = t->next_unused;
        if ((index & (SLOTS - 1u)) == 0) {
            make_room(t, t->pages + 1u);
            t->page[t->pages++] = tide_page_new(PAGE_BYTES); // Nothing reads past next_unused
        }
        t->next_unused++;
    }

    tide_entity_slot *slot = slot_to_change(t, index);
    if (t->free_head) t->free_head = slot->row;
    slot->generation++; // Even (free) -> odd (alive).
    slot->archetype = TIDE_ARCHETYPE_NONE;
    slot->row = 0;
    slot->snaps = 0;
    return (tide_entity){index, slot->generation};
}

bool tide_entity_destroy(tide_entities *t, const tide_entity e)
{
    if (!tide_entity_alive(t, e)) return false;

    tide_entity_slot *slot = slot_to_change(t, e.index);
    slot->generation++; // Odd (alive) -> even (free); stale handles stop matching.
    if (slot->generation & TIDE_ENTITY_TEMPORARY) slot->generation = 2; // A slot reused 2^30 times: temporary handles have that bit
    slot->archetype = TIDE_ARCHETYPE_NONE;
    slot->row = t->free_head;
    slot->snaps = 0;
    t->free_head = e.index + 1u;
    return true;
}

void tide_entity_set_location(tide_entities *t, const tide_entity e, const tide_location loc)
{
    if (!tide_entity_alive(t, e)) return;
    tide_entity_slot *slot = slot_to_change(t, e.index);
    slot->archetype = loc.archetype;
    slot->row = loc.row;
}

tide_entity tide_entity_in_slot(const tide_entities *t, const uint32_t index)
{
    if (index >= t->next_unused) return (tide_entity){0};
    const uint32_t generation = slot_of(t, index)->generation;
    return (generation & 1u) ? (tide_entity){index, generation} : (tide_entity){0};
}

void tide_entity_snap(tide_entities *t, const tide_entity e)
{
    if (tide_entity_alive(t, e)) slot_to_change(t, e.index)->snaps++;
}

uint32_t tide_entity_snaps(const tide_entities *t, const tide_entity e)
{
    return tide_entity_alive(t, e) ? slot_of(t, e.index)->snaps : 0u;
}

void tide_entity_set_born(tide_entities *t, const tide_entity e, const uint32_t born)
{
    if (!tide_entity_alive(t, e)) return;
    tide_page **page = &t->page[t->room + (e.index >> TIDE_ENTITY_PAGE_SHIFT)];
    *page = *page ? tide_page_own(*page, 1, BORN_BYTES) : tide_page_new(BORN_BYTES); // Its snapshots keep theirs
    ((uint32_t *)tide_page_data(*page))[e.index & (SLOTS - 1u)] = born;
}

void tide_entities_copy(tide_entities *to, const tide_entities *from)
{
    if (to == from) return;
    // Its pages first, then letting go of to's: they can be the same ones.
    for (uint32_t i = 0; i < from->pages; i++) {
        if (from->page[i]) tide_page_retain(from->page[i]);
        if (from->page[from->room + i]) tide_page_retain(from->page[from->room + i]);
    }
    for (uint32_t i = 0; i < to->pages; i++) {
        tide_page_release(to->page[i], 1);
        tide_page_release(to->page[to->room + i], 1);
    }
    make_room(to, from->pages);
    tide_entities_settle(to); // Snapshots are taken while nothing else runs on it
    if (to->room) memset(to->page + to->room, 0, to->room * sizeof *to->page);
    if (from->pages) {
        memcpy(to->page, from->page, from->pages * sizeof *to->page);
        memcpy(to->page + to->room, from->page + from->room, from->pages * sizeof *to->page);
    }
    to->pages = from->pages;
    to->next_unused = from->next_unused;
    to->free_head = from->free_head;
}

uint64_t tide_entities_hash(uint64_t h, const tide_entities *t)
{
    h = tide_hash_more(h, &t->next_unused, sizeof t->next_unused);
    h = tide_hash_more(h, &t->free_head, sizeof t->free_head);
    for (uint32_t p = 0; p < t->pages; p++) {
        const uint64_t page = t->page[p] ? tide_page_hash(t->page[p], used_on(t, p) * (uint32_t)sizeof(tide_entity_slot)) : 0u;
        h = tide_hash_more(h, &page, sizeof page);
    }
    return h;
}

void tide_entities_stale(const tide_entities *t, tide_stale_pages *s)
{
    for (uint32_t p = 0; p < t->pages; p++) {
        if (t->page[p]) tide_page_stale(s, t->page[p], used_on(t, p) * (uint32_t)sizeof(tide_entity_slot));
    }
}

void tide_entities_free(tide_entities *t)
{
    for (uint32_t i = 0; i < t->pages; i++) {
        tide_page_release(t->page[i], 1);
        tide_page_release(t->page[t->room + i], 1);
    }
    tide_entities_settle(t);
    if (t->page) free(t->page - 1);
    memset(t, 0, sizeof *t);
}

// Free slots lead to free slots in the table, without going round: true of
// any table, so one that came as bytes isn't one without it.
static bool free_slots_hold(const tide_entities *t)
{
    uint32_t free = t->free_head;
    for (uint32_t n = 0; free; n++) {
        const tide_entity_slot *slot = free <= t->next_unused ? slot_of(t, free - 1u) : NULL;
        if (!slot || (slot->generation & 1u) || slot->row > t->next_unused || n == t->next_unused) return false;
        free = slot->row;
    }
    return true;
}

uint32_t tide_entities_packed_size(const tide_entities *t)
{
    bool sparse = false;
    uint64_t size = 16;
    for (uint32_t p = 0; p < t->pages; p++) {
        if (!t->page[p]) sparse = true;
        else size += 4u + (uint64_t)used_on(t, p) * sizeof(tide_entity_slot);
    }
    if (!sparse) size = 8u + (uint64_t)t->next_unused * sizeof(tide_entity_slot);
    if (size > UINT32_MAX) tide_out_of_memory();
    return (uint32_t)size;
}

static bool sparse_pages(const tide_entities *t)
{
    for (uint32_t p = 0; p < t->pages; p++) if (!t->page[p]) return true;
    return false;
}

// A free-list head past the table is otherwise invalid. Dense tables retain
// their existing byte representation; sparse tables name only populated pages.
static uint32_t sparse_marker(const uint32_t next_unused)
{
    return next_unused == UINT32_MAX ? UINT32_MAX : next_unused + 1u;
}

void tide_entities_pack(const tide_entities *t, tide_writer *w)
{
    tide_write_u32(w, t->next_unused);
    const bool sparse = sparse_pages(t);
    tide_write_u32(w, sparse ? sparse_marker(t->next_unused) : t->free_head);
    if (sparse) {
        tide_write_u32(w, t->free_head);
        uint32_t count = 0;
        for (uint32_t p = 0; p < t->pages; p++) count += t->page[p] != NULL;
        tide_write_u32(w, count);
    }
    for (uint32_t p = 0; p < t->pages; p++) {
        if (!t->page[p]) continue;
        if (sparse) tide_write_u32(w, p);
        tide_write_bytes(w, tide_page_data(t->page[p]), used_on(t, p) * (uint32_t)sizeof(tide_entity_slot));
    }
}

bool tide_entities_unpack(tide_entities *t, tide_reader *r)
{
    const uint32_t next_unused = tide_read_u32(r);
    uint32_t free_head = tide_read_u32(r);
    const bool sparse = free_head == sparse_marker(next_unused) && (free_head > next_unused || next_unused == UINT32_MAX);
    const uint32_t count = sparse ? (free_head = tide_read_u32(r), tide_read_u32(r))
                                  : (uint32_t)(((uint64_t)next_unused + SLOTS - 1u) >> TIDE_ENTITY_PAGE_SHIFT);
    if (r->failed || free_head > next_unused) return false;
    if (!sparse && next_unused > (r->size - r->at) / sizeof(tide_entity_slot)) return false;
    if (sparse && count > (r->size - r->at) / (4u + sizeof(tide_entity_slot))) return false;
    t->next_unused = next_unused;
    t->free_head = free_head;
    const uint32_t pages = (uint32_t)(((uint64_t)next_unused + SLOTS - 1u) >> TIDE_ENTITY_PAGE_SHIFT);
    make_room(t, pages);
    t->pages = pages;
    uint32_t previous = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t p = sparse ? tide_read_u32(r) : i;
        if (r->failed || p >= pages || (i && p <= previous)) return false;
        previous = p;
        const uint32_t bytes = used_on(t, p) * (uint32_t)sizeof(tide_entity_slot);
        const uint8_t *data = tide_read_bytes(r, bytes);
        if (!data) return false;
        t->page[p] = tide_page_new(PAGE_BYTES);
        memcpy(tide_page_data(t->page[p]), data, bytes);
    }
    tide_entities_settle(t);
    return !r->failed && free_slots_hold(t);
}

// Packed as part of a delta: its numbers, then each page as a region.

#define SLOT ((uint32_t)sizeof(tide_entity_slot))

void tide_entities_pack_delta(const tide_entities *t, const tide_entities *base, tide_delta_writer *d)
{
    const bool sparse = sparse_pages(t);
    tide_delta_number(d, t->next_unused);
    tide_delta_number(d, sparse ? sparse_marker(t->next_unused) : t->free_head);
    if (sparse) {
        tide_delta_number(d, t->free_head);
        uint32_t count = 0;
        for (uint32_t p = 0; p < t->pages; p++) count += t->page[p] != NULL;
        tide_delta_number(d, count);
    }
    for (uint32_t p = 0; p < t->pages; p++) {
        if (!t->page[p]) continue;
        if (sparse) tide_delta_number(d, p);
        const tide_page *b = base && p < base->pages ? base->page[p] : NULL;
        tide_delta_region(d, tide_page_data(t->page[p]), used_on(t, p) * SLOT, b ? tide_page_data(b) : NULL,
                          b ? used_on(base, p) * SLOT : 0u, b == t->page[p], SLOT);
    }
    tide_delta_close(d);
}

bool tide_entities_unpack_delta(tide_entities *t, const tide_entities *base, tide_delta_reader *d)
{
    const uint32_t next_unused = tide_delta_get_number(d);
    uint32_t free_head = tide_delta_get_number(d);
    const bool sparse = free_head == sparse_marker(next_unused) && (free_head > next_unused || next_unused == UINT32_MAX);
    const uint32_t count = sparse ? (free_head = tide_delta_get_number(d), tide_delta_get_number(d))
                                  : (uint32_t)(((uint64_t)next_unused + SLOTS - 1u) >> TIDE_ENTITY_PAGE_SHIFT);
    if (d->bytes.failed || free_head > next_unused) return false;
    // Each page is the base's, or bytes of the delta
    const uint32_t pages = (uint32_t)(((uint64_t)next_unused + SLOTS - 1u) >> TIDE_ENTITY_PAGE_SHIFT);
    if (count > pages || count > (base ? base->pages : 0u) + (d->bytes.size - d->bytes.at)) return false;
    t->next_unused = next_unused;
    t->free_head = free_head;
    make_room(t, pages);
    t->pages = pages;
    bool ok = true;
    uint32_t previous = UINT32_MAX;
    for (uint32_t i = 0; ok && i < count; i++) {
        const uint32_t p = sparse ? tide_delta_get_number(d) : i;
        if (d->bytes.failed || p >= pages || (i && p <= previous)) { ok = false; break; }
        previous = p;
        tide_page *b = base && p < base->pages ? base->page[p] : NULL;
        tide_page *page = tide_delta_page(d, used_on(t, p) * SLOT, PAGE_BYTES, 1, b, b ? used_on(base, p) * SLOT : 0u, true);
        ok = page != NULL;
        if (ok) t->page[p] = page;
    }
    tide_entities_settle(t);
    return ok && tide_delta_closed(d) && free_slots_hold(t);
}

void tide_entities_hash_pages(const tide_entities *t, tide_writer *w)
{
    if (sparse_pages(t)) {
        tide_write_varint(w, UINT32_MAX);
        uint32_t count = 0;
        for (uint32_t p = 0; p < t->pages; p++) count += t->page[p] != NULL;
        tide_write_varint(w, count);
        for (uint32_t p = 0; p < t->pages; p++) {
            if (!t->page[p]) continue;
            tide_write_varint(w, p);
            tide_write_u64(w, tide_page_hash(t->page[p], used_on(t, p) * SLOT));
        }
        return;
    }
    tide_write_varint(w, t->pages);
    for (uint32_t p = 0; p < t->pages; p++) tide_write_u64(w, t->page[p] ? tide_page_hash(t->page[p], used_on(t, p) * SLOT) : 0u);
}

void tide_entities_need_pages(const tide_entities *base, tide_needs *n)
{
    tide_reader peek = n->list;
    const bool sparse = tide_read_varint(&peek) == UINT32_MAX;
    if (sparse) n->list = peek;
    const uint32_t count = tide_needs_count(n);
    uint32_t previous = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t p = sparse ? tide_read_varint(&n->list) : i;
        if (sparse && (p > (UINT32_MAX >> TIDE_ENTITY_PAGE_SHIFT) || (i && p <= previous))) n->list.failed = true;
        previous = p;
        const bool have = p < base->pages && base->page[p];
        tide_needs_put(n, have, have ? tide_page_hash(base->page[p], used_on(base, p) * SLOT) : 0u);
    }
}
