# Render3D package

## Skinning, shared vertex deformation and HDR effects (2026-10-25)

The owner requested extending this package rather than implementing another
game pipeline. The owner approved checked Asset<SkinnedMesh<V>> references and
an attached local render Pose? whose omitted value means authored rest. The
compiler reference completion preserves the original pack model, typed geometry
and LOD handles; language/asset notes record its boundaries and tests.

SkinnedMesh3D aliases SkinnedMesh<MeshVertex>. SkinnedMeshRenderer has mesh,
visible/castShadows flags and attached pose. Prepare offset 1 copies it into
preparedPose, so all shadow/color draws use one frame snapshot. The package owns
no animation clock/controller. Null uses the omitted-pose draw; a present
invalid pose keeps the engine's skip-draw behavior. Main and shadow submissions
use proven borrowed models, retaining palette reuse and automatic LOD. Static
and skinned transparents are recorded inside one mark/sort scope. Missing skinned
UV tangents remain missing: mapped skinned materials need authored tangents.
Bounds/sort centers do not automatically follow animation or user deformation.

Material.vertex is an optional configured Deform stage, explicitly constructed
as Deform { vertex = callback }. Its Func<VertexInput, mut MeshVertex, void>
runs after engine skinning, before outer/world placement. Input contains the
pre-deformation world position, effective localToWorld matrix and the main eye,
also during shadow casting. Normal/tangent/UV/color edits feed the existing
material evaluation; the callback owns any normal/tangent correction needed by
its position edits. Color projects after the hook; shadow remaps this.clip after
the same hook. Captures are shared uniforms, avoiding new instance-attribute
pressure. A missing binding keeps the original static no-vert/culling path.

Default opaque shadows keep the prior contiguous asset/handedness/cull bulk
submission. Custom opaque shadows prepare transforms and owned numeric callbacks
once, then record per-caster configured stages; equal adjacent captures can
still merge in the ordinary draw list. This deliberately keeps arbitrarily
larger vertex captures out of per-instance attributes. Cutout and color helpers
retain live component resource/attached-field context. No Material or query row
is retained in the prepared opaque batches.

The new stage order after Transparent is PostPrepare, Bloom, PostProcess,
Present. BeginPost resets output from hdr.color each frame. Bloom thresholds
scene-linear light before exposure, extracts at quarter resolution, applies a
normalized paired Gaussian and publishes an additive HDR composite. Defaults:
disabled, threshold/softness 1, intensity .25, radius 1, two blur pairs. Numeric
inputs/intermediates clamp to finite half-float radiance. The original scene
target remains intact. Existing RGBA8 fallback still clips HDR and cannot promise
the same bloom result as float targets.

PostEffect is Func<Texture, RenderTarget, bool>. Ordered callbacks receive the
current source and a distinct package-owned destination: true publishes a fully
overwritten image, false/default bypasses. Plain captures are construction-time
copies. NextPostTarget/CommitPost expose the same ping-pong pair to staged views
that need their own singleton resources. Such resource context is not implicitly
captured by a Func. defaultPresent=false skips only the final stock blit. The
normal presentation applies exposure/Reinhard/sRGB exactly once. No effects
allocates no scratch and retains one presentation pass.

Full-size post scratch and quarter-size bloom targets allocate lazily, with
readiness independent of Renderer.ready for additive reloads. Window targets
resize through the engine; every used image is rewritten. Disabling bloom before
Prepare frees its pair; full-size scratch remains until disable/Dispose.
Dispose still belongs before recording, never after Present.

The existing provisional eight-texture implementation limit is raised to twelve
across drawing/compiler/backends/Unity; a fully mapped skinned material uses ten.
Sampling banks, read-kind transport, actual-count embed packets and ABI 6 are
recorded in drawing/rendering/web/unity/runtime-ownership notes. All hosts,
libraries and generated games must rebuild together. No new runtime function is
introduced and no new performance result is claimed.

Focused native CPU and Vulkan/OpenGL package checks pass, including original
material/atlas regressions, pose snapshots and palette reuse, mixed transparent
order, independent mapped/cutout pixels and HDR/kernel/effect/lifecycle oracles.
One initial fixture used unsupported `is var`; it now explicitly unwraps Pose.
A local-spawn fixture now checks next-frame visibility, and shadow rays avoid
exact filtered silhouette edges; pixel tolerances remain unchanged. One isolated
Vulkan skinning process crashed without diagnostics; its isolated retry passes.
No cause or source correction is claimed. Full native/web debug builds pass
with external prototype directories disabled for this engine-only task. The
first full native run is 2,789/2,790 enabled checks passing (246.82 s); its sole
diagnostic-fixture mismatch is corrected and passes targeted recheck. The first
full web run is 2,685/2,687 (541.91 s): a color landmark landed on a raster edge
on the software backends. Moving that landmark inside/outside the silhouettes,
without changing its oracle or tolerance, passes all three web backends and both
native backends. The existing disabled Microduck right-turn case remains.

All new skinning/post pixels pass on Vulkan, OpenGL, WebGL, ANGLE D3D11 and
WebGPU, including mapped skin palette/influence reads in high slots, captured
deformation/cutout agreement, mixed transparency and device recreation. Added
BloomChain cases independently verify bloom-before-callback routing at exposures
.5 and 2 across every size/scale row. Actual skinned pack unload/reload and
deferred captures pass on native/WASI. All 372 actual-consumer FXC /WX stage/API
checks pass. Fresh Asset, capacity, post and pipeline source reviews are clean
after correcting the written Bend example's Jacobian and stale API sentence.
Old-layout post migration and Unity player execution remain untested.

The one-round merge of origin/dev at b4655dfd is fa8c3bf3. Its only conflict was
the editor note, resolved preserving both sections. Independent merge review
cleared the Asset/callback/capacity/Unity lifecycle overlaps and found one
inherited declaration-only bootstrap issue. The bounded compile.c correction
retains package functions/shaders like builtins; game routines remain omitted,
and ordinary asset/body/default checking remains. The actual Render3D bootstrap
and strict native/WASI C17 regressions pass; a fresh correction review is clean.
Game-defined shader/callback field dependencies remain an existing limitation
of that Unity fallback, documented in language.md and cli-and-packages.md.

After that correction both whole debug builds pass, and both full engine suites
are green: 2,800 enabled native CTests (192.66 s) and 2,696 enabled web CTests
(536.35 s). The original disabled Microduck case remains disabled. These caches
explicitly omit external prototype directories, per this engine-only task's
scope. New static opaque coverage additionally exercises two consecutive casters
with distinct owned 20-float callbacks and overwritten construction locals.
Exact matrix/uniform/order/frame/atlas comparisons, all capture words and the
16-main/9-shadow attribute limits pass on all five backends; the ordinary
267-item run retains the >256 witness. Its independent fixture review is clean.

Svelte check has zero errors/warnings, the merged production docs build passes,
and all 304 grammar checks pass. Interactive effects/browser validation is in
docs-site.md. No Unity player/high-slot reload, old-layout post migration,
spirv-val, Vulkan validation-layer, mobile, other native OS or sanitizer coverage
is claimed locally. The one isolated Vulkan crash above remains unexplained;
Vulkan checks in both subsequent whole native runs pass without a correction
for it. The final rebuilt effects demo also passes source/stats/camera and GUI
pose/deform/bloom/tint/collapsed-zoom checks on WebGL and WebGPU after the merge.

## Mapped materials and interactive docs (2026-10-25)

The owner approved opaque/cutout/transparent materials, normal/ORM/emission and
height maps, interactive examples with movable cameras, and stats enabled on
documentation launches. Material modes are explicit. Optional maps default to
empty references; CPU size checks produce numeric sampling flags, preserving
the no-map fetch path. Base color keeps the white default. Raw numeric texture
binding exposed a language gap: the owner approved extending `[Raw]` to
`Texture` fields, following raw grid fields. Raw assets/fields/grids bind only
in an explicit raw field context, with no new runtime representation or
function. Checker, capture, helper specialization and role-change migration
contracts and focused validation are recorded in textures.md.

Lit and ShadowCutout share scaled/offset UV evaluation, the UV callback,
centered single-sample bilinear height parallax, and final-UV coverage/cutoff.
Parallax uses the main eye in both passes, with .5 neutral height and a .05
grazing denominator floor; it changes UV relief, not geometry or depth. The
world tangent frame reconstructs the forward tangent from inverse-transpose
normals, corrects the exact CPU determinant sign and orthogonalizes it. Missing
tangents have a stable axis fallback. Double-sided shading faces the geometric
normal and bitangent toward the main eye. Static import generation is selected
only for layouts declaring float4 tangent; other layouts and editor reads keep
canonical geometry/hashes and prior LOD keys. Generation and tangent-sign LOD
charts are described in assets-and-files.md.

ORM channels are R occlusion, G roughness, B metallic. Roughness/metallic map
channels multiply material scalars. Occlusion affects diffuse ambient
irradiance only; that explicit irradiance defaults to zero. Emission maps decode
sRGB and multiply linear emission, which is added once. UV and coverage hooks
live in attached local state; the existing configured Lit surface callback
remains valid. The maximum stock material uses eight sampled bindings.

Opaque shadow batches retain the prior contiguous mesh/handedness policy and
also split cull state. Cutout transforms are prepared once, while their shadow
draws retain each query row's material resource/callback context. Transparent
materials draw after opaque/cutout into HDR with alpha blending and read-only
depth; they cast no shadows. A small package C helper stably sorts owned
captured packets by camera-forward transformed mesh-center depth without
retaining query rows or resources. It uses cached bounds, a base-LOD sphere
fallback, or the origin when neither is available. Empty transparent sets add
no packet barrier. The drawing
runtime preserves depth-read blended LOD order through recording/preparation;
packet ownership/barriers and append contracts are in drawing.md.

Both full native/web debug builds pass. The native suite passes 2,773 of 2,774
enabled CTests (295.69 s), with only the independently reproduced private
prototype hash failure below. All six packet and thirteen engine draw-level
cases pass. Original Render3D pixels and the independent exact shadow-reference
frame/atlas checks pass on Vulkan/OpenGL and WebGL/ANGLE D3D11/WebGPU. All eleven
new material scenarios pass on those five backends, including strict alpha-zero
raw-data witnesses. The complete web run passes 2,687 of 2,689 enabled CTests
(3,231.48 s): only the two private prototype checks described below fail.
Repository checks pass; no complete green configured-suite claim is made.
Both configurations retain the existing disabled Microduck right-turn fixture.

Numeric fixtures independently cover transformed tangent bases and hooks,
indirect-only AO and emission invariance. Pixel fixtures additionally check
cutout holes/shadows, parallax coverage, raw resource/callback retention,
transparent depth state and mixed-LOD order. Actual material-consumer exports
pass all 36 HLSL API/stage compilations with FXC /WX. Compiler purity/query
fixes and Raw field checker/capture/metadata/migration validation are recorded
in shaders.md and textures.md. Fresh material integration, transparent safety,
center ordering, Raw and typed-tangent reviews have no blocking findings.

Svelte check reports zero errors/warnings; the staged production docs build,
three export/admin HTTP checks, five playground tests and 301 grammar cases
pass. Both examples pass live WebGL/WebGPU source, stats URL and camera checks.
The mapped example additionally witnesses reset, real height-command edits,
cutoff/opacity changes and collapsed zoom above stats through scene pixels.
Native 240-frame --stats captures at 376x400 and 960x540 show the complete
exhibit above its controls, and browser cutout/blend/collapsed captures were
visually inspected. One initial UI probe clicked outside a slider; correcting
the test coordinates preserves its scene-pixel assertions.

The recreated web cache now uses the checked-in WASI SDK 24 toolchain, unlike
the historical SDK 34 runs below. Vulkan layers/spirv-val, other native operating
systems, mobile and sanitizers remain unavailable local coverage. An initial
material Vulkan process exited without diagnostics; its debugger run, five
isolated repeats and both subsequent full native rounds pass. No cause or
source correction is claimed for that unreproduced event. No new performance
measurement or speedup is claimed. Later performance comparisons need equivalent
material/mesh layouts; earlier frozen CPU-preparation candidates describe their
earlier source rather than this expanded material layout.

The initial full native run also found a CRACKED prototype deterministic-hash
failure. An isolated clean control at engine commit
5d5f61729aff7b94fd269be543b6f95bcb7cc9d4 and private prototype commit
6ae2af1d1a763b58aa579e7bc98ef8ffd3499f51 reproduces the identical actual
0xbf53c0610c47764e versus pinned 0x0a216d532fa6445c after 3,000 ticks (816
bullets, one round, zero points, map zero). All 122 prototype input hashes are
unchanged. The control ran only that test and exited 1; no expected hash or
prototype source was changed. Its command/identity/output receipts are retained
under the isolated t3-6f56361c worktree's build/baseline-control directory.

The optional Inside Out WebGL playthrough also fails its exact cross-player
tutorial-label PNG comparison. Independent read-only attribution finds one
blue-channel step at global pixel (1123,41): RGB (157,145,126) versus
(157,145,125). Both labels read "10 moves / par 10"; the entire 1280x76 header
otherwise matches, as do earlier move-label and home-region captures. The
private assertion compares PNG buffers byte-for-byte; its -1 result is not a
pixel difference count. The failure captures are frozen completed frames.
Inside Out uses procedural position/normal geometry, no Raw fields or Render3D,
and its scene passes use Depth.ReadWrite/Blend.Replace. The label uses unchanged
Draw.Text/font paths; its Normal helper does no sampled UV lifting, and neither
page requests --stats. No direct route from this feature to the label was found.
This supports a small visual quantization difference, but no baseline browser
control was executed and pre-existing status is not claimed. The private source,
assertion and hash pins remain unchanged; raw output and captures are retained.
The full WebGPU playthrough and both backends' dedicated material/shutter checks
pass, as do all other optional playthroughs in that run.

The origin/dev merge through 1a12d872 adds Transform correction defaults, editor
Play cleanup, Earth terrain and the click-to-play Earth guide. Compiler overlap
is confined to independent checker blocks. Resolved notes retain both complete
histories; the homepage retains incoming static behavior. EarthDemo's initial
separate-tab link and hosted/local runtime URLs request stats once, retaining
the readiness URL and incoming controls. The adapted browser smoke discovers
Samples' actual prose link to the guide, rather than assuming an Earth game
card. Review caught that initially unreachable test branch; explicit link
discovery resolves it and the fresh follow-up review is clean.

Both merged builds pass, with all 67 focused native CPU/compiler/editor/Earth
checks (92.11 s), six Vulkan/OpenGL Render3D checks (1.49 s) and 69 affected web
checks (123.00 s) passing. The latter includes WebGL/ANGLE D3D11/WebGPU material
and shadow-reference checks plus both stats paths. Final Svelte check is clean,
the staged production build passes, and all three export/admin HTTP checks pass
after adapting the Earth guide's Markdown play link. Both material demos and
Earth's local fallback Play/Stop/stats path pass live on WebGL and WebGPU.
Snapshot probes now wait for the iframe's CSS fade to finish and disable
background pipeline compilation; exact camera-reset pixel assertions remain.
No repeat of the unrelated optional playthroughs is required for this scoped
merge overlap.

## CPU recording preparation (2026-10-25)

The owner requested further performance work after the static-cube measurements.
Existing per-view profiling identifies Cast and DrawMesh as the recording work;
light collection and pass setup are small in the 1,000-cube/eight-view probe.
Private sparse inner timers also identify both hierarchy resolution and shaded
recording, with material timer overhead. These diagnostic means are attribution
probes, not a gated before/after performance result.

Cast prepares current visible, shadow-casting query rows once per invocation.
Contiguous mesh-reference and transform-handedness runs share a transform list
across the light views. Single-item runs retain ordinary Mesh submission and
store their matrix inline, avoiding a list allocation for every distinct mesh.
Larger runs use existing Instanced drawing after each view's camera and clip
are set. Winding uses the runtime's exact determinant expression and operation
order; splitting there retains prepared LOD ordering across mirrored runs.
The existing per-view LOD/culling behavior, geometry, AtlasVertex, depth, atlas
gutters and receiver shading remain the drawing runtime's.

Prepared values are scratch-local to this Cast invocation, rebuilt from current
mesh selection, hierarchy and flags. There is no cross-frame scene cache or new
renderer state/lifetime. Zero shadow views skip preparation while retaining
Cast's destination clip reset. DrawMesh/material
textures, configured callbacks and numeric instance values retain their existing
path. The compiler's fixed-size component loads and owner-approved typed asset
identity comparison are documented in language.md; no runtime API was added.

The independent original per-caster reference compares prepared geometry,
instance bytes, winding, LOD order, camera/clip state, full-frame pixels and
atlas depth, including dynamic changes, A/B/A meshes, hierarchy shear and
mirroring, more than 256 transforms, all point faces and no-view state. Review
found a per-view nested-list copy; direct indexed reads remove it, confirmed in
generated C. Final state review also confirms the no-view clip reset witness.

Full native debug passes all 2,726 enabled CTests (181.02 s), followed by all
nine focused Render3D/benchmark checks after the no-view clip completion (6.81 s).
Full web debug passes all 2,622 enabled CTests (572.99 s); all eleven focused
web Render3D/benchmark checks pass after the clip completion (93.89 s), including
WebGL, ANGLE D3D11 and WebGPU. Optimized native Vulkan/OpenGL benchmark quick
checks pass (two CTests, 1.45 s). The production docs build passes. One earlier existing OpenGL pixel process
crashed without diagnostics; its isolated run, five debugger runs, full suite
and final focused round pass. No cause or source correction is claimed for that
unreproduced crash. The web cache uses SDK 34 rather than pinned SDK 24;
spirv-val/Vulkan layers, other native operating systems, mobile and sanitizer
coverage remain unavailable.

The clean origin/dev merge through 1a1e4f7f brings GPU simplex/Earth changes.
The affected shader/HLSL, renderer, benchmark and Earth checks pass: 98 native
(49.01 s) and 62 web (243.05 s), plus two optimized benchmark smoke checks
(1.18 s). Render3D's regenerated C/header differ from the pre-merge build only
in simulation build identity. No unrelated full-suite repeat is claimed.

The later task/worker merge through a21a81a8 overlaps the compiler and generated
runtime boundary. Both full builds pass again; native passes all 2,739 enabled
CTests (181.44 s) and web all 2,635 (439.38 s), retaining the same disabled
Microduck fixture. The docs build and all 300 grammar tests pass. Independent
source and resolved-notes reviews find no actionable compatibility issue.
The optimized benchmark fixture also rebuilds against this merged source.

The subsequent GUI refresh through c7234d3c merges the container cleanup and
primitive drawing changes without source conflicts. Both complete builds pass;
the affected GUI/drawing/shader/compiler/Render3D checks pass on native (263,
11.29 s) and web (258, 226.80 s). The docs build, independent overlap review
and optimized Vulkan/OpenGL smoke checks (two, 1.14 s) pass. This narrower
overlap uses affected checks rather than another full-suite repeat. The ready
optimized executable has SHA-256
e94238145d22e53af0f3ef3fd3f95c2b70ef702c8fb98b80d8d5056d01f75681.

The coordinator deferred the paired timing attempt for active correctness/P1
work before any paired measurement process or benchmark gate started. A fresh
explicit quiet window is required. The original a1d962c9 executable remains
frozen (SHA-256 9d3ab3ba54b734334b87e9276c331fc1bf97177583ae71802e19ae4d424d1ce9);
the first merged 90368d1e build had SHA-256
c9a5aafe13fcc1e208a1d957ee54249350cad9d5b32421ff4061dd008ef11251.
No post-optimization timing result or speedup is claimed. Historical
measurements below retain their original source/executable identities.

## Measured static-cube workload (2026-10-25)

`bench_render3d` measures source a1d962c99d93657d23f79ba54b0c836f635905e8,
optimized native Vulkan at 1920x1080 on the operator-supplied Ryzen 9 7900X /
RTX 4080 SUPER machine (NVIDIA inventory driver 617.42; Clang 23.1.2 -O2,
no fast-math/FMA contraction). It uses production PBR, atlas shadows and HDR
presentation. Fixed-rotation 24-vertex/12-triangle cubes keep summed projected
coverage constant, with one fixed receiver. This is a controlled static
geometry workload, not a representative complex-model or real-game limit.

Medians of three process medians, package CPU recording / GPU ms:

| Cubes / lights | Shadows | Recording / GPU | Total calls |
| --- | --- | ---: | ---: |
| 100 / one directional | One map | 0.04395 / 0.117440 | 5 |
| 1,000 / one directional | One map | 0.31935 / 0.132432 | 5 |
| 10,000 / one directional | One map | 2.92990 / 0.260016 | 5 |
| 10,000 / same directional | Off | 1.82285 / 0.148672 | 4 |
| 1,000 / 1, 8, 32 points | Off | 0.18885, 0.18555, 0.19270 / 0.103472, 0.352864, 1.099296 | 4 |
| 1,000 / one point | Six faces | 0.75105 / 0.155936 | 10 |
| 1,000 / one directional, two points, one spot | Eight views | 0.95850 / 0.333952 | 12 |

The same-light point and mixed unshadowed controls give 0.18555/0.104192 and
0.21355/0.212192 ms respectively. One opaque batch contains all cubes and the
receiver despite numeric material differences; each shadow view adds one
caster batch, with two clears and presentation accounting for the other three
calls. Warmed texture uploads are zero. Requested/active shadow cells stay 512;
six/eight views use a 1536-square atlas and mixed's second point is unshadowed.

The equivalent cached-direct C path is a static submission floor, excluding
recurring ECS/hierarchy/collection work while using the identical shaders,
transforms, numeric data and passes. At 10,000/one map its recording/GPU is
0.15775/0.207312 ms. Package/floor GPU ranges are 0.212080–0.299984 and
0.206432–0.211264; identical shader work with uncontrolled, unrecorded clocks
establishes no GPU speedup. CPU/GPU intervals overlap and are never added;
native completion/readback is separate synchronization overhead, not FPS.

All 78 rows across 36 optimized processes pass untimed full native pixels and
expanded-command equivalence, with fixed per-frame counts and 100 GPU samples
per 100 measured frames (7,800 total each). The coordinated quiet window used
the shared gate separately per group, with 5/1-second guards and no detected
contention/resets/exclusions; other desktop activity was uncontrolled and the
physical adapter was not queried by the API. Focused native/web debug builds
and Vulkan/OpenGL/WebGL/served WebGPU quick checks pass; optimized native
Vulkan/OpenGL quick checks pass (two CTests, 2.22 s). Software web smoke is
correctness coverage only. Broader package validation below is separate.

[Saved evidence, exact ranges/p95s, verification and reproduction](../bench/results/render3d-20261025/README.md)
retain original bytes and measured source/binary/input identities; no binaries
are committed. A later source-comment correction clarifies batching without
changing the measured a1d962c9 identity. No new package/compiler/runtime change
or full-suite/other-platform timing claim accompanies these measurements.

## Mesh aliases and light components (2026-10-24)

The owner approved general transparent type aliases and multiple directional,
point and spot light components. Tide's alias declaration is `using Name = Type;`;
the compiler contracts live in language.md. Render3D declares
`using Mesh3D = Mesh<MeshVertex>;`, so assets use `asset Render3D.Mesh3D` and the
renderer component holds `Asset<Mesh3D>`. This preserves the existing mesh asset
identity and vertex layout. Area lights, baked lighting and probes remain later
work.

### Collection and lighting

Renderer.sun is replaced by DirectionalLight, PointLight and SpotLight entities.
The package collects both match and local placements, using stored hierarchy
matrices. Position is the matrix's translation; direction is a local photon/cone
direction transformed by that matrix then normalized. No Transform means identity.
The existing stored-transform interpolation limitation still applies.

Directional radiance and punctual intensity are linear RGB. Point/spot intensity
is the incident-light scale at one world unit before range fading. The finite
window is `(1-(distance/range)^4)^2` inside range, with inverse-square falloff and
a .01-unit source-radius floor. Spot angles are full cone openings in radians:
outer clamps to 1..179 degrees, inner to 0..outer; their half-angle cosines give
smoothstep cone attenuation. Nonpositive range disables a punctual light.

Stages are Prepare, Lights, Shadows, Color, Opaque, Present. Prepare clears the
retained numeric light/view lists. Collection keeps up to maxLights (default 32,
nonnegative) in descending priority; ties retain collection order, directionals
then spots then points, match before local and entity traversal within each.
droppedLights counts otherwise enabled candidates excluded by that budget.
The shader loops the numeric light list in one forward pass. EvaluateDirectPbr
excludes emission; emission is added exactly once even with zero or many lights.
There is no implicit default light.

### Shadow atlas

maxShadowMaps defaults to 8 and clamps to 0..16 views. Each directional/spot uses
one view; a point reserves six contiguous views together or stays unshadowed.
Selected lights request shadows in priority order. unshadowedLights counts
requests without views, including global shadow disable or unusable near/range.
Per-light castShadows and material receiveShadows are independent.

One direct DepthTexture field binds the shared atlas. Numeric LightData and
ShadowView lists use the existing data textures; with the base map this is four
texture bindings. No runtime ABI, resource array or new backend feature is needed.
Raw square light-camera WorldToClip matrices preserve receiver projection and
outside-volume tests. A custom vert maps only clip x/y into each atlas interior;
scissor clips are set in target.Screen. Every PCF tap clamps to interior pixel
centers, with two cleared gutter pixels around each tile.

shadowResolution requests cell size including gutters. The actual grid is based
on reserved views and caps the atlas at the portable 2048-pixel baseline; cell
resolution is reduced as needed and exposed as activeShadowResolution. With no
views, the current atlas size is retained (initially 16) and active resolution is
zero. IDs stay stable over size changes. The full atlas clears after resizing
and before casting. Shadows iterate views outside a caster query, allowing
compatible caster draws to batch within a view; all shadows finish before HDR.
Point faces use +x,-x,+y,-y,+z,-z with the existing cubemap camera orientations.
The receiver selects one by its largest light-relative axis (x then y then z
ties), and compares perspective depth rather than radial distance. A selected
point face covers x/y by construction: it keeps the strict w/z checks but clamps
the PCF taps instead of rejecting slightly out-of-range x/y at exact face seams.
Directional and spot views retain strict x/y outside-volume rejection.

### Validation

After merging GUI and local-worker work through a6ed9bfd (2026-10-25), the final
warning-free full native/web debug builds passed. Native CTest passed all 2716
enabled tests (298.33 s); web CTest passed all 2611 (539.43 s). The existing
disabled Microduck right-turn test remains unchanged in both suites.
Render3D pixels pass on Vulkan/OpenGL and WebGL/ANGLE D3D11/WebGPU, including
colored match/local lights, transformed directions/positions, point range and
full spot cones, independent per-light shadows, all six point faces/seams,
material/light shadow flags, zero/multiple-light emission and 1x/2x capture.
CPU fixtures cover priority/budgets, atomic six-face reservations, independent
projection landmarks, atlas resizing/gutters, batching/order, disposal and hashes.

Review found one point-face seam issue from strict x/y roundoff rejection; the
correction and targeted second review are clean. The GPU regression projects
four coordinates two binary32 ULPs outside face edges against real occluder
depth, and separately checks strict far-z/negative-w and nonpoint guards.
All four generated renderer shaders passed FXC /WX across D3D11/GLCORE/GLES3
vertex and fragment stages (24 compilations). The aliased-stage compiler
consumer passed six more HLSL compilations. The production docs build, live
WebGL example/source check, 295 grammar tests and 257 LSP tests pass.
Native and browser example captures were visually inspected.

The merge audit corrected live-asset reads through worker reference values and
task-start synchronization of readonly match copies in views. The mixed
Render3D/worker consumer passes strict generated-C checking; the local_workers
fixture preserves attached local fields, blended values and match hashes.
Independent review cleared both corrections. Details are in language.md.

The web cache still uses installed WASI SDK 34 via ignored wasi-local.cmake,
not the checked-in SDK 24 preset. spirv-val/Vulkan validation layers remain
unavailable; other operating systems, mobile and sanitizers remain CI coverage.
No CI was dispatched. General alias contracts/review corrections are recorded
in language.md and editors.md. No deterministic expected hash changed.

## First forward slice (2026-10-24)

Owner-approved: default metallic-roughness PBR, one directional light/shadow map,
linear HDR with tonemapping, and a custom surface callback. The owner selected
mesh/material components with package-owned passes. render3d/Render3D is separate
from future render2d/Render2D. Baked indirect lighting, probes and compile-time
lightmaps remain the broader direction; this slice adds no placeholders for them.

packages/render3d is pure Tide: no new runtime functions, compiler semantics,
generated-C ABI or backend changes. Ordinary package directory scanning discovers
and installs it. docs/packages/render3d.md is the API page; the runnable
docs/examples/render3d.tide uses analytic cube/sphere OBJ assets in its own folder.

### Ownership and passes

Match entities have MeshRenderer (typed mesh asset handle, visibility and casting
flags), Material (numeric display-color/metallic/roughness/emission) and usually
Transform. Material's texture and concrete Lit binding are attached local fields,
outside snapshots/networking/hashes. This material component cannot be placed in
the local world: Tide disallows local placement with attached local fields.
The default shader is `Lit { surface = Render3D.StandardSurface }`; the qualified
reference is a valid constant initializer under existing constant rules.

The local Renderer singleton owns camera/sun/settings and retained targets.
Prepare allocates/resizes the bounded 16..4096 shadow map and clears depth;
Shadows finishes all casters before Color clears HDR; Opaque draws material
instances; Present blits through exposure/Reinhard/sRGB. Games configure before
Stages.Prepare and draw HUDs after Stages.Present. Ordinary stored hierarchy
transforms place meshes; hierarchy interpolation remains existing engine work.

Targets are reused until disabled or explicitly disposed. Disabling before
Prepare disposes both; re-enabling recreates them. Shadow resolution resizes in
place; the HDR target follows the window. Every pass redraws each frame, so lost
contents need no restoration. Disposing before recorded commands render invalidates
those commands under the existing engine contract. Shadows disabled retain/clear
a valid depth resource, avoiding invalid statically bound samplers.

### Shading contracts

The frag samples the direct local Material.baseMap field. No texture is captured
by a callable or passed through an ordinary GPU aggregate; nested sampled resource
paths/material resource collections retain existing limitations. SurfaceInput and
Surface are numeric. The frag initializes the surface, calls
Func<SurfaceInput,mut Surface,void>, then lights it. Material.shader stores that
concrete Lit binding, including owned plain captures. Surface edits change
appearance, not the shadow geometry.

PBR uses GGX, height-correlated Smith visibility (already including the
Cook-Torrance denominator), Schlick and Fresnel/metal-reduced Lambert diffuse.
Roughness clamps to .05..1; metallic/albedo to 0..1. No ambient/indirect
approximation is added: unlit surfaces are dark, with emission added once.
Instance.Normal handles mirrored and nonuniform transforms.

Tint, base map and vertex colors each decode from display sRGB before
multiplication. Lighting stays linear in RGBA16F; presentation applies linear
exposure, bounded Reinhard, then piecewise sRGB encoding once. Display-space
texture filtering/mips and float-target fallback are unchanged. pbr.tide contains
the pure numeric helpers and guards half-float overflow at presentation.

Shadows use target.WorldToClip, divide by w, flip UV y and remap clip z to depth.
Outside-volume points are lit. Four comparison samples soften edges. Receiver
bias is normalized light depth and depends on volume/resolution: the 256-pixel
fixture uses .003; package defaults target 1024 pixels.

### Tests and remaining scope

tide_render3d_pbr checks independent analytic dielectric/metal values,
roughness/grazing/zero directions, color roundtrips, HDR, callbacks and hashes.
tide_render3d_pipeline checks shadow/color/present ordering, retained IDs,
disable/dispose and recreation. Shadow draws with custom vertex types can use
SHADED commands, so it classifies their destination rather than command kind.

tide_render3d_pixels exercises Vulkan/OpenGL and WebGL/D3D11/WebGPU: distinct
base-map colors, metallic/roughness, HDR emission above one, captured surface
callbacks, inverse-transpose normals under mirrored/nonuniform scale, lit/shadowed
and outside-volume receivers, changed shadow settings and 1x/2x output. Hosts load
the pack before creating the world. Match hashes remain unchanged.

Limits: opaque static mesh assets, one camera/sun/fixed shadow volume, base map
plus scalar values; no transparency, cascades, point lights, normal/mask maps,
skinning, GI, probes or baker. Ordinary compatible draws batch; changed materials,
textures and state split them. Callable type-wide dispatch/layout limits remain.
This introduces no general retained render graph or material registry.

### Final validation

Native debug build and full `ctest --preset debug -j4 --output-on-failure`:
2625 enabled tests passed (359.17 s). Web debug build and full
`ctest --preset web-debug -j4 --output-on-failure`: 2520 enabled tests passed
(546.09 s). Both retain the pre-existing disabled Microduck right-turn fixture;
this task added no disabled tests. Render3D pixels confirmed Vulkan, OpenGL 3.3,
WebGL 2, ANGLE D3D11 and WebGPU renderer identities. Native/web examples compile.

`npm --prefix docs run build`, all 283 shared grammar tests, and the scoped
`TIDE_EXAMPLES=render3d node docs/tests/examples.mjs` passed. The browser test
checks displayed source and live WebGL rendering with fatal shader errors and
muted audio. The native ten-frame capture and browser example were visually
inspected. Three generated HLSL programs passed FXC /WX for vertex/fragment
stages on D3D11, GLCORE and GLES3 (18 compilations).

Implementation review found no actionable code findings. The final docs audit
found a reversed Draw.Text example argument order and missing explicit disposal
timing; both are corrected in the final built docs.

The existing web build cache uses ignored build/wasi-local.cmake and installed
WASI SDK 34 rather than the checked-in preset's SDK 24; the earlier SDK 24
download was unavailable. This validates SDK 34, not the pinned SDK 24 toolchain.
spirv-val and Vulkan validation layers are unavailable on this machine. Other
operating systems/mobile and sanitizers remain CI coverage; no CI was dispatched.
