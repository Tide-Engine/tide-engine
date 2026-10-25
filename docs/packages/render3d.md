---
description: "Use the optional Render3D package for opaque meshes, directional, point and spot lights, shadow atlases, linear PBR lighting and HDR tone mapping."
---

# Render3D

`render3d` is an optional package for opaque 3D meshes. Add mesh and material components to match entities, then spawn directional, point or spot lights. The package's views draw shadows, light the meshes into an HDR target and present the result through one camera. There is no implicit light: without light entities, only material emission lights a surface.

## Add the package

In a checkout or installation containing the package:

```sh
tide add render3d
```

Its line in `tide.packages` is `render3d`, and its namespace is `Render3D`. Games that omit the package compile and ship none of it. The engine's [drawing APIs](../game/3d.md) remain available without it.

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

`Render3D.Mesh3D` is a transparent [type alias](../language/namespaces.md#type-aliases) for `Mesh<Render3D.MeshVertex>`. With `using Render3D;`, the asset declaration can use `asset Mesh3D Cube = "art/cube.obj";`. See [Mesh assets](../game/assets.md#meshes) for supported imports. `MeshVertex` contains `float3 position`, `float3 normal`, `float2 uv` and `Color color`, which defaults to white. Meshes follow the entity's stored `Transform` hierarchy.

These are **match entities**: create them with ordinary `Spawn`, not `local Spawn`. `Material` contains attached local resource fields, which currently prevent placing it on local-world entities. Its texture and shader stay on each machine, outside match snapshots and hashes; its ordinary material fields and the mesh asset handle are match data. See [Local fields](../game/local-state.md#local-fields).

Both `MeshRenderer` and `Material` must be present for an entity to draw or cast a shadow. Their fields are:

| Component field | Default | Meaning |
|---|---|---|
| `MeshRenderer.mesh` | No asset | `Asset<Render3D.Mesh3D>` to draw |
| `MeshRenderer.visible` | `true` | Draw the mesh and allow it to cast shadows |
| `MeshRenderer.castShadows` | `true` | Include the visible mesh in the shadow pass |
| `Material.baseColor` | `Color.white` | Display-space RGB tint |
| `Material.metallic` | `0` | Metallic weight, from 0 to 1 |
| `Material.roughness` | `0.5` | Surface roughness; lighting clamps it to 0.05–1 |
| `Material.emission` | `float3(0)` | Linear HDR emission, which may exceed 1 |
| `Material.receiveShadows` | `true` | Apply available light shadows to this surface |
| `Material.baseMap` | `Render3D.White` | Attached local `Texture`, white by default |
| `Material.shader` | `Render3D.Lit { surface = Render3D.StandardSurface }` | Attached local shader binding |

The base map, base color and vertex color each supply display-space RGB. The shader decodes each to linear light before multiplying them into the surface's albedo. Alpha is ignored in this opaque path. Assign a different `baseMap` or `shader` through a mutable `Material` parameter in a view before preparation.

## A complete example

::: tide-example render3d Mesh materials, multiple lights and shadows

<<< @/examples/render3d.tide

:::

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
| `SurfaceInput.uv` | Mesh texture coordinates |
| `SurfaceInput.viewDirection` | Normalized direction from the surface toward the camera |
| `Surface.albedo` | Linear RGB reflectance |
| `Surface.normal` | World-space shading normal |
| `Surface.roughness`, `Surface.metallic` | Roughness and metallic weight |
| `Surface.emission` | Linear HDR emission |

Use `Render3D.SrgbToLinear` when supplying display-space colors inside a callback. Owned captures of plain values, including numeric structs, work with the existing [callable rules](../language/functions.md#function-values); resources and heap values cannot be captured. A callback changes surface appearance, not the geometry used for shadows. This package offers no custom vertex-deformation API.

## Lighting and current limits

Lighting uses a metallic-roughness BRDF with GGX specular, Smith visibility, Schlick Fresnel and Lambert diffuse. Direct contributions from all retained lights are summed; nonnegative emission is added once, including with zero lights. There is no ambient or indirect approximation. Presentation multiplies linear HDR RGB by `exposure`, applies Reinhard tone mapping (`x / (1 + x)`) and encodes sRGB once. Keep callback emission and lighting values linear; do not encode them before presentation.

Base-map sampling currently filters display-space texels **before** decoding them to linear light. This can produce different edges and mip colors from linear-light filtering; automatic hardware sRGB sampling is not part of this slice. On devices unable to render into float targets, the engine's existing [RGBA8 fallback](../game/render-targets.md#what-a-target-holds) clips HDR values before tone mapping.

The current path draws opaque static mesh assets, which can move with their entity transforms. It has no `LightMode`, baked or area lights, cascades, transparency, skinning, normal or mask maps, GI, probes or light baker. There is one base map, base color, metallic/roughness pair and emission per material; model texture and metallic/roughness import is not automatic. Broader rendering features remain future work.
