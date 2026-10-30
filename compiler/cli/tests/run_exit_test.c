// Exercise the command's actual dispatch. The fixture runs no game and opens
// no window; its run entrypoints return a distinctive game exit code, and keep
// what the game's command line would have been. Replacing the update checker
// lets ordering be checked without clocks or the network.
#define main tide_command_main
#define sys_hidden fixture_hidden
#include "../main.c"
#undef sys_hidden
#undef main

static char calls[16];
int tide_hosting_command(const char *root, int argc, char **argv) { (void)root; (void)argc; (void)argv; abort(); }
int tide_open(int argc, char **argv) { (void)argc; (void)argv; return 0; }
char *tide_build_editor(const char *root, const build_options *opts) { (void)root; (void)opts; return NULL; }
int tide_unity(const char *root, const build_options *opts) { (void)root; (void)opts; return 0; }
static size_t call_count;
static const char *passed[128]; // What the last run was given for its game
static int passed_count;
static const char *passed_optimization;
static bool passed_release, passed_distribution, passed_debug;
static const char *passed_scene, *passed_source, *passed_draft, *passed_work;

static void called(const char call)
{
    if (call_count + 1 >= sizeof calls) abort();
    calls[call_count++] = call;
    calls[call_count] = '\0';
}

static void pass(const char *const *args)
{
    passed_count = 0;
    for (int i = 0; args[i]; i++) {
        if (passed_count >= (int)(sizeof passed / sizeof passed[0])) abort();
        passed[passed_count++] = args[i];
    }
}

bool fixture_hidden(void)
{
    // The watched native branch requires a windowed run. Its fixture
    // entrypoint below returns immediately, without starting anything.
    return false;
}

void tide_check_for_update(const char *root)
{
    (void)root;
    called('U');
}

int tide_run_native(const char *root, const build_options *opts, const char *const *args)
{
    (void)root;
    passed_optimization = opts->optimization;
    passed_release = opts->release;
    passed_distribution = opts->distribution;
    passed_debug = false;
    for (int i = 0; i < opts->define_count; i++)
        if (strcmp(opts->defines[i], "DEBUG") == 0) passed_debug = true;
    passed_scene = opts->scene;
    passed_source = opts->editor_source;
    passed_draft = opts->editor_draft;
    passed_work = opts->work;
    pass(args);
    called(opts->watch ? 'X' : 'N');
    return 13;
}

int tide_run_reloading(const char *root, const build_options *opts, const char *const *args)
{
    (void)root;
    pass(args);
    called(opts->watch ? 'W' : 'X');
    return 13;
}

int tide_run_web(const char *root, const build_options *opts, const bool open_page, const char *const *args)
{
    (void)root;
    (void)open_page;
    pass(args);
    called(opts->watch ? 'w' : 'b');
    return 13;
}

int tide_run_android(const char *root, const build_options *opts, const char *const *args)
{
    (void)root;
    pass(args);
    called(opts->watch ? 'a' : 'A');
    return 13;
}

// Startup and unrelated commands have no work in this fixture. Abort if
// dispatch reaches an unrelated command, so an unexpected path cannot pass.
void packages_set_builtin_folder(const char *folder) { (void)folder; }
void packages_set_symbols(const char *const *names, const int count) { (void)names; (void)count; }
char *packages_unity_game(const char *folder) { (void)folder; abort(); }
void tide_cleanup(const char *root) { (void)root; }
const char *tide_channel(const char *root) { (void)root; abort(); }
bool tide_installation_whole(const char *root, const char *version) { (void)root; (void)version; abort(); }
char *find_clang(void) { abort(); }
char *find_web_clang(void) { abort(); }
char *find_wasm_ld(const char *clang) { (void)clang; abort(); }
int tide_editors(const char *root, const bool only_updates) { (void)root; (void)only_updates; abort(); }
int tide_ide(const char *root, int count, const char *const *args) { (void)root; (void)count; (void)args; abort(); }
int tide_add(const char *folder, const char *what) { (void)folder; (void)what; abort(); }
int tide_eject(const char *folder, const char *name) { (void)folder; (void)name; abort(); }
int tide_pin(const char *folder, const char *version) { (void)folder; (void)version; abort(); }
int tide_update(const char *folder, const char *const *names, const int count)
{
    (void)folder;
    (void)names;
    (void)count;
    abort();
}
int tide_fetch(const char *folder) { (void)folder; abort(); }
int tide_publish(const char *folder) { (void)folder; abort(); }
int tide_upgrade(const char *root, const char *channel, const char *version)
{
    (void)root;
    (void)channel;
    (void)version;
    abort();
}
bool tide_schedule(const build_options *opts, const bool shaders) { (void)opts; (void)shaders; abort(); }
// Usage data is telemetry_test.mjs's: these runs record nothing.
void tide_telemetry_record(const char *root, const int argc, const char *const *argv, const int code, const int64_t ms,
                           const char *version)
{
    (void)root; (void)argc; (void)argv; (void)code; (void)ms; (void)version;
}
int tide_telemetry_send(const char *root, const char *version) { (void)root; (void)version; abort(); }
int tide_telemetry(const char *root, const int count, const char *const *args, const char *version)
{
    (void)root; (void)count; (void)args; (void)version;
    abort();
}
char *tide_build(const char *root, const build_options *opts) { (void)root; (void)opts; abort(); }

int main(void)
{
    struct {
        char *argv[6];
        int argc;
        const char *calls;
    } runs[] = {
        {{"tide", "run"}, 2, "UW"},
        {{"tide", "run", "--size", "640", "480"}, 5, "UW"},
        {{"tide", "run", "--data-dir", "test saves"}, 4, "UW"},
        {{"tide", "run", "--frames", "1"}, 4, "UN"},
        {{"tide", "run", "--capture", "frame.png"}, 4, "UN"},
        {{"tide", "run", "--script", "play.txt"}, 4, "UN"},
        {{"tide", "run", "--interactive", "play.txt"}, 4, "UN"},
        {{"tide", "run", "--capture-audio", "run.wav"}, 4, "UN"},
        {{"tide", "run", "--record", "run.gif"}, 4, "UN"},
        {{"tide", "run", "--microphone", "voice.wav"}, 4, "UN"},
        {{"tide", "run", "--web", "--no-open"}, 4, "Uw"},
        {{"tide", "run", "--android"}, 3, "Ua"},
        {{"tide", "run", "--server", "--anonymous"}, 4, "UN"},
        {{"tide", "run", "--server", "--frames", "2"}, 5, "UN"},
    };
    for (size_t i = 0; i < sizeof runs / sizeof runs[0]; i++) {
        call_count = 0;
        calls[0] = '\0';
        const int code = tide_command_main(runs[i].argc, runs[i].argv);
        if (code != 13 || strcmp(calls, runs[i].calls) != 0) {
            fprintf(stderr, "run %zu: expected update check before the run, no work after it, and exit code 13; "
                            "got calls '%s' and code %d\n", i, calls, code);
            return 1;
        }
    }
    puts("ok: native, web and Android runs end with the game's exit code, with no update check after them");

    const char *modes[] = {"--release", "--distribution"};
    for (int i = 0; i < 2; i++) {
        char *mode[] = {"tide", "run", "--frames", "1", (char *)modes[i]};
        call_count = 0;
        if (tide_command_main(5, mode) != 13 || !passed_release
            || passed_distribution != (i == 1) || passed_debug || passed_count != 2) {
            fputs("optimized mode must reach the build without DEBUG or a host argument\n", stderr);
            return 1;
        }
    }

    const char *levels[] = {"-O0", "-O1", "-O2", "-O3"};
    for (int i = 0; i < 4; i++) {
        char *level[] = {"tide", "run", "--frames", "1", (char *)levels[i]};
        call_count = 0;
        if (tide_command_main(5, level) != 13 || !passed_optimization
            || strcmp(passed_optimization, levels[i]) || passed_release || !passed_debug || passed_count != 2) {
            fputs("optimization override must preserve debug mode and stay out of host arguments\n", stderr);
            return 1;
        }
    }
    const char *invalid_levels[] = {"-O", "-O4", "-Ofast", "-Os", "-O33"};
    for (size_t i = 0; i < sizeof invalid_levels / sizeof invalid_levels[0]; i++) {
        char *level[] = {"tide", "run", "--frames", "1", (char *)invalid_levels[i]};
        call_count = 0;
        if (tide_command_main(5, level) != 2 || call_count) return 1;
    }
    char *duplicate[] = {"tide", "run", "--frames", "1", "-O2", "-O3"};
    call_count = 0;
    if (tide_command_main(6, duplicate) != 2 || call_count) return 1;

    // Every option of the game's own command line, each once, with the game's
    // folder after them: there's room for them all, and the game is given each
    // as it was typed, but for the files, which are where the command says
    // from here, as the game runs in its own folder.
    char *all[] = {"tide", "run", "--host", "7777", "--join", "K7QF2M", "--connect", "localhost", "--capture", "frame.png",
                   "--capture-audio", "run.wav", "--record", "run.gif", "--script", "play.txt", "--interactive", "live play.txt", "--microphone", "voice.wav", "--frames", "1",
                   "--scale", "2", "--players", "2", "--seed", "7", "--data-dir", "test saves", "--size", "640", "480",
                   "--latency", "80", "--jitter", "10", "--loss", "5", "--latency", "2:120", "game"};
    const int all_count = (int)(sizeof all / sizeof all[0]);
    call_count = 0;
    calls[0] = '\0';
    passed_count = 0;
    const int code = tide_command_main(all_count, all);
    if (code != 13 || strcmp(calls, "UN") != 0 || passed_count != all_count - 3) {
        fprintf(stderr, "every option once: expected a run with no window given %d arguments and exit code 13; got calls '%s', "
                        "%d arguments and code %d\n", all_count - 3, calls, passed_count, code);
        return 1;
    }
    for (int i = 0; i < passed_count; i++) {
        const char *typed = all[i + 2];
        const char *before = i > 0 ? all[i + 1] : "";
        const bool file = strcmp(before, "--capture") == 0 || strcmp(before, "--capture-audio") == 0 || strcmp(before, "--record") == 0 ||
                          strcmp(before, "--script") == 0 || strcmp(before, "--interactive") == 0 || strcmp(before, "--microphone") == 0 || strcmp(before, "--data-dir") == 0;
        const char *expected = file ? path_absolute(typed) : typed;
        if (strcmp(passed[i], expected) != 0) {
            fprintf(stderr, "every option once: argument %d should be '%s', not '%s'\n", i, expected, passed[i]);
            return 1;
        }
    }
    puts("ok: a run passes on every option of the game's command line, its files as paths from here");

    char *startup[] = {"tide", "run", "game", "--scene", "Levels.First", "--source-overlay", "new.tprefab", "draft.txt",
                       "--editor-cache", "private-cache", "--frames", "1"};
    call_count = 0;
    calls[0] = '\0';
    if (tide_command_main((int)(sizeof startup / sizeof startup[0]), startup) != 13
        || strcmp(calls, "UN") || passed_count != 2 || strcmp(passed[0], "--frames") || strcmp(passed[1], "1")
        || !passed_scene || strcmp(passed_scene, "Levels.First")
        || !passed_source || strcmp(passed_source, path_absolute("new.tprefab"))
        || !passed_draft || strcmp(passed_draft, path_absolute("draft.txt"))
        || !passed_work || strcmp(passed_work, "private-cache")) {
        fputs("startup options must reach the compiler without entering the host command line\n", stderr);
        return 1;
    }
    const char *invalid_options[] = {"--scene", "--source-overlay", "--editor-cache"};
    for (size_t i = 0; i < sizeof invalid_options / sizeof invalid_options[0]; i++) {
        char *invalid[] = {"tide", "run", (char *)invalid_options[i]};
        call_count = 0;
        calls[0] = '\0';
        if (tide_command_main(3, invalid) != 2 || calls[0]) {
            fputs("an incomplete startup option must fail before running or checking updates\n", stderr);
            return 1;
        }
    }
    puts("ok: startup selection, original-path source overlay and isolated cache stay compiler options");

    // More of them than that is said, never written past the room there is
    char *many[2 + 3 * 48] = {"tide", "run"};
    for (int i = 0; i < 48; i++) {
        many[2 + 3 * i] = "--size";
        many[3 + 3 * i] = "640";
        many[4 + 3 * i] = "480";
    }
    call_count = 0;
    calls[0] = '\0';
    const int refused = tide_command_main(2 + 3 * 48, many);
    if (refused != 2 || calls[0]) {
        fprintf(stderr, "an option given 48 times: expected exit code 2 and no run; got calls '%s' and code %d\n", calls, refused);
        return 1;
    }
    puts("ok: an option given over and over is refused");
    return 0;
}

int tide_use_pinned(const char *folder, int argc, char **argv, const char *version)
{ (void)folder; (void)argc; (void)argv; (void)version; return -1; }
