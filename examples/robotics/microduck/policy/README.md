# Pinned MicroDuck VelStand policy

This directory uses Pollen Robotics' official pretrained `velstand.onnx`,
version **v8**, published in `pollen-robotics/microduck-policies`. Its identity
and provenance below were checked against pinned primary sources on
2026-10-22. The policy was trained upstream; it is not a newly trained model.

## Artifact and training provenance

| Item | Pinned value |
| --- | --- |
| Download | [Official `velstand.onnx`][model] |
| Hugging Face revision | `665ae4eb20bfe13bca1ef4ea6d4bb6b4a096e448` |
| Size | **793,705 bytes** |
| SHA-256 | `2fc7f8ee6acd7d0d3f66a99927b5d5a56589aeed02ec714de329b252d5ccf492` |
| Training task | `Mjlab-VelStand-Rough-Backlash-MicroDuck` |
| Weights & Biases run | `pollen-robotics/mjlab_microduck/0045i2si` |
| Checkpoint | `model_2749.pt` |
| Training source | [`3750776761ecfaa5671b36972087cafba45b8527`][training-commit], branch `improve_standup_velstand` |
| Export time declared in manifest | `2026-10-12T00:00:00Z` |
| Policy role | `perpetual`, slot `walk`, entry pose `standing` |
| Manifest contract | schema 2, model API 1; MicroDuck hardware revision 1, XL330 servos, **50 Hz** |

The pinned [manifest][manifest] records the run, checkpoint, training commit
abbreviation `3750776` and `dirty: false`. GitHub resolves that abbreviation
to the full training-source commit above. The later documentation/source pin
is [`b02fdea4902c2b93b7d34a83a6f05da4ececb7d3`][source-commit]. Its
[`docs/velstand_policy.md`][policy-doc] explicitly identifies this run and
checkpoint as the v8 production policy, robot-tested upstream on 2026-10-12.
These are separate training and documentation revisions.

The pinned Hub tree reports the same model size and SHA-256 as its LFS object
ID. The ONNX metadata itself contains `run_path = "None"`; the run attribution
comes from the pinned manifest and upstream policy document.

## License and attribution

Copyright **2026 Pollen Robotics**. The pinned Hub [model card][card] declares
`license: apache-2.0`. The manifest supplies the interface and training
provenance but has **no license field**. The pinned Hub file listing has no
`LICENSE` or `NOTICE` file.

[`LICENSE`](LICENSE) is a byte-identical copy of the complete Apache License
2.0 from the pinned [`microduck_rl` source][source-license], preserving its
Pollen Robotics copyright notice. The license is identical at the full
training commit and the documentation pin. Requests for a root `NOTICE` at
both source commits returned HTTP 404. Keep this attribution and the complete
license with redistributed upstream weights or derived weight data.

## Graph verified from the pinned ONNX bytes

The model declares ONNX IR version 8, default-domain opset 18, and producer
`pytorch` version `2.9.1`. It has one float32 input, `obs` with fixed shape
`[1, 61]`, and one float32 output, `actions` with shape `[1, 14]`. There are
no recurrent state inputs or outputs.

Its nine graph nodes, in order, are:

```text
Sub -> Div -> Gemm -> Elu -> Gemm -> Elu -> Gemm -> Elu -> Gemm
```

The normalizer is already part of the graph:

```text
normalized_obs = (obs - obs_normalizer._mean) / onnx::Div_24
actions = Linear14(ELU(Linear128(ELU(Linear256(ELU(Linear512(normalized_obs)))))))
```

The normalizer mean and divisor are stored float32 `[1, 61]` initializers.
Use that stored divisor directly when reproducing this graph; it is not an
instruction to compute a new normalizer. Do not normalize observations a
second time. The four weight tensors have shapes `[512,61]`, `[256,512]`,
`[128,256]`, `[14,128]`, with corresponding biases. Every `Gemm` has
`alpha=1`, `beta=1`, `transB=1`; every `Elu` has `alpha=1`. The final layer
is linear. The graph contains no input/output clipping or action scaling.

Upstream's pinned [export implementation][export-source] documents the
checkpoint-to-ONNX path as `actor(normalizer(obs))`, using
`runner.export_policy_to_onnx`. This agrees with the inspected graph.
The pinned dependency [normalizer source][normalizer-source] computes
`(x - mean) / (std + 0.01)`; the exported divisor already incorporates that
epsilon. Adding another epsilon would change the published graph.

## Embedded interface metadata and integration observations

The model records these observation groups in order:

```text
base_ang_vel, projected_gravity, joint_pos, joint_vel, actions,
command, head_command, body_command
```

The pinned [source layout][layout-source] specifies these zero-based,
half-open slices. These describe the upstream contract; local packing and
coordinate conversion still need their own validation.

| Slice | Upstream quantity |
| --- | --- |
| `0:3` | Body-frame angular velocity |
| `3:6` | Body-frame projected gravity |
| `6:20` | Joint positions relative to HOME |
| `20:34` | Joint velocities relative to zero default velocity |
| `34:48` | Previous raw policy action |
| `48:51` | True velocity command `[vx, vy, wz]` |
| `51:55` | HOME-relative head commands `[neck_pitch, head_pitch, head_yaw, head_roll]` |
| `55:61` | Body command deltas `[x, y, z, roll, pitch, yaw]` |

Its command names are `twist,head_pose,body_pose`, and its declared
`action_scale` is `1.0`. Joint ordering for positions, velocities and actions
is explicitly embedded in the model:

```text
left_hip_yaw, left_hip_roll, left_hip_pitch, left_knee, left_ankle,
neck_pitch, head_pitch, head_yaw, head_roll,
right_hip_yaw, right_hip_roll, right_hip_pitch, right_knee, right_ankle
```

The upstream [velocity configuration][velocity-source] uses action scale
`1.0`; actions are joint-position offsets in radians added to the configured
HOME. The pinned [robot constants][constants-source] define HOME in this
joint order as:

```text
0, -0.0873, -0.4579, -0.0049, 0.4530, 0.3491, 0.3491, 0, 0,
0, 0.0873, 0.4579, 0.0049, -0.4530
```

The model also embeds `default_joint_pos`, `joint_stiffness` and
`joint_damping` as decimal text. These metadata strings do not implement
actuator physics, and embedded default positions are rounded to three decimal
places. The source HOME above retains the configuration's four decimals.
In particular, the source training task includes rough
terrain, backlash and BAM actuator behavior. Passing an inference comparison
alone would not establish that a different simulator reproduces those dynamics.

The pinned upstream [policy document][policy-doc] supplies several practical
constraints:

- Zero velocity command is trained standing behavior within the same policy.
- v8 takes the **true yaw command**. The document's historical v7 runtime yaw
  remap is explicitly inapplicable to v8; its calibrated anchor remap was a
  training mechanism.
- Body roll/pitch control is trained for standing. Body-height/crouch control
  remains unlearned in the reported upstream evaluation.
- Slow turning in place at 0.3 rad/s remains weak and bursty, despite improved
  tracking from about 0.5 rad/s upward.

These are upstream observations, not validation results for this example.

## Local API

`inference.h` exports a pure C API, also usable from C++:

```c
enum { MD_POLICY_OBSERVATION_COUNT = 61, MD_POLICY_ACTION_COUNT = 14 };
int md_policy_evaluate(const float *observations, float *actions);
```

Supply arrays containing at least those counts. Success returns `1` and writes
all 14 raw outputs. A null pointer, nonfinite observation, or nonfinite
intermediate/output returns `0` and leaves the output array unchanged. Input
and output may overlap. The runtime is stateless and reentrant: weights are
`static const`, workspace lives on the stack, and there is no allocation or
mutable global state. The caller owns observation packing, previous actions,
reset behavior, coordinate conversion, HOME addition, and actuator handling.

`inference.c` executes each graph node separately. Normalization subtracts
the stored mean, then divides by the stored divisor. Each Gemm reduces input
indices in ascending order from `+0.0f`, then adds bias. Products and sums
round separately to float32. ELU uses Tide's deterministic `tide_exp_f` and a
separate subtraction of `1.0f`; it does not call platform `expf`. Every
normalizer operation, product, partial sum, bias addition and activation is
checked for finiteness before the final output commit.

The only runtime source dependency is `engine/src/math.c`, with the existing
`engine/include` headers. Use the same `inference.c` on native and WebAssembly.
Compile with `-fno-fast-math -ffp-contract=off` and IEEE-754 binary32 behavior.
The runtime rejects builds defining `__FAST_MATH__` and disables contraction
and reassociation with Clang pragmas. No ONNX Runtime, Python, dynamic model
loading, training, alternate controller or runtime weight conversion is used.

## Regeneration and build

From the repository root, use an isolated Python environment with NumPy and
ONNX for authoring. Validation additionally needs ONNX Runtime. Versions used
here were Python 3.14, NumPy **2.5.4**, ONNX **1.23.2** and ONNX Runtime
**1.31.0**. No global Python packages are needed.

```powershell
$env:TIDE_HIDDEN = '1'
$py = 'build/research-newton/venv/Scripts/python.exe'
& $py -B examples/robotics/microduck/tools/export_policy.py
& $py -B examples/robotics/microduck/tools/export_policy.py --check
& $py -B examples/robotics/microduck/test/policy_oracle.py --check
```

The exporter verifies the byte count and SHA-256 before reading the model.
It accepts only the exact interface, tensor set/shapes, dataflow, opset and
operator attributes described above. Unsupported graphs fail explicitly.
`model_weights.h` contains all **197,896** original float32 values as exact
hexadecimal C literals, with the original `[output,input]` weight ordering.
`--check` regenerates in memory and compares bytes without rewriting files.

The standalone test lives at `packages/newton/test/policy_runtime_test.c`,
outside the recursively discovered game C sources. These PowerShell commands
compile only the policy, deterministic math, and that test with system Clang
and the checkout's existing sysroots; no CMake build tree is needed:

```powershell
$cc = 'C:/Program Files/LLVM/bin/clang.exe'
$scratch = 'build/microduck-policy-inference'
New-Item -ItemType Directory -Force $scratch | Out-Null
$sources = @('examples/robotics/microduck/policy/inference.c',
             'engine/src/math.c', 'packages/newton/test/policy_runtime_test.c')
$flags = @('-std=c17', '-O2', '-fno-fast-math', '-ffp-contract=off', '-Iengine/include')
& $cc @flags --target=x86_64-w64-windows-gnu `
  --sysroot=build/newton-install/mingw/sysroot @sources -fuse-ld=lld -nodefaultlibs `
  build/newton-install/mingw/libclang_rt.builtins-x86_64.a `
  -lmingw32 -lmingwex -lmoldname -lmsvcrt -lkernel32 -o "$scratch/policy_runtime_test.exe"
& $cc @flags --target=wasm32-wasip1 `
  --sysroot=build/research-newton/wasi-sdk/wasi-sysroot-24.0 @sources -nodefaultlibs `
  -lc build/newton-install/wasi/libclang_rt.builtins.a '-Wl,-z,stack-size=262144' `
  -o "$scratch/policy_runtime_test.wasm"
& $py -B examples/robotics/microduck/test/policy_oracle.py --check `
  --native-executable "$scratch/policy_runtime_test.exe" `
  --wasi-module "$scratch/policy_runtime_test.wasm"
```

The full WASI research sysroot supplies the command entry object; the staged
WASI sysroot contains only the reactor entry object. The cross-target checker
uses Node's `node:wasi` support and temporary files to run the same C test and
compare the complete output streams. For an additional 1,024 random ORT
comparisons, build `inference.c` and `engine/src/math.c` as a native shared
library and pass its path to `policy_oracle.py --check --library PATH`.
Regenerate `policy_expected.h` with `policy_oracle.py` (without `--check`)
only when intentionally updating the independent oracle fixtures.

## Validation

The offline oracle uses ONNX Runtime's sequential CPU provider with graph
optimizations disabled, reading the original pinned ONNX rather than generated
C weights. The 73 checked-in fixtures cover zero, standing, pitch/roll,
inverted gravity, previous actions, commands, joint motion, subnormals, and 64
seeded random observations. A separate native shared-library comparison adds
1,024 random observations (1,097 total). Observed maximum absolute action
error was **3.57627869e-6**; the fixed-fixture maximum was **1.60932541e-6**.
The test tolerance is `2e-4 + 2e-5 * abs(reference)` per action; ORT's matrix
reduction/exp implementation need not be bit-identical to the deterministic C.

The standalone C test also checks null arguments, NaN and both infinities in
each of the 61 input slots, finite normalization/Gemm overflow, unchanged
outputs on failure, overlapping input/output, and A/B/A calls. All passed.

Before pinning its replay hash, native and WASI Clang 23.1.2 builds were
compared over every output of the 73 fixtures and 512 synthetic steps feeding
the prior raw action back into subsequent observations. Both emitted the same
**32,760 bytes**, with SHA-256
`c1f56b3fc34b72aa9ff804d9b6620b03618e9c0fecbb2001930507fdbb1a1c74`.
The C test pins the resulting FNV-1a-64 hash **`1780e8e7b6265eef`**.
Native `-O0` and `-O2` output streams were identical as well.

These tests validate the inference implementation and a synthetic observation
replay. They do not validate simulator dynamics, walking performance, physical
recovery, or hardware transfer; those require separate integration tests.

## Fetched source checksums

SHA-256 values below are for the exact downloaded response bytes, before any
newline conversion. Raw files are the reproducible evidence; API response
hashes additionally record the metadata response observed on 2026-10-22.
No model or metadata download requires an unpinned `main` URL.

| Source | Bytes | SHA-256 |
| --- | ---: | --- |
| [Hub model card][card] | 75 | `491b39e6bd8ddb2840c7d12a39650331ef87412aa46d834980d8076504f5b8e7` |
| [Hub manifest][manifest] | 2,847 | `c566390738dd90a54c1e2de829aaab0e904c636949e2c2c6158ee7bbb889b818` |
| [Hub recursive file listing][hub-tree] | 3,254 | `af22bec617b2f23d7ff4c56fb3996727adf0e54a374d70de65507453ed6ef058` |
| [Training commit API record][training-api] | 17,840 | `7c070599108f18c255bde39469488b224fd26588bd4c7708ba54d2222b0c27fc` |
| [Documentation commit API record][source-api] | 50,267 | `e8184eadfff4fb18bce0d9519f695f6b1ba51ae6520886a279c4fc6526f6e459` |
| [VelStand policy document][policy-doc] | 13,305 | `5053cf4ddafff4ceb4e2eb743bb12f9508db15e1d3693a8b1191bdfa0b774c5b` |
| [ONNX export source][export-source] | 12,340 | `ef4d851eee70ce0d1c896a0be14fd4a936a0d17e83649dfd572a5282ffcdf5b9` |
| [Source license][source-license] (also local `LICENSE`) | 11,351 | `2d3fd9ffffa70a4307498ec630b14a6b4731e010f8ddda92686eaf65733c2dfe` |
| [Training-commit license][training-license] | 11,351 | `2d3fd9ffffa70a4307498ec630b14a6b4731e010f8ddda92686eaf65733c2dfe` |

Additional primary source files used for the interface cross-check:

| Source | SHA-256 |
| --- | --- |
| [Observation layout][layout-source] | `e37980389773f2f87b8f376df449af4d70e0c983744459f9a35a3ee7b686186b` |
| [Velocity configuration][velocity-source] | `75a0430b814ac7495eb237ab806fbd4dd10309b9fd3027ac3eee3c2e3c5a944a` |
| [Robot HOME constants][constants-source] | `0e7097afe833ac1259b197f9854d01c1666be03ac0c9b090233b4fbc7619eb94` |
| [rsl_rl normalizer][normalizer-source] | `539e1193cebcc472585cd3df100946fb8d788c4aa0f9f5cbcb3387a64d312926` |

[model]: https://huggingface.co/pollen-robotics/microduck-policies/resolve/665ae4eb20bfe13bca1ef4ea6d4bb6b4a096e448/velstand.onnx
[card]: https://huggingface.co/pollen-robotics/microduck-policies/resolve/665ae4eb20bfe13bca1ef4ea6d4bb6b4a096e448/README.md
[manifest]: https://huggingface.co/pollen-robotics/microduck-policies/resolve/665ae4eb20bfe13bca1ef4ea6d4bb6b4a096e448/manifest.json
[hub-tree]: https://huggingface.co/api/models/pollen-robotics/microduck-policies/tree/665ae4eb20bfe13bca1ef4ea6d4bb6b4a096e448?recursive=true&expand=false
[training-commit]: https://github.com/pollen-robotics/microduck_rl/commit/3750776761ecfaa5671b36972087cafba45b8527
[training-api]: https://api.github.com/repos/pollen-robotics/microduck_rl/commits/3750776761ecfaa5671b36972087cafba45b8527
[source-commit]: https://github.com/pollen-robotics/microduck_rl/commit/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3
[source-api]: https://api.github.com/repos/pollen-robotics/microduck_rl/commits/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3
[policy-doc]: https://raw.githubusercontent.com/pollen-robotics/microduck_rl/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/docs/velstand_policy.md
[export-source]: https://raw.githubusercontent.com/pollen-robotics/microduck_rl/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/src/mjlab_microduck/export.py
[source-license]: https://raw.githubusercontent.com/pollen-robotics/microduck_rl/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/LICENSE
[training-license]: https://raw.githubusercontent.com/pollen-robotics/microduck_rl/3750776761ecfaa5671b36972087cafba45b8527/LICENSE
[layout-source]: https://raw.githubusercontent.com/pollen-robotics/microduck_rl/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/src/mjlab_microduck/tasks/symmetry.py
[velocity-source]: https://raw.githubusercontent.com/pollen-robotics/microduck_rl/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/src/mjlab_microduck/tasks/microduck_velocity_env_cfg.py
[constants-source]: https://raw.githubusercontent.com/pollen-robotics/microduck_rl/b02fdea4902c2b93b7d34a83a6f05da4ececb7d3/src/mjlab_microduck/robot/microduck_constants.py
[normalizer-source]: https://raw.githubusercontent.com/leggedrobotics/rsl_rl/3ac56acd3376f2952eb636a133f4b5aa30142552/rsl_rl/modules/normalization.py
