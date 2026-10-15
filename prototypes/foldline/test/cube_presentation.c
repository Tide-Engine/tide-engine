// These fixtures tick real portal crossings, then inspect the actual cargo
// geometry recorded by Render with both interpolation endpoints supplied.
static tide_entity presentation_crate(const tide_world *world)
{
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (tide_has_Cube(world, entity)) {
            const Cube cube = tide_get_Cube(world, entity);
            if (cube.chamber == world->Arena.chamber && cube.index == 1) return entity;
        }
    }
    CHECK(false);
    return (tide_entity){0};
}

static void presentation_move(tide_world *world, tide_entity entity, tide_float3 at, tide_float3 velocity)
{
    Cube cube = tide_get_Cube(world, entity);
    Box3D_Body body = tide_get_Box3D_Body(world, entity);
    cube.last = at;
    cube.at = at;
    cube.turn = body.rotation = tide_identity_q();
    body.position = at;
    body.velocity = velocity;
    body.angularVelocity = (tide_float3){0, 1.5f, 0};
    CHECK(tide_set_Cube(world, entity, cube));
    CHECK(tide_set_Box3D_Body(world, entity, body));
}

static Fixture presented_cube(tide_world *world, const tide_world *previous, float alpha, int kind)
{
    extern const unsigned char tide_game_pack[];
    extern const size_t tide_game_pack_size;
    static bool loaded;
    if (!loaded) {
        CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
        loaded = true;
    }
    const uint64_t unchanged = tide_world_hash(world);
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    const tide_devices devices = {0};
    const tide_float2 screen = {960, 540};
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, player(0), 0, true, false, "");
    local.Interface.open = false;
    local.Interface.introduced = true;
    local.Control.started = true;
    local.Optics.warmed = 3;
    tide_draw_screen_size(&draw, screen);
    tide_gui_begin(&gui, &devices, screen, NULL);
    tide_frame(world, previous, alpha, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);

    // The cube's pale central box uniquely identifies its full transform.
    // For the crate fixture, the other crate stays near x=1 throughout; the
    // tested one crosses between the two negative-x portal positions.
    const tide_list lists[] = {local.Optics.cubeRest, local.Optics.cubeParts};
    Fixture found = {0};
    int matches = 0;
    for (int list = 0; list < 2; list++) {
        const int count = tide_list_read_count(&local.heap, lists[list]);
        for (int index = 0; index < count; index++) {
            const Fixture *part = tide_list_read(&local.heap, lists[list], index, sizeof *part);
            CHECK(part != NULL);
            const bool hull = kind == 0
                ? near(part->tint.r, .86f, .001f) && near(part->tint.g, .86f, .001f) && near(part->tint.b, .8f, .001f)
                : part->cell >= 0 && part->transform.c3.x < -4;
            if (hull) { found = *part; matches++; }
        }
    }
    CHECK(matches == 1);
    CHECK(tide_world_hash(world) == unchanged);
    tide_draw_forget(&draw);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_render_targets_free();
    return found;
}

static float presentation_distance(tide_float4 a, tide_float4 b)
{
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return sqrtf(x * x + y * y + z * z);
}

static void check_presentation(tide_world *world, const tide_world *previous, Cube current, Cube old, bool snapped, const char *label)
{
    static const float fractions[] = {.1f, .5f, .9f, 1};
    for (int index = 0; index < 4; index++) {
        const float alpha = fractions[index];
        const Fixture actual = presented_cube(world, previous, alpha, current.kind);
        const float fraction = snapped ? 1 : alpha;
        const tide_float3 at = tide_lerp_f3(old.at, current.at, (tide_float3){fraction, fraction, fraction});
        const tide_quaternion turn = tide_nlerp_q(old.turn, current.turn, fraction);
        const float scale = old.scale + (current.scale - old.scale) * fraction;
        const tide_float4x4 expected = tide_trs_f4x4(at, turn, (tide_float3){.74f * scale, .74f * scale, .74f * scale});
        const float distance = presentation_distance(actual.transform.c3, expected.c3);
        if (distance > .001f)
            fprintf(stderr, "%s alpha %.1f: drawn (%.3f %.3f %.3f), expected (%.3f %.3f %.3f), gap %.3f\n",
                    label, alpha, actual.transform.c3.x, actual.transform.c3.y, actual.transform.c3.z,
                    expected.c3.x, expected.c3.y, expected.c3.z, distance);
        CHECK(distance < .001f);
        CHECK(presentation_distance(actual.transform.c0, expected.c0) < .001f);
        CHECK(presentation_distance(actual.transform.c1, expected.c1) < .001f);
        CHECK(presentation_distance(actual.transform.c2, expected.c2) < .001f);
    }
}

static void cube_crossing_presentation(bool held, bool crate, bool resize)
{
    tide_world world = {0}, previous = {0};
    start(&world);
    ticks(&world, 30);
    two_ends(&world, (tide_float3){-5, 0, resize ? -5 : -2.5f}, WEST, 0);
    CHECK(world.Fold.ends.at[1].size == (resize ? .5f : 1));
    const tide_entity entity = crate ? presentation_crate(&world) : world.Arena.cube;
    place(&world, 0, (tide_float3){-5, 0, -7}, SOUTH, 0);
    presentation_move(&world, entity, (tide_float3){-5, held ? .4f : 1.2f, -8.1f}, (tide_float3){0, 0, held ? 0 : -7});
    if (held) {
        tide_world_tick(&world);
        press(&world, (PilotInput){.carry = true});
        CHECK(world.Fold.holding.at[0] == 1);
        ticks(&world, 20);
        input(&world, 0, (PilotInput){.move = {0, 1}});
    }
    const int traversals = world.Fold.traversals;
    for (int tick = 0; tick < 180 && world.Fold.traversals == traversals; tick++) {
        tide_world_copy(&previous, &world);
        tide_world_tick(&world);
    }
    CHECK(world.Fold.traversals == traversals + 1);
    const tide_entity after = resize ? world.Arena.cube : entity;
    CHECK(resize || tide_entity_equal(after, entity));
    const Cube old = tide_get_Cube(&previous, entity), current = tide_get_Cube(&world, after);
    CHECK(current.at.x < -9.5f && current.at.z > -6);
    CHECK(old.at.z < -9.5f && old.at.x > -5.1f);
    if (held) CHECK(world.Fold.holding.at[0] == 1 && current.holder == 0);
    if (resize) CHECK(!tide_entity_equal(after, entity) && current.scale == .5f);
    check_presentation(&world, &previous, current, old, true, held ? "held cube crossing" : crate ? "crate crossing" : resize ? "resized cube crossing" : "loose cube crossing");
    tide_world_free(&previous);
    tide_world_free(&world);
}

static void cube_teleports_without_interpolating(void)
{
    cube_crossing_presentation(false, false, false);
    cube_crossing_presentation(true, false, false);
    cube_crossing_presentation(false, true, false);
    cube_crossing_presentation(false, false, true);
    // Ordinary motion still uses fractional positions and rotations.
    tide_world world = {0}, previous = {0};
    start(&world);
    ticks(&world, 30);
    presentation_move(&world, world.Arena.cube, (tide_float3){-5, 1.5f, -7}, (tide_float3){6, 1, 0});
    tide_world_tick(&world);
    tide_world_copy(&previous, &world);
    tide_world_tick(&world);
    const Cube old = tide_get_Cube(&previous, world.Arena.cube), current = tide_get_Cube(&world, world.Arena.cube);
    CHECK(current.at.x - old.at.x > .08f);
    check_presentation(&world, &previous, current, old, false, "ordinary cube motion");
    tide_world_free(&previous);
    tide_world_free(&world);
}
