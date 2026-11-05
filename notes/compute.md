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
- [x] Recorder for Unity (`gpu_record.c`), 2026-11-05; its replay (`TideRender.cs`) not built
- [x] WGSL compute and storage in the translators (`wgsl_read.inc`, `spirv.c`, `hlsl.c`), 2026-11-05:
      see "The translators" below and `notes/shaders.md`
- [x] tidec: `compute`, `GPUList<T>`, `GPU.Run`, ordered `Add`, atomics,
      `shared`, `GPU.Barrier`, `await list.Read()`, 2026-11-05 ("Built" below)
- [x] Engine/renderer: GPU lists in the draw list, compute commands, ordering
- [x] Indirect draws over a `GPUList` (`Draw.Instanced`; `Draw.Mesh` over GPU lists not built)
- [x] GL backends removed, 2026-11-05 (`notes/rendering.md`, GL backends removed)
- [ ] Engine GPU culling of listed runs
- [x] Docs: `docs/game/shaders.md`, spec, editors

## Built: the language, the engine and the renderer (2026-11-05)

Everything between the language and gpu.h, as "Language and runtime design"
above has it, with these deviations (each also in the spec's "Built" list):
`this.at` is always an `int3`; ordered `Add` needs a group of one axis (a 2D
group's local order isn't this.index's, which the first GPU run found);
each list a kernel adds to has its own scan; `GPUList` is 16 bytes (`id`,
then the capacity it was made or recreated with and its stride, for a web
reload, which can't read another address space's state); reads back begin as
the next drawn frame's work ends (not a draw-list command: the awaiting task
has no draw list); a 2D or 3D `Draw.Instanced` over a GPU list is not moved by
`Draw.Transform`.

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
  storage slots [lists][Add scratch][run record], at most 7, and `write_kernel`
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
- The recorder writes the new ops after the existing ones (`notes/unity.md`).

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

- The engine's GPU culling of listed runs (the next step of the plan; the GL
  backends' removal, the one before it, is done).
- `Draw.Mesh` over GPU-list vertices and indices; `DrawIndirectCount`.
- A frag or vert reading a GPU list through a function given a struct works by
  field; one passed on alone is refused.
- `tide unity`'s `.compute` file per kernel, and the C# replay of the recorder's
  new ops (the Unity task; `TIDE_EMBED_ABI` stays 10 until it's built).
- A run moved out of the render pass it fell in: runs end any open pass.
- `Draw.Transform` on indirect draws.

## gpu.h contract (2026-11-05)

- `TIDE_GPU_STORAGE` buffers: storage, vertices, indices, indirect arguments,
  copy source/destination, readback. Zeroed when made with no data.
- Storage buffer `i` of a pipeline is at binding `TIDE_GPU_STORAGE_BINDING + i`
  (25 + i) of group 0, at most `TIDE_GPU_MAX_STORAGE` (7): with the uniform
  block (0) and 12 texture/sampler pairs (1..24) that's 32 descriptors,
  Vulkan's least `maxPushDescriptors`. A pipeline's layout is explicit (not
  WGSL's `auto`), so a binding the shader leaves unused is still bound.
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
  bytes. Words 0..3 are a header: count, capacity, 0, 0. Element i starts at
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
  values struct it takes (local fields included). Each distinct list a
  shader names is one storage slot (at most 7 with the engine's own, below);
  the slot is written (`read_write`) only in a kernel that writes it, and
  declared `array<atomic<i32>>` in a kernel that uses atomics on it.

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
  storage slot it uses + 1 (`tide_run`): words 0..2 the dispatch's groups,
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

- `@group(0) @binding(25 + i) var<storage, read> name: array<T>;` and
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
  `&name[i]` or `&name`.
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
- **The Unity replay's contract** (HLSL, for whoever builds `TideRender.cs`'s
  compute):
  - A compute program is one kernel, `tidehl_compute`, in a `.compute` file
    (`tide_hlsl_unity_compute`), its `[numthreads]` the WGSL's
    `@workgroup_size`.
  - Storage buffer i (binding 25 + i) is `_TideStorage<i>`:
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
- **Not run on a GPU yet:** the Vulkan backend's compute and the Unity replay
  aren't built, so the translations are checked by spirv-val (Vulkan 1.3) and
  fxc only; `tide_platform_compute` exercises them once `gpu_vk.c` has compute.
