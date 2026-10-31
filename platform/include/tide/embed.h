#pragma once

// Tide inside another program's frame loop (Unity's, say), with no window, GPU
// or audio device of its own: the standard host's loop (tide/host.h), which
// the other program calls once a frame, handing it what its own window read,
// and whose sound it plays (tide_embed_audio).
// Everything else is the standard host's: the session, rooms, dedicated
// servers, host migration, local requests, commands and local handlers.
//
// One at a time: the host's state is the process's.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/devices.h"
#include "tide/math.h"
#include "tide/network.h"
#include "tide/session.h"

typedef struct tide_host_game tide_host_game;
typedef struct tide_embed tide_embed;

// What another language binds against: a change to anything here that it lays
// out or calls changes the number, and tide_embed_abi says it.
#define TIDE_EMBED_ABI 9u
uint32_t tide_embed_abi(void);

typedef struct tide_embed_desc {
    const tide_host_game *game; // A game linked in: tide/run.h's tide_host_game_api, or NULL with library_dir
    const uint8_t *pack;        // ...its pack of assets (tide_game_pack), copied, or NULL for a game with none
    size_t pack_size;
    int tick_rate;              // Ticks per second, over the game's tickRate setting; 0 for that
    int argc;                   // As main's (argv[0] the program): tide/run.h's --host, --join, --connect, --seed
                                // and --data-dir, --stats, and a sharded game's --shards and --shard-width; one
                                // that's wrong is left out, saying so
    char **argv;
    const char *game_name;      // Its folders' name when the game sets no appId; NULL for "game"
    const char *relay;          // Another relay's address; NULL for TIDE_RELAY or the default
    // ...or the folder tide writes the game's libraries to (game-<n>, pack-<n>: tide unity), desktop only: the newest
    // is the game, and each newer one is swapped into the running match, as tide run's host does
    const char *library_dir;
} tide_embed_desc;

// One finger's event this frame, as tide/devices.h's tide_touch_event takes it.
typedef struct tide_embed_touch {
    uint32_t phase;  // tide_touch_phase
    uint32_t source; // The system's number for the finger while it touches
    float x, y;      // Logical pixels from the top left, y down
} tide_embed_touch;

#define TIDE_EMBED_TOUCHES 32u

// What the other program's window read this frame, as a window of Tide's own
// would say it: the platform turns it into Devices, latching presses and
// adding up movement until a tick samples them, as for any window.
typedef struct tide_embed_input {
    uint8_t keys[16];       // Held keys, a bit each (1 << i % 8 in keys[i / 8]), in tide_keyboard's order
    float mouse_x, mouse_y; // Where the cursor is: logical pixels from the top left, y down
    float wheel_x, wheel_y; // This frame's scroll, y positive away from the user
    float moved_x, moved_y; // This frame's movement while the cursor is locked, y down
    uint32_t mouse_buttons; // A bit each, in tide_mouse's order: left, right, middle, back, forward
    bool mouse_locked;      // The window holds the cursor (Cursor.locked)
    bool gamepad;           // A gamepad is connected
    bool touchscreen;       // This machine has a touchscreen
    uint8_t pad_;
    float left_x, left_y, right_x, right_y; // Its sticks, -1 to 1, y down as gamepads report it
    float left_trigger, right_trigger;      // 0 to 1
    uint32_t gamepad_buttons; // A bit each, in tide_gamepad's order, the d-pad's after
    uint32_t text_count;      // Characters typed this frame, as Unicode code points
    uint32_t text[TIDE_TEXT_MAX];
    uint32_t touch_count;     // This frame's touch events, in order
    tide_embed_touch touches[TIDE_EMBED_TOUCHES];
} tide_embed_input;

_Static_assert(sizeof(tide_embed_input) == 212 + 16 * TIDE_EMBED_TOUCHES && TIDE_TEXT_MAX == 32u,
               "tide_embed_input has no padding the compiler adds, and the size other languages lay out");

// The session as tide_session_status says it, flat, with no padding the
// compiler adds, so another language lays it out the same on every platform.
typedef struct tide_embed_status {
    uint32_t state;          // tide_session_state
    uint32_t reason;         // tide_disconnect_reason, once it's offline again
    uint32_t player;         // This machine's player (tide_player_id.id: index + 1), 0 for none
    uint32_t ping_ms;        // The network's round trip to the server
    uint32_t verified_tick;  // Ticks the server has confirmed
    uint32_t predicted_tick; // Ticks run, predicted ones included
    uint32_t delay_ticks;    // Ticks this machine's input is delayed by
    uint32_t resyncs;        // Times its world diverged and the server sent it again
    uint64_t sent_bytes;     // Bytes sent and received over networks so far
    uint64_t received_bytes;
    uint8_t server;          // This machine runs the server
    uint8_t open;            // ...and other machines can join it
    uint8_t typing;          // The GUI is typing into a field: a phone shows its keyboard
    uint8_t cursor_locked;   // The game asks for the cursor locked (Cursor.Lock)
    char room[TIDE_NETWORK_ADDRESS_BYTES]; // The code others join the match by, as Session.room says; "" for none
    char message[TIDE_MESSAGE_BYTES];      // A kick's message, once kicked
    uint8_t microphone;      // The game listens to the microphone (Clip.Microphone): tide_embed_microphone, please
    uint8_t tide_pad[3];
} tide_embed_status;

_Static_assert(sizeof(tide_embed_status) == 52 + TIDE_NETWORK_ADDRESS_BYTES + TIDE_MESSAGE_BYTES + 4,
               "tide_embed_status has no padding the compiler adds");

// Starts the game, and the match its command line asks for or Main's, as
// tide/run.h does. NULL when one runs already, `desc` names no game, or its
// library_dir has no build of one that loads (it says why).
tide_embed *tide_embed_create(const tide_embed_desc *desc);
void tide_embed_destroy(tide_embed *e);

// One frame: the devices from `input`, the session's update, connection
// events, local handlers, local requests and commands, then the views, for a
// screen of `width` by `height` logical pixels at `scale` pixels to one.
// `seconds` is the frame's length: the session's clock is their sum. Returns
// the seconds until the match next needs a frame.
double tide_embed_frame(tide_embed *e, float seconds, const tide_embed_input *input, float width, float height, float scale);

void tide_embed_status_of(const tide_embed *e, tide_embed_status *out);

// The frame's view packet (tide/embed_game.h says its bytes): what the game's
// views saw, blended between ticks. Valid until the next frame; NULL for a game
// built without it (tidec --embed).
const uint8_t *tide_embed_view(const tide_embed *e, uint32_t *size);

// The game's schema, as JSON: its types as its C lays them out (tide/layout.h),
// which are the packet's and the commands', with what they are, its enums,
// commands, prefabs and scenes. Valid until a new build comes in; NULL for a game
// built without tidec's --embed.
const char *tide_embed_schema(const tide_embed *e, uint32_t *size);

// What the frame's views drew (tide/draw.h's draw list), for the other program
// to draw in its own window, as bytes: this header, then its arrays, each from
// a multiple of 8 bytes, in this order: the commands (tide_draw_command), the
// smooth shapes' rest (tide_draw_shape_data), the meshes' corners (tide_vertex)
// and triangles (uint32_t indices), the commands' text (`text` bytes, each
// command's from its offset to a NUL), the matrices of 3D cameras and instances
// (tide_float4x4) and turned text's transforms (tide_float2x2). The sizes say
// how big this build's structs are, for a reader to check. Valid until the next
// frame.
#define TIDE_EMBED_DRAW_MAGIC 0x44444954u // "TIDD"
#define TIDE_EMBED_DRAW_VERSION 1u

typedef struct tide_embed_draw_header {
    uint32_t magic, version;
    uint32_t commands, shapes, vertices, indices, text, matrices, turns;
    uint32_t command_size, shape_size, vertex_size;
    float screen_width, screen_height; // The screen the views drew for, in logical pixels...
    float screen_scale;                // ...and pixels to one of them
    uint32_t unused;
} tide_embed_draw_header;

_Static_assert(sizeof(tide_embed_draw_header) == 64, "tide_embed_draw_header has no padding the compiler adds");

const uint8_t *tide_embed_draw(const tide_embed *e, uint32_t *size);

// What the frame's views drew, drawn by Tide's renderer as it draws for a
// window of its own, but into the other program's: the GPU calls it made, as
// commands for that program to replay with its GPU API (Unity's
// TideRender.cs does, where it has compute-class GPUs; tide_embed_draw is for
// the others). Shapes, text, meshes, 3D, render targets, stencil, blits and
// the game's frags and verts all come out of it, the same pixels as Tide
// draws itself.
//
// The calls are platform/src/gpu.h's, which says what each does. Each
// command is a uint32_t op (tide_embed_render_op), a uint32_t count of the
// bytes after it (a multiple of 4), then those bytes: uint32_ts, but where
// it says otherwise, and bytes padded with zeros to a multiple of 4. An object
// (a buffer, a texture, a cube, a pipeline or a target) is a number from 1
// that the commands give out as they make it, and it lives until a command
// frees it, across frames, until tide_embed_render_reset. Target 0 is the
// other program's own: what the frame draws over, never cleared. The pointer
// is valid until the next call.
typedef enum tide_embed_render_op {
    // id, kind (tide_gpu_buffer_kind: 0 vertices, 1 indices), size, given (0 or 1), then `size` bytes if given
    TIDE_EMBED_RENDER_BUFFER = 1,
    TIDE_EMBED_RENDER_BUFFER_WRITE, // id, offset, size, then `size` bytes
    TIDE_EMBED_RENDER_BUFFER_FREE,  // id
    // id, format (tide_gpu_format: 0 RGBA8, 1 R8, 2 R16F, 3 RGBA32I), width, height, given, then its pixels if
    // given, rows from the top
    TIDE_EMBED_RENDER_TEXTURE,
    TIDE_EMBED_RENDER_TEXTURE_WRITE, // id, x, y, width, height, then that part's pixels, its rows one after another
    TIDE_EMBED_RENDER_MIPMAPS,       // id: a texture's, a target's color texture's or a cube's
    TIDE_EMBED_RENDER_TEXTURE_FREE,  // id: a texture's or a cube's
    // id, color (tide_gpu_color), size, given, then if given its six faces' pixels, 4 bytes each, +x, -x, +y, -y, +z, -z
    TIDE_EMBED_RENDER_CUBE,
    TIDE_EMBED_RENDER_CUBE_FACE, // target id, cube id, face, its depth texture's id
    // id, its shaders' key as two uint32_ts (low first: FNV-1a's 64 bits of its SPIR-V words, or of its WGSL where
    // it has none, which tide unity names the shader it writes by), slot_count, 3 slots of stride and per_instance,
    // attribute_count, 16 attributes of slot, format and offset, uniform_size, texture_count, texture_count reads, blend,
    // depth, cull, stencil_test, stencil_op, into, outputs, alpha_to_coverage, then its name's length and bytes
    TIDE_EMBED_RENDER_PIPELINE,
    TIDE_EMBED_RENDER_PIPELINE_FREE, // id
    // id, format (tide_gpu_target_format), width, height, then the ids of its 4 color textures (0 past its colors)
    // and of its depth texture (0 for a multisampled one, till RESOLVE_DEPTH)
    TIDE_EMBED_RENDER_TARGET,
    TIDE_EMBED_RENDER_TARGET_FREE,    // id: a target's or a cube face's
    TIDE_EMBED_RENDER_RESOLVE_DEPTH,  // target id, its depth texture's id
    TIDE_EMBED_RENDER_BEGIN,          // target (0 for the other program's), clear
    TIDE_EMBED_RENDER_END,
    TIDE_EMBED_RENDER_SCISSOR,        // x, y, width, height: pixels from the target's top left
    TIDE_EMBED_RENDER_USE,            // pipeline
    TIDE_EMBED_RENDER_STENCIL,        // value
    TIDE_EMBED_RENDER_UNIFORMS,       // size, then `size` bytes
    TIDE_EMBED_RENDER_SAMPLE,         // slot, texture, filter (tide_gpu_filter), wrap (tide_gpu_wrap)
    TIDE_EMBED_RENDER_VERTICES,       // slot, buffer, offset
    TIDE_EMBED_RENDER_INDICES,        // buffer, wide (32 bits, or 16)
    TIDE_EMBED_RENDER_DRAW,           // first, count, instances
    TIDE_EMBED_RENDER_DRAW_INDEXED,   // count, instances
} tide_embed_render_op;

const uint8_t *tide_embed_render(tide_embed *e, uint32_t *size);

// ...and the frame's 3D again, through another camera of the other program's
// (its editor's scene view): only the 3D drawn on its own target, each through
// this camera (as tide_embed_camera takes one) whatever camera drew it, onto a
// target `width` by `height` pixels; its 2D, clips and render targets are the
// frame's alone. The commands go on from tide_embed_render's, the same objects
// and their numbers: replayed after them, into that camera. Valid until the
// next call of either; NULL for a camera or size that isn't one.
const uint8_t *tide_embed_render_view(tide_embed *e, const float *transform, const float *projection, int width, int height,
                                      uint32_t *size);

// The other program's GPU objects are gone (it started over): the next
// tide_embed_render makes everything it draws with again, numbering from 1.
void tide_embed_render_reset(tide_embed *e);

// A command from the other program, as local code's Send: `type` its type's
// place in the schema, sent to the entity `target_index`/`target_generation`
// (zeros for none: entity.Send against Send), `value` its bytes as the schema
// lays them out; a command's text and lists follow its value, as the view
// packet's STRINGS lays them out (tide/embed_game.h), each field holding its
// entry's place + 1. False for a type that's no command, bytes that aren't its,
// text that isn't UTF-8, or a command too big to send.
bool tide_embed_command(tide_embed *e, uint32_t type, uint32_t target_index, uint32_t target_generation, const void *value,
                        uint32_t size);

// What local code's Session calls do: TIDE_REQUEST_START (`text` a scene's
// name in the schema, or NULL for Main's), _JOIN (`text` a room's code), _CONNECT (`text` an address, with
// `port`), _OPEN (on `port`, and in a room), _CLOSE, _LEAVE, _END and
// _KICK_ALL (`text` its message). False for a kind it doesn't take, or a scene
// the game hasn't.
bool tide_embed_request(tide_embed *e, tide_session_request_kind kind, const char *text, uint32_t port);

// The camera the frame's views start with, as the program Tide is in draws its
// scene, as tide_draw_set_camera_matrices takes one: `transform` places it in
// the world (it looks along its +z, +y up: Unity's camera transform, unscaled),
// and `projection` takes what it sees, looking down -z, to OpenGL's clip
// space (Unity's Camera.projectionMatrix), each 16 floats, column by column.
// Draw.WorldToScreen, the 3D draws and their depth then agree with that
// program's picture, perspective or orthographic, near and far planes too; a
// view's own Draw.Camera is still its own. NULL for either, or a value that
// isn't finite, goes back to Tide's own first camera, which views start with
// until this is called.
void tide_embed_camera(tide_embed *e, const float *transform, const float *projection);

// The frame's sound, for the other program to play: `frames` frames of
// `channels` (1 to 8, interleaved) at `rate` into `out`, from Tide's mixer as a
// device of Tide's own is fed (its stereo resampled to the rate, a mono
// program's the two averaged, more channels the two and silence). From one
// thread at a time, which may be the other program's audio thread (Unity's
// OnAudioFilterRead), where everything else here is its frame's thread's; it
// says no more calls come from there with NULL and 0 frames, which a reload
// then no longer waits for, and makes sure none comes during
// tide_embed_destroy. Silence for no embed. On the web Tide's program plays
// through the page's audio itself, and hears the page's microphone.
void tide_embed_audio(tide_embed *e, float *out, uint32_t frames, uint32_t rate, uint32_t channels);

// What the other program's microphone heard since the last call, while the
// status says the game listens (`microphone`): `frames` frames of `channels`
// (1 to 8, interleaved) at `rate`, which voices of Clip.Microphone play. From
// the frame's thread.
void tide_embed_microphone(tide_embed *e, const float *samples, uint32_t frames, uint32_t rate, uint32_t channels);

// ---------------------------------------------------------------------------
// Time travel: a debugger's, for a match this machine runs (an editor's Play).
// A match is deterministic and its worlds are cheap to keep, so the embed
// keeps the worlds of its latest ticks and what went into every tick since the
// match started (its server's journal: inputs, joins and leaves, commands).
// Paused, the match runs no ticks and its local code waits, while the frames
// show a tick of it: the latest, or any kept one, through the views, the view
// packet and the drawing as the other program reads them. A recording is
// those ticks as bytes, which replay on any machine with a build of the same
// data layout, each tick's world checked against the hash the server's had.
//
// Only a match this machine runs: tide_embed_travel says why not otherwise.
// With players of other machines in it, pausing never stops it for them: the
// match goes on behind what's shown (`background`), this machine's player as
// if it let go of everything, and going on shows its latest tick again. A
// frame that shows another tick than the latest runs the views on a local
// state of their own, so the match's is as it was when it goes on, and what's
// pressed or sent there never reaches the match.

typedef enum tide_embed_travel_state {
    TIDE_TRAVEL_OFF,    // Nothing kept: tide_embed_travel_keep wasn't given a time
    TIDE_TRAVEL_NONE,   // No match it can pause (`why` says why), and no recording shown
    TIDE_TRAVEL_LIVE,   // The match runs, recorded
    TIDE_TRAVEL_PAUSED, // The match is paused, showing `shown`: or goes on behind it, with `background`
    TIDE_TRAVEL_REPLAY, // Showing a recording (tide_embed_replay); the match, if any, is paused
} tide_embed_travel_state;

#define TIDE_EMBED_TRAVEL_TEXT 128u

typedef struct tide_embed_travel {
    uint32_t state;      // tide_embed_travel_state
    uint32_t first;      // The first tick shown (the recording's start; tick n is the world after n ticks)...
    uint32_t kept;       // ...the first shown at once, the ones before made again from the start...
    uint32_t last;       // ...and the last: the match's latest, or the recording's last
    uint32_t shown;      // The tick the frames show
    uint32_t tick_rate;  // Ticks a second
    // Replaying: ticks replayed and checked so far; the first whose world wasn't the recording's, and its hashes
    // (the recording's `expected`, this build's `got`); the first this build couldn't replay at all; 0 for none
    uint32_t checked;
    uint32_t differs;
    uint32_t unreadable;
    uint8_t playing;     // Replaying: it goes on at the tick rate (tide_embed_pause false)
    uint8_t other_build; // The recording was made by a build whose simulation was another
    uint8_t background;  // Players of other machines are in the match: it goes on behind what's shown
    uint8_t tide_pad[1];
    uint64_t hash;       // The shown world's
    uint64_t expected, got;
    char why[TIDE_EMBED_TRAVEL_TEXT];     // Why it can't pause the match now, "" when it can
    char refused[TIDE_EMBED_TRAVEL_TEXT]; // Why the last recording given to replay wasn't, "" when it was
} tide_embed_travel;

_Static_assert(sizeof(tide_embed_travel) == 64 + 2 * TIDE_EMBED_TRAVEL_TEXT,
               "tide_embed_travel has no padding the compiler adds");

void tide_embed_travel_of(const tide_embed *e, tide_embed_travel *out);

// Keeps the worlds of the last `seconds` of ticks, and records the match from
// its start, or from now for one that runs; 0 (the default) keeps nothing,
// letting go of what it kept and of a recording shown.
void tide_embed_travel_keep(tide_embed *e, float seconds);

// Pauses the match (true) where it can, or goes on (false) from its latest
// tick, whatever was shown. Showing a recording, it stops or plays that.
// False when there's nothing to pause or play.
bool tide_embed_pause(tide_embed *e, bool paused);

// Paused: one tick on. At the latest, the match runs its next tick at the
// next frame, with this machine's input then; before it (or with the match
// going on behind), the next tick is shown. A recording shows its next tick.
// False when it can't.
bool tide_embed_step(tide_embed *e);

// Paused or replaying: shows the world after `tick` ticks, from first to last.
// False outside them, or for a tick this build can't replay.
bool tide_embed_show(tide_embed *e, uint32_t tick);

// Paused: the match goes back to the tick shown and on from there, the ticks
// after it let go, for players of other machines too, whose worlds are sent
// again (tide_session_rewind). Which commands are in it stays as it was, so
// none comes twice. The recording goes on from there too. False when it's
// not paused, or showing a recording.
bool tide_embed_continue(tide_embed *e);

// The match's recording, from its start: bytes for tide_embed_replay, on this
// machine or another. Valid until the next call; NULL with nothing recorded.
const uint8_t *tide_embed_recording(tide_embed *e, uint32_t *size);

// Shows a recording (copied), from its first tick, paused: the match, if any,
// pauses until tide_embed_replay_end. Its ticks are made again with this
// build as they're shown, each world checked against the recording's. False,
// saying why in tide_embed_travel's `refused`, for bytes that aren't a
// recording, one of another data layout, or a match that can't pause.
bool tide_embed_replay(tide_embed *e, const uint8_t *data, uint32_t size);

// Back to the match from a recording, paused where it was.
void tide_embed_replay_end(tide_embed *e);

// The app went to the background (true) or came back. A phone leaves its match
// there, which hands a match it hosted over where host migration is on, rather
// than going on unseen; desktop changes nothing.
void tide_embed_background(tide_embed *e, bool background);
