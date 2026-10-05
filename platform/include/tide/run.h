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
//
// A game also runs with no window, to see what it draws without one on
// screen, or in CI (tide_platform_open_headless). Time is made up, a 60th of
// a second a frame, and so is input, so a run repeats exactly on any machine:
//
//     game --frames 300 --capture menu.png    300 frames, the last of them to a PNG
//     game --script play.txt                  input by frame from a file, which also says
//                                             when to capture and quit (docs/tools/cli.md)
//     game --capture big.png --frames 60 --scale 2
//                                             as on a display scaled to 200%
//
// Such a run ends with exit code 0 at --frames or the script's `quit`, with
// 1 when a picture can't be written, and with 2, having started nothing,
// when a flag or a line of the script is wrong. Desktop only.
//
// The loop itself is in tide/host.h, which runs the game through
// tide_host_game_api, made here from the game's header.

#include "tide/host.h"

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
                                  void *local, tide_draw_list *draw, tide_gui *gui)
{
    tide_frame(world, previous, alpha, verified, local, draw, gui);
}

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
static inline uint32_t tide_run_command(const void *local, uint8_t *out, const uint32_t capacity)
{
    return tide_local_command(local, out, capacity);
}

static inline void tide_run_command_sent(void *local)
{
    tide_local_command_sent(local);
}
#endif

static inline void tide_run_set_session(void *local, const uint32_t state, const tide_player_id player,
                                        const uint32_t ping, const bool server, const bool open, const char *room)
{
    tide_local_set_session(local, state, player, ping, server, open, room);
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
}

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
    .frame_time = tide_run_frame_time,
    .take_request = tide_run_take_request,
#ifdef TIDE_HAS_COMMANDS
    .command = tide_run_command,
    .command_sent = tide_run_command_sent,
#endif
    .set_session = tide_run_set_session,
    .connected = tide_run_connected,
    .disconnected = tide_run_disconnected,
    .tick = tide_run_tick,
    .entity_count = tide_run_entity_count,
    .unload = tide_run_unload,
    .rebind_resources = tide_run_rebind_resources,
#ifdef TIDE_HAS_VIEW_CORRECTIONS
    .correct_views = tide_run_local_correct,
#endif
};

_Noreturn static inline void tide_run(const tide_run_desc *desc)
{
    tide_host_run(desc, &tide_host_game_api);
}
