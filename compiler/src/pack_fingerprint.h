#pragma once

#include "tide/pack.h"

// Private to tidec and its emitted asset loader. The raw pack does not say
// an ordinary map's value kind or a grid's cell/chunk shape.
#ifndef TIDE_ASSET_FINGERPRINT_DESC_DEFINED
#define TIDE_ASSET_FINGERPRINT_DESC_DEFINED
typedef struct tide_asset_fingerprint_desc {
    uint32_t kind, leaf_kind, vertex_size;
    tide_grid_shape grid_shape;
} tide_asset_fingerprint_desc;
#endif

// Hash one declaration's reachable logical content before loading a pack.
// Physical offsets, block padding and cached versions do not contribute.
bool pack_fingerprint(const uint8_t *bytes, size_t size, const char *qualified,
                      const tide_asset_fingerprint_desc *desc, uint8_t out[32]);

// The exact implementation above, embedded into generated C when needed.
// This array is source text, without a trailing NUL.
extern const unsigned char tide_pack_fingerprint_source[];
extern const unsigned int tide_pack_fingerprint_source_size;
