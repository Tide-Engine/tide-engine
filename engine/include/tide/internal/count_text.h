#pragma once

// A count as a person reads one, its thousands apart (16,777,216), for the
// messages of a limit a file is past: the engine's decoders and tidec's
// importers name their limits that way (see the Limits blocks of tide/png.h,
// tide/skinned_data.h and the others).

#include <stdint.h>
#include <stdio.h>

static inline const char *tide_count_text(const uint64_t n, char out[32])
{
    char digits[24];
    const int count = snprintf(digits, sizeof digits, "%llu", (unsigned long long)n);
    int at = 0;
    for (int i = 0; i < count; i++) {
        if (i && (count - i) % 3 == 0) out[at++] = ',';
        out[at++] = digits[i];
    }
    out[at] = 0;
    return out;
}
