#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compile.h"

static void usage(void)
{
    fprintf(stderr,
            "usage: tidec <file.tide>... -o <output-dir> [--name <name>] [--no-line] [--layout]\n"
            "       tidec <file.tide>... --schedule [--name <name>]\n"
            "\n"
            "Transpiles a Tide program, made of one or more files, to\n"
            "<output-dir>/<name>.h and <name>.c.\n"
            "  @<list>        the files a text file lists, one per line: for more files\n"
            "                 than a command line holds\n"
            "  --name <name>  base name of the generated files (default: the first file's name)\n"
            "  --no-line      don't map generated code back to .tide lines for debuggers\n"
            "  --layout       also describe the data layout, as tide run does for hot reloading\n"
            "  --schedule     print which systems can run at the same time and why the others\n"
            "                 wait, instead of generating code\n");
}

// The files a list names, one per line, added to `inputs`: a game of thousands
// of files is more than a command line holds (32,767 characters on Windows).
// False when the list can't be read.
static bool add_listed(const char *path, const char ***inputs, int *count, int *room)
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
        if (*line) {
            if (*count == *room) {
                *room = *room ? *room * 2 : 64;
                *inputs = realloc(*inputs, (size_t)*room * sizeof(char *));
                if (!*inputs) return false;
            }
            (*inputs)[(*count)++] = line;
        }
        line = next;
    }
    return true;
}

int main(const int argc, char **argv)
{
    int input_room = argc;
    const char **inputs = calloc((size_t)argc, sizeof(char *));
    int input_count = 0;
    codegen_options opts = {NULL, NULL, true, false, NULL, NULL};
    bool schedule = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            opts.out_dir = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            opts.name = argv[++i];
        } else if (strcmp(argv[i], "--no-line") == 0) {
            opts.line_directives = false;
        } else if (strcmp(argv[i], "--layout") == 0) {
            opts.layout = true;
        } else if (strcmp(argv[i], "--schedule") == 0) {
            schedule = true;
        } else if (argv[i][0] == '-') {
            usage();
            return 2;
        } else if (argv[i][0] == '@') {
            if (!add_listed(argv[i] + 1, &inputs, &input_count, &input_room)) return 2;
        } else {
            if (input_count == input_room) {
                input_room *= 2;
                inputs = realloc(inputs, (size_t)input_room * sizeof(char *));
                if (!inputs) return 2;
            }
            inputs[input_count++] = argv[i];
        }
    }
    if (input_count == 0 || (!opts.out_dir && !schedule)) {
        usage();
        return 2;
    }

    if (!schedule) return compile_program(inputs, input_count, &opts, NULL) ? 0 : 1;
    sb text = {0};
    if (!compile_program(inputs, input_count, &opts, &text)) return 1;
    fputs(text.data, stdout);
    return 0;
}
