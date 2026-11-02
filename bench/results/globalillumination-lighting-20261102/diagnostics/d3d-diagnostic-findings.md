# D3D11/WARP room diagnostic, 2026-11-02

This is one diagnostic run outside CTest, not a validation pass or a performance comparison. The normal 120-second runner guard remains unchanged. A private ignored copy changes only that runner guard and its timeout message to 240 seconds; browser_dump.mjs is copied unchanged. Captures, quality, counts, query, virtual-time budget and assertions are unchanged. No build or source edit was performed for this diagnostic.

The frozen room page completed with its original successful assertions in **138.0075276 seconds**, on `ANGLE (Microsoft, Microsoft Basic Render Driver (0x0000008C) Direct3D11 vs_5_0 ps_5_0, D3D11)`. Thus this failure is work exceeding the normal guard, not an assertion failure or an indefinite hang in this run. The result does not establish that the normal CTest passes.

HTML SHA256 before/after: `9DE578C57B6D56CA9D5F692620998F9B5614CADAFDDF61EEB4278A5161FE20E2`.
The exact shared mutex name was read from gate.ps1, acquired at 03:28:27.862 UTC and released/disposed at 03:30:45.884 UTC. No owned Chrome process remained at the follow-up check. A pre-existing unrelated Inside Out browser was live, so elapsed time is diagnostic context only.

## Process evidence

Read-only Windows process snapshots, in seconds since browser creation:

| Wall age | GPU-process CPU | All renderer CPU |
| ---: | ---: | ---: |
| 19.451 | 19.344 | 0.484 |
| 39.440 | 39.375 | 0.563 |
| 65.803 | 61.672 | 7.063 |
| 86.712 | 75.281 | 18.563 |
| 132.398 | 105.078 | 42.766 |

The first observed ~40 seconds are almost one full CPU core in the GPU process while the page renderers are nearly idle. Thereafter both the GPU process and page renderer accumulate CPU. This is consistent with a substantial driver/compiler startup phase followed by repeated frame work. **It does not identify a particular shader or split compilation from WARP execution.** The unchanged file/dump runner has no streaming shader/refresh-stage timestamps. A reliable exact stage attribution needs those timestamps around first pipeline readiness and subsequent refreshes, or a driver stack trace; this run cannot supply that attribution.

## Generated shader evidence

GLSL was decoded from the frozen generated game.c into d3d-diagnostic-shader-*.frag.glsl. Counts are source complexity, not execution cost:

| Fragment | Bytes | Lines | Syntactic loops |
| --- | ---: | ---: | ---: |
| Ordinary Lit, pixel fixture | 62,314 | 1,142 | 7 |
| Ordinary Lit, skinning fixture | 55,632 | 1,053 | 6 |
| Ordinary Lit, benchmark | 55,039 | 1,034 | 6 |
| Convolve | 9,211 | 215 | 1 |
| ReadGIWitness | 28,869 | 525 | 4 |
| ReadGISharedFacing | 15,743 | 309 | 2 |
| ReadGIPhysicalVisibility | 4,545 | 99 | 0 |

Convolve still uses the original uniform-bounded ray loop. The sparse PCF/neighbor/tree loops remain uniform-bounded. No obvious accidental literal expansion is present in the generated GLSL. Splitting physical witness fragments has not removed the ordinary Lit shader's additional fixture callbacks.

The pixel fixture's material callback dispatch includes NoIndirect, **two distinct sparse-provider lambdas**, and the test-only **IndirectLegacy** lambda. Both sparse lambdas call the same production Indirect function with identical capture types. The skinning and benchmark graphs contain one sparse provider and no legacy gather. The pixel Lit shader therefore retains GatherLegacyIrradiance and another eight-neighbor loop even for `group=room`, because a URL group cannot remove a compiled callable variant. This is a concrete fixture-specific graph difference, not proof of the runtime bottleneck.

## Narrow recommendation

First trim the fixture's material callback graph: reuse one sparse callback registration and keep the legacy analytic sampler in its independent ReadGILegacyWitness fragment rather than adding its callback to every ordinary Lit variant. The dense analytic assertions read witness pixels, while room/direct/geometry material assertions require the production sparse provider. Check the dense fixture's material use before removing its callback; retain every existing oracle and the original run guards. If a separate legacy material oracle is desired, isolate its shader graph rather than combining legacy/sparse material callbacks globally.

This candidate targets the extra graph carried by the failing real-material groups while preserving production ray count, captures, reconstruction and lighting quality. It is not a demonstrated fix; first-pipeline/refresh-stage timestamps in the next normal diagnostic should verify the attribution. Do not infer that a ~12% source-size reduction guarantees a corresponding compile-time or total-time reduction.

Evidence: d3d-diagnostic-result.json, d3d-diagnostic-browser.log, d3d-diagnostic-cpu-summary.json, five raw process snapshots, d3d-diagnostic-shader-complexity.json and d3d-diagnostic-related-shaders.json.
