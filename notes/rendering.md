# The platform layer and the renderer

## GPU lists in pools, on Vulkan (2026-11-05)

`TIDE_GPU_MAX_STORAGE` is 4 (it was 7): a shader's GPU lists are ranges of two pools (`notes/compute.md`, GPU lists in
pools), so the renderer's pipelines bind 3 storage buffers at the most: the words pool at 33, the ints pool at 34, a
kernel's run record at 35 (`TIDE_GPU_STORAGE_BINDING` + `TIDE_STORAGE_WORDS`/`_INTS`/`_RUN`; 36 the last a pipeline
can name). Merged after Sixteen textures a draw (below), whose layout for each pipeline's bindings it keeps: a
pipeline's layout is 1 + 16 + 16 + 4 = 37 descriptors at the most (it was 40), pushed where it has no more than
`maxPushDescriptors` and a set of the frame's otherwise, as there; a draw sharing samplers with its lists in pools is
under 32. (The pools' branch alone had one layout of 29 bindings, 12 texture/sampler pairs then storage at 25..28.)
Sets as before with `TIDE_VULKAN_DESCRIPTOR_SETS`. The device requirement `maxPerStageDescriptorStorageBuffers` >= 7 became >= 4,
Vulkan's least, so no conformant device is passed over for storage any more. `gpu.h`'s new `storage_most`
(`vk_storage_most`) is the least of `maxStorageBufferRange`, `VkPhysicalDeviceVulkan11Properties.maxMemoryAllocationSize`
and `VkPhysicalDeviceVulkan13Properties.maxBufferSize`, rounded down to 16 bytes: what a pool grows to. Pools grow by a
new buffer and a `buffer_copy` outside passes; the old buffer is discarded as any is (after the frame's fence). Tests:
compute.md, GPU lists in pools (validation layer, push descriptors and sets). Merged again after One image a texture
(below): its textures at 1 + 2i and samplers at 2 + 2j end at 32, so the pools' bindings and the 37 hold.

## Texture arrays (2026-11-05)

`gpu.h`'s `array`, `array_depth`, `array_layer` and the reads `TIDE_GPU_READ_ARRAY`/`_DEPTH_ARRAY`, on Vulkan, WebGPU
and the recorder; the renderer's `gpu_array`. Details: notes/textures.md, Texture arrays: built.

## One image a texture, its samplers apart (2026-11-05)

The owner's direction for the follow-up to Sixteen textures (below): a texture read several ways is one image binding
and several samplers ("just cause nothing needs [it] isn't an excuse"). The counting rule this changes is
language-visible and **awaits the owner's confirmation** (`docs/spec.md`, Wrap: "A draw reads 16 textures and has 16
samplers"); everything below is Claude's, Provisional.

- **The model.** A shader has textures (images: one for each texture its code reads, however it's read) and samplers
  apart: texture i at binding 1 + 2i, sampler j at 2 + 2j, 16 of each (WebGPU's default per-stage limits, Vulkan's
  least, Metal's samplers). A depth read and compared is one `texture_depth_2d` through a `sampler` and a
  `sampler_comparison`; a texture sampled Point and Trilinear is one image through two samplers. Samplers are shared
  as before by textures every draw reads alike; one whose filter or wrap a `Texture` value carries is that texture's
  alone. Lists and grids read as data are images with no sampler, as before.
- **`tide_shader`** (`tide/draw.h`): `texture_samplers` (1 + another texture) is gone. `sampler_count`, `sampler_reads`
  (`tide_shader_read`, COMPARE among them) and `texture_samplers` (`uint16_t`, bit j for sampler j a texture is read
  through). With `sampler_count` 0 each sampled texture has sampler i of its own, read as it is, and a list none: what
  every hand-written shader and most games' generated C say, so they're unchanged (`samplers_own` in `shader.c` decides
  when tidec writes the fields). Helpers: `tide_shader_samplers`, `tide_shader_texture_samplers`,
  `tide_shader_texture_filter` (the most a run reads a texture between: what `prepare_mipmaps` makes images for).
  A texture's read is its first way's: DEPTH or COMPARE are both "a depth" to the renderer (`texture_to_read`).
- **The sampling word is by sampler** (`tide_draw_run.sampling`, the same 4 bits a slot, two banks of eight).
  `gen_shader_textures` writes a fixed sampler's bits as constants and a carried one's from its texture
  (`tide_sampling_filter_of(j, texture)`).
- **`gpu.h`:** `sample(slot, texture, filter, wrap)` became `image(slot, texture)` and `sampler(slot, filter, wrap)`;
  `tide_gpu_sample(gpu, ...)` calls both for a pipeline whose samplers are its textures' own (the renderer's own
  pipelines, tests). `tide_gpu_pipeline_desc.samplers` became `sampler_count`, `sampler_reads` (TIDE_GPU_READ_DATA a
  binding it hasn't) and `texture_samplers` (only the recorder reads it); `tide_gpu_samplers(desc, reads)` gives the
  bindings either way. `tide_gpu_own_sampler` is gone. The renderer's `shader_pipeline_textures` fills the description
  and `bind_shader_textures` binds a run's (draws and kernel runs alike).
- **Vulkan** (`gpu_vk.c`): a pipeline keeps `sampler_count`/`sampler_reads`; `layout_for` keys `own` by sampler
  binding; `bind()` writes each texture, then each sampler by its kind (`vk.sampled` is by sampler). A texture without
  its smaller images is still bound as its first alone, so any sampler that reads it reads its own pixels.
- **WebGPU** (`tide.js`, `gpu_web.c`): imports `gpu_image` and `gpu_sampler` replace `gpu_sample`;
  `gpu_pipeline_samplers(count, low, high)` gives 3 bits a sampler's read (only for a pipeline that says samplers
  apart; otherwise `ownSamplers(reads)`); `layoutFor(reads, through, ...)` keys by both; `bindGroup` binds textures,
  then samplers. The old cube-only "Trilinear before its mips is Linear" in `gpu_sample` went: a cube's base view is
  bound until its images are made, as before. `tide.release.js` regenerated.
- **Unity** (embed ABI 14): notes/unity.md, One image a texture.
- **Limits and messages:** "this draw reads N textures, and a draw takes 16" (images); "this draw reads its textures N
  ways, and a draw has 16 samplers" (`plan_samplers`), and the same two for a kernel.
- **Tests:** `texture_capacity` (fifteen textures through five samplers; `CapacityWays`: three textures, seven
  samplers, by bits; the depth of `CapacityKinds` one texture through two; fixed samplers shared); `render_depth`,
  `wrap`, `mipmaps`, `raw_texture_fields`, `textures`, `asset_textures` (their helpers count a texture-sampler pair as
  a way); `gpu_limit_samplers` (new) and `gpu_limit_textures`; 17 shader goldens (bindings and which sampler each
  read names); `platform/tests/draw.c`'s `two_ways_shader` (one quarters texture through a nearest and a blended
  sampler, the same pixels as `halves_shader`'s two bindings; prewarmed too) and `shared_shader` in the new form;
  `embed_render_pipeline_carries_samplers_apart` and the reads test's sampler words; `web_runtime.test.mjs`
  (layouts of samplers apart). Native ASan: the compiler suite (2,921), all 67 `gpu` tests on Vulkan, embed and
  `tide_tests`. `tests/test_draw.c`'s bank test still checked slot 12 out of range since the 16-texture change, which
  `tide_tests` failed on; fixed to `TIDE_SHADER_TEXTURES`.
- **Gaps:** Unity's OpenGL Core samples a texture as its own state, so one texture read two ways there reads both as
  its first sampler does (it was "as the frame's last draw of it says" before; no Unity run was made). The renderer
  still binds an image per texture where two textures of one draw are the same image (two fields holding one target):
  they're two bindings, which is what the language counts.

## Sixteen textures a draw, samplers shared (2026-11-05)

The owner, asked whether the 12-texture limit could go (mount-and-rifle's ground and road frags hit it adding a third
shadow cascade): "yes lets go ahead and not cap ourselves to arbitrary limits". Everything below is Claude's, behind
the language (the spec's count rule is unchanged; only the number is), Provisional.

- **Where 12 came from.** No GPU's limit: every pipeline had one layout of 32 descriptors, the uniforms, a texture
  and a sampler of its own for each of 12, and 7 storage buffers (1 + 24 + 7), which is Vulkan's least
  `maxPushDescriptors`. What every backend takes in a stage is 16 sampled images and 16 samplers (WebGPU's default
  `maxSampledTexturesPerShaderStage`/`maxSamplersPerShaderStage`, which Tide asks no more than; Vulkan's least
  `maxPerStage*`; Metal's 16 samplers; Unity's D3D11 16 samplers and 128 images, of which its replay uses 10 inline
  samplers whatever the draw). So `TIDE_GPU_MAX_TEXTURES` and `TIDE_SHADER_TEXTURES` are 16, and storage moved after
  their bindings: `TIDE_GPU_STORAGE_BINDING` is 33 (1 + 2 * 16), storage buffer i at 33 + i, 39 the last.
- **Shared samplers** (`tide_gpu_pipeline_desc.samplers`, `tide_shader.texture_samplers`, `shader_texture.sampler`).
  Texture i stays at binding 1 + 2i; its sampler, where it has one of its own, at 2 + 2i. A texture whose sampling
  every draw does the same way, as its code or its declaration says (a `Sample` that says its filter and its wrap, an
  asset's `[Filter]`/`[Wrap]`, a grid's field, a depth read or compared, a cube's filter), is read through the first
  such texture's sampler that's sampled as it is, and declares none (`share_samplers`, `sampled_alike`, `sampled_as`
  in `compiler/src/shader.c`: the same predicate `gen_shader_textures` uses for what a draw carries). A `Texture` a
  draw or a field gives, whose sampling it says itself, keeps one of its own, set as the draw goes; a list (read as
  data) has none, which was a binding wasted before. So a draw's samplers are never more than its textures, and
  usually a few: the ground frag's 13 take 4 or so.
- **Vulkan: a layout for each set of bindings** (`layout_for` in `gpu_vk.c`, `vk_layout`, cached by texture count,
  which textures have their own sampler, and storage count). A pipeline's layout has only the bindings it reads.
  It's pushed where it has no more descriptors than the device's `maxPushDescriptors` (`vk.max_push`), and written
  as a set of the frame's otherwise (`frame_set` takes the layout): 1 + 16 + 16 + 7 = 40 at the most (37 since GPU lists in pools, above), more than a
  desktop driver's usual 32, though a draw that shares samplers is far under. A device with push descriptors no
  longer needs 32 of them to use them. Pools keep their sizes (16 images and 16 samplers a set).
- **WebGPU** (`tide.js`): `layoutFor(reads, through, storage, compute)` leaves out a sampler a texture doesn't own,
  keyed by which do; `bindGroup` binds none for them. `gpu_pipeline_samplers(low, high)`, a new import that
  `gpu_web.c` calls only for a pipeline that shares any, gives 4 bits a texture (0 its own, 1 + j another's).
- **A texture whose smaller images aren't made yet is bound as its first alone**, so that a trilinear sampler it
  shares reads its own pixels, as a texture with no smaller images is read (what each backend did by choosing a
  blended sampler for it, which a shared sampler can't): Vulkan binds `attach` (a 2D image's first) or a cube's new
  `base` view; WebGPU a cube's `base` view (its 2D textures get their levels only as they're made), dropping the
  cube's bind groups when its images are made. Unity's replay (`hlsl.c`) reads each module texture's own slot's
  sampling bits rather than the sampler's, so its blended fallback for a texture without mips stays the texture's.
- **Tests.** `cmake/web_runtime.test.mjs`: high slots 8..15 of every read kind, the layouts of textures that share
  (two samplers for 16 textures) and storage at 33..39 (33..36 since GPU lists in pools, above). `platform/tests/draw.c`'s `shared_shader`: a texture read
  through another's blended sampler is blended though its draw said the nearest pixel (`tide_platform_draw`, its
  descriptor-set and WebGPU variants). `texture_capacity`'s `CapacitySixteen`: 16 textures, 12 sampled alike through
  one sampler, a depth read and compared, a cube and a list; Textures that say how they're sampled keep their own.
  `gpu_limit_textures` refuses 17.
- **Left as it was:** each way a texture is read still counts as a texture (one sampled two ways, a depth read and
  compared). Making those one image with two samplers would let a draw bind the same image once, but the backends'
  slots are textures (`sample(slot, ...)`), Unity's sampling bits are a slot's, and nothing has hit it.

## GL backends removed (2026-11-05)

The owner's rule: "compute shaders will be a guarantee now, if a render backend can't support it then we should remove it". Compute runs on Vulkan, WebGPU and (recorded) Unity, on Unity's graphics APIs that compute (OpenGL Core 4.5 among them: `notes/unity.md`), so OpenGL 3.3 (the desktop fallback), OpenGL ES 3 (Android's) and WebGL 2 (the web's) are gone. How it's done is Claude's, Provisional.

- **Removed:** `platform/src/gpu_gl.c`, `platform/src/gl.h`, the desktop GL loader (`platform/desktop/gl.c`) and GL context creation in `desktop/glfw.c` (its windows are `GLFW_NO_API`), Android's EGL/GLES path (`activity.c` links neither `EGL` nor `GLESv3`, nor does `tide build --android`), the WebGL half of `platform/web/tide.js` (the GL functions by C name, `checkGL`'s glGetError wrapper, `forgetGL`, `KHR_parallel_shader_compile`, `EXT_color_buffer_float`) and `tide_gpu_gl`. tidec writes no GLSL (`notes/shaders.md`), and the renderer's own shaders (`platform/src/shaders.h`) are WGSL only, SPIR-V at build for Vulkan as before.
- **No backend choice:** `TIDE_BACKEND`, the page's `backend=` and `Tide.backend`, and Android's `tide.backend` intent extra are gone. The web takes any WebGPU adapter the browser gives, its software (fallback) one too: slower, but the game runs, where before it went to WebGL.
- **`gpu.h`:** compute is required of every backend (the renderer's no-compute pass-by path, `can_compute`, is gone); `float_targets` is gone (Vulkan, WebGPU and the recorder all draw into, blend and sample RGBA16F, so the Format.Rgba16f-as-Rgba8 fallback and its message went); `tide_gpu_pipeline_desc.glsl_vertex`/`glsl_fragment` are gone; and so are the test hooks `tide_gpu_gl_no_floats` and `tide_gpu_gl_no_vertex_binding`. `TIDE_GPU_MAX_COLORS` stays 4 and the uniform limits stay as they were (their GLES-era reasons are history; changing them would change the language's limits).
- **What a machine that can't draw is told** (never a silent failure):
  - Desktop (`say_needs_vulkan` in `glfw.c`): no Vulkan loader, no surface extensions, or no device `tide_gpu_vk_open` takes (which says why on stderr): the message goes to stderr and, outside a quiet run (`TIDE_HIDDEN`), to a dialog of the system's: `MessageBoxW` on Windows, `CFUserNotificationDisplayAlert` on macOS, and on Linux (with a `DISPLAY` or `WAYLAND_DISPLAY`) the first of `zenity`, `kdialog` and `xmessage` that's there (`posix_spawnp`; 127 means it isn't). It says the game needs Vulkan 1.3 and that a current graphics driver brings it. The window doesn't open.
  - Android (`say_needs_vulkan` in `activity.c`): the message is drawn by the CPU straight into the activity's window (`ANativeWindow_lock`, RGBA 8888, the regular Tuffy weight from the font atlas, word-wrapped at 20 logical pixels) and drawn again for each window the activity is given, until the player leaves (the back button, which is Escape, or the activity going); then `tide_window_open` fails and the app ends. A frame is posted, so `am start -W` reports the launch done.
  - Web (`sayNeedsWebGPU` in `tide.js`): `tide_web_init_canvas` is a `bool` now (true: WebGPU readied on the canvas). Without a device the page hides the canvas and puts a note in its place (`#tide-needs-webgpu`, `role="alert"`) saying the game needs WebGPU (a current Chrome, Edge or Safari, or Firefox where WebGPU is on) and why there's none (`noDevice`: no `navigator.gpu`, no adapter, or the request's error); the shell's loader shows the same text as its error, and it's printed.
- **GPU timing:** Vulkan and WebGPU timestamps only. The desktop GL timer paths and their mock test (`tide_platform_gpu_timing`) are gone; `tide_platform_gpu_time` asks `tide_gpu_vk_timestamps()` natively.
- **Benchmarks:** `versus_draw`'s OpenGL half (`bench/draw/draw_gl.c`) and `versus_draw_opengl` are gone; its saved OpenGL rows stay in `bench/results/draw.jsonl`. `bench/lag.mjs --gpu software` uses the software WebGPU flags.
- **Tests:** every `*_opengl` twin, `tide_platform_draw_opengl_legacy` and `tide_gpu_test`'s `OPENGL` option are gone; `tide_platform_hidden_runs`' window case finds the window through `tide_glfw_window` (no GL context to find it by). The web tests' new arrangement is in `notes/testing.md`, GL backends removed.
- **Checked by hand:** `tide_platform_draw` with `VK_DRIVER_FILES`/`VK_ICD_FILENAMES` pointing at no driver, under `TIDE_HIDDEN`, printed "This game needs Vulkan 1.3 to draw, and this machine's Vulkan can't draw in a window" (and the driver advice) and exited 1, with no dialog. The dialogs themselves (Windows, macOS, Linux) weren't shown on this machine, which a run nobody watches mustn't do; the Linux code was compiled warning-free with clang 23 under WSL, the macOS branch not at all. Android's screen: `notes/android-and-mingw.md`. The web's: `tide_platform_web_keys`' page in Chrome with `TIDE_WEBGPU_FLAGS="--disable-features=WebGPU,WebGPUService --disable-gpu"` printed "This game needs WebGPU to draw, which this browser doesn't have or has turned off (the browser gave no GPU adapter)…" and gave result 1 within half a second; no test covers it, as every test browser gives an adapter.
- Measurements and validation records below that compare OpenGL with Vulkan, or name OpenGL/WebGL tests, are history.

## Runs out of passes, pulled vertices, a pass count (2026-11-05)

`move_work_out_of_passes` (compute.inc) moves each run and GPU list operation step back over the draws before it
that don't depend on it, so it doesn't split their pass; `tide_platform_render_passes()` (platform.h) counts the
passes the last list drawn began, which `tide_platform_kernels` pins (2; 3 without the move). A pulled-vertex
shader's pipeline has a vertex slot of stride 0, and one with GPU indices draws `draw_indirect` with the list's count.
Details: notes/compute.md, Meshes over GPU lists.

## Occlusion culling's depth, read where it is (2026-11-05)

`gpu.h depth_now(target, &samples)` replaced `depth_far`: a multisampled target's depth is given as its samples
(`TIDE_GPU_READ_SAMPLES`), which the renderer's pyramid kernel (shaders.h, one dispatch) loads, instead of being
resolved by the farthest sample into a texture first; Vulkan makes such depth images `SAMPLED` where the device samples
that many, and turns them back for their next pass. drawing.md, Occlusion's measure made cheaper.

## Occlusion culling's depth (2026-11-05)

`gpu.h depth_far(target)`: what a destination's depth holds now, each pixel the farthest of its samples (a target's
own depth texture; a multisampled one's resolved by the most: Vulkan's `VK_RESOLVE_MODE_MAX_BIT`, WebGPU's pass over
every sample; the window's copied on Vulkan, the canvas's depth sampled on WebGPU), which `Draw.Occlusion`'s pyramid is
made from; and `occlusion.tide`, the renderer's second Tide source. Everything else: drawing.md, Occlusion culling from
a depth pyramid.

## The renderer's own kernels, in Tide (2026-11-05)

`platform/src/cull.tide` is the renderer's first Tide source: `tidec --kernels` makes it `kernels/cull_kernels.h`
in the platform's build (WGSL for the web, SPIR-V for Vulkan, each kernel's `tide_shader` and its bindings by
field name), which `platform/src/cull.inc` includes after compute.inc. The steps of a culled run, its buffers and
fallbacks: drawing.md, Listed runs culled on the GPU. `tide_platform_cull` is its pixel and read-back test.

## Vulkan compute (2026-11-05)

The owner decided compute is guaranteed on every render backend (`docs/spec.md`, Compute on the GPU; `notes/compute.md` is the contract). This is `gpu.h`'s compute on Vulkan (`platform/src/gpu_vk.c`); every pick below is Claude's, Provisional, behind the language.

- **Storage buffers** (`TIDE_GPU_STORAGE`): usage storage, vertex, index, indirect, transfer source and destination, in device memory from the blocks as other buffers. Made without data, a `vkCmdFillBuffer` of zeros goes outside passes (WebGPU's are zeroed). `storage_of` refuses an id that isn't a storage buffer, so a vertex or index buffer can't be bound as storage, copied or read back (it has none of those usages).
- **One layout for every pipeline, drawing or computing** (until Sixteen textures a draw, above, made it a layout for each pipeline's bindings, storage at 33..39; 33..36 since GPU lists in pools, above): 32 bindings of set 0 (the uniforms, 12 texture/sampler pairs, then `TIDE_GPU_MAX_STORAGE` storage buffers at 25..31), each for vertex, fragment and compute. A draw or dispatch pushes the storage it binds, whole (its size, held to `maxStorageBufferRange`), at its binding; a slot with nothing bound is left out, as an unbound texture is. Push descriptors need `maxPushDescriptors` ≥ 32 now, the extension's least: a device with fewer gets descriptor sets, as one without the extension does, from pools that now hold storage buffers too. `TIDE_VULKAN_DESCRIPTOR_SETS` still forces sets.
- **Device requirements added**: a queue family that draws, computes and presents; `maxPerStageDescriptorStorageBuffers` ≥ 7 (≥ 4 since GPU lists in pools, above); workgroups of 256 invocations, 256 × 256 × 64, and 16 KiB of shared memory (WebGPU's default limits, which tidec's kernels keep to). A device without them is passed over with why, on stderr. `drawIndirectFirstInstance` is enabled where the device has it; `gpu.h` has the first instance 0 anyway (but for `draw_indexed_indirect_count`'s records, which take it where `indirect()` says so; since 2026-11-05 `drawIndirectCount` and `multiDrawIndirect` are enabled where there too: drawing.md, Culled runs drawn by a count, joined). `vertexPipelineStoresAndAtomics` and `fragmentStoresAndAtomics` are not enabled: vertex and fragment stages only read storage, which the translator marks `NonWritable`.
- **Compute pipelines**: entry point `compute`, the SPIR-V from `desc.spirv` or a test's `tide_gpu_spirv_of(wgsl)`, through the same `VkPipelineCache` as graphics pipelines (so `pipelines.vulkan` keeps them across runs). `use` refuses a compute pipeline in a pass and a drawing one in a compute pass.
- **A compute pass is a command buffer of its own, as a pass is.** `compute_begin` closes what was recorded outside passes and begins one; writes and uploads recorded while it's on go into a `before` command buffer queued just ahead of it, as a pass's do (so a pass of either kind sees, from its start, every write recorded before its end, as WebGPU's queue writes are). After `compute_end` its command buffer goes on as the one outside passes. Uniforms come from the frame's ring as a pass's do; a dispatch with no uniforms written in its compute pass does nothing, as a draw without them.
- **Barriers, by the stages that write and read**:
  - `compute_begin` (`barrier_compute`): transfers, draws (indirect, vertex input, vertex, fragment, and attachments, which it may sample) and compute before it → compute and indirect reads and compute writes. It's the full one `notes/compute.md` asks for: a compute pass may read anything recorded before it and overwrite what any of it read.
  - Between dispatches of one pass (`barrier_dispatches`), only from the second on: compute writes → compute reads/writes and indirect reads. A pass of one dispatch has none.
  - `compute_end` (`barrier_computed`), only after a dispatch: compute writes → indirect, index, vertex attribute, uniform and shader reads of draws, compute reads and writes, transfer reads and writes. Draws, copies, reads back and writes after it then need nothing of their own.
  - Draws' stages now include indirect reads (`DRAW_STAGES`, `DRAW_READS`), so the pass barrier makes uploaded indirect numbers visible, and transfers after a pass wait for its indirect reads.
  - Images read in `SHADER_READ_ONLY_OPTIMAL` or `DEPTH_STENCIL_READ_ONLY_OPTIMAL` are read by compute too (`layout_use`), so a texture turned there after an upload, or a target's after a pass drew in it, is ready for a dispatch's sampling, and one turned away waits for dispatches that sampled it.
  - Copies (`buffer_copy`, `buffer_read`) record a transfer → transfer barrier first (`barrier_copies`), as `buffer_write` does: copies in one command buffer have no order of their own. A copy onto overlapping bytes of the same buffer is skipped (Vulkan and WebGPU have none).
- **Dispatches**: `dispatch` with any count 0 records nothing; counts are held to `maxComputeWorkGroupCount`. `dispatch_indirect`, `draw_indirect` (stride 16) and `draw_indexed_indirect` (stride 20, `drawCount` 1) need a storage buffer, an offset that's a multiple of 4, and their 12, 16 or 20 bytes inside it, or record nothing. An index buffer of a draw may be a storage buffer (`indices`), as may a vertex buffer.
- **Reads back** (`buffer_read`): each read copies into a buffer of `HOST_CACHED` coherent memory of its own (a power of two from 256 bytes), followed by a transfer → host barrier, and remembers the submission it goes with: the next one, whichever sends it (a frame's present or a flush). `buffer_read_done` copies once that submission is done, which it knows by `vk.completed` or by asking a frame's fence with `vkGetFenceStatus`, never waiting; a read not yet submitted is 0. `buffer_read_wait` flushes (submit and wait idle) when any read is still on its way. A read's buffer is kept for the next as one of four spares, the biggest ones, taken by a read of at most a quarter of its room; others go with the frame's garbage. A lost device (a failed submission, `tide_gpu_vk_lose`) makes every read still on its way -1; remaking the device frees every read's memory and marks those not yet taken lost.
- **Tests**: `tide_platform_compute` (`platform/tests/compute.c`) and its twin `tide_platform_compute_vulkan_sets` (`TIDE_VULKAN_DESCRIPTOR_SETS=1`), label `gpu`, under the validation layer with synchronization validation. After its checks pass on Vulkan, the test begins a read of the tally, checks it isn't there yet (`buffer_read_done` 0, without waiting), loses the device (`tide_gpu_vk_lose`), checks the read comes back -1, and records and checks everything again on the next device. Both pass with the translators' compute (`t3/wgsl-compute-storage-translators`, merged), as do `tide_platform_draw` (with its device-loss pass), `_draw_vulkan_sets`, `_targets`, `_shaders`, `_skinned`, `_swapchain`, `_gpu_time`, `_pipeline_cache`, `_gpu_wait` and `_gpu_wait_headless`, all under the layer.
  - What catches a missing compute barrier is the test's data, not the layer: with `barrier_dispatches` left out, the reversed groups read zeros and the test fails, while synchronization validation (SDK 1.4.363) reported nothing, with push descriptors and with sets. Nor did it report a missing pass barrier for storage a fragment shader reads (`barrier_pass` left out: the compute test passed). It does report that barrier's hazard on vertex buffers (`tide_platform_draw` stops at `SYNC-HAZARD-READ-AFTER-WRITE`). So the layer doesn't track storage buffers reached through descriptors here, and the barriers above rest on reasoning and on the test's data.
- **Gaps**: compute passes aren't timed (`pass_time_*` times render passes only). Every read makes or takes its own buffer; reads aren't batched into one. A real device loss is still simulated. Linux, macOS (MoltenVK) and Android Vulkan need CI or a machine of theirs; Android's devices are where the new requirements (storage buffers a stage, 256 invocations) could pass one over.

## Fonts of a game's own, and the checked font reader (2026-11-03)

The owner asked for custom fonts, several in one game ("yeah I want custom fonts; we should also allow multiple fonts to be used for different text on the same game of course"), for the GUI theme too ("yeah themes now need a font"), and for runtime decoding, "careful with security here". The language surface is in `docs/spec.md`, Views and drawing, Fonts; what's below is how it's built.

- **The font reader** (`engine/include/tide/font.h`, `engine/src/font_data.c`, Claude's under the drawing exception, see `notes/runtime-ownership.md`). `tide_font_data_make` reads a TrueType file into *font data*: a 64-byte header (units per em, hhea's ascent, descent and gap, counts, and an id: the first 8 bytes of the SHA-256 of everything after the header, 2 or more), sorted runs of code points to glyphs (format 13's "all one glyph" runs flagged `TIDE_FONT_RUN_SAME`), a 20-byte record a glyph (advance, bearing, box, its vertices) and 10-byte vertices (moves, lines, quadratic curves, stb_truetype's `stbtt_vertex` minus cubics). It reads what stb_truetype reads of a TrueType font, the same way, so its outlines are stb's vertex for vertex: `font_data_is_what_stb_truetype_reads` (`tests/test_font_data.c`) compares every code point under 0x10000, every glyph's metrics and box and every vertex of both Tuffy weights with stb's own reader. What differs is that every read is checked against the file (the `inside` spans), and limits bound what a file can ask: 64 MiB, 16,384 points and 65,535 vertices a glyph, 4 Mi vertices in all, 256 Ki runs, composite parts 8 deep (a part of itself ends there, `font_data_refuses_glyphs_of_themselves`), and a work budget (`MAX_WORK`, 64 Mi vertices copied, parts followed and code points read one by one) so nested composites or overlapping format 4 segments can't take minutes. stb_truetype itself trusts its file: it reads outside a broken one and recurses forever on a self-referencing composite.
  - Differences from stb, all for files stb would misread: a format 4 segment with an idRangeOffset adds idDelta to the glyph it reads, as the specification says (stb doesn't; Tuffy has no such segment); a contour whose first point is off the curve takes its next point from its own contour; loca ranges past glyf are a broken file, not reads past it; a composite part placed by matching points is placed at its origin (stb asserts); overlapping cmap runs keep the one whose first code point comes first, sorted by first code point, count and glyph so every platform orders them alike.
  - Refusals say why (`tide_font_error_text`), and CFF outlines are refused with the advice to convert to `.ttf`: reading them safely would be a second parser (Type 2 charstrings), not built.
  - `tide_font_data_check` is what makes any bytes safe to draw: counts that add up to the size, runs in order inside Unicode and the glyphs, each glyph's vertices inside the vertices and starting with a move, every vertex a move, line or curve. Coordinates can be any 16-bit values; the renderer copes with those (below).
  - `font_data_refuses_what_isnt_a_font` reads every length the regular weight can be cut to (every byte to 4 KiB, then every 97th), and `font_data_survives_broken_files` 560 copies with random bytes changed in each table and in the directory; under ASan, every read stays inside, and what reads passes the check.
- **The registry** (same files): `tide_font_add` checks font data and keeps a copy, once per id, in memory of its own never moved or freed, so `tide_font_find(id)` is the same pointer until the program ends and draw lists hold it; `tide_font_decode` is make then add. At most 1,024 fonts and 512 MiB. A spin lock guards it (views and the GUI can measure from more than one thread).
- **The renderer** (`platform/src/font.c`) reads only font data, the engine's two weights' too: the regular weight is made from `tide_font_ttf` the first time anything measures or draws, the bold from `tide_font_bold`'s file. stb_truetype only rasterizes now. A *weight* is any font: a growing table of them by id, the regular one first, each with its own measures and fitting, a glyph's key their place in the table. A font whose id is known again at another address (a game library built again) is read there; the same id is the same outlines, so its glyphs stay good. A font of a game's C that isn't the registry's is checked the first time it's seen. A glyph a font has none of is the regular weight's, as bold's always were. Hostile coordinates: a glyph whose fitted outline is past what a short holds at whole pixels, or wider than the atlas, has no pixels (never cast past a short, and never marks the atlas full, which would clear it every frame); advances are held to a short; a font's line pitch to at most 4 lines' worth.
- **Draw lists** name fonts by place: `tide_draw_list.fonts`, each font once a frame, and a text command's `words.font` is 1 + its place (0 the regular weight), replacing `words.bold` and the list's `bold`. `tide_draw_append` carries a command's font across. The C API takes `const tide_font *` wherever it took a weight's file (`tide_draw_text_at`, `tide_measure_fn`, `tide_platform_measure_text`, `tide_draw_measure_text`); `TIDE_FONT_BOLD` is `&tide_font_bold`, defined in the bold file's own object (`platform/font/tide_font_bold.inc`, appended by `cmake/embed.cmake`'s new `APPEND`), so the linker still takes the file only for a program that names it.
- **Tide's `Font`** is `TY_FONT`, a `tide_font_ref` (its number: 0 regular, 1 bold, else the registry's), no longer an enum. It's this machine's as a `Clip` is (`holds_render_resource`), but a constant can hold one and a parameter default to one (`holds_resource_but_font`), so `const GUITheme` constants and `Font font = Font.Bold` parameters keep working. `.Bold`/`.Regular` shorthand where a Font goes is rewritten to `Font.Bold` (`check_expr_want`, a conditional's sides too), and a builtin overload fits it only where its parameter is a Font or an enum that has the member (`fits` in builtins.c), so `Draw.Text(..., color, .Bold)` takes the font overload. `Font(n)` is gone. Generated `tide_font_file(ref)` is `tide_font_find`, with `TIDE_FONT_BOLD` for 1 only in a game that says `Font.Bold`.
- **Assets and decoding**: `asset Font` (`ASSET_FONT`) reads the file with `tide_font_data_make` at build time, an error at the declaration saying why; the pack keeps its font data in a clip-shaped block (`TIDE_PACK_FONT`, and `TIDE_PACK_FONTS` for a map; lists are `TIDE_PACK_LIST` of them), which loading adds to the registry (`note_font`, `tide_pack_font_of`), laying lists and maps out as `tide_font_ref`s as it does clips. `Font.Decode(bytes)` (`CALL_FONT_DECODE`, `check_font_decode`) is local code only, failing with `IOError.InvalidData`; its helper flattens the list and calls `tide_font_decode`.
- **The GUI**: `tide_gui_theme.font`, Tide's `GUITheme.font`, which the widgets measure and draw in (`text_width`, `text_at` in `engine/src/gui.c`).
- Tests: `tests/test_font_data.c` (above); `tidec_e2e_fonts` (`compiler/tests/e2e/fonts.tide`): an asset's font, a glob's list and map, a decoded one that's the same outlines as the glob's second and so the same registry font, a non-font refused, a font passed on and chosen by shorthand, measuring in one, the theme's, and each font once in the list; `compiler/tests/errors/font_asset_not_a_font.tide`, `font_decode_in_match.tide`, `font_unknown_weight.tide`. `tidec_e2e_text`, `tide_platform_font` and the platform draw tests run as before.
- Gaps: CFF outlines; kerning (GPOS/kern) and hinting; variable fonts' axes (the default instance draws); a collection's other fonts; a decoded font after a hot reload is the regular weight until decoded again (the new library's registry hasn't it); no `==` on Fonts.

## GPU timing fixture waits for pass observations (2026-11-03)

macOS web run 41669641001 passed 940/941 tests; `gpu_time_webgpu` missed the
offscreen-only fallback-clear observation in its second round, despite valid
normal pass samples. The fixture drew that case on just one frame. WebGPU's
three busy readbacks may leave a frame untimed, and device loss discards pending
observations, so normal samples do not imply that one particular probe arrived.

The fixture now alternates ordinary and offscreen-only frames after its initial
pixel check. Each round waits for all required observations, including two
offscreen-only samples, or the existing 3,600-frame patience limit. The 60-frame
minimum, outer deadlines, device-loss/reopen work, exact pass metadata checks,
overlay exclusion and failing assertions remain. Two samples ensure that a
single lucky probe cannot satisfy the repeated-case coverage. No renderer or
backend behavior, timing capability or release gate changes.

Validation: four focused Windows ASan CTests pass (real Vulkan/OpenGL timing,
native GL mock and WebGPU mock), plus all three web timing CTests (WebGL,
WebGPU and its deferred-readback mock). Builds are warning-free and browsers
use the existing muted runners. Actual macOS confirmation remains CI coverage.

## Mesh geometry attribution (2026-11-03)

Renderer mesh/shader steps retain draw-list contribution chains and collect them
only after issuing a GPU draw, outside overlay drawing. Counts need no timing
query, wait or backend changes, and include repeated render-target passes and
selected LOD indices. Shapes/text/unshaded 2D work remains aggregate-only.
LOD fixture checks both repeated pixel-read submissions and regrouped runs on
built-in/custom shaders. Standard-host.md and drawing.md describe reports and
provenance lifetime/budgets.


## Attributed GPU pass timings (2026-11-03)

Owner-approved extension of structured captures: optional independent pass
queries on tide_gpu, with accepted uint32 tokens returned alongside results.
The renderer retains up to 1,024 pending identities (target, render-pass order,
pixel extent and distinct shader labels), counts actual submitted calls and
vertices, and copies labels before a game library can unload. Mixed passes
retain their shader membership, with no invented per-shader/view time or source
location. No draw-list/compiler boundary or language change is needed.

Vulkan resets/writes pairs directly in existing pass command buffers, before
pass setup and after rendering/layout restoration; no added pass, transfer
barrier or submission. Submission serials and availability guard reads, like
the total timer, including mid-frame flushes. Desktop GL uses independent
GL_TIMESTAMP pairs (counter-width wrapping handled) alongside GL_TIME_ELAPSED.
WebGPU retains each pass's token in its existing frame readback and returns all
completed pass observations even when frame readbacks settle out of order.
Pass intervals may overlap and omit uploads/mips outside render passes: their
sum is not the existing frame timer. Timers remain optional on each backend;
WebGL/ES and the Unity recorder have none.

Collection is capture-only. No pending slot or unsupported/full query means an
explicit unmeasured pass; oversized shader membership discards that pass's
attribution rather than truncating identities. Reset/device loss cancels owned
metadata and counts those observations as unmeasured. Polling does not wait;
export reports pending observations. Backend query resources are released on
reset. Shader labels and renderer pass identities do not enter match state.

Validation: focused Windows ASan native GPU timing/mock/profile/draw/target
checks pass on Vulkan (with synchronization validation) and OpenGL. Web-debug
GPU timing, draw and target checks pass on WebGL and WebGPU. Real-device timing
checks two distinct destinations, pixel sizes, pass order, shader membership,
exact submitted counts, overlay exclusion, unchanged pixels, reopen and forced
device loss while pass queries exist. Offscreen-only frames explicitly select
the window before attributing its fallback clear. GL mocks
cover independent queries alongside the total, delayed results, capacity and
reset. WebGPU mocks cover out-of-order tokens, 1,024-query pressure, invalid
results and stale callback cancellation. Report/schema coverage is recorded
in standard-host.md. Other OSes and Android were not run locally.


## Match updates during desktop Vulkan frame waits (2026-11-03)

The desktop's `tide_gpu_vk_present_updating` polls the next frame slot's fence
after submission and presentation. If it is still pending, the platform may
run the host's match-only callback between timed waits. The callback receives
elapsed time since the render frame began and returns its next update delay
from that point; platform.c subtracts time spent in the callback. Waits follow
that deadline, bounded to 1–100 ms, and finish early when the fence signals.
A ready fence invokes no callback. A disabled callback uses the ordinary
indefinite wait.

`tide_platform_update_while_waiting` is optional and active only after the
frame callback returns `TIDE_KEEP_RUNNING`, during presentation. It must not
poll input/events, render, reload, or change GPU/window resources. Desktop
GLFW supplies it; Android retains `tide_gpu_vk_present` without an updater,
and OpenGL/web are unchanged. Headless runs never call the host updater, so
their made-up time and scripted frame boundaries remain unchanged.

Two frame slots remain in flight. Fence completion still precedes garbage
collection and command-pool, descriptor-pool and upload-ring reuse, in the
same order. No work is queued farther ahead. This services networking during
the measured end-present fence bottleneck; it does not reduce GPU workload.
Swapchain acquisition, `vkQueuePresentKHR`, resize/device-idle, readback and
mid-frame flushes can still block and never invoke this callback.

The private `tide_gpu_vk_wait_for_test` hook intercepts only end-present fence
waits and supplies the real wait function; even a claimed success is checked
against the real fence before resources are reused. Native GPU fixtures
`tide_platform_gpu_wait` and `_headless` force pending waits and cover deadline
subtraction/bounds, ready fences, absent/withdrawn callbacks, exclusion during
drawing/acquisition/readback, simulated device loss, pixels after frame-slot
reuse, and headless exclusion. Both fixtures pass with Windows ASan and
Vulkan validation, as does the complete 2,989-test native suite. Coordinated
web validation and real-game measurements are recorded in netcode.md.

## Gap integration validation (2026-11-02)

Camera-origin uniforms and target content revisions were validated together after
integration: all 2,873 web-release CTests passed; native Windows ASan passed
2,905/2,906 non-GPU tests initially, then the sole Chrome room-hosting timeout
passed on its isolated retry without a code change. All 80 native GPU-labeled
tests passed (2,986 native tests covered in total). The docs production build
passed. External prototypes were excluded from these full engine suites.
Other OS/Android and an actual Unity editor run remain CI/host coverage, not
local claims. Target revision/reload specifics are in render-targets.md.

## Distant cameras retain local geometry precision (2026-11-02)

The renderer carries recorded camera-data references through destination state
and draw steps, fitting the stored relative projection and uploading the camera
origin. Built-in mesh uniforms grow from 96 to 112 bytes. Generated-shader
headers stay 96 bytes in 2D and are 112 in 3D, with matching pipeline sizes and
payload offsets. Both built-in and generated vertex placement rebase model
columns before multiplying their local vertex. The two handwritten 3D test
shaders follow that same contract; 2D capacity fixtures are unchanged.

Unity's scene-view override supplies only an already-combined matrix and keeps
origin zero. The ordinary embedded draw path receives the corrected camera
records. Source review confirms gpu_record records explicit uniform sizes and
bytes, and TideRender copies/aligned-uploads those bytes without fixed header
offsets; regenerated shader identity carries the new layout. No recorded packet
shape or embed ABI number changes. Unity itself was not run.

The dedicated camera_precision fixture uses eye (3000,200,3000), near .04 and
far 80000, with a tilted plane intersecting an instanced box near distance 140,
five yaw angles and 81 interior probes. It compares translated/origin cameras,
default projection/this.clip/authored explicit clip, built-in mesh placement,
world-space material and Instance.transform sentinels, and appended/direct
lists. Native Vulkan and OpenGL pass. A temporary control setting camera_data
to zero restores the old combined matrix and fails both APIs: five sampled
pixels change which surface occludes the other, and amplified depth differs;
authored explicit clips stay invariant. Restoring camera data passes again.
The final fixture additionally covers built-in FOV cameras and passes all five
backend checks: native Vulkan/OpenGL (about .55s each), WebGL2 (1.49s), ANGLE
D3D11 (1.34s) and WebGPU (1.07s). Existing shader pixel checks pass across those
backends too. The explicit reference uses the inverse of the untranslated camera
transform, matching the custom camera's matrix arithmetic; using a conjugate
instead differed at two intersection pixels on WebGL from ordinary rounding.
No tolerance was relaxed. Broad suite results are recorded with integration.


## Native cutout stored alpha (2026-11-02)

Desktop OpenGL and Vulkan with the optional `alphaToOne` feature now store
opaque alpha after hardware coverage selection for single-output cutout
pipelines. State resets and capability gating are in the backends; shader
generation is unchanged. The HDR resolve/resample regression, the secondary
output control and unresolved MRT/web cases are recorded in
`notes/render-targets.md`, Native single-output cutout alpha.

## The device's own memory (2026-11-02)

`tide_gpu.device_memory` (`gpu.h`, optional) is the bytes of memory a discrete GPU has of its own: Vulkan's
biggest device-local heap there (`vk_device_memory`), 0 for an integrated GPU or a phone, NULL on OpenGL, WebGL,
WebGPU and the recorder. Its one reader is the idle image budget, a sixteenth of it where that's more than
128 MiB. Why, what it fixed (Mount & Rifle's forest maps sent to the GPU again after a frame out of view), the
measurements, tests and the other hitch causes found (cold pipelines in runs with no window, Vulkan's second
image for runtime mips): `notes/textures.md`, Idle images by the device's memory.

## Meshes a frame skips stay resident (2026-11-02)

Internal optimization from the 2026-11-02 review. Until now a 3D mesh place
the frame didn't draw was freed in `tide_draw_reset`, and its GPU buffers became
spares. So a terrain/voxel chunk that left the view, or that a game skipped for a
frame, was copied and uploaded again when it came back. Meshes now get an idle
budget like the one inactive pack images have (`notes/textures.md`).

- **The budget** (`tide/draw.h`, beside `tide_draw_mesh_data`):
  `TIDE_MESH_IDLE_FRAMES` 36,000 lists (ten minutes at 60 Hz, the textures'
  number), `TIDE_MESH_IDLE_COUNT` 4,096 meshes and `TIDE_MESH_IDLE_BYTES`
  64 MiB of copies. The list and the renderer each apply it. Meshes drawn this
  frame are outside it. When over budget, the longest-unused meshes go first;
  equal ages go by place, so eviction depends only on the list and is the same
  each run. Sorting happens only under pressure.
- **The list** (`forget_idle_meshes` in `engine/src/draw.c`). An undrawn place
  is kept unless one of these holds:
  - Only one frame drew it (`tide_draw_mesh_data.made == frame`). So a mesh
    rebuilt every frame still goes at the next reset, as before, and the
    renderer's spare-buffer reuse for it is unchanged. (Since 2026-11-05 such a
    mesh stays `TIDE_MESH_ONCE_FRAMES` frames within a budget of its own, on
    the GPU too: `notes/drawing.md`, Idle listed regions, held pages and
    one-frame meshes. A mesh of a world's lists the renderer has keeps no
    copy in the list while idle, and the renderer's budget is a sixteenth of
    the device's memory, farthest from the camera first: same file, Turning
    round.)
  - The frame drew the same lists (space, id, other) at another version. The
    old copy can never draw again. Scratch and pack lists all share one id
    (`mesh3_slot`), so only the budget evicts those.
  - It's past the budget.
  A level of detail goes when its base mesh goes (`forget_unplaced_levels`).
  The first pass frees one-frame and expired places without allocating; the
  tables are built only when something is left to weigh. `tide_draw_forget`
  still frees every place. Identities were already content-keyed (C meshes
  carry `tide_mesh_version`, and Tide lists `list_version`), so a kept place is
  never stale.
- **The renderer** (`spare_unused_meshes` in `platform/src/platform.c`). An
  unwanted GPU place stays while the list's place still has the identity it
  uploaded (`gpu_mesh_current`, which `mesh_for` now shares) and the list drew
  it in more than one frame. The same budget applies by `tide_texture_idle_trim`,
  with GPU bytes and render-frame ages. Everything else becomes a spare, as
  before. `forget_meshes_3d` is gone; a frame's end frees only spares no mesh
  took. So a skipped mesh's buffers are owned, counted in `vram`, and no spare
  outlives its frame. A place the list evicted is spared at the next draw, the
  same timing as before. A new device epoch still clears every GPU place; the
  list's copies upload again. Overlays never spare and are unchanged.
  - The one-frame rule is needed here too. The list lets a mesh go only at the
    reset after the first frame that skips it, so during that frame the list
    still has it. A first version kept such a place on the GPU for that one
    frame. `check_gpu_memory` (`platform/tests/targets.c`) caught it: a
    blit's quad, drawn once, stayed counted after the frame without it. With
    the rule, a mesh rebuilt each frame takes exactly the old path.
- **The old failure** ("a version that kept spares across frames failed
  `transient_data_textures`"): that version kept *unowned* spares, which
  showed up in `vram` after the frame. This version never keeps a spare. The
  fixture's expectation changed on purpose: its quads, drawn in phases 0 to 2,
  stay counted while phases 3 and 4 skip them. It now starts from a forgotten
  list, so meshes earlier fixtures leave idle don't move its baseline at its
  phase-7 forget.
- **Frustum culling** is unchanged. A culled instance still finds its mesh
  place, so the list kept those meshes already. Now the GPU keeps their
  buffers too (under the budget) instead of re-uploading when they come back
  into view.
- **Tests:**
  - `draw_idle_meshes_are_kept_for_when_drawn_again` (`tests/test_draw.c`)
    covers kept-and-found uncopied, one-frame meshes going, a newer version
    superseding, scratch meshes not superseding each other, expiry at exactly
    36,000/36,001 frames, and forget.
  - `draw_idle_meshes_past_the_budget_go_longest_unused_first` covers the
    count limit (a recently drawn mesh in the highest place survives; the
    oldest highest place goes) and the byte limit (two 32+ MiB meshes).
  - `draw_levels_kept_unused_go_with_their_mesh` (`tests/test_draw_levels.c`)
    checks that a level stays with its kept base and goes when the base is
    superseded.
  - `idle_meshes_kept` (`platform/tests/draw.c`) runs on Vulkan before and
    after device loss, OpenGL, WebGL and WebGPU. A skipped mesh keeps its
    memory and isn't sent again (the private counter `tide_mesh_uploads`,
    `texture_residency.h`). With `tide_mesh_idle_frames` lowered it expires,
    then comes back from the list's copy with one upload.
- **Measured** (release, no sanitizers, RTX 4080 SUPER, driver 617.42,
  `tide_platform_draw --bench --only <n>`: medians of 100 frames, before/after
  run alternately five times each, HEAD built in a separate worktree with the
  same bench code). Workload 14 is new: 8 of 32 Tide-list terrain chunks of
  16,384 corners, the view panning a chunk a frame, so one chunk leaves and one
  comes back each frame.
  - Workload 14, Vulkan: record 0.083–0.154 → 0.005–0.006 ms, render +
    readback 0.173–0.189 → 0.085–0.100, total 0.266–0.385 → 0.089–0.105.
  - Workload 14, OpenGL: record 0.091–0.164 → 0.005–0.008, total 0.413–0.531 →
    0.301–0.499 (medians 0.504 → 0.404). OpenGL's render part is about even,
    because its driver does the buffer work later on its own thread.
  - Workload 12 (one mesh rebuilt every frame, the spare-reuse control):
    Vulkan totals 0.316–0.349 → 0.313–0.428, medians 0.318 → 0.324. OpenGL
    0.380–0.633 → 0.389–0.608, medians 0.484 → 0.516, with run-to-run spread
    far larger than the difference. Its code path is the old one (above).
- **Validation** (Clang 23.1.2, Node 24.22.1, `TIDE_HIDDEN`): the sanitize (ASan)
  build is warning-free. 430 affected native tests pass: every `gpu` test (59,
  Vulkan with its device-loss pass, and the OpenGL twins), `tide_tests`, and the
  draw, mesh, LOD, skinned, Render3D, prewarm, Sumo, Towers and Earth targets.
  276 web-debug tests pass, including draw/targets/shaders/skinned on WebGL and
  WebGPU, plus `tide_tests` under WASI. The web build's only failure is the
  external Foldline prototype's layout check: its debug wasm
  (`tide_query_Render_10`, "local count too large") fails to compile in Node.
  That's generated code this change doesn't touch, and engine tests don't
  depend on it. Linux, macOS, MinGW and Android are left to CI. After merging
  `origin/dev`'s levels of detail for meshes a game makes (whose
  `tide_draw_lod_collect` runs after the retention pass and gives levels to kept
  places too), the same sets pass again: 459 native and 307 web.
- **Not done:** no game or prototype forest was measured, only this
  workload. Another GPU/driver, Android, macOS and the web's own timings are
  unmeasured. The budget's numbers are picks (Claude's, alongside the
  textures'), with no language surface. A game that wants a mesh gone sooner
  has no way to say so beyond `tide_draw_forget`.

## Shared desktop OpenGL vertex bindings (2026-10-31)

The desktop OpenGL skinning gap was vertex input setup. Legacy
`glVertexAttribPointer` / `glVertexAttribIPointer` give each attribute its own
buffer binding, even when those attributes share an interleaved stream. The
10/11 live attributes in `bench_skinned`'s GPU/baked modes cost about 7.1 ms on
the RTX 4080 SUPER (driver 617.42), against Vulkan's 4.4-4.7 ms. Configuring
attributes against shared input slots gives OpenGL 4.3-4.6 ms with unchanged
shader sources, buffers, draws and pixels. See benchmarks.md, Baked poses /
OpenGL investigation, for the alternating measurements.

**The decisive control:** a cheap ALU-only shader retaining all the joint,
weight and instance attributes still took 7.088/7.035 ms, without any texture
reads. Earlier no-skinning and one-fetch controls allowed the driver to remove
attributes and were misleading. The initial inference that this was texture-
heavy vertex-shader execution was wrong. Changing only how the existing
attributes share bindings closes the gap. The driver's internal reason for
the legacy binding cost is not established; no universal threshold or claim
about other vendors follows from this one device.

Implementation and contracts:

- `desktop/gl.c` checks OpenGL 4.3 or `GL_ARB_vertex_attrib_binding` and all
  five optional entry points. A function address alone is not support.
- Each GL pipeline chooses its path once. The shared path assigns attribute
  formats and relative offsets with VertexAttribFormat/IFormat, maps them to
  the existing descriptor slots with VertexAttribBinding, and sets per-slot
  VertexBindingDivisor. Drawing only binds each slot's buffer/base offset/
  stride with BindVertexBuffer. Integer, normalized byte and float formats
  keep their existing interpretation.
- Legacy attribute-pointer/divisor calls never modify a shared-path VAO:
  those calls reset attribute-to-binding associations. Pipeline setup and
  later buffer changes consistently use the selected path.
- Older desktop contexts, WebGL and Android retain the existing path. A
  pipeline also falls back for relative offsets above 2047, strides above
  2048, or a used zero-stride slot. These conservative bounds fit the new
  API's guaranteed limits; zero stride means tightly packed only in the old
  API. This preserves prior layout behavior, subject to the device's existing
  limits (newer GL also limits legacy strides). No version requirement increased.
- The private `tide_gpu_gl_no_vertex_binding` test hook forces legacy setup
  before pipelines are created. It is not a game API or environment setting.
  No shader generator, simulation/runtime contract, draw order, upload bytes,
  shader arithmetic, snapshot/hash pin or public API changed.

Measurement controls and rejected probes (temporary instrumentation removed):

- Both timers surround the same shared draw region, after CPU preparation
  and before readback. Disabling GL queries leaves the slow completion time.
  Every ordinary measured workload collects all 100 samples. These are
  headless frames with full synchronous readback, not presented game FPS.
- Both APIs execute about 65,144,000 vertex invocations for 181,440,000
  submitted indices. The asset's 3,072 unique vertices are not executed just
  once each. Extra invocations do not explain the gap; equal counts alone
  cannot establish cache locality or scheduling equivalence.
- A matched 10,000-character baked frame is byte-identical across GL/Vulkan.
  A longer 500-frame pair reproduces the gap at comparable warm P0 clocks
  (about 2.79-2.82 GHz graphics, 11,501 MHz memory). Desktop load remains
  uncontrolled; slower saved runs are kept separately, not silently dropped.
- Static buffer hints, uniform orphaning, immutable data textures, MAX_LEVEL=0,
  float versus integer textures, buffer textures, instance draw splitting,
  first provoking vertex, and a branchless/unconditional offscreen Y flip
  did not close it. Resident mesh buffers already used STATIC_DRAW.
- Early-return GLSL list readers, diagnostic unchecked readers, nearest
  textureLod, Vulkan SPIR-V translated back to GLSL, and direct OpenGL SPIR-V
  ingestion did not close it. Unchecked readers were discarded; all original
  bounds checks remain. Removing only position or normal skinning did not
  help either: those controls still kept the additional attributes live.

Validation: all 14 focused Windows ASan CTests pass: draw, targets, shaders,
skinned, baked and GPU timing on Vulkan and OpenGL, Vulkan descriptor sets,
and the new forced-legacy draw run. The draw fixture checks a 2048-byte
absolute attribute offset with an 8-byte stride, between ordinary shared
bindings and again on reused VAOs. This exercises per-pipeline fallback
without exceeding the device's stride limit. Existing draw checks cover
normalized bytes, float and integer fields, per-instance divisors, nonzero
buffer offsets, indexed/nonindexed draws and pipeline recreation. Matched
before/after captures of static, gpu_clip and baked_clip are byte-identical.
All 13 focused web-debug draw/targets/shaders/skinned/baked CTests also pass,
including RGBA fallback, WebGL, ANGLE D3D11 and WebGPU, using Node 24.22.1
and the muted browser runners. These checks include the incoming
`Pose.Models()` compiler/runtime dependency after merging `origin/dev`.

Other GPU vendors/drivers, real older desktop contexts, Android and macOS
hardware remain unmeasured. The forced-legacy test exercises the retained
fallback on this machine; it does not substitute for those hardware checks.

## Twelve shader textures and recorded ABI 6 (2026-10-25)

The approved material/skinning boundary completion raises the internal texture
capacity to twelve. Backend descriptors and bound arrays follow that capacity;
draw uploads, bindings and descriptor writes keep their actual-count loops.
Vulkan's shared layout is twenty-five descriptors including uniforms, within
the thirty-two push-descriptor baseline. OpenGL's scratch upload unit is twelve.
WebGPU/HLSL transport changes are recorded in web.md and shaders.md.

Recorded pipelines now contain texture_count read entries rather than eight
fixed entries. The embed ABI is 6, with the Unity consumer changed together;
the separate legacy draw packet version is unchanged. The packet regression
checks each count from zero through twelve. Texture-capacity pixel fixtures
compare slots 8..11 against independent low-slot draws for every filter/wrap
combination and check data/depth/compare/cube reads, including device recreation.
Native Vulkan and OpenGL pixel fixtures and the actual-count packet regression
passed. WebGL2, Direct3D11 ANGLE and WebGPU comparisons passed, including WebGPU
device loss and recreation. Vulkan validation layers were unavailable on this
machine. No performance measurement is claimed.

## Independent rectangle corners (2026-10-25)

SHAPE_CORNERS uses the existing 32-byte shape_extra: radius/outline/soft/sweep
carry TL/TR/BR/BL radii, with rotation in turn. The vertex shader passes those
four lengths in look and reserves only normal AA padding. The fragment selects
the quadrant's radius, evaluates the same rounded-box distance and uses ordinary
target-pixel coverage/premultiplied alpha. There is no outline or fade on this
form. It shares the flexible pipeline with other shapes; equal radii record the
ordinary box form and keep the compact solid-radius path. No GPU attribute,
buffer stride, target, depth or stencil contract changes. Native/web pixel and
batch-count regressions are in platform/tests/draw.c; validation: gui.md.

## Pipelines made before they're drawn (2026-10-22)

The owner asked for shaders to be made while the game loads rather than at
their first draw ("maybe the engine should compile before the game starts or
something?"): BANKED's first felled pine hitched on Vulkan as four new frags
drew for the first time, and the game drew each once unseen to hide it. The
design below is Claude's, Provisional.

- **Where the cost is** (measured on this machine, RTX 4080 SUPER, driver
  617.42, BANKED's 25 shaders, NVIDIA's own disk cache pointed at an empty
  folder with `__GL_SHADER_DISK_CACHE_PATH`, temporary instrumentation):
  - Vulkan: `vkCreateShaderModule` is nothing; `vkCreateGraphicsPipelines`
    compiles, 20 to 230 ms a pipeline cold (BANKED's first frames: 17
    pipelines, 835 to 900 ms; its later first uses: HewnModel 75 ms,
    NotchFace 52, Snowlump 28, HewnShadow 24, Arm 87, AxeLit 50, Ice 119),
    0.2 ms with the driver's and `pipelines.vulkan`'s caches warm. The driver
    compiles again for much of the key, not only the shader: a pipeline
    made for the window's kind of target first left another for a float
    target at about 80% of its cold cost, one for a depth-only target at
    two thirds, `TIDE_DEPTH_READ` for `READ_WRITE` at all of it, `ADD` for
    `ALPHA` at 6%, and other samples or cull at nothing. (A first experiment
    with Tag, its driver cache warm, saw 6 ms become 0.03 ms for a different
    target: the driver's disk cache answering, not reuse.)
  - Desktop GL (NVIDIA): the link compiles, 250 to 430 ms for BANKED's big
    frags cold, 4 ms from the driver's disk cache. Blend, depth, cull and
    the target are set at the draw, so one program serves every key.
  - WebGPU: `createRenderPipelineAsync` per key, in the background; Dawn
    compiles per pipeline. WebGL: compile and link per program, in parallel
    where `KHR_parallel_shader_compile` is. Not measured beyond the tests.
- **What the compiler knows:** every shader combination a draw can use
  (`program.shaders`): its sources, vertex layout, `dims`, outputs and
  whether it's a blit. Not the target it draws into (a `RenderTarget` is a
  value) nor the settings it draws with (settings are a stack at run time),
  which is most of what the drivers compile for.
- **The table:** tidec writes `const tide_shader *const tide_game_shaders[]`
  in the game's C (not the header, which is the game's hash), every shader
  and `NULL` after them, in every game (`gen_shader_table`), and
  `tide_shader.blit` for blits' shaders. `tide/run.h` declares it and fills
  the new last member of `tide_host_game`, `shaders`. Generated code calls no
  new runtime function. Hosts and libraries rebuild together for the
  `tide_host_game` and `tide_shader` layout changes.
- **`tide_platform_prewarm(shaders, count)`** (`tide/platform.h`): makes
  the renderer's own pipelines for the window's kind of target (default
  blend, depth and cull), and for each of the game's shaders:
  - every key it drew with before, by the shader's name (`used_key`: an
    FNV-1a of `tide_shader.name`, its kind, and `into`, blend, depth, cull
    and stencil state), so a shader whose code changed (another id, the same
    name) is made too;
  - for a shader that never drew, once a shader of its kind (2D or 3D, a
    blit, its outputs) drew: for each kind of target its kind drew into, the
    key most of them drew with there (`kind_keys`, at most 8). So BANKED's
    HewnShadow gets the depth map's key as well as the scene's, and a model
    the scene's (and the depth map's, unused).
  It's asked again cheaply: nothing happens till the table, the device
  (`tide_gpu.epoch`) or the keys drawn change (`used_changes`). Prewarmed
  pipelines of the current table (`shader_pipeline.warmed`) survive the
  first frame of another build (`forget_pipelines`); another table lets
  them go there. `tide_platform_prewarm_waiting()` counts those asked for
  that the backend is still making in the background.
- **The keys file:** `pipelines.keys` in the game's temporary cache folder,
  beside `pipelines.vulkan`: `TIDEKEY1`, a count, 4 zero bytes, an FNV-1a of
  the keys, then 24 bytes a key, 4,096 keys at most. Read as the folder is
  known (`follow_cache_folder`), written as the window closes (`finish`) and
  as the folder changes, and only when a draw drew with a key that wasn't
  there. A file cut short, changed, or with a key out of range is left out
  whole; keys of shaders not in the last table go when it's written. Native
  only: a page has no such folder, so it keeps its own run's keys.
- **The standard host** (`tide_run_prewarm` in `tide/host.h`) gives it
  `tide_game_shaders` in `tide_run_begin` (after `tide_run_bind_io` named the
  game, so both files are read first) and every frame before
  `tide_platform_draw`, which covers a reloaded build and a new device. Not
  in a headless run (nobody waits for its frames, and they stay as they
  were) or embedded. Background pipelines stay as they were: on for games,
  so a page's prewarming doesn't hold its first frame.
- **OpenGL shares programs** (`gl_program` in `gpu_gl.c`): pipelines with the
  same sources and texture count, by FNV-1a, are one program, counted, so a
  shader's other keys cost a vertex array. A program begun in the background
  is finished at once for a pipeline asked for with background off.
- **Measured after** (BANKED, a debug build, whose cost here is the
  driver's, 120 headless frames with prewarming forced on by temporary
  instrumentation, NVIDIA's cache emptied for the cold runs):
  - First run, no keys file: the first frames make what they draw as before
    (17 pipelines, 849 ms on Vulkan); frame 5, the first after its 3D draws,
    makes the 7 shaders that hadn't drawn, for the scene's target and the
    depth maps (14 pipelines, 690 ms Vulkan, 585 ms GL, where GL's second
    key cost 0.03 ms each).
  - Second run, its keys file, the driver cold: 31 pipelines as the game
    starts, 1.5 to 1.6 s on Vulkan, 1.9 to 2.0 s on GL; none in any frame.
  - Second run, the driver warm: 31 pipelines as it starts, 12 ms on Vulkan
    (25 ms GL); none in any frame. The felling's shaders' first draws make
    nothing, where before each made its pipeline then.
- **Tests:**
  - `tide_platform_draw`'s prewarm phase (after the background one), native
    Vulkan, OpenGL and descriptor sets, WebGL and WebGPU: a frame draws one
    shader of each kind after the list forgot; prewarming a table of others
    makes a pipeline for each (on WebGPU in the background), the same table
    again makes nothing; once made, the scene, a 3D shader of the drawn one's
    name with another id, and a blit that never drew each draw in their first
    frame (WebGPU's first frame through a pipeline not made leaves it out)
    and make no pipeline; another build's first frame keeps them, and lets
    them go once another table is given. Prewarming each key with its cull
    changed, on purpose, fails it.
  - `tide_platform_shaders`, every backend and ANGLE's D3D11: after all its
    checks, the list forgets, a build's first frame lets the pipelines it
    didn't draw with go, and prewarming the 172 shaders remakes the 131 gone
    the ways they drew; every check then draws again, the same pixels, making
    no pipeline.
  - `tide_platform_prewarm` (`platform/tests/prewarm.tide`, `prewarm.c`,
    run by `prewarm.cmake` in a folder of its own, Vulkan and OpenGL; once
    in a page, WebGL and WebGPU): the standard host over a game whose late
    mesh and blit draw once the test says. With no file nothing of the
    game's is made as it starts, the late mesh is once the early one drew,
    and the blit is made at its draw; with the file left, all three are made
    as it starts and the late frame makes none and draws them all; a file
    with a byte changed or cut short is left out and written over whole.
- **Validation:** the native debug suite passed 2,540 of 2,540 and the web
  debug suite 2,441 of 2,441, warning-free.
- **Not done, and open for the owner:**
  - A brand-new shader whose kind hasn't drawn yet is made at its first
    draw, and so is one drawn into a kind of target its kind never drew into,
    or with a setting other than its kind's commonest there (a decal's
    `Depth.Read`): the next run has its key.
  - Guessing costs: in a first run every not-yet-drawn shader is compiled
    for each kind of target its kind drew into, some of which it never draws
    into (BANKED: 7 of 14 unused); with the driver's cache warm that's a
    fraction of a millisecond each.
  - Vulkan makes them one at a time on the render thread: a cold start of
    BANKED's 31 takes 1.5 s. Making them on the job pool's threads
    (`vkCreateGraphicsPipelines` may be called from several), or
    `VK_EXT_graphics_pipeline_library` to compile shaders apart from state,
    would cut that; not built.
  - A page keeps no keys between visits (`localStorage` through `tide.js`
    would; a new import, not built). Its prewarming is the guess alone.
  - Nothing lets a game wait for prewarming (a loading screen reading
    `tide_platform_prewarm_waiting`), nor turn it off: language surface, the
    owner's to decide. Headless runs never prewarm.
  - Not run: the Vulkan validation layer (no SDK on this machine), drivers
    other than NVIDIA's, Android, macOS.

## Recording, for Unity (2026-10-22)

`platform/src/gpu_record.c` is a `tide_gpu` that draws nothing. It writes each call as
`tide_embed_render`'s commands (`tide/embed.h`, `tide_embed_render_op`), for Unity's
`TideRender.cs` to replay. The renderer is unchanged.
`tide_platform_embed_render` (`platform.c`, `embedded.h`) points `gpu` at it and runs
`draw_list` on the frame's list into target 0. Target 0 is the other program's, so
the window's first pass asks to clear it, and the replay ignores that.
`tide_platform_embed_render_reset` bumps its epoch, so `gpu_ready` makes everything again.
`tide_platform_embed_render_view` (the other program's scene views) draws the same list again, `over`, with
`view_only_3d` set: only the window's 3D steps (`window_3d_step`: STEP_MESH_3D, and STEP_SHADED through a 3D
shader) draw, each through `view_world_to_clip` (`camera_3d_to_clip`), and target switches leave the targets
alone. Its commands follow the frame's, the same objects.
Objects are numbers it gives out from 1. A target's color and depth textures get
theirs when it is made, and a multisampled one's depth gets its number at
`resolve_depth`. It keeps texture shapes for part writes, sending only the part's
rows. A pipeline carries a key: FNV-1a 64 of its SPIR-V words, or of its WGSL where
it has none. `tide unity` names a shader file by the same key.
`texture_blocks` returns 0, so the renderer sends RGBA. `target_read` gives
nothing, because the pixels are drawn later by the other program. `float_targets` is
true; `target_samples` rounds down to 1, 2, 4 or 8. Test: `embed_render`
(`platform/tests/embed.c`): the commands parse whole, a pass on target 0 asks to
clear, every pipeline key is a built-in SPIR-V's hash, the next frame makes no
pipelines, and after a reset they are made again from low numbers.


## Recorded quads and faster conversion (2026-10-20, third round)

The owner asked to close drawing's gap to hand-written C further, recording
being where most of it was (notes/drawing.md, Recording inline and quads as
quads, for the list's side; notes/benchmarks.md, Drawing, third round, for the
numbers). Every benchmark frame, all 13 workloads, is the same bytes before
and after on Vulkan and OpenGL (`--dump-frames`, compared with round two's
binaries).

- **TIDE_DRAW_QUADS** (`add_quads`): a run of the list's quads
  (`tide_draw_quad`) goes into a mesh step as the quads `add_mesh` makes of
  the same two triangles: `make_quad_across`'s box of p and r through the
  camera, a lane each as there, their uvs' bits, `to_rgba` of their color, and
  the pattern as recorded. A step a mesh command already turned to triangles
  takes them as `corner_of`'s corners of `tide_draw_quad_corner`'s vertices.
  So a run that mixes recorded quads and meshes is one step, as before:
  quads then a triangle that isn't one turn back (`triangles_instead`), and
  quads after triangles go on as triangles.
- **TIDE_DRAW_PLAIN** (`add_plain_run`): a run of the list's plain-shape
  records goes into the shape step in pieces of 4,096, each one's arithmetic
  what a plain command's was, to the bit, its color copied as recorded (the
  same bytes `to_rgba` made of the float). A circle on a scaled destination
  is still a smooth shape (`add_circle`), between the pieces. Unknown kinds
  draw nothing, and a run's first and count (`tide_draw_plain_run`, the open
  run's being the list's records from its first on) are kept within the
  list's records.
- **Solid smooth shapes** (`add_smooth_run`): a shape is tested for being
  solid (`solid_shape`'s test: a box or an ellipse, unturned, no outline, no
  fade, its bottom color's bytes its top's) on the numbers its extra would
  have before the extra is made, converting the bottom color only where its
  float bits differ from the top's; the extra and its outline's color are
  made only for one that isn't. Converting 65,536 round rects took about a
  third less.
- **`to_rgba`** converts a color's four channels in one vector (maxnum and
  minnum, a multiply, an add, a truncating conversion), where four calls of
  `color_channel` did each: the same byte for each of the 2^32 floats, checked
  exhaustively with the engine's flags (`-O2 -ffp-contract=off`). It's
  `tide_draw_color_bytes` in `tide/draw.h` now, which the list records plain
  shapes' colors with too.
- Measured and not built: converting straight into GPU-visible memory. Upload
  is write-combined memory at about 19 GB/s either way (65,536 rects' 1.5 MB
  took 78 µs), so fusing conversion with it saves only their overlap, about
  70 µs of a rects frame; skipping the GPU-side copy measured about 10 µs.
  It would also need Vulkan memory kept past a mid-list flush (`flush_if_full`
  at a pass's start) until the frame's done. A per-phase timing (temporary)
  found no fixed per-frame cost in the upload phase: its first frame makes the
  pipelines.

## Quads, solid shapes and a fenced flush (2026-10-20, second round)

Round 2 of the performance pass against the handwritten drawing baseline
(`notes/benchmarks.md`, Drawing, second round). Internal, Claude's; no API
changed. Every benchmark frame is the same bytes before and after on Vulkan
and OpenGL, all 13 workloads (`tide_platform_draw --bench --dump-frames`).

- **Quads** (`quad`, `make_quad`, `make_quad_across`, `triangles_instead`,
  `add_glyph` in `platform/src/platform.c`). Two of a 2D mesh's or of text's
  triangles go to the GPU as one 40-byte instance where their six corners'
  places take two numbers at most on each axis, as their uvs do, and they're of
  one color: a sprite, a tile, an upright glyph, in any order of corners and
  triangles. The instance has the two numbers of each axis (`box`, `span`), the
  color, and 4 bits a corner (`pattern`) saying which number each of the six
  takes. `PIPELINE_QUADS` and `PIPELINE_TEXT_QUADS` read it per instance
  (`quad_glsl_vertex`, `QUAD_WGSL_VERTEX`, by `gl_VertexID` / `vertex_index`)
  into the meshes' and text's own fragment shaders, with the arithmetic of
  their vertex shader, so the GPU draws the same triangles from the same
  numbers: 40 bytes where six vertices were 120. A run (`draw_step.quads`)
  starts as quads; the first two triangles that aren't one (a turned glyph,
  corners of two colors, a lone triangle) turn its quads back into the
  vertices they stood for, to the bit, and it goes on as triangles. One step
  is still one draw call, and six vertices a quad are counted
  (`tide_platform_draw_vertices`). `make_quad_across` is the common case,
  triangles p q r and p r s with p and r opposite: it compares the list's
  vertices as they are, by their bits, four lanes at once (clang's
  `ext_vector_type`, SSE2 here, NEON on arm64, scalars on wasm), since a
  corner's place on the screen is worked out from its own on that axis alone,
  and works out p's and r's only. `make_quad` takes any other order. The
  first version, every corner converted into a `mesh_vertex` and compared a
  field at a time, built quads slower than the triangles it replaced (474 ->
  650-760 µs a frame of 65,536): the compiler spilled and branched, and wide
  loads over the structs' narrower stores waited.
- **Solid shapes** (`solid_shape`, `settle_radii`, `smooth_instead`). A run of
  smooth shapes whose smooth ones are all solid (a box or an ellipse,
  unturned, one color top to bottom, no outline, no fade) has a radius for
  each of its shapes, 4 bytes, where it would have a 32-byte extra:
  `PIPELINE_SOLID_SHAPES`, whose vertex shader (`solid_shape_glsl_vertex`,
  `solid_shape_wgsl_vertex`) sets the extra's other numbers as constants
  before the same lines as the smooth shapes' (`SMOOTH_SHAPE_GLSL_MAIN`,
  `SMOOTH_SHAPE_WGSL_MAIN`), into the same fragment shader. That's round
  rects, plain circles on a scaled display and the plain shapes among them.
  The first smooth shape that isn't solid gives the run's shapes the extras
  they stood for and it goes on as before. Those extras differ from what
  `add_smooth_run` would have written only where no shader reads them (an
  arc's sweep, an outline's color with no outline) and in the sign of a zero
  (turn.y), which places nothing differently.
- **Vulkan's flush** submits with a fence of its own (`vk.flushed`) and waits
  for it, which waits for everything submitted before it, where it waited for
  the queue to go idle, and a command pool none of whose buffers was begun
  since its last reset isn't reset again (`flush`, `tide_gpu_vk_present`).
- Tests: `runs_drawn_alike` in `platform/tests/draw.c` draws quads (four
  orders of corners, flipped uvs, places that aren't whole), text in both
  weights, and solid shapes with rects and circles among them, at scales 1 and
  2, three ways: alone; after something in the same run that keeps it from
  being quads or solid (a lone triangle, turned text, part of a ring); and
  with that last, which turns it back. All three are the same pixels and as
  many draw calls, on Vulkan (before and after its device is lost), OpenGL,
  WebGL and WebGPU. A quad with its uvs swapped, or solid shapes given no
  radius, fail it (checked by breaking each on purpose). The renderer's SPIR-V
  is ten shaders now (`tide_spirv_shaders_valid`).
- Tried and dropped: one `vkCmdPipelineBarrier` for each pass's barriers and
  layout turns, with their stage masks joined (no change beyond noise to an
  empty frame or rects, and it would have the next pass's vertices wait for
  the last one's fragments).
- Not built:
  - Converting shapes straight into GPU-visible memory. Uploads are bound by
    the bus: Vulkan's ring is device-local host-visible memory, which the
    CPU writes at about 20 GB/s here (1.5 MB of rects takes 0.08 ms, 7.8 MB
    of triangles took 0.38). Writing instances there as they're converted
    would save the CPU's copy of what it just converted (about 0.07 ms for
    65,536 rects, 0.19 for round rects before solid shapes). It needs a
    `gpu.h` call that hands out room which lasts until the list's last pass
    is recorded: `flush_if_full` can flush mid-list, which frees the ring and
    may destroy a ring that grew. WebGL and WebGPU would keep `buffer_write`.
  - OpenGL's empty frame (0.17-0.29 ms against C's 0.01-0.02): the hidden
    window's swap, which the next readback waits for. Skipping the swap made
    uploads slower (round 1), as NVIDIA recycles `glBufferSubData`'s memory at
    swaps; uploads through an unsynchronized ring with fences would have to
    come first.

## Readback, upload memory and runs of shapes (2026-10-20)

From the performance pass against the handwritten drawing baseline
(`bench/draw/versus_draw`, `notes/benchmarks.md`, Drawing against
handwritten OpenGL and Vulkan). Internal changes, Claude's; pixels are the
same bytes before and after on both backends for all 13 benchmark workloads.

- `tide_platform_read_pixels` reads back only the box around the points it's
  asked for, through `gpu.h`'s optional `target_read_part` (OpenGL, so WebGL
  too, and Vulkan; WebGPU has none and reads whole targets as before). Vulkan's
  blue-first-to-red-first swizzle of what's read back is a `restrict` loop that
  vectorizes; done a byte at a time it was an eighth of the benchmark's own CPU
  time while it read whole frames. Vulkan's empty benchmark frame: 0.137 -> 0.065 ms.
- `draw_list` turns a run of plain shapes (`add_plain_run`: rects, their
  outlines, lines, and circles where the destination isn't scaled) and a run
  of smooth shapes (`add_smooth_run`) into steps in one loop each, with the
  camera's numbers read once and the arithmetic of the cases they replace,
  bit for bit. `rect_corner` and `add_smooth_shape` went into them.
- `color_channel` keeps a channel from 0 to 1 with `fminf`/`fmaxf` (NaN as 0)
  instead of branching, so a color converts four channels at once; it gives
  the same byte for all 2^32 floats (checked exhaustively, same flags).
- `add_mesh` reads its camera's numbers once. A small cache of converted
  corners (a quad's two triangles share two) was slower, and was dropped.
- Vulkan's per-frame upload ring (the source of buffer and texture copies,
  and the uniforms) is allocated in device-local host-visible memory where the
  device has such a type (all of a desktop GPU's memory with resizable BAR,
  else a 256 MB window; any integrated GPU's), for rings up to 32 MB
  (`UPLOAD_LOCAL_BYTES`), else, or when that allocation fails, in host memory
  as before. Copies out of it then don't cross the bus as the GPU runs: with
  per-phase timers (not committed), submission to pixel read back went 0.21 ->
  0.14-0.18 ms for 65,536 rects and 0.44 -> 0.27 ms for 65,536 cubes'
  transforms, while the CPU's copy into the ring rose slightly (0.075 -> 0.086 ms).
- Tried and not kept: OpenGL swapping the window only after a frame drew in
  it (as Vulkan presents only an image it drew in), flushing otherwise. A
  hidden window's swap costs the next readback about 0.09 ms (measured in the
  baseline too: 0.025 -> 0.111 ms with a swap between frames), and the empty
  frame went 0.131 -> 0.034 ms, but frames that upload a lot got 1.5 to 5 times
  slower (round rects 0.84 -> 3.95 ms): NVIDIA's driver evidently recycles
  `glBufferSubData`'s memory at swaps. Every test frame through
  `tide_platform_read_pixels` still swaps the hidden window.

## Pipelines made in the background (2026-10-20)

Cause of the first-use hitch the owner saw in GRAVEWAKE on the web: each GPU
pipeline (shader + blend/depth/cull/target key) is made on its first draw, and
both web backends made it synchronously. WebGPU's `createRenderPipeline`
compiled before the frame could submit. WebGL queried `COMPILE_STATUS` and
`LINK_STATUS` straight after compiling and linking, which waits for the driver.
A new effect's first frame therefore stalled. Separately, idle pack images expired after 60 lists,
so a texture shown now and then (a boss's animation) was uploaded again each
time it returned (see `notes/textures.md`).

- `tide_gpu` gained optional `background(bool)` and `pipeline_ready(id)`. With
  background on, `pipeline` starts one and returns at once; `pipeline_ready`
  polls. NULL on backends that only make them synchronously (Vulkan, desktop GL).
  The renderer's `pipeline_made` leaves out steps (shapes, clear, mesh, mesh 3D,
  shaded, blit, text) whose pipeline isn't made yet. The step is simply not
  drawn that frame; nothing waits.
- `tide_platform_background_pipelines(bool)` (`tide/platform.h`) turns it on;
  `gpu_ready` passes it to the backend each frame. Off by default. The standard
  host (`tide_run_bind_io` in `host.h`) turns it on except in headless runs,
  whose frames are read back and must draw the same every time. Custom hosts and
  tests keep synchronous pipelines unless they ask.
- WebGPU (`tide.js`): `createRenderPipelineAsync`, the entry's `pipeline` set
  when it resolves on the same device. A rejection fails the page under
  `checkGL`, otherwise prints. WebGL: `KHR_parallel_shader_compile` is enabled
  where present; `gpu_gl.c` splits compile/link from their status checks
  (`gl_pipeline_finish`), and on wasm `gl_pipeline_ready` polls
  `GL_COMPLETION_STATUS_KHR` before finishing. Without the extension the
  completion query answers true, so it finishes on the first poll as before.
  Desktop GL keeps immediate finishing; Vulkan has its disk pipeline cache.
- Test: `platform/tests/draw.c` adds a background phase after all other checks.
  It forgets the list, draws a frame without the shaders (their pipelines go),
  turns background on and draws `shaded_scene()` each frame until (32,32) is
  orange, then runs `shaded_checks()` on it (600-frame limit). On WebGPU frame 1
  must leave the draws out. Measured on this machine: native Vulkan/GL and
  WebGL draw it in frame 1 (WebGL's compile finished before the poll); WebGPU
  leaves frame 1 out and draws it in frame 3. The rgba-fallback page variants
  pass too.
- Pages that step frames and compare screenshots turn it off with
  `Tide.backgroundPipelines = false` (see `notes/web.md`).
- Gap: on the web, a shader's first appearance is one or a few frames late
  rather than hitching; nothing prewarms pipelines before they are drawn.
  Closed in part by Pipelines made before they're drawn (2026-10-22, above):
  what's left is listed there.

## Window scale getter (2026-10-20)

`tide_platform_screen_scale()` exposes the existing `window_scale()` float2,
physical/logical pixel ratios independently on each axis. The standard host
passes it to generated frames as draw-list metadata for `Screen.scale`. It adds
no backend call, allocation, device query or target invalidation rule. The float
bits platform fixture also checks frame access against this getter. See
`notes/drawing.md` and `notes/language.md` for contracts and validation.


## Target dimensions for local metadata (2026-10-18)

tide_platform_target_pixels exposes the existing target_extent calculation to
the frame's Texture.size resolver, using current window logical/physical sizes
and a device-limit cache keyed by GPU epoch. No readback or target allocation
is needed. Custom capture hosts provide their own root-aware callback; the
platform shader fixture does so for scale-1/2 read_pixels_as frames. Rendering
and texture binding paths are unchanged; coverage is in notes/textures.md.

Texture compression/residency (2026-10-11): the private `tide_gpu.texture_blocks` factory uploads complete immutable BC1/BC3 chains or returns zero for RGBA fallback. GPU accounting uses the actual representation, and compressed objects are recreated on content/draw epoch changes. Inactive pack images/cubes now have a 128 MiB / 4096-image / 60-rendered-list cache; other texture lifetimes remain unchanged. The cache owns no CPU pack pointers. Contracts, measurements, coverage and remaining streaming gaps are in `notes/textures.md`.

## Submitted vertex totals (2026-10-15)

The owner's request for a total of vertices being drawn adds
`tide_platform_draw_vertices`, a 64-bit total for the last rendered list,
beside `tide_platform_draw_calls`. The shared renderer counts at submission
on every backend: non-indexed vertices or indices multiplied by instances,
using each selected LOD's index count. Shapes, glyphs, 2D/3D/custom meshes,
blits, explicit clear triangles and all render-target passes count. Repeated
indices, GPU-clipped/face-culled triangles and repeated passes count again; backend
attachment clears, mipmap generation and host overlays do not. This measures
submitted geometry, not unique corners or hardware vertex-shader invocations.
Instances rejected during draw-list frustum culling never reach submission and
do not count; the frustum pixel fixture also checks that six rejected indices
disappear from the submitted total (`notes/drawing.md`, Frustum culling).
An empty list resets the total; an overlay preserves the game's total.

The existing draw fixture checks all submission paths, instance/LOD totals,
overlay exclusion and an empty frame, including after device recreation.
The standard host's display and final validation are in `notes/standard-host.md`.

## Vulkan (2026-10-14)

The owner asked for Vulkan ("lets get Vulkan implemented"), the agreed next backend (step 1 of the agreed order, below). Desktop now draws with it by default. Everything below the request is Claude's pick, Provisional: the backend is behind the language, where the owner's rule is that the fastest wins.

- **`platform/src/gpu_vk.c` is `gpu.h` on Vulkan 1.3**, so `platform.c` drew through it unchanged. A device needs 1.3 (dynamic rendering; `robustImageAccess` is used where it's there), `VK_KHR_swapchain`, `independentBlend`, and a graphics queue that presents to the window. `VK_KHR_push_descriptor` is used where it's there, and descriptor sets of each frame's where it isn't (`notes/android-and-mingw.md`, Vulkan on Android). Android draws with it too (same note). With none of that, or no loader, the window doesn't open, and the machine is told what the game needs (GL backends removed, above; until then it drew with OpenGL 3.3, and `TIDE_BACKEND` picked).
- **The model is WebGPU's, which Vulkan underlies:**
  - Every pipeline shares one layout of set 0 (a layout for each pipeline's bindings since 2026-11-05: Sixteen textures a draw, above). Binding 0 is the uniform block; texture i and its sampler are at 1 + 2i and 2 + 2i, as the WGSL binds them. A draw pushes what it reads (`vkCmdPushDescriptorSetKHR`): the uniforms at their place in the frame's ring, 4096 bytes from there (`TIDE_GPU_MAX_UNIFORMS`), and each texture through the sampler for how the pipeline reads it.
  - The viewport is flipped (negative height), so clip space's y is up, rows go from the top in every image, and a front is clockwise (`VK_FRONT_FACE_CLOCKWISE`), as on WebGPU. Depth goes 0 to 1 because the WGSL already converts it.
  - Byte color images, a target's and a drawn-into cube's, have the window's format (`B8G8R8A8_UNORM` where the surface has it, else `R8G8B8A8_UNORM`), so one pipeline draws into both. `target_read` swaps blue and red back. Floats are `R16G16B16A16_SFLOAT`. Depth and stencil are `D24_UNORM_S8_UINT`, or `D32_SFLOAT_S8_UINT` where D24 can't be both attached and sampled.
  - Uploaded textures are `R8G8B8A8_UNORM`, `R8_UNORM`, `R16_SFLOAT` and `R32G32B32A32_SINT`, zeroed until written, as WebGPU's are. BC1/BC3 are used where `textureCompressionBC` exists.
- **Writes happen before the pass, as WebGPU's queue writes do.** Each pass begins a command buffer of its own. Buffer and texture writes, mipmaps, depth resolves and timestamps recorded outside passes go into the command buffer of the pass before them, after a full memory barrier recorded at the first such write, or into a new one if no pass came before. A write during a pass goes into a "before" command buffer, made at the first such write and placed in the queue just ahead of the pass's, so the whole pass sees it, as on WebGPU.
- **Barriers are by the stages that write and read** (2026-10-14, after the owner's "let's close those gaps"; Claude's picks, Provisional). A barrier orders everything before it in submission order, across command buffers. So:
  - Each pass's command buffer starts with two barriers. Transfers come before anything the pass reads or draws in. Attachments drawn in before (the window's depth, multisampled images, which stay attachments) come before this pass's. The pass's vertex work doesn't wait for the last pass's fragments.
  - Transfers recorded after a pass (`vk.drawn`) first wait for the draws before them, which may have read the buffer a transfer overwrites. Buffer copies keep a transfer-to-transfer barrier each.
  - A layout turn waits for the stages that used the image in its old layout, and holds up those that use it in the new one (`layout_use`). An undefined or shown image is at color output, which is where a frame's submission waits for the image it acquired.
  - Until then, every command buffer began with a full `ALL_COMMANDS` barrier, and every turn was one too. GPU time a frame, release at 1920×1080, frames 21–320, three runs each: Tag 0.42 → 0.28 ms, Duel 0.29 → 0.14, Two Forks 0.067 → 0.037, Sumo 0.075 → 0.064. OpenGL's timer on the same frames gives 0.33, 0.137, 0.044 and 0.050.
  - Synchronization validation (below) checks every pass of the GPU tests and of six games (Duel, Asteroids, Marbles, Sumo, Tag, Two Forks) for a missing barrier, and finds none. With the pass barrier removed on purpose, the draw test stops at `SYNC-HAZARD-READ-AFTER-WRITE`. The games' captures are the same bytes as before.
- **Images rest in the layout they're read in:** textures in `SHADER_READ_ONLY_OPTIMAL`, depths in `DEPTH_STENCIL_READ_ONLY_OPTIMAL`, multisampled images in their attachment layouts. Whatever uses an image otherwise turns it there and back, so nothing tracks layouts.
- **Two frames are in flight.** Each frame has its command pool, fence, upload/uniform ring and garbage list. Anything freed is destroyed once the GPU has finished the frame that freed it. `tide_window_present` (desktop/glfw.c) submits the frame and presents. A frame that never presents is flushed (submit, wait idle) on reaching 512 queued command buffers, 1024 recorded ones, or a ring of 256 MB, and also on `target_read`.
- **Window and memory:**
  - The swapchain is FIFO for a shown window and IMMEDIATE/MAILBOX for a hidden one, as OpenGL's swap interval was. It is remade when the window's pixel size changes or Vulkan says it's out of date.
  - Headless runs never acquire an image.
  - Memory comes from 64 MB blocks per type, with buffers and images kept apart, and first-fit with merging. Anything of 16 MB or more gets its own allocation.
- **Rest of `gpu.h`:**
  - Multisampled targets resolve each color image as the pass ends (`VK_RESOLVE_MODE_AVERAGE_BIT`). `resolve_depth` is an empty rendering with a sample-zero depth and stencil resolve.
  - `mipmaps` blits level by level, after first remaking a one-level texture with room for its levels, as WebGPU's backend does. A format that can't blit linearly gets none.
  - Timing writes timestamps around the game's drawing. Only pairs already submitted are read.
  - A lost device makes `epoch()` remake the device and return a new epoch.
- **Pipelines are kept across runs** (`tide_gpu.pipeline_cache`, 2026-10-14, Claude's picks, Provisional):
  - In `pipelines.vulkan`, in the game's temporary cache folder. That's `%TEMP%/<appId>`, `$TMPDIR` or `/tmp` with `<appId>`, or the app's cache folder on Android, the same as `Application.temporaryCachePath`. The host names the game whether it uses files or not (`tide_platform_name_game`, which `tide_run_bind_io` calls), and the folder is made only when the file is first written.
  - The renderer tells the backend as the folder changes (`follow_cache_folder`). It's read then, and on every new device, into one `VkPipelineCache` that every pipeline is made with. It's written as the window closes, unless it's what was read.
  - The file is the driver's data after a header of Tide's: `TIDEPSO1`, the length and an FNV-1a hash. A file cut short or changed on disk never reaches the driver. Nor does one whose driver header (vendor, device, `pipelineCacheUUID`) is another device's: this device's is written over it. Writes go to a file of the process's own beside it, then replace it (`tide_file_write_whole` in `platform/src/io.c`), so two players of one game running together leave a whole file.
  - Measured with release builds on this machine: making the first frames' pipelines took Tag (17 pipelines) 24–27 ms without the file and 3 ms with it. Duel (8) took 11–13 ms and 1.3 ms, Two Forks (11) 13 ms and 1.5 ms. That was the same with NVIDIA's own disk cache off (`__GL_SHADER_DISK_CACHE=0`) or on: the driver's cache didn't save these.
  - `tide_platform_pipeline_cache` (`platform/tests/pipeline_cache.c`, run in order by `pipeline_cache.cmake` with the temporary folder pointed into the build) checks five runs. A first run reads nothing. Each run after reads what the last wrote (77,516 bytes). A file with a byte changed, a whole one for another vendor, and one cut in half are each left out, then written over whole.
- **Shaders are SPIR-V that tidec translated from the WGSL** (`compiler/src/spirv.c`: `notes/shaders.md`, SPIR-V).
  - Games' shaders are in the generated C (`tide_shader.spirv`, under `TIDE_SPIRV`, which `tide/draw.h` defines everywhere but the web).
  - The renderer's own are made at build time by `tide_spirv_shaders` (`platform/tools/spirv_shaders.c`) into `shaders_spirv.c` (`src/shaders_spirv.h`). That tool is built in `compiler/` so cross builds get a native one, as they get tidec (`cmake/HostTidec.cmake`).
  - A `tide_gpu_pipeline_desc` carries `spirv` and `spirv_words`.
  - A C test's hand-written shader has WGSL alone. Such a test hands the translator over (`tide_gpu_spirv_of`, `gpu.h`), and the pipeline translates it at creation. Games never set it, so no game carries the translator (about 106 KB of code).
- **Validation:** `TIDE_VULKAN_VALIDATION` (anything but empty or 0) loads `VK_LAYER_KHRONOS_validation` and stops the program at the first validation error or warning, as `Tide.checkGL` fails a page. Synchronization validation is on with it where the layer has `VK_EXT_validation_features`: a write and a read with no barrier between them stop the program too. One message ID is let through: `Undefined-Value-ShaderOutputNotConsumed*`, a color written where the target has no image. `gpu.h` defines that write to go nowhere (depth-only targets, a clear of four colors into fewer), and the layer only says the write is unused. The variable set with no layer to be had exits with 1.
- **The Vulkan headers are vendored** (`platform/third_party/vulkan`, `vulkan-sdk-1.4.350.0`, Apache-2.0; their license installs with the package). Everything is found through the loader that GLFW opens, so nothing of Khronos's is linked.
- **Tests:** every native GPU test now draws with Vulkan here (`tide_gpu_test` in `cmake/VulkanTests.cmake`; see `notes/testing.md`). `tide_platform_draw`, `_targets`, `_shaders`, `_skinned` and `_gpu_time` also ran as `*_opengl` twins (`TIDE_BACKEND=opengl`) until 2026-11-05. The draw (1,447 checks), targets and shaders (1,310) tests pass on Vulkan under the validation layer with the same counts as OpenGL. `tide_platform_hidden_runs`' window case read the window through its OpenGL context, so it forced OpenGL; it reads it through `tide_glfw_window` now. `tide_platform_gpu_time` asks `tide_gpu_vk_timestamps()` whether the device can time.
  - Losing the device: once its checks pass on Vulkan, `tide_platform_draw` asks for the device to be lost as the frame is shown (`tide_gpu_vk_lose`, a test hook). The next frame's renderer remakes it, and all 1,447 checks run again on the new one (2,850 `ok`s in all), as `tide_platform_draw_webgpu` does after losing WebGPU's.
  - `tide_platform_swapchain` (`platform/tests/swapchain.c`) resizes a hidden window bigger and then smaller (GLFW resizes a window that isn't the player's to resize, through `tide_glfw_window`). After each, it checks the swapchain was made again at the window's pixel size (`tide_gpu_vk_swapchain`). Then it loses the device while frames are shown, and checks the next frame drew with a new device in a new swapchain, reading a pixel back each frame. Windows keeps a titled window at least 120 pixels wide, so the sizes are read from the window, not assumed.
- **Measured 2026-10-14, Windows, RTX 4080 SUPER (driver 617.42), release `tide_platform_draw --bench`** (20 warmup and 100 measured frames a workload, median ms of render and synchronous readback, Vulkan / OpenGL 3.3): nothing 0.162 / 0.245; 65,536 rects 0.867 / 0.735; circles 0.779 / 0.694; textured quads 2.144 / 2.437; cube instances 0.620 / 0.714; round rects 1.520 / 1.587; a 1024² grid texture with 64 cells painted 0.162 / 0.096; 65,536 lights read whole 0.218 / 0.121; a 16,384-corner mesh moved each frame 0.280 / 0.238. Recording costs the same on both (it's the platform's).
  - The first run read pixels back from host-visible memory the CPU doesn't cache: 29 ms a frame for nothing, about 100 times OpenGL's. Readback now asks for `HOST_CACHED` memory where there is some (`USE_READBACK`). That fix also took the native GPU tests from 85 s to 30 s.
  - Games, debug builds headless (`--frames 90 --capture --size 640 360`) on each backend, compared pixel by pixel: Duel, Asteroids, Marbles, Sumo and Two Forks are the same bytes on Vulkan as on OpenGL. Tag differs in 663 of 230,400 pixels by 9 levels of 255 at most, in its filtered shadows, where it differs from itself run to run already (`notes/shaders.md`).
  - What's left between the two is texture and list uploads (a staging copy and two layout turns each, behind a full barrier) and the per-pass command buffers. No order-of-magnitude gap remains. This benchmark reads back every frame, which a game doesn't. The optimization pass below measured real frames and later cut the per-pass cost.
- **Optimization pass (2026-10-14).** The owner asked for one over every rendering layer ("ok do an optimization pass maybe on all rendering layers not just vulkan"); what follows is Claude's, behind the language. Real frames were measured, not the readback benchmark. The setup: release builds of Tag, Duel and Two Forks, hidden at 1920×1080 for 400 frames, with each renderer phase timed (temporary instrumentation, not committed) and averaged over frames 301–400. Four changes came out of it, three in the shared renderer and one in Vulkan:
  - **Mesh buffers are reused within a frame** (`spare_unused_meshes`, `taken_buffer` in `platform/src/platform.c`). Games that fill a list in a view each frame had meshes made again every frame, and creating a buffer costs OpenGL about 0.15 ms. Before any upload, a non-overlay list gives up the buffers of mesh places it doesn't draw, and `mesh_for` gives up a buffer too small for its place. A mesh that needs a buffer takes the smallest spare of its kind with room that is no more than four times its size plus 4 KB. Spares left over are freed at the end of the list, as the data textures' are, so no spare is counted in `vram` after a frame. That contract is pinned by `transient_data_textures`; a version that kept spares across frames failed it. (Since 2026-11-02 a mesh the list still keeps is no spare. Its buffers stay owned under the idle budget: see Meshes a frame skips stay resident, above.) Result: OpenGL's `upload_shaded` fell from 0.34 to 0.016 ms a frame in Tag, 0.12 to 0.001 in Duel, and 0.21 to 0.002 in Two Forks. WebGL buffers cost the same kind of work. Vulkan was already cheap here.
  - **Passes begin at their first draw** (`step_draws`, `draw_list`). A pass used to begin at every change of target, even when the steps before the next change drew nothing. Duel's list began 16 passes, 5 of them empty; Tag's began 15, 3 of them empty. The frame's target is cleared by the first pass that draws in it, or by a pass of its own at the end if nothing does. Clips set with no pass open apply when one begins. OpenGL costs nothing here. Every Vulkan pass is a command buffer, and every WebGPU pass is a command encoder and a `queue.submit`.
  - **Vulkan records one command buffer a pass, not two** (above). Vulkan's pass phase fell from 0.078 to 0.047 ms in Duel, 0.076 to 0.056 in Tag, and 0.033 to 0.024 in Two Forks. About a third of that came from lazy passes and the rest from the command buffers. OpenGL's same phase is 0.008–0.018 ms because its driver does the work later, on its own thread.
  - **The font atlas sends only the rows new glyphs changed** (`tide_font_atlas_changed` in `platform/src/font.h`). A new glyph used to send the whole atlas, 2048 pixels wide and up to 2048 rows. Tag's OpenGL atlas upload fell from 0.010 ms a frame on average to 0.000.
  - Validation: the native debug suite passed 2,327 of 2,328, with its GPU tests on Vulkan under the validation layer. `tide_platform_rooms`' two Firefox cases failed to open the browser under the parallel load, and the test passed when rerun alone. The web debug suite passed 2,249/2,249. Debug captures of Duel, Asteroids, Marbles, Sumo, Tag and Two Forks (`--frames 90 --capture --size 640 360`) are the same bytes as before the pass on both Vulkan and OpenGL.
  - Unchanged, by measurement: shapes cost 4–8 ns each to record and 7.5 ns to submit, and round rects and textured quads 12–23 ns (`tide_platform_draw --frames-bench`). That is the same on both backends and within a small factor of a hand-written loop. WebGPU still submits once a pass: one submit a frame would need staged copies in place of `queue.writeBuffer`, so that a write lands between the passes it falls between. The pipeline cache and the narrower barriers came after this pass (above).
- **MoltenVK on macOS** (2026-10-14, after the owner's "let's close those gaps"; Claude's picks, Provisional). Vulkan on Metal, from Khronos, Apache 2.0, version 1.4.2. It reports Vulkan 1.4, so push descriptors are core and a Mac's GPU passes the device checks above.
  - `cmake/MoltenVK.cmake` downloads the release's `MoltenVK-macos.tar` on Apple, pinned by SHA-256 as GLFW is. Its universal `libMoltenVK.dylib` (x86_64 and arm64, 10.9 MB) goes beside the build's programs (`bin/`), and into the package's `lib/native` with its license (`licenses/moltenvk`).
  - `tide build` and `tide run` put it beside a macOS game's program (`place_moltenvk`, `compiler/cli/build.c`). Only the slice for the game's CPU goes, found by `lib_universal_slice` (`compiler/cli/libraries.c`, tested by `cli_libraries_universal_slice`). That's about 5 MB, and each slice keeps the code signature it was built with.
  - `desktop/glfw.c`'s `find_vulkan`, before GLFW starts: a system loader (`libvulkan.1.dylib`, as the Vulkan SDK installs it, with its validation layer) first. Else `libMoltenVK.dylib` beside the program, or in an app bundle's `../Frameworks`. Its `vkGetInstanceProcAddr` goes to `glfwInitVulkanLoader`, so GLFW makes the window's surface with it (`VK_EXT_metal_surface`). With neither, the window doesn't open, saying what the game needs (until 2026-11-05 it drew with OpenGL). `gpu_vk.c` already enabled `VK_KHR_portability_enumeration` and `VK_KHR_portability_subset` where they're listed.
  - Tested: `tide_platform_moltenvk` (Apple only, no window or GPU, so CI's Macs run it) loads the MoltenVK beside it. It checks the instance has `VK_KHR_surface` and `VK_EXT_metal_surface`, is Vulkan 1.3 or newer, and makes an instance. Its first CI run (Build `38623397915`, macOS arm64) found MoltenVK 1.4.2 loading and reporting Vulkan 1.4 with both extensions. That run also showed MoltenVK has no `VK_KHR_portability_enumeration` when used directly: that extension is the Vulkan loader's. The test had wrongly required it, and now asks for it only where it's listed, as `gpu_vk.c` always did. With that, Build `38626860832` (`fe678b81`) passed every job, including the Android emulator tests in the Linux package, and published `v0.4.0-nightly.1`. Then it prints each device it lists, with whether it has push descriptors and a swapchain. The Mac-only C was checked on Windows by compiling for `arm64-apple-macos13` with `-Wall -Wextra -Wpedantic -Werror` against stand-in system headers.
  - Not tested: drawing on a Mac. Nothing here has one, and CI's runs no GPU test. The renderer's paths on MoltenVK (its portability subset, Apple GPUs' lack of `D24_UNORM_S8_UINT`, for which `D32_SFLOAT_S8_UINT` is the fallback) need a Mac to run the GPU tests.
- **Not done:** (storage buffers came 2026-11-05: Vulkan compute, above). A real device loss (a driver that starts over) is only simulated: `tide_gpu_vk_lose` takes the same path a lost device's errors do. A minimized window's swapchain isn't tested, since a hidden window can't be. Linux and macOS Vulkan need CI or a machine of theirs.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Rendering and platform

- **The renderer's retained textures are indexed by space/id** (`gpu_texture_index`, `texture_for` in `platform/src/platform.c`), as the draw list's are (`notes/textures.md`). The table stores array places and stays at most half full; frame pruning rebuilds it only after compaction, and a new device epoch clears it together with the texture count. Pixel version/shape/format/epoch checks, partial uploads, cube uploads, mip invalidation and GPU memory accounting are unchanged. `many_cached_textures` in `platform/tests/draw.c` draws 128 distinct images through growth, reverse warm lookup, changed version and size, pruning, reinserted ids and forgetting the list; it checks every image's pixels, exact uploaded byte counts and GPU memory changes. The WebGPU device-loss pass runs it again after reset.
  - **Measured 2026-10-08, Windows:** the real `texture_for` and cache structures extracted verbatim into a warmed stable-version harness, seven-pass median at 1,000/2,000/4,000/8,000 cached images, both strict `-O2` with the same engine dependency library. Before: 0.334/1.105/3.594/14.716 ms; after: 0.005/0.011/0.016/0.040 ms. Both have the same lookup checksum and zero GPU uploads; upload/free stubs abort if reached. This isolates lookup cost, not full rendering time.
- **Changing scratch shader data reuses obsolete allocations within one rendered list** (2026-10-10). After building steps, `prepare_data_textures` protects every existing texture named by an actual `READ_DATA` step, including an unchanged identity first sampled late in the list. Unprotected previous-frame integer `TIDE_PIXELS_LIST` textures with scratch content identities enter a transient pool indexed by exact width and height. A new matching identity takes that backend allocation and uploads its entire captured image; changed shape, version, epoch and ordinary retained textures keep their existing checks. Detaching an allocation clears its old owner's backend texture handle; the logical space/id remains indexed until pruning. Unused pooled allocations are freed before overlays and frame completion, so the renderer retains no extra GPU spares across frames or device epochs. Protection alone never marks a texture as drawn. `transient_data_textures` in `platform/tests/draw.c` checks every pixel of twelve small shader-data draws through changed identities, reversed order, an unchanged late read, mixed inactive draws, shape changes, shrinking and empty frames, append, separate lists and forgetting; it also pins exact uploaded bytes and end-of-frame GPU memory. The WebGPU device-loss pass repeats the fixture.
- Skinned meshes (2026-10-08) use the existing shaded-mesh path: generated vertex code reads two READ_DATA textures, a matrix palette and a vertex-to-influence stream, and applies deformation before the game's vert. No backend-specific skin pipeline or vertex layout is added; OpenGL/WebGL integer textures and WebGPU `textureLoad` serve the same checked stream. The CPU resolves the pose's hierarchy and inverse binds and captures palette data in the draw list, so later draws do not overwrite earlier poses. `platform/tests/skinned.tide`, `skinned.gltf` and `skinned.c` verify twelve pixels over two frames on OpenGL 3.3, WebGL 2 and WebGPU, including eight influences, custom normals/tangents and vertex defaults, independent poses, explicit clip placement and later world-position edits, and explicit depth/color passes; all three pass locally. Native uses label `gpu`, web's WebGPU variant label `webgpu`. The GPU stage's contracts are in `notes/shaders.md`; the draw syntax and proposed explicit skinned-instance list API are in `notes/drawing.md`.
  - **Palette preparation and batching (2026-10-10):** shared `draw_list` calls `tide_draw_prepare(list, tide_platform_jobs())` before reading captured data or starting GPU timing. Independent palette calculations can use the existing pool after simulation work has joined; recording and all backend calls stay on the rendering thread. Compatible characters with distinct poses share integer-texture atlas pages and a hidden instance matrix offset, so they can form one mesh run. The same hook covers ordinary lists, overlays and render-target capture. A missing pool prepares the same captured inputs serially. The skinned platform fixture additionally compares every prepared byte of 2,048 distinct poses on the real platform pool against serial preparation and reports how many worker callbacks joined; callers may finish alone according to the pool's contract. CPU submission measurements include this preparation; GPU queries start afterward and still include on-demand texture uploads. See `notes/drawing.md` for capture lifetime, the explicit C preparation contract and the legacy fallback for shaders without another instance lane.
  - Validation: complete Windows debug suites pass 2,064 native and 2,034 web CTests, with all 40 focused skinned runtime tests and unchanged deterministic pose bits. OpenGL, WebGL and WebGPU pixels pass. The actual native preparation probe observed three worker callbacks; the initial WebGPU round observed the caller alone, and WebGL exercised the no-pool fallback. Every prepared byte matched serial output. A bounded unsaved release smoke of all four 1,000-character modes passes; independent poses use one mesh draw plus the clear and still upload 4,096,000 palette bytes. Performance comparisons are recorded separately in `notes/benchmarks.md`.

- GLFW's objects use function/data sections in Release, RelWithDebInfo and MinSizeRel too (`cmake/GLFW.cmake`, 2026-10-10). Their archive members previously kept unrelated APIs together, although Tide's own objects already allowed release link collection. The optimization levels and debug link policy stay unchanged. With final merged sources and packaged Clang 23.1.2, an empty game's Windows executable falls from 993,280 to 977,408 bytes (15,872 saved) and Duel's from 1,438,208 to 1,420,800 (17,408 saved). The comparison holds all 38 Tide platform objects and the generated game/engine objects identical, changing only GLFW's two section flags; final sizes match the packaged build. An earlier source snapshot saved 15,360 bytes in each game. The empty game's map drops 97 unused GLFW public APIs, including Vulkan access, native handles and unused window/cursor setters; final executable code falls by 13,184 bytes and read-only data by 2,460, with unwind data and PE alignment accounting for whole-file sizes. Actual CMake-built GLFW objects reproduce the sectioned sizes; both games complete three hidden frames. The package cache regression checks that an unused Vulkan loader diagnostic is present in the Windows platform archive and debug executable, and absent from the release executable; native and web stripping cases pass. Web and Android do not build GLFW. Linux/macOS behavior remains CI's validation.
  - The regular font's 172,324 bytes remain in an empty game, as do all 365 Windows controller mapping strings (98,271 bytes plus 2,920 bytes of pointers). They are live dependencies of text and input initialization; section splitting removes neither capability. The bold file remains conditional as described below. Size optimization and ThinLTO were measured privately but not enabled: `-Os`/`-Oz` change performance tradeoffs, and `-O2` ThinLTO enlarged these native executables. Safe identical-code folding saved zero bytes in the empty game and 512 in Duel, so it was not enabled either.

- Windows, input and rendering are the platform layer's own, with no engine under it:
  - `platform/src/platform.c` is the frame loop, the devices and the renderer. What it needs of the system is `platform/src/window.h`: a window, what draws in it, and what its input devices say. Each build has one: `platform/desktop/glfw.c` (GLFW, for the window, keyboard, mouse, gamepads through its mappings, and clipboard), `platform/web/canvas.c` (the page, through `tide.js`) or `platform/android/activity.c` (NativeActivity).
  - The renderer is written once, against `platform/src/gpu.h`: buffers, textures, pipelines, targets and passes, whichever API is under them (shared, agreed, rather than a renderer per backend). It's shaped by WebGPU, the strictest: a pipeline is made once with its shaders, the vertices it reads and how it blends and tests depth; its uniforms are one block of bytes; drawing happens in passes that start cleared or as they were left, so a clear inside a list is drawn (a triangle over the target, which the clip cuts, and which sets depth back). Clip space is OpenGL's, and pixels and rows count from the top left; each backend turns those into its own. Compute joined it (2026-11-05), and every backend has it.
  - Backends: Vulkan 1.3 on desktop and Android (`platform/src/gpu_vk.c`: see Vulkan, above), WebGPU on the web (`platform/web/gpu_web.c`, whose functions are the page's: `tide.js` has the WebGPU objects), and the recorder inside another program (`gpu_record.c`). OpenGL 3.3, OpenGL ES 3 and WebGL 2 (`gpu_gl.c`) were removed 2026-11-05 (GL backends removed, above). The window says which draws (`tide_window_gpu`), and `tide_platform_renderer` names it; the stats overlay shows it.
  - The web draws with WebGPU, any adapter the browser gives, its software one too (since 2026-11-05; before, WebGL 2 took over where there was none or only the software one). The page asks for the device before the program starts (`tide.js`), as asking takes a while and the program can't wait for an answer; without one, the page says what the game needs. A device that goes away while the game runs (a driver that starts over) takes what was made on it: the page asks for another, and the renderer, which sees it's another (`tide_gpu.epoch`, `gpu_ready` in `platform/src/platform.c`), forgets what it had there and makes it again as lists draw, from the copies the lists and the font keep. Nothing draws meanwhile. `tide_platform_draw` (on the web) loses its device on purpose and checks its pixels again. With no device to be had again the canvas stays as it is, and the page says to reload.
  - (History, removed 2026-11-05.) `platform/src/gl.h` was OpenGL as its backend used it: only the functions it called, which the three shared, with no system header. On the web they're the page's (`tide.js` has each by its C name), on Android libGLESv3's, and on desktop they're looked up in the window's context through GLFW (`platform/desktop/gl.c`). Shared functions are added there and in `tide.js`. Desktop-only optional functions are loaded separately: timer queries and shared vertex bindings (OpenGL 4.3 or `ARB_vertex_attrib_binding`, above). Float render targets also use extension support (see A target holds several color images, below).
  - The renderer's own shaders (shapes, text, clears, the built-in meshes) are in `platform/src/shaders.h`, written by hand in WGSL (SPIR-V of it at build, for Vulkan). Until 2026-11-05 each had a GLSL twin for the GL backends. A game's shaders are tidec's, from its `frag` and `vert` declarations (below).
  - A pipeline samples up to 8 textures (12 since 2026-10-25, 16 since 2026-11-05, some through another's sampler: Sixteen textures a draw, above; in WGSL, texture i and its sampler at bindings 1 + 2i and 2 + 2i), each through a sampler of the backend's, nearest, blended or between its mipmaps too, and clamped, repeated or mirrored past its edges (`sample`'s `filter` and `wrap`), so one texture can be read several ways in a draw. Its vertex attributes are floats or ints (`TIDE_GPU_INT`...), 16 at most, and its uniform block 4096 bytes at most (`TIDE_GPU_MAX_UNIFORMS`), of which a game's shader has 3984 of its own (`TIDE_SHADER_UNIFORM_BYTES`, which tidec checks at the draw) after the engine's 96 (its camera, the screen and the tint): 255 float4s in all, as Android's emulator links no shader whose block is 256, counting its members as the vertex shader's uniform vectors; on WebGPU a draw's uniforms take as many 256 byte slots of the uniform buffer as they need. A texture that's a target's depth is read as a number or compared, through samplers of its own, which take no wrap (see A target says what it holds, below). `pipeline_free` lets one go.
  - A backend sets all the state it draws with at each pass, so nothing depends on what drew before it: a pass on another target or, on the web, the build this one took over from (see Hot reloading).
  - Text is `stb_truetype`'s (`platform/third_party/stb_truetype.h`, version 1.26 from nothings/stb, public domain, kept static to `platform/src/font.c` so a game's C can bring a copy of its own), in Tuffy, in two weights, each a file: regular (`platform/font/Tuffy.ttf`) and bold (`platform/font/Tuffy_Bold.ttf`), both version 1.28, from the one archive at tulrich.com/fonts (`tuffy-20120614.zip`), under the one `LICENSE.txt` beside them, public domain too, so games carry no license terms for any of it; GLFW's zlib license asks nothing of binaries. The build puts each in the library as an array (`cmake/embed.cmake`), an object of its own. A text's size is its font size, as CSS's and Unity's: the height of a line of it, with its capitals in the line's middle (what the font draws past a line, accents above and tails below, goes past evenly). Text has to be sharp at the sizes it's read at, and the font has no hints (stb_truetype would read none), so it's fitted to the target's pixels (`platform/src/font.c`): drawn at the whole number of pixels nearest its size, never stretched (but past 128 pixels, where nobody sees it); each glyph's outline moved onto them before it's drawn, as a hinted font's is; and each glyph a whole number of pixels from the last. The fitting finds a glyph's stems in its outline (an edge is a run of it along an axis, a straight side or a curve where it turns back; two edges that are each other's nearest across ink are a stem): upright stems and flat ones (bars, the tops and bottoms of bowls) are a whole number of pixels thick between pixel edges, those about as thick as the font's usual stem all alike, so letters keep one weight; the heights letters share (the baseline, the x-height, the capitals', the descenders') are on the same pixel edges in every glyph, and what round letters go past them by is dropped until it's a pixel; and the rest of the outline moves in proportion between the edges that moved. Stems and bars then fill their pixels, where an outline drawn as it falls blends them between two. What's slanted or round still covers pixels in part, and colors blend as the display's values, not as light, so light text's parts count for more (the text shaders, `platform/src/shaders.h`: a pixel's share to a power under 1, 0.55 for white), or they'd fade beside the stems; dark text is as it is. `tide_platform_font` (`platform/tests/font.c`, no window or GPU, so every platform's tests run it) checks that glyphs of upright and flat strokes have no pixel covered in part at any size from 8 to 48, and that letters share their heights and their stems' thickness, in both weights. Glyphs are drawn the first time each is needed at a size and in a weight into an atlas of a byte a pixel that grows as it needs, and text goes to the GPU as two triangles a glyph. `tide_platform_measure_text` is the size that's drawn (`tide_font_size`): its widest line by the same whole-pixel advances, and its lines' height, a line as high as the size and each newline the whole pixels lines are apart (`tide_font_pitch`, which drawing uses too), at the window's scale or, for a render target, at its image's own (the `tide_target_size` it's given). It needs no window open. It's the `tide_measure_fn` (`tide/draw.h`) hosts give the GUI, and what Tide code measures with: `Draw.MeasureText` is `tide_draw_measure_text` (`engine/src/draw_coordinates.c`), which asks it at the size the frame's 2D camera draws text at and gives the answer back in the units the view draws in, so a view measures what it draws, to the pixel. A host with no measure function gets a guess (`tide_draw_guess_text`). `tide_platform_draw` (`text_measures_as_drawn` in `platform/tests/draw.c`) draws text in parts, each where the last measures to, and checks they're the pixels of the text drawn at once, in both weights.
  - A text's position is a place of its box (agreed): `Draw.Text(text, position, size, color[, anchor][, font])`, with the GUI's `Anchor` (`tide_anchor`, in `tide/draw.h`), its top left corner without one. The box is what `MeasureText` gives, upper is towards the top of the screen under a camera too, and each line is against the anchor's side. The command keeps the anchor (`tide_draw_command.words`), and the platform places each line (`line_start`, `add_text` in `platform/src/platform.c`), on whole pixels as ever: text at an anchor is, to the pixel, its lines drawn from the corners its box puts them at, which `text_anchors` in `platform/tests/draw.c` checks for the nine, on the screen and under a camera, with one line and several, in both weights. `Anchor.UpperLeft` goes through the arithmetic it always did.
  - Bold is the font's own bold weight, an argument of the draw (agreed: `Font.Bold`, as `Filter.Point` is an argument), and a game that never uses it doesn't carry its file (agreed). How (as built 2026-10; since 2026-11-03 C names a font by a `tide_font` and lists keep a table of fonts, see Fonts of a game's own, above):
    - C names a weight by its file: `TIDE_FONT_REGULAR` (NULL) or `TIDE_FONT_BOLD`, the array `tide_font_bold_ttf` (`tide/draw.h`). `tide_draw_text_at` takes one, and so does a renderer's `tide_measure_fn`. A text command only says it's bold (`words.bold`), and its list has the file (`tide_draw_list.bold`, set by the frame's first bold text and cleared by `tide_draw_reset`), which the platform draws with. So the file reaches the renderer as data, from the code that draws with it.
    - Nothing in the engine or the platform layer names `tide_font_bold_ttf`: no source file of either may. Its array is alone in its object (`tide_font_bold.c`, which the build makes) in the platform layer's library, which stays a static one, so a linker only takes that object out for a program whose own code names the array. Generated C does for a game that says `Font.Bold`, or makes a `Font` of a number (`program.names_bold`, in the `tide_font_file` helper that turns a `Font` into its file), and for no other.
    - That's a program's link: CMake's, and `tide build`'s on every target. `tide run`'s library of the game has none of the platform layer, as its host does, so for a game that names the file (`game_info.bold`) tide links the library with the platform layer's too, which gives it that one object (`link_objects` in `compiler/cli/build.c`). A program with no platform layer that such a game is built into (a test's host, a server) needs the object too: `tide_add_game` links a `HOST`'s program with `tide_font_bold`, a library of that one object (`cmake/Tide.cmake`), so `duel_tests` builds as it did; a host built some other way links the platform layer's library, or defines the array itself, as nothing draws with it there.
    - `platform/src/font.c` keeps what it measures of a font per weight (`weight`: the file, its lines, its advances, and the fitting's heights and usual stems), and a glyph's key has its weight's number, which is another whenever a weight's file is: a game's library that was built again has its own copy of it, at another address, and nothing of the old one is read again. Both weights have the same lines, so text of both stands on one baseline. A code point the bold weight has no glyph for (it has 887 code points of the regular's 1499) is the regular weight's, drawn and measured so.
    - Stems are a whole number of pixels thick in both, by each weight's own usual stem, so under 13 pixels both are one pixel thick (and at 20, both two): there, bold differs in its letters' shapes and widths alone. Nothing fattens it to tell them apart. What the agreed design left open (this, the regular weight's name, what a missing glyph is) is in `docs/spec.md`, Views and drawing, awaiting the owner.
    - The GUI, the stats overlay and the debug views are in the regular weight.
    - `tide_platform_font_files` (`platform/tests/font_files.mjs`, every platform) reads two games as the build left them, `tests/text.tide`, which says `Font.Bold`, and `tests/text_plain.tide`, which doesn't, and checks the bold's file is in the first and none of it in the second. `tide_platform_text` is also text to look at: `build/debug/bin/tide_platform_text --frames 2 --capture text.png --size 1180 900` draws both weights at the sizes text is read at, light on dark and dark on light, and a text at each anchor.
- The agreed order from here, with OpenGL staying as a small fallback for machines without the main backends (browsers without WebGPU, older phones; superseded 2026-11-05, when compute became guaranteed and the GL backends were removed):
  1. Vulkan natively (MoltenVK on Apple until a Metal backend is worth writing), under the same `gpu.h` as WebGPU: explicit pipelines, buffers and compute. tidec writes SPIR-V for it then, beside GLSL and WGSL. **Built on desktop 2026-10-14** (see Vulkan, above): pipelines, buffers and SPIR-V; compute waits for Tide code that computes, as below. Android and MoltenVK followed the same day.
  2. The rest of render state (which side draws, masks), as draw settings like the ones there are (below), frags with other outputs than one color (drawing into a texture is there already: render targets), and Tide code that computes on the GPU. On the OpenGL fallback, compute runs on CPU threads instead, so games work everywhere (not taken: the GL backends were removed instead, 2026-11-05).
  - How a draw blends was part of that second step, and the owner brought it forward (2026-10-05), with tints, transforms and the settings rule: "these are things we need as part of the foundation of our drawing apis". What a 3D mesh does with depth followed (2026-10-06), from Alley's shadows, which hid each other: "this seems like our Draw3D or shaders or whatever might need some tweaking".
  - More of that second step is agreed (2026-10-07; `docs/spec.md`, Views and drawing, What the GPU side gains, which has each rule and which names are picks), and built in part: a target's format and its depth read are (below: A target says what it holds), the rest isn't. It came of how the demos drew shadows: Sumo laid its casters flat on the floor and shaded the floor again under them, as nothing could read a target's depth (its shadows are a shadow map since: see The physics games). The owner asked for no shadows feature but for a complete set of raw pieces a game builds any technique from, and decided each:
    - A draw with no frag keeps the engine's default look, and never means "depth only": what a draw writes is said.
    - A target says what it holds when it's made (a target of depth alone is the new one), and a frag reads a target's depth, plainly or compared with a number. Built.
    - Cull is a setting of `Draw3D`'s whose default is that the back of a triangle doesn't draw ("its what its expected in game engines"), so every 3D mesh in the repo has its winding checked with it: "we should NEVER be afraid to make changes based on our existing work (well until we release v1)". **Built 2026-10-07**: see the draw settings, below (Which side of a 3D mesh's triangles draws).
    - How `uv` wraps is chosen where a texture is sampled, which is built (below: What a `uv` past a texture's edges reads). Stencil comes as a GPU has it (a number a pixel, a test, an operation), last. **Built 2026-10-07**: see the draw settings, below (Stencil is a setting too).
    - A frag's outputs are `out`, its color too (the owner's idea), and it can have several. A vert hands its frag a struct through an `out` parameter. Verts and frags stay declarations of their own, paired at the draw (the owner, of grouping them in a `shader` block, which stays an open idea: "lets keep them separate"). **Built 2026-10-07**, several outputs too: see What a frag and a vert write, and A target holds several color images, below.
    - No setting for whether a draw writes color.
- **The drawing surface is being reshaped (agreed 2026-10-07, from a review of the graphics side the owner asked for: "I want to tackle all your points and I agree with them").** The owner wants the foundation complete from a rendering standpoint and simple to approach, and found three things off: `Grid2<Color>` being a texture, the names, and where settings go. What's agreed, each a card, in stages; a card that lands updates this file with what it built, and the users' pages with it:
  1. **One `Draw`, which has every method a render target has, and nothing else.** `Draw2D` and `Draw3D` go: the 2D and 3D forms are overloads told apart by type (`Draw.Camera(float2 center, float size)` beside `Draw.Camera(float3 position, quaternion rotation, float fov)`; `Draw.Mesh` of a `List<Vertex>` beside one of a `List<Vertex3>` with a transform), and the window is the same object a target is (`target.Clip`, `target.Depth`, `target.Stencil`, `target.Mesh` are the API; `Draw.X` is the window's). Every setting belongs to the destination it's called on, blend and tint too, with one rule for depth and cull: only 3D meshes have depth or a back. The old spellings are errors with the rename as a quick fix, as the earlier renames were. **Built 2026-10-07**: see One `Draw`, under Drawing below, and `docs/spec.md`, Provisional, One `Draw`, for the picks.
  2. **One word for the window, and `Screen` for the window's measurements alone:** `RenderTarget.Window()` and `Window(factor)` for the target that follows the window, `Draw.Screen()` and `Draw.SetScreen()` for drawing in the screen's pixels (the card named them `Pixels`, which went back to `Screen` the same day, below), `Screen.width` and `Screen.height` as they are. Depth alone is `Format.Depth`, and the formats are named by what they store so the scheme scales. **Built 2026-10-07**: see One word for the window, under Drawing below, and `docs/spec.md`, Provisional, One word for the window, and formats by what they store, for the picks.
  3. **`Texture` is the one name a draw and a `Sample` take.** A `Grid2<Color>` converts to one implicitly where a draw or a `Sample` wants one, as it already does underneath, and so does a `Grid2<float>` (a texture of one channel: a heightmap, a lookup table). A `Texture` value carries its filter and wrap, so `art.sprite = tiles` draws as `tiles` declared: the `[Filter]` and `[Wrap]` attributes become the sampling a `Texture` is born with, and a draw's or a `Sample`'s arguments an override, which leaves two places to say it where there were three. No fourth CPU image type. **Built 2026-10-07**: see `Texture` is the one type, below, and `docs/spec.md`, Provisional, Textures, for the picks.
  4. **A per-draw parameter struct** for `Mesh` and `Blit`, bound as an `Instanced` draw's struct is, so a blur's direction isn't a singleton field written between two blits: `Draw.Blit(src, dst, Blur, BlurParams { direction = float2(1, 0) })`. **Built 2026-10-07**: see A draw's values, below (after What a frag and a vert write), and `docs/spec.md`, Provisional, A draw's values, for the picks.
  5. **The cameras' matrices are readable** in a view, so a shadow map's frag isn't handed a world-to-clip matrix the game built by hand, z-flip and all. **Built 2026-10-07**: see The cameras as matrices, under Drawing below, and `docs/spec.md`, Provisional, The cameras as matrices, for the picks.
  6. **Mipmaps**, the one capability gap that keeps the 3D side from being complete (shimmer and cache thrash on textured floors at a distance); the card designs how a mipmapped sample is written where code may sample in a branch. **Built 2026-10-07**: see Mipmaps are a filter, below (after `Texture` is the one type), and `docs/spec.md`, Provisional, Mipmaps, for the picks.
  7. **Alpha and color space are decided, not left by omission:** everything blends straight alpha in display space today, assets aren't premultiplied and nothing is sRGB-aware. The card shows the owner the fringe bilinear sampling of straight-alpha sprites makes and the two ways out (premultiply at pack time with a premultiplied default blend, or keep display-space blending and document it), and the owner picks. **Picked and built 2026-10-07** (premultiplied under the hood, display space): see Alpha is premultiplied under the hood, under Drawing below, and `docs/spec.md`, Provisional, Alpha and color space, for the picks.
  8. **MSAA on render targets**, a `samples` argument, as 3D mesh edges have no antialiasing (smooth shapes are analytic, so 2D needs none). **Built 2026-10-07**: see A target's samples, below (after A target holds several color images), and `docs/spec.md`, Provisional, A target's samples, for the picks.
  9. **Cubemaps**, for skyboxes and reflections, last. **Built 2026-10-07**: see Cubemaps, below (after A target holds several color images), and `docs/spec.md`, Provisional, Cubemaps, for the picks. With it every card of this list is built.
  - What stays as it is, by the same review: frags and verts bound by their parameters, instancing that falls out of a view running per entity, textures with nothing to create or free, settings as a block form and a `Set` form reset every frame, logical pixels on targets, the GPU kept out of the match, `gpu.h`'s shape, the analytic shapes and the fitted text, and the enums `Blend`, `Depth`, `Cull`, `Wrap`, `Filter`, and `Instance`. A frag's built-in `Pixel` parameter became its implicit `this` later the same day (below).
- Views are written in Tide (`view` declarations). Their `Draw` calls record commands into a renderer-agnostic draw list (`tide/draw.h`), which grows as it needs, with no limit but memory. The platform layer renders the list (`tide_platform_draw`), so another backend only means another of that function. Drawing a lot has to stay cheap: shapes (rects, circles, their outlines and lines, and the smooth shapes, below) go to the GPU as instances of one quad, each its shape in window pixels, so a run of them between text and clears is one draw call (`platform/src/platform.c`), and so is a run of text. `tide_platform_draw --bench` (`?bench` on the web) times frames of 65,536 shapes, of as many textured quads of one mesh, and of as many instances of a 3D cube, and of what a game keeps and the GPU reads: a big grid that's a texture and a big list a shader reads, each with a little changed a frame, a list made again each frame, and a mesh whose every corner moves. The stats show in the bottom right corner when the host asks (`tide_run_desc.stats`: `STATS`, `tide run --stats`) or the game's `stats` setting does (`tide_game.stats`), which the host reads every frame (`tide_run_stats` in `tide/host.h`), so a reload brings them or takes them away. They're a drawer (below): one line, which opens to the frame rate, what a frame's work takes on its thread (`cpu`: the frame function up to the drawer, so not the wait for the display), ping, bandwidth (sessions count the bytes of their datagrams over networks, not loopback: `tide_session_status.sent_bytes` and `received_bytes`), entity count, the threads ticks run on (`tide_platform_jobs`), the memory the program has (`tide_platform_memory`, `platform/src/memory.c`: the working set on Windows, what's in RAM on Linux and Android, the footprint on macOS, and on the web the WebAssembly memory's size), what draws and in how many draw calls (`tide_platform_draw_calls`: the steps of the last list that drew, the drawer's aside). It's for a glance, so a number that joins it has to earn its line: the drawer holds 16, including rendering CPU and GPU time and the submitted vertex count. `vram` is one (the owner, 2026-10-07: "lets keep track of it ourselves"): what the renderer has on the GPU by its own count, `tide_platform_gpu_memory` (`platform/src/platform.c`: every buffer, texture, cube and target it asks the backend for goes through `made_*` and `free_*` there, which keep each one's bytes by the backend's id, a texture's smaller images a third more, and a lost device zeroes it all in `gpu_ready`). It's what was asked for, by the formats' bytes, so it's the same number on every backend, and never the window's own framebuffer, the pipelines or what the driver keeps; `check_gpu_memory` in `platform/tests/targets.c` pins a target's bytes and that a disposed one's go. The profiler shows the same two numbers in its frame column's last row, `Memory`, with no bar (`tide_profiler.memory`, `gpu_memory`, which the host sets once a frame, reading the program's memory once a second as the drawer does).
