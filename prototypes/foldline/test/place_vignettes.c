static Beast vignette_beast(const tide_world *world)
{
    for (uint32_t index = 0; index < world->entities.next_unused; index++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, index);
        if (!tide_has_Beast(world, entity)) continue;
        const Beast beast = tide_get_Beast(world, entity);
        if (beast.chamber == world->Arena.chamber) return beast;
    }
    CHECK(false);
    return (Beast){0};
}

static Mover vignette_bolt(const tide_world *world)
{
    for (uint32_t index = 0; index < world->entities.next_unused; index++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, index);
        if (!tide_has_Mover(world, entity)) continue;
        const Mover mover = tide_get_Mover(world, entity);
        if (mover.chamber == world->Arena.chamber) return mover;
    }
    CHECK(false);
    return (Mover){0};
}

// These are all walked from their arrival. Standing outside the lift keeps a
// vignette open; returning after the beat lets its ordinary door finish it.
void place_vignettes_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, LAMPS_OUT);
    CHECK(world.Place.dead && world.Place.kind == PlaceKind_Vignette);
    walk_to(&world, 0, 0, 3);
    aim_at(&world, 0, (tide_float3){0, 1.6f, 24});
    aim_at(&world, 1, (tide_float3){0, 1.6f, 24});
    // Watched, it stands while there's light; each lamp that fails takes every light for a moment, and it comes on
    bool dark = false;
    for (int tick = 0; tick < 900 && world.Arena.arrive < 10; tick++) {
        const float before = vignette_beast(&world).at.z;
        tide_world_tick(&world);
        dark = dark || world.Arena.blackout;
        if (!world.Arena.blackout) CHECK(near(vignette_beast(&world).at.z, before, .001f));
        CHECK(world.Arena.caught == 0);
    }
    CHECK(dark && world.Arena.darkGroups != 0 && near(vignette_beast(&world).at.z, 6.1f, .01f));
    ticks(&world, 120);
    CHECK(world.Place.id == LAMPS_OUT && world.Arena.caught == 0 && vignette_beast(&world).at.z >= 6.09f);
    act(&world, 0, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open);
    const int lamps = world.Arena.chamber;
    walk_to(&world, 0, 0, -.5f);
    ticks(&world, 140);
    CHECK(world.Arena.chamber == lamps + 1);

    // The bolt hangs under its pipe, drops through the silk, and the spider wraps it; then it's at the bars.
    choose(&world, 0, THE_SLOT);
    CHECK(near(vignette_bolt(&world).at.y, 2.23f, .02f));
    walk_to(&world, 0, 0, 3);
    for (int tick = 0; tick < 600 && world.Arena.arrive < 6.5f; tick++) tide_world_tick(&world);
    CHECK(world.Place.id == THE_SLOT && world.Arena.caught == 0);
    CHECK(near(vignette_bolt(&world).at.y, .075f, .02f));
    CHECK(vignette_beast(&world).kind == BeastKind_Spider);
    CHECK(near(vignette_beast(&world).at.x, 0, .04f) && near(vignette_beast(&world).at.z, 7.4f, .04f));
    for (int tick = 0; tick < 600 && world.Arena.arrive < 7.9f; tick++) tide_world_tick(&world);
    CHECK(near(vignette_beast(&world).at.z, 4.75f, .05f));
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 60);
    input(&world, 0, (PilotInput){0});
    CHECK(pilot(&world, 0).position.z < 3.7f && world.Arena.caught == 0);
    const int slot = world.Arena.chamber;
    walk_to(&world, 0, 0, -.5f);
    ticks(&world, 220);
    CHECK(world.Arena.chamber == slot + 1);

    // The pan slides off the middle bed; out from behind the last curtain, it comes round to it. After, a step on
    // the grate before the lift brings it at the bars.
    choose(&world, 0, THE_WARD);
    CHECK(near(vignette_bolt(&world).at.y, .645f, .02f));
    walk_to(&world, 0, 1.4f, 0);
    for (int tick = 0; tick < 900 && world.Arena.arrive < 7.7f; tick++) tide_world_tick(&world);
    CHECK(world.Place.id == THE_WARD && world.Arena.caught == 0);
    CHECK(near(vignette_bolt(&world).at.y, .06f, .02f) && near(vignette_bolt(&world).at.x, 11.8f, .02f));
    CHECK(near(vignette_beast(&world).at.x, 12.1f, .05f) && near(vignette_beast(&world).at.z, .45f, .05f));
    walk_to(&world, 0, 3, 0);
    ticks(&world, 120);
    CHECK(vignette_beast(&world).at.x < 5 && world.Arena.caught == 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 60);
    input(&world, 0, (PilotInput){0});
    CHECK(pilot(&world, 0).position.x < 3.7f && world.Arena.caught == 0);
    const int ward = world.Arena.chamber;
    walk_to(&world, 0, -.5f, 0);
    ticks(&world, 220);
    CHECK(world.Arena.chamber == ward + 1);
    tide_world_free(&world);
}
