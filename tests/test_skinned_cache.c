#include <stdlib.h>
#include <string.h>

#include "tide/pack.h"
#include "tide/jobs.h"
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
static tide_list cache_model_palettes(const uint32_t count, const uint32_t palette_count, const float bind)
{
    const size_t capacity = TIDE_SKINNED_HEADER_SIZE + (size_t)count * TIDE_SKINNED_NODE_SIZE
        + (size_t)palette_count * TIDE_SKINNED_PALETTE_SIZE + 3u * TIDE_SKINNED_VERTEX_SIZE
        + 3u * 4u + 3u * TIDE_SKINNED_INFLUENCE_SIZE + 4u;
    uint8_t *p = tide_alloc_zeroed(capacity, 1u);
    uint32_t end = TIDE_SKINNED_HEADER_SIZE;
    put_word(p, 0u, TIDE_SKINNED_DATA_MAGIC);
    put_word(p, 4u, TIDE_SKINNED_DATA_VERSION);
    const uint32_t nodes = put_table(p, &end, 16u, count, TIDE_SKINNED_NODE_SIZE);
    const uint32_t palettes = put_table(p, &end, 24u, palette_count, TIDE_SKINNED_PALETTE_SIZE);
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
    for (uint32_t i = 0; i < palette_count; i++) {
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
    const tide_list out = tide_list_from(p, (int32_t)end, 1u);
    free(p);
    return out;
}

static tide_list cache_model(const uint32_t count, const float bind)
{
    return cache_model_palettes(count, 2u, bind);
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

static bool atlas_matches(const tide_draw_list *draw, const uint32_t texture, const int32_t base,
                           const tide_float4x4 *expected, const uint32_t count)
{
    if (!texture || texture > draw->texture_count || base < 0) return false;
    const tide_draw_texture *t = &draw->textures[texture - 1u];
    const size_t at = (size_t)(uint32_t)base * sizeof *expected, bytes = (size_t)count * sizeof *expected;
    return t->pixels && at <= t->capacity && bytes <= t->capacity - at
        && memcmp(t->pixels + at, expected, bytes) == 0;
}

static void expected_palette(const tide_list data, const tide_list pose, tide_float4x4 out[2])
{
    const tide_list palette = tide_skinned_palette(data, pose);
    const void *matrices = tide_list_at(palette, 0, sizeof *out);
    if (matrices && tide_list_count(palette) >= 2) memcpy(out, matrices, 2u * sizeof *out);
    else memset(out, 0, 2u * sizeof *out);
}

TIDE_TEST(skinned_atlas_captures_owned_dependencies_before_prepare)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(128u, 0.0f);
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
    tide_float4x4 expected[3][2];
    expected_palette(data, pose, expected[0]);
    tide_draw_list draw = {0}, other = {0};
    uint32_t textures[3][2], again[2];
    int32_t info[3][4], repeated[4];
    TIDE_REQUIRE(tide_draw_skinned_atlas(&draw, data, pose, textures[0], info[0]));
    TIDE_CHECK(info[0][0] == 2 && info[0][1] == 3 && info[0][2] == 3 && info[0][3] == 0);
    tide_bone_transform *last = tide_list_at_mut(pose, 127, sizeof *last);
    TIDE_REQUIRE(last != NULL);
    last->position.y = 7.0f;
    expected_palette(data, pose, expected[1]);
    TIDE_REQUIRE(tide_draw_skinned_atlas(&draw, data, pose, textures[1], info[1]));
    TIDE_CHECK(textures[1][0] == textures[0][0] && textures[1][1] == textures[0][1] && info[1][3] == 2);
    tide_text_use(&snapshot, NULL);
    TIDE_REQUIRE(tide_draw_skinned_atlas(&draw, data, pose, again, repeated));
    TIDE_CHECK(again[0] == textures[0][0] && repeated[3] == info[0][3]);
    TIDE_CHECK(tide_heap_hash(1u, &snapshot) == snapshot_hash);
    // Equal captures in another list have independent page identities.
    TIDE_REQUIRE(tide_draw_skinned_atlas(&other, data, pose, again, repeated));
    TIDE_CHECK(other.textures[again[0] - 1u].id != draw.textures[textures[0][0] - 1u].id);
    const uint32_t reused = tide_scratch_mark();
    tide_list scratch_pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &scratch_pose, TIDE_IN_SCRATCH));
    last = tide_list_at_mut(scratch_pose, 127, sizeof *last);
    TIDE_REQUIRE(last != NULL);
    last->position.y = -4.0f;
    expected_palette(data, scratch_pose, expected[2]);
    TIDE_REQUIRE(tide_draw_skinned_atlas(&draw, data, scratch_pose, textures[2], info[2]));
    tide_scratch_reset(reused);
    memset(tide_scratch_memory(16384u), 0xa5, 16384u);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_heap_free(&snapshot);
    tide_pack_unload();
    // All dependencies are now gone or overwritten; preparation owns them.
    tide_draw_prepare(&draw, NULL);
    tide_draw_prepare(&other, NULL);
    for (uint32_t i = 0; i < 3u; i++) TIDE_CHECK(atlas_matches(&draw, textures[i][0], info[i][3], expected[i], 2u));
    TIDE_CHECK(atlas_matches(&other, again[0], repeated[3], expected[0], 2u));
    const uint64_t version = draw.textures[textures[0][0] - 1u].version;
    tide_draw_prepare(&draw, NULL);
    TIDE_CHECK(draw.textures[textures[0][0] - 1u].version == version);
    tide_draw_forget(&draw);
    TIDE_CHECK(draw.texture_count == 0u);
    tide_draw_prepare(&draw, NULL);
    tide_draw_free(&draw);
    tide_draw_free(&other);
    free(bytes);
    tide_scratch_reset(mark);
}

typedef struct atlas_pool_probe {
    uint32_t calls, others, mask;
} atlas_pool_probe;

static void atlas_pool_some(void *self, const uint32_t others,
                            void (*work)(void *, uint32_t), void *context)
{
    atlas_pool_probe *probe = self;
    probe->calls++;
    probe->others = others;
    for (uint32_t i = others; i > 0u; i--) {
        probe->mask |= 1u << i;
        work(context, i);
    }
    probe->mask |= 1u;
    work(context, 0u);
}

static void atlas_pool_all(void *self, void (*work)(void *, uint32_t), void *context)
{
    atlas_pool_some(self, 3u, work, context);
}

TIDE_TEST(skinned_atlas_grows_splits_and_prepares_through_jobs)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model_palettes(128u, 2048u, 0.0f);
    TIDE_REQUIRE(tide_skinned_valid(raw));
    size_t size;
    uint8_t *bytes = cache_pack(raw, false, &size);
    TIDE_REQUIRE(bytes && tide_pack_load(bytes, size));
    const tide_list data = packed_model();
    tide_draw_list draw = {0};
    uint32_t textures[33][2];
    int32_t info[33][4];
    tide_float4x4 expected[33][2];
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    atlas_pool_probe probe = {0};
    tide_jobs jobs = {.self = &probe, .threads = 8u, .run = atlas_pool_all, .run_some = atlas_pool_some};
    uint64_t page_id = 0u, version = 0u;
    for (uint32_t frame = 0; frame < 2u; frame++) {
        for (uint32_t i = 0; i < 33u; i++) {
            tide_bone_transform *last = tide_list_at_mut(pose, 127, sizeof *last);
            TIDE_REQUIRE(last != NULL);
            last->position.y = (float)i;
            expected_palette(data, pose, expected[i]);
            TIDE_REQUIRE(tide_draw_skinned_atlas(&draw, data, pose, textures[i], info[i]));
            TIDE_CHECK(info[i][0] == 2048 && info[i][3] == (int32_t)((i % 32u) * 2048u));
            if (i < 32u) TIDE_CHECK(textures[i][0] == textures[0][0]);
        }
        TIDE_CHECK(textures[32][0] != textures[0][0] && textures[32][1] == textures[0][1]);
        const tide_draw_texture *first = &draw.textures[textures[0][0] - 1u];
        TIDE_CHECK(first->width == 1024 && first->height == 256 && first->capacity >= 4194304u);
        if (!frame) { page_id = first->id; version = first->version; }
        else TIDE_CHECK(first->id == page_id && first->version == version);
        tide_draw_prepare(&draw, &jobs);
        TIDE_CHECK(probe.calls == frame + 1u && probe.others == (frame ? 3u : 1u));
        TIDE_CHECK((probe.mask & 3u) == 3u);
        for (uint32_t i = 0; i < 33u; i++) {
            TIDE_CHECK(atlas_matches(&draw, textures[i][0], info[i][3], expected[i], 2u));
            // Every palette entry after the first addresses the last node.
            TIDE_CHECK(atlas_matches(&draw, textures[i][0], info[i][3] + 2047, &expected[i][1], 1u));
        }
        tide_draw_prepare(&draw, &jobs);
        TIDE_CHECK(probe.calls == frame + 1u);
        if (!frame) { tide_draw_reset(&draw); jobs.run_some = NULL; }
    }
    tide_draw_reset(&draw);
    // Reset discards pending work before textures are compacted.
    TIDE_REQUIRE(tide_draw_skinned_atlas(&draw, data, pose, textures[0], info[0]));
    tide_draw_reset(&draw);
    tide_draw_prepare(&draw, &jobs);
    TIDE_CHECK(probe.calls == 2u);
    tide_draw_free(&draw);
    tide_pack_unload();
    free(bytes);
    tide_scratch_reset(mark);
}

static void atlas_command(tide_draw_list *draw, const tide_list vertices, const tide_list indices,
                           const uint32_t texture[2], const int32_t base)
{
    static const tide_shader shader = {.vertex_size = sizeof(tide_float3), .instance_ints = 1u, .texture_count = 2u};
    const int32_t instance[4] = {base, 0, 0, 0};
    const uint32_t textures[TIDE_SHADER_TEXTURES] = {texture[0], texture[1]};
    tide_draw_mesh_shaded(draw, &shader, vertices, indices, instance, NULL, textures, 0u);
}

TIDE_TEST(skinned_atlas_append_preserves_two_source_frame_snapshots)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(3u, 0.0f);
    size_t size;
    uint8_t *bytes = cache_pack(raw, false, &size);
    TIDE_REQUIRE(bytes && tide_pack_load(bytes, size));
    const tide_list data = packed_model();
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    const tide_float3 points[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const uint32_t corners[3] = {0u, 1u, 2u};
    const tide_list vertices = tide_list_from(points, 3, sizeof *points), indices = tide_list_from(corners, 3, sizeof *corners);
    tide_draw_list source = {0}, destination = {0}, copied = {0};
    tide_float4x4 expected[2][2];
    uint32_t texture[2];
    int32_t info[4];
    uint64_t page_id = 0u;
    for (uint32_t frame = 0; frame < 2u; frame++) {
        tide_bone_transform *last = tide_list_at_mut(pose, 2, sizeof *last);
        TIDE_REQUIRE(last != NULL);
        last->position.y = (float)frame * 9.0f;
        expected_palette(data, pose, expected[frame]);
        TIDE_REQUIRE(tide_draw_skinned_atlas(&source, data, pose, texture, info));
        if (!frame) page_id = source.textures[texture[0] - 1u].id;
        else TIDE_CHECK(source.textures[texture[0] - 1u].id == page_id);
        atlas_command(&source, vertices, indices, texture, info[3]);
        // Append must finalize the deferred source before taking its copy.
        tide_draw_append(&destination, &source);
        TIDE_REQUIRE(destination.run_count == frame + 1u);
        for (uint32_t i = 0; i <= frame; i++) {
            TIDE_CHECK(atlas_matches(&destination, destination.runs[i].textures[0], 0, expected[i], 2u));
        }
        tide_draw_reset(&source);
    }
    TIDE_CHECK(destination.runs[0].textures[0] != destination.runs[1].textures[0]);
    tide_draw_free(&source);
    // A second append recognizes the copied writer kind as well.
    tide_draw_append(&copied, &destination);
    TIDE_REQUIRE(copied.run_count == 2u);
    for (uint32_t i = 0; i < 2u; i++) TIDE_CHECK(atlas_matches(&copied, copied.runs[i].textures[0], 0, expected[i], 2u));
    tide_draw_free(&destination);
    tide_draw_free(&copied);
    tide_pack_unload();
    free(bytes);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_atlas_rechecks_proof_and_keeps_immediate_owned_fallback)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(3u, 0.0f);
    size_t size;
    uint8_t *bytes = cache_pack(raw, true, &size);
    TIDE_REQUIRE(bytes && tide_pack_load(bytes, size));
    const tide_list data = packed_model();
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    tide_float4x4 expected[2];
    expected_palette(data, pose, expected);
    tide_draw_list draw = {0}, owned_draw = {0};
    uint32_t first[2], next[2];
    int32_t info[4];
    TIDE_REQUIRE(tide_draw_skinned_atlas(&draw, data, pose, first, info));
    tide_heap heap = {0};
    tide_text_use(&heap, NULL);
    tide_list owned = {0};
    tide_list_set(&owned, data, 1u, TIDE_IN_MATCH);
    TIDE_REQUIRE(tide_draw_skinned_atlas(&owned_draw, owned, pose, next, info));
    TIDE_CHECK(owned_draw.skinned == NULL && info[3] == 0 && captured_matches(&owned_draw, next[0], owned, pose));
    const uint32_t carrier_size = (uint32_t)tide_list_count(raw) + (uint32_t)sizeof(tide_block);
    uint32_t count;
    void *carrier = tide_pack_mesh_open(tide_pack_find("Game.writable", TIDE_PACK_MESH), 0u, carrier_size, &count);
    TIDE_REQUIRE(carrier != NULL && count == 1u);
    uint8_t *model = (uint8_t *)carrier + sizeof(tide_block);
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(model, carrier_size - sizeof(tide_block), &view));
    put_float(model, view.palette_at + 52u, 5.0f);
    tide_pack_mesh_updated(carrier, carrier_size);
    TIDE_REQUIRE(tide_draw_skinned_atlas(&draw, data, pose, next, info));
    TIDE_CHECK(next[0] != first[0] && info[3] == 0 && captured_matches(&draw, next[0], data, pose));
    put_word(model, view.palette_at, view.nodes);
    tide_pack_mesh_updated(carrier, carrier_size);
    TIDE_CHECK(!tide_draw_skinned_atlas(&draw, data, pose, next, info) && zero_outputs(next, info) && info[3] == 0);
    tide_pack_unload();
    tide_draw_prepare(&draw, NULL);
    TIDE_CHECK(atlas_matches(&draw, first[0], 0, expected, 2u));
    tide_draw_free(&draw);
    tide_draw_free(&owned_draw);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    free(bytes);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_explicit_captures_recheck_model_identity_across_equal_and_changed_reloads)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(128u, 0.0f), changed = cache_model(128u, 8.0f);
    size_t first_size, next_size;
    uint8_t *first_pack = cache_pack(raw, false, &first_size);
    uint8_t *next_pack = cache_pack(changed, false, &next_size);
    TIDE_REQUIRE(first_pack && next_pack && tide_pack_load(first_pack, first_size));
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(packed_model(), &pose, TIDE_IN_SCRATCH));
    tide_draw_list immediate = {0}, atlas = {0};
    uint32_t textures[3][2], plain[3][2], repeat[2];
    int32_t info[3][4], plain_info[3], repeated[4];
    tide_float4x4 expected[2][2];
    expected_palette(packed_model(), pose, expected[0]);
    uint64_t identity, version, reloaded_identity, reloaded_version;
    TIDE_REQUIRE(tide_pack_skinned_proof(packed_model(), &identity, &version));
    for (uint32_t i = 0; i < 3u; i++) {
        if (i == 1u) {
            TIDE_REQUIRE(tide_pack_load(first_pack, first_size));
            TIDE_REQUIRE(tide_pack_skinned_proof(packed_model(), &reloaded_identity, &reloaded_version));
            TIDE_CHECK(reloaded_identity != identity && reloaded_version == version);
        } else if (i == 2u) {
            TIDE_REQUIRE(tide_pack_load(next_pack, next_size));
            expected_palette(packed_model(), pose, expected[1]);
        }
        TIDE_REQUIRE(tide_draw_skinned_data(&immediate, packed_model(), pose, plain[i], plain_info));
        TIDE_REQUIRE(tide_draw_skinned_atlas(&atlas, packed_model(), pose, textures[i], info[i]));
        TIDE_REQUIRE(tide_draw_skinned_atlas(&atlas, packed_model(), pose, repeat, repeated));
        TIDE_CHECK(repeat[0] == textures[i][0] && repeated[3] == info[i][3]);
    }
    TIDE_CHECK(plain[0][0] == plain[1][0] && plain[2][0] != plain[0][0]);
    TIDE_CHECK(info[0][3] == info[1][3] && info[2][3] != info[0][3]);
    tide_pack_unload();
    tide_draw_prepare(&atlas, NULL);
    for (uint32_t i = 0; i < 3u; i++) {
        TIDE_CHECK(atlas_matches(&atlas, textures[i][0], info[i][3], expected[i == 2u], 2u));
        TIDE_CHECK(memcmp(immediate.textures[plain[i][0] - 1u].pixels, expected[i == 2u], sizeof expected[0]) == 0);
    }
    tide_draw_free(&immediate);
    tide_draw_free(&atlas);
    free(first_pack);
    free(next_pack);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_rest_capture_reuses_owned_rest_after_edits_growth_and_slot_changes)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(3u, 0.0f);
    size_t size;
    uint8_t *bytes = cache_pack(raw, false, &size);
    TIDE_REQUIRE(bytes && tide_pack_load(bytes, size));
    const tide_list data = packed_model();
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    tide_float4x4 expected[2];
    expected_palette(data, pose, expected);
    tide_draw_list draw[2] = {0};
    for (uint32_t atlas = 0; atlas < 2u; atlas++) {
        uint32_t first[2], next[2];
        int32_t first_info[4] = {0}, info[4] = {0};
        TIDE_REQUIRE(atlas ? tide_draw_skinned_rest_atlas(&draw[atlas], data, first, first_info)
                           : tide_draw_skinned_rest_data(&draw[atlas], data, first, first_info));
        tide_draw_prepare(&draw[atlas], NULL);
        TIDE_CHECK(atlas_matches(&draw[atlas], first[0], first_info[3], expected, 2u));
        tide_bone_transform *edited = tide_list_at_mut(pose, 1, sizeof *edited);
        TIDE_REQUIRE(edited);
        edited->position.y = 7.0f + (float)atlas;
        TIDE_REQUIRE(atlas ? tide_draw_skinned_atlas(&draw[atlas], data, pose, next, info)
                           : tide_draw_skinned_data(&draw[atlas], data, pose, next, info));
        TIDE_CHECK(next[0] != first[0] || info[3] != first_info[3]);
        tide_draw_prepare(&draw[atlas], NULL);
        // Other atlas captures have changed the page's version and dimensions;
        // preparation has finished, but the rest slice remains a valid hit.
        const uint32_t textures = draw[atlas].texture_count, scratch = tide_scratch_mark();
        for (uint32_t i = 0; i < 100u; i++) {
            memset(info, 0, sizeof info);
            TIDE_REQUIRE(atlas ? tide_draw_skinned_rest_atlas(&draw[atlas], data, next, info)
                               : tide_draw_skinned_rest_data(&draw[atlas], data, next, info));
            TIDE_CHECK(next[0] == first[0] && next[1] == first[1] && info[3] == first_info[3]);
            TIDE_CHECK(memcmp(info, first_info, 3u * sizeof *info) == 0);
            TIDE_CHECK(draw[atlas].texture_count == textures && tide_scratch_mark() == scratch);
        }
        TIDE_CHECK(atlas_matches(&draw[atlas], first[0], first_info[3], expected, 2u));
        if (!atlas) {
            draw[0].textures[first[0] - 1u].version++;
            TIDE_REQUIRE(tide_draw_skinned_rest_data(&draw[0], data, next, info));
            TIDE_CHECK(atlas_matches(&draw[0], next[0], 0, expected, 2u));
            draw[0].textures[next[1] - 1u].version++;
            TIDE_REQUIRE(tide_draw_skinned_rest_data(&draw[0], data, next, info));
            TIDE_CHECK(atlas_matches(&draw[0], next[0], 0, expected, 2u));
        } else {
            // Both capture forms share one rest entry and may alternate after
            // the atlas page has grown and its deferred writes have completed.
            for (uint32_t i = 0; i < 3u; i++) {
                TIDE_REQUIRE(tide_draw_skinned_rest_data(&draw[1], data, next, info));
                TIDE_CHECK(atlas_matches(&draw[1], next[0], 0, expected, 2u));
                TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&draw[1], data, next, info));
                TIDE_CHECK(next[0] == first[0] && info[3] == first_info[3]);
            }
            // Rejecting an atlas slot must not return that same bad slot via
            // the generic atlas pose cache. Exceptional recovery is immediate.
            for (uint32_t field = 0; field < 5u; field++) {
                const tide_draw_texture saved = draw[1].textures[first[0] - 1u];
                tide_draw_texture *bad = &draw[1].textures[first[0] - 1u];
                if (field == 0u) bad->id++;
                else if (field == 1u) bad->version++;
                else if (field == 2u) bad->width++;
                else if (field == 3u) bad->height++;
                else bad->epoch++;
                for (uint32_t retry = 0; retry < 2u; retry++) {
                    info[3] = -1;
                    TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&draw[1], data, next, info));
                    TIDE_CHECK(next[0] != first[0] && info[3] == 0 && atlas_matches(&draw[1], next[0], 0, expected, 2u));
                }
                // Restore only metadata: a fallback may grow the texture
                // array, but it never changes this atlas page's owned pixels.
                bad = &draw[1].textures[first[0] - 1u];
                bad->id = saved.id;
                bad->version = saved.version;
                bad->width = saved.width;
                bad->height = saved.height;
                bad->epoch = saved.epoch;
                TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&draw[1], data, next, info));
                TIDE_CHECK(next[0] == first[0] && info[3] == first_info[3]);
            }
        }
        for (uint32_t reset = 0; reset < 2u; reset++) {
            if (reset) tide_draw_forget(&draw[atlas]);
            else tide_draw_reset(&draw[atlas]);
            TIDE_REQUIRE(atlas ? tide_draw_skinned_rest_atlas(&draw[atlas], data, next, info)
                               : tide_draw_skinned_rest_data(&draw[atlas], data, next, info));
            tide_draw_prepare(&draw[atlas], NULL);
            TIDE_CHECK(atlas_matches(&draw[atlas], next[0], info[3], expected, 2u));
        }
        tide_draw_free(&draw[atlas]);
    }
    tide_pack_unload();
    free(bytes);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_rest_capture_keeps_append_snapshots_and_rechecks_reloaded_tokens)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(3u, 0.0f), changed = cache_model(3u, 8.0f);
    size_t sizes[2];
    uint8_t *packs[2] = {cache_pack(raw, false, &sizes[0]), cache_pack(changed, false, &sizes[1])};
    TIDE_REQUIRE(packs[0] && packs[1] && tide_pack_load(packs[0], sizes[0]));
    const uint32_t handle = packed_model().at;
    const tide_float3 points[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const uint32_t corners[3] = {0u, 1u, 2u};
    const tide_list vertices = tide_list_from(points, 3, sizeof *points), indices = tide_list_from(corners, 3, sizeof *corners);
    tide_draw_list source = {0}, destination = {0};
    tide_float4x4 expected[2][2];
    uint64_t previous_identity = 0u;
    for (uint32_t frame = 0; frame < 2u; frame++) {
        if (frame) TIDE_REQUIRE(tide_pack_load(packs[frame], sizes[frame]));
        const tide_list data = packed_model();
        uint64_t identity, version;
        TIDE_REQUIRE(tide_pack_skinned_proof(data, &identity, &version));
        TIDE_CHECK(identity != 0u && identity != previous_identity && data.at == handle);
        previous_identity = identity;
        tide_list pose = {0};
        TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
        expected_palette(data, pose, expected[frame]);
        uint32_t textures[2];
        int32_t info[4];
        TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&source, data, textures, info));
        atlas_command(&source, vertices, indices, textures, info[3]);
        // Append prepares owned source bytes, even after their pack disappears.
        tide_pack_unload();
        tide_draw_append(&destination, &source);
        for (uint32_t i = 0; i <= frame; i++) {
            TIDE_CHECK(atlas_matches(&destination, destination.runs[i].textures[0], 0, expected[i], 2u));
        }
        tide_draw_reset(&source);
    }
    TIDE_REQUIRE(destination.run_count == 2u);
    TIDE_CHECK(destination.runs[0].textures[0] != destination.runs[1].textures[0]);
    // Replacing the pack within one frame must miss the numeric proof identity,
    // including an identical reload whose content hash and list handle agree.
    TIDE_REQUIRE(tide_pack_load(packs[0], sizes[0]));
    uint32_t first[2], next[2];
    int32_t first_info[4], info[4];
    TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&source, packed_model(), first, first_info));
    uint64_t first_identity, first_version, next_identity, next_version;
    TIDE_REQUIRE(tide_pack_skinned_proof(packed_model(), &first_identity, &first_version));
    TIDE_REQUIRE(tide_pack_load(packs[0], sizes[0]));
    TIDE_REQUIRE(tide_pack_skinned_proof(packed_model(), &next_identity, &next_version));
    TIDE_CHECK(first_identity != next_identity && first_version == next_version && packed_model().at == handle);
    TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&source, packed_model(), next, info));
    TIDE_REQUIRE(tide_pack_load(packs[1], sizes[1]));
    TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&source, packed_model(), next, info));
    TIDE_CHECK(next[0] != first[0] || info[3] != first_info[3]);
    tide_pack_unload();
    tide_draw_prepare(&source, NULL);
    TIDE_CHECK(atlas_matches(&source, first[0], first_info[3], expected[0], 2u));
    TIDE_CHECK(atlas_matches(&source, next[0], info[3], expected[1], 2u));
    TIDE_CHECK(!tide_draw_skinned_rest_atlas(&source, packed_model(), next, info) && zero_outputs(next, info) && info[3] == 0);
    // Distinct reload identities grow and rehash the frame-local rest index,
    // even when exact model bytes allow the owned palette capture to be reused.
    for (uint32_t i = 0; i < 24u; i++) {
        TIDE_REQUIRE(tide_pack_load(packs[0], sizes[0]));
        TIDE_REQUIRE(tide_pack_skinned_proof(packed_model(), &next_identity, &next_version));
        TIDE_CHECK(next_identity != first_identity);
        first_identity = next_identity;
        TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&source, packed_model(), first, first_info));
        TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&source, packed_model(), next, info));
        TIDE_CHECK(first[0] == next[0] && first[1] == next[1] && first_info[3] == info[3]);
    }
    tide_pack_unload();
    tide_draw_prepare(&source, NULL);
    TIDE_CHECK(atlas_matches(&source, first[0], first_info[3], expected[0], 2u));
    tide_draw_free(&source);
    tide_draw_free(&destination);
    free(packs[0]);
    free(packs[1]);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_rest_capture_rechecks_invalidated_proof_and_owned_fallback)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = cache_model(3u, 0.0f);
    size_t size;
    uint8_t *bytes = cache_pack(raw, true, &size);
    TIDE_REQUIRE(bytes && tide_pack_load(bytes, size));
    const tide_list data = packed_model();
    tide_draw_list draw = {0}, owned_draw = {0};
    uint32_t first[2], next[2];
    int32_t info[4];
    TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&draw, data, first, info));
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    tide_float4x4 original[2];
    expected_palette(data, pose, original);
    tide_heap heap = {0};
    tide_text_use(&heap, NULL);
    tide_list owned = {0};
    tide_list_set(&owned, data, 1u, TIDE_IN_MATCH);
    TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&owned_draw, owned, next, info));
    TIDE_CHECK(owned_draw.skinned == NULL && info[3] == 0 && atlas_matches(&owned_draw, next[0], 0, original, 2u));
    const uint32_t carrier_size = (uint32_t)tide_list_count(raw) + (uint32_t)sizeof(tide_block);
    uint32_t count;
    void *carrier = tide_pack_mesh_open(tide_pack_find("Game.writable", TIDE_PACK_MESH), 0u, carrier_size, &count);
    TIDE_REQUIRE(carrier && count == 1u);
    // Invalidating unchanged, valid bytes must still take the checked fallback.
    tide_pack_mesh_updated(carrier, carrier_size);
    uint64_t identity, version;
    TIDE_CHECK(!tide_pack_skinned_proof(data, &identity, &version));
    TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&draw, data, next, info));
    TIDE_CHECK(info[3] == 0 && atlas_matches(&draw, next[0], 0, original, 2u));
    uint8_t *model = (uint8_t *)carrier + sizeof(tide_block);
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(model, carrier_size - sizeof(tide_block), &view));
    put_float(model, view.node_at + TIDE_SKINNED_NODE_SIZE + 20u, 5.0f);
    tide_pack_mesh_updated(carrier, carrier_size);
    TIDE_REQUIRE(tide_draw_skinned_rest_atlas(&draw, data, next, info));
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(info[3] == 0 && captured_matches(&draw, next[0], data, pose));
    put_word(model, view.palette_at, view.nodes);
    tide_pack_mesh_updated(carrier, carrier_size);
    TIDE_CHECK(!tide_draw_skinned_rest_data(&draw, data, next, info) && zero_outputs(next, info));
    TIDE_CHECK(!tide_draw_skinned_rest_atlas(&draw, data, next, info) && zero_outputs(next, info) && info[3] == 0);
    TIDE_REQUIRE(tide_draw_skinned_rest_data(&owned_draw, owned, next, info));
    TIDE_CHECK(atlas_matches(&owned_draw, next[0], 0, original, 2u));
    tide_pack_unload();
    tide_draw_prepare(&draw, NULL);
    TIDE_CHECK(atlas_matches(&draw, first[0], 0, original, 2u));
    tide_draw_free(&draw);
    tide_draw_free(&owned_draw);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    free(bytes);
    tide_scratch_reset(mark);
}
