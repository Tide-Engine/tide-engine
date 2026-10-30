#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/net.h"
#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide/skinned.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

static void word(uint8_t *bytes, const uint32_t at, const uint32_t value)
{
    for (uint32_t i = 0; i < 4u; i++) bytes[at + i] = (uint8_t)(value >> (i * 8u));
}

static void number(uint8_t *bytes, const uint32_t at, const float value)
{
    word(bytes, at, tide_f_bits(value));
}

static uint32_t table(uint8_t *bytes, uint32_t *at, const uint32_t header, const uint32_t count, const uint32_t size)
{
    const uint32_t first = *at;
    word(bytes, header, count);
    word(bytes, header + 4u, first);
    *at += count * size;
    return first;
}

static uint32_t channel(uint8_t *bytes, const uint32_t at, uint32_t *keys, const uint32_t node,
                         const uint32_t path, const uint32_t interpolation, const float *values)
{
    word(bytes, at, node);
    word(bytes, at + 4u, path);
    word(bytes, at + 8u, interpolation);
    word(bytes, at + 12u, 2u);
    word(bytes, at + 16u, *keys);
    number(bytes, *keys, 0.0f);
    number(bytes, *keys + 4u, 2.0f);
    *keys += 8u;
    word(bytes, at + 20u, *keys);
    const uint32_t words = (path == TIDE_SKINNED_ROTATION ? 4u : 3u) * 2u
                         * (interpolation == TIDE_SKINNED_CUBICSPLINE ? 3u : 1u);
    for (uint32_t i = 0; i < words; i++) number(bytes, *keys + i * 4u, values[i]);
    *keys += words * 4u;
    return at + TIDE_SKINNED_CHANNEL_SIZE;
}

// A root, arm and hand; two skin palettes; all three interpolation modes.
// Kept as portable bytes so the same fixture runs natively and under WASI.
static tide_list model(void)
{
    uint8_t bytes[2048] = {0};
    word(bytes, 0u, TIDE_SKINNED_DATA_MAGIC);
    word(bytes, 4u, TIDE_SKINNED_DATA_VERSION);
    uint32_t at = TIDE_SKINNED_HEADER_SIZE;
    const uint32_t nodes = table(bytes, &at, 16u, 3u, TIDE_SKINNED_NODE_SIZE);
    const uint32_t palettes = table(bytes, &at, 24u, 2u, TIDE_SKINNED_PALETTE_SIZE);
    const uint32_t vertices = table(bytes, &at, 32u, 3u, TIDE_SKINNED_VERTEX_SIZE);
    const uint32_t indices = table(bytes, &at, 40u, 3u, 4u);
    const uint32_t clips = table(bytes, &at, 48u, 3u, TIDE_SKINNED_CLIP_SIZE);
    const uint32_t channels = table(bytes, &at, 56u, 4u, TIDE_SKINNED_CHANNEL_SIZE);
    const uint32_t influences = table(bytes, &at, 64u, 6u, TIDE_SKINNED_INFLUENCE_SIZE);
    const uint32_t names = at;
    static const char text[] = "\0arm\0hand\0linear\0step\0cubic\0";
    memcpy(bytes + names, text, sizeof text);
    at += ((uint32_t)sizeof text + 3u) / 4u * 4u;
    word(bytes, 72u, names);
    word(bytes, 76u, (uint32_t)sizeof text);
    for (uint32_t i = 0; i < 32u; i++) bytes[88u + i] = (uint8_t)(i + 1u);
    for (uint32_t i = 0; i < 3u; i++) {
        const uint32_t n = nodes + i * TIDE_SKINNED_NODE_SIZE;
        word(bytes, n, i ? i - 1u : UINT32_MAX);
        word(bytes, n + 4u, i ? i - 1u : UINT32_MAX);
        word(bytes, n + 8u, names + (i == 1u ? 1u : i == 2u ? 5u : 0u));
        word(bytes, n + 12u, i == 1u ? 3u : i == 2u ? 4u : 0u);
        number(bytes, n + 16u, i == 1u ? 2.0f : 0.0f);
        number(bytes, n + 20u, i == 2u ? 3.0f : 0.0f);
        number(bytes, n + 40u, 1.0f);
        for (uint32_t k = 0; k < 3u; k++) number(bytes, n + 44u + k * 4u, 1.0f);
        const uint32_t v = vertices + i * TIDE_SKINNED_VERTEX_SIZE;
        number(bytes, v, (float)i);
        number(bytes, v + 20u, 1.0f);
        for (uint32_t k = 0; k < 4u; k++) number(bytes, v + 40u + k * 4u, 1.0f);
        number(bytes, v + 56u, 1.0f);
        number(bytes, v + 68u, 1.0f);
        word(bytes, v + 72u, i * 2u);
        word(bytes, v + 76u, 2u);
        word(bytes, indices + i * 4u, i);
        word(bytes, influences + i * 16u, 0u);
        number(bytes, influences + i * 16u + 4u, 0.25f);
        word(bytes, influences + i * 16u + 8u, 1u);
        number(bytes, influences + i * 16u + 12u, 0.75f);
    }
    for (uint32_t i = 0; i < 2u; i++) {
        const uint32_t p = palettes + i * TIDE_SKINNED_PALETTE_SIZE;
        word(bytes, p, i + 1u);
        for (uint32_t k = 0; k < 4u; k++) number(bytes, p + 4u + k * 20u, 1.0f);
        number(bytes, p + 52u, -2.0f);
        number(bytes, p + 56u, i ? -3.0f : 0.0f);
    }
    static const uint32_t name_offsets[] = {10u, 17u, 22u}, lengths[] = {6u, 4u, 5u};
    for (uint32_t i = 0; i < 3u; i++) {
        const uint32_t c = clips + i * TIDE_SKINNED_CLIP_SIZE;
        word(bytes, c, names + name_offsets[i]);
        word(bytes, c + 4u, lengths[i]);
        word(bytes, c + 8u, i == 0u ? 0u : i + 1u);
        word(bytes, c + 12u, i ? 1u : 2u);
        number(bytes, c + 16u, 2.0f);
    }
    const uint32_t key_start = at;
    static const float translations[] = {2,0,0, 4,0,0}, rotations[] = {0,0,0,1, 0,0,1,0};
    static const float scales[] = {1,1,1, 3,1,1};
    static const float cubic[] = {0,0,0, 0,3,0, 0,1,0, 0,1,0, 0,5,0, 0,0,0};
    uint32_t c = channel(bytes, channels, &at, 1u, TIDE_SKINNED_TRANSLATION, TIDE_SKINNED_LINEAR, translations);
    c = channel(bytes, c, &at, 2u, TIDE_SKINNED_ROTATION, TIDE_SKINNED_LINEAR, rotations);
    c = channel(bytes, c, &at, 2u, TIDE_SKINNED_SCALE, TIDE_SKINNED_STEP, scales);
    (void)channel(bytes, c, &at, 2u, TIDE_SKINNED_TRANSLATION, TIDE_SKINNED_CUBICSPLINE, cubic);
    word(bytes, 80u, key_start);
    word(bytes, 84u, at - key_start);
    word(bytes, 8u, at);
    return tide_list_from(bytes, (int32_t)at, 1u);
}

static tide_bone_transform bone(const tide_list pose, const int32_t index)
{
    tide_bone_transform out = {0};
    const tide_bone_transform *value = tide_list_at(pose, index, sizeof out);
    if (value) out = *value;
    return out;
}

// A separate animation export: no geometry or skin, an unrelated mesh node before the arm and the hand before its parent; node indices and counts differ from model().
static const char separate_nodes[] =
    "{\"name\":\"mesh\",\"translation\":[9,0,0]},"
    "{\"name\":\"hand\",\"translation\":[0,3,0]},"
    "{\"name\":\"arm\",\"translation\":[-2,0,0],\"children\":[1]}";

static tide_list separate_model(const char *nodes)
{
    char json[4096];
    const int length = snprintf(json, sizeof json,
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[%s],"
        "\"buffers\":[{\"byteLength\":56,\"uri\":\"data:application/octet-stream;base64,"
        "AAAAAAAAAEAAAADAAAAAAAAAAAAAAIDAAAAAAAAAAAAAAAAAAABAQAAAAAAAAAAAAABAQAAAAAA=\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":8,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":32,\"byteLength\":24}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
        "\"animations\":[{\"name\":\"move\",\"samplers\":[{\"input\":0,\"output\":1}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"translation\"}}]},"
        "{\"name\":\"hold\",\"samplers\":[{\"input\":0,\"output\":2}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}]}]}", nodes);
    if (length < 0 || (size_t)length >= sizeof json) return (tide_list){0};
    uint8_t *bytes = NULL;
    uint32_t size = 0;
    char why[256], note[256];
    const bool decoded = tide_skinned_data_decode(json, (size_t)length, false, NULL, NULL, &bytes, &size, why, note);
    if (!decoded) printf("separate animation fixture: %s (%s)\n", why, note);
    const tide_list out = decoded ? tide_list_from(bytes, (int32_t)size, 1u) : (tide_list){0};
    tide_skinned_data_free(bytes);
    return out;
}

static uint64_t pose_hash(const tide_list pose)
{
    uint64_t hash = TIDE_HASH_START;
    for (int32_t i = 0; i < tide_list_count(pose); i++) {
        const tide_bone_transform value = bone(pose, i);
        hash = tide_hash_more(hash, &value, sizeof value);
    }
    return tide_hash_end(hash);
}

// Typed geometry is deliberately simple: sampling consumes only the portable bytes. The alias variant embeds them in writable packed geometry to test proof invalidation.
static uint8_t *sample_model_pack(const tide_list data, const bool alias, size_t *size)
{
    tide_pack_builder b = {0};
    const uint32_t bytes = (uint32_t)tide_list_count(data);
    const void *flat = tide_list_at(data, 0, 1u);
    uint8_t h[32];
    uint32_t offsets[3];
    if (alias) {
        uint8_t carrier[2048u + sizeof(tide_block)] = {0};
        if (bytes > 2048u) return NULL;
        const tide_block header = {.a = bytes, .b = bytes};
        memcpy(carrier, &header, sizeof header);
        memcpy(carrier + sizeof header, flat, bytes);
        const uint32_t carrier_size = bytes + (uint32_t)sizeof header, indices[3] = {0u, 0u, 0u};
        uint32_t writable[2];
        tide_sha256_of(carrier, carrier_size, h);
        tide_pack_mesh(&b, h, carrier, carrier_size, 1u, indices, 3u, writable);
        tide_pack_name(&b, "Game.writable", TIDE_PACK_MESH, writable[0], writable[1], 1u);
        offsets[0] = writable[0] + (uint32_t)sizeof(tide_block);
    } else {
        tide_sha256_of(flat, bytes, h);
        uint32_t chars;
        offsets[0] = tide_pack_bytes(&b, h, flat, bytes, &chars);
    }
    const tide_float3 vertices[3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    const uint32_t indices[3] = {0u, 1u, 2u};
    tide_sha256_of(vertices, sizeof vertices, h);
    tide_pack_mesh(&b, h, vertices, sizeof vertices[0], 3u, indices, 3u, &offsets[1]);
    const uint32_t record = tide_pack_skinned_list(&b, offsets, 1u);
    tide_pack_name(&b, "Game.actor", TIDE_PACK_SKINNED, record, 1u, 1u);
    return tide_pack_end(&b, size);
}

TIDE_TEST(skinned_own_promotes_runtime_models_and_preserves_values_and_leases)
{
    const uint32_t mark = tide_scratch_mark();
    tide_heap heap = {0}, local_heap = {0}, snapshot = {0}, back = {0};
    tide_text_use(&heap, &local_heap);
    tide_list raw = model(), stored = raw, local = raw;
    const uint32_t size = (uint32_t)tide_list_count(raw);
    uint8_t *expected = malloc(size);
    memcpy(expected, tide_list_at(raw, 0, 1u), size);
    tide_skinned_own(&stored, TIDE_IN_MATCH);
    tide_skinned_own(&local, TIDE_IN_LOCAL);
    bool nonnegative = false;
    uint64_t version = 0;
    TIDE_REQUIRE(tide_pack_skinned_sample_proof(stored, &nonnegative) && nonnegative);
    TIDE_CHECK(tide_pack_skinned_version(stored, &version) && version == tide_hash(expected, size));
    TIDE_CHECK(tide_pack_skinned_sample_proof(local, NULL));
    TIDE_CHECK(!tide_list_at_mut(stored, 0, 1u));
    tide_list copy = stored;
    tide_skinned_own(&copy, TIDE_IN_MATCH);
    TIDE_CHECK(copy.at != stored.at && tide_list_at(copy, 0, 1u) == tide_list_at(stored, 0, 1u));
    tide_list a = {0}, b = {0};
    for (int32_t clip = 0; clip < 3; clip++) {
        for (int32_t step = 0; step < 9; step++) {
            TIDE_REQUIRE(tide_skinned_sample(raw, clip, (float)step * 0.25f, &a, TIDE_IN_SCRATCH));
            TIDE_REQUIRE(tide_skinned_sample(stored, clip, (float)step * 0.25f, &b, TIDE_IN_SCRATCH));
            for (int32_t node = 0; node < 3; node++)
                TIDE_CHECK(!memcmp(tide_list_at(a, node, sizeof(tide_bone_transform)),
                                   tide_list_at(b, node, sizeof(tide_bone_transform)), sizeof(tide_bone_transform)));
        }
    }
    tide_scratch_reset(mark);
    tide_heap_copy(&snapshot, &heap);
    const uint64_t original = tide_heap_hash(0, &snapshot);
    // Promote the incoming value before releasing an aliased old destination.
    tide_list replacement = stored;
    tide_skinned_own(&replacement, TIDE_IN_MATCH);
    tide_list_release(&stored, TIDE_IN_MATCH);
    stored = replacement;
    tide_heap_flush(&heap);
    TIDE_CHECK(!memcmp(tide_list_at(stored, 0, 1u), expected, size));
    tide_skinned_data_view view; tide_skinned_channel_data channel_data;
    TIDE_REQUIRE(tide_skinned_data_open(expected, size, &view));
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 0u, &channel_data));
    tide_list_prepare(&stored, 1u, TIDE_IN_MATCH);
    number(tide_list_at_mut(stored, 0, 1u), channel_data.values_at, 7.0f);
    TIDE_CHECK(!tide_pack_skinned_sample_proof(stored, NULL));
    TIDE_CHECK(tide_pack_skinned_sample_proof(copy, NULL));
    TIDE_CHECK(!memcmp(tide_list_at(copy, 0, 1u), expected, size));
    TIDE_CHECK(tide_heap_hash(0, &snapshot) == original);
    a = (tide_list){0}; b = (tide_list){0};
    TIDE_REQUIRE(tide_skinned_sample(stored, 0, 0, &a, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_sample(copy, 0, 0, &b, TIDE_IN_SCRATCH));
    TIDE_CHECK(((const tide_bone_transform *)tide_list_at(a, 1, sizeof(tide_bone_transform)))->position.x == 7.0f);
    TIDE_CHECK(((const tide_bone_transform *)tide_list_at(b, 1, sizeof(tide_bone_transform)))->position.x == 2.0f);
    const uint32_t packed_size = tide_heap_packed_size(&heap);
    uint8_t *bytes = malloc(packed_size);
    tide_writer writer = {bytes, packed_size, 0, false, false};
    tide_heap_pack(&heap, &writer);
    TIDE_REQUIRE(!writer.overflow && writer.size == packed_size);
    tide_reader reader = {bytes, packed_size, 0, false};
    TIDE_REQUIRE(tide_heap_unpack(&back, &reader));
    TIDE_CHECK(reader.at == packed_size && tide_heap_hash(0, &back) == tide_heap_hash(0, &heap));
    tide_heap_free(&heap);
    TIDE_CHECK(!memcmp(tide_list_read(&snapshot, copy, 0, 1u), expected, size));
    tide_text_use(&back, &local_heap);
    TIDE_REQUIRE(tide_pack_skinned_sample_proof(copy, NULL));
    a = (tide_list){0};
    TIDE_REQUIRE(tide_skinned_sample(copy, 0, 0, &a, TIDE_IN_SCRATCH));
    TIDE_CHECK(((const tide_bone_transform *)tide_list_at(a, 1, sizeof(tide_bone_transform)))->position.x == 2.0f);
    TIDE_CHECK(!memcmp(tide_list_at(local, 0, 1u), expected, size));
    tide_text_use(NULL, NULL);
    tide_heap_free(&back); tide_heap_free(&snapshot); tide_heap_free(&local_heap);
    tide_scratch_reset(mark); free(bytes); free(expected);
}

TIDE_TEST(skinned_own_handles_chunked_sources_and_invalid_data_without_false_proof)
{
    const uint32_t mark = tide_scratch_mark();
    tide_heap heap = {0};
    tide_text_use(&heap, NULL);
    tide_list raw = model(), scratch = raw;
    tide_skinned_own(&scratch, TIDE_IN_SCRATCH);
    TIDE_CHECK(scratch.at == raw.at && !tide_pack_skinned_sample_proof(scratch, NULL));
    const uint32_t old_size = (uint32_t)tide_list_count(raw), size = old_size + TIDE_LIST_CHUNK * 2u;
    uint8_t *large = calloc(size, 1u);
    memcpy(large, tide_list_at(raw, 0, 1u), old_size);
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(large, old_size, &view));
    word(large, 8u, size); word(large, 84u, view.key_size + size - old_size);
    tide_list chunked = {0}, temporary = tide_list_from(large, (int32_t)size, 1u);
    tide_list_set(&chunked, temporary, 1u, TIDE_IN_MATCH);
    tide_list owned = chunked;
    tide_skinned_own(&owned, TIDE_IN_MATCH);
    TIDE_REQUIRE(tide_pack_skinned_sample_proof(owned, NULL));
    TIDE_CHECK(!memcmp(tide_list_at(owned, 0, 1u), large, size));
    tide_list_release(&chunked, TIDE_IN_MATCH); tide_heap_flush(&heap);
    TIDE_CHECK(tide_skinned_animation_count(owned) == 3);
    tide_skinned_channel_data c;
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 0u, &c));
    number(large, c.times_at + 4u, -2.0f);
    tide_list invalid = tide_list_from(large, (int32_t)size, 1u);
    tide_skinned_own(&invalid, TIDE_IN_MATCH);
    TIDE_CHECK(!tide_pack_skinned_sample_proof(invalid, NULL));
    bool copied = false;
    void *flat = tide_list_flatten(invalid, 1u, false, &copied);
    TIDE_CHECK(!memcmp(flat, large, size));
    tide_list_unflatten(invalid, flat, 1u, false, copied);
    tide_list empty = {0}; tide_skinned_own(&empty, TIDE_IN_MATCH); TIDE_CHECK(!empty.at);
    tide_text_use(NULL, NULL);
    tide_list no_heap = raw; tide_skinned_own(&no_heap, TIDE_IN_MATCH); TIDE_CHECK(no_heap.at == raw.at);
    tide_heap_free(&heap); tide_scratch_reset(mark); free(large);
}

TIDE_TEST(skinned_own_retains_packed_models_after_unload)
{
    const uint32_t mark = tide_scratch_mark();
    tide_list raw = model();
    size_t size;
    uint8_t *pack = sample_model_pack(raw, false, &size);
    TIDE_REQUIRE(pack && tide_pack_load(pack, size));
    tide_pack_use();
    const tide_list packed = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
    tide_heap heap = {0}; tide_text_use(&heap, NULL);
    tide_list stored = packed; tide_skinned_own(&stored, TIDE_IN_MATCH);
    TIDE_REQUIRE(tide_pack_skinned_sample_proof(stored, NULL));
    tide_list invalid = {TIDE_IN_SCRATCH << 30 | TIDE_PACK_BASE | ((uint32_t)size - 1u)};
    tide_skinned_own(&invalid, TIDE_IN_MATCH);
    TIDE_CHECK(!invalid.at);
    tide_pack_unload(); tide_scratch_reset(mark); free(pack);
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_sample(stored, 0, 1.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(((const tide_bone_transform *)tide_list_at(pose, 1, sizeof(tide_bone_transform)))->position.x == 3.0f);
    tide_text_use(NULL, NULL); tide_heap_free(&heap); tide_scratch_reset(mark);
}

// Five nonuniform keys exercise both sides of the binary-search decisions and exact-key selection; equal cubic endpoint slopes make each midpoint the average of its neighbors.
static tide_list sampling_search_model(void)
{
    tide_list data = model();
    tide_skinned_data_view view;
    if (!tide_skinned_data_open(tide_list_at(data, 0, 1u), (size_t)tide_list_count(data), &view)) return (tide_list){0};
    const uint32_t channel_at = view.channel_at, key_at = view.key_at;
    uint32_t at = view.size;
    tide_list_resize(&data, (int32_t)(at + 360u), 1u, TIDE_IN_SCRATCH);
    uint8_t *bytes = tide_list_at_mut(data, 0, 1u);
    static const float times[] = {0.25f, 0.5f, 1.0f, 1.5f, 2.0f}, values[] = {2, 4, 8, 16, 32};
    static const uint32_t channels[] = {0u, 2u, 3u};
    for (uint32_t c = 0; c < 3u; c++) {
        const uint32_t record = channel_at + channels[c] * TIDE_SKINNED_CHANNEL_SIZE;
        word(bytes, record + 12u, 5u);
        word(bytes, record + 16u, at);
        for (uint32_t k = 0; k < 5u; k++) number(bytes, at + k * 4u, times[k]);
        at += 20u;
        word(bytes, record + 20u, at);
        for (uint32_t k = 0; k < 5u; k++) {
            if (c == 2u) {
                number(bytes, at, 2.0f);
                at += 12u;
            }
            number(bytes, at, values[k]);
            number(bytes, at + 4u, 11.0f);
            number(bytes, at + 8u, -0.0f);
            at += 12u;
            if (c == 2u) {
                number(bytes, at, 2.0f);
                at += 12u;
            }
        }
    }
    word(bytes, 8u, at);
    word(bytes, 84u, at - key_at);
    return data;
}

// One combined clip and one isolated clip per channel give an independent search oracle: isolated calls can't reuse another channel's interval.
static tide_list sampling_intervals_model(void)
{
    tide_list data = model();
    tide_skinned_data_view view;
    if (!tide_skinned_data_open(tide_list_at(data, 0, 1u), (size_t)tide_list_count(data), &view)) return (tide_list){0};
    uint32_t at = view.size;
    tide_list_resize(&data, (int32_t)(at + 8192u), 1u, TIDE_IN_SCRATCH);
    uint8_t *bytes = tide_list_at_mut(data, 0, 1u);
    const uint32_t clips = table(bytes, &at, 48u, 11u, TIDE_SKINNED_CLIP_SIZE);
    const uint32_t channels = table(bytes, &at, 56u, 10u, TIDE_SKINNED_CHANNEL_SIZE);
    const uint32_t key_start = at;
    static const uint32_t counts[] = {5u, 5u, 5u, 5u, 5u, 5u, 9u, 33u, 5u, 1u};
    static const uint32_t modes[] = {TIDE_SKINNED_LINEAR, TIDE_SKINNED_LINEAR, TIDE_SKINNED_STEP,
        TIDE_SKINNED_CUBICSPLINE, TIDE_SKINNED_CUBICSPLINE, TIDE_SKINNED_LINEAR,
        TIDE_SKINNED_STEP, TIDE_SKINNED_LINEAR, TIDE_SKINNED_LINEAR, TIDE_SKINNED_STEP};
    static const float common[] = {0.25f, 0.5f, 1.0f, 1.5f, 2.0f};
    static const float different[] = {0.25f, 0.75f, 1.25f, 1.75f, 2.25f};
    for (uint32_t c = 0; c < 10u; c++) {
        const uint32_t record = channels + c * TIDE_SKINNED_CHANNEL_SIZE;
        const uint32_t path = c < 9u ? c % 3u : TIDE_SKINNED_SCALE;
        const uint32_t width = path == TIDE_SKINNED_ROTATION ? 4u : 3u;
        const uint32_t copies = modes[c] == TIDE_SKINNED_CUBICSPLINE ? 3u : 1u;
        word(bytes, record, c < 9u ? c / 3u : 2u);
        word(bytes, record + 4u, path);
        word(bytes, record + 8u, modes[c]);
        word(bytes, record + 12u, counts[c]);
        word(bytes, record + 16u, at);
        for (uint32_t k = 0; k < counts[c]; k++) {
            const float time = c == 5u ? different[k] : counts[c] == 5u ? common[k]
                : counts[c] == 1u ? 0.375f : (float)k * (counts[c] == 33u ? 0.0625f : 0.25f);
            number(bytes, at, time);
            at += 4u;
        }
        word(bytes, record + 20u, at);
        for (uint32_t k = 0; k < counts[c]; k++) {
            for (uint32_t copy = 0; copy < copies; copy++) {
                for (uint32_t part = 0; part < width; part++) {
                    const bool tangent = copies == 3u && copy != 1u;
                    const float value = part == 2u ? -0.0f : tangent ? (float)(part + 1u) * 0.125f
                        : part == 3u ? 1.0f : (float)((k + 1u) * (c + 1u) * (part + 1u)) * 0.125f;
                    number(bytes, at, value);
                    at += 4u;
                }
            }
        }
    }
    for (uint32_t c = 0; c < 11u; c++) {
        const uint32_t record = clips + c * TIDE_SKINNED_CLIP_SIZE;
        word(bytes, record, view.string_at + 10u); // Existing "linear" name.
        word(bytes, record + 4u, 6u);
        word(bytes, record + 8u, c ? c - 1u : 0u);
        word(bytes, record + 12u, c ? 1u : 10u);
        number(bytes, record + 16u, 3.0f);
    }
    word(bytes, 80u, key_start);
    word(bytes, 84u, at - key_start);
    word(bytes, 8u, at);
    tide_list_resize(&data, (int32_t)at, 1u, TIDE_IN_SCRATCH);
    return data;
}

TIDE_TEST(skinned_sampling_intervals_match_isolated_search_bits)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = sampling_intervals_model();
    TIDE_REQUIRE(tide_skinned_valid(raw));
    size_t size;
    uint8_t *pack = sample_model_pack(raw, false, &size);
    TIDE_REQUIRE(pack && tide_pack_load(pack, size));
    const tide_list packed = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
    uint64_t version;
    TIDE_REQUIRE(tide_pack_skinned_version(packed, &version));
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(tide_list_at(raw, 0, 1u), (size_t)tide_list_count(raw), &view));
    tide_heap heap = {0};
    tide_text_use(&heap, NULL);
    tide_list owned = {0}, stored = {0};
    tide_list_set(&owned, raw, 1u, TIDE_IN_MATCH);
    const float times[] = {-1.0f, -0.0f, 0.125f, 0.25f, 0.375f, 0.5f,
        tide_f_from_bits(tide_f_bits(0.5f) - 1u), tide_f_from_bits(tide_f_bits(0.5f) + 1u),
        0.625f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 3.0f, 4.0f,
        tide_f_from_bits(0x7fc00000u), tide_f_from_bits(0x7f800000u), tide_f_from_bits(0xff800000u)};
    const tide_list sources[] = {raw, packed, owned};
    for (uint32_t t = 0; t < sizeof times / sizeof times[0]; t++) {
        tide_list expected = {0};
        TIDE_REQUIRE(tide_skinned_rest(raw, &expected, TIDE_IN_SCRATCH));
        for (uint32_t c = 0; c < 10u; c++) {
            tide_list isolated = {0};
            tide_skinned_channel_data channel;
            TIDE_REQUIRE(tide_skinned_data_channel(&view, c, &channel));
            TIDE_REQUIRE(tide_skinned_sample(raw, (int32_t)c + 1, times[t], &isolated, TIDE_IN_SCRATCH));
            const tide_bone_transform from = bone(isolated, (int32_t)channel.node);
            tide_bone_transform *to = tide_list_at_mut(expected, (int32_t)channel.node, sizeof *to);
            TIDE_REQUIRE(to);
            if (channel.path == TIDE_SKINNED_TRANSLATION) to->position = from.position;
            else if (channel.path == TIDE_SKINNED_SCALE) to->scale = from.scale;
            else to->rotation = from.rotation;
        }
        for (uint32_t s = 0; s < 3u; s++) {
            tide_list pose = {0};
            TIDE_REQUIRE(tide_skinned_sample(sources[s], 0, times[t], &pose, TIDE_IN_SCRATCH));
            for (int32_t node = 0; node < tide_list_count(expected); node++) {
                const tide_bone_transform first = bone(expected, node), second = bone(pose, node);
                TIDE_CHECK(memcmp(&first, &second, sizeof first) == 0);
            }
        }
        TIDE_REQUIRE(tide_skinned_sample(packed, 0, times[t], &stored, TIDE_IN_MATCH));
        TIDE_CHECK(pose_hash(stored) == pose_hash(expected));
    }
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_pack_unload();
    free(pack);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_sampling_search_preserves_modes_keys_endpoints_and_nonfinite_time)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = sampling_search_model();
    TIDE_REQUIRE(tide_skinned_valid(raw));
    size_t size;
    uint8_t *pack = sample_model_pack(raw, false, &size);
    TIDE_REQUIRE(pack && tide_pack_load(pack, size));
    const tide_list packed = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
    uint64_t version;
    TIDE_REQUIRE(tide_pack_skinned_version(packed, &version));
    tide_heap heap = {0};
    tide_text_use(&heap, NULL);
    tide_list owned = {0};
    tide_list_set(&owned, raw, 1u, TIDE_IN_MATCH);
    const tide_list sources[] = {raw, packed, owned};
    const float times[] = {tide_f_from_bits(0xff800000u), tide_f_from_bits(0x7fc00000u), -1, 0, 0.125f,
                           0.25f, 0.375f, 0.5f, 0.75f, 1, 1.25f, 1.5f, 1.75f, 2, 3, tide_f_from_bits(0x7f800000u)};
    const float smooth[] = {2, 2, 2, 2, 2, 2, 3, 4, 6, 8, 12, 16, 24, 32, 32, 32};
    const float stepped[] = {2, 2, 2, 2, 2, 2, 2, 4, 4, 8, 8, 16, 16, 32, 32, 32};
    for (int32_t clip = 0; clip < 3; clip++) {
        for (uint32_t t = 0; t < sizeof times / sizeof times[0]; t++) {
            tide_list expected = {0};
            for (uint32_t s = 0; s < 3u; s++) {
                tide_list pose = {0};
                TIDE_REQUIRE(tide_skinned_sample(sources[s], clip, times[t], &pose, TIDE_IN_SCRATCH));
                const tide_bone_transform value = bone(pose, clip == 0 ? 1 : 2);
                const tide_float3 sampled = clip == 1 ? value.scale : value.position;
                TIDE_CHECK(sampled.x == (clip == 1 ? stepped[t] : smooth[t]) && sampled.y == 11.0f);
                if (t < 5u || t >= 13u || clip == 1) TIDE_CHECK(tide_f_bits(sampled.z) == 0x80000000u);
                if (s == 0u) expected = pose;
                else TIDE_CHECK(pose_hash(pose) == pose_hash(expected));
            }
        }
    }
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_pack_unload();
    free(pack);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_sampling_rejects_later_bad_keys_before_any_output_write)
{
    const uint32_t mark = tide_scratch_mark();
    static const uint32_t channels[] = {0u, 2u, 3u};
    const float times[] = {-1, 0, 0.375f, 1, 2, 3, tide_f_from_bits(0x7fc00000u), tide_f_from_bits(0x7f800000u)};
    for (int32_t clip = 0; clip < 3; clip++) {
        for (uint32_t variant = 0; variant < 7u; variant++) {
            if (variant == 5u && clip != 2) continue; // A cubic tangent, separate from its value.
            const tide_list raw = sampling_search_model();
            uint8_t *bytes = tide_list_at_mut(raw, 0, 1u);
            tide_skinned_data_view view;
            tide_skinned_channel_data c;
            TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(raw), &view));
            TIDE_REQUIRE(tide_skinned_data_channel(&view, channels[clip], &c));
            tide_list scratch = {0};
            TIDE_REQUIRE(tide_skinned_rest(raw, &scratch, TIDE_IN_SCRATCH));
            tide_heap heap = {0};
            tide_text_use(&heap, NULL);
            tide_list owned = {0}, stored = {0};
            TIDE_REQUIRE(tide_skinned_rest(raw, &stored, TIDE_IN_MATCH));
            if (variant < 3u) {
                const uint32_t bad[] = {0x7fc00000u, 0x7f800000u, tide_f_bits(0.5f)};
                word(bytes, c.times_at + (c.keys - 1u) * 4u, bad[variant]);
            } else if (variant == 6u) word(bytes, view.channel_at + channels[clip] * TIDE_SKINNED_CHANNEL_SIZE + 20u, view.size - 4u);
            else {
                const uint32_t copies = clip == 2 ? 3u : 1u;
                const uint32_t value = c.values_at + ((c.keys - 1u) * 3u * copies + (copies == 3u && variant != 5u ? 3u : 0u)) * 4u;
                word(bytes, value, variant == 4u ? 0x7f800000u : 0x7fc00000u);
            }
            tide_list_set(&owned, raw, 1u, TIDE_IN_MATCH);
            const uint64_t scratch_before = pose_hash(scratch), stored_before = pose_hash(stored), heap_before = tide_heap_hash(TIDE_HASH_START, &heap);
            for (uint32_t t = 0; t < sizeof times / sizeof times[0]; t++) {
                TIDE_CHECK(!tide_skinned_sample(raw, clip, times[t], &scratch, TIDE_IN_SCRATCH));
                TIDE_CHECK(!tide_skinned_sample(owned, clip, times[t], &stored, TIDE_IN_MATCH));
                TIDE_CHECK(pose_hash(scratch) == scratch_before && pose_hash(stored) == stored_before);
                TIDE_CHECK(tide_heap_hash(TIDE_HASH_START, &heap) == heap_before);
            }
            tide_text_use(NULL, NULL);
            tide_heap_free(&heap);
        }
    }
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_owned_key_scans_check_every_lane_and_tail_before_writing)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = sampling_intervals_model(), target = model();
    uint8_t *bytes = tide_list_at_mut(raw, 0, 1u);
    bytes[88] ^= 1u; // Exercise separate binding rather than exact-rig delegation.
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(raw), &view));
    tide_heap heap = {0};
    tide_text_use(&heap, NULL);
    tide_list owned = {0}, scratch = {0}, separate = {0}, stored = {0};
    TIDE_REQUIRE(tide_skinned_rest(raw, &scratch, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_rest(target, &separate, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_rest(raw, &stored, TIDE_IN_MATCH));
    const uint64_t scratch_hash = pose_hash(scratch), separate_hash = pose_hash(separate), stored_hash = pose_hash(stored);
    const uint32_t tracks[] = {7u, 4u, 3u};
    const uint32_t bad[] = {0x7fc00000u, 0x7f800000u, 0xff800000u};
    for (uint32_t t = 0; t < sizeof tracks / sizeof *tracks; t++) {
        tide_skinned_channel_data c;
        TIDE_REQUIRE(tide_skinned_data_channel(&view, tracks[t], &c));
        const int32_t clip = (int32_t)tracks[t] + 1;
        tide_list valid = {0};
        TIDE_REQUIRE(tide_skinned_sample_into(target, raw, clip, 0.6f, &valid, TIDE_IN_SCRATCH));
        const uint32_t width = c.path == TIDE_SKINNED_ROTATION ? 4u : 3u;
        const uint32_t copies = c.interpolation == TIDE_SKINNED_CUBICSPLINE ? 3u : 1u;
        for (uint32_t pass = 0; pass < 2u; pass++) {
            const uint32_t count = pass ? c.keys * width * copies : c.keys;
            const uint32_t first = pass ? c.values_at : c.times_at;
            for (uint32_t i = 0; i < count; i++) {
                uint8_t original[4];
                memcpy(original, bytes + first + i * 4u, sizeof original);
                for (uint32_t b = 0; b < sizeof bad / sizeof *bad + (pass ? 0u : 1u); b++) {
                    // Zero is negative/nonincreasing except at the first key,
                    // where use -1; finite invalid times must also be rejected.
                    word(bytes, first + i * 4u, b < sizeof bad / sizeof *bad ? bad[b] : i ? 0u : tide_f_bits(-1.0f));
                    tide_list_set(&owned, raw, 1u, TIDE_IN_MATCH);
                    const uint64_t heap_hash = tide_heap_hash(TIDE_HASH_START, &heap);
                    TIDE_CHECK(!tide_skinned_sample(raw, clip, 0.6f, &scratch, TIDE_IN_SCRATCH));
                    TIDE_CHECK(!tide_skinned_sample(owned, clip, 0.6f, &stored, TIDE_IN_MATCH));
                    TIDE_CHECK(!tide_skinned_sample_into(target, owned, clip, 0.6f, &separate, TIDE_IN_SCRATCH));
                    TIDE_CHECK(pose_hash(scratch) == scratch_hash && pose_hash(stored) == stored_hash);
                    TIDE_CHECK(pose_hash(separate) == separate_hash && tide_heap_hash(TIDE_HASH_START, &heap) == heap_hash);
                }
                memcpy(bytes + first + i * 4u, original, sizeof original);
            }
        }
    }
    tide_skinned_channel_data long_track;
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 7u, &long_track));
    const uint32_t descriptor = view.channel_at + 7u * TIDE_SKINNED_CHANNEL_SIZE;
    for (uint32_t keys = 1u; keys <= long_track.keys; keys++) {
        word(bytes, descriptor + 12u, keys);
        tide_list valid_tail = {0};
        TIDE_REQUIRE(tide_skinned_sample(raw, 8, 0.6f, &valid_tail, TIDE_IN_SCRATCH));
        const uint32_t offsets[] = {long_track.times_at + (keys - 1u) * 4u,
            long_track.values_at + (keys * 4u - 1u) * 4u};
        for (uint32_t i = 0; i < sizeof offsets / sizeof *offsets; i++) {
            uint8_t original[4];
            memcpy(original, bytes + offsets[i], sizeof original);
            word(bytes, offsets[i], 0x7f800000u);
            TIDE_CHECK(!tide_skinned_sample(raw, 8, 0.6f, &scratch, TIDE_IN_SCRATCH));
            TIDE_CHECK(pose_hash(scratch) == scratch_hash);
            memcpy(bytes + offsets[i], original, sizeof original);
        }
    }
    word(bytes, descriptor + 12u, long_track.keys);
    number(bytes, long_track.times_at, -0.0f);
    tide_list valid = {0};
    TIDE_REQUIRE(tide_skinned_sample(raw, 8, 0.6f, &valid, TIDE_IN_SCRATCH));
    number(bytes, long_track.times_at + 4u, 0.0f);
    TIDE_CHECK(!tide_skinned_sample(raw, 8, 0.6f, &scratch, TIDE_IN_SCRATCH));
    number(bytes, long_track.times_at, 0.0f);
    number(bytes, long_track.times_at + 4u, -0.0f);
    TIDE_CHECK(!tide_skinned_sample(raw, 8, 0.6f, &scratch, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(scratch) == scratch_hash);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_sampling_keeps_chunked_output_ownership_and_snapshots)
{
    const uint32_t mark = tide_scratch_mark();
    tide_list data = sampling_search_model();
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(tide_list_at(data, 0, 1u), (size_t)tide_list_count(data), &view));
    uint8_t original[3u * TIDE_SKINNED_NODE_SIZE];
    memcpy(original, view.data + view.node_at, sizeof original);
    const uint32_t nodes = TIDE_LIST_CHUNK / (uint32_t)sizeof(tide_bone_transform) + 2u, at = view.size;
    const uint32_t bytes = at + nodes * TIDE_SKINNED_NODE_SIZE;
    tide_list_resize(&data, (int32_t)bytes, 1u, TIDE_IN_SCRATCH);
    uint8_t *flat = tide_list_at_mut(data, 0, 1u);
    word(flat, 8u, bytes);
    word(flat, 16u, nodes);
    word(flat, 20u, at);
    memcpy(flat + at, original, sizeof original);
    for (uint32_t i = 3u; i < nodes; i++) {
        memcpy(flat + at + i * TIDE_SKINNED_NODE_SIZE, original, TIDE_SKINNED_NODE_SIZE);
        word(flat, at + i * TIDE_SKINNED_NODE_SIZE, 0u);
    }
    TIDE_REQUIRE(tide_skinned_valid(data));
    tide_heap heap = {0}, snapshot = {0};
    tide_text_use(&heap, NULL);
    tide_list stored = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &stored, TIDE_IN_MATCH));
    TIDE_REQUIRE(tide_block_at(stored.at)->b & TIDE_LIST_CHUNKED);
    const tide_list previous = stored;
    tide_heap_copy(&snapshot, &heap);
    for (int32_t clip = 0; clip < 3; clip++) {
        tide_list scratch = {0};
        // Resize keeps an already correctly sized heap output in place even
        // when the requested destination is scratch. Select by actual tag.
        TIDE_REQUIRE(tide_skinned_sample(data, clip, 0.75f, &stored, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_sample(data, clip, 0.75f, &scratch, TIDE_IN_SCRATCH));
        TIDE_CHECK(stored.at == previous.at && stored.at >> 30 == TIDE_IN_MATCH);
        TIDE_CHECK(pose_hash(stored) == pose_hash(scratch));
        const tide_bone_transform *old = tide_list_read(&snapshot, previous, 1, sizeof *old);
        TIDE_REQUIRE(old);
        TIDE_CHECK(old->position.x == 2.0f && old->position.y == 0.0f);
        old = tide_list_read(&snapshot, previous, (int32_t)nodes - 1, sizeof *old);
        TIDE_REQUIRE(old);
        TIDE_CHECK(old->rotation.value.w == 1.0f && old->scale.x == 1.0f);
    }
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_heap_free(&snapshot);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_packed_samples_match_owned_bits_and_reject_negative_first_keys)
{
    const uint32_t mark = tide_scratch_mark();
    uint32_t previous_handle = 0u;
    for (uint32_t variant = 0; variant < 4u; variant++) {
        const tide_list raw = model();
        uint8_t *flat = tide_list_at_mut(raw, 0, 1u);
        const uint32_t bytes = (uint32_t)tide_list_count(raw);
        tide_skinned_data_view view;
        tide_skinned_channel_data channel;
        TIDE_REQUIRE(tide_skinned_data_open(flat, bytes, &view));
        TIDE_REQUIRE(tide_skinned_data_channel(&view, variant >= 2u ? 1u : 0u, &channel));
        if (variant) number(flat, channel.times_at, variant == 3u ? -0.0f : -0.5f);
        // Full validation permits this strictly increasing negative first
        // time, while sampling has always required every key time >=0.
        TIDE_REQUIRE(tide_skinned_valid(raw));
        size_t size;
        uint8_t *pack = sample_model_pack(raw, false, &size);
        TIDE_REQUIRE(pack && tide_pack_load(pack, size));
        const tide_list packed = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
        uint64_t version;
        TIDE_REQUIRE(tide_pack_skinned_version(packed, &version));
        if (previous_handle) TIDE_CHECK(packed.at == previous_handle);
        previous_handle = packed.at;
        bool nonnegative = variant != 0u && variant != 3u;
        TIDE_REQUIRE(tide_pack_skinned_sample_proof(packed, &nonnegative));
        TIDE_CHECK(nonnegative == (variant == 0u || variant == 3u));
        TIDE_CHECK(tide_pack_skinned_sample_proof(packed, NULL));
        tide_heap heap = {0};
        tide_text_use(&heap, NULL);
        tide_list owned = {0}, a = {0}, b = {0}, stored = {0};
        tide_list_set(&owned, raw, 1u, TIDE_IN_MATCH);
        TIDE_CHECK(!tide_pack_skinned_version(owned, &version));
        nonnegative = true;
        TIDE_CHECK(!tide_pack_skinned_sample_proof(owned, &nonnegative) && nonnegative);
        TIDE_REQUIRE(tide_skinned_rest(raw, &a, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_rest(raw, &b, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_rest(raw, &stored, TIDE_IN_MATCH));
        const float times[] = {-1.0f, 0.0f, 0.375f, 1.0f, 2.0f, 3.0f};
        for (int32_t clip = 0; clip < 3; clip++) {
            for (uint32_t i = 0; i < sizeof times / sizeof times[0]; i++) {
                const uint64_t before_a = pose_hash(a), before_b = pose_hash(b), before_stored = pose_hash(stored);
                const uint64_t heap_before = tide_heap_hash(TIDE_HASH_START, &heap);
                const bool expected = variant == 0u || variant == 3u || clip != 0;
                TIDE_CHECK(tide_skinned_sample(packed, clip, times[i], &a, TIDE_IN_SCRATCH) == expected);
                TIDE_CHECK(tide_skinned_sample(owned, clip, times[i], &b, TIDE_IN_SCRATCH) == expected);
                TIDE_CHECK(tide_skinned_sample(packed, clip, times[i], &stored, TIDE_IN_MATCH) == expected);
                if (!expected) {
                    TIDE_CHECK(pose_hash(a) == before_a && pose_hash(b) == before_b && pose_hash(stored) == before_stored);
                    TIDE_CHECK(tide_heap_hash(TIDE_HASH_START, &heap) == heap_before);
                    TIDE_CHECK(!tide_skinned_sample(raw, clip, times[i], &b, TIDE_IN_SCRATCH));
                }
                TIDE_CHECK(tide_list_count(a) == tide_list_count(b));
                for (int32_t node = 0; node < tide_list_count(a); node++) {
                    const tide_bone_transform first = bone(a, node), second = bone(b, node), third = bone(stored, node);
                    TIDE_CHECK(memcmp(&first, &second, sizeof first) == 0);
                    TIDE_CHECK(memcmp(&first, &third, sizeof first) == 0);
                }
            }
        }
        const uint64_t unchanged = pose_hash(a);
        TIDE_CHECK(!tide_skinned_sample(packed, -1, 0, &a, TIDE_IN_SCRATCH));
        TIDE_CHECK(!tide_skinned_sample(packed, 3, 0, &a, TIDE_IN_SCRATCH));
        TIDE_CHECK(pose_hash(a) == unchanged);
        tide_text_use(NULL, NULL);
        tide_heap_free(&heap);
        free(pack);
    }
    tide_pack_unload();
    bool unchanged = true;
    TIDE_CHECK(!tide_pack_skinned_sample_proof((tide_list){previous_handle}, &unchanged) && unchanged);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_packed_sampling_rechecks_later_keys_after_proof_invalidation)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw = model();
    size_t size;
    uint8_t *pack = sample_model_pack(raw, true, &size);
    TIDE_REQUIRE(pack && tide_pack_load(pack, size));
    const tide_list packed = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
    uint64_t version;
    TIDE_REQUIRE(tide_pack_skinned_version(packed, &version));
    bool nonnegative = false;
    TIDE_REQUIRE(tide_pack_skinned_sample_proof(packed, &nonnegative));
    TIDE_CHECK(nonnegative);
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_sample(packed, 0, 1.0f, &pose, TIDE_IN_SCRATCH));
    const uint64_t before = pose_hash(pose);
    const uint32_t carrier_size = (uint32_t)tide_list_count(raw) + (uint32_t)sizeof(tide_block);
    uint32_t count;
    uint8_t *to = tide_pack_mesh_open(tide_pack_find("Game.writable", TIDE_PACK_MESH), 0u, carrier_size, &count);
    TIDE_REQUIRE(to && count == 1u);
    tide_skinned_data_view view;
    tide_skinned_channel_data channel;
    TIDE_REQUIRE(tide_skinned_data_open(to + sizeof(tide_block), (size_t)tide_list_count(raw), &view));
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 0u, &channel));
    number(to + sizeof(tide_block), channel.times_at + 4u, 0.0f);
    tide_pack_mesh_updated(to, carrier_size);
    TIDE_CHECK(!tide_pack_skinned_version(packed, &version));
    TIDE_CHECK(!tide_pack_skinned_sample_proof(packed, &nonnegative) && nonnegative);
    TIDE_CHECK(!tide_skinned_sample(packed, 0, 1.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    tide_pack_unload();
    free(pack);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_sampling_rechecks_later_channel_fields_after_proof_invalidation)
{
    const uint32_t mark = tide_scratch_mark();
    for (uint32_t field = 0; field < 6u; field++) {
        const tide_list raw = model();
        size_t size;
        uint8_t *pack = sample_model_pack(raw, true, &size);
        TIDE_REQUIRE(pack && tide_pack_load(pack, size));
        const tide_list packed = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
        uint64_t version;
        TIDE_REQUIRE(tide_pack_skinned_version(packed, &version));
        tide_list pose = {0};
        TIDE_REQUIRE(tide_skinned_sample(packed, 0, 1.0f, &pose, TIDE_IN_SCRATCH));
        const uint64_t before = pose_hash(pose);
        const uint32_t carrier_size = (uint32_t)tide_list_count(raw) + (uint32_t)sizeof(tide_block);
        uint32_t count;
        uint8_t *to = tide_pack_mesh_open(tide_pack_find("Game.writable", TIDE_PACK_MESH), 0u, carrier_size, &count);
        TIDE_REQUIRE(to && count == 1u);
        uint8_t *bytes = to + sizeof(tide_block);
        tide_skinned_data_view view;
        TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(raw), &view));
        const uint32_t invalid[] = {view.nodes, TIDE_SKINNED_SCALE + 1u, TIDE_SKINNED_CUBICSPLINE + 1u,
                                    0u, view.key_at + view.key_size, view.key_at + view.key_size};
        word(bytes, view.channel_at + TIDE_SKINNED_CHANNEL_SIZE + field * 4u, invalid[field]);
        tide_pack_mesh_updated(to, carrier_size);
        TIDE_CHECK(!tide_pack_skinned_version(packed, &version));
        tide_heap heap = {0};
        tide_text_use(&heap, NULL);
        tide_list owned = {0}, stored = {0};
        tide_list_set(&owned, packed, 1u, TIDE_IN_MATCH);
        tide_list_set(&stored, pose, sizeof(tide_bone_transform), TIDE_IN_MATCH);
        const uint64_t heap_before = tide_heap_hash(TIDE_HASH_START, &heap);
        TIDE_CHECK(!tide_skinned_sample(packed, 0, 1.0f, &pose, TIDE_IN_SCRATCH));
        TIDE_CHECK(!tide_skinned_sample(owned, 0, 1.0f, &stored, TIDE_IN_MATCH));
        TIDE_CHECK(pose_hash(pose) == before && pose_hash(stored) == before);
        TIDE_CHECK(tide_heap_hash(TIDE_HASH_START, &heap) == heap_before);
        tide_text_use(NULL, NULL);
        tide_heap_free(&heap);
        tide_pack_unload();
        free(pack);
    }
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_rest_rechecks_invalidated_proof_and_keeps_owned_outputs)
{
    const uint32_t mark = tide_scratch_mark();
    const struct { uint32_t field, bits; } malformed[] = {
        {0u, 1u}, // The arm cannot be its own parent.
        {8u, UINT32_MAX}, // Its name must still lie inside the name table.
        {16u, 0x7fc00000u}, // Nonfinite position, rotation and scale.
        {28u, 0x7f800000u},
        {44u, 0xff800000u},
    };
    for (uint32_t test = 0; test < sizeof malformed / sizeof malformed[0]; test++) {
        const tide_list raw = model();
        size_t size;
        uint8_t *pack = sample_model_pack(raw, true, &size);
        TIDE_REQUIRE(pack && tide_pack_load(pack, size));
        const tide_list packed = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
        uint64_t version;
        TIDE_REQUIRE(tide_pack_skinned_version(packed, &version));
        tide_heap heap = {0};
        tide_text_use(&heap, NULL);
        tide_list owned = {0}, scratch = {0}, stored = {0}, checked = {0};
        tide_list_set(&owned, raw, 1u, TIDE_IN_MATCH);
        TIDE_REQUIRE(tide_skinned_rest(raw, &scratch, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_rest(packed, &checked, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_rest(owned, &stored, TIDE_IN_MATCH));
        TIDE_CHECK(pose_hash(scratch) == pose_hash(checked) && pose_hash(scratch) == pose_hash(stored));
        // A sampled sentinel differs from rest, making an accidental write
        // visible even when a failure is reported after initialization.
        TIDE_REQUIRE(tide_skinned_sample(raw, 0, 1.0f, &scratch, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_sample(raw, 0, 1.0f, &stored, TIDE_IN_MATCH));
        const uint64_t unchanged = pose_hash(scratch);
        const uint32_t carrier_size = (uint32_t)tide_list_count(raw) + (uint32_t)sizeof(tide_block);
        uint32_t count;
        uint8_t *carrier = tide_pack_mesh_open(tide_pack_find("Game.writable", TIDE_PACK_MESH), 0u, carrier_size, &count);
        TIDE_REQUIRE(carrier && count == 1u);
        uint8_t *bytes = carrier + sizeof(tide_block);
        tide_skinned_data_view view;
        TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(raw), &view));
        const uint32_t at = view.node_at + TIDE_SKINNED_NODE_SIZE + malformed[test].field;
        word(bytes, at, malformed[test].bits);
        tide_pack_mesh_updated(carrier, carrier_size);
        TIDE_CHECK(!tide_pack_skinned_version(packed, &version));
        word(tide_list_at_mut(raw, 0, 1u), at, malformed[test].bits);
        tide_list_set(&owned, raw, 1u, TIDE_IN_MATCH);
        const tide_list sources[] = {packed, raw, owned};
        const uint64_t before_heap = tide_heap_hash(TIDE_HASH_START, &heap);
        for (uint32_t source = 0; source < 3u; source++) {
            TIDE_CHECK(!tide_skinned_rest(sources[source], &scratch, TIDE_IN_SCRATCH));
            TIDE_CHECK(!tide_skinned_rest(sources[source], &stored, TIDE_IN_MATCH));
            TIDE_CHECK(pose_hash(scratch) == unchanged && pose_hash(stored) == unchanged);
            TIDE_CHECK(tide_heap_hash(TIDE_HASH_START, &heap) == before_heap);
        }
        tide_text_use(NULL, NULL);
        tide_heap_free(&heap);
        tide_pack_unload();
        free(pack);
    }
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_separate_animation_exports_bind_by_required_rig)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list target = model(), source = separate_model(separate_nodes);
    TIDE_REQUIRE(tide_skinned_valid(source));
    TIDE_CHECK(!tide_skinned_compatible(target, source));
    TIDE_CHECK(tide_skinned_bone_count(source) == 4 && tide_skinned_animation_count(source) == 2);
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_sample_into(target, source, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_list_count(pose) == 3 && bone(pose, 1).position.x == 3.0f && bone(pose, 2).position.y == 3.0f);
    TIDE_REQUIRE(tide_skinned_sample_into(target, source, 0, 0, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).position.x == 2.0f);

    TIDE_REQUIRE(tide_skinned_sample_into(source, target, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_list_count(pose) == 4 && bone(pose, 1).position.x == -9.0f && bone(pose, 2).position.x == 3.0f);
    TIDE_CHECK(tide_abs_f(bone(pose, 3).rotation.value.z - 0.70710677f) < 0.000001f);

    // A different rest quaternion sign is the same orientation, without
    // changing the numeric sign of values copied from the animation export.
    const tide_list sign = separate_model(
        "{\"name\":\"mesh\"},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"arm\",\"translation\":[-2,0,0],\"rotation\":[0,0,0,-1],\"children\":[1]}");
    TIDE_REQUIRE(tide_skinned_sample_into(target, sign, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).rotation.value.w == -1.0f);
    tide_list sign_pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(sign, &sign_pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_pose_into(target, sign, sign_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).rotation.value.w == -1.0f);
    const tide_list unrelated_rest = separate_model(
        "{\"name\":\"mesh\",\"translation\":[20,0,0]},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"arm\",\"translation\":[-2,0,0],\"children\":[1]}");
    TIDE_REQUIRE(tide_skinned_sample_into(unrelated_rest, source, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).position.x == -20.0f && bone(pose, 2).position.x == 3.0f);
    TIDE_REQUIRE(tide_skinned_rest(source, &sign_pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_pose_into(unrelated_rest, source, sign_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).position.x == -20.0f);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_separate_bindings_reject_changed_or_ambiguous_required_nodes)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list target = model(), source = separate_model(separate_nodes);
    tide_list pose = {0}, source_pose = {0};
    TIDE_REQUIRE(tide_skinned_sample_into(target, source, 0, 1, &pose, TIDE_IN_SCRATCH));
    const uint64_t before = pose_hash(pose);
    const uint32_t place = pose.at;
    static const char *const wrong[] = {
        "{\"name\":\"mesh\"},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"missing\",\"translation\":[-2,0,0],\"children\":[1]}",
        "{\"name\":\"arm\"},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"arm\",\"translation\":[-2,0,0],\"children\":[1]}",
        "{\"name\":\"mesh\"},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"translation\":[-2,0,0],\"children\":[1]}"
    };
    for (uint32_t i = 0; i < sizeof wrong / sizeof *wrong; i++) {
        const tide_list invalid = separate_model(wrong[i]);
        TIDE_REQUIRE(tide_skinned_valid(invalid));
        TIDE_CHECK(!tide_skinned_sample_into(target, invalid, 0, 1, &pose, TIDE_IN_SCRATCH));
        TIDE_CHECK(pose.at == place && pose_hash(pose) == before);
        TIDE_CHECK(!tide_skinned_sample_into(invalid, source, 0, 1, &pose, TIDE_IN_SCRATCH));
        TIDE_CHECK(pose.at == place && pose_hash(pose) == before);
    }
    const tide_list changed_parent = separate_model(
        "{\"name\":\"mesh\"},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"arm\",\"translation\":[-2,0,0]}");
    TIDE_CHECK(!tide_skinned_sample_into(target, changed_parent, 1, 0, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose.at == place && pose_hash(pose) == before);
    TIDE_REQUIRE(tide_skinned_rest(changed_parent, &source_pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(!tide_skinned_pose_into(target, changed_parent, source_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose.at == place && pose_hash(pose) == before);
    TIDE_CHECK(!tide_skinned_sample_into(target, source, -1, 0, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(!tide_skinned_sample_into(target, source, 2, 0, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(!tide_skinned_pose_into(target, source, (tide_list){0}, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose.at == place && pose_hash(pose) == before);
    const tide_list unrequired_duplicate = separate_model(
        "{\"name\":\"hand\"},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"arm\",\"translation\":[-2,0,0],\"children\":[1]}");
    TIDE_REQUIRE(tide_skinned_sample_into(target, unrequired_duplicate, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    TIDE_REQUIRE(tide_skinned_rest(unrequired_duplicate, &source_pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(!tide_skinned_pose_into(target, unrequired_duplicate, source_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_binding_validates_all_nodes_before_private_reads)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list target = model(), source = separate_model(separate_nodes);
    tide_list source_pose = {0}, output = {0};
    TIDE_REQUIRE(tide_skinned_sample(source, 0, 1, &source_pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_pose_into(target, source, source_pose, &output, TIDE_IN_SCRATCH));
    const uint64_t before = pose_hash(output);
    const uint32_t place = output.at;
    for (uint32_t side = 0; side < 2u; side++) {
        const tide_list data = side ? source : target;
        uint8_t *bytes = tide_list_at_mut(data, 0, 1u);
        tide_skinned_data_view view;
        TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(data), &view));
        for (uint32_t node = 0; node < view.nodes; node++) {
            for (uint32_t field = 0; field < 13u; field++) {
                const uint32_t offset = field == 0u ? 0u : field == 1u ? 8u : field == 2u ? 12u : 16u + (field - 3u) * 4u;
                const uint32_t at = view.node_at + node * TIDE_SKINNED_NODE_SIZE + offset;
                uint8_t original[4];
                memcpy(original, bytes + at, sizeof original);
                word(bytes, at, field == 0u ? node : field < 3u ? UINT32_MAX : 0x7fc00000u);
                TIDE_CHECK(!tide_skinned_sample_into(target, source, 0, 1, &output, TIDE_IN_SCRATCH));
                TIDE_CHECK(!tide_skinned_pose_into(target, source, source_pose, &output, TIDE_IN_SCRATCH));
                TIDE_CHECK(output.at == place && pose_hash(output) == before);
                memcpy(bytes + at, original, sizeof original);
            }
        }
    }
    TIDE_REQUIRE(tide_skinned_pose_into(target, source, source_pose, &output, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(output) == before);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_separate_exports_copy_different_default_poses_without_retargeting)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list target = model(), source = separate_model(
        "{\"name\":\"mesh\"},{\"name\":\"hand\",\"translation\":[1,7,2],\"scale\":[2,3,4]},"
        "{\"name\":\"arm\",\"translation\":[-5,6,8],\"rotation\":[0,0,1,0],\"scale\":[3,2,1],\"children\":[1]}");
    TIDE_REQUIRE(tide_skinned_valid(source));
    TIDE_CHECK(!tide_skinned_compatible(target, source));
    tide_list sampled = {0}, into = {0}, rebound = {0};
    TIDE_REQUIRE(tide_skinned_sample(source, 1, 1, &sampled, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_sample_into(target, source, 1, 1, &into, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_pose_into(target, source, sampled, &rebound, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(into) == pose_hash(rebound));
    const tide_bone_transform arm = bone(into, 1), hand = bone(into, 2);
    TIDE_CHECK(arm.position.x == 5 && arm.position.y == 6 && arm.position.z == 8);
    TIDE_CHECK(arm.rotation.value.z == -1 && arm.rotation.value.w == 0);
    TIDE_CHECK(arm.scale.x == 3 && arm.scale.y == 2 && arm.scale.z == 1);
    TIDE_CHECK(hand.position.x == 0 && hand.position.y == 3 && hand.position.z == 0);
    TIDE_CHECK(hand.scale.x == 2 && hand.scale.y == 3 && hand.scale.z == 4);
    tide_heap heap = {0}, snapshot = {0};
    tide_text_use(&heap, NULL);
    tide_list stored = {0};
    TIDE_REQUIRE(tide_skinned_rest(target, &stored, TIDE_IN_MATCH));
    const tide_list previous = stored;
    tide_heap_copy(&snapshot, &heap);
    TIDE_REQUIRE(tide_skinned_sample_into(target, source, 1, 1, &stored, TIDE_IN_MATCH));
    TIDE_CHECK(pose_hash(stored) == pose_hash(into));
    const tide_bone_transform *old = tide_list_read(&snapshot, previous, 1, sizeof *old);
    TIDE_REQUIRE(old);
    TIDE_CHECK(old->position.x == 2 && old->scale.x == 1);
    TIDE_REQUIRE(tide_skinned_pose_into(target, source, sampled, &sampled, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(sampled) == pose_hash(into));
    tide_text_use(NULL, NULL);
    tide_heap_free(&snapshot);
    tide_heap_free(&heap);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_separate_poses_bind_procedural_edits_aliases_and_capture_draws)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list target = model(), source = separate_model(separate_nodes);
    tide_list source_pose = {0}, pose = {0};
    TIDE_REQUIRE(tide_skinned_sample(source, 0, 1, &source_pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_pose_into(target, source, source_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_list_count(pose) == 3 && bone(pose, 1).position.x == 3.0f);
    tide_bone_transform *hand = tide_list_at_mut(source_pose, 3, sizeof *hand);
    TIDE_REQUIRE(hand);
    hand->position.x = 7.0f;
    TIDE_REQUIRE(tide_skinned_pose_into(target, source, source_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 2).position.x == 7.0f);
    const uint64_t before = pose_hash(pose);
    tide_bone_transform *extra = tide_list_at_mut(source_pose, 1, sizeof *extra);
    TIDE_REQUIRE(extra);
    extra->position.x = 1.0f;
    // A node the model hasn't got is skipped (2026-10-27): editing it moves nothing.
    TIDE_CHECK(tide_skinned_pose_into(target, source, source_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    extra->position.x = -9.0f;

    tide_draw_list draw = {0};
    uint32_t textures[2];
    int32_t info[3];
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, target, pose, textures, info));
    const uint32_t palette_texture = textures[0];
    TIDE_CHECK(info[0] == 2 && info[1] == 3 && info[2] == 6);
    const float *matrices = (const float *)(const void *)draw.textures[palette_texture - 1u].pixels;
    TIDE_CHECK(matrices[12] == 1.0f && matrices[28] == 8.0f);
    hand->position.x = 4.0f;
    TIDE_REQUIRE(tide_skinned_pose_into(target, source, source_pose, &source_pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_list_count(source_pose) == 3 && bone(source_pose, 2).position.x == 4.0f);
    matrices = (const float *)(const void *)draw.textures[palette_texture - 1u].pixels;
    TIDE_CHECK(matrices[12] == 1.0f && matrices[28] == 8.0f);
    tide_draw_free(&draw);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_separate_sampling_keeps_owned_snapshots_and_exact_bits)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list target = model(), source = separate_model(separate_nodes);
    tide_list original = {0}, exact = {0};
    TIDE_REQUIRE(tide_skinned_sample(target, 0, 1, &original, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_sample_into(target, target, 0, 1, &exact, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(original) == pose_hash(exact));
    TIDE_REQUIRE(tide_skinned_pose_into(target, target, original, &exact, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(original) == pose_hash(exact));
    tide_heap heap = {0}, snapshot = {0};
    tide_text_use(&heap, NULL);
    tide_list stored = {0};
    TIDE_REQUIRE(tide_skinned_rest(target, &stored, TIDE_IN_MATCH));
    const tide_list previous = stored;
    tide_heap_copy(&snapshot, &heap);
    TIDE_REQUIRE(tide_skinned_sample_into(target, source, 0, 1, &stored, TIDE_IN_MATCH));
    TIDE_CHECK(bone(stored, 1).position.x == 3.0f);
    const tide_bone_transform *old = tide_list_read(&snapshot, previous, 1, sizeof *old);
    TIDE_REQUIRE(old);
    TIDE_CHECK(old->position.x == 2.0f);
    tide_list source_pose = {0};
    TIDE_REQUIRE(tide_skinned_sample(source, 0, 2, &source_pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_pose_into(target, source, source_pose, &stored, TIDE_IN_MATCH));
    TIDE_CHECK(bone(stored, 1).position.x == 4.0f);
    old = tide_list_read(&snapshot, previous, 1, sizeof *old);
    TIDE_REQUIRE(old);
    TIDE_CHECK(old->position.x == 2.0f);
    tide_heap_free(&heap);
    tide_heap_free(&snapshot);
    tide_text_use(NULL, NULL);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_separate_bindings_skip_only_static_identity_scene_wrappers)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list target = model();
    tide_list wrapped = separate_model(
        "{\"children\":[2]},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"arm\",\"translation\":[-2,0,0],\"children\":[1]}");
    TIDE_REQUIRE(tide_skinned_valid(wrapped));
    tide_skinned_data_view view;
    uint8_t *bytes = tide_list_at_mut(wrapped, 0, 1u);
    TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(wrapped), &view));
    // FBX conversion adds a constant scene-root translation channel to retain
    // the take's duration. Turn the fixture's second clip into that channel.
    const uint32_t wrapper_channel = view.channel_at + TIDE_SKINNED_CHANNEL_SIZE;
    word(bytes, wrapper_channel, 1u);
    tide_skinned_channel_data channel;
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 1u, &channel));
    for (uint32_t i = 0; i < 6u; i++) number(bytes, channel.values_at + i * 4u, 0.0f);
    TIDE_REQUIRE(tide_skinned_valid(wrapped));
    tide_list source_pose = {0}, pose = {0};
    TIDE_REQUIRE(tide_skinned_sample_into(target, wrapped, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).position.x == 3.0f && bone(pose, 2).position.y == 3.0f);
    TIDE_REQUIRE(tide_skinned_sample_into(target, wrapped, 1, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 0).position.x == 0.0f && bone(pose, 1).position.x == 2.0f);
    TIDE_REQUIRE(tide_skinned_sample(wrapped, 0, 1, &source_pose, TIDE_IN_SCRATCH));
    tide_bone_transform *root = tide_list_at_mut(source_pose, 0, sizeof *root);
    TIDE_REQUIRE(root);
    root->position.x = 5.0f;
    TIDE_REQUIRE(tide_skinned_pose_into(target, wrapped, source_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 0).position.x == 5.0f && bone(pose, 1).position.x == 3.0f);
    const uint64_t before = pose_hash(pose);
    tide_bone_transform *wrapper = tide_list_at_mut(source_pose, 1, sizeof *wrapper);
    TIDE_REQUIRE(wrapper);
    wrapper->position.x = 1.0f;
    TIDE_CHECK(!tide_skinned_pose_into(target, wrapped, source_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    wrapper->position.x = 0.0f;
    number(bytes, channel.values_at + 12u, 1.0f);
    TIDE_CHECK(!tide_skinned_sample_into(target, wrapped, 1, 0, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(!tide_skinned_pose_into(target, wrapped, source_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    number(bytes, channel.values_at + 12u, 0.0f);
    TIDE_REQUIRE(tide_skinned_sample_into(wrapped, target, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).position.x == 0.0f && bone(pose, 2).position.x == 3.0f);
    const uint64_t reverse = pose_hash(pose);
    number(bytes, view.node_at + TIDE_SKINNED_NODE_SIZE + 16u, 1.0f);
    TIDE_CHECK(!tide_skinned_sample_into(wrapped, target, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == reverse);
    const tide_list nested = separate_model(
        "{\"children\":[1]},{\"children\":[2]},{\"name\":\"arm\",\"translation\":[-2,0,0]}");
    TIDE_REQUIRE(tide_skinned_valid(nested));
    TIDE_CHECK(!tide_skinned_sample_into(target, nested, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == reverse);
    tide_scratch_reset(mark);
}

// The shape of a Blender export: the armature object is a named node above the
// skeleton's top bone, carrying the object's transform (0.01 for a centimetre
// rig). Clip "move" moves the arm and hand and animates the armature's uniform
// scale; clip "uneven" scales it unevenly.
static tide_list armature_model(void)
{
    static const char json[] =
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":["
        "{\"name\":\"Armature\",\"translation\":[1,2,3],\"rotation\":[0.70710677,0,0,0.70710677],"
        "\"scale\":[0.01,0.01,0.01],\"children\":[1]},"
        "{\"name\":\"arm\",\"translation\":[-200,0,0],\"children\":[2]},"
        "{\"name\":\"hand\",\"translation\":[0,300,0]}],"
        "\"buffers\":[{\"byteLength\":80,\"uri\":\"data:application/octet-stream;base64,"
        "AAAAAAAAAEAAAADAAAAAAAAAAAAAAIDAAAAAAAAAAAAK1yM8CtcjPArXIzwK16M8CtejPArXozwK1yM8CtejPArXIzwK1yM8CtejPArXIzw=\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":8,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":32,\"byteLength\":24},{\"buffer\":0,\"byteOffset\":56,\"byteLength\":24}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
        "\"animations\":[{\"name\":\"move\",\"samplers\":[{\"input\":0,\"output\":1},{\"input\":0,\"output\":2}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},"
        "{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"translation\"}},"
        "{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"scale\"}}]},"
        "{\"name\":\"uneven\",\"samplers\":[{\"input\":0,\"output\":1},{\"input\":0,\"output\":3}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}},"
        "{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"scale\"}}]}]}";
    uint8_t *bytes = NULL;
    uint32_t size = 0;
    char why[256], note[256];
    const bool decoded = tide_skinned_data_decode(json, sizeof json - 1u, false, NULL, NULL, &bytes, &size, why, note);
    if (!decoded) printf("armature fixture: %s (%s)\n", why, note);
    const tide_list out = decoded ? tide_list_from(bytes, (int32_t)size, 1u) : (tide_list){0};
    tide_skinned_data_free(bytes);
    return out;
}

static bool same_model_space(const tide_list a_data, const tide_list a_pose, const tide_list b_data, const tide_list b_pose,
                             const char *name)
{
    const tide_str bone_name = tide_str_from_cstr(name);
    const tide_float4x4 a = tide_skinned_model(a_data, a_pose, tide_skinned_find_bone(a_data, bone_name));
    const tide_float4x4 b = tide_skinned_model(b_data, b_pose, tide_skinned_find_bone(b_data, bone_name));
    const float *x = &a.c0.x, *y = &b.c0.x;
    for (uint32_t i = 0; i < 16u; i++) {
        if (!(tide_abs_f(x[i] - y[i]) <= 1e-5f)) return false;
    }
    return true;
}

TIDE_TEST(skinned_separate_bindings_fold_unmatched_top_level_nodes)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list plain = model(), armature = armature_model();
    TIDE_REQUIRE(tide_skinned_valid(armature));
    TIDE_REQUIRE(tide_skinned_find_animation(armature, tide_str_from_cstr("move")) == 0);
    // A Blender clip drawn on a character exported elsewhere: the armature's
    // transform, including its animated scale, moves into the arm.
    for (uint32_t step = 0; step <= 4u; step++) {
        const float seconds = 0.5f * (float)step;
        tide_list source_pose = {0}, into = {0}, rebound = {0};
        TIDE_REQUIRE(tide_skinned_sample(armature, 0, seconds, &source_pose, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_sample_into(plain, armature, 0, seconds, &into, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_pose_into(plain, armature, source_pose, &rebound, TIDE_IN_SCRATCH));
        TIDE_CHECK(tide_list_count(into) == 3 && pose_hash(into) == pose_hash(rebound));
        TIDE_CHECK(same_model_space(plain, into, armature, source_pose, "arm"));
        TIDE_CHECK(same_model_space(plain, into, armature, source_pose, "hand"));
    }
    // The other way around, the character's own armature node is undone.
    for (uint32_t step = 0; step <= 4u; step++) {
        const float seconds = 0.5f * (float)step;
        tide_list source_pose = {0}, into = {0}, rebound = {0};
        TIDE_REQUIRE(tide_skinned_sample(plain, 0, seconds, &source_pose, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_sample_into(armature, plain, 0, seconds, &into, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_pose_into(armature, plain, source_pose, &rebound, TIDE_IN_SCRATCH));
        TIDE_CHECK(pose_hash(into) == pose_hash(rebound));
        TIDE_CHECK(same_model_space(armature, into, plain, source_pose, "arm"));
        TIDE_CHECK(same_model_space(armature, into, plain, source_pose, "hand"));
        const tide_bone_transform kept = bone(into, tide_skinned_find_bone(armature, tide_str_from_cstr("Armature")));
        TIDE_CHECK(kept.scale.x == 0.01f && kept.position.z == 3.0f);
    }
    // An uneven scale can't be folded into the arm's translation, rotation and
    // scale: both routes reject and leave their outputs alone.
    tide_list pose = {0}, source_pose = {0};
    TIDE_REQUIRE(tide_skinned_sample_into(plain, armature, 0, 1.0f, &pose, TIDE_IN_SCRATCH));
    const uint64_t before = pose_hash(pose);
    TIDE_CHECK(!tide_skinned_sample_into(plain, armature, 1, 1.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    TIDE_REQUIRE(tide_skinned_sample(armature, 1, 1.0f, &source_pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(!tide_skinned_pose_into(plain, armature, source_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    // A named node that does exist on the other side is still matched, not
    // folded: an arm below it in the clip and beside it in the model is a
    // different rig.
    const tide_list beside = separate_model(
        "{\"name\":\"mesh\"},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"arm\",\"translation\":[-2,0,0],\"children\":[1]},{\"name\":\"Armature\"}");
    TIDE_REQUIRE(tide_skinned_valid(beside));
    TIDE_CHECK(!tide_skinned_sample_into(beside, armature, 0, 1.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_pose_samples_defaults_modes_and_hierarchy)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list data = model();
    tide_list pose = {0};
    TIDE_REQUIRE(tide_skinned_valid(data));
    TIDE_CHECK(tide_skinned_compatible(data, data));
    TIDE_CHECK(!tide_skinned_compatible(data, (tide_list){0}));
    TIDE_CHECK(tide_skinned_bone_count(data) == 3 && tide_skinned_animation_count(data) == 3);
    TIDE_CHECK(tide_skinned_find_bone(data, tide_str_from_cstr("hand")) == 2);
    TIDE_CHECK(tide_skinned_find_animation(data, tide_str_from_cstr("linear")) == 0);
    TIDE_CHECK(tide_skinned_find_animation(data, tide_str_from_cstr("missing")) == -1);
    TIDE_CHECK(tide_skinned_animation_duration(data, 0) == 2.0f);
    TIDE_CHECK(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    const tide_float4x4 rest = tide_skinned_model(data, pose, 2);
    TIDE_CHECK(rest.c3.x == 2.0f && rest.c3.y == 3.0f);
    TIDE_CHECK(tide_skinned_sample(data, 1, 2.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 2).scale.x == 3.0f);
    TIDE_CHECK(tide_skinned_sample(data, 0, 1.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).position.x == 3.0f && bone(pose, 2).scale.x == 1.0f);
    TIDE_CHECK(tide_abs_f(bone(pose, 2).rotation.value.z - 0.70710677f) < 0.000001f);
    TIDE_CHECK(tide_skinned_sample(data, 0, -10.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).position.x == 2.0f);
    TIDE_CHECK(tide_skinned_sample(data, 0, 10.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).position.x == 4.0f);
    TIDE_CHECK(tide_skinned_sample(data, 1, 1.9f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 2).scale.x == 1.0f);
    TIDE_CHECK(tide_skinned_sample(data, 2, 1.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 2).position.y == 4.0f);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_pose_blends_aliases_and_snapshot_storage)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list data = model();
    tide_list a = {0}, b = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &a, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_sample(data, 0, 2.0f, &b, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_skinned_blend(a, b, 0.5f, &a, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(a, 1).position.x == 3.0f);
    tide_heap heap = {0}, snapshot = {0};
    tide_text_use(&heap, NULL);
    tide_list stored = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &stored, TIDE_IN_MATCH));
    const tide_list previous = stored;
    tide_heap_copy(&snapshot, &heap);
    TIDE_REQUIRE(tide_skinned_sample(data, 0, 2.0f, &stored, TIDE_IN_MATCH));
    TIDE_CHECK(bone(stored, 1).position.x == 4.0f);
    const tide_bone_transform *old = tide_list_read(&snapshot, previous, 1, sizeof *old);
    TIDE_REQUIRE(old);
    TIDE_CHECK(old->position.x == 2.0f);
    tide_heap_free(&heap);
    tide_heap_free(&snapshot);
    tide_text_use(NULL, NULL);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_rejects_bad_data_without_changing_pose)
{
    const uint32_t mark = tide_scratch_mark();
    tide_list data = model(), pose = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    const uint64_t hash = tide_hash(tide_list_at(pose, 0, sizeof(tide_bone_transform)), 3u * sizeof(tide_bone_transform));
    TIDE_CHECK(!tide_skinned_sample(data, -1, 1, &pose, TIDE_IN_SCRATCH));
    uint8_t *bad = tide_list_at_mut(data, 0, 1u);
    word(bad, 20u, UINT32_MAX);
    TIDE_CHECK(!tide_skinned_valid(data));
    TIDE_CHECK(!tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(hash == tide_hash(tide_list_at(pose, 0, sizeof(tide_bone_transform)), 3u * sizeof(tide_bone_transform)));
    TIDE_CHECK(tide_skinned_fingerprint(data) == 0u);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_compatibility_reads_owned_chunked_headers_and_keeps_snapshots)
{
    const uint32_t mark = tide_scratch_mark();
    tide_list data = model();
    uint8_t nodes[3u * TIDE_SKINNED_NODE_SIZE];
    memcpy(nodes, (const uint8_t *)tide_list_at(data, 0, 1u) + TIDE_SKINNED_HEADER_SIZE, sizeof nodes);
    const uint32_t bytes = TIDE_LIST_CHUNK * 32u + 4u, node_at = bytes - (uint32_t)sizeof nodes;
    tide_list_resize(&data, (int32_t)bytes, 1u, TIDE_IN_SCRATCH);
    uint8_t *source = tide_list_at_mut(data, 0, 1u);
    word(source, 8u, bytes);
    word(source, 20u, node_at);
    memcpy(source + node_at, nodes, sizeof nodes);
    TIDE_REQUIRE(tide_skinned_valid(data));

    tide_heap heap = {0}, snapshot = {0};
    tide_text_use(&heap, NULL);
    tide_list owned = {0};
    tide_list_set(&owned, data, 1u, TIDE_IN_MATCH);
    const tide_list stored = owned;
    TIDE_REQUIRE(tide_block_at(owned.at)->b & TIDE_LIST_CHUNKED);
    tide_heap_copy(&snapshot, &heap);
    const uint64_t before = tide_heap_hash(TIDE_HASH_START, &heap);
    const uint32_t scratch_before = tide_scratch_mark(), moves = heap.moves;
    for (uint32_t i = 0; i < 100u; i++) {
        TIDE_CHECK(tide_skinned_compatible(data, owned));
        TIDE_CHECK(tide_skinned_compatible(owned, data));
        TIDE_CHECK(tide_skinned_compatible(owned, owned));
    }
    TIDE_CHECK(tide_heap_hash(TIDE_HASH_START, &heap) == before && heap.moves == moves);
    TIDE_CHECK(tide_scratch_mark() == scratch_before);

    const tide_list short_header = tide_list_from(source, TIDE_SKINNED_HEADER_SIZE - 1u, 1u);
    TIDE_CHECK(!tide_skinned_compatible(data, short_header));
    TIDE_CHECK(!tide_skinned_compatible(short_header, data));
    TIDE_CHECK(!tide_skinned_compatible(data, (tide_list){0}));
    TIDE_CHECK(!tide_skinned_compatible((tide_list){0}, data));
    const struct { uint32_t at, bad, restored; } rejected[] = {
        {0u, 0u, TIDE_SKINNED_DATA_MAGIC},
        {4u, TIDE_SKINNED_DATA_VERSION + 1u, TIDE_SKINNED_DATA_VERSION},
        {8u, bytes - 4u, bytes},
        {16u, UINT32_MAX, 3u},
        {20u, bytes - 4u, node_at},
        {16u, 2u, 3u}, // Same fingerprint, different declared node count.
    };
    for (uint32_t i = 0; i < sizeof rejected / sizeof rejected[0]; i++) {
        uint8_t *header = tide_list_at_mut(owned, 0, 1u);
        word(header, rejected[i].at, rejected[i].bad);
        TIDE_CHECK(!tide_skinned_compatible(data, owned));
        TIDE_CHECK(!tide_skinned_compatible(owned, data));
        word(header, rejected[i].at, rejected[i].restored);
    }
    uint8_t *fingerprint = tide_list_at_mut(owned, 119, 1u);
    *fingerprint ^= 1u;
    TIDE_CHECK(!tide_skinned_compatible(data, owned));
    *fingerprint ^= 1u;
    // Compatibility always checked header identity rather than all body
    // contents. Keep that behavior for a malformed node in a distant chunk.
    word(tide_list_at_mut(owned, (int32_t)node_at, 1u), 0u, 0u);
    TIDE_CHECK(!tide_skinned_valid(owned));
    TIDE_CHECK(tide_skinned_compatible(data, owned));
    tide_text_use(&snapshot, NULL);
    TIDE_CHECK(tide_skinned_valid(stored));
    TIDE_CHECK(tide_skinned_compatible(data, stored));
    TIDE_CHECK(tide_heap_hash(TIDE_HASH_START, &snapshot) == before);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_heap_free(&snapshot);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_draw_captures_palettes_and_variable_influences)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list data = model();
    tide_list pose = {0};
    tide_draw_list draw = {0};
    uint32_t textures[2], other[2];
    int32_t info[3];
    TIDE_REQUIRE(tide_skinned_rest(data, &pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, textures, info));
    TIDE_CHECK(info[0] == 2 && info[1] == 3 && info[2] == 6);
    const tide_draw_texture *influences = &draw.textures[textures[1] - 1u];
    const int32_t *words = (const int32_t *)(const void *)influences->pixels;
    TIDE_CHECK(words[0] == 0 && words[1] == 2 && words[8] == 4 && words[9] == 2);
    TIDE_CHECK(words[12] == 0 && tide_f_from_bits((uint32_t)words[13]) == 0.25f);
    TIDE_CHECK(words[14] == 1 && tide_f_from_bits((uint32_t)words[15]) == 0.75f);
    TIDE_REQUIRE(tide_skinned_sample(data, 0, 2, &pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_draw_skinned_data(&draw, data, pose, other, info));
    TIDE_CHECK(other[0] != textures[0] && other[1] == textures[1]);
    const tide_draw_texture *old = &draw.textures[textures[0] - 1u];
    const float *matrices = (const float *)(const void *)old->pixels;
    TIDE_CHECK(matrices[0] == 1.0f && matrices[12] == 0.0f);
    tide_draw_free(&draw);
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_crossplatform_pose_bits)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list data = model();
    tide_list pose = {0}, rest = {0}, mixed = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &rest, TIDE_IN_SCRATCH));
    uint64_t hash = TIDE_HASH_START;
    for (int32_t clip = 0; clip < 3; clip++) {
        for (int32_t step = -1; step <= 33; step++) {
            const float time = (float)step / 16.0f;
            TIDE_REQUIRE(tide_skinned_sample(data, clip, time, &pose, TIDE_IN_SCRATCH));
            TIDE_REQUIRE(tide_skinned_blend(rest, pose, 0.375f, &mixed, TIDE_IN_SCRATCH));
            for (int32_t node = 0; node < 3; node++) {
                const tide_bone_transform value = bone(pose, node), blended = bone(mixed, node);
                const tide_float4x4 matrix = tide_skinned_model(data, mixed, node);
                hash = tide_hash_more(hash, &value, sizeof value);
                hash = tide_hash_more(hash, &blended, sizeof blended);
                hash = tide_hash_more(hash, &matrix, sizeof matrix);
            }
        }
    }
    hash = tide_hash_end(hash);
    if (hash != UINT64_C(0xe5d23f82e5d18f26)) printf("skinned pose hash: %016llx\n", (unsigned long long)hash);
    TIDE_CHECK(hash == UINT64_C(0xe5d23f82e5d18f26));
    tide_scratch_reset(mark);
}

// An export of its own nodes and one clip "go" of the given channels: sampler 0
// moves a node from x -2 to -4 over two seconds, sampler 1 holds y at 3.
static tide_list channels_model(const char *nodes, const char *channels)
{
    char json[4096];
    const int length = snprintf(json, sizeof json,
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[%s],"
        "\"buffers\":[{\"byteLength\":56,\"uri\":\"data:application/octet-stream;base64,"
        "AAAAAAAAAEAAAADAAAAAAAAAAAAAAIDAAAAAAAAAAAAAAAAAAABAQAAAAAAAAAAAAABAQAAAAAA=\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":8,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":32,\"byteLength\":24}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
        "\"animations\":[{\"name\":\"go\",\"samplers\":[{\"input\":0,\"output\":1},{\"input\":0,\"output\":2}],"
        "\"channels\":[%s]}]}", nodes, channels);
    if (length < 0 || (size_t)length >= sizeof json) return (tide_list){0};
    uint8_t *bytes = NULL;
    uint32_t size = 0;
    char why[256], note[256];
    const bool decoded = tide_skinned_data_decode(json, (size_t)length, false, NULL, NULL, &bytes, &size, why, note);
    if (!decoded) printf("channels fixture: %s (%s)\n", why, note);
    const tide_list out = decoded ? tide_list_from(bytes, (int32_t)size, 1u) : (tide_list){0};
    tide_skinned_data_free(bytes);
    return out;
}

#define CHANNEL(sampler, node) "{\"sampler\":" #sampler ",\"target\":{\"node\":" #node ",\"path\":\"translation\"}}"

// 2026-10-27: a clip may animate nodes the model lacks (IK helpers, another
// part's bones); those are skipped. Nodes both have keep their parent chains.
TIDE_TEST(skinned_separate_bindings_skip_nodes_the_model_lacks)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list target = model(), plain = separate_model(separate_nodes);
    // The plain export's arm, with a top-level ik_settings, an ik_foot_root > ik_foot_l chain and a twist below the arm,
    // all animated, none of them the model's.
    const tide_list extra = channels_model(
        "{\"name\":\"ik_settings\"},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"arm\",\"translation\":[-2,0,0],\"children\":[1,4]},{\"name\":\"ik_foot_root\",\"children\":[5]},"
        "{\"name\":\"twist\"},{\"name\":\"ik_foot_l\"}",
        CHANNEL(0, 2) "," CHANNEL(0, 0) "," CHANNEL(1, 5) "," CHANNEL(1, 4));
    TIDE_REQUIRE(tide_skinned_valid(extra));
    tide_list expected = {0}, pose = {0};
    TIDE_REQUIRE(tide_skinned_sample_into(target, plain, 0, 1, &expected, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_sample_into(target, extra, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == pose_hash(expected) && bone(pose, 1).position.x == 3.0f);
    // A pose of the export binds the same way, and draws.
    tide_list plain_pose = {0}, extra_pose = {0}, from_plain = {0}, from_extra = {0};
    TIDE_REQUIRE(tide_skinned_sample(plain, 0, 1, &plain_pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_sample(extra, 0, 1, &extra_pose, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_pose_into(target, plain, plain_pose, &from_plain, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_pose_into(target, extra, extra_pose, &from_extra, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(from_extra) == pose_hash(from_plain));
    tide_draw_list draw = {0};
    uint32_t textures[2];
    int32_t info[3];
    TIDE_CHECK(tide_draw_skinned_data(&draw, target, from_extra, textures, info) && info[0] == 2 && info[1] == 3);
    tide_draw_free(&draw);

    // A model with only part of the chain: the arm is moved, the hand the clip holds is skipped.
    const tide_list arm_only = channels_model("{\"name\":\"arm\",\"translation\":[-2,0,0]}", CHANNEL(0, 0));
    TIDE_REQUIRE(tide_skinned_sample_into(arm_only, extra, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_list_count(pose) == 2 && bone(pose, 1).position.x == 3.0f);
    TIDE_REQUIRE(tide_skinned_sample_into(arm_only, plain, 1, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_list_count(pose) == 2 && bone(pose, 1).position.x == 2.0f);
    TIDE_REQUIRE(tide_skinned_pose_into(arm_only, extra, extra_pose, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).position.x == 3.0f);

    // A node both have, below one the model lacks, is a different chain: refused, the pose left as it was.
    const tide_list elbow = channels_model(
        "{\"name\":\"mesh\"},{\"name\":\"hand\",\"translation\":[0,3,0]},{\"name\":\"arm\",\"children\":[3]},"
        "{\"name\":\"elbow\",\"children\":[1]}", CHANNEL(1, 1));
    const uint64_t before = pose_hash(expected);
    TIDE_CHECK(!tide_skinned_sample_into(target, elbow, 0, 1, &expected, TIDE_IN_SCRATCH));
    tide_list elbow_pose = {0};
    TIDE_REQUIRE(tide_skinned_sample(elbow, 0, 1, &elbow_pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(!tide_skinned_pose_into(target, elbow, elbow_pose, &expected, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(expected) == before);
    // A clip of none of the model's nodes is another rig: refused.
    const tide_list other = channels_model("{\"name\":\"tail\"},{\"name\":\"fin\"}", CHANNEL(0, 0) "," CHANNEL(1, 1));
    TIDE_CHECK(!tide_skinned_sample_into(target, other, 0, 1, &expected, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(expected) == before);
    // A duplicate name the clip needs still refuses.
    const tide_list twice = channels_model("{\"name\":\"arm\"},{\"name\":\"arm\"}", CHANNEL(0, 0));
    TIDE_CHECK(!tide_skinned_sample_into(target, twice, 0, 1, &expected, TIDE_IN_SCRATCH));
    tide_scratch_reset(mark);
}

// A LINEAR rotation clip from q to -q, the same orientation, holds q: the
// sampler takes the shorter way between keys, whatever their signs.
TIDE_TEST(skinned_linear_rotation_between_opposite_signs_takes_the_short_way)
{
    const uint32_t mark = tide_scratch_mark();
    // Keys (0.6, 0, 0, 0.8) and (-0.6, 0, 0, -0.8) at 0 and 2 seconds.
    char json[2048];
    snprintf(json, sizeof json,
        "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"name\":\"spine\"}],"
        "\"buffers\":[{\"byteLength\":40,\"uri\":\"data:application/octet-stream;base64,"
        "AAAAAAAAAECamRk/AAAAAAAAAADNzEw/mpkZvwAAAAAAAAAAzcxMvw==\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":8,\"byteLength\":32}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}],"
        "\"animations\":[{\"name\":\"turn\",\"samplers\":[{\"input\":0,\"output\":1}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"rotation\"}}]}]}");
    uint8_t *bytes = NULL;
    uint32_t size = 0;
    char why[256], note[256];
    TIDE_REQUIRE(tide_skinned_data_decode(json, strlen(json), false, NULL, NULL, &bytes, &size, why, note));
    const tide_list data = tide_list_from(bytes, (int32_t)size, 1u);
    tide_skinned_data_free(bytes);
    for (int i = 0; i <= 4; i++) {
        tide_list pose = {0};
        TIDE_REQUIRE(tide_skinned_sample(data, 0, 0.5f * (float)i, &pose, TIDE_IN_SCRATCH));
        const tide_quaternion q = bone(pose, 1).rotation;
        // x is reflected on import: (0.6, 0, 0, 0.8) is (0.6, -0, -0, 0.8) in Tide, up to sign
        const float sign = q.value.w < 0.0f ? -1.0f : 1.0f;
        TIDE_CHECK(tide_abs_f(q.value.x * sign - 0.6f) < 1e-6f && tide_abs_f(q.value.w * sign - 0.8f) < 1e-6f
                   && tide_abs_f(q.value.y) < 1e-6f && tide_abs_f(q.value.z) < 1e-6f);
    }
    tide_scratch_reset(mark);
}

// The model as a packed SkinnedMesh and the others as packed bytes, as
// imported Animation assets are: "Game.clip1" on.
static uint8_t *plan_pack(const tide_list *data, const uint32_t count, size_t *size)
{
    tide_pack_builder b = {0};
    uint8_t h[32];
    uint32_t offsets[3], chars;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t bytes = (uint32_t)tide_list_count(data[i]);
        const void *flat = tide_list_at(data[i], 0, 1u);
        tide_sha256_of(flat, bytes, h);
        const uint32_t at = tide_pack_bytes(&b, h, flat, bytes, &chars);
        if (i == 0u) {
            offsets[0] = at;
            continue;
        }
        char name[32];
        snprintf(name, sizeof name, "Game.clip%u", i);
        tide_pack_name(&b, name, TIDE_PACK_BYTES, at, bytes, chars);
    }
    const tide_float3 vertices[3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    const uint32_t indices[3] = {0u, 1u, 2u};
    tide_sha256_of(vertices, sizeof vertices, h);
    tide_pack_mesh(&b, h, vertices, sizeof vertices[0], 3u, indices, 3u, &offsets[1]);
    const uint32_t record = tide_pack_skinned_list(&b, offsets, 1u);
    tide_pack_name(&b, "Game.actor", TIDE_PACK_SKINNED, record, 1u, 1u);
    return tide_pack_end(&b, size);
}

static tide_list plan_packed(const uint32_t i)
{
    if (i == 0u) return tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
    char name[32];
    snprintf(name, sizeof name, "Game.clip%u", i);
    return (tide_list){tide_pack_handle(tide_pack_find(name, TIDE_PACK_BYTES))};
}

TIDE_TEST(skinned_kept_single_keys_preserve_modes_duplicate_order_and_folded_nodes)
{
    const uint32_t mark = tide_scratch_mark();
    for (uint32_t reversed = 0; reversed < 2u; reversed++) {
        const uint32_t variant_mark = tide_scratch_mark();
        tide_list raw[3] = {sampling_intervals_model(), armature_model(), model()};
        for (uint32_t source = 0; source < 2u; source++) {
            uint8_t *bytes = tide_list_at_mut(raw[source], 0, 1u);
            tide_skinned_data_view view;
            TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(raw[source]), &view));
            for (uint32_t c = 0; c < view.channels; c++) {
                // Keep a changing scale beside a later single-key scale for
                // the same node. Cubic tracks require at least two keys;
                // their original interpolation remains beside the constants.
                if (source == 0u && c == 8u) continue;
                tide_skinned_channel_data channel_data;
                TIDE_REQUIRE(tide_skinned_data_channel(&view, c, &channel_data));
                if (channel_data.interpolation == TIDE_SKINNED_CUBICSPLINE) continue;
                word(bytes, view.channel_at + c * TIDE_SKINNED_CHANNEL_SIZE + 12u, 1u);
            }
            if (source == 0u && reversed) {
                uint8_t saved[TIDE_SKINNED_CHANNEL_SIZE];
                uint8_t *a = bytes + view.channel_at + 8u * TIDE_SKINNED_CHANNEL_SIZE;
                uint8_t *b = a + TIDE_SKINNED_CHANNEL_SIZE;
                memcpy(saved, a, sizeof saved); memcpy(a, b, sizeof saved); memcpy(b, saved, sizeof saved);
            }
            TIDE_REQUIRE(tide_skinned_valid(raw[source]));
        }
        size_t size;
        uint8_t *pack = plan_pack(raw, 3u, &size);
        TIDE_REQUIRE(pack && tide_pack_load(pack, size));
        tide_heap heap = {0}, snapshot = {0};
        tide_text_use(&heap, NULL);
        tide_list held[3];
        for (uint32_t i = 0; i < 3u; i++) {
            held[i] = plan_packed(i); tide_skinned_own(&held[i], TIDE_IN_MATCH);
        }
        tide_list stored = {0};
        TIDE_REQUIRE(tide_skinned_rest(held[2], &stored, TIDE_IN_MATCH));
        tide_heap_copy(&snapshot, &heap);
        const uint64_t snapshot_hash = tide_heap_hash(0, &snapshot);
        const float seconds[] = {-1.0f, -0.0f, 0.0f, 0.125f, 0.25f, 0.375f, 0.73f, 1.99f, 3.0f,
                                  tide_f_from_bits(0x7fc00000u), tide_f_from_bits(0x7f800000u), tide_f_from_bits(0xff800000u)};
        for (uint32_t s = 0; s < 2u; s++) for (uint32_t t = 0; t < sizeof seconds / sizeof seconds[0]; t++) {
            const uint32_t sample_mark = tide_scratch_mark();
            tide_list alone = {0}, kept = {0}, asset = {0};
            TIDE_REQUIRE(tide_skinned_sample(raw[s], 0, seconds[t], &alone, TIDE_IN_SCRATCH));
            TIDE_REQUIRE(tide_skinned_sample(plan_packed(s), 0, seconds[t], &kept, TIDE_IN_SCRATCH));
            TIDE_REQUIRE(tide_skinned_sample(held[s], 0, seconds[t], &asset, TIDE_IN_SCRATCH));
            TIDE_CHECK(pose_hash(kept) == pose_hash(alone) && pose_hash(asset) == pose_hash(alone));
            alone = kept = asset = (tide_list){0};
            TIDE_REQUIRE(tide_skinned_sample_into(raw[2], raw[s], 0, seconds[t], &alone, TIDE_IN_SCRATCH));
            TIDE_REQUIRE(tide_skinned_sample_into(plan_packed(2), plan_packed(s), 0, seconds[t], &kept, TIDE_IN_SCRATCH));
            TIDE_REQUIRE(tide_skinned_sample_into(held[2], held[s], 0, seconds[t], &asset, TIDE_IN_SCRATCH));
            TIDE_REQUIRE(tide_skinned_sample_into(held[2], held[s], 0, seconds[t], &stored, TIDE_IN_MATCH));
            TIDE_CHECK(pose_hash(kept) == pose_hash(alone) && pose_hash(asset) == pose_hash(alone)
                       && pose_hash(stored) == pose_hash(alone) && tide_heap_hash(0, &snapshot) == snapshot_hash);
            tide_scratch_reset(sample_mark);
        }
        tide_text_use(NULL, NULL); tide_heap_free(&heap); tide_heap_free(&snapshot);
        tide_pack_unload(); free(pack); tide_scratch_reset(variant_mark);
    }
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_sampling_large_temporary_arrays_do_not_accumulate_in_scratch)
{
    const uint32_t mark = tide_scratch_mark();
    tide_list raw = model();
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(tide_list_at(raw, 0, 1u), (size_t)tide_list_count(raw), &view));
    uint32_t at = view.size;
    tide_list_resize(&raw, (int32_t)(at + 20000u), 1u, TIDE_IN_SCRATCH);
    uint8_t *bytes = tide_list_at_mut(raw, 0, 1u);
    const uint32_t clips = table(bytes, &at, 48u, 1u, TIDE_SKINNED_CLIP_SIZE);
    const uint32_t channels = table(bytes, &at, 56u, 300u, TIDE_SKINNED_CHANNEL_SIZE);
    const uint32_t keys = at;
    word(bytes, clips, view.string_at + 10u); word(bytes, clips + 4u, 6u);
    word(bytes, clips + 8u, 0u); word(bytes, clips + 12u, 300u); number(bytes, clips + 16u, 5.0f);
    for (uint32_t c = 0; c < 300u; c++) {
        const uint32_t record = channels + c * TIDE_SKINNED_CHANNEL_SIZE;
        const uint32_t path = c % 3u, width = path == TIDE_SKINNED_ROTATION ? 4u : 3u;
        word(bytes, record, c % 3u); word(bytes, record + 4u, path);
        word(bytes, record + 8u, TIDE_SKINNED_LINEAR); word(bytes, record + 12u, 2u);
        word(bytes, record + 16u, at); number(bytes, at, 0.0f); number(bytes, at + 4u, 2.0f + (float)c * 0.01f); at += 8u;
        word(bytes, record + 20u, at);
        for (uint32_t k = 0; k < 2u; k++) for (uint32_t n = 0; n < width; n++) {
            number(bytes, at, n == 3u ? 1.0f : (float)(c + k + n + 1u) * 0.03125f); at += 4u;
        }
    }
    word(bytes, 80u, keys); word(bytes, 84u, at - keys); word(bytes, 8u, at);
    tide_list_resize(&raw, (int32_t)at, 1u, TIDE_IN_SCRATCH);
    TIDE_REQUIRE(tide_skinned_valid(raw));
    size_t size; uint8_t *pack = plan_pack(&raw, 1u, &size);
    TIDE_REQUIRE(pack && tide_pack_load(pack, size));
    const tide_list packed = plan_packed(0u);
    tide_list alone = {0}, kept = {0};
    TIDE_REQUIRE(tide_skinned_sample(raw, 0, 0.0f, &alone, TIDE_IN_SCRATCH));
    TIDE_REQUIRE(tide_skinned_sample(packed, 0, 0.0f, &kept, TIDE_IN_SCRATCH));
    const uint32_t retained = tide_scratch_mark();
    for (uint32_t i = 0; i < 40u; i++) {
        const float seconds = (float)i * 0.125f;
        TIDE_REQUIRE(tide_skinned_sample(raw, 0, seconds, &alone, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_sample(packed, 0, seconds, &kept, TIDE_IN_SCRATCH));
        TIDE_CHECK(tide_scratch_mark() == retained && pose_hash(kept) == pose_hash(alone));
    }
    tide_pack_unload(); free(pack); tide_scratch_reset(mark);
}

// Pack-held and asset-held bytes keep their sample plans; owned scratch bytes
// plan for each call. Both give the same bits and refusals.
TIDE_TEST(skinned_kept_sample_plans_match_plans_made_for_one_call)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw[3] = {model(), separate_model(separate_nodes), armature_model()};
    size_t size;
    uint8_t *pack = plan_pack(raw, 3u, &size);
    TIDE_REQUIRE(pack && tide_pack_load(pack, size));
    tide_heap heap = {0};
    tide_text_use(&heap, NULL);
    tide_list packed[3], held[3] = {{0}};
    for (uint32_t i = 0; i < 3u; i++) {
        packed[i] = plan_packed(i);
        TIDE_REQUIRE(tide_list_count(packed[i]) == tide_list_count(raw[i]));
        tide_list_set(&held[i], packed[i], 1u, TIDE_IN_MATCH);
    }
    // The exact rig, separate bindings both ways, folding an armature into
    // the model and the model's out of it, rejected bindings and clips.
    static const struct { uint32_t target, source; int32_t clip; } pairs[] = {
        {0, 0, 0}, {0, 0, 1}, {0, 0, 2}, {0, 0, 3}, {0, 1, 0}, {0, 1, 1}, {1, 0, 0}, {0, 2, 0},
        {0, 2, 1}, {2, 0, 0}, {2, 0, 2}, {1, 2, 0},
    };
    tide_list stored = {0};
    for (uint32_t pass = 0; pass < 2u; pass++) {
        for (uint32_t p = 0; p < sizeof pairs / sizeof pairs[0]; p++) {
            const uint32_t t = pairs[p].target, s = pairs[p].source;
            const int32_t clip = pairs[p].clip;
            for (int step = -2; step <= 10; step++) {
                const float seconds = 0.25f * (float)step + (pass ? 0.125f : 0.0f);
                const uint32_t at = tide_scratch_mark();
                tide_list alone = {0}, kept = {0}, asset = {0};
                const bool bound = tide_skinned_sample_into(raw[t], raw[s], clip, seconds, &alone, TIDE_IN_SCRATCH);
                TIDE_CHECK(tide_skinned_sample_into(packed[t], packed[s], clip, seconds, &kept, TIDE_IN_SCRATCH) == bound);
                TIDE_CHECK(tide_skinned_sample_into(held[t], held[s], clip, seconds, &asset, TIDE_IN_SCRATCH) == bound);
                TIDE_CHECK(pose_hash(kept) == pose_hash(alone) && pose_hash(asset) == pose_hash(alone));
                if (bound) {
                    TIDE_CHECK(tide_skinned_sample_into(packed[t], packed[s], clip, seconds, &stored, TIDE_IN_MATCH));
                    TIDE_CHECK(pose_hash(stored) == pose_hash(alone));
                }
                // The clip on its own skeleton
                alone = kept = asset = (tide_list){0};
                const bool own = tide_skinned_sample(raw[s], clip, seconds, &alone, TIDE_IN_SCRATCH);
                TIDE_CHECK(tide_skinned_sample(packed[s], clip, seconds, &kept, TIDE_IN_SCRATCH) == own);
                TIDE_CHECK(tide_skinned_sample(held[s], clip, seconds, &asset, TIDE_IN_SCRATCH) == own);
                TIDE_CHECK(pose_hash(kept) == pose_hash(alone) && pose_hash(asset) == pose_hash(alone));
                tide_scratch_reset(at);
            }
        }
    }
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_pack_unload();
    free(pack);
    tide_scratch_reset(mark);
}

// A plan is kept for the pack it was made from: another load at the same
// offsets, with other bytes there, plans again.
TIDE_TEST(skinned_kept_sample_plans_follow_reloaded_packs)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list first[2] = {model(), separate_model(separate_nodes)};
    tide_list changed = model();
    tide_skinned_data_view view;
    tide_skinned_channel_data channel;
    uint8_t *bytes = tide_list_at_mut(changed, 0, 1u);
    TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(changed), &view));
    TIDE_REQUIRE(tide_skinned_data_channel(&view, 0u, &channel));
    number(bytes, channel.values_at + 12u, 7.0f); // The arm's second translation key
    number(bytes, view.node_at + 2u * TIDE_SKINNED_NODE_SIZE + 20u, 5.0f); // The hand's rest
    const tide_list second[2] = {changed, first[1]};
    const tide_list *loads[2] = {first, second};
    uint32_t handles[2] = {0};
    for (uint32_t load = 0; load < 2u; load++) {
        size_t size;
        uint8_t *pack = plan_pack(loads[load], 2u, &size);
        TIDE_REQUIRE(pack && tide_pack_load(pack, size));
        const tide_list actor = plan_packed(0u), clip = plan_packed(1u);
        handles[load] = actor.at;
        for (int step = 0; step <= 8; step++) {
            const float seconds = 0.25f * (float)step;
            tide_list kept = {0}, alone = {0};
            TIDE_REQUIRE(tide_skinned_sample(actor, 0, seconds, &kept, TIDE_IN_SCRATCH));
            TIDE_REQUIRE(tide_skinned_sample(loads[load][0], 0, seconds, &alone, TIDE_IN_SCRATCH));
            TIDE_CHECK(pose_hash(kept) == pose_hash(alone));
            kept = alone = (tide_list){0};
            TIDE_REQUIRE(tide_skinned_sample_into(actor, clip, 0, seconds, &kept, TIDE_IN_SCRATCH));
            TIDE_REQUIRE(tide_skinned_sample_into(loads[load][0], loads[load][1], 0, seconds, &alone, TIDE_IN_SCRATCH));
            TIDE_CHECK(pose_hash(kept) == pose_hash(alone));
        }
        tide_pack_unload();
        free(pack);
    }
    TIDE_CHECK(handles[0] == handles[1]); // Otherwise the second load proves nothing
    tide_scratch_reset(mark);
}

TIDE_TEST(skinned_known_model_compatibility_survives_cache_collisions)
{
    const uint32_t mark = tide_scratch_mark();
    // More identities than cache slots guarantees collisions without depending
    // on a particular offset/hash bucket formula.
    enum { COUNT = 33 };
    tide_list raw[COUNT], packed[COUNT], held[COUNT];
    memset(held, 0, sizeof held);
    for (uint32_t i = 0; i < COUNT; i++) {
        raw[i] = model();
        word(tide_list_at_mut(raw[i], 0, 1u), 88u, i + 1u);
    }
    size_t size;
    uint8_t *pack = plan_pack(raw, COUNT, &size);
    TIDE_REQUIRE(pack && tide_pack_load(pack, size));
    tide_heap heap = {0};
    tide_text_use(&heap, NULL);
    for (uint32_t i = 0; i < COUNT; i++) {
        packed[i] = plan_packed(i);
        tide_list_set(&held[i], packed[i], 1u, TIDE_IN_MATCH);
    }
    for (uint32_t i = 0; i < COUNT; i++) for (uint32_t j = 0; j < COUNT; j++) {
        TIDE_CHECK(tide_skinned_compatible(packed[i], packed[j]) == (i == j));
        TIDE_CHECK(tide_skinned_compatible(held[i], held[j]) == (i == j));
        TIDE_CHECK(tide_skinned_compatible(packed[i], held[j]) == (i == j));
        TIDE_CHECK(tide_skinned_compatible(held[i], packed[j]) == (i == j));
        TIDE_CHECK(tide_skinned_compatible(packed[i], raw[j]) == (i == j));
    }
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap);
    tide_pack_unload();
    free(pack);
    tide_scratch_reset(mark);
}

// More plans than are kept: the oldest go, and what's sampled stays right.
TIDE_TEST(skinned_kept_sample_plans_survive_eviction)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list raw[2] = {model(), separate_model(separate_nodes)};
    size_t size;
    uint8_t *pack = plan_pack(raw, 2u, &size);
    TIDE_REQUIRE(pack && tide_pack_load(pack, size));
    const tide_list actor = plan_packed(0u), clip = plan_packed(1u);
    for (int32_t round = 0; round < 1200; round++) {
        const uint32_t at = tide_scratch_mark();
        tide_list pose = {0};
        // Clips past the model's few are refused, and their refusals kept too.
        TIDE_CHECK(!tide_skinned_sample(actor, 3 + round, 0.5f, &pose, TIDE_IN_SCRATCH));
        TIDE_CHECK(!tide_skinned_sample_into(actor, clip, 2 + round, 0.5f, &pose, TIDE_IN_SCRATCH));
        if (round % 97 == 0) {
            const float seconds = 0.01f * (float)(round % 200);
            tide_list kept = {0}, alone = {0};
            TIDE_CHECK(tide_skinned_sample_into(actor, clip, round % 2, seconds, &kept, TIDE_IN_SCRATCH));
            TIDE_CHECK(tide_skinned_sample_into(raw[0], raw[1], round % 2, seconds, &alone, TIDE_IN_SCRATCH));
            TIDE_CHECK(pose_hash(kept) == pose_hash(alone));
            kept = alone = (tide_list){0};
            TIDE_CHECK(tide_skinned_sample(actor, round % 3, seconds, &kept, TIDE_IN_SCRATCH));
            TIDE_CHECK(tide_skinned_sample(raw[0], round % 3, seconds, &alone, TIDE_IN_SCRATCH));
            TIDE_CHECK(pose_hash(kept) == pose_hash(alone));
        }
        tide_scratch_reset(at);
    }
    tide_pack_unload();
    free(pack);
    tide_scratch_reset(mark);
}

// Every node's model matrix as the old Model made them: all nodes, root down.
static bool all_models_match(const tide_list data, const tide_list pose)
{
    tide_float4x4 global[8];
    const int32_t nodes = tide_skinned_bone_count(data);
    if (nodes <= 0 || nodes > 8 || tide_list_count(pose) != nodes) return false;
    tide_skinned_data_view view;
    const tide_list flat = tide_list_from(tide_list_at(data, 0, 1u), tide_list_count(data), 1u);
    if (!tide_skinned_data_open(tide_list_at(flat, 0, 1u), (size_t)tide_list_count(flat), &view)) return false;
    for (int32_t i = 0; i < nodes; i++) {
        tide_skinned_node_data n;
        if (!tide_skinned_data_node(&view, (uint32_t)i, &n)) return false;
        const tide_bone_transform local = bone(pose, i);
        const tide_float4x4 matrix = tide_trs_f4x4(local.position, local.rotation, local.scale);
        global[i] = n.parent < 0 ? matrix : tide_mul_f4x4(global[n.parent], matrix);
        const tide_float4x4 made = tide_skinned_model(data, pose, i);
        if (memcmp(&made, &global[i], sizeof made) != 0) return false;
    }
    return true;
}

// What a model's header and names say is kept for pack- and asset-held bytes:
// the same answers as owned bytes, and other answers once another pack's
// bytes are at the same offsets.
TIDE_TEST(skinned_known_models_answer_as_their_bytes_do)
{
    const uint32_t mark = tide_scratch_mark();
    const tide_list plain = model();
    tide_list renamed = model();
    uint8_t *bytes = tide_list_at_mut(renamed, 0, 1u);
    tide_skinned_data_view view;
    TIDE_REQUIRE(tide_skinned_data_open(bytes, (size_t)tide_list_count(renamed), &view));
    memcpy(bytes + view.string_at + 5u, "palm", 4u); // "hand"
    bytes[88] ^= 1u; // Another skeleton
    number(bytes, view.node_at + TIDE_SKINNED_NODE_SIZE + 16u, 5.0f); // The arm's rest
    const tide_list *loads[2] = {&plain, &renamed};
    const tide_str hand = tide_str_from_cstr("hand"), palm = tide_str_from_cstr("palm"), cubic = tide_str_from_cstr("cubic");
    uint32_t handles[2] = {0};
    for (uint32_t load = 0; load < 2u; load++) {
        const tide_list raw = *loads[load];
        size_t size;
        uint8_t *pack = sample_model_pack(raw, false, &size);
        TIDE_REQUIRE(pack && tide_pack_load(pack, size));
        tide_heap heap = {0};
        tide_text_use(&heap, NULL);
        const tide_list packed = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
        tide_list held = {0};
        tide_list_set(&held, packed, 1u, TIDE_IN_MATCH);
        handles[load] = packed.at;
        const tide_list sources[] = {packed, held};
        for (uint32_t twice = 0; twice < 2u; twice++) {
            for (uint32_t s = 0; s < 2u; s++) {
                const tide_list data = sources[s];
                TIDE_CHECK(tide_skinned_fingerprint(data) == tide_skinned_fingerprint(raw));
                TIDE_CHECK(tide_skinned_bone_count(data) == 3);
                TIDE_CHECK(tide_skinned_find_bone(data, hand) == (load ? -1 : 2));
                TIDE_CHECK(tide_skinned_find_bone(data, palm) == (load ? 2 : -1));
                TIDE_CHECK(tide_skinned_find_animation(data, cubic) == 2);
                TIDE_CHECK(tide_skinned_compatible(data, raw) && tide_skinned_compatible(data, plain) == !load);
                tide_list pose = {0};
                TIDE_REQUIRE(tide_skinned_sample(data, 2, 1.0f, &pose, TIDE_IN_SCRATCH));
                TIDE_CHECK(all_models_match(data, pose) && all_models_match(raw, pose));
                const tide_float4x4 none = tide_skinned_model(data, pose, 3), identity = tide_identity_f4x4();
                TIDE_CHECK(memcmp(&none, &identity, sizeof none) == 0);
            }
        }
        tide_text_use(NULL, NULL);
        tide_heap_free(&heap);
        tide_pack_unload();
        free(pack);
    }
    TIDE_CHECK(handles[0] == handles[1]); // Otherwise the second load proves nothing
    tide_scratch_reset(mark);
}
