#ifndef FOLDLINE_TEST_HELPERS_H
#define FOLDLINE_TEST_HELPERS_H

#include "foldline_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FOLDLINE check failed: %s (%s:%d)\n", #value, __FILE__, __LINE__); \
    exit(1); } } while (0)

static tide_player_id player(int index) { return tide_player_from_index(index); }
static bool near(float a, float b, float within) { return fabsf(a - b) < within; }

// The places, as places.tide numbers them.
enum { BENCH = 0, LANDING = 1, RECEPTION = 2, MAILROOM = 3, LAMPS_OUT = 4, SHOWROOM = 5,
       WEIGHBRIDGE = 6, THE_SLOT = 7, CRAWLSPACE = 8, LONG_THROW = 9, THE_WARD = 10,
       ARCHIVE = 11, EXCHANGE = 12, RED_STAIR = 13, ATRIUM = 14, RECEPTION_AGAIN = 15, PLACES = 16 };

static void choose(tide_world *world, int sender, int id);

static void start(tide_world *world)
{
    tide_world_init(world, 1.0f / 60.0f);
    tide_world_player_joined(world, player(0));
    tide_world_tick(world);
    CHECK(world->Arena.joined == 1);
    CHECK(tide_has_Operator(world, world->Arena.op0));
    CHECK(tide_has_Operator(world, world->Arena.op1));
    choose(world, 0, BENCH);
    tide_world_tick(world);
    tide_world_tick(world);
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
    cube.angularVelocity = (tide_float3){0};
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
    input(world, 0, (PilotInput){.active = controls.active});
    tide_world_tick(world);
}

static void choose(tide_world *world, const int sender, const int id)
{
    // The game's only command: generated wire ID, null entity target, zigzag int.
    CHECK(id >= -1 && id <= PLACES);
    uint8_t payload[11] = {0};
    payload[0] = (uint8_t)TIDE_COMMAND_ID_SelectPlace;
    payload[1] = (uint8_t)(TIDE_COMMAND_ID_SelectPlace >> 8);
    payload[10] = (uint8_t)(id < 0 ? -id * 2 - 1 : id * 2);
    const uint32_t sequence = tide_game_api.command_sequence(world, player(sender), 0) + 1;
    CHECK(tide_game_api.command(world, player(sender), 0, sequence, payload, sizeof payload));
    tide_world_tick(world);
}

// Yaws, as play.tide's NORTH to WEST: yaw 0 looks along +z.
static const float NORTH = 0, SOUTH = TIDE_PI_F, WEST = -TIDE_PI_F / 2, EAST = TIDE_PI_F / 2;

// A turn about the vertical a quarter at a time, as play.tide's TurnBy.
static tide_float3 turn_by(const tide_float3 v, const float yaw)
{
    const int quarters = (((int)lroundf(yaw / (TIDE_PI_F * 0.5f))) % 4 + 4) % 4;
    if (quarters == 1) return (tide_float3){v.z, v.y, -v.x};
    if (quarters == 2) return (tide_float3){-v.x, v.y, -v.z};
    if (quarters == 3) return (tide_float3){-v.z, v.y, v.x};
    return v;
}

// Walk into the bench's whole end and out of the other, as often as it takes to be crossed `crossings` times in all.
static void walk_through(tide_world *world, const int id, const tide_float3 from, const float yaw, const int crossings, const int most)
{
    place(world, id, from, yaw, 0);
    input(world, 0, (PilotInput){.active = id, .move = {0, 1}});
    for (int step = 0; step < most && world->Fold.traversals < crossings; step++) tide_world_tick(world);
    CHECK(world->Fold.traversals == crossings);
    input(world, 0, (PilotInput){.active = id});
    ticks(world, 40);
}

// Operator A's two ends: amber on the south wall at x -5, whole, and cyan where they shoot it from `from` facing `yaw`.
static void two_ends(tide_world *world, const tide_float3 from, const float yaw, const float pitch)
{
    place(world, 0, (tide_float3){-5, 0, -7}, SOUTH, 0);
    ticks(world, 4);
    press(world, (PilotInput){.amber = true});
    CHECK(world->Fold.ends.at[0].open && near(world->Fold.ends.at[0].at.z, -10, 0.001f));
    place(world, 0, from, yaw, pitch);
    ticks(world, 4);
    press(world, (PilotInput){.cyan = true});
    CHECK(world->Fold.ends.at[1].open);
}


// The following helpers drive the same input as a player. They never reposition
// bodies, objects or folds. Per-place solves use them from the arrival lift.
static inline tide_entity find_beast(const tide_world *world, BeastKind kind)
{
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (tide_has_Beast(world, entity)) {
            const Beast beast = tide_get_Beast(world, entity);
            if (beast.kind == kind && beast.chamber == world->Arena.chamber) return entity;
        }
    }
    CHECK(false);
    return (tide_entity){0};
}

static inline void act(tide_world *world, int id, PilotInput controls)
{
    controls.active = id;
    press(world, controls);
}

static inline void aim_at(tide_world *world, int id, tide_float3 target)
{
    for (int turn = 0; turn < 32; turn++) {
        const Operator op = pilot(world, id);
        const float dx = target.x - op.position.x, dz = target.z - op.position.z;
        const float dy = target.y - op.position.y - 1.55f * op.scale;
        float yaw = atan2f(dx, dz) - op.yaw;
        while (yaw > TIDE_PI_F) yaw -= 2 * TIDE_PI_F;
        while (yaw < -TIDE_PI_F) yaw += 2 * TIDE_PI_F;
        const float pitch = fmaxf(-1.4f, fminf(1.4f, atan2f(dy, sqrtf(dx * dx + dz * dz)))) - op.pitch;
        if (fabsf(yaw) < .0001f && fabsf(pitch) < .0001f) break;
        input(world, 0, (PilotInput){.active = id, .look = {fmaxf(-.35f, fminf(.35f, yaw)), fmaxf(-.35f, fminf(.35f, pitch))}});
        tide_world_tick(world);
    }
    input(world, 0, (PilotInput){.active = id});
}

static inline void walk_to(tide_world *world, int id, float x, float z)
{
    const int chamber = world->Arena.chamber;
    for (int step = 0; step < 3600; step++) {
        const Operator op = pilot(world, id);
        const float dx = x - op.position.x, dz = z - op.position.z;
        const float gap = sqrtf(dx * dx + dz * dz);
        if (gap < 0.055f * op.scale + 0.015f) {
            input(world, 0, (PilotInput){.active = id});
            ticks(world, 12);
            return;
        }
        const float pace = fminf(1, gap / fmaxf(op.scale * .7f, .02f));
        const float sn = tide_sin_f(op.yaw), cs = tide_cos_f(op.yaw);
        input(world, 0, (PilotInput){.active = id, .move = {(dx * cs - dz * sn) / gap * pace, (dx * sn + dz * cs) / gap * pace}});
        tide_world_tick(world);
        if (world->Arena.chamber != chamber || world->Arena.caught > 0) {
            fprintf(stderr, "walk_to encounter: body %d at %.3f %.3f %.3f trying %.3f %.3f, place %d caught %.3f\n", id,
                    pilot(world, id).position.x, pilot(world, id).position.y, pilot(world, id).position.z, x, z,
                    world->Place.id, world->Arena.caught);
            CHECK(false);
        }
    }
    fprintf(stderr, "walk_to: operator %d stuck at %.3f %.3f %.3f trying %.3f %.3f (place %d)\n", id,
            pilot(world, id).position.x, pilot(world, id).position.y, pilot(world, id).position.z, x, z, world->Place.id);
    fprintf(stderr, "gate %.3f signals %d cube %.3f %.3f %.3f other %.3f %.3f %.3f\n", world->Arena.gate, world->Arena.signals,
            world->Fold.cube.x, world->Fold.cube.y, world->Fold.cube.z,
            pilot(world, 1-id).position.x, pilot(world, 1-id).position.y, pilot(world, 1-id).position.z);
    CHECK(false);
}

static inline void cross_fold(tide_world *world, int id, tide_float3 target, int limit)
{
    const int before = world->Fold.traversals;
    aim_at(world, id, target);
    input(world, 0, (PilotInput){.active = id, .move = {0, 1}});
    bool crossed = false;
    for (int i = 0; i < limit && !crossed; i++) {
        const Operator was = pilot(world, id);
        tide_world_tick(world);
        const Operator now = pilot(world, id);
        const float dx = now.position.x - was.position.x, dz = now.position.z - was.position.z;
        const float continuous = 1 + sqrtf(was.velocity.x * was.velocity.x + was.velocity.z * was.velocity.z) / 30;
        crossed = now.scale != was.scale || dx * dx + dz * dz > continuous * continuous;
    }
    input(world, 0, (PilotInput){.active = id});
    if (!crossed)
        fprintf(stderr, "cross_fold failed: place %d operator %d at %.3f %.3f %.3f toward %.3f %.3f %.3f, scale %.3f\n",
                world->Place.id, id, pilot(world, id).position.x, pilot(world, id).position.y, pilot(world, id).position.z,
                target.x, target.y, target.z, pilot(world, id).scale);
    CHECK(crossed && world->Fold.traversals > before);
    ticks(world, 16);
}

#endif
