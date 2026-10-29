#pragma once

// The match of local_fields.tide and local_fields_plain.tide (the same game without its local fields). Local fields
// are outside the match, so both worlds are the same. Each game's test plays this script and checks the numbers
// below.
//
// A world's bytes start with its struct, which holds pointers, so they differ where pointers are 4 bytes (the
// web). When worlds' storage or transfer changes on purpose, the tests print what they got; both games must
// print the same.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide_test.h"

#define SAME_HASH UINT64_C(0xAB58CD3A5BFEE54F)
#define SAME_WHOLE_SIZE 219u
#define SAME_WHOLE UINT64_C(0xD49D9FE755B1C27F)
#define SAME_DELTA_SIZE 122u
#define SAME_DELTA UINT64_C(0xE1D6B6872BC8EB96)
// The derived sparse-query cache appends a zero pointer slot to raw headers.
// Logical world hashes and portable whole/delta bytes keep their existing pins.
#if UINTPTR_MAX > 0xFFFFFFFFu
#define SAME_PACKED_SIZE 447u
#define SAME_PACKED UINT64_C(0x04C64696C170B049)
#else
#define SAME_PACKED_SIZE 403u
#define SAME_PACKED UINT64_C(0x7466E398E439BE99)
#endif

static uint64_t same_bytes_hash(const uint8_t *bytes, const uint32_t size)
{
    uint64_t h = UINT64_C(0xCBF29CE484222325); // FNV-1a
    for (uint32_t i = 0; i < size; i++) h = (h ^ bytes[i]) * UINT64_C(0x100000001B3);
    return h;
}

// Plays the script on `w`, with `between` (or NULL) after every tick, and
// checks the world it ends with.
static void same_match_check(tide_world *w, tide_world *base, void (*between)(const tide_world *w))
{
    tide_world_init(w, 1.0f / 60.0f);
    for (int tick = 0; tick < 12; tick++) {
        if (tick == 3) w->Rules.spawn = 6;  // A box comes,
        if (tick == 6) w->Rules.shed = true; // the glowing ones stop,
        if (tick == 9) w->Rules.drop = 3;   // and one goes
        if (tick == 5) tide_world_copy(base, w);
        tide_world_tick(w);
        if (between) between(w);
    }
    const uint64_t hash = tide_world_hash(w);
    const uint32_t packed_size = tide_world_pack(w, NULL, 0);
    uint8_t *packed = malloc(packed_size);
    tide_world_pack(w, packed, packed_size);
    uint32_t whole_size;
    uint8_t *whole = tide_world_pack_delta(w, NULL, NULL, 0, &whole_size);
    uint32_t delta_size;
    uint8_t *delta = tide_world_pack_delta(w, base, NULL, 0, &delta_size);
    const uint64_t packed_hash = same_bytes_hash(packed, packed_size);
    const uint64_t whole_hash = same_bytes_hash(whole, whole_size);
    const uint64_t delta_hash = same_bytes_hash(delta, delta_size);
    const bool same = hash == SAME_HASH && packed_size == SAME_PACKED_SIZE && packed_hash == SAME_PACKED
                   && whole_size == SAME_WHOLE_SIZE && whole_hash == SAME_WHOLE && delta_size == SAME_DELTA_SIZE
                   && delta_hash == SAME_DELTA;
    // Derived query-cache ownership must never enter a raw saved header.
    const uint32_t cache_begin = (uint32_t)offsetof(tide_world, tide_rows_cache);
    const uint32_t cache_end = cache_begin + (uint32_t)sizeof w->tide_rows_cache;
    for (uint32_t i = cache_begin; i < cache_end; i++) TIDE_CHECK(packed[i] == 0);
    if (!same) {
        printf("    hash 0x%016llX\n    packed %uu 0x%016llX\n    whole %uu 0x%016llX\n    delta %uu 0x%016llX\n",
               (unsigned long long)hash, (unsigned)packed_size, (unsigned long long)packed_hash, (unsigned)whole_size,
               (unsigned long long)whole_hash, (unsigned)delta_size, (unsigned long long)delta_hash);
        // This isolates the intentional opaque-pointer header growth when
        // comparing the current raw save with its pre-cache pin.
        uint64_t without_cache = UINT64_C(0xCBF29CE484222325);
        for (uint32_t i = 0; i < packed_size; i++) {
            if (i >= cache_begin && i < cache_end) continue;
            without_cache = (without_cache ^ packed[i]) * UINT64_C(0x100000001B3);
        }
        printf("    packed without zero cache slot %uu 0x%016llX\n",
               (unsigned)(packed_size - (cache_end - cache_begin)), (unsigned long long)without_cache);
    }
    TIDE_CHECK(same);
    TIDE_CHECK(tide_world_entity_count(w) == 4);
    free(packed);
    free(whole);
    free(delta);
}
