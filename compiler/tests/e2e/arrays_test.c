#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_world copy;
static tide_local local;

static const tide_entity board_entity = {1, 1};

// Whether two worlds hold the same: the same bytes (tide_world_pack, which
// leaves out where their pages are), and the same hash.
static bool same(const tide_world *a, const tide_world *b)
{
    const uint32_t size = tide_world_pack(a, NULL, 0);
    if (tide_world_pack(b, NULL, 0) != size) return false;
    uint8_t *x = malloc(size);
    uint8_t *y = malloc(size);
    tide_world_pack(a, x, size);
    tide_world_pack(b, y, size);
    const bool equal = memcmp(x, y, size) == 0;
    free(x);
    free(y);
    return equal && tide_world_hash(a) == tide_world_hash(b);
}

TIDE_TEST(arrays_all_checks_pass)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world);
    TIDE_REQUIRE(tide_has_Results(&world, board_entity));
    const Results r = tide_get_Results(&world, board_entity);
    if (r.firstFailure) printf("    check %d failed\n", (int)r.firstFailure);
    const tide_str shown = tide_text_read(&world.heap, r.shown);
    printf("    shown: %.*s\n", (int)shown.bytes, shown.ptr);
    printf("    checks: %d\n", (int)r.checks);
    TIDE_CHECK(r.checks == 18);
    TIDE_CHECK(r.passed == r.checks);
    TIDE_CHECK(tide_scratch_mark() == 0);
}

// An array is its component's bytes: C reads it as a struct of elements.
TIDE_TEST(arrays_are_component_bytes)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world);
    const Board b = tide_get_Board(&world, board_entity);
    TIDE_CHECK(sizeof b.cells == 8 * sizeof(int32_t));
    TIDE_CHECK(sizeof b.grid == 2 * 3 * sizeof(float));
    TIDE_CHECK(b.cells.at[0] == 3);
    TIDE_CHECK(b.stats.scores.at[1] == 42);
    TIDE_CHECK(b.grid.at[1].at[2] == 1.5f);
}

TIDE_TEST(arrays_snapshot_pack_and_hash)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_copy(&copy, &world);
    for (int i = 0; i < 5; i++) {
        tide_world_tick(&world);
        tide_world_tick(&copy);
    }
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&world, bytes, size);
    memset(&copy, 0, sizeof copy);
    TIDE_REQUIRE(tide_world_unpack(&copy, bytes, size));
    free(bytes);
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
    printf("    hash: %llu\n", (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == ARRAYS_HASH);
}

// An input's arrays: packed element by element, whole and as a delta, and
// repaired as they come in: NaN sticks become the default, and the dials are
// clamped.
TIDE_TEST(arrays_in_inputs_pack_and_repair)
{
    Hand in = {0};
    in.dials.at[0] = 3;
    in.dials.at[1] = 30;
    in.dials.at[2] = -5;
    in.sticks.at[0] = 0.25f;
    in.sticks.at[1] = 0.75f;
    in.cursor = 7;
    uint8_t buf[256];
    const uint32_t n = tide_game_api.write_input(&in, buf, sizeof buf);
    TIDE_REQUIRE(n > 0);
    Hand out = {0};
    TIDE_REQUIRE(tide_game_api.read_input(buf, n, &out));
    TIDE_CHECK(memcmp(&in, &out, sizeof in) == 0);
    Hand changed = in;
    changed.dials.at[1] = 31;
    changed.sticks.at[1] = 0.5f;
    const uint32_t d = tide_game_api.write_input_delta(&changed, &in, buf, sizeof buf);
    TIDE_REQUIRE(d > 0);
    Hand back = {0};
    TIDE_REQUIRE(tide_game_api.read_input_delta(buf, d, &in, &back));
    TIDE_CHECK(memcmp(&changed, &back, sizeof changed) == 0);

    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world);
    Hand bad = in;
    bad.sticks.at[0] = NAN;
    tide_world_set_server_input(&world, bad); // The board has no owner: it reads the server's input
    tide_world_tick(&world);
    const Board b = tide_get_Board(&world, board_entity);
    TIDE_CHECK(b.cells.at[0] == 3 + 10 + 0); // Clamped to 0..10
    TIDE_CHECK(b.cells.at[1] == 0);           // The NaN stick became the default, 0
}

TIDE_TEST(arrays_in_local_state_and_views)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    TIDE_CHECK(local.Mirror.sum == 24);
    TIDE_CHECK(local.Mirror.copy.at[0] == 4 && local.Mirror.copy.at[7] == 3);
    tide_local_free(&local);
}

// Two arrays separated by a struct: every inner field is bounded, and every
// element's padding cleared. Different lengths catch shadowed loop indices
// that would use the inner index for both arrays, past the outer array's end.
TIDE_TEST(arrays_of_structs_repair_inner_bounds_and_padding)
{
    Hand bad = {0};
    Hand clean = {0};
    for (int b = 0; b < 2; b++) {
        for (int d = 0; d < 3; d++) {
            InputDial *dial = &bad.banks.at[b].dials.at[d];
            dial->enabled = (b + d) % 2 == 0;
            dial->value = d == 0 ? -5 : d == 1 ? 20 : 7;
            memset(dial->tide_pad0, 0xa5, sizeof dial->tide_pad0);
            clean.banks.at[b].dials.at[d].enabled = dial->enabled;
            clean.banks.at[b].dials.at[d].value = d == 0 ? 0 : d == 1 ? 10 : 7;
        }
    }
    tide_world_init(&world, 1.0f);
    tide_world_init(&copy, 1.0f);
    tide_world_set_server_input(&world, bad);
    tide_world_set_server_input(&copy, clean);
    TIDE_CHECK(memcmp(&world.inputs[TIDE_SERVER_INPUT], &clean, sizeof clean) == 0);
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
}
