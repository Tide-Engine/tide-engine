# Render3D

Optional 3D rendering with opaque, cutout and transparent mesh/material components, directional, point
and spot lights, a shadow atlas, linear PBR lighting and HDR tone mapping. The
package owns its lighting, shadow, color and presentation passes and includes a
small C helper for transparent-object sorting. Its namespace
is `Render3D`. Lights and ambient irradiance default to zero; with neither
configured, only material emission contributes.

## Using it

In a Tide checkout or installation containing the package:

```sh
tide add render3d
```

This writes `render3d` in the game's `tide.packages`. Games that omit it compile
and ship none of its code. Supply a cube mesh at `art/cube.obj`:

```csharp
asset Render3D.Mesh3D Cube = "art/cube.obj";

scene Main;

event(Spawned) Build(with Main)
{
    Spawn(Transform,
        Render3D.MeshRenderer { mesh = Cube },
        Render3D.Material { baseColor = Color.white });
    Spawn(Render3D.DirectionalLight { radiance = float3(2) });
    Spawn(Transform { localPosition = float3(0, 3, -2) },
        Render3D.SpotLight { direction = float3(0, -3, 2),
                             intensity = float3(25), castShadows = true });
}

[Before(Render3D.Stages.Prepare)]
view ConfigureRenderer(mut Render3D.Renderer renderer)
{
    renderer.camera.position = float3(0, 2, -5);
    renderer.camera.rotation = quaternion.LookRotation(float3(0, -2, 5), float3(0, 1, 0));
}
```

`Render3D.Mesh3D` is a transparent alias for `Mesh<Render3D.MeshVertex>`.
With `using Render3D;`, write `asset Mesh3D Cube = "art/cube.obj";`.

Full API and runnable example: [Render3D](https://tide-engine.dev/packages/render3d).
The example's source is [docs/examples/render3d.tide](../../docs/examples/render3d.tide).
Drag or swipe to orbit; use a pinch gesture or zoom buttons, and reset the
camera from its controls. Standalone pages also support wheel zoom; the wheel
scrolls the documentation page when an example is embedded. The [material example](../../docs/examples/render3d-materials.tide)
compares normal/height/ORM/emission maps, cutout shadows and sorted transparency.

## Components and settings

- `MeshVertex`: position, normal, tangent (`float4`), UV and color (white by default).
- `MeshRenderer`: `Asset<Mesh3D> mesh`, `visible = true` and
  `castShadows = true`.
- `Material`: display-space `baseColor` (white), `metallic` (0), `roughness`
  (0.5), linear HDR `emission` (0), and `receiveShadows = true`. Attached local
  `baseMap` defaults to the package's white texture; attached local `shader`
  defaults to `Render3D.Lit { surface = Render3D.StandardSurface }`.
- Surface controls: `mode = SurfaceMode.Opaque` (`Cutout` and `Transparent` are
  also available), `alphaCutoff = 0.5`, `doubleSided = false`, `normalScale = 1`,
  `occlusionStrength = 1`, `heightScale = 0`, `uvScale = float2(1)`,
  `uvOffset = float2(0)`.
- Attached local maps: only `baseMap` defaults to `White`. `normalMap`,
  `maskMap`, `emissionMap` and `heightMap` default to empty handles (`default`).
  Empty maps leave the geometric normal, scalar roughness/metallic, occlusion 1
  and scalar emission unchanged, with no height parallax. Unused optional maps
  are not sampled, including empty maps on emissive materials. `FlatNormal` is
  available for explicit assignment. ORM mask channels
  are **R** ambient occlusion, **G** roughness, **B** metallic. Roughness and
  metallic multiply their scalars; decoded emission multiplies linear HDR
  `emission`. Normal/ORM/height channels are numeric; base and emission RGB
  are display sRGB, and alpha is linear.

`normalMap`, `maskMap` and `heightMap` are `[Raw] local Texture` fields.
Bind Raw image assets, other Raw texture fields, Raw `Grid2<Color>` fields/assets
or `default`. Ordinary color textures are rejected. `baseMap` and `emissionMap`
are color fields. The Raw field annotation preserves the channel numbers,
including zero-alpha pixels, through sampling and GPU texture helpers:

```csharp
[Raw] asset Texture normal = "art/normal.png";
[Raw] asset Texture orm = "art/orm.png";
[Raw] asset Texture height = "art/height.png";

[Before(Render3D.Stages.Prepare)]
view BindMaps(mut Render3D.Material material)
{
    material.normalMap = normal;
    material.maskMap = orm;
    material.heightMap = height;
}
```

There is no separate Raw texture type. Raw sources cannot pass through ordinary
`Texture` locals/fields, be returned as textures or be drawn directly. Keep
the Raw source expression when passing it through a GPU texture helper.

Put `MeshRenderer` and `Material` on **match entities**, using ordinary `Spawn`.
Both are required to draw or cast shadows. Attached local resource fields prevent
local-world material placement. Meshes follow the stored `Transform` hierarchy;
textures and shader bindings stay machine-local. Numeric material fields are
match state: change them in match code or through a command from local UI.

The per-machine local singleton `Renderer` has `enabled = true`, `camera`,
display-space `background = Color(0.06, 0.08, 0.12)` and linear-multiplier
`exposure = 1`, plus linear diffuse `ambientIrradiance = float3(0)`.
`Camera` defaults to position `(0,2,-5)`, identity rotation,
`verticalFov = 60deg` in **radians**,
`near = 0.1` and `far = 1000`.

## Lights and shadows

Light components work on match entities (`Spawn`) and machine-local entities
(`local Spawn`). Each has `enabled = true` and `priority = 0`:

| Component | Light fields and defaults | `castShadows` |
|---|---|---|
| `DirectionalLight` | `direction = float3(1, -2, 1)`, `radiance = float3(4)` | `true` |
| `PointLight` | `intensity = float3(10)`, `range = 10` | `false` |
| `SpotLight` | `direction = float3(0, 0, 1)`, `intensity = float3(10)`, `range = 10`, `innerAngle = 25deg`, `outerAngle = 40deg` | `false` |

Positions come from the stored world transform. Local directions mean photon
travel/cone direction; the world matrix transforms them before normalization.
The world matrix is identity when neither the entity nor its ancestors has a
`Transform`. These reads do not interpolate presentation transforms. Range is in
world units. Radiance and intensity are linear RGB, clamped nonnegative.
Point/spot intensity is the unwindowed light at one world unit; inverse-square
falloff uses a 0.01-world-unit distance floor and a smooth window reaching zero
at `range`. Nonpositive range disables a light. Spot angles are full cone openings
in radians: outer clamps to 1–179 degrees, inner to 0–outer, with a smooth fade
between them (a hard edge when equal).

`Renderer.maxLights = 32` is configurable; negative values act as zero. Highest
`priority` wins, descending. Ties retain collection order: directional, spot,
point, with match before local within each kind. `droppedLights` counts lights
excluded by the limit. Configure directional components instead of the removed
`Renderer.sun`.

Shadows default on. Directional lights use `shadowCascades = 4`, clamped to
0–4. Values 1–4 fit shadow maps to slices of the camera frustum, concentrating
detail near the camera. `shadowRange = 40` sets the farthest camera-forward
distance receiving these shadows, limited by `camera.far`. Keep it near the
distance your scene needs. Cascade projections use stable sphere bounds and
texel-snapped placement, blend over the last 10% of each slice, and fade to lit
at the final range. `shadowDistance = 40` extends their casting volumes toward
the light so off-camera objects can still cast onto visible receivers.

Set `shadowCascades = 0` for a single fixed directional volume. This mode uses
`shadowCenter = float3(0)`, `shadowExtent = 10` (half-width/half-height) and
`shadowDistance = 40` (total depth), in world units. Only this mode uses
`shadowCenter` and `shadowExtent`. Spot shadows follow the cone; point shadows
use six faces. Outside-volume receivers are unshadowed.

`maxShadowMaps = 8` clamps to 0–16 shadow views, assigned in retained light
order. A directional light needs one cell per cascade, or one in fixed mode;
a spot needs one and a point needs six. All of a light's cells are allocated
together or none. A rejected shadow request still illuminates; later requests
can use remaining capacity. `shadowNear = 0.05` (at least 0.001) is the
point/spot near plane; a range no larger than this cannot cast shadows.
`unshadowedLights` counts retained lights requesting shadows but receiving none,
including when `shadows = false`.

Edges use normalized tent filtering, with four bilinear depth comparisons and
weights adjusted to the fractional texel position. There is no random jitter.
Vulkan devices whose depth format cannot filter comparisons linearly retain
the backend's nearest-comparison fallback, with reduced edge quality.
`shadowBias = 0.0005` and `shadowSlopeBias = 0.002` are normalized comparison-depth
offsets. Cascades scale these offsets for their world-space texel size and depth
span; fixed, point and spot views use the offsets directly. Too little bias
causes self-shadowing; too much separates the shadow from its caster.

`shadowResolution = 2048` requests each atlas cell's pixel size, including a
two-pixel gutter on every edge. The smallest square grid fitting the views clamps
cells to `16..floor(atlasLimit / gridSize)`, with an atlas limit of 4096 per side
or the device's smaller limit. One default directional light uses four 2048-pixel
cells in a 4096-square atlas on devices supporting that size. Set a lower
`shadowResolution` for a smaller memory and shadow-rasterization budget.
Derived `activeShadowResolution` reports actual cell size including gutters, and
`atlasSize` the atlas side length. Gutters, filtering and cascade snapping use
these actual dimensions on smaller devices too. With no shadow views, the atlas keeps its size (initially
16×16), clears to far depth and reports `activeShadowResolution = 0`.
`shadows = false` skips shadow drawing/sampling while retaining a valid sampler.

The package retains its HDR and depth targets and preserves the depth resource
ID across atlas resizing. Disabling before preparation disposes them, and
re-enabling recreates them. `Render3D.Dispose(renderer)` releases them explicitly.
Call it before `Stages.Prepare`: disposing after `Present` still invalidates that
frame's recorded draws and presentation.

## Ordering and custom surfaces

The package views run `Prepare → Lights → Shadows → Color → Opaque → Transparent → PostPrepare → Bloom → PostProcess → Present`, from
`Render3D.Stages`. All casters finish before color rendering. Configure camera,
lighting and attached local material fields in `[Before(Render3D.Stages.Prepare)]`
views; draw window UI in `[After(Render3D.Stages.Present)]` views.

Assign `material.shader = Render3D.Lit { surface = CustomSurface }` in a view.
The callback is `Func<SurfaceInput, mut Surface, void>`: input contains `world`,
`normal`, world-space `tangent`/`bitangent`, `uv` and `viewDirection`; the
initialized surface contains linear `albedo`, world-space `normal`, `roughness`,
`metallic`, linear HDR `emission` and `occlusion = 1`.
Owned plain-value captures work; resource and heap captures do not. Callbacks
change appearance. `Material.vertex` separately controls shared geometry deformation.
Compatible shader/mesh/texture/uniform/state draws can batch;
different materials need not share a draw.

`Material.uv` is an attached local
`Func<SurfaceInput, mut float2, void>` initialized to `StandardUV`. It runs
after `uvScale`/`uvOffset` and before height parallax. `Material.coverage` is an
attached local `Func<SurfaceInput, mut Coverage, void>` initialized to
`StandardCoverage`; it receives final UVs and the sampled alpha/cutoff.
`Coverage` declares `alpha = 1`, `cutoff = 0.5`; the package initializes them from
base-map/base-color/vertex alpha and the material cutoff. Both hooks run for
cutout shadows too. The original `Lit { surface = CustomSurface }` form remains
valid. A surface callback cannot change coverage.

Opaque and cutout materials write depth. Cutout discards alpha below its cutoff
in both color and shadow passes. Transparent materials draw after opaque/cutout
with source-alpha blending, depth testing and no depth writes or shadow casting.
Objects sort back to front; intersecting geometry and self-overlapping triangles
can blend incorrectly because triangles are not sorted. `doubleSided` draws
both sides and turns the shading frame toward the viewed side. No refraction
or physically modeled glass transmission is provided.

Normal maps use the mesh tangent frame, including mirrored/nonuniform transforms.
Only static `Mesh<V>` layouts declaring `float4 tangent`, including `Mesh3D`,
generate missing UV tangents and duplicate vertices at opposing generated
handedness seams. Generated xyz is normalized and orthogonal to the normal,
with w of +1 or -1 and finite fallbacks for degenerate UVs. Authored tangents
retain their existing coordinate/node-transform conversion, including zero and
nonorthogonal values. Layouts without tangents or with `float3 tangent`, and
skinned meshes, retain their prior streams, counts, indices and canonical hashes.
LOD preserves tangent signs only for static `float4 tangent` layouts, without a
new xyz error metric; other layouts and skinned meshes retain their prior LOD
behavior and cache keys. Generated tangents do not promise MikkTSpace bake compatibility. Height
mapping shifts UVs with one height sample; it changes no geometry, depth or
silhouette. Keep the scale modest, especially near grazing views.

## Skinning, vertex deformation and post-processing

Use `SkinnedMeshRenderer { mesh = model }` and a normal `Material` for an
`asset Render3D.SkinnedMesh3D model`. Its model field is a checked
`Asset<SkinnedMesh3D>` reference. Configure the attached local `pose` before
`Stages.Prepare`; the package snapshots it once for every pass. `null` draws
authored rest, while a present invalid pose skips drawing. Games own sampling,
blending and playback clocks. Skinned maps need authored UV tangents.

Set `material.vertex = Render3D.Deform { vertex = MyVertex }` for a shared
`Func<VertexInput, mut MeshVertex, void>` callback. It runs after skinning in
mesh-local space, before the draw transform, for both color and shadow passes.
Input contains the pre-deformation `world` position, main-camera `eye` and
`localToWorld`. Update normals/tangents when needed. Plain captures are shared
uniforms; `null` preserves the original static path and automatic bounds.
Custom-deformed/skinned meshes need explicit conservative bounds for culling.
Static and skinned transparent submissions share one object sort; centers do
not follow animated geometry.

Enable `renderer.bloom.enabled` for scene-linear bloom before custom HDR
effects, exposure and tone mapping. Defaults are threshold `1`, softness `1`,
intensity `0.25`, radius `1` and two blur pairs. Bloom starts disabled and zero
intensity bypasses its work. Scratch targets are retained and allocated only
when needed.

`renderer.postEffects` is an ordered `List<PostEffect>`, where
`PostEffect = Func<Texture, RenderTarget, bool>`. The callback receives the
current linear HDR image and a distinct destination. Fully overwrite it and
return `true` to publish, or `false` to bypass. Keep output linear. Resource
arguments are explicit; captures remain plain values. Staged views can bind
additional game resources through ordinary shader parameters: between
`PostPrepare` and `Present`, read `renderer.output`, obtain
`NextPostTarget(renderer)`, draw and call `CommitPost(renderer)`.
`defaultPresent = false` suppresses only the stock final presentation.

## Current scope

One camera; static and skinned mesh assets with material maps and scalar values.
No `LightMode`, baked or area lights, environment specular,
GI, probes or light baker in this slice.
Model texture and metallic/roughness import is not automatic.

The package requests RGBA16F, sums direct light in linear HDR and adds nonnegative
emission once, even with zero lights. Optional diffuse ambient contributes
`albedo * (1 - metallic) * ambientIrradiance * occlusion / Math.PI`.
AO only affects this term, not direct lights or emission. This supplies no
environment specular or automatic occlusion bake.
It then applies linear exposure, Reinhard tone mapping and one sRGB encode.
Base-map filtering currently happens in display space before decoding; it is not
automatic hardware sRGB sampling.
The engine's existing float-target fallback on older OpenGL/WebGL devices uses
RGBA8 and clips HDR before tone mapping. Broader rendering features remain future
work.
