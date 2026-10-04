#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/math.h"
#include "tide_test.h"

static tide_world world;

static const tide_entity THING = {1, 1};

TIDE_TEST(long_fields_and_arithmetic_wrap)
{
    tide_world_init(&world, 1.0f);
    TIDE_REQUIRE(tide_has_Tally(&world, THING));
    TIDE_CHECK(tide_get_Tally(&world, THING).bits == 4294967295LL);
    TIDE_CHECK(tide_get_Tally(&world, THING).ledger.balance == 500000000000LL);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    const Tally t = tide_get_Tally(&world, THING);
    TIDE_CHECK(t.total == 105); // ((0 + 7) * 2 + 1, three times over
    TIDE_CHECK(t.wrapped == INT64_MIN + 2); // The largest long plus 3 wraps around
    TIDE_CHECK(t.ledger.entries == 3);
    TIDE_CHECK(t.ledger.balance == 470000000000LL);
    TIDE_CHECK(tide_list_count(t.history) == 3);
    tide_world_free(&world);
}

TIDE_TEST(long_operators_and_conversions)
{
    tide_world_init(&world, 1.0f);
    for (int i = 0; i < 3; i++) tide_world_tick(&world);
    const Results r = tide_get_Results(&world, THING);
    TIDE_CHECK(r.sum == 3000000007LL);
    TIDE_CHECK(r.product == 9000000000000000000LL);
    TIDE_CHECK(r.quotient == 3000000000LL);
    TIDE_CHECK(r.zero == 0);
    TIDE_CHECK(r.remainder == 4);
    TIDE_CHECK(r.shifted == 1099511627776LL);
    TIDE_CHECK(r.rightShifted == -4);
    TIDE_CHECK(r.masked == (int64_t)(9000000000000000000ull & 0xFFFFFFFFull));
    TIDE_CHECK(r.negated == -3000000000LL);
    TIDE_CHECK(r.smallest == INT64_MIN);
    TIDE_CHECK(r.fromFloat == (int64_t)1e15f);
    TIDE_CHECK(r.saturated == INT64_MAX);
    TIDE_CHECK(r.fromNaN == 0);
    TIDE_CHECK(r.fromEnum == 70000);
    TIDE_CHECK(r.truncated == (int32_t)(uint32_t)3000000000u); // The low 32 bits: -1294967296
    TIDE_CHECK(r.widened == 3000000000.0f);
    TIDE_CHECK(r.rankOrder == 0);
    TIDE_CHECK(r.clamped == 7);
    TIDE_CHECK(r.largest == 3000000000LL);
    TIDE_CHECK(r.hash == tide_hash_l(3000000000LL));
    TIDE_CHECK(r.hash >= 0);
    TIDE_CHECK(r.ordered);
    TIDE_CHECK(r.equal);
    TIDE_CHECK(r.hex == INT64_MAX);
    TIDE_CHECK(r.ticks == 2 + 100); // The last tick, and the switch's first case
    char expected[128];
    snprintf(expected, sizeof expected, "3000000000 -003000000000 %llX 3000000007",
             (unsigned long long)(9000000000000000000ull & 0xFFFFFFFFull));
    const tide_str text = tide_text_read(&world.heap, r.text);
    TIDE_CHECK(text.bytes == (int32_t)strlen(expected) && memcmp(text.ptr, expected, (size_t)text.bytes) == 0);
    tide_world_free(&world);
}

TIDE_TEST(long_inputs_are_bounded_and_read)
{
    tide_world_init(&world, 1.0f);
    const Controls in = {.bid = 5000000000LL, .bounded = 2000000000000LL}; // bounded clamps to a trillion
    tide_world_set_input(&world, tide_player_from_index(0), in);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Results(&world, THING).ticks == 100 + 5000000000LL + 1000000000000LL);
    tide_world_free(&world);
}

// Longs go over the network as 64-bit varints, and as deltas by how much they
// changed: every value comes back bit for bit, the extremes included.
TIDE_TEST(long_inputs_pack_and_come_back)
{
    static const int64_t values[] = {0, 1, -1, 127, -128, 3000000000LL, -3000000000LL, INT64_MAX, INT64_MIN, INT64_MIN + 1};
    uint8_t bytes[256];
    Controls previous = {.bid = 5, .bounded = -5};
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        const Controls in = {.bid = values[i], .bounded = values[(i + 3) % (sizeof values / sizeof values[0])]};
        Controls out = {0};
        const uint32_t size = tide_game_api.write_input(&in, bytes, sizeof bytes);
        TIDE_REQUIRE(size > 0 && tide_game_api.read_input(bytes, size, &out));
        TIDE_CHECK(out.bid == in.bid && out.bounded == in.bounded);
        memset(&out, 0, sizeof out);
        const uint32_t delta = tide_game_api.write_input_delta(&in, &previous, bytes, sizeof bytes);
        TIDE_REQUIRE(delta > 0 && tide_game_api.read_input_delta(bytes, delta, &previous, &out));
        TIDE_CHECK(out.bid == in.bid && out.bounded == in.bounded);
        previous = in;
    }
    // A small change takes little: a bit for each field and a byte for the difference
    const Controls a = {.bid = 1000000000000LL, .bounded = 7};
    const Controls b = {.bid = 1000000000003LL, .bounded = 7};
    TIDE_CHECK(tide_game_api.write_input_delta(&b, &a, bytes, sizeof bytes) <= 2);
}
