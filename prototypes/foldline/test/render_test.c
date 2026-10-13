#include "foldline_render_test.h"
#include "tide/platform.h"

#include <stdio.h>
#include <stdlib.h>

enum { WIDTH = 1280, HEIGHT = 720, GRID_X = 21, GRID_Y = 31, SAMPLES = GRID_X * GRID_Y };

typedef struct image {
    tide_float2 points[SAMPLES];
    uint32_t pixels[SAMPLES];
    int count;
} image;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static int failures;
static bool scene_samples;

static void check(bool passed, const char *message)
{
    if (!passed) {
        fprintf(stderr, "FOLDLINE portal rendering failure: %s\n", message);
        failures++;
    }
}

static int color_distance(uint32_t a, uint32_t b)
{
    int distance = 0;
    for (int shift = 8; shift <= 24; shift += 8) {
        int difference = abs((int)((a >> shift) & 255) - (int)((b >> shift) & 255));
        if (difference > distance) distance = difference;
    }
    return distance;
}

static int changed(const image *a, const image *b)
{
    int count = 0;
    for (int i = 0; i < a->count; i++)
        if (color_distance(a->pixels[i], b->pixels[i]) > 14) count++;
    return count;
}

static float changed_center(const image *empty, const image *occupied)
{
    float sum = 0;
    int count = 0;
    for (int i = 0; i < empty->count; i++) {
        if (color_distance(empty->pixels[i], occupied->pixels[i]) > 14) {
            sum += occupied->points[i].x;
            count++;
        }
    }
    check(count >= 8, "a destination cube must cover several portal pixels");
    return count ? sum / (float)count : 0;
}

// The cube, where the renderer reads it: its own component, and the fold's copy of the first loose thing.
static void cube_at(tide_float3 position)
{
    Cube cube = tide_get_Cube(&world, world.Arena.cube);
    cube.at = position;
    cube.turn = (tide_quaternion){0, 0, 0, 1};
    check(tide_set_Cube(&world, world.Arena.cube, cube), "test cube exists");
    world.Fold.cube = position;
}

static void operator_at(tide_entity entity, tide_float3 position, float yaw)
{
    Operator body = tide_get_Operator(&world, entity);
    body.position = position;
    body.yaw = yaw;
    body.pitch = 0;
    check(tide_set_Operator(&world, entity, body), "test operator exists");
}

static void record(image *out)
{
    const tide_float2 screen = {WIDTH, HEIGHT};
    const tide_devices devices = {0};
    local.Interface.open = false;
    local.Interface.looking = false;
    local.Interface.introduced = true;
    local.Control.active = 0;
    local.Control.started = true;
    local.Optics.warmed = 3; // Past the first frames, which are drawn with folds that aren't there
    tide_local_frame_time(&local, 0);
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw, screen);
    tide_gui_begin(&gui, &devices, screen, tide_platform_measure_text);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);

    // Independently project the source aperture, for a yaw-zero camera.
    const Operator viewer = tide_get_Operator(&world, world.Arena.op0);
    const float focal = HEIGHT * 0.5f / tide_tan_f(75 * TIDE_DEG2RAD_F * 0.5f);
    const float center_x = scene_samples ? WIDTH * 0.5f : WIDTH * 0.5f + (-5 - viewer.position.x) * focal / (2.4f - viewer.position.z);
    out->count = 0;
    for (int row = 0; row < GRID_Y; row++) {
        for (int column = 0; column < GRID_X; column++) {
            const float x = center_x + (column - GRID_X / 2) * 7;
            const float y = HEIGHT * 0.5f + (row - GRID_Y / 2) * 7;
            // The crosshair is presentation, outside this image assertion.
            if (tide_abs_f(x - WIDTH * 0.5f) < 20 && tide_abs_f(y - HEIGHT * 0.5f) < 20) continue;
            out->points[out->count++] = (tide_float2){x, y};
        }
    }
    tide_platform_read_pixels_as(&draw, WIDTH, HEIGHT, 1, out->points, out->count, out->pixels);
}

static void prepare(float camera_x)
{
    operator_at(world.Arena.op0, (tide_float3){camera_x, 0, -1.5f}, 0);
    operator_at(world.Arena.op1, (tide_float3){4, 0, -10}, 0);
    // One end on the divider before the viewer, the other on the far room's side wall
    world.Fold.ends.at[0] = (Portal){.open = true, .at = {-5, 1.55f, 2.4f}, .face = {0, 0, -1}, .size = 1};
    world.Fold.ends.at[1] = (Portal){.open = true, .at = {-9.8f, 1.55f, 10}, .face = {1, 0, 0}, .size = 1};
    cube_at((tide_float3){-3, 0.5f, -8});
    world.Fold.cubeTurn = (tide_quaternion){0, 0, 0, 1};
    world.Fold.carried = -1;
}

static int red_interior(const image *seen)
{
    int red = 0;
    for (int i = 0; i < seen->count; i++) {
        if (tide_abs_f(seen->points[i].x - WIDTH * .5f) > 35 || tide_abs_f(seen->points[i].y - HEIGHT * .5f) > 60) continue;
        const int r = (seen->pixels[i] >> 24) & 255, g = (seen->pixels[i] >> 16) & 255, b = (seen->pixels[i] >> 8) & 255;
        if (r > g + 35 && r > b + 35) red++;
    }
    return red;
}

static void select_level(int level)
{
    uint8_t payload[11] = {0};
    payload[0] = (uint8_t)TIDE_COMMAND_ID_SelectChamber;
    payload[1] = (uint8_t)(TIDE_COMMAND_ID_SelectChamber >> 8);
    payload[10] = (uint8_t)(level * 2);
    const tide_player_id owner = tide_player_from_index(0);
    const uint32_t sequence = tide_game_api.command_sequence(&world, owner, 0) + 1;
    check(tide_game_api.command(&world, owner, 0, sequence, payload, sizeof payload), "select render fixture room");
    tide_world_tick(&world);
    tide_world_tick(&world);
}

static void body_at(tide_entity entity, tide_float3 feet)
{
    Operator body = tide_get_Operator(&world, entity);
    Box3D_Body rigid = tide_get_Box3D_Body(&world, entity);
    body.position = feet;
    body.yaw = 0;
    body.pitch = 0;
    body.last = (tide_float3){feet.x, feet.y + .9f, feet.z};
    rigid.position = body.last;
    rigid.velocity = (tide_float3){0};
    check(tide_set_Operator(&world, entity, body) && tide_set_Box3D_Body(&world, entity, rigid), "position lift riders");
}

static void transition_checks(void)
{
    static const struct { int level; tide_float3 lift, arrival; } route[] = {
        {0, {0,0,15.6f}, {6.5f,0,-15.6f}}, {1, {0,0,15.6f}, {6.5f,0,-15.6f}},
        {6, {0,0,15.6f}, {6.5f,0,-15.6f}}, {3, {0,0,15.6f}, {0,0,-24}},
        {12, {0,0,16.6f}, {6.5f,0,-15.6f}}, {7, {0,0,15.6f}, {6.5f,0,-15.6f}},
        {4, {0,0,15.6f}, {6.5f,0,-15.6f}}, {5, {0,0,15.6f}, {6.5f,0,-15.6f}},
        {8, {0,0,15.6f}, {0,0,-24}}, {10, {0,0,12.6f}, {0,0,-20}},
        {9, {0,5,26.6f}, {0,0,-24}}, {11, {10,0,12.6f}, {6.5f,0,-15.6f}},
        {6, {0,0,15.6f}, {6.5f,0,-15.6f}}
    };
    scene_samples = true;
    for (int stop = 0; stop < 13; stop++) {
        select_level(route[stop].level);
        world.Arena.depth = stop; // The second maze is the last stop, not the first occurrence.
        world.Arena.arrive = 2;
        world.Arena.gate = 0;
        world.Arena.ride = 0;
        const tide_float3 lift = route[stop].lift, arrival = route[stop].arrival;
        body_at(world.Arena.op0, (tide_float3){lift.x - .8f, lift.y, lift.z + .2f});
        body_at(world.Arena.op1, (tide_float3){lift.x + 1.2f, lift.y, lift.z + .2f});
        for (int step = 0; step < 10; step++) tide_world_tick(&world); // Settle physical placement before measuring continuity.
        world.Arena.ride = 0;
        const Operator departed_a = tide_get_Operator(&world, world.Arena.op0), departed_b = tide_get_Operator(&world, world.Arena.op1);
        image before, crossing, after;
        record(&before);
        const int chamber = world.Arena.chamber;
        world.Arena.ride = 1.3f - 1.0f / 60;
        for (int step = 0; step < 3 && world.Arena.chamber == chamber; step++) tide_world_tick(&world);
        check(world.Arena.chamber == chamber + 1, "every lift advances to its next room");
        record(&crossing); // Includes the first frame before the arriving bodies are updated.
        int dark = 0;
        for (int i = 0; i < crossing.count; i++)
            if ((crossing.pixels[i] >> 8) == 0) dark++;
        check(dark > crossing.count * 9 / 10, "the scene change occurs inside the lift blackout");
        for (int step = 0; step < 31; step++) tide_world_tick(&world);
        record(&after);
        const Operator a = tide_get_Operator(&world, world.Arena.op0), b = tide_get_Operator(&world, world.Arena.op1);
        check(tide_abs_f(a.position.x - (arrival.x + departed_a.position.x - lift.x)) < .05f && tide_abs_f(a.position.z - (arrival.z + departed_a.position.z - lift.z)) < .05f
              && tide_abs_f(a.position.y - arrival.y) < .05f, "camera retains its position relative to the lift");
        check(tide_abs_f(b.position.x - (arrival.x + departed_b.position.x - lift.x)) < .05f && tide_abs_f(b.position.z - (arrival.z + departed_b.position.z - lift.z)) < .05f,
              "partner retains their position relative to the lift");
        check(local.Lens.lift < .01f, "arrival fades back in before its door opens");
        // The shutter is still closed, at the same local plane, throughout the reveal.
        check(world.Arena.arrive < .7f, "geometry comparison precedes the opening door");
        check(changed(&crossing, &after) > after.count / 2, "arrival reveals a rendered lift rather than remaining black");
    }
    scene_samples = false;
}

static int render_checks(void *user, float seconds)
{
    (void)user;
    (void)seconds;
    image empty, occupied, moved, wall;

    prepare(-5);
    record(&empty);
    int spread = 0;
    for (int i = 1; i < empty.count; i++) {
        const int distance = color_distance(empty.pixels[0], empty.pixels[i]);
        if (distance > spread) spread = distance;
    }
    check(spread > 30, "portal interior contains destination geometry rather than a flat fill");
    world.Fold.ends.at[1].open = false;
    record(&wall);
    check(changed(&empty, &wall) >= 40, "a linked opening shows a different image from the solid source wall");

    prepare(-5);
    cube_at((tide_float3){-7, 1.55f, 10.6f});
    record(&occupied);
    check(changed(&empty, &occupied) >= 8, "moving the cube beyond the exit updates the portal in the next drawn frame");
    cube_at((tide_float3){-7, 1.55f, 9.4f});
    record(&moved);
    check(changed(&occupied, &moved) >= 8, "a cube moving sideways beyond the exit moves in the portal image");

    prepare(-5);
    operator_at(world.Arena.op1, (tide_float3){-7, 0, 10.6f}, TIDE_PI_F * 0.5f);
    record(&occupied);
    check(changed(&empty, &occupied) >= 8, "the other operator is rendered live beyond the exit");

    // Geometry between the virtual eye and exit plane must be discarded. The cube is further behind the wall
    // than anything part of the way through a fold is, which would also be drawn at the fold's other end.
    prepare(-5);
    cube_at((tide_float3){-13, 1.55f, 10.6f});
    record(&occupied);
    check(changed(&empty, &occupied) <= 4, "the exit plane clips a cube on the camera side of its wall");

    // The cube unfolds to (-5.6, 1.55, 5.2). Its image follows that depth,
    // rather than sliding rigidly with a picture painted on the source wall.
    prepare(-5.5f);
    record(&empty);
    cube_at((tide_float3){-7, 1.55f, 10.6f});
    record(&occupied);
    const float left_center = changed_center(&empty, &occupied);
    prepare(-4.5f);
    record(&empty);
    cube_at((tide_float3){-7, 1.55f, 10.6f});
    record(&occupied);
    const float right_center = changed_center(&empty, &occupied);
    const float focal = HEIGHT * 0.5f / tide_tan_f(75 * TIDE_DEG2RAD_F * 0.5f);
    const float expected_left = WIDTH * 0.5f + (-5.6f + 5.5f) * focal / 6.7f;
    const float expected_right = WIDTH * 0.5f + (-5.6f + 4.5f) * focal / 6.7f;
    printf("Portal cube projection: left %.1f (%.1f), right %.1f (%.1f)\n", left_center, expected_left, right_center, expected_right);
    check(tide_abs_f(left_center - expected_left) < 22, "left camera agrees with independently projected destination cube");
    check(tide_abs_f(right_center - expected_right) < 22, "right camera agrees with independently projected destination cube");
    check(left_center - right_center > 35 && left_center - right_center < 90,
          "lateral camera movement preserves destination depth parallax");

    // A destination below the blood is opaque red, both directly and inside the other pair.
    prepare(-5);
    world.Fold.ends.at[1].at = (tide_float3){-9.8f, -2, 0};
    record(&occupied);
    printf("Submerged portal interior red samples: %d\n", red_interior(&occupied));
    check(red_interior(&occupied) > 90, "a submerged destination shows opaque red instead of dry geometry");
    prepare(-5);
    world.Fold.ends.at[2] = (Portal){.open=true, .at={-7.5f,1.55f,10}, .face={-1,0,0}, .size=1};
    world.Fold.ends.at[3] = (Portal){.open=true, .at={-9.8f,-2,0}, .face={1,0,0}, .size=1};
    record(&occupied);
    printf("Nested submerged portal interior red samples: %d\n", red_interior(&occupied));
    check(red_interior(&occupied) > 55, "a submerged destination remains red through the other operator's fold");
    transition_checks();

    tide_draw_forget(&draw);
    tide_draw_free(&draw);
    tide_draw_free(&gui.list);
    tide_local_free(&local);
    tide_render_targets_free();
    tide_world_free(&world);
    if (failures) return 1;
    puts("FOLDLINE portal rendering checks passed");
    return 0;
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, tide_player_from_index(0));
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    tide_platform_open(&(tide_window_desc){.title = "FOLDLINE portal rendering", .width = WIDTH, .height = HEIGHT, .hidden = true});
    tide_platform_run(render_checks, NULL);
}
