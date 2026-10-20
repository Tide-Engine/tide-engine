#pragma once

// Portable binary64 subset of musl 1.1.18 (Sun fdlibm / FreeBSD kernels).
// Source: https://musl.libc.org/releases/musl-1.1.18.tar.gz
// Local adaptation: namespacing, memcpy word access and strict IEEE binary64.
// Original notices are retained per file; musl's notice is in COPYRIGHT.h.
// No platform libm, FMA, extended precision or floating exception dependency.
#include "tide/math.h"
#include "COPYRIGHT.h"
#include <float.h>

_Static_assert(DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024 && FLT_EVAL_METHOD == 0,
               "Tide double math requires IEEE binary64 without excess precision");

#pragma STDC FP_CONTRACT OFF
typedef double double_t;
#define FORCE_EVAL(x) do { volatile double tide_eval = (x); (void)tide_eval; } while (0)
#define EXTRACT_WORDS(hi, lo, d) do { const uint64_t tide_words = tide_d_bits(d); (hi) = tide_words >> 32; (lo) = (uint32_t)tide_words; } while (0)
#define GET_HIGH_WORD(hi, d) do { (hi) = (uint32_t)(tide_d_bits(d) >> 32); } while (0)
#define GET_LOW_WORD(lo, d) do { (lo) = (uint32_t)tide_d_bits(d); } while (0)
#define INSERT_WORDS(d, hi, lo) do { (d) = tide_d_from_bits(((uint64_t)(uint32_t)(hi) << 32) | (uint32_t)(lo)); } while (0)
#define SET_HIGH_WORD(d, hi) do { (d) = tide_d_from_bits((tide_d_bits(d) & UINT64_C(0xFFFFFFFF)) | ((uint64_t)(uint32_t)(hi) << 32)); } while (0)
#define SET_LOW_WORD(d, lo) do { (d) = tide_d_from_bits((tide_d_bits(d) & UINT64_C(0xFFFFFFFF00000000)) | (uint32_t)(lo)); } while (0)
#define fabs tide_abs_d
#define sqrt tide_sqrt_d
#define floor tide_floor_d
#define isnan(x) ((x) != (x))
#define sin tide_sin_d
#define cos tide_cos_d
#define tan tide_tan_d
#define asin tide_asin_d
#define acos tide_acos_d
#define atan tide_atan_d
#define atan2 tide_atan2_d
#define exp tide_exp_d
#define exp2 tide_exp2_d
#define log tide_log_d
#define log2 tide_log2_d
#define log10 tide_log10_d
#define pow tide_pow_d
#define __sin tide_kernel_sin_d
#define __cos tide_kernel_cos_d
#define __tan tide_kernel_tan_d
#define __rem_pio2 tide_kernel_rem_pio2_d
#define __rem_pio2_large tide_kernel_rem_pio2_large_d
#define scalbn tide_math_scalbn_d

int __rem_pio2_large(double *, double *, int, int, int);
int __rem_pio2(double, double *);
double __sin(double, double, int);
double __cos(double, double);
double __tan(double, double, int);
double scalbn(double, int);
