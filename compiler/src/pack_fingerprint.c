#include "pack_fingerprint.h"

#include "pack_fingerprint.inc"

bool pack_fingerprint(const uint8_t *bytes, const size_t size, const char *qualified,
                      const tide_asset_fingerprint_desc *desc, uint8_t out[32])
{
    return tide_asset_fingerprint(bytes, size, qualified, desc, out);
}
