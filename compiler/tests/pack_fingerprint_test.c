#include "pack_fingerprint.h"
#include "tide/sha256.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fingerprint_pack {
    uint8_t *bytes;
    size_t size;
    tide_asset_fingerprint_desc desc;
} fingerprint_pack;

static uint64_t fingerprint_leaf(tide_pack_builder *b, const uint32_t kind, const bool changed,
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

static bool fingerprint_model(tide_pack_builder *b, const bool skinned, const bool changed, uint64_t offsets[3])
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
        const uint64_t at = tide_pack_bytes(&b, hash, unrelated, sizeof unrelated, &extra);
        tide_pack_name(&b, "cosmetic", TIDE_PACK_BYTES, at, sizeof unrelated, extra);
    }
    const bool mesh = kind == TIDE_PACK_MESH || kind == TIDE_PACK_MESH_LIST || kind == TIDE_PACK_MESHES;
    const bool skinned = kind == TIDE_PACK_SKINNED || kind == TIDE_PACK_SKINNEDS || kind == TIDE_PACK_SKINNED_MAP;
    if (mesh || skinned) {
        uint64_t offsets[6] = {0};
        if (!fingerprint_model(&b, skinned, changed, offsets)) {
            size_t ignored;
            free(tide_pack_end(&b, &ignored));
            return result;
        }
        const uint32_t stride = skinned ? 3u : 2u;
        memcpy(offsets + stride, offsets, (size_t)stride * sizeof offsets[0]);
        const uint32_t layout = 0x12345678u;
        if (kind == TIDE_PACK_MESH) tide_pack_name(&b, "chosen", kind, offsets[0], offsets[1], layout);
        else if (kind == TIDE_PACK_MESHES || kind == TIDE_PACK_SKINNED_MAP) {
            uint64_t keys[2], index;
            tide_sha256_of("first", 5u, hash);
            keys[0] = tide_pack_string(&b, hash, "first", 5u, &extra);
            tide_sha256_of("second", 6u, hash);
            keys[1] = tide_pack_string(&b, hash, "second", 6u, &extra);
            const uint64_t at = skinned ? tide_pack_skinned_map(&b, keys, offsets, 2u, &index)
                                       : tide_pack_mesh_map(&b, keys, offsets, 2u, &index);
            tide_pack_name(&b, "chosen", kind, at, index, layout);
        } else {
            const uint32_t count = kind == TIDE_PACK_SKINNED ? 1u : 2u;
            const uint64_t at = skinned ? tide_pack_skinned_list(&b, offsets, count) : tide_pack_mesh_list(&b, offsets, count);
            tide_pack_name(&b, "chosen", kind, at, count, layout);
        }
    } else if (kind == TIDE_PACK_LIST || kind == TIDE_PACK_MAP || kind == TIDE_PACK_TEXTURES || kind == TIDE_PACK_CLIPS) {
        uint64_t offsets[2];
        offsets[0] = fingerprint_leaf(&b, leaf, changed, &result.desc.grid_shape, &extra);
        offsets[1] = offsets[0]; // Two logical elements may share one physical block.
        if (kind == TIDE_PACK_LIST) tide_pack_name(&b, "chosen", kind, tide_pack_list(&b, offsets, 2u), 2u, leaf);
        else {
            uint64_t keys[2], index;
            tide_sha256_of("first", 5u, hash);
            keys[0] = tide_pack_string(&b, hash, "first", 5u, &extra);
            tide_sha256_of("second", 6u, hash);
            keys[1] = tide_pack_string(&b, hash, "second", 6u, &extra);
            const uint64_t at = tide_pack_map(&b, keys, offsets, 2u, &index);
            tide_pack_name(&b, "chosen", kind, at, index, 2u);
        }
    } else if (kind == TIDE_PACK_CUBEMAP) {
        uint64_t faces[TIDE_CUBE_FACES];
        const uint64_t image = fingerprint_leaf(&b, TIDE_PACK_TEXTURE, changed, &result.desc.grid_shape, &extra);
        for (uint32_t i = 0; i < TIDE_CUBE_FACES; i++) faces[i] = image;
        tide_pack_name(&b, "chosen", kind, tide_pack_list(&b, faces, TIDE_CUBE_FACES), TIDE_CUBE_FACES, 2u);
    } else {
        const uint64_t at = fingerprint_leaf(&b, kind, changed, &result.desc.grid_shape, &extra);
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

// A packed skinned map's row, as generated code lays it out: order, the key, the model.
typedef struct fingerprint_skinned_row {
    int64_t live;
    tide_handle key;
    tide_skinned_lists value;
} fingerprint_skinned_row;

// Where a packed skinned list's or map's first model is in its row
static size_t fingerprint_model_at(const uint32_t kind)
{
    return kind == TIDE_PACK_SKINNED_MAP ? offsetof(fingerprint_skinned_row, value) : 0u;
}

// Expected hashes are captured only from an intact builder fixture. The
// runtime function stays the independent oracle for candidate pack checks.
typedef struct fingerprint_canonical_control {
    const char *qualified;
    uint32_t kind, layout, count;
    const uint8_t *hashes;
    uint8_t logical[32];
} fingerprint_canonical_control;

static fingerprint_canonical_control fingerprint_canonical_request(fingerprint_pack *p, uint8_t hashes[2][32])
{
    const tide_pack_entry *e = fingerprint_entry(p);
    const uint8_t *region = fingerprint_region(p);
    const tide_block *records = (const tide_block *)(const void *)(region + e->offset);
    const bool map = e->kind == TIDE_PACK_SKINNED_MAP;
    const uint32_t stride = map ? (uint32_t)sizeof(fingerprint_skinned_row) : (uint32_t)sizeof(tide_skinned_lists);
    for (uint32_t i = 0; i < records->a; i++) {
        const uint8_t *row = (const uint8_t *)(records + 1) + (size_t)i * stride;
        tide_sha256 hash;
        tide_sha256_init(&hash);
        if (map) {
            tide_handle key;
            memcpy(&key, row + offsetof(fingerprint_skinned_row, key), sizeof key);
            const tide_block *text = (const tide_block *)(const void *)(region + tide_handle_offset(key) - TIDE_PACK_BASE);
            tide_sha256_add(&hash, text + 1, text->a);
        }
        tide_skinned_lists model;
        memcpy(&model, row + fingerprint_model_at(e->kind), sizeof model);
        const tide_block *data = (const tide_block *)(const void *)(region + tide_handle_offset(model.data.at) - TIDE_PACK_BASE);
        tide_sha256_add(&hash, data + 1, data->a);
        tide_sha256_end(&hash, hashes[i]);
    }
    fingerprint_canonical_control request = {.qualified = "chosen", .kind = e->kind, .layout = e->extra,
                                             .count = records->a, .hashes = &hashes[0][0]};
    TIDE_CHECK(fingerprint_hash(p, request.logical));
    return request;
}

static bool fingerprint_canonical_matches_oracle(const fingerprint_pack *p, const fingerprint_canonical_control *request)
{
    const bool oracle = tide_pack_skinned_content(p->bytes, p->size, request->qualified, request->kind,
                                                  request->layout, request->hashes, request->count);
    const tide_asset_fingerprint_request selected = {.qualified = request->qualified, .desc = p->desc};
    uint32_t failed = 0;
    const bool actual = pack_fingerprints(p->bytes, p->size, &selected, 1u, NULL,
                                          (const uint8_t (*)[32])request->logical, &failed);
    TIDE_CHECK(actual == oracle);
    TIDE_CHECK(actual ? failed == UINT32_MAX : failed == 0u || failed == UINT32_MAX);
    return actual;
}

TIDE_TEST(pack_skinned_fingerprint_preserves_canonical_record_cut_and_map_index_rejections)
{
    const uint32_t kinds[] = {TIDE_PACK_SKINNED, TIDE_PACK_SKINNEDS, TIDE_PACK_SKINNED_MAP};
    for (size_t kind = 0; kind < sizeof kinds / sizeof kinds[0]; kind++) {
        fingerprint_pack p = fingerprint_fixture(kinds[kind], 0u, true, false);
        TIDE_REQUIRE(p.bytes != NULL);
        uint8_t hashes[2][32];
        const fingerprint_canonical_control request = fingerprint_canonical_request(&p, hashes);
        TIDE_REQUIRE(fingerprint_canonical_matches_oracle(&p, &request));
        for (size_t size = 0; size < p.size; size++) {
            fingerprint_pack cut = p;
            cut.size = size;
            cut.bytes = malloc(size ? size : 1u);
            TIDE_REQUIRE(cut.bytes != NULL);
            memcpy(cut.bytes, p.bytes, size);
            TIDE_CHECK(!fingerprint_canonical_matches_oracle(&cut, &request));
            free(cut.bytes);
        }
        const uint8_t *region = fingerprint_region(&p);
        const tide_pack_entry *e = fingerprint_entry(&p);
        const uint8_t *rows = region + e->offset + sizeof(tide_block);
        tide_skinned_lists model;
        memcpy(&model, rows + fingerprint_model_at(kinds[kind]), sizeof model);
        const uint64_t data_at = tide_handle_offset(model.data.at) - TIDE_PACK_BASE;
        for (uint32_t variant = 0; variant < 11u; variant++) {
            fingerprint_pack malformed = p;
            malformed.bytes = malloc(p.size);
            TIDE_REQUIRE(malformed.bytes != NULL);
            memcpy(malformed.bytes, p.bytes, p.size);
            tide_pack_entry *entry = fingerprint_entry(&malformed);
            uint8_t *body = fingerprint_region(&malformed);
            tide_block *records = (tide_block *)(void *)(body + entry->offset);
            switch (variant) {
            case 0: entry->kind = TIDE_PACK_BYTES; break;
            case 1: entry->extra++; break;
            case 2: entry->offset++; break;
            case 3: records->a++; break;
            case 4: records->b++; break;
            case 5: records->a = records->b = UINT32_MAX; break;
            case 6: records->a = records->b = 0u; break;
            case 7: entry->size = UINT32_MAX; break;
            case 8: {
                const tide_handle invalid = TIDE_PACK_BASE; // Wrong handle tag
                memcpy(body + entry->offset + sizeof(tide_block) + fingerprint_model_at(kinds[kind]), &invalid, sizeof invalid);
                break;
            }
            case 9: ((tide_block *)(void *)(body + data_at))->a = UINT32_MAX; break;
            default: body[data_at + sizeof(tide_block)] ^= 1u; break;
            }
            TIDE_CHECK(!fingerprint_canonical_matches_oracle(&malformed, &request));
            free(malformed.bytes);
        }
        if (kinds[kind] == TIDE_PACK_SKINNED_MAP) {
            for (uint32_t variant = 0; variant < 8u; variant++) {
                fingerprint_pack malformed = p;
                malformed.bytes = malloc(p.size);
                TIDE_REQUIRE(malformed.bytes != NULL);
                memcpy(malformed.bytes, p.bytes, p.size);
                tide_pack_entry *entry = fingerprint_entry(&malformed);
                uint8_t *body = fingerprint_region(&malformed);
                tide_block *index = (tide_block *)(void *)(body + entry->size);
                uint32_t *table = (uint32_t *)(void *)(index + 1);
                uint8_t *row = body + entry->offset + sizeof(tide_block);
                uint32_t first = 0, second = 0;
                for (uint32_t i = 0; i < index->a; i++) {
                    if (table[i] == 1u) first = i;
                    if (table[i] == 2u) second = i;
                }
                switch (variant) {
                case 0: index->a = index->b = 7u; break;
                case 1: table[first] = 3u; break;
                case 2: table[second] = 1u; break;
                case 3: table[first] = 0u; break;
                case 4: {
                    const int64_t invalid = -1;
                    memcpy(row, &invalid, sizeof invalid);
                    break;
                }
                case 5: {
                    const tide_handle invalid = UINT64_MAX;
                    memcpy(row + offsetof(fingerprint_skinned_row, key), &invalid, sizeof invalid);
                    break;
                }
                case 6: {
                    uint32_t empty = 0;
                    while (table[empty]) empty++;
                    table[empty] = table[first];
                    table[first] = 0u; // Keep the live count, break the key's probe chain.
                    break;
                }
                default: {
                    tide_handle key;
                    memcpy(&key, row + offsetof(fingerprint_skinned_row, key), sizeof key);
                    ((tide_block *)(void *)(body + tide_handle_offset(key) - TIDE_PACK_BASE))->a = UINT32_MAX;
                    break;
                }
                }
                TIDE_CHECK(!fingerprint_canonical_matches_oracle(&malformed, &request));
                free(malformed.bytes);
            }
        }
        free(p.bytes);
    }
}

typedef struct fingerprint_default_vertex {
    float position[3], extra;
} fingerprint_default_vertex;

static fingerprint_pack fingerprint_default_model_fixture(const uint32_t kind)
{
    fingerprint_pack p = {.desc = {.kind = kind, .vertex_size = sizeof(fingerprint_default_vertex)}};
    tide_pack_builder builder = {0};
    uint64_t offsets[6];
    if (!fingerprint_model(&builder, true, false, offsets)) return p;
    const tide_block *vertices = (const tide_block *)(const void *)(builder.region + offsets[1]);
    const tide_block *indices = (const tide_block *)(const void *)(builder.region + offsets[2]);
    const uint32_t count = vertices->a, index_count = indices->a;
    fingerprint_default_vertex *values = calloc(count, sizeof *values);
    uint32_t *ix = malloc((size_t)index_count * sizeof *ix);
    if (!values || !ix) { free(values); free(ix); return p; }
    for (uint32_t i = 0; i < count; i++)
        memcpy(values[i].position, (const uint8_t *)(vertices + 1) + (size_t)i * 3u * sizeof(float), sizeof values[i].position);
    memcpy(ix, indices + 1, (size_t)index_count * sizeof *ix);
    uint8_t content[32];
    tide_sha256_of(values, (size_t)count * sizeof *values, content);
    tide_pack_mesh(&builder, content, values, sizeof *values, count, ix, index_count, offsets + 1u);
    free(ix);
    free(values);
    memcpy(offsets + 3u, offsets, 3u * sizeof offsets[0]);
    if (kind == TIDE_PACK_SKINNED_MAP) {
        uint64_t keys[2], index;
        uint32_t chars;
        tide_sha256_of("first", 5u, content);
        keys[0] = tide_pack_string(&builder, content, "first", 5u, &chars);
        tide_sha256_of("second", 6u, content);
        keys[1] = tide_pack_string(&builder, content, "second", 6u, &chars);
        const uint64_t at = tide_pack_skinned_map(&builder, keys, offsets, 2u, &index);
        tide_pack_name(&builder, "chosen", kind, at, index, 0x12345678u);
    } else {
        const uint32_t models = kind == TIDE_PACK_SKINNED ? 1u : 2u;
        const uint64_t at = tide_pack_skinned_list(&builder, offsets, models);
        tide_pack_name(&builder, "chosen", kind, at, models, 0x12345678u);
    }
    p.bytes = tide_pack_end(&builder, &p.size);
    return p;
}

TIDE_TEST(pack_skinned_fingerprint_checks_source_vertices_before_loaded_defaults)
{
    const uint32_t kinds[] = {TIDE_PACK_SKINNED, TIDE_PACK_SKINNEDS, TIDE_PACK_SKINNED_MAP};
    for (size_t kind = 0; kind < sizeof kinds / sizeof kinds[0]; kind++) {
        fingerprint_pack p = fingerprint_default_model_fixture(kinds[kind]);
        TIDE_REQUIRE(p.bytes != NULL);
        uint8_t hashes[2][32];
        const fingerprint_canonical_control control = fingerprint_canonical_request(&p, hashes);
        const tide_asset_fingerprint_request request = {.qualified = "chosen", .desc = p.desc};
        TIDE_REQUIRE(pack_fingerprints(p.bytes, p.size, &request, 1u, NULL,
                                        (const uint8_t (*)[32])control.logical, NULL));
        tide_pack_entry *e = fingerprint_entry(&p);
        uint8_t *region = fingerprint_region(&p);
        tide_skinned_lists model;
        memcpy(&model, region + e->offset + sizeof(tide_block) + fingerprint_model_at(kinds[kind]), sizeof model);
        const uint64_t vertex_at = tide_handle_offset(model.vertices.at) - TIDE_PACK_BASE;
        tide_block *vertex_block = (tide_block *)(void *)(region + vertex_at);
        fingerprint_default_vertex *typed = (fingerprint_default_vertex *)(void *)(vertex_block + 1);
        const float original = typed[0].position[0];
        typed[0].position[0] += 17.0f;
        // Blob-only preflight accepted this changed source-filled attribute.
        TIDE_CHECK(tide_pack_skinned_content(p.bytes, p.size, "chosen", control.kind, control.layout, control.hashes, control.count));
        TIDE_CHECK(!pack_fingerprints(p.bytes, p.size, &request, 1u, NULL,
                                      (const uint8_t (*)[32])control.logical, NULL));
        typed[0].position[0] = original;
        // Runtime cache versions and allocation padding remain irrelevant.
        memset(region + vertex_at + tide_block_next(vertex_block), 91, sizeof(uint64_t));
        const uint64_t index_at = tide_handle_offset(model.indices.at) - TIDE_PACK_BASE;
        tide_block *index_block = (tide_block *)(void *)(region + index_at);
        const size_t payload_end = sizeof(tide_block) + (size_t)index_block->a * sizeof(uint32_t);
        memset(region + index_at + payload_end, 73, (size_t)tide_block_next(index_block) - payload_end);
        TIDE_REQUIRE(pack_fingerprints(p.bytes, p.size, &request, 1u, NULL,
                                        (const uint8_t (*)[32])control.logical, NULL));
        for (uint32_t load = 0; load < 2u; load++) {
            TIDE_REQUIRE(tide_pack_load(p.bytes, p.size));
            const tide_pack_entry *loaded = tide_pack_find_mesh("chosen", kinds[kind], control.layout);
            uint32_t count;
            fingerprint_default_vertex *values = tide_pack_skinned_open(loaded, 0u, sizeof *values, &count);
            TIDE_REQUIRE(values && count == vertex_block->a);
            TIDE_CHECK(values[0].position[0] == original && values[0].extra == 0.0f);
            for (uint32_t i = 0; i < count; i++) values[i].extra = 42.0f;
            tide_pack_mesh_updated(values, sizeof *values);
            TIDE_CHECK(values[0].extra == 42.0f);
            // Default writes affect the loaded copy, so matching input packs
            // remain valid on repeated loads and start from raw zero defaults.
            TIDE_CHECK(pack_fingerprints(p.bytes, p.size, &request, 1u, NULL,
                                          (const uint8_t (*)[32])control.logical, NULL));
        }
        free(p.bytes);
    }
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
            // Mixed descriptors use the same payload walker in batch mode.
            const tide_asset_fingerprint_request requests[2] = {
                {.qualified = "cosmetic", .desc = {.kind = TIDE_PACK_BYTES}}, {.qualified = "chosen", .desc = moved.desc},
            };
            uint8_t expected[2][32], actual[2][32];
            TIDE_REQUIRE(pack_fingerprint(moved.bytes, moved.size, "cosmetic", &requests[0].desc, expected[0]));
            memcpy(expected[1], a, sizeof a);
            uint32_t failed = 0;
            TIDE_CHECK(pack_fingerprints(moved.bytes, moved.size, requests, 2u, actual, NULL, &failed));
            TIDE_CHECK(failed == UINT32_MAX && !memcmp(actual, expected, sizeof actual));
            uint8_t ids[2][32];
            tide_asset_fingerprint_request hashed[2];
            for (int k = 0; k < 2; k++) {
                tide_sha256_of(requests[k].qualified, strlen(requests[k].qualified), ids[k]);
                hashed[k] = (tide_asset_fingerprint_request){.desc = requests[k].desc, .id = ids[k]};
            }
            TIDE_CHECK(pack_fingerprints(moved.bytes, moved.size, hashed, 2u, actual, NULL, &failed));
            TIDE_CHECK(!memcmp(actual, expected, sizeof actual));
            TIDE_CHECK(pack_fingerprints(moved.bytes, moved.size, hashed, 2u, NULL, (const uint8_t (*)[32])expected, &failed));
            hashed[1].id = ids[0];
            TIDE_CHECK(!pack_fingerprints(moved.bytes, moved.size, hashed, 2u, actual, NULL, &failed) && failed == 1u);
            hashed[1].id = ids[1];
            hashed[1].qualified = requests[1].qualified;
            TIDE_CHECK(!pack_fingerprints(moved.bytes, moved.size, hashed, 2u, actual, NULL, &failed) && failed == 1u);
            hashed[1].qualified = NULL;
            ids[1][0] ^= 1u;
            TIDE_CHECK(!pack_fingerprints(moved.bytes, moved.size, hashed, 2u, actual, NULL, &failed) && failed == 1u);
            TIDE_CHECK(pack_fingerprints(moved.bytes, moved.size, requests, 2u, NULL,
                                          (const uint8_t (*)[32])expected, &failed));
        }
        free(plain.bytes);
        free(moved.bytes);
        free(changed.bytes);
    }
}

TIDE_TEST(pack_fingerprint_batch_handles_growth_collisions_and_request_order)
{
    const uint32_t entries = 1024u;
    char (*names)[32] = calloc(entries, sizeof *names);
    tide_asset_fingerprint_request *requests = calloc(entries, sizeof *requests);
    uint8_t (*actual)[32] = calloc(entries, sizeof *actual);
    uint8_t (*expected)[32] = calloc(entries, sizeof *expected);
    TIDE_REQUIRE(names && requests && actual && expected);
    tide_pack_builder builder = {0};
    const char *values[2] = {"small", "larger value"};
    uint64_t offsets[2];
    uint32_t lengths[2], chars[2];
    uint8_t content[32];
    for (uint32_t i = 0; i < 2u; i++) {
        lengths[i] = (uint32_t)strlen(values[i]);
        tide_sha256_of(values[i], lengths[i], content);
        offsets[i] = tide_pack_string(&builder, content, values[i], lengths[i], &chars[i]);
    }
    for (uint32_t i = 0; i < entries; i++) {
        const int written = snprintf(names[i], sizeof names[i], "Selected.Asset%08u", i);
        TIDE_REQUIRE(written > 0 && (size_t)written < sizeof names[i]);
        const uint32_t value = (i / 2u) % 2u, kind = (i & 1u) ? TIDE_PACK_BYTES : TIDE_PACK_TEXT;
        tide_pack_name(&builder, names[i], kind, offsets[value], lengths[value], chars[value]);
    }
    // Unselected directory duplicates stay irrelevant to this dependency set.
    tide_pack_name(&builder, "unselected", TIDE_PACK_TEXT, offsets[0], lengths[0], chars[0]);
    tide_pack_name(&builder, "unselected", TIDE_PACK_TEXT, offsets[1], lengths[1], chars[1]);
    size_t size;
    uint8_t *bytes = tide_pack_end(&builder, &size);
    TIDE_REQUIRE(bytes != NULL);
    uint8_t controls[4][32];
    for (uint32_t i = 0; i < 4u; i++) {
        const tide_asset_fingerprint_desc desc = {.kind = (i & 1u) ? TIDE_PACK_BYTES : TIDE_PACK_TEXT};
        TIDE_REQUIRE(pack_fingerprint(bytes, size, names[i], &desc, controls[i]));
    }
    uint32_t count = 0;
    for (uint32_t remaining = entries; remaining; remaining--) {
        const uint32_t i = remaining - 1u;
        if (i % 5u == 0u) continue;
        requests[count] = (tide_asset_fingerprint_request){.qualified = names[i], .desc = {.kind = (i & 1u) ? TIDE_PACK_BYTES : TIDE_PACK_TEXT}};
        memcpy(expected[count++], controls[i % 4u], sizeof controls[0]);
    }
    uint32_t failed = 0;
    TIDE_REQUIRE(pack_fingerprints(bytes, size, requests, count, actual, NULL, &failed));
    TIDE_CHECK(failed == UINT32_MAX && !memcmp(actual, expected, (size_t)count * sizeof *actual));
    TIDE_CHECK(pack_fingerprints(bytes, size, requests, count, NULL, (const uint8_t (*)[32])expected, &failed));
    expected[count / 2u][0] ^= 1u;
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, count, NULL, (const uint8_t (*)[32])expected, &failed));
    TIDE_CHECK(failed == count / 2u);
    free(bytes);
    free(expected);
    free(actual);
    free(requests);
    free(names);
}

TIDE_TEST(pack_fingerprint_batch_rejects_selected_directory_errors_and_invalid_requests)
{
    tide_pack_builder builder = {0};
    uint8_t content[32];
    tide_sha256_of("a", 1u, content);
    uint32_t chars;
    const uint64_t at = tide_pack_string(&builder, content, "a", 1u, &chars);
    const char *const names[] = {"first", "second", "unselected"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        tide_pack_name(&builder, names[i], TIDE_PACK_TEXT, at, 1u, chars);
    size_t size;
    uint8_t *bytes = tide_pack_end(&builder, &size);
    TIDE_REQUIRE(bytes != NULL);
    tide_asset_fingerprint_request requests[2] = {
        {.qualified = "second", .desc = {.kind = TIDE_PACK_TEXT}}, {.qualified = "first", .desc = {.kind = TIDE_PACK_TEXT}}};
    uint8_t out[2][32];
    uint32_t failed = 0;
    TIDE_REQUIRE(pack_fingerprints(bytes, size, requests, 2u, out, NULL, &failed));
    TIDE_CHECK(pack_fingerprints(bytes, size, NULL, 0u, NULL, NULL, &failed));
    TIDE_CHECK(failed == UINT32_MAX);
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, NULL, NULL, &failed) && failed == UINT32_MAX);
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, out, (const uint8_t (*)[32])out, &failed));
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, UINT32_MAX, out, NULL, &failed));
    requests[1].qualified = "missing";
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, out, NULL, &failed) && failed == 1u);
    requests[1].qualified = NULL;
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, out, NULL, &failed) && failed == 1u);
    requests[1].qualified = "second";
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, out, NULL, &failed) && failed == 1u);
    requests[1].qualified = "first";
    tide_pack_entry *directory = (tide_pack_entry *)(void *)(bytes + sizeof(tide_pack_header));
    directory[2] = directory[0]; // A duplicate selected name after its first occurrence
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, out, NULL, &failed) && failed == 1u);
    directory[0].kind = TIDE_PACK_BYTES; // Duplicates still fail even when the kinds differ.
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, out, NULL, &failed) && failed == 1u);
    tide_sha256_of("unselected", strlen("unselected"), directory[2].name);
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, out, NULL, &failed) && failed == 1u);
    directory[0].kind = TIDE_PACK_TEXT;
    directory[0].offset = UINT32_MAX;
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, out, NULL, &failed) && failed == 1u);
    ((tide_pack_header *)(void *)bytes)->entries = UINT32_MAX;
    TIDE_CHECK(!pack_fingerprints(bytes, size, requests, 2u, out, NULL, &failed) && failed == UINT32_MAX);
    free(bytes);
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
    memset(region + e->offset + tide_block_next(vertices), 99, sizeof(uint64_t));
    memset(region + e->size + tide_block_next(indices), 77, sizeof(uint64_t));
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
        // A map's first value, after its order and key; a list's first element
        tide_handle *handle = (tide_handle *)(void *)((uint8_t *)(b + 1) + (kinds[i] == TIDE_PACK_MAP ? 16u : 0u));
        const tide_handle valid = *handle;
        *handle = tide_handle_offset(valid);
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
    uint64_t *stride = (uint64_t *)(void *)(region + e->offset + tide_block_next(b) + sizeof(uint64_t));
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
    const uint64_t block = records[0].block;
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
        uint64_t index = 0;
        const uint64_t at = kind == TIDE_PACK_LIST ? tide_pack_list(&b, NULL, 0u) : tide_pack_map(&b, NULL, NULL, 0u, &index);
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
    const uint64_t at = tide_pack_grid(&b, content, (int32_t)width, 1, pixels, &shape);
    tide_pack_name(&b, "chosen", TIDE_PACK_GRID, at, 0u, 0u);
    fingerprint_pack p = {.desc = {.kind = TIDE_PACK_GRID, .grid_shape = shape}};
    p.bytes = tide_pack_end(&b, &p.size);
    free(pixels);
    TIDE_REQUIRE(fingerprint_hash(&p, before));
    tide_grid_dir *d = (tide_grid_dir *)(void *)(fingerprint_region(&p) + at + sizeof(tide_block));
    TIDE_CHECK(d->flat == 0u && d->records == 2u);
    const uint64_t block = tide_grid_records(d)[1].block - TIDE_PACK_BASE;
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
    const uint64_t block = tide_grid_records(d)[1].block - TIDE_PACK_BASE;
    tide_color *cells = (tide_color *)(void *)(fingerprint_region(&p) + block);
    // The last chunk of this 17x1 image has one visible cell. Its remaining
    // cells are canonically zero payload, distinct from block padding.
    TIDE_CHECK(cells[1].r == 0.0f);
    cells[1].r = 0.25f;
    TIDE_REQUIRE(fingerprint_hash(&p, after));
    TIDE_CHECK(memcmp(before, after, sizeof before) != 0);
    free(p.bytes);
}
