// Random bits from the system (tide_platform_entropy), for a match's seed.

#include "tide/platform.h"

#if defined(__wasm__)

#include <wasi/api.h>

// WASI asks the host: the page (tide.js) answers with the browser's
// crypto.getRandomValues, and Node's tests with its own.
uint64_t tide_platform_entropy(void)
{
    uint64_t bits = 0;
    return __wasi_random_get((uint8_t *)&bits, sizeof bits) == 0 ? bits : 0;
}

#else

#include "rtc/rtc.h"

// The generator rooms' keys and WebRTC's come from.
uint64_t tide_platform_entropy(void)
{
    uint64_t bits = 0;
    return rtc_random(&bits, sizeof bits) ? bits : 0;
}

#endif
