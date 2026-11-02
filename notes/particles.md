# Code-oriented particle effects

## First package (2026-11-02)

The owner requested a general particle system authored in code so agents can
create and change effects. `packages/particles`, namespace `Particles`, is an
optional ordinary-Tide package. No compiler syntax, generated-C entry point,
engine runtime ownership or backend design changes. The API, defaults and
limits below are implementation choices, not new language requirements.

`Emitter` owns a public `List<Particle>`, a `Random`, capacity (default 4096)
and a double fractional emission remainder. Visual users keep it in local
singletons or attached local fields. The package declares no singleton, system,
view or hidden clock and allocates no entity per particle. Callers choose when
to emit, update and draw.

`Particle` holds position/velocity, age/lifetime, full 2D size endpoints,
color endpoints, rotation/angular velocity and a `float4 custom`. The stock
lifetime is one second; size begins at 0.1 and ends at zero, color begins white
and ends transparent white. All angles are radians. Positive 2D rotation goes
from +x toward +y (counterclockwise in world coordinates, clockwise in screen
coordinates); billboards rotate camera-right toward camera-up. Texture uv
(0, 0) remains top-left in either 2D coordinate mode.

- `Burst` initializes default particles through a
  `Func<mut Particle, mut Random, void>`. Only available capacity's worth of
  callbacks run. `Add` accepts a fully authored particle. Both reject particles
  with nonfinite fields, negative age or age at/past lifetime. A rejected
  initializer result consumes that attempt; it does not retry.
- `Emit` takes particles/second and explicit dt, keeps only the fractional
  amount due and drops whole emissions that do not fit. Its positive finite
  float inputs multiply in double, and large totals are bounded before integer
  conversion. Invalid/nonpositive time or rate is a no-op; pauses preserve
  fractional carry. Explicitly setting `emitter.random = Random(seed)` provides
  repeatability for identical callback/call/time sequences.
- Capacity is clamped to [0, 1048576] on use. Add, Burst and positive valid
  Emit/Update truncate the list's end if it exceeds that effective capacity.
  `Clear` empties particles and remainder, keeping capacity and the RNG stream.
- `Update` advances age first, omits expired particles before callback work,
  checks callback results again and compacts survivors in their original
  relative order. The default movement is semi-implicit Euler with nonnegative
  reciprocal drag; `Integrate` is also callable by a game's custom updater.
  A custom `Func<mut Particle, float, void>` replaces movement, not lifecycle.
  Positive finite dt is required; the package does not clamp frame stalls.
- `Age` gives normalized lifetime progress. `Size` linearly blends full size,
  clamped nonnegative. `Tint` blends alpha-weighted endpoints and returns
  straight alpha, avoiding darkening on a fade to transparent black. Custom
  curves, spawn shapes, forces and sprite selection remain ordinary game code.

Rendering uses a caller-owned local `Renderer`, reusable quad geometry and
ordinary instanced draws. `DrawSprites` uses position.xy and the current 2D camera
or screen. `DrawBillboards` makes camera-facing billboards from the explicitly passed
eye and quaternion, with optional stable center-depth sorting for that draw.
Sorting leaves simulation order untouched and does not sort other emitters or
Render3D transparent objects. It assumes world-space particle centers with no
additional inherited draw transform. Untextured draws use soft discs; texture overloads
use the full sprite. Target-first overloads draw into an explicit RenderTarget.
Blending and the 3D read-only depth/no-cull scope restore previous settings.

Public docs are `docs/packages/particles.md`; `packages/particles/README.md`
links that guide. `docs/examples/particles.tide` is a local 2D smoke/sparks
example with callable initializers, a custom updater, pointer bursts, emission
rate, stop/start and clear. It uses the existing package/example CMake path,
shows the exact running source and leaves the bottom host-stats corner free.
The guide also covers textures, 3D, custom shaders and drawing into Render3D's
HDR target between Transparent and PostPrepare. The package has no Render3D
dependency.

Current limits: CPU simulation, no built-in physics collision, ribbon/trail
topology, compute shader path, effect file format or editor graph. GPU drawing
is visual-only; local effects do not change the match's deterministic state.
No existing game's hand-authored effects were replaced.

Validation (Windows Clang 23.1.2, Node 24.22.1):

- `tide_particles`: all six CPU cases pass under native ASan and WASI release.
  They cover independent integration/lifetime landmarks, custom motion,
  alpha-weighted color and size, capacity/compaction, fractional emission and
  dropped debt, seeded captures/replay, invalid inputs and unchanged match
  hashes. No existing deterministic expectation changed.
- `tide_particles_pixels` passes under native ASan Vulkan (SDK 1.4.363.0
  validation) and OpenGL, and web-release WebGL, Windows ANGLE D3D11 and
  WebGPU. Pixels check soft falloff, asymmetric texture orientation in screen
  and world coordinates, non-square rotation, rotated-camera billboards,
  sorted/unsorted/tied transparency and opaque occlusion. Packet assertions
  check restored settings, independent captures when renderer storage is
  reused, and 257 instances in one run. Every draw overload is exercised.
- `docs_example_particles` and its web page build. A 180-frame hidden native
  ASan run at 376 x 400 captures the smoke/sparks and readable controls without
  overlap. The production docs build passes, including navigation, source
  includes, links and anchors; its allowed 404s are unstaged unrelated examples.
  `TIDE_EXAMPLES=particles node docs/tests/examples.mjs` also passes for WebGL
  and WebGPU, checking the displayed source and live page rendering, with
  `Tide.checkGL` enabled and browsers muted and closed by the runner.
- Linux, macOS, MinGW and Android execution remain CI coverage. No extra CI
  workflow was dispatched; no unrelated game or full suite was run.

Independent read-only review found no blocking correctness issue in emission
bounds/casts, stable compaction, callback validation, alpha-weighted tint,
instance capture, screen orientation or per-draw sorting. Emission births are
age zero, without within-frame staggering; transparency sorting is local to
one draw and assumes world-space centers. Validation/update and renderer
preparation are linear passes.

An exploratory non-sanitized CPU comparison in this checkout used generated C
and the checkout's optimized host engine, with C17 `-O2 -fno-fast-math
-ffp-contract=off`, five batches of 32 updates, and the same finite checks,
age/expiry, acceleration, drag, rotation and stable compaction. Every particle
agreed byte-for-byte with an independent flat C-array implementation. The best
batch per case measured 4096 particles at 0.194 ms for the package, 0.200 ms
for an inline handwritten Tide loop and 0.061 ms for C; at 65536, 3.13 / 3.15 /
0.95 ms. Other builds were active on this machine, so these are indicative
measurements, not throughput guarantees. The scratch harness remains under
`build/particle-bench` locally, not a committed benchmark. No GPU throughput
claim is made; the measured Tide list/storage gap was not expanded into a
runtime optimization task.

Merged `origin/dev`'s Render2D package and compiler/CLI diagnostic improvements.
Resolved only package-list, docs-example registration and notes conflicts,
retaining both packages and the same pure-Tide link guard. The updated compiler
rebuilt the particle CPU/pixel targets and example on native ASan and web-release;
all three native and four web particle CTests pass again. The merged production
docs build also passes. No runtime or particle behavior conflict required a
broader suite.
