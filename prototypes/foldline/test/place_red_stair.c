static Mover red_deck(const tide_world *world, float high)
{
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (tide_has_Mover(world, entity)) {
            const Mover mover = tide_get_Mover(world, entity);
            if (mover.chamber == world->Arena.chamber && near(mover.from.y + .15f, high, .01f)) return mover;
        }
    }
    CHECK(false);
    return (Mover){0};
}

static void red_ride(tide_world *world, int id, float high, float x, float from, float to)
{
    for (int tick = 0; tick < 1000; tick++) {
        const Mover deck = red_deck(world, high);
        if (near(deck.at.z, from, .04f) && fabsf(deck.velocity.z) < .01f) break;
        tide_world_tick(world);
        CHECK(tick < 999);
    }
    walk_to(world, id, x, from);
    CHECK(near(pilot(world, id).position.y, high, .05f));
    for (int tick = 0; tick < 1000; tick++) {
        const Mover deck = red_deck(world, high);
        if (near(deck.at.z, to, .04f)) break;
        tide_world_tick(world);
        CHECK(tick < 999);
    }
    walk_to(world, id, x, to + (to > from ? 1.5f : -1.5f));
    CHECK(near(pilot(world, id).position.y, high, .05f));
}

static void red_route(tide_world *world, int id, float lift_x)
{
    walk_to(world, id, -10.5f, -7);
    walk_to(world, id, -10.5f, -2.5f);
    red_ride(world, id, 6, -10.5f, 0, 3);
    walk_to(world, id, -10.5f, 10);
    walk_to(world, id, 10.5f, 10);
    walk_to(world, id, 10.5f, 4.5f);
    red_ride(world, id, 3, 10.5f, 1.5f, -1.5f);
    walk_to(world, id, 10.5f, -10);
    walk_to(world, id, 8.6f, -10);
    walk_to(world, id, 3.3f, -10);
    CHECK(near(pilot(world, id).position.y, 0, .05f));
    walk_to(world, id, 3.3f, -12.8f);
    walk_to(world, id, lift_x, -12.8f);
    walk_to(world, id, lift_x, -16.1f);
}

static tide_entity red_hunter(const tide_world *world)
{
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (tide_has_Descent(world, entity) && tide_get_Beast(world, entity).chamber == world->Arena.chamber) return entity;
    }
    CHECK(false);
    return (tide_entity){0};
}

// What fell into the well comes back out behind them once both are over the first gap. Watched, it stands; with
// both looking away it has them, and the stair starts over for both.
static void red_hunter_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, RED_STAIR);
    ticks(&world, 100);
    for (int id = 0; id < 2; id++) {
        walk_to(&world, id, -10.5f + id, -7);
        walk_to(&world, id, -10.5f + id, -2.5f);
        red_ride(&world, id, 6, -10.5f, 0, 3);
        walk_to(&world, id, -11.5f + id * 2, 10.5f + id);
    }
    CHECK(tide_get_Descent(&world, red_hunter(&world)).state != 0);
    // Up out of the blood behind them, under the first gap and out on their side
    bool under = false;
    for (int tick = 0; tick < 1200; tick++) {
        tide_world_tick(&world);
        const Descent now = tide_get_Descent(&world, red_hunter(&world));
        under = under || now.state == 3;
        if (now.state == 2 && tide_get_Beast(&world, red_hunter(&world)).at.z > 4) break;
        CHECK(world.Arena.caught == 0);
    }
    CHECK(under);
    const Descent out = tide_get_Descent(&world, red_hunter(&world));
    CHECK(out.state == 2 && tide_get_Beast(&world, red_hunter(&world)).at.z > 4);
    // A watches it: it stands, but while every lamp is out
    const int chamber = world.Arena.chamber;
    aim_at(&world, 0, (tide_float3){tide_get_Beast(&world, red_hunter(&world)).at.x, 7.1f, tide_get_Beast(&world, red_hunter(&world)).at.z});
    float along = tide_get_Descent(&world, red_hunter(&world)).along;
    int frozen = 0, lit = 0;
    // ...until the lamps begin to stutter: in the dark it comes on, seen or not
    for (int tick = 0; tick < 300 && world.Arena.warning == 0; tick++) {
        tide_world_tick(&world);
        const Descent now = tide_get_Descent(&world, red_hunter(&world));
        if (!world.Arena.blackout) {
            lit++;
            if (fabsf(now.along - along) < .0001f) frozen++;
        }
        along = now.along;
        CHECK(world.Arena.chamber == chamber && world.Arena.caught == 0);
    }
    CHECK(lit > 60 && frozen == lit && tide_get_Descent(&world, red_hunter(&world)).seen);
    // Both look away, up the way they came: it has one of them, and the stair is built again
    aim_at(&world, 0, (tide_float3){-10.5f, 7.5f, 14});
    aim_at(&world, 1, (tide_float3){-10.5f, 7.5f, 14});
    for (int tick = 0; tick < 1200 && world.Arena.chamber == chamber; tick++) tide_world_tick(&world);
    CHECK(world.Arena.chamber == chamber + 1 && world.Place.id == RED_STAIR);
    ticks(&world, 100);
    CHECK(near(pilot(&world, 0).position.y, 8, .05f) && tide_get_Descent(&world, red_hunter(&world)).state == 0);
    tide_world_free(&world);
}

static void place_red_stair_tests(void)
{
    red_hunter_tests();
    tide_world world = {0};
    start(&world);
    choose(&world, 0, RED_STAIR);
    ticks(&world, 100);
    CHECK(world.Place.dead && near(pilot(&world, 0).position.y, 8, .04f));
    red_route(&world, 0, 4.8f);
    red_route(&world, 1, 7.2f);
    const int chamber = world.Arena.chamber;
    ticks(&world, 180);
    CHECK(world.Arena.chamber > chamber && world.Place.id == ATRIUM);
    tide_world_free(&world);
}

static void place_ending_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, RECEPTION_AGAIN);
    ticks(&world, 100);
    CHECK(world.Place.kind == PlaceKind_Ending && world.Place.dead && (world.Arena.signals & 2) != 0);
    const int chamber = world.Arena.chamber;
    ticks(&world, 900);
    CHECK(world.Arena.chamber == chamber && world.Arena.ride == 0 && (world.Arena.darkGroups & 2) != 0);
    act(&world, 0, (PilotInput){.reset = true});
    ticks(&world, 3);
    CHECK(world.Place.id == RECEPTION_AGAIN && world.Arena.chamber == chamber + 1);
    tide_world_free(&world);
}
