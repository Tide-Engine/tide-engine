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
#define SCRIPTED_HASH UINT64_C(0x61d5af1dfe471339)
static tide_world world;

static tide_player_id player(const int seat) { return tide_player_from_index(seat); }

static void input(tide_world *w, const int seat, Controls control)
{
    control.away = false;
    tide_world_set_input(w, player(seat), control);
}

static void tick(const int count)
{
    // Convenient 60 Hz fixture frames; each contains two real 120 Hz ticks.
    for (int i = 0; i < count * 2; i++) tide_world_tick(&world);
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
    world.MatchState.cars.at[0] = car(tide_f3(3, 0.38f, -24), 1);
    world.MatchState.cars.at[1] = car(tide_f3(-25, 0.38f, 25), -1);
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

TIDE_TEST(afterburn_acceleration_reverse_steering_and_boost)
{
    fixture();
    input(&world, 0, (Controls){.throttle = 1});
    tick(60);
    TIDE_CHECK(world.MatchState.cars.at[0].position.z > -20);
    TIDE_CHECK(speed(world.MatchState.cars.at[0]) > 9);
    const float normal = speed(world.MatchState.cars.at[0]);
    fixture();
    input(&world, 0, (Controls){.throttle = 1, .boost = true});
    tick(60);
    TIDE_CHECK(speed(world.MatchState.cars.at[0]) > normal);
    TIDE_CHECK(world.MatchState.cars.at[0].boost < 100);
    TIDE_CHECK(world.MatchState.cars.at[0].boost >= 0);
    fixture();
    input(&world, 0, (Controls){.throttle = -1});
    tick(45);
    TIDE_CHECK(world.MatchState.cars.at[0].position.z < -25);
    fixture();
    input(&world, 0, (Controls){.throttle = 1, .steer = 1});
    tick(45);
    TIDE_CHECK(tide_abs_f(world.MatchState.cars.at[0].forward.x) > 0.1f);
    TIDE_CHECK(tide_abs_f(world.MatchState.cars.at[0].position.x - 3) > 0.2f);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_jump_release_and_directional_second_jump)
{
    fixture();
    input(&world, 0, (Controls){.jump = true});
    tick(1);
    TIDE_REQUIRE(world.MatchState.cars.at[0].position.y > 0.4f);
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
    TIDE_CHECK(world.MatchState.cars.at[0].position.y >= 0.37f);
    TIDE_CHECK(world.MatchState.cars.at[0].position.y < 0.6f);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_holding_jump_adds_height)
{
    fixture();
    tide_world short_jump = {0};
    tide_world_copy(&short_jump, &world);
    input(&world, 0, (Controls){.jump = true});
    input(&short_jump, 0, (Controls){.jump = true});
    for (int i = 0; i < 2; i++) tide_world_tick(&short_jump);
    input(&short_jump, 0, (Controls){0});
    for (int i = 0; i < 46; i++) tide_world_tick(&short_jump);
    tick(24);
    TIDE_CHECK(world.MatchState.cars.at[0].position.y > short_jump.MatchState.cars.at[0].position.y + 0.2f);
    TIDE_CHECK(!world.MatchState.cars.at[0].grounded && !short_jump.MatchState.cars.at[0].grounded);
    tide_world_free(&short_jump);
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
    world.MatchState.cars.at[0] = car(tide_f3(36.8f, 0.38f, 0), 1);
    world.MatchState.cars.at[0].forward = tide_f3(1, 0, 0);
    world.MatchState.cars.at[0].velocity = tide_f3(15, 0, 0);
    input(&world, 0, (Controls){.throttle = 1});
    tick(30);
    TIDE_CHECK(world.MatchState.cars.at[0].position.y > 1);
    TIDE_CHECK(world.MatchState.cars.at[0].up.x < -0.2f);
    TIDE_CHECK(world.MatchState.cars.at[0].grounded);
    // A wheel-aligned wall contact is a grounded surface: its jump impulse
    // must leave the wall along its inward normal, not world vertical.
    world.MatchState.cars.at[0] = car(tide_f3(40.58f, 5, 0), 1);
    world.MatchState.cars.at[0].up = tide_f3(-1, 0, 0);
    world.MatchState.cars.at[0].velocity = tide_f3(0, 0, 12);
    tick(8);
    TIDE_CHECK(world.MatchState.cars.at[0].grounded);
    TIDE_CHECK(world.MatchState.cars.at[0].up.x < -0.95f);
    input(&world, 0, (Controls){.jump = true});
    tick(1);
    TIDE_CHECK(!world.MatchState.cars.at[0].grounded);
    TIDE_CHECK(world.MatchState.cars.at[0].velocity.x < -2);
    TIDE_CHECK(world.MatchState.cars.at[0].position.x < 40.53f);
    tide_world_free(&world);
}

TIDE_TEST(afterburn_large_and_small_boost_pads_deplete_and_respawn)
{
    fixture();
    world.MatchState.cars.at[0] = car(tide_f3(-35.84f, 0.38f, -40.96f), 1);
    world.MatchState.cars.at[0].boost = 0;
    tick(1);
    TIDE_CHECK(world.MatchState.cars.at[0].boost == 100);
    TIDE_CHECK(world.MatchState.pads.at[0] > 9.9f);
    world.MatchState.cars.at[0].boost = 10;
    tick(10);
    TIDE_CHECK(world.MatchState.cars.at[0].boost == 10);
    // Leave the pad while it respawns, then drive onto the same real pickup.
    world.MatchState.cars.at[0].position = tide_f3(3, 0.38f, -24);
    tick(610);
    TIDE_CHECK(world.MatchState.pads.at[0] == 0);
    world.MatchState.cars.at[0].position = tide_f3(-35.84f, 0.38f, -40.96f);
    tick(1);
    TIDE_CHECK(world.MatchState.cars.at[0].boost == 100);
    fixture();
    world.MatchState.cars.at[0] = car(tide_f3(-17.92f, 0.38f, -42.24f), 1);
    world.MatchState.cars.at[0].boost = 20;
    tick(1);
    TIDE_CHECK(world.MatchState.cars.at[0].boost == 32);
    TIDE_CHECK(world.MatchState.pads.at[6] > 3.9f && world.MatchState.pads.at[6] <= 4);
    world.MatchState.cars.at[0].position = tide_f3(3, 0.38f, -24);
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

TIDE_TEST(afterburn_supersonic_front_hit_demolishes_and_respawns)
{
    fixture();
    world.MatchState.cars.at[1] = car(tide_f3(3, 0.38f, -22.9f), -1);
    world.MatchState.cars.at[1].team = 1;
    world.MatchState.cars.at[0].velocity = tide_f3(0, 0, 22.5f);
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
    TIDE_CHECK(world.MatchState.cars.at[1].boost == 33);
    TIDE_CHECK(world.MatchState.cars.at[1].position.z > 35);
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
        TIDE_CHECK(ball.position.y >= 0.90f && ball.position.y <= 20.45f);
        const float ball_speed = tide_sqrt_f(ball.velocity.x * ball.velocity.x + ball.velocity.y * ball.velocity.y
            + ball.velocity.z * ball.velocity.z);
        if (ball_speed > fastest_ball) fastest_ball = ball_speed;
        for (int seat = 0; seat < 2; seat++) {
            const CarState value = world.MatchState.cars.at[seat];
            TIDE_CHECK(finite3(value.position) && finite3(value.velocity) && finite3(value.forward) && finite3(value.up));
            TIDE_CHECK(value.boost >= 0 && value.boost <= 100);
            TIDE_CHECK(speed(value) < 24);
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
        .pitch = at % 180 < 90 ? -0.4f : 0.6f, .yaw = seat ? -0.3f : 0.3f,
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
