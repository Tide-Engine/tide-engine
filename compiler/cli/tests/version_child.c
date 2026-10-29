#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("tide 9.8.7 (fixture)");
        return 0;
    }
    for (int i = 1; i < argc; i++) printf("arg: %s\n", argv[i]);
    return 23;
}
