# Test infrastructure

## A 4-sample pass ended and begun again (2026-11-05)

`tide_platform_occlusion` failed every hardware-lane run at two pixels, (120, 54) and (121, 54), of the 4-sample target's shots 2 and 3 (walls at 6 and 4): `9033e5ff` and `9031fdff` with occlusion, `9133e6ff` and `9032fdff` on the CPU's path, which never drew the culled values in 32 more draws. Not occlusion's and not Tide's: Chrome's WebGPU on this machine's RTX 4080 SUPER (Dawn on D3D12, driver 32.0.16.1742, Chrome 156.0.8078.81) draws a box differently when the render pass it's drawn in began by loading what an earlier pass stored. Found, Claude's:

- **Nothing occlusion hid is there.** Left out of the CPU's path one at a time, only box 7 of the first patch changes those pixels (it's in front, the pane and a wall behind it); it alone with the walls and the pane draws the CPU's values. Occlusion's culled set, the pyramid and its level-0 reads of every sample (`texture_depth_multisampled_2d`, `textureNumSamples` 4 here) aren't involved: what differs is box 7's own color, by one in two bytes.
- **The frames' commands, logged in `tide.js` and their buffers read back**, differ only in their passes: with occlusion the target's pass ends after the walls and the pane (the measure, `breaks` 2), and box 7 is drawn in the next pass, which loads; the CPU's path draws all of it in one pass. The pipeline, uniforms, mesh, indices, instance data, places (the same 1,873, in the same order, without occlusion) and draw counts are the same; multi-draw-indirect, `indirect-first-instance`, `drawIndexed` in place of the counted draw and Dawn's `emulate_store_and_msaa_resolve`/`use_d3d12_render_pass` toggles change nothing.
- **Reproduced with nothing of Tide's** (`notes/repro/msaa_pass_split.html`, run headless and muted with `node notes/repro/run.mjs`): a hand-written WGSL box, the two walls, the pane and box 7 as captured, one pass against the same draws split by a `store`/`load`. Every run, one pass gives `9133e6ff 9032fdff` and the split `9033e5ff 9031fdff`, Tide's numbers exactly. Read sample by sample, the depths are the same both ways; one pass gives (120, 54) two colors (`9330e8ff` at sample 0, `9034e5ff` at the other three, two faces of the box), the split one color in all four (`9033e5ff`). A split right after the clear changes nothing; one after the walls and the pane changes those 2 pixels, one after the patches 4 others. Switching to another pipeline between the walls and the box instead of ending the pass does the same (the GPU path with occlusion off drew the scene's walls, a list made each frame, through the ordinary vertex stage before the listed one: the same two pixels, which the same box drawn after them through the same stage doesn't change). Whether a texture is bindable (`TEXTURE_BINDING`) doesn't matter. Vulkan natively and SwiftShader draw the split frame as the one-pass one.
- **The earlier reading was wrong**: A GPU whose 4-sample frames differ from themselves (below) took these two pixels for that noise, the CPU's path having given both values at shot 2; here the CPU's path gave `9133e6ff` at both shots in every draw (32 more a shot in each failing run, and every diagnostic redraw), and the culled frame `9033e5ff` in every one. Whether that noise (the same frame differing from one draw to the next, with other GPU work going on) is this, triggered by something else ending the hardware's work mid-pass, isn't known.
- **What the test does now** (`occlusion.c`, `Shot.split` in `occlusion.tide`): where a shot's culled frame differs from the CPU's path and its measures ended passes (`report.breaks`), the CPU's path is drawn again with the destination's pass ended at the same places (something drawn into another target before each run testing occlusion), and the culled frame must be that, byte for byte; then it's a `note:`. Where a pass ending changes nothing (Vulkan, SwiftShader), that frame is the one already compared, so nothing is loosened; on this GPU the comparison is exact rather than "explained" (`explained_by_cpu` stays for the noise below, and wasn't needed in the runs here). A culling mistake still fails: the split frame draws everything.
- **Not done**: no Chromium or NVIDIA bug filed; other GPUs and Chrome on Vulkan or Metal not tried; which of the two is right by WebGPU's rules (centre interpolation per face) not worked out.

## The hardware web lane on Direct3D 11 (2026-11-05)

Chrome's WebGPU on Dawn's D3D11 adapter (some Windows machines get it instead of D3D12) compiles shaders through FXC,
which refused four of the renderer's and tidec's kernels. The lane that finds that, Claude's, Provisional:

- **Running it:** `ctest --preset web-hardware-d3d11` after a `web-debug` build: the `pixels` tests with
  `TIDE_WEB_HARDWARE=d3d11`, which is the hardware lane's flags plus `--use-webgpu-adapter=d3d11`
  (`cmake/web_gpu.mjs`). The adapter's probe now also reports `backend` (Chrome gives it with
  `--enable-webgpu-developer-features`, which the lane passes: `D3D11`, `D3D12`), printed after the description
  (`WebGPU adapter: nvidia, lovelace, 0x2702, NVIDIA GeForce RTX 4080 SUPER (D3D11)`), and a page whose backend isn't
  D3D11 fails at once. Run it beside the D3D12 lane for anything reaching WebGPU's shaders (tidec's WGSL, the
  renderer's kernels, `tide.js`'s pipelines).
- **Before the fixes**, 27 of 35 passed: `tide_platform_draw`, `_draw_rgba_fallback`, `_prewarm` and `_web_stats`
  stopped at the prewarmed pyramid kernel's FXC error, `_cull` and `_occlusion` at CullListed's and the pyramid's,
  `_kernels` at its `Prefix` kernel's, `_compute` at its hand-written shared-memory kernel's. Every render pipeline
  compiled. FXC's own text only shows with `--enable-dawn-features=dump_shaders` (else Dawn says `FXC compile failed
  with error: E_FAIL.` alone); the errors and their fixes are in notes/shaders.md, Kernels on Dawn's Direct3D 11.
- **After:** 36 of 36 (the new `tide_platform_cull_fallback` too) in 107 seconds, Chrome 156.0.8078.81, RTX 4080
  SUPER, driver 32.0.16.1742. There the GPU culls every shot of `tide_platform_cull` and `_occlusion` (occlusion hides
  1,450 boxes in the steady frame) with the CPU path's pixels byte for byte, before and after a lost device, and no
  4-sample shot of `_occlusion` differs (the noise below is D3D12's).
- **A sweep of every shader** (not committed; scratch scripts): every WGSL module the goldens
  (`compiler/tests/shaders/*.txt`), the translator fixtures (`compiler/tests/wgsl`), the renderer (`TIDE_RENDERER_WGSL`)
  and `tidec --shaders` of `platform/src/cull.tide`+`occlusion.tide` and each `platform/tests/*.tide` hold, 382
  (38 kernels), made into pipelines in one headless page (`layout: 'auto'`, vertex buffers and targets from the entry
  points' signatures) inside error scopes. Before: 375 on D3D11, the 7 failing all kernels; SwiftShader 382. After:
  381 on D3D11 and 382 on SwiftShader; the one left is `compiler/tests/wgsl/atomics.wgsl`, a translator fixture of bare
  workgroup atomics (`&name`, `&name[i]`) that no WebGPU page runs, kept as it is to test those forms.
- **The D3D12 lane's `tide_platform_occlusion` fails today**, 2 pixels of shots 2 and 3 of the 4-sample target (the
  noise of "A GPU whose 4-sample frames differ" below, at the same place): its probe drew the CPU path 17 times and saw
  no difference, so the shots were checked and failed. Not this work's: the parent commit 63c718e5c, built alone in a
  scratch worktree, fails the same 6 runs of 6, and so does this branch with the old pyramid kernel, with optional
  pipelines made at once and without the new error scopes. Left as it is (loosening it is the owner's call); the D3D11
  lane draws those shots the same as the CPU path.
- **Validation** (Windows, Clang 23, Node 24.22.1, Chrome 156.0.8078.81, `TIDE_HIDDEN`, `-DTIDE_EXTERNAL_DIRS=`):
  `web-hardware-d3d11` 36/36 (100 s); `web-hardware` (D3D12) 35/36, `tide_platform_occlusion` failing as above; the
  whole `web-debug` suite on SwiftShader 3,000 of 3,001 (636 s), the one `tide_web_runtime`, whose two layout sandboxes
  lacked `READ_SAMPLES` since the occlusion commit before this (f8960a08c added it to `layoutFor`): given it, it passes.
  Native `sanitize`: the 64 `gpu` tests on Vulkan with the validation layer, and 345 shader tests (goldens, spirv-val,
  every `tidec_hlsl_*`/`wgsl_hlsl_*` through fxc, `tide_spirv_shaders_valid`, `tide_hlsl_shaders_valid`,
  `tidec_e2e_compute`, the GPU/kernel error tests).
- **Not run:** another GPU vendor, Chrome on Vulkan, a machine whose Chrome picks D3D11 by itself (the flag forces it
  here); FXC on a driver other than this machine's `d3dcompiler_47.dll`. Whether the D3D11 lane joins `AGENTS.md`'s
  required validation for rendering changes is the owner's call.

## The hardware web lane (2026-11-05)

Every web test draws on Chrome's software WebGPU (SwiftShader), so bugs that only show on a real GPU's WebGPU went unseen. The lane runs the web tests on this machine's GPU through Chrome instead, headless and muted as always. Claude's arrangement, Provisional:

- **Running it:** `ctest --preset web-hardware` after a `web-debug` build: the `pixels` tests with `TIDE_WEB_HARDWARE=1`. The variable works for any web test (`TIDE_WEB_HARDWARE=1 ctest --preset web-debug -R <name>`). It's optional: the owner (2026-11-05) has local validation focus on native tests (the `gpu` tests on Vulkan); run it, or `web-hardware-d3d11`, when a WebGPU-only change or a real-GPU report calls for it. On this machine (RTX 4080 SUPER, Chrome 156, D3D12) all 35 pass in about 94 seconds.
- **Not CI's (the owner, 2026-11-05):** "im not always going to have my computer on and i dont want it stressing it out either so for now i guess CI wont test real GPUs". CI tests WebGPU on software adapters alone (SwiftShader on Windows and macOS, `gpu=none` on Linux), and real-GPU WebGPU coverage is this lane, run locally. No self-hosted or GPU job exists (`notes/releases-and-ci.md`).
- **`cmake/web_gpu.mjs`** is the one place the runners get their GPU flags (`gpuFlags()`): `run_web_served_test.mjs`, `platform/tests/web_rooms.mjs`, `web_threads.mjs` and `docs/tests/examples.mjs` (`bench/lag.mjs` keeps its own). Software, as before: `--enable-unsafe-webgpu --use-webgpu-adapter=swiftshader --disable-gpu-watchdog`, `--use-angle=d3d11-warp` on Windows and SwiftShader's Vulkan on Linux with `TIDE_WEB_LINUX_VULKAN=1`. `web_rooms.mjs` and `web_threads.mjs` gained `--disable-gpu-watchdog` with this, and every runner now honors `TIDE_WEBGPU_FLAGS`, which still replaces the flags in either mode. Hardware: `--enable-unsafe-webgpu --enable-webgpu-developer-features` (the adapter's full description) `--ignore-gpu-blocklist --use-webgpu-power-preference=default-high-performance --disable-gpu-watchdog`, and no ANGLE choice: the browser's own drawing goes as Chrome picks.
- **Which adapter it was** is asked before every test page: the browser goes to `/tide-adapter?next=<the test's address>` first, a page of the runner's (`probeRoute`) that calls `navigator.gpu.requestAdapter()` as `tide.js` does, posts its `info` (vendor, architecture, device, description, `isFallbackAdapter`) back and replaces itself with the test's page. `examples.mjs`, which drives its pages over DevTools, evaluates the same expression (`adapterExpression`) in its first page. The served runner keeps the line in a failure's account in both modes. In the hardware lane it's printed (`WebGPU adapter: nvidia, lovelace, 0x2702, NVIDIA GeForce RTX 4080 SUPER`), and the test fails at once when the adapter is a fallback one, names SwiftShader, llvmpipe, lavapipe, WARP or Microsoft's Basic Render Driver, or there's none, and fails at its end if no adapter was ever reported. Checked: `TIDE_WEBGPU_FLAGS` set to the software flags with `TIDE_WEB_HARDWARE=1` fails `tide_platform_draw` with "the browser gave its fallback adapter".
- **Found, run the first time** (`web-debug`, all 35 `pixels` tests): 34 passed as they were. `tide_platform_occlusion` failed at four 4-sample shots, 2 to 17 pixels a shot: the GPU's, below. `--use-webgpu-adapter=d3d11` (Dawn's D3D11) was no lane: there FXC failed to compile the renderer's `CullListed` (`E_FAIL`), and its one-sample shots differed from the CPU path by hundreds of pixels (the culled runs' draws went unwritten). Since, fixed and a lane: The hardware web lane on Direct3D 11, above. `force-low-power` gets no adapter here (the AMD iGPU isn't offered).
- **The rest of the web suite** works in the lane too: `tide_platform_web_rooms*` and `_web_threads` pass with `TIDE_WEB_HARDWARE=1`, and so does the docs examples test (`TIDE_EXAMPLES=assets,audio,commands node tests/examples.mjs` after a `-DTIDE_DOC_EXAMPLES=ON` `docs_examples` build and `npm run build`). Both modes print the adapter (`google, swiftshader (a fallback adapter)` in software).

## A GPU whose 4-sample frames differ from themselves (2026-11-05)

On Chrome's WebGPU on this machine's RTX 4080 SUPER (Dawn on D3D12, driver 32.0.16.1742), a 4-sample target drawn twice with the same commands and the same bytes doesn't come out the same: a few pixels at triangles' edges differ, by 1 in a byte or a sample's share of a color, in most frames of `tide_platform_occlusion`'s view from inside the field. It's Chrome's or the driver's, not Tide's:

- **The CPU path differs from itself.** Drawn 8 more times right after each shot (no GPU culling, no occlusion), the 4-sample frame differed from the first at 2 to 34 pixels in nearly every draw at camera 2, now and then at 2 pixels at camera 0. A hash of the program's commands (steps, pipelines, instance offsets and counts) and of its instance bytes was the same every frame, and a hash of the resolved target's own bytes, copied from it on the GPU, was different nearly every frame. One-sample targets and the window never differed. Native Vulkan on the same GPU, and SwiftShader, gave 0 differing pixels in every redraw.
- **So it isn't listed against ordinary vertex stages**, which was the guess: one ordinary instanced draw of all 1,720 boxes, its bytes copied, varies the same way.
- **Outside Tide.** The 4-sample pass was captured from the page (its buffers' bytes, its two pipelines' WGSL and descriptors, its commands) and replayed by a page of its own with nothing of Tide's. Replayed alone, 16 to 48 times, it was the same every time. It began to vary once each frame also read through a 2D canvas, as `gpu_target_read` does (a WebGPU `OffscreenCanvas`, `drawImage`, `getImageData`): over 200 to 300 frames, 2 to 10 different results, up to 8 pixels, up to 27 in a byte. After that, the pass alone varied too. Changing what was replayed showed where: with depth testing off and a frag that writes only its flat (per-instance) tint, 300 frames were the same. With depth off and the interpolated corner color, they varied by 1 in a byte. With depth on and the flat frag, a sample's whole color varied. One sample never varied. `@invariant` on the position, `depth32float`, `less` for `less-equal`, a clear by `loadOp` rather than the clear triangle, and centroid or per-sample interpolation all still varied. Dawn's `emulate_store_and_msaa_resolve` and `use_d3d12_render_pass` (on and off) changed nothing. So the same vertex stage, on the same bytes, gives positions or varyings that aren't the same from one run to the next, at 4 samples only, depending on what else Chrome's GPU process is doing. That's below WebGPU. A hand-written page with its own cubes didn't reproduce it, so it also takes something of this scene: boxes crossing each other and the camera.
- **What the test does now:** where a 4-sample shot differs from the CPU path, `occlusion.c` asks once whether the device draws the same 4-sample frame twice (`reproduces_samples`: the CPU path at camera 2, drawn 17 times). On Vulkan and SwiftShader it does, so the shot fails as it always did: nothing is loosened there. On this GPU it doesn't, so the shot is printed as a `note:` ("not checked: this device draws the same 4-sample frame differently from one time to the next") and isn't counted. Everything else in the test is checked there as everywhere: the window's and the one-sample target's shots byte for byte, the probes, the counts.
- **Its two pixels at (120, 54) and (121, 54)** of shots 2 and 3 seemed to be occlusion's: with occlusion, 9033e5ff all 10 times, and on the CPU path and without occlusion, 9133e6ff all 10 times at shot 3. They're the same noise: at shot 2 the CPU path alone gives both values (2 of 10 draws 9133e6ff). The pass that occlusion's measure splits lands on one of the two each time. (Since: not the noise, but the pass the measure ends: A 4-sample pass ended and begun again, above.)
- **Validation** (Windows, Clang 23, Node 24.22.1, Chrome 156.0.8078.81, `TIDE_HIDDEN`): `ctest --preset web-hardware` passes 35/35 (94 s), `tide_platform_occlusion` there with its 4-sample shots noted, 6 to 7 of them a run. The whole `web-debug` suite on SwiftShader passes 3,000/3,000 (338 s). Every served page now goes through the probe first. `tide_platform_occlusion` passes natively (`sanitize`, Vulkan with validation), where no shot differs, so the device is never asked.
- **Not done:** no Chromium or NVIDIA bug filed; the replay page isn't kept (made from a capture in a scratch folder). A GPU of another vendor, and Chrome on Vulkan or Metal, weren't tried.

## GL backends removed: every web test on WebGPU (2026-11-05)

With OpenGL, OpenGL ES and WebGL gone (`notes/rendering.md`, GL backends removed), the tests' arrangement is Claude's, Provisional:

- **`tide_add_web_test(<name> TARGET <target> [QUERY <query>] [PIXELS] [EXPECT <text>] [SECONDS <n>])`** (`cmake/WebTest.cmake`). Every web test is served to headless Chrome or Edge in real time (`cmake/run_web_served_test.mjs`), cross-origin isolated, on the browser's software WebGPU: `--enable-unsafe-webgpu --use-webgpu-adapter=swiftshader`, and the browser's own drawing where it can show WebGPU's images: on Windows `--use-angle=d3d11-warp` (with SwiftShader's GL, Chrome there has no WebGPU at all), and on Linux `--use-angle=vulkan --use-vulkan=swiftshader --enable-features=Vulkan` (`TIDE_WEBGPU_FLAGS` still replaces them). `SECONDS` is the result budget, 120 when not given, and CTest's guard is `SECONDS + 50` for the runner's diagnostics; each page still reserves the host's CPUs (`PROCESSORS`). Unknown arguments are a configure error.
- **Gone:** the WebGL file-page runner (`cmake/run_web_test.cmake`, `cmake/browser_dump.mjs`) and virtual time; the `WEBGPU`, `SERVED`, `D3D11` and `D3D11_SERVED` options; the runner's `--angle-backend`; the test shell's `expect-angle=` check; the `_webgpu` and `_d3d11` test names (the WebGPU run has the base name now) and the `webgpu` and `webgl_d3d11` labels.
- **`PIXELS`** labels a test `pixels`: it reads WebGPU's pixels back. Every test that was `WEBGPU` before is `PIXELS` (the drawing fixtures: draw, targets, shaders, kernels, compute, cull (the GPU culling's, merged in after as `PIXELS`), prewarm, skinned, baked, fills, float bits, camera precision, texture capacity, GPU functions, gpu_time, stats, the demo's smoke test, the Render2D/Render3D/particles pixel tests, bench_skinned/render3d/forest), whether or not each reads pixels itself. Windows' and macOS's CI runners run every web test; Linux's leave these out (`ctest -LE pixels`), below.
- **Linux reads WebGPU's pixels** with SwiftShader's Vulkan under the browser's compositor. With its default GL compositor (`--use-angle=swiftshader`, or nothing), Chrome on a machine without a GPU draws with WebGPU but its canvas never shows it (`SharedImageStub: Unable to create shared image`, a WebGPU swap-buffer factory it can't find), so a page reads zeros: that's why CI's Linux web shards ran `-LE webgpu` before. Checked on Ubuntu under WSL, Chrome 155.0.8059.39, Node 24.15.0, through the real runner against this branch's web-debug pages: with the old flags `tide_platform_draw` read zeros (`FAIL: a mesh's left corners' color (0x00000000, ...)`) while `tide_platform_web_keys` passed; with `--use-angle=vulkan --use-vulkan=swiftshader --enable-features=Vulkan` `tide_platform_draw` (3,359 `ok`s), `_targets`, `_kernels`, `_compute`, `_skinned`, `_web_keys` and `_host_wait` all passed. `--enable-features=Vulkan,VulkanFromANGLE,DefaultANGLEVulkan --use-angle=swiftshader` gave no result at all, and `--disable-vulkan-surface` read zeros again. On GitHub's hosted Ubuntu runners (Build 41974964375, 2026-11-05) those flags gave no WebGPU adapter at all ("the browser gave no GPU adapter"), so every page there failed to start, pixels or not. The Vulkan flags are now asked for with `TIDE_WEB_LINUX_VULKAN=1` only (for WSL), and CI's Linux shards run `-LE pixels` with `TIDE_WEB_GPU_NONE=1`: each page gets `gpu=none` (served runner, `web_rooms.mjs`, `web_threads.mjs`) and draws through `tide_gpu_none` (`platform/src/gpu_none.c`), a GPU that takes every call and draws nothing, reads back zeros, and is what `Tide.gpuNone` picks in `canvas.c`. No game page uses it. Tests that read no pixels (keys, touch, cursor, embedded, debug views, first tick, quit, lag, host wait, rooms, threads, io, fetch, reload, the lobby relay) run everywhere, on WebGPU now where they ran on WebGL.
- **Custom runners** (`platform/tests/web_threads.mjs`, `web_rooms.mjs`, `docs/tests/examples.mjs`, `bench/lag.mjs --gpu software`) launch with the same software WebGPU flags. The served tests that passed `backend=webgl` (`tidec_e2e_fetch_web`, `tide_platform_web_io`, `tide_platform_reload_revision`, `tide_lobby_relay_web`) pass no backend. The docs examples test draws on WebGPU only (`TIDE_EXAMPLE_BACKEND` is gone).
- **Native:** `tide_gpu_test(<tests>...)` labels `gpu` and gives the Vulkan environment; its `OPENGL` option, every `*_opengl` twin, `tide_platform_draw_opengl_legacy`, `versus_draw_opengl` and `tide_platform_gpu_timing` (the GL timer's mock) are gone, and so is `TIDE_BACKEND=vulkan` on the Vulkan-only tests (there's nothing else). CI still runs no `gpu` test; Vulkan's coverage is a machine with a GPU's.
- **Android:** the package job's emulator stays `-gpu swiftshader_indirect`, whose Vulkan is 1.2: the Google Play check's app shows the screen saying it needs Vulkan 1.3 (it posts a frame, so `am start -W` reports ok and the activity stays resumed). Since 2026-11-05 the Android job's boots with `-gpu lavapipe` (the emulator's own Mesa lavapipe, Vulkan 1.3 in the device), and the renderer's apps (`tide_platform_draw_app`, `_targets_app`, `_shaders_app`) are CTests labelled `gpu`, run through `run_android.mjs --app`, which also fails any app whose process crashed, as it ended too (`tide_platform_app_exit`, any device). Locally: boot the emulator with `-gpu lavapipe` or `-gpu host`, or leave out `gpu` (`notes/android-and-mingw.md`, An app's end).
- **A lost device's first draws (2026-11-05):** since a028cf057 (drawing.md, Turning round) a mesh whose copy the list gave up while the renderer had it isn't drawn the first time after the device goes, and is copied again for the next. `tide_platform_shaders`' WebGPU pass after losing its device read those first draws black (Build 41989911905); it now runs every check once on the new device without counting it (`run_again`, its rolls with another seed), then again.
- **Real time instead of virtual time:** `tide_platform_web_lag` relied on the file pages' virtual time. In real time on software WebGPU its steps that hold packets through two or three page timers measured up to 20 ms over their arithmetic (a 200 lag untold: 250 where 230 is due; a 50 lag on an 80 ms network: 95 where 80 is), steady from run to run, where the step with no timer measured 30.8 for 30. Its tolerances went from 4 ms to 24 for pings and the least lag and 12 for what's held; its steps are 20 ms or more apart. Weaker than before; tide.js's lag timers weren't changed. CI's first run (Windows and macOS web) measured 259.6 where 230 was due, so the bounds are one-sided now: a ping may be up to 48 ms late or 12 early (`LATE`, `EARLY`), and what's held 36 late or 12 early, as real-time timers and frames only ever deliver late. Every other web test passed in real time unchanged. 2026-11-11: the step that first tells the page its ping settles for 5 seconds, not 2. The page takes the first ping it's told whole for the network's time, and a late one (CI's macOS runner, Chrome 157) left the pings short for longer than the step waited: 158 where 200 was due, as it wears off an eighth a counted ping (`room_ping`). tide.js wasn't changed.
- **Validation** (Windows, Clang 23, Node 24.22.1, Chrome 156, `TIDE_HIDDEN`, external prototypes left out with `-DTIDE_EXTERNAL_DIRS=`), after merging the tidec half: the `sanitize` build is warning-free and passes all 3,125 tests, the 60 `gpu` ones on Vulkan under the validation layer (SDK 1.4.363); `web-debug` builds warning-free and passes all 2,957, every browser page on software WebGPU; the docs site builds. The Linux runs under WSL are above. Android: `notes/android-and-mingw.md`. `release` builds warning-free in full, and `mingw-release` the platform layer, the demo, `tide_platform_draw` and `versus_draw`. Not run here: macOS, a hosted Linux runner, MinGW's tests, the Docs workflow's examples test.
- Records below that name WebGL, D3D11, OpenGL or `_webgpu` tests are history.

## D3D11 served runs take the test's SECONDS (2026-11-04)

Run 41862233666 failed tide_platform_shaders_d3d11 on CI's Windows runner:
the file runner's fixed 120 seconds ran out after the page's first five
checks, at its first pixel readback on WARP. The same test had passed in
64, 80 and 89 seconds in the runs before, while its WebGL and WebGPU runs
already had `SECONDS 240`, which a file-page D3D11 run can't take. It is now
`D3D11_SERVED`, and a `D3D11_SERVED` run's budget is the test's `SECONDS`
(120 when not given) plus 50 for the guard, so the shader test's D3D11 run
has 240/290. The forest's D3D11 variants, which give `SECONDS 240`, get
240/290 too instead of 120/170. Locally (web-debug, Chrome 156) the served
shader D3D11 run passes in 40 seconds.

`tide_platform_rooms` runs alone (`RUN_SERIAL`): see `notes/webrtc-and-rooms.md`
(2026-11-04).

## All forest browser variants await completion (2026-11-03)

Run 41556106089 repeats the premature DOM completion in ordinary WebGL:
bench_forest returns a complete 3.36 MB DOM in 1.69 seconds with only the
startup line and no exit marker. This is the same asynchronous forest frame
loop as the quick D3D11 case below. Both quick and motion forest queries now
use the served result barrier on every browser backend, instead of relying on
a virtual-time DOM snapshot for any variant.

Only three additional registrations change: bench_forest, bench_forest_motion
and bench_forest_motion_d3d11. Ordinary WebGL now shares the already-declared
240-second software-render budget with WebGPU, with a 290-second outer guard.
Both D3D11 variants use a nominal 120-second result budget and 170-second
outer guard, WARP selection and the Direct3D11 renderer assertion. The six
variants keep their existing program arguments, scene/check work, CPU
reservation and result validation. No helper, runtime or game code changes,
and no retries or other test migrations are introduced.

The web-debug forest target builds, and all six browser cases pass locally:
quick WebGL/D3D11/WebGPU in 3.53/5.42/4.51 seconds, and motion in
5.62/8.69/7.89 seconds. Rendered CTest metadata confirms exactly the three
registrations listed above changed among 2,875 tests. Validation uses Node
24.22.1 and muted Chrome 156.0.8078.79; the whitespace diff passes. Hosted
Chrome Headless Shell confirmation remains the next normal push's coverage.

## Forest D3D11 waits for its result (2026-11-03)

Run 41554207974's bench_forest_d3d11 failure has decisive new diagnostics:
after 1.64 seconds Chrome returned a complete 3.36 MB DOM with the forest's
startup line, but no data-exit marker. The program had started, but the file
runner treated dump-dom completion as the end of the test and closed Chrome.
The forest runs its work over hidden-window timer callbacks (one warmup frame,
two measured frames and a completion drain), so a DOM snapshot is not its
completion signal. This does not establish why Chrome's virtual-time dump
finished early, nor prove the older empty-output failure had the same cause.

Initially only bench_forest_d3d11 opted into D3D11_SERVED. The served runner
accepts --angle-backend for WebGL, restricted to swiftshader or d3d11-warp;
its original default backend flags are unchanged. This fixture selects WARP
and retains expect-angle=Direct3D11, all forest checks and the nominal 120-second
result budget. It waits for the actual page result and closes Chrome through
DevTools. The outer CTest guard is 170 seconds for the existing diagnostic/
cleanup allowance. This initial fix left motion D3D11 and other file
registrations unchanged; the follow-up above covers all forest variants.
No retry is added. Like other served tests, this page uses real
time and cross-origin isolation/shared WASM memory. No forest or runtime code
changes.

Local validation: quick D3D11 passes in 5.39 seconds and the unchanged motion
D3D11 test in 10.74 seconds. Controlled real-browser cases wait for a delayed
success, reject exit 7, reject a mismatched renderer expectation, and reject a
complete page that never supplies a result. The default SwiftShader case still
passes. Invalid, missing and WebGPU-only angle-backend combinations fail before
launch. Rendered CTest metadata confirms only bench_forest_d3d11 changed among
2,875 registrations. Tests use Node 24.22.1 and muted Chrome 156.0.8078.79;
syntax and whitespace checks pass. Hosted Chrome Headless Shell confirmation
remains the next normal push's coverage.

## Batched served-test output (2026-11-03)

Run 41550970088's macOS shader diagnostics establish a result-delivery failure:
the page records successful program exit at 118598 ms, but the first HTTP log
arrives at runner time 120.455 seconds, and the log queue is still draining at
123.670 seconds when the runner gives up. test_shell.html had queued one serial
POST per printed line during the synchronous shader callback. The result waited
behind all of those request/response round trips, although the assertions passed.

The test shell now has one asynchronous sender. It coalesces queued lines into
a newline-separated log body, consumes its response completely, then sends any
lines accumulated while that request was in flight. Queued results follow the
logs. Console/DOM output and ordering remain unchanged; the served runner's
existing substring check for an expected line also works within a batch. A
failed fetch or response-body read still preserves later output and changes
the result to the recorded send failure. File pages make no requests. Browser
flags, shader work/assertions and runtime code are unchanged.

tide_web_test_shell runs the actual shell script under Node with controlled
fetch responses. Its seven checks cover a 5000-line burst in one log request,
exact text/order (including Unicode, embedded newlines and HTML-like text),
arrivals during response-body backpressure before the pending result, fetch
and response-body failures, abort, file-page output, and a result without any
printed lines. The previous shell fails the burst/coalescing checks.

The successful shader page completed at 118.598 seconds on its own clock,
close to the nominal 120-second WebGL budget; another successful hosted run
took 114.26 seconds. This is a correctness fixture on software rendering, not a performance
threshold. SERVED now honors an explicit SECONDS while retaining 120 by default,
so only the shader WebGL test gains its already-declared 240-second software
budget, matching its WebGPU sibling, with a 290-second outer guard. Other
registrations keep their budgets. The batching fixes the observed delivery
backlog; the budget change separately allows slower hosted execution. Neither
changes shader work, assertions or the shared runner's diagnostic-grace policy.

Local validation: all seven Node regressions pass, and the previous shell fails
the burst/coalescing cases. A real served page prints 5000 lines in exact order,
matches EXPECT against the final line within the batch, and delivers success
in 0.964 seconds. The shader WebGL, D3D11/file and WebGPU CTests pass in 27.38,
42.99 and 24.76 seconds, respectively; these runs still used the stricter initial
WebGL budget. Rendered CTest metadata confirms the later budget change affects
only tide_platform_shaders. An isolated registration check confirms served
defaults of 120/170 seconds and explicit 240/290 (result/outer guard), while
WebGPU's unspecified 100/150 and D3D11's 150-second guard remain unchanged.
Validation uses Node 24.22.1 and muted Chrome 156.0.8078.79. The whitespace diff
passes. Hosted macOS confirmation remains the next normal push's coverage.

## File-browser early-exit diagnostics (2026-11-03)

Run 41547870181's bench_forest_d3d11 failed in 0.88 seconds with no page
result and empty browser stderr. The longer motion case of the same executable
passed immediately afterward, and the unchanged quick case passed in 9.98
seconds in run 41539750446. These logs identify neither a forest assertion
failure nor a timeout; the browser's exit status and raw DOM were not reported.

browser_dump.mjs now reports the child's exit code/signal and output length
when its streams close without a complete DOM. Intentional termination after
the closing HTML tag produces no such diagnostic. run_web_test.cmake's existing
missing/nonzero page-result failure now includes the runner result, DOM length
and its first 4096 bytes, marking truncation. Launch flags, deadlines, result
checks and exit statuses are unchanged. This improves evidence on a recurrence;
it does not claim to fix the unidentified hosted failure.

Controlled scratch-browser cases pass for empty output with exit 0 and 7,
partial HTML with exit 9, a complete failing page result, a large result-less
DOM with a bounded excerpt, and a successful complete DOM whose browser is
terminated by the wrapper. A self-terminating Node child also reports the
platform-provided status (Windows reports exit 1, signal none). Ordinary
bench_forest_d3d11 passes in 8.20 seconds with the existing web-debug page,
Node 24.22.1 and Chrome 156.0.8078.79. The whitespace diff passes; no runtime,
forest code, permanent fixture, retry or workflow dispatch was added.

## Shader WebGL runner diagnostics (2026-11-03)

The macOS WebGL shader fixture reached the file runner's 120-second guard in
runs 41527109289 and 41539750446, without a dumped DOM or any of its assertions.
The intervening run 41535944061 passed at 120.03 seconds. That duration alone
does not prove expensive shader work: the old runner accepts a successful DOM
even when the browser subsequently reaches the cleanup timeout. Its dump wrapper
only signals the main Chrome process after receiving the DOM. The logs cannot
distinguish a busy shader callback from delayed DOM output or browser shutdown.

Initially only tide_platform_shaders used tide_add_web_test's SERVED WebGL path.
Its initial routing reused run_web_served_test.mjs with the same SwiftShader
backend and a nominal 120-second result budget, plus 50 seconds for diagnostics/
cleanup. The batching follow-up above gives it the explicit 240-second budget.
The existing runner records page/crash/network diagnostics and closes Chrome
through Browser.close. Shader work and assertions are unchanged. D3D11 remains
a file page, and WebGPU retains its nominal 240-second result budget. The shared
runner can accept a result arriving during timeout diagnostics; this change
retains that existing grace instead of adding a different deadline policy.
No other fixture opts into SERVED.

Serving also enables cross-origin isolation/shared WASM memory and uses real
time instead of virtual time. The fixture's tick/frame calls are explicit, so
its shader checks remain the same, but this is a different browser scheduling
path. Its synchronous callback can delay the shell's HTTP log posts until it
yields; timeout diagnostics can identify an unresponsive page, but do not
guarantee a per-shader trace. Hosted macOS execution must establish whether the
failure is resolved; local success alone cannot establish its original cause.

Local validation with Node 24.22.1 and Chrome 156.0.8078.79: the web-debug
shader target builds, and all three shader CTests pass (served WebGL 29.91 s,
file-page D3D11 46.01 s, WebGPU 27.80 s). A scratch page returning failure 7
is rejected with exit 1 and a timestamped account. Comparing CTest JSON before
and after confirms exactly one registration changed among 2,874 tests; the
other 2,873 commands/properties, including D3D11 and WebGPU, are identical.
No native/runtime/compiler code changed; no permanent harness fixture or
workflow dispatch was added. The whitespace diff passes.

## Cache cleanup tests (2026-10-30)

.github/cache-cleanup.test.mjs checks bounded compiler-cache retention with fake
paginated GitHub responses, including branch/family isolation, preservation of
newest snapshots and the retry fallback, unsafe metadata and concurrent deletion.
It runs alongside release-note fixtures in Version's existing npm test step.
All eight tests pass on Node 24.22.1; the exact hosted-action script also imports
and executes on Windows. This changes cache maintenance only; native/web tests
from the preceding workflow change remain valid. Hosted execution follows the
normal push trigger, with no additional dispatched run.

## Web CI shards and sanitizer scheduling (2026-10-30)

Build's web-release entries select every third CTest index on each of three
independent runners. They keep the existing per-browser whole-CPU reservation,
muted audio, assertions and test deadlines. CTest JSON confirms their union is
exactly the original suite, with no duplicates, including when -LE webgpu applies
on Linux. There are no cross-test fixtures or DEPENDS properties requiring tests
to share a runner. Each shard's failure reaches the existing required Build gates.

The sanitizer preset now restores/saves CTest duration history as release and
MinGW do, starting expensive cases first, and has a 2 GB compiler cache instead
of 500 MB. Compiler objects are also saved after failed tests (but not cancelled
jobs), preserving successful work for the next fix. Hosted measurements and the
publication/cancellation contracts are in releases-and-ci.md, Faster feedback for overlapping dev builds.

Local validation: Windows Clang 23.1.2/Node 24.22.1 web-release builds and all
three shards pass: 931, 930 and 929 enabled tests, with the existing disabled
right-turn case retained in the third shard. The 2,790 enabled tests include
WebGL, WebGPU and Windows D3D11 coverage. The targeted native Windows ASan
`tidec_e2e_tag_fallback` build/test passes. Native Mac compiler selection and
hosted scheduling/cache performance cannot be exercised on this Windows host.

## Routine native sanitizers (2026-10-29)

The owner approved routine sanitizer testing after ASan identified a Render3D
pixel fixture's use-after-free (notes/render3d.md). The existing `sanitize`
configure/build/test/workflow preset now supports Windows with ASan; Linux and
macOS retain ASan + UBSan. It instruments the engine, platform, generated C,
test hosts and native build tools together. Use it for affected native
correctness tests, including local Vulkan/OpenGL tests, and keep normal release
and web coverage. Performance measurements use an ordinary optimized build.

`cmake/Sanitizers.cmake` queries the selected clang for its target-specific ASan
DLL and copies it beside the build's executables. Missing runtimes fail at
configure with installation guidance. CTest also supplies the runtime directory
on PATH and the matching llvm-symbolizer, inherited by tests that copy tools to
scratch installations; this works with both `ctest --preset sanitize` and
`ctest --test-dir build/sanitize`. The two editor installer fixtures that clear
PATH copy the DLL alongside their scratch CLI instead. Sanitizer builds keep
their language server in their own bin directory, without publishing it to the
editor's shared build/tools directory. Cross builds/MinGW are not supported by
this native sanitizer configuration.

Windows ASan links disable identical/tail data folding (`/OPT:NOICF`), keeping
reference elimination. lld-link otherwise enables folding with `/OPT:REF`:
different-sized narrow/wide literals can share ASan-padded storage, and empty
strings can land in poisoned padding ([LLVM issue 62078](https://github.com/llvm/llvm-project/issues/62078)).
The first full run reproduced both failures; the corrected links pass the
same platform fixtures without suppressing checks. Sanitizer executables also
reserve an 8 MiB main-thread stack: the instrumented rewind C fixture alone
uses a 0x10a5f8-byte frame, exceeding Windows' ordinary 1 MiB reserve. Ordinary
builds keep their stack limits. The native debugger fixture allows ASan's
handled first-chance shadow-memory faults; unhandled exceptions and sanitizer
reports still fail it (notes/debugging.md).

`tide_sanitizer_detects_use_after_free` deliberately reads freed memory and
requires a failing exit with ASan's specific diagnostic. It also proves that
the expected-compiler-error harness rejects a sanitizer finding emitted after
the expected diagnostic. Ordinary successful tests alone cannot prove that
instrumentation is enabled. Compiler diagnostic tests now explicitly reject
ASan/UBSan reports, as the existing safety harness already did. CTest assigns
sanitizer exit code 86, so those failures cannot satisfy wrappers expecting
ordinary error exits 1 or 2. ASan and UBSan share that option in the runtime.

The preset retains `detect_leaks=0`: tools/tests may keep allocations until
process exit. ASan checks exercised CPU memory accesses, including those in
renderer hosts; Vulkan validation and shader/browser tests remain necessary.
Neither this preset nor a passing suite proves every memory error absent.
Linux sanitizer CI remains enabled and excludes native GPU tests because its
runners have no GPU; local Windows validation closes that coverage gap.

Validation: with Clang 23.1.2 and Node 24.22.1, the full Windows sanitizer
build passes all 2,892 enabled CTest tests, including 60 native GPU checks,
29 safety checks, the ASan detection/diagnostic-wrapper probe, and the actual
CLI scratch-installation and language-server subprocesses. No manual PATH
setup is needed. The ordinary debug natvis test also passes. The full web-debug
build and all 2,783 enabled tests pass, including WebGL, ten Windows ANGLE
D3D11 checks and 22 WebGPU checks. Both suites retain the same pre-existing
disabled MicroDuck right-turn test; no new exclusion or suppression was added.
Agent runs use TIDE_HIDDEN and the existing muted browser launchers. Linux,
macOS, MinGW, Android and installed-package C++ validation remain CI coverage;
no workflow was manually dispatched.

## Debugger tests (2026-10-26)

`tidec_debugger_natvis` drives Windows' own `dbgeng.dll` (no install) on
Windows' own target; `tidec_debugger_lldb` runs LLDB where `TIDE_LLDB` is found
(Linux by default; CI installs `lldb-$major`). Both use
`compiler/tests/debugger/fixture.tide`. What they check and where they ran:
`notes/debugging.md`.

## Node 24.22.1 security validation (2026-10-23)

The checkout's portable Node 24.22.1 executable matches the official release's
SHA-256. Both debug configurations explicitly select it as TIDE_NODE; their
builds pass with Clang 23.1.2. Full native debug passes all 2,599 enabled tests
(114.01 s), including 39 GPU checks with Vulkan validation/OpenGL. Full web
debug passes all 2,459 enabled tests (285.28 s), including WebGL, Windows ANGLE
D3D11 and 12 WebGPU checks. Both retain the existing disabled MicroDuck
right-turn limitation; no test, assertion, timeout or hash pin was changed.

Docs Svelte check/build and all 49 HTTP/unit/export/SEO checks pass; relay/lobby
37/37 and release-note 3/3 pass. Docs and release-note dependency audits report
zero vulnerabilities. Parsed workflow comparisons confirm that only Node
version/check-latest inputs changed. Production docs/relay Docker images build
and report v24.22.1; hosting image/service validation is recorded in the
services repository. All runs retain TIDE_HIDDEN and muted browser flags.
Engine coverage on other operating systems, Android, MinGW and sanitizers
remains CI's; no workflow was manually dispatched.

## Windows ANGLE D3D11 shader regression (2026-10-19)

tide_add_web_test(... D3D11) adds a Windows-host WebGL run using
--use-angle=d3d11-warp, currently tide_platform_shaders_d3d11. The ordinary
WebGL run still uses SwiftShader. run_web_test.cmake accepts ANGLE_BACKEND
and only gives SwiftShader --enable-unsafe-swiftshader. Both paths retain
muted audio, isolated scratch profiles, CPU reservations and fatal shader
warnings. The D3D11 page must report a renderer containing Direct3D11, so
fallback cannot silently turn this into another SwiftShader pass. The shell's
expect-angle query is a test-only check, not a Tide program argument.

This covers the varying-loop depth comparison reported by Inside Out and
the math/container warnings found while exercising its existing full shader
fixture. See notes/shaders.md. No prototype browser playtest was run.

## Custom browser runners reserve CPUs (2026-10-18)

The three `tide_platform_web_rooms*` tests and `tide_platform_web_threads`
now reserve `TIDE_WEB_TEST_PROCESSORS`, matching `tide_add_web_test`.
Their manually registered tests had bypassed the software renderer's CPU
accounting. Run `39128917687` ran rooms alongside migration/ended rooms and
threaded-browser work on Windows; the hidden host reported 5.878 seconds
skipped, followed by its joiner's session timeout. The log does not prove
the underlying source of that hosted scheduling gap. Reservation prevents
CTest itself from oversubscribing these pages, without changing session
timeouts, test deadlines, assertions or browser flags that affect rendering.
The runners also close Chrome through DevTools before returning, as described
in `notes/webrtc-and-rooms.md`.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## GPU tests on Vulkan (2026-10-14)

(History: the `OPENGL` option, the `*_opengl` twins and `TIDE_BACKEND` went 2026-11-05, above.) Native GPU tests are registered through `tide_gpu_test(<test>... [OPENGL])` (`cmake/VulkanTests.cmake`), which gives them the `gpu` label as `set_tests_properties(... LABELS gpu)` did, and an environment:

- `VK_LOADER_LAYERS_DISABLE=~implicit~`, so overlays and capture tools a machine has installed (Steam's, OBS's, Epic's are on this one) stay out of a test's Vulkan.
- `TIDE_VULKAN_VALIDATION=1` and `VK_ADD_LAYER_PATH`, where CMake found the Khronos validation layer: in `TIDE_VULKAN_SDK`, `VULKAN_SDK`'s `Bin`/`share`/`etc`, or the system's explicit layer folders. Its errors and warnings then stop the test (`notes/rendering.md`, Vulkan). Synchronization validation is on with it, so a missing barrier stops a test too. With no layer, configure says so, and the tests draw with Vulkan unchecked.
- `OPENGL` adds `TIDE_BACKEND=opengl`. `tide_platform_draw`, `_targets`, `_shaders`, `_skinned` and `_gpu_time` have such a twin, `*_opengl`, so OpenGL keeps its coverage now that Vulkan is the default.

Where CMake finds `spirv-val`, `tidec_spirv_<name>` and `tide_spirv_shaders_valid` check tidec's and the renderer's SPIR-V (`notes/shaders.md`, SPIR-V).

This machine has no SDK installed. The Windows SDK 1.4.350.0 installer (`vulkansdk-windows-X64-1.4.350.0.exe`, SHA-256 `855b27ba05d2d8119c5114c5d4ff870ca38f2c632b11e1bb9923b9b7e6ecfe7b`, as Scoop's manifest pins it) was unpacked with 7-Zip into `%LOCALAPPDATA%/TideDev/VulkanSDK/1.4.350.0` (its `Bin` alone comes out, which holds both tools). The debug preset was configured with `-DTIDE_VULKAN_SDK=%LOCALAPPDATA%/TideDev/VulkanSDK/1.4.350.0`. Nothing was registered with the system, so a run outside CTest has no layer unless it sets those variables itself.

`tide_platform_swapchain` and `tide_platform_pipeline_cache` are Vulkan's alone, and set `TIDE_BACKEND=vulkan`: on a machine without it they fail, where the others would draw with OpenGL. So does `versus_draw` (2026-10-20, the drawing benchmark's handwritten baseline, `bench/draw/`), whose `--quick` run draws a few frames of each workload and checks the pixel read back; `versus_draw_opengl` is its OpenGL twin. The baseline enables the validation layer itself under `TIDE_VULKAN_VALIDATION`, with synchronization checks, and stops at its first message.

CI runs no `gpu` test (its runners have no GPU), so Vulkan's coverage is this machine's.

## Building

- The package's web-watch request diagnostics retain its existing guards
  (2026-10-11). Run `38139106428`'s sole underlying failure was
  the initial page's five-second request timeout in the Windows package job;
  every platform suite passed. Individual requests still have that limit,
  and build/pack publication keeps its 120-second readiness guard.
  Failure output names the request, header/body phase, elapsed time and CLI
  output. Injected header/body timeouts, permanent unavailability and invalid
  responses cover failure behavior; evidence and validation are
  in `notes/cli-and-packages.md`, Web-watch request diagnostics. The hosted stall's
  underlying cause is unproven; no server behavior or browser test changed.

- Browser tests registered by `tide_add_web_test` reserve the host's CPU count
  through CTest `PROCESSORS`, for WebGL and WebGPU (2026-10-10). Chrome's software
  renderer starts its own workers, so counting each page as one job let four
  browsers compete on GitHub's small runners. In run 38003829989, Linux's draw
  and shader fixtures reached the 120-second guard without a result; the prior
  shader run had already taken 106 seconds with the same renderer/test code.
  `ProcessorCount` measures the build host even for a WebAssembly build, with
  one as the fallback if the count is unavailable. CTest schedules the page
  within that CPU budget; assertions, browser flags and deadlines are unchanged.
  The Build workflow's web test parallelism also follows the available CPUs,
  instead of its four-job default: private Windows/Linux runners have two CPUs.
  Otherwise a two-CPU reservation would still allow two browsers at once.
  Validation: full Windows debug suites passed 2,010 native and 1,984 web tests,
  with the web suite using CI's Chrome Headless Shell 155.0.8059.39. A scoped
  Linux release build under WSL, restricted to two CPUs with CTest parallelism
  two, ran the draw, targets, shaders and skinned fixtures separately and passed
  all four; draw took 26 seconds and shaders 53 against their original 120-second
  guards. A four-CPU comparison also passed both schedules, with draw/shaders
  taking 42/74 seconds concurrently and 25/57 separately. These timings show the
  scheduling effect, not a hardware-independent benchmark. Workflow lint passes
  with only actionlint 1.7.12's unsupported existing `queue` key excluded.
  GitHub-hosted confirmation still needs a requested CI run; none was dispatched.

- Skinned-model implementation validation (2026-10-08): the merged full native suite passed 1,957 tests and the full web suite passed 1,932. After the subsequent function-capability merge and the empty-alias diagnostic, both full builds passed again, with all 1,880 affected native compiler/editor tests and 1,869 web compiler tests passing. The language-server harness passed 188/188; docs production build and grammar 202/202 passed. CPU pose bits and the persistent match hash agree between native and WASI; OpenGL, WebGL and WebGPU each pass 24 skinning pixel assertions. An isolated Windows clang AddressSanitizer build passed the skinned, pack, map and static-importer groups, and the web CLI watch regression passed. No CI workflow was dispatched; real modelling-tool exports and Android GPU rendering were not exercised here.

Node runs many of the tests (`cmake/run_wasi.mjs`, the relay's, the build service's, the scripts under `platform/tests/`). Use 24.22.1 or a newer 24.x patch on every platform: the [October 21, 2026 security release](https://nodejs.org/en/blog/vulnerability/october-2026-security-releases) fixes the Brotli decoder, TLS/DNS validation and other vulnerabilities. Windows also needs the crash fix introduced in 24.16. Before that fix, a process that starts a TCP connection ends, about one launch in 500, with exit code 3221226505 (0xC0000409) and nothing on stderr, however idle the machine is:

- It's libuv's (`uv__is_fast_loopback_fail_supported` in its `src/win/tcp.c`, which `uv_tcp_connect` goes through; fixed by libuv's #5107, in its 1.53, which Node carries from 24.16): it hands `RtlGetVersion` a struct whose size it never set, right under the function's stack cookie. Where the bytes left there say 284, the size of the longer struct, Windows writes 8 bytes more, over the cookie, and the function stops the process as it returns. In a test file's process those bytes are half of a heap's address, which is 284 once in about 500 launches.
- `node --test` then reports the file as failed with no test named, its process gone before its first test ran; `--test-reporter=tap` shows the exit code. It's never the test's: nothing is loosened or run again for it. The Node is replaced.
- CI's Build and Docs workflows request `^24.22.1` with `check-latest: true` (`actions/setup-node`), so older cached Node versions cannot satisfy the security floor and newer 24.x patches are selected. The runners' own Node is not used for these tests.
- A crash of Node's own is found by running the process under lldb, which leaves its streams pipes (`lldb -b -o run -k "thread backtrace all" -k quit -- node <file>`, with `NODE_TEST_CONTEXT=child-v8` in the environment for a test file as `node --test` runs it), and `llvm-objdump -d` reads a frame of `node.exe` without its symbols.

Every browser a test starts is muted (the owner, 2026-10-08, who heard an agent's runs: a page in headless Chrome, sent keys through the debugger). Headless, Chrome and Edge still play through the machine's own output device once a page's AudioContext runs, and a page's resumes at its first pointer, key or touch (`platform/web/tide.js`), so the tests were silent only for as long as none of them sent one.

- Each launch has `--mute-audio`: `cmake/run_web_test.cmake` and `cmake/run_web_served_test.mjs` (every `tide_add_web_test`), `platform/tests/web_threads.mjs`, `web_rooms.mjs` and `rooms.mjs`, `docs/tests/examples.mjs`, and `bench/lag.mjs`, which is no test but sends its pages keys. Firefox has no such flag: `rooms.mjs` gives its profile `media.volume_scale` 0.
- It mutes what reaches the device and nothing before it: the page's context still runs, so a test of the page's audio goes as it did.
- A browser started by hand is given it too, as an agent's scratch page is (`AGENTS.md`, Rendering and platform): the variable that silences Tide's own programs (`TIDE_HIDDEN`: `notes/standard-host.md`, Nobody listens either) is nothing a browser reads.
- Nothing checks that a launch has the flag: a new runner has to be given it. And nobody listened for the difference: that every test passes with the flag on its command line, and the rooms' Firefox cases with the setting, is all that was checked.

### Cross-platform determinism tests

- Skinned poses have an exact CPU hash in `tests/test_skinned.c`: `skinned_crossplatform_pose_bits` samples STEP, LINEAR and CUBICSPLINE clips, blends poses and resolves node matrices, pinning `e5d23f82e5d18f26` on native and WASI. The focused `skinned` runtime group passes 14/14 on both (2026-10-08), including checked decoding, pack mapping, snapshot storage and GPU palette capture. GPU deformation is checked separately by `tide_platform_skinned`: twelve pixel assertions, repeated over two frames, pass on OpenGL 3.3, WebGL 2 and WebGPU. The native fixture has label `gpu`; the web's second run is `tide_platform_skinned_webgpu`, label `webgpu`. Its self-contained glTF has eight influences per vertex, a mesh-node translation that skinning must ignore, animated translation and nonuniform scale, and a normal/tangent whose X components account for the importer's coordinate reflection. The checks cover the built-in draw, custom vertex fields and posed normals/tangents before user code, independent pose captures, explicit matching depth/color passes, and a vert that captures `this.clip` before a later position edit: its handed data, final frag world position and two blank alternate placements distinguish skinning and projection order. Shader goldens and CPU-only shader diagnostics are `compiler/tests/shaders/skinned` and `compiler/tests/errors/gpu_skinned_*`.

- (History as to backends: OpenGL and WebGL went 2026-11-05, and the web runs are WebGPU only, above.) The demo's smoke test (`demo --smoke`, or `demo.html?smoke`) plays a scripted session through the real platform layer. It checks a hash of the simulation (`SMOKE_HASH` in `demo/main.c`, the same on every platform) and pixels read back from the renderer. The native presets run it in a hidden window, so they need a desktop session with OpenGL; so does `tide_platform_draw`, which checks the renderer's pixels. Native tests like these have the `gpu` label, which CI leaves out (`ctest -LE gpu`): its Windows and Linux runners have no GPU, and on its macOS ones GLFW finds no OpenGL to open a window with (it was tried: the window isn't made). So nothing runs Tide's window and its OpenGL on a Mac; there, only the web tests draw, in Chrome, on WebGL and WebGPU. The web presets run both in headless Chrome or Edge, and skip them if neither browser is found: on the browser's software WebGL (`backend=webgl`, in virtual time, as every `tide_add_web_test`), and again on its software WebGPU (`demo_smoke_web_webgpu`, `tide_platform_draw_webgpu`: `tide_add_web_test(... WEBGPU)`, label `webgpu`). The browser only answers about its GPU in real time, so those pages are served (`cmake/run_web_served_test.mjs`), cross-origin isolated, so on threads too, and `backend=webgpu` fails rather than falls back. Chrome has no WebGPU when its own drawing runs on SwiftShader (`--use-angle=swiftshader`, the other web tests' flag), so these run it with `--enable-unsafe-webgpu --use-webgpu-adapter=swiftshader`, and on Windows `--use-angle=d3d11-warp`. CI runs them on Windows and macOS. On Linux's runners, which have no GPU, Chrome draws with WebGPU but gives nothing back when a test reads its pixels (through a 2D canvas, the only way that doesn't wait), whatever flags it's given, so CI leaves the label out there (`ctest -LE webgpu`), as it leaves `gpu` out everywhere. A served page has 100 seconds to give its result; a test whose work takes the software WebGPU longer names its own (`SECONDS`, `--seconds` to the runner): `bench_render3d_webgpu` takes about a minute on CI's Windows runner alone and up to 110 seconds beside other tests, so it has 240 (2026-10-25, after it failed several dev runs at 100), as has `tide_platform_shaders_webgpu`, 65-90 seconds on CI's macOS runner and past 100 now and then. On Android it's the demo's app, started by hand with its argument in the intent (`adb shell am start -n dev.tide.demo/android.app.NativeActivity --es tide.args "--smoke"`), and it says how it went in the log. Its pixel checks hold whatever the screen's shape, a phone's upright included: the demo's camera fits the arena to the screen's narrower side (`DrawArena` in `demo/demo.tide`), so the player is on it wherever the script left it.

## Confirmed presentation/physics rewind validation (2026-10-11)

Local full native debug: 2,132 CTest tests passed. Local full web debug: 2,104 passed, including WebGL/WebGPU pages, with Node 24.16.0 configured for both builds. Docs npm build passed. The final Box2D rotation/cleanup changes additionally pass its package and Towers suites on native/web. Release rewind executables pass their correctness tests and report batch timings in notes/box2d.md and notes/box3d.md. Simulation/body/native-image/answer pins agree natively and on web; changed world hashes reflect intentional schema additions. The full suites also include malicious view-time wire tests, singleton methods/LSP, independent confirmed interpolation and custom interpolation across separate endpoint heaps. Android, Linux/macOS/MinGW and sanitizers were not run on this Windows machine; CI was not dispatched.

After merging origin/dev, both full builds and suites pass again: 2,158/2,158 native (174.68 s) and 2,129/2,129 web (339.92 s), plus the production docs build. Incoming LOD and Rooms page hosts were migrated to the confirmed frame arguments. The merged suites include their tests, all rewind/view-time/heap interpolation regressions and WebGL/WebGPU coverage. No CI was dispatched.

Latency/frame-rate follow-up: full native debug passed 2,160/2,160 tests (203.17 s), full web debug passed 2,131/2,131 (333.09 s), and the production docs build passed, using Node 24.16.0. Both suites include the held-input prediction fix. After refining the new fixtures to move the target through its owner's sampled input and checking acceptance counts against actual shot age, both final rewind-network targets passed again on native and web. Each package runs 28 two-client profiles; the age oracle checks every received shot and accepted hits match the actual drawn translating box within 0.0001 units/fraction. Existing shape/rotation tests provide separate coverage. These deterministic loopback profiles simulate latency, jitter, loss, update/render rates and stalls; they do not measure real Internet traffic or display scanout. Other operating systems, Android and sanitizers were not run; no CI was dispatched.

The follow-up merged incoming JSON/compiler and Inside Out work cleanly. Rebuilt both rewind-network games and view_time with the merged compiler/runtime, then passed those three targets on native (6.35 s) and web (8.41 s). The merged production docs build also passed. No session/physics conflict required another full-suite run.
