# Tide: Agent Guide

Working rules and agreed decisions for humans and AI agents. Implementation details, tests, measurements and feature status belong in `notes/`. Language syntax and semantics belong in `docs/spec.md`, which distinguishes decided, provisional and open designs. A proposal or inactive implementation is not an approved requirement.

## Working agreement

- The project owner is the lead programmer and writes most of the code. Agents do specific, scoped tasks: do not scaffold systems, widen scope or refactor nearby code without being asked. If code versus discussion is unclear, ask.
- Within an assigned task, small API completions that follow existing conventions and simplify game code are clear wins: implement and validate them without asking about routine edge cases. Bring substantive language, behavior or ownership decisions to the owner.
- The owner decides language design. The agent working on a transpiler task implements and maintains its compiler changes, tests and build integration, and proposes language changes for approval. The compiler, CLI and editor integrations are agent-maintained; references to Claude record authorship, not exclusive ownership.
- The owner writes the runtime, with an existing exception: agents maintain the temporary implementations listed in `notes/runtime-ownership.md` until the owner takes them over. Read that note before changing the generated-C/runtime boundary. Any new runtime function generated code needs is agreed on before either side depends on it.
- Validate changes locally, commit the relevant notes with the work, then merge directly into `dev` using the procedure below. Do not create pull requests, push feature branches or dispatch CI unless the owner asks.
- Before changing a subsystem, read its notes file **whole**, plus related notes for boundaries the change touches. Record what was built, its contracts, tests, remaining gaps and corrections in the same commit. Put only rules and agreed decisions needed across sessions in this file.
- Changes to the language, CLI or editors update the corresponding user documentation as well as the spec when semantics change. Keep decided behavior separate from provisional behavior and open questions.

## Quiet agent runs

Before running commands, set `TIDE_HIDDEN=1` in the agent's shell: PowerShell `$env:TIDE_HIDDEN = '1'`; POSIX `export TIDE_HIDDEN=1`. Repeat it for fresh shells. Never set it in the owner's environment or editor run configurations. Claude Code gets it from the checked-in `.claude/settings.json`.

- On desktop, Tide refuses visible windows and audio devices and suppresses crash dialogs when the variable is nonempty and not `0`. Tests and `tide run` inherit it; details are in `notes/standard-host.md`.
- Run only programs built in this checkout. Another checkout's binaries may predate these safeguards. Never run a game without `--capture`, `--frames` or `--script`.
- Browsers do not read `TIDE_HIDDEN`. Every Chrome/Edge launch, including headless tests and scratch pages, must have `--mute-audio`. Firefox test profiles use `media.volume_scale=0`. See `notes/testing.md`.

## What Tide is

Tide is a networking-first game engine written in strict C17 and compiled with clang. Simulation uses deterministic floats, not fixed point. Its language transpiles to C; game and engine code are optimized together, allowing calls to be inlined. The ECS derives data dependencies from the language and schedules work automatically. Rendering is secondary to simulation and networking.

Desktop and consoles are the main targets. Android, then iOS, are mobile targets. WebAssembly is actively supported and tested, with WebGL 2 and WebGPU rendering. Desktop and web players share a deterministic simulation. Touch is input wherever available. A phone that backgrounds or closes leaves the match; enabled host migration transfers its hosted match, rather than continuing simulation in the background.

## Building

Requires CMake 3.25+, Ninja and clang. The toolchain finds clang through `$LLVM_ROOT/bin`, a standalone Windows LLVM installation, `PATH`, then Visual Studio's bundled clang.

| Action | Command |
|---|---|
| Configure, build and test native debug | `cmake --workflow --preset debug` |
| Build configured native debug | `cmake --build --preset debug` |
| Run native tests | `ctest --preset debug` |
| Run selected engine tests | `build/debug/bin/tide_tests [name-filter]` |
| Configure, build and test web debug | `cmake --workflow --preset web-debug` |
| Run web tests | `ctest --preset web-debug` |

`debug` has no optimization; `release` is optimized with debug information. `sanitize` enables ASan and UBSan on Linux/macOS. Web, MinGW, Android and packaging presets are in `CMakePresets.json`; read their notes before using them. Windows Node-based tests require Node 24.16 or newer (`notes/testing.md`).

Desktop configuration downloads pinned GLFW. Linux desktop builds need X11 development headers. `-DTIDE_PLATFORM=OFF` omits the platform layer and targets requiring it, useful offline. Native builds copy `tidels` to `build/tools/tidels` for the editors. Build integration and layout details are in `notes/repository.md` and `notes/tide-programs.md`.

Warnings are errors. Fix warnings encountered during validation, including ones already present:

- C uses `-Wall -Wextra -Wpedantic -Werror`; third-party sources have their warnings disabled. Under strict C17, a file calling POSIX functions defines `_DEFAULT_SOURCE` before its first include on platforms other than Windows and Apple, as `platform/src/rtc/net.c` does.
- `tide_add_game` rejects unexpected tidec warnings. Fixtures expecting warnings declare them with `WARNINGS` and pin their messages.
- Web test pages enable `Tide.checkGL`; WebGL/WebGPU validation errors fail tests. Keep the web build working.

### Testing and merging

Test the change once locally before merging, according to scope:

| Scope | Required validation |
|---|---|
| Compiler, engine, platform or build | Full native and web debug suites |
| Repository game, including a benchmark or prototype | Its targets and tests, native and web |
| `docs/` only | `npm run build` in `docs/` |
| `relay/` only | `node --test relay/relay.test.mjs relay/lobbies.test.mjs` |
| `tools/` only | That tool's tests |
| Unbuilt text, including this guide and notes | No build or test suite; check references and the diff |

Android emulator tests are CI's job unless the owner asks for Android work. Linux, macOS, MinGW and sanitizers are also CI validation for this Windows machine. Tests needing a window, GPU or browser follow `notes/testing.md`; report unavailable coverage.

Merge procedure: fetch, merge `origin/dev` into the task branch, then push `HEAD:dev`. Inspect `git diff --stat ORIG_HEAD HEAD` after a merge:

- Unrelated incoming changes and a clean merge require no repeat tests.
- Changes to a dependency require building the affected target and running its tests.
- Same-file/subsystem overlap or conflicts require tests for that overlap on native/web as applicable. Run both full suites again only for overlap that broad.
- One round: if `dev` moves again with unrelated work, merge and push without restarting validation.

### Other repositories

The owner's prototype games are in `Tide-Engine/tide-prototypes` and the managed hosting and build services in `Tide-Engine/tide-services`, both private. Each follows this repo's `dev` with no pinned version and has its own guide, notes and `dev`; this repo never depends on them. A change spanning repos merges here first, then in the other.

- Prototypes build as part of an engine checkout: configure with `-DTIDE_EXTERNAL_DIRS=<tide-prototypes checkout>` (or that environment variable, a path list). The Docs workflow builds them that way to stage the website's prototype pages and admin catalog (`docs/prototypes.js`).
- Engine changes need no prototype or service validation unless the task names one. A task changing an interface they use (`tide_add_game`, `docs/static/prototypes/`, `relay/`, `tide deploy`/`tide publish`) also builds or tests that repository against the change and reports any breakage.
- The split is described, with how to carry a branch made before it, in `notes/repository.md`.

CI runs only when manually requested. Pushes and pull requests trigger neither Build nor Docs. The requested Build workflow tests platforms, builds the docs site, and on `dev` deploys docs and publishes a nightly after success. Do not dispatch it automatically. Report when changes need validation this machine cannot run. When asked to run CI, inspect its completed run and fix failures attributable to this work before anything else. See `notes/releases-and-ci.md`.

### Cross-platform determinism tests

`tests/test_crossplatform.c` and `compiler/tests/e2e/crossplatform.tide` pin exact math/simulation bits. Randomness is pinned by `crossplatform_random` and `random_crossplatform_match` in `compiler/tests/e2e/random_test.c`, running `random.tide`. Native/web and all configurations must agree. Change expected hashes only for an intentional result change, then confirm other platforms/configurations agree.

Do not depend on C's unspecified argument evaluation order. Draw random inputs into locals before calling a function.

## Language and runtime contracts

- Derive behavior from the language where possible; be explicit where needed and keep dependency trees static. Systems declare read/write access separately from entity filters; filters alone create no data dependencies.
- Preserve caller capabilities through helper chains. Match code writes the match, views read it and write local state, and `Sample` is read-only. Helpers gain no implicit singleton lookup or unrestricted world access. Read `docs/spec.md` and `notes/language.md` before altering functions, singleton parameters, local fields or `World` behavior.
- Functions can send events, spawn and change entities without an explicit `World` parameter, subject to their caller's capabilities. Their side effects contribute to the caller's scheduling and evaluation order. Ordinary explicit `World` parameters remain the interface for world queries.
- Errors are values, not exceptions (`fails` and local handling). Async functions/handlers use `async`/`await`; match tasks belong to the world, including snapshots, hashes and packed bytes. Match waits must be deterministic; outside-service answers enter through input.
- C reached through `extern` is trusted. Determinism, external state and thread safety are the game's responsibility; the compiler does not infer C data dependencies. Calls to C and helpers calling C retain source side-effect order. Generated C declares extern functions from their Tide signatures in the `.c`, not the generated header or a library header.
- Tide supplies no C++ runtime, standard library headers, exceptions or RTTI. Game `.cpp` files use C++20 with `-nostdinc++ -fno-exceptions -fno-rtti -fno-threadsafe-statics` and a C API. Runtime-dependent C++ needs an appropriate prebuilt library; web libraries must be static and need no C++ runtime. See `notes/cli-and-packages.md` and `notes/c-interop.md`.
- **No Tide code can corrupt memory.** Bounds, entity generations, allocation sizes, cache lifetimes and integer operations must remain safe for arbitrary game values. Defined integer overflow wraps; divide/remainder by zero give zero; shift counts are masked; float-to-int conversion saturates and maps NaN to zero. A game's C remains trusted. Read `notes/memory-safety.md` before changing runtime code taking game values.
- Error messages explain the fix when knowable. Unused access warns: a parameter unused as data should be a filter, and `mut` never written should be removed.
- No backward compatibility yet: rename/change freely, without old aliases; diagnostics may name the replacement. Packages supporting several versions use version symbols such as `TIDE_0_3_OR_NEWER`. No platform symbols: every machine of a match runs the same code.
- When no Tide convention applies, follow Unity.Mathematics and Unity's Input System for names, axes, units and ordering. Tide's conventions, including PascalCase methods, win.

## ECS and simulation state

- Generated C specializes component types, storage, dispatch and ticks for each game; there is no runtime type registry.
- The match world owns all simulation state, including singleton and allocator state. Components store handles/offsets, not pointers into simulation memory. Snapshotting captures the simulation. Worlds are passed explicitly and several can coexist.
- Local state lives outside the **match** world in machine-local storage. It is never sent, rolled back or hashed with the match. The compiler keeps match code from reading local state and local code from changing the match.
- Generated types have explicit zeroed padding and size assertions. Input repairs also clear padding. Preserve these contracts for raw state comparisons, snapshots, packing and hashing.

## Rendering and platform

Simulation never includes platform headers. Views read the world and draw without modifying simulation state. Device input enters through `Devices`. Keep generated game headers separate from system/GLFW headers because type names collide; hosts use `tide/platform.h`.

`frag` and `vert` compile to GLSL for OpenGL/OpenGL ES/WebGL, WGSL for WebGPU, and SPIR-V for Vulkan, which tidec translates from the WGSL. Desktop and Android draw with Vulkan where the device has 1.3 (MoltenVK, which tide ships, on a Mac without a loader), OpenGL 3.3 and OpenGL ES 3 being the fallbacks. GPU results are visual only, never match state. Read `notes/rendering.md` before changing backend design.

## Networking

- Server authority, prediction and rollback apply to every match. Single-player uses the same server/client path through loopback; there is no separate offline simulation or engine-imposed pause. Matches start closed; `Session.Open`/`Close` control admission.
- `Devices` uses explicit read-only parameters. Local code reads this machine's devices; match code reads the entity owner's tick input, or the server's for unowned entities/once-only code. Send only the fields match code reads; preserve text/window-position restrictions.
- UI actions use typed commands sent by local code. The connection supplies the sender. Handlers return void and validate with ordinary guards, with no acceptance method, transaction or return status. Commands are copied, bounded payloads, resent until verified sequence acknowledgement. The server assigns tick/order; sequence state survives snapshots and migration. Current limits and channel semantics are in `notes/netcode.md` and the spec's Commands section.
- Clients run ahead to deliver their own input without input delay; only other players' input is guessed. The server reports input arrival lead. Clients sending no input keep their join lead.
- Desktop and web share unordered, unreliable WebRTC channels and rooms. Desktop also supports direct UDP. The relay introduces peers; match traffic uses direct connections first and TURN where necessary. Dedicated servers remain supported. Read `notes/webrtc-and-rooms.md` before changing transport or room behavior.
- Repair NaN/infinite input fields to their defaults before `Sanitize`. Math should avoid propagating invalid values: preserve forgiving clamp/min/max/normalize behavior in `tide/math.h`.
- Hash and check the world **every tick, in every build**. Optimize its cost rather than making it periodic. Verified ticks have received required state and corrections; later ticks are predicted.
- Visibility is decided dynamically by the server for entities and whole components, with ordinary state behind it; there is no field visibility. Hidden state is absent from the recipient's simulation. Put secrets in restricted components or scenes, not public match singletons or public payloads. Client verification hashes permitted logical values and repairs from an acknowledged authoritative baseline. Availability (Shown/Hidden) is distinct from actual lifetime (Spawned/Destroyed). Current transfer, prediction and migration limitations are in `notes/netcode.md` and `notes/language.md`.
- Managed hosting runs one bounded WebAssembly instance per match, including game C. `extern` remains allowed inside the sandbox; match code and its C must build for web, and server builds may omit local code/views. Local artifact builds are the default; remote source builds remain deferred. GitHub sign-in, Stripe monthly subscriptions with included usage and opt-in prepaid extra usage, and Fly.io are selected. Monthly allowance expires; purchased credit carries forward. Contracts, validation and the remaining public-launch work are in `notes/managed-hosting.md` of `Tide-Engine/tide-services`.

## Modding

Read `notes/modding.md` and the spec's Mods section before modding work. Package-level `virtual`/`override` exists; mod restrictions, alternate-build loading and sandbox integration do not. The build service is on hold. Its remote-build, signing, revocation and closed-source model are not current requirements and must not be reinstated implicitly. Agreed mods must preserve platform parity and the same whole build for all players.

## Performance

Runtime, compilation and rendering speed are requirements. Compare equivalent work with the best practical handwritten implementation, retaining determinism guarantees. An unexplained order-of-magnitude gap is a performance bug. Flexible features may cost more while the common fixed path stays fast; keep engine/game friction low and snapshot/restore cheap.

Internal optimizations are authorized within the assigned task when observable language behavior, deterministic results, ordering, snapshots and hot reload are preserved. Storage, scheduling, batching and caching are implementation choices; language expectations remain the owner's decision.

## Determinism

Given the same build and inputs, simulation results must be bit-identical across supported platforms and configurations.

- Use IEEE 754 basic operations (`+ - * /` and `sqrt`) as the basis of float math. No FMA contraction, fast-math, reassociation or reordered float operations.
- Simulation uses Tide's math, not platform `sin`, `cos`, `atan2`, `exp`, `pow`, etc. Vector/matrix/quaternion math is in-house. Third-party transcendental code must be vendored source compiled with Tide's flags; correctly rounded implementations such as CORE-MATH are preferred.
- No approximate `rsqrt`/reciprocal instructions, x87 or relaxed WebAssembly SIMD. Ordinary `-msimd128` is supported. Denormals stay enabled (no FTZ/DAZ) on every thread/platform. NaN simulation state is a bug because NaN payload bits are not portable.
- Parallelism cannot alter observable results, including system order, deferred changes, reductions and entity IDs. Conflicting systems run serially in deterministic order; two systems writing one component is normal. Tooling explains their dependencies.
- Named `[Stage(Enum.Member, offset)]` phases follow enum declaration order, independently for systems, views and each event's handlers. Offset is a constant int, defaults to zero and orders within a phase; empty phases retain their place. `[Before]`/`[After]` can name declarations or phases. Later-phase systems wait for all earlier-phase systems of the same enum, even without shared data. Different enums define independent orders.
- The scheduler's reported **stage number** is dependency depth, distinct from a named phase. Ready systems run as soon as their dependencies finish; there is no additional barrier between equal-depth groups. Entity splitting must preserve serial results. See `notes/schedule-and-threads.md`.

## Packaging and releases

Tide is licensed `MIT OR Apache-2.0` (`LICENSE-MIT`, `LICENSE-APACHE`). Users install the `tide` command; see `README.md`. Versions follow conventional commits: `fix:` is a patch, `feat:` is a minor, and breaking changes (`feat!:`) are minor while versions start with 0. Avoid breaking changes where possible. `.releaserc.json` and `notes/releases-and-ci.md` describe release machinery.

## Notes index

Read the notes for every subsystem the task touches. Paths and former section references are detailed in `notes/repository.md`; older comments saying “AGENTS.md, <section>” still resolve through that file. Public runtime comments describe code, not agent ownership or this guide, which is not in the package.

| Area / files | Notes |
|---|---|
| Repository layout, other repositories and the split, build navigation, legacy section redirects, guide maintenance | `notes/repository.md` |
| Windows/platform layer, GPU backends, fonts, renderer steps | `notes/rendering.md` |
| Draw API, draw lists, primitives, camera/settings, instancing | `notes/drawing.md` |
| `compiler/src/shader.c`, GPU declarations and shader fixtures | `notes/shaders.md` |
| Texture storage, sampling, wrap, mipmaps, alpha | `notes/textures.md` |
| Render targets, formats, depth, samples, cubemaps | `notes/render-targets.md` |
| Audio runtime, mixer, decoders, devices, microphone | `notes/audio.md` |
| GUI controls, theme, input claims and focus | `notes/gui.md` |
| `tide/run.h`, `host.h`, capture/scripts/players, debug/stats/profiler, hidden runs | `notes/standard-host.md` |
| Asset declarations/importers/packs, static/skinned meshes and animation, files/fetch | `notes/assets-and-files.md` |
| GPU/browser test runners, Node requirements, test labels | `notes/testing.md` |
| `platform/web/`, WASI, `tide.js`, web threads and tests | `notes/web.md` |
| MinGW/Android toolchains, APKs, signing, Android runner | `notes/android-and-mingw.md` |
| `bench/`, harness and saved measurements | `notes/benchmarks.md` |
| `cmake/Tide.cmake`, `run_tidec.cmake`, generated host API/header | `notes/tide-programs.md` |
| Docs site, pages/examples, SEO, hosting and docs workflow | `notes/docs-site.md` |
| `examples/towers/`, `examples/sumo/` | `notes/physics-games.md` |
| `compiler/cli/`, package resolution, built-in clang, package contents | `notes/cli-and-packages.md` |
| `.github/`, installers, release/upgrade code and nightlies | `notes/releases-and-ci.md` |
| `packages/box2d/` | `notes/box2d.md` |
| `packages/box3d/`, resident state | `notes/box3d.md` |
| `packages/rooms/` | `notes/rooms-package.md` |
| `packages/lobby/`, the client of the relay's lobbies and its menu | `notes/lobby-package.md` |
| `tide/network.h`, a game's own match network, `examples/network/`, `packages/test/network_players.h` | `notes/own-network.md` |
| `packages/steam/`, the lobby package's slots on Steam, and its fake | `notes/lobby-steam-package.md` |
| Reload/migration runtime, CLI serving and generated layouts | `notes/hot-reload.md` |
| `compiler/lsp/`, `tools/` | `notes/editors.md` |
| Temporary runtime maintenance, generated-C boundary, CPU skinning/pose contracts | `notes/runtime-ownership.md` |
| Input devices, desktop/mobile/web input | `notes/input-devices.md` |
| `extern`, `out List<T>`, C systems and rows/jobs ABI | `notes/c-interop.md` |
| Compiler lexer/parser/checker/codegen, types, evaluation order, functions, containers, diagnostics | `notes/language.md` |
| Random runtime and CPU/GPU rolls | `notes/random.md` |
| Runtime handling game values, safety fixtures and sanitizers | `notes/memory-safety.md` |
| Entity/table/page/heap/list/map/grid/text runtime, queries, snapshots and hashes | `notes/ecs.md` |
| Scheduler analysis, tick threads, parallel loops and temporary handles | `notes/schedule-and-threads.md` |
| Session/net/delta runtime, inputs, commands, lead, migration and verification | `notes/netcode.md` |
| Dedicated server builds, multiple matches, match IDs and authentication/admission | `notes/dedicated-servers.md` |
| Managed WASM hosting, local-build deployment, simulation compatibility, recovery and prepaid billing | `notes/managed-hosting.md` in `Tide-Engine/tide-services` |
| Verified/local event handlers and generated listeners | `notes/verified-and-local-handlers.md` |
| RTC, rooms, UDP/relay transport and peer tests | `notes/webrtc-and-rooms.md` |
| Modding requirements, implementation status and withdrawn decisions | `notes/modding.md` |
| `tide publish`, inactive build service | `notes/build-service.md` in `Tide-Engine/tide-services` |
| Relocated standalone mockup.games repository | `notes/mockup-games.md` |
| `unity/`, `tide unity`, `tide/embed.h`: Unity as an authoring/rendering front end | `notes/unity.md` |
