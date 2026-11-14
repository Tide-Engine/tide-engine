# Depth precision investigation (2026-11-14)

Historical findings; the experimental implementation was not merged. Owner approved the
final-remap recommendation on 2026-11-14: "go with recommendations".

On this machine's NVIDIA GPU, Vulkan and hardware WebGPU agreed. For the default camera
(near 0.3, infinite far), farther surfaces won 62/192 cases with forward storage, both
with D24 and D32. Reversing only at final clip conversion with D32 gave 0/192 wrong.
Across the custom-camera grid the counts were 274/864 forward and 0/864 reversed, per path.
The built-in, custom frag and explicit-clip paths totalled 3,168 observations per mode
per backend. This is one GPU vendor, not independent hardware evidence.

Seven public convention/pre-pass/clear cases at one and four samples passed 14/14 on each
backend with final remapping. Reversing privately inside the projection matrix passed
12/14: explicit `this.clip` writeback failed exact `Depth.Equal` at both sample counts.
That variant was rejected because reconstructing rounded public clip coordinates cannot
recover the original precise depth bits.

Forward D32 alone gave no precision improvement. Final remapping keeps the OpenGL
projection and changes only `(z+w)/2` to `(w-z)/2`. Public forward reads retain their
meaning but cannot express the extra far precision. Arbitrary custom clip values may
already have lost precision before conversion.

A static control used the existing `bench_render3d`: 10,000 meshes, mixed lighting, package path, 1920×1080, 30 warmup + 150 timed frames, one repeat per process. Every run submitted 3,240,048 indices and 12 draw calls per frame, eight shadow views, no texture upload. The benchmark's package/direct equivalence checks passed. This control uses an orthographic camera, so it measures format cost, not distant perspective precision.

| Alternating pair | Static control D24 GPU p50 ms | D32 GPU p50 ms | Change |
|---:|---:|---:|---:|
| 1 | 2.768 | 2.796 | +1.0% |
| 2 | 2.707 | 2.777 | +2.6% |
| 3 | 2.653 | 2.773 | +4.5% |

No claim of a universal performance percentage: these short, serial, warm-driver runs share the owner's desktop GPU. The control's recorded-command digest differs between processes; it includes captured resource identities. Geometry counts, lighting configuration, world hash and equivalence flags are retained in the raw records; cross-process pixel identity was not asserted.

Memory below is **measured VkMemoryRequirements.size**, not the renderer's estimated memory statistic:

| Depth image | D24 requirement | D32 requirement |
|---|---:|---:|
| 1280×720, one sample | 3.75 MiB | 7.50 MiB |
| 1280×720, four samples | 15.00 MiB | 30.00 MiB |
| 2048×2048 shadow map, one sample | 16.00 MiB | 32.00 MiB |
| 33 depth image creation requests across the M&R route | 210.166 MiB | 420.330 MiB |

All three runs per format had the same depth-image request list and summed size. The last row is cumulative allocation requirements across creations, **not peak live VRAM**, allocator-block residency, compression/bandwidth or total game memory. The 1×1 allocation rounds to 512 bytes in both formats. Tide's existing `gpu_memory_bytes` estimate in the static control stayed 75,890,528 in both modes; it is not format-aware enough to measure this change. The production change corrects target/depth/resolve accounting.
