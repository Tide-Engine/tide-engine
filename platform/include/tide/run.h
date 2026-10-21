#pragma once

// Runs a Tide game in a window. Include it after the game's generated
// header, in the file with main:
//
//     #include "game.h"
//     #include "tide/run.h"
//
//     int main(int argc, char **argv)
//     {
//         tide_run(&(tide_run_desc){.title = "Sandbox", .argc = argc, .argv = argv});
//     }
//
// The game runs in a session (tide/session.h). If its Main scene is the
// match's, a match starts right away on this machine, or it joins another's:
//
//     game --host [port]      the match opened, for others to join (port 7777 by default)
//     game --join code        the match in the room with this code, like K7QF2M
//     game --connect address  the match at "192.168.1.5", "localhost:7777" and the like
//
// An open match is in a room, whose code players join it with, and on the
// desktop on a UDP port too. If Main is local, the program starts in it, with
// no match, and local code starts one with Session.Start (and Session.Open),
// Join or Connect (--join and --connect work too). This machine's
// player joins before the match's first tick. If the game has an input, the
// devices are sampled once per tick this machine runs, minus what the GUI is
// using. The views draw every frame, and their GUI over the world. Close the
// window to quit.
//
//     game --size 1280 720    the window's size, in logical pixels (960 by 540 without it)
//     game --debug-views      F7 shows the profiler
//     game --seed 7           the seed of the matches this machine starts (Time.seed), from 0 to
//                             2147483647, in place of another one from the system each match
//
// A game also runs with no window, to see what it draws without one on
// screen, or in CI (tide_platform_open_headless). Time is made up, a 60th of
// a second a frame, and so is input, and its matches start with seed 0
// unless --seed says another, so a run repeats exactly on any machine:
//
//     game --frames 300 --capture menu.png    300 frames, the last of them to a PNG
//     game --script play.txt                  input by frame from a file, which also says
//                                             when to capture and quit (docs/tools/cli.md)
//     game --capture big.png --frames 60 --scale 2
//                                             as on a display scaled to 200%
//     game --frames 300 --capture-audio run.wav
//                                             everything the run played, as a WAV
//     game --script play.txt --seed 7         the same input, in a match that rolls otherwise
//
// Such a run ends with exit code 0 at --frames or the script's `quit`, with
// 1 when a picture can't be written, and with 2, having started nothing,
// when a flag or a line of the script is wrong. Desktop only.
//
// ...and with several players, each a copy of the program with no window,
// which the first starts (with --player after its own command line). They go
// a frame at a time together, on a network that's made up as time and input
// are (tide_platform_players), so the run repeats exactly as a run of one
// does. If Main is the match's, the first opens it and the others join it;
// if it's local, the game's own code does, at the script's keys and clicks:
//
//     game --players 2 --script play.txt      the script's lines say whose they are
//                                             (`120 player 2 key d down`), and a picture
//                                             has its player's number (fight-2.png)
//
// Where nobody watches (TIDE_HIDDEN in the environment: tide/page.h), a game
// shows no window, so a run with one is refused the same way, with exit code
// 2 and what to give it instead.
//
// The loop itself is in tide/host.h, which runs the game through
// tide_host_game_api, made here from the game's header.

#include "tide/host.h"
#include "tide/pack.h"

#ifdef TIDE_HAS_EMBED // The game was built for another program to run (tidec --embed): what it generated for that, and its layout
#include "tide/embed_game.h"
extern const tide_embed_game tide_game_embed;
extern const tide_layout tide_game_layout;
#endif

static inline void tide_run_local_init(void *local)
{
    tide_local_init(local);
}

static inline void tide_run_set_io(void *local, const tide_io *io)
{
    tide_local_set_io(local, io);
}

#ifdef TIDE_HAS_INPUT
static inline void tide_run_sample_input(const tide_devices *devices, const void *local, void *input)
{
    *(tide_input *)input = tide_input_sample(devices, local);
}
#endif

static inline void tide_run_views(const void *world, const void *previous, const float alpha, const void *verified,
                                  const void *verified_previous, const float verified_alpha,
                                  void *local, tide_draw_list *draw, tide_gui *gui)
{
    tide_frame(world, previous, alpha, verified, verified_previous, verified_alpha, local, draw, gui);
}

#ifdef TIDE_HAS_AUDIO
static inline void tide_run_audio_set(void *local, tide_audio_frame *frame)
{
    tide_local_audio(local, frame);
}
#endif

static inline void tide_run_events(void *local, tide_event_log *events)
{
    tide_local_events(local, events);
}

static inline void tide_run_frame_time(void *local, const float seconds)
{
    tide_local_frame_time(local, seconds);
}

#ifdef TIDE_HAS_VIEW_CORRECTIONS
static inline void tide_run_local_correct(void *local, const tide_view_correction *correction)
{
    tide_local_correct(local, correction);
}
#endif

static inline bool tide_run_take_request(void *local, tide_session_request *request, void *start)
{
    return tide_local_take_request(local, request, start);
}

#ifdef TIDE_HAS_COMMANDS
static inline const uint8_t *tide_run_command(const void *local, const uint32_t channel, uint32_t *size)
{
    return tide_local_command(local, channel, size);
}

static inline void tide_run_command_sent(void *local, const uint32_t channel)
{
    tide_local_command_sent(local, channel);
}
#endif

static inline void tide_run_set_session(void *local, const uint32_t state, const tide_player_id player,
                                        const uint32_t ping, const bool server, const bool open, const char *room)
{
    tide_local_set_session(local, state, player, ping, server, open, room);
}

static inline void tide_run_set_invite(void *local, const char *invite)
{
    tide_local_set_invite(local, invite);
}

static inline void tide_run_connected(void *local)
{
    tide_local_connected(local);
}

static inline void tide_run_disconnected(void *local, const uint32_t reason, const char *message)
{
    tide_local_disconnected(local, reason, message);
}

static inline int32_t tide_run_tick(const void *world)
{
    return ((const tide_world *)world)->Time.tick;
}

static inline uint32_t tide_run_entity_count(const void *world)
{
    return tide_world_entity_count(world);
}

static inline void tide_run_unload(void)
{
    tide_render_targets_free();
    tide_scratch_free();
#ifdef TIDE_HAS_ASSETS
    tide_pack_unload();
#endif
#ifdef TIDE_HAS_AUDIO
    tide_clips_free(); // Once the host had the mixer let go of them (tide_platform_audio_forget)
#endif
    tide_pages_free();
}

#ifdef TIDE_HAS_ASSETS
static inline bool tide_run_assets(const uint8_t *pack, const size_t size)
{
    return tide_assets_load(pack, size);
}
#endif

static inline void tide_run_local_free(void *local)
{
    tide_local_free(local);
}

static inline uint32_t tide_run_local_pack(const void *local, uint8_t *out, const uint32_t capacity)
{
    return tide_local_pack(local, out, capacity);
}

static inline bool tide_run_local_unpack(void *local, const uint8_t *data, const uint32_t size)
{
    return tide_local_unpack(local, data, size);
}

static inline void tide_run_rebind_resources(void *local, const bool same_memory)
{
    tide_local_rebind_resources(local, same_memory);
}

// The game as hosts run it.
static const tide_host_game tide_host_game_api = {
    .game = &tide_game_api,
    .local_size = sizeof(tide_local),
#ifdef TIDE_MAIN_IS_LOCAL
    .main_is_local = true,
#endif
    .local_init = tide_run_local_init,
#ifdef TIDE_HAS_IO
    .set_io = tide_run_set_io,
#endif
    .local_free = tide_run_local_free,
    .local_pack = tide_run_local_pack,
    .local_unpack = tide_run_local_unpack,
#ifdef TIDE_HAS_INPUT
    .sample_input = tide_run_sample_input,
#endif
    .frame = tide_run_views,
    .events = tide_run_events,
#ifdef TIDE_HAS_AUDIO
    .audio = tide_run_audio_set,
#endif
    .frame_time = tide_run_frame_time,
    .take_request = tide_run_take_request,
#ifdef TIDE_HAS_COMMANDS
    .command = tide_run_command,
    .command_sent = tide_run_command_sent,
#endif
    .set_session = tide_run_set_session,
    .set_invite = tide_run_set_invite,
    .connected = tide_run_connected,
    .disconnected = tide_run_disconnected,
    .tick = tide_run_tick,
    .entity_count = tide_run_entity_count,
    .unload = tide_run_unload,
#ifdef TIDE_HAS_ASSETS
    .assets = tide_run_assets,
#endif
    .rebind_resources = tide_run_rebind_resources,
#ifdef TIDE_HAS_VIEW_CORRECTIONS
    .correct_views = tide_run_local_correct,
#endif
    .network = tide_network_get,
#ifdef TIDE_HAS_EMBED
    .layout = &tide_game_layout,
    .embed = &tide_game_embed,
#endif
};

_Noreturn static inline void tide_run(const tide_run_desc *desc)
{
    tide_host_run(desc, &tide_host_game_api);
}
