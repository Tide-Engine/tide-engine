static void place_archive_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, ARCHIVE);
    ticks(&world, 100);
    CHECK(world.Place.dead && world.Place.kind == PlaceKind_Passage);
    walk_to(&world, 0, -8, -10);
    walk_to(&world, 0, 6, -9.5f);
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    aim_at(&world, 0, (tide_float3){10, 2, -1});
    act(&world, 0, (PilotInput){.hurl = true});
    ticks(&world, 75);
    walk_to(&world, 0, -8, -7);
    walk_to(&world, 0, -8, 2);
    CHECK(world.Arena.caught == 0);
    walk_to(&world, 1, -8, -7);
    walk_to(&world, 1, -8, 2);
    CHECK(world.Arena.caught == 0);

    // The second quiet bank has the next physical distraction. Both cross
    // while its clatter occupies the listener on the other side of the room.
    walk_to(&world, 0, -4, .5f);
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 2);
    aim_at(&world, 0, (tide_float3){10, 2, 6});
    act(&world, 0, (PilotInput){.hurl = true});
    ticks(&world, 60);
    walk_to(&world, 0, -8, 2);
    walk_to(&world, 0, -8, 8);
    walk_to(&world, 1, -8, 8);
    CHECK(world.Arena.caught == 0);
    walk_to(&world, 0, 6.8f, 9);
    walk_to(&world, 0, 6.8f, 14.1f);
    walk_to(&world, 1, 9.2f, 9);
    walk_to(&world, 1, 9.2f, 14.1f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id != ARCHIVE);
    tide_world_free(&world);

    // Walking noisily toward the creature without a decoy is caught, and
    // rebuilds the passage for both. No test positions the live encounter.
    start(&world);
    choose(&world, 0, ARCHIVE);
    ticks(&world, 100);
    walk_to(&world, 0, 0, -7);
    aim_at(&world, 0, (tide_float3){0, 1.3f, 7});
    input(&world, 0, (PilotInput){.move = {0, 1}});
    bool caught = false;
    for (int i = 0; i < 240 && !caught; i++) {
        tide_world_tick(&world);
        caught = world.Arena.caught > 0;
    }
    CHECK(caught);
    input(&world, 0, (PilotInput){0});
    const int before = world.Arena.chamber;
    ticks(&world, 100);
    CHECK(world.Arena.chamber > before && world.Place.id == ARCHIVE);
    tide_world_free(&world);
}
