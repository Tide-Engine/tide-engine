// Car soccer rules on the generated match, with real owned input and snapshots.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide/session.h"
#include "tide_test.h"

#define DT (1.0f / 120.0f)
#define REST_HEIGHT 0.1701f
#define SCRIPTED_HASH UINT64_C(0xe6230c01396f6826)
static tide_world world;

static tide_player_id player(const int seat) { return tide_player_from_index(seat); }

static void input(tide_world *w, const int seat, Controls control)
{
    control.away = false;
    if (control.dodgeDeadzone == 0) control.dodgeDeadzone = .8f;
    tide_world_set_input(w, player(seat), control);
}

static void tick(const int count)
{
    // Convenient 60 Hz fixture frames; each contains two real 120 Hz ticks.
    for (int i = 0; i < count * 2; i++) tide_world_tick(&world);
}

static void ticks(const int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(&world);
}

static bool near(const char *label, const float actual, const float expected, const float tolerance)
{
    const bool valid = tide_is_finite_f(actual) && tide_abs_f(actual - expected) <= tolerance;
    if (!valid) printf("    %s: %.6f, expected %.6f +/- %.6f\n", label, actual, expected, tolerance);
    return valid;
}

static void start(const int humans)
{
    tide_world_free(&world);
    tide_world_init(&world, DT);
    for (int i = 0; i < humans; i++) {
        tide_world_player_joined(&world, player(i));
        input(&world, i, (Controls){0});
    }
    tick(2);
}

static CarState car(const tide_float3 position, const float forward)
{
    return (CarState){.position = position, .forward = {0, 0, forward}, .up = {0, 1, 0},
        .boost = 100, .grounded = true};
}

static void fixture(void)
{
    start(2);
    world.MatchState.phase = MatchPhase_Playing;
    world.MatchState.phaseTime = 0;
    world.MatchState.remaining = 300;
    world.MatchState.cars.at[0] = car(tide_f3(3, REST_HEIGHT, -24), 1);
    world.MatchState.cars.at[1] = car(tide_f3(-25, REST_HEIGHT, 25), -1);
    world.MatchState.cars.at[1].team = 1;
    world.MatchState.ball = (BallState){.position = {20, 0.915f, 5}, .lastTouch = -1};
}

static float speed(const CarState value)
{
    return tide_sqrt_f(value.velocity.x * value.velocity.x + value.velocity.y * value.velocity.y
        + value.velocity.z * value.velocity.z);
}

static bool finite3(const tide_float3 value)
{
    return tide_is_finite_f(value.x) && tide_is_finite_f(value.y) && tide_is_finite_f(value.z);
}

TIDE_TEST(afterburn_two_seats_and_bots_replace_departed_players)
{
    start(0);
    TIDE_CHECK(world.MatchState.initialized);
    TIDE_CHECK(world.MatchState.humans == 0);
    TIDE_CHECK(world.MatchState.cars.at[0].team == 0 && world.MatchState.cars.at[1].team == 1);
    tide_world_player_joined(&world, player(0));
    tide_world_player_joined(&world, player(1));
    tick(2);
    TIDE_CHECK(world.MatchState.humans == 2);
    tide_world_player_joined(&world, player(2));
    tick(2);
    TIDE_CHECK(world.MatchState.humans == 2);
    tide_world_player_left(&world, player(0));
    tick(2);
    TIDE_CHECK(world.MatchState.humans == 1);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_kickoff_freezes_match_clock_then_releases)
{
    start(2);
    TIDE_REQUIRE(world.MatchState.phase == MatchPhase_Kickoff);
    const float clock = world.MatchState.remaining;
    input(&world, 0, (Controls){.throttle = 1, .boost = true});
    const tide_float3 position = world.MatchState.cars.at[0].position;
    tick(30);
    TIDE_CHECK(world.MatchState.remaining == clock);
    TIDE_CHECK(world.MatchState.cars.at[0].position.z == position.z);
    tick(240);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Playing);
    TIDE_CHECK(world.MatchState.remaining < clock);
    tide_world_free(&world);
}

// External reference, not a second copy of StepCar:
// https://www.smish.dev/rocket_league/ground_control/
// https://github.com/samuelpmish/RLUtilities/blob/develop/src/mechanics/drive.cc
// At speeds below 14 m/s the measured full-throttle fit is dv/dt=16-(14.4/14)v.
// Its analytic solution supplies the two checkpoints below. A .14 m/s speed
// tolerance covers one 120 Hz acceleration step; .09 m covers position stepping.
TIDE_TEST(afterburn_measured_throttle_acceleration_and_coasting)
{
    fixture();
    input(&world, 0, (Controls){.throttle = 1});
    ticks(60);
    TIDE_CHECK(near("throttle .5s velocity", world.MatchState.cars.at[0].velocity.z, 6.25446f, .14f));
    TIDE_CHECK(near("throttle .5s distance", world.MatchState.cars.at[0].position.z + 24, 1.69705f, .09f));
    ticks(60);
    TIDE_CHECK(near("throttle 1s velocity", world.MatchState.cars.at[0].velocity.z, 9.99417f, .14f));
    TIDE_CHECK(near("throttle 1s distance", world.MatchState.cars.at[0].position.z + 24, 5.83900f, .09f));
    ticks(360);
    TIDE_CHECK(near("throttle-only terminal speed", speed(world.MatchState.cars.at[0]), 14.1f, .03f));
    fixture();
    world.MatchState.cars.at[0].velocity.z = 10;
    ticks(60);
    // Coasting is constant 5.25 m/s², not proportional drag.
    TIDE_CHECK(near("coast .5s from 10m/s", world.MatchState.cars.at[0].velocity.z, 7.375f, .05f));
    fixture();
    world.MatchState.cars.at[0].velocity.z = 10;
    input(&world, 0, (Controls){.throttle = -1});
    ticks(24);
    TIDE_CHECK(near("brake .2s from 10m/s", world.MatchState.cars.at[0].velocity.z, 3, .05f));
    fixture();
    input(&world, 0, (Controls){.throttle = -1});
    tick(45);
    TIDE_CHECK(world.MatchState.cars.at[0].position.z < -25);
    TIDE_CHECK(world.MatchState.cars.at[0].velocity.z < 0);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_measured_turn_radius_and_no_stationary_spin)
{
    fixture();
    input(&world, 0, (Controls){.steer = 1});
    ticks(120);
    TIDE_CHECK(near("stationary steer heading", world.MatchState.cars.at[0].forward.x, 0, .001f));
    // Empirical maximum-curvature fit in drive.cc, converted from UU^-1 to m^-1.
    // Average over .2s after .2s settling; allow 10% for the fit's approximate
    // steady trajectory and this prototype's simplified tyre/suspension response.
    const float speeds[] = {5, 10, 15, 23};
    const float curvature[] = {.398f, .235f, .138f, .088f};
    for (int sample = 0; sample < 4; sample++) {
        fixture();
        world.MatchState.cars.at[0].velocity.z = speeds[sample];
        input(&world, 0, (Controls){.throttle = .02f, .steer = 1});
        ticks(24);
        float angle = 0, distance = 0;
        for (int at = 0; at < 24; at++) {
            const CarState before = world.MatchState.cars.at[0];
            ticks(1);
            const CarState after = world.MatchState.cars.at[0];
            const float sine = before.forward.z * after.forward.x - before.forward.x * after.forward.z;
            const float cosine = before.forward.x * after.forward.x + before.forward.z * after.forward.z;
            angle += tide_atan2_f(sine, cosine);
            const tide_float3 move = tide_sub_f3(after.position, before.position);
            distance += tide_sqrt_f(move.x * move.x + move.z * move.z);
        }
        const float measured = angle / distance;
        printf("    full-steer %.1fm/s: radius %.3fm (reference %.3fm)\n",
            speeds[sample], 1 / measured, 1 / curvature[sample]);
        TIDE_CHECK(near("measured turning curvature", measured, curvature[sample], curvature[sample] * .10f));
    }
    tide_world_free(&world);
}

TIDE_TEST(afterburn_powerslide_trajectory_matches_independent_rocketsim_reference)
{
    // Independently run the pinned MIT RocketSim build on a flat static plane:
    // commit c2baacb8f4b441dd8505e63c2aeb5a1679b60b02, Octane COM17.01UU,
    // initial velocity1000/1500UU/s, throttle.02, steer1, handbrake held.
    // The reference was measured before this four-wheel tire solve existed.
    // A .14m/.10m position allowance covers one input tick plus the analytic
    // suspension/contact approximation. The .18m/s vector bound is under3%
    // of the slowest final reference velocity (6.113m/s), not3% per component.
    const struct {
        float initial;
        int checkpoint;
        float forward_distance, side_distance, forward_velocity, side_velocity;
        float heading, yaw_rate;
    } samples[] = {
        {10, 60, 4.616834f, .984217f, 8.011390f, 3.190682f, 1.099752f, 2.592835f},
        {10, 120, 7.846190f, 2.603352f, 5.404876f, 2.852377f, 2.228757f, 2.134542f},
        {15, 60, 6.968331f, 1.333106f, 11.984447f, 4.421085f, 1.215760f, 2.946254f},
        {15, 120, 11.920829f, 3.402780f, 8.503550f, 3.132042f, 2.508944f, 2.560881f}
    };
    for (size_t sample = 0; sample < sizeof(samples) / sizeof(samples[0]); sample++) {
        fixture();
        world.MatchState.cars.at[0].velocity.z = samples[sample].initial;
        input(&world, 0, (Controls){.throttle = .02f, .steer = 1, .handbrake = true});
        ticks(samples[sample].checkpoint);
        const CarState result = world.MatchState.cars.at[0];
        const float yaw = tide_atan2_f(result.forward.x, result.forward.z);
        const tide_float3 velocity_error = tide_f3(result.velocity.x - samples[sample].side_velocity,
            0, result.velocity.z - samples[sample].forward_velocity);
        printf("    powerslide %.0fm/s %.1fs: heading %.4f (reference %.4f), speed %.3f, velocity error %.3fm/s\n",
            samples[sample].initial, samples[sample].checkpoint * DT, yaw, samples[sample].heading,
            speed(result), tide_length_f3(velocity_error));
        TIDE_CHECK(near("slide forward distance", result.position.z + 24, samples[sample].forward_distance, .14f));
        TIDE_CHECK(near("slide side distance", result.position.x - 3, samples[sample].side_distance, .10f));
        TIDE_CHECK(near("slide horizontal velocity vector error", tide_length_f3(velocity_error), 0, .18f));
        TIDE_CHECK(near("slide heading", yaw, samples[sample].heading, .04f));
        TIDE_CHECK(near("slide yaw rate", result.angularVelocity.y, samples[sample].yaw_rate, .06f));
    }
    tide_world_free(&world);
}

TIDE_TEST(afterburn_boost_forces_throttle_and_obeys_measured_budget)
{
    fixture();
    tide_world reference = {0};
    tide_world_copy(&reference, &world);
    input(&world, 0, (Controls){.boost = true});
    input(&reference, 0, (Controls){.throttle = 1, .boost = true});
    ticks(120);
    for (int i = 0; i < 120; i++) tide_world_tick(&reference);
    TIDE_CHECK(near("boost alone equals W+boost", world.MatchState.cars.at[0].velocity.z,
        reference.MatchState.cars.at[0].velocity.z, .001f));
    TIDE_CHECK(near("one-second boost budget", world.MatchState.cars.at[0].boost, 100 - 100.0f / 3, .01f));
    TIDE_CHECK(speed(world.MatchState.cars.at[0]) > 15);
    // RocketSim RLConst: a released boost pulse still lasts .1 seconds.
    fixture();
    input(&world, 0, (Controls){.boost = true});
    ticks(1);
    input(&world, 0, (Controls){0});
    ticks(11);
    TIDE_CHECK(near("minimum boost pulse", world.MatchState.cars.at[0].boost, 100 - 10.0f / 3, .30f));
    fixture();
    input(&world, 0, (Controls){.throttle = 1, .boost = true});
    ticks(360);
    TIDE_CHECK(near("boosted terminal speed", speed(world.MatchState.cars.at[0]), 23, .01f));
    TIDE_CHECK(near("three-second tank", world.MatchState.cars.at[0].boost, 0, .01f));
    tide_world_free(&reference);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_jump_release_and_directional_second_jump)
{
    fixture();
    input(&world, 0, (Controls){.jump = true});
    tick(1);
    TIDE_REQUIRE(world.MatchState.cars.at[0].position.y > REST_HEIGHT + .025f);
    TIDE_CHECK(world.MatchState.cars.at[0].velocity.y > 1);
    tick(8);
    input(&world, 0, (Controls){0});
    tick(2);
    const float before = world.MatchState.cars.at[0].velocity.z;
    input(&world, 0, (Controls){.jump = true, .pitch = -1, .throttle = 1});
    tick(1);
    TIDE_CHECK(tide_abs_f(world.MatchState.cars.at[0].velocity.z - before) > 2);
    TIDE_CHECK(world.MatchState.cars.at[0].dodgeTime > 0);
    input(&world, 0, (Controls){0});
    tick(180);
    TIDE_CHECK(world.MatchState.cars.at[0].position.y >= REST_HEIGHT - .03f);
    TIDE_CHECK(world.MatchState.cars.at[0].position.y < REST_HEIGHT + .12f);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_jump_apex_matches_the_external_120hz_reference)
{
    // RLBot jumping-physics' independent 120 Hz integration, with exact
    // RocketSim impulse/hold constants: 2.916667 m/s impulse, 14.583333 m/s²
    // hold for at least .025s and at most .2s, gravity6.5 and sticky3.25 for
    // three ticks. These are model predictions, not recorded game trajectories.
    // .10m/.04s allow suspension takeoff and one hold-boundary tick; they still
    // reject the old jump's wrong impulse and missing minimum-held bonus.
    // https://wiki.rlbot.org/v5/botmaking/jumping-physics/
    const int holds[] = {1, 24, 48};
    const float heights[] = {.9421f, 2.4124f, 2.4124f};
    const float times[] = {.4917f, .8833f, .8833f};
    for (int sample = 0; sample < 3; sample++) {
        fixture();
        float maximum = REST_HEIGHT, apex_time = 0;
        for (int at = 0; at < 180; at++) {
            input(&world, 0, (Controls){.jump = at < holds[sample]});
            ticks(1);
            const float height = world.MatchState.cars.at[0].position.y;
            if (height > maximum) { maximum = height; apex_time = (float)(at + 1) * DT; }
        }
        printf("    jump held %d ticks: apex %.4fm at %.4fs\n", holds[sample], maximum, apex_time);
        TIDE_CHECK(near("jump apex height", maximum, heights[sample], .10f));
        TIDE_CHECK(near("jump apex time", apex_time, times[sample], .04f));
    }
    tide_world_free(&world);
}

TIDE_TEST(afterburn_second_jump_window_starts_after_the_first_hold)
{
    for (int held = 0; held < 2; held++) {
        fixture();
        for (int at = 0; at < 158; at++) {
            input(&world, 0, (Controls){.jump = at < (held ? 24 : 1)});
            ticks(1);
            // Keep this deadline fixture away from landing without modifying
            // any jump timers/budget. The real first jump establishes them.
            world.MatchState.cars.at[0].position.y = 10;
        }
        input(&world, 0, (Controls){.jump = true});
        ticks(1);
        TIDE_CHECK(world.MatchState.cars.at[0].secondJump == (held != 0));
    }
    tide_world_free(&world);
}

static void airborne_fixture(void)
{
    fixture();
    world.MatchState.cars.at[0].position.y = 8;
    world.MatchState.cars.at[0].grounded = false;
    world.MatchState.cars.at[0].airTime = 2;
}

TIDE_TEST(afterburn_aerial_axes_rotate_in_the_real_input_directions)
{
    // Smish's experimentally fitted aerial response. Positive RLBot pitch
    // noses UP; W/forward-dodge is -1. Pitch is therefore negative about this
    // prototype's right axis. D/yaw+1 turns right; E/roll+1 rolls right.
    // https://www.smish.dev/rocket_league/aerial_control/
    const Controls controls[] = {{.pitch = -1}, {.yaw = 1}, {.roll = 1}};
    const float rates[] = {1.21460f, .89196f, -2.9093f};
    const float orientation[] = {-.06069f, .04458f, .1557f};
    for (int axis = 0; axis < 3; axis++) {
        airborne_fixture();
        input(&world, 0, controls[axis]);
        ticks(12);
        const CarState value = world.MatchState.cars.at[0];
        const float rate = axis == 0 ? value.angularVelocity.x : axis == 1
            ? value.angularVelocity.y : value.angularVelocity.z;
        const float rotated = axis == 0 ? value.forward.y : axis == 1
            ? value.forward.x : value.up.x;
        // One angular integration tick accounts for .11rad/s and .02 in
        // these basis-vector components, without accepting an inverted axis.
        TIDE_CHECK(near("aerial .1s angular rate", rate, rates[axis], .11f));
        TIDE_CHECK(near("aerial .1s rotated basis", rotated, orientation[axis], .02f));
    }
    airborne_fixture();
    input(&world, 0, (Controls){.pitch = -1, .yaw = 1, .roll = 1});
    ticks(84);
    const tide_float3 angular = world.MatchState.cars.at[0].angularVelocity;
    TIDE_CHECK(near("combined angular speed cap", tide_sqrt_f(angular.x * angular.x
        + angular.y * angular.y + angular.z * angular.z), 5.5f, .001f));
    airborne_fixture();
    input(&world, 0, (Controls){.pitch = -1, .suppressPitch = true});
    ticks(24);
    TIDE_CHECK(near("keyboard safety suppresses nose torque", world.MatchState.cars.at[0].forward.y, 0, .001f));
    const float decay[] = {.49679f, .62398f, .32697f};
    for (int axis = 0; axis < 3; axis++) {
        airborne_fixture();
        if (axis == 0) world.MatchState.cars.at[0].angularVelocity.x = 1;
        if (axis == 1) world.MatchState.cars.at[0].angularVelocity.y = 1;
        if (axis == 2) world.MatchState.cars.at[0].angularVelocity.z = 1;
        ticks(30);
        const tide_float3 value = world.MatchState.cars.at[0].angularVelocity;
        const float actual = axis == 0 ? value.x : axis == 1 ? value.y : value.z;
        // Analytic exp(-D*.25) from smish's damping fit. .015rad/s covers
        // first-order stepping at120Hz; it rejects instantaneous release.
        TIDE_CHECK(near("released aerial angular damping .25s", actual, decay[axis], .015f));
    }
    tide_world_free(&world);
}

static void prepare_dodge(const float forward_speed)
{
    fixture();
    world.MatchState.cars.at[0].velocity.z = forward_speed;
    input(&world, 0, (Controls){.jump = true});
    ticks(3);
    input(&world, 0, (Controls){0});
    ticks(3);
    world.MatchState.cars.at[0].position.y = 5;
}

TIDE_TEST(afterburn_forward_flip_can_be_cancelled_without_erasing_angular_momentum)
{
    prepare_dodge(0);
    input(&world, 0, (Controls){.jump = true, .pitch = -1});
    ticks(1);
    tide_world continued = {0};
    tide_world_copy(&continued, &world);
    input(&world, 0, (Controls){.pitch = 1});
    input(&continued, 0, (Controls){.pitch = -1});
    ticks(12);
    for (int at = 0; at < 12; at++) tide_world_tick(&continued);
    const float cancelled = world.MatchState.cars.at[0].angularVelocity.x;
    const float full = continued.MatchState.cars.at[0].angularVelocity.x;
    TIDE_CHECK(cancelled > .5f && cancelled < full * .5f);
    TIDE_CHECK(full > 5);
    tide_world_free(&continued);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_air_throttle_and_boost_use_measured_acceleration)
{
    airborne_fixture();
    input(&world, 0, (Controls){.throttle = 1});
    ticks(60);
    TIDE_CHECK(near("air throttle .5s", world.MatchState.cars.at[0].velocity.z, 1.0f / 3, .01f));
    airborne_fixture();
    input(&world, 0, (Controls){.boost = true});
    ticks(60);
    // Unlike grounded wheel throttle, boost does not force the separate air
    // throttle: RocketSim _UpdateAirTorque reads the original throttle input.
    TIDE_CHECK(near("air boost alone .5s", world.MatchState.cars.at[0].velocity.z, 5.291667f, .06f));
    TIDE_CHECK(near("air gravity .5s", world.MatchState.cars.at[0].velocity.y, -3.25f, .06f));
    airborne_fixture();
    input(&world, 0, (Controls){.throttle = 1, .boost = true});
    ticks(60);
    TIDE_CHECK(near("air W+boost .5s", world.MatchState.cars.at[0].velocity.z, 5.625f, .06f));
    tide_world_free(&world);
}

TIDE_TEST(afterburn_two_wheel_landing_reduces_drive_and_gates_air_controls)
{
    // A 30-degree nose-down body at.37m puts only the front two suspension
    // rays on the floor, with the chassis clear. RocketSim quarters engine
    // torque below3 contacts and adds air throttle, while ordinary aerial
    // torque/damping requires zero contacts. Compare to an identical car
    // without pitch/yaw/roll so suspension torque is not mistaken for input.
    fixture();
    world.MatchState.cars.at[0].position.y = .37f;
    world.MatchState.cars.at[0].forward = tide_f3(0, -.5f, .8660254f);
    world.MatchState.cars.at[0].up = tide_f3(0, .8660254f, .5f);
    tide_world reference = {0};
    tide_world_copy(&reference, &world);
    input(&world, 0, (Controls){.throttle = 1, .pitch = -1, .yaw = 1, .roll = 1});
    input(&reference, 0, (Controls){.throttle = 1});
    ticks(1);
    tide_world_tick(&reference);
    const CarState actual = world.MatchState.cars.at[0];
    const CarState neutral = reference.MatchState.cars.at[0];
    TIDE_REQUIRE(actual.wheelContacts == 2 && !actual.grounded);
    // Two of four wheels * quarter torque *16m/s², plus projected air throttle.
    TIDE_CHECK(near("two-wheel forward acceleration", actual.velocity.z,
        (2 + .666667f * .8660254f) * DT, .001f));
    TIDE_CHECK(near("two-wheel pitch input gated", actual.angularVelocity.x, neutral.angularVelocity.x, .000001f));
    TIDE_CHECK(near("two-wheel yaw input gated", actual.angularVelocity.y, neutral.angularVelocity.y, .000001f));
    TIDE_CHECK(near("two-wheel roll input gated", actual.angularVelocity.z, neutral.angularVelocity.z, .000001f));
    tide_world_free(&reference);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_wheel_ball_support_changes_only_the_car_and_restores_jump)
{
    // The body box is above the sphere's highest point, but its suspension
    // rays reach the curved support. The pinned btVehicleRL applies spring
    // and tire impulses to the chassis only; it does not react those wheel
    // impulses onto a dynamic ball. Three contacts count as grounded/reset.
    fixture();
    world.MatchState.cars.at[0].position = tide_f3(3, 4.96f, -24);
    world.MatchState.cars.at[0].grounded = false;
    world.MatchState.cars.at[0].hasJumped = true;
    world.MatchState.cars.at[0].secondJump = true;
    world.MatchState.cars.at[0].airTimeSinceJump = 2;
    world.MatchState.ball = (BallState){.position = {3, 4, -24}, .lastTouch = -1};
    ticks(1);
    const CarState actual = world.MatchState.cars.at[0];
    TIDE_REQUIRE(actual.ballWheelContacts == 4 && actual.wheelContacts == 4);
    TIDE_CHECK(actual.grounded && !actual.hasJumped && !actual.secondJump);
    TIDE_CHECK(actual.airTimeSinceJump == 0);
    TIDE_CHECK(actual.velocity.y > -6.5f * DT + .03f);
    TIDE_CHECK(world.MatchState.ball.lastTouch == -1);
    TIDE_CHECK(world.MatchState.ball.velocity.x == 0 && world.MatchState.ball.velocity.z == 0);
    TIDE_CHECK(near("wheel-only ball keeps free-fall velocity", world.MatchState.ball.velocity.y,
        -6.5f * DT * (1 - .03f * DT), .000001f));
    TIDE_CHECK(tide_length_f3(world.MatchState.ball.spin) == 0);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_directional_dodge_impulse_and_deadzone_match_reference)
{
    // RocketSim: forward impulse500UU/s; lateral500*(1+.9*|vf|/2300);
    // backward500*(16/15)*(1+1.5*|vf|/2300). .06m/s covers a tick of
    // air acceleration and conversion; no car/ball/surface contacts occur.
    // https://github.com/ZealanL/RocketSim/blob/main/src/Sim/Car/Car.cpp
    const Controls controls[] = {{.jump = true, .pitch = -1, .suppressPitch = true},
        {.jump = true, .yaw = 1}, {.jump = true, .pitch = 1}};
    const float initial[] = {0, 10, 10};
    const float impulse[] = {5, 6.95652f, -8.81159f};
    for (int sample = 0; sample < 3; sample++) {
        prepare_dodge(initial[sample]);
        const tide_float3 before = world.MatchState.cars.at[0].velocity;
        input(&world, 0, controls[sample]);
        ticks(1);
        const CarState after = world.MatchState.cars.at[0];
        const float delta = sample == 1 ? after.velocity.x - before.x : after.velocity.z - before.z;
        TIDE_CHECK(near("directional dodge impulse", delta, impulse[sample], .06f));
        TIDE_CHECK(after.secondJump && after.dodgeTime > 0);
        // Front flip really rotates nose-down; a sideways dodge rotates right.
        ticks(12);
        if (sample == 0) TIDE_CHECK(world.MatchState.cars.at[0].forward.y < -.1f);
        if (sample == 1) TIDE_CHECK(world.MatchState.cars.at[0].up.x > .1f);
    }
    prepare_dodge(0);
    const float before = world.MatchState.cars.at[0].velocity.y;
    input(&world, 0, (Controls){.jump = true, .pitch = .79f});
    ticks(1);
    TIDE_CHECK(near("below .8 deadzone is neutral jump", world.MatchState.cars.at[0].velocity.y - before,
        2.916667f - 6.5f * DT, .06f));
    TIDE_CHECK(world.MatchState.cars.at[0].dodgeTime == 0);
    prepare_dodge(0);
    input(&world, 0, (Controls){.jump = true, .pitch = -.81f});
    ticks(1);
    TIDE_CHECK(world.MatchState.cars.at[0].dodgeTime > 0);
    TIDE_CHECK(world.MatchState.cars.at[0].velocity.z > 4.9f);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_second_jump_is_spent_until_landing)
{
    fixture();
    input(&world, 0, (Controls){.jump = true});
    tick(1);
    input(&world, 0, (Controls){0});
    tick(2);
    const float before = world.MatchState.cars.at[0].velocity.y;
    input(&world, 0, (Controls){.jump = true});
    tick(1);
    TIDE_CHECK(world.MatchState.cars.at[0].velocity.y > before + 2.5f);
    TIDE_CHECK(world.MatchState.cars.at[0].secondJump);
    input(&world, 0, (Controls){0});
    tick(1);
    const float spent = world.MatchState.cars.at[0].velocity.y;
    input(&world, 0, (Controls){.jump = true});
    tick(1);
    TIDE_CHECK(world.MatchState.cars.at[0].velocity.y < spent);
    input(&world, 0, (Controls){0});
    tick(180);
    TIDE_CHECK(world.MatchState.cars.at[0].grounded);
    TIDE_CHECK(!world.MatchState.cars.at[0].secondJump);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_curved_ramp_wall_driving_and_wall_jump)
{
    fixture();
    world.MatchState.cars.at[0] = car(tide_f3(36.8f, REST_HEIGHT, 0), 1);
    world.MatchState.cars.at[0].forward = tide_f3(1, 0, 0);
    world.MatchState.cars.at[0].velocity = tide_f3(15, 0, 0);
    input(&world, 0, (Controls){.throttle = 1});
    tick(30);
    TIDE_CHECK(world.MatchState.cars.at[0].position.y > 1);
    TIDE_CHECK(world.MatchState.cars.at[0].up.x < -0.2f);
    TIDE_CHECK(world.MatchState.cars.at[0].grounded);
    // A wheel-aligned wall contact is a grounded surface: its jump impulse
    // must leave the wall along its inward normal, not world vertical.
    world.MatchState.cars.at[0] = car(tide_f3(40.96f - REST_HEIGHT, 5, 0), 1);
    world.MatchState.cars.at[0].up = tide_f3(-1, 0, 0);
    world.MatchState.cars.at[0].velocity = tide_f3(0, 0, 12);
    tick(8);
    TIDE_CHECK(world.MatchState.cars.at[0].grounded);
    TIDE_CHECK(world.MatchState.cars.at[0].up.x < -0.95f);
    input(&world, 0, (Controls){.jump = true});
    tick(1);
    TIDE_CHECK(!world.MatchState.cars.at[0].grounded);
    TIDE_CHECK(world.MatchState.cars.at[0].velocity.x < -2);
    TIDE_CHECK(world.MatchState.cars.at[0].position.x < 40.96f - REST_HEIGHT - .025f);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_large_and_small_boost_pads_deplete_and_respawn)
{
    fixture();
    world.MatchState.cars.at[0] = car(tide_f3(-30.72f, REST_HEIGHT, -40.96f), 1);
    world.MatchState.cars.at[0].boost = 0;
    tick(1);
    TIDE_CHECK(world.MatchState.cars.at[0].boost == 100);
    TIDE_CHECK(world.MatchState.pads.at[0] > 9.9f);
    world.MatchState.cars.at[0].boost = 10;
    tick(10);
    TIDE_CHECK(world.MatchState.cars.at[0].boost == 10);
    // Leave the pad while it respawns, then drive onto the same real pickup.
    world.MatchState.cars.at[0].position = tide_f3(3, REST_HEIGHT, -24);
    tick(610);
    TIDE_CHECK(world.MatchState.pads.at[0] == 0);
    world.MatchState.cars.at[0].position = tide_f3(-30.72f, REST_HEIGHT, -40.96f);
    tick(1);
    TIDE_CHECK(world.MatchState.cars.at[0].boost == 100);
    fixture();
    world.MatchState.cars.at[0] = car(tide_f3(0, REST_HEIGHT, -42.40f), 1);
    world.MatchState.cars.at[0].boost = 20;
    tick(1);
    TIDE_CHECK(world.MatchState.cars.at[0].boost == 32);
    TIDE_CHECK(world.MatchState.pads.at[6] > 3.9f && world.MatchState.pads.at[6] <= 4);
    world.MatchState.cars.at[0].position = tide_f3(3, REST_HEIGHT, -24);
    tick(250);
    TIDE_CHECK(world.MatchState.pads.at[6] == 0);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_ball_bounces_and_car_strikes_transfer_momentum)
{
    fixture();
    world.MatchState.ball = (BallState){.position = {39.9f, 5, 0}, .velocity = {30, 0, 0}, .lastTouch = -1};
    tick(4);
    TIDE_CHECK(world.MatchState.ball.velocity.x < 0);
    TIDE_CHECK(world.MatchState.ball.position.x < 40.96f);
    fixture();
    world.MatchState.ball = (BallState){.position = {3, 0.915f, -21.8f}, .lastTouch = -1};
    world.MatchState.cars.at[0].velocity = tide_f3(0, 0, 18);
    input(&world, 0, (Controls){.throttle = 1});
    tick(12);
    TIDE_CHECK(world.MatchState.ball.velocity.z > 5);
    TIDE_CHECK(world.MatchState.ball.lastTouch == 0);
    TIDE_CHECK(world.MatchState.cars.at[0].touches > 0);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_airborne_car_shot_matches_independent_rocketsim_reference)
{
    // Independently compiled RocketSim/Bullet, MIT, pinned at
    // https://github.com/ZealanL/RocketSim/tree/c2baacb8f4b441dd8505e63c2aeb5a1679b60b02
    // Arena::Create(THE_VOID), default mutators and120Hz, CAR_CONFIG_OCTANE.
    // Identity car basis; COM(0,0,800)UU, velocity(1000,0,0), airborne,
    // boost0/all controls neutral; ball(190,0,820.755),velocity(0,0,-100),
    // zero spin. Nonzero ball velocity avoids RocketSim's zero-speed sleep.
    // Axes RocketSim(X,Y,Z) = Tide(Z,X,Y); UU=m*100.
    // After four ticks reference ballvelocity=(1459.242310,0,-28.956762)UU/s.
    // Allow1UU/s vector error (<.07% shot speed) for solver/integration detail.
    // This benchmarks outgoing velocity, not identical collision scheduling:
    // Tide's contact is tick3 rather than4, and its tick4 ball is5.39UU ahead.
    fixture();
    world.MatchState.cars.at[0] = (CarState){.position={0,8,0}, .forward={0,0,1},
        .up={0,1,0}, .velocity={0,0,10}};
    world.MatchState.ball = (BallState){.position={0,8.20755f,1.9f},
        .velocity={0,-1,0}, .lastTouch=-1};
    ticks(4);
    TIDE_REQUIRE(world.MatchState.ball.lastTouch == 0);
    const tide_float3 reference = {0,-.289567623f,14.592423096f};
    const tide_float3 error = tide_sub_f3(world.MatchState.ball.velocity, reference);
    printf("    independent RocketSim shot: velocity vector error %.6fUU/s\n", tide_length_f3(error) * 100);
    TIDE_CHECK(near("independent shot velocity vector error", tide_length_f3(error), 0, .01f));
    tide_world_free(&world);
}

TIDE_TEST(afterburn_ball_flight_drag_gravity_and_wall_restitution)
{
    // RocketSim's free ball constants: drag .03/s, gravity6.5m/s² and normal
    // restitution .6. The .03m/s vertical allowance includes drag's effect
    // during the fall; .02m/s horizontal allowance exceeds a 120 Hz drag step.
    // https://github.com/ZealanL/RocketSim/blob/main/src/RLConst.h
    fixture();
    world.MatchState.ball = (BallState){.position = {0, 10, 0}, .velocity = {0, 0, 10}, .lastTouch = -1};
    ticks(60);
    TIDE_CHECK(near("free ball horizontal drag .5s", world.MatchState.ball.velocity.z, 9.85112f, .02f));
    TIDE_CHECK(near("free ball gravity .5s", world.MatchState.ball.velocity.y, -3.25f, .03f));
    fixture();
    world.MatchState.ball = (BallState){.position = {39.95f, 5, 0}, .velocity = {10, 0, 0}, .lastTouch = -1};
    ticks(4);
    TIDE_CHECK(near("wall normal restitution", world.MatchState.ball.velocity.x, -6, .02f));
    tide_world_free(&world);
}

TIDE_TEST(afterburn_supersonic_grace_measures_the_whole_episode_after_contacts)
{
    // Pinned RocketSim Car::_PostTickUpdate increments supersonicTime for
    // the whole episode, including travel above22m/s. Only an episode under
    // one second can retain supersonic at21..22; this is not a new one-second
    // grace every time the car slows down. Evaluate the completed contact speed.
    // https://github.com/ZealanL/RocketSim/blob/c2baacb8f4b441dd8505e63c2aeb5a1679b60b02/src/Sim/Car/Car.cpp
    fixture();
    input(&world, 0, (Controls){.throttle = 1});
    world.MatchState.cars.at[0].velocity.z = 22.5f;
    ticks(1);
    TIDE_REQUIRE(world.MatchState.cars.at[0].supersonic);
    world.MatchState.cars.at[0].velocity.z = 21.5f;
    ticks(30);
    TIDE_CHECK(world.MatchState.cars.at[0].supersonic);
    ticks(100);
    TIDE_CHECK(!world.MatchState.cars.at[0].supersonic);
    TIDE_CHECK(world.MatchState.cars.at[0].supersonicTime == 0);

    fixture();
    input(&world, 0, (Controls){.throttle = 1});
    world.MatchState.cars.at[0].velocity.z = 22.5f;
    ticks(132);
    TIDE_REQUIRE(world.MatchState.cars.at[0].supersonic);
    TIDE_REQUIRE(world.MatchState.cars.at[0].supersonicTime > 1);
    world.MatchState.cars.at[0].velocity.z = 21.5f;
    ticks(1);
    TIDE_CHECK(!world.MatchState.cars.at[0].supersonic);

    fixture();
    input(&world, 0, (Controls){.throttle = 1});
    world.MatchState.cars.at[0].velocity.z = 22.5f;
    world.MatchState.cars.at[0].supersonic = true;
    world.MatchState.cars.at[0].supersonicTime = 2;
    world.MatchState.ball = (BallState){.position = {3, .9125f, -22.4f}, .lastTouch = -1};
    ticks(1);
    TIDE_REQUIRE(speed(world.MatchState.cars.at[0]) < 22);
    TIDE_CHECK(!world.MatchState.cars.at[0].supersonic);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_supersonic_front_hit_demolishes_and_respawns)
{
    fixture();
    // First contact follows .1875m travel in one 120 Hz tick. Starting1.5m
    // apart meets at the actual front bumpers, rather than deep box overlap.
    world.MatchState.cars.at[1] = car(tide_f3(3, REST_HEIGHT, -22.5f), -1);
    world.MatchState.cars.at[1].team = 1;
    world.MatchState.cars.at[0].velocity = tide_f3(0, 0, 22.5f);
    world.MatchState.cars.at[0].supersonic = true;
    input(&world, 0, (Controls){.throttle = 1, .boost = true});
    tick(1);
    TIDE_CHECK(world.MatchState.cars.at[1].respawn > 2.9f);
    TIDE_CHECK(world.MatchState.cars.at[0].demolitions == 1);
    input(&world, 0, (Controls){0});
    tick(60);
    TIDE_CHECK(world.MatchState.cars.at[1].respawn > 1.8f);
    tick(130);
    TIDE_CHECK(world.MatchState.cars.at[1].respawn == 0);
    TIDE_CHECK(world.MatchState.cars.at[1].team == 1);
    TIDE_CHECK(near("respawn boost", world.MatchState.cars.at[1].boost, 100.0f / 3, .01f));
    TIDE_CHECK(world.MatchState.cars.at[1].position.z > 35);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_oriented_car_contacts_reject_side_roof_and_backwards_demos)
{
    fixture();
    // Two parallel Octane boxes are .866994m wide. A .92m separation is
    // clear; the former centre-sphere test incorrectly pushed these cars.
    world.MatchState.cars.at[1] = car(tide_f3(3.92f, REST_HEIGHT, -24), 1);
    world.MatchState.cars.at[1].team = 1;
    ticks(2);
    TIDE_CHECK(near("separated sides remain still", world.MatchState.cars.at[1].position.x, 3.92f, .001f));
    TIDE_CHECK(near("separated side velocity", world.MatchState.cars.at[1].velocity.x, 0, .001f));
    fixture();
    // Effective collider height .386591m: .45m COM height difference clears
    // both roofs for these two ticks, despite overlapping old bounding spheres.
    world.MatchState.cars.at[1] = car(tide_f3(3, REST_HEIGHT + .45f, -24), 1);
    world.MatchState.cars.at[1].grounded = false;
    world.MatchState.cars.at[1].team = 1;
    ticks(2);
    TIDE_CHECK(near("separated roof has no horizontal bump", world.MatchState.cars.at[1].velocity.z, 0, .001f));
    TIDE_CHECK(world.MatchState.cars.at[0].respawn == 0 && world.MatchState.cars.at[1].respawn == 0);
    fixture();
    world.MatchState.cars.at[1] = car(tide_f3(3, REST_HEIGHT, -22.5f), -1);
    world.MatchState.cars.at[1].team = 1;
    world.MatchState.cars.at[0].velocity.z = 10;
    ticks(2);
    TIDE_CHECK(world.MatchState.cars.at[1].velocity.z > 1);
    TIDE_CHECK(world.MatchState.cars.at[1].respawn == 0 && world.MatchState.cars.at[0].demolitions == 0);
    fixture();
    world.MatchState.cars.at[0].position.y = 5;
    world.MatchState.cars.at[0].grounded = false;
    world.MatchState.cars.at[1] = car(tide_f3(4.05f, 5, -24), 1);
    world.MatchState.cars.at[1].grounded = false;
    world.MatchState.cars.at[1].team = 1;
    world.MatchState.cars.at[0].velocity.x = 22.5f;
    world.MatchState.cars.at[0].supersonic = true;
    ticks(2);
    TIDE_CHECK(world.MatchState.cars.at[1].respawn == 0 && world.MatchState.cars.at[0].demolitions == 0);
    fixture();
    world.MatchState.cars.at[1] = car(tide_f3(3, REST_HEIGHT, -25.5f), 1);
    world.MatchState.cars.at[1].team = 1;
    world.MatchState.cars.at[0].velocity.z = -22.5f;
    world.MatchState.cars.at[0].supersonic = true;
    ticks(2);
    TIDE_CHECK(world.MatchState.cars.at[1].respawn == 0 && world.MatchState.cars.at[0].demolitions == 0);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_goal_requires_the_whole_ball_to_cross)
{
    fixture();
    world.MatchState.ball = (BallState){.position = {0, 2, 52.0f}, .lastTouch = -1};
    tick(1);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Playing && world.MatchState.blueScore == 0);
    world.MatchState.ball.velocity = tide_f3(0, 0, 20);
    tick(1);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Goal && world.MatchState.blueScore == 1);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_goals_require_the_mouth_and_reset_for_kickoff)
{
    fixture();
    world.MatchState.ball = (BallState){.position = {0, 2, 53}, .velocity = {0, 0, 10}, .lastTouch = 0};
    tick(1);
    TIDE_CHECK(world.MatchState.blueScore == 1 && world.MatchState.orangeScore == 0);
    TIDE_REQUIRE(world.MatchState.phase == MatchPhase_Goal);
    const float clock = world.MatchState.remaining;
    tick(30);
    TIDE_CHECK(world.MatchState.remaining == clock);
    tick(420);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Playing);
    TIDE_CHECK(world.MatchState.blueScore == 1);
    fixture();
    world.MatchState.ball = (BallState){.position = {0, 2, -53}, .velocity = {0, 0, -10}, .lastTouch = 1};
    tick(1);
    TIDE_CHECK(world.MatchState.orangeScore == 1 && world.MatchState.blueScore == 0);
    fixture();
    world.MatchState.ball = (BallState){.position = {20, 2, 51}, .velocity = {0, 0, 30}, .lastTouch = -1};
    tick(5);
    TIDE_CHECK(world.MatchState.blueScore == 0 && world.MatchState.orangeScore == 0);
    TIDE_CHECK(world.MatchState.ball.velocity.z < 0);
    fixture();
    world.MatchState.ball = (BallState){.position = {0, 9, 51}, .velocity = {0, 0, 30}, .lastTouch = -1};
    tick(5);
    TIDE_CHECK(world.MatchState.blueScore == 0 && world.MatchState.orangeScore == 0);
    TIDE_CHECK(world.MatchState.ball.velocity.z < 0);
    fixture();
    // The centre overlaps the goal's width, but the whole sphere cannot fit
    // past this post. Contact must not pull it sideways through the frame.
    world.MatchState.ball = (BallState){.position = {8.5f, 2, 50}, .velocity = {0, 0, 30}, .lastTouch = -1};
    tick(5);
    TIDE_CHECK(world.MatchState.blueScore == 0 && world.MatchState.orangeScore == 0);
    TIDE_CHECK(world.MatchState.ball.velocity.z < 0);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_tied_regulation_enters_sudden_death)
{
    fixture();
    world.MatchState.remaining = DT * 0.5f;
    world.MatchState.ball.position = tide_f3(20, 0.915f, 5);
    tick(2);
    TIDE_CHECK(world.MatchState.overtime);
    tick(300);
    world.MatchState.phase = MatchPhase_Playing;
    world.MatchState.ball = (BallState){.position = {0, 2, 53}, .velocity = {0, 0, 10}, .lastTouch = 0};
    tick(1);
    TIDE_CHECK(world.MatchState.blueScore == 1);
    tick(300);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Finished);
    TIDE_CHECK(world.MatchState.winner == 0);
    fixture();
    world.MatchState.blueScore = 2;
    world.MatchState.orangeScore = 1;
    world.MatchState.remaining = DT * 0.5f;
    tick(2);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Finished && world.MatchState.winner == 0);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_zero_clock_keeps_the_last_aerial_live)
{
    fixture();
    world.MatchState.blueScore = 1;
    world.MatchState.remaining = DT * 0.5f;
    world.MatchState.ball = (BallState){.position = {0, 8, 0}, .lastTouch = -1};
    tick(15);
    TIDE_CHECK(world.MatchState.remaining == 0);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Playing);
    TIDE_CHECK(!world.MatchState.overtime && world.MatchState.winner == -1);
    tick(180);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Finished && world.MatchState.winner == 0);
    tide_world_free(&world);
}

static bool restart(const int seat)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_Restart, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    const uint32_t size = tide_bits_end(&bits);
    const tide_player_id sender = player(seat);
    return tide_game_api.command(&world, sender, 0,
        tide_game_api.command_sequence(&world, sender, 0) + 1, bytes, size);
}

TIDE_TEST(afterburn_restart_requires_a_participant_and_resets_the_match)
{
    fixture();
    world.MatchState.blueScore = 3;
    world.MatchState.orangeScore = 1;
    world.MatchState.remaining = 45;
    world.MatchState.phase = MatchPhase_Finished;
    world.MatchState.winner = 0;
    tide_world_player_left(&world, player(1));
    tick(1);
    (void)restart(1);
    tick(1);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Finished && world.MatchState.blueScore == 3);
    TIDE_REQUIRE(restart(0));
    tick(1);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Kickoff);
    TIDE_CHECK(world.MatchState.blueScore == 0 && world.MatchState.orangeScore == 0);
    TIDE_CHECK(world.MatchState.remaining == 300 && world.MatchState.winner == -1);
    TIDE_CHECK(world.MatchState.humans == 1);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_bots_play_a_complete_bounded_match)
{
    start(0);
    float fastest_ball = 0;
    bool touched = false;
    for (int at = 0; at < 120 * 420 && world.MatchState.phase != MatchPhase_Finished; at++) {
        tide_world_tick(&world);
        if (at % 120 != 0) continue;
        const BallState ball = world.MatchState.ball;
        TIDE_CHECK(finite3(ball.position) && finite3(ball.velocity));
        TIDE_CHECK(tide_abs_f(ball.position.x) <= 40.97f && tide_abs_f(ball.position.z) <= 60.01f);
        TIDE_CHECK(ball.position.y >= .9025f && ball.position.y <= 20.48f - .9125f + .01f);
        const float ball_speed = tide_sqrt_f(ball.velocity.x * ball.velocity.x + ball.velocity.y * ball.velocity.y
            + ball.velocity.z * ball.velocity.z);
        if (ball_speed > fastest_ball) fastest_ball = ball_speed;
        TIDE_CHECK(ball_speed <= 60.01f);
        for (int seat = 0; seat < 2; seat++) {
            const CarState value = world.MatchState.cars.at[seat];
            TIDE_CHECK(finite3(value.position) && finite3(value.velocity) && finite3(value.forward) && finite3(value.up));
            TIDE_CHECK(value.boost >= 0 && value.boost <= 100);
            TIDE_CHECK(speed(value) <= 23.01f);
            TIDE_CHECK(value.position.y >= -.01f && value.position.y <= 20.49f);
            TIDE_CHECK(tide_abs_f(value.position.x) <= 40.97f && tide_abs_f(value.position.z) <= 60.01f);
            touched |= value.touches > 0;
        }
    }
    printf("    bot match: %d-%d, winner %d, %.2f elapsed seconds, %.2f m/s fastest ball\n",
        world.MatchState.blueScore, world.MatchState.orangeScore, world.MatchState.winner,
        world.MatchState.elapsed, fastest_ball);
    TIDE_CHECK(touched && fastest_ball > 5);
    TIDE_CHECK(world.MatchState.blueScore + world.MatchState.orangeScore > 0);
    TIDE_CHECK(world.MatchState.phase == MatchPhase_Finished);
    TIDE_CHECK(world.MatchState.winner >= 0 && world.MatchState.winner < 2);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_input_bounds_and_nonfinite_repair)
{
    fixture();
    tide_world reference = {0};
    tide_world_copy(&reference, &world);
    input(&world, 0, (Controls){.throttle = 1000, .steer = -1000, .pitch = 1000, .yaw = -1000, .roll = 1000});
    input(&reference, 0, (Controls){.throttle = 1, .steer = -1, .pitch = 1, .yaw = -1, .roll = 1});
    tick(30);
    for (int i = 0; i < 60; i++) tide_world_tick(&reference);
    TIDE_CHECK(memcmp(&world.MatchState.cars.at[0], &reference.MatchState.cars.at[0], sizeof(CarState)) == 0);
    const uint32_t nan_bits = UINT32_C(0x7fc12345), inf_bits = UINT32_C(0x7f800000);
    float nan_value, inf_value;
    memcpy(&nan_value, &nan_bits, sizeof nan_value);
    memcpy(&inf_value, &inf_bits, sizeof inf_value);
    input(&world, 0, (Controls){.throttle = nan_value, .steer = inf_value, .pitch = -inf_value,
        .yaw = nan_value, .roll = inf_value, .boost = true, .jump = true});
    tick(120);
    TIDE_CHECK(finite3(world.MatchState.cars.at[0].position));
    TIDE_CHECK(finite3(world.MatchState.cars.at[0].velocity));
    TIDE_CHECK(finite3(world.MatchState.ball.position));
    TIDE_CHECK(world.MatchState.cars.at[0].boost >= 0 && world.MatchState.cars.at[0].boost <= 100);
    tide_world_free(&reference);
    tide_world_free(&world);
}

static Controls script(const uint32_t at, const int seat)
{
    return (Controls){.throttle = at % 360 < 300 ? 1 : -0.5f,
        .steer = tide_sin_f((float)at * 0.013f + (float)seat * 2.1f) * 0.65f,
        .pitch = at % 180 < 90 ? -0.4f : 0.6f, .yaw = seat ? -0.3f : 0.3f, .dodgeDeadzone = .8f,
        .roll = at % 270 < 135 ? 0.3f : -0.3f, .boost = at % 120 < 50,
        .jump = at % 180 < 12 || (at % 180 >= 25 && at % 180 < 28), .handbrake = at % 210 > 180};
}

static void scripted_ticks(tide_world *w, const uint32_t from, const uint32_t through)
{
    for (uint32_t at = from; at < through; at++) {
        for (int seat = 0; seat < 2; seat++) input(w, seat, script(at, seat));
        tide_world_tick(w);
    }
}

TIDE_TEST(afterburn_snapshot_pack_and_replay_preserve_the_match)
{
    fixture();
    world.MatchState.ball = (BallState){.position = {3, 0.915f, -20}, .lastTouch = -1};
    scripted_ticks(&world, 0, 180);
    tide_world saved = {0}, replay = {0}, unpacked = {0};
    tide_world_copy(&saved, &world);
    const uint64_t snapshot = tide_world_hash(&saved);
    const uint32_t size = tide_world_pack(&saved, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && size > 0);
    TIDE_CHECK(tide_world_pack(&saved, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&unpacked, bytes, size));
    TIDE_CHECK(tide_world_hash(&unpacked) == snapshot);
    scripted_ticks(&world, 180, 1800);
    const uint64_t final = tide_world_hash(&world);
    tide_world_copy(&replay, &saved);
    scripted_ticks(&replay, 180, 1800);
    scripted_ticks(&unpacked, 180, 1800);
    TIDE_CHECK(tide_world_hash(&replay) == final);
    TIDE_CHECK(tide_world_hash(&unpacked) == final);
    TIDE_CHECK(tide_world_hash(&saved) == snapshot);
    TIDE_CHECK(final == SCRIPTED_HASH);
    printf("    exact simulation hash: 0x%016llx\n", (unsigned long long)final);
    free(bytes);
    tide_world_free(&unpacked);
    tide_world_free(&replay);
    tide_world_free(&saved);
    tide_world_free(&world);
}

typedef struct network_match network_match;
typedef struct network_player { network_match *match; int seat; } network_player;
struct network_match {
    tide_loopback *net;
    tide_server *server;
    tide_client *clients[2];
    network_player players[2];
    double now;
    uint64_t hashes[3001];
};

static void sample_network(void *user, const uint32_t at, void *output)
{
    const network_player *me = user;
    Controls control = script(at, me->seat);
    control.away = false;
    memcpy(output, &control, sizeof control);
}

TIDE_TEST(afterburn_two_clients_confirm_car_soccer_on_a_lossy_network)
{
    static network_match match;
    match = (network_match){.net = tide_loopback_create(98312)};
    tide_loopback_set_conditions(match.net, (tide_net_conditions){.latency = 0.03, .jitter = 0.02, .loss = 0.05});
    match.server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 120,
        .transports = {tide_loopback_endpoint(match.net, 1)}}, 0);
    for (int seat = 0; seat < 2; seat++) {
        match.players[seat] = (network_player){&match, seat};
        match.clients[seat] = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
            .transport = tide_loopback_endpoint(match.net, (uint32_t)seat + 2),
            .server = tide_loopback_address(1), .lead = 2,
            .sample = sample_network, .user = &match.players[seat]}, 0);
    }
    for (int step = 0; tide_server_tick(match.server) < 3000 && step < 3600; step++) {
        match.now += 1.0 / 120.0;
        tide_loopback_set_time(match.net, match.now);
        for (int seat = 0; seat < 2; seat++) tide_client_update(match.clients[seat], match.now);
        tide_server_update(match.server, match.now);
        const uint32_t at = tide_server_tick(match.server);
        if (at <= 3000) match.hashes[at] = tide_world_hash(tide_server_world(match.server));
    }
    const tide_world *authority = tide_server_world(match.server);
    TIDE_CHECK(authority->MatchState.humans == 2);
    TIDE_CHECK(authority->MatchState.remaining < 290);
    for (int seat = 0; seat < 2; seat++) {
        const tide_client_status status = tide_client_status_of(match.clients[seat]);
        const tide_world *verified = tide_client_verified_world(match.clients[seat]);
        TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED && status.resyncs == 0);
        TIDE_REQUIRE(verified != NULL && status.verified_tick > 2600 && status.verified_tick <= 3000);
        TIDE_CHECK(tide_world_hash(verified) == match.hashes[status.verified_tick]);
        TIDE_CHECK(status.again_ticks > 0);
        printf("    player %d verified %u, replayed %u, resyncs %u\n", seat + 1,
            status.verified_tick, status.again_ticks, status.resyncs);
    }
    for (int seat = 0; seat < 2; seat++) tide_client_destroy(match.clients[seat]);
    tide_server_destroy(match.server);
    tide_loopback_destroy(match.net);
}
