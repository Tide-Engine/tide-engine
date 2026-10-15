#include <stdio.h>
#include <string.h>

const char *InsideOutInviteBase(void);

int main(const int argc, char **const argv)
{
    if (argc < 2) return 2;
    const char *const base = InsideOutInviteBase();
    if (strcmp(base, argv[1]) != 0) {
        fprintf(stderr, "invite base: expected %s, got %s\n", argv[1], base);
        return 1;
    }
    // The cached result must remain valid on later clicks.
    if (strcmp(InsideOutInviteBase(), argv[1]) != 0) return 1;
    return 0;
}
