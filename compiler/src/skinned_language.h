#pragma once

// Checked model operations shared by the checker and generated C adapters.
typedef enum skinned_operation {
    SKIN_OP_NONE,
    SKIN_OP_ANIMATIONS,
    SKIN_OP_SKELETON,
    SKIN_OP_BONES,
    SKIN_OP_ANIMATION_MEMBER,
    SKIN_OP_BONE_MEMBER,
    SKIN_OP_ANIMATION_COUNT,
    SKIN_OP_BONE_COUNT,
    SKIN_OP_ANIMATION_NAME,
    SKIN_OP_ANIMATION_DURATION,
    SKIN_OP_TRY_GET_ANIMATION_BY_NAME,
    SKIN_OP_TRY_GET_BONE_BY_NAME,
    SKIN_OP_TRY_GET_ANIMATION_AT,
    SKIN_OP_TRY_GET_BONE_AT,
    SKIN_OP_REST_POSE,
    SKIN_OP_ANIMATION_SAMPLE,
    SKIN_OP_POSE_SAMPLE,
    SKIN_OP_POSE_BLEND,
    SKIN_OP_POSE_MODEL,
    SKIN_OP_POSE_INDEX,
    SKIN_OP_DECODE,
    SKIN_OP_BLEND_SHAPES,
    SKIN_OP_BLEND_SHAPE_COUNT,
    SKIN_OP_BLEND_SHAPE_NAME,
    SKIN_OP_BLEND_SHAPE_WEIGHT,
    SKIN_OP_TRY_GET_BLEND_SHAPE_BY_NAME,
    SKIN_OP_TRY_GET_BLEND_SHAPE_AT,
    SKIN_OP_SHAPED,
    SKIN_OP_HUMANOID_AUTO,
    SKIN_OP_HUMANOID_INDEX,
    SKIN_OP_HUMANOID_STATUS,
    SKIN_OP_RETARGET_CREATE,
    SKIN_OP_POSE_RETARGET,
    SKIN_OP_RETARGET_BAKE_ANIMATION,
    SKIN_OP_RETARGET_BAKE_CATALOG,
    SKIN_OP_BONE_INDEX,
    SKIN_OP_BONE_NAME,
} skinned_operation;

static inline bool skinned_bakes(const skinned_operation op)
{
    return op == SKIN_OP_RETARGET_BAKE_ANIMATION || op == SKIN_OP_RETARGET_BAKE_CATALOG;
}

static inline bool skinned_changes_pose(const skinned_operation op)
{
    return op == SKIN_OP_POSE_SAMPLE || op == SKIN_OP_POSE_BLEND || op == SKIN_OP_POSE_RETARGET;
}

static inline bool skinned_static(const skinned_operation op)
{
    return op == SKIN_OP_DECODE || op == SKIN_OP_HUMANOID_AUTO || op == SKIN_OP_RETARGET_CREATE;
}

static inline bool skinned_lookup(const skinned_operation op)
{
    return op == SKIN_OP_TRY_GET_ANIMATION_BY_NAME || op == SKIN_OP_TRY_GET_BONE_BY_NAME
        || op == SKIN_OP_TRY_GET_ANIMATION_AT || op == SKIN_OP_TRY_GET_BONE_AT
        || op == SKIN_OP_TRY_GET_BLEND_SHAPE_BY_NAME || op == SKIN_OP_TRY_GET_BLEND_SHAPE_AT;
}
