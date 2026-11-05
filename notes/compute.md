# Compute on the GPU

Language rules: `docs/spec.md`, Compute on the GPU (decided 2026-11-05 where it
says so; every other rule is Claude's, Provisional). The owner's instruction for
the implementation (2026-11-05): build it to release-ready, compute shaders
properly working, every pick Provisional, and the last push to `dev` publishing
a release. The decided backend rule: compute is guaranteed on every render
backend, and one that can't run it (OpenGL 3.3, OpenGL ES 3, WebGL 2) is removed.

## Progress

- [ ] `gpu.h` compute API (storage buffers, compute passes, dispatch and
      indirect dispatch, indirect draws, copies, asynchronous reads)
- [x] Vulkan backend (`gpu_vk.c`; `notes/rendering.md`, Vulkan compute)
- [x] WebGPU backend (`web/gpu_web.c`, `web/tide.js`), 2026-11-05: notes/web.md,
      WebGPU compute
- [ ] Recorder for Unity (`gpu_record.c`) and its replay (`TideRender.cs`)
- [x] WGSL compute and storage in the translators (`wgsl_read.inc`, `spirv.c`, `hlsl.c`), 2026-11-05:
      see "The translators" below and `notes/shaders.md`
- [ ] tidec: `compute`, `GPUList<T>`, `GPU.Run`, ordered `Add`, atomics,
      `shared`, `GPU.Barrier`, `await list.Read()`
- [ ] Engine/renderer: GPU lists in the draw list, compute commands, ordering
- [ ] Indirect draws over a `GPUList`
- [ ] GL backends removed
- [ ] Engine GPU culling of listed runs
- [ ] Docs: `docs/game/shaders.md`, spec, editors

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
- `tide_gpu_gl` leaves all of these NULL.
- Test: `platform/tests/compute.c` (`tide_platform_compute`, native Vulkan with
  label `gpu`, and a WebGPU page), WGSL written by hand. Pipelines are made
  first and recorded once `pipeline_ready` says they're made. On WebGPU it
  also loses the device with a read pending (which must give -1) and repeats
  everything on the next device with background pipelines. The page's WebGL
  test prints `SKIPPED:`; natively a backend that can't compute fails it.

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
