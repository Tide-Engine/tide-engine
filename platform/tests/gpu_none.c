// The GPU that draws nothing (src/gpu_none.c), which web pages that ask for `gpu=none` draw with: built here too,
// natively, where the sanitizers see its read backs. No window or GPU.

#include <stdlib.h>
#include <string.h>

#include "gpu.h"
#include "tide_test.h"

// More reads begun than it keeps at once, each of its own size, before any is polled: as a frame begins every band of
// every target read asked for (a volume of a million rows is 65 bands). It refuses what it can't keep (0, which the
// renderer asks again next frame), and each read it began gives its own size of zeros, never another's.
TIDE_TEST(gpu_none_reads_keep_their_own_sizes_however_many_begin)
{
    const tide_gpu *gpu = &tide_gpu_none;
    enum { ASKED = 300 };
    uint32_t tokens[ASKED];
    size_t sizes[ASKED];
    uint32_t begun = 0;
    for (uint32_t i = 0; i < ASKED; i++) {
        sizes[i] = i == 0 ? 4u : 4096u + 4u * i; // The first, the smallest, is a band's last few rows
        tokens[i] = gpu->buffer_read(1, 0, sizes[i]);
        begun += tokens[i] != 0;
    }
    TIDE_CHECK(begun >= 64 && begun < ASKED);
    for (uint32_t i = 0; i < ASKED; i++) {
        if (!tokens[i]) continue;
        for (uint32_t k = 0; k < i; k++) TIDE_CHECK(tokens[k] != tokens[i]);
        // Exactly its size, which the sanitizer holds it to, and marked past that
        uint8_t *data = malloc(sizes[i] + 1u);
        TIDE_REQUIRE(data != NULL);
        memset(data, 0xAB, sizes[i] + 1u);
        TIDE_CHECK(gpu->buffer_read_done(tokens[i], data) == 1);
        bool zeros = true;
        for (size_t b = 0; b < sizes[i]; b++) zeros = zeros && data[b] == 0;
        TIDE_CHECK(zeros && data[sizes[i]] == 0xAB);
        TIDE_CHECK(gpu->buffer_read_done(tokens[i], data) == -1); // ...once
        free(data);
    }
    // Every place is free again: the ones refused begin now
    for (uint32_t i = 0; i < ASKED - begun; i++) {
        const uint32_t token = gpu->buffer_read(1, 0, 8);
        TIDE_REQUIRE(token != 0);
        uint8_t data[8];
        TIDE_CHECK(gpu->buffer_read_done(token, data) == 1);
    }
    TIDE_CHECK(gpu->buffer_read_done(0, NULL) == -1);
}
