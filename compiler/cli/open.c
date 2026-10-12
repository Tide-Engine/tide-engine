#include "open.h"
#include "sys.h"
#include <stdio.h>
#include <stdlib.h>

int tide_open(const int argc, char **argv)
{
    char *bin = sys_exe_dir();
#ifdef _WIN32
    char *editor = path_join(bin, "tide-editor.exe");
#else
    char *editor = path_join(bin, "tide-editor");
#endif
    free(bin);
    if (!sys_exists(editor)) {
        fputs("tide: native scene editor is missing; install a desktop Tide package (or build tide_editor in this checkout)\n", stderr);
        free(editor); return 2;
    }
    const char **args = calloc((size_t)argc + 1, sizeof(*args));
    if (!args) abort();
    args[0] = editor;
    for (int i = 2; i < argc; i++) args[i - 1] = argv[i];
    const int code = sys_run(args, NULL, false);
    free(args); free(editor); return code < 0 ? 2 : code;
}
