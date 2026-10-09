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

enum { WIDTH = 960, HEIGHT = 540, SAMPLES = 48, STATES = 9 };
static tide_world world;
static tide_local local;
static tide_draw_list list;
static tide_gui gui;
static tide_devices devices;
static int failures;
static bool level_seen, actor_seen, gun_seen, present_seen;

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
    }
    local.Aim.generation = -1;
}

static void check_frame(const int state)
{
    static const char *names[STATES] = {"intake", "combat", "reservoir/arc", "custodian", "relic", "downed", "wipe", "victory", "sector map"};
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
    const bool ok = list.count > (state == 8 ? 500u : 10u) && colored > 30 && changes > 8;
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
    if (++state < STATES) return TIDE_KEEP_RUNNING;
    const bool shaders = level_seen && actor_seen && gun_seen && present_seen;
    printf("%s: all four private-game shader paths rendered\n", shaders ? "ok" : "FAIL");
    if (!shaders) failures++;
    return failures ? 1 : 0;
}

int main(void)
{
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
    tide_platform_open(&(tide_window_desc){.title = "Dead Signal renderer test", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(frame, NULL);
}
