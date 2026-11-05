# Textures and how they are sampled

## Texture arrays: built (2026-11-05)

What the owner agreed (below) is built; the rules are in `docs/spec.md` (Texture arrays), the users' page is
`docs/game/shaders.md` (Texture arrays) and `docs/game/assets.md`. Everything here is Claude's, Provisional.

- **Runtime values** (`tide/draw.h`): `tide_texture_array_ref` (40 bytes: a target, a pack name, its layers) is Tide's
  `TextureArray`; `DepthTextureArray` is a `tide_depth_ref` of the array's target. A drawn array is a managed target's
  state with `layers` (1 to `TIDE_TEXTURE_ARRAY_LAYERS`, 256); its id has `TIDE_RENDER_ARRAY` (bit 52), and a layer as a
  destination `TIDE_RENDER_LAYER` (bits 53 to 61, 1 + the layer): `tide_render_target_arrayed`, `_layer`, `_array_of`,
  as a cube's faces. `tide_texture_array_make` (and the `_sized`/`_scaled` wrappers generated C calls), `_layer`,
  `_depth`, `_resize`, `_dispose`, `_recreate` (a web reload: the layers come in the value), `_size`, `_count`,
  `tide_depth_array_count`. `tide_render_target_recreate` keeps a layer's bits. Resizing or disposing a layer alone says
  so and does nothing; a layer's color and depth name no image, as a face's don't.
- **The draw list:** `TIDE_PIXELS_TARGET_ARRAY` (by the array's id) and `TIDE_PIXELS_PACK_ARRAY` (the pack's pointer to
  each layer's pixels, as a pack cube's), with `tide_draw_texture.layers`; an array's depth is `TIDE_PIXELS_TARGET_DEPTH`
  of the array's id. `tide_draw_array_source`. A blit refuses an array a layer of which it draws into; `append` carries
  both.
- **The pack:** `TIDE_PACK_TEXTURE_ARRAY` (19), a list block of texture handles, uncompressed (`pack_item` with no
  compression, `[Raw]` as the declaration says). The loader notes each array whose layers are whole and of one size
  (`note_arrays`, `tide_pack_texture_array_find`, `tide_pack_texture_array_of`); its version is a hash of its layers'.
- **tidec:** `TY_TEXTURE_ARRAY`, `TY_DEPTH_TEXTURE_ARRAY`; `ASSET_TEXTURE_ARRAY`, read as Textures (each path a file or a
  glob, a list of paths as an AnimationCatalog's, no layers merged), with the size and count checked;
  `check_array_target`, `check_array_sample`, `check_depth_array_read`, `layers[i]` through `colors_indexed` as a cube's
  `faces`. Shader reads `READ_ARRAY`, `READ_DEPTH_ARRAY` and the compiler-only `READ_COMPARE_ARRAY` (a depth array's
  texture read, a compared sampler); a way's sampler reads its `base_read`, so arrays share samplers with textures read
  alike. WGSL: `texture_2d_array<f32>`, `texture_depth_2d_array`, the layer after the uv as
  `clamp(i32(layer), 0, i32(textureNumLayers(t)) - 1)` (`put_layer`). The SPIR-V and HLSL translators take both
  (`arrayed` types; vec3/float3 coordinates; `tidehl_layers`; fixtures `compiler/tests/wgsl/texture_arrays*.wgsl`,
  through spirv-val and fxc). A reload remakes a drawn array from its value (`gen_resource_value`).
- **gpu.h:** `array(format, width, height, layers, layer_pixels)`, `array_depth`, `array_layer`; reads
  `TIDE_GPU_READ_ARRAY` (6) and `_DEPTH_ARRAY` (7), whose samplers read COLOR and DEPTH/COMPARE
  (`tide_gpu_sampler_read`). Vulkan: `make_layers` (one image of `layers`, a `2D_ARRAY` view, a base view, a 2D view a
  layer to draw into, cleared), color arrays with room for their smaller images from the start where the format blits,
  a depth array beside (or as) it; a layer's target is a face-like target that attaches that layer of both. WebGPU: the
  same in `tide.js` (`gpu_array`, `layered` views, which cubes now share, `depthLayers`), its layouts `'2d-array'`.
  The renderer: `gpu_array`, `array_for` (layers' targets cleared black and far as made), texture reads, sources made
  before they're read, a layer's pass counted for the array's smaller images, fallbacks (`blank`, a white array of one
  pixel; `far_array`, a depth array at the far limit). Memory is held as the array's whole.
- **Unity** (embed ABI 15, 16 for a grid's arrays): notes/unity.md, Texture arrays.
- **Tests:** `tidec_e2e_texture_arrays` (an asset's glob and list, a list's repeated file one image, counts, layers as
  destinations, the run's textures and samplers, a reload in other memory, Resize and Dispose, natively and under
  Node); `compiler/tests/shaders/texture_arrays` (golden, through spirv-val and fxc); `tidec_error_texture_array_*`
  (7); pass 25 of `platform/tests/shaders.tide` (`run_array_checks`: an asset's six layers, a painted array's four,
  layers past the last and before the first, a layer that differs across a square, a layer read between its smaller
  images, a never-made array and depth, a depth array's three layers read and compared), on Vulkan (with the
  validation layer) and WebGPU, again after device loss on the web; `embed`'s packet reader takes the new ops; LSP
  (`lsp_texture_arrays`).
- **Gaps:** see the follow-up below.

### The gaps closed (2026-11-05, the owner: "work on the gaps")

- **Compressed layers.** The pack writes each layer as a `Texture` asset's (`compress = !lossless`), so a layer of 128
  or more, a power of two a side, is BC1 or BC3 with its chain made in the build; `[Lossless]` is allowed on
  `ASSET_TEXTURE_ARRAY` and keeps them all RGBA8. `note_arrays` takes layers that are blocks alone (their pixels NULL)
  and `tide_pack_texture_array_blocks(named)` gives each layer's `tide_texture_blocks` (`blocks_at`, shared with
  `tide_pack_texture_blocks_find`); the draw list hands them on as `tide_draw_texture.layer_blocks`. gpu.h's
  `array_blocks(width, height, layers, blocks)` takes one format a call, each layer a whole chain `bytes` apart. The
  renderer's `pack_array_texture` makes the GPU one: all BC1 stays BC1, a mix is BC3 throughout
  (`tide_texture_blocks_bc1_to_bc3`, `tide/texture_blocks.h`, puts an opaque alpha block before each BC1 one: exact),
  without BC every level decoded to an RGBA8 chain (the same mips a `Texture`'s fallback has, not regenerated), and an
  array of RGBA8 layers as before. Vulkan `vk_array_blocks`, WebGPU `gpu_array_blocks`; the recorder decodes to pixels
  (Unity's arrays aren't compressed).
- **`size` and `count` on the GPU.** A shader texture's size words are three for an array (`shader_size_words`): its
  width, height and layers, from `tide_texture_array_size`/`_count` (`gen_array_size`), read by `ex_texture_size`
  as uniform words, as a texture's size already is.
- **`Grid3` to `TextureArray`.** `type_is_array_grid` (`Grid3<Color>` or `Grid3<float>`) is assignable to
  `TY_TEXTURE_ARRAY`; `gen_as` makes `tide_texture_array_of_grid(grid, &shape)`, a value naming the grid
  (`reserved == TIDE_TEXTURE_ARRAY_GRID`, `named` the handle, `layers` the cell kind and the chunk shifts;
  `tide_texture_array_grid` reads it back). The draw list's `TIDE_PIXELS_GRID_ARRAY` (`tide_draw_grid_array_texture`,
  mesh.c) is `width` by `height * layers` pixels, the layers one under another, through `grid_cells` with a box of
  three dimensions and `across` 1, so its changed areas follow the chunks as a grid texture's do; `layers` set. gpu.h
  `array_of(format, width, height, layers)` (RGBA8 or R16F, zeros, room for smaller images) and
  `array_write(array, layer, pixels, x, y, width, height)`; the renderer's `grid_array_texture` writes it whole when
  made, else each changed area split by the layers it crosses. Vulkan `vk_array_of`/`vk_array_write`
  (`write_pixels` with a layer), WebGPU `gpu_array_of`/`gpu_array_write` (`writeTexture` at `[x, y, layer]`, its
  smaller images made again when next read), the recorder `ARRAY_OF`/`ARRAY_WRITE` (embed ABI 16, notes/unity.md).
- **Tests:** `tidec_e2e_texture_arrays` (BC1 and BC3 layers with no pixels, a `[Lossless]` array's pixels and no
  blocks, the blocks handed to the list, a `Grid3<float>`'s array: count, size, no destination, the list's halves
  stacked by layer); `compiler/tests/shaders/texture_arrays` (`count` and `size` in a frag); pass 25 of
  `platform/tests/shaders.tide`: a compressed array's opaque and alpha layers against the same files as `Texture`s,
  its smaller images, a frag's `size`/`count`, a `Grid3<Color>`'s three layers and a `Grid3<float>`'s fourth, a cell
  changed then changed back between frames (a partial write; a broken one leaves the old color, checked by breaking
  it), on Vulkan and WebGPU, and `tide_platform_shaders_rgba_fallback` (WebGPU with `texture-compression=off`);
  `embed_render_array_written_in_parts`.
- **Then the rest** (the owner: "why are we stopping with things still open? the testing i understand and that can be
  postponed"):
  - **`[Raw]` arrays and cubes.** The checker's raw role is `raw_kind` (Texture, TextureArray, Cubemap) wherever it
    was Texture's: `[Raw]` on `ASSET_TEXTURE_ARRAY`/`ASSET_CUBEMAP` assets and on TextureArray, Cubemap and
    `Grid3<Color>` fields (`check_field_raw`); raw uses, field values and escapes as for textures (`check_raw_uses`,
    `check_raw_field_value`, `refuse_raw_grid` naming the type); array and cube `Sample` and array `size`/`count`
    mark the source read. `declared_raw` covers both field types, so their reads skip `tide_straight` (the cube's in
    `ex_cube_sample` too). A raw array asset's layers pack raw (`pack_item` with `sample_raw`, compressed as raw
    Textures are); a raw cube's faces too (never compressed). A `[Raw] Grid3<Color>`'s array is
    `tide_texture_array_of_grid(grid, &shape, true)`: cell kind 3 in the ref (`tide_texture_array_grid_raw`), drawn by
    `tide_draw_grid_array_texture(..., raw)` with `cells_writer.raw` and `KIND_ARRAY_RAW`. Hot-reload layout names
    `[Raw] TextureArray`/`[Raw] Cubemap`. Tests: e2e (`numbers` asset, a raw Grid3's alpha-0 cell kept, the run has
    no `tide_straight` on the web, a color array's does); goldens `shaders/texture_arrays`, `shaders/cubemaps`; errors
    `raw_array_kept`, `raw_grid3_kept`, `raw_array_field_colors`, `raw_on_float_grid3`, `raw_cube_kept` (replacing
    `cubemap_asset_raw`); pass 25's alpha-0 raw Grid3 pixel on Vulkan and WebGPU.
  - **Compressed images in Unity**, textures and arrays alike: notes/unity.md, ABI 17.
  - **Unity's OpenGL Core reading one texture two ways**: notes/unity.md, ABI 17.
  - **The prototypes' `WEBGPU`**: already gone on tide-prototypes' dev (its foldline test says `PIXELS`); only a stale
    local checkout had it.
- **Still open:** none of the Unity side has run in Unity (no Unity here: C# compile checks, fxc for the HLSL, the
  recorder's packets in `embed`); Linux, macOS and Android are CI's. The layer-before-array reload case above needed
  nothing: recreating the array reconciles its layer count.

## Texture arrays: the proposal, agreed (2026-11-05)

The owner picked the recommended option of each of the four questions asked (2026-11-05): types of their own
(`TextureArray`, `DepthTextureArray`); an asset of a glob or a list of paths; one drawn into made by
`TextureArray(width, height, layers, ...)` with `layers[i]` as targets and `.depth` the depth array; and
`Sample(uv, layer)` with an int layer, clamped. The rest of the proposal below came with them; its details stay
Claude's picks, Provisional.

Asked for by the owner ("just cause nothing needs [it] isn't an excuse"), from Mount & Rifle's ground frag (9 material
scans, two or three shadow cascades and a canopy texture, notes/gaps.md there). The language design is the owner's:
nothing below is built until it's agreed. Every backend has them: WGSL `texture_2d_array<f32>` and
`texture_depth_2d_array` (with `textureSample*(t, s, uv, layer)`), Vulkan images of several layers viewed as
`VK_IMAGE_VIEW_TYPE_2D_ARRAY`, Unity's `Texture2DArray` and `RenderTexture` with `TextureDimension.Tex2DArray`
(HLSL `Texture2DArray`). The layer limit everywhere is at least 256 (WebGPU's default `maxTextureArrayLayers`,
Vulkan's least `maxImageArrayLayers`).

1. **Types of their own, `TextureArray` and `DepthTextureArray`,** as `Cubemap` is one (the same argument: every place a
   `Texture` goes reads it by a `float2`, and an array is read by a `float2` and a layer, so each would refuse one).
   One array is one of a draw's 16 textures, its ways of reading samplers as a texture's (the rule just built).
2. **An asset of same-sized images:** `asset TextureArray ground = "art/ground/*.png";`, the glob's files as layers in
   the order a glob's list has them (by path), or a list of paths, `asset TextureArray ground = ["art/grass.png",
   "art/rock.png"];`, in its order. Every file the same size (an error naming the one that isn't, with both sizes),
   1 to 256 of them. `[Filter]`, `[Wrap]`, `[Raw]` and `[Lossless]` are the whole array's, as for a `Texture`. Each
   layer is compressed (BC1/BC3) as a `Texture` asset is, all layers one format (BC3 if any has alpha). Layer names:
   the glob's `ground.Layer("rock")`? Not proposed; a game uses the index (or its own `const int`s).
3. **One a view draws into:** `TextureArray(width, height, layers[, pixelsPerUnit][, format...][, samples: n])`, a
   constructor as `Cubemap(size)` is, whose `layers[i]` is a `RenderTarget`, a destination as a cube's face is (its own
   camera, clip and settings, depth and stencil of its own). `array.depth` is its `DepthTextureArray`: each layer's depth.
   So shadow cascades are `TextureArray(2048, 2048, 3, 1, Format.Depth)`, each cascade drawn into `cascades.layers[c]`,
   read as `cascades.depth.Compare(uv, c, value)`. `Resize(width, height)` and `Dispose()` as a target's.
4. **Reading one in a frag or a vert:** `array.Sample(uv, layer[, filter][, wrap])`, `array.SampleGrad(uv, dx, dy,
   layer[, filter][, wrap])`, `depthArray.Sample(uv, layer)` and `depthArray.Compare(uv, layer, value)`. `layer` is an
   `int` of any expression, so it can differ from pixel to pixel (a cascade picked by depth, a material by a splat
   map). One past the layers reads the nearest one: tidec clamps it, as WGSL leaves an out-of-range layer to the GPU.
   Filters and wraps are a sampler's, so they're the whole array's for that read, never a layer's own: a
   `Filter.Trilinear` read lifts the uv as a texture's does (the layer needn't be), and the smaller images are made
   for every layer.
5. **Where it's read from:** an asset by its name, or a field of a component or a singleton the code takes, as a
   `Cubemap` or a `DepthTexture` is; a function takes one and reads its call's. A `Grid3<Color>` (z the layer) and a
   `Grid3<float>` convert to a `TextureArray` where one is wanted, as a `Grid2` converts to a `Texture`: the grid's
   cells as layers, sent as a grid's texture is (only what changed).
6. **On the CPU** (local code, as `Texture.size` is): `array.size` (an `int2`, a layer's pixels) and `array.count`
   (the layers), zeros for none. A `TextureArray` is this machine's, as a `Cubemap` is: no match state holds one.
7. **Limits:** 256 layers (an error for a constant past it, a line and the most at run time), a side up to the
   device's 2D limit, and each layer counts towards the image memory budgets as a texture does.
8. **Unity:** `Texture2DArray` for an asset's, a `RenderTexture` of `Tex2DArray` with `volumeDepth` for one drawn
   into, read in the HLSL as `Texture2DArray<float4>`/`<float>` with `float3(uv, layer)` (the compare by `GatherRed`
   as a depth's is now); the embed's texture and target packets gain a layer count, so the ABI moves.

Not proposed: cube arrays (`texture_cube_array` isn't in WebGPU's core); a layer of an array as a `Texture` of its
own; arrays of more than one color image or as a draw's own texture (`Draw.Mesh(..., texture)`).

OpenGL 3.3, OpenGL ES 3 and WebGL 2 were removed 2026-11-05 (`notes/rendering.md`, GL backends removed): what this file says of OpenGL, WebGL, GLSL, `gpu_gl.c`, `gl.h`, `*_opengl` tests or `_d3d11`/WebGL runs is history; Vulkan and WebGPU are the backends, and every web test runs on WebGPU.

## `[Raw]` images compress too; `[Lossless]` keeps one exact (2026-11-04)

From a game's feedback (Mount & Rifle, pinned 0.6.0-nightly.21): its 618 MB distribution pack was ~495 MB of 47
`[Raw]` textures, normal and ARM maps stored as RGBA8 because `pack_item` compressed only `!raw` images (a 4096
normal map 67,108,864 bytes, 4096²×4; BC1 with mips is ~11 MB). 173 of its 179 Raw textures had no alpha. The
owner chose, from a proposal: "Compress, opt-out", the attribute named `[Lossless]`.

- **Packing** (`pack_item`, `compiler/src/codegen.c`): the `compress` argument is now `!decl.lossless`, and Raw
  images go through `texture_compress` as color ones do. The RGBA it's given is already what the GPU reads: color
  bytes premultiplied, Raw channels as the file has them (worked alpha 1), and `texture_compress`'s box filter averages each
  channel apart, which is right for both. BC1 when every alpha byte is 255, else BC3, as for colors. The
  decoder for devices without BC (`tide_texture_blocks_decode`) never premultiplied, so a Raw image decodes to
  its numbers; no runtime, shader or ABI change. Authored DDS chains, cubemap faces (`compress` false) and
  small/non-power-of-two images are as they were.
- **Cache keys** (`texture_cache_key`, `compiler/src/texture_cache.c`): a Raw compressed variant hashes a third
  word, as its old key held exact RGBA another build would otherwise reuse. Color keys are unchanged, so no color
  image is re-encoded.
- **`[Lossless]`** (`decl.lossless`, `check_lossless_attribute` in `check.c`): before a `Texture` asset, one or a
  glob's list or map, with or without `[Raw]`. Takes nothing, once; an error on a non-asset (through the general
  attribute loop) and on any other asset type, grids and cubemaps included ("which is never compressed"). Editors
  offer it in an asset's brackets with a hover (`compiler/lsp/analysis.c`).
- **Mips** (the feedback's second question): an uncompressed image, Raw or `[Lossless]`, has only its base level
  in the pack; the renderer makes the rest on the GPU the first time a `Filter.Trilinear` draw reads it
  (`prepare_mipmaps`, below), so the pack's 4096²×4 was right and the GPU held 4/3 of it. Compressed Raw images
  now carry the build's chain, as color ones do.
- **Images are shared by what they store** (owner, the same day: "if they are equivalent they should remain the
  same"): `[Raw]` only changes alpha's part, so an opaque file declared as a color and as Raw comes out the same
  bytes, which were two blocks because the content key was the file's hash with bytes turned for Raw and for
  blocks. The content key is now `texture_content` (`texture_cache.c`): SHA-256 of a version string, the size,
  the blocks' format and levels, and the stored bytes. `tide_pack_texture_blocks` already shared by content, so
  equal images are one block, one version in the draw list and one GPU upload; images that differ by a byte are
  two. Its first 8 bytes stay the image's version. The texture cache entry carries the key after its 20-byte head
  (layout word 2 in `texture_cache_key`, so every texture is prepared once more), so a build that reuses the cache
  hashes nothing. Exact equality, not a rule about alpha: a 16-bit alpha just under 1 rounds colors apart, and
  stays two. Two different files of the same pixels share too.
- **The no-alpha warning** the feedback proposed is moot with Raw compressed; not built.
- **Not built:** BC4/BC5 (two-channel normals: a Raw texture's `Sample` would read z as 0, which changes what a
  frag gets), BC7, per-asset quality, renormalized normal-map mips (the GPU's runtime mips didn't either).
- **Tests:** `texture_compression` e2e: Raw alpha image BC3 with unpremultiplied channels within 8 of the file,
  Raw opaque BC1, `[Lossless]` color exact and premultiplied, `[Raw, Lossless]` exact, small exact; the opaque
  pair is one block and version, the pairs with alpha are two. `tide_platform_shaders` (Vulkan and OpenGL)
  passes with the new keys, its Raw and color `hero.png` having clear pixels and so staying two.
  `compiler/tests/errors/lossless_*.tide` (on a grid, on a component, twice). The renderer's Raw passes use an
  8x6 image, below the size that compresses, so their exact-pixel checks stand.

## Idle images by the device's memory (2026-11-02)

Internal, from an investigation of frame hitches in the owner's Mount & Rifle (pinned 0.6.0-nightly.12, the
same engine as dev then). Its profiled route (release, hidden, 1280x720, seed 42, `--profile-warmup 200`) had a
mean CPU frame of 7.8 ms and a max of 88 ms, the render phase's max 77 ms.

- **The cause** (temporary per-frame instrumentation of every backend call, of `vkAllocateMemory`, and of
  `texture_for`'s uploads and `forget_textures`' evictions; not committed). The game's `[Raw]` conifer maps
  are three 4096 by 4096 RGBA images, 89 MB each with the mips the renderer makes for Trilinear. The game draws
  every vegetation material once behind the camera at its menu (`vegetation_warmup.tide`), about 430 MB of
  images. At the next frame the idle byte budget, 128 MiB whatever the device, let most of it go: two of the
  conifer maps and 20 of the 1024 by 1024 maps at frame 3, the third conifer map at 122. So the forest's first
  appearance uploaded them again (frames 518 and 519: 64 MB each copied into the ring, 4 allocations of 298 MB,
  mips), they went again at 525 after one frame out of view, and came back at 817 and 818 (render 22 to 40 ms,
  mip generation 13 to 31 ms of CPU, nearly all `vkAllocateMemory`: 5 allocations, 554 MB, 18.7 ms, the
  textures, their copies with room for mips, and the upload ring doubling to 256 MB), then a wait in the next
  present while the GPU copied. The 1024 maps came back at 850. On a busy machine the same allocations took
  127 ms once.
- **The budget is now the larger of 128 MiB and a sixteenth of the device's own memory**
  (`tide_texture_idle_budget`, `TIDE_TEXTURE_IDLE_SHARE` in `platform/src/texture_residency.h`; the share is
  Claude's pick). `tide_gpu.device_memory` (`gpu.h`, optional) says how much: Vulkan's is the biggest
  device-local heap of a discrete GPU (`vk_device_memory`), 0 for an integrated GPU or a phone, whose memory is
  the system's. OpenGL, WebGL, WebGPU and the Unity recorder don't say, so they keep 128 MiB. On this RTX 4080
  SUPER that's 1,002 MiB of 16,045 MiB. `forget_textures` reads it every frame (`tide_texture_idle_bytes`), so a
  new device is weighed by its own. The count, the ten-minute age and the pressure order are unchanged, and
  active images are still outside the budget: it isn't a VRAM cap. The memory kept is the only cost: Mount &
  Rifle now keeps its warmed set (the profile's `gpu_memory` is that much higher while the forest is out of view).
- **Measured after** (the same route from a build of this checkout with the game as an external dir, release,
  without and with the change built side by side and run alternately twice each, warm pipeline and driver
  caches): CPU work max 28.2/25.1 ms before and 18.3/15.3 after, the render phase's max 20.0/16.4 and 3.2/2.5,
  means unchanged (6.7 ms CPU, 0.8 ms render). With the instrumentation on, nothing is evicted or uploaded again
  after the menu's warmup. The frames over 20 ms left are the script's five captures (readback and PNG),
  which the profile leaves out.
- **What else the hitches were, not changed here:**
  - Pipelines. The owner's baseline was a new build, so its pipelines weren't in `pipelines.vulkan` yet: a cold
    run here compiled 38, 12 to 709 ms each, five of them at frame 971 (360 ms together) and six between 832
    and 846. A run with no window never prewarms (`notes/rendering.md`, Pipelines made before they're drawn),
    so a profile of a new build hitches where a window's second run, with its keys file, wouldn't. Prewarming
    in a `--profile` run would make its first frames like a player's; not built (a behaviour of headless runs
    that note decided).
  - Vulkan makes a texture that's read Trilinear twice: `vk_texture_make` with one level, then `vk_mipmaps`
    makes another with room for its levels and copies the first into it, as WebGPU's backend does. For a 4096
    image that's 64 MB and then 85 MB more of `vkAllocateMemory` and a GPU copy. Making it with its levels when
    the renderer knows a Trilinear draw reads it would save that; `gpu.h`'s `texture` has no levels to say it
    with. Not built.
  - The first upload of a big uncompressed image is synchronous: 64 MB is about 4 to 9 ms of copying into the
    ring. A game that draws its images during loading (as Mount & Rifle does) pays it there. Asynchronous
    uploads remain a gap (Automatic compression, below).
  - The upload ring grows to the most a frame has uploaded and never shrinks, so its doublings cost once.
- **Tests:** `resident_pack_textures` (`platform/tests/draw.c`) checks the budget's values (0 and 1 GiB keep
  128 MiB, 16 GiB keeps 1 GiB, no overflow) and that the renderer weighs by its device's.
  `resident_large_pack_textures` draws two 4096 by 4096 Trilinear pack images (128 MiB, 170 MiB with mips),
  skips them for two frames, and draws them again: nothing is sent again, their memory stays counted, and
  they go with the pack. It runs where the device's budget has room (here, Vulkan), and says it's skipped
  elsewhere. With the old fixed budget it fails at frame 3 (64 MB sent again).
- **Validation** (Clang 23.1.2, Node 24.22.1, `TIDE_HIDDEN`): Windows ASan, all four `tide_platform_draw` tests
  (Vulkan with its device-loss pass, OpenGL, OpenGL's legacy vertex path, Vulkan descriptor sets) and the
  targets, shaders, prewarm and skinned tests on Vulkan and OpenGL; web debug, all four draw pages (WebGL,
  WebGPU and both RGBA fallbacks). Android (whose Vulkan builds `vk_device_memory`), Linux and macOS are CI's.
  The game wasn't changed; its gap is recorded in its own notes.
- **How it was measured, for next time:** a scratch folder whose `CMakeLists.txt` globs the game's `.tide` and
  `.c` files (hidden folders left out) into `tide_add_game(... PACKAGES Rooms <packages/rooms/rooms.tide>
  DEFINES PERFORMANCE_TRACE)`, given to the `release` preset as `-DTIDE_EXTERNAL_DIRS`, builds an owner's game
  against the checkout, so the renderer can be instrumented; the game's own route script copied beside it keeps
  its captures out of the game's folder.

## Authored foliage mip coverage and portable full chains (2026-11-02)

The owner asked to apply established foliage techniques without losing distant
leaf coverage. The scoped asset route is authored, uncompressed DDS mipmaps;
there is no shader alpha heuristic, new Tide syntax, or generated runtime call.
This extends the existing immutable texture-block trailer with RGBA8 format 4.
Ordinary PNG/PSD/JPEG/TGA/BMP assets keep their existing import/compression path.

- `image_file_read_mips` accepts a legacy DDS_HEADER/DDS_PIXELFORMAT containing
  one tightly packed 2D RGBA8 or BGRA8 chain. Both base sides must be powers of
  two from 4 through 16384, and the exact mip count and payload must describe
  every level through 1x1. Pixel flags, channel masks, FourCC, bit depth, pitch,
  depth and cube/volume caps are checked before allocation. DX10/compressed
  DDS, arrays, volumes, cubes, padding, truncation and extra bytes fail with
  export guidance. Dimensions/size arithmetic is bounded before allocation;
  source bytes and decoded float base plus retained RGBA chain each fit the
  existing 512 MiB image-import budget. Signatures take precedence over names.
- The file cache owns independent decoded base colors and straight RGBA mip
  bytes, freeing both on invalidation. Texture packing premultiplies RGB by
  alpha separately at every authored color mip. `[Raw]` keeps all four bytes
  independently. Textures/lists/maps retain the chain alone in the pack;
  grids and cubemap faces use only the base pixels. Runtime Decode stays PNG
  only. Authored chains bypass automatic BC encoding and cost a full RGBA
  chain (about 4/3 of base RGBA) in the pack and GPU.
- Native OpenGL/Vulkan, WebGL2 and WebGPU upload every RGBA level through the
  existing private `texture_blocks` factory; no compression extension is
  required. Immutable resident mip state prevents later GPU mip generation
  from replacing the supplied levels. Unsupported-BC fallback now decodes
  **every** original BC mip into a temporary RGBA8 chain, uploads it, and frees
  it synchronously. An explicit exact RGBA base fallback, when present, still
  supplies level zero. Upload/residency accounting counts the actual full
  representation. Unity's current draw recorder has no block factory and
  retains only the base RGBA image; authored Unity mip preservation is not
  implemented by this pass.
- `tide_texture_blocks_decode_level` validates the entire descriptor before
  writing and clips partial 2x2/1x1 BC tail blocks to the destination extent.
  The existing base decode helper delegates to level zero. Trailer/Texture
  layouts, pack bounds checks, borrowed draw-list lifetimes and generated
  calls stay unchanged; RGBA8 is another validated immutable representation.
- Offline `bench/forest/alpha-mips.mjs` exports
  `writeCoverageDDS(path,width,height,rgba,cutoff,options)` and
  `writeMipDDS(path,width,height,rgba,{raw})`, plus pure build/encode helpers.
  Sources are straight RGBA Uint8Arrays, POT4..16384, bounded to 128 MiB of
  source bytes. Color filtering averages premultiplied RGB then unpremultiplies;
  raw filtering averages independent channels. Every mip is filtered before
  scaling, avoiding correction feedback. The coverage helper uses the original
  `alpha >= cutoff` coverage and chooses the closest achievable uniform-alpha
  scale in byte space at every mip. A tiny offset inside a byte-rounding
  interval avoids a floating-point half-byte boundary losing passing texels.
  Tied alpha values and finite texel counts make exact coverage impossible
  for some mips; there is no promise of identical silhouettes at 1x1.
- Atlas options `{tileWidth,tileHeight}` preserve each view's original coverage
  independently while that tile still has at least one texel in both axes.
  Smaller mandatory full-atlas tails use aggregate coverage. This does not
  solve view bleeding at coarse LODs: the baker/sampler still owns gutters and
  atlas coordinate/LOD choices. The coverage algorithm follows Ignacio
  Castano's [Computing Alpha Mipmaps](https://www.ludicon.com/castano/blog/articles/computing-alpha-mipmaps/);
  DDS layout follows Microsoft's legacy DDS header/pixel-format documentation.

Focused regression coverage: specification-written RGBA/BGRA rectangular DDS
chains, unsupported shapes/headers, exact-allocation cuts and byte mutations;
RGBA and clipped BC tail decoding against an independent oracle; Raw/color
pack bytes and premultiplication at every mip in `texture_compression`; actual
authored mip-2 pixels, BC fallback, residency and upload counters in
`platform/tests/draw.c`. The offline algorithm's four Node 24.22.1 tests pass,
including independent exhaustive finite-coverage oracles for full images and
per-view atlas tiles, color/raw filtering and complete DDS layout. Generated
release web runtime matches source. Central native/web compilation and GPU
validation are coordinated centrally; no timing claim is made here. Initial
native/web builds succeeded. The first native DDS run exposed diagnostic
priority for a non-POT width whose pitch also differed: POT validation now
precedes the independent pitch check, retaining both errors and the existing
strict expectation. The first WebGPU RGBA-fallback run completed every draw
check and reported result 0; CTest rejected only its old base-only upload
marker (65536 instead of the correct full-chain 87380 bytes). Its CMake marker
is updated centrally. The repeated native image/runtime checks pass, and the
corrected WebGPU fallback passes in the 47-case integrated web foliage run.

## A grid's Texture keeps a 64-bit handle (2026-10-30)

A grid's handle is 64 bits now (notes/ecs.md, 64-bit handles), which
`image.version` can't hold beside the cells, the shifts and the sampling bits.
`tide_texture_of_grid` (`tide/mesh.h`) keeps the handle's low 32 bits where
they were and its high 32 in `image.width`, which nothing reads of a grid's
Texture (its size is the grid's: `tide_texture_size`, `see_grid`);
`tide_texture_grid` puts them back together. The 48-byte `tide_texture_ref`
is unchanged. The draw list's ids for lists, maps and grids of numbers
(`numbers_id`, `engine/src/mesh.c`) are a 63-bit mix of the handle and the
layout, where they were the 32-bit handle beside the layout, and a mesh made
along the way is `tide_handle_of(TIDE_IN_SCRATCH, 0)`, no world's handle.

## Packs keep compressed images alone (2026-10-30)

bannerlord-duel (notes/gaps.md) found its web pack going from 32 to 231 MB with
a dozen 1k-2k photographs, and four 4096 by 4096 RGBA atlases ending `tide
run` with "tide: out of memory". Both were the pack keeping every compressed
image's exact RGBA beside its blocks: 85 MB for one 4096 image with BC3, and
tidec's builder aborts (`block` in `engine/src/pack.c`) past the 512 MiB a pack
handle reaches (`TIDE_PACK_BASE`, tide/text.h), which their pack passed.

- **tidec** (`pack_item`) gives `tide_pack_texture_blocks` no RGBA when
  compression made blocks; its content key flips another byte too, as this is
  another representation. Uncompressed images (small, not a power of two,
  `[Raw]`, cubemap faces) are as they were.
- **The block** of one: the size, then the existing trailer (magic, format,
  levels, bytes) and the blocks, with `next` exactly `8 + 16 + bytes`. With
  RGBA, `next` counts the RGBA too and is never that, and an uncompressed
  image's is 0, so `texture_at` (`pack_load.c`) tells them apart. A
  blocks-only image's `tide_pack_texture_find` has `pixels` NULL (its size is
  as before), its `tide_pack_texture_blocks_find` the blocks, and it's no
  cubemap face. Old layouts still load.
- **A device without BC** (`gpu->texture_blocks` absent or failing: phones'
  OpenGL ES and WebGPU, WebGL without S3TC, Unity's backend) gets the blocks'
  largest level decoded to premultiplied RGBA in `texture_for`
  (`platform/src/platform.c`), uploaded as an ordinary image and freed; such a
  texture makes its own mips for Trilinear as any uncompressed one does. The
  decoder is `tide_texture_blocks_decode` (`tide/texture_blocks.h`, inline):
  BC1's four and three-color blocks, BC3's eight and six-level alpha with its
  color block always four colors, thirds and sevenths rounded to the nearest.
  It checks the blocks' size against the image's before writing anything.
- **Measured:** seven 4096 textures (six BC3, one BC1) pack into 145 MB; the
  same with RGBA beside them was past 512 MiB and tidec aborted.

Tests: `pack_textures_of_blocks_alone_have_no_pixels_and_their_blocks`
(tests/test_pack.c: no pixels, whole blocks, a damaged `next` or trailer no
blocks, no cube of it, an image given neither stays zeros);
`texture_blocks_decode_to_the_first_level_as_the_formats_say` (within 2 of the
independent decoder in texture_compress_test.c over both smooth fixtures,
hand-made three-color BC1 and six-level BC3 blocks, wrong sizes, formats and
counts writing nothing); `compressed_pack_textures` in `platform/tests/draw.c`
now draws each format as blocks alone too, with its pixels and upload bytes;
`texture_compression` e2e reads the decoded pixel.

## Depth dimensions (2026-10-29)

The Render3D resolution increase needs physical shadow dimensions after the
device clamps a target. `DepthTexture.size` now completes the existing image
metadata API: an int2 in local CPU code and shaders, including depth-only and
color-target depth. Retained references follow resize/dispose; invalid, window,
cube-face and default references remain zero. A missing frame resolver is zero.
Metadata works before allocation or multisample resolve and never reads pixels.

Generated code reuses `tide_texture_size` with a temporary texture reference
marked by `TIDE_TEXTURE_DEPTH` in image.height. Only its color-index check is
bypassed; target resolution, lifetime checks and the borrowed target_pixels
callback are unchanged. No runtime function, signature or layout was added.
READ_SIZE still supplies uniform ints without texture bindings. CPU evaluation
order and the direct/transitive parallel-loop restrictions match Texture.size.

`depth_size` covers both target kinds, missing/invalid references, helpers,
resize/dispose, source order, automatic loops, callback restoration, no callback,
zero texture captures and a simulated 2048-pixel device limit. Three diagnostics
cover assignment and direct/transitive parallel reads. Existing platform shader
pass 23 checks CPU/vert/frag parity, multiple depth helper sources, real device
limits on an oversized metadata-only target and web device recreation. The LSP
fixture covers completion, chained vector members and hover. The combined
native scope passes 31 CTests, including metadata/errors and Vulkan/OpenGL
shader pixels; lsp_textures_in_functions passes. The corresponding 38 web
CTests pass, including WASI metadata/error checks and WebGL, ANGLE D3D11 and
WebGPU shader pixels. The docs production build passes. Final validation and
local coverage limits are recorded with the Render3D change in render3d.md.

## Twelve shader slots (2026-10-25)

The internal draw capacity is twelve (sixteen since 2026-11-05: notes/rendering.md, Sixteen textures a draw). Individual Texture references retain
their existing four sampling bits, Raw role and ownership. A run's sampling
value is uint64_t: slots 0..7 retain their original low bank and slots 8..11 use
the same layout in the high bank. Existing filters, wraps, mip preparation and
binding identity remain unchanged. The coordinated drawing/backend/Unity ABI
and high-slot validation are recorded in drawing.md and rendering.md. Native
Vulkan/OpenGL, WebGL2, Direct3D11 ANGLE and WebGPU comparisons passed for all
filter/wrap combinations in slots 8..11, including WebGPU device recreation.

## Raw Texture fields (2026-10-25)

The owner approved extending [Raw] to Texture fields using the existing raw-grid
pattern. Classification is static, with ordinary Texture references and sampling
bits. Numeric pack bytes already stay raw; grid-backed references are captured
through tide_texture_grid and tide_draw_grid_texture_raw, preserving channels
even at alpha zero. There is no runtime role, new function or ABI. Metadata reads
do not capture pixels and remain zero for absent resources. Static role checks
prevent color/raw mismatches and unannotated escapes; read-only GPU helpers keep
their source's role through specialization. Alias and attached-field placement,
grid reference lifetimes and sampling remain ordinary behavior.

raw_texture_fields records singular asset defaults/assignments, raw grid assets,
Tide-owned numeric grids, field forwarding, aliases, attached fields, empty size,
alpha-zero channels, filter/wrap/mip flags and pack unload/reload with retained
references. Its golden and HLSL checks cover GLSL/WGSL numeric reads and helper
forwarding. Existing Raw diagnostics remain, with new field-flow, whole-expression
destination and overloaded CPU grid-escape fixtures. The former negative
raw_on_texture_field case is replaced by the color-default mismatch fixture.
Role-change migration is described and tested in hot-reload.md.

Focused native validation passes 34 CTests (3.08 s), including all six capture/
pack/migration cases, 28 Raw diagnostics, layout identity, GLSL/WGSL goldens and
HLSL `/WX`; the focused Raw LSP protocol case passes too. The stock Render3D
pipeline consumer regenerates and passes Clang strict C17 with warnings as errors.
Full native/web and pixel validation belong to the combined integration.
The final grid-source metadata capture correction also passes the six affected
E2E/layout/golden/HLSL checks (1.55 s), including exact captured helper dimensions
for local and asset grids and missing-pack metadata. Its standalone generated-C
probe now compiles under the same strict C17 flags.
The full scoped Raw review and a separate fresh review of the final metadata
correction are clean. The latter inspected conversion, capture assertions,
shader goldens and role-layout checks after the final source freeze.
Combined native validation found the cubemap_asset_raw fixture's exact usage-note
expectation still omitted Texture fields. It now pins the complete updated note
while retaining the same Cubemap rejection; no compiler diagnostic changed.

## Texture dimensions (2026-10-18)

The owner's request to clear conventional API gaps adds read-only
`Texture.size: int2` in local CPU code and shaders. It is base-level physical
pixels, with `(0, 0)` for missing/unloaded/invalid/disposed images. Packed names
resolve on every query; C pixels use their dimensions without touching bytes;
grid references use the existing bounded `see_grid` validation, including the
16384-side limit and rejection of open axes. No pixels are captured, allocated
or uploaded by a query. A stale grid reference keeps its existing bounded
handle semantics rather than gaining lifetime ownership.

Targets resolve their current managed state, selected color validity and size,
so retained aliases follow resize/dispose. During a generated frame the runtime
borrows `draw.target_pixels`, a host callback returning the actual extent under
that frame's window/device. The standard host supplies
`tide_platform_target_pixels`, reusing `target_extent` (rounding, scale, window
factor and aspect-preserving device clamp). Its limit query is cached per GPU
epoch. Without a callback target metadata is zero; custom offscreen hosts must
provide the resolver for their own draw root. The thread-local borrowed callback
is restored after a frame, including nested frames; no platform pointer is
stored in simulation state or permanently registered across reload libraries.

`texture_size` checks assets/Raw assets, grid and helper reads, attached-local
shader sources, no pixel captures, retained target aliases, context restoration,
resize/dispose and pack unload/reload. CPU ordering tests read through an
immutable alias directly and through helper chains before a later argument
resizes the same target. Metadata reads and
their helper callers retain source order beside C/resource operations.
Automatic grid foreach reads remain on the frame thread, including helper
chains. Explicit parallel steps precompute the int2 before the loop; direct and
transitive reads diagnose that requirement. No renderer callback runs on workers.
Platform shader pass 23 checks matching CPU/vert/frag values, missing images,
multiple size-only helper specializations,
Raw metadata, numeric event forwarding and scaled window targets. Its shader
uses zero texture bindings, and device recreation repeats the web checks.
Final validation is recorded with the language completion pass.

Contextual enum shorthand (2026-10-11): the compiler's fixed Sample argument
checker supplies Filter/Wrap types to `.Point`, `.Trilinear` and `.Repeat`,
including named arguments in written order. Texture/grid/cubemap Sample keep
their existing shader-binding, constant-member and trilinear restrictions.
The declared/cubemap shader fixtures preserve their existing GLSL/WGSL goldens.
There is no sampler, runtime or GPU contract change; see `notes/language.md`.

## Automatic compression and bounded GPU residency (2026-10-11)

- The owner selected GPU compression/residency for larger scenes and explicitly chose "Compress ordinary color textures automatically". No language syntax or generated runtime call changes. Current eligibility is an implementation choice: ordinary color `Texture` assets, including lists/maps, whose dimensions are powers of two and both at least 128. Small/non-power-of-two images, `[Raw]`, cubemap faces, grids and targets stay uncompressed.
- `compiler/src/texture_compress.c` uses pinned stb_dxt 1.12 in high-quality mode at build time only. Existing RGBA byte conversion and display-space premultiplication happen first. All-255 alpha selects BC1; any other alpha selects BC3. Integer box filtering builds every mip down to 1x1, with clamped 4x4 tail blocks. Compression is lossy and visual only; numeric data must keep `[Raw]`. The smooth-gradient fixtures measured maximum channel error 7/255 and mean absolute error 1.15517 (BC1) / 0.959274 (BC3); those are fixture measurements, not arbitrary-art quality bounds.
- Exact premultiplied RGBA remains in the pack for unsupported devices and existing C readers. An optional trailer carries magic, format, mip count, byte count and blocks; the texture block's `next` bounds its payload. `tide_pack_texture_blocks_find` validates dimensions, complete mip sizes, counts and file-region bounds before exposing a borrowed pointer. Missing/malformed metadata gives RGBA fallback. Compressed/uncompressed representations have distinct content versions. Pack bytes and CPU asset memory increase; download/CPU-memory compression is not claimed.
- The draw list borrows both representations until the pack is replaced/unloaded; append propagates the block reference. The private backend factory uploads complete immutable BC mips when supported, otherwise creates the ordinary RGBA image. OpenGL/WebGL query S3TC formats; WebGPU requests optional `texture-compression-bc`. GPU accounting counts the actual full block chain. Point/Bilinear still sample the base level; Trilinear uses the prepared chain without runtime generation. Epoch changes recreate compressed GPU objects rather than writing RGBA into them.
- Only immutable pack image/cube GPU identities survive inactive frames. The idle budget is 128 MiB (since 2026-11-02 a sixteenth of a discrete GPU's memory where that's more: Idle images by the device's memory, above), 4096 images, and at most `TIDE_TEXTURE_IDLE_FRAMES` rendered lists since last use: 36,000 (ten minutes at 60 Hz) since 2026-10-20, previously 60, which re-uploaded GRAVEWAKE's boss animation every time one rose and hitched on the web. The byte/count budget is what bounds memory. `tide_texture_idle_frames` (platform.c) holds the value; the residency test in `platform/tests/draw.c` sets it to 60 to see expiry and restores it. Active images are outside this budget; it is not a total VRAM cap. Pressure prefers the most recent images that fit, with stable ties and overflow-safe subtraction. C pointers, grids, shader data and targets retain their existing lifetimes. The GPU cache retains ids/metadata only, never borrowed CPU pack pointers. Device epochs clear it; draw epochs retire stale inactive images.
- Renderer fixture measurements on this Windows RTX 4080 SUPER: 128x128 BC1 uploaded/accounted 10,936 bytes, BC3 21,872, both including all eight mips, versus 65,536 RGBA base bytes. Relative to a full RGBA mip chain (87,380 bytes), those are approximately 8x/4x smaller. Returning after a hidden frame uploads zero bytes; expiry after 61 unused lists releases the entire allocation. These are upload and engine GPU-accounting measurements, excluding driver allocation overhead, not FPS claims.
- Coverage: compiler compression tests use an independent decoder and repeat-build comparison; `texture_compression` E2E checks automatic opaque/alpha imports, exact Raw/small channels and append. Pack tests reject corrupt metadata without changing RGBA. Renderer tests cover pixels, premultiplied alpha, point/trilinear reads, upload/memory accounting, residency expiry, byte/count pressure, unload/draw epochs, forced RGBA fallback and WebGPU device loss. Adapter mocks check BC with/without timestamps, timestamp fallback preserving BC and explicit disablement.
- Validation: full native/web debug builds and suites exercised 2081/2053 tests. Initial failures came from new PNGs entering an existing recursive-glob fixture, an empty optional-feature request descriptor, and forwarding the page's fallback query as a C test argument. Those were corrected, fixtures/pages rebuilt, and all seven affected native checks and sixteen web asset/runtime/renderer/timing/shader/skinning/thread checks passed; every other full-suite case passed. The production docs build passed with existing allowed missing-example placeholders. All native runs used `TIDE_HIDDEN=1`; browsers were muted. No CI or deployment was requested.
- Merge validation: incoming `dev` editor completions/playback hints and unbuilt LOD design were retained. Two top-of-note insertion conflicts preserved both entries; the spec merged cleanly. Rebuilt the changed compiler dependency, editor tests and affected asset/animation/render targets. Eleven native and eight web dependency checks passed, including all editor transports, automatic compression, CPU/default-pose sampling and skinned pixels on both web backends. The merged production docs build passed.
- Remaining gaps: ETC2/ASTC/mobile-specific compressed variants, compressed DDS/KTX imports (authored RGBA8 DDS is built above), compressed cubemaps/non-power-of-two images, per-asset quality controls, smaller CPU/download representation (built 2026-10-30: Packs keep compressed images alone, above), asynchronous uploads, mip streaming, total-memory budgets and virtual texturing. No streaming system is scaffolded here. Android and macOS hardware coverage remains CI/device work.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Rendering and platform

- **Finding a retained texture is indexed** (`engine/src/draw.c`, `tide_draw_list.texture_index` in `tide/draw.h`): a table of slot numbers, keyed by space/id and at most half full, covers memory textures, grids, shader data lists, pack images/cubes and target color/depth/cube images. It leaves the texture array and returned slots unchanged. Multiple target sizes/versions for one id are indexed in array order and still matched individually. Ordinary textures keep their place when version, size or format changes. Frame pruning only rebuilds after removing/compacting entries; `tide_draw_forget` clears the retained allocation and `tide_draw_free` frees it. `draw_texture_slots_survive_growth_and_frame_pruning` and `draw_target_texture_versions_survive_growth_pruning_and_append` cover growth, space separation, reverse lookup, changed versions/formats/sizes, compacted slots, append and forget; existing pack texture/cubemap tests cover their borrowed pixels. C users must rebuild with the changed draw-list layout.
  - **Measured 2026-10-08, Windows, no GPU:** a warmed frame looking up 1,000/2,000/4,000/8,000 unchanged 1x1 textures, median of seven passes, excluding reset. Both original and indexed `draw.c` compiled with the same strict `-O2` flags and linked against the same debug engine dependency library: 0.179/0.786/3.252/14.176 ms before, 0.006/0.012/0.025/0.059 ms after. The prior scan grew quadratically; the indexed lookups grow with the number requested.
    - The same `-O2` harness linked to the saved original and rebuilt normal debug engine libraries: 0.427/2.078/8.099/32.934 ms before, 0.018/0.036/0.078/0.160 ms after, measured while other work was active. The isolated optimized comparison above keeps the dependency library identical.

- **What a `uv` past a texture's edges reads is said where the texture is sampled**, as the filter is (agreed 2026-10-07: "sure, i dont have opinions here, seems grounded"; built the same day; the enum's name, its members and each rule are Claude's picks, Provisional in `docs/spec.md`, Views and drawing, `Wrap`): the built-in enum `Wrap`, with `Clamp` (what a call without one has, and what every draw did), `Repeat` and `Mirror`, one for both axes, after the filter wherever the filter is an argument: `Draw.Mesh(vertices, indices, texture, Filter.Point, Wrap.Repeat)`, `Draw.Mesh`, `Draw.Instanced`, a target's `Mesh` and `Instanced`, and a frag's or vert's `source.Sample(uv, Filter.Point, Wrap.Repeat)`. What's built, and what code relies on:
  - **It's a sampler's, never a texture's or a shader's.** A wrap is no part of a shader's text (`compiler/tests/shaders/wrap` pins that), of a pipeline or of a texture's GPU copy: it's how one of a draw's textures is read, beside its filter. So a shader's `id` and its pipeline are the same whatever its wraps, and one grid or one target's color is clamped in one draw and repeated in the next.
  - **`gpu.h`:** `sample(slot, texture, blended, wrap)`, with `tide_gpu_wrap` (`TIDE_GPU_WRAP_CLAMP`, `_REPEAT`, `_MIRROR`), whose numbers are `tide_wrap`'s (a `_Static_assert` in `platform/src/platform.c`, where `gpu_wrap` makes what isn't one clamp). OpenGL's is six sampler objects, by filter and wrap (`sampler_for` in `gpu_gl.c`), with `GL_REPEAT` and `GL_MIRRORED_REPEAT` in `gl.h`: no function it didn't call, and no extension, as textures of any size wrap in OpenGL 3.3, OpenGL ES 3 and WebGL 2. WebGPU's is six samplers in `tide.js` (`addressModeU` and `addressModeV`), at 2 times the wrap's number plus whether it blends, which a bind group's key has for each texture: a change to the enum goes there, as `depths`' does. A texture's own parameters stay clamped and nearest; the unit's sampler stands in for them.
  - **The draw list:** a mesh's command has its wrap beside its filter (`tide_draw_command.mesh.wrap` and `mesh3.wrap`: the 32 bits `filter` had are 16 each, so a command is the size it was), and a run of meshes goes on while texture, filter and wrap are the same, in the list (`tide_draw_triangles_with`, `mesh_instances`) and in the platform's steps (`draw_step.wrap`). A shader's run says how each of its textures is sampled in one word, `tide_draw_run.sampling` (it was `blended`): bit i for texture i read blended, and the two bits from `TIDE_SAMPLING_WRAP` (8) + 2i its wrap (`tide_sampling_wrapped`, `tide_sampling_blended`, `tide_sampling_wrap`), so a word with only its low bits set clamps every texture, and the word generated C passes for a game with no wrap is the text it was.
  - **C:** `tide_wrap` (`tide/draw.h`), with `tide_wrap_of`, which makes any number one, as a Tide enum's value can be any. The functions that take a `tide_texture` have a twin that takes a wrap after the filter, and clamp themselves (`tide_draw_mesh_wrapped`, `tide_draw_triangles_wrapped`, `tide_draw_mesh_3d_wrapped`); the ones in steps, with what `tide_draw_texture_slot` gave, take it (`tide_draw_triangles_with`, `tide_draw_instance`, `tide_draw_instances`). Twins and not another parameter for the first three, as tests and games' C call those everywhere.
  - **Generated C** has the enum in a game's header only when its code names it (`prog->wrap`, `decl.named` in `find_in`, `enum_generated` in `compiler/src/codegen.c`, as `Depth`). A draw's filter and wrap are no arguments of a C function any more: they're put on the texture it's given (`tide_texture_filtered`, `tide_texture_wrapped`), which every textured draw then reads its sampling off (see `Texture` is the one type, below, which changed this on 2026-10-07: `tide/mesh.h`'s `_wrapped` twins and a shader function's `tide_wrap` parameter are gone).
  - **tidec:** the three textured mesh signatures (2D, 3D, 3D instances; one each since a grid is a `Texture` there) have two parameters a call can leave out (`sampled` in `compiler/src/builtins.c`), which editors show as `Filter filter = the texture's, Wrap wrap = the texture's`. `check_mesh_draw` says a wrap that isn't after a filter before either path takes the call (the engine's own vertices and a shader's), and a second one; a draw through a frag takes both too, as its texture's for that draw. `check_grid_sample` takes `uv, filter, wrap`, by name too, each of the last two written as an enum's member: the GPU is told before any pixel runs. In a frag or a vert, each filter and wrap a texture is read with is a texture of its draw's 8 (`shader_sample.wrap`, `shader_texture.wrap`), and the same way twice is one.
  - **The name is the engine's**, as `Blend`'s and `Depth`'s are: a game's own type or function called `Wrap` outside a namespace is an error. The asteroids sample's helper was one, and is `Around`.
  - Blended, a pixel at a texture's edge is blended with what the wrap says is past it. A render target's image is the other way up on OpenGL, which changes nothing: each wrap reads the same turned over.
  - Not built: a wrap for each axis and a border's color. (`gpu.h`'s `sample` takes a filter where this says `blended`, and the samplers are nine, since mipmaps: below.)
  - Tests: `compiler/tests/e2e/wrap` (what each draw records: the engine's vertices in 2D and 3D, instances, a target's color, into a target, a wrap as a value and of a number, a shader with no frag whose draws say one or none, a frag's and a vert's samples by their run's word, and C's functions); `compiler/tests/errors/gpu_sample_wrap*.tide`, `draw_mesh_wrap*.tide`, `draw_mesh_two_wraps.tide`, `texture_shader_draw_wrap.tide` and `wrap_*.tide`, one a diagnostic; `compiler/tests/shaders/wrap`; `lsp_wrap`; `wraps` in `platform/tests/draw.c` (each wrap's pixels with the nearest pixel and blended, before 0 and past 1, one texture read two ways in one draw through a shader, 3D meshes and their runs) and pass 7 of `platform/tests/shaders.tide` with `run_wrap_checks` in `shaders.c` (from Tide: mesh draws, frags, a vert, a target's color, a list's instances), the same pixels on OpenGL 3.3, WebGL 2 and WebGPU, where the device is then lost and found again. The docs' `wrap` example shows the three (`docs/game/meshes-and-textures.md`, Past the edges).
- **A texture's declaration can say how it's sampled** (agreed 2026-10-07, of `[Filter(Filter.Point), Wrap(Wrap.Repeat)]` before a `Texture` asset and a `Grid2<Color>` asset: "also add those attributes for both"; built the same day; where else they go and where a declaration's say ends are Claude's picks, Provisional in `docs/spec.md`, Views and drawing, Declared sampling). It's a default for where a draw or a `Sample` that names the texture leaves its filter or its wrap out, each apart. What's built, and what code relies on:
  - **Where:** before an asset that's a `Texture` or a `Grid2<Color>`, alone or a glob's (`check_sampling_attribute` in `compiler/src/check.c`: `decl.sample_filter` and `sample_wrap`, 1 + the member's number), and before a field that's a `Grid2<Color>`, a `Grid2<float>` or a `RenderTarget`, for its color images (`check_field_sampling`, whose value stays in the field's attribute; the fields weren't asked for). An asset's are read with its declaration (`check_asset_decl`), before any code is checked, as the shaders are planned from them; `check_attributes` only says what a declaration that's no asset can't take. A `Texture` field refuses them with an error of its own: it holds whichever texture it's given, which says how it's sampled itself.
  - **It's the sampling the `Texture` made there is born with, which the value carries** (2026-10-07; until then it was resolved as code was compiled, by what a draw's texture expression named, and a `Texture` in a field had none). `declared_sampling` in `compiler/src/shader.c` still reads what an expression's declaration says (an asset by its name, one of its list or map, the variable of a `foreach` over it, a grid's field, a `RenderTarget` field's `.color` and `.colors[i]`), and codegen puts it into the value where the value is made (`declared_sampling_bits`): an asset `Texture`'s global as the pack is loaded (`tide_texture_sampled` in `tide_assets_load`, and `tide_pack_sample_textures` for each of a glob's, which the loader laid out), a grid where it converts (`tide_texture_of_grid`'s third argument, in `gen_as`), and a target's color where it's read (`tide_texture_sampled` around `tide_render_target_color`).
  - **A draw** reads its texture's sampling off the value (`tide_texture_filter`, `tide_texture_wrap`, in `engine/src/mesh.c` and in a shader's generated function), and what the call says is put on the value first (`gen_draw_texture` in `codegen.c`).
  - **A `Sample`** of an asset by name or of a grid's field has its declaration's filter and wrap worked into its binding (`ex_sample`), so it's the binding it would be had the `Sample` said them: that much is still known as the game is compiled. **A `Texture` that's no asset's by name** (a frag's own `Texture` parameter, a `Texture` field) has no declaration there: what its `Sample` leaves unsaid is a binding of its own (`shader_sample.filter_said`, `wrap_said`, part of a binding's key), and the shader's `tide_shade_<n>` works that texture's own filter and wrap into the run's `sampling` for those bindings (`tide_sampling_filter_of`, `tide_sampling_wrap_of`, of `tide_texture` or of the field). So a shader's text and its id are the same whatever its textures say, and two draws of one frag with textures sampled differently are two runs. `shader.sourced`, `wrapped` and `source_grid` are gone.
  - **Editors:** both attributes are offered in the brackets before a declaration and before a field, with hovers, and the enum member each names is an occurrence (the checker fills the attribute's `qname`).
  - A game with neither attribute generates the C it did, byte for byte (checked with the Texture assets, above).
  - Tests: `compiler/tests/e2e/asset_textures` (each place a declaration is named, a call that says one or both, a value through a field, a shader with no frag, samples by name, a frag's source from three kinds of draw, blits), `compiler/tests/shaders/declared`, `compiler/tests/errors/sampling_*.tide` (one a diagnostic), pass 10 of `tide_platform_shaders` for the pixels on each backend, and `lsp_asset_textures_and_declared_sampling`.
- **`Texture` is the one type a draw's texture and a `Sample` take** (item 3 of the reshaping, agreed 2026-10-07 and built the same day; that grids of colors and of floats convert, that a `Texture` carries its filter and wrap, and that `[Filter]` and `[Wrap]` are what one is born with are the owner's; each rule below is Claude's pick, Provisional in `docs/spec.md`, Views and drawing, Textures). `docs/game/meshes-and-textures.md` (Textures, A Texture is a value, One channel, Saying it once) and `docs/game/shaders.md` (Textures) are the users' pages. What's built, and what code relies on:
  - **The conversion is assignability** (`type_assignable` in `compiler/src/types.c`, with `type_is_texture_grid`: a `Grid2<Color>` or a `Grid2<float>` to `TY_TEXTURE`), so it's everywhere a value goes where another type is wanted, with no list of places: a mesh's texture, a blit's source, a `Texture` field, local, parameter and `return`. `gen_as` (`compiler/src/codegen.c`) writes it, `tide_texture_of_grid(grid, &shape, sampling)`. The match needs no rule: nothing there has anywhere a `Texture` goes (the errors rendering resources always gave). A grid that's no texture where one goes is said by `check_mesh_draw`, `check_blit` and `check_grid_sample`.
  - **One textured signature for each of the three mesh draws** (`builtins.c`), whose `arg_want` is `Texture`; a shaded draw's texture argument wants one too (`check_mesh_draw`), and so does a blit's source. `plan` (`compiler/src/shader.c`) no longer tells a draw given a grid from one given a `Texture`: a game that drew one vertex struct both ways has a shader fewer (`compiler/tests/shaders/render_sources` lost its second `Copy`).
  - **A draw gives a texture to a frag that takes one** (a grid too, which used to be an error that said the frag samples the grids it takes itself), and **a draw through a frag takes a filter and a wrap** (which used to be an error): they're its texture's for that draw, which the frag's `Sample`s that say nothing then read. `compiler/tests/errors/texture_shader_draw_filter` and `_wrap` went with that.
  - **A grid's `Texture` is a name, in the 48 bytes a `tide_texture_ref` always was** (`tide/draw.h`, `tide/mesh.h`): no pixels, no target, `image.height` `TIDE_TEXTURE_GRID`, and in `image.version` the grid's handle (its low 32 bits), its cells (1 colors, 2 floats) and its chunks' shifts. `tide_texture_grid` reads it back, and `tide_draw_texture_source` then calls `tide_draw_grid_texture`, as a grid's own draw always did: the list's copy, versioned by chunk hashes, nothing to upload or free.
  - **Sampling is 4 bits of the reference** (`tide_texture_sampling`: bit 0 for the nearest pixel, the wrap from bit 2), the top of `image.version` (`TIDE_TEXTURE_SAMPLING_SHIFT`, 60), free there for a target's reference (whose `image` is otherwise zero), a pack image's (whose name is masked to `TIDE_TEXTURE_NAME`: `texture_name` in `engine/src/pack_load.c`, `pack_slot` in `draw.c`) and a grid's. A reference to C's own pixels keeps all 64 bits of C's version, so its sampling is in `target.size.scale`, as a whole number, which nothing reads where there's no target. So a zeroed reference, and one C makes with nothing said, are blended and clamped. A `tide_texture_ref` is compared or hashed nowhere; code that starts to masks the sampling.
  - **`tide/mesh.h` has one function for each textured draw** (`tide_draw_mesh_texture`, `tide_draw_mesh3_texture`, `tide_draw_mesh3_instances_texture`), which take the reference and nothing else. The grid ones and every `_wrapped` twin there are gone.
  - **A shader's function takes one `tide_texture_ref tide_texture`** where its draw gives a texture, and no filter, wrap or shape: `gen_shaders` writes each texture's source as before and the run's `sampling` as a constant for what's known as the game compiles, or'd with `tide_sampling_filter_of` and `tide_sampling_wrap_of` of the reference for what isn't (the draw's own texture in a shader with no frag: both; a `Sample` of a `Texture` parameter or field: what it leaves unsaid).
  - **Nothing ties a grid's `Texture` to the grid's lifetime**, and the heap's handles are offsets, so `tide_draw_grid_texture` trusts nothing a handle names: the directory, its records and each chunk are read through `span` (`engine/src/mesh.c`), which gives bytes only where they're all within one page of the heap (`tide_pack_span` for the pack's region), the shape's shifts are checked before a chunk's size is worked out (shifts of 15 made a chunk of 0 bytes and passed, which `compiler/tests/e2e/textures` found), a chunk's place is worked out 64 bits wide, and the directory has to be one a grid of that size could have (its chunk counts, and no more records than chunks). A side past `TIDE_GRID_TEXTURE_MOST` (16384) is no texture, which also keeps bytes that aren't a grid's from asking for gigabytes. A stale reference draws nothing or anything. It's part of what No Tide code can corrupt memory rests on (see Language): a read path that's added for a grid named by a `Texture` goes through `span`.
  - **A `Grid2<float>` is `TIDE_TEXELS_HALF` in the draw list** (`tide_draw_texture.format`, `tide_draw_texture_slot_as`: 2 bytes a pixel) and `TIDE_GPU_R16F` on the GPU (`platform/src/gpu.h`; `GL_R16F` with `GL_HALF_FLOAT` in `gpu_gl.c` and `gl.h`, whose WebGL upload takes a `Uint16Array`: `pixels` in `tide.js`; `r16float` in `tide.js`'s `gpu_texture`, by `tide_gpu_format`'s numbers). Picked over 32 bits because every backend blends a half between pixels with nothing asked, where a float of 32 needs `OES_texture_float_linear` on WebGL 2 and `float32-filterable` on WebGPU, whose bind group layouts would then depend on the texture a frag is given. So no device falls back. `tide_half_from_float` (`mesh.c`) rounds to the nearest, ties to even, with 65504 for what's past it and for an infinity, and 0 for NaN. A shader reads it as `(r, 0, 0, 1)`, which is what each backend gives a one-channel texture: no shader text depends on it. The platform's texture cache tells a texture by its format too (`gpu_texture.format` in `platform.c`).
  - **A hot reload keeps a `Texture` of the pack's or of a grid's**, and a target's with its sampling (`gen_resource_fields`): only one of C's own pixels, or of a target that isn't the game's, is zeroed. (It zeroed the pack's too before, which nobody had noticed.) After a migration to another layout a grid's `Texture` may name another place; nothing clears it.
  - **Generated C:** 20 of 246 games differ from what the compiler before wrote (old and new tidec over every game's source list, with `--layout` and without), all in their `.c` and none in a header, so no game's hash moved: textured draws convert their grid at the argument and call the one function, shader functions take one reference and work their sampling out, and reloads carry sampling. The 224 others are byte for byte what they were.
  - **Not built:** a way to change a `Texture`'s sampling in Tide outside a draw; a `Texture`'s size; grids of other cells and `Grid3` as textures; a `Grid2<float>` asset; clearing a grid's `Texture` when its grid goes or when a reload migrates; a live docs example of a float grid.
  - **Tests:** `compiler/tests/e2e/textures` (a grid as a texture drawn, kept, passed, returned, in 3D, instanced and into a target; sampling carried from a grid's field and a target's, and a draw's over it; frags whose runs carry it, one shader in three runs; a float grid's halves and the conversion's rounding; every offset of a heap, and past it, as a grid's handle), natively and under Node; `compiler/tests/e2e/asset_textures` (a kept asset draws as declared, in a mesh and in a frag); pass 12 of `platform/tests/shaders.tide` with `run_height_checks` in `shaders.c` (a float grid in a frag, with no frag, blended, numbers past 1 and under 0, a kept `Texture` with and without the draw's wrap) and pass 10's kept image, the same pixels on OpenGL 3.3, WebGL 2 and WebGPU, where the device is then lost and found; `compiler/tests/errors/` for the messages that changed; `lsp_wrap`, `lsp_meshes`, `lsp_asset_textures_and_declared_sampling`. Not run: Android, and OpenGL on a Mac.
- **Mipmaps are a filter, `Filter.Trilinear`** (item 6 of the reshaping, built 2026-10-07; that the drawing side gets mipmaps and that the card designs how a mipmapped sample is written where code samples in a branch are the owner's; each rule is Claude's pick, Provisional in `docs/spec.md`, Views and drawing, Mipmaps, with the options that were weighed). `docs/game/meshes-and-textures.md` (Drawn smaller) and `docs/game/shaders.md` (Textures) are the users' pages. What's built, and what code relies on:
  - **The filter's numbers**: `tide_filter` has `TIDE_FILTER_TRILINEAR` (2) after point and bilinear, with `tide_filter_of`, which makes any number one (what's none is bilinear). A mesh command's `filter` is that number, as it was. A texture reference's sampling has bit 1 for it beside bit 0 for the nearest pixel (`tide_sampling_bits`, `tide_texture_filter`; `tide_texture_filtered` replaces both and `tide_texture_wrapped` keeps both), so a zeroed reference is still blended. A shader run's `sampling` has bit `TIDE_SAMPLING_MIPS` (24) + i for texture i, set with its blended bit i (`tide_sampling_filtered`, `tide_sampling_filter`), so the word generated C writes for a game with no Trilinear is the text it was, and code that only asks `tide_sampling_blended` reads it as blended.
  - **`gpu.h`:** `sample(slot, texture, filter, wrap)` takes a `tide_gpu_filter` (`_NEAREST`, `_LINEAR`, `_MIPMAPS`, with `tide_filter`'s numbers by a `_Static_assert` in `gpu_filter`, `platform/src/platform.c`) where it took a bool, and `mipmaps(texture)` makes a texture's smaller images from its pixels as they are, outside a pass, for an uploaded texture and for a target's color image alike. Nothing keeps them up to date but calling it again.
  - **OpenGL** (`gpu_gl.c`): nine sampler objects, by filter and wrap, the mipmapped ones `GL_LINEAR_MIPMAP_LINEAR` drawn smaller and `GL_LINEAR` drawn bigger; `glGenerateMipmap` (in `gl.h` and `tide.js`, the one function this added). A texture with only its own image read through a sampler that reads the smaller ones is incomplete, and reads as black, so `gl_sample` gives such a texture the blended sampler until `gl_mipmaps` has run for it (`gl_texture.mipped`). OpenGL makes the images by drawing, so of a float texture only where it draws into floats (`gl_float_targets`): elsewhere a `Grid2<float>` has none, and is read blended.
  - **WebGPU** (`tide.js`): nine samplers at 3 times the wrap's number plus the filter's (a change to either enum goes there), the six that aren't mipmapped with `lodMaxClamp: 0`, so they keep to a texture's own pixels whatever it has and however a shader asks, which is what keeps every draw that isn't Trilinear the pixels it was. A WebGPU texture has as many images as it was made with, so the first `gpu_mipmaps` of one makes another with room for them, copies its pixels over, lets the bind groups that had it go, and keeps `first` (its own image, which a pass on a target draws into: a view of every level is no attachment) beside `view` (all of it, which draws sample). The levels are drawn one from the other by a pipeline for each format (`mipPipeline`: one sample between four pixels, blended), in one submit.
  - **The renderer makes them only when a draw reads between them and they're stale** (`prepare_mipmaps`, `prepare_step_mipmaps`, in the step loop after `prepare_source_targets`, with the pass ended around it as for that): an uploaded texture's once for each upload (`gpu_texture.mipped`, cleared in `texture_for`), and a target's color image's when a pass has begun on the target since they were made (`gpu_target.drawn`, counted where the loop begins a pass on it and where its image is made, against `mipped[image]`). So a texture never read so has none, one that doesn't change makes them once, and a frame with no Trilinear draw pays a comparison a step. `bench/results/draw.jsonl` (`mipmaps-before`, `mipmaps`) has the bench either side: nothing moved.
  - **The engine's own mesh shaders didn't change**: they already let the GPU pick (`texture()`, `textureSample`), in `main`, before any branch.
  - **In a frag, tidec asks for a Sample's derivatives as the frag starts, and reads with them** (`ex_sample`, `liftable`, `known_filter` in `compiler/src/shader.c`): a GPU only says how fast a `uv` changes outside branches and loops, and Tide samples anywhere. For each Sample that may read between smaller images, the frag's function begins with `tide_uv<n>` (the uv worked out a first time), `tide_dx<n>` and `tide_dy<n>` (`dFdx`/`dFdy`, `dpdx`/`dpdy`), collected in `emit.grads` while its body is written and put between its head and its body (`need_function`), and the Sample is `textureGrad` or `textureSampleGrad`. The uv is written that first time with `emit.lifting` set, where a local is written as the value it was declared with (`ex_name`). `compiler/tests/shaders/mipmaps` pins both languages.
  - **Which Samples**: one whose filter is known to be Trilinear (said there, or its grid's or asset's declaration's), and one that leaves its filter to the Texture it's given (`!filter_said`) in a game that names `Filter.Trilinear` (`program.names_trilinear`). Every other is `textureLod(..., 0.0)` as before, and so is every Sample in a vert.
  - **What a uv can be made of is `liftable`'s to say**: the frag's parameters that aren't `mut` or `out`, the pixel's fields through `this`, constants, members and swizzles of those, operators, `?:`, struct literals, constructors, the engine's functions, a game's functions that take nothing `mut` or `out`, a texture read with a filter known not to be Trilinear, and locals that aren't `mut`, a loop's, an `out` argument's or assigned again, whose own values are so. Anything else isn't. Where the filter is known, that's an error at the uv (`compiler/tests/errors/gpu_sample_trilinear_*.tide`); where it's the texture's to say, the Sample reads level 0. **What `liftable` allows is written twice in the shader, so it has to change nothing**: an expression kind that joins it is one with no side effect.
  - **`program.names_trilinear`** is set where a `Filter.Trilinear` member is resolved (`check_member`'s enum path, and an asset's `[Filter]` in `check_sampling_attribute`), never by `Filter(2)`. `codegen` hides the enum's third member in a game that doesn't name it (its count is cut to 2 as codegen starts, so tidels still offers it), and the shader writer goes by the same flag: such a game's generated C and shaders are what they were, byte for byte (old and new tidec over every game's source list, with `--layout` and without: 209 games, none differs). A shader's `filter` (`shader_sample.filter`, `shader_texture.filter`: 0, 1 or 2, where they had a bool) is a binding's key, as it was.
  - **Not built:** anisotropy (a sampler's setting on each backend; left until something wants it); a level or a bias a game picks; a Trilinear Sample at a uv a loop makes; mipmaps an asset brings, or made at build time; Duel's blur doesn't use it.
  - **Tests:** `mipmaps` in `platform/tests/draw.c` (a texture of stripes drawn an eighth and a quarter of its size reads as the mean of what each pixel covers, the same texture read bilinear beside it in the same frame doesn't, its own size is its own pixels, a 3D mesh's, and other pixels make them again), pass 14 of `platform/tests/shaders.tide` with `run_mipmap_checks` in `shaders.c` (from Tide: a declared grid sampled in a branch, a carried filter after a discard, one frag in runs read two ways, a Sample that says it, a uv a loop changes, meshes with no frag and with a vert alone, a grid of floats, and a target's color made again as it's drawn into), the same pixels on OpenGL 3.3, WebGL 2 and WebGPU, where the device is then lost and found; `compiler/tests/e2e/mipmaps` (what each draw records, natively and under Node); `compiler/tests/shaders/mipmaps`; the two error tests; `lsp_trilinear`. Not run: Android, and OpenGL on a Mac.
- Nothing but a render target (below) holds a GPU texture: a mesh names pixels where the game keeps them, and every layer keeps a copy it can make again. An image of the game's pack (an `asset Texture`: see Assets, under Tech stack) is the one whose pixels the draw list doesn't copy: it draws them where the pack has them.
  - The draw list copies a texture's pixels when they aren't what it has (`tide_draw_texture_slot`: whose they are, and a version), and the platform uploads from that copy (`texture_for` in `platform/src/platform.c`). Each lets a texture go once a frame draws without it.
  - Tide's own pixels are grids, `Grid2<Color>` and `Grid2<float>` (`tide/mesh.h`), local or the match's, which are `Texture`s where one is wanted (see `Texture` is the one type, below). Their version is a hash of their chunks' page hashes, which a heap keeps until a page changes (`tide_heap_block_hash`), so an unchanged grid costs a hash per chunk, not its cells. Asking counts as a move of the heap, so a chunk cache that held the page looks it up again and says it changed.
  - C's are `tide_texture`: its own pixels, and a version it changes when they do.
  - So reloads carry nothing. On the web a new program uploads again. On desktop the host has the list forget its copies when it swaps the game's code (`tide_draw_forget`, whose epoch the platform checks), as the new build's C may name other pixels at the same address and version.
  - **A texture a frame changes a little of costs that little** (built 2026-10-08; the owner: "only sending data that we actually use vs the entire thing, or upload only what changes"; how is Claude's). A grid's texture and a list's are megabytes; before, a version that changed meant every cell converted again and the whole texture sent. What's built, and what code relies on:
    - **The draw list knows which pixels changed** (`tide/draw.h`, `engine/src/draw.c`): `tide_draw_texture_edit` is `tide_draw_texture_slot_as` for a writer that can tell. It takes the writer's `kind` (a number, never 0, for what it writes and how) and says `kept` where the list has the texture at another version, with the same size and format, last written by that kind: the pixels are then the ones it had, and the writer changes those that differ and says where (`tide_draw_texture_changed`, areas of pixels). Anything else is all to write, and nothing is known of it. What's known is `tide_draw_texture.changes` (NULL for textures nobody edits): `was`, the version as the frame it last changed in started, up to 32 areas that differ from it (`TIDE_DRAW_TEXTURE_AREAS`; areas against each other grow into one, and past 32 they're one around them all), and the writer's own numbers (`tide_draw_texture_parts`, one a part it copies, and `tide_draw_texture_used`), which are zeros whenever the pixels weren't kept.
    - **It's counted from the frame's start, because a renderer draws between two frames** (`changes.frame` against `tide_draw_list.frame`): a texture edited twice in a frame says both edits since the version the frame found. A renderer asks `tide_draw_texture_since(texture, had, ...)` with the version it has: true, with the areas (none, where only the version differs, as when a page's hash changed for another block in it), only when `had` is exactly `was`. A frame that wasn't drawn, another list, a lost device, a hot reload (`epoch`): it has another version, and takes all of it. So nothing is ever assumed about what the GPU holds, and nothing is timed or acknowledged.
    - **`gpu.h`: `texture_write_part(texture, pixels, x, y, width, height)`**, where `pixels` is all of the texture's, read where the part is: OpenGL's `GL_UNPACK_ROW_LENGTH` (in `gl.h`; OpenGL 3.3, OpenGL ES 3 and WebGL 2 have it, and it's put back to 0), WebGPU's `writeTexture` with an origin and the whole image's `bytesPerRow`. `texture_for` (`platform/src/platform.c`) writes the areas, and leaves a texture's smaller images as they are when none changed.
    - **The writers are `engine/src/mesh.c`'s.** A grid (`grid_cells`, for a grid that's sampled and one read cell by cell alike, through a `cells_writer`, which makes a cell's bytes, and a `cells_box`, which says which cells the texture holds and where: all of a sampled grid's, and for one read cell by cell the box its chunks take up, a `Grid3`'s layers tiled, `notes/shaders.md`): each chunk's hash by its place in the box is kept with the texture (`chunk_hash`: the heap's page hash, for a chunk under a page too, where other blocks in the page count as changes and the chunk is then copied again and found the same); a chunk whose hash is what it was is left alone, and of the others each row is made apart and compared, so the area is from the first cell that differs to the last, a chunk at a time. A chunk that went is zeros again. A grid of the pack's, and one of more than `GRID_PARTS_MOST` chunks, is written whole. A list (`list_numbers`): a world's big list by its chunks' hashes, and in a chunk that changed, from the first element that differs to the last; any other list in one run. Fewer elements than it had: zeros past them. A map (`tide_draw_map_data`): made again whole, apart, and the rows that differ are the texture's (`rows_changed`).
    - **A list made along the way is by what's in it** (its id is its version), so it's never kept: another content is another texture, written and sent whole.
    - **`tide_draw_append` hands on what it knows** (`append_texture`): the areas that changed since the pixels the other list has are all it copies, under a kind of its own (the writer's, turned), so that the writer itself on that list finds none of its numbers and writes it whole.
    - **`tide_platform_gpu_written()`** (`tide/platform.h`; the name is Claude's pick) counts the bytes of textures' pixels the renderer sent the GPU, for tests and tools.
    - **Measured** (`tide_platform_draw --bench`, release, OpenGL 3.3 on an RTX 4080 SUPER, three runs each, `gpu-data-before` and `gpu-data` in `bench/results/draw.jsonl`; a frame is recorded, drawn and a pixel read back, and "nothing" takes 0.28 to 0.30 ms of that): a 1024 by 1024 grid of colors as a texture with a brush of 64 cells painted each frame, 2.2 to 2.4 ms a frame before and 0.13 to 0.16 after (recording it 1.5 before and 0.02 to 0.03 after); a list of 65,536 lights of 25 numbers a shader reads, 16 of them written each frame, 1.7 to 1.9 before and 0.20 to 0.23 after (recording 0.63 to 0.70 and 0.06 to 0.07). What a list costs when all of it is sent goes by what's read of it (`notes/shaders.md`, Only what's read of an element is sent): the same list sent whole each frame took 1.7 ms with its 25 numbers and 0.7 with the 7 a shader reads. The workloads that keep nothing (shapes, quads, instances) are as they were.
    - **Tests:** `draw_textures_say_what_changed` (`tests/test_draw.c`, no GPU: the areas after a cell, cells of several chunks, two edits in a frame, a frame with none, a list handed on by an append, a grid that's cleared, a list's element, elements across rows, a shorter list, another layout of one list); `draw_textures_kept_are_the_textures_made_anew` beside it (240 frames of changes picked by a seed to a grid of colors, a `Grid3` of ints with no size along an axis, which is cleared now and then, and a list that's resized: each frame the kept texture is, byte for byte, the one packed from nothing, and the areas it says are all that differs from the frame before; it only tells what the kept path gets wrong, as a fault in packing itself is in both); `changed_textures` in `platform/tests/draw.c` (pixels and the exact bytes sent after each kind of change, for a grid of colors and one of floats, and a frame that wasn't drawn), on OpenGL 3.3, WebGL 2 and WebGPU, where it's run again after the device is lost; pass 18 of `platform/tests/shaders.tide`, whose lists, maps and grids are written between frames. Not run: Android, and OpenGL on a Mac.
    - **Not built:** meshes (a list of corners that changed is copied and sent whole, into buffers of its own: nothing measured said it costs on OpenGL); a texture's smaller images made again for the part that changed; partial copies of C's `tide_texture` pixels, whose owner says nothing of what changed.
- **Alpha is premultiplied under the hood, and colors are the display's values** (card 7 of the reshaping; the owner picked both 2026-10-07 from a proposal with captures of the fringe: "go with your recommendations, we will polish if we find out it wasnt ideal after all"; built the same day; the rules are Claude's picks, Provisional in `docs/spec.md`, Views and drawing, Alpha and color space). `docs/game/draw-settings.md` (Alpha), `docs/game/views.md` (Colors) and `docs/game/shaders.md` (Textures) are the users' pages. What it's for: a straight-alpha sprite blended between its pixels took in its clear pixels' color (a white disc over white had a gray rim, 194 of 255 halfway across its edge), and a target's alpha came out as `a * a + d * (1 - a)`, so half-clear draws into a clear target and then from it were washed out. What's built, and what code relies on:
  - **Nothing a game writes changed:** a `Color` is a color as it is, with its alpha, everywhere (draws, tints, vertices, grids, PNGs, C's `tide_texture` pixels, a frag's outputs). The engine multiplies where bytes for the GPU are made and where a shader writes a pixel.
  - **`gpu.h`'s contract:** a fragment shader writes its color times its alpha, a texture's pixels are that way, and so is what a target holds. `TIDE_GPU_BLEND_ALPHA` is `ONE, ONE_MINUS_SRC_ALPHA` for color and alpha; `TIDE_GPU_BLEND_ADD` is `ONE, ONE` for color and `ONE_MINUS_DST_ALPHA, ONE` for alpha; multiply and replace are as they were (`gpu_gl.c`, with `GL_ONE_MINUS_DST_ALPHA` in `gl.h`; `blends` in `tide.js`). A shader written by hand for a test or a host multiplies before it writes, and for a pipeline that multiplies adds `1 - a` after.
  - **Add's alpha never passes 1,** on purpose: `ONE, ONE` grew a float image's alpha to 3 after three adds, and a `Sample` of it, which divides by alpha, gave a third of the light (pass 11 of `shaders.tide` caught it). Onto an opaque image it stays 1.
  - **Texture bytes:** an asset `Texture`'s are multiplied by tidec as the pack is built (`channel_byte` in `compiler/src/codegen.c`), a grid's as the draw list copies its cells (`channel` in `engine/src/mesh.c`, each channel kept from 0 to 1 first), and C's pixels as the list copies them (`texture_slot` in `engine/src/draw.c`, in whole numbers, rounded). A `Grid2<Color>` asset stays as its file has it in the pack, as code reads it, and goes through the grid's copy. A `Grid2<float>` and the font's atlas have no alpha. `append_texture` copies what's already multiplied.
  - **The engine's shaders** (`platform/src/shaders.h`): `whiten(c, by)` takes a color as it is and gives it times its alpha, plus `by * (1 - a)` for a pipeline that multiplies; `over(p, tint, by)` takes a texture's pixel, which is multiplied already, and the tint it's drawn with, which isn't (2D and 3D meshes). The plain shapes' shader multiplies in its last line, the smooth shapes' no longer divides its paint back, text keeps its power under 1 (that's about display space), and a clear's color is multiplied in its shader, so `Clear(Color(1, 0, 0, 0.25))` leaves (0.25, 0, 0, 0.25).
  - **A game's shaders** (`compiler/src/shader.c`): `put_fragment_main` finishes each output as its tint, then `rgb * a`, then `+ (1 - a)` where the draw multiplies. A color read from a texture goes through `tide_straight` (`H_STRAIGHT`, a helper like Math's: the pixel divided by its alpha, zeros where that's 0), in `ex_sample` for a `Sample` and in the main of a shader with no frag, so code only ever sees colors as they are. A depth read has none of it. Every game shader's text changed, so every shader's `id`, and the pins of `compiler/tests/shaders/` were made again (`tidec --shaders`, LF).
  - **`Blend.Replace` multiplies like the others,** which isn't what was proposed (as the frag wrote it): written raw, a `Sample` of the result would divide by an alpha that was never worked in, and a blit of colors into a target that's then drawn from would be wrong. So an image always holds colors times alpha, and a round trip through `Sample` is the color that was written, for any alpha but 0. Onto the window a replaced color with an alpha under 1 is darker by that much (it was the color as written, the window having no alpha).
  - **What's lost:** a pixel's color where its alpha is 0, and the finer steps of a nearly clear color in bytes. **Not built:** Nothing sRGB (a `Format`, a texture's mark, `Math` helpers): the docs' Colors section has the recipe for a game that works in light, which the float targets are enough for.
  - **`[Raw]` is a texture of numbers** (built 2026-10-07, after the owner asked for the follow-up; the attribute was approved with the proposal, and its name and rules are Claude's picks, Provisional in the spec beside the rest). `docs/game/shaders.md` (Textures that are numbers) is the users' page. What code relies on:
    - **Where:** before an asset that's one `Texture` or one `Grid2<Color>` (`decl.sample_raw`, `check_raw_attribute` in `compiler/src/check.c`, read with the asset's declaration as `[Filter]` is), and on a `Grid2<Color>` field (`check_field_raw`; the attribute stays on the field, `field_is_raw`). Refused, each with what to write: a glob's list or map, any other asset, a `Grid2<float>`, a `Texture` or `RenderTarget` field, an argument, twice.
    - **Nothing carries it at run time, on purpose:** a `Texture`'s four sampling bits are all taken (the filter's two since `Filter.Trilinear`, the wrap's two), and a frag handed a `Texture` would need a uniform or a second shader to know. So a raw texture is only ever the object of a `Sample` by its name, where `declared_raw` (`compiler/src/shader.c`) knows it as the game compiles, and every other use is an error. For a raw `Texture` asset, each place its name is used is noted (`checker.raw_uses`, in `use_asset`), `check_grid_sample` marks its own (`expr.raw_sampled`), and `check_raw_uses` says the rest once code is checked: that catches a local or a function argument too, with no list of places. For a raw grid, which code still reads cell by cell, it's where a `Texture` is wanted: `check_expr_want_any`, `check_blit` and `check_mesh_draw` call `refuse_raw_grid`. A new place a grid becomes a `Texture` without going through one of those has to call it too: `compiler/tests/errors/raw_*.tide` has a file for each way in there is.
    - **Bytes:** tidec packs a raw `Texture` asset's channels as the file has them (`pack_item`'s `raw`), under the file's hash with two bytes turned, so the same file declared both ways is two blocks and two versions in the draw list (since 2026-11-04 the key is what's stored, and the two are one block where the bytes match: `[Raw]` images compress too, above). A raw grid goes through `tide_draw_grid_texture_raw` (`engine/src/mesh.c`: `grid_pixels` with no alpha worked in, and another version than the same cells as colors), which generated `tide_shade_<n>` calls for a raw asset grid or field. A `Grid2<Color>` asset is in the pack as its file has it either way.
    - **The shader:** `ex_sample` leaves `tide_straight` off a raw texture's read, and nothing else differs. `compiler/tests/shaders/raw` pins it beside the same file as colors.
    - A game with no `[Raw]` generates the C it did (the changes are a suffix and a flag that are empty and false without one).
    - **Tests:** pass 16 of `platform/tests/shaders.tide` with `run_raw_checks` in `shaders.c` (a raw `Texture` asset's clear pixel keeps its file's numbers where the same file as colors has none, a raw `Grid2<Color>` asset, a raw grid field's cell with no alpha, and numbers blended between cells), the same on OpenGL 3.3, WebGL 2 and WebGPU; `compiler/tests/shaders/raw`; `compiler/tests/errors/raw_*.tide`, a file a diagnostic; `lsp_asset_textures_and_declared_sampling` (hover, completion, the diagnostic and its note).
    - **Not built:** a raw texture as a `Texture` value, and C's `tide_texture` pixels as numbers.
  - **Generated C** differs in shader text alone (and a pack's texture bytes where an image has alpha under 1); no header, so no game's hash.
  - **Tests:** `meshes` in `platform/tests/draw.c` (C's pixels: white blended towards a clear magenta pixel over white is white, a half-clear pixel is half); pass 14 of `platform/tests/shaders.tide` with `run_alpha_checks` in `shaders.c` (a grid blended towards a clear cell, what a `Sample` between the two gives, and a half-clear rect drawn straight, through a clear target, and through a frag that samples the target: the same pixels); `settings` in `draw.c` (`blended`: a replaced color is its color times its alpha); `check_render_depth_alpha` in `targets.c` (what an image holds after a clear with alpha); `tests/test_draw.c` and `compiler/tests/e2e/mesh_test.c` (the list's copies), `compiler/tests/e2e/asset_textures` (the pack's bytes where the file's clear pixels have a color). The hand-written shaders of `draw.c` follow the contract.
