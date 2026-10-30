#include "versions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define stdin_is_terminal() _isatty(_fileno(stdin))
#define TIDE_PROGRAM "bin/tide.exe"
#else
#include <unistd.h>
#define stdin_is_terminal() isatty(0)
#define TIDE_PROGRAM "bin/tide"
#endif

#include "install.h"
#include "packages.h"
#include "release.h"
#include "sys.h"
#include "upgrade.h"

static char *versions_root(void)
{
    const char *custom = sys_env("TIDE_VERSIONS");
    if (custom) return path_absolute(custom);
#ifdef _WIN32
    const char *home = sys_env("LOCALAPPDATA");
    return home ? path_join(home, "Tide/versions") : NULL;
#else
    const char *home = sys_env("HOME");
    return home ? path_join(home, ".tide/versions") : NULL;
#endif
}

int tide_use_pinned(const char *folder, const int argc, char **argv, const char *version)
{
    // Engine development and Unity's package-selected toolchain keep their
    // existing version rules. Pin itself must run with the invoking executable.
    if (tide_version_dev(version)) return -1;
    char *unity = packages_unity_game(folder);
    if (unity) { free(unity); return -1; }
    packages_file game;
    const bool read = packages_read(folder, &game, NULL, NULL);
    if (!read || game.name || !game.pinned || strcmp(game.pinned, version) == 0) {
        packages_free_file(&game);
        return -1;
    }
    char *cache = versions_root();
    if (!cache) {
        fputs("tide: can't find the versions folder; set TIDE_VERSIONS\n", stderr);
        packages_free_file(&game);
        return 1;
    }
    char *root = path_join(cache, game.pinned);
    char *program = path_join(root, TIDE_PROGRAM);
    int code = 1;
    if (!tide_installation_whole(root, game.pinned) || !sys_exists(program)) {
        fprintf(stderr, "tide: this game needs tide %s, which isn't installed in %s.\n", game.pinned, cache);
        if (!stdin_is_terminal() && !sys_env("TIDE_INTERACTIVE")) {
            fputs("  = note: run this command in a terminal to approve the download, or set TIDE_INTERACTIVE=1 to answer through stdin\n", stderr);
            goto done;
        }
        fprintf(stderr, "Download tide %s there? [y/N] ", game.pinned);
        fflush(stderr);
        char answer[32];
        if (!fgets(answer, sizeof answer, stdin) || (answer[0] != 'y' && answer[0] != 'Y')) {
            fputs("tide: nothing was downloaded\n", stderr);
            goto done;
        }
        if (!tide_install_version(root, game.pinned)) goto done;
    }
    // Check the executable as well as VERSION: a damaged cache must not dispatch
    // recursively, or run another version under the expected folder's name.
    const char *probe[] = {program, "--version", NULL};
    char said[256], prefix[128];
    snprintf(prefix, sizeof prefix, "tide %s (", game.pinned);
    if (sys_capture(probe, said, sizeof said) != 0 || strncmp(said, prefix, strlen(prefix)) != 0) {
        fprintf(stderr, "tide: %s isn't tide %s; repair that cached installation\n", program, game.pinned);
        goto done;
    }
    const char **args = calloc((size_t)argc + 1, sizeof *args);
    if (!args) abort();
    args[0] = program;
    for (int i = 1; i < argc; i++) args[i] = argv[i];
    code = sys_run(args, NULL, false);
    if (code < 0) code = 1;
    free(args);
done:
    free(program);
    free(root);
    free(cache);
    packages_free_file(&game);
    return code;
}
