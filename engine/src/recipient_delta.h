#pragma once

#include <stdbool.h>
#include <stdint.h>

// Private session wire codec, only over the recipient's permitted canonical
// bytes. Neither physical worlds nor private allocator/history state enter it.
uint8_t *tide_recipient_delta_pack(const uint8_t *now, uint32_t count,
    const uint8_t *base, uint32_t base_count, uint32_t *size);
// The same bytes, with the room its index of spans takes given by the caller (`index`,
// TIDE_RECIPIENT_INDEX_BYTES, whatever it holds): room used again, where each pack took
// half a megabyte fresh from the system, which several threads packing at once wait on.
#define TIDE_RECIPIENT_INDEX_BYTES (2u * 65536u * 4u)
uint8_t *tide_recipient_delta_pack_with(const uint8_t *now, uint32_t count,
    const uint8_t *base, uint32_t base_count, uint32_t *size, uint32_t *index);
bool tide_recipient_delta_unpack(uint8_t *out, uint32_t count,
    const uint8_t *base, uint32_t base_count, const uint8_t *data, uint32_t size);
