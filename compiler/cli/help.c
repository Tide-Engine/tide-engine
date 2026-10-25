#include "help.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum {
    RUN = 1u << 0, BUILD = 1u << 1, DEPLOY = 1u << 2, LOGIN = 1u << 3,
    LOGOUT = 1u << 4, SCHEDULE = 1u << 5, SHADERS = 1u << 6, ADD = 1u << 7,
    UPDATE = 1u << 8, EJECT = 1u << 9, FETCH = 1u << 10, PUBLISH = 1u << 11,
    EDITORS = 1u << 12, OPEN = 1u << 13, UPGRADE = 1u << 14, VERSION = 1u << 15,
    HELP = 1u << 16, CC = 1u << 17, IDE = 1u << 18, UNITY = 1u << 19, TELEMETRY = 1u << 20,
    COMPILE = RUN | BUILD | SCHEDULE | SHADERS | UNITY,
    FOLDERS = COMPILE | DEPLOY | FETCH | PUBLISH
};

typedef struct command_help {
    const char *name, *usage, *summary, *details;
    unsigned mask;
} command_help;

static const command_help commands[] = {
    {"run", "run [folder] [options]", "Build, play and reload on changes",
     "Builds the game in folder (default: here) and reloads as files change.\n"
     "Native captures and scripted runs play once without a window, in fixed\n"
     "1/60-second frames, and return the game's exit code. Use --frames or a\n"
     "script's quit to end them. They are unavailable on web and Android.\n"
     "With TIDE_HIDDEN set, native runs require a bounded mode; web opens no browser.\n\n"
     "Examples: tide run --web\n"
     "          tide run --frames 300 --capture menu.png", RUN},
    {"build", "build [folder] [options]", "Build a game for distribution",
     "Builds into <folder>/build by default; -o selects the output path.\n"
     "A game includes .tide and .tprefab files, its C/C++ and libraries, and\n"
     "the packages listed in tide.packages. Work files are cached in .tide.\n\n"
     "Example: tide build --release --web", BUILD},
    {"unity", "unity [folder] -o <path> [options]", "Build a game for Unity's Tide package",
     "Builds the game for the Tide package in a Unity project, into the path\n"
     "(the project's Library/Tide): the host Unity loads, then a library of the\n"
     "game each time its files change, which the running game swaps in. It ends\n"
     "when its input closes, as the Unity editor's package does. --once builds\n"
     "once and ends. In a Unity project the game is all of Assets, and C is\n"
     "only ever a package's. --notified builds when a `changed` line comes on\n"
     "its input, rather than looking at every file of Assets, as the package\n"
     "tells it.\n\n"
     "Example: tide unity Assets -o Library/Tide --assets Assets", UNITY},
    {"deploy", "deploy [folder] [options]", "Build locally and upload to managed hosting",
     "Builds a release client and WASM server locally, then uploads them.\n"
     "Creates or reuses a folder-named project and saves its immutable ID and\n"
     "service origin in tide.deploy.json. Later deploys reuse that link, even\n"
     "after renaming the folder. Use --project to select or relink explicitly.\n"
     "Run tide login first; --bundle-only builds without login or upload.", DEPLOY},
    {"login", "login", "Authorize CLI access to managed hosting",
     "Requests browser device approval and saves credentials for the hosting\n"
     "service. Credentials are bound to that service origin.", LOGIN},
    {"logout", "logout", "Revoke CLI access to managed hosting",
     "Revokes this CLI's hosting access and removes its saved credentials.", LOGOUT},
    {"schedule", "schedule [folder] [options]", "Explain system order and dependencies",
     "Checks the game and shows which systems can run at the same time,\n"
     "and why the others wait. Builds no executable.\n"
     "Use --define and --release to inspect a particular conditional build.", SCHEDULE},
    {"shaders", "shaders [folder] [options]", "Show generated GLSL and WGSL",
     "Checks the game and prints the GLSL and WGSL generated from its frags\n"
     "and verts. Builds no executable.\n"
     "Use --define and --release to inspect a particular conditional build.", SHADERS},
    {"add", "add <source>", "Add a built-in, local or git package",
     "Adds a package and its dependencies to tide.packages in the current game.\n"
     "A built-in package is its name alone, like rooms. A local package is a\n"
     "path, like ../shared. Git sources accept github.com/owner/repo or a URL;\n"
     "@branch follows a branch, @v1 follows the newest v1.x.y tag, and //folder\n"
     "selects a repository subfolder. Git packages are pinned to a commit.\n\n"
     "Examples: tide add rooms\n"
     "          tide add github.com/owner/repo@main", ADD},
    {"update", "update [package ...]", "Update packages from git",
     "Moves named git packages to the newest commit of the branch or version\n"
     "they follow, updating tide.packages in the current game. Without names,\n"
     "updates all git packages. Built-in and local packages stay as they are.", UPDATE},
    {"eject", "eject <package>", "Copy a package into the game to edit it",
     "Copies a listed package to packages/<name> in the current game and\n"
     "replaces its tide.packages line with that local path. It stays a package,\n"
     "with the same namespace and dependencies. An existing destination fails.\n\n"
     "Example: tide eject rooms", EJECT},
    {"fetch", "fetch [folder]", "Download missing git packages",
     "Downloads the game's missing git packages at their pinned commits.\n"
     "Builds nothing and does not move the pins; use tide update for that.", FETCH},
    {"publish", "publish [folder]", "Send source to the inactive mod build service",
     "The mod build service is on hold. This existing command sends source,\n"
     "C, libraries and tide.packages using TIDE_GAME_KEY from the environment.\n"
     "Use tide deploy for managed hosting of locally built games.", PUBLISH},
    {"editors", "editors", "Install Tide support in supported code editors",
     "Installs Tide support in the VS Code, Cursor, VSCodium and Windsurf\n"
     "installations it finds, and existing JetBrains IDE profiles (2024.2+).\n"
     "Uses the plugins bundled with this Tide version. Restart JetBrains IDEs\n"
     "after installation; open a new IDE once before running tide editors.", EDITORS},
    {"ide", "ide <editor>", "Print setup for Emacs, Vim or Neovim",
     "Prints configuration to copy into your editor's init file, using this\n"
     "installation's bundled modes and tidels. Changes no files and starts\n"
     "no editor. Emacs and Neovim configurations enable automatic LSP startup;\n"
     "Vim highlighting works alone and LSP needs your chosen client.\n\n"
     "Examples: tide ide emacs\n"
     "          tide ide vim\n"
     "          tide ide neovim", IDE},
    {"open", "open <file.tprefab> [options]", "Open the native scene editor",
     "Opens the desktop editor for an authored prefab or scene. The project\n"
     "is the nearest enclosing tide.packages folder, or the prefab's folder.\n"
     "Use --project when the game root is elsewhere. Packages must already\n"
     "be available locally (tide fetch). --check validates without a window.\n"
     "With TIDE_HIDDEN set, use --check or a bounded capture/script run.", OPEN},
    {"upgrade", "upgrade [options]", "Update Tide or change release channel",
     "Installs the newest version in the current channel unless --version\n"
     "selects one. --nightly and --stable save the channel for future upgrades.\n"
     "Tide checks for newer versions once a day unless TIDE_NO_UPDATE_CHECK is set.", UPGRADE},
    {"version", "version", "Show version, installation and compilers",
     "Shows Tide's version and release channel, installation folder, and\n"
     "the built-in or external compilers it found.", VERSION},
    {"telemetry", "telemetry <on|off|status>", "Turn anonymous usage data on or off",
     "Each command sends one small anonymous event: the command, target,\n"
     "version, OS and CPU, CI or not, duration, outcome and a coarse failure\n"
     "kind, with file and package counts in ranges. Never paths, names, code\n"
     "or messages. status shows exactly what is sent, where, and the install's\n"
     "random ID. TIDE_TELEMETRY=0 or DO_NOT_TRACK=1 also turn it off.\n"
     "Details: https://tide-engine.dev/tools/telemetry", TELEMETRY},
    {"help", "help [command | argument] [argument]", "Show command or argument details",
     "Without a topic, shows the short command list. Name a command for its\n"
     "usage and options, or an argument for its meaning and defaults. Add a\n"
     "command before an argument to limit help to that command.\n\n"
     "Examples: tide help run\n"
     "          tide help --frames\n"
     "          tide help deploy --project\n"
     "          tide run --help", HELP},
#ifdef TIDE_EMBEDDED_CLANG
    {"cc", "cc <options>", "Use the built-in Clang C compiler",
     "Runs Clang for C you build yourself, such as prebuilt libraries.\n"
     "Use tide cc --help for Clang's own options.", CC},
#endif
};

typedef struct argument_help {
    const char *name, *usage, *summary, *details;
    unsigned mask;
} argument_help;

// Shared entries are also used in each command's option list.
static const argument_help arguments[] = {
    {"editor", "<editor>", "emacs, vim or neovim",
     "emacs needs Emacs 29.1+ with Eglot; neovim needs Neovim 0.11+.\n"
     "vim needs Vim 8+ and a separate client for LSP features.\n"
     "Add the printed configuration to your init file and restart the editor.", IDE},
    {"folder", "[folder]", "Game folder (default: current directory)",
     "Includes .tide and .tprefab files in this folder and its subfolders, plus\n"
     "C/C++ files, libraries and packages from tide.packages. Nested folders\n"
     "with their own tide.packages are separate games or packages.", FOLDERS},
    {"source", "<source>", "Built-in name, local path or git source",
     "Examples: rooms, ../shared, github.com/owner/repo@main.\n"
     "Git sources accept @branch, @v1 and //subfolder; see tide help add.", ADD},
    {"package", "<package>", "Name of a package listed by this game",
     "Use the package's source name as listed in tide.packages.\n"
     "update accepts several names, or none for every git package; eject takes one.", UPDATE | EJECT},
    {"file.tprefab", "<file.tprefab>", "Authored prefab or scene file",
     "Selects the file to edit or validate. Use --project to choose the game root.", OPEN},
    {"--release", "--release", "Optimize game code for players",
     "Engine code is always optimized. Without --release, game/package code\n"
     "is unoptimized, assertions remain enabled and DEBUG is defined.\n"
     "Standalone releases strip unused code; Tide-only releases also omit\n"
     "unreferenced assets. schedule/shaders use this to leave DEBUG undefined.", COMPILE},
    {"--web", "--web", "Build a web page",
     "Uses Clang's WebAssembly target. Pages use WebGPU with WebGL 2 fallback.\n"
     "build --release produces one self-contained HTML file. run serves the\n"
     "page and opens a browser unless --no-open or TIDE_HIDDEN is set.\n"
     "unity --web builds tide-game.js, for a Unity WebGL player's page, once.\n"
     "Choose one target: native (default), --web or --android.", RUN | BUILD | UNITY},
    {"--android", "--android", "Build an Android APK",
     "run installs and starts it on the connected phone or emulator, using adb.\n"
     "Saving rebuilds and restarts the app. Release builds also make an app\n"
     "bundle and require the game's appId. Choose native, web or Android.", RUN | BUILD},
    {"--define", "--define <SYMBOL>", "Define a symbol for #if lines",
     "Repeat for as many symbols as needed, like --define FAST --define STEAM.\n"
     "DEBUG is automatic without --release; PACKAGE_<NAME> is automatic for\n"
     "each package. TIDE_ and PACKAGE_ prefixes are reserved.", COMPILE},
    {"--title", "--title <title>", "Override the game's window title",
     "Defaults to the game's title setting, or its folder name.", RUN | BUILD},
    {"--scene", "--scene <qualified-name>", "Start directly in an authored prefab or scene",
     "Overrides the project default: settings { startScene = Levels.Start; }.\n"
     "Without either selection, the game starts in Main. Runs normal\n"
     "Spawned handlers, systems and views, with the usual session and devices.", RUN | BUILD},
    {"--stats", "--stats", "Enable the stats drawer (F8)",
     "Shows frame rate, CPU/GPU time, ping, bandwidth, memory, draw calls and\n"
     "slow systems. run starts the drawer open; build starts it closed.\n"
     "A click or F8 toggles it, as with the game's stats setting.", RUN | BUILD},
    {"--debug-views", "--debug-views", "Enable the profiler (F7)",
     "F7 shows where frame and tick time go, as with the debugViews setting.", RUN | BUILD},
    {"-o", "-o <path>", "Select the built program's output path",
     "Overrides the default output in <folder>/build. unity requires it.", BUILD | UNITY},
    {"--once", "--once", "Build once and end",
     "unity builds the game once rather than as its files change, as a player build does.", UNITY},
    {"--notified", "--notified", "Build when told the files changed",
     "unity builds when a `changed` line comes on its input, and between builds\n"
     "looks only at the files it built, not every file of Assets: what Unity's\n"
     "package passes, as Unity says when its assets change.", UNITY},
    {"--declarations", "--declarations", "Build the game's types alone, for their C#",
     "unity builds the game's declarations once, leaving out its systems, views,\n"
     "handlers, functions, frags and verts, and writes the project's\n"
     "Packages/com.tide.game from them. tide unity runs it itself after a build\n"
     "that failed, so a component's C# comes before the code that uses it builds.", UNITY},
    {"--assets", "--assets <folder>", "Include a Unity project's prefabs and scenes",
     "Reads a Unity project's Assets folder: each prefab and scene holding\n"
     "TideEntity objects is a Tide prefab named after its file, and Build\n"
     "Settings' first scene loads as Main starts. build and run take it for a\n"
     "server, or any build, of a game made in Unity.", UNITY | BUILD | RUN},
    {"--no-open", "--no-open", "Serve a web run without opening a browser",
     "Use with tide run --web. TIDE_HIDDEN also prevents opening a browser.", RUN},
    {"--host", "--host [port]", "Open the startup match for others to join",
     "Requires a match startup, selected by --scene, settings.startScene or Main.\n"
     "Opens a WebRTC room and, on desktop,\n"
     "a UDP port (7777 by default). Web hosts use the room only.", RUN},
    {"--join", "--join <code>", "Join a match by WebRTC room code",
     "Example: --join K7QF2M. For dedicated servers, room/0 requests a new\n"
     "match and room/<match-id> joins an existing match.", RUN},
    {"--connect", "--connect <address>", "Join a match by UDP address",
     "Examples: 192.168.1.5 or localhost:7777. Unavailable on web.\n"
     "For dedicated servers, host:port/0 requests a new match and\n"
     "host:port/<match-id> joins an existing match.", RUN},
    {"--server", "--server", "Build or run a native headless dedicated server",
     "Hosts many matches in one process without watching or hot reload.\n"
     "Cannot combine with web, Android, stats, debug views or player captures.\n"
     "Server runs accept --frames to bound their duration.", RUN | BUILD},
    {"--strip-server-assets", "--strip-server-assets", "Opt custom C into server asset pruning",
     "Also selects --server. Tide-only server builds prune client-only assets\n"
     "automatically; use this when custom C does not need those assets.", RUN | BUILD},
    {"--anonymous", "--anonymous", "Allow unauthenticated server admission",
     "Requires run --server. Allows clients to create and join matches\n"
     "without authentication.", RUN},
    {"--port", "--port <n>", "Server listening port (default: 7777)",
     "Requires run --server.", RUN},
    {"--max-matches", "--max-matches <n>", "Server match capacity (default: 64)",
     "Requires run --server. Limits simultaneous matches.", RUN},
    {"--max-connections", "--max-connections <n>", "Server connection capacity (default: 256)",
     "Requires run --server. Limits admitted and pending connections.", RUN},
    {"--udp-only", "--udp-only", "Disable the server's WebRTC room",
     "Requires run --server. Clients connect by direct UDP address.", RUN},
    {"--shards", "--shards <n>", "Split a sharded game's world into n regions",
     "For a game with 'settings { shards = true; }': its match starts as n\n"
     "regions along x, each a shard. On a server, each shard is one of its\n"
     "matches and players join the world at match 0.", RUN},
    {"--shard-width", "--shard-width <units>", "Each region's width, in world units",
     "Default: 16 times the game's reach. Use with --shards.", RUN},
    {"--shard-most", "--shard-most <n>", "Split a region whose shard owns more entities",
     "Requires run --server. A region splits in two by where its entities\n"
     "are, down to twice the game's reach across; two neighbouring\n"
     "regions owning a quarter of n together merge back into one.", RUN},
    {"--shard-journal", "--shard-journal <folder>", "Keep a sharded server's world in a folder",
     "Requires run --server. A checkpoint now and then, and every shard's ticks\n"
     "since, on the disk before anything a shard sends goes on: a server\n"
     "started again with the same folder goes on where the world was.", RUN},
    {"--capture", "--capture <file.png>", "Save the last frame as a PNG",
     "Runs natively without a window; add --frames or a script's quit.\n"
     "The output path is relative to the directory where tide was invoked.", RUN | OPEN},
    {"--data-dir", "--data-dir <folder>", "Use a separate native save directory",
     "Overrides Application.persistentDataPath and relative file paths. Creates\n"
     "the directory and parents; paths are relative to where tide was invoked.\n"
     "With several --players, each uses a player-<number> subdirectory.\n"
     "Native desktop only. The directory persists until you remove it.", RUN},
    {"--capture-audio", "--capture-audio <file.wav>", "Save all audio from a headless run",
     "Writes 16-bit stereo WAV at 48 kHz. Without sound the file is silent.\n"
     "Native only; add --frames or a script's quit. Paths are relative to\n"
     "where tide was invoked.", RUN},
    {"--microphone", "--microphone <file>", "Loop WAV or Ogg Vorbis into the microphone",
     "Native headless runs otherwise hear silence. Paths are relative to\n"
     "where tide was invoked. Add --frames or a script's quit.", RUN},
    {"--frames", "--frames <n>", "End a native headless run after n frames",
     "Game captures use fixed 1/60-second frames. Also bounds dedicated server\n"
     "runs. Unavailable with run --web or --android.", RUN},
    {"--frames", "--frames <n>", "End an editor run after n frames",
     "Bounds the native editor's hidden input/capture loop. With preview\n"
     "enabled, waits for its current build; a failed build returns 1.", OPEN},
    {"--script", "--script <file>", "Read frame-numbered input and capture actions",
     "Native only. Lines include: 120 key d down, 200 mouse 640 360,\n"
     "201 button left, 400 capture fight.png, 500 quit. Use --frames or quit\n"
     "to end the run. The script path is relative to where tide was invoked.", RUN | OPEN},
    {"--scale", "--scale <s>", "Headless display scale (default: 1)",
     "Native only. For example, --size 1280 720 --scale 2 draws 2560 by 1440\n"
     "pixels. Combine with --frames, --capture or --script.", RUN},
    {"--size", "--size <width> <height>", "Native window size (default: 960 by 540)",
     "Also controls headless capture size, before --scale.\n"
     "Unavailable with run --web or --android.", RUN},
    {"--size", "--size <width> <height>", "Editor size (default: 1440 by 900)",
     "Accepts widths from 1100 to 8192 and heights from 650 to 8192.", OPEN},
    {"--seed", "--seed <n>", "Choose the seed of matches the game starts",
     "A whole number from 0 to 2147483647. Defaults to 0 for headless runs\n"
     "and a different seed for every match in a window. The same seed and\n"
     "inputs repeat the same simulation.", RUN},
    {"--players", "--players <n>", "Run several players on a simulated network",
     "Native headless only: add --frames, --capture or --script. At most 32\n"
     "players, limited by the game's maxPlayers. With a match startup, player 1\n"
     "hosts and the others join. A local startup leaves joining to game code.\n"
     "Script input can name a player: 120 player 2 key d down.\n"
     "Captures gain player numbers, like fight-2.png. With --players 1, filenames\n"
     "have no number. Cannot combine with --host, --join or --connect.", RUN},
    {"--project", "--project <name-or-id>", "Select or relink the deployment project",
     "Overrides tide.deploy.json and saves the selected project after deploy.\n"
     "An explicit missing name creates a Small/one-match project; an invalid\n"
     "or inaccessible ID fails without creating another project. Bundle-only\n"
     "builds ignore the saved deployment link.", DEPLOY},
    {"--server-only", "--server-only", "Deploy only the managed WASM server",
     "Omits the client build from the local release bundle.", DEPLOY},
    {"--bundle-only", "--bundle-only", "Build a deployment bundle without uploading",
     "Needs no login and ignores tide.deploy.json. Can combine with --server-only.", DEPLOY},
    {"--project", "--project <folder>", "Choose the native editor's game root",
     "Overrides discovery of the nearest enclosing tide.packages folder,\n"
     "or the prefab's folder when none exists.", OPEN},
    {"--check", "--check", "Validate a prefab without opening a window",
     "Uses front-end validation and asset metadata, without a full game build,\n"
     "even with --preview. Returns 1 for project errors.", OPEN},
    {"--preview", "--preview", "Enable compiled views on authored match entities",
     "Runs only RunInEditor and EditorOnly views, without gameplay\n"
     "initialization or systems. Requires the full native build toolchain.\n"
     "Bounded captures wait for the build and return 1 on failure.", OPEN},
    {"--local-preview", "--local-preview", "Enable compiled views with local placement",
     "Like --preview, using local entities instead of match placement.", OPEN},
    {"--nightly", "--nightly", "Follow nightly Tide versions",
     "Saves the nightly channel for future upgrades.", UPGRADE},
    {"--stable", "--stable", "Follow stable Tide versions",
     "Saves the stable channel for future upgrades.", UPGRADE},
    {"--version", "--version <v>", "Install an exact Tide version",
     "Example: tide upgrade --version 0.4.0. Use tide version to inspect\n"
     "the current installation; tide --version is also accepted.", UPGRADE},
    {"setting", "<on|off|status>", "Turn usage data on or off, or show it",
     "off also deletes events not sent yet; on sends again from the next command.\n"
     "status says whether anything is sent and why not, what an event holds,\n"
     "where it goes, the install ID and the settings folder.", TELEMETRY},
    {"--help", "--help, -h", "Show help for a command",
     "Use tide <command> --help or tide <command> -h. For argument details,\n"
     "use tide help <argument> or tide help <command> <argument>.", FOLDERS | LOGIN | LOGOUT | ADD | UPDATE | EJECT | EDITORS | OPEN | UPGRADE | VERSION | HELP | TELEMETRY},
};

static const command_help *find_command(const char *name)
{
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++)
        if (strcmp(commands[i].name, name) == 0) return &commands[i];
    return NULL;
}

static void overview(const char *version)
{
    printf("tide %s: builds and runs Tide games.\n\n"
           "usage: tide <command> [arguments] [options]\n\n", version);
    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++)
        printf("  %-10s %s\n", commands[i].name, commands[i].summary);
    puts("\nUse `tide help <command>` or `tide help <argument>` for details.\n"
         "Examples: tide help run, tide help --frames, tide help deploy --project");
}

static void command_details(const command_help *command)
{
    printf("usage: tide %s\n\n%s\n\n%s\n", command->usage, command->summary, command->details);
    bool any = false;
    for (size_t i = 0; i < sizeof arguments / sizeof arguments[0]; i++) {
        const argument_help *arg = &arguments[i];
        if (!(arg->mask & command->mask)) continue;
        if (!any) puts("\nArguments and options:");
        any = true;
        printf("  %-27s %s\n", arg->usage, arg->summary);
    }
    if (any) printf("\nUse `tide help %s <argument>` for defaults, limits and examples.\n", command->name);
}

int tide_help(const int count, const char *const *topics, const char *version)
{
    if (!count) { overview(version); return 0; }
    if (count > 2) {
        fputs("tide: help takes a command or argument, or a command followed by an argument\n"
              "  = note: try `tide help run` or `tide help run --frames`\n", stderr);
        return 2;
    }
    const command_help *command = find_command(topics[0]);
    if (count == 1 && command) { command_details(command); return 0; }
    if (count == 2 && !command) {
        fprintf(stderr, "tide: unknown help command '%s'; see `tide help`\n", topics[0]);
        return 2;
    }
    const char *name = topics[count - 1];
    if (strcmp(name, "-h") == 0) name = "--help";
    bool found = false;
    for (size_t i = 0; i < sizeof arguments / sizeof arguments[0]; i++) {
        const argument_help *arg = &arguments[i];
        if (strcmp(arg->name, name) || (command && !(command->mask & arg->mask))) continue;
        if (found) putchar('\n');
        printf("%s\n%s\n\n%s\n\nCommands:", arg->usage, arg->summary, arg->details);
        for (size_t j = 0; j < sizeof commands / sizeof commands[0]; j++)
            if (arg->mask & commands[j].mask) printf(" %s", commands[j].name);
        putchar('\n');
        found = true;
    }
    if (found) return 0;
    if (command)
        fprintf(stderr, "tide: unknown argument '%s' for %s; see `tide help %s`\n", name, command->name, command->name);
    else
        fprintf(stderr, "tide: unknown help topic '%s'; use `tide help` for commands or name an argument like --frames\n", name);
    return 2;
}
