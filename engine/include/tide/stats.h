#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/devices.h"
#include "tide/draw.h"
#include "tide/profile.h"

// The stats: a few numbers about a running game, in the bottom right corner
// of its screen. They're a drawer. Closed, it's one line, with what's worth a
// glance: the frame rate, and the ping when another machine runs the match.
// A click or a tap on it opens it, and so does the key its host gives it (F8
// in tide/host.h): the rest is listed above that line, which stays where it
// is, so a click in the same place closes it again.
//
// It's a host's, like the profiler (tide/profiler.h), and none of the GUI's
// widgets: it keeps its look whatever the game's theme, and the focus never
// goes to it. A press that lands on it is the drawer's until it's let go,
// wherever it goes meanwhile, and never the game's: the host hides it from
// the game's input and from the frame's GUI and views (tide_stats_hide). A
// press that began elsewhere stays the game's when it crosses the drawer.
//
// A frame of a host that shows it:
//
//     tide_platform_poll(&devices);
//     tide_stats_input(&stats, &devices, screen, devices.keyboard.f8.held);
//     ...
//     tide_devices sampled = devices; // The game's input
//     tide_gui_hide(&gui, &sampled);
//     tide_stats_hide(&stats, &sampled);
//     ...
//     tide_devices seen = devices; // What the GUI and the views read
//     tide_stats_hide(&stats, &seen);
//     tide_gui_begin(&gui, &seen, screen, tide_platform_measure_text);
//     ...
//     tide_platform_draw(&draw);
//     tide_stats_collect(&stats, values.profile, seconds);
//     tide_draw_reset(&over);
//     tide_stats_draw(&stats, &values, &over, screen, tide_platform_measure_text);
//     tide_platform_draw_over(&over);
//
// Nothing here measures anything: a host says what it knows
// (tide_stats_values), and needs to know less while the drawer is closed.

// What a host knows of its game. Closed, the drawer shows `fps`, and
// `ping_ms` of a match another machine runs: the rest is for while it's open
// (tide_stats.open), so a host measures it only then.
typedef struct tide_stats_values {
    double fps;           // Frames a second
    double work_ms;       // What a frame's work takes on its thread; under 0 while it isn't known
    double render_ms;     // The CPU's rendering work per frame
    double gpu_ms;        // The GPU's rendering work per frame, when its backend can measure it
    bool render_known;
    bool gpu_known;
    uint32_t threads;     // What ticks run on
    uint64_t memory;      // Bytes the program has; 0 where the system doesn't say
    uint64_t gpu_memory;  // Bytes the renderer has on the GPU, by its own count; 0 for no line
    const char *renderer; // What draws, like "WebGPU"; NULL for no line
    uint32_t draw_calls;  // ...and in how many draw calls
    uint64_t vertices;    // Submitted vertices, counting indices and instances in every pass
    // The match's, while there's one
    bool match;
    bool remote;          // Another machine runs it: its ping is worth a glance
    uint32_t ping_ms;     // The round trip to the machine that runs it
    uint32_t delay_ticks; // Ticks the input is delayed by; no line for 0
    double up, down;      // Bytes a second over the network
    uint32_t entities;
    // The profile collected by tide_stats_collect; NULL for no system lines
    const tide_profile *profile;
} tide_stats_values;

// The line that's always there, and every line of the list above it.
#define TIDE_STATS_LINES 16

// A line of the list is a label on the left and a value on the right, in two
// columns, so the eye finds a number by its row: `cpu` and `2.1 ms`, a
// system's name and what it took. The line that's always there is one text,
// its label (`60 fps   ping 34 ms`), with no value.
typedef struct tide_stats_line {
    char label[TIDE_PROFILE_NAME]; // Cut from its end where the screen is too narrow; a value never is
    char value[32];
    bool indent; // Under the line before it, as a part of it: `render` under `cpu`
    bool gap;    // A little space before it: the first line of a group
    bool arrows; // The value is two numbers, two spaces apart, drawn with an up arrow before the first and a down arrow before the second
} tide_stats_line;

// A finger that touched the drawer.
typedef struct tide_stats_finger {
    int32_t id; // Its touch's; 0 for none
    bool held;  // It hasn't lifted yet
} tide_stats_finger;

// A system's measurements since the drawer last took stock, and the counters
// they came from. Names are copied: the game's library may have been replaced.
typedef struct tide_stats_system {
    char name[TIDE_PROFILE_NAME];
    double from_ms, total_ms;
    uint64_t from_count, count;
} tide_stats_system;

typedef struct tide_stats_time {
    char name[TIDE_PROFILE_NAME];
    double ms;
} tide_stats_time;

// A zeroed one is ready to use, closed.
typedef struct tide_stats {
    bool open; // The whole list shows, not the one line: a host sets it to start open
    // What the last frame showed: [0] is the line that's always there, and
    // the others the list above it, from the top, as many as the screen had
    // room for
    tide_stats_line lines[TIDE_STATS_LINES];
    uint32_t line_count;
    // Where the last frame drew it, in the screen's pixels from the top left,
    // out to the screen's corner: the pointer there is on the drawer
    tide_rect rect;
    float held;   // The widest the panel has been since it opened: it only grows while open, so its edge stays put
    bool shown;   // ...if it drew it
    bool hovered; // The mouse's cursor is on it
    bool key_held;        // Its key, at the last frame
    uint8_t buttons_held; // The mouse's buttons at the last frame, a bit each, the left one's first
    uint8_t buttons;      // ...and the ones that went down on the drawer, until a sample is done with each
    tide_stats_finger fingers[TIDE_TOUCHES]; // The fingers that touched it, while each has its slot
    // The three slowest systems' average per measurement. Their names and
    // values stay until another half-second of measurements is collected, and
    // a system keeps its row while it stays among the three: one that joins
    // them takes the row of the one it replaces, so the rows don't shuffle.
    tide_stats_time slow[3];
    uint32_t slow_count;
    double span;
    const tide_profile *profile;
    tide_stats_system *systems;
    uint32_t system_count, system_room;
} tide_stats;

// Once a frame, after the devices were polled (the pointer too:
// tide_pointer_poll) and before anything reads them: what the player did with
// the drawer. `screen` is the window's size in pixels, and `key` whether the
// drawer's key is down. A press of the key opens the drawer or closes it, and
// so does a click or a tap: the mouse's left button or a finger that went down
// on it and is let go on it. True when it did.
bool tide_stats_input(tide_stats *s, const tide_devices *devices, tide_float2 screen, bool key);

// Whether a press is on the drawer, which tide_stats_hide hides.
bool tide_stats_pressed(const tide_stats *s);

// Hides the presses that are the drawer's from `devices`, a copy that the
// game's input is about to be sampled from, or that the frame's GUI and views
// are about to read: the mouse's buttons that went down on it, the fingers
// that touched it, and the pointer's press when it's one of those.
void tide_stats_hide(const tide_stats *s, tide_devices *devices);

// Once a frame while shown, after its ticks: collect the profile's new system
// measurements, and take stock every half-second of `seconds`. Frames with no
// tick add no samples. Call once with 0 seconds before ticking when the drawer
// opens, to exclude what the profile measured while it was closed. NULL means
// there's no match: any old system lines go. Closing or switching the drawer
// off also drops its measurements. The profiler may clear its own totals.
void tide_stats_collect(tide_stats *s, const tide_profile *profile, double seconds);

// Lets its memory go: it's zeroed, and ready to use again.
void tide_stats_free(tide_stats *s);

// Adds the drawer to `list`, over what's there, in the bottom right corner of
// a screen of `screen` pixels, and remembers where: its one line, and above
// it, while it's open, as much of the list as the screen has room for.
// `measure` is how big the renderer draws text (tide_platform_measure_text);
// NULL guesses.
void tide_stats_draw(tide_stats *s, const tide_stats_values *values, tide_draw_list *list, tide_float2 screen,
                     tide_measure_fn measure);

// The drawer isn't shown this frame: the pointer where it was is the game's
// again. Whether it's open stays.
void tide_stats_off(tide_stats *s);
