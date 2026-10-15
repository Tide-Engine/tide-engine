static void place_exchange_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, EXCHANGE);
    ticks(&world, 100);
    CHECK(world.Place.id == EXCHANGE && world.Arena.signals == 0);
    // Two laps through the same half frame compound; no scripted resize.
    walk_to(&world, 1, -14, -8);
    cross_fold(&world, 1, (tide_float3){-16, 1.3f, -8}, 160);
    CHECK(near(pilot(&world, 1).scale, .5f, .001f));
    walk_to(&world, 1, -14, 2);
    walk_to(&world, 1, -14, -8);
    cross_fold(&world, 1, (tide_float3){-16, 1.3f, -8}, 240);
    CHECK(near(pilot(&world, 1).scale, .25f, .001f));
    walk_to(&world, 1, -13, 2);

    walk_to(&world, 0, -10, -6);
    aim_at(&world, 0, (tide_float3){-10, .4f, -4});
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, -10, 4.3f);
    aim_at(&world, 0, (tide_float3){-10, 0, 6});
    ticks(&world, 40);
    act(&world, 0, (PilotInput){.carry = true});
    ticks(&world, 70);
    CHECK((world.Arena.signals & 1) != 0);
    walk_to(&world, 0, -6, 4);
    walk_to(&world, 0, -6, -6);
    walk_to(&world, 0, 0, -6);
    walk_to(&world, 0, 1, -7);
    CHECK((world.Arena.signals & 2) == 0);

    walk_to(&world, 1, -6, 2);
    walk_to(&world, 1, -6, -6);
    walk_to(&world, 1, 2, -6);
    walk_to(&world, 1, 2, 0);
    walk_to(&world, 1, 6, 0);
    walk_to(&world, 1, 6, 7.5f);
    walk_to(&world, 1, 7.5f, 7.5f);
    aim_at(&world, 1, (tide_float3){18, 1.3f, 9});
    act(&world, 1, (PilotInput){.cyan = true});
    CHECK(near(world.Fold.ends.at[3].at.x, -16, .03f));
    walk_to(&world, 0, 1, -5);
    CHECK((world.Arena.signals & 2) != 0);
    // The plate holder cannot make the scout's shot from the same position.
    aim_at(&world, 0, (tide_float3){18, 1.3f, 9});
    act(&world, 0, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open);
    act(&world, 1, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[3].open && near(world.Fold.ends.at[3].at.x, 18, .03f));
    CHECK(near(world.Fold.ends.at[3].size, 1, .001f));
    // Both return before the cargo worker removes the cube from D0.
    walk_to(&world, 0, 0, -6);
    walk_to(&world, 0, -6, -6);
    walk_to(&world, 0, -7, 4.5f);
    CHECK((world.Arena.signals & 2) == 0);
    walk_to(&world, 1, 6, 7.5f);
    walk_to(&world, 1, 6, 0);
    walk_to(&world, 1, 2, 0);
    walk_to(&world, 1, 2, -6);
    walk_to(&world, 1, -6, -6);
    walk_to(&world, 1, -14, -8);
    cross_fold(&world, 1, (tide_float3){-16, 1.3f, -8}, 240);
    walk_to(&world, 1, 11, 9);
    walk_to(&world, 0, -10, 4.2f);
    aim_at(&world, 0, world.Fold.cube);
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, -12, 3);
    walk_to(&world, 0, -14, -8);
    cross_fold(&world, 0, (tide_float3){-16, 1.3f, -8}, 160);
    CHECK((world.Arena.signals & 1) == 0);
    walk_to(&world, 0, 14, 9.8f);
    aim_at(&world, 0, (tide_float3){14, 0, 11});
    ticks(&world, 40);
    act(&world, 0, (PilotInput){.carry = true});
    ticks(&world, 70);
    if (!(world.Arena.signals & 4))
        fprintf(stderr, "Exchange exit cube %.3f %.3f %.3f scale %.3f holder %d, operator %.3f %.3f %.3f\n", world.Fold.cube.x, world.Fold.cube.y, world.Fold.cube.z, world.Fold.cubeScale, tide_get_Cube(&world, world.Arena.cube).holder, pilot(&world, 0).position.x, pilot(&world, 0).position.y, pilot(&world, 0).position.z);
    CHECK((world.Arena.signals & 4) != 0 && world.Arena.gate > .99f);
    walk_to(&world, 0, 16, 9.3f);
    walk_to(&world, 0, 16, 13);
    walk_to(&world, 0, 14.2f, 16.1f);
    walk_to(&world, 1, 11, 13);
    walk_to(&world, 1, 11.8f, 16.1f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id != EXCHANGE);
    tide_world_free(&world);
}
