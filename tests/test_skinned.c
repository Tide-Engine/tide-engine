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

// A separate animation export: an unrelated mesh node precedes the arm, the
// hand precedes its parent in glTF, and there is no geometry or skin. Node
// indices and counts differ from model(), while the arm/hand rest rig agrees.
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

// Typed geometry is deliberately simple: sampling consumes only the portable
// bytes. The alias variant embeds those bytes in writable packed geometry to
// exercise proof invalidation after a generated-default-style update.
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

TIDE_TEST(skinned_packed_samples_match_owned_bits_and_reject_negative_first_keys)
{
    const uint32_t mark = tide_scratch_mark();
    for (uint32_t variant = 0; variant < 2u; variant++) {
        const tide_list raw = model();
        uint8_t *flat = tide_list_at_mut(raw, 0, 1u);
        const uint32_t bytes = (uint32_t)tide_list_count(raw);
        tide_skinned_data_view view;
        tide_skinned_channel_data channel;
        TIDE_REQUIRE(tide_skinned_data_open(flat, bytes, &view));
        TIDE_REQUIRE(tide_skinned_data_channel(&view, 0u, &channel));
        if (variant) number(flat, channel.times_at, -0.5f);
        // Full validation permits this strictly increasing negative first
        // time, while sampling has always required every key time >=0.
        TIDE_REQUIRE(tide_skinned_valid(raw));
        size_t size;
        uint8_t *pack = sample_model_pack(raw, false, &size);
        TIDE_REQUIRE(pack && tide_pack_load(pack, size));
        const tide_list packed = tide_pack_skinned_of(tide_pack_find("Game.actor", TIDE_PACK_SKINNED), sizeof(tide_float3)).data;
        uint64_t version;
        TIDE_REQUIRE(tide_pack_skinned_version(packed, &version));
        tide_heap heap = {0};
        tide_text_use(&heap, NULL);
        tide_list owned = {0}, a = {0}, b = {0};
        tide_list_set(&owned, raw, 1u, TIDE_IN_MATCH);
        TIDE_CHECK(!tide_pack_skinned_version(owned, &version));
        TIDE_REQUIRE(tide_skinned_rest(raw, &a, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_rest(raw, &b, TIDE_IN_SCRATCH));
        const float times[] = {-1.0f, 0.0f, 0.375f, 1.0f, 2.0f, 3.0f};
        for (int32_t clip = 0; clip < 3; clip++) {
            for (uint32_t i = 0; i < sizeof times / sizeof times[0]; i++) {
                const uint64_t before_a = pose_hash(a), before_b = pose_hash(b);
                const bool expected = variant == 0u || clip != 0;
                TIDE_CHECK(tide_skinned_sample(packed, clip, times[i], &a, TIDE_IN_SCRATCH) == expected);
                TIDE_CHECK(tide_skinned_sample(owned, clip, times[i], &b, TIDE_IN_SCRATCH) == expected);
                if (!expected) {
                    TIDE_CHECK(pose_hash(a) == before_a && pose_hash(b) == before_b);
                    TIDE_CHECK(!tide_skinned_sample(raw, clip, times[i], &b, TIDE_IN_SCRATCH));
                }
                TIDE_CHECK(tide_list_count(a) == tide_list_count(b));
                for (int32_t node = 0; node < tide_list_count(a); node++) {
                    const tide_bone_transform first = bone(a, node), second = bone(b, node);
                    TIDE_CHECK(memcmp(&first, &second, sizeof first) == 0);
                }
            }
        }
        const uint64_t unchanged = pose_hash(a);
        TIDE_CHECK(!tide_skinned_sample(packed, -1, 0, &a, TIDE_IN_SCRATCH));
        TIDE_CHECK(!tide_skinned_sample(packed, 3, 0, &a, TIDE_IN_SCRATCH));
        TIDE_CHECK(pose_hash(a) == unchanged);
        tide_text_use(NULL, NULL);
        tide_heap_free(&heap);
        tide_pack_unload();
        free(pack);
    }
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
    // Leave the first key valid; the fallback must inspect the later key.
    number(to + sizeof(tide_block), channel.times_at + 4u, 0.0f);
    tide_pack_mesh_updated(to, carrier_size);
    TIDE_CHECK(!tide_pack_skinned_version(packed, &version));
    TIDE_CHECK(!tide_skinned_sample(packed, 0, 1.0f, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(pose_hash(pose) == before);
    tide_pack_unload();
    free(pack);
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

    // The reverse binding keeps the animation export's unrelated node at rest.
    TIDE_REQUIRE(tide_skinned_sample_into(source, target, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_list_count(pose) == 4 && bone(pose, 1).position.x == -9.0f && bone(pose, 2).position.x == 3.0f);
    TIDE_CHECK(tide_abs_f(bone(pose, 3).rotation.value.z - 0.70710677f) < 0.000001f);

    // A different rest quaternion sign is the same orientation, without
    // changing the numeric sign of values copied from the animation export.
    const tide_list sign = separate_model(
        "{\"name\":\"mesh\"},{\"name\":\"hand\",\"translation\":[0,3,0]},"
        "{\"name\":\"arm\",\"translation\":[-2,0,0],\"rotation\":[0,0,0,-1],\"children\":[1]}");
    TIDE_REQUIRE(tide_skinned_sample_into(target, sign, 0, 1, &pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(bone(pose, 1).rotation.value.w == 1.0f);
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
        "{\"name\":\"arm\",\"translation\":[-5,0,0],\"children\":[1]}",
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
    TIDE_CHECK(!tide_skinned_pose_into(target, source, source_pose, &pose, TIDE_IN_SCRATCH));
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
