#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world copy;
static const tide_entity THING = {1, 1};

TIDE_TEST(byte_integer_arguments_run_in_local_code)
{
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_local_init(&local);
    tide_frame(NULL, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.IntegerResults.sent && local.IntegerResults.handled == 2);
    tide_session_request request;
    tide_start start;
    uint32_t requests = 0;
    while (tide_local_take_request(&local, &request, &start)) {
        TIDE_CHECK(request.port == 2);
        requests++;
    }
    TIDE_CHECK(requests > 0);
    tide_local_free(&local);
    tide_draw_free(&draw);
}

TIDE_TEST(byte_fields_lists_arithmetic_and_conversions)
{
    TIDE_CHECK(sizeof(Octets) == 3);
    TIDE_CHECK(sizeof(Controls) == 2);
    TIDE_CHECK(sizeof(CompactCode) == 1 && CompactCode_Maximum == 255);
    TIDE_CHECK(BytePhases_Prepare == 1 && BytePhases_Advance == 64);
    tide_world_init(&world, 1.0f);
    TIDE_REQUIRE(tide_has_Bytes(&world, THING));
    const Bytes initial = tide_get_Bytes(&world, THING);
    TIDE_CHECK(initial.packed.a == 1 && initial.packed.b == 131 && initial.packed.c == 52);
    const uint8_t *first = tide_list_at(initial.content, 0, 1);
    const uint8_t *second = tide_list_at(initial.content, 1, 1);
    TIDE_REQUIRE(first && second);
    TIDE_CHECK(second == first + 1 && first[0] == 0 && first[1] == 128 && first[2] == 255);
    tide_world_set_input(&world, tide_player_from_index(0), (Controls){255, 255});
    tide_world_tick(&world);
    const Bytes b = tide_get_Bytes(&world, THING);
    TIDE_CHECK(b.value == 255 && b.fromLong == 188 && b.fromFloat == 1);
    TIDE_CHECK(b.fromNaN == 0 && b.fromNegative == 255 && b.fromSaturated == 255);
    TIDE_CHECK(b.sum == 183 && b.negative == -131 && b.complement == -2);
    TIDE_CHECK(b.wide == 134 && b.real == 131.0f && b.selected == 1 && b.sent == 129);
    TIDE_CHECK(tide_list_count(b.content) == 6);
    TIDE_CHECK(*(const uint8_t *)tide_list_at(b.content, 0, 1) == 199); // 255 + Clamp(255, 3, 200)
    TIDE_CHECK(*(const uint8_t *)tide_list_at(b.content, 1, 1) == 0);
    const tide_str text = tide_text_read(&world.heap, b.text);
    TIDE_CHECK(text.bytes == 6 && memcmp(text.ptr, "255 83", 6) == 0);
    TIDE_REQUIRE(tide_list_count(world.Cells.content) == 17000);
    TIDE_CHECK(*(const uint8_t *)tide_list_at(world.Cells.content, 255, 1) == 0);
    TIDE_CHECK(*(const uint8_t *)tide_list_at(world.Cells.content, 16999, 1) == (uint8_t)(16999 + 1));
    tide_world_free(&world);
}

TIDE_TEST(byte_input_has_eight_bits_and_round_trips_deltas)
{
    uint8_t bytes[32];
    Controls previous = {255, 0};
    for (int i = 0; i < 256; i++) {
        const Controls in = {(uint8_t)i, (uint8_t)(255 - i)};
        Controls out = {0};
        const uint32_t size = tide_game_api.write_input(&in, bytes, sizeof bytes);
        TIDE_REQUIRE(size == 2 && tide_game_api.read_input(bytes, size, &out));
        TIDE_CHECK(memcmp(&in, &out, sizeof in) == 0);
        const uint32_t delta = tide_game_api.write_input_delta(&in, &previous, bytes, sizeof bytes);
        TIDE_REQUIRE(delta > 0 && tide_game_api.read_input_delta(bytes, delta, &previous, &out));
        TIDE_CHECK(memcmp(&in, &out, sizeof in) == 0);
        previous = in;
    }
}

TIDE_TEST(byte_snapshots_pack_and_hash_deterministically)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *packed = malloc(size);
    TIDE_REQUIRE(packed);
    TIDE_REQUIRE(tide_world_pack(&world, packed, size) == size);
    tide_world_init(&copy, 1.0f);
    TIDE_REQUIRE(tide_world_unpack(&copy, packed, size));
    TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&copy));
    for (int i = 0; i < 5; i++) {
        tide_world_tick(&world);
        tide_world_tick(&copy);
        TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&copy));
    }
    free(packed);
    tide_world_free(&copy);
    tide_world_free(&world);
}
