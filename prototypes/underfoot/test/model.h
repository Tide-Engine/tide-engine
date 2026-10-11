#pragma once

// A second, independent statement of Underfoot's rules, small enough to search
// exhaustively. The game's own rules are in game.tide; test/model.c holds the
// two together move by move, so what the search proves of a puzzle is true of
// the puzzle players get.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define UF_W 9
#define UF_H 7
#define UF_CELLS (UF_W * UF_H)
#define UF_CRATES 6
#define UF_NONE 255

enum { UF_VOID, UF_SLAB, UF_ICE, UF_PORTAL_P, UF_PORTAL_Q, UF_PLATE_A, UF_PLATE_B, UF_RUBBLE, UF_WALL };

typedef struct uf_layout {
    uint8_t fixed[UF_CELLS];
    uint8_t gate[UF_CELLS];
    uint8_t goal[UF_CELLS];
    int actors;
} uf_layout;

typedef struct uf_state {
    uint8_t ground[UF_CELLS];
    uint8_t actor[2];
    uint8_t held[2];
    uint8_t crate[UF_CRATES];
} uf_state;

static inline int uf_step(const int cell, const int direction)
{
    const int x = cell % UF_W + (direction == 1) - (direction == 3);
    const int y = cell / UF_W + (direction == 2) - (direction == 0);
    return x < 0 || y < 0 || x >= UF_W || y >= UF_H ? -1 : y * UF_W + x;
}

static inline int uf_actor_at(const uf_layout *const layout, const uf_state *const state, const int cell)
{
    for (int i = 0; i < layout->actors; i++)
        if (state->actor[i] == cell) return i;
    return -1;
}

static inline int uf_crate_at(const uf_state *const state, const int cell)
{
    for (int i = 0; i < UF_CRATES; i++)
        if (state->crate[i] == cell) return i;
    return -1;
}

static inline bool uf_loose(const int ground)
{
    return ground >= UF_SLAB && ground <= UF_PLATE_B;
}

// Bit 0 is plate A's gates, bit 1 plate B's.
static inline int uf_pressed(const uf_layout *const layout, const uf_state *const state)
{
    int pressed = 0;
    for (int i = 0; i < layout->actors; i++) {
        const int ground = state->ground[state->actor[i]];
        if (ground == UF_PLATE_A || ground == UF_PLATE_B) pressed |= 1 << (ground - UF_PLATE_A);
    }
    for (int i = 0; i < UF_CRATES; i++) {
        if (state->crate[i] == UF_NONE) continue;
        const int ground = state->ground[state->crate[i]];
        if (ground == UF_PLATE_A || ground == UF_PLATE_B) pressed |= 1 << (ground - UF_PLATE_A);
    }
    return pressed;
}

static inline bool uf_shut(const uf_layout *const layout, const int cell, const int pressed)
{
    return layout->gate[cell] != 0 && (pressed & (1 << (layout->gate[cell] - 1))) == 0;
}

// What a thing does once it is on a new cell: a crate over nothing sinks and
// becomes ground, a portal sends it to its free twin, and ice carries it on.
static inline void uf_settle(const uf_layout *const layout, uf_state *const state, uint8_t *const place,
    const bool crate, const int direction, const int pressed)
{
    for (;;) {
        const int cell = *place;
        const int ground = state->ground[cell];
        if (crate && ground == UF_VOID) {
            state->ground[cell] = UF_RUBBLE;
            *place = UF_NONE;
            return;
        }
        if (ground == UF_PORTAL_P || ground == UF_PORTAL_Q) {
            const int twin = ground == UF_PORTAL_P ? UF_PORTAL_Q : UF_PORTAL_P;
            for (int other = 0; other < UF_CELLS; other++) {
                if (state->ground[other] != twin) continue;
                if (uf_actor_at(layout, state, other) < 0 && uf_crate_at(state, other) < 0) *place = (uint8_t)other;
                break;
            }
            return;
        }
        if (ground != UF_ICE) return;
        const int next = uf_step(cell, direction);
        if (next < 0 || state->ground[next] == UF_WALL || uf_shut(layout, next, pressed)) return;
        if (uf_actor_at(layout, state, next) >= 0 || uf_crate_at(state, next) >= 0) return;
        if (!crate && state->ground[next] == UF_VOID) return;
        *place = (uint8_t)next;
    }
}

static inline bool uf_move(const uf_layout *const layout, uf_state *const state, const int actor, const int direction)
{
    const int to = uf_step(state->actor[actor], direction);
    if (to < 0) return false;
    const int pressed = uf_pressed(layout, state);
    const int ground = state->ground[to];
    if (ground == UF_WALL || ground == UF_VOID || uf_shut(layout, to, pressed)) return false;
    if (uf_actor_at(layout, state, to) >= 0) return false;
    const int crate = uf_crate_at(state, to);
    if (crate >= 0) {
        const int beyond = uf_step(to, direction);
        if (beyond < 0 || state->ground[beyond] == UF_WALL || uf_shut(layout, beyond, pressed)) return false;
        if (uf_actor_at(layout, state, beyond) >= 0 || uf_crate_at(state, beyond) >= 0) return false;
        state->crate[crate] = (uint8_t)beyond;
        uf_settle(layout, state, &state->crate[crate], true, direction, pressed);
    }
    state->actor[actor] = (uint8_t)to;
    uf_settle(layout, state, &state->actor[actor], false, direction, pressed);
    return true;
}

static inline bool uf_use(const uf_layout *const layout, uf_state *const state, const int actor, const int direction)
{
    const int to = uf_step(state->actor[actor], direction);
    if (to < 0) return false;
    const int ground = state->ground[to];
    if (ground == UF_WALL || layout->gate[to] != 0) return false;
    if (uf_actor_at(layout, state, to) >= 0 || uf_crate_at(state, to) >= 0) return false;
    if (state->held[actor] == UF_VOID) {
        // Lifting takes firm footing: nobody pulls up a stone from a loose one.
        const int here = state->actor[actor];
        if (!layout->fixed[here] && state->ground[here] != UF_RUBBLE) return false;
        if (!uf_loose(ground) || layout->fixed[to]) return false;
        state->held[actor] = (uint8_t)ground;
        state->ground[to] = UF_VOID;
        return true;
    }
    if (ground != UF_VOID) return false;
    state->ground[to] = state->held[actor];
    state->held[actor] = UF_VOID;
    return true;
}

// Actions 0-3 walk north, east, south, west; 4-7 lift or lay that way.
static inline bool uf_act(const uf_layout *const layout, uf_state *const state, const int actor, const int action)
{
    if (actor < 0 || actor >= layout->actors || action < 0 || action > 7) return false;
    return action < 4 ? uf_move(layout, state, actor, action) : uf_use(layout, state, actor, action - 4);
}

static inline bool uf_solved(const uf_layout *const layout, const uf_state *const state)
{
    for (int i = 0; i < layout->actors; i++)
        if (!layout->goal[state->actor[i]]) return false;
    return true;
}

// Rows are two characters a cell: its ground, then what stands on it.
//   ground  # wall  ~ nothing  . fixed stone  o loose stone  i I ice (loose, fixed)
//           p q P Q portals  a A b B plates  1 2 gates  G goal
//   on it   @ first paver  & second paver  x crate
static inline bool uf_parse_cell(uf_layout *const layout, uf_state *const state, const int cell,
    const char ground, const char thing)
{
    int kind = UF_SLAB;
    bool fixed = true;
    switch (ground) {
    case '#': kind = UF_WALL; break;
    case '~': kind = UF_VOID; fixed = false; break;
    case '.': break;
    case 'o': fixed = false; break;
    case 'i': kind = UF_ICE; fixed = false; break;
    case 'I': kind = UF_ICE; break;
    case 'p': kind = UF_PORTAL_P; fixed = false; break;
    case 'P': kind = UF_PORTAL_P; break;
    case 'q': kind = UF_PORTAL_Q; fixed = false; break;
    case 'Q': kind = UF_PORTAL_Q; break;
    case 'a': kind = UF_PLATE_A; fixed = false; break;
    case 'A': kind = UF_PLATE_A; break;
    case 'b': kind = UF_PLATE_B; fixed = false; break;
    case 'B': kind = UF_PLATE_B; break;
    case '1': layout->gate[cell] = 1; break;
    case '2': layout->gate[cell] = 2; break;
    case 'G': layout->goal[cell] = 1; break;
    default: return false;
    }
    state->ground[cell] = (uint8_t)kind;
    layout->fixed[cell] = fixed;
    if (thing == '@') state->actor[0] = (uint8_t)cell;
    else if (thing == '&') { state->actor[1] = (uint8_t)cell; layout->actors = 2; }
    else if (thing == 'x') {
        for (int i = 0; i < UF_CRATES; i++)
            if (state->crate[i] == UF_NONE) { state->crate[i] = (uint8_t)cell; return true; }
        return false;
    }
    else if (thing != ' ') return false;
    return true;
}

static inline void uf_blank(uf_layout *const layout, uf_state *const state)
{
    memset(layout, 0, sizeof *layout);
    memset(state, 0, sizeof *state);
    layout->actors = 1;
    for (int cell = 0; cell < UF_CELLS; cell++) state->ground[cell] = UF_WALL;
    for (int i = 0; i < UF_CRATES; i++) state->crate[i] = UF_NONE;
    state->actor[1] = UF_NONE;
}
