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
static tide_float3 projected_corners[2][8];
static tide_float3 projected_fov_edge;

static void views(const int width, const int height)
{
    tide_draw_reset(&draw);
    // The standard host samples filtered game input before drawing a frame.
    // Besides filtering, hide marks that the game reads devices: without it,
    // a controller stick would navigate GUI focus as if this were a menu-only
    // application, then hide subsequent camera sticks from Prepare.
    tide_devices sampled_devices = devices;
    tide_gui_hide(&gui, &sampled_devices);
    tide_gui_begin(&gui, &devices, tide_f2((float)width, (float)height), tide_platform_measure_text);
    tide_local_frame_time(&local, 1.0f / 60.0f);
    tide_frame(&world, NULL, 1, NULL, NULL, 1.0f, &local, &draw, &gui);
    // Coordinate helpers read the destination's current settings. GUI append
    // restores every destination's defaults, so capture the playable camera
    // before gui_end appends its independently drawn widgets.
    tide_draw_list *scene = tide_draw_render_target(&draw, local.SceneView.scene);
    // The shipped wide preset is 110 degrees horizontally at 16:9. Its
    // vertical half-angle tangent is tan(55deg)/(16/9)=.80333325. A ray at
    // that angle must touch the top edge in either aspect ratio. This detects
    // accidentally passing horizontal FOV directly to Tide's vertical API.
    const tide_float3 fov_ray = tide_rotate_q(local.SceneView.cameraRotation, tide_f3(0, 8.0333325f, 10));
    projected_fov_edge = tide_draw_world_to_screen_3d(scene,
        tide_add_f3(local.SceneView.eye, fov_ray), tide_f2((float)width, (float)height));
    for (int seat = 0; seat < 2; seat++) {
        const CarState car = world.MatchState.cars.at[seat];
        // Current RocketSim Octane effective collider and COM offsets. Keep
        // the whole car in the view, without prescribing a vertical screen
        // band that could reject a faithful low chase camera.
        const tide_float3 right = tide_cross_f3(car.up, car.forward);
        const tide_float3 centre = tide_add_f3(tide_add_f3(car.position,
            tide_mul_f3(car.forward, tide_f3_splat(.138757f))),
            tide_mul_f3(car.up, tide_f3_splat(.20755f)));
        projected_cars[seat] = tide_draw_world_to_screen_3d(scene, centre,
            tide_f2((float)width, (float)height));
        for (int corner = 0; corner < 8; corner++) {
            tide_float3 point = tide_add_f3(centre, tide_mul_f3(right,
                tide_f3_splat((corner & 1) ? .433497f : -.433497f)));
            point = tide_add_f3(point, tide_mul_f3(car.up,
                tide_f3_splat((corner & 2) ? .1932955f : -.1932955f)));
            point = tide_add_f3(point, tide_mul_f3(car.forward,
                tide_f3_splat((corner & 4) ? .602535f : -.602535f)));
            projected_corners[seat][corner] = tide_draw_world_to_screen_3d(scene, point,
                tide_f2((float)width, (float)height));
        }
    }
    tide_gui_end(&gui, &draw);
}

static bool car_in_frame(const int width, const int height, const int seat,
                         const char *phase, const int at)
{
    const tide_float3 screen = projected_cars[seat];
    bool visible = true;
    for (int corner = 0; corner < 8; corner++) {
        const tide_float3 point = projected_corners[seat][corner];
        visible &= tide_is_finite_f(point.x) && tide_is_finite_f(point.y)
            && tide_is_finite_f(point.z) && point.z > 0 && point.z < 1
            && point.x >= 2 && point.x <= (float)width - 2
            && point.y >= 2 && point.y <= (float)height - 2;
    }
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
            *car = (CarState){.position = {2, .1701f, -sign * 20}, .forward = {0, 0, sign},
                .up = {0, 1, 0}, .grounded = true, .boost = 33, .team = seat};
            world.MatchState.cars.at[1 - seat].position = tide_f3(-20, .1701f, sign * 30);
            world.MatchState.ball.position = tide_add_f3(car->position, tide_f3(0, .7614f, sign * 12));
            world.MatchState.ball.velocity = tide_f3(0, 0, 0);
            local.SceneView.cameraReady = false;
            local.SceneView.ballCamera = true;
            local.SceneView.forward = tide_f3(0, 0, 1);
            local.SceneView.touch = false;
            local.SceneView.shake = 0;
            local.SceneView.previousCarVelocity = tide_f3(0, 0, 0);
            local.CameraPreferences.fov = 110;
            local.CameraPreferences.distance = 2.7f;
            local.CameraPreferences.height = 1;
            local.CameraPreferences.angle = -3;
            local.CameraPreferences.shake = false;
            for (int at = 0; at < 45; at++) views(width, height);
            if (!car_in_frame(width, height, seat, "ball ahead", 45)) failures++;
            if (local.SceneView.seat != seat) {
                printf("FAIL camera: local player %d followed car %d\n", seat, local.SceneView.seat);
                failures++;
            }
            if (!tide_is_finite_f(projected_fov_edge.y) || tide_abs_f(projected_fov_edge.y) > 1) {
                printf("FAIL wide-preset vertical FOV %dx%d: 110deg/16:9 edge y %.3f\n",
                    width, height, projected_fov_edge.y);
                failures++;
            }
            // Turn ball cam through 180 degrees, then translate at top speed.
            // The car must retain its chase distance while the camera turns;
            // smoothing its absolute eye position used to swallow that gap.
            world.MatchState.ball.position = tide_add_f3(car->position, tide_f3(0, .7614f, -sign * 12));
            car->velocity = tide_f3(0, 0, sign * 23);
            local.SceneView.previousCarVelocity = car->velocity;
            const tide_float3 move = tide_mul_f3(car->velocity, tide_f3_splat(1.0f / 60.0f));
            bool moving_visible = true;
            for (int at = 0; at < 90; at++) {
                car->position = tide_add_f3(car->position, move);
                world.MatchState.ball.position = tide_add_f3(world.MatchState.ball.position, move);
                views(width, height);
                if (!car_in_frame(width, height, seat, "ball behind at 23 m/s", at)) {
                    moving_visible = false;
                    break;
                }
            }
            if (!moving_visible) failures++;
            printf("Afterburn camera %dx%d seat %d: opposite heading and moving ball-cam reversal %s\n",
                width, height, seat, moving_visible ? "visible" : "FAIL");
            // Car cam must obey the selected geometry instead of adding an
            // unrelated absolute-eye spring. Ball cam must aim in 3D: an
            // aerial ball cannot be flattened to a fixed ground-height look.
            car->velocity = tide_f3(0, 0, 0);
            local.SceneView.previousCarVelocity = car->velocity;
            local.SceneView.ballCamera = false;
            local.SceneView.cameraReady = false;
            for (int at = 0; at < 60; at++) views(width, height);
            const tide_float3 offset = tide_sub_f3(local.SceneView.eye, car->position);
            if (tide_abs_f(offset.z + sign * 2.7f) > .01f || tide_abs_f(offset.x) > .01f
                || tide_abs_f(offset.y - 1) > .01f) {
                printf("FAIL car-camera preset seat %d: offset %.3f, %.3f, %.3f\n",
                    seat, offset.x, offset.y, offset.z);
                failures++;
            }
            const tide_float3 car_aim = tide_forward_q(local.SceneView.cameraRotation);
            if (tide_abs_f(car_aim.y + .05233596f) > .002f) {
                printf("FAIL car-camera -3deg pitch: %.5f\n", car_aim.y);
                failures++;
            }
            local.SceneView.ballCamera = true;
            world.MatchState.ball.position = tide_add_f3(car->position, tide_f3(0, 10, sign * 12));
            for (int at = 0; at < 120; at++) views(width, height);
            const tide_float3 to_ball = tide_normalize_f3(tide_sub_f3(world.MatchState.ball.position, local.SceneView.eye));
            if (tide_dot_f3(tide_forward_q(local.SceneView.cameraRotation), to_ball) < .999f) {
                puts("FAIL ball camera: elevated ball is not the actual three-dimensional target");
                failures++;
            }
            // Arena clipping can move the eye sideways and separate the
            // car's bearing from the ball's. These are real surface poses,
            // not a prescribed screen band: retain all eight collider
            // corners from the first automatic frame through convergence.
            const struct {
                const char *name;
                tide_float3 position, forward, up, ball;
            } surfaces[] = {
                {"left wall", {-40.6f,3,30}, {0,0,-1}, {1,0,0}, {0,.9315f,0}},
                {"right wall", {40.6f,3,-30}, {0,0,1}, {-1,0,0}, {0,.9315f,0}},
                {"positive rounded corner", {37.3f,5,47.7f}, {.70710678f,0,-.70710678f},
                    {-.70710678f,0,-.70710678f}, {0,.9315f,0}},
                {"negative rounded corner", {-37.3f,5,-47.7f}, {-.70710678f,0,.70710678f},
                    {.70710678f,0,.70710678f}, {0,.9315f,0}},
                {"ceiling", {10,20.1f,20}, {0,0,1}, {0,-1,0}, {0,.9315f,0}},
                {"exactly vertical ball", {0,.1701f,0}, {0,0,1}, {0,1,0}, {0,16,0}}
            };
            for (size_t pose = 0; pose < sizeof surfaces / sizeof surfaces[0]; pose++) {
                *car = (CarState){.position=surfaces[pose].position, .forward=surfaces[pose].forward,
                    .up=surfaces[pose].up, .grounded=true, .team=seat};
                world.MatchState.ball.position = surfaces[pose].ball;
                local.SceneView.cameraReady = false;
                local.SceneView.ballCamera = true;
                local.SceneView.rearView = false;
                local.SceneView.swivel = tide_f2(0,0);
                local.SceneView.mouseLook = tide_f2(0,0);
                local.SceneView.previousCarVelocity = tide_f3(0,0,0);
                for (int at = 0; at < 60; at++) {
                    views(width,height);
                    if (!car_in_frame(width,height,seat,surfaces[pose].name,at)) {
                        failures++;
                        break;
                    }
                }
            }
        }
    }
    tide_world_copy(&world, &saved);
    tide_world_free(&saved);
    return failures;
}

static int input_regressions(void)
{
    int failures = 0;
    const tide_button held = {.pressed = true, .held = true};
    local.Hand.settingsOpen = false;
    local.Hand.keyboardAerialSafety = false;
    local.Hand.controllerDeadzone = .2f;
    local.Hand.dodgeDeadzone = .8f;
    for (int modern = 0; modern < 2; modern++) {
        local.Hand.profile = modern ? ControlProfile_Modern : ControlProfile_Classic;
        for (int direction = 0; direction < 6; direction++) {
            memset(&devices, 0, sizeof devices);
            if (direction == 0) devices.keyboard.w = held;
            if (direction == 1) devices.keyboard.s = held;
            if (direction == 2) devices.keyboard.a = held;
            if (direction == 3) devices.keyboard.d = held;
            if (direction == 4) devices.keyboard.q = held;
            if (direction == 5) devices.keyboard.e = held;
            views(1280, 720);
            const Controls value = tide_input_sample(&devices, &local);
            const float expected = (direction & 1) ? 1 : -1;
            const bool correct = !value.away && !value.jump && !value.boost && !value.handbrake
                && (direction < 2 ? value.throttle == -expected && value.pitch == expected
                    : direction < 4 ? value.steer == expected && value.yaw == expected : value.roll == expected);
            if (!correct) { printf("FAIL %s keyboard direction %d\n", modern ? "Modern" : "Classic", direction); failures++; }
        }
        // First-party TAInput.ini's Classic/Modern profiles are distinct.
        // Test every action independently so the old Space+Shift fixture
        // cannot pass while jump, boost and ball-camera are interchanged.
        for (int action = 0; action < (modern ? 7 : 5); action++) {
            memset(&devices, 0, sizeof devices);
            if (action == 0) devices.mouse.left = held;
            if (action == 1) devices.mouse.right = held;
            if (action == 2) devices.keyboard.leftShift = held;
            if (action == 3) devices.keyboard.space = held;
            if (action == 4) devices.mouse.middle = held;
            if (action == 5) devices.keyboard.leftCtrl = held;
            if (action == 6) devices.keyboard.leftAlt = held;
            // Space (Classic) and MMB (Modern) toggle on the edge.
            if (!modern && action == 3) devices.keyboard.space.down = true;
            if (modern && action == 4) devices.mouse.middle.down = true;
            const bool ball_camera = local.SceneView.ballCamera;
            views(1280, 720);
            const Controls value = tide_input_sample(&devices, &local);
            const bool jump = modern ? action == 0 || action == 3 : action == 1;
            const bool boost = modern ? action == 2 : action == 0;
            const bool brake = modern ? action == 1 : action == 2;
            const bool rear = modern ? action == 5 : action == 4;
            const bool toggle = modern ? action == 4 : action == 3;
            if (value.away || value.jump != jump || value.boost != boost || value.handbrake != brake
                || local.SceneView.rearView != rear || local.SceneView.ballCamera != (ball_camera != toggle)) {
                printf("FAIL %s keyboard action %d: jump%d boost%d slide%d rear%d camera%d\n",
                    modern ? "Modern" : "Classic", action, value.jump, value.boost, value.handbrake,
                    local.SceneView.rearView, local.SceneView.ballCamera);
                failures++;
            }
        }
        memset(&devices, 0, sizeof devices);
        devices.keyboard.d = held;
        if (modern) devices.mouse.right = held; else devices.keyboard.leftShift = held;
        views(1280, 720);
        const Controls roll = tide_input_sample(&devices, &local);
        if (roll.roll < .99f || roll.yaw != 0 || !roll.handbrake || roll.boost || roll.jump) failures++;
    }
    local.Hand.profile = ControlProfile_Classic;
    local.Hand.keyboardAerialSafety = true;
    const CarState saved = world.MatchState.cars.at[0];
    world.MatchState.cars.at[0].grounded = true;
    memset(&devices, 0, sizeof devices);
    devices.keyboard.w = held;
    views(1280, 720);
    Controls value = tide_input_sample(&devices, &local);
    if (value.throttle != 1 || value.pitch != -1 || !value.suppressPitch || !local.Hand.pitchBlocked) failures++;
    world.MatchState.cars.at[0].grounded = false;
    views(1280, 720);
    value = tide_input_sample(&devices, &local);
    if (value.pitch != -1 || !value.suppressPitch) failures++;
    devices.keyboard.w = (tide_button){0};
    views(1280, 720);
    devices.keyboard.w = held;
    views(1280, 720);
    value = tide_input_sample(&devices, &local);
    if (value.pitch != -1 || value.suppressPitch || local.Hand.pitchBlocked) failures++;
    world.MatchState.cars.at[0] = saved;

    memset(&devices, 0, sizeof devices);
    devices.gamepad.connected = true;
    devices.gamepad.leftStick = tide_f2(.19f, .19f);
    views(1280, 720);
    value = tide_input_sample(&devices, &local);
    if (value.steer != 0 || value.pitch != 0 || value.yaw != 0 || value.roll != 0) failures++;
    devices.gamepad.leftStick = tide_f2(1, 1);
    devices.gamepad.rightTrigger = 1;
    views(1280, 720);
    value = tide_input_sample(&devices, &local);
    if (value.steer != 1 || value.pitch != -1 || value.yaw != 1 || value.throttle != 1
        || value.roll != 0 || value.dodgeDeadzone != .8f) failures++;
    devices.gamepad.rightTrigger = 0;
    devices.gamepad.leftTrigger = 1;
    views(1280, 720);
    value = tide_input_sample(&devices, &local);
    if (value.roll != 1 || value.yaw != 0 || value.throttle != -1 || value.handbrake) failures++;
    devices.gamepad.leftTrigger = 0;
    devices.gamepad.buttonWest = held;
    views(1280, 720);
    value = tide_input_sample(&devices, &local);
    if (!value.handbrake || value.yaw != 1 || value.roll != 0) failures++;
    memset(&devices, 0, sizeof devices);
    devices.gamepad.connected = true;
    devices.gamepad.rightStick = tide_f2(0, 1);
    local.SceneView.ballCamera = false;
    local.SceneView.cameraReady = false;
    for (int at = 0; at < 45; at++) views(1280, 720);
    if (local.SceneView.look.y <= local.SceneView.eye.y) {
        printf("FAIL controller right stick up: orbitPitch%.4f swivelY%.4f mouseLookY%.4f rear%d settings%d playing%d dt%.5f; aim %.3f,%.3f,%.3f\n",
            local.SceneView.orbitPitch, local.SceneView.swivel.y, local.SceneView.mouseLook.y,
            local.SceneView.rearView, local.Hand.settingsOpen, local.Rooms_Lobby.playing, local.SceneView.dt,
            local.SceneView.look.x - local.SceneView.eye.x, local.SceneView.look.y - local.SceneView.eye.y,
            local.SceneView.look.z - local.SceneView.eye.z);
        failures++;
    }
    value = tide_input_sample(&devices, &local);
    if (value.pitch != 0) failures++;

    local.Hand.settingsOpen = true;
    value = tide_input_sample(&devices, &local);
    if (value.away || value.throttle != 0 || value.steer != 0 || value.jump || value.boost) failures++;
    local.Hand.settingsOpen = false;
    local.Rooms_Lobby.playing = false;
    value = tide_input_sample(&devices, &local);
    if (value.away || value.throttle != 0 || value.steer != 0 || value.jump || value.boost) failures++;
    local.Rooms_Lobby.playing = true;
    memset(&devices, 0, sizeof devices);
    puts("Afterburn: Classic/Modern actions, aircraft axes, keyboard safety, controller deadzone/air roll and neutral menus sampled");
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
    puts("Afterburn: portrait touch movement/boost/jump/slide sampled");
    failures += input_regressions();
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
