// The renderer's own shaders (src/shaders.h) as SPIR-V, for its Vulkan
// backend: their WGSL through tidec's translator (compiler/src/spirv.c), into
// a C file of arrays the platform layer's desktop builds compile, which
// src/shaders_spirv.h declares. The platform's build runs it:
//
//     tide_spirv_shaders <file.c>
//     tide_spirv_shaders --spv <dir>    each as <dir>/<name>.spv, for spirv-val
//                                       (tide_spirv_shaders_valid)
//     tide_spirv_shaders --hlsl <dir>   each as HLSL, Unity's (compiler/src/hlsl.c),
//                                       as <dir>/<name>.hlsl, for fxc (tide_hlsl_shaders_valid)

#define TIDE_SHADERS_WGSL 1 // shaders.h's WGSL, which it has for the web otherwise

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hlsl.h"
#include "spirv.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable" // The GLSL, which isn't this tool's
#include "shaders.h"
#pragma clang diagnostic pop

static const char *spv_dir;  // Where --spv writes them, or NULL for the C file
static const char *hlsl_dir; // ...or where --hlsl writes their HLSL

static bool write(FILE *out, const char *name, const char *first, const char *second)
{
    // Some are in two pieces, as the renderer joins them: together they're longer than C promises a string literal can be
    const size_t length = strlen(first) + (second ? strlen(second) : 0u);
    char *wgsl = malloc(length + 1u);
    if (!wgsl) return false;
    strcpy(wgsl, first);
    if (second) strcat(wgsl, second);
    char error[256];
    if (hlsl_dir) {
        char *hlsl = tide_wgsl_to_hlsl(wgsl, error, sizeof error);
        free(wgsl);
        if (!hlsl) {
            fprintf(stderr, "tide_spirv_shaders: the %s shader's WGSL didn't become HLSL: %s\n", name, error);
            return false;
        }
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.hlsl", hlsl_dir, name);
        FILE *file = fopen(path, "wb");
        bool written = file && fputs(hlsl, file) >= 0;
        if (file && fclose(file) != 0) written = false;
        if (!written) fprintf(stderr, "tide_spirv_shaders: can't write %s\n", path);
        free(hlsl);
        return written;
    }
    size_t count = 0;
    uint32_t *words = tide_wgsl_to_spirv(wgsl, &count, error, sizeof error);
    free(wgsl);
    if (!words) {
        fprintf(stderr, "tide_spirv_shaders: the %s shader's WGSL didn't become SPIR-V: %s\n", name, error);
        return false;
    }
    bool written = true;
    if (spv_dir) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s.spv", spv_dir, name);
        FILE *file = fopen(path, "wb");
        written = file && fwrite(words, sizeof(uint32_t), count, file) == count;
        if (file && fclose(file) != 0) written = false;
        if (!written) fprintf(stderr, "tide_spirv_shaders: can't write %s\n", path);
    } else {
        fprintf(out, "const uint32_t tide_spirv_%s[%u] = {", name, (unsigned)count);
        for (size_t i = 0; i < count; i++) fprintf(out, "%s0x%08x,", i % 10 == 0 ? "\n    " : "", (unsigned)words[i]);
        fprintf(out, "\n};\nconst uint32_t tide_spirv_%s_words = %u;\n\n", name, (unsigned)count);
    }
    free(words);
    return written;
}

static bool write_all(FILE *out)
{
#define WRITE(name, first, second) &&write(out, #name, first, second)
    return true TIDE_RENDERER_WGSL(WRITE);
#undef WRITE
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--spv") == 0) {
        spv_dir = argv[2];
        return write_all(NULL) ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "--hlsl") == 0) {
        hlsl_dir = argv[2];
        return write_all(NULL) ? 0 : 1;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: tide_spirv_shaders <file.c>\n       tide_spirv_shaders --spv <dir>\n"
                        "       tide_spirv_shaders --hlsl <dir>\n");
        return 2;
    }
    FILE *out = fopen(argv[1], "wb");
    if (!out) {
        fprintf(stderr, "tide_spirv_shaders: can't write %s\n", argv[1]);
        return 1;
    }
    fprintf(out, "// The renderer's shaders as SPIR-V, made by tide_spirv_shaders from platform/src/shaders.h\n\n"
                 "#include \"shaders_spirv.h\"\n\n");
    const bool ok = write_all(out);
    if (fclose(out) != 0 || !ok) {
        remove(argv[1]);
        return 1;
    }
    return 0;
}
