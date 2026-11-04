#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_sizes.h"
#include "compile.h"
#include "lexer.h"

static void usage(void)
{
    fprintf(stderr,
            "usage: tidec <file.tide>... -o <output-dir> [--name <name>] [--no-line] [--visualizers] [--layout] [--embed]\n"
            "             [--define <SYMBOL>]...\n"
            "       tidec <file.tide>... --schedule [--name <name>]\n"
            "       tidec <file.tide>... --shaders\n"
            "       tidec <file.tide>... --outline\n"
            "       tidec <file.tide>... --spirv <dir>\n"
            "       tidec <file.tide>... --hlsl <dir>\n"
            "\n"
            "Transpiles a Tide program, made of one or more files, to\n"
            "<output-dir>/<name>.h and <name>.c.\n"
            "  @<list>        the files a text file lists, one per line: for more files\n"
            "                 than a command line holds\n"
            "  --package <P>  the files after it are package P's, to the next --package;\n"
            "                 a line of a list can say it too. Packages' files compile\n"
            "                 first, in the order the packages are named\n"
            "  --define <S>   a symbol every file's #if conditions read as defined, like DEBUG;\n"
            "                 a line of a list can say it too. PACKAGE_<NAME> is defined for\n"
            "                 each package named\n"
            "  --name <name>  base name of the generated files (default: the first file's name)\n"
            "  --scene <name> start directly in this qualified prefab or scene, overriding settings.startScene\n"
            "  --no-line      don't map generated code back to .tide lines for debuggers\n"
            "  --visualizers  also write <name>.natvis and <name>_lldb.py, how debuggers show the game's values\n"
            "  --layout       also describe the data layout, as tide run does for hot reloading\n"
            "  --embed        ...and what another program needs to run the game (tide/embed.h), as tide unity builds it\n"
            "  --declarations the game's types alone: its systems, views, handlers, functions, frags and verts\n"
            "                 left out, as tide unity builds them while the code doesn't build\n"
            "  --strip-unused-assets  leave unreferenced assets out of generated loaders and the pack\n"
            "  --strip-names          strip diagnostic/shader identifiers; write a private <name>.names map\n"
            "  --server               preserve the client identity and emit server build metadata\n"
            "  --strip-server-assets  also retain only simulation assets (explicit C dependency promise)\n"
            "  --game <dir>   the game's folder, where .tide/ keeps its asset hashes and fetched URLs\n"
            "  --pack-c       also write the assets' pack as C (<name>_pack.c: tide_game_pack), for a host that embeds it\n"
            "  --asset-sizes  also print what each asset declaration adds to the pack, biggest first\n"
            "  --depfile <file> write source and transitive asset dependencies for the generated C\n"
            "  --build-cc <cc>  the C compiler that builds the program running the game's build code (clang);\n"
            "                   with --build-include <engine/include> and --build-engine <engine/src>, the\n"
            "                   engine's headers and sources it's built with. --build-flag <f> adds a flag to\n"
            "                   each compile and link, --build-lib <l> to the link\n"
            "  --schedule     print which systems can run at the same time and why the others\n"
            "                 wait, instead of generating code\n"
            "  --shaders      print the shaders the game's mesh draw calls draw through, as\n"
            "                 GLSL and WGSL, instead of generating code\n"
            "  --outline      print what the game declares, file by file, as JSON: kinds, names,\n"
            "                 places, fields, and what systems read, write and wait for,\n"
            "                 instead of generating code\n"
            "  --spirv <dir>  write each of those shaders as SPIR-V, Vulkan's, into <dir>\n"
            "                 as <n>.spv in --shaders' order, instead of generating code\n"
            "  --hlsl <dir>   ...or as HLSL, Unity's (tide unity), as <n>.hlsl\n");
}

// A game's files as they're named, each with the package it's in.
typedef struct input_list {
    compile_input *items;
    int count;
    int room;
    const char *package; // What `--package` last said: the files after it are that package's
} input_list;

// The symbols `--define` named.
static const char *defines[256];
static int define_count;

static char *overlay_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = size >= 0 ? malloc((size_t)size + 1u) : NULL;
    const size_t read = text ? fread(text, 1, (size_t)size, f) : 0u;
    const bool ok = text && read == (size_t)size && !ferror(f);
    fclose(f);
    if (!ok) { free(text); return NULL; }
    text[read] = '\0';
    return text;
}

static bool add_define(const char *name)
{
    if (!lex_symbol_ok(name)) {
        fprintf(stderr, "tidec: --define takes a symbol's name, like `--define FAST`, and '%s' isn't one%s\n", name,
                strncmp(name, "TIDE_", 5) == 0 || strncmp(name, "PACKAGE_", 8) == 0
                    ? ": symbols that start with TIDE_ and PACKAGE_ are tide's to say"
                    : "");
        return false;
    }
    if (define_count == 256) {
        fprintf(stderr, "tidec: --define takes 256 symbols at most; remove unused definitions or combine conditions\n");
        return false;
    }
    defines[define_count++] = name;
    return true;
}

static bool add_input(input_list *inputs, const char *path)
{
    if (inputs->count == inputs->room) {
        inputs->room = inputs->room ? inputs->room * 2 : 64;
        inputs->items = realloc(inputs->items, (size_t)inputs->room * sizeof(compile_input));
        if (!inputs->items) return false;
    }
    inputs->items[inputs->count++] = (compile_input){path, inputs->package, NULL};
    return true;
}

// The order files compile in, which is the default order of systems: each
// package's, in the order the packages were first named, then the game's own,
// each by path (see path_compare), as tide gives them.
static const input_list *ordering;

static int package_rank(const char *package)
{
    if (!package) return ordering->count; // The game's own, after every package's
    for (int i = 0; i < ordering->count; i++) {
        const char *other = ordering->items[i].package;
        if (other && strcmp(other, package) == 0) return i;
    }
    return ordering->count;
}

static int input_order(const void *a, const void *b)
{
    const compile_input *x = a;
    const compile_input *y = b;
    const int rank = package_rank(x->package) - package_rank(y->package);
    return rank ? rank : path_compare(x->path, y->path);
}

// The files a list names, one per line, added to `inputs`: a game of thousands
// of files is more than a command line holds (32,767 characters on Windows).
// A line `--package <name>` says the files after it are that package's.
// False when the list can't be read.
static bool add_listed(const char *path, input_list *inputs)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "tidec: can't read the list of files '%s'; check that it exists and is readable\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)(size > 0 ? size : 0) + 1u); // Kept: the inputs are its lines
    const size_t read = text && size > 0 ? fread(text, 1, (size_t)size, f) : 0u;
    fclose(f);
    if (!text) return false;
    text[read] = '\0';
    for (char *line = text; *line;) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r') end++;
        char *next = end;
        while (*next == '\n' || *next == '\r') next++;
        *end = '\0';
        if (strncmp(line, "--package ", 10) == 0) {
            inputs->package = line + 10;
        } else if (strncmp(line, "--define ", 9) == 0) {
            if (!add_define(line + 9)) return false;
        } else if (*line && !add_input(inputs, line)) {
            return false;
        }
        line = next;
    }
    return true;
}

// Make-style dependencies, consumed by CMake/Ninja. Windows separators are
// normalized before escaping; dollar signs, spaces, hashes and drive colons
// must remain part of a path rather than become build-language syntax.
static void dependency_path(FILE *file, const char *path)
{
    for (; *path; path++) {
        char c = *path;
#ifdef _WIN32
        if (c == '\\') c = '/';
#endif
        if (c == '$') fputc('$', file);
        else if (c == ' ' || c == '\t' || c == '#' || c == ':' || c == '\\') fputc('\\', file);
        fputc(c, file);
    }
}

static bool write_dependencies(const char *path, const codegen_options *opts,
                               const input_list *inputs, const game_info *info)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "tidec: can't write dependency file '%s'; create its parent folder and check write permissions\n", path);
        return false;
    }
    sb target = {0};
    sb_printf(&target, "%s/%s.c", opts->out_dir, opts->name);
    dependency_path(file, target.data);
    fputc(':', file);
    for (int i = 0; i < inputs->count; i++) {
        fputs(" \\\n  ", file);
        dependency_path(file, inputs->items[i].path);
    }
    for (int i = 0; i < info->asset_count; i++) {
        fputs(" \\\n  ", file);
        dependency_path(file, info->asset_paths[i]);
    }
    fputc('\n', file);
    bool ok = !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok) fprintf(stderr, "tidec: can't finish dependency file '%s'; check free disk space and write permissions, then retry\n", path);
    return ok;
}

int main(const int argc, char **argv)
{
    input_list inputs = {0};
    codegen_options opts = {0};
    asset_sizes sizes = {0};
    const char **build_cc = calloc(2, sizeof *build_cc);
    const char **build_flags = calloc((size_t)argc + 1u, sizeof *build_flags);
    const char **build_libs = calloc((size_t)argc + 1u, sizeof *build_libs);
    if (!build_cc || !build_flags || !build_libs) return 2;
    int build_flag_count = 0, build_lib_count = 0;
    opts.line_directives = true;
    bool schedule = false;
    bool pack_c = false;
    const char *depfile = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts.out_dir = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            opts.name = argv[++i];
        } else if (strcmp(argv[i], "--scene") == 0 && i + 1 < argc && !opts.scene) {
            opts.scene = argv[++i];
        } else if (strcmp(argv[i], "--source-overlay") == 0 && i + 2 < argc && !opts.editor_source) {
            opts.editor_source = argv[++i];
            const char *replacement = argv[++i];
            opts.editor_text = overlay_text(replacement);
            if (!opts.editor_text) {
                fprintf(stderr, "tidec: can't read the source overlay replacement '%s'; check that it exists and is readable\n", replacement);
                return 2;
            }
        } else if (strcmp(argv[i], "--strip-unused-assets") == 0) {
            opts.strip_unused_assets = true;
        } else if (strcmp(argv[i], "--strip-names") == 0) {
            opts.strip_names = true;
        } else if (strcmp(argv[i], "--server") == 0) {
            opts.server = true;
        } else if (strcmp(argv[i], "--strip-server-assets") == 0) {
            opts.server = opts.strip_server_assets = true;
        } else if (strcmp(argv[i], "--no-line") == 0) {
            opts.line_directives = false;
        } else if (strcmp(argv[i], "--visualizers") == 0) {
            opts.visualizers = true;
        } else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            opts.game_folder = argv[++i];
        } else if (strcmp(argv[i], "--build-cc") == 0 && i + 1 < argc) {
            build_cc[0] = argv[++i];
            opts.build_cc = build_cc;
        } else if (strcmp(argv[i], "--build-flag") == 0 && i + 1 < argc) {
            build_flags[build_flag_count++] = argv[++i];
            opts.build_flags = build_flags;
        } else if (strcmp(argv[i], "--build-lib") == 0 && i + 1 < argc) {
            build_libs[build_lib_count++] = argv[++i];
            opts.build_libs = build_libs;
        } else if (strcmp(argv[i], "--build-include") == 0 && i + 1 < argc) {
            opts.build_include = argv[++i];
        } else if (strcmp(argv[i], "--build-engine") == 0 && i + 1 < argc) {
            opts.build_engine = argv[++i];
        } else if (strcmp(argv[i], "--pack-c") == 0) {
            pack_c = true;
        } else if (strcmp(argv[i], "--asset-sizes") == 0) {
            opts.asset_size_out = asset_sizes_add;
            opts.asset_size_user = &sizes;
        } else if (strcmp(argv[i], "--depfile") == 0 && i + 1 < argc && !depfile) {
            depfile = argv[++i];
        } else if (strcmp(argv[i], "--layout") == 0) {
            opts.layout = true;
        } else if (strcmp(argv[i], "--embed") == 0) {
            opts.embed = true;
        } else if (strcmp(argv[i], "--declarations") == 0) {
            opts.declarations_only = true;
        } else if (strcmp(argv[i], "--editor-prefab") == 0 && i + 1 < argc) {
            opts.editor_prefab = argv[++i];
        } else if (strcmp(argv[i], "--editor-local") == 0) {
            opts.editor_local = true;
        } else if (strcmp(argv[i], "--schedule") == 0) {
            schedule = true;
        } else if (strcmp(argv[i], "--shaders") == 0) {
            schedule = true;
            opts.shaders = true;
        } else if (strcmp(argv[i], "--outline") == 0) {
            schedule = true;
            opts.outline = true;
        } else if (strcmp(argv[i], "--spirv") == 0 && i + 1 < argc) {
            schedule = true;
            opts.shaders = true;
            opts.spirv_dir = argv[++i];
        } else if (strcmp(argv[i], "--hlsl") == 0 && i + 1 < argc) {
            schedule = true;
            opts.shaders = true;
            opts.hlsl_dir = argv[++i];
        } else if (strcmp(argv[i], "--package") == 0 && i + 1 < argc) {
            inputs.package = argv[++i];
        } else if (strcmp(argv[i], "--define") == 0 && i + 1 < argc) {
            if (!add_define(argv[++i])) return 2;
        } else if (argv[i][0] == '-') {
            static const char *const valued[] = {"-o", "--name", "--scene", "--game", "--depfile", "--editor-prefab",
                                                "--spirv", "--hlsl", "--package", "--define", "--build-cc",
                                                "--build-flag", "--build-lib", "--build-include", "--build-engine"};
            bool takes_value = false;
            for (size_t j = 0; j < sizeof valued / sizeof valued[0]; j++) {
                if (strcmp(argv[i], valued[j]) == 0) takes_value = true;
            }
            if ((strcmp(argv[i], "--scene") == 0 && opts.scene) || (strcmp(argv[i], "--depfile") == 0 && depfile)
                || (strcmp(argv[i], "--source-overlay") == 0 && opts.editor_source))
                fprintf(stderr, "tidec: '%s' can be specified only once; remove the duplicate option\n", argv[i]);
            else if (takes_value)
                fprintf(stderr, "tidec: '%s' needs a value after it; see the usage below\n", argv[i]);
            else if (strcmp(argv[i], "--source-overlay") == 0)
                fprintf(stderr, "tidec: --source-overlay needs the original source path and a replacement file path\n");
            else
                fprintf(stderr, "tidec: unknown option '%s'; check its spelling in the usage below\n", argv[i]);
            usage();
            return 2;
        } else if (argv[i][0] == '@') {
            if (!add_listed(argv[i] + 1, &inputs)) return 2;
        } else if (!add_input(&inputs, argv[i])) {
            return 2;
        }
    }
    if (inputs.count == 0 || (!opts.out_dir && !schedule) || (depfile && (schedule || !*depfile))) {
        if (inputs.count == 0)
            fprintf(stderr, "tidec: no source files; pass one or more .tide files, or @<list> for a file listing them\n");
        else if (!opts.out_dir && !schedule)
            fprintf(stderr, "tidec: no output folder; pass '-o <output-dir>' to generate C, or choose an inspection mode below\n");
        else if (!*depfile)
            fprintf(stderr, "tidec: --depfile needs a nonempty file path\n");
        else
            fprintf(stderr, "tidec: --depfile requires C generation; remove it for an inspection mode, or use '-o <output-dir>' without that mode\n");
        usage();
        return 2;
    }
    opts.defines = defines;
    opts.define_count = define_count;
    // By the order the packages were named in, which sorting would lose
    input_list named = inputs;
    named.items = malloc((size_t)inputs.count * sizeof(compile_input));
    if (!named.items) return 2;
    memcpy(named.items, inputs.items, (size_t)inputs.count * sizeof(compile_input));
    ordering = &named;
    qsort(inputs.items, (size_t)inputs.count, sizeof(compile_input), input_order);
    // The generated files are named after the game's first file, not a package's
    if (!opts.name) {
        for (int i = 0; i < inputs.count && !opts.name; i++) {
            if (!inputs.items[i].package) opts.name = path_stem(inputs.items[i].path);
        }
    }

    // The game's assets go beside the generated files as <name>.pack, and as C for a host that embeds them
    sb pack = {0}, pack_c_path = {0};
    if (opts.out_dir) {
        sb_printf(&pack, "%s/%s.pack", opts.out_dir, opts.name);
        sb_printf(&pack_c_path, "%s/%s_pack.c", opts.out_dir, opts.name);
        opts.pack = pack.data;
        if (pack_c) opts.pack_c = pack_c_path.data;
    }

    if (!schedule) {
        game_info info = {0};
        if (depfile) opts.info = &info;
        bool ok = compile_inputs(inputs.items, inputs.count, &opts, NULL);
        if (ok && depfile) ok = write_dependencies(depfile, &opts, &inputs, &info);
        if (ok) asset_sizes_print(stdout, &sizes);
        asset_sizes_free(&sizes);
        for (int i = 0; i < info.asset_count; i++) free(info.asset_paths[i]);
        free(info.asset_paths);
        return ok ? 0 : 1;
    }
    sb text = {0};
    if (!compile_inputs(inputs.items, inputs.count, &opts, &text)) return 1;
    fputs(text.data, stdout);
    return 0;
}
