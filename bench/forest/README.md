# Forest stress scene

`bench_forest` exercises ordinary Render3D entities, material collection, shared
wind, cutout foliage, shadows and HDR presentation. Each planted tree retains
separate bark/foliage entities; distant rendering uses one whole-tree impostor
and hides the foliage entity. Deterministic planting is outside frame timings.
Mesh selection, material binding, crossfade submission and rendering run every
frame. There is no cached direct-draw replacement.
Foliage rows are planted before bark/proxy rows, allowing nearby leaf depth to
reject hidden distant atlas fragments before their expensive shading.

Use an ordinary optimized build for measurements and the sanitizer build for
correctness. Runs are finite and hidden:

```powershell
$env:TIDE_HIDDEN = '1'
cmake --build --preset release --target bench_forest
build/release/bin/bench_forest --count 10000 --camera ground --capture forest-ground.png
build/release/bin/bench_forest --count 100000 --camera wide --frames 100 --warmup 20 --output forest.jsonl
build/release/bin/bench_forest --count 10000 --orbit 1 --orbit-radius 180 --orbit-height 35 --frames 180 --capture orbit.png
build/release/bin/bench_forest --motion-smoke
```

Ground, canopy and wide presets show the ride at eye height, above the canopy
and from a distant overview. Nominal spacing stays 5.5 m: 10,000 trees span
about 550 m and 100,000 about 1.74 km. Terrain spans 2.4 km; the camera far
plane is 4 km. Position, rotation, size and tint vary reproducibly. The winding
ride shifts trees sideways without reducing the requested count.

## Assets and distance levels

The original procedural broadleaf fixture has three mesh levels:

| Level | Bark triangles | Foliage triangles |
| --- | ---: | ---: |
| Detailed | 530 | 1,140 |
| Medium | 212 | 456 |
| Coarse | 52 | 38 |

Branch geometry, shaped leaf sprays and the 512² albedo/normal/mask maps are
reproducible. Leaf-mask RGBA holds ambient occlusion, roughness, metallic and
optical transmittance. Explicit vertex RGB holds wind response, branch phase
and leaf phase; the forest's vertex hook consumes it and restores white display
color. Regenerate source assets, then bake their impostor:

```powershell
node bench/forest/make-assets.mjs
node bench/forest/impostor-bake.mjs
```

The baker rasterizes the actual detailed bark/leaf OBJ triangles and mapped
leaf inputs in their rest pose. Eight azimuths at elevations 0°, 45° and 90°
produce 24 views in a 1024×512 atlas: 8×4 tiles, each 128² with a two-pixel
gutter and 124² image. Outputs include albedo/coverage, raw model-normal/depth,
raw material-mask maps, a two-triangle card, generated Tide settings and JSON
with source hashes, bounds and per-view coverage. Atlas normals are relit;
lighting and shadows are not baked into albedo.

Albedo DDS mipmaps preserve the closest achievable alpha-test coverage at
cutoff 0.32 before the importer premultiplies color. Impostor coverage is
preserved per tile until that tile reaches one texel, then for the aggregate
atlas tail. Raw normal/depth and mask DDS channels remain independent. Native
OpenGL/Vulkan, WebGL2 and WebGPU upload the authored chain; unsupported BC
textures also retain every mip through RGBA fallback. Unity's recorder currently
keeps only the base image. Tiny-mip coverage is approximate, and coarse atlas
footprints still need the runtime's view clamps.

Each frame selects mesh references through local `meshOverride` values. Ordered
complementary dither cells crossfade detailed→medium at 80–100 m,
medium→coarse at 250–310 m and coarse→impostor at 430–510 m. Transition bands
submit both representations; the match's planted entities/mesh identities stay
unchanged. Four shadow cascades receive to 160 m, with mesh casters within
260 m. Impostors do not cast distant shadows. Conservative local bounds cover
wind movement in color and shadow passes.

All mesh materials capture the same `Wind` once per frame. Impostor materials
use `Material.impostor.wind` and `bend` for approximate whole-card sway through
the package's internal impostor vertex stage. The atlas stores one nearest
rest-pose layer, sampled with blended views and optional depth-plane parallax;
it cannot recover hidden geometry or independent moving leaves/branches.
Depth testing, lighting position and fog still use the proxy card's geometry.
Scene fog affects trees, impostors and terrain after lighting, before exposure
and tone mapping. This is an authored reference fixture and an impostor
prototype, not scanned tree art or Gaussian splats.

## Controls and checks

`--help` lists bounds and defaults. Useful controls:

- `--count`, `--camera ground|canopy|wide`, `--width`, `--height`, `--frames`
  and `--warmup` set the workload; defaults are 10,000 trees, ground,
  1920×1080, 100 measured frames and 20 warmup frames.
- `--impostors 0|1|2` selects mesh-only, distant atlas (default), or forced
  atlas reference. `--levels 0` keeps detailed meshes unless forced atlas is
  selected. `--parallax 0|1`, `--wind 0|1`, `--fog 0|1`, `--shadows 0|1` and
  `--shadow-resolution` isolate visual features.
- `--travel` supplies forward metres/second and `--travel-start` its initial
  offset. `--orbit 1` replaces that path with an orbit, controlled by
  `--orbit-start`, `--orbit-speed`, `--orbit-radius` and `--orbit-height`.
  Omitted orbit radius/height are derived from forest extent. Motion and wind
  use a fixed 60 Hz frame sequence; `--start-frame` selects an offset.
- `--capture final.png` saves the completed native image. With
  `--capture-every N`, it also writes `final.png-NNNNNN.png` sequence frames.
  `--output` appends JSONL; web prints JSONL and rejects file output options.
- `--quick` uses 64 trees, 320×180, 32-pixel shadow cells, one warmup and two
  checked frames, with timing disabled. `--motion-smoke` uses 16 checked frames
  on a bounded travel sequence and verifies every primary representation plus
  a transition was exercised under the default levels/impostor controls.

Checks retain exact planting counts, submitted tree work, unchanged match state
and a nonempty completed native image. Separate package fixtures test wind,
leaf lighting, fog, impostor sampling and dither behavior numerically and on
the GPU. `--profile` clears per-view totals at the warmup boundary and prints
instrumented totals/arithmetic means; these are diagnostic attribution values,
not comparison medians. Zero warmup includes first-frame initialization.

## Measurement limits

JSONL includes primary near/medium/coarse/impostor count ranges, transition
counts, main-target submitted bark/foliage/impostor instances, calls/indices,
uploads, memory, camera controls, view-recording/CPU-submission median/p95 and
completed GPU-query median/p95 with sample count. Primary counts include
planted trees outside the camera; submitted instance counts can exceed unique
trees during crossfades. Final count fields describe the final primary levels.
Ranges make changing-camera work explicit; static and moving runs are different
workloads.

Unavailable GPU timing is `null`. Native completion/readback is a separate
synchronized interval; web reports no such interval. Backend GPU timer scopes
differ. CPU and GPU intervals overlap and must not be added or called shipping
FPS. Sanitized/debug runs and software web rendering prove correctness only.
The new quality path has no completed performance claim yet; historical results
refer to their frozen source/assets, not this changed workload.

Set `TIDE_BENCH_REVISION` and `TIDE_BENCH_MACHINE` when recording evidence.
Machine identity is operator supplied, not a physical-adapter query. Serialize
timings with the shared benchmark gate, separately from builds/GPU tests, and
retain repeated runs and their spread.
