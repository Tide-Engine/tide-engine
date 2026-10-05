#pragma once

// The standard host's loop: a game in a window, in a session, driven through
// a table of the game's functions (tide_host_game).
//
// tide/run.h makes the table for a game compiled into the program, which is
// how games run. Under `tide run` the game is a library instead, and
// tide_host_run_library swaps in each new build of it that tide makes: hot
// reloading. The table is what lets one loop do both.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/layout.h"
#include "tide/platform.h"
#include "tide/profiler.h"
#include "tide/session.h"
#include "tide/time.h"

typedef struct tide_run_desc {
    const char *title;      // The window's title, over the game's title setting; NULL for that
    const char *game_name;  // The title when neither says: the game's folder or target; default "Tide"
    int width;              // Starting window size in pixels; default 960 x 540
    int height;
    int tick_rate;          // Ticks per second, over the game's tickRate setting; 0 for that, or else 60
    // Show the stats in a corner, as the game's `stats` setting does: the frame rate, what a frame's work takes,
    // ping, bandwidth, tick, entity count, threads, memory, what draws, its draw calls and the slowest systems
    bool stats;
    // The debug views are on, as with the game's debugViews setting or
    // --debug-views on the command line: F7 shows the profiler (tide/profiler.h)
    bool debug_views;
    // The command line: --host, --join and --connect, --debug-views, --size,
    // and --capture, --frames, --script and --scale for a run with no window
    // (see tide/run.h)
    int argc;
    char **argv;
} tide_run_desc;

// A game as a host runs it: what sessions run (tide_game), and this machine's
// side of it: its local state, its views and its input. `local`, `world` and
// `start` are the game's tide_local, tide_world and tide_start.
typedef struct tide_host_game {
    const tide_game *game;
    uint32_t local_size; // sizeof(tide_local): local state starts as that many zeros
    bool main_is_local;  // TIDE_MAIN_IS_LOCAL: the program starts outside any match
    void (*local_init)(void *local);
    // The host owns the filesystem provider across game library reloads.
    void (*set_io)(void *local, const tide_io *io);
    // Lets go of what local state has, before its memory goes, and the local
    // state as bytes and back, as tide_game's worlds (tide_local_pack).
    void (*local_free)(void *local);
    uint32_t (*local_pack)(const void *local, uint8_t *out, uint32_t capacity);
    bool (*local_unpack)(void *local, const uint8_t *data, uint32_t size);
    // tide_input_sample, into `input`; NULL for a game without an input
    void (*sample_input)(const tide_devices *devices, const void *local, void *input);
    // tide_frame: `verified` is the last world the server confirmed, which the
    // local fields of the match's entities are kept by, with `world`
    void (*frame)(const void *world, const void *previous, float alpha, const void *verified, void *local,
                  tide_draw_list *draw, tide_gui *gui);
    // How long this frame is, before `frame`: local tasks' Wait.Seconds counts it down
    void (*frame_time)(void *local, float seconds);
    bool (*take_request)(void *local, tide_session_request *request, void *start);
    // The oldest packed command, without removing it; 0 when none. Remove it
    // only once the session has copied it into its pending queue.
    uint32_t (*command)(const void *local, uint8_t *out, uint32_t capacity);
    void (*command_sent)(void *local);
    // `room`: the code of the room the match is in, "" if none
    void (*set_session)(void *local, uint32_t state, tide_player_id player, uint32_t ping, bool server, bool open,
                        const char *room);
    void (*connected)(void *local);
    void (*disconnected)(void *local, uint32_t reason, const char *message); // `message`: a kick's, or ""
    int32_t (*tick)(const void *world); // Time.tick
    uint32_t (*entity_count)(const void *world);
    // Frees what the game's code keeps for itself (scratch and target bookkeeping), before
    // its library is unloaded.
    void (*unload)(void);
    // Its data layout, when tidec described it (tide run's builds), to carry
    // the game over to a build whose layout changed; NULL otherwise.
    const tide_layout *layout;
    // Graphics references in retained local state: native targets transfer to
    // the new build; a new address space recreates them. Optional for C hosts.
    void (*rebind_resources)(void *local, bool same_memory);
    // Optional: accumulated visual errors for tagged match fields. The context
    // belongs to this synchronous call; it never changes simulation state.
    void (*correct_views)(void *local, const tide_view_correction *correction);
} tide_host_game;

// A game's library exports this function, which returns its table.
#define TIDE_HOST_LIBRARY "tide_host_library"
#ifdef _WIN32
#define TIDE_HOST_EXPORT __declspec(dllexport)
#else
#define TIDE_HOST_EXPORT __attribute__((visibility("default")))
#endif

// Runs the game from the libraries tide builds in `dir`: game-1 first (a .dll,
// .so or .dylib), then each newer one that appears there, swapped in between
// two frames. A build with the same data layout as the one running (the same
// tide_game hash) takes over the match and the local state where they are;
// with another, they're carried over to it by name (tide/migrate.h), or when
// that can't be done, the game starts over. A file named `restart` in `dir`
// starts it over too. Native builds only (platform/src/reload.c).
_Noreturn void tide_host_run_library(const tide_run_desc *desc, const char *dir);

// The same on the web (tide run --web), where each new build is a program of
// its own, which the page starts in the running one's place: `game` goes on
// from what the last one left it, carried over by name, or starts as
// tide_host_run would. Web builds only (platform/src/reload.c).
_Noreturn void tide_host_run_web(const tide_run_desc *desc, const tide_host_game *game);

// ---------------------------------------------------------------------------
// The loop, for tide/run.h and platform/src/reload.c

static const tide_host_game *tide_run_game;
static void *tide_run_local;  // The game's tide_local
static void *tide_run_start;  // A tide_start, for what local code asks
static tide_devices tide_run_devices;
static tide_draw_list tide_run_draw;
static tide_gui tide_run_gui;
// The profiler, and in it how long each system and view took, measured while
// it's shown or the stats overlay is
static tide_profiler tide_run_profiler;
static bool tide_run_profiling; // The profiler is shown
static bool tide_run_f7;        // F7 was held at the last frame
static bool tide_run_debug_told;
// What ticks run on where the platform has no pool: this thread, which still
// has a profile to fill
static tide_jobs tide_run_alone = {.threads = 1};
static tide_run_desc tide_run_settings;
// A run with no window, when the command line asks for one with --capture,
// --frames or --script: what it asked (tide_platform_open_headless)
static bool tide_run_headless;
static tide_headless_desc tide_run_headless_desc;
static tide_session *tide_run_session;
static double tide_run_now; // Seconds since the program started
// The server went away or turned this machine away: not because it left,
// couldn't start, was kicked, or the match ran out of scenes
static bool tide_run_dropped;
// Host migration: when this machine went to its match's room again, or 0
static double tide_run_migrating_since;
// The stats overlay (tide_run_stats): frames, their work and bytes over the
// last second, counted from when it began, with the session's totals then.
static double tide_run_stats_since;
static uint32_t tide_run_stats_frames;
static uint64_t tide_run_stats_work; // Nanoseconds the frames' work took: not the overlay, nor the wait for the display
static uint64_t tide_run_stats_sent, tide_run_stats_received;
static double tide_run_fps, tide_run_up, tide_run_down; // A second, over the last one
static double tide_run_work_ms;                         // A frame's work, over the last second
static uint64_t tide_run_memory;                        // tide_platform_memory, as of then

// Whether the stats overlay shows: the host asked for it (--stats, STATS), or
// the game's `stats` setting does.
static inline bool tide_run_stats(void)
{
    return tide_run_settings.stats || tide_run_game->game->stats;
}

// This machine's input for one tick.
static inline void tide_run_sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    if (!tide_run_game->sample_input) return;
    tide_devices sampled = tide_run_devices;
    tide_gui_hide(&tide_run_gui, &sampled); // A click on a button isn't the game's
    tide_run_game->sample_input(&sampled, tide_run_local, input);
    tide_devices_consume(&tide_run_devices);
}

static inline void tide_run_correct_views(void *user, const tide_view_correction *correction)
{
    (void)user;
    if (tide_run_game->correct_views) tide_run_game->correct_views(tide_run_local, correction);
}

static inline void tide_run_refresh_view_corrector(void)
{
    tide_session_set_view_corrector(tide_run_session,
                                   tide_run_game->correct_views ? tide_run_correct_views : NULL);
}

// Local commands waiting for transport capacity belong to the match whose
// views sent them. Explicit match changes discard them; migration keeps them.
static inline void tide_run_discard_commands(void)
{
    const tide_host_game *game = tide_run_game;
    if (!game->command || !game->command_sent) return;
    uint8_t bytes[TIDE_COMMAND_BYTES];
    while (game->command(tide_run_local, bytes, sizeof bytes)) game->command_sent(tide_run_local);
}

// Starts, joins or leaves a match, as local code or the command line asked,
// and copies what it asked to.
// `start` is a tide_start, or NULL for Main.
static inline void tide_run_request(const tide_session_request *request, const void *start)
{
    tide_session *s = tide_run_session;
    tide_transport network;
    tide_address server;
    const bool port_ok = request->port <= 65535u;
    const uint16_t port = port_ok ? (uint16_t)request->port : 0u;
    switch (request->kind) {
    case TIDE_REQUEST_START:
        tide_run_discard_commands();
        tide_session_start(s, start, tide_run_now);
        break;
    case TIDE_REQUEST_OPEN:
        // Players join on a UDP port (not on the web), and in a room. A match
        // opened before opens again where it was.
        if (!tide_session_status_of(s).server || tide_session_open(s, (tide_transport){0})) break;
        if (port_ok && tide_platform_host_open(port, &network)) {
            tide_session_open(s, network);
            break;
        }
        fprintf(stderr, "tide: can't take players on port %u\n", (unsigned)request->port);
        break;
    case TIDE_REQUEST_CLOSE:
        tide_session_close(s);
        break;
    case TIDE_REQUEST_KICK:
        tide_session_kick(s, request->player, request->text);
        break;
    case TIDE_REQUEST_KICK_ALL:
        tide_session_kick_all(s, request->text);
        break;
    case TIDE_REQUEST_JOIN:
        tide_run_discard_commands();
        if (!tide_platform_room_join(request->text, &network, &server)) {
            fprintf(stderr, "tide: can't join room '%s'\n", request->text);
            tide_session_leave(s);
            tide_session_fail(s, TIDE_DISCONNECT_FAILED);
            break;
        }
        tide_session_join(s, network, server, tide_run_now);
        break;
    case TIDE_REQUEST_CONNECT:
        tide_run_discard_commands();
        if (!port || !tide_platform_resolve(request->text, port, &server) || !tide_platform_udp_open(0, &network)) {
            fprintf(stderr, "tide: can't reach '%s'\n", request->text);
            tide_session_leave(s);
            tide_session_fail(s, TIDE_DISCONNECT_FAILED);
            break;
        }
        tide_session_join(s, network, server, tide_run_now);
        break;
    case TIDE_REQUEST_LEAVE:
        tide_run_discard_commands();
        tide_session_leave(s);
        break;
    case TIDE_REQUEST_END:
        if (tide_session_status_of(s).server) tide_run_discard_commands();
        tide_session_end(s);
        break;
    case TIDE_REQUEST_COPY:
        tide_platform_copy(request->text);
        break;
    default:
        break;
    }
}

// --host [port], --join code or --connect address, from the command line.
// --host is an Open, for Main's match.
static inline bool tide_run_arguments(tide_session_request *request)
{
    for (int i = 1; i < tide_run_settings.argc; i++) {
        const char *arg = tide_run_settings.argv[i];
        const char *next = i + 1 < tide_run_settings.argc ? tide_run_settings.argv[i + 1] : NULL;
        if (strcmp(arg, "--host") == 0) {
            *request = (tide_session_request){.kind = TIDE_REQUEST_OPEN, .port = TIDE_DEFAULT_PORT};
            if (next && next[0] >= '0' && next[0] <= '9') request->port = (uint32_t)strtoul(next, NULL, 10);
            return true;
        }
        const bool join = strcmp(arg, "--join") == 0;
        if ((join || strcmp(arg, "--connect") == 0) && next) {
            *request = (tide_session_request){.kind = join ? TIDE_REQUEST_JOIN : TIDE_REQUEST_CONNECT,
                                              .port = TIDE_DEFAULT_PORT};
            snprintf(request->text, sizeof request->text, "%s", next);
            return true;
        }
    }
    return false;
}

// The command line's argument after `*i`, which `flag` takes, moving `*i` to
// it. Without one, the program ends, saying what the flag takes.
static inline const char *tide_run_value(int *i, const char *flag, const char *takes)
{
    if (*i + 1 < tide_run_settings.argc) return tide_run_settings.argv[++*i];
    fprintf(stderr, "tide: %s takes %s\n", flag, takes);
    exit(2);
}

// ...as a number from `least` to `most`, or the program ends the same way.
static inline double tide_run_number(int *i, const char *flag, const double least, const double most, const char *takes)
{
    const char *text = tide_run_value(i, flag, takes);
    char *end = NULL;
    const double value = strtod(text, &end);
    if (end != text && !*end && value >= least && value <= most) return value;
    fprintf(stderr, "tide: %s takes %s, not '%s'\n", flag, takes, text);
    exit(2);
}

// How tide_run_game runs: what `desc` says, with the defaults for what it and
// the game's settings leave out, and what its command line says: --debug-views,
// the window's size, and a run with no window.
static inline void tide_run_configure(const tide_run_desc *desc)
{
    tide_run_settings = *desc;
    if (!tide_run_settings.title) tide_run_settings.title = tide_run_game->game->title;
    if (!tide_run_settings.title) tide_run_settings.title = tide_run_settings.game_name;
    if (!tide_run_settings.title) tide_run_settings.title = "Tide";
    if (tide_run_settings.width <= 0) tide_run_settings.width = 960;
    if (tide_run_settings.height <= 0) tide_run_settings.height = 540;
    if (tide_run_settings.tick_rate < 0) tide_run_settings.tick_rate = 0; // The game's
    tide_run_headless = false;
    tide_run_headless_desc = (tide_headless_desc){0};
    bool scaled = false;
    for (int i = 1; i < tide_run_settings.argc; i++) {
        const char *arg = tide_run_settings.argv[i];
        if (strcmp(arg, "--debug-views") == 0) {
            tide_run_settings.debug_views = true;
        } else if (strcmp(arg, "--size") == 0) {
            const char *takes = "the window's width and height in pixels, like --size 1280 720";
            tide_run_settings.width = (int)tide_run_number(&i, arg, 1.0, 16384.0, takes);
            tide_run_settings.height = (int)tide_run_number(&i, arg, 1.0, 16384.0, takes);
        } else if (strcmp(arg, "--scale") == 0) {
            const char *takes = "a display's scale from 0.25 to 8, like --scale 2";
            tide_run_headless_desc.scale = (float)tide_run_number(&i, arg, 0.25, 8.0, takes);
            scaled = true;
        } else if (strcmp(arg, "--frames") == 0) {
            const char *takes = "how many frames the run lasts, like --frames 300";
            tide_run_headless_desc.frames = (uint32_t)tide_run_number(&i, arg, 1.0, 1e9, takes);
            tide_run_headless = true;
        } else if (strcmp(arg, "--capture") == 0) {
            tide_run_headless_desc.capture = tide_run_value(&i, arg, "the file the run's last frame goes to, like --capture menu.png");
            tide_run_headless = true;
        } else if (strcmp(arg, "--script") == 0) {
            tide_run_headless_desc.script = tide_run_value(&i, arg, "a file of input by frame, like --script play.txt");
            tide_run_headless = true;
        }
    }
    if (scaled && !tide_run_headless) {
        fprintf(stderr, "tide: --scale is for a run with no window: with --capture, --frames or --script\n");
        exit(2);
    }
    tide_run_headless_desc.width = tide_run_settings.width;
    tide_run_headless_desc.height = tide_run_settings.height;
#if defined(__wasm__) || defined(__ANDROID__)
    if (tide_run_headless) {
        fprintf(stderr, "tide: --capture, --frames and --script run a game with no window, on desktop only\n");
        tide_run_headless = false;
    }
#endif
}

// Opens tide_run_game's window, as `desc` and the game's settings say, or
// none for a run that asked for none.
static inline void tide_run_open(const tide_run_desc *desc)
{
    tide_run_configure(desc);
    if (tide_run_headless) {
        tide_platform_open_headless(&tide_run_headless_desc);
        return;
    }
    tide_platform_open(&(tide_window_desc){.title = tide_run_settings.title,
                                           .width = tide_run_settings.width,
                                           .height = tide_run_settings.height});
}

// What the session ticks on: the platform's pool, a thread for each core, or
// this thread alone where there's none.
static inline const tide_jobs *tide_run_jobs(void)
{
    const tide_jobs *pool = tide_platform_jobs();
    return pool ? pool : &tide_run_alone;
}

// The debug views, once a frame. They're on with tide_run_desc.debug_views,
// --debug-views on the command line or the game's debugViews setting, which a
// hot reload can change: F7 then shows the profiler, and hides it again. Ticks
// and views say how long their systems and views took only while something
// shows it.
static inline void tide_run_debug_views(void)
{
    const bool on = tide_run_settings.debug_views || tide_run_game->game->debug_views;
    const bool f7 = tide_run_devices.keyboard.f7.held; // As the last poll left it
    if (on && !tide_run_debug_told) {
        fprintf(stderr, "tide: debug views are on: F7 shows the profiler\n");
        tide_run_debug_told = true;
    }
    if (on && f7 && !tide_run_f7) {
        tide_run_profiling = !tide_run_profiling;
        if (tide_run_profiling) tide_profiler_reset(&tide_run_profiler); // What it last measured is old
    }
    if (!on) tide_run_profiling = false;
    tide_run_f7 = f7;
    tide_profile *profile = tide_run_stats() || tide_run_profiling ? &tide_run_profiler.profile : NULL;
    tide_platform_jobs_profile(profile);
    tide_run_alone.profile = profile;
    tide_run_gui.profile = tide_run_profiling ? profile : NULL;
}

// A session for tide_run_game, in no match yet. Its matches tick at the game's
// rate unless the host said otherwise.
static inline tide_session *tide_run_new_session(void)
{
    return tide_session_create(&(tide_session_desc){
        .game = tide_run_game->game,
        .tick_rate = (uint32_t)tide_run_settings.tick_rate,
        .sample = tide_run_sample,
        .jobs = tide_run_jobs(),      // Ticks on every core
        // To keep running predicted ticks again within a frame's budget. A run
        // with no window has no clock, so it goes the same on every machine.
        .now_ns = tide_run_headless ? NULL : tide_time_now_ns,
        .correct_views = tide_run_game->correct_views ? tide_run_correct_views : NULL,
        .entropy = tide_platform_entropy, // A seed for each match it starts, so no two go the same
    });
}

// Bind local files before startup handlers or restored tasks can use them.
static inline void tide_run_bind_io(const tide_host_game *game, void *local)
{
    if (!game->set_io) return;
    char fallback[512];
    const char *id = game->game->app_id;
    if (!id) {
        const char *name = tide_run_settings.game_name ? tide_run_settings.game_name : "game";
        snprintf(fallback, sizeof fallback, "dev.tide.%s", name);
        for (char *p = fallback; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.'))
                *p = '_';
        id = fallback;
    }
    game->set_io(local, tide_platform_io_init(id));
}

// Starts tide_run_game: its local state and a session, then the match the
// command line asks for, or Main's if it's the match's.
static inline void tide_run_begin(void)
{
    const tide_host_game *game = tide_run_game;
    tide_run_local = calloc(1, game->local_size);
    tide_run_start = calloc(1, game->game->start_size);
    if (!tide_run_local || !tide_run_start) abort();
    tide_run_bind_io(game, tide_run_local);
    game->local_init(tide_run_local);
    tide_run_session = tide_run_new_session();
    tide_session_request request = {0};
    const bool asked = tide_run_arguments(&request);
    const bool open = asked && request.kind == TIDE_REQUEST_OPEN;
    if (asked && !open) {
        tide_run_request(&request, NULL); // --join or --connect
    } else if (!game->main_is_local) {
        // Main is the match's: it starts right away, opened with --host
        tide_run_request(&(tide_session_request){.kind = TIDE_REQUEST_START}, NULL);
        if (open) tide_run_request(&request, NULL);
    } else if (open) {
        fprintf(stderr, "tide: --host opens Main's match, and Main is local: open one from the game instead\n");
    }
}

// Ends what tide_run_begin started: the match, if any, and the local state.
static inline void tide_run_end(void)
{
    tide_session_destroy(tide_run_session);
    tide_run_session = NULL;
    if (tide_run_local) tide_run_game->local_free(tide_run_local);
    free(tide_run_local);
    free(tide_run_start);
    tide_run_local = NULL;
    tide_run_start = NULL;
    tide_run_dropped = false;
    tide_run_migrating_since = 0.0;
    tide_draw_free(&tide_run_gui.list);
    memset(&tide_run_gui, 0, sizeof tide_run_gui);
    tide_draw_forget(&tide_run_draw); // What the game starts over with is other pixels
}

// Host migration (see tide_session_take_over). The machine that runs a match
// tells the server the room its players can meet in again; one whose match
// lost its server goes there, to take the match over or to join whoever did.
static inline void tide_run_migrate(void)
{
    tide_session *s = tide_run_session;
    char code[TIDE_ROOM_CODE_LENGTH + 1] = "";
    char key[TIDE_ROOM_KEY_LENGTH + 1] = "";
    if (tide_session_status_of(s).server) {
        tide_platform_room_code(code, sizeof code);
        tide_platform_room_key(key, sizeof key);
        tide_session_set_room(s, code, key);
    }
    if (!tide_session_migrating(s, code, key)) {
        tide_run_migrating_since = 0.0;
        return;
    }
    if (tide_run_migrating_since == 0.0) {
        tide_run_migrating_since = tide_run_now > 0.0 ? tide_run_now : 1e-9;
        fprintf(stderr, "tide: the match lost its host; to room %s again\n", code);
        tide_platform_room_migrate(code, key);
        return;
    }
    tide_transport network;
    tide_address server;
    const int moved = tide_platform_room_migrated(&network, &server);
    if (moved == 1) {
        fprintf(stderr, "tide: this machine hosts the match now, in room %s\n", code);
        tide_session_take_over(s, network, tide_run_now);
    } else if (moved == 2) {
        tide_session_join(s, network, server, tide_run_now);
    } else if (moved == -2) {
        tide_session_fail(s, TIDE_DISCONNECT_ENDED); // Its host ended it: the goodbye was lost
    } else if (moved < 0 || tide_run_now - tide_run_migrating_since > 30.0) {
        tide_session_fail(s, TIDE_DISCONNECT_TIMED_OUT);
    }
}

static inline int tide_run_frame(void *user, const float seconds)
{
    (void)user;
    const tide_host_game *game = tide_run_game;
    const uint64_t began = tide_time_now_ns();
    // Asked every frame: a reload can bring the setting, or take it away
    const bool stats = tide_run_stats();
    tide_run_debug_views();
    // Where the frame's time goes, part by part, while the profiler is shown
    tide_profiler *profiler = tide_run_profiling ? &tide_run_profiler : NULL;
    tide_profiler_begin(profiler, seconds);
    tide_platform_poll(&tide_run_devices);
    tide_run_now += seconds;
    tide_profiler_mark(profiler, TIDE_FRAME_INPUT);

    tide_session *s = tide_run_session;
    // Existing visual errors age before this update adds fresh corrections.
    game->frame_time(tide_run_local, seconds);
    // A room joined that isn't one, or whose host can't be reached, fails now
    // rather than timing out.
    if (tide_platform_room_failed()) tide_session_fail(s, TIDE_DISCONNECT_FAILED);
    tide_session_update(s, tide_run_now);
    tide_run_migrate();
    tide_session_event event;
    while (tide_session_next_event(s, &event)) {
        const bool connected = event.kind == TIDE_SESSION_CONNECTED_EVENT;
        tide_run_dropped = !connected && event.reason != TIDE_DISCONNECT_LEFT && event.reason != TIDE_DISCONNECT_FAILED &&
                           event.reason != TIDE_DISCONNECT_ENDED && event.reason != TIDE_DISCONNECT_KICKED;
        if (connected) game->connected(tide_run_local);
        else game->disconnected(tide_run_local, event.reason, event.message);
    }
    const tide_session_status status = tide_session_status_of(s);
    char room[TIDE_ROOM_CODE_LENGTH + 1] = "";
    if (status.open || !status.server) tide_platform_room_code(room, sizeof room); // A closed room isn't one to join
    game->set_session(tide_run_local, status.client.state, status.client.player, status.client.ping_ms, status.server,
                      status.open, room);
    tide_profiler_mark(profiler, TIDE_FRAME_MATCH);

    // Views draw at the frame rate, the match blended between its last two ticks
    const tide_view_worlds view = tide_session_view(s);
    const void *match = view.current;
    tide_draw_reset(&tide_run_draw);
    tide_gui_begin(&tide_run_gui, &tide_run_devices, tide_platform_screen_size(), tide_platform_measure_text);
    game->frame(match, view.previous, view.alpha, view.verified, tide_run_local, &tide_run_draw, &tide_run_gui);
    tide_session_mark_presented(s);
    tide_gui_end(&tide_run_gui, &tide_run_draw);
    tide_profiler_mark(profiler, TIDE_FRAME_VIEWS);
    tide_profiler_draw(profiler, &tide_run_draw, tide_platform_screen_size(), tide_platform_measure_text);
    tide_platform_typing(tide_gui_typing(&tide_run_gui));
    tide_platform_cursor_lock(tide_gui_cursor_lock(&tide_run_gui)); // The next poll says whether the window holds it
    tide_platform_draw(&tide_run_draw);
    tide_profiler_mark(profiler, TIDE_FRAME_RENDER);

    if (stats) {
        tide_run_stats_frames++;
        tide_run_stats_work += tide_time_now_ns() - began;
        const double span = tide_run_now - tide_run_stats_since;
        if (span >= 1.0) {
            const uint64_t sent = status.sent_bytes, received = status.received_bytes;
            tide_run_fps = tide_run_stats_frames / span;
            tide_run_work_ms = (double)tide_run_stats_work * 1e-6 / tide_run_stats_frames;
            tide_run_up = sent >= tide_run_stats_sent ? (double)(sent - tide_run_stats_sent) / span : 0.0;
            tide_run_down = received >= tide_run_stats_received ? (double)(received - tide_run_stats_received) / span : 0.0;
            tide_run_memory = tide_platform_memory();
            tide_run_stats_since = tide_run_now;
            tide_run_stats_frames = 0;
            tide_run_stats_work = 0;
            tide_run_stats_sent = sent;
            tide_run_stats_received = received;
        }
    }
    if (stats && match) {
        const tide_jobs *jobs = tide_platform_jobs(); // What the session ticks on
        char text[512];
        char delay[32] = "";
        char memory[32] = ""; // Where the system says
        const double megabytes = (double)tide_run_memory / (1024.0 * 1024.0);
        if (status.client.delay_ticks) snprintf(delay, sizeof delay, "\ndelay %u ticks", (unsigned)status.client.delay_ticks);
        if (tide_run_memory) snprintf(memory, sizeof memory, "\nmemory %.0f MB", megabytes);
        int n = snprintf(text, sizeof text,
                         "%.0f fps\ncpu %.1f ms\nping %u ms%s\nup %.1f KB/s\ndown %.1f KB/s\ntick %d\n%u entities\n"
                         "threads %u%s\n%s\n%u draw calls",
                         tide_run_fps, tide_run_work_ms, (unsigned)status.client.ping_ms, delay, tide_run_up / 1024.0,
                         tide_run_down / 1024.0, (int)game->tick(match), (unsigned)game->entity_count(match),
                         jobs ? (unsigned)jobs->threads : 1u, memory, tide_platform_renderer(),
                         (unsigned)tide_platform_draw_calls());
        // The three systems that took longest in the last tick. Keep zero-time
        // rows too: a short system can fall below the clock's resolution, and
        // dropping its row would move every stat above it.
        const tide_profile_time *times = tide_run_profiler.profile.systems.items;
        const uint32_t count = tide_run_profiler.profile.systems.count;
        bool shown[3] = {false, false, false};
        uint32_t slow[3] = {0, 0, 0};
        for (int k = 0; k < 3 && n > 0 && (size_t)n < sizeof text; k++) {
            uint32_t best = UINT32_MAX;
            for (uint32_t s = 0; s < count; s++) {
                bool taken = false;
                for (int j = 0; j < k; j++) taken |= shown[j] && slow[j] == s;
                if (!taken && times[s].name[0] && (best == UINT32_MAX || times[s].ms > times[best].ms)) best = s;
            }
            if (best == UINT32_MAX) break;
            shown[k] = true;
            slow[k] = best;
            n += snprintf(text + n, sizeof text - (size_t)n, "\n%s %.2f ms", times[best].name, times[best].ms);
        }
        tide_platform_draw_overlay(text);
    }

    if (game->command && game->command_sent) {
        uint8_t bytes[TIDE_COMMAND_BYTES];
        uint32_t size;
        while ((size = game->command(tide_run_local, bytes, sizeof bytes)) != 0) {
            if (!tide_session_send_command(s, bytes, size)) break;
            game->command_sent(tide_run_local);
        }
    }
    tide_session_request request;
    while (game->take_request(tide_run_local, &request, tide_run_start)) tide_run_request(&request, tide_run_start);
    // While nobody sees the window, the next frame is the match's next tick
    tide_platform_next_frame(tide_session_until_tick(s));
    tide_profiler_end(profiler, status.client.again_ticks);
    return TIDE_KEEP_RUNNING;
}

// Runs `game` in a window until it closes (see tide/run.h).
_Noreturn static inline void tide_host_run(const tide_run_desc *desc, const tide_host_game *game)
{
    tide_run_game = game;
    tide_run_open(desc);
    tide_run_begin();
    tide_platform_run(tide_run_frame, NULL);
}
