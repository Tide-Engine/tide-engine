#include "game.h"
#include "tide_test.h"

TIDE_TEST(arrays_of_forward_structs_have_their_values_and_layout)
{
    tide_world world = {0};
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    const Shelf shelf = world.Rack.shelf;
    TIDE_CHECK(world.Seen.total == 24);
    TIDE_CHECK(shelf.bags.at[0].items.at[0].at[1].used);
    TIDE_CHECK(shelf.bags.at[0].items.at[0].at[1].value == 5);
    TIDE_CHECK(shelf.bags.at[1].items.at[1].at[2].used);
    TIDE_CHECK(shelf.bags.at[1].items.at[1].at[2].value == 19);
    TIDE_CHECK(!shelf.bags.at[0].items.at[1].at[2].used);
    TIDE_CHECK(shelf.bags.at[0].items.at[1].at[2].value == 0);
    TIDE_CHECK(sizeof(Bag) == 2 * 3 * sizeof(Item));
    TIDE_CHECK(sizeof(Shelf) == 2 * sizeof(Bag));
    tide_world_free(&world);
}
