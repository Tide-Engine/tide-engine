#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world, previous, restored;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static const tide_entity ITEM = {1, 1};

double DoubleIdentity(double value) { return value; }
tide_double3 DoubleVectorIdentity(tide_double3 value) { return value; }
void DoubleOut(double *value) { *value = 1000000000.125; }

TIDE_TEST(double_arithmetic_and_containers_retain_precision)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Results.ticks == 1);
    TIDE_CHECK(world.Results.difference == 0.125);
    TIDE_CHECK(world.Results.sum == 2.25 && world.Results.product == 4.5 && world.Results.quotient == 2.25);
    TIDE_CHECK(world.Results.dot == 32.0 && world.Results.length == 5.0);
    TIDE_CHECK(world.Results.saturatedInt == INT32_MAX && world.Results.saturatedLong == INT64_MIN && world.Results.fromNaN == 0);
    TIDE_CHECK(world.Results.normalized.y == 3.0 * (1.0 / 5.0));
    TIDE_CHECK(world.Results.reflected.y == 2.0 && world.Results.crossPair.x == -3.0);
    TIDE_CHECK(world.Results.render.x == .125f && world.Results.render.y == .25f);
    const tide_str text = tide_text_read(&world.heap, world.Results.text);
    TIDE_CHECK(tide_str_eq(text, tide_str_from_cstr("1000000000.125 (1.25, -2.50) 1.25000000000000000")));
    TIDE_CHECK(sizeof(Measurement) == 32 && _Alignof(Measurement) == 8);
    tide_world_free(&world);
}

TIDE_TEST(double_snapshots_packing_projection_and_interpolation)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_copy(&previous, &world);
    const uint64_t before = tide_world_hash(&previous);
    for (int i = 0; i < 8; i++) tide_world_tick(&world);
    const uint64_t after = tide_world_hash(&world);
    TIDE_CHECK(world.Results.taskValue == 1000000000.375);
    TIDE_CHECK(tide_world_hash(&previous) == before);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes && tide_world_pack(&world, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&restored, bytes, size));
    TIDE_CHECK(tide_world_hash(&restored) == after);
    free(bytes);
    tide_world_free(&restored);
    tide_world_copy(&restored, &previous);
    for (int i = 0; i < 8; i++) tide_world_tick(&restored);
    TIDE_CHECK(tide_world_hash(&restored) == after);
    tide_world_free(&restored);
    const tide_player_id recipient = tide_player_from_index(0);
    uint32_t projected_size = 0;
    bytes = tide_game_api.client_state(&world, recipient, &projected_size);
    TIDE_REQUIRE(bytes && projected_size && tide_game_api.load_client_state(&restored, bytes, projected_size));
    TIDE_CHECK(tide_game_api.client_hash(&world, recipient) == tide_game_api.client_hash(&restored, recipient));
    TIDE_CHECK(tide_get_Position64(&restored, ITEM).position.x == tide_get_Position64(&world, ITEM).position.x);
    free(bytes);
    tide_local_init(&local);
    tide_frame(&world, &previous, .5f, NULL, NULL, 1.0f, &local, &draw, &gui);
    const Position64 current = tide_get_Position64(&world, ITEM);
    const Position64 old = tide_get_Position64(&previous, ITEM);
    TIDE_CHECK(local.Display.position.x == old.position.x + .5 * (current.position.x - old.position.x));
    TIDE_CHECK(tide_world_hash(&world) == after && tide_world_hash(&previous) == before);
    tide_local_free(&local);
    tide_world_free(&previous);
    tide_world_free(&restored);
    tide_world_free(&world);
}

TIDE_TEST(double_commands_keep_scalar_and_vector_payload_bits)
{
    tide_world_init(&world, 1.0f);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, false, false, "");
    local.Display.submit = true;
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    uint32_t size = 0, channel = 123;
    const uint8_t *bytes = tide_local_command(&local, 0, &size);
    TIDE_REQUIRE(bytes && size && tide_game_api.check_command(bytes, size, &channel));
    TIDE_CHECK(channel == 0);
    for (uint32_t cut = 0; cut < size; cut++) TIDE_CHECK(!tide_game_api.check_command(bytes, cut, &channel));
    TIDE_REQUIRE(tide_game_api.command(&world, tide_player_from_index(0), 0, 1, bytes, size));
    tide_world_tick(&world);
    TIDE_CHECK(world.Results.commandValue == 1000000000.125);
    TIDE_CHECK(world.Results.destination.x == 1000000000.125 && world.Results.destination.y == .25);
    tide_local_free(&local);
    tide_world_free(&world);
}

TIDE_TEST(double_inputs_pack_exactly_and_repair_nonfinite_values)
{
    uint8_t bytes[256];
    static const uint64_t patterns[] = {0, UINT64_C(0x8000000000000000), 1, UINT64_C(0x000FFFFFFFFFFFFF),
        UINT64_C(0x0010000000000000), UINT64_C(0x7FEFFFFFFFFFFFFF), UINT64_C(0x3FF0000000000001),
        UINT64_C(0x7FF0000000000000), UINT64_C(0xFFF0000000000000), UINT64_C(0x7FF8123456789ABC)};
    Controls base = {0};
    for (size_t i = 0; i < sizeof patterns / sizeof patterns[0]; i++) {
        Controls in = {0}, out = {0};
        in.amount = tide_d_from_bits(patterns[i]);
        in.direction = tide_d3(in.amount, -in.amount, 3.25);
        in.samples.at[0] = in.amount;
        uint32_t size = tide_game_api.write_input(&in, bytes, sizeof bytes);
        TIDE_REQUIRE(size && tide_game_api.read_input(bytes, size, &out));
        TIDE_CHECK(tide_d_bits(out.amount) == patterns[i]);
        TIDE_CHECK(memcmp(&in.direction, &out.direction, sizeof in.direction) == 0);
        size = tide_game_api.write_input_delta(&in, &base, bytes, sizeof bytes);
        TIDE_REQUIRE(size && tide_game_api.read_input_delta(bytes, size, &base, &out));
        TIDE_CHECK(tide_d_bits(out.amount) == patterns[i]);
        TIDE_CHECK(tide_d_bits(out.samples.at[0]) == patterns[i]);
        base = in;
    }
    tide_world_init(&world, 1.0f);
    Controls in = {0};
    in.amount = tide_d_from_bits(UINT64_C(0x7FF8123456789ABC));
    in.direction = tide_d3(1e300, -1e300, tide_d_from_bits(UINT64_C(0x7FF0000000000000)));
    tide_world_set_input(&world, tide_player_from_index(0), in);
    tide_world_tick(&world);
    TIDE_CHECK(world.Results.input == 10.125);
    tide_world_free(&world);
}
