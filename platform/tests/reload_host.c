#include "tide/host.h"

#ifdef TIDE_RELOAD_PLAIN
const tide_host_game *tide_host_library(void);
#endif

int main(int argc, char **argv)
{
#ifdef TIDE_RELOAD_PLAIN
    tide_host_run(&(tide_run_desc){.argc = argc, .argv = argv}, tide_host_library());
#else
    if (argc < 2) return 2;
    // Which tide built this host, where the test says one: libraries another built aren't loaded
    tide_host_run_library(&(tide_run_desc){.argc = argc - 1, .argv = argv + 1, .made_by = getenv("TIDE_RELOAD_MADE_BY")},
                          argv[1]);
#endif
}
