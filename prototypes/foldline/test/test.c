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
    body.position = (tide_float3){feet.x, feet.y + 0.9f, feet.z};
    body.velocity = (tide_float3){0};
    seen.position = feet;
    seen.last = body.position;
    seen.yaw = yaw;
    seen.pitch = pitch;
    CHECK(tide_set_Operator(world, entity, seen));
    CHECK(tide_set_Box3D_Body(world, entity, body));
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
    CHECK(level >= -1 && level <= 3);
    uint8_t payload[11] = {0};
    payload[0] = (uint8_t)TIDE_COMMAND_ID_SelectChamber;
    payload[1] = (uint8_t)(TIDE_COMMAND_ID_SelectChamber >> 8);
    payload[10] = (uint8_t)(level < 0 ? -level * 2 - 1 : level * 2);
    const uint32_t sequence = tide_game_api.command_sequence(world, player(sender), 0) + 1;
    CHECK(tide_game_api.command(world, player(sender), 0, sequence, payload, sizeof payload));
    tide_world_tick(world);
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
    CHECK(pilot(&world, 0).position.z > -6.5f);
    CHECK(near(pilot(&world, 1).position.z, -9, 0.05f)); // Alone, the other body stays where it's left
    // The gate in the doorway stops a walk
    ticks(&world, 400);
    CHECK(pilot(&world, 0).position.z < 2.7f && pilot(&world, 0).position.z > 2.3f);
    input(&world, 0, (PilotInput){.jump = true});
    ticks(&world, 8);
    CHECK(pilot(&world, 0).position.y > 0.3f);
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
    ticks(&world, 5);
    // A shot at the ceiling, and one at the lobby's concrete past nothing: neither takes a fold
    place(&world, 0, (tide_float3){-5, 0, -2}, 0, 1.3f);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(!world.Fold.amber.open && world.Fold.fizzleTime > 0 && world.Fold.fizzleAmber);
    // The divider takes one, standing on the floor where it was aimed near it
    place(&world, 0, (tide_float3){-5, 0, -2}, 0, 0);
    tide_world_tick(&world);
    press(&world, (PilotInput){.amber = true});
    CHECK(world.Fold.amber.open);
    CHECK(near(world.Fold.amber.at.x, -5, 0.01f) && near(world.Fold.amber.at.z, 2.4f, 0.001f) && near(world.Fold.amber.at.y, 1.3f, 0.001f));
    CHECK(world.Fold.amber.face.z < -0.99f);
    // One end alone is still a wall
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 120);
    CHECK(world.Fold.traversals == 0 && pilot(&world, 0).position.z < 2.4f);
    // The other end, on the side wall. Not while someone stands in an end's mouth, nor for a moment after
    input(&world, 0, (PilotInput){0});
    tide_world_tick(&world);
    press(&world, (PilotInput){.cyan = true});
    CHECK(!world.Fold.cyan.open);
    place(&world, 0, (tide_float3){-5, 0, -2}, -TIDE_PI_F / 2, 0);
    ticks(&world, 6);
    press(&world, (PilotInput){.cyan = true});
    CHECK(world.Fold.cyan.open && world.Fold.cyan.face.x > 0.99f);
    CHECK(near(world.Fold.cyan.at.x, -9.8f, 0.001f) && near(world.Fold.cyan.at.z, -2, 0.01f));
    // An end can't go where it would overlap the other
    press(&world, (PilotInput){.amber = true});
    CHECK(near(world.Fold.amber.at.z, 2.4f, 0.001f) && world.Fold.fizzleTime > 0);
    ticks(&world, 3);
    // Walking into one comes out of the other, turned, with its speed, and never behind a wall's face
    place(&world, 0, (tide_float3){-5, 0, 0.5f}, 0, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    bool crossed = false;
    for (int step = 0; step < 90 && !crossed; step++) {
        tide_world_tick(&world);
        const Operator now = pilot(&world, 0);
        CHECK(now.position.x > -9.9f && now.position.z < 3.4f);
        crossed = world.Fold.traversals == 1;
    }
    CHECK(crossed);
    Operator out = pilot(&world, 0);
    CHECK(out.position.x > -9.8f && out.position.x < -9.6f && near(out.position.z, -2, 0.3f));
    CHECK(out.velocity.x > 3 && tide_sin_f(out.yaw) > 0.99f);
    ticks(&world, 30);
    CHECK(pilot(&world, 0).position.x > -8.5f && world.Fold.traversals == 1);
    // A cube thrown at one flies out of the other
    Box3D_Body cube = tide_get_Box3D_Body(&world, world.Arena.cube);
    cube.position = (tide_float3){-5, 1.2f, 0.5f};
    cube.velocity = (tide_float3){0, 0, 7};
    CHECK(tide_set_Box3D_Body(&world, world.Arena.cube, cube));
    Cube marks = tide_get_Cube(&world, world.Arena.cube);
    marks.last = cube.position;
    CHECK(tide_set_Cube(&world, world.Arena.cube, marks));
    place(&world, 0, (tide_float3){4, 0, -9}, 0, 0);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 40);
    CHECK(world.Fold.traversals == 2);
    CHECK(world.Fold.cube.x > -9.8f && world.Fold.cube.x < 0 && world.Fold.cube.z < 0);
    tide_world_free(&world);
}

static void carrying_and_throwing(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 30);
    place(&world, 0, (tide_float3){-3, 0, -8.3f}, 0, -0.3f);
    tide_world_tick(&world);
    press(&world, (PilotInput){.carry = true});
    CHECK(world.Fold.carried == 0);
    ticks(&world, 40);
    // Held up before its holder
    CHECK(world.Fold.cube.y > 0.6f && near(world.Fold.cube.x, -3, 0.3f));
    press(&world, (PilotInput){.hurl = true});
    CHECK(world.Fold.carried == -1);
    CHECK(tide_get_Box3D_Body(&world, world.Arena.cube).velocity.z > 5);
    ticks(&world, 60);
    CHECK(world.Fold.cube.z > -5);
    tide_world_free(&world);
}

static void chambers_and_commands(void)
{
    tide_world world = {0};
    start(&world);
    ticks(&world, 10);
    // The cube on plate A opens the first chamber's gate
    Box3D_Body cube = tide_get_Box3D_Body(&world, world.Arena.cube);
    cube.position = (tide_float3){5, 0.5f, -2};
    CHECK(tide_set_Box3D_Body(&world, world.Arena.cube, cube));
    ticks(&world, 90);
    CHECK(world.Arena.plateA && world.Arena.solved && world.Arena.gate > 0.99f);
    const int32_t built = world.Arena.chamber;
    choose(&world, 0, 2);
    CHECK(world.Arena.level == 2 && world.Arena.chamber == built + 1 && !world.Arena.solved);
    ticks(&world, 20);
    CHECK(near(pilot(&world, 0).position.z, -9, 0.05f) && near(pilot(&world, 0).position.y, 0, 0.05f));
    CHECK(near(world.Fold.cube.z, -8, 0.1f) && !world.Fold.amber.open);
    // A chamber that isn't one, and a sender who isn't in the match, change nothing
    choose(&world, 0, 3);
    CHECK(world.Arena.level == 2);
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
    carrying_and_throwing();
    chambers_and_commands();
    replay_and_pack_are_identical();
    puts("FOLDLINE simulation checks passed");
    return 0;
}
