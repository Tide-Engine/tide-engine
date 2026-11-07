// A program with a log (tide_platform_log_begin, platform/src/log.c), which
// log.mjs runs: it says something on stderr, then ends as it's asked.
//
//   tide_platform_log <folder> end|crash|abort|out
//
// `end` returns from main, `crash` writes through a null pointer, `abort`
// calls abort(), as the engine's stops do, and `out` stops as running out of
// memory does (tide_memory_ran_out), without running out. Built with AddressSanitizer it
// says so first: the sanitizer catches a crash before the log can.

#include "tide/page.h"
#include "tide/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: tide_platform_log <folder> end|crash|abort|out\n");
        return 2;
    }
    tide_platform_log_begin("dev.tide.logtest", "Log Test", argv[1]);
    const char *path = tide_platform_log_path();
#if __has_feature(address_sanitizer)
    printf("sanitized\n"); // AddressSanitizer catches a crash first, and ends the program itself
#endif
    printf("log %s\n", path ? path : "(none)");
    fflush(stdout);
    fprintf(stderr, "said before the end: %s\n", argv[2]);
    if (strcmp(argv[2], "crash") == 0) {
        volatile int *volatile nowhere = NULL;
        *nowhere = 1;
    }
    if (strcmp(argv[2], "abort") == 0) abort();
    if (strcmp(argv[2], "out") == 0) tide_memory_ran_out("a test's 12 bytes", 1500);
    return 0;
}
