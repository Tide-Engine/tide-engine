# Render3D package

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

The final warning-free full native/web debug builds passed. Native CTest passed
all 2687 enabled tests (339.25 s); web CTest passed all 2582 (603.33 s). The
existing disabled Microduck right-turn test remains unchanged in both suites.
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
WebGL example/source check, 290 grammar tests and 256 LSP tests pass.
Native and browser example captures were visually inspected.

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
