#pragma once

#include "tide/retarget.h"

// Shared cold-path support for imported clips and future bake-on-load callers.
// This is not a generated-C/Tide API. Baked bytes use the existing canonical
// skinned format and ordinary sampling; there is no per-frame retarget work.
#define TIDE_RETARGET_BAKE_VERSION 2u
#define TIDE_RETARGET_BAKE_DEFAULT_RATE 60.0f

// Limits: bound requested work before allocating/iterating over it. Counts
// include every selected clip, even when its channels later compress away.
#define TIDE_RETARGET_BAKE_MAX_SAMPLES (1u << 20)
// Source plus target node transforms evaluated over all selected samples.
#define TIDE_RETARGET_BAKE_MAX_TRANSFORMS (1u << 22)
// Owned input sampling currently validates its keys again for each sample.
// Bound that cold-path work as well as the number of produced transforms.
#define TIDE_RETARGET_BAKE_MAX_KEY_READS (1u << 30)
// Encoded output and the accumulated key payload; canonical limits also apply.
#define TIDE_RETARGET_BAKE_MAX_BYTES (128u << 20)

typedef struct tide_retarget_bake_settings {
    float root_scale;
    tide_float3 root_offset;
    float heading_degrees, spine_distribution, twist_distribution;
    float sample_rate; // 0 means the default; otherwise finite and positive.
} tide_retarget_bake_settings;

static inline tide_retarget_bake_settings tide_retarget_bake_defaults(float root_scale)
{
    return (tide_retarget_bake_settings){root_scale, {0, 0, 0}, 0, 1, 1, TIDE_RETARGET_BAKE_DEFAULT_RATE};
}

// Bake clip_index, or all source clips when -1, using tide_retarget_apply.
// Retains target names/order/rest/hash and source clip names/durations. Output
// has no geometry, skins, shapes or unrelated source data. Sampling includes
// the regular grid, authored keys, endpoints and adjacent float times around
// STEP changes. Between bake samples ordinary interpolation is an approximation.
// Opposite quaternion signs are made continuous. Rest-equal constant channels
// disappear; other constant channels retain one key.
// Inputs are borrowed only for this call. On success *out is free-compatible
// (tide_skinned_data_free), independent of all inputs. On failure outputs stay
// unchanged and why (optional, 256 bytes) explains the refusal. Scratch and
// world state are unchanged on either path; allocation exhaustion follows the
// engine's normal allocation-failure policy.
bool tide_retarget_bake(tide_list source_data, int32_t clip_index,
                        tide_list target_data, tide_list plan,
                        const tide_retarget_bake_settings *settings,
                        uint8_t **out, uint32_t *out_size, char why[256]);

// The same target-preserving encoder, without clips: strips geometry and
// animation from a profile's reference asset. Same ownership/error contract.
bool tide_retarget_bake_skeleton(tide_list data, uint8_t **out,
                                 uint32_t *out_size, char why[256]);
