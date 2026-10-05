#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "tide/random.h"
#include "tide_test.h"

// Random numbers (tide/random.h): every method stays in its range, a state of
// zero isn't stuck, seeds next to each other give sequences that have nothing
// to do with each other, and the numbers are spread as random ones are. What
// the bits are, on every platform, is in test_crossplatform.c.

#define DRAWS 200000

TIDE_TEST(random_zero_state_is_not_stuck)
{
    tide_random r = {0};
    uint32_t seen[64];
    for (int i = 0; i < 64; i++) {
        seen[i] = tide_random_next(&r);
        TIDE_CHECK(r.state != 0);
        for (int k = 0; k < i; k++) TIDE_CHECK(seen[k] != seen[i]); // 64 numbers of 32 bits: none twice
    }
}

TIDE_TEST(random_ints_stay_in_range)
{
    tide_random r = tide_random_seeded(1);
    for (int i = 0; i < DRAWS; i++) {
        TIDE_CHECK(tide_random_next_int(&r) >= 0);
        const int32_t die = tide_random_next_int_between(&r, 1, 7);
        TIDE_CHECK(die >= 1 && die <= 6);
        const int32_t below = tide_random_next_int_below(&r, 52);
        TIDE_CHECK(below >= 0 && below < 52);
        const int32_t negative = tide_random_next_int_between(&r, -5, 5);
        TIDE_CHECK(negative >= -5 && negative < 5);
    }
    // The whole range of an int, whose width doesn't fit one
    bool low = false, high = false;
    for (int i = 0; i < DRAWS; i++) {
        const int32_t any = tide_random_next_int_between(&r, INT32_MIN, INT32_MAX);
        TIDE_CHECK(any < INT32_MAX);
        low |= any < -1000000000;
        high |= any > 1000000000;
    }
    TIDE_CHECK(low && high);
}

// A range with nothing in it gives its start, and still steps the generator:
// what a call does to the state never depends on its arguments.
TIDE_TEST(random_empty_ranges_give_their_start)
{
    tide_random r = tide_random_seeded(2);
    tide_random other = r;
    TIDE_CHECK(tide_random_next_int_below(&r, 0) == 0);
    TIDE_CHECK(tide_random_next_int_below(&r, -3) == 0);
    TIDE_CHECK(tide_random_next_int_between(&r, 5, 5) == 5);
    TIDE_CHECK(tide_random_next_int_between(&r, 5, 2) == 5);
    TIDE_CHECK(tide_random_next_float_below(&r, 0.0f) == 0.0f);
    TIDE_CHECK(tide_random_next_float_below(&r, -1.0f) == 0.0f);
    TIDE_CHECK(tide_random_next_float_between(&r, 2.0f, 2.0f) == 2.0f);
    TIDE_CHECK(tide_random_next_float_between(&r, 2.0f, 1.0f) == 2.0f);
    for (int i = 0; i < 8; i++) tide_random_next(&other);
    TIDE_CHECK(r.state == other.state);
}

TIDE_TEST(random_floats_stay_in_range)
{
    tide_random r = tide_random_seeded(3);
    float least = 1.0f, most = 0.0f;
    for (int i = 0; i < DRAWS; i++) {
        const float unit = tide_random_next_float(&r);
        TIDE_CHECK(unit >= 0.0f && unit < 1.0f);
        if (unit < least) least = unit;
        if (unit > most) most = unit;
        const float below = tide_random_next_float_below(&r, 3.0f);
        TIDE_CHECK(below >= 0.0f && below < 3.0f);
        const float between = tide_random_next_float_between(&r, -2.5f, 7.5f);
        TIDE_CHECK(between >= -2.5f && between < 7.5f);
        // A range a float's steps are coarse in: rounding never gives its end
        const float coarse = tide_random_next_float_between(&r, 16777215.0f, 16777216.0f);
        TIDE_CHECK(coarse >= 16777215.0f && coarse < 16777216.0f);
        const tide_float2 point = tide_random_next_float2_between(&r, (tide_float2){-1.0f, 10.0f}, (tide_float2){1.0f, 20.0f});
        TIDE_CHECK(point.x >= -1.0f && point.x < 1.0f && point.y >= 10.0f && point.y < 20.0f);
        const tide_float3 corner = tide_random_next_float3(&r);
        TIDE_CHECK(corner.x >= 0.0f && corner.x < 1.0f && corner.y >= 0.0f && corner.y < 1.0f && corner.z >= 0.0f && corner.z < 1.0f);
    }
    TIDE_CHECK(least < 0.001f && most > 0.999f);
}

TIDE_TEST(random_directions_have_length_one)
{
    tide_random r = tide_random_seeded(4);
    tide_float3 sum = {0};
    for (int i = 0; i < DRAWS; i++) {
        const tide_float2 flat = tide_random_next_float2_direction(&r);
        const float flat_length = tide_length_f2(flat);
        TIDE_CHECK(flat_length > 0.9999f && flat_length < 1.0001f);
        const tide_float3 way = tide_random_next_float3_direction(&r);
        const float length = tide_length_f3(way);
        TIDE_CHECK(length > 0.9999f && length < 1.0001f);
        sum = tide_add_f3(sum, way);
        const tide_quaternion turn = tide_random_next_quaternion_rotation(&r);
        const float norm = tide_dot_q(turn, turn);
        TIDE_CHECK(norm > 0.9999f && norm < 1.0001f && turn.value.w >= 0.0f);
    }
    // Every direction as likely: they cancel out
    TIDE_CHECK(tide_length_f3(sum) < (float)DRAWS * 0.01f);
}

// Each of a die's faces comes up about as often, each bit of an int is set
// about half the time, and a coin lands each way about as often.
TIDE_TEST(random_numbers_are_spread_evenly)
{
    tide_random r = tide_random_seeded(5);
    uint32_t faces[6] = {0};
    uint32_t bits[31] = {0};
    uint32_t heads = 0;
    for (int i = 0; i < DRAWS; i++) {
        faces[tide_random_next_int_below(&r, 6)]++;
        const int32_t any = tide_random_next_int(&r);
        for (int b = 0; b < 31; b++) bits[b] += (uint32_t)(any >> b) & 1u;
        heads += tide_random_next_bool(&r) ? 1u : 0u;
    }
    for (int f = 0; f < 6; f++) TIDE_CHECK(faces[f] > DRAWS / 6 - DRAWS / 100 && faces[f] < DRAWS / 6 + DRAWS / 100);
    for (int b = 0; b < 31; b++) TIDE_CHECK(bits[b] > DRAWS / 2 - DRAWS / 100 && bits[b] < DRAWS / 2 + DRAWS / 100);
    TIDE_CHECK(heads > DRAWS / 2 - DRAWS / 100 && heads < DRAWS / 2 + DRAWS / 100);
}

// Seeds 0, 1, 2... are what games write. Their generators' first numbers
// have nothing in common: no two the same, and the dice they roll are even.
TIDE_TEST(random_seeds_next_to_each_other_are_unrelated)
{
    enum { SEEDS = 4096 };
    static uint32_t first[SEEDS];
    uint32_t faces[6] = {0};
    for (int s = 0; s < SEEDS; s++) {
        tide_random r = tide_random_seeded(s);
        TIDE_CHECK(r.state != 0);
        first[s] = tide_random_next(&r);
        faces[tide_random_next_int_below(&r, 6)]++;
    }
    uint32_t same = 0;
    for (int a = 0; a < SEEDS; a++) {
        for (int b = a + 1; b < SEEDS; b++) same += first[a] == first[b];
    }
    TIDE_CHECK(same <= 2); // 8 million pairs of 32 bits: about 0.002 are alike by chance
    for (int f = 0; f < 6; f++) TIDE_CHECK(faces[f] > SEEDS / 6 - SEEDS / 20 && faces[f] < SEEDS / 6 + SEEDS / 20);
    TIDE_CHECK(tide_random_seeded(7).state == tide_random_seeded(7).state);
    TIDE_CHECK(tide_random_seeded(7).state != tide_random_seeded(-7).state);
}

// What the engine seeds for a game: by the match's seed, the tick, the entity
// and the field. Changing any one gives another generator, and never the
// unseeded one.
TIDE_TEST(random_places_are_their_own)
{
    const tide_entity e = {12, 3};
    const tide_random base = tide_random_place(99, 40, e, 256);
    TIDE_CHECK(base.state != 0);
    TIDE_CHECK(base.state == tide_random_place(99, 40, e, 256).state);
    TIDE_CHECK(base.state != tide_random_place(98, 40, e, 256).state);
    TIDE_CHECK(base.state != tide_random_place(99, 41, e, 256).state);
    TIDE_CHECK(base.state != tide_random_place(99, 40, (tide_entity){13, 3}, 256).state);
    TIDE_CHECK(base.state != tide_random_place(99, 40, (tide_entity){12, 5}, 256).state);
    TIDE_CHECK(base.state != tide_random_place(99, 40, e, 257).state);
    // The entities of one tick, as a spawner makes them: their first rolls are even
    uint32_t faces[6] = {0};
    for (uint32_t i = 0; i < 6000; i++) {
        tide_random r = tide_random_place(0, 7, (tide_entity){i, 1}, 256);
        faces[tide_random_next_int_below(&r, 6)]++;
    }
    for (int f = 0; f < 6; f++) TIDE_CHECK(faces[f] > 900 && faces[f] < 1100);
    // A field that's seeded stays as it is; one that isn't gets its place's
    tide_random kept = tide_random_seeded(5);
    const int64_t before = kept.state;
    tide_random_seed_place(&kept, 99, 40, e, 256);
    TIDE_CHECK(kept.state == before);
    tide_random fresh = {0};
    tide_random_seed_place(&fresh, 99, 40, e, 256);
    TIDE_CHECK(fresh.state == base.state);
}

// A copy goes on as the one it was copied from: the state is all there is.
TIDE_TEST(random_state_is_all_there_is)
{
    tide_random r = tide_random_seeded(11);
    for (int i = 0; i < 100; i++) tide_random_next(&r);
    tide_random copy;
    memcpy(&copy, &r, sizeof copy);
    TIDE_CHECK(sizeof(tide_random) == 8);
    for (int i = 0; i < 100; i++) TIDE_CHECK(tide_random_next_float(&r) == tide_random_next_float(&copy));
}
