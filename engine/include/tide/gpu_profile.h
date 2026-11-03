#pragma once

#include <stdint.h>

// Diagnostic render-pass identity. Shaders lists the distinct pipeline labels
// separated by newlines; it attributes the whole pass, not individual shaders.
// Strings are borrowed only for the duration of the collection callback.
#define TIDE_GPU_PROFILE_PENDING 1024u
#define TIDE_GPU_PROFILE_ROWS 1024u
#define TIDE_GPU_PROFILE_NAME_BYTES 4096u

typedef struct tide_gpu_pass_sample {
    uint64_t target, frame;
    uint32_t pass, width, height;
    uint64_t draw_calls, vertices;
    const char *shaders;
    double ms;
} tide_gpu_pass_sample;

typedef void (*tide_gpu_pass_collect)(void *user, const tide_gpu_pass_sample *sample);
