#pragma once
#include "tide/texture_blocks.h"

// Free blocks.pixels after packing. Small/non-power-of-two images stay exact.
tide_texture_blocks texture_compress(const uint8_t *rgba, int32_t width, int32_t height);
