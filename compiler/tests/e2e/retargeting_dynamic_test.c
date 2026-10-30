#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world, copy;
static tide_world *active = &world;

int64_t StateHash(void) { return (int64_t)tide_world_hash(active); }
int32_t HeapUsed(void) { return (int32_t)active->heap.used; }

static void tick(tide_world *value)
{
    active = value;
    tide_world_tick(value);
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

TIDE_TEST(retargeting_dynamic_owned_bakes_survive_inputs_pack_and_snapshots)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 0.25f);
    tick(&world);
    if (world.Probe.firstFailure) printf("    check %d of %d failed\n", world.Probe.firstFailure, world.Probe.checks);
    TIDE_REQUIRE(world.Probe.checks >= 25 && world.Probe.checks == world.Probe.passed);
    tide_pack_unload();
    tide_world_copy(&copy, &world);
    const uint64_t hash = tide_world_hash(&copy);
    tick(&world);
    TIDE_CHECK(tide_world_hash(&copy) == hash);
    tick(&copy);
    TIDE_CHECK(same(&world, &copy));
    tide_world_free(&copy);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *data = malloc(size);
    tide_world_pack(&world, data, size);
    memset(&copy, 0, sizeof copy);
    TIDE_REQUIRE(tide_world_unpack(&copy, data, size));
    free(data);
    TIDE_CHECK(same(&world, &copy));
    for (int i = 0; i < 12; i++) { tick(&world); tick(&copy); }
    TIDE_CHECK(same(&world, &copy) && world.Probe.passed == world.Probe.checks);
    const uint32_t used = world.heap.used;
    for (int i = 0; i < 60; i++) tick(&world);
    TIDE_CHECK(world.heap.used == used && world.Probe.passed == world.Probe.checks);
    TIDE_CHECK(tide_scratch_mark() == 0);
    printf("    baked world hash: %llu\n", (unsigned long long)tide_world_hash(&world));
    TIDE_CHECK(tide_world_hash(&world) == UINT64_C(6273408732930949875));
    tide_world_free(&copy);
    tide_world_free(&world);
}
