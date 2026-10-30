#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static uint32_t mark, expected;

void MeasureFreshStart(void)
{
    const tide_list data = tide_pack_skinned_of(tide_pack_find("character", TIDE_PACK_SKINNED), sizeof(Vertex3)).data;
    mark = tide_scratch_mark();
    tide_list transforms = {0};
    TIDE_REQUIRE(tide_skinned_rest(data, &transforms, TIDE_IN_SCRATCH));
    expected = tide_scratch_mark() - mark;
    tide_scratch_reset(mark);
}
bool MeasureFreshEnd(void)
{
    // A fresh pose owns exactly its constructor's allocation: no extra copy.
    return tide_scratch_mark() - mark == expected;
}
static bool same(const tide_world *a, const tide_world *b)
{
    const uint32_t size = tide_world_pack(a, NULL, 0);
    if (tide_world_pack(b, NULL, 0) != size) return false;
    uint8_t *x = malloc(size), *y = malloc(size);
    tide_world_pack(a, x, size); tide_world_pack(b, y, size);
    const bool equal = !memcmp(x, y, size) && tide_world_hash(a) == tide_world_hash(b);
    free(x); free(y);
    return equal;
}
TIDE_TEST(fresh_values_avoid_copies_without_aliasing_or_changing_branch_effects)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world world = {0}, copy = {0};
    tide_world_init(&world, 0.25f);
    for (int i = 0; i < 6; i++) tide_world_tick(&world);
    if (world.Probe.firstFailure) printf("    check %d of %d failed\n", world.Probe.firstFailure, world.Probe.checks);
    TIDE_REQUIRE(world.Probe.checks >= 60 && world.Probe.checks == world.Probe.passed);
    tide_world_copy(&copy, &world);
    const uint64_t hash = tide_world_hash(&copy);
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&copy) == hash);
    tide_world_tick(&copy);
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *data = malloc(size);
    tide_world_pack(&world, data, size);
    memset(&copy, 0, sizeof copy);
    TIDE_REQUIRE(tide_world_unpack(&copy, data, size));
    free(data);
    tide_world_tick(&world); tide_world_tick(&copy);
    TIDE_CHECK(same(&world, &copy) && world.Probe.checks == world.Probe.passed);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&copy); tide_world_free(&world);
    tide_pack_unload();
}
