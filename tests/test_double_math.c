#include <float.h>
#include <inttypes.h>
#include <stdio.h>

#include "tide/math.h"
#include "tide_test.h"
#include "double_math_cases.h"

static double evaluate(const int function, const double x, const double y)
{
    switch (function) {
    case 0: return tide_sin_d(x);
    case 1: return tide_cos_d(x);
    case 2: return tide_tan_d(x);
    case 3: return tide_asin_d(x);
    case 4: return tide_acos_d(x);
    case 5: return tide_atan_d(x);
    case 6: return tide_atan2_d(x, y);
    case 7: return tide_exp_d(x);
    case 8: return tide_exp2_d(x);
    case 9: return tide_log_d(x);
    case 10: return tide_log2_d(x);
    case 11: return tide_log10_d(x);
    default: return tide_pow_d(x, y);
    }
}

static uint64_t ordered(const uint64_t bits)
{
    return bits >> 63 ? ~bits : bits | UINT64_C(0x8000000000000000);
}

TIDE_TEST(double_transcendentals_match_high_precision_reference_values)
{
    for (size_t i = 0; i < sizeof double_math_cases / sizeof double_math_cases[0]; i++) {
        const double x = tide_d_from_bits(double_math_cases[i].x);
        const double y = tide_d_from_bits(double_math_cases[i].y);
        const double got = evaluate(double_math_cases[i].function, x, y);
        const uint64_t expected = double_math_cases[i].expected;
        if (got == 0 && (expected << 1) == 0) continue; // Signed zero is checked separately.
        const uint64_t a = ordered(tide_d_bits(got)), b = ordered(expected);
        const uint64_t distance = a > b ? a - b : b - a;
        if (distance > 1 || (!tide_is_finite_d(got) && tide_d_bits(got) != expected)) {
            printf("    case %zu function %d x=%016" PRIX64 " y=%016" PRIX64
                   " got=%016" PRIX64 " reference=%016" PRIX64 "\n",
                   i, double_math_cases[i].function, double_math_cases[i].x,
                   double_math_cases[i].y, tide_d_bits(got), expected);
            TIDE_CHECK(false);
        }
    }
}

TIDE_TEST(double_transcendental_special_values_and_full_range)
{
    const double inf = tide_d_from_bits(UINT64_C(0x7FF0000000000000));
    const double nan = tide_d_from_bits(UINT64_C(0x7FF8000000000000));
    TIDE_CHECK(tide_d_bits(tide_sin_d(-0.0)) == UINT64_C(0x8000000000000000));
    TIDE_CHECK(tide_d_bits(tide_tan_d(-0.0)) == UINT64_C(0x8000000000000000));
    TIDE_CHECK(tide_d_bits(tide_asin_d(-0.0)) == UINT64_C(0x8000000000000000));
    TIDE_CHECK(tide_d_bits(tide_atan_d(-0.0)) == UINT64_C(0x8000000000000000));
    TIDE_CHECK(tide_d_bits(tide_atan2_d(-0.0, 1.0)) == UINT64_C(0x8000000000000000));
    TIDE_CHECK(tide_atan2_d(0.0, -0.0) == 0x1.921fb54442d18p+1);
    TIDE_CHECK(tide_atan2_d(-inf, -inf) == -0x1.2d97c7f3321d2p+1);
    const double bad[] = {tide_sin_d(inf), tide_cos_d(-inf), tide_tan_d(inf),
        tide_asin_d(2), tide_acos_d(-2), tide_log_d(-1), tide_log2_d(-1), tide_log10_d(-1), tide_pow_d(-2, .5)};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) TIDE_CHECK(bad[i] != bad[i]);
    TIDE_CHECK(tide_exp_d(inf) == inf && tide_exp_d(-inf) == 0);
    TIDE_CHECK(tide_exp2_d(inf) == inf && tide_exp2_d(-inf) == 0);
    TIDE_CHECK(tide_log_d(0) == -inf && tide_log2_d(-0.0) == -inf && tide_log10_d(inf) == inf);
    TIDE_CHECK(tide_pow_d(nan, 0) == 1 && tide_pow_d(1, nan) == 1);
    TIDE_CHECK(tide_d_bits(tide_pow_d(-0.0, 3)) == UINT64_C(0x8000000000000000));
    TIDE_CHECK(tide_pow_d(-0.0, -3) == -inf && tide_pow_d(-inf, 3) == -inf);
    TIDE_CHECK(tide_exp2_d(-1074) == DBL_TRUE_MIN && tide_log2_d(DBL_TRUE_MIN) == -1074);
    TIDE_CHECK(tide_is_finite_d(tide_sin_d(DBL_MAX)) && tide_is_finite_d(tide_cos_d(DBL_MAX)));
    TIDE_CHECK(tide_sin_d(1.0) != (double)tide_sin_f(1.0f));
}

TIDE_TEST(double_matrix_inverse_and_large_translations_keep_precision)
{
    const tide_double2x2 a = {{2, 0}, {1, 4}};
    const tide_double2x2 ai = tide_inverse_d2x2(a);
    TIDE_CHECK(ai.c0.x == .5 && ai.c1.x == -.125 && ai.c1.y == .25);
    const tide_double3x3 b = {{2, 0, 0}, {0, 4, 0}, {0, 0, 8}};
    TIDE_CHECK(tide_determinant_d3x3(b) == 64);
    const tide_double3x3 bi = tide_inverse_d3x3(b);
    TIDE_CHECK(bi.c0.x == .5 && bi.c1.y == .25 && bi.c2.z == .125);
    const tide_double4x4 t = tide_trs_d4x4(tide_d3(1000000000.125, 2, 3), tide_identity_d3x3(), tide_d3(2, 4, 8));
    const tide_double4x4 inverse = tide_inverse_d4x4(t);
    TIDE_CHECK(tide_determinant_d4x4(t) == 64 && inverse.c3.x == -500000000.0625);
    const tide_double3 point = tide_transform_d4x4(t, tide_d3(.125, 1, 2));
    TIDE_CHECK(point.x == 1000000000.375 && point.y == 6 && point.z == 19);
    const tide_double3 original = tide_transform_d4x4(inverse, point);
    TIDE_CHECK(original.x == .125 && original.y == 1 && original.z == 2);
    const tide_double4x4 identity = tide_mul_d4x4(inverse, t);
    TIDE_CHECK(identity.c0.x == 1 && identity.c1.y == 1 && identity.c2.z == 1 && identity.c3.w == 1 && identity.c3.x == 0);
    const tide_float4x4 narrow = tide_f4x4_from_d4x4(t);
    TIDE_CHECK(narrow.c3.x == 1000000000.0f && tide_d4x4_from_f4x4(narrow).c3.x != t.c3.x);
}

static uint64_t append_bits(uint64_t hash, const double value)
{
    const uint64_t bits = value == value ? tide_d_bits(value) : UINT64_C(0x7FF8000000000000);
    for (int i = 0; i < 8; i++) hash = (hash ^ (uint8_t)(bits >> (i * 8))) * UINT64_C(0x100000001B3);
    return hash;
}

TIDE_TEST(crossplatform_double_transcendentals_and_matrices)
{
    uint64_t hash = UINT64_C(0xCBF29CE484222325);
    for (size_t i = 0; i < sizeof double_math_cases / sizeof double_math_cases[0]; i++) {
        hash = append_bits(hash, evaluate(double_math_cases[i].function,
                           tide_d_from_bits(double_math_cases[i].x), tide_d_from_bits(double_math_cases[i].y)));
    }
    for (int i = 1; i <= 1000; i++) {
        const double angle = (double)i * 0x1p-6;
        const double c = tide_cos_d(angle), s = tide_sin_d(angle);
        const tide_double3x3 rotation = {{c, s, 0}, {-s, c, 0}, {0, 0, 1}};
        const tide_double4x4 m = tide_trs_d4x4(tide_d3(1000000000.125 + i, -i, i), rotation, tide_d3(2, 3, 4));
        const tide_double4x4 inverse = tide_inverse_d4x4(m);
        const tide_double3 p = tide_transform_d4x4(inverse, tide_d3(i, -i, i));
        hash = append_bits(hash, tide_determinant_d4x4(m));
        hash = append_bits(hash, p.x);
        hash = append_bits(hash, p.y);
        hash = append_bits(hash, p.z);
    }
    printf("    double math hash: %016" PRIX64 "\n", hash);
    TIDE_CHECK(hash == UINT64_C(0xA8DEFF186244546C));
}
