// tide: builds and runs Tide games, and keeps itself up to date.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "editors.h"
#include "install.h"
#include "lexer.h"
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

#ifdef TIDE_EMBEDDED_CLANG
#define CC_USAGE "  cc <options>       clang, built into tide, for C to build yourself (like libraries)\n"
#else
#define CC_USAGE ""
#endif

static void usage(void)
{
    printf("tide %s: builds and runs Tide games.\n"
           "\n"
           "usage: tide <command> [folder] [options]\n"
           "\n"
           "  run [folder]       build the game in folder (default: here) and play it\n"
           "  build [folder]     build the game into <folder>/build\n"
           "  schedule [folder]  show which systems can run at the same time, and why the others wait\n"
           "  shaders [folder]   show the shaders the game's frags and verts compile to, as GLSL and WGSL\n"
           "  add <source>       add a package to the game here: from git, a folder, or one that comes with tide (see below)\n"
           "  update [package]   move the game's packages from git to the newest commit of what they follow\n"
           "  fetch [folder]     download the game's packages from git that aren't on this machine yet, building nothing\n"
           "  publish [folder]   send this version of the game's source to the build service, which builds it with mods\n"
           "  editors            add Tide to VS Code, Cursor, VSCodium and Windsurf\n"
           "  upgrade            update tide to the newest version\n"
           "  version            show tide's version and channel, where it's installed, and which compilers it found\n"
           CC_USAGE
           "\n"
           "A game is every .tide file in its folder and its subfolders, and the packages its\n"
           "tide.packages lists.\n"
           "\n"
           "run and build:\n"
           "  --release          optimized, the way players get it\n"
           "  --web              a web page (WebGPU, or WebGL 2 where the browser has none), with clang's WebAssembly target\n"
           "  --android          an Android app (.apk); run installs it on the phone or emulator connected\n"
           "  --define <SYMBOL>  a symbol the game's #if lines read as defined, like `--define FAST`; as many as needed.\n"
           "                     DEBUG is defined without --release, and PACKAGE_<NAME> for each of the game's packages\n"
           "  --title <title>    the window's title (default: the game's title setting, or the folder's name)\n"
           "  --stats            show the stats in a corner, as the game's stats setting does: the frame rate, and at a\n"
           "                     click or F8 the rest (cpu time, ping, bandwidth, memory, draw calls, the slowest\n"
           "                     systems). run starts them open\n"
           "  --debug-views      F7 shows the profiler: where a frame's time and a tick's go (as the debugViews setting)\n"
           "  -o <path>          build: where the program goes\n"
           "  --no-open          run --web: serve the page without opening a browser\n"
           "  --watch            run: rebuild as files change and reload (Android: reinstall and restart)\n"
           "\n"
           "run, in a match with others (the game's Main must be the match's for --host):\n"
           "  --host [port]        a match others can join: in a room, and on a port (7777 by default; not on the web)\n"
           "  --join <code>        the match in the room with this code, like K7QF2M\n"
           "  --connect <address>  the match at an address, like 192.168.1.5 or localhost:7777 (not on the web)\n"
           "\n",
           TIDE_VERSION);
    // In two parts: C promises a string literal of 4095 characters only
    printf("run, with no window, to see what the game draws (not on the web or Android). The game runs once, in\n"
           "made-up time, a 60th of a second a frame, so a run repeats exactly, and tide ends with its exit code:\n"
           "  --capture <file.png>  the run's last frame, as a PNG\n"
           "  --frames <n>          the run ends after this many frames\n"
           "  --script <file>       input by frame, and when to capture and quit: lines like `120 key d down`,\n"
           "                        `200 mouse 640 360`, `201 button left`, `400 capture fight.png` and `500 quit`\n"
           "  --scale <s>           as on a display scaled this much: 1.5, 2 (1 without it)\n"
           "  --size <w> <h>        the window's size in pixels, for a run with a window too (960 by 540 without it)\n"
           "  --seed <n>            the seed of the matches the game starts, from 0 to 2147483647, for any run: without\n"
           "                        it, 0 with no window, and another one every match with one\n"
           "With TIDE_HIDDEN set (an agent's shell, a build server), games show no window: run takes one of\n"
           "these or says so, and run --web opens no browser.\n"
           "\n"
           "add: a package from git is github.com/owner/repo (or its URL), at the newest commit of\n"
           "its default branch; @branch after it follows a branch, @v1 the newest v1.x.y tag, and\n"
           "//folder a folder in the repository. A folder on this machine is a path, like ../shared.\n"
           "A package that comes with tide is its name alone, and is the version this tide came with.\n"
           "\n"
           "publish: the game's key is in the environment, as TIDE_GAME_KEY. What's sent is what a build reads from\n"
           "the game's folder: its .tide files, its C and libraries, and its tide.packages.\n"
           "\n"
           "upgrade:\n"
           "  --nightly          follow nightly versions from now on\n"
           "  --stable           follow stable versions from now on\n"
           "  --version <v>      install exactly this version\n"
           "tide says once a day when a newer version is out, unless TIDE_NO_UPDATE_CHECK is set.\n");
}

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
    if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage();
        return argc < 2 ? 2 : 0;
    }
    const char *command = argv[1];
    char *root = install_root();
    // The packages that come with tide are sources of the installation, like the engine's
    char *builtin = path_join(root, "src/packages");
    packages_set_builtin_folder(builtin);
    free(builtin);
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

    const bool run = strcmp(command, "run") == 0;
    const bool build = strcmp(command, "build") == 0;
    const bool shaders = strcmp(command, "shaders") == 0;
    const bool schedule = strcmp(command, "schedule") == 0 || shaders;
    if (!run && !build && !schedule) {
        fprintf(stderr, "tide: unknown command '%s'\n", command);
        fprintf(stderr, "  = note: the commands are run, build, schedule, shaders, add, update, fetch, publish, editors, upgrade "
                        "and version; see `tide help`\n");
        return 2;
    }

    build_options opts = {.folder = "."};
    // What the game's own command line takes (tide/run.h), which run passes on: --host [port], --join <code> or
    // --connect <address>, the window's size, its matches' seed, and a run with no window
    const char *game_args[24] = {NULL};
    int game_arg_count = 0;
    bool open_page = true; // run --web opens the page in a browser
    bool headless = false; // --capture, --frames or --script: the game runs with no window
    bool sized = false;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const int left = argc - i - 1; // Arguments after this one
        if (run && game_arg_count > 20) { // Room for the longest of them yet
            fprintf(stderr, "tide: run takes each of its options once\n");
            return 2;
        }
        if (run && strcmp(a, "--host") == 0) {
            game_args[game_arg_count++] = a;
            if (left >= 1 && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') game_args[game_arg_count++] = argv[++i];
        } else if (run && (strcmp(a, "--join") == 0 || strcmp(a, "--connect") == 0) && left >= 1) {
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = argv[++i];
        } else if (run && (strcmp(a, "--capture") == 0 || strcmp(a, "--script") == 0)) {
            if (left < 1) {
                fprintf(stderr, "tide: %s takes a file, like `%s`\n", a, a[2] == 'c' ? "--capture menu.png" : "--script play.txt");
                return 2;
            }
            // The game runs in its own folder: the file is where the command says, from here
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = path_absolute(argv[++i]);
            headless = true;
        } else if (run && (strcmp(a, "--frames") == 0 || strcmp(a, "--scale") == 0)) {
            if (left < 1) {
                fprintf(stderr, "tide: %s takes a number, like `%s`\n", a, a[2] == 'f' ? "--frames 300" : "--scale 2");
                return 2;
            }
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = argv[++i];
            if (a[2] == 'f') headless = true;
        } else if (run && strcmp(a, "--size") == 0) {
            if (left < 2) {
                fprintf(stderr, "tide: --size takes the window's width and height, like `--size 1280 720`\n");
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
                return 2;
            }
            game_args[game_arg_count++] = a;
            game_args[game_arg_count++] = argv[++i];
        } else if (run && strcmp(a, "--no-open") == 0) open_page = false;
        else if (run && strcmp(a, "--watch") == 0) opts.watch = true;
        else if (strcmp(a, "--release") == 0) opts.release = true;
        else if (strcmp(a, "--web") == 0) opts.web = true;
        else if (strcmp(a, "--android") == 0) opts.android = true;
        else if (strcmp(a, "--stats") == 0) opts.stats = true;
        else if (strcmp(a, "--debug-views") == 0) opts.debug_views = true;
        else if (strcmp(a, "--define") == 0) {
            const char *name = left >= 1 ? argv[++i] : "";
            if (!lex_symbol_ok(name) || opts.define_count >= 63) {
                fprintf(stderr, "tide: --define takes a symbol for the game's #if lines, like `--define FAST`%s\n",
                        strncmp(name, "TIDE_", 5) == 0 || strncmp(name, "PACKAGE_", 8) == 0
                            ? ": symbols that start with TIDE_ and PACKAGE_ are tide's to say"
                            : "");
                return 2;
            }
            opts.defines[opts.define_count++] = name;
        } else if (strcmp(a, "--title") == 0 && i + 1 < argc) opts.title = argv[++i];
        else if (strcmp(a, "-o") == 0 && i + 1 < argc && build) opts.output = argv[++i];
        else if (a[0] != '-') opts.folder = a;
        else {
            fprintf(stderr, "tide: %s doesn't take '%s'; see `tide help`\n", command, a);
            return 2;
        }
    }

    // Whoever runs a game with --stats wants to read them: they start open. What a build shows its players
    // starts as one line.
    opts.stats_open = run && opts.stats;
    // DEBUG is every build's but --release's
    if (!opts.release) opts.defines[opts.define_count++] = "DEBUG";
    if (opts.web && opts.android) {
        fprintf(stderr, "tide: a build is for the web or for Android, not both at once\n");
        return 2;
    }
    if ((headless || sized) && (opts.web || opts.android)) {
        fprintf(stderr, "tide: %s on this machine: not with --web or --android\n",
                headless ? "--capture, --frames and --script run the game with no window," : "--size is for a window");
        return 2;
    }
    if (opts.watch && headless) {
        fprintf(stderr, "tide: --watch needs a run with a window: --capture, --frames and --script run once; leave out --watch\n");
        return 2;
    }
    if (run && sys_hidden()) {
        // Nobody watches: the game shows no window, so its run has to end by itself, and no browser opens
        if (!headless && !opts.web && !opts.android) {
            fprintf(stderr, "tide: TIDE_HIDDEN is set, so the game shows no window, and nothing else would end its run: give run "
                            "--frames <n> (and --capture <file.png> for a picture of the last one), or --script <file>\n");
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
    return code;
}
