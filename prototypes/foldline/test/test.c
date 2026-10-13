#include "foldline_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FOLDLINE check failed: %s (%s:%d)\n", #value, __FILE__, __LINE__); \
    exit(1); } } while (0)

static tide_player_id player(int index) { return tide_player_from_index(index); }
static bool near(float a, float b, float within) { return fabsf(a - b) < within; }

static void start(tide_world *world)
{
    tide_world_init(world, 1.0f / 60.0f);
    tide_world_player_joined(world, player(0));
    tide_world_tick(world);
    CHECK(world->Arena.joined == 1);
    CHECK(tide_has_Operator(world, world->Arena.op0));
    CHECK(tide_has_Operator(world, world->Arena.op1));
}

static void input(tide_world *world, int index, PilotInput controls)
{
    tide_world_set_input(world, player(index), controls);
    if (index == 0) tide_world_set_server_input(world, controls);
}

static void ticks(tide_world *world, int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(world);
}

static Operator pilot(const tide_world *world, int id)
{
    return tide_get_Operator(world, id == 0 ? world->Arena.op0 : world->Arena.op1);
}

// An operator standing at a place on the floor, looking a way.
static void place(tide_world *world, int id, tide_float3 feet, float yaw, float pitch)
{
    const tide_entity entity = id == 0 ? world->Arena.op0 : world->Arena.op1;
    Operator seen = tide_get_Operator(world, entity);
    Box3D_Body body = tide_get_Box3D_Body(world, entity);
    body.position = (tide_float3){feet.x, feet.y + 0.9f * seen.scale, feet.z};
    body.velocity = (tide_float3){0};
    seen.position = feet;
    seen.last = body.position;
    seen.yaw = yaw;
    seen.pitch = pitch;
    CHECK(tide_set_Operator(world, entity, seen));
    CHECK(tide_set_Box3D_Body(world, entity, body));
}

// Where a drum is along the trench, as play.tide's DrumAt has it.
static float drum_at(int ticks, int which)
{
    float along = (ticks / 60.0f * 6.0f + which * 17.2f) / 34.4f;
    along = (along - floorf(along)) * 34.4f;
    return -8.6f + (along < 17.2f ? along : 34.4f - along);
}

static void put_cube(tide_world *world, tide_float3 at, tide_float3 velocity)
{
    Box3D_Body cube = tide_get_Box3D_Body(world, world->Arena.cube);
    cube.position = at;
    cube.velocity = velocity;
    CHECK(tide_set_Box3D_Body(world, world->Arena.cube, cube));
    Cube marks = tide_get_Cube(world, world->Arena.cube);
    marks.last = at;
    CHECK(tide_set_Cube(world, world->Arena.cube, marks));
}

// A key pressed for one tick, then let go for another.
static void press(tide_world *world, PilotInput controls)
{
    input(world, 0, controls);
    tide_world_tick(world);
    input(world, 0, (PilotInput){0});
    tide_world_tick(world);
}

static void choose(tide_world *world, int sender, int level)
{
    // The game's only command: generated wire ID, null entity target, zigzag int.
    CHECK(level >= -1 && level <= 13);
    uint8_t payload[11] = {0};
    payload[0] = (uint8_t)TIDE_COMMAND_ID_SelectChamber;
    payload[1] = (uint8_t)(TIDE_COMMAND_ID_SelectChamber >> 8);
    payload[10] = (uint8_t)(level < 0 ? -level * 2 - 1 : level * 2);
    const uint32_t sequence = tide_game_api.command_sequence(world, player(sender), 0) + 1;
    CHECK(tide_game_api.command(world, player(sender), 0, sequence, payload, sizeof payload));
    tide_world_tick(world);
}

// The fourth room, whose floor is whole: an end on the south wall before an
// operator at (0, -8) who looks south, and one on the west wall when they look west.
static const float SOUTH = TIDE_PI_F, WEST = -TIDE_PI_F / 2, EAST = TIDE_PI_F / 2;

static void open_floor(tide_world *world)
{
    start(world);
    choose(world, 0, 3);
    ticks(world, 8);
    CHECK(world->Arena.level == 3);
}

static void ownership_and_movement(void)
{
    tide_world world = {0};
    start(&world);
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op0).player, player(0)));
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op1).player, player(0)));
    // In the lift they came by, whose door is shut for a moment and then opens
    ticks(&world, 30);
    CHECK(near(pilot(&world, 0).position.y, 0, 0.03f) && pilot(&world, 0).grounded); // On the floor, which is Box3D's
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 20);
    CHECK(pilot(&world, 0).position.z < -13.6f && pilot(&world, 0).position.z > -17);
    ticks(&world, 90);
    CHECK(pilot(&world, 0).position.z > -12 && pilot(&world, 0).position.z < -4);
    CHECK(near(pilot(&world, 1).position.z, -15.6f, 0.05f)); // Alone, the other body stays where it's left
    input(&world, 0, (PilotInput){.jump = true});
    ticks(&world, 8);
    CHECK(pilot(&world, 0).position.y > 0.3f);
    // The first chamber's pit: whoever walks into it is back at the start
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 300 && pilot(&world, 0).taken == 0; step++) tide_world_tick(&world);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 150);
    CHECK(pilot(&world, 0).position.z < -3.5f && near(pilot(&world, 0).position.y, 0, 0.05f));
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
    open_floor(&world);
    // A shot at the ceiling takes no fold
    place(&world, 0, (tide_float3){0, 0, -8}, SOUTH, 1.3f);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && world.Fold.fizzleTime > 0 && world.Fold.fizzleEnd == 0);
    // Nor do the thin walls of the room in the corner
    place(&world, 0, (tide_float3){7.5f, 0, 2}, 0, 0);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && world.Fold.fizzleTime > 0);
    // The south wall takes one, standing on the floor where it was aimed near it
    place(&world, 0, (tide_float3){0, 0, -8}, SOUTH, 0);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].open);
    CHECK(near(world.Fold.ends.at[0].at.x, 0, 0.01f) && near(world.Fold.ends.at[0].at.z, -13, 0.001f) && near(world.Fold.ends.at[0].at.y, 1.3f, 0.001f));
    CHECK(world.Fold.ends.at[0].face.z > 0.99f);
    // One end alone is still a wall
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 120);
    CHECK(world.Fold.traversals == 0 && pilot(&world, 0).position.z > -13);
    // The other end, on the west wall. Not while someone stands in an end's mouth, nor for a moment after
    input(&world, 0, (PilotInput){0});
    tide_world_tick(&world);
    press(&world, (PilotInput){.cyan = true});
    CHECK(!world.Fold.ends.at[1].open);
    place(&world, 0, (tide_float3){0, 0, -8}, WEST, 0);
    ticks(&world, 6);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].open && world.Fold.ends.at[1].face.x > 0.99f);
    CHECK(near(world.Fold.ends.at[1].at.x, -9.8f, 0.001f) && near(world.Fold.ends.at[1].at.z, -8, 0.01f));
    // An end can't go where it would overlap the other
    press(&world, (PilotInput){.amber = true});
    CHECK(near(world.Fold.ends.at[0].at.z, -13, 0.001f) && world.Fold.fizzleTime > 0);
    ticks(&world, 3);
    // Walking into one comes out of the other, turned, with its speed, and never behind a wall's face
    place(&world, 0, (tide_float3){0, 0, -11}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    bool crossed = false;
    for (int step = 0; step < 90 && !crossed; step++) {
        tide_world_tick(&world);
        const Operator now = pilot(&world, 0);
        CHECK(now.position.x > -9.9f && now.position.z > -14);
        crossed = world.Fold.traversals == 1;
    }
    CHECK(crossed);
    const Operator out = pilot(&world, 0);
    CHECK(out.position.x > -9.8f && out.position.x < -9.6f && near(out.position.z, -8, 0.3f));
    CHECK(out.velocity.x > 3 && tide_sin_f(out.yaw) > 0.99f);
    ticks(&world, 30);
    CHECK(pilot(&world, 0).position.x > -8.5f && world.Fold.traversals == 1);
    // A cube thrown at one flies out of the other
    put_cube(&world, (tide_float3){0, 1.2f, -11}, (tide_float3){0, 0, -7});
    place(&world, 0, (tide_float3){5, 0, -3}, 0, 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    CHECK(world.Fold.traversals == 2);
    CHECK(world.Fold.cube.x > -9.8f && world.Fold.cube.z > -12);
    // What lies just past a fold is within reach through it: picked up from before the other end
    ticks(&world, 90);
    put_cube(&world, (tide_float3){-8.8f, 0.4f, -8}, (tide_float3){0});
    place(&world, 0, (tide_float3){0, 0, -12}, SOUTH, 0);
    ticks(&world, 6);
    press(&world, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    // ...and it comes back through the fold with the hand that holds it
    input(&world, 0, (PilotInput){.move = {0, -1}});
    ticks(&world, 30);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    CHECK(world.Fold.holding.at[0] == 1 && world.Fold.cube.z > -13 && world.Fold.cube.z < -8.5f && near(world.Fold.cube.x, 0, 0.6f));
    // A click with it in hand throws it, and shoots no fold
    const tide_float3 amber = world.Fold.ends.at[0].at;
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.holding.at[0] == 0 && world.Fold.ends.at[0].at.x == amber.x && world.Fold.ends.at[0].at.z == amber.z && world.Fold.fizzleTime <= 0);
    tide_world_free(&world);
}

// A fold's ends of two sizes: what goes in one comes out the other's size.
static void sizes(void)
{
    tide_world world = {0};
    open_floor(&world);
    place(&world, 0, (tide_float3){0, 0, -8}, SOUTH, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.amber = true});
    // A shot by the small frame on the east wall goes into it, at its size
    place(&world, 0, (tide_float3){5, 0, 1.2f}, EAST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[0].open && world.Fold.ends.at[0].size == 1 && world.Fold.ends.at[1].open && world.Fold.ends.at[1].size == 0.5f);
    CHECK(near(world.Fold.ends.at[1].at.y, 0.65f, 0.001f) && near(world.Fold.ends.at[1].at.z, 1.5f, 0.001f) && near(world.Fold.ends.at[1].at.x, 9.8f, 0.001f));
    // ...and one well clear of it is whole
    place(&world, 0, (tide_float3){5, 0, -3}, EAST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].size == 1 && near(world.Fold.ends.at[1].at.z, -3, 0.01f));
    place(&world, 0, (tide_float3){5, 0, 1.2f}, EAST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].size == 0.5f);
    // In the whole end, out of the half one at half the size, on the floor
    place(&world, 0, (tide_float3){0, 0, -11}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 0; step++) tide_world_tick(&world);
    CHECK(world.Fold.traversals == 1);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    const Operator small = pilot(&world, 0);
    CHECK(small.scale == 0.5f && near(small.position.y, 0, 0.05f) && small.position.x < 9.8f && small.position.x > 5 && small.grounded);
    CHECK(near(tide_get_Box3D_Capsule(&world, world.Arena.op0).height, 0.9f, 0.001f));
    // Small, it goes back in the half end and comes out whole
    place(&world, 0, (tide_float3){8.6f, 0, 1.5f}, EAST, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 120 && world.Fold.traversals == 1; step++) tide_world_tick(&world);
    CHECK(world.Fold.traversals == 2);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    const Operator whole = pilot(&world, 0);
    CHECK(whole.scale == 1 && near(whole.position.y, 0, 0.05f) && whole.position.z > -13);
    // A whole body doesn't fit the half end
    place(&world, 0, (tide_float3){8.6f, 0, 1.5f}, EAST, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 90);
    CHECK(world.Fold.traversals == 2 && pilot(&world, 0).position.x < 9.8f);
    // A new chamber makes everyone whole again
    place(&world, 0, (tide_float3){0, 0, -11}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 2; step++) tide_world_tick(&world);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 5);
    CHECK(pilot(&world, 0).scale == 0.5f);
    choose(&world, 0, 0);
    ticks(&world, 10);
    CHECK(pilot(&world, 0).scale == 1 && near(pilot(&world, 0).position.z, -15.6f, 0.05f) && near(pilot(&world, 0).position.y, 0, 0.05f));
    tide_world_free(&world);
}

static void carrying_and_throwing(void)
{
    tide_world world = {0};
    open_floor(&world);
    ticks(&world, 30);
    place(&world, 0, (tide_float3){0, 0, -7.8f}, 0, -0.3f);
    tide_world_tick(&world);
    press(&world, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    ticks(&world, 40);
    // Held up before its holder
    CHECK(world.Fold.cube.y > 0.6f && near(world.Fold.cube.x, 0, 0.3f));
    press(&world, (PilotInput){.hurl = true});
    CHECK(world.Fold.holding.at[0] == 0);
    CHECK(tide_get_Box3D_Body(&world, world.Arena.cube).velocity.z > 5);
    ticks(&world, 60);
    CHECK(world.Fold.cube.z > -3);
    tide_world_free(&world);
}

static void chambers_and_commands(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 10);
    // The cube on one plate isn't enough for the first chamber's gate
    put_cube(&world, (tide_float3){5, 0.5f, 8}, (tide_float3){0});
    ticks(&world, 90);
    CHECK(world.Arena.plateA && !world.Arena.plateB && !world.Arena.solved && world.Arena.gate < 0.01f);
    // ...with someone on the other it opens
    place(&world, 1, (tide_float3){-5, 0, 8}, 0, 0);
    ticks(&world, 90);
    CHECK(world.Arena.plateB && world.Arena.solved && world.Arena.gate > 0.99f);
    // ...and only while they stay there
    place(&world, 1, (tide_float3){-5, 0, 5}, 0, 0);
    ticks(&world, 60);
    CHECK(!world.Arena.plateB && !world.Arena.solved && world.Arena.gate < 0.01f);
    // Whoever went through stands on the lobby's plate, and the gate stays up for the one who held it open
    place(&world, 1, (tide_float3){-5, 0, 8}, 0, 0);
    ticks(&world, 60);
    place(&world, 0, (tide_float3){0, 0, 16}, 0, 0);
    ticks(&world, 10);
    CHECK(world.Arena.hold && !world.Arena.complete);
    place(&world, 1, (tide_float3){-5, 0, 5}, 0, 0);
    ticks(&world, 60);
    CHECK(!world.Arena.solved && world.Arena.gate > 0.99f);
    // With both in the lift its door shuts on them whatever holds it, and the lift goes down: they're in the
    // next room, in the lift they came by, standing as they stood and facing as they faced
    place(&world, 1, (tide_float3){1.5f, 0, 15}, 0.5f, 0);
    const int32_t built = world.Arena.chamber;
    ticks(&world, 20);
    CHECK(world.Arena.gate < 0.5f && world.Arena.chamber == built && world.Arena.level == 0);
    ticks(&world, 120);
    CHECK(world.Arena.level == 1 && world.Arena.depth == 1 && world.Arena.chamber == built + 1 && world.Arena.rode == built + 1);
    CHECK(near(pilot(&world, 0).position.x, 6.5f, 0.1f) && near(pilot(&world, 0).position.z, -15.2f, 0.1f) && near(pilot(&world, 0).position.y, 0, 0.05f));
    CHECK(near(pilot(&world, 1).position.x, 8, 0.1f) && near(pilot(&world, 1).position.z, -16.2f, 0.1f));
    CHECK(near(pilot(&world, 1).yaw, 0.5f, 0.01f));
    // Its door opens
    ticks(&world, 100);
    place(&world, 0, (tide_float3){6.5f, 0, -15}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 40);
    CHECK(pilot(&world, 0).position.z > -13);
    input(&world, 0, (PilotInput){0});
    choose(&world, 0, 4);
    CHECK(world.Arena.level == 4 && world.Arena.depth == 6 && world.Arena.theme == 2 && world.Arena.chamber == built + 2 && !world.Arena.solved);
    ticks(&world, 20);
    CHECK(near(pilot(&world, 0).position.z, -15.6f, 0.05f) && near(pilot(&world, 0).position.y, 0, 0.05f));
    CHECK(near(world.Fold.cube.z, -8, 0.4f) && near(world.Fold.cube.x, -3, 0.4f) && !world.Fold.ends.at[0].open);
    // The places in order: two rooms and a stretch between, three times, each three its own place
    static const int levels[13] = {0, 1, 6, 3, 12, 7, 4, 5, 8, 10, 9, 11, 6};
    for (int room = 0; room < 13; room++) {
        choose(&world, 0, room);
        // The pool's first room of the hall's is off the way down, at the depth of the room that took its place
        CHECK(world.Arena.level == room && (room == 2 ? world.Arena.depth == 3 : levels[world.Arena.depth] == room) && world.Arena.theme == world.Arena.depth / 3 % 3);
    }
    // A room that isn't one changes nothing
    choose(&world, 0, 13);
    CHECK(world.Arena.level == 12);
    tide_world_free(&world);
}

// Between the rooms: something that comes only while nobody sees it.
static void between(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, 6);
    ticks(&world, 8);
    CHECK(world.Arena.level == 6 && world.Arena.solved);
    // No shot does anything down here
    place(&world, 0, (tide_float3){-3, 0, -15}, TIDE_PI_F, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && world.Fold.fizzleTime <= 0);
    place(&world, 0, (tide_float3){5.3f, 0, -15.6f}, 0, 0);
    // Both in the lift, where it comes for nobody: it stays where it starts
    const tide_float3 began = world.Stalker.at;
    ticks(&world, 400);
    CHECK(world.Stalker.at.x == began.x && world.Stalker.at.z == began.z && world.Arena.caught == 0);
    // Someone out in the maze with their back to it: it comes, by the ways there are, and never through a wall
    place(&world, 0, (tide_float3){-8.5f, 0, -11.7f}, TIDE_PI_F, -1.2f);
    float nearest = 1000;
    bool came = false;
    for (int step = 0; step < 900 && world.Arena.caught == 0; step++) {
        tide_world_tick(&world);
        const tide_float3 at = world.Stalker.at;
        CHECK(at.x > -9.8f && at.x < 9.8f && at.z > -13 && at.z < 13);
        const float away = hypotf(at.x + 8.5f, at.z + 11.7f);
        if (away < nearest) nearest = away;
        came = came || away < 6;
    }
    // ...and gets them: the stretch starts over, with both back in the lift
    CHECK(came && world.Arena.caught > 0 && nearest < 1.4f);
    const int32_t built = world.Arena.chamber;
    ticks(&world, 90);
    CHECK(world.Arena.chamber == built + 1 && world.Arena.level == 6 && world.Arena.caught == 0);
    CHECK(near(pilot(&world, 0).position.z, -15.6f, 0.05f) && near(pilot(&world, 1).position.z, -15.6f, 0.05f));
    // Looked at down a clear way, it doesn't move; looked away from, it does
    ticks(&world, 260);
    const tide_float3 stands = world.Stalker.at;
    const tide_float3 watch = {stands.x + 2.1f, 0, stands.z};
    // ...until every light fails for a moment, and it's nearer when they're back
    place(&world, 0, watch, WEST, 0);
    bool dark = false;
    int watched = 0;
    for (int step = 0; step < 600; step++) {
        tide_world_tick(&world);
        if (world.Stalker.dark) {
            dark = watched > 0;
            if (dark) break;
            continue;
        }
        if (watched > 0 || world.Stalker.seen) {
            CHECK(world.Stalker.seen && world.Stalker.at.x == stands.x && world.Stalker.at.z == stands.z && world.Arena.caught == 0);
            watched += 1;
        }
    }
    CHECK(dark && !world.Stalker.seen);
    ticks(&world, 30);
    CHECK(world.Stalker.at.x != stands.x || world.Stalker.at.z != stands.z || world.Arena.caught > 0);
    tide_world_free(&world);
}

// What each chamber rests on: who's the size to get where.
// The other two stretches: the long pool, and the works.
static void stretches(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, 7);
    ticks(&world, 30);
    CHECK(world.Arena.level == 7 && world.Arena.theme == 1 && world.Arena.solved && world.Fold.count == 5);
    // In the shallows, with it right beside them, nobody is had
    place(&world, 0, (tide_float3){-7, -0.45f, 0.5f}, 0, 0);
    bool beside = false;
    for (int step = 0; step < 900; step++) {
        tide_world_tick(&world);
        CHECK(pilot(&world, 0).taken == 0 && pilot(&world, 0).position.y > -0.6f);
        beside = beside || hypotf(world.Lurker.at.x + 7, world.Lurker.at.z - 0.5f) < 1.6f;
    }
    CHECK(beside);
    // A step off them, they are
    place(&world, 0, (tide_float3){-7, -1.2f, 2.6f}, 0, 0);
    bool had = false;
    for (int step = 0; step < 300 && !had; step++) {
        tide_world_tick(&world);
        had = pilot(&world, 0).taken > 0;
    }
    CHECK(had);
    tide_world_free(&world);
    // The works: four drums, a lane each, and the strips between are out of every one's reach
    start(&world);
    choose(&world, 0, 8);
    ticks(&world, 30);
    CHECK(world.Arena.level == 8 && world.Arena.theme == 2 && world.Arena.solved);
    place(&world, 0, (tide_float3){0, -1, 1}, 0, 0);
    for (int step = 0; step < 900; step++) {
        tide_world_tick(&world);
        CHECK(pilot(&world, 0).taken == 0);
    }
    place(&world, 0, (tide_float3){0, -1, 2.5f}, 0, 0);
    had = false;
    for (int step = 0; step < 600 && !had; step++) {
        tide_world_tick(&world);
        had = pilot(&world, 0).shredded;
    }
    CHECK(had);
    tide_world_free(&world);
}

// Where the shaft's first deck that goes by itself is, from x -3 to 4.5, as play.tide's MoverAt has it.
static float deck_at(int ticks)
{
    const float travel = 7.5f / 2.2f, wait = 1.3f, cycle = (travel + wait) * 2;
    float now = ticks / 60.0f / cycle;
    now = (now - floorf(now)) * cycle;
    const float along = now < wait ? 0 : now < wait + travel ? (now - wait) / travel : now < wait * 2 + travel ? 1 : 1 - (now - wait * 2 - travel) / travel;
    return -3 + 7.5f * along;
}

// The shaft: a room that isn't the hall's, on two storeys, with what it moves.
static void shaft(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, 9);
    ticks(&world, 30);
    CHECK(world.Arena.level == 9 && world.Arena.depth == 10 && world.Fold.count == 1);
    // Come to by a lift of its own, at the end of a passage
    CHECK(near(pilot(&world, 0).position.z, -20, 0.05f) && near(pilot(&world, 0).position.x, -1.2f, 0.05f) && pilot(&world, 0).grounded);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 240);
    CHECK(pilot(&world, 0).position.z > -9 && near(pilot(&world, 0).position.y, 0, 0.05f));
    // The tall room's walls take a fold to the balcony's height and none above it
    place(&world, 0, (tide_float3){0, 0, -4}, WEST, 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].open && near(world.Fold.ends.at[0].at.x, -8, 0.001f));
    place(&world, 0, (tide_float3){0, 0, -4}, WEST, 0.75f);
    ticks(&world, 3);
    press(&world, (PilotInput){.cyan = true});
    CHECK(!world.Fold.ends.at[1].open && world.Fold.fizzleTime > 0 && world.Fold.fizzleAt.y > 5);
    // The deck rises for as long as its plate is weighted, with whoever is on it, and goes down when it's let up
    place(&world, 0, (tide_float3){5, 0, -0.6f}, 0, 0);
    ticks(&world, 30);
    CHECK(near(pilot(&world, 0).position.y, 0, 0.05f) && !world.Arena.plateC);
    put_cube(&world, (tide_float3){-5, 0.5f, -6}, (tide_float3){0});
    ticks(&world, 240);
    CHECK(world.Arena.plateC && near(pilot(&world, 0).position.y, 5, 0.06f) && near(pilot(&world, 0).position.x, 5, 0.3f));
    put_cube(&world, (tide_float3){0, 0.5f, -6}, (tide_float3){0});
    ticks(&world, 240);
    CHECK(!world.Arena.plateC && near(pilot(&world, 0).position.y, 0, 0.06f));
    // Upstairs: someone standing on a deck that goes from side to side goes with it
    place(&world, 0, (tide_float3){deck_at(world.Arena.ticks), 5.1f, 14.5f}, 0, 0);
    float least = 100, most = -100;
    for (int step = 0; step < 600; step++) {
        tide_world_tick(&world);
        const Operator now = pilot(&world, 0);
        if (step < 20) continue;
        CHECK(near(now.position.y, 5, 0.08f) && near(now.position.z, 14.5f, 0.3f));
        if (now.position.x < least) least = now.position.x;
        if (now.position.x > most) most = now.position.x;
    }
    CHECK(most - least > 6.5f && least > -4.6f && most < 6.1f);
    // Off the decks the floor is 2.5 m down, and a slope goes back up to the near ledge
    place(&world, 0, (tide_float3){-8, 5.5f, 19}, TIDE_PI_F, 0);
    ticks(&world, 60);
    CHECK(near(pilot(&world, 0).position.y, 2.5f, 0.06f));
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 200);
    CHECK(near(pilot(&world, 0).position.y, 5, 0.06f) && pilot(&world, 0).position.z < 12.4f);
    // The cube thrown into a whole end comes out of the small frame small: what the slot in the cage is for.
    // Nobody whole gets in, and the small cube on the plate inside is a weight like any
    input(&world, 0, (PilotInput){0});
    place(&world, 0, (tide_float3){0, 0, -4}, EAST, 0);
    ticks(&world, 30);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[0].open && world.Fold.ends.at[0].size == 0.5f && world.Fold.ends.at[1].open && world.Fold.ends.at[1].size == 1);
    put_cube(&world, (tide_float3){5, 1.2f, -4}, (tide_float3){8, 0, 0});
    ticks(&world, 60);
    CHECK(world.Fold.cubeScale == 0.5f && world.Fold.cube.x < 0);
    place(&world, 0, (tide_float3){-2, 5, 22.75f}, WEST, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 90);
    CHECK(pilot(&world, 0).position.x > -4.2f && !world.Arena.plateA);
    put_cube(&world, (tide_float3){-5.6f, 5.4f, 22.75f}, (tide_float3){0});
    ticks(&world, 60);
    CHECK(world.Arena.plateA && world.Fold.cube.y < 5.4f);
    // The lift out is on the upper storey; both in it ride to the next place and stand on its floor
    input(&world, 0, (PilotInput){0});
    place(&world, 0, (tide_float3){-1, 5, 26.8f}, 0, 0);
    place(&world, 1, (tide_float3){1, 5, 26.2f}, 0, 0);
    const int32_t built = world.Arena.chamber;
    ticks(&world, 160);
    CHECK(world.Arena.chamber == built + 1 && world.Arena.depth == 11 && world.Arena.level == 11);
    CHECK(near(pilot(&world, 0).position.x, -1, 0.1f) && near(pilot(&world, 0).position.z, -23.8f, 0.1f) && near(pilot(&world, 0).position.y, 0, 0.06f));
    CHECK(near(pilot(&world, 1).position.x, 1, 0.1f) && near(pilot(&world, 1).position.z, -24.4f, 0.1f));
    tide_world_free(&world);
}

// The dark passage: lamps on a plate, what moves when they're off, the pit, and the pad over it.
static void dark(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, 11);
    ticks(&world, 30);
    CHECK(world.Arena.level == 11 && world.Arena.depth == 11 && !world.Arena.plateC);
    // It starts where the pad would put someone, and in the dark comes as near whoever is nearest as its side
    // of the pit lets it: drawn off to the east by someone at the pit's south-east corner
    CHECK(near(world.Shade.at.x, -2.5f, 0.01f) && near(world.Shade.at.z, 2.3f, 0.01f));
    place(&world, 0, (tide_float3){3.4f, 0, -8.3f}, 0, 0);
    ticks(&world, 400);
    CHECK(!world.Arena.plateC && world.Shade.at.x > 3 && near(world.Shade.at.z, -0.5f, 0.05f) && world.Arena.caught == 0);
    // On the plate there the lamps are on, and it stands where it is whoever comes
    place(&world, 0, (tide_float3){2.8f, 0, -7.1f}, 0, 0);
    ticks(&world, 20);
    CHECK(world.Arena.plateC && world.Shade.lit);
    const tide_float3 stood = world.Shade.at;
    // The pad throws the other over the pit, to the far side, a pad's throw from it
    place(&world, 1, (tide_float3){-2.5f, 0, -6.7f}, 0, 0);
    bool over = false;
    float highest = 0;
    for (int step = 0; step < 150 && !over; step++) {
        tide_world_tick(&world);
        const Operator now = pilot(&world, 1);
        CHECK(now.taken == 0 && world.Arena.caught == 0);
        if (now.position.y > highest) highest = now.position.y;
        over = step > 20 && now.grounded && now.position.z > -1;
    }
    CHECK(over && highest > 1.6f && near(pilot(&world, 1).position.x, -2.5f, 0.4f) && pilot(&world, 1).position.z > 1);
    CHECK(world.Shade.at.x == stood.x && world.Shade.at.z == stood.z);
    // The far room can't be shot into from this side of the pit; its wall by the passage's end takes a fold
    // from inside it
    place(&world, 0, (tide_float3){2.8f, 0, -7.1f}, 0.5f, 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open);
    place(&world, 1, (tide_float3){10, 0, 8.5f}, WEST, 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.active = 1, .amber = true});
    CHECK(world.Fold.ends.at[2].open && near(world.Fold.ends.at[2].at.x, 5, 0.001f) && world.Arena.caught == 0);
    // With the plate let up it's dark, and whoever is on its side of the pit is caught: the room starts over
    place(&world, 1, (tide_float3){-2, 0, 4}, 0, 0);
    place(&world, 0, (tide_float3){0, 0, -17}, 0, 0);
    const int32_t built = world.Arena.chamber;
    bool caught = false;
    for (int step = 0; step < 300 && !caught; step++) {
        tide_world_tick(&world);
        caught = world.Arena.caught > 0;
    }
    CHECK(caught && !world.Arena.plateC);
    ticks(&world, 100);
    CHECK(world.Arena.chamber == built + 1 && world.Arena.level == 11 && world.Arena.caught == 0);
    // The pit takes whoever falls in it, as the hall's does
    ticks(&world, 60);
    place(&world, 0, (tide_float3){0, 0, -3.5f}, 0, 0);
    ticks(&world, 60);
    CHECK(pilot(&world, 0).taken > 0);
    tide_world_free(&world);
}

// The flooded channel: its door, the plate under the water, and what the thing in it goes for.
static void undertow(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, 12);
    ticks(&world, 60);
    CHECK(world.Arena.level == 12 && world.Arena.depth == 4 && world.Arena.theme == 1 && world.Fold.count == 4);
    // It's in a pool as wide as the channel, and no wider
    for (int step = 0; step < 600; step++) {
        tide_world_tick(&world);
        CHECK(world.Lurker.at.x > -2.3f && world.Lurker.at.x < 2.3f && world.Lurker.at.z > -11.5f && world.Lurker.at.z < 5.5f);
    }
    // The door at the far end is shut
    place(&world, 0, (tide_float3){0, 0, 5.8f}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 60);
    CHECK(pilot(&world, 0).position.z < 6.3f && !world.Arena.plateC);
    // Someone who wades with nothing to draw it off is had
    place(&world, 0, (tide_float3){0, -1.5f, -6}, 0, 0);
    bool had = false;
    for (int step = 0; step < 400 && !had; step++) {
        tide_world_tick(&world);
        had = pilot(&world, 0).taken > 0;
    }
    CHECK(had);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 150);
    // The cube on the plate under the water opens the door, and is what it goes for
    put_cube(&world, (tide_float3){0, 0.2f, -3}, (tide_float3){0});
    ticks(&world, 200);
    CHECK(world.Arena.plateC && world.Fold.cube.y < -1 && hypotf(world.Lurker.at.x, world.Lurker.at.z + 3) < 1 && world.Lurker.lureLeft > 0);
    // A crate thrown in at the near end after it is what it goes for then: the last thing in
    place(&world, 0, (tide_float3){3, 0, -17.8f}, 0, -0.3f);
    ticks(&world, 3);
    press(&world, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] > 1);
    for (int step = 0; step <= 4; step++) {
        place(&world, 0, (tide_float3){3 - 0.75f * step, 0, -17.8f + 1.2f * step}, 0, 0);
        ticks(&world, 12);
    }
    CHECK(world.Fold.holding.at[0] > 1);
    press(&world, (PilotInput){.hurl = true});
    ticks(&world, 90);
    CHECK(world.Fold.holding.at[0] == 0 && world.Lurker.lure.z < -6 && world.Lurker.lureLeft > 4);
    // ...and with it down there someone wades the far half, out up the slope and through the open door
    place(&world, 1, (tide_float3){0, -1.5f, -1}, 0, 0);
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    bool through = false;
    for (int step = 0; step < 400 && !through; step++) {
        tide_world_tick(&world);
        CHECK(pilot(&world, 1).taken == 0);
        through = pilot(&world, 1).position.z > 7.5f && pilot(&world, 1).position.y > -0.05f;
    }
    CHECK(through && world.Lurker.at.z < -4);
    tide_world_free(&world);
}

// Three rooms in a row: what the way through rests on, and the way through.
static void sight(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 0, 10);
    ticks(&world, 100);
    CHECK(world.Arena.level == 10 && world.Arena.depth == 9 && world.Fold.count == 1);
    // A door is shut, and open only while its plate is weighted
    place(&world, 0, (tide_float3){0, 0, -14}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 90);
    CHECK(pilot(&world, 0).position.z < -11.8f);
    // A closed door also blocks a shot back to a receptive wall: no remote escape from the sealed room.
    input(&world, 0, (PilotInput){0});
    place(&world, 0, (tide_float3){0, 0, -10}, atan2f(-6, -7), 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.cyan = true});
    CHECK(!world.Fold.ends.at[1].open && world.Fold.fizzleTime > 0);
    place(&world, 0, (tide_float3){0, 0, -14}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    put_cube(&world, (tide_float3){4, 0.5f, -15}, (tide_float3){0});
    ticks(&world, 150);
    CHECK(world.Arena.plateC && pilot(&world, 0).position.z > -9);
    // Nothing in the second room takes a fold
    place(&world, 0, (tide_float3){0, 0, -7}, WEST, 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && world.Fold.fizzleTime > 0);
    // The gallery's wall cannot be reached through the second door while D is empty.
    place(&world, 0, (tide_float3){0, 0, -3.8f}, -0.248f, 0.284f);
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && !world.Arena.plateD && world.Fold.fizzleTime > 0);
    // With both doors open the far wall isn't seen from the first room, nor from the second door's plate
    place(&world, 1, (tide_float3){-4, 0, -9.6f}, 0.366f, 0.195f);
    ticks(&world, 150);
    CHECK(world.Arena.plateD);
    place(&world, 0, (tide_float3){0, 0, -20}, 0, 0.137f);
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open);
    press(&world, (PilotInput){.active = 1, .amber = true});
    CHECK(!world.Fold.ends.at[2].open);
    // From the second room's far end, through its door, it is: an end goes onto it, standing on the gallery
    place(&world, 0, (tide_float3){0, 0, -3.8f}, -0.248f, 0.284f);
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].open && near(world.Fold.ends.at[0].at.z, 10, 0.001f) && near(world.Fold.ends.at[0].at.y, 5.7f, 0.01f) && world.Fold.ends.at[0].at.x < -1);
    // ...and from the third room's floor the other side of it, for the other operator's end
    place(&world, 1, (tide_float3){1, 0, -1}, 0.2235f, 0.4f);
    place(&world, 0, (tide_float3){-4, 0, -9.6f}, 0, 0);
    ticks(&world, 10);
    press(&world, (PilotInput){.active = 1, .amber = true});
    CHECK(world.Fold.ends.at[2].open && near(world.Fold.ends.at[2].at.z, 10, 0.001f) && world.Fold.ends.at[2].at.x > 1);
    // Their other ends in the first room, the one room that takes them
    place(&world, 0, (tide_float3){0, 0, -17}, WEST, 0);
    place(&world, 1, (tide_float3){0, 0, -15}, EAST, 0);
    ticks(&world, 10);
    press(&world, (PilotInput){.cyan = true});
    press(&world, (PilotInput){.active = 1, .cyan = true});
    CHECK(world.Fold.ends.at[1].open && near(world.Fold.ends.at[1].at.x, -6, 0.001f) && world.Fold.ends.at[3].open && near(world.Fold.ends.at[3].at.x, 6, 0.001f));
    // With the cube taken off the first plate and nobody on the second, whoever is in the second room is shut in
    put_cube(&world, (tide_float3){-3, 0.5f, -17}, (tide_float3){0});
    ticks(&world, 150);
    CHECK(!world.Arena.plateC && !world.Arena.plateD);
    place(&world, 1, (tide_float3){0, 0, -9}, TIDE_PI_F, 0);
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    ticks(&world, 90);
    CHECK(pilot(&world, 1).position.z > -11.3f);
    // Each walks in at their own end and is up on their own side of the gallery, past a jump from the floor
    input(&world, 0, (PilotInput){0});
    place(&world, 0, (tide_float3){-4, 0, -17}, WEST, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 0; step++) tide_world_tick(&world);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 30);
    CHECK(world.Fold.traversals == 1 && near(pilot(&world, 0).position.y, 4.4f, 0.06f) && pilot(&world, 0).position.x < -0.5f && pilot(&world, 0).position.z > 7);
    place(&world, 1, (tide_float3){4, 0, -15}, EAST, 0);
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 1; step++) tide_world_tick(&world);
    input(&world, 0, (PilotInput){.active = 1});
    ticks(&world, 30);
    CHECK(world.Fold.traversals == 2 && near(pilot(&world, 1).position.y, 4.4f, 0.06f) && pilot(&world, 1).position.x > 0.5f);
    // The cube on one plate up there and someone on the other open the gate
    put_cube(&world, (tide_float3){-3.5f, 4.9f, 8.5f}, (tide_float3){0});
    place(&world, 1, (tide_float3){3.5f, 4.4f, 8.5f}, 0, 0);
    ticks(&world, 90);
    CHECK(world.Arena.plateA && world.Arena.plateB && world.Arena.gate > 0.99f);
    tide_world_free(&world);
}

static void chambers_hold(void)
{
    tide_world world = {0};
    // Someone who walks into the pit is caught under the floor, held however they move, and back at the start
    start(&world);
    ticks(&world, 8);
    place(&world, 0, (tide_float3){0, 0, -1}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}, .jump = true});
    bool caught = false;
    for (int step = 0; step < 120 && !caught; step++) {
        tide_world_tick(&world);
        caught = pilot(&world, 0).taken > 0;
    }
    CHECK(caught && pilot(&world, 0).position.y < -1.8f && pilot(&world, 0).position.y > -4);
    const float caught_at = pilot(&world, 0).position.z;
    ticks(&world, 60);
    CHECK(pilot(&world, 0).taken > 0.9f && near(pilot(&world, 0).position.z, caught_at, 0.1f) && pilot(&world, 0).position.y > -5);
    ticks(&world, 70);
    CHECK(pilot(&world, 0).taken == 0 && pilot(&world, 0).position.y > -0.1f && pilot(&world, 0).position.z < -5);
    tide_world_free(&world);
    // The vent is too low for a whole operator, and takes a half one
    start(&world);
    choose(&world, 0, 3);
    ticks(&world, 8);
    place(&world, 0, (tide_float3){5.5f, 0, 2.5f}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 90);
    CHECK(pilot(&world, 0).position.z < 4.6f);
    input(&world, 0, (PilotInput){0});
    // The vent cannot be used as a window to put a whole exit inside and skip shrinking.
    // Sweep the approach, aiming through both halves of the opening at several heights.
    for (int side = 0; side < 7; side++) {
        for (int slot = 0; slot < 6; slot++) {
            place(&world, 0, (tide_float3){(float)side * 1.3f, 0, 2.5f}, 0, 0);
            const float dx = 5.2f + (slot % 2) * 0.6f - pilot(&world, 0).position.x;
            const float dz = 2.3f;
            const float pitch = atan2f(0.3f + (slot / 2) * 0.3f - 1.55f, sqrtf(dx * dx + dz * dz));
            place(&world, 0, pilot(&world, 0).position, atan2f(dx, dz), pitch);
            ticks(&world, 2);
            press(&world, (PilotInput){.amber = true});
            CHECK(!world.Fold.ends.at[0].open || world.Fold.ends.at[0].at.z < 4.6f);
        }
    }
    tide_world_free(&world);
    // The third room is three lifts down, where the place is a pool's. Past the water no wall takes a fold
    start(&world);
    choose(&world, 0, 2);
    ticks(&world, 8);
    CHECK(world.Arena.depth == 3 && world.Arena.theme == 1);
    CHECK(world.Fold.cube.y > 1.8f && world.Fold.cube.x > 5.5f); // The cube is up on its gallery
    place(&world, 0, (tide_float3){0, 0, -6}, WEST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && world.Fold.fizzleTime > 0);
    // What's in the pool goes for the cube thrown in, and the pool is waded meanwhile: down a slope, slowly
    // through the water, up the other side
    put_cube(&world, (tide_float3){8, 1, 1}, (tide_float3){0});
    ticks(&world, 60);
    CHECK(world.Lurker.lureLeft > 0);
    place(&world, 0, (tide_float3){-8, 0, -2}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    float lowest = 0;
    int waded = 0;
    bool across = false;
    for (int step = 0; step < 400 && !across; step++) {
        tide_world_tick(&world);
        const Operator now = pilot(&world, 0);
        CHECK(now.taken == 0);
        if (now.position.y < lowest) lowest = now.position.y;
        if (now.position.y < -1) waded += 1;
        across = now.position.z > 4 && now.position.y > -0.05f && now.grounded;
    }
    CHECK(across && lowest < -1.4f && lowest > -1.6f && waded > 20 && world.Lurker.at.x > 4);
    // With nothing to draw it off it has whoever wades, and they're back where they started
    input(&world, 0, (PilotInput){0});
    ticks(&world, 480);
    CHECK(world.Lurker.lureLeft == 0);
    place(&world, 0, (tide_float3){world.Lurker.at.x - 3, 0, -2}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    bool had = false;
    for (int step = 0; step < 300 && !had; step++) {
        tide_world_tick(&world);
        had = pilot(&world, 0).taken > 0;
    }
    CHECK(had && pilot(&world, 0).position.y < -0.8f);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 150);
    CHECK(pilot(&world, 0).taken == 0 && near(pilot(&world, 0).position.z, -15.6f, 0.1f));
    tide_world_free(&world);
    // The fifth room is six down, in bare concrete: its pit is a trench, and a drum going along it leaves nothing
    start(&world);
    choose(&world, 0, 4);
    ticks(&world, 8);
    CHECK(world.Arena.depth == 6 && world.Arena.theme == 2);
    bool clear = false;
    for (int step = 0; step < 600 && !clear; step++) {
        tide_world_tick(&world);
        clear = fabsf(drum_at(world.Arena.ticks, 0)) > 8.3f && fabsf(drum_at(world.Arena.ticks, 1)) > 8.3f;
    }
    CHECK(clear);
    // Run across while both are at the ends
    place(&world, 0, (tide_float3){0, 0, -1.6f}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    across = false;
    lowest = 0;
    for (int step = 0; step < 200 && !across; step++) {
        tide_world_tick(&world);
        const Operator now = pilot(&world, 0);
        CHECK(now.taken == 0);
        if (now.position.y < lowest) lowest = now.position.y;
        across = now.position.z > 3.6f && now.position.y > -0.05f;
    }
    CHECK(across && lowest < -0.9f);
    // Stood in its way
    place(&world, 0, (tide_float3){0, -1, 1}, 0, 0);
    input(&world, 0, (PilotInput){0});
    had = false;
    for (int step = 0; step < 400 && !had; step++) {
        tide_world_tick(&world);
        had = pilot(&world, 0).shredded;
    }
    CHECK(had && pilot(&world, 0).taken > 0 && (fabsf(drum_at(world.Arena.ticks, 0)) < 1.2f || fabsf(drum_at(world.Arena.ticks, 1)) < 1.2f));
    ticks(&world, 90);
    CHECK(!pilot(&world, 0).shredded && pilot(&world, 0).taken == 0 && near(pilot(&world, 0).position.z, -15.6f, 0.1f));
    // ...and it throws the cube out of its way
    put_cube(&world, (tide_float3){3, -0.6f, 1}, (tide_float3){0});
    bool thrown = false;
    for (int step = 0; step < 400 && !thrown; step++) {
        tide_world_tick(&world);
        thrown = world.Fold.cube.y > 0.5f;
    }
    CHECK(thrown);
    tide_world_free(&world);
    // The second room's galleries are past a jump
    start(&world);
    choose(&world, 0, 1);
    ticks(&world, 8);
    place(&world, 0, (tide_float3){-2, 0, 6}, WEST, 0);
    for (int step = 0; step < 150; step++) {
        input(&world, 0, (PilotInput){.move = {0, 1}, .jump = step % 20 == 0});
        tide_world_tick(&world);
        CHECK(pilot(&world, 0).position.y < 1.3f && pilot(&world, 0).position.x > -4.4f);
    }
    // A fold shot at the wall over one stands on it, and whoever walks in at the other end is up there
    place(&world, 0, (tide_float3){2, 0, 6}, WEST, 0.13f);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].open && near(world.Fold.ends.at[0].at.x, -9.8f, 0.001f) && near(world.Fold.ends.at[0].at.y, 2.9f, 0.01f));
    place(&world, 0, (tide_float3){0, 0, -8}, SOUTH, 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.cyan = true});
    place(&world, 0, (tide_float3){0, 0, -11}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 0; step++) tide_world_tick(&world);
    ticks(&world, 30);
    CHECK(world.Fold.traversals == 1 && near(pilot(&world, 0).position.y, 1.6f, 0.05f) && pilot(&world, 0).position.x < -5 && pilot(&world, 0).grounded);
    // ...where its plate is, which counts only who's up there with it
    place(&world, 0, (tide_float3){-7, 1.6f, 6}, 0, 0);
    place(&world, 1, (tide_float3){7, 0, 9}, 0, 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 10);
    CHECK(world.Arena.plateA && !world.Arena.plateB);
    tide_world_free(&world);
    // The teaching room has one cube: it can't bypass the partner's plate relay.
    start(&world);
    choose(&world, 0, 1);
    ticks(&world, 30);
    CHECK(world.Fold.count == 1);
    put_cube(&world, (tide_float3){-7, 2.0f, 6}, (tide_float3){0});
    ticks(&world, 90);
    CHECK(world.Arena.plateA && !world.Arena.plateB && world.Arena.gate < 0.01f);
    // A box as a step still cannot clear the maintenance grille.
    put_cube(&world, (tide_float3){-3.35f, 0.4f, 2}, (tide_float3){0});
    ticks(&world, 40);
    place(&world, 0, (tide_float3){-3.3f, 0.78f, 2}, WEST, 0);
    ticks(&world, 10);
    CHECK(pilot(&world, 0).position.y > 0.6f && pilot(&world, 0).grounded);
    input(&world, 0, (PilotInput){.move = {0, 1}, .jump = true});
    tide_world_tick(&world);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 60);
    CHECK(pilot(&world, 0).position.x > -3.8f);
    tide_world_free(&world);
    // The fifth room's plate is on a gallery walled off from this side of the trench: the wall over it takes a
    // fold only from past the drums
    start(&world);
    choose(&world, 0, 4);
    ticks(&world, 8);
    place(&world, 0, (tide_float3){0, 0, -6}, -0.61f, 0.1f);
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.ends.at[0].open && world.Fold.fizzleTime > 0);
    place(&world, 0, (tide_float3){0, 0, 8}, WEST, 0.13f);
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.ends.at[0].open && near(world.Fold.ends.at[0].at.x, -9.8f, 0.001f) && near(world.Fold.ends.at[0].at.y, 2.9f, 0.01f));
    tide_world_free(&world);
    // The last room: the cube up on its gallery, and a cage whose way in is a meter square
    start(&world);
    choose(&world, 0, 5);
    ticks(&world, 30);
    CHECK(world.Arena.depth == 7 && world.Arena.theme == 2 && world.Fold.cube.y > 1.8f && world.Fold.cube.x < -5.5f);
    place(&world, 0, (tide_float3){6.5f, 0, 4.6f}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 90);
    CHECK(pilot(&world, 0).position.z < 5.9f);
    // Small by the frame on the south wall, an operator goes in
    place(&world, 0, (tide_float3){-3, 0, -3}, WEST, 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    place(&world, 0, (tide_float3){0.5f, 0, -9}, SOUTH, 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[0].open && world.Fold.ends.at[0].size == 1 && world.Fold.ends.at[1].open && world.Fold.ends.at[1].size == 0.5f);
    place(&world, 0, (tide_float3){-8, 0, -3}, WEST, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 0; step++) tide_world_tick(&world);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 20);
    CHECK(pilot(&world, 0).scale == 0.5f);
    place(&world, 0, (tide_float3){6.5f, 0, 4.6f}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 120);
    CHECK(pilot(&world, 0).position.z > 6.6f && pilot(&world, 0).position.x > 4.2f);
    // ...and on its plate, with the cube on the other, the gate opens
    place(&world, 0, (tide_float3){7, 0, 10}, 0, 0);
    put_cube(&world, (tide_float3){-5, 0.5f, 8}, (tide_float3){0});
    input(&world, 0, (PilotInput){0});
    ticks(&world, 90);
    CHECK(world.Arena.plateA && world.Arena.plateB && world.Arena.gate > 0.99f);
    tide_world_free(&world);
    // A half operator goes through the vent, round the baffle and into the room
    start(&world);
    choose(&world, 0, 3);
    ticks(&world, 8);
    place(&world, 0, (tide_float3){0, 0, -8}, SOUTH, 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    place(&world, 0, (tide_float3){5, 0, 1.5f}, EAST, 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.cyan = true});
    place(&world, 0, (tide_float3){0, 0, -11}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 0; step++) tide_world_tick(&world);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 20);
    CHECK(pilot(&world, 0).scale == 0.5f);
    place(&world, 0, (tide_float3){5.5f, 0, 3.5f}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 80);
    CHECK(pilot(&world, 0).position.z > 5.2f && pilot(&world, 0).position.z < 6.2f); // Through the wall, up to the baffle
    place(&world, 0, pilot(&world, 0).position, TIDE_PI_F / 2, 0);
    ticks(&world, 100);
    place(&world, 0, pilot(&world, 0).position, 0, 0);
    ticks(&world, 100);
    CHECK(pilot(&world, 0).position.x > 8 && pilot(&world, 0).position.z > 7);
    input(&world, 0, (PilotInput){0});
    place(&world, 0, (tide_float3){8.5f, 0, 10.5f}, EAST, 0.38f);
    ticks(&world, 3);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[1].open && near(world.Fold.ends.at[1].at.x, 9.8f, 0.01f)
        && world.Fold.ends.at[1].at.z > 8.7f && world.Fold.ends.at[1].size == 1);
    tide_world_free(&world);
}

// Drive the teaching puzzles from their arrival lift using inputs only: no placed bodies, cubes or ends.
static void walk_to(tide_world *world, int id, float x, float z)
{
    for (int step = 0; step < 600; step++) {
        const Operator now = pilot(world, id);
        const float dx = x - now.position.x, dz = z - now.position.z;
        const float distance = sqrtf(dx * dx + dz * dz);
        if (distance < 0.18f) {
            input(world, 0, (PilotInput){.active = id});
            ticks(world, 8);
            return;
        }
        input(world, 0, (PilotInput){.active = id, .move = {
            (dx * cosf(now.yaw) - dz * sinf(now.yaw)) / distance,
            (dx * sinf(now.yaw) + dz * cosf(now.yaw)) / distance}});
        tide_world_tick(world);
        CHECK(pilot(world, id).taken == 0);
    }
    fprintf(stderr, "Walk %d to %.2f %.2f stopped at %.2f %.2f %.2f\n", id, x, z,
        pilot(world, id).position.x, pilot(world, id).position.y, pilot(world, id).position.z);
    CHECK(false);
}

static void aim_at(tide_world *world, int id, tide_float3 target)
{
    for (int step = 0; step < 40; step++) {
        const Operator now = pilot(world, id);
        const float dx = target.x - now.position.x, dz = target.z - now.position.z;
        const float yaw = atan2f(dx, dz), pitch = atan2f(target.y - now.position.y - 1.55f * now.scale, sqrtf(dx * dx + dz * dz));
        const float turn = atan2f(sinf(yaw - now.yaw), cosf(yaw - now.yaw));
        if (fabsf(turn) < 0.001f && fabsf(pitch - now.pitch) < 0.001f) return;
        input(world, 0, (PilotInput){.active = id, .look = {
            fmaxf(-0.35f, fminf(0.35f, turn)), fmaxf(-0.35f, fminf(0.35f, pitch - now.pitch))}});
        tide_world_tick(world);
    }
    CHECK(false);
}

static void act(tide_world *world, int id, PilotInput controls)
{
    controls.active = id;
    input(world, 0, controls);
    tide_world_tick(world);
    input(world, 0, (PilotInput){.active = id});
    tide_world_tick(world);
}

static void teaching_solutions(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 100);
    walk_to(&world, 0, 6.5f, -10);
    walk_to(&world, 0, 0, -8);
    aim_at(&world, 0, (tide_float3){-9.8f, 1.3f, 6});
    act(&world, 0, (PilotInput){.amber = true});
    aim_at(&world, 0, (tide_float3){0, 1.3f, -13});
    act(&world, 0, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[0].open && world.Fold.ends.at[1].open);
    walk_to(&world, 0, -3, -9.5f);
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, 0, -10);
    aim_at(&world, 0, (tide_float3){0, 1.55f, -14});
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 120 && world.Fold.traversals < 2; step++) tide_world_tick(&world);
    CHECK(world.Fold.traversals >= 2 && pilot(&world, 0).position.z > 3 && world.Fold.holding.at[0] == 1);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 20);
    walk_to(&world, 0, 3.7f, 8);
    aim_at(&world, 0, (tide_float3){5, 0.4f, 8});
    ticks(&world, 35);
    act(&world, 0, (PilotInput){.carry = true});
    ticks(&world, 60);
    CHECK(world.Arena.plateA && !world.Arena.plateB);
    walk_to(&world, 1, 6.5f, -10);
    walk_to(&world, 1, 0, -10);
    aim_at(&world, 1, (tide_float3){0, 1.55f, -14});
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    for (int step = 0; step < 120 && pilot(&world, 1).position.z < 3; step++) tide_world_tick(&world);
    CHECK(pilot(&world, 1).position.z > 3);
    walk_to(&world, 1, -5, 8);
    ticks(&world, 60);
    CHECK(world.Arena.solved && world.Arena.gate > 0.99f);
    walk_to(&world, 0, 0, 10);
    walk_to(&world, 0, 0, 16);
    CHECK(world.Arena.hold);
    walk_to(&world, 1, -5, 10);
    walk_to(&world, 1, 0, 10);
    CHECK(!world.Arena.solved && world.Arena.gate > 0.99f);
    walk_to(&world, 1, 1.2f, 15.5f);
    ticks(&world, 150);
    CHECK(world.Arena.level == 1 && world.Arena.depth == 1);
    // High Ground: carry the only weight onto A, and use the other operator's own fold for B.
    walk_to(&world, 0, 6.5f, -10);
    walk_to(&world, 0, 2, 6);
    aim_at(&world, 0, (tide_float3){-9.8f, 2.9f, 6});
    act(&world, 0, (PilotInput){.amber = true});
    walk_to(&world, 0, 0, -8);
    aim_at(&world, 0, (tide_float3){0, 1.3f, -13});
    act(&world, 0, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[0].open && world.Fold.ends.at[1].open);
    walk_to(&world, 0, 0, -7.5f);
    act(&world, 0, (PilotInput){.carry = true});
    CHECK(world.Fold.holding.at[0] == 1);
    walk_to(&world, 0, 0, -10);
    aim_at(&world, 0, (tide_float3){0, 1.55f, -14});
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 120 && world.Fold.traversals < 2; step++) tide_world_tick(&world);
    CHECK(world.Fold.traversals >= 2 && pilot(&world, 0).position.x < -5);
    walk_to(&world, 0, -8.3f, 6);
    aim_at(&world, 0, (tide_float3){-7, 2, 6});
    ticks(&world, 35);
    act(&world, 0, (PilotInput){.carry = true});
    ticks(&world, 60);
    CHECK(world.Arena.plateA && !world.Arena.plateB);
    aim_at(&world, 0, (tide_float3){-11, 3.15f, 6});
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 120 && pilot(&world, 0).position.x < -5; step++) tide_world_tick(&world);
    CHECK(pilot(&world, 0).position.z < -10);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 20);
    walk_to(&world, 1, 6.5f, -10);
    walk_to(&world, 1, 2, 9);
    aim_at(&world, 1, (tide_float3){9.8f, 2.9f, 9});
    act(&world, 1, (PilotInput){.amber = true});
    walk_to(&world, 1, 0, 6);
    walk_to(&world, 1, 0, -6);
    walk_to(&world, 1, 2, -10);
    aim_at(&world, 1, (tide_float3){2, 1.3f, -13});
    act(&world, 1, (PilotInput){.cyan = true});
    CHECK(world.Fold.ends.at[2].open && world.Fold.ends.at[3].open);
    aim_at(&world, 1, (tide_float3){2, 1.55f, -14});
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    for (int step = 0; step < 120 && pilot(&world, 1).position.x < 5; step++) tide_world_tick(&world);
    walk_to(&world, 1, 7, 9);
    ticks(&world, 60);
    CHECK(world.Arena.solved && world.Arena.gate > 0.99f);
    walk_to(&world, 0, 0, 10);
    walk_to(&world, 0, 0, 16);
    CHECK(world.Arena.hold);
    walk_to(&world, 1, 8.3f, 9);
    aim_at(&world, 1, (tide_float3){11, 3.15f, 9});
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    for (int step = 0; step < 120 && pilot(&world, 1).position.x > 5; step++) tide_world_tick(&world);
    CHECK(pilot(&world, 1).position.z < -10 && !world.Arena.solved && world.Arena.gate > 0.99f);
    input(&world, 0, (PilotInput){.active = 1});
    ticks(&world, 20);
    walk_to(&world, 1, 0, -6);
    walk_to(&world, 1, 0, 10);
    walk_to(&world, 1, 1.2f, 15.5f);
    ticks(&world, 150);
    CHECK(world.Arena.level == 6 && world.Arena.depth == 2);
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

int main(void)
{
    ownership_and_movement();
    folds_are_shot_and_crossed();
    sizes();
    carrying_and_throwing();
    chambers_and_commands();
    between();
    stretches();
    shaft();
    sight();
    dark();
    undertow();
    chambers_hold();
    teaching_solutions();
    replay_and_pack_are_identical();
    puts("FOLDLINE simulation checks passed");
    return 0;
}
