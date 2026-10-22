# Pinned MicroDuck model and collision provenance

Source: [Pollen Robotics microduck_rl](https://github.com/pollen-robotics/microduck_rl),
revision `b02fdea4902c2b93b7d34a83a6f05da4ececb7d3`. Copyright 2026 Pollen
Robotics. The files in `upstream/` are byte-identical originals from that
revision, without edits or newline conversion:

| Local file | Original path | SHA-256 |
| --- | --- | --- |
| `upstream/robot_walk.xml` | `src/mjlab_microduck/robot/microduck/robot_walk.xml` | `80fc4424ed71e430ec4919110dfdb5c714dc1c4e1138e1832a16865cf31eb8c8` |
| `upstream/scene_walk.xml` | `src/mjlab_microduck/robot/microduck/scene_walk.xml` | `6e23146c1a5025473b55ec94cbe37b08104ca388b7eb005ea277f8230736c8be` |
| `upstream/LICENSE` | `LICENSE` | `2d3fd9ffffa70a4307498ec630b14a6b4731e010f8ddda92686eaf65733c2dfe` |

The original MJCF descriptions and the transformed inertial/joint `model.tide` are
Apache-2.0; the complete upstream license is retained in `upstream/LICENSE`.
Upstream's pinned [LICENSE-MODELS.md](https://github.com/pollen-robotics/microduck_rl/blob/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/LICENSE-MODELS.md)
explicitly separates XML/configuration/data from its CC BY-NC-SA 4.0 STL
meshes. The new mesh-derived collision support data is **CC BY-NC-SA 4.0**;
see [`LICENSE-COLLISIONS.md`](LICENSE-COLLISIONS.md) and
[`README-COLLISIONS.md`](README-COLLISIONS.md) for attribution, source hashes,
the frozen data and its exporter. Upstream STL geometry was read, sampled and
converted privately for that data. Raw STL files are not redistributed.
The released neural model and code retain their separate existing licenses.
The XML-only `tools/convert.py` still never opens meshes; its source references
and Apache-2.0 inertial/joint output are distinct from the collision export.

## Original appearance and collision proxies

The learned controller separately vendors the released Apache-2.0 VelStand v8
model at Hugging Face revision `665ae4eb20bfe13bca1ef4ea6d4bb6b4a096e448`.
`policy/README.md` records its original ONNX checksum, model card, manifest,
exporter, graph and independent ONNX Runtime comparisons. The weights are
unchanged. `policy.tide` implements the 61-slot input and 50 Hz inference,
with target scaling/filtering from hardware source
`942e58e3aa6dbb28e7037203fff11866122c7567`. BAM M6 parameters follow the
training dependency `62bd8ce12154340be97e06f7f41a0ca8f116d967`. Rounded policy
HOME is kept separate from precise model STAND. This port omits backlash,
sensor/actuator delay and the daemon's separate limp/pose recovery wrapper.
Inference agreement does not establish locomotion or recovery transfer. The
default game still uses this hardware/BAM profile and the released weights;
the scratch official CPU no-BAM rehearsal is a separate diagnostic profile.

The integrated native/WASI probes of the fixed 174-collider model preserve W/S travel
and pass seeded prone, side and supine recoveries. **D/right turning remains
weak; arbitrary-fall recovery and reliable directional turning are unvalidated.**
The [example README](README.md) gives the measured results and limits. Seeded
success is not proof of arbitrary-fall recovery, hardware fidelity or exact
training identity. Earlier no-BAM/61-sphere recovery failures describe the
previous model. Geometry, self-contact, friction, limits and actuator/delay
behavior remain differences between the game and the upstream training plant.

The casings, camera/servo details and broad feet in `visuals.tide` are original
procedural geometry. Appearance references are Pollen Robotics'
[official MicroDuck press-kit photos](https://pollen-robotics.com/microduck/press-kit/)
and the body, joint, geom/site frames and material metadata in the
[pinned XML](https://github.com/pollen-robotics/microduck_rl/blob/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/src/mjlab_microduck/robot/microduck/robot_walk.xml).
Photo-estimated proportions are approximate, not measurements from exact CAD.
No photographs or photo textures are distributed with the example. These
references document how the appearance was authored; the XML/data license
does not describe the press photographs or the separate STL assets.

The fixed model in `collisions.tide` contains 174 spheres per
duck: 131 mesh-derived support spheres on 13 nonfoot bodies, 26 source-inscribed
internal cores, 16 original foot-body proxies, then one appended procedural
trunk-casing core. Including the floor and balls, the scene has 353 colliders.
One constant model is used for walking, recovery and grabbing.
The sampled nonfoot support error of the mesh-derived subset is approximately
5.5 mm (maximum 5.542759 mm); it is not
an exact source hull or a guarantee over every surface direction. The cores
add interior volume, with two per nonfoot body at least 2 mm inward of the
baseline support envelope. Those 26 cores retain their source-inscribed
certificates. The additional body-0 core at `(.004, 0, -.006)` m, radius
`.0115` m, approximates the visible black case. It is not source-hull-inscribed
and expands selected support by 6.67484 mm; it is outside the source support
certificate. The targeted private pick/prop regression fails three assertions
on 173 spheres and passes on 174, including a point 2.5 mm inside the case and
the ray hit. This is a narrow static-coverage check; it does not cover every
rendered surface. Some visible trunk/head pointer positions can still miss.
The wider interaction checks are listed below.
Source hashes and fit provenance identify the fixed data;
they do not establish exact training collision/material identity. The 17 named
training contact-dimension overrides must not be generalized to all 70 robot
collision geoms.

Feet retain the original approximate 16 proxies, including 12 soles. Six marked
spheres per foot approximate a 72 × 43 mm sole. Their
original-frame centres are
`(shift + x * 0.025, -0.0138146, -0.0140852 + side * 0.011)` metres, with
`x = -1, 0, 1`, `side = -1, 1`, radius `0.010` m and `shift = -0.005` m
on the left foot or `0.005` m on the right. Their bottom tangencies retain
the XML foot site's original-frame Y coordinate `-0.0238146` m. The four other
foot-body spheres retain their earlier approximate ankle/foot coverage.

The game converts each proxy into the principal body frame using the
relationship below. It adds four original loose sphere props and a static
infinite plane at world Y=0; the drawn floor covers only 4 × 3 m. All game
colliders use stiffness 5600, damping 3, friction damping 1 and explicitly opt
into `ContactFriction.Coulomb`. Soles, floor and balls use friction 0.8;
nonsole duck spheres use 0. The package's public default remains
`ContactFriction.Regularized`. Both shapes must opt into Coulomb; its pair
coefficient is the geometric mean. The upstream compliant normal response is
unchanged. Joint and contact friction impulses share Newton's H/L mass solve
in 16 fixed interleaved sweeps, with a fixed 32-iteration quadratic contact
block bracket and no persistent solver state. The joint-only path retains
eight sweeps. There are four substeps per 200 Hz game tick. Matching groups
suppress each duck's internal collisions. These contact proxies, original visuals and gameplay material
choices leave the source pin, authored mass/tensors and converter output
unchanged. Rendered appearance remains original procedural/photo-based geometry;
collision support data is separately derived from upstream STL geometry.

Game joint stops use compliant gains 55/0.6, rather than clamping coordinates.
Normal forward/reverse commands use 0.4 m/s and yaw requests use 1 rad/s;
either held Shift boosts signed translation to 0.5 m/s and yaw to 1.25 rad/s,
including reverse and diagonals. The 0.5 reverse trial fell, and boosted turns
may stumble: these are policy requests, not guaranteed stable/actual speeds,
fast-forward or artificial forces. Learned turning uses `turn = -drive`:
A/left now produces the passing negative-yaw turn, while D/right is weak.
Manual drive/trim behavior is unchanged. Gearbox friction uses preceding
successful applied motor effort and joint load; voltage sag uses raw motor
history before the separate effort ceiling. Reset clears those histories and
joint effort/load/applied-effort readback before controls run on the reset tick.
These are game/runtime choices, not edits to the source XML, converted model,
policy weights or a claim of equivalent MuJoCo/training dynamics.

## Frozen collision-data export

`collision_spheres.json` is the frozen numeric source for `collisions.tide`.
`collision_provenance.json` records pinned XML/STL source hashes, attribution,
fit settings and tested record order; `collision_certificates.json` records
geometry checks and their scope. Keep the frozen decimal values and record
order: 131 nonfoot support spheres, 26 source-inscribed cores, 16 original foot
proxies, then the procedural visual-casing core.
Those values preserve the private probe's tested float32 geometry; re-exporting
higher-precision research coordinates is a different numeric artifact.

The separate standard-library exporter performs no fitting or simulation:

```powershell
python -B examples/robotics/microduck/tools/export_collisions.py
python -B examples/robotics/microduck/tools/export_collisions.py --check
```

It verifies the frozen JSON hash/schema, finite values, positive radii, body
coverage and sole assignments, and emits deterministic source. It needs no
STL, MuJoCo, NumPy, simulator, network or build cache. Full fit regeneration is
a separate private research workflow tied to the recorded assets and numeric
environment. Ordinary game builds compile the checked-in collision data and
need neither exporter nor private meshes. The licenses and notices beside
the data identify its CC BY-NC-SA 4.0 provenance separately from code/XML.

## XML model regeneration and validation

From the repository root, with `TIDE_HIDDEN=1` in the agent shell:

```powershell
python -B examples/robotics/microduck/tools/convert.py
python -B examples/robotics/microduck/tools/convert.py --check
```

Python's standard library is sufficient. No NumPy, MuJoCo, upstream Python
scripts, network access or mesh files are needed. `--source-dir` can select
another directory containing the same three pinned input files. The tool
checks all input hashes before conversion. `--check` repeats source/model
validation and compares both generated files byte-for-byte, without writing
them. Modified source requires an intentional new pin and converter review;
this is a scoped conversion, not a supported general MJCF importer.

Generated files are `model.tide` and `test/model_expected.h`. The C header is
an original-MJCF reference: authored mass/COM/full tensor, original local body
and hinge frames, and zero/STAND body-origin poses. Full tensors use
`xx, yy, zz, xy, xz, yz`; quaternions use `x, y, z, w`. Reference world poses
are relative to the original trunk origin/orientation, with no root offset or
Tide up-axis rotation. These source-only references do not use the generated
principal-axis records and do not provide independent dynamics validation.

Local conversion validation on Windows passed with Python 3.11.15, 3.13.1
and 3.14.4 producing byte-identical outputs; Python 3.14 also passed with
isolated mode and site packages disabled (`-I -S`). Both checkout-built
native/web Tide compilers transpiled the model, and strict C17 compilation
passed natively and for WASM. A scratch native world verified all 2,100 bytes
of the 15 emitted Tide records against the converter's float32 values.
Negative checks rejected stale output, changed input bytes, unsupported XML
features and invalid inertia. Nonzero hinge-anchor checks also passed.
Independent review found no issues; its separate 258-pose FK check stayed
within `8.08e-9` m and `1.50e-7` rotation-matrix element error. Cross-platform
regeneration and dynamic simulation validation are outside these checks.

The fd6f stability-basis native/WASI hosts both pass 7/8, with all nine emitted
final whole-world hashes matching, including the repeated walk baseline and
known D/right failure. `build/microduck-run-check/final-test-summary.json`
records W/S travel of 0.786932/0.863201 m, and Shift+W travel of 1.155245 m:
0.368313 m (46.8%) above W, with 14/14 loaded cycles. Prone/side/supine one-second
upright dwell confirms at 1.340/2.115/1.905 s, each with final-2s upright 100%.
A/left reaches -3.316523 rad; D/right reaches only +0.196558 rad and fails
the threshold. The fully fitted 150-sphere alternative failed walking and is
not the selected model. No random-fall reliability or sim-to-real claim follows.

The right-turn CTest is explicitly disabled with label `known-limitation`, while
the seven supported cases register normally. Its strict 0.25 rad assertion still
runs directly in the target/full host, which measures 7/8. The missing unrelated
`bench/` directory blocks CMake configuration; registration inspection and
installed-host evidence are not a clean full-suite CTest pass.

Final runtime SHA-256 prefix `4f7a9110` passes 63 runtime tests on native
and WASI, including three new stability regressions. Installed CLI native
and web builds pass. All-direction boost policy checks pass 13 common tests on
both targets and 14 on the threaded native host, including 1,215 actual worker callbacks; all four
cadence-residue replay hashes match. Fresh-process joins in both native/WASI
directions produce byte-identical 128,254-byte files (SHA-256
`570cc830abd1ca6f0170f31a2d149060cc0027d321791c635e553688a31347fe`).
Browser W/Shift+W/A/R and F8/F7 checks pass without errors, with screenshots
inspected. A final 12-second steady WebGL observation shows 32.08 ms/frame
(31 FPS), 2.35 ms/tick and 1.77 ms Newton work, 401 ticks/sec and 12.9 ticks/frame
under combined client/server load. These snapshots are not a paired benchmark
or evidence of 60 FPS or fully resolved browser lag. The basis's 23 native
manual behavior cases pass; both target pin reruns pass
1/1 for observed hash `90ed0aaf9bb442c8`. The full WASI 24-case suite has not
been repeated on the final source; older complete runs and grab captures are historical.

The adapter centers transient virtual free-joint parent frames at each body's
current origin, with zero initial translation coordinates, preserving public
COM/world conventions while avoiding distant-origin Cholesky cancellation.
On the fd6f stability basis, corrected close-duck/ball/grab and ordinary
full-scene W runs complete 4,000 native/WASI ticks with matching hashes `4cb7f65b76088097` and
`658515fe75a71d1d`. A separate paired native block-multiply benchmark on a
frozen previous-control idle scene improves 3.38 to 1.72 ms/tick, with three
output arrays byte-identical for 1,020 ticks. Basis loaded headless W timings
are 1.594 ms/tick native and 3.185 ms/tick WASI, not browser performance.
The package's `UPSTREAM.md` describes these adapter changes and changed pins.

Final-source walk/Shift+W checks pass 1/1 per target, preserving basis hashes
`af41801b0af94572` / `dc74c754ef1b6356`; the current controls/replay summary is
`build/microduck-run-check/all-boost-jtp4-test-summary.json`. The ordered
four-column JᵀP optimization retains every term, tail and finite check.
A paired native `nt_step` comparison measures 1.459854 to 1.088080 ms/call
(25.47%) with all record bytes/status equal over 1,020 calls. One captured
35/28/353 scene snapshot matches native/WASI SIMD, hash `d267a59f35210b5f`.
This bounded result is separate from the earlier block benchmark and must not
be multiplied into a precise cumulative or browser performance claim.
Reverse boost falling or boosted turns stumbling is a controller capability
limit, distinct from the corrected Newton numerical `Unstable` error.

Earlier 33-case regularized-contact reference tests, installed-source/hash
checks, assisted sole-envelope clearance/topple measurements and full-suite
counts are historical evidence for the preceding manual sandbox. They are not
a full-suite validation of the new model. Current measured results and the
remaining checks are in the [example README](README.md).
These checks leave XML/model pins and conversion unchanged and do not establish
physical fidelity or sim-to-real transfer.

## Preserved model and interface

The model is a tree of 15 bodies and 14 one-DOF hinges in original depth-first
order: trunk, five left-leg bodies, four neck/head bodies, five right-leg
bodies. The incoming hinge of body `i > 0` has joint index `i - 1`. Root
`parent` is `-1`. There are no active loop constraints in this source.

`DuckModel()` returns `List<DuckBodyDefinition>` with fields in this order:

```csharp
int parent;
float mass;
float3 inertia;
float3 centerOfMass;
quaternion principalRotation;
float3 parentPosition;
quaternion parentRotation;
float3 childPosition;
quaternion childRotation;
float3 axis;
float lower;
float upper;
float home;
float damping;
float armature;
float effortLimit;
```

`DUCK_BODY_COUNT = 15`, `DUCK_JOINT_COUNT = 14`, `string DuckJointName(int
jointIndex)` and `string DuckBodyName(int bodyIndex)` complete the interface.
The name helpers return empty text for an invalid index. Tide spells its
textual value type `string`. Incoming-joint fields on the root are unused.

Each body's authored mass and complete symmetric inertia tensor about the
authored COM are preserved, including tiny products of inertia and their
mirrored signs. No shape-derived mass approximation or parallel-axis shift
is applied. A fixed-order cyclic Jacobi eigensolver runs 32 sweeps in
`(0,1), (0,2), (1,2)` order. Eigenvalues are sorted ascending; eigenvector
signs are canonicalized by their largest absolute component (XYZ tie order),
then the final column is flipped if needed to make the basis right-handed.
All eigenvalues must remain positive, and basis rotations must be proper.
Dot products accumulate explicitly from left to right, including quaternion
normalization; they do not depend on Python's version-dependent float `sum`.

Let `P_i` be `principalRotation`, mapping the **new principal-body local**
basis into the **original MJCF body local** basis. Only the basis rotates;
the origin stays at the same point:

```text
I_original = P_i * diag(inertia_i) * transpose(P_i)
COM_principal = transpose(P_i) * COM_original
```

Body and geom/site/camera source quaternions are normalized before use;
MJCF WXYZ is converted to Tide XYZW. Quaternions have a canonical sign and
generated decimal constants round-trip to their stored float32 values.

For original parent-to-child transform `T_pc` and the hinge position `j`
in original child-local coordinates, the Newton joint frames are:

```text
parentFrame = inverse(P_parent) * T_pc * Translate(j)
childFrame = inverse(P_child) * Translate(j)
```

The original hinge axis remains unchanged in this joint frame, as do the
joint coordinate/sign, bounds and home position. `home` comes from the
active `STAND` key's `qpos` in `scene_walk.xml`, skipping seven free-root
coordinates; it does not use the commented older STAND or rounded Python
HOME settings. All 14 hinges use source `chosen_actuator` damping `0.053`,
armature `0.0018`, and actuator effort limit `0.96` Nm.

For visual geometry, sites or cameras authored in an original local frame
`T_visual`, the matching new local frame is `inverse(P_i) * T_visual`.
Equivalently, reconstruct the original body world rotation before drawing:

```csharp
var originalRotation = Math.Mul(body.rotation, Math.Inverse(def.principalRotation));
var visualPosition = body.position + Math.Rotate(originalRotation, originalLocalPosition);
var visualRotation = Math.Mul(originalRotation, originalLocalRotation);
```

No global axis conversion or root placement is baked into the model records.
The original root body and STAND free-root coordinates both specify the same
`0.12` m root height; they are not two offsets to add. A fixture chooses its
world placement and up-axis conversion. To put the original trunk at rotation
`Q`, initialize the principal trunk at `Q * P_root`. The sandbox keeps
the trunk's authored positive mass/inertia as a free dynamic root, without a
world-parent Fixed joint. Reset reconstructs all body placements from STAND
joint coordinates at that spawn pose before the package evaluates contacts.

## Conversion error bounds and deliberate omissions

The converter checks reconstruction from the actual float32 constants,
using standard-library double arithmetic for the checks. It validates both
zero and source STAND FK and every one of the 83 original geom/site/camera
local frames. Observed maximum errors for the pinned model are:

| Check | Maximum absolute error |
| --- | ---: |
| Full tensor element | `1.49268056e-11` kg m² |
| Full tensor element / largest source tensor element | `1.45325131e-7` |
| Reconstructed local COM coordinate | `2.13271422e-9` m |
| Body-origin FK coordinate, zero/STAND | `5.96817813e-9` m |
| Body/visual rotation matrix element, zero/STAND | `1.65110411e-7` |
| World COM coordinate, zero/STAND | `8.36563471e-9` m |
| Geom/site/camera world coordinate, zero/STAND | `1.36054833e-8` m |

Acceptance bounds are `1e-6` for normalized tensor reconstruction, and
`2e-6` for position/COM and rotation-matrix errors. Stored quaternion rotation
orthogonality/determinant errors must be below `1e-6`. These checks measure
coordinate conversion and float32 constant storage; they do not measure
Newton dynamics, contacts, fidelity, stability or sim-to-real transfer.

The converter excludes mesh/material declarations, collision geoms and floor,
scene lighting/textures, sites/cameras, sensors/noise, position-servo gains/control
ranges and Coulomb `frictionloss = 0.0048` from its generated dynamics records.
It examines sites/cameras/geoms only for frame validation.
The empty equality section creates no loop; equality defaults do not create
constraints. Hinge ranges are retained as data; a fixture chooses whether to
enable Newton's soft limit forces. No mesh, contacts, friction, MuJoCo servo
or BAM actuator behavior is implemented by this XML conversion. The sandbox's
separate compound sphere collision export and package contacts are separate from this
unchanged conversion. Its mouse pulls and upright/heading/lane assistance are
ordinary game forces and torques. Current upstream
Python training configures different actuators; the XML settings here are a
specific baseline, not a reproduction of that training environment.

Unknown XML nodes/attributes, alternate orientation representations, unsupported
joint types, extra joints/free roots, active equality constraints, unexpected
default classes/actuator mappings, nonfinite values, invalid axes and invalid
mass/inertia are rejected. All source bytes must also match the pinned hashes.
