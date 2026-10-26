#ifndef MICRODUCK_POLICY_INFERENCE_H
#define MICRODUCK_POLICY_INFERENCE_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MD_POLICY_OBSERVATION_COUNT = 61,
    MD_POLICY_ACTION_COUNT = 14
};

/* Evaluate the released velstand policy, including its baked normalizer.
 * Returns 1 on success; 0 for null pointers, nonfinite input, or a nonfinite
 * intermediate/output. On failure actions is unchanged. The caller supplies
 * at least the counts above. Input/output may overlap. Stateless and reentrant.
 * Actions are the raw network output: do not normalize observations again here.
 * Compile with IEEE float32 semantics, no fast math and FP contraction disabled.
 */
int md_policy_evaluate(const float *observations, float *actions);

#ifdef __cplusplus
}
#endif
#endif
