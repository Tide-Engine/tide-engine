// The standard host's loading screen (tide/host.h, tide_run_load_shown): in a
// window, the game's pack is read and loaded on a thread of the pool's while
// the frames go on and show how far it got, and the game begins once it's in.
// A game's table written in C, whose assets() waits until the test lets it go,
// runs in the real host loop in a hidden window.
#include "tide/host.h"
#ifdef __ANDROID__
#include "files.h" // tide_platform_cache_folder
#endif

typedef struct world { uint32_t ticks; } world;
typedef struct local { uint32_t frames; } local;

#define PACK_BYTES (3u * 1048576u + 17u)
static const char *pack_file = "loading-test.pack"; // In the working folder; an app's (Android's) is its cache folder

static TIDE_THREAD_LOCAL bool on_main;
static uint32_t release;          // The test lets assets() return (atomic)
static uint32_t in_assets;        // assets() has started (atomic)
static bool assets_off_main;      // ...and ran on a thread other than the frames'
static size_t assets_size;
static uint32_t local_starts, game_frames;

static void start(void *w, float dt, const void *scene, uint32_t seed)
{
    (void)dt;
    (void)scene;
    (void)seed;
    memset(w, 0, sizeof(world));
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

// What takes a big game's pack seconds: here, as long as the test says
static bool assets(const uint8_t *bytes, const size_t size)
{
    assets_off_main = !on_main;
    assets_size = size;
    bool whole = true;
    for (size_t i = 0; i < size; i++) whole = whole && bytes[i] == (uint8_t)(i * 7u);
    __atomic_store_n(&in_assets, 1u, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&release, __ATOMIC_ACQUIRE)) {
    }
    return whole;
}

static void local_init(void *l)
{
    ((local *)l)->frames = 0;
    local_starts++;
}

static void local_free(void *l) { (void)l; }

static void frame(const void *w, const void *previous, float alpha, const void *verified,
                  const void *verified_previous, float verified_alpha, void *l, tide_draw_list *draw, tide_gui *gui)
{
    (void)w;
    (void)previous;
    (void)alpha;
    (void)verified;
    (void)verified_previous;
    (void)verified_alpha;
    (void)draw;
    (void)gui;
    ((local *)l)->frames++;
    game_frames++;
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

static const tide_game game = {
    .hash = 4747,
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
};

#define CHECK(condition, ...)                                                                                          \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "loading-test: frame %u: ", (unsigned)frames);                                             \
            fprintf(stderr, __VA_ARGS__);                                                                              \
            fprintf(stderr, "\n");                                                                                     \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static uint32_t frames, blocked_frames, begun_at;
static bool shown, released; // The loading screen's bar and text were seen; assets() was let go

// The loading screen as its last frame drew it, read back at `count` points of the window's logical pixels: the
// window's size, 320 by 180 on desktop and the screen's on a phone, which the screen is laid out on
static void pixels_at(const tide_float2 *points, const int count, uint32_t *rgba)
{
    const tide_float2 screen = tide_platform_screen_size();
    tide_platform_read_pixels_as(&tide_run_loading.list, (int)screen.x, (int)screen.y, 1.0f, points, count, rgba);
}

static int test_frame(void *user, const float seconds)
{
    frames++;
    if (!begun_at && __atomic_load_n(&in_assets, __ATOMIC_ACQUIRE) && !released) {
        // The pack is read whole and the game is loading it: the frames still go on, showing it
        CHECK(tide_run_loading.on, "the loading screen is up while the game loads its pack");
        CHECK(!local_starts && !game_frames, "the game hasn't begun");
        CHECK(__atomic_load_n(&tide_run_loading.progress[0], __ATOMIC_RELAXED) == PACK_BYTES &&
                  __atomic_load_n(&tide_run_loading.progress[1], __ATOMIC_RELAXED) == PACK_BYTES,
              "it says how much was read, of how much");
        if (blocked_frames >= 5 && shown) {
            __atomic_store_n(&release, 1u, __ATOMIC_RELEASE);
            released = true;
        }
        if (blocked_frames++ > 0) { // What the last frame drew: the web shell's background and bar, and its text
            // The bar 220 wide in the middle, and the text 16 under it: its left end and right, its middle, the
            // background's corner, and the 60 by 14 pixels in the middle of the text's line
            const tide_float2 screen = tide_platform_screen_size();
            const float middle = floorf(screen.x * 0.5f), bar_left = floorf((screen.x - 220.0f) * 0.5f);
            const float bar_y = floorf((screen.y - (4.0f + 16.0f + 14.0f)) * 0.5f) + 2.0f, text_y = bar_y + 18.0f;
            enum { TEXT_POINTS = 60 * 14, POINTS = 4 + TEXT_POINTS };
            static tide_float2 points[POINTS];
            static uint32_t got[POINTS];
            points[0] = tide_f2(2.0f, 2.0f);
            points[1] = tide_f2(middle, bar_y), points[2] = tide_f2(bar_left + 2.0f, bar_y), points[3] = tide_f2(bar_left + 218.0f, bar_y);
            for (int i = 0; i < TEXT_POINTS; i++) points[4 + i] = tide_f2(middle - 30.0f + (float)(i % 60), text_y + (float)(i / 60));
            pixels_at(points, POINTS, got);
            CHECK(got[0] == 0x0E0E14FFu, "the background is the shell's, not %08X", (unsigned)got[0]);
            bool text = false;
            for (int i = 0; i < TEXT_POINTS && !text; i++) text = got[4 + i] != 0x0E0E14FFu;
            const uint32_t track = got[1], left = got[2], right = got[3];
            const uint32_t background = 0x0E0E14FFu, empty = 0x24242EFFu;
            if (!shown && !(text && track != background && left != background && right != background)) {
                // The screen's pipelines are made on the pool's threads, as a game's host has them made
                // (tide_platform_background_pipelines): till they are, its frames show the background alone, for a
                // frame or a few, and assets() waits on
                CHECK(blocked_frames < 600, "the loading screen's bar and text never showed: %08X %08X %08X, text %s",
                      (unsigned)left, (unsigned)track, (unsigned)right, text ? "drawn" : "not drawn");
            } else {
                CHECK(text, "'Preparing' is drawn under the bar");
                CHECK(track != background && left != background && right != background,
                      "the bar is across the middle: %08X %08X %08X", (unsigned)left, (unsigned)track, (unsigned)right);
                CHECK(left == empty || right == empty, "a third of the bar moves while nothing says how far it got: %08X %08X",
                      (unsigned)left, (unsigned)right);
                shown = true;
            }
        }
    }
    const uint32_t before = game_frames;
    const int code = tide_run_frame(user, seconds);
    if (!begun_at && local_starts) {
        begun_at = frames;
        CHECK(before == 0 && game_frames == 1, "the frame the pack is in in is the game's first");
        CHECK(!tide_run_loading.on, "the loading screen is down");
        CHECK(blocked_frames >= 6, "the frames went on while the game loaded its pack (%u)", (unsigned)blocked_frames);
        CHECK(assets_off_main, "the pack loads on another thread than the frames'");
        CHECK(assets_size == PACK_BYTES && tide_run_pack && tide_run_pack_size == PACK_BYTES, "the host keeps the pack");
        CHECK(local_starts == 1 && tide_run_session, "the game began once");
    }
    CHECK(frames < 100000, "the pack never loaded");
    if (begun_at && frames >= begun_at + 3) {
        CHECK(game_frames == 4, "the game's frames go on (%u)", (unsigned)game_frames);
        printf("ok: the pack loads on the pool while the window's frames show it, and the game begins once it's in\n");
        fflush(stdout);
        remove(pack_file);
        return 0;
    }
    return code;
}

int main(int argc, char **argv)
{
    on_main = true;
#ifdef __ANDROID__
    // An app's working folder is the system's root, where it can write nothing
    static char in_cache[1024];
    tide_platform_name_game("dev.tide.test.loading");
    const char *folder = tide_platform_cache_folder();
    if (!folder) return 2;
    snprintf(in_cache, sizeof in_cache, "%s/loading-test.pack", folder);
    pack_file = in_cache;
#endif
    FILE *f = fopen(pack_file, "wb");
    if (!f) return 2;
    for (uint32_t i = 0; i < PACK_BYTES; i++) fputc((int)(uint8_t)(i * 7u), f);
    if (fclose(f) != 0) return 2;

    tide_run_game = &host;
    tide_run_configure(&(tide_run_desc){.title = "loading", .argc = argc, .argv = argv});
    tide_platform_open(&(tide_window_desc){.title = "loading", .width = 320, .height = 180, .hidden = true});
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || !pool->background) {
        printf("ok: no background threads here, so the pack loads in place\n");
        remove(pack_file);
        return 0;
    }
    if (!tide_run_load_shown(pack_file)) {
        fprintf(stderr, "loading-test: a window's pack should load on the pool\n");
        return 1;
    }
    tide_run_begin_loaded();
    if (local_starts) {
        fprintf(stderr, "loading-test: the game began before its pack was in\n");
        return 1;
    }
    tide_platform_run(test_frame, NULL);
}
