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
- [ ] Vulkan backend (`gpu_vk.c`)
- [ ] WebGPU backend (`web/gpu_web.c`, `web/tide.js`)
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
  label `gpu`, and a WebGPU page), WGSL written by hand.

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
