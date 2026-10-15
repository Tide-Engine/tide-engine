// What weighs on the plate of a signal, as the match has it.
static float exchange_load(const tide_world *world, int signal)
{
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Plate(world, entity)) continue;
        const Plate plate = tide_get_Plate(world, entity);
        if (plate.chamber == world->Arena.chamber && plate.signal == signal) return plate.load;
    }
    CHECK(false);
    return 0;
}

static void place_exchange_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, EXCHANGE);
    ticks(&world, 100);
    CHECK(world.Place.id == EXCHANGE && world.Arena.signals == 0);

    // The cube alone is too light for the door's heavy plate: its gauge shows it, and the door stays shut.
    walk_to(&world, 0, -10, -6);
    aim_at(&world, 0, (tide_float3){-10, .4f, -4});
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, -10, 4.3f);
    aim_at(&world, 0, (tide_float3){-10, 0, 6});
    ticks(&world, 40);
    act(&world, 0, (PilotInput){.carry = true});
    ticks(&world, 70);
    CHECK((world.Arena.signals & 1) == 0 && near(exchange_load(&world, 0), 48, 1));
    // A person holds it. The cube's job is the hall's plate, which B carries it to.
    aim_at(&world, 0, world.Fold.cube);
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, -12.5f, 4.3f);
    aim_at(&world, 0, (tide_float3){-14, .4f, 4.3f});
    act(&world, 0, (PilotInput){.carry = true});
    ticks(&world, 40);
    walk_to(&world, 0, -10, 6);
    ticks(&world, 30);
    CHECK((world.Arena.signals & 1) != 0);
    walk_to(&world, 1, -12, 2);
    walk_to(&world, 1, world.Fold.cube.x + 1.4f, world.Fold.cube.z);
    aim_at(&world, 1, world.Fold.cube);
    act(&world, 1, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[1] == 1);
    walk_to(&world, 1, -6, -6);
    walk_to(&world, 1, 0, -6);
    walk_to(&world, 1, 0, -5);
    aim_at(&world, 1, (tide_float3){1.3f, 0, -5});
    ticks(&world, 40);
    act(&world, 1, (PilotInput){.carry = true});
    ticks(&world, 70);
    if (!(world.Arena.signals & 2))
        fprintf(stderr, "Exchange hall cube %.3f %.3f %.3f load %.1f, B %.3f %.3f %.3f\n", world.Fold.cube.x, world.Fold.cube.y, world.Fold.cube.z,
                exchange_load(&world, 1), pilot(&world, 1).position.x, pilot(&world, 1).position.y, pilot(&world, 1).position.z);
    CHECK((world.Arena.signals & 2) != 0);

    // B goes back out, shrinks twice through their own pair, and crawls the duct to the pocket by the shutter.
    walk_to(&world, 1, -6, -6);
    walk_to(&world, 1, -14, -8);
    cross_fold(&world, 1, (tide_float3){-16, 1.3f, -8}, 160);
    CHECK(near(pilot(&world, 1).scale, .5f, .001f));
    walk_to(&world, 1, -14, 2);
    walk_to(&world, 1, -14, -8);
    cross_fold(&world, 1, (tide_float3){-16, 1.3f, -8}, 240);
    CHECK(near(pilot(&world, 1).scale, .25f, .001f));
    walk_to(&world, 1, -13, 2);
    walk_to(&world, 1, -6, 2);
    walk_to(&world, 1, -6, -6);
    walk_to(&world, 1, 2, -6);
    walk_to(&world, 1, 2, 0);
    walk_to(&world, 1, 6, 0);
    walk_to(&world, 1, 6, 7.5f);
    // What was waiting at the duct's end has gone through the bars, and is in the bank's far corner
    CHECK(tide_get_Shy(&world, find_beast(&world, BeastKind_Crawler)).fled >= 0);
    walk_to(&world, 1, 7.5f, 7.5f);
    // Off its half frame and onto the far wall, through the slot the cube holds open: a whole-sized cargo route.
    aim_at(&world, 1, (tide_float3){18, 1.3f, 9});
    act(&world, 1, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[3].open && near(world.Fold.ends.at[3].at.x, 18, .03f) && near(world.Fold.ends.at[3].size, 1, .001f));
    walk_to(&world, 1, 6, 7.5f);
    walk_to(&world, 1, 6, 0);
    walk_to(&world, 1, 2, 0);
    walk_to(&world, 1, 2, -6);
    walk_to(&world, 1, -6, -6);
    walk_to(&world, 1, -12, -3);

    // Too small to carry the cube back out, B grows again: A, still on the plate, puts a pair of their own into the
    // freed half frame and beside it, and B walks in at the small end twice.
    aim_at(&world, 0, (tide_float3){-16, .65f, 2});
    act(&world, 0, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].open && near(world.Fold.ends.at[0].size, .5f, .001f));
    aim_at(&world, 0, (tide_float3){-16, 1.3f, -3});
    act(&world, 0, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].open && near(world.Fold.ends.at[1].size, 1, .001f) && near(world.Fold.ends.at[1].at.z, -3, .05f));
    CHECK((world.Arena.signals & 1) != 0);
    walk_to(&world, 1, -14.2f, 2);
    cross_fold(&world, 1, (tide_float3){-16, .33f, 2}, 240);
    CHECK(near(pilot(&world, 1).scale, .5f, .001f));
    walk_to(&world, 1, -14, -1);
    walk_to(&world, 1, -14.6f, 2);
    cross_fold(&world, 1, (tide_float3){-16, .65f, 2}, 240);
    CHECK(near(pilot(&world, 1).scale, 1, .001f));

    // Whole again, B fetches the cube, which lets the shutter down, and takes it through their own pair to the exit's plate.
    walk_to(&world, 1, -12, -3);
    walk_to(&world, 1, -6, -6);
    walk_to(&world, 1, 0, -6);
    aim_at(&world, 1, world.Fold.cube);
    act(&world, 1, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[1] == 1);
    walk_to(&world, 1, -6, -6);
    walk_to(&world, 1, -14, -8);
    cross_fold(&world, 1, (tide_float3){-16, 1.3f, -8}, 160);
    CHECK(pilot(&world, 1).position.x > 9 && world.Fold.holding.at[1] == 1);
    walk_to(&world, 1, 14, 9.8f);
    aim_at(&world, 1, (tide_float3){14, 0, 11});
    ticks(&world, 40);
    act(&world, 1, (PilotInput){.carry = true});
    ticks(&world, 70);
    if (!(world.Arena.signals & 4))
        fprintf(stderr, "Exchange exit cube %.3f %.3f %.3f scale %.3f, operator %.3f %.3f %.3f\n", world.Fold.cube.x, world.Fold.cube.y, world.Fold.cube.z,
                world.Fold.cubeScale, pilot(&world, 1).position.x, pilot(&world, 1).position.y, pilot(&world, 1).position.z);
    CHECK((world.Arena.signals & 4) != 0 && world.Arena.gate > .99f);
    // A lets the door down and follows.
    walk_to(&world, 0, -12, -6);
    CHECK((world.Arena.signals & 1) == 0);
    walk_to(&world, 0, -14, -8);
    cross_fold(&world, 0, (tide_float3){-16, 1.3f, -8}, 160);
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
