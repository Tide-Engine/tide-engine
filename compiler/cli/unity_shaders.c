#include "unity_shaders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hlsl.h"
#include "spirv.h"
#include "sys.h"

#define TIDE_SHADERS_WGSL 1 // The renderer's WGSL, which only its web build keeps
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable" // Its GLSL
#include "../../platform/src/shaders.h"
#pragma clang diagnostic pop

// FNV-1a's 64 bits.
static uint64_t fnv(const void *data, const size_t size)
{
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++) h = (h ^ ((const uint8_t *)data)[i]) * UINT64_C(1099511628211);
    return h;
}

uint64_t unity_shader_key(const char *wgsl)
{
    char error[256];
    size_t count = 0;
    uint32_t *words = tide_wgsl_to_spirv(wgsl, &count, error, sizeof error);
    if (!words) return 0;
    const uint64_t h = fnv(words, count * 4u); // As platform/src/gpu_record.c names a pipeline's shaders
    free(words);
    return h;
}

// The names of the files written, which the folder's others aren't.
typedef struct written {
    char (*names)[32];
    int count, capacity;
    char *keys; // A line for each: its WGSL's key, then its SPIR-V's (web-keys.txt)
    size_t keys_size;
} written;

static bool write_one(const char *dir, written *w, const char *name, const char *wgsl)
{
    const uint64_t key = unity_shader_key(wgsl);
    char error[256];
    char *hlsl = key ? tide_wgsl_to_hlsl(wgsl, error, sizeof error) : NULL;
    if (!hlsl) {
        fprintf(stderr, "tide: the %s shader's WGSL didn't become HLSL%s%s: this is a bug in tide\n", name, key ? ": " : "",
                key ? error : "");
        return false;
    }
    char file[32];
    snprintf(file, sizeof file, "%016llx.shader", (unsigned long long)key);
    for (int i = 0; i < w->count; i++) {
        if (strcmp(w->names[i], file) == 0) { // Two of the same shaders
            free(hlsl);
            return true;
        }
    }
    if (w->count == w->capacity) {
        w->capacity = w->capacity ? w->capacity * 2 : 32;
        w->names = realloc(w->names, sizeof *w->names * (size_t)w->capacity);
        if (!w->names) abort();
    }
    memcpy(w->names[w->count++], file, sizeof file);
    // Where the program has no SPIR-V (on the web), a pipeline is known by its WGSL's key: what the file is to it
    char line[64];
    const int line_size = snprintf(line, sizeof line, "%016llx %016llx\n", (unsigned long long)fnv(wgsl, strlen(wgsl)),
                                   (unsigned long long)key);
    w->keys = realloc(w->keys, w->keys_size + (size_t)line_size + 1u);
    if (!w->keys) abort();
    memcpy(w->keys + w->keys_size, line, (size_t)line_size + 1u);
    w->keys_size += (size_t)line_size;
    char title[64];
    snprintf(title, sizeof title, "Hidden/Tide/%016llx", (unsigned long long)key);
    char *shader = tide_hlsl_unity_shader(hlsl, title);
    free(hlsl);
    // Which shader it is, for whoever opens it
    const size_t length = strlen(shader) + strlen(name) + 64;
    char *text = malloc(length);
    if (!text) abort();
    snprintf(text, length, "// %s\n%s", name, shader);
    free(shader);
    char *path = path_join(dir, file);
    size_t had = 0;
    char *old = sys_read_file(path, &had);
    const size_t size = strlen(text);
    const bool same = old && had == size && memcmp(old, text, size) == 0;
    const bool ok = same || sys_write_file(path, text, size);
    if (!ok) fprintf(stderr, "tide: can't write %s\n", path);
    free(old);
    free(path);
    free(text);
    return ok;
}

typedef struct stale {
    const char *dir;
    const written *kept;
} stale;

// A shader of the folder's that wasn't written, and its meta, removed.
static void remove_stale(void *user, const char *name, const bool is_dir)
{
    const stale *s = user;
    if (is_dir) return;
    const char *shader = strstr(name, ".shader");
    if (!shader || (shader[7] != '\0' && strcmp(shader + 7, ".meta") != 0)) return;
    for (int i = 0; i < s->kept->count; i++) {
        if (strncmp(name, s->kept->names[i], strlen(s->kept->names[i])) == 0) return;
    }
    char *path = path_join(s->dir, name);
    sys_remove(path);
    free(path);
}

// The length of the text at `s`, to its NUL or `most`.
static size_t length_within(const char *s, const size_t most)
{
    size_t n = 0;
    while (n < most && s[n]) n++;
    return n;
}

bool unity_shaders_write(const char *dir, const char *game, const size_t size)
{
    char *resources = path_join(dir, "Resources/TideShaders");
    bool ok = sys_mkdirs(resources);
    if (!ok) fprintf(stderr, "tide: can't write %s\n", resources);
    written w = {0};
    // The renderer's own, each in one or two pieces
#define WRITE(name, first, second)                                                                                       \
    if (ok) {                                                                                                            \
        const size_t length = strlen(first) + (second ? strlen(second) : 0u);                                          \
        char *wgsl = malloc(length + 1u);                                                                              \
        if (!wgsl) abort();                                                                                            \
        strcpy(wgsl, first);                                                                                           \
        if (second) strcat(wgsl, second);                                                                              \
        ok = write_one(resources, &w, "the renderer's " #name, wgsl);                                                  \
        free(wgsl);                                                                                                    \
    }
    TIDE_RENDERER_WGSL(WRITE)
#undef WRITE
    // ...and the game's
    for (size_t at = 0; ok && at < size;) {
        const char *name = game + at;
        const size_t name_length = length_within(name, size - at);
        if (at + name_length + 1u >= size) break;
        const char *wgsl = name + name_length + 1u;
        const size_t wgsl_length = length_within(wgsl, size - at - name_length - 1u);
        ok = write_one(resources, &w, name, wgsl);
        at += name_length + 1u + wgsl_length + 1u;
    }
    if (ok) {
        char *path = path_join(resources, "web-keys.txt");
        size_t had = 0;
        char *old = sys_read_file(path, &had);
        const bool same = old && had == w.keys_size && memcmp(old, w.keys, w.keys_size) == 0;
        ok = same || sys_write_file(path, w.keys ? w.keys : "", w.keys_size);
        if (!ok) fprintf(stderr, "tide: can't write %s\n", path);
        free(old);
        free(path);
    }
    if (ok) {
        stale s = {resources, &w};
        sys_list(resources, remove_stale, &s);
    }
    free(w.keys);
    free(w.names);
    free(resources);
    return ok;
}
