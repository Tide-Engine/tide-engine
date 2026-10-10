#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compile.h"
#include "lexer.h"

static void usage(void)
{
    fprintf(stderr,
            "usage: tidec <file.tide>... -o <output-dir> [--name <name>] [--no-line] [--layout]\n"
            "             [--define <SYMBOL>]...\n"
            "       tidec <file.tide>... --schedule [--name <name>]\n"
            "       tidec <file.tide>... --shaders\n"
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
            "  --no-line      don't map generated code back to .tide lines for debuggers\n"
            "  --layout       also describe the data layout, as tide run does for hot reloading\n"
            "  --strip-unused-assets  leave unreferenced assets out of generated loaders and the pack\n"
            "  --game <dir>   the game's folder, where .tide/ keeps its asset hashes and fetched URLs\n"
            "  --pack-c       also write the assets' pack as C (<name>_pack.c: tide_game_pack), for a host that embeds it\n"
            "  --schedule     print which systems can run at the same time and why the others\n"
            "                 wait, instead of generating code\n"
            "  --shaders      print the shaders the game's mesh draw calls draw through, as\n"
            "                 GLSL and WGSL, instead of generating code\n");
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
        fprintf(stderr, "tidec: --define takes 256 symbols at most\n");
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
    inputs->items[inputs->count++] = (compile_input){path, inputs->package};
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
        fprintf(stderr, "tidec: can't read the list of files '%s'\n", path);
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

int main(const int argc, char **argv)
{
    input_list inputs = {0};
    codegen_options opts = {0};
    opts.line_directives = true;
    bool schedule = false;
    bool pack_c = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts.out_dir = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            opts.name = argv[++i];
        } else if (strcmp(argv[i], "--strip-unused-assets") == 0) {
            opts.strip_unused_assets = true;
        } else if (strcmp(argv[i], "--no-line") == 0) {
            opts.line_directives = false;
        } else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            opts.game_folder = argv[++i];
        } else if (strcmp(argv[i], "--pack-c") == 0) {
            pack_c = true;
        } else if (strcmp(argv[i], "--layout") == 0) {
            opts.layout = true;
        } else if (strcmp(argv[i], "--schedule") == 0) {
            schedule = true;
        } else if (strcmp(argv[i], "--shaders") == 0) {
            schedule = true;
            opts.shaders = true;
        } else if (strcmp(argv[i], "--package") == 0 && i + 1 < argc) {
            inputs.package = argv[++i];
        } else if (strcmp(argv[i], "--define") == 0 && i + 1 < argc) {
            if (!add_define(argv[++i])) return 2;
        } else if (argv[i][0] == '-') {
            usage();
            return 2;
        } else if (argv[i][0] == '@') {
            if (!add_listed(argv[i] + 1, &inputs)) return 2;
        } else if (!add_input(&inputs, argv[i])) {
            return 2;
        }
    }
    if (inputs.count == 0 || (!opts.out_dir && !schedule)) {
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

    if (!schedule) return compile_inputs(inputs.items, inputs.count, &opts, NULL) ? 0 : 1;
    sb text = {0};
    if (!compile_inputs(inputs.items, inputs.count, &opts, &text)) return 1;
    fputs(text.data, stdout);
    return 0;
}
