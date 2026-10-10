// Loading a pack (see tide/pack.h): its region at the top of the scratch
// area's offsets, where every thread maps it (tide_pack_use), and its entries
// by name. The builder is pack.c.

#include "tide/pack.h"

#include <stdlib.h>
#include <string.h>

#include "tide/net.h"
#include "tide/page.h"
#include "tide/sha256.h"
#include "tide/skinned_data.h"

#define OFFSET(at) ((at) & 0x3FFFFFFFu)

// A tagged offset into the region.
static uint32_t tagged(const uint32_t offset)
{
    return TIDE_IN_SCRATCH << 30 | (TIDE_PACK_BASE + offset);
}


uint32_t tide_pack_region_slots;
tide_heap tide_pack_still;

// An image as a declaration names it: what a Texture names it by, and where
// its block is, with what the block says of it.
typedef struct packed_texture {
    uint64_t named;   // The declaration, and for one of a glob's, its place or its key: never 0
    uint64_t version; // What's in it: the file's hash
    uint32_t offset;
    int32_t width, height; // An image's; a clip's (`clip`) has its file's size in `width`
    bool clip;
} packed_texture;

// An entry of a map from text to clips, as generated code lays it out.
typedef struct clip_entry {
    int64_t live;
    uint32_t key;
    tide_clip_ref value;
} clip_entry;

// An entry of a map from text to textures, as generated code lays it out:
// live, the key, the value (the builder's has a handle where the value is).
typedef struct texture_entry {
    int64_t live;
    uint32_t key;
    tide_texture_ref value;
} texture_entry;

typedef struct handle_entry {
    int64_t live;
    uint32_t key;
    uint32_t value;
} handle_entry;

// A cube as a declaration names it: its faces' pixels, where the region has them.
typedef struct packed_cube {
    uint64_t named;
    uint64_t version; // What's in its six faces
    int32_t size;
    const uint8_t *faces[TIDE_CUBE_FACES];
} packed_cube;

// Only canonical blobs that passed full skinned-record validation are here.
// Ordinary packed byte lists do not establish this provenance.
typedef struct packed_skinned {
    uint32_t at;
    uint32_t end; // End of its header/payload span, or 0 once a geometry write overlaps it.
    uint64_t version;
    uint64_t identity;
} packed_skinned;

typedef struct skinned_span {
    uint32_t begin, end, slot, prefix_end;
} skinned_span;

static struct {
    uint8_t *region;
    uint32_t size;
    uint32_t file_size; // Before mutable texture/clip references are laid out after it.
    tide_pack_entry *entries;
    uint32_t entry_count;
    uint32_t epoch; // Counts the loads, so threads see a new one
    // Its images by what a Texture names one by: a table of a power of two of places, under half of them
    // taken, each found from its name's low bits and on from there
    packed_texture *textures;
    uint32_t texture_count, texture_places;
    packed_cube *cubes; // Its cubes, of which a game has a few: looked through
    uint32_t cube_count;
    packed_skinned *skinned;
    uint32_t skinned_count, skinned_places;
    skinned_span *skinned_spans;
    uint32_t skinned_span_count;
} pack;

static TIDE_THREAD_LOCAL uint32_t mapped_epoch;
// Never reset on unload: an offset reused by another load is a different proof.
static uint64_t skinned_proof_identity;

// What a Texture names an image by: the declaration (its name's hash, as its
// entry has it), and for one of a glob's, its place in the list or its key
// in the map. Not what's in the image: a Texture a field keeps goes on
// naming the declaration's image when the pack is loaded again with the file
// edited, as the declaration's own name does. Never 0.
#define ALONE UINT32_MAX
#define KEYED (UINT32_MAX - 1u)
static uint64_t texture_name(const uint8_t name[32], const uint32_t place, const void *key, const uint32_t key_size)
{
    tide_sha256 s;
    tide_sha256_init(&s);
    tide_sha256_add(&s, name, 32);
    const uint8_t where[4] = {(uint8_t)place, (uint8_t)(place >> 8), (uint8_t)(place >> 16), (uint8_t)(place >> 24)};
    tide_sha256_add(&s, where, sizeof where);
    if (key_size) tide_sha256_add(&s, key, key_size);
    uint8_t out[32];
    tide_sha256_end(&s, out);
    uint64_t named;
    memcpy(&named, out, sizeof named);
    named &= TIDE_TEXTURE_NAME; // Its top bits are how a Texture is sampled (tide_texture_sampling)
    return named ? named : 1u;
}

// The texture block at `offset` of a region of `size` bytes: its size and
// what's in it, or false for a block that isn't whole there.
static bool texture_at(const uint8_t *region, const uint32_t size, const uint32_t offset, packed_texture *t)
{
    if ((uint64_t)offset + sizeof(tide_block) + 2u * sizeof(int32_t) > size) return false;
    tide_block b; // Copied out: a file's bytes are wherever its reader put them
    int32_t wide[2];
    memcpy(&b, region + offset, sizeof b);
    memcpy(wide, region + offset + sizeof b, sizeof wide);
    if (wide[0] <= 0 || wide[1] <= 0) return false;
    const uint64_t bytes = (uint64_t)wide[0] * (uint64_t)wide[1] * 4u;
    if ((uint64_t)offset + sizeof b + sizeof wide + bytes > size) return false;
    *t = (packed_texture){.version = (uint64_t)b.a | (uint64_t)b.b << 32, .offset = offset, .width = wide[0], .height = wide[1]};
    return true;
}

// A text block's bytes, or NULL with none for one that isn't whole in the region.
static const uint8_t *text_at(const uint8_t *region, const uint32_t size, const uint32_t offset, uint32_t *bytes)
{
    *bytes = 0;
    if (offset == UINT32_MAX || (uint64_t)offset + sizeof(tide_block) > size) return NULL;
    tide_block b;
    memcpy(&b, region + offset, sizeof b);
    if ((uint64_t)offset + sizeof b + b.a > size) return NULL;
    *bytes = b.a;
    return region + offset + sizeof b;
}

// Where a handle of the file's points in its region, or UINT32_MAX for one that points nowhere in it.
static uint32_t place_of(const uint32_t handle, const uint32_t size)
{
    const uint32_t offset = OFFSET(handle);
    return handle >> 30 == TIDE_IN_SCRATCH && offset >= TIDE_PACK_BASE && offset - TIDE_PACK_BASE < size ? offset - TIDE_PACK_BASE
                                                                                                         : UINT32_MAX;
}

// How many elements the list block at `offset` has, each `element` bytes, or 0 for one that isn't whole in the region.
static uint32_t count_at(const uint8_t *region, const uint32_t size, const uint32_t offset, const uint32_t element)
{
    if ((uint64_t)offset + sizeof(tide_block) > size) return 0;
    tide_block b;
    memcpy(&b, region + offset, sizeof b);
    return (uint64_t)offset + sizeof b + (uint64_t)b.a * element <= size ? b.a : 0;
}

// The clip block at `offset` of a region of `size` bytes, or false for a
// block that isn't whole there.
static bool clip_at(const uint8_t *region, const uint32_t size, const uint32_t offset, packed_texture *t)
{
    if ((uint64_t)offset + sizeof(tide_block) + sizeof(uint32_t) > size) return false;
    tide_block b;
    uint32_t bytes;
    memcpy(&b, region + offset, sizeof b);
    memcpy(&bytes, region + offset + sizeof b, sizeof bytes);
    if ((uint64_t)offset + sizeof b + sizeof bytes + bytes > size) return false;
    *t = (packed_texture){.version = (uint64_t)b.a | (uint64_t)b.b << 32, .offset = offset, .width = (int32_t)(bytes & 0x7FFFFFFFu),
                          .clip = true};
    return bytes <= 0x7FFFFFFFu;
}

// A list block at `offset` that says what's in it after its elements (the
// builder's versioned_list: a mesh's vertices, or its indices): how many
// elements, of how many bytes each, and its version. False for a block that
// isn't one, or isn't whole in the region's `size` bytes.
static bool versioned_at(const uint8_t *region, const uint32_t size, const uint32_t offset, uint32_t *count, uint32_t *element,
                         uint64_t *version)
{
    if (offset == UINT32_MAX || (uint64_t)offset + sizeof(tide_block) > size) return false;
    tide_block b;
    memcpy(&b, region + offset, sizeof b);
    if (b.next < sizeof b || (b.b & TIDE_LIST_CHUNKED) || (uint64_t)offset + b.next + 16u > size) return false;
    uint32_t said[2];
    memcpy(version, region + offset + b.next, sizeof *version);
    memcpy(said, region + offset + b.next + sizeof *version, sizeof said);
    if (!*version || (uint64_t)b.a * said[0] > b.next - sizeof b) return false;
    *count = b.a;
    *element = said[0];
    return true;
}

// Whether the two blocks a mesh names are a mesh's: its vertices' and its
// indices', each whole, the indices ints. *vertex gets a vertex's bytes.
static bool mesh_at(const uint8_t *region, const uint32_t size, const uint32_t vertices, const uint32_t indices, uint32_t *vertex,
                    uint32_t *count)
{
    uint32_t index_count, index_bytes;
    uint64_t version;
    return versioned_at(region, size, vertices, count, vertex, &version)
        && versioned_at(region, size, indices, &index_count, &index_bytes, &version) && index_bytes == sizeof(uint32_t);
}

// An entry of a map from text to meshes, as generated code lays it out.
typedef struct mesh_entry {
    int64_t live;
    uint32_t key;
    tide_mesh_lists value;
} mesh_entry;

// The lists and maps of meshes the entries name are read where the file has
// them, as their handles are the same bytes on every platform. A mesh in one
// whose blocks aren't whole is made an empty one, here, once: what reads a
// list's element takes its two lists as they are.
static void check_meshes(void)
{
    for (uint32_t i = 0; i < pack.entry_count; i++) {
        tide_pack_entry *e = &pack.entries[i];
        const bool list = e->kind == TIDE_PACK_MESH_LIST;
        if (!list && e->kind != TIDE_PACK_MESHES) continue;
        const uint32_t each = list ? (uint32_t)sizeof(tide_mesh_lists) : (uint32_t)sizeof(mesh_entry);
        const uint32_t count = count_at(pack.region, pack.size, e->offset, each);
        uint8_t *elements = pack.region + e->offset + sizeof(tide_block);
        for (uint32_t k = 0; k < count; k++) {
            tide_mesh_lists *m = list ? &((tide_mesh_lists *)(void *)elements)[k] : &((mesh_entry *)(void *)elements)[k].value;
            uint32_t vertex, vertices;
            if (!mesh_at(pack.region, pack.size, place_of(m->vertices.at, pack.size), place_of(m->indices.at, pack.size), &vertex,
                         &vertices)) {
                *m = (tide_mesh_lists){{0}, {0}};
            }
        }
        if (!count && e->offset < pack.size) { // A block that isn't whole: a list of none
            tide_block empty = {0};
            if ((uint64_t)e->offset + sizeof empty <= pack.size) {
                memcpy(&empty, pack.region + e->offset, sizeof empty);
                empty.a = 0;
                memcpy(pack.region + e->offset, &empty, sizeof empty);
            } else {
                e->offset = UINT32_MAX;
            }
        }
        if (list) e->size = count;
    }
}

typedef struct skinned_entry {
    int64_t live;
    uint32_t key;
    tide_skinned_lists value;
} skinned_entry;

_Static_assert(sizeof(skinned_entry) == 24u && offsetof(skinned_entry, key) == 8u && offsetof(skinned_entry, value) == 12u,
               "packed skinned map entries must match the generated map layout");

// Skinned records, byte data and map indices are flat immutable lists. Their
// block headers must agree with the storage, including the room and alignment.
static bool flat_in(const uint8_t *region, const uint32_t size, const uint32_t offset, const uint32_t element, uint32_t *count)
{
    *count = 0;
    if ((offset & 15u) || (uint64_t)offset + sizeof(tide_block) > size) return false;
    tide_block b;
    memcpy(&b, region + offset, sizeof b);
    if (b.a != b.b || b.a > INT32_MAX || (uint64_t)offset + sizeof b + (uint64_t)b.a * element > size) return false;
    *count = b.a;
    return true;
}

static bool flat_at(const uint32_t offset, const uint32_t element, uint32_t *count)
{
    return flat_in(pack.region, pack.size, offset, element, count);
}

static uint32_t skinned_place(const packed_skinned *slots, const uint32_t places, const uint32_t at)
{
    uint32_t i = (uint32_t)tide_hash_i((int32_t)at) & (places - 1u);
    while (slots[i].at && slots[i].at != at) i = (i + 1u) & (places - 1u);
    return i;
}

static void note_skinned(const tide_list data)
{
    if (pack.skinned_places && pack.skinned[skinned_place(pack.skinned, pack.skinned_places, data.at)].at) return;
    if ((uint64_t)(pack.skinned_count + 1u) * 2u > pack.skinned_places) {
        const uint32_t places = pack.skinned_places ? pack.skinned_places * 2u : 8u;
        packed_skinned *slots = tide_alloc_zeroed(places, sizeof *slots);
        for (uint32_t i = 0; i < pack.skinned_places; i++) {
            const packed_skinned had = pack.skinned[i];
            if (had.at) slots[skinned_place(slots, places, had.at)] = had;
        }
        free(pack.skinned);
        pack.skinned = slots;
        pack.skinned_places = places;
    }
    // Keep a candidate, not a proof yet: a later malformed record may alias
    // this byte block when sanitizing writes clear that record.
    pack.skinned[skinned_place(pack.skinned, pack.skinned_places, data.at)] = (packed_skinned){.at = data.at};
    pack.skinned_count++;
}

static int compare_skinned_spans(const void *a, const void *b)
{
    const skinned_span *left = a, *right = b;
    return left->begin < right->begin ? -1 : left->begin > right->begin ? 1 : 0;
}

static void index_skinned_spans(void)
{
    if (!pack.skinned_count) return;
    pack.skinned_spans = tide_alloc((size_t)pack.skinned_count * sizeof *pack.skinned_spans);
    uint32_t count = 0;
    for (uint32_t i = 0; i < pack.skinned_places; i++) {
        packed_skinned *model = &pack.skinned[i];
        if (!model->at) continue;
        const uint32_t offset = OFFSET(model->at) - TIDE_PACK_BASE;
        uint32_t bytes;
        if (!flat_in(pack.region, pack.file_size, offset, 1u, &bytes)
            || !tide_skinned_data_validate(pack.region + offset + sizeof(tide_block), bytes)) continue;
        // Exhausting identities disables this optimization rather than reusing
        // a proof that a live draw list could still name.
        if (skinned_proof_identity == UINT64_MAX) continue;
        // Establish the proof only after every load-time sanitizing write.
        model->end = offset + (uint32_t)sizeof(tide_block) + bytes;
        model->version = tide_hash(pack.region + offset + sizeof(tide_block), bytes);
        model->identity = ++skinned_proof_identity;
        pack.skinned_spans[count++] = (skinned_span){offset, model->end, i, 0u};
    }
    pack.skinned_span_count = count;
    qsort(pack.skinned_spans, count, sizeof *pack.skinned_spans, compare_skinned_spans);
    uint32_t end = 0;
    for (uint32_t i = 0; i < count; i++) {
        skinned_span *span = &pack.skinned_spans[i];
        if (span->end > end) end = span->end;
        span->prefix_end = end;
    }
}

// Generated defaults normally write disjoint geometry blocks. A forged raw
// pack can alias one with a canonical byte block, so invalidate only those
// proofs whose physical storage may have changed. Sorted spans keep ordinary
// default fills logarithmic even when thousands of models are imported.
static void changed_skinned_span(const uint32_t begin, const uint32_t end)
{
    uint32_t low = 0u, high = pack.skinned_span_count;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;
        if (pack.skinned_spans[middle].prefix_end <= begin) low = middle + 1u;
        else high = middle;
    }
    for (uint32_t i = low; i < pack.skinned_span_count && pack.skinned_spans[i].begin < end; i++) {
        const skinned_span span = pack.skinned_spans[i];
        if (span.end > begin) pack.skinned[span.slot].end = 0u;
    }
}

static bool skinned_at(const tide_skinned_lists *model, const bool full, uint32_t *vertex, uint32_t *count)
{
    const uint32_t data = place_of(model->data.at, pack.size), vertices = place_of(model->vertices.at, pack.size);
    const uint32_t indices = place_of(model->indices.at, pack.size);
    uint32_t bytes, index_count, index_size;
    uint64_t version;
    tide_skinned_data_view view;
    if (!flat_at(data, 1u, &bytes) || (vertices & 15u) || (indices & 15u)
        || !tide_skinned_data_open(pack.region + data + sizeof(tide_block), bytes, &view)
        || (full && !tide_skinned_data_validate(view.data, bytes))
        || !mesh_at(pack.region, pack.size, vertices, indices, vertex, count) || !*vertex
        || !versioned_at(pack.region, pack.size, indices, &index_count, &index_size, &version)
        || *count != view.vertices || index_count != view.indices) return false;
    if (full) {
        // Typed geometry and the influence table address the same vertices.
        // The separately packed indices must also match the canonical ones.
        for (uint32_t i = 0; i < index_count; i++) {
            uint32_t actual, expected;
            memcpy(&actual, pack.region + indices + sizeof(tide_block) + (size_t)i * sizeof actual, sizeof actual);
            if (!tide_skinned_data_index(&view, i, &expected) || actual != expected) return false;
        }
    }
    return true;
}

static bool skinned_map_index_in(const uint8_t *region, const uint32_t size, const tide_pack_entry *e, const uint32_t count)
{
    uint32_t slots, records;
    if (!flat_in(region, size, e->offset, sizeof(skinned_entry), &records) || records != count
        || !flat_in(region, size, e->size, sizeof(uint32_t), &slots) || slots < 8u || (slots & (slots - 1u))
        || (uint64_t)count * 2u > slots) return false;
    const uint8_t *index = region + e->size + sizeof(tide_block);
    uint32_t live = 0;
    for (uint32_t i = 0; i < slots; i++) {
        uint32_t at;
        memcpy(&at, index + (size_t)i * sizeof at, sizeof at);
        if (at > count) return false;
        if (at) live++;
    }
    if (live != count) return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *row = region + e->offset + sizeof(tide_block) + (size_t)i * sizeof(skinned_entry);
        int64_t order;
        uint32_t key, length;
        memcpy(&order, row, sizeof order);
        memcpy(&key, row + offsetof(skinned_entry, key), sizeof key);
        const uint32_t at = place_of(key, size);
        const uint8_t *text = (at & 15u) ? NULL : text_at(region, size, at, &length);
        if (order != (int64_t)i + 1 || !text) return false;
        // Every key must reach its own row before an empty slot. Together
        // with the live-slot count this also refuses duplicates and omissions.
        uint32_t slot = tide_map_hash_bytes(text, length) & (slots - 1u);
        for (;;) {
            uint32_t number;
            memcpy(&number, index + (size_t)slot * sizeof number, sizeof number);
            if (!number) return false;
            if (number == i + 1u) break;
            slot = (slot + 1u) & (slots - 1u);
        }
    }
    return true;
}

static bool skinned_map_index(const tide_pack_entry *e, const uint32_t count)
{
    return skinned_map_index_in(pack.region, pack.size, e, count);
}

static void check_skinned(void)
{
    for (uint32_t i = 0; i < pack.entry_count; i++) {
        tide_pack_entry *e = &pack.entries[i];
        const bool map = e->kind == TIDE_PACK_SKINNED_MAP;
        if (!map && e->kind != TIDE_PACK_SKINNED && e->kind != TIDE_PACK_SKINNEDS) continue;
        const uint32_t each = map ? (uint32_t)sizeof(skinned_entry) : (uint32_t)sizeof(tide_skinned_lists);
        uint32_t count;
        if (!flat_at(e->offset, each, &count) || (e->kind == TIDE_PACK_SKINNED && count != 1u)
            || (map && !skinned_map_index(e, count))) {
            e->offset = UINT32_MAX;
            if (!map) e->size = 0;
            continue;
        }
        for (uint32_t k = 0; k < count; k++) {
            uint8_t *row = pack.region + e->offset + sizeof(tide_block) + (size_t)k * each;
            tide_skinned_lists model;
            const uint32_t value_at = map ? (uint32_t)offsetof(skinned_entry, value) : 0u;
            memcpy(&model, row + value_at, sizeof model);
            uint32_t vertex, vertices;
            if (!skinned_at(&model, true, &vertex, &vertices)) {
                model = (tide_skinned_lists){0};
                memcpy(row + value_at, &model, sizeof model);
            } else note_skinned(model.data);
        }
        if (!map) e->size = count;
    }
}

// A block's bytes with its header, as the builder pads them.
static uint64_t padded(const uint64_t bytes)
{
    return (sizeof(tide_block) + bytes + 15u) & ~15ull;
}

// The lists and maps of textures the entries name, which Tide reads as
// Textures side by side: how many bytes they take laid out so.
static uint64_t textures_room(const uint8_t *region, const uint32_t size, const tide_pack_entry *entries, const uint32_t count)
{
    uint64_t room = 0;
    for (uint32_t i = 0; i < count; i++) {
        const tide_pack_entry *e = &entries[i];
        if (e->kind == TIDE_PACK_LIST && e->extra == TIDE_PACK_TEXTURE) {
            room += padded((uint64_t)count_at(region, size, e->offset, sizeof(uint32_t)) * sizeof(tide_texture_ref));
        } else if (e->kind == TIDE_PACK_TEXTURES) {
            room += padded((uint64_t)count_at(region, size, e->offset, sizeof(handle_entry)) * sizeof(texture_entry));
        } else if (e->kind == TIDE_PACK_LIST && e->extra == TIDE_PACK_CLIP) {
            room += padded((uint64_t)count_at(region, size, e->offset, sizeof(uint32_t)) * sizeof(tide_clip_ref));
        } else if (e->kind == TIDE_PACK_CLIPS) {
            room += padded((uint64_t)count_at(region, size, e->offset, sizeof(handle_entry)) * sizeof(clip_entry));
        }
    }
    return room;
}

// A block after the ones there, as the builder makes one: its offset.
static uint32_t lay_block(const uint32_t count, const uint64_t bytes)
{
    const uint32_t at = pack.size;
    uint32_t c = 0;
    while (c + 1 < TIDE_HEAP_CLASSES && ((uint64_t)16 << c) < sizeof(tide_block) + bytes) c++;
    memset(pack.region + at, 0, (size_t)padded(bytes));
    tide_block *b = (tide_block *)(void *)(pack.region + at);
    b->size_class = c;
    b->a = count;
    b->b = count;
    pack.size += (uint32_t)padded(bytes);
    return at;
}

// The table's place for the image named so: the image's, or the free one it would take.
static packed_texture *texture_place(const uint64_t named)
{
    const uint32_t mask = pack.texture_places - 1u;
    uint32_t i = (uint32_t)named & mask;
    while (pack.textures[i].named && pack.textures[i].named != named) i = (i + 1u) & mask;
    return &pack.textures[i];
}

// Puts `t` in the table under `named`, growing it as it needs.
static void note_named(packed_texture t, const uint64_t named)
{
    t.named = named;
    if ((pack.texture_count + 1u) * 2u > pack.texture_places) {
        packed_texture *had = pack.textures;
        const uint32_t had_places = pack.texture_places;
        pack.texture_places = had_places ? had_places * 2u : 32u;
        pack.textures = tide_alloc_zeroed(pack.texture_places, sizeof *pack.textures);
        for (uint32_t i = 0; i < had_places; i++) {
            if (had[i].named) *texture_place(had[i].named) = had[i];
        }
        free(had);
    }
    packed_texture *place = texture_place(named);
    if (!place->named) pack.texture_count++;
    *place = t;
}

// Notes the image at `offset` of the file's `file` bytes under what a Texture names it by, and gives that
// Texture: the default one for a block that isn't whole, which nothing is noted for.
static tide_texture_ref note_texture(const uint64_t named, const uint32_t offset, const uint32_t file)
{
    packed_texture t;
    if (offset == UINT32_MAX || !texture_at(pack.region, file, offset, &t)) return (tide_texture_ref){0};
    note_named(t, named);
    return (tide_texture_ref){.image = {.pixels = NULL, .width = t.width, .height = t.height, .version = named}};
}

// The same for a clip's block, and the Clip that names it.
static tide_clip_ref note_clip(const uint64_t named, const uint32_t offset, const uint32_t file)
{
    packed_texture t;
    if (offset == UINT32_MAX || !clip_at(pack.region, file, offset, &t)) return (tide_clip_ref){0};
    note_named(t, named);
    return (tide_clip_ref){.name = named};
}

// Lays the lists and maps of textures out as Tide reads them, after the
// file's region (whose `file` bytes they're made from), and has their entries
// name those. Notes every image on the way.
static void lay_textures(const uint32_t file)
{
    for (uint32_t i = 0; i < pack.entry_count; i++) {
        tide_pack_entry *e = &pack.entries[i];
        if (e->kind == TIDE_PACK_TEXTURE) {
            note_texture(texture_name(e->name, ALONE, NULL, 0), e->offset, file);
        } else if (e->kind == TIDE_PACK_LIST && e->extra == TIDE_PACK_TEXTURE) {
            const uint32_t count = count_at(pack.region, file, e->offset, sizeof(uint32_t));
            const uint32_t at = lay_block(count, (uint64_t)count * sizeof(tide_texture_ref));
            const uint32_t *handles = (const uint32_t *)(const void *)(pack.region + e->offset + sizeof(tide_block));
            tide_texture_ref *out = (tide_texture_ref *)(void *)(pack.region + at + sizeof(tide_block));
            for (uint32_t k = 0; k < count; k++) {
                out[k] = note_texture(texture_name(e->name, k, NULL, 0), place_of(handles[k], file), file);
            }
            e->offset = at;
            e->size = count;
        } else if (e->kind == TIDE_PACK_TEXTURES) {
            const uint32_t count = count_at(pack.region, file, e->offset, sizeof(handle_entry));
            const uint32_t at = lay_block(count, (uint64_t)count * sizeof(texture_entry));
            const handle_entry *from = (const handle_entry *)(const void *)(pack.region + e->offset + sizeof(tide_block));
            texture_entry *out = (texture_entry *)(void *)(pack.region + at + sizeof(tide_block));
            for (uint32_t k = 0; k < count; k++) {
                uint32_t key_size;
                const uint8_t *key = text_at(pack.region, file, place_of(from[k].key, file), &key_size);
                out[k].live = from[k].live;
                out[k].key = from[k].key;
                out[k].value = note_texture(texture_name(e->name, KEYED, key, key_size), place_of(from[k].value, file), file);
            }
            e->offset = at;
            e->extra = e->extra > count ? count : e->extra; // Its live count
        } else if (e->kind == TIDE_PACK_CLIP) {
            note_clip(texture_name(e->name, ALONE, NULL, 0), e->offset, file);
        } else if (e->kind == TIDE_PACK_LIST && e->extra == TIDE_PACK_CLIP) {
            const uint32_t count = count_at(pack.region, file, e->offset, sizeof(uint32_t));
            const uint32_t at = lay_block(count, (uint64_t)count * sizeof(tide_clip_ref));
            const uint32_t *handles = (const uint32_t *)(const void *)(pack.region + e->offset + sizeof(tide_block));
            tide_clip_ref *out = (tide_clip_ref *)(void *)(pack.region + at + sizeof(tide_block));
            for (uint32_t k = 0; k < count; k++) {
                out[k] = note_clip(texture_name(e->name, k, NULL, 0), place_of(handles[k], file), file);
            }
            e->offset = at;
            e->size = count;
        } else if (e->kind == TIDE_PACK_CLIPS) {
            const uint32_t count = count_at(pack.region, file, e->offset, sizeof(handle_entry));
            const uint32_t at = lay_block(count, (uint64_t)count * sizeof(clip_entry));
            const handle_entry *from = (const handle_entry *)(const void *)(pack.region + e->offset + sizeof(tide_block));
            clip_entry *out = (clip_entry *)(void *)(pack.region + at + sizeof(tide_block));
            for (uint32_t k = 0; k < count; k++) {
                uint32_t key_size;
                const uint8_t *key = text_at(pack.region, file, place_of(from[k].key, file), &key_size);
                out[k].live = from[k].live;
                out[k].key = from[k].key;
                out[k].value = note_clip(texture_name(e->name, KEYED, key, key_size), place_of(from[k].value, file), file);
            }
            e->offset = at;
            e->extra = e->extra > count ? count : e->extra;
        }
    }
}

// Notes the cubes the entries name: each one's six faces, where they're whole, square and of one size.
static void note_cubes(void)
{
    uint32_t most = 0;
    for (uint32_t i = 0; i < pack.entry_count; i++) most += pack.entries[i].kind == TIDE_PACK_CUBEMAP;
    if (!most) return;
    pack.cubes = tide_alloc_zeroed(most, sizeof *pack.cubes);
    for (uint32_t i = 0; i < pack.entry_count; i++) {
        const tide_pack_entry *e = &pack.entries[i];
        if (e->kind != TIDE_PACK_CUBEMAP || count_at(pack.region, pack.size, e->offset, sizeof(uint32_t)) != TIDE_CUBE_FACES) continue;
        uint32_t handles[TIDE_CUBE_FACES];
        memcpy(handles, pack.region + e->offset + sizeof(tide_block), sizeof handles);
        packed_cube cube = {.named = texture_name(e->name, ALONE, NULL, 0)};
        tide_sha256 s;
        tide_sha256_init(&s);
        bool whole = true;
        for (uint32_t face = 0; face < TIDE_CUBE_FACES && whole; face++) {
            packed_texture t;
            const uint32_t at = place_of(handles[face], pack.size);
            whole = at != UINT32_MAX && texture_at(pack.region, pack.size, at, &t) && t.width == t.height
                 && (face == 0 || t.width == cube.size);
            if (!whole) break;
            cube.size = t.width;
            cube.faces[face] = pack.region + at + sizeof(tide_block) + 2u * sizeof(int32_t);
            tide_sha256_add(&s, &t.version, sizeof t.version);
        }
        if (!whole) continue;
        uint8_t out[32];
        tide_sha256_end(&s, out);
        memcpy(&cube.version, out, sizeof cube.version);
        if (!cube.version) cube.version = 1u;
        pack.cubes[pack.cube_count++] = cube;
    }
}

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
    // Its lists and maps of textures are laid out after the file's region, as Tide reads them
    const uint64_t whole = (uint64_t)h.region_size + textures_room(bytes + h.region_at, h.region_size, entries, h.entries);
    if (whole > TIDE_PACK_BASE) return false;
    tide_pack_unload();
    // The region in memory of its own, a whole number of slots, so that the
    // last slot's pointer reaches memory wherever an offset in it points
    const uint32_t slots = (uint32_t)((whole + TIDE_SCRATCH_BYTES - 1u) >> TIDE_SCRATCH_SHIFT);
    pack.region = tide_alloc(slots ? (size_t)slots << TIDE_SCRATCH_SHIFT : 16u);
    memcpy(pack.region, bytes + h.region_at, h.region_size);
    pack.size = h.region_size;
    pack.file_size = h.region_size;
    pack.entries = tide_alloc(h.entries ? h.entries * sizeof(tide_pack_entry) : 16u);
    memcpy(pack.entries, entries, (size_t)h.entries * sizeof(tide_pack_entry));
    pack.entry_count = h.entries;
    lay_textures(h.region_size);
    check_meshes();
    check_skinned();
    index_skinned_spans();
    note_cubes(); // Once nothing more is laid out: the region is where it stays
    pack.epoch++;
    tide_pack_region_slots = slots;
    tide_pack_use();
    return true;
}

void tide_pack_unload(void)
{
    free(pack.region);
    free(pack.entries);
    free(pack.textures);
    free(pack.cubes);
    free(pack.skinned);
    free(pack.skinned_spans);
    pack.skinned = NULL;
    pack.skinned_spans = NULL;
    pack.skinned_count = pack.skinned_places = 0;
    pack.skinned_span_count = 0;
    pack.cubes = NULL;
    pack.cube_count = 0;
    pack.region = NULL;
    pack.entries = NULL;
    pack.textures = NULL;
    pack.texture_count = pack.texture_places = 0;
    pack.size = 0;
    pack.file_size = 0;
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
    return e && e->offset != UINT32_MAX ? tagged(e->offset) : 0u;
}

const tide_pack_entry *tide_pack_find_mesh(const char *qualified, const uint32_t kind, const uint32_t layout)
{
    const tide_pack_entry *e = tide_pack_find(qualified, kind);
    return e && e->extra == layout ? e : NULL;
}

tide_map tide_pack_mesh_map_of(const tide_pack_entry *e)
{
    if (!e || e->kind != TIDE_PACK_MESHES || e->offset == UINT32_MAX) return (tide_map){0};
    const uint32_t count = count_at(pack.region, pack.size, e->offset, sizeof(mesh_entry));
    return (tide_map){.entries = {tagged(e->offset)}, .index = {tagged(e->size)}, .live = count, .last_order = count};
}

tide_mesh_lists tide_pack_mesh_of(const tide_pack_entry *e, const uint32_t vertex)
{
    uint32_t said, count;
    if (!e || e->kind != TIDE_PACK_MESH || !mesh_at(pack.region, pack.size, e->offset, e->size, &said, &count) || said != vertex) {
        return (tide_mesh_lists){{0}, {0}};
    }
    return (tide_mesh_lists){{tagged(e->offset)}, {tagged(e->size)}};
}

void *tide_pack_mesh_open(const tide_pack_entry *e, const uint32_t k, const uint32_t vertex, uint32_t *count)
{
    static uint8_t none;
    *count = 0;
    if (!e || e < pack.entries || e >= pack.entries + pack.entry_count) return NULL;
    uint32_t vertices = UINT32_MAX, indices = UINT32_MAX;
    if (e->kind == TIDE_PACK_MESH) {
        if (k > 0) return NULL;
        vertices = e->offset, indices = e->size;
    } else if (e->kind == TIDE_PACK_MESH_LIST || e->kind == TIDE_PACK_MESHES) {
        const bool list = e->kind == TIDE_PACK_MESH_LIST;
        const uint32_t each = list ? (uint32_t)sizeof(tide_mesh_lists) : (uint32_t)sizeof(mesh_entry);
        if (e->offset == UINT32_MAX || k >= count_at(pack.region, pack.size, e->offset, each)) return NULL;
        const uint8_t *elements = pack.region + e->offset + sizeof(tide_block);
        const tide_mesh_lists *m = list ? &((const tide_mesh_lists *)(const void *)elements)[k]
                                        : &((const mesh_entry *)(const void *)elements)[k].value;
        vertices = place_of(m->vertices.at, pack.size), indices = place_of(m->indices.at, pack.size);
    } else {
        return NULL;
    }
    uint32_t said, there;
    if (!mesh_at(pack.region, pack.size, vertices, indices, &said, &there) || said != vertex) return &none;
    *count = there;
    return pack.region + vertices + sizeof(tide_block);
}

void tide_pack_mesh_updated(void *vertices, const uint32_t vertex)
{
    if (!pack.region || !vertices) return;
    const uintptr_t begin = (uintptr_t)pack.region, at = (uintptr_t)vertices;
    if (at < begin || at - begin < sizeof(tide_block) || at - begin > pack.size) return;
    const uint32_t offset = (uint32_t)(at - begin - sizeof(tide_block));
    uint32_t count, said;
    uint64_t version;
    if (!versioned_at(pack.region, pack.size, offset, &count, &said, &version) || said != vertex) return;
    // Defaults can read other assets: their resulting bytes, rather than the
    // unchanged model's file hash, tell a draw whether its cached copy is current.
    // Hash once after filling, never while drawing; shared blocks and repeated
    // loads of the same values keep the same version.
    version = tide_hash(vertices, (size_t)count * vertex);
    if (!version) version = 1;
    const tide_block *b = (const tide_block *)(const void *)(pack.region + offset);
    changed_skinned_span(offset, offset + b->next + (uint32_t)sizeof version);
    memcpy(pack.region + offset + b->next, &version, sizeof version);
}

// Locate a record only through an entry of the current pack. Comparisons use
// integer addresses, so a stale or separately copied entry is safe to reject.
static bool skinned_record(const tide_pack_entry *e, const uint32_t k, tide_skinned_lists *model)
{
    const uintptr_t at = (uintptr_t)e, first = (uintptr_t)pack.entries;
    if (!e || !pack.entries || at < first || at - first >= (size_t)pack.entry_count * sizeof *e
        || (at - first) % sizeof *e) return false;
    const bool map = e->kind == TIDE_PACK_SKINNED_MAP;
    if (!map && e->kind != TIDE_PACK_SKINNED && e->kind != TIDE_PACK_SKINNEDS) return false;
    const uint32_t each = map ? (uint32_t)sizeof(skinned_entry) : (uint32_t)sizeof(tide_skinned_lists);
    uint32_t count;
    if (!flat_at(e->offset, each, &count) || k >= count) return false;
    const uint8_t *row = pack.region + e->offset + sizeof(tide_block) + (size_t)k * each;
    memcpy(model, row + (map ? offsetof(skinned_entry, value) : 0u), sizeof *model);
    return true;
}

tide_skinned_lists tide_pack_skinned_of(const tide_pack_entry *e, const uint32_t vertex)
{
    tide_skinned_lists model;
    uint32_t said, count;
    if (!skinned_record(e, 0u, &model) || e->kind != TIDE_PACK_SKINNED || !skinned_at(&model, false, &said, &count)
        || said != vertex) return (tide_skinned_lists){0};
    return model;
}

tide_map tide_pack_skinned_map_of(const tide_pack_entry *e)
{
    tide_skinned_lists unused;
    uint32_t count;
    if (!e || e->kind != TIDE_PACK_SKINNED_MAP || !flat_at(e->offset, sizeof(skinned_entry), &count)
        || (count && !skinned_record(e, 0u, &unused)) || !skinned_map_index(e, count)) return (tide_map){0};
    return (tide_map){.entries = {tagged(e->offset)}, .index = {tagged(e->size)}, .live = count, .last_order = count};
}

void *tide_pack_skinned_open(const tide_pack_entry *e, const uint32_t k, const uint32_t vertex, uint32_t *count)
{
    static uint8_t none;
    *count = 0;
    tide_skinned_lists model;
    if (!skinned_record(e, k, &model)) return NULL;
    uint32_t said, there;
    if (!skinned_at(&model, false, &said, &there) || said != vertex) return &none;
    *count = there;
    return pack.region + place_of(model.vertices.at, pack.size) + sizeof(tide_block);
}

bool tide_pack_skinned_content(const uint8_t *bytes, const size_t size, const char *qualified, const uint32_t kind,
                               const uint32_t layout, const uint8_t *hashes, const uint32_t count)
{
    if (!bytes || !qualified || (!hashes && count) || size < sizeof(tide_pack_header)
        || (kind != TIDE_PACK_SKINNED && kind != TIDE_PACK_SKINNEDS && kind != TIDE_PACK_SKINNED_MAP)
        || (kind == TIDE_PACK_SKINNED && count != 1u)) return false;
    tide_pack_header header;
    memcpy(&header, bytes, sizeof header);
    const uint64_t table_end = (uint64_t)sizeof header + (uint64_t)header.entries * sizeof(tide_pack_entry);
    if (memcmp(header.magic, TIDE_PACK_MAGIC, sizeof header.magic) || header.version != TIDE_PACK_VERSION
        || table_end > header.region_at || (header.region_at & 15u) || header.region_size > TIDE_PACK_BASE
        || (uint64_t)header.region_at + header.region_size != size) return false;
    uint8_t named[32];
    tide_sha256_of(qualified, strlen(qualified), named);
    tide_pack_entry found = {0};
    bool had = false;
    for (uint32_t i = 0; i < header.entries; i++) {
        tide_pack_entry e;
        memcpy(&e, bytes + sizeof header + (size_t)i * sizeof e, sizeof e);
        if (memcmp(e.name, named, sizeof named)) continue;
        if (had || e.kind != kind || e.extra != layout) return false;
        found = e;
        had = true;
    }
    if (!had) return false;
    const uint8_t *region = bytes + header.region_at;
    const bool map = kind == TIDE_PACK_SKINNED_MAP;
    const uint32_t each = map ? (uint32_t)sizeof(skinned_entry) : (uint32_t)sizeof(tide_skinned_lists);
    uint32_t records;
    if (!flat_in(region, header.region_size, found.offset, each, &records) || records != count
        || (!map && found.size != count)) return false;
    if (map && !skinned_map_index_in(region, header.region_size, &found, count)) return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *row = region + found.offset + sizeof(tide_block) + (size_t)i * each;
        tide_sha256 content;
        tide_sha256_init(&content);
        if (map) {
            uint32_t key, length;
            memcpy(&key, row + offsetof(skinned_entry, key), sizeof key);
            const uint32_t at = place_of(key, header.region_size);
            const uint8_t *text = (at & 15u) ? NULL : text_at(region, header.region_size, at, &length);
            if (!text) return false;
            tide_sha256_add(&content, text, length);
        }
        tide_skinned_lists model;
        memcpy(&model, row + (map ? offsetof(skinned_entry, value) : 0u), sizeof model);
        const uint32_t at = place_of(model.data.at, header.region_size);
        uint32_t length;
        tide_skinned_data_view view;
        if (!flat_in(region, header.region_size, at, 1u, &length)
            || !tide_skinned_data_open(region + at + sizeof(tide_block), length, &view)) return false;
        uint8_t actual[32];
        tide_sha256_add(&content, view.data, length);
        tide_sha256_end(&content, actual);
        if (memcmp(actual, hashes + (size_t)i * sizeof actual, sizeof actual)) return false;
    }
    return true;
}

bool tide_pack_list_version(const tide_list list, const uint32_t element, uint64_t *version)
{
    if (!pack.region || !tide_pack_holds(list.at)) return false;
    uint32_t count, said;
    return versioned_at(pack.region, pack.size, (list.at & 0x3FFFFFFFu) - TIDE_PACK_BASE, &count, &said, version) && said == element;
}

bool tide_pack_skinned_proof(const tide_list data, uint64_t *identity, uint64_t *version)
{
    if (!pack.region || !pack.skinned_places) return false;
    const packed_skinned *found = &pack.skinned[skinned_place(pack.skinned, pack.skinned_places, data.at)];
    if (!found->at || !found->end) return false;
    if (identity) *identity = found->identity;
    if (version) *version = found->version;
    return true;
}

bool tide_pack_skinned_version(const tide_list data, uint64_t *version)
{
    return version && tide_pack_skinned_proof(data, NULL, version);
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
    return (tide_map){.entries = {tagged(e->offset)}, .index = {tagged(e->size)}, .live = e->extra, .last_order = e->extra};
}

tide_clip_ref tide_pack_clip_of(const tide_pack_entry *e)
{
    if (!e || e->kind != TIDE_PACK_CLIP || !pack.texture_places) return (tide_clip_ref){0};
    const uint64_t named = texture_name(e->name, ALONE, NULL, 0);
    const packed_texture *t = texture_place(named);
    if (t->named != named || !t->clip) return (tide_clip_ref){0};
    return (tide_clip_ref){.name = named};
}

bool tide_pack_clip_find(const uint64_t named, const uint8_t **bytes, uint32_t *size, uint64_t *version)
{
    if (!named || !pack.texture_places) return false;
    const packed_texture *t = texture_place(named);
    if (t->named != named || !t->clip) return false;
    *bytes = pack.region + t->offset + sizeof(tide_block) + sizeof(uint32_t);
    *size = (uint32_t)t->width;
    *version = t->version;
    return true;
}

void tide_pack_clips_each(void (*fn)(const uint8_t *bytes, uint32_t size, uint64_t version, void *context), void *context)
{
    for (uint32_t i = 0; i < pack.texture_places; i++) {
        const packed_texture *t = &pack.textures[i];
        if (!t->named || !t->clip) continue;
        fn(pack.region + t->offset + sizeof(tide_block) + sizeof(uint32_t), (uint32_t)t->width, t->version, context);
    }
}

tide_texture_ref tide_pack_texture_of(const tide_pack_entry *e)
{
    if (!e || e->kind != TIDE_PACK_TEXTURE || !pack.texture_places) return (tide_texture_ref){0};
    const uint64_t named = texture_name(e->name, ALONE, NULL, 0);
    const packed_texture *t = texture_place(named); // Noted as the pack was loaded, if its block is whole
    if (t->named != named || t->clip) return (tide_texture_ref){0};
    return (tide_texture_ref){.image = {.pixels = NULL, .width = t->width, .height = t->height, .version = named}};
}

bool tide_pack_texture_find(const uint64_t named, tide_texture *image)
{
    if (!named || !pack.texture_places) return false;
    const packed_texture *t = texture_place(named);
    if (t->named != named || t->clip) return false;
    *image = (tide_texture){.pixels = pack.region + t->offset + sizeof(tide_block) + 2u * sizeof(int32_t),
                            .width = t->width, .height = t->height, .version = t->version};
    return true;
}

tide_cubemap_ref tide_pack_cubemap_of(const tide_pack_entry *e)
{
    if (!e || e->kind != TIDE_PACK_CUBEMAP) return (tide_cubemap_ref){0};
    return (tide_cubemap_ref){.named = texture_name(e->name, ALONE, NULL, 0)};
}

bool tide_pack_cubemap_find(const uint64_t named, int32_t *size, uint64_t *version, const uint8_t *const **faces)
{
    for (uint32_t i = 0; named && i < pack.cube_count; i++) {
        if (pack.cubes[i].named != named) continue;
        *size = pack.cubes[i].size;
        *version = pack.cubes[i].version;
        *faces = pack.cubes[i].faces;
        return true;
    }
    return false;
}

uint64_t tide_pack_grid_version(const tide_grid g)
{
    const tide_block *b = (const tide_block *)(const void *)tide_pack_span(g.at & 0x3FFFFFFFu, sizeof(tide_block));
    return b ? (uint64_t)b->a | (uint64_t)b->b << 32 : 0u;
}

const uint8_t *tide_pack_span(const uint32_t offset, const uint64_t bytes)
{
    if (!pack.region || offset < TIDE_PACK_BASE) return NULL;
    const uint64_t at = offset - TIDE_PACK_BASE;
    return at + bytes <= pack.size ? pack.region + at : NULL;
}

void tide_pack_sample_textures(const tide_pack_entry *e, const uint32_t sampling)
{
    // Its elements as loading laid them out (lay_textures), whose entry is one of the loaded pack's own
    if (!e || e < pack.entries || e >= pack.entries + pack.entry_count) return;
    if (e->kind == TIDE_PACK_LIST && e->extra == TIDE_PACK_TEXTURE) {
        tide_texture_ref *out = (tide_texture_ref *)(void *)(pack.region + e->offset + sizeof(tide_block));
        for (uint32_t k = 0; k < e->size; k++) out[k] = tide_texture_sampled(out[k], sampling);
    } else if (e->kind == TIDE_PACK_TEXTURES) {
        const tide_block *b = (const tide_block *)(const void *)(pack.region + e->offset);
        texture_entry *out = (texture_entry *)(void *)(pack.region + e->offset + sizeof(tide_block));
        for (uint32_t k = 0; k < b->a; k++) out[k].value = tide_texture_sampled(out[k].value, sampling);
    }
}
