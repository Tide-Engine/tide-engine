# NVIDIA Newton sources

This package uses NVIDIA Newton, the Linux Foundation robotics simulator at
<https://github.com/newton-physics/newton>. It is unrelated to Newton Game Dynamics.

The selected solver is Newton's float32 Featherstone implementation. Its actual
upstream kernels are generated into `kernels/featherstone.inl`; `runtime.cpp`
supplies model arrays and invokes the kernels in the upstream solver's order.
It does not replace Newton with a solver written for Tide.

## Pinned inputs

- Newton `ea4899864046dcd5981f8ba0bc3d8c25e65dcb96` (`1.8.0.dev0`).
- Warp `b346b941a4c0cd19c510db67d6506d781464ed5b`, matching the
  `warp-lang==1.19.0.dev20261020` generator wheel.
- Upstream solver: `newton/_src/solvers/featherstone/kernels.py` and the
  functions it imports from Newton's articulation/math/semi-implicit modules.
- Rigid contact response: the unchanged `eval_body_contact` entry point and
  its `_eval_body_contact`/Warp Huber-norm closure from
  `newton/_src/solvers/semi_implicit/kernels_contact.py`, statically exported
  into `kernels/contacts.inl`. The exporter registers that original function
  in an isolated module, excluding particle/hash-grid entry points.
- Warp support: the transitive quoted-header closure of
  `warp/native/builtin.h` and `deterministic.h` (38 headers).

The source licenses are in `vendor/`, with third-party notices retained in
their headers. The generated Newton code and Warp are Apache-2.0. SVD support
also retains its MIT notice; NanoVDB retains its OpenVDB notice. These support
headers are present because Warp's builtins include them; the package does
not expose those additional simulation features.

## Regeneration

In an isolated Python environment, install the pinned Warp wheel and Newton's
Python dependencies, check out both revisions above, then run:

```text
python packages/newton/tools/export.py --newton-root <newton checkout> --warp-root <warp checkout>
```

Python, CUDA, Warp DLLs, and a C++ runtime are not game dependencies. Tide builds
the checked-in C++ source with its ordinary no-runtime C++ flags. The generated
module uses `block_dim=1`, `enable_backward=False`, `fuse_fp=False`,
`fast_math=False`, and generic CPU compiler flags. This preserves the upstream
ordered scalar loops and avoids the Warp CPU cooperative-block runtime.

`UPSTREAM.sha256` records the resulting generated source and patched support
headers. Regeneration rejects mismatched source revisions and patch contexts.

## Tide changes

1. `builtin.h` excludes WebAssembly from the Clang `_Float16` implementation.
   The upstream fallback declarations remain; the selected float32 kernels do
   not use half conversion.
2. `mat.h` scalar `muladd` specializations use separate multiplication and
   addition. Upstream's explicit `fmaf`/`fma` bypass floating-point contraction
   flags, so flags alone do not meet Tide's arithmetic contract.
3. `math_bindings.h` and `math.c` route standalone CRT transcendental calls to
   Tide's deterministic float32/float64 math. Basic square root remains IEEE
   square root. Every target uses the same modified arithmetic.
4. Generated source location comments use upstream-relative paths. The
   generated include has diagnostic guards, private support-header paths and
   translation-unit-local CPU launch wrappers.
5. `crt.h` uses the system C integer typedefs. The standalone typedefs otherwise
   conflict with LP64 platforms, where `int64_t` is `long`.
6. Generated temporaries are value-initialized. Warp emits unused loop-carried
   copies of indices after possibly empty loops; initialization prevents those
   copies from reading indeterminate values. Defined solver arithmetic and
   control flow are unchanged.

These changes can change results relative to stock Newton. A policy trained
with this package must be evaluated with this same numerical build. Stock
Newton CUDA equivalence is not an established contract.

## State and solver profile

The package reconstructs model arrays and zeroed solver scratch on each call.
Mass matrices are recomputed every substep, with tile GEMM, differentiability,
mimics, tendons and solver caches disabled. Joint coordinates, free-root poses,
velocities, controls and external forces live in ordinary Tide values. No native
allocation or array descriptor survives the call.

The collision profile is compound spheres and static infinite planes. The
adapter evaluates the analytic formulas from upstream
`geometry/collision_primitive.py` in sorted collider-entity pair order, with
the same +X normal for coincident sphere centers. Zero-radius spheres are
rejected. Tide planes use local +Y as their positive exterior; upstream's
plane convention is local +Z. A plane represents the boundary of a solid
negative half-space, so spheres below it are pushed toward its positive side.
Body/local shape transforms are composed after fresh forward kinematics on
every substep. Contact midpoint/separation/normal are converted to body-frame
core anchors and effective-radius thickness using `sim/collide.py`'s writer
formulas, then the actual upstream force kernel evaluates each pair serially.
Materials are averaged, adhesion and surface velocity are zero, and
`friction_smoothing` is fixed at 0.01. The force kernel uses penalty stiffness
and approach damping, with velocity-dependent Huber-regularized friction; it
does not implement a static-friction constraint or contact history. Its scalar
friction limiter does not strictly bound the final Euclidean vector magnitude
by Coulomb's limit because its Huber value differs from vector length.

Exclusion groups are nonnegative; negative values are rejected. Same-body,
matching nonzero exclusion-group and static/static pairs are skipped.
A transient zeroed per-pair wrench buffer permits exact per-collider
force readback, including static reactions, before ordered body accumulation.
Readback is the mean over all successful substeps, with zero for no contact.
No contact, matching, warm-start or native solver state survives the call.
The no-collider and noncontacting paths retain the original dynamics kernels.

The general upstream collision pipeline, meshes, boxes, queries, soft bodies,
importers and GPU execution remain outside this port's current profile. Adding
them requires extending the state/portability boundary and the native/WASM
parity tests; selecting a different solver on one target would not preserve
the package's simulation contract.
