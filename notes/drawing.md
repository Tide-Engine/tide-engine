# Drawing: `Draw`, its settings, shapes and meshes

## Custom GUI drawing (2026-10-24)

`GUI.Draw` records into the existing GUI list in declaration order and restores
window settings via `tide_draw_restore_window`. `tide_draw_move_gui` relocates
an anchored area's screen-space 2D content, including meshes/quads/shader runs
and clips, without interpreting state-command unions as positions. Indexed
vertices/shared clip ancestors move once using the existing remap scratch;
target content, explicit world cameras and 3D keep their coordinates. No backend,
shader ABI or batching rule changed. Contracts/tests/measurements: `notes/gui.md`,
Custom control composition.

## Callable instance data (2026-10-24)

Configured stage values and callable fields extend compiler binding and shader
packing, with no Draw runtime or backend change. Explicit instance lists can
carry different SDF functions and numeric captures in one run; generated dispatch
is inside the shared shader. `tide_platform_gpu_functions` checks four shapes in
one actual GPU draw, plus the existing clear triangle. Ordinary per-draw values
remain shared uniforms and existing batching/order boundaries still apply.
Contracts, limits and full-suite validation are in language.md and shaders.md.

## A sprite in one call (2026-10-21)

The owner's "yes do it" (2026-10-20), to a call for sprites, which the third
round found was what was left of textured quads' recording: the caller's four
32-byte corners. `Draw.Sprite` is in the spec under Views and drawing,
Provisional, every rule a pick; the user docs are in
`docs/game/meshes-and-textures.md`, Sprites, and the Draw table.

- **Forms** (`compiler/src/builtins.c`): `(texture, center, size)`,
  `(..., color)`, `(..., rotation, color)`, `(..., part, color)` and
  `(..., part, rotation, color)`, each with an optional filter and wrap after
  it (`sampled`). `MAX_PARAMS` went from 6 to 8 for the last. Codegen
  (`gen_expr`, the `tide_draw_sprite` names) writes the texture as a mesh's is
  (`gen_draw_texture`, the filter and the wrap being the call's only enums),
  then the rest, and `TIDE_COLOR_WHITE` where there's no color. A `[Raw]`
  grid is refused as a mesh's is (`check_sprite_args`, which also has the Filter and Wrap resolved). Targets have it as a
  method through the generic path.
- **Runtime** (`tide/draw.h`: `tide_draw_sprite`, `_rotated`, `_part`,
  `_part_rotated`, inline over `tide_draw_sprite_placed` in `draw.c`): the
  slot from `tide_draw_texture_source`, filter and wrap from the reference.
  The top is +y under a camera and -y where the destination's 2D camera is
  `Screen` (`place.camera_2d`'s `screen`). Unturned and not `altered`, it
  builds the `tide_draw_quad` itself (p top left, r bottom right, pattern
  `TIDE_DRAW_QUAD_ROUND`) and `quad_placed` records it; otherwise four
  corners from the top left round, `turn_by(rotation)` applied as
  `center + (o.x * c - o.y * s, o.x * s + o.y * c)`, through
  `tide_draw_mesh_with`, which tints and moves them (a turn whose corners
  come out whole is the quad they are, as any mesh's).
- **More of a run, inline** (`tide_draw_sprite_part`): where nothing set
  changes it, the last command is a run of quads with the blend, filter and
  wrap, its quads end the list's, there's room, and the texture is its run's
  image (`tide_draw_same_source`: C's pixels as `tide_draw_same_image` tells
  them, a pack image by the name `pack_slot` found last this frame), the quad
  is stored there and the run's count raised. Out of line, recording 65,536
  took 0.98 ms (best of 400, alone), as meshes of four corners 0.38: the quad
  went to `quad_placed` through memory built a float at a time (a
  store-forwarding stall), and the texture's slot was looked up each time.
  Inline, 0.25 ms. `pack_slot` keeps the name it found last and the list's
  texture for it, for the frame (`tide_draw_list.pack_named`, `pack_slot`,
  `pack_frame`; `tide_draw_forget` drops it), so a run of sprites, or of
  meshes, of one asset looks it up once. Grids and rendered images are found
  each time, as a view can paint a grid between two draws.
- Tests: `draw_sprites_are_quads_upright` (`tests/test_draw.c`: corners and
  uvs to the bit under a camera and in Screen, runs, a filter's own run,
  mirroring, parts, turned and tinted as corners, a zeroed texture);
  `tidec_e2e_sprites` (each form from Tide, an asset's declared filter, the
  call's filter and wrap, a grid mirrored, a target's color, into a target,
  Screen); the platform pixel test `sprites_drawn_as_their_corners`
  (`platform/tests/draw.c`, in `runs_drawn_alike`: the same pixels as the
  meshes of their corners, under a camera and in Screen, and the texture's
  first pixel at the top left).

## Recording inline and quads as quads (2026-10-20, third round)

The owner asked to close drawing's gap to hand-written C further: recording
was 5 to 37 times C's, where C writes each instance once. Internal; the API is
as it was, and every benchmark frame is the same bytes (notes/rendering.md,
Recorded quads; numbers in notes/benchmarks.md, Drawing, third round).

- **Inline common cases** (`tide/draw.h`): `tide_draw_rect`, `_circle`,
  `_wire_rect`, `_wire_circle` and `_line` (`tide_draw_plain_kind`),
  `tide_draw_round_rect`, `tide_draw_mesh(_wrapped)` and
  `tide_draw_mesh_3d(_wrapped)` are header-inline: where nothing set changes
  what's recorded (`altered`, `unsettled`) and there's room, their stores go
  straight into the list from where the caller has the values (registers, as
  often as not: on Windows a color or a matrix passed to a function goes by
  its address, through memory). Everything else is the functions as they were,
  renamed `tide_draw_plain_placed`, `tide_draw_round_rect_placed`,
  `tide_draw_mesh_placed` and `tide_draw_mesh_3d_placed`. The mesh paths are
  `always_inline`: clang otherwise kept them as calls. Same names, arguments and
  results for every caller, generated code included.
- **A 3D mesh's next instance**: the list keeps the place
  `tide_draw_mesh_slot` found last (`tide_draw_list.mesh_last`, where a
  thread-local was), which the inline path checks; the transform is stored as
  the list's next matrix, and `tide_draw_instance_stored` culls and records it
  as `instance_at` did, reading it where it is.
- **Quads as quads** (`tide_draw_quad`, `TIDE_DRAW_QUADS`, `tide_draw_list.quads`):
  a mesh of four corners and six indices, triangles p q r and p r s, whose
  corners' places and uvs take p's and r's numbers on each axis (compared by
  their bits, as the renderer does) and whose colors are one, is kept as a
  52-byte quad (p's and r's places and uvs, the color, 4 bits a corner
  saying which number each takes) where its corners and triangles took 152:
  `tide_draw_quad_of`, `tide_draw_quad_corner` gives its corners back to the
  bit. Not where a tint or a transform changes corners (`altered`): those stay
  corners and triangles. Quads one after another with one image, blend,
  filter and wrap are one command; a quad starts a run of its own after a
  mesh of corners, and a mesh after quads, which the renderer still draws as
  one step. `tide_draw_mesh_with` (a texture's slot already found) is what
  Tide's lists go through (`mesh_lists`), so `Draw.Mesh` of a sprite's list is
  a quad too. `tide_draw_append` carries quads (`quad_placed`, with the
  destination's textures); forgetting textures clears a quads command's as a
  mesh's. The embed host's packet (`platform/src/embed.c`, `unquad`) shows
  them as the meshes they were, a `TIDE_DRAW_MESH` over their four corners, so
  what reads it is unchanged.
- **Plain shapes as records** (`tide_draw_plain`, `TIDE_DRAW_PLAIN`,
  `tide_draw_list.plains`; the owner's "yes do it", 2026-10-20): rects,
  circles, their outlines and one-pixel lines are kept as 24-byte records (two
  points, the color as four bytes, the kind), where each was a 44-byte command
  with a float color. A run of them is one `TIDE_DRAW_PLAIN` command
  (`plain.first`, `plain.count`) that goes on while the blend is the same and
  nothing is recorded between; what settles first (a camera, a clip) ends it.
  A tint or a transform changes the shape as before (`plain_altered`) and then
  records it. The color is the bytes the GPU gets (`tide_draw_color_bytes`,
  the renderer's `to_rgba` conversion moved into `tide/draw.h` for both), so
  every frame is the same bytes; read back (`tide_draw_plain_color`) it's
  each byte over 255, not the float the call had. `tide_draw_expand` gives a
  list's commands with each plain shape a command of its own, as they were:
  the embed packet goes out that way (`unquad`, with its quads as meshes), so
  what reads it is unchanged, and tests read lists through it
  (`tests/draw_test.h`: `tide_test_flat`, a copy; `tide_test_flatten`, the
  list itself; `tide_test_color_is`, within half a byte's step).
  `tide_draw_append` carries the records. The GUI moves an area's records
  with it once it's placed, and sizes a plain panel's record
  (`tide_gui_group.panel_plain`, `commands_from`, `plains_from`).
- **The open run** (`tide_draw_list.plain_open`): built first with the run's
  count kept as each shape was recorded and the inline path checking the
  last command (its kind, blend, and that the list's records end with it),
  recording 65,536 rects took 0.183 ms where 44-byte commands took 0.113
  (best of 400, alone), more than the renderer saved, so the frame was
  slower (gated: Vulkan rects 0.463 -> 0.494 ms). Now the list keeps which
  command is a run recording goes on with as it is (1 + its place), and the
  inline path asks that and whether there's room, then stores the record and
  the list's count: 0.108 ms. Everything that records another command or
  changes a setting closes the run first, writing its count: `push`,
  `push_state`, `refresh` (which the blend's setters, and
  `tide_draw_append`'s last blend, now call too; `append` closes it where
  another list's commands change the blend), and the round rect's inline
  path, which leaves that to its out-of-line one. An open run's count is
  the list's records from its first on: whatever reads a run's count asks
  `tide_draw_plain_run`. `tide_draw_color_bytes` packs its four whole
  numbers as a vector of bytes (`packuswb`), where shifting each in took
  about 0.025 ms more for 65,536; the same bytes.
- Measured (65,536 a frame, Vulkan, totals): rects 0.58 -> 0.47 ms, round
  rects 1.23 -> about 0.7, textured quads 1.46 -> about 1.2, cubes 1.63 ->
  1.35 (see benchmarks.md for the gated numbers). Recording rects 0.27 ->
  0.15 ms, round rects 0.71 -> 0.21, cubes 1.21 -> 0.94; quads' recording is
  now mostly the caller building four 32-byte corners for a call that takes
  them, which only a call for sprites would avoid (an API question; built
  since, A sprite in one call).
- Tests: `draw_meshes_of_one_quad_are_kept_as_quads` (`tests/test_draw.c`:
  a quad with -0 and 0 told apart, its corners to the bit, quads merging,
  gradients, other corner orders and tints staying corners and triangles,
  Tide lists' path, `tide_draw_append` with textures); `embed_draw_packet`
  (`platform/tests/embed.c`) draws a sprite in Tide and finds it in the packet
  as a mesh over its corners, with no quads command; the platform pixel tests
  (`runs_drawn_alike` mixes recorded quads with meshes both ways) and every
  draw test as before. With plain records: `draw_list_grows` (100,000 rects
  are one run and one command, and appended as one), `draw_records_in_order`;
  the rest read lists through `tests/draw_test.h`, colors to a byte.

## Recording sprites and 3D instances (2026-10-20, second round)

Round 2 of the performance pass (`notes/benchmarks.md`, Drawing, second
round). Internal to `engine/src/draw.c`; recorded lists, culling decisions and
pixels are unchanged, and no layout changed.

- `texture_slot`: a mesh drawn with the image the last command drew a mesh
  with, unchanged and already noted as drawn this frame, takes that command's
  slot without hashing: what `texture_place` would find and do.
- `tide_draw_triangles_with` keeps the list's index count in a register as it
  copies a mesh's triangles; `grow`'s reallocating part is apart
  (`grown`, never inlined), so what records a command keeps its registers.
- One 3D instance (`instance_at`, what a view drawing a mesh for each entity
  records), where the settings are settled, nothing is between the mesh and
  the world, and the mesh is a valid one of `tide_vertex3`s without levels:
  its transform is read once, into its place in the list's matrices, where
  culling and the side it's drawn from read it, and the place is taken only
  if it isn't culled. The same checks in the same order as before; anything
  else goes through `instance_placed`, the code as it was, apart.
- `bounds_visible` works out its rounding allowance only for a box past a
  plane: the allowance is never under 0, so a box on a plane's inner side
  passes it whatever the allowance is, and the answers are the same. Bounds
  are passed by pointer, read where the list keeps them.
- `tide_draw_mesh_slot` checks the place found last on its thread in a small
  function, the hashing apart (`mesh_slot_found`), and `matrix_read` stores
  what it reads a column at a time, which reads of any width after it find.
- Measured (release, five gated rounds, medians, Vulkan / OpenGL): 65,536
  textured quads recorded in 0.915 / 0.912 ms, from 1.117 / 1.114; 65,536
  cube instances in 1.218 / 1.224, from 1.529 / 1.488; 65,536 round rects in
  0.708 / 0.707, from 0.794 / 0.793 (`grown` apart: a shape's two arrays and
  its command each grow). A sprite's four vertices copied 8 bytes at a time
  instead of by `memcpy` (as `color_read` reads a color) recorded quads in
  0.98 ms against 0.91, and was dropped; a struct-copy loop changed nothing.
- Tests: the native suite's draw, frustum and settings tests (`tests/test_draw*.c`,
  `tests/test_frustum.c`), the generated draw E2Es and the platform pixel tests,
  all passing; nothing new was needed for recording, whose lists are as they were.

## Recording reads what its caller just wrote (2026-10-20)

From the performance pass against the handwritten drawing baseline
(`notes/benchmarks.md`, Drawing against handwritten OpenGL and Vulkan).
Internal to `engine/src/draw.c`; recorded lists, culling decisions and pixels
are unchanged.

- On Windows x64 a struct of more than 8 bytes (a `tide_color`, a
  `tide_float4x4`) is passed by address, to a copy the caller has usually just
  written a float or two at a time. A 16-byte load over several narrower
  stores can't be forwarded from them and waits until they reach the cache;
  the sampling profiler put about half of recording a rect there. `push` reads
  its color a float at a time (`color_read`, `volatile` so the compiler can't
  join the loads again), and a single instance's transform is read the same way
  (`matrix_read`) into the list's matrices and the frustum test. Elsewhere
  (System V passes these in registers) this costs a store and four loads.
  Recording 65,536 rects: 0.337 -> 0.26 ms (Ryzen 9 7900X, Clang 23.1.2).
- `tide_draw_instance` and `tide_draw_mesh_3d` pass the transform on by
  address (`instance_at`, `mesh_3d_at`) instead of copying it into each call
  below; `frustum_camera` compares its three cached numbers directly where it
  built an array and compared it whole; `tide_draw_mesh_slot` keeps the place
  it found last on its thread (`TIDE_THREAD_LOCAL`) and checks that place's
  key first, as a view drawing one mesh per entity asks again and again (keys
  are unique, as only it makes places, and a free place's space is
  `TIDE_MESH_NONE`, which nobody asks for). Together: 65,536 cube instances
  recorded in about 1.51 ms, from 1.58.
- `solid` (round rects, turned rects, ellipses) writes a shape's numbers
  into the list's slot a field at a time (`shape_slot`, `shape_placed`, which
  `push_shape` also goes through) instead of building them and copying the
  whole: 65,536 round rects 0.82 -> 0.795 ms. Copying a just-built shape or
  vertex array a word at a time instead didn't help (shapes: no change; a
  quad's four vertices: 1.13 -> 1.35 ms, slower), and was dropped.

## Window pixel scale metadata (2026-10-20)

`Screen.scale` is a read-only float2 in views and their drawing helpers: physical
pixels per logical window pixel on x/y. It exposes the renderer's existing
ratios, including rounding that can make the axes differ. It does not describe
an arbitrary selected render target or imply target-content validity. The
standard host sets `tide_draw_list.screen_scale` before the generated frame from
`tide_platform_screen_scale`; a custom host may supply its own. A zeroed axis
reads as 1. The metadata is local, needs no GPU allocation/readback and survives
ordinary draw reset like other host metadata. All hosts/game libraries rebuild
for the C layout change. Tests and validation are in `notes/language.md`.


## Explicit skinned instances (2026-10-20)

Owner-approved: `Draw.Instanced(model, instances[, texture/filter/wrap][, vert][, frag][, values])`
and target receivers accept a `SkinnedMesh<V>`. Existing optional
`float4x4 transform` instance fields place the mesh (identity when absent).
An optional `Pose pose` field supplies each item's pose; omission means authored
rest, including zero-clip models and `List<float4x4>`. A field named `pose` must
have type Pose. Explicit invalid or incompatible poses skip only their item.
Ordinary per-item shader fields remain selectively uploaded; pose data stays on
the CPU and shader code cannot sample/read skeletons.

The compiler adapter uses the existing skinned capture/retargeting functions and
records each valid item through the ordinary shaded draw path. It owns each pose
before return, then resets temporary retargeting scratch. Compatible items share
palette pages and hardware-instanced runs; saturated 16-attribute layouts retain
individual palettes. No runtime entry point, shader layout rule, deterministic
pose operation or ownership contract was added. Shared texture/value bindings
are prepared once per call. Written operands retain left-to-right snapshots.
Stored instance lists borrow their current handles when no later operand can
change them, avoiding a full nested-pose copy before capture. Later Tide or C
effects retain an owned list/pose snapshot; `skinned_draw_borrow` checks both
those mutations and the unchanged scratch mark for the ordinary borrowed call.

`skinned_instanced` E2E checks separate-file binding, edited/captured poses,
invalid-item skipping, list edits after capture, zero-clip rest transforms,
identity without a transform field, texture/target calls, operand order, tint
upload and two different poses in one run. The attribute-limit fixture covers
explicit items in both atlas/spare-lane and saturated fallback shaders. Updated
shader goldens and SPIR-V validation cover the custom vert/frag combination.
Platform skinned pixels exercise explicit independent poses, per-item red/blue
tints and depth-target drawing across device recreation. Error fixtures pin pose
field types and CPU-only operations. Editor signature help identifies the model
form and explains the conventions. User docs/spec describe the approved API.

Validation: complete warning-free native/web debug builds passed, followed by
all 2,442 native CTests (2,410 ordinary plus 32 GPU) and all 2,352 web CTests,
with the production docs build passing. The final instance-list borrowing
completion additionally passed all four focused skinned E2Es on native/WASI,
including later list clearing and nested C pose mutation. Final platform pixel
rechecks passed on Vulkan, OpenGL, WebGL and WebGPU. No private prototype browser playtest
was run. Android, Linux/macOS, MinGW and sanitizer coverage remains CI work.

## Frame image metadata (2026-10-18)

tide_draw_list.target_pixels is a borrowed host callback for physical target
dimensions while generated local code records Texture.size reads. It survives
draw reset like screen metadata, records no command and owns no GPU resource.
Hosts/libraries rebuild for this C layout addition. The shader captures only
two uniform ints per source; draw texture bindings stay unchanged. Contracts
and tests are in notes/textures.md.

## Frustum culling (2026-10-15)

The owner approved automatic culling for ordinary draws, conservative developer bounds for deformation and a disable control. `Draw.FrustumCulling(bool)` / `SetFrustumCulling(bool)` and `Draw.Bounds(float3 center, float3 size)` / `SetBounds` are the implementation's provisional names in the destination-specific setting family. Reset forms, early-exit block restoration, target isolation, `tide_draw_defaults` and append use the ordinary contracts. A full size is halved into extents. Negative/nonfinite bounds keep the draw; zero sizes are valid. Views and draw operands always execute; no match state or hashes change.

`engine/src/draw.c` caches geometry AABBs beside captured mesh bytes. Refills invalidate them; LOD levels consult their base mesh, independently of the LOD sphere. Plain meshes, primitives and generated fragment-only 3D draws get automatic bounds. `tide_shader.bounds_position` is the float3 byte offset plus one, emitted only without a custom vert or skinning. Deforming shaders and skinned draws remain uncullable without explicit bounds. The box is after deformation in mesh-local space, before instance/destination transforms; custom clip placement must also be covered by the declaration. Sampling/palette capture can precede rejection.

The fitted destination camera's planes and rounding coefficients are cached by camera/destination/epoch/size and invalidated on reset. Conservative box support tests use double intermediates and a rounding allowance. Infinite projections omit the redundant far plane. Mirroring, nonuniform scale and shear work; nonfinite/projective instance transforms, unknown sizes and unusable bounds keep the draw. `LODCamera` never supplies the culling camera. Bulk APIs filter each item, appending surviving intervals through existing batching and preserving their order. No backend, occlusion, spatial-index or match-visibility feature is added.

Culling happens during recording. The standard host already supplies logical window size; C hosts use `tide_draw_screen_size`. Fixed targets use captured dimensions even without a window size. Append copies already filtered draws without reculling. A source list intended for several destination aspects must disable recording-time culling; rejected geometry cannot be reconstructed from commands. CPU geometry caches can retain offscreen meshes, but wholly rejected runs submit no GPU draw or mesh upload. Place, mesh-cache, shader and draw-list C layouts changed; all hosts/libraries/games rebuild together.

Tests: `tests/test_frustum.c` covers planes/intersections, near/infinite far, mirror/shear/nested transforms, survivor order/batching, shader bounds, invalid/extreme values, target/frame/default isolation, aspect changes, geometry versions, LOD base bounds and append. `draw_frustum` E2E checks nonzero vertex offsets, settings, target control, early exits and operand effects on rejected draws; `skinned_rest_test.c` pins absent automatic skinned bounds. Two diagnostics exercise setting forms. The platform draw fixture compares enabled/disabled pixels with visible content and confirms one fewer GPU draw, also after device loss. Older command-inspection fixtures disable culling for intentional offscreen/origin submissions; original assertions remain.

Release recording probe (Windows, Clang 23.1.2, 65,536 instances, 20 warmup/120 measured frames, no GPU/match): final cached planes, all-visible individual 0.652 ms disabled / 3.132 enabled; bulk 0.209 / 2.227. Alternating visible/offscreen: individual 0.645 / 2.680, bulk 0.208 / 2.120, retaining 32,768 transforms. This measures CPU culling cost, not whole-frame speedup. Before plane caching, all-visible enabled was 3.318 individual / 2.878 bulk. Ignored `build/frustum-bench.c` and result text reproduce the local probe; no benchmark catalog or performance promise is added.

Validation: Clang 23.1.2 and checkout-local Node 24.16.0; warning-free complete native/web debug builds. Final native full suite ran 2,340 tests (2,339 passed; the unrelated server-pack child was killed by its existing 15-second shutdown guard after its client check passed). Its isolated rerun passed in 7.14 seconds. Final web full suite passed 2,253/2,253, including WebGL/WebGPU and exact deterministic pins. Native GPU coverage includes Vulkan validation and OpenGL; culling pixels/draw counts pass before/after device loss. Seven focused runtime regressions and generated settings/metadata/side-effect checks pass on native/WASI. Production docs build and `git diff --check` pass. No CI dispatch; Android, other native operating systems and sanitizer configurations were not run on this Windows machine.

Merged incoming submitted-vertex stats cleanly. The frustum fixture additionally pins that rejected instances remove six indices from `tide_platform_draw_vertices`. Both complete builds and the production docs build pass after merging; all 13 affected native and 15 web renderer/runtime/generated-draw/stats tests pass, including WebGPU stats, both native APIs and Vulkan's two descriptor paths. No broader overlap required repeating unrelated suites.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Rendering and platform

- **A clear of depth alone: `Draw.ClearDepth()`** (2026-10-13; the owner asked for the engine to be fixed where FOLDLINE drew its handheld at a fifth of its size because depth couldn't be set back without the colors; the name and rules are Claude's picks, Provisional in `docs/spec.md`, A clear of depth alone). What's built, and what code relies on:
  - **Tide:** `Draw.ClearDepth()` and a target's `target.ClearDepth()`, no arguments (`compiler/src/builtins.c`, one signature, so a target has it and editors hover and complete it). `docs/game/3d.md` and `docs/game/views.md` are the users' pages.
  - **It's a `TIDE_DRAW_CLEAR` command whose `a.x` is 1** (`tide_draw_clear_depth` in `engine/src/draw.c`), not a kind of its own: everything that carries or counts clears (`tide_draw_append`, the settings' settling, tests that count clears) is the code it was. A list filled by hand has 0 there, a clear as before.
  - **The renderer** (`platform/src/platform.c`): a `STEP_CLEAR` with `depth_alone`, drawn through `depth_clear_pipeline`: the clears' triangle and shaders with a clear color under `TIDE_GPU_BLEND_ALPHA`, which leaves every color image as it is, `TIDE_GPU_DEPTH_RESET`, and no stencil operation, where a clear stores 0. One pipeline for each kind of target (`engine_pipelines.depth_clear`). The clip cuts it as it cuts a clear, and it writes depth in a target of depth alone. No backend changed for it.
  - **Tests:** `check_render_samples` in `platform/tests/targets.c` draws a mesh behind two others with and without the clear, at 1, 4 and 8 samples, and reads the colors that have to stay and the depth after; `compiler/tests/e2e/render_targets` checks a target's and the window's commands.
  - **Validation of both 2026-10-13 additions** (this and the multisampled depth read in `notes/render-targets.md`): see the end of `notes/foldline.md` (tide-prototypes), which records the suites run.
  - **Not built:** a clear of stencil alone; a depth other than the far one.
- **Texture slots are indexed by space and id** (`tide_draw_list.texture_index`, `engine/src/draw.c`), beside the retained texture array. Ordinary texture updates still reuse a slot; target images at different sizes or versions still have separate slots. Frame pruning rebuilds the index after compaction, forgetting clears it, and freeing the list releases it. See `notes/textures.md` for the tests and lookup measurements. The added fields change the C draw-list layout, so C hosts, engine/platform libraries and generated games must be rebuilt together.

- Drawing is one `Draw` (built 2026-10-07, card 1 of the reshaping above; the picks are Provisional in `docs/spec.md`, One `Draw`): shapes, text, 2D meshes, the screen's pixels and clipping, 3D cameras, meshes, and cube, sphere, cylinder and plane primitives, with `Blit`, `window` and `list` beside them. **One `Draw`, and what code relies on:**
  - **The 2D and 3D forms are overloads of one owner, told apart by their arguments** (`add("Draw", ...)` in `compiler/src/builtins.c`, resolved first-fit by `resolve_builtin_call`, so each pair differs by type or count): `Camera(float2, float)`, `Camera(float3, quaternion, float)` and `Camera(float4x4, float4x4)`; `Transform` of a `float2` and the rest, of a `float3x3`, or of a `float4x4`; `WorldToScreen` and `ScreenToWorld` of a `float2` or a `float3`; `Mesh` and `Instanced` of a `List<Vertex>` or a `List<Vertex3>`, which `check_mesh_draw` sends to the one its vertices' `position` says (a 3D mesh takes a transform after its indices, a 2D mesh takes none; `[]` with a transform after its indices is a 3D mesh's). `SetCamera()` and `SetTransform()` with no arguments put both the 2D and the 3D one back (`tide_draw_unset_cameras`, `tide_draw_unset_transforms`), as a target's did.
  - **A render target has the same signature table** (`resolve_builtin_call(str_from("Draw"), e)` for a `TY_RENDER_TARGET` receiver, `e->targeted`; `target_draw_owner` is gone), but for `Blit`, which names its destination and is an error on a target, and the members `window` and `list`. The language server lists `Draw`'s members for a target and `Draw`'s signatures for its calls (`target_owner` is `Draw`).
  - **Every setting is the destination's own, blend and tint included** (`tide_draw_place.blend`, `tinted` and `tint`, where the list had them; `tide_draw_pop` puts them back into the place that saved them, `tide_draw_defaults` resets every destination's, and `tide_draw_append` leaves no destination's blend set). Every `Draw` call selects the window (`e->draw_dims` is 2 for all of them, where `Draw.Blend` and `Draw.Tint` used to select none), so the C generated for a game that only used those two calls differs there, and nowhere else: old and new tidec over every game's source list, with `--layout` and without. A command carries its blend as it did, so the renderer didn't change.
  - **Depth and cull around 2D draws change nothing, and are no error.** Nothing records what a 2D command does with either.
  - **`Draw2D` and `Draw3D` are errors that name the rename**, with the quick fix on the owner's name (`old_draw_owner` in `compiler/src/check.c` for calls, `check_member` for `Draw2D.list`; `builtin_old_draw_owner` keeps the two names reserved, so a game can't declare them). The settings' form errors are as they were.
  - **Tests:** `compiler/tests/e2e/draw_settings` (the overloads, a target's own blend and tint, the `Set` forms with no arguments), `tests/test_draw_settings.c` (`draw_settings_of_a_destination_are_its_own`), `compiler/tests/errors/draw2d_*_renamed`, `draw3d_*_renamed`, `draw_mesh2_transform`, `draw_custom_mesh2_transform`, `draw_custom_mesh3_no_transform`, `draw_target_mesh3_no_transform`, `draw_target_blit`, `draw_member_unknown`; `lsp_complete_builtin_owners`, `lsp_draw_depth`, `lsp_draw_cull` and `lsp_draw_stencil` for the editors.
  - Screen and world conversions (`engine/src/draw_coordinates.c`) go through the frame's current camera, in logical pixels from the top left with y down. Device positions count from the bottom left with y up, as Unity's Input System's do (decided), so views take them through `Screen.Flip(position)` (`tide_draw_screen_flip`: the same point counted from the window's other edge, its own inverse; the name is provisional), never `Screen.height - y` by hand. 3D depth is 0 at the near plane and 1 at the far limit, and picking rays start on the near plane with a normalized direction in the world. The primitives (`engine/src/primitives.c`) are cached unlit meshes, given their full sizes and an optional rotation; a cylinder's axis is its local y, and a plane spans its local xz. Their triangles' fronts are outside, and a plane's is its top (see Draw settings, Which side of a 3D mesh's triangles draws).
- **The cameras as matrices** (built 2026-10-07, card 5 of the reshaping above; that they're readable is agreed, and the shape, the names and each rule are Claude's picks, Provisional in `docs/spec.md`, The cameras as matrices). What's built, and what code relies on:
  - **Tide:** `Draw.WorldToClip()`, `Draw.CameraTransform()` and `Draw.CameraProjection()` (each a `float4x4`, the 3D camera's) and `Draw.WorldToScreenMatrix()` (a `float3x3`, the 2D camera's), a target's as its methods. They're four signatures of `Draw`'s with no arguments (`compiler/src/builtins.c`), so a target has them, editors hover and complete them, and local code alone calls them, by the code the conversions go through; `gen_expr` passes the window's size after the list, as for the conversions. Functions and not a struct read as a member, as a target has `Draw`'s functions and none of its members.
  - **C** (`engine/src/draw_coordinates.c`, `tide/draw.h`): `tide_draw_world_to_clip`, `tide_draw_camera_transform`, `tide_draw_camera_projection` and `tide_draw_world_to_screen_matrix`, of the selected destination, each given the window's size as the conversions are. `camera3`, which the 3D conversions and rays go through, is `tide_draw_world_to_clip`'s arithmetic (`fitted`), and the 2D matrix is `camera2`'s.
  - **The fitting is one function, the renderer's too:** `tide_draw_camera_fit(matrix, width, height)` (`engine/src/draw.c`), which `camera_3d_to_clip` in `platform/src/platform.c` calls where it had the four multiplies. So what `WorldToClip()` gives is the renderer's matrix bit for bit when the two sizes are the same floats.
  - **A camera keeps its parts** (`tide_draw_camera_3d_data.transform` and `projection`, beside `matrix`): what `tide_draw_set_camera_matrices` was given, or for a camera set with a field of view its position and normalized rotation as a matrix and the square projection, which the readers fit (`perspective_camera`; `tide_draw_first_camera_3d` is each frame's first). `camera_3d` takes the whole struct. A camera that reaches a list as a command's matrix alone (`tide_draw_append` replaying another list's commands) has the identity for its transform and the matrix with z turned back for its projection (`camera_of_matrix`), which make the same matrix; one a place still has is copied whole.
  - **The parts given back set the same camera:** bit for bit for one set from matrices, and within rounding for one set with a field of view (its `matrix` is still worked out as it always was, from the rotation's conjugate, never from the parts, so no camera's bits moved and a list records what it did).
  - **A destination with no size** has zeros for all but the transform; a singular camera is given as it is.
  - **Generated C:** all that changed in codegen is the window's size after these four calls, by their names on a `CALL_DRAW`; no diff of every game's C was run for it.
  - **The shadow maps use it:** `docs/examples/shadows.tide`, Sumo's `Cast` (`examples/sumo/play.tide`) and pass 7 of `platform/tests/shaders.tide` say `shadow.map.WorldToClip()` after the target's `SetCamera`, where each multiplied the projection, a flip of z and the transform's inverse by hand. Sumo's pins are what they were.
  - **Tests:** `draw_cameras_are_read_as_matrices` (`tests/test_draw.c`: against `tide_draw_world_to_screen_3d` and `tide_draw_world_to_screen` for each kind of camera, the window's shape and a target's own, the parts setting the same camera, a transform left out, no size); `compiler/tests/e2e/draw_apis` reads all four in a view, the window's and a target's; pass 7 of `tide_platform_shaders` (`run_shadow_checks`) for the same pixels on OpenGL 3.3, WebGL 2 and WebGPU; `lsp_camera_matrices` for the editors.
- **One word for the window, and formats named by what they store** (built 2026-10-07, card 2 of the reshaping above; the words are agreed, and the picks are Provisional in `docs/spec.md`, One word for the window, and formats by what they store). What's built, and what code relies on:
  - **Tide:** `RenderTarget.Window()` and `RenderTarget.Window(factor)`, with formats after them as before, where they were `RenderTarget.Screen`; `Draw.Screen() { ... }` and `Draw.SetScreen()`, a target's own as `target.Screen` and `target.SetScreen`, as they were before the card: the card renamed them `Pixels` and `SetPixels`, which went back the same day (2026-10-07, the owner having left the review's follow-ups to Claude): with the target named `Window`, `Screen` means one thing, the screen's pixel space, as `Screen.width` and `Screen.Flip` do, and `Pixels` sat beside a frag's `Pixel` as two names that read as one; `Draw.Pixels` and `Draw.SetPixels` are errors that name `Screen`, with the quick fix; `Format { Rgba8, Depth, Rgba16f }`, where it was `{ Color, DepthOnly, Float }`. `Screen.width`, `Screen.height`, `Screen.Flip` and `Draw.window` are as they were: `Screen` is the window's measurements alone.
  - **The format names are a scheme** (Claude's pick): channels, the bits of each, `f` for floats, so the formats to come are named already (`R8`, `Rg8`, `R16f`). There's no `Format.Color` alias for `Rgba8` (a pick: one name for one thing, and the first target anyone makes says no format).
  - **The enum's numbers didn't change** (`Rgba8` 0, `Depth` 1, `Rgba16f` 2), so nothing that carries them did: the bits of a target's id (`TIDE_RENDER_FORMAT`), `tide_render_target_recreate` across a hot reload, `tide_gpu_color`, `colorFormats` in `tide.js`. A game that names `Format` has other member names in its header, so another `tide_game` hash: a reload across this change migrates by name, and its targets are made again in the formats they had.
  - **In C the formats are `TIDE_TARGET_RGBA8`, `TIDE_TARGET_DEPTH` and `TIDE_TARGET_RGBA16F`** (`tide/draw.h`; they were `_COLOR`, `_DEPTH_ONLY` and `_FLOAT`). Function names stayed: `tide_render_target_window`, `tide_render_target_screen` (a part of the window), their `_as` and `_of`, `tide_draw_push_screen`, `tide_draw_set_screen`, `TIDE_DRAW_SCREEN`. `gpu.h` is as it was (`TIDE_GPU_TARGET_COLOR`, `TIDE_GPU_TARGET_DEPTH`, `TIDE_GPU_COLOR_BYTES` and `_FLOAT` are kinds of target and of image, not Tide's names).
  - **The old spellings are errors that name the new one, with the rename as a quick fix** (`compiler/src/check.c`): `RenderTarget.Screen` in `check_window_target`; `Screen()` with no arguments and `SetScreen` on `Draw` or a target in `old_setting`, which runs before the name is looked up; `Format.Color`, `Format.DepthOnly` and `Format.Float` in `check_enum_member` (`old_format_member`). Each is a `suggest_replacement` on the name's own token.
  - **Generated C:** old tidec over every game's sources as they were against new tidec over them as they are, with `--layout` and without: 237 games the same, byte for byte, and the 8 that name `Format` differ in the enum's members and their header's hash alone.
  - **Tests:** `compiler/tests/errors/render_target_old_screen`, `draw_old_screen`, `draw_old_set_screen`, `draw_old_target_set_screen`, `format_old_color`, `format_old_depth_only`, `format_old_float`, one a diagnostic (and `render_target_window_*`, which were `render_target_screen_*`); `lsp_window_pixels_and_format_names` for the quick fixes. Everything else that named them was respelled and runs as it did.
- Draw settings change how everything drawn after them comes out (agreed 2026-10-05; `docs/spec.md`, Draw settings, where the picks beyond what was agreed are Provisional). One family, two forms each, told apart by name: the plain name wraps a block and puts back what it found when the block ends (`Draw.Blend(Blend.Add) { ... }`), and the `Set` name stays for the views that follow, until it's set again or the frame ends (`Draw.SetBlend(Blend.Add);`). A form written as the other is a compile error that names the other, and so are the spellings from before there were two (`Draw.Camera(...);`, `Draw.Clip(rect);`, `Draw.Clip();`, `Draw.Screen();`), with the rename as a quick fix. A setting that joins the family gets both forms and the same rule, as stencil did.
  - The family: `Blend`, `Tint`, `Transform`, `Clip`, `Camera`, `Screen`, `Depth`, `Cull` and `Stencil`, each the destination's own: the window's as `Draw`'s, a target's as its methods (since one `Draw`, 2026-10-07; before it, blend and tint were the list's, the same wherever a draw went, and depth and cull were `Draw3D`'s by the rule that a setting is in the namespace of what it changes, the owner, 2026-10-06: "is this also a 2D thing? if not then we should stick it to the rendering namespace that makes the most sense"). Every frame starts from the defaults (`tide_draw_reset`), and the GUI, the profiler and the stats overlay never draw with a game's: `tide_draw_append` and `tide_draw_defaults` start from them, and the overlay's list is its own.
  - In `tide/draw.h`, a `Set` is `tide_draw_set_*` (and `tide_draw_unset_*` with no arguments), and a block `tide_draw_push_*`, which saves what it found on the list's stack and returns a mark, then `tide_draw_pop(list, mark)`, which puts back everything saved since: generated code pops the outermost block's mark where code leaves early (`closing` in `compiler/src/codegen.c`, with the GUI's containers).
  - A setting never comes between draws a renderer would draw at once. A command carries its blend (`tide_draw_command.blend`), a 3D mesh's its depth too (`depth`, below), a tint and a 2D transform are worked into shapes, text and 2D meshes as they're recorded (`plain_altered`, `alter_shape`, `alter_vertices` in `engine/src/draw.c`), a 3D transform into each instance's, and a 3D mesh's and a shader's tint are their run's. The list has the cameras and clips it was given (`tide_draw_list.cameras`, `clips`, a clip naming the clip it's inside and the camera it was made under), and each destination's `tide_draw_place` says which the commands to come go through and which the commands so far play: the commands that say so (`settle`) are recorded before the next one that draws, and only where the two differ. So a setting set to what it was, or set and put back around nothing, records nothing, and the same block around each entity's draw is still one run.
  - Recording a plain shape is a few stores (`push` is part of each function that records one, and what's rare isn't, nor what a tint or a transform does to a shape or a mesh's corners): a change there is measured, as the renderer's path is (`bench/results/draw.jsonl`, `draw-settings-before` and `draw-settings`).
  - A pipeline is made with how it blends, and a 3D mesh's with what it does with depth and which side of a triangle it leaves out, so the engine's are made once for each way the first time a list draws that way (`engine_pipeline` in `platform/src/platform.c`: `engine_made`, for each kind of target a list draws into, by kind, blend, depth and cull, of which only the 3D meshes' kind has more than the first depth and cull), and a game shader's too (`pipeline_for`, by `tide_shader.id`, blend, depth and cull). What multiplies needs a color that's gone to white where it's clear: the shaders do that by a uniform (`fix.x`, a game shader's `tide_screen.w`), so no shader has a version of its own for a blend, and the plain shapes' pipeline has none of it: a run of plain shapes that multiplies goes through the smooth shapes' pipeline. A game shader's color is multiplied by `tide_tint` after its frag (`put_fragment_main` in `compiler/src/shader.c`).
  - Text a transform turns, stretches or mirrors can't be fitted to pixels: it's laid out as upright text of the size the transform's mean scale makes it, and each glyph goes through the transform (`add_text_turned`), so it measures as upright text does; its anchor's place goes through the transform and its box is laid out against it, as upright text's is (`add_text`). Moved and scaled as much each way, it's fitted text of that size.
  - `tide_platform_draw` checks each blend, tint and transform's pixels for every kind of thing that draws, clips in clips, and that a blend, a depth, a cull, a clip, a tint and a transform around each of 200 draws are still one draw call, on OpenGL, WebGL 2 and WebGPU; `tests/test_draw_settings.c` checks the list without a GPU, and `compiler/tests/e2e/draw_settings` what blocks, lists of them and early exits generate.
  - **What a 3D mesh does with depth is a setting, for 3D meshes alone** (built 2026-10-06 as `Draw3D`'s, `Draw`'s since one `Draw`; that depth is a draw setting, and that only 3D meshes have it, are agreed; the enum's name, its modes and what each means are Claude's picks, Provisional in `docs/spec.md`, Draw settings): `Draw.Depth(mode) { ... }` and `Draw.SetDepth(mode);`, with the built-in enum `Depth`: `ReadWrite` (each frame's first, and what 3D meshes always did), `Read` (hidden behind what's nearer and already drawn, writing none: what's see-through) and `None`. What's built, and what code relies on:
    - **It's a destination's own, as every setting is** (`tide_draw_place.depth`): the window's as `Draw`'s, and a target's as `target.Depth` and `target.SetDepth`, so `Draw.Depth` around a target's meshes doesn't reach them. Around 2D draws alone it changes nothing, and is no error.
    - **Only a 3D mesh's command carries it:** `tide_draw_command.depth` (`tide_depth`, the byte that was `unused`), set by `mesh_instances` and, for a 3D shader, `shaded_instances` (`engine/src/draw.c`). Every other command says 0, whatever is set: 2D commands have no depth, so the setting never comes between two of their runs, and `push`, which records a plain shape, is the code it was (nothing to measure against `bench/results/draw.jsonl`). 0 is `TIDE_DEPTH_READ_WRITE`, so a list filled by hand draws as it did.
    - `tide_draw_set_depth`, `tide_draw_unset_depth` and `tide_draw_push_depth` are its forms in C, for the destination that's selected (`TIDE_DRAW_SAVED_DEPTH`, which a pop puts back into its own destination's place), and `tide_draw_append` and `tide_draw_defaults` start every destination from the default.
    - `gpu.h` has `TIDE_GPU_DEPTH_READ` beside the three it had. OpenGL's is `glDepthMask` (in `gl.h` and `tide.js`), which `gl_use` sets with each pipeline that tests depth, and `gl_begin` sets back before a pass's clear, as a clear only reaches the depth that's written. WebGPU's is the pipeline's `depthWriteEnabled` and `depthCompare` (`depths` in `tide.js`, by `tide_gpu_depth`'s numbers: a change to the enum goes there).
    - A clear sets depth back and a blit leaves it, whatever is set: neither's command has a depth.
    - **Generated C has the enum only in a game that names it** (`decl.named`, set where a name is looked up, as for `Random`; `enum_generated` in `compiler/src/codegen.c`, which the header's enums, their text helpers and the layout's enums go by). Every other built-in enum is in every game's header; one more there would have changed every game's header, and with it `tide_game`'s hash. So a game that says nothing of depth generates the C it did, byte for byte: checked by running old and new tidec over every game's source list, with `--layout` and without.
    - The setting is a signature like the others (`add_setting` in `compiler/src/builtins.c`), so both forms, each one's misuse and its quick fix, the lists with commas and early exits are the family's code.
    - Decals that only read depth are still at the surface's own depth where they lie on it, so a game draws them a little off it (Sumo's `ON_FLOOR`): the docs say so.
    - Which side of a triangle draws didn't fall out of this, and is a setting of its own since (below).
    - Tests: `draw_depth_is_the_3d_meshes_own` (`tests/test_draw_settings.c`) for the list; `depth_settings` in `platform/tests/draw.c` for pixels, for ordinary 3D meshes and for ones through a shader (see-through meshes that only read depth blend with each other, stay behind an opaque mesh in front and hide nothing drawn after; one that does neither draws over what's near and hides nothing; blocks and Sets put back what they found; `Depth.ReadWrite` said outright is what a frame starts with, pixel for pixel); `check_render_depth_alpha` in `platform/tests/targets.c` for a target's own depth; `compiler/tests/e2e/draw_depth` for what tidec generates, a target's own beside the window's among it; `compiler/tests/errors/draw_depth_*` for each diagnostic; `lsp_draw_depth` for the editors.
  - **Which side of a 3D mesh's triangles draws is a setting too, for 3D meshes alone** (built 2026-10-07 as `Draw3D`'s, `Draw`'s since one `Draw`; that it's a setting with both forms, that each frame starts with the back of a triangle not drawn, that a target has its own and that nothing in 2D culls are the owner's; that the front is the side a triangle's corners go clockwise on, as Unity's, and the enum `Cull` with `Back`, `Front` and `None` are Claude's picks, Provisional in `docs/spec.md`, Draw settings, with the rules below): `Draw.Cull(side) { ... }` and `Draw.SetCull(side);`. What's built, and what code relies on:
    - **It's built as depth is, part for part.** A destination's own (`tide_draw_place.cull`, a `tide_cull`, 0 for `TIDE_CULL_BACK`, so a zeroed place is a frame's first; `target.Cull` and `target.SetCull` are the same signatures); only a 3D mesh's command carries it (`tide_draw_command.cull`, the byte `kind` gave up: `kind` is 8 bits now, and `push` writes the same four bytes it did), set by `mesh_run` and, for a 3D shader, `shaded_instances`, and 0 for every other command; `tide_draw_set_cull`, `tide_draw_unset_cull` and `tide_draw_push_cull` with `TIDE_DRAW_SAVED_CULL`; a signature like the others (`add_setting` in `compiler/src/builtins.c`), so both forms, each one's misuse, its quick fix and the lists with commas are the family's code; and the enum in generated C only for a game that names it (`enum_generated`). A game that says nothing of cull generates the C it did, byte for byte (old and new tidec over every game's source list, with `--layout` and without: 200 games), so what the default changes is what's drawn, never a game's C or its hash.
    - **The rule is the mesh's own, not the screen's:** the front is the side the corners go clockwise on as the mesh has them, which is the side `(b - a) x (c - a)` points to, and it stays the front through whatever mirrors it on the way to the screen. Three things can, and each says so where its parts are known, never by asking the finished matrix (a camera far from the origin would answer by its rounding):
      - An instance's own transform: `mirrors` in `engine/src/draw.c` is the sign of its 3 by 3 determinant, under 0. The instances that are mirrored are a run of their own, whose command says `TIDE_DRAW_MIRRORED` with its `tide_cull`: `mesh_instances` splits a list given at once, and `tide_draw_shaded_instances` a shader's by the transform that starts each instance. With `TIDE_CULL_NONE` nothing is asked and nothing is split.
      - `Draw.Transform`'s: `TIDE_DRAW_MOVED_MIRROR` in the place's `moved`, worked out where the transform is set, pushed or put back, so a mesh asks of its own transform alone and the two are an exclusive or. A 3D transform's bits in `moved` are `MOVED_3D`: what sets or saves the 2D one leaves both.
      - A camera: `tide_draw_camera_3d_data.mirrored`, which its `TIDE_DRAW_CAMERA_3D` command carries. `tide_draw_set_camera_3d` never is. `tide_draw_set_camera_matrices` is when its transform mirrors or its projection's determinant is above 0, and not both: a projection as OpenGL's has one under 0, as it turns the view's z around, so the identity as a projection is a mirror (it looks back along z). `check_render_depth_alpha` in `platform/tests/targets.c` draws through one, and its quads are wound for it.
      - `tide_draw_append` passes what a command said (`SIDES_KNOWN`), and asks no transform again.
    - **The platform resolves it per step** (`gpu_cull` in `platform/src/platform.c`): none, or the back or the front as the GPU will see them wound, which is the command's side, turned when its instances are mirrored and its camera isn't, or the other way about. Pipelines are by kind, blend, depth and that (`engine_pipelines.made`, `shader_pipeline.cull`, the ones into targets of depth alone among them), and nothing but a 3D mesh's has any but `TIDE_GPU_CULL_NONE`, which is 0, so a pipeline description that says nothing draws both sides.
    - **`gpu.h` says the front goes clockwise in its clip space, y up,** in the window and in a target alike, and each backend makes that so. OpenGL's `gl_use` sets `glCullFace` and `glFrontFace` with each pipeline (`glCullFace`, `glFrontFace` in `gl.h` and `tide.js`): `GL_CW` in the window, and `GL_CCW` in a target's pass, where its vertex wrapper turns y over (`compile`). WebGPU's pipelines are made with `frontFace: 'cw'` and `cullMode` by `tide_gpu_cull`'s numbers (`culls` in `tide.js`: a change to the enum goes there), with nothing to turn for a target.
    - **Every pixel test draws into a target** (`tide_platform_read_pixels`), so OpenGL's window, the one place its front face is `GL_CW`, is seen by none of them. It was checked by hand (2026-10-07): a scratch game with a square and a plane seen from the front and one each from the back, as a page in headless Chrome, `--screenshot`, with `backend=webgl` and `backend=webgpu`: the canvas showed the fronts and not the backs on both. WebGL's canvas is the code a desktop window's is.
    - **A transform that flattens has a determinant of 0, which isn't under it:** nothing is mirrored, and each triangle is drawn or not by how it's wound once flat. A closed mesh still covers its outline (the triangles that faced along the flattening are the ones left, which `cull_settings` pins with a cube), and a flat one may be gone, so the docs say to draw what's flattened with `Cull.None`.
    - **A game's vert can turn triangles round** (mirror its mesh, or turn it inside out), and nothing knows but the game: it draws with `Cull.Front`. `Sides` in `platform/tests/shaders.tide` pins that.
    - **The primitives were wound outward already,** their fronts outside and a plane's its top (`tests/test_primitives.c` had pinned it, by the same cross product): nothing in `engine/src/primitives.c` changed. A plane seen from below isn't drawn.
    - **What it costs:** recording an instance of a 3D mesh takes a determinant more, about 1.7 ns (65,536 instances of a cube: 0.76 ms to 0.87, `bench/results/draw.jsonl`, `cull-before` and `cull`; written four floats at a time it was no quicker). `tide_draw_instance` is `mesh_instances` for a count of one, not a call to `tide_draw_instances`: through the call it was 0.95. Plain shapes are recorded in the time they were.
    - **The audit** (2026-10-07): each 3D game run with no window, with both sides drawn and as it's drawn now, and the pictures compared. Wound the other way, and turned: the docs' `lighting` sphere and the cube of its `shadows` example (which came in with a merge, wound as OpenGL has its fronts, and drew its insides), Marbles' ball, its coin's near face and rim and two sides of its island's skirt, Sumo's floor plate (the rings under its balls were gone) and the renderer bench's cube. Drawn with both sides on purpose: Sumo's shadows, which were flattened then (they're a shadow map since: see The physics games), and its sparks, which glow and add. Marbles' shadow discs are thin cylinders that blended their far cap in too, so their ink went up to what one layer needs for the same look (0.3 to 0.51). `draw3d`, `physics-3d`, Sumo's boxes and balls and the docs' sample triangle were right. What's left that differs is a pixel here and there where a back used to show through a seam.
    - Tests: `draw_cull_is_the_3d_meshes_own` (`tests/test_draw_settings.c`) for the list, mirrored instances, transforms and cameras among it; `cull_settings` in `platform/tests/draw.c` for pixels on each backend, for ordinary 3D meshes and for ones through a shader (each side, mirrored by its own transform, through a transform that mirrors, under a camera placed by a matrix that mirrors and one whose projection is upside down, set and in blocks, flattened; mirrored instances as draws apart, and one draw with both sides; `Cull.Back` said outright is what a frame starts with, pixel for pixel); `check_render_cull` in `platform/tests/targets.c` for a target's own, either way, and `check_depth_cull` for a target of depth alone, which is drawn from the side its own cull says; `Sides` in `platform/tests/shaders.tide` for meshes through tidec's verts and frags; `compiler/tests/e2e/draw_cull` for what tidec generates; `compiler/tests/errors/draw_cull_*` for each diagnostic; `lsp_draw_cull` for the editors.
  - **Stencil is a setting too: `Draw`'s, and a destination's own** (built 2026-10-07; that it's there as it is on a GPU (a whole number from 0 to 255 for each pixel beside its depth, a test against a number of the draw's, and what becomes of the stored one where the draw draws), that it's a draw setting with both forms, and that it has none of a GPU's other stencil state (each side of a triangle apart, what a failed depth test does, bit masks) are the owner's; `Draw.Stencil(test, value[, operation])`, the enums `StencilTest` and `StencilOp` with their members, that it's `Draw`'s with a target's own as a method, what a clear does and what a frame starts with are Claude's picks, Provisional in `docs/spec.md`, Draw settings, Stencil, with the rules below): `Draw.Stencil(test, value) { ... }`, `Draw.Stencil(test, value, operation) { ... }`, `Draw.SetStencil(...)` with the same arguments, and `Draw.SetStencil()`. What's built, and what code relies on:
    - **Whose it is.** 2D and 3D draws both take stencil (a mask for sprites and panels is its commonest use). It's a destination's own, as the numbers it's about are: `Draw.Stencil` is the window's, and its call selects the window (`draw_dims` 2 in `compiler/src/check.c`, as every `Draw` call's does since one `Draw`), and a target's is `target.Stencil` and `target.SetStencil`, the same signatures.
    - **The test's order is the GPU's:** the draw's number on the left, the stored one on the right (`StencilTest.Less` draws where the value is less than the stored number), as OpenGL, WebGPU and Unity have it. `Increment` and `Decrement` stay at 255 and 0 (`GL_INCR`, `increment-clamp`).
    - **In C** (`tide/draw.h`): `tide_stencil_test` and `tide_stencil_op`, each with its default first (`TIDE_STENCIL_ALWAYS`, `TIDE_STENCIL_KEEP`), so a zeroed `tide_stencil` is a frame's first; `tide_stencil_tested(test, value)` and `tide_stencil_stored(test, value, operation)`, which make one of what Tide passes (any numbers: an enum's that's no member's is the first, a value is kept from 0 to 255) and are the signatures' `pack` functions, as a call can leave the operation out; `tide_draw_set_stencil`, `tide_draw_unset_stencil` and `tide_draw_push_stencil`, with `TIDE_DRAW_SAVED_STENCIL`. What nothing reads is 0 (`tide_stencil_stored`: the number of `Always`, unless it's what `Replace` stores, and all of `Never`, which draws nothing), so two that do the same are the same, and `Draw.Stencil(StencilTest.Always, 9)` is none at all.
    - **It reaches a renderer as a camera or a clip does, not in each command.** A command has no byte to spare for it, and `push` is the code it was (nothing to measure against `bench/results/draw.jsonl`). A place has `stencil` and `played_stencil` (a `tide_stencil` as one number, `tide_stencil_packed`), `refresh` counts a difference as unsettled, and `settle` records a `TIDE_DRAW_STENCIL` command before the next command that draws. So one that's set to what it was, or set and put back around nothing, records nothing; the same block around each entity's draw records it once, with their shapes still side by side; and it's the selected destination's alone. `tide_draw_append` carries the commands across, and what was set after the last draw, and `tide_draw_defaults` sets every destination's back, so the GUI's commands are told there's none after a game left one set.
    - **In the renderer** (`platform/src/platform.c`): a `TIDE_DRAW_STENCIL` command sets its destination's (`frame_target.stencil`, `draw_stencil`) and adds a `STEP_STENCIL`, which draws nothing and only ends the run before it, so `run_goes_on` and `add_shape` are the code they were. Every step takes its destination's stencil as it's added (`add_step`), but a clear and a blit, which have none. A step's pipeline is by its test and operation (`stencil_state`, the low byte), never by its number, which is the pass's: `use_pipeline` sets it where the pass has another (`tide_gpu.stencil`), and `begin_pass` starts at 0. So pipelines don't multiply by 256. The engine's pipelines with any stencil are a list of their own (`stencil_pipelines`: by kind, blend, depth, cull, test and operation, and what they draw into), looked through, as a game has a handful; with none, the arrays there were. A game shader's key has it (`shader_pipeline.state`).
    - **`gpu.h`:** a pipeline's `stencil_test` and `stencil_op` (`tide_gpu_stencil_test`, `tide_gpu_stencil_op`, with `tide_stencil_*`'s numbers, by `_Static_assert`s in `platform.c`), zeroed for none, and `stencil(value)`: the number the tests of the draws after it compare with, and what `Replace` stores, which is the pass's whatever pipeline is used, and 0 as a pass begins. A pass that starts cleared sets stencil to 0. OpenGL keeps the number with the test, so `gl_use` and `gl_stencil` both go through `set_stencil` (`glStencilFunc` against all 8 bits, and `glStencilOp(GL_KEEP, GL_KEEP, op)`, both in `gl.h` and `tide.js`), and `gl_begin` turns the test off and clears with `GL_STENCIL_BUFFER_BIT`. WebGPU's is the pipeline's `stencilFront` and `stencilBack`, the same (`stencilTests` and `stencilOps` in `tide.js`, by the enums' numbers: a change to either goes there), and the pass's `setStencilReference`.
    - **A clear sets stencil back to 0, and a blit leaves it,** whatever is set: the clears' pipeline stores with `TIDE_GPU_STENCIL_ZERO` and tests nothing, within the clip as for depth, and a blit's pipeline has no stencil (`pipeline_for`, for `blit`).
    - **The window has stencil on each platform:** GLFW is asked for 24 bits of depth and 8 of stencil (`platform/desktop/glfw.c`), the WebGL context is made with `stencil: true`, Android's EGL config asks for `EGL_STENCIL_SIZE` 8, and WebGPU's canvas already had `depth24plus-stencil8`. A window without it would pass every test, with no error: OpenGL does, where there's no stencil to test.
    - **What a draw touches is what stores,** whatever its color, so the engine's shaders draw exactly those pixels where a step stores: `fix.y`, the second of their `fix` uniform, is 1 for a step whose operation isn't `Keep` (`step_uniforms`) and 0 otherwise, where both shaders are the code they were. With it, text discards where no glyph covers a pixel (under 0.002, half the least a glyph's pixel is), where it otherwise draws its quads clear; and a smooth shape that's clear is drawn, where it's otherwise discarded, at the pixels it covers a 255th of or more (0.004: with its edge on the pixels' own, the pixel just past it is then out on every GPU, where "more than 0" could be a millionth on one). That's a line more in each of those two shaders, measured (`bench/results/draw.jsonl`, `stencil-before` and `stencil`: six runs of each, taken in turn on a machine other sessions were busy on): 65,536 round rects render in 2.81 ms before and 2.83 after by the least of them, rects with text between in 1.72 and 1.81 with middles of 2.10 and 2.09, and recording is as it was. The plain shapes' shader has no `fix`, already draws exactly what it touches (a circle discards past its edge), and is unchanged: a run of plain shapes under a stencil goes through the plain pipeline. On a scaled display a plain circle is a smooth shape (`add_circle`), which is why a clear smooth shape has to store. A mesh stores over all of its triangles, whatever its texture's alpha: a game's frag discards for a mask the shape of a picture.
    - **A target of depth alone has it,** and whatever stores is drawn there (`writes_depth` counts `stencil_stores`): the engine's shapes, text and 2D meshes then have pipelines into it too (`bare`, in `stencil_pipelines`), their color going nowhere.
    - **Generated C has the two enums only in a game that names them** (`named_only` in `check.c`, which `find_in` goes by, and `enum_generated`, with `Depth`, `Cull`, `Wrap` and `Format`). A game that says nothing of stencil generates the C it did, byte for byte (old and new tidec over every game's source list, with `--layout` and without: 238 games). `default` where one of those enums goes names it too (`check_default_value`): `Draw.SetDepth(default)` generated `((Depth)0)` in a game with no `Depth` in its C, which didn't compile.
    - **Not built:** reading a pixel's stencil, in a frag or back into Tide; the operations that wrap round.
    - Tests: `draw_stencil_is_a_destinations_own` (`tests/test_draw_settings.c`) for the list; `check_stencil` in `platform/tests/targets.c` at `gpu.h` (each test and each operation against both ends of what a pixel holds, kept across passes, a pass's number, a draw hidden behind what's nearer, a clear, a target of depth alone and, on the web, the page's own canvas, read as the browser shows it) and `check_render_stencil` through the renderer, at scales of 1 and 2 (shapes, smooth shapes, text against the same text's ink, meshes, each test and each operation, 200 draws in one call, a clear in a clip, a blit, 3D meshes with depth, shaders, a target's own kept from frame to frame, a target of depth alone); pass 9 of `platform/tests/shaders.tide` with `run_stencil_checks` in `shaders.c`, from Tide, in one frame on a display scaled to twice its pixels (one, as that test's frames are slow in a browser with no GPU: on CI's Linux runner it took 110 of its 120 seconds before this pass was there). All the same pixels on OpenGL 3.3, WebGL 2 and WebGPU, where the device is then lost and found again. `compiler/tests/e2e/draw_stencil` for what tidec generates; `compiler/tests/errors/draw_stencil_*` for each diagnostic; `lsp_draw_stencil` for the editors. The docs' `stencil` example shows it (`docs/game/draw-settings.md`, Stencil).
    - **Not run by any test:** the desktop OpenGL window's own framebuffer, as every pixel test reads a target. Checked by hand (2026-10-07, on Windows): a `glReadPixels` of the hidden window, put into `check_stencil` for one run, after a draw that passes on its left half only, which was drawn there and not beside it. Android (its apps of these tests are started by hand), and OpenGL on a Mac, which nothing runs.
- Calls that take a block can be listed with commas before one block (agreed 2026-10-05; `docs/spec.md`, Calls that take a block): draw settings, the GUI's containers and a game's functions with an `Action`, each beside the others. The parser makes each call's block the call after it (`expr.stacked`), so it means what nesting them does, and the rest of the compiler sees nested calls. A call in the list that takes no block is an error at that call. The formatter puts a call a line (`stacks_calls` in `compiler/lsp/analysis.c`).
- Smooth shapes are primitives, drawn by the engine's own shaders: they take no `frag` or `vert` (agreed: not every draw function takes a shader). `Draw.RoundRect`, `Ellipse`, `Arc`, their outlines, the thick `Line`, `WireRect` and `WireCircle`, the turned `Rect`, and `Draw.Shape(shape)`, the general one the others but `Arc` are short for: a built-in `Shape` struct's box or ellipse, turned, with a fill from `color` at its top to `color2` at its bottom (`Color.none`, its default: one color), an outline inside its edge, and an edge that fades outwards (`soft` above 0) or inwards (under 0). See `docs/spec.md`, Views and drawing, and `docs/game/views.md`, Smooth shapes.
  - `Rect`, `WireRect` and the `Line` with no width stay as they were, filling whole pixels, so rects side by side leave no seam (`tiles_leave_no_gaps` in `platform/tests/draw.c`). Only the smooth shapes are drawn by distance.
  - Angles are in radians from +x towards +y in the units the call is in (counterclockwise under a camera, clockwise on the screen), and a fill's first color is at the shape's top as the screen has it, in both. Outlines are inside the edge. `Arc`'s ends are cut straight across, so a width of the radius is a slice of a disc.
  - The list keeps one as a `TIDE_DRAW_SHAPE` command: its center, size and fill in the command, like a rect's, and the rest in `tide_draw_list.shapes` (`tide_draw_shape_data`: its rotation as cos and sin, its corners' radius, its outline, how far it fades, its form). The functions of `tide/draw.h` (`tide_draw_round_rect`, `tide_draw_thick_line`...) fill those themselves; `tide_draw_shape` takes a `tide_shape`, which generated C makes of Tide's `Shape` in a helper (`tide_draw_shape_value`), as the two are different C types.
  - The platform has two pipelines of shapes (`platform/src/platform.c`, `platform/src/shaders.h`). A run of the plain kinds alone goes to the GPU as it always did, an instance of 24 bytes each. A run with a smooth shape in it goes through the pipeline that draws every kind, each shape with a `shape_extra` (32 bytes) in a buffer of its own, so it's still one draw call whatever kinds it mixes. Both pipelines draw the plain kinds from the same shader lines, so a rect is the same pixels in either. The plain pipeline's shaders are what they were before there were smooth shapes, but for their color going out times its alpha (see Alpha is premultiplied under the hood), and a plain shape is added to its run by the code that always added it (a mixed run's plain shapes get their extras when the run ends, `settle_extras`), so a frame of plain shapes costs what it did: a change to that path is measured against `bench/results/draw.jsonl` (`smooth-shapes`). A plain circle's edge is smooth over a logical pixel, which would be soft on a scaled display, so there the renderer draws circles as the smooth shapes they also are (`add_circle`).
  - A pixel is in a smooth shape by how far it is from the edge, in the shape's own frame: a box's distance is exact; an ellipse's is found (three steps towards the nearest point of the edge, then two of Newton's, which 32-bit floats need for flat ellipses), so its outline is as wide all the way round; part of a ring is a ring and the wedge it's cut to. Edges are smooth over one pixel of the target, which the shaders know from `scale` (the platform's `pixel_scale`), and colors blend with their alpha in them, so a fill to a clear color fades and doesn't darken.
  - `tide_platform_draw` checks every kind's pixels, edge by edge, on each backend, and that a run of every kind is one draw call. `tide_platform_draw --bench` has workloads of smooth shapes too, and `--dump <file.ppm>` writes the smooth shapes' scene as an image, to look at.
- Meshes are the general primitive (`Draw.Mesh`, `tide_draw_mesh`): triangles with a position, a uv and a color at each corner, indexed, which the list keeps (`vertices`, `indices`) and draws in order with its other commands. The platform expands each run of them with one texture, filter and wrap into triangles in window pixels, a draw call. A clip (`Draw.Clip`, `Draw.SetClip`) is a scissor, its rect in the units the commands are in, which clears honor and the GUI never draws through; the screen (`Draw.Screen`, `Draw.SetScreen`: `TIDE_DRAW_SCREEN`) is the GUI's pixels, for views. The shapes' instanced path stays as it is.
- 3D is meshes too, kept bare so that games and libraries scale it up: `Vertex3` (`tide_vertex3`: a position, a uv and a color), drawn by `Draw.Mesh(vertices, indices, transform)` (`tide_draw_mesh_3d`) through a camera of their own, `Draw.SetCamera(position, rotation, fieldOfView)` or any `transform` and `projection`. No lighting is built in: normals and what a pixel does with them are a vertex struct and a `frag` of the game's. What the engine has for them is `instance.Normal`, a normal as it is in the world under the draw's transform (`notes/shaders.md`).
- **A mesh draw takes a `Mesh<V>` where its vertices and its indices go** (2026-10-08): `Draw.Mesh(mesh, transform, ...)` and `Draw.Instanced(mesh, instances, ...)`, a target's too, for an asset's mesh (`notes/assets-and-files.md`, An asset can be a `Mesh<V>`). It's the checker's alone: `check_mesh_draw` makes the call's arguments the Mesh's two lists, so nothing in `builtins.c`'s signatures, the shader writer or the draw list knows a Mesh from two lists, and every rule of a mesh draw (the transform a 3D mesh takes, a texture, a vert, a frag, values, runs) is the lists'. What the draw list had to learn is to tell a list of the pack's without reading it (`list_version` in `engine/src/mesh.c`).
- **A mesh marked `[LOD]` draws at a level of detail each instance picks** (2026-10-11; `notes/assets-and-files.md`, Levels of detail, has all of it). What the draw list relies on: a level is a mesh of the list's with no vertices of its own (`tide_draw_mesh_data.base` names the mesh whose it draws with), so commands, runs and their merging know nothing of levels; `tide_draw_instance`, `tide_draw_instances` and `tide_draw_shaded_instances` pick, and the inner functions that `tide_draw_append` and the primitives call don't; `Draw.LOD` and `Draw.LODCamera` are a destination's own (`tide_draw_place.lod`, `lod_camera`) and record no command; and `tide_draw_gather_levels`, which `tide_draw_prepare` calls, makes the runs of one mesh's levels that follow each other a run a level, leaving commands that draw nothing (a mesh of 0, a zeroed run) where the others were. A host says the window's size for the pick with `tide_draw_screen_size`.
- Manual instancing is `Draw.Instanced`, and a target's `Instanced` (`(vertices, indices, instances[, texture[, filter]][, vert][, frag])`): one mesh for each list item, without entities. In 3D, a `List<float4x4>` supplies transforms, or an ordinary struct's optional `float4x4 transform` does (identity without it). A shader's read-only ordinary struct parameter receives the list item; other components still come from the view's entity and singletons are shared. In 2D a vert places each item, and the engine applies no instance transform. Only read fields are sent, beside a 3D transform; an empty list draws nothing, and each compatible run is one GPU draw. See `docs/game/shaders.md`, Instances from a list.
- A game's C draws through the view's list: an extern function takes a `DrawList` (`tide_draw_list *`), which only `Draw.list` is, in views and the functions they call. C calls `tide/draw.h` itself (`tide_draw_vertices` and `tide_draw_triangles` for vertices several batches share), so libraries that hand over triangles (Dear ImGui, Nuklear, Spine) need no copy through Tide lists.
- **Skinned mesh drawing** (2026-10-08, optional pose added 2026-10-10; `docs/spec.md`, Skinned meshes and animation): `Draw.SkinnedMesh(model, transform[, pose][, texture][, vert][, frag][, values])`, and the same on a render target. The model is a typed `SkinnedMesh<V>` and the transform a `float4x4`. Omitting the Pose draws authored rest, including models with no clips; `model.defaultPose` returns an independently editable authored-rest Pose, as `model.skeleton.RestPose()` does. An explicit invalid Pose still records no draw. `Draw.Mesh` and `Draw.Instanced` keep their existing APIs.
  - `check_mesh_draw` retains the original model and optional pose, expands the model into its ordinary vertex/index lists, and plans a skinned shader even for a plain `Vertex3` with no custom stage. Generated draw adapters evaluate the written operands once, in order, use strictly compatible poses directly, or bind compatible separate exports through `tide_skinned_pose_into` (see `notes/runtime-ownership.md`). Failed binding records no draw. Rest and bound transforms live in scratch until capture has owned the current values; the adapter then resets its scratch mark. Two READ_DATA textures hold palette matrices and all hidden vertex influences. Neither stream adds fields to V. Later pose edits and draws do not change an earlier palette capture.
  - CPU sampling and model-space hierarchy evaluation are deterministic, usable in systems and views (`tide/skinned.h`). GPU deformation precedes a custom vert, so its position, normal and tangent are already posed in model-local space. The outer Draw transform and camera run afterward; `instance.Normal` applies only the outer transform. A vert's `this.clip` projects its current posed position, and its `out float4` supplies explicit placement through the ordinary shader path. A shadow target calls the same draw with the same pose and vert; depth-only targets use the existing shaded-mesh path.
  - Loaded skinned records establish immutable canonical-byte provenance once (`tide_pack_skinned_version`, 2026-10-10), after all pack sanitization. Their draws reuse full validation and the exact influence-content hash from pack load, removing per-character scans of unchanged geometry and animation keys. Owned/copied/decoded models and unrelated packed byte lists keep the checked path. Replacement/unload discard proofs, and a packed geometry-default write invalidates any physically aliased model proof through a sorted span index.
  - **Repeated palette preparation** (2026-10-10, model identity shortcut 2026-10-11): a draw list owns a frame-local palette memo for currently proven packed models. Each generic pose lookup first checks current provenance. A matching unique pack proof identity establishes unchanged node/palette-table dependencies; different identities still compare their exact owned bytes, including equal-content reloads and version collisions. The pose's current 40-byte local transforms still receive exact comparisons. The immediate capture path also verifies the retained texture slot's identity/version/shape. Hashes only index those comparisons. Rest/shared values can reuse a captured palette; procedural edits, reused scratch handles, heap COW and snapshots are read as current values. No model or pose pointer survives the call. Distinct pose values prepare separate palettes; independently sampled equal values also reuse a capture. Reset clears the memo before texture pruning; forgetting clears it, and freeing releases its retained arena/index capacities. This adds a private pointer to the C draw-list layout, so all hosts and libraries rebuild together. Draw-only preparation reads already validated parent/inverse-bind words directly; public model-matrix/palette operations retain their checked boundary and exact arithmetic. `tests/test_skinned_cache.c` covers chunked ownership/snapshots, edits, hash-index growth, scratch reuse, list separation, slot changes, reset/forget/free, equal/changed same-offset pack replacement, inverse binds, invalidated proof and ordinary owned fallback.
  - **Omitted rest capture** (2026-10-11): generated adapters retain the known-rest case through specialized immediate/atlas calls. A draw-owned frame-local index uses the current pack proof's unique numeric identity to reuse the first owned rest capture, skipping repeated rest construction, transform hashing and model-table comparisons. Every hit rechecks current proof and retained texture/page guards; reload, unload and aliased writes cannot reuse an old identity. Rejected atlas palette slots take checked immediate capture with base zero. Reset/forget clear the index and free releases its capacities. Explicit Pose draws, including editable defaultPose values, keep their ordinary content checks and separate-rig binding. Three skinned_rest_capture runtime regressions and the rest/attribute-limit compiler fixtures cover parity, edits, growth, invalidation, slots, reset and append ownership.
  - Pose-index hashing uses the existing four-chain `tide_hash_wide` (stripes since 2026-10-20: ecs.md; a cache key, so nothing drawn changed), with exact owned-TRS comparisons still deciding equality. A contiguous scratch pose is borrowed only during synchronous capture; heap/chunked values are staged as before, and deferred preparation still receives an owned copy. Neither change affects public/world hashes, pose arithmetic, palette layout or shader uploads.
  - **Shared palette pages and preparation jobs** (2026-10-10): generated adapters with instance-attribute room call `tide_draw_skinned_atlas`, recording the same three counts plus a matrix base. The compiler packs that base into a spare instance-int lane or adds one integer attribute, within the existing 16-attribute limit; saturated layouts keep the immediate `tide_draw_skinned_data` path and implicit base zero. Currently proven packed models share retained palette pages, normally at most 65,536 matrices (4 MiB); a larger single valid palette gets its own page within the existing shader-data bound. Different poses can therefore join the ordinary mesh/material run with different per-instance bases. Counts and influence indexing remain per pose. Exact owned validated node/palette dependencies and current local TRS are captured at recording time; only matrix math waits for `tide_draw_prepare`. The renderer calls it with the platform pool, and append finalizes its source serially before copying. Sufficient work uses chunked disjoint output slices and at most three additional workers when `run_some` is available, otherwise the ordinary `run` contract; no pool or small work runs serially. Pose order, matrix arithmetic and bits are unchanged. Pages keep unique identities across frames and versions derived from ordered captured content; reset/forget discard pending work, free releases storage, and append copies receive immutable content identities so later source frames cannot overwrite earlier destination draws. Owned/unregistered/invalidated models retain immediate checked capture with base zero. Four `skinned_atlas_*` runtime regressions pin deferred ownership after scratch/heap/pack disposal, exact palette bits, page growth/splitting, stable frame identities, job dispatch/fallback, reset/forget, repeated append snapshots and checked fallback. The platform fixture also compares serial and real-pool preparation of 2,048 distinct poses; compiler E2Es cover different-pose merging and the attribute-limit fallback.
  - Tests: `tests/test_skinned.c` checks interpolation, hierarchy, aliasing, snapshot storage, malformed inputs, separate-file binding and palette capture, with exact pose bits pinned for native/WASI. `compiler/tests/e2e/skinned_rest` checks zero-clip rest poses, independent edits, omitted and explicit forms with textures/custom stages/targets, operand evaluation order, invalid explicit poses, and matching palettes when a separate animation is sampled into the target or drawn directly. `compiler/tests/shaders/skinned` pins generated GLSL/WGSL. `tide_platform_skinned` reads native/WebGL/WebGPU pixels for eight influences per vertex, independent poses, pre-user-vert deformation, custom fields, explicit clip placement with later world-position edits and matching depth/color passes.
  - Not built: animation controllers, implicit playback clocks, and automatic shadow passes. Explicit `Draw.Instanced` poses were added 2026-10-20 as recorded above; ordinary posed draws also batch through palette pages.
