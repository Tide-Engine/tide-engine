#include "play.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define PLAY_EXE "play.exe"
#define PLAY_CLI "tide.exe"
#else
#define PLAY_EXE "play"
#define PLAY_CLI "tide"
#endif

static char *copy_text(const char *text)
{
    const size_t size = strlen(text) + 1;
    char *out = malloc(size);
    if (!out) abort();
    memcpy(out, text, size);
    return out;
}

static bool active(const editor_play *p)
{
    return p->state == EDITOR_PLAY_BUILDING || p->state == EDITOR_PLAY_RUNNING;
}

static void free_work(editor_play *p)
{
    if (p->directory) sys_remove_tree(p->directory);
    free(p->directory); free(p->executable); free(p->log);
    free(p->project); free(p->source); free(p->scene);
    for (int i = 0; i < p->argument_count; i++) free(p->arguments[i]);
    free(p->arguments);
    p->directory = p->executable = p->log = NULL;
    p->project = p->source = p->scene = NULL;
    p->arguments = NULL; p->argument_count = 0;
}

void editor_play_stop(editor_play *p)
{
    if (p->process) sys_kill(p->process);
    p->process = NULL;
    free_work(p);
    p->state = EDITOR_PLAY_STOPPED;
    p->exit_code = 0;
    snprintf(p->message, sizeof p->message, "Stopped; runtime state discarded.");
}

void editor_play_close(editor_play *p)
{
    editor_play_stop(p);
    *p = (editor_play){0};
}

static bool refuse(editor_play *p, const char *message)
{
    p->state = EDITOR_PLAY_FAILED;
    p->exit_code = 2;
    snprintf(p->message, sizeof p->message, "%s", message);
    return false;
}

static bool bounded(const char *const *arguments)
{
    if (!arguments) return false;
    for (int i = 0; arguments[i]; i++) {
        if (strcmp(arguments[i], "--capture") == 0 || strcmp(arguments[i], "--script") == 0) {
            if (arguments[i + 1] && arguments[i + 1][0] && arguments[i + 1][0] != '-') return true;
        } else if (strcmp(arguments[i], "--frames") == 0 && arguments[i + 1]) {
            char *end = NULL;
            const long frames = strtol(arguments[i + 1], &end, 10);
            if (end != arguments[i + 1] && !*end && frames > 0) return true;
        }
    }
    return false;
}

static bool make_work(editor_play *p)
{
    static unsigned long long serial;
    char *base = path_join(p->project, ".tide/editor-play");
    // PID reuse and old failed runs must never cause a later Stop to remove
    // somebody else's artifacts. Only a new, absent child directory is owned.
    for (int attempt = 0; attempt < 1000; attempt++) {
        char name[80]; snprintf(name, sizeof name, "%u-%llu", sys_pid(), ++serial);
        char *directory = path_join(base, name);
        if (sys_exists(directory)) { free(directory); continue; }
        if (!sys_mkdirs(directory)) { free(directory); break; }
        char *work = path_join(p->project, ".tide"), *ignore = path_join(work, ".gitignore");
        if (!sys_exists(ignore)) sys_write_text(ignore, "*\n");
        sys_hide(work); free(work); free(ignore);
        p->directory = directory;
        p->executable = path_join(directory, PLAY_EXE);
        p->log = path_join(directory, "build.log");
        free(base);
        return true;
    }
    free(base);
    return false;
}

bool editor_play_start(editor_play *p, const editor_document *d, const editor_play_options *options)
{
    if (active(p)) {
        snprintf(p->message, sizeof p->message, "Stop the current play session before starting another.");
        return false;
    }
    if (!d || !d->text || !d->path || !d->project || d->errors || d->syntax_errors)
        return refuse(p, "Play needs a valid authored scene; fix the document's diagnostics first.");
    if (d->conflict || d->recovery)
        return refuse(p, "Resolve the document's outside changes or recovery before Play.");
    if (d->active < 0 || d->active >= d->prefabs.count)
        return refuse(p, "Select an authored scene or prefab before Play.");
    const char *const *arguments = options ? options->arguments : NULL;
    if (sys_hidden() && !bounded(arguments))
        return refuse(p, "TIDE_HIDDEN requires Play arguments --frames <n>, --capture <file.png> or --script <file>.");

    free_work(p);
    p->exit_code = 0;
    p->revision = d->revision;
    p->project = copy_text(d->project);
    p->source = copy_text(d->path);
    const str qualified = d->prefabs.items[d->active]->qualified;
    p->scene = malloc((size_t)qualified.len + 1);
    if (!p->scene) abort();
    memcpy(p->scene, qualified.ptr, (size_t)qualified.len);
    p->scene[qualified.len] = '\0';
    if (arguments) while (arguments[p->argument_count]) p->argument_count++;
    p->arguments = calloc((size_t)p->argument_count + 2, sizeof *p->arguments);
    if (!p->arguments) abort();
    for (int i = 0; i < p->argument_count; i++) p->arguments[i] = copy_text(arguments[i]);
    if (!make_work(p)) {
        free_work(p);
        return refuse(p, "Cannot create the private Play build directory.");
    }
    char *draft = path_join(p->directory, "draft.txt");
    char *cache = path_join(p->directory, "cache");
    char *cli;
    if (options && options->cli) cli = path_absolute(options->cli);
    else {
        char *bin = sys_exe_dir(); cli = path_join(bin, PLAY_CLI); free(bin);
    }
    const char *argv[] = {cli, "build", p->project, "--scene", p->scene,
        "--source-overlay", p->source, draft, "--editor-cache", cache, "-o", p->executable, NULL};
    const bool wrote = sys_write_text(draft, d->text);
    if (wrote) p->process = sys_start_tree_logged(argv, p->project, p->log);
    free(draft); free(cache); free(cli);
    if (!p->process) {
        free_work(p);
        return refuse(p, wrote ? "Cannot start the Play compiler; check the Tide installation." : "Cannot write the applied Play document.");
    }
    p->state = EDITOR_PLAY_BUILDING;
    snprintf(p->message, sizeof p->message, "Building %s for Play...", p->scene);
    return true;
}

static void failed(editor_play *p, const char *what, const int code)
{
    p->state = EDITOR_PLAY_FAILED;
    p->exit_code = code;
    char *said = sys_read_file(p->log, NULL);
    snprintf(p->message, sizeof p->message, "%s (exit %d).\n%s", what, code, said ? said : "No process output was written.");
    free(said);
}

void editor_play_poll(editor_play *p, int wait_ms)
{
    if (!p->process) return;
    if (wait_ms < 0) wait_ms = 0;
    if (wait_ms > 50) wait_ms = 50;
    int code;
    if (!sys_wait(p->process, wait_ms, &code)) return;
    p->process = NULL;
    if (code) { failed(p, p->state == EDITOR_PLAY_BUILDING ? "Play build failed" : "Game stopped with an error", code); return; }
    if (p->state == EDITOR_PLAY_RUNNING) {
        p->state = EDITOR_PLAY_FINISHED;
        p->exit_code = 0;
        snprintf(p->message, sizeof p->message, "Play finished; runtime state discarded.");
        return;
    }
    const char **argv = calloc((size_t)p->argument_count + 2, sizeof *argv);
    if (!argv) abort();
    argv[0] = p->executable;
    for (int i = 0; i < p->argument_count; i++) argv[i + 1] = p->arguments[i];
    free(p->log); p->log = path_join(p->directory, "play.log");
    p->process = sys_start_tree_logged(argv, p->project, p->log);
    free(argv);
    if (!p->process) { failed(p, "Cannot launch the built game", -1); return; }
    p->state = EDITOR_PLAY_RUNNING;
    snprintf(p->message, sizeof p->message, "Playing %s. Stop discards runtime state.", p->scene);
}
