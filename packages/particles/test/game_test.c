#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game.h"
#include "tide_test.h"

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static ParticleProbe *start(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    local = (tide_local){0};
    world = (tide_world){0};
    draw = (tide_draw_list){0};
    gui = (tide_gui){0};
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.ParticleProbe.emitter.random = tide_random_seeded(23);
    return &local.ParticleProbe;
}

static void step(int operation)
{
    const uint64_t match = tide_world_hash(&world);
    local.ParticleProbe.operation = operation;
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1, NULL, NULL, 1, &local, &draw, &gui);
    TIDE_CHECK(tide_world_hash(&world) == match);
    TIDE_CHECK(world.MatchSentinel.value == 91);
}

static int count(void) { return tide_list_count(local.ParticleProbe.emitter.particles); }
static Particles_Particle particle(int index)
{
    return *(const Particles_Particle *)tide_list_at(local.ParticleProbe.emitter.particles, index, sizeof(Particles_Particle));
}

static void near1(float actual, float expected)
{
    TIDE_CHECK(isfinite(actual) && fabsf(actual - expected) <= 0.00001f * fmaxf(1, fabsf(expected)));
}

TIDE_TEST(particles_lifetime_integration_and_custom_updates)
{
    ParticleProbe *p = start();
    p->input.position = (tide_float3){1, 2, 3};
    p->input.velocity = (tide_float3){4, 0, 0};
    p->input.lifetime = 1;
    p->input.angularVelocity = 2;
    step(1);
    TIDE_CHECK(p->result == 1 && count() == 1);
    p->dt = .25f;
    p->acceleration = (tide_float3){0, -8, 0};
    p->drag = 4;
    step(4);
    Particles_Particle a = particle(0);
    near1(a.age, .25f);
    near1(a.velocity.x, 2); near1(a.velocity.y, -1);
    near1(a.position.x, 1.5f); near1(a.position.y, 1.75f); near1(a.position.z, 3);
    near1(a.rotation, .5f);
    p->dt = .75f;
    step(4);
    TIDE_CHECK(count() == 0);

    p->input.position = (tide_float3){0};
    p->input.velocity = (tide_float3){100, 100, 100};
    p->input.lifetime = 2;
    p->input.custom = (tide_float4){6, 0, 0, 0};
    step(1);
    p->dt = .5f;
    step(5);
    a = particle(0);
    near1(a.age, .5f); near1(a.position.x, 3); near1(a.position.y, 0);
    near1(a.custom.y, 1);
    // The custom callback owns motion; the default velocity is not added too.
    near1(a.velocity.x, 100);
    step(8);
    TIDE_CHECK(count() == 0); // A callback can end a particle immediately.
}

TIDE_TEST(particles_sampling_keeps_fading_hues_and_clamps_sizes)
{
    ParticleProbe *p = start();
    p->input.age = .25f;
    p->input.lifetime = 1;
    p->input.size = (tide_float2){4, 2};
    p->input.endSize = (tide_float2){0, 6};
    p->input.color = (tide_color){1, .25f, 0, 1};
    p->input.endColor = (tide_color){1, 1, 1, 0};
    step(7);
    near1(p->sampledAge, .25f);
    near1(p->sampledSize.x, 3); near1(p->sampledSize.y, 3);
    near1(p->sampledTint.r, 1); near1(p->sampledTint.g, .25f);
    near1(p->sampledTint.b, 0); near1(p->sampledTint.a, .75f);
    p->input.age = .5f;
    p->input.color = (tide_color){1, 0, 0, 1};
    p->input.endColor = (tide_color){0, 0, 1, .5f};
    step(7);
    near1(p->sampledTint.r, 2.0f / 3); near1(p->sampledTint.b, 1.0f / 3);
    near1(p->sampledTint.a, .75f);
    p->input.size = (tide_float2){-4, -2};
    p->input.endSize = (tide_float2){-8, -6};
    step(7);
    near1(p->sampledSize.x, 0); near1(p->sampledSize.y, 0);
}

TIDE_TEST(particles_capacity_keeps_oldest_and_compaction_is_stable)
{
    ParticleProbe *p = start();
    p->emitter.capacity = 4;
    for (int i = 0; i < 4; i++) {
        p->input.position.x = (float)i;
        p->input.lifetime = i == 1 ? .125f : 2;
        step(1);
        TIDE_CHECK(p->result == 1);
    }
    step(1);
    TIDE_CHECK(p->result == 0 && count() == 4);
    p->dt = .25f;
    step(4);
    TIDE_CHECK(count() == 3);
    near1(particle(0).position.x, 0);
    near1(particle(1).position.x, 2);
    near1(particle(2).position.x, 3);
    p->emitter.capacity = 2;
    step(4);
    TIDE_CHECK(count() == 2);
    near1(particle(0).position.x, 0);
    near1(particle(1).position.x, 2);
    p->emitter.capacity = -1;
    step(4);
    TIDE_CHECK(count() == 0);
    p->emitter.capacity = INT_MAX;
    step(1);
    TIDE_CHECK(p->result == 1 && count() == 1);
}

TIDE_TEST(particles_fractional_rates_drop_overflow_without_future_debt)
{
    ParticleProbe *p = start();
    p->rate = 2.5f;
    p->dt = .125f;
    for (int i = 0; i < 8; i++) step(3);
    TIDE_CHECK(count() == 2);
    TIDE_CHECK(p->emitter.remainder == .5);
    for (int i = 0; i < 8; i++) step(3);
    TIDE_CHECK(count() == 5 && p->emitter.remainder == 0);
    const int64_t random_after_five = p->emitter.random.state;
    step(6);
    TIDE_CHECK(count() == 0 && p->emitter.remainder == 0);
    TIDE_CHECK(p->emitter.random.state == random_after_five);
    p->emitter.random = tide_random_seeded(23);
    p->dt = 2;
    step(3);
    TIDE_CHECK(count() == 5 && p->emitter.random.state == random_after_five);

    p->emitter.capacity = 2;
    p->rate = 1000;
    p->dt = 1;
    step(3);
    TIDE_CHECK(count() == 2);
    TIDE_CHECK(p->emitter.random.state == random_after_five);
    TIDE_CHECK(p->emitter.remainder == 0);
    p->dt = 11;
    step(4);
    TIDE_CHECK(count() == 0);
    p->rate = 1;
    p->dt = .25f;
    step(3);
    TIDE_CHECK(count() == 0 && p->emitter.remainder == .25);
    p->rate = FLT_MAX;
    p->dt = FLT_MAX;
    step(3);
    TIDE_CHECK(count() == 2 && isfinite(p->emitter.remainder));
}

TIDE_TEST(particles_seeded_callbacks_capture_values_and_replay)
{
    ParticleProbe *p = start();
    p->origin = (tide_float3){3, 4, 5};
    p->count = 17;
    step(2);
    TIDE_REQUIRE(p->result == 17 && count() == 17);
    Particles_Particle saved[17];
    for (int i = 0; i < 17; i++) {
        saved[i] = particle(i);
        near1(saved[i].position.x, 3); near1(saved[i].position.y, 4); near1(saved[i].position.z, 5);
        TIDE_CHECK(saved[i].velocity.x >= -2 && saved[i].velocity.x < 2);
        TIDE_CHECK(saved[i].velocity.y >= 1 && saved[i].velocity.y < 3);
        TIDE_CHECK(saved[i].velocity.z >= -1 && saved[i].velocity.z < 1);
        near1(saved[i].custom.w, 7);
    }
    step(6);
    p->emitter.random = tide_random_seeded(23);
    step(2);
    for (int i = 0; i < 17; i++) {
        const Particles_Particle copy = particle(i);
        TIDE_CHECK(memcmp(&copy, &saved[i], sizeof copy) == 0);
    }
    p->emitter.capacity = 17;
    const int64_t full_state = p->emitter.random.state;
    p->count = INT_MAX;
    step(2);
    TIDE_CHECK(p->result == 0 && p->emitter.random.state == full_state);
    step(6);
    p->emitter.capacity = 3;
    p->count = INT_MAX;
    step(9);
    TIDE_CHECK(p->result == 0 && count() == 0);
    tide_random expected = {full_state};
    for (int i = 0; i < 3; i++) (void)tide_random_next_float(&expected);
    TIDE_CHECK(p->emitter.random.state == expected.state); // Bounded attempts, even when each is rejected.
}

TIDE_TEST(particles_invalid_values_are_bounded_and_bad_time_is_a_noop)
{
    ParticleProbe *p = start();
    const Particles_Particle good = p->input;
    const float invalid[] = {NAN, INFINITY, -INFINITY};
    for (int i = 0; i < 3; i++) {
        p->input = good; p->input.position.x = invalid[i]; step(1); TIDE_CHECK(p->result == 0);
        p->input = good; p->input.velocity.y = invalid[i]; step(1); TIDE_CHECK(p->result == 0);
        p->input = good; p->input.custom.w = invalid[i]; step(1); TIDE_CHECK(p->result == 0);
        p->input = good; p->input.lifetime = invalid[i]; step(1); TIDE_CHECK(p->result == 0);
    }
    p->input = good; p->input.age = -1; step(1); TIDE_CHECK(p->result == 0);
    p->input = good; p->input.lifetime = 0; step(1); TIDE_CHECK(p->result == 0);
    p->input = good; step(1); TIDE_CHECK(p->result == 1 && count() == 1);
    const Particles_Particle original = particle(0);
    const int64_t random = p->emitter.random.state;
    const float times[] = {0, -1, NAN, INFINITY, -INFINITY};
    p->rate = 10;
    for (int i = 0; i < 5; i++) {
        p->dt = times[i];
        step(4);
        const Particles_Particle after = particle(0);
        TIDE_CHECK(memcmp(&original, &after, sizeof after) == 0);
        step(3);
        TIDE_CHECK(p->result == 0 && count() == 1 && p->emitter.random.state == random);
    }
    p->dt = .25f;
    for (int i = 0; i < 3; i++) {
        p->rate = invalid[i]; step(3); TIDE_CHECK(p->result == 0 && count() == 1);
    }
    p->count = -1; step(2); TIDE_CHECK(p->result == 0 && count() == 1);
    p->input = good; p->input.position.x = FLT_MAX; p->input.velocity.x = FLT_MAX;
    step(1); TIDE_CHECK(p->result == 1 && count() == 2);
    p->dt = .5f; step(4);
    TIDE_CHECK(count() == 1); // An overflowing move cannot leave nonfinite live state.
}
