# Global Illumination

Experimental realtime diffuse GI for the optional Render3D renderer. The package
derives its lighting field from visible scene geometry, captures direct diffuse
reflection and emission, and adds one diffuse bounce to surfaces. No authored
probe volumes are required.

This implementation is under development on `feat/render3d-gi`. It has not been
merged to `dev` or published in a Tide release.

## Add the package

In a checkout or installation containing this experimental package:

```sh
tide add globalillumination
```

Its dependency name is `globalillumination`, its namespace is
`GlobalIllumination`, and it depends on `render3d`. Adding it opts into GI;
games that omit it compile and ship none of its gathering or sampling code.
The package uses Tide passes and a small C helper for asset-cache invalidation.

Use the ordinary Render3D mesh, material and light components. GI starts enabled
when the package is present. Configure its local settings before preparation:

```csharp
[Before(Render3D.Stages.Prepare)]
view ConfigureGI(mut GlobalIllumination.Environment environment)
{
    environment.settings.probeSpacing = 1.5;
    environment.settings.maxProbes = 512;
    environment.settings.updatesPerFrame = 4;
}
```

## Controls

| `Environment.settings` field | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Enable GI on this machine; false releases its targets |
| `probeSpacing` | `1.5` | Requested world-space spacing, clamped to 0.1–1000 |
| `maxProbes` | `512` | Total probe budget, clamped to 125–2048 |
| `updatesPerFrame` | `4` | Complete probes refreshed per frame, clamped to 0–16; zero freezes updates |
| `intensity` | `1` | Received irradiance multiplier, clamped to 0–16 |
| `debug` | `false` | Show probe positions, gathered light and invalid probes |

Coverage is padded and aligned to a stable world-space lattice. The package
increases actual spacing when necessary to fit `maxProbes`. Geometry moving
inside the retained coverage does not rebuild the field. Expansion beyond it,
changed layout controls or an empty/repopulated scene can restart its lighting.
Local mesh bounds are cached per asset-pack generation; meshes are not scanned
vertex by vertex every frame.

Updates are progressive. A full refresh takes approximately
`ceil(environment.layout.total / updatesPerFrame)` rendered frames. Lower
spacing can preserve smaller spatial lighting features, while more probes
increase memory and refresh latency. Raising the update budget does more scene
capture and convolution work each frame. Zero freezes the current captured
lighting; changing lights or geometry will then leave it stale.

Each update reuses six 32×32 raster captures. The retained field stores
directional irradiance and visibility, rejects probes with excessive back-face
hits, and interpolates trustworthy visible neighbors. Debug markers are magenta
until captured, orange/red for rejected probes and otherwise shaded by gathered
irradiance. The package does not relocate rejected probes.

## Current limits

This is a bounded uniform-grid implementation. Large scene extents force
coarser spacing under the same probe budget; automatic coverage does not
promise uniform lighting detail across arbitrarily large worlds. Thin geometry,
small emitters and sparse valid probes can still cause missed light or leaks.
Bounds use authored rest geometry for skinned or custom-deformed meshes, so
large animation/deformation excursions are not automatically included.

Opaque and cutout static/skinned surfaces appear in captures. Transparent
surfaces receive GI but do not appear in the captured field. Surface callbacks
use the probe view direction; deformation callbacks retain the main eye.
Captures reuse selected lights and the main-camera shadow atlas, whose coverage
may need adjustment for surfaces visible from probes. Ambient irradiance,
previous GI and specular reflection do not feed captures. There is no glossy
GI, automatic sky lighting or light baker.

All settings, history and resources are local presentation data, outside match
snapshots and hashes. Disabling GI or Render3D releases GI targets; re-enabling
starts a fresh field. A GPU reset loses retained lighting, which refills as
updates run; frozen updates must resume to recover it.

Full guide: [Global Illumination](https://tide-engine.dev/packages/globalillumination).
