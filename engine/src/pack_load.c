// Loading a pack (see tide/pack.h): its region at the top of the scratch
// area's offsets, where every thread maps it (tide_pack_use), and its entries
// by name. The builder is pack.c.

#include "tide/pack.h"

#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/sha256.h"

#define OFFSET(at) ((at) & 0x3FFFFFFFu)

// A tagged offset into the region.
static uint32_t tagged(const uint32_t offset)
{
    return TIDE_IN_SCRATCH << 30 | (TIDE_PACK_BASE + offset);
}


uint32_t tide_pack_region_slots;
tide_heap tide_pack_still;

static struct {
    uint8_t *region;
    uint32_t size;
    tide_pack_entry *entries;
    uint32_t entry_count;
    uint32_t epoch; // Counts the loads, so threads see a new one
} pack;

static TIDE_THREAD_LOCAL uint32_t mapped_epoch;

bool tide_pack_load(const uint8_t *bytes, const size_t size)
{
    tide_pack_header h;
    if (size < sizeof h) return false;
    memcpy(&h, bytes, sizeof h);
    if (memcmp(h.magic, TIDE_PACK_MAGIC, 8) != 0 || h.version != TIDE_PACK_VERSION) return false;
    const uint64_t table_end = (uint64_t)sizeof h + (uint64_t)h.entries * sizeof(tide_pack_entry);
    if (table_end > h.region_at || (uint64_t)h.region_at + h.region_size != size || h.region_size > TIDE_PACK_BASE) return false;
    const tide_pack_entry *entries = (const tide_pack_entry *)(const void *)(bytes + sizeof h);
    for (uint32_t i = 0; i < h.entries; i++) {
        if (entries[i].offset >= h.region_size && h.region_size) return false;
    }
    tide_pack_unload();
    // The region in memory of its own, a whole number of slots, so that the
    // last slot's pointer reaches memory wherever an offset in it points
    const uint32_t slots = (h.region_size + TIDE_SCRATCH_BYTES - 1u) >> TIDE_SCRATCH_SHIFT;
    pack.region = tide_alloc(slots ? (size_t)slots << TIDE_SCRATCH_SHIFT : 16u);
    memcpy(pack.region, bytes + h.region_at, h.region_size);
    pack.size = h.region_size;
    pack.entries = tide_alloc(h.entries ? h.entries * sizeof(tide_pack_entry) : 16u);
    memcpy(pack.entries, entries, (size_t)h.entries * sizeof(tide_pack_entry));
    pack.entry_count = h.entries;
    pack.epoch++;
    tide_pack_region_slots = slots;
    tide_pack_use();
    return true;
}

void tide_pack_unload(void)
{
    free(pack.region);
    free(pack.entries);
    pack.region = NULL;
    pack.entries = NULL;
    pack.size = 0;
    pack.entry_count = 0;
    pack.epoch++;
    tide_pack_region_slots = 0;
    tide_pack_use();
}

void tide_pack_use(void)
{
    if (mapped_epoch == pack.epoch) return;
    mapped_epoch = pack.epoch;
    const uint32_t first = TIDE_PACK_BASE >> TIDE_SCRATCH_SHIFT;
    for (uint32_t k = first; k < TIDE_SCRATCH_SLOTS; k++) {
        const uint32_t i = k - first;
        tide_scratch_slots[k] = i < tide_pack_region_slots ? (char *)pack.region + ((size_t)i << TIDE_SCRATCH_SHIFT) : NULL;
    }
}

const tide_pack_entry *tide_pack_find(const char *qualified, const uint32_t kind)
{
    uint8_t name[32];
    tide_sha256_of(qualified, strlen(qualified), name);
    for (uint32_t i = 0; i < pack.entry_count; i++) {
        const tide_pack_entry *e = &pack.entries[i];
        if (e->kind == kind && memcmp(e->name, name, 32) == 0) return e;
    }
    return NULL;
}

uint32_t tide_pack_handle(const tide_pack_entry *e)
{
    return e ? tagged(e->offset) : 0u;
}

tide_str tide_pack_text(const tide_pack_entry *e)
{
    if (!e) return TIDE_STR_EMPTY;
    const tide_block *b = (const tide_block *)(const void *)(pack.region + e->offset);
    return (tide_str){(const char *)(b + 1), (int32_t)b->a, (int32_t)e->extra};
}

tide_map tide_pack_map_of(const tide_pack_entry *e)
{
    if (!e) return (tide_map){0};
    return (tide_map){{tagged(e->offset)}, {tagged(e->size)}, e->extra};
}

uint64_t tide_pack_grid_version(const tide_grid g)
{
    const tide_block *b = tide_block_at(g.at);
    return b ? (uint64_t)b->a | (uint64_t)b->b << 32 : 0u;
}
