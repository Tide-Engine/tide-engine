#pragma once

#include <stdbool.h>
#include <stdint.h>

// Private session wire codec, only over the recipient's permitted canonical
// bytes. Neither physical worlds nor private allocator/history state enter it.
uint8_t *tide_recipient_delta_pack(const uint8_t *now, uint32_t count,
    const uint8_t *base, uint32_t base_count, uint32_t *size);
bool tide_recipient_delta_unpack(uint8_t *out, uint32_t count,
    const uint8_t *base, uint32_t base_count, const uint8_t *data, uint32_t size);
