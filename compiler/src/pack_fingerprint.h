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

// Names are unique within a batch. The requests and expected hashes remain
// caller-owned; the helper releases its lookup scratch before returning.
#ifndef TIDE_ASSET_FINGERPRINT_REQUEST_DEFINED
#define TIDE_ASSET_FINGERPRINT_REQUEST_DEFINED
typedef struct tide_asset_fingerprint_request {
    const char *qualified;
    tide_asset_fingerprint_desc desc;
    const uint8_t *id; // Optional SHA-256 name ID instead of qualified; exactly one must be supplied.
} tide_asset_fingerprint_request;
#endif

// With a nonempty batch, give exactly one of out or expected: produce hashes
// in request order, or compare them before loading. failed is optional and
// receives the first failing request, or UINT32_MAX for a pack/header error.
// A zero-count batch only validates the pack header and needs neither array.
bool pack_fingerprints(const uint8_t *bytes, size_t size, const tide_asset_fingerprint_request *requests,
                       uint32_t count, uint8_t (*out)[32], const uint8_t (*expected)[32], uint32_t *failed);

// Hash one declaration's reachable logical content before loading a pack.
// Physical offsets, block padding and cached versions do not contribute.
bool pack_fingerprint(const uint8_t *bytes, size_t size, const char *qualified,
                      const tide_asset_fingerprint_desc *desc, uint8_t out[32]);

// The exact implementation above, embedded into generated C when needed.
// This array is source text, without a trailing NUL.
extern const unsigned char tide_pack_fingerprint_source[];
extern const unsigned int tide_pack_fingerprint_source_size;
