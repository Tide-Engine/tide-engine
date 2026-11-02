---
description: "Add experimental automatic diffuse GI to Render3D with adaptive probe blocks, a bounded resident field and regional updates."
---

# Global Illumination

`globalillumination` is an optional package depending on [Render3D](./render3d.md).
It gathers diffuse light reflected by lit surfaces and emitted by materials,
then adds one diffuse bounce. Coverage is derived automatically from rendered
geometry and the main view. Games author no GI boxes or probe volumes.

::: info Experimental branch
Development remains on `feat/render3d-gi`. This package has not been merged to
`dev` or published in a Tide release. Its API and quality limits may change.
:::

## Add and configure

In a checkout or installation containing this experimental package:

```sh
tide add globalillumination
```

Its dependency line is `globalillumination` and namespace `GlobalIllumination`.
The dependency includes `render3d`. Adding the package opts into GI; games
omitting it compile and ship none of its field management, capture passes or
sampling. Render3D continues to expose its general indirect-light provider and
owns no GI field when used alone.

Use ordinary Render3D mesh, material and light components. GI starts enabled
when present. Configure its machine-local environment before preparation:

```csharp
[Before(Render3D.Stages.Prepare)]
view ConfigureGI(mut GlobalIllumination.Environment environment)
{
    environment.settings.probeSpacing = 1.5;
    environment.settings.maxProbes = 512;
    environment.settings.updatesPerFrame = 4;
    environment.settings.viewDistance = 48;
    environment.settings.captureDistance = 48;
    environment.settings.refreshInterval = 600;
}
```

| `Environment.settings` field | Default | Behavior |
|---|---|---|
| `enabled` | `true` | Enable GI locally; false releases its targets |
| `probeSpacing` | `1.5` | Requested finest spacing in world units, clamped to 0.1–1000 |
| `maxProbes` | `512` | Physical probe capacity, clamped to 125–2048; coincident block anchors share captures |
| `updatesPerFrame` | `4` | Maximum complete captures per rendered frame, clamped to 0–16; zero freezes captures |
| `viewDistance` | `48` | Distance from the main camera considered for resident receiver coverage, clamped to 1–1,000,000 |
| `captureDistance` | `48` | Maximum capture distance around each probe, clamped to 1–1,000,000 |
| `refreshInterval` | `600` | Age in rendered frames after which unchanged probes become eligible for background refresh, clamped to 1–36000 |
| `intensity` | `1` | Multiplier on received linear irradiance, clamped to 0–16 |
| `debug` | `false` | Show actual probe positions, gathered light and rejected/uncaptured probes |

These are presentation settings. Each machine can choose its quality and update
budget without changing simulation, networking, snapshots or match hashes.

## Adaptive automatic coverage

The package uses sparse blocks of 3×3×3 logical probe anchors. It checks for
nearby receiver geometry, skips empty regions and refines useful blocks toward
the requested finest spacing. Geometry proximity uses actual opaque static
triangles where available, with conservative bounds for other geometry. Detail
is prioritized near the main view under the finite probe budget. Coincident
anchors share physical captures, including anchors common to different
subdivision levels. There are at most 128 resident blocks. Logical anchors
whose interpolation support cannot reach receiver bounds are omitted,
including padding for surface bias and boundary blending, so empty outer
anchors do not consume the physical budget. The package plans useful geometry
within a bounded hierarchy, then coarsens less important
sibling regions until the physical probe and block limits fit. It compacts the
surviving hierarchy before uploading the lookup table.

A half-base-spacing anchor phase reduces exact alignment with common wall/floor
planes. At very large coordinates, the base spacing increases in power-of-two steps so
anchor positions remain representable without changing phase on every camera
movement.

`viewDistance` bounds the working set around the camera. Moving across coverage
cells changes the required blocks; matching blocks and anchors retain their
atlas slots, captured lighting and relocation offsets. Newly required anchors
start uncaptured, while retired slots can be reused. Adding distant buildings
does not force one world-sized grid to become coarser. More geometry competing
within the resident working set can still reduce detail.

`probeSpacing` is a requested finest level, not a guarantee for every receiver.
Coarser blocks use fewer probes in less important regions. Increasing the probe
capacity can retain more detail; increasing view distance spreads that capacity
over more of the world. Receivers outside resident coverage can have no GI.
Coverage is automatic, but it does not provide unlimited detail or residency.

Before capture, nearby eligible triangles can move a probe out of a recognized
solid or improve surface clearance. Movement is limited to 45% of the common base
spacing. Established free positions stay fixed until clearance is lost. Shared
anchors use that same base unit for relocation and visibility, so their captured
data remains consistent when neighboring blocks have different spacings.

Opaque, single-sided static meshes without custom vertex deformation guide
triangle relocation. Closed meshes can classify embedded probes; open meshes
only provide surface clearance. Skinned, cutout, double-sided and deformed
surfaces remain in captures but do not guide relocation. Tight gaps and deep
solid interiors can still leave probes rejected within the movement bound.

Each block interpolates visible neighboring probes. Equal-resolution neighbors
share their face records, so they use one gather up to that face. A small
world-space box filter blends only where resolution changes or coverage is
missing, with separate handling for affected faces, edges and corners.
Uncovered portions fade toward no indirect lighting. The field's
lookup and initialization metadata share its two GI texture bindings; ordinary
fully mapped skinned materials retain the twelve-texture limit. A reused slot
cannot contribute its old light before its new capture is initialized.

When a region refines, available retained outer-corner probes can supply provisional
lighting while new edge/face/interior probes fill. Their contribution blends
toward the fine field as captured fine support increases. Pruned corners and
topology changes across several levels can still expose uncaptured coverage.

## Regional refresh

Newly needed probes are captured first, followed by the oldest dirty probes and
aged background refreshes. Age prevents a continuously moving light or object
from repeatedly refreshing only the nearest probes. `updatesPerFrame` is a
maximum; unchanged regions can
perform no captures. Existing lighting remains available while dirty probes
wait for their turn. Initial fill time depends on the active probe count and
capture budget; the atlas capacity includes unused slots and is not the fill
target.

The package tracks numeric world transforms, material values, added/removed
geometry and selected Render3D lights. Changes dirty affected capture
neighborhoods. Point/spot light changes affect their old and new regions;
directional light changes affect all resident probes. Moving shadow casters,
caster flags and cutout coverage can change lighting at distant captured
surfaces, so their changes propagate through affected selected shadow lights.
Directional shadow changes can dirty the whole resident field. Opaque emission,
color and roughness changes retain local invalidation when their shadow
silhouette is unchanged. Shadow enablement/allocation changes also trigger
refresh. Skinned and custom-deformed shadow casters use conservative propagation
because authored rest bounds do not constrain their current silhouette. Asset
pack changes clear geometry caches and captured history.

Texture identity/content changes and values captured inside shader callbacks
are not detectable through ordinary numeric material comparisons. After such a
change, call `GlobalIllumination.Invalidate` before `GlobalIllumination.Update`.
Pass an affected world-space region:

```csharp
void RefreshChangedGI(mut GlobalIllumination.Environment environment)
{
    GlobalIllumination.Invalidate(environment, GlobalIllumination.LocalBounds {
        valid = true,
        minimum = float3(-4, 0, -4),
        maximum = float3(4, 6, 4)
    });
}
```

Probes whose capture range intersects the region become dirty. Possible shadow
changes also propagate through affected selected shadow lights, including
receivers whose probes cannot see the edited geometry directly. To dirty all
active probes, use:

```csharp
GlobalIllumination.Invalidate(environment, default);
```

An invalid/default region means all active probes. Background refresh provides
a fallback for unreported changes. Its default 600-frame threshold makes a probe
eligible; the capture budget and other dirty work decide when it is updated.
It is not a guaranteed complete refresh period.

Set `updatesPerFrame = 0` to freeze captures. Changes then leave retained light
stale. The working set can still change with the camera, so newly streamed
regions remain uncaptured until updates resume. Intensity, debug, update budget
and refresh-age changes preserve captures. Probe capacity/base-spacing changes,
pack replacement and disabling/re-enabling GI can recreate the field. Changing
capture distance marks the existing field dirty.

## Captures and cost

Each updated probe uses six 32×32 raster views of opaque and cutout static/skinned
geometry, retaining material maps, callbacks and prepared poses. Captures gather
direct diffuse reflection and emission. They exclude ambient irradiance, previous
GI and direct specular reflection, so there is one diffuse bounce without
recursive feedback. Transparent surfaces receive GI but are omitted from
captures. Material AO attenuates received GI without darkening direct light or
emission.

An instance BVH selects nearby capture candidates and is shared with coverage
queries. Static triangle caches are built as needed near resident geometry.
For each updated probe, current material/pose query bindings record owned draw
packets once; the other faces reuse those packets with face culling. Skinned and
custom-deformed instances are kept as conservative unbounded candidates rather
than rejected using potentially stale rest bounds.

Scene bookkeeping still scans instances each frame, including when captures are
frozen. Recording an updated probe still scans its live world queries to obtain
the selected bound materials. These CPU scans remain proportional to scene
instance count. This is not a claim that all large-scene work becomes sublinear.
Topology construction and geometry changes have additional CPU costs; sampling
near mixed-resolution boundaries can cost more than sampling block interiors.
Choose budgets from actual images and measurements on target hardware.

Positive cosine integration retains directional diffuse irradiance with
directional visibility. Excessive one-sided back-face hits reject a probe.
Trustworthy visible neighbors are normalized together, with a confidence check
when none can support the receiver. Thin geometry, sparse usable probes, small
emitters and coarse coverage can still cause missed light, leaks or contact
artifacts.

`captureDistance` bounds radial geometry distance, including the corners of each
cube-face view. Geometry outside that range is omitted, so distant emissive
surfaces and occluders can be missed. Selected direct lights still shade the
surfaces captured within range. Captures reuse selected Render3D lights and the main-camera
shadow atlas; camera-fitted directional shadows may not cover every surface
visible from probes. Surface/UV callbacks see the probe view direction. Vertex
callbacks retain the main-camera `VertexInput.eye`, including after skinning.
Coverage uses authored rest bounds for skinned/deformed meshes, so large
excursions beyond those bounds are not automatically included.

## Debug and lifetime

Set `debug = true` to show octahedral markers at actual relocated capture
positions. Uncaptured probes are magenta, rejected probes orange/red, and valid
probes show gathered light. Markers are depth tested after transparent objects
and before post-processing.

All field state is local presentation data. Disabling GI or Render3D releases
its resources; re-enabling starts a fresh field. Explicit disposal is
`GlobalIllumination.Dispose(environment)` before recording. A GPU device reset
loses retained lighting, which refills through ordinary updates; a frozen field
cannot recover until captures resume. Capture updates upload the field lookup
table again so it can recover alongside the lighting after GPU resource loss.

There is no automatic sky lighting, glossy GI, light baker or multiple-bounce
transport. The renderer's float-target fallback can clip HDR and reduce
visibility precision on devices without float render targets. Native and web
use the same package design; supported quality and budgets depend on the scene
and hardware.
