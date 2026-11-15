#include "game.h"
#include "tide_test.h"

#include <string.h>

static tide_world world;

TIDE_TEST(enum_shorthand_uses_expected_types)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Shorthand.weapon == Weapon_Bow);
    tide_world_tick(&world);
    TIDE_CHECK(world.Shorthand.weapon == Weapon_Sword);
    TIDE_CHECK(world.Shorthand.checks == 39);
}

static void ticks(int n)
{
    for (int i = 0; i < n; i++) tide_world_tick(&world);
}

TIDE_TEST(enums_have_their_values)
{
    TIDE_CHECK(Phase_Warmup == 0);
    TIDE_CHECK(Phase_Playing == 5);
    TIDE_CHECK(Phase_Over == 6); // One more than the member before it
    TIDE_CHECK(Weapon_Bow == 1);
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Match.phase == Phase_Warmup);
    TIDE_CHECK(world.Match.loadout.primary == Weapon_Bow); // The struct's default
}

TIDE_TEST(enums_switch_between_phases)
{
    tide_world_init(&world, 1.0f);
    tide_world_set_server_input(&world, (Controls){.weapon = Weapon_Bow});
    ticks(2);
    TIDE_CHECK(world.Match.phase == Phase_Playing);
    TIDE_CHECK(world.Match.phaseValue == 5);
    ticks(3);
    // Points for ticks 2, 3 and 4: 0, 10 and 1.
    TIDE_CHECK(world.Match.score == 11);
    TIDE_CHECK(world.Match.phase == Phase_Over);
    TIDE_CHECK(world.Match.phaseValue == 6);
    ticks(1);
    TIDE_CHECK(world.Match.phase == Phase_Over); // default: nothing happens
}

TIDE_TEST(enums_from_other_machines_are_repaired)
{
    tide_world_init(&world, 1.0f);
    tide_world_set_server_input(&world, (Controls){.weapon = 7});
    ticks(1);
    TIDE_CHECK(world.Match.loadout.primary == Weapon_Sword); // 7 isn't a Weapon, so it's the default
    tide_world_set_server_input(&world, (Controls){.weapon = Weapon_Bow});
    ticks(1);
    TIDE_CHECK(world.Match.loadout.primary == Weapon_Bow);
}

TIDE_TEST(enums_convert_from_their_numbers)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Picks.spare == Weapon_Bow); // A constant, Weapon(1)
    ticks(1);
    TIDE_CHECK(world.Picks.rolled == Weapon_Sword);
    TIDE_CHECK(world.Picks.small == Weapon_Bow);
    TIDE_CHECK(world.Picks.voxel == Voxel_Stone);
    TIDE_CHECK(world.Picks.cases == 1); // case Weapon(0)
    ticks(1);
    TIDE_CHECK(world.Picks.rolled == Weapon_Bow);
    TIDE_CHECK(world.Picks.voxel == Voxel_Water);
    TIDE_CHECK(world.Picks.cases == 11); // case SPARE
}

// As C#'s cast: a number none of the members has is that value.
TIDE_TEST(enums_keep_numbers_no_member_has)
{
    tide_world_init(&world, 1.0f);
    ticks(1);
    TIDE_CHECK(world.Picks.odd == 3);
    TIDE_CHECK(world.Picks.oddValue == 3);
    TIDE_CHECK(world.Picks.oddCase == 2); // A switch's default
    // In text, a member's name, and the number where there's none
    TIDE_CHECK(strcmp(tide_str_c(tide_text_read(&world.heap, world.Picks.shown)), "Playing 3 Stone 255") == 0);
}

TIDE_TEST(enums_count_members_and_wrap)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Picks.members == 3);
    TIDE_CHECK(world.Picks.aliases == 4); // Sparse, negative and aliased values; Count is a member too
    TIDE_CHECK(world.Picks.forwardCount == 2);
    TIDE_CHECK(Later_A == 4 && Later_B == 5);
    ticks(1);
    TIDE_CHECK(world.Picks.direction == Direction_East);
    TIDE_CHECK(world.Picks.counts.at[3] == 5);
    TIDE_CHECK(world.Picks.countedCase == 1 && world.Picks.builtinCount == 3); // Rollback, Delay, Owned
    ticks(3);
    TIDE_CHECK(world.Picks.direction == Direction_North);
}

TIDE_TEST(enums_order_by_their_numbers)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Ordering.inOrder);
    ticks(3); // Playing by now, whichever of Advance and Order runs first
    TIDE_CHECK(world.Ordering.before == 2);      // Warmup and Playing come before Over
    TIDE_CHECK(world.Ordering.smallOrder);       // Voxel(-1) is 255, past Water; Voxel(256) is Air
    TIDE_CHECK(world.Ordering.shorthand);        // .Warmup and .Over take match.phase's enum
    TIDE_CHECK(world.Ordering.shorthandCount == 1);
}

TIDE_TEST(enum_values_are_every_member_in_order)
{
    tide_world_init(&world, 1.0f);
    TIDE_CHECK(world.Ordering.all.at[0] == Direction_North && world.Ordering.all.at[1] == Direction_East
               && world.Ordering.all.at[2] == Direction_South && world.Ordering.all.at[3] == Direction_West);
    TIDE_CHECK(world.Ordering.counted == 3);
    ticks(1);
    TIDE_CHECK(world.Ordering.phases == 11);
    TIDE_CHECK(world.Ordering.order == 123);
    TIDE_CHECK(world.Ordering.aliases == 130); // An alias is at its own place: Count() of them
    TIDE_CHECK(world.Ordering.sameSeen == 2);  // First and Same have one number
    TIDE_CHECK(world.Ordering.lastValue == Phase_Over);
    TIDE_CHECK(world.Ordering.voxelSum == 3);
}

// A byte's and a ushort's enums keep a number's low bits, as byte(x) does.
TIDE_TEST(enums_stored_small_keep_low_bits)
{
    TIDE_CHECK(sizeof(Voxel) == 1 && sizeof(Tile) == 2);
    tide_world_init(&world, 1.0f);
    ticks(1);
    TIDE_CHECK(world.Picks.wrapped == Voxel_Water); // 258: 2
    TIDE_CHECK(world.Picks.tile == Tile_Rock);      // 65537: 1
    TIDE_CHECK(world.Picks.wrappedConstant == 2);
    ticks(1);
    TIDE_CHECK(world.Picks.wrapped == 3);           // 259: 3, which is no member
    TIDE_CHECK(world.Picks.tile == Tile_Grass);     // 65536: 0
}
