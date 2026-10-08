#include <stdio.h>
#include <string.h>

#include "tide/net.h"
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
