# Test infrastructure

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

Native GPU tests are registered through `tide_gpu_test(<test>... [OPENGL])` (`cmake/VulkanTests.cmake`), which gives them the `gpu` label as `set_tests_properties(... LABELS gpu)` did, and an environment:

- `VK_LOADER_LAYERS_DISABLE=~implicit~`, so overlays and capture tools a machine has installed (Steam's, OBS's, Epic's are on this one) stay out of a test's Vulkan.
- `TIDE_VULKAN_VALIDATION=1` and `VK_ADD_LAYER_PATH`, where CMake found the Khronos validation layer: in `TIDE_VULKAN_SDK`, `VULKAN_SDK`'s `Bin`/`share`/`etc`, or the system's explicit layer folders. Its errors and warnings then stop the test (`notes/rendering.md`, Vulkan). Synchronization validation is on with it, so a missing barrier stops a test too. With no layer, configure says so, and the tests draw with Vulkan unchecked.
- `OPENGL` adds `TIDE_BACKEND=opengl`. `tide_platform_draw`, `_targets`, `_shaders`, `_skinned` and `_gpu_time` have such a twin, `*_opengl`, so OpenGL keeps its coverage now that Vulkan is the default.

Where CMake finds `spirv-val`, `tidec_spirv_<name>` and `tide_spirv_shaders_valid` check tidec's and the renderer's SPIR-V (`notes/shaders.md`, SPIR-V).

This machine has no SDK installed. The Windows SDK 1.4.350.0 installer (`vulkansdk-windows-X64-1.4.350.0.exe`, SHA-256 `855b27ba05d2d8119c5114c5d4ff870ca38f2c632b11e1bb9923b9b7e6ecfe7b`, as Scoop's manifest pins it) was unpacked with 7-Zip into `%LOCALAPPDATA%/TideDev/VulkanSDK/1.4.350.0` (its `Bin` alone comes out, which holds both tools). The debug preset was configured with `-DTIDE_VULKAN_SDK=C:/Users/blenm/AppData/Local/TideDev/VulkanSDK/1.4.350.0`. Nothing was registered with the system, so a run outside CTest has no layer unless it sets those variables itself.

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

Node runs many of the tests (`cmake/run_wasi.mjs`, the relay's, the build service's, the scripts under `platform/tests/`), and on Windows it has to be 24.16 or newer. Before that, and in every 22 so far, a process that starts a TCP connection ends, about one launch in 500, with exit code 3221226505 (0xC0000409) and nothing on stderr, however idle the machine is:

- It's libuv's (`uv__is_fast_loopback_fail_supported` in its `src/win/tcp.c`, which `uv_tcp_connect` goes through; fixed by libuv's #5107, in its 1.53, which Node carries from 24.16): it hands `RtlGetVersion` a struct whose size it never set, right under the function's stack cookie. Where the bytes left there say 284, the size of the longer struct, Windows writes 8 bytes more, over the cookie, and the function stops the process as it returns. In a test file's process those bytes are half of a heap's address, which is 284 once in about 500 launches.
- `node --test` then reports the file as failed with no test named, its process gone before its first test ran; `--test-reporter=tap` shows the exit code. It's never the test's: nothing is loosened or run again for it. The Node is replaced.
- CI's Build workflow installs Node 24 on every platform (`actions/setup-node`), as the docs' does: the runners' own is 22.
- A crash of Node's own is found by running the process under lldb, which leaves its streams pipes (`lldb -b -o run -k "thread backtrace all" -k quit -- node <file>`, with `NODE_TEST_CONTEXT=child-v8` in the environment for a test file as `node --test` runs it), and `llvm-objdump -d` reads a frame of `node.exe` without its symbols.

Every browser a test starts is muted (the owner, 2026-10-08, who heard an agent's runs: a page in headless Chrome, sent keys through the debugger). Headless, Chrome and Edge still play through the machine's own output device once a page's AudioContext runs, and a page's resumes at its first pointer, key or touch (`platform/web/tide.js`), so the tests were silent only for as long as none of them sent one.

- Each launch has `--mute-audio`: `cmake/run_web_test.cmake` and `cmake/run_web_served_test.mjs` (every `tide_add_web_test`), `platform/tests/web_threads.mjs`, `web_rooms.mjs` and `rooms.mjs`, `docs/tests/examples.mjs`, and `bench/lag.mjs`, which is no test but sends its pages keys. Firefox has no such flag: `rooms.mjs` gives its profile `media.volume_scale` 0.
- It mutes what reaches the device and nothing before it: the page's context still runs, so a test of the page's audio goes as it did.
- A browser started by hand is given it too, as an agent's scratch page is (`AGENTS.md`, Rendering and platform): the variable that silences Tide's own programs (`TIDE_HIDDEN`: `notes/standard-host.md`, Nobody listens either) is nothing a browser reads.
- Nothing checks that a launch has the flag: a new runner has to be given it. And nobody listened for the difference: that every test passes with the flag on its command line, and the rooms' Firefox cases with the setting, is all that was checked.

### Cross-platform determinism tests

- Skinned poses have an exact CPU hash in `tests/test_skinned.c`: `skinned_crossplatform_pose_bits` samples STEP, LINEAR and CUBICSPLINE clips, blends poses and resolves node matrices, pinning `e5d23f82e5d18f26` on native and WASI. The focused `skinned` runtime group passes 14/14 on both (2026-10-08), including checked decoding, pack mapping, snapshot storage and GPU palette capture. GPU deformation is checked separately by `tide_platform_skinned`: twelve pixel assertions, repeated over two frames, pass on OpenGL 3.3, WebGL 2 and WebGPU. The native fixture has label `gpu`; the web's second run is `tide_platform_skinned_webgpu`, label `webgpu`. Its self-contained glTF has eight influences per vertex, a mesh-node translation that skinning must ignore, animated translation and nonuniform scale, and a normal/tangent whose X components account for the importer's coordinate reflection. The checks cover the built-in draw, custom vertex fields and posed normals/tangents before user code, independent pose captures, explicit matching depth/color passes, and a vert that captures `this.clip` before a later position edit: its handed data, final frag world position and two blank alternate placements distinguish skinning and projection order. Shader goldens and CPU-only shader diagnostics are `compiler/tests/shaders/skinned` and `compiler/tests/errors/gpu_skinned_*`.

- The demo's smoke test (`demo --smoke`, or `demo.html?smoke`) plays a scripted session through the real platform layer. It checks a hash of the simulation (`SMOKE_HASH` in `demo/main.c`, the same on every platform) and pixels read back from the renderer. The native presets run it in a hidden window, so they need a desktop session with OpenGL; so does `tide_platform_draw`, which checks the renderer's pixels. Native tests like these have the `gpu` label, which CI leaves out (`ctest -LE gpu`): its Windows and Linux runners have no GPU, and on its macOS ones GLFW finds no OpenGL to open a window with (it was tried: the window isn't made). So nothing runs Tide's window and its OpenGL on a Mac; there, only the web tests draw, in Chrome, on WebGL and WebGPU. The web presets run both in headless Chrome or Edge, and skip them if neither browser is found: on the browser's software WebGL (`backend=webgl`, in virtual time, as every `tide_add_web_test`), and again on its software WebGPU (`demo_smoke_web_webgpu`, `tide_platform_draw_webgpu`: `tide_add_web_test(... WEBGPU)`, label `webgpu`). The browser only answers about its GPU in real time, so those pages are served (`cmake/run_web_served_test.mjs`), cross-origin isolated, so on threads too, and `backend=webgpu` fails rather than falls back. Chrome has no WebGPU when its own drawing runs on SwiftShader (`--use-angle=swiftshader`, the other web tests' flag), so these run it with `--enable-unsafe-webgpu --use-webgpu-adapter=swiftshader`, and on Windows `--use-angle=d3d11-warp`. CI runs them on Windows and macOS. On Linux's runners, which have no GPU, Chrome draws with WebGPU but gives nothing back when a test reads its pixels (through a 2D canvas, the only way that doesn't wait), whatever flags it's given, so CI leaves the label out there (`ctest -LE webgpu`), as it leaves `gpu` out everywhere. On Android it's the demo's app, started by hand with its argument in the intent (`adb shell am start -n dev.tide.demo/android.app.NativeActivity --es tide.args "--smoke"`), and it says how it went in the log. Its pixel checks hold whatever the screen's shape, a phone's upright included: the demo's camera fits the arena to the screen's narrower side (`DrawArena` in `demo/demo.tide`), so the player is on it wherever the script left it.

## Confirmed presentation/physics rewind validation (2026-10-11)

Local full native debug: 2,132 CTest tests passed. Local full web debug: 2,104 passed, including WebGL/WebGPU pages, with Node 24.16.0 configured for both builds. Docs npm build passed. The final Box2D rotation/cleanup changes additionally pass its package and Towers suites on native/web. Release rewind executables pass their correctness tests and report batch timings in notes/box2d.md and notes/box3d.md. Simulation/body/native-image/answer pins agree natively and on web; changed world hashes reflect intentional schema additions. The full suites also include malicious view-time wire tests, singleton methods/LSP, independent confirmed interpolation and custom interpolation across separate endpoint heaps. Android, Linux/macOS/MinGW and sanitizers were not run on this Windows machine; CI was not dispatched.

After merging origin/dev, both full builds and suites pass again: 2,158/2,158 native (174.68 s) and 2,129/2,129 web (339.92 s), plus the production docs build. Incoming LOD and Rooms page hosts were migrated to the confirmed frame arguments. The merged suites include their tests, all rewind/view-time/heap interpolation regressions and WebGL/WebGPU coverage. No CI was dispatched.

Latency/frame-rate follow-up: full native debug passed 2,160/2,160 tests (203.17 s), full web debug passed 2,131/2,131 (333.09 s), and the production docs build passed, using Node 24.16.0. Both suites include the held-input prediction fix. After refining the new fixtures to move the target through its owner's sampled input and checking acceptance counts against actual shot age, both final rewind-network targets passed again on native and web. Each package runs 28 two-client profiles; the age oracle checks every received shot and accepted hits match the actual drawn translating box within 0.0001 units/fraction. Existing shape/rotation tests provide separate coverage. These deterministic loopback profiles simulate latency, jitter, loss, update/render rates and stalls; they do not measure real Internet traffic or display scanout. Other operating systems, Android and sanitizers were not run; no CI was dispatched.

The follow-up merged incoming JSON/compiler and Inside Out work cleanly. Rebuilt both rewind-network games and view_time with the merged compiler/runtime, then passed those three targets on native (6.35 s) and web (8.41 s). The merged production docs build also passed. No session/physics conflict required another full-suite run.
