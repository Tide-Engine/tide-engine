#include "game.h"
#include "tide_test.h"

static tide_world world;

// Entity handles are allocated deterministically: the Main scene is slot 0, and
// its Setup spawns a, b and the spawner first, so they get slots 1, 2 and 3,
// each at generation 1.
static tide_entity A;
static tide_entity B;
static tide_entity SPAWNER;

// Spawn archetypes come first, in source order: Health, Health + Poisoned,
// Spawner, Child.
#define CHILDREN world.arch3_Child

static void ticks(int n)
{
    for (int i = 0; i < n; i++) tide_world_tick(&world);
}

TIDE_TEST(structural_main_applies_spawns_then_adds)
{
    tide_world_init(&world, 1.0f);
    A = tide_entity_in_slot(&world.entities, 1);
    B = tide_entity_in_slot(&world.entities, 2);
    SPAWNER = tide_entity_in_slot(&world.entities, 3);
    TIDE_CHECK(tide_world_entity_count(&world) == 4); // The Main scene, a, b and the spawner
    TIDE_REQUIRE(tide_has_Health(&world, A));
    TIDE_CHECK(tide_get_Health(&world, A).value == 3);
    TIDE_REQUIRE(tide_has_Poisoned(&world, A)); // Add on a pending spawn.
    TIDE_CHECK(tide_get_Poisoned(&world, A).damage == 1);
    TIDE_CHECK(tide_get_Health(&world, B).value == 1);
    TIDE_CHECK(tide_get_Spawner(&world, SPAWNER).remaining == 2);
}

TIDE_TEST(structural_changes_apply_at_end_of_tick)
{
    tide_world_init(&world, 1.0f);
    A = tide_entity_in_slot(&world.entities, 1);
    B = tide_entity_in_slot(&world.entities, 2);
    SPAWNER = tide_entity_in_slot(&world.entities, 3);
    ticks(1);
    // b dropped to -4: it lost Poisoned and gained Dead, but Cleanup didn't see
    // Dead yet because the change was deferred.
    TIDE_CHECK(tide_get_Health(&world, B).value == -4);
    TIDE_CHECK(!tide_has_Poisoned(&world, B));
    TIDE_CHECK(tide_has_Dead(&world, B));
    TIDE_CHECK(tide_entity_alive(&world.entities, B));
    TIDE_CHECK(tide_get_Health(&world, A).value == 2);
    TIDE_CHECK(tide_world_entity_count(&world) == 5); // Main, a, b, spawner, one child
}

TIDE_TEST(structural_destroy_frees_the_entity)
{
    tide_world_init(&world, 1.0f);
    A = tide_entity_in_slot(&world.entities, 1);
    B = tide_entity_in_slot(&world.entities, 2);
    SPAWNER = tide_entity_in_slot(&world.entities, 3);
    ticks(2);
    TIDE_CHECK(!tide_entity_alive(&world.entities, B));
    TIDE_CHECK(!tide_has_Health(&world, B));
    TIDE_CHECK(tide_world_entity_count(&world) == 5); // Main, a, spawner, two children
}

TIDE_TEST(structural_spawned_children_point_at_spawner)
{
    tide_world_init(&world, 1.0f);
    A = tide_entity_in_slot(&world.entities, 1);
    B = tide_entity_in_slot(&world.entities, 2);
    SPAWNER = tide_entity_in_slot(&world.entities, 3);
    ticks(5);
    TIDE_CHECK(tide_get_Spawner(&world, SPAWNER).remaining == 0);
    TIDE_REQUIRE(CHILDREN.count == 2);
    for (uint32_t i = 0; i < CHILDREN.count; i++) {
        TIDE_CHECK(tide_entity_equal(TIDE_AT(&world, arch3_Child, Child, i).parent, SPAWNER));
    }
}

TIDE_TEST(structural_everything_poisoned_eventually_dies)
{
    tide_world_init(&world, 1.0f);
    A = tide_entity_in_slot(&world.entities, 1);
    B = tide_entity_in_slot(&world.entities, 2);
    SPAWNER = tide_entity_in_slot(&world.entities, 3);
    ticks(4);
    // a hit zero on tick 3 and was destroyed on tick 4.
    TIDE_CHECK(!tide_entity_alive(&world.entities, A));
    TIDE_CHECK(tide_world_entity_count(&world) == 4); // Main, the spawner and two children
}
