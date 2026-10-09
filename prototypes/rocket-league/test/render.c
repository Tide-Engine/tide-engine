// The playable views and shaders, exercised through the real GPU backends.
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
static tide_float3 projected_cars[2];

static void views(const int width, const int height)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2((float)width, (float)height), tide_platform_measure_text);
    tide_local_frame_time(&local, 1.0f / 60.0f);
    tide_frame(&world, NULL, 1, NULL, &local, &draw, &gui);
    // Coordinate helpers read the destination's current settings. GUI append
    // restores every destination's defaults, so capture the playable camera
    // before gui_end appends its independently drawn widgets.
    tide_draw_list *scene = tide_draw_render_target(&draw, local.SceneView.scene);
    for (int seat = 0; seat < 2; seat++) {
        const CarState car = world.MatchState.cars.at[seat];
        const tide_float3 centre = tide_add_f3(car.position, tide_mul_f3(car.up, tide_f3_splat(0.1f)));
        projected_cars[seat] = tide_draw_world_to_screen_3d(scene, centre,
            tide_f2((float)width, (float)height));
    }
    tide_gui_end(&gui, &draw);
}

static bool car_in_frame(const int width, const int height, const int seat,
                         const char *phase, const int at)
{
    const tide_float3 screen = projected_cars[seat];
    const bool visible = tide_is_finite_f(screen.x) && tide_is_finite_f(screen.y)
        && screen.z > 0 && screen.z < 1 && screen.x >= (float)width * 0.25f
        && screen.x <= (float)width * 0.75f && screen.y >= (float)height * 0.55f
        && screen.y <= (float)height * 0.90f;
    if (!visible) printf("FAIL camera %dx%d seat %d %s frame %d: car projects to %.2f, %.2f, %.5f; eye %.2f, %.2f, %.2f\n",
        width, height, seat, phase, at, screen.x, screen.y, screen.z,
        local.SceneView.eye.x, local.SceneView.eye.y, local.SceneView.eye.z);
    return visible;
}

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
            tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(seat), 0, true, true, "AAAAAA");
            local.Rooms_Lobby.page = Rooms_Page_Playing;
            local.Rooms_Lobby.started = true;
            local.Rooms_Lobby.playing = true;
            world.MatchState.phase = MatchPhase_Playing;
            const float sign = seat == 0 ? 1.0f : -1.0f;
            CarState *car = &world.MatchState.cars.at[seat];
            *car = (CarState){.position = {2, 0.38f, -sign * 20}, .forward = {0, 0, sign},
                .up = {0, 1, 0}, .grounded = true, .boost = 33, .team = seat};
            world.MatchState.cars.at[1 - seat].position = tide_f3(-20, 0.38f, sign * 30);
            world.MatchState.ball.position = tide_add_f3(car->position, tide_f3(0, 0.535f, sign * 12));
            world.MatchState.ball.velocity = tide_f3(0, 0, 0);
            local.SceneView.cameraReady = false;
            local.SceneView.ballCamera = true;
            local.SceneView.forward = tide_f3(0, 0, 1);
            local.SceneView.touch = false;
            local.SceneView.shake = 0;
            local.SceneView.previousCarVelocity = tide_f3(0, 0, 0);
            for (int at = 0; at < 45; at++) views(width, height);
            if (!car_in_frame(width, height, seat, "ball ahead", 45)) failures++;
            if (local.SceneView.seat != seat) {
                printf("FAIL camera: local player %d followed car %d\n", seat, local.SceneView.seat);
                failures++;
            }
            // Turn ball cam through 180 degrees, then translate at top speed.
            // The car must retain its chase distance while the camera turns;
            // smoothing its absolute eye position used to swallow that gap.
            world.MatchState.ball.position = tide_add_f3(car->position, tide_f3(0, 0.535f, -sign * 12));
            car->velocity = tide_f3(0, 0, sign * 23);
            local.SceneView.previousCarVelocity = car->velocity;
            const tide_float3 move = tide_mul_f3(car->velocity, tide_f3_splat(1.0f / 60.0f));
            bool moving_visible = true;
            for (int at = 0; at < 90; at++) {
                car->position = tide_add_f3(car->position, move);
                world.MatchState.ball.position = tide_add_f3(world.MatchState.ball.position, move);
                views(width, height);
                if (at >= 30 && !car_in_frame(width, height, seat, "ball behind at 23 m/s", at)) {
                    moving_visible = false;
                    break;
                }
            }
            if (!moving_visible) failures++;
            printf("Afterburn camera %dx%d seat %d: opposite heading and moving ball-cam reversal %s\n",
                width, height, seat, moving_visible ? "visible" : "FAIL");
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
        for (int retained = 0; retained < 2; retained++) {
            views(width, height);
            if (local.SceneView.touch != (size == 1)) failures++;
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
            printf("Afterburn %dx%d retained %d: %u commands, %d colored, %d varied samples\n",
                width, height, retained, draw.count, colored, varied);
            if (draw.count < 20 || colored < 45 || varied < 20) failures++;
        }
        if (capture_directory && size == 0) {
            char filename[4096];
            const int length = snprintf(filename, sizeof filename, "%s/afterburn-desktop.png", capture_directory);
            tide_platform_draw(&draw);
            if (length < 0 || (size_t)length >= sizeof filename || !tide_platform_capture(filename)) failures++;
        }
    }
    // Run the portrait controls through their real view-to-Sample path.
    // Action fingers must stay out of the movement joystick.
    devices.touchscreen.connected = true;
    const tide_float2 actions[] = {{326, 728}, {237, 769}, {326, 633}};
    for (int action = 0; action < 3; action++) {
        memset(&devices.touchscreen.touches, 0, sizeof devices.touchscreen.touches);
        devices.touchscreen.touches.at[0] = (tide_touch){.id = 1,
            .position = {actions[action].x, 844 - actions[action].y},
            .startPosition = {actions[action].x, 844 - actions[action].y},
            .press = {.pressed = true, .held = true}};
        views(390, 844);
        const Controls sampled = tide_input_sample(&devices, &local);
        if (sampled.away || sampled.boost != (action == 0) || sampled.jump != (action == 1)
            || sampled.handbrake != (action == 2) || sampled.throttle != 0 || sampled.steer != 0) failures++;
    }
    memset(&devices.touchscreen.touches, 0, sizeof devices.touchscreen.touches);
    devices.touchscreen.touches.at[0] = (tide_touch){.id = 1,
        .position = {82, 172}, .startPosition = {82, 118},
        .press = {.pressed = true, .held = true}};
    views(390, 844);
    const Controls joystick = tide_input_sample(&devices, &local);
    if (joystick.throttle < 0.95f || joystick.boost || joystick.jump || joystick.handbrake) failures++;
    memset(&devices, 0, sizeof devices);
    devices.keyboard.w = (tide_button){.pressed = true, .held = true};
    devices.keyboard.leftShift = (tide_button){.pressed = true, .held = true};
    devices.keyboard.space = (tide_button){.pressed = true, .held = true};
    views(1280, 720);
    const Controls keyboard = tide_input_sample(&devices, &local);
    if (keyboard.throttle < 0.95f || keyboard.pitch > -0.95f || !keyboard.boost || !keyboard.jump) failures++;
    puts("Afterburn: keyboard and portrait touch movement/boost/jump/slide sampled");
    failures += camera_regressions();
    if (tide_world_hash(&world) != hash) failures++;
    printf("AFTERBURN_RENDER_%s\n", failures ? "FAIL" : "PASS");
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
    for (int i = 0; i < 380; i++) tide_world_tick(&world);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, true, "AAAAAA");
    local.Rooms_Lobby.page = Rooms_Page_Playing;
    local.Rooms_Lobby.started = true;
    local.Rooms_Lobby.playing = true;
    if (capture_directory)
        tide_platform_open_headless(&(tide_headless_desc){.width = 1280, .height = 720, .frames = 1});
    else tide_platform_open(&(tide_window_desc){.title = "Afterburn renderer verification", .width = 1280, .height = 720, .hidden = true});
    tide_platform_run(frame, NULL);
}
