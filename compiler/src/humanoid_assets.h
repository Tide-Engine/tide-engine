#pragma once

#include "asset_files.h"
#include "tide/retarget_bake.h"

// Compiler-owned profile storage. Offsets name aligned list headers inside
// asset_match.skinned, a private packed byte payload authenticated as a whole.
typedef struct humanoid_asset {
    uint32_t data_at, pose_at, bones_at, status_at;
    const uint8_t *data;
    uint32_t size, nodes;
    const tide_bone_transform *pose;
    const int32_t *bones, *status;
} humanoid_asset;

bool humanoid_asset_read(asset_match *item, loc at, const char *game_folder);
bool humanoid_asset_bake(asset_match *item, int32_t clip,
                          const asset_match *source, const asset_match *target,
                          tide_retarget_bake_settings settings, bool automatic_scale,
                          loc at, const char *game_folder);
