Portable binary64 math
======================

The selected C kernels come from the official musl 1.1.18 source release:
https://musl.libc.org/releases/musl-1.1.18.tar.gz

Archive SHA-256:
d017ee5d01aec0c522a1330fdff06b1e428cb409e1db819cc4935d5da4a5a118

These portable Sun fdlibm / FreeBSD kernels use basic IEEE operations with no
FMA or platform transcendental calls. They are kept separate from Tide's existing
float implementations, whose results remain unchanged. Later musl math algorithms
are not implicitly adopted. Correct rounding for every input is not promised.

Local changes:

- libm.h is a small Tide adapter: namespaced symbols, double_t fixed to binary64,
  memcpy-based word getters/setters, and Tide abs/floor/sqrt helpers. It requires
  IEEE binary64 with FLT_EVAL_METHOD == 0. Tide's normal noncontraction and strict
  arithmetic flags apply to these files on every target.
- scalbn.c includes that adapter instead of the system math header.
- pow.c forms its negative exponent adjustment with unsigned shift/addition;
  its bit-pattern comparison explicitly converts the signed word to uint32_t.
- Trailing whitespace is normalized. Original per-file notices remain intact;
  COPYRIGHT.h retains musl's complete notice and is shipped with private headers.

tests/double_math_cases.h contains 398 independent reference values generated at
400 decimal digits with mpmath 1.3.0. tests/generate_double_math_cases.py reproduces
them. Runtime tests allow one binary64 ulp, test special values separately, and pin
exact results together with matrix arithmetic across native/WASI debug/release.
