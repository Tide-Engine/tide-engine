// A game's table written in C so a full reload can be checked without the
// transpiler: local state, a match, and a C static all have different owners.
#include "tide/host.h"

typedef struct world { uint32_t ticks; } world;
static uint32_t starts;
static bool packed; // The host handed this build the run's pack
#ifndef RELOAD_GAME_HASH
#define RELOAD_GAME_HASH 4242 // Another for a build of another layout
#endif
#ifdef RELOAD_GAME_READS
// A GPU list of the build's own capacity, set and read back through the host's renderer: the library's engine copy
// asks, and the host's finishes (tide_gpu_reads_share). The next build's list is bigger, and is made after the last
// one's state was freed, as after a full reload or a swap that doesn't adopt it: likely at its address.
#if RELOAD_GAME_HASH == 4242
#define READ_CAPACITY 4u
#else
#define READ_CAPACITY 64u
#endif
typedef struct local { uint32_t frames; tide_gpu_list list; uint32_t ticket; bool said; } local;
#else
typedef struct local { uint32_t frames; } local;
#endif
#ifdef _WIN32
#define NUMBERED_LIBRARY "game-1.dll"
#define NEXT_LIBRARY "game-2.dll"
#elif defined(__APPLE__)
#define NUMBERED_LIBRARY "game-1.dylib"
#define NEXT_LIBRARY "game-2.dylib"
#else
#define NUMBERED_LIBRARY "game-1.so"
#define NEXT_LIBRARY "game-2.so"
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

static uint8_t *pack(const void *w, const void *base, const uint8_t *need, uint32_t need_size, bool assets, uint32_t *size)
{
    (void)base;
    (void)need;
    (void)need_size;
    (void)assets;
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

// A build's defaults can name its assets, so it has the pack before its local
// state is made: a build swapped in while the game runs too.
static bool assets(const uint8_t *bytes, size_t size)
{
    (void)bytes;
    packed = size > 0;
    return true;
}

static void local_init(void *l)
{
    ((local *)l)->frames = 0;
    printf("reload-test: local started static=%u packed=%u\n", (unsigned)++starts, (unsigned)packed);
}

// The next build, as tide publishes one beside the build that runs: whole
// under another name, then renamed into place.
static void publish(const char *from)
{
    FILE *in = fopen(from, "rb");
    FILE *out = in ? fopen("game-2.part", "wb") : NULL;
    char bytes[65536];
    size_t count;
    while (out && (count = fread(bytes, 1, sizeof bytes, in)) > 0) fwrite(bytes, 1, count, out);
    if (in) fclose(in);
    if (out && fclose(out) == 0 && rename("game-2.part", NEXT_LIBRARY) == 0) printf("reload-test: next build published\n");
}

static void local_free(void *l)
{
    printf("reload-test: local freed frames=%u\n", (unsigned)((local *)l)->frames);
}

static void frame(const void *w, const void *previous, float alpha, const void *verified,
                  const void *verified_previous, float verified_alpha, void *l, tide_draw_list *draw, tide_gui *gui)
{
    (void)previous;
    (void)alpha;
    (void)verified;
    (void)verified_previous;
    (void)verified_alpha;
    (void)draw;
#ifdef RELOAD_GAME_READS
    local *mine = l;
    if (mine->frames == 0) {
        mine->list = tide_gpu_list_make((int32_t)READ_CAPACITY, 1u);
        uint32_t words[READ_CAPACITY];
        for (uint32_t i = 0; i < READ_CAPACITY; i++) words[i] = RELOAD_GAME_HASH * 1000u + i;
        tide_draw_gpu_list_set(draw, mine->list, words, READ_CAPACITY);
    }
    if (mine->frames == 1) mine->ticket = tide_gpu_list_read_begin(mine->list, false);
    tide_gpu_read_result got;
    if (mine->ticket && !mine->said && tide_gpu_list_read_poll(mine->ticket, &got)) {
        const uint32_t first = RELOAD_GAME_HASH * 1000u;
        const bool right = got.ok && got.count == (int32_t)READ_CAPACITY && got.stride == 1u && got.words[0] == first
                        && got.words[READ_CAPACITY - 1u] == first + READ_CAPACITY - 1u;
        printf("reload-test: read build=%u ok=%d count=%d right=%d\n", (unsigned)RELOAD_GAME_HASH, (int)got.ok,
               (int)got.count, (int)right);
        tide_gpu_list_read_forget(mine->ticket);
        mine->said = true;
    }
#endif
    printf("reload-test: frame=%u tick=%u r=%u\n", (unsigned)++((local *)l)->frames,
           w ? (unsigned)((const world *)w)->ticks : 0u, (unsigned)gui->devices.keyboard.r.pressed);
    // The watcher retires numbered builds. The host must keep its own loaded
    // copy so a restart remains possible after the original file disappears.
    if (((local *)l)->frames == 5 && getenv("TIDE_RELOAD_REMOVE_NUMBERED") && remove(NUMBERED_LIBRARY) == 0)
        printf("reload-test: numbered library removed\n");
    if (((local *)l)->frames == 5 && RELOAD_GAME_HASH == 4242 && getenv("TIDE_RELOAD_PUBLISH")) publish(getenv("TIDE_RELOAD_PUBLISH"));
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
static void unload(void)
{
#ifdef RELOAD_GAME_READS
    tide_gpu_lists_free();
#endif
    printf("reload-test: unloaded static=%u\n", (unsigned)starts);
}

static const tide_game game = {
    .hash = RELOAD_GAME_HASH,
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
        .assets = assets,
        .unload = unload,
#ifdef RELOAD_GAME_READS
        .gpu_reads_share = tide_gpu_reads_share,
#endif
    };
    return &host;
}
