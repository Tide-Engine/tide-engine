# Render2D package

## First height-aware sprite renderer (2026-11-02)

The owner requested a Render2D sibling to Render3D, with normal maps, lights,
shadows, good performance and the Path of Exile GI technique. Asked about scope,
the owner selected implementing a usable first version and height-aware 2D,
including elevated sprites casting shadows across floors.

`packages/render2d` is a pure Tide package, namespace `Render2D`, with no
Render3D dependency, compiler change, runtime entry point or backend addition.
It uses shared quad geometry, existing shader stages/MRT/data-texture lists,
managed render targets and package-owned views. Package scanning/installation
already discovers its ordinary `tide.packages`. Public API and limitations are
in `docs/packages/render2d.md` and the package README.

### Geometry, materials and lighting

XY is the game plane; Z is elevation. The fixed orthographic -Z camera preserves
screen-right X and screen-up Y using a reflected camera basis. Quads follow the
stored Transform hierarchy, size and normalized UV rectangle. A public Rect is
converted to a numeric float4 for GPU binding. A shared quad's compatible draws
can batch. Opaque/cutout surfaces use real depth: elevated sprites are sheets,
not columns down to the floor. Their shadow maps retain real height and alpha
cutouts, including displacement on lower receiver surfaces.

SpriteRenderer + Material are match components; resource fields are attached
local state, excluding them from networking/snapshots/hashes. This first API
inherits the language's restriction on placing such materials on local entities.
Lights can be match or local entities. No simulation field is changed by views.

Normals use Raw numeric maps, +R sprite-right and +G sprite-up. The geometric
normal and reciprocal tangent frame account for rotation, scale and mirroring.
Scalar metallic/roughness/specular feed GGX/Smith/Schlick direct shading. Base
and emission RGB decode from display sRGB. Empty normal/emission maps skip their
fetches. Zero lights/ambient gives emission only. The scalar emission clamp is
32752 because premultiplied metadata can double its stored value in half float.

Four RGBA16F geometry outputs hold albedo/roughness, normal/metallic,
position/flags and emission/specular. Numeric alpha metadata is biased above
zero so the engine's ordinary premultiply/unpremultiply cannot erase opaque-hit
radiance or zero-valued material fields. Positions have camera-relative XY and
world Z; direct lighting reconstructs XY analytically from UV and the exact
shared viewSize to avoid half-float XY quantization. Keep elevation modest for
fine distinctions: half-float Z precision is still finite.

Deferred lighting writes two RGBA16F outputs: full HDR and diffuse+emission.
GI reads the latter so view-dependent specular is not rebounced as diffuse.
Directional/point/spot collection uses bounded priority selection, at most 256 lights
and at most 16 shadow views. Point faces reserve six together; directional/spot use
one. The atlas has gutters/clamped comparison reads and a device-size fallback.
Directional bounds are explicitly fixed, not camera-fitted cascades. Derived
counts expose lights dropped or left unshadowed and actual cell resolution.

### Indirect light

`GlobalIllumination` owns the bounded screen-space surface Radiance Cascades.
The topmost visible position/source radiance is point-sampled to a grid with
long axis 32..256 (default 256). Probe spacing 1..16 (default 4) and cascades 1..4
(default 3) bound memory/work. A cascade doubles probe spacing and octahedral
direction resolution; its atlas stays the same dimensions. Disjoint distance
intervals grow and merge far-to-near with radiance/transmittance composition.
Atlas alpha stores 1+transmittance so hit radiance survives alpha 0 logically.

The ray marcher traverses XY cells with DDA and intersects each thin horizontal
patch's elevation within its entry/exit time interval. A ray below a raised card
passes under it; no artificial vertical wall is inferred from a height step.
The <=256-square grid bounds any ray to 514 cell visits. Structurally nonzero
division denominators also cover compile-time axis-aligned rays on ANGLE D3D11.
Depth-aware interpolation rejects probes from other heights. Sixteen directions
in cascade 0 collapse to first-order RGB spherical harmonics; the full-resolution
normal evaluates their diffuse convolution. Targets rebuild each frame with no
temporal feedback, stochastic noise or previous-frame energy accumulation.

This is one diffuse bounce from two-sided diffuse sheet radiance plus emission,
not full-volume or multi-bounce GI. Hidden/offscreen sheets are absent. Tiny
features can vanish in the reduced grid, and low angular/SH resolution softens
directional detail. Arbitrarily pitched/rolled surfaces are not represented by
the horizontal-patch intersection model. Normal maps affect lighting, not height.

### Post, lifecycle and scope

Stages: Prepare, Lights, Shadows, Color, Lighting, Indirect, PostProcess, Present.
Games configure before Prepare and draw HUDs after Present. Optional bloom uses
four quarter-resolution extraction reads, a paired nine-tap separable blur and
an HDR composite. Exposure/Reinhard/sRGB occurs once at final presentation.
defaultPresent=false exposes output for a custom presentation pass.

Core, GI and bloom have independent retained resources. Renderer.enabled=false
before Prepare releases all three. Core Dispose(renderer) releases its own
resources; explicitly disposing it while enabled does not release the other
singletons' scratch. Bloom cleanup runs at Prepare offset 1, after game settings;
placing it Before(Prepare) incorrectly observed the previous frame's enabled
value and was corrected after a lifecycle regression exposed the retained image.
Tests forget recorded draw lists before freeing managed target bookkeeping.

Floating-point render targets are required for faithful numeric buffers; the
engine's RGBA8 fallback cannot preserve this material/height encoding. No capability
API/backend change is introduced here. No alpha blending, independent painter
order, isometric billboards, ORM/height textures, custom material callbacks,
displacement, environment specular or hardware-raytracing dependency is added.

### Example and validation

`docs/examples/render2d.tide` is a compact interactive lantern garden: raised
copper plates and cutout foliage, moving warm/cool point lights, emissive cards,
and independent normals/shadows/GI/bloom controls. Its seven analytic PNG maps
are reproduced by the standard-library Python script beside them. Both source
and live example are linked from the package guide, using existing docs staging.

The focused pipeline and pixel targets cover independent diffuse/color/falloff
math, normal rotation/mirroring, point/spot ranges, elevated displaced shadows,
matching cutout holes, pure emission/no-light controls, real GI transfer and
actual DDA underpass/disjoint-interval witnesses. They also cover bloom spread
and constant energy, target retain/disable/recreate, light/shadow budgets,
no sampled-destination feedback, display scale 1/2 and unchanged match hashes.

Validation passes on Windows ASan: the pipeline fixture and Vulkan/OpenGL pixel
tests (3 CTests). Web-release passes the WASI pipeline fixture and WebGL,
ANGLE D3D11 and WebGPU pixel tests (4 CTests). The demo builds native and web;
the production docs build and selected source/rendering smoke checks pass on
WebGL and WebGPU. Native captures at 960x540 and 376x400 were inspected, including
scripted toggles for GI, bloom, normals and shadows. No compiler warnings or
graphics validation errors remain. Other desktop operating systems, real
phones and consoles were not run locally. Point/spot light collection and
illumination are tested, but pixel shadow assertions currently use directional
lights; the demo also exercises two shadowed point lights.

Uninstrumented Vulkan measurements on the RTX 4080 SUPER host (2026-11-02):
`tide_render2d_pixels --profile --frames 120`, 20 warm-up frames, 1280x720,
two sprites, no analytic lights/shadows or bloom, GI grid 256x144, 3 cascades,
probe spacing 4, distance 5. Two runs with GI measured CPU recording means
0.006113/0.006300ms, GPU means 0.179058/0.188389ms, and 64,379,188 GPU bytes.
Two `--no-gi` runs measured CPU means 0.004434/0.004641ms, GPU means
0.083239/0.083330ms, and 51,615,028 GPU bytes. Each collected 120 GPU samples;
GI maximums were 0.799712/0.800672ms. Timing excludes readback and presentation
waiting. These measure a small pipeline's overhead, not large-scene or mobile
performance. A representative many-sprite game still needs its own measurements.
