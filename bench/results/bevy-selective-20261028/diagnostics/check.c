#include <stdio.h>
#include "mask.c"
static u32 scalar_extract(signs16 mask) {
    u32 bits = 0;
    for (u32 lane = 0; lane < 16; lane++) bits |= (u32)(mask[lane] != 0) << lane;
    return bits;
}
int main(void) {
    for (u32 pattern = 0; pattern <= 65535u; pattern++) {
        u32 other = (pattern * 40503u + 17777u) & 65535u;
        u8 p[16], q[16];
        for (u32 lane = 0; lane < 16; lane++) {
            p[lane] = pattern & (1u << lane) ? (lane % 3u == 0 ? 128u : lane % 3u == 1 ? 255u : 1u) : 0u;
            q[lane] = other & (1u << lane) ? 128u : 0u;
        }
        bytes16 v;
        __builtin_memcpy(&v, p, sizeof v);
        signs16 comparison = (signs16)(v != (bytes16){0});
        if (sparse_one(p) != pattern || sparse_portable(p) != pattern || scalar_extract(comparison) != pattern || sparse_two(p, q) != (pattern & ~other)) return 1;
    }
    puts("All 65536 presence masks match native, scalar, weighted and two-filter references.");
    return 0;
}
