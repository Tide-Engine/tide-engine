# Render3D package

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
