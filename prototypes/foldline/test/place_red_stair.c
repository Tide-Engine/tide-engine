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
    red_ride(world, id, 6, -10.5f, -1.5f, 4.5f);
    walk_to(world, id, -10.5f, 10);
    walk_to(world, id, 10.5f, 10);
    walk_to(world, id, 10.5f, 4.5f);
    red_ride(world, id, 3, 10.5f, 3, -3);
    walk_to(world, id, 10.5f, -7);
    walk_to(world, id, 9.1f, -7.8f);
    walk_to(world, id, 8.6f, -8.5f);
    walk_to(world, id, 2.7f, -10);
    walk_to(world, id, 2.7f, -12.5f);
    walk_to(world, id, lift_x, -12.5f);
    walk_to(world, id, lift_x, 14);
    walk_to(world, id, lift_x, 17.1f);
}

static void place_red_stair_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, RED_STAIR);
    ticks(&world, 100);
    CHECK(world.Place.dead && near(pilot(&world, 0).position.y, 8, .04f));
    red_route(&world, 0, 9.3f);
    red_route(&world, 1, 11.7f);
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
