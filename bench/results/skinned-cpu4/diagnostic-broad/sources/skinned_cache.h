#pragma once

#include "tide/draw.h"

#define TIDE_SKINNED_ATLAS_KIND UINT64_C(0x534b494e41544c53)

// Draw-list-owned palette memoization never outlives the frame's slots.
void tide_skinned_draw_cache_reset(tide_draw_list *draw);
void tide_skinned_draw_cache_free(tide_draw_list *draw);
