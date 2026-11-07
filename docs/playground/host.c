#include "host.h"
#include "build_code.h"

#include <errno.h>

bool build_code_run(program *prog, const codegen_options *opts, bool *again)
{
    (void)opts;
    *again = false;
    if (!prog->build_roots.count) return true;
    diag_error(prog->build_roots.items[0]->at, "build code cannot run in the browser playground");
    diag_note("use tide build or tide run --web to build this game with the native tools");
    return false;
}

FILE *popen(const char *command, const char *mode)
{
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE *stream)
{
    (void)stream;
    errno = ENOSYS;
    return -1;
}
