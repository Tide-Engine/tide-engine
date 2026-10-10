#include <stdlib.h>
#include <string.h>

#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide/skinned.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

static void put_word(uint8_t *p, const uint32_t at, const uint32_t value)
{
    for (uint32_t k = 0; k < 4u; k++) p[at + k] = (uint8_t)(value >> (k * 8u));
}

static void put_float(uint8_t *p, const uint32_t at, const float value)
{
    put_word(p, at, tide_f_bits(value));
}

static uint32_t put_table(uint8_t *p, uint32_t *end, const uint32_t header, const uint32_t count,
                           const uint32_t stride)
{
    const uint32_t start = *end;
    put_word(p, header, count);
    put_word(p, header + 4u, start);
    *end += count * stride;
    return start;
}

// Enough nodes for an owned pose to occupy several chunks. The skeleton
// fingerprint is intentionally shared by variants with different inverse binds.
static tide_list cache_model(const uint32_t count, const float bind)
{
    uint8_t p[16384] = {0};
    uint32_t end = TIDE_SKINNED_HEADER_SIZE;
    put_word(p, 0u, TIDE_SKINNED_DATA_MAGIC);
    put_word(p, 4u, TIDE_SKINNED_DATA_VERSION);
    const uint32_t nodes = put_table(p, &end, 16u, count, TIDE_SKINNED_NODE_SIZE);
    const uint32_t palettes = put_table(p, &end, 24u, 2u, TIDE_SKINNED_PALETTE_SIZE);
    const uint32_t vertices = put_table(p, &end, 32u, 3u, TIDE_SKINNED_VERTEX_SIZE);
    const uint32_t indices = put_table(p, &end, 40u, 3u, 4u);
    (void)put_table(p, &end, 48u, 0u, TIDE_SKINNED_CLIP_SIZE);
    (void)put_table(p, &end, 56u, 0u, TIDE_SKINNED_CHANNEL_SIZE);
    const uint32_t influences = put_table(p, &end, 64u, 3u, TIDE_SKINNED_INFLUENCE_SIZE);
    const uint32_t strings = end;
    put_word(p, 72u, strings);
    put_word(p, 76u, 4u);
    end += 4u;
    put_word(p, 80u, end);
    put_word(p, 8u, end);
    memset(p + 88u, 1, 32u);
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t n = nodes + i * TIDE_SKINNED_NODE_SIZE;
        put_word(p, n, i ? i - 1u : UINT32_MAX);
        put_word(p, n + 4u, i ? i - 1u : UINT32_MAX);
        put_word(p, n + 8u, strings);
        put_float(p, n + 16u, i ? 1.0f : 0.0f);
        put_float(p, n + 40u, 1.0f);
        for (uint32_t k = 0; k < 3u; k++) put_float(p, n + 44u + k * 4u, 1.0f);
    }
    for (uint32_t i = 0; i < 2u; i++) {
        const uint32_t at = palettes + i * TIDE_SKINNED_PALETTE_SIZE;
        put_word(p, at, i ? count - 1u : 1u);
        for (uint32_t k = 0; k < 4u; k++) put_float(p, at + 4u + k * 20u, 1.0f);
        put_float(p, at + 52u, bind);
    }
    for (uint32_t i = 0; i < 3u; i++) {
        const uint32_t at = vertices + i * TIDE_SKINNED_VERTEX_SIZE;
        put_float(p, at, (float)i);
        put_word(p, at + 72u, i);
        put_word(p, at + 76u, 1u);
        put_word(p, indices + i * 4u, i);
        put_float(p, influences + i * TIDE_SKINNED_INFLUENCE_SIZE + 4u, 1.0f);
    }
    return tide_list_from(p, (int32_t)end, 1u);
}

static uint8_t *cache_pack(const tide_list data, const bool alias, size_t *size)
{
    tide_pack_builder b = {0};
    const uint32_t bytes = (uint32_t)tide_list_count(data);
    const void *flat = tide_list_at(data, 0, 1u);
    uint8_t hash[32];
    uint32_t offsets[3];
    if (alias) {
        const uint32_t carrier_bytes = bytes + (uint32_t)sizeof(tide_block);
        uint8_t *carrier = tide_alloc(carrier_bytes);
        const tide_block header = {.a = bytes, .b = bytes};
        memcpy(carrier, &header, sizeof header);
        memcpy(carrier + sizeof header, flat, bytes);
        uint32_t writable[2];
        const uint32_t indices[3] = {0u, 0u, 0u};
        tide_sha256_of(carrier, carrier_bytes, hash);
        tide_pack_mesh(&b, hash, carrier, carrier_bytes, 1u, indices, 3u, writable);
        tide_pack_name(&b, "Game.writable", TIDE_PACK_MESH, writable[0], writable[1], 1u);
        offsets[0] = writable[0] + (uint32_t)sizeof(tide_block);
        free(carrier);
    } else {
        uint32_t chars;
        tide_sha256_of(flat, bytes, hash);
        offsets[0] = tide_pack_bytes(&b, hash, flat, bytes, &chars);
    }
    const tide_float3 vertices[3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    const uint32_t indices[3] = {0u, 1u, 2u};
    tide_sha256_of(vertices, sizeof vertices, hash);
    tide_pack_mesh(&b, hash, vertices, sizeof vertices[0], 3u, indices, 3u, &offsets[1]);
    const uint32_t record = tide_pack_skinned_list(&b, offsets, 1u);
    tide_pack_name(&b, "Game.actor", TIDE_PACK_SKINNED, record, 1u, 1u);
    return tide_pack_end(&b, size);
}

static tide_list packed_model(void)
{
    return tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
}

static bool captured_matches(const tide_draw_list *draw, const uint32_t texture, const tide_list data,
                              const tide_list transforms)
{
    if (!texture || texture > draw->texture_count) return false;
    const tide_list expected = tide_skinned_palette(data, transforms);
    const int32_t count = tide_list_count(expected);
    const void *matrices = tide_list_at(expected, 0, sizeof(tide_float4x4));
    return count > 0 && matrices && memcmp(draw->textures[texture - 1u].pixels, matrices,
                                          (size_t)count * sizeof(tide_float4x4)) == 0;
}

static bool zero_outputs(const uint32_t textures[2], const int32_t info[3])
{
    return textures[0] == 0u && textures[1] == 0u && info[0] == 0 && info[1] == 0 && info[2] == 0;
}

TIDE_TEST(skinned_draw_cache_captures_edits_chunked_ownership_and_snapshots)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(128u, 0.0f);
    TIDE_REQUIRE(tide_skinned_valid(raw));
    size_t size;
    uint8_t *bytes = cache_pack(raw, false, &size);
    TIDE_REQUIRE(bytes && tide_pack_load(bytes, size));
    const tide_list data = packed_model();
    tide_heap heap = {0}, snapshot = {0};
    tide_text_use(&heap, NULL);
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_MATCH));
    tide_heap_copy(&snapshot, &heap);
    const uint64_t snapshot_hash = tide_heap_hash(1u, &snapshot);
    tide_draw_list draw = {0}, other = {0};
    uint32_t first[2], changed[2], again[2];
    int32_t info[3];
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, first, info));
    TIDE_CHECK(draw.skinned != NULL && info[0] == 2 && captured_matches(&draw, first[0], data, pose));
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, again, info));
    TIDE_CHECK(again[0] == first[0] && again[1] == first[1]);
    tide_bone_transform *last = tide_list_at_mut(pose, 127, sizeof *last);
    TIDE_REQUIRE(last != NULL);
    last->position.y = 7.0f;
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, changed, info));
    TIDE_CHECK(changed[0] != first[0] && changed[1] == first[1]);
    TIDE_CHECK(captured_matches(&draw, changed[0], data, pose));
    tide_text_use(&snapshot, NULL);
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, again, info));
    TIDE_CHECK(again[0] == first[0] && captured_matches(&draw, first[0], data, pose));
    TIDE_CHECK(tide_heap_hash(1u, &snapshot) == snapshot_hash);
    TIDE_REQUIRE(tide_draw_skinned_data(&other, data, pose, again, info));
    TIDE_CHECK(captured_matches(&other, again[0], data, pose) && other.skinned != draw.skinned);
    tide_text_use(&heap, NULL);
    // Distinct exact TRS values exercise index growth; returning to an old
    // value must recover that capture even after unrelated poses came between.
    for (uint32_t i = 1u; i <= 80u; i++) {
        last = tide_list_at_mut(pose, 127, sizeof *last);
        last->position.y = (float)i;
        TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, again, info));
        TIDE_CHECK(captured_matches(&draw, again[0], data, pose));
    }
    last = tide_list_at_mut(pose, 127, sizeof *last);
    last->position.y = 0.0f;
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, again, info));
    TIDE_CHECK(again[0] == first[0]);
    tide_list invalid = {0};
    TIDE_CHECK(!tide_draw_skinned_data(&draw, data, invalid, again, info) && zero_outputs(again, info));
    tide_draw_free(&draw);
    tide_draw_free(&other);
    TIDE_CHECK(draw.skinned == NULL && other.skinned == NULL);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_heap_free(&snapshot);
    tide_pack_unload();
    free(bytes);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_draw_cache_survives_scratch_reuse_reset_forget_and_slot_changes)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(3u, 0.0f);
    size_t size;
    uint8_t *bytes = cache_pack(raw, false, &size);
    TIDE_REQUIRE(bytes && tide_pack_load(bytes, size));
    const tide_list data = packed_model();
    tide_draw_list draw = {0};
    uint32_t first[2], second[2], again[2];
    int32_t info[3];
    const uint32_t reused = tide_scratch_mark();
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    const uint32_t handle = pose.at;
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, first, info));
    tide_scratch_reset(reused);
    pose = (tide_list){0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose.at == handle);
    tide_bone_transform *bone = tide_list_at_mut(pose, 1, sizeof *bone);
    TIDE_REQUIRE(bone != NULL);
    bone->position.y = 4.0f;
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, second, info));
    TIDE_CHECK(second[0] != first[0] && captured_matches(&draw, second[0], data, pose));
    // A slot's changed version cannot masquerade as the cached capture.
    draw.textures[second[0] - 1u].version ^= 1u;
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, again, info));
    TIDE_CHECK(again[0] == second[0] && captured_matches(&draw, again[0], data, pose));
    for (uint32_t frame = 0; frame < 4u; frame++) {
        tide_draw_reset(&draw);
        TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, again, info));
        TIDE_CHECK(captured_matches(&draw, again[0], data, pose));
    }
    tide_draw_forget(&draw);
    TIDE_CHECK(draw.texture_count == 0u);
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, again, info));
    TIDE_CHECK(captured_matches(&draw, again[0], data, pose));
    tide_draw_free(&draw);
    tide_pack_unload();
    free(bytes);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_draw_cache_checks_pack_replacement_and_inverse_bind_identity)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(3u, 0.0f), changed = cache_model(3u, 8.0f);
    TIDE_REQUIRE(tide_skinned_compatible(raw, changed));
    size_t first_size, next_size;
    uint8_t *first_pack = cache_pack(raw, false, &first_size);
    uint8_t *next_pack = cache_pack(changed, false, &next_size);
    TIDE_REQUIRE(first_pack && next_pack && tide_pack_load(first_pack, first_size));
    tide_list data = packed_model(), pose = {0};
    const uint32_t handle = data.at;
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    tide_draw_list draw = {0};
    uint32_t first[2], next[2];
    int32_t info[3];
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, first, info));
    tide_float4x4 saved;
    memcpy(&saved, draw.textures[first[0] - 1u].pixels, sizeof saved);
    TIDE_REQUIRE(tide_pack_load(next_pack, next_size));
    data = packed_model();
    TIDE_CHECK(data.at == handle);
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, next, info));
    TIDE_CHECK(next[0] != first[0] && captured_matches(&draw, next[0], data, pose));
    TIDE_CHECK(memcmp(&saved, draw.textures[first[0] - 1u].pixels, sizeof saved) == 0);
    tide_pack_unload();
    TIDE_CHECK(!tide_draw_skinned_data(&draw, data, pose, next, info) && zero_outputs(next, info));
    tide_draw_free(&draw);
    free(first_pack);
    free(next_pack);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_draw_cache_rechecks_invalidated_proof_and_owned_models)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(3u, 0.0f);
    size_t size;
    uint8_t *bytes = cache_pack(raw, true, &size);
    TIDE_REQUIRE(bytes && tide_pack_load(bytes, size));
    const tide_list data = packed_model();
    uint64_t version;
    TIDE_REQUIRE(tide_pack_skinned_version(data, &version));
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    tide_draw_list draw = {0}, owned_draw = {0};
    uint32_t first[2], changed[2];
    int32_t info[3];
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, first, info));
    tide_heap heap = {0};
    tide_text_use(&heap, NULL);
    tide_list owned = {0};
    tide_list_set(&owned, data, 1u, TIDE_IN_MATCH);
    TIDE_REQUIRE(tide_draw_skinned_data(&owned_draw, owned, pose, changed, info));
    TIDE_CHECK(owned_draw.skinned == NULL && captured_matches(&owned_draw, changed[0], owned, pose));
    const uint32_t carrier_size = (uint32_t)tide_list_count(raw) + (uint32_t)sizeof(tide_block);
    uint32_t count;
    void *carrier = tide_pack_mesh_open(tide_pack_find("Game.writable", TIDE_PACK_MESH), 0u, carrier_size, &count);
    TIDE_REQUIRE(carrier != NULL && count == 1u);
    uint8_t *model = (uint8_t *)carrier + sizeof(tide_block);
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(model, carrier_size - sizeof(tide_block), &view));
    put_float(model, view.palette_at + 52u, 5.0f);
    tide_pack_mesh_updated(carrier, carrier_size);
    TIDE_CHECK(!tide_pack_skinned_version(data, &version));
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, changed, info));
    TIDE_CHECK(changed[0] != first[0] && captured_matches(&draw, changed[0], data, pose));
    // It must not fall back to an old memo entry after malformed bytes replace
    // a valid palette, even though pose/skeleton/handle are unchanged.
    put_word(model, view.palette_at, view.nodes);
    tide_pack_mesh_updated(carrier, carrier_size);
    TIDE_CHECK(!tide_draw_skinned_data(&draw, data, pose, changed, info) && zero_outputs(changed, info));
    // The owned copy keeps its original content and checked path.
    TIDE_REQUIRE(tide_draw_skinned_data(&owned_draw, owned, pose, changed, info));
    TIDE_CHECK(captured_matches(&owned_draw, changed[0], owned, pose));
    tide_draw_free(&draw);
    tide_draw_free(&owned_draw);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_pack_unload();
    free(bytes);
    tide_scratch_reset(mark);
}
