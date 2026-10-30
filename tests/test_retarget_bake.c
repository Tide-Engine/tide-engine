#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "tide/retarget_bake.h"
#include "tide/sha256.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

enum { NODES = 17, HIPS = 1, SPINE = 2, ARM = 4, STEP_ARM = 7, CAPE = 16 };

typedef struct bake_fixture {
    tide_list source, target, source_pose, target_pose, plan;
    tide_retarget_bake_settings settings;
} bake_fixture;

static void word(uint8_t *p, uint32_t value)
{
    for (uint32_t i = 0; i < 4u; i++) p[i] = (uint8_t)(value >> (i * 8u));
}

static void number(uint8_t *p, float value) { word(p, tide_f_bits(value)); }

static uint32_t read_word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const char *const names[NODES] = {"", "Hips", "Spine", "Head", "LeftUpperArm", "LeftLowerArm", "LeftHand",
    "RightUpperArm", "RightLowerArm", "RightHand", "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "RightUpperLeg",
    "RightLowerLeg", "RightFoot", "cape_extra"};
static const int parents[NODES] = {-1, 0, 1, 2, 2, 4, 5, 2, 7, 8, 1, 10, 11, 1, 13, 14, 2};
static const tide_float3 places[NODES] = {{0,0,0}, {0,1,0}, {0,0.5f,0}, {0,0.4f,0},
    {-0.3f,0,0}, {-0.4f,0,0}, {-0.3f,0,0}, {0.3f,0,0}, {0.4f,0,0}, {0.3f,0,0},
    {-0.15f,0,0}, {0,-0.45f,0}, {0,-0.45f,0}, {0.15f,0,0}, {0,-0.45f,0}, {0,-0.45f,0}, {0,0,-0.1f}};

static tide_list make_model(float scale, bool animation)
{
    uint8_t bytes[8192] = {0};
    const uint32_t nodes_at = 120u;
    const uint32_t clips_at = nodes_at + NODES * 56u;
    const uint32_t channels_at = clips_at + (animation ? 40u : 0u);
    const uint32_t palette_at = channels_at + (animation ? 96u : 0u);
    const uint32_t vertex_at = palette_at + 68u;
    const uint32_t influence_at = vertex_at + 80u;
    const uint32_t strings_at = influence_at + 8u;
    uint32_t next = strings_at;
    word(bytes, TIDE_SKINNED_DATA_MAGIC); word(bytes + 4, TIDE_SKINNED_DATA_VERSION);
    word(bytes + 16, NODES); word(bytes + 20, nodes_at);
    for (uint32_t i = 0; i < NODES; i++) {
        uint8_t *p = bytes + nodes_at + i * 56u;
        word(p, (uint32_t)parents[i]); word(p + 4, i ? i - 1u : UINT32_MAX);
        word(p + 8, next); word(p + 12, (uint32_t)strlen(names[i]));
        memcpy(bytes + next, names[i], strlen(names[i]) + 1u); next += (uint32_t)strlen(names[i]) + 1u;
        number(p + 16, places[i].x * scale); number(p + 20, places[i].y * scale); number(p + 24, places[i].z * scale);
        number(p + 40, 1); number(p + 44, 1); number(p + 48, 1); number(p + 52, 1);
    }
    // Complete harmless geometry demonstrates that the baker strips it.
    word(bytes + 24, 1); word(bytes + 28, palette_at); word(bytes + palette_at, HIPS);
    for (uint32_t i = 0; i < 4; i++) number(bytes + palette_at + 4u + i * 20u, 1);
    word(bytes + 32, 1); word(bytes + 36, vertex_at); word(bytes + vertex_at + 76, 1);
    word(bytes + 64, 1); word(bytes + 68, influence_at); number(bytes + influence_at + 4, 1);
    if (animation) {
        word(bytes + 48, 2); word(bytes + 52, clips_at);
        word(bytes + 56, 4); word(bytes + 60, channels_at);
        word(bytes + clips_at, next); word(bytes + clips_at + 4, 10);
        memcpy(bytes + next, "Root|Walk!", 11); next += 11;
        word(bytes + clips_at + 12, 4); number(bytes + clips_at + 16, 1);
        word(bytes + clips_at + 20, next); word(bytes + clips_at + 24, 5);
        memcpy(bytes + next, "Still", 6); next += 6;
        word(bytes + clips_at + 28, 4); number(bytes + clips_at + 36, 0.5f);
    } else {
        word(bytes + 52, clips_at); word(bytes + 60, channels_at);
    }
    word(bytes + 72, strings_at); word(bytes + 76, next - strings_at);
    const uint32_t keys_at = (next + 3u) & ~3u;
    next = keys_at;
    if (animation) for (uint32_t channel = 0; channel < 4u; channel++) {
        uint8_t *p = bytes + channels_at + channel * 24u;
        const uint32_t count = channel == 3 ? 2u : 3u;
        const uint32_t width = channel == 0 ? 3u : 4u;
        const uint32_t copies = channel == 3 ? 3u : 1u;
        word(p, channel == 0 ? HIPS : channel == 1 ? ARM : channel == 2 ? STEP_ARM : SPINE);
        word(p + 4, channel == 0 ? TIDE_SKINNED_TRANSLATION : TIDE_SKINNED_ROTATION);
        word(p + 8, channel == 2 ? TIDE_SKINNED_STEP : channel == 3 ? TIDE_SKINNED_CUBICSPLINE : TIDE_SKINNED_LINEAR);
        word(p + 12, count); word(p + 16, next);
        for (uint32_t k = 0; k < count; k++) { number(bytes + next, k == 0 ? 0 : k == count - 1u ? 1 : 0.37f); next += 4; }
        word(p + 20, next);
        for (uint32_t k = 0; k < count; k++) {
            const float time = k == 0 ? 0 : k == count - 1u ? 1 : 0.37f;
            float values[4] = {time, 1, 0, 0};
            if (channel) {
                tide_quaternion q = tide_axisangle_q(tide_f3(0, 0, 1), channel == 1 ? time * 1.2f
                    : channel == 2 ? (k ? 0.5f : 0) : time * 0.2f);
                if (channel == 2 && k == 2) q.value = tide_neg_f4(q.value);
                values[0] = q.value.x; values[1] = q.value.y; values[2] = q.value.z; values[3] = q.value.w;
            }
            const uint32_t value_at = next + (copies == 3u ? width * 4u : 0u);
            for (uint32_t axis = 0; axis < width; axis++) number(bytes + value_at + axis * 4u, values[axis]);
            next += width * copies * 4u;
        }
    }
    word(bytes + 8, next); word(bytes + 44, next);
    word(bytes + 80, keys_at); word(bytes + 84, next - keys_at);
    tide_sha256_of(bytes + nodes_at, NODES * 56u, bytes + 88);
    return tide_list_from(bytes, (int32_t)next, 1u);
}

static bool fixture(bake_fixture *f, bool calibration)
{
    *f = (bake_fixture){0};
    f->source = make_model(1, true); f->target = make_model(2, false);
    if (!tide_skinned_rest(f->source, &f->source_pose, TIDE_IN_SCRATCH)
        || !tide_skinned_rest(f->target, &f->target_pose, TIDE_IN_SCRATCH)) return false;
    if (calibration) {
        tide_bone_transform *cape = tide_list_at_mut(f->target_pose, CAPE, sizeof *cape);
        cape->position.y = 0.25f; cape->scale = tide_f3_splat(1.1f);
    }
    tide_list sb = {0}, ss = {0}, tb = {0}, ts = {0};
    if (!tide_humanoid_auto(f->source, f->source_pose, &sb, &ss, TIDE_IN_SCRATCH)
        || !tide_humanoid_auto(f->target, f->target_pose, &tb, &ts, TIDE_IN_SCRATCH)) return false;
    float ratio;
    if (tide_retarget_create(f->source, f->source, f->source_pose, sb, ss,
        f->target, f->target_pose, tb, ts, &f->plan, &ratio, TIDE_IN_SCRATCH) != TIDE_RETARGET_OK) return false;
    f->settings = tide_retarget_bake_defaults(ratio);
    return true;
}

static tide_bone_transform get_transform(tide_list pose, int index)
{
    const tide_bone_transform *p = tide_list_at(pose, index, sizeof *p);
    return p ? *p : (tide_bone_transform){0};
}

static bool same_rotation(tide_quaternion a, tide_quaternion b)
{
    return tide_abs_f(tide_dot_q(a, b)) > 0.99999f;
}

TIDE_TEST(retarget_bake_preserves_target_identity_names_and_calibrated_constants)
{
    const uint32_t mark = tide_scratch_mark();
    bake_fixture f; TIDE_REQUIRE(fixture(&f, true));
    uint8_t *bytes = NULL; uint32_t size = 0; char why[256];
    const uint32_t before = tide_scratch_mark();
    TIDE_REQUIRE(tide_retarget_bake(f.source, -1, f.target, f.plan, &f.settings, &bytes, &size, why));
    TIDE_CHECK(tide_scratch_mark() == before && !why[0]);
    TIDE_REQUIRE(tide_skinned_data_validate(bytes, size));
    tide_skinned_data_view view; TIDE_REQUIRE(tide_skinned_data_open(bytes, size, &view));
    TIDE_CHECK(view.nodes == NODES && view.clips == 2 && !view.vertices && !view.indices && !view.palettes && !view.influences && !view.shapes);
    for (uint32_t i = 0; i < NODES; i++) {
        tide_skinned_node_data node; TIDE_REQUIRE(tide_skinned_data_node(&view, i, &node));
        TIDE_CHECK(node.parent == parents[i] && strcmp(node.name, names[i]) == 0);
        TIDE_CHECK(node.position[0] == places[i].x * 2 && node.position[1] == places[i].y * 2 && node.position[2] == places[i].z * 2);
    }
    tide_list baked = tide_list_from(bytes, (int32_t)size, 1);
    TIDE_CHECK(tide_skinned_compatible(f.target, baked));
    tide_skinned_clip_data walk, still;
    TIDE_REQUIRE(tide_skinned_data_clip(&view, 0, &walk) && tide_skinned_data_clip(&view, 1, &still));
    TIDE_CHECK(strcmp(walk.name, "Root|Walk!") == 0 && walk.length == 1);
    TIDE_CHECK(strcmp(still.name, "Still") == 0 && still.length == 0.5f);
    bool position = false, scale = false;
    for (uint32_t i = 0; i < still.channels; i++) {
        tide_skinned_channel_data c; TIDE_REQUIRE(tide_skinned_data_channel(&view, still.first_channel + i, &c));
        TIDE_CHECK(c.keys == 1);
        if (c.node == CAPE && c.path == TIDE_SKINNED_TRANSLATION) position = true;
        if (c.node == CAPE && c.path == TIDE_SKINNED_SCALE) scale = true;
    }
    TIDE_CHECK(position && scale);
    tide_list pose = {0}; TIDE_REQUIRE(tide_skinned_sample(baked, 0, 0.5f, &pose, TIDE_IN_SCRATCH));
    const tide_bone_transform hips = get_transform(pose, HIPS), cape = get_transform(pose, CAPE);
    TIDE_CHECK(tide_abs_f(hips.position.x - 1.0f) < 0.00001f && hips.position.y == 2);
    TIDE_CHECK(cape.position.y == 0.25f && cape.scale.x == 1.1f);
    uint8_t *again = NULL; uint32_t again_size = 0;
    TIDE_REQUIRE(tide_retarget_bake(f.source, -1, f.target, f.plan, &f.settings, &again, &again_size, why));
    TIDE_CHECK(size == again_size && memcmp(bytes, again, size) == 0);
    free(bytes); free(again); tide_scratch_reset(mark);
}

TIDE_TEST(retarget_bake_keeps_authored_keys_steps_and_quaternion_continuity)
{
    const uint32_t mark = tide_scratch_mark();
    bake_fixture f; TIDE_REQUIRE(fixture(&f, false));
    f.settings.sample_rate = 4;
    uint8_t *bytes = NULL; uint32_t size = 0; char why[256];
    TIDE_REQUIRE(tide_retarget_bake(f.source, 0, f.target, f.plan, &f.settings, &bytes, &size, why));
    tide_skinned_data_view view; TIDE_REQUIRE(tide_skinned_data_open(bytes, size, &view));
    uint32_t previous_bits = tide_f_bits(0.37f) - 1u; float before; memcpy(&before, &previous_bits, sizeof before);
    bool authored = false, predecessor = false, midpoint = false, endpoint = false;
    for (uint32_t i = 0; i < view.channels; i++) {
        tide_skinned_channel_data c; TIDE_REQUIRE(tide_skinned_data_channel(&view, i, &c));
        tide_quaternion previous = tide_identity_q();
        for (uint32_t k = 0; k < c.keys; k++) {
            float time, value[4], in[4], out[4]; TIDE_REQUIRE(tide_skinned_data_key(&view, &c, k, &time, value, in, out));
            authored |= time == 0.37f; predecessor |= time == before; midpoint |= time == 0.5f; endpoint |= time == 1;
            if (c.path == TIDE_SKINNED_ROTATION) {
                tide_quaternion q = tide_q(value[0], value[1], value[2], value[3]);
                if (k) TIDE_CHECK(tide_dot_q(previous, q) >= 0);
                previous = q;
            }
        }
    }
    TIDE_CHECK(authored && predecessor && midpoint && endpoint);
    tide_list baked = tide_list_from(bytes, (int32_t)size, 1), pose = {0}, source = {0}, expected = {0};
    const float times[] = {0, 0.25f, before, 0.37f, 0.5f, 0.75f, 1};
    for (uint32_t i = 0; i < sizeof times / sizeof *times; i++) {
        TIDE_REQUIRE(tide_skinned_sample(baked, 0, times[i], &pose, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_skinned_sample(f.source, 0, times[i], &source, TIDE_IN_SCRATCH));
        TIDE_REQUIRE(tide_retarget_apply(f.plan, f.source, source, f.target, f.settings.root_scale,
            f.settings.root_offset, 0, 1, 1, &expected, TIDE_IN_SCRATCH));
        for (int node = 0; node < NODES; node++) {
            tide_bone_transform a = get_transform(pose, node), b = get_transform(expected, node);
            TIDE_CHECK(tide_length_f3(tide_sub_f3(a.position, b.position)) < 0.00001f && same_rotation(a.rotation, b.rotation));
        }
        // Independent local expectation: the stepped upper arm turns
        // 0.5 radians at the authored timestamp, never one sample early.
        const float angle = (times[i] >= 0.37f ? 0.5f : 0.0f);
        // Spine's cubic motion composes before the arm, so subtract that
        // parent's model rotation by testing the local arm transform.
        TIDE_CHECK(same_rotation(get_transform(pose, STEP_ARM).rotation, tide_axisangle_q(tide_f3(0,0,1), angle)));
    }
    free(bytes); tide_scratch_reset(mark);
}

TIDE_TEST(retarget_bake_selected_constant_and_zero_duration_clips)
{
    const uint32_t mark = tide_scratch_mark();
    bake_fixture f; TIDE_REQUIRE(fixture(&f, false));
    uint8_t *bytes = NULL; uint32_t size = 0; char why[256];
    f.settings.sample_rate = 0;
    TIDE_REQUIRE(tide_retarget_bake(f.source, 1, f.target, f.plan, &f.settings, &bytes, &size, why));
    tide_skinned_data_view view; TIDE_REQUIRE(tide_skinned_data_open(bytes, size, &view));
    tide_skinned_clip_data clip; TIDE_REQUIRE(tide_skinned_data_clip(&view, 0, &clip));
    TIDE_CHECK(view.clips == 1 && !view.channels && strcmp(clip.name, "Still") == 0 && clip.length == 0.5f);
    free(bytes);
    const uint32_t at = 120u + NODES * 56u + 36u;
    for (uint32_t i = 0; i < 4; i++) *(uint8_t *)tide_list_at_mut(f.source, (int32_t)(at + i), 1) = 0;
    TIDE_REQUIRE(tide_retarget_bake(f.source, 1, f.target, f.plan, &f.settings, &bytes, &size, why));
    TIDE_REQUIRE(tide_skinned_data_open(bytes, size, &view) && tide_skinned_data_clip(&view, 0, &clip));
    TIDE_CHECK(clip.length == 0 && !view.channels);
    free(bytes); tide_scratch_reset(mark);
}

TIDE_TEST(retarget_bake_skeleton_strips_payload_and_owns_its_bytes)
{
    const uint32_t mark = tide_scratch_mark();
    tide_list source = make_model(1, true);
    uint8_t *bytes = NULL; uint32_t size = 0; char why[256];
    TIDE_REQUIRE(tide_retarget_bake_skeleton(source, &bytes, &size, why));
    tide_list stripped = tide_list_from(bytes, (int32_t)size, 1);
    TIDE_CHECK(tide_skinned_compatible(source, stripped));
    tide_scratch_reset(mark);
    tide_skinned_data_view view; TIDE_REQUIRE(tide_skinned_data_open(bytes, size, &view));
    TIDE_CHECK(tide_skinned_data_validate(bytes, size) && view.nodes == NODES && !view.clips && !view.channels && !view.vertices);
    tide_skinned_node_data node; TIDE_REQUIRE(tide_skinned_data_node(&view, ARM, &node));
    TIDE_CHECK(strcmp(node.name, "LeftUpperArm") == 0);
    free(bytes);
}

TIDE_TEST(retarget_bake_refusals_leave_outputs_and_scratch_unchanged)
{
    const uint32_t mark = tide_scratch_mark();
    bake_fixture f; TIDE_REQUIRE(fixture(&f, false));
    uint8_t sentinel = 19, *bytes = &sentinel; uint32_t size = 123; char why[256];
    const uint32_t before = tide_scratch_mark();
    const int bad_indices[] = {-2, 2, INT32_MAX};
    for (uint32_t i = 0; i < sizeof bad_indices / sizeof *bad_indices; i++) {
        TIDE_CHECK(!tide_retarget_bake(f.source, bad_indices[i], f.target, f.plan, &f.settings, &bytes, &size, why));
        TIDE_CHECK(bytes == &sentinel && size == 123 && why[0] && tide_scratch_mark() == before);
    }
    f.settings.sample_rate = FLT_MAX;
    TIDE_CHECK(!tide_retarget_bake(f.source, 0, f.target, f.plan, &f.settings, &bytes, &size, why));
    TIDE_CHECK(strstr(why, "TIDE_RETARGET_BAKE_MAX_SAMPLES") != NULL);
    f.settings.sample_rate = -1;
    TIDE_CHECK(!tide_retarget_bake(f.source, 0, f.target, f.plan, &f.settings, &bytes, &size, why));
    f.settings = tide_retarget_bake_defaults(2); f.settings.root_offset.x = tide_f_from_bits(0x7fc00000u);
    TIDE_CHECK(!tide_retarget_bake(f.source, 0, f.target, f.plan, &f.settings, &bytes, &size, why));
    f.settings = tide_retarget_bake_defaults(2);
    TIDE_CHECK(!tide_retarget_bake(f.source, 0, f.target, (tide_list){0}, &f.settings, &bytes, &size, why));
    TIDE_CHECK(bytes == &sentinel && size == 123 && tide_scratch_mark() == before);
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_bake_rejects_cut_models_invalid_keys_and_animated_scale)
{
    const uint32_t mark = tide_scratch_mark();
    bake_fixture f; TIDE_REQUIRE(fixture(&f, false));
    bool copied = false;
    uint8_t *flat = tide_list_flatten(f.source, 1, false, &copied);
    const uint32_t full = (uint32_t)tide_list_count(f.source);
    uint8_t *saved = malloc(full); memcpy(saved, flat, full);
    tide_list_unflatten(f.source, flat, 1, false, copied);
    uint8_t sentinel = 17, *bytes = &sentinel; uint32_t size = 99; char why[256];
    for (uint32_t cut = 0; cut < full; cut++) {
        const uint32_t cut_mark = tide_scratch_mark();
        tide_list partial = tide_list_from(saved, (int32_t)cut, 1);
        const uint32_t before = tide_scratch_mark();
        TIDE_CHECK(!tide_retarget_bake(partial, 0, f.target, f.plan, &f.settings, &bytes, &size, why));
        TIDE_CHECK(bytes == &sentinel && size == 99 && tide_scratch_mark() == before);
        tide_scratch_reset(cut_mark);
    }
    const uint32_t channels = read_word(saved + 60), keys = read_word(saved + channels + 16);
    const uint32_t bad_places[] = {20, 72, channels, channels + 12, channels + 16, channels + 20, keys + 4};
    for (uint32_t i = 0; i < sizeof bad_places / sizeof *bad_places; i++) {
        const uint32_t saved_word = read_word(saved + bad_places[i]); word(saved + bad_places[i], UINT32_MAX);
        const uint32_t before = tide_scratch_mark();
        tide_list bad = tide_list_from(saved, (int32_t)full, 1);
        TIDE_CHECK(!tide_retarget_bake(bad, 0, f.target, f.plan, &f.settings, &bytes, &size, why));
        TIDE_CHECK(bytes == &sentinel && size == 99);
        tide_scratch_reset(before); word(saved + bad_places[i], saved_word);
    }
    word(saved + channels + 4, TIDE_SKINNED_SCALE);
    tide_list scale = tide_list_from(saved, (int32_t)full, 1);
    TIDE_CHECK(!tide_retarget_bake(scale, 0, f.target, f.plan, &f.settings, &bytes, &size, why));
    TIDE_CHECK(strstr(why, "animates scale") != NULL && bytes == &sentinel && size == 99);
    const uint32_t scale_values = read_word(saved + channels + 20);
    for (uint32_t i = 0; i < 9; i++) number(saved + scale_values + i * 4u, 1);
    scale = tide_list_from(saved, (int32_t)full, 1);
    bytes = NULL;
    TIDE_REQUIRE(tide_retarget_bake(scale, 0, f.target, f.plan, &f.settings, &bytes, &size, why));
    free(bytes);
    number(saved + scale_values + 16u, 1.1f);
    scale = tide_list_from(saved, (int32_t)full, 1);
    bytes = &sentinel; size = 99;
    TIDE_CHECK(!tide_retarget_bake(scale, 0, f.target, f.plan, &f.settings, &bytes, &size, why));
    TIDE_CHECK(strstr(why, "animates scale") != NULL && bytes == &sentinel && size == 99);
    free(saved); tide_scratch_reset(mark);
}

TIDE_TEST(retarget_bake_borrows_owned_inputs_and_keeps_snapshot_bytes)
{
    const uint32_t mark = tide_scratch_mark();
    bake_fixture f; TIDE_REQUIRE(fixture(&f, true));
    tide_heap heap = {0}, snapshot = {0};
    tide_text_use(&heap, NULL);
    tide_list source = {0}, target = {0}, plan = {0};
    tide_list_set(&source, f.source, 1, TIDE_IN_MATCH);
    tide_list_set(&target, f.target, 1, TIDE_IN_MATCH);
    tide_list_set(&plan, f.plan, 1, TIDE_IN_MATCH);
    const uint64_t hash = tide_heap_hash(0, &heap);
    tide_heap_copy(&snapshot, &heap);
    uint8_t *bytes = NULL; uint32_t size = 0; char why[256];
    const uint32_t before = tide_scratch_mark();
    TIDE_REQUIRE(tide_retarget_bake(source, 0, target, plan, &f.settings, &bytes, &size, why));
    TIDE_CHECK(tide_scratch_mark() == before && tide_heap_hash(0, &heap) == hash && tide_heap_hash(0, &snapshot) == hash);
    TIDE_CHECK(!tide_retarget_bake(source, 0, target, (tide_list){0}, &f.settings, &bytes, &size, why));
    TIDE_CHECK(tide_scratch_mark() == before && tide_heap_hash(0, &heap) == hash && tide_heap_hash(0, &snapshot) == hash);
    tide_text_use(NULL, NULL);
    tide_heap_free(&heap); tide_heap_free(&snapshot);
    tide_scratch_reset(mark);
    TIDE_CHECK(tide_skinned_data_validate(bytes, size));
    free(bytes);
}
