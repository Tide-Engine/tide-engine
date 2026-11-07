# Renderer overhead investigation, 2026-11-02

Release Clang 23.1.2, Windows, Ryzen 9 7900X / RTX 4080 SUPER.
Source: `fb675d4eb7b60cb045059d06fb5d4284ff784486`.

The completed Render3D baseline has six alternating package/cached-direct
processes, 24 rows, 2,400 measured frames and 2,400 completed GPU samples.
Each uses 20 warmup frames, 1920x1080 and unchanged production shaders.
Native Vulkan quick correctness passes. Every measured row retains full native
pixel and expanded-command equality. Within each count/case, both ways retain
equal world hashes, draws, submitted vertices and zero warmed texture uploads.

Median of three process medians, milliseconds:

| Meshes / case | Package recording | Cached recording | Package submission | GPU |
| --- | ---: | ---: | ---: | ---: |
| 1,000 / unshadowed | 0.2612 | 0.0081 | 0.0373 | 1.7568 |
| 1,000 / mixed | 0.3807 | 0.0307 | 0.0634 | 1.2545 |
| 10,000 / unshadowed | 2.7188 | 0.0759 | 0.1177 | 2.7375 |
| 10,000 / mixed | 4.2718 | 0.3332 | 0.3656 | 2.0831 |

Unshadowed uses four calls; mixed uses twelve, including eight shadow views.
The cached path omits recurring scene collection and preparation: it is a
static-scene floor, not a replacement for dynamic rendering. These results
point toward scene recording as the larger CPU opportunity in this workload;
they do not isolate which part of recording is responsible. Zero texture
uploads does not mean zero instance-buffer uploads.

The runner held the shared mutex with an explicit five-second quiet guard and
polled known contenders throughout. No contention was reported in the completed
baseline. GPU clock observations are retained in gpu.csv; clocks and unrelated
desktop activity were not controlled. Native readback synchronizes frames;
CPU/GPU intervals overlap and are not added or converted to shipping FPS.

Two earlier attempts are retained as failed attempts: log hashing raced the
PowerShell redirected writer. The runner now waits up to five seconds to acquire
an exclusive read handle, then obtains both length and SHA-256 from that handle.
The complete six-process run exercises the corrected path. Files still locked
after the deadline fail rather than being hashed while writable.

## Rejected conversion probe

The only renderer probe added `restrict` to the local output pointer in
`add_plain_run`, documenting that renderer shape storage cannot alias draw-list
plain records. Three alternating before/after rounds on Vulkan and OpenGL cover
all fifteen draw workloads: 180 rows and 18,000 measured frames, with 20 warmups
per workload. Raw rows remain in draw.jsonl and probe-rows.jsonl; executable
identities and contention observations are in probe-metadata.json.

It establishes no reliable improvement: Vulkan rect render/readback medians
before are 0.28305/0.27260/0.27155 ms and after 0.27475/0.27690/0.27215;
circles are similarly mixed. Unchanged controls also vary, including adverse
after rows. The probe is reverted. No renderer optimization is shipped and no
pixel-equivalence or GPU speedup claim is made for this rejected probe.

Large binaries and PNGs stay in build/render3d-measurements; their identities
are in the exported metadata. Raw paths in metadata refer to that original
evidence. No web timing or new browser/sanitizer coverage is claimed: the only
retained executable change is the Windows benchmark runner.

Reproduce the baseline with `bench/render3d/run.ps1 -Counts 1000,10000
-Cases unshadowed,mixed -QuietSeconds 5 -MonitorClocks -Capture`, supplying a
fresh label and the actual built revision after building bench_render3d.
