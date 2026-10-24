---
description: "Use the optional Render3D package for opaque mesh materials, directional shadows, linear PBR lighting and HDR tone mapping."
---

# Render3D

`render3d` is an optional package for opaque 3D meshes. Add mesh and material components to match entities; the package's views draw their shadows, light them into an HDR target and present the result. This first slice has one camera, one directional sun and one shadow volume.

## Add the package

In a checkout or installation containing the package:

```sh
tide add render3d
```

Its line in `tide.packages` is `render3d`, and its namespace is `Render3D`. Games that omit the package compile and ship none of it. The engine's [drawing APIs](../game/3d.md) remain available without it.

## Meshes and materials

Import a static mesh with the package's vertex type, then give an entity both components:

```csharp
asset Mesh<Render3D.MeshVertex> Model = "art/model.glb";

scene Main;

event(Spawned) Build(with Main)
{
    Spawn(Transform,
        Render3D.MeshRenderer { mesh = Model },
        Render3D.Material { baseColor = Color(0.98, 0.75, 0.14), roughness = 0.6 });
}
```

`art/model.glb` is a file supplied by the game. See [Mesh assets](../game/assets.md#meshes) for supported imports. `Render3D.MeshVertex` contains `float3 position`, `float3 normal`, `float2 uv` and `Color color`, which defaults to white. The package places the mesh with the entity's `Transform`, including its parents.

These are **match entities**: create them with ordinary `Spawn`, not `local Spawn`. `Material` contains attached local resource fields, which currently prevent placing it on local-world entities. Its texture and shader stay on each machine, outside match snapshots and hashes; its ordinary material fields and the mesh asset handle are match data. See [Local fields](../game/local-state.md#local-fields).

Both `MeshRenderer` and `Material` must be present for an entity to draw or cast a shadow. Their fields are:

| Component field | Default | Meaning |
|---|---|---|
| `MeshRenderer.mesh` | No asset | `Asset<Mesh<Render3D.MeshVertex>>` to draw |
| `MeshRenderer.visible` | `true` | Draw the mesh and allow it to cast shadows |
| `MeshRenderer.castShadows` | `true` | Include the visible mesh in the shadow pass |
| `Material.baseColor` | `Color.white` | Display-space RGB tint |
| `Material.metallic` | `0` | Metallic weight, from 0 to 1 |
| `Material.roughness` | `0.5` | Surface roughness; lighting clamps it to 0.05–1 |
| `Material.emission` | `float3(0)` | Linear HDR emission, which may exceed 1 |
| `Material.receiveShadows` | `true` | Apply the sun's shadow to this surface |
| `Material.baseMap` | `Render3D.White` | Attached local `Texture`, white by default |
| `Material.shader` | `Render3D.Lit { surface = Render3D.StandardSurface }` | Attached local shader binding |

The base map, base color and vertex color each supply display-space RGB. The shader decodes each to linear light before multiplying them into the surface's albedo. Alpha is ignored in this opaque path. Assign a different `baseMap` or `shader` through a mutable `Material` parameter in a view before preparation.

## A complete example

::: tide-example render3d Mesh materials, sunlight and shadows

<<< @/examples/render3d.tide

:::

## Camera and renderer

`Render3D.Renderer` is a local singleton on each machine. Set its camera, sun and other settings in a view ordered before the package prepares the frame:

```csharp
[Before(Render3D.Stages.Prepare)]
view ConfigureRenderer(mut Render3D.Renderer renderer)
{
    renderer.camera.position = float3(0, 2, -5);
    renderer.camera.rotation = quaternion.LookRotation(float3(0, -1, 5), float3(0, 1, 0));
    renderer.camera.verticalFov = 60deg;
    renderer.sun.direction = float3(1, -2, 1);
    renderer.sun.radiance = float3(4);
    renderer.exposure = 1;
}
```

`Render3D.Camera` looks along its rotation's `+z`, with `+y` up. Its fields are:

| Field | Default | Units |
|---|---|---|
| `position` | `float3(0, 2, -5)` | World position |
| `rotation` | `quaternion.identity` | Camera rotation |
| `verticalFov` | `60deg` | **Radians**, as with `float4x4.PerspectiveFov`; a `deg` literal converts to radians |
| `near` | `0.1` | Distance to the near plane |
| `far` | `1000` | Distance to the far plane |

The `sun` is a `Render3D.DirectionalLight`. Its `direction` is the direction photons **travel**, defaulting to `float3(1, -2, 1)`. The package normalizes it. Its `radiance` is linear RGB, defaulting to `float3(4)`, independent of exposure.

| Renderer setting | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Run the package's renderer |
| `camera` | Defaults above | This machine's camera |
| `sun` | Defaults above | One directional light |
| `background` | `Color(0.06, 0.08, 0.12)` | Display-space background, decoded before rendering |
| `exposure` | `1` | Linear multiplier before tone mapping; 2 doubles the light |
| `shadows` | `true` | Cast and receive the sun's shadows |
| `shadowResolution` | `1024` | Square map size in pixels, clamped to 16–4096 |
| `shadowCenter` | `float3(0)` | World-space center of the shadow volume |
| `shadowExtent` | `10` | Half-width and half-height of the shadow volume, in world units |
| `shadowDistance` | `40` | Depth of the shadow volume, in world units |
| `shadowBias` | `0.0005` | Constant offset in normalized comparison depth |
| `shadowSlopeBias` | `0.002` | Additional normalized depth offset as the surface turns away from the light |

The shadow volume is orthographic and does not follow the camera automatically. Move `shadowCenter` with the area that needs shadows. Surfaces outside the volume receive full sunlight. Biases are comparison-depth offsets, not world distances; too little can produce self-shadowing, while too much separates shadows from their casters.

The package caches its window-sized `Format.Rgba16f` scene target and depth shadow target in `Renderer`, resizing as needed. Leave these resource and derived fields to the package. Setting `enabled = false` before preparation disposes the targets; re-enabling recreates them. `Render3D.Dispose(renderer)` also releases them explicitly; an enabled renderer recreates them on its next preparation. Setting `shadows = false` skips casting and shadow sampling, but retains the shadow target. An enabled renderer still presents its background when there is no match.

Call `Render3D.Dispose` before `Render3D.Stages.Prepare`. Recorded draws run after the views finish, so disposing after `Present` still invalidates that frame's rendering and presentation.

## Frame order and drawing UI

The package owns these named view stages, in order:

| Stage | Work |
|---|---|
| `Render3D.Stages.Prepare` | Prepare targets and the shadow camera |
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

Lighting uses a metallic-roughness BRDF with GGX specular, Smith visibility, Schlick Fresnel and Lambert diffuse. The sun and emission produce linear HDR RGB. Presentation multiplies by `exposure`, applies Reinhard tone mapping (`x / (1 + x)`) and encodes sRGB once. Keep callback emission and lighting values linear; do not encode them before presentation.

Base-map sampling currently filters display-space texels **before** decoding them to linear light. This can produce different edges and mip colors from linear-light filtering; automatic hardware sRGB sampling is not part of this slice. On devices unable to render into float targets, the engine's existing [RGBA8 fallback](../game/render-targets.md#what-a-target-holds) clips HDR values before tone mapping.

The current path draws opaque static mesh assets, which can move with their entity transforms. It has no point lights, cascades, transparency, skinning, normal or mask maps, GI, probes or light baker. There is one base map, base color, metallic/roughness pair and emission per material; model texture and metallic/roughness import is not automatic. Broader rendering features remain future work.
