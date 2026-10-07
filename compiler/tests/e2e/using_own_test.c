#include "game.h"
#include "tide_test.h"

static tide_world world;

// A name the game declares itself is the game's, though a namespace it
// imports has one too: the file's namespace, the ones around it out to the
// global one, and only then the `using` ones.
TIDE_TEST(using_own_names_come_first)
{
    tide_world_init(&world, 1.0f);
    const tide_entity e = {1, 1};
    TIDE_REQUIRE(tide_has_Body(&world, e) && tide_has_Lib_Body(&world, e) && tide_has_Lib_Hit(&world, e));
    TIDE_CHECK(tide_get_Body(&world, e).at.x == 1.0f && tide_get_Body(&world, e).at.y == 2.0f);
    TIDE_CHECK(tide_get_Lib_Body(&world, e).mass == 5);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.doubled == 105);     // The game's Double: 5 + 100
    TIDE_CHECK(world.Seen.hit == 1000 + 3 + 6); // Hit through `using`, and Lib.Double by its name
    TIDE_CHECK(world.Seen.inner == 101);        // In a namespace of the game's: its Body and its Double still
}
