#pragma once

#include "tide/skinned.h"

// Humanoid roles follow Unity's HumanBodyBones order. A rig maps each role
// to a node of its reference pose. Missing roles have node index -1.
typedef enum tide_humanoid_bone {
    TIDE_HUMANOID_HIPS,
    TIDE_HUMANOID_LEFT_UPPER_LEG, TIDE_HUMANOID_RIGHT_UPPER_LEG,
    TIDE_HUMANOID_LEFT_LOWER_LEG, TIDE_HUMANOID_RIGHT_LOWER_LEG,
    TIDE_HUMANOID_LEFT_FOOT, TIDE_HUMANOID_RIGHT_FOOT,
    TIDE_HUMANOID_SPINE, TIDE_HUMANOID_CHEST, TIDE_HUMANOID_NECK, TIDE_HUMANOID_HEAD,
    TIDE_HUMANOID_LEFT_SHOULDER, TIDE_HUMANOID_RIGHT_SHOULDER,
    TIDE_HUMANOID_LEFT_UPPER_ARM, TIDE_HUMANOID_RIGHT_UPPER_ARM,
    TIDE_HUMANOID_LEFT_LOWER_ARM, TIDE_HUMANOID_RIGHT_LOWER_ARM,
    TIDE_HUMANOID_LEFT_HAND, TIDE_HUMANOID_RIGHT_HAND,
    TIDE_HUMANOID_LEFT_TOES, TIDE_HUMANOID_RIGHT_TOES,
    TIDE_HUMANOID_LEFT_EYE, TIDE_HUMANOID_RIGHT_EYE, TIDE_HUMANOID_JAW,
    TIDE_HUMANOID_LEFT_THUMB_PROXIMAL, TIDE_HUMANOID_LEFT_THUMB_INTERMEDIATE, TIDE_HUMANOID_LEFT_THUMB_DISTAL,
    TIDE_HUMANOID_LEFT_INDEX_PROXIMAL, TIDE_HUMANOID_LEFT_INDEX_INTERMEDIATE, TIDE_HUMANOID_LEFT_INDEX_DISTAL,
    TIDE_HUMANOID_LEFT_MIDDLE_PROXIMAL, TIDE_HUMANOID_LEFT_MIDDLE_INTERMEDIATE, TIDE_HUMANOID_LEFT_MIDDLE_DISTAL,
    TIDE_HUMANOID_LEFT_RING_PROXIMAL, TIDE_HUMANOID_LEFT_RING_INTERMEDIATE, TIDE_HUMANOID_LEFT_RING_DISTAL,
    TIDE_HUMANOID_LEFT_LITTLE_PROXIMAL, TIDE_HUMANOID_LEFT_LITTLE_INTERMEDIATE, TIDE_HUMANOID_LEFT_LITTLE_DISTAL,
    TIDE_HUMANOID_RIGHT_THUMB_PROXIMAL, TIDE_HUMANOID_RIGHT_THUMB_INTERMEDIATE, TIDE_HUMANOID_RIGHT_THUMB_DISTAL,
    TIDE_HUMANOID_RIGHT_INDEX_PROXIMAL, TIDE_HUMANOID_RIGHT_INDEX_INTERMEDIATE, TIDE_HUMANOID_RIGHT_INDEX_DISTAL,
    TIDE_HUMANOID_RIGHT_MIDDLE_PROXIMAL, TIDE_HUMANOID_RIGHT_MIDDLE_INTERMEDIATE, TIDE_HUMANOID_RIGHT_MIDDLE_DISTAL,
    TIDE_HUMANOID_RIGHT_RING_PROXIMAL, TIDE_HUMANOID_RIGHT_RING_INTERMEDIATE, TIDE_HUMANOID_RIGHT_RING_DISTAL,
    TIDE_HUMANOID_RIGHT_LITTLE_PROXIMAL, TIDE_HUMANOID_RIGHT_LITTLE_INTERMEDIATE, TIDE_HUMANOID_RIGHT_LITTLE_DISTAL,
    TIDE_HUMANOID_UPPER_CHEST,
    TIDE_HUMANOID_COUNT
} tide_humanoid_bone;

enum {
    TIDE_HUMANOID_MISSING, TIDE_HUMANOID_AUTOMATIC,
    TIDE_HUMANOID_UNCERTAIN, TIDE_HUMANOID_MANUAL
};

typedef enum tide_retarget_error {
    TIDE_RETARGET_OK, TIDE_RETARGET_INVALID_RIG, TIDE_RETARGET_MISSING_BONE,
    TIDE_RETARGET_AMBIGUOUS_BONE, TIDE_RETARGET_INVALID_REFERENCE_POSE,
    TIDE_RETARGET_INCOMPATIBLE_SKELETON, TIDE_RETARGET_UNSUPPORTED_SCALE
} tide_retarget_error;

// Discovery may be partial. Both lists have TIDE_HUMANOID_COUNT int32 values.
// A valid reference uses finite transforms and positive uniform local scale.
// Invalid data leaves the output lists unchanged. No pointer escapes the call.
bool tide_humanoid_auto(tide_list data, tide_list reference,
                        tide_list *bones, tide_list *status, uint32_t where);

// Prepare once for the actual animation export, separately from its rig's
// calibrated reference pose. The plan is immutable, pointer-free owned bytes.
// Outputs change only on success. root_scale receives the target/source leg
// length ratio. Required roles are hips, head, upper/lower arms and legs,
// hands and feet; torso, shoulder, neck, toes and fingers are optional.
int32_t tide_retarget_create(tide_list source_data,
                             tide_list source_reference_data, tide_list source_reference,
                             tide_list source_bones, tide_list source_status,
                             tide_list target_data, tide_list target_reference,
                             tide_list target_bones, tide_list target_status,
                             tide_list *plan, float *root_scale, uint32_t where);

// Transfer into the target skeleton, preserving its calibrated local lengths
// and scale. root_scale is absolute (zero removes root travel); root_offset is
// model-space metres; heading_degrees turns the result about the target up
// direction at its reference hips. Source scale animation is unsupported.
// Invalid inputs leave out unchanged. Source transforms and output may alias;
// output may not alias any byte list (plan or skeleton data).
bool tide_retarget_apply(tide_list plan, tide_list source_data, tide_list source_transforms,
                         tide_list target_data, float root_scale, tide_float3 root_offset,
                         float heading_degrees, float spine_distribution, float twist_distribution,
                         tide_list *out, uint32_t where);
