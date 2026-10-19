static void place_atrium_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, ATRIUM);
    ticks(&world, 100);
    CHECK(world.Place.id == ATRIUM && world.Arena.signals == 0);
    walk_to(&world, 1, -14, -14);
    walk_to(&world, 1, -14, 4);
    aim_at(&world, 1, (tide_float3){0, 5.2f, 8});
    act(&world, 1, (PilotInput){.cyan = true});
    CHECK(!world.Fold.ends.at[3].open);
    aim_at(&world, 1, (tide_float3){-8, 1.55f, 4});
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    ticks(&world, 70);
    input(&world, 0, (PilotInput){.active = 1});
    CHECK(pilot(&world, 1).position.x < -12.1f);
    act(&world, 1, (PilotInput){.jump = true});
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    ticks(&world, 60);
    input(&world, 0, (PilotInput){.active = 1});
    CHECK(pilot(&world, 1).position.x < -12.1f);
    walk_to(&world, 1, -14, 4);
    walk_to(&world, 1, -14, -14);
    // The pipe's door is shut but while something weighs on its plate: the cube does.
    walk_to(&world, 0, -17.8f, -11);
    aim_at(&world, 0, (tide_float3){-16, .4f, -11});
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, -14.5f, .4f);
    aim_at(&world, 0, (tide_float3){-14.5f, 0, 2});
    ticks(&world, 40);
    act(&world, 0, (PilotInput){.carry = true});
    ticks(&world, 70);
    CHECK((world.Arena.signals & 2) != 0);
    // A shrinks twice through their own pair and crosses to the island.
    walk_to(&world, 0, -18, -14);
    cross_fold(&world, 0, (tide_float3){-20, 1.3f, -14}, 160);
    CHECK(near(pilot(&world, 0).scale, .5f, .001f));
    walk_to(&world, 0, -18, -7);
    walk_to(&world, 0, -18, -14);
    cross_fold(&world, 0, (tide_float3){-20, 1.3f, -14}, 240);
    CHECK(near(pilot(&world, 0).scale, .25f, .001f));
    walk_to(&world, 0, -14, -7);
    walk_to(&world, 0, -14, 4);
    walk_to(&world, 0, -9, 4);
    walk_to(&world, 0, -6.5f, 4);
    walk_to(&world, 0, -6.5f, 18);
    walk_to(&world, 0, 0, 18);
    // The giant's end: A's half end off the gallery wall and into the big frame.
    aim_at(&world, 0, (tide_float3){0, 5.2f, 8});
    act(&world, 0, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].open && near(world.Fold.ends.at[1].size, 4, .001f));

    // B takes the cube off the gate plate, which shuts the pipe, and throws it into A's gallery end.
    walk_to(&world, 1, -14.5f, -.6f);
    aim_at(&world, 1, world.Fold.cube);
    act(&world, 1, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[1] == 1);
    ticks(&world, 20);
    CHECK((world.Arena.signals & 2) == 0);
    walk_to(&world, 1, -17.8f, -14);
    aim_at(&world, 1, (tide_float3){-20, 1.3f, -14});
    ticks(&world, 30);
    act(&world, 1, (PilotInput){.hurl = true});
    ticks(&world, 300);
    CHECK(near(world.Fold.cubeScale, 4, .001f));
    if (!(world.Arena.signals & 1))
        fprintf(stderr, "Atrium exit cube %.3f %.3f %.3f\n", world.Fold.cube.x, world.Fold.cube.y, world.Fold.cube.z);
    CHECK((world.Arena.signals & 1) != 0 && world.Arena.gate > .99f);

    // B is left whole, with the pipe shut. The service panel takes no end but the quarter frame's...
    walk_to(&world, 0, 6.5f, 18);
    walk_to(&world, 0, 6.5f, 9.5f);
    aim_at(&world, 0, (tide_float3){5.5f, 1.3f, 8});
    act(&world, 0, (PilotInput){.cyan = true});
    CHECK(near(world.Fold.ends.at[1].size, 4, .001f));
    // ...so A moves their island end into it: from the gallery's whole end, a way over at a quarter of the size.
    aim_at(&world, 0, (tide_float3){6.5f, .325f, 8});
    act(&world, 0, (PilotInput){.cyan = true});
    CHECK(near(world.Fold.ends.at[1].size, .25f, .001f) && near(world.Fold.ends.at[1].at.z, 8, .03f));
    walk_to(&world, 0, 7.5f, 12);
    walk_to(&world, 1, -18, -14);
    cross_fold(&world, 1, (tide_float3){-20, 1.3f, -14}, 160);
    CHECK(near(pilot(&world, 1).scale, .25f, .001f) && pilot(&world, 1).position.z > 7);
    walk_to(&world, 0, 6.5f, 18);
    walk_to(&world, 0, 1.2f, 18);
    walk_to(&world, 0, 1.2f, 22.1f);
    walk_to(&world, 1, 7.2f, 10);
    walk_to(&world, 1, 6.5f, 18);
    walk_to(&world, 1, -1.2f, 18);
    walk_to(&world, 1, -1.2f, 22.1f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id != ATRIUM);
    tide_world_free(&world);
}
