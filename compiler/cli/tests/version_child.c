#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        char path[2048], version[128];
        snprintf(path, sizeof path, "%s", argv[0]);
        char *slash = strrchr(path, '/');
        if (!slash) slash = strrchr(path, '\\');
        if (!slash) return 1;
        *slash = '\0';
        const size_t length = strlen(path);
        snprintf(path + length, sizeof path - length, "/../VERSION");
        FILE *file = fopen(path, "r");
        if (!file || !fgets(version, sizeof version, file)) return 1;
        fclose(file);
        version[strcspn(version, "\r\n")] = '\0';
        printf("tide %s (fixture)\n", version);
        return 0;
    }
    for (int i = 1; i < argc; i++) printf("arg: %s\n", argv[i]);
    fflush(stdout);
    for (int i = 1; i < argc; i++) if (strcmp(argv[i], "--hold") == 0) (void)getchar();
    return 23;
}
