// Hot reloading, for `tide run` (see tide/host.h). tide rebuilds the game as a
// library whenever a .tide file changes: game-1, game-2 and so on, each renamed
// into place once it's whole. Between two frames, the host loads the newest
// and runs it from then on. A file of its own, since it includes the operating
// system's headers.
//
// On the web, each build is a program of its own instead. The page asks the
// running one for its state (tide_reload_save), then starts the new one in its
// place (platform/web/tide.js), which carries that state over.

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
#define _DEFAULT_SOURCE // dlopen under strict C
#endif

#include "tide/host.h"
#include "tide/migrate.h"

#include "reload_library.inc"

#if defined(__wasm__)

#include "tide_web.h"

_Noreturn void tide_host_run_library(const tide_run_desc *desc, const char *dir)
{
    (void)desc;
    (void)dir;
    fprintf(stderr, "tide: web builds reload with tide_host_run_web\n");
    exit(1);
}

// What a program leaves the next one: this, then its layout (packed), its
// match's world as the server has it and its local state, as bytes
// (tide_world_pack, tide_local_pack), and its GUI, each 8-aligned. What the
// host keeps of its own goes too: whether the stats' drawer is open.
#define SAVED_MAGIC 0x53525550u // "PURS"

typedef struct saved {
    uint32_t size; // Of it all, first, for the page
    uint32_t magic;
    uint64_t hash;    // Its game's: the same one has the same layout
    tide_player_set players; // In the match
    uint32_t layout, layout_size;
    uint32_t world, world_size; // 0 bytes outside a match
    uint32_t local, local_size;
    uint32_t gui, gui_size;
    uint32_t stats_open; // The stats' drawer is, as the player left it
    uint32_t pad;
} saved;

static uint32_t align8(const uint32_t n)
{
    return (n + 7u) & ~7u;
}

// For the page, before it starts the next build: where this program's state
// is, starting with its size, or 0 when there's none to carry over. It stays
// until the program goes.
__attribute__((export_name("tide_reload_save"))) uint32_t tide_reload_save(void)
{
    const tide_host_game *game = tide_run_game;
    if (!game || !game->layout || !tide_run_local) return 0;
    if (game->rebind_resources) game->rebind_resources(tide_run_local, true);
    uint32_t layout_size = 0;
    void *layout = tide_layout_pack(game->layout, &layout_size);
    if (!layout) return 0;
    const void *world = tide_session_server_world(tide_run_session);
    const tide_session_status status = tide_session_status_of(tide_run_session);
    const int32_t player = tide_player_index(status.client.player);
    saved h = {0};
    h.magic = SAVED_MAGIC;
    h.hash = game->game->hash;
    if (world && player >= 0) tide_players_add(&h.players, (uint32_t)player);
    h.layout = align8(sizeof h);
    h.layout_size = layout_size;
    h.world = align8(h.layout + layout_size);
    h.world_size = world ? game->game->pack_world(world, NULL, 0) : 0u;
    h.local = align8(h.world + h.world_size);
    h.local_size = game->local_pack(tide_run_local, NULL, 0);
    h.gui = align8(h.local + h.local_size);
    h.gui_size = sizeof tide_run_gui;
    h.stats_open = tide_run_drawer.open;
    h.size = h.gui + h.gui_size;
    uint8_t *block = malloc(h.size);
    if (!block) {
        free(layout);
        return 0;
    }
    memset(block, 0, h.size);
    memcpy(block, &h, sizeof h);
    memcpy(block + h.layout, layout, layout_size);
    if (world) game->game->pack_world(world, block + h.world, h.world_size);
    game->local_pack(tide_run_local, block + h.local, h.local_size);
    memcpy(block + h.gui, &tide_run_gui, h.gui_size);
    free(layout);
    return (uint32_t)(uintptr_t)block;
}

// Goes on from what the last program left, if anything: false to start the
// game as it would anyway, having said why when something was left.
static bool resume(void)
{
    const tide_host_game *game = tide_run_game;
    const uint32_t size = tide_web_resume_size();
    if (size < sizeof(saved) || !game->layout) return false;
    uint8_t *block = malloc(size);
    if (!block) return false;
    tide_web_resume_copy(block);
    saved h;
    memcpy(&h, block, sizeof h);
    const bool whole = h.magic == SAVED_MAGIC && h.size == size && h.layout <= size && h.layout_size <= size - h.layout
                    && h.world <= size && h.world_size <= size - h.world && h.local <= size
                    && h.local_size <= size - h.local && h.gui <= size && h.gui_size <= size - h.gui;
    const tide_layout *old = whole ? tide_layout_unpack(block + h.layout, h.layout_size) : NULL;
    if (!old) {
        printf("tide: reloaded, and started over: the last build's state didn't come through\n");
        free(block);
        return false;
    }

    void *local = calloc(1, game->local_size);
    void *world = h.world_size ? calloc(1, game->game->world_size) : NULL;
    tide_migration local_done = {0};
    tide_migration match_done = {0};
    bool ok = local && (world || !h.world_size);
    if (ok) {
        tide_run_bind_io(game, local);
        ok = carry_bytes(old, &old->local, block + h.local, h.local_size, game->layout, &game->layout->local,
                         game->local_unpack, local, &local_done);
    }
    if (ok && world) {
        ok = carry_bytes(old, &old->match, block + h.world, h.world_size, game->layout, &game->layout->match,
                         game->game->unpack_world, world, &match_done);
    }
    if (!ok) {
        const char *why = local_done.failed[0] ? local_done.failed : match_done.failed;
        printf("tide: reloaded, and started over: %s\n", why[0] ? why : "there wasn't enough memory");
        if (local) game->local_free(local);
        if (world) game->game->free_world(world);
        free(local);
        free(world);
        free(block);
        return false;
    }

    if (game->rebind_resources) game->rebind_resources(local, false);
    tide_run_local = local;
    tide_run_start = calloc(1, game->game->start_size);
    if (!tide_run_start) abort();
    tide_run_session = tide_run_new_session();
    if (world) tide_session_start_from(tide_run_session, world, h.players, tide_run_now);
    if (h.gui_size == sizeof tide_run_gui) { // Its draw list is the old program's memory: this one keeps its own
        const tide_draw_list list = tide_run_gui.list;
        memcpy(&tide_run_gui, block + h.gui, sizeof tide_run_gui);
        tide_run_gui.list = list;
    }
    tide_run_drawer.open = h.stats_open != 0;
    if (h.hash == game->game->hash) printf("tide: reloaded\n");
    else say_carried(old, game->layout, local_done.entities_dropped + match_done.entities_dropped,
                     local_done.tasks_dropped + match_done.tasks_dropped);
    if (world) game->game->free_world(world);
    free(world);
    free(block);
    return true;
}

// The page fetched another pack from tide (a named file changed): the game loads it.
__attribute__((export_name("tide_pack_changed"))) void tide_pack_changed(void)
{
    tide_run_load_assets();
}

_Noreturn void tide_host_run_web(const tide_run_desc *desc, const tide_host_game *game)
{
    tide_run_game = game;
    tide_run_open(desc);
    tide_run_load_assets();
    if (!resume()) tide_run_begin();
    tide_platform_run(tide_run_frame, NULL);
}

#else

static void restart_input(tide_devices *devices)
{
    tide_keyboard *k = &devices->keyboard;
    const bool ctrl = k->leftCtrl.held || k->rightCtrl.held;
    if (ctrl && !reload.r_held && (k->r.held || (k->r.down && !reload.r_down))) reload.restart_requested = true;
    reload.r_held = k->r.held;
    if (ctrl) k->r = (tide_button){0};
}

static int reload_frame(void *user, const float seconds)
{
    reload.since_check += seconds;
    if (reload.since_check >= 0.1f) {
        reload.since_check = 0.0f;
        check();
    }
    const int code = tide_run_frame(user, seconds);
    reload.r_down = tide_run_devices.keyboard.r.down;
    if (reload.restart_requested) {
        check(); // A new build, if one is ready, is the one to start fresh.
        restart();
        reload.restart_requested = false;
    }
    rejoin(seconds);
    tide_platform_next_frame(0.1 - reload.since_check); // New builds, whatever the tick rate
    return code;
}

_Noreturn void tide_host_run_library(const tide_run_desc *desc, const char *dir)
{
    reload.dir = dir;
    reload.running = 1;
    reload.loaded = 1;
    const tide_host_game *game = load(1, &reload.library, true);
    if (!game) exit(1);
    tide_run_game = game;
    tide_run_input = restart_input;
    tide_run_open(desc);
    check_pack(); // The pack tide wrote with the first build, where the game has assets
    tide_run_begin();
    tide_platform_run(reload_frame, NULL);
}

#endif
