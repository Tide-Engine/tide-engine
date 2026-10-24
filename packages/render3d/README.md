# Render3D

Optional opaque 3D rendering with mesh/material components, one directional sun,
one shadow volume, linear PBR lighting and HDR tone mapping. The package owns
its shadow, color and presentation passes. Its namespace is `Render3D`.

## Using it

In a Tide checkout or installation containing the package:

```sh
tide add render3d
```

This writes `render3d` in the game's `tide.packages`. Games that omit it compile
and ship none of its code. Supply a mesh file such as `art/model.glb`:

```csharp
asset Mesh<Render3D.MeshVertex> Model = "art/model.glb";

scene Main;

event(Spawned) Build(with Main)
{
    Spawn(Transform,
        Render3D.MeshRenderer { mesh = Model },
        Render3D.Material { baseColor = Color(0.98, 0.75, 0.14), roughness = 0.6 });
}

[Before(Render3D.Stages.Prepare)]
view ConfigureRenderer(mut Render3D.Renderer renderer)
{
    renderer.camera.position = float3(0, 2, -5);
    renderer.camera.rotation = quaternion.LookRotation(float3(0, -1, 5), float3(0, 1, 0));
    renderer.camera.verticalFov = 60deg;
    renderer.sun.direction = float3(1, -2, 1);
    renderer.sun.radiance = float3(4);
}
```

Full API and runnable example: [Render3D](https://tide-engine.dev/packages/render3d).
The example's source is [docs/examples/render3d.tide](../../docs/examples/render3d.tide).

## Components and settings

- `MeshVertex`: position, normal, UV and color (white by default).
- `MeshRenderer`: `Asset<Mesh<MeshVertex>> mesh`, `visible = true` and
  `castShadows = true`.
- `Material`: display-space `baseColor` (white), `metallic` (0), `roughness`
  (0.5), linear HDR `emission` (0), and `receiveShadows = true`. Attached local
  `baseMap` defaults to the package's white texture; attached local `shader`
  defaults to `Render3D.Lit { surface = Render3D.StandardSurface }`.

Put both components on **match entities**, using ordinary `Spawn`. Attached local
resource fields currently prevent local-world material placement. Meshes follow
the entity's `Transform`; textures and shader bindings stay machine-local.

The per-machine local singleton `Renderer` has `enabled = true`, `camera`, `sun`,
display-space `background` and linear-multiplier `exposure = 1`. `Camera` defaults
to position `(0,2,-5)`, identity rotation, `verticalFov = 60deg` in **radians**,
`near = 0.1` and `far = 1000`. The sun's default `(1,-2,1)` direction means
photon travel direction; `radiance = float3(4)` is linear RGB.

Shadows default on: resolution 1024 (clamped to 16–4096), center `(0,0,0)`,
extent 10 (half-width/half-height) and distance 40 (depth), in world units.
`shadowBias = 0.0005` and `shadowSlopeBias = 0.002` are normalized comparison-depth
offsets. The volume does not automatically follow the camera. The package caches
its HDR and depth targets; disabling before preparation disposes them, and
re-enabling recreates them. `Render3D.Dispose(renderer)` releases them explicitly.
Call it before `Stages.Prepare`: disposing after `Present` still invalidates that
frame's recorded draws and presentation.

## Ordering and custom surfaces

The package views run `Prepare → Shadows → Color → Opaque → Present`, from
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

One camera, sun and shadow volume; opaque static mesh assets with a base map,
base color, metallic/roughness and emission. No point lights, cascades,
transparency, skinning, normal/mask maps, GI, probes or light baker in this slice.
Model texture and metallic/roughness import is not automatic.

The package requests RGBA16F, lights in linear HDR, then applies linear exposure,
Reinhard tone mapping and one sRGB encode. Base-map filtering currently happens
in display space before decoding; it is not automatic hardware sRGB sampling.
The engine's existing float-target fallback on older OpenGL/WebGL devices uses
RGBA8 and clips HDR before tone mapping. Broader rendering features remain future
work.
