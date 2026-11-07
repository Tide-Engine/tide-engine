# Compute on the GPU

Language rules: `docs/spec.md`, Compute on the GPU (decided 2026-11-05 where it
says so; every other rule is Claude's, Provisional). The owner's instruction for
the implementation (2026-11-05): build it to release-ready, compute shaders
properly working, every pick Provisional, and the last push to `dev` publishing
a release. The decided backend rule: compute is guaranteed on every render
backend, and one that can't run it (OpenGL 3.3, OpenGL ES 3, WebGL 2) is removed.

## Progress

- [x] `gpu.h` compute API (storage buffers, compute passes, dispatch and
      indirect dispatch, indirect draws, copies, asynchronous reads)
- [x] Vulkan backend (`gpu_vk.c`; `notes/rendering.md`, Vulkan compute)
- [x] WebGPU backend (`web/gpu_web.c`, `web/tide.js`), 2026-11-05: notes/web.md,
      WebGPU compute
- [x] Recorder for Unity (`gpu_record.c`) and its replay (`TideRender.cs`), reads back
      included (embed ABI 11), 2026-11-05: notes/unity.md, Compute in Unity
- [x] WGSL compute and storage in the translators (`wgsl_read.inc`, `spirv.c`, `hlsl.c`), 2026-11-05:
      see "The translators" below and `notes/shaders.md`
- [x] tidec: `compute`, `GPUList<T>`, `GPU.Run`, ordered `Add`, atomics,
      `shared`, `GPU.Barrier`, `await list.Read()`, 2026-11-05 ("Built" below)
- [x] Engine/renderer: GPU lists in the draw list, compute commands, ordering
- [x] Indirect draws over a `GPUList` (`Draw.Instanced`), and meshes over GPU lists' vertices and indices, 2026-11-05
      ("Meshes over GPU lists" below)
- [x] `Draw.Transform` on indirect draws, runs moved out of passes, fxc's shared-memory races warned, 2026-11-05
- [x] GL backends removed, 2026-11-05 (`notes/rendering.md`, GL backends removed)
- [x] Engine GPU culling of listed runs, 2026-11-05: notes/drawing.md, Listed runs culled on the GPU; Hi-Z
      occlusion (`Draw.Occlusion`) the same day: drawing.md, Occlusion culling from a depth pyramid
- [x] Docs: `docs/game/shaders.md`, spec, editors
- [x] GPU lists passed on alone, writes/Add/atomics in functions, mirrored instances of indirect draws, shared writes
      guarded for fxc, Adds sharing one scan, Draw.Bounds pinned for GPU vertices, 2026-11-05 ("Closing the gaps" below)
- [x] `draw_indexed_indirect_count` (Vulkan's DrawIndexedIndirectCount, WebGPU's multi-draw-indirect or a loop, the
      recorder's op, embed ABI 12), used by the GPU culling: empty level buckets draw nothing, runs one after another
      that draw alike are one call, stats from the GPU's counts, 2026-11-05: notes/drawing.md, Culled runs drawn by a
      count, joined
- [x] Occlusion's pyramid in one dispatch (a hand-written WGSL kernel, shaders.h), multisampled depth read in place
      (`gpu.h depth_now`, `TIDE_GPU_READ_SAMPLES`), measured at a pass break the frame has; ordinary listed runs in culled
      runs' calls, 2026-11-05: notes/drawing.md, Occlusion's measure made cheaper
- [x] Kernels on Dawn's Direct3D 11 (shared atomics in a struct, the pyramid's levels unrolled), a kernel the browser
      can't make done without, the D3D11 hardware lane, 2026-11-05: notes/shaders.md, Kernels on Dawn's Direct3D 11;
      notes/drawing.md, A culling kernel that can't be made
- [x] GPU lists in two pools: a shader names any number of GPU lists (64 at the most), a kernel adds to any number,
      Vulkan's storage requirement 7 -> 4, 2026-11-05 ("GPU lists in pools" below)

## GPU lists in pools: no limit on the lists a shader names (2026-11-05)

The owner approved lifting the limit of 7 storage buffers a frag, vert or kernel bound (each GPU list one, and a
kernel's run record and each Add's scratch more), and Claude's design: GPU lists suballocated from a few large pooled
storage buffers, each list's place given to shaders in their uniforms. The details are Claude's, Provisional. What
changed for games: a shader names up to 64 GPU lists (the table's limit, `SHADER_STORAGE_MOST` / `TIDE_SHADER_STORAGE`,
an error with its fix past it), a kernel has any number of Adds, and nothing else (the same pixels and results).

- **Two pools** (`compute.inc`, `gpu_pools`): `TIDE_STORAGE_INTS` holds the lists whose element is `int`
  (`GPUList<int>`), `TIDE_STORAGE_WORDS` every other. Why two: WGSL types a storage binding whole, `array<u32>` or
  `array<atomic<i32>>`, and WebGPU refuses one buffer bound twice in a dispatch where either binding writes (a usage
  scope, and writable aliasing). Atomics are only on `GPUList<int>`, so a kernel that uses atomics declares the pool of
  ints `array<atomic<i32>>` and its other lists stay plain loads in the other pool; its lists of ints are read and
  written through `atomicLoad`/`atomicStore`, as one list with atomics was before. Which pool is static, the element
  type: generated code passes `TIDE_GPU_LIST_INTS` in the stride to `tide_gpu_list_make`, `_adopt` and `_recreate`
  (`tide_gpu_list_state.ints`; another kind across a hot reload is another version, made again empty), and
  `tide_shader.storage_ints` says each slot's pool. A list read through a slot of the other kind is the empty list.
- **A list is a range** of its pool: 4 + capacity * stride words, rounded up to 4 (16 bytes), its header then its
  elements as before. Words 0 to 3 of each pool are an empty list's header, zeros, so a table of zeros names a list of
  nothing (what a slot reads where its list isn't there, disposed, or can't be held).
- **The table** (`shader.storage_at`, `tide_shader.storage_table`: bytes into the game's uniforms, after the floats'
  packs): two int words a slot, its range's first word and its capacity, then for a kernel a word an Add, its
  scratch's first word. Generated code leaves them zero; the renderer writes them into its copy of the uniforms as it
  draws (`draw_shaded`) or runs (`run_compute`, `cull_slot` for the culling's kernels). The recorded uniforms, runs'
  joins and the Unity recording keep their shape.
- **What a shader reads and writes stays in its list's range** (`need_storage`, shader_compute.inc):
  `tide_word<s>(k)` is `pool[tide_base<s>() + k]`; `tide_count<s>` is min(the header's count, the table's capacity) (one
  storage load, where it was two); `tide_capacity<s>` is min(the header's capacity, the table's). The header's word
  keeps FXC's X3694 away from a write every invocation makes to one place (it can't see what storage holds: "Kernels on
  Dawn's Direct3D 11", notes/shaders.md), and the table's keeps a header written wrong from reaching another list.
  `tide_get` gives zeros past the count, `tide_put` and atomics do nothing past the capacity, under 0 either; an
  indirect draw's instance index is held to its capacity (its count is the header's, which the GPU draws as many of);
  Adds' scatter to the header's capacity. The translators' clamp of a runtime array's index keeps it in the pool.
- **Bindings** (`put_pools`; `tide_shader_compute_storage`): 33 the words pool, 34 the ints pool (read in draws; in a
  kernel read_write where it writes a list of that pool, `tide_shader_pool_written`), and a kernel's run record at 35
  (25 to 27 on the pools' branch, before it was merged after Sixteen textures a draw, notes/rendering.md),
  read. The engine's kernels (shaders.h) bind the pool of the list they work on and take its first word in their
  uniforms: `run_size` (pool, record), `add_offsets` (pool, record), `add_scatter` (pool), the new `zero_words`
  (pool), and occlusion's pyramid (the ints pool, its levels' words offset). Each Add's scratch is a range of its
  list's pool (`take_add_scratch`, taken in turn each frame, never zeroed: each group writes its region before it's
  read), so the offsets and the scatter bind one pool, and an indirect run's `dispatch_indirect` reads the record,
  which no binding of the same dispatch writes.
- **Adds:** any number, four lists a scan (`tide_scan`, `tide_scan1`, ...: `scan_lanes`, `scan_type`), all in one
  Hillis-Steele loop, so its barriers are once whatever the count; every scan counts against the 16 KiB.
- **The allocator** (`pool_take`, `pool_give`): first fit over the ranges let go of (in order, joined where they touch),
  else the pool's tail, which has never been a range's since it was made (zeros). A pool grows by being made again
  twice as big (256 KiB first) with its used words copied on the GPU (`buffer_copy`, in order with the frame's work), so
  a range's place never changes; only outside passes (`passing`, set by `begin_pass`/`end_pass`), and never in a
  compute pass (every range a run needs is taken before its first). Up to gpu.h's new optional `storage_most`: Vulkan's
  least of `maxStorageBufferRange`, `maxMemoryAllocationSize` and `maxBufferSize`; WebGPU's least of
  `maxStorageBufferBindingSize` and `maxBufferSize`, which `tide.js` now asks the adapter for at its most; 128 MiB
  where it's NULL (the recorder). `TIDE_GPU_POOL_MOST` (bytes) is less, for a test. A list's range taken where another
  list had one is zeroed first, by `zero_words` (or from the CPU while that pipeline isn't made).
  - **Why growth by making the pool again, not by adding pools:** WGSL can't choose a binding as it runs, so a list in a
    second pool would cost every access a branch (or a pipeline for each way lists fall between pools); one pool of
    each kind keeps one add. The copy is once a doubling.
- **What doesn't fit:** a list past what the device binds as one buffer, counting every list of its kind, is the empty
  list on the GPU (zeros read, nothing written, capacity 0 there) and the pool says so once; `capacity` on the CPU is
  still what it was made with. Every device binds 128 MiB (a list's most is 128 MiB, as before); this machine's Vulkan
  4 GiB. Before, each list had a buffer of its own and only the device's memory bounded them all.
- **The culling's lists** (cull.inc): each of cull.tide's lists is a range of the pool its elements say
  (`cull_pool_of`), grown by doubling (rewritten each frame, so nothing is copied); `depths` too, in the pool of ints;
  the places and arguments its draws read as vertices and indirect records are at their ranges' offsets
  (`cull_at`). `tidec --kernels` writes `.storage_ints` and `.storage_table` beside the slots' enums. A frame whose
  lists its pools can't hold draws its culled runs nothing and the later frames' are the CPU's (`cull_failed`, said).
- **gpu.h:** `TIDE_GPU_MAX_STORAGE` 7 -> 4 (Vulkan's least `maxPerStageDescriptorStorageBuffers`, so no conformant
  device is refused for storage now; the renderer binds 3 at the most), a pipeline's layout 37 bindings at the most
  with 16 textures and samplers (pushed where `maxPushDescriptors` takes it, a set otherwise: rendering.md), `storage_most`. The recorder's ops and packets are as they were: no embed ABI change (TideRender.cs's
  `MaxStorage` 4, and `Supported` asks for 4 storage buffers a stage).
- **GPU memory** (`tide_platform_gpu_memory`): the pools are buffers the renderer made, so it counts each pool's size,
  not each list's.
- **Hot reload, device loss, Dispose:** lists' records by id as before (`list_range`); a list not named since a reload,
  disposed or gone gives its range back as the frame ends; a lost device forgets the pools, and lists are made again
  (their contentRevision changes). Reads back read their list's range (`buffer_read` at its offset, at most its bytes).
- **Tests** (sanitize, this machine; every one passes):
  - Goldens `kernels_add`, `kernels_helpers`, `kernels_shared`, `kernels_shared_races`, `gpu_meshes` written again (the
    pools and tables alone), and the new `kernels_many` (20 lists, 6 Adds, atomics, a frag of 10); every golden's
    SPIR-V through spirv-val and HLSL through fxc (cs_5_0 for kernels), the renderer's shaders too
    (`tide_spirv_shaders_valid`, `tide_hlsl_shaders_valid`, `zero_words` among them).
  - Errors: `kernel_storage_limit` (65 lists: "names 65 GPU lists, and a kernel names 64 at the most") and
    `gpu_list_draw_limit` (a draw of 65), where the first was the 8-buffer error.
  - `tidec_e2e_compute`: the kernel's table (5 words, after its values, zeros as recorded), its pools, the atomic
    kernel's list in the pool of ints.
  - `tide_platform_kernels`: `Spread`, a kernel of 20 lists over 100 invocations in 2 groups (3 read, 6 written by
    index, 4 with atomics, 6 Adds in two scans, ints and float2s, one list of structs), and `ShowWide`, a frag reading 10
    of them (a pixel); `Trespass`, every way past a full list's ends (under 0, at and past its capacity, the ints'
    limits, many capacities on; writes, reads, atomics, Adds) between neighbours in both pools: every list's words and
    header count kept, and zeros read. With `tide_put`'s guard made `>` for `>=` by hand, the fence fails (checked).
    Native Vulkan with the validation layer, and with `TIDE_VULKAN_DESCRIPTOR_SETS=1`; WebGPU (web-debug, SwiftShader)
    through its lost device.
  - `tide_platform_compute`, `_compute_vulkan_sets`, `_cull`, `_cull_decided`, `_occlusion`, `_draw`,
    `_draw_vulkan_sets`, `_shaders`, `_prewarm`, `_gpu_functions`, `_embed` (Vulkan, validation layer); on web-debug
    `tide_platform_kernels`, `_compute`, `_cull`, `_cull_fallback`, `_occlusion`, `_draw`.
  - Whole suites (sanitize): every `tidec_*` test (2,900), and the other 280 but `tide_server_smoke`, whose port 7777
    another program's UDP socket held (it can't listen; nothing of this). `tide_cli_tests`' `unity_shaders_written`
    counts the renderer's kernels, 6 with `zero_words`; `tide_platform_gpu_timing_web`'s fake adapter has no limits, so
    `tide.js` asks only for the limits an adapter says it has, and for no descriptor where there's nothing to ask.
    The docs build (`npm run build`) passes.
  - Safety: `compiler/tests/safety/gpu_lists` (capacities of none, under none, the ints' limit and past a GPU's
    binding, sent more than they hold, counted past both ends, resized and let go of while kernels run over them and a
    frag reads them, a read of one let go of; kernels indexing at the ints' limits), also through the renderer
    (`_gpu`, Vulkan, validation layer).
- **Measured** (release, hidden, Vulkan, RTX 4080 SUPER; `dev` at 12fcc04bc in a temporary worktree against this, each
  built with the same scratch `TIDE_EXTERNAL_DIRS` folder, run alternately; the cost of the add in every access):
  - A read-heavy measure (scratch, not committed: a kernel of 1,048,576 invocations gathering four lists 16 times each,
    eight runs a frame, and a frag over 1920x1080 reading four lists 8 times a pixel; within 7 buffers, so both builds
    run it), 600 frames, three pairs: GPU 1.705 / 1.701 / 1.701 -> 1.630 / 1.633 / 1.632 ms a frame (median), 4%
    less: the count's check loads one header word where it loaded two, which more than pays for the add.
  - `bench_occlusion` (4 blocks of 300 frames), six pairs, both orders: with occlusion (the culling's kernels and the
    pyramid's) 0.329-0.331 -> 0.322-0.323 ms median, 2.4% less; without it the frame is the draws' (3.76-3.99 ms either
    way, drifting over the runs, the later of each pair the higher whichever was later), the same bytes on and off.
  - Mount & Rifle (D:/Projects/mount-and-rifle at 45c7cff, a copy of its `game` folder built only), its menu field scene,
    1920x1080, `--profile --profile-warmup 300`, 1,500 frames, fresh `--data-dir`: GPU p50 5.038 / 5.065 / 5.048 ->
    5.063 / 5.027 / 5.006 / 5.013 ms (three runs, one each way, had another program's GPU work in them, 9-10 ms, and
    are left out); views and render as noisy as before (0.82-1.11 ms, 1.23-1.73 ms both ways).
  - `tide_platform_kernels` as it was before (its sources, built in both): 820-860 ms a run both ways (six pairs, the
    outliers either way), its 45 checks passing on both.
- **Merged after dev's sixteen textures, D3D11 kernels and list reload fixes** (2026-11-05): storage after the 16
  textures' and samplers' bindings, so the pools are at 33 and 34 and a run's record at 35 (`SHADER_STORAGE_BINDING` +
  `TIDE_STORAGE_*` in `put_pools`; `shaders.h`'s kernels at 33/34), a pipeline's Vulkan layout its own bindings
  (37 at the most, pushed where it fits); WebGPU binds at most 3 storage buffers of its default 8 and 16 textures and
  samplers. Dev's "a list made where a freed one was gets its own buffer" holds as a range: `list_range` makes a new
  range where the state isn't `made`, or its words (capacity times stride) or pool changed, and a range taken where
  another list's was is zeroed (`zero_words`). The goldens changed from the pools' branch in their storage bindings
  alone. Tests after the merge (sanitize, Vulkan validation layer): every `tidec_shaders_*`, `_spirv_*`, `_hlsl_*`
  (fxc), `wgsl_*`, `tide_spirv_shaders_valid`, `tide_hlsl_shaders_valid` (196); `tide_tests`, `tide_cli_tests`, every
  `tidec_safety_*` (`gpu_lists_gpu` too), `tidec_error_kernel*`/`_gpu*`, `tide_platform_kernels` (61 checks, dev's
  freed-list check among them), `_compute`, `_cull`, `_cull_decided`, `_occlusion`, `_draw`, `_targets`, `_shaders`,
  `_prewarm`, `_gpu_functions`, `_reload_reads`, `_embed`, `tide_editor_embed` and their `_vulkan_sets` (182);
  kernels, cull, cull_decided and occlusion with `TIDE_VULKAN_DESCRIPTOR_SETS=1`; web-debug `tide_platform_kernels`
  (through its lost device), `_compute`, `_cull`, `_cull_fallback`, `_occlusion`, `_draw`, `_shaders`,
  `tide_web_runtime` (its storage layout test now 4 at 33..36); the docs build.
- **Merged after dev's samplers apart and texture arrays** (2026-11-05): dev's textures are images at 1 + 2i and its
  samplers, counted apart, at 2 + 2j, 16 of each (the last 32), so the pools stay at 33 and 34 and a run's record at
  35; a Vulkan layout is still 1 + 16 + 16 + 4 = 37 at the most (dev's alone was 40 with 7 storage buffers), pushed
  where it fits and a set otherwise; WebGPU's bind group 16 textures, 16 samplers and 3 storage buffers, within its
  defaults. Kernel pipelines and the culling's take their textures from dev's `shader_pipeline_textures` and bind them
  with `bind_shader_textures` (`image`/`sampler` in place of `sample`); their storage is the pools as before. A texture
  array is one texture of the 16, so nothing about storage changed. Golden `kernels_shared` written again (dev's new
  sampler declaration with the pools; every other golden matched as merged). Tests (sanitize): every `tidec_*` with
  `tide_tests` and `tide_cli_tests` (2,920); with the validation layer `tide_platform_embed`, `_draw`,
  `_draw_vulkan_sets`, `_targets`, `_compute`, `_compute_vulkan_sets`, `_kernels`, `_occlusion`, `_cull`,
  `_cull_decided`, `_shaders` (dev's texture-array draws among them), `_prewarm`, `_texture_capacity`,
  `_gpu_functions`, `_reload_reads`, `tide_editor_embed` and every `tidec_safety_*_gpu` (22); kernels, cull,
  cull_decided and occlusion with `TIDE_VULKAN_DESCRIPTOR_SETS=1` too (4); web-debug (SwiftShader)
  `tide_platform_kernels`, `_compute`, `_cull`, `_cull_fallback`, `_occlusion`, `_draw`, `_targets`, `_shaders`,
  `_texture_capacity`, `tide_web_runtime` and the e2e texture tests (`texture_arrays`, `texture_capacity`, `textures`,
  `asset_textures`, `raw_texture_fields`, `mipmaps`, `wrap`, `render_depth`, `compute`) (19); the docs build.
- **Gaps:**
  - Unity's replay renames a buffer a kernel or copy writes after a draw of the frame read it (`Written`, notes/unity.md):
    with pools, a kernel writing any list after a draw read another of the same pool copies the whole pool there, where
    it copied one list. The same pictures; not measured in Unity, and the Sample's `ComputeReplayTests` weren't run
    (no packet changed).
  - A pool never shrinks: its size is the most its lists took at once (ranges let go of are taken again).
  - A kernel that uses atomics reads its other lists of ints through `atomicLoad` (Tint's HLSL makes that an
    `InterlockedOr` on Direct3D): CullListed's `depths`, `scratch` and the like. Measured on Vulkan only (above).
  - Real-GPU WebGPU lanes and Android/Linux/macOS weren't run here (CI's, optional lanes).

## GPU lists under the library host (2026-11-05)

Found auditing compute for the owner's Mount & Rifle crash (whose cause was the culling's join: drawing.md, A culled
set's join past its top level). Internal: no Tide, generated-C or language change.

- **Reads never finished under `tide run`.** `gpu_list.c`'s request table was a static, and a watching run's host
  (`game.exe`) and game library each link their own engine copy (both PDBs have `tide_gpu_read_requests`): generated
  code asked in the library's table and the renderer polled the host's, so `await list.Read()` waited forever and
  after 64 the begins failed. `tide_platform_kernels` is a standalone program, which has one copy. Now the table and its
  serial are a `tide_gpu_reads` (draw.h); each copy uses its own until `tide_gpu_reads_share` hands it another's, and
  the host does that as it loads each library (`load` in `reload_library.inc`, through the optional
  `tide_host_game.gpu_reads_share`, which run.h's table fills). One table through every reload, so the serial goes on
  (no carried ticket matches a new build's request) and a read begun before a same-layout reload finishes after it.
  `tide_gpu_lists_free` takes the lists it frees out of the table (`reads_lose`: asked fails, begun finishes and
  fails when polled), and a poll whose list is gone fails, so nothing reads a freed state.
- **A list made where a freed one was took its buffer.** The renderer's records are by id (the state's address) and
  version; a full reload, or a swap that didn't adopt a list, frees its state, and the next build's first list of
  that size can be calloc'ed at the same address, version 1 again. That frame named it, so `list_buffer` returned
  the freed list's buffer, sized for its capacity and stride: a `Set` of more wrote past its end
  (`vkCmdCopyBuffer`, which the validation layer reports) and a read read past it. Now a record also keeps the words
  it was made for, and is reused only for a state a renderer made a buffer for (`made`: an adopted list's is set, a
  new state's isn't) of that version and size.
- **contentRevision.** The renderer's copy bumped a remade list's revision from its own counter, which can be behind
  the game's: now past both (`max(next, old + 1)`).
- **Tests.** `tide_platform_reload_reads` (`reload_reads.mjs`, `reload_game.c` with `RELOAD_GAME_READS` linking the
  engine as its own copy, `tide_pic`, as a game library does): a list set and read back through the host's renderer,
  then a build of another layout published while it runs, whose list is 16 times bigger, read back whole; without
  the host sharing its table no read finishes. `tide_platform_kernels` gains `reused_list_over`: a list read back,
  then its state made over into what a new state at that address is (made false, version 1, 16 times the capacity),
  set and read back whole; without the record's check the validation layer reports the copy past the 32-byte buffer
  and it fails. (Under AddressSanitizer freed memory isn't handed out again soon, so the reload test alone can't show
  the address reused.) Native Windows, sanitize, Vulkan with the validation layer; the kernels page on WebGPU and
  Linux/macOS (where `tide_pic` is the engine built again position-independent) are CI's.
- **Left as it is:** an adopted list the new build's first frame doesn't name loses its buffer (it comes back empty
  with a new revision), as designed; a list made in a view every frame keeps its state until the next unload.

## Built: the language, the engine and the renderer (2026-11-05)

Everything between the language and gpu.h, as "Language and runtime design"
above has it, with these deviations (each also in the spec's "Built" list):
`this.at` is always an `int3`; ordered `Add` needs a group of one axis (a 2D
group's local order isn't this.index's, which the first GPU run found);
each list a kernel adds to has its own scan; `GPUList` is 16 bytes (`id`,
then the capacity it was made or recreated with and its stride, for a web
reload, which can't read another address space's state); reads back begin as
the next drawn frame's work ends (not a draw-list command: the awaiting task
has no draw list). (A 2D or 3D `Draw.Instanced` over a GPU list wasn't moved by
`Draw.Transform` in this first cut; it is since, below.)

### tidec

- **Parsing:** `compute` is a contextual declaration word (`parse_shader`
  with `is_compute`), `[Group(...)]` takes values as LOD's do, and `shared T
  name;` is a local (`stmt.is_shared`), given `default`, `mut`.
- **Types:** `TY_GPU_LIST` and `DECL_GPU_LIST` (appended), one decl a
  element type in `program.gpu_lists` (`gpu_list_of`); elements are
  `gpu_element_refusal`'s GPU values. Machine-local through
  `holds_render_resource_in`, so match state, constants and match code refuse it
  as they refuse a RenderTarget. C type `tide_gpu_list`, layout 16/8.
- **Checker** (`compute_check.inc`): members, index, methods, writes (an
  assignment's target, `check_gpu_list_write`), `GPUList<T>(capacity)`,
  `GPU.Run`/`GPU.Barrier` (`check_gpu_static`, before `names_builtin_owner`),
  the kernel's signature (`check_kernel_decl`), `[Group]` (`check_group`, after
  the body, which it needs a checker for), and `check_kernel_flow`: shared
  placement and size (16 KiB), Add once a path (`adds_through`, a definite
  assignment-like walk where `return` ends a path), Add's one-axis group, and
  barriers' uniformity (`barriers_uniform`). `expr.gpu` (`gpu_op`) says what an
  expression does; `decl.adds`, `atomics`, `gpu_writes` are a kernel's lists by
  field, `decl.barrier` whether it waits; `decl.uses_gpu_read` gives a task's
  frame `tide_gpu_ticket`. `this` in a kernel is `program.invocation`. A function
  that reads a GPU list's elements or count is GPU-only (`gpu_only_reads ==
  TY_GPU_LIST`, said at a CPU call). Kernels are "kernel" in messages.
- **Shaders** (`shader_compute.inc`, `shader_kernel.inc`): a GPU list a shader
  names is a storage slot (`shader.storage`, by source decl and field, from
  `plan_reads`), read through per-slot functions (`need_storage`: `tide_word`,
  `tide_count`, `tide_capacity`, `tide_get`, and `tide_put` where a kernel
  writes it); atomics through `need_atomic` (each `&array[i]`
  inline, as the HLSL translator takes them; capacity-guarded); shared atomic
  arrays through `need_shared_access`. `plan_kernel` plans one shader a kernel
  (`shader.compute`, `.kernel`): its values all uniforms, no engine header,
  storage slots [lists][Add scratch][run record], at most 7 (since "GPU lists in pools": two pools and the run
  record, lists and scratch ranges of the pools, found by a table in the uniforms), and `write_kernel`
  writes its WGSL: the entry point sets `tide_inv_*`, calls the body (guarded by
  the run's size unless it waits), then each Add's scan of the group's flags
  (`tide_scan`, Hillis-Steele with barriers in uniform control flow) and its
  survivors to the group's region of the scratch, the count by the group's last
  invocation (fxc refuses unguarded writes to one place). An indirect draw's
  shader (`shader.gpu_instances`) reads its instance words from storage slot 0
  into the instance packs (`put_instance_loads`) in place of attributes. Kernels
  check in WGSL (`check_shader`).
- **Codegen:** `gen_kernel` writes a kernel's WGSL, SPIR-V and `tide_shader`
  (`.compute`, `.group`, `.storage_count`, `.storage_writes`, `.adds`), its
  lists' `_words` writers and `tide_run_<n>(draw, size[3], over, [values],
  bound...)`, which fills uniforms, textures (`gen_shader_textures`, the draw
  wrappers' loop, extracted) and storage ids and calls `tide_draw_compute_run`.
  Draw wrappers of shaders that read GPU lists record them
  (`tide_draw_set_storage` before, `storage_next = 0` after); indirect ones call
  `tide_draw_mesh_shaded_indirect`. `gen_gpu_lists` writes, for each GPU list
  type, `tide_gpu_words_<n>`, `tide_gpu_set_<n>` and `tide_gpu_list_of_<n>`;
  `gen_gpu` the expressions; `gen_await` the reads (the fetch pattern, with the
  frame's `tide_gpu_ticket`). Hot reload: `tide_gpu_list_adopt` /
  `tide_gpu_list_recreate` in `gen_resource_value`, `tide_gpu_lists_recreated`.

### Engine (`tide/draw.h`, `engine/src/gpu_list.c`, `draw.c`, `mesh.c`)

- `tide_gpu_list_state` (capacity, stride, version, content_revision, made,
  alive), made by `tide_gpu_list_make`, `_resize`, `_dispose`, `_capacity`,
  `_content_revision`; `tide_gpu_list_made` (the renderer's: a buffer made again
  bumps the revision); `_adopt`/`_recreate`/`_recreated`/`tide_gpu_lists_free`
  mirror render targets'. Revisions share `tide_render_revision_next`.
- Draw list: `TIDE_DRAW_COMPUTE` (`tide_draw_compute`: shader, uniforms,
  textures, storage ids, size or the list it's over) and `TIDE_DRAW_GPU_LIST`
  (`tide_draw_gpu_op`: Set with its words in `gpu_words`, Clear, SetCount),
  appended kinds; `storage` holds runs' list ids; `tide_draw_run.storage` and
  `.indirect` (runs with different lists or an indirect one never join).
- Reads: 64 requests (`tide_gpu_read_requests`), tickets of a place and a
  serial; `tide_gpu_reads_frame` lets go of one done and not polled for 600
  frames; a forgotten begun read is finished by the renderer before it goes.

### Renderer (`platform/src/compute.inc`, `platform.c`, `shaders.h`)

- Lists' buffers by id (`list_buffer`: made empty with their header where
  missing or of another version, which `tide_gpu_list_made` hears); after a hot
  reload a record is kept only where the new build's first frame names it,
  before which its state isn't read; dead lists' buffers go as frames end.
- `STEP_COMPUTE` and `STEP_GPU_OP` steps, done outside passes in order: a run
  writes its record (`TIDE_GPU_RUN_WORDS`; an indirect one's by `run_size` in a
  compute pass of its own, so `dispatch_indirect` reads it), takes Add scratch
  (`most groups * tide_gpu_add_group_words`), and in one compute pass
  dispatches the kernel, then for each Add `add_offsets` (one group of 256)
  and `add_scatter` (a group a kernel group, indirect for an indirect run).
  Set/Clear/SetCount are `buffer_write`s. Indirect draws' arguments (index
  count, then the list's count by `buffer_copy`) are made at the frame's start
  and after each run or list operation for the draws up to the next one
  (`prepare_storage_draws`), so no pass is split for them. A list a run or draw
  names that isn't there is an empty one of no capacity.
- Pipelines: kernels' by shader id (`kernel_pipeline_for`, kept across a reload
  like draws'), prewarmed from the shader table; the engine's three kernels
  (WGSL in `shaders.h`, SPIR-V at build, HLSL checked by fxc).
- Reads begin at `compute_frame_end` of each drawn frame, poll at the next
  frame's start (`buffer_read_wait` first in headless runs); a device that's
  another fails the begun ones (`forget_compute`).
- Every backend computes (2026-11-05): the no-compute path (`gpu->dispatch`
  NULL on OpenGL and WebGL: runs passed by, said once, reads failed) went with
  the GL backends.
- The recorder writes the new ops after the existing ones, and a read back as
  `BUFFER_READ`, which the other program answers (`tide_embed_render_read`);
  Unity's TideRender replays them (`notes/unity.md`, Compute in Unity).

### Tests

- `compiler/tests/shaders/kernels_add` and `kernels_shared` (goldens; their
  SPIR-V through spirv-val, their HLSL through fxc as cs_5_0); every existing
  golden unchanged.
- 72 error tests, one a diagnostic: `compiler/tests/errors/gpu_*`, `kernel_*`,
  `group_*`, `shared_*`.
- `tidec_e2e_compute`: what runs, list operations and an indirect draw record,
  in frame order, and their words, uniforms, sizes and lists.
- `tide_platform_kernels` (`platform/tests/kernels.tide`, `kernels.c`): a Tide
  function on the GPU against the CPU, ordered Add at three group sizes and a
  2D run, a chained indirect run, atomics, shared memory with barriers and an
  atomic on shared memory, a struct list sent, changed and read back, Read and
  ReadCount, an indirect draw's pixels, contentRevision; Vulkan with the
  validation layer (gpu label), and WebGPU with a lost device. (Its OpenGL
  and WebGL refusal checks went with those backends, 2026-11-05.)
- `lsp_compute_complete`; the grammars' `compute` case and probe line.

### Not built

- (Built 2026-11-05: `draw_indexed_indirect_count`, below and notes/drawing.md.) A game's `Draw.Instanced` over a
  GPU list is still one indirect draw a call: nothing in the language asks for many records.
- (Hi-Z occlusion in the engine's culling is built: drawing.md, Occlusion culling from a depth pyramid.)
- (Built since, "Closing the gaps" below: GPU lists passed on alone, mirrored instances of indirect draws, fxc's race
  guarded.)
- Stats: an indirect draw counts its mesh's indices once (its instance count is the GPU's), and a draw of GPU indices
  counts 0 vertices (`draw_shaded` in platform.c: the corner count is the GPU's). What the CPU knows is counted: CPU
  indices times CPU instances. The same gap as GPU-culled runs' (drawing.md, Listed runs culled on the GPU), which
  asynchronous GPU-side counts would close for both; nothing here reads them back.
- The engine's Add offsets and scatter (`add_offsets`, `add_scatter`) are still one dispatch each a list a kernel adds
  to; only the kernel's own scan is shared.
- `++`/`--` inside an expression on a shared scalar (`int a = last++;`) isn't under the shared-write guard (only
  assignment statements are, `writes_shared`): fxc would refuse it where every invocation does it.

## Meshes over GPU lists, Draw.Transform on indirect draws, runs out of passes (2026-11-05)

Task: finish what "Not built" listed. Every pick is Claude's, Provisional, and in the spec's Built list.

- **Meshes over GPU lists.** `Draw.Mesh`, `Draw.Instanced` and a target's `Mesh` take a `GPUList<V>` where the
  vertices go and a `GPUList<int>` or `List<int>` where the indices go (`expr.gpu_vertices`, `gpu_indices`; the
  checker never takes the built-in unshaded path for them, so a plain `Vertex` mesh gets a shader of no vert and frag).
  - Shader (`shader.gpu_vertices`, `gpu_indices`, `vertex_storage`, `index_storage`; `shader_storage.role`): the
    draw's own lists are the first storage slots, instances', vertices', indices', then the code's. A pulled vertex
    stage has no `@location` vertex inputs: `var tide_vertex = tide_get<v>(i32(tide_vertex_index))`, zeros past the
    count. With GPU indices the built-in is named `tide_corner_at` and `tide_vertex_index` is
    `bitcast<u32>(tide_get<i>(i32(tide_corner_at)))`, so rolls and the vertex read go by the vertex's index. Instance
    attributes start at location 0 then, as their places in the pipeline's attributes do.
  - `tide_shader.vertex_storage`, `.index_storage` (1 + slot), `attribute_count` 0, no `bounds_position`. Generated
    `tide_shade_<n>` takes `tide_gpu_list tide_gpu_vertices`/`tide_gpu_indices` in the lists' places, passes `(tide_list){0}`
    on to the engine for them, and the lists through `tide_storage` as any GPU list a shader reads.
  - Engine (`mesh.c`, `pulled_mesh`): such a shader's mesh is the CPU's indices alone, keyed by their list and version
    (`MESH_PULLED` as the other id), or for GPU indices one unused triangle by the vertex size; both have one vertex of
    zeros (`tide_draw_mesh_fill_pulled`, draw.c: indices kept as they are, whole triangles, as the GPU's vertices aren't
    the CPU's to count). So every run still has a mesh, and CPU instances (a `List<T>` or a 3D Mesh's transform) are
    an ordinary run's: Draw.Transform, mirroring splits, frustum culling with no bounds (drawn).
  - Renderer: the pipeline's vertex slot has stride 0 (the mesh's one vertex, never read: WebGPU checks a vertex
    buffer's size against a stride). With GPU indices the step is `draw_indirect` of {the list's count, by
    `buffer_copy` of its header, instances (the run's count, or the instance list's count copied), 0, 0}, made in
    `prepare_storage_draws` with the others; `step_indirect` says which steps need arguments. With the CPU's indices it's
    the mesh's `draw_indexed` (or `draw_indexed_indirect` for GPU instances), as before.
  - Why pulled and not the list's buffer as a vertex buffer: GPU lists' element layout (floats, then ints) isn't the
    vertex struct's, it'd need a pipeline layout of its own per list layout, and reads past the count wouldn't be
    zeros. Why GPU indices are read, not an index buffer: Unity's replay makes index views from CPU bytes, so a storage
    buffer as indices isn't replayable, and reading them costs one storage load a corner. So GPU indices need GPU
    vertices (an error with the fix: `Set` them into a GPUList).
- **Draw.Transform on indirect draws.** 2D was already moved (a 2D run's `moved` is the camera's, the renderer's);
  now pinned by a pixel test. 3D: a `gpu_instances` shader in 3D has 16 more uniform floats, pack-aligned, after its
  values (`shader.through_at`, `tide_shader.instance_through` = 1 + its first float word), and its instance loads
  end with `tide_moved = through * transform`. `tide_draw_shaded_indirect` copies the draw's uniforms and writes the
  place's `transform_3d` there (the identity when nothing moves it); one that mirrors sets the run's
  `TIDE_DRAW_MIRRORED` (`shaded_visible`), as for a CPU list. The renderer needs nothing.
- **Runs out of passes** (`move_work_out_of_passes`, compute.inc, before the frame's uploads): each run or list
  operation step moves back over the steps before it while `moves_before` says: not over another run or operation
  (they keep their order), not over a draw that reads a list it writes (`step_reads_list`: a run's storage ids and its
  indirect list; a kernel's written slots, `storage_writes` and its Adds' lists; an operation's list), and a run that
  samples a render target (`TIDE_PIXELS_TARGET*`) not over any step into a target. Indirect arguments come after each
  run as before (`prepare_storage_draws` from the moved step). `tide_platform_render_passes()` (platform.h) says how
  many passes the last list drawn began, as `tide_platform_draw_calls` does its calls.
- **fxc's races** (`shared_races`, compute_check.inc; replaced by a guard, "Closing the gaps" below). Measured with fxc 10.0.22621 (cs_5_0, `/WX`, Unity's
  `TideWarnings.hlsl`): user writes to a GPU list never trip X3694 (`tide_put`'s capacity check reads the buffer, which
  fxc can't see through); shared memory does, X3695, for a write of differing values at a place uniform to the group
  (a constant, a values field, `this.group`), under a uniform `if` too, a field of a shared struct, and `+=`; not for
  `this.local`/`this.index` places, a loop's index, a varying condition. One value for all was X3584, now turned off
  in `TideWarnings.hlsl` beside X3583. A guard (`if (local == 0)`) isn't exact: an invocation reading back its own
  write before a barrier could see the old value. So tidec warns (`group_uniform`: constants, the kernel's
  parameters, `this.group`/`this.count`, arithmetic, and unchanging locals of those; reached where every invocation
  is, by `barriers_uniform`'s rule plus uniform `if`s), with the fix: write from one invocation, then a barrier.
- **Tests.**
  - `compiler/tests/shaders/gpu_meshes` (golden, spirv-val, fxc): every form above, a vert's rolls over GPU indices,
    a values struct, CPU and GPU instances, 3D through. `kernels_add`'s golden gains its indirect draw's through.
  - `tidec_e2e_compute`: the through matrix in each indirect run's uniforms (identity, moved, mirrored and its
    `TIDE_DRAW_MIRRORED`), a GPU-vertex-and-index mesh's run (its lists, its stand-in mesh, its CPU transform), CPU
    indices kept as they are, CPU instances over GPU vertices.
  - Errors `gpu_mesh_cpu_vertices`, `gpu_mesh_lod`, `gpu_mesh_index_type`, `gpu_mesh_vertex_type`; `mesh_indices_type`'s
    text; warnings `kernel_shared_race` (6 warned, 7 forms not).
  - `tide_platform_kernels` pixels: a 2D indirect draw inside Draw.Transform, a mesh of a kernel's vertices and
    Add-ed indices (the middle square left out: the GPU's count), the same vertices by the CPU's indices with a vert
    and one triangle past them, a 3D indirect draw moved into view by Draw.Transform, a draw after a run that rewrote
    the vertices (the draws before it keep the old colors), and 2 render passes (3 with the move turned off, checked
    by hand). Vulkan with the validation layer, and WebGPU with a lost device.

## Closing the gaps (2026-11-05)

Task: the language/codegen gaps "Not built" listed, beside two agents' GPU-culling work (cull.tide, the renderer's
culling and indirect emission), which this leaves alone. Every pick is Claude's, Provisional, and in the spec's Built list.

- **GPU lists passed on alone.** A function's `GPUList<T>` parameter is `is_given` (shader.c): no argument, and the
  function is written for the storage slot its call names (`given.slot`, in the suffix as a list's texture number is),
  which `gpu_list_slot` resolves (a field of what's bound or given, or the caller's own such parameter through
  `em->spec`). Reads of the root's field are noted at the call (`gpu_list_slot` on the argument), so a function checked
  once still has every caller send its lists. `type_fits` still refuses one as a value (a local, a result, `?:`).
  - Writes in functions (checker, compute_check.inc): `gpu_list_write_ok` takes a function's own GPUList parameter or a
    field, in a function (then `gpu_only`, `TY_GPU_LIST`), and `note_gpu_write` notes it on the code being checked:
    a field on its `adds`/`atomics`/`gpu_writes`, a parameter by `param.gpu_written`/`gpu_adds`/`gpu_atomic`.
  - `settle_gpu_list_writes` (after every body, before `check_gpu_only_calls`): over `c->calls`, to a fixed point,
    a callee's field lists and its parameters' writes by what each call passes (`hand_on_writes`: a field, or the
    caller's parameter) go to the caller, so a kernel's lists say everything it reaches writes before `plan_kernel`.
    Then a frag or vert calling a writer is an error, an adder's call must be a statement (`call_site.statement`) and in
    no loop (`call_site.looped`: a loop's body, condition or step), each kernel and function adds once a path
    (`adds_through` over a `gpu_target`, a field or a parameter: a call of what adds to it counts one), and a kernel
    that adds has a group of one axis. Those last two moved here from `check_kernel_flow`, which runs before callees'
    bodies are checked.
  - Add in a function sets the kernel's `tide_adding<k>`/`tide_added<k>`, which are `var<private>`.
  - The CPU calling a function that writes a GPU list: `check_gpu_only_calls`' error, now "reads or writes".
- **Mirrored instances of indirect draws** (Provisional): a 3D `gpu_instances` shader whose element has a transform (or
  is one) has 4 more uniform floats after the through matrix (`shader.sides_at`, `tide_shader.instance_sides` = 1 +
  its float word). Its instance loads, before the through multiply, work out the instance's own determinant as
  draw.c's `mirrors()` does, term by term, and return a zeroed `tide_corner` (every corner one point: no triangle)
  where `tide_keep` (0 all, 1 those that don't mirror, 2 those that do) says the instance isn't the run's.
  `tide_draw_shaded_indirect` records two runs where the place culls (keep 1 with the place's mirror, keep 2 with the
  other), one with keep 0 under `Cull.None`. Two runs, not one with both sides and a frag's discard: back faces cost
  no fragments and early depth tests stay, for the second run's instance load and determinant per vertex. The
  renderer needs nothing: two indirect runs, each with its own arguments (`prepare_storage_draws`).
- **fxc's race, guarded exactly.** Measured with fxc 10.0.22621 (cs_5_0, /WX, `TideWarnings.hlsl`'s pragmas): a
  write by every invocation to one place of shared memory with differing values is X3695 bare, and under a condition
  on a read-only storage buffer's word too; under a condition on a groupshared word nothing writes (`tide_zero == 0`)
  it compiles, also for `+=`, a struct field, inside a uniform `if`, and a place held in a local that changes (so
  does one on a read-write storage word, which not every kernel has). The groupshared word is exact: workgroup memory
  starts zeroed (WGSL, and the translators' polyfill, notes/shaders.md) and nothing writes it, so every write happens
  for every invocation as written, on every backend. `writes_shared` (shader_compute.inc) puts every non-atomic
  assignment to a shared local under `if (tide_zero == 0u)`, and `write_kernel` declares `var<workgroup> tide_zero:
  u32` where one is. The warning (`shared_races`, `group_uniform`) is gone; its fixture is the golden
  `kernels_shared_races`, whose HLSL fxc compiles (tidec_hlsl_kernels_shared_races). Cost: a shared load and a branch
  a write.
- **Adds share one scan** in the kernel: `tide_scan` is `array<vec2u>` or `array<vec4u>` for 2 or 3 lists (a lane
  each; at most 3, as 7 storage buffers bind a list and a scratch each and the run's record; since "GPU lists in
  pools", any number, four a scan, `tide_scan1` and on), one Hillis-Steele loop
  for all of them, then each list's survivors and count by its lane. One list's WGSL is unchanged. The group's shared
  bytes now count the scan and `tide_zero` against 16 KiB (`plan_kernel`, an error at the run), which they didn't.
- **Draw.Bounds for GPU vertices over CPU instances:** already worked (`draw_bounds` takes the place's bounds where
  set, whatever the mesh); pinned by `tidec_e2e_compute` and documented.
- **Tests.**
  - Goldens: `kernels_helpers` (new: a helper written for two slots, reads, writes, atomics and Adds through helpers
    two deep, a helper given the struct, a frag reading through a helper), `kernels_shared_races` (new, the warning's
    forms, guarded), `kernels_shared` (its writes guarded), `kernels_add` (its two Adds' scan shared), `gpu_meshes`
    (its transformed GPU instances' sides). Each through spirv-val and fxc.
  - Errors: `gpu_list_param_add_loop`, `_add_twice`, `_add_value`, `_helper_twice` (new); `gpu_list_write_function`
    (now a frag calling a writer), `gpu_list_local_on_gpu` (now a local keeping one), `gpu_list_cpu_calls_gpu_function`'s
    text. Warnings: `kernel_shared_race` gone.
  - `tidec_e2e_compute`: a culled indirect draw of transformed instances is two runs (keep 1 and 2, the second
    mirrored) and one under Cull.None (keep 0); Blades (no transform) have no sides; GPU vertices over CPU instances
    culled by Draw.Bounds (the test now sets a screen size, which a frustum needs).
  - `tide_platform_kernels`: helpers writing by index, atomics and Adds (across 3 groups), a kernel's Add beside a
    helper's to another list in one scan, read back; two boxes of a GPU list drawn culled, one's own transform
    mirrored, both drawn, and a frag reading a GPU list through a helper (with the split turned off the mirrored one
    is black, checked by hand). Vulkan with the validation layer, and WebGPU with a lost device.

## gpu.h contract (2026-11-05)

- `TIDE_GPU_STORAGE` buffers: storage, vertices, indices, indirect arguments,
  copy source/destination, readback. Zeroed when made with no data.
- Storage buffer `i` of a pipeline is at binding `TIDE_GPU_STORAGE_BINDING + i`
  (33 + i since 2026-11-05, 25 + i before) of group 0, at most `TIDE_GPU_MAX_STORAGE` (4 since "GPU lists in
  pools", 7 before), after the uniform block (0) and 16 textures' and samplers' bindings (1..32): notes/rendering.md,
  Sixteen textures a draw. A pipeline's layout is explicit (not WGSL's `auto`), so a binding the shader leaves unused
  is still bound.
- `storage_most` (optional): the most bytes a storage buffer bound whole can be,
  a multiple of 16; NULL for 128 MiB. What the pools grow to.
- `tide_gpu_pipeline_desc.storage_count`, `.storage_writes[i]`: read-only
  (`var<storage, read>`) or written (`var<storage, read_write>`). Only compute
  pipelines write. Vertex and fragment stages read storage.
- `.compute`: the WGSL/SPIR-V entry point is `compute`; vertex/fragment fields
  are ignored. Used between `compute_begin` and `compute_end`, outside passes.
- A compute pass sees everything recorded before it and everything after it
  sees what it wrote. Backends place the barriers (Vulkan: a full
  graphics/compute/transfer -> compute barrier at begin, and compute writes ->
  indirect/vertex input/vertex/fragment/compute/transfer reads at end; within
  one compute pass, between dispatches, compute -> compute).
- `draw_indirect` (4 u32s), `draw_indexed_indirect` (5 u32s), first instance
  always 0. `dispatch_indirect` (3 u32s).
- `draw_indexed_indirect_count(buffer, offset, most, counts, count_offset)`:
  records of 5 u32s 20 bytes apart, as many as the u32 at `count_offset` of
  storage buffer `counts`, at most `most`; records past the count must be of no
  instances (a backend without `TIDE_GPU_INDIRECT_COUNT` draws all `most`).
  `indirect()` gives `TIDE_GPU_INDIRECT_COUNT` (Vulkan `drawIndirectCount`,
  WebGPU multi-draw-indirect) and `TIDE_GPU_INDIRECT_FIRST_INSTANCE` (Vulkan
  `drawIndirectFirstInstance`, WebGPU `indirect-first-instance`; else records'
  first instances must be 0); the recorder says neither. Test:
  `tide_platform_compute` (two records counted of three, the second's first
  index and first instance, a count of none).
- `buffer_copy` copies from an index buffer too, to a storage buffer.
- `buffer_copy` outside passes, ordered with the passes around it.
- `buffer_read` begins an asynchronous read (token), `buffer_read_done` polls
  (1 done, 0 not yet, -1 lost), `buffer_read_wait` waits for every pending
  read (NULL on the web).
- Every backend has all of these (the GL backend, which left them NULL, is gone).
- Test: `platform/tests/compute.c` (`tide_platform_compute`, native Vulkan with
  label `gpu`, and a WebGPU page), WGSL written by hand. Pipelines are made
  first and recorded once `pipeline_ready` says they're made. On WebGPU it
  also loses the device with a read pending (which must give -1) and repeats
  everything on the next device with background pipelines. A backend that
  can't compute fails it (the page's WebGL run, which printed `SKIPPED:`, is
  gone).

## Language and runtime design (Claude's, Provisional, 2026-11-05)

Everything here implements the spec section; where the spec and this differ,
the spec is updated with the pick.

### `GPUList<T>`

- A generic built-in type, `TY_GPU_LIST`, whose decl holds one field, the
  element (as `TY_LIST`'s does). Elements are GPU values (`type_fits` in
  shader.c): numbers, vectors, colors, quaternions, matrices, bools, bytes,
  enums, arrays and structs of them. Machine-local, as `RenderTarget`: a
  match component or singleton can't hold one; a `local` field, a local
  singleton's field, a local variable and a parameter can. It is a value
  that shares: copies are the same list.
- C: `tide_gpu_list` (`tide/draw.h`), `{ uint64_t id; }`, 8 bytes. 0 is no list:
  the default value, which Run and draws over treat as empty. A made list's
  id is its state's address with a managed bit, as a managed render
  target's is: `tide_gpu_list_state { next, capacity, stride (words an
  element), version, content_revision, alive, memory, reload_id }`, kept in
  the engine's list of them, freed at unload, adopted and recreated across
  hot reloads where render targets are (`gen_resource_fields`). A changed
  element layout is another stride: the renderer makes the buffer again,
  empty, and the revision changes.
- GPU layout: one `TIDE_GPU_STORAGE` buffer, 16 + capacity * stride * 4
  bytes (since "GPU lists in pools", a range of one of two pools, as many words
  rounded up to 4). Words 0..3 are a header: count, capacity, 0, 0. Element i starts at
  word 4 + i * stride. An element's words are its floats' then its ints'
  (`put_value`'s order, whole elements: no parts are left out, since kernels
  write them). stride is the element's word count exactly.
- CPU API (local code): `GPUList<T>(capacity)`; `Resize(capacity)` (empties
  it); `Dispose()`; `Set(List<T>)` (sends the list, count = its count,
  capped at the capacity); `Clear()` (count 0); `SetCount(n)` (the count,
  clamped to 0..capacity, elements kept); `capacity` (int);
  `contentRevision` (long, as a target's); `await Read()` (`List<T>`) and
  `await ReadCount()` (int), local async only. `count` in CPU code is an
  error that says to read it back. Set, Clear, Run and Read are recorded in
  the frame's draw list, in order with draws; Resize and Dispose change the
  state, which the renderer reads.
- GPU API: frags, verts and kernels read `list[i]` (zeros past the count),
  `list.count`, `list.capacity`, `foreach`. Kernels also write: `list[i] = v`
  (dropped past the capacity; the count stays: only `Add`, `Set`, `Clear` and
  `SetCount(n)` change it, so a list written by index says its count from the
  CPU, in frame order), `list.Add(v)` (below), and
  `GPUList<int>` atomics (`AtomicAdd`, `AtomicSub`, `AtomicMin`,
  `AtomicMax`, `AtomicAnd`, `AtomicOr`, `AtomicXor`, `AtomicExchange`,
  `AtomicCompareExchange(i, compare, value)`, each returning the old value).
- GPU code reaches a GPUList through a field of a component, singleton or
  values struct it takes (local fields included), or a function's GPUList
  parameter (since "Closing the gaps"). Each distinct list a
  shader names is one storage slot (at most 7 with the engine's own, below;
  since "GPU lists in pools", 64, each a range of a pool);
  the slot is written (`read_write`) only in a kernel that writes it, and
  declared `array<atomic<i32>>` in a kernel that uses atomics on it (since, the
  pool of ints is, in a kernel that uses atomics on any list of it).

### Kernels

- `compute Name(params)` is a `DECL_SHADER` with `is_compute` (frags and
  verts keep `is_frag`). `[Group(x[, y[, z]])]` on it, 64 when left out,
  within 256 invocations and z <= 64. Parameters: components and singletons
  (of the entity the calling view runs for, as a frag's), and at most one
  struct, the run's values. No vertex, no `out`.
- `this` is a built-in record, `Invocation`: `index` (int, the invocation's
  place in the whole run, x + y * size.x + z * size.x * size.y), `at`
  (int3), `local` (int3), `group` (int3), `count` (int3, the run's size).
- A kernel's invocations past the run's size don't run its body, unless it
  calls `GPU.Barrier()`: then every invocation of a group runs it, since a
  barrier needs them all, and the kernel checks `this.index` itself.
- `shared T name;` (T a fixed-size array or any GPU value) is a statement of
  a kernel's own body, top level: a `var<workgroup>`. Zeroed as the group
  starts. Atomics on a `shared int[N]`'s elements are the same methods.
- `GPU.Barrier()`: `workgroupBarrier(); storageBarrier();`. Only in a
  kernel's own body, not in a branch or a loop whose condition can differ
  between a group's invocations (tidec's conservative check: inside no
  `if`/`while`/`switch`, and inside `for` loops only with constant bounds),
  and with no `return` before it.
- WGSL: `@compute @workgroup_size(x, y, z) fn compute(...)`; the user's body
  is a function the entry point calls, so the entry point's own code (the
  guard, `Add`'s compaction) runs in uniform control flow after it.

### `GPU.Run(kernel, over[, values])`

- A statement in drawing code (views, local handlers that draw, drawing
  functions), recorded into the draw list as a compute command in order with
  draws (`tide_draw_compute`, generated through a `tide_run_<n>` wrapper as
  draws go through `tide_shade_<n>`): the kernel's shader, its uniforms
  (bound singletons' and components' read fields, values), its textures
  (data textures for `List<T>` fields, as frags have), its storage lists, and
  the size: an int, int2 or int3 of invocations, a `List<T>`'s count, or a
  GPUList's GPU-side count (indirect).
- The run record: every kernel binds an engine storage buffer at the last
  storage slot it uses + 1 (`tide_run`; since "GPU lists in pools", at binding 35): words 0..2 the dispatch's groups,
  3 the total, 4..6 the size. A direct run's is written by the CPU; an
  indirect run's by the engine's `tide_run_size` compute pipeline from the
  list's count (groups = ceil(count / group size)), then `dispatch_indirect`
  reads words 0..2. Records come from a pool of small storage buffers.
- `Add` lowering: a kernel that adds to a list L keeps, per invocation, a
  flag and the value's words in private variables, which the body sets. After
  the body the entry point scans the group's flags in workgroup memory and
  writes the group's survivors, compacted, to a scratch buffer (another
  storage slot) at group * groupsize, and the group's count to the
  scratch's counts. The engine then runs `tide_add_offsets` (one group:
  exclusive prefix over groups, base = L's count, L's count = min(base +
  total, capacity)) and `tide_add_scatter` (each survivor's words to
  L[base + offset + j], dropped past the capacity), indirectly for an
  indirect run. Order is invocation order whatever the GPU's order: bit-stable.
  At most once per list per invocation on any path (checked like definite
  assignment), not in a loop.
- Frame order: compute commands end any open pass; the renderer issues
  `compute_begin`/`dispatch`/`compute_end` and the engine's helpers in
  recording order. List `Set`/`Clear` are `buffer_write`s outside passes.
  (Since 2026-11-05 one that no draw before it depends on goes before those draws: Runs out of passes, above.)

### Indirect draws

- `Draw.Instanced(mesh, gpuList, vert, frag[, values])`: the element type is
  the instance struct (with its optional `float4x4 transform`, as a list's);
  the vertex stage reads instance `instance_index` from the list's storage
  slot. Recorded as one command; the renderer writes the mesh's index
  count into an indirect argument record and `buffer_copy`s the list's count
  into its instance count, then `draw_indexed_indirect`. No CPU culling or
  level picking.

### Readback

- `await list.Read()`: a read request in the draw list; the renderer begins
  `buffer_read` when it reaches it and the awaitable completes when
  `buffer_read_done` says so (headless runs: `buffer_read_wait` at the next
  frame's start, so runs repeat exactly). A lost device fails it
  (`IOError`). Pending reads are bounded (64); past that, the awaitable waits
  for a slot.

## The WGSL tidec writes for kernels and storage

What the translators take (SPIR-V for Vulkan, HLSL for Unity):

- `@group(0) @binding(33 + i) var<storage, read> name: array<T>;` and
  `var<storage, read_write>`, `T` one of `u32`, `i32`, `f32`, `vec2<f32>`,
  `vec4<u32>`, `atomic<u32>`, `atomic<i32>`. Runtime-sized. `arrayLength(&name)`.
- `var<workgroup> name: T;` for any type, including arrays of structs and
  `atomic<u32>`/`atomic<i32>` and arrays of them.
- `@compute @workgroup_size(x[, y[, z]]) fn compute(...)` with the built-ins
  `global_invocation_id`, `local_invocation_id`, `local_invocation_index`,
  `workgroup_id`, `num_workgroups`.
- `workgroupBarrier()`, `storageBarrier()`.
- `atomicLoad`, `atomicStore`, `atomicAdd`, `atomicSub`, `atomicMax`,
  `atomicMin`, `atomicAnd`, `atomicOr`, `atomicXor`, `atomicExchange`, and
  `atomicCompareExchangeWeak` with `.old_value` and `.exchanged`, each on
  `&name[i]` or `&name`, and on a workgroup struct's member, `&name.member[i]`
  or `&name.member` (tidec's shared atomics since 2026-11-05: notes/shaders.md,
  Kernels on Dawn's Direct3D 11).
- Storage reads in vertex and fragment stages (read-only).

## The translators (2026-11-05)

Both take the whole list above, `vec4<f32>`, `vec2<u32>` and `vec4<i32>`
(any scalar or vector but bool) as storage elements, compute built-ins in any
order and inside a struct parameter, and a uniform block that is a number,
vector or matrix rather than a struct (`var<uniform> u: vec4<u32>`, which
`platform/tests/compute.c` writes). Existing shaders' SPIR-V and HLSL came out
byte-identical (every `compiler/tests/shaders` fixture and the renderer's 10).
Details and tests are in `notes/shaders.md` (SPIR-V, for Vulkan; HLSL, for
Unity).

- **Out of range, as Tint:** a runtime-sized array's index, constant or not,
  is `min(i, arrayLength - 1)` for reads, writes and atomics; workgroup arrays
  clamp as other fixed arrays do.
- **Workgroup memory is zeroed by the kernel itself** as it starts (Tint's
  polyfill: each invocation an array's elements from its local index by the
  workgroup's size, the first invocation everything else, then a workgroup
  barrier), so Vulkan needs no `shaderZeroInitializeWorkgroupMemory`.
- **The Unity replay's contract** (the HLSL `TideRender.cs`'s compute binds):
  - A compute program is one kernel, `tidehl_compute`, in a `.compute` file
    (`tide_hlsl_unity_compute`), its `[numthreads]` the WGSL's
    `@workgroup_size`.
  - Storage buffer i (binding 33 + i) is `_TideStorage<i>`:
    `ByteAddressBuffer` for `read`, `RWByteAddressBuffer` for `read_write`,
    in every stage. Bind it by that name (`ComputeShader.SetBuffer`,
    `CommandBuffer.SetComputeBufferParam`, or a material's/global buffer for a
    draw). Raw, so one `GraphicsBuffer` with `Target.Raw` (plus `Vertex`,
    `Index`, `IndirectArguments`, `CopySource`/`CopyDestination` as gpu.h's
    `TIDE_GPU_STORAGE` flags) serves every use: elements are at WGSL's stride
    and the length is the buffer's size over it, so the buffer's own stride
    doesn't matter.
  - The uniform block is `cbuffer TideUniforms`, as for draws (its bytes as
    WGSL lays them out).
  - `num_workgroups` is `uint3 _TideGroups`, alone in `cbuffer TideGroups`
    (16 bytes: x, y, z, padding). Set it to the dispatch's group counts; for
    an indirect dispatch copy the arguments' 3 words into a constant buffer
    bound as `TideGroups`. Every compute program declares it.
  - Workgroup memory, barriers and atomics need nothing from the replay.
  - A draw shader that reads storage has no OpenGL ES 3.0/WebGL 2 subshader
    (`tide_hlsl_unity_shader`), which have no storage buffers.
- **fxc (Unity's Direct3D 11 compiler) refuses races it can see:** a write
  every invocation makes, unconditionally, to one place (a workgroup
  variable, or an element at a constant index of one or of storage) with
  values that differ: X3695 for workgroup memory, X3694 for storage, errors
  no pragma turns off, and a uniform condition doesn't help. WGSL allows it
  and SPIR-V takes it; tidec's kernels write one place under a condition on
  the invocation (`if (local == 0u)`) or by its index. One value every
  invocation writes alike is only a warning (X3583), which
  `TideWarnings.hlsl` disables. Unity's other compilers (DXC for Direct3D 12
  and Vulkan, Metal) don't check this.
- **Run on a GPU:** the SPIR-V by the Vulkan backend (`tide_platform_compute`,
  `tide_platform_kernels`); the HLSL by Unity's replay, a kernel and a draw
  reading storage, on Direct3D 11 and 12, Vulkan and OpenGL Core (the Sample's
  `ComputeReplayTests`, `notes/unity.md`). Metal and WebGPU in Unity aren't run.
