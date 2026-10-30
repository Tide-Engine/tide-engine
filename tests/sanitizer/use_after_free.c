#include <stdlib.h>
#include <stdio.h>

/* Deliberately invalid: the CMake harness requires ASan's use-after-free
 * diagnostic and a failing exit. A volatile read survives optimization. */
int main(void) {
    fputs("sanitizer probe: expected compiler diagnostic\n", stderr);
    char *allocation = malloc(8);
    if (!allocation) return 2;
    allocation[0] = 42;
    volatile char *expired = allocation;
    free(allocation);
    return expired[0];
}
