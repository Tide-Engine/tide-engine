#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif

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


// Usage locks live outside installations, so replacement/deletion never changes
// their identity. Readers share them for the complete delegated command.
static char *cache_file(const char *cache, const char *pin, const char *suffix)
{
    char name[192];
    snprintf(name, sizeof name, ".%s.%s", pin, suffix);
    return path_join(cache, name);
}

static void touch_path(const char *path)
{
    char stamp[32];
    snprintf(stamp, sizeof stamp, "%lld", (long long)sys_now());
    sys_write_text(path, stamp);
}

static void touch(const char *cache, const char *pin)
{
    char *path = cache_file(cache, pin, "last-used");
    touch_path(path);
    free(path);
}

static void expire(void *user, const char *name, const bool is_dir)
{
    const char *cache = user;
    if (!is_dir || !packages_exact_version(name)) return;
    char *stamp = cache_file(cache, name, "last-used");
    char *text = sys_read_file(stamp, NULL);
    // Existing caches get a full grace period on first observation.
    if (!text) { touch(cache, name); free(stamp); return; }
    const long long last = atoll(text);
    free(text);
    if (last <= 0 || sys_now() - last < 30LL * 24 * 60 * 60) { free(stamp); return; }
    char *usage = cache_file(cache, name, "use");
    sys_lock *idle = sys_lock_file(usage);
    free(usage);
    char *install = cache_file(cache, name, "lock");
    sys_lock *installer = idle ? sys_lock_file(install) : NULL;
    free(install);
    if (installer) {
        // Recheck under the lock: a command may have refreshed its use stamp.
        text = sys_read_file(stamp, NULL);
        const long long checked = text ? atoll(text) : 0;
        free(text);
        if (checked > 0 && sys_now() - checked >= 30LL * 24 * 60 * 60) {
            char *root = path_join(cache, name);
            if (sys_remove_tree(root)) sys_remove(stamp);
            free(root);
        }
        sys_unlock(installer);
    }
    if (idle) sys_unlock(idle);
    free(stamp);
}

static bool consent(const char *pin, const char *cache)
{
    fprintf(stderr, "tide: this game needs tide %s, which isn't installed in %s.\n", pin, cache);
    if (!stdin_is_terminal() && !sys_env("TIDE_INTERACTIVE")) {
        fputs("  = note: run this command in a terminal to approve the download, or set TIDE_INTERACTIVE=1 to answer through stdin\n", stderr);
        return false;
    }
    fprintf(stderr, "Download tide %s there? [y/N] ", pin);
    fflush(stderr);
    char answer[32];
    if (fgets(answer, sizeof answer, stdin) && (answer[0] == 'y' || answer[0] == 'Y')) return true;
    fputs("tide: nothing was downloaded\n", stderr);
    return false;
}

static sys_lock *own_usage;
static char *own_stamp;
static void release_usage(void)
{
    if (own_usage) {
        touch_path(own_stamp);
        sys_unlock(own_usage);
        free(own_stamp);
    }
}

static bool keep_current(void)
{
    if (own_usage) return true;
    char *cache = versions_root();
    char *bin = sys_exe_dir();
    char *root = path_dir(bin);
    char *parent = path_dir(root);
    const char *name = strrchr(root, '/');
    name = name ? name + 1 : root;
    bool ok = true;
    if (cache && strcmp(cache, parent) == 0 && (packages_exact_version(name) || packages_channel(name))) {
        char *path = cache_file(cache, name, "use");
        own_usage = sys_lock_shared_file(path);
        free(path);
        ok = own_usage != NULL;
        if (ok) {
            own_stamp = cache_file(cache, name, "last-used");
            touch_path(own_stamp);
            atexit(release_usage);
        }
    }
    free(parent);
    free(root);
    free(bin);
    free(cache);
    return ok;
}

int tide_use_pinned(const char *folder, const int argc, char **argv, const char *version)
{
    if (tide_version_dev(version)) return -1;
    if (!keep_current()) {
        fputs("tide: this toolchain is being updated; retry when the update finishes\n", stderr);
        return 1;
    }
    char *unity = packages_unity_game(folder);
    if (unity) { free(unity); return -1; }
    packages_file game;
    const bool read = packages_read(folder, &game, NULL, NULL);
    if (!read || game.name || !game.pinned) {
        char *cache = versions_root();
        if (cache && sys_is_dir(cache)) sys_list(cache, expire, cache);
        free(cache);
        packages_free_file(&game);
        return -1;
    }
    char *cache = versions_root();
    if (!cache || !sys_mkdirs(cache)) {
        fputs("tide: can't write the versions folder; set TIDE_VERSIONS to a writable folder\n", stderr);
        free(cache);
        packages_free_file(&game);
        return 1;
    }
    const bool channel = packages_channel(game.pinned);
    // Exact pins are intentional. Older downloaded compilers must inherit this
    // opt-out too, before performing their own automatic update check.
    if (!channel) {
#ifdef _WIN32
        _putenv_s("TIDE_NO_UPDATE_CHECK", "1");
#else
        setenv("TIDE_NO_UPDATE_CHECK", "1", 1);
#endif
    }
    char *root = path_join(cache, game.pinned);
    char *program = path_join(root, TIDE_PROGRAM);
    char *usage = cache_file(cache, game.pinned, "use");
    char *bin = sys_exe_dir();
    char *own = path_dir(bin);
    char *absolute = path_absolute(root);
    const bool self = strcmp(own, absolute) == 0;
    free(bin); free(own); free(absolute);
    int code = 1;
    sys_lock *lease = NULL;
    // A dispatched compiler (or a direct invocation of its cached executable)
    // uses its installed version for this session and never dispatches again.
    if (self) {
        touch(cache, game.pinned);
        code = -1;
        goto done;
    }
    if (!channel && strcmp(game.pinned, version) == 0) {
        touch(cache, game.pinned);
        sys_list(cache, expire, cache);
        code = -1;
        goto done;
    }
    if (channel) {
        sys_lock *idle = sys_lock_file(usage);
        if (idle) {
            const bool present = sys_exists(program);
            const bool approved = present || consent(game.pinned, cache);
            const bool installed = approved && tide_install_channel(root, game.pinned);
            sys_unlock(idle);
            if (!installed) goto done;
        } else {
            fprintf(stderr, "tide: %s is in use; deferring its update\n", game.pinned);
        }
    }
    lease = sys_lock_shared_file(usage);
    if (!lease) goto busy;
    if (!channel && (!tide_installation_whole(root, game.pinned) || !sys_exists(program))) {
        if (!consent(game.pinned, cache) || !tide_install_version(root, game.pinned)) goto done;
    }
    char *version_path = path_join(root, "VERSION");
    char *selected = channel ? sys_read_file(version_path, NULL) : NULL;
    free(version_path);
    if (selected) selected[strcspn(selected, "\r\n ")] = '\0';
    const char *wanted = channel ? selected : game.pinned;
    if (!wanted || !packages_exact_version(wanted) || !tide_installation_whole(root, wanted)) {
        fprintf(stderr, "tide: %s isn't a complete installation; retry when its update finishes\n", root);
        free(selected);
        goto done;
    }
    const char *probe[] = {program, "--version", NULL};
    char said[256], prefix[128];
    snprintf(prefix, sizeof prefix, "tide %s (", wanted);
    if (sys_capture(probe, said, sizeof said) != 0 || strncmp(said, prefix, strlen(prefix)) != 0) {
        fprintf(stderr, "tide: %s isn't tide %s\n", program, wanted);
        fprintf(stderr, "  = note: move the cached folder %s aside, then rerun this command in a terminal to download it again\n", root);
        free(selected);
        goto done;
    }
    if (channel) fprintf(stderr, "tide: %s uses tide %s\n", game.pinned, wanted);
    free(selected);
    touch(cache, game.pinned);
    sys_list(cache, expire, cache);
    const char **args = calloc((size_t)argc + 1, sizeof *args);
    if (!args) abort();
    args[0] = program;
    for (int i = 1; i < argc; i++) args[i] = argv[i];
    code = sys_run(args, NULL, false);
    if (code < 0) code = 1;
    free(args);
    touch(cache, game.pinned);
    goto done;
busy:
    fputs("tide: this toolchain is being updated; retry when the update finishes\n", stderr);
done:
    if (lease && lease != own_usage) sys_unlock(lease);
    free(usage);
    free(program);
    free(root);
    free(cache);
    packages_free_file(&game);
    return code;
}
