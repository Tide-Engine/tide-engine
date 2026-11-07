#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tide/pack.h"
#include "tide_test.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world, copy;

TIDE_TEST(retargeting_baked_profiles_and_ordinary_sampling)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 0.25f);
    tide_world_tick(&world);
    if (world.Probe.firstFailure) printf("    check %d of %d failed\n", world.Probe.firstFailure, world.Probe.checks);
    TIDE_REQUIRE(world.Probe.checks >= 25 && world.Probe.passed == world.Probe.checks);
    tide_world_copy(&copy, &world);
    const uint64_t hash = tide_world_hash(&copy);
    tide_world_tick(&world);
    TIDE_CHECK(tide_world_hash(&copy) == hash);
    tide_world_tick(&copy);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    tide_world_free(&copy);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    tide_world_pack(&world, bytes, size);
    memset(&copy, 0, sizeof copy);
    TIDE_REQUIRE(tide_world_unpack(&copy, bytes, size));
    free(bytes);
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world));
    for (int i = 0; i < 8; i++) { tide_world_tick(&copy); tide_world_tick(&world); }
    TIDE_CHECK(tide_world_hash(&copy) == tide_world_hash(&world) && world.Probe.passed == world.Probe.checks);
    TIDE_CHECK(tide_scratch_mark() == 0);
    // Every embedded profile list is authenticated before replacing a pack.
    uint8_t *changed = malloc(tide_game_pack_size);
    memcpy(changed, tide_game_pack, tide_game_pack_size);
    bool found = false;
    for (size_t i = 0; i + 20 < tide_game_pack_size; i++) {
        if (memcmp(changed + i, "TIDEHUM1", 8)) continue;
        changed[i + 8] ^= 1;
        found = true;
        break;
    }
    TIDE_REQUIRE(found);
    TIDE_CHECK(!tide_assets_load(changed, tide_game_pack_size));
    free(changed);
    tide_world_free(&copy);
    tide_world_free(&world);
    tide_pack_unload();
}
