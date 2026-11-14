# Production reversed depth (2026-11-14)

Owner approved the investigation: "go with recommendations". The implementation keeps all public clip,
camera and forward depth conventions, reverses only the final GPU conversion, uses combined D32 float/stencil
where available, and retains the forward legacy path where it is not. The historical measurements and rejected
private-matrix variant are in depth-precision-investigation.md. No experimental projection or mode switch was merged.

## Provisional implementation choices

- One Vulkan sampled/attachment/transfer-capable combined format per device; requested sample counts reduce
  to supported counts. D32 is preferred, D24 forward is the fallback; no usable combined format retains the
  existing unsupported-device diagnostic.
- A private shader constant selects the convention: SPIR-V SpecId 0 on all pipeline stages, source substitution
  before WebGPU module caching. No extra game uniform or public runtime call.
- Hi-Z converts every source sample to forward depth before its existing MAX pyramid. The existing nearest
  bounds, empty=1, positive-float bit order and 2^-20 conservative allowance remain valid. This can miss culling
  opportunities, not visible objects. Farthest raw Vulkan resolves instead use MIN, conditional on MIN support.
- Unity prefers D32 render attachments and retains its native depth convention. HLSL distinguishes old forward
  WGSL fixtures from new reversed WGSL, avoiding double conversion of clip, depth, loads, PCF and SV_Depth.
- Resource estimates count nominal 8-byte combined float depth/stencil samples and resolves, 4-byte fallback;
  Unity reports the choice through bit 31 of render_formats. Physical alignment/compression remain unestimated.

Incoming dev work was integrated through 70a9573be (SampleLevel, WriteDepth and DepthBias). WriteDepth converts
its forward argument once at the write; branches not writing depth retain the exact raw raster bits. Both
constant and slope bias change sign at the Vulkan/WebGPU callback boundary for reversed storage only; Unity's
positive-away SetGlobalDepthBias contract is retained. The merged DepthBias packet uses embed ABI 21; depth
storage itself adds no replay packet. No simulation/random/pose/world-hash pin was changed.

## Validation

Windows, Clang 23.1.2, Node 24.22.1, hidden native windows and muted headless Chrome:

- Full native sanitize suite: all 3,384 tests executed. Its four initial failures were the legacy hand-written
  draw shader's forward conversion (three configurations) and the missing offscreen clear prewarm. They were
  fixed without changing pixel expectations or the exact prewarm draw-call assertion. The 13 affected native
  GPU fixtures then passed, including float and forced fallback targets/shaders/occlusion.
- Full web-debug suite: all 3,190 tests executed on software WebGPU. Its five initial failures were the same
  two hand-written draw configurations and three diagnostic expectations stale after incoming WriteDepth.
  Corrected cases passed, then all 13 affected web fixtures passed, including recovery and forced fallback.
- After DepthBias integration: 200 native compiler/golden/SPIR-V/FXC/diagnostic/recorder/GPU checks passed,
  118 draw-filtered runtime checks passed, and seven web overlap checks passed. The actual fallback storage
  byte count and 1/4 requested sample counts are asserted, rather than inferred from a switch.
- The distant-pair grid gives zero wrong/blank pixels across all 3,168 built-in/custom/explicit-clip observations
  on Vulkan and hardware WebGPU, default near=.3/infinite far and custom near/far included. Seven public
  convention/prepass/clear checks pass at 1 and 4 samples, with forced fallback and web device recovery.
- Hardware WebGPU D3D12: six depth/target/occlusion/shader checks passed after the final bias merge. D3D11:
  the six depth cases passed before the bias merge, with merged bias/read overlap checks repeated afterward.
- All 64 changed shader goldens were mechanically reviewed against dev: removing only private helpers,
  final clip conversion and raw/public depth wrappers reproduces the original text exactly. Color/world pins
  were not regenerated. All 16 web-runtime unit checks pass; the release JavaScript is regenerated.
- Unity 6000.6.0f1: ten ComputeReplayTests pass on D3D11, D3D12, Vulkan and OpenGL Core. On the final ABI-21
  game/runtime, 13 replay/drawing/hot-reload cases pass on D3D11. Shader compilation and generated-doc XML
  EditMode cases pass; the encountered Assembly.Location analyzer warnings were corrected. The broad initial
  PlayMode run was 31/33: the live-build test passes with the checkout's build service running; ScaleTests still
  observes two whole-frame allocations while showing itself allocates none. That unrelated allocation
  assertion was not loosened (unity.md records the diagnostic).
- The docs production build passes. The native/web full builds and all affected overlap builds are warning-free
  for C and generated shaders. Linux/macOS/Android build validation follows the ordinary Build workflow.

## Single release before/after measurement

One process per version, BelowNormal, no sanitizers, at most two build workers; no engine worker pool was
installed and background pipelines were disabled. bench_render3d: 10,000 meshes, mixed lighting, package path,
1920x1080, 30 warmup and 150 measured frames, one repeat. No other process was stopped. Other desktop activity
and GPU clocks were uncontrolled. Both versions pass their package/direct equivalence checks, submit 3,240,048
vertices/indices in 12 draws, eight shadow views, zero texture upload, and world hash f99077b76e7433ee.

| Measure | Before | After |
|---|---:|---:|
| GPU p50 | 2.318384 ms | 2.303120 ms |
| GPU p95 | 3.303456 ms | 2.317120 ms |
| Renderer memory estimate | 75,890,528 bytes | 110,210,916 bytes |
| Renderer memory estimate | 72.37485 MiB | 105.10532 MiB |

The observed median change is -0.66%, not a speedup claim or an isolated format-cost estimate: the after build
also includes the concurrent SampleLevel/WriteDepth/DepthBias commits. It is a single process pair, not a
statistical confidence interval. Nominal combined depth/stencil storage doubles from four to eight bytes per
sample; the total renderer estimate rises by 34,320,388 bytes (32.73047 MiB). This is not measured VRAM.
Raw complete rows and source-cohort/method details: repro/depth-production.json.

External graphics verification still needs another GPU vendor, genuinely feature-missing WebGPU hardware,
and Metal. The forced fallback tests cover the logical missing-feature path on this machine.
