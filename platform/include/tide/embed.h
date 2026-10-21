#pragma once

// Tide inside another program's frame loop (Unity's, say), with no window, GPU
// or audio device of its own: the standard host's loop (tide/host.h), which
// the other program calls once a frame, handing it what its own window read.
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
#define TIDE_EMBED_ABI 3u
uint32_t tide_embed_abi(void);

typedef struct tide_embed_desc {
    const tide_host_game *game; // A game linked in: tide/run.h's tide_host_game_api, or NULL with library_dir
    const uint8_t *pack;        // ...its pack of assets (tide_game_pack), copied, or NULL for a game with none
    size_t pack_size;
    int tick_rate;              // Ticks per second, over the game's tickRate setting; 0 for that
    int argc;                   // As main's (argv[0] the program): tide/run.h's --host, --join, --connect, --seed
                                // and --data-dir; one that's wrong is left out, saying so
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
    uint8_t tide_pad[4];
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

// A command from the other program, as local code's Send: `type` its type's
// place in the schema, sent to the entity `target_index`/`target_generation`
// (zeros for none: entity.Send against Send), `value` its bytes as the schema
// lays them out. False for a type that's no command, a size that isn't its, or
// one that holds text or lists, which bytes from another program can't.
bool tide_embed_command(tide_embed *e, uint32_t type, uint32_t target_index, uint32_t target_generation, const void *value,
                        uint32_t size);

// What local code's Session calls do: TIDE_REQUEST_START (`text` a scene's
// name in the schema, or NULL for Main's), _JOIN (`text` a room's code), _CONNECT (`text` an address, with
// `port`), _OPEN (on `port`, and in a room), _CLOSE, _LEAVE, _END and
// _KICK_ALL (`text` its message). False for a kind it doesn't take, or a scene
// the game hasn't.
bool tide_embed_request(tide_embed *e, tide_session_request_kind kind, const char *text, uint32_t port);

// The camera the frame's views start with, as the program Tide is in draws its
// scene: where it is (x, y, z), which way it faces (a quaternion x, y, z, w,
// in Tide's axes: +y up, +z forward) and its vertical field of view in
// degrees. Draw.WorldToScreen and the 3D draws then agree with that program's
// picture; a view's own Draw.Camera is still its own. A field of view that
// isn't between 0 and 180, or a value that isn't finite, goes back to Tide's
// own first camera, which views start with until this is called.
void tide_embed_camera(tide_embed *e, float x, float y, float z, float qx, float qy, float qz, float qw,
                       float field_of_view);

// The app went to the background (true) or came back. A phone leaves its match
// there, which hands a match it hosted over where host migration is on, rather
// than going on unseen; desktop changes nothing.
void tide_embed_background(tide_embed *e, bool background);
