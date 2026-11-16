#include "compile.h"
#include "shader_names.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "tweaks.h"
#include "asset_cache.h"
#include "asset_files.h"
#include "build_code.h"
#include "lexer.h"
#include "indexing.h"

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    char *text = arena_alloc((size_t)size + 1);
    *len = fread(text, 1, (size_t)size, f);
    fclose(f);
    return text;
}

const char *path_stem(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    const char *dot = strrchr(base, '.');
    const size_t n = dot ? (size_t)(dot - base) : strlen(base);
    char *out = arena_alloc(n + 1);
    memcpy(out, base, n);
    out[n] = '\0';
    return out;
}

// The game's types alone (codegen_options.declarations_only): what it wrote that runs, its systems, views, handlers,
// functions, frags and verts, is left out, and the rest checked and generated as it would be. Tide's own and
// package functions/shaders stay: a package's field types/defaults can need their signatures and real callbacks.
static void keep_declarations(program *prog)
{
    int kept = 0;
    for (int i = 0; i < prog->decls.count; i++) {
        decl *const d = prog->decls.items[i];
        const bool package = d->unit && d->unit->src && d->unit->src->package;
        const bool runs = d->kind == DECL_SYSTEM
            || (!package && (d->kind == DECL_FUNCTION || d->kind == DECL_SHADER));
        if (runs && !d->builtin) continue;
        prog->decls.items[kept++] = d;
    }
    prog->decls.count = kept;
}

// A build that isn't a dev one (codegen_options.dev) leaves `dev` declarations out before checking, as if they weren't
// written. The checker holds code without `dev` to not using them in every build (find_named), so leaving them out
// changes nothing else; what names one anyway is told why (prog->left_out).
static void leave_out_dev(program *prog)
{
    int kept = 0;
    for (int i = 0; i < prog->decls.count; i++) {
        decl *const d = prog->decls.items[i];
        if (d->is_dev) {
            vec_push(prog->left_out, d);
            continue;
        }
        prog->decls.items[kept++] = d;
        // ...and a type's dev methods
        int methods = 0;
        for (int m = 0; m < d->methods.count; m++) {
            decl *const method = d->methods.items[m];
            if (method->is_dev) vec_push(prog->left_out, method);
            else d->methods.items[methods++] = method;
        }
        d->methods.count = methods;
    }
    prog->decls.count = kept;
}

// For another program (tidec --embed): a local handler of each of the match's own events, as it's heard and once it's
// verified, so the program hears what the game's local handlers would, once each under rollback. Their bodies are
// generated (gen_heard: tide_embed_heard_<Event>); nothing of them is in the match, as for any local handler.
static bool add_embed_taps(program *prog)
{
    sb text = {0};
    const int count = prog->decls.count;
    for (int i = 0; i < count; i++) {
        const decl *d = prog->decls.items[i];
        if (d->kind != DECL_EVENT || d->builtin || !d->unit || d->is_local || d->is_command) continue;
        // Its name from the global namespace, which the parser hasn't qualified yet
        sb name = {0};
        if (d->unit->ns.len) sb_printf(&name, STR_FMT ".", STR_ARG(d->unit->ns));
        sb_printf(&name, STR_FMT, STR_ARG(d->name));
        sb_printf(&text, "local event(%s heard) TideEmbedHeard%d() { }\n"
                         "local event(verified %s heard) TideEmbedVerified%d() { }\n",
                  name.data, i, name.data, i);
    }
    if (!text.len) return true;
    source *src = NEW(source);
    src->path = "<tide-embed-events>";
    src->text = text.data;
    src->len = text.len;
    diag_add_source(src);
    token *toks = lex(src);
    if (!toks || !parse_file(prog, src, toks, false)) return false;
    for (int i = count; i < prog->decls.count; i++) prog->decls.items[i]->embed_tap = true;
    return true;
}

static bool compile_inputs_impl(const compile_input *inputs, const int count, const codegen_options *opts, sb *schedule)
{
    diag_reset();
    diag_phase(NULL);
    program *prog = program_new();
    prog->startup_name = opts->scene;
    // What every file's #if conditions read as defined: the build's own symbols, and its packages
    lex_symbols_reset();
    for (int i = 0; i < opts->define_count; i++) lex_define(opts->defines[i]);
    for (int i = 0; i < count; i++) {
        if (inputs[i].package) lex_define_package(inputs[i].package);
    }
    unsigned stored = 0; // A file loads or lists from the server's store, whose answers' events are Tide's own code too
    bool web = false; // A file names Web.Request's types, which are Tide's own code
    const char **texts = arena_alloc(sizeof(char *) * (size_t)(count ? count : 1)); // ...or a fill's, which are too
    size_t *lens = arena_alloc(sizeof(size_t) * (size_t)(count ? count : 1));
    for (int i = 0; i < count; i++) {
        compile_progress("Parsing Tide", inputs[i].path, i, count);
        source *src = NEW(source);
        src->path = inputs[i].path;
        src->package = inputs[i].package;
        char *text;
        const char *given = inputs[i].text;
        if (given) {
            src->len = strlen(given);
            text = arena_alloc(src->len + 1);
            memcpy(text, given, src->len + 1);
        } else text = read_file(inputs[i].path, &src->len);
        if (!text) {
            fprintf(stderr, "tidec: can't read source file '%s'; check that it exists and is readable\n", inputs[i].path);
            return false;
        }
        src->text = text;
        diag_add_source(src);
        web |= web_prelude_wanted(text, src->len);
        stored |= storage_prelude_wanted(text, src->len);
        texts[i] = text;
        lens[i] = src->len;

        token *toks = lex(src);
        if (!toks) return false;
        if (!parse_file(prog, src, toks, false)) return false;
        compile_progress("Parsing Tide", inputs[i].path, i + 1, count);
    }
    if (web) {
        source *src = NEW(source);
        src->path = WEB_PRELUDE_PATH;
        src->text = web_prelude(&src->len);
        diag_add_source(src);
        token *toks = lex(src);
        if (!toks || !parse_file(prog, src, toks, false)) return false;
    }
    if (stored) {
        source *src = NEW(source);
        src->path = STORAGE_PRELUDE_PATH;
        src->text = storage_prelude(stored, &src->len);
        diag_add_source(src);
        token *toks = lex(src);
        if (!toks || !parse_file(prog, src, toks, false)) return false;
    }
    if (draw_prelude_wanted(texts, lens, count)) {
        source *src = NEW(source);
        src->path = DRAW_PRELUDE_PATH;
        src->text = draw_prelude(&src->len);
        diag_add_source(src);
        token *toks = lex(src);
        if (!toks || !parse_file(prog, src, toks, false)) return false;
    }
    if (baked_prelude_wanted(texts, lens, count)) {
        source *src = NEW(source);
        src->path = BAKED_PRELUDE_PATH;
        src->text = baked_prelude(&src->len);
        diag_add_source(src);
        token *toks = lex(src);
        if (!toks || !parse_file(prog, src, toks, false)) return false;
    }

    diag_phase("lexing and parsing");
    if (!opts->dev) leave_out_dev(prog);
    // The game's .tweaks files: in its folder and the folders in it, or beside the files of its own named one by one;
    // and a dev build's TideTweaks
    if (!opts->declarations_only) {
        const char **folders = arena_alloc(sizeof(char *) * (size_t)(count ? count : 1));
        int folder_count = 0;
        if (opts->game_folder) folders[folder_count++] = opts->game_folder;
        for (int i = 0; !opts->game_folder && i < count; i++) {
            if (inputs[i].package) continue; // A package's files are its own
            const char *path = inputs[i].path;
            const char *slash = NULL;
            for (const char *ch = path; *ch; ch++) {
                if (*ch == '/' || *ch == '\\') slash = ch;
            }
            char *dir = arena_alloc(slash ? (size_t)(slash - path) + 1 : 2);
            if (slash) {
                memcpy(dir, path, (size_t)(slash - path));
                dir[slash - path] = '\0';
            } else strcpy(dir, ".");
            bool seen = false;
            for (int k = 0; k < folder_count && !seen; k++) seen = strcmp(folders[k], dir) == 0;
            if (!seen) folders[folder_count++] = dir;
        }
        if (!tweaks_read(prog, folders, folder_count, opts->game_folder != NULL)) return false;
        if (opts->dev && (!tweaks_prelude(prog) || !inspect_prelude(prog))) return false;
    }
    if (opts->declarations_only) keep_declarations(prog);
    if (opts->embed && !add_embed_taps(prog)) return false;
    prog->game_folder = opts->game_folder;
    prog->calls_as_written = opts->no_call_merging;
    compile_progress("Checking Tide", NULL, 0, 0);
    if (!check(prog)) return false;
    // What build code makes (build_code.h): found, or made by running it and then found as the game compiles again
    if (!schedule && !opts->declarations_only && !opts->build_program && prog->build_roots.count) {
        static int again_depth;
        bool again = false;
        if (!build_code_run(prog, opts, &again)) return false;
        diag_phase("running build code");
        if (again) {
            if (again_depth) {
                diag_error(prog->build_roots.items[0]->at, "build code ran, and what it made isn't found: build the game again");
                return false;
            }
            again_depth++;
            const bool ok = compile_inputs_impl(inputs, count, opts, schedule);
            again_depth--;
            return ok;
        }
    }

    codegen_options named = *opts;
    if (!named.name) named.name = path_stem(inputs[0].path);
    if (schedule && opts->strip_names && (opts->shaders || opts->spirv_dir || opts->hlsl_dir)) {
        for (int i = 0; i < prog->shaders.count; i++) {
            shader *sh = prog->shaders.items[i];
            sh->wgsl = shader_minify_names(sh->wgsl, NULL);
        }
    }
    if (schedule && opts->spirv_dir) return write_spirv(prog, opts->spirv_dir, schedule);
    if (schedule && opts->hlsl_dir) return write_hlsl(prog, opts->hlsl_dir, schedule);
    if (schedule && opts->kernels_out) return write_kernels(prog, opts->kernels_out);
    if (opts->shader_out) {
        for (int i = 0; i < prog->shaders.count; i++) {
            opts->shader_out(opts->shader_user, prog->shaders.items[i]->name, prog->shaders.items[i]->wgsl);
        }
    }
    if (schedule && opts->shaders) {
        print_shaders(prog, schedule);
        return true;
    }
    if (schedule && opts->outline) {
        print_outline(prog, schedule);
        return true;
    }
    if (schedule) {
        print_schedule(prog, named.name, schedule);
        if (opts->profile && !print_schedule_measured(prog, opts->profile, schedule)) return false;
        print_loop_indexes(prog, schedule);
        print_parallel_loops(prog, schedule);
        return true;
    }
    const bool ok = codegen(prog, &named);
    diag_phase("generating C");
    if (ok && opts->outline_to) print_outline(prog, opts->outline_to);
    return ok;
}

bool compile_inputs(const compile_input *inputs, const int count, const codegen_options *opts, sb *schedule)
{
    asset_files_use_build_cache(schedule == NULL);
    if (!schedule && !opts->build_program) asset_cache_tidy(opts->game_folder);
    compile_progress_whole(0); // Parsing counts its files; checking starts the whole compilation's count
    const bool ok = compile_inputs_impl(inputs, count, opts, schedule);
    compile_progress_whole(0);
    asset_files_use_build_cache(false);
    return ok;
}
