# NVIDIA Newton sources

This package uses NVIDIA Newton, the Linux Foundation robotics simulator at
<https://github.com/newton-physics/newton>. It is unrelated to Newton Game Dynamics.

The selected solver is Newton's float32 Featherstone implementation. Its actual
upstream kernels are generated into `kernels/featherstone.inl`; `runtime.cpp`
supplies model arrays and invokes the kernels in the upstream solver's order.
The deterministic adapter also supplies optional coupled friction, transient
free-root frame centering and the scoped block multiplication described below;
the generated upstream dynamics kernels remain the backend.

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
After removing unused scalar tile guards, full regeneration with both pinned
checkouts and the pinned Warp wheel reproduced all 40 manifest files byte for
byte. At that historical regeneration milestone, the 33 existing runtime and
14 joint-friction tests passed on native/WASM with the then-current contact
and friction hashes. Later frame centering intentionally changes two pins;
these earlier results are not current full-suite validation.
A historical native stress probe with eight Windows threads also matched independent
serial full-record histories over 15,360 overlapping `nt_step` calls, including
contacts, joint friction, free/branched assemblies and snapshot replay.

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
7. Scalar CPU dispatchers omit Warp's unused `tile_shared_storage_t` guard
   and conditional `shared_tile_storage` assignment. The guard constructor
   otherwise touches function-static Warp tile scratch even though this
   `block_dim=1` profile emits no tile operations, introducing shared state
   during concurrent independent calls. The exporter accepts only the exact
   pinned guard prologue in every CPU dispatcher and rejects any remaining
   tile identifier in code or a non-scalar block dimension. Vendor headers,
   numerical kernel bodies and dispatch order are unchanged.
8. The adapter anchors each free root's virtual parent frame at that body's
   current world-space origin, initializing its translation coordinates to
   zero during model construction. This keeps small rotational inertia from
   being lost to float cancellation in Cholesky factorization when world-origin
   coordinates are large. Existing public body-origin poses, COM velocities
   and world-space force conventions are preserved through the existing
   conversions. The frame and arrays are transient; no API, serialized state
   or persistent solver state is added.
9. The adapter's six-column mass-times-Jacobian calculation skips structurally
   zero off-block mass entries, keeping increasing accumulation order within
   each nonzero block. Nonfinite inputs retain the dense fallback. A paired
   native benchmark on a frozen previous-control idle scene measured
   3.381 to 1.720 ms/tick; all three output arrays were byte-identical for
   1,020 ticks. This is a bounded finite-input optimization check, not a claim
   of universal equivalence, WASI speedup or browser frame rate.
10. The adapter evaluates JᵀP using four independent scalar accumulators for
    consecutive output columns, then the unchanged scalar tail. Every product
    remains in the original increasing accumulation order. No terms are
    skipped, no FMA/reassociation is introduced, and finite/publish checks are
    unchanged. The local restrict pointers reference distinct J/P/H buffers.
    A paired native comparison against the frozen `fd6f4839` reference matches
    status and all three output record arrays for 1,020 calls. One captured
    35-body/28-joint/353-collider snapshot also matches native and WASI SIMD
    record bytes, hash `d267a59f35210b5f`. This is bounded evidence, not an
    all-input or full WASI-history equivalence claim.

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
For the default `ContactFriction.Regularized` path, materials are averaged,
adhesion and surface velocity are zero, and `friction_smoothing` is fixed at
0.01. The force kernel uses penalty stiffness and approach damping, with
velocity-dependent Huber-regularized friction. That default path does not
implement static sticking; its scalar friction limiter does not strictly bound
the final Euclidean vector magnitude by Coulomb's limit because its Huber value
differs from vector length. Public friction/default values are unchanged.

Both colliders must select `ContactFriction.Coulomb` for the optional adapter
impulse path; mixed pairs retain Regularized behavior. Coulomb leaves the
upstream compliant normal force and its averaged stiffness/damping unchanged,
disables the force kernel's tangent damping for that pair and uses
`sqrt(muA) * sqrt(muB)` for friction. Joint and two-tangent contact impulses
interleave through the same Newton H/Cholesky L mass solve for 16 fixed sweeps.
The saturated full-rank quadratic block uses a fixed 32-iteration multiplier
bracket; the legacy joint-only path uses eight sweeps. Impulses cold-start each
substep. Finite sweeps permit bounded sticking without a global sticking or
hard-nonpenetration guarantee, and add no hidden contact history.

Exclusion groups are nonnegative; negative values are rejected. Same-body,
matching nonzero exclusion-group and static/static pairs are skipped.
A transient zeroed per-pair wrench buffer permits exact per-collider
force readback, including static reactions, before ordered body accumulation.
Readback is the mean over all successful substeps, with zero for no contact.
No contact, matching, warm-start or native solver state survives the call.
The no-collider and noncontacting paths retain the original dynamics kernels.

## Final runtime and validation scope

Runtime SHA-256
`4f7a91102e1e4cd87bc3906885c02d1bb5e3cbb4578b8cee9495dff14abeb4fa`
contains the centering, block M×J and ordered four-column JᵀP changes.

All 63 backend cases pass on native and WASI in
`build/microduck-run-check/jtp4-backend-{native,wasi}.log`. Three new
regressions fail with the old adapter and pass after centering: distant free
sphere/small rotational inertia, a translated free-root/fixed-child assembly,
and an interaction-launched ball/floor snapshot. The state and contact hashes
were observed on both targets before final pinning as `be6cce7a3d5e1828` and
`445004fca1d80fa4`; their changes are intentional numerical consequences.

The actual full-scene close-duck/ball/grab reproduction fails with the old
adapter at tick 1734, substep index 3. On the `fd6f4839` stability basis,
corrected native/WASI hosts complete
4,000 ticks with hash `4cb7f65b76088097`. Ordinary W with both ducks and balls
active also completes 4,000 ticks with matching hash `658515fe75a71d1d`.
Those basis loaded headless runs measure 1.594 ms/tick native and 3.185 ms/tick
WASI; they are separate from the paired idle-scene optimization comparison.
These results establish bounded regression coverage, not unlimited stability
or calibrated physical fidelity.

The separate paired native JᵀP comparison in
`build/microduck-perf-audit/jtp4/RESULTS.md` measures `nt_step` calls on a frozen
previous-control idle scene: 1.459854 to 1.088080 ms, a 25.47% reduction.
This excludes cloning/comparison overhead and is not a full game-tick or
browser measurement. Do not multiply this result with the earlier block M×J
benchmark to claim a precise cumulative gain. A final 12-second steady WebGL
profile shows 32.08 ms/frame (31 FPS), 2.35 ms/tick and 1.77 ms Newton work,
401 ticks/sec and 12.9 ticks/frame under combined client/server load. Earlier
fd6f and preoptimization frame/tick/Newton snapshots were 45.58/2.74/2.35 and
86.09/5.23/4.83 ms; those observations are not controlled paired benchmarks or
evidence of 60 FPS or fully resolved browser lag.

The full stability-basis eight-case acceptance matrix is in
`build/microduck-run-check/final-test-summary.json`: the full eight-case host
passes 7/8, with all nine emitted hashes matching. The strict D/right-turn
criterion remains a known failure and is explicitly disabled in CTest with
label `known-limitation`;
final-source forward walk/boost checks pass 1/1 per target and preserve the
basis world hashes. `all-boost-jtp4-test-summary.json` records the final
13/13 native/WASI policy cases and 14/14 threaded native with 1,215 callbacks,
including sampled full/delta Shift/reverse/diagonal inputs and actor history.
Shift requests signed 0.5 m/s translation and 1.25 rad/s yaw in all directions;
reverse may fall and boosted turns may stumble. Those controller capability
limits are distinct from the corrected Newton numerical `Unstable` failure.
CMake configuration is
blocked by the unrelated missing `bench/` directory, so no
clean repository-suite CTest pass is claimed.

The general upstream collision pipeline, meshes, boxes, queries, soft bodies,
importers and GPU execution remain outside this port's current profile. Adding
them requires extending the state/portability boundary and the native/WASM
parity tests; selecting a different solver on one target would not preserve
the package's simulation contract.
