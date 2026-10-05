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
    uint32_t threads;     // What ticks run on
    uint64_t memory;      // Bytes the program has; 0 where the system doesn't say
    const char *renderer; // What draws, like "WebGPU"; NULL for no line
    uint32_t draw_calls;  // ...and in how many draw calls
    // The match's, while there's one
    bool match;
    bool remote;          // Another machine runs it: its ping is worth a glance
    uint32_t ping_ms;     // The round trip to the machine that runs it
    uint32_t delay_ticks; // Ticks the input is delayed by; no line for 0
    double up, down;      // Bytes a second over the network
    int32_t tick;
    uint32_t entities;
    // What its ticks said their systems took, for the three that took longest
    // in the last one; NULL for none
    const tide_profile *profile;
} tide_stats_values;

// The line that's always there, and every line of the list above it.
#define TIDE_STATS_LINES 16

typedef struct tide_stats_line {
    char text[TIDE_PROFILE_NAME + 24];
    uint32_t name; // How many of its first bytes are a system's name, which is cut where the line is too long
} tide_stats_line;

// A finger that touched the drawer.
typedef struct tide_stats_finger {
    int32_t id; // Its touch's; 0 for none
    bool held;  // It hasn't lifted yet
} tide_stats_finger;

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
    bool shown;   // ...if it drew it
    bool hovered; // The mouse's cursor is on it
    bool key_held;        // Its key, at the last frame
    uint8_t buttons_held; // The mouse's buttons at the last frame, a bit each, the left one's first
    uint8_t buttons;      // ...and the ones that went down on the drawer, until a sample is done with each
    tide_stats_finger fingers[TIDE_TOUCHES]; // The fingers that touched it, until a sample is done with each
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
