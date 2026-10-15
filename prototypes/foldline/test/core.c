// Shared mechanics are exercised on the bench.
typedef struct preview_pair { FoldShot first, second; } preview_pair;

// Read the real HUD's result through an ordinary local frame. Nothing is
// submitted to a GPU; hashing around the frame checks that aiming is read-only.
static preview_pair read_preview(tide_world *world, int active)
{
    extern const unsigned char tide_game_pack[];
    extern const size_t tide_game_pack_size;
    static bool loaded;
    if (!loaded) {
        CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
        loaded = true;
    }
    const uint64_t before = tide_world_hash(world);
    tide_local local = {0};
    tide_draw_list draw = {0};
    tide_gui gui = {0};
    const tide_devices devices = {0};
    const tide_float2 screen = {960, 540};
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, player(0), 0, true, false, "");
    local.Interface.open = false;
    local.Interface.introduced = true;
    local.Control.active = active;
    local.Control.started = true;
    local.Optics.warmed = 3;
    tide_draw_screen_size(&draw, screen);
    tide_gui_begin(&gui, &devices, screen, NULL);
    tide_frame(world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
    const preview_pair result = {local.Interface.shot0, local.Interface.shot1};
    CHECK(tide_world_hash(world) == before);
    tide_draw_forget(&draw);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_render_targets_free();
    return result;
}

static bool same_point(tide_float3 a, tide_float3 b)
{
    return near(a.x, b.x, .001f) && near(a.y, b.y, .001f) && near(a.z, b.z, .001f);
}

// The preview is observed before a real trigger tick, rather than calling the
// same helper twice. Assert both the decision and its nudged/fizzle position.
static void fire_preview(tide_world *world, int id, int side, FoldShot shot)
{
    const Portal previous = world->Fold.ends.at[id * 2 + side];
    const Wall wall = shot.valid ? tide_get_Wall(world, shot.portal.wall) : (Wall){0};
    PilotInput controls = {.active = id};
    if (side == 0) controls.amber = true;
    else controls.cyan = true;
    input(world, 0, controls);
    tide_world_tick(world);
    input(world, 0, (PilotInput){.active = id});
    const Portal actual = world->Fold.ends.at[id * 2 + side];
    if (shot.valid) {
        CHECK(actual.open && tide_entity_equal(actual.wall, shot.portal.wall));
        CHECK(same_point(actual.at, shot.portal.at) && same_point(actual.face, shot.portal.face));
        CHECK(actual.size == shot.portal.size);
        CHECK(tide_get_Wall(world, actual.wall).version == wall.version + 1);
    } else {
        CHECK(memcmp(&actual, &previous, sizeof actual) == 0);
        if (shot.hit) {
            CHECK(world->Fold.fizzleTime > 0 && world->Fold.fizzleEnd == id * 2 + side);
            CHECK(same_point(world->Fold.fizzleAt, shot.point) && same_point(world->Fold.fizzleFace, shot.face));
        } else CHECK(world->Fold.fizzleTime == 0);
    }
}

static void preview_pose(tide_world *world, tide_float3 at, float yaw, float pitch)
{
    choose(world, 0, BENCH);
    ticks(world, 60);
    place(world, 0, at, yaw, pitch);
    ticks(world, 3);
}

static void fold_preview_matches_firing(void)
{
    tide_world world = {0};
    start(&world);
    // Ordinary receptive wall: the floor nudges eye-height aim down to its sill.
    preview_pose(&world, (tide_float3){-5, 0, -7}, SOUTH, 0);
    preview_pair shot = read_preview(&world, 0);
    CHECK(shot.first.hit && shot.first.valid && shot.second.valid);
    CHECK(near(shot.first.portal.at.y, 1.3f, .001f));
    fire_preview(&world, 0, 0, shot.first);
    tide_world_tick(&world); // Apply the replacement collider before aiming at it again.
    // Replacing one's own end is valid; placing its other end over it is not.
    shot = read_preview(&world, 0);
    CHECK(shot.first.valid && shot.second.hit && !shot.second.valid);
    fire_preview(&world, 0, 1, shot.second);

    // A shot by the wall's corner is moved sideways to clear the adjacent wall.
    preview_pose(&world, (tide_float3){-9.7f, 0, -7}, SOUTH, 0);
    shot = read_preview(&world, 0);
    CHECK(shot.first.valid && shot.first.portal.at.x > pilot(&world, 0).position.x + .2f);
    fire_preview(&world, 0, 0, shot.first);

    // The same shot into the half frame acquires its wall-owned size and centre.
    preview_pose(&world, (tide_float3){-5, 0, -5}, WEST, 0);
    shot = read_preview(&world, 0);
    CHECK(shot.first.valid && shot.first.portal.size == .5f);
    CHECK(same_point(shot.first.portal.at, (tide_float3){-10, .65f, -5}));
    fire_preview(&world, 0, 0, shot.first);

    preview_pose(&world, (tide_float3){-5, 0, -7}, SOUTH, 0);
    world.Fold.busy.at[0] = 3;
    shot = read_preview(&world, 0);
    CHECK(shot.first.hit && !shot.first.valid && !shot.second.valid);
    fire_preview(&world, 0, 0, shot.first);

    // Bare concrete and a receptive-but-thin partition are distinct refusals.
    preview_pose(&world, (tide_float3){5, 0, 8}, EAST, 0);
    shot = read_preview(&world, 0);
    CHECK(shot.first.hit && !shot.first.valid);
    fire_preview(&world, 0, 0, shot.first);
    preview_pose(&world, (tide_float3){-1, 0, 2}, NORTH, 0);
    shot = read_preview(&world, 0);
    CHECK(shot.first.hit && !shot.first.valid);
    fire_preview(&world, 0, 0, shot.first);

    // Through a grille, a closed shutter still blocks the real receptive wall.
    choose(&world, 0, RECEPTION);
    ticks(&world, 100);
    place(&world, 0, (tide_float3){10.8f, 0, 0}, NORTH, 0);
    ticks(&world, 3);
    shot = read_preview(&world, 0);
    CHECK(shot.first.hit && !shot.first.valid && near(shot.first.point.z, 2.2f, .03f));
    fire_preview(&world, 0, 0, shot.first);
    place(&world, 1, (tide_float3){-8, 0, 4}, NORTH, 0);
    ticks(&world, 150);
    shot = read_preview(&world, 0);
    CHECK(shot.first.valid && near(shot.first.portal.at.z, 6, .001f));
    fire_preview(&world, 0, 0, shot.first);

    // No surface in range gives no candidate and retains the silent miss rule.
    preview_pose(&world, (tide_float3){-5, 40, -7}, NORTH, 1.3f);
    shot = read_preview(&world, 0);
    CHECK(!shot.first.hit && !shot.first.valid && !shot.second.hit);
    fire_preview(&world, 0, 0, shot.first);
    tide_world_free(&world);
}

static void ownership_and_movement(void)
{
    tide_world world = {0};
    start(&world);
    CHECK(world.Place.id == BENCH && world.Place.kind == PlaceKind_Bench);
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op0).player, player(0)));
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op1).player, player(0)));
    // In the lift they came by, facing out of it, whose door is shut for a moment and then opens
    ticks(&world, 30);
    CHECK(near(pilot(&world, 0).position.y, 0, 0.03f) && pilot(&world, 0).grounded); // On the floor, which is Box3D's
    CHECK(near(pilot(&world, 0).position.x, 3.8f, 0.05f) && near(pilot(&world, 0).position.z, -12.6f, 0.05f) && pilot(&world, 0).yaw == 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 20);
    CHECK(pilot(&world, 0).position.z > -12.4f && pilot(&world, 0).position.z < -10.6f);
    ticks(&world, 60);
    CHECK(pilot(&world, 0).position.z > -9.5f && pilot(&world, 0).position.z < -4);
    CHECK(near(pilot(&world, 1).position.z, -12.6f, 0.05f)); // Alone, the other body stays where it's left
    input(&world, 0, (PilotInput){.jump = true});
    ticks(&world, 8);
    CHECK(pilot(&world, 0).position.y > 0.3f);
    // The strip of blood: whoever walks into it is back at the start
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 300 && pilot(&world, 0).taken == 0; step++) tide_world_tick(&world);
    CHECK(pilot(&world, 0).taken > 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 150);
    CHECK(near(pilot(&world, 0).position.z, -12.6f, 0.1f) && near(pilot(&world, 0).position.x, 3.8f, 0.1f) && near(pilot(&world, 0).position.y, 0, 0.05f));
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    CHECK(world.Arena.joined == 2);
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op1).player, player(1)));
    tide_world_player_left(&world, player(1));
    tide_world_tick(&world);
    CHECK(world.Arena.joined == 1);
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op1).player, player(0)));
    tide_world_free(&world);
}

static void folds_are_shot_and_crossed(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 8);
    // A shot at the ceiling takes no fold
    place(&world, 0, (tide_float3){-5, 0, -7}, SOUTH, 1.3f);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && world.Fold.fizzleTime > 0 && world.Fold.fizzleEnd == 0);
    // Nor does the papered partition, which is too thin for one, nor the bare east wall
    place(&world, 0, (tide_float3){-1, 0, 2}, NORTH, 0);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && world.Fold.fizzleTime > 0);
    place(&world, 0, (tide_float3){5, 0, 8}, EAST, 0);
    ticks(&world, 40);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && world.Fold.fizzleTime > 0);
    // The south wall takes one, standing on the floor where it was aimed near it
    place(&world, 0, (tide_float3){-5, 0, -7}, SOUTH, 0);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].open);
    CHECK(near(world.Fold.ends.at[0].at.x, -5, 0.01f) && near(world.Fold.ends.at[0].at.z, -10, 0.001f) && near(world.Fold.ends.at[0].at.y, 1.3f, 0.001f));
    CHECK(world.Fold.ends.at[0].face.z > 0.99f);
    // One end alone is still a wall
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 120);
    CHECK(world.Fold.traversals == 0 && pilot(&world, 0).position.z > -10);
    // The other end, on the west wall. Not while someone stands in an end's mouth, nor for a moment after
    input(&world, 0, (PilotInput){0});
    tide_world_tick(&world);
    press(&world, (PilotInput){.cyan = true});
    CHECK(!world.Fold.ends.at[1].open);
    place(&world, 0, (tide_float3){-5, 0, -2.5f}, WEST, 0);
    ticks(&world, 6);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].open && world.Fold.ends.at[1].face.x > 0.99f && world.Fold.ends.at[1].size == 1);
    CHECK(near(world.Fold.ends.at[1].at.x, -10, 0.001f) && near(world.Fold.ends.at[1].at.z, -2.5f, 0.01f));
    // An end can't go where it would overlap the other
    press(&world, (PilotInput){.amber = true});
    CHECK(near(world.Fold.ends.at[0].at.z, -10, 0.001f) && world.Fold.fizzleTime > 0);
    ticks(&world, 3);
    // Walking into one comes out of the other, turned, with its speed, and never behind a wall's face
    place(&world, 0, (tide_float3){-5, 0, -8}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    bool crossed = false;
    for (int step = 0; step < 90 && !crossed; step++) {
        tide_world_tick(&world);
        const Operator now = pilot(&world, 0);
        CHECK(now.position.x > -10.1f && now.position.z > -10.2f);
        crossed = world.Fold.traversals == 1;
    }
    CHECK(crossed);
    const Operator out = pilot(&world, 0);
    CHECK(out.position.x > -10 && out.position.x < -9.8f && near(out.position.z, -2.5f, 0.3f));
    CHECK(out.velocity.x > 3 && tide_sin_f(out.yaw) > 0.99f);
    ticks(&world, 30);
    CHECK(pilot(&world, 0).position.x > -8.5f && world.Fold.traversals == 1);
    // A cube thrown at one flies out of the other
    put_cube(&world, (tide_float3){-5, 1.2f, -8}, (tide_float3){0, 0, -7});
    place(&world, 0, (tide_float3){3, 0, 3}, 0, 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    CHECK(world.Fold.traversals == 2);
    CHECK(world.Fold.cube.x > -10 && world.Fold.cube.z > -4);
    // What lies just past a fold is within reach through it: picked up from before the other end
    ticks(&world, 90);
    put_cube(&world, (tide_float3){-9, 0.4f, -2.5f}, (tide_float3){0});
    place(&world, 0, (tide_float3){-5, 0, -9}, SOUTH, 0);
    ticks(&world, 6);
    press(&world, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    // ...and it comes back through the fold with the hand that holds it
    input(&world, 0, (PilotInput){.move = {0, -1}});
    ticks(&world, 30);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    CHECK(world.Fold.holding.at[0] == 1 && world.Fold.cube.z > -10 && world.Fold.cube.z < -5.5f && near(world.Fold.cube.x, -5, 0.6f));
    // A click with it in hand throws it, and shoots no fold
    const tide_float3 amber = world.Fold.ends.at[0].at;
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.holding.at[0] == 0 && world.Fold.ends.at[0].at.x == amber.x && world.Fold.ends.at[0].at.z == amber.z && world.Fold.fizzleTime <= 0);
    tide_world_free(&world);
}

// An end put on a wall as a place is built, as play.tide's PlaceEnd puts it: no shot, so no wall is told its
// folds changed, and its hole is cut all the same.
static void ends_placed_at_building(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 8);
    // Where two ends go, on the south wall and the west, and the walls they're on: shot, one at a time, alone
    place(&world, 0, (tide_float3){-5, 0, -7}, SOUTH, 0);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    const Portal south = world.Fold.ends.at[0];
    place(&world, 0, (tide_float3){-5, 0, -2.5f}, WEST, 0);
    ticks(&world, 4);
    press(&world, (PilotInput){.amber = true});
    const Portal west = world.Fold.ends.at[0];
    CHECK(south.open && west.open && south.face.z > 0.99f && west.face.x > 0.99f);
    // Given to operator B as a builder gives them: the walls' pieces are cut for them by how many there are
    world.Fold.ends.at[0] = (Portal){.face = {0, 0, 1}, .size = 1};
    world.Fold.ends.at[2] = south;
    world.Fold.ends.at[3] = west;
    ticks(&world, 2);
    walk_through(&world, 0, (tide_float3){-5, 0, -8}, SOUTH, 1, 90);
    CHECK(pilot(&world, 0).position.x > -10 && near(pilot(&world, 0).position.z, -2.5f, 0.5f));
    tide_world_free(&world);
}

// The ends' sizes, which are the frames': what goes in one comes out the other's size, again and again.
static void sizes(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 8);
    // A shot by the half frame on the west wall goes into it, at its size; one well clear of it is whole
    two_ends(&world, (tide_float3){-5, 0, -5}, WEST, 0);
    CHECK(world.Fold.ends.at[0].size == 1 && world.Fold.ends.at[1].size == 0.5f);
    CHECK(near(world.Fold.ends.at[1].at.y, 0.65f, 0.001f) && near(world.Fold.ends.at[1].at.z, -5, 0.001f) && near(world.Fold.ends.at[1].at.x, -10, 0.001f));
    place(&world, 0, (tide_float3){-5, 0, -2.5f}, WEST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].size == 1 && near(world.Fold.ends.at[1].at.z, -2.5f, 0.01f));
    place(&world, 0, (tide_float3){-5, 0, -5}, WEST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].size == 0.5f);
    // In the whole end, out of the half one at half the size, on the floor; and again, a quarter
    walk_through(&world, 0, (tide_float3){-5, 0, -8.5f}, SOUTH, 1, 90);
    const Operator half = pilot(&world, 0);
    CHECK(half.scale == 0.5f && near(half.position.y, 0, 0.05f) && half.position.x > -10 && half.position.x < -5 && half.grounded);
    CHECK(near(tide_get_Box3D_Capsule(&world, world.Arena.op0).height, 0.9f, 0.001f));
    walk_through(&world, 0, (tide_float3){-5, 0, -8.5f}, SOUTH, 2, 150);
    CHECK(pilot(&world, 0).scale == 0.25f && near(pilot(&world, 0).position.y, 0, 0.05f));
    // Back up: small, into the half end and out of the whole one twice the size, twice
    walk_through(&world, 0, (tide_float3){-8.6f, 0, -5}, WEST, 3, 240);
    CHECK(pilot(&world, 0).scale == 0.5f && pilot(&world, 0).position.z > -10 && near(pilot(&world, 0).position.x, -5, 0.6f));
    walk_through(&world, 0, (tide_float3){-8.6f, 0, -5}, WEST, 4, 150);
    CHECK(pilot(&world, 0).scale == 1 && near(pilot(&world, 0).position.y, 0, 0.05f));
    // A whole body doesn't fit the half end
    place(&world, 0, (tide_float3){-8.6f, 0, -5}, WEST, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 90);
    CHECK(world.Fold.traversals == 4 && pilot(&world, 0).position.x > -10);
    input(&world, 0, (PilotInput){0});

    // The quarter frame: whole to a quarter, a quarter to a sixteenth, and no smaller: the end is a wall to that
    place(&world, 0, (tide_float3){-5, 0, -8}, WEST, -0.23f);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].size == 0.25f && near(world.Fold.ends.at[1].at.z, -8, 0.001f));
    walk_through(&world, 0, (tide_float3){-5, 0, -8.5f}, SOUTH, 5, 90);
    CHECK(pilot(&world, 0).scale == 0.25f);
    walk_through(&world, 0, (tide_float3){-5, 0, -9}, SOUTH, 6, 150);
    CHECK(pilot(&world, 0).scale == 0.0625f && near(pilot(&world, 0).position.y, 0, 0.02f));
    const int bumped = world.Fold.bumped;
    place(&world, 0, (tide_float3){-5, 0, -9.75f}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 150);
    input(&world, 0, (PilotInput){0});
    CHECK(world.Fold.bumped > bumped && world.Fold.traversals == 6 && pilot(&world, 0).scale == 0.0625f && pilot(&world, 0).position.z > -10.01f);
    // Gone under, it's back whole at the start
    place(&world, 0, (tide_float3){6, -2, -2}, 0, 0);
    ticks(&world, 140);
    CHECK(pilot(&world, 0).scale == 1 && near(pilot(&world, 0).position.z, -12.6f, 0.1f) && near(pilot(&world, 0).position.y, 0, 0.05f));

    // Growing: whole into the whole end, and out of the frame half again as big, one and a half times the size
    place(&world, 0, (tide_float3){-5, 0, 0}, WEST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].size == 1.5f && near(world.Fold.ends.at[1].at.y, 1.95f, 0.001f));
    walk_through(&world, 0, (tide_float3){-5, 0, -8.5f}, SOUTH, 7, 90);
    CHECK(pilot(&world, 0).scale == 1.5f && near(pilot(&world, 0).position.y, 0, 0.05f) && near(tide_get_Box3D_Capsule(&world, world.Arena.op0).height, 2.7f, 0.001f));
    // ...which no longer fits the whole end
    place(&world, 0, (tide_float3){-5, 0, -8.5f}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 120);
    input(&world, 0, (PilotInput){0});
    CHECK(world.Fold.traversals == 7 && pilot(&world, 0).position.z > -10);
    // ...but fits the one and a half, out of the double frame in the tall bay: twice the size
    place(&world, 0, (tide_float3){-6, 0, 8}, WEST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].size == 2 && near(world.Fold.ends.at[1].at.z, 8, 0.001f));
    place(&world, 0, (tide_float3){-6, 0, 0}, WEST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].size == 1.5f && near(world.Fold.ends.at[0].at.z, 0, 0.001f));
    walk_through(&world, 0, (tide_float3){-7, 0, 0}, WEST, 8, 120);
    CHECK(near(pilot(&world, 0).scale, 2, 0.001f) && near(pilot(&world, 0).position.y, 0, 0.05f) && pilot(&world, 0).position.z > 5);
    // Coming to a place again makes everyone whole
    choose(&world, 0, BENCH);
    ticks(&world, 10);
    CHECK(pilot(&world, 0).scale == 1 && near(pilot(&world, 0).position.z, -12.6f, 0.05f) && near(pilot(&world, 0).position.y, 0, 0.05f));
    tide_world_free(&world);
}

static void carrying_and_throwing(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 30);
    place(&world, 0, (tide_float3){-2, 0, -7.8f}, 0, -0.3f);
    tide_world_tick(&world);
    press(&world, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    ticks(&world, 40);
    // Held up before its holder
    CHECK(world.Fold.cube.y > 0.6f && near(world.Fold.cube.x, -2, 0.3f));
    press(&world, (PilotInput){.hurl = true});
    CHECK(world.Fold.holding.at[0] == 0);
    CHECK(tide_get_Box3D_Body(&world, world.Arena.cube).velocity.z > 5);
    ticks(&world, 60);
    CHECK(world.Fold.cube.z > -3);
    tide_world_free(&world);
}

// Whether an operator walking north from before a doorway in the north wall gets through it.
static bool through_door(tide_world *world, const int id, const float x)
{
    place(world, id, (tide_float3){x, 0, 10.6f}, NORTH, 0);
    input(world, 0, (PilotInput){.active = id, .move = {0, 1}});
    ticks(world, 70);
    input(world, 0, (PilotInput){.active = id});
    tide_world_tick(world);
    return pilot(world, id).position.z > 13;
}

// Plates by weight, and the doors their wires go to: open only while weighed down enough, nothing latching.
static void plates_by_mass(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 8);
    // The standard plate: nothing on it, its door is shut
    CHECK((world.Arena.signals & 1) == 0 && !through_door(&world, 0, 3.5f));
    // A whole operator on it holds the door open...
    place(&world, 1, (tide_float3){3.5f, 0, 9}, 0, 0);
    ticks(&world, 10);
    CHECK((world.Arena.signals & 1) != 0);
    CHECK(through_door(&world, 0, 3.5f));
    // ...and let up, it's shut again at once
    place(&world, 1, (tide_float3){3.5f, 0, 6}, 0, 0);
    ticks(&world, 3);
    CHECK((world.Arena.signals & 1) == 0);
    ticks(&world, 30);
    CHECK(!through_door(&world, 0, 3.5f));
    // The cube holds it
    put_cube(&world, (tide_float3){3.5f, 0.4f, 9}, (tide_float3){0});
    ticks(&world, 30);
    CHECK((world.Arena.signals & 1) != 0);
    put_cube(&world, (tide_float3){-2, 0.4f, -6}, (tide_float3){0});
    ticks(&world, 30);
    CHECK((world.Arena.signals & 1) == 0);
    // A half-size operator weighs an eighth as much, which isn't enough
    two_ends(&world, (tide_float3){-5, 0, -5}, WEST, 0);
    walk_through(&world, 0, (tide_float3){-5, 0, -8.5f}, SOUTH, 1, 90);
    CHECK(pilot(&world, 0).scale == 0.5f);
    place(&world, 0, (tide_float3){3.5f, 0, 9}, 0, 0);
    ticks(&world, 10);
    CHECK((world.Arena.signals & 1) == 0);

    // The heavy plate: the cube isn't enough, nor are the whole operator and the cube together
    put_cube(&world, (tide_float3){8, 0.4f, 9}, (tide_float3){0});
    ticks(&world, 30);
    CHECK((world.Arena.signals & 2) == 0);
    place(&world, 1, (tide_float3){7.4f, 0, 8.4f}, 0, 0);
    ticks(&world, 10);
    CHECK((world.Arena.signals & 2) == 0);
    place(&world, 1, (tide_float3){3, 0, 3}, 0, 0);
    // The cube made twice its size, through the double frame, is eight times as heavy, and is
    place(&world, 0, (tide_float3){-6, 0, 8}, WEST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].size == 2);
    const int before = world.Fold.traversals;
    place(&world, 0, (tide_float3){3, 0, -6}, 0, 0);
    put_cube(&world, (tide_float3){-5, 1.2f, -8.5f}, (tide_float3){0, 0, -7});
    ticks(&world, 60);
    CHECK(world.Fold.traversals == before + 1 && world.Fold.cubeScale == 2);
    put_cube(&world, (tide_float3){8, 0.78f, 9}, (tide_float3){0});
    ticks(&world, 30);
    CHECK((world.Arena.signals & 2) != 0 && through_door(&world, 1, 8));
    // ...and too big for a whole operator to lift: they strain at it, and it stays where it is
    place(&world, 1, (tide_float3){8, 0, 7.2f}, NORTH, -0.3f);
    tide_world_tick(&world);
    press(&world, (PilotInput){.active = 1, .carry = true});
    CHECK(world.Fold.holding.at[1] == 0 && world.Fold.strained.at[1] > 0);

    // The plate that lingers: its door stays open three seconds after it's let up, then shuts
    place(&world, 1, (tide_float3){-1, 0, 9}, 0, 0);
    ticks(&world, 10);
    CHECK((world.Arena.signals & 4) != 0);
    place(&world, 1, (tide_float3){-1, 0, 6}, 0, 0);
    ticks(&world, 150);
    CHECK((world.Arena.signals & 4) != 0);
    ticks(&world, 40);
    CHECK((world.Arena.signals & 4) == 0);
    // The lift's gate is its plate's: up while it's weighed down, down again when it isn't
    CHECK(world.Arena.gate == 0);
    place(&world, 1, (tide_float3){6.5f, 0, 3}, 0, 0);
    ticks(&world, 60);
    CHECK((world.Arena.signals & 8) != 0 && world.Arena.gate > 0.99f);
    place(&world, 1, (tide_float3){6.5f, 0, 0.5f}, 0, 0);
    ticks(&world, 60);
    CHECK(world.Arena.gate < 0.01f);
    tide_world_free(&world);
}

// Both in the lift out: its gate shuts on them, and they're in the next place's lift as they stood in this one,
// turned as the two lifts are turned.
static void lift_rides(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 10);
    put_cube(&world, (tide_float3){6.5f, 0.4f, 3}, (tide_float3){0});
    ticks(&world, 60);
    CHECK(world.Arena.gate > 0.99f);
    const tide_float3 lift = world.Place.exit;
    const float leftWay = world.Place.exitYaw + TIDE_PI_F;
    place(&world, 0, (tide_float3){lift.x + 0.7f, 0, lift.z - 0.8f}, EAST, 0);
    place(&world, 1, (tide_float3){lift.x - 0.6f, 0, lift.z + 1.5f}, 0.5f, 0);
    const int32_t built = world.Arena.chamber;
    ticks(&world, 20);
    CHECK(world.Arena.gate < 0.5f && world.Arena.chamber == built);
    const Operator a = pilot(&world, 0), b = pilot(&world, 1);
    ticks(&world, 120);
    CHECK(world.Arena.chamber == built + 1 && world.Arena.rode == built + 1 && world.Arena.depth == 0 && world.Place.id == RECEPTION);
    const tide_float3 arrival = world.Place.arrival;
    const float arrivalYaw = world.Place.arrivalYaw;
    const tide_float3 la = turn_by((tide_float3){a.position.x - lift.x, 0, a.position.z - lift.z}, -leftWay);
    const tide_float3 lb = turn_by((tide_float3){b.position.x - lift.x, 0, b.position.z - lift.z}, -leftWay);
    const tide_float3 wa = turn_by(la, arrivalYaw), wb = turn_by(lb, arrivalYaw);
    CHECK(near(pilot(&world, 0).position.x, arrival.x + wa.x, 0.1f) && near(pilot(&world, 0).position.z, arrival.z + wa.z, 0.1f) && near(pilot(&world, 0).position.y, 0, 0.05f));
    CHECK(near(pilot(&world, 1).position.x, arrival.x + wb.x, 0.1f) && near(pilot(&world, 1).position.z, arrival.z + wb.z, 0.1f));
    // Facing east into the lift out, op A faces north out of the lift come by
    CHECK(near(pilot(&world, 0).yaw, 0, 0.01f) && near(pilot(&world, 1).yaw, 0.5f - TIDE_PI_F / 2, 0.01f));
    // Its door opens, and they walk out
    ticks(&world, 100);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 40);
    CHECK(pilot(&world, 0).position.z > -10);
    tide_world_free(&world);
}

// R builds the place again for both: ends gone, everyone whole and back in the lift.
static void restart(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 8);
    two_ends(&world, (tide_float3){-5, 0, -5}, WEST, 0);
    walk_through(&world, 0, (tide_float3){-5, 0, -8.5f}, SOUTH, 1, 90);
    CHECK(pilot(&world, 0).scale == 0.5f);
    const int32_t built = world.Arena.chamber;
    press(&world, (PilotInput){.reset = true});
    ticks(&world, 10);
    CHECK(world.Arena.chamber == built + 1 && world.Place.id == BENCH && !world.Fold.ends.at[0].open && !world.Fold.ends.at[1].open);
    CHECK(pilot(&world, 0).scale == 1 && near(pilot(&world, 0).position.z, -12.6f, 0.05f) && near(pilot(&world, 1).position.z, -12.6f, 0.05f));
    // A place that isn't one changes nothing
    choose(&world, 0, PLACES);
    CHECK(world.Arena.chamber == built + 1);
    tide_world_free(&world);
}

// A pad throws whoever steps on it, over the blood.
static void pads_throw(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 8);
    place(&world, 1, (tide_float3){8.6f, 0, -7.5f}, NORTH, 0);
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    bool thrown = false;
    for (int step = 0; step < 60 && !thrown; step++) {
        tide_world_tick(&world);
        thrown = pilot(&world, 1).flung > 0;
    }
    CHECK(thrown && pilot(&world, 1).velocity.y > 3);
    input(&world, 0, (PilotInput){.active = 1});
    ticks(&world, 90);
    CHECK(pilot(&world, 1).taken == 0 && pilot(&world, 1).position.z > -1 && near(pilot(&world, 1).position.y, 0, 0.05f));
    tide_world_free(&world);
}

// A lift that stops on the way: its door opens on the landing, shuts after a moment with both in it, and the lift
// goes on to the next place, where they stand as they stood.
static void lift_stops(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, LANDING);
    ticks(&world, 3);
    CHECK(world.Place.id == LANDING && world.Place.kind == PlaceKind_Vignette && world.Arena.depth == -1);
    const tide_float3 a = pilot(&world, 0).position;
    ticks(&world, 90);
    CHECK(world.Arena.door > 0.99f && world.Arena.ride == 0);
    // Someone out on the landing when it's time keeps the door open
    place(&world, 0, (tide_float3){4, 0, 1}, EAST, 0);
    ticks(&world, 240);
    CHECK(world.Arena.door > 0.99f && world.Arena.ride == 0 && (world.Arena.darkGroups & 2) != 0);
    // Back in, it shuts, and they're on their way
    place(&world, 0, a, EAST, 0);
    const int32_t built = world.Arena.chamber;
    ticks(&world, 30);
    CHECK(world.Arena.door < 0.01f);
    ticks(&world, 90);
    CHECK(world.Arena.chamber == built + 1 && world.Place.id == RECEPTION && world.Arena.depth == 0);
    ticks(&world, 5);
    CHECK(near(pilot(&world, 0).position.x, -7.2f, 0.05f) && near(pilot(&world, 0).position.z, -10.1f, 0.05f) && near(pilot(&world, 0).yaw, 0, 0.01f));
    CHECK(near(pilot(&world, 1).position.x, -4.8f, 0.05f) && near(pilot(&world, 1).position.z, -10.1f, 0.05f));
    tide_world_free(&world);
}

static void replay_and_pack_are_identical(void)
{
    tide_world world = {0}, snapshot = {0}, restored = {0};
    start(&world);
    input(&world, 0, (PilotInput){.move = {0.3f, 1}, .look = {0.01f, 0}});
    ticks(&world, 20);
    tide_world_copy(&snapshot, &world);
    ticks(&world, 80);
    const uint64_t expected = tide_world_hash(&world);
    tide_world_copy(&world, &snapshot);
    ticks(&world, 80);
    CHECK(tide_world_hash(&world) == expected);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    CHECK(bytes != NULL && tide_world_pack(&world, bytes, size) == size);
    CHECK(tide_world_unpack(&restored, bytes, size));
    CHECK(tide_world_hash(&restored) == expected);
    free(bytes);
    input(&world, 0, (PilotInput){.active = 999, .move = {NAN, INFINITY}, .look = {INFINITY, NAN}});
    ticks(&world, 3);
    const Operator body = tide_get_Operator(&world, world.Arena.op1);
    CHECK(isfinite(body.position.x) && isfinite(body.position.y) && isfinite(body.position.z));
    printf("FOLDLINE replay hash: %llu\n", (unsigned long long)expected);
    tide_world_free(&restored);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
