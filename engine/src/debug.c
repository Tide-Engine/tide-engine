#include "tide/debug.h"

#include <stdio.h>
#include <stdlib.h>

// See tide/debug.h.

void tide_debug_log(const char *text)
{
    fprintf(stderr, "%s\n", text ? text : "");
    fflush(stderr);
}

void tide_debug_assert(const bool ok, const char *source, const char *file, const int line, const char *message)
{
    if (ok) return;
    fprintf(stderr, "tide: assertion failed at %s:%d: %s\n", file, line, source);
    if (message && message[0]) fprintf(stderr, "  %s\n", message);
    fflush(stderr);
    abort();
}
