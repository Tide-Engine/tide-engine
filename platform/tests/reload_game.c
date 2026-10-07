// A game's table written in C so a full reload can be checked without the
// transpiler: local state, a match, and a C static all have different owners.
#include "tide/host.h"

typedef struct world { uint32_t ticks; } world;
typedef struct local { uint32_t frames; } local;
static uint32_t starts;
#ifdef _WIN32
#define NUMBERED_LIBRARY "game-1.dll"
#elif defined(__APPLE__)
#define NUMBERED_LIBRARY "game-1.dylib"
#else
#define NUMBERED_LIBRARY "game-1.so"
#endif

static void start(void *w, float dt, const void *scene, uint32_t seed)
{
    (void)dt;
    (void)scene;
    (void)seed;
    memset(w, 0, sizeof(world));
    printf("reload-test: match started\n");
}

static void tick(void *w, const tide_jobs *jobs)
{
    (void)jobs;
    ((world *)w)->ticks++;
}

static void copy(void *to, const void *from) { memcpy(to, from, sizeof(world)); }
static uint64_t hash(const void *w) { return ((const world *)w)->ticks; }
static void player(void *w, tide_player_id id) { (void)w; (void)id; }

static uint8_t *pack(const void *w, const void *base, const uint8_t *need, uint32_t need_size, uint32_t *size)
{
    (void)base;
    (void)need;
    (void)need_size;
    uint8_t *bytes = malloc(sizeof(world));
    if (!bytes) abort();
    memcpy(bytes, w, sizeof(world));
    *size = sizeof(world);
    return bytes;
}

static bool unpack(void *w, const void *base, const uint8_t *bytes, uint32_t size)
{
    (void)base;
    if (size != sizeof(world)) return false;
    memcpy(w, bytes, size);
    return true;
}

static void local_init(void *l)
{
    ((local *)l)->frames = 0;
    printf("reload-test: local started static=%u\n", (unsigned)++starts);
}

static void local_free(void *l)
{
    printf("reload-test: local freed frames=%u\n", (unsigned)((local *)l)->frames);
}

static void frame(const void *w, const void *previous, float alpha, const void *verified,
                  void *l, tide_draw_list *draw, tide_gui *gui)
{
    (void)previous;
    (void)alpha;
    (void)verified;
    (void)draw;
    printf("reload-test: frame=%u tick=%u r=%u\n", (unsigned)++((local *)l)->frames,
           w ? (unsigned)((const world *)w)->ticks : 0u, (unsigned)gui->devices.keyboard.r.pressed);
    // The watcher retires numbered builds. The host must keep its own loaded
    // copy so a restart remains possible after the original file disappears.
    if (((local *)l)->frames == 5 && getenv("TIDE_RELOAD_REMOVE_NUMBERED") && remove(NUMBERED_LIBRARY) == 0)
        printf("reload-test: numbered library removed\n");
    fflush(stdout);
}

static void frame_time(void *l, float seconds) { (void)l; (void)seconds; }
static bool request(void *l, tide_session_request *r, void *s) { (void)l; (void)r; (void)s; return false; }
static void session(void *l, uint32_t state, tide_player_id id, uint32_t ping, bool server, bool open, const char *room)
{
    (void)l; (void)state; (void)id; (void)ping; (void)server; (void)open; (void)room;
}
static void connected(void *l) { (void)l; }
static void disconnected(void *l, uint32_t reason, const char *message) { (void)l; (void)reason; (void)message; }
static int32_t current_tick(const void *w) { return (int32_t)((const world *)w)->ticks; }
static void unload(void) { printf("reload-test: unloaded static=%u\n", (unsigned)starts); }

static const tide_game game = {
    .hash = 4242,
    .world_size = sizeof(world),
    .start_size = 1,
    .start = start,
    .tick = tick,
    .copy_world = copy,
    .hash_world = hash,
    .pack_delta = pack,
    .unpack_delta = unpack,
    .player_joined = player,
    .player_left = player,
};

TIDE_HOST_EXPORT const tide_host_game *tide_host_library(void)
{
    static const tide_host_game host = {
        .game = &game,
        .local_size = sizeof(local),
        .local_init = local_init,
        .local_free = local_free,
        .frame = frame,
        .frame_time = frame_time,
        .take_request = request,
        .set_session = session,
        .connected = connected,
        .disconnected = disconnected,
        .tick = current_tick,
        .unload = unload,
    };
    return &host;
}
