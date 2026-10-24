# Render3D

Optional opaque 3D rendering with mesh/material components, directional, point
and spot lights, a shadow atlas, linear PBR lighting and HDR tone mapping. The
package owns its lighting, shadow, color and presentation passes. Its namespace
is `Render3D`. There is no implicit light: spawn light entities to light a surface;
without them, only material emission contributes.

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

## Components and settings

- `MeshVertex`: position, normal, UV and color (white by default).
- `MeshRenderer`: `Asset<Mesh3D> mesh`, `visible = true` and
  `castShadows = true`.
- `Material`: display-space `baseColor` (white), `metallic` (0), `roughness`
  (0.5), linear HDR `emission` (0), and `receiveShadows = true`. Attached local
  `baseMap` defaults to the package's white texture; attached local `shader`
  defaults to `Render3D.Lit { surface = Render3D.StandardSurface }`.

Put `MeshRenderer` and `Material` on **match entities**, using ordinary `Spawn`.
Both are required to draw or cast shadows. Attached local resource fields prevent
local-world material placement. Meshes follow the stored `Transform` hierarchy;
textures and shader bindings stay machine-local.

The per-machine local singleton `Renderer` has `enabled = true`, `camera`,
display-space `background = Color(0.06, 0.08, 0.12)` and linear-multiplier
`exposure = 1`. `Camera` defaults to position `(0,2,-5)`, identity rotation,
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

Shadows default on. `maxShadowMaps = 8` clamps to 0–16 shadow views, assigned in
retained light order. Directional/spot lights cost one; point lights require six
contiguous views together or none. A rejected shadow request still illuminates;
later requests can use remaining capacity. `shadowNear = 0.05` (at least 0.001)
is the point/spot near plane; a range no larger than this cannot cast shadows.
`unshadowedLights` counts retained lights requesting shadows but receiving none,
including when `shadows = false`.

Directional volumes share `shadowCenter = float3(0)`, `shadowExtent = 10`
(half-width/half-height) and `shadowDistance = 40` (depth), in world units.
They do not automatically follow the camera. Spot shadows follow the cone;
point shadows use six faces. Outside-volume receivers are unshadowed.
`shadowBias = 0.0005` and `shadowSlopeBias = 0.002` are normalized comparison-depth
offsets shared by all lights.

`shadowResolution = 1024` requests each atlas cell's pixel size, including a
two-pixel gutter on every edge. The smallest square grid fitting the views clamps
cells to `16..floor(2048 / gridSize)`, keeping the atlas at most 2048 per side.
Derived `activeShadowResolution` reports actual cell size including gutters, and
`atlasSize` the atlas side length. With no shadow views, the atlas keeps its size (initially
16×16), clears to far depth and reports `activeShadowResolution = 0`.
`shadows = false` skips shadow drawing/sampling while retaining a valid sampler.

The package retains its HDR and depth targets and preserves the depth resource
ID across atlas resizing. Disabling before preparation disposes them, and
re-enabling recreates them. `Render3D.Dispose(renderer)` releases them explicitly.
Call it before `Stages.Prepare`: disposing after `Present` still invalidates that
frame's recorded draws and presentation.

## Ordering and custom surfaces

The package views run `Prepare → Lights → Shadows → Color → Opaque → Present`, from
`Render3D.Stages`. All casters finish before color rendering. Configure camera,
lighting and attached local material fields in `[Before(Render3D.Stages.Prepare)]`
views; draw window UI in `[After(Render3D.Stages.Present)]` views.

Assign `material.shader = Render3D.Lit { surface = CustomSurface }` in a view.
The callback is `Func<SurfaceInput, mut Surface, void>`: input contains `world`,
`normal`, `uv` and `viewDirection`; the initialized surface contains linear
`albedo`, world-space `normal`, `roughness`, `metallic` and linear HDR `emission`.
Owned plain-value captures work; resource and heap captures do not. Callbacks
change appearance, not shadow geometry; no custom vertex-deformation API is
offered here. Compatible shader/mesh/texture/uniform/state draws can batch;
different materials need not share a draw.

## Current scope

One camera; opaque static mesh assets with a base map, base color,
metallic/roughness and emission. No `LightMode`, baked or area lights, cascades,
transparency, skinning, normal/mask maps, GI, probes or light baker in this slice.
Model texture and metallic/roughness import is not automatic.

The package requests RGBA16F, sums direct light in linear HDR and adds nonnegative
emission once, even with zero lights. There is no ambient/indirect approximation.
It then applies linear exposure, Reinhard tone mapping and one sRGB encode.
Base-map filtering currently happens in display space before decoding; it is not
automatic hardware sRGB sampling.
The engine's existing float-target fallback on older OpenGL/WebGL devices uses
RGBA8 and clips HDR before tone mapping. Broader rendering features remain future
work.
