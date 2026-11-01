---
description: "Add experimental automatic diffuse global illumination to Render3D with bounded probe counts, progressive updates and local quality controls."
---

# Global Illumination

`globalillumination` is an optional package depending on [Render3D](./render3d.md).
It gathers light reflected by lit surfaces and emitted by materials, then adds
one diffuse bounce to the scene. It derives coverage from rendered geometry;
games do not author GI boxes or place probe volumes.

::: info Experimental branch
This package is under development on `feat/render3d-gi`. It has not been merged
to `dev` or published in a Tide release. Its API and quality limits may change.
:::

## Add and configure

In a checkout or installation containing this experimental package:

```sh
tide add globalillumination
```

Its dependency line is `globalillumination` and its namespace is
`GlobalIllumination`. The dependency includes `render3d`. Adding the package
opts into GI; games omitting it compile and ship none of its field management,
capture passes or sampling implementation. Render3D's ordinary lights,
materials and presentation remain usable without it.

GI starts enabled when the package is present. Use the ordinary Render3D mesh,
material and light components, then configure the machine-local environment:

```csharp
[Before(Render3D.Stages.Prepare)]
view ConfigureGI(mut GlobalIllumination.Environment environment)
{
    environment.settings.probeSpacing = 1.5;
    environment.settings.maxProbes = 512;
    environment.settings.updatesPerFrame = 4;
    environment.settings.debug = false;
}
```

| `Environment.settings` field | Default | Behavior |
|---|---|---|
| `enabled` | `true` | Enable GI on this machine; false releases the GI targets |
| `probeSpacing` | `1.5` | Requested probe spacing in world units, clamped to 0.1–1000 |
| `maxProbes` | `512` | Total probe budget, clamped to 125–2048 |
| `updatesPerFrame` | `4` | Whole probes refreshed per rendered frame, clamped to 0–16; zero freezes updates |
| `intensity` | `1` | Multiplier on received linear irradiance, clamped to 0–16 |
| `debug` | `false` | Show probe placement, gathered light and invalid probes |

These are presentation settings. Each machine can choose its own quality and
update budget without changing the match, networking, snapshots or hashes.

## Automatic world coverage

The package computes bounds from visible Render3D mesh assets and their world
transforms. A padded probe lattice covers those receivers, including floors
and walls. Its anchors remain aligned to world space. Geometry moving inside
retained coverage does not move the grid; expansion beyond it can rebuild the
field. Coverage is retained rather than shrinking around every small movement.
An empty scene releases the targets.

Before refreshing a probe, the package checks nearby mesh triangles and can
relocate the capture position out of a recognized solid or away from a surface.
Movement is limited to 45% of actual probe spacing from the anchor, preserving
the probe budget and grid coverage. An established free position stays fixed
until it loses surface clearance. Blending stays aligned to the grid; visibility
and orientation weights use the actual capture positions. Games need no
placement volumes or per-object GI setup.

Opaque, single-sided static meshes without custom vertex deformation guide
relocation. Closed geometry can classify an embedded probe; open geometry only
provides surface clearance. Cutout, skinned, double-sided and custom-deformed
surfaces remain in captures but do not guide relocation. Tight gaps, deep solid
interiors and ambiguous geometry can leave probes unusable within the movement
bound; capture-based rejection still handles them.

Asset-local bounds and placement geometry are cached once per mesh per asset-pack
generation. World transforms are applied to bounds each frame. Pack changes
invalidate these caches and restart the captured field. Skinned and custom-deformed
meshes currently use their authored rest geometry for coverage; large excursions
beyond those bounds are not automatically tracked.

`probeSpacing` is a request, not a guarantee. Actual spacing increases when
needed to keep the field under `maxProbes`. The current implementation uses
one uniform grid: a very large world therefore gets coarser lighting detail
under the same budget. Automatic placement removes authored volumes, but does
not make this a sparse or unlimited-detail world GI solution.

## Refresh and quality

The package updates complete probes in round-robin order. Each update captures
six 32×32 views of visible opaque and cutout meshes, including skinned meshes
and shared vertex deformation. Captures contain diffuse direct reflection and
emission. They exclude ambient irradiance, previous indirect light and direct
specular reflection, so the field provides one diffuse bounce without recursive
feedback. Transparent materials receive GI but do not appear in captures.

Directional irradiance is gathered with positive cosine weights for diffuse
transport, then retained with directional visibility between frames.
Probe back-face hits identify locations inside one-sided solid geometry;
probes with excessive back-face coverage are rejected during interpolation.
Visible valid neighbors are normalized together, with a confidence check when
none is trustworthy. This avoids treating every occluded neighbor as black
incident light. Thin double-sided cards do not classify a probe as inside a
solid. Probes that cannot find usable nearby space within the relocation bound
can remain rejected.

New fields start dark and fill progressively. A full update cycle takes
approximately `ceil(environment.layout.total / updatesPerFrame)` rendered
frames. Changed lights, emission, poses and geometry appear as their probes
refresh. Lower `probeSpacing` can retain finer spatial lighting changes; more
probes increase memory and refresh latency, while a higher update budget does
more work each frame. The field can still miss small emitters, thin occluders
or contact detail. Choose settings using the scene's actual images and frame
cost on its target hardware.

Set `updatesPerFrame = 0` to freeze the captured field. Subsequent lighting or
geometry changes leave it stale until updates resume. Changing spacing or the
probe-count budget can restart the field; intensity and debug changes retain
its captures. GI uses Render3D's indirect irradiance provider, so material AO
attenuates received GI without darkening direct light or emission.

## Debug and lifetime

Set `debug = true` to show small octahedral markers at actual capture positions,
including relocated probes.
Uncaptured probes are magenta, rejected probes are orange/red, and valid probes
show their gathered lighting. Markers are drawn after transparent objects and
before post-processing.

Disabling GI or Render3D releases the package's targets. Re-enabling starts a
fresh progressive field. Explicit disposal uses
`GlobalIllumination.Dispose(environment)` before recording a new frame. A GPU
device reset loses retained lighting, which refills through ordinary updates;
a frozen field cannot recover until updates resume.

## Rendering limits

Captures reuse selected Render3D lights and its existing main-camera shadow
atlas. Ensure that shadow coverage includes surfaces visible from the probes;
camera-fitted directional cascades can otherwise leave capture surfaces
unshadowed. Surface and UV callbacks see the probe view direction during
capture. Vertex callbacks keep the main-camera `VertexInput.eye`, including
after skinning.

There is no automatic sky lighting, glossy GI, light baking or multiple-bounce
transport in this implementation. The renderer's existing float-target
fallback can clip HDR and reduce the precision of visibility data on devices
without float render targets. Native and web use the same package design;
supported quality and update budgets depend on the scene and hardware.
