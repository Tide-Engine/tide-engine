#include "game.h"
#include "tide_test.h"

static tide_world world;

// Default order: Physics.Move, Physics.Gravity, Combat.Damage, Items.Heal
// (files by path, then declaration order). [After(Gravity)] on Move and
// [Before(Physics.Move)] on Damage give Gravity, Damage, Move, Heal.
TIDE_TEST(multi_file_order)
{
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    TIDE_CHECK(world.Trace.gravity == 0);
    TIDE_CHECK(world.Trace.damage == 1);
    TIDE_CHECK(world.Trace.move == 2);
    TIDE_CHECK(world.Trace.heal == 3);
    TIDE_CHECK(world.Trace.step == 4);
}

// Namespaced names in C: Combat.Health is Combat_Health.
TIDE_TEST(multi_file_namespaces)
{
    tide_world_init(&world, 1.0f);
    const tide_entity e = {1, 1};
    TIDE_REQUIRE(tide_has_Physics_Body(&world, e));
    TIDE_REQUIRE(tide_has_Combat_Health(&world, e));
    TIDE_REQUIRE(tide_has_Items_Health(&world, e));
    TIDE_CHECK(tide_get_Combat_Health(&world, e).value == 10);
    TIDE_CHECK(tide_get_Items_Health(&world, e).value == 3);
    tide_world_tick(&world);
    TIDE_CHECK(tide_get_Combat_Health(&world, e).value == 10); // Heal changes Items.Health only
    TIDE_CHECK(tide_get_Items_Health(&world, e).value == 4);
}

// A struct and a function used by their qualified names: Combat.Hit, as a
// field and a local, and Combat.Strength.
TIDE_TEST(multi_file_qualified_names)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Trace.hit.amount == 2); // Its default
    tide_world_tick(&world);
    TIDE_CHECK(world.Trace.hit.amount == 5); // Set by Combat.Damage...
    TIDE_CHECK(world.Trace.hit_seen == 10);  // ...then read by Items.Heal, through Combat.Strength
}

// An enum of a namespace converts from its number by its name there, Stance(4),
// and by its qualified name elsewhere, Combat.Stance(4).
TIDE_TEST(multi_file_qualified_enum_conversion)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Trace.high == Combat_Stance_High); // Its default, Combat.Stance(4)
    tide_world_tick(&world);
    TIDE_CHECK(world.Trace.stance == Combat_Stance_High); // Stance(5 - 1), in Combat
    TIDE_CHECK(world.Trace.healed == Combat_Stance_High); // Combat.Stance(4), in Items: its Health was 3, healed to 4
}
