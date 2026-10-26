#pragma once

// What every way of versus_rollback shares: its options, the parts its ticks
// are timed in, and what a period does.
//
// A way's tick in versus terms is a period, a frame of a client that
// predicts: `every` ticks, each with the world before it kept in a ring of
// `kept` and the world after it hashed, and then a correction: the world
// `back` ticks ago restored from the ring and those ticks run again, each kept
// again as it starts, as a client keeps the ticks it predicts again, and the
// world at the end hashed and checked against the hash it had the first time.
// The ticks run again with what they had the first time, so they end where
// they did, and every way ends with the same world, and the same ring.

#include <stdint.h>

#include "versus.h"

// main.c's flags, in order
enum { FLAG_MOVERS, FLAG_TURRETS, FLAG_MOVING, FLAG_SPREAD, FLAG_NOTES, FLAG_KEPT, FLAG_BACK, FLAG_EVERY, FLAG_COUNT };

// What each way times of a period: running ticks (the first time and again),
// keeping worlds, hashing them, and restoring one
enum { PART_TICK, PART_SNAPSHOT, PART_HASH, PART_RESTORE, PART_COUNT };

typedef struct plan {
    uint32_t movers, turrets;
    uint32_t moving; // Of each hundred movers, how many move: 100 at most
    uint32_t first;  // How many of the first movers move, when they aren't spread
    uint32_t spread; // 1: every hundred movers' first `moving` move, rather than the first `first` of them all
    uint32_t notes;  // The list's length: each turret counts its shots in its slot of it
    uint32_t kept;   // Worlds in the ring
    uint32_t back;   // Ticks a correction goes back, as many as are kept at the most
    uint32_t every;  // Ticks between corrections: a period
} plan;

static inline plan plan_of(const versus_flag *f)
{
    plan p = {f[FLAG_MOVERS].value,
              f[FLAG_TURRETS].value,
              f[FLAG_MOVING].value < 100u ? f[FLAG_MOVING].value : 100u,
              0u,
              f[FLAG_SPREAD].value != 0u,
              f[FLAG_NOTES].value,
              f[FLAG_KEPT].value,
              f[FLAG_BACK].value,
              f[FLAG_EVERY].value};
    p.first = (uint32_t)((uint64_t)p.movers * p.moving / 100u);
    if (p.back > p.kept) p.back = p.kept;
    return p;
}

// How far a correction goes back once `ticks` have run: never past the first.
static inline uint32_t plan_back(const plan *p, const uint32_t ticks)
{
    return p->back < ticks ? p->back : ticks;
}

// Each mover and turret as the digest adds it up.
typedef struct mover_item {
    float position[3];
    float velocity[3];
} mover_item;

typedef struct turret_item {
    float position[3];
    float cooldown, reload;
    int32_t slot, shots;
} turret_item;

typedef struct note_item {
    int32_t slot, shots;
} note_item;
