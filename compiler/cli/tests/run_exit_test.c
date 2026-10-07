// Exercise the command's actual dispatch. The fixture runs no game and opens
// no window; its run entrypoints return a distinctive game exit code. Replacing
// the update checker lets ordering be checked without clocks or the network.
#define main tide_command_main
#define sys_hidden fixture_hidden
#include "../main.c"
#undef sys_hidden
#undef main

static char calls[16];
static size_t call_count;

static void called(const char call)
{
    if (call_count + 1 >= sizeof calls) abort();
    calls[call_count++] = call;
    calls[call_count] = '\0';
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
    (void)args;
    called(opts->watch ? 'X' : 'N');
    return 13;
}

int tide_run_reloading(const char *root, const build_options *opts, const char *const *args)
{
    (void)root;
    (void)args;
    called(opts->watch ? 'W' : 'X');
    return 13;
}

int tide_run_web(const char *root, const build_options *opts, const bool open_page, const char *const *args)
{
    (void)root;
    (void)open_page;
    (void)args;
    called(opts->watch ? 'w' : 'b');
    return 13;
}

int tide_run_android(const char *root, const build_options *opts, const char *const *args)
{
    (void)root;
    (void)args;
    called(opts->watch ? 'a' : 'A');
    return 13;
}

// Startup and unrelated commands have no work in this fixture. Abort if
// dispatch reaches an unrelated command, so an unexpected path cannot pass.
void packages_set_builtin_folder(const char *folder) { (void)folder; }
void tide_cleanup(const char *root) { (void)root; }
const char *tide_channel(const char *root) { (void)root; abort(); }
bool tide_installation_whole(const char *root, const char *version) { (void)root; (void)version; abort(); }
char *find_clang(void) { abort(); }
char *find_web_clang(void) { abort(); }
char *find_wasm_ld(const char *clang) { (void)clang; abort(); }
int tide_editors(const char *root, const bool only_updates) { (void)root; (void)only_updates; abort(); }
int tide_add(const char *folder, const char *what) { (void)folder; (void)what; abort(); }
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
        {{"tide", "run", "--frames", "1"}, 4, "UN"},
        {{"tide", "run", "--capture", "frame.png"}, 4, "UN"},
        {{"tide", "run", "--script", "play.txt"}, 4, "UN"},
        {{"tide", "run", "--web", "--no-open"}, 4, "Uw"},
        {{"tide", "run", "--android"}, 3, "Ua"},
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
    return 0;
}
