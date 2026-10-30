#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world, copy;

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 0.25f);
    tide_world_tick(&world);
    if (world.Probe.firstFailure) printf("    check %d of %d failed\n", world.Probe.firstFailure, world.Probe.checks);
    TIDE_REQUIRE(world.Probe.checks >= 35 && world.Probe.passed == world.Probe.checks);
}

static bool same(const tide_world *a, const tide_world *b)
{
    const uint32_t size = tide_world_pack(a, NULL, 0);
    if (tide_world_pack(b, NULL, 0) != size) return false;
    uint8_t *x = malloc(size), *y = malloc(size);
    tide_world_pack(a, x, size);
    tide_world_pack(b, y, size);
    const bool equal = !memcmp(x, y, size) && tide_world_hash(a) == tide_world_hash(b);
    free(x);
    free(y);
    return equal;
}

TIDE_TEST(retargeting_automatic_manual_references_and_prepared_maps)
{
    start();
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&world);
    tide_pack_unload();
}

TIDE_TEST(retargeting_owned_profiles_maps_poses_snapshot_and_pack)
{
    start();
    tide_world_copy(&copy, &world);
    const uint64_t before = tide_world_hash(&copy);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.height != copy.Probe.height && tide_world_hash(&copy) == before);
    tide_world_tick(&copy);
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&world, bytes, size);
    memset(&copy, 0, sizeof copy);
    TIDE_REQUIRE(tide_world_unpack(&copy, bytes, size));
    free(bytes);
    TIDE_CHECK(same(&world, &copy));
    for (int i = 0; i < 12; i++) {
        tide_world_tick(&world);
        tide_world_tick(&copy);
    }
    TIDE_CHECK(same(&world, &copy) && world.Probe.passed == world.Probe.checks);
    const uint32_t used = world.heap.used;
    for (int i = 0; i < 96; i++) tide_world_tick(&world);
    TIDE_CHECK(world.heap.used == used && world.Probe.passed == world.Probe.checks);
    TIDE_CHECK(tide_scratch_mark() == 0);
    tide_world_free(&copy);
    tide_world_free(&world);
    tide_pack_unload();
}
