#include "asset_store.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide/skinned_data.h"

static atomic_flag pack_lock = ATOMIC_FLAG_INIT;
static tide_asset_pool *current;

static uint32_t slot_of(const uint8_t id[32], const uint32_t slots)
{
    uint32_t word;
    memcpy(&word, id, sizeof word);
    return word & (slots - 1u);
}

static tide_asset_entry *entry_of(tide_asset_pool *pool, const uint8_t id[32])
{
    uint32_t at = slot_of(id, pool->slots);
    while (pool->entries[at].data && memcmp(pool->entries[at].data->id, id, 32) != 0) at = (at + 1u) & (pool->slots - 1u);
    return &pool->entries[at];
}

void tide_asset_retain(tide_asset_data *data)
{
    if (data) __atomic_add_fetch(&data->refs, 1u, __ATOMIC_RELAXED);
}

void tide_asset_release(tide_asset_data *data)
{
    if (data && __atomic_sub_fetch(&data->refs, 1u, __ATOMIC_ACQ_REL) == 0) free(data);
}

void tide_asset_pool_retain(tide_asset_pool *pool)
{
    if (pool) __atomic_add_fetch(&pool->refs, 1u, __ATOMIC_RELAXED);
}

void tide_asset_pool_release(tide_asset_pool *pool)
{
    if (!pool || __atomic_sub_fetch(&pool->refs, 1u, __ATOMIC_ACQ_REL) != 0) return;
    for (uint32_t i = 0; i < pool->slots; i++) {
        tide_asset_release(pool->entries[i].data);
        free(pool->entries[i].blocks);
    }
    free(pool);
}

static void append(tide_asset_entry *entry, const uint64_t block)
{
    if (entry->users == entry->room) {
        const uint32_t room = entry->room ? entry->room * 2u : 4u;
        uint64_t *blocks = tide_alloc((size_t)room * sizeof *blocks);
        if (entry->users) memcpy(blocks, entry->blocks, (size_t)entry->users * sizeof *blocks);
        free(entry->blocks);
        entry->blocks = blocks;
        entry->room = room;
    }
    entry->blocks[entry->users++] = block;
}

static tide_asset_entry copied(const tide_asset_entry entry)
{
    tide_asset_entry made = {.data = entry.data};
    tide_asset_retain(made.data);
    for (uint32_t i = 0; i < entry.users; i++) append(&made, entry.blocks[i]);
    return made;
}

static void remove_block(tide_asset_entry *entry, const uint64_t block)
{
    for (uint32_t i = 0; i < entry->users; i++) {
        if (entry->blocks[i] != block) continue;
        entry->blocks[i] = entry->blocks[--entry->users];
        return;
    }
}

static void lease(tide_asset_pool *pool, const tide_asset_data *data, const uint64_t block, const bool add)
{
    const uint64_t id = tide_hash(data->id, sizeof data->id);
    const uint64_t hash = tide_hash_more(id, &block, sizeof block);
    if (add) { pool->leases += hash; pool->descriptors++; }
    else { pool->leases -= hash; pool->descriptors--; }
}

static tide_asset_pool *changed(const tide_asset_pool *was, tide_asset_data *data, const uint64_t block, const int change)
{
    uint32_t count = was ? was->count : 0u;
    const tide_asset_entry *old = was ? entry_of((tide_asset_pool *)(uintptr_t)was, data->id) : NULL;
    const uint32_t users = old && old->data ? old->users : 0u;
    if (change > 0 && users == 0) count++;
    if (change < 0 && users == 1u) count--;
    uint32_t slots = 8u;
    while (slots / 2u < count) slots *= 2u;
    tide_asset_pool *pool = tide_alloc_zeroed(1, sizeof *pool + (size_t)slots * sizeof *pool->entries);
    pool->refs = 1;
    pool->count = count;
    pool->slots = slots;
    pool->leases = was ? was->leases : 0;
    pool->descriptors = was ? was->descriptors : 0;
    lease(pool, data, block, change > 0);
    for (uint32_t i = 0; was && i < was->slots; i++) {
        tide_asset_entry item = was->entries[i];
        if (!item.data || memcmp(item.data->id, data->id, 32) == 0) continue;
        *entry_of(pool, item.data->id) = copied(item);
    }
    const uint32_t next = change > 0 ? users + 1u : users - 1u;
    if (next) {
        tide_asset_entry item = old && old->data ? copied(*old) : (tide_asset_entry){.data = data};
        if (!old || !old->data) tide_asset_retain(data);
        if (change > 0) append(&item, block); else remove_block(&item, block);
        *entry_of(pool, data->id) = item;
    }
    return pool;
}

static void replace(tide_heap *heap, tide_asset_pool *pool)
{
    tide_asset_pool *was = __atomic_load_n(&heap->assets, __ATOMIC_ACQUIRE);
    if (was) {
        // Retirement belongs to this heap, not the shared immutable pool.
        tide_asset_retired *item = tide_alloc(sizeof *item);
        *item = (tide_asset_retired){was, __atomic_load_n(&heap->assets_retired, __ATOMIC_ACQUIRE)};
        __atomic_store_n(&heap->assets_retired, item, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&heap->assets, pool, __ATOMIC_RELEASE);
}

void tide_asset_settle(tide_heap *heap)
{
    while (heap->assets_retired) {
        tide_asset_retired *item = heap->assets_retired;
        heap->assets_retired = item->next;
        tide_asset_pool_release(item->pool);
        free(item);
    }
}

tide_asset_data *tide_asset_find(const tide_heap *heap, const uint8_t id[32])
{
    tide_asset_pool *pool = __atomic_load_n(&heap->assets, __ATOMIC_ACQUIRE);
    tide_asset_data *data = pool ? entry_of(pool, id)->data : NULL;
    // A local value may still name a just-released field until the routine ends.
    for (tide_asset_retired *old = __atomic_load_n(&heap->assets_retired, __ATOMIC_ACQUIRE);
         !data && old; old = old->next) data = entry_of(old->pool, id)->data;
    return data;
}

void tide_asset_use(tide_heap *heap, tide_asset_data *data, const uint64_t block)
{
    tide_asset_pool *pool = heap->assets;
    tide_asset_entry *entry = pool ? entry_of(pool, data->id) : NULL;
    if (pool && __atomic_load_n(&pool->refs, __ATOMIC_RELAXED) == 1u && entry->data) {
        append(entry, block);
        lease(pool, data, block, true);
        return;
    }
    replace(heap, changed(pool, data, block, 1));
}

void tide_asset_drop(tide_heap *heap, const uint8_t id[32], const uint64_t block)
{
    tide_asset_pool *pool = heap->assets;
    tide_asset_entry *entry = pool ? entry_of(pool, id) : NULL;
    if (!entry || !entry->data) return;
    if (__atomic_load_n(&pool->refs, __ATOMIC_RELAXED) == 1u && entry->users > 1u) {
        remove_block(entry, block);
        lease(pool, entry->data, block, false);
        return;
    }
    replace(heap, changed(pool, entry->data, block, -1));
}

static void identity(const void *bytes, const uint32_t count, const uint32_t element, uint8_t out[32])
{
    const uint32_t shape[2] = {count, element};
    tide_sha256 hash;
    tide_sha256_init(&hash);
    tide_sha256_add(&hash, "TideListAsset1", 14);
    tide_sha256_add(&hash, shape, sizeof shape);
    tide_sha256_add(&hash, bytes, (size_t)count * element);
    tide_sha256_end(&hash, out);
}

static tide_asset_data *make(const void *bytes, const uint32_t count, const uint32_t element, const uint64_t pack_at)
{
    tide_asset_data *data = tide_alloc_zeroed(1, sizeof *data + (size_t)count * element);
    data->refs = 1;
    data->count = count;
    data->element = element;
    data->pack_at = pack_at;
    memcpy(data->bytes, bytes, (size_t)count * element);
    identity(bytes, count, element, data->id);
    // Validation is once, on the immutable owned copy. A raw byte asset may
    // qualify too, but malformed or later detached bytes never inherit it.
    tide_skinned_data_view view;
    if (element == 1u && tide_skinned_data_open(data->bytes, count, &view) && tide_skinned_data_validate(data->bytes, count)) {
        data->skinned = true;
        data->nonnegative = true;
        for (uint32_t i = 0; i < view.channels; i++) {
            tide_skinned_channel_data channel;
            float time, value[4], in[4], out[4];
            if (!tide_skinned_data_channel(&view, i, &channel)
                || !tide_skinned_data_key(&view, &channel, 0, &time, value, in, out)
                || !(time >= 0.0f)) data->nonnegative = false;
        }
        data->version = tide_hash(data->bytes, count);
    }
    return data;
}

tide_asset_data *tide_asset_model(const void *bytes, const uint32_t count)
{
    if (!bytes || count < TIDE_SKINNED_HEADER_SIZE || count > TIDE_SKINNED_MAX_BYTES) return NULL;
    tide_asset_data *data = make(bytes, count, 1u, 0u);
    if (data->skinned) return data;
    tide_asset_release(data);
    return NULL;
}

tide_asset_data *tide_asset_pack(const tide_list list, const uint32_t element)
{
    if (!tide_pack_holds(list.at) || !element) return NULL;
    const uint64_t offset = tide_handle_offset(list.at);
    const uint8_t *header = tide_pack_span(offset, sizeof(tide_block));
    if (!header) return NULL;
    tide_block block;
    memcpy(&block, header, sizeof block);
    if (!block.a || block.a > INT32_MAX || block.b != block.a || (uint64_t)block.a * element > SIZE_MAX / 2u) return NULL;
    const uint8_t *bytes = tide_pack_span(offset + sizeof(tide_block), (uint64_t)block.a * element);
    if (!bytes) return NULL;
    while (atomic_flag_test_and_set_explicit(&pack_lock, memory_order_acquire)) {}
    tide_asset_data *found = NULL;
    for (uint32_t i = 0; current && i < current->slots; i++) {
        tide_asset_data *data = current->entries[i].data;
        if (data && data->pack_at == offset && data->element == element) { found = data; break; }
    }
    if (!found) {
        found = make(bytes, block.a, element, offset);
        tide_asset_data *equal = current ? entry_of(current, found->id)->data : NULL;
        if (equal) {
            tide_asset_release(found);
            found = equal;
        } else {
            tide_asset_pool *next = changed(current, found, 0, 1);
            tide_asset_pool_release(current);
            current = next;
            tide_asset_release(found);
        }
    }
    atomic_flag_clear_explicit(&pack_lock, memory_order_release);
    return found;
}

void tide_asset_pack_clear(void)
{
    tide_asset_pool_release(current);
    current = NULL;
}

tide_asset_data *tide_asset_list(const tide_list list)
{
    const tide_block *block = tide_block_at(list.at);
    const tide_heap *heap = list.at ? tide_heap_read(tide_handle_where(list.at)) : NULL;
    if (!heap || !block || block->b != TIDE_LIST_ASSET) return NULL;
    tide_asset_data *data = tide_asset_find(heap, (const uint8_t *)(block + 1));
    return data && data->count == block->a ? data : NULL;
}

const void *tide_list_asset_at(const tide_list list, const int32_t i, const uint32_t element)
{
    const tide_asset_data *data = tide_asset_list(list);
    return data && element == data->element && i >= 0 && (uint32_t)i < data->count
               ? data->bytes + (size_t)(uint32_t)i * element : NULL;
}

// An entry's bytes before its payload, as packed: its ID, count, element's bytes, users and pack place.
#define ASSET_HEAD (32u + 3u * 4u + 8u)

uint32_t tide_asset_packed_size(const tide_heap *heap)
{
    uint64_t bytes = 4;
    const tide_asset_pool *pool = heap->assets;
    for (uint32_t i = 0; pool && i < pool->slots; i++) {
        const tide_asset_data *data = pool->entries[i].data;
        if (data) bytes += ASSET_HEAD + (uint64_t)data->count * data->element + (uint64_t)pool->entries[i].users * 8u;
    }
    if (bytes > UINT32_MAX) tide_out_of_memory();
    return (uint32_t)bytes;
}

void tide_asset_pack_heap(const tide_heap *heap, tide_writer *writer, const bool references, const tide_heap *base)
{
    const tide_asset_pool *pool = heap->assets;
    tide_write_u32(writer, pool ? pool->count : 0u);
    for (uint32_t i = 0; pool && i < pool->slots; i++) {
        const tide_asset_entry entry = pool->entries[i];
        if (!entry.data) continue;
        const tide_asset_data *data = entry.data;
        bool reference = false;
        if (references && data->pack_at && tide_pack_file_span(data->pack_at, sizeof(tide_block) + (uint64_t)data->count * data->element)) {
            tide_asset_data *now = tide_asset_pack((tide_list){tide_handle_of(TIDE_IN_SCRATCH, data->pack_at)}, data->element);
            reference = now && memcmp(now->id, data->id, 32) == 0;
        }
        tide_write_bytes(writer, data->id, 32);
        tide_write_u32(writer, data->count);
        tide_write_u32(writer, data->element);
        tide_write_u32(writer, entry.users);
        const bool from_base = base && tide_asset_find(base, data->id) != NULL;
        tide_write_u64(writer, from_base ? UINT64_MAX : reference ? data->pack_at : 0u);
        if (!reference && !from_base) tide_write_bytes(writer, data->bytes, data->count * data->element);
        for (uint32_t j = 0; j < entry.users; j++) tide_write_u64(writer, entry.blocks[j]);
    }
}

bool tide_asset_unpack_heap(tide_heap *heap, tide_reader *reader, const tide_heap *base)
{
    const uint32_t count = tide_read_u32(reader);
    if (count > (reader->size - reader->at) / ASSET_HEAD) return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *id = tide_read_bytes(reader, 32);
        const uint32_t elements = tide_read_u32(reader);
        const uint32_t element = tide_read_u32(reader);
        const uint32_t users = tide_read_u32(reader);
        const uint64_t at = tide_read_u64(reader);
        const uint64_t bytes = (uint64_t)elements * element;
        if (reader->failed || !elements || elements > INT32_MAX || !element || bytes > SIZE_MAX / 2u || !users
            || users > (reader->size - reader->at) / 8u) return false;
        tide_asset_data *data;
        if (at == UINT64_MAX) {
            data = base ? tide_asset_find(base, id) : NULL;
            if (!data || data->count != elements || data->element != element) return false;
            tide_asset_retain(data);
        } else if (at) {
            if (at < TIDE_PACK_BASE || at > TIDE_HANDLE_OFFSET) return false;
            data = tide_asset_pack((tide_list){tide_handle_of(TIDE_IN_SCRATCH, at)}, element);
            if (!data || data->count != elements || memcmp(data->id, id, 32) != 0) return false;
            tide_asset_retain(data);
        } else {
            const uint8_t *source = bytes <= reader->size - reader->at ? tide_read_bytes(reader, (uint32_t)bytes) : NULL;
            if (!source) return false;
            data = make(source, elements, element, 0);
            if (memcmp(data->id, id, 32) != 0) { tide_asset_release(data); return false; }
        }
        if (tide_asset_find(heap, data->id)) { tide_asset_release(data); return false; }
        if (users > (reader->size - reader->at) / 8u) { tide_asset_release(data); return false; }
        for (uint32_t j = 0; j < users; j++) tide_asset_use(heap, data, tide_read_u64(reader));
        tide_asset_release(data);
    }
    tide_asset_settle(heap);
    return !reader->failed;
}

void tide_asset_origins_retain(tide_asset_origins *origins)
{
    if (origins) origins->refs++;
}

void tide_asset_origins_release(tide_asset_origins *origins)
{
    if (!origins || --origins->refs) return;
    for (uint32_t i = 0; i < origins->room; i++) tide_asset_release(origins->pages[i].data);
    free(origins);
}

static tide_asset_origins *own_origins(tide_heap *heap, const uint32_t page)
{
    tide_asset_origins *was = heap->origins;
    if (was && was->refs == 1u && page < was->room) return was;
    uint32_t room = was ? was->room : 4u;
    while (room <= page) room *= 2u;
    tide_asset_origins *made = tide_alloc_zeroed(1, sizeof *made + (size_t)room * sizeof *made->pages);
    made->refs = 1;
    made->room = room;
    if (was) {
        memcpy(made->pages, was->pages, (size_t)was->room * sizeof *was->pages);
        for (uint32_t i = 0; i < was->room; i++) tide_asset_retain(was->pages[i].data);
    }
    heap->origins = made;
    tide_asset_origins_release(was);
    return made;
}

void tide_asset_origin_set(tide_heap *heap, const uint32_t page, const tide_asset_origin origin)
{
    tide_asset_origins *origins = own_origins(heap, page);
    tide_asset_retain(origin.data);
    tide_asset_release(origins->pages[page].data);
    origins->pages[page] = origin;
}

void tide_asset_origin_clear(tide_heap *heap, const uint64_t block)
{
    const uint64_t page = block >> TIDE_HEAP_PAGE_SHIFT;
    if (!heap->origins || page >= heap->origins->room || !heap->origins->pages[page].data) return;
    tide_asset_origin_set(heap, (uint32_t)page, (tide_asset_origin){0});
}

const tide_asset_origin *tide_asset_origin_page(const tide_heap *heap, const uint32_t page)
{
    return heap->origins && page < heap->origins->room && heap->origins->pages[page].data
        ? &heap->origins->pages[page] : NULL;
}

void tide_asset_origin_list(tide_heap *heap, const tide_list list, tide_asset_data *data)
{
    if (!data || !list.at || tide_handle_where(list.at) == TIDE_IN_SCRATCH) return;
    const tide_block *block = tide_heap_block(heap, tide_handle_offset(list.at));
    if (!(block->b & TIDE_LIST_CHUNKED)) return;
    const uint32_t per = TIDE_LIST_CHUNK / data->element;
    const uint32_t chunks = block->b & ~TIDE_LIST_CHUNKED;
    const uint64_t *directory = (const uint64_t *)(const void *)(block + 1);
    for (uint32_t k = 0; k < chunks; k++) {
        const uint64_t first = (uint64_t)k * per * data->element;
        const uint64_t length = (uint64_t)data->count * data->element;
        if (first >= length) break;
        const uint32_t left = (uint32_t)(length - first);
        const uint32_t count = left < per * data->element ? left : per * data->element;
        tide_asset_origin_set(heap, (uint32_t)(directory[k] >> TIDE_HEAP_PAGE_SHIFT), (tide_asset_origin){data, (uint32_t)first, count});
    }
}

bool tide_asset_origin_reference(const tide_asset_origin *origin)
{
    if (!origin || !origin->data->pack_at) return false;
    const tide_asset_data *data = origin->data;
    if (!tide_pack_file_span(data->pack_at, sizeof(tide_block) + (uint64_t)data->count * data->element)) return false;
    const tide_asset_data *now = tide_asset_pack((tide_list){tide_handle_of(TIDE_IN_SCRATCH, data->pack_at)}, data->element);
    return now && memcmp(now->id, data->id, 32) == 0;
}

bool tide_asset_origin_bytes(const tide_asset_origin *origin, void *bytes, const uint32_t size)
{
    if (!origin || size != 1u << TIDE_HEAP_PAGE_SHIFT || origin->count > TIDE_LIST_CHUNK
        || (uint64_t)origin->first + origin->count > (uint64_t)origin->data->count * origin->data->element) return false;
    memset(bytes, 0, size);
    tide_block header = {.link = tide_block_link(TIDE_HEAP_PAGE_SHIFT - 4u, 0)};
    memcpy(bytes, &header, sizeof header);
    memcpy((uint8_t *)bytes + sizeof header, origin->data->bytes + origin->first, origin->count);
    return true;
}

// Check registered descriptors before received state is published. Live grid
// chunks overwrite allocation headers with cells, so never scan all heap bytes
// as headers. Free/pending blocks are checked separately and cannot be leased.
bool tide_asset_validate_heap(const tide_heap *heap)
{
    const tide_asset_pool *pool = heap->assets;
    if (!pool || !pool->count) return true; // Ordinary heaps retain their existing unpack path.
    if (pool && pool->count > heap->used / 64u) return false;
    const uint64_t cells = (heap->used + 15u) / 16u;
    if ((cells + 7u) / 8u + 1u > SIZE_MAX) return false;
    uint8_t *released = tide_alloc_zeroed((size_t)((cells + 7u) / 8u + 1u), 1u);
    bool ok = true;
    for (uint32_t chain = 0; ok && chain <= TIDE_HEAP_CLASSES; chain++) {
        uint64_t at = chain == TIDE_HEAP_CLASSES ? heap->pending : heap->free[chain];
        while (at) {
            if (at < 16u || at % 16u || at > heap->used || heap->used - at < sizeof(tide_block)) { ok = false; break; }
            const uint64_t cell = at / 16u;
            const uint8_t bit = (uint8_t)(1u << (cell % 8u));
            if (released[cell / 8u] & bit) { ok = false; break; }
            released[cell / 8u] |= bit;
            at = tide_block_next(tide_heap_block(heap, at));
        }
    }
    for (uint32_t i = 0; ok && pool && i < pool->slots; i++) {
        const tide_asset_entry entry = pool->entries[i];
        for (uint32_t j = 0; ok && entry.data && j < entry.users; j++) {
            const uint64_t at = entry.blocks[j];
            if (at < 16u || at % 16u || at > heap->used || heap->used - at < 64u) { ok = false; break; }
            const uint64_t cell = at / 16u;
            const uint8_t bit = (uint8_t)(1u << (cell % 8u));
            if (released[cell / 8u] & bit) { ok = false; break; }
            released[cell / 8u] |= bit;
            const tide_page *page = heap->page[at >> TIDE_HEAP_PAGE_SHIFT];
            if (64u > page->size - (at - ((uint64_t)page->first << TIDE_HEAP_PAGE_SHIFT))) { ok = false; break; }
            const tide_block *block = tide_heap_block(heap, at);
            if (tide_block_class(block) != 2u || block->b != TIDE_LIST_ASSET || block->a != entry.data->count
                || memcmp(block + 1, entry.data->id, 32) != 0) ok = false;
        }
    }
    free(released);
    return ok;
}
