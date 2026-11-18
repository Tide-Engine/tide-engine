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

#include "tide/checkpoint.h"
#include "tide/timeline.h"
#include "tide/layout.h"
#include "tide/hosting.h"
#include "tide/network.h"
#include "tide/pack.h"
#include "tide/page.h"
#include "tide/platform.h"
#include "tide/profiler.h"
#include "tide/profile_report.h"
#include "tide/session.h"
#include "tide/sha256.h"
#include "tide/stats.h"
#include "tide/time.h"
#include "tide/tweaks.h"
#include "tide/inspect.h"
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
    // Optional: tide_debug_log_to, which a host that shows the game's Debug.Log lines itself (tide/embed.h, for the
    // program it's inside of) calls as it loads the library, whose engine copy is the one that logs.
    void (*debug_log_to)(void (*heard)(const char *text));
    // Optional: the game's `dev` views by name, NULL after them (tidec's tide_dev_view_names), which the dev menu
    // lists, and tide_show_dev_views, which the host calls before each frame with a byte for each: nonzero, it runs
    const char *const *dev_views;
    void (*show_dev_views)(const uint8_t *shown);
    // Optional: a dev build's tweaks (tide/tweaks.h, tidec's tide_game_tweaks), which the host takes the values tide run works out for
    // again as it changes and the dev menu shows and writes; NULL for none
    const struct tide_tweaks_api *(*tweaks)(void);
    // Optional: a dev build's inspector (tide/inspect.h, tidec's tide_game_inspect), which the dev menu shows the match's
    // and this machine's state with and changes them through; NULL for none
    const struct tide_inspect_api *(*inspect)(void);
} tide_host_game;

// A game's library exports this function, which returns its table.
#define TIDE_HOST_LIBRARY "tide_host_library"
// ...and this one, which returns TIDE_HOST_SHAPE as the library was built: how big the tables the host reads the
// game through are. A library another tide built, whose tables are laid out otherwise, says another number (or has
// no such function), and the host says so rather than load it and call through members that aren't where it looks.
#define TIDE_HOST_LIBRARY_SHAPE "tide_host_library_shape"
#define TIDE_HOST_SHAPE                                                                                               \
    ((uint64_t)(sizeof(tide_host_game) & 0xFFFF) | (uint64_t)(sizeof(tide_game) & 0xFFFF) << 16                       \
     | (uint64_t)((sizeof(tide_io) + sizeof(tide_gpu_reads)) & 0xFFFF) << 32 /* The read table the host shares */     \
     | (uint64_t)((sizeof(tide_layout) + 3u * sizeof(tide_layout_type) + 7u * sizeof(tide_layout_field)) & 0xFFFF) << 48)
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

// Has the game load the pack in `bytes`, where they were read: offered to the loader (tide_pack_give), which takes
// them as the pack's memory where it can. A gigabyte isn't then copied, and kept twice.
static inline bool tide_run_load_given(uint8_t *bytes, const size_t size)
{
#ifndef __wasm__ // A page's pack is fetched, with no room past its bytes
    const size_t room = tide_platform_read_room(bytes);
    if (room) tide_pack_give(bytes, size, room);
#endif
    return tide_run_game->assets(bytes, size);
}

// The pack from `bytes` (malloc'd, the host's from here on): the game loads it.
static inline void tide_run_assets_from(uint8_t *bytes, const size_t size)
{
    free(tide_run_pack); // The last one's bytes first: a pack can be gigabytes
    tide_run_pack = NULL;
    const bool loaded = !tide_run_game->assets || !bytes || tide_run_load_given(bytes, size);
    if (tide_pack_given(bytes)) bytes = NULL; // The loaded pack's own memory now
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
// --ticks-alongside: the match this machine runs has its ticks beside the frame (tide_session_desc.alongside),
// whatever the game's ticksAlongsideViews setting says; and how many of them the profile report has heard of
static bool tide_run_alongside;
static uint32_t tide_run_alongside_ended;
// The room the last frame told local code the match is in (Session.room)
static char tide_run_room[TIDE_NETWORK_ADDRESS_BYTES];

// Checkpoints (tide/checkpoint.h), a dev build's: their folder (--checkpoints; tide run gives the project's
// `checkpoints`), the one the run starts from, and starts over from (--from, then the last loaded), and what the menu
// or a script asked for, carried out as the next frame begins (tide_run_checkpoints_poll)
static const char *tide_run_checkpoints = "checkpoints";
static char tide_run_from[TIDE_CHECKPOINT_NAME];
static bool tide_run_from_force; // ...loaded with what carries over (--force)
static void *tide_run_from_world; // ...loaded already, for tide_run_begin to start from, and the player who saved it
static int32_t tide_run_from_player;
// On a page, one on its way from tide (1 to load, 2 to load what carries over), and one saved, until tide says it's kept
static int tide_run_checkpoint_coming;
static char tide_run_checkpoint_coming_name[TIDE_CHECKPOINT_NAME];
static char tide_run_checkpoint_saving[TIDE_CHECKPOINT_NAME];
static int tide_run_checkpoint_saving_kind; // ...a checkpoint (0), or a recording (1)
static int tide_run_checkpoint_want = -1; // 0 save, 1 load, 2 load what carries over
static char tide_run_checkpoint_name[TIDE_CHECKPOINT_NAME];
static char tide_run_checkpoint_said[240];  // What the last one did, for the menu
static bool tide_run_checkpoint_forceable;  // ...refused for what loading anyway gets past: the menu offers it
static char tide_run_checkpoint_refused[TIDE_CHECKPOINT_NAME];
// The folder's checkpoints, newest first, as last listed
#define TIDE_RUN_CHECKPOINTS_LISTED 64u
static char tide_run_checkpoint_names[TIDE_RUN_CHECKPOINTS_LISTED][TIDE_CHECKPOINT_NAME];
static uint32_t tide_run_checkpoint_count;
static double tide_run_checkpoints_listed_at = -1.0;

// Pausing a dev build's match, stepping it, and going back (tide/timeline.h), from the dev menu: the match this machine
// runs is recorded as it runs, the worlds of its last ticks kept (any since it began made again by replaying). Paused,
// the match gets no time but a step's, or with other machines' players in it, goes on for them while the frames hold
// the tick shown; an earlier tick shows through this machine's views as they are, with nothing they send or ask for,
// nor its devices, reaching the match; and the match can go on from the tick shown. A recording (its ticks saved as a
// file beside the checkpoints) replays in the match's place. The embedded host has its own (embed.c).
static struct {
    tide_timeline *live;          // The match's ticks...
    const tide_session *recorded; // ...of this session's match, by this build
    const tide_game *game;
    bool paused, step;
    uint32_t shown; // The tick shown while paused
    // A recording replayed in the match's place: its ticks from its first, played at its rate or stepped through
    tide_timeline *replay;
    tide_timeline_about about;
    char replay_name[TIDE_CHECKPOINT_NAME];
    uint32_t replay_shown;
    bool playing;
    double played;
    char coming[TIDE_CHECKPOINT_NAME]; // On a page, a recording on its way from tide to be replayed
    char said[240];                    // What the last save or replay did, for the menu
} tide_run_travel;
// The recordings in the checkpoints' folder, newest first, as last listed
static char tide_run_recording_names[TIDE_RUN_CHECKPOINTS_LISTED][TIDE_CHECKPOINT_NAME];
static uint32_t tide_run_recording_count;
static double tide_run_recordings_listed_at = -1.0;

// Pausing, stepping and going back (with the frame, below)
static inline const char *tide_run_travel_why(void);
static inline uint32_t tide_run_travel_first(void);
static inline uint32_t tide_run_travel_last(void);
static inline uint32_t tide_run_travel_shown(void);
static inline bool tide_run_travel_background(void);
static inline void tide_run_travel_pause(bool paused);
static inline void tide_run_travel_forward(void);
static inline void tide_run_travel_back(void);
static inline void tide_run_travel_show(uint32_t tick);
static inline void tide_run_travel_go_on(void);
static inline const void *tide_run_travel_world(void);
static inline void tide_run_travel_save(void);
static inline void tide_run_travel_replay(const char *name);
static inline void tide_run_travel_stop_replay(void);


static inline void tide_run_checkpoint_ask(const int way, const char *name)
{
    tide_run_checkpoint_want = way;
    snprintf(tide_run_checkpoint_name, sizeof tide_run_checkpoint_name, "%s", name);
}


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
    // While the frames show another tick and the match goes on (for other machines' players), this machine's player
    // lets go of everything, as nothing seen is the match as it is
    if (tide_run_travel_world()) memset(&sampled, 0, sizeof sampled);
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
    tide_run_alongside = false;
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
        } else if (strcmp(arg, "--ticks-alongside") == 0) {
            tide_run_alongside = true;
        } else if (strcmp(arg, "--paced") == 0) {
            tide_run_headless_desc.paced = true;
        } else if (strcmp(arg, "--frame-rate") == 0) {
            const char *takes = "how many frames a second a run with no window makes up, from 1 to 1000, like --frame-rate 144";
            tide_run_headless_desc.frame_rate = (uint32_t)tide_run_number(&i, arg, 1.0, 1000.0, takes);
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
        } else if (strcmp(arg, "--checkpoints") == 0) {
            tide_run_checkpoints = tide_run_value(&i, arg, "the folder a dev build's checkpoints are in, like --checkpoints checkpoints");
        } else if (strcmp(arg, "--from") == 0) {
            const char *name = tide_run_value(&i, arg, "a checkpoint's name, which the match starts from, like --from boss-fight");
            char why[200];
            if (!tide_checkpoint_name_ok(name, why, sizeof why)) {
                fprintf(stderr, "tide: --from: %s\n", why);
                exit(2);
            }
            snprintf(tide_run_from, sizeof tide_run_from, "%s", name);
        } else if (strcmp(arg, "--force") == 0) {
            tide_run_from_force = true;
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

static inline const char *tide_run_name_game(const tide_host_game *game, char *fallback, size_t size);

static inline void tide_run_open(const tide_run_desc *desc)
{
    tide_platform_on_close(tide_run_close);
    tide_run_configure(desc);
    // Named before its window opens: the GPU driver is told where the game's own cache of compiled shaders is as
    // Vulkan starts, and not after (tide_platform_driver_cache)
    char fallback[512];
    (void)tide_run_name_game(tide_run_game, fallback, sizeof fallback);
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

// The dev menu: in a dev build with `dev` views, the key under Escape (backquote, ` on a US keyboard, where Valve's
// games open their console) shows it and hides it again, and it lists those views, each one shown or not. Views are
// off until it shows them. What's shown is kept by name, so a reload keeps it. The game sees the key too.
#define TIDE_RUN_DEV_VIEWS 64
static bool tide_run_dev_open;
static bool tide_run_dev_key; // Its key was held at the last frame
static char tide_run_dev_on[TIDE_RUN_DEV_VIEWS][64]; // The views shown, by name
static uint32_t tide_run_dev_on_count;

static inline bool tide_run_dev_shown(const char *name)
{
    for (uint32_t i = 0; i < tide_run_dev_on_count; i++) {
        if (strncmp(tide_run_dev_on[i], name, sizeof tide_run_dev_on[i]) == 0) return true;
    }
    return false;
}

static inline void tide_run_dev_show(const char *name, const bool shown)
{
    for (uint32_t i = 0; i < tide_run_dev_on_count; i++) {
        if (strncmp(tide_run_dev_on[i], name, sizeof tide_run_dev_on[i]) != 0) continue;
        if (!shown) memmove(tide_run_dev_on[i], tide_run_dev_on[i + 1], (size_t)(--tide_run_dev_on_count - i) * sizeof tide_run_dev_on[i]);
        return;
    }
    if (!shown || tide_run_dev_on_count == TIDE_RUN_DEV_VIEWS) return;
    snprintf(tide_run_dev_on[tide_run_dev_on_count++], sizeof tide_run_dev_on[0], "%s", name);
}

// Before the frame: which dev views run (tide_host_game.show_dev_views)
static inline void tide_run_dev_views(const tide_host_game *game)
{
    if (!game->dev_views || !game->show_dev_views) return;
    uint8_t shown[TIDE_RUN_DEV_VIEWS] = {0};
    for (uint32_t i = 0; i < TIDE_RUN_DEV_VIEWS && game->dev_views[i]; i++) shown[i] = tide_run_dev_shown(game->dev_views[i]);
    game->show_dev_views(shown);
}

// After the frame's views, in its GUI: the menu, over them
// A dev build's tweaks (tide/tweaks.h): the values tide run works out again as a .tweaks file changes, with no C
// built (tide_tweaks_api.values), taken half a second at the most after they're written, go to this machine's copy
// and to the match (tide_tweaks_api.send). What the file last held, so the same values aren't taken twice.
static uint64_t tide_run_tweaks_seen;
static double tide_run_tweaks_at = -1.0;

static inline uint64_t tide_run_bytes_hash(const uint8_t *bytes, const size_t length)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < length; i++) h = (h ^ bytes[i]) * 0x100000001B3ull;
    return h | 1u; // Never 0, which is nothing taken yet
}

#ifdef __wasm__
// On a page, under tide run --web, which has the values from tide's server (tide.js): copies them if they fit in
// `capacity` bytes, returning their size, 0 for none. And a value the menu changed, for tide to write into its file.
__attribute__((import_module("tide"), import_name("tweaks_copy"))) uint32_t tide_web_tweaks_copy(void *to, uint32_t capacity);
__attribute__((import_module("tide"), import_name("tweak_write"))) void tide_web_tweak_write(const char *file, const char *name,
                                                                                            const char *value);
// A dev build's checkpoints (`kind` 0) and recordings (1) on a page, which tide run --web keeps in the game's folder
// (tide.js): one saved, and whether it was kept (0 not yet, 1 kept, 2 not); one asked for, and copied once it's come
// (its size, 0 while it hasn't, UINT32_MAX for one that isn't there); a kind's names, newest first, a line each, as
// last listed
__attribute__((import_module("tide"), import_name("checkpoint_save"))) void tide_web_checkpoint_save(uint32_t kind,
                                                                                                    const char *name,
                                                                                                    const void *bytes,
                                                                                                    uint32_t size);
__attribute__((import_module("tide"), import_name("checkpoint_saved"))) uint32_t tide_web_checkpoint_saved(void);
__attribute__((import_module("tide"), import_name("checkpoint_fetch"))) void tide_web_checkpoint_fetch(uint32_t kind,
                                                                                                      const char *name);
__attribute__((import_module("tide"), import_name("checkpoint_copy"))) uint32_t tide_web_checkpoint_copy(uint32_t kind,
                                                                                                        const char *name,
                                                                                                        void *to,
                                                                                                        uint32_t capacity);
__attribute__((import_module("tide"), import_name("checkpoint_list"))) uint32_t tide_web_checkpoint_list(uint32_t kind,
                                                                                                        char *to,
                                                                                                        uint32_t capacity);
#endif

static inline void tide_run_tweaks_poll(const tide_host_game *game)
{
    const tide_tweaks_api *api = game->tweaks ? game->tweaks() : NULL;
    if (!api || !api->values[0] || (tide_run_tweaks_at >= 0.0 && tide_run_now - tide_run_tweaks_at < 0.5)) return;
    tide_run_tweaks_at = tide_run_now;
    static uint8_t bytes[8192];
#ifdef __wasm__
    const size_t length = tide_web_tweaks_copy(bytes, sizeof bytes);
    if (!length || length > sizeof bytes) return;
#else
    FILE *f = fopen(api->values, "rb");
    if (!f) return;
    const size_t length = fread(bytes, 1, sizeof bytes, f);
    fclose(f);
#endif
    const uint64_t h = tide_run_bytes_hash(bytes, length);
    if (h == tide_run_tweaks_seen) return;
    const bool first = !tide_run_tweaks_seen;
    uint8_t values[4096];
    if (api->size > sizeof values || !tide_tweaks_load(api, values, bytes, length)) return; // Another build's, or half written
    tide_run_tweaks_seen = h;
    void *here = api->here(tide_run_local);
    if (memcmp(here, values, api->size) == 0) return; // As this machine has them
    memcpy(here, values, api->size);
    api->send(tide_run_local);
    if (!first) fprintf(stderr, "tide: the tweaks' new values are in\n");
}

// One value's editor in the dev menu, by its kind (tide_tweak_kind): a switch for a bool, a button that steps through
// an enum's members, a field for a number, a vector or a Color, ints, longs, bytes and doubles as the fields for ints
// and floats have them; `count` components of `size` bytes at `at`. True when it changed.
static inline bool tide_run_dev_value(const uint32_t id, const char *name, const uint32_t kind, const uint32_t count,
                                      const uint32_t size, const bool color, const char *const *members,
                                      const int32_t *member_values, const uint32_t member_count, uint8_t *at)
{
    tide_gui *g = &tide_run_gui;
    bool changed = false;
    if (kind == TIDE_TWEAK_BOOL) {
        bool v = *at != 0;
        if (tide_gui_layout_toggle(g, id, name, &v)) {
            *at = v ? 1u : 0u;
            changed = true;
        }
    } else if (kind == TIDE_TWEAK_ENUM) {
        int32_t v = 0;
        memcpy(&v, at, size < 4u ? size : 4u); // Little-endian, as every target is
        uint32_t m = 0;
        while (m < member_count && member_values[m] != v) m++;
        char label[160];
        snprintf(label, sizeof label, "%s: %s", name, m < member_count ? members[m] : "?");
        if (tide_gui_layout_button(g, id, label) && member_count) {
            const int32_t next = member_values[m < member_count ? (m + 1u) % member_count : 0u];
            memcpy(at, &next, size < 4u ? size : 4u);
            changed = true;
        }
    } else if (color) {
        changed = tide_gui_layout_color_field(g, id, name, (tide_color *)(void *)at);
    } else if (kind == TIDE_TWEAK_FLOAT) {
        if (count == 1) changed = tide_gui_layout_float_field(g, id, name, (float *)(void *)at);
        else if (count == 2) changed = tide_gui_layout_float2_field(g, id, name, (tide_float2 *)(void *)at);
        else if (count == 3) changed = tide_gui_layout_float3_field(g, id, name, (tide_float3 *)(void *)at);
        else changed = tide_gui_layout_float4_field(g, id, name, (tide_float4 *)(void *)at);
    } else {
        // A component at a time, side by side: ints, longs, bytes and doubles as the fields for ints and floats have them
        const int row = tide_gui_begin_horizontal(g, id + 7u);
        for (uint32_t k = 0; k < count; k++) {
            const char *label = k ? "" : name;
            if (kind == TIDE_TWEAK_DOUBLE) {
                double d;
                memcpy(&d, at + 8u * k, 8);
                float v = (float)d;
                if (tide_gui_layout_float_field(g, id + k, label, &v)) {
                    d = v;
                    memcpy(at + 8u * k, &d, 8);
                    changed = true;
                }
                continue;
            }
            int32_t v = 0;
            if (kind == TIDE_TWEAK_INT) memcpy(&v, at + 4u * k, 4);
            else if (kind == TIDE_TWEAK_BYTE) v = at[k];
            else {
                int64_t l;
                memcpy(&l, at + 8u * k, 8);
                v = l > INT32_MAX ? INT32_MAX : l < INT32_MIN ? INT32_MIN : (int32_t)l;
            }
            if (!tide_gui_layout_int_field(g, id + k, label, &v)) continue;
            if (kind == TIDE_TWEAK_INT) memcpy(at + 4u * k, &v, 4);
            else if (kind == TIDE_TWEAK_BYTE) at[k] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            else {
                const int64_t l = v;
                memcpy(at + 8u * k, &l, 8);
            }
            changed = true;
        }
        tide_gui_close(g, row);
    }
    return changed;
}

// The dev menu's look: quieter than a game's GUI, which it sits over. Its text a little smaller, its controls faint
// washes of white rather than blocks, fields sunk into the panel, a soft blue for what's focused or on.
#define TIDE_RUN_DEV_DIM ((tide_color){0.55f, 0.58f, 0.65f, 1.0f})   // What says more of a row, and section names
#define TIDE_RUN_DEV_FAINT ((tide_color){1.0f, 1.0f, 1.0f, 0.07f})   // The tree's guides, the tabs' rule
#define TIDE_RUN_DEV_TYPE ((tide_color){0.56f, 0.76f, 1.0f, 1.0f})   // A component's name over its fields

static inline tide_gui_theme tide_run_dev_theme(void)
{
    tide_gui_theme t = tide_gui_theme_default();
    t.text = (tide_color){0.9f, 0.91f, 0.94f, 1.0f};
    t.panel = (tide_color){0.075f, 0.08f, 0.1f, 0.96f};
    t.control = (tide_color){1.0f, 1.0f, 1.0f, 0.1f};
    t.hot = (tide_color){1.0f, 1.0f, 1.0f, 0.12f};
    t.pressed = (tide_color){1.0f, 1.0f, 1.0f, 0.04f};
    t.field = (tide_color){0.0f, 0.0f, 0.0f, 0.28f};
    t.field_hot = (tide_color){0.0f, 0.0f, 0.0f, 0.4f};
    t.accent = (tide_color){0.42f, 0.66f, 1.0f, 1.0f};
    t.radius = 5.0f;
    t.border = (tide_color){1.0f, 1.0f, 1.0f, 0.06f};
    t.border_width = 1.0f;
    t.shadow = (tide_color){0.0f, 0.0f, 0.0f, 0.45f};
    t.shadow_size = 20.0f;
    t.text_size = 14.0f;
    t.spacing = 3.0f;
    t.padding = 10.0f;
    return t;
}

// A label in a color of its own, at a size of its own
static inline void tide_run_dev_label(const char *text, const float size, const tide_color color)
{
    tide_gui *g = &tide_run_gui;
    tide_gui_theme theme = g->theme;
    theme.text = color;
    const int depth = tide_gui_begin_theme(g, theme);
    tide_gui_layout_label_sized(g, text, size);
    tide_gui_close(g, depth);
}

// A section's heading: small capitals, quiet, with room above it
static inline void tide_run_inspect_heading(const char *text)
{
    tide_gui_layout_space(&tide_run_gui, 6.0f);
    tide_run_dev_label(text, 11.0f, TIDE_RUN_DEV_DIM);
}

// A line from `a` to `b`, `width` wide with round ends
static inline void tide_run_dev_line(const tide_float2 a, const tide_float2 b, const float width, const tide_color color)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    tide_shape s = TIDE_SHAPE_WHITE;
    s.center = (tide_float2){(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    s.size = (tide_float2){sqrtf(dx * dx + dy * dy) + width, width};
    s.rotation = atan2f(dy, dx);
    s.radius = width * 0.5f;
    s.color = color;
    tide_draw_shape(&tide_run_gui.list, s);
}

// A chevron centered on `c`: pointing right, or down when what it's on is open
static inline void tide_run_dev_chevron(const tide_float2 c, const bool open, const tide_color color)
{
    const float a = 3.5f, b = 1.75f;
    if (open) {
        tide_run_dev_line((tide_float2){c.x - a, c.y - b}, (tide_float2){c.x, c.y + b}, 1.5f, color);
        tide_run_dev_line((tide_float2){c.x, c.y + b}, (tide_float2){c.x + a, c.y - b}, 1.5f, color);
    } else {
        tide_run_dev_line((tide_float2){c.x - b, c.y - a}, (tide_float2){c.x + b, c.y}, 1.5f, color);
        tide_run_dev_line((tide_float2){c.x + b, c.y}, (tide_float2){c.x - b, c.y + a}, 1.5f, color);
    }
}

// Text at `x`, centered on `middle`, in the theme's font at its size
static inline float tide_run_dev_text(const char *text, const float x, const float middle, const tide_color color)
{
    tide_gui *g = &tide_run_gui;
    const float size = g->theme.text_size;
    if (!text[0]) return 0.0f;
    tide_draw_text_at(&g->list, text, (tide_float2){x, middle - size * 0.5f}, size, color, TIDE_ANCHOR_UPPER_LEFT,
                      g->theme.font);
    return g->measure ? g->measure(text, size, NULL, g->theme.font).x : 8.0f * (float)strlen(text);
}

// The tweaks in the menu, laid out one under another: an editor each, by its kind, or a worked-out one's value.
// Changed, the copy goes to the match, and the value into its .tweaks file.
static inline void tide_run_dev_tweaks(const tide_tweaks_api *api)
{
    tide_gui *g = &tide_run_gui;
    uint8_t *values = api->here(tide_run_local);
    tide_run_inspect_heading("TWEAKS");
    for (uint32_t i = 0; i < api->count; i++) {
        const tide_tweak *t = &api->tweaks[i];
        uint8_t *at = values + t->offset;
        const uint32_t id = 0xDE710000u + i * 8u;
        bool changed = false;
        if (!t->plain) {
            // Worked out in its file from others: shown, and left to the file
            char value[160], label[320];
            tide_tweaks_format(t, values, value, sizeof value);
            snprintf(label, sizeof label, "%s = %s (worked out)", t->name, value);
            tide_gui_layout_label(g, label);
            continue;
        }
        changed = tide_run_dev_value(id, t->name, t->kind, t->count, t->size, t->color, t->members, t->member_values,
                                     t->member_count, at);
        if (!changed) continue;
        // To the match at once, and into its .tweaks file, from which tide run works out the tweaks made from it
        api->send(tide_run_local);
#ifdef __wasm__
        char value[256]; // Written by tide, which has the files
        if (tide_tweaks_format(t, values, value, sizeof value) < sizeof value) tide_web_tweak_write(t->file, t->name, value);
#else
        if (!tide_tweaks_save(t, values)) fprintf(stderr, "tide: can't write %s to %s\n", t->name, t->file);
#endif
    }
}

// The dev menu's inspector (tide/inspect.h): the match's or this machine's local world, as packets made while it's
// open, as a tree: its singletons, then its entities under their parents, each opened in place with its fields. Every
// row is there to scroll to, and as deep as the tree goes, the list scrolls sideways. A field of the match changes
// through the server, so the field shows what was sent until the match has it; the local world's changes here.
static bool tide_run_inspect_local; // The local world's, not the match's (the menu's tab)
static tide_packet tide_run_inspect_packets[2];

// What's open in the tree, by what it is, so it stays open as the world changes: an entity by its handle, a singleton
// by its section's kind and type; each in the world of its tab
typedef struct tide_run_inspect_node {
    tide_entity entity;
    uint32_t kind; // 0: an entity
    int32_t type;
    bool local;
} tide_run_inspect_node;
#define TIDE_RUN_INSPECT_OPEN 256u
static tide_run_inspect_node tide_run_inspect_open[TIDE_RUN_INSPECT_OPEN];
static uint32_t tide_run_inspect_open_count;

// An entity a field's button opened, scrolled to as its row is laid out
static tide_entity tide_run_inspect_reveal;
static uint32_t tide_run_inspect_revealing; // Frames left to find it in: the one it's asked in may be past its row
// The list's viewport this frame, and where it's scrolled to: rows past it are only room
static tide_rect tide_run_inspect_view;
static tide_float2 *tide_run_inspect_offset;

// Each frame's, from its packet: the entities' children (the first and the next, UINT32_MAX for none), each entity's
// components (from its start, as a section and a row of it), and a stack of rows to lay out
typedef struct tide_run_inspect_has {
    uint32_t part, row;
} tide_run_inspect_has;
static uint32_t *tide_run_inspect_first, *tide_run_inspect_next, *tide_run_inspect_start, *tide_run_inspect_stack;
static uint32_t tide_run_inspect_entity_capacity;
static tide_run_inspect_has *tide_run_inspect_comps;
static uint32_t tide_run_inspect_comp_capacity;
static tide_packet_part *tide_run_inspect_parts;
static uint32_t tide_run_inspect_part_capacity;
static const tide_packet *tide_run_inspect_indexed; // The packet they're of, NULL for none this frame
static const tide_layout *tide_run_inspect_layout;  // The inspector's

// A change sent to the match and not back yet: shown as sent, a second at the most
typedef struct tide_run_inspect_sent {
    tide_entity target;
    int32_t type;
    uint32_t offset, size;
    uint8_t bytes[TIDE_INSPECT_BYTES];
    double at;
} tide_run_inspect_sent;
static tide_run_inspect_sent tide_run_inspect_pending[16];

// Its fields' widgets' ids, one after another in the frame
static uint32_t tide_run_inspect_id;

#define TIDE_RUN_INSPECT_INDENT 16.0f // A level of the tree, in pixels
#define TIDE_RUN_INSPECT_GUIDE 9.0f   // Where in its level a row's chevron is, and the guide down from it

static inline const char *tide_run_inspect_type_name(const tide_layout *layout, const uint32_t type)
{
    return type < layout->type_count ? layout->types[type].name : "?";
}

// A type that's the engine's own, every field of it hidden (a command's sequences, a ghost's): never listed
static inline bool tide_run_inspect_internal(const tide_layout *layout, const uint32_t type)
{
    if (type >= layout->type_count || !layout->types[type].field_count) return false;
    const tide_layout_type *t = &layout->types[type];
    for (uint32_t i = 0; i < t->field_count; i++) {
        if (!t->fields[i].hidden) return false;
    }
    return true;
}

// An entity as the menu names it: by its birth, which stays what it is, and where the packet has others born with it
// (spawned in the same tick), a little of its lineage after it, which tells them apart
static inline void tide_run_inspect_entity_name(const tide_packet *p, const tide_entity e, char *out, const size_t size)
{
    const uint32_t n = tide_packet_entities(p->bytes, p->size);
    bool shared = false;
    for (uint32_t i = 0; i < n && !shared; i++) {
        const tide_entity other = tide_packet_entity(p->bytes, p->size, i, NULL);
        shared = other.birth == e.birth && other.lineage != e.lineage;
    }
    if (shared) snprintf(out, size, "#%llu/%04llx", (unsigned long long)e.birth, (unsigned long long)(e.lineage & 0xFFFFu));
    else snprintf(out, size, "#%llu", (unsigned long long)(e.birth ? e.birth : e.lineage));
}

static inline uint32_t tide_run_inspect_place(const tide_packet *p, const tide_entity e)
{
    const uint32_t n = tide_packet_entities(p->bytes, p->size);
    for (uint32_t i = 0; i < n; i++) {
        const tide_entity at = tide_packet_entity(p->bytes, p->size, i, NULL);
        if (at.birth == e.birth && at.lineage == e.lineage) return i;
    }
    return UINT32_MAX;
}

// Entity `i`'s parent, as its place: UINT32_MAX for a root (or a parent that isn't among them, or itself)
static inline uint32_t tide_run_inspect_parent(const tide_packet *p, const uint32_t i)
{
    const uint32_t n = tide_packet_entities(p->bytes, p->size);
    if (i >= n) return UINT32_MAX;
    uint32_t parent;
    memcpy(&parent, p->bytes + TIDE_PACKET_HEADER + (size_t)i * TIDE_PACKET_ENTITY + 24u, sizeof parent);
    return parent < n && parent != i ? parent : UINT32_MAX;
}

static inline uint32_t tide_run_inspect_find(const uint32_t kind, const int32_t type, const tide_entity e)
{
    for (uint32_t i = 0; i < tide_run_inspect_open_count; i++) {
        const tide_run_inspect_node *o = &tide_run_inspect_open[i];
        if (o->kind == kind && o->type == type && o->local == tide_run_inspect_local && o->entity.birth == e.birth
            && o->entity.lineage == e.lineage)
            return i;
    }
    return UINT32_MAX;
}

// Opens it (the longest open closing, past as many as are kept), or closes it
static inline void tide_run_inspect_set_open(const uint32_t kind, const int32_t type, const tide_entity e, const bool open)
{
    const uint32_t at = tide_run_inspect_find(kind, type, e);
    if ((at != UINT32_MAX) == open) return;
    if (!open) {
        memmove(&tide_run_inspect_open[at], &tide_run_inspect_open[at + 1],
                (size_t)(--tide_run_inspect_open_count - at) * sizeof tide_run_inspect_open[0]);
        return;
    }
    if (tide_run_inspect_open_count == TIDE_RUN_INSPECT_OPEN) {
        memmove(&tide_run_inspect_open[0], &tide_run_inspect_open[1],
                (size_t)--tide_run_inspect_open_count * sizeof tide_run_inspect_open[0]);
    }
    tide_run_inspect_open[tide_run_inspect_open_count++] = (tide_run_inspect_node){e, kind, type, tide_run_inspect_local};
}

// An entity in the tree, opened, and every entity it's under, then scrolled to
static inline void tide_run_inspect_show(const tide_packet *p, const tide_entity e)
{
    tide_run_inspect_reveal = e;
    tide_run_inspect_revealing = 2;
    uint32_t at = tide_run_inspect_place(p, e);
    for (uint32_t depth = 0; at != UINT32_MAX && depth < 256u; depth++) {
        tide_run_inspect_set_open(0, -1, tide_packet_entity(p->bytes, p->size, at, NULL), true);
        at = tide_run_inspect_parent(p, at);
    }
}

// As tall as a row is: a widget's (gui.c's LINE)
static inline float tide_run_inspect_line(void)
{
    return tide_run_gui.theme.text_size + 12.0f;
}

// Whether the next row of the tree shows in the list, its top in `y`. One that doesn't is only room, taken here, so a
// world of any size costs what's shown: none of its text is made or measured.
static inline bool tide_run_inspect_row_shows(const uint32_t depth, float *y)
{
    tide_gui *g = &tide_run_gui;
    const tide_float2 next = tide_gui_layout_next(g);
    const float line = tide_run_inspect_line();
    const tide_rect v = tide_run_inspect_view;
    *y = next.y;
    if (next.y + line >= v.y && next.y <= v.y + v.height) return true;
    tide_gui_layout_reserve(g, (tide_float2){TIDE_RUN_INSPECT_INDENT * (float)depth, line});
    return false;
}

// The tree's guides beside what's `depth` levels in, from `top` to `bottom`: a faint line down from each level's
// chevron, so what's under an open row reads as its
static inline void tide_run_inspect_guides(const float x, const uint32_t depth, const float top, const float bottom)
{
    for (uint32_t l = 0; l < depth; l++) {
        const float at = x + TIDE_RUN_INSPECT_INDENT * (float)l + TIDE_RUN_INSPECT_GUIDE;
        tide_run_dev_line((tide_float2){at, top}, (tide_float2){at, bottom}, 1.0f, TIDE_RUN_DEV_FAINT);
    }
}

// A row of the tree that shows, `depth` levels in: a chevron, its name, then what says more of it, dimmer. Lit under
// the pointer, as wide as the list at least; `label` to scripts and the GUI's listing. True when it's clicked.
static inline bool tide_run_inspect_row(const uint32_t id, const char *label, const char *name, const char *more,
                                        const uint32_t depth, const bool open)
{
    tide_gui *g = &tide_run_gui;
    const float indent = TIDE_RUN_INSPECT_INDENT * (float)depth, chevron = 20.0f, gap = 8.0f;
    const float size = g->theme.text_size;
    const float named = g->measure ? g->measure(name, size, NULL, g->theme.font).x : 8.0f * (float)strlen(name);
    const float said = more[0] ? gap + (g->measure ? g->measure(more, size, NULL, g->theme.font).x : 8.0f * (float)strlen(more)) : 0.0f;
    const float line = tide_run_inspect_line();
    const tide_rect r = tide_gui_layout_reserve(g, (tide_float2){indent + chevron + named + said + 8.0f, line});
    tide_run_inspect_guides(r.x, depth, r.y - g->theme.spacing, r.y + r.height);
    const float wide = r.width > tide_run_inspect_view.width ? r.width : tide_run_inspect_view.width;
    const tide_rect hit = {r.x + indent, r.y, wide - indent, r.height};
    tide_gui_describe(g, id, TIDE_GUI_KIND_BUTTON, label);
    const tide_control c = tide_gui_control(g, id, hit);
    const float lit = 0.06f * c.hover + 0.04f * c.press;
    const tide_float2 center = {hit.x + hit.width * 0.5f, hit.y + hit.height * 0.5f};
    if (lit > 0.002f) tide_draw_round_rect(&g->list, center, (tide_float2){hit.width, hit.height}, 4.0f, (tide_color){1.0f, 1.0f, 1.0f, lit});
    if (c.focused) {
        tide_shape s = TIDE_SHAPE_WHITE;
        s.center = center;
        s.size = (tide_float2){hit.width, hit.height};
        s.radius = 4.0f;
        s.color = (tide_color){0.0f, 0.0f, 0.0f, 0.0f};
        s.outline = 1.5f;
        s.outline_color = g->theme.accent;
        tide_draw_shape(&g->list, s);
    }
    const float middle = hit.y + hit.height * 0.5f;
    tide_run_dev_chevron((tide_float2){hit.x + TIDE_RUN_INSPECT_GUIDE, middle}, open, c.hover > 0.5f ? g->theme.text : TIDE_RUN_DEV_DIM);
    const float x = hit.x + chevron;
    tide_run_dev_text(name, x, middle, g->theme.text);
    if (more[0]) tide_run_dev_text(more, x + named + gap, middle, TIDE_RUN_DEV_DIM);
    return c.clicked;
}

// What an open row holds, `indent` pixels in: a row of its own with room before a column. Returns what closes it,
// -1 for nothing to close (and nothing laid out)
static inline int tide_run_inspect_begin_indent(const uint32_t id, const float indent)
{
    tide_gui *g = &tide_run_gui;
    const int row = tide_gui_begin_horizontal(g, id);
    if (row < 0) return -1;
    tide_gui_layout_space(g, indent);
    if (tide_gui_begin_vertical(g, id + 1u) < 0) {
        tide_gui_close(g, row);
        return -1;
    }
    return row;
}

// Where an open row's fields start: under its name, past its chevron
static inline float tide_run_inspect_fields_at(const uint32_t depth)
{
    return TIDE_RUN_INSPECT_INDENT * (float)depth + 20.0f;
}

#define TIDE_RUN_DEV_COLUMN 130.0f // A field's name, then its value: as wide as the GUI's labelled widgets' names

static inline void tide_run_inspect_summary(const tide_layout *layout, const tide_packet *p, uint32_t i, char *has,
                                            size_t has_size, char *preview, size_t preview_size);

// A field that's no value an editor changes, said as it is, its value where an editor's would be: text, an entity (a
// link that shows it in the tree), how much a list or a map holds
static inline void tide_run_inspect_shown_value(const tide_packet *p, const tide_layout_field *f, const char *name,
                                                const uint8_t *at)
{
    tide_gui *g = &tide_run_gui;
    char value[256];
    const char *type = f->type ? f->type : "";
    const bool entity = strcmp(type, "Entity") == 0 || strcmp(type, "LocalEntity") == 0;
    tide_entity e = {0, 0};
    if (entity) {
        memcpy(&e, at, sizeof e);
        const uint32_t place = tide_entity_is_null(e) || tide_run_inspect_indexed != p ? UINT32_MAX : tide_run_inspect_place(p, e);
        if (tide_entity_is_null(e)) snprintf(value, sizeof value, "none");
        else if (place == UINT32_MAX) {
            tide_run_inspect_entity_name(p, e, value, sizeof value);
            snprintf(value + strlen(value), sizeof value - strlen(value), " (not here)");
        } else {
            char has[160], preview[96];
            tide_run_inspect_summary(tide_run_inspect_layout, p, place, has, sizeof has, preview, sizeof preview);
            snprintf(value, sizeof value, "%s%s%s", has[0] ? has : "(empty)", preview[0] ? " " : "", preview);
        }
    } else if (strcmp(type, "string") == 0 || strncmp(type, "List<", 5) == 0 || strncmp(type, "Map<", 4) == 0
               || strncmp(type, "Set<", 4) == 0) {
        const bool text = type[0] == 's', map = type[0] == 'M' || type[0] == 'S';
        uint64_t handle = 0;
        if (map) {
            uint32_t m;
            memcpy(&m, at, sizeof m);
            handle = m;
        } else memcpy(&handle, at, sizeof handle);
        uint32_t length = 0;
        const uint8_t *bytes = tide_packet_text_of(p->bytes, p->size, handle, &length);
        if (text) snprintf(value, sizeof value, "\"%.*s\"", (int)(length < 200u ? length : 200u), bytes ? (const char *)bytes : "");
        else if (map) {
            uint32_t count = 0;
            if (bytes && length >= 4u) memcpy(&count, bytes, 4);
            snprintf(value, sizeof value, "%s, %u", type, count);
        } else snprintf(value, sizeof value, "%s, %u bytes", type, length);
    } else {
        snprintf(value, sizeof value, "%s", type);
    }
    const float size = g->theme.text_size;
    const float named = g->measure ? g->measure(name, size, NULL, g->theme.font).x : 8.0f * (float)strlen(name);
    const float column = named + 16.0f > TIDE_RUN_DEV_COLUMN ? named + 16.0f : TIDE_RUN_DEV_COLUMN;
    const float said = g->measure ? g->measure(value, size, NULL, g->theme.font).x : 8.0f * (float)strlen(value);
    const tide_rect r = tide_gui_layout_reserve(g, (tide_float2){column + said + 20.0f, tide_run_inspect_line()});
    const float middle = r.y + r.height * 0.5f;
    tide_run_dev_text(name, r.x, middle, g->theme.text);
    if (!entity || tide_entity_is_null(e)) {
        tide_run_dev_text(value, r.x + column + 10.0f, middle, entity ? TIDE_RUN_DEV_DIM : g->theme.text);
        return;
    }
    // An entity: a link to it
    const uint32_t id = tide_run_inspect_id++;
    const tide_rect link = {r.x + column, r.y, said + 20.0f, r.height};
    tide_gui_describe(g, id, TIDE_GUI_KIND_BUTTON, value);
    const tide_control c = tide_gui_control(g, id, link);
    tide_draw_round_rect(&g->list, (tide_float2){link.x + link.width * 0.5f, middle}, (tide_float2){link.width, link.height},
                         g->theme.radius, (tide_color){0.42f, 0.66f, 1.0f, 0.1f + 0.08f * c.hover});
    tide_run_dev_text(value, link.x + 10.0f, middle, g->theme.accent);
    if (c.clicked) tide_run_inspect_show(p, e);
}

// A changed field: to the server (the match's own), or here (the local world's, or the local fields of the match's)
static inline void tide_run_inspect_write(const tide_inspect_api *api, const uint32_t kind, const tide_entity target,
                                          const int32_t type, const uint32_t offset, const uint8_t *bytes,
                                          const uint32_t size)
{
    if (!tide_run_inspect_local && (kind == TIDE_PACKET_COMPONENT || kind == TIDE_PACKET_SINGLETON)) {
        api->set(tide_run_local, target, type, offset, bytes, size);
        // Shown as sent till the match has it
        uint32_t slot = 0;
        for (uint32_t i = 0; i < 16u; i++) {
            const tide_run_inspect_sent *s = &tide_run_inspect_pending[i];
            const bool same = s->type == type && s->offset == offset && s->target.birth == target.birth
                           && s->target.lineage == target.lineage;
            if (same || s->at < tide_run_inspect_pending[slot].at) slot = i;
            if (same) break;
        }
        tide_run_inspect_sent *s = &tide_run_inspect_pending[slot];
        *s = (tide_run_inspect_sent){target, type, offset, size, {0}, tide_run_now};
        memcpy(s->bytes, bytes, size);
        return;
    }
    if (!api->set_local(tide_run_local, target, type, offset, bytes, size)) fprintf(stderr, "tide: that field can't be changed\n");
}

// The fields of `type` at `base` in `value` (the root type's, `root`), an editor each or a line, structs' fields
// under their names
static inline void tide_run_inspect_fields(const tide_inspect_api *api, const tide_packet *p, const uint32_t kind,
                                           const tide_entity target, const int32_t root, const int32_t type,
                                           uint8_t *value, const uint32_t base, const char *prefix, const int depth)
{
    const tide_layout *layout = api->layout;
    if (type < 0 || (uint32_t)type >= layout->type_count || depth > 8) return;
    const tide_layout_type *t = &layout->types[type];
    for (uint32_t i = 0; i < t->field_count; i++) {
        const tide_layout_field *f = &t->fields[i];
        if (f->hidden) continue; // The engine's own
        char name[160];
        snprintf(name, sizeof name, "%s%s%s", prefix, prefix[0] ? "." : "", f->name);
        const uint32_t offset = base + f->offset;
        if (f->kind == TIDE_LAYOUT_STRUCT) {
            tide_run_inspect_fields(api, p, kind, target, root, f->decl, value, offset, name, depth + 1);
            continue;
        }
        if (!tide_inspect_field(layout, root, offset, f->size)) {
            tide_run_inspect_shown_value(p, f, name, value + offset);
            continue;
        }
        // Its editor, by what the layout says it is
        uint32_t edit_kind = TIDE_TWEAK_INT, count = f->dim ? f->dim : 1u, size = f->size / count;
        bool color = false;
        const char *names[64];
        int32_t values[64];
        uint32_t members = 0;
        switch (f->kind) {
        case TIDE_LAYOUT_FLOAT: edit_kind = TIDE_TWEAK_FLOAT; break;
        case TIDE_LAYOUT_DOUBLE: edit_kind = TIDE_TWEAK_DOUBLE; break;
        case TIDE_LAYOUT_LONG: edit_kind = TIDE_TWEAK_LONG; break;
        case TIDE_LAYOUT_BYTE: edit_kind = TIDE_TWEAK_BYTE; break;
        case TIDE_LAYOUT_ENUM: {
            edit_kind = TIDE_TWEAK_ENUM;
            count = 1;
            size = f->size;
            const tide_layout_enum *e = f->decl >= 0 && (uint32_t)f->decl < layout->enum_count ? &layout->enums[f->decl] : NULL;
            for (uint32_t m = 0; e && m < e->member_count && m < 64u; m++) {
                names[m] = e->members[m].name;
                values[m] = e->members[m].value;
                members++;
            }
            break;
        }
        case TIDE_LAYOUT_PLAIN: // A bool, or a Color
            if (f->size == 1) edit_kind = TIDE_TWEAK_BOOL;
            else {
                edit_kind = TIDE_TWEAK_FLOAT;
                color = true;
                count = 4;
                size = 4;
            }
            break;
        default: break;
        }
        uint64_t field[TIDE_INSPECT_BYTES / 8u]; // Aligned for the editors
        memcpy(field, value + offset, f->size);
        const uint32_t id = tide_run_inspect_id;
        tide_run_inspect_id += 8u;
        if (tide_run_dev_value(id, name, edit_kind, count, size, color, names, values, members, (uint8_t *)field)
            && memcmp(field, value + offset, f->size) != 0) {
            memcpy(value + offset, field, f->size);
            tide_run_inspect_write(api, kind, target, root, offset, (const uint8_t *)field, f->size);
        }
    }
}

// A value of a section, with what was sent of it and isn't back yet over it, then its fields
static inline void tide_run_inspect_value(const tide_inspect_api *api, const tide_packet *p, const tide_packet_part *part,
                                          const uint32_t index, const tide_entity target)
{
    uint8_t value[4096];
    if (part->stride > sizeof value) {
        tide_gui_layout_label(&tide_run_gui, "(too big to show)");
        return;
    }
    memcpy(value, part->values + (size_t)index * part->stride, part->stride);
    for (uint32_t i = 0; i < 16u && !tide_run_inspect_local; i++) {
        tide_run_inspect_sent *s = &tide_run_inspect_pending[i];
        if (!s->size || s->type != (int32_t)part->type || s->target.birth != target.birth || s->target.lineage != target.lineage
            || s->offset + s->size > part->stride)
            continue;
        if (memcmp(value + s->offset, s->bytes, s->size) == 0 || tide_run_now - s->at > 1.0) s->size = 0; // Back, or never
        else memcpy(value + s->offset, s->bytes, s->size);
    }
    // An earlier tick's, while paused there, are shown as they were: a change goes to the match as it is now
    const int frozen = tide_gui_begin_disabled(&tide_run_gui, tide_run_travel_world() != NULL);
    tide_run_inspect_fields(api, p, part->kind, target, (int32_t)part->type, (int32_t)part->type, value, 0, "", 0);
    if (frozen >= 0) tide_gui_close(&tide_run_gui, frozen);
}

// Room for `count` of `each` bytes in `*buffer`, kept between frames; false when there's none
static inline bool tide_run_inspect_room(void **buffer, uint32_t *capacity, const uint32_t count, const size_t each)
{
    if (count <= *capacity) return true;
    const uint32_t want = count < 64u ? 64u : count + count / 2u;
    void *grown = realloc(*buffer, (size_t)want * each);
    if (!grown) return false;
    *buffer = grown;
    *capacity = want;
    return true;
}

// From the packet: its components' sections the menu lists, each entity's of them, and its children
static inline bool tide_run_inspect_index(const tide_layout *layout, const tide_packet *p, const uint32_t n,
                                          uint32_t *parts_out)
{
    uint32_t parts = 0, total = 0, at = 0;
    tide_packet_part part;
    while (tide_packet_next(p->bytes, p->size, &at, &part)) {
        if (part.kind != TIDE_PACKET_COMPONENT && part.kind != TIDE_PACKET_COMPONENT_LOCAL) continue;
        if (tide_run_inspect_internal(layout, part.type)) continue;
        if (!tide_run_inspect_room((void **)&tide_run_inspect_parts, &tide_run_inspect_part_capacity, parts + 1u,
                                   sizeof *tide_run_inspect_parts))
            return false;
        tide_run_inspect_parts[parts++] = part;
        total += part.count;
    }
    uint32_t capacity = tide_run_inspect_entity_capacity;
    if (!tide_run_inspect_room((void **)&tide_run_inspect_first, &capacity, n + 1u, sizeof(uint32_t))) return false;
    capacity = tide_run_inspect_entity_capacity;
    if (!tide_run_inspect_room((void **)&tide_run_inspect_next, &capacity, n + 1u, sizeof(uint32_t))) return false;
    capacity = tide_run_inspect_entity_capacity;
    if (!tide_run_inspect_room((void **)&tide_run_inspect_start, &capacity, n + 1u, sizeof(uint32_t))) return false;
    capacity = tide_run_inspect_entity_capacity;
    if (!tide_run_inspect_room((void **)&tide_run_inspect_stack, &capacity, n + 1u, sizeof(uint32_t))) return false;
    tide_run_inspect_entity_capacity = capacity;
    if (!tide_run_inspect_room((void **)&tide_run_inspect_comps, &tide_run_inspect_comp_capacity, total + 1u,
                               sizeof *tide_run_inspect_comps))
        return false;
    // Each entity's components, side by side: counted, then each one's start, then put in its place in order
    uint32_t *start = tide_run_inspect_start;
    memset(start, 0, (size_t)(n + 1u) * sizeof *start);
    for (uint32_t k = 0; k < parts; k++) {
        for (uint32_t r = 0; r < tide_run_inspect_parts[k].count; r++) {
            const uint32_t e = tide_run_inspect_parts[k].rows[r];
            if (e < n) start[e + 1u]++;
        }
    }
    for (uint32_t i = 0; i < n; i++) start[i + 1u] += start[i];
    uint32_t *fill = tide_run_inspect_stack; // For now, where each entity's next one goes
    memcpy(fill, start, (size_t)n * sizeof *fill);
    for (uint32_t k = 0; k < parts; k++) {
        for (uint32_t r = 0; r < tide_run_inspect_parts[k].count; r++) {
            const uint32_t e = tide_run_inspect_parts[k].rows[r];
            if (e < n) tide_run_inspect_comps[fill[e]++] = (tide_run_inspect_has){k, r};
        }
    }
    // Children, in the order the entities are: from the last up, each its parent's first so far
    for (uint32_t i = 0; i < n; i++) tide_run_inspect_first[i] = tide_run_inspect_next[i] = UINT32_MAX;
    for (uint32_t i = n; i-- > 0;) {
        const uint32_t parent = tide_run_inspect_parent(p, i);
        if (parent == UINT32_MAX) continue;
        tide_run_inspect_next[i] = tide_run_inspect_first[parent];
        tide_run_inspect_first[parent] = i;
    }
    *parts_out = parts;
    return true;
}

// A field's value in a few characters, for a row's preview: text (quoted, the first 40 characters), a number or a
// vector of them, a bool, an enum's member. False for what's none of those.
static inline bool tide_run_inspect_brief(const tide_layout *layout, const tide_packet *p, const tide_layout_field *f,
                                          const uint8_t *at, char *out, const size_t size)
{
    const char *type = f->type ? f->type : "";
    if (strcmp(type, "string") == 0) {
        uint64_t handle;
        memcpy(&handle, at, sizeof handle);
        uint32_t length = 0;
        const uint8_t *bytes = tide_packet_text_of(p->bytes, p->size, handle, &length);
        if (!bytes || !length) return false;
        snprintf(out, size, "\"%.*s%s\"", (int)(length < 40u ? length : 40u), (const char *)bytes, length > 40u ? "..." : "");
        return true;
    }
    const uint32_t dim = f->dim ? f->dim : 1u;
    size_t used = 0;
    for (uint32_t k = 0; k < dim && k < 4u; k++) {
        char one[48];
        switch (f->kind) {
        case TIDE_LAYOUT_INT: {
            int32_t v;
            memcpy(&v, at + 4u * k, 4);
            snprintf(one, sizeof one, "%d", v);
            break;
        }
        case TIDE_LAYOUT_FLOAT: {
            float v;
            memcpy(&v, at + 4u * k, 4);
            snprintf(one, sizeof one, "%g", (double)v);
            break;
        }
        case TIDE_LAYOUT_DOUBLE: {
            double v;
            memcpy(&v, at + 8u * k, 8);
            snprintf(one, sizeof one, "%g", v);
            break;
        }
        case TIDE_LAYOUT_LONG: {
            int64_t v;
            memcpy(&v, at + 8u * k, 8);
            snprintf(one, sizeof one, "%lld", (long long)v);
            break;
        }
        case TIDE_LAYOUT_BYTE: snprintf(one, sizeof one, "%u", (unsigned)at[k]); break;
        case TIDE_LAYOUT_ENUM: {
            int32_t v = 0;
            memcpy(&v, at, f->size < 4u ? f->size : 4u);
            const char *member = "?";
            if (f->decl >= 0 && (uint32_t)f->decl < layout->enum_count) {
                const tide_layout_enum *e = &layout->enums[f->decl];
                for (uint32_t m = 0; m < e->member_count; m++)
                    if (e->members[m].value == v) member = e->members[m].name;
            }
            snprintf(out, size, "%s", member);
            return true;
        }
        case TIDE_LAYOUT_PLAIN:
            if (f->size == 1 && strcmp(type, "bool") == 0) {
                snprintf(out, size, "%s", at[0] ? "true" : "false");
                return true;
            }
            return false;
        default: return false;
        }
        used += (size_t)snprintf(out + used, used < size ? size - used : 0, "%s%s", k ? ", " : dim > 1 ? "(" : "", one);
    }
    if (dim > 1) snprintf(out + used, used < size ? size - used : 0, ")");
    return true;
}

// What entity `i` is, said in a few words, from the frame's index: its components' names into `has`, and into
// `preview` a value of one of them, a text field's if one has any (a name, most likely), else its first field's that
// can be said ("level 3")
static inline void tide_run_inspect_summary(const tide_layout *layout, const tide_packet *p, const uint32_t i, char *has,
                                            const size_t has_size, char *preview, const size_t preview_size)
{
    size_t used = 0;
    has[0] = preview[0] = '\0';
    for (int pass = 0; pass < 3; pass++) {
        for (uint32_t c = tide_run_inspect_start[i]; c < tide_run_inspect_start[i + 1u]; c++) {
            const tide_packet_part *of = &tide_run_inspect_parts[tide_run_inspect_comps[c].part];
            if (of->kind != TIDE_PACKET_COMPONENT) continue; // Its local fields are its component's
            if (pass == 0) {
                if (used < has_size)
                    used += (size_t)snprintf(has + used, has_size - used, "%s%s", used ? ", " : "",
                                             tide_run_inspect_type_name(layout, of->type));
                continue;
            }
            if (of->type >= layout->type_count || preview[0]) continue;
            const tide_layout_type *t = &layout->types[of->type];
            const uint8_t *value = of->values + (size_t)tide_run_inspect_comps[c].row * of->stride;
            for (uint32_t k = 0; k < t->field_count && !preview[0]; k++) {
                const tide_layout_field *f = &t->fields[k];
                const bool text = f->type && strcmp(f->type, "string") == 0;
                if (f->hidden || f->offset + f->size > of->stride || (pass == 1) != text) continue;
                char brief[96];
                if (!tide_run_inspect_brief(layout, p, f, value + f->offset, brief, sizeof brief)) continue;
                if (text) snprintf(preview, preview_size, "%s", brief);
                else snprintf(preview, preview_size, "%s %s", f->name, brief);
            }
        }
    }
}

// An entity's row's id, by its handle, so it's the same widget whatever's around it
static inline uint32_t tide_run_inspect_row_id(const tide_entity e)
{
    const uint64_t key[2] = {e.birth, e.lineage};
    const uint64_t h = tide_run_bytes_hash((const uint8_t *)key, sizeof key);
    return (uint32_t)(h ^ (h >> 32)) | 0x80000000u;
}

static inline void tide_run_inspect_tree(const tide_inspect_api *api, const tide_packet *p)
{
    const tide_layout *layout = api->layout;
    const uint32_t n = tide_packet_entities(p->bytes, p->size);
    uint32_t parts = 0;
    tide_run_inspect_indexed = n && tide_run_inspect_index(layout, p, n, &parts) ? p : NULL;
    // Singletons, by name, each opened in place
    tide_packet_part part;
    uint32_t at = 0;
    bool any = false;
    while (tide_packet_next(p->bytes, p->size, &at, &part)) {
        if (part.kind != TIDE_PACKET_SINGLETON && part.kind != TIDE_PACKET_SINGLETON_LOCAL) continue;
        const char *type = tide_run_inspect_type_name(layout, part.type);
        if (strcmp(type, "TideTweaks") == 0 || strcmp(type, "TideTweaksHere") == 0) continue; // The Dev tab's tweaks
        if (tide_run_inspect_internal(layout, part.type) || !part.count) continue;
        if (!any) tide_run_inspect_heading("SINGLETONS");
        any = true;
        const tide_entity none = {0, 0};
        const bool open = tide_run_inspect_find(part.kind, (int32_t)part.type, none) != UINT32_MAX;
        const uint32_t id = 0xDE730000u + part.type * 8u + part.kind;
        float y;
        if (tide_run_inspect_row_shows(0, &y)
            && tide_run_inspect_row(id, type, type,
                                    part.kind == TIDE_PACKET_SINGLETON_LOCAL && !tide_run_inspect_local ? "local fields" : "",
                                    0, open))
            tide_run_inspect_set_open(part.kind, (int32_t)part.type, none, !open);
        if (!open) continue;
        const tide_float2 from = tide_gui_layout_next(&tide_run_gui);
        const int block = tide_run_inspect_begin_indent(0xDE740000u + part.type * 8u + part.kind * 2u, tide_run_inspect_fields_at(0));
        if (block < 0) continue;
        tide_run_inspect_value(api, p, &part, 0, none);
        tide_gui_close(&tide_run_gui, block);
        tide_run_inspect_guides(from.x, 1, from.y - tide_run_gui.theme.spacing, tide_gui_layout_next(&tide_run_gui).y);
    }
    // Entities, from those under none: each opened in place with its components, then the entities under it
    if (!tide_run_inspect_indexed) return;
    char heading[48];
    snprintf(heading, sizeof heading, "ENTITIES  %u", n);
    tide_run_inspect_heading(heading);
    // Depth-first, by a stack of places, each with its depth above it
    uint32_t *stack = tide_run_inspect_stack, top = 0;
    static uint32_t *depths;
    static uint32_t depth_capacity;
    if (!tide_run_inspect_room((void **)&depths, &depth_capacity, n + 1u, sizeof *depths)) return;
    for (uint32_t i = n; i-- > 0;) {
        if (tide_run_inspect_parent(p, i) != UINT32_MAX) continue;
        depths[top] = 0;
        stack[top++] = i;
    }
    while (top) {
        const uint32_t i = stack[--top], depth = depths[top];
        const tide_entity e = tide_packet_entity(p->bytes, p->size, i, NULL);
        const bool open = tide_run_inspect_find(0, -1, e) != UINT32_MAX;
        // Its row, where it shows: its name, and its components'
        const uint32_t id = tide_run_inspect_row_id(e);
        float y;
        if (tide_run_inspect_row_shows(depth, &y)) {
            char has[400], preview[128], number[48], more[192], label[460];
            tide_run_inspect_entity_name(p, e, number, sizeof number);
            tide_run_inspect_summary(layout, p, i, has, sizeof has, preview, sizeof preview);
            uint32_t under = 0;
            for (uint32_t child = tide_run_inspect_first[i]; child != UINT32_MAX && under < n; child = tide_run_inspect_next[child]) under++;
            if (under) snprintf(more, sizeof more, "%s%s\xC2\xB7 %u under it", preview, preview[0] ? "  " : "", under);
            else snprintf(more, sizeof more, "%s", preview);
            snprintf(label, sizeof label, "%s %s", number, has[0] ? has : "(empty)"); // Its number tells scripts which
            if (tide_run_inspect_row(id, label, has[0] ? has : "(empty)", more, depth, open)) tide_run_inspect_set_open(0, -1, e, !open);
        }
        if (tide_run_inspect_revealing && e.birth == tide_run_inspect_reveal.birth
            && e.lineage == tide_run_inspect_reveal.lineage) {
            // Shown at the top of the list
            if (tide_run_inspect_offset) tide_run_inspect_offset->y += y - tide_run_inspect_view.y;
            tide_run_inspect_revealing = 0;
        }
        if (!open) continue;
        // Its components, a level in
        const tide_float2 opened = tide_gui_layout_next(&tide_run_gui);
        const int block = tide_run_inspect_begin_indent(id ^ 0x40000000u, tide_run_inspect_fields_at(depth));
        if (block >= 0) {
            char number[64], said[96];
            tide_run_inspect_entity_name(p, e, number, sizeof number);
            snprintf(said, sizeof said, "Entity %s", number);
            tide_run_dev_label(said, 11.0f, TIDE_RUN_DEV_DIM);
            for (uint32_t c = tide_run_inspect_start[i]; c < tide_run_inspect_start[i + 1u]; c++) {
                const tide_packet_part *of = &tide_run_inspect_parts[tide_run_inspect_comps[c].part];
                char title[160];
                snprintf(title, sizeof title, "%s%s", tide_run_inspect_type_name(layout, of->type),
                         of->kind == TIDE_PACKET_COMPONENT_LOCAL && !tide_run_inspect_local ? "  (local fields)" : "");
                if (c > tide_run_inspect_start[i]) tide_gui_layout_space(&tide_run_gui, 4.0f);
                tide_run_dev_label(title, 13.0f, TIDE_RUN_DEV_TYPE);
                // Its fields a step in under its name
                const int fields = tide_run_inspect_begin_indent((id ^ 0x20000000u) + 4u * (c - tide_run_inspect_start[i] + 1u), 12.0f);
                if (fields < 0) continue;
                tide_run_inspect_value(api, p, of, tide_run_inspect_comps[c].row, e);
                tide_gui_close(&tide_run_gui, fields);
            }
            if (tide_run_inspect_start[i] == tide_run_inspect_start[i + 1u]) tide_run_dev_label("No components", 13.0f, TIDE_RUN_DEV_DIM);
            tide_gui_close(&tide_run_gui, block);
            tide_gui_layout_space(&tide_run_gui, 4.0f);
            tide_run_inspect_guides(opened.x, depth + 1u, opened.y - tide_run_gui.theme.spacing, tide_gui_layout_next(&tide_run_gui).y);
        }
        // Then the entities under it, in their order
        const uint32_t from = top;
        for (uint32_t child = tide_run_inspect_first[i]; child != UINT32_MAX && top < n; child = tide_run_inspect_next[child]) {
            depths[top] = depth + 1u;
            stack[top++] = child;
        }
        for (uint32_t a = from, b = top; a + 1u < b; a++, b--) { // Reversed, so the first comes off first
            const uint32_t s = stack[a];
            stack[a] = stack[b - 1u];
            stack[b - 1u] = s;
        }
    }
    if (tide_run_inspect_revealing) tide_run_inspect_revealing--; // Not found
}

static inline void tide_run_dev_inspect(const tide_inspect_api *api, const void *match)
{
    tide_run_inspect_id = 0xDE720000u;
    tide_run_inspect_layout = api->layout;
    api->view(match, tide_run_local, &tide_run_inspect_packets[0], &tide_run_inspect_packets[1]);
    tide_run_inspect_tree(api, &tide_run_inspect_packets[tide_run_inspect_local ? 1 : 0]);
}

// The checkpoints' folder's files of a kind (0 checkpoints, 1 recordings), newest first, into `names`: how many
static inline uint32_t tide_run_list_kind(const uint32_t kind, char (*names)[TIDE_CHECKPOINT_NAME])
{
#ifdef __wasm__
    static char listed[TIDE_RUN_CHECKPOINTS_LISTED * TIDE_CHECKPOINT_NAME];
    const uint32_t size = tide_web_checkpoint_list(kind, listed, sizeof listed - 1u);
    listed[size < sizeof listed ? size : 0u] = '\0';
    uint32_t count = 0;
    for (char *line = listed; *line && count < TIDE_RUN_CHECKPOINTS_LISTED;) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        char why[8];
        if (tide_checkpoint_name_ok(line, why, sizeof why)) snprintf(names[count++], TIDE_CHECKPOINT_NAME, "%s", line);
        if (!end) break;
        line = end + 1;
    }
    return count;
#else
    const uint32_t count = tide_checkpoint_list_of(tide_run_checkpoints, kind ? ".recording" : ".checkpoint", names,
                                                   TIDE_RUN_CHECKPOINTS_LISTED);
    return count < TIDE_RUN_CHECKPOINTS_LISTED ? count : TIDE_RUN_CHECKPOINTS_LISTED;
#endif
}

// The next free name of `prefix`-<n>, past every one listed
static inline void tide_run_next_name(const char *prefix, char (*listed)[TIDE_CHECKPOINT_NAME], const uint32_t count, char *out)
{
    uint32_t next = 1;
    const size_t n = strlen(prefix);
    for (uint32_t i = 0; i < count; i++) {
        unsigned number = 0;
        char tail;
        if (strncmp(listed[i], prefix, n) == 0 && sscanf(listed[i] + n, "-%u%c", &number, &tail) == 1 && number >= next) next = number + 1u;
    }
    snprintf(out, TIDE_CHECKPOINT_NAME, "%s-%u", prefix, (unsigned)next);
}

// The Dev tab's recordings: Save recording (named recording-<n>), the match's ticks so far, and each saved one, newest
// first, which a click replays in the match's place
static inline void tide_run_dev_recordings(void)
{
    tide_gui *g = &tide_run_gui;
    if (tide_run_recordings_listed_at < 0.0 || tide_run_now - tide_run_recordings_listed_at > 1.0) {
        tide_run_recording_count = tide_run_list_kind(1u, tide_run_recording_names);
        tide_run_recordings_listed_at = tide_run_now;
    }
    tide_run_inspect_heading("RECORDINGS");
    if (tide_gui_layout_button(g, 0xDE780001u, "Save recording")) tide_run_travel_save();
    if (tide_run_travel.said[0]) tide_run_dev_label(tide_run_travel.said, 12.0f, TIDE_RUN_DEV_DIM);
    for (uint32_t i = 0; i < tide_run_recording_count; i++) {
        const char *name = tide_run_recording_names[i];
        float y;
        if (!tide_run_inspect_row_shows(0, &y)) continue;
        const bool on = tide_run_travel.replay && strcmp(name, tide_run_travel.replay_name) == 0;
        if (tide_run_inspect_row(0xDE790000u + i, name, name, on ? "replaying" : "", 0, false)) tide_run_travel_replay(name);
    }
    if (!tide_run_recording_count) tide_run_dev_label("None yet: Save recording keeps the match's ticks so far", 12.0f, TIDE_RUN_DEV_DIM);
}

// The Dev tab's checkpoints: Save (named checkpoint-<n>, the next number), then each saved one, newest first, which a
// click loads; the one the run starts over from is marked, and Start fresh forgets it
static inline void tide_run_dev_checkpoints(void)
{
    tide_gui *g = &tide_run_gui;
    if (tide_run_checkpoints_listed_at < 0.0 || tide_run_now - tide_run_checkpoints_listed_at > 1.0) {
        tide_run_checkpoint_count = tide_run_list_kind(0u, tide_run_checkpoint_names);
        tide_run_checkpoints_listed_at = tide_run_now;
    }
    tide_run_inspect_heading("CHECKPOINTS");
    const int row = tide_gui_begin_horizontal(g, 0xDE750000u);
    if (tide_gui_layout_button(g, 0xDE750001u, "Save")) {
        char name[TIDE_CHECKPOINT_NAME];
        tide_run_next_name("checkpoint", tide_run_checkpoint_names, tide_run_checkpoint_count, name);
        tide_run_checkpoint_ask(0, name);
    }
    if (tide_run_from[0] && tide_gui_layout_button(g, 0xDE750002u, "Start fresh")) tide_run_checkpoint_ask(3, "");
    if (row >= 0) tide_gui_close(g, row);
    if (tide_run_checkpoint_said[0]) tide_run_dev_label(tide_run_checkpoint_said, 12.0f, TIDE_RUN_DEV_DIM);
    if (tide_run_checkpoint_forceable && tide_gui_layout_button(g, 0xDE750003u, "Load anyway"))
        tide_run_checkpoint_ask(2, tide_run_checkpoint_refused);
    const uint32_t listed = tide_run_checkpoint_count < TIDE_RUN_CHECKPOINTS_LISTED ? tide_run_checkpoint_count : TIDE_RUN_CHECKPOINTS_LISTED;
    for (uint32_t i = 0; i < listed; i++) {
        const char *name = tide_run_checkpoint_names[i];
        float y;
        if (!tide_run_inspect_row_shows(0, &y)) continue;
        const bool from = strcmp(name, tide_run_from) == 0;
        if (tide_run_inspect_row(0xDE760000u + i, name, name, from ? "starts over here" : "", 0, false)) tide_run_checkpoint_ask(1, name);
    }
    if (!listed) tide_run_dev_label("None yet: Save keeps the match as it is", 12.0f, TIDE_RUN_DEV_DIM);
}

// A bar of the ticks from `first` to `last`, the one shown `shown`, which a drag shows another of
static inline void tide_run_dev_scrub(const uint32_t id, const uint32_t first, const uint32_t last, const uint32_t shown)
{
    if (last <= first) return;
    int32_t tick = (int32_t)shown;
    if (tide_gui_layout_int_slider(&tide_run_gui, id, "", &tick, (int32_t)first, (int32_t)last) && tick >= 0)
        tide_run_travel_show((uint32_t)tick);
}

// The match's time, at the menu's top whatever its tab: Pause or Play, then paused, Back and Step a tick, the tick
// shown, a bar to drag through the ticks, and at an earlier one, Go on from here, which the match goes on from. Why it
// can't pause, where it can't. A recording replayed has its own: Play or Pause, Back, Step, its bar, whether this build
// replays it as it was recorded, and Stop replay.
static inline void tide_run_dev_time(void)
{
    tide_gui *g = &tide_run_gui;
    char line[160];
    if (tide_run_travel.replay) {
        tide_timeline *r = tide_run_travel.replay;
        const uint32_t first = tide_timeline_first(r), last = tide_timeline_last(r), shown = tide_run_travel.replay_shown;
        const int row = tide_gui_begin_horizontal(g, 0xDE770010u);
        if (row < 0) return;
        if (tide_gui_layout_button(g, 0xDE770011u, tide_run_travel.playing ? "Pause" : "Play")) tide_run_travel_pause(tide_run_travel.playing);
        if (tide_gui_layout_button(g, 0xDE770012u, "Back")) tide_run_travel_back();
        if (tide_gui_layout_button(g, 0xDE770013u, "Step")) tide_run_travel_forward();
        snprintf(line, sizeof line, "%s: tick %u of %u", tide_run_travel.replay_name, (unsigned)shown, (unsigned)last);
        tide_run_dev_label(line, 12.0f, TIDE_RUN_DEV_DIM);
        tide_gui_close(g, row);
        tide_run_dev_scrub(0xDE770014u, first, last, shown);
        tide_timeline_check check;
        tide_timeline_check_of(r, &check);
        if (check.unreadable) snprintf(line, sizeof line, "This build can't replay tick %u", (unsigned)check.unreadable);
        else if (check.differs) snprintf(line, sizeof line, "Tick %u differs from the recording", (unsigned)check.differs);
        else snprintf(line, sizeof line, "As recorded, to tick %u", (unsigned)check.checked);
        tide_run_dev_label(line, 12.0f, check.differs || check.unreadable ? g->theme.accent : TIDE_RUN_DEV_DIM);
        if (tide_gui_layout_button(g, 0xDE770015u, "Stop replay")) tide_run_travel_stop_replay();
        return;
    }
    const char *why = tide_run_travel_why();
    const bool paused = tide_run_travel.paused;
    if (why[0] && !paused) {
        snprintf(line, sizeof line, "Can't pause: %s", why);
        tide_run_dev_label(line, 12.0f, TIDE_RUN_DEV_DIM);
        return;
    }
    const int row = tide_gui_begin_horizontal(g, 0xDE770000u);
    if (row < 0) return;
    if (tide_gui_layout_button(g, 0xDE770001u, paused ? "Play" : "Pause")) tide_run_travel_pause(!paused);
    if (paused && tide_gui_layout_button(g, 0xDE770002u, "Back")) tide_run_travel_back();
    if (paused && tide_gui_layout_button(g, 0xDE770003u, "Step")) tide_run_travel_forward();
    const uint32_t shown = tide_run_travel_shown(), last = tide_run_travel_last();
    if (paused && shown < last) snprintf(line, sizeof line, "tick %u of %u", (unsigned)shown, (unsigned)last);
    else snprintf(line, sizeof line, "tick %u", (unsigned)last);
    tide_run_dev_label(line, 12.0f, TIDE_RUN_DEV_DIM);
    tide_gui_close(g, row);
    if (!paused) return;
    tide_run_dev_scrub(0xDE770005u, tide_run_travel_first(), last, shown);
    if (tide_run_travel_background()) tide_run_dev_label("The match goes on for the other players", 12.0f, TIDE_RUN_DEV_DIM);
    if (shown < last && tide_gui_layout_button(g, 0xDE770004u, "Go on from here")) tide_run_travel_go_on();
}

// The menu's tab: 0 the game's dev views and tweaks, 1 the match's inspector, 2 the local world's. Each tab's list
// where it's scrolled to, from its top, and how tall it last was
static uint32_t tide_run_dev_tab;
static tide_float2 tide_run_dev_offsets[3];
static float tide_run_dev_heights[3] = {64.0f, 64.0f, 64.0f};
static float tide_run_dev_tabs_tall = 40.0f; // The tabs' row, and the room under it

static inline void tide_run_dev_menu(const tide_host_game *game, const void *match)
{
    const tide_tweaks_api *tweaks = game->tweaks ? game->tweaks() : NULL;
    if (tweaks && !tweaks->count) tweaks = NULL;
    const tide_inspect_api *inspect = game->inspect ? game->inspect() : NULL;
    if ((!game->dev_views || !game->dev_views[0]) && !tweaks && !inspect) return;
    const bool key = tide_run_devices.keyboard.backquote.held;
    if (key && !tide_run_dev_key) tide_run_dev_open = !tide_run_dev_open;
    tide_run_dev_key = key;
    if (!tide_run_dev_open) return;
    tide_gui *g = &tide_run_gui;
    const float pad = 8.0f, width = 320.0f, margin = 8.0f;
    const tide_float2 screen = tide_platform_screen_size();
    const float most = screen.y - 2.0f * pad > 0.0f ? screen.y - 2.0f * pad : 0.0f;
    // Inside the panel, past its margin: as much as the screen has
    const float x = pad + margin, inner = most - 2.0f * margin > 0.0f ? most - 2.0f * margin : 0.0f;
    tide_gui_view(g, UINT32_MAX); // Its own: no view's claims hide the devices from it
    const bool dev = (game->dev_views && game->dev_views[0]) || tweaks || inspect; // A dev build's checkpoints
    if (!dev && tide_run_dev_tab == 0) tide_run_dev_tab = 1;
    if (!inspect) tide_run_dev_tab = 0;
    // As tall as they were a frame before: the tabs, and under them this tab's list, which a longer list scrolls past
    // (the wheel, or a drag on a touchscreen)
    const float tabs = inspect ? (tide_run_dev_tabs_tall < inner ? tide_run_dev_tabs_tall : inner) : 0.0f;
    const float top = x + tabs, room = inner - tabs;
    tide_float2 *offset = &tide_run_dev_offsets[tide_run_dev_tab];
    float *last = &tide_run_dev_heights[tide_run_dev_tab];
    const float shown = tabs + (*last < room ? *last : room) + 2.0f * margin;
    // A panel behind it all, so its text reads over any game. The pointer is its over the panel alone: below it, the
    // game's
    const tide_rect back = {pad, pad, width + 2.0f * margin, shown < most ? shown : most};
    tide_gui_claim_pointer_at(g, back);
    const int themed = tide_gui_begin_theme(g, tide_run_dev_theme()); // Its own look, whatever the game's
    const int area = tide_gui_begin_area(g, 0xDE6D0000u, back);
    if (inspect) {
        // Its tabs, which stay at the top however far the list is scrolled: Dev for a game with dev views or tweaks,
        // and the inspector's two worlds (a container of their own, as tall as their row was)
        static tide_float2 none;
        none = (tide_float2){0.0f, 0.0f};
        const int box = tide_gui_begin_scroll(g, 0xDE6E0000u, (tide_rect){x, x, width, tabs}, &none);
        const int row = tide_gui_begin_horizontal(g, 0xDE6E0001u);
        static const char *const names[3] = {"Dev", "Match", "Local"};
        const float size = g->theme.text_size, line = size + 14.0f;
        for (uint32_t t = dev ? 0u : 1u; t < 3u; t++) {
            const float w = g->measure ? g->measure(names[t], size, NULL, g->theme.font).x : 8.0f * (float)strlen(names[t]);
            const tide_rect r = tide_gui_layout_reserve(g, (tide_float2){w + 20.0f, line});
            tide_gui_describe(g, 0xDE6E0002u + t, TIDE_GUI_KIND_BUTTON, names[t]);
            const tide_control c = tide_gui_control(g, 0xDE6E0002u + t, r);
            const bool on = tide_run_dev_tab == t;
            const tide_color text = on || c.hover > 0.5f ? g->theme.text : TIDE_RUN_DEV_DIM;
            if (c.hover > 0.01f && !on) {
                tide_draw_round_rect(&g->list, (tide_float2){r.x + r.width * 0.5f, r.y + r.height * 0.5f},
                                     (tide_float2){r.width, r.height}, 5.0f, (tide_color){1.0f, 1.0f, 1.0f, 0.05f * c.hover});
            }
            tide_run_dev_text(names[t], r.x + 10.0f, r.y + r.height * 0.5f - 1.0f, text);
            if (on) tide_draw_rect(&g->list, (tide_float2){r.x + r.width * 0.5f, r.y + r.height - 1.0f}, (tide_float2){w + 8.0f, 2.0f}, g->theme.accent);
            if (c.clicked) tide_run_dev_tab = t;
        }
        tide_gui_close(g, row);
        tide_run_dev_time();
        const tide_rect rule = tide_gui_layout_reserve(g, (tide_float2){width, 1.0f});
        tide_draw_rect(&g->list, (tide_float2){rule.x + width * 0.5f, rule.y + 0.5f}, (tide_float2){width, 1.0f}, TIDE_RUN_DEV_FAINT);
        tide_gui_layout_space(g, 2.0f);
        const tide_rect below = tide_gui_layout_reserve(g, (tide_float2){0.0f, 0.0f});
        tide_gui_close(g, box);
        if (below.y > x) tide_run_dev_tabs_tall = below.y - x;
    }
    const tide_rect list = {x, top, width, room};
    const int depth = tide_gui_begin_scroll(g, 0xDE6F0000u + tide_run_dev_tab, list, offset);
    if (tide_run_dev_tab == 0) {
        for (uint32_t i = 0; game->dev_views && i < TIDE_RUN_DEV_VIEWS && game->dev_views[i]; i++) {
            bool on = tide_run_dev_shown(game->dev_views[i]);
            if (tide_gui_layout_toggle(g, 0xDE700000u + i, game->dev_views[i], &on)) tide_run_dev_show(game->dev_views[i], on);
        }
        if (tweaks) tide_run_dev_tweaks(tweaks);
        if (inspect) tide_run_dev_checkpoints();
        if (inspect) tide_run_dev_recordings();
    } else {
        tide_run_inspect_local = tide_run_dev_tab == 2;
        tide_run_inspect_view = list;
        tide_run_inspect_offset = offset;
        tide_run_dev_inspect(inspect, match);
    }
    // Where the list ends, as an empty place after it: its height, wherever it's scrolled to
    const tide_rect end = tide_gui_layout_reserve(g, (tide_float2){0.0f, 0.0f});
    tide_gui_close(g, depth);
    tide_gui_close(g, area);
    tide_gui_close(g, themed);
    const float height = end.y + offset->y - list.y;
    if (height > 0.0f) *last = height;
}

// Where the matches this machine hosts keep their store (Storage.Save and Load in match code): the game's own
// files, once they're named (tide_run_bind_io), for a game whose match code uses it
static const tide_io *tide_run_store;
// ...in a run with no window, those files with no thread of their own: its sessions have no clock, so a run repeats,
// and an answer read on the provider's thread would reach whichever tick was running as the disk finished. Carried
// out as each tick ends instead, its answers go to the next tick, every run
static tide_io tide_run_store_here;

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
        .io = tide_run_store,
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
    // The provider makes the game's folders at once: a run with no window (a test's) of a game that uses neither
    // files nor the store leaves none behind. A window's run has them for its log anyway, and makes the store for
    // whichever build first uses it, so a reload that adds Storage keeps what it saves.
    if (tide_run_headless && !game->set_io && !game->game->stored) return;
    const char *directory = tide_run_data_directory;
    char *player_directory = NULL;
    if (directory && tide_platform_players() > 1u) {
        const size_t size = strlen(directory) + 32u;
        player_directory = tide_alloc(size);
        snprintf(player_directory, size, "%s/player-%u", directory, (unsigned)tide_platform_player());
        directory = player_directory;
    }
    const tide_io *io = tide_platform_io_init(id, directory);
    if (game->set_io) game->set_io(local, io);
    // ...and the matches this machine runs keep their store there (Storage.Save in match code), whichever build first
    // uses it: a session made now hands its servers this provider, which stays the same across reloads
    tide_run_store = io;
    if (io && tide_run_headless) {
        tide_io here = *io;
        here.post = NULL;
        here.settle = NULL;
        // Written only where it changed: a reload binds again while the server may read it
        if (memcmp(&here, &tide_run_store_here, sizeof here) != 0) tide_run_store_here = here;
        tide_run_store = &tide_run_store_here;
    }
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

static inline bool tide_run_checkpoint_world(const char *name, bool force, void **world, int32_t *player);

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
    // From a checkpoint: --from, or the one loaded last. The first time, one that can't be loaded ends the run, so a
    // script or an agent that asked for it hears why; after, the game starts fresh, saying why
    static bool begun;
    void *from = tide_run_from_world; // Loaded already, by the menu or a script
    int32_t from_player = tide_run_from_player;
    tide_run_from_world = NULL;
    if (!from && tide_run_from[0] && !tide_run_checkpoint_world(tide_run_from, tide_run_from_force, &from, &from_player)) {
#ifdef __wasm__
        if (tide_run_checkpoint_coming) tide_run_from[0] = '\0'; // On its way: it's gone on from as it comes
        else
#endif
        if (!begun) exit(2);
        tide_run_from[0] = '\0';
    }
    begun = true;
    tide_session_request request = {0};
    bool asked = tide_run_arguments(&request);
    if (from && asked && request.kind != TIDE_REQUEST_OPEN) {
        fprintf(stderr, "tide: --from starts this machine's match from a checkpoint: it takes no --join or --connect\n");
        exit(2);
    }
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
    if (from) {
        // The match goes on from it, this machine's player the one who saved it
        tide_player_set players = {0};
        tide_players_add(&players, from_player >= 0 ? (uint32_t)from_player : 0u);
        tide_session_start_from(tide_run_session, from, players, tide_run_now);
        game->game->free_world(from);
        free(from);
        if (open) tide_run_request(&request, NULL);
    } else if (asked && !open) {
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
    bool found;     // There was a pack to read (its bytes may be the loaded pack's by now, and not kept here)
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
    l->loaded = !bytes || tide_run_load_given(bytes, size);
    l->found = bytes != NULL;
    l->bytes = tide_pack_given(bytes) ? NULL : bytes; // The loaded pack's own memory, where it took them
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
        if (l->read && !l->found && !l->path) {
            fprintf(stderr, "tide: the game's pack of assets isn't with the program (<program>.pack beside it): they read as empty\n");
        }
        free(l->path);
        l->path = NULL;
        if (l->found) tide_run_assets_kept(l->bytes, l->size, l->loaded);
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

static inline void tide_run_travel_drop(void);

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
    tide_run_travel_drop();
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

// The game's layout, which checkpoints are saved with and carried over by: a dev build's (its inspector's), or one
// tide run's library has; NULL for neither
static inline const tide_layout *tide_run_layout(void)
{
    const tide_host_game *game = tide_run_game;
    const tide_inspect_api *inspect = game->inspect ? game->inspect() : NULL;
    return inspect && inspect->layout ? inspect->layout : game->layout;
}

// The world of checkpoint `name`, carried over to this build, into `*world` (free it with free_world, then free()),
// and the player who saved it: false, having said why (and kept it for the menu)
static inline bool tide_run_checkpoint_world(const char *name, const bool force, void **world, int32_t *player)
{
    const tide_host_game *game = tide_run_game;
    const tide_layout *layout = tide_run_layout();
    tide_run_checkpoint_forceable = false;
    if (!layout) {
        snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said,
                 "checkpoints are a dev build's: tide run, or tide build --dev");
        fprintf(stderr, "tide: %s\n", tide_run_checkpoint_said);
        return false;
    }
    tide_checkpoint_loaded loaded;
#ifdef __wasm__
    const uint32_t size = tide_web_checkpoint_copy(0u, name, NULL, 0);
    if (!size) { // Not here yet: asked for, and loaded as it comes (tide_run_checkpoints_poll)
        tide_web_checkpoint_fetch(0u, name);
        tide_run_checkpoint_coming = force ? 2 : 1;
        snprintf(tide_run_checkpoint_coming_name, sizeof tide_run_checkpoint_coming_name, "%s", name);
        snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said, "Loading %s", name);
        return false;
    }
    void *bytes = size != UINT32_MAX ? malloc(size) : NULL;
    if (bytes) tide_web_checkpoint_copy(0u, name, bytes, size);
    else if (size == UINT32_MAX) { // Forgotten, so a later ask fetches it again
        char none;
        tide_web_checkpoint_copy(0u, name, &none, 0);
    }
    const bool ok = bytes && tide_checkpoint_unpack(bytes, size, name, layout, game->game, force, world, player, &loaded);
    if (!bytes) {
        loaded = (tide_checkpoint_loaded){0};
        snprintf(loaded.carried.failed, sizeof loaded.carried.failed,
                 size == UINT32_MAX ? "there's no checkpoint %s in the game's checkpoints folder (it's tide run --web's)"
                                    : "there wasn't enough memory for %s",
                 name);
    }
    free(bytes);
    if (!ok) {
#else
    if (!tide_checkpoint_load(tide_run_checkpoints, name, layout, game->game, force, world, player, &loaded)) {
#endif
        const bool scene = strncmp(loaded.carried.failed, "the scene ", 10) == 0;
        snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said, "%s didn't load: %s", name, loaded.carried.failed);
        fprintf(stderr, "tide: checkpoint %s%s\n", tide_run_checkpoint_said,
                scene && !force ? "; load what carries over with --force, `checkpoint force-load`, or the menu's Load anyway" : "");
        tide_run_checkpoint_forceable = scene && !force;
        if (tide_run_checkpoint_forceable) snprintf(tide_run_checkpoint_refused, sizeof tide_run_checkpoint_refused, "%s", name);
        return false;
    }
    int n = snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said, "Loaded %s, from tick %u", name, (unsigned)loaded.tick);
    if (!loaded.same) {
        const tide_migration *m = &loaded.carried;
        n += snprintf(tide_run_checkpoint_said + n, sizeof tide_run_checkpoint_said - (size_t)n, ", carried over to this build");
        if (loaded.fields_reset)
            n += snprintf(tide_run_checkpoint_said + n, sizeof tide_run_checkpoint_said - (size_t)n, "; %u field%s reset",
                          (unsigned)loaded.fields_reset, loaded.fields_reset == 1 ? "" : "s");
        if (m->entities_dropped)
            n += snprintf(tide_run_checkpoint_said + n, sizeof tide_run_checkpoint_said - (size_t)n, "; %u entit%s dropped%s%s",
                          (unsigned)m->entities_dropped, m->entities_dropped == 1 ? "y" : "ies",
                          m->scene_dropped[0] ? " with the scene " : "", m->scene_dropped);
        if (m->tasks_dropped)
            snprintf(tide_run_checkpoint_said + n, sizeof tide_run_checkpoint_said - (size_t)n, "; %u waiting task%s dropped",
                     (unsigned)m->tasks_dropped, m->tasks_dropped == 1 ? "" : "s");
    }
    printf("tide: %s\n", tide_run_checkpoint_said);
    fflush(stdout);
    return true;
}

// Saves the match this machine's server runs as checkpoint `name`
static inline void tide_run_checkpoint_save(const char *name)
{
    const tide_host_game *game = tide_run_game;
    const tide_layout *layout = tide_run_layout();
    tide_run_checkpoint_forceable = false;
    // As it is after the tick that's running, if one runs beside the frame
    const tide_server *server = tide_run_session && layout ? tide_session_server(tide_run_session) : NULL;
    const void *world = server ? tide_session_server_world(tide_run_session) : NULL;
    char why[200];
    if (!layout) {
        snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said, "checkpoints are a dev build's: tide run, or tide build --dev");
    } else if (!world) {
        snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said,
                 "%s isn't saved: this machine doesn't run the match's server, so save on the one that hosts it", name);
    } else {
        const tide_session_status status = tide_session_status_of(tide_run_session);
        const uint32_t tick = status.client.server_tick;
        const int32_t player = tide_player_index(status.client.player);
#ifdef __wasm__
        // A page's go to tide, which keeps them in the game's folder: kept once it says so (tide_run_checkpoints_poll)
        uint32_t size = 0;
        uint8_t *bytes = NULL;
        if (tide_checkpoint_name_ok(name, why, sizeof why)) {
            bytes = tide_checkpoint_pack(layout, game->game, world, player, tick, &size);
            if (!bytes) snprintf(why, sizeof why, "there wasn't enough memory");
        }
        if (bytes) {
            tide_web_checkpoint_save(0u, name, bytes, size);
            free(bytes);
            snprintf(tide_run_checkpoint_saving, sizeof tide_run_checkpoint_saving, "%s", name);
            tide_run_checkpoint_saving_kind = 0;
            snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said, "Saving %s, at tick %u", name, (unsigned)tick);
            return;
        }
#else
        if (tide_checkpoint_save(tide_run_checkpoints, name, layout, game->game, world, player, tick, why, sizeof why)) {
            snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said, "Saved %s, at tick %u", name, (unsigned)tick);
            printf("tide: %s (%s/%s.checkpoint)\n", tide_run_checkpoint_said, tide_run_checkpoints, name);
            fflush(stdout);
            tide_run_checkpoints_listed_at = -1.0; // Listed again
            return;
        }
#endif
        snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said, "%s isn't saved: %s", name, why);
    }
    fprintf(stderr, "tide: checkpoint %s\n", tide_run_checkpoint_said);
}

// Starts the game again from checkpoint `name` (`force`: what of it carries over), which it starts over from from now
// on; one that won't load leaves the match as it is. A checkpoint is the match's: the local state starts over, as at a
// full reload, and the game's local code makes it again from the match. "" starts fresh.
static inline void tide_run_checkpoint_go(const char *name, const bool force)
{
    if (name[0]) {
        void *world = NULL;
        int32_t player = -1;
        if (!tide_run_checkpoint_world(name, force, &world, &player)) return;
        tide_run_from_world = world; // tide_run_begin starts from it
        tide_run_from_player = player;
    }
    snprintf(tide_run_from, sizeof tide_run_from, "%s", name);
    tide_run_from_force = force;
    tide_run_end();
    tide_platform_audio_forget();
    tide_devices_consume(&tide_run_devices);
    tide_run_begin();
    if (!name[0]) {
        snprintf(tide_run_checkpoint_said, sizeof tide_run_checkpoint_said, "Started fresh");
        printf("tide: started fresh\n");
        fflush(stdout);
    }
}

// What the menu or a script asked of checkpoints, as a frame begins
static inline void tide_run_checkpoints_poll(void)
{
    uint32_t way = 0;
    char name[TIDE_CHECKPOINT_NAME];
#ifdef __wasm__
    if (tide_run_checkpoint_saving[0]) {
        const uint32_t saved = tide_web_checkpoint_saved();
        if (saved) {
            char *said = tide_run_checkpoint_saving_kind ? tide_run_travel.said : tide_run_checkpoint_said;
            snprintf(said, sizeof tide_run_checkpoint_said, saved == 1u ? "Saved %s" : "%s isn't saved: "
                     "a page keeps them in the game's folder only under tide run --web", tide_run_checkpoint_saving);
            printf("tide: %s\n", said);
            tide_run_recordings_listed_at = -1.0;
            tide_run_checkpoint_saving[0] = '\0';
            tide_run_checkpoints_listed_at = -1.0;
        }
    }
    if (tide_run_travel.coming[0] && tide_web_checkpoint_copy(1u, tide_run_travel.coming, NULL, 0)) {
        snprintf(name, sizeof name, "%s", tide_run_travel.coming);
        tide_run_travel.coming[0] = '\0';
        tide_run_travel_replay(name);
    }
    if (tide_run_checkpoint_coming && tide_web_checkpoint_copy(0u, tide_run_checkpoint_coming_name, NULL, 0)) {
        const bool force = tide_run_checkpoint_coming == 2;
        tide_run_checkpoint_coming = 0;
        snprintf(name, sizeof name, "%s", tide_run_checkpoint_coming_name);
        tide_run_checkpoint_go(name, force);
    }
#endif
    while (tide_platform_checkpoint_asked(&way, name, sizeof name)) {
        if (way == 0u) tide_run_checkpoint_save(name);
        else tide_run_checkpoint_go(name, way == 2u);
    }
    const int want = tide_run_checkpoint_want;
    tide_run_checkpoint_want = -1;
    if (want == 0) tide_run_checkpoint_save(tide_run_checkpoint_name);
    else if (want == 1 || want == 2) tide_run_checkpoint_go(tide_run_checkpoint_name, want == 2);
    else if (want == 3) tide_run_checkpoint_go("", false);
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

// The match's ticks a second, which a recording says it was made at
static inline uint32_t tide_run_travel_rate(void)
{
    if (tide_run_settings.tick_rate > 0) return (uint32_t)tide_run_settings.tick_rate;
    return tide_run_game && tide_run_game->game->tick_rate ? tide_run_game->game->tick_rate : 60u;
}

// Players of other machines are in the match: it never stops for them
static inline bool tide_run_travel_background(void)
{
    return tide_run_session && tide_session_remote_players(tide_run_session) > 0u;
}

static inline void tide_run_travel_stop_replay(void)
{
    tide_timeline_destroy(tide_run_travel.replay);
    tide_run_travel.replay = NULL;
    tide_run_travel.playing = false;
    tide_run_travel.replay_name[0] = '\0';
}

// Lets go of the recording (and of a pause, and a replay): before the code of the build that made its worlds goes
static inline void tide_run_travel_drop(void)
{
    if (tide_run_travel.live && tide_run_session && tide_run_travel.recorded == tide_run_session) {
        tide_session_record(tide_run_session, NULL);
    }
    tide_timeline_destroy(tide_run_travel.live);
    tide_run_travel.live = NULL;
    tide_run_travel.recorded = NULL;
    tide_run_travel.game = NULL;
    tide_run_travel.paused = tide_run_travel.step = false;
    tide_run_travel_stop_replay();
}

// Why the match can't pause now, "" when it can: a dev build's match that this machine runs
static inline const char *tide_run_travel_why(void)
{
    const tide_host_game *game = tide_run_game;
    if (tide_run_embedded || !game || !game->inspect) return "pausing is a dev build's";
    if (!tide_run_session) return "there's no match";
    if (game->game->shards) return "a sharded match doesn't pause";
    const tide_session_status status = tide_session_status_of(tide_run_session);
    if (!status.server) return status.client.state == TIDE_SESSION_OFFLINE ? "there's no match" : "the match is another machine's";
    if (!tide_run_travel.live || !tide_timeline_started(tide_run_travel.live)) return "the match hasn't started";
    return "";
}

// The match recorded while it's one this machine runs in a dev build, and a pause that can't last ended
static inline void tide_run_travel_follow(void)
{
    const tide_host_game *game = tide_run_game;
    const bool wanted = !tide_run_embedded && game && game->inspect && tide_run_session && !game->game->shards
                     && tide_session_status_of(tide_run_session).server;
    if (!wanted || (tide_run_travel.live && (tide_run_travel.recorded != tide_run_session || tide_run_travel.game != game->game))) {
        if (tide_run_travel.live || tide_run_travel.paused) {
            tide_timeline *replay = tide_run_travel.replay; // A recording replayed isn't the match's: it stays
            tide_run_travel.replay = NULL;
            tide_run_travel_drop();
            tide_run_travel.replay = replay;
        }
    }
    if (wanted && !tide_run_travel.live) {
        tide_run_travel.live = tide_timeline_create(&(tide_timeline_desc){.game = game->game, .jobs = tide_run_jobs()});
        if (!tide_run_travel.live) return;
        tide_run_travel.recorded = tide_run_session;
        tide_run_travel.game = game->game;
        const tide_session_recorder recorder = tide_timeline_recorder(tide_run_travel.live);
        tide_session_record(tide_run_session, &recorder);
    }
    if (tide_run_travel.paused && tide_run_travel_why()[0]) tide_run_travel.paused = tide_run_travel.step = false; // It goes on
}

static inline uint32_t tide_run_travel_first(void)
{
    return tide_run_travel.live ? tide_timeline_first(tide_run_travel.live) : 0u;
}

static inline uint32_t tide_run_travel_last(void)
{
    return tide_run_travel.live ? tide_timeline_last(tide_run_travel.live) : 0u;
}

// The tick the frames show: the latest, but for an earlier one shown while paused
static inline uint32_t tide_run_travel_shown(void)
{
    const uint32_t last = tide_run_travel_last();
    return tide_run_travel.paused && tide_run_travel.shown < last ? tide_run_travel.shown : last;
}

// Pauses, or goes on: a recording replayed plays or stops
static inline void tide_run_travel_pause(const bool paused)
{
    if (tide_run_travel.replay) {
        const bool ended = tide_run_travel.replay_shown >= tide_timeline_last(tide_run_travel.replay);
        if (!paused && ended) tide_run_travel.replay_shown = tide_timeline_first(tide_run_travel.replay); // Again from its start
        tide_run_travel.playing = !paused;
        tide_run_travel.played = 0.0;
        return;
    }
    if (!paused) { // Going on from the latest
        tide_run_travel.paused = tide_run_travel.step = false;
        return;
    }
    if (tide_run_travel_why()[0] || tide_run_travel.paused) return;
    tide_run_travel.paused = true;
    tide_run_travel.step = false;
    tide_run_travel.shown = tide_run_travel_last();
}

// A tick on: the next one kept, or from the latest, the match's next tick (which goes by itself for other players)
static inline void tide_run_travel_forward(void)
{
    if (tide_run_travel.replay) {
        tide_run_travel.playing = false;
        if (tide_run_travel.replay_shown < tide_timeline_last(tide_run_travel.replay)
            && tide_timeline_world(tide_run_travel.replay, tide_run_travel.replay_shown + 1u)) tide_run_travel.replay_shown++;
        return;
    }
    if (!tide_run_travel.paused) return;
    if (tide_run_travel.shown < tide_run_travel_last()) tide_run_travel.shown++;
    else if (!tide_run_travel_background()) tide_run_travel.step = true;
}

// A tick back, if there's one before it
static inline void tide_run_travel_back(void)
{
    if (tide_run_travel.replay) {
        tide_run_travel.playing = false;
        if (tide_run_travel.replay_shown > tide_timeline_first(tide_run_travel.replay)
            && tide_timeline_world(tide_run_travel.replay, tide_run_travel.replay_shown - 1u)) tide_run_travel.replay_shown--;
        return;
    }
    if (!tide_run_travel.paused || !tide_run_travel.live) return;
    const uint32_t shown = tide_run_travel_shown();
    if (shown > tide_timeline_first(tide_run_travel.live) && tide_timeline_world(tide_run_travel.live, shown - 1u)) {
        tide_run_travel.shown = shown - 1u;
    }
}

// Tick `tick` shown, from the first to the last: made again by replaying where it isn't kept
static inline void tide_run_travel_show(const uint32_t tick)
{
    if (tide_run_travel.replay) {
        if (tide_timeline_world(tide_run_travel.replay, tick)) tide_run_travel.replay_shown = tick;
        tide_run_travel.playing = false;
        return;
    }
    if (!tide_run_travel.paused || !tide_run_travel.live) return;
    const uint32_t last = tide_run_travel_last();
    if (tick < tide_timeline_first(tide_run_travel.live) || tick > last) return;
    if (tick < last && !tide_timeline_world(tide_run_travel.live, tick)) return;
    tide_run_travel.shown = tick;
    tide_run_travel.step = false;
}

// The match goes on from the tick shown, for every machine in it: the ticks after it are let go
static inline void tide_run_travel_go_on(void)
{
    if (tide_run_travel.replay || !tide_run_travel.paused || !tide_run_travel.live) return;
    const uint32_t tick = tide_run_travel_shown();
    const void *world = tide_timeline_world(tide_run_travel.live, tick);
    if (!world || !tide_session_rewind(tide_run_session, world)) return;
    tide_timeline_cut(tide_run_travel.live, tick, tide_session_server_world(tide_run_session));
    tide_run_travel.paused = tide_run_travel.step = false;
}

// The match's ticks so far, as a recording beside the checkpoints, named recording-<n>
static inline void tide_run_travel_save(void)
{
    tide_run_travel_follow();
    if (!tide_run_travel.live || !tide_timeline_started(tide_run_travel.live)) {
        snprintf(tide_run_travel.said, sizeof tide_run_travel.said, "Nothing's recorded: %s",
                 tide_run_travel_why()[0] ? tide_run_travel_why() : "the match hasn't started");
        return;
    }
    char name[TIDE_CHECKPOINT_NAME];
    tide_run_next_name("recording", tide_run_recording_names, tide_run_recording_count, name);
    const uint32_t player = tide_run_session ? tide_session_status_of(tide_run_session).client.player.id : 0u;
    uint32_t size = 0;
    uint8_t *bytes = tide_timeline_save(tide_run_travel.live, tide_run_travel_rate(), player, &size);
    if (!bytes) {
        snprintf(tide_run_travel.said, sizeof tide_run_travel.said, "%s isn't saved: there wasn't enough memory", name);
        return;
    }
    const uint32_t ticks = tide_timeline_last(tide_run_travel.live) - tide_timeline_first(tide_run_travel.live);
#ifdef __wasm__
    tide_web_checkpoint_save(1u, name, bytes, size); // Kept once tide says so (tide_run_checkpoints_poll)
    snprintf(tide_run_checkpoint_saving, sizeof tide_run_checkpoint_saving, "%s", name);
    tide_run_checkpoint_saving_kind = 1;
    snprintf(tide_run_travel.said, sizeof tide_run_travel.said, "Saving %s, %u ticks", name, (unsigned)ticks);
#else
    char why[200];
    if (tide_checkpoint_write(tide_run_checkpoints, name, ".recording", bytes, size, why, sizeof why)) {
        snprintf(tide_run_travel.said, sizeof tide_run_travel.said, "Saved %s, %u ticks", name, (unsigned)ticks);
        printf("tide: %s (%s/%s.recording)\n", tide_run_travel.said, tide_run_checkpoints, name);
        fflush(stdout);
        tide_run_recordings_listed_at = -1.0;
    } else {
        snprintf(tide_run_travel.said, sizeof tide_run_travel.said, "%s isn't saved: %s", name, why);
        fprintf(stderr, "tide: recording %s\n", tide_run_travel.said);
    }
#endif
    free(bytes);
}

// Recording `name` replayed in the match's place, from its first tick, playing: the match this machine runs pauses
// meanwhile, but for other machines' players, for whom it goes on. A recording of another layout isn't replayed.
static inline void tide_run_travel_replay(const char *name)
{
    char why[200] = "";
    uint32_t size = 0;
    uint8_t *bytes = NULL;
#ifdef __wasm__
    const uint32_t coming = tide_web_checkpoint_copy(1u, name, NULL, 0);
    if (!coming) { // Not here yet: asked for, and replayed as it comes (tide_run_checkpoints_poll)
        tide_web_checkpoint_fetch(1u, name);
        snprintf(tide_run_travel.coming, sizeof tide_run_travel.coming, "%s", name);
        snprintf(tide_run_travel.said, sizeof tide_run_travel.said, "Loading %s", name);
        return;
    }
    if (coming == UINT32_MAX) {
        char none;
        tide_web_checkpoint_copy(1u, name, &none, 0);
        snprintf(why, sizeof why, "there's no recording %s in the game's checkpoints folder", name);
    } else if ((bytes = malloc(coming)) != NULL) {
        tide_web_checkpoint_copy(1u, name, bytes, coming);
        size = coming;
    } else {
        snprintf(why, sizeof why, "there wasn't enough memory");
    }
#else
    bytes = tide_checkpoint_read(tide_run_checkpoints, name, ".recording", &size, why, sizeof why);
#endif
    tide_timeline_about about = {0};
    tide_timeline *r = bytes ? tide_timeline_load(&(tide_timeline_desc){.game = tide_run_game->game, .jobs = tide_run_jobs()},
                                                  bytes, size, &about, why, sizeof why)
                             : NULL;
    free(bytes);
    if (!r) {
        snprintf(tide_run_travel.said, sizeof tide_run_travel.said, "%s isn't replayed: %s", name, why);
        fprintf(stderr, "tide: recording %s\n", tide_run_travel.said);
        return;
    }
    tide_run_travel_stop_replay();
    if (!tide_run_travel_background() && !tide_run_travel.paused && !tide_run_travel_why()[0]) { // At the latest
        tide_run_travel.paused = true;
        tide_run_travel.step = false;
        tide_run_travel.shown = tide_run_travel_last();
    }
    tide_run_travel.replay = r;
    tide_run_travel.about = about;
    snprintf(tide_run_travel.replay_name, sizeof tide_run_travel.replay_name, "%s", name);
    tide_run_travel.replay_shown = tide_timeline_first(r);
    tide_run_travel.playing = true;
    tide_run_travel.played = 0.0;
    snprintf(tide_run_travel.said, sizeof tide_run_travel.said, "Replaying %s%s", name,
             about.other_build ? ", recorded by another build: its ticks may go otherwise" : "");
}

// A new build takes over (reload_library.inc's reload_before, while the old one's code is still loaded): one of the
// same layout keeps the recording and a replay, their worlds its to show and free from now on, and the pause;
// another, or a full reload, lets go of them, and the match goes on
static inline void tide_run_travel_reload(const tide_host_game *old, const tide_host_game *next)
{
    if (next && next->game->hash == old->game->hash) {
        tide_timeline_set_game(tide_run_travel.live, next->game);
        tide_timeline_set_game(tide_run_travel.replay, next->game);
        if (tide_run_travel.live) tide_run_travel.game = next->game;
        return;
    }
    tide_run_travel_drop();
}

// What the frame gives the match: its own length, or paused, none but a step's, to just past the next tick; with
// other machines' players, its own length all the same. A recording replayed plays on by the frame's.
static inline float tide_run_travel_seconds(const float seconds)
{
    tide_run_travel_follow();
    tide_timeline *r = tide_run_travel.replay;
    if (r && tide_run_travel.playing) {
        const uint32_t rate = tide_run_travel.about.tick_rate ? tide_run_travel.about.tick_rate : tide_run_travel_rate();
        tide_run_travel.played += tide_is_finite_d((double)seconds) && seconds > 0.0f ? (double)seconds : 0.0;
        if (tide_run_travel.played > 0.25) tide_run_travel.played = 0.25; // A frame that took long doesn't race on
        while (tide_run_travel.played * rate >= 1.0 && tide_run_travel.replay_shown < tide_timeline_last(r)) {
            tide_run_travel.played -= 1.0 / rate;
            if (!tide_timeline_world(r, tide_run_travel.replay_shown + 1u)) {
                tide_run_travel.playing = false;
                break;
            }
            tide_run_travel.replay_shown++;
        }
        if (tide_run_travel.replay_shown >= tide_timeline_last(r)) tide_run_travel.playing = false;
    }
    if (!tide_run_travel.paused || tide_run_travel_background()) return seconds;
    const bool step = tide_run_travel.step && tide_run_travel_shown() == tide_run_travel_last();
    tide_run_travel.step = false;
    if (!step) return 0.0f;
    tide_run_travel.shown = tide_run_travel_last() + 1u; // ...where the frames go, as the step is the latest
    return (float)(tide_session_until_tick(tide_run_session) + 1e-6);
}

// The world the frame's views draw in the match's place: a recording's tick replayed, or one of the match's earlier
// ticks while paused there. NULL for the match as it is.
static inline const void *tide_run_travel_world(void)
{
    if (tide_run_travel.replay) return tide_timeline_world(tide_run_travel.replay, tide_run_travel.replay_shown);
    if (!tide_run_travel.paused || !tide_run_travel.live || tide_run_travel.shown >= tide_run_travel_last()) return NULL;
    return tide_timeline_world(tide_run_travel.live, tide_run_travel.shown);
}

// After a frame of an earlier tick: what its views sent or asked for goes nowhere
static inline void tide_run_travel_discard(void)
{
    const tide_host_game *game = tide_run_game;
    void *local = tide_run_local;
    if (game->command && game->command_sent) {
        uint32_t size;
        for (uint32_t i = 0; i <= tide_game_command_channels(game->game); i++) {
            const uint32_t channel = tide_run_command_channel(i);
            while (game->command(local, channel, &size)) game->command_sent(local, channel);
        }
    }
    tide_session_request request;
    void *start = calloc(1, game->game->start_size ? game->game->start_size : 1u);
    while (start && game->take_request(local, &request, start)) {}
    free(start);
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
    if (tide_run_session) tide_run_checkpoints_poll(); // Before anything of the frame reads the match
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
    // Paused, the match gets no time but a step's (tide_run_travel)
    const float match_seconds = tide_run_travel_seconds(seconds);
    tide_run_frame_now += match_seconds;
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
    game->frame_time(tide_run_local, match_seconds);
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
    // Asked every frame, as a reload can bring the setting or take it away
    tide_session_set_alongside(s, tide_run_alongside || game->game->ticks_alongside);
    tide_session_update(s, tide_run_now);
    { // The ticks that ran beside frames and ended since the last one: the last of them, for the report
        uint32_t ended;
        const uint64_t ns = tide_session_alongside_ns(s, &ended);
        if (tide_run_reporting && ended != tide_run_alongside_ended) tide_profile_report_alongside(&tide_run_report, (double)ns * 1e-6);
        if (profiler && ended > tide_run_alongside_ended) {
            profiler->beside += ended - tide_run_alongside_ended;
            profiler->beside_ms += (double)ns * 1e-6 * (double)(ended - tide_run_alongside_ended);
        }
        tide_run_alongside_ended = ended;
    }
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
    if (tide_run_unblended || tide_run_travel.paused) {
        view.previous = view.verified_previous = NULL;
        view.alpha = view.verified_alpha = 1.0f;
    }
    // Paused at an earlier tick: its world
    const void *earlier = tide_run_travel_world();
    if (earlier) view.current = view.verified = earlier;
    if (earlier && tide_run_travel.replay) { // Its views see the recording's player as this machine's
        const uint32_t player = tide_run_travel.about.player;
        game->set_session(tide_run_local, TIDE_SESSION_CONNECTED, player ? tide_player_from_index((int32_t)player - 1) : (tide_player_id){0},
                          0, true, false, "");
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
    // The views' parallel loops take the pool's threads, as a tick's do (tide/jobs.h)
    const tide_jobs *const loops_before = tide_loops_enter(tide_run_gui.jobs);
    if (game->frame_shards) { // A sharded match's other shards this machine follows, drawn beside its home's
        tide_view_worlds more[TIDE_SESSION_VIEWS];
        uint32_t shards[TIDE_SESSION_VIEWS];
        const uint32_t n = tide_session_views(s, more, shards, TIDE_SESSION_VIEWS);
        game->frame_shards(more, n < TIDE_SESSION_VIEWS ? n : TIDE_SESSION_VIEWS);
    }
    tide_run_dev_views(game);
    tide_run_tweaks_poll(game); // The tweaks' values tide run worked out since: this machine's copy and the match's
    game->frame(match, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha, tide_run_local, &tide_run_draw, &tide_run_gui);
    tide_loops_leave(loops_before);
    tide_run_dev_menu(game, match);
    if (game->voice) {
        const tide_voice_provider *voice = game->voice();
        if (voice) voice->frame(voice->self, s, &tide_run_audio, tide_run_now);
    }
    tide_session_mark_presented(s);
    tide_gui_end(&tide_run_gui, &tide_run_draw);
    if (earlier) tide_run_travel_discard(); // What its views sent or asked for goes nowhere
    tide_profiler_mark(profiler, TIDE_FRAME_VIEWS);
    if (tide_run_reporting) tide_profile_report_mark(&tide_run_report, TIDE_FRAME_VIEWS);
    tide_profiler_draw(profiler, &tide_run_draw, tide_platform_screen_size(), tide_platform_measure_text);
    tide_run_typing(game, tide_gui_typing(&tide_run_gui));
    // The next poll says whether the window holds it. The dev menu frees it while it's open, as a modal does: a game
    // that locks it (a first-person one) would otherwise turn with the mouse and no switch could be clicked
    tide_platform_cursor_lock(tide_gui_cursor_lock(&tide_run_gui) && !tide_run_dev_open);
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
    if (game->assets) tide_platform_pack_ahead(); // Read while the window and the device open
    tide_run_open(desc);
    if (!tide_run_load_shown(NULL)) tide_run_load_assets();
    tide_run_begin_loaded();
    tide_platform_update_while_waiting(tide_run_wait_update, NULL);
    tide_platform_run(tide_run_frame, NULL);
}
