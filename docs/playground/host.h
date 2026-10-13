#pragma once

// A docs example has source and bundled files in a virtual filesystem. The
// compiler's optional native URL fetcher cannot start curl in a browser.
#include <stdio.h>
FILE *popen(const char *command, const char *mode);
int pclose(FILE *stream);
