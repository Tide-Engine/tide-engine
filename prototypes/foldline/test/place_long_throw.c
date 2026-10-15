// Explicit setup is only for negative sight/jump checks. The cargo solve
// following this fixture never positions a player, object, or fold directly.
static void launch_blocked_shots(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, LONG_THROW);
    ticks(&world, 100);
    const tide_float3 origins[] = {
        {-11, 0, -10}, {-9, 0, -5}, {-7, 0, 1}, {-5, 0, -10}, {-2, 0, -7}, {1, 0, -10}
    };
    for (int i = 0; i < 6; i++) {
        place(&world, 1, origins[i], NORTH, 0);
        ticks(&world, 3);
        aim_at(&world, 1, (tide_float3){-6, 9.3f, 6});
        act(&world, 1, (PilotInput){.amber = true});
        CHECK(!world.Fold.ends.at[2].open || world.Fold.ends.at[2].at.y < 8);
        aim_at(&world, 1, (tide_float3){12, 6.7f, 13.5f});
        act(&world, 1, (PilotInput){.cyan = true});
        CHECK(!world.Fold.ends.at[3].open || world.Fold.ends.at[3].at.x < 6);
    }
    const tide_float3 cage[] = {{-1.6f, 8, -11.3f}, {1.6f, 8, -5}, {0, 8, -1.5f}};
    for (int i = 0; i < 3; i++) {
        place(&world, 1, cage[i], NORTH, 0);
        ticks(&world, 3);
        aim_at(&world, 1, (tide_float3){12, 6.7f, 13.5f});
        act(&world, 1, (PilotInput){.cyan = true});
        CHECK(!world.Fold.ends.at[3].open || world.Fold.ends.at[3].at.x < 6);
    }
    place(&world, 1, (tide_float3){0, 8, -1.5f}, NORTH, 0);
    ticks(&world, 3);
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}, .jump = true});
    ticks(&world, 120);
    input(&world, 0, (PilotInput){.active = 1});
    CHECK(pilot(&world, 1).position.z < 0);
    tide_world_free(&world);
}

static void place_long_throw_tests(void)
{
    launch_blocked_shots();
    tide_world world = {0};
    start(&world);
    choose(&world, 0, LONG_THROW);
    ticks(&world, 100);
    CHECK(world.Place.id == LONG_THROW && world.Arena.gate < .01f);
    // A boards at the bottom. B works the plate while A is an idle rider.
    walk_to(&world, 0, -10.2f, -10.8f);
    walk_to(&world, 0, 0, -10.8f);
    walk_to(&world, 0, 0, -9);
    walk_to(&world, 1, -7.8f, -10);
    walk_to(&world, 1, -3, -9);
    CHECK((world.Arena.signals & 1) != 0);
    // The lower operator's real shot cannot hit the high receiving wall.
    aim_at(&world, 1, (tide_float3){-6, 9.3f, 6});
    act(&world, 1, (PilotInput){.cyan = true});
    CHECK(!world.Fold.ends.at[3].open || world.Fold.ends.at[3].at.y < 8);
    for (int frame = 0; frame < 360 && pilot(&world, 0).position.y < 7.9f; frame++) tide_world_tick(&world);
    CHECK(near(pilot(&world, 0).position.y, 8, .15f));
    walk_to(&world, 0, -1.6f, -11.3f);
    aim_at(&world, 0, (tide_float3){-10, 1.3f, 0});
    act(&world, 0, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].open && near(world.Fold.ends.at[0].at.z, 0, .02f));
    aim_at(&world, 0, (tide_float3){-6, 9.3f, 6});
    act(&world, 0, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].open && near(world.Fold.ends.at[1].at.y, 9.3f, .06f));
    // The rescue panel is the front of an inside corner, not a free shot from
    // the upper operator's cage.
    aim_at(&world, 0, (tide_float3){12, 6.7f, 13.5f});
    // Keep A's launch pair intact; B supplies this negative shot after leaving
    // the plate, from the near floor, before collecting the cube.
    walk_to(&world, 1, -8, -7);
    CHECK((world.Arena.signals & 1) == 0);
    aim_at(&world, 1, (tide_float3){12, 6.7f, 13.5f});
    act(&world, 1, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[2].open || world.Fold.ends.at[2].at.x < 6);
    aim_at(&world, 1, (tide_float3){-9.5f, .4f, -7});
    act(&world, 1, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[1] == 1);
    walk_to(&world, 1, -10, -3.1f);
    aim_at(&world, 1, (tide_float3){-10, 1.3f, 0});
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    bool landed = false, crossed = false;
    float edge_height = 0;
    for (int frame = 0; frame < 240; frame++) {
        tide_world_tick(&world);
        const Operator flyer = pilot(&world, 1);
        crossed = crossed || flyer.position.y > 7;
        if (flyer.position.x > .75f && edge_height == 0) edge_height = flyer.position.y;
        if (flyer.position.x > 6 && flyer.grounded && flyer.position.y > 5.2f) {
            landed = true;
            break;
        }
        if (flyer.taken > 0) break;
    }
    input(&world, 0, (PilotInput){.active = 1});
    ticks(&world, 30);
    if (!landed)
        fprintf(stderr, "long throw: landed %d crossed %d at %.3f %.3f %.3f velocity %.3f %.3f %.3f cube %.3f %.3f %.3f held %d\n",
                landed, crossed, pilot(&world, 1).position.x, pilot(&world, 1).position.y, pilot(&world, 1).position.z,
                pilot(&world, 1).velocity.x, pilot(&world, 1).velocity.y, pilot(&world, 1).velocity.z,
                world.Fold.cube.x, world.Fold.cube.y, world.Fold.cube.z, world.Fold.holding.at[1]);
    CHECK(landed && crossed && near(pilot(&world, 1).position.y, 5.4f, .1f));
    CHECK(world.Fold.holding.at[1] == 1 && edge_height > 5.6f);
    walk_to(&world, 1, 13, 7);
    walk_to(&world, 1, 14, 11.7f);
    aim_at(&world, 1, (tide_float3){14, 5.4f, 13});
    ticks(&world, 40);
    act(&world, 1, (PilotInput){.carry = true});
    ticks(&world, 90);
    CHECK((world.Arena.signals & 2) != 0 && world.Arena.gate > .99f);
    walk_to(&world, 1, 15.3f, 11.7f);
    walk_to(&world, 1, 15.3f, 13.5f);
    walk_to(&world, 1, 14, 13.5f);
    aim_at(&world, 1, (tide_float3){12, 6.7f, 13.5f});
    act(&world, 1, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[2].open && near(world.Fold.ends.at[2].at.x, 12, .02f));
    walk_to(&world, 1, 15.3f, 13.5f);
    walk_to(&world, 1, 15.3f, 10.5f);
    walk_to(&world, 1, 13, 7);
    aim_at(&world, 1, (tide_float3){0, 9.3f, -12});
    act(&world, 1, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[3].open && near(world.Fold.ends.at[3].at.z, -12, .02f));
    walk_to(&world, 0, 0, -11.2f);
    cross_fold(&world, 0, (tide_float3){0, 9.3f, -12}, 100);
    CHECK(pilot(&world, 0).position.x > 12 && near(pilot(&world, 0).position.y, 5.4f, .15f));
    walk_to(&world, 0, 12.8f, 16.3f);
    walk_to(&world, 0, 11.8f, 18.1f);
    walk_to(&world, 1, 15.3f, 10.5f);
    walk_to(&world, 1, 15.3f, 15.2f);
    walk_to(&world, 1, 14.2f, 18.1f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id != LONG_THROW);
    tide_world_free(&world);
}
