// The private prototype's rules, on the generated simulation; no game seats
// are bots. Network tests feed inputs from their own two real client slots.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/list.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"

#define DT (1.0f / 60.0f)
#define SCRIPTED_HASH UINT64_C(0x1fe52c6bf4f307fe)

static tide_world world;

static tide_player_id player(const int seat)
{
    return tide_player_from_index(seat);
}

static tide_entity marine(const tide_world *w, const int seat)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Marine(w, e) && tide_get_Marine(w, e).seat == seat) return e;
    }
    return (tide_entity){0};
}

static int enemies(const tide_world *w)
{
    int count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (tide_has_Enemy(w, e) && tide_get_Enemy(w, e).health > 0) count++;
    }
    return count;
}

static int projectiles(const tide_world *w)
{
    int count = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        if (tide_has_Projectile(w, tide_entity_in_slot(&w->entities, i))) count++;
    }
    return count;
}

static void tick(const int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(&world);
}

static void start(const int count)
{
    tide_world_free(&world);
    tide_world_init(&world, DT);
    for (int seat = 0; seat < count; seat++) tide_world_player_joined(&world, player(seat));
    tick(2);
}

// Commands use the same bounded wire representation as the game's outbox.
static uint32_t message(const uint32_t id, const int value, const int sector, uint8_t bytes[TIDE_COMMAND_BYTES])
{
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, id, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    if (id == TIDE_COMMAND_ID_Ready) tide_bits_put_bool(&bits, value != 0);
    if (id == TIDE_COMMAND_ID_PickRelic) {
        tide_bits_put_varint(&bits, tide_zigzag(value));
        tide_bits_put_varint(&bits, tide_zigzag(sector));
    }
    return tide_bits_end(&bits);
}

static bool command(const int seat, const uint32_t id, const int value, const int sector)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = message(id, value, sector, bytes);
    const tide_player_id who = player(seat);
    return tide_game_api.command(&world, who, 0,
        tide_game_api.command_sequence(&world, who, 0) + 1, bytes, size);
}

static bool ready(const int count)
{
    for (int seat = 0; seat < count; seat++) {
        if (!command(seat, TIDE_COMMAND_ID_Ready, 1, 0)) return false;
    }
    tick(3);
    return world.Expedition.phase == Phase_Fight;
}

static void put_marine(const int seat, const tide_float3 position)
{
    const tide_entity e = marine(&world, seat);
    Pose pose = tide_get_Pose(&world, e);
    pose.position = position;
    pose.velocity = tide_f3(0, 0, 0);
    tide_set_Pose(&world, e, pose);
}

static void invulnerable(const int seat)
{
    const tide_entity e = marine(&world, seat);
    Marine body = tide_get_Marine(&world, e);
    body.invulnerable = 10000;
    tide_set_Marine(&world, e, body);
}

#define MAP_WIDTH 41
#define MAP_HEIGHT 33
#define MAP_TILES (MAP_WIDTH * MAP_HEIGHT)
#define CELL_SIZE 2.4f

static bool wall_cell(const int sector, const int x, const int z)
{
    if (x < 0 || z < 0 || x >= MAP_WIDTH || z >= MAP_HEIGHT) return true;
    const uint8_t *cell = tide_list_read(&world.heap, world.GeometryAudit.cells,
        sector * MAP_TILES + z * MAP_WIDTH + x, sizeof(uint8_t));
    return cell == NULL || *cell == '#';
}

static int cell_of(const tide_float2 point)
{
    return (int)tide_floor_f(point.y / CELL_SIZE) * MAP_WIDTH + (int)tide_floor_f(point.x / CELL_SIZE);
}

// A separate ray/AABB oracle checks the DDA traversal's first intersection.
// It visits every wall tile rather than following the game's stepping logic.
static float wall_intersection(const int sector, const tide_float2 from, const tide_float2 way)
{
    float hit = 150;
    for (int z = 0; z < MAP_HEIGHT; z++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (!wall_cell(sector, x, z)) continue;
            const float origin[2] = {from.x, from.y};
            const float direction[2] = {way.x, way.y};
            const float low[2] = {(float)x * CELL_SIZE, (float)z * CELL_SIZE};
            float near = 0;
            float far = hit;
            for (int axis = 0; axis < 2; axis++) {
                if (direction[axis] == 0) {
                    if (origin[axis] < low[axis] || origin[axis] > low[axis] + CELL_SIZE) far = -1;
                    continue;
                }
                const float first = (low[axis] - origin[axis]) / direction[axis];
                const float second = (low[axis] + CELL_SIZE - origin[axis]) / direction[axis];
                const float enter = first < second ? first : second;
                const float exit = first > second ? first : second;
                if (enter > near) near = enter;
                if (exit < far) far = exit;
            }
            if (near <= far && far >= 0 && near < hit) hit = near;
        }
    }
    return hit;
}

static bool circle_safe(const int sector, const tide_float2 center, const float radius)
{
    for (int z = 0; z < MAP_HEIGHT; z++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (!wall_cell(sector, x, z)) continue;
            const float left = (float)x * CELL_SIZE;
            const float top = (float)z * CELL_SIZE;
            const float closest_x = center.x < left ? left : center.x > left + CELL_SIZE ? left + CELL_SIZE : center.x;
            const float closest_z = center.y < top ? top : center.y > top + CELL_SIZE ? top + CELL_SIZE : center.y;
            const float dx = center.x - closest_x;
            const float dz = center.y - closest_z;
            if (dx * dx + dz * dz < radius * radius - 0.00001f) return false;
        }
    }
    return true;
}

TIDE_TEST(dead_signal_every_sector_is_connected_and_its_spawns_and_rays_are_safe)
{
    start(0);
    TIDE_REQUIRE(world.GeometryAudit.done);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, world.GeometryAudit.cells) == 6 * MAP_TILES);
    static const tide_float2 directions[8] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {0.70710677f, 0.70710677f}, {-0.70710677f, 0.70710677f},
        {0.70710677f, -0.70710677f}, {-0.70710677f, -0.70710677f},
    };
    static const int count[6] = {5, 5, 6, 6, 6, 5};
    for (int sector = 0; sector < 6; sector++) {
        bool seen[MAP_TILES] = {false};
        int queue[MAP_TILES];
        int read = 0;
        int put = 1;
        queue[0] = cell_of(world.GeometryAudit.starts.at[sector * 4]);
        TIDE_REQUIRE(queue[0] >= 0 && queue[0] < MAP_TILES);
        seen[queue[0]] = true;
        while (read < put) {
            const int at = queue[read++];
            const int x = at % MAP_WIDTH;
            const int z = at / MAP_WIDTH;
            static const int dx[4] = {1, -1, 0, 0};
            static const int dz[4] = {0, 0, 1, -1};
            for (int i = 0; i < 4; i++) {
                const int next = (z + dz[i]) * MAP_WIDTH + x + dx[i];
                if (wall_cell(sector, x + dx[i], z + dz[i]) || seen[next]) continue;
                seen[next] = true;
                queue[put++] = next;
            }
        }
        int floor = 0;
        for (int z = 0; z < MAP_HEIGHT; z++) {
            for (int x = 0; x < MAP_WIDTH; x++) {
                if (x == 0 || x == MAP_WIDTH - 1 || z == 0 || z == MAP_HEIGHT - 1)
                    TIDE_CHECK(wall_cell(sector, x, z));
                if (!wall_cell(sector, x, z)) {
                    floor++;
                    TIDE_CHECK(seen[z * MAP_WIDTH + x]);
                }
            }
        }
        TIDE_CHECK(floor > 550 && floor == put);
        for (int seat = 0; seat < 4; seat++) {
            TIDE_CHECK(world.GeometryAudit.safeStarts.at[sector * 4 + seat]);
            TIDE_CHECK(seen[cell_of(world.GeometryAudit.starts.at[sector * 4 + seat])]);
        }
        TIDE_CHECK(seen[cell_of(world.GeometryAudit.exits.at[sector])]);
        for (int i = 0; i < 3; i++) TIDE_CHECK(seen[cell_of(world.GeometryAudit.relays.at[sector * 3 + i])]);
        for (int i = 0; i < 2; i++) TIDE_CHECK(seen[cell_of(world.GeometryAudit.caches.at[sector * 2 + i])]);
        TIDE_CHECK(world.GeometryAudit.encounterCounts.at[sector] == count[sector]);
        for (int encounter = 0; encounter < count[sector]; encounter++) {
            TIDE_CHECK(seen[cell_of(world.GeometryAudit.encounters.at[sector * 6 + encounter])]);
            for (int i = 0; i < 16; i++)
                TIDE_CHECK(world.GeometryAudit.safeEnemies.at[sector * 96 + encounter * 16 + i]);
        }
        const tide_float2 from = world.GeometryAudit.starts.at[sector * 4];
        for (int i = 0; i < 8; i++) {
            const float expected = wall_intersection(sector, from, directions[i]);
            TIDE_CHECK(tide_abs_f(world.GeometryAudit.rays.at[sector * 8 + i] - expected) < 0.0002f);
            if (i < 4) {
                const tide_float2 moved = world.GeometryAudit.moves.at[sector * 4 + i];
                const float distance = (moved.x - from.x) * directions[i].x + (moved.y - from.y) * directions[i].y;
                TIDE_CHECK(distance > 0 && distance < expected + 0.001f);
                TIDE_CHECK(circle_safe(sector, moved, 0.46f));
                TIDE_CHECK(!circle_safe(sector, tide_f2(moved.x + directions[i].x * 0.3f,
                    moved.y + directions[i].y * 0.3f), 0.46f));
                TIDE_CHECK(!wall_cell(sector, (int)(moved.x / CELL_SIZE), (int)(moved.y / CELL_SIZE)));
            }
        }
        printf("    sector %d: %d connected floor cells, %d encounters, 3 relays, safe spawn sweeps\n",
            sector + 1, floor, count[sector]);
    }
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_requires_two_real_players_and_their_readiness)
{
    start(1);
    TIDE_REQUIRE(tide_has_Marine(&world, marine(&world, 0)));
    TIDE_CHECK(world.Expedition.phase == Phase_Lobby && world.Expedition.manned == 1);
    TIDE_REQUIRE(command(0, TIDE_COMMAND_ID_Ready, 1, 0));
    tick(180);
    TIDE_CHECK(world.Expedition.phase == Phase_Lobby && enemies(&world) == 0);
    tide_world_player_joined(&world, player(1));
    tick(2);
    TIDE_CHECK(world.Expedition.players == 2 && world.Expedition.phase == Phase_Lobby);
    TIDE_REQUIRE(command(1, TIDE_COMMAND_ID_Ready, 1, 0));
    tick(3);
    TIDE_CHECK(world.Expedition.phase == Phase_Fight && world.Expedition.sector == 0);
    TIDE_CHECK(tide_get_Owner(&world, marine(&world, 0)).player.id !=
        tide_get_Owner(&world, marine(&world, 1)).player.id);
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_a_missing_teammate_suspends_the_expedition)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    invulnerable(0);
    tide_world_player_left(&world, player(1));
    tick(2);
    TIDE_CHECK(world.Expedition.manned == 1 && world.Expedition.suspended);
    TIDE_CHECK(!tide_has_Marine(&world, marine(&world, 1)));
    const float elapsed = world.Expedition.elapsed;
    const float timer = world.Expedition.timer;
    const Pose waiting = tide_get_Pose(&world, marine(&world, 0));
    const Marine before = tide_get_Marine(&world, marine(&world, 0));
    tide_world_set_input(&world, player(0), (Controls){.move = {1, 1}, .fire = true, .use = true});
    tick(600);
    const Pose after = tide_get_Pose(&world, marine(&world, 0));
    TIDE_CHECK(world.Expedition.elapsed == elapsed && world.Expedition.timer == timer);
    TIDE_CHECK(memcmp(&waiting.position, &after.position, sizeof waiting.position) == 0);
    TIDE_CHECK(tide_get_Marine(&world, marine(&world, 0)).shells == before.shells);
    tide_world_set_input(&world, player(0), (Controls){0});
    tide_world_player_joined(&world, player(2));
    tick(3);
    TIDE_CHECK(world.Expedition.players == 2 && !world.Expedition.suspended);
    TIDE_CHECK(tide_has_Marine(&world, marine(&world, 1)));
    TIDE_CHECK(tide_get_Owner(&world, marine(&world, 1)).player.id == player(2).id);
    TIDE_CHECK(world.Expedition.elapsed > elapsed);
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_a_living_partner_can_revive_but_the_downed_cannot)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    invulnerable(1);
    const tide_entity down = marine(&world, 0);
    const tide_entity rescuer = marine(&world, 1);
    const tide_float3 nearby = tide_get_Pose(&world, rescuer).position;
    put_marine(0, nearby);
    Marine body = tide_get_Marine(&world, down);
    body.health = 0;
    body.downed = true;
    body.invulnerable = 0;
    tide_set_Marine(&world, down, body);
    tick(2);
    TIDE_REQUIRE(tide_get_Marine(&world, down).downed);
    tide_world_set_input(&world, player(0), (Controls){.use = true});
    tick(150);
    TIDE_CHECK(tide_get_Marine(&world, down).downed && tide_get_Marine(&world, down).revive == 0);
    tide_world_set_input(&world, player(0), (Controls){0});
    tide_world_set_input(&world, player(1), (Controls){.use = true});
    tick(60);
    TIDE_CHECK(tide_get_Marine(&world, down).downed && tide_get_Marine(&world, down).revive > 0);
    tide_world_set_input(&world, player(1), (Controls){0});
    tick(180);
    TIDE_CHECK(tide_get_Marine(&world, down).downed);
    tide_world_set_input(&world, player(1), (Controls){.use = true});
    tick(160);
    body = tide_get_Marine(&world, down);
    TIDE_CHECK(!body.downed && body.health > 0 && body.invulnerable > 0);
    TIDE_CHECK(world.Expedition.phase == Phase_Fight && world.Expedition.wipes == 0);
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_a_wipe_retries_the_sector_and_keeps_the_build)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    const int generation = world.Expedition.generation;
    for (int seat = 0; seat < 2; seat++) {
        const tide_entity e = marine(&world, seat);
        Marine body = tide_get_Marine(&world, e);
        body.health = 0;
        body.downed = true;
        body.invulnerable = 0;
        body.relic = seat + 1;
        tide_set_Marine(&world, e, body);
    }
    tick(3);
    TIDE_REQUIRE(world.Expedition.phase == Phase_Wipe);
    TIDE_CHECK(world.Expedition.wipes == 1);
    TIDE_REQUIRE(command(0, TIDE_COMMAND_ID_Retry, 0, 0));
    tick(4);
    TIDE_CHECK(world.Expedition.phase == Phase_Fight && world.Expedition.sector == 0);
    TIDE_CHECK(world.Expedition.generation > generation && world.Expedition.wipes == 1);
    for (int seat = 0; seat < 2; seat++) {
        const Marine body = tide_get_Marine(&world, marine(&world, seat));
        TIDE_CHECK(body.health > 0 && !body.downed && body.relic == seat + 1);
        TIDE_CHECK(body.shells > 0 && body.nails > 0);
    }
    tide_world_free(&world);
}

static float distance_sq(const tide_float3 a, const tide_float3 b)
{
    const float x = a.x - b.x;
    const float z = a.z - b.z;
    return x * x + z * z;
}

static tide_float3 floor_point(const tide_float2 point)
{
    return tide_f3(point.x, 0, point.y);
}

static bool clear_segment(const int sector, const tide_float3 a, const tide_float3 b)
{
    const float distance = tide_sqrt_f(distance_sq(a, b));
    const int steps = (int)(distance / 0.18f) + 1;
    for (int i = 0; i <= steps; i++) {
        const float part = (float)i / (float)steps;
        const float x = a.x + (b.x - a.x) * part;
        const float z = a.z + (b.z - a.z) * part;
        if (wall_cell(sector, (int)tide_floor_f(x / CELL_SIZE), (int)tide_floor_f(z / CELL_SIZE))) return false;
    }
    return true;
}

// Spawn a real encounter, then retain one stationary enemy for focused shot
// tests. The fixtures alter health/position, but firing and damage are real.
static tide_entity shot_fixture(void)
{
    put_marine(0, floor_point(world.GeometryAudit.encounters.at[world.Expedition.sector * 6]));
    tick(2);
    world.Expedition.triggered = (1 << world.GeometryAudit.encounterCounts.at[world.Expedition.sector]) - 1;
    for (int i = 0; i < 8; i++) world.Expedition.beats.at[i] = 3;
    tide_entity kept = {0};
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Enemy(&world, e)) continue;
        Enemy body = tide_get_Enemy(&world, e);
        if (!tide_has_Enemy(&world, kept)) {
            kept = e;
            body.kind = EnemyKind_Trooper;
            body.health = 10000;
            body.maximum = 10000;
            body.cooldown = 10000;
            body.windup = 10000;
        }
        else body.dead = true;
        tide_set_Enemy(&world, e, body);
    }
    tick(2);
    return kept;
}

static void aim_at(const int seat, const tide_float3 target, const Weapon weapon)
{
    const tide_float3 from = tide_get_Pose(&world, marine(&world, seat)).position;
    const float gap = tide_sqrt_f(distance_sq(from, target));
    tide_world_set_input(&world, player(seat), (Controls){.fire = true, .weapon = weapon,
        .yaw = tide_atan2_f(target.x - from.x, target.z - from.z),
        .pitch = -tide_atan2_f(1.0f - 1.55f, gap)});
}

TIDE_TEST(dead_signal_aim_matches_the_first_person_camera_at_nonzero_angles)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    const float angles[2][2] = {{0.73f, -0.52f}, {-1.15f, 0.66f}};
    for (int i = 0; i < 2; i++) {
        tide_world_set_input(&world, player(0), (Controls){.yaw = angles[i][0], .pitch = angles[i][1], .weapon = -1});
        tick(2);
        const tide_float3 aim = tide_get_Marine(&world, marine(&world, 0)).aim;
        const tide_float3 camera = tide_forward_q(tide_euler_q(tide_f3(angles[i][1], angles[i][0], 0)));
        TIDE_CHECK(tide_abs_f(aim.x - camera.x) < 0.00001f);
        TIDE_CHECK(tide_abs_f(aim.y - camera.y) < 0.00001f);
        TIDE_CHECK(tide_abs_f(aim.z - camera.z) < 0.00001f);
    }
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_shots_pass_allies_without_harming_them)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    const tide_entity enemy = shot_fixture();
    TIDE_REQUIRE(tide_has_Enemy(&world, enemy));
    const tide_float3 shooter = floor_point(world.GeometryAudit.starts.at[0]);
    tide_float3 target = shooter;
    bool found = false;
    static const tide_float2 ways[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int i = 0; i < 4; i++) {
        const tide_float3 candidate = tide_f3(shooter.x + ways[i].x * 6, 0, shooter.z + ways[i].y * 6);
        if (!clear_segment(0, shooter, candidate)) continue;
        target = candidate;
        found = true;
        break;
    }
    TIDE_REQUIRE(found);
    put_marine(0, shooter);
    put_marine(1, tide_f3((shooter.x + target.x) * 0.5f, 0, (shooter.z + target.z) * 0.5f));
    Pose pose = tide_get_Pose(&world, enemy);
    pose.position = target;
    tide_set_Pose(&world, enemy, pose);
    const tide_entity ally = marine(&world, 1);
    Marine body = tide_get_Marine(&world, ally);
    body.invulnerable = 0;
    body.armour = 0;
    tide_set_Marine(&world, ally, body);
    aim_at(0, target, Weapon_Nailgun);
    tick(180);
    TIDE_CHECK(tide_get_Marine(&world, ally).health == body.health);
    TIDE_CHECK(tide_get_Enemy(&world, enemy).health < 9900);
    TIDE_CHECK(tide_get_Marine(&world, marine(&world, 0)).nails < 150);
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_an_enemy_behind_a_wall_never_receives_a_hitscan_shot)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    const tide_entity enemy = shot_fixture();
    TIDE_REQUIRE(tide_has_Enemy(&world, enemy));
    const tide_float3 shooter = floor_point(world.GeometryAudit.starts.at[0]);
    tide_float3 target = shooter;
    bool found = false;
    for (int z = 2; z < MAP_HEIGHT - 2 && !found; z++) {
        for (int x = 2; x < MAP_WIDTH - 2 && !found; x++) {
            if (wall_cell(0, x, z)) continue;
            const tide_float3 candidate = tide_f3(((float)x + 0.5f) * CELL_SIZE, 0, ((float)z + 0.5f) * CELL_SIZE);
            const float distance = distance_sq(shooter, candidate);
            if (distance > 45 * 45 || distance < 5 * 5 || clear_segment(0, shooter, candidate)) continue;
            target = candidate;
            found = true;
        }
    }
    TIDE_REQUIRE(found);
    put_marine(0, shooter);
    Pose pose = tide_get_Pose(&world, enemy);
    pose.position = target;
    tide_set_Pose(&world, enemy, pose);
    aim_at(0, target, Weapon_Nailgun);
    tick(180);
    TIDE_CHECK(tide_get_Enemy(&world, enemy).health == 10000);
    TIDE_CHECK(tide_get_Marine(&world, marine(&world, 0)).nails < 150);
    tide_world_free(&world);
}

static tide_entity projectile_of(const bool hostile)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Projectile(&world, e) && tide_get_Projectile(&world, e).hostile == hostile) return e;
    }
    return (tide_entity){0};
}

TIDE_TEST(dead_signal_hostile_bolts_and_rockets_fly_through_clear_air)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    world.Expedition.sector = 1;
    world.Expedition.generation += 1;
    tick(2);
    const tide_entity enemy = shot_fixture();
    TIDE_REQUIRE(tide_has_Enemy(&world, enemy));
    const tide_float3 from = floor_point(world.GeometryAudit.starts.at[4]);
    static const tide_float2 ways[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    tide_float3 target = from;
    bool found = false;
    for (int i = 0; i < 4; i++) {
        const tide_float3 candidate = tide_f3(from.x + ways[i].x * 8, 0, from.z + ways[i].y * 8);
        if (!clear_segment(1, from, candidate)) continue;
        target = candidate;
        found = true;
        break;
    }
    TIDE_REQUIRE(found);
    put_marine(0, from);
    put_marine(1, from);
    invulnerable(0);
    invulnerable(1);
    Pose enemy_pose = tide_get_Pose(&world, enemy);
    enemy_pose.position = target;
    tide_set_Pose(&world, enemy, enemy_pose);
    Enemy body = tide_get_Enemy(&world, enemy);
    body.windup = 0.001f;
    body.cooldown = 0;
    tide_set_Enemy(&world, enemy, body);
    Marine shooter = tide_get_Marine(&world, marine(&world, 0));
    shooter.fireCooldown = 0;
    tide_set_Marine(&world, marine(&world, 0), shooter);
    aim_at(0, target, Weapon_Launcher);
    tick(1);
    tide_world_set_input(&world, player(0), (Controls){.weapon = -1});
    const tide_entity bolt = projectile_of(true);
    const tide_entity rocket = projectile_of(false);
    TIDE_REQUIRE(tide_has_Projectile(&world, bolt) && tide_has_Projectile(&world, rocket));
    const tide_float3 bolt_from = tide_get_Pose(&world, bolt).position;
    const tide_float3 rocket_from = tide_get_Pose(&world, rocket).position;
    tick(3);
    TIDE_CHECK(tide_has_Projectile(&world, bolt) && tide_has_Projectile(&world, rocket));
    TIDE_CHECK(distance_sq(tide_get_Pose(&world, bolt).position, bolt_from) > 0.2f);
    TIDE_CHECK(distance_sq(tide_get_Pose(&world, rocket).position, rocket_from) > 1.0f);
    TIDE_CHECK(tide_get_Projectile(&world, bolt).life < 3 && tide_get_Projectile(&world, rocket).life < 3);
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_enemy_damage_downs_a_marine_without_ending_the_team)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    const tide_entity enemy = shot_fixture();
    TIDE_REQUIRE(tide_has_Enemy(&world, enemy));
    const tide_entity victim = marine(&world, 0);
    const tide_float3 at = tide_get_Pose(&world, victim).position;
    Marine body = tide_get_Marine(&world, victim);
    body.health = 1;
    body.armour = 0;
    body.invulnerable = 0;
    tide_set_Marine(&world, victim, body);
    Pose pose = tide_get_Pose(&world, enemy);
    pose.position = tide_f3(at.x + 0.7f, 0, at.z);
    tide_set_Pose(&world, enemy, pose);
    Enemy attacker = tide_get_Enemy(&world, enemy);
    attacker.kind = EnemyKind_Rush;
    attacker.windup = 0;
    attacker.cooldown = 0;
    tide_set_Enemy(&world, enemy, attacker);
    tick(2);
    TIDE_CHECK(!tide_get_Marine(&world, victim).downed && tide_get_Marine(&world, victim).health == 1);
    TIDE_CHECK(tide_get_Enemy(&world, enemy).windup > 0);
    tick(20);
    body = tide_get_Marine(&world, victim);
    TIDE_CHECK(body.downed && body.health == 0 && body.revive == 0);
    TIDE_CHECK(world.Expedition.phase == Phase_Fight && world.Expedition.wipes == 0);
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_relays_and_extraction_require_two_continuous_living_users)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    // Isolate paired interactions from the encounters checked by the full
    // input playthrough. Every actual player input and hold timer still runs.
    const int all = (1 << world.GeometryAudit.encounterCounts.at[0]) - 1;
    world.Expedition.triggered = all;
    world.Expedition.cleared = all;
    for (int i = 0; i < 8; i++) world.Expedition.beats.at[i] = 3;
    const tide_float3 relay = floor_point(world.GeometryAudit.relays.at[0]);
    put_marine(0, relay);
    put_marine(1, relay);
    tide_world_set_input(&world, player(0), (Controls){.weapon = -1, .use = true});
    tick(240);
    TIDE_CHECK(world.Expedition.relays == 0 && world.Expedition.relayHold.at[0] == 0);
    const tide_entity second = marine(&world, 1);
    Marine down = tide_get_Marine(&world, second);
    down.downed = true;
    down.health = 0;
    tide_set_Marine(&world, second, down);
    tide_world_set_input(&world, player(1), (Controls){.weapon = -1, .use = true});
    tick(60);
    TIDE_CHECK(world.Expedition.relays == 0 && world.Expedition.relayHold.at[0] == 0);
    TIDE_CHECK(tide_get_Marine(&world, second).downed);
    down.downed = false;
    down.health = 100;
    down.revive = 0;
    tide_set_Marine(&world, second, down);
    tick(60);
    TIDE_CHECK(world.Expedition.relays == 0 && world.Expedition.relayHold.at[0] > 0);
    tide_world_set_input(&world, player(1), (Controls){.weapon = -1});
    tick(2);
    TIDE_CHECK(world.Expedition.relayHold.at[0] == 0);
    tide_world_set_input(&world, player(1), (Controls){.weapon = -1, .use = true});
    tick(140);
    TIDE_CHECK((world.Expedition.relays & 1) != 0 && world.Expedition.objective == 1);
    world.Expedition.relays = 7;
    const tide_float3 exit = floor_point(world.GeometryAudit.exits.at[0]);
    put_marine(0, exit);
    put_marine(1, exit);
    tide_world_set_input(&world, player(1), (Controls){.weapon = -1});
    tick(240);
    TIDE_CHECK(world.Expedition.phase == Phase_Fight && world.Expedition.timer == 0);
    tide_world_set_input(&world, player(1), (Controls){.weapon = -1, .use = true});
    tick(60);
    TIDE_CHECK(world.Expedition.timer > 0 && world.Expedition.phase == Phase_Fight);
    tide_world_set_input(&world, player(1), (Controls){.weapon = -1});
    tick(2);
    TIDE_CHECK(world.Expedition.timer == 0);
    tide_world_set_input(&world, player(1), (Controls){.weapon = -1, .use = true});
    tick(140);
    TIDE_CHECK(world.Expedition.phase == Phase_Relic);
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_relic_choices_validate_sender_sector_and_once_per_seat)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    world.Expedition.phase = Phase_Relic;
    world.Expedition.offers = tide_i3(Relic_Vitality, Relic_QuickHands, Relic_Overpressure);
    TIDE_REQUIRE(command(2, TIDE_COMMAND_ID_PickRelic, 0, 0));
    TIDE_REQUIRE(command(0, TIDE_COMMAND_ID_PickRelic, -1, 0));
    TIDE_REQUIRE(command(0, TIDE_COMMAND_ID_PickRelic, 3, 0));
    TIDE_REQUIRE(command(0, TIDE_COMMAND_ID_PickRelic, 0, 1));
    tick(2);
    TIDE_CHECK(tide_get_Marine(&world, marine(&world, 0)).selected == -1);
    TIDE_CHECK(world.Expedition.sector == 0 && world.Expedition.phase == Phase_Relic);
    TIDE_REQUIRE(command(0, TIDE_COMMAND_ID_PickRelic, 0, 0));
    TIDE_REQUIRE(command(0, TIDE_COMMAND_ID_PickRelic, 1, 0));
    tick(2);
    const Marine first = tide_get_Marine(&world, marine(&world, 0));
    TIDE_CHECK(first.selected == 0 && first.maximum == 135 && first.perks == (1 << Relic_Vitality));
    TIDE_CHECK(world.Expedition.phase == Phase_Relic && world.Expedition.sector == 0);
    TIDE_REQUIRE(command(1, TIDE_COMMAND_ID_PickRelic, 1, 0));
    tick(4);
    TIDE_CHECK(world.Expedition.phase == Phase_Fight && world.Expedition.sector == 1);
    TIDE_CHECK(tide_get_Marine(&world, marine(&world, 0)).perks == (1 << Relic_Vitality));
    TIDE_CHECK(tide_get_Marine(&world, marine(&world, 1)).perks == (1 << Relic_QuickHands));
    tide_world_free(&world);
}

static tide_entity nearest_enemy(const tide_world *w, const tide_float3 from, const bool visible)
{
    float nearest = 1000000;
    tide_entity chosen = {0};
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Enemy(w, e)) continue;
        const Enemy enemy = tide_get_Enemy(w, e);
        if (enemy.dead || enemy.generation != w->Expedition.generation || enemy.health <= 0) continue;
        const Pose pose = tide_get_Pose(w, e);
        const float distance = distance_sq(from, pose.position);
        if (distance >= nearest || (visible && !clear_segment(w->Expedition.sector, from, pose.position))) continue;
        nearest = distance;
        chosen = e;
    }
    return chosen;
}

static tide_float3 route_to(const int sector, const tide_float3 from, const tide_float3 goal)
{
    int distances[MAP_TILES];
    int queue[MAP_TILES];
    for (int i = 0; i < MAP_TILES; i++) distances[i] = -1;
    const int target = cell_of(tide_f2(goal.x, goal.z));
    const int here = cell_of(tide_f2(from.x, from.z));
    if (target < 0 || target >= MAP_TILES || here < 0 || here >= MAP_TILES) return from;
    if (target == here) return goal;
    int read = 0;
    int put = 1;
    queue[0] = target;
    distances[target] = 0;
    static const int dx[4] = {1, -1, 0, 0};
    static const int dz[4] = {0, 0, 1, -1};
    while (read < put && distances[here] < 0) {
        const int at = queue[read++];
        const int x = at % MAP_WIDTH;
        const int z = at / MAP_WIDTH;
        for (int i = 0; i < 4; i++) {
            const int next = (z + dz[i]) * MAP_WIDTH + x + dx[i];
            if (wall_cell(sector, x + dx[i], z + dz[i]) || distances[next] >= 0) continue;
            distances[next] = distances[at] + 1;
            queue[put++] = next;
        }
    }
    if (distances[here] < 0) return from;
    const int x = here % MAP_WIDTH;
    const int z = here / MAP_WIDTH;
    for (int i = 0; i < 4; i++) {
        const int next = (z + dz[i]) * MAP_WIDTH + x + dx[i];
        if (wall_cell(sector, x + dx[i], z + dz[i]) || distances[next] != distances[here] - 1) continue;
        return tide_f3(((float)(x + dx[i]) + 0.5f) * CELL_SIZE, 0, ((float)(z + dz[i]) + 0.5f) * CELL_SIZE);
    }
    return from;
}

static tide_float3 expedition_goal(const int seat)
{
    const int sector = world.Expedition.sector;
    const tide_float3 from = tide_get_Pose(&world, marine(&world, seat)).position;
    const tide_entity partner = marine(&world, 1 - seat);
    const tide_entity prey = nearest_enemy(&world, from, false);
    if (tide_get_Marine(&world, marine(&world, seat)).downed)
        return tide_get_Pose(&world, partner).position;
    if (tide_get_Marine(&world, partner).downed &&
        (!tide_has_Enemy(&world, prey) || distance_sq(from, tide_get_Pose(&world, prey).position) > 9 * 9))
        return tide_get_Pose(&world, partner).position;
    if (tide_has_Enemy(&world, prey)) return tide_get_Pose(&world, prey).position;
    // Use each sector's supplies as the route reaches them; leaving every
    // relay until after five arenas would ignore most of its ammunition.
    if ((world.Expedition.cleared & (1 << 1)) != 0) {
        if ((world.Expedition.caches & 1) == 0) return floor_point(world.GeometryAudit.caches.at[sector * 2]);
        if ((world.Expedition.relays & 1) == 0) return floor_point(world.GeometryAudit.relays.at[sector * 3]);
    }
    if ((world.Expedition.cleared & (1 << 3)) != 0) {
        if ((world.Expedition.caches & 2) == 0) return floor_point(world.GeometryAudit.caches.at[sector * 2 + 1]);
        if ((world.Expedition.relays & 2) == 0) return floor_point(world.GeometryAudit.relays.at[sector * 3 + 1]);
    }
    if ((world.Expedition.cleared & (1 << 4)) != 0 && (world.Expedition.relays & 4) == 0)
        return floor_point(world.GeometryAudit.relays.at[sector * 3 + 2]);
    for (int i = 0; i < world.GeometryAudit.encounterCounts.at[sector]; i++) {
        if ((world.Expedition.cleared & (1 << i)) == 0)
            return floor_point(world.GeometryAudit.encounters.at[sector * 6 + i]);
    }
    for (int i = 0; i < 3; i++) {
        if ((world.Expedition.relays & (1 << i)) == 0)
            return floor_point(world.GeometryAudit.relays.at[sector * 3 + i]);
    }
    return floor_point(world.GeometryAudit.exits.at[sector]);
}

static Controls play_input(const int seat)
{
    const tide_entity me = marine(&world, seat);
    const Marine body = tide_get_Marine(&world, me);
    const tide_float3 from = tide_get_Pose(&world, me).position;
    const tide_float3 objective = expedition_goal(seat);
    tide_float3 next = route_to(world.Expedition.sector, from, objective);
    const tide_entity target = nearest_enemy(&world, from, true);
    const tide_entity partner = marine(&world, 1 - seat);
    const bool rescuing = tide_get_Marine(&world, partner).downed &&
        distance_sq(objective, tide_get_Pose(&world, partner).position) < 0.01f;
    bool interaction = distance_sq(objective, floor_point(world.GeometryAudit.exits.at[world.Expedition.sector])) < 0.01f;
    for (int i = 0; i < 3; i++) interaction |= distance_sq(objective,
        floor_point(world.GeometryAudit.relays.at[world.Expedition.sector * 3 + i])) < 0.01f;
    for (int i = 0; i < 2; i++) interaction |= distance_sq(objective,
        floor_point(world.GeometryAudit.caches.at[world.Expedition.sector * 2 + i])) < 0.01f;
    Controls controls = {.weapon = -1,
        .use = distance_sq(from, objective) < 6.25f && clear_segment(world.Expedition.sector, from, objective)
            && (rescuing || (interaction && enemies(&world) == 0))};
    if (tide_has_Enemy(&world, target)) {
        const Enemy enemy = tide_get_Enemy(&world, target);
        const Pose pose = tide_get_Pose(&world, target);
        const float gap = tide_sqrt_f(distance_sq(from, pose.position));
        controls.yaw = tide_atan2_f(pose.position.x - from.x, pose.position.z - from.z);
        controls.pitch = -tide_atan2_f((enemy.kind == EnemyKind_Warden ? 1.7f : 1.0f) - (body.downed ? 0.45f : 1.55f), gap);
        controls.fire = true;
        if (world.Expedition.sector >= 2 && body.cells > 0 && gap < 13) controls.weapon = Weapon_Arc;
        else if (body.shells > 0 && gap < 10) controls.weapon = Weapon_Scattergun;
        else if (body.nails > 0) controls.weapon = Weapon_Nailgun;
        else if (world.Expedition.sector >= 1 && body.rockets > 0) controls.weapon = Weapon_Launcher;
        else if (body.shells > 0) controls.weapon = Weapon_Scattergun;
        else controls.weapon = Weapon_Nailgun;
        // A human can backpedal while aiming: these input proxies do too.
        if (!rescuing && !body.downed && enemy.kind == EnemyKind_Rush && gap < 5 && gap > 0.01f) {
            next = tide_f3(from.x + (from.x - pose.position.x) / gap * 2,
                0, from.z + (from.z - pose.position.z) / gap * 2);
        }
    }
    else controls.yaw = body.yaw;
    float x = next.x - from.x;
    float z = next.z - from.z;
    const float distance = tide_sqrt_f(x * x + z * z);
    if (distance > 0.1f && !controls.use) {
        const float speed = distance < 0.4f ? 0.3f : 0.85f;
        x *= speed / distance;
        z *= speed / distance;
        const float sine = tide_sin_f(controls.yaw);
        const float cosine = tide_cos_f(controls.yaw);
        controls.move = tide_f2(x * cosine - z * sine, x * sine + z * cosine);
    }
    return controls;
}

TIDE_TEST(dead_signal_normal_crew_can_clear_the_opening_encounter)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    int downs = 0;
    bool was_down[2] = {false, false};
    float lowest[2] = {100, 100};
    int ticks = 0;
    for (; ticks < 18000 && world.Expedition.phase == Phase_Fight && (world.Expedition.cleared & 1) == 0; ticks++) {
        for (int seat = 0; seat < 2; seat++) {
            const Marine body = tide_get_Marine(&world, marine(&world, seat));
            if (body.downed && !was_down[seat]) downs++;
            was_down[seat] = body.downed;
            if (body.health < lowest[seat]) lowest[seat] = body.health;
            Controls controls = play_input(seat);
            const tide_float3 from = tide_get_Pose(&world, marine(&world, seat)).position;
            const tide_entity target = nearest_enemy(&world, from, true);
            const bool rescuing = controls.use;
            if (!body.downed && !rescuing && tide_has_Enemy(&world, target)) {
                const float gap = tide_sqrt_f(distance_sq(from, tide_get_Pose(&world, target).position));
                // The nail driver is the precise opening weapon at this
                // firing distance; scattergun pellets spread beyond 6 m.
                if (body.nails > 0) controls.weapon = Weapon_Nailgun;
                const float advance = gap < 8 ? -0.8f : gap > 12 ? 0.4f : -0.2f;
                const float sine = tide_sin_f(controls.yaw);
                const float cosine = tide_cos_f(controls.yaw);
                for (int attempt = 0; attempt < 2; attempt++) {
                    const float across = ((seat + attempt) % 2 == 0) ? 0.5f : -0.5f;
                    const tide_float2 next = tide_f2(from.x + (cosine * across + sine * advance) * 0.6f,
                        from.z + (-sine * across + cosine * advance) * 0.6f);
                    if (!circle_safe(world.Expedition.sector, next, 0.35f)) continue;
                    controls.move = tide_f2(across, advance);
                    break;
                }
            }
            tide_world_set_input(&world, player(seat), controls);
        }
        tide_world_tick(&world);
    }
    const Marine first = tide_get_Marine(&world, marine(&world, 0));
    const Marine second = tide_get_Marine(&world, marine(&world, 1));
    printf("    normal opening: %.1f seconds, %d kills, %d down events, health %.1f/%.1f (lowest %.1f/%.1f), shells %d/%d, nails %d/%d\n",
        ticks / 60.0, world.Expedition.kills, downs,
        (double)first.health, (double)second.health, (double)lowest[0], (double)lowest[1],
        first.shells, second.shells, first.nails, second.nails);
    TIDE_CHECK(world.Expedition.phase == Phase_Fight && (world.Expedition.cleared & 1) != 0);
    TIDE_CHECK(world.Expedition.beats.at[0] == 3 && world.Expedition.kills >= 30);
    TIDE_CHECK(world.Expedition.wipes == 0 && !first.downed && !second.downed);
    TIDE_CHECK(first.health > 0 && second.health > 0);
    TIDE_CHECK(first.perks == 0 && second.perks == 0);
    tide_world_free(&world);
}

TIDE_TEST(dead_signal_two_input_proxies_can_finish_all_six_sectors)
{
    // Invulnerable QA actors isolate map routing, encounter cleanup, limited
    // ammunition, paired interactions and progression from combat balance.
    // The damage/revive/wipe tests above exercise real health independently.
    start(2);
    TIDE_REQUIRE(ready(2));
    int visited = 1;
    int last_sector = -1;
    int retries = 0;
    int ticks = 0;
    for (; ticks < 120000 && world.Expedition.phase != Phase_Victory; ticks++) {
        if (last_sector != world.Expedition.sector) {
            last_sector = world.Expedition.sector;
            visited |= 1 << last_sector;
            printf("    entering sector %d at %.1f simulated minutes, %d kills, %d wipes\n",
                last_sector + 1, ticks / 3600.0, world.Expedition.kills, world.Expedition.wipes);
        }
        if (world.Expedition.phase == Phase_Wipe) {
            printf("    checkpoint retry %d: %d kills, triggered %x, cleared %x\n",
                retries + 1, world.Expedition.kills, world.Expedition.triggered, world.Expedition.cleared);
            TIDE_REQUIRE(retries++ < 12);
            TIDE_REQUIRE(command(0, TIDE_COMMAND_ID_Retry, 0, 0));
        }
        else if (world.Expedition.phase == Phase_Relic) {
            for (int seat = 0; seat < 2; seat++) {
                TIDE_REQUIRE(command(seat, TIDE_COMMAND_ID_PickRelic, seat, world.Expedition.sector));
                tide_world_set_input(&world, player(seat), (Controls){.weapon = -1});
            }
        }
        else {
            for (int seat = 0; seat < 2; seat++) {
                invulnerable(seat);
                tide_world_set_input(&world, player(seat), play_input(seat));
            }
        }
        tide_world_tick(&world);
    }
    printf("    finish: phase %d, sector %d, mask %x, %d kills, %d wipes, %.1f simulated minutes\n",
        world.Expedition.phase, world.Expedition.sector + 1, visited,
        world.Expedition.kills, world.Expedition.wipes, ticks / 3600.0);
    TIDE_CHECK(world.Expedition.phase == Phase_Victory && world.Expedition.sector == 5 && visited == 63);
    TIDE_CHECK(world.Expedition.kills > 700);
    for (int seat = 0; seat < 2; seat++) {
        const Marine body = tide_get_Marine(&world, marine(&world, seat));
        TIDE_CHECK(body.perks != 0 && body.health > 0 && !body.downed);
    }
    tide_world_free(&world);
}

// Reproducible inputs, with turns, movement, shooting and weapon changes.
// Their angles use Tide's deterministic math rather than the host's libm.
static Controls script(const uint32_t at, const int seat)
{
    return (Controls){
        .yaw = (float)(at % 720) * 0.008726646f + (float)seat * 0.2f,
        .pitch = tide_sin_f((float)at * 0.021f) * 0.12f,
        .move = {at % 240 < 120 ? 0.35f : -0.35f, at % 180 < 90 ? 0.45f : -0.45f},
        .fire = at % 90 < 40,
        .weapon = (int)(at / 240 % 4),
    };
}

static void scripted_ticks(tide_world *w, const uint32_t from, const uint32_t through)
{
    for (uint32_t at = from; at < through; at++) {
        for (int seat = 0; seat < 2; seat++) {
            Controls controls = script(at, seat);
            const tide_float3 origin = tide_get_Pose(w, marine(w, seat)).position;
            const tide_entity target = nearest_enemy(w, origin, true);
            if (at % 240 < 160 && tide_has_Enemy(w, target)) {
                const tide_float3 victim = tide_get_Pose(w, target).position;
                const float gap = tide_sqrt_f(distance_sq(origin, victim));
                const float height = tide_get_Enemy(w, target).kind == EnemyKind_Warden ? 1.7f : 1.0f;
                controls.yaw = tide_atan2_f(victim.x - origin.x, victim.z - origin.z);
                controls.pitch = -tide_atan2_f(height - 1.55f, gap);
                controls.weapon = Weapon_Nailgun;
                controls.fire = true;
            }
            tide_world_set_input(w, player(seat), controls);
        }
        tide_world_tick(w);
    }
}

TIDE_TEST(dead_signal_combat_snapshot_replay_is_identical)
{
    start(2);
    TIDE_REQUIRE(ready(2));
    // Begin in the first live encounter so the snapshot contains its actual
    // AI, bullets, damage queues and ammunition, rather than an idle lobby.
    const tide_float3 arena = floor_point(world.GeometryAudit.encounters.at[0]);
    for (int seat = 0; seat < 2; seat++) {
        put_marine(seat, tide_f3(arena.x + (seat == 0 ? -0.8f : 0.8f), 0, arena.z));
        invulnerable(seat);
    }
    tick(2);
    TIDE_REQUIRE((world.Expedition.triggered & 1) != 0 && enemies(&world) > 0);
    scripted_ticks(&world, 0, 60);
    uint32_t snapshot_at = 60;
    // Save while a real enemy bolt is in flight, rather than relying on an
    // arbitrary tick that can fall between two volleys.
    while (projectiles(&world) == 0 && snapshot_at < 600) {
        scripted_ticks(&world, snapshot_at, snapshot_at + 1);
        snapshot_at++;
    }
    tide_world saved = {0};
    tide_world replay = {0};
    tide_world_copy(&saved, &world);
    const uint64_t snapshot = tide_world_hash(&saved);
    TIDE_CHECK(enemies(&saved) > 0);
    TIDE_CHECK(projectiles(&saved) > 0);
    scripted_ticks(&world, snapshot_at, 1200);
    const uint64_t final = tide_world_hash(&world);
    TIDE_CHECK(world.Expedition.kills > 0);
    TIDE_CHECK(tide_get_Marine(&world, marine(&world, 0)).kills > 0);
    printf("    replay fixture: %d enemies and %d bullets at snapshot, %d kills after replay\n",
        enemies(&saved), projectiles(&saved), world.Expedition.kills);
    tide_world_copy(&replay, &saved);
    scripted_ticks(&replay, snapshot_at, 1200);
    TIDE_CHECK(tide_world_hash(&replay) == final);
    TIDE_CHECK(tide_world_hash(&saved) == snapshot);
    // Exact match bits, checked on Windows clang and WASI together.
    TIDE_CHECK(final == SCRIPTED_HASH);
    printf("    scripted simulation hash: 0x%016llx\n", (unsigned long long)final);
    tide_world_free(&replay);
    tide_world_free(&saved);
    tide_world_free(&world);
}

typedef struct network_match network_match;

typedef struct network_player {
    network_match *match;
    int seat;
} network_player;

struct network_match {
    tide_loopback *net;
    tide_server *server;
    tide_client *clients[2];
    network_player players[2];
    double now;
    bool saw_enemies;
    bool saw_firing;
    uint64_t hashes[1901];
};

static void sample_network(void *user, const uint32_t at, void *input)
{
    const network_player *me = user;
    Controls controls = script(at, me->seat);
    if (at < 900) {
        const tide_world *mine = tide_client_world(me->match->clients[me->seat]);
        if (mine != NULL && mine->GeometryAudit.done) {
            const tide_float2 from = mine->GeometryAudit.starts.at[me->seat];
            const tide_float2 arena = mine->GeometryAudit.encounters.at[0];
            controls = (Controls){.move = {0, 1}, .fire = true, .weapon = Weapon_Nailgun,
                .yaw = tide_atan2_f(arena.x - from.x, arena.y - from.y), .pitch = 0.08f};
        }
    }
    memcpy(input, &controls, sizeof controls);
}

static void network_advance(network_match *m)
{
    m->now += 1.0 / 60.0;
    tide_loopback_set_time(m->net, m->now);
    for (int seat = 0; seat < 2; seat++) tide_client_update(m->clients[seat], m->now);
    tide_server_update(m->server, m->now);
    const tide_world *authority = tide_server_world(m->server);
    m->saw_enemies |= enemies(authority) > 0;
    for (int seat = 0; seat < 2; seat++) {
        const tide_entity e = marine(authority, seat);
        if (tide_has_Marine(authority, e))
            m->saw_firing |= tide_get_Marine(authority, e).nails < 160;
    }
    const uint32_t at = tide_server_tick(m->server);
    if (at <= 1900) m->hashes[at] = tide_world_hash(authority);
}

static bool network_ready(network_match *m, const int seat)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    const uint32_t size = message(TIDE_COMMAND_ID_Ready, 1, 0, bytes);
    return tide_client_send_command(m->clients[seat], bytes, size);
}

TIDE_TEST(dead_signal_two_clients_confirm_the_same_world_on_a_bad_network)
{
    static network_match m;
    m = (network_match){.net = tide_loopback_create(7131)};
    tide_loopback_set_conditions(m.net, (tide_net_conditions){.latency = 0.035, .jitter = 0.015, .loss = 0.06});
    m.server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
        .transports = {tide_loopback_endpoint(m.net, 1)}}, m.now);
    for (int seat = 0; seat < 2; seat++) {
        m.players[seat] = (network_player){&m, seat};
        m.clients[seat] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
            .transport = tide_loopback_endpoint(m.net, (uint32_t)seat + 2),
            .server = tide_loopback_address(1), .lead = 2,
            .sample = sample_network, .user = &m.players[seat]}, m.now);
    }
    for (int i = 0; i < 240; i++) network_advance(&m);
    for (int seat = 0; seat < 2; seat++) {
        TIDE_REQUIRE(tide_client_status_of(m.clients[seat]).state == TIDE_SESSION_CONNECTED);
        TIDE_REQUIRE(network_ready(&m, seat));
    }
    for (int i = 0; i < 300; i++) network_advance(&m);
    const tide_world *authority = tide_server_world(m.server);
    TIDE_CHECK(authority->Expedition.phase == Phase_Fight && authority->Expedition.players == 2);
    while (tide_server_tick(m.server) < 1800) network_advance(&m);
    authority = tide_server_world(m.server);
    TIDE_CHECK(authority->Expedition.players == 2);
    TIDE_CHECK(authority->Expedition.triggered != 0 && m.saw_enemies && m.saw_firing);
    printf("    network combat: encounter mask %x, %d kills, saw live enemies and consumed ammunition\n",
        authority->Expedition.triggered, authority->Expedition.kills);
    for (int seat = 0; seat < 2; seat++) {
        const tide_client_status status = tide_client_status_of(m.clients[seat]);
        const tide_world *confirmed = tide_client_verified_world(m.clients[seat]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
        TIDE_REQUIRE(status.verified_tick > 1600 && status.verified_tick < 1901);
        TIDE_REQUIRE(confirmed != NULL);
        TIDE_CHECK(tide_world_hash(confirmed) == m.hashes[status.verified_tick]);
        TIDE_CHECK(status.again_ticks > 0);
        printf("    client %d confirmed tick %u, replayed %u ticks, resynced %u times\n",
            seat + 1, status.verified_tick, status.again_ticks, status.resyncs);
    }
    for (int seat = 0; seat < 2; seat++) tide_client_destroy(m.clients[seat]);
    tide_server_destroy(m.server);
    tide_loopback_destroy(m.net);
}
