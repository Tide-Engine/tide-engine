## 32-bit numbers loaded exactly, typed volumes blended, depth cubes, SampleLevel reads levels (2026-11-14)

The owner's "make sure everything we can add or validate we do" took the remaining refusals of the section below.
Every rule is Provisional (Claude) in docs/spec.md: Portable filtering, Number images as array layers and cube
faces, Depth cubes (Cubemaps), explicit mip level, and Compute's volume sampling. Users: game/shaders.md (Number
images, Choosing a mip level, cubes), game/render-targets.md (A cube of depth alone, readback), meshes-and-textures.md.

- **What a game gains.** `Filter.Bilinear` on a typed R32f/Rgba32f `Texture`, `TextureArray` layer or `Texture3D`:
  four (eight) texels loaded and blended in the shader, bit-identical everywhere (`Point`, and `SampleLevel`/`SampleGrad`
  naming Point/Bilinear, load the nearest). Typed `Texture3D` Bilinear: Rgba16f and `Grid3<float>` through the sampler
  (`TIDE_SHADER_READ_NUMBER_VOLUME_BLEND`, 16, a GPU `READ_VOLUME`, no `tide_straight`, zeros for none from the
  existing zero volume). `TextureArray`/`Cubemap` of R32f, Rgba32f and R32i; `[Raw(int)]` on arrays, cubes and helper
  parameters; a typed array's Point read is a load where its format is 32-bit or unknown (a kernel's field). `Cubemap(size, Format.Depth)` with
  `cube.depth`, a `DepthCubemap` (`TY_DEPTH_CUBEMAP`, appended to the type kinds; `tide_depth_ref` of the cube's id),
  `Sample(direction)` and `Compare(direction, value)`. A Color/cube/array `SampleLevel` that names no filter is
  Trilinear (`said_filter` 2 in `ex_sample_read`).
- **Compiler** (`shader.c`). `sampling_of` = `sampling_as` (the old body) then `exact_sampling(s, format, typed)`: a
  typed field's format comes from `image_target_format` of its assignments (fixed for every typed field already), a
  frag's or vert's own Texture's from the texture its draw gives there (`shader.source_formats`, one for each of the
  draw's textures, `given_source_format`; part of `plan`'s key, `same_source_formats`, so only a 7/8 one splits a
  shader; `writing` is the shader `emitter` is writing, NULL while checking, and the layout refines direct samples
  by `source_format_of`; one `shader.source_format` for the frag's one texture until the merge with several
  textures, 2026-11-15, shaders.md top). Refinement: `NUMBER_BLEND`/typed `VOLUME` filter <= 1 on
  7/8 -> `READ_NUMBER`/`READ_NUMBER_VOLUME`; typed `ARRAY` filter 0 (a format not known blendable: 32-bit, or unknown as a kernel's is; a known R8-Rgba16f one keeps its one sampled binding, which a draw's 16 textures need) or <= 1 (7/8/9) -> `READ_NUMBER_LAYERS`
  /`READ_INT_LAYERS` (new compiler kinds, TIDE 17/18 -> GPU `READ_NUMBER_ARRAY`/`READ_INT_ARRAY`, 13/14); typed `CUBE`
  filter 0 on 7/8/9 -> the same layers kinds. `[Raw(int)]` arrays/cubes go straight to `READ_INT_LAYERS`.
  `ex_numeric_sample` writes the helpers: wrap on the uv, then either the nearest texel or four/eight corners whose
  integer places are wrapped by `tide_index{2,3}_{0,1,2}` (clamp, repeat, mirrored repeat) and `mix`ed; a cube's
  `tide_cube_face(d) -> (u, v, face)` (longest axis, ties x then y then z; the gpu.h face layout, the inverse of
  ReadTargetFace's directions), loaded from `texture_2d_array`. A read written for a sampler returns all four
  channels and `ex_sample` narrows. What stays on a sampler for 7/8 (levels, gradients, Trilinear, a cube blended) is
  `refuse_blended_numbers` at the draw. Depth cubes: `ex_depth_cube_read` emits `tide_depth_cube<t>_<s>` /
  `tide_compare_cube<t>_<s>` (face math then the `DEPTH_ARRAY`/`COMPARE_ARRAY` read, `tide_depth` both ways), the
  texture a `texture_depth_2d_array`, so `texture_reads` say `DEPTH_ARRAY`: no new runtime read kind. `check_image_sources`
  refuses a `DepthCubemap` whose cube's known format isn't Depth (`image_target_format` follows `cube.depth`).
- **Checker** (`check.c`): Cubemap's Depth/32-bit refusals and TextureArray's 32-bit one gone; `[Raw(int)]` on layered
  fields and parameters allowed; int array/cube filters, int volume Bilinear refused ("a GPU blends no integers");
  typed float volumes take Bilinear. `cube.depth` (local code; an asset's is an error), `check_depth_cube_read`, the
  type through holds/arrays/purity/codegen (`tide_cubemap_depth`, reload like `DEPTH_TEXTURE_ARRAY`, `tide_draw_depth_source`).
- **Runtime** (`platform.c`). `cube_layered(id)`: a cube of depth alone or 32-bit numbers is `made_array(format, s, s,
  6)` with `made_layer` faces (`cube_for`), never `gpu->cube`; `texture_to_read_as` gives it only to `NUMBER_ARRAY`/
  `INT_ARRAY` (float vs R32i) and its depth array to a `DEPTH_ARRAY` read of `TIDE_PIXELS_TARGET_DEPTH` of the cube's
  id (`of_cube_depth`: `of_array`, `of_destination` and source preparation take it; a color cube's depth reads none),
  and a `READ_CUBE` of a layered cube or a filtering `READ_ARRAY` of a 32-bit array none. New fallbacks
  `gpu_state.number_layers`/`int_layers` (1x1x1 arrays of RGBA32F/R32I, zero on every backend). Mipmaps are never made
  of 32-bit images, layered cubes or 32-bit target images (`unblended_format`). Readback: a layered cube's face reads
  through the layer kernels (`PIXEL_READ_LAYER_DEPTH`, `_LAYER_NUMBERS`); R32i faces and layers through the new
  `ReadTargetLayerInts` (cull.tide; `gpu_list.c` no longer refuses int faces/layers); `ReadTargetLayerNumbers` is now
  a load, so it reads R32f/Rgba32f layers too. read_pixels_wgsl.h regenerated, shaders.h and the CLI's Unity count
  follow (merged with the banded part reads of the same day: ReadTargetLayerInts takes their rows/first/left/top
  uniforms, PIXEL_READ_LAYER_INTS sits before the volume kernels; 22 computes).
- **Backends.** gpu.h `TIDE_GPU_READ_NUMBER_ARRAY`/`_INT_ARRAY` (loaded, no sampler: `tide_gpu_sampler_read`'s
  `>= NUMBER` covers them). WebGPU: `layoutFor` 2d-array `unfilterable-float`/`sint`; `gpu_array` takes kinds 7-9 with
  one level. Vulkan: `color_of` maps R32_SFLOAT/R32G32B32A32_SFLOAT/R32_SINT back (a layer target's kind was BYTES and
  stopped the program: "a pipeline is for targets that hold something else"); descriptors were already read-agnostic.
  The recorder passes the new read values through unchanged; no embed ABI or packet change.
- **Unity's replay (not changed here: TideRender.cs is the owner's later pass).** What it must do: (1) bind a recorded
  array to the two new read kinds 13/14 as a `Texture2DArray` (the HLSL is `Texture2DArray<float4|int4>.Load`, checked
  by fxc); `EmptyRead` gives `white` for an unknown kind, which is never reached while the engine binds its own
  recorded fallback arrays, but should give a zero RGBA32F/R32_SInt Tex2DArray for 13/14; (2) a depth cube and a
  32-bit cube arrive as ordinary array packets (depth-only and color arrays, `TIDE_GPU_CUBE_FACES` layers, layer
  targets), already replayed for `TextureArray`; their reads are its existing `Texture2DArray` depth compare (Gather
  PCF) and loads, so the picture matches Vulkan and WebGPU; (3) the catalogue gains ReadTargetLayerInts and a changed
  ReadTargetLayerNumbers, written by `tide unity`. Not replayed in an editor for this change.
- **Tests.** Shaders: `exact_numbers` and `depth_cubes` (new WGSL goldens; their SPIR-V passes spirv-val and HLSL fxc),
  `number_filters` and `linear_textures` regenerated (typed array Point loads; SampleLevel's Trilinear samplers).
  Errors: 12 new (`number_cube_blend_r32f`, `number_array_level_rgba32f`, `number_array_int_filter`,
  `number_cube_int_filter`, `number_volume_int_blend`, `depth_cube_*` x7), 7 changed messages, 10 removed (they compile
  now: `cubemap_target_depth`, `number_filter_r32f`, `number_filter_source_r32f`, `number_*_format*`, `*_role_int*`).
  Units: `draw_number_cubes_and_arrays_hold_every_format` (every format, a depth cube's face and depth and its reload).
  LSP: `lsp_exact_numbers_and_depth_cubes`. Pixels: platform/tests/images 16 probes (R32f/Rgba32f blends clamped,
  repeated, mirrored, through a helper and a frag's own source; R32f/Rgba32f/R32i layers; an R32f cube's six faces and
  a drawn face's halves; R32i/Rgba32f faces; an R32f volume's eight cells; Rgba16f and Grid3<float> volumes through the
  sampler; Color texture/cube/array SampleLevel unnamed; a depth cube sampled, compared and through a helper; none
  given), registered also as `tide_platform_images_depth_fallback`; platform/tests/readback: R32f/R32i faces, R32i and
  Rgba32f layers, a depth cube's faces, bit for bit.
- **Validation** (Windows, Clang 23, Node 24.22.1, hidden): sanitize Vulkan `tide_platform_images` (+ depth fallback),
  `_readback` (+ fallback), `_targets`, `_shaders`, `_compute`, `_kernels`, `_depth_reads` (+ fallbacks), every
  `tidec_error_*`, e2e cubemaps/texture_arrays/number_images/target_reads/render_targets/texture3d, `tide_tests`,
  `tidels_tests`, `tide_cli_tests`, `tide_spirv_shaders_valid`, `tide_hlsl_shaders_valid`; web-debug SwiftShader
  WebGPU images/readback and their fallbacks (device loss included), the WASI e2e and `tide_web_runtime`. Not run:
  Unity, Android, Metal, real-GPU web lanes.
- **Not built, and why.** Mipmaps of R32f/Rgba32f (a kernel-made chain and an eight-load level blend weighed, spec);
  blending a 32-bit cube across a face's edge (needs the faces beside it; nearest only, an error that names R16f);
  a color cube's depth (its faces' depths are separate images; the spec says to draw a depth cube too);
  `cube.depth.size`. Still left out by the owner: a blend per output, a chosen mip level as a destination, frag-drawn
  volume slices, an sRGB target format; the precise depth read waits on the owner.

## Reads of any size, of parts and of volumes; grids kept across waits; dropped reads let go (2026-11-14)

The readback audit's workstream (owner, 2026-11-14: "make sure everything we can add or validate we do"). Every
rule is a pick, Provisional in docs/spec.md (Grids a task keeps across its waits; Reads of any size, of parts and of
volumes; the GPU lists' reads bullet for dropped tasks). Users: game/render-targets.md, game/tasks.md,
language/grids.md. Corrects "Owned pixel readback" below (the refusal of a wait while a grid is held is gone) and
"Readback of every image" (its limits).

- **Grids across waits** (the high finding: the first fix refused a wait only while an `is` grid local was in scope,
  so a failable local's grid and the grid of an earlier `await` in the same statement were carried as raw scratch
  handles across the wait, and read freed or reused scratch after it). Now `gen_spill` has a grid case,
  `tide_grid_carry(&slot, value, shape, where)`: a helper written into the game's C (no runtime function), which
  leaves the slot alone where the value is the slot's own grid (the task took it back and didn't change it), copies a
  grid code didn't make first (a world's, the pack's: `tide_grid_set` only takes a scratch grid's cells), and else is
  `tide_grid_set` into the task's world's heap. `gen_reload` gives the local the frame's grid as it is; `place_where`
  of a task's grid local is `tide_grid_mine(&local, shape)`, which makes it a scratch copy before any write (a cell,
  `Clear`, `Decode`, a `mut` argument) and says `TIDE_IN_SCRATCH`; a whole assignment to it takes the new grid with no
  copy. A dead slot holding a grid is released at the next wait (`slot_holds_grid` in `gen_spill_live`). The frame's
  release already released grid fields. The checker's blanket refusal in `check_await` and the S_VAR "a task can't
  keep a grid while it waits" are gone; `errors/read_target_kept_across_wait` and `errors/grid_async_local` became
  `tidec_e2e_task_grids`. Match tasks keep theirs in the match heap, which `tide_heaps_hash`, the snapshot copy and
  packing already cover. `tide_assign_<T>`'s grid paths use `tide_grid_carry` too, so a struct value with a grid
  carried in a frame (a foreach over an asset `Map<string, Grid2<Color>>`) keeps it across a second wait (it was
  released and re-owned as empty) and copies a pack grid (it took the size alone).
- **Dropped tasks forget their GPU read** (`gen_frame_forget_gpu`, `frame_reads_gpu`: a frame's own ticket or an
  awaited child frame's): `tide_forget_gpu_<frame>` on the gone/cancelled branch of the task pass, the `tide_r < 0`
  paths of the run loop and of a task's start, `tide_task_local_cancel_work` and `tide_local_free`;
  `tide_reset_gpu_<frame>` (ticket 0, read again) as a local world is unpacked. A swap that keeps the frames
  (rebind) forgets nothing: the host's table is the next build's. The 600-frame release stays as a backstop.
- **Reads of any size.** `target_read_prepare` clamps the part asked for to the image (`pixel_span`), and splits its
  rows into bands of at most `PIXEL_BAND_BYTES` (32 MiB) and `PIXEL_BAND_ROWS` (65,535). Each band is a storage
  buffer of its own (`made_buffer`), its list's header at word 0, bound where the kernel's list's pool goes (after
  `bind_pools`, so the other pool is still bound): no GPU list's pool grows, and every band's kernel runs in the same
  compute pass of the same frame. `compute.inc`'s `pixel_reads[slot]` (serial, bands, failed) replaces the pool
  ranges; each band's `buffer_read` begins as soon as one can (the request is BEGUN at its first), its `buffer_read_done`
  writes into the request's data at the band's place, and the read finishes once all are back, failed where one came
  back -1. A band of a request let go of before it began goes; a forgotten BEGUN one begins no more bands and finishes
  as its begun ones come back. Device loss drops the bands (ids alone) and fails a banded or begun read. The 4,000,000
  pixel check is gone; `PIXEL_READ_PENDING_MOST` (512 MiB) bounds what reads hold together.
- **Kernels** (`cull.tide`): every ReadTarget kernel indexes by `this.at.xy` over a 2D dispatch of a band, with
  uniforms `width` (the part's), `rows` and `first` (the band's), `left`, `top` (the part's first pixel), `across`,
  `down` (the image's size, for its uv), and `face`/`layer`. Four new ones, ReadVolumeColor/Float/Float4/Int, read a
  Texture3D's cells exactly (`image[int3]`) by row (a y and a z each: `VolumeCell`). `pixel_read_kernel` names each
  kernel's uniforms; `read_pixels_wgsl.h` and shaders.h list fifteen; Unity's CLI catalogue writes 21 computes.
- **Engine** (`gpu_list.c`, draw.h): `tide_render_target_read_begin_part(target, image, kind, from[3], size[3])`
  (begin_as is it with all of the image); `tide_gpu_read_request` gains `from`, `size`, `depth` (appended; embed ABI
  26, Unity's `TideLibrary.Abi`; TIDE_HOST_SHAPE counts the shared table's size), `tide_gpu_read_result.depth`. A
  volume (`TIDE_RENDER_VOLUME`) is named as image 0 of its one format, no depth. `tide_render_target_read_grid` makes
  a Grid3 for a volume's result and fills a row's run of a chunk with one poke and one copy (a `memcpy` of the run where
  a pixel is its cell's words). Measured on this machine (sanitize build, ASan, a 2000x2000 stand-in result, three
  runs each, a temporary test not kept): `Grid2<float>` 93-98 ms a cell at a time, 6.4-7.2 ms now; `Grid2<Color>`
  103-119 ms, 26-30 ms now (64 MB of copying). Release builds weren't timed.
- **Renderer, volumes:** a volume nothing drew with is made for the read (`volume_for` of a description of it), and
  one still being made (`texture_status` 0) is asked again next frame.
- **Compiler:** `Read`/`ReadDepth` take `int2 from, int2 size` after the image's number (`expr.read_part`), the image
  number folds (`fold_int`: a const of constants and operators; `Read(-1)` gets the range message), and a Texture3D's
  `Read<T>([int3 from, int3 size])` (`expr.read_volume`) gives a `Grid3`, its type held against the volume's format by
  `check_target_read`. Generated C calls begin_part only for a part (other reads are the C they were).
- **Tests.** `tidec_e2e_task_grids` (local task: two reads in a statement, a read and an async call among a call's
  arguments, a failable local, an `is` name across a read of another target, a made grid changed after waits and
  cleared, an asset list's and an asset map's grids a frame at a time, with a view scribbling scratch each frame; a
  match task's grid across ticks, packed and unpacked as it waits, hashes equal each tick); both fail with the old raw
  spill. `tidec_e2e_target_reads`: a computed const image number, a read cancelled asked and begun and one waiting as
  the local world goes (each frees its slot; fails without the forget), parts and volumes as asked (refusals, a Grid3
  of a box), grids of reads 1 to 186 wide with a step of 1 and 4. `draw_pixel_reads_pending_through_a_build_unload`
  (tests/test_draw_settings.c). `tide_platform_reload_reads`: a target read asked as a build unloads fails, and the
  host's renderer never resolves it (an ASan use-after-free without `pixel_reads_unload`). `tide_platform_readback`,
  native Vulkan (validation layers) and WebGPU, and under the depth fallback: a 3840x2160 target, a 4K window's (8.3
  million pixels, 127 MiB of Colors, four bands; read where it's kept, a band's last row and the next's checked), a
  2100x2000 Rgba8 target (4.2 million pixels, three bands, rows checked across the seams) and R32f one bit for bit, a corner past the edge clamped, one
  pixel of an int image, a part clamped from negative corners, a depth's, a face's and a layer's part, a part outside
  and a part of nothing failing, four Texture3D formats whole and a box, a written depth of 0.1 within 2^-24 (and as
  `1 - (1 - 0.1f)` under float storage), then the device lost with a read on its way back (it fails), and reads on the
  next device exact. Errors' four argument messages changed; `read_target_index_negative` (`Read(-1)` gets the range
  message).
- **Not run:** Unity. The Sample's read kernels (`Assets/Tests/Resources/TideShaders/00000000c0de0014` and
  `c0de0061`-`68`.compute) were written again from the new WGSL with `tide_wgsl_translate --hlsl` (the old WGSL
  reproduced the checked-in files byte for byte but for c0de0014, already stale), and ComputeReplayTests' read
  commands now give the banded layout (48 bytes of uniforms, a 2D dispatch): neither was compiled or replayed in an
  editor. Android, Metal and the real-GPU web lanes were not run. The asked-with-bands state at device loss (every
  band's `buffer_read` refused) has no hook to reach it.
- **Not built:** reading a Texture asset back (spec: the same pixels are a `Grid2<Color>` asset on the CPU, and the
  GPU's copy can be block-compressed).

## Kernels read every kind of image, and multisampled targets have smaller images everywhere (2026-11-14)

An audit of the renderer's paths (Vulkan and WebGPU) found kernels reading images through fallbacks only draws
made, mipmaps kernels never had made, and gaps beside them; the owner's "make sure everything we can add or validate
we do" covers building them. Every rule is a pick, Provisional in docs/spec.md (Compute on the GPU, what a kernel
reads; Views and drawing, Portable filtering and A target's samples). Users: game/shaders.md, Reading images;
game/render-targets.md, Smooth edges.

- **Fallbacks for kernels.** `texture_to_read_as` worked out its `none` before looking at the texture, and the far
  depth, the dark cube and the white/zero arrays were made only in `upload_shaded`'s loop over SHADED/BLIT steps.
  A kernel reading a depth with no frag reading one called `target_depth(0)`: `vk.targets[-1]` on Vulkan, a
  TypeError on WebGPU; a cube or array fallback of 0 left a Vulkan binding unwritten and kept a WebGPU run from
  ever running. Now: `make_read_fallbacks(shader)` (the loop, factored out) also runs for every STEP_COMPUTE step,
  before any pass; `read_fallback(read, blend)` is called only on the paths that return none (the one-pixel
  textures made there as before, the pass-needing ones only found); `vk_target_texture`/`vk_target_depth` and
  tide.js's `gpu_target_texture`/`gpu_target_depth` give 0 for an id that's no target's; and `run_compute` passes by
  a run with any image 0 on every backend (it had only on WebGPU, through `texture_status`).
- **An array of depth alone read as colors** (texture_to_read_as's array branch) is none now, as a depth-alone
  target's color already was through `vk_target_texture`: a kernel bound the depth layers to a float, filtering
  binding (refused by WebGPU, undefined filtering on Vulkan). Draws were already passed by (`texture_unavailable`).
  tidec now refuses it where the constructor is seen (`check_image_sources`, shader.c): a `TextureArray` or a
  `Texture` field, or a frag's direct source, holding Format.Depth's image read as colors, naming the
  `DepthTextureArray`/`DepthTexture` field to read instead (`array.depth.Sample` isn't GPU code; the audit's
  suggested message named it). `image_format_accepts(1, ...)` is unchanged: frag outputs into depth alone rely on it.
- **Mipmaps and the format guard.** `blends_target_format` is the one rule for both: `texture_to_read_as` gives a
  filtering read of an R32f/Rgba32f/R32i target image none, and `mipmaps_needed` makes no chain of one (WebGPU's
  `gpu_mipmaps` drew r32sint/r32float through a filtering pipeline: validation errors every frame the target was
  drawn). `gpu_mipmaps` returns early for those formats itself (`blendedFormat`), as a backstop.
- **Kernels have mipmaps made.** `run_compute` calls `prepare_mipmaps` for each COLOR/CUBE/ARRAY slot it doesn't
  write, with the slot's filter, outside a pass, as `prepare_step_mipmaps` does for draws. And a pass begun on a
  destination after a kernel ran (without another destination chosen between) counts as drawn again
  (`after_kernel`, `target_drawn`): before, a draw, a run that read the target's chain, and a draw into the same
  target left `mipped == drawn`, so the next read got the chain of the first draw. A kernel writing a target image
  whose chain exists binds its full view as storage on Vulkan, which validation accepts (stores go to the base
  level); WebGPU binds `first`, its one-level view.
- **Multisampled targets' chains.** WebGPU's resolve textures now carry `format` and `usage`, so `gpu_mipmaps`
  remakes them with levels and `first` becomes level 0, which `gpu_begin` already resolves into. Vulkan had it.
  The recorder needs nothing new: `TIDE_EMBED_RENDER_TARGET` says the target's samples in its format and names
  its resolved color, and `TIDE_EMBED_RENDER_MIPMAPS` names that color when a read needs a chain
  (`embed_render_multisampled_target_mipmaps` pins both, and that an R32f image read Trilinear asks for none).
  `TideRender.cs` was left alone: making that chain there (a one-sample companion with mips, resolved into before
  GenerateMips) is the next Unity pass's (unity.md).
- **Tests.** `tide_platform_kernel_reads` (new: `platform/tests/kernel_reads.tide/.c`), native Vulkan with
  validation and web-debug WebGPU, the device lost and found on the web, and `tide_platform_kernel_reads_none`
  (web, `gpu=none`): 25 probes (26 since the correction below) in a game where no frag reads a depth, cube or array: no depth, cube, array, array
  depth, typed cube/array/image; disposed target color, depth, cube, array (colors and depth), typed image; a depth a
  frag wrote (read and compared), a face, a layer, depth layers; a depth-alone array through `?:`; level 3 of R16f,
  Color, an R16f layer, an R16f face, 4-sample R16f and Color targets, read at the middle of a column of 4 left of a
  quarter's line so level 0 (0) and level 3 (1) differ; the same target drawn again after the kernel in one pass
  run (2); an R32i image through `?:` at a level; a chain written by a kernel then read at levels 0 and 3. Without
  the kernels' fallbacks the first 15 fail (the runs are passed by), without the kernels' mipmaps the six level
  probes, without `after_kernel` the redraw probe. `tide_platform_images` gained a 4-sample R16f and Color target's
  level 3 in a frag and a Trilinear read of an R32i image (white, and on WebGPU no validation error). Errors:
  `texture_array_depth_alone_colors`, `_kernel`, `render_target_depth_alone_color`, `_source`.
- **Correction (review, same day): typed cubes' zeros weren't tested.** Every typed cube and array given no image
  was `[Raw(float)]`, whose one channel is 0 in the zeros and in the dark cube alike, so dropping `zero_cube` from
  `make_read_fallbacks` (or `read_fallback`'s `blend && cube` branch) left every suite green: a typed cube would
  have read (0,0,0,1). kernel_reads' never-given typed cube and array are now `[Raw(float4)]`, checked to be four
  zeros, and a new probe (row 0, column 15) reads a disposed `[Raw(float4)]` Rgba16f cube and array; images.tide's
  frag probe of no array and no cube (filtered mode 6) reads four channels too. Not making `zero_cube`, or not
  choosing it in `read_fallback`, fails the kernel's never-given and disposed probes and the frag's, on Vulkan; not
  choosing `zero_array` fails the same three. Sanitize (Vulkan) and web-debug (WebGPU, device lost and found;
  `gpu=none`): kernel_reads and images pass.
- **Validation** (Windows, Clang 23, Node 24.22.1, hidden): sanitize: 29 renderer `gpu` fixtures (draw, targets,
  compute, images, kernels, kernel_reads, occlusion, cull, shaders, prewarm, readback, depth fallbacks and others),
  draw units 129/129, the embed suite 29/29, the 84 related diagnostics, 73 shader goldens and texture e2es;
  web-debug: 21 pixel fixtures on software WebGPU, `kernel_reads_none`, WASI draw units 129/129.
- **Not run:** Unity's replay (no C# changed), Android, Metal, the optional real-GPU web lanes.

## Number images blend, have mipmaps, and make array layers and cube faces (2026-11-14)

The owner asked that the first day's conservative refusals go where every backend can do the work ("make sure
everything we can add or validate we do"). Every rule is a pick, Provisional in docs/spec.md, Views and drawing,
Portable filtering and Number images as array layers and cube faces. Users: game/render-targets.md, game/shaders.md.

- **What changed for a game.** A typed read that names no filter is still the nearest pixel. `[Raw(float)]`,
  `[Raw(float2)]` and `[Raw(float4)]` Texture reads of R8/Rg8/R16f/Rg16f/Rgba16f images take Bilinear, Trilinear and
  Anisotropic, SampleLevel and SampleGrad (unsaid filter there: Trilinear). `TextureArray` and `Cubemap` constructors
  take R8/Rg8/R16f/Rg16f; their fields and GPU helper parameters take the three float roles; frag outputs and typed
  Clear into `layers[i]`/`faces[i]` are matched to the format. R32f/Rgba32f/R32i stay nearest 2D images and are refused
  as layers and faces; `[Raw(int)]` on an array or cube is an error.
- **Compiler.** `typed_through_sampler` (shader.c) decides by syntax: a typed float Texture's read that blends, names a
  level or takes gradients is `READ_NUMBER_BLEND`, a color's binding and sampler with no `tide_straight` and a
  swizzle to the role's channels (`ex_sample` wraps `ex_sample_read`); a typed array's and cube's reads are the
  existing `READ_ARRAY`/`READ_CUBE` the same way. `sampling_of` makes a typed image's filter and wrap the Sample's
  alone (never carried by the image). Because the read kind is syntactic, a GPU helper's sampling metadata needs no
  format; `check_image_sources` then checks each draw: the field's fixed constructor format must fit the role (now for
  TextureArray and Cubemap fields too) and a `READ_NUMBER_BLEND` texture must not be R32f/Rgba32f, with the fix in
  the message. `image_target_format` follows `Cubemap(...)`/`TextureArray(...)` constructors and `faces[i]`/`layers[i]`.
  Generated C says `TIDE_SHADER_READ_NUMBER_BLEND`, `_NUMBER_ARRAY` or `_NUMBER_CUBE` in `texture_reads`; shaders
  that use none of it are byte for byte what they were (the 67 existing shader goldens are unchanged).
- **Runtime.** tide_shader_read 13/14/15 map to TIDE_GPU_READ_COLOR/ARRAY/CUBE (`gpu_read_of`), so no backend, recorder
  packet or embed ABI changed; what differs is what no image reads as: `gpu_state.zero`, `zero_cube`, `zero_array`
  (zero bytes), made outside a pass where a shader with such a read is first seen. `tide_shader_read_loads` and
  `tide_shader_read_blends_numbers` (draw.h) replace `>= TIDE_SHADER_READ_NUMBER` tests. Mipmaps use the existing
  on-demand path (`prepare_mipmaps`, a target's `mipped[image]` against `drawn`).
- **Guards for what tidec can't follow** (code review, same day). `texture_to_read_as` gives no image to any read
  through a filtering sampler whose target image or volume is R32f/Rgba32f/R32i (a format picked as the game runs,
  a `?:` of two targets, a `mut` parameter, C): before, a Color read could bind r32sint/r32float to a filterable
  float binding (WebGPU validation error each draw, undefined on Vulkan). `image_destination_fits` (compute.inc)
  sends a kernel's stores to `empty_image_destination` when the held image's format isn't the pipeline's
  `image_formats[k]` (a `mut Texture3D` parameter re-made as R32i compiles, and did bind an R32_SINT view to an
  rgba8 storage binding), once with a line on stderr. A written target's image is now bound by its own id.
- **Backends.** Vulkan: `vk_cube` and the face/layer targets take every `color_format` (`color_of` is its inverse);
  `vk_mipmaps` already blits any format with linear blit support, which Vulkan requires of these five. WebGPU:
  `gpu_cube`/`gpu_array` take `attachmentFormats[3..6]`; `mipPipeline(format)` already keys by format. Unity:
  `TideRender.Target` gives the four compact formats a mip chain (`held <= 6`); cubes and arrays already used
  `AttachmentFormat`. gpu_none and the recorder carry the values unchanged.
- **Two compiler fixes met on the way.** `st_steps` re-walked a short-circuit's condition in its other arm, writing
  that condition's own arms again in a scope without their variables (`a && b && c` with SampleLevel operands failed
  SPIR-V translation: "isn't declared"). `check_image_clear` checked each Clear argument twice (`expr.args_checked`).
  A typed Sample's wrap now goes through `sampling_of` like every other (no `[Wrap]` can be beside `[Raw(T)]` on a
  Texture field today, pinned by errors/number_wrap_role).
- **Tests.** compiler/tests/shaders/number_filters (WGSL golden, spirv-val, FXC): every filter, level, gradients,
  helper and direct-source forms, typed arrays and cubes, numeric draws and clears into layers and faces, a
  three-term chain of level reads. 22 diagnostics in compiler/tests/errors/number_* (new and changed), one a
  message. `draw_number_cubes_and_arrays_hold_blendable_formats_only` (tests/test_draw.c). LSP:
  `lsp_numeric_images_and_outputs`, `lsp_numeric_image_helpers`. Pixels: platform/tests/images.tide/.c, 14 new probes
  (R8/R16f/Rg16f/Rgba16f midpoints, mip level 3 as the mean of what was drawn, level 1 halves, gradients, zeros for
  no image/array/cube, an R16f array layer cleared, blended and at a level, an Rg8 layer, R16f and Rg16f cube
  faces, a Color read handed an integer image, a kernel whose volume was re-made as R32i).
- **Validation** (Windows, Clang 23, Node 24.22.1, hidden): sanitize `tide_platform_images` on Vulkan and web-debug
  on software WebGPU with device loss pass; sanitize `tide_platform_targets`, `_shaders`, `_compute` pass; all
  206 shader golden/SPIR-V/FXC checks, the focused diagnostics, `tidec_e2e_number_images`/`_cubemaps`, draw units
  125/125 and `tidels_tests` pass. Unity: the C# change is one condition and was not compiled or replayed in an
  editor for this change; Metal, Android and real-GPU web lanes were not run.
- **Not built, and why.** (Since built, the section above this one: R32f/Rgba32f blending by exact loads, 32-bit layers
  and faces, blended typed Texture3D reads.) (Mipmaps of a multisampled target on WebGPU are built since, the top of this
  note; Unity's replay of them waits on the next Unity pass.) Left out by the owner: a blend per output, a chosen mip level as a destination,
  frag-drawn volume slices, an sRGB target format; R8/Rg8/R16f/Rg16f kernel storage isn't portable on WebGPU.

## Numeric image language surface and clears (2026-11-14)

Completes the owner-approved feature recorded in "Numeric attachments: backend foundation" below. The language
now exposes R8/Rg8/R16f/Rg16f/R32f/Rgba32f/R32i, beside Rgba8/Depth/Rgba16f, in both RenderTarget constructors.
The output/format table and every implementation pick are Provisional in docs/spec.md, Views and drawing;
owner decisions retain their 2026-11-13 quote there. User pages: game/render-targets.md and game/shaders.md.

- The old three-bit format-per-image layout remains valid for every legacy-only target. Numeric formats use
  the unused tags of that same 16-bit space: 31 old format codes times 16 image/sample combinations reserve
  496 tags. A dense numeric catalogue has 7,381 single-sample format combinations and three banks of 1,555
  compact-format combinations for 2/4/8 samples, each with four image selectors. Rank/unrank skips the reserved
  tags. All 48,184 payloads fit without widening pointers, holds, state alignment or public resource values.
  Exhaustive unit checks pin every payload and every legacy tag; reload tests pin mixed formats, samples,
  all image aliases and byte limits. Widening four format lanes to four bits would have required 1 MiB alignment;
  a resource registry or a changed public value would have introduced a broader lifetime/ABI change.
- R8/Rg8/R16f/Rg16f and numeric float4 in Rgba16f resolve MSAA by mean. R32f/Rgba32f/R32i use one sample:
  optional float32 resolves and integer sample-zero resolves were rejected in favor of baseline portability.
- target.Clear(v0, ..., vN) records one value per image in format order, with exact int or float bits. Values
  are Color/float/float2/float4/int; Color alone preserves the old broadcast on legacy targets. Numeric values
  neither clamp to one nor premultiply; normalized attachments quantize on storage. Color values premultiply.
  Tint/blend/transforms do not affect clears, clips do, and depth/stencil reset. Sixteen precompiled WGSL/SPIR-V/
  HLSL clear variants cover the possible integer attachment masks without runtime shader translation.
  ClearDepth uses an internal reset-without-color depth mode on numeric targets, retaining every image's bits.
- Typed numeric reads use declared Raw(T) Texture/Texture3D roles. (Superseded the same day, top of this note: the
  compact float formats blend, have mipmaps and make array layers and cube faces.) The shader
  implementation and storage-image/volume connection are in shaders.md and compute.md.

Validation after merging origin/dev's reversed-depth, depth bias, WriteDepth, SampleLevel and Color readback:
Windows Clang 23.1.2 ASan draw group 134/134 and the corresponding Node 24.22.1 WASI group 134/134 pass.
Native and WASI E2Es pass for image_clears, number_images, render_colors, render_samples and texture3d.
Native Vulkan target, generated images, compute (push descriptors and descriptor sets), embed, shaders,
readback, draw (both descriptor paths) and prewarm checks pass. Software WebGPU targets, images, compute,
draw, prewarm and readback pass, including device loss; target clears also pass forced forward-depth fallback
on native and web. The generated mixed MRT witness pins unchanged Color tint/alpha/blend beside untouched
numeric float4/int replacement. Exact backend probes serialize all 32 stored bits through opaque RGB, so a
browser canvas's opaque alpha cannot hide the high byte. Typed clear probes compare every float/int bit,
including -2147483647, values past one, free fourth channels, clips and ClearDepth preservation.

All 198 native WGSL golden/SPIR-V/FXC shader checks pass, along with numeric diagnostics and generated-C
metadata checks (shaders.md); no pre-existing shader expectation changed for this feature. The web configuration's
161 shader/SPIR-V/numeric-diagnostic checks and minifier sweep pass. The five added clear diagnostics pass on
both configurations. Renderer clear variants pass spirv-val and FXC /WX; CLI Unity shader catalogue checks pass.
Editor LSP 278/278, grammar 383/383 and docs production build pass. Unity actual replay results and the few
unavailable hardware platforms are recorded in unity.md. No CI was manually dispatched.

# Readback of every image (2026-11-14)

The owner asked for the gaps of "Owned pixel readback" (below) to be closed: "make sure everything we can add or
validate we do". Rules and the alternatives weighed are Provisional in docs/spec.md (which image a read is of, and
as what); the user page is docs/game/render-targets.md, Reading pixels in local code. Where this and the section
below differ (no depth, face, layer or numeric read; one pipeline), this is what's built.

- **Language.** `await target.Read()`, `Read(image)`, `Read<T>()`, `Read<T>(image)` (T: Color, float, float2,
  float4, int; `image` a constant int 0..3) and `await target.ReadDepth()`, each a failable owned `Grid2` of that
  cell. `target.colors.count` is an int. Cube faces and array layers are RenderTargets already, so
  `cube.faces[i].Read()`/`ReadDepth()` and `array.layers[i]...` are the same calls. The window stays a compile error;
  the spec says why it isn't the same path (its image is an attachment-only swapchain/canvas image, Unity's under
  Unity).
- **Checker** (`check.c`, the RenderTarget method branch): sets `expr.read_kind` (tide_target_read: 0 Color, 1
  float, 2 float2, 3 float4, 4 int, 5 depth) and `expr.read_image`. The `<T>` on a method named Read/ReadDepth is
  left for the receiver's type (`read_typed`), and refused there for any other receiver. `colors.count` grants the
  `colors` member its one-at-a-time exemption through `colors_indexed`, which `check_index` now restores instead of
  clearing (it broke `cube.faces[0].colors...` before).
- **Format check** (`check_target_read`, shader_kernel.inc, from `plan_shaders`): `image_target_format` of the
  read's target and image, the provenance numeric frag outputs and typed clears use. Known format: the read's kind
  must fit (Rgba16f fits Color and float4), else an error naming the format and the read that fits. Unknown format:
  an error for a number read, with the fix; allowed for a Color read, which the runtime checks. No image there
  (`Format.Depth`, an image past the constructor's) is an error. A face and a layer are checked the same way
  against their cube's or array's constructor format (`image_target_format` follows `faces[i]`/`layers[i]` since
  number images became layers and faces, top of this note), and take no image number.
  The type can't be inferred in the checker: a field's `image_assignments` are complete only after every body.
- **Engine** (`draw.h`, `gpu_list.c`): `tide_render_target_read_begin_as(target, image, kind)`;
  `tide_render_target_read_begin` is image 0 as Color. `tide_gpu_read_request` gained `image` and `kind` in what was
  padding (its size and the other fields' places are unchanged, so no embed ABI change: nothing but the engine and
  the renderer reads them). A request is refused as it's asked (`target_read_names`): no target, the window, a
  volume, a cube or array without a face/layer, a dead target, an image it hasn't got, a kind that isn't the
  image's (`tide_target_read_fits`), depth of a borrowed target. Poll gives `stride` by kind
  (`tide_target_read_words`: 4, 1, 2, 4, 1, 1) and `step`, the words from a pixel to the next as the renderer
  wrote them (what `bytes` holds over the pixel count: `stride`, or 4 for a face's or layer's numbers), and fails
  a result whose pixels hold fewer than `stride`. `tide_render_target_read_grid` makes a grid only where the
  shape's cell is `stride * 4` bytes, of two dimensions, `step >= stride` and `count` is `width * height`; it
  copies each pixel's first `stride` words. `tide_render_target_color_count(id)` is `colors.count`.
- **Renderer** (`platform.c` `target_read_prepare`, `compute.inc`): eleven kernels in `cull.tide`, a pipeline each
  made on first use (`pixel_read_pipelines`): ReadTargetPixels (Color, as before), ReadTargetFloat/Float2/Float4
  and ReadTargetInt (typed `[Raw(T)]` textures: `textureLoad`, no sampler, no alpha), ReadTargetDepth
  (`DepthTexture.Sample`, so `tide_depth` turns reversed storage forward), ReadTargetFace (the cube, by the
  direction through each pixel's middle), ReadTargetLayer and ReadTargetLayerDepth (the array and its depth array,
  by layer), and ReadTargetFaceNumbers and ReadTargetLayerNumbers (`[Raw(float4)]` cube and array, Point: an R8,
  Rg8, R16f, Rg16f or Rgba16f face's or layer's four channels as stored, of which the grid keeps its type's; one
  kernel each rather than one a type, for 4 words a pixel while pending). Ints go to a range of the ints pool, everything else the words pool. The request is checked again
  against what the target holds now. A multisampled target's depth is resolved first by `resolve_target_depth`,
  factored out of `prepare_source_target` (same bookkeeping and memory accounting). No gpu.h function, recorder
  opcode or tide.js change: Vulkan, WebGPU, the recorder and none run them through compute, images and buffer reads.
- **Face directions** (gpu.h's cube convention; `p` is the pixel's middle in -1..1, x right, y down): +x
  `(1, -p.y, -p.x)`, -x `(-1, -p.y, p.x)`, +y `(p.x, 1, p.y)`, -y `(p.x, -1, -p.y)`, +z `(p.x, -p.y, 1)`, -z
  `(-p.x, -p.y, -1)`. The fixture pins faces 0, 3 and 5 with an asymmetric top row.
- **Unity's catalogue.** `platform/src/read_pixels_wgsl.h` now holds all eleven kernels' WGSL, written by
  `node compiler/tests/read_target_builtin.mjs <tidec> <repo> --write` from `tidec --kernels`, and
  `tidec_read_target_builtin` compares it byte for byte (it was a substring check, which had let the checked-in
  ReadTargetPixels go stale when kernels gained the `tide_reversed_depth` preamble: a catalogue entry is found by
  a key of its exact WGSL on the web and of its SPIR-V elsewhere, so a copy that isn't what the renderer hands the
  recorder may not be found). Regenerate it whenever a ReadTarget kernel or the
  kernel preamble changes. shaders.h lists the eleven (`target_pixels` .. `target_layer_numbers`); the CLI's
  `unity_shaders_written` counts 17 computes.
- **Limits** are unchanged: 64 slots shared with list reads, 4,000,000 pixels, 128 MiB outstanding (now counted
  by each kind's words).
- **Tests.**
  - `tidec_e2e_target_reads`: every form's request (image, kind, the face/layer in its id), a stand-in renderer's
    answer, and the typed grid it becomes, word for word; then the engine alone: each kind against each format,
    images out of range (negative, INT32_MAX), the window, no target, a cube without a face, a face past the
    sixth, a disposed target, a result shorter than its pixels, sizes that are negative or overflow, a grid of
    another cell size; an R16f face and an Rg8 layer answered four words a pixel, of which the grids keep one
    and two.
  - Errors `read_target_*`, `read_depth_*`, `read_type_other_receiver`, `number_read_cpu`,
    `render_target_colors_count_frag` (23 new or changed; `render_target_colors_count` is gone, as it compiles).
  - `tide_platform_readback` and `_depth_fallback`, native Vulkan (sanitize, validation layer) and WebGPU
    (web-debug): the earlier 4x3 MSAA and HDR reads at frame 5; four images of one target (Rgba8, R32f, Rg16f,
    R32i) from typed clears, whole and clipped to one pixel, bit for bit, -2147483647 and 16777217 among the ints;
    R8/Rg8 bytes; R16f; Rgba32f with 1e9 and 1e-6; an Rgba16f image's stored (premultiplied) numbers through
    `Read<float4>`; depth a frag wrote (0.25, 0.75, 0.125, 0.5, 0.375, 0.625) beside the far limit for a target,
    a 4-sample target, a target of depth alone, a face, a color array's layer and a depth array's layer (exact
    under float storage, asserted on Windows); faces and layers by color and orientation; R16f cube faces
    (`Read<float>`, each face its own height and a clipped pixel), an Rg8 layer (`Read<float2>`) and an Rgba16f
    layer both as `Read<float4>` and as a Color; a layer past the array's and a disposed target failing;
    `colors.count`.
  - `lsp_render_target_formats_and_depth`: completions and signature help for the reads, a clean document.
- **Not run:** Unity's replay of the ten new kernels (their HLSL compiles under fxc cs_5_0 with Unity's
  warnings, `tide_hlsl_shaders_valid`, and they use no new packet); Android, where the same fixture is the app
  `tide_platform_readback_app` on CI's emulator (not run here; its exact-depth assertion is Windows' alone, as a
  device chooses its depth storage); Metal; the optional real-GPU web lanes.

# Owned pixel readback (2026-11-14)

Correction (review, 2026-11-14): `await target.Read() is Grid2<Color> pixels` names a scratch grid, and a task's
frame carried it across a wait as a raw handle (`task_value_handles` has no grid case), so a read or write of
`pixels` after a later `await` went through reused scratch memory. `check_await` now refuses a wait while a grid
local is in scope (`errors/read_target_kept_across_wait.tide`); the fixture's own shape, copy then wait, is
unchanged. With no GPU (`gpu_none`) a read still succeeds with zeroes, as a list's read does there.

Owner-approved 2026-11-13: await target.Read gives an owned Grid2<Color> fails IOError in local async code.
Picks and alternatives are Provisional in spec.md. It shares the 64 bounded read tickets and gpu.h asynchronous
buffer_read/done/wait operations with list reads. ReadTargetPixels, an internal Tide kernel, samples exact pixel
centers with Point, unpremultiplies through ordinary SampleLevel, and writes a temporary pool range. Resolved
color image zero is read; RGBA16f retains HDR. Checked dimensions cap an image at 4,000,000 pixels and outstanding
CPU pixel data at 128 MiB. Ranges return after GPU completion; canceled slots use serials, with no per-read
GPUList tombstones. Generated C builds an owned scratch grid through its generated shape and checked cache.
Grid is-pattern unwrapping and fresh await/default results complete the existing ownership API.

Pending captures fail before build resources are freed; begun copies own their GPU data. Device reset fails
captures already begun or prepared. Empty/disposed/depth, window and face/layer targets fail without invalid
bindings. Read explicitly admits Rgba8/Rgba16f as Color; other numeric formats give format-specific guidance
to typed Sample or a kernel writing a GPU list. Rgba16f Read unpremultiplies as Color even if a numeric frag
wrote the image; `[Raw(float4)]` GPU sampling preserves all four independent channels. Unity's generated shader catalogue includes read_pixels_wgsl.h, copied
from --kernels and checked against the actual Tide source by tidec_read_target_builtin. CLI tests pin its
compute asset. Embed ABI 22 covers the shared read table's target/dimension fields. Recorder and none reuse
compute and buffer-read operations; no new GPU function or readback opcode.

Typed numeric reads return the declaration's numeric type through Sample; blended reads, SampleLevel and
SampleGrad follow the top of this note. No numeric CPU Grid2 conversion is implied by the Color
readback API.

Android (correction, 2026-11-14): `tide_platform_readback` was registered there too, as a program started
through adb. It opens a window, which on Android only an app has, so it waited for good and CTest's timeout
didn't end it: the Android job hung on every Build run from a5b50cd74 on. On Android the same game and host
are now an app, `tide_platform_readback_app` (`tide_android_app_test ... GPU`, as the shaders' checks):
on this machine's emulator (`-gpu lavapipe`) it printed "ok: await target.Read owns the exact drawn 4x3
colors at frame 5" on Vulkan and ended with 0 in 2 s.

Validation: native ASan Vulkan and web-debug SwiftShader WebGPU readback fixtures pin a 4x3 MSAA-resolved
image, prior-frame ownership, row order, straight alpha, HDR 2.0 and exact frame-5 completion of sequential reads.
Existing GPU-list kernel fixtures pass on both; native E2E compute passes. Draw units 130/130, shader LSP 11/11,
shared grammar 381/381, web runtime 15/15, CLI Unity shader writing and embedded recorder tests pass. All 207
selected compiler/shader/diagnostic/GPU checks pass after reversed-depth integration (WGSL goldens, SPIR-V and FXC included). Unity's added real
pixel-read kernel replay validates every float word: ComputeReplayTests 11/11 each on D3D11, D3D12, Vulkan and
OpenGL Core, Unity 6000.6.0f1 hidden batch with no audio. Docs build passes. Native Vulkan and WebGPU shader pixels also pass the forced forward-depth fallback; Metal needs a Mac.


## Float depth storage and forward public reads (2026-11-14)

Targets, depth-only images, depth arrays, cube-face attachments and MSAA/resolved depth now share the device's
selected depth convention. Vulkan D32_SFLOAT_S8_UINT/WebGPU depth32float-stencil8 store reversed values; legacy
D24/depth24plus store forward values. ClearDepth and clipped/general color clears reset to the selected far
value. Public depth reads and Compare references remain forward. Sample-zero resolving preserves raw storage;
farthest resolving selects MIN for reverse, MAX for forward. Device recovery renegotiates the WebGPU feature
and recreates pipelines, samplers and images. Forced fallback tests use TIDE_TEST_DEPTH_FALLBACK=1 (Vulkan)
and the test shell's depth-fallback query (WebGPU); the convention fixture asserts actual 4-byte fallback
storage and actual requested 1/4 sample counts, then repeats after web device loss.

Resource accounting includes 8-byte combined float depth/stencil samples and resolves, versus 4-byte legacy
samples. The existing target-memory test follows the backend's selected count; its pixel assertions were
retained. Hand-written gpu.h fixtures now declare the same private depth helper as production shaders.
Validation and remaining external hardware coverage: depth-precision-production.md.

# Render targets and cubemaps

OpenGL 3.3, OpenGL ES 3 and WebGL 2 were removed 2026-11-05 (`notes/rendering.md`, GL backends removed): what this file says of OpenGL, WebGL, GLSL, `gpu_gl.c`, `gl.h`, `*_opengl` tests or `_d3d11`/WebGL runs is history; Vulkan and WebGPU are the backends, and every web test runs on WebGPU.

Full combined native/web suite results: `notes/rendering.md`, Gap integration validation.

## Numeric attachments: original backend foundation (2026-11-14)

The owner approved numeric frag outputs, seven formats, typed Sample and one clear value per image on
2026-11-13: "these make sense, lets do all of these". The original foundation exposed the backend catalogue;
the language, runtime and editor connections are now built, as described at the top of this note. The
following records the foundation's original scope and validation.

- `tide_gpu_color` appends R8, RG8, R16F, RG16F, R32F, RGBA32F and R32I as values 3 through 9. Existing
  NONE/BYTES/FLOAT values and four-bit attachment packing stay unchanged. Vulkan and WebGPU map these to
  their matching attachment formats; Unity replay maps them to explicit GraphicsFormats. The recorder
  already carries the packed format unchanged. No generated-C/runtime boundary or shader generation changes.
- `tide_gpu_color_bytes` centralizes 1/2/4/8/16-byte storage counts; renderer accounting uses it. Invalid
  attachment kinds yield no storage and cannot index beyond a table.
- Provisional: R32F, RGBA32F and R32I use one sample. Vulkan, WebGPU's C bridge, recorder and none agree.
  Optional float32 multisampling and integer sample-zero resolve were considered; the baseline portable
  attachment path is chosen instead. The language now diagnoses unsupported sample counts too.
- `check_number_attachments` in the existing platform target fixture checks every appended kind's packed
  round trip, byte count, single-sample restriction, allocation and a draw (negative signed integer or a
  float vector with a fractional fourth component and a component past one). It does not read these numeric
  images back and therefore does not establish exact pixel preservation.
- Validation: Windows Clang 23 ASan `tide_platform_targets` passes on Vulkan; web-debug target builds and
  its CTest passes on software WebGPU (muted headless browser). All 174 existing shader golden, SPIR-V
  validation and HLSL/FXC CTests pass unchanged. Docs production build passes. Unity's C#
  changes have not been compiled in a Unity editor. Release web runtime regenerated with Node 24.22.1.
- Integration: merged origin/dev's `this.front` work without conflicts. Rebuilt native and web targets and shaders;
  both platform CTests pass on Vulkan ASan and software WebGPU. The new triangle-side golden, spirv-val and
  FXC checks also pass.
- Follow-up integration adds managed format encoding, language members, typed outputs and matching, typed
  Sample, per-image clears, storage/volume connections, editor support and exact numeric pixel witnesses.

## Cached images detect discarded contents (2026-11-02)

Owner-approved per-target invalidation query: `target.contentRevision`, a
read-only local `long`, gives a positive token shared by aliases, or zero for
default/disposed/borrowed targets and the window. Cache the token with a baked
image and rebake when it differs. Clear and normal drawing do not change it.

`tide_render_target_state` holds the token and last observed device, physical
extent and draw epoch. The value passed through local state remains 24 bytes.
Create, explicit resize and recreation allocate a token from a checked positive
64-bit counter; native adoption invalidates the adopted target conservatively.
The standard host supplies live metadata through `draw.target_content`; generated
frame entry installs/restores its thread-local scope, including draw epoch.
The query observes device loss/recovery and automatic physical resizing before
the first drawing submission. Renderer target/cube allocations use the same
observer, so size changes away and back between queries cannot hide a discard.
The first observation after creation/explicit resize initializes metadata without
a second invalidation. Hosts providing their own frames should supply both the
target-content callback and draw epoch; absent a callback, explicit lifetime
operations and renderer observations still work.

Web saved host state carries the revision counter high-water and restores it
before generated resource rebinding. Reused WASM addresses and per-module counter
resets cannot reproduce a cached token. Its host-state magic changed, so reload
from a build with the previous host header starts afresh; subsequent reloads keep
local state and invalidate targets. Saved game/world formats are unchanged.

Compiler checking keeps metadata reads ordered and forbids assignment, GPU and
parallel-step reads; the shared shader-preparation optimization excludes such
queries because the host callback may have effects. Editor completion and type
hover expose `long`. Tests cover aliases, clear stability, explicit/automatic
resize, resize-away/back without queries, device generation and draw epoch,
high-water restoration and rebind; the WebGPU target test checks the first
post-device-loss query before rendering. Native ASan and web-release lifecycle,
sample/rebind and three diagnostic CTests pass, along with the focused runtime
revision test on native/WASI and native LSP completion/hover. Target GPU tests
pass on Vulkan, OpenGL, WebGL and WebGPU, including allocation-away/back and
pre-render device-recovery queries. The authored expression-order regression
reads the revision before a later argument resizes the alias, on native/WASI.

Embedded frames use the recording backend's current epoch and target limit,
even before the first render, including the time-travel frame path. Its physical
extent agrees with actual recording allocations; the native ASan embed regression
checks reset before rendering, and no extra invalidation during the next render.
The real web reload fixture replaces the same WASM module three times through
the production page and host save/resume path. `tide_platform_reload_revision`
passes on headless WebGL: each fresh instance retains the cached long and bake
counter, sees a different positive revision, rebakes once, and preserves the
target alias. This tests saved-header high-water transfer across actual WASM
counter resets, not a mock of the reload exports.

## Native single-output cutout alpha (2026-11-02)

The Mount & Rifle pale-rim report exposed coverage stored twice: a covered
sample had opaque RGB but fractional alpha, then resolve averaged both and
sampling divided RGB by that undersized alpha. Desktop OpenGL now enables
`GL_SAMPLE_ALPHA_TO_ONE` with alpha-to-coverage for single-output shaders,
resetting it at every pass and when another pipeline is used. Vulkan requests
the optional `alphaToOne` feature only where supported and enables it on the
same pipelines. Hardware coverage selection and shader RGB are unchanged;
the selected samples store alpha one. No generated-C/runtime API changed.

This correction is deliberately limited. Alpha-to-one also replaces secondary
outputs' alpha (observed on both local backends), so multi-output shaders retain
the old path rather than corrupting their other outputs. WebGL/OpenGL ES lack
this desktop state; WebGPU and Vulkan devices without `alphaToOne` retain the
old path too. Those cases still need separate coverage and color outputs or a
different implementation. Premultiplying RGB by coverage again is not a fix:
it would attenuate color twice. The game checkout and pin were not changed.

`platform/tests/shaders` pass 24 now draws HDR coverage into opaque and
transparent four-sample targets, resolves and samples their color and alpha,
and checks a multi-output control's secondary alpha. The corrected native
Vulkan and OpenGL ASan CTests pass, including repeated and prewarmed checks.
Disabling only alpha-to-one makes precisely the new HDR checks fail on both:
opaque probe `EBCF76FF` instead of `BFFF60FF`, transparent `FF8FAAFF` instead
of `FFBF7FFF`; the secondary-output control stays `7F3040FF`. Unsupported
backends explicitly report that the stored-alpha correction is unavailable;
their existing coverage tests are retained. Other GPU vendors, macOS and
Android hardware have not been exercised.

The web-release shader fixture also builds and passes on WebGL, ANGLE D3D11
and WebGPU (31.05/45.59/29.51 s), validating the retained fallback and new
probe shaders, not claiming the stored-alpha defect fixed on those backends.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Rendering and platform

- **Cut-outs and derivatives: `this.Cutout(alpha)`, `this.Coverage(share)`, `this.Ddx/Ddy/Fwidth(v)`** (2026-10-31, for bannerlord-duel's foliage, cut with `this.Discard()` and aliasing in a 4-sample target. A `[Cutout]` attribute was built first and replaced the same day: the owner prefers code, and asked for derivatives and the raw share too. The rules are Claude's picks, Provisional in `docs/spec.md`, A target's samples, Cut-outs and How fast a value changes). `docs/game/shaders.md` (How fast a value changes, Cut-outs) is the users' page. What's built, and what code relies on:
  - **tidec, checking:** `check_pixel_method` (`compiler/src/check.c`) takes the pixel's new methods: `CALL_DERIVATIVE` (a float, a float vector or a `Color`, giving its type) and `CALL_COVER`, a statement of its own (not in `?:` or `&&`), one float. A frag's first `CALL_COVER` sets `decl.cutout`, and `decl.raw_coverage` for `Coverage`; the other kind in the same frag is an error. Frag bodies are checked before their shaders are written. `[Cutout]` is an error that names `this.Cutout`.
  - **tidec, shaders** (`compiler/src/shader.c`): **`ex_derivative`** lifts `v` as `ex_sample` lifts a Trilinear uv: `let tide_d<n> = fwidth(v)` (or `dpdx`/`dpdy`, GLSL's `dFdx`/`dFdy`) into `emit.grads`, at the frag's function's start, and the call is `tide_d<n>`. A `v` that isn't `liftable` is refused there (`derivative_changes`). Written while lifting (a derivative of a derivative), it's asked in place. `liftable` takes `CALL_DERIVATIVE` and now any `Sample` filter, Trilinear and a `Texture`'s unknown one too: `ex_sample` while lifting reads those with `textureSample`/`texture`, the GPU's own level, in uniform control flow. No golden moved. **GLSL's `Ddy` is `(tide_gpu_height > 0 ? 1.0 : -1.0) * dFdy(v)`**: `gpu_gl.c` draws a target's pass upside down and hands its frags their rows back (`compile_begin`), so `dFdy` goes down in a target and up in the window. `tide_gpu_height` is that backend's uniform, now part of `gpu.h`'s GLSL contract. **A cut-out:** `tide_cover` (a GLSL global, a WGSL `var<private>`) is 1 as `put_fragment_main` starts, `this.Cutout`/`Coverage` assign it, and the first output's finish is: its tint, `tide_share = tide_cover * tint.a`, sharpened for `Cutout` (`(s - 0.5) / max(fwidth(s), 0.0001) + 0.5`), then where `tide_screen.w < 0` a discard under 0.5 and 1, elsewhere `clamp(s, 0, 1)`, as the color's alpha; no premultiply and no multiply's white. That fwidth is in the fragment entry, after the frag's call: WebGPU took it after a conditional `discard` in the frag (pass 24 has one). Other outputs are finished as ever. Generated C says `.cutout = true` in the `tide_shader`; every other shader and game is byte for byte what it was.
  - **`tide_shader.cutout`** (`tide/draw.h`, in the padding after `blit`: the struct's size is the same) and `tide_screen.w`'s third meaning: -1 for a cut-out into a target of one sample.
  - **The renderer** (`platform/src/platform.c`): `covers_by_samples(into)` (colors and more than one sample, as the device gave them) decides both `tide_gpu_pipeline_desc.alpha_to_coverage` in `pipeline_for` and `screen[3] = -1` in `draw_shaded`, from the same `frame_target.into`, so they never disagree. A cut-out's `TIDE_GPU_BLEND_ALPHA` becomes `REPLACE` in its pipeline (the key keeps the draw's blend: the shader id tells the pipelines apart). No key gained a field.
  - **`gpu.h`: `alpha_to_coverage`** on a pipeline. OpenGL/WebGL: `GL_SAMPLE_ALPHA_TO_COVERAGE` enabled or disabled in `gl_use`, disabled at `gl_begin`. WebGPU: `multisample.alphaToCoverageEnabled` (a new last argument of `gpu_pipeline_begin`; `tide.release.js` regenerated). Vulkan: `alphaToCoverageEnable` where the pipeline has samples. Recorded pipelines (Unity) carry it after `outputs`: **`TIDE_EMBED_ABI` and `TideLibrary.Abi` are 9**; `TideRender.cs` sets `_TideAlphaToMask`, which the ShaderLab `tide unity` writes reads as `AlphaToMask [_TideAlphaToMask]` (`tide_hlsl_unity_shader`).
  - **Tests:** `compiler/tests/shaders/cutout` and `derivatives` pin GLSL and WGSL (and their SPIR-V and HLSL validate where spirv-val and fxc are); `compiler/tests/errors/cutout_*` and `derivative_*`; `lsp_shaders_complete` offers the methods; pass 24 of `platform/tests/shaders.tide` with `run_cut_checks` in `shaders.c`: a sheet cut out by an alpha climbing a 64th a pixel, into 4 samples (the edge pixel in between, the ones past it whole where the alpha is 0.53), into one sample (cut, and whole), with its bottom rows discarded; a sheet covering three quarters as given (0xBF into 4 samples, whole into one); a card into a target of depth alone (depth on one side only); and `Ddx(position.x)`, `Ddy(uv.y) * 64` and `Fwidth(uv.x) * 32` as (1, 1, 0.5) drawn into the window's pass and into a target, on Vulkan, OpenGL 3.3, WebGL 2, ANGLE's D3D11 and WebGPU. `embed_render_pipeline_carries_actual_texture_reads` reads the new word.
  - **Not run:** the window's own OpenGL pass, `tide_gpu_height` 0 (reading pixels back draws through a target, so `Ddy`'s sign there is by construction); Unity (the owner's TideUnity project tests it; the Sample's checked-in shaders lack `_TideAlphaToMask` until its next `tide unity` build, which only means no alpha to coverage there); Android and OpenGL on a Mac.
  - **Not built:** a sample mask of the frag's own; fine and coarse derivatives; derivatives in a vert or a function.

- **A multisampled target's depth is read** (2026-10-13; the owner asked for the engine to be fixed where FOLDLINE wrote distances into a second float image because the read was refused; the rules are Claude's picks, Provisional in `docs/spec.md`, A target's samples). It replaces "A depth read of a multisampled target is refused, not resolved" below, which is kept as the record of what was built before. What's built, and what code relies on:
  - **`gpu.h`: `resolve_depth(target)`**, outside a pass. For a multisampled target it makes the depth texture the first time and fills it from the samples as they are; for any other target it does nothing. `target_depth` of a multisampled target is 0 until the first resolve, then that texture. A backend that gains multisampling has to give both.
  - **OpenGL** (`gl_resolve_depth` in `gpu_gl.c`): a `GL_DEPTH24_STENCIL8` texture in a framebuffer of its own with no color (`resolved_depth`), filled by `glBlitFramebuffer` with `GL_DEPTH_BUFFER_BIT` and `GL_NEAREST`, the scissor test off. No new GL function. Which sample a blit picks is the driver's.
  - **WebGPU** (`gpu_resolve_depth` in `tide.js`): WebGPU resolves no depth, so a pass over a one-sample `depth24plus-stencil8` texture runs a shader of the engine's that reads sample 0 (`texture_depth_multisampled_2d`, `textureLoad`) and writes `frag_depth`, with `depthCompare: 'always'`. The multisampled depth texture gained `TEXTURE_BINDING` and a depth-only view (`sampledDepth.only`). The pipeline is made once a device (`depthResolver`, reset in `gpuStart`).
  - **The renderer resolves only what's read, when it's stale** (`prepare_source_target` in `platform/src/platform.c`): a step that reads the depth of a target that asked for samples ends the pass, as a source target that has to be made does, and resolves where `gpu_target.resolved` isn't `drawn + 1`. So a target nothing reads the depth of has no texture and no pass, and one read twice with nothing drawn between resolves once. `unavailable_texture` no longer refuses the read.
  - **Memory:** the texture's 4 bytes a pixel are held from the first resolve (`gpu_held.resolved`), not in `target_bytes`, so what a target costs before its depth is read is what it was.
  - **A target of depth alone still has one sample.** The compile error and the run-time line now say why as it is: samples smooth colors, and it has none (`depth_alone_sampled` in `check.c`, `samples_of` in `engine/src/draw.c`).
  - **Tests:** `check_samples` in `platform/tests/targets.c` (no texture before a resolve, one after, a plain target's unchanged) and `check_render_samples` (the depth read through the renderer at 1, 4 and 8 samples: far where nothing drew, the mesh's where it is, and again after a nearer mesh is drawn in a later frame). Results are with the validation below the depth clear's in `notes/drawing.md`.
  - **Not built:** a mean or nearest of the samples; a multisampled target of depth alone; reading a cube face's depth.
- Before a web reload saves local state, generated resource rebinding resolves each drawn cubemap's target after adopting it (`gen_resource_fields`), just as it does a `RenderTarget`. Resizing changes the managed state, so every stored cube alias needs that live size before it is packed. `compiler/tests/e2e/cubemaps` checks a resized cube restored through a singleton, a nested struct, a retained face and a component's local field, keeping aliases, pixel scale and formats; an asset's named cube remains unchanged.
- Render targets are the exception: images the GPU alone holds, local to a machine. `RenderTarget(width, height)` makes one, `target.Resize(width, height)` changes its size and discards what's in it, `target.color` is its `Texture` and `target.depth` its `DepthTexture`, and `target.Dispose()` lets it go, explicitly. Copies share the image, and its color and depth stay from frame to frame until they're cleared. A target's own drawing methods (`target.Clear`, `target.Mesh` and the rest) draw into it; `Draw`'s own calls, the GUI and the stats overlay draw into `Draw.window`. `Draw.Blit(source[, textures...], destination[, blend], frag[, values])` runs a frag over the whole destination, replacing its color (or going over it as its own `blend` says) and keeping its depth whatever the camera, the clip and the other draw settings are, with the source as the frag's first read-only `Texture` parameter and the textures after it as its others, in order (since 2026-11-14: notes/shaders.md, Several textures a draw gives): a target's color or a `Grid2<Color>`, never the destination's own, which is an error. Tide code can't read a target's pixels back, so a hot reload and a lost device make its image again, empty. `tide_platform_targets` (`platform/tests/targets.c`) checks targets on each backend, at `gpu.h`. The renderer finds its image of a managed target by the address of the target's state, and reads that state (`tide_render_target_live`) at every draw until a list's epoch says the build changed: so targets' bookkeeping is only freed (`tide_render_targets_free`) once the lists that drew them are forgotten (`tide_draw_forget`), which is the order a hot reload has and every check of that test. One check there freed without forgetting (2026-10-07), and the renderer then read freed memory: its count of what it has on the GPU, which the next check pins, was off by those targets' bytes whenever the allocator gave a later target one of the freed addresses, one run in three on Windows and never on the web.
- A target's size is in logical pixels, like the window's, at the display's scale (agreed 2026-10-05; `docs/spec.md`, Views and drawing): drawing into one is the same as drawing into the window. `RenderTarget(width, height, pixelsPerUnit)` has pixels per unit of its own (`RenderTarget(320, 180, 1)` is exactly those pixels on any display), and `RenderTarget.Window()` and `RenderTarget.Window(factor)` are always the window's size, or that part of it: made again, empty, when the window's size or the display's scale changes, and never resized by the game. Nothing reads the display's scale (no `Screen.scale`). What the design left open is picked and Provisional there: sizes are floats, an image has the nearest whole number of pixels, `Resize` on a target that follows the window is a diagnostic, and an image past the GPU's largest texture keeps its shape with fewer pixels.
  - A size is a `tide_target_size` (`tide/draw.h`): a width and a height in logical pixels, `scale` (the image's pixels in one, 0 for the display's) and `window` (above 0: the part of the window's size it always is). A `tide_render_target` is an id and one, a managed target's state has one, and the draw list records one with each destination and each target it samples (`tide_draw_target_data.size`, `tide_draw_texture.target`), so a list says nothing in pixels. The engine only knows logical sizes: `tide_target_size_logical` is what the conversions and `MeasureText` use, with the window's size for a target that follows it.
  - The platform works a target's pixels out as a list is drawn (`target_extent` in `platform/src/platform.c`), under the draw's root (`draw_root`): what the list's window commands go to, with a size in logical pixels and one in its own. The root's pixels over its logical size are the display's scale for that draw: the window's, a headless run's `--scale`, or 1 for `tide_platform_read_pixels`. A target that follows the window has the root's own pixels times its factor, so `RenderTarget.Window()` is the window's pixels exactly. A destination's logical size and pixels are then all the renderer goes by (`draw_width`, `draw_pixel_width`, `pixel_scale`), the window's or a target's alike: text fitting, circles drawn as smooth shapes where the scale isn't 1, clips, and a shader's `tide_screen`.
  - The GPU's copy of a target is made again when its image comes to another number of pixels, as when its version changes (`target_for`, and `prepare_source_target` for one that's only sampled): that's all that happens when the window is resized or moves to another display. `tide_gpu.target_limit` is the most pixels a side an image has (OpenGL's largest texture, WebGPU's `maxTextureDimension2D`).
  - Text is measured where it's drawn: `tide_measure_fn` takes the target's `tide_target_size`, or NULL for the window, and `tide_platform_measure_text` works its pixels out as drawing does.
  - `tide_platform_read_pixels_as(list, width, height, scale, ...)` draws a list as a window of that size would on a display of that scale, and reads its pixels: how tests see a scaled display and a window that changes size on any machine and every backend, with no window resized. `tide_platform_targets` draws a pattern one real pixel wide into a screen target at scales from 1 to 3 and expects it back pixel for pixel, checks that a scene drawn into a screen target is the window's pixels (text, circles and smooth shapes), that a target with pixels of its own keeps them and its contents on any display, and that the others are made again when the scale or the window's size changes; `tide_platform_shaders` does it from Tide (`RenderTarget.Window`, `RenderTarget(4, 4, 1)`, a frag's `this.position`).
  - Hot reload keeps a target's size, its scale and whether it follows the window: they're in the `tide_render_target` values the local state holds, which `tide_render_target_recreate` makes a web build's targets from.
- **A target says what it holds when it's made, and a frag reads a target's depth** (built 2026-10-07; what's agreed is in `docs/spec.md`, Views and drawing, and the picks made to build it under its Provisional, Target formats and depth: `Format`, its members, where it's written, `DepthTexture` and its two reads' names and rules). What's built, and what code relies on:
  - **Tide:** `RenderTarget(width, height[, pixelsPerUnit][, format])` and `RenderTarget.Window([factor][, format])`, with the built-in enum `Format { Rgba8, Depth }`; `target.depth`, a `DepthTexture`, beside `target.color`; and in a frag or a vert, `depth.Sample(uv)`, a float, and `depth.Compare(uv, value)`, where the depth is a field of a component or a singleton the code takes, as a grid is. `docs/game/render-targets.md` (What a target holds) and `docs/game/shaders.md` (A target's depth, with a live shadow map: `docs/examples/shadows.tide`) are the users' pages.
  - **What a target holds is in its id, never its size:** a managed target's id is its state's address with `TIDE_RENDER_MANAGED`, and what it holds in the bits the address leaves free (`TIDE_RENDER_FORMAT`; `managed_id` in `engine/src/draw.c`). They were three, for one format, fourteen once targets held several color images, which is where their layout is said (below), and are sixteen since a target says its samples (below that). `tide_render_target_formats(id)` reads them (`tide_render_target_format(id)` is the first image's, or depth alone), and `tide_render_target_state_of` masks them off. It's there, and not a field of `tide_target_size`, because `tide_render_target` is 24 bytes in every game's local state that has one (`type_layout` in `compiler/src/types.c`), and a field more would have changed those games' generated headers. So everything that carries a target's id carries what it holds, with no field of its own: the draw list's destinations and textures, the platform's `target_for`, and the value another build is handed to make the target again (`tide_render_target_recreate`, which takes the format from the id it can't read as an address: a hot reload keeps it, natively and on the web). A borrowed target (an id of C's own, without the managed bit) holds color: C that wants depth alone makes a managed one (`tide_render_target_make_as` and the three beside it). A format that's none of the enum's is color.
  - **A target's depth is a texture of the draw list's, of a space of its own:** `TIDE_PIXELS_TARGET_DEPTH` beside `TIDE_PIXELS_TARGET` (`tide_draw_depth_source`, `tide_draw_target_depth_sized`; `tide_depth_ref` is Tide's `DepthTexture`, a target's reference and nothing else). A shader says how it reads each of its textures (`tide_shader.texture_reads`: a color, a depth, a depth compared), and the renderer only hands a texture to a read of its kind (`texture_to_read` in `platform/src/platform.c`): a depth that's read with none given, or with a color given, is the far one (`gpu_state.far`, a target of depth alone of one pixel, made the first time a list draws through a shader that reads a depth), and a color read that's given a depth is the white pixel.
  - **`gpu.h`:** `target(format, width, height)` with `TIDE_GPU_TARGET_COLOR` or `TIDE_GPU_TARGET_DEPTH` (two of the formats there are since: see A target holds several color images, below), `target_depth(target)` beside `target_texture(target, color)`, a pipeline's `reads` (a `tide_gpu_read` for each texture: `TIDE_GPU_READ_COLOR`, `_DEPTH`, `_COMPARE`) and `into`, the kind of target it draws into: a pipeline is for one kind. `sample` comes after `use`, as how a texture is sampled is the pipeline's to say where it's a depth.
  - **Every target's depth is a texture now, with 8 bits of stencil beside it** (which draws test and store since: see Stencil is a setting too, under the draw settings): OpenGL's is `GL_DEPTH24_STENCIL8`, attached at `GL_DEPTH_STENCIL_ATTACHMENT`, where it was a renderbuffer, so `gl.h` and `tide.js` have no renderbuffer functions any more; WebGPU's is `depth24plus-stencil8` for every target, the canvas's too (a pipeline names one depth format), sampled through a view of its depth alone. The window's own framebuffer on OpenGL has stencil too, asked for on each platform since the stencil card.
  - **A target of depth alone has no color attachment.** OpenGL 3.3 wants that said (`glDrawBuffers(GL_NONE)`, `glReadBuffer(GL_NONE)`, both in `gl.h` and `tide.js` now), and the same program draws into it. WebGPU's pass has no color attachment and its pipelines no color target (`fragment.targets: []`), which is what `into` is for: the fragment stage still runs, as it may discard, and what it returns goes nowhere. So the renderer has a second pipeline of what draws there, as it has one for each kind of target: the clears' (`clear_pipeline`), the 3D meshes' (`engine_pipeline` with `into` `TIDE_GPU_TARGET_DEPTH`, one for each side they leave out: a lamp's image is drawn from the side its target's `Cull` says), and a game shader's (`shader_pipeline.into`, part of its key, with its cull).
  - **Only what writes depth is drawn into one, or stores stencil** (`writes_depth`, `leaves_nothing` in `platform/src/platform.c`): a clear, a 3D mesh or a 3D shaded run under `TIDE_DEPTH_READ_WRITE`, and any step whose stencil operation isn't `Keep`. Every other step is passed by there, with nothing said: shapes, text, 2D meshes, blits, meshes that only read depth. A frag is never skipped where it could be (one that can't discard, into depth alone): a backend may, some day.
  - **How each backend reads a depth.** As a number: a `sampler2D` whose red is the depth (`uniform highp`, as a sampler's own precision is the lowest there is on OpenGL ES) through the nearest sampler, and `texture_depth_2d` with a `sampler` bound as non-filtering: a depth is only whole at its nearest pixel on OpenGL ES 3, WebGL 2 and WebGPU alike, which is why `Sample` takes no `Filter`. Compared: `sampler2DShadow` through a sampler object with `GL_COMPARE_REF_TO_TEXTURE` and `GL_LEQUAL`, linear; `texture_depth_2d` with a `sampler_comparison` (`less-equal`, linear) and `textureSampleCompareLevel`. tidec clamps what's compared to 0..1 itself (`ex_sample` in `compiler/src/shader.c`): past them, GPUs differ. Both are read at the one level, so in any branch and in a vert. WebGPU's bind group layouts are by how a pipeline reads each texture (`layoutFor` in `tide.js`), made the first time one reads so.
  - **Clip space, depth and rows are the same on each:** a depth is `(z / w + 1) / 2` of OpenGL's clip space on all three (WGSL's vertex stage already takes OpenGL's z to WebGPU's), and a target's depth is the same way up as its color, as OpenGL's target passes are flipped as they're drawn.
  - **A draw into a target that reads that target's depth is passed by,** as one that reads its color is (`samples_destination`, with the same line on stderr; `tide_draw_blit` refuses as it records), and so is a draw that samples the color of a target of depth alone (`unavailable_texture`).
  - **tidec:** `TY_DEPTH_TEXTURE` (`tide_depth_ref`, 24 bytes), which is a rendering resource wherever `Texture` is one (`holds_render_resource`, the reload's rebinding in `gen_resource_fields`); `check_depth_read` for the two reads; `shader_sample.read` and `shader_texture.read`, so a depth that's both sampled and compared is two of the shader's textures; `Format` is in generated C only in a game that names it, as `Depth` is (`enum_generated`), and a shader's `.texture_reads` only where a texture isn't a color. A game that uses none of it generates the C it did, byte for byte: old and new tidec over every game's source list, with `--layout` and without (237 games; only the three that use it differ: the shaders test, `render_depth` and the docs' `shadows`).
  - **Not built:** a `DepthTexture` as a draw's own source or a frag's own parameter (an error that says where it goes); skipping a frag that can't discard. A function's parameter is there since 2026-10-08: a function a frag or vert calls takes a `DepthTexture`, a `Texture` or a `Cubemap` and reads the one its call names (`notes/shaders.md`, A function samples the textures it takes).
  - **Tests:** `tide_platform_targets` (`platform/tests/targets.c`): at `gpu.h`, a target of depth alone drawn into column by column and read back as a number, compared at pixels and between them, kept and tested across passes, a color target's depth, a clear; and through the renderer, what draws into depth alone and what doesn't, a frag that discards there, the same way up as color, a target's own depth refused, the far limit where none is given, a screen target of depth alone at three scales, and a forgotten list. `tide_platform_shaders` (pass 7 of `shaders.tide`, `run_shadow_checks`): a shadow map written in Tide, casters through a slanted lamp's `float4x4.Ortho` camera, one through a frag that discards, a lit frag that compares, and the stored depth read as a number, at three display scales. Both on OpenGL 3.3, WebGL 2 and WebGPU, where the device is then lost and found. `draw_targets_say_what_they_hold_and_lend_their_depth` (`tests/test_draw.c`) for the list and the id; `compiler/tests/e2e/render_depth` for what tidec generates, a reload in the same memory and in other memory among it; `compiler/tests/shaders/depth` pins the GLSL and WGSL; `compiler/tests/errors/render_depth_*` and `render_target_format_*`, a file a diagnostic; `lsp_render_target_formats_and_depth`. Android: the apps of these tests (`tide_platform_targets_app`, `_shaders_app` and their 24-bit depth fallbacks) in CI's Android job (`notes/android-and-mingw.md`, Every window fixture an app). Not run: OpenGL on a Mac, which nothing runs.
- **A target holds several color images, a frag writes them all, and an image can be floats** (built 2026-10-07. The owner picked each of three, asked that day: a format for each color image after a target's size, four at the most; a frag's `out Color` parameters to them by their order, each image read as `target.colors[i]`, an output with no image in its place dropped and an image with no output left as it was; and float images wherever a GPU draws into them, with 4 bytes a pixel and a line that says so where one doesn't. The rest is Claude's picks, Provisional in `docs/spec.md`, Views and drawing, Several colors and float images). What's built, and what code relies on:
  - **Tide:** `RenderTarget(width, height[, pixelsPerUnit], format...)` and `RenderTarget.Window([factor], format...)`, a `Format` for each color image; `Format.Rgba16f`, the enum's third member, beside `Rgba8` and `Depth`; `target.colors[i]`, a `Texture`, with `target.color` the first; and a frag with up to four `out Color` parameters. `docs/game/render-targets.md` (What a target holds, Several colors) and `docs/game/shaders.md` (Several outputs) are the users' pages.
  - **What a target holds is still its id's, in more bits.** The low 14: 3 for each of four color images in order, 1 + its `tide_target_format` and 0 past the last, so none at all is a target of depth alone (`TIDE_RENDER_FORMAT`); then 2 that only a texture's reference has, for which image it names (`TIDE_RENDER_IMAGE`). Three bits a format, where two would hold the two there are, for the formats to come. So a target's state was on a boundary of 16 KiB (`TIDE_RENDER_BITS + 1`), and is on one of 64 KiB since its samples took two bits more (A target's samples, below): `make_target` allocates that much more than the state and keeps what it allocated in `tide_render_target_state.memory`, which is what's freed, by the build that allocated it or by one that adopted the state. `tide_render_target` is the 24 bytes it was, and a game's header says nothing of any of it. A test that makes a state by hand makes it on that boundary (`foreign_state` in `tests/test_draw.c`).
  - **A target's own id and an image's are told apart where it matters:** `tide_render_target_whole(id)` is the target's, which a destination, the renderer's image of a target (`find_target`, `target_for`) and a comparison of two targets go by (a blit that samples its destination, `samples_destination`), and `tide_render_target_image(id)` which image a texture's reference names. The draw list's textures keep the id as the reference has it, so a target's images are textures apart (`TIDE_PIXELS_TARGET`, by id). Both leave a borrowed id alone (an id of C's own, without the managed bit, which holds one color image): its bits are C's.
  - **An image a target hasn't got is still named** (`tide_render_target_color_at`: any of the four; past them, or of the window or a borrowed target, it's `TIDE_RENDER_INVALID`), and the renderer passes by a draw that samples it, with a line (`unavailable_texture`), as it does for the color of a target of depth alone: the target says what it has, and tidec can't follow a `Texture` to a target.
  - **A hot reload keeps all of it with no new generated code.** `tide_local_rebind_resources` is the C it was: for a `Texture` field, `tide_render_target_color(tide_render_target_recreate(ref.target))`. So `tide_render_target_recreate` makes the target from the formats in the id it's handed and gives back an id with that id's image bits, and `tide_render_target_color` of a value whose id names an image is that image again. A `RenderTarget` value's id never has them.
  - **C:** `tide_render_target_make_of(width, height, formats, count)` and the three beside it; the four `_as` there were are one format, as ever, `TIDE_TARGET_RGBA16F` among them. Among several, a format that's no color's (`TIDE_TARGET_DEPTH`, which tidec refuses where it's written, or a number that's none) is `TIDE_TARGET_RGBA8`, with a line for the first. `tide_shader.outputs` is how many colors a shader's fragment stage writes (`tide_shader_outputs`: one for 0, so a shader made by hand says nothing).
  - **Generated C is what it was, byte for byte, for a game that doesn't name `Format`** (old and new tidec over every game's source list, 239 of them, with `--layout` and without). The ones that do (the shaders test, `render_depth`, the docs' `shadows`, and Sumo since its shadow map) have the enum's third member in their header, and with it another `tide_game` hash, and nothing else. One format calls what it called; several are an array in the call, `(const tide_target_format[]){...}, n` (`gen_target_format` in `compiler/src/codegen.c`, by `expr.target_formats`); `target.colors[i]` is `tide_render_target_color_at`; and a shader says `.outputs` only when its frag has more than one.
  - **tidec:** `target_formats` (`compiler/src/check.c`) counts a constructor's trailing formats, and refuses a fifth and `Format.Depth` written among several. `target.colors` is only an index's object (`checker.colors_indexed`, as `device_whole_ok` is for a device's `touches`; `expr.target_colors`): alone, counted or looped over, it's an error that says to read one. `check_writable` refuses an assignment to `target.color`, `target.colors[i]` and `target.depth`: before, the first and the last compiled, to C that didn't. `check_shader_decl` counts a frag's `out Color`s (`decl.outputs`, `param.output`), and `compiler/src/assigned.c` names an output after the first where a path doesn't write it ("...without writing 'glow', its second output"): the first keeps the message it had.
  - **`compiler/src/shader.c`:** a frag with one output is written as it was, to the byte, so no shader's id moved (`compiler/tests/shaders/` but for the new `outputs`). With several, GLSL has `layout(location = k) out vec4 tide_pixel<k>` for each and WGSL a `tide_colors` struct its `fragment` returns, each a `tide_color<k>` the frag's function is handed in its parameters' order (`output_name`, `shader_outputs`), and each goes through what the draw finishes a color with: its tint, and what a draw that multiplies needs.
  - **`gpu.h`:** `tide_gpu_target_format` is a number, 4 bits for each color image in order (`tide_gpu_color`: bytes or floats) and 0 past the last, so `TIDE_GPU_TARGET_DEPTH` is 0 and `TIDE_GPU_TARGET_COLOR` 1, which is the window's kind too. **A pipeline description has to say `into`:** 0 is depth alone now, where it was color. One used in a pass on another kind of target stops the program on OpenGL too, naming it (`gl_use`), as WebGPU refuses it: OpenGL would have drawn all the same, and only the web's tests would have said (the stencil tests' own descriptions did, as the two were merged). `outputs` is how many colors its fragment shader writes (one for 0); `target_texture(target, color)` takes which image; `float_targets()` says whether a float image is one; `target_read` gives a target's first image where that's bytes, and zeros otherwise.
  - **A pipeline's outputs go to a target's images in order, and the rest of either is nobody's.** OpenGL: `gl_use` sets the bound target's draw buffers to its first images, as many as the pipeline has outputs, and `GL_NONE` for the others (`draw_into` in `gpu_gl.c`, only for a target of several, and only when it changes): an image with no output would get anything at all, and WebGL refuses the draw. A pass that starts cleared names them all first, as a clear reaches the buffers that are named. WebGPU: a pipeline's `fragment.targets` are the target's formats, with `writeMask: 0` past its outputs (`colorFormats` in `tide.js`: a change to `tide_gpu_color` goes there); a shader with more outputs than its pipeline has targets is fine by it, which the tests draw. One blend for all of a draw's outputs: OpenGL ES 3 and WebGL 2 have no other.
  - **A float image is `GL_RGBA16F` and `rgba16float`:** four floats of 16 bits, which every backend that has them draws into, blends and reads blended between pixels. OpenGL 3.3, OpenGL ES 3.2 and WebGPU have them; an older OpenGL ES 3 and WebGL 2 where the extension is there, which is nearly everywhere (99.7% of WebGL 2 by web3dsurvey, 2026-10). It adds no function: `tide.js` asks WebGL for `EXT_color_buffer_float`, or `EXT_color_buffer_half_float`, as it makes the context, and `gl_float_targets` finds out by making a framebuffer of one, once. Without it a float image is made as bytes, `gl_target_make` says so once on stderr, and nothing else differs. `tide_gpu_gl_no_floats` has OpenGL make targets that way while it's set, for tests.
  - **The renderer's pipelines are for a kind of target, which is what it holds** (`frame_target.into`, from `gpu_target_format`): the engine's own in `engine_made`, a set for each kind a list draws into (`pipelines_into`; the window's kind first), the ones that test or store stencil in their list by it too (`stencil_pipeline.into`), and a game shader's by `shader_pipeline.into`, where each had a flag for depth alone. The engine's own draw one color, into a target's first image. A clear of a target of several goes through a shader of four outputs (`clear_all_glsl_fragment`, `clear_all_wgsl` in `platform/src/shaders.h`), the same color into each.
  - **Not built:** a float of 32 bits and images of one or two channels (WebGPU neither blends nor filters 32-bit floats without features a device may lack); an output of another type than `Color`; a blend for each output; a clear's color past 1, or one for each image (a clear's color is bytes on its way to the renderer); a game asking whether float images are floats; `colors.count`. All Open in `docs/spec.md`.
  - **Tests:** `draw_targets_hold_several_colors_and_lend_each` (`tests/test_draw.c`) for ids, references, the list and another build's; `check_colors` in `platform/tests/targets.c` at `gpu.h` (three outputs into three images, one into three, three into one, floats that add up, what's read back, and OpenGL as a device with no floats), and `check_render_colors` through the renderer (the engine's own draws, a clear, blits of two outputs and of one, a target's own second image refused, one it hasn't got, a forgotten list, and the fallback again); pass 11 of `platform/tests/shaders.tide` with `run_output_checks` in `shaders.c` (frags of three outputs, two and one written in Tide, one that discards, a shape of the engine's, each image read back by a frag, a float image added into and read blended), the same pixels on OpenGL 3.3, WebGL 2 and WebGPU, where the device is then lost and found; `compiler/tests/e2e/render_colors` for what tidec generates, a reload in the same memory and in other memory among it; `compiler/tests/shaders/outputs` pins the GLSL and WGSL; `compiler/tests/errors/render_target_format*`, `render_target_colors_*`, `render_target_*_assigned` and `frag_*_output*`, a file a diagnostic; `lsp_render_target_colors_and_frag_outputs`. Android: the apps of these tests, in CI's Android job (`notes/android-and-mingw.md`, Every window fixture an app). Not run: OpenGL on a Mac. The docs' `outputs` example shows it (`docs/game/shaders.md`, Several outputs): a scene filled into three images by one frag, and lit from them by a blit's.
- **A target's samples: multisampling, said where a target is made** (card 8 of the reshaping, built 2026-10-07. That render targets have MSAA with a `samples` argument is the owner's; that it's a named argument and the last, the counts, what a device with fewer does, that a multisampled depth is refused, and that the window has none are Claude's picks, Provisional in `docs/spec.md`, Views and drawing, A target's samples). `docs/game/render-targets.md` (Smooth edges) is the users' page. What's built, and what code relies on:
  - **Tide:** `RenderTarget(width, height[, pixelsPerUnit][, format...], samples: n)` and `RenderTarget.Window([factor][, format...], samples: n)`, with n 1, 2, 4 or 8. Its color images are read as any target's (`target.color`, `target.colors[i]`): what each pixel's samples come to when a pass on it ends. `target.depth` of one wasn't read until 2026-10-13 (above): the draw that read it was passed by, with a line.
  - **The id has it, in two bits more** (`TIDE_RENDER_SAMPLES`, bits 14 and 15: the count as a power of two; `tide_render_target_samples(id)`, 1 for the window and a borrowed target). So `TIDE_RENDER_BITS` is 16 bits, a state is on a boundary of 64 KiB (`make_target_sampled` allocates that much more than the state, as `foreign_state` in `tests/test_draw.c` does), and `tide_render_target_state.holds` is those 16 bits but the image's two. The low bits, and not the bits above a pointer's 48: Android tags the top byte of heap pointers, and a 32-bit one has none to spare by rule. `tide_render_target_whole` keeps the samples (they're the target's, not an image's), so everything that goes by a target's id goes by them with no field of its own, and `tide_render_target_recreate` makes a target again with the samples its old id said: a hot reload keeps them with the generated code it had (`tide_local_rebind_resources` is the C it was). `tide_render_target` is still 24 bytes.
  - **C:** `tide_render_target_make_sampled(width, height, formats, count, samples)` and the three beside it (`_make_scaled_sampled`, `_screen_sampled`, `_window_sampled`); NULL and 0 for the formats is one `TIDE_TARGET_RGBA8` image. `samples_of` in `engine/src/draw.c` makes any number one of the four (the one under it, with a line), and gives a target of depth alone one, with a line.
  - **tidec** (`compiler/src/check.c`): `target_samples` finds the argument named `samples`, which has to be the call's last and a whole number, and sets `names_taken`; any other name on a target's arguments is an error there. A constant that's none of 1, 2, 4 and 8 is an error, and so is `Format.Depth` with a constant above 1 (`depth_alone_sampled`). `target_formats` counts the formats before that tail. The call is `expr.target_samples`, with `expr.target_formats` then however many formats it has, none and one too, as C's `_sampled` functions take them as several (`gen_target_format` in `codegen.c` writes `NULL, 0` for none). **A game that says no samples generates the C it did, byte for byte** (old and new tidec over every game's source list, with `--layout` and without: 209 games), and its header and hash with it.
  - **`gpu.h`: a target's samples are in its kind,** `tide_gpu_target_format`, from bit 16 (`tide_gpu_target_sampled`, `tide_gpu_target_samples`; none said is 1), and not a fourth argument of `target`: a pipeline is made for a sample count on WebGPU, and everything the renderer makes for a kind of target is keyed by that one number already (`engine_pipelines.into`, `stencil_pipeline.into`, `shader_pipeline.into`, a pipeline's `into`), so no key gained a field and none can be forgotten. `target_samples(format)` is what a device has for what a format asks: the most of 8, 4 and 2 that's no more than asked and that each of the target's images and its depth can have (OpenGL: `GL_MAX_SAMPLES` on desktop; on OpenGL ES 3 and WebGL 2, `glGetInternalformativ` for each of `GL_RGBA8`, `GL_DEPTH24_STENCIL8` and, for a float image, `GL_RGBA16F`, which may have none); WebGPU has 1 and 4, so 2 is 4 there. **The format given to `target` and a pipeline's `into` say what `target_samples` gave**: the renderer's `gpu_target_format` asks it, and says once when it's fewer than asked. A multisampled target's color textures have one color a pixel, what `end` resolves its samples into, and those are what `target_texture` and `target_read` give; `target_depth` is 0 until `resolve_depth` (above).
  - **OpenGL** (`gpu_gl.c`): the target's framebuffer is of multisampled renderbuffers, each color image's and a `GL_DEPTH24_STENCIL8` one (`gl_target.sampled`, `sampled_depth`), and each color image, a texture as ever, is in a framebuffer of its own (`resolved`), which `gl_end` blits the samples into, attachment by attachment (`glReadBuffer`, `glBlitFramebuffer` with `GL_COLOR_BUFFER_BIT`), with the scissor test off, as a blit goes by it. `gl.h` and `tide.js` gained `glBindRenderbuffer`, `glBlitFramebuffer`, `glDeleteRenderbuffers`, `glFramebufferRenderbuffer`, `glGenRenderbuffers` and `glRenderbufferStorageMultisample`, and for OpenGL ES 3 and WebGL 2 alone `glGetInternalformativ` (desktop has it from OpenGL 4.2, so it isn't among the functions a 3.3 context has to have). No extension. `gl_use`'s check that a pipeline is for the pass's kind of target is by the samples too.
  - **WebGPU** (`tide.js`): a multisampled texture for each color image and one for depth and stencil (`sampled`, `sampledDepth`, `sampleCount: 4`), kept from pass to pass (`storeOp: 'store'`), with each color image's own texture as the attachment's `resolveTarget`; a pipeline's `multisample.count` is its `into`'s (`samplesOf`: bit 16 on, 4 or 1; a change to the format's layout goes there).
  - **(Until 2026-10-13: it's resolved since, above.) A depth read of a multisampled target is refused, not resolved** (`unavailable_texture` in `platform/src/platform.c`: "draw skipped: source render target is multisampled..."), by what the target asked for, whatever the device gave, so the same draws are passed by everywhere. OpenGL could resolve a depth by a blit; WebGPU can't without a shader of the engine's that reads each sample, a pass more for every such target every frame whether anything reads it.
  - **The window has none** (see the spec for why: three ways to ask on three platforms, each refusable, and none on a WebGPU canvas). A game draws into `RenderTarget.Window(samples: 4)` and blits.
  - **Memory:** `samples` times the color and depth, and the resolved color beside: 36 bytes a pixel at 4 samples of `Rgba8` where a plain target has 8.
  - **Not built:** a query of what a device has or a target got; a frag for each sample; alpha to coverage; a resolve before which light is tone mapped; completion that offers `samples:` in the editors.
  - **Tests:** `draw_targets_say_their_samples_and_keep_them_through_a_reload` (`tests/test_draw.c`: the id's bits, each way to make one, counts that are none, references, recreate, a foreign state); `check_samples` in `platform/tests/targets.c` at `gpu.h` (a triangle whose edge crosses an 8 by 8 target: pixels in between with samples and none without, the texture sampled is what's read back, a second pass over the samples the first left under a scissor, a clear, and three color images with floats, each resolved) and `check_render_samples` through the renderer (a turned 3D mesh in a target of the window's size, blitted to the window, with 4, 1 and 8 samples: edge pixels in between or not, a shader's blit and a shape of the engine's into it, kept a frame, its depth refused), the same on OpenGL 3.3, WebGL 2 and WebGPU, where the device is then lost and found; `compiler/tests/e2e/render_samples` (each way to write it, a constant, a variable, a function, numbers that are none, depth alone, references, a reload in the same memory and in other memory), natively and under Node; `compiler/tests/errors/render_target_samples_*`, `render_target_window_samples_*` and `render_target_argument_named`, a file a diagnostic; `lsp_render_target_samples`. Android: the apps of `targets.c` and `shaders.tide`, on the device's Vulkan, in CI's Android job (OpenGL ES 3's path went 2026-11-05). Not run: OpenGL on a Mac.
- **A `Cubemap` is six images a frag reads by a direction: an asset's six files, or a cube a view draws into a face at a time** (item 9 of the reshaping, the last, built 2026-10-07. That the drawing side gets cube maps, for skyboxes and reflections, is the owner's; each rule is Claude's pick, Provisional in `docs/spec.md`, Views and drawing, Cubemaps, with what was weighed: a type of its own over a `Texture` with a flag, the faces' file names, `Cubemap(size)` giving a `Cubemap`, a cube's size in pixels, a filter said at the Sample and none carried). `docs/game/shaders.md` (Skybox and reflections, with the live `skybox` example), `docs/game/assets.md` (Cubemaps) and `docs/game/meshes-and-textures.md` (Six of them, as a cube) are the users' pages. What's built, and what code relies on:
  - **Tide:** `asset Cubemap sky = "art/sky_??.png";`; `Cubemap(size[, format])`, a constructor as `RenderTarget(width, height)` is (it was `RenderTarget.Cube` for an afternoon: a static of one type that makes another read oddly, and `RenderTarget.Cube` is an error that names the constructor now); `cube.faces[i]`, a `RenderTarget`; `cube.Resize(size)` and `cube.Dispose()`; and in a frag or a vert, `cube.Sample(direction[, filter])`, where the cube is an asset by its name or a field of a component or a singleton the code takes, as a `DepthTexture` is.
  - **The faces are `+x`, `-x`, `+y`, `-y`, `+z`, `-z`, each as it's seen from the cube's middle with its rows from the top:** `+y` up for the four around, `+x` to the right of `+z` (the world is left-handed, as `perspective_camera` is), `-z` at the top of the one above and `+z` at the top of the one below. It's OpenGL's and WebGPU's own layout, so pixels go in as they are on both, and a face drawn into is the same way up as one uploaded: OpenGL's target passes are flipped as they're drawn (`compile` in `gpu_gl.c`), which puts a face's top in its first row, where a cube face's `t` starts. `check_cubes` (`platform/tests/targets.c`) reads an uploaded cube and a drawn one towards the same twelve directions, and pass 17 of `shaders.tide` draws a card through `quaternion.LookRotation` cameras into each face and reads it back by the direction it was at: a change to either backend's rows shows there.
  - **`gpu.h`:** `cube(color, size, faces)`, a texture that's only ever read as a cube (`TIDE_GPU_READ_CUBE`, a pipeline's `reads`: a `samplerCube`, and a `texture_cube<f32>` with a `sampler`), with `faces` six pointers to 4 bytes a pixel or NULL for one that's drawn into; and `cube_face(cube, face)`, a target whose one color image is that face, with depth and stencil of its own, whose `target_texture` is 0, whose `target_read` reads the face, and which is freed before its cube. `sample`'s wrap isn't read for a cube, `mipmaps` makes a cube's smaller images, and `texture_free` frees one. It counts as one of a pipeline's 8 textures.
  - **OpenGL** (`gpu_gl.c`) adds no function: `glTexImage2D`, `glFramebufferTexture2D` and `glGenerateMipmap` take a cube's targets (`GL_TEXTURE_CUBE_MAP`, `GL_TEXTURE_CUBE_MAP_POSITIVE_X` + face, in `gl.h`). `gl_texture.cube` says which way a name is bound, and `gl_texture_free` zeroes what's known of a name, as OpenGL hands names out again. Desktop OpenGL 3.3 has to be asked to read across a cube's edges (`GL_TEXTURE_CUBE_MAP_SEAMLESS`, enabled once, where the samplers are made); OpenGL ES 3 and WebGL 2 always do, and WebGL would refuse the enable, so it's desktop's alone. A cube asked for floats on a device with none is bytes, and its faces' targets are still of the float kind (`asked_floats`), as a target's float image is.
  - **WebGPU** (`tide.js`): a texture of six layers with room for every smaller image from the start (a cube's samplers that read none of them keep to its own pixels by `lodMaxClamp`), a `cube` view that draws sample, and a 2D view of each face's each level, which a pass on a face draws into and `gpu_mipmaps` halves through, a face at a time, with the pipeline textures' use. One with nothing in its smaller images yet is read as `LINEAR` when it's asked for `MIPMAPS` (`gpu_sample`, by `t.mipped`), which is what `gpu.h` says of a texture with none. A cube that's uploaded is `rgba8unorm`; one that's drawn into is the canvas's own format (`gpuFormat`), as a target's image is, since the pipelines into it are made for that. A pipeline's `reads` are 3 bits each on their way to the page (`tide_web_gpu_pipeline` in `gpu_web.c`, `gpu_pipeline_begin`): they were 2 until `READ_DATA`, the fifth way to read a texture (see Lists and arrays on the GPU, below). `layoutFor` gives a cube `viewDimension: 'cube'`, and `bindGroup` its clamping sampler by its filter alone.
  - **A cube's and a face's ids** (`tide/draw.h`): a cube that's drawn into is a managed target's state with `cube` set, and its id has `TIDE_RENDER_CUBE` (bit 51); a face's is the cube's with which face in `TIDE_RENDER_FACE` (bits 48 to 50, 1 + its number). Those are above where an address ends on every platform Tide builds for, and `tide_cubemap_make_as` stops the program for a state whose address has them. So a face is a destination of its own in a draw list (its own `tide_draw_place`: camera, clip and settings), with the cube's state, version and size; `tide_render_target_state_of` masks the bits, `tide_render_target_face` and `tide_render_target_cube_of` read them, and `tide_render_target_cubic` says either. `tide_render_target_color` and `_depth` of a face name no image (`TIDE_RENDER_INVALID`), and `tide_render_target_resize` and `_dispose` of one say so and do nothing. `tide_render_target_recreate` keeps both sets of bits, so generated C's reload code for a `RenderTarget` field that holds a face is what it was.
  - **`tide_cubemap_ref`** (Tide's `Cubemap`, 32 bytes: a `tide_render_target`, then a name) is a cube that's drawn into (`target`) or the pack's (`named`, as a pack `Texture`'s version names its declaration); zeroed, it's no cube. `tide_draw_cube_source` gives the list's texture: `TIDE_PIXELS_TARGET_CUBE` (the cube's id, size and version, no pixels) or `TIDE_PIXELS_PACK_CUBE`, told by what's in its six faces, whose `pixels` is the pack's six pointers (a `const uint8_t *const *`, cast: nothing here frees it, and `append_texture` hands it on), noted again each frame as a pack image's are. A shader's `texture_reads` says `TIDE_SHADER_READ_CUBE`.
  - **The renderer** (`platform/src/platform.c`): `gpu_cube` is a drawn cube's GPU texture, by the cube's id, and each face is a `gpu_target` by the face's id, made with the cube (`cube_for`, which clears all six, so a face nothing drew into is black on OpenGL too) and freed before it. `target_for` of a face goes through `cube_for`; the cube's own id is no destination. A pack's cube goes through `texture_for`, which makes it again whenever its version isn't what it has (`gpu->cube` has no write). `texture_to_read` gives a cube only to a read of that kind, and `gpu_state.dark`, a cube of one black pixel a face, where none was given. `of_destination` is what `samples_destination` asks: a face's draws don't read its cube. `prepare_source_target` makes a cube nothing drew into, `prepare_mipmaps` makes a drawn cube's smaller images when a pass began on any of its faces since (`gpu_cube.drawn`, counted where the loop begins a pass on a face), and a lost device and a hot reload forget cubes with targets.
  - **The pack:** `TIDE_PACK_CUBEMAP` (8) is a block of six handles to texture blocks (`tide_pack_texture`, shared by their files' SHA-256 with any `Texture` of the same file), in the faces' order; tidec writes it (`write_pack`, with `pack_item` for each face). The loader notes each cube whose six faces are whole, square and one size (`note_cubes`, after `lay_textures`, as the pointers it keeps are into the region where it stays), under a name made of the declaration's as a texture's is, with a version that's a hash of its faces' versions. `tide_pack_cubemap_of` gives the value and `tide_pack_cubemap_find` the faces. The file format's version didn't change: a pack with no cubemap is the bytes it was.
  - **tidec:** `TY_CUBEMAP` (`tide_cubemap_ref`), a rendering resource wherever `DepthTexture` is one (`holds_render_resource`, `resource_type`, the reload's rebinding in `gen_resource_fields`: `tide_render_target_adopt`, or `tide_cubemap_recreate`). `check_cube_target` (from `check_window_target`, as `Cubemap(size)` is the owner `RenderTarget`'s; the two entries in `builtins.c` are for editors), `check_cube_sample`, and `cube.faces` as an index's object alone, through `checker.colors_indexed` and `expr.target_colors`, as `target.colors` is. `ASSET_CUBEMAP` (`asset_files.h`): `asset_resolve` wants a glob, reads each match as a PNG, and `order_cube` puts the six in the faces' order by how their names end (`cube_face_of`), or says what's wrong.
  - **In the shaders** (`ex_cube_sample` in `compiler/src/shader.c`): `READ_CUBE` is a `shader_sample`'s and a `shader_texture`'s `read`, a binding for each filter a cube is read with; `Filter.Trilinear` in a frag lifts the direction as a uv is lifted (`tide_uv<n>`, `tide_dx<n>`, `tide_dy<n>`, of three components), and what's read goes through `tide_straight`, as a texture's color does. `compiler/tests/shaders/cubemaps` pins both languages.
  - **Generated C is what it was for a game with no cubemap, byte for byte** (dev's tidec against this one over every game's source list, 248 of them, with `--layout` and without), and no shader's text moved (`compiler/tests/shaders/`). The two games that differ are the ones that use it.
  - **Not built** (all in `docs/spec.md`; a level a game picks, `SampleLevel`, and a cube of depth alone were built since, 2026-11-13/14): a face's color or depth as a texture; several colors or samples on a cube; a cube as a draw's own texture or a frag's own parameter; files other than six PNGs; lists and maps of cubes; all six faces in one pass; a helper for the six cameras.
  - **Tests:** `check_cubes` in `platform/tests/targets.c` at `gpu.h` (six faces uploaded and six drawn into, each read towards both of its quarters, blended at a face's middle, with no smaller images and with them, a face read back, a pass over what's there, a float cube, everything made twice); pass 17 of `platform/tests/shaders.tide` with `run_cube_checks` in `shaders.c` (from Tide: the asset by name and through a field, a painted cube, a face read between its smaller images made of what was just drawn, a float cube, no cube, a vert's read, and each face against a camera at its middle), the same pixels on OpenGL 3.3, WebGL 2 and WebGPU, where the device is then lost and found; `compiler/tests/e2e/cubemaps` (values, ids, faces as destinations, each binding's filter, a reload in the same memory and in other memory, Resize and Dispose, the pack gone), natively and under Node; `pack_cubemaps_are_six_images_named_as_one` (`tests/test_pack.c`); `compiler/tests/shaders/cubemaps`; `compiler/tests/errors/cubemap_*.tide`, a file a diagnostic (48, with wrong face files in `compiler/tests/e2e/cubes/`, which is outside `e2e/assets/` so the asset tests' globs don't match them); `lsp_cubemaps`. Android: the apps of these tests, in CI's Android job (`notes/android-and-mingw.md`, Every window fixture an app). Not run: OpenGL on a Mac, which nothing runs.
