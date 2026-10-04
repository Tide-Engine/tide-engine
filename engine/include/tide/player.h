#pragma once

#include <stdbool.h>
#include <stdint.h>

// Player identity (Tide's PlayerID).
//
// Temporary implementation written by Claude; the project owner takes it over
// later. The simulation only ever sees player IDs, never connections: the
// networking layer maps connections to IDs, so a player who reconnects and gets
// their ID back keeps everything they owned.
//
// A zeroed ID is "no player". PlayerID(n) in Tide is stored as n + 1.

// How many players a match takes is the game's: its `maxPlayers` setting
// (tide_game.max_players), TIDE_PLAYERS_DEFAULT without one, and at most
// TIDE_PLAYERS_MOST.
#define TIDE_PLAYERS_DEFAULT 16u
#define TIDE_PLAYERS_MOST 256u

typedef struct tide_player_id {
    uint32_t id; // 0 = no player, otherwise player index + 1
} tide_player_id;

static inline tide_player_id tide_player_from_index(const int32_t index)
{
    return (tide_player_id){index >= 0 ? (uint32_t)index + 1u : 0u};
}

static inline bool tide_player_is_null(const tide_player_id p)
{
    return p.id == 0;
}

static inline bool tide_player_equal(const tide_player_id a, const tide_player_id b)
{
    return a.id == b.id;
}

// Index into per-player arrays, or -1 for no player or an ID no game takes. A
// game takes fewer than the most: code that indexes its arrays checks its own
// count too (TIDE_GAME_PLAYERS in generated code).
static inline int32_t tide_player_index(const tide_player_id p)
{
    return p.id >= 1 && p.id <= TIDE_PLAYERS_MOST ? (int32_t)(p.id - 1u) : -1;
}

// A set of players, a bit each by index, with room for one slot past the last
// player's: the server's input comes after the players' (see tide/session.h).
#define TIDE_PLAYER_SET_BITS 320u
typedef struct tide_player_set {
    uint64_t bits[TIDE_PLAYER_SET_BITS / 64u];
} tide_player_set;

static inline bool tide_players_has(const tide_player_set *s, const uint32_t i)
{
    return i < TIDE_PLAYER_SET_BITS && (s->bits[i >> 6] >> (i & 63u) & 1u) != 0;
}

static inline void tide_players_add(tide_player_set *s, const uint32_t i)
{
    if (i < TIDE_PLAYER_SET_BITS) s->bits[i >> 6] |= 1ull << (i & 63u);
}

static inline void tide_players_remove(tide_player_set *s, const uint32_t i)
{
    if (i < TIDE_PLAYER_SET_BITS) s->bits[i >> 6] &= ~(1ull << (i & 63u));
}

static inline bool tide_players_any(const tide_player_set *s)
{
    uint64_t any = 0;
    for (uint32_t w = 0; w < TIDE_PLAYER_SET_BITS / 64u; w++) any |= s->bits[w];
    return any != 0;
}
