#pragma once

// A map of logical entity keys (never null) to 64-bit values, for the shard agent and
// coordinator (engine/src/shard_agent.c, shard_coordinator.c): linear
// probing, deletion by shifting back what follows.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tide/entity.h"
#include "tide/page.h"
#include "tide/net.h"

typedef struct map64 {
    tide_entity *keys;
    uint64_t *values;
    uint32_t count, room; // room: a power of two, or 0
} map64;

static inline uint32_t map_slot(const map64 *m, const tide_entity key)
{
    return (uint32_t)tide_entity_hash(0, key) & (m->room - 1u);
}

static inline uint64_t *map_find(const map64 *m, const tide_entity key)
{
    if (!m->room || tide_entity_is_null(key)) return NULL;
    for (uint32_t i = map_slot(m, key);; i = (i + 1u) & (m->room - 1u)) {
        if (tide_entity_equal(m->keys[i], key)) return &m->values[i];
        if (tide_entity_is_null(m->keys[i])) return NULL;
    }
}

static inline void map_put(map64 *m, tide_entity key, uint64_t value);

static inline void map_grow(map64 *m)
{
    const map64 old = *m;
    if (old.room > UINT32_MAX / 2u) tide_out_of_memory();
    m->room = old.room ? old.room * 2u : 64u;
    m->keys = calloc(m->room, sizeof *m->keys);
    m->values = calloc(m->room, sizeof *m->values);
    if (!m->keys || !m->values) tide_out_of_memory();
    m->count = 0;
    for (uint32_t i = 0; i < old.room; i++) {
        if (!tide_entity_is_null(old.keys[i])) map_put(m, old.keys[i], old.values[i]);
    }
    free(old.keys);
    free(old.values);
}

static inline void map_put(map64 *m, const tide_entity key, const uint64_t value)
{
    if (((uint64_t)m->count + 1u) * 2u > m->room) map_grow(m);
    for (uint32_t i = map_slot(m, key);; i = (i + 1u) & (m->room - 1u)) {
        if (tide_entity_equal(m->keys[i], key)) {
            m->values[i] = value;
            return;
        }
        if (tide_entity_is_null(m->keys[i])) {
            m->keys[i] = key;
            m->values[i] = value;
            m->count++;
            return;
        }
    }
}

static inline void map_remove(map64 *m, const tide_entity key)
{
    if (!m->room || tide_entity_is_null(key)) return;
    uint32_t i = map_slot(m, key);
    while (!tide_entity_equal(m->keys[i], key)) {
        if (tide_entity_is_null(m->keys[i])) return;
        i = (i + 1u) & (m->room - 1u);
    }
    m->keys[i] = (tide_entity){0, 0};
    m->count--;
    // What follows in its run moves back into the gap when it can
    for (uint32_t j = (i + 1u) & (m->room - 1u); !tide_entity_is_null(m->keys[j]); j = (j + 1u) & (m->room - 1u)) {
        const uint32_t home = map_slot(m, m->keys[j]);
        const bool between = i <= j ? (home > i && home <= j) : (home > i || home <= j);
        if (between) continue;
        m->keys[i] = m->keys[j];
        m->values[i] = m->values[j];
        m->keys[j] = (tide_entity){0, 0};
        i = j;
    }
}

static inline void map_free(map64 *m)
{
    free(m->keys);
    free(m->values);
    memset(m, 0, sizeof *m);
}

static inline tide_entity key_of(const tide_entity e) { return e; }
static inline tide_entity entity_of(const tide_entity e) { return e; }

static inline void write_entity(tide_writer *w, const tide_entity e)
{
    tide_write_u64(w, e.birth);
    tide_write_u64(w, e.lineage);
}

static inline tide_entity read_entity(tide_reader *r)
{
    const uint64_t birth = tide_read_u64(r);
    const uint64_t lineage = tide_read_u64(r);
    return (tide_entity){birth, lineage};
}
