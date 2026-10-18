#include <stdio.h>
#include <string.h>

const char *InsideOutInviteBase(void);
int InsideOutPreviewLook(void);
int InsideOutPlaytest(void);

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
    for (int i = 2; i < argc; i++) {
        static const char playtest[] = "--expect-playtest=";
        if (strncmp(argv[i], playtest, sizeof playtest - 1) == 0) {
            const int expected = argv[i][sizeof playtest - 1] == '1';
            if (InsideOutPlaytest() != expected) {
                fprintf(stderr, "playtest: expected %d, got %d\n", expected, InsideOutPlaytest());
                return 1;
            }
            continue;
        }
        static const char prefix[] = "--expect-look=";
        if (strncmp(argv[i], prefix, sizeof prefix - 1) != 0) continue;
        const char *const expected = argv[i] + sizeof prefix - 1;
        const int look = strcmp(expected, "-1") == 0 ? -1 : expected[0] - '0';
        if (InsideOutPreviewLook() != look || InsideOutPreviewLook() != look) {
            fprintf(stderr, "preview look: expected %d, got %d\n", look, InsideOutPreviewLook());
            return 1;
        }
    }
    return 0;
}
