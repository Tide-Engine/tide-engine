// A WGSL file through tidec's translators (src/spirv.c, src/hlsl.c), for the
// wgsl_* tests (expect_wgsl.cmake), which check what they write with spirv-val
// and fxc:
//
//     tide_wgsl_translate --spv <in.wgsl> <out.spv>
//     tide_wgsl_translate --hlsl <in.wgsl> <out.hlsl>   and beside it Unity's
//                                                       <out.hlsl>.compute or .shader
//     tide_wgsl_translate --refuse <in.wgsl> <text>     both refuse it, saying <text>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hlsl.h"
#include "spirv.h"

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *text = NULL;
    size_t size = 0;
    for (;;) {
        char *grown = realloc(text, size + 4097);
        if (!grown) abort();
        text = grown;
        const size_t got = fread(text + size, 1, 4096, file);
        size += got;
        if (got < 4096) break;
    }
    fclose(file);
    text[size] = '\0';
    return text;
}

static int write_file(const char *path, const void *data, const size_t size)
{
    FILE *file = fopen(path, "wb");
    const int written = file && fwrite(data, 1, size, file) == size;
    if (file && fclose(file) != 0) return 0;
    if (!written) fprintf(stderr, "tide_wgsl_translate: can't write %s\n", path);
    return written;
}

int main(const int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: tide_wgsl_translate --spv|--hlsl|--refuse <in.wgsl> <out or text>\n");
        return 2;
    }
    char *wgsl = read_file(argv[2]);
    if (!wgsl) {
        fprintf(stderr, "tide_wgsl_translate: can't read %s\n", argv[2]);
        return 1;
    }
    char error[256];
    int ok = 0;
    if (strcmp(argv[1], "--spv") == 0) {
        size_t count = 0;
        uint32_t *words = tide_wgsl_to_spirv(wgsl, &count, error, sizeof error);
        if (!words) fprintf(stderr, "tide_wgsl_translate: %s didn't become SPIR-V: %s\n", argv[2], error);
        else ok = write_file(argv[3], words, count * sizeof(uint32_t));
        free(words);
    } else if (strcmp(argv[1], "--hlsl") == 0) {
        char *hlsl = tide_wgsl_to_hlsl(wgsl, error, sizeof error);
        if (!hlsl) {
            fprintf(stderr, "tide_wgsl_translate: %s didn't become HLSL: %s\n", argv[2], error);
        } else {
            ok = write_file(argv[3], hlsl, strlen(hlsl));
            const int compute = strstr(wgsl, "@compute") != NULL;
            char *unity = compute ? tide_hlsl_unity_compute(hlsl) : tide_hlsl_unity_shader(hlsl, "Hidden/Tide/Test");
            char path[1024];
            snprintf(path, sizeof path, "%s.%s", argv[3], compute ? "compute" : "shader");
            ok = ok && write_file(path, unity, strlen(unity));
            free(unity);
        }
        free(hlsl);
    } else if (strcmp(argv[1], "--refuse") == 0) {
        size_t count = 0;
        uint32_t *words = tide_wgsl_to_spirv(wgsl, &count, error, sizeof error);
        ok = 1;
        if (words || !strstr(error, argv[3])) {
            fprintf(stderr, "tide_wgsl_translate: SPIR-V %s %s, not refusing it for '%s'\n", words ? "took" : "refused", argv[2], argv[3]);
            if (!words) fprintf(stderr, "    %s\n", error);
            ok = 0;
        }
        free(words);
        char *hlsl = tide_wgsl_to_hlsl(wgsl, error, sizeof error);
        if (hlsl || !strstr(error, argv[3])) {
            fprintf(stderr, "tide_wgsl_translate: HLSL %s %s, not refusing it for '%s'\n", hlsl ? "took" : "refused", argv[2], argv[3]);
            if (!hlsl) fprintf(stderr, "    %s\n", error);
            ok = 0;
        }
        free(hlsl);
    } else {
        fprintf(stderr, "tide_wgsl_translate: %s isn't --spv, --hlsl or --refuse\n", argv[1]);
    }
    free(wgsl);
    return ok ? 0 : 1;
}
