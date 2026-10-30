#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "microduck.h"
#include "tide_test.h"
#include "../../../examples/robotics/microduck/policy/inference.h"
#ifdef NEWTON_TEST_JOBS
#include "tide/platform.h"
#endif

#define POLICY_DT (1.0f / 200.0f)
#define POLICY_JOINTS 14
#define POLICY_OBSERVATIONS 61

/* Independent constants from Pollen's model.rs DEFAULT_POSITION and the
 * original MJCF STAND key, not from DuckPolicyHome or generated model helpers.
 * Policy14 and this model's DFS14 have identical order; only physical15 has
 * a mouth at index 9 which is omitted from policy14. */
static const float policy_home[POLICY_JOINTS] = {
    0, -.0873f, -.4579f, -.0049f, .4530f, .3491f, .3491f,
    0, 0, 0, .0873f, .4579f, .0049f, -.4530f
};
static const float model_home[POLICY_JOINTS] = {
    0, -.0872664601f, -.457924008f, -.0049399999f, .452984005f,
    .34906584f, .34906584f, 0, 0, 0, .0872664601f,
    .457924008f, .0049399999f, -.452984005f
};
static const float raw_history[POLICY_JOINTS] = {
    -2.25f, -1.875f, -1.5f, -1.125f, -.75f, -.375f, 0,
    .375f, .75f, 1.125f, 1.5f, 1.875f, 2.25f, 2.625f
};

static tide_entity policy_duck(const tide_world *world, int seat)
{
    return seat == 0 ? world->DuckFixture.first : world->DuckFixture.second;
}

static tide_entity policy_entity(const tide_world *world, tide_list values, int index)
{
    const tide_entity *value = tide_list_read(&world->heap, values, index, sizeof(*value));
    return value ? *value : (tide_entity){0};
}

static float policy_number(const tide_world *world, tide_list values, int index)
{
    const float *value = tide_list_read(&world->heap, values, index, sizeof(*value));
    return value ? *value : NAN;
}

static void policy_read(const tide_world *world, tide_list values, float *out, int count)
{
    for (int i = 0; i < count; ++i) out[i] = policy_number(world, values, i);
}

static void policy_close(float actual, float expected, float tolerance)
{
    if (!(isfinite(actual) && fabsf(actual - expected) <= tolerance)) {
        printf("    policy value: actual %.9g, expected %.9g, tolerance %.3g\n",
            (double)actual, (double)expected, (double)tolerance);
        TIDE_CHECK(isfinite(actual) && fabsf(actual - expected) <= tolerance);
    }
}

static void policy_vector_zero(tide_float3 value)
{
    TIDE_CHECK(value.x == 0 && value.y == 0 && value.z == 0);
}

static void policy_check_no_assistance(const tide_world *world, int seat)
{
    TIDE_REQUIRE(world->Newton_Physics.status == Newton_Status_Ok);
    const Duck duck = tide_get_Duck(world, policy_duck(world, seat));
    TIDE_CHECK(!duck.assisted);
    policy_vector_zero(duck.assistanceForce);
    policy_vector_zero(duck.assistanceTorque);
    for (int i = 0; i < 15; ++i) {
        const tide_entity entity = policy_entity(world, duck.bodies, i);
        const Newton_Body body = tide_get_Newton_Body(world, entity);
        /* Newton stores gravity/contact separately. No grabs or game assistance
         * are requested by these tests, so no external body wrench is allowed. */
        policy_vector_zero(body.force);
        policy_vector_zero(body.torque);
        bool collected = false;
        const int bodies = tide_list_read_count(&world->heap, world->Newton_Physics.bodies);
        for (int j = 0; j < bodies; ++j) {
            const Newton_BodyState *state = tide_list_read(&world->heap, world->Newton_Physics.bodies, j, sizeof(*state));
            if (!state || !tide_entity_equal(state->entity, entity)) continue;
            /* PublishBodies clears component wrenches after the step. Inspect
             * the actual solver input as well, which retains their values. */
            policy_vector_zero(state->force);
            policy_vector_zero(state->torque);
            collected = true;
        }
        TIDE_CHECK(collected);
    }
    const int count = tide_list_read_count(&world->heap, duck.transitions);
    TIDE_REQUIRE(count > 0);
    const int latest = (duck.next + 63) % 64;
    const DuckTransition *record = tide_list_read(&world->heap, duck.transitions, latest, sizeof(*record));
    TIDE_REQUIRE(record && record->tick == world->Time.tick - 1);
    policy_vector_zero(record->assistanceForce);
    policy_vector_zero(record->assistanceTorque);
}

/* Host setters copy component lanes, not their lists' contents. Own modified
 * list values in this world's heap before handing them to the public setter. */
static bool policy_seed(tide_world *world, tide_entity entity, const float *action, const float *filtered)
{
    const uint64_t mark = tide_scratch_mark();
    tide_text_use(&world->heap, NULL);
    DuckPolicyState state = tide_get_DuckPolicyState(world, entity);
    tide_list_set(&state.action, action ? tide_list_from(action, POLICY_JOINTS, sizeof(float)) : (tide_list){0},
        sizeof(float), TIDE_IN_MATCH);
    tide_list_set(&state.filtered, filtered ? tide_list_from(filtered, POLICY_JOINTS, sizeof(float)) : (tide_list){0},
        sizeof(float), TIDE_IN_MATCH);
    state.cadence = 0;
    const bool ok = tide_set_DuckPolicyState(world, entity, state);
    tide_scratch_reset(mark);
    tide_heap_flush(&world->heap);
    return ok;
}

static void policy_step(tide_world *world, DuckControls first, DuckControls second, const tide_jobs *jobs)
{
    tide_world_set_input(world, tide_player_from_index(0), first);
    tide_world_set_input(world, tide_player_from_index(1), second);
    if (jobs) tide_world_tick_on(world, jobs);
    else tide_world_tick(world);
}

static bool policy_transport_input(DuckControls input, DuckControls *previous, DuckControls *received)
{
    const uint32_t capacity = tide_game_api.max_input_bytes;
    if (!capacity) return false;
    uint8_t bytes[capacity];
    for (int delta = 0; delta < 2; ++delta) {
        const uint32_t size = delta
            ? tide_game_api.write_input_delta(&input, previous, bytes, capacity)
            : tide_game_api.write_input(&input, bytes, capacity);
        if (!size || size > capacity) return false;
        const bool decoded = delta
            ? tide_game_api.read_input_delta(bytes, size, previous, received)
            : tide_game_api.read_input(bytes, size, received);
        if (!decoded || received->walk != input.walk || received->turn != input.turn ||
            received->drive != input.drive || received->run != input.run ||
            received->assistance != input.assistance || received->manual != input.manual ||
            received->reset != input.reset) return false;
    }
    *previous = *received;
    return true;
}

static float policy_safety(float value)
{
    const float pi = 3.14159265358979323846f;
    return value < -pi ? -pi : value > pi ? pi : value;
}

static bool policy_evaluate_saved(const tide_world *world, int seat, float action[POLICY_JOINTS])
{
    const DuckPolicyState state = tide_get_DuckPolicyState(world, policy_duck(world, seat));
    if (state.status != 1 || tide_list_read_count(&world->heap, state.observation) != POLICY_OBSERVATIONS ||
        tide_list_read_count(&world->heap, state.action) != POLICY_JOINTS ||
        tide_list_read_count(&world->heap, state.filtered) != POLICY_JOINTS ||
        tide_list_read_count(&world->heap, state.motor) != POLICY_JOINTS) return false;
    float observation[POLICY_OBSERVATIONS];
    policy_read(world, state.observation, observation, POLICY_OBSERVATIONS);
    if (!md_policy_evaluate(observation, action)) return false;
    for (int i = 0; i < POLICY_JOINTS; ++i) {
        /* Same immutable actor, called independently of Tide's list bridge. */
        if (policy_number(world, state.action, i) != action[i]) return false;
    }
    return true;
}

TIDE_TEST(microduck_policy_exact_61_slots_and_original_trunk_frames)
{
    /* XYZW Qbody = Rx(-90deg) * source-attitude * P_root, independently
     * calculated from the pinned XML principal rotation. No game math helper
     * constructs expected rotations or vectors. The last case is all-axis. */
    static const struct {
        float rotation[4];
        float gyro[3];
        float gravity[3];
    } frames[] = {
        {{-.9030070606f, -.4296142698f, -.0002393733f, .0031292770f}, {1, -3, 2}, {0, 0, -1}},
        {{-.636309683f, -.303613901f, -.303952426f, .640735149f}, {1, 2, 3}, {0, -1, 0}},
        {{-.9423055795f, .3347392525f, -.0023819955f, .0020434705f}, {-2, -3, 1}, {1, 0, 0}},
        {{-.6386916785f, -.3015704305f, .6383531535f, .3059958965f}, {-3, -1, 2}, {0, 0, -1}},
        {{-.7619617860f, -.4400115816f, .1101798260f, .4622385452f},
            {.0882990368f, -1.0258415431f, 3.5972006350f},
            {-.3420201433f, -.6040227736f, -.7198463104f}}
    };
    for (unsigned f = 0; f < sizeof(frames) / sizeof(frames[0]); ++f) {
        for (int sign = -1; sign <= 1; sign += 2) {
            static tide_world world;
            tide_world_init(&world, POLICY_DT);
            const tide_entity entity = policy_duck(&world, 0);
            const Duck duck = tide_get_Duck(&world, entity);
            const tide_entity root_entity = policy_entity(&world, duck.bodies, 0);
            Newton_Body root = tide_get_Newton_Body(&world, root_entity);
            root.rotation = tide_q(
                sign * frames[f].rotation[0], sign * frames[f].rotation[1],
                sign * frames[f].rotation[2], sign * frames[f].rotation[3]);
            root.angularVelocity = tide_f3(1, 2, 3);
            TIDE_REQUIRE(tide_set_Newton_Body(&world, root_entity, root));
            for (int i = 0; i < POLICY_JOINTS; ++i) {
                const tide_entity joint_entity = policy_entity(&world, duck.joints, i);
                Newton_Joint joint = tide_get_Newton_Joint(&world, joint_entity);
                joint.position = policy_home[i] + (float)(i + 1) * .01f;
                joint.velocity = (float)(i - 7) * 1.125f;
                TIDE_REQUIRE(tide_set_Newton_Joint(&world, joint_entity, joint));
            }
            /* Preserve the independent oracle's prescribed .2 m/s command;
             * default WASD speed is checked separately below. */
            DuckPolicyState prescribed = tide_get_DuckPolicyState(&world, entity);
            prescribed.forwardSpeed = .25f;
            TIDE_REQUIRE(tide_set_DuckPolicyState(&world, entity, prescribed));
            TIDE_REQUIRE(policy_seed(&world, entity, raw_history, NULL));
            policy_step(&world, (DuckControls){.walk = .8f, .turn = .6f}, (DuckControls){0}, NULL);
            const DuckPolicyState state = tide_get_DuckPolicyState(&world, entity);
            TIDE_REQUIRE(state.status == 1 && state.inferences == 1);
            TIDE_REQUIRE(tide_list_read_count(&world.heap, state.observation) == POLICY_OBSERVATIONS);
            float expected[POLICY_OBSERVATIONS] = {0};
            memcpy(expected, frames[f].gyro, sizeof(frames[f].gyro));
            memcpy(expected + 3, frames[f].gravity, sizeof(frames[f].gravity));
            for (int i = 0; i < POLICY_JOINTS; ++i) {
                expected[6 + i] = (float)(i + 1) * .01f;
                expected[20 + i] = (float)(i - 7) * 1.125f;
                expected[34 + i] = raw_history[i];
            }
            expected[48] = .2f;
            expected[50] = -.6f;
            for (int i = 0; i < POLICY_OBSERVATIONS; ++i)
                policy_close(policy_number(&world, state.observation, i), expected[i], 2e-6f);
            tide_world_free(&world);
        }
    }
}

TIDE_TEST(microduck_policy_home_keeps_the_precise_model_residual)
{
    static tide_world world;
    tide_world_init(&world, POLICY_DT);
    const tide_entity entity = policy_duck(&world, 0);
    const Duck duck = tide_get_Duck(&world, entity);
    for (int i = 0; i < POLICY_JOINTS; ++i) {
        const Newton_Joint joint = tide_get_Newton_Joint(&world, policy_entity(&world, duck.joints, i));
        policy_close(joint.position, model_home[i], 1e-8f);
    }
    policy_step(&world, (DuckControls){0}, (DuckControls){0}, NULL);
    const DuckPolicyState state = tide_get_DuckPolicyState(&world, entity);
    TIDE_REQUIRE(state.status == 1);
    for (int i = 0; i < POLICY_JOINTS; ++i) {
        policy_close(policy_number(&world, state.observation, 6 + i), model_home[i] - policy_home[i], 1e-8f);
        TIDE_CHECK(policy_number(&world, state.observation, 20 + i) == 0);
        TIDE_CHECK(policy_number(&world, state.observation, 34 + i) == 0);
    }
    TIDE_CHECK(fabsf(policy_number(&world, state.observation, 9)) > 3e-5f);
    for (int i = 48; i < POLICY_OBSERVATIONS; ++i) TIDE_CHECK(policy_number(&world, state.observation, i) == 0);
    tide_world_free(&world);
}

TIDE_TEST(microduck_policy_real_actor_first_targets_are_unfiltered)
{
    static tide_world world;
    tide_world_init(&world, POLICY_DT);
    policy_step(&world, (DuckControls){.walk = 1}, (DuckControls){.turn = -1}, NULL);
    for (int seat = 0; seat < 2; ++seat) {
        float action[POLICY_JOINTS];
        TIDE_REQUIRE(policy_evaluate_saved(&world, seat, action));
        const tide_entity entity = policy_duck(&world, seat);
        const DuckPolicyState state = tide_get_DuckPolicyState(&world, entity);
        const Duck duck = tide_get_Duck(&world, entity);
        bool nonzero = false;
        for (int i = 0; i < POLICY_JOINTS; ++i) {
            const float expected = policy_home[i] + .9f * action[i];
            policy_close(policy_number(&world, state.filtered, i), expected, 1e-6f);
            policy_close(policy_number(&world, duck.targets, i), policy_safety(expected), 1e-6f);
            nonzero = nonzero || fabsf(action[i]) > .001f;
            TIDE_CHECK(policy_number(&world, state.observation, 34 + i) == 0);
        }
        TIDE_CHECK(nonzero && state.inferences == 1 && state.cadence == 1);
        TIDE_CHECK(duck.phase == 0 && duck.settleTicks == 0);
        policy_check_no_assistance(&world, seat);
    }
    tide_world_free(&world);
}

TIDE_TEST(microduck_policy_filter_history_is_raw_and_pre_safety)
{
    static tide_world world;
    tide_world_init(&world, POLICY_DT);
    const tide_entity entity = policy_duck(&world, 0);
    float previous[POLICY_JOINTS];
    for (int i = 0; i < POLICY_JOINTS; ++i) previous[i] = i % 2 ? -100 : 100;
    TIDE_REQUIRE(policy_seed(&world, entity, raw_history, previous));
    float expected_history[POLICY_JOINTS];
    memcpy(expected_history, raw_history, sizeof(expected_history));
    for (int inference = 0; inference < 2; ++inference) {
        const DuckControls input = {.walk = -.75f, .turn = .4f, .run = inference == 0};
        for (int tick = 0; tick < (inference == 0 ? 1 : 4); ++tick)
            policy_step(&world, input, (DuckControls){0}, NULL);
        const DuckPolicyState state = tide_get_DuckPolicyState(&world, entity);
        const Duck duck = tide_get_Duck(&world, entity);
        float action[POLICY_JOINTS];
        TIDE_REQUIRE(policy_evaluate_saved(&world, 0, action));
        TIDE_CHECK(policy_number(&world, state.observation, 48) == (input.run ? .5f : .4f) * input.walk);
        TIDE_CHECK(policy_number(&world, state.observation, 50) == (input.run ? -1.25f : -1.f) * input.turn);
        for (int i = 0; i < POLICY_JOINTS; ++i) {
            const float alpha = i >= 5 && i <= 8 ? .5f : .7f;
            const float expected = alpha * (policy_home[i] + .9f * action[i]) + (1 - alpha) * previous[i];
            TIDE_CHECK(policy_number(&world, state.observation, 34 + i) == expected_history[i]);
            policy_close(policy_number(&world, state.filtered, i), expected, 4e-6f);
            policy_close(policy_number(&world, duck.targets, i), policy_safety(expected), 1e-6f);
            /* Synthetic remembered targets force clipping on every hinge and
             * distinguish remembering pre-clamp from final actuator targets. */
            TIDE_CHECK(fabsf(expected) > 3.1415927f);
            previous[i] = expected;
            expected_history[i] = action[i];
        }
    }
    tide_world_free(&world);
}

TIDE_TEST(microduck_policy_infers_at_50_hz_and_holds_between_ticks)
{
    static tide_world world;
    tide_world_init(&world, POLICY_DT);
    DuckPolicyState config = tide_get_DuckPolicyState(&world, policy_duck(&world, 0));
    config.runSpeed = .55f;
    TIDE_REQUIRE(tide_set_DuckPolicyState(&world, policy_duck(&world, 0), config));
    float saved_action[POLICY_JOINTS] = {0}, saved_filtered[POLICY_JOINTS] = {0};
    float saved_targets[POLICY_JOINTS] = {0}, saved_observation[POLICY_OBSERVATIONS] = {0};
    for (int tick = 1; tick <= 17; ++tick) {
        const DuckControls input = {.walk = (tick / 4) % 2 ? -1 : .75f,
            .turn = (float)(tick % 5 - 2) * .375f, .run = tick % 3 != 0};
        policy_step(&world, input, (DuckControls){0}, NULL);
        const tide_entity entity = policy_duck(&world, 0);
        const DuckPolicyState state = tide_get_DuckPolicyState(&world, entity);
        const Duck duck = tide_get_Duck(&world, entity);
        TIDE_REQUIRE(state.status == 1 && world.Newton_Physics.status == Newton_Status_Ok);
        TIDE_CHECK(state.cadence == tick % 4 && state.inferences == (tick + 3) / 4);
        /* Current commands follow held Shift each tick; raw actor history,
         * filtered targets and observations still update only at 50 Hz. */
        const float command = (input.run ? .55f : .4f) * input.walk;
        const float yaw = (input.run ? -1.25f : -1.f) * input.turn;
        TIDE_CHECK(state.command.x == command && state.command.y == 0 && state.command.z == yaw);
        if ((tick - 1) % 4 == 0) {
            float action[POLICY_JOINTS];
            TIDE_REQUIRE(policy_evaluate_saved(&world, 0, action));
            for (int i = 0; i < POLICY_JOINTS; ++i)
                TIDE_CHECK(policy_number(&world, state.observation, 34 + i) == saved_action[i]);
            TIDE_CHECK(policy_number(&world, state.observation, 48) == command);
            TIDE_CHECK(policy_number(&world, state.observation, 50) == yaw);
            policy_read(&world, state.action, saved_action, POLICY_JOINTS);
            policy_read(&world, state.filtered, saved_filtered, POLICY_JOINTS);
            policy_read(&world, duck.targets, saved_targets, POLICY_JOINTS);
            policy_read(&world, state.observation, saved_observation, POLICY_OBSERVATIONS);
        } else {
            for (int i = 0; i < POLICY_JOINTS; ++i) {
                TIDE_CHECK(policy_number(&world, state.action, i) == saved_action[i]);
                TIDE_CHECK(policy_number(&world, state.filtered, i) == saved_filtered[i]);
                TIDE_CHECK(policy_number(&world, duck.targets, i) == saved_targets[i]);
            }
            for (int i = 0; i < POLICY_OBSERVATIONS; ++i)
                TIDE_CHECK(policy_number(&world, state.observation, i) == saved_observation[i]);
        }
    }
    tide_world_free(&world);
}

TIDE_TEST(microduck_policy_high_back_emf_preserves_raw_motor_and_pwm_order)
{
    static tide_world world;
    tide_world_init(&world, POLICY_DT);
    const tide_entity entity = policy_duck(&world, 0);
    Duck duck = tide_get_Duck(&world, entity);
    DuckPolicyState state = tide_get_DuckPolicyState(&world, entity);
    float q[POLICY_JOINTS], velocity[POLICY_JOINTS] = {0}, target[POLICY_JOINTS];
    velocity[0] = 100; velocity[9] = -100;
    velocity[1] = 25; velocity[10] = -25;
    for (int i = 0; i < POLICY_JOINTS; ++i) {
        const tide_entity joint_entity = policy_entity(&world, duck.joints, i);
        Newton_Joint joint = tide_get_Newton_Joint(&world, joint_entity);
        q[i] = joint.position;
        joint.velocity = velocity[i];
        TIDE_REQUIRE(tide_set_Newton_Joint(&world, joint_entity, joint));
        target[i] = q[i] + (velocity[i] > 0 ? -.2f : velocity[i] < 0 ? .2f : 0);
    }
    /* Bypass this tick's NN update so the independent target/velocity fixture
     * reaches the public Drive -> Motors path exactly as prescribed. */
    const uint64_t mark = tide_scratch_mark();
    tide_text_use(&world.heap, NULL);
    tide_list_set(&duck.targets, tide_list_from(target, POLICY_JOINTS, sizeof(float)), sizeof(float), TIDE_IN_MATCH);
    TIDE_REQUIRE(tide_set_Duck(&world, entity, duck));
    tide_scratch_reset(mark);
    state.cadence = 1; state.status = 1;
    state.voltage = 7.4f; state.currentLimit = 1.75f; state.previousMotorLoad = 4;
    TIDE_REQUIRE(tide_set_DuckPolicyState(&world, entity, state));
    policy_step(&world, (DuckControls){0}, (DuckControls){0}, NULL);
    state = tide_get_DuckPolicyState(&world, entity);
    TIDE_REQUIRE(state.status == 1 && state.cadence == 2 && state.inferences == 0);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, state.motor) == POLICY_JOINTS);
    /* Source-independent M6 controller constants, float32 operations in order.
     * V=7.4-.1*4=7.0. At +/-100 rad/s, the current interval is outside the
     * physical PWM range: the last clamp must win, and raw torque exceeds
     * the final actuator clamp. At +/-25 the current clamp alone is active. */
    const float voltage = 7.4f - .1f * 4;
    const float resistance = 2.8113923539223227f;
    const float kt = .36601349688984386f;
    const float gain = .5754755004452713f;
    const float torque_per_volt = .13018940468383897f;
    const float effort_limit = .9634015946604082f;
    float raw_sum = 0, applied_sum = 0;
    for (int i = 0; i < POLICY_JOINTS; ++i) {
        float duty = gain * (target[i] - q[i]);
        const float center = kt * velocity[i] / voltage;
        const float span = resistance * 1.75f / voltage;
        duty = fminf(center + span, fmaxf(center - span, duty));
        duty = fminf(1, fmaxf(-1, duty));
        const float raw = torque_per_volt * (voltage * duty - kt * velocity[i]);
        const float applied = fminf(effort_limit, fmaxf(-effort_limit, raw));
        const Newton_Actuator actuator = tide_get_Newton_Actuator(&world, policy_entity(&world, duck.joints, i));
        policy_close(policy_number(&world, state.motor, i), raw, 1e-6f);
        policy_close(actuator.effort, applied, 1e-6f);
        raw_sum += fabsf(raw);
        applied_sum += fabsf(applied);
        if (i == 0 || i == 9) {
            TIDE_CHECK(fabsf(raw) > effort_limit + 1);
            TIDE_CHECK(fabsf(raw) > kt * 1.75f + 1);
            TIDE_CHECK(fabsf(actuator.effort) == effort_limit);
        }
    }
    policy_close(state.previousMotorLoad, raw_sum, 2e-6f);
    TIDE_CHECK(state.previousMotorLoad > applied_sum + 1);
    tide_world_free(&world);
}

TIDE_TEST(microduck_policy_friction_uses_applied_history_and_reset_clears_it)
{
    static tide_world world;
    tide_world_init(&world, POLICY_DT);
    const tide_entity entity = policy_duck(&world, 0);
    const Duck duck = tide_get_Duck(&world, entity);
    DuckPolicyState state = tide_get_DuckPolicyState(&world, entity);
    float raw[POLICY_JOINTS] = {3};
    const uint64_t mark = tide_scratch_mark();
    tide_text_use(&world.heap, NULL);
    tide_list_set(&state.motor, tide_list_from(raw, POLICY_JOINTS, sizeof(float)), sizeof(float), TIDE_IN_MATCH);
    state.cadence = 1; state.status = 1; state.previousMotorLoad = 3;
    TIDE_REQUIRE(tide_set_DuckPolicyState(&world, entity, state));
    tide_scratch_reset(mark);
    const tide_entity joint_entity = policy_entity(&world, duck.joints, 0);
    Newton_Joint joint = tide_get_Newton_Joint(&world, joint_entity);
    Newton_Actuator actuator = tide_get_Newton_Actuator(&world, joint_entity);
    joint.load = -.07f;
    actuator.appliedEffort = .12f;
    TIDE_REQUIRE(tide_set_Newton_Joint(&world, joint_entity, joint));
    TIDE_REQUIRE(tide_set_Newton_Actuator(&world, joint_entity, actuator));
    policy_step(&world, (DuckControls){0}, (DuckControls){0}, NULL);
    joint = tide_get_Newton_Joint(&world, joint_entity);
    /* qdot=0, so Stribeck=1. Motor-side load is the previous applied .12 Nm,
     * not the previous unclipped 3 Nm retained for battery sag. */
    const float motor = .12f, external = -.07f;
    const float expected = .004771183165566f
        + fabsf(external * .000008515871897059342f - motor * .2667860954283698f)
        + .004676345799486616f
        + fabsf(external * .08077928978935671f - motor * .000010722918395099123f)
        + .004902565732332559f * external * external;
    policy_close(joint.friction, expected, 1e-7f);
    policy_step(&world, (DuckControls){.reset = true}, (DuckControls){0}, NULL);
    joint = tide_get_Newton_Joint(&world, joint_entity);
    policy_close(joint.friction, .004771183165566f + .004676345799486616f, 1e-7f);
    tide_world_free(&world);
}

TIDE_TEST(microduck_policy_failed_inference_keeps_history_and_zeroes_effort)
{
    static tide_world world;
    tide_world_init(&world, POLICY_DT);
    policy_step(&world, (DuckControls){.walk = 1}, (DuckControls){0}, NULL);
    const tide_entity entity = policy_duck(&world, 0);
    const Duck duck = tide_get_Duck(&world, entity);
    DuckPolicyState before = tide_get_DuckPolicyState(&world, entity);
    float action[POLICY_JOINTS], filtered[POLICY_JOINTS], motor[POLICY_JOINTS];
    TIDE_REQUIRE(tide_list_read_count(&world.heap, before.motor) == POLICY_JOINTS);
    policy_read(&world, before.action, action, POLICY_JOINTS);
    policy_read(&world, before.filtered, filtered, POLICY_JOINTS);
    policy_read(&world, before.motor, motor, POLICY_JOINTS);
    before.cadence = 0;
    TIDE_REQUIRE(tide_set_DuckPolicyState(&world, entity, before));
    const tide_entity joint_entity = policy_entity(&world, duck.joints, 3);
    Newton_Joint joint = tide_get_Newton_Joint(&world, joint_entity);
    joint.position = NAN;
    TIDE_REQUIRE(tide_set_Newton_Joint(&world, joint_entity, joint));
    policy_step(&world, (DuckControls){0}, (DuckControls){0}, NULL);
    const DuckPolicyState after = tide_get_DuckPolicyState(&world, entity);
    TIDE_CHECK(after.status == -2 && after.cadence == 0 && after.inferences == before.inferences);
    TIDE_CHECK(after.previousMotorLoad == before.previousMotorLoad);
    for (int i = 0; i < POLICY_JOINTS; ++i) {
        TIDE_CHECK(policy_number(&world, after.action, i) == action[i]);
        TIDE_CHECK(policy_number(&world, after.filtered, i) == filtered[i]);
        TIDE_CHECK(policy_number(&world, after.motor, i) == motor[i]);
        const Newton_Actuator actuator = tide_get_Newton_Actuator(&world, policy_entity(&world, duck.joints, i));
        TIDE_CHECK(actuator.effort == 0);
    }
    policy_step(&world, (DuckControls){.reset = true}, (DuckControls){0}, NULL);
    const DuckPolicyState reset = tide_get_DuckPolicyState(&world, entity);
    TIDE_REQUIRE(reset.status == 1 && reset.cadence == 1 && reset.inferences == 1);
    TIDE_CHECK(world.Newton_Physics.status == Newton_Status_Ok);
    tide_world_free(&world);
}

TIDE_TEST(microduck_policy_reset_clears_feedback_at_every_cadence)
{
    for (int residue = 0; residue < 4; ++residue) {
        static tide_world world, fresh;
        tide_world_init(&world, POLICY_DT);
        tide_world_init(&fresh, POLICY_DT);
        for (int i = 0; i < 12 + residue; ++i)
            policy_step(&world, (DuckControls){.walk = 1, .turn = .5f}, (DuckControls){0}, NULL);
        TIDE_CHECK(tide_get_DuckPolicyState(&world, policy_duck(&world, 0)).cadence == residue);
        const Duck dirty = tide_get_Duck(&world, policy_duck(&world, 0));
        for (int i = 0; i < POLICY_JOINTS; ++i) {
            const tide_entity entity = policy_entity(&world, dirty.joints, i);
            Newton_Joint joint = tide_get_Newton_Joint(&world, entity);
            /* The prior solver load feeds the very next motor friction
             * budget. Seed both signs explicitly: a quiet preceding episode
             * must not let a missing load reset pass by coincidence. */
            joint.load = (i % 2 ? -.25f : .25f) * (float)(i + 1);
            TIDE_REQUIRE(tide_set_Newton_Joint(&world, entity, joint));
        }
        DuckPolicyState dirty_policy = tide_get_DuckPolicyState(&world, policy_duck(&world, 0));
        dirty_policy.previousMotorLoad = 4 + (float)residue;
        TIDE_REQUIRE(tide_set_DuckPolicyState(&world, policy_duck(&world, 0), dirty_policy));
        const DuckControls reset = {.reset = true, .walk = -.8f, .turn = -.6f, .assistance = true};
        /* Reset the companion too, and ResetDuckBalls restores shared props,
         * so the compared duck sees identical physical surroundings. */
        policy_step(&world, reset, (DuckControls){.reset = true}, NULL);
        policy_step(&fresh, reset, (DuckControls){.reset = true}, NULL);
        TIDE_REQUIRE(world.Newton_Physics.status == Newton_Status_Ok && fresh.Newton_Physics.status == Newton_Status_Ok);
        const DuckPolicyState actual = tide_get_DuckPolicyState(&world, policy_duck(&world, 0));
        const DuckPolicyState expected = tide_get_DuckPolicyState(&fresh, policy_duck(&fresh, 0));
        const Duck actual_duck = tide_get_Duck(&world, policy_duck(&world, 0));
        const Duck expected_duck = tide_get_Duck(&fresh, policy_duck(&fresh, 0));
        TIDE_REQUIRE(actual.enabled && actual.status == 1 && actual.cadence == 1 && actual.inferences == 1);
        for (int i = 0; i < POLICY_OBSERVATIONS; ++i)
            TIDE_CHECK(policy_number(&world, actual.observation, i) == policy_number(&fresh, expected.observation, i));
        for (int i = 0; i < POLICY_JOINTS; ++i) {
            TIDE_CHECK(policy_number(&world, actual.observation, 34 + i) == 0);
            TIDE_CHECK(policy_number(&world, actual.action, i) == policy_number(&fresh, expected.action, i));
            TIDE_CHECK(policy_number(&world, actual.filtered, i) == policy_number(&fresh, expected.filtered, i));
            TIDE_CHECK(policy_number(&world, actual.motor, i) == policy_number(&fresh, expected.motor, i));
            policy_close(policy_number(&world, actual.filtered, i),
                policy_home[i] + .9f * policy_number(&world, actual.action, i), 1e-6f);
            const Newton_Joint actual_joint = tide_get_Newton_Joint(&world, policy_entity(&world, actual_duck.joints, i));
            const Newton_Joint expected_joint = tide_get_Newton_Joint(&fresh, policy_entity(&fresh, expected_duck.joints, i));
            TIDE_CHECK(actual_joint.friction == expected_joint.friction);
            policy_close(actual_joint.position, expected_joint.position, 0);
            policy_close(actual_joint.velocity, expected_joint.velocity, 0);
            policy_close(actual_joint.load, expected_joint.load, 0);
        }
        for (int i = 0; i < 15; ++i) {
            const Newton_Body actual_body = tide_get_Newton_Body(&world, policy_entity(&world, actual_duck.bodies, i));
            const Newton_Body expected_body = tide_get_Newton_Body(&fresh, policy_entity(&fresh, expected_duck.bodies, i));
            if (memcmp(&actual_body, &expected_body, sizeof(actual_body)) != 0)
                printf("    reset cadence %d body %d differs from fresh physical step\n", residue, i);
            TIDE_CHECK(memcmp(&actual_body, &expected_body, sizeof(actual_body)) == 0);
        }
        TIDE_CHECK(actual.previousMotorLoad == expected.previousMotorLoad);
        policy_check_no_assistance(&world, 0);
        /* A held R is one edge, not an episode restarted every match tick. */
        for (int i = 0; i < 4; ++i) policy_step(&world, reset, (DuckControls){0}, NULL);
        TIDE_CHECK(tide_get_DuckPolicyState(&world, policy_duck(&world, 0)).inferences == 2);
        tide_world_free(&world);
        tide_world_free(&fresh);
    }
}

TIDE_TEST(microduck_policy_manual_toggle_resets_feedback_without_retriggering)
{
    static tide_world world;
    tide_world_init(&world, POLICY_DT);
    for (int i = 0; i < 9; ++i)
        policy_step(&world, (DuckControls){.walk = 1}, (DuckControls){0}, NULL);
    const tide_entity entity = policy_duck(&world, 0);
    /* R then M is the documented system order: reset the learned state,
     * switch to manual, and keep that mode while M remains held. */
    const DuckControls manual = {.manual = true, .reset = true};
    policy_step(&world, manual, (DuckControls){0}, NULL);
    DuckPolicyState state = tide_get_DuckPolicyState(&world, entity);
    TIDE_CHECK(!state.enabled && state.status == 0 && state.cadence == 0 && state.inferences == 0);
    TIDE_CHECK(tide_list_read_count(&world.heap, state.action) == 0);
    TIDE_CHECK(tide_list_read_count(&world.heap, state.filtered) == 0);
    TIDE_CHECK(tide_list_read_count(&world.heap, state.motor) == 0);
    TIDE_CHECK(tide_list_read_count(&world.heap, state.observation) == 0);
    TIDE_CHECK(state.previousMotorLoad == 0);
    TIDE_CHECK(tide_get_Duck(&world, entity).assisted);
    policy_step(&world, manual, (DuckControls){0}, NULL);
    TIDE_CHECK(!tide_get_DuckPolicyState(&world, entity).enabled);
    /* Shift changes the learned command only. Exercise manual gait and joint
     * trim together, beyond its settle delay, against the same world copy. */
    static tide_world running;
    tide_world_copy(&running, &world);
    const DuckControls trim = {.walk = .75f, .drive = -.5f, .turn = .5f};
    DuckControls run_trim = trim;
    run_trim.run = true;
    for (int tick = 0; tick < 205; ++tick) {
        policy_step(&world, trim, (DuckControls){0}, NULL);
        policy_step(&running, run_trim, (DuckControls){0}, NULL);
    }
    const Duck walking_duck = tide_get_Duck(&world, entity);
    const Duck running_duck = tide_get_Duck(&running, entity);
    TIDE_CHECK(walking_duck.walk > 0 && walking_duck.phase > 0 &&
        walking_duck.walk == running_duck.walk && walking_duck.phase == running_duck.phase &&
        walking_duck.selected == running_duck.selected &&
        walking_duck.settleTicks == running_duck.settleTicks);
    for (int i = 0; i < POLICY_JOINTS; ++i) {
        TIDE_CHECK(policy_number(&world, walking_duck.trim, i) ==
            policy_number(&running, running_duck.trim, i));
        TIDE_CHECK(policy_number(&world, walking_duck.targets, i) ==
            policy_number(&running, running_duck.targets, i));
    }
    for (int i = 0; i < 15; ++i) {
        const Newton_Body walking_body = tide_get_Newton_Body(&world,
            policy_entity(&world, walking_duck.bodies, i));
        const Newton_Body running_body = tide_get_Newton_Body(&running,
            policy_entity(&running, running_duck.bodies, i));
        TIDE_CHECK(memcmp(&walking_body, &running_body, sizeof(walking_body)) == 0);
    }
    TIDE_CHECK(!tide_get_DuckPolicyState(&running, entity).enabled);
    tide_world_free(&running);
    policy_step(&world, (DuckControls){0}, (DuckControls){0}, NULL);
    policy_step(&world, (DuckControls){.manual = true, .walk = -.4f}, (DuckControls){0}, NULL);
    state = tide_get_DuckPolicyState(&world, entity);
    TIDE_REQUIRE(state.enabled && state.status == 1 && state.cadence == 1 && state.inferences == 1);
    TIDE_REQUIRE(tide_list_read_count(&world.heap, state.motor) == POLICY_JOINTS);
    for (int i = 0; i < POLICY_JOINTS; ++i) {
        TIDE_CHECK(policy_number(&world, state.observation, 34 + i) == 0);
        policy_close(policy_number(&world, state.filtered, i),
            policy_home[i] + .9f * policy_number(&world, state.action, i), 1e-6f);
    }
    policy_check_no_assistance(&world, 0);
    const int count = state.inferences;
    policy_step(&world, (DuckControls){.manual = true}, (DuckControls){0}, NULL);
    TIDE_CHECK(tide_get_DuckPolicyState(&world, entity).enabled);
    TIDE_CHECK(tide_get_DuckPolicyState(&world, entity).inferences == count);
    tide_world_free(&world);
}

TIDE_TEST(microduck_policy_wasd_requests_true_commands_without_assistance)
{
    static tide_local local;
    tide_local_init(&local);
    local.Rooms_Lobby.playing = true;
    static const struct {
        bool w, s, a, d, left_shift, right_shift;
        float stick_x, stick_y, walk, turn;
    } cases[] = {
        {.w = true, .walk = 1},
        {.s = true, .walk = -1},
        {.a = true, .turn = 1},
        {.d = true, .turn = -1},
        {.w = true, .s = true, .a = true, .d = true},
        {.w = true, .a = true, .walk = 1, .turn = 1},
        {.w = true, .left_shift = true, .walk = 1},
        {.w = true, .right_shift = true, .walk = 1},
        {.s = true, .left_shift = true, .walk = -1},
        {.s = true, .right_shift = true, .walk = -1},
        {.left_shift = true},
        {.right_shift = true},
        {.w = true, .s = true, .left_shift = true, .right_shift = true},
        {.a = true, .left_shift = true, .turn = 1},
        {.d = true, .right_shift = true, .turn = -1},
        {.a = true, .right_shift = true, .turn = 1},
        {.d = true, .left_shift = true, .turn = -1},
        {.w = true, .a = true, .left_shift = true, .walk = 1, .turn = 1},
        {.w = true, .d = true, .right_shift = true, .walk = 1, .turn = -1},
        {.s = true, .a = true, .right_shift = true, .walk = -1, .turn = 1},
        {.s = true, .d = true, .left_shift = true, .walk = -1, .turn = -1},
        {.w = true, .left_shift = true, .right_shift = true, .walk = 1},
        {.w = true, .a = true, .left_shift = true, .right_shift = true, .walk = 1, .turn = 1},
        {.s = true, .d = true, .left_shift = true, .right_shift = true, .walk = -1, .turn = -1},
        {.stick_x = -.75f, .stick_y = .5f, .walk = .5f, .turn = .75f},
        {.stick_x = .75f, .stick_y = -.5f, .walk = -.5f, .turn = -.75f},
        {.left_shift = true, .stick_y = .5f, .walk = .5f},
        {.right_shift = true, .stick_y = -.5f, .walk = -.5f},
        {.left_shift = true, .stick_x = -.75f, .stick_y = .5f, .walk = .5f, .turn = .75f},
        {.right_shift = true, .stick_x = .75f, .stick_y = -.5f, .walk = -.5f, .turn = -.75f}
    };
    for (unsigned key = 0; key < sizeof(cases) / sizeof(cases[0]); ++key) {
        tide_devices devices = {0};
        tide_button_set(&devices.keyboard.w, cases[key].w);
        tide_button_set(&devices.keyboard.s, cases[key].s);
        tide_button_set(&devices.keyboard.a, cases[key].a);
        tide_button_set(&devices.keyboard.d, cases[key].d);
        tide_button_set(&devices.keyboard.leftShift, cases[key].left_shift);
        tide_button_set(&devices.keyboard.rightShift, cases[key].right_shift);
        devices.gamepad.leftStick = tide_f2(cases[key].stick_x, cases[key].stick_y);
        tide_button_set(&devices.keyboard.b, true);
        const float walk = cases[key].walk, turn = cases[key].turn;
        const bool run = cases[key].left_shift || cases[key].right_shift;
        const float command = (run ? .5f : .4f) * walk;
        const float yaw = (run ? -1.25f : -1.f) * turn;
        DuckControls previous_input = {0};
        static tide_world world;
        tide_world_init(&world, POLICY_DT);
        for (int tick = 0; tick < 5; ++tick) {
            const DuckControls sampled = tide_input_sample(&devices, &local);
            DuckControls input = {0};
            TIDE_REQUIRE(policy_transport_input(sampled, &previous_input, &input));
            /* The manual trim drive keeps its original sign. Learned yaw has
             * the opposite input sign for both the keyboard and stick. */
            TIDE_CHECK(input.walk == walk && input.turn == turn && input.drive == -turn &&
                input.run == run && input.assistance);
            if (tick > 0)
                TIDE_CHECK(!devices.keyboard.leftShift.down && !devices.keyboard.rightShift.down);
            policy_step(&world, input, (DuckControls){0}, NULL);
            const DuckPolicyState state = tide_get_DuckPolicyState(&world, policy_duck(&world, 0));
            TIDE_REQUIRE(state.status == 1);
            TIDE_CHECK(state.forwardSpeed == .4f && state.runSpeed == .5f);
            TIDE_CHECK(state.command.x == command && state.command.y == 0 && state.command.z == yaw);
            TIDE_CHECK(policy_number(&world, state.observation, 48) == command);
            TIDE_CHECK(policy_number(&world, state.observation, 49) == 0);
            TIDE_CHECK(policy_number(&world, state.observation, 50) == yaw);
            for (int i = 51; i < POLICY_OBSERVATIONS; ++i)
                TIDE_CHECK(policy_number(&world, state.observation, i) == 0);
            tide_devices_consume(&devices);
        }
        if (cases[key].left_shift && cases[key].right_shift) {
            tide_button_set(&devices.keyboard.leftShift, false);
            DuckControls left_released = {0};
            TIDE_REQUIRE(policy_transport_input(tide_input_sample(&devices, &local),
                &previous_input, &left_released));
            TIDE_CHECK(left_released.run && left_released.walk == walk && left_released.turn == turn);
            policy_step(&world, left_released, (DuckControls){0}, NULL);
            tide_devices_consume(&devices);
            DuckControls right_held = {0};
            TIDE_REQUIRE(policy_transport_input(tide_input_sample(&devices, &local),
                &previous_input, &right_held));
            TIDE_CHECK(right_held.run && right_held.walk == walk && right_held.turn == turn);
            policy_step(&world, right_held, (DuckControls){0}, NULL);
            const DuckPolicyState still_running = tide_get_DuckPolicyState(&world, policy_duck(&world, 0));
            TIDE_CHECK(still_running.command.x == command && still_running.command.z == yaw);
        }
        /* A release window retains pressed until the host samples and
         * consumes it. The following sample must return to walking and normal
         * yaw, even between the actor's four-tick inferences. Keep WASD held. */
        tide_button_set(&devices.keyboard.leftShift, false);
        tide_button_set(&devices.keyboard.rightShift, false);
        DuckControls releasing = {0};
        TIDE_REQUIRE(policy_transport_input(tide_input_sample(&devices, &local),
            &previous_input, &releasing));
        TIDE_CHECK(releasing.run == run && releasing.walk == walk && releasing.turn == turn);
        policy_step(&world, releasing, (DuckControls){0}, NULL);
        const DuckPolicyState release_window = tide_get_DuckPolicyState(&world, policy_duck(&world, 0));
        TIDE_CHECK(release_window.command.x == command && release_window.command.z == yaw);
        tide_devices_consume(&devices);
        DuckControls walking = {0};
        TIDE_REQUIRE(policy_transport_input(tide_input_sample(&devices, &local),
            &previous_input, &walking));
        TIDE_CHECK(!walking.run && walking.walk == walk && walking.turn == turn);
        policy_step(&world, walking, (DuckControls){0}, NULL);
        const DuckPolicyState walking_state = tide_get_DuckPolicyState(&world, policy_duck(&world, 0));
        TIDE_CHECK(walking_state.command.x == .4f * walk && walking_state.command.z == -turn);
        const Duck duck = tide_get_Duck(&world, policy_duck(&world, 0));
        TIDE_CHECK(duck.phase == 0 && duck.settleTicks == 0);
        policy_check_no_assistance(&world, 0);
        policy_step(&world, (DuckControls){0}, (DuckControls){0}, NULL);
        const DuckPolicyState released = tide_get_DuckPolicyState(&world, policy_duck(&world, 0));
        policy_vector_zero(released.command);
        tide_world_free(&world);
    }
    local.Rooms_Lobby.playing = false;
    tide_devices blocked = {0};
    tide_button_set(&blocked.keyboard.w, true);
    tide_button_set(&blocked.keyboard.a, true);
    tide_button_set(&blocked.keyboard.leftShift, true);
    tide_button_set(&blocked.keyboard.rightShift, true);
    const DuckControls input = tide_input_sample(&blocked, &local);
    TIDE_CHECK(input.walk == 0 && input.drive == 0 && input.turn == 0 &&
        !input.run && !input.manual && !input.reset);
    tide_local_free(&local);
}

static DuckControls policy_trace(int tick, int seat, int variant)
{
    const int phase = (tick + 17 * seat + 11 * variant) / 13;
    return (DuckControls){.walk = phase % 3 == 0 ? 1 : phase % 3 == 1 ? -.6f : 0,
        .turn = phase % 4 == 0 ? -.7f : phase % 4 == 1 ? .4f : 0,
        .run = (tick / 7 + seat + variant) % 2 != 0,
        .assistance = (tick / 9) % 2 != 0,
        .reset = tick == 31 && seat == 0};
}

static void policy_trace_tick(tide_world *world, int variant, const tide_jobs *jobs)
{
    const int tick = world->Time.tick;
    policy_step(world, policy_trace(tick, 0, variant), policy_trace(tick, 1, variant), jobs);
}

TIDE_TEST(microduck_policy_snapshot_pack_raw_delta_replay_all_cadences)
{
    for (int residue = 0; residue < 4; ++residue) {
        static tide_world source, base, saved, replay, packed, raw, delta, other;
        tide_world_init(&source, POLICY_DT);
        tide_world_init(&other, POLICY_DT);
        for (int tick = 0; tick < 8; ++tick) policy_trace_tick(&source, 0, NULL);
        tide_world_copy(&base, &source);
        for (int tick = 0; tick < 4 + residue; ++tick) policy_trace_tick(&source, 0, NULL);
        const DuckPolicyState state = tide_get_DuckPolicyState(&source, policy_duck(&source, 0));
        TIDE_REQUIRE(state.status == 1 && state.cadence == residue && state.inferences >= 3);
        TIDE_REQUIRE(tide_list_read_count(&source.heap, state.action) == POLICY_JOINTS);
        TIDE_REQUIRE(tide_list_read_count(&source.heap, state.filtered) == POLICY_JOINTS);
        TIDE_REQUIRE(tide_list_read_count(&source.heap, state.motor) == POLICY_JOINTS);
        bool nonzero = false;
        for (int i = 0; i < POLICY_JOINTS; ++i) nonzero = nonzero || fabsf(policy_number(&source, state.action, i)) > .001f;
        TIDE_REQUIRE(nonzero && state.previousMotorLoad > 0);
        tide_world_copy(&saved, &source);
        tide_world_copy(&replay, &source);
        const uint64_t saved_hash = tide_world_hash(&saved);
        const uint32_t size = tide_world_pack(&source, NULL, 0);
        uint8_t *bytes = malloc(size);
        TIDE_REQUIRE(bytes && tide_world_pack(&source, bytes, size) == size);
        TIDE_REQUIRE(tide_world_unpack(&packed, bytes, size));
        free(bytes);
        uint32_t raw_size = 0, delta_size = 0;
        bytes = tide_world_pack_delta(&source, NULL, NULL, 0, &raw_size);
        TIDE_REQUIRE(bytes && tide_world_unpack_delta(&raw, NULL, bytes, raw_size));
        free(bytes);
        bytes = tide_world_pack_delta(&source, &base, NULL, 0, &delta_size);
        TIDE_REQUIRE(bytes && tide_world_unpack_delta(&delta, &base, bytes, delta_size));
        free(bytes);
        TIDE_CHECK(tide_world_hash(&packed) == saved_hash && tide_world_hash(&raw) == saved_hash &&
            tide_world_hash(&delta) == saved_hash);
        for (int tick = 0; tick < 64; ++tick) {
            policy_trace_tick(&source, 0, NULL);
            policy_trace_tick(&other, 3, NULL); /* unrelated NN history interleaved */
            policy_trace_tick(&delta, 0, NULL);
            policy_trace_tick(&replay, 0, NULL);
            policy_trace_tick(&raw, 0, NULL);
            policy_trace_tick(&packed, 0, NULL);
            const uint64_t expected = tide_world_hash(&source);
            TIDE_REQUIRE(source.Newton_Physics.status == Newton_Status_Ok);
            TIDE_REQUIRE(tide_world_hash(&replay) == expected && tide_world_hash(&packed) == expected &&
                tide_world_hash(&raw) == expected && tide_world_hash(&delta) == expected);
            TIDE_CHECK(tide_world_hash(&saved) == saved_hash);
        }
        /* Observations only; no native/WASM pin is invented before both run. */
        printf("    policy replay residue %d tick %d: %016llx\n", residue, source.Time.tick,
            (unsigned long long)tide_world_hash(&source));
        tide_world_free(&source); tide_world_free(&base); tide_world_free(&saved);
        tide_world_free(&replay); tide_world_free(&packed); tide_world_free(&raw);
        tide_world_free(&delta); tide_world_free(&other);
    }
}

TIDE_TEST(microduck_policy_feedback_is_hashed_and_snapshot_owned)
{
    static tide_world source, saved, changed;
    tide_world_init(&source, POLICY_DT);
    for (int tick = 0; tick < 5; ++tick) policy_trace_tick(&source, 0, NULL);
    tide_world_copy(&saved, &source);
    const uint64_t expected = tide_world_hash(&saved);
    for (int field = 0; field < 7; ++field) {
        tide_world_copy(&changed, &saved);
        const tide_entity entity = policy_duck(&changed, 0);
        DuckPolicyState state = tide_get_DuckPolicyState(&changed, entity);
        if (field <= 3) {
            tide_text_use(&changed.heap, NULL);
            tide_list values = field == 0 ? state.action : field == 1 ? state.filtered :
                field == 2 ? state.observation : state.motor;
            float *value = tide_list_at_mut(values, 0, sizeof(float));
            TIDE_REQUIRE(value);
            *value += .125f;
        } else if (field == 4) state.cadence = (state.cadence + 1) % 4;
        else if (field == 5) state.inferences += 1;
        else state.previousMotorLoad += .125f;
        TIDE_REQUIRE(tide_set_DuckPolicyState(&changed, entity, state));
        TIDE_CHECK(tide_world_hash(&changed) != expected);
        TIDE_CHECK(tide_world_hash(&saved) == expected && tide_world_hash(&source) == expected);
    }
    tide_world_free(&source); tide_world_free(&saved); tide_world_free(&changed);
}

#ifdef NEWTON_TEST_JOBS
typedef struct policy_jobs_probe {
    const tide_jobs *pool;
    uint32_t worker_entries;
} policy_jobs_probe;

typedef struct policy_jobs_call {
    policy_jobs_probe *probe;
    void (*work)(void *context, uint32_t thread);
    void *context;
} policy_jobs_call;

static void policy_jobs_work(void *context, uint32_t thread)
{
    policy_jobs_call *call = context;
    if (thread) __atomic_fetch_add(&call->probe->worker_entries, 1u, __ATOMIC_RELAXED);
    call->work(call->context, thread);
}

static void policy_jobs_run(void *self, void (*work)(void *, uint32_t), void *context)
{
    policy_jobs_probe *probe = self;
    policy_jobs_call call = {.probe = probe, .work = work, .context = context};
    probe->pool->run(probe->pool->self, policy_jobs_work, &call);
}

static void policy_jobs_more(void *self, uint32_t count)
{
    policy_jobs_probe *probe = self;
    probe->pool->more(probe->pool->self, count);
}

static void policy_jobs_noop(void *context) { (void)context; }

static uint32_t policy_jobs_prime_count(const void *world, uint64_t *work)
{
    (void)world;
    *work = 1;
    return 1;
}

static void policy_jobs_prime_run(void *world, uint32_t task, uint32_t pieces)
{
    (void)world; (void)task; (void)pieces;
    const tide_job job = tide_job_start(policy_jobs_noop, NULL);
    tide_job_wait(job);
}

static void policy_jobs_prime(tide_world *world, const tide_jobs *pool)
{
    /* This small fixture lies below the scheduler's normal work threshold.
     * Exercise its supported adaptive C-job hint before each game tick, so
     * the test covers the parallel game path without changing production
     * thresholds or match state. The probe is run on the unwrapped pool:
     * worker counts below therefore belong only to the actual game tick. */
    const tide_system_tasks system = {.count = policy_jobs_prime_count, .run = policy_jobs_prime_run,
        .name = "policy test adaptive C-job hint"};
    const tide_tick_systems tick = {.systems = &system, .count = 1,
        .queue = &world->commands, .entities = &world->entities};
    tide_run_systems(world, &tick, pool);
}

TIDE_TEST(microduck_policy_threaded_and_serial_ticks_match_with_real_history)
{
    const tide_jobs *pool = tide_platform_jobs();
    if (!pool || pool->threads < 2 || !pool->run) {
        printf("    SKIP policy worker coverage: no multithreaded jobs pool\n");
        return;
    }
    policy_jobs_probe probe = {.pool = pool};
    const tide_jobs jobs = {.self = &probe, .threads = pool->threads, .run = policy_jobs_run,
        .profile = pool->profile, .more = pool->more ? policy_jobs_more : NULL};
    static tide_world serial, threaded, other;
    tide_world_init(&serial, POLICY_DT);
    tide_world_init(&threaded, POLICY_DT);
    tide_world_init(&other, POLICY_DT);
    for (int tick = 0; tick < 96; ++tick) {
        policy_trace_tick(&serial, 0, NULL);
        policy_trace_tick(&other, 2, pool);
        policy_jobs_prime(&threaded, pool);
        policy_trace_tick(&threaded, 0, &jobs);
        TIDE_REQUIRE(serial.Newton_Physics.status == Newton_Status_Ok);
        TIDE_REQUIRE(tide_world_hash(&serial) == tide_world_hash(&threaded));
    }
    const uint32_t workers = __atomic_load_n(&probe.worker_entries, __ATOMIC_RELAXED);
    if (!workers) printf("    SKIP policy worker coverage: scheduler used its serial fallback\n");
    else printf("    policy scheduler worker entries: %u\n", workers);
    tide_world_free(&serial); tide_world_free(&threaded); tide_world_free(&other);
}
#endif
