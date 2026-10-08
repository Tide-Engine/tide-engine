#include "pack_fingerprint.h"
#include "tide/sha256.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fingerprint_pack {
    uint8_t *bytes;
    size_t size;
    tide_asset_fingerprint_desc desc;
} fingerprint_pack;

static uint32_t fingerprint_leaf(tide_pack_builder *b, const uint32_t kind, const bool changed,
                                  const tide_grid_shape *shape, uint32_t *extra)
{
    const char *text = changed ? "four" : "a\xce\xb2z";
    uint8_t hash[32];
    tide_sha256_of(text, strlen(text), hash);
    if (kind == TIDE_PACK_TEXT) return tide_pack_string(b, hash, text, (uint32_t)strlen(text), extra);
    if (kind == TIDE_PACK_BYTES) return tide_pack_bytes(b, hash, text, (uint32_t)strlen(text), extra);
    if (kind == TIDE_PACK_CLIP) return tide_pack_clip(b, hash, text, (uint32_t)strlen(text));
    if (kind == TIDE_PACK_TEXTURE) {
        const uint8_t pixels[16] = {changed ? 7u : 1u, 2u, 3u, 255u, 4u, 5u, 6u, 255u,
                                   8u, 9u, 10u, 255u, 11u, 12u, 13u, 255u};
        return tide_pack_texture(b, hash, 2, 2, pixels);
    }
    tide_color pixels[17] = {0};
    pixels[0] = (tide_color){changed ? 0.5f : 1.0f, 0.0f, 0.0f, 1.0f};
    pixels[16] = (tide_color){0.0f, 1.0f, 0.0f, 1.0f};
    return tide_pack_grid(b, hash, 17, 1, pixels, shape);
}

static bool fingerprint_model(tide_pack_builder *b, const bool skinned, const bool changed, uint32_t offsets[3])
{
    const float vertices[9] = {changed ? 2.0f : 1.0f, 0, 0, 0, 1, 0, 0, 0, 1};
    const uint32_t indices[3] = {2u, 0u, 1u};
    uint8_t hash[32];
    tide_sha256_of(vertices, sizeof vertices, hash);
    if (!skinned) {
        tide_pack_mesh(b, hash, vertices, 3u * sizeof(float), 3u, indices, 3u, offsets);
        return true;
    }
    FILE *f = fopen(PACK_FINGERPRINT_MODEL, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return false; }
    const long length = ftell(f);
    if (length <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return false; }
    uint8_t *source = malloc((size_t)length);
    const bool read = source && fread(source, 1, (size_t)length, f) == (size_t)length;
    fclose(f);
    uint8_t *model = NULL;
    uint32_t size = 0;
    char why[256], note[256];
    const bool decoded = read && tide_skinned_data_decode(source, (size_t)length, true, NULL, NULL, &model, &size, why, note);
    free(source);
    if (!decoded) return false;
    tide_skinned_data_view view;
    if (!tide_skinned_data_open(model, size, &view)) { tide_skinned_data_free(model); return false; }
    float *positions = malloc((size_t)view.vertices * 3u * sizeof(float));
    uint32_t *ix = malloc((size_t)view.indices * sizeof(uint32_t));
    bool valid = positions && ix;
    for (uint32_t i = 0; valid && i < view.vertices; i++) {
        tide_skinned_vertex_data vertex;
        valid = tide_skinned_data_vertex(&view, i, &vertex);
        if (valid) memcpy(positions + (size_t)i * 3u, vertex.position, sizeof vertex.position);
    }
    for (uint32_t i = 0; valid && i < view.indices; i++) valid = tide_skinned_data_index(&view, i, ix + i);
    if (valid) {
        if (changed && view.vertices) positions[0] += 1.0f;
        tide_sha256_of(positions, (size_t)view.vertices * 3u * sizeof(float), hash);
        tide_pack_mesh(b, hash, positions, 3u * sizeof(float), view.vertices, ix, view.indices, offsets + 1);
        uint32_t chars;
        tide_sha256_of(model, size, hash);
        offsets[0] = tide_pack_bytes(b, hash, model, size, &chars);
    }
    free(positions);
    free(ix);
    tide_skinned_data_free(model);
    return valid;
}

static fingerprint_pack fingerprint_fixture(const uint32_t kind, const uint32_t leaf, const bool moved, const bool changed)
{
    fingerprint_pack result = {.desc = {.kind = kind, .leaf_kind = leaf, .vertex_size = 3u * sizeof(float),
                                         .grid_shape = {sizeof(tide_color), 2u, {4u, 4u, 0u}}}};
    tide_pack_builder b = {0};
    uint8_t hash[32];
    uint32_t extra = 0;
    if (moved) {
        uint8_t unrelated[256] = {0};
        unrelated[0] = 77u;
        tide_sha256_of(unrelated, sizeof unrelated, hash);
        const uint32_t at = tide_pack_bytes(&b, hash, unrelated, sizeof unrelated, &extra);
        tide_pack_name(&b, "cosmetic", TIDE_PACK_BYTES, at, sizeof unrelated, extra);
    }
    const bool mesh = kind == TIDE_PACK_MESH || kind == TIDE_PACK_MESH_LIST || kind == TIDE_PACK_MESHES;
    const bool skinned = kind == TIDE_PACK_SKINNED || kind == TIDE_PACK_SKINNEDS || kind == TIDE_PACK_SKINNED_MAP;
    if (mesh || skinned) {
        uint32_t offsets[6] = {0};
        if (!fingerprint_model(&b, skinned, changed, offsets)) {
            size_t ignored;
            free(tide_pack_end(&b, &ignored));
            return result;
        }
        const uint32_t stride = skinned ? 3u : 2u;
        memcpy(offsets + stride, offsets, (size_t)stride * sizeof(uint32_t));
        const uint32_t layout = 0x12345678u;
        if (kind == TIDE_PACK_MESH) tide_pack_name(&b, "chosen", kind, offsets[0], offsets[1], layout);
        else if (kind == TIDE_PACK_MESHES || kind == TIDE_PACK_SKINNED_MAP) {
            uint32_t keys[2], index;
            tide_sha256_of("first", 5u, hash);
            keys[0] = tide_pack_string(&b, hash, "first", 5u, &extra);
            tide_sha256_of("second", 6u, hash);
            keys[1] = tide_pack_string(&b, hash, "second", 6u, &extra);
            const uint32_t at = skinned ? tide_pack_skinned_map(&b, keys, offsets, 2u, &index)
                                       : tide_pack_mesh_map(&b, keys, offsets, 2u, &index);
            tide_pack_name(&b, "chosen", kind, at, index, layout);
        } else {
            const uint32_t count = kind == TIDE_PACK_SKINNED ? 1u : 2u;
            const uint32_t at = skinned ? tide_pack_skinned_list(&b, offsets, count) : tide_pack_mesh_list(&b, offsets, count);
            tide_pack_name(&b, "chosen", kind, at, count, layout);
        }
    } else if (kind == TIDE_PACK_LIST || kind == TIDE_PACK_MAP || kind == TIDE_PACK_TEXTURES || kind == TIDE_PACK_CLIPS) {
        uint32_t offsets[2];
        offsets[0] = fingerprint_leaf(&b, leaf, changed, &result.desc.grid_shape, &extra);
        offsets[1] = offsets[0]; // Two logical elements may share one physical block.
        if (kind == TIDE_PACK_LIST) tide_pack_name(&b, "chosen", kind, tide_pack_list(&b, offsets, 2u), 2u, leaf);
        else {
            uint32_t keys[2], index;
            tide_sha256_of("first", 5u, hash);
            keys[0] = tide_pack_string(&b, hash, "first", 5u, &extra);
            tide_sha256_of("second", 6u, hash);
            keys[1] = tide_pack_string(&b, hash, "second", 6u, &extra);
            const uint32_t at = tide_pack_map(&b, keys, offsets, 2u, &index);
            tide_pack_name(&b, "chosen", kind, at, index, 2u);
        }
    } else if (kind == TIDE_PACK_CUBEMAP) {
        uint32_t faces[TIDE_CUBE_FACES];
        const uint32_t image = fingerprint_leaf(&b, TIDE_PACK_TEXTURE, changed, &result.desc.grid_shape, &extra);
        for (uint32_t i = 0; i < TIDE_CUBE_FACES; i++) faces[i] = image;
        tide_pack_name(&b, "chosen", kind, tide_pack_list(&b, faces, TIDE_CUBE_FACES), TIDE_CUBE_FACES, 2u);
    } else {
        const uint32_t at = fingerprint_leaf(&b, kind, changed, &result.desc.grid_shape, &extra);
        tide_pack_name(&b, "chosen", kind, at, 4u, extra);
    }
    result.bytes = tide_pack_end(&b, &result.size);
    return result;
}

static tide_pack_entry *fingerprint_entry(fingerprint_pack *p)
{
    const tide_pack_header *h = (const tide_pack_header *)(const void *)p->bytes;
    tide_pack_entry *entries = (tide_pack_entry *)(void *)(p->bytes + sizeof *h);
    uint8_t named[32];
    tide_sha256_of("chosen", 6u, named);
    for (uint32_t i = 0; i < h->entries; i++) {
        if (!memcmp(entries[i].name, named, sizeof named)) return entries + i;
    }
    return NULL;
}

static uint8_t *fingerprint_region(fingerprint_pack *p)
{
    return p->bytes + ((const tide_pack_header *)(const void *)p->bytes)->region_at;
}

static bool fingerprint_hash(const fingerprint_pack *p, uint8_t out[32])
{
    return pack_fingerprint(p->bytes, p->size, "chosen", &p->desc, out);
}

TIDE_TEST(pack_fingerprint_all_asset_kinds_ignore_relocation_and_cover_content)
{
    static const uint32_t kinds[][2] = {
        {TIDE_PACK_TEXT, 0}, {TIDE_PACK_BYTES, 0}, {TIDE_PACK_GRID, 0}, {TIDE_PACK_TEXTURE, 0}, {TIDE_PACK_CLIP, 0},
        {TIDE_PACK_LIST, TIDE_PACK_TEXT}, {TIDE_PACK_LIST, TIDE_PACK_BYTES}, {TIDE_PACK_LIST, TIDE_PACK_GRID},
        {TIDE_PACK_LIST, TIDE_PACK_TEXTURE}, {TIDE_PACK_LIST, TIDE_PACK_CLIP}, {TIDE_PACK_MAP, TIDE_PACK_TEXT},
        {TIDE_PACK_MAP, TIDE_PACK_BYTES}, {TIDE_PACK_MAP, TIDE_PACK_GRID}, {TIDE_PACK_TEXTURES, TIDE_PACK_TEXTURE},
        {TIDE_PACK_CLIPS, TIDE_PACK_CLIP}, {TIDE_PACK_CUBEMAP, 0}, {TIDE_PACK_MESH, 0}, {TIDE_PACK_MESH_LIST, 0},
        {TIDE_PACK_MESHES, 0}, {TIDE_PACK_SKINNED, 0}, {TIDE_PACK_SKINNEDS, 0}, {TIDE_PACK_SKINNED_MAP, 0},
    };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        fingerprint_pack plain = fingerprint_fixture(kinds[i][0], kinds[i][1], false, false);
        fingerprint_pack moved = fingerprint_fixture(kinds[i][0], kinds[i][1], true, false);
        fingerprint_pack changed = fingerprint_fixture(kinds[i][0], kinds[i][1], false, true);
        TIDE_REQUIRE(plain.bytes && moved.bytes && changed.bytes);
        uint8_t a[32], b[32], c[32];
        if (!fingerprint_hash(&plain, a) || !fingerprint_hash(&moved, b) || !fingerprint_hash(&changed, c)) {
            printf("    kind %u leaf %u\n", kinds[i][0], kinds[i][1]);
            TIDE_CHECK(false);
        } else {
            TIDE_CHECK(!memcmp(a, b, sizeof a));
            TIDE_CHECK(memcmp(a, c, sizeof a) != 0);
        }
        free(plain.bytes);
        free(moved.bytes);
        free(changed.bytes);
    }
}

TIDE_TEST(pack_fingerprint_excludes_mesh_cache_versions_and_allocation_padding)
{
    fingerprint_pack p = fingerprint_fixture(TIDE_PACK_MESH, 0, false, false);
    TIDE_REQUIRE(p.bytes);
    uint8_t before[32], after[32];
    TIDE_REQUIRE(fingerprint_hash(&p, before));
    tide_pack_entry *e = fingerprint_entry(&p);
    uint8_t *region = fingerprint_region(&p);
    tide_block *vertices = (tide_block *)(void *)(region + e->offset);
    tide_block *indices = (tide_block *)(void *)(region + e->size);
    memset(region + e->offset + vertices->next, 99, sizeof(uint64_t));
    memset(region + e->size + indices->next, 77, sizeof(uint64_t));
    // Three float3 values leave four bytes before the version trailer.
    memset(region + e->offset + sizeof *vertices + 3u * 3u * sizeof(float), 123, 4u);
    ((tide_pack_header *)(void *)p.bytes)->reserved[0] = 42u;
    TIDE_REQUIRE(fingerprint_hash(&p, after));
    TIDE_CHECK(!memcmp(before, after, sizeof before));
    e->extra++;
    TIDE_REQUIRE(fingerprint_hash(&p, after));
    TIDE_CHECK(memcmp(before, after, sizeof before) != 0);
    free(p.bytes);
}

TIDE_TEST(pack_fingerprint_rejects_truncated_and_malformed_raw_spans_without_loading)
{
    fingerprint_pack p = fingerprint_fixture(TIDE_PACK_TEXT, 0, false, false);
    TIDE_REQUIRE(p.bytes);
    TIDE_REQUIRE(tide_pack_load(p.bytes, p.size));
    const tide_pack_entry *loaded = tide_pack_find("chosen", TIDE_PACK_TEXT);
    uint8_t hash[32];
    for (size_t size = 0; size < p.size; size++) {
        uint8_t *cut = malloc(size ? size : 1u);
        if (size) memcpy(cut, p.bytes, size);
        TIDE_CHECK(!pack_fingerprint(cut, size, "chosen", &p.desc, hash));
        free(cut);
    }
    uint8_t *copy = malloc(p.size);
    TIDE_REQUIRE(copy != NULL);
    memcpy(copy, p.bytes, p.size);
    tide_pack_header *h = (tide_pack_header *)(void *)p.bytes;
    h->entries = UINT32_MAX;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    memcpy(p.bytes, copy, p.size);
    h->region_at++;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    memcpy(p.bytes, copy, p.size);
    tide_pack_entry *e = fingerprint_entry(&p);
    e->offset = UINT32_MAX;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    memcpy(p.bytes, copy, p.size);
    e = fingerprint_entry(&p);
    e->extra++;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    memcpy(p.bytes, copy, p.size);
    e = fingerprint_entry(&p);
    tide_block *b = (tide_block *)(void *)(fingerprint_region(&p) + e->offset);
    b->a = UINT32_MAX;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    TIDE_CHECK(tide_pack_find("chosen", TIDE_PACK_TEXT) == loaded);
    TIDE_CHECK(tide_pack_text(loaded).chars == 3);
    tide_pack_unload();
    free(copy);
    free(p.bytes);
}

TIDE_TEST(pack_fingerprint_checks_handle_tags_collection_counts_and_geometry_strides)
{
    static const uint32_t kinds[] = {TIDE_PACK_LIST, TIDE_PACK_MAP, TIDE_PACK_MESH_LIST, TIDE_PACK_SKINNEDS};
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        fingerprint_pack p = fingerprint_fixture(kinds[i], TIDE_PACK_TEXT, false, false);
        TIDE_REQUIRE(p.bytes);
        uint8_t hash[32];
        TIDE_REQUIRE(fingerprint_hash(&p, hash));
        tide_pack_entry *e = fingerprint_entry(&p);
        tide_block *b = (tide_block *)(void *)(fingerprint_region(&p) + e->offset);
        uint32_t *handle = (uint32_t *)(void *)((uint8_t *)(b + 1) + (kinds[i] == TIDE_PACK_MAP ? 12u : 0u));
        const uint32_t valid = *handle;
        *handle = valid & 0x3fffffffu;
        TIDE_CHECK(!fingerprint_hash(&p, hash));
        *handle = valid + 1u;
        TIDE_CHECK(!fingerprint_hash(&p, hash));
        *handle = valid;
        b->a = b->b = UINT32_MAX;
        TIDE_CHECK(!fingerprint_hash(&p, hash));
        free(p.bytes);
    }
    fingerprint_pack p = fingerprint_fixture(TIDE_PACK_MESH, 0, false, false);
    TIDE_REQUIRE(p.bytes);
    uint8_t hash[32];
    tide_pack_entry *e = fingerprint_entry(&p);
    uint8_t *region = fingerprint_region(&p);
    tide_block *b = (tide_block *)(void *)(region + e->offset);
    uint32_t *stride = (uint32_t *)(void *)(region + e->offset + b->next + sizeof(uint64_t));
    *stride = 16u;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    *stride = p.desc.vertex_size;
    uint32_t *indices = (uint32_t *)(void *)(region + e->size + sizeof(tide_block));
    indices[0] = 3u;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    free(p.bytes);
}

TIDE_TEST(pack_fingerprint_checks_grid_directories_chunks_and_image_products)
{
    fingerprint_pack p = fingerprint_fixture(TIDE_PACK_GRID, 0, false, false);
    TIDE_REQUIRE(p.bytes);
    uint8_t hash[32];
    TIDE_REQUIRE(fingerprint_hash(&p, hash));
    tide_pack_entry *e = fingerprint_entry(&p);
    tide_grid_dir *d = (tide_grid_dir *)(void *)(fingerprint_region(&p) + e->offset + sizeof(tide_block));
    tide_grid_record *records = tide_grid_records(d);
    const uint32_t block = records[0].block;
    records[0].block = TIDE_PACK_BASE - 16u;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    records[0].block = block;
    d->room = UINT32_MAX;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    free(p.bytes);
    p = fingerprint_fixture(TIDE_PACK_TEXTURE, 0, false, false);
    TIDE_REQUIRE(p.bytes);
    e = fingerprint_entry(&p);
    int32_t *dimensions = (int32_t *)(void *)(fingerprint_region(&p) + e->offset + sizeof(tide_block));
    dimensions[0] = dimensions[1] = INT32_MAX;
    TIDE_CHECK(!fingerprint_hash(&p, hash));
    free(p.bytes);
}

TIDE_TEST(pack_fingerprint_empty_collections_and_hashed_grid_directory)
{
    for (uint32_t kind = TIDE_PACK_LIST; kind <= TIDE_PACK_MAP; kind++) {
        tide_pack_builder b = {0};
        uint32_t index = 0;
        const uint32_t at = kind == TIDE_PACK_LIST ? tide_pack_list(&b, NULL, 0u) : tide_pack_map(&b, NULL, NULL, 0u, &index);
        tide_pack_name(&b, "chosen", kind, at, kind == TIDE_PACK_LIST ? 0u : index, kind == TIDE_PACK_LIST ? TIDE_PACK_TEXT : 0u);
        fingerprint_pack p = {.desc = {.kind = kind, .leaf_kind = TIDE_PACK_TEXT}};
        p.bytes = tide_pack_end(&b, &p.size);
        uint8_t hash[32];
        TIDE_CHECK(fingerprint_hash(&p, hash));
        free(p.bytes);
    }
    // Tiny chunks reach the hashed directory branch without a large image.
    const uint32_t width = 65537u;
    tide_color *pixels = calloc(width, sizeof *pixels);
    TIDE_REQUIRE(pixels != NULL);
    pixels[0].a = pixels[width - 1u].a = 1.0f;
    const tide_grid_shape shape = {sizeof(tide_color), 2u, {0u, 0u, 0u}};
    tide_pack_builder b = {0};
    uint8_t content[32], before[32], after[32];
    tide_sha256_of(pixels, (size_t)width * sizeof *pixels, content);
    const uint32_t at = tide_pack_grid(&b, content, (int32_t)width, 1, pixels, &shape);
    tide_pack_name(&b, "chosen", TIDE_PACK_GRID, at, 0u, 0u);
    fingerprint_pack p = {.desc = {.kind = TIDE_PACK_GRID, .grid_shape = shape}};
    p.bytes = tide_pack_end(&b, &p.size);
    free(pixels);
    TIDE_REQUIRE(fingerprint_hash(&p, before));
    tide_grid_dir *d = (tide_grid_dir *)(void *)(fingerprint_region(&p) + at + sizeof(tide_block));
    TIDE_CHECK(d->flat == 0u && d->records == 2u);
    const uint32_t block = tide_grid_records(d)[1].block - TIDE_PACK_BASE;
    tide_color *cells = (tide_color *)(void *)(fingerprint_region(&p) + block);
    cells[0].r = 0.25f;
    TIDE_REQUIRE(fingerprint_hash(&p, after));
    TIDE_CHECK(memcmp(before, after, sizeof before) != 0);
    free(p.bytes);
}

TIDE_TEST(pack_fingerprint_grid_edge_cells_belong_to_canonical_chunk_payload)
{
    fingerprint_pack p = fingerprint_fixture(TIDE_PACK_GRID, 0, false, false);
    TIDE_REQUIRE(p.bytes);
    uint8_t before[32], after[32];
    TIDE_REQUIRE(fingerprint_hash(&p, before));
    const tide_pack_entry *e = fingerprint_entry(&p);
    const tide_grid_dir *d = (const tide_grid_dir *)(const void *)(fingerprint_region(&p) + e->offset + sizeof(tide_block));
    const uint32_t block = tide_grid_records(d)[1].block - TIDE_PACK_BASE;
    tide_color *cells = (tide_color *)(void *)(fingerprint_region(&p) + block);
    // The last chunk of this 17x1 image has one visible cell. Its remaining
    // cells are canonically zero payload, distinct from block padding.
    TIDE_CHECK(cells[1].r == 0.0f);
    cells[1].r = 0.25f;
    TIDE_REQUIRE(fingerprint_hash(&p, after));
    TIDE_CHECK(memcmp(before, after, sizeof before) != 0);
    free(p.bytes);
}
