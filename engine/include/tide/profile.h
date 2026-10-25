#pragma once

#include <stdint.h>

#include "tide/time.h"

// Profiling: how long a tick's systems and a frame's views take, for a host
// that shows it (the standard host's profiler, tide/profiler.h). The host
// gives one to what ticks run on (tide_jobs.profile) and to the GUI its views
// run with (tide_gui.profile), and ticks and frames fill it. Without one,
// nothing is measured, and nothing reads the clock.
//
// Each time is the last one measured, and a total since the profile was last
// cleared, so a host shows them over as many ticks and frames as it likes.
// The measurement counters survive a clear: independent readers can average
// the same profile over different intervals without clearing each other's data.
// Names are copied, as a game's code may be swapped out under `tide run`
// between a tick and a frame that shows them.
#define TIDE_PROFILE_NAME 48
typedef struct tide_profile_time {
    char name[TIDE_PROFILE_NAME];
    double ms;    // The last time it ran
    double total; // ...and every time since the profile was cleared, in milliseconds
    double measured_ms;    // Every measurement of this name, independent of clears
    uint64_t measurements; // ...and how many, including ones below the clock's resolution
} tide_profile_time;

typedef struct tide_profile_times {
    tide_profile_time *items;
    uint32_t count;
    uint32_t room;
} tide_profile_times;

// A zeroed one is ready to use.
typedef struct tide_profile {
    // A tick's systems, in the tick's order: each from its first task's start
    // to its last task's end, so systems that ran at the same time on several
    // threads add up to more than the tick took.
    tide_profile_times systems;
    tide_profile_times views; // A frame's views, in the frame's order
    // Local worker queue delay and elapsed computation, published on the frame
    // thread after provider completion. These intervals can overlap each other
    // and frames; they are not parts to add to a frame's time.
    tide_profile_times workers;
    uint64_t worker_runs; // Completed measured calculations since clear
    // Since it was cleared:
    uint32_t ticks;    // Ticks that ran
    double systems_ms; // ...their systems, all together
    double changes_ms; // ...and what follows them in a tick: structural changes, events' handlers and tasks
} tide_profile;

// The totals start over, and the ticks' count.
void tide_profile_clear(tide_profile *p);

// Lets its memory go: it's zeroed, and ready to use again.
void tide_profile_free(tide_profile *p);

// ---------------------------------------------------------------------------
// For what fills one: ticks (tide/jobs.h) and generated code

// The clock, where there's a profile, and 0 without one.
static inline uint64_t tide_profile_now(const tide_profile *p)
{
    return p ? tide_time_now_ns() : 0u;
}

// `times` has `count` of them from now on. Another count than it had is
// another build of the game: what it had goes.
void tide_profile_count(tide_profile_times *times, uint32_t count);

// The one at `index`, named `name`, ran from `from_ns` to `to_ns`. Under
// another name than it had, it starts over.
void tide_profile_add(tide_profile_times *times, uint32_t index, const char *name, uint64_t from_ns, uint64_t to_ns);

// A tick ran: its systems from `began_ns` to `systems_done_ns`, and the rest
// of it from then until now.
void tide_profile_tick(tide_profile *p, uint64_t began_ns, uint64_t systems_done_ns);

// View `index` of a frame's `count`, named `name`, ran from `began_ns` until
// now. Returns now: when the next one begins.
uint64_t tide_profile_view(tide_profile *p, uint32_t index, uint32_t count, const char *name, uint64_t began_ns);
