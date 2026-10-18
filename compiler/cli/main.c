// tide: builds and runs Tide games, and keeps itself up to date.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "deploy.h"
#include "editors.h"
#include "install.h"
#include "help.h"
#include "lexer.h"
#include "open.h"
#include "packages.h"
#include "packages_cmd.h"
#include "publish.h"
#include "sys.h"
#include "toolchain.h"
#include "upgrade.h"

#ifdef TIDE_EMBEDDED_CLANG
#include "cc.h"
#endif

#ifndef TIDE_VERSION
#define TIDE_VERSION "0.0.0-dev"
#endif

// The installation: tide is in <root>/bin.
static char *install_root(void)
{
    char *bin = sys_exe_dir();
    char *root = path_dir(bin);
    free(bin);
    return root;
}

static int version(const char *root)
{
    printf("tide %s (%s)\ninstalled in %s\n", TIDE_VERSION, tide_channel(root), root);
    if (!tide_installation_whole(root, TIDE_VERSION)) {
        printf("the installation isn't whole (an upgrade of it stopped partway): run `tide upgrade`\n");
    }
#ifdef TIDE_EMBEDDED_CLANG
    printf("clang:   built in (LLVM %s)\n", TIDE_LLVM_VERSION);
#else
    char *clang = find_clang();
    char *web_clang = find_web_clang();
    char *wasm_ld = web_clang ? find_wasm_ld(web_clang) : NULL;
    printf("clang:   %s\n", clang ? clang : "not found (tide needs it to build games)");
    if (web_clang && clang && strcmp(web_clang, clang) != 0) printf("web:     %s\n", web_clang);
    printf("wasm-ld: %s\n", wasm_ld ? wasm_ld : "not found (needed for --web)");
#endif
    return 0;
}

int main(const int argc, char **argv)
{
#ifdef TIDE_EMBEDDED_CLANG
    // tide is its own C compiler: `tide cc` is clang, which runs itself again
    // as `tide -cc1` when it compiles in another process.
    if (argc > 1 && (strcmp(argv[1], "cc") == 0 || strcmp(argv[1], "-cc1") == 0)) {
        return tide_cc(argc, (const char **)argv);
    }
#endif
    if (argc < 2) {
        tide_help(0, NULL, TIDE_VERSION);
        return 2;
    }
    const char *command = argv[1];
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0)
        return tide_help(argc - 2, (const char *const *)argv + 2, TIDE_VERSION);
    if (argc == 3 && (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0))
        return tide_help(1, &command, TIDE_VERSION);
    if (strcmp(command, "open") == 0) return tide_open(argc, argv);
    char *root = install_root();
    // The packages that come with tide are sources of the installation, like the engine's
    char *builtin = path_join(root, "src/packages");
    packages_set_builtin_folder(builtin);
    free(builtin);
    if (strcmp(command, "_editor-preview") == 0) {
        if (argc != 8 || (strcmp(argv[7], "match") && strcmp(argv[7], "local"))) return 2;
        setvbuf(stdout, NULL, _IONBF, 0); setvbuf(stderr, NULL, _IONBF, 0);
        const char *defines[] = {"DEBUG"};
        const build_options opts = {.folder=argv[2], .editor_source=argv[3], .editor_draft=argv[4], .editor_prefab=argv[5],
            .output=argv[6], .editor_local=strcmp(argv[7], "local") == 0, .defines=defines, .define_count=1};
        char *built = tide_build_editor(root, &opts);
        const int result = built ? 0 : 1;
        free(built); free(root); return result;
    }
    tide_cleanup(root);

    if (strcmp(command, "version") == 0 || strcmp(command, "--version") == 0) return version(root);

    if (strcmp(command, "editors") == 0) {
        bool only_updates = false;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--update") == 0) only_updates = true; // Used by tide upgrade
            else {
                fprintf(stderr, "tide: editors doesn't take '%s'\n", argv[i]);
                return 2;
            }
        }
        return tide_editors(root, only_updates);
    }

    if (strcmp(command, "add") == 0) {
        if (argc != 3 || argv[2][0] == '-') {
            fprintf(stderr, "tide: add takes a package: `tide add github.com/owner/repo`, a folder, or the name of one that "
                            "comes with tide\n");
            return 2;
        }
        char *folder = path_absolute(".");
        return tide_add(folder, argv[2]);
    }

    if (strcmp(command, "update") == 0) {
        for (int i = 2; i < argc; i++) {
            if (argv[i][0] != '-') continue;
            fprintf(stderr, "tide: update doesn't take '%s'; it takes the names of packages, or none for all\n", argv[i]);
            return 2;
        }
        char *folder = path_absolute(".");
        return tide_update(folder, (const char *const *)argv + 2, argc - 2);
    }

    if (strcmp(command, "eject") == 0) {
        if (argc != 3 || argv[2][0] == '-') {
            fprintf(stderr, "tide: eject takes the name of one of the game's packages, like `tide eject rooms`\n");
            return 2;
        }
        char *folder = path_absolute(".");
        return tide_eject(folder, argv[2]);
    }

    if (strcmp(command, "fetch") == 0 || strcmp(command, "publish") == 0) {
        if (argc > 3 || (argc == 3 && argv[2][0] == '-')) {
            fprintf(stderr, "tide: %s takes the game's folder, or none for this one\n", command);
            return 2;
        }
        char *folder = path_absolute(argc == 3 ? argv[2] : ".");
        return command[0] == 'f' ? tide_fetch(folder) : tide_publish(folder);
    }

    if (strcmp(command, "upgrade") == 0) {
        const char *channel = NULL;
        const char *exact = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--nightly") == 0) channel = "nightly";
            else if (strcmp(argv[i], "--stable") == 0) channel = "stable";
            else if (strcmp(argv[i], "--version") == 0 && i + 1 < argc) exact = argv[++i];
            else {
                fprintf(stderr, "tide: upgrade doesn't take '%s'\n", argv[i]);
                return 2;
            }
        }
        return tide_upgrade(root, channel, exact);
    }

    if (strcmp(command, "deploy") == 0 || strcmp(command, "login") == 0 || strcmp(command, "logout") == 0)
        return tide_hosting_command(root, argc, argv);
    const bool run = strcmp(command, "run") == 0;
    const bool build = strcmp(command, "build") == 0;
    const bool shaders = strcmp(command, "shaders") == 0;
    const bool schedule = strcmp(command, "schedule") == 0 || shaders;
    if (!run && !build && !schedule) {
        fprintf(stderr, "tide: unknown command '%s'\n", command);
        fprintf(stderr, "  = note: see `tide help` for commands\n");
        return 2;
    }

    // There can be no more symbols than arguments, with room for DEBUG too.
    // Keep these outside the compiler's arena: a watched run resets it at every build.
    const char **defines = malloc((size_t)argc * sizeof *defines);
    if (!defines) abort();
    build_options opts = {.folder = ".", .defines = defines};
    // What the game's own command line takes (tide/run.h), which run passes on: --host [port], --join <code> or
    // --connect <address>, the window's size, its matches' seed, its data directory, and a run with no window. Each once is 27
    // arguments
    const char *game_args[32] = {NULL};
    int game_arg_count = 0;
    bool open_page = true; // run --web opens the page in a browser
    bool headless = false; // --capture, --capture-audio, --frames, --microphone or --script: the game runs with no window
    bool players = false;  // --players: several players, which only such a run has
    bool sized = false;
    bool data_directory = false;
    bool server_args = false;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const int left = argc - i - 1; // Arguments after this one
        if (run && game_arg_count > 28) { // Room for the longest of them yet, --size's 3, and the NULL after them
            fprintf(stderr, "tide: run takes each of its options once\n");
            free(defines);
            return 2;
        }
        if (strcmp(a, "--server") == 0) {
            opts.server = true;
        } else if (strcmp(a, "--strip-server-assets") == 0) {
            opts.server = opts.server_strip_assets = true;
        } else if (run && (strcmp(a, "--anonymous") == 0 || strcmp(a, "--udp-only") == 0)) {
            server_args = true;
            game_args[game_arg_count++] = a;
        } else if (run && (strcmp(a, "--port") == 0 || strcmp(a, "--max-matches") == 0
                          || strcmp(a, "--max-connections") == 0) && left >= 1) {
            server_args = true;
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = argv[++i];
        } else if (run && strcmp(a, "--host") == 0) {
            game_args[game_arg_count++] = a;
            if (left >= 1 && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') game_args[game_arg_count++] = argv[++i];
        } else if (run && (strcmp(a, "--join") == 0 || strcmp(a, "--connect") == 0) && left >= 1) {
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = argv[++i];
        } else if (run && strcmp(a, "--data-dir") == 0) {
            if (left < 1 || !argv[i + 1][0] || argv[i + 1][0] == '-') {
                fprintf(stderr, "tide: --data-dir takes a nonempty save directory, like `--data-dir test-saves`\n");
                free(defines);
                return 2;
            }
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = path_absolute(argv[++i]);
            data_directory = true;
        } else if (run && (strcmp(a, "--capture") == 0 || strcmp(a, "--capture-audio") == 0 || strcmp(a, "--script") == 0 ||
                           strcmp(a, "--microphone") == 0)) {
            if (left < 1) {
                const char *like = "--capture menu.png";
                if (a[2] == 's') like = "--script play.txt";
                else if (a[2] == 'm') like = "--microphone voice.wav";
                else if (a[9]) like = "--capture-audio run.wav"; // What's after --capture
                fprintf(stderr, "tide: %s takes a file, like `%s`\n", a, like);
                free(defines);
                return 2;
            }
            // The game runs in its own folder: the file is where the command says, from here
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = path_absolute(argv[++i]);
            headless = true;
        } else if (run && (strcmp(a, "--frames") == 0 || strcmp(a, "--scale") == 0)) {
            if (left < 1) {
                fprintf(stderr, "tide: %s takes a number, like `%s`\n", a, a[2] == 'f' ? "--frames 300" : "--scale 2");
                free(defines);
                return 2;
            }
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = argv[++i];
            if (a[2] == 'f') headless = true;
        } else if (run && strcmp(a, "--players") == 0) {
            if (left < 1) {
                fprintf(stderr, "tide: --players takes how many players the run has, like `--players 2`\n");
                free(defines);
                return 2;
            }
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = argv[++i];
            players = true;
        } else if (run && strcmp(a, "--size") == 0) {
            if (left < 2) {
                fprintf(stderr, "tide: --size takes the window's width and height, like `--size 1280 720`\n");
                free(defines);
                return 2;
            }
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = argv[++i];
            game_args[game_arg_count++] = argv[++i];
            sized = true;
        } else if (run && strcmp(a, "--seed") == 0) {
            // Said before anything is built: the game would refuse it too
            const char *seed = left >= 1 ? argv[i + 1] : "";
            uint64_t value = 0;
            const char *c = seed;
            for (; *c >= '0' && *c <= '9' && value <= 2147483647u; c++) value = value * 10u + (uint64_t)(*c - '0');
            if (c == seed || *c || value > 2147483647u) {
                fprintf(stderr, "tide: --seed takes the seed of the matches the game starts, a whole number from 0 to 2147483647, "
                                "like `--seed 7`\n");
                free(defines);
                return 2;
            }
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = argv[++i];
        } else if (run && strcmp(a, "--no-open") == 0) open_page = false;
        else if (strcmp(a, "--release") == 0) opts.release = true;
        else if (strcmp(a, "--web") == 0) opts.web = true;
        else if (strcmp(a, "--android") == 0) opts.android = true;
        else if (strcmp(a, "--stats") == 0) opts.stats = true;
        else if (strcmp(a, "--debug-views") == 0) opts.debug_views = true;
        else if (strcmp(a, "--define") == 0) {
            const char *name = left >= 1 ? argv[++i] : "";
            if (!lex_symbol_ok(name)) {
                fprintf(stderr, "tide: --define takes a symbol for the game's #if lines, like `--define FAST`%s\n",
                        strncmp(name, "TIDE_", 5) == 0 || strncmp(name, "PACKAGE_", 8) == 0
                            ? ": symbols that start with TIDE_ and PACKAGE_ are tide's to say"
                            : "");
                free(defines);
                return 2;
            }
            defines[opts.define_count++] = name;
        } else if (strcmp(a, "--title") == 0 && i + 1 < argc) opts.title = argv[++i];
        else if (strcmp(a, "-o") == 0 && i + 1 < argc && build) opts.output = argv[++i];
        else if (a[0] != '-') opts.folder = a;
        else {
            fprintf(stderr, "tide: %s doesn't take '%s'; see `tide help %s`\n", command, a, command);
            free(defines);
            return 2;
        }
    }

    // Whoever runs a game with --stats wants to read them: they start open. What a build shows its players
    // starts as one line.
    opts.stats_open = run && opts.stats;
    // Interactive runs reload as files change. Runs with no window end once.
    opts.watch = run && !headless && !opts.server;
    // DEBUG is every build's but --release's
    if (!opts.release) defines[opts.define_count++] = "DEBUG";
    // The game's tide.packages reads them too: its #if lines say which packages this build has
    packages_set_symbols(defines, opts.define_count);
    if (server_args && !opts.server) {
        fputs("tide: --anonymous, --udp-only, --port and server capacity options require --server\n", stderr);
        free(defines);
        return 2;
    }
    if (opts.server) {
        for (int i = 0; i < game_arg_count; i++) {
            const char *a = game_args[i];
            if (a[0] != '-') continue;
            if (strcmp(a, "--anonymous") == 0 || strcmp(a, "--udp-only") == 0) continue;
            if (strcmp(a, "--port") == 0 || strcmp(a, "--max-matches") == 0
                || strcmp(a, "--max-connections") == 0 || strcmp(a, "--frames") == 0) { i++; continue; }
            fprintf(stderr, "tide: --server does not take %s\n", a);
            free(defines);
            return 2;
        }
    }
    if (opts.server && (opts.web || opts.android || opts.stats || opts.debug_views || sized || players)) {
        fputs("tide: --server is a native headless build; omit --web, --android, --stats, --debug-views, --size and --players\n", stderr);
        free(defines);
        return 2;
    }
    if (opts.web && opts.android) {
        fprintf(stderr, "tide: a build is for the web or for Android, not both at once\n");
        free(defines);
        return 2;
    }
    if (players && !headless) {
        // Said before anything is built: the game would refuse it too
        fprintf(stderr, "tide: --players is for a run with no window: with --capture, --frames or --script\n");
        free(defines);
        return 2;
    }
    if ((headless || sized) && (opts.web || opts.android)) {
        fprintf(stderr, "tide: %s on this machine: not with --web or --android\n",
                headless ? "--capture, --frames and --script run the game with no window," : "--size is for a window");
        free(defines);
        return 2;
    }
    if (data_directory && (opts.web || opts.android)) {
        fprintf(stderr, "tide: --data-dir overrides native desktop saves, not web or Android storage\n");
        free(defines);
        return 2;
    }
    if (run && sys_hidden()) {
        // Nobody watches: the game shows no window, so its run has to end by itself, and no browser opens
        if (!headless && !opts.web && !opts.android) {
            fprintf(stderr, "tide: TIDE_HIDDEN is set, so the game shows no window, and nothing else would end its run: give run "
                            "--frames <n> (and --capture <file.png> for a picture of the last one), or --script <file>\n");
            free(defines);
            return 2;
        }
        open_page = false;
    }
    // A run ends with its game. Do the occasional update check before it
    // starts, so the network can't keep tide alive after the window closes.
    if (run) tide_check_for_update(root);
    int code = 0;
    if (schedule) {
        code = tide_schedule(&opts, shaders) ? 0 : 1;
    } else if (run && opts.android) {
        code = tide_run_android(root, &opts, game_args);
    } else if (run && !opts.web) {
        code = opts.watch ? tide_run_reloading(root, &opts, game_args) : tide_run_native(root, &opts, game_args);
    } else if (run) {
        code = tide_run_web(root, &opts, open_page, game_args);
    } else {
        char *program = tide_build(root, &opts);
        if (!program) code = 1;
        else printf("Built %s\n", program);
    }
    if (!run) tide_check_for_update(root);
    free(defines);
    return code;
}
