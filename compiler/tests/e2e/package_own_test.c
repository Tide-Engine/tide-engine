#include "game.h"
#include "tide_test.h"

static tide_world world;

// Inside a package its own names win, and its imports' over the game's global
// ones: Combat's Side(i) is its enum's conversion, and its Box Shapes'
// component, though the game has global functions of both names, and its
// Wheel Shapes' though the game has a component Wheel of its own. The game's
// code still finds Combat's function Shake past its command handler Shake.
TIDE_TEST(package_own_names_come_first)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.facing == 10); // Combat.Side(1) is Right
    TIDE_CHECK(world.Seen.side == 7);    // The game's Side
    TIDE_CHECK(world.Seen.shaken == 15); // Combat.Shake(5)
    TIDE_CHECK(world.Seen.boxes == 2);   // Combat's Crate spawned a Shapes.Box of size 2
    TIDE_CHECK(world.Seen.lit == 0.5f);  // The game's function Lit
    TIDE_CHECK(world.Seen.spokes == 4);  // Combat spawned Shapes.Wheel, not the game's Wheel
}
