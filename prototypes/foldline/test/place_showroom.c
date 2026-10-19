// Move with ordinary stick/look input while watching the stalker's actual
// place. Walls still decide whether the creature is visible to either body.
static void showroom_walk(tide_world *world, int id, float x, float z)
{
    for (int step = 0; step < 1800; step++) {
        const Operator body = pilot(world, id);
        const float dx = x - body.position.x, dz = z - body.position.z;
        const float gap = sqrtf(dx * dx + dz * dz);
        if (gap < .08f) {
            input(world, 0, (PilotInput){.active = id});
            tide_world_tick(world);
            return;
        }
        const float desired = atan2f(world->Stalker.at.x - body.position.x, world->Stalker.at.z - body.position.z);
        float turn = desired - body.yaw;
        while (turn > TIDE_PI_F) turn -= 2 * TIDE_PI_F;
        while (turn < -TIDE_PI_F) turn += 2 * TIDE_PI_F;
        turn = fmaxf(-.35f, fminf(.35f, turn));
        const float yaw = body.yaw + turn;
        const float sn = tide_sin_f(yaw), cs = tide_cos_f(yaw);
        const float pace = fminf(1, gap / .35f);
        input(world, 0, (PilotInput){.active = id, .look = {turn, -body.pitch},
                                     .move = {(dx * cs - dz * sn) / gap * pace, (dx * sn + dz * cs) / gap * pace}});
        const int chamber = world->Arena.chamber;
        tide_world_tick(world);
        if (world->Arena.chamber != chamber || world->Arena.caught > 0)
            fprintf(stderr, "Showroom caught body%d at%.2f,%.2f heading%.2f,%.2f; stalker%.2f,%.2f clock%.2f seen%d\n",
                    id, body.position.x, body.position.z, x, z, world->Stalker.at.x, world->Stalker.at.z,
                    world->Stalker.clock, world->Stalker.seen);
        CHECK(world->Arena.chamber == chamber && world->Arena.caught == 0);
    }
    fprintf(stderr, "Showroom: body %d stopped at %.2f %.2f toward %.2f %.2f\n", id,
            pilot(world, id).position.x, pilot(world, id).position.z, x, z);
    CHECK(false);
}

void place_showroom_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, SHOWROOM);
    CHECK(world.Place.dead && world.Place.kind == PlaceKind_Passage);
    ticks(&world, 480);
    CHECK(world.Place.id == SHOWROOM && world.Arena.caught == 0);
    const tide_float3 resting = world.Stalker.at;
    ticks(&world, 120);
    CHECK(near(world.Stalker.at.x, resting.x, .001f) && near(world.Stalker.at.z, resting.z, .001f));
    // The opaque landing side blocks sight, and from the landing's grille window it's too far off, through too many
    // bars, to keep it still: whoever watches has to be in the aisles, where it hunts. Reached through ordinary input.
    choose(&world, 0, SHOWROOM);
    ticks(&world, 100);
    aim_at(&world, 1, (tide_float3){-9, 1, -50});
    showroom_walk(&world, 0, -6.4f, -14.5f);
    CHECK(!world.Stalker.seen);
    showroom_walk(&world, 0, -10.5f, -13.5f);
    CHECK(!world.Stalker.seen);
    // Establish a real lookout beyond the first turn. Its retained yaw sees
    // through the display grilles while the moving partner crosses the aisles.
    choose(&world, 0, SHOWROOM);
    showroom_walk(&world, 0, -10.5f, -10.5f);
    showroom_walk(&world, 0, 10.5f, -10.5f);
    showroom_walk(&world, 1, -10.5f, -10.5f);
    showroom_walk(&world, 1, 10.5f, -10.5f);
    showroom_walk(&world, 1, 10.5f, -4.5f);
    showroom_walk(&world, 1, 4.5f, -4.5f);
    aim_at(&world, 0, (tide_float3){10.5f, 1, -40});
    while (world.Stalker.dark) tide_world_tick(&world);
    const tide_float3 watched = world.Stalker.at;
    ticks(&world, 12);
    CHECK(world.Stalker.seen);
    CHECK(near(world.Stalker.at.x, watched.x, .001f) && near(world.Stalker.at.z, watched.z, .001f));
    showroom_walk(&world, 0, 10.5f, -4.5f);
    showroom_walk(&world, 0, -10.5f, -4.5f);
    showroom_walk(&world, 1, -9.7f, -4.5f);
    showroom_walk(&world, 0, -10.5f, 1.5f);
    showroom_walk(&world, 0, 4.5f, 1.5f);
    showroom_walk(&world, 1, -10.5f, 1.5f);
    showroom_walk(&world, 1, 3.7f, 1.5f);
    showroom_walk(&world, 0, 4.5f, 7.5f);
    showroom_walk(&world, 0, 10.5f, 7.5f);
    showroom_walk(&world, 1, 4.5f, 7.5f);
    showroom_walk(&world, 1, 9.7f, 7.5f);
    showroom_walk(&world, 1, 10.5f, 10.5f);
    showroom_walk(&world, 1, 9, 14.1f);
    showroom_walk(&world, 0, 10.5f, 10.5f);
    showroom_walk(&world, 0, 9, 14.1f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber == chamber + 1);
    tide_world_free(&world);

    // With both bodies turned away the real stalker reaches the entered body,
    // then the ordinary caught timer rebuilds this passage for both bodies.
    start(&world);
    choose(&world, 0, SHOWROOM);
    showroom_walk(&world, 0, -10.5f, -10.5f);
    showroom_walk(&world, 0, 10.5f, -10.5f);
    aim_at(&world, 1, (tide_float3){-9, 1, -50});
    aim_at(&world, 0, (tide_float3){10.5f, 1, -50});
    const tide_float3 beforeHunt = world.Stalker.at;
    bool caught = false;
    for (int step = 0; step < 1200 && !caught; step++) {
        tide_world_tick(&world);
        caught = world.Arena.caught > 0;
    }
    CHECK(caught);
    CHECK(!near(world.Stalker.at.x, beforeHunt.x, .1f) || !near(world.Stalker.at.z, beforeHunt.z, .1f));
    const int beforeReset = world.Arena.chamber;
    ticks(&world, 100);
    CHECK(world.Arena.chamber == beforeReset + 1 && world.Place.id == SHOWROOM);
    CHECK(pilot(&world, 0).position.z < -12 && pilot(&world, 1).position.z < -12);
    tide_world_free(&world);
}
