// Controlled compiler/linker for build scheduling tests. Linked outputs are
// copies of this fixture, which exit without loading a game or opening devices.
#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys.h"

int main(int argc, char **argv)
{
    const char *output = NULL, *source = NULL;
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) output = argv[i + 1];
        if (strcmp(argv[i], "-c") == 0) source = argv[i + 1];
    }
    if (sys_env("TIDE_TEST_BUILD_REQUIRE_THIN")) {
        bool thin = false, cache = false, jobs = false;
        for (int i = 1; i < argc; i++) {
            thin = thin || strcmp(argv[i], "-flto=thin") == 0;
            cache = cache || strncmp(argv[i], "--thinlto-cache-dir=", 20) == 0 || strcmp(argv[i], "-cache_path_lto") == 0;
            jobs = jobs || strncmp(argv[i], "--thinlto-jobs=", 15) == 0 || strncmp(argv[i], "-threads=", 9) == 0;
        }
        if (source && (strcmp(path_base(source), "game.c") == 0 || strstr(path_base(source), "_part")) && !thin) return 8;
        if (!source && output && (!thin || !cache || !jobs)) return 9;
    }
    if (!output) return 0; // The fake host, or a compiler discovery probe
    const char *symbol = sys_env("TIDE_TEST_BUILD_DEFINE");
    if (source && symbol) {
        bool found = false;
        for (int i = 1; i + 1 < argc; i++) {
            if (strcmp(argv[i], "-D") == 0 && strcmp(argv[i + 1], "BATTLE_TEST") == 0) found = true;
        }
        const bool authored = strncmp(path_base(source), "custom.", 7) == 0;
        if (found != (authored && strcmp(symbol, "on") == 0)) return 10;
    }
    const char *events = sys_env("TIDE_TEST_BUILD_EVENTS");
    if (!events) return 2;
    if (source) {
        const char *name = path_base(source);
        char *started = path_join(events, name);
        char pid[32];
        snprintf(pid, sizeof pid, "%u", (unsigned)sys_pid());
        if (!sys_write_text(started, pid)) return 2;
        free(started);
        const char *gate = sys_env("TIDE_TEST_BUILD_GATE");
        const char *gated = sys_env("TIDE_TEST_BUILD_GATE_SOURCE");
        if (gate && strcmp(name, gated ? gated : "engine.c") == 0) {
            const int64_t end = sys_now_ms() + 8000;
            while (!sys_exists(gate)) {
                if (sys_now_ms() >= end) { fputs("compiler gate timed out\n", stderr); return 3; }
                sys_sleep_ms(5);
            }
        }
        const char *fail = sys_env("TIDE_TEST_BUILD_FAIL");
        if (fail && strstr(fail, name)) {
            fprintf(stderr, "fixture compile error: %s\n", name);
            return 7;
        }
        return sys_write_text(output, source) ? 0 : 2;
    }
    char *bin = sys_exe_dir();
    char *self = path_join(bin, "clang"
#ifdef _WIN32
                           ".exe"
#endif
    );
    size_t size;
    char *bytes = sys_read_file(self, &size);
    const bool ok = bytes && sys_write_file(output, bytes, size);
    free(bytes);
    free(self);
    free(bin);
#ifndef _WIN32
    // A copied executable must retain its execute bit for the fake native host.
    if (ok) {
        const char *args[] = {"chmod", "+x", output, NULL};
        if (sys_run(args, NULL, false) != 0) return 2;
    }
#endif
    return ok ? 0 : 2;
}
