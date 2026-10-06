#include <stdio.h>

#include "assets_same.h"
#include "game.h"
#include "tide_test.h"

static tide_world world;

// A world of the game with no assets: the size and hash the one with assets has too.
TIDE_TEST(assets_plain_world_is_the_same_as_with_assets)
{
    tide_world_init(&world, 1.0f / 60.0f);
    const uint64_t hash = tide_world_hash(&world);
    if (sizeof(tide_world) != ASSETS_WORLD_SIZE || hash != ASSETS_EMPTY_WORLD_HASH) {
        printf("assets_same.h: world size %zu, empty world hash 0x%016llx\n", sizeof(tide_world), (unsigned long long)hash);
    }
    TIDE_CHECK(sizeof(tide_world) == ASSETS_WORLD_SIZE);
    TIDE_CHECK(hash == ASSETS_EMPTY_WORLD_HASH);
    tide_world_tick(&world);
    TIDE_CHECK(world.Probe.heroWidth == 8);
    tide_world_free(&world);
}
