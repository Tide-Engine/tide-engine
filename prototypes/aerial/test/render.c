// Aerial's views and shaders through the real GPU backends: that they draw
// at a desktop's size and a phone's, that keys and touches reach the input,
// that the camera keeps its car in the picture, and that none of it touches
// the match.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/platform.h"
#include "tide/session.h"

extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static const char *capture_directory;
static tide_float3 projected[2];

static void views(const int width, const int height)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2((float)width, (float)height), tide_platform_measure_text);
    tide_local_frame_time(&local, 1.0f / 60.0f);
    tide_frame(&world, NULL, 1, NULL, &local, &draw, &gui);
    // Where each car lands on the screen, asked while the scene's camera is
    // still the frame's: the GUI's widgets put every destination's defaults back
    tide_draw_list *scene = tide_draw_render_target(&draw, local.Look.scene);
    for (int seat = 0; seat < 2; seat++)
        projected[seat] = tide_draw_world_to_screen_3d(scene, world.MatchState.cars.at[seat].position,
            tide_f2((float)width, (float)height));
    tide_gui_end(&gui, &draw);
}

static bool car_in_frame(const int width, const int height, const int seat, const char *when, const int at)
{
    const tide_float3 screen = projected[seat];
    const bool visible = tide_is_finite_f(screen.x) && tide_is_finite_f(screen.y)
        && screen.z > 0 && screen.z < 1 && screen.x >= (float)width * 0.2f
        && screen.x <= (float)width * 0.8f && screen.y >= (float)height * 0.45f
        && screen.y <= (float)height * 0.95f;
    if (!visible) printf("FAIL camera %dx%d seat %d %s frame %d: car at %.1f, %.1f, %.4f; eye %.2f, %.2f, %.2f\n",
        width, height, seat, when, at, screen.x, screen.y, screen.z, local.Look.eye.x, local.Look.eye.y, local.Look.eye.z);
    return visible;
}

static void playing(const int seat)
{
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(seat), 0, true, true, "AAAAAA");
    local.Rooms_Lobby.page = Rooms_Page_Playing;
    local.Rooms_Lobby.started = true;
    local.Rooms_Lobby.playing = true;
}

// Each side's car, facing its way down the pitch, with the ball ahead of it
// and then behind it at full speed: the car stays in the lower middle of the
// picture while the ball camera swings right round.
static int camera_regressions(void)
{
    tide_world saved = {0};
    tide_world_copy(&saved, &world);
    int failures = 0;
    const int sizes[][2] = {{1280, 720}, {390, 844}};
    for (int size = 0; size < 2; size++) {
        const int width = sizes[size][0], height = sizes[size][1];
        for (int seat = 0; seat < 2; seat++) {
            memset(&devices, 0, sizeof devices);
            devices.touchscreen.connected = size == 1;
            playing(seat);
            world.MatchState.phase = MatchPhase_Playing;
            const float way = seat == 0 ? 1.0f : -1.0f;
            CarState *car = &world.MatchState.cars.at[seat];
            *car = (CarState){.position = {2, 0.17f, -way * 20}, .rotation = {{0, seat == 0 ? 0.0f : 1.0f, 0, seat == 0 ? 1.0f : 0.0f}},
                .boost = 33, .wheels = 4, .grounded = true};
            world.MatchState.cars.at[1 - seat].position = tide_f3(-20, 0.17f, way * 30);
            world.MatchState.ball.position = tide_add_f3(car->position, tide_f3(0, 0.74f, way * 12));
            world.MatchState.ball.velocity = tide_f3(0, 0, 0);
            world.MatchState.ball.live = true;
            local.Look.placed = false;
            local.Look.ballCam = true;
            local.Look.shake = 0;
            for (int at = 0; at < 45; at++) views(width, height);
            if (!car_in_frame(width, height, seat, "ball ahead", 45)) failures++;
            if (local.Look.seat != seat) {
                printf("FAIL camera: player %d was shown car %d\n", seat, local.Look.seat);
                failures++;
            }
            world.MatchState.ball.position = tide_add_f3(car->position, tide_f3(0, 0.74f, -way * 12));
            car->velocity = tide_f3(0, 0, way * 23);
            const tide_float3 move = tide_mul_f3(car->velocity, tide_f3_splat(1.0f / 60.0f));
            bool kept = true;
            for (int at = 0; at < 90 && kept; at++) {
                car->position = tide_add_f3(car->position, move);
                world.MatchState.ball.position = tide_add_f3(world.MatchState.ball.position, move);
                views(width, height);
                if (at >= 40) kept = car_in_frame(width, height, seat, "ball behind at 23 m/s", at);
            }
            if (!kept) failures++;
            // The car camera too, along the car's nose
            local.Look.ballCam = false;
            for (int at = 0; at < 60; at++) views(width, height);
            if (!car_in_frame(width, height, seat, "car camera", 60)) failures++;
            printf("Aerial camera %dx%d seat %d: %s\n", width, height, seat, kept ? "car kept in the picture" : "FAIL");
        }
    }
    tide_world_copy(&world, &saved);
    tide_world_free(&saved);
    return failures;
}

static int frame(void *unused, const float seconds)
{
    (void)unused, (void)seconds;
    int failures = 0;
    const uint64_t hash = tide_world_hash(&world);
    const int sizes[][2] = {{1280, 720}, {390, 844}};
    printf("renderer: %s\n", tide_platform_renderer());
    for (int size = 0; size < 2; size++) {
        const int width = sizes[size][0], height = sizes[size][1];
        devices.touchscreen.connected = size == 1;
        for (int again = 0; again < 2; again++) {
            views(width, height);
            if (local.Hand.touch != (size == 1)) failures++;
            tide_float2 points[60];
            uint32_t pixels[60];
            for (int i = 0; i < 60; i++) points[i] = tide_f2(
                (float)width * ((float)(i % 10) + 0.5f) / 10.0f,
                (float)height * ((float)(i / 10) + 0.5f) / 6.0f);
            tide_platform_read_pixels_as(&draw, width, height, 1, points, 60, pixels);
            int colored = 0, varied = 0;
            for (int i = 0; i < 60; i++) {
                if ((pixels[i] & UINT32_C(0xffffff00)) != 0) colored++;
                if (pixels[i] != pixels[0]) varied++;
            }
            printf("Aerial %dx%d frame %d: %u commands, %d colored, %d varied samples\n",
                width, height, again, draw.count, colored, varied);
            if (draw.count < 20 || colored < 45 || varied < 20) failures++;
        }
        if (capture_directory && size == 0) {
            char filename[4096];
            const int length = snprintf(filename, sizeof filename, "%s/aerial-desktop.png", capture_directory);
            tide_platform_draw(&draw);
            if (length < 0 || (size_t)length >= sizeof filename || !tide_platform_capture(filename)) failures++;
        }
    }
    // A phone held upright: each button's finger presses that button alone,
    // and none of them moves the stick
    devices.touchscreen.connected = true;
    const tide_float2 buttons[] = {{316, 740}, {222, 774}, {320, 638}};
    for (int button = 0; button < 3; button++) {
        memset(&devices.touchscreen.touches, 0, sizeof devices.touchscreen.touches);
        devices.touchscreen.touches.at[0] = (tide_touch){.id = 1,
            .position = {buttons[button].x, 844 - buttons[button].y},
            .startPosition = {buttons[button].x, 844 - buttons[button].y},
            .press = {.pressed = true, .held = true}};
        views(390, 844);
        const Controls sampled = tide_input_sample(&devices, &local);
        if (sampled.away || sampled.boost != (button == 0) || sampled.jump != (button == 1)
            || sampled.handbrake != (button == 2) || sampled.throttle != 0 || sampled.steer != 0) failures++;
    }
    // ...and the stick pushed forward is the throttle, and the nose down in the air
    memset(&devices.touchscreen.touches, 0, sizeof devices.touchscreen.touches);
    devices.touchscreen.touches.at[0] = (tide_touch){.id = 1,
        .position = {96, 844 - 732 + 54}, .startPosition = {96, 844 - 732},
        .press = {.pressed = true, .held = true}};
    views(390, 844);
    const Controls stick = tide_input_sample(&devices, &local);
    if (stick.throttle < 0.95f || stick.pitch > -0.95f || stick.boost || stick.jump || stick.handbrake) failures++;
    // The camera button switches cameras, once a press
    const bool was = local.Look.ballCam;
    memset(&devices.touchscreen.touches, 0, sizeof devices.touchscreen.touches);
    devices.touchscreen.touches.at[0] = (tide_touch){.id = 2,
        .position = {224, 844 - 676}, .startPosition = {224, 844 - 676},
        .press = {.pressed = true, .held = true, .down = true}};
    views(390, 844);
    if (local.Look.ballCam == was) failures++;
    memset(&devices, 0, sizeof devices);
    devices.keyboard.w = (tide_button){.pressed = true, .held = true};
    devices.keyboard.leftShift = (tide_button){.pressed = true, .held = true};
    devices.keyboard.space = (tide_button){.pressed = true, .held = true};
    devices.keyboard.d = (tide_button){.pressed = true, .held = true};
    views(1280, 720);
    const Controls keys = tide_input_sample(&devices, &local);
    if (keys.throttle < 0.95f || keys.pitch > -0.95f || keys.steer < 0.95f || !keys.boost || !keys.jump) failures++;
    // Powerslide held, the same stick rolls the car where it would have turned it
    devices.keyboard.leftCtrl = (tide_button){.pressed = true, .held = true};
    views(1280, 720);
    const Controls rolling = tide_input_sample(&devices, &local);
    if (!rolling.handbrake || rolling.roll < 0.95f || rolling.yaw != 0) failures++;
    puts("Aerial: keys, the touch stick and each touch button sampled");
    failures += camera_regressions();
    if (tide_world_hash(&world) != hash) {
        puts("FAIL: drawing changed the match");
        failures++;
    }
    printf("AERIAL_RENDER_%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

int main(const int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--capture-directory") == 0) capture_directory = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "usage: %s [--capture-directory <existing-dir>]\n", argv[0]);
        return 2;
    }
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    tide_world_init(&world, 1.0f / 120.0f);
    tide_world_player_joined(&world, tide_player_from_index(0));
    tide_world_player_joined(&world, tide_player_from_index(1));
    for (int i = 0; i < 420; i++) tide_world_tick(&world);
    tide_local_init(&local);
    playing(0);
    if (capture_directory)
        tide_platform_open_headless(&(tide_headless_desc){.width = 1280, .height = 720, .frames = 1});
    else tide_platform_open(&(tide_window_desc){.title = "Aerial renderer verification", .width = 1280, .height = 720, .hidden = true});
    tide_platform_run(frame, NULL);
}
