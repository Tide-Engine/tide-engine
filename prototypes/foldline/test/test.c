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
    CHECK(level >= -1 && level <= 5);
    uint8_t payload[11] = {0};
    payload[0] = (uint8_t)TIDE_COMMAND_ID_SelectChamber;
    payload[1] = (uint8_t)(TIDE_COMMAND_ID_SelectChamber >> 8);
    payload[10] = (uint8_t)(level < 0 ? -level * 2 - 1 : level * 2);
    const uint32_t sequence = tide_game_api.command_sequence(world, player(sender), 0) + 1;
    CHECK(tide_game_api.command(world, player(sender), 0, sequence, payload, sizeof payload));
    tide_world_tick(world);
}

// The second chamber, whose floor is whole: an end on the south wall before an
// operator at (0, -8) who looks south, and one on the west wall when they look west.
static const float SOUTH = TIDE_PI_F, WEST = -TIDE_PI_F / 2;

static void open_floor(tide_world *world)
{
    start(world);
    choose(world, 0, 1);
    ticks(world, 8);
    CHECK(world->Arena.level == 1);
}

static void ownership_and_movement(void)
{
    tide_world world = {0};
    start(&world);
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op0).player, player(0)));
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op1).player, player(0)));
    ticks(&world, 30);
    // Standing on the floor, which is Box3D's
    CHECK(near(pilot(&world, 0).position.y, 0, 0.03f) && pilot(&world, 0).grounded);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 60);
    CHECK(pilot(&world, 0).position.z > -8);
    CHECK(near(pilot(&world, 1).position.z, -10.5f, 0.05f)); // Alone, the other body stays where it's left
    input(&world, 0, (PilotInput){.jump = true});
    ticks(&world, 8);
    CHECK(pilot(&world, 0).position.y > 0.3f);
    // The first chamber's pit has no bottom: whoever walks into it is back at the start
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 300);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 60);
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
    CHECK(!world.Fold.amber.open && world.Fold.fizzleTime > 0 && world.Fold.fizzleAmber);
    // Nor do the cage's thin walls
    place(&world, 0, (tide_float3){-3, 0, 3.5f}, WEST, 0);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.amber.open && world.Fold.fizzleTime > 0);
    // The south wall takes one, standing on the floor where it was aimed near it
    place(&world, 0, (tide_float3){0, 0, -8}, SOUTH, 0);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.amber.open);
    CHECK(near(world.Fold.amber.at.x, 0, 0.01f) && near(world.Fold.amber.at.z, -13, 0.001f) && near(world.Fold.amber.at.y, 1.3f, 0.001f));
    CHECK(world.Fold.amber.face.z > 0.99f);
    // One end alone is still a wall
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 120);
    CHECK(world.Fold.traversals == 0 && pilot(&world, 0).position.z > -13);
    // The other end, on the west wall. Not while someone stands in an end's mouth, nor for a moment after
    input(&world, 0, (PilotInput){0});
    tide_world_tick(&world);
    press(&world, (PilotInput){.cyan = true});
    CHECK(!world.Fold.cyan.open);
    place(&world, 0, (tide_float3){0, 0, -8}, WEST, 0);
    ticks(&world, 6);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.cyan.open && world.Fold.cyan.face.x > 0.99f);
    CHECK(near(world.Fold.cyan.at.x, -9.8f, 0.001f) && near(world.Fold.cyan.at.z, -8, 0.01f));
    // An end can't go where it would overlap the other
    press(&world, (PilotInput){.amber = true});
    CHECK(near(world.Fold.amber.at.z, -13, 0.001f) && world.Fold.fizzleTime > 0);
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
    CHECK(world.Fold.carried == 0);
    // ...and it comes back through the fold with the hand that holds it
    input(&world, 0, (PilotInput){.move = {0, -1}});
    ticks(&world, 30);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    CHECK(world.Fold.carried == 0 && world.Fold.cube.z > -13 && world.Fold.cube.z < -8.5f && near(world.Fold.cube.x, 0, 0.6f));
    // A click with it in hand throws it, and shoots no fold
    const tide_float3 amber = world.Fold.amber.at;
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.carried == -1 && world.Fold.amber.at.x == amber.x && world.Fold.amber.at.z == amber.z && world.Fold.fizzleTime <= 0);
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
    place(&world, 0, (tide_float3){0, 0, -8}, WEST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.cyan = true, .size = 1});
    CHECK(world.Fold.amber.open && world.Fold.amber.size == 1 && world.Fold.cyan.open && world.Fold.cyan.size == 0.5f);
    CHECK(near(world.Fold.cyan.at.y, 0.65f, 0.001f)); // Half as high, standing on the floor
    // In the whole end, out of the half one at half the size, on the floor
    place(&world, 0, (tide_float3){0, 0, -11}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 0; step++) tide_world_tick(&world);
    CHECK(world.Fold.traversals == 1);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    const Operator small = pilot(&world, 0);
    CHECK(small.scale == 0.5f && near(small.position.y, 0, 0.05f) && small.position.x > -9.8f && small.grounded);
    CHECK(near(tide_get_Box3D_Capsule(&world, world.Arena.op0).height, 0.9f, 0.001f));
    // Small, it goes back in the half end and comes out whole
    place(&world, 0, (tide_float3){-8.6f, 0, -8}, WEST, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 120 && world.Fold.traversals == 1; step++) tide_world_tick(&world);
    CHECK(world.Fold.traversals == 2);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    const Operator whole = pilot(&world, 0);
    CHECK(whole.scale == 1 && near(whole.position.y, 0, 0.05f) && whole.position.z > -13);
    // A whole body doesn't fit the half end
    place(&world, 0, (tide_float3){-8.6f, 0, -8}, WEST, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 90);
    CHECK(world.Fold.traversals == 2 && pilot(&world, 0).position.x > -9.8f);
    // A new chamber makes everyone whole again
    place(&world, 0, (tide_float3){0, 0, -11}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 2; step++) tide_world_tick(&world);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 5);
    CHECK(pilot(&world, 0).scale == 0.5f);
    choose(&world, 0, 2);
    ticks(&world, 10);
    CHECK(pilot(&world, 0).scale == 1 && near(pilot(&world, 0).position.z, -10.5f, 0.05f) && near(pilot(&world, 0).position.y, 0, 0.05f));
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
    CHECK(world.Fold.carried == 0);
    ticks(&world, 40);
    // Held up before its holder
    CHECK(world.Fold.cube.y > 0.6f && near(world.Fold.cube.x, 0, 0.3f));
    press(&world, (PilotInput){.hurl = true});
    CHECK(world.Fold.carried == -1);
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
    // The cube on the plate opens the first chamber's gate
    put_cube(&world, (tide_float3){5, 0.5f, 8}, (tide_float3){0});
    ticks(&world, 90);
    CHECK(world.Arena.plateA && world.Arena.solved && world.Arena.gate > 0.99f);
    // ...and only while it stays there
    put_cube(&world, (tide_float3){0, 0.5f, 6}, (tide_float3){0});
    ticks(&world, 60);
    CHECK(!world.Arena.plateA && !world.Arena.solved && world.Arena.gate < 0.01f);
    // Both operators in the lobby past the gate end the chamber
    place(&world, 0, (tide_float3){-1, 0, 15.5f}, 0, 0);
    place(&world, 1, (tide_float3){1, 0, 15.5f}, 0, 0);
    ticks(&world, 4);
    CHECK(world.Arena.complete);
    const int32_t built = world.Arena.chamber;
    choose(&world, 0, 4);
    CHECK(world.Arena.level == 4 && world.Arena.chamber == built + 1 && !world.Arena.solved && !world.Arena.complete);
    ticks(&world, 20);
    CHECK(near(pilot(&world, 0).position.z, -10.5f, 0.05f) && near(pilot(&world, 0).position.y, 0, 0.05f));
    CHECK(near(world.Fold.cube.z, -11, 0.4f) && near(world.Fold.cube.x, -7, 0.4f) && !world.Fold.amber.open);
    // A chamber that isn't one changes nothing
    choose(&world, 0, 5);
    CHECK(world.Arena.level == 4);
    tide_world_free(&world);
}

// What each chamber rests on: who's the size to get where.
static void chambers_hold(void)
{
    tide_world world = {0};
    // The vent is too low for a whole operator, and takes a half one
    start(&world);
    choose(&world, 0, 2);
    ticks(&world, 8);
    place(&world, 0, (tide_float3){5.5f, 0, 2.5f}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 90);
    CHECK(pilot(&world, 0).position.z < 4.6f);
    tide_world_free(&world);
    // The fourth chamber's pit is past a whole operator's jump, and its side walls take no fold
    start(&world);
    choose(&world, 0, 3);
    ticks(&world, 8);
    place(&world, 0, (tide_float3){0, 0, -6}, WEST, 0);
    ticks(&world, 2);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.amber.open && world.Fold.fizzleTime > 0);
    place(&world, 0, (tide_float3){0, 0, -6}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 200; step++) {
        input(&world, 0, (PilotInput){.move = {0, 1}, .jump = pilot(&world, 0).position.z > -1.9f});
        tide_world_tick(&world);
        CHECK(!(pilot(&world, 0).position.z > 3.5f && pilot(&world, 0).position.y > -0.2f));
    }
    // ...but not past the jump of one who came out of a larger end
    place(&world, 0, (tide_float3){-4, 0, -8}, SOUTH, 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    place(&world, 0, (tide_float3){4, 0, -8}, SOUTH, 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.cyan = true, .size = 2});
    CHECK(world.Fold.amber.open && world.Fold.cyan.open && world.Fold.cyan.size == 1.5f);
    place(&world, 0, (tide_float3){-4, 0, -11}, SOUTH, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    for (int step = 0; step < 90 && world.Fold.traversals == 0; step++) tide_world_tick(&world);
    CHECK(world.Fold.traversals == 1);
    bool over = false;
    for (int step = 0; step < 240 && !over; step++) {
        input(&world, 0, (PilotInput){.move = {0, 1}, .jump = pilot(&world, 0).position.z > -2.4f && pilot(&world, 0).position.z < -1.5f});
        tide_world_tick(&world);
        over = pilot(&world, 0).position.z > 3.8f && pilot(&world, 0).grounded;
    }
    CHECK(pilot(&world, 0).scale == 1.5f && over);
    // The slab past the pit takes a fold on the side that faces away from it
    place(&world, 0, (tide_float3){-5.5f, 0, 12.5f}, SOUTH, 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 4);
    press(&world, (PilotInput){.amber = true});
    CHECK(near(world.Fold.amber.at.z, 10.5f, 0.001f) && world.Fold.amber.face.z > 0.99f);
    tide_world_free(&world);
    // The cage's plate is reached by a fold shot through its window onto the wall inside
    start(&world);
    choose(&world, 0, 1);
    ticks(&world, 8);
    place(&world, 0, (tide_float3){-2.6f, 0, 3.5f}, WEST, 0.3f);
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.amber.open && near(world.Fold.amber.at.x, -9.8f, 0.001f) && world.Fold.amber.at.z > 2 && world.Fold.amber.at.z < 5);
    tide_world_free(&world);
    // A half operator goes through the vent, round the baffle and into the room
    start(&world);
    choose(&world, 0, 2);
    ticks(&world, 8);
    place(&world, 0, (tide_float3){0, 0, -8}, SOUTH, 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.amber = true});
    place(&world, 0, (tide_float3){0, 0, -8}, WEST, 0);
    ticks(&world, 3);
    press(&world, (PilotInput){.cyan = true, .size = 1});
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
    chambers_hold();
    replay_and_pack_are_identical();
    puts("FOLDLINE simulation checks passed");
    return 0;
}
