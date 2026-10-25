---
description: "Explore Render3D materials, normal and height maps, cutout shadows, sorted transparency, lights and linear HDR PBR with interactive examples."
---

# Render3D

`render3d` is an optional package for 3D meshes with opaque, cutout and transparent materials. Add mesh and material components to match entities, then spawn directional, point or spot lights. The package draws shadows, lights meshes into an HDR target and presents through one camera. Lights and ambient irradiance default to zero; with neither configured, only material emission contributes.

## Add the package

In a checkout or installation containing the package:

```sh
tide add render3d
```

Its line in `tide.packages` is `render3d`, and its namespace is `Render3D`. The package contains Tide rendering passes and a small C helper for transparent-object sorting. Games that omit the package compile and ship none of it. The engine's [drawing APIs](../game/3d.md) remain available without it.

## Meshes and materials

Supply a cube mesh at `art/cube.obj`, then spawn a white cube, a directional light and a spotlight:

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

`Render3D.Mesh3D` is a transparent [type alias](../language/namespaces.md#type-aliases) for `Mesh<Render3D.MeshVertex>`. With `using Render3D;`, write `asset Mesh3D Cube = "art/cube.obj";`. See [Mesh assets](../game/assets.md#meshes) for supported imports. `MeshVertex` contains `float3 position`, `float3 normal`, `float4 tangent`, `float2 uv` and `Color color`, which defaults to white. Meshes follow the entity's stored `Transform` hierarchy. Because this static mesh layout declares `float4 tangent`, its import generates missing UV tangents and splits vertices at opposing generated handedness seams. Layouts without tangents or with `float3 tangent`, and skinned meshes, retain their prior imports; see the [tangent import rules](../game/assets.md#meshes).

These are **match entities**: create them with ordinary `Spawn`, not `local Spawn`. `Material` contains attached local resource fields, which currently prevent placing it on local-world entities. Its texture and shader stay on each machine, outside match snapshots and hashes; its ordinary material fields and the mesh asset handle are match data. See [Local fields](../game/local-state.md#local-fields).

Both `MeshRenderer` and `Material` must be present for an entity to draw or cast a shadow. Their fields are:

| Component field | Default | Meaning |
|---|---|---|
| `MeshRenderer.mesh` | No asset | `Asset<Render3D.Mesh3D>` to draw |
| `MeshRenderer.visible` | `true` | Draw the mesh and allow it to cast shadows |
| `MeshRenderer.castShadows` | `true` | Include the visible mesh in the shadow pass |
| `Material.baseColor` | `Color.white` | Display-space RGB tint |
| `Material.mode` | `Render3D.SurfaceMode.Opaque` | Opaque, alpha-tested `Cutout`, or alpha-blended `Transparent` |
| `Material.alphaCutoff` | `0.5` | Cutout threshold, from 0 to 1 |
| `Material.doubleSided` | `false` | Draw both sides, turning the shading frame toward the viewed side |
| `Material.metallic` | `0` | Metallic weight, from 0 to 1 |
| `Material.roughness` | `0.5` | Surface roughness; lighting clamps it to 0.05–1 |
| `Material.emission` | `float3(0)` | Linear HDR emission, which may exceed 1 |
| `Material.normalScale` | `1` | Tangent-space normal map's XY strength |
| `Material.occlusionStrength` | `1` | Blend between no occlusion and the mask's red channel |
| `Material.heightScale` | `0` | Height-map UV parallax strength; 0 disables it |
| `Material.uvScale` | `float2(1)` | Texture-coordinate scale |
| `Material.uvOffset` | `float2(0)` | Texture-coordinate offset |
| `Material.receiveShadows` | `true` | Apply available light shadows to this surface |
| `Material.baseMap` | `Render3D.White` | Attached local `Texture`, white by default |
| `Material.normalMap` | `default` (empty) | Attached local `[Raw] Texture` for tangent-space normals; empty uses the geometric normal |
| `Material.maskMap` | `default` (empty) | Attached local `[Raw] Texture` for ORM; empty leaves occlusion at 1 and the material scalars unchanged |
| `Material.emissionMap` | `default` (empty) | Attached local display-space emission texture; empty uses `emission` directly |
| `Material.heightMap` | `default` (empty) | Attached local `[Raw] Texture` for numeric height; empty disables parallax |
| `Material.uv` | `Render3D.StandardUV` | Attached local callback adjusting UVs before parallax |
| `Material.coverage` | `Render3D.StandardCoverage` | Attached local callback adjusting alpha and cutoff |
| `Material.shader` | `Render3D.Lit { surface = Render3D.StandardSurface }` | Attached local shader binding |

The base map, base color and vertex color each supply display-space RGB. The shader decodes each to linear light before multiplying them into albedo. Their alpha channels multiply without color conversion. Assign maps, callbacks or `shader` through a mutable `Material` parameter in a view before preparation. Numeric material fields are match state: change them in match code or send a [command](../game/commands.md) from a local control. A captured surface callback can make local shading comparisons without changing match data.

## A complete example

::: tide-example render3d Mesh materials, multiple lights and shadows

<<< @/examples/render3d.tide

:::

Drag the scene to orbit, or tap the frame and use one finger to orbit. In these embedded examples, use the zoom buttons or a two-finger pinch; the wheel scrolls the documentation page. **Open separately** also enables wheel zoom. **Reset camera** restores the initial view; **Hide controls** reveals more of the scene. The example keeps slider drags out of the camera gesture.

## Material maps and ambient occlusion

| Map | Channels | How it combines with the material |
|---|---|---|
| `baseMap` | Display-space RGB, linear alpha | RGB multiplies decoded `baseColor` and vertex color; alpha multiplies their alpha |
| `normalMap` | Numeric RGB, centered at `(0.5, 0.5, 1)` | Tangent-space normal; XY is scaled by `normalScale` |
| `maskMap` | **R** occlusion, **G** roughness, **B** metallic | AO blends by `occlusionStrength`; G and B multiply the roughness and metallic scalars |
| `emissionMap` | Display-space RGB | Decoded to linear light, then multiplied by linear HDR `emission` |
| `heightMap` | Numeric R, centered at `0.5` | Offsets UVs according to camera direction and `heightScale` |

Only `baseMap` defaults to `Render3D.White`; all four optional maps start empty. Unused optional maps are not sampled, including an empty emission map on an emissive material. `Render3D.FlatNormal` is available for explicit assignment when a flat normal texture is wanted.

Normal, ORM and height images contain numbers; color compression or sRGB decoding would change their meaning. Base and emission maps contain display-space color. A white mask leaves scalar values unchanged; `metallic = 0` or `emission = float3(0)` still gives no metallic or emission contribution even when a map is assigned.

`normalMap`, `maskMap` and `heightMap` are declared `[Raw] local Texture` fields. Bind `[Raw]` image assets so all channels reach the shader as numbers, including where alpha is zero. These fields also accept another Raw texture field, a `[Raw] Grid2<Color>` field/asset or `default`; ordinary color textures are rejected. `baseMap` and `emissionMap` remain ordinary color textures.

```csharp
asset Texture paint = "art/paint.png";
[Raw] asset Texture bumps = "art/normal.png";
[Raw] asset Texture packed = "art/orm.png";
[Raw] asset Texture heights = "art/height.png";

[Before(Render3D.Stages.Prepare)]
view BindMaps(mut Render3D.Material material)
{
    material.baseMap = paint;
    material.normalMap = bumps;
    material.maskMap = packed;
    material.heightMap = heights;
}
```

The field annotation controls numeric sampling, including through GPU texture helpers. It does not create a new texture type or let a Raw image pass through an ordinary `Texture` local, return value or draw argument. See [Textures that are numbers](../game/shaders.md#textures-that-are-numbers) for the complete binding rules.

Set `Renderer.ambientIrradiance` to a nonnegative **linear** RGB irradiance to see diffuse ambient light. Its default is zero. The contribution is `albedo * (1 - metallic) * ambientIrradiance * occlusion / Math.PI`. AO affects this diffuse indirect term; it does not darken direct lights or emission. There is no environment specular reflection or automatic occlusion bake. With zero ambient irradiance, an AO slider has no lighting contribution to change.

Height mapping is a single-sample UV parallax offset. It changes the sampled pattern as you orbit; it does not move vertices, change depth, create a new silhouette or displace shadow geometry. Use modest scales, especially near grazing angles. This is not tessellation, relief mapping or refraction.

::: tide-example render3d-materials Normal, height, ORM, emission, cutout shadows and blending

<<< @/examples/render3d-materials.tide

:::

Choose **PBR maps**, **Cutout** or **Blend** to move between the exhibits. In PBR maps, compare zero/positive normal and height strengths, turn ORM off, then vary roughness, metallic, AO and emission. AO is visible because this demo explicitly supplies ambient irradiance. The height slider changes the material through an ordinary targeted command. In Cutout, adjust the cutoff and watch the leaf shadow change with it. Orbit behind the leaves to see both sides. In Blend, adjust opacity and orbit around the overlapping cards and opaque sphere. The demo's original PNGs and quad are reproducible with `python docs/examples/assets/render3d/make_materials.py` in a checkout.

## Cutout, transparency and sidedness

`SurfaceMode.Opaque` ignores alpha for visibility and writes depth. `Cutout` discards fragments whose multiplied alpha is below `alphaCutoff`; retained fragments draw opaquely and write depth. Its shadow pass uses the same UV, height, alpha and coverage rules, so holes also cut the shadow. Use cutout for leaves, fences and other surfaces with holes.

`Transparent` draws after opaque and cutout geometry, with source-alpha blending and depth testing, without writing depth. Transparent objects sort back to front for the camera; transparent materials do not cast shadows. This is object sorting, not per-triangle sorting: intersecting cards, nested surfaces and a mesh whose triangles overlap itself can blend incorrectly. Separate meshes can help when an object has distinct layers. Transparency supplies no refraction or physically modeled glass transmission.

`doubleSided = true` disables back-face culling and flips the shading frame for the viewed back side. It is useful for a thin leaf or card. A closed transparent mesh can expose both front and back surfaces; object sorting does not solve ordering inside that mesh.

## Camera and renderer

`Render3D.Renderer` is a local singleton on each machine. Configure it in a `[Before(Render3D.Stages.Prepare)]` view, as in the cube example above.

`Render3D.Camera` looks along its rotation's `+z`, with `+y` up. Its fields are:

| Field | Default | Units |
|---|---|---|
| `position` | `float3(0, 2, -5)` | World position |
| `rotation` | `quaternion.identity` | Camera rotation |
| `verticalFov` | `60deg` | **Radians**, as with `float4x4.PerspectiveFov`; a `deg` literal converts to radians |
| `near` | `0.1` | Distance to the near plane |
| `far` | `1000` | Distance to the far plane |

| Renderer setting | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Run the package's renderer |
| `camera` | Defaults above | This machine's camera |
| `background` | `Color(0.06, 0.08, 0.12)` | Display-space background, decoded before rendering |
| `exposure` | `1` | Linear multiplier before tone mapping; 2 doubles the light |
| `ambientIrradiance` | `float3(0)` | Linear diffuse ambient irradiance, multiplied by material AO |
| `maxLights` | `32` | Maximum retained lights; negative values act as 0 |
| `shadows` | `true` | Enable shadow casting and sampling |
| `maxShadowMaps` | `8` | Shadow-view budget, clamped to 0–16 |
| `shadowResolution` | `1024` | Requested atlas cell size in pixels, including gutters |
| `shadowCenter` | `float3(0)` | World-space center of directional shadow volumes |
| `shadowExtent` | `10` | Half-width and half-height of directional shadow volumes, in world units |
| `shadowDistance` | `40` | Depth of directional shadow volumes, in world units |
| `shadowNear` | `0.05` | Point/spot shadow near plane, in world units; at least 0.001 |
| `shadowBias` | `0.0005` | Constant offset in normalized comparison depth |
| `shadowSlopeBias` | `0.002` | Additional normalized depth offset as the surface turns away from the light |

The package retains its window-sized `Format.Rgba16f` scene target and depth atlas in `Renderer`, resizing as needed. The depth resource ID survives atlas resizing. Setting `enabled = false` before preparation disposes them; re-enabling recreates them. `Render3D.Dispose(renderer)` also releases them explicitly; an enabled renderer recreates them on its next preparation. An enabled renderer still presents its background when there is no match.

Leave the resource and derived fields to the package. Call `Render3D.Dispose` before `Render3D.Stages.Prepare`. Recorded draws run after the views finish, so disposing after `Present` still invalidates that frame's rendering and presentation.

## Light components

`DirectionalLight`, `PointLight` and `SpotLight` can belong to match entities (`Spawn`) or machine-local entities (`local Spawn`). All three default to `enabled = true` and `priority = 0`. Directional lights default to `castShadows = true`; point and spot lights default to `false`.

| Component field | Default | Meaning |
|---|---|---|
| `DirectionalLight.direction` | `float3(1, -2, 1)` | Local direction photons travel |
| `DirectionalLight.radiance` | `float3(4)` | Linear RGB light, independent of distance |
| `PointLight.intensity`, `SpotLight.intensity` | `float3(10)` | Linear RGB light at one world unit, before range/cone attenuation |
| `PointLight.range`, `SpotLight.range` | `10` | Finite range in world units; nonpositive values disable the light |
| `SpotLight.direction` | `float3(0, 0, 1)` | Local cone axis, pointing away from the light |
| `SpotLight.innerAngle` | `25deg` | Full cone opening at full strength |
| `SpotLight.outerAngle` | `40deg` | Full outer cone opening |

Positions and directions come from `World.LocalToWorld`: the stored transform hierarchy, without presentation interpolation. The matrix transforms local directions, including its scale, then the package normalizes them. Without a transform on the entity or its ancestors, the matrix is identity. Light ranges remain in world units.

Radiance and intensity clamp negative channels to zero. Point and spot lights use inverse-square falloff with a 0.01-world-unit distance floor and a smooth window that reaches zero at `range`. Their intensity describes the unwindowed light at one world unit. Spot angles are **radians**; `deg` literals convert to radians. `outerAngle` clamps to 1–179 degrees and `innerAngle` to 0–`outerAngle`. Light fades smoothly between the cones; equal angles give a hard edge.

Each frame, the renderer keeps up to `maxLights` lights in descending `priority`. Equal priorities retain collection order: directionals, then spots, then points, with match entities before local entities within each kind. `Renderer.droppedLights` counts otherwise eligible lights excluded by this limit. There is no `Renderer.sun` field; configure a directional light through its component.

## Shadows

Shadow allocation follows the retained light order. A directional or spot light uses one atlas cell; a point light needs six contiguous cells, allocated together or not at all. A shadow request that cannot fit in `maxShadowMaps` still illuminates without shadows; later requests can use the remaining cells. Point/spot lights whose range does not exceed the effective shadow near plane also illuminate without shadows. `Renderer.unshadowedLights` counts retained lights requesting shadows that receive none, including when shadows are globally disabled.

Directional shadow volumes are orthographic and share `shadowCenter`, `shadowExtent` and `shadowDistance`. They do not follow the camera automatically. Move `shadowCenter` with the area that needs shadows. Spot shadow volumes follow their cone; point shadows cover six faces. Surfaces outside a shadow view are unshadowed by that view. The global biases are comparison-depth offsets, not world distances; too little can produce self-shadowing, while too much separates shadows from their casters.

The atlas uses the smallest square grid that fits the allocated views. Each cell has a two-pixel gutter on every edge, so its usable interior is `cellSize - 4` pixels per side. `shadowResolution` clamps to 16 through `floor(2048 / gridSize)`; the atlas never exceeds 2048 pixels per side. `Renderer.activeShadowResolution` reports the actual cell size, including gutters; `Renderer.atlasSize` reports the atlas side length. For example, eight views use a 3×3 grid: a request of 1024 yields 682-pixel cells in a 2046-pixel atlas.

With no allocated shadow views, including `shadows = false`, the renderer retains the atlas size (initially 16×16), clears it to far depth and reports `activeShadowResolution = 0`. The depth resource remains valid for the shader's bound sampler. Disabling shadows skips shadow drawing and sampling; lights and emission still render.

## Frame order and drawing UI

The package owns these named view stages, in order:

| Stage | Work |
|---|---|
| `Render3D.Stages.Prepare` | Prepare retained targets and reset per-frame light data |
| `Render3D.Stages.Lights` | Collect and select lights, allocate shadow views and clear the atlas |
| `Render3D.Stages.Shadows` | Draw all visible shadow casters |
| `Render3D.Stages.Color` | Set the scene camera and clear the HDR target |
| `Render3D.Stages.Opaque` | Draw opaque meshes with their materials |
| `Render3D.Stages.Transparent` | Draw sorted transparent meshes after opaque/cutout geometry |
| `Render3D.Stages.Present` | Apply exposure, tone map and present to the window |

Every caster finishes before color drawing starts. Configure camera, lighting, textures and shader bindings with `[Before(Render3D.Stages.Prepare)]`. Draw ordinary window UI with `[After(Render3D.Stages.Present)]`:

```csharp
[After(Render3D.Stages.Present)]
view Hud()
{
    Draw.Text("Render3D", float2(0, 160), 20, Color.white);
}
```

Ordinary compatible mesh draws can batch when their shader, mesh, texture, uniforms and draw state match. Different material values can split batches; the package does not put every material into one draw.

## Custom surfaces

`Render3D.Lit` takes a `Func<Render3D.SurfaceInput, mut Render3D.Surface, void>` callback. The shader initializes the surface from the material, then calls it before evaluating lighting. The default `Render3D.StandardSurface` leaves it unchanged.

```csharp
void StripedSurface(Render3D.SurfaceInput input, mut Render3D.Surface surface)
{
    float stripe = Math.Step(0.5, Math.Frac(input.uv.x * 8));
    surface.albedo *= Math.Lerp(0.35, 1, stripe);
}

[Before(Render3D.Stages.Prepare)]
view ConfigureMaterials(mut Render3D.Material material)
{
    material.shader = Render3D.Lit { surface = StripedSurface };
}
```

| Callback data | Meaning |
|---|---|
| `SurfaceInput.world` | World-space position |
| `SurfaceInput.normal` | World-space geometric normal |
| `SurfaceInput.tangent`, `SurfaceInput.bitangent` | World-space tangent frame, including transform/UV handedness |
| `SurfaceInput.uv` | Scaled/offset UVs for the UV hook; final UVs, including parallax, for coverage and surface |
| `SurfaceInput.viewDirection` | Normalized direction from the surface toward the camera |
| `Surface.albedo` | Linear RGB reflectance |
| `Surface.normal` | World-space shading normal |
| `Surface.roughness`, `Surface.metallic` | Roughness and metallic weight |
| `Surface.emission` | Linear HDR emission |
| `Surface.occlusion` | Ambient diffuse occlusion, default 1 |

`Material.uv` is an attached local `Func<Render3D.SurfaceInput, mut float2, void>` and runs after scale/offset, before height parallax. `Material.coverage` is an attached local `Func<Render3D.SurfaceInput, mut Render3D.Coverage, void>` and runs after alpha sampling; `Coverage` contains `alpha = 1` and `cutoff = 0.5`. The package initializes those from the material and multiplied alpha before invoking the hook. The same hooks run for cutout shadows. A surface callback runs after the maps initialize `Surface`; it cannot change fragment coverage.

Use `Render3D.SrgbToLinear` when supplying display-space colors inside a callback. Owned captures of plain values, including numeric structs, work with the existing [callable rules](../language/functions.md#function-values); resources and heap values cannot be captured. The original `Lit { surface = CustomSurface }` form remains valid. Callbacks change appearance and coverage; the package offers no custom vertex-deformation API.

## Lighting and current limits

Lighting uses a metallic-roughness BRDF with GGX specular, Smith visibility, Schlick Fresnel and Lambert diffuse. Direct contributions from all retained lights are summed with optional diffuse ambient irradiance; nonnegative emission is added once, including with zero lights. Presentation multiplies linear HDR RGB by `exposure`, applies Reinhard tone mapping (`x / (1 + x)`) and encodes sRGB once. Keep callback emission and lighting values linear; do not encode them before presentation.

Base-map sampling currently filters display-space texels **before** decoding them to linear light. This can produce different edges and mip colors from linear-light filtering; automatic hardware sRGB sampling is not part of this slice. On devices unable to render into float targets, the engine's existing [RGBA8 fallback](../game/render-targets.md#what-a-target-holds) clips HDR values before tone mapping.

The current path draws static mesh assets, which can move with their entity transforms. It has no `LightMode`, baked or area lights, cascades, skinning, environment specular, GI, probes or light baker. Model texture and metallic/roughness import is not automatic: declare and bind the maps explicitly. Height is UV parallax and transparency is sorted alpha blending, subject to the limits above.
