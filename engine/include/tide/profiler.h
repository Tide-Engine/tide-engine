#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/draw.h"
#include "tide/gui.h"
#include "tide/profile.h"

// The profiler: where a frame's time and a tick's time go, as a panel drawn
// over the game. It's one of the standard host's debug views (tide/host.h
// shows it at F7 where they're on), and any host can show it:
//
//     tide_profiler_begin(&profiler, seconds); // The frame starts
//     tide_platform_poll(&devices);
//     tide_profiler_mark(&profiler, TIDE_FRAME_INPUT); // ...and so on, part by part
//     ...
//     tide_profiler_draw(&profiler, &draw, screen, tide_platform_measure_text);
//     tide_platform_draw(&draw);
//     tide_profiler_mark(&profiler, TIDE_FRAME_RENDER);
//     tide_profiler_end(&profiler, again_ticks);
//
// A frame's parts are what the host marks. The views in it and the ticks'
// systems are what the game's code says, into `profile`, while the host gives
// that to the GUI (tide_gui.profile) and to what ticks run on
// (tide_jobs.profile).
//
// The panel shows averages over about half a second, so its numbers hold still
// long enough to read: a frame's parts in milliseconds a frame, and a tick's
// in milliseconds a tick.

// What a host's frame spends its time on.
typedef enum tide_frame_part {
    TIDE_FRAME_INPUT,  // Polling the devices
    TIDE_FRAME_MATCH,  // The session's update: the match's ticks, and its netcode
    TIDE_FRAME_VIEWS,  // tide_frame and the GUI: the views, and local tasks
    TIDE_FRAME_RENDER, // Drawing the frame's list
    TIDE_FRAME_OTHER,  // Whatever else the host does in a frame, up to tide_profiler_end
    TIDE_FRAME_PARTS,
} tide_frame_part;

// What the renderer's CPU spends drawing a frame's list on (tide_platform_render_parts), within TIDE_FRAME_RENDER.
typedef enum tide_render_part {
    TIDE_RENDER_PREPARE, // Levels of detail, skinning's poses, the destinations, GPU lists' reads begun
    TIDE_RENDER_STEPS,   // The list's commands turned into the renderer's steps (shapes, text and 2D meshes laid out)
    TIDE_RENDER_UPLOADS, // Pipelines found, textures, meshes, instances and GPU lists sent to the GPU
    TIDE_RENDER_CULLING, // Culled runs' and occlusion's kernels set going, before any pass
    TIDE_RENDER_RECORD,  // The passes' draws recorded through the backend, with the kernels and copies between them
    TIDE_RENDER_FINISH,  // Read-backs, spares and textures let go
    TIDE_RENDER_PARTS,
} tide_render_part;

// A line of the panel.
typedef struct tide_profiler_row {
    char label[TIDE_PROFILE_NAME];
    char note[48];  // After the label, dimmer: "60 fps"
    char value[16]; // Against the right edge: "1.25 ms"
    float share;    // Of its column's whole, 0 to 1: how long its bar is
    uint32_t depth; // How far it's indented
} tide_profiler_row;

#define TIDE_PROFILER_ROWS 24

// A zeroed one is ready to use.
typedef struct tide_profiler {
    // What ticks and views fill while the host gives it to them. The profiler
    // clears it each time it takes stock.
    tide_profile profile;
    uint64_t mark_ns; // When the frame started, or its last part ended
    // Since the panel last took stock
    double span; // Seconds
    uint32_t frames;
    double longest; // The longest frame, in seconds
    double part_ms[TIDE_FRAME_PARTS];
    uint32_t again_from; // The session's count of predicted ticks run again, as it was then
    bool again_known;
    // Ticks that ran beside frames and ended since (tide_session_alongside_ns), which the host adds: how many, and
    // what they took on their own thread. None of it is in the frame's parts.
    uint32_t beside;
    double beside_ms;
    // What the host says of its memory (tide_platform_memory, tide_platform_gpu_memory), as of the last frame: the
    // frame's last row, where either is above 0
    uint64_t memory, gpu_memory;
    // What the panel shows: the frame's rows, then the tick's
    tide_profiler_row rows[2][TIDE_PROFILER_ROWS];
    uint32_t row_count[2];
} tide_profiler;

// Every function takes NULL for no profiler, and does nothing then: a host
// calls them whether or not its profiler is shown.

// Starts over: what it measured goes, and the panel says so until it has
// measured again. For when it's shown again after a while.
void tide_profiler_reset(tide_profiler *p);

// A frame starts, `seconds` after the one before it did.
void tide_profiler_begin(tide_profiler *p, float seconds);

// What the frame did since it started, or since its last mark, was `part`.
void tide_profiler_mark(tide_profiler *p, tide_frame_part part);

// The frame is over. `again_ticks` is the session's count of predicted ticks
// it ran again (tide_client_status.again_ticks). About twice a second, the
// panel takes stock here: its rows are what was measured since it last did.
void tide_profiler_end(tide_profiler *p, uint32_t again_ticks);

// Adds the panel to `list`, over what's there, in the top left corner of a
// screen of `screen` pixels: the frame's rows and the tick's side by side, or
// one under the other where the screen is narrow. `measure` is how big the
// renderer draws text (tide_platform_measure_text); NULL guesses.
void tide_profiler_draw(const tide_profiler *p, tide_draw_list *list, tide_float2 screen, tide_measure_fn measure);

// Lets its memory go: it's zeroed, and ready to use again.
void tide_profiler_free(tide_profiler *p);
