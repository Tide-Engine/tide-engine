#pragma once

// A map of 64-bit keys (never 0) to 64-bit values, for the shard agent and
// coordinator (engine/src/shard_agent.c, shard_coordinator.c): linear
// probing, deletion by shifting back what follows.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tide/entity.h"
#include "tide/page.h"

typedef struct map64 {
    uint64_t *keys;
    uint64_t *values;
    uint32_t count, room; // room: a power of two, or 0
} map64;

static inline uint32_t map_slot(const map64 *m, const uint64_t key)
{
    uint64_t h = key * 0x9E3779B97F4A7C15ull;
    h ^= h >> 32;
    return (uint32_t)h & (m->room - 1u);
}

static inline uint64_t *map_find(const map64 *m, const uint64_t key)
{
    if (!m->room) return NULL;
    for (uint32_t i = map_slot(m, key);; i = (i + 1u) & (m->room - 1u)) {
        if (m->keys[i] == key) return &m->values[i];
        if (!m->keys[i]) return NULL;
    }
}

static inline void map_put(map64 *m, uint64_t key, uint64_t value);

static inline void map_grow(map64 *m)
{
    const map64 old = *m;
    m->room = old.room ? old.room * 2u : 64u;
    m->keys = calloc(m->room, sizeof *m->keys);
    m->values = calloc(m->room, sizeof *m->values);
    if (!m->keys || !m->values) tide_out_of_memory();
    m->count = 0;
    for (uint32_t i = 0; i < old.room; i++) {
        if (old.keys[i]) map_put(m, old.keys[i], old.values[i]);
    }
    free(old.keys);
    free(old.values);
}

static inline void map_put(map64 *m, const uint64_t key, const uint64_t value)
{
    if ((m->count + 1u) * 2u > m->room) map_grow(m);
    for (uint32_t i = map_slot(m, key);; i = (i + 1u) & (m->room - 1u)) {
        if (m->keys[i] == key) {
            m->values[i] = value;
            return;
        }
        if (!m->keys[i]) {
            m->keys[i] = key;
            m->values[i] = value;
            m->count++;
            return;
        }
    }
}

static inline void map_remove(map64 *m, const uint64_t key)
{
    if (!m->room) return;
    uint32_t i = map_slot(m, key);
    while (m->keys[i] != key) {
        if (!m->keys[i]) return;
        i = (i + 1u) & (m->room - 1u);
    }
    m->keys[i] = 0;
    m->count--;
    // What follows in its run moves back into the gap when it can
    for (uint32_t j = (i + 1u) & (m->room - 1u); m->keys[j]; j = (j + 1u) & (m->room - 1u)) {
        const uint32_t home = map_slot(m, m->keys[j]);
        const bool between = i <= j ? (home > i && home <= j) : (home > i || home <= j);
        if (between) continue;
        m->keys[i] = m->keys[j];
        m->values[i] = m->values[j];
        m->keys[j] = 0;
        i = j;
    }
}

static inline void map_free(map64 *m)
{
    free(m->keys);
    free(m->values);
    memset(m, 0, sizeof *m);
}

static inline uint64_t key_of(const tide_entity e)
{
    return ((uint64_t)e.generation << 32) | e.index;
}

static inline tide_entity entity_of(const uint64_t key)
{
    return (tide_entity){(uint32_t)key, (uint32_t)(key >> 32)};
}
