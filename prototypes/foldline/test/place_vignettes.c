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
    const Beast watched = vignette_beast(&world);
    ticks(&world, 90);
    CHECK(near(vignette_beast(&world).at.z, watched.at.z, .001f));
    CHECK(world.Arena.darkGroups != 0 && world.Arena.caught == 0);
    aim_at(&world, 0, (tide_float3){0, 1, -10});
    aim_at(&world, 1, (tide_float3){0, 1, -10});
    ticks(&world, 90);
    CHECK(vignette_beast(&world).at.z < watched.at.z - 1);
    ticks(&world, 500);
    CHECK(world.Place.id == LAMPS_OUT && world.Arena.caught == 0 && vignette_beast(&world).at.z >= 4.8f);
    act(&world, 0, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open);
    const int lamps = world.Arena.chamber;
    walk_to(&world, 0, 0, -.5f);
    ticks(&world, 140);
    CHECK(world.Arena.chamber == lamps + 1);

    choose(&world, 0, THE_SLOT);
    CHECK(near(vignette_bolt(&world).at.y, 2.2f, .02f));
    walk_to(&world, 0, 0, 3);
    ticks(&world, 400);
    CHECK(world.Place.id == THE_SLOT && world.Arena.caught == 0);
    CHECK(near(vignette_bolt(&world).at.y, .1f, .02f));
    CHECK(vignette_beast(&world).kind == BeastKind_Spider);
    CHECK(near(vignette_beast(&world).at.x, 0, .04f) && near(vignette_beast(&world).at.z, 7, .04f));
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 60);
    input(&world, 0, (PilotInput){0});
    CHECK(pilot(&world, 0).position.z < 3.7f && world.Arena.caught == 0);
    const int slot = world.Arena.chamber;
    walk_to(&world, 0, 0, -.5f);
    ticks(&world, 220);
    CHECK(world.Arena.chamber == slot + 1);

    choose(&world, 0, THE_WARD);
    CHECK(near(vignette_bolt(&world).at.y, .8f, .02f));
    walk_to(&world, 0, 3, 0);
    ticks(&world, 420);
    CHECK(world.Place.id == THE_WARD && world.Arena.caught == 0);
    CHECK(near(vignette_bolt(&world).at.y, .08f, .02f));
    CHECK(near(vignette_beast(&world).at.x, 12, .04f) && near(vignette_beast(&world).at.z, 0, .04f));
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
