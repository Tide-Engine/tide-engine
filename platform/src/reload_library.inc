// Loading the game's libraries and carrying its state over to a new build:
// tide run's host (reload.c) and another program's (embed.c), each with the
// standard host's state of its own (tide/host.h), which this works on.

static const char *plural(const uint32_t n)
{
    return n == 1 ? "" : "s";
}

// What carrying a game over to a new build did, after "tide: reloaded".
static void say_carried(const tide_layout *from, const tide_layout *to, const uint32_t dropped, const uint32_t tasks)
{
    const uint32_t reset = tide_layout_fields_reset(from, to);
    printf("tide: reloaded, and carried the game over to its new data layout");
    if (reset) printf("; %u field%s reset", (unsigned)reset, plural(reset));
    if (dropped) printf("; %u entit%s dropped", (unsigned)dropped, dropped == 1 ? "y" : "ies");
    if (tasks) printf("; %u waiting task%s dropped, as %s code changed", (unsigned)tasks, plural(tasks), tasks == 1 ? "its" : "their");
    printf("\n");
}

// A world, or local state, carried over to a new build's layout from its bytes
// (tide_world_pack, tide_local_pack): made in `to`, zeroed, by the new build's
// `unpack`. False, with `done->failed` saying why, if it can't be.
static bool carry_bytes(const tide_layout *old, const tide_layout_world *old_world, const void *bytes,
                        const uint32_t size, const tide_layout *now, const tide_layout_world *now_world,
                        bool (*unpack)(void *, const uint8_t *, uint32_t), void *to, tide_migration *done)
{
    void *carried = NULL;
    uint32_t carried_size = 0;
    bool ok = tide_migrate_world(old, old_world, bytes, size, now, now_world, &carried, &carried_size, done);
    if (ok && !unpack(to, carried, carried_size)) {
        snprintf(done->failed, sizeof done->failed, "the new build couldn't take what was carried over");
        ok = false;
    }
    free(carried);
    return ok;
}

#if !defined(__wasm__)


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define LIBRARY_SUFFIX ".dll"
#else
#include <dlfcn.h>
#include <sys/stat.h>
#ifdef __APPLE__
#define LIBRARY_SUFFIX ".dylib"
#else
#define LIBRARY_SUFFIX ".so"
#endif
#endif

typedef const tide_host_game *(*library_fn)(void);

#ifdef _WIN32

static bool file_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void *library_open(const char *path)
{
    char native[4096];
    snprintf(native, sizeof native, "%s", path);
    for (char *c = native; *c; c++) {
        if (*c == '/') *c = '\\'; // LoadLibrary wants backslashes
    }
    return (void *)LoadLibraryA(native);
}

static library_fn library_find(void *library, const char *name)
{
    const FARPROC found = GetProcAddress((HMODULE)library, name);
    library_fn fn = NULL;
    if (found) memcpy(&fn, &found, sizeof fn);
    return fn;
}

static void library_close(void *library)
{
    FreeLibrary((HMODULE)library);
}

static void library_error(char *out, const size_t size)
{
    snprintf(out, size, "error %lu", (unsigned long)GetLastError());
}

#else

static bool file_exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

static void *library_open(const char *path)
{
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static library_fn library_find(void *library, const char *name)
{
    void *found = dlsym(library, name);
    library_fn fn = NULL;
    if (found) memcpy(&fn, &found, sizeof fn);
    return fn;
}

static void library_close(void *library)
{
    dlclose(library);
}

static void library_error(char *out, const size_t size)
{
    const char *error = dlerror();
    snprintf(out, size, "%s", error ? error : "unknown error");
}

#endif

static struct {
    const char *dir;
    uint32_t running;  // The build running: game-<running>
    uint32_t loaded;   // The build successfully loaded (a newer one may have failed)
    uint32_t pack;     // The pack loaded: pack-<pack>, 0 for none
    void *library;     // ...and its library
    float since_check; // Seconds since `dir` was last looked at
    float dropped_for; // Seconds since the match ended without this machine leaving it
    bool r_held;
    bool r_down; // A tap already seen but still pending the game's next input sample
    bool restart_requested;
} reload;

static void library_path(const uint32_t build, char *out, const size_t size)
{
    snprintf(out, size, "%s/game-%u" LIBRARY_SUFFIX, reload.dir, (unsigned)build);
}

static void loaded_path(const uint32_t build, char *out, const size_t size)
{
    snprintf(out, size, "%s/loaded-%u" LIBRARY_SUFFIX, reload.dir, (unsigned)build);
}

static void pack_path(const uint32_t n, char *out, const size_t size)
{
    snprintf(out, size, "%s/pack-%u", reload.dir, (unsigned)n);
}

// Loads the newest pack tide wrote in the run's folder, if it's newer than
// the one loaded: tide writes one with each build, and again when a file an
// asset names changes, so a saved image is in the running game at once.
static void check_pack(void)
{
    char path[4096];
    uint32_t newest = reload.pack;
    for (;;) {
        pack_path(newest + 1u, path, sizeof path);
        if (!file_exists(path)) break;
        newest++;
    }
    if (newest == reload.pack) return;
    reload.pack = newest;
    pack_path(newest, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    const long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = length > 0 ? malloc((size_t)length) : NULL;
    const bool ok = bytes && fread(bytes, 1, (size_t)length, f) == (size_t)length;
    fclose(f);
    if (ok) tide_run_assets_from(bytes, (size_t)length);
    else free(bytes);
}

// The CLI keeps only two published builds. The active library's own copy
// stays available for a full reload even if newer builds fail to load.
static bool keep_library(const char *from, const char *to)
{
    FILE *source = fopen(from, "rb");
    if (!source) return false;
    FILE *target = fopen(to, "wb");
    bool ok = target != NULL;
    if (target) {
        uint8_t bytes[16384];
        size_t n;
        while ((n = fread(bytes, 1, sizeof bytes, source)) != 0) {
            if (fwrite(bytes, 1, n, target) != n) { ok = false; break; }
        }
        ok = ok && !ferror(source);
        if (fclose(target) != 0) ok = false;
    }
    fclose(source);
    if (!ok) remove(to);
    return ok;
}

// Loads a build: its game, or NULL after saying why. A new build first
// gets its own copy; a full reload reopens that copy.
static const tide_host_game *load(const uint32_t build, void **library, const bool fresh)
{
    char path[4096];
    loaded_path(build, path, sizeof path);
    if (fresh) {
        char published[4096];
        library_path(build, published, sizeof published);
        if (!keep_library(published, path)) {
            fprintf(stderr, "tide: can't keep %s for reloading\n", published);
            return NULL;
        }
    }
    void *opened = library_open(path);
    if (!opened) {
        char error[512];
        library_error(error, sizeof error);
        fprintf(stderr, "tide: can't load %s (%s)\n", path, error);
        if (fresh) remove(path);
        return NULL;
    }
    const library_fn game = library_find(opened, TIDE_HOST_LIBRARY);
    if (!game) {
        fprintf(stderr, "tide: %s isn't a game's library: it has no %s\n", path, TIDE_HOST_LIBRARY);
        library_close(opened);
        if (fresh) remove(path);
        return NULL;
    }
    *library = opened;
    return game();
}

// The match's worlds, carried over for tide_session_migrate. The first one is
// the server's, or the client's when another machine runs it: what's said of
// the carrying over is about that one.
typedef struct match_carry {
    const tide_host_game *from;
    const tide_host_game *to;
    tide_migration first;
    tide_migration failed;
    int worlds;
} match_carry;

// A world, or local state, of the old build, packed by `pack` and carried over
// to the new one's (see carry_bytes).
static bool carry_world(uint32_t (*pack)(const void *, uint8_t *, uint32_t), const void *from, const tide_layout *old,
                        const tide_layout_world *old_world, const tide_layout *now, const tide_layout_world *now_world,
                        bool (*unpack)(void *, const uint8_t *, uint32_t), void *to, tide_migration *done)
{
    *done = (tide_migration){0};
    const uint32_t size = pack(from, NULL, 0);
    uint8_t *bytes = malloc(size);
    if (!bytes) {
        snprintf(done->failed, sizeof done->failed, "there wasn't enough memory");
        return false;
    }
    pack(from, bytes, size);
    const bool ok = carry_bytes(old, old_world, bytes, size, now, now_world, unpack, to, done);
    free(bytes);
    return ok;
}

static bool carry_match(void *user, const void *from, void *to)
{
    match_carry *m = user;
    tide_migration done;
    const bool ok = carry_world(m->from->game->pack_world, from, m->from->layout, &m->from->layout->match, m->to->layout,
                                &m->to->layout->match, m->to->game->unpack_world, to, &done);
    if (m->worlds++ == 0) m->first = done;
    if (!ok) m->failed = done;
    return ok;
}

// The match and the local state, carried over to `next`'s data layout by
// name. False, having said why and changed nothing, if they can't be.
static bool carry_over(const tide_host_game *next)
{
    const tide_host_game *old = tide_run_game;
    if (!old->layout || !next->layout) {
        printf("tide: reloaded, and started over: a build doesn't describe its data layout\n");
        return false;
    }
    if (tide_game_players(old->game) != tide_game_players(next->game)) {
        printf("tide: reloaded, and started over: maxPlayers changed, and a match keeps an input for each player it takes\n");
        return false;
    }
    void *local = calloc(1, next->local_size);
    void *start = calloc(1, next->game->start_size);
    tide_migration local_done = {0};
    match_carry match = {old, next, {0}, {0}, 0};
    bool ok = local && start;
    if (ok) {
        tide_run_bind_io(next, local);
        ok = carry_world(old->local_pack, tide_run_local, old->layout, &old->layout->local, next->layout,
                         &next->layout->local, next->local_unpack, local, &local_done);
    }
    if (ok) ok = tide_session_migrate(tide_run_session, next->game, carry_match, &match);
    if (!ok) {
        const char *why = local_done.failed[0] ? local_done.failed : match.failed.failed;
        printf("tide: reloaded, and started over: %s\n", why[0] ? why : "there wasn't enough memory");
        if (local) next->local_free(local);
        free(local);
        free(start);
        return false;
    }
    old->local_free(tide_run_local);
    free(tide_run_local);
    free(tide_run_start);
    tide_run_local = local;
    tide_run_start = start;
    say_carried(old->layout, next->layout, local_done.entities_dropped + match.first.entities_dropped,
                local_done.tasks_dropped + match.first.tasks_dropped);
    return true;
}

// Runs `next` from now on. With the same data layout, it takes over the match
// and the local state where they are; with another, they're carried over to
// it, and when they can't be, the game starts over.
static void swap(const tide_host_game *next, void *library)
{
    const tide_host_game *old = tide_run_game;
    // Release private caches with the code that made their layout, before
    // the new table can reinterpret an equally sized local structure.
    if (old->correct_views) old->correct_views(tide_run_local, &(tide_view_correction){.reset = true});
    // The new build's assets, from the pack the host has, before anything asks
    // it for a default: a field's can name one
    if (next->assets && tide_run_pack) next->assets(tide_run_pack, tide_run_pack_size);
    bool retained = true;
    // A match on the game's own network (tide/network.h) is on transports the
    // old build's C made, whose code goes with it: nothing of that match can
    // be carried into the new build
    const bool own_network = tide_run_network.on;
    if (own_network) {
        printf("tide: the match is on the game's own network, which the new build replaces: starting over\n");
        retained = false;
        tide_run_end();
        tide_run_game = next;
        tide_run_begin();
    } else if (next->game->hash == old->game->hash && next->local_size == old->local_size) {
        tide_session_set_game(tide_run_session, next->game);
        tide_run_game = next;
        printf("tide: reloaded\n");
    } else if (carry_over(next)) {
        tide_run_game = next;
    } else {
        retained = false;
        tide_run_end();
        tide_run_game = next;
        tide_run_begin();
    }
    if (retained) {
        tide_run_bind_io(next, tide_run_local);
        // Correction caches describe the old build's tagged fields and are
        // presentation state, so they start empty under the new table.
        tide_run_refresh_view_corrector();
        if (next->correct_views) next->correct_views(tide_run_local, &(tide_view_correction){.reset = true});
    }
    if (retained && next->rebind_resources) next->rebind_resources(tide_run_local, true);
    fflush(stdout);
    tide_platform_audio_forget(); // The samples the old build's voices play are its own, and go with it
    if (old->voice) {
        const tide_voice_provider *voice = old->voice();
        if (voice && voice->stop) voice->stop(voice->self, tide_run_session);
    }
    old->unload();
    library_close(reload.library);
    char path[4096];
    loaded_path(reload.loaded, path, sizeof path);
    remove(path);
    reload.library = library;
    tide_draw_forget(&tide_run_draw); // The pixels the old build's C named were its own
}

// A full reload also resets C and C++ globals: nothing of the old library
// can survive its unloading. The window and the host's filesystem stay open.
static void restart(void)
{
    const tide_host_game *old = tide_run_game;
    tide_run_end();
    tide_platform_audio_forget();
    old->unload();
    library_close(reload.library);
    reload.library = NULL;
    tide_run_game = load(reload.loaded, &reload.library, false);
    if (!tide_run_game && !tide_run_embedded) exit(1);
    if (!tide_run_game) return; // Another program's game stops, saying why: the program goes on
    if (tide_run_game->assets && tide_run_pack) tide_run_game->assets(tide_run_pack, tide_run_pack_size);
    tide_clips_prepare(tide_platform_jobs());
    tide_devices_consume(&tide_run_devices);
    reload.r_down = false;
    reload.dropped_for = 0.0f;
    tide_run_begin();
    printf("tide: started over\n");
    fflush(stdout);
}

// Swaps in the newest build, if there's a newer one, and starts over if tide
// was asked to.
static void check(void)
{
    char path[4096];
    uint32_t newest = reload.running;
    for (;;) {
        library_path(newest + 1u, path, sizeof path);
        if (!file_exists(path)) break;
        newest++;
    }
    check_pack(); // Before a new build, which starts with the pack as it is
    if (newest != reload.running) {
        reload.running = newest; // A build that doesn't load isn't tried again
        void *library = NULL;
        const tide_host_game *next = load(newest, &library, true);
        if (next) {
            swap(next, library);
            reload.loaded = newest;
        }
    }

    snprintf(path, sizeof path, "%s/restart", reload.dir);
    if (file_exists(path)) {
        remove(path);
        reload.restart_requested = true;
    }
}

// A game started with --join or --connect joins again a second after its
// match ends without it leaving: when the server starts over, has another
// build for a moment, or went away and came back.
static void rejoin(const float seconds)
{
    tide_session_request request;
    if (!tide_run_dropped || !tide_run_arguments(&request)
        || (request.kind != TIDE_REQUEST_JOIN && request.kind != TIDE_REQUEST_CONNECT)) {
        reload.dropped_for = 0.0f;
        return;
    }
    reload.dropped_for += seconds;
    if (reload.dropped_for < 1.0f) return;
    reload.dropped_for = 0.0f;
    tide_run_dropped = false;
    tide_run_request(&request, NULL);
}

#endif
