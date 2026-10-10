// Aerial's match on the generated world: the driving model against the
// numbers it's built to, the rules, and the same match on a lossy network.
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
#define SCRIPTED_HASH UINT64_C(0xa9b16d2a5f2c8639)
static tide_world world;

static tide_player_id player(const int index) { return tide_player_from_index(index); }

static void input(tide_world *w, const int index, Controls control)
{
    control.away = false;
    tide_world_set_input(w, player(index), control);
}

static void ticks(const int count)
{
    for (int i = 0; i < count; i++) tide_world_tick(&world);
}

static void start(const int humans, const int size)
{
    tide_world_free(&world);
    tide_world_init(&world, DT);
    world.MatchState.wanted = size;
    for (int i = 0; i < humans; i++) {
        tide_world_player_joined(&world, player(i));
        input(&world, i, (Controls){0});
    }
    ticks(2);
}

static float length3(const tide_float3 v) { return tide_sqrt_f(v.x * v.x + v.y * v.y + v.z * v.z); }
static float dot3(const tide_float3 a, const tide_float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static tide_float3 axis(const CarState *car, const float x, const float y, const float z)
{
    return tide_rotate_q(car->rotation, tide_f3(x, y, z));
}
static bool finite3(const tide_float3 v) { return tide_is_finite_f(v.x) && tide_is_finite_f(v.y) && tide_is_finite_f(v.z); }
static CarState parked(const float x, const float z, const float facing)
{
    return (CarState){.position = {x, 0.1701f, z}, .rotation = {{0, tide_sin_f(facing * 0.5f), 0, tide_cos_f(facing * 0.5f)}},
        .boost = 100, .struck = -1000};
}

// One player on the pitch with a parked rival, in open play: the car at rest
// in the blue half facing the orange goal, the ball out of everyone's way.
static CarState *alone(void)
{
    start(2, 1);
    MatchState *m = &world.MatchState;
    m->phase = MatchPhase_Playing;
    m->waiting = false;
    m->ball.position = tide_f3(30, 0.9125f, -30);
    m->cars.at[0] = parked(0, -20, 0);
    m->cars.at[1] = parked(-30, 40, 3.14159265f);
    input(&world, 0, (Controls){0});
    input(&world, 1, (Controls){0});
    return &m->cars.at[0];
}

TIDE_TEST(aerial_car_rests_on_its_springs)
{
    CarState *car = alone();
    ticks(240);
    TIDE_CHECK(car->wheels == 4 && car->grounded && !car->touching);
    TIDE_CHECK(car->position.y > 0.16f && car->position.y < 0.18f);
    TIDE_CHECK(length3(car->velocity) < 0.02f && length3(car->spin) < 0.02f);
    // Nose a touch down, as the rear springs are the stiffer
    TIDE_CHECK(axis(car, 0, 0, 1).y < 0 && axis(car, 0, 0, 1).y > -0.03f);
    tide_world_free(&world);
}

TIDE_TEST(aerial_throttle_boost_and_their_top_speeds)
{
    CarState *car = alone();
    input(&world, 0, (Controls){.throttle = 1});
    ticks(120);
    TIDE_CHECK(length3(car->velocity) > 9.5f && length3(car->velocity) < 10.6f);
    ticks(300);
    TIDE_CHECK(length3(car->velocity) > 14.0f && length3(car->velocity) <= 14.11f);
    TIDE_CHECK(!car->supersonic);
    car = alone();
    car->position.x = 6; // Clear of the pads down the middle
    input(&world, 0, (Controls){.throttle = 1, .boost = true});
    ticks(180);
    TIDE_CHECK(car->boost > 49.5f && car->boost < 50.5f); // A third of a tank a second
    TIDE_CHECK(length3(car->velocity) > 19 && !car->supersonic);
    ticks(60);
    TIDE_CHECK(length3(car->velocity) > 22.99f && length3(car->velocity) <= 23.001f);
    TIDE_CHECK(car->supersonic && car->boosting);
    input(&world, 0, (Controls){.throttle = -1, .boost = true});
    car->boost = 0.2f;
    ticks(30);
    TIDE_CHECK(car->boost == 0 && !car->boosting);
    tide_world_free(&world);
}

TIDE_TEST(aerial_coasting_braking_and_reverse)
{
    CarState *car = alone();
    car->position.x = 6;
    input(&world, 0, (Controls){.throttle = 1});
    ticks(420);
    input(&world, 0, (Controls){0});
    ticks(120);
    // Coasting sheds 5.25 m/s a second
    TIDE_CHECK(length3(car->velocity) > 8.6f && length3(car->velocity) < 9.1f);
    input(&world, 0, (Controls){.throttle = -1});
    ticks(36);
    TIDE_CHECK(length3(car->velocity) < 0.6f); // The brake sheds 35
    ticks(240);
    TIDE_CHECK(dot3(car->velocity, axis(car, 0, 0, 1)) < -9);
    tide_world_free(&world);
}

static float turning_radius(const CarState *car)
{
    const float yaw = dot3(car->spin, axis(car, 0, 1, 0));
    return length3(car->velocity) / (tide_abs_f(yaw) < 0.001f ? 0.001f : yaw);
}

TIDE_TEST(aerial_turning_circle_widens_with_speed)
{
    CarState *car = alone();
    input(&world, 0, (Controls){.throttle = 1, .steer = 1});
    ticks(480);
    // Flat out on full lock settles near 12 m/s, on a circle of about 5 m
    TIDE_CHECK(length3(car->velocity) > 11.4f && length3(car->velocity) < 12.6f);
    TIDE_CHECK(turning_radius(car) > 4.5f && turning_radius(car) < 5.6f);
    car = alone();
    input(&world, 0, (Controls){.throttle = 1, .steer = 1});
    ticks(60);
    TIDE_CHECK(car->position.x > -0.0f && axis(car, 0, 0, 1).x > 0.3f); // Steering right turns right
    car = alone();
    car->position = tide_f3(-30, 0.1701f, -44);
    input(&world, 0, (Controls){.throttle = 1, .boost = true});
    ticks(230);
    input(&world, 0, (Controls){.throttle = 1, .boost = true, .steer = 1});
    ticks(90);
    // At 23 m/s the same lock is a circle of over 11 m
    TIDE_CHECK(length3(car->velocity) > 22.5f);
    TIDE_CHECK(turning_radius(car) > 10.5f && turning_radius(car) < 12.6f);
    tide_world_free(&world);
}

TIDE_TEST(aerial_powerslide_lets_the_tail_out)
{
    float sideways[2];
    for (int slide = 0; slide < 2; slide++) {
        CarState *car = alone();
        car->position.x = 6;
        input(&world, 0, (Controls){.throttle = 1});
        ticks(300);
        input(&world, 0, (Controls){.throttle = 1, .steer = 1, .handbrake = slide == 1});
        ticks(50);
        sideways[slide] = tide_abs_f(dot3(car->velocity, axis(car, 1, 0, 0)));
    }
    printf("    sideways at 14 m/s on full lock: %.2f m/s gripping, %.2f sliding\n", sideways[0], sideways[1]);
    TIDE_CHECK(sideways[0] < 2.5f);
    TIDE_CHECK(sideways[1] > sideways[0] * 2 && sideways[1] > 4);
    tide_world_free(&world);
}

static float jump_top(const int held, const bool again)
{
    CarState *car = alone();
    ticks(60);
    float top = 0;
    for (int i = 0; i < 360; i++) {
        input(&world, 0, (Controls){.jump = i < held || (again && i >= held + 3 && i < held + 6)});
        ticks(1);
        if (car->position.y > top) top = car->position.y;
    }
    TIDE_CHECK(car->wheels == 4 && !car->jumped);
    return top;
}

TIDE_TEST(aerial_jump_height_by_how_long_it_is_held)
{
    const float tap = jump_top(2, false), held = jump_top(40, false), twice = jump_top(24, true);
    printf("    jumps reach %.2f, %.2f and %.2f m\n", tap, held, twice);
    TIDE_CHECK(tap > 0.7f && tap < 1.1f);
    TIDE_CHECK(held > 2.2f && held < 2.5f);
    TIDE_CHECK(twice > 4.6f && twice < 5.2f);
    tide_world_free(&world);
}

TIDE_TEST(aerial_one_dodge_and_only_within_its_window)
{
    // A forward dodge at speed: five metres a second more, a whole turn, and back on its wheels
    CarState *car = alone();
    car->position.x = 6;
    input(&world, 0, (Controls){.throttle = 1});
    ticks(240);
    const float before = length3(car->velocity);
    input(&world, 0, (Controls){.throttle = 1, .jump = true});
    ticks(8);
    input(&world, 0, (Controls){.throttle = 1});
    ticks(4);
    input(&world, 0, (Controls){.throttle = 1, .jump = true, .pitch = -1});
    ticks(2);
    TIDE_CHECK(car->flipped && car->flipping);
    TIDE_CHECK(length3(car->velocity) > before + 4.5f && length3(car->velocity) < before + 6);
    input(&world, 0, (Controls){.throttle = 1});
    ticks(40);
    TIDE_CHECK(length3(car->spin) > 5.4f && length3(car->spin) <= 5.501f);
    TIDE_CHECK(dot3(car->spin, axis(car, 1, 0, 0)) > 5); // Nose over
    // A second press does nothing: one dodge a jump
    const tide_float3 flying = car->velocity;
    input(&world, 0, (Controls){.jump = true, .yaw = 1});
    ticks(1);
    TIDE_CHECK(tide_abs_f(car->velocity.x - flying.x) < 0.2f);
    input(&world, 0, (Controls){.throttle = 1});
    ticks(200);
    TIDE_CHECK(car->wheels == 4 && axis(car, 0, 1, 0).y > 0.95f && !car->flipped);

    // A sideways dodge goes sideways, and rolls
    car = alone();
    input(&world, 0, (Controls){.jump = true});
    ticks(6);
    input(&world, 0, (Controls){0});
    ticks(4);
    input(&world, 0, (Controls){.jump = true, .yaw = 1});
    ticks(3);
    TIDE_CHECK(car->velocity.x > 4.5f && car->velocity.x < 5.5f);
    TIDE_CHECK(dot3(car->spin, axis(car, 0, 0, 1)) < -3);

    // Past a second and a quarter in the air there's no second jump left
    car = alone();
    car->position.y = 14;
    car->jumped = true;
    input(&world, 0, (Controls){0});
    ticks(156);
    const float falling = car->velocity.y;
    input(&world, 0, (Controls){.jump = true});
    ticks(1);
    TIDE_CHECK(!car->doubleJumped && !car->flipped && car->velocity.y < falling);
    // ...but a car that never jumped, knocked into the air, has it for as long as it falls
    car = alone();
    car->position.y = 16;
    input(&world, 0, (Controls){0});
    ticks(200);
    input(&world, 0, (Controls){.jump = true});
    ticks(1);
    TIDE_CHECK(car->doubleJumped);
    tide_world_free(&world);
}

TIDE_TEST(aerial_air_control_and_boost_fly_the_car)
{
    CarState *car = alone();
    car->position.y = 9;
    input(&world, 0, (Controls){.pitch = 1});
    ticks(40);
    TIDE_CHECK(axis(car, 0, 0, 1).y > 0.5f); // Pulled back, the nose comes up
    TIDE_CHECK(length3(car->spin) <= 5.501f);
    car = alone();
    car->position.y = 9;
    input(&world, 0, (Controls){.roll = 1});
    ticks(30);
    TIDE_CHECK(axis(car, 1, 0, 0).y < -0.5f); // Rolling right drops the right side
    car = alone();
    car->position.y = 9;
    input(&world, 0, (Controls){.yaw = 1});
    ticks(60);
    TIDE_CHECK(axis(car, 0, 0, 1).x > 0.5f);
    // Nose to the sky, boost beats gravity: this is how a car flies
    car = alone();
    car->position.y = 6;
    car->rotation = (tide_quaternion){{-0.70710678f, 0, 0, 0.70710678f}};
    TIDE_REQUIRE(axis(car, 0, 0, 1).y > 0.99f);
    input(&world, 0, (Controls){.boost = true});
    ticks(120);
    TIDE_CHECK(car->velocity.y > 3 && car->position.y > 7.5f && car->boost < 70);
    tide_world_free(&world);
}

TIDE_TEST(aerial_walls_and_ceiling_are_driven_on)
{
    CarState *car = alone();
    car->rotation = (tide_quaternion){{0, 0.70710678f, 0, 0.70710678f}};
    car->position = tide_f3(30, 0.1701f, 0);
    car->velocity = tide_f3(14, 0, 0);
    input(&world, 0, (Controls){.throttle = 1, .boost = true});
    ticks(120);
    // Up the ramp and onto the side wall, wheels on it
    TIDE_CHECK(car->wheels == 4 && axis(car, 0, 1, 0).x < -0.98f);
    TIDE_CHECK(car->position.x > 40.6f && car->position.x < 40.96f && car->position.y > 4);
    // ...and on over the top onto the ceiling, upside down
    bool reached = false;
    for (int i = 0; i < 240 && !reached; i++) {
        ticks(1);
        reached = car->position.y > 19.8f && axis(car, 0, 1, 0).y < -0.9f;
    }
    TIDE_REQUIRE(reached);
    // Tyres hold a wall, but not a roof: it drops away. It never jumped,
    // so the dodge it has left is there all the way down.
    input(&world, 0, (Controls){0});
    ticks(90);
    TIDE_CHECK(car->wheels == 0 && car->position.y < 19.5f && !car->jumped);
    input(&world, 0, (Controls){.jump = true, .pitch = -1});
    ticks(2);
    TIDE_CHECK(car->flipped && car->flipping);
    tide_world_free(&world);
}

TIDE_TEST(aerial_an_overturned_car_rights_itself_at_a_jump)
{
    CarState *car = alone();
    car->rotation = (tide_quaternion){{0, 0, 1, 0}}; // On its roof
    car->position.y = 0.5f;
    ticks(120);
    TIDE_REQUIRE(car->touching && car->wheels == 0);
    TIDE_CHECK(axis(car, 0, 1, 0).y < -0.9f);
    input(&world, 0, (Controls){.jump = true});
    ticks(3);
    input(&world, 0, (Controls){0});
    ticks(240);
    TIDE_CHECK(car->wheels == 4 && axis(car, 0, 1, 0).y > 0.95f);
    tide_world_free(&world);
}

TIDE_TEST(aerial_ball_bounces_spins_and_keeps_to_its_top_speed)
{
    alone();
    BallState *ball = &world.MatchState.ball;
    *ball = (BallState){.position = {10, 10, 10}, .rotation = {{0, 0, 0, 1}}, .live = true};
    float top = 0;
    bool landed = false;
    for (int i = 0; i < 600; i++) {
        ticks(1);
        if (ball->velocity.y > 0) landed = true;
        if (landed && ball->position.y > top) top = ball->position.y;
        if (landed && ball->velocity.y < 0) break;
    }
    // It keeps 0.6 of its speed, so a little over a third of the fall's height
    TIDE_CHECK(top > 3.7f && top < 4.3f);
    // Thrown along the grass with no spin, it's gripped into rolling
    *ball = (BallState){.position = {10, 0.9125f, 10}, .velocity = {12, 0, 0}, .rotation = {{0, 0, 0, 1}}, .live = true};
    ticks(120);
    TIDE_CHECK(ball->velocity.x > 7 && ball->velocity.x < 9 && ball->spin.z < -5.5f);
    // It turns no faster than 6 rad/s, so a quick one goes on scrubbing speed
    ticks(240);
    TIDE_CHECK(ball->velocity.x > 4.5f && ball->velocity.x < 6 && ball->spin.z > -6.2f);
    TIDE_CHECK(tide_abs_f(ball->spin.z + ball->velocity.x / 0.9125f) < 0.4f);
    // It settles rather than bouncing for ever
    *ball = (BallState){.position = {10, 3, 10}, .rotation = {{0, 0, 0, 1}}, .live = true};
    ticks(1500);
    TIDE_CHECK(ball->position.y < 0.92f && tide_abs_f(ball->velocity.y) < 0.06f && ball->floored);
    // ...and nothing sends it past 60 m/s
    *ball = (BallState){.position = {0, 5, 0}, .velocity = {300, 0, 0}, .rotation = {{0, 0, 0, 1}}, .live = true};
    ticks(1);
    TIDE_CHECK(length3(ball->velocity) <= 60.001f);
    ticks(600);
    TIDE_CHECK(tide_abs_f(ball->position.x) < 40.96f && ball->position.y > 0.9f && ball->position.y < 20.48f);
    tide_world_free(&world);
}

static float kick(const bool dodge)
{
    CarState *car = alone();
    MatchState *m = &world.MatchState;
    car->position.x = 6;
    m->ball.position = tide_f3(6, 0.9125f, 10);
    input(&world, 0, (Controls){.throttle = 1});
    for (int i = 0; i < 900 && m->toucher < 0; i++) {
        const float gap = m->ball.position.z - car->position.z;
        const bool air = !car->grounded;
        if (dodge && gap < 4.2f && !car->flipped)
            input(&world, 0, (Controls){.throttle = 1, .jump = !air || car->jumpTime < 0.07f || car->jumpTime > 0.1f,
                .pitch = air && car->jumpTime > 0.1f ? -1.0f : 0.0f});
        ticks(1);
    }
    TIDE_CHECK(m->toucher == 0 && m->last.at[0] == 0);
    ticks(4);
    return length3(m->ball.velocity);
}

TIDE_TEST(aerial_a_car_strikes_the_ball_and_a_dodge_strikes_it_harder)
{
    const float driven = kick(false), dodged = kick(true);
    printf("    driven into at 12.5 m/s the ball leaves at %.1f, dodged into at %.1f\n", driven, dodged);
    TIDE_CHECK(driven > 15 && driven < 22);
    TIDE_CHECK(dodged > driven + 2);
    TIDE_CHECK(world.MatchState.ball.velocity.z > 10 && world.MatchState.ball.velocity.y > 1);
    tide_world_free(&world);
}

TIDE_TEST(aerial_a_ball_on_the_roof_is_carried_not_punched)
{
    CarState *car = alone();
    MatchState *m = &world.MatchState;
    car->position.x = 6;
    m->ball = (BallState){.position = {6, 1.4715f, -19.86f}, .rotation = {{0, 0, 0, 1}}, .live = true};
    ticks(240);
    // Set down on the roof, it stays there: touched every tick, never knocked away
    TIDE_CHECK(m->toucher == 0);
    TIDE_CHECK(length3(tide_sub_f3(m->ball.position, car->position)) < 1.6f && m->ball.position.y > 1.3f);
    TIDE_CHECK(length3(m->ball.velocity) < 0.5f && car->wheels == 4);
    tide_world_free(&world);
}

static void shoot(const int toucher, const float x, const float y, const float z, const float vz)
{
    MatchState *m = &world.MatchState;
    m->ball = (BallState){.position = {x, y, z}, .velocity = {0, 0, vz}, .rotation = {{0, 0, 0, 1}}, .live = true};
    m->toucher = toucher;
    m->last.at[toucher % 2] = toucher;
    m->lastAt.at[toucher % 2] = m->played;
}

TIDE_TEST(aerial_goals_posts_and_the_kickoff_after)
{
    alone();
    MatchState *m = &world.MatchState;
    // Wide of the post and over the bar: off the wall, and no goal
    shoot(0, 9.6f, 2, 48, 30);
    ticks(60);
    TIDE_CHECK(m->score.at[0] == 0 && m->ball.velocity.z < 0);
    shoot(0, 0, 7.2f, 48, 30);
    ticks(60);
    TIDE_CHECK(m->score.at[0] == 0 && m->ball.velocity.z < 0);
    // On the line is not over it: the whole ball has to cross
    shoot(0, 0, 0.9125f, 51.2f, 0);
    ticks(30);
    TIDE_CHECK(m->score.at[0] == 0 && m->phase == MatchPhase_Playing);
    // Inside the post, it's a goal to blue, with its scorer
    shoot(0, 7.5f, 3, 48, 30);
    ticks(40);
    TIDE_REQUIRE(m->score.at[0] == 1 && m->score.at[1] == 0);
    TIDE_CHECK(m->phase == MatchPhase_Goal && !m->ball.live);
    TIDE_CHECK(m->scorer == 0 && m->cars.at[0].goals == 1 && m->cars.at[0].points >= 100);
    TIDE_CHECK(m->scoredSpeed > 25);
    const float clock = m->clock;
    ticks(120 * 4);
    // Then a kickoff: cars back on their marks, the ball on the spot, the clock held
    TIDE_REQUIRE(m->phase == MatchPhase_Countdown);
    TIDE_CHECK(m->ball.live && m->ball.position.x == 0 && m->ball.position.z == 0);
    TIDE_CHECK(m->cars.at[0].position.z < -20 && m->cars.at[1].position.z > 20);
    TIDE_CHECK(m->cars.at[0].position.x == -m->cars.at[1].position.x && m->cars.at[0].position.z == -m->cars.at[1].position.z);
    TIDE_CHECK(m->cars.at[0].goals == 1 && m->cars.at[0].boost > 33 && m->cars.at[0].boost < 34);
    input(&world, 0, (Controls){.throttle = 1});
    const tide_float3 mark = m->cars.at[0].position;
    ticks(120);
    TIDE_CHECK(tide_abs_f(m->cars.at[0].position.z - mark.z) < 0.05f); // Nobody moves in the countdown
    ticks(120 * 3);
    TIDE_CHECK(m->phase == MatchPhase_Playing && m->waiting);
    TIDE_CHECK(m->clock == clock); // ...and the clock waits for the first touch
    TIDE_CHECK(m->cars.at[0].position.z > mark.z + 5);
    // An own goal is the other side's, and nobody's by name
    shoot(1, 0, 2, 48, 30);
    m->last.at[0] = -1;
    ticks(40);
    TIDE_CHECK(m->score.at[0] == 2 && m->scorer == -1 && m->cars.at[1].goals == 0);
    tide_world_free(&world);
}

TIDE_TEST(aerial_an_assist_is_the_pass_before_the_goal)
{
    start(4, 2);
    MatchState *m = &world.MatchState;
    m->phase = MatchPhase_Playing;
    m->waiting = false;
    for (int seat = 0; seat < 4; seat++) m->cars.at[seat] = parked(-30 + seat * 4.0f, -40, 0);
    m->played = 20;
    // Car 0 touches three seconds after car 2 did, and it goes in
    m->ball = (BallState){.position = {7, 3, 48}, .velocity = {0, 0, 30}, .rotation = {{0, 0, 0, 1}}, .live = true};
    m->before.at[0] = 2;
    m->beforeAt.at[0] = 17;
    m->last.at[0] = 0;
    m->lastAt.at[0] = 20;
    m->toucher = 0;
    ticks(40);
    TIDE_REQUIRE(m->score.at[0] == 1);
    TIDE_CHECK(m->cars.at[0].goals == 1 && m->cars.at[2].assists == 1 && m->cars.at[2].points == 50);
    tide_world_free(&world);
}

TIDE_TEST(aerial_shots_and_saves_are_told_from_where_the_ball_was_going)
{
    alone();
    MatchState *m = &world.MatchState;
    CarState *keeper = &m->cars.at[1];
    // Blue rolls it at the orange goal from midfield: a shot, once it's within three seconds of going in
    m->cars.at[0] = parked(-20, 0, 0);
    *keeper = parked(30, 30, 3.14159265f);
    m->ball = (BallState){.position = {3, 0.9125f, 8}, .velocity = {0, 0, 16}, .spin = {6, 0, 0}, .rotation = {{0, 0, 0, 1}}, .live = true};
    m->toucher = 0;
    m->last.at[0] = 0;
    ticks(2);
    TIDE_CHECK(m->threat < 0 && m->cars.at[0].shots == 0);
    for (int i = 0; i < 900 && m->threat < 0; i++) {
        m->lastAt.at[0] = m->played; // As if blue had only just struck it
        ticks(1);
    }
    TIDE_REQUIRE(m->threat == 1);
    TIDE_CHECK(m->cars.at[0].shots == 1 && m->cars.at[0].points == 10 && m->threatIn > 2.5f);
    // Orange drives out to meet it: a save
    *keeper = parked(3, 46, 3.14159265f);
    input(&world, 1, (Controls){.throttle = 1});
    for (int i = 0; i < 360 && m->toucher != 1; i++) ticks(1);
    TIDE_REQUIRE(m->toucher == 1);
    ticks(12);
    TIDE_CHECK(m->score.at[0] == 0 && m->threat < 0 && m->ball.velocity.z < 0);
    TIDE_CHECK(keeper->saves == 1 && keeper->points >= 50);
    tide_world_free(&world);
}

TIDE_TEST(aerial_the_clock_overtime_and_the_last_touch)
{
    alone();
    MatchState *m = &world.MatchState;
    m->clock = 0.1f;
    // Level at 0:00 with the ball in the air: play goes on until it lands
    m->ball = (BallState){.position = {20, 8, 0}, .rotation = {{0, 0, 0, 1}}, .live = true};
    ticks(60);
    TIDE_CHECK(m->clock == 0 && m->phase == MatchPhase_Playing && !m->overtime);
    ticks(240);
    // ...then overtime: a kickoff, and a clock that counts up
    TIDE_REQUIRE(m->overtime && m->phase == MatchPhase_Countdown);
    ticks(120 * 3 + 2);
    m->waiting = false;
    ticks(120);
    TIDE_CHECK(m->clock > 0.9f && m->clock < 1.1f && m->winner < 0);
    // The next goal wins it
    shoot(1, 0, 2, -48, -30);
    ticks(40);
    TIDE_CHECK(m->score.at[1] == 1 && m->winner == 1 && m->phase == MatchPhase_Goal);
    ticks(120 * 4);
    TIDE_CHECK(m->phase == MatchPhase_Finished && m->winner == 1);
    // After the result, another match starts by itself
    ticks(120 * 15);
    TIDE_CHECK(m->phase != MatchPhase_Finished && m->score.at[1] == 0 && m->clock == 300 && !m->overtime);

    // Ahead at 0:00 with the ball on the grass: that's the match
    alone();
    m->score.at[0] = 2;
    m->clock = 0.05f;
    ticks(30);
    TIDE_CHECK(m->phase == MatchPhase_Finished && m->winner == 0);
    tide_world_free(&world);
}

TIDE_TEST(aerial_boost_pads_fill_and_come_back)
{
    CarState *car = alone();
    MatchState *m = &world.MatchState;
    // A small pad on the centre line gives twelve, and is back in four seconds
    car->boost = 10;
    car->position = tide_f3(0, 0.1701f, -10.24f);
    ticks(2);
    TIDE_CHECK(car->boost == 22 && m->pads.at[30] > 3.9f);
    ticks(120 * 3);
    TIDE_CHECK(car->boost == 22);
    ticks(125);
    TIDE_CHECK(car->boost == 34);
    // A big one fills the tank, and takes ten
    car->position = tide_f3(-35.84f, 0.1701f, 0);
    ticks(2);
    TIDE_CHECK(car->boost == 100 && m->pads.at[4] > 9.9f);
    // A full tank leaves a pad where it is
    car->position = tide_f3(10.24f, 0.1701f, 0);
    ticks(2);
    TIDE_CHECK(m->pads.at[33] <= 0);
    // ...and so does a car flying over it
    car->boost = 0;
    car->position = tide_f3(10.24f, 4, 0);
    ticks(1);
    TIDE_CHECK(car->boost == 0);
    tide_world_free(&world);
}

TIDE_TEST(aerial_bumps_and_demolitions)
{
    CarState *car = alone();
    MatchState *m = &world.MatchState;
    CarState *rival = &m->cars.at[1];
    // A supersonic car, nose first into a rival: gone for three seconds
    *rival = parked(6, 0, 1.5707963f);
    car->position = tide_f3(6, 0.1701f, -3);
    car->velocity = tide_f3(0, 0, 22.8f);
    car->supersonic = true;
    input(&world, 0, (Controls){.throttle = 1, .boost = true});
    ticks(30);
    TIDE_REQUIRE(rival->wrecked > 2.5f);
    TIDE_CHECK(car->wrecks == 1 && car->wrecked <= 0);
    ticks(120 * 3);
    // ...then back in front of its own goal, on its wheels
    TIDE_CHECK(rival->wrecked <= 0 && rival->position.z > 40 && rival->boost > 33 && rival->boost < 34);
    // Below that speed it's a shove
    car = alone();
    *rival = parked(6, 0, 1.5707963f);
    car->position = tide_f3(6, 0.1701f, -3);
    car->velocity = tide_f3(0, 0, 14);
    input(&world, 0, (Controls){.throttle = 1});
    ticks(30);
    TIDE_CHECK(rival->wrecked <= 0 && rival->velocity.z > 6 && rival->velocity.y >= 0);
    // ...and nobody wrecks their own side
    start(4, 2);
    m->phase = MatchPhase_Playing;
    m->cars.at[2] = parked(6, 0, 1.5707963f);
    m->cars.at[0] = parked(6, -3, 0);
    m->cars.at[0].velocity = tide_f3(0, 0, 22.8f);
    m->cars.at[0].supersonic = true;
    m->cars.at[1] = parked(30, 30, 0);
    m->cars.at[3] = parked(-30, 30, 0);
    ticks(30);
    TIDE_CHECK(m->cars.at[2].wrecked <= 0 && m->cars.at[0].wrecks == 0);
    tide_world_free(&world);
}

static bool restart(const int index)
{
    uint8_t bytes[TIDE_COMMAND_BYTES];
    tide_bits bits = {bytes, TIDE_COMMAND_BYTES, 0, false};
    tide_bits_put(&bits, TIDE_COMMAND_ID_Restart, 16);
    tide_bits_put(&bits, 0, 32);
    tide_bits_put(&bits, 0, 32);
    const uint32_t size = tide_bits_end(&bits);
    const tide_player_id sender = player(index);
    return tide_game_api.command(&world, sender, 0,
        tide_game_api.command_sequence(&world, sender, 0) + 1, bytes, size);
}

TIDE_TEST(aerial_seats_sides_and_bots)
{
    start(0, 2);
    MatchState *m = &world.MatchState;
    TIDE_CHECK(m->started && m->size == 2 && m->seated == 0);
    // Players take sides in turn: blue, orange, blue, orange
    for (int i = 0; i < 4; i++) tide_world_player_joined(&world, player(i));
    ticks(2);
    TIDE_CHECK(m->seated == 15 && m->size == 2);
    // A fifth makes the teams three a side, and the new cars drive on
    tide_world_player_joined(&world, player(4));
    ticks(2);
    TIDE_CHECK(m->seated == 31 && m->size == 3);
    TIDE_CHECK(m->cars.at[4].position.z < -40 && m->cars.at[5].position.z > 40);
    tide_world_player_joined(&world, player(5));
    ticks(2);
    TIDE_CHECK(m->seated == 63);
    // Someone leaving frees their seat, for a bot now and whoever comes next
    tide_world_player_left(&world, player(1));
    ticks(2);
    TIDE_CHECK(m->seated == 61 && !m->manned.at[1]);
    tide_world_player_joined(&world, player(1));
    ticks(2);
    TIDE_CHECK(m->seated == 63);
    // A seat whose player is in a menu is the bot's meanwhile
    input(&world, 0, (Controls){.throttle = 1});
    ticks(2);
    TIDE_CHECK(m->manned.at[0]);
    tide_world_set_input(&world, player(0), (Controls){.away = true});
    ticks(2);
    TIDE_CHECK(!m->manned.at[0]);
    // The teams shrink at the next kickoff, to no fewer than its players need
    for (int i = 2; i < 6; i++) tide_world_player_left(&world, player(i));
    m->wanted = 1;
    ticks(2);
    TIDE_CHECK(m->size == 3);
    m->phase = MatchPhase_Goal;
    m->phaseTime = 0.01f;
    ticks(3);
    TIDE_CHECK(m->phase == MatchPhase_Countdown && m->size == 1);
    // Only someone in the match starts it over
    m->score.at[0] = 3;
    tide_world_player_left(&world, player(1));
    ticks(1);
    (void)restart(1);
    ticks(1);
    TIDE_CHECK(m->score.at[0] == 3);
    TIDE_REQUIRE(restart(0));
    ticks(1);
    TIDE_CHECK(m->score.at[0] == 0 && m->clock == 300 && m->phase == MatchPhase_Countdown);
    tide_world_free(&world);
}

TIDE_TEST(aerial_bots_play_a_whole_match)
{
    start(0, 2);
    const MatchState *m = &world.MatchState;
    float fastest = 0;
    int touches = 0, last = -1, at = 0;
    for (; at < 120 * 600 && m->phase != MatchPhase_Finished; at++) {
        tide_world_tick(&world);
        if (m->toucher != last && m->toucher >= 0) touches++;
        last = m->toucher;
        if (at % 60 != 0) continue;
        TIDE_CHECK(finite3(m->ball.position) && finite3(m->ball.velocity));
        TIDE_CHECK(tide_abs_f(m->ball.position.x) <= 40.97f && tide_abs_f(m->ball.position.z) <= 60.01f);
        TIDE_CHECK(m->ball.position.y >= 0.9f && m->ball.position.y <= 20.49f);
        if (length3(m->ball.velocity) > fastest) fastest = length3(m->ball.velocity);
        for (int seat = 0; seat < 4; seat++) {
            const CarState *car = &m->cars.at[seat];
            TIDE_CHECK(finite3(car->position) && finite3(car->velocity) && finite3(car->spin));
            TIDE_CHECK(car->boost >= 0 && car->boost <= 100);
            TIDE_CHECK(length3(car->velocity) <= 23.001f && length3(car->spin) <= 5.501f);
            TIDE_CHECK(tide_abs_f(car->position.x) <= 41.2f && tide_abs_f(car->position.z) <= 60.3f);
            TIDE_CHECK(car->position.y >= -0.1f && car->position.y <= 20.6f);
        }
    }
    int shots = 0, saves = 0, goals = 0;
    for (int seat = 0; seat < 4; seat++) {
        shots += m->cars.at[seat].shots; saves += m->cars.at[seat].saves; goals += m->cars.at[seat].goals;
    }
    printf("    bots, two a side: %d-%d in %.0f s of play, %d changes of touch, %d shots, %d saves, fastest ball %.0f m/s\n",
        m->score.at[0], m->score.at[1], m->played, touches, shots, saves, fastest);
    TIDE_CHECK(m->phase == MatchPhase_Finished && m->winner >= 0 && m->winner < 2);
    TIDE_CHECK(m->score.at[0] + m->score.at[1] > 0 && goals <= m->score.at[0] + m->score.at[1]);
    TIDE_CHECK(m->played >= 299.9f && touches > 40 && shots > 0 && fastest > 20);
    tide_world_free(&world);
}

static uint32_t noise_state;
static float noise(void)
{
    noise_state = noise_state * 1664525u + 1013904223u;
    return (float)(noise_state >> 8) / 8388608.0f - 1.0f;
}

TIDE_TEST(aerial_six_cars_driven_at_random_stay_in_the_arena)
{
    start(6, 3);
    ticks(400);
    noise_state = 12345;
    Controls held[6] = {0};
    const MatchState *m = &world.MatchState;
    float outside = 0;
    for (int at = 0; at < 120 * 150; at++) {
        for (int seat = 0; seat < 6; seat++) {
            if (at % 23 == seat) held[seat] = (Controls){.throttle = noise() > -0.7f ? 1 : -1, .steer = noise(), .pitch = noise(),
                .yaw = noise(), .roll = noise() * 0.3f, .jump = noise() > 0.6f, .boost = noise() > 0, .handbrake = noise() > 0.8f};
            input(&world, seat, held[seat]);
        }
        tide_world_tick(&world);
        for (int seat = 0; seat < 6; seat++) {
            const CarState *car = &m->cars.at[seat];
            if (car->wrecked > 0) continue;
            TIDE_REQUIRE(finite3(car->position) && finite3(car->velocity) && finite3(car->spin));
            const float out = tide_max_f(tide_abs_f(car->position.x) - 40.96f,
                tide_max_f(tide_abs_f(car->position.z) - 60.0f, tide_max_f(-car->position.y, car->position.y - 20.48f)));
            if (out > outside) outside = out;
        }
        TIDE_REQUIRE(finite3(m->ball.position));
    }
    TIDE_CHECK(outside < 0.25f);
    tide_world_free(&world);
}

TIDE_TEST(aerial_input_bounds_and_nonfinite_repair)
{
    alone();
    tide_world reference = {0};
    tide_world_copy(&reference, &world);
    input(&world, 0, (Controls){.throttle = 1000, .steer = -1000, .pitch = 1000, .yaw = -1000, .roll = 1000});
    input(&reference, 0, (Controls){.throttle = 1, .steer = -1, .pitch = 1, .yaw = -1, .roll = 1});
    ticks(60);
    for (int i = 0; i < 60; i++) tide_world_tick(&reference);
    TIDE_CHECK(memcmp(&world.MatchState.cars.at[0], &reference.MatchState.cars.at[0], sizeof(CarState)) == 0);
    const uint32_t nan_bits = UINT32_C(0x7fc12345), inf_bits = UINT32_C(0x7f800000);
    float nan_value, inf_value;
    memcpy(&nan_value, &nan_bits, sizeof nan_value);
    memcpy(&inf_value, &inf_bits, sizeof inf_value);
    input(&world, 0, (Controls){.throttle = nan_value, .steer = inf_value, .pitch = -inf_value,
        .yaw = nan_value, .roll = inf_value, .boost = true, .jump = true});
    ticks(240);
    TIDE_CHECK(finite3(world.MatchState.cars.at[0].position) && finite3(world.MatchState.cars.at[0].velocity));
    TIDE_CHECK(finite3(world.MatchState.cars.at[0].spin) && finite3(world.MatchState.ball.position));
    tide_world_free(&reference);
    tide_world_free(&world);
}

static Controls script(const uint32_t at, const int seat)
{
    return (Controls){.throttle = at % 360 < 300 ? 1 : -0.5f,
        .steer = tide_sin_f((float)at * 0.013f + (float)seat * 2.1f) * 0.65f,
        .pitch = at % 180 < 90 ? -0.6f : 0.6f, .yaw = seat % 2 ? -0.3f : 0.3f,
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

TIDE_TEST(aerial_snapshot_pack_and_replay_preserve_the_match)
{
    // Two players and two bots, from a kickoff, on scripted controls
    start(2, 2);
    scripted_ticks(&world, 0, 600);
    tide_world saved = {0}, replay = {0}, unpacked = {0};
    tide_world_copy(&saved, &world);
    const uint64_t snapshot = tide_world_hash(&saved);
    const uint32_t size = tide_world_pack(&saved, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(bytes != NULL && size > 0);
    TIDE_CHECK(tide_world_pack(&saved, bytes, size) == size);
    TIDE_REQUIRE(tide_world_unpack(&unpacked, bytes, size));
    TIDE_CHECK(tide_world_hash(&unpacked) == snapshot);
    scripted_ticks(&world, 600, 2400);
    const uint64_t final = tide_world_hash(&world);
    tide_world_copy(&replay, &saved);
    scripted_ticks(&replay, 600, 2400);
    scripted_ticks(&unpacked, 600, 2400);
    TIDE_CHECK(tide_world_hash(&replay) == final);
    TIDE_CHECK(tide_world_hash(&unpacked) == final);
    TIDE_CHECK(tide_world_hash(&saved) == snapshot);
    printf("    exact simulation hash: 0x%016llx, a world of %u bytes packed\n", (unsigned long long)final, size);
    // The same bits on every platform: this is the number the web build has to reach too
    TIDE_CHECK(final == SCRIPTED_HASH);
    TIDE_CHECK(world.MatchState.toucher >= 0);
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

TIDE_TEST(aerial_two_clients_confirm_the_match_on_a_lossy_network)
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
    TIDE_CHECK(authority->MatchState.seated == 3 && authority->MatchState.size == 2);
    TIDE_CHECK(authority->MatchState.phase == MatchPhase_Playing || authority->MatchState.phase == MatchPhase_Goal);
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
