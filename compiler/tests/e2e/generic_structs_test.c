#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;

static bool said(const tide_text t, const char *want)
{
    const tide_str s = tide_text_read(&world.heap, t);
    return (size_t)s.bytes == strlen(want) && memcmp(s.ptr, want, (size_t)s.bytes) == 0;
}

static void start(void)
{
    tide_world_free(&world);
    world = (tide_world){0};
    tide_world_init(&world, 0.1f);
    tide_world_tick(&world);
}

TIDE_TEST(generic_structs_are_a_struct_for_each_set_of_types)
{
    start();
    const Results *r = &world.Results;
    TIDE_CHECK(r->swappedFirst == 2);   // Pair<int>.Swapped
    TIDE_CHECK(r->setSecond == 1.5f);   // Pair<float>.Set, a mut method
    TIDE_CHECK(r->taggedValue == 1);    // Tag<U>: the struct's T and the method's own U
    TIDE_CHECK(said(r->taggedTag, "one"));
    // Two instantiations are two C structs, each the size of what it holds
    TIDE_CHECK(sizeof(Pair_int) == 2 * sizeof(int32_t));
    TIDE_CHECK(sizeof(Pair_float) == 2 * sizeof(float));
}

TIDE_TEST(generic_structs_hold_lists_and_maps_of_their_types)
{
    start();
    const Results *r = &world.Results;
    TIDE_CHECK(r->stackCount == 3);
    TIDE_CHECK(r->popped == 6);
    TIDE_CHECK(r->poppedAgain == 5);
    TIDE_CHECK(r->emptyPop);
    TIDE_CHECK(r->counterSize == 2); // "a" put twice
    TIDE_CHECK(r->nodes == 4);       // Node<int> holds a List<Node<int>>
    TIDE_CHECK(r->edges == 2);       // Edge<int>'s code reads Graph<int>'s fields, made one inside the other
    TIDE_CHECK(r->spot == 7);        // Geo.Spot<int> { at = 7 }
}

TIDE_TEST(generic_structs_give_their_type_arguments_to_calls)
{
    start();
    const Results *r = &world.Results;
    TIDE_CHECK(r->largerInt == 8);    // T from a Pair<T> argument
    TIDE_CHECK(r->largerMoney == 9);  // A comparable struct, by its operator <
    TIDE_CHECK(r->scaledFloat == 2.5f); // A plain struct's generic method
    TIDE_CHECK(r->scaledVector.x == 2 && r->scaledVector.y == 4 && r->scaledVector.z == 6);
}

TIDE_TEST(generic_structs_a_generic_is_a_funcs_value)
{
    start();
    static tide_local local;
    static tide_draw_list draw;
    static tide_gui gui;
    tide_local_init(&local);
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(local.Picks.picked == 9);          // Biggest as a Func<int, int, int>: Biggest<int>
    TIDE_CHECK(local.Picks.pickedFloat == 0.25f); // ...and as a Func<float, float, float>, another
    tide_draw_free(&draw);
    tide_local_free(&local);
}

TIDE_TEST(generic_structs_say_their_written_name_as_text)
{
    start();
    TIDE_CHECK(said(world.Results.shown, "Pair<int> { first = 1, second = 2 }"));
}

TIDE_TEST(generic_structs_are_components_fields)
{
    start();
    const Results *r = &world.Results;
    TIDE_CHECK(r->inventorySlots == 2);
    TIDE_CHECK(r->inventoryWeight == 2.5f);
    const tide_entity bag = tide_entity_in_slot(&world.entities, 1); // The Main scene is slot 0, the one spawned 1
    TIDE_REQUIRE(tide_has_Inventory(&world, bag));
    const Inventory inventory = tide_get_Inventory(&world, bag);
    TIDE_CHECK(inventory.weights.first == 2.5f && inventory.weights.second == 1.0f); // A Pair<float> field
}
