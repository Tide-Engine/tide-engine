---
description: "Light elevated 2D sprites with normal maps, directional/point/spot lights, cutout shadows, Radiance Cascades and bloom."
---

# Render2D

`render2d` supplies a sprite renderer with real elevation, normal-mapped materials, direct lights, shadow maps, optional screen-space Radiance Cascades and HDR bloom. It is a separate, pure Tide package with no Render3D dependency.

## Add the package

```sh
tide add render2d
```

The dependency name is `render2d`; its namespace is `Render2D`. Games that omit it compile and ship none of it. The engine's ordinary [drawing](../game/views.md) and [shader](../game/shaders.md) APIs remain available.

## Sprites and elevation

World X points right, Y points up, and positive Z is elevation toward the camera. The orthographic camera looks down the Z axis. A floor and a raised sprite are actual thin XY quads: light can pass underneath a raised sprite, and its shadow can land on the floor away from its footprint.

```csharp
scene Main;

event(Spawned) Build(with Main)
{
    Spawn(Transform,
        Render2D.SpriteRenderer { size = float2(12, 8) },
        Render2D.Material { baseColor = Color(0.35, 0.4, 0.45), roughness = 0.8 });
    Spawn(Transform { localPosition = float3(0, 0, 1) },
        Render2D.SpriteRenderer { size = float2(2) },
        Render2D.Material { baseColor = Color(0.9, 0.5, 0.2) });
    Spawn(Transform { localPosition = float3(-3, 2, 5) },
        Render2D.PointLight { intensity = float3(90, 60, 30), range = 20, castShadows = true });
}

[Before(Render2D.Stages.Prepare)]
view Camera(mut Render2D.Renderer renderer)
{
    renderer.camera.verticalSize = 10;
}
```

Put `SpriteRenderer` and `Material` together on match entities using ordinary `Spawn`. Their numeric fields are match data. The material's attached local texture fields live on each machine and are excluded from snapshots and hashes. Those attached fields currently prevent placing `Material` on a local entity. Lights can be match or local entities.

`SpriteRenderer.size` defaults to `(1,1)`, `uvRect` to the whole texture, and `visible` and `castShadows` to `true`. `uvRect` uses normalized texture coordinates; animate a sheet by selecting another rectangle. Ordinary `Transform` hierarchy placement, scale, rotation around Z and mirroring apply to the sprite and its normal frame. Elevation also determines depth visibility: there is no independent painter-order field in this first version. Avoid coplanar overlapping opaque sprites.

The first version has `SurfaceMode.Cutout` (the default) and `Opaque`. Cutout removes pixels below `alphaCutoff` in both color and shadow passes. It does not blend translucent pixels. Texture height maps, displaced sprite geometry, alpha-blended layers and isometric billboard placement are not included.

## Materials

| Field | Default | Meaning |
|---|---|---|
| `baseColor` | White | Display-sRGB tint, multiplied by the decoded base texture |
| `roughness` | `0.5` | GGX roughness, clamped to 0.05–1 |
| `metallic` | `0` | Metallic weight, clamped to 0–1 |
| `specular` | `1` | Specular weight; zero gives a diffuse reference material |
| `emission` | `(0,0,0)` | Linear RGB emission; values above one are allowed |
| `alphaCutoff` | `0.5` | Cutout threshold |
| `normalScale` | `1` | Strength of the normal map's XY perturbation |
| `receiveShadows` | `true` | Apply available direct-light shadows |
| `baseMap` | `Render2D.White` | Attached local color texture |
| `normalMap` | Empty | Attached local `[Raw] Texture`; empty uses the flat normal |
| `emissionMap` | Empty | Attached local color texture multiplied by emission; empty keeps scalar emission |

Base and emission texture RGB are display sRGB; normal channels are numeric. Flat normals are `(0.5,0.5,1)`, with red pointing sprite-right and green sprite-up. Rotating or mirroring a sprite transforms the normal frame too. Bind maps from a view before Prepare:

```csharp
asset Texture art = "art/sprite.png";
[Raw] asset Texture normal = "art/sprite-normal.png";

[Before(Render2D.Stages.Prepare)]
view Maps(mut Render2D.Material material)
{
    material.baseMap = art;
    material.normalMap = normal;
}
```

Raw assets must stay in Raw fields; see [numeric textures](../game/meshes-and-textures.md). Normal maps change lighting, not the sprite's silhouette or geometric elevation.

## Lights and shadows

`DirectionalLight`, `PointLight` and `SpotLight` have `enabled = true` and `priority = 0`. Larger priorities receive the light and shadow budgets first. Positions come from `Transform`; directions are transformed from local space.

| Light | Main fields | Default shadows |
|---|---|---|
| Directional | Photon-travel `direction = (-1,-1,-2)`, linear `radiance = (4,4,4)` | On |
| Point | Linear `intensity = (10,10,10)`, `range = 10` | Off |
| Spot | Point-light fields, `direction = (0,0,-1)`, full `innerAngle = 25deg`, `outerAngle = 40deg` | Off |

Point/spot intensity is the incident light at one world unit before the smooth range cutoff. The package evaluates lights in linear HDR, using diffuse and GGX specular terms. With no lights or ambient, only emission contributes.

`Renderer.maxLights` defaults to 32. `maxShadowMaps` defaults to 8 and counts views: a directional or spot light uses one, and a point light needs six together. Two shadowed point lights therefore need at least 12. `shadowResolution` defaults to 512 pixels per atlas cell; the actual resolution can shrink to fit the device/atlas budget. `droppedLights`, `unshadowedLights` and `activeShadowResolution` expose the result.

Directional shadows use the configured `shadowCenter`, `shadowExtent` and `shadowDistance`. Keep that volume around the sprites that should cast and receive them. `shadowBias` and `shadowSlopeBias` control self-shadow bias. Material cutouts also cut the shadow map. Invisible sprites do not cast.

## Radiance Cascades

The local `Render2D.GlobalIllumination` singleton controls a surface Radiance Cascades pass. It uses the topmost visible surfaces' positions and outgoing diffuse light plus emission, calculates growing distance intervals at decreasing spatial and increasing directional resolution, then merges those intervals. The result supplies one diffuse indirect bounce. It is rebuilt each frame, with no temporal history or noisy sampling.

This is height-aware screen-space GI. Rays intersect thin patches at their stored elevations rather than filling every sprite's footprint down to the floor. The source is treated as two-sided diffuse sheet radiance. It can light visible lower surfaces beside an elevated emitter; it cannot recover surfaces hidden behind another sprite or outside the camera. Small geometry can disappear in the reduced-resolution lighting representation. Keep sprites horizontal for this GI representation; pitched or rolled cards are not faithfully represented by one height per texel.

`enabled` switches the pass off without affecting direct lighting. `resolution` bounds the surface grid's longest side (32–256); `probeSpacing` sets the spacing in that grid (1–16). `cascades` selects 1–4 interval levels, `distance` sets the world-space reach, `intensity` scales the indirect contribution, and `thickness` controls the self-hit bias and height tolerance for interpolation. Derived `activeResolution`, `activeProbes` and `activeCascades` report the configuration used. The final diffuse field uses first-order spherical harmonics, so directional detail is softer than direct normal-mapped lighting.

Use a 128-pixel GI grid as a starting point, then inspect the smallest emitters and occluders in the actual game. More resolution and closer probes cost more GPU time and memory. This package does not require compute shaders or hardware ray tracing, and does not claim full-scene or multi-bounce transport.

## Camera, HDR and bloom

The local `Renderer.camera` has `position` (XY center), `verticalSize = 12`, `minHeight = -100` and `maxHeight = 100`. The horizontal extent follows the window aspect. The scene buffers keep XY relative to the camera to reduce half-float precision loss. Z is stored in half float: use modest elevations for fine height differences.

`Renderer.exposure` defaults to one and `ambientIrradiance` to zero. Lighting stays linear until exposure, Reinhard tone mapping and a single sRGB conversion at presentation. Base-map filtering currently happens in display space before decoding. The renderer needs working floating-point render targets for its material data and GI; the engine's older-device RGBA8 fallback cannot preserve those numeric buffers.

Optional `renderer.bloom.enabled = true` extracts bright HDR values, blurs at quarter resolution and composites before tone mapping. Its defaults are `threshold = 1`, `softness = 0.5`, `intensity = 0.2` and `radius = 1`. Bloom is a glow effect; it contributes no scene illumination.

Configure before `Render2D.Stages.Prepare`; draw HUDs after `Stages.Present`. The package orders Prepare, Lights, Shadows, Color, Lighting, Indirect, PostProcess and Present. To handle presentation yourself, set `defaultPresent = false` and read `renderer.output` after PostProcess. Custom post passes must use a separate destination rather than reading and writing the same texture.

Targets are retained across frames and window-size changes. Setting `Renderer.enabled = false` before Prepare releases core, GI and bloom resources; re-enabling recreates them. These resources are local presentation state and never alter the match hash.

## Try the lighting

The garden uses raised copper plates, alpha-cutout foliage and emissive lanterns. Toggle normals, shadows, GI and bloom independently, and freeze the moving light to compare them. Its color and normal maps are generated by the checked-in `make_materials.py` beside the assets.

::: tide-example render2d Elevated sprites and indirect light
<<< @/examples/render2d.tide
:::
