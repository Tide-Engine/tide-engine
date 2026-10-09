// Real private-game views and shaders, through a hidden native window or
// the existing muted WebGL/WebGPU test runner. All fixtures are test-only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#include "tide/session.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

enum { WIDTH = 960, HEIGHT = 540, SAMPLES = 48, STATES = 13, MAP_W = 41, MAP_H = 33 };
static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;
static bool level_seen, actor_seen, gun_seen, present_seen, actor_snap_seen, shadow_seen, overlay_seen;
#if !defined(__wasm__)
static const char *capture_directory;
#endif

static bool floor_cell(const int sector, const int x, const int z)
{
    if (x < 0 || x >= MAP_W || z < 0 || z >= MAP_H) return false;
    const uint8_t *cell = tide_list_read(&world.heap, world.GeometryAudit.cells,
        sector * MAP_W * MAP_H + z * MAP_W + x, sizeof(uint8_t));
    return cell && *cell != '#';
}

static bool safe_point(const tide_float3 point)
{
    const float radius = 0.46f;
    const int x0 = (int)tide_floor_f((point.x - radius) / 2.4f);
    const int x1 = (int)tide_floor_f((point.x + radius) / 2.4f);
    const int z0 = (int)tide_floor_f((point.z - radius) / 2.4f);
    const int z1 = (int)tide_floor_f((point.z + radius) / 2.4f);
    for (int z = z0; z <= z1; z++) {
        for (int x = x0; x <= x1; x++) {
            if (floor_cell(world.Expedition.sector, x, z)) continue;
            const float low_x = (float)x * 2.4f, high_x = low_x + 2.4f;
            const float low_z = (float)z * 2.4f, high_z = low_z + 2.4f;
            const float nearest_x = point.x < low_x ? low_x : point.x > high_x ? high_x : point.x;
            const float nearest_z = point.z < low_z ? low_z : point.z > high_z ? high_z : point.z;
            const float dx = point.x - nearest_x, dz = point.z - nearest_z;
            if (dx * dx + dz * dz < radius * radius) return false;
        }
    }
    return true;
}

static tide_entity operative(const int seat)
{
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (tide_has_Marine(&world, e) && tide_get_Marine(&world, e).seat == seat) return e;
    }
    return (tide_entity){0};
}

static void ticks(const int count)
{
    for (int i = 0; i < count; i++) {
        for (int seat = 0; seat < 2; seat++) {
            const Marine m = tide_get_Marine(&world, operative(seat));
            tide_world_set_input(&world, tide_player_from_index(seat),
                (Controls){.yaw = m.yaw, .pitch = m.pitch, .weapon = -1});
        }
        tide_world_tick(&world);
    }
}

static void place(const int seat, const tide_float2 at)
{
    const tide_entity e = operative(seat);
    Pose p = tide_get_Pose(&world, e);
    p.position = tide_f3(at.x + (float)seat * 1.0f, 0, at.y);
    p.velocity = tide_f3(0, 0, 0);
    tide_set_Pose(&world, e, p);
    Marine m = tide_get_Marine(&world, e);
    m.invulnerable = 1000;
    tide_set_Marine(&world, e, m);
}

static void separate_teammate(void)
{
    const tide_entity viewer = operative(0), teammate = operative(1);
    const Pose viewer_pose = tide_get_Pose(&world, viewer);
    const Marine marine = tide_get_Marine(&world, viewer);
    const float right_x = tide_cos_f(marine.yaw), right_z = -tide_sin_f(marine.yaw);
    const float forward_x = tide_sin_f(marine.yaw), forward_z = tide_cos_f(marine.yaw);
    for (int step = 0; step < 4; step++) {
        const float distance = 3.0f + (float)step * 0.6f;
        for (int side = 0; side < 2; side++) {
            const float sign = side == 0 ? 1 : -1;
            tide_float3 candidate = viewer_pose.position;
            candidate.x += right_x * distance * sign + forward_x * 0.8f;
            candidate.z += right_z * distance * sign + forward_z * 0.8f;
            if (!safe_point(candidate)) continue;
            Pose pose = tide_get_Pose(&world, teammate);
            pose.position = candidate;
            pose.velocity = tide_f3(0, 0, 0);
            tide_set_Pose(&world, teammate, pose);
            return;
        }
    }
    puts("FAIL: no safe lateral teammate position in render fixture");
    failures++;
}

static void sector(const int number)
{
    world.Expedition.sector = number;
    world.Expedition.generation++;
    world.Expedition.phase = Phase_Fight;
    world.Expedition.triggered = 0;
    world.Expedition.cleared = 0;
    world.Expedition.relays = 0;
    world.Expedition.objective = 0;
    world.Expedition.finale = false;
    memset(&world.Expedition.beats, 0, sizeof world.Expedition.beats);
    memset(&world.Expedition.queued, 0, sizeof world.Expedition.queued);
    ticks(2);
}

static void healthy_weapon(const Weapon weapon)
{
    world.Expedition.phase = Phase_Fight;
    devices.keyboard.m = (tide_button){0};
    Marine m = tide_get_Marine(&world, operative(0));
    m.health = 100;
    m.downed = false;
    m.revive = 0;
    m.weapon = weapon;
    m.shells = 30;
    m.nails = 120;
    m.rockets = 8;
    m.cells = 60;
    tide_set_Marine(&world, operative(0), m);
}

static void gallery(void)
{
    sector(0);
    for (int seat = 0; seat < 2; seat++) place(seat, world.GeometryAudit.encounters.at[0]);
    ticks(3); // Get real encounter entities; no production-only fixture API.
    int gallery_x = -1, gallery_z = -1;
    for (int z = 2; z < MAP_H - 5 && gallery_x < 0; z++) {
        for (int x = 5; x < MAP_W - 5; x++) {
            bool clear = true;
            for (int dz = 0; dz < 5; dz++)
                for (int dx = -4; dx <= 4; dx++) clear &= floor_cell(0, x + dx, z + dz);
            if (clear) { gallery_x = x; gallery_z = z; break; }
        }
    }
    if (gallery_x < 0) {
        puts("FAIL: no connected open rectangle for cast gallery");
        failures++;
        return;
    }
    const tide_float3 camera = tide_f3(((float)gallery_x + 0.5f) * 2.4f, 0,
        ((float)gallery_z + 0.5f) * 2.4f);
    for (int seat = 0; seat < 2; seat++) {
        const tide_entity e = operative(seat);
        Pose pose = tide_get_Pose(&world, e);
        pose.position = camera;
        if (seat == 1) { pose.position.x += 5.6f; pose.position.z += 7.2f; }
        pose.velocity = tide_f3(0, 0, 0);
        tide_set_Pose(&world, e, pose);
        Marine marine = tide_get_Marine(&world, e);
        marine.health = 100;
        marine.downed = false;
        marine.yaw = seat == 0 ? 0 : TIDE_PI_F;
        marine.pitch = 0;
        tide_set_Marine(&world, e, marine);
    }
    int actors = 0;
    for (uint32_t i = 0; i < world.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&world.entities, i);
        if (!tide_has_Enemy(&world, e)) continue;
        Enemy enemy = tide_get_Enemy(&world, e);
        enemy.dead = actors >= 4;
        if (!enemy.dead) {
            enemy.kind = (EnemyKind)actors;
            enemy.generation = world.Expedition.generation;
            enemy.health = enemy.maximum = 100;
            enemy.serial = 100 + actors;
            enemy.aim = tide_f3(0, 0, -1);
            enemy.windup = actors == 1 ? 0.15f : 0;
            enemy.flash = 0;
            Pose pose = tide_get_Pose(&world, e);
            pose.position = camera;
            pose.position.x += -5.6f + (float)actors * 2.8f;
            pose.position.z += 7.2f;
            pose.velocity = tide_f3(0, 0, 0);
            tide_set_Pose(&world, e, pose);
            actors++;
        }
        tide_set_Enemy(&world, e, enemy);
    }
    if (actors != 4) { puts("FAIL: cast gallery needs all four enemy kinds"); failures++; }
    healthy_weapon(Weapon_Scattergun);
}

static void fixture(const int state)
{
    if (state == 1) {
        for (int seat = 0; seat < 2; seat++) place(seat, world.GeometryAudit.encounters.at[0]);
        ticks(5);
    } else if (state == 2) {
        sector(2);
        for (int seat = 0; seat < 2; seat++) place(seat, world.GeometryAudit.encounters.at[2 * 6 + 1]);
        ticks(5);
        Marine m = tide_get_Marine(&world, operative(0));
        m.weapon = Weapon_Arc;
        tide_set_Marine(&world, operative(0), m);
    } else if (state == 3) {
        sector(5);
        world.Expedition.triggered = 31;
        world.Expedition.cleared = 31;
        world.Expedition.relays = 7;
        for (int i = 0; i < 5; i++) world.Expedition.beats.at[i] = 3;
        ticks(3);
        for (int seat = 0; seat < 2; seat++) place(seat, world.GeometryAudit.encounters.at[5 * 6 + 4]);
    } else if (state == 4) {
        world.Expedition.phase = Phase_Relic;
        world.Expedition.offers = (tide_int3){.x = 0, .y = 4, .z = 6};
    } else if (state == 5) {
        world.Expedition.phase = Phase_Fight;
        Marine m = tide_get_Marine(&world, operative(0));
        m.health = 0;
        m.downed = true;
        m.revive = 1.0f;
        tide_set_Marine(&world, operative(0), m);
    } else if (state == 6) world.Expedition.phase = Phase_Wipe;
    else if (state == 7) world.Expedition.phase = Phase_Victory;
    else if (state == 8) {
        world.Expedition.phase = Phase_Fight;
        devices.keyboard.m = (tide_button){.held = true, .pressed = true, .down = true};
    } else if (state == 9) healthy_weapon(Weapon_Nailgun);
    else if (state == 10) healthy_weapon(Weapon_Launcher);
    else if (state == 11) gallery();
    else if (state == 12) {
        healthy_weapon(Weapon_Scattergun);
        Marine m = tide_get_Marine(&world, operative(0));
        m.shells = 0;
        tide_set_Marine(&world, operative(0), m);
    }
    if (state < 11) separate_teammate();
    local.Aim.generation = -1;
}

static void check_frame(const int state)
{
    static const char *names[STATES] = {"intake", "combat", "reservoir/arc", "custodian", "relic", "downed", "wipe", "victory", "sector map",
        "nailgun", "launcher", "cast gallery", "emergency sidearm"};
    tide_draw_reset(&list);
    tide_local_frame_time(&local, 1.0f / 60.0f);
    tide_gui_begin(&gui, &devices, tide_f2(WIDTH, HEIGHT), tide_platform_measure_text);
    tide_frame(&world, NULL, 1.0f, NULL, &local, &list, &gui);
    tide_gui_end(&gui, &list);
    for (uint32_t i = 0; i < list.count; i++) {
        // Present is a fullscreen Blit, which uses the same shaded-run
        // payload as meshes but has its own command kind.
        if (list.commands[i].kind != TIDE_DRAW_SHADED && list.commands[i].kind != TIDE_DRAW_BLIT) continue;
        const tide_shader *s = list.runs[list.commands[i].shaded.run].shader;
        if (!s) continue;
        level_seen |= strstr(s->name, "LevelPaint") != NULL;
        actor_seen |= strstr(s->name, "ActorPaint") != NULL;
        gun_seen |= strstr(s->name, "GunPaint") != NULL;
        present_seen |= strstr(s->name, "Present") != NULL;
        actor_snap_seen |= strstr(s->name, "ActorSnap") != NULL;
        shadow_seen |= strstr(s->name, "ShadowPaint") != NULL;
        overlay_seen |= strstr(s->name, "OverlayPaint") != NULL;
    }
    tide_float2 points[SAMPLES];
    uint32_t pixels[SAMPLES];
    for (int i = 0; i < SAMPLES; i++) points[i] = tide_f2(60 + (float)(i % 8) * 120, 70 + (float)(i / 8) * 72);
    tide_platform_read_pixels_as(&list, WIDTH, HEIGHT, 1.0f, points, SAMPLES, pixels);
    int colored = 0, changes = 0;
    for (int i = 0; i < SAMPLES; i++) {
        if ((pixels[i] & UINT32_C(0xffffff00)) != 0) colored++;
        if (i > 0 && pixels[i] != pixels[0]) changes++;
    }
    const bool cast = state != 11 || (tide_list_read_count(&local.heap, local.Art.marines) == 1
        && tide_list_read_count(&local.heap, local.Art.hunters) == 1
        && tide_list_read_count(&local.heap, local.Art.troopers) == 1
        && tide_list_read_count(&local.heap, local.Art.bulwarks) == 1
        && tide_list_read_count(&local.heap, local.Art.wardens) == 1);
    const bool ok = cast && list.count > (state == 8 ? 500u : 10u) && colored > 30 && changes > 8;
    printf("%s: %s actual game view (%u draw commands, %d colored samples, %d variations)\n",
        ok ? "ok" : "FAIL", names[state], list.count, colored, changes);
    if (!ok) failures++;
}

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static int state;
    printf("renderer: %s\n", tide_platform_renderer());
    fixture(state);
    check_frame(state);
    check_frame(state); // Retained meshes, pipelines, textures and targets.
#if !defined(__wasm__)
    if (capture_directory) {
        char path[4096];
        const int length = snprintf(path, sizeof path, "%s/state-%d.png", capture_directory, state);
        tide_platform_draw(&list);
        if (length < 0 || (size_t)length >= sizeof path || !tide_platform_capture(path)) failures++;
    }
#endif
    if (++state < STATES) return TIDE_KEEP_RUNNING;
    const bool shaders = level_seen && actor_seen && gun_seen && present_seen && actor_snap_seen && shadow_seen && overlay_seen;
    printf("%s: all seven private-game shader paths rendered\n", shaders ? "ok" : "FAIL");
    if (!shaders) failures++;
    return failures ? 1 : 0;
}

int main(const int argc, char **argv)
{
#if !defined(__wasm__)
    if (argc == 3 && strcmp(argv[1], "--capture-directory") == 0) capture_directory = argv[2];
    else if (argc != 1) { fprintf(stderr, "usage: %s [--capture-directory <existing-dir>]\n", argv[0]); return 2; }
#else
    (void)argc, (void)argv;
#endif
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, tide_player_from_index(0));
    tide_world_player_joined(&world, tide_player_from_index(1));
    ticks(2);
    world.Expedition.ready = world.Expedition.manned;
    ticks(3);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    local.Rooms_Lobby.started = true;
    local.Rooms_Lobby.page = Rooms_Page_Playing;
    local.Rooms_Lobby.playing = true;
#if !defined(__wasm__)
    if (capture_directory) tide_platform_open_headless(&(tide_headless_desc){.width = WIDTH, .height = HEIGHT, .frames = STATES});
    else
#endif
        tide_platform_open(&(tide_window_desc){.title = "Dead Signal renderer test", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
