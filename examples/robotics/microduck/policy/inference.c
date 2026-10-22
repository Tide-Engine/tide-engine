#include "inference.h"
#include "model_weights.h"
#include "tide/math.h"

#include <float.h>
#include <stddef.h>

#if defined(__FAST_MATH__)
#error "MicroDuck policy requires -fno-fast-math -ffp-contract=off"
#endif
#pragma STDC FP_CONTRACT OFF
#if defined(__clang__)
#pragma clang fp reassociate(off)
#pragma clang fp contract(off)
#endif

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "MicroDuck policy requires IEEE-754 binary32");

/* Gemm alpha=beta=1, transB=1. The reduction order is ascending input index,
 * starting at +0, with bias added after the dot product. Every basic operation
 * rounds to binary32. Keep each ONNX node separate; never fold the normalizer,
 * fuse an activation, use SIMD horizontal reductions, or fuse multiply/add.
 */
static int md_policy_gemm(const float *input, const float *weight, const float *bias,
                          int input_count, int output_count, float *output)
{
    for (int row = 0; row < output_count; ++row) {
        float sum = 0.0f;
        for (int column = 0; column < input_count; ++column) {
            const float product = input[column] * weight[row * input_count + column];
            if (!tide_is_finite_f(product)) return 0;
            sum = sum + product;
            if (!tide_is_finite_f(sum)) return 0;
        }
        const float value = sum + bias[row];
        if (!tide_is_finite_f(value)) return 0;
        output[row] = value;
    }
    return 1;
}

static int md_policy_elu(float *values, int count)
{
    for (int i = 0; i < count; ++i) {
        /* ONNX Elu(alpha=1): x >= 0 ? x : exp(x)-1. */
        if (values[i] < 0.0f) {
            const float exponential = tide_exp_f(values[i]);
            if (!tide_is_finite_f(exponential)) return 0;
            values[i] = exponential - 1.0f;
        }
        if (!tide_is_finite_f(values[i])) return 0;
    }
    return 1;
}

int md_policy_evaluate(const float *observations, float *actions)
{
    if (observations == NULL || actions == NULL) return 0;
    float normalized[MD_POLICY_OBSERVATION_COUNT];
    float first[512];
    float second[256];
    float candidate[MD_POLICY_ACTION_COUNT];

    for (int i = 0; i < MD_POLICY_OBSERVATION_COUNT; ++i) {
        if (!tide_is_finite_f(observations[i])) return 0;
        const float centered = observations[i] - md_policy_mean[i];
        if (!tide_is_finite_f(centered)) return 0;
        normalized[i] = centered / md_policy_divisor[i];
        if (!tide_is_finite_f(normalized[i])) return 0;
    }
    if (!md_policy_gemm(normalized, md_policy_weight_0, md_policy_bias_0, 61, 512, first) ||
        !md_policy_elu(first, 512) ||
        !md_policy_gemm(first, md_policy_weight_1, md_policy_bias_1, 512, 256, second) ||
        !md_policy_elu(second, 256) ||
        !md_policy_gemm(second, md_policy_weight_2, md_policy_bias_2, 256, 128, first) ||
        !md_policy_elu(first, 128) ||
        !md_policy_gemm(first, md_policy_weight_3, md_policy_bias_3, 128, 14, candidate)) return 0;

    /* One commit point. Failures above never write through actions, even when
     * the caller aliases it with observations. All workspace is on the stack.
     */
    for (int i = 0; i < MD_POLICY_ACTION_COUNT; ++i) actions[i] = candidate[i];
    return 1;
}
