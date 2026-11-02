# Global Illumination

Experimental realtime diffuse GI for the optional Render3D renderer. The package
places probes automatically around rendered geometry, captures direct diffuse
reflection and emission, and adds one diffuse bounce to surfaces. Games author
no probe volumes.

Development remains on `feat/render3d-gi`; this package has not been merged to
`dev` or published in a Tide release.

## Add and configure

In a checkout or installation containing the experimental package:

```sh
tide add globalillumination
```

The dependency name is `globalillumination`, namespace `GlobalIllumination`, and
dependency `render3d`. Adding the package opts into GI. Games omitting it compile
and ship none of its field management, captures or sampling. Render3D alone owns
no GI field and continues to use its general indirect-light provider interface.

Use ordinary Render3D mesh, material and light components. GI starts enabled
when present; configure its machine-local environment before preparation:

```csharp
[Before(Render3D.Stages.Prepare)]
view ConfigureGI(mut GlobalIllumination.Environment environment)
{
    environment.settings.probeSpacing = 1.5;
    environment.settings.maxProbes = 512;
    environment.settings.updatesPerFrame = 4;
    environment.settings.viewDistance = 48;
    environment.settings.captureDistance = 48;
}
```

| `Environment.settings` field | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Enable GI locally; false releases its targets |
| `probeSpacing` | `1.5` | Requested finest spacing in world units, clamped to 0.1–1000 |
| `maxProbes` | `512` | Physical probe capacity, clamped to 125–2048; neighboring blocks share coincident probes |
| `updatesPerFrame` | `4` | Maximum complete captures per rendered frame, clamped to 0–16; zero freezes captures |
| `viewDistance` | `48` | Distance from the main camera considered for resident receiver coverage, clamped to 1–1,000,000 |
| `captureDistance` | `48` | Maximum distance captured around each probe, clamped to 1–1,000,000 |
| `refreshInterval` | `600` | Age in rendered frames after which unchanged probes become eligible for background refresh, clamped to 1–36000 |
| `intensity` | `1` | Received irradiance multiplier, clamped to 0–16 |
| `debug` | `false` | Show actual capture positions, gathered light and rejected/uncaptured probes |

## Adaptive coverage

Coverage uses sparse blocks with 3×3×3 logical probe anchors. The package checks
geometry proximity, skips empty regions and refines useful blocks toward the
requested spacing, prioritizing receivers near the main view. Opaque static
triangles guide placement where available; other rendered geometry supplies
conservative bounds. Coincident anchors share one physical capture across
neighboring blocks and subdivision levels. A block limit of 128 and the physical
probe budget bound the resident field. Logical anchors whose interpolation
support cannot reach receiver bounds are omitted, including padding for surface
bias and boundary blending. Empty outer anchors therefore do not consume the
physical budget. Planning first resolves useful geometry
within a bounded hierarchy, then coarsens less important sibling regions until
the shared physical probes fit. Surviving tree nodes are compacted before upload.

A half-base-spacing anchor phase reduces exact alignment with common wall/floor
planes. At very large coordinates, the base spacing increases in power-of-two
steps to retain representable, stable positions.

Moving the camera rebuilds the required working set as it crosses coverage
cells. Matching blocks and physical anchors retain their atlas slots, captured
light and relocation offsets. Newly needed anchors start uncaptured, and retired
slots can be reused. Adding distant geometry does not force one world-sized
grid to become coarser; geometry competing within the active working set can
still reduce detail under the same budget.

Each block interpolates nearby visible probes. Equal-resolution neighbors share
their face probes and retain one gather right to the face. A small world-space
box filter blends where resolution changes or coverage is missing, including
affected edges and corners. Visibility and relocation offsets use the common
base spacing so a shared probe has the same meaning in every block. Before capture, eligible
static triangles can move an embedded probe outward or improve surface
clearance, within 45% of the base spacing. An established free offset is retained.

The field, indirection and initialization metadata use two GI texture bindings.
Initialization belongs to each physical slot, so changing the working set never
treats a reused slot's old lighting as a newly captured probe. Debug markers are
magenta until captured, orange/red when rejected, and otherwise show gathered
light. They mark actual relocated capture positions.

During refinement, available retained outer-corner captures can provide
provisional light while new fine probes fill. Pruned corners and changes across
several levels can still expose uncaptured coverage.

## Updates and external changes

New probes are captured first, followed by the oldest dirty probes and aged
background refreshes, subject to `updatesPerFrame`. Continuous motion therefore
does not repeatedly select only the nearest dirty probes. Unchanged regions can
perform no captures. Numeric transform/material changes, added or removed
geometry, and selected light changes mark affected capture neighborhoods dirty.
Moving shadow casters, caster flags and cutout coverage also dirty receivers
through affected selected shadow lights; directional shadow changes can affect
the whole resident field. Ordinary opaque emission/color/roughness edits stay
local. Skinned and custom-deformed instances request updates conservatively.
Existing captured lighting stays available while dirty probes await refresh.

Texture edits and shader-callback state changes cannot be inferred from ordinary
material values. Mark their affected world region after the change and before
`GlobalIllumination.Update`:

```csharp
void RefreshChangedGI(mut GlobalIllumination.Environment environment)
{
    GlobalIllumination.Invalidate(environment, GlobalIllumination.LocalBounds {
        valid = true, minimum = float3(-4, 0, -4), maximum = float3(4, 6, 4)
    });
}
```

Call `GlobalIllumination.Invalidate(environment, default)` to dirty all active
probes. Region invalidation includes probes whose capture range intersects that
region and propagates possible shadow changes through affected selected shadow
lights. Background refresh is a fallback for unreported changes; its age
threshold is not a promise that every probe finishes within that interval.

Zero updates freeze captures, while coverage can still change with the camera.
Newly streamed regions then remain uncaptured until updates resume. Intensity,
debug, update budget and refresh-age changes retain lighting. Changing the probe
capacity or base spacing, replacing the asset pack, or disabling/re-enabling GI
can recreate the field.

## Cost and current limits

Each updated probe uses six 32×32 raster views and positive cosine integration.
An instance BVH selects local capture candidates. Live material/pose query
bindings are recorded once per updated probe; the other faces replay those
frame-owned packets with face culling. Scene bookkeeping still scans instances
each frame, and the live query scan remains proportional to scene instance
count per updated probe. Skinned/deformed candidates are kept conservatively
rather than culled using potentially stale rest bounds. This does not make all
CPU work sublinear. Topology construction, dynamic geometry and boundary filtering
have additional costs.
Use actual scene images and target-hardware measurements to choose budgets.

Coverage and capture distances are finite. Distant receivers may have no resident
GI. Captures omit geometry beyond their radial range, including cube-face corners
outside that range; distant emissive surfaces and occluders can be missed. Thin
geometry, small emitters, limited valid probes and coarse blocks can still miss
light or leak. Authored rest bounds cannot track arbitrary skinning/deformation
excursions. Only opaque, single-sided static meshes without vertex callbacks
guide triangle relocation; other captured geometry still relies on raster
visibility and rejection.

Opaque/cutout static and skinned surfaces appear in captures; transparent
surfaces receive GI but are omitted from captures. Surface/UV callbacks use the
probe view direction, while vertex deformation keeps the main eye. Captures
reuse selected Render3D lights and its main-camera shadow atlas, whose coverage
may need adjustment. Ambient irradiance, prior GI and specular reflection do
not feed captures. There is no glossy GI, automatic sky lighting, multiple-bounce
transport or light baker.

All settings, history and resources are local presentation data, outside match
snapshots and hashes. Disabling GI or Render3D releases GI resources. A GPU reset
loses retained light, which refills as updates run; frozen captures must resume
to recover. Capture updates also upload the field lookup table again after GPU
resource loss. Explicit disposal is `GlobalIllumination.Dispose(environment)` before
recording.

Full guide: [Global Illumination](https://tide-engine.dev/packages/globalillumination).
