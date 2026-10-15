#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/audio.h"
#include "tide/devices.h"
#include "tide/draw.h"
#include "tide/gui.h"
#include "tide/io.h"
#include "tide/jobs.h"
#include "tide/net.h"

// The platform layer: a window, the frame loop, input devices and a renderer
// for draw lists, on OpenGL (3.3, ES 3 or WebGL 2) or, on the web, WebGPU.
// Hosts use it; the simulation never does. This header includes no system
// header, nor GLFW's, and a file that includes a game's generated header
// shouldn't either: their names would clash with the game's components
// (Transform, Camera, Rectangle...).
//
// Pixels here are the display's logical pixels, as CSS pixels are on the web:
// on a display scaled to 150%, one is 1.5 of the screen's real pixels wide.

typedef struct tide_window_desc {
    const char *title;
    // The starting size in pixels. The user can resize and maximize the window;
    // on the web, the canvas fills the page instead.
    int width;
    int height;
    // For automated tests. On desktop, no visible window. On the web, frames
    // run on timers, because headless browsers have no animation frames.
    bool hidden;
} tide_window_desc;

// Opens the window and its graphics context, and the system's audio output.
//
// In a run nobody watches (TIDE_HIDDEN in the environment: tide_hidden,
// tide/page.h), no window shows on a desktop, whatever asks for one. A window
// that's `hidden` opens as ever, as a headless one does, but for its sound:
// nobody listens either, so it opens no output device and no microphone, and
// what tide_platform_audio is handed goes unheard, as on a machine with
// neither. One that isn't `hidden` has only its closing to end its run, which
// nobody could do, so the program ends here instead, with exit code 2, having
// said why: a program that ends by itself opens its window `hidden`, or
// headless.
void tide_platform_open(const tide_window_desc *desc);

// A headless window: none on screen, for pictures of what a game draws and
// for runs nobody watches (an agent's, CI's). It's what tide/run.h opens for
// a game started with --capture, --frames or --script. Nothing about it
// depends on this machine, so a run repeats exactly, on any:
//
// - Frames draw into pixels of its own, at the size asked for and `scale` of
//   them to a logical pixel, whatever this machine's display is.
//   tide_platform_frame_pixels reads them, and tide_platform_capture writes
//   them as a PNG.
// - Time is made up: every frame is a 60th of a second long, however long it
//   took, and frames come as fast as the machine runs them.
// - The devices are made up too, never the system's: tide_platform_poll takes
//   what tide_platform_input said and what the script says, as it would take
//   a player's input. The cursor is locked whenever the game asks, and
//   nothing reaches the system's clipboard.
//
// tide_platform_run ends the program with exit code 0 once `frames` frames
// drew or the script's `quit` line says, whichever is first, with 1 when a
// picture couldn't be written, and as ever with what the frame function
// returns.
typedef struct tide_headless_desc {
    int width, height; // In logical pixels; default 960 x 540
    float scale;       // Pixels to a logical one, each way, as a display scaled to 150% has 1.5; default 1
    uint32_t frames;   // The run ends once this many drew; 0 for when its script quits
    const char *capture; // The file the run's last frame goes to, as a PNG; NULL for none
    // The file everything the run played goes to, as a WAV (tide_platform_capture_audio); NULL for none
    const char *capture_audio;
    // A WAV or Ogg Vorbis file the microphone plays in the run, looped, where
    // a real one's device would be heard (Clip.Microphone); NULL for a silent one
    const char *microphone;
    // A file of input by frame, of pictures to take and of when the run ends:
    // lines like `120 key d down`, `400 capture fight.png` and `500 quit`
    // (docs/tools/cli.md, Scripts, has every kind); NULL for none. A file it
    // names is beside it.
    const char *script;
    // A run of several players (tide_platform_players, below): how many, each
    // a process of its own that the first starts; 0 for a run that isn't one.
    // The first is given the program's command line, `argc` and `argv`, and
    // starts the others with it and `--player <player> <hub>` after it, which
    // their descs say: which player the process is, from 2, and the port the
    // first listens on for them.
    uint32_t players;
    uint32_t player, hub;
    int argc;
    char **argv;
} tide_headless_desc;

// Opens a headless window, in tide_platform_open's place. Ends the program
// with exit code 2, having said why, when the script can't be read or a line
// of it is wrong (which it names), or when nothing ends the run. Desktop only.
void tide_platform_open_headless(const tide_headless_desc *desc);

// Made-up input for a headless window, one event as a script's line has it
// after its frame: "key d down", "mouse 640 360", "button left", "text K7QF2M".
// The next tide_platform_poll takes it. False, having said why, for what
// isn't an event, or when the window isn't headless.
bool tide_platform_input(const char *event);

// The frame a headless window last drew (tide_platform_draw, and what was
// drawn over it), whole: `*width` by `*height` pixels of 4 bytes each (red, green,
// blue, alpha), rows from the top, which stay until the next call. A window
// shows no alpha, and what's in it here is what drawing left there. NULL
// before the first frame, and for a window that isn't headless.
const uint8_t *tide_platform_frame_pixels(int *width, int *height);

// Writes that frame to the file at `path` as a PNG, without its alpha, and
// says so on stdout. False, having said why, when there's no frame or the
// file can't be written; folders aren't made for it.
bool tide_platform_capture(const char *path);

// What a frame says should be sounding (tide/audio.h): a host hands it over
// once a frame, after the frame drew, and the mixer plays it on the system's
// audio thread. A headless window has no device: its mixer runs all the same,
// as many blocks a frame as a 60th of a second takes at TIDE_AUDIO_RATE, and
// keeps what it played for tide_platform_capture_audio. Nothing of a clock's
// is in that, so a headless run plays the same bytes on every machine.
void tide_platform_audio(const tide_audio_frame *frame);
// Also: while the frame says a voice plays the microphone (`frame->microphone`),
// the capture device is kept open and its frames reach the mixer (audio_in.h),
// tried again every second where it couldn't be opened (a player who hasn't
// allowed it yet); when no frame does, it's closed. A headless run has no
// device: `tide_headless_desc.microphone` plays a file into it instead. A
// window in a run nobody watches (TIDE_HIDDEN) has none either, and its
// microphone is silent.

// Writes what a headless window played since the last such call (or since the
// run began) to the file at `path`, as a WAV of 16-bit stereo PCM, and says
// so on stdout. False, having said why, when the window isn't headless or
// the file can't be written. `tide_headless_desc.capture_audio` writes the
// whole run as it ends, whatever these took out of it.
bool tide_platform_capture_audio(const char *path);

// Stops every sound at once and has the mixer let go of every clip and
// effect, before what they point at goes: a host calls it before it unloads
// a game's library. Returns once the mixer has.
void tide_platform_audio_forget(void);

// Returned by a frame function to keep going. Any other value ends the program
// with that exit code.
#define TIDE_KEEP_RUNNING (-1)

// Runs once per display frame; what it draws shows when it returns.
// `seconds` is the time since the previous frame started.
typedef int (*tide_frame_fn)(void *user, float seconds);

// Runs frames until one returns an exit code or the window is closed (exit
// code 0), then closes the window and ends the program. Never returns, because
// on the web the browser drives the frames: main can't wait for the loop.
// Frames go on while the window is minimized or, on the web, the page is in a
// tab in the background, so a match goes on for the other players. They draw
// nothing then, and come when the frame function asks (tide_platform_next_frame).
_Noreturn void tide_platform_run(tide_frame_fn frame, void *user);

// While nobody sees the window, the next frame comes `seconds` after this one
// started: when the host next has work, such as its match's next tick
// (tide_session_until_tick). Called from the frame function; the soonest of
// several calls counts, and a frame that makes none has the next one in a
// 60th of a second. Frames others see come at the display's pace.
void tide_platform_next_frame(double seconds);

// Updates the devices with the input since the previous frame. Call once per
// frame, before sampling input. The characters typed are only the ones since
// this call's previous one.
//
// Keys are read by physical position on every platform. Axes follow Unity: y
// is positive up for sticks and the mouse, and mouse position is in window
// pixels from the bottom left.
//
// `mouse.locked` is whether the window holds the cursor (see
// tide_platform_cursor_lock). While it does, the mouse's position stays where
// it was, and its delta is the mouse's own movement.
void tide_platform_poll(tide_devices *devices);

// Whether the game wants the cursor locked (tide_gui_cursor_lock), once a
// frame: hidden and kept in the window, so the mouse turns a camera without
// reaching the screen's edge. Asking isn't having: the system decides, and
// tide_platform_poll says whether the window holds it (`mouse.locked`).
//
// - A desktop window holds the cursor while it has the focus. Alt+Tab lets it
//   go, and coming back takes it again while this is still asked. Escape lets
//   it go too, as in a browser: the player's next click on the window or press
//   of another key takes it again, or the game asking again once it stopped.
// - A browser gives it only when asked from a click or a key press, so the
//   page asks at the next one on the canvas while it's wanted (and at once,
//   which works shortly after one). The browser lets the cursor go itself when
//   the player presses Escape, which the page can't prevent, and when the page
//   loses the focus or is hidden. For a moment after Escape it refuses, and
//   nothing asks again on its own: the next click or key press does.
// - Android never holds it, yet.
//
// So a game reads whether it's locked rather than assuming, and usually shows
// its pause menu when the lock goes away.
void tide_platform_cursor_lock(bool lock);

// Renders a frame's draw list (see tide/draw.h), such as the one
// tide_frame fills. Call from the frame function.
void tide_platform_draw(const tide_draw_list *list);

// Where a world position lands on screen, in window pixels from the top left,
// through the camera the last tide_platform_draw ended with. For tools and
// tests.
tide_float2 tide_platform_world_to_screen(tide_float2 world);

// Whether the player is typing into the GUI (tide_gui_typing), once a frame:
// on phones, and in their browsers, the system's keyboard shows while they
// are. Does nothing elsewhere.
void tide_platform_typing(bool typing);

// Puts `text` on the system's clipboard. A browser takes it only shortly
// after the player clicked or pressed a key, as when a button is pressed.
void tide_platform_copy(const char *text);

// Draws `text` in the window's bottom right corner, over the frame, each of
// its lines against the right edge. For debug overlays such as frame rates.
void tide_platform_draw_overlay(const char *text);

// Draws `list` over the frame tide_platform_draw drew, and leaves what that
// frame left as it is: its camera (tide_platform_world_to_screen) and its
// vertex and draw-call counts. For what a host shows over a game, like its stats
// (tide/stats.h), which aren't the game's drawing.
void tide_platform_draw_over(const tide_draw_list *list);

// Frames per second, averaged over the last frames.
int tide_platform_fps(void);

// What the open window draws with: "WebGPU", "WebGL 2", "OpenGL 3.3" or
// "OpenGL ES 3". The web draws with WebGPU where the browser has it on a GPU,
// and with WebGL 2 where it doesn't.
const char *tide_platform_renderer(void);

// Measure the GPU work of tide_platform_draw, including its render targets,
// while the stats drawer is open. Overlays and the wait for display aren't
// included. Disabling discards pending samples and releases query resources.
void tide_platform_gpu_timing(bool enabled);

// A newly completed GPU sample, in milliseconds; -1 while none is ready or
// the backend has no timer. Never waits for the GPU. Results arrive later
// than the frame measured, and each is returned once.
double tide_platform_gpu_ms(void);

// Local files, independent of a window or renderer. The app ID identifies
// saves across versions. Reinitializing the same ID preserves the provider
// and pending storage operations, including when the game hot reloads.
// Relative paths are under persistent_data_path; native absolute paths use
// the operating system's permissions. Browser paths stay in its app's roots.
const tide_io *tide_platform_io_init(const char *app_id);
// The game this program runs, by the same app ID, for a game that never uses
// files too: what names the folder the renderer keeps what it compiled in
// across runs (Vulkan's pipelines), the game's temporary cache folder, which
// is only made once something's kept there. tide_platform_io_init names it too.
void tide_platform_name_game(const char *app_id);

// How many draw calls the last list drawn took (tide_platform_draw), aside
// from what was drawn over it (tide_platform_draw_overlay,
// tide_platform_draw_over): a run of shapes, of text, or of one mesh's
// instances is one.
uint32_t tide_platform_draw_calls(void);

// Vertices submitted by the last list drawn, with the same scope as draw
// calls above: all render-target passes, without the host's overlays. Each
// index counts once per instance, including repeated indices; this isn't a
// count of unique vertices or of GPU vertex-shader invocations after caching.
// Includes shapes, text, blits and clear triangles, even if clipped or culled.
uint64_t tide_platform_draw_vertices(void);

// How much memory the program has, in bytes, as the system counts it: what it
// keeps in RAM or, on the web, its WebAssembly memory, which only grows. 0
// where the system doesn't say.
uint64_t tide_platform_memory(void);

// What the renderer has on the GPU, in bytes, by its own count: every buffer,
// texture, cube and render target it asked the backend for, by what each
// holds (a texture's smaller images a third more), less what it freed. Not
// the window's own framebuffer, the pipelines or what the driver keeps
// beside them: the same number on every backend, for what a game's draws
// cost. 0 before anything drew, and again when the device was lost.
uint64_t tide_platform_gpu_memory(void);

// How many bytes of textures' pixels the renderer has sent the GPU since the
// program started, by its own count: all of a texture that's new, or whose
// writer says nothing of what changed, and of one that says, the pixels that
// did (tide_draw_texture_edit). For tools and tests: what a frame adds to it
// is what the frame's textures cost to keep up to date.
uint64_t tide_platform_gpu_written(void);

// The game's pack of assets (tide/pack.h), as its build left it with the
// program: beside it as <program>.pack on desktop, in its page on the web and
// in its APK on Android. malloc'd bytes, the caller's to free, or NULL for
// none. The standard host loads it before the game runs (tide/host.h).
uint8_t *tide_platform_pack(size_t *size);

// 64 bits from the system's random number generator, which nobody could guess:
// what a session makes the seeds of the matches it starts from
// (tide_session_desc.entropy), so no two go the same. 0 if the system gives
// none. A host whose runs have to repeat gives its session none, as
// tide/run.h's does for a run with no window, and for one whose command line
// picks the seed (--seed).
uint64_t tide_platform_entropy(void);

// The window's size in pixels, which tide_gui_begin takes.
tide_float2 tide_platform_screen_size(void);

// How big `text` is drawn `size` tall with the platform's font, in the same
// units, logical pixels: its widest line's width and its lines' height, on
// the window's pixels (NULL) or on those of a render target of the size
// `target` says, as the window's display has them, in the weight `font` is
// the file of: TIDE_FONT_REGULAR or TIDE_FONT_BOLD. It's the tide_measure_fn
// that tide_gui_begin takes (tide/draw.h), and needs no window open.
tide_float2 tide_platform_measure_text(const char *text, float size, const tide_target_size *target,
                                       const unsigned char *font);

// Renders `list` offscreen at the window's size, then reads the pixels at
// `points` (window pixels from the top left) as 0xRRGGBBAA. For tests: proves
// the whole drawing path works without a visible window. Its scale is its
// own, 1, whatever the display's: a logical pixel is one of its pixels, and
// so it is in the list's render targets that are at the display's scale.
void tide_platform_read_pixels(const tide_draw_list *list, const tide_float2 *points, int count, uint32_t *rgba);

// The same, as a window of `width` by `height` logical pixels would draw the
// list on a display scaled `scale`: into that many pixels times the scale,
// which `points` are in, from the top left. The list's render targets take
// that window's size and that display's scale, so tests see what a scaled
// display or a window of another size does on any machine, with no window
// resized. Text measures as it's drawn there through
// tide_platform_measure_text with a target of that size and scale.
void tide_platform_read_pixels_as(const tide_draw_list *list, int width, int height, float scale,
                                  const tide_float2 *points, int count, uint32_t *rgba);

// Networking for sessions (tide/session.h): a UDP transport, taking datagrams
// on `port` (0: any free one). False where there's no network, like the web
// for now, or when the port is taken.
bool tide_platform_udp_open(uint16_t port, tide_transport *out);
// The same, reachable from this machine only: for tests, which then open no
// port to the network (and on Windows, ask nothing of the firewall).
bool tide_platform_udp_open_local(uint16_t port, tide_transport *out);

// An address from "192.168.1.5", "localhost:7777" or a name: its IPv4 address,
// with `default_port` if it says none. False if there's none.
bool tide_platform_resolve(const char *text, uint16_t default_port, tide_address *out);

// Rooms: matches players find by a code, like "K7QF2M", through the relay
// (relay/). The relay only introduces players; their datagrams go straight
// between them, on WebRTC: the browser's on the web, and our own on desktop
// (platform/src/rtc), so web and desktop players meet in the same rooms. A
// program is in one room at a time: opening one closes the last. Desktop
// games reach the relay at wss://relay.tide-engine.dev, with the system's
// TLS (on Linux, OpenSSL's libssl), or at $TIDE_RELAY.
//
// A room this machine hosts: a transport that takes the players who join it.
// It picks the room's code itself, so it has one at once.
bool tide_platform_room_host(tide_transport *out);
// Joins the room with `code`: a transport to its host, at `server`.
bool tide_platform_room_join(const char *code, tide_transport *out, tide_address *server);
// The room's code into `out`, "" while there's none: the room closed or
// failed, or its host can't reach the relay (it opens again once it can).
// It changes only if another room had it first, before anyone could join.
void tide_platform_room_code(char *out, size_t size);
// The room joined turned out not to be one, or its host couldn't be reached.
bool tide_platform_room_failed(void);
// The ping the session measures to the room's host, in milliseconds, which a
// host says every frame (tide_client_status.ping_ms: -1 for none, when this
// machine hosts the room or isn't in its match yet), and what an answer takes
// to be read (answer_ms). A page told to play at a slow connection goes by
// them (the page's `Tide.lag`, platform/web/tide.js): its lag is the ping its
// player is to have, so it adds what this one lacks. Nothing on desktop.
void tide_platform_room_ping(int32_t ping_ms, uint32_t answer_ms);
// For tests and measurements of the netcode in a page: whether the page wants
// the session's numbers (its `Tide.netcode`, a function it gives), and the
// numbers, which a host then says every frame: `count` of them, in the order
// tide/host.h has them (tide_run_netcode). Never wanted on desktop.
bool tide_platform_netcode_wanted(void);
void tide_platform_netcode(const double *values, uint32_t count);
// The key of the room this machine hosts into `out`, "" for none: what lets
// the players of its match meet there again when it goes (see
// tide_session_set_room). Up to TIDE_ROOM_KEY_LENGTH letters and digits.
void tide_platform_room_key(char *out, size_t size);
// Host migration (see tide_session_migrating): goes to room `code` again,
// with its `key`. If its host is gone, the relay makes this machine the
// room's host; if not, or another machine got there first, it introduces this
// one to the host.
void tide_platform_room_migrate(const char *code, const char *key);
// ...and which it was, once the relay says: 1, this machine hosts the room
// now, and `out` takes the players who join it; 2, it joins the room's host,
// whom `out` reaches at `server`; -2, the match ended there (its host ended
// it, see tide_transport.end); -1, the room can't be reached; 0, not yet.
int tide_platform_room_migrated(tide_transport *out, tide_address *server);

// Takes the players who join a match this machine hosts: on UDP `port` (0 for
// any), where there's UDP, and in a room, in one transport. False only if
// there's neither.
bool tide_platform_host_open(uint16_t port, tide_transport *out);

// A run of several players with no window (tide_headless_desc.players): how
// many it has, 0 for any other run, and which of them this process is, from 1.
// Each is a process of its own, as on a machine of its own, and they go a
// frame at a time together, on a network that's made up as the run's time and
// devices are: a datagram sent in a frame is read in the next, by whoever it's
// for, in the same order every run. No port opens and no relay is asked, so a
// run of several repeats exactly, as a run of one does. A host goes by what's
// below in such a run, in the place of what's above.
uint32_t tide_platform_players(void);
uint32_t tide_platform_player(void);
// Takes the players who join a match this process hosts, in
// tide_platform_host_open's place: in its room, and on `port`.
bool tide_platform_players_open(uint16_t port, tide_transport *out);
// Joins the room with `code`, in tide_platform_room_join's place. False for a
// code that's no player's of the run. A room nobody has open fails at once
// (tide_platform_players_failed), as the relay says of a room that isn't one.
bool tide_platform_players_join(const char *code, tide_transport *out, tide_address *server);
// A transport to the match at `address`, in tide_platform_resolve's and
// tide_platform_udp_open's place: the first player's whose match is open on
// its port. Its host says nothing there, and no name is looked up. One
// nobody has open never answers, as an address nobody is at.
bool tide_platform_players_connect(const char *address, uint16_t default_port, tide_transport *out, tide_address *server);
// The code of the room this process's match is in, hosted or joined, in
// tide_platform_room_code's place: "" for none.
void tide_platform_players_code(char *out, size_t size);
// ...and of player `player`'s room, whether its match is open or not: six of
// a character that's the player's own, `AAAAAA` for the first and `BBBBBB`
// for the second. "" for a player the run doesn't have.
void tide_platform_players_room(uint32_t player, char *out, size_t size);
// The room joined has nobody's match open in it, in tide_platform_room_failed's place.
bool tide_platform_players_failed(void);

// Threads to run ticks on (tide/jobs.h): a pool with one for each of the
// CPU's cores, the caller's among them, made on first use. NULL with one core.
// On the web, its threads are workers, which only a cross-origin isolated page
// has (NULL elsewhere), started the first time a tick has work for them.
const tide_jobs *tide_platform_jobs(void);
// Where ticks on the pool say how long each system took (tide/profile.h), or
// NULL to stop. Nothing without a pool (one core, or a page without threads):
// a host gives its own tide_jobs of one thread a profile there.
void tide_platform_jobs_profile(tide_profile *profile);
