#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

TIDE_TEST(animation_lookup_owns_outputs_resets_failures_and_preserves_snapshots)
{
    tide_world world = {0}, snapshot = {0};
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 0.25f);
    tide_world_tick(&world);
    if (world.Probe.firstFailure) printf("    check %d failed\n", world.Probe.firstFailure);
    TIDE_REQUIRE(world.Probe.checks == 24 && world.Probe.passed == world.Probe.checks);
    tide_world_copy(&snapshot, &world);
    const uint64_t hash = tide_world_hash(&snapshot);
    const uint32_t used = world.heap.used;
    tide_pack_unload();
    for (int i = 0; i < 32; i++) tide_world_tick(&world);
    TIDE_CHECK(world.Probe.passed == world.Probe.checks && tide_world_hash(&snapshot) == hash);
    TIDE_CHECK(world.heap.used == used);
    for (int i = 0; i < 32; i++) tide_world_tick(&snapshot);
    TIDE_CHECK(tide_world_hash(&snapshot) == tide_world_hash(&world));
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL);
    tide_world_pack(&world, bytes, size);
    TIDE_REQUIRE(tide_world_unpack(&snapshot, bytes, size));
    TIDE_CHECK(tide_world_hash(&snapshot) == tide_world_hash(&world));
    free(bytes);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
