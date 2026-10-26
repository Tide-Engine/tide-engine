// The CPU kernel port shares Tide's math on every target. In particular,
// revolute forward kinematics must not call the platform's sinf/cosf.
#include "tide/math.h"

#include <stdio.h>
#include <stdlib.h>

#define NT_UNARY(name) \
    float nt_math_##name##f(float x) { return tide_##name##_f(x); } \
    double nt_math_##name(double x) { return tide_##name##_d(x); }
#define NT_BINARY(name) \
    float nt_math_##name##f(float x, float y) { return tide_##name##_f(x, y); } \
    double nt_math_##name(double x, double y) { return tide_##name##_d(x, y); }

NT_UNARY(sin)
NT_UNARY(cos)
NT_UNARY(tan)
NT_UNARY(asin)
NT_UNARY(acos)
NT_UNARY(atan)
NT_UNARY(exp)
NT_UNARY(exp2)
NT_UNARY(log)
NT_UNARY(log2)
NT_UNARY(log10)
NT_BINARY(atan2)
NT_BINARY(pow)

void _wp_assert(const char *message, const char *file, unsigned int line)
{
    fprintf(stderr, "Newton kernel assertion at %s:%u: %s\n", file, line, message);
    abort();
}

int _wp_isfinite(double x) { return tide_is_finite_d(x); }
int _wp_isnan(double x) { return (tide_d_bits(x) & UINT64_C(0x7FFFFFFFFFFFFFFF)) > UINT64_C(0x7FF0000000000000); }
int _wp_isinf(double x) { return (tide_d_bits(x) & UINT64_C(0x7FFFFFFFFFFFFFFF)) == UINT64_C(0x7FF0000000000000); }
