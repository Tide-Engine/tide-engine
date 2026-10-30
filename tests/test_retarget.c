#include <float.h>
#include <stdio.h>
#include <string.h>

#include "tide/net.h"
#include "tide/retarget.h"
#include "tide/sha256.h"
#include "tide/skinned_data.h"
#include "tide_test.h"

typedef struct test_node { const char *name; int32_t parent; tide_bone_transform local; } test_node;
typedef struct test_rig { test_node nodes[96]; int32_t count, role[TIDE_HUMANOID_COUNT], helper, wrapper; } test_rig;

static int32_t add_node(test_rig *r, const char *name, const int32_t parent, const tide_float3 position)
{
    const int32_t index = r->count++;
    r->nodes[index] = (test_node){name, parent, {position, tide_identity_q(), {1, 1, 1}}};
    return index;
}

static int32_t add_role(test_rig *r, const int role, const char *name, const int32_t parent, const tide_float3 position)
{
    const int32_t index = add_node(r, name, parent, position);
    r->role[role] = index;
    return index;
}

static test_rig humanoid(const bool synty, const float size, const float arm_angle, const bool helper)
{
    test_rig r = {0};
    r.helper = r.wrapper = -1;
    for (int i = 0; i < TIDE_HUMANOID_COUNT; i++) r.role[i] = -1;
    add_node(&r, "", -1, tide_f3(0, 0, 0));
    const int32_t hips = add_role(&r, TIDE_HUMANOID_HIPS, synty ? "Hips" : "mixamorig:Hips", 0, tide_f3(0, size, 0));
    const int32_t spine = add_role(&r, TIDE_HUMANOID_SPINE, "Spine_01", hips, tide_f3(0, size * 0.2f, 0));
    const int32_t chest = add_role(&r, TIDE_HUMANOID_CHEST, "Spine_02", spine, tide_f3(0, size * 0.3f, 0));
    const int32_t upper = add_role(&r, TIDE_HUMANOID_UPPER_CHEST, "Spine_03", chest, tide_f3(0, size * 0.1f, 0));
    const int32_t neck = add_role(&r, TIDE_HUMANOID_NECK, "Neck", upper, tide_f3(0, size * 0.15f, 0));
    add_role(&r, TIDE_HUMANOID_HEAD, "Head", neck, tide_f3(0, size * 0.15f, 0));
    for (int side = 0; side < 2; side++) {
        const float sign = side ? 1.0f : -1.0f;
        const int32_t clavicle = add_role(&r, TIDE_HUMANOID_LEFT_SHOULDER + side,
            synty ? side ? "Clavicle_R" : "Clavicle_L" : side ? "mixamorig:RightShoulder" : "mixamorig:LeftShoulder",
            upper, tide_f3(sign * size * 0.1f, 0, 0));
        const int32_t arm = add_role(&r, TIDE_HUMANOID_LEFT_UPPER_ARM + side,
            synty ? side ? "Shoulder_R" : "Shoulder_L" : side ? "mixamorig:RightArm" : "mixamorig:LeftArm",
            clavicle, tide_f3(sign * size * 0.2f, 0, 0));
        r.nodes[arm].local.rotation = tide_axisangle_q(tide_f3(0, 0, 1), -sign * arm_angle);
        int32_t elbow_parent = arm;
        if (helper && !side) {
            r.helper = add_node(&r, "upperarm_twist_l", arm, tide_f3(sign * size * 0.2f, 0, 0));
            elbow_parent = r.helper;
        }
        const int32_t elbow = add_role(&r, TIDE_HUMANOID_LEFT_LOWER_ARM + side,
            synty ? side ? "Elbow_R" : "Elbow_L" : side ? "mixamorig:RightForeArm" : "mixamorig:LeftForeArm",
            elbow_parent, tide_f3(sign * size * (helper && !side ? 0.2f : 0.4f), 0, 0));
        const int32_t hand = add_role(&r, TIDE_HUMANOID_LEFT_HAND + side,
            synty ? side ? "Hand_R" : "Hand_L" : side ? "mixamorig:RightHand" : "mixamorig:LeftHand",
            elbow, tide_f3(sign * size * 0.3f, 0, 0));
        const int32_t finger = add_role(&r, TIDE_HUMANOID_LEFT_MIDDLE_PROXIMAL + side * 15,
            side ? "Finger_01_1" : "Finger_01", hand, tide_f3(sign * size * 0.1f, 0, 0));
        const int32_t middle = add_role(&r, TIDE_HUMANOID_LEFT_MIDDLE_INTERMEDIATE + side * 15,
            side ? "Finger_02_1" : "Finger_02", finger, tide_f3(sign * size * 0.04f, 0, 0));
        add_role(&r, TIDE_HUMANOID_LEFT_MIDDLE_DISTAL + side * 15,
            side ? "Finger_03_1" : "Finger_03", middle, tide_f3(sign * size * 0.03f, 0, 0));
        add_role(&r, TIDE_HUMANOID_LEFT_INDEX_PROXIMAL + side * 15,
            side ? "IndexFinger_01_1" : "IndexFinger_01", hand, tide_f3(sign * size * 0.1f, 0, size * 0.03f));
        const int32_t thigh = add_role(&r, TIDE_HUMANOID_LEFT_UPPER_LEG + side,
            synty ? side ? "UpperLeg_R" : "UpperLeg_L" : side ? "mixamorig:RightUpLeg" : "mixamorig:LeftUpLeg",
            hips, tide_f3(sign * size * 0.12f, 0, 0));
        const int32_t shin = add_role(&r, TIDE_HUMANOID_LEFT_LOWER_LEG + side,
            synty ? side ? "LowerLeg_R" : "LowerLeg_L" : side ? "mixamorig:RightLeg" : "mixamorig:LeftLeg",
            thigh, tide_f3(0, -size * 0.45f, 0));
        const int32_t foot = add_role(&r, TIDE_HUMANOID_LEFT_FOOT + side,
            synty ? side ? "Ankle_R" : "Ankle_L" : side ? "mixamorig:RightFoot" : "mixamorig:LeftFoot",
            shin, tide_f3(0, -size * 0.45f, 0));
        add_role(&r, TIDE_HUMANOID_LEFT_TOES + side, side ? "Ball_R" : "Ball_L", foot, tide_f3(0, -size * 0.05f, size * 0.1f));
    }
    add_node(&r, "ik_foot_root", 0, tide_f3(0, 0, 0));
    add_node(&r, "abac_dyn_cape", chest, tide_f3(0, 0, -size * 0.1f));
    return r;
}

static void put_word(uint8_t *p, const uint32_t at, const uint32_t value)
{
    for (uint32_t i = 0; i < 4u; i++) p[at + i] = (uint8_t)(value >> (i * 8u));
}

static void put_number(uint8_t *p, const uint32_t at, const float value) { put_word(p, at, tide_f_bits(value)); }

static tide_list model_of(const test_rig *r)
{
    uint8_t bytes[16384] = {0};
    put_word(bytes, 0, TIDE_SKINNED_DATA_MAGIC);
    put_word(bytes, 4, TIDE_SKINNED_DATA_VERSION);
    put_word(bytes, 16, (uint32_t)r->count);
    put_word(bytes, 20, TIDE_SKINNED_HEADER_SIZE);
    uint32_t name_at = TIDE_SKINNED_HEADER_SIZE + (uint32_t)r->count * TIDE_SKINNED_NODE_SIZE;
    const uint32_t names = name_at;
    for (int32_t i = 0; i < r->count; i++) {
        const test_node n = r->nodes[i];
        const uint32_t at = TIDE_SKINNED_HEADER_SIZE + (uint32_t)i * TIDE_SKINNED_NODE_SIZE;
        const uint32_t length = (uint32_t)strlen(n.name);
        put_word(bytes, at, (uint32_t)n.parent);
        put_word(bytes, at + 4u, i ? (uint32_t)i - 1u : UINT32_MAX);
        put_word(bytes, at + 8u, name_at); put_word(bytes, at + 12u, length);
        memcpy(bytes + name_at, n.name, length + 1u); name_at += length + 1u;
        const float values[] = {n.local.position.x, n.local.position.y, n.local.position.z,
            n.local.rotation.value.x, n.local.rotation.value.y, n.local.rotation.value.z, n.local.rotation.value.w,
            n.local.scale.x, n.local.scale.y, n.local.scale.z};
        for (uint32_t k = 0; k < 10u; k++) put_number(bytes, at + 16u + k * 4u, values[k]);
    }
    put_word(bytes, 72, names); put_word(bytes, 76, name_at - names);
    const uint32_t size = (name_at + 3u) & ~3u;
    for (uint32_t at = 24; at <= 64; at += 8u) put_word(bytes, at + 4u, size);
    put_word(bytes, 80, size); put_word(bytes, 8, size);
    tide_sha256_of(bytes + TIDE_SKINNED_HEADER_SIZE, size - TIDE_SKINNED_HEADER_SIZE, bytes + 88);
    return tide_list_from(bytes, (int32_t)size, 1u);
}

static tide_list reference_of(const test_rig *r)
{
    tide_bone_transform transforms[96];
    for (int32_t i = 0; i < r->count; i++) transforms[i] = r->nodes[i].local;
    return tide_list_from(transforms, r->count, sizeof *transforms);
}

static int32_t role_at(const tide_list bones, const int role)
{
    const int32_t *at = tide_list_at(bones, role, sizeof *at);
    return at ? *at : -1;
}

static tide_bone_transform *transform_at(const tide_list pose, const int32_t index)
{
    return tide_list_at_mut(pose, index, sizeof(tide_bone_transform));
}

static tide_float3 model_position(const tide_list data, const tide_list pose, const int32_t node)
{
    const tide_float4x4 matrix = tide_skinned_model(data, pose, node);
    return tide_f3(matrix.c3.x, matrix.c3.y, matrix.c3.z);
}

static uint64_t transform_hash(const tide_list pose)
{
    uint64_t hash = TIDE_HASH_START;
    for (int32_t i = 0; i < tide_list_count(pose); i++) {
        const tide_bone_transform *value = tide_list_at(pose, i, sizeof *value);
        hash = tide_hash_more(hash, value, sizeof *value);
    }
    return tide_hash_end(hash);
}

static uint64_t byte_hash(const tide_list bytes)
{
    uint64_t hash = TIDE_HASH_START;
    for (int32_t i = 0; i < tide_list_count(bytes); i++) hash = tide_hash_more(hash, tide_list_at(bytes, i, 1u), 1u);
    return tide_hash_end(hash);
}

static bool near3(const tide_float3 a, const tide_float3 b)
{
    return tide_length_f3(tide_sub_f3(a, b)) < 0.0001f;
}

typedef struct prepared { tide_list source, target, source_pose, target_pose, source_bones, source_status, target_bones, target_status, map; float scale; } prepared;

static bool prepare(const test_rig *source, const test_rig *target, prepared *p)
{
    memset(p, 0, sizeof *p);
    p->source = model_of(source); p->target = model_of(target);
    p->source_pose = reference_of(source); p->target_pose = reference_of(target);
    return tide_skinned_valid(p->source) && tide_skinned_valid(p->target)
        && tide_humanoid_auto(p->source, p->source_pose, &p->source_bones, &p->source_status, TIDE_IN_SCRATCH)
        && tide_humanoid_auto(p->target, p->target_pose, &p->target_bones, &p->target_status, TIDE_IN_SCRATCH)
        && tide_retarget_create(p->source, p->source, p->source_pose, p->source_bones, p->source_status,
            p->target, p->target_pose, p->target_bones, p->target_status, &p->map, &p->scale, TIDE_IN_SCRATCH) == TIDE_RETARGET_OK;
}

static bool apply(const prepared *p, const tide_list input, tide_list *out)
{
    return tide_retarget_apply(p->map, p->source, input, p->target, p->scale, tide_f3(0, 0, 0), 0, 1, 1, out, TIDE_IN_SCRATCH);
}

TIDE_TEST(retarget_auto_matches_mixamo_synty_chains_and_finger_suffixes)
{
    const uint32_t mark = tide_scratch_mark();
    const test_rig source = humanoid(false, 1, 0, false), target = humanoid(true, 2, 0.5f, true);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    TIDE_CHECK(tide_abs_f(p.scale - 2.0f) < 0.00001f);
    for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) {
        TIDE_CHECK(role_at(p.source_bones, role) == source.role[role]);
        TIDE_CHECK(role_at(p.target_bones, role) == target.role[role]);
        if (source.role[role] >= 0) TIDE_CHECK(role_at(p.source_status, role) == TIDE_HUMANOID_AUTOMATIC);
        if (target.role[role] >= 0) TIDE_CHECK(role_at(p.target_status, role) == TIDE_HUMANOID_AUTOMATIC);
    }
    // Crossing the hands in a deliberately edited reference cannot make the
    // geometry fallback steal fingers from their known anatomical parent.
    tide_list crossed = tide_list_copy(p.target_pose, sizeof(tide_bone_transform));
    transform_at(crossed, target.role[TIDE_HUMANOID_LEFT_HAND])->position.x = 10.0f;
    tide_list roles = {0}, statuses = {0};
    TIDE_REQUIRE(tide_humanoid_auto(p.target, crossed, &roles, &statuses, TIDE_IN_SCRATCH));
    TIDE_CHECK(role_at(roles, TIDE_HUMANOID_LEFT_MIDDLE_INTERMEDIATE) == target.role[TIDE_HUMANOID_LEFT_MIDDLE_INTERMEDIATE]);
    TIDE_CHECK(role_at(roles, TIDE_HUMANOID_RIGHT_MIDDLE_INTERMEDIATE) == target.role[TIDE_HUMANOID_RIGHT_MIDDLE_INTERMEDIATE]);
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_calibrates_a_pose_and_bone_axes_preserving_target_lengths)
{
    const uint32_t mark = tide_scratch_mark();
    const test_rig source = humanoid(false, 1, 0, false);
    test_rig target = humanoid(true, 2, TIDE_PI_F / 6.0f, false);
    const int32_t arm = target.role[TIDE_HUMANOID_LEFT_UPPER_ARM];
    const int32_t elbow = target.role[TIDE_HUMANOID_LEFT_LOWER_ARM];
    const tide_quaternion roll = tide_axisangle_q(tide_f3(1, 0, 0), 0.7f);
    target.nodes[arm].local.rotation = tide_mul_q(target.nodes[arm].local.rotation, roll);
    target.nodes[elbow].local.rotation = tide_conjugate_q(roll);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    tide_list output = {0};
    TIDE_REQUIRE(apply(&p, p.source_pose, &output));
    const tide_float3 shoulder = model_position(p.target, output, arm);
    const tide_float3 rest_elbow = model_position(p.target, output, elbow);
    TIDE_CHECK(near3(tide_sub_f3(rest_elbow, shoulder), tide_f3(-0.8f, 0, 0)));
    tide_list animated = tide_list_copy(p.source_pose, sizeof(tide_bone_transform));
    transform_at(animated, source.role[TIDE_HUMANOID_LEFT_UPPER_ARM])->rotation = tide_axisangle_q(tide_f3(0, 0, 1), -TIDE_PI_F * 0.5f);
    TIDE_REQUIRE(apply(&p, animated, &output));
    TIDE_CHECK(near3(tide_sub_f3(model_position(p.target, output, elbow), shoulder), tide_f3(0, 0.8f, 0)));
    for (int32_t i = 0; i < target.count; i++) if (i != target.role[TIDE_HUMANOID_HIPS]) {
        const tide_bone_transform *local = tide_list_at(output, i, sizeof *local);
        TIDE_CHECK(near3(local->position, target.nodes[i].local.position));
        TIDE_CHECK(near3(local->scale, target.nodes[i].local.scale));
    }
    // Axial rotation survives different rest rolls, independently of swing.
    transform_at(animated, source.role[TIDE_HUMANOID_LEFT_UPPER_ARM])->rotation = tide_axisangle_q(tide_f3(1, 0, 0), 0.4f);
    TIDE_REQUIRE(apply(&p, animated, &output));
    const tide_float4x4 matrix = tide_skinned_model(p.target, output, arm);
    const tide_float3 expected_up = tide_rotate_q(tide_mul_q(tide_axisangle_q(tide_f3(1, 0, 0), 0.4f), roll), tide_f3(0, 1, 0));
    TIDE_CHECK(near3(tide_f3(matrix.c1.x, matrix.c1.y, matrix.c1.z), expected_up));
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_twist_helpers_do_not_distribute_elbow_swing)
{
    const uint32_t mark = tide_scratch_mark();
    const test_rig source = humanoid(false, 1, 0, false), target = humanoid(true, 1, 0, true);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    tide_list animated = tide_list_copy(p.source_pose, sizeof(tide_bone_transform)), output = {0};
    transform_at(animated, source.role[TIDE_HUMANOID_LEFT_LOWER_ARM])->rotation = tide_axisangle_q(tide_f3(0, 0, 1), TIDE_PI_F * 0.5f);
    TIDE_REQUIRE(apply(&p, animated, &output));
    const tide_float3 elbow = model_position(p.target, output, target.role[TIDE_HUMANOID_LEFT_LOWER_ARM]);
    TIDE_CHECK(near3(elbow, model_position(p.source, animated, source.role[TIDE_HUMANOID_LEFT_LOWER_ARM])));
    TIDE_CHECK(near3(model_position(p.target, output, target.role[TIDE_HUMANOID_LEFT_HAND]), model_position(p.source, animated, source.role[TIDE_HUMANOID_LEFT_HAND])));
    const tide_bone_transform *helper = tide_list_at(output, target.helper, sizeof *helper);
    TIDE_CHECK(tide_abs_f(tide_dot_q(helper->rotation, tide_identity_q())) > 0.99999f);
    transform_at(animated, source.role[TIDE_HUMANOID_LEFT_LOWER_ARM])->rotation = tide_axisangle_q(tide_f3(1, 0, 0), TIDE_PI_F * 0.5f);
    TIDE_REQUIRE(apply(&p, animated, &output));
    helper = tide_list_at(output, target.helper, sizeof *helper);
    TIDE_CHECK(near3(tide_rotate_q(helper->rotation, tide_f3(0, 1, 0)), tide_f3(0, tide_sqrt_f(0.5f), tide_sqrt_f(0.5f))));
    TIDE_REQUIRE(tide_retarget_apply(p.map, p.source, animated, p.target, p.scale, tide_f3(0, 0, 0), 0, 1, 0, &output, TIDE_IN_SCRATCH));
    helper = tide_list_at(output, target.helper, sizeof *helper);
    TIDE_CHECK(tide_abs_f(tide_dot_q(helper->rotation, tide_identity_q())) > 0.99999f);
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_root_controls_and_unrelated_target_bones)
{
    const uint32_t mark = tide_scratch_mark();
    const test_rig source = humanoid(false, 1, 0, false), target = humanoid(true, 2, 0, false);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    tide_list animated = tide_list_copy(p.source_pose, sizeof(tide_bone_transform)), output = {0};
    transform_at(animated, source.role[TIDE_HUMANOID_HIPS])->position.x += 1.0f;
    TIDE_REQUIRE(tide_retarget_apply(p.map, p.source, animated, p.target, p.scale, tide_f3(3, 4, 5), 90, 1, 1, &output, TIDE_IN_SCRATCH));
    TIDE_CHECK(near3(model_position(p.target, output, target.role[TIDE_HUMANOID_HIPS]), tide_f3(3, 6, 3)));
    const tide_bone_transform *cape = tide_list_at(output, target.count - 1, sizeof *cape);
    TIDE_CHECK(memcmp(cape, &target.nodes[target.count - 1].local, sizeof *cape) == 0);
    TIDE_REQUIRE(tide_retarget_apply(p.map, p.source, animated, p.target, 0, tide_f3(0, 0, 0), 0, 1, 1, &output, TIDE_IN_SCRATCH));
    TIDE_CHECK(near3(model_position(p.target, output, target.role[TIDE_HUMANOID_HIPS]), tide_f3(0, 2, 0)));
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_reference_body_facing_calibrates_rotation_and_travel)
{
    const uint32_t mark = tide_scratch_mark();
    test_rig source = humanoid(false, 1, 0, false);
    const test_rig target = humanoid(true, 2, 0, false);
    source.nodes[0].local.rotation = tide_axisangle_q(tide_f3(0, 1, 0), TIDE_PI_F * 0.5f);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    tide_list animated = tide_list_copy(p.source_pose, sizeof(tide_bone_transform)), output = {0};
    transform_at(animated, source.role[TIDE_HUMANOID_HIPS])->position.x += 1.0f;
    transform_at(animated, source.role[TIDE_HUMANOID_LEFT_UPPER_ARM])->rotation = tide_axisangle_q(tide_f3(0, 0, 1), -TIDE_PI_F * 0.5f);
    TIDE_REQUIRE(apply(&p, animated, &output));
    TIDE_CHECK(near3(model_position(p.target, output, target.role[TIDE_HUMANOID_HIPS]), tide_f3(2, 2, 0)));
    const tide_float3 shoulder = model_position(p.target, output, target.role[TIDE_HUMANOID_LEFT_UPPER_ARM]);
    const tide_float3 elbow = model_position(p.target, output, target.role[TIDE_HUMANOID_LEFT_LOWER_ARM]);
    TIDE_CHECK(near3(tide_sub_f3(elbow, shoulder), tide_f3(0, 0.8f, 0)));
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_different_spine_counts_distribute_between_semantic_anchors)
{
    const uint32_t mark = tide_scratch_mark();
    const test_rig source = humanoid(false, 1, 0, false);
    test_rig target = humanoid(true, 1, 0, false);
    const int32_t inserted = target.role[TIDE_HUMANOID_CHEST];
    for (int32_t i = target.count; i > inserted; i--) target.nodes[i] = target.nodes[i - 1];
    target.count++;
    for (int32_t i = 0; i < target.count; i++) if (target.nodes[i].parent >= inserted) target.nodes[i].parent++;
    for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) if (target.role[role] >= inserted) target.role[role]++;
    target.nodes[inserted].name = "torso_helper";
    target.nodes[inserted].local.position.y = 0.15f;
    target.nodes[inserted + 1].parent = inserted;
    target.nodes[inserted + 1].local.position.y = 0.15f;
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    tide_list animated = tide_list_copy(p.source_pose, sizeof(tide_bone_transform)), output = {0};
    transform_at(animated, source.role[TIDE_HUMANOID_CHEST])->rotation = tide_axisangle_q(tide_f3(0, 0, 1), TIDE_PI_F * 0.5f);
    TIDE_REQUIRE(apply(&p, animated, &output));
    const tide_float4x4 helper = tide_skinned_model(p.target, output, inserted);
    TIDE_CHECK(near3(tide_f3(helper.c0.x, helper.c0.y, helper.c0.z), tide_f3(tide_sqrt_f(0.5f), tide_sqrt_f(0.5f), 0)));
    const tide_float4x4 chest = tide_skinned_model(p.target, output, target.role[TIDE_HUMANOID_CHEST]);
    TIDE_CHECK(near3(tide_f3(chest.c0.x, chest.c0.y, chest.c0.z), tide_f3(0, 1, 0)));
    TIDE_REQUIRE(tide_retarget_apply(p.map, p.source, animated, p.target, p.scale, tide_f3(0, 0, 0), 0, 0, 1, &output, TIDE_IN_SCRATCH));
    const tide_bone_transform *local = tide_list_at(output, inserted, sizeof *local);
    TIDE_CHECK(near3(tide_rotate_q(local->rotation, tide_f3(1, 0, 0)), tide_f3(1, 0, 0)));
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_auto_ignores_mesh_aliases_and_handles_toe_and_mitten_chains)
{
    const uint32_t mark = tide_scratch_mark();
    test_rig source = humanoid(true, 1, 0, false);
    const int32_t geometry = add_node(&source, "GEO", 0, tide_f3(0, 0, 0));
    add_node(&source, "mannequin_geo_pelvis", geometry, tide_f3(0, 0, 0));
    add_node(&source, "mannequin_geo_lowerarm_l", geometry, tide_f3(0, 0, 0));
    add_node(&source, "character_mesh_head", geometry, tide_f3(0, 0, 0));
    add_node(&source, "Toes_L", source.role[TIDE_HUMANOID_LEFT_TOES], tide_f3(0, 0, 0.05f));
    source.nodes[source.role[TIDE_HUMANOID_LEFT_INDEX_PROXIMAL]].name = "FingerIndex_01_L";
    tide_list roles = {0}, statuses = {0};
    TIDE_REQUIRE(tide_humanoid_auto(model_of(&source), reference_of(&source), &roles, &statuses, TIDE_IN_SCRATCH));
    for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) if (source.role[role] >= 0) {
        TIDE_CHECK(role_at(roles, role) == source.role[role]);
        TIDE_CHECK(role_at(statuses, role) == TIDE_HUMANOID_AUTOMATIC);
    }
    source.nodes[source.role[TIDE_HUMANOID_LEFT_MIDDLE_INTERMEDIATE]].parent = source.role[TIDE_HUMANOID_LEFT_HAND];
    TIDE_REQUIRE(tide_humanoid_auto(model_of(&source), reference_of(&source), &roles, &statuses, TIDE_IN_SCRATCH));
    TIDE_CHECK(role_at(statuses, TIDE_HUMANOID_LEFT_MIDDLE_PROXIMAL) == TIDE_HUMANOID_UNCERTAIN);
    TIDE_CHECK(role_at(roles, TIDE_HUMANOID_LEFT_MIDDLE_PROXIMAL) == -1);
    TIDE_CHECK(role_at(roles, TIDE_HUMANOID_LEFT_INDEX_PROXIMAL) == source.role[TIDE_HUMANOID_LEFT_INDEX_PROXIMAL]);
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_optional_reference_fingers_may_be_absent_from_actual_export)
{
    const uint32_t mark = tide_scratch_mark();
    const test_rig source = humanoid(false, 1, 0, false), target = humanoid(true, 2, 0.4f, false);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    test_rig body = source;
    for (int role = TIDE_HUMANOID_LEFT_THUMB_PROXIMAL; role < TIDE_HUMANOID_UPPER_CHEST; role++)
        if (body.role[role] >= 0) body.nodes[body.role[role]].name = "";
    const tide_list body_data = model_of(&body), body_pose = reference_of(&body);
    tide_list map = {0}, output = {0};
    float scale;
    TIDE_REQUIRE(tide_retarget_create(body_data, p.source, p.source_pose, p.source_bones, p.source_status,
        p.target, p.target_pose, p.target_bones, p.target_status, &map, &scale, TIDE_IN_SCRATCH) == TIDE_RETARGET_OK);
    TIDE_REQUIRE(tide_retarget_apply(map, body_data, body_pose, p.target, scale, tide_f3(0, 0, 0), 0, 1, 1, &output, TIDE_IN_SCRATCH));
    TIDE_CHECK(role_at(p.source_bones, TIDE_HUMANOID_LEFT_MIDDLE_PROXIMAL) == source.role[TIDE_HUMANOID_LEFT_MIDDLE_PROXIMAL]);
    TIDE_CHECK(near3(model_position(p.target, output, target.role[TIDE_HUMANOID_LEFT_HAND]), tide_f3(-2, 3.2f, 0)));
    const tide_float3 hand = model_position(p.target, output, target.role[TIDE_HUMANOID_LEFT_HAND]);
    const tide_float3 finger = model_position(p.target, output, target.role[TIDE_HUMANOID_LEFT_MIDDLE_PROXIMAL]);
    TIDE_CHECK(near3(tide_sub_f3(finger, hand), tide_f3(-0.2f, 0, 0)));
    tide_scratch_reset(mark);
}

// A different exporter reorders independent branches, adds a centimetre
// armature object, and stores a take's first frame as its node defaults.
static test_rig animation_export(const test_rig *reference, const bool changed_defaults)
{
    test_rig out = {0};
    out.helper = -1;
    int32_t mapping[96];
    for (int i = 0; i < 96; i++) mapping[i] = -1;
    for (int i = 0; i < TIDE_HUMANOID_COUNT; i++) out.role[i] = -1;
    mapping[0] = add_node(&out, "", -1, tide_f3(0, 0, 0));
    out.wrapper = add_node(&out, "Armature", 0, tide_f3(0, 0, 0));
    out.nodes[out.wrapper].local.scale = tide_f3_splat(0.01f);
    for (int32_t pass = 0; pass < reference->count; pass++) {
        for (int32_t n = reference->count - 1; n > 0; n--) {
            if (mapping[n] >= 0 || mapping[reference->nodes[n].parent] < 0) continue;
            const test_node original = reference->nodes[n];
            const int32_t parent = original.parent == 0 ? out.wrapper : mapping[original.parent];
            const int32_t made = add_node(&out, original.name, parent, tide_mul_f3(original.local.position, tide_f3_splat(100.0f)));
            out.nodes[made].local.rotation = original.local.rotation;
            mapping[n] = made;
        }
    }
    for (int role = 0; role < TIDE_HUMANOID_COUNT; role++) if (reference->role[role] >= 0) out.role[role] = mapping[reference->role[role]];
    if (changed_defaults) {
        out.nodes[out.role[TIDE_HUMANOID_LEFT_UPPER_ARM]].local.rotation = tide_axisangle_q(tide_f3(0, 1, 0), 0.9f);
        out.nodes[out.role[TIDE_HUMANOID_HIPS]].local.position.z += 55.0f;
    }
    return out;
}

TIDE_TEST(retarget_reference_is_independent_of_animation_defaults_order_and_wrappers)
{
    const uint32_t mark = tide_scratch_mark();
    const test_rig source = humanoid(false, 1, 0, false), target = humanoid(true, 2, 0.4f, false);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    const test_rig export_defaults = animation_export(&source, true), export_reference = animation_export(&source, false);
    const tide_list animation_data = model_of(&export_defaults);
    tide_list animation_pose = reference_of(&export_reference), map = {0}, expected = {0}, output = {0};
    float ratio = 0.0f;
    TIDE_REQUIRE(tide_retarget_create(animation_data, p.source, p.source_pose, p.source_bones, p.source_status,
        p.target, p.target_pose, p.target_bones, p.target_status, &map, &ratio, TIDE_IN_SCRATCH) == TIDE_RETARGET_OK);
    TIDE_CHECK(tide_abs_f(ratio - 2.0f) < 0.0001f);
    TIDE_REQUIRE(apply(&p, p.source_pose, &expected));
    TIDE_REQUIRE(tide_retarget_apply(map, animation_data, animation_pose, p.target, ratio, tide_f3(0, 0, 0), 0, 1, 1, &output, TIDE_IN_SCRATCH));
    for (int32_t i = 0; i < target.count; i++) TIDE_CHECK(near3(model_position(p.target, output, i), model_position(p.target, expected, i)));
    transform_at(animation_pose, export_reference.role[TIDE_HUMANOID_HIPS])->position.x += 100.0f;
    TIDE_REQUIRE(tide_retarget_apply(map, animation_data, animation_pose, p.target, ratio, tide_f3(0, 0, 0), 0, 1, 1, &animation_pose, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_list_count(animation_pose) == target.count);
    TIDE_CHECK(near3(model_position(p.target, animation_pose, target.role[TIDE_HUMANOID_HIPS]), tide_f3(2, 2, 0)));
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_missing_ambiguous_manual_and_invalid_references_are_atomic)
{
    const uint32_t mark = tide_scratch_mark();
    test_rig source = humanoid(false, 1, 0, false);
    const test_rig target = humanoid(true, 1, 0, false);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    const uint64_t original = byte_hash(p.map);
    const float before_scale = p.scale;
    source.nodes[source.role[TIDE_HUMANOID_LEFT_HAND]].name = "custom_wrist";
    const tide_list missing_data = model_of(&source), missing_pose = reference_of(&source);
    tide_list roles = {0}, statuses = {0};
    TIDE_REQUIRE(tide_humanoid_auto(missing_data, missing_pose, &roles, &statuses, TIDE_IN_SCRATCH));
    // A non-anatomical name still contains wrist, so use a truly opaque name.
    source.nodes[source.role[TIDE_HUMANOID_LEFT_HAND]].name = "custom_joint";
    const tide_list opaque = model_of(&source);
    TIDE_REQUIRE(tide_humanoid_auto(opaque, missing_pose, &roles, &statuses, TIDE_IN_SCRATCH));
    TIDE_CHECK(tide_retarget_create(opaque, opaque, missing_pose, roles, statuses, p.target, p.target_pose,
        p.target_bones, p.target_status, &p.map, &p.scale, TIDE_IN_SCRATCH) == TIDE_RETARGET_MISSING_BONE);
    TIDE_CHECK(byte_hash(p.map) == original && p.scale == before_scale);
    *(int32_t *)tide_list_at_mut(roles, TIDE_HUMANOID_LEFT_HAND, sizeof(int32_t)) = source.role[TIDE_HUMANOID_LEFT_HAND];
    *(int32_t *)tide_list_at_mut(statuses, TIDE_HUMANOID_LEFT_HAND, sizeof(int32_t)) = TIDE_HUMANOID_MANUAL;
    TIDE_REQUIRE(tide_retarget_create(opaque, opaque, missing_pose, roles, statuses, p.target, p.target_pose,
        p.target_bones, p.target_status, &p.map, &p.scale, TIDE_IN_SCRATCH) == TIDE_RETARGET_OK);
    source.nodes[source.role[TIDE_HUMANOID_LEFT_HAND]].name = "mixamorig:LeftHand";
    add_node(&source, "mixamorig:LeftHand", 0, tide_f3(3, 2, 1));
    const tide_list duplicate = model_of(&source), duplicate_pose = reference_of(&source);
    TIDE_REQUIRE(tide_humanoid_auto(duplicate, duplicate_pose, &roles, &statuses, TIDE_IN_SCRATCH));
    TIDE_CHECK(role_at(statuses, TIDE_HUMANOID_LEFT_HAND) == TIDE_HUMANOID_UNCERTAIN);
    TIDE_CHECK(tide_retarget_create(duplicate, duplicate, duplicate_pose, roles, statuses, p.target, p.target_pose,
        p.target_bones, p.target_status, &p.map, &p.scale, TIDE_IN_SCRATCH) == TIDE_RETARGET_AMBIGUOUS_BONE);
    tide_list invalid = tide_list_copy(p.target_pose, sizeof(tide_bone_transform));
    transform_at(invalid, target.role[TIDE_HUMANOID_LEFT_HAND])->scale.y = 2.0f;
    TIDE_CHECK(tide_retarget_create(p.source, p.source, p.source_pose, p.source_bones, p.source_status,
        p.target, invalid, p.target_bones, p.target_status, &p.map, &p.scale, TIDE_IN_SCRATCH) == TIDE_RETARGET_UNSUPPORTED_SCALE);
    transform_at(invalid, target.role[TIDE_HUMANOID_LEFT_HAND])->scale.y = 1.0f;
    transform_at(invalid, target.role[TIDE_HUMANOID_LEFT_HAND])->position.x = tide_f_from_bits(0x7fc00000u);
    TIDE_CHECK(tide_retarget_create(p.source, p.source, p.source_pose, p.source_bones, p.source_status,
        p.target, invalid, p.target_bones, p.target_status, &p.map, &p.scale, TIDE_IN_SCRATCH) == TIDE_RETARGET_INVALID_REFERENCE_POSE);
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_invalid_input_leaves_pose_and_heap_unchanged)
{
    const uint32_t mark = tide_scratch_mark();
    const test_rig source = humanoid(false, 1, 0, false), target = humanoid(true, 2, 0.4f, true);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    tide_heap heap = {0}, snapshot = {0};
    tide_text_use(&heap, NULL);
    tide_list output = {0}, kept_map = {0};
    tide_list_set(&output, p.target_pose, sizeof(tide_bone_transform), TIDE_IN_MATCH);
    tide_list_set(&kept_map, p.map, 1u, TIDE_IN_MATCH);
    tide_heap_copy(&snapshot, &heap);
    const uint64_t before_pose = transform_hash(output), before_heap = tide_heap_hash(TIDE_HASH_START, &heap);
    tide_list bad = tide_list_copy(p.source_pose, sizeof(tide_bone_transform));
    transform_at(bad, source.role[TIDE_HUMANOID_LEFT_UPPER_ARM])->rotation.value.x = tide_f_from_bits(0x7f800000u);
    TIDE_CHECK(!tide_retarget_apply(kept_map, p.source, bad, p.target, p.scale, tide_f3(0, 0, 0), 0, 1, 1, &output, TIDE_IN_MATCH));
    TIDE_CHECK(transform_hash(output) == before_pose && tide_heap_hash(TIDE_HASH_START, &heap) == before_heap);
    const float values[] = {-1.0f, tide_f_from_bits(0x7f800000u), tide_f_from_bits(0x7fc00000u)};
    for (uint32_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        TIDE_CHECK(!tide_retarget_apply(kept_map, p.source, p.source_pose, p.target, values[i], tide_f3(0, 0, 0), 0, 1, 1, &output, TIDE_IN_MATCH));
        TIDE_CHECK(transform_hash(output) == before_pose && tide_heap_hash(TIDE_HASH_START, &heap) == before_heap);
    }
    TIDE_CHECK(!tide_retarget_apply(kept_map, p.target, p.source_pose, p.target, p.scale, tide_f3(0, 0, 0), 0, 1, 1, &output, TIDE_IN_MATCH));
    const int32_t length = tide_list_count(p.map);
    const void *bytes = tide_list_at(p.map, 0, 1u);
    for (int32_t cut = 0; cut < length; cut++) {
        const uint32_t cut_mark = tide_scratch_mark();
        const tide_list truncated = tide_list_from(bytes, cut, 1u);
        TIDE_CHECK(!tide_retarget_apply(truncated, p.source, p.source_pose, p.target, p.scale, tide_f3(0, 0, 0), 0, 1, 1, &output, TIDE_IN_MATCH));
        tide_scratch_reset(cut_mark);
    }
    TIDE_CHECK(transform_hash(output) == before_pose && tide_heap_hash(TIDE_HASH_START, &heap) == before_heap);
    TIDE_REQUIRE(tide_retarget_apply(kept_map, p.source, p.source_pose, p.target, p.scale, tide_f3(1, 0, 0), 0, 1, 1, &output, TIDE_IN_MATCH));
    const tide_bone_transform *old = tide_list_read(&snapshot, output, target.role[TIDE_HUMANOID_HIPS], sizeof *old);
    TIDE_REQUIRE(old);
    TIDE_CHECK(old->position.x == 0.0f);
    const uint64_t settled = tide_heap_hash(TIDE_HASH_START, &heap);
    for (int i = 0; i < 20; i++) TIDE_CHECK(tide_retarget_apply(kept_map, p.source, p.source_pose, p.target, p.scale, tide_f3(1, 0, 0), 0, 1, 1, &output, TIDE_IN_MATCH));
    TIDE_CHECK(tide_heap_hash(TIDE_HASH_START, &heap) == settled);
    tide_heap_free(&snapshot); tide_heap_free(&heap); tide_text_use(NULL, NULL);
    tide_scratch_reset(mark);
}

TIDE_TEST(retarget_crossplatform_pose_bits)
{
    const uint32_t mark = tide_scratch_mark();
    const test_rig source = humanoid(false, 1, 0, false), target = humanoid(true, 1.7f, 0.43f, true);
    prepared p;
    TIDE_REQUIRE(prepare(&source, &target, &p));
    tide_list animated = tide_list_copy(p.source_pose, sizeof(tide_bone_transform)), output = {0};
    uint64_t hash = TIDE_HASH_START;
    for (int frame = 0; frame < 13; frame++) {
        const float t = (float)frame * 0.13f;
        transform_at(animated, source.role[TIDE_HUMANOID_HIPS])->position = tide_f3(t, 1.0f + t * 0.07f, -t * 0.2f);
        transform_at(animated, source.role[TIDE_HUMANOID_LEFT_UPPER_ARM])->rotation = tide_euler_q(tide_f3(t * 0.17f, t * 0.23f, -t));
        transform_at(animated, source.role[TIDE_HUMANOID_LEFT_LOWER_ARM])->rotation = tide_euler_q(tide_f3(t, 0, t * 0.11f));
        transform_at(animated, source.role[TIDE_HUMANOID_SPINE])->rotation = tide_euler_q(tide_f3(t * 0.09f, -t * 0.31f, t * 0.05f));
        TIDE_REQUIRE(tide_retarget_apply(p.map, p.source, animated, p.target, p.scale, tide_f3(0.1f, -0.04f, 0.2f), 17.0f, 0.7f, 0.8f, &output, TIDE_IN_SCRATCH));
        const uint64_t value = transform_hash(output);
        hash = tide_hash_more(hash, &value, sizeof value);
    }
    hash = tide_hash_end(hash);
    TIDE_CHECK(hash == UINT64_C(0xa669d398066dafa5));
    tide_scratch_reset(mark);
}
