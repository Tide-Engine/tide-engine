// Every state change in this solve comes from a real player input.
static void freight_jump(tide_world *world, float z, float high, bool under_shutter)
{
    Operator op = pilot(world, 0);
    aim_at(world, 0, (tide_float3){-9, op.position.y + (under_shutter ? 3.1f : 6), z + 3});
    input(world, 0, (PilotInput){.move = {0, 1}, .jump = true});
    tide_world_tick(world);
    bool across = false;
    for (int frame = 0; frame < 180; frame++) {
        input(world, 0, (PilotInput){.move = {0, 1}});
        tide_world_tick(world);
        op = pilot(world, 0);
        if (op.position.z >= z && op.position.y > high - .1f) { across = true; break; }
    }
    input(world, 0, (PilotInput){0});
    // The carried body can still be descending when the leading foot clears
    // the lip. Wait for the real landing rather than assuming 35 frames.
    for (int settle = 0; settle < 180; settle++) {
        const Operator landed = pilot(world, 0);
        if (landed.grounded && near(landed.position.y, high, .15f)) break;
        tide_world_tick(world);
    }
    if (!across || fabsf(pilot(world, 0).position.y - high) > .15f)
        fprintf(stderr, "freight jump: z %.2f high %.2f actual %.2f %.2f %.2f cube %.2f %.2f %.2f held %d\n", z, high,
                pilot(world, 0).position.x, pilot(world, 0).position.y, pilot(world, 0).position.z,
                world->Fold.cube.x, world->Fold.cube.y, world->Fold.cube.z, world->Fold.holding.at[0]);
    CHECK(across && near(pilot(world, 0).position.y, high, .15f));
}

static void freight_wait_phase(tide_world *world, int from, int to)
{
    for (int frame = 0; frame < 500; frame++) {
        const int phase = world->Arena.ticks % 396;
        if (phase >= from && phase <= to) return;
        tide_world_tick(world);
    }
    CHECK(false);
}

// Separate negative geometry fixture: direct origins sample sight lines;
// the complete solve below still starts in the lift and uses inputs only.
static void freight_blocked_shots(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, WEIGHBRIDGE);
    ticks(&world, 100);
    const float xs[] = {-12, -7, -2, 3, 8, 12};
    const float zs[] = {-12, -8, -4, 4, 8};
    for (int x = 0; x < 6; x++) for (int z = 0; z < 5; z++) {
        if (xs[x] < -4 && zs[z] > -3) continue; // Those cells are solid loading terraces, not ground.
        place(&world, 1, (tide_float3){xs[x], 0, zs[z]}, NORTH, 0);
        ticks(&world, 3);
        for (int target = 0; target < 3; target++) for (int high = 0; high < 2; high++) {
            aim_at(&world, 1, (tide_float3){-11 + target * 2, 6.7f + high * .2f, 13.99f});
            act(&world, 1, (PilotInput){.amber = true});
            if (world.Fold.ends.at[2].open && world.Fold.ends.at[2].at.y >= 5.4f)
                fprintf(stderr, "freight blocked shot: origin %.2f %.2f %.2f target %d high %d end %.2f %.2f %.2f\n", pilot(&world, 1).position.x, pilot(&world, 1).position.y, pilot(&world, 1).position.z, target, high, world.Fold.ends.at[2].at.x, world.Fold.ends.at[2].at.y, world.Fold.ends.at[2].at.z);
            CHECK(!world.Fold.ends.at[2].open || world.Fold.ends.at[2].at.y < 5.4f);
        }
    }
    for (int x = 0; x < 3; x++) for (int z = 0; z < 3; z++) {
        place(&world, 1, (tide_float3){6 + x * 3, 3.6f, -4 + z * 3}, NORTH, 0);
        ticks(&world, 3);
        for (int target = 0; target < 3; target++) for (int high = 0; high < 2; high++) {
            aim_at(&world, 1, (tide_float3){-11 + target * 2, 6.7f + high * .2f, 13.99f});
            act(&world, 1, (PilotInput){.amber = true});
            if (world.Fold.ends.at[2].open && world.Fold.ends.at[2].at.y >= 5.4f)
                fprintf(stderr, "freight blocked shot: origin %.2f %.2f %.2f target %d high %d end %.2f %.2f %.2f\n", pilot(&world, 1).position.x, pilot(&world, 1).position.y, pilot(&world, 1).position.z, target, high, world.Fold.ends.at[2].at.x, world.Fold.ends.at[2].at.y, world.Fold.ends.at[2].at.z);
            CHECK(!world.Fold.ends.at[2].open || world.Fold.ends.at[2].at.y < 5.4f);
        }
    }
    tide_world_free(&world);
}

static void place_weighbridge_tests(void)
{
    freight_blocked_shots();
    tide_world world = {0};
    start(&world);
    choose(&world, 0, WEIGHBRIDGE);
    ticks(&world, 100);
    CHECK(world.Place.id == WEIGHBRIDGE && world.Fold.ends.at[1].size == 2);
    walk_to(&world, 0, -9.2f, -12);
    walk_to(&world, 0, -7.2f, -10);
    aim_at(&world, 0, (tide_float3){-6, .4f, -9});
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, -12, -11.5f);
    cross_fold(&world, 0, (tide_float3){-14, 1.3f, -11.5f}, 120);
    ticks(&world, 30);
    CHECK(pilot(&world, 0).scale == 2 && world.Fold.cubeScale == 2 && world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, -9, -4.6f);
    freight_jump(&world, -.2f, 1.8f, false);
    freight_jump(&world, 3, 3.6f, false);
    aim_at(&world, 0, (tide_float3){-9, 6.7f, -4});
    walk_to(&world, 0, -9, 2);
    CHECK((world.Arena.signals & 1) == 0);

    walk_to(&world, 1, -6.8f, -12);
    walk_to(&world, 1, 8, -10);
    freight_wait_phase(&world, 0, 6);
    walk_to(&world, 1, 8, -7);
    for (int frame = 0; frame < 280 && pilot(&world, 1).position.y < 3.5f; frame++) tide_world_tick(&world);
    CHECK(pilot(&world, 1).position.y > 3.5f);
    walk_to(&world, 1, 8, -4.8f);
    walk_to(&world, 1, 8, 0);
    CHECK((world.Arena.signals & 1) != 0);
    // The service operator cannot shoot the rescue wall through its opaque
    // fourth riser. This uses a real shot from the actual plate.
    aim_at(&world, 1, (tide_float3){-9, 6.7f, 13.99f});
    act(&world, 1, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[2].open || world.Fold.ends.at[2].at.y < 5.4f);
    freight_jump(&world, 7, 5.4f, true);
    freight_jump(&world, 11.2f, 5.4f, false);
    // Set the large cargo down from beside the plate. A steep downward aim
    // with the giant pressed against the rear wall jams it into its own body.
    // Stay close enough that its held position reaches the plate after braking.
    walk_to(&world, 0, -8.2f, 11.5f);
    aim_at(&world, 0, (tide_float3){-9, 6.14f, 11.5f});
    ticks(&world, 45);
    CHECK((world.Arena.signals & 2) == 0 && world.Arena.gate < .01f);
    act(&world, 0, (PilotInput){.carry = true});
    ticks(&world, 120);
    CHECK(world.Fold.holding.at[0] == 0 && world.Fold.cubeScale == 2);
    if (!((world.Arena.signals & 2) != 0 && world.Arena.gate > .99f))
        fprintf(stderr, "Freight cargo %.3f %.3f %.3f scale%.3f holder%d operator %.3f %.3f %.3f signals%d\n", world.Fold.cube.x, world.Fold.cube.y, world.Fold.cube.z,
                world.Fold.cubeScale, world.Fold.holding.at[0], pilot(&world, 0).position.x, pilot(&world, 0).position.y, pilot(&world, 0).position.z, world.Arena.signals);
    CHECK((world.Arena.signals & 2) != 0 && world.Arena.gate > .99f);
    aim_at(&world, 0, (tide_float3){-9, 6.7f, 13.99f});
    act(&world, 0, (PilotInput){.amber = true});
    CHECK(near(world.Fold.ends.at[0].at.y, 6.7f, .06f) && world.Fold.ends.at[0].size == 1);
    aim_at(&world, 0, (tide_float3){-14, 1.3f, -11.5f});
    act(&world, 0, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].size == 1 && near(world.Fold.ends.at[1].at.z, -11.5f, .06f));

    walk_to(&world, 1, 8, -4.8f);
    freight_wait_phase(&world, 200, 205);
    walk_to(&world, 1, 8, -7);
    for (int frame = 0; frame < 320 && pilot(&world, 1).position.y > .1f; frame++) tide_world_tick(&world);
    CHECK(pilot(&world, 1).position.y < .1f);
    walk_to(&world, 1, -11.8f, -11.5f);
    cross_fold(&world, 1, (tide_float3){-14, 1.3f, -11.5f}, 120);
    CHECK(near(pilot(&world, 1).position.y, 5.4f, .15f));
    CHECK((world.Arena.signals & 1) == 0 && (world.Arena.signals & 2) != 0);
    walk_to(&world, 0, 7, 12);
    walk_to(&world, 0, 7, 16.1f);
    walk_to(&world, 1, 9.2f, 12);
    walk_to(&world, 1, 9.2f, 16.1f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id != WEIGHBRIDGE);
    tide_world_free(&world);
}
