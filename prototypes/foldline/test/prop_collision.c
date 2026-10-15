// Player-input regressions for the furniture the owner could walk through.
// The structural checks also preserve real gaps instead of replacing a chair
// or desk with an invisible enclosing box.
static tide_entity prop_fixture(const tide_world *world, PropKind kind, float x, float z)
{
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Prop(world, entity)) continue;
        const Prop prop = tide_get_Prop(world, entity);
        if (prop.chamber == world->Arena.chamber && prop.kind == kind &&
            near(prop.at.x, x, .01f) && near(prop.at.z, z, .01f)) return entity;
    }
    CHECK(false);
    return (tide_entity){0};
}

static bool prop_contains_local(const tide_world *world, tide_entity entity, tide_float3 local)
{
    const Prop prop = tide_get_Prop(world, entity);
    const tide_float3 point = {local.x * prop.scale.x, local.y * prop.scale.y, local.z * prop.scale.z};
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity shape = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Box3D_Part(world, shape) || !tide_has_Box3D_Box(world, shape)) continue;
        const Box3D_Part part = tide_get_Box3D_Part(world, shape);
        if (!tide_entity_equal(part.of, entity) || part.box == 0) continue;
        const Box3D_Box box = tide_get_Box3D_Box(world, shape);
        if (!box.sensor && fabsf(point.x - box.offset.x) <= box.size.x * .5f &&
            fabsf(point.y - box.offset.y) <= box.size.y * .5f &&
            fabsf(point.z - box.offset.z) <= box.size.z * .5f) return true;
    }
    return false;
}

static void prop_drive_into(tide_world *world, float x, float z)
{
    const Operator before = pilot(world, 0);
    aim_at(world, 0, (tide_float3){x, before.position.y + 1.55f * before.scale, z});
    for (int tick = 0; tick < 120; tick++) {
        const Operator op = pilot(world, 0);
        const float dx = x - op.position.x, dz = z - op.position.z;
        const float gap = sqrtf(dx * dx + dz * dz);
        CHECK(gap > .01f);
        const float sn = tide_sin_f(op.yaw), cs = tide_cos_f(op.yaw);
        input(world, 0, (PilotInput){.move = {(dx * cs - dz * sn) / gap, (dx * sn + dz * cs) / gap}});
        tide_world_tick(world);
    }
    input(world, 0, (PilotInput){0});
    ticks(world, 12);
    CHECK(world->Arena.caught == 0 && near(pilot(world, 0).position.y, before.position.y, .12f));
}

static void prop_collision_tests(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, RECEPTION);
    ticks(&world, 100);
    const tide_entity chair = prop_fixture(&world, PropKind_PlasticChair, -10.7f, 1);
    const tide_entity cabinet = prop_fixture(&world, PropKind_Cabinet, -11.65f, 2.8f);
    const tide_entity desk = prop_fixture(&world, PropKind_Desk, -4, .8f);
    CHECK(tide_has_Box3D_Body(&world, chair) && tide_has_Box3D_Body(&world, cabinet));
    CHECK(prop_contains_local(&world, chair, (tide_float3){0, .48f, 0}));
    CHECK(prop_contains_local(&world, chair, (tide_float3){0, .77f, -.22f}));
    CHECK(!prop_contains_local(&world, chair, (tide_float3){0, .2f, 0}));
    CHECK(prop_contains_local(&world, desk, (tide_float3){0, .76f, 0}));
    CHECK(!prop_contains_local(&world, desk, (tide_float3){0, .35f, 0}));

    walk_to(&world, 0, -8, -1);
    walk_to(&world, 0, -9, 1);
    prop_drive_into(&world, -11.8f, 1);
    if (!(pilot(&world, 0).position.x > -10.3f && pilot(&world, 0).position.x < -10.0f))
        fprintf(stderr, "chair contact actual %.4f %.4f %.4f\n", pilot(&world, 0).position.x, pilot(&world, 0).position.y, pilot(&world, 0).position.z);
    CHECK(pilot(&world, 0).position.x > -10.3f && pilot(&world, 0).position.x < -10.0f);
    CHECK(near(pilot(&world, 0).position.z, 1, .1f));

    // The gap between the adjacent chairs is genuinely open. Walk through it
    // until the closed cabinet itself stops the body before the rear wall.
    walk_to(&world, 0, -9, 1);
    walk_to(&world, 0, -9, 2.8f);
    prop_drive_into(&world, -12, 2.8f);
    CHECK(pilot(&world, 0).position.x > -11.2f && pilot(&world, 0).position.x < -10.9f);
    CHECK(near(pilot(&world, 0).position.z, 2.8f, .1f));
    const Portal old = world.Fold.ends.at[0];
    aim_at(&world, 0, (tide_float3){-12, .4f, 2.8f});
    act(&world, 0, (PilotInput){.amber = true});
    CHECK(memcmp(&old, &world.Fold.ends.at[0], sizeof old) == 0 && world.Fold.fizzleTime > 0);

    walk_to(&world, 0, -9, 2.8f);
    walk_to(&world, 0, -9, 5.75f);
    // The ray passes above and beside the last cabinet and reaches receptive
    // wallpaper. Floor nudging brings the full mouth down beside its body,
    // where the edge overlaps the cabinet. Checking the ray alone accepts it.
    aim_at(&world, 0, (tide_float3){-12, 2, 5.75f});
    const preview_pair cabinet_edge = read_preview(&world, 0);
    CHECK(cabinet_edge.first.hit && !cabinet_edge.first.valid);
    CHECK(near(cabinet_edge.first.point.x, -12, .002f));
    CHECK(near(cabinet_edge.first.point.y, 1.3f, .002f));
    CHECK(near(cabinet_edge.first.point.z, 5.75f, .002f));
    fire_preview(&world, 0, 0, cabinet_edge.first);
    // A nearby complete mouth clears the furniture and still places normally.
    aim_at(&world, 0, (tide_float3){-12, 2, 6.3f});
    const preview_pair clear_edge = read_preview(&world, 0);
    CHECK(clear_edge.first.hit && clear_edge.first.valid);
    CHECK(near(clear_edge.first.portal.at.x, -12, .002f));
    CHECK(near(clear_edge.first.portal.at.y, 1.3f, .002f));
    CHECK(near(clear_edge.first.portal.at.z, 6.3f, .002f));
    fire_preview(&world, 0, 0, clear_edge.first);

    walk_to(&world, 0, -9, 2.8f);
    walk_to(&world, 0, -8, -.5f);
    walk_to(&world, 0, -6.5f, .8f);
    prop_drive_into(&world, -4, .8f);
    CHECK(pilot(&world, 0).position.x > -5.25f && pilot(&world, 0).position.x < -4.95f);

    choose(&world, 0, MAILROOM);
    ticks(&world, 100);
    const tide_entity shelf = prop_fixture(&world, PropKind_Shelf, 2.9f, 2);
    CHECK(prop_contains_local(&world, shelf, (tide_float3){0, .57f, 0}));
    CHECK(!prop_contains_local(&world, shelf, (tide_float3){0, .34f, 0}));
    walk_to(&world, 0, -8, -4);
    walk_to(&world, 0, 1, -4);
    walk_to(&world, 0, 1, 2);
    prop_drive_into(&world, 4, 2);
    CHECK(pilot(&world, 0).position.x > 2.2f && pilot(&world, 0).position.x < 2.55f);

    // Room replacement retires every compound with its actual owning prop.
    for (uint32_t slot = 0; slot < world.entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world.entities, slot);
        if (!tide_has_Box3D_Part(&world, entity)) continue;
        const Box3D_Part part = tide_get_Box3D_Part(&world, entity);
        CHECK(tide_has_Prop(&world, part.of) && tide_get_Prop(&world, part.of).chamber == world.Arena.chamber);
        CHECK(tide_has_Box3D_Body(&world, part.of) && part.box != 0);
    }
    tide_world_free(&world);
}
