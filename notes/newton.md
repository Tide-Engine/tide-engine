# The Newton package

## Featherstone CPU integration and documentation (2026-10-22)

The owner requested NVIDIA Newton as a separate optional package like Box3D,
for robotics and multiplayer/training using the same physics. A separate
training engine is outside that contract. `packages/newton`, dependency name
`newton`, namespace `Newton`, is an experimental first integration; the Tide
API names and first-build behavior below are provisional. This is NVIDIA
Newton, not Newton Game Dynamics. There is no silent Box3D fallback.

The backend runs the actual upstream Featherstone kernels; native/WASI runtime
tests and native/web debug/release Tide fixtures pass as detailed below.
The full native and web debug suites also pass (2,512/2,512 and 2,413/2,413).
Installed-source native/web builds and a bounded desktop arm run pass too.
Physical fidelity remains unvalidated.

### Source and build path

- Newton revision: `ea4899864046dcd5981f8ba0bc3d8c25e65dcb96`.
- Warp revision: `b346b941a4c0cd19c510db67d6506d781464ed5b`, wheel/version
  `1.19.0.dev20261020`.
- `tools/export.py` requires those source revisions and the pinned Warp
  version. It statically exports upstream Featherstone CPU kernels with
  backward generation, fused floating-point operations and fast math disabled,
  and block dimension 1. Checked-in source is `kernels/featherstone.inl`;
  transitive Warp native headers and upstream/license notices are in `vendor/`.
  `UPSTREAM.sha256` records generated-source/header hashes.
- Game builds compile that same scalar CPU source on native and WASM. Python
  and Warp are regeneration-time dependencies only; there is no game Python,
  JIT, CUDA or C++ runtime dependency. The package's C++ is a source port of
  generated CPU kernels, not a second solver implementation.
- `math_bindings.h`/`math.c` route kernel transcendental operations, including
  revolute trigonometry, to Tide's shared float/double math. Builds disable FMA
  contraction/fast math; the selected solver path's explicit `fmaf`/`fma`
  scalar `muladd` specializations are replaced with separate multiplication
  and addition. Unused vendored intersection helpers are outside that path.
- Audited export patches exclude WebAssembly from Warp's Clang native-half
  guard, make CPU launch wrappers translation-unit-local, and reuse system
  `stdint` typedefs to avoid LP64 conflicts. Generated primal temporaries are
  value-initialized: Warp's dead loop-carried copies after possibly empty loops
  can otherwise read uninitialized values. Defined solver arithmetic/control
  flow is unchanged. `UPSTREAM.md` records patch details and regeneration checks.

### State and API contracts

- `Body`: free dynamic or static roots; world-space body-origin pose, world
  centre-of-mass linear velocity and angular velocity; authored mass, body-space
  diagonal inertia about an authored local centre of mass; world force/torque.
  Dynamic mass and inertia must be positive and finite. Static roots may have
  zero mass. No shape or shape-derived mass properties exist.
- `Joint`: a separate entity connects a body child to a body parent or the
  world (`parent = default`). Fixed, revolute and prismatic are the first
  supported types. Frames are relative to body origins (world space for a
  world parent); the axis is in joint-frame space. Coordinates/velocities are
  radians/radians per second or metres/metres per second. Body poses/speeds
  for jointed children are reconstructed from root and joint state. Writable
  joint coordinates and free-root body poses/speeds are resets.
- Each child has at most one inbound joint; the graph is acyclic. An explicit
  static parent root with a dynamic child is allowed. An explicit joint with
  a static child is rejected as `InvalidJoint`. Missing/stale references fail
  the step rather than deleting a joint. Destroy referencing joints when
  destroying bodies, respecting ordinary tick-end structural application.
- Damping and armature are nonnegative finite joint settings. Optional position
  limits use spring/damper response, not hard positional stops. There are no
  friction or velocity constraints.
- Optional `Actuator` on the joint entity supplies generalized effort: torque
  for revolute, force for prismatic. `effortLimit` is the package clamp before
  Newton control input, not a native solver total-effort constraint. Passive
  damping and limit forces are excluded. `appliedEffort` reports that last
  successful clamped control, not a torque/force sensor; fixed joints report
  no driven effort. `effort` persists until code changes it.
- `[Before(PhysicsStages.Step)]` writes this tick's actions, forces and resets.
  `[After(PhysicsStages.Step)]` reads status and completed results. Physics
  defaults are gravity `(0,-9.81,0)`, four substeps, total duration `Time.dt`.
  Games order against the whole phase, not internal collection/publication
  systems.
- `Physics.status`: `Ok`, `InvalidBody`, `InvalidJoint`, `InvalidTopology`,
  `TooLarge`, `Unstable`. Failure publishes no new body/joint/applied-effort
  results; pending forces/torques remain. They clear only after success and
  are held throughout all substeps. Check status before accepting observations.
- All persistent state is ordinary Tide components, lists and maps. `Physics`
  owns value collection buffers and generation-bearing entity maps. Native
  scratch/model storage is rebuilt per call and retains no pointers or hidden
  state between ticks; no `Resident` arena. BodyState/JointState and internal
  collection/map fields are package implementation records, not game controls.

### Games, robotics policies and demonstrations

Human input, game code and a policy all write ordinary match actions before
the same physics phase and read real joint coordinates/body poses afterwards.
Outside-service answers must enter through normal match input. Episode reset
uses standard Tide world snapshot/restore, with controller/reward state kept
in the match too. There is no separate Newton/training simulation to reset.

`examples/robotics/arm/` is a runnable two-player target-reaching example using
Rooms and owner keyboard/gamepad input. Each `Arm` retains a bounded ring of
128 transitions: tick, pre-step joint observation, action as desired joint
angles, actual clamped effort, next observation and reward. `Drive` captures
pre-step state/actions; `Record` appends only after a successful physics step.
The ring is ordinary match state and participates in restore and rollback.

These are executed-state pre/post-step records, not an exported dataset or
exact human-view observation timing. Human demonstrations needing that timing
must also associate the observation used to choose input with its match-time
application. Pose-only animation is insufficient. Export verified ticks in tick
order to avoid duplicate or superseded prediction samples. No exporter, dataset
format or trainer is implemented.

### Documentation and discovery

`packages/newton/README.md` and `docs/packages/newton.md` describe the scoped
API, failure behavior and unsupported features. Both have an anchored revolute
pendulum and bounded Tide PD example; the site page also shows status-aware
post-step observation. Authored inertia and illustrative gains are explicit;
the runnable arm example and its executed-state recording limits are described.
The package appears in the Packages sidebar, overview, usage/authoring guide,
root README, repository layout and AGENTS notes index. Touched package lists
also retain/include `voice`; no unrelated discovery refactor is intended.

### Validation and remaining work

- All 22 runtime tests pass natively and under WASI. They include one two-link
  stock Newton Python reference model's joint coordinates/velocities and body
  COM state at 1, 8 and 32 ticks within `5e-5` absolute tolerance, analytic
  dynamics, validation atomicity, topology, replay and alternate worlds.
  Runtime hash `92249536c6268981` is pinned on both.
- All six Tide arm fixture tests pass in native debug, native release, web
  debug and web release. Coverage includes replay/alternating worlds,
  snapshot restore, raw packing and delta repair, stale entities, and equality
  across thread counts for 180 ticks. The 200-tick fixture hash is
  `b3a702e67f351594` in all four builds.
- The fixture's actual two-client loopback rollback test passes in those four
  builds for 720 frames at 0.035 s latency, 0.01 s jitter and 0.03 loss. Each
  client executes over 6,000 replay ticks with zero resyncs in the observed
  runs; the test asserts replay activity and zero resyncs.
- Separate fresh-process cross-target checks pass native-to-WASI and
  WASI-to-native, using a 40-tick snapshot followed by 20 continuation ticks.
  The registered join CTest exercises write/read with the same executable.
- Full native debug passes 2,512/2,512 (497.90 s), and full web debug passes
  2,413/2,413 (669.18 s), with the latest exporter patches. The four focused
  Newton CTests pass again in native/web debug/release with unchanged hashes.
  Test sources and join hosts are in `packages/newton/test/`.
- Installed CLI validation passes with clang 23.1.2 and Node 24.16.0. The arm
  builds natively and for web from installed bridge/math/runtime sources,
  without a prebuilt Newton library. All 45 source/header files match the
  checkout. Deliberately breaking the staged `.inl` invalidates both builds;
  restoring its bytes recovers both. A hidden, muted 120-frame desktop run
  captures both arms responding. Minimal native/web games compile zero Newton
  objects; native PDB modules also confirm exclusion. Browser execution of the
  installed arm was not run. Reproduction script, logs and captures are under
  `build/newton-cli/`; the example README documents controls and bounded runs.
- Documentation dependencies were already installed with
  `npm ci --no-audit --no-fund`. The final `npm run build` passes with local
  Node 24.16.0 from `build/tools/node/node-v24.16.0-win-x64`, including
  page/sidebar, link and anchor checks. Existing allowed 404s for unstaged
  game/example pages remain. The three documentation files pass whitespace
  checks, all 12 local links resolve, and scoped `git diff --check` passes.

Other-platform CI, performance
measurements and independent physical-fidelity validation remain outstanding.
Stock Newton reference and analytic checks do not establish realistic robot
behavior, CUDA equivalence or sim-to-real transfer.

Not implemented: collider shapes, collision detection/contacts/contact friction,
collision queries, URDF/MJCF import, CUDA execution, joint friction or velocity
constraints, automatic realistic robots, sensors/noise, trainer or differentiable
training interface. No realistic sim-to-real transfer claim is supported.
