#pragma once

#include <stdbool.h>
#include <stdint.h>

// Private recipient-only repair negotiation. Larger projections fall back to
// the ordinary codec; this bound limits negotiation metadata, not world size.
#define TIDE_RECIPIENT_NEEDS_BYTES 512u
#define TIDE_RECIPIENT_BLOCK_BYTES 16384u

uint8_t *tide_recipient_hashes(const uint8_t *target, uint32_t count, uint32_t *size);
bool tide_recipient_needs(const uint8_t *candidate, uint32_t count,
    const uint8_t *hashes, uint32_t size, uint8_t *out, uint32_t capacity, uint32_t *needed);
uint8_t *tide_recipient_patch(const uint8_t *target, uint32_t count,
    const uint8_t *needs, uint32_t need_size, uint32_t *size);
// On failure leaves both output arguments unchanged. Successful bytes belong
// to the caller and have the target's complete canonical hash.
bool tide_recipient_patch_apply(const uint8_t *candidate, uint32_t count,
    const uint8_t *patch, uint32_t size, uint8_t **out, uint32_t *out_count);
