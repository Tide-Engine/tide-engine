#include "pack_fingerprint.h"

#include "pack_fingerprint.inc"

bool pack_fingerprints(const uint8_t *bytes, const size_t size, const tide_asset_fingerprint_request *requests,
                       const uint32_t count, uint8_t (*out)[32], const uint8_t (*expected)[32], uint32_t *failed)
{
    return tide_asset_fingerprints(bytes, size, requests, count, out, expected, failed);
}

bool pack_fingerprint(const uint8_t *bytes, const size_t size, const char *qualified,
                      const tide_asset_fingerprint_desc *desc, uint8_t out[32])
{
    if (!qualified || !desc || !out) return false;
    const tide_asset_fingerprint_request request = {.qualified = qualified, .desc = *desc};
    return tide_asset_fingerprints(bytes, size, &request, 1u, (uint8_t (*)[32])out, NULL, NULL);
}
