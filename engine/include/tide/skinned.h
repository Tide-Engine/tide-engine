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
