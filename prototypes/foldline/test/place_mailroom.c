static void place_mailroom_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, MAILROOM);
    ticks(&world, 100);
    CHECK(world.Place.id == MAILROOM && world.Fold.ends.at[2].open && world.Fold.ends.at[3].size == .5f);

    // The normal cargo worker cannot walk through the parcel duct.
    walk_to(&world, 0, 2, -3);
    aim_at(&world, 0, (tide_float3){14, 1.3f, 8.5f});
    act(&world, 0, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open);
    aim_at(&world, 0, (tide_float3){12, 1.55f, -3});
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 65);
    input(&world, 0, (PilotInput){0});
    CHECK(pilot(&world, 0).position.x < 4.1f);
    act(&world, 0, (PilotInput){.jump = true});
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 60);
    input(&world, 0, (PilotInput){0});
    CHECK(pilot(&world, 0).position.x < 4.1f);

    // B becomes the half-sized scout. No state is moved by the test.
    walk_to(&world, 1, -8, -7);
    walk_to(&world, 1, -12, -5);
    cross_fold(&world, 1, (tide_float3){-14, 1.3f, -5}, 160);
    CHECK(near(pilot(&world, 1).scale, .5f, .001f));
    walk_to(&world, 1, -10, 6);
    walk_to(&world, 1, -10, 2);
    walk_to(&world, 1, -6, 2);
    walk_to(&world, 1, -6, 1);
    aim_at(&world, 1, world.Fold.cube);
    act(&world, 1, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[1] == 0 && world.Fold.strained.at[1] > .1f);
    walk_to(&world, 1, -6, 2);
    walk_to(&world, 1, 2, 2);
    walk_to(&world, 1, 2, -3);
    walk_to(&world, 1, 6, -3);
    walk_to(&world, 1, 9, -3);
    walk_to(&world, 1, 9, 3);
    walk_to(&world, 1, 9, 6);
    walk_to(&world, 1, 11, 7);
    ticks(&world, 20);
    CHECK(world.Arena.signals == 0 && world.Arena.gate < .01f);
    // Taking the end off its half frame makes a whole-sized cargo route.
    aim_at(&world, 1, (tide_float3){14, 1.3f, 8.5f});
    act(&world, 1, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[3].open && near(world.Fold.ends.at[3].size, 1, .001f));
    CHECK(near(world.Fold.ends.at[3].at.x, 14, .02f));
    walk_to(&world, 1, 8, 6);

    walk_to(&world, 0, 2, 0);
    walk_to(&world, 0, -6.8f, 1);
    aim_at(&world, 0, (tide_float3){-5, .4f, 1});
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, -9, -3);
    walk_to(&world, 0, -12, -5);
    cross_fold(&world, 0, (tide_float3){-14, 1.3f, -5}, 160);
    CHECK(pilot(&world, 0).position.x > 6 && near(pilot(&world, 0).scale, 1, .001f));
    walk_to(&world, 0, 11, 5.4f);
    aim_at(&world, 0, (tide_float3){11, 0, 7});
    ticks(&world, 40);
    act(&world, 0, (PilotInput){.carry = true});
    ticks(&world, 70);
    CHECK((world.Arena.signals & 1) != 0 && world.Arena.gate > .99f);
    walk_to(&world, 0, 12.4f, 5.4f);
    walk_to(&world, 0, 12.4f, 10.2f);
    walk_to(&world, 0, 11.2f, 14.1f);
    walk_to(&world, 1, 8, 10);
    walk_to(&world, 1, 8.8f, 14.1f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id != MAILROOM);
    tide_world_free(&world);
}
