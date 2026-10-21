#include "build.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "android.h"
#include "apk.h"
#include "common.h"
#include "compile.h"
#include "fetch.h"
#include "folders.h"
#include "install.h"
#include "libraries.h"
#include "packages.h"
#include "serve.h"
#include "sys.h"
#include "toolchain.h"
#include "unity_assets.h"
#include "tide/sha256.h"

#ifdef TIDE_EMBEDDED_CLANG
#include <zlib.h>
#endif

#ifndef TIDE_VERSION
#define TIDE_VERSION "0.0.0-dev"
#endif

// A growable argv for sys_run, ending with NULL.
typedef struct args {
    const char **items;
    int count;
    int cap;
} args;

static void arg(args *a, const char *s)
{
    if (a->count + 2 > a->cap) {
        a->cap = a->cap * 2 + 16;
        a->items = realloc(a->items, sizeof(char *) * (size_t)a->cap);
        if (!a->items) abort();
    }
    a->items[a->count++] = s;
    a->items[a->count] = NULL;
}

static void arg_list(args *a, const char *const *list)
{
    for (int i = 0; list[i]; i++) arg(a, list[i]);
}

static char *format(const char *fmt, const char *a, const char *b)
{
    const size_t n = strlen(fmt) + strlen(a) + (b ? strlen(b) : 0) + 1;
    char *out = malloc(n);
    if (!out) abort();
    snprintf(out, n, fmt, a, b ? b : "");
    return out;
}

// ---------------------------------------------------------------------------
// The game's files

typedef struct file_list {
    char **items;
    int count;
} file_list;

static void add_file(void *user, const char *path)
{
    file_list *list = user;
    list->items = realloc(list->items, sizeof(char *) * (size_t)(list->count + 1));
    if (!list->items) abort();
    const size_t n = strlen(path) + 1;
    list->items[list->count] = malloc(n);
    if (!list->items[list->count]) abort();
    memcpy(list->items[list->count++], path, n);
}

static void free_files(file_list *list)
{
    for (int i = 0; i < list->count; i++) free(list->items[i]);
    free(list->items);
    *list = (file_list){0};
}

static int path_order(const void *a, const void *b)
{
    return path_compare(*(const char *const *)a, *(const char *const *)b);
}

// The .tide files in `folder` and its subfolders, but those of games and
// packages of their own (subfolders with a tide.packages), by path.
static file_list tide_files(const char *folder)
{
    file_list list = {0};
    char *dir = path_join(folder, "");
    folder_find_own(dir, ".tide", add_file, &list);
    folder_find_own(dir, ".tprefab", add_file, &list);
    free(dir);
    if (list.count > 0) qsort(list.items, (size_t)list.count, sizeof(char *), path_order);
    return list;
}

// The game's own files of other kinds (its C, its headers, its libraries):
// not in a hidden folder, like .tide/, where tide keeps what it makes, or
// .build/, where builds go (folder_find_own never looks in one).
typedef struct own_files {
    const char *const *extensions;
    file_list list;
} own_files;

static bool has_extension(const char *path, const char *extension)
{
    const size_t n = strlen(path);
    const size_t e = strlen(extension);
    if (n <= e) return false;
    for (size_t i = 0; i < e; i++) {
        char ch = path[n - e + i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a'); // STEAM_API64.LIB
        if (ch != extension[i]) return false;
    }
    return true;
}

static void add_own_file(void *user, const char *path)
{
    own_files *o = user;
    bool wanted = false;
    for (int i = 0; o->extensions[i] && !wanted; i++) wanted = has_extension(path, o->extensions[i]);
    if (wanted) add_file(&o->list, path);
}

static file_list own_files_of(const char *folder, const char *const *extensions)
{
    char *dir = path_join(folder, "");
    own_files o = {extensions, {0}};
    folder_find_own(dir, "", add_own_file, &o); // Every file, once
    free(dir);
    return o.list;
}

// The game's C, and its C++, which builds with no C++ runtime (cpp_flags).
static const char *const cpp_extensions[] = {".cpp", ".cc", ".cxx", NULL};
static const char *const source_extensions[] = {".c", ".cpp", ".cc", ".cxx", NULL};
static const char *const header_extensions[] = {".h", ".hpp", ".hh", ".hxx", ".inl", NULL};
static const char *const library_extensions[] = {".a", ".lib", ".so", ".dll", ".dylib", NULL};
// Whatever a build of the game reads, besides its .tide files: tide run builds
// again when one changes.
static const char *const c_side_extensions[] = {".c",   ".cpp", ".cc", ".cxx", ".h",  ".hpp", ".hh",
                                                ".hxx", ".inl", ".a",  ".lib", ".so", ".dll", ".dylib", NULL};

static bool is_cpp(const char *path)
{
    for (int i = 0; cpp_extensions[i]; i++) {
        if (has_extension(path, cpp_extensions[i])) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Packages (packages.h): those from git are downloaded the first time a build
// needs them.

// Says what's wrong with a tide.packages, as tidec says what's wrong with code.
static void report_package(void *user, const char *where, const int line, const char *message, const char *note)
{
    (void)user;
    if (line > 0) fprintf(stderr, "%s:%d: error: %s\n", where, line, message);
    else fprintf(stderr, "%s: error: %s\n", where, message);
    if (note) fprintf(stderr, "  = note: %s\n", note);
}

// The packages of the game in `folder`, into `out`. False after saying
// what's wrong, which includes `folder` being a package rather than a game.
static bool find_packages(const char *folder, game_packages *out)
{
    packages_free(out);
    if (!packages_resolve(folder, out, fetch_package, report_package, NULL)) return false;
    if (out->file.name) {
        fprintf(stderr, "tide: %s is package %s, not a game\n", folder, out->file.name);
        fprintf(stderr, "  = note: to try it, make a game in a folder of its own whose tide.packages lists this "
                        "folder, like `..` from a folder in it\n");
        return false;
    }
    return true;
}

// Every .tide file of the game, in the order they compile: each package's,
// in the packages' order, then the game's own, each by path. `paths` holds
// the paths. NULL after saying the game has no files of its own.
static compile_input *game_inputs(const char *folder, const game_packages *packages, file_list *paths, int *count)
{
    *paths = (file_list){0};
    const char **names = malloc(sizeof(char *) * 1); // Each path's package
    if (!names) abort();
    int game_start = 0;
    for (int p = 0; p <= packages->count; p++) {
        const char *name = p < packages->count ? packages->items[p].name : NULL;
        if (!name) game_start = paths->count;
        file_list files = tide_files(name ? packages->items[p].folder : folder);
        names = realloc(names, sizeof(char *) * (size_t)(paths->count + files.count + 1));
        if (!names) abort();
        for (int i = 0; i < files.count; i++) {
            names[paths->count] = name;
            add_file(paths, files.items[i]);
        }
        free_files(&files);
    }
    if (game_start == paths->count) {
        fprintf(stderr, "tide: there are no .tide files in %s\n", folder);
        fprintf(stderr, "  = note: a game is every .tide file in its folder; add one, like game.tide\n");
        free_files(paths);
        free(names);
        return NULL;
    }
    compile_input *inputs = malloc(sizeof(compile_input) * (size_t)paths->count);
    if (!inputs) abort();
    for (int i = 0; i < paths->count; i++) inputs[i] = (compile_input){paths->items[i], names[i], NULL};
    free(names);
    *count = paths->count;
    return inputs;
}

char *tide_work_folder(const char *folder)
{
    char *tide_dir = path_join(folder, ".tide");
    sys_mkdirs(tide_dir);
    sys_hide(tide_dir);
    char *ignore = path_join(tide_dir, ".gitignore");
    if (!sys_exists(ignore)) sys_write_text(ignore, "# Made by tide; safe to delete.\n*\n");
    free(ignore);
    return tide_dir;
}

// Where a build with no -o puts `file`: the game's .build/, which git ignores,
// as it does .tide/. Hidden folders are no part of a game; builds went in
// build/ before, which is a folder of the game like any other now.
static char *default_output(const char *folder, const char *file)
{
    char *old = format("%s/build/%s", folder, file);
    if (sys_exists(old)) {
        fprintf(stderr, "tide: build/%s is an older tide's build: builds go in .build/ now, and build/ is part of the "
                        "game like any folder, so delete it\n", file);
    }
    free(old);
    char *dir = path_join(folder, ".build");
    sys_mkdirs(dir);
    char *ignore = path_join(dir, ".gitignore");
    if (!sys_exists(ignore)) sys_write_text(ignore, "# Made by tide; safe to delete.\n*\n");
    free(ignore);
    char *output = path_join(dir, file);
    free(dir);
    return output;
}

// Adds what a build reads of the game or package in `dir` (ending in '/') to
// `list`: its tide.packages, its .tide files and its C side, each as `under`
// (the folder's path from the game's, ending in '/' or empty) and its path
// from `dir`. What tide made itself (.tide/, .build/) is no part of a game, as
// no hidden folder is.
static void add_sources_of(file_list *list, const char *dir, const char *under)
{
    file_list found = tide_files(dir);
    file_list c = own_files_of(dir, c_side_extensions);
    for (int i = 0; i < c.count; i++) add_file(&found, c.items[i]);
    free_files(&c);
    char *packages = path_join(dir, PACKAGES_FILE);
    if (sys_exists(packages)) add_file(&found, packages);
    free(packages);
    const size_t prefix = strlen(dir);
    for (int i = 0; i < found.count; i++) {
        const char *relative = found.items[i] + prefix;
        while (*relative == '/') relative++;
        char *path = format("%s%s", under, relative);
        add_file(list, path);
        free(path);
    }
    free_files(&found);
}

// The same on every machine, whatever its file system does with case.
static int byte_order(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

char **tide_game_sources(const char *folder_arg, int *count)
{
    *count = 0;
    char *folder = path_absolute(folder_arg);
    game_packages packages = {0};
    if (!find_packages(folder, &packages)) {
        free(folder);
        return NULL;
    }
    file_list sources = {0};
    char *own = path_join(folder, "");
    add_sources_of(&sources, own, "");
    free(own);
    free(folder);
    // The packages in folders: those from git are named by their commit, and tide's own come with tide
    const char *game = packages.file.folder;
    bool ok = true;
    for (int p = 0; p < packages.count; p++) {
        const package *package = &packages.items[p];
        if (!package->line->local) continue;
        if (strncmp(package->folder, game, strlen(game)) != 0) {
            fprintf(stderr, "%s:%d: error: %s is in a folder outside the game's, so it can't be published with it\n",
                    packages.file.path, package->line->line, package->name);
            fprintf(stderr, "  = note: list it from git, by its repository and commit, or move its folder into the game's\n");
            ok = false;
            continue;
        }
        add_sources_of(&sources, package->folder, package->folder + strlen(game));
    }
    packages_free(&packages);
    if (!ok) {
        free_files(&sources);
        return NULL;
    }
    if (sources.count > 0) qsort(sources.items, (size_t)sources.count, sizeof(char *), byte_order);
    *count = sources.count;
    if (!sources.items) sources.items = calloc(1, sizeof(char *)); // A folder with nothing to send isn't this function's to refuse
    if (!sources.items) abort();
    return sources.items;
}

// The folder's name, as a window title and a file name.
static const char *game_name(const char *folder)
{
    const char *name = path_base(folder);
    return name[0] ? name : "game";
}

// `text` in a C string literal.
static void c_string(char *out, const size_t size, const char *text)
{
    size_t n = 0;
    for (const char *p = text; *p && n + 3 < size; p++) {
        if (*p == '"' || *p == '\\') out[n++] = '\\';
        out[n++] = *p;
    }
    out[n] = '\0';
}

// The window's title in a tide_run_desc: `.title = "..."` when --title gave
// one, which goes over the game's title setting, or else the game's name,
// which only stands in when the game has none.
static void title_field(char *out, const size_t size, const build_options *opts, const char *name)
{
    char escaped[512];
    c_string(escaped, sizeof escaped, opts->title ? opts->title : name);
    snprintf(out, size, "%s = \"%s\"", opts->title ? ".title" : ".game_name", escaped);
}

// The generated host: the game in a window, through tide/run.h. The same as
// tide_add_game's (cmake/Tide.cmake).
static bool write_main(const char *path, const build_options *opts, const char *name)
{
    if (opts->managed) return sys_write_text(path, "#include \"game.h\"\n#include \"tide/managed_run.h\"\n");
    if (opts->server) {
        char server[1024], escaped[512];
        c_string(escaped, sizeof escaped, name);
        snprintf(server, sizeof server,
                 "// Generated by tide. Do not edit.\n#include \"game.h\"\n#include \"tide/server_run.h\"\n"
                 "int main(int argc, char **argv) { return tide_server_run(argc, argv, \"%s-server\"); }\n", escaped);
        return sys_write_text(path, server);
    }
    char title[600];
    title_field(title, sizeof title, opts, name);
    char text[1024];
    snprintf(text, sizeof text,
             "// Generated by tide. Do not edit.\n"
             "#include \"game.h\"\n"
             "#include \"tide/run.h\"\n"
             "\n"
             "int main(int argc, char **argv)\n"
             "{\n"
             "    tide_run(&(tide_run_desc){%s, .stats = %s, .stats_open = %s, .debug_views = %s, .argc = argc,\n"
             "                              .argv = argv});\n"
             "}\n",
             title, opts->stats ? "true" : "false", opts->stats_open ? "true" : "false",
             opts->debug_views ? "true" : "false");
    return sys_write_text(path, text);
}

// ---------------------------------------------------------------------------
// Compiler flags

// Determinism (see AGENTS.md): no fast-math and no contraction, in this order.
static const char *const common_flags[] = {"-fno-fast-math", "-ffp-contract=off", "-w", NULL};
static const char *const c_flags[] = {"-std=c17", NULL};
// C++ is the language alone: tide brings no C++ runtime (see AGENTS.md,
// Language), so no standard library (its headers aren't looked for, the
// system's neither, so the same code builds on every platform), no exceptions
// and no RTTI. A function's static with a constructor is made without the
// runtime's lock (__cxa_guard_acquire).
static const char *const cpp_flags[] = {"-std=c++20",      "-nostdinc++",             "-fno-exceptions",
                                        "-fno-rtti",       "-fno-threadsafe-statics", NULL};
static const char *const debug_flags[] = {"-O0", "-g", NULL};
static const char *const engine_debug_flags[] = {"-O2", "-g", NULL};
static const char *const release_flags[] = {"-O2", "-DNDEBUG", NULL};
// These compile flags are part of every object's stamp. The link runs on
// every build, so changing its collection policy cannot reuse an old program.
static const char *const release_sections[] = {"-ffunction-sections", "-fdata-sections", NULL};

#ifdef _WIN32
// Windows games use the MinGW-w64 target, whose C runtime comes with tide (as
// in cmake/mingw-toolchain.cmake): Microsoft's can't be redistributed. Its C
// library is the UCRT, part of Windows, so games need no DLLs of their own.
// The package holds exactly these libraries (platform/CMakeLists.txt).
#define NATIVE_TARGET "--target=x86_64-w64-windows-gnu"
#define NATIVE_RUNTIME "mingw"
// GLFW, in the platform layer, loads OpenGL at run time.
static const char *const native_libs[] = {"-lmingw32", "-lmingwex", "-lmoldname", "-lmsvcrt", "-lkernel32",
                                          "-luser32", "-lgdi32", "-lshell32", "-ladvapi32", "-lws2_32",
                                          "-lsecur32", "-lole32", NULL};
#define EXE_SUFFIX ".exe"
#define LIBRARY_SUFFIX ".dll"
#elif defined(__APPLE__)
// What GLFW, in the platform layer, links on macOS, and Security, for TLS to
// the relay.
static const char *const native_libs[] = {"-framework", "Cocoa", "-framework", "IOKit", "-framework", "CoreFoundation",
                                          "-framework", "Security", "-framework", "AudioToolbox", "-framework", "CoreAudio",
                                          NULL};
#define EXE_SUFFIX ""
#define LIBRARY_SUFFIX ".dylib"
#else
// GLFW, in the platform layer, loads X11 and OpenGL at run time, so games
// link neither.
static const char *const native_libs[] = {"-lm", "-lpthread", "-ldl", "-lrt", NULL};
#define EXE_SUFFIX ""
#define LIBRARY_SUFFIX ".so"
#endif

// Web builds: clang's own wasm target, with threads, and the package's
// wasi-libc (as in cmake/wasi-toolchain.cmake and TideFlags.cmake). The page's
// JavaScript implements the GL functions the platform imports (the only
// functions a program may leave undefined: see page_imports_flag), and makes
// the memory, which it shares with workers when it can.
// Threads are spelled out (-pthread, --shared-memory) for an installed clang
// older than tide's, which doesn't take them from the target's name.
static const char *const web_link_flags[] = {"-Wl,-z,stack-size=1048576", "-Wl,--import-memory",
                                             "-Wl,--export-memory",       "-Wl,--shared-memory",
                                             "-Wl,--max-memory=4294967296", NULL};

// The target, when it isn't the system's: its C library comes with tide, in
// <root>/<runtime>/sysroot, and its compiler runtime is passed by path, since
// clang looks for it in its own installation.
typedef struct target {
    const char *flag;   // --target=..., or NULL for the system's
    char *sysroot_flag; // --sysroot=...
    char *builtins;     // The compiler runtime
} target;

static target build_target;

static bool find_target(const char *root, const build_options *opts)
{
    const char *runtime = NULL;
    if (opts->android) return true; // Each of its CPUs, as it's built (build_android)
    if (opts->web) {
        build_target.flag = opts->managed ? "--target=wasm32-wasip1" : "--target=wasm32-wasip1-threads";
        runtime = "wasi";
    }
#ifdef NATIVE_TARGET
    else {
        build_target.flag = NATIVE_TARGET;
        runtime = NATIVE_RUNTIME;
    }
#endif
    if (!runtime) return true;
    char *dir = path_join(root, runtime);
    build_target.sysroot_flag = format("--sysroot=%s/sysroot", dir, NULL);
    build_target.builtins = opts->web ? path_join(dir, "libclang_rt.builtins.a")
                                      : path_join(dir, "libclang_rt.builtins-x86_64.a");
    const bool found = sys_exists(build_target.builtins);
    if (!found) {
        fprintf(stderr, "tide: this installation can't build %s games: %s is missing\n", opts->web ? "web" : "native",
                dir);
        fprintf(stderr, "  = note: reinstall tide, or for a build of this repo package the %s preset too\n",
                opts->web ? "web-package" : "mingw-release");
    }
    free(dir);
    return found;
}

// ---------------------------------------------------------------------------
// A build: what it needs, found once

typedef struct build {
    const char *root;
    const build_options *opts;
    bool library; // For interactive `tide run`: the game is a library, which a host reloads (see tide/host.h)
    int abi;      // For Android: which of its CPUs is being built (android_abis)
    char *folder;
    const char *name;
    char *include;
    char *compiler;
    char *wasm_ld; // Web builds with an installed clang
    char *cache;   // <folder>/.tide/<configuration>
    uint64_t headers; // The installed headers, which every compiled file can include
    int jobs;         // How many files compile at once
    bool engine_built;
    game_info info; // Its settings, from its last generate
    sb externs;     // ...and the C function of each of its extern functions, a line each
    file_list engine;       // The engine's objects
    game_packages packages; // Found again by each build of the game
    char *unity_map;        // With opts->unity_assets: the map of its UnityEngine.Source numbers, from its last generate (unity_assets.h)
} build;

static uint64_t header_stamp(const char *folder);

// Game code stays unoptimized for debugging; the engine keeps its speed in
// every configuration, with debug information and assertions in development.
static void config_flags(args *a, const build *b, const bool cpp, const bool engine)
{
    if (b->opts->server) arg(a, "-DTIDE_SERVER_BUILD=1");
    if (build_target.flag) {
        arg(a, build_target.flag);
        arg(a, build_target.sysroot_flag);
    }
    if (b->opts->web) {
        if (!b->opts->managed) arg(a, "-pthread");
        arg(a, "-msimd128"); // WebAssembly's SIMD, the same on every machine (not relaxed SIMD)
    }
    arg_list(a, cpp ? cpp_flags : c_flags);
    arg_list(a, common_flags);
    arg_list(a, b->opts->release ? release_flags : engine ? engine_debug_flags : debug_flags);
    if (b->opts->release || b->opts->server) arg_list(a, release_sections);
    // A library's code works wherever it's loaded, and Android's games are libraries.
    // On Linux and Android, a function whose locals take more than a page
    // touches each page as it makes room for them (-fstack-clash-protection):
    // a stack that runs out, as recursion with no end does, then ends at its
    // guard page, and never past it in memory that's something else's (see
    // AGENTS.md, Language). Windows' targets check big frames by themselves.
#if !defined(_WIN32) && !defined(__APPLE__)
    if (b->library || b->opts->android) arg(a, "-fPIC");
    if (!b->opts->web) arg(a, "-fstack-clash-protection");
#else
    if (b->opts->android) {
        arg(a, "-fPIC");
        arg(a, "-fstack-clash-protection");
    }
#endif
#ifdef _WIN32
    // Debug info Visual Studio's debugger reads, in a .pdb next to the game.
    if (!b->opts->web && !b->opts->android && !b->opts->release) arg(a, "-gcodeview");
#endif
}

// The C compiler: an installed clang, or tide itself as `tide cc` when clang
// is built in (compiler/cli/llvm).
static void arg_compiler(args *a, const char *compiler)
{
    arg(a, compiler);
#ifdef TIDE_EMBEDDED_CLANG
    arg(a, "cc");
#endif
}

// One line with everything that decides the engine objects, and the game's
// C and C++ ones, so a change in any of it rebuilds them.
static char *stamp_of(const build *b, const bool engine)
{
    args a = {0};
    config_flags(&a, b, false, engine);
    arg_list(&a, cpp_flags);
    // clang also reads header paths and the macOS SDK from its environment.
    // Changing those must not reuse objects compiled against other headers.
    static const char *const names[] = {"SDKROOT", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
                                      "MACOSX_DEPLOYMENT_TARGET"};
    char *environment[sizeof names / sizeof names[0]];
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *value = getenv(names[i]);
        environment[i] = format("%s=%s", names[i], value);
        arg(&a, environment[i]);
        arg(&a, value ? "set" : "unset");
    }
    size_t n = strlen(b->compiler) + strlen(TIDE_VERSION) + 40;
    for (int i = 0; i < a.count; i++) n += strlen(a.items[i]) + 1;
    char *stamp = malloc(n);
    if (!stamp) abort();
    snprintf(stamp, n, "%s %s %016llx %016llx", TIDE_VERSION, b->compiler,
             (unsigned long long)sys_file_stamp(b->compiler), (unsigned long long)b->headers);
    for (int i = 0; i < a.count; i++) {
        strcat(stamp, " ");
        strcat(stamp, a.items[i]);
    }
    free(a.items);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) free(environment[i]);
    return stamp;
}

// ---------------------------------------------------------------------------
// C++ with no runtime: what the compiler or the linker said for want of one,
// explained. clang's own words go to the terminal as they are; a link that
// failed runs once more for them as text, and a compile's are kept as it runs.

// What the program `argv` says, errors too. To free().
static char *said(const char *const *argv)
{
    const size_t size = 1u << 16;
    char *text = malloc(size);
    if (!text) abort();
    if (sys_capture_all(argv, text, size) == -1) text[0] = '\0';
    return text;
}

static bool says_any(const char *text, const char *const *words)
{
    for (int i = 0; words[i]; i++) {
        if (strstr(text, words[i])) return true;
    }
    return false;
}

#define NO_RUNTIME                                                                                                     \
    "a game's C++ builds with no C++ runtime: no standard library, exceptions or RTTI. Code that needs them comes "   \
    "as a prebuilt library behind a C API"

// C's headers, which C++ also has under names of its own: <cstdint> is <stdint.h>.
static const char *const c_headers[] = {"assert", "ctype",  "errno",  "fenv",   "float",  "inttypes", "limits", "locale",
                                        "math",   "setjmp", "signal", "stdarg", "stddef", "stdint",   "stdio",  "stdlib",
                                        "string", "time",   "uchar",  "wchar",  "wctype", NULL};

// Whether a header that wasn't found is the C++ standard library's: those
// have no extension and no folder, like <vector>.
static bool standard_header(const char *name, const size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (!islower((unsigned char)name[i]) && !isdigit((unsigned char)name[i]) && name[i] != '_') return false;
    }
    return len > 0;
}

// A C++ file that didn't compile, by what the compiler said (`text`): a header
// of the standard library, which isn't there, or exceptions or RTTI, which
// are off.
static void explain_cpp_compile(const char *text)
{
    bool note = false;
    static const char not_found[] = "' file not found";
    for (const char *at = text; (at = strstr(at, not_found)); at += sizeof not_found - 1) {
        const char *name = at;
        while (name > text && name[-1] != '\'') name--;
        const size_t len = (size_t)(at - name);
        if (name == text || !standard_header(name, len)) continue;
        const char *c_header = NULL;
        for (int i = 0; c_headers[i] && !c_header; i++) {
            if (name[0] == 'c' && strlen(c_headers[i]) == len - 1 && memcmp(c_headers[i], name + 1, len - 1) == 0) {
                c_header = c_headers[i];
            }
        }
        fprintf(stderr, "tide: <%.*s> is the C++ standard library's, which tide doesn't have\n", (int)len, name);
        if (c_header) fprintf(stderr, "  = note: include <%s.h>, C's header for the same\n", c_header);
        else note = true;
    }
    if (strstr(text, "with exceptions disabled")) {
        fprintf(stderr, "tide: a game's C++ builds without exceptions\n");
        note = true;
    }
    if (strstr(text, "requires -frtti")) {
        fprintf(stderr, "tide: a game's C++ builds without RTTI, so without typeid and dynamic_cast\n");
        note = true;
    }
    if (note) fprintf(stderr, "  = note: " NO_RUNTIME "\n");
}

// A link that failed for want of the C++ runtime: new and delete, which the
// game's C++ can define itself, or what a prebuilt library was built to need.
static void explain_cpp_link(const char *text)
{
    static const char *const allocation[] = {"operator new", "operator delete", "_Znw", "_Zna", "_Zdl", "_Zda", NULL};
    static const char *const runtime[] = {"std::",       "__cxa_guard_", "__cxa_throw",   "__cxa_begin_catch",
                                          "__cxa_allocate_exception",    "__gxx_personality", "_Unwind_",
                                          "typeinfo for", "__dynamic_cast", "__cxxabiv1", NULL};
    if (says_any(text, allocation)) {
        fprintf(stderr, "tide: the game's C++ uses new or delete, which tide has no C++ runtime to define\n");
        fprintf(stderr, "  = note: define the ones the linker names in one of the game's .cpp files, over malloc and "
                        "free, like `void *operator new(size_t size) { return malloc(size); }` and `void operator "
                        "delete(void *p) noexcept { free(p); }`. A class with a virtual destructor uses delete too\n");
    }
    if (strstr(text, "__cxa_pure_virtual")) {
        fprintf(stderr, "tide: a class with a pure virtual function needs __cxa_pure_virtual, which tide has no C++ "
                        "runtime to define\n");
        fprintf(stderr, "  = note: define it in one of the game's .cpp files: `extern \"C\" void __cxa_pure_virtual() "
                        "{ abort(); }`\n");
    }
    if (says_any(text, runtime)) {
        fprintf(stderr, "tide: a library the game links needs the C++ runtime, which tide doesn't have\n");
        fprintf(stderr, "  = note: " NO_RUNTIME ": build the library without them (-fno-exceptions, -fno-rtti, "
                        "-fno-threadsafe-statics, and no standard library), or bring it as a .dll, .so or .dylib, "
                        "which has its runtime with it\n");
    }
}

// ---------------------------------------------------------------------------
// Compiling: a build's files, several at a time
//
// The files a build has to compile are listed first, in the order tide
// compiles them (the engine's, the game's generated C, its own C, then each
// package's), and then compiled as many at once as the machine has
// processors. What that leaves is what compiling them one after another in
// that order leaves:
// - What the compilers say is kept, and said a file at a time in the files'
//   order, up to the first file that didn't compile. Nothing is said for the
//   files after that one.
// - No file after that one is started once it's known, and the files before
//   it all compile. The ones already on their way finish, and keep what they
//   made: an object is good for its source whatever else failed.
// So a build that fails says the same thing every time, whichever compiler
// was the quicker.

typedef struct compile_job {
    args a;           // The compiler's command line
    char *made[4];    // ...and the parts of it made for this file
    bool cpp;
    bool first;       // Started ahead of its turn: the game's own code, which takes the longest
    bool engine;      // One of the engine's files, which have one stamp for them all (compiles.engine_stamp)
    char *stamp_path; // The file that says what the object was made of, NULL for a file with none of its own:
    char *stamp;      // gone while the file compiles, so a compile that's cut short leaves no object that passes, and written once it has
    bool started;
    bool done;
    int code;   // The compiler's exit code; -1 if it couldn't start
    char *said; // What it said
} compile_job;

typedef struct compiles {
    compile_job *items;
    int count;
    int cap;
    char *engine_stamp_path; // Written once each of the engine's files listed here has compiled; NULL when none is
    char *engine_stamp;
} compiles;

static char *copy_text(const char *text)
{
    return format("%s", text, NULL);
}

// Lists one C or C++ file to compile; `gen`, the generated files' folder, may
// be NULL.
static compile_job *queue_compile(compiles *list, const build *b, const char *source, const char *object, const char *gen,
                                  const bool engine)
{
    if (list->count == list->cap) {
        list->cap = list->cap * 2 + 64;
        list->items = realloc(list->items, sizeof(compile_job) * (size_t)list->cap);
        if (!list->items) abort();
    }
    compile_job *job = &list->items[list->count++];
    *job = (compile_job){.cpp = is_cpp(source), .engine = engine};
    job->made[0] = copy_text(source);
    job->made[1] = copy_text(object);
    job->made[2] = format("-I%s", b->include, NULL);
    job->made[3] = gen ? format("-I%s", gen, NULL) : NULL;
    arg_compiler(&job->a, b->compiler);
    arg(&job->a, "-c");
    arg(&job->a, job->made[0]);
    arg(&job->a, "-o");
    arg(&job->a, job->made[1]);
    arg(&job->a, job->made[2]);
    if (job->made[3]) arg(&job->a, job->made[3]);
    config_flags(&job->a, b, job->cpp, engine);
    // The compiler's words are kept until its file's turn, so it can't see
    // that they're for a terminal: it's told. No part of an object's stamp.
    if (diag_colored()) {
        arg(&job->a, "-fcolor-diagnostics");
#ifdef _WIN32
        arg(&job->a, "-fansi-escape-codes"); // As escape sequences, which a pipe carries, not calls to the console
#endif
    }
    return job;
}

static void free_compiles(compiles *list)
{
    for (int i = 0; i < list->count; i++) {
        compile_job *job = &list->items[i];
        for (size_t m = 0; m < sizeof job->made / sizeof job->made[0]; m++) free(job->made[m]);
        free(job->a.items);
        free(job->stamp_path);
        free(job->stamp);
        free(job->said);
    }
    free(list->items);
    free(list->engine_stamp_path);
    free(list->engine_stamp);
    *list = (compiles){0};
}

// The next file to start among those before `stop`: one that goes ahead of
// its turn, or else the first in order. -1 when none is left.
static int next_compile(const compiles *list, const int stop)
{
    int next = -1;
    for (int i = 0; i < stop; i++) {
        const compile_job *job = &list->items[i];
        if (job->started) continue;
        if (job->first) return i;
        if (next < 0) next = i;
    }
    return next;
}

// Says what the compilers of the files that are done said, in the files'
// order: `shown` is how many have had their turn, which stops at the first
// that didn't compile.
static void show_compiles(const build *b, compiles *list, int *shown)
{
    while (*shown < list->count && list->items[*shown].done) {
        compile_job *job = &list->items[*shown];
        if (job->said) {
            // As the compiler would have printed it: a line ends once, and
            // stderr ends each as the system does.
            char *to = job->said;
            for (const char *from = job->said; *from; from++) {
                if (from[0] != '\r' || from[1] != '\n') *to++ = *from;
            }
            *to = '\0';
            fputs(job->said, stderr);
        }
        if (job->code == -1) fprintf(stderr, "tide: couldn't start %s\n", b->compiler);
        else if (job->code > 0 && job->cpp) explain_cpp_compile(job->said ? job->said : "");
        if (job->code != 0) {
            *shown = list->count; // Nothing after it has a turn
            return;
        }
        (*shown)++;
    }
}

// Compiles the files listed, and empties the list. False once it has said
// what's wrong with the first that didn't compile.
static bool run_compiles(build *b, compiles *list)
{
    sys_process *running[SYS_AT_ONCE];
    int job_of[SYS_AT_ONCE];
    int count = 0;
    int stop = list->count; // Files from here on aren't started: the first that failed
    int shown = 0;
    int engine_left = 0;
    for (int i = 0; i < list->count; i++) engine_left += list->items[i].engine;
    const int at_once = b->jobs > 0 ? b->jobs : 1;
    for (;;) {
        while (count < at_once) {
            const int next = next_compile(list, stop);
            if (next < 0) break;
            compile_job *job = &list->items[next];
            job->started = true;
            if (job->stamp_path) sys_remove(job->stamp_path);
            sys_process *process = sys_start_kept(job->a.items);
            if (!process) {
                job->done = true;
                job->code = -1;
                stop = next;
                continue;
            }
            running[count] = process;
            job_of[count++] = next;
        }
        show_compiles(b, list, &shown);
        if (count == 0) break;
        int code = 1;
        char *text = NULL;
        const int ended = sys_wait_any(running, count, &code, &text);
        if (ended < 0) {
            fprintf(stderr, "tide: the system couldn't wait for the compiler\n");
            for (int i = 0; i < count; i++) sys_kill(running[i]);
            stop = -1;
            break;
        }
        const int which = job_of[ended];
        compile_job *job = &list->items[which];
        running[ended] = running[count - 1];
        job_of[ended] = job_of[count - 1];
        count--;
        job->done = true;
        job->code = code;
        job->said = text;
        if (code != 0) {
            if (which < stop) stop = which;
            continue;
        }
        if (job->stamp_path) sys_write_text(job->stamp_path, job->stamp);
        if (job->engine && --engine_left == 0) {
            sys_write_text(list->engine_stamp_path, list->engine_stamp);
            b->engine_built = true;
        }
    }
    const bool ok = stop == list->count;
    free_compiles(list);
    return ok;
}

// Compiles one C or C++ file; `gen`, the generated files' folder, may be NULL.
static bool compile_c(build *b, const char *source, const char *object, const char *gen)
{
    compiles list = {0};
    queue_compile(&list, b, source, object, gen, false);
    return run_compiles(b, &list);
}

// Finds the compiler and makes the game's .tide folder. False after saying
// what's wrong.
static bool build_setup(build *b, const char *root, const build_options *opts, const bool library)
{
    b->root = root;
    b->opts = opts;
    b->library = library;
    b->folder = path_absolute(opts->folder);
    b->name = game_name(b->folder);
    b->include = path_join(root, "include");
    char *run_h = path_join(b->include, "tide/run.h");
    if (!sys_exists(run_h)) {
        fprintf(stderr, "tide: the engine files aren't next to tide, in %s\n", root);
        fprintf(stderr, "  = note: run `tide upgrade`, which installs them again, or for a build of this repo run "
                        "`cmake --install` first\n");
        return false;
    }
    free(run_h);
    // An upgrade that stopped partway leaves files of two versions, which
    // build nothing that works.
    if (!tide_installation_whole(root, TIDE_VERSION)) {
        fprintf(stderr, "tide: the installation in %s isn't whole: an upgrade of it stopped partway\n", root);
        fprintf(stderr, "  = note: run `tide upgrade`, which installs it again\n");
        return false;
    }

#ifdef TIDE_EMBEDDED_CLANG
    char *bin = sys_exe_dir();
    b->compiler = path_join(bin, "tide" EXE_SUFFIX);
    free(bin);
#ifdef __APPLE__
    // macOS's headers and libraries come with Apple's command line tools,
    // which clang finds through SDKROOT.
    if (!opts->web && !find_macos_sdk()) {
        fprintf(stderr, "tide: building for macOS needs Apple's command line tools\n");
        fprintf(stderr, "  = note: install them with `xcode-select --install`\n");
        return false;
    }
#endif
#else
    b->compiler = opts->web ? find_web_clang() : find_clang();
    if (!b->compiler) {
        fprintf(stderr, "tide: clang isn't installed, and tide needs it to build games\n");
        fprintf(stderr, "  = note: install LLVM (https://github.com/llvm/llvm-project/releases), add it to PATH, "
                        "or set LLVM_ROOT\n");
        return false;
    }
    if (opts->web) {
        b->wasm_ld = find_wasm_ld(b->compiler);
        if (!b->wasm_ld) {
            fprintf(stderr, "tide: web builds need wasm-ld, the WebAssembly linker, and it isn't next to clang\n");
            fprintf(stderr, "  = note: LLVM's releases include it; on Linux install your distribution's lld, and on "
                            "macOS `brew install llvm lld`\n");
            return false;
        }
    }
#endif
    if (!find_target(root, opts)) return false;
    b->headers = header_stamp(b->include);
    // Files compile as many at once as the machine has processors, or as
    // TIDE_JOBS says: for a machine short of memory, and for measuring.
    b->jobs = sys_processors();
    const char *asked = sys_env("TIDE_JOBS");
    if (asked) {
        char *end = NULL;
        const long jobs = strtol(asked, &end, 10);
        if (end == asked || *end || jobs < 1) {
            fprintf(stderr, "tide: TIDE_JOBS is '%s', which isn't how many files to compile at once\n", asked);
            fprintf(stderr, "  = note: set it to a whole number from 1 up, or leave it unset for as many as the machine has "
                            "processors\n");
            return false;
        }
        b->jobs = jobs > SYS_AT_ONCE ? SYS_AT_ONCE : (int)jobs;
    }
    if (b->jobs > SYS_AT_ONCE) b->jobs = SYS_AT_ONCE;

    // Everything tide makes goes in <folder>/.tide/<configuration>.
    char *tide_dir = tide_work_folder(b->folder);
    char config[32];
    snprintf(config, sizeof config, "%s-%s", opts->managed ? "managed" : opts->server ? "server" : opts->web ? "web" : opts->android ? "android" : "native",
             opts->release ? "release" : "debug");
    b->cache = path_join(tide_dir, config);
    free(tide_dir);
    sys_mkdirs(b->cache);
    return true;
}

// ---------------------------------------------------------------------------
// Engine objects, compiled once per configuration and kept

typedef struct engine_build {
    const build *b;
    const char *out_dir;
    bool rebuild;
    compiles *list;
    file_list objects;
} engine_build;

static void queue_engine_file(void *user, const char *source)
{
    engine_build *e = user;
    char name[512];
    snprintf(name, sizeof name, "%s.o", path_base(source));
    char *object = path_join(e->out_dir, name);
    if (e->rebuild || sys_mtime(object) < sys_mtime(source)) {
        queue_compile(e->list, e->b, source, object, NULL, true);
    }
    add_file(&e->objects, object);
    free(object);
}

// Lists the engine's files that have to compile: all of them when tide, the
// compiler or the flags changed, and otherwise those changed since their
// objects were made. Once for a list.
static void queue_engine(build *b, compiles *list)
{
    if (b->engine_built) return;
#if !defined(_WIN32) && !defined(__APPLE__)
    // Objects for a library have flags of their own (config_flags): kept apart,
    // so running and building don't rebuild each other's.
    char *engine_dir = path_join(b->cache, b->library ? "engine-pic" : "engine");
#else
    char *engine_dir = path_join(b->cache, "engine");
#endif
    sys_mkdirs(engine_dir);
    char *stamp_path = path_join(engine_dir, "stamp");
    char *engine_src = path_join(b->root, "src/engine/");
    // Private headers affect engine objects alone; game and user C objects
    // keep their ordinary public-header stamp.
    char *common_stamp = stamp_of(b, true);
    char private_headers[17];
    snprintf(private_headers, sizeof private_headers, "%016llx", (unsigned long long)header_stamp(engine_src));
    char *stamp = format("%s %s", common_stamp, private_headers);
    free(common_stamp);
    char *old_stamp = sys_read_file(stamp_path, NULL);
    engine_build engine = {b, engine_dir, !old_stamp || strcmp(old_stamp, stamp) != 0, list, {0}};
    const int listed = list->count;
    folder_find(engine_src, ".c", queue_engine_file, &engine);
    free(engine_src);
    b->engine = engine.objects;
    if (list->count == listed) {
        b->engine_built = true; // Nothing of it to compile: its stamp stands
        free(stamp_path);
        free(stamp);
    } else {
        // Some objects may be replaced before a later compile fails. Reverting
        // sources, headers or flags must not make that partial rebuild look
        // complete: the stamp is back once every file listed has compiled.
        sys_remove(stamp_path);
        list->engine_stamp_path = stamp_path;
        list->engine_stamp = stamp;
    }
    free(old_stamp);
    free(engine_dir);
}

// ---------------------------------------------------------------------------
// The game's C: the .c and .cpp files in its folder, which define its extern
// functions, and the prebuilt libraries there that were built for the target
// (see libraries.h).

typedef struct c_side {
    file_list objects; // Its .c and .cpp files, compiled
    file_list link;    // Its libraries for the target, as the linker takes them
    file_list dynamic; // ...those the program loads as it starts, which go next to it
    file_list names;   // On Android, the name each of those goes in the app by
    file_list unsure;  // On Android, ELF libraries for its CPU left out as Linux's
} c_side;

static void free_game_c(c_side *c)
{
    free_files(&c->objects);
    free_files(&c->link);
    free_files(&c->dynamic);
    free_files(&c->names);
    free_files(&c->unsure);
}

// Android's CPUs, in android_abis' order
static const unsigned android_cpus[ANDROID_ABIS] = {LIB_ARM64, LIB_X64};

static lib_platform target_platform(const build *b)
{
    if (b->opts->web) return LIB_WEB;
    if (b->opts->android) return LIB_ANDROID;
#ifdef _WIN32
    return LIB_WINDOWS;
#elif defined(__APPLE__)
    return LIB_MACOS;
#else
    return LIB_LINUX;
#endif
}

static unsigned target_cpu(const build *b)
{
    if (b->opts->web) return LIB_WASM32;
    if (b->opts->android) return android_cpus[b->abi];
#if defined(_WIN32) || defined(__x86_64__)
    return LIB_X64; // Windows games are always x86-64 (NATIVE_TARGET)
#elif defined(__aarch64__) || defined(__arm64__)
    return LIB_ARM64;
#else
    return LIB_OTHER_CPU;
#endif
}

static uint64_t hash_text(uint64_t h, const char *text)
{
    for (const char *p = text; *p; p++) h = (h ^ (uint8_t)*p) * 0x100000001B3ull;
    return h;
}

// A file's path and its stamp, which changes whenever it does.
static uint64_t hash_file(const uint64_t h, const char *path)
{
    return (hash_text(h, path) ^ sys_file_stamp(path)) * 0x100000001B3ull;
}

static uint64_t header_stamp(const char *folder)
{
    file_list headers = own_files_of(folder, header_extensions);
    uint64_t h = 0xCBF29CE484222325ull;
    for (int i = 0; i < headers.count; i++) h = hash_file(h, headers.items[i]);
    free_files(&headers);
    return h;
}

// tidec still checks the whole game, but its files are written again even
// when their contents haven't changed. Keep generated objects by contents,
// not modification times: a C-only edit needn't compile the game or launcher.
// The generated header is shared by both, so a layout change rebuilds both.
// Lists `source` when its object isn't what it would compile to, `first` for
// the one to start ahead of its turn. False when the generated files aren't
// there to read.
static bool queue_generated(const build *b, compiles *list, const char *source, const char *object, const char *gen,
                            const bool first)
{
    char *text = sys_read_file(source, NULL);
    char *header_path = path_join(gen, "game.h");
    char *header = sys_read_file(header_path, NULL);
    free(header_path);
    if (!text || !header) {
        free(text);
        free(header);
        return false;
    }
    char *flags = stamp_of(b, false);
    uint64_t h = hash_text(0xCBF29CE484222325ull, flags);
    h = hash_text(hash_text(h, source), text);
    // Only the managed reactor embeds this macro in its launcher. Ordinary
    // launchers use the game API's identity, so gameplay edits still reuse
    // their objects. Every other header byte remains a cache dependency.
    char *identity = !first && !b->opts->managed ? strstr(header, "\n#define TIDE_SIMULATION_ID ") : NULL;
    if (identity) {
        char *end = strchr(identity + 1, '\n');
        *identity = '\0';
        h = hash_text(h, header);
        h = hash_text(h, end ? end : "");
    } else {
        h = hash_text(h, header);
    }
    free(flags);
    free(text);
    free(header);
    char stamp[32];
    snprintf(stamp, sizeof stamp, "%016llx", (unsigned long long)h);
    char *stamp_path = format("%s.stamp", object, NULL);
    char *old = sys_read_file(stamp_path, NULL);
    if (!old || strcmp(old, stamp) != 0 || !sys_exists(object)) {
        // A failed compile may leave a partial object: never reuse it, even
        // if the source is then put back exactly as it was before the failure.
        // The stamp goes as the compile starts (run_compiles).
        compile_job *job = queue_compile(list, b, source, object, gen, false);
        job->stamp_path = stamp_path;
        job->stamp = copy_text(stamp);
        job->first = first;
    } else {
        free(stamp_path);
    }
    free(old);
    return true;
}

// Lists the .c and .cpp files of the game, or of one of its packages
// (`package`, its name), in `folder_path` to compile into a folder of the
// cache, each one again only when it, a header in that folder, or the flags
// changed: each object has a stamp of what made it. Objects for a library are
// kept apart on Linux, as the engine's are.
static void queue_c_files(const build *b, compiles *list, const char *folder_path, const char *package, c_side *out,
                           const char *gen)
{
    file_list sources = own_files_of(folder_path, source_extensions);
    if (sources.count == 0) return;
#if !defined(_WIN32) && !defined(__APPLE__)
    char *dir = path_join(b->cache, b->library ? "c-pic" : "c");
#else
    char *dir = path_join(b->cache, "c");
#endif
    if (package) {
        char sub[512];
        snprintf(sub, sizeof sub, "packages/%s", package);
        char *base = dir;
        dir = path_join(base, sub);
        free(base);
    }
    sys_mkdirs(dir);
    char *flags = stamp_of(b, false);
    uint64_t common = hash_text(0xCBF29CE484222325ull, flags);
    free(flags);
    if (b->opts->server) {
        char *header = path_join(gen, "game.h");
        common = hash_file(common, header);
        free(header);
    }
    file_list headers = own_files_of(folder_path, header_extensions);
    for (int i = 0; i < headers.count; i++) common = hash_file(common, headers.items[i]);
    free_files(&headers);
    char *folder = path_join(folder_path, "");
    const size_t prefix = strlen(folder);
    free(folder);
    for (int i = 0; i < sources.count; i++) {
        char name[512]; // lib/noise.c: lib_noise.c.o
        snprintf(name, sizeof name, "%s.o", sources.items[i] + prefix);
        for (char *p = name; *p; p++) {
            if (*p == '/') *p = '_';
        }
        char *object = path_join(dir, name);
        char *stamp_path = format("%s.stamp", object, NULL);
        char stamp[32];
        snprintf(stamp, sizeof stamp, "%016llx", (unsigned long long)hash_file(common, sources.items[i]));
        char *old = sys_read_file(stamp_path, NULL);
        if (!old || strcmp(old, stamp) != 0 || !sys_exists(object)) {
            compile_job *job = queue_compile(list, b, sources.items[i], object, b->opts->server ? gen : NULL, false);
            job->stamp_path = stamp_path;
            job->stamp = copy_text(stamp);
        } else {
            free(stamp_path);
        }
        add_file(&out->objects, object);
        free(old);
        free(object);
    }
    free(dir);
    free_files(&sources);
}

// The game's C, and its packages'.
static void queue_game_c(const build *b, compiles *list, c_side *out, const char *gen)
{
    queue_c_files(b, list, b->folder, NULL, out, gen);
    for (int p = 0; p < b->packages.count; p++) {
        queue_c_files(b, list, b->packages.items[p].folder, b->packages.items[p].name, out, gen);
    }
}

// Compiles what a build of the game is made of and isn't compiled yet: the
// engine, the game's generated C (`game_c`) and what starts it (`start_c`: its
// launcher, or its library's side of a host) into `objects`, and the game's
// own C and its packages', whose objects go in `c`.
static bool compile_game(build *b, const char *gen, const char *game_c, const char *start_c, const file_list *objects,
                         c_side *c)
{
    compiles list = {0};
    queue_engine(b, &list);
    if (!queue_generated(b, &list, game_c, objects->items[0], gen, true)
        || !queue_generated(b, &list, start_c, objects->items[1], gen, false)) {
        free_compiles(&list);
        return false;
    }
    queue_game_c(b, &list, c, gen);
    return run_compiles(b, &list);
}

// Links to the ELF shared library at `path` by its name, so the program asks
// for it by name, wherever the two are put.
static void link_by_name(c_side *out, const char *path)
{
    char *dir = path_dir(path);
    char *search = format("-L%s", dir, NULL);
    char *name = format("-l:%s", path_base(path), NULL);
    add_file(&out->link, search);
    add_file(&out->link, name);
    free(dir);
    free(search);
    free(name);
}

// The pages of Android's newer phones: a library loads there when its
// segments are aligned to them (the game's own is: link_android).
#define ANDROID_PAGE_SIZE 16384

// A shared library for the Android app: linked to, and put in the app beside
// the game's library under the name that one asks for it by, which is its
// soname, or its file's name when it has none. An app's libraries are named
// lib<name>.so, one of a name for each CPU. False after saying why it can't
// go in.
static bool add_android_library(const build *b, const char *path, const lib_info *info, c_side *out)
{
    const char *name = info->name[0] ? info->name : path_base(path);
    const size_t n = strlen(name);
    if (n < 7 || strncmp(name, "lib", 3) != 0 || strcmp(name + n - 3, ".so") != 0) {
        fprintf(stderr, "tide: %s can't go in an Android app: its libraries are named lib<name>.so, and this one's "
                        "name is %s\n", path, name);
        fprintf(stderr, info->name[0] ? "  = note: that's the name it was linked with, which the game asks for it by; "
                                        "link it again with -Wl,-soname,lib<name>.so\n"
                                      : "  = note: rename the file\n");
        return false;
    }
    if (strcmp(name, "libgame.so") == 0) {
        fprintf(stderr, "tide: %s can't go in an Android app: libgame.so is the game's own library there\n", path);
        return false;
    }
    for (int i = 0; i < out->names.count; i++) {
        if (strcmp(out->names.items[i], name) != 0) continue;
        fprintf(stderr, "tide: two libraries for %s are named %s: %s and %s\n", android_abis[b->abi], name,
                out->dynamic.items[i], path);
        fprintf(stderr, "  = note: an Android app holds one library of a name for each CPU\n");
        return false;
    }
    if (info->page_size && info->page_size < ANDROID_PAGE_SIZE) {
        fprintf(stderr, "tide: warning: %s is aligned for %u KiB pages, and phones with 16 KiB pages may not load it\n",
                path, (unsigned)(info->page_size / 1024));
        fprintf(stderr, "  = note: Google Play requires apps to run on those phones; build the library with NDK r28 or "
                        "newer, or link it with -Wl,-z,max-page-size=16384\n");
    }
    add_file(&out->dynamic, path);
    add_file(&out->names, name);
    link_by_name(out, path);
    return true;
}

// The libraries in `folder` (the game's or a package's) built for the target:
// static ones (and Windows import libraries) are linked in, and dynamic ones
// are linked to and go next to the program, which finds them there (on
// Android, in the app). A library tide can't read is left out, saying so; one
// for another platform or CPU is left out quietly. False after saying why a
// library for the target can't be used.
static bool find_libraries_in(const build *b, const char *folder, c_side *out)
{
    file_list libraries = own_files_of(folder, library_extensions);
    const lib_platform platform = target_platform(b);
    const unsigned cpu = target_cpu(b);
    bool dynamic = false;
    bool ok = true;
    for (int i = 0; i < libraries.count && ok; i++) {
        const char *path = libraries.items[i];
        const lib_info info = lib_identify_file(path);
        if (info.platform == LIB_UNKNOWN) {
            fprintf(stderr, "tide: left out %s: tide can't tell which platform it was built for\n", path);
            continue;
        }
        // Linux's and Android's are both ELF (see libraries.h): if the link
        // fails, one taken for Linux's may be why
        if (platform == LIB_ANDROID && info.platform == LIB_LINUX && (info.cpus & cpu)) add_file(&out->unsure, path);
        if (info.platform != platform || !(info.cpus & cpu)) continue;
        if (!info.dynamic) {
            add_file(&out->link, path);
            continue;
        }
        if (platform == LIB_ANDROID) {
            ok = add_android_library(b, path, &info, out);
            continue;
        }
        add_file(&out->dynamic, path);
        dynamic = true;
#ifdef _WIN32
        // Linked through its import library (a .lib, static above): the .dll only goes next to the game
#elif defined(__APPLE__)
        add_file(&out->link, path);
#else
        link_by_name(out, path); // So the program looks for it by name: next to itself
#endif
    }
#ifdef __APPLE__
    if (dynamic) add_file(&out->link, "-Wl,-rpath,@loader_path");
#elif !defined(_WIN32)
    if (dynamic) add_file(&out->link, "-Wl,-rpath,$ORIGIN");
#else
    (void)dynamic;
#endif
    free_files(&libraries);
    return ok;
}

// The game's libraries, and its packages'.
static bool find_libraries(const build *b, c_side *out)
{
    bool ok = find_libraries_in(b, b->folder, out);
    for (int p = 0; p < b->packages.count && ok; p++) ok = find_libraries_in(b, b->packages.items[p].folder, out);
    return ok;
}

// On a Mac, MoltenVK (Vulkan on Metal, from lib/native) goes beside the
// program too, which draws with it where the Mac has no Vulkan of its own
// (platform/desktop/glfw.c): only its slice for the game's CPU, half of the
// universal library. One already there and up to date stays.
static void place_moltenvk(const build *b, const char *dir)
{
    if (target_platform(b) != LIB_MACOS) return;
    char *from = path_join(b->root, "lib/native/libMoltenVK.dylib");
    char *to = path_join(dir, "libMoltenVK.dylib");
    size_t size = 0;
    char *data = sys_exists(from) ? sys_read_file(from, &size) : NULL;
    size_t at = 0, length = size;
    if (data && !lib_universal_slice((const unsigned char *)data, size, target_cpu(b), &at, &length)) at = 0, length = size;
    const bool current = data && sys_file_size(to) == (int64_t)length && sys_mtime(to) >= sys_mtime(from);
    if (data && !current && !sys_write_file(to, data + at, length)) {
        fprintf(stderr, "tide: can't put MoltenVK next to the game, at %s: the game draws with OpenGL where the Mac has no Vulkan\n", to);
    }
    free(data);
    free(from);
    free(to);
}

// Copies the game's dynamic libraries into `dir`, next to its program. Ones
// already there and up to date stay: the running game may have them open.
static void copy_dynamic(const c_side *c, const char *dir)
{
    for (int i = 0; i < c->dynamic.count; i++) {
        const char *from = c->dynamic.items[i];
        char *to = path_join(dir, path_base(from));
        const bool current = sys_file_size(to) == sys_file_size(from) && sys_mtime(to) >= sys_mtime(from);
        if (strcmp(from, to) != 0 && !current) {
            size_t len = 0;
            char *data = sys_read_file(from, &len);
            if (!data || !sys_write_file(to, data, len)) {
                fprintf(stderr, "tide: can't copy %s next to the game, to %s\n", from, to);
            }
            free(data);
        }
        free(to);
    }
}

static void write_import(void *user, const char *name, const size_t len)
{
    fprintf(user, "%.*s\n", (int)len, name);
}

// What a web program may leave for the page to define: the functions the
// prebuilt platform layer calls and doesn't define, which are GL's, from
// tide.js. Any other function nothing defines stops the link; allowing them
// all would leave it to fail when it's called. Lists them in a file next to
// `output`, and returns the linker's flag for it, to free().
static char *page_imports_flag(const char *output, const char *platform_lib)
{
    char *dir = path_dir(output);
    char *path = path_join(dir, "page-imports.txt");
    FILE *f = fopen(path, "wb");
    if (f) {
        size_t size = 0;
        char *archive = sys_read_file(platform_lib, &size);
        if (archive) wasm_archive_imports((const unsigned char *)archive, size, "env", write_import, f);
        free(archive);
        fclose(f);
    }
    char *flag = format("--allow-undefined-file=%s", path, NULL);
    free(dir);
    free(path);
    return flag;
}

// On the web, a C function of the game's that nothing defines: natively a
// library did, of a kind the web doesn't have.
static void explain_web_externs(const char *text, const sb *externs)
{
    if (!externs->data) return;
    static const char undefined[] = "undefined symbol: ";
    bool missing = false;
    for (const char *line = externs->data; *line;) {
        const char *end = strchr(line, '\n');
        const size_t len = (size_t)(end - line);
        for (const char *at = text; (at = strstr(at, undefined));) {
            at += sizeof undefined - 1;
            if (strncmp(at, line, len) != 0 || (at[len] != '\n' && at[len] != '\r' && at[len] != '\0')) continue;
            fprintf(stderr, "tide: nothing defines the C function %.*s for the web\n", (int)len, line);
            missing = true;
            break;
        }
        line = end + 1;
    }
    if (missing) {
        fprintf(stderr, "  = note: the game's C files and WebAssembly libraries (.a) define its C functions on the web; "
                        "for one that only exists natively, write a stand-in inside '#ifdef __wasm__'\n");
    }
}

// A link that failed, explained where tide knows more than the linker says.
static void explain_link(const build *b, const char *const *argv)
{
    char *text = said(argv);
    explain_cpp_link(text);
    if (b->opts->web) explain_web_externs(text, &b->externs);
    free(text);
}

// Links `objects`, the game's C (`c`, NULL for tide run's host) and the
// engine's objects into `output`: a program, with the platform layer, or with
// `shared`, a library of the game for a host to load (see tide/host.h), whose
// debug info on Windows goes in `pdb`.
static bool link_objects(const build *b, const file_list *objects, const c_side *c, const char *output,
                         const bool shared, const char *pdb)
{
    const build_options *opts = b->opts;
    args a = {0};
    arg_compiler(&a, b->compiler);
    if (build_target.flag) {
        arg(&a, build_target.flag);
        arg(&a, build_target.sysroot_flag);
    }
    if (shared) arg(&a, "-shared");
    for (int i = 0; i < objects->count; i++) arg(&a, objects->items[i]);
    for (int i = 0; c && i < c->objects.count; i++) arg(&a, c->objects.items[i]);
    for (int i = 0; c && i < c->link.count; i++) arg(&a, c->link.items[i]);
    for (int i = 0; i < b->engine.count; i++) arg(&a, b->engine.items[i]);
    // The prebuilt platform layer (see platform/CMakeLists.txt). A library of
    // the game has none of it, as its host does. But for the bold weight's
    // file, which is data: a game that says Font.Bold names it in its C, and
    // the linker takes that one object out of the platform layer's library,
    // as it does for a program. A game that doesn't is linked without the
    // library, so that nothing else can come from it.
    char *lib_dir = path_join(b->root, opts->web ? "lib/web" : "lib/native");
    char *platform_lib = format("%s/lib%s.a", lib_dir, "tide_platform");
    if (!opts->managed && (!shared || b->info.bold)) arg(&a, platform_lib);
    arg(&a, "-o");
    arg(&a, output);
    arg_list(&a, opts->release ? release_flags : debug_flags);
    // Hot-reload libraries keep their public symbols and their existing link
    // policy. Standalone releases only need code reachable from their entry.
    if ((opts->release || opts->server) && !opts->watch && !shared) {
        if (opts->web) arg(&a, "-Wl,-s"); // wasm-ld already collects unused sections.
#ifdef __APPLE__
        else arg(&a, opts->release ? "-Wl,-dead_strip,-S,-x" : "-Wl,-dead_strip");
#else
        else arg(&a, opts->release ? "-Wl,--gc-sections,-s" : "-Wl,--gc-sections"); // ELF and Windows' MinGW lld.
#endif
    }
    char *pdb_flag = NULL;
    char *ld_flag = NULL;
    char *imports_flag = NULL;
    if (opts->web) {
        if (opts->managed) {
            arg(&a, "-mexec-model=reactor");
            arg(&a, "-Wl,--import-memory,--export-memory,--max-memory=134217728,-z,stack-size=1048576");
        } else {
            arg_list(&a, web_link_flags);
            imports_flag = page_imports_flag(output, platform_lib);
            arg(&a, "-Xlinker"); // Not -Wl, which would split a path at its commas
            arg(&a, imports_flag);
        }
#ifndef TIDE_EMBEDDED_CLANG
        ld_flag = format("-fuse-ld=%s", b->wasm_ld, NULL); // It may be elsewhere than clang, as with Homebrew
        arg(&a, ld_flag);
#endif
    } else {
#if defined(TIDE_EMBEDDED_CLANG) && !defined(_WIN32)
        arg(&a, "-fuse-ld=lld"); // The built-in linker
#endif
#ifdef _WIN32
        arg(&a, "-fuse-ld=lld");
        if (shared) {
            if (pdb) {
                pdb_flag = format("-Wl,--pdb=%s", pdb, NULL);
                arg(&a, pdb_flag);
            }
        } else if (!opts->release) {
            arg(&a, "-Wl,--pdb="); // A debug game gets its debug info in a .pdb
        } else if (!b->library && !opts->server) {
            // A release game opens its window without a console next to it. tide
            // run's host keeps tide's, to say what each reload did.
            arg(&a, "-Wl,--subsystem,windows");
        }
#else
        (void)pdb;
#endif
        arg_list(&a, native_libs);
    }
    // A target's C library, when it comes with tide: its libraries by name
    // (on the web, just the C library) and the compiler runtime by path.
    if (build_target.flag) {
        arg(&a, "-nodefaultlibs");
        if (opts->web) arg(&a, "-lc");
        arg(&a, build_target.builtins);
    }
    const int code = sys_run(a.items, NULL, false);
    if (code > 0) explain_link(b, a.items);
    free(a.items);
    free(lib_dir);
    free(platform_lib);
    free(pdb_flag);
    free(ld_flag);
    free(imports_flag);
    if (code == -1) fprintf(stderr, "tide: couldn't start %s\n", b->compiler);
    return code == 0;
}

// ---------------------------------------------------------------------------
// The web page: the package's shell, with a <script> holding the program as
// base64 and tide.js, which runs it. The same as cmake/web_page.mjs.

static char *base64(const unsigned char *data, const size_t len)
{
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *out = malloc((len + 2) / 3 * 4 + 1);
    if (!out) abort();
    size_t n = 0;
    for (size_t i = 0; i < len; i += 3) {
        const unsigned v = (unsigned)data[i] << 16 | (i + 1 < len ? (unsigned)data[i + 1] << 8 : 0)
                         | (i + 2 < len ? data[i + 2] : 0);
        out[n++] = digits[v >> 18 & 63];
        out[n++] = digits[v >> 12 & 63];
        out[n++] = i + 1 < len ? digits[v >> 6 & 63] : '=';
        out[n++] = i + 2 < len ? digits[v & 63] : '=';
    }
    out[n] = '\0';
    return out;
}

// The installed compiler already links zlib for LLVM. Compress embedded
// release payloads before base64: the page's release runtime accepts gzip
// and raw bytes. Development builds need no additional dependency.
static char *page_payload(const char *data, const size_t len, const bool release)
{
#ifdef TIDE_EMBEDDED_CLANG
    if (release && len && len <= UINT_MAX) {
        z_stream stream = {0};
        if (deflateInit2(&stream, 9, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK) {
            const uLong bound = deflateBound(&stream, (uLong)len);
            if (bound <= UINT_MAX) {
                unsigned char *compressed = malloc((size_t)bound);
                if (!compressed) abort();
                // A stable header on every host: no timestamp or host OS.
                gz_header header = {0};
                header.os = 255;
                const int header_ok = deflateSetHeader(&stream, &header);
                stream.next_in = (Bytef *)(void *)data;
                stream.avail_in = (uInt)len;
                stream.next_out = compressed;
                stream.avail_out = (uInt)bound;
                if (header_ok == Z_OK && deflate(&stream, Z_FINISH) == Z_STREAM_END && stream.total_out < len) {
                    char *encoded = base64(compressed, (size_t)stream.total_out);
                    free(compressed);
                    deflateEnd(&stream);
                    return encoded;
                }
                free(compressed);
            }
            deflateEnd(&stream);
        }
    }
#else
    (void)release;
#endif
    return base64((const unsigned char *)data, len);
}

// The first line from `from` that's `marker` alone (indented or not), or NULL.
// `after` gets the start of the line after it.
static char *marker_line(char *from, const char *marker, char **after)
{
    const size_t n = strlen(marker);
    for (char *line = from; line && *line;) {
        char *text = line;
        while (*text == ' ' || *text == '\t') text++;
        char *end = text + n;
        if (strncmp(text, marker, n) == 0 && (*end == '\n' || (*end == '\r' && end[1] == '\n'))) {
            *after = end + (*end == '\r' ? 2 : 1);
            return line;
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    return NULL;
}

// Leaves out the lines tide.js has for tide run's hot reloading, from a
// `// <tide run>` line to a `// </tide run>` one: pages made to ship don't
// have them.
static void strip_reloading(char *script)
{
    char *after_start = NULL;
    char *after_end = NULL;
    for (char *start; (start = marker_line(script, "// <tide run>", &after_start));) {
        if (!marker_line(after_start, "// </tide run>", &after_end)) return;
        memmove(start, after_end, strlen(after_end) + 1);
    }
}

// The run's arguments as JavaScript strings. Quotes, backslashes and HTML
// delimiters aren't part of its session options and are left out.
static void page_arguments(char *text, const size_t size, const char *const *args)
{
    snprintf(text, size, "Tide.arguments = [");
    for (int i = 0; args && args[i]; i++) {
        size_t at = strlen(text);
        if (at + strlen(args[i]) + 8 >= size) break;
        text[at++] = i ? ',' : ' ';
        text[at++] = '"';
        for (const char *c = args[i]; *c; c++) {
            if (*c != '"' && *c != '\\' && *c != '<' && (unsigned char)*c >= ' ') text[at++] = *c;
        }
        text[at++] = '"';
        text[at] = '\0';
    }
    strcat(text, "];\n");
}

static bool make_page(const char *root, const char *program, const char *page, const char *pack, const char *const *args,
                      const bool release)
{
    char *shell_path = path_join(root, "web/shell.html");
    char *script_path = path_join(root, release ? "web/tide.release.js" : "web/tide.js");
    char *shell = sys_read_file(shell_path, NULL);
    char *script = sys_read_file(script_path, NULL);
    free(shell_path);
    free(script_path);
    size_t wasm_len = 0;
    char *wasm = sys_read_file(program, &wasm_len);
    size_t pack_len = 0;
    char *packed = pack ? sys_read_file(pack, &pack_len) : NULL; // The game's assets, inside the page too (TIDE_PACK)
    const char *slot = shell ? strstr(shell, "{{{ SCRIPT }}}") : NULL;
    bool ok = false;
    if (!script || !wasm || !slot) {
        fprintf(stderr, "tide: the web page's files are missing from %s/web; reinstall tide\n", root);
        goto done;
    }
    strip_reloading(script);
    FILE *f = fopen(page, "wb");
    if (!f) {
        fprintf(stderr, "tide: can't write %s\n", page);
        goto done;
    }
    char *encoded = page_payload(wasm, wasm_len, release);
    fwrite(shell, 1, (size_t)(slot - shell), f);
    fprintf(f, "<script>\nconst TIDE_PROGRAM = \"%s\";\n", encoded);
    if (args && args[0]) {
        char arguments[512];
        page_arguments(arguments, sizeof arguments, args);
        fputs(arguments, f);
    }
    if (packed) {
        char *pack_encoded = page_payload(packed, pack_len, release);
        fprintf(f, "const TIDE_PACK = \"%s\";\n", pack_encoded);
        free(pack_encoded);
    }
    fprintf(f, "%s</script>", script);
    fputs(slot + strlen("{{{ SCRIPT }}}"), f);
    ok = fclose(f) == 0;
    free(encoded);
done:
    free(shell);
    free(script);
    free(wasm);
    free(packed);
    return ok;
}

// ---------------------------------------------------------------------------

// Finds the game's packages and generates its C. With `layout`, the generated
// code describes the data layout too, for hot reloading.
// The files and folders the game's assets name, as the last generate left
// them in its info: freed before the next.
static void free_asset_paths(game_info *info)
{
    for (int i = 0; i < info->asset_count; i++) free(info->asset_paths[i]);
    free(info->asset_paths);
    info->asset_paths = NULL;
    info->asset_count = 0;
}

// Where the game's pack of assets lands with its generated C: game.pack in
// `gen`, or nothing for a game with no assets (tide/pack.h).
static char *pack_of(const char *gen)
{
    return path_join(gen, "game.pack");
}

static bool folder_has_c_code(const char *folder)
{
    file_list sources = own_files_of(folder, source_extensions);
    file_list libraries = own_files_of(folder, library_extensions);
    const bool found = sources.count || libraries.count;
    free_files(&sources);
    free_files(&libraries);
    return found;
}

// C and libraries can read pack entries by name without a Tide reference.
// Only a closed Tide game gives the compiler every root of its asset graph.
static bool strip_game_assets(const build *b, const bool layout)
{
    if (!b->opts->release || b->opts->watch || b->library || layout || folder_has_c_code(b->folder)) return false;
    for (int i = 0; i < b->packages.count; i++) {
        if (folder_has_c_code(b->packages.items[i].folder)) return false;
    }
    return true;
}

static bool strip_server_assets(const build *b)
{
    if (!b->opts->server) return false;
    if (b->opts->server_strip_assets) return true;
    if (folder_has_c_code(b->folder)) return false;
    for (int i = 0; i < b->packages.count; i++) if (folder_has_c_code(b->packages.items[i].folder)) return false;
    return true;
}

static bool generate(build *b, const char *gen, const bool layout)
{
    if (!find_packages(b->folder, &b->packages)) return false;
    file_list paths;
    int count = 0;
    compile_input *inputs = game_inputs(b->folder, &b->packages, &paths, &count);
    if (!inputs) return false;
    // A Unity project's prefabs and scenes, as the prefabs tide makes of them, after the game's own files
    unity_sources unity = {0};
    if (b->opts->unity_assets) {
        if (!unity_assets_read(b->opts->unity_assets, &unity)) {
            unity_sources_free(&unity);
            free(inputs);
            free_files(&paths);
            return false;
        }
        inputs = realloc(inputs, sizeof(compile_input) * (size_t)(count + unity.count + 1));
        if (!inputs) abort();
        for (int i = 0; i < unity.count; i++) inputs[count++] = (compile_input){unity.items[i].path, NULL, unity.items[i].text};
        // ...each as tide read it, beside the build, which its messages' lines are of
        char *prefabs = b->opts->unity && b->opts->output ? path_join(b->opts->output, "prefabs") : NULL;
        if (prefabs) sys_remove_tree(prefabs);
        for (int i = 0; prefabs && i < unity.count; i++) {
            if (strcmp(unity.items[i].path, UNITY_SOURCE_PATH) == 0) continue;
            char *name = format("%s.tprefab", unity.items[i].path, NULL);
            char *path = path_join(prefabs, name);
            char *dir = path_dir(path);
            sys_mkdirs(dir);
            sys_write_text(path, unity.items[i].text);
            free(dir);
            free(path);
            free(name);
        }
        free(prefabs);
    }
    tide_sha256 external;
    tide_sha256_init(&external);
    static const char *const identity_extensions[] = {".c", ".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".hxx", ".inl",
        ".a", ".lib", ".so", ".dll", ".dylib", NULL};
    bool has_external = false;
    for (int k = 0; k <= b->packages.count; k++) {
        const char *folder = k == b->packages.count ? b->folder : b->packages.items[k].folder;
        const char *name = k == b->packages.count ? "game" : b->packages.items[k].name;
        file_list files = own_files_of(folder, identity_extensions);
        if (files.count) qsort(files.items, (size_t)files.count, sizeof(char *), path_order);
        char *prefix = path_join(folder, "");
        for (int i = 0; i < files.count; i++) {
            size_t size;
            char *bytes = sys_read_file(files.items[i], &size);
            if (!bytes) { fprintf(stderr, "tide: cannot fingerprint %s\n", files.items[i]); free_files(&files); free(prefix); free(inputs); free_files(&paths); unity_sources_free(&unity); return false; }
            has_external = true;
            // Source checkout line endings do not change C semantics. Binary
            // libraries remain exact bytes, including every target variant.
            if (!has_extension(files.items[i], ".a") && !has_extension(files.items[i], ".lib")
                && !has_extension(files.items[i], ".so") && !has_extension(files.items[i], ".dll") && !has_extension(files.items[i], ".dylib")) {
                size_t written = 0;
                for (size_t at = 0; at < size; at++) {
                    if (bytes[at] == '\r' && at + 1u < size && bytes[at + 1u] == '\n') continue;
                    bytes[written++] = bytes[at];
                }
                size = written;
            }
            tide_sha256_add(&external, name, strlen(name) + 1u);
            const char *relative = files.items[i] + strlen(prefix);
            tide_sha256_add(&external, relative, strlen(relative) + 1u);
            uint8_t digest[32];
            tide_sha256_of(bytes, size, digest);
            tide_sha256_add(&external, digest, sizeof digest);
            free(bytes);
        }
        free(prefix);
        free_files(&files);
    }
    uint8_t external_identity[32];
    if (has_external) {
        const uint8_t release = b->opts->release ? 1u : 0u;
        tide_sha256_add(&external, &release, 1);
    }
    tide_sha256_end(&external, external_identity);
    arena_reset(); // What an earlier build compiled, when a run rebuilds the game
    b->externs = (sb){0};
    game_info next_info = {0};
    char *pack = pack_of(gen);
    // The game's folder is where its .tide/ is: the hashes of the files its assets name, and fetched URLs, are kept there
    char *editor_text = b->opts->editor_draft ? sys_read_file(b->opts->editor_draft, NULL) : NULL;
    if (b->opts->editor_draft && !editor_text) { fprintf(stderr, "tide: cannot read the editor draft\n"); free(pack); free_files(&paths); free(inputs); unity_sources_free(&unity); return false; }
    const codegen_options codegen = {.name = "game", .out_dir = gen, .line_directives = true, .layout = layout,
                                     .embed = b->opts->unity, .externs = &b->externs,
                                     .info = &next_info, .game_folder = b->folder, .pack = pack,
                                     .server = b->opts->server,
                                     .strip_server_assets = strip_server_assets(b),
                                     .strip_unused_assets = strip_game_assets(b, layout),
                                     .defines = b->opts->defines, .define_count = b->opts->define_count,
                                     .external_identity = has_external ? external_identity : NULL,
                                     .editor_prefab = b->opts->editor_prefab, .editor_local = b->opts->editor_local,
                                     .editor_source = b->opts->editor_source, .editor_text = editor_text};
    const bool ok = compile_inputs(inputs, count, &codegen, NULL);
    free(editor_text);
    if (ok) {
        free_asset_paths(&b->info);
        b->info = next_info;
        free(b->unity_map);
        b->unity_map = unity.map;
        unity.map = NULL;
    } else {
        // A bad asset can be fixed without editing code: keep watching the
        // paths and rebuild policy of the last successful generation.
        free_asset_paths(&next_info);
    }
    free(pack);
    free(inputs);
    free_files(&paths);
    unity_sources_free(&unity);
    return ok;
}

// Puts the pack `from` at `to` when there is one, and takes away what was at
// `to` when there isn't: a game whose assets went has none left beside it.
static bool place_pack(const char *from, const char *to)
{
    size_t size = 0;
    char *bytes = sys_read_file(from, &size);
    if (!bytes) {
        sys_remove(to);
        return true;
    }
    const bool ok = sys_write_file(to, bytes, size);
    if (!ok) fprintf(stderr, "tide: can't write %s\n", to);
    free(bytes);
    return ok;
}

static char *build_android(const char *root, const build_options *opts, char *package, size_t package_size);

static char *build_game(const char *root, const build_options *opts, const char *const *args)
{
    if (opts->android) return build_android(root, opts, NULL, 0);
    build b = {0};
    if (!build_setup(&b, root, opts, false)) return NULL;
    char *gen = path_join(b.cache, "gen");
    sys_mkdirs(gen);

    if (!generate(&b, gen, false)) return NULL;
    char *main_c = path_join(gen, "main.c");
    write_main(main_c, opts, b.name);

    char *game_c = path_join(gen, "game.c");
    file_list objects = {0};
    add_file(&objects, path_join(b.cache, "game.o"));
    add_file(&objects, path_join(b.cache, "main.o"));
    c_side c = {0};
    if (!compile_game(&b, gen, game_c, main_c, &objects, &c) || !find_libraries(&b, &c)) return NULL;

    char *output;
    const char *suffix = opts->managed ? ".wasm" : opts->web ? ".html" : EXE_SUFFIX;
    if (opts->output) {
        output = path_absolute(opts->output);
    } else {
        char file[512];
        snprintf(file, sizeof file, "%s%s%s", b.name, opts->server ? "-server" : "", suffix);
        output = default_output(b.folder, file);
    }
    char *output_dir = path_dir(output);
    sys_mkdirs(output_dir);
    // Web builds link a .wasm, which then goes inside the page.
    char *program = output;
    if (opts->web && !opts->managed) {
        char file[512];
        snprintf(file, sizeof file, "%s.wasm", b.name);
        program = path_join(b.cache, file);
    }
    if (!link_objects(&b, &objects, &c, program, false, NULL)) return NULL;
    char *pack = pack_of(gen);
    if (opts->web && !opts->managed && !make_page(root, program, output, pack, args, opts->release && !opts->watch)) return NULL;
    if (!opts->web || opts->managed) { // Beside the program, as <name>.pack, which tide/run.h loads
        char file[512];
        snprintf(file, sizeof file, "%s.pack", opts->managed ? "server" : b.name);
        if (opts->server && !opts->managed) snprintf(file, sizeof file, "%s-server.pack", b.name);
        char *beside = path_join(output_dir, file);
        const bool placed = place_pack(pack, beside);
        free(beside);
        if (!placed) return NULL;
    }
    free(pack);
    char *identity = format("%s.simulation", output, NULL);
    const bool wrote_identity = sys_write_text(identity, b.info.simulation);
    free(identity);
    if (!wrote_identity) return NULL;
    copy_dynamic(&c, output_dir);
    if (!opts->server) place_moltenvk(&b, output_dir);
    free_game_c(&c);
    return output;
}

char *tide_build(const char *root, const build_options *opts)
{
    return build_game(root, opts, NULL);
}

// ---------------------------------------------------------------------------
// Interactive `tide run`: the game as a library in a small host program, which swaps in
// each new build of it (see tide/host.h), and a new build whenever one of the
// game's files changes. Each run has a folder of its own, .tide/<configuration>/run/
// <tide's process ID>, so two runs of one game (a server and a client) never
// build over each other.

typedef struct run {
    build b;
    bool web;        // Each build is a web program the page starts, not a library
    char *dir;       // This run's folder
    char *gen;       // ...and its generated files
    uint32_t builds; // Made so far, game-1 to game-<builds>
    uint32_t packs;  // Packs of the game's assets written so far, pack-1 to pack-<packs>: with each build, and as a named file changes
} run;

// The game's pack as the last generate left it, into the run's folder as the
// next pack-<n>, which the host loads (platform/src/reload.c). Nothing for a
// game with no assets.
static bool write_pack(run *r)
{
    char *from = pack_of(r->gen);
    size_t size = 0;
    char *bytes = sys_read_file(from, &size);
    free(from);
    if (!bytes) return true;
    char name[32];
    snprintf(name, sizeof name, "pack-%u", (unsigned)(r->packs + 1u));
    char *to = path_join(r->dir, name);
    const bool ok = sys_write_file(to, bytes, size);
    if (ok) r->packs++;
    else fprintf(stderr, "tide: can't write %s\n", to);
    free(to);
    free(bytes);
    return ok;
}

static void stamp_file(void *user, const char *path);

static void stamp_asset_file(void *user, const char *path, const unsigned long long size)
{
    (void)size;
    stamp_file(user, path);
}

// Changes whenever a file the game's assets name does, or one comes or goes
// in a folder a glob names: the paths the last generate listed.
static uint64_t asset_stamp(const build *b)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (int i = 0; i < b->info.asset_count; i++) {
        const char *path = b->info.asset_paths[i];
        if (path[0] && path[strlen(path) - 1] == '/') folder_find_files(path, stamp_asset_file, &h);
        else stamp_file(&h, path);
    }
    return h;
}

// A file an asset names changed: the game's C stands, and its pack is made
// again, which the running game loads: a saved image is in it at once.
static void repack(run *r)
{
    if (generate(&r->b, r->gen, true)) write_pack(r);
    else fprintf(stderr, "tide: the game keeps its assets as they were\n");
}

static char *build_path(const run *r, const uint32_t n, const char *suffix)
{
    char name[64];
    snprintf(name, sizeof name, "game-%u%s", (unsigned)n, suffix);
    return path_join(r->dir, name);
}

// Builds the game's next library (or web program), game-<builds + 1>, linked
// to another name and renamed once it's whole: the host (or the page) takes
// it as soon as it's there, and says what it did with it. False after saying
// what's wrong.
static bool build_library(run *r)
{
    if (!generate(&r->b, r->gen, true)) return false;
    const char *suffix = r->web ? ".wasm" : LIBRARY_SUFFIX;
    char *game_c = path_join(r->gen, "game.c");
    char *library_c = path_join(r->gen, r->web ? "main.c" : "library.c");
    file_list objects = {0};
    add_file(&objects, path_join(r->dir, "game.o"));
    add_file(&objects, path_join(r->dir, r->web ? "main.o" : "library.o"));
    c_side c = {0};
    bool ok = compile_game(&r->b, r->gen, game_c, library_c, &objects, &c) && find_libraries(&r->b, &c);
    if (ok) copy_dynamic(&c, r->dir); // Next to the host, which loads the game's library
    if (ok) place_moltenvk(&r->b, r->dir);
    const uint32_t n = r->builds + 1u;
    char *linked = build_path(r, n, ".tmp");
    char *pdb = build_path(r, n, ".pdb");
    char *library = build_path(r, n, suffix);
    ok = ok && link_objects(&r->b, &objects, &c, linked, !r->web, r->b.opts->release || r->web ? NULL : pdb);
    free_game_c(&c);
    if (ok && !sys_rename(linked, library)) {
        fprintf(stderr, "tide: can't write %s\n", library);
        ok = false;
    }
    if (ok) ok = write_pack(r);
    if (ok) {
        r->builds = n;
        // The one before the last is done with; if the host still has it, it
        // stays until the run ends.
        if (n > 2) {
            char *old = build_path(r, n - 2u, suffix);
            char *old_pdb = build_path(r, n - 2u, ".pdb");
            sys_remove(old);
            sys_remove(old_pdb);
            free(old);
            free(old_pdb);
        }
    }
    free(game_c);
    free(library_c);
    free_files(&objects);
    free(linked);
    free(pdb);
    free(library);
    return ok;
}

char *tide_build_editor(const char *root, const build_options *opts)
{
    if (!opts->editor_prefab || !opts->output || opts->web || opts->android) return NULL;
    run r = {0};
    if (!build_setup(&r.b, root, opts, true)) return NULL;
    r.dir = path_absolute(opts->output);
    r.gen = path_join(r.dir, "gen");
    if (!sys_mkdirs(r.gen)) return NULL;
    char *stub = path_join(r.gen, "library.c");
    const bool wrote = sys_write_text(stub, "#include \"game.h\"\n");
    free(stub);
    if (!wrote || !build_library(&r)) return NULL;
    return build_path(&r, 1, LIBRARY_SUFFIX);
}

// The host: tide/host.h's loop, reloading the libraries in the run's folder.
static bool build_host(run *r, const char *program)
{
    char title[600];
    char dir[2048];
    title_field(title, sizeof title, r->b.opts, r->b.name);
    c_string(dir, sizeof dir, r->dir);
    char text[4096];
    snprintf(text, sizeof text,
             "// Generated by tide. Do not edit.\n"
             "#include \"tide/host.h\"\n"
             "\n"
             "int main(int argc, char **argv)\n"
             "{\n"
             "    tide_host_run_library(&(tide_run_desc){%s, .stats = %s, .stats_open = %s, .debug_views = %s,\n"
             "                                           .argc = argc, .argv = argv},\n"
             "                          \"%s\");\n"
             "}\n",
             title, r->b.opts->stats ? "true" : "false", r->b.opts->stats_open ? "true" : "false",
             r->b.opts->debug_views ? "true" : "false", dir);
    char *host_c = path_join(r->gen, "host.c");
    file_list objects = {0};
    add_file(&objects, path_join(r->dir, "host.o"));
    const bool ok = sys_write_text(host_c, text) && compile_c(&r->b, host_c, objects.items[0], NULL)
                 && link_objects(&r->b, &objects, NULL, program, false, NULL);
    free(host_c);
    free_files(&objects);
    return ok;
}

// What the host loads from each library (see tide/host.h): the game, with its
// data layout.
static bool write_library(const run *r)
{
    char *path = path_join(r->gen, "library.c");
    const bool ok = sys_write_text(path, "// Generated by tide. Do not edit.\n"
                                         "#include \"game.h\"\n"
                                         "#include \"tide/run.h\"\n"
                                         "\n"
                                         "extern const tide_layout tide_game_layout;\n"
                                         "\n"
                                         "TIDE_HOST_EXPORT const tide_host_game *tide_host_library(void)\n"
                                         "{\n"
                                         "    static tide_host_game game;\n"
                                         "    game = tide_host_game_api;\n"
                                         "    game.layout = &tide_game_layout;\n"
                                         "    return &game;\n"
                                         "}\n");
    free(path);
    return ok;
}

// Removes the folders of runs whose tide has ended without cleaning up.
static void remove_ended_run(void *user, const char *name, const bool is_dir)
{
    const char *runs = user;
    char *end = NULL;
    const unsigned long pid = strtoul(name, &end, 10);
    if (!is_dir || end == name || *end || sys_process_alive((uint32_t)pid)) return;
    char *path = path_join(runs, name);
    sys_remove_tree(path);
    free(path);
}

static void stamp_file(void *user, const char *path)
{
    uint64_t *h = user;
    for (const char *p = path; *p; p++) *h = (*h ^ (uint8_t)*p) * 0x100000001B3ull;
    *h = (*h ^ sys_file_stamp(path)) * 0x100000001B3ull;
}

// Stamps the .tide files in `folder`, its C, headers and libraries, and its
// tide.packages.
static void stamp_folder(uint64_t *h, const char *folder)
{
    char *dir = path_join(folder, "");
    folder_find_own(dir, ".tide", stamp_file, h);
    folder_find_own(dir, ".tprefab", stamp_file, h);
    char *packages = path_join(dir, PACKAGES_FILE);
    stamp_file(h, packages);
    free(packages);
    free(dir);
    file_list others = own_files_of(folder, c_side_extensions);
    for (int i = 0; i < others.count; i++) stamp_file(h, others.items[i]);
    free_files(&others);
}

// Changes whenever one of the game's .tide files does, or its C, headers and
// libraries, or one comes or goes; or its tide.packages, or the files of a
// package in a folder on this machine, or of one that comes with tide. Those
// from git, at their commits, never change.
static uint64_t game_stamp(const build *b)
{
    uint64_t h = 0xCBF29CE484222325ull;
    stamp_folder(&h, b->folder);
    for (int p = 0; p < b->packages.count; p++) {
        const package_line *line = b->packages.items[p].line;
        if (line->local || line->builtin) stamp_folder(&h, b->packages.items[p].folder);
    }
    if (b->opts->unity_assets) h = (h ^ unity_assets_stamp(b->opts->unity_assets)) * 0x100000001B3ull;
    return h;
}

// A change is built once the files have stayed the same this long, as
// editors can save in steps.
#define SETTLE_MS 200

// This run's folder, made anew, after removing those of runs that ended.
static void open_run(run *r)
{
    char *runs = path_join(r->b.cache, "run");
    sys_mkdirs(runs);
    sys_list(runs, remove_ended_run, (void *)runs);
    char pid[32];
    snprintf(pid, sizeof pid, "%u", (unsigned)sys_pid());
    r->dir = path_join(runs, pid);
    r->gen = path_join(r->dir, "gen");
    sys_remove_tree(r->dir); // A run that ended with the same process ID
    sys_mkdirs(r->gen);
    free(runs);
}

int tide_run_native(const char *root, const build_options *opts, const char *const *game_args)
{
    run r = {0};
    if (!build_setup(&r.b, root, opts, false)) return 1;
    open_run(&r);
    char file[512];
    snprintf(file, sizeof file, "%s%s", r.b.name, EXE_SUFFIX);
    char *output = path_join(r.dir, file);
    build_options standalone = *opts;
    standalone.output = output; // Another run never links over this one's executable
    char *program = tide_build(root, &standalone);
    int code = 1;
    if (program) {
        args a = {0};
        arg(&a, program);
        for (int i = 0; game_args[i]; i++) arg(&a, game_args[i]);
        code = sys_run(a.items, r.b.folder, false);
        if (code < 0) fprintf(stderr, "tide: couldn't start %s\n", program);
        free(a.items);
    }
    sys_remove_tree(r.dir);
    free(output);
    free(program);
    return code < 0 ? 1 : code;
}

// Whether a line typed into tide's terminal asks to start the game over,
// saying so either way.
static bool typed_restart(void)
{
    char line[64];
    if (!sys_typed_line(line, sizeof line)) return false;
    const bool restart = strcmp(line, "r") == 0;
    if (restart) printf("tide: started over\n");
    else if (line[0]) printf("tide: type r and press Enter to start the game over\n");
    fflush(stdout);
    return restart;
}

int tide_run_reloading(const char *root, const build_options *opts, const char *const *game_args)
{
    run r = {0};
    if (!build_setup(&r.b, root, opts, true)) return 1;
    open_run(&r);

    char file[512];
    snprintf(file, sizeof file, "%s%s", r.b.name, EXE_SUFFIX);
    char *program = path_join(r.dir, file);
    if (!write_library(&r) || !build_library(&r) || !build_host(&r, program)) return 1;

    args a = {0};
    arg(&a, program);
    for (int i = 0; game_args[i]; i++) arg(&a, game_args[i]);
    sys_process *game = sys_start(a.items, r.b.folder);
    free(a.items);
    if (!game) {
        fprintf(stderr, "tide: couldn't start %s\n", program);
        return 1;
    }
    int code = 0;
    printf("tide: saving a .tide or C file reloads the game; Ctrl+R or r and Enter starts it over\n");
    fflush(stdout);
    sys_read_lines();

    uint64_t stamp = game_stamp(&r.b);
    uint64_t assets = asset_stamp(&r.b);
    int64_t changed_at = -1, assets_changed_at = -1;
    while (!sys_wait(game, 250, &code)) {
        if (typed_restart()) {
            char *restart = path_join(r.dir, "restart");
            sys_write_text(restart, "");
            free(restart);
        }
        const uint64_t now = game_stamp(&r.b);
        if (now != stamp) {
            stamp = now;
            changed_at = sys_now_ms();
            continue;
        }
        const uint64_t assets_now = asset_stamp(&r.b);
        if (assets_now != assets) {
            assets = assets_now;
            assets_changed_at = sys_now_ms();
            continue;
        }
        if (changed_at >= 0 && sys_now_ms() - changed_at >= SETTLE_MS) {
            changed_at = -1;
            assets_changed_at = -1; // A build brings the pack as the files are now
            if (!build_library(&r)) fprintf(stderr, "tide: the game keeps running its last build\n");
        } else if (assets_changed_at >= 0 && sys_now_ms() - assets_changed_at >= SETTLE_MS) {
            assets_changed_at = -1;
            if (r.b.info.assets_affect_match) {
                if (!build_library(&r)) fprintf(stderr, "tide: the game keeps running its last build\n");
            } else {
                repack(&r);
            }
        }
    }
    sys_remove_tree(r.dir);
    return code;
}

// ---------------------------------------------------------------------------
// `tide unity`: the game for Unity's Tide package (unity/com.tide.unity), which
// runs it inside the editor through tide/embed.h. Into the folder it's given
// (the project's Library/Tide) go the host Unity loads, tide_embed, once, then
// the game's libraries and packs, as tide run writes them for its host.

#ifdef _WIN32
#define EMBED_LIBRARY "tide_embed.dll"
#elif defined(__APPLE__)
#define EMBED_LIBRARY "libtide_embed.dylib"
#else
#define EMBED_LIBRARY "libtide_embed.so"
#endif

// What tide/embed.h's host gives the program that loads it
static const char *const embed_exports[] = {"tide_embed_abi", "tide_embed_create", "tide_embed_destroy", "tide_embed_frame",
                                            "tide_embed_view", "tide_embed_status_of", "tide_embed_request", "tide_embed_command",
                                            "tide_embed_background", "tide_embed_schema", "tide_embed_draw", NULL};

typedef struct numbered {
    const char *prefix, *suffix;
    uint32_t most;
} numbered;

static void count_numbered(void *user, const char *name, const bool is_dir)
{
    numbered *n = user;
    const size_t size = strlen(name), prefix = strlen(n->prefix), suffix = strlen(n->suffix);
    if (is_dir || size <= prefix + suffix || strncmp(name, n->prefix, prefix) != 0 || strcmp(name + size - suffix, n->suffix) != 0) return;
    uint64_t value = 0;
    size_t i = prefix;
    for (; i < size - suffix && name[i] >= '0' && name[i] <= '9' && value <= UINT32_MAX; i++) value = value * 10u + (uint64_t)(name[i] - '0');
    if (i == size - suffix && value > n->most && value <= UINT32_MAX) n->most = (uint32_t)value;
}

// The highest n of a `<prefix><n><suffix>` in `dir`, or 0: where its numbers go on from.
static uint32_t highest_numbered(const char *dir, const char *prefix, const char *suffix)
{
    numbered n = {prefix, suffix, 0};
    sys_list(dir, count_numbered, &n);
    return n.most;
}

// What each library loads the game through: tide/run.h's table, with what --embed generated and the layout.
static bool write_unity_library(const run *r)
{
    char *path = path_join(r->gen, "library.c");
    const bool ok = sys_write_text(path, "// Generated by tide. Do not edit.\n"
                                         "#define TIDE_HAS_EMBED 1 // tide/run.h's table has tide_game_embed and the layout\n"
                                         "#include \"game.h\"\n"
                                         "#include \"tide/run.h\"\n"
                                         "\n"
                                         "TIDE_HOST_EXPORT const tide_host_game *tide_host_library(void)\n"
                                         "{\n"
                                         "    return &tide_host_game_api;\n"
                                         "}\n"
                                         "\n"
                                         "// The schema, for tide to write beside the build: what Unity's package binds the game by\n"
                                         "TIDE_HOST_EXPORT uint32_t tide_unity_schema(char *out, uint32_t capacity)\n"
                                         "{\n"
                                         "    return tide_game_embed.schema(out, capacity);\n"
                                         "}\n");
    free(path);
    return ok;
}

// The host Unity loads: the platform layer and the engine, with tide/embed.h's functions for it to call, linked
// under another name and renamed into place once it's whole. False after saying what's wrong.
static bool build_embed_host(run *r)
{
    char *source = path_join(r->gen, "embed_host.c");
    char *object = path_join(r->dir, "embed_host.o");
    bool ok = sys_write_text(source, "// Generated by tide. Do not edit. The host is the platform layer's tide/embed.h\n"
                                     "#include \"tide/embed.h\"\n");
    compiles list = {0};
    if (ok) {
        queue_compile(&list, &r->b, source, object, NULL, false);
        queue_engine(&r->b, &list);
        ok = run_compiles(&r->b, &list);
    }
    char *linked = path_join(r->dir, EMBED_LIBRARY ".tmp");
    char *library = path_join(r->dir, EMBED_LIBRARY);
    char *exports = path_join(r->gen, "tide_embed.def");
    char *lib_dir = path_join(r->b.root, "lib/native");
    char *platform_lib = format("%s/lib%s.a", lib_dir, "tide_platform");
    args a = {0};
    file_list keep = {0};
    if (ok) {
        arg_compiler(&a, r->b.compiler);
        if (build_target.flag) {
            arg(&a, build_target.flag);
            arg(&a, build_target.sysroot_flag);
        }
        arg(&a, "-shared");
        arg(&a, object);
        for (int i = 0; i < r->b.engine.count; i++) arg(&a, r->b.engine.items[i]);
        arg(&a, platform_lib);
#ifdef _WIN32
        // A MinGW DLL exports what a module definition file lists
        char def[1024] = "EXPORTS\n";
        for (int i = 0; embed_exports[i]; i++) {
            const size_t at = strlen(def);
            snprintf(def + at, sizeof def - at, "    %s\n", embed_exports[i]);
        }
        ok = sys_write_text(exports, def);
        arg(&a, exports);
#endif
        // Each from the platform layer's library, which links only what's asked for
        for (int i = 0; embed_exports[i]; i++) {
#ifdef __APPLE__
            char *flag = format("-Wl,-u,_%s", embed_exports[i], NULL);
#else
            char *flag = format("-Wl,-u,%s", embed_exports[i], NULL);
#endif
            add_file(&keep, flag);
            free(flag);
        }
        for (int i = 0; i < keep.count; i++) arg(&a, keep.items[i]);
        arg(&a, "-o");
        arg(&a, linked);
        arg_list(&a, r->b.opts->release ? release_flags : debug_flags);
#if defined(_WIN32) || defined(TIDE_EMBEDDED_CLANG)
        arg(&a, "-fuse-ld=lld");
#endif
        arg_list(&a, native_libs);
        if (build_target.flag) {
            arg(&a, "-nodefaultlibs");
            arg(&a, build_target.builtins);
        }
        const int code = sys_run(a.items, NULL, false);
        if (code > 0) explain_link(&r->b, a.items);
        if (code == -1) fprintf(stderr, "tide: couldn't start %s\n", r->b.compiler);
        ok = ok && code == 0;
    }
    if (ok && !sys_rename(linked, library)) {
        fprintf(stderr, "tide: can't write %s\n", library);
        ok = false;
    }
    free(a.items);
    free_files(&keep);
    free(source);
    free(object);
    free(linked);
    free(library);
    free(exports);
    free(lib_dir);
    free(platform_lib);
    return ok;
}

// Writes `text` to `path` unless it holds it already: Unity compiles again for what changes.
static bool write_if_changed(const char *path, const char *text, const size_t size)
{
    size_t had = 0;
    char *old = sys_read_file(path, &had);
    const bool same = old && had == size && memcmp(old, text, size) == 0;
    free(old);
    return same || sys_write_file(path, text, size);
}

// The project's Packages/com.tide.game, an embedded package of the assembly Tide.Game: the schema, which the
// package's source generator makes the game's C# types of, and a stub naming the schema's hash. Unity's build
// cache knows an assembly by its sources, so the stub is what makes a new schema compile again. Made from the
// project of opts->unity_assets, as each build's schema changes; meant for source control, so a clone compiles
// before tide has built anything. False after saying what's wrong.
static bool write_unity_package(const run *r, const char *schema, const uint32_t size)
{
    if (!r->b.opts->unity_assets) return true;
    char *assets = path_absolute(r->b.opts->unity_assets);
    char *project = path_dir(assets);
    char *dir = path_join(project, "Packages/com.tide.game");
    bool ok = sys_mkdirs(dir);
    // The hash, from the schema's "hash":"..."
    char hash[17] = "";
    const char *at = strstr(schema, "\"hash\":\"");
    for (int i = 0; at && i < 16 && at[8 + i] && at[8 + i] != '"'; i++) hash[i] = at[8 + i];
    static const char package[] = "{\n  \"name\": \"com.tide.game\",\n  \"displayName\": \"Tide game types\",\n  \"version\": \"0.0.0\",\n"
                                  "  \"description\": \"Written by tide unity: the game's schema, which Tide's package makes "
                                  "the game's C# types of. Keep it in source control.\"\n}\n";
    static const char asmdef[] = "{\n    \"name\": \"Tide.Game\",\n    \"rootNamespace\": \"Tide.Game\",\n"
                                 "    \"references\": [\n        \"Tide.Runtime\"\n    ],\n    \"allowUnsafeCode\": true,\n"
                                 "    \"autoReferenced\": true\n}\n";
    char stub[512];
    snprintf(stub, sizeof stub, "// Written by tide unity. The game's C# types are generated from schema.json beside this,\n"
                                "// by Tide's package (Tide.Generator), whenever this hash changes: %s\n", hash);
    const char *const names[] = {"package.json", "Tide.Game.asmdef", "TideGameSchema.cs", "schema.json"};
    const char *const texts[] = {package, asmdef, stub, schema};
    const size_t sizes[] = {sizeof package - 1, sizeof asmdef - 1, strlen(stub), size};
    for (int i = 0; ok && i < 4; i++) {
        char *path = path_join(dir, names[i]);
        ok = write_if_changed(path, texts[i], sizes[i]);
        if (!ok) fprintf(stderr, "tide: can't write %s\n", path);
        free(path);
    }
    free(dir);
    free(project);
    free(assets);
    return ok;
}

// The newest build's schema, from the build itself, as schema.json beside it: what the package's C# bindings are
// generated from before anything runs. False after saying what's wrong.
static bool write_unity_schema(const run *r)
{
    char *library_path = build_path(r, r->builds, LIBRARY_SUFFIX);
    void *library = sys_library_open(library_path);
    typedef uint32_t (*schema_fn)(char *, uint32_t);
    schema_fn schema = NULL;
    void (*found)(void) = sys_library_find(library, "tide_unity_schema");
    if (found) memcpy(&schema, &found, sizeof schema);
    bool ok = schema != NULL;
    if (ok) {
        const uint32_t size = schema(NULL, 0);
        char *text = malloc((size_t)size + 1u);
        if (!text) abort();
        schema(text, size + 1u);
        char *written = path_join(r->dir, "schema.json.tmp");
        char *path = path_join(r->dir, "schema.json");
        ok = sys_write_file(written, text, size) && sys_rename(written, path);
        if (!ok) fprintf(stderr, "tide: can't write the schema of %s\n", library_path);
        ok = ok && write_unity_package(r, text, size);
        free(written);
        free(path);
        free(text);
    } else {
        fprintf(stderr, "tide: can't read the schema of %s\n", library_path);
    }
    sys_library_close(library);
    free(library_path);
    // ...and which GameObject shows each entity of the Unity project's prefabs and scenes
    if (ok) {
        char *written = path_join(r->dir, "unity.json.tmp");
        char *path = path_join(r->dir, "unity.json");
        const char *map = r->b.unity_map ? r->b.unity_map : "{\"assets\": []}\n";
        ok = sys_write_file(written, map, strlen(map)) && sys_rename(written, path);
        if (!ok) fprintf(stderr, "tide: can't write %s\n", path);
        free(written);
        free(path);
    }
    return ok;
}

// Tide's program in a Unity WebGL player's page: tide/embed.h's host with the game linked in, its frames run by
// Unity's through tide.js's `pageFrames`, and what Unity's Tide.jslib calls (com.tide.unity).
static const char unity_web_main[] =
    "// Generated by tide. Do not edit. Tide's program beside Unity's on the web (tide unity --web): tide/embed.h's\n"
    "// host with the game linked in, its frames run by Unity's (Tide.jslib, through tide.js's pageFrames) while the\n"
    "// page is shown, and by tide.js's worker while it's hidden. The jslib copies Unity's input in and the view\n"
    "// packet, schema and status out through these exports.\n"
    "#define TIDE_HAS_EMBED 1\n"
    "#include \"game.h\"\n"
    "#include \"tide/run.h\"\n"
    "#include \"tide/embed.h\"\n"
    "#include \"tide/platform.h\"\n"
    "\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "\n"
    "#define TIDE_UNITY_EXPORT(name) __attribute__((export_name(#name)))\n"
    "\n"
    "static tide_embed *unity_embed;\n"
    "static tide_embed_input unity_input;\n"
    "static bool unity_fresh; // Unity gave this frame's input: a frame without it (the page hidden) has none\n"
    "static float unity_width = 960.0f, unity_height = 540.0f, unity_scale = 1.0f;\n"
    "static tide_embed_status unity_status;\n"
    "static uint32_t unity_view_size, unity_schema_size, unity_draw_size;\n"
    "static uint8_t *unity_scratch;\n"
    "static uint32_t unity_scratch_size;\n"
    "\n"
    "static int unity_frame(void *user, const float seconds)\n"
    "{\n"
    "    (void)user;\n"
    "    if (!unity_fresh) memset(&unity_input, 0, sizeof unity_input);\n"
    "    unity_fresh = false;\n"
    "    tide_embed_frame(unity_embed, seconds, &unity_input, unity_width, unity_height, unity_scale);\n"
    "    return TIDE_KEEP_RUNNING;\n"
    "}\n"
    "\n"
    "int main(int argc, char **argv)\n"
    "{\n"
    "    size_t pack_size = 0;\n"
    "    uint8_t *pack = tide_platform_pack(&pack_size); // The page's (TIDE_PACK)\n"
    "    unity_embed = tide_embed_create(&(tide_embed_desc){.game = &tide_host_game_api, .pack = pack, .pack_size = pack_size,\n"
    "                                                       .argc = argc, .argv = argv, .game_name = \"%s\"});\n"
    "    free(pack);\n"
    "    if (!unity_embed) return 1;\n"
    "    tide_platform_run(unity_frame, NULL);\n"
    "}\n";

// ...and what Tide.jslib calls
static const char unity_web_exports[] =
    "\n"
    "TIDE_UNITY_EXPORT(tide_unity_ready) int tide_unity_ready(void) { return unity_embed != NULL; }\n"
    "TIDE_UNITY_EXPORT(tide_unity_input) tide_embed_input *tide_unity_input(void) { return &unity_input; }\n"
    "\n"
    "// The page's clock times frames, the worker's too: Unity's seconds aren't needed\n"
    "TIDE_UNITY_EXPORT(tide_unity_frame_info) void tide_unity_frame_info(float seconds, float width, float height, float scale)\n"
    "{\n"
    "    (void)seconds;\n"
    "    unity_fresh = true;\n"
    "    unity_width = width;\n"
    "    unity_height = height;\n"
    "    unity_scale = scale;\n"
    "}\n"
    "\n"
    "TIDE_UNITY_EXPORT(tide_unity_status) const tide_embed_status *tide_unity_status(void)\n"
    "{\n"
    "    tide_embed_status_of(unity_embed, &unity_status);\n"
    "    return &unity_status;\n"
    "}\n"
    "\n"
    "TIDE_UNITY_EXPORT(tide_unity_view) const uint8_t *tide_unity_view(void) { return tide_embed_view(unity_embed, &unity_view_size); }\n"
    "TIDE_UNITY_EXPORT(tide_unity_view_size) uint32_t tide_unity_view_size(void) { return unity_view_size; }\n"
    "TIDE_UNITY_EXPORT(tide_unity_schema) const char *tide_unity_schema(void) { return tide_embed_schema(unity_embed, &unity_schema_size); }\n"
    "TIDE_UNITY_EXPORT(tide_unity_schema_size) uint32_t tide_unity_schema_size(void) { return unity_schema_size; }\n"
    "TIDE_UNITY_EXPORT(tide_unity_draw) const uint8_t *tide_unity_draw(void) { return tide_embed_draw(unity_embed, &unity_draw_size); }\n"
    "TIDE_UNITY_EXPORT(tide_unity_draw_size) uint32_t tide_unity_draw_size(void) { return unity_draw_size; }\n"
    "\n"
    "// Where the page puts what it passes in (a request's text, a command's value): `size` bytes, till the next call\n"
    "TIDE_UNITY_EXPORT(tide_unity_buffer) uint8_t *tide_unity_buffer(const uint32_t size)\n"
    "{\n"
    "    if (size > (64u << 20)) return NULL;\n"
    "    if (size > unity_scratch_size) {\n"
    "        uint8_t *grown = realloc(unity_scratch, size);\n"
    "        if (!grown) return NULL;\n"
    "        unity_scratch = grown;\n"
    "        unity_scratch_size = size;\n"
    "    }\n"
    "    return unity_scratch;\n"
    "}\n"
    "\n"
    "TIDE_UNITY_EXPORT(tide_unity_request) int tide_unity_request(uint32_t kind, const char *text, uint32_t port)\n"
    "{\n"
    "    return tide_embed_request(unity_embed, (tide_session_request_kind)kind, text, port);\n"
    "}\n"
    "\n"
    "TIDE_UNITY_EXPORT(tide_unity_command) int tide_unity_command(uint32_t type, uint32_t index, uint32_t generation,\n"
    "                                                             const void *value, uint32_t size)\n"
    "{\n"
    "    return tide_embed_command(unity_embed, type, index, generation, value, size);\n"
    "}\n"
    "\n"
    "TIDE_UNITY_EXPORT(tide_unity_background) void tide_unity_background(int hidden) { tide_embed_background(unity_embed, hidden != 0); }\n";

// tide-game.js, for the page: the Tide config tide.js reads (the program waits for Unity's TideRunner to start
// it, its frames are Unity's, and it draws in a canvas of its own that no one sees), the program and its pack as
// base64, and tide.js. False after saying what's wrong.
static bool write_unity_script(const char *root, const char *program, const char *script_path, const char *pack, const bool release)
{
    char *runtime_path = path_join(root, release ? "web/tide.release.js" : "web/tide.js");
    char *runtime = sys_read_file(runtime_path, NULL);
    free(runtime_path);
    size_t wasm_len = 0, pack_len = 0;
    char *wasm = sys_read_file(program, &wasm_len);
    char *packed = pack ? sys_read_file(pack, &pack_len) : NULL;
    bool ok = false;
    FILE *f = NULL;
    if (!runtime || !wasm) {
        fprintf(stderr, "tide: the web page's files are missing from %s/web; reinstall tide\n", root);
        goto done;
    }
    strip_reloading(runtime);
    f = fopen(script_path, "wb");
    if (!f) {
        fprintf(stderr, "tide: can't write %s\n", script_path);
        goto done;
    }
    fputs("// Generated by tide unity --web. Tide's program for a Unity WebGL player's page, which Tide's package for Unity\n"
          "// puts in it before Unity's own: it runs as the player's TideRunner says (Tide.jslib).\n"
          "var Tide = window.Tide = Object.assign(window.Tide || {}, {\n"
          "    wait: true, pageFrames: true, embedded: true, canvas: document.createElement('canvas'),\n"
          "});\n", f);
    char *encoded = page_payload(wasm, wasm_len, release);
    fprintf(f, "const TIDE_PROGRAM = \"%s\";\n", encoded);
    free(encoded);
    if (packed) {
        char *pack_encoded = page_payload(packed, pack_len, release);
        fprintf(f, "const TIDE_PACK = \"%s\";\n", pack_encoded);
        free(pack_encoded);
    }
    fputs(runtime, f);
    ok = fclose(f) == 0;
    f = NULL;
    if (!ok) fprintf(stderr, "tide: can't write %s\n", script_path);
done:
    if (f) fclose(f);
    free(runtime);
    free(wasm);
    free(packed);
    return ok;
}

// tide unity --web: the game as Tide's program for a Unity WebGL player (tide-game.js), with unity.json. Once.
static int tide_unity_web(const char *root, const build_options *opts)
{
    build b = {0};
    if (!build_setup(&b, root, opts, false)) return 1;
    char *dir = path_absolute(opts->output);
    char *gen = path_join(b.cache, "gen-unity");
    sys_mkdirs(dir);
    sys_mkdirs(gen);
    int code = 1;
    char *main_c = path_join(gen, "main.c");
    char *game_c = path_join(gen, "game.c");
    char *program = path_join(b.cache, "unity.wasm");
    char *script = path_join(dir, "tide-game.js");
    char *map = path_join(dir, "unity.json");
    char *pack = NULL;
    file_list objects = {0};
    c_side c = {0};
    if (!generate(&b, gen, false)) goto done;
    char escaped[512];
    c_string(escaped, sizeof escaped, b.name);
    char *text = malloc(sizeof unity_web_main + strlen(escaped) + sizeof unity_web_exports);
    if (!text) abort();
    sprintf(text, unity_web_main, escaped);
    strcat(text, unity_web_exports);
    const bool wrote = sys_write_text(main_c, text);
    free(text);
    if (!wrote) goto done;
    add_file(&objects, path_join(b.cache, "unity-game.o"));
    add_file(&objects, path_join(b.cache, "unity-main.o"));
    if (!compile_game(&b, gen, game_c, main_c, &objects, &c) || !find_libraries(&b, &c)) goto done;
    if (!link_objects(&b, &objects, &c, program, false, NULL)) goto done;
    pack = pack_of(gen);
    if (!write_unity_script(root, program, script, pack, opts->release)) goto done;
    const char *mapped = b.unity_map ? b.unity_map : "{\"assets\": []}\n";
    if (!sys_write_file(map, mapped, strlen(mapped))) {
        fprintf(stderr, "tide: can't write %s\n", map);
        goto done;
    }
    printf("tide: built the game for Unity's WebGL player in %s\n", dir);
    code = 0;
done:
    free_game_c(&c);
    free_files(&objects);
    free(pack);
    free(map);
    free(script);
    free(program);
    free(game_c);
    free(main_c);
    free(gen);
    free(dir);
    return code;
}

int tide_unity(const char *root, const build_options *opts)
{
    if (opts->web) return tide_unity_web(root, opts);
    run r = {0};
    if (!build_setup(&r.b, root, opts, true)) return 1;
    r.dir = path_absolute(opts->output);
    r.gen = path_join(r.dir, "gen");
    if (!sys_mkdirs(r.gen)) {
        fprintf(stderr, "tide: can't make %s\n", r.gen);
        return 1;
    }
    // A running game swaps in a build numbered past the one it runs: go on from what's there
    r.builds = highest_numbered(r.dir, "game-", LIBRARY_SUFFIX);
    r.packs = highest_numbered(r.dir, "pack-", "");
    if (!write_unity_library(&r) || !build_embed_host(&r)) return 1;
    if (!build_library(&r) || !write_unity_schema(&r)) {
        if (!opts->watch) return 1;
        fprintf(stderr, "tide: the game will build once its files are fixed\n");
    }
    if (!opts->watch) {
        printf("tide: built the game for Unity in %s\n", r.dir);
        return 0;
    }
    printf("tide: building the game for Unity in %s whenever its files change\n", r.dir);
    fflush(stdout);
    sys_read_lines();
    uint64_t stamp = game_stamp(&r.b);
    uint64_t assets = asset_stamp(&r.b);
    int64_t changed_at = -1, assets_changed_at = -1;
    while (!sys_input_ended()) { // The editor closed tide's input, or went away
        sys_sleep_ms(250);
        char line[64];
        while (sys_typed_line(line, sizeof line)) {
            if (strcmp(line, "restart") != 0) continue; // The editor asks the running game to start over
            char *restart = path_join(r.dir, "restart");
            sys_write_text(restart, "");
            free(restart);
        }
        const uint64_t now = game_stamp(&r.b);
        if (now != stamp) {
            stamp = now;
            changed_at = sys_now_ms();
            continue;
        }
        const uint64_t assets_now = asset_stamp(&r.b);
        if (assets_now != assets) {
            assets = assets_now;
            assets_changed_at = sys_now_ms();
            continue;
        }
        if (changed_at >= 0 && sys_now_ms() - changed_at >= SETTLE_MS) {
            changed_at = -1;
            assets_changed_at = -1; // A build brings the pack as the files are now
            if (build_library(&r) && write_unity_schema(&r)) printf("tide: built game-%u\n", (unsigned)r.builds);
            else fprintf(stderr, "tide: the game keeps running its last build\n");
            fflush(stdout);
        } else if (assets_changed_at >= 0 && sys_now_ms() - assets_changed_at >= SETTLE_MS) {
            assets_changed_at = -1;
            if (r.b.info.assets_affect_match) {
                if (!build_library(&r)) fprintf(stderr, "tide: the game keeps running its last build\n");
            } else {
                repack(&r);
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// `tide run --web`: tide serves the game's page on this machine, which asks
// it for the newest build four times a second and starts each new one in the
// running one's place (platform/web/tide.js).

typedef struct web_run {
    run r;
    char *page;
    uint32_t restarts; // Times `r` was typed: the page starts the game over when it changes
    char status[48];
    char *program; // The newest build, read when first asked for
    size_t program_size;
    uint32_t program_build;
    char *pack;    // The newest pack of assets, likewise
    size_t pack_size;
    uint32_t pack_number;
} web_run;

// The game's program for the page: tide/host.h's loop, going on from where
// the last build left it.
static bool write_web_main(const run *r)
{
    char title[600];
    title_field(title, sizeof title, r->b.opts, r->b.name);
    char text[2048];
    snprintf(text, sizeof text,
             "// Generated by tide. Do not edit.\n"
             "#include \"game.h\"\n"
             "#include \"tide/run.h\"\n"
             "\n"
             "extern const tide_layout tide_game_layout;\n"
             "\n"
             "int main(int argc, char **argv)\n"
             "{\n"
             "    static tide_host_game game;\n"
             "    game = tide_host_game_api;\n"
             "    game.layout = &tide_game_layout;\n"
             "    tide_host_run_web(&(tide_run_desc){%s, .stats = %s, .stats_open = %s, .debug_views = %s,\n"
             "                                       .argc = argc, .argv = argv},\n"
             "                      &game);\n"
             "}\n",
             title, r->b.opts->stats ? "true" : "false", r->b.opts->stats_open ? "true" : "false",
             r->b.opts->debug_views ? "true" : "false");
    char *path = path_join(r->gen, "main.c");
    const bool ok = sys_write_text(path, text);
    free(path);
    return ok;
}

// The page: the package's shell, and tide.js as it is, hot reloading and all,
// which loads the program from tide. `args` (--host, --join, --connect, ending with NULL)
// go to the game.
static char *web_page(const char *root, const char *const *args)
{
    char *shell_path = path_join(root, "web/shell.html");
    char *script_path = path_join(root, "web/tide.js");
    char *shell = sys_read_file(shell_path, NULL);
    char *script = sys_read_file(script_path, NULL);
    free(shell_path);
    free(script_path);
    const char *slot = shell ? strstr(shell, "{{{ SCRIPT }}}") : NULL;
    if (!script || !slot) {
        fprintf(stderr, "tide: the web page's files are missing from %s/web; reinstall tide\n", root);
        return NULL;
    }
    char arguments[512];
    page_arguments(arguments, sizeof arguments, args);
    char before[600];
    snprintf(before, sizeof before, "<script>\nTide.reload = true;\n%s", arguments);
    const char *after = "</script>";
    const size_t n = (size_t)(slot - shell) + strlen(before) + strlen(script) + strlen(after)
                   + strlen(slot + strlen("{{{ SCRIPT }}}")) + 1;
    char *page = malloc(n);
    if (!page) abort();
    snprintf(page, n, "%.*s%s%s%s%s", (int)(slot - shell), shell, before, script, after, slot + strlen("{{{ SCRIPT }}}"));
    free(shell);
    free(script);
    return page;
}

static void answer_web(void *user, const char *path, serve_reply *reply)
{
    web_run *w = user;
    char newest[64], pack[64];
    snprintf(newest, sizeof newest, "/game-%u.wasm", (unsigned)w->r.builds);
    snprintf(pack, sizeof pack, "/pack-%u", (unsigned)w->r.packs);
    if (strcmp(path, "/") == 0) {
        *reply = (serve_reply){200, "text/html; charset=utf-8", w->page, strlen(w->page)};
    } else if (w->r.b.opts->watch && strcmp(path, "/build") == 0) {
        // The newest build, how often to start over, and the newest pack of assets (0 for none)
        snprintf(w->status, sizeof w->status, "%u %u %u", (unsigned)w->r.builds, (unsigned)w->restarts, (unsigned)w->r.packs);
        *reply = (serve_reply){200, "text/plain; charset=utf-8", w->status, strlen(w->status)};
    } else if (w->r.packs && strcmp(path, pack) == 0) {
        if (w->pack_number != w->r.packs) {
            char *file = path_join(w->r.dir, pack + 1);
            free(w->pack);
            w->pack = sys_read_file(file, &w->pack_size);
            w->pack_number = w->pack ? w->r.packs : 0;
            free(file);
        }
        if (w->pack) *reply = (serve_reply){200, "application/octet-stream", w->pack, w->pack_size};
    } else if (w->r.b.opts->watch && strcmp(path, newest) == 0) {
        if (w->program_build != w->r.builds) {
            char *file = build_path(&w->r, w->r.builds, ".wasm");
            free(w->program);
            w->program = sys_read_file(file, &w->program_size);
            w->program_build = w->program ? w->r.builds : 0;
            free(file);
        }
        if (w->program) *reply = (serve_reply){200, "application/wasm", w->program, w->program_size};
    }
}

int tide_run_web(const char *root, const build_options *opts, const bool open_page, const char *const *args)
{
    web_run w = {0};
    run *r = &w.r;
    r->web = true;
    if (opts->watch) {
        if (!build_setup(&r->b, root, opts, false)) return 1;
        open_run(r);
        w.page = web_page(root, args);
        if (!w.page || !write_web_main(r) || !build_library(r)) return 1;
    } else {
        char *page = build_game(root, opts, args);
        if (!page) return 1;
        w.page = sys_read_file(page, NULL);
        free(page);
        if (!w.page) return 1;
        r->b.opts = opts;
    }

    uint16_t port = 0;
    serve *server = serve_open(&port);
    if (!server) {
        fprintf(stderr, "tide: can't serve the game's page on this machine\n");
        return 1;
    }
    char url[64];
    snprintf(url, sizeof url, "http://127.0.0.1:%u/", (unsigned)port);
    printf("tide: the game is at %s\n", url); // The VS Code extension reads the address from this line
    if (opts->watch) printf("tide: saving a .tide or C file reloads it; Ctrl+R or r and Enter starts it over, and Ctrl+C stops\n");
    else printf("tide: Ctrl+C stops serving the page\n");
    fflush(stdout);
    if (open_page && !sys_open_in_browser(url)) fprintf(stderr, "tide: couldn't open a browser; open %s in one\n", url);
    if (opts->watch) sys_read_lines();

    uint64_t stamp = opts->watch ? game_stamp(&r->b) : 0;
    uint64_t assets = opts->watch ? asset_stamp(&r->b) : 0;
    int64_t changed_at = -1, assets_changed_at = -1;
    for (;;) {
        serve_poll(server, 250, answer_web, &w);
        if (!opts->watch) continue;
        if (typed_restart()) w.restarts++;
        const uint64_t now = game_stamp(&r->b);
        if (now != stamp) {
            stamp = now;
            changed_at = sys_now_ms();
            continue;
        }
        const uint64_t assets_now = asset_stamp(&r->b);
        if (assets_now != assets) {
            assets = assets_now;
            assets_changed_at = sys_now_ms();
            continue;
        }
        if (changed_at >= 0 && sys_now_ms() - changed_at >= SETTLE_MS) {
            changed_at = -1;
            assets_changed_at = -1;
            if (build_library(r)) printf("tide: built it again; the page reloads it\n");
            else fprintf(stderr, "tide: the page keeps running its last build\n");
            fflush(stdout);
        } else if (assets_changed_at >= 0 && sys_now_ms() - assets_changed_at >= SETTLE_MS) {
            assets_changed_at = -1;
            if (r->b.info.assets_affect_match) {
                if (build_library(r)) printf("tide: built it again; the page reloads it\n");
                else fprintf(stderr, "tide: the page keeps running its last build\n");
                fflush(stdout);
            } else {
                repack(r);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Android: the game as a library for each of Android's CPUs, which its
// NativeActivity loads (see platform/android), in an app tide signs itself
// (apk.h).

// The app's ID: its appId setting, or dev.tide.<its name> to test with, since
// an app store would take that one from whoever got there first.
static bool app_id(const build *b, char *out, const size_t size)
{
    if (b->info.app_id[0]) {
        snprintf(out, size, "%s", b->info.app_id);
        return true;
    }
    char name[128];
    size_t n = 0;
    for (const char *c = b->name; *c && n < sizeof name - 1; c++) {
        if (isalnum((unsigned char)*c)) name[n++] = (char)tolower((unsigned char)*c);
    }
    name[n] = '\0';
    if (b->opts->release) {
        fprintf(stderr, "tide: an app made to ship needs an ID of its own, which phones and app stores know it by\n");
        fprintf(stderr, "  = note: give the game one in its settings, like 'settings { appId = \"com.studio.%s\"; }'\n",
                n ? name : "game");
        return false;
    }
    snprintf(out, size, "dev.tide.%s%s", n && isalpha((unsigned char)name[0]) ? "" : "game", name);
    return true;
}

// The app's version code, which Android and Google Play only take updates
// with higher ones of: the minutes since 2020 began, so each build is newer
// than the last with no number to keep. SOURCE_DATE_EPOCH stands in for now
// where it's set, for builds that come out the same each time.
static int android_version_code(void)
{
    const char *epoch = getenv("SOURCE_DATE_EPOCH");
    const int64_t now = epoch && *epoch ? strtoll(epoch, NULL, 10) : sys_now();
    const int64_t minutes = (now - 1577836800) / 60; // 2020-01-01
    return minutes > 1 ? (int)minutes : 1;
}

// Links the game for one CPU: a library with the platform layer, built for
// Android in the package (lib/android/<abi>), the game's libraries for that
// CPU, and Android's own.
static bool link_android(const build *b, const file_list *objects, const c_side *c, const char *output)
{
    char *lib_dir = path_join(b->root, "lib/android");
    char *abi_dir = path_join(lib_dir, android_abis[b->abi]);
    char *platform_lib = path_join(abi_dir, "libtide_platform.a");
    free(lib_dir);
    if (!sys_exists(platform_lib)) {
        fprintf(stderr, "tide: this installation can't build Android games: %s is missing\n", abi_dir);
        fprintf(stderr, "  = note: reinstall tide, or for a build of this repo package the android-package presets too\n");
        free(abi_dir);
        free(platform_lib);
        return false;
    }
    args a = {0};
    arg_compiler(&a, b->compiler);
    arg(&a, build_target.flag);
    arg(&a, build_target.sysroot_flag);
    arg(&a, "-shared");
    for (int i = 0; i < objects->count; i++) arg(&a, objects->items[i]);
    for (int i = 0; i < c->objects.count; i++) arg(&a, c->objects.items[i]);
    for (int i = 0; i < c->link.count; i++) arg(&a, c->link.items[i]);
    for (int i = 0; i < b->engine.count; i++) arg(&a, b->engine.items[i]);
    arg(&a, platform_lib);
    arg(&a, "-o");
    arg(&a, output);
    arg_list(&a, b->opts->release ? release_flags : debug_flags);
    // The activity and every public shared-library export remain roots. Only
    // unused private sections are collected; stripping never changes exports.
    if (b->opts->release && !b->opts->watch) arg(&a, "-Wl,--gc-sections,-s");
    // The activity's way in, which nothing in the game calls; a C function
    // nothing defines fails here, not when the app starts; pages of 16 KiB,
    // as Android 15's devices can have.
    static const char *const flags[] = {"-fuse-ld=lld", "-Wl,-u,ANativeActivity_onCreate", "-Wl,--no-undefined",
                                        "-Wl,-z,max-page-size=16384", "-nodefaultlibs", "-lc", "-lm", "-ldl",
                                        "-landroid", "-llog", "-lEGL", "-lGLESv3", "-laaudio", NULL};
    arg_list(&a, flags);
    arg(&a, build_target.builtins);
    const int code = sys_run(a.items, NULL, false);
    free(abi_dir);
    free(platform_lib);
    if (code == -1) fprintf(stderr, "tide: couldn't start %s\n", b->compiler);
    // What the link may have lacked: a library that doesn't say it's Android's
    for (int i = 0; code > 0 && i < c->unsure.count; i++) {
        const char *path = c->unsure.items[i];
        if (has_extension(path, ".so")) {
            fprintf(stderr, "  = note: left out %s, which tide took for Linux's: it has no .note.android.ident, the "
                            "note the NDK puts in the libraries it links for Android\n", path);
        } else {
            fprintf(stderr, "  = note: left out %s, which tide took for Linux's: a static library doesn't say which "
                            "of the two it's for, so build it as a shared one (.so) for Android\n", path);
        }
    }
    if (code > 0) explain_link(b, a.items);
    free(a.items);
    return code == 0;
}

// Builds the app, and gives its ID in `package` if it's there. Returns the
// app's path, or NULL after saying what went wrong.
static char *build_android(const char *root, const build_options *opts, char *package, const size_t package_size)
{
    build b = {0};
    if (!build_setup(&b, root, opts, false)) return NULL;
    android_ndk ndk;
    if (!android_find_ndk(root, &ndk)) return NULL;
    char *gen = path_join(b.cache, "gen");
    sys_mkdirs(gen);
    if (!generate(&b, gen, false)) return NULL;
    char id[256];
    if (!app_id(&b, id, sizeof id)) return NULL;
    char *main_c = path_join(gen, "main.c");
    write_main(main_c, opts, b.name);
    char *game_c = path_join(gen, "game.c");

    // Each CPU: objects of its own, in <cache>/<abi>, and the game's libraries
    // for it, of which the shared ones go in the app with the game's. The app
    // is made for Android 16, as Google Play wants.
    char *cache = b.cache;
    apk_desc desc = {.package = id, .lib_name = "game", .version_code = android_version_code(),
                     .version_name = b.info.version[0] ? b.info.version : "1.0",
                     .min_sdk = atoi(ANDROID_API), .target_sdk = 36, .debuggable = !opts->release};
    c_side c[ANDROID_ABIS] = {0};
    apk_lib *needed = NULL;
    for (int abi = 0; abi < ANDROID_ABIS; abi++) {
        char target_flag[64];
        snprintf(target_flag, sizeof target_flag, "--target=%s-linux-android" ANDROID_API, android_arches[abi]);
        build_target.flag = target_flag;
        build_target.sysroot_flag = format("--sysroot=%s", ndk.sysroot, NULL);
        build_target.builtins = ndk.builtins[abi];
        b.abi = abi;
        b.cache = path_join(cache, android_abis[abi]);
        sys_mkdirs(b.cache);
        b.engine_built = false;
        b.engine = (file_list){0};
        file_list objects = {0};
        add_file(&objects, path_join(b.cache, "game.o"));
        add_file(&objects, path_join(b.cache, "main.o"));
        if (!compile_game(&b, gen, game_c, main_c, &objects, &c[abi]) || !find_libraries(&b, &c[abi])) return NULL;
        char *lib = path_join(b.cache, "libgame.so");
        if (!link_android(&b, &objects, &c[abi], lib)) return NULL;
        desc.abis[desc.lib_count] = android_abis[abi];
        desc.libs[desc.lib_count++] = lib;
        needed = realloc(needed, sizeof(apk_lib) * (size_t)(desc.needed_count + c[abi].dynamic.count + 1));
        if (!needed) abort();
        for (int i = 0; i < c[abi].dynamic.count; i++) {
            needed[desc.needed_count++] = (apk_lib){android_abis[abi], c[abi].names.items[i], c[abi].dynamic.items[i]};
        }
    }
    desc.needed = needed;
    build_target = (target){0};

    char *output;
    if (opts->output) {
        output = path_absolute(opts->output);
    } else {
        char file[512];
        snprintf(file, sizeof file, "%s.apk", b.name);
        output = default_output(b.folder, file);
    }
    char *output_dir = path_dir(output);
    sys_mkdirs(output_dir);
    desc.label = opts->title ? opts->title : b.info.title[0] ? b.info.title : b.name;
    // Its icon: the game's icon.png, or Tide's
    char *icon = path_join(b.folder, "icon.png");
    if (!sys_exists(icon)) {
        free(icon);
        icon = path_join(root, "lib/android/icon.png");
    }
    desc.icon = sys_exists(icon) ? icon : NULL;
    char *pack = pack_of(gen);
    desc.pack = sys_exists(pack) ? pack : NULL; // The game's assets, as assets/game.pack
    desc.microphone = b.info.microphone;
    char error[512];
    apk_key key;
    bool made;
    char *key_path = android_key_path();
    if (!apk_key_load(key_path, &key, &made, error, sizeof error)) {
        fprintf(stderr, "tide: %s\n", error);
        return NULL;
    }
    if (made) {
        printf("Made the key that signs your Android apps: %s\n"
               "  Keep a copy somewhere safe. An app's updates have to be signed with the key the app was, and on\n"
               "  Google Play it's your upload key.\n",
               key_path);
    }
    if (!apk_write(output, &desc, &key, error, sizeof error)) {
        fprintf(stderr, "tide: %s\n", error);
        return NULL;
    }
    // Made to ship: an App Bundle too, which Google Play takes
    if (opts->release) {
        const size_t n = strlen(output);
        char *bundle = format("%s%s", output, ".aab");
        if (n > 4 && strcmp(output + n - 4, ".apk") == 0) strcpy(bundle + n - 4, ".aab");
        if (!apk_bundle_write(bundle, &desc, &key, error, sizeof error)) {
            fprintf(stderr, "tide: %s\n", error);
            return NULL;
        }
        printf("Built %s for Google Play\n", bundle);
    }
    for (int abi = 0; abi < ANDROID_ABIS; abi++) free_game_c(&c[abi]);
    free(needed);
    if (package) snprintf(package, package_size, "%s", id);
    return output;
}

// adb's devices, as `adb devices` lists them: how many are ready.
static int android_devices(const char *adb)
{
    char out[4096];
    const char *const argv[] = {adb, "devices", NULL};
    if (sys_capture(argv, out, sizeof out) != 0) return 0;
    int count = 0;
    for (const char *line = strchr(out, '\n'); line; line = strchr(line + 1, '\n')) {
        const char *tab = strchr(line + 1, '\t');
        const char *end = strchr(line + 1, '\n');
        if (tab && (!end || tab < end) && strncmp(tab + 1, "device", 6) == 0) count++;
    }
    return count;
}

// The app's process on the device, or 0 when it isn't running.
static long android_pid(const char *adb, const char *package)
{
    char out[256];
    const char *const argv[] = {adb, "shell", "pidof", package, NULL};
    if (sys_capture(argv, out, sizeof out) != 0) return 0;
    return strtol(out, NULL, 10);
}

// Installs the app. One installed before with another key (another
// machine's) can't be updated: it goes, and this one comes in its place.
static bool install_app(const char *adb, const char *apk, const char *package)
{
    printf("Installing %s...\n", path_base(apk));
    fflush(stdout);
    const char *const install[] = {adb, "install", "-r", apk, NULL};
    char out[4096];
    if (sys_capture(install, out, sizeof out) == 0) return true;
    if (strstr(out, "INSTALL_FAILED_UPDATE_INCOMPATIBLE") || strstr(out, "signatures do not match")) {
        printf("The app on the device was signed with another key: replacing it.\n");
        const char *const uninstall[] = {adb, "uninstall", package, NULL};
        sys_run(uninstall, NULL, true);
    }
    if (sys_run(install, NULL, false) == 0) return true;
    fprintf(stderr, "tide: the device didn't take the app\n");
    return false;
}

// Starts the app, or starts it over, with what goes to its main
// (platform/android): the session's flags.
static bool start_app(const char *adb, const char *package, const char *const *args)
{
    char start[1024];
    int at = snprintf(start, sizeof start, "am start -S -n %s/android.app.NativeActivity", package);
    if (args[0]) {
        at += snprintf(start + at, sizeof start - (size_t)at, " --es tide.args '");
        for (int i = 0; args[i]; i++) {
            for (const char *c = args[i]; *c && at < (int)sizeof start - 8; c++) {
                if (*c != '\'') start[at++] = *c; // Codes and addresses never have quotes
            }
            if (args[i + 1]) start[at++] = ' ';
        }
        at += snprintf(start + at, sizeof start - (size_t)at, "'");
    }
    const char *const launch[] = {adb, "shell", start, NULL};
    if (sys_run(launch, NULL, true) == 0) return true;
    fprintf(stderr, "tide: the app didn't start\n");
    return false;
}

int tide_run_android(const char *root, const build_options *opts, const char *const *args)
{
    char *adb = android_find_adb(root);
    if (!adb) return 1;
    if (android_devices(adb) == 0) {
        fprintf(stderr, "tide: no Android phone or emulator is connected\n");
        fprintf(stderr, "  = note: on the phone, turn on USB debugging (Settings > About phone: tap Build number seven "
                        "times; then System > Developer options > USB debugging), connect it, and allow this computer\n");
        return 1;
    }
    char package[256];
    char *apk = build_android(root, opts, package, sizeof package);
    if (!apk || !install_app(adb, apk, package)) return 1;
    const char *const clear[] = {adb, "logcat", "-c", NULL};
    sys_run(clear, NULL, true);
    if (!start_app(adb, package, args)) return 1;

    // What it prints, until it ends; saving a file makes the app again and
    // starts it over, as the device can't swap code into a running app.
    printf("Running %s on the device; its output follows.\n", package);
    if (opts->watch) printf("Saving a .tide or C file builds it again and starts it over; type r and press Enter to start it "
                           "over, or close it to stop.\n");
    fflush(stdout);
    if (opts->watch) sys_read_lines();
    const char *const logs[] = {adb, "logcat", "-v", "raw", "-s", "tide:V", NULL};
    sys_process *log = sys_start(logs, NULL);
    build watch = {0}; // The game's files and its packages', for their stamp
    if (opts->watch) {
        watch.folder = path_absolute(opts->folder);
        find_packages(watch.folder, &watch.packages);
    }
    uint64_t stamp = opts->watch ? game_stamp(&watch) : 0;
    int64_t changed_at = -1;
    bool seen = false;
    int waits = 0; // Half seconds without the app, since it was started
    for (;;) {
        int code;
        if (log && sys_wait(log, 500, &code)) log = NULL;
        bool restart = opts->watch && typed_restart();
        const uint64_t now = opts->watch ? game_stamp(&watch) : stamp;
        if (now != stamp) {
            stamp = now;
            changed_at = sys_now_ms();
        } else if (changed_at >= 0 && sys_now_ms() - changed_at >= SETTLE_MS) {
            changed_at = -1;
            char *rebuilt = build_android(root, opts, package, sizeof package);
            if (rebuilt && install_app(adb, rebuilt, package)) restart = true;
            else fprintf(stderr, "tide: the device keeps running the last build\n");
        }
        if (restart) {
            if (!start_app(adb, package, args)) break;
            seen = false;
            waits = 0;
            continue;
        }
        const long pid = android_pid(adb, package);
        if (pid) seen = true;
        else waits++;
        if ((seen && !pid) || (!seen && waits > 40) || !log) break;
    }
    if (log) sys_kill(log);
    printf(seen ? "The app ended.\n" : "The app didn't start.\n");
    return seen ? 0 : 1;
}

bool tide_schedule(const build_options *opts, const bool shaders)
{
    char *folder = path_absolute(opts->folder);
    game_packages packages = {0};
    file_list paths;
    int count = 0;
    compile_input *inputs = find_packages(folder, &packages) ? game_inputs(folder, &packages, &paths, &count) : NULL;
    if (!inputs) return false;
    const codegen_options codegen = {.name = game_name(folder), .line_directives = true, .shaders = shaders, .game_folder = folder,
                                     .defines = opts->defines, .define_count = opts->define_count};
    sb text = {0};
    if (!compile_inputs(inputs, count, &codegen, &text)) return false;
    fputs(text.data, stdout);
    return true;
}
