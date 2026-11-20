// Vulkan's pipelines kept across runs (tide_gpu.pipeline_cache), a run at a
// time, as pipeline_cache.cmake runs this in its order, each in a hidden
// window of a game whose temporary cache folder is the script's:
//
//   pipeline_cache fresh     no file yet: nothing is read, and the run's pipelines are written as it ends
//   pipeline_cache warm      the last run's file is read
//   pipeline_cache corrupt   a byte of the file changed: it's left out, and written over as the run ends
//   pipeline_cache foreign   a whole file, for another device: left out too
//   pipeline_cache cut       the file cut short: left out too
//   pipeline_cache outgrown  the file bigger than one is read at: left out, and written over with the run's own
//
// Each draws what the renderer has pipelines for, and reads it back. A file
// holds the driver's pipelines after the two headers, and a warm run reads
// them, where the driver keeps them (tide_gpu_vk_pipeline_cache_keeps, asked
// of the driver apart from the device's own cache): a driver that keeps its
// header alone, as the Android emulator's does, has files of the headers.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "gpu_vk.h"
#include "tide/platform.h"

static const char *mode;

static char *cache_file(void)
{
    const char *folder = tide_platform_cache_folder();
    if (!folder) return NULL;
    const size_t length = strlen(folder) + 32u;
    char *file = malloc(length);
    snprintf(file, length, "%s/pipelines.vulkan", folder);
    return file;
}

// The header's hash, as gpu_vk.c makes it.
static uint64_t fnv(const uint8_t *data, const size_t size)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < size; i++) h = (h ^ data[i]) * UINT64_C(0x100000001b3);
    return h;
}

// The file as the mode has it before the run: false where it can't be.
static bool spoil(const char *file)
{
    if (strcmp(mode, "fresh") == 0) {
        remove(file);
        return true;
    }
    if (strcmp(mode, "warm") == 0 || strcmp(mode, "outgrown") == 0) return true;
    size_t size = 0;
    uint8_t *bytes = tide_file_read_whole(file, SIZE_MAX, &size);
    if (!bytes || size < 24u + 32u) {
        printf("FAIL: the last run left no file of pipelines to spoil (%zu bytes)\n", size);
        return false;
    }
    if (strcmp(mode, "corrupt") == 0) {
        bytes[24u + (size - 24u) / 2u] ^= 0x5Au;
    } else if (strcmp(mode, "foreign") == 0) {
        bytes[24u + 8u] ^= 0xFFu; // The driver's vendor, with the hash made again: whole, but not this device's
        const uint64_t hash = fnv(bytes + 24, size - 24u);
        memcpy(bytes + 16, &hash, 8);
    } else if (strcmp(mode, "cut") == 0) {
        size = 24u + (size - 24u) / 2u;
    } else {
        printf("FAIL: no mode is named '%s'\n", mode);
        free(bytes);
        return false;
    }
    const bool written = tide_file_write_whole(file, bytes, size);
    free(bytes);
    return written;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    tide_draw_list list = {0};
    tide_draw_clear(&list, TIDE_COLOR_BLUE);
    tide_draw_set_screen(&list); // In the window's pixels
    tide_draw_rect(&list, tide_f2(8, 8), tide_f2(8, 8), TIDE_COLOR_RED);
    tide_draw_round_rect(&list, tide_f2(24, 8), tide_f2(8, 8), 2.0f, TIDE_COLOR_GREEN);
    tide_draw_text(&list, "Tide", tide_f2(2, 20), 12, TIDE_COLOR_WHITE);
    const tide_float2 point = tide_f2(10, 10);
    uint32_t got = 0;
    tide_platform_read_pixels(&list, &point, 1, &got);
    tide_draw_free(&list);
    if (strcmp(tide_platform_renderer(), "Vulkan") != 0) {
        printf("FAIL: the window draws with %s, where this test is Vulkan's\n", tide_platform_renderer());
        return 1;
    }
    const size_t read = tide_gpu_vk_pipeline_cache_read();
    const bool wants = strcmp(mode, "warm") == 0;
    // What a file holds past Tide's header (24 bytes) and the driver's (32): the pipelines, where the driver keeps
    // them. One that keeps none, as the Android emulator's, is said as such, and its files are the two headers alone
    const bool keeps = tide_gpu_vk_pipeline_cache_keeps();
    printf("pipeline cache: this device's driver keeps %s\n", keeps ? "its pipelines" : "no pipelines, only its 32-byte header");
    const size_t headers = 24u + 32u;
    if (strcmp(mode, "outgrown") == 0) { // A whole file of this device's, which a warm run would read
        char *file = cache_file();
        size_t size = 0;
        uint8_t *bytes = file ? tide_file_read_whole(file, SIZE_MAX, &size) : NULL;
        const bool whole = keeps ? size > headers : size == headers;
        printf("%s: the file left out is one a run would read (%zu bytes, where %s)\n", whole ? "ok" : "FAIL", size,
               keeps ? "the driver's pipelines follow the headers" : "the driver keeps its header alone");
        free(bytes);
        free(file);
        if (!whole) return 1;
    }
    // A warm run takes the driver's pipelines, not its header alone, where it keeps them
    const bool took = wants ? read > (keeps ? 32u : 0u) : read == 0;
    const bool drew = got == 0xFF0000FFu;
    printf("%s: a %s run read %zu bytes of pipelines, where it reads %s\n", took ? "ok" : "FAIL", mode, read,
           !wants ? "none" : keeps ? "more than the driver's header" : "the driver's header");
    printf("%s: and drew with them (0x%08X)\n", drew ? "ok" : "FAIL", got);
    return took && drew ? 0 : 1;
}

int main(const int argc, char **argv)
{
    mode = argc > 1 ? argv[1] : "";
    tide_platform_io_init("dev.tide.test.pipeline_cache", NULL);
    char *file = cache_file();
    if (!file) {
        printf("FAIL: the game has no temporary cache folder\n");
        return 1;
    }
    bool ready = spoil(file);
    if (ready && strcmp(mode, "outgrown") == 0) {
        // A byte less than the driver's data in the file the last run read: whatever its driver keeps. A desktop's
        // keeps kilobytes, the Android emulator's no more than the 32 bytes of its header
        size_t size = 0;
        uint8_t *bytes = tide_file_read_whole(file, SIZE_MAX, &size);
        free(bytes);
        ready = size >= 24u + 32u;
        if (ready) tide_gpu_vk_pipeline_cache_outgrown(size - 24u - 1u);
        else printf("FAIL: the last run left no file of pipelines to outgrow (%zu bytes)\n", size);
    }
    // What the run starts from, said for a failure to be read by: no file, one of the headers alone, or more
    if (ready) {
        size_t size = 0;
        uint8_t *bytes = tide_file_read_whole(file, SIZE_MAX, &size);
        printf("pipeline cache: the file before this run is %s (%zu bytes)", bytes ? "there" : "not there", size);
        for (size_t i = 0; bytes && i < size && i < 24u + 32u; i++) printf("%s%02X", i % 8u ? "" : " ", bytes[i]);
        printf("\n");
        free(bytes);
    }
    free(file);
    if (!ready) return 1;
    tide_platform_open(&(tide_window_desc){.title = "pipeline cache", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
