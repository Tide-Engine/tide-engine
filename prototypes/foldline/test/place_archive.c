static Beast archive_listener(const tide_world *world)
{
    return tide_get_Beast(world, find_beast(world, BeastKind_Flayed));
}

static void archive_hold_aisle_clear(tide_world *world, int ticks_left)
{
    for (int tick = 0; tick < ticks_left; tick++) {
        tide_world_tick(world);
        CHECK(world->Arena.caught == 0);
    }
}

static void place_archive_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, ARCHIVE);
    ticks(&world, 100);
    CHECK(world.Place.dead && world.Place.kind == PlaceKind_Passage);

    // B waits at the near wall's gap; A takes the bank's parcel.
    walk_to(&world, 1, -8.75f, -4.5f);
    walk_to(&world, 0, -8, -10);
    walk_to(&world, 0, 6, -9.5f);
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    // While it walks the far end of its aisle, the parcel is thrown over the stacks onto the metal by it: the crash
    // holds it there, and both cross at the gap, far from it.
    walk_to(&world, 0, 0, -6);
    for (int tick = 0; tick < 1200 && archive_listener(&world).at.x < 5; tick++) tide_world_tick(&world);
    CHECK(archive_listener(&world).at.x >= 5 && world.Arena.caught == 0);
    aim_at(&world, 0, (tide_float3){8, 13.5f, 0});
    act(&world, 0, (PilotInput){.hurl = true});
    for (int tick = 0; tick < 120 && !tide_get_ArchiveHunt(&world, find_beast(&world, BeastKind_Flayed)).decoy; tick++) tide_world_tick(&world);
    CHECK(tide_get_ArchiveHunt(&world, find_beast(&world, BeastKind_Flayed)).decoy);
    walk_to(&world, 1, -8.75f, -1);
    walk_to(&world, 1, -8.75f, 1);
    walk_to(&world, 0, -8.25f, -4.5f);
    walk_to(&world, 0, -8.25f, -1);
    walk_to(&world, 0, -8.25f, 1.5f);
    CHECK(world.Arena.caught == 0);

    // On the quiet middle bank, along its edge clear of the aisle's middle, the other parcel goes with them east.
    walk_to(&world, 0, -5.6f, 1.2f);
    aim_at(&world, 0, (tide_float3){-4, .4f, 2});
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 2);
    walk_to(&world, 0, -6, 3.6f);
    walk_to(&world, 1, -7, 3.6f);
    // It walks the middle of the bank: pass it on the side away from it.
    for (int tick = 0; tick < 1200; tick++) {
        const Beast listener = archive_listener(&world);
        if (listener.at.x < -2 || listener.at.z < 1) break;
        tide_world_tick(&world);
    }
    walk_to(&world, 0, 8.5f, 3.6f);
    walk_to(&world, 1, 8.5f, 3.2f);
    CHECK(world.Arena.caught == 0);
    // Thrown back up the aisle: it runs to the crash, and both cross the far strip and out at the far gap.
    aim_at(&world, 0, (tide_float3){0, 8.7f, 2.7f});
    act(&world, 0, (PilotInput){.hurl = true});
    for (int tick = 0; tick < 120 && !(tide_get_ArchiveHunt(&world, find_beast(&world, BeastKind_Flayed)).decoy &&
                                       tide_get_ArchiveHunt(&world, find_beast(&world, BeastKind_Flayed)).left > 5); tick++)
        tide_world_tick(&world);
    CHECK(tide_get_ArchiveHunt(&world, find_beast(&world, BeastKind_Flayed)).decoy);
    walk_to(&world, 0, 8.75f, 5);
    walk_to(&world, 0, 8.75f, 8);
    walk_to(&world, 1, 9.3f, 5);
    walk_to(&world, 1, 9.3f, 8);
    archive_hold_aisle_clear(&world, 30);
    walk_to(&world, 0, 6.8f, 9);
    walk_to(&world, 0, 6.8f, 14.1f);
    walk_to(&world, 1, 9.2f, 9);
    walk_to(&world, 1, 9.2f, 14.1f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id != ARCHIVE);
    tide_world_free(&world);

    // Steps on the metal with nothing to draw it off bring it at a run, and its touch rebuilds the passage for both.
    // Behind the bars of the stacks, on the carpet, nobody is reached.
    start(&world);
    choose(&world, 0, ARCHIVE);
    ticks(&world, 100);
    walk_to(&world, 0, -8.75f, -4.5f);
    walk_to(&world, 1, -6, -3.2f);
    walk_to(&world, 0, -8.75f, -1.2f);
    bool caught = false;
    for (int lap = 0; lap < 12 && !caught; lap++) {
        aim_at(&world, 0, (tide_float3){lap % 2 ? -10.5f : -5.5f, 1.5f, -1.2f});
        input(&world, 0, (PilotInput){.move = {0, 1}});
        for (int tick = 0; tick < 50 && !caught; tick++) {
            tide_world_tick(&world);
            caught = world.Arena.caught > 0;
        }
    }
    CHECK(caught);
    input(&world, 0, (PilotInput){0});
    const int before = world.Arena.chamber;
    ticks(&world, 100);
    CHECK(world.Arena.chamber > before && world.Place.id == ARCHIVE);
    tide_world_free(&world);
}
