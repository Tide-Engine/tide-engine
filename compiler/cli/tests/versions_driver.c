#include <stdio.h>
#include "versions.h"
#include "telemetry.h"

// Exercise the real dispatcher and installer even in an engine development build.
void telemetry_fail(telemetry_error error) { (void)error; }
bool tide_editors_have_tide(void) { return false; }

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const int code = tide_use_pinned(argv[1], argc - 1, argv + 1, "1.0.0");
    if (code >= 0) return code;
    puts("using invoking tide");
    return 0;
}
