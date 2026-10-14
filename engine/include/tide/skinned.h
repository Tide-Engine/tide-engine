#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/draw.h"
#include "tide/list.h"
#include "tide/math.h"
#include "tide/text.h"

// A skeleton node's parent-local transform. Pose values own a List of these;
// model bytes are immutable and use the portable layout in skinned_data.h.
typedef struct tide_bone_transform {
    tide_float3 position;
    tide_quaternion rotation;
    tide_float3 scale;
} tide_bone_transform;

// Invalid model data has no nodes, clips or fingerprint. Names and matrices
// return empty text and identity; a failed lookup returns -1.
bool tide_skinned_valid(tide_list data);
uint64_t tide_skinned_fingerprint(tide_list data);
bool tide_skinned_compatible(tide_list a, tide_list b);
int32_t tide_skinned_bone_count(tide_list data);
int32_t tide_skinned_animation_count(tide_list data);
int32_t tide_skinned_find_bone(tide_list data, tide_str name);
int32_t tide_skinned_find_animation(tide_list data, tide_str name);
tide_str tide_skinned_bone_name(tide_list data, int32_t bone);
tide_str tide_skinned_animation_name(tide_list data, int32_t animation);
float tide_skinned_animation_duration(tide_list data, int32_t animation);

// Fill an owned pose list, reusing its storage. Each sample begins with the
// authored rest transforms, so absent channels never carry an earlier clip.
// Seconds are clamped to the clip's endpoints; callers choose looping before
// sampling. Invalid data or a clip outside the model leaves the list alone.
bool tide_skinned_rest(tide_list data, tide_list *transforms, uint32_t where);
bool tide_skinned_sample(tide_list data, int32_t animation, float seconds,
                         tide_list *transforms, uint32_t where);

// Bind a clip or an existing pose to another export of the same rig. Required
// nodes match unique exact names and parents, independently of source node
// order or authored default poses. Required source local transforms are copied
// directly; unrelated target nodes start at target rest. No retargeting is done.
// Sampling requires the selected clip's targets and ancestors. A pose requires
// all its model's clip targets, changed transforms and ancestors. These are
// bindings, not retargeting: missing, ambiguous or different required nodes
// fail and leave the destination untouched. Source and destination may alias.
// An unnamed identity wrapper directly under node zero is transparent only
// while its source clip channels and pose remain at rest; node zero is kept.
bool tide_skinned_sample_into(tide_list target_data, tide_list animation_data, int32_t animation,
                              float seconds, tide_list *transforms, uint32_t where);
bool tide_skinned_pose_into(tide_list target_data, tide_list source_data, tide_list source_transforms,
                            tide_list *transforms, uint32_t where);

// Blend parent-local TRS values, with shortest-path quaternion interpolation.
// Callers check skeleton compatibility; unequal list counts leave out alone.
// Inputs and output may alias. Weight is clamped to 0..1, with NaN giving 0.
bool tide_skinned_blend(tide_list a, tide_list b, float weight,
                        tide_list *out, uint32_t where);

// Accumulated model-space transform, without inverse-bind offsets, and the
// complete palette of model-space skin matrices in the scratch area. Pose
// counts must match the model's nodes. Nothing caches a mutable pose's values.
tide_float4x4 tide_skinned_model(tide_list data, tide_list transforms, int32_t bone);
tide_list tide_skinned_palette(tide_list data, tide_list transforms);

// The GPU's two read-only textures of integer words: palette matrices, then
// hidden vertex influences. info is {palette count, vertex count, influence
// count}. Influences have one int4 header {first,count,0,0} per vertex, then
// pairs {palette index,weight float bits}, two per pixel. Matrices take four
// pixels each, columns in order. Both textures use TIDE_SHADER_READ_DATA.
// Bytes are captured by the draw list, so later pose edits cannot alter an
// earlier draw. False, and zero outputs, for invalid data or an incompatible
// pose; callers then record no draw. Geometry remains the ordinary V list.
bool tide_draw_skinned_data(tide_draw_list *draw, tide_list data, tide_list transforms,
                            uint32_t textures[2], int32_t info[3]);

// Generated skinned draws can share a palette page. info[3] is that pose's
// matrix base, in addition to the three counts above. Validated dependencies
// and current transforms are captured immediately; tide_draw_prepare fills
// the matrices before rendering or copying the list. Unproven model values
// retain the immediate checked path and have base zero.
bool tide_draw_skinned_atlas(tide_draw_list *draw, tide_list data, tide_list transforms,
                             uint32_t textures[2], int32_t info[4]);

// Omitted-pose draws use authored rest without creating a mutable Pose value.
// Current immutable-pack proof permits reuse of a frame-local owned capture;
// other model values retain complete checked rest and draw preparation.
bool tide_draw_skinned_rest_data(tide_draw_list *draw, tide_list data,
                                 uint32_t textures[2], int32_t info[3]);
bool tide_draw_skinned_rest_atlas(tide_draw_list *draw, tide_list data,
                                  uint32_t textures[2], int32_t info[4]);
