#include "deadweight_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define CHECK(v) do { if (!(v)) { fprintf(stderr, "DEADWEIGHT failed: %s at %d\n", #v, __LINE__); exit(1); } } while (0)

static tide_player_id player(int seat) { return tide_player_from_index(seat); }
static void input(tide_world *world, Controls controls)
{
    controls.shared = true;
    tide_world_set_input(world, player(0), controls);
    tide_world_set_server_input(world, controls);
}
static void ticks(tide_world *world, int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(world);
}
static void sampled_keys(void)
{
    tide_local local = {0};
    tide_devices devices = {0};
    tide_local_init(&local);
    local.Hands.playing = true;
    local.Hands.shared = true;
    devices.keyboard.w.pressed = true;
    devices.keyboard.e.pressed = true;
    devices.keyboard.q.pressed = true;
    devices.keyboard.upArrow.pressed = true;
    devices.keyboard.rightShift.pressed = true;
    devices.keyboard.comma.pressed = true;
    Controls controls = tide_input_sample(&devices, &local);
    CHECK(controls.shared && controls.use && controls.secondUse && controls.reel && controls.secondReel);
    CHECK(controls.move.y == 1 && controls.secondMove.y == 1);
    local.Hands.playing = false;
    controls = tide_input_sample(&devices, &local);
    CHECK(!controls.use && !controls.secondUse && !controls.reel && !controls.secondReel);
    tide_local_free(&local);
}
static void choose(tide_world *world, int sender, int job);
static void start(tide_world *world, int job)
{
    tide_world_init(world, 1.0f / 60);
    tide_world_player_joined(world, player(0));
    ticks(world, 5);
    CHECK(world->Site.joined == 1);
    if (job != 0) choose(world, 0, job);
}
static void bridge_walk(void)
{
    tide_world world = {0};
    start(&world, 0);
    input(&world, (Controls){.reel = true, .secondReel = true});
    ticks(&world, 215);
    input(&world, (Controls){0});
    ticks(&world, 180);
    CHECK(world.Site.bridge);
    Box3D_Body cargo = tide_get_Box3D_Body(&world, world.Site.cargo.at[0]);
    Box3D_Body crew = tide_get_Box3D_Body(&world, world.Site.crew.at[0]);
    tide_float3 rear = tide_rotate_q(cargo.rotation, (tide_float3){0, 0, -4.9f});
    crew.position = (tide_float3){cargo.position.x + rear.x, 0.77f, cargo.position.z + rear.z};
    crew.velocity = (tide_float3){0};
    CHECK(tide_set_Box3D_Body(&world, world.Site.crew.at[0], crew));
    int supported = 0;
    for (int i = 0; i < 125; i++) {
        cargo = tide_get_Box3D_Body(&world, world.Site.cargo.at[0]);
        crew = tide_get_Box3D_Body(&world, world.Site.crew.at[0]);
        tide_float3 front = tide_rotate_q(cargo.rotation, (tide_float3){0, 0, 4.6f});
        tide_float2 way = tide_normalize_f2((tide_float2){cargo.position.x + front.x - crew.position.x, cargo.position.z + front.z - crew.position.z});
        input(&world, (Controls){.move = {way.x * 0.94f + way.y * 0.342f, -way.x * 0.342f + way.y * 0.94f}});
        tide_world_tick(&world);
        if (crew.position.z > -1 && crew.position.z < 3 && crew.position.y > 1.1f) supported++;
    }
    crew = tide_get_Box3D_Body(&world, world.Site.crew.at[0]);
    CHECK(crew.position.z > 3.8f && world.Site.mishaps == 0 && supported > 40);
    tide_world_free(&world);
}
static void choose(tide_world *world, int sender, int job)
{
    uint8_t payload[11] = {0};
    payload[0] = (uint8_t)TIDE_COMMAND_ID_ChooseJob;
    payload[1] = (uint8_t)(TIDE_COMMAND_ID_ChooseJob >> 8);
    payload[10] = (uint8_t)(job < 0 ? -job * 2 - 1 : job * 2);
    uint32_t sequence = tide_game_api.command_sequence(world, player(sender), 0) + 1;
    CHECK(tide_game_api.command(world, player(sender), 0, sequence, payload, sizeof payload));
    ticks(world, 5);
}
static void press(tide_world *world, Controls controls)
{
    input(world, controls);
    ticks(world, 1);
    input(world, (Controls){0});
    ticks(world, 1);
}
static void rigging_and_ownership(void)
{
    tide_world world = {0};
    start(&world, 0);
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Site.crew.at[1]).player, player(0)));
    press(&world, (Controls){.release = true});
    CHECK(tide_get_Worker(&world, world.Site.crew.at[0]).hook);
    CHECK(tide_entity_equal(world.Site.cables.at[0].target, (tide_entity){0}));
    Box3D_Body cargo = tide_get_Box3D_Body(&world, world.Site.cargo.at[0]);
    tide_float3 point = tide_rotate_q(cargo.rotation, (tide_float3){-0.64f, 0.28f, 3.85f});
    Box3D_Body crew = tide_get_Box3D_Body(&world, world.Site.crew.at[0]);
    crew.position = (tide_float3){cargo.position.x + point.x, 0.77f, cargo.position.z + point.z + 1.6f};
    crew.velocity = (tide_float3){0};
    CHECK(tide_set_Box3D_Body(&world, world.Site.crew.at[0], crew));
    press(&world, (Controls){.use = true});
    CHECK(!tide_get_Worker(&world, world.Site.crew.at[0]).hook);
    CHECK(tide_entity_equal(world.Site.cables.at[0].target, world.Site.cargo.at[0]));
    CHECK(world.Site.cables.at[0].socket == 0);
    const float before = world.Site.cables.at[0].length;
    ticks(&world, 30);
    CHECK(world.Site.cables.at[0].length == before);
    tide_world_player_joined(&world, player(1));
    ticks(&world, 2);
    CHECK(world.Site.joined == 2);
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Site.crew.at[1]).player, player(1)));
    tide_world_set_input(&world, player(0), (Controls){.reel = true});
    tide_world_set_input(&world, player(1), (Controls){.payout = true});
    ticks(&world, 15);
    CHECK(world.Site.cables.at[0].length < before - 0.3f);
    CHECK(world.Site.cables.at[1].length > 19.2f);
    tide_world_player_left(&world, player(0));
    ticks(&world, 2);
    CHECK(world.Site.joined == 1);
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Site.crew.at[0]).player, player(1)));
    CHECK(tide_player_equal(tide_get_Owner(&world, world.Site.crew.at[1]).player, player(1)));
    tide_world_free(&world);
}
static void recovery_and_commands(void)
{
    tide_world world = {0};
    start(&world, 0);
    choose(&world, 1, 2);
    CHECK(world.Site.job == 0 && world.Site.revision == 1);
    choose(&world, 0, 9);
    CHECK(world.Site.job == 0);
    choose(&world, 0, 1);
    CHECK(world.Site.job == 1 && world.Site.revision == 2);
    choose(&world, 0, 0);
    CHECK(world.Site.job == 0 && world.Site.revision == 3);
    const tide_entity old = world.Site.cargo.at[0];
    choose(&world, 0, 2);
    CHECK(!tide_has_Cargo(&world, old));
    CHECK(tide_get_Worker(&world, world.Site.crew.at[0]).winch == 4);
    Box3D_Body worker = tide_get_Box3D_Body(&world, world.Site.crew.at[0]);
    worker.position.y = -4;
    CHECK(tide_set_Box3D_Body(&world, world.Site.crew.at[0], worker));
    ticks(&world, 1);
    CHECK(world.Site.mishaps == 1);
    CHECK(tide_get_Box3D_Body(&world, world.Site.crew.at[0]).position.y > 0.6f);
    Box3D_Body vessel = tide_get_Box3D_Body(&world, world.Site.cargo.at[0]);
    vessel.position.y = -5;
    CHECK(tide_set_Box3D_Body(&world, world.Site.cargo.at[0], vessel));
    ticks(&world, 1);
    CHECK(world.Site.mishaps == 2);
    CHECK(tide_get_Box3D_Body(&world, world.Site.cargo.at[0]).position.y > 2);
    input(&world, (Controls){.active = 99, .move = {NAN, INFINITY}, .secondMove = {INFINITY, NAN}});
    ticks(&world, 5);
    worker = tide_get_Box3D_Body(&world, world.Site.crew.at[0]);
    CHECK(isfinite(worker.position.x) && isfinite(worker.position.y) && isfinite(worker.position.z));
    tide_world_free(&world);
}
static void replay_and_pack(void)
{
    tide_world world = {0}, snapshot = {0}, restored = {0};
    start(&world, 1);
    input(&world, (Controls){.payout = true, .secondReel = true});
    ticks(&world, 180);
    tide_world_copy(&snapshot, &world);
    ticks(&world, 150);
    const uint64_t expected = tide_world_hash(&world);
    tide_world_copy(&world, &snapshot);
    ticks(&world, 150);
    CHECK(tide_world_hash(&world) == expected);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    CHECK(bytes && tide_world_pack(&world, bytes, size) == size);
    CHECK(tide_world_unpack(&restored, bytes, size));
    CHECK(tide_world_hash(&restored) == expected);
    ticks(&world, 100);
    ticks(&restored, 100);
    CHECK(tide_world_hash(&world) == tide_world_hash(&restored));
    free(bytes);
    tide_world_free(&restored);
    tide_world_free(&snapshot);
    tide_world_free(&world);
}
int main(void)
{
    for (int job = 0; job < 3; job++)
    {
        tide_world world = {0};
        start(&world, job);
        for (int tick = 0; tick < 1500; tick++)
        {
            Controls controls = {0};
            if (job == 0)
            {
                controls.reel = world.Site.cables.at[0].length > 3.95f;
                controls.secondReel = world.Site.cables.at[1].length > 3.95f;
            }
            else if (job == 1)
            {
                controls.payout = !world.Site.delivered && world.Site.cables.at[0].length < 6.7f;
                controls.reel = world.Site.delivered && world.Site.cables.at[0].length > 2.35f;
                controls.secondReel = world.Site.cables.at[1].length > 4.8f;
            }
            else
            {
                controls.reel = world.Site.cables.at[4].length > 9.0f;
                controls.secondReel = world.Site.cables.at[1].length > 4.1f;
            }
            input(&world, controls);
            tide_world_tick(&world);
            const Box3D_Body load = tide_get_Box3D_Body(&world, world.Site.cargo.at[0]);
            CHECK(isfinite(load.position.x) && isfinite(load.position.y) && isfinite(load.position.z));
            CHECK(tide_lengthsq_f3(load.velocity) < 2000);
        }
        CHECK(world.Site.complete && world.Site.delivered && world.Site.completed == (1 << job));
        CHECK(world.Site.mishaps == 0);
        if (job == 0) CHECK(world.Site.bridge);
        if (job == 1) CHECK(tide_get_Box3D_Body(&world, world.Site.cargo.at[1]).position.y > 4.6f);
        if (job == 2) CHECK(fabsf(tide_up_q(tide_get_Box3D_Body(&world, world.Site.cargo.at[0]).rotation).y) < 0.05f);
        const uint64_t expected[] = {UINT64_C(0x4ed5916f3c6a6094), UINT64_C(0xa1fa0ec602478a72), UINT64_C(0x2d739a1276e8e402)};
        CHECK(tide_world_hash(&world) == expected[job]);
        printf("DEADWEIGHT job %d complete, hash %016llx\n", job + 1, (unsigned long long)tide_world_hash(&world));
        tide_world_free(&world);
    }
    bridge_walk();
    sampled_keys();
    rigging_and_ownership();
    recovery_and_commands();
    replay_and_pack();
    puts("DEADWEIGHT simulation checks passed");
    return 0;
}
