#include "foldline_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FOLDLINE check failed: %s (%s:%d)\n", #value, __FILE__, __LINE__); \
    exit(1); } } while (0)

static const tide_float3 panels[6] = {
    {-9.8f, 1.5f, -6}, {9.8f, 1.5f, -6},
    {-9.8f, 1.5f, 10}, {9.8f, 1.5f, 10},
    {-5, 1.5f, 2.8f}, {5, 1.5f, 3.2f}
};
static const tide_float3 normals[6] = {
    {1, 0, 0}, {-1, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}
};

static tide_player_id player(int index) { return tide_player_from_index(index); }
static bool near(float a, float b) { return fabsf(a - b) < 0.002f; }

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

static void place(tide_world *world, int id, tide_float3 position, float yaw)
{
    tide_entity entity = id == 0 ? world->Arena.op0 : world->Arena.op1;
    Operator body = tide_get_Operator(world, entity);
    body.position = position;
    body.yaw = yaw;
    body.pitch = 0;
    body.velocity = (tide_float3){0};
    body.portalCooldown = 0;
    CHECK(tide_set_Operator(world, entity, body));
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
    input(&world, 0, (PilotInput){.active = 1, .move = {0, 1}});
    ticks(&world, 30);
    CHECK(near(tide_get_Operator(&world, world.Arena.op0).position.z, -9));
    CHECK(tide_get_Operator(&world, world.Arena.op1).position.z > -7);
    input(&world, 0, (PilotInput){0});
    tide_world_player_joined(&world, player(1));
    tide_world_tick(&world);
    CHECK(world.Arena.joined == 2);
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op1).player, player(1)));
    const float before = tide_get_Operator(&world, world.Arena.op1).position.x;
    input(&world, 1, (PilotInput){.active = 0, .move = {1, 0}});
    ticks(&world, 30);
    CHECK(tide_get_Operator(&world, world.Arena.op1).position.x > before + 2);
    tide_world_player_left(&world, player(0));
    tide_world_tick(&world);
    CHECK(world.Arena.joined == 1);
    CHECK(tide_player_equal(world.Arena.first, player(1)));
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op0).player, player(1)));
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Arena.op1).player, player(1)));
    tide_world_free(&world);
}

static void menus_gate_sampling_and_stop_momentum(void)
{
    tide_local local = {0};
    tide_devices devices = {0};
    tide_local_init(&local);
    local.Control.active = 1;
    tide_button_set(&devices.keyboard.w, true);
    tide_button_set(&devices.keyboard.space, true);
    tide_button_set(&devices.mouse.left, true);
    devices.mouse.locked = true;
    devices.mouse.delta = (tide_float2){20, 10};
    PilotInput controls = tide_input_sample(&devices, &local);
    CHECK(controls.active == 1 && controls.move.y == 0 && controls.look.x == 0);
    CHECK(!controls.jump && !controls.amber);
    local.Control.started = true;
    controls = tide_input_sample(&devices, &local);
    CHECK(controls.move.y == 1 && controls.look.x > 0.05f && controls.jump && controls.amber);
    tide_world world = {0};
    start(&world);
    input(&world, 0, controls);
    ticks(&world, 15);
    CHECK(tide_get_Operator(&world, world.Arena.op1).velocity.z > 3);
    local.Control.started = false;
    input(&world, 0, tide_input_sample(&devices, &local));
    ticks(&world, 40);
    CHECK(fabsf(tide_get_Operator(&world, world.Arena.op1).velocity.z) < 0.01f);
    CHECK(fabsf(tide_get_Operator(&world, world.Arena.op0).velocity.z) < 0.01f);
    tide_world_free(&world);
    tide_local_free(&local);
}

static void portals_rotate_and_cross(void)
{
    for (int source = 0; source < 6; source++) {
        for (int destination = 0; destination < 6; destination++) {
            if (source == destination) continue;
            tide_world world = {0};
            start(&world);
            world.Fold.a = source;
            world.Fold.b = destination;
            const tide_float3 n = normals[source];
            const tide_float3 dn = normals[destination];
            tide_float3 position = panels[source];
            position.y = 0;
            position.x += n.x * 0.04f;
            position.z += n.z * 0.04f;
            const float yaw = n.x > 0 ? -TIDE_PI_F / 2 : n.x < 0 ? TIDE_PI_F / 2 : n.z < 0 ? 0 : TIDE_PI_F;
            place(&world, 0, position, yaw);
            Operator body = tide_get_Operator(&world, world.Arena.op0);
            body.velocity = (tide_float3){-n.x * 5.2f, 0, -n.z * 5.2f};
            CHECK(tide_set_Operator(&world, world.Arena.op0, body));
            input(&world, 0, (PilotInput){.move = {0, 1}});
            tide_world_tick(&world);
            body = tide_get_Operator(&world, world.Arena.op0);
            CHECK(world.Fold.traversals == 1);
            CHECK(fabsf(body.position.x - panels[destination].x) < 0.5f);
            CHECK(fabsf(body.position.z - panels[destination].z) < 0.5f);
            CHECK(near(body.position.y, 0));
            CHECK(body.velocity.x * dn.x + body.velocity.z * dn.z > 5.1f);
            CHECK(tide_sin_f(body.yaw) * dn.x + tide_cos_f(body.yaw) * dn.z > 0.999f);
            // It comes out as far past the destination's face as it went past the source's: no step.
            const float out = (body.position.x - panels[destination].x) * dn.x + (body.position.z - panels[destination].z) * dn.z;
            CHECK(out > 0 && out < 0.06f);
            // Turning back goes back through the fold, or stays in the room: never behind a wall's face.
            input(&world, 0, (PilotInput){.move = {0, -1}});
            for (int step = 0; step < 12; step++) {
                tide_world_tick(&world);
                body = tide_get_Operator(&world, world.Arena.op0);
                CHECK(body.position.x >= -9.801f && body.position.x <= 9.801f);
                CHECK(body.position.z >= -12.7f && body.position.z <= 12.7f);
            }
            tide_world_free(&world);
        }
    }
    // Being beside a fold is ordinary solid wall, including the divider.
    tide_world world = {0};
    start(&world);
    place(&world, 0, (tide_float3){-7, 0, 2.4f}, 0);
    input(&world, 0, (PilotInput){.move = {0, 1}});
    ticks(&world, 20);
    CHECK(tide_get_Operator(&world, world.Arena.op0).position.z <= 2.541f);
    CHECK(world.Fold.traversals == 0);
    tide_world_free(&world);
    // Loose and carried cubes reach the same destination region as operators.
    start(&world);
    world.Fold.cube = (tide_float3){-5, 1.5f, 2.76f};
    world.Fold.cubeVelocity = (tide_float3){0, 0, 5.2f};
    tide_world_tick(&world);
    CHECK(world.Fold.traversals == 1 && near(world.Fold.cube.z, 10));
    CHECK(world.Fold.cubeVelocity.x > 5.1f);
    tide_world_free(&world);
    start(&world);
    place(&world, 0, (tide_float3){-5, 0, 2.76f}, 0);
    Operator body = tide_get_Operator(&world, world.Arena.op0);
    body.velocity.z = 5.2f;
    CHECK(tide_set_Operator(&world, world.Arena.op0, body));
    world.Fold.carried = 0;
    input(&world, 0, (PilotInput){.move = {0, 1}});
    tide_world_tick(&world);
    CHECK(world.Fold.traversals == 1 && world.Fold.carried == 0 && near(world.Fold.cube.z, 10));
    CHECK(world.Fold.cube.x > -9);
    tide_world_free(&world);
}

static void aiming_carry_and_jump(void)
{
    tide_world world = {0};
    start(&world);
    world.Fold.a = 0;
    place(&world, 0, (tide_float3){-5, 0, 0}, 0);
    input(&world, 0, (PilotInput){.amber = true, .jump = true, .ping = true});
    tide_world_tick(&world);
    CHECK(world.Fold.a == 4);
    CHECK(world.Fold.pingTime > 2.9f);
    CHECK(tide_get_Operator(&world, world.Arena.op0).position.y > 0.08f);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 70);
    CHECK(near(tide_get_Operator(&world, world.Arena.op0).position.y, 0));
    place(&world, 0, (tide_float3){-3, 0, -7.5f}, 0);
    input(&world, 0, (PilotInput){.carry = true});
    tide_world_tick(&world);
    CHECK(world.Fold.carried == 0);
    ticks(&world, 3);
    CHECK(world.Fold.carried == 0); // A held press never toggles repeatedly.
    place(&world, 0, (tide_float3){5, 0, -3.25f}, 0);
    input(&world, 0, (PilotInput){0});
    tide_world_tick(&world);
    input(&world, 0, (PilotInput){.carry = true});
    tide_world_tick(&world);
    CHECK(world.Fold.carried == -1);
    input(&world, 0, (PilotInput){0});
    ticks(&world, 70);
    CHECK(world.Arena.plateA && world.Arena.solved && world.Arena.gate > 0.99f);
    input(&world, 0, (PilotInput){.reset = true});
    tide_world_tick(&world);
    CHECK(!world.Arena.solved && !world.Arena.complete && near(world.Arena.gate, 0));
    CHECK(near(tide_get_Operator(&world, world.Arena.op0).position.z, -9));
    CHECK(world.Fold.carried == -1);
    tide_world_free(&world);
}

static void chamber_conditions_and_commands(void)
{
    tide_world world = {0};
    start(&world);
    choose(&world, 1, 2); // A valid player ID which has not joined has no authority.
    CHECK(world.Arena.level == 0);
    choose(&world, 0, -1);
    choose(&world, 0, 3);
    CHECK(world.Arena.level == 0);
    choose(&world, 0, 1);
    CHECK(world.Arena.level == 1);
    world.Fold.cube = (tide_float3){5, 0.5f, -4};
    tide_world_tick(&world);
    CHECK(world.Arena.plateA && !world.Arena.plateB && !world.Arena.solved);
    place(&world, 1, (tide_float3){-5, 0, 10}, 0);
    tide_world_tick(&world);
    CHECK(world.Arena.plateB && world.Arena.solved);
    world.Fold.cube = (tide_float3){0, 0.5f, -4};
    place(&world, 1, (tide_float3){0, 0, 10}, 0);
    ticks(&world, 60);
    CHECK(world.Arena.solved && world.Arena.gate > 0.99f); // Resonance stays latched.
    choose(&world, 0, 2);
    CHECK(world.Arena.level == 2 && !world.Arena.bridge && !world.Arena.solved);
    world.Fold.cube = (tide_float3){5, 0.5f, 10.5f};
    place(&world, 0, (tide_float3){-5, 0, -2}, 0);
    tide_world_tick(&world);
    CHECK(world.Arena.plateA && world.Arena.plateB && !world.Arena.solved && !world.Arena.bridge);
    world.Fold.cube = (tide_float3){-5, 0.5f, -2};
    place(&world, 0, (tide_float3){5, 0, 10.5f}, 0);
    tide_world_tick(&world);
    CHECK(world.Arena.solved && world.Arena.bridge);
    place(&world, 0, (tide_float3){-1, 0, 11.5f}, 0);
    tide_world_tick(&world);
    CHECK(!world.Arena.complete);
    place(&world, 1, (tide_float3){1, 0, 11.5f}, 0);
    tide_world_tick(&world);
    CHECK(world.Arena.complete);
    choose(&world, 0, 2);
    place(&world, 0, (tide_float3){0, 0, 7}, 0);
    ticks(&world, 60);
    CHECK(near(tide_get_Operator(&world, world.Arena.op0).position.z, -9));
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
    menus_gate_sampling_and_stop_momentum();
    portals_rotate_and_cross();
    aiming_carry_and_jump();
    chamber_conditions_and_commands();
    replay_and_pack_are_identical();
    puts("FOLDLINE simulation checks passed");
    return 0;
}
