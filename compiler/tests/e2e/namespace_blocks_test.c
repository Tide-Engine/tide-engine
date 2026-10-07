#include "game.h"
#include "tide_test.h"

static tide_world world;

// Namespace blocks: several namespaces in one file, one opened twice, blocks
// inside blocks and inside a file's own namespace, each declaration named in C
// by the namespace it's in.
TIDE_TEST(namespace_blocks_put_declarations_in_their_namespace)
{
    tide_world_init(&world, 1.0f);
    const tide_entity e = {1, 1};
    TIDE_REQUIRE(tide_has_Combat_Health(&world, e) && tide_has_Game_Items_Bag(&world, e));
    TIDE_CHECK(tide_get_Combat_Health(&world, e).hp == 10);
    tide_world_tick(&world);
    TIDE_CHECK(world.Seen.global == 107);            // The global Double
    TIDE_CHECK(world.Seen.combat == 14);             // Combat's, in its second block
    TIDE_CHECK(world.Seen.rules == 23);              // Rules.Raise through Combat's Double: 10 * 2 + 3, under the limit
    TIDE_CHECK(world.Seen.stance == Combat_Stance_High);
    TIDE_CHECK(world.Seen.order == 123);             // [After] by a block's plain name, and by a qualified one from outside
    TIDE_CHECK(world.Seen.bag == 4 * 10 + 3);        // Game.Items from two files; Game.Scale from around it; CRIT by `using`
    TIDE_CHECK(world.Seen.tool == 23 + 5);           // Tools.Sharpen by `using`, and Game.Tools.Deep.Edge
    TIDE_CHECK(world.Seen.hits == 3);                // A handler in the block of its event
    TIDE_CHECK(tide_get_Combat_Health(&world, e).hp == 23);
}
