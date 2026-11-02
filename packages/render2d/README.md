# Render2D

Optional pure Tide rendering for elevated XY sprites, normal-mapped materials,
directional/point/spot lights, cutout shadow maps, screen-space Radiance Cascades,
linear HDR presentation and optional bloom. No Render3D dependency.

```sh
tide add render2d
```

The dependency is `render2d`; the namespace is `Render2D`. Add `SpriteRenderer`
and `Material` to match entities; `Transform.localPosition.z` is elevation.
Material textures are attached local fields. Lights may be match or local
entities. Configure the local `Renderer` before `Stages.Prepare` and draw HUDs
after `Stages.Present`.

```csharp
scene Main;

event(Spawned) Build(with Main)
{
    Spawn(Transform, Render2D.SpriteRenderer { size = float2(12, 8) },
        Render2D.Material { baseColor = Color(0.4, 0.45, 0.5) });
    Spawn(Transform { localPosition = float3(0, 0, 1) },
        Render2D.SpriteRenderer { size = float2(2) }, Render2D.Material);
    Spawn(Transform { localPosition = float3(-3, 2, 5) },
        Render2D.PointLight { intensity = float3(90, 60, 30), range = 20, castShadows = true });
}
```

See the [API guide](https://tide-engine.dev/packages/render2d), the
[interactive example](../../docs/examples/render2d.tide), and
[implementation/validation notes](../../notes/render2d.md).

The first version supports opaque and alpha-tested sprites. It has no blended
transparent layers, height-map displacement, isometric billboards or artist
sort-order field. Actual quad elevation drives direct shadows. Normal maps
use Raw numeric channels, red toward sprite-right and green toward sprite-up.

GI uses only the topmost visible surfaces in a bounded screen-space grid,
with thin horizontal patch intersections and two-sided diffuse source radiance.
It supplies one diffuse bounce and loses hidden, offscreen and sub-grid geometry;
normal response is reconstructed from first-order spherical harmonics. It is
not full-scene volumetric GI. Keep sprites horizontal for this representation.
Floating-point render targets are required for the material/position buffers.

Disable `Renderer.enabled` before Prepare to release retained resources.
`GlobalIllumination.enabled` and `Renderer.bloom.enabled` control the optional
passes separately. GI and all GPU state remain outside simulation, networking
and hashes. Desktop OpenGL/Vulkan and WebGL2/WebGPU use the same package shaders.
