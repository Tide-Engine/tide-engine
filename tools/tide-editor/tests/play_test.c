#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif

#include "play.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

static int failures;
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); failures++; } } while (0)

static char *self;

static bool has_arg(const int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], name)) return true;
    return false;
}

static const char *arg_value(const int argc, char **argv, const char *name)
{
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], name)) return argv[i + 1];
    return NULL;
}

static int child(const char *pid_file)
{
    char pid[40]; snprintf(pid, sizeof pid, "%u\n", sys_pid());
    if (!sys_write_text(pid_file, pid)) return 1;
    sys_sleep_ms(10000); // Safety bound even if the process-tree assertion fails.
    return 0;
}

static sys_process *start_child(const char *directory, const char *name)
{
    char *pid = path_join(directory, name);
    char *log = path_join(directory, "child.log");
    const char *argv[] = {self, "--child", pid, NULL};
    sys_process *p = sys_start_logged(argv, directory, log);
    free(pid); free(log);
    return p;
}

// This fixture is a real executable with a real grandchild, so cancellation
// exercises the OS job/group rather than a mocked process implementation.
static int builder(const int argc, char **argv)
{
    const char *output = arg_value(argc, argv, "-o");
    const char *source = arg_value(argc, argv, "--source-overlay");
    const char *draft = NULL;
    for (int i = 1; i + 2 < argc; i++) if (!strcmp(argv[i], "--source-overlay")) draft = argv[i + 2];
    const char *cache = arg_value(argc, argv, "--editor-cache");
    if (!output || !source || !draft || !cache || !arg_value(argc, argv, "--scene")) return 3;
    char *text = sys_read_file(draft, NULL);
    if (!text) return 4;
    char *directory = path_dir(output), *seen = path_join(directory, "seen.txt");
    const bool wrote = sys_write_text(seen, text);
    free(seen);
    if (!wrote) { free(text); free(directory); return 5; }
    if (strstr(text, "fail-build")) {
        fprintf(stderr, "fixture compiler rejected the authored draft\n");
        free(text); free(directory); return 7;
    }
    if (strstr(text, "slow-build")) {
        sys_process *p = start_child(directory, "builder-child.pid");
        if (p) { int code; while (!sys_wait(p, 50, &code)) {} }
        free(text); free(directory); return 8;
    }
    if (strstr(text, "leave-child")) {
        sys_process *p = start_child(directory, "builder-child.pid");
        char *pid = path_join(directory, "builder-child.pid");
        const int64_t until = sys_now_ms() + 3000;
        while (p && !sys_exists(pid) && sys_now_ms() < until) sys_sleep_ms(10);
        free(pid);
        // Deliberately outlive this fixture compiler; the editor must own it.
    }
    free(text);
    size_t size;
    char *bytes = sys_read_file(self, &size);
    const bool copied = bytes && sys_write_file(output, bytes, size);
    free(bytes); free(directory);
#ifndef _WIN32
    if (copied && chmod(output, 0700) != 0) return 6;
#endif
    return copied ? 0 : 6;
}

static int game(const int argc, char **argv)
{
    if (has_arg(argc, argv, "--spawn-child")) {
        char *directory = path_dir(self);
        sys_process *p = start_child(directory, "game-child.pid");
        free(directory);
        if (p) { int code; while (!sys_wait(p, 50, &code)) {} }
        return 8;
    }
    const char *capture = arg_value(argc, argv, "--capture");
    if (capture && !sys_write_text(capture, "fixture game finished\n")) return 9;
    sys_sleep_ms(100);
    fprintf(stderr, "fixture game output\n");
    return 0;
}

static char *fixture(const char *work, const char *name, const char *code, const char *prefab)
{
    char *directory = path_join(work, name);
    sys_remove_tree(directory); CHECK(sys_mkdirs(directory));
    char *file = path_join(directory, "tide.packages"); CHECK(sys_write_text(file, "")); free(file);
    file = path_join(directory, "game.tide"); CHECK(sys_write_text(file, code)); free(file);
    file = path_join(directory, "Level.tprefab"); CHECK(sys_write_text(file, prefab));
    free(directory);
    return file;
}

static bool wait_state(editor_play *p, const editor_play_state state, const int timeout)
{
    const int64_t until = sys_now_ms() + timeout;
    while (p->state != state && (p->state == EDITOR_PLAY_BUILDING || p->state == EDITOR_PLAY_RUNNING) && sys_now_ms() < until)
        editor_play_poll(p, 10);
    if (p->state != state) fprintf(stderr, "Play state %d, expected %d: %s\n", p->state, state, p->message);
    return p->state == state;
}

static uint32_t wait_pid(const char *directory, const char *name)
{
    char *path = path_join(directory, name);
    const int64_t until = sys_now_ms() + 3000;
    uint32_t pid = 0;
    do {
        char *text = sys_read_file(path, NULL);
        // Creating the file precedes writing it. Wait for the complete line,
        // rather than treating an observed empty/partial file as process 0.
        if (text && strchr(text, '\n')) pid = (uint32_t)strtoul(text, NULL, 10);
        free(text);
        if (!pid) sys_sleep_ms(10);
    } while (!pid && sys_now_ms() < until);
    free(path);
    return pid;
}

static bool dead(const uint32_t pid)
{
    const int64_t until = sys_now_ms() + 3000;
    while (sys_process_alive(pid) && sys_now_ms() < until) sys_sleep_ms(10);
    return !sys_process_alive(pid);
}

static int control_tests(const char *work)
{
    CHECK(sys_hidden());
    const char *code = "scene Main {}\ncomponent Health { int value = 1; }\n";
    const char *prefab = "// authored source stays here\nprefab Level { Health { value = 4 }; }\n";
    char *file = fixture(work, "control", code, prefab);
    editor_document d = {0}; CHECK(editor_open(&d, file, NULL)); CHECK(!d.errors);
    editor_play p = {0};
    const editor_play_options visible = {.cli = self};
    CHECK(!editor_play_start(&p, &d, &visible)); CHECK(p.state == EDITOR_PLAY_FAILED);
    CHECK(strstr(p.message, "TIDE_HIDDEN") != NULL); CHECK(!p.directory && !p.process);
    const char *arguments[] = {"--frames", "3", NULL};
    const editor_play_options options = {.cli = self, .arguments = arguments};
    d.conflict = true; CHECK(!editor_play_start(&p, &d, &options)); CHECK(strstr(p.message, "outside changes")); d.conflict = false;
    CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "9"));
    CHECK(editor_play_start(&p, &d, &options)); CHECK(p.state == EDITOR_PLAY_BUILDING);
    const uint64_t revision = p.revision;
    CHECK(!editor_play_start(&p, &d, &options)); CHECK(p.state == EDITOR_PLAY_BUILDING);
    CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "12"));
    CHECK(wait_state(&p, EDITOR_PLAY_RUNNING, 5000)); CHECK(p.revision == revision);
    char *seen_path = path_join(p.directory, "seen.txt"), *seen = sys_read_file(seen_path, NULL);
    CHECK(seen && strstr(seen, "value = 9") && !strstr(seen, "value = 12")); free(seen); free(seen_path);
    CHECK(wait_state(&p, EDITOR_PLAY_FINISHED, 5000)); CHECK(!p.process && p.exit_code == 0);
    char *said = sys_read_file(p.log, NULL); CHECK(said && strstr(said, "fixture game output")); free(said);
    char *directory = path_absolute(p.directory); editor_play_stop(&p); CHECK(!sys_exists(directory)); free(directory);
    CHECK(p.state == EDITOR_PLAY_STOPPED && !p.arguments);
    char *disk = sys_read_file(file, NULL); CHECK(disk && !strcmp(disk, prefab)); free(disk);
    CHECK(editor_dirty(&d));
    editor_play_close(&p); editor_close(&d); free(file);

    file = fixture(work, "finished-compiler", code, "// leave-child\nprefab Level { Health; }\n");
    CHECK(editor_open(&d, file, NULL)); CHECK(editor_play_start(&p, &d, &options));
    CHECK(wait_state(&p, EDITOR_PLAY_RUNNING, 5000));
    const uint32_t leftover = wait_pid(p.directory, "builder-child.pid"); CHECK(leftover != 0);
    CHECK(dead(leftover)); // A successful compiler exit also cleans descendants.
    CHECK(wait_state(&p, EDITOR_PLAY_FINISHED, 5000));
    editor_play_close(&p); editor_close(&d); free(file);

    file = fixture(work, "failed", code, "// fail-build\nprefab Level { Health; }\n");
    CHECK(editor_open(&d, file, NULL)); CHECK(editor_play_start(&p, &d, &options));
    CHECK(wait_state(&p, EDITOR_PLAY_FAILED, 5000)); CHECK(p.exit_code == 7 && !p.process);
    CHECK(strstr(p.message, "fixture compiler rejected") != NULL);
    editor_play_close(&p); editor_close(&d); free(file);

    file = fixture(work, "cancel-build", code, "// slow-build\nprefab Level { Health; }\n");
    CHECK(editor_open(&d, file, NULL)); CHECK(editor_play_start(&p, &d, &options));
    const uint32_t compiler_child = wait_pid(p.directory, "builder-child.pid"); CHECK(compiler_child != 0);
    directory = path_absolute(p.directory); editor_play_stop(&p);
    CHECK(p.state == EDITOR_PLAY_STOPPED && !p.process); CHECK(dead(compiler_child)); CHECK(!sys_exists(directory)); free(directory);
    editor_close(&d); free(file);

    file = fixture(work, "cancel-game", code, prefab);
    CHECK(editor_open(&d, file, NULL));
    const char *child_args[] = {"--frames", "600", "--spawn-child", NULL};
    const editor_play_options child_options = {.cli = self, .arguments = child_args};
    CHECK(editor_play_start(&p, &d, &child_options)); CHECK(wait_state(&p, EDITOR_PLAY_RUNNING, 5000));
    const uint32_t game_child = wait_pid(p.directory, "game-child.pid"); CHECK(game_child != 0);
    editor_play_stop(&p); CHECK(dead(game_child));
    editor_close(&d); free(file); editor_play_close(&p);
#ifndef _WIN32
    char *closed_log = path_join(work, "closed-output.log");
    char *outer_log = path_join(work, "closed-fixture.log");
    for (int both = 0; both < 2; both++) {
        const char *args[] = {self, both ? "--closed-both" : "--closed-stderr", closed_log, NULL};
        sys_process *closed = sys_start_tree_logged(args, work, outer_log);
        CHECK(closed != NULL);
        int code = -1;
        const int64_t until = sys_now_ms() + 3000;
        bool ended = false;
        while (closed && !(ended = sys_wait(closed, 10, &code)) && sys_now_ms() < until) {}
        if (closed && !ended) sys_kill(closed);
        CHECK(ended && code == 0);
        char *text = sys_read_file(closed_log, NULL);
        CHECK(text && strstr(text, "stdout marker") && strstr(text, "stderr marker")); free(text);
    }
    free(closed_log); free(outer_log);
#endif
    if (!failures) puts("ok: immutable Play overlay, hidden refusal, phase/errors, compiler/game descendant cleanup");
    return failures ? 1 : 0;
}

static int integration(const char *cli, const char *work)
{
    const char *code =
        "component Health { int value = 4; }\n"
        "event(Spawned) Born(mut Health health) { health.value += 10; Debug.Log(\"PLAY_SPAWNED\"); }\n"
        "system Advance(mut Health health) { health.value += 1; Debug.Log(\"PLAY_SYSTEM\"); }\n"
        "view Render(Health health, Devices devices) {\n"
        "    Debug.Assert(health.value >= 19, \"unsaved authored value or Spawned missing\");\n"
        "    if (devices.keyboard.a.down) Debug.Log(\"PLAY_DEVICE\");\n"
        "    Debug.Log(\"PLAY_UNMARKED_VIEW\");\n"
        "    Draw.Clear(Color.green); Draw.SetCamera(float2(10, 0), 10);\n"
        "    Draw.Circle(float2(10, 0), 2, Color.blue);\n"
        "}\n";
    const char *prefab = "// Keep original source bytes\nprefab Level { Health { value = 4 }; }\n";
    char *file = fixture(work, "real-game", code, prefab);
    editor_document d = {0}; CHECK(editor_open(&d, file, NULL));
    for (int i = 0; i < d.diagnostics.count; i++) fprintf(stderr, "%s\n", d.diagnostics.items[i]);
    CHECK(!d.errors);
    CHECK(editor_set_field(&d, 0, str_from("Health"), str_from("value"), "9"));
    char *script = path_join(work, "play.txt"), *image = path_join(work, "play.png"), *data = path_join(work, "data");
    CHECK(sys_write_text(script, "2 key a\n8 quit\n"));
    const char *args[] = {"--script", script, "--capture", image, "--size", "320", "200", "--data-dir", data, NULL};
    const editor_play_options options = {.cli = cli, .arguments = args};
    editor_play p = {0}; CHECK(editor_play_start(&p, &d, &options));
    CHECK(wait_state(&p, EDITOR_PLAY_FINISHED, 120000));
    char *log = p.log ? sys_read_file(p.log, NULL) : NULL;
    CHECK(log && strstr(log, "PLAY_SPAWNED") && strstr(log, "PLAY_SYSTEM") && strstr(log, "PLAY_UNMARKED_VIEW") && strstr(log, "PLAY_DEVICE"));
    if (log) fputs(log, stdout);
    CHECK(sys_exists(image));
    char *disk = sys_read_file(file, NULL); CHECK(disk && !strcmp(disk, prefab)); free(disk);
    CHECK(editor_dirty(&d));
    editor_play_stop(&p);
    CHECK(editor_play_start(&p, &d, &options)); CHECK(wait_state(&p, EDITOR_PLAY_FINISHED, 120000));
    char *again = p.log ? sys_read_file(p.log, NULL) : NULL;
    CHECK(log && again && !strcmp(log, again)); // Stop/restart starts fresh runtime state.
    free(log); free(again);
    editor_play_close(&p); editor_close(&d);
    free(file); free(script); free(image); free(data);
    return failures ? 1 : 0;
}

int main(const int argc, char **argv)
{
    self = path_absolute(argv[0]);
    int result;
    if (argc > 2 && !strcmp(argv[1], "--child")) result = child(argv[2]);
#ifndef _WIN32
    else if (argc == 3 && (!strcmp(argv[1], "--closed-both") || !strcmp(argv[1], "--closed-stderr"))) {
        if (!strcmp(argv[1], "--closed-both")) close(1);
        close(2);
        const char *args[] = {self, "--log-line", NULL};
        sys_process *p = sys_start_tree_logged(args, NULL, argv[2]);
        result = 1;
        if (p) while (!sys_wait(p, 10, &result)) {}
    } else if (argc == 2 && !strcmp(argv[1], "--log-line")) {
        fputs("stdout marker\n", stdout); fputs("stderr marker\n", stderr); result = 0;
    }
#endif
    else if (argc > 1 && !strcmp(argv[1], "build")) result = builder(argc, argv);
    else if (argc > 1 && !strcmp(argv[1], "--frames")) result = game(argc, argv);
    else if (argc == 4 && !strcmp(argv[1], "--integration")) {
        char *work = path_absolute(argv[3]); CHECK(sys_mkdirs(work)); result = integration(argv[2], work); free(work);
    } else if (argc == 2) {
        char *work = path_absolute(argv[1]); CHECK(sys_mkdirs(work)); result = control_tests(work); free(work);
    } else { fprintf(stderr, "pass a private test directory, or --integration <installed tide> <directory>\n"); result = 2; }
    free(self);
    return result;
}
