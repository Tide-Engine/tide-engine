static void place_reception_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, RECEPTION);
    ticks(&world, 100);
    CHECK(world.Place.id == RECEPTION && world.Fold.ends.at[0].open && world.Fold.ends.at[1].open);
    CHECK(world.Arena.signals == 0 && world.Arena.gate < .01f);

    // One operator supplies the west control; the other uses the pre-opened
    // route to reach the shutter. Every step of the solve below is input.
    walk_to(&world, 1, -4.8f, -6);
    walk_to(&world, 1, -10.5f, -3);
    cross_fold(&world, 1, (tide_float3){-12, 1.3f, -3}, 120);
    walk_to(&world, 1, 8, 0);
    aim_at(&world, 1, (tide_float3){8, 1.3f, 6});
    act(&world, 1, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[2].open && world.Fold.fizzleTime > 0);
    // A ray through the bars still meets the shutter immediately behind it.
    walk_to(&world, 0, -8, -6);
    walk_to(&world, 0, -8, 4);
    CHECK((world.Arena.signals & 1) != 0);
    aim_at(&world, 0, (tide_float3){8, 1.3f, 6});
    act(&world, 0, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].face.x > .99f && world.Fold.fizzleTime > 0);
    act(&world, 1, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[2].open && near(world.Fold.ends.at[2].at.z, 6, .02f));
    aim_at(&world, 1, (tide_float3){12, 1.3f, -6});
    act(&world, 1, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[3].open);
    walk_to(&world, 1, 10.5f, -6);
    cross_fold(&world, 1, (tide_float3){12, 1.3f, -6}, 120);
    CHECK(pilot(&world, 1).position.z > 2 && pilot(&world, 1).position.z < 6);
    act(&world, 1, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[1] == 1);
    cross_fold(&world, 1, (tide_float3){8, 1.3f, 6}, 120);
    walk_to(&world, 1, 8, -1);
    walk_to(&world, 1, 4, 3.8f);
    aim_at(&world, 1, (tide_float3){4, 0, 5.5f});
    ticks(&world, 40);
    act(&world, 1, (PilotInput){.carry = true});
    ticks(&world, 70);
    CHECK((world.Arena.signals & 2) != 0 && world.Arena.gate > .99f);
    // The west plate is momentary. Once its job is finished, both can leave.
    walk_to(&world, 0, -8, -3);
    CHECK((world.Arena.signals & 1) == 0);
    cross_fold(&world, 0, (tide_float3){-12, 1.3f, -3}, 160);
    walk_to(&world, 0, 3, -1);
    walk_to(&world, 0, 3, 7);
    walk_to(&world, 0, 4.8f, 10.1f);
    walk_to(&world, 1, 1.5f, 3.8f);
    walk_to(&world, 1, 1.5f, 7.2f);
    walk_to(&world, 1, 7.2f, 7.2f);
    walk_to(&world, 1, 7.2f, 10.1f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id != RECEPTION);
    tide_world_free(&world);
}
