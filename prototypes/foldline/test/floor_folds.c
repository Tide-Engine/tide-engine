// Ends on floors and ceilings: a fall becomes a throw, a walk becomes a launch, and what drops through a
// floor's end can come down out of a ceiling's.

// The bench's box of the room that a point is inside of.
static tide_entity wall_holding(const tide_world *world, const tide_float3 at)
{
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Wall(world, entity)) continue;
        const Wall wall = tide_get_Wall(world, entity);
        if (wall.chamber != world->Arena.chamber) continue;
        if (fabsf(at.x - wall.center.x) <= wall.size.x * .5f && fabsf(at.y - wall.center.y) <= wall.size.y * .5f
            && fabsf(at.z - wall.center.z) <= wall.size.z * .5f) return entity;
    }
    CHECK(false);
    return (tide_entity){0};
}

static Portal end_on(const tide_world *world, const tide_float3 at, const tide_float3 face, const tide_float3 up)
{
    // The box is found a little behind the face, where it is solid
    const tide_float3 inside = {at.x - face.x * .1f, at.y - face.y * .1f, at.z - face.z * .1f};
    return (Portal){.open = true, .at = at, .face = face, .up = up, .wall = wall_holding(world, inside), .size = 1};
}

// Drops operator A from feet `from` until it crosses, and gives what it was as it came out.
static Operator drop_through(tide_world *world, const tide_float3 from, const float yaw, const float pitch, const int most)
{
    const int before = world->Fold.traversals;
    place(world, 0, from, yaw, pitch);
    input(world, 0, (PilotInput){0});
    for (int step = 0; step < most && world->Fold.traversals == before; step++) tide_world_tick(world);
    if (world->Fold.traversals == before) {
        const Operator o = pilot(world, 0);
        fprintf(stderr, "drop_through from %.2f %.2f %.2f: still at %.2f %.2f %.2f\n", from.x, from.y, from.z, o.position.x, o.position.y, o.position.z);
    }
    CHECK(world->Fold.traversals > before);
    return pilot(world, 0);
}

static void floor_folds(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 8);
    // A's amber on the floor, its length along +z, and cyan on the west wall, out of the way of the throw
    world.Fold.ends.at[0] = end_on(&world, (tide_float3){-5, 0, 9}, (tide_float3){0, 1, 0}, (tide_float3){0, 0, 1});
    world.Fold.ends.at[1] = end_on(&world, (tide_float3){-10, 1.3f, 5}, (tide_float3){1, 0, 0}, (tide_float3){0, 1, 0});
    ticks(&world, 2);
    // Dropped onto it looking down, it falls through the floor and is thrown out of the wall, looking out of it
    const Operator thrown = drop_through(&world, (tide_float3){-5, 2.5f, 9}, NORTH, -1.2f, 120);
    CHECK(thrown.position.x > -10 && thrown.position.x < -9.4f && near(thrown.position.z, 5, .45f));
    CHECK(thrown.position.y > 0.2f && thrown.position.y < 1.0f);
    CHECK(thrown.velocity.x > 6.5f && fabsf(thrown.velocity.y) < 1 && fabsf(thrown.velocity.z) < .5f);
    CHECK(near(thrown.yaw, EAST, .01f) && near(thrown.pitch, .37f, .02f));
    CHECK(thrown.flung > 0);
    // ...and lands out in the room, having kept that speed in the air: out low and level, it flies 2.5 m
    ticks(&world, 90);
    CHECK(pilot(&world, 0).position.x > -7.8f && pilot(&world, 0).grounded && world.Fold.traversals == 1);
    // Walked into the wall's end, it comes up out of the floor's, going up
    place(&world, 0, (tide_float3){-8, 0, 5}, WEST, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    const int walked = world.Fold.traversals;
    for (int step = 0; step < 120 && world.Fold.traversals == walked; step++) tide_world_tick(&world);
    input(&world, 0, (PilotInput){0});
    CHECK(world.Fold.traversals == walked + 1);
    const Operator launched = pilot(&world, 0);
    CHECK(launched.velocity.y > 3 && near(launched.position.x, -5, .45f) && near(launched.position.z, 9, 1.1f));
    CHECK(launched.position.y > -1 && launched.position.y < .5f);
    // The cube dropped on it comes out of the wall too
    world.Fold.ends.at[1] = end_on(&world, (tide_float3){-10, 1.3f, 5}, (tide_float3){1, 0, 0}, (tide_float3){0, 1, 0});
    place(&world, 0, (tide_float3){3, 0, 9}, 0, 0);
    ticks(&world, 30);
    const int cubed = world.Fold.traversals;
    put_cube(&world, (tide_float3){-5, 1.5f, 9.2f}, (tide_float3){0});
    for (int step = 0; step < 120 && world.Fold.traversals == cubed; step++) tide_world_tick(&world);
    CHECK(world.Fold.traversals > cubed && world.Fold.cube.x > -10.1f);
    ticks(&world, 2);
    CHECK(tide_get_Box3D_Body(&world, world.Arena.cube).velocity.x > 2);

    // A floor's end and a ceiling's: what falls into the one comes down out of the other
    // (Ends set by hand: a tick with none, so the floor's hole closes before another is cut)
    world.Fold.ends.at[0] = (Portal){0};
    world.Fold.ends.at[1] = (Portal){0};
    ticks(&world, 2);
    world.Fold.ends.at[2] = end_on(&world, (tide_float3){3, 0, 7}, (tide_float3){0, 1, 0}, (tide_float3){0, 0, 1});
    world.Fold.ends.at[3] = end_on(&world, (tide_float3){3, 4.4f, 0}, (tide_float3){0, -1, 0}, (tide_float3){0, 0, 1});
    ticks(&world, 2);
    const Operator dropped = drop_through(&world, (tide_float3){3, 1.5f, 7}, NORTH, 0, 120);
    CHECK(dropped.velocity.y < -3 && dropped.position.y > 2 && dropped.position.y < 4.4f && near(dropped.position.z, 0, .6f));
    ticks(&world, 60);
    CHECK(pilot(&world, 0).grounded && near(pilot(&world, 0).position.y, 0, .05f));
    world.Fold.ends.at[2] = (Portal){0};
    world.Fold.ends.at[3] = (Portal){0};
    ticks(&world, 2);
    // A floor's end right under a ceiling's: a fall that never ends, and comes out no faster than FASTEST, 24 m/s,
    // so it's never faster than that and the 4.4 m it falls from the ceiling's end to the floor's
    world.Fold.ends.at[2] = end_on(&world, (tide_float3){3, 0, 7}, (tide_float3){0, 1, 0}, (tide_float3){0, 0, 1});
    world.Fold.ends.at[3] = end_on(&world, (tide_float3){3, 4.4f, 7}, (tide_float3){0, -1, 0}, (tide_float3){0, 0, 1});
    ticks(&world, 2);
    const int falls = world.Fold.traversals;
    place(&world, 0, (tide_float3){3, 1, 7}, NORTH, 0);
    float fastest = 0;
    for (int step = 0; step < 600; step++) {
        tide_world_tick(&world);
        const tide_float3 v = pilot(&world, 0).velocity;
        fastest = fmaxf(fastest, sqrtf(v.x * v.x + v.y * v.y + v.z * v.z));
    }
    if (!(world.Fold.traversals > falls + 10 && fastest < 27.5f && fastest > 24))
        fprintf(stderr, "endless fall: %d crossings, fastest %.2f, at %.2f %.2f %.2f\n", world.Fold.traversals - falls, fastest,
                pilot(&world, 0).position.x, pilot(&world, 0).position.y, pilot(&world, 0).position.z);
    CHECK(world.Fold.traversals > falls + 10 && fastest < 27.5f && fastest > 24);
    world.Fold.ends.at[2] = (Portal){0};
    world.Fold.ends.at[3] = (Portal){0};
    ticks(&world, 2);
    place(&world, 0, (tide_float3){3, 0, 9}, 0, 0);
    ticks(&world, 30);

    // Shots: a floor that takes a fold takes one shot down at it, its length the way the shot went, before the
    // shooter; a ceiling that takes one, one shot up
    const tide_entity floor = wall_holding(&world, (tide_float3){-5, -.5f, 5});
    Wall papered = tide_get_Wall(&world, floor);
    papered.takes = true;
    CHECK(tide_set_Wall(&world, floor, papered));
    place(&world, 0, (tide_float3){-5, 0, 3}, NORTH, -1.0f);
    ticks(&world, 4);
    press(&world, (PilotInput){.amber = true});
    const Portal down = world.Fold.ends.at[0];
    CHECK(down.open && down.face.y > .99f && down.up.z > .99f && near(down.at.y, 0, .001f));
    CHECK(down.at.z > 3.2f && down.at.z < 6 && near(down.at.x, -5, .3f));
    place(&world, 0, (tide_float3){-5, 0, 3}, EAST, -1.0f);
    ticks(&world, 4);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].open && world.Fold.ends.at[0].up.x > .99f && world.Fold.ends.at[0].at.x > -4.8f);
    const tide_entity ceiling = wall_holding(&world, (tide_float3){0, 4.9f, 0});
    Wall over = tide_get_Wall(&world, ceiling);
    over.takes = true;
    CHECK(tide_set_Wall(&world, ceiling, over));
    place(&world, 0, (tide_float3){0, 0, -4}, NORTH, 1.2f);
    ticks(&world, 4);
    press(&world, (PilotInput){.cyan = true});
    const Portal up = world.Fold.ends.at[1];
    CHECK(up.open && up.face.y < -.99f && up.up.z > .99f && near(up.at.y, 4.4f, .001f));
    // The two are a fold: the floor's end, walked onto, drops its walker out of the ceiling's
    const Operator fallen = drop_through(&world, (tide_float3){world.Fold.ends.at[0].at.x, .5f, world.Fold.ends.at[0].at.z}, NORTH, 0, 120);
    CHECK(fallen.velocity.y < 0 && fallen.position.y > 2);
    tide_world_free(&world);
}
