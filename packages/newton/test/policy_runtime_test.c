/* Standalone policy-only test. Deliberately outside the game source tree. */
#include "../../../examples/robotics/microduck/policy/inference.h"
#include "../../../examples/robotics/microduck/test/policy_expected.h"
#include "tide/math.h"

#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

/* Pin only after native and WASI full output streams have been compared. */
#define MD_POLICY_REPLAY_HASH UINT64_C(0x1780e8e7b6265eef)

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "policy check failed at line %d: %s\n", __LINE__, #condition); ++failures; \
} } while (0)

static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static float random_unit(uint32_t *state)
{
    const int value = (int)((next_random(state) >> 8) % 4097u) - 2048;
    return (float)value / 2048.0f;
}

static void append_actions(FILE *dump, const float *actions, uint64_t *hash)
{
    for (int i = 0; i < MD_POLICY_ACTION_COUNT; ++i) {
        const uint32_t bits = tide_f_bits(actions[i]);
        unsigned char bytes[4];
        for (int j = 0; j < 4; ++j) {
            bytes[j] = (unsigned char)(bits >> (8 * j));
            *hash = (*hash ^ bytes[j]) * UINT64_C(0x100000001b3);
        }
        if (dump) CHECK(fwrite(bytes, 1, 4, dump) == 4);
    }
}

static void check_failures(void)
{
    float observations[MD_POLICY_OBSERVATION_COUNT];
    float actions[MD_POLICY_ACTION_COUNT];
    float sentinel[MD_POLICY_ACTION_COUNT];
    for (int i = 0; i < MD_POLICY_ACTION_COUNT; ++i) sentinel[i] = 123.0f + (float)i;
    memcpy(actions, sentinel, sizeof actions);
    CHECK(md_policy_evaluate(NULL, actions) == 0);
    CHECK(memcmp(actions, sentinel, sizeof actions) == 0);
    CHECK(md_policy_evaluate(md_policy_fixture_observations[1], NULL) == 0);
    const uint32_t invalid[] = {UINT32_C(0x7fc12345), UINT32_C(0x7f800000), UINT32_C(0xff800000)};
    for (int kind = 0; kind < 3; ++kind) {
        for (int i = 0; i < MD_POLICY_OBSERVATION_COUNT; ++i) {
            memcpy(observations, md_policy_fixture_observations[1], sizeof observations);
            observations[i] = tide_f_from_bits(invalid[kind]);
            CHECK(md_policy_evaluate(observations, actions) == 0);
            CHECK(memcmp(actions, sentinel, sizeof actions) == 0);
        }
    }
    for (int i = 0; i < MD_POLICY_OBSERVATION_COUNT; ++i) observations[i] = FLT_MAX;
    CHECK(md_policy_evaluate(observations, actions) == 0);
    CHECK(memcmp(actions, sentinel, sizeof actions) == 0);
    /* Also exercise overflow inside Gemm rather than the normalizer. */
    for (int i = 0; i < MD_POLICY_OBSERVATION_COUNT; ++i) observations[i] = 1.0e36f;
    CHECK(md_policy_evaluate(observations, actions) == 0);
    CHECK(memcmp(actions, sentinel, sizeof actions) == 0);
    float before[MD_POLICY_OBSERVATION_COUNT];
    observations[60] = tide_f_from_bits(invalid[0]);
    memcpy(before, observations, sizeof before);
    CHECK(md_policy_evaluate(observations, observations) == 0);
    CHECK(memcmp(observations, before, sizeof before) == 0);
    memcpy(observations, md_policy_fixture_observations[1], sizeof observations);
    CHECK(md_policy_evaluate(observations, actions) == 1);
    CHECK(md_policy_evaluate(observations, observations) == 1);
    CHECK(memcmp(observations, actions, sizeof actions) == 0);
    /* A/B/A checks that preceding calls do not change any hidden state. */
    float repeated[MD_POLICY_ACTION_COUNT];
    CHECK(md_policy_evaluate(md_policy_fixture_observations[7], repeated) == 1);
    CHECK(md_policy_evaluate(md_policy_fixture_observations[1], repeated) == 1);
    CHECK(memcmp(repeated, actions, sizeof actions) == 0);
}

int main(int argc, char **argv)
{
    FILE *dump = NULL;
    if (argc == 3 && strcmp(argv[1], "--dump") == 0) {
        dump = fopen(argv[2], "wb");
        if (!dump) { fprintf(stderr, "cannot open policy dump\n"); return 1; }
    } else if (argc != 1) {
        fprintf(stderr, "usage: policy_runtime_test [--dump file]\n");
        return 1;
    }
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    float max_error = 0.0f;
    float actions[MD_POLICY_ACTION_COUNT];
    for (int fixture = 0; fixture < MD_POLICY_FIXTURE_COUNT; ++fixture) {
        const int ok = md_policy_evaluate(md_policy_fixture_observations[fixture], actions);
        CHECK(ok == 1);
        if (!ok) continue;
        for (int i = 0; i < MD_POLICY_ACTION_COUNT; ++i) {
            const float expected = md_policy_fixture_expected[fixture][i];
            const float error = tide_abs_f(actions[i] - expected);
            if (error > max_error) max_error = error;
            if (!(error <= .0002f + .00002f * tide_abs_f(expected))) {
                fprintf(stderr, "ORT mismatch: %s action %d: %.9g vs %.9g\n",
                        md_policy_fixture_names[fixture], i, actions[i], expected);
                ++failures;
            }
        }
        append_actions(dump, actions, &hash);
    }
    check_failures();
    /* Synthetic observation replay with raw prior-action feedback. This tests
     * inference determinism across 512 steps; it is not a physics rollout.
     */
    uint32_t random = UINT32_C(0x4d445038);
    float previous[MD_POLICY_ACTION_COUNT] = {0};
    for (int step = 0; step < 512; ++step) {
        float observations[MD_POLICY_OBSERVATION_COUNT] = {0};
        for (int i = 0; i < 3; ++i) observations[i] = random_unit(&random) * .25f;
        observations[3] = (step / 64) % 2 ? .6f : 0.0f;
        observations[5] = (step / 64) % 2 ? -.8f : -1.0f;
        for (int i = 0; i < MD_POLICY_ACTION_COUNT; ++i) {
            const float noise = random_unit(&random) * .015625f;
            const float feedback = previous[i] * .03125f;
            observations[6 + i] = noise + feedback;
            observations[20 + i] = random_unit(&random) * .125f;
            observations[34 + i] = previous[i];
        }
        observations[48] = step < 128 ? 0.0f : .25f;
        observations[50] = step < 256 ? 0.0f : -.5f;
        CHECK(md_policy_evaluate(observations, actions) == 1);
        append_actions(dump, actions, &hash);
        for (int i = 0; i < MD_POLICY_ACTION_COUNT; ++i)
            previous[i] = tide_clamp_f(actions[i], -4.0f, 4.0f);
    }
    if (dump) CHECK(fclose(dump) == 0);
    CHECK(hash == MD_POLICY_REPLAY_HASH);
    printf("policy: %d ORT fixtures, max_abs_error=%.9g; 512 feedback steps; hash=%016" PRIx64 "; failures=%d\n",
           MD_POLICY_FIXTURE_COUNT, max_error, hash, failures);
    return failures ? 1 : 0;
}
