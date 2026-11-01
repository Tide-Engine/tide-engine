#include "tide/pack.h"

#include <stdint.h>
#include <string.h>

// Preserve the complete generation bits through Tide's signed long ABI.
// No pointer, geometry byte or external state is retained by this bridge.
int64_t GIAssetGeneration(void)
{
    const uint64_t generation = tide_pack_generation();
    int64_t result;
    memcpy(&result, &generation, sizeof result);
    return result;
}
