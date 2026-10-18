#include <inttypes.h>
#include <stdio.h>

#include "game.h"
#include "tide_test.h"

TIDE_TEST(random_local_declarations_preserve_match_rolls)
{
    tide_world world = {0};
    tide_world snapshot = {0};
    tide_world_start_seeded(&world, 1.0f / 60.0f, NULL, 777u);
    TIDE_CHECK(world.Dice.fixed.state == tide_random_seeded(42).state);
    for (int tick = 0; tick < 10; tick++) tide_world_tick(&world);
    tide_world_copy(&snapshot, &world);
    for (int tick = 0; tick < 20; tick++) {
        tide_world_tick(&world);
        tide_world_tick(&snapshot);
        TIDE_CHECK(tide_world_hash(&world) == tide_world_hash(&snapshot));
    }
    const uint64_t hash = tide_world_hash(&world);
    printf("    hash: %" PRIu64 "\n", hash);
    TIDE_CHECK(hash == UINT64_C(5079178118440730783));
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
