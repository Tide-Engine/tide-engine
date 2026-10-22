# Pinned MicroDuck walk-model conversion

Source: [Pollen Robotics microduck_rl](https://github.com/pollen-robotics/microduck_rl),
revision `b02fdea4902c2b93b7d34a83a6f05da4ececb7d3`. Copyright 2026 Pollen
Robotics. The files in `upstream/` are byte-identical originals from that
revision, without edits or newline conversion:

| Local file | Original path | SHA-256 |
| --- | --- | --- |
| `upstream/robot_walk.xml` | `src/mjlab_microduck/robot/microduck/robot_walk.xml` | `80fc4424ed71e430ec4919110dfdb5c714dc1c4e1138e1832a16865cf31eb8c8` |
| `upstream/scene_walk.xml` | `src/mjlab_microduck/robot/microduck/scene_walk.xml` | `6e23146c1a5025473b55ec94cbe37b08104ca388b7eb005ea277f8230736c8be` |
| `upstream/LICENSE` | `LICENSE` | `2d3fd9ffffa70a4307498ec630b14a6b4731e010f8ddda92686eaf65733c2dfe` |

The original MJCF descriptions and the transformed model data are
Apache-2.0; the complete upstream license is retained in `upstream/LICENSE`.
Upstream's pinned [LICENSE-MODELS.md](https://github.com/pollen-robotics/microduck_rl/blob/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/LICENSE-MODELS.md)
explicitly separates XML/configuration/data from its CC BY-NC-SA 4.0 STL
meshes. No upstream STL files are included, read, converted or sampled for
geometry. Original XML references to absent mesh files are retained as
provenance; this converter never opens them.

## Original appearance and collision proxies

The casings, camera/servo details and broad feet in `visuals.tide` are original
procedural geometry. Appearance references are Pollen Robotics'
[official MicroDuck press-kit photos](https://pollen-robotics.com/microduck/press-kit/)
and the body, joint, geom/site frames and material metadata in the
[pinned XML](https://github.com/pollen-robotics/microduck_rl/blob/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/src/mjlab_microduck/robot/microduck/robot_walk.xml).
Photo-estimated proportions are approximate, not measurements from exact CAD.
No photographs or photo textures are distributed with the example. These
references document how the appearance was authored; the XML/data license
does not describe the press photographs or the separate STL assets.

The 61 sphere proxies per duck in `sandbox.tide` are separately authored
estimates covering all 15 bodies, based on the original visual geometry and
source link frames. They are not imported collision meshes or measured casing
boundaries. Six marked spheres per foot approximate a 72 × 43 mm sole. Their
original-frame centres are
`(shift + x * 0.025, -0.0138146, -0.0140852 + side * 0.011)` metres, with
`x = -1, 0, 1`, `side = -1, 1`, radius `0.010` m and `shift = -0.005` m
on the left foot or `0.005` m on the right. Their bottom tangencies retain
the XML foot site's original-frame Y coordinate `-0.0238146` m. Other spheres
cover the ankle, leg, trunk, neck and head visuals approximately.

The game converts each proxy into the principal body frame using the
relationship below. It adds four original loose sphere props and a static
infinite plane at world Y=0; the drawn floor covers only 4 × 3 m. All game
colliders use stiffness 5600, damping 3, friction damping 1 and friction 0.8.
The Newton package evaluates its actual upstream compliant rigid-contact
kernel, with regularized friction and no static-friction constraint, at four
substeps per 200 Hz game tick. Matching groups suppress each duck's internal
collisions. These contact proxies, original visuals and gameplay material
choices leave the source pin, authored mass/tensors and converter output
unchanged. No STL files are used for either appearance or collision geometry.

## Regeneration and validation

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

Separate validation of the current collision integration passes all 33
backend runtime tests on native/WASI debug and release, including nine
independent stock Newton contact-reference samples checking state and forces.
All 40 bundled upstream hashes and 46 installed source/header files match
the checkout. Installed CLI native/web release builds and final-profile
WebGL/WebGPU interaction checks pass. The [example README](README.md) records
the sphere-envelope clearance and unassisted-topple measurements. These
integration checks leave the XML/model pins and conversion unchanged and do
not establish physical fidelity or sim-to-real transfer.

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
or BAM actuator behavior is implemented by this conversion. The sandbox's
original compound sphere geometry and package contacts are separate from this
unchanged conversion. Its mouse pulls and upright/heading/lane assistance are
ordinary game forces and torques. Current upstream
Python training configures different actuators; the XML settings here are a
specific baseline, not a reproduction of that training environment.

Unknown XML nodes/attributes, alternate orientation representations, unsupported
joint types, extra joints/free roots, active equality constraints, unexpected
default classes/actuator mappings, nonfinite values, invalid axes and invalid
mass/inertia are rejected. All source bytes must also match the pinned hashes.
