#pragma once

// The standard host's loop: a game in a window, in a session, driven through
// a table of the game's functions (tide_host_game).
//
// tide/run.h makes the table for a game compiled into the program, which is
// how games run. Under `tide run` the game is a library instead, and
// tide_host_run_library swaps in each new build of it that tide makes: hot
// reloading. The table is what lets one loop do both.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/layout.h"
#include "tide/hosting.h"
#include "tide/network.h"
#include "tide/page.h"
#include "tide/platform.h"
#include "tide/profiler.h"
#include "tide/profile_report.h"
#include "tide/session.h"
#include "tide/sha256.h"
#include "tide/stats.h"
#include "tide/time.h"
#include "tide/voice.h"
#include "tide/screen_keyboard.h"
#include "tide/worker.h"

typedef struct tide_run_desc {
    const char *title;      // The window's title, over the game's title setting; NULL for that
    const char *game_name;  // The title when neither says: the game's folder or target; default "Tide"
    int width;              // Starting window size in pixels; default 960 x 540
    int height;
    int tick_rate;          // Ticks per second, over the game's tickRate setting; 0 for that, or else 60
    // Show the stats in the bottom right corner, as the game's `stats` setting does: a drawer (tide/stats.h), one
    // line with the frame rate, which a click or a tap on it or F8 opens to the rest: what a frame's work takes,
    // ping, bandwidth, entity count, threads, memory, what draws, its draw calls and the slowest systems
    bool stats;
    // ...and they start open, for whoever asked to read them, as `tide run --stats` does. They start closed
    // without it: what a game's players get of them is the one line.
    bool stats_open;
    // The debug views are on, as with the game's debugViews setting or
    // --debug-views on the command line: F7 shows the profiler (tide/profiler.h)
    bool debug_views;
    // The command line: --host, --join and --connect, --stats, --debug-views, --size,
    // and --capture, --frames, --script, --scale and --players for a run with
    // no window (see tide/run.h)
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
    void (*frame)(const void *world, const void *previous, float alpha, const void *verified, const void *verified_previous, float verified_alpha, void *local,
                  tide_draw_list *draw, tide_gui *gui);
    // tide_frame_shards, before `frame`: in a sharded match, the other shards'
    // worlds this machine follows, which the frame draws beside its own
    // (tide_session_views). NULL for a game that isn't sharded.
    void (*frame_shards)(const tide_view_worlds *more, uint32_t count);
    // tide_local_events, before `frame`: what the match sent since the last one
    // (tide_session_events), which the game's local handlers hear as the frame
    // starts. It leaves `events` empty. Optional for C hosts.
    void (*events)(void *local, tide_event_log *events);
    // tide_local_audio, before `frame`: the frame's audio list, which Audio.Play records into (tide/audio.h).
    // NULL for a game that plays no sound.
    void (*audio)(void *local, tide_audio_frame *frame);
    // How long this frame is, before `frame`: local tasks' Wait.Seconds counts it down
    void (*frame_time)(void *local, float seconds);
    bool (*take_request)(void *local, tide_session_request *request, void *start);
    // The oldest packed command waiting in `channel` (0, the default, up to
    // the game's tide_game.command_channels, and TIDE_COMMAND_UNORDERED),
    // with its size, without removing it; NULL when none. Remove it only
    // once the session has copied it into its pending queue.
    const uint8_t *(*command)(const void *local, uint32_t channel, uint32_t *size);
    void (*command_sent)(void *local, uint32_t channel);
    // `room`: the code of the room the match is in, or what the game's own
    // network says it's joined by (up to TIDE_NETWORK_ADDRESS_BYTES with its NUL), "" if none
    void (*set_session)(void *local, uint32_t state, tide_player_id player, uint32_t ping, bool server, bool open,
                        const char *room);
    // tide_local_set_invite, after set_session: Session.invite, what others
    // open to join the match (tide_platform_room_invite). Optional for C hosts:
    // without it, set_session's room is the invite.
    void (*set_invite)(void *local, const char *invite);
    void (*connected)(void *local);
    void (*disconnected)(void *local, uint32_t reason, const char *message); // `message`: a kick's, or ""
    int32_t (*tick)(const void *world); // Time.tick
    uint32_t (*entity_count)(const void *world);
    // Frees what the game's code keeps for itself (scratch and target bookkeeping), before
    // its library is unloaded.
    void (*unload)(void);
    // Loads the game's assets from its pack (tide_assets_load, tide/pack.h): before the first
    // frame, and again with the pack tide writes under tide run as a named file changes. NULL
    // for a game with no assets.
    bool (*assets)(const uint8_t *pack, size_t size);
    // Its data layout, when tidec described it (tide run's builds), to carry
    // the game over to a build whose layout changed; NULL otherwise.
    const tide_layout *layout;
    // Graphics references in retained local state: native targets transfer to
    // the new build; a new address space recreates them. Optional for C hosts.
    void (*rebind_resources)(void *local, bool same_memory);
    // Optional: accumulated visual errors for tagged match fields. The context
    // belongs to this synchronous call; it never changes simulation state.
    void (*correct_views)(void *local, const tide_view_correction *correction);
    // tide_network_get: the network the game's C registered for its matches
    // (tide/network.h), which a library keeps in its own copy of the engine.
    // NULL for a table that never has one: its matches are in Tide's rooms.
    const tide_network *(*network)(void);
    // What another program needs to run the game (tide/embed.h): its view
    // packet, commands and scenes by name; NULL for a game built without it.
    const struct tide_embed_game *embed;
    // Optional voice package, registered in the game's own engine copy.
    const tide_voice_provider *(*voice)(void);
    // Every shader the game's draws can go through, and NULL after them (tidec's tide_game_shaders), whose pipelines
    // the host has the renderer make before anything draws with them (tide_platform_prewarm). Optional for C hosts.
    const tide_shader *const *shaders;
    // Optional: age existing presentation corrections before a session update.
    // Separate from frame_time so updates between draws neither change Frame.dt
    // nor reseed local randomness. Seconds cover only time since the last update.
    void (*age_views)(void *local, float seconds);
    // Optional: tide_screen_keyboard_get, the keyboard on screen the game's C
    // registered (tide/screen_keyboard.h) in its own engine copy.
    const tide_screen_keyboard *(*screen_keyboard)(void);
    // Optional: tide_gpu_reads_share, which the host calls with its own table of GPU lists' reads as it loads the
    // library, so the game's reads (in its own engine copy) are the ones the host's renderer begins and finishes.
    void (*gpu_reads_share)(tide_gpu_reads *table);
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
// fully reloads it too, as Ctrl+R in its window does: the match, local state
// and the library's globals start fresh. Native builds only (platform/src/reload.c).
_Noreturn void tide_host_run_library(const tide_run_desc *desc, const char *dir);

// The same on the web (tide run --web), where each new build is a program of
// its own, which the page starts in the running one's place: `game` goes on
// from what the last one left it, carried over by name, or starts as
// tide_host_run would. Web builds only (platform/src/reload.c).
_Noreturn void tide_host_run_web(const tide_run_desc *desc, const tide_host_game *game);

// ---------------------------------------------------------------------------
// The loop, for tide/run.h and platform/src/reload.c

static const tide_host_game *tide_run_game;
// The game's pack of assets as the host read it (tide_platform_pack, or under
// tide run the newest pack-<n>): kept, so a reloaded library gets it too.
static uint8_t *tide_run_pack;
static size_t tide_run_pack_size;

// The pack from `bytes` (malloc'd, the host's from here on), which the game
// has loaded (`loaded`) or found isn't one.
static inline void tide_run_assets_kept(uint8_t *bytes, const size_t size, const bool loaded)
{
    if (tide_run_pack != bytes) free(tide_run_pack);
    tide_run_pack = bytes;
    tide_run_pack_size = size;
    if (!loaded) fprintf(stderr, "tide: the game's pack isn't one: its assets read as empty\n");
    tide_clips_prepare(tide_platform_jobs()); // Its clips decoded now, on the pool's threads, not at their first play
}

// The pack from `bytes` (malloc'd, the host's from here on): the game loads it.
static inline void tide_run_assets_from(uint8_t *bytes, const size_t size)
{
    free(tide_run_pack); // The last one's bytes first: a pack can be gigabytes
    tide_run_pack = NULL;
    const bool loaded = !tide_run_game->assets || !bytes || tide_run_game->assets(bytes, size);
    tide_run_assets_kept(bytes, size, loaded);
}

// The pack the build left with the program, for a game that has assets.
static inline void tide_run_load_assets(void)
{
    if (!tide_run_game->assets) return;
    size_t size = 0;
    uint8_t *bytes = tide_platform_pack(&size);
    if (!bytes) {
        fprintf(stderr, "tide: the game's pack of assets isn't with the program (<program>.pack beside it): they read as empty\n");
        return;
    }
    tide_run_assets_from(bytes, size);
}
static void *tide_run_local;  // The game's tide_local
static void *tide_run_start;  // A tide_start, for what local code asks
static const char *tide_run_data_directory;
static tide_devices tide_run_devices;
// The watching host's shortcuts, immediately after polling and before the
// game reads its devices. Plain hosts leave this unset.
static void (*tide_run_input)(tide_devices *devices);
static tide_draw_list tide_run_draw;
static tide_audio_frame tide_run_audio; // What the frame says should be sounding (tide/audio.h)
static tide_gui tide_run_gui;
// The profiler, and in it how long each system and view took, measured while
// it's shown or the stats' drawer is open
static tide_profiler tide_run_profiler;
static bool tide_run_profiling; // The profiler is shown
static bool tide_run_f7;        // F7 was held at the last frame
static bool tide_run_debug_told;
static tide_profile_report tide_run_report;
static const char *tide_run_report_path;
static uint32_t tide_run_report_warmup, tide_run_report_frames;
static bool tide_run_reporting, tide_run_report_started;
static char tide_run_report_executable[65];
// What ticks run on where the platform has no pool: this thread, which still
// has a profile to fill
static tide_jobs tide_run_alone = {.threads = 1};
static tide_run_desc tide_run_settings;
// A run with no window, when the command line asks for one with --capture,
// --frames or --script: what it asked (tide_platform_open_headless)
static bool tide_run_headless;
static tide_headless_desc tide_run_headless_desc;
// --seed <n> on the command line: the seed of every match this machine starts
// (Time.seed), in place of one from the system
static bool tide_run_seeded;
static uint32_t tide_run_seed;
// --shards <n> and --shard-width <units>: a sharded game's matches on this
// machine split into n regions along x from the start, each that wide (0: 16
// times the game's reach), for trying borders out (tide/shards.h)
static uint32_t tide_run_shards;
static double tide_run_shard_width;
static tide_session *tide_run_session;
static double tide_run_now; // Seconds since the program started
// Rendered frames' summed seconds. A GPU wait can service the match ahead of
// this clock; the next frame includes that wait and must not count it twice.
static double tide_run_frame_now;
// --latency, --jitter and --loss: this machine's way to a match it joins, as
// the command line says for everyone or for this player of several
static tide_net_conditions tide_run_conditions;
// The server went away or turned this machine away: not because it left,
// couldn't start, was kicked or banned, or the match ran out of scenes
static bool tide_run_dropped;
// The game asked to quit (Application.Quit): the program ends after the
// frame, or inside another program, that program hears it (tide_embed_status)
static bool tide_run_quit;
// Host migration: when this machine went to its match's room again, or 0
static double tide_run_migrating_since;
// The last Join or Connect asked for, which a script's `rejoin` asks for again
// (tide_platform_players_action): no kind before one
static tide_session_request tide_run_joined;
// The game's own network, while the session's match is on one (tide/network.h)
static tide_network_match tide_run_network;
// The camera another program draws its scene with (tide_embed_camera): the
// frame's views start with it rather than Tide's own first camera.
static bool tide_run_view_camera;
static tide_float4x4 tide_run_view_camera_transform, tide_run_view_camera_projection;

// Inside another program's frame (tide/embed.h), which reads the devices,
// draws and plays itself: the frame leaves the GPU, the audio device and the
// stats' drawer alone, and the platform takes the devices from that program
static bool tide_run_embedded;
// The frame's views draw the match's latest tick as it is, not blended into
// the one before: a debugger's paused match (tide/embed.h's time travel)
static bool tide_run_unblended;
// The room the last frame told local code the match is in (Session.room)
static char tide_run_room[TIDE_NETWORK_ADDRESS_BYTES];

// The network the game registered, or NULL: its matches are in Tide's rooms.
static inline const tide_network *tide_run_registered(void)
{
    return tide_run_game->network ? tide_run_game->network() : NULL;
}
// The stats (tide_run_stats): their drawer, which is open or closed whatever
// build of the game runs, and what the host draws it into, over the frame.
static tide_stats tide_run_drawer;
static tide_draw_list tide_run_over;
// ...and its numbers: frames, their work and bytes over the last second,
// counted from when it began, with the session's totals then.
static double tide_run_stats_since;
static uint32_t tide_run_stats_frames;
// Nanoseconds the frames' work took, not the drawer nor the wait for the
// display, and how many frames that's of: the ones the drawer was open for
static uint64_t tide_run_stats_work;
static uint32_t tide_run_stats_worked;
static uint64_t tide_run_stats_render;
static uint32_t tide_run_stats_rendered, tide_run_stats_gpu_samples;
static double tide_run_stats_gpu, tide_run_timing_span;
static bool tide_run_timing;
static uint32_t tide_run_timing_epoch;
static uint64_t tide_run_stats_sent, tide_run_stats_received;
static bool tide_run_stats_known;                       // There's a frame rate to show: the drawer shows from then
static double tide_run_fps, tide_run_up, tide_run_down; // A second, over the last one
static double tide_run_work_ms;                         // A frame's work, over the last half second; under 0 until known
static double tide_run_render_ms, tide_run_gpu_ms;       // Rendering on the CPU and GPU, over the last half second
static uint64_t tide_run_memory;                        // tide_platform_memory, as of then
static double tide_run_memory_at = -1.0;                // When the profiler last had it read, in tide_run_now

static int tide_run_report_finish(const int code)
{
    const tide_jobs *jobs = tide_platform_jobs();
    const tide_float2 size = tide_platform_screen_size(), scale = tide_platform_screen_scale();
    tide_platform_gpu_profile_read();
    const tide_profile_report_info info = {
        .title = tide_run_settings.title, .renderer = tide_platform_renderer(), .compiler = __clang_version__,
        .executable_sha256 = tide_run_report_executable[0] ? tide_run_report_executable : NULL,
        .layout_hash = tide_run_game->game->hash, .simulation = tide_run_game->game->simulation,
        .total_frames = tide_run_report_frames, .warmup = tide_run_report_warmup,
        .player = tide_platform_player(), .players = tide_platform_players(), .threads = jobs ? jobs->threads : 1u,
        .width = (uint32_t)size.x, .height = (uint32_t)size.y, .scale_x = scale.x, .scale_y = scale.y,
#ifdef __OPTIMIZE__
        .optimized = true,
#endif
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
        .sanitized = true,
#endif
        .gpu_pending_passes = tide_platform_gpu_profile_pending(),
        .exit_code = code,
    };
    char *path = tide_platform_players_file(tide_run_report_path);
    FILE *file = fopen(path, "wb");
    bool ok = file && tide_profile_report_write(&tide_run_report, file, &info);
    if (file && fclose(file) != 0) ok = false;
    if (!ok) fprintf(stderr, "tide: couldn't write profile '%s'; choose a writable JSON path\n", path);
    else fprintf(stderr, "tide: profile written to %s\n", path);
    free(path);
    tide_run_draw.diagnostics = NULL;
    tide_platform_gpu_profile(NULL, NULL);
    tide_profile_report_free(&tide_run_report);
    return !ok && code == 0 ? 1 : code;
}

static inline void tide_run_report_identity(void)
{
    tide_run_report_executable[0] = '\0';
    if (!tide_run_settings.argc || !tide_run_settings.argv[0]) return;
    FILE *file = fopen(tide_run_settings.argv[0], "rb");
    if (!file) return;
    tide_sha256 sha;
    tide_sha256_init(&sha);
    uint8_t bytes[16384];
    size_t n;
    while ((n = fread(bytes, 1, sizeof bytes, file)) != 0) tide_sha256_add(&sha, bytes, n);
    const bool ok = !ferror(file);
    fclose(file);
    if (!ok) return;
    uint8_t digest[32];
    tide_sha256_end(&sha, digest);
    for (uint32_t i = 0; i < 32u; i++) snprintf(tide_run_report_executable + 2u * i, 3, "%02x", (unsigned)digest[i]);
}

static inline void tide_run_reset_timing(const bool active)
{
    tide_run_stats_work = tide_run_stats_render = 0;
    tide_run_stats_worked = tide_run_stats_rendered = tide_run_stats_gpu_samples = 0;
    tide_run_stats_gpu = tide_run_timing_span = 0.0;
    tide_run_work_ms = tide_run_render_ms = tide_run_gpu_ms = -1.0;
    tide_platform_gpu_timing(false);
    tide_stats_collect(&tide_run_drawer, NULL, 0.0);
    // F7 may have kept measuring while the drawer was closed, or across a reload.
    if (active) tide_stats_collect(&tide_run_drawer, &tide_run_profiler.profile, 0.0);
    tide_run_timing_epoch = tide_run_draw.epoch;
}

// Whether the stats show: the host asked for them (--stats, STATS), or the
// game's `stats` setting does.
static inline bool tide_run_stats(void)
{
    return tide_run_settings.stats || tide_run_game->game->stats;
}

// The stats' drawer, once a frame, right after the devices are polled and
// before anything reads them: a click or a tap on it, or F8, opens it and
// closes it. The game sees the key too. The press of a click is the drawer's,
// which tide_run_sample and the frame's GUI then never see. Closed, it shows
// what the host knows anyway, so nothing is measured for it: what its list
// shows is, from when it opens.
static inline void tide_run_drawer_input(const bool stats)
{
    if (!stats) {
        tide_stats_off(&tide_run_drawer);
        tide_run_stats_known = false; // Counted from nothing when they're back
        tide_run_stats_since = tide_run_now;
        tide_run_stats_frames = 0;
        return;
    }
    const bool f8 = tide_run_devices.keyboard.f8.held;
    if (!tide_stats_input(&tide_run_drawer, &tide_run_devices, tide_platform_screen_size(), f8)) return;
    if (!tide_run_drawer.open) return;
    tide_run_work_ms = -1.0; // No frame was timed yet
    tide_run_memory = tide_platform_memory();
}

// This machine's input for one tick.
static inline void tide_run_sample(void *user, const uint32_t tick, void *input)
{
    (void)user;
    (void)tick;
    if (!tide_run_game->sample_input) return;
    tide_devices sampled = tide_run_devices;
    tide_gui_hide(&tide_run_gui, &sampled); // A click on a button isn't the game's
    tide_stats_hide(&tide_run_drawer, &sampled); // ...nor one on the stats
    tide_run_game->sample_input(&sampled, tide_run_local, input);
    tide_devices_consume(&tide_run_devices);
}

// What views were corrected by so far, for a page that measures the netcode
// (tide_run_netcode): the times a wrong guess was put right, and the times
// what they drew was replaced outright
static uint32_t tide_run_corrections;
static uint32_t tide_run_view_resets;

static inline void tide_run_correct_views(void *user, const tide_view_correction *correction)
{
    (void)user;
    if (correction->reset) tide_run_view_resets++;
    else tide_run_corrections++;
    if (tide_run_game->correct_views) tide_run_game->correct_views(tide_run_local, correction);
}

// The session's numbers for a page that asked for them (its `Tide.netcode`,
// tide_platform_netcode_wanted), once a frame, in this order: its state, whether
// this machine runs the server, the ping and what an answer takes in
// milliseconds, the ticks confirmed, run and the server's as it last said, the
// input delay in ticks, how long its inputs wait at the server in milliseconds,
// the ticks counted as late, run again, run at all and run ahead so far, the
// times the world was sent again, the times views were corrected and replaced,
// what the session's update and the whole frame's work took, in
// milliseconds, and what the input delay goes by: what a rollback would cost
// now, the most one may take, and what rollbacks have cost an update of late,
// in milliseconds. A value joins at the end, so a page that reads the first
// ones by their place keeps reading them.
static inline void tide_run_netcode(const tide_session_status *status, const uint64_t update_ns, const uint64_t work_ns)
{
    const tide_client_status *c = &status->client;
    const double values[] = {
        (double)c->state, status->server ? 1.0 : 0.0, (double)c->ping_ms, (double)c->answer_ms, (double)c->verified_tick,
        (double)c->predicted_tick, (double)c->server_tick, (double)c->delay_ticks, (double)c->early_ms, (double)c->late_ticks,
        (double)c->again_ticks, (double)c->run_ticks, (double)c->ahead_ticks, (double)c->resyncs,
        (double)tide_run_corrections, (double)tide_run_view_resets, (double)update_ns * 1e-6, (double)work_ns * 1e-6,
        (double)c->rollback_ms, (double)c->rollback_most_ms, (double)c->resim_ms,
    };
    tide_platform_netcode(values, (uint32_t)(sizeof values / sizeof values[0]));
}

static inline void tide_run_refresh_view_corrector(void)
{
    tide_session_set_view_corrector(tide_run_session,
                                   tide_run_game->correct_views ? tide_run_correct_views : NULL);
}

// Local commands waiting for transport capacity belong to the match whose
// views sent them. Explicit match changes discard them; migration keeps them.
// The channels a game's commands are in, to go through them: the default and
// the game's own, then the unordered ones as a channel of their own
static inline uint32_t tide_run_command_channel(const uint32_t index)
{
    return index < tide_game_command_channels(tide_run_game->game) ? index : TIDE_COMMAND_UNORDERED;
}

static inline void tide_run_discard_commands(void)
{
    const tide_host_game *game = tide_run_game;
    if (!game->command || !game->command_sent) return;
    uint32_t size;
    for (uint32_t i = 0; i <= tide_game_command_channels(game->game); i++) {
        const uint32_t channel = tide_run_command_channel(i);
        while (game->command(tide_run_local, channel, &size)) game->command_sent(tide_run_local, channel);
    }
}

static inline double tide_run_clock(void *context)
{
    (void)context;
    return tide_run_now;
}

// `network`, to a match this machine joins, as far away as --latency, --jitter
// and --loss make it. A machine that runs the match's server adds nothing: the
// way to it is each player's who joined.
static inline tide_transport tide_run_conditioned(const tide_transport network)
{
    const tide_net_conditions c = tide_run_conditions;
    tide_transport out;
    if (!network.send || (c.latency <= 0.0 && c.jitter <= 0.0 && c.loss <= 0.0)) return network;
    // The same lost and late datagrams every run, and another player's others
    const uint64_t seed = 0x6E6574636F6E6400ull ^ tide_platform_player();
    return tide_net_conditioned(network, c, seed, tide_run_clock, NULL, &out) ? out : network;
}

// Starts, joins or leaves a match, as local code or the command line asked,
// and copies what it asked to.
// `start` is a tide_start, or NULL for Main.
static tide_hosting_client *tide_run_hosting;
static bool tide_run_hosting_joined;
static char tide_run_hosting_endpoint[TIDE_NETWORK_ADDRESS_BYTES];

static inline void tide_run_hosting_leave(void)
{
    if (!tide_run_hosting) return;
    tide_session_leave(tide_run_session);
    tide_hosting_client_destroy(tide_run_hosting);
    tide_run_hosting = NULL;
    tide_run_hosting_joined = false;
}

static inline bool tide_run_hosted_request(const tide_session_request *request)
{
    const char *slash = strrchr(request->text, '/');
    if (!slash || slash == request->text || !slash[1]) return false;
    uint64_t match = 0;
    for (const char *p = slash + 1; *p; p++) {
        if (*p < '0' || *p > '9' || match > UINT64_C(0x0000ffffffffffff) / 10u) return false;
        match = match * 10u + (unsigned)(*p - '0');
        if (match > UINT64_C(0x0000ffffffffffff)) return false;
    }
    const size_t n = (size_t)(slash - request->text);
    if (n >= sizeof tide_run_hosting_endpoint) return false;
    memcpy(tide_run_hosting_endpoint, request->text, n);
    tide_run_hosting_endpoint[n] = 0;
    tide_transport transport = {0};
    tide_address server;
    const bool room = request->kind == TIDE_REQUEST_JOIN;
    const bool opened = room ? tide_platform_room_join(tide_run_hosting_endpoint, &transport, &server)
                            : tide_platform_resolve(tide_run_hosting_endpoint, (uint16_t)request->port, &server)
                              && tide_platform_udp_open(0, &transport);
    if (opened) transport = tide_run_conditioned(transport);
    tide_session_leave(tide_run_session);
    const uint64_t nonce = tide_platform_entropy();
    tide_hosting_client_desc d = {.transport = transport, .server = opened ? server : (tide_address){0},
        .match = match, .nonce = nonce, .secure = room};
    memcpy(d.simulation, tide_run_game->game->simulation, 32);
    tide_run_hosting = opened ? tide_hosting_client_create(&d, tide_run_now) : NULL;
    if (!tide_run_hosting) {
        if (opened) transport.close(transport.self);
        tide_session_fail(tide_run_session, TIDE_DISCONNECT_FAILED);
    }
    return true;
}

static inline void tide_run_request(const tide_session_request *request, const void *start)
{
    tide_session *s = tide_run_session;
    tide_transport network;
    tide_address server;
    const bool port_ok = request->port <= 65535u;
    const uint16_t port = port_ok ? (uint16_t)request->port : 0u;
    const tide_network *own = tide_run_registered();
    if (request->kind == TIDE_REQUEST_START || request->kind == TIDE_REQUEST_JOIN
        || request->kind == TIDE_REQUEST_CONNECT || request->kind == TIDE_REQUEST_LEAVE) tide_run_hosting_leave();
    if (request->kind == TIDE_REQUEST_JOIN || request->kind == TIDE_REQUEST_CONNECT) tide_run_joined = *request;
    if ((!own || request->kind == TIDE_REQUEST_CONNECT) && !tide_platform_players()
        && (request->kind == TIDE_REQUEST_JOIN || request->kind == TIDE_REQUEST_CONNECT)
        && tide_run_hosted_request(request)) {
        tide_run_discard_commands();
        tide_network_match_leave(&tide_run_network);
        return;
    }
    switch (request->kind) {
    case TIDE_REQUEST_START:
        tide_run_discard_commands();
        tide_network_match_leave(&tide_run_network);
        tide_session_start(s, start, tide_run_now);
        break;
    case TIDE_REQUEST_OPEN:
        // Players join on a UDP port (not on the web), and in a room. A match
        // opened before opens again where it was.
        if (!tide_session_status_of(s).server || tide_session_open(s, (tide_transport){0})) break;
        // On the game's own network, where it registered one, and on nothing else: no port, and no room
        if (own) {
            if (tide_network_match_host(&tide_run_network, own, &network)) tide_session_open(s, network);
            else fprintf(stderr, "tide: the game's network couldn't open the match to players\n");
            break;
        }
        // In a run of several players with no window, the network is made up (tide_platform_players)
        if (port_ok && (tide_platform_players() ? tide_platform_players_open(port, &network)
                                                : tide_platform_host_open(port, &network))) {
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
    case TIDE_REQUEST_BAN:
        tide_session_ban(s, request->player, request->text);
        break;
    case TIDE_REQUEST_UNBAN:
        tide_session_unban(s, request->player);
        break;
    case TIDE_REQUEST_QUIT:
        // After this frame, as when the window closes (tide_run_close). Another program's frame: that program decides
        tide_run_quit = true;
        if (!tide_run_embedded) tide_platform_quit();
        break;
    case TIDE_REQUEST_JOIN:
        tide_run_discard_commands();
        tide_network_match_leave(&tide_run_network);
        if (!(own ? tide_network_match_join(&tide_run_network, own, request->text, &network, &server)
              : tide_platform_players() ? tide_platform_players_join(request->text, &network, &server)
                                        : tide_platform_room_join(request->text, &network, &server))) {
            fprintf(stderr, own ? "tide: the game's network can't join '%s'\n" : "tide: can't join room '%s'\n", request->text);
            tide_session_leave(s);
            tide_session_fail(s, TIDE_DISCONNECT_FAILED);
            break;
        }
        tide_session_join(s, tide_run_conditioned(network), server, tide_run_now);
        break;
    case TIDE_REQUEST_CONNECT:
        tide_run_discard_commands();
        tide_network_match_leave(&tide_run_network);
        if (!port || !(tide_platform_players()
                           ? tide_platform_players_connect(request->text, port, &network, &server)
                           : tide_platform_resolve(request->text, port, &server) && tide_platform_udp_open(0, &network))) {
            fprintf(stderr, "tide: can't reach '%s'\n", request->text);
            tide_session_leave(s);
            tide_session_fail(s, TIDE_DISCONNECT_FAILED);
            break;
        }
        tide_session_join(s, tide_run_conditioned(network), server, tide_run_now);
        break;
    case TIDE_REQUEST_LEAVE:
        tide_run_discard_commands();
        tide_session_leave(s);
        tide_network_match_leave(&tide_run_network);
        break;
    case TIDE_REQUEST_END:
        if (tide_session_status_of(s).server) {
            tide_run_discard_commands();
            tide_session_end(s);
            tide_network_match_leave(&tide_run_network);
        }
        break;
    case TIDE_REQUEST_COPY:
        tide_platform_copy(request->text);
        break;
    case TIDE_REQUEST_WATCH:
        tide_session_watch(s, &request->area);
        break;
    case TIDE_REQUEST_UNWATCH:
        tide_session_watch(s, NULL);
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

// ...as a whole number from 0 to `most`, or the program ends the same way.
static inline uint32_t tide_run_whole(int *i, const char *flag, const uint32_t most, const char *takes)
{
    const char *text = tide_run_value(i, flag, takes);
    const char *c = text;
    uint64_t value = 0;
    for (; *c >= '0' && *c <= '9' && value <= most; c++) value = value * 10u + (uint64_t)(*c - '0');
    if (c != text && !*c && value <= most) return (uint32_t)value;
    fprintf(stderr, "tide: %s takes %s, not '%s'\n", flag, takes, text);
    exit(2);
}

// --latency <ms>, --jitter <ms> or --loss <percent>, at `*i`: for everyone,
// or with a player's number and a colon (2:120) for that player of a run of
// several, into `said`, by kind and then player (0 for everyone), in seconds
// and shares. Anything else ends the program, saying what the flag takes.
static inline void tide_run_condition(int *i, const char *flag, double said[3][33])
{
    const uint32_t kind = strcmp(flag, "--latency") == 0 ? 0u : strcmp(flag, "--jitter") == 0 ? 1u : 2u;
    const char *takes = kind == 0u ? "how many milliseconds a datagram takes each way, from 0 to 10000, like --latency 80, "
                                     "or one player's, like --latency 2:120"
                      : kind == 1u ? "up to how many milliseconds more a datagram takes, at random, from 0 to 10000, like "
                                     "--jitter 20, or one player's, like --jitter 2:20"
                                   : "the share of datagrams lost, in percent from 0 to 100, like --loss 5, or one player's, "
                                     "like --loss 2:5";
    const char *text = tide_run_value(i, flag, takes);
    const char *number = text;
    uint32_t player = 0;
    const char *colon = strchr(text, ':');
    if (colon) {
        for (const char *c = text; c < colon && player <= 32u; c++) {
            player = *c >= '0' && *c <= '9' ? player * 10u + (uint32_t)(*c - '0') : 33u;
        }
        number = colon + 1;
    }
    char *end = NULL;
    const double value = strtod(number, &end);
    const double most = kind == 2u ? 100.0 : 10000.0;
    if ((colon && (colon == text || player < 1u || player > 32u)) || end == number || *end || !(value >= 0.0 && value <= most)) {
        fprintf(stderr, "tide: %s takes %s, not '%s'\n", flag, takes, text);
        exit(2);
    }
    said[kind][player] = value / (kind == 2u ? 100.0 : 1000.0);
}

// How tide_run_game runs: what `desc` says, with the defaults for what it and
// the game's settings leave out, and what its command line says: --stats, --debug-views,
// the window's size, its matches' seed, and a run with no window.
static inline void tide_run_configure(const tide_run_desc *desc)
{
    tide_run_settings = *desc;
    tide_run_drawer.open = desc->stats_open;
    tide_run_work_ms = -1.0; // No frame was timed yet
    if (!tide_run_settings.title) tide_run_settings.title = tide_run_game->game->title;
    if (!tide_run_settings.title) tide_run_settings.title = tide_run_settings.game_name;
    if (!tide_run_settings.title) tide_run_settings.title = "Tide";
    if (tide_run_settings.width <= 0) tide_run_settings.width = 960;
    if (tide_run_settings.height <= 0) tide_run_settings.height = 540;
    if (tide_run_settings.tick_rate < 0) tide_run_settings.tick_rate = 0; // The game's
    tide_run_headless = false;
    tide_run_headless_desc = (tide_headless_desc){0};
    tide_run_seeded = false;
    tide_run_seed = 0;
    tide_run_shards = 0;
    tide_run_shard_width = 0.0;
    tide_run_data_directory = NULL;
    tide_run_report_path = NULL;
    tide_run_report_warmup = tide_run_report_frames = 0;
    tide_run_reporting = tide_run_report_started = false;
    tide_run_conditions = (tide_net_conditions){0};
    double conditions[3][33]; // --latency, --jitter and --loss, as tide_run_condition has them: -1 unsaid
    for (uint32_t kind = 0; kind < 3u; kind++) {
        for (uint32_t player = 0; player <= 32u; player++) conditions[kind][player] = -1.0;
    }
    bool scaled = false, profile_warmup = false;
    for (int i = 1; i < tide_run_settings.argc; i++) {
        const char *arg = tide_run_settings.argv[i];
        if (strcmp(arg, "--stats") == 0) {
            tide_run_settings.stats = true; // Keep the host's chosen initial drawer state.
        } else if (strcmp(arg, "--debug-views") == 0) {
            tide_run_settings.debug_views = true;
        } else if (strcmp(arg, "--profile") == 0) {
            if (tide_run_report_path) {
                fprintf(stderr, "tide: --profile takes one JSON report path\n");
                exit(2);
            }
            tide_run_report_path = tide_run_value(&i, arg, "a JSON report path, like --profile profile.json");
            if (!*tide_run_report_path || *tide_run_report_path == '-') {
                fprintf(stderr, "tide: --profile takes a nonempty JSON report path, like --profile profile.json\n");
                exit(2);
            }
        } else if (strcmp(arg, "--profile-warmup") == 0) {
            if (profile_warmup) {
                fprintf(stderr, "tide: --profile-warmup takes one frame count\n");
                exit(2);
            }
            tide_run_report_warmup = tide_run_whole(&i, arg, 1000000000u, "a frame count from 0 to 1000000000");
            profile_warmup = true;
        } else if (strcmp(arg, "--data-dir") == 0) {
            tide_run_data_directory = tide_run_value(&i, arg, "a save directory, like --data-dir test-saves");
            if (!*tide_run_data_directory || *tide_run_data_directory == '-') {
                fprintf(stderr, "tide: --data-dir takes a nonempty save directory, like --data-dir test-saves\n");
                exit(2);
            }
        } else if (strcmp(arg, "--size") == 0) {
            const char *takes = "the window's width and height in pixels, like --size 1280 720";
            tide_run_settings.width = (int)tide_run_number(&i, arg, 1.0, 16384.0, takes);
            tide_run_settings.height = (int)tide_run_number(&i, arg, 1.0, 16384.0, takes);
        } else if (strcmp(arg, "--seed") == 0) {
            const char *takes = "the seed of the matches this machine starts, a whole number from 0 to 2147483647, like --seed 7";
            tide_run_seed = tide_run_whole(&i, arg, 2147483647u, takes);
            tide_run_seeded = true;
        } else if (strcmp(arg, "--shards") == 0) {
            const char *takes = "how many regions a sharded game's match starts split into along x, from 1 to 256, like --shards 2";
            tide_run_shards = tide_run_whole(&i, arg, 256u, takes);
            if (!tide_run_game->game->shards) {
                fprintf(stderr, "tide: --shards is for a sharded game: one with 'settings { shards = true; }'\n");
                exit(2);
            }
        } else if (strcmp(arg, "--shard-width") == 0) {
            const char *takes = "how wide each region --shards makes is, in world units, like --shard-width 100";
            tide_run_shard_width = tide_run_number(&i, arg, 1e-3, 1e12, takes);
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
        } else if (strcmp(arg, "--record") == 0) {
            tide_run_headless_desc.record = tide_run_value(&i, arg, "the file the whole run goes to, as a GIF, like --record run.gif");
            tide_run_headless = true;
        } else if (strcmp(arg, "--capture-audio") == 0) {
            tide_run_headless_desc.capture_audio =
                tide_run_value(&i, arg, "the file everything the run plays goes to, like --capture-audio run.wav");
            tide_run_headless = true;
        } else if (strcmp(arg, "--interactive") == 0) {
            tide_run_headless_desc.interactive = tide_run_value(&i, arg, "a replay file, like --interactive play.txt");
            tide_run_headless = true;
        } else if (strcmp(arg, "--script") == 0) {
            tide_run_headless_desc.script = tide_run_value(&i, arg, "a file of input by frame, like --script play.txt");
            tide_run_headless = true;
        } else if (strcmp(arg, "--microphone") == 0) {
            tide_run_headless_desc.microphone =
                tide_run_value(&i, arg, "a WAV or Ogg Vorbis file the microphone plays in the run, like --microphone voice.wav");
            tide_run_headless = true;
        } else if (strcmp(arg, "--players") == 0) {
            const char *takes = "how many players the run has, from 1 to 32, like --players 2";
            tide_run_headless_desc.players = tide_run_whole(&i, arg, 32u, takes);
            if (!tide_run_headless_desc.players) {
                fprintf(stderr, "tide: --players takes %s, not '0'\n", takes);
                exit(2);
            }
        } else if (strcmp(arg, "--latency") == 0 || strcmp(arg, "--jitter") == 0 || strcmp(arg, "--loss") == 0) {
            tide_run_condition(&i, arg, conditions);
        } else if (strcmp(arg, "--player") == 0) {
            // What the first player of such a run starts the others with, after its own command line
            const char *takes = "which player of a run this is and where its first listens: it's what --players starts the others with";
            tide_run_headless_desc.player = tide_run_whole(&i, arg, 32u, takes);
            tide_run_headless_desc.hub = tide_run_whole(&i, arg, 65535u, takes);
        }
    }
    if (profile_warmup && !tide_run_report_path) {
        fprintf(stderr, "tide: --profile-warmup requires --profile <file.json>\n");
        exit(2);
    }
    if (tide_run_report_path && !tide_run_headless_desc.frames && !tide_run_headless_desc.script && !tide_run_headless_desc.interactive) {
        fprintf(stderr, "tide: --profile requires --frames, --script or --interactive to bound or control the run\n");
        exit(2);
    }
    // This machine's: a player's own where the command line says one, and everyone's otherwise
    const uint32_t players = tide_run_headless_desc.players;
    const uint32_t own = !players ? 0u : tide_run_headless_desc.player >= 2u && tide_run_headless_desc.player <= players
                                           ? tide_run_headless_desc.player : 1u;
    double *const kinds[3] = {&tide_run_conditions.latency, &tide_run_conditions.jitter, &tide_run_conditions.loss};
    for (uint32_t kind = 0; kind < 3u; kind++) {
        const char *flag = kind == 0u ? "--latency" : kind == 1u ? "--jitter" : "--loss";
        for (uint32_t player = 1; player <= 32u; player++) {
            if (conditions[kind][player] < 0.0 || player <= players) continue;
            if (players) {
                fprintf(stderr, "tide: %s %u:... is for player %u, and this run has %u players\n", flag, (unsigned)player,
                        (unsigned)player, (unsigned)players);
            } else {
                fprintf(stderr, "tide: %s %u:... is for one player of a run of several, which --players makes, like "
                                "--players 2: on its own, give it no player, like %s 80\n", flag, (unsigned)player, flag);
            }
            exit(2);
        }
        const double said = conditions[kind][own] >= 0.0 ? conditions[kind][own] : conditions[kind][0];
        *kinds[kind] = said > 0.0 ? said : 0.0;
    }
    if (scaled && !tide_run_headless) {
        fprintf(stderr, "tide: --scale is for a run with no window: with --capture, --frames or --script\n");
        exit(2);
    }
    if (tide_run_headless_desc.players) {
        const uint32_t players = tide_run_headless_desc.players;
        tide_session_request asked;
        if (!tide_run_headless) {
            fprintf(stderr, "tide: --players is for a run with no window: with --capture, --frames or --script\n");
            exit(2);
        }
        if (players > tide_game_players(tide_run_game->game)) {
            fprintf(stderr, "tide: --players %u is more players than the game takes: its maxPlayers is %u\n", (unsigned)players,
                    (unsigned)tide_game_players(tide_run_game->game));
            exit(2);
        }
        if (tide_run_arguments(&asked)) {
            fprintf(stderr, "tide: --players takes no --host, --join or --connect: its first player opens Main's match and the "
                            "others join it, and where Main is local, the game's own code does, as its script has it\n");
            exit(2);
        }
    }
    tide_run_headless_desc.width = tide_run_settings.width;
    tide_run_headless_desc.height = tide_run_settings.height;
    tide_run_headless_desc.argc = tide_run_settings.argc;
    tide_run_headless_desc.argv = tide_run_settings.argv;
#if defined(__wasm__) || defined(__ANDROID__)
    if (tide_run_report_path) {
        fprintf(stderr, "tide: --profile exports desktop reports; web and Android export is not supported\n");
        exit(2);
    }
    if (tide_run_data_directory) {
        fprintf(stderr, "tide: --data-dir overrides native desktop saves, not web or Android storage\n");
        exit(2);
    }
    if (tide_run_headless) {
        fprintf(stderr, "tide: --capture, --frames and --script run a game with no window, on desktop only\n");
        tide_run_headless = false;
    }
#endif
    if (tide_run_report_path) {
        tide_run_headless_desc.finished = tide_run_report_finish;
        tide_run_report_identity();
    }
}

// Opens tide_run_game's window, as `desc` and the game's settings say, or
// none for a run that asked for none.
static inline void tide_run_end(void);

// The window closed, or the game quit: the match is left (its server's
// players told, or with host migration handed over) and the local state let
// go before the program ends (tide_platform_on_close).
static inline void tide_run_close(void)
{
    if (tide_run_session) tide_run_end();
}

static inline void tide_run_open(const tide_run_desc *desc)
{
    tide_platform_on_close(tide_run_close);
    tide_run_configure(desc);
    if (tide_run_headless) {
        tide_platform_open_headless(&tide_run_headless_desc);
        return;
    }
#if !defined(__wasm__) && !defined(__ANDROID__)
    // A run nobody watches shows no window (tide_platform_open), and a game's
    // closing is all that ends its run: it says what does without one
    if (tide_hidden()) {
        fprintf(stderr, "tide: TIDE_HIDDEN is set, so the game shows no window, and nothing else would end its run: give it "
                        "--frames <n> (and --capture <file.png> for a picture of the last one), or --script <file>\n");
        exit(2);
    }
#endif
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
    // The stats list the slowest systems while their drawer is open
    const bool listed = tide_run_stats() && tide_run_drawer.open;
    tide_profile *profile = listed || tide_run_profiling || tide_run_reporting ? &tide_run_profiler.profile : NULL;
    tide_platform_jobs_profile(profile);
    tide_run_alone.profile = profile;
    tide_run_gui.profile = tide_run_profiling || tide_run_reporting ? profile : NULL;
}

// A session for tide_run_game, in no match yet. Its matches tick at the game's
// rate unless the host said otherwise.
static inline tide_session *tide_run_new_session(void)
{
    // A sharded game's regions, as --shards and --shard-width say: centered on 0 along x
    const tide_shard_game *sharded = tide_run_game->game->shards;
    const uint32_t regions = tide_run_shards ? tide_run_shards : 1u;
    const double width = tide_run_shard_width > 0.0 ? tide_run_shard_width : sharded ? 16.0 * sharded->range : 1.0;
    const double half = 0.5 * width * (double)regions;
    return tide_session_create(&(tide_session_desc){
        .shard_world = {{-half, -half, -half}, {half, half, half}},
        .shard_split = {regions, 1u, 1u},
        .game = tide_run_game->game,
        .tick_rate = (uint32_t)tide_run_settings.tick_rate,
        .sample = tide_run_sample,
        .jobs = tide_run_jobs(),      // Ticks on every core
        // To keep running predicted ticks again within a frame's budget. A run
        // with no window has no clock, so it goes the same on every machine.
        .now_ns = tide_run_headless ? NULL : tide_time_now_ns,
        .correct_views = tide_run_game->correct_views ? tide_run_correct_views : NULL,
        // A seed for each match it starts, so no two go the same: but the one
        // --seed says, and in a run with no window, whose matches start with
        // seed 0 without one, so it repeats.
        .entropy = tide_run_headless || tide_run_seeded ? NULL : tide_platform_entropy,
        .seed = tide_run_seed,
    });
}

// Names the game to the platform: its folders, its log, and how its pipelines are made. Again for the same game,
// nothing changes. Its ID, written in `fallback` where the game has none of its own.
static inline const char *tide_run_name_game(const tide_host_game *game, char *fallback, const size_t size)
{
    const char *id = game->game->app_id;
    if (!id) {
        const char *name = tide_run_settings.game_name ? tide_run_settings.game_name : "game";
        snprintf(fallback, size, "dev.tide.%s", name);
        for (char *p = fallback; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.'))
                *p = '_';
        id = fallback;
    }
    tide_platform_name_game(id); // Its folders: the renderer's cache too, whether the game uses files or not
    // Its log, and what a crash leaves, beside its saves: for a player's runs and the developer's, not a run with
    // no window, whose output is the test's, nor another program's, whose log is its own
    if (!tide_run_headless && !tide_run_embedded) tide_platform_log_begin(id, tide_run_settings.title, tide_run_data_directory);
    // A game's frames don't wait for a GPU pipeline to be made: what's drawn with one shows once it's made. Not in
    // a run with no window, whose frames are read back and go the same every time
    tide_platform_background_pipelines(!tide_run_headless);
    return id;
}

// Bind local files before startup handlers or restored tasks can use them.
static inline void tide_run_bind_io(const tide_host_game *game, void *local)
{
    char fallback[512];
    const char *id = tide_run_name_game(game, fallback, sizeof fallback);
    if (!game->set_io) return;
    const char *directory = tide_run_data_directory;
    char *player_directory = NULL;
    if (directory && tide_platform_players() > 1u) {
        const size_t size = strlen(directory) + 32u;
        player_directory = tide_alloc(size);
        snprintf(player_directory, size, "%s/player-%u", directory, (unsigned)tide_platform_player());
        directory = player_directory;
    }
    game->set_io(local, tide_platform_io_init(id, directory));
    free(player_directory);
}

// The pipelines of the game's shaders made before anything draws with them
// (tide_platform_prewarm), so a shader's first draw doesn't wait for one: as
// the game starts, once its folders are named (how its shaders drew in the
// last runs is read from there, and Vulkan's cache of what it compiled), and
// again every frame, which costs nothing till the table or the device is
// another (a reloaded build, a lost device) or a shader drew another way.
// Not in a run with no window, whose frames nobody waits for, nor inside
// another program, which draws.
static inline void tide_run_prewarm(void)
{
    if (tide_run_headless || tide_run_embedded || !tide_run_game->shaders) return;
    uint32_t count = 0;
    while (tide_run_game->shaders[count]) count++;
    tide_platform_prewarm(tide_run_game->shaders, count);
}

// Starts tide_run_game: its local state and a session, then the match the
// command line asks for, or Main's if it's the match's.
static inline void tide_run_begin(void)
{
    const tide_host_game *game = tide_run_game;
#ifdef __wasm__
    tide_platform_startup_ready(game->main_is_local);
#endif
    tide_run_local = calloc(1, game->local_size);
    tide_run_start = calloc(1, game->game->start_size);
    if (!tide_run_local || !tide_run_start) abort();
    tide_run_bind_io(game, tide_run_local);
    tide_run_prewarm();
    game->local_init(tide_run_local);
    tide_run_session = tide_run_new_session();
    tide_session_request request = {0};
    bool asked = tide_run_arguments(&request);
    if (tide_platform_players() && !game->main_is_local) {
        // A run of several players: the first opens Main's match, as --host would, and the others join its room
        asked = true;
        request = (tide_session_request){.kind = TIDE_REQUEST_OPEN, .port = TIDE_DEFAULT_PORT};
        if (tide_platform_player() != 1u) {
            request.kind = TIDE_REQUEST_JOIN;
            tide_platform_players_room(1u, request.text, sizeof request.text);
        }
    }
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

// A game's pack can take seconds to read and check (a big game's is
// gigabytes), so in a window it loads on a thread of the pool's while the
// frames show how far it got, as a page does while it fetches one (tide.js):
// the window answers meanwhile, and isn't blank. The game starts once it's in
// (tide_run_loading_frame). A run with no window loads it in place, as nobody
// waits on one and it goes the same every time; so does a page, which has the
// pack already, and a machine whose pool has no background threads.
typedef struct tide_run_loader {
    tide_background job;
    char *path;           // The file read, or NULL: the program's own pack (tide_platform_pack_counted)
    uint64_t progress[2]; // Bytes read so far, and the file's, as the reading thread stores them
    uint32_t checking;    // 1 once it's read and the game is loading it (atomic)
    uint8_t *bytes;       // What was read, and whether the game loaded it: the thread's until the job's done
    size_t size;
    bool loaded;
    bool read;      // A pack is read on the pool: none, where the game has no pack or it was read in place
    bool on;        // Frames show where it is, until it's in and the game has begun
    bool named;     // The platform knows the game by its ID (tide_run_name_game), from the loader's first frame
    bool warming;   // It's in, and the game's pipelines are being made (tide_run_prewarm)...
    uint32_t warm_total; // ...this many, the most that were left at once
    double seconds; // How long they've shown it, for the bar that moves while nothing can say how far
    tide_draw_list list;
} tide_run_loader;
static tide_run_loader tide_run_loading;

static inline void tide_run_load_work(void *context)
{
    tide_run_loader *l = context;
    size_t size = 0;
    uint8_t *bytes = l->path ? tide_platform_read_counted(l->path, &size, l->progress)
                             : tide_platform_pack_counted(&size, l->progress);
    __atomic_store_n(&l->checking, 1u, __ATOMIC_RELAXED);
    l->loaded = !bytes || tide_run_game->assets(bytes, size);
    l->bytes = bytes;
    l->size = size;
}

// Starts loading the pack at `path` (NULL: the program's own) on the pool, if
// this run loads it there: false where it's to be loaded in place.
static inline bool tide_run_load_shown(const char *path)
{
#ifdef __wasm__
    (void)path;
    return false;
#else
    const tide_jobs *pool = tide_platform_jobs();
    if (!tide_run_game->assets || tide_run_headless || tide_run_embedded || !pool || !pool->background) return false;
    tide_run_loader *l = &tide_run_loading;
    *l = (tide_run_loader){.job = {.work = tide_run_load_work, .context = l}, .read = true, .on = true};
    if (path) {
        const size_t n = strlen(path) + 1u;
        l->path = malloc(n);
        if (!l->path) abort();
        memcpy(l->path, path, n);
    }
    pool->background(pool->self, &l->job);
    return true;
#endif
}

// Starts the game once its pack is in, unless it's loading on the pool, and its pipelines are made: the loading
// frames wait for them in a window, as they do once a pack is read (tide_run_loading_frame).
static inline void tide_run_begin_loaded(void)
{
#ifdef __wasm__
    tide_platform_startup_ready(false);
#endif
    tide_run_loader *l = &tide_run_loading;
    if (l->on) return;
    if (tide_run_headless || tide_run_embedded || !tide_run_game->shaders) {
        tide_run_begin();
        return;
    }
    *l = (tide_run_loader){.loaded = true, .on = true};
    l->job.done = 1u; // Nothing to read: the first frame makes the pipelines
}

// A frame while the pack loads: the loading screen, as the web's shell has it
// (platform/web/shell.html), with how much of the pack is read, then a bar
// that moves while the game checks what it read. Once it's in, the game's
// pipelines are made (tide_run_prewarm), which takes a minute or more where
// the driver has none of them yet: where the backend makes them in the
// background, the frames go on saying how many are made, and the game begins
// once all are, so its first frames draw everything. This frame is then the
// game's first.
static inline int tide_run_frame(void *user, float seconds);
static inline int tide_run_loading_frame(void *user, const float seconds)
{
    tide_run_loader *l = &tide_run_loading;
    // What's pressed meanwhile isn't the game's: it starts from what's held
    tide_platform_poll(&tide_run_devices);
    tide_devices_consume(&tide_run_devices);
    if (!l->named) {
        // Before the first frame draws: the renderer then reads this game's file of compiled pipelines, in the
        // background, where it would read the one of the folder a game with no name yet has, and then this one
        char fallback[512];
        (void)tide_run_name_game(tide_run_game, fallback, sizeof fallback);
        l->named = true;
    }
    if (!l->warming && __atomic_load_n(&l->job.done, __ATOMIC_ACQUIRE)) {
        if (l->read && !l->bytes && !l->path) {
            fprintf(stderr, "tide: the game's pack of assets isn't with the program (<program>.pack beside it): they read as empty\n");
        }
        free(l->path);
        l->path = NULL;
        if (l->bytes) tide_run_assets_kept(l->bytes, l->size, l->loaded);
        char fallback[512];
        (void)tide_run_name_game(tide_run_game, fallback, sizeof fallback); // How its shaders drew before, read first
        tide_run_prewarm();
        l->warming = true;
    }
    // A big game's pipelines are asked for a few milliseconds' worth a frame (tide_platform_prewarm): each frame
    // goes on from the last, and nothing is waited for once all are asked for and made
    if (l->warming) tide_run_prewarm();
    const uint32_t waiting = l->warming ? tide_platform_prewarm_waiting() : 0u;
    if (l->warming && !waiting) {
        l->on = false;
        tide_draw_free(&l->list);
        tide_run_begin();
        return tide_run_frame(user, seconds);
    }
    if (waiting > l->warm_total) l->warm_total = waiting;
    l->seconds += tide_is_finite_d((double)seconds) && seconds > 0.0f ? (double)seconds : 0.0;
    const uint64_t read = l->warming ? l->warm_total - waiting : __atomic_load_n(&l->progress[0], __ATOMIC_RELAXED);
    const uint64_t total = l->warming ? l->warm_total : __atomic_load_n(&l->progress[1], __ATOMIC_RELAXED);
    const bool known = l->warming || (!__atomic_load_n(&l->checking, __ATOMIC_RELAXED) && total);
    char text[64];
    if (l->warming) {
        snprintf(text, sizeof text, "Compiling shaders %u / %u", (unsigned)read, (unsigned)total);
    } else if (known) {
        const double mb = 1048576.0;
        const int places = total < 10u * 1048576u ? 1 : 0;
        snprintf(text, sizeof text, "Loading %.*f / %.*f MB", places, (double)read / mb, places, (double)total / mb);
    } else {
        snprintf(text, sizeof text, "%s", total ? "Preparing" : "Loading");
    }

    const tide_float2 screen = tide_platform_screen_size();
    const tide_float2 measured = tide_platform_measure_text(text, 14.0f, NULL, TIDE_FONT_REGULAR);
    // The bar, then the text 16 pixels under it, the two in the middle of the window
    const float bar_w = 220.0f, bar_h = 4.0f, gap = 16.0f;
    const float top = floorf((screen.y - (bar_h + gap + measured.y)) * 0.5f);
    const float left = floorf((screen.x - bar_w) * 0.5f);
    float from = 0.0f, width = 0.0f; // The bar's filled part, from its left
    if (known) {
        width = bar_w * (float)((double)read / (double)total);
    } else { // A third of it going side to side, easing in and out, every 1.2 seconds
        const double phase = fmod(l->seconds / 1.2, 2.0);
        const float t = (float)(phase < 1.0 ? phase : 2.0 - phase);
        width = bar_w * 0.3f;
        from = (bar_w - width) * t * t * (3.0f - 2.0f * t);
    }
    tide_draw_list *d = &l->list;
    tide_draw_reset(d);
    d->target_pixels = tide_platform_target_pixels;
    d->target_content = tide_platform_target_content;
    d->screen_scale = tide_platform_screen_scale();
    tide_draw_clear(d, (tide_color){14.0f / 255.0f, 14.0f / 255.0f, 20.0f / 255.0f, 1.0f});
    tide_draw_set_screen(d);
    tide_draw_round_rect(d, tide_f2(left + bar_w * 0.5f, top + bar_h * 0.5f), tide_f2(bar_w, bar_h), bar_h * 0.5f,
                         (tide_color){36.0f / 255.0f, 36.0f / 255.0f, 46.0f / 255.0f, 1.0f});
    if (width >= 1.0f) {
        tide_draw_round_rect(d, tide_f2(left + from + width * 0.5f, top + bar_h * 0.5f), tide_f2(width, bar_h), bar_h * 0.5f,
                             (tide_color){106.0f / 255.0f, 140.0f / 255.0f, 1.0f, 1.0f});
    }
    tide_draw_text(d, text, tide_f2(floorf((screen.x - measured.x) * 0.5f), top + bar_h + gap), 14.0f,
                   (tide_color){154.0f / 255.0f, 154.0f / 255.0f, 174.0f / 255.0f, 1.0f});
    tide_platform_draw(d);
    return TIDE_KEEP_RUNNING;
}

// Ends what tide_run_begin started: the match, if any, and the local state.
static inline void tide_run_end(void)
{
    if (tide_run_game->voice) {
        const tide_voice_provider *voice = tide_run_game->voice();
        if (voice && voice->stop) {
            tide_platform_audio_forget();
            voice->stop(voice->self, tide_run_session);
        }
    }
    tide_run_hosting_leave();
    const bool open = tide_run_drawer.open;
    tide_stats_free(&tide_run_drawer);
    tide_run_drawer.open = open;
    tide_platform_gpu_timing(false);
    tide_run_timing = false;
    tide_session_destroy(tide_run_session);
    tide_run_session = NULL;
    tide_network_match_leave(&tide_run_network);
    if (tide_run_local) tide_run_game->local_free(tide_run_local);
    free(tide_run_local);
    free(tide_run_start);
    tide_run_local = NULL;
    tide_run_start = NULL;
    tide_run_dropped = false;
    tide_run_quit = false;
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
    // A match on the game's own network meets again where that network says
    if (tide_run_network.on) {
        tide_run_migrating_since = 0.0;
        tide_network_match_after(&tide_run_network, s, tide_run_now);
        return;
    }
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
        tide_session_join(s, tide_run_conditioned(network), server, tide_run_now);
    } else if (moved == -2) {
        tide_session_fail(s, TIDE_DISCONNECT_ENDED); // Its host ended it: the goodbye was lost
    } else if (moved < 0 || tide_run_now - tide_run_migrating_since > 30.0) {
        tide_session_fail(s, TIDE_DISCONNECT_TIMED_OUT);
    }
}

// Each channel's commands local code sent into the session, in order, until a
// channel is full there: the others go on, as a channel waits for nothing else
static inline void tide_run_send_commands(void)
{
    const tide_host_game *game = tide_run_game;
    if (!game->command || !game->command_sent) return;
    for (uint32_t i = 0; i <= tide_game_command_channels(game->game); i++) {
        const uint32_t channel = tide_run_command_channel(i);
        const uint8_t *bytes;
        uint32_t size;
        while ((bytes = game->command(tide_run_local, channel, &size)) != NULL) {
            if (!tide_session_send_command(tide_run_session, bytes, size)) break;
            game->command_sent(tide_run_local, channel);
        }
    }
}

// Only between complete frames: no views, GPU calls, reload, session requests
// or local event handlers run here. The last polled devices keep held input;
// sampling consumes their edges once, as it does in an ordinary frame.
static inline double tide_run_wait_update(void *user, const double elapsed)
{
    (void)user;
    tide_session *s = tide_run_session;
    if (!s) return 1.0 / 60.0;
    const double at = tide_run_frame_now + elapsed;
    if (at > tide_run_now && tide_is_finite_d(at)) {
        const uint64_t began = tide_run_timing || tide_run_profiling ? tide_time_now_ns() : 0;
        const float seconds = (float)(at - tide_run_now);
        tide_run_now = at;
        if (tide_run_game->age_views) tide_run_game->age_views(tide_run_local, seconds);
        tide_network_match_update(&tide_run_network, tide_run_registered());
        tide_session_update(s, tide_run_now);
        if (began) {
            const uint64_t work = tide_time_now_ns() - began;
            if (tide_run_timing) tide_run_stats_work += work;
            if (tide_run_profiling) tide_run_profiler.part_ms[TIDE_FRAME_MATCH] += (double)work * 1e-6;
        }
    }
    return tide_session_until_tick(s);
}

// The machine slept (tide_platform_slept): it was gone from its match all the
// while, as a phone that went to the background is, so it leaves a match
// other machines can be in, as Session.Leave does: one it joined, one it was
// moving to, or one it runs that's open or has others in it. Their match goes
// on without it, through host migration where the game has it. A match it
// runs alone and closed (single-player) goes on where it stopped, as nobody
// waited for it.
static inline void tide_run_slept(void)
{
    const tide_session *s = tide_run_session;
    const tide_session_status status = tide_session_status_of(s);
    char code[TIDE_ROOM_CODE_LENGTH + 1], key[TIDE_ROOM_KEY_LENGTH + 1];
    const bool others = status.server ? status.open || tide_session_remote_players(s) > 0
                                      : status.client.state != TIDE_SESSION_OFFLINE || tide_session_migrating(s, code, key);
    if (others || tide_run_hosting) tide_run_request(&(tide_session_request){.kind = TIDE_REQUEST_LEAVE}, NULL);
}

// Whether the player types, once a frame: the system's keyboard shows where
// there is one (tide_platform_typing), and the game's own is told once each
// time it changes (tide/screen_keyboard.h). One registered while the player
// types, or by a new build, is told at once. Runs with no window, and runs
// nobody watches, show neither.
static tide_screen_keyboard tide_run_screen_keyboard_was; // The game's keyboard as of the last frame ({0} for none)...
static bool tide_run_screen_keyboard_typing;              // ...and what it was told

static inline void tide_run_typing(const tide_host_game *game, const bool typing)
{
    tide_platform_typing(typing);
    const bool shown = !tide_run_headless && !tide_run_embedded && !tide_hidden();
    const tide_screen_keyboard *k = shown && game->screen_keyboard ? game->screen_keyboard() : NULL;
    const tide_screen_keyboard now = k ? *k : (tide_screen_keyboard){0};
    // One that's new to the host was told nothing
    const tide_screen_keyboard was = tide_run_screen_keyboard_was;
    if (now.self != was.self || now.typing != was.typing) tide_run_screen_keyboard_typing = false;
    tide_run_screen_keyboard_was = now;
    if (!now.typing || typing == tide_run_screen_keyboard_typing) return;
    tide_run_screen_keyboard_typing = typing;
    now.typing(now.self, typing);
}

static inline int tide_run_frame(void *user, const float seconds)
{
    if (tide_run_loading.on) return tide_run_loading_frame(user, seconds); // The game's pack isn't in yet
    const tide_host_game *game = tide_run_game;
    // Asked every frame: a reload can bring the setting, or take it away. Inside another program's frame they're
    // drawn with the frame, as the views' drawing is, and say only what's Tide's
    const bool stats = tide_run_stats();
    // What a frame's work takes is in the stats' list: timed while their drawer is open
    const uint64_t began = stats && tide_run_drawer.open ? tide_time_now_ns() : 0;
    // ...and for a page that measures the netcode, with its session's update apart
    const bool measured_netcode = tide_platform_netcode_wanted();
    const uint64_t frame_began = measured_netcode ? tide_time_now_ns() : 0;
    tide_run_reporting = tide_run_report_path && ++tide_run_report_frames > tide_run_report_warmup;
    tide_run_debug_views();
    if (tide_run_reporting) tide_profile_report_begin(&tide_run_report, &tide_run_profiler.profile, tide_run_report_frames);
    // Where the frame's time goes, part by part, while the profiler is shown
    tide_profiler *profiler = tide_run_profiling ? &tide_run_profiler : NULL;
    tide_profiler_begin(profiler, seconds);
    tide_platform_poll(&tide_run_devices);
    if (tide_run_input) tide_run_input(&tide_run_devices);
    tide_run_drawer_input(stats);
    const bool timing = stats && tide_run_drawer.open;
    if (timing != tide_run_timing) {
        tide_run_reset_timing(timing);
        tide_run_timing = timing;
    }
    if (tide_run_reporting && !tide_run_report_started) {
        // Discard any timer results belonging to warmup, including ones the
        // stats drawer requested. Pending measured results stay asynchronous.
        tide_platform_gpu_timing(false);
        tide_run_report_started = true;
    }
    if (!tide_run_embedded) tide_platform_gpu_timing(timing || tide_run_reporting);
    // F8 may have changed the drawer after debug_views selected a profile.
    // Bind it before ticks, so a closing press stops measuring at once.
    tide_profile *measured = timing || tide_run_profiling || tide_run_reporting ? &tide_run_profiler.profile : NULL;
    tide_platform_jobs_profile(measured);
    tide_run_alone.profile = measured;
    tide_run_frame_now += seconds;
    const float update_seconds = tide_run_frame_now > tide_run_now ? (float)(tide_run_frame_now - tide_run_now) : 0.0f;
    if (tide_run_frame_now > tide_run_now) tide_run_now = tide_run_frame_now;
    tide_profiler_mark(profiler, TIDE_FRAME_INPUT);
    if (tide_run_reporting) tide_profile_report_mark(&tide_run_report, TIDE_FRAME_INPUT);
    // A script's leave and rejoin, in a run of several players, as the frame's poll had them
    for (tide_players_action action; (action = tide_platform_players_action()) != TIDE_PLAYERS_NONE;) {
        const tide_session_request again = tide_run_joined;
        if (action == TIDE_PLAYERS_LEAVE) tide_run_request(&(tide_session_request){.kind = TIDE_REQUEST_LEAVE}, NULL);
        else if (again.kind) tide_run_request(&again, NULL);
        else fprintf(stderr, "tide: player %u has nothing to rejoin: it never joined a match\n", (unsigned)tide_platform_player());
    }
    // Before the session's update, which would otherwise go on as if no time had passed
    if (!tide_run_embedded && tide_platform_slept()) tide_run_slept();

    tide_session *s = tide_run_session;
    // Existing visual errors age before this update adds fresh corrections.
    if (game->age_views) game->age_views(tide_run_local, update_seconds);
    game->frame_time(tide_run_local, seconds);
    // A room joined that isn't one, or whose host can't be reached, fails now
    // rather than timing out.
    if (tide_platform_room_failed() || tide_platform_players_failed()) tide_session_fail(s, TIDE_DISCONNECT_FAILED);
    // What the match's ticks send, for the game's local handlers: asked for
    // before the update, so the session records from its first tick
    tide_event_log *heard = game->events ? tide_session_events(s) : NULL;
    tide_network_match_update(&tide_run_network, tide_run_registered());
    if (tide_run_hosting) {
        tide_hosting_client_update(tide_run_hosting, tide_run_now);
        const tide_hosting_result result = tide_hosting_client_result(tide_run_hosting);
        if (result == TIDE_HOSTING_ACCEPTED && !tide_run_hosting_joined) {
            const uint64_t match = tide_hosting_client_match(tide_run_hosting);
            fprintf(stderr, "tide: joining dedicated match %llu\n", (unsigned long long)match);
            tide_session_join(s, tide_hosting_client_transport(tide_run_hosting),
                              tide_hosting_client_address(tide_run_hosting), tide_run_now);
            tide_run_hosting_joined = true;
        } else if (result != TIDE_HOSTING_PENDING && result != TIDE_HOSTING_ACCEPTED) {
            const tide_disconnect_reason disconnected = tide_run_hosting_joined && result == TIDE_HOSTING_NOT_FOUND
                ? TIDE_DISCONNECT_ENDED : result == TIDE_HOSTING_TIMED_OUT ? TIDE_DISCONNECT_TIMED_OUT
                : result == TIDE_HOSTING_DENIED || result == TIDE_HOSTING_FULL || result == TIDE_HOSTING_WRONG_VERSION ? TIDE_DISCONNECT_REFUSED
                : TIDE_DISCONNECT_FAILED;
            const char *reason = disconnected == TIDE_DISCONNECT_ENDED ? "match ended"
                               : result == TIDE_HOSTING_WRONG_VERSION ? "different simulation; update the client to this game's release"
                               : result == TIDE_HOSTING_DENIED ? "permission denied"
                               : result == TIDE_HOSTING_FULL ? "server capacity reached; try again later"
                               : result == TIDE_HOSTING_NOT_FOUND ? "match ID not found; check the address"
                               : result == TIDE_HOSTING_TIMED_OUT ? "no admission answer; check the server address"
                               : "invalid admission request";
            fprintf(stderr, "tide: dedicated server: %s\n", reason);
            tide_run_hosting_leave();
            tide_session_fail_message(s, disconnected, reason);
        }
    }
    const uint64_t update_began = measured_netcode ? tide_time_now_ns() : 0;
    tide_session_update(s, tide_run_now);
    const uint64_t update_ns = measured_netcode ? tide_time_now_ns() - update_began : 0;
    tide_run_migrate();
    tide_session_event event;
    while (tide_session_next_event(s, &event)) {
        const bool connected = event.kind == TIDE_SESSION_CONNECTED_EVENT;
        tide_run_dropped = !connected && event.reason != TIDE_DISCONNECT_LEFT && event.reason != TIDE_DISCONNECT_FAILED &&
                           event.reason != TIDE_DISCONNECT_ENDED && event.reason != TIDE_DISCONNECT_KICKED &&
                           event.reason != TIDE_DISCONNECT_BANNED;
        if (connected) game->connected(tide_run_local);
        else game->disconnected(tide_run_local, event.reason, event.message);
    }
    const tide_session_status status = tide_session_status_of(s);
    // What a page that plays at a slow connection makes up the rest of (tide.js: `Tide.lag`), once
    // this machine is in the match: the round trips of joining one aren't like a match's
    const bool in_match = !status.server && status.client.state == TIDE_SESSION_CONNECTED;
    tide_platform_room_ping(in_match ? (int32_t)status.client.ping_ms : -1, status.client.answer_ms);
    char room[TIDE_NETWORK_ADDRESS_BYTES] = "";
    if (status.open || !status.server) { // A closed room isn't one to join
        if (tide_run_hosting && tide_run_hosting_joined)
            snprintf(room, sizeof room, "%s/%llu", tide_run_hosting_endpoint,
                     (unsigned long long)tide_hosting_client_match(tide_run_hosting));
        else if (tide_run_network.on) tide_network_match_address(&tide_run_network, room);
        else if (tide_platform_players()) tide_platform_players_code(room, sizeof room);
        else tide_platform_room_code(room, sizeof room);
    }
    const uint32_t session_state = tide_run_hosting && !tide_run_hosting_joined ? TIDE_SESSION_CONNECTING : status.client.state;
    game->set_session(tide_run_local, session_state, status.client.player, status.client.ping_ms, status.server,
                      status.open, room);
    if (game->set_invite) { // A link to this page on the web, the room's code elsewhere
        char invite[TIDE_INVITE_BYTES];
        tide_platform_room_invite(room, invite, sizeof invite);
        game->set_invite(tide_run_local, invite);
    }
    memcpy(tide_run_room, room, sizeof tide_run_room);
    tide_profiler_mark(profiler, TIDE_FRAME_MATCH);
    if (tide_run_reporting) tide_profile_report_mark(&tide_run_report, TIDE_FRAME_MATCH);

    // Views draw at the frame rate, the match blended between its last two ticks
    tide_view_worlds view = tide_session_view(s);
    if (tide_run_unblended) {
        view.previous = view.verified_previous = NULL;
        view.alpha = view.verified_alpha = 1.0f;
    }
    const void *match = view.current;
#ifdef __wasm__
    // A failed initial connection must still show local error/retry UI without a match view.
    const bool failed_start = status.client.state == TIDE_SESSION_OFFLINE && status.client.reason != TIDE_DISCONNECT_LEFT;
    tide_platform_startup_ready(game->main_is_local || match != NULL || failed_start);
#endif
    tide_draw_reset(&tide_run_draw);
    tide_run_draw.diagnostics = tide_run_reporting ? &tide_run_report.draw : NULL;
    tide_draw_screen_size(&tide_run_draw, tide_platform_screen_size()); // What a mesh's levels of detail are picked by
    tide_run_draw.target_pixels = tide_platform_target_pixels;
    tide_run_draw.target_content = tide_platform_target_content;
    tide_run_draw.screen_scale = tide_platform_screen_scale();
    tide_run_draw.gpu_culling = tide_platform_gpu_culling();
    if (tide_run_view_camera) {
        tide_draw_set_camera_matrices(&tide_run_draw, tide_run_view_camera_transform, tide_run_view_camera_projection);
    }
    tide_audio_reset(&tide_run_audio);
    // The GUI and the views read the devices, less a press that's on the stats
    const tide_devices *seen = &tide_run_devices;
    tide_devices kept;
    if (tide_stats_pressed(&tide_run_drawer)) {
        kept = tide_run_devices;
        tide_stats_hide(&tide_run_drawer, &kept);
        seen = &kept;
    }
    tide_gui_begin(&tide_run_gui, seen, tide_platform_screen_size(), tide_platform_measure_text);
    tide_run_gui.copy = tide_platform_copy;
    tide_run_gui.jobs = tide_platform_jobs();
    tide_platform_list_controls(&tide_run_gui);
    // What the match sent since the last frame: its local handlers run first
    if (heard) game->events(tide_run_local, heard);
    if (game->audio) game->audio(tide_run_local, &tide_run_audio);
    if (game->frame_shards) { // A sharded match's other shards this machine follows, drawn beside its home's
        tide_view_worlds more[TIDE_SESSION_VIEWS];
        uint32_t shards[TIDE_SESSION_VIEWS];
        const uint32_t n = tide_session_views(s, more, shards, TIDE_SESSION_VIEWS);
        game->frame_shards(more, n < TIDE_SESSION_VIEWS ? n : TIDE_SESSION_VIEWS);
    }
    game->frame(match, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, tide_run_local, &tide_run_draw, &tide_run_gui);
    if (game->voice) {
        const tide_voice_provider *voice = game->voice();
        if (voice) voice->frame(voice->self, s, &tide_run_audio, tide_run_now);
    }
    tide_session_mark_presented(s);
    tide_gui_end(&tide_run_gui, &tide_run_draw);
    tide_profiler_mark(profiler, TIDE_FRAME_VIEWS);
    if (tide_run_reporting) tide_profile_report_mark(&tide_run_report, TIDE_FRAME_VIEWS);
    tide_profiler_draw(profiler, &tide_run_draw, tide_platform_screen_size(), tide_platform_measure_text);
    tide_run_typing(game, tide_gui_typing(&tide_run_gui));
    tide_platform_cursor_lock(tide_gui_cursor_lock(&tide_run_gui)); // The next poll says whether the window holds it
    if (timing && tide_run_draw.epoch != tide_run_timing_epoch) {
        // A retained native reload keeps the host, but the samples are the old build's.
        tide_run_reset_timing(true);
        tide_platform_gpu_timing(true);
    }
    tide_platform_gpu_profile(tide_run_reporting ? tide_profile_report_gpu_pass : NULL,
                              tide_run_reporting ? &tide_run_report : NULL);
    const uint64_t render_began = timing ? tide_time_now_ns() : 0;
    tide_run_prewarm();
    if (!tide_run_embedded) tide_platform_draw(&tide_run_draw);
    tide_platform_audio(&tide_run_audio); // Embedded, for the other program to play (tide_embed_audio)
    const uint64_t rendered = timing ? tide_time_now_ns() : 0;
    tide_profiler_mark(profiler, TIDE_FRAME_RENDER);
    if (tide_run_reporting) {
        tide_profile_report_mark(&tide_run_report, TIDE_FRAME_RENDER);
        if (!tide_run_embedded) { // The renderer's parts of it
            double parts[TIDE_RENDER_PARTS];
            tide_platform_render_parts(parts);
            tide_profile_report_render(&tide_run_report, parts);
        }
    }
    if (measured_netcode) tide_run_netcode(&status, update_ns, tide_time_now_ns() - frame_began);

    if (tide_run_reporting) tide_platform_gpu_profile_read();
    if (timing || tide_run_reporting) {
        for (uint32_t i = 0; i < 4; i++) {
            const double ms = tide_platform_gpu_ms();
            if (ms < 0.0) break;
            if (tide_run_reporting) tide_profile_report_gpu(&tide_run_report, ms);
            if (timing) {
                tide_run_stats_gpu += ms;
                tide_run_stats_gpu_samples++;
            }
        }
    }
    if (timing) {
        if (began) {
            tide_run_stats_work += rendered - began;
            tide_run_stats_worked++;
        }
        tide_run_stats_render += rendered - render_began;
        tide_run_stats_rendered++;
        tide_stats_collect(&tide_run_drawer, match ? &tide_run_profiler.profile : NULL, seconds);
        tide_run_timing_span += seconds;
        if (tide_run_timing_span >= 0.5) {
            if (tide_run_stats_worked) tide_run_work_ms = (double)tide_run_stats_work * 1e-6 / tide_run_stats_worked;
            tide_run_render_ms = (double)tide_run_stats_render * 1e-6 / tide_run_stats_rendered;
            tide_run_gpu_ms = tide_run_stats_gpu_samples ? tide_run_stats_gpu / tide_run_stats_gpu_samples : -1.0;
            tide_run_stats_work = tide_run_stats_render = 0;
            tide_run_stats_worked = tide_run_stats_rendered = tide_run_stats_gpu_samples = 0;
            tide_run_stats_gpu = tide_run_timing_span = 0.0;
        }
    }

    if (stats) {
        tide_run_stats_frames++;
        const double span = tide_run_now - tide_run_stats_since;
        // Once a second, and the first time sooner, so the stats aren't long in showing
        if (span >= (tide_run_stats_known ? 1.0 : 0.25)) {
            const uint64_t sent = status.sent_bytes, received = status.received_bytes;
            tide_run_fps = tide_run_stats_frames / span;
            tide_run_up = sent >= tide_run_stats_sent ? (double)(sent - tide_run_stats_sent) / span : 0.0;
            tide_run_down = received >= tide_run_stats_received ? (double)(received - tide_run_stats_received) / span : 0.0;
            if (tide_run_drawer.open) tide_run_memory = tide_platform_memory();
            tide_run_stats_known = true;
            tide_run_stats_since = tide_run_now;
            tide_run_stats_frames = 0;
            tide_run_stats_sent = sent;
            tide_run_stats_received = received;
        }
    } else {
        tide_run_stats_sent = status.sent_bytes;
        tide_run_stats_received = status.received_bytes;
    }
    if (stats && tide_run_stats_known) {
        // Closed, the drawer shows the frame rate and the ping: what's only in its list isn't asked for
        const bool open = tide_run_drawer.open;
        const tide_jobs *jobs = tide_platform_jobs(); // What the session ticks on
        const tide_stats_values values = {
            .fps = tide_run_fps,
            .work_ms = tide_run_work_ms,
            .render_ms = tide_run_render_ms,
            .gpu_ms = tide_run_gpu_ms,
            .render_known = tide_run_render_ms >= 0.0,
            .gpu_known = tide_run_gpu_ms >= 0.0,
            .threads = jobs ? jobs->threads : 1u,
            .elsewhere = tide_run_embedded,
            .memory = tide_run_embedded ? 0u : tide_run_memory, // Embedded, the process is the other program's
            .gpu_memory = tide_platform_gpu_memory(),
            .renderer = tide_platform_renderer(),
            .draw_calls = tide_platform_draw_calls(),
            .vertices = tide_platform_draw_vertices(),
            .match = match != NULL,
            .remote = match && !status.server,
            .ping_ms = status.client.ping_ms,
            .delay_ticks = status.client.delay_ticks,
            .up = tide_run_up,
            .down = tide_run_down,
            .resyncs = status.client.resyncs,
            .repairs = status.client.repairs,
            .world_bytes = status.client.world_bytes,
            .entities = open && match ? game->entity_count(match) : 0u,
            .profile = &tide_run_profiler.profile,
        };
        if (tide_run_embedded) {
            // Over the views' drawing, in the list the other program draws (tide_embed_draw)
            tide_stats_draw(&tide_run_drawer, &values, &tide_run_draw, tide_platform_screen_size(), tide_platform_measure_text);
        } else {
            tide_draw_reset(&tide_run_over);
            tide_stats_draw(&tide_run_drawer, &values, &tide_run_over, tide_platform_screen_size(), tide_platform_measure_text);
            tide_platform_draw_over(&tide_run_over);
        }
    }

    // The devices keep what happened for the next tick's sample: a key that
    // went down and up between two ticks, how far the mouse moved. Where no
    // tick can sample, that's this frame's alone: in a game with no input, and
    // while this machine is in no match, or isn't in it yet (connecting, or
    // finding one that changed hands). A match that starts later reads what's
    // held as it starts, never what was pressed before it.
    if (!game->sample_input || status.client.state != TIDE_SESSION_CONNECTED) tide_devices_consume(&tide_run_devices);

    tide_run_send_commands();
    tide_session_request request;
    while (game->take_request(tide_run_local, &request, tide_run_start)) tide_run_request(&request, tide_run_start);
    // While nobody sees the window, the next frame is the match's next tick
    tide_platform_next_frame(tide_session_until_tick(s));
    if (profiler) { // Its memory row: the program's once a second, as the drawer has it, and the renderer's count
        if (tide_run_memory_at < 0.0 || tide_run_now - tide_run_memory_at >= 1.0) {
            tide_run_memory = tide_platform_memory();
            tide_run_memory_at = tide_run_now;
        }
        profiler->memory = tide_run_memory;
        profiler->gpu_memory = tide_platform_gpu_memory();
    }
    if (tide_run_reporting) {
        tide_profile_report_end(&tide_run_report, &tide_run_profiler.profile, tide_platform_draw_calls(),
                                tide_platform_draw_vertices());
        // Occlusion culling's counts, each frame's once as they come back
        static uint64_t occlusion_serial;
        tide_platform_occlusion_report occlusion;
        if (tide_platform_occlusion(&occlusion, false) && occlusion.serial != occlusion_serial) {
            occlusion_serial = occlusion.serial;
            tide_profile_report_occlusion(&tide_run_report, occlusion.tested, occlusion.hidden);
        }
    }
    tide_profiler_end(profiler, status.client.again_ticks);
    return TIDE_KEEP_RUNNING;
}

// Runs `game` in a window until it closes (see tide/run.h).
_Noreturn static inline void tide_host_run(const tide_run_desc *desc, const tide_host_game *game)
{
    tide_run_game = game;
    tide_run_open(desc);
    if (!tide_run_load_shown(NULL)) tide_run_load_assets();
    tide_run_begin_loaded();
    tide_platform_update_while_waiting(tide_run_wait_update, NULL);
    tide_platform_run(tide_run_frame, NULL);
}
