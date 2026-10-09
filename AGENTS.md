# Tide: Agent Guide

Rules and decisions for anyone, human or AI agent, working on Tide. Only agreed decisions go in this file. Open questions stay out until they're decided.

## Working agreement

- The project owner is the lead programmer and writes most of the code.
- Agents help with specific, scoped tasks. Do what was asked. Don't widen scope, scaffold systems, or refactor nearby code unless asked.
- Agents validate changes locally and merge directly into `dev`. Do not create pull requests or push feature branches unless the owner explicitly requests them.
- If it's unclear whether the owner wants code or discussion, ask.
- **Exception: the AI agent working on a task owns its transpiler work.** The owner decides the language design (what it should do, its syntax and semantics). The agent implements and maintains the transpiler, including its tests and build integration, and proposes designs for the owner to approve. This applies to any AI agent; references to Claude in this guide do not reserve work for a particular agent.
- The contract between the two sides is the C the transpiler generates and the engine runtime functions that code calls. The owner writes the runtime. Any runtime function the generated code needs is agreed on before either side depends on it.

## What Tide is

A networking-first game engine:

- The simulation is deterministic and uses floats, not fixed point, on supported hardware.
- User code and the engine's built-in systems are written in a custom language that compiles to native code.
- An ECS runs systems on multiple threads automatically. Data dependencies come from the ECS and the language, so users never have to think about threads.
- Graphics work is kept to a minimum. Rendering is not the focus.

## Platforms

- The main targets are desktop and consoles.
- Mobile is a target too: Android first, then iOS.
- A phone that goes to the background, or closes, can no longer simulate, so it leaves the match: with host migration on (see Networking), another player's machine takes over the match it hosted. Nothing keeps a phone's match running in the background.
- Touch is input on every platform, not only phones: the web (on phones and desktops) and desktop touchscreens have it.
- The web (WebAssembly, rendering with WebGL) is actively supported and tested, not an afterthought. Changes must keep the web build working.
- Determinism holds on every platform, the web included, so web and desktop players can share one simulation.

## Notes

This file is the rules and the agreed decisions every session needs. What was built, what code relies on, the tests and the measurements are in `notes/`, a file a subsystem. They were this file's sections until 2026-10-08, and moved as they were.

- **Before changing a subsystem, read its notes file, whole.** What its code relies on is there and nowhere else: what has to change together, what a test pins, what was tried and didn't work.
- **Work that lands updates its notes file in the same commit:** what it built, what code relies on, its tests, what isn't built, and any note it made wrong. A line goes in this file only for a rule or an agreed decision that every session needs.
- **Section names still mean what they did.** "See Web builds" in the notes, and "AGENTS.md, Benchmarks" in the repo's comments, name a section of this file as it was. The notes keep those sections as their headings, and the second table says which files each is in.

| Notes | What's in it | Read it before changing |
|---|---|---|
| `notes/rendering.md` | The platform layer's windows, `gpu.h` and its backends (OpenGL, WebGPU), the renderer's own shaders, text and the font, the draw list, the agreed order (Vulkan next), the nine cards of the reshaping | `platform/src/` (`platform.c`, `gpu*`, `gl.h`, `shaders.h`, `font.c`), `platform/desktop/`, `platform/web/gpu_web.c`, what `tide.js` has of WebGL and WebGPU |
| `notes/drawing.md` | `Draw`: one owner, the cameras as matrices, the window and `Screen`, the draw settings (blend, tint, transform, clip, depth, cull, stencil), smooth shapes, meshes, 3D, instancing, C that draws through a `DrawList` | `engine/src/draw*.c`, `primitives.c`, `tide/draw.h`, `Draw`'s signatures in `compiler/src/builtins.c`, the renderer's steps |
| `notes/shaders.md` | `frag` and `vert`: `this`, `out`, a draw's values, `Math` and noise on the GPU, lists, arrays, maps and grids on the GPU | `compiler/src/shader.c`, `compiler/tests/shaders/`, `platform/tests/shaders.*`, `engine/src/mesh.c` |
| `notes/textures.md` | `Wrap`, sampling a declaration says, `Texture` as the one type, mipmaps, where pixels are kept, premultiplied alpha, `[Raw]` | Textures in `engine/src/draw.c`, `mesh.c` and `platform.c`, `Sample` in `shader.c`, samplers in a backend |
| `notes/render-targets.md` | Render targets: their sizes and scale, formats and depth reads, several colors and float images, samples, cubemaps | `tide_render_target_*` and target ids in `engine/src/draw.c`, targets in `gpu.h` and its backends |
| `notes/audio.md` | `tide/audio.h`, the mixer, `Clip`, `Sound`, buses, `audio` declarations, the devices, Ogg Vorbis, the microphone | `engine/src/audio.c`, `vorbis.c`, `wav.c`, `platform/src/mixer.c`, `audio_*.c`, each platform's `audio.c` |
| `notes/gui.md` | The GUI: what it hides from the game, keys kept until they're let go, fields that say `entered`, controls, focus, the theme, claims | `engine/src/gui.c`, `tide/gui.h` |
| `notes/standard-host.md` | `tide/run.h` and `tide/host.h`, runs with no window (`--capture`, `--frames`, `--script`, and `--players` for several players at once), what `TIDE_HIDDEN` refuses and silences, the stats' drawer, `Debug`, debug views and the profiler | `platform/include/tide/host.h` and `run.h`, `platform/src/script.c`, `players.c`, `png.c`, `engine/src/stats.c`, `profile.c`, `profiler.c` |
| `notes/assets-and-files.md` | Local files, `asset` declarations and the pack, `Texture` assets, `Mesh<V>` assets and the glTF and OBJ reader, assets as defaults, `grid.Decode` and `Web.Fetch` | `compiler/src/asset_files.c`, `mesh_files.c`, `assets.c`, `engine/src/pack*.c`, `png_read.c`, `platform/src/fetch.c`, files in `tide.js` |
| `notes/testing.md` | Node's crash on Windows before 24.16, browsers muted, the demo's smoke test, the `gpu` and `webgpu` labels and which runner has neither | A test that needs a window, a GPU or a browser, and the runners under `cmake/` |
| `notes/web.md` | Web builds: `tide.js`, what a page's address says (`host`, `join`, `lag`, `wait`, `embedded`), threads and the pool on the web, hidden pages, served tests | `platform/web/`, `cmake/wasi-toolchain.cmake`, `run_wasi.mjs`, `WebTest.cmake`, `run_web_served_test.mjs`, `platform/src/jobs.c` |
| `notes/android-and-mingw.md` | The MinGW and Android builds, apps, `tide_apk`, bundles and signing | `platform/android/`, `compiler/cli/apk.c`, `rsa.c`, `sign.c`, `cmake/android-toolchain.cmake`, `mingw-toolchain.cmake`, `run_android.mjs` |
| `notes/benchmarks.md` | `bench/`: the games, the harness, saved runs, Tide against C | `bench/` |
| `notes/tide-programs.md` | `tide_add_game`, and the generated header: the API between a game and its host | `cmake/Tide.cmake`, `run_tidec.cmake`, what `compiler/src/codegen.c` writes into a game's header |
| `notes/docs-site.md` | The docs site: its pages and folders, SEO, hosting, live examples, the samples | `docs/` but `spec.md`, `.github/workflows/docs.yml` |
| `notes/physics-games.md` | Towers and Sumo: their folders, seats, rules, look, shadows, tests and what they cost | `examples/towers/`, `examples/sumo/` |
| `notes/deadbell.md` | The private Deadbell co-op roguelike prototype, its run, crowds, presentation and validation | `prototypes/deadbell/` |
| `notes/cli-and-packages.md` | `tide`: its commands, the built-in clang and tide's size, packages and `tide.packages`, a game's C and libraries, what the package holds, `tide build --android` | `compiler/cli/`, `compiler/lsp/packages.c`, `cmake/LLVM.cmake`, `cmake/package.cmake` |
| `notes/releases-and-ci.md` | Installing and upgrading, releases through the site, the Build workflow, its runners, nightlies | `.github/`, `install.ps1`, `install.sh`, `docs/releases.js`, `compiler/cli/upgrade.c`, `install.c`, `release.c` |
| `notes/box2d.md` | The Box2D package: its state in the match, its stage, sleep, sensors, images in parts, parts, queries, tuning, pins and costs | `packages/box2d/` |
| `notes/box3d.md` | The Box3D package: resident state, what differs from Box2D's, pins and costs | `packages/box3d/`, `engine/src/resident.c` |
| `notes/rooms-package.md` | The menu that comes with tide | `packages/rooms/` |
| `notes/hot-reload.md` | Hot reloading: libraries, carrying state over by name, the web's | `platform/src/reload.c`, `migrate.c`, `tide/layout.h`, `compiler/cli/serve.c`, the layout `codegen.c` describes |
| `notes/editors.md` | `tidels` (reading ahead, code lenses, assets in the editor) and the editors' integrations | `compiler/lsp/`, `tools/` |
| `notes/runtime-ownership.md` | The runtime Claude wrote and maintains for now, file by file, with what generated code calls of each | A runtime function generated code calls |
| `notes/input-devices.md` | The platform layer's input: keys by position, typed text, touch, the pointer, the cursor's lock, phones' keyboards, logical pixels | `engine/src/devices.c`, a window's input in `platform/desktop/`, `platform/android/` and `tide.js` |
| `notes/c-interop.md` | `out List<T>`, jobs, and systems written in C (`tide/rows.h`) | `engine/src/list.c`'s `tide_list_out_*`, `jobs.c`, `rows.c`, what tidec generates for `extern` |
| `notes/language.md` | Language features as tidec builds them: evaluation order, `++` as a value, settings blocks, name lookup, namespace blocks, defaults, named arguments and overloads, `out`, `World` in functions, `virtual` and `override`, types as trees, arrays, containers that nest, diagnostics, `#if` symbols | `compiler/src/` (the lexer, the parser, `check.c`, `assigned.c`, `types.c`, `codegen.c`) |
| `notes/random.md` | Random numbers: rolls on the type, `Random.seed`, the value, rolls on the GPU | `tide/random.h`, rolls in `check.c`, `codegen.c` and `shader.c` |
| `notes/memory-safety.md` | What "no Tide code can corrupt memory" rests on, its tests and the `sanitize` preset | Runtime code that takes a game's values, `compiler/tests/safety/` |
| `notes/ecs.md` | Queries, archetypes, sparse components, the heap, lists, grids, the scratch area, tasks, local fields, pages and snapshots, lanes and row loops, hashing | `engine/src/table.c`, `page.c`, `heap.c`, `list.c`, `map.c`, `grid.c`, `text.c`, `entity.c`, `attached.c`, the rows `codegen.c` writes |
| `notes/schedule-and-threads.md` | When systems conflict, the tick on threads, parallel loops, temporary handles, the schedule's analysis | `compiler/src/parallel.c`, `engine/src/jobs.c`, `platform/src/jobs.c`, the loops in `grid.c` |
| `notes/netcode.md` | Sessions: how a match is sent, players, how early inputs go, input delay, the guess limit, pings, host migration, commands and their channels, `[Position]` and `[Rotation]` | `engine/src/session.c`, `net.c`, `delta.c`, commands and inputs in `codegen.c` |
| `notes/verified-and-local-handlers.md` | `verified`, local handlers of the match's events, and the contract for what they hear | `engine/src/events.c`, listeners and `verified` in `check.c` and `codegen.c` |
| `notes/webrtc-and-rooms.md` | Desktop WebRTC (ICE, DTLS, SCTP), and the rooms' tests on desktop and the web | `platform/src/rtc/`, `rooms.c`, `relay/`, `platform/tests/rooms*`, `rtc*` |
| `notes/build-service.md` | The build service for mods, on hold | `builds/`, `compiler/cli/publish.c` |

Where each section of this file went, by the name it had:

| Section as it was | Now in |
|---|---|
| Rendering and platform | Here for its rules; `rendering`, `drawing`, `shaders`, `textures`, `render-targets`, `audio`, `gui`, `standard-host` |
| Tech stack | Here; `assets-and-files` |
| Building | Here, with Testing and merging and Cross-platform determinism tests; `testing` |
| Web builds | `web`; the rooms' tests in `webrtc-and-rooms` |
| MinGW builds, Android builds | `android-and-mingw` |
| Benchmarks | `benchmarks` |
| Tide programs | `tide-programs` |
| Docs site | `docs-site` |
| The physics games | `physics-games` |
| Packaging and releases | `cli-and-packages`, `releases-and-ci` |
| Box2D, Box3D, Rooms, Hot reloading | `box2d`, `box3d`, `rooms-package`, `hot-reload` |
| Layout | Here; `editors` |
| Runtime written by Claude for now | `runtime-ownership`, `input-devices` |
| Language | Here for its principles; `language`, `c-interop`, `random`, `memory-safety`, `standard-host` (`Debug`, debug views) |
| ECS and simulation state | Here for its rules; `ecs` |
| Networking | Here for its decisions; `netcode`, `verified-and-local-handlers`, `webrtc-and-rooms` |
| Modding | Here; The build service in `build-service` |
| Determinism | Here; Systems that touch the same data also in `schedule-and-threads` |

## Rendering and platform

- Tide code runs on the GPU on the backends there are (agreed, ahead of Vulkan): `frag` and `vert` (see `docs/spec.md`, Views and drawing), which tidec compiles to GLSL (OpenGL 3.3, OpenGL ES 3, WebGL 2) and WGSL (WebGPU). What the GPU computes is for what's seen, never the match: GPUs don't give the same bits everywhere.
- Rendering stays disconnected from the simulation, so it can be replaced later (for example for consoles):
  - The simulation never includes a platform header.
  - The view reads the world and draws it. It never writes simulation state.
  - The platform layer's only link to the simulation is filling `Devices`.
  - Game code never shares a source file with the system's headers or GLFW's: generated headers name types after the game's components (`Transform`), and those define many of the same names. Hosts include `tide/platform.h`, which includes neither, and `tide_platform` keeps them private.
- That nothing an agent runs shows on the owner's screen doesn't rest on the agent remembering: `TIDE_HIDDEN` in the environment, set to anything but nothing or 0, says nobody watches a run (`tide_hidden`, `tide/page.h`), and then no Tide program shows a window or a dialog, or opens an audio device: nobody listens to such a run either (agreed 2026-10-08). Desktop only.
  - Agents' shells have it. Claude Code sessions get it from `.claude/settings.json` (`env`), the one file of `.claude/` that's checked in, so every checkout and worktree has it. A Codex session exports it before anything else it runs: `export TIDE_HIDDEN=1`, or in PowerShell `$env:TIDE_HIDDEN = '1'`. It's never set where it would reach the owner's own runs: not the user's environment, not the editors' run configurations. `tide run`, CTest and the tests' scripts pass the environment on as it is.
  - Two rules stand beside it, as a program built before the variable doesn't know it: an agent runs only programs built in its own checkout (another checkout's `build/` can be older than the flags it's given, and opens a window), and never runs a game without `--capture`, `--frames` or `--script`.
  - A browser doesn't read the variable, and a headless one plays through the machine's device once its page is sent a key: whatever starts one, a test's runner or an agent's own script, gives Chrome and Edge `--mute-audio` (`notes/testing.md`).
  - What a program does with the variable (what's refused, dialogs, and sound) and its tests are in `notes/standard-host.md`.

## Tech stack

- The engine is written in C and compiled with clang on every platform.
- The custom language transpiles to C.
- User code and engine code are compiled and optimized together, so calls between them cost nothing.

## Building

Requires CMake 3.25+, Ninja, and clang. The build finds clang automatically, checking `$LLVM_ROOT/bin`, then a standalone LLVM install, then `PATH`, then Visual Studio's bundled clang. This works with any CMake invocation, not just the presets.

- Configure, build and test in one step: `cmake --workflow --preset debug`
- Build only: `cmake --build --preset debug`
- Run tests: `ctest --preset debug`, or run `build/debug/bin/tide_tests [name-filter]` directly
- Run the sandbox: `build/debug/bin/sandbox`
- Run the demo: `build/debug/bin/demo`, or open `build/web-release/bin/demo.html` in a browser after a web build. Two players: `demo --host` in one window and `demo --connect 127.0.0.1` in another (or another machine's address).
- Run a benchmark: `build/release/bin/bench_sand` (see Benchmarks).

There are two presets for everyday work. `debug` has no optimization. `release` is optimized and keeps debug info for profiling. Web, MinGW and Android builds and the package have their own (`notes/web.md`, `notes/android-and-mingw.md`, `notes/cli-and-packages.md`). `sanitize` is the whole build under AddressSanitizer and UndefinedBehaviorSanitizer, on Linux and macOS (see Language: no Tide code can corrupt memory).

Warnings are errors, wherever they come from, and one that's already there gets fixed, never left alone:

- The compiler's: our C builds with `-Werror` (`cmake/TideFlags.cmake`). Third-party code (GLFW, stb_truetype, LLVM's headers) is built with its warnings off.
  - It's strict C17 (`-std=c17`), where Linux's C library declares only C's own functions unless a file asks for more: one that calls POSIX's (`popen`, `readlink`, `getaddrinfo`, `nanosleep`) defines `_DEFAULT_SOURCE` before its first include, on platforms that aren't Windows or Apple's (the top of `platform/src/rtc/net.c`). Windows and macOS declare them regardless, so only a Linux build says one is missing, as an undeclared function: a file that starts calling one gets the define with it.
- tidec's, in the games `tide_add_game` builds (`cmake/run_tidec.cmake`). A test of what tidec generates for a program it warns about lists those warnings after `WARNINGS`, and the build checks they're there.
- The browser's, in web tests: test pages set `Tide.checkGL`, so a GL error stops the program and names the call (`platform/web/tide.js`), where WebGL would only warn in the console. It checks every call, which is too slow for pages that ship. WebGPU's errors (a shader that's wrong, a call it refuses) stop a test page the same way; pages that ship print them.

Every native build also copies the language server to `build/tools/tidels`, the fixed path editors run: the VS Code extension and the JetBrains plugin use it when the open folder is this repo and the editor trusts it (see `tools/`). On Windows, a build replaces the server while an editor still runs the old one: the running file is renamed aside and deleted by a later build.

A desktop build's first configure downloads GLFW, pinned (see `cmake/GLFW.cmake`); on Linux it builds for X11, whose development headers it needs (libx11, libxrandr, libxinerama, libxcursor, libxi), and loads X11 and OpenGL at run time, so games link neither. Web and Android builds download nothing for the platform layer. Configure with `-DTIDE_PLATFORM=OFF` to build without the platform layer and what needs it (the demo, the sandbox and the benchmarks), for example offline.

### Testing and merging

Tests are for finding what a change broke, not a ritual before every push. With several sessions working at once, `dev` moves while suites run, and running everything again after each merge takes hours for nothing (agreed).

- A change is tested on this machine once, before it's merged, with what it can reach:
  - The compiler, the engine, the platform layer or the build: the native suite (`ctest --preset debug`) and the web suite (`ctest --preset web-debug`). Everything depends on them.
  - A game of the repo (`examples/`, `demo/`, `sandbox/`, a benchmark's): its own targets and tests, native and web.
  - `docs/` alone: the site's build (`npm run build` in `docs/`). `relay/` alone: `node --test relay/relay.test.mjs`. `builds/` alone: `node --test builds/builds.test.mjs`, and when the builder or what it runs changed, `builds/page.test.mjs` with a tide that builds web games (see Modding). `tools/` alone: that tool's tests. A text file nothing builds, like this one: nothing.
- Android isn't tested on this machine. Its tests need the emulator, and they're CI's job, as Linux, macOS and MinGW are: nobody boots the emulator to merge. The exception is work on Android itself, when the owner asks for it.
- Merging is: fetch, merge `origin/dev` into the branch, push `HEAD:dev`. What's tested again after that merge depends on what came in (`git diff --stat ORIG_HEAD HEAD`), and it's never "everything again" by default:
  - It merged cleanly and touches nothing the change touches or leans on: nothing is run again. Push.
  - It changed something the change is built on (a header it includes, tidec under a game, a function it calls): build what the change touches, run its own tests, push.
  - It touches the same files or the same subsystem, or there were conflicts: run the tests for that overlap (`ctest --preset debug -R <pattern>`, or a label), on native or the web as the overlap calls for. Both suites whole only when the overlap is that wide.
  - One round. When `dev` moved again meanwhile and what came in is unrelated again, merge and push; don't start over.
- CI and the docs workflow run only when requested manually (the owner, 2026-10-07). Pushes and pull requests start nothing, and agents do not dispatch workflows automatically after pushing. The local test obligations above still apply. Linux, macOS, MinGW, the sanitizers and Android are tested by a requested run of the Build workflow (`gh workflow run build.yml --ref dev`), which runs every suite on every platform and, on `dev`, publishes a nightly once they all passed (see Packaging and releases), and builds the docs site beside them, deploying it from `dev` (`notes/docs-site.md`). A change that leans on what this machine can't run (Linux's, macOS's or MinGW's own code, the sanitizers) reports that a run is needed; it is dispatched when the owner asks. A session asked to run CI checks its run when it has finished (`gh run list --branch dev`): a package that doesn't build publishes nothing, and whoever's change broke it fixes it there first, ahead of anything else.

### Cross-platform determinism tests

- `tests/test_crossplatform.c` and `compiler/tests/e2e/crossplatform` hash the exact bits of math results and of a full simulation. The hashes must be identical on every platform and in every configuration.
- If math or generated code changes the results on purpose, update the expected hashes from one platform, then confirm every other platform and configuration agrees.
- Random numbers have hashes of their own: `crossplatform_random` in `tests/test_crossplatform.c` (every function of `tide/random.h`) and `random_crossplatform_match` in `compiler/tests/e2e/random` (a match that rolls, from a seed).
- Test code must not depend on C's unspecified argument evaluation order: clang goes right to left on Windows and left to right on WebAssembly. Draw random inputs into locals first.

## Packaging and releases

Users get Tide as the `tide` command, not this repo: see README.md.

- Versions come from conventional commits (`.releaserc.json`): `fix:` is a patch, `feat:` a minor version. While the version starts with 0, breaking changes (`feat!:`) are minor versions too, and we avoid them until 1.0 anyway.

## Layout

- `engine/`: the engine library (`tide`). Public headers go in `engine/include/tide/`, sources in `engine/src/`. New `.c` files are picked up automatically.
- `compiler/`: `tidec`, the Tide transpiler (owned by Claude). `compiler/src/asset_files.c` resolves what `asset` declarations name (files, globs, URLs with their cache), `mesh_files.c` reads the glTF and OBJ files a `Mesh<V>` asset names, and `folders.c` beside it lists folders and files for the compiler, the language server and tide. `compiler/tests/e2e/` holds programs compiled and run as tests. `compiler/tests/errors/` holds programs that must fail with the message on their first line (and say each `// expect:` line right after it, an error's note), and `compiler/tests/warnings/` programs that must compile with as many warnings as their first line says, and the texts on their `// expect:` lines (warnings' and notes'). `compiler/tests/shaders/` pins the GLSL and WGSL tidec writes for a program's frags and verts (`tidec --shaders`), as `compiler/tests/schedule/` pins a schedule. A test's packages are folders named as the packages are: `errors/<name>/<Package>/*.tide`, and `<name>/<Package>/*.tide` beside a schedule's, a shaders' or a warnings' `<name>.tide`. `compiler/tests/scale/` makes up a big game of many namespaces at test time, which tidec has to compile within a generous time: codegen names every declaration once (`name_program`), as a compile that made each name again at every use grew with the game's size squared. tidec reads files with Windows line endings (CRLF) exactly as LF ones: `e2e/crlf.tide` and `errors/*_crlf.tide` test it, and `.gitattributes` keeps them CRLF on every checkout.
- `compiler/cli/`: `tide`, the command users run (owned by Claude; see Packaging and releases). `compiler/cli/tests/` tests what it can without a network or a compiler: versions and releases, upgrades (see Packaging and releases), packages, which platform a library is for, apps and bundles and their RSA signatures, zips, `tide editors`, and `tide publish` against the build service run on this machine (`publish_test.mjs`, `tide_cli_publish`: see Modding). `cpp_test.cmake` there builds games with C++ and checks what tide says when one can't build, so it needs a tide that builds games: CI runs it on the package it made (`cmake -DTIDE=<tide> -DWORK=<folder> -P compiler/cli/tests/cpp_test.cmake`).
- `compiler/lsp/` (`tidels`, the Tide language server) and `tools/` (the VS Code extension, the JetBrains plugin and the grammar they share), both owned by Claude: `notes/editors.md`.
- `docs/`: the docs site (see Docs site). `docs/spec.md` is the language spec; `guide/`, `game/`, `physics/`, `multiplayer/`, `language/`, `tools/` and `c/` are the pages for users, written from it, from this file and from `notes/` (see Docs site for what goes where).
- `platform/`: the platform layer (`tide_platform`): window, frame loop, input devices and the renderer (see Rendering and platform), and rooms (`src/rooms.c`, with desktop WebRTC in `src/rtc/`), and runs with no window (`src/script.c`, `src/png.c`). Public headers in `platform/include/tide/`: `platform.h`, and the hosts' `run.h`, `host.h` and `migrate.h`. `desktop/`, `web/` and `android/` hold what's each one's own: its window (`src/window.h`) and, on the web, the page (`tide.js`) and the WebGPU backend; `font/` holds the font text is drawn in, and `third_party/` what's vendored (`stb_truetype.h`).
- `demo/`: a small game on the platform layer. `demo.tide` is the simulation and the views that draw it, and `main.c` is the host. It builds as `demo.html` on the web.
- `sandbox/`: the owner's experiments: a game with no C, built by `tide_add_game`.
- `examples/`: whole games, a folder each as `tide run` builds it, which the docs site shows: `duel` (see Docs site), and the physics games, `towers` and `sumo`, each with a `tide.packages` and its test in `test/` (see The physics games).
- `bench/`: benchmarks, games played by bots on one machine (see Benchmarks).
- `packages/`: the packages that come with tide (see Packaging and releases), a folder each, named as a game's `tide.packages` names it, with a `tide.packages` of its own that says `package Name`. They install into the package as they are, in `src/packages/`, but for what's this repo's own: `packages/CMakeLists.txt`, which builds their tests the way tide would build the package, and each one's `test/`. `box2d` and `box3d` are the two so far (see Box2D and Box3D, under Packaging and releases).
- `relay/`: the relay, which introduces players in rooms (see Networking): Node, with a WebSocket server of its own and no dependencies. It runs on Fly.io as `purrengine-relay`, one machine in `fra` (`relay/fly.toml`; deploy with `fly deploy --ha=false` from `relay/`). Games reach it as `wss://relay.tide-engine.dev`, a CNAME to the Fly app in the domain's DNS on Cloudflare, so it can move without them changing: `platform/src/rooms.c` connects there unless `TIDE_RELAY` says otherwise, and `platform/web/tide.js` unless the page's `Tide.relay` does. All of a relay's time comes from its `clock` (`createRelay`'s option: `now` and `wait`), never from `Date.now()` or a timer of its own: the system's is a clock that only goes forward, as the time of day gets set back (WSL's by seconds at a time under load, which is how a room's hold outlasted a test's sleep), and its tests (`relay/relay.test.mjs`) bring one that only moves when a test says (`clock.pass`), with what the relay does when its time is up done inside that call. So no test there sleeps until a time must have passed: what a test waits for, it asks the relay's own state about (`until`).
- `builds/`: the build service, which builds a game with mods (see Modding): Node, with no dependencies, written as the relay is. It's on hold, and nothing of it is deployed (see Modding). Deployed, it's on Fly.io as `tide-builds`, one machine in `fra` that stops when idle, with a volume (`builds/fly.toml`; deploy with `fly deploy --ha=false` from `builds/`), and makes a machine for each build in a second app, `tide-builders` (`builds/fly.builder.toml`); it's reached as `https://builds.tide-engine.dev`, a CNAME to the Fly app in the domain's DNS on Cloudflare, as the relay's name is, which `tide publish` sends to unless `TIDE_BUILDS_URL` says otherwise.
- `tests/`: tests built on the harness in `tests/tide_test.h`. New test files are picked up automatically.
- `.github/workflows/`, `.releaserc.json`, `install.ps1`, `install.sh`: releases and installing (see Packaging and releases), and the docs site.
- `cmake/`: shared compiler flags (`TideFlags.cmake`), the package (`package.cmake`), the file that locates clang (`clang-toolchain.cmake`), the web, MinGW and Android toolchains (`wasi-toolchain.cmake`, `mingw-toolchain.cmake`, `android-toolchain.cmake`), tide's built-in clang (`LLVM.cmake`), `tide_add_game` (`Tide.cmake`), the GLFW download (`GLFW.cmake`), files as C arrays (`embed.cmake`), and `tide_add_web_test` (`WebTest.cmake`), which runs a web page in headless Chrome or Edge as a test, and with `WEBGPU`, again on WebGPU.
- `notes/`: what was built and what code relies on, a file a subsystem (see Notes).

### Runtime written by Claude for now

Who wrote and who maintains what is said here, never in the files: users read the package's headers and sources, so their comments are about the code, and don't point at this file, which the package doesn't have.

- The list, file by file, with what generated code calls of each: `notes/runtime-ownership.md`.

## Language

The language shares the engine's name, Tide. Its syntax and semantics are specified in `docs/spec.md`. The docs site explains it to users: a change to the language, `tide` or the editors updates the site's pages too (the folders of `docs/` beside `spec.md`).

- Derive as much as possible from the language.
- It should be as explicit as our needs require, while staying friendly.
- Dependency trees are static: known at compile time, not discovered at runtime.
- Named ordering phases are ordinary enum members. `[Stage(Stages.Objects[, offset])]` assigns a system, view or event handler to one phase; the offset is a constant int, 0 when left out, and orders only within that phase. The enum's declaration order decides the phases' order, not its members' values. `[Before]` and `[After]` take whole phases as well as declarations. Systems, views and each event's handlers are scheduled apart; a system in a later phase waits for every system of the earlier ones, even with no data between them, and empty phases keep their place.
- Explicit is the default. Systems declare which components they read and write in their signatures.
- Ordinary functions and methods can take explicit singleton parameters, with the same read-only copy and `mut` caller-variable rules as components. They gain no implicit singleton lookup, ECS binding or world access. Caller capabilities remain decisive through helper chains: match systems and handlers write match state, views read the match and write local state, and `Sample` stays read-only; helper reads and writes remain part of access analysis and scheduling. Only ordinary function helpers carry a bound component's or singleton's local fields; plain copies and methods have none. Match code can't call local-field helpers, local code can't call match-writing helpers, and a helper can't do both through the same parameter. Engine-owned singleton instances stay read-only, and passing `Session` grants no session operations. Singleton returns stay unsupported, as do the existing unsupported copies and resource arguments; this adds no extern ABI. World access is a `World` parameter's, which a function's caller passes its own (`notes/language.md`).
- Functions and methods send events, spawn and change entities as the code that calls them could (agreed 2026-10-08): `Send`, `Spawn`, `Add`, `Remove` and `Destroy` are written in one as in a system, with no parameter for it, and it inherits its caller's ability. A function is never an error by itself. A call of one is, only from a place that couldn't do the same thing itself: a view calling one that sends a match event, the match calling one that sends a command. It follows helper chains as the capability rules above do, and it's the one thing a function does to a world without a `World` parameter (`notes/language.md`).
- A system's access (which components it reads or writes) is declared separately from its filters (which components an entity must have or lack). Filters don't create data dependencies.
- The language is built around the ECS. It should use syntax sugar to hide the ECS's pain points.
- When nothing else decides a convention (names, axes, units, orderings), follow Unity: Unity.Mathematics for math, the Input System for input. Tide's own rules, such as PascalCase methods, still win.
- Games call C through `extern` functions, and C is trusted: what it does (determinism, state outside the world, thread safety) is the game's responsibility. The language adds no markers or checks for it, and the compiler takes a C call as touching nothing it tracks. tidec declares each one in the generated `.c` from its signature, never in the header (which would change `tide_game`'s hash) and never by including the library's header. Calls to C, and to functions and methods that call it (`decl.calls_c`), are side effects that codegen keeps in source order, as it does spawns. C makes no system wait, and systems that call it split across threads like any other: what C does on several threads at once is the game's to get right.
- Tide brings no C++ runtime (the standard library, exceptions, RTTI), and that stays so: not its headers, not its libraries, not `operator new`. A game's `.cpp` files compile as the language alone (`cpp_flags` in `compiler/cli/build.c`, and `TIDE_CPP_FLAGS` for `tide_add_game`: C++20, `-nostdinc++ -fno-exceptions -fno-rtti -fno-threadsafe-statics`, the same on every platform), behind `extern "C"`, and link as C does; code that wants `new`, `delete` or `__cxa_pure_virtual` defines them. C++ that needs the runtime comes as a prebuilt library behind a C API: a shared one, which has its runtime with it, or a static one built to need none, which is the only kind the web has. A build that fails for want of the runtime says so, and what to write (`explain_cpp_compile`, `explain_cpp_link`): the compiler's and the linker's own words go to the terminal as they are, and a failed one runs once more for them as text.
- Error messages guide the user to the fix: say what's wrong and, whenever it's knowable, what to write instead (a note with the right usage, or "did you mean ..." for a misspelled name).
- Errors are values, never exceptions: a function that can fail says so (`fails`), and the code that calls it handles the error right there (`??`, `is`, `!`, `try`); see `docs/spec.md`, Errors. What can't fail stays forgiving (indexing, arithmetic, text), and failures that come later stay events, like `Disconnected`.
- Async functions and handlers (`async`, `await`, as C#'s, with no `Task<T>`) run as tasks, in the match or in local code: see `docs/spec.md`, Tasks. A task belongs to its world like a component: the match's are in its snapshots, hashes and packed bytes. Match code only waits for what's the same on every machine; a service's answer reaches the match through input.
- **No Tide code can corrupt memory.** It's the language's promise to every game (the owner, 2026-10-05: "a Tide issue, not a mod issue"), which mods will rest on, and it's tested as one. Tide alone, that is: a game's C is trusted, as ever. What it rests on, and what code that touches a game's values has to keep:
  - **Nothing a game says is trusted to be a place.** An index into a list, a grid, a map or text is checked where it's used, against what's there then: past it, a read gives the zero value and a write does nothing (`tide_list_at`, `tide_grid_peek`, `tide_map_find`, `tide_text_*`), through the caches too, which only hold while their heap hasn't moved and below a list's count as it is now. An entity's handle is looked up by its generation each time (`tide_entity_alive`), so one that's stale, default or temporary is nobody's. Counts a game picks are added and multiplied 64 bits wide before anything is allocated, and one past what there's memory for stops the program with the engine's own message (`tide_out_of_memory`).
  - **Ints never do what C leaves undefined.** Generated code goes through `tide/math.h` for everything C doesn't define: overflow wraps, division and remainder by zero give 0, shifts use their count's low bits, and a float becomes an int through `tide_i_from_f`, which saturates and makes NaN 0. Runtime code that takes a game's numbers does the same: a float is kept to what an int holds before it's made one (`clamp_pixel` and `whole_pixels` in `platform/src/platform.c`, `tide_font_pixels`, the int slider), and what's summed over a game's data is 64 bits wide (a line's width in pixels).
  - The rest (places at the ints' edge, a stack that runs out, structs in room of a fixed size, values too big to copy, the tests and the `sanitize` preset) is in `notes/memory-safety.md`: read it before changing runtime code that takes a game's values.
- No backward compatibility yet: rename and change freely. Old spellings aren't supported; at most, an error points to the new one. Code that has to build with several versions of tide, like packages', picks between them with `#if TIDE_0_3_OR_NEWER` (see `docs/spec.md`, Conditional compilation). There are no symbols for platforms: every machine of a match runs the same code.

## ECS and simulation state

- The transpiler generates C specific to each game: component structs, archetype storage, per-system dispatch, and the tick. There is no runtime type registry.
- Singletons hold state for the whole world, such as time, RNG and game rules (other ECSs call them resources). There is one of each per world, stored inside it, and systems declare them the same way as components.
- The world is always passed as a pointer, never stored in a global. Several worlds can exist at once, for example predicted and verified copies, several matches on one server, or snapshots.
- The world owns all simulation memory. Components hold offsets into world memory, never pointers. The allocator's state lives in the world too, so a snapshot captures everything.
- Local state (menus, settings, view state such as particles) belongs to one machine and lives outside every world. It's never sent, rolled back or hashed. The compiler keeps match code from reading it and local code from changing the match (see `docs/spec.md`).
- Generated types (components, singletons, the input and structs) have no padding the compiler adds: tidec writes it out as members, which every value sets to zero, and the generated header checks each type's size (`_Static_assert`). Their bytes only depend on their fields, so snapshots and state hashes can compare memory directly. Input from outside gets its padding cleared along with its other repairs.

## Performance

- Performance is a top priority. Weigh design choices by their runtime cost.
- Runtime, compilation and rendering speed are requirements. Compare equivalent work with the best practical handwritten implementation; an unexplained order-of-magnitude gap is a performance bug to investigate and fix. Runtime comparisons keep the same determinism guarantees. Fast iteration matters alongside the speed of the shipped game.
- Internal performance changes are authorized when they preserve observable language behavior, including deterministic results, ordering, snapshots and hot reload. Storage, generated code, scheduling, batching and caching are implementation choices; changes to the language's expectations remain the owner's decision.
- Flexible features are fine even if they cost more, as long as the common, fixed path stays fast.
- Friction between engine code and user code should be minimal or non-existent.
- Snapshotting and restoring the full simulation state must be cheap. Rollback depends on it.

## Networking

- Devices use explicit read-only parameters everywhere they can be read: `Sample(Devices devices)`, views, systems, match handlers and helper functions. Bare `Devices.keyboard` is an error. Sample and views get this machine's devices; match systems and handlers get the entity owner's tick input, or the server's for unowned entities and code that runs once. Only fields match code reads are sent. Frame text and window positions keep their existing restrictions.
- UI actions enter the match as typed `command Name { ... }` payloads, sent by local code with `Send(...)` or `entity.Send(...)`. A command has one void handler, `command(Name value, PlayerID sender) Handler(...)`. The engine supplies the sender from the connection; ordinary guard clauses decide which actions to take. There is no acceptance method, return status or transaction. Match events remain simulation notifications.
- Commands are bounded copied wire payloads, queued independently of tick input and resent until a verified world confirms their per-player sequence. The server chooses the tick and order; handlers join the existing deferred event queue at tick end. Sequence state is part of snapshots, hashes and host migration. Initial limits are 1024 packed bytes per command, 32 pending per player and 16 scheduled per tick. Game-specific validation belongs to the handler.
- Rollback netcode with full server authority. The server is the source of truth. Clients predict ahead and roll back when the server disagrees.
- Single-player is a match too: the machine runs the server itself and connects to it through a loopback transport, so there's no separate offline path. The engine assumes nothing about what games do with matches (no built-in pause): it gives them the tools.
- There's one kind of match, so no special case for playing alone: a match starts closed, and whether other machines can join it is a switch on it (`Session.Open` and `Session.Close`), not another way to start one. Single-player is a match nobody else was let into.
- Clients have no input delay: they run ahead of the server by about half the round trip plus a small margin, so their inputs arrive in time, and only other players' inputs are guessed. The server says how early their inputs arrive, and clients keep them as early as they have to be by it (`notes/netcode.md`). A client that sends none (a game without an input) ignores it and keeps the lead it joined with.
- Matches go over WebRTC data channels, unordered and unreliable, on the web and on desktop alike, so web and desktop players meet in the same rooms: they're the only way a browser can reach another player's machine directly, so browsers host matches as well as join them. Desktop hosts also take players on a UDP port, our own thin layer, which `Session.Connect` reaches by address. The protocol above the transport stays the same.
- Direct connections come first, since they cost the relay nothing. Players find each other in rooms, by a code the host picks itself (`Session.room`), so there's nothing to wait for; a code already taken gets another before anyone could read it. The relay we host (`relay/`) introduces players in a room, passing along what WebRTC needs to connect them, and nothing else goes through it. Players whose networks can't connect directly go through Cloudflare's TURN: the relay makes short-lived credentials with the key in its Fly secrets, and hands them to players. Dedicated servers stay.
- Sync relies on determinism as much as possible. The main thing sent over the network is inputs.
- Inputs come from clients, so they're attack points. The engine is forgiving with them and makes bad values hard to turn into broken math or a broken game: NaN and infinite floats in an input become the field's default before the game's `Sanitize` runs, and math functions avoid spreading NaN where they can (`Math.Clamp` always returns a value in range; `Math.Min` and `Math.Max` with one NaN return the other argument; `Math.Normalize` gives zero for a vector too short to normalize, and the identity for a quaternion, so there's no `NormalizeSafe`: `tide_normalize_*` in `tide/math.h`).
- State corrections are supported. Divergence is detected through state hashing.
- A world is hashed and checked every tick, in every build (decided 2026-10-07, when the question was whether the check should be a debug build's alone, the language being deterministic): "i would rather check it every tick or not check it at all; so if we are to check, we should do it on tick. its more predictable, reliable and its the fastest to correct or notify of error." Never every Nth tick, which was proposed and turned down: it pays the same cost as a spike. The language keeps Tide code deterministic, not the C a game calls, which is where both divergences found so far were (Box3D's signed zeros and its lock on the web), and the check is what turns one from players silently seeing different matches into a correction within a tick. So what it costs is made small, not seldom (see ECS and simulation state: the wide page hash, and hashing on a few threads).
- Clients can be denied specific state through a visibility system, for example other players' cards in a poker game. Not built yet: a scene says who's in it (`SceneVisibility.Private`, `Scene.AddPlayer`), but every player is still sent the whole world.
- Visibility is dynamic. The server decides at runtime what each player can see, through an API.
- Each client tracks a **verified tick**. It is the latest tick for which any state the client lacked but should have has arrived, and any divergence has been corrected by the server. Ticks after it are predicted and may be wrong for a while.
- Secrets such as RNG seeds are ordinary state behind visibility, for example an entity the client can't see. They need no special mechanism.
- Clients don't have to simulate the whole world. LOD borders and culling are resolved through state sync. Not built yet: every client ticks the whole world.
- Hosting (not built yet): Tide will host games for people, and hosted matches will run as WebAssembly in a sandbox (a runtime like wasmtime), with a time budget per tick. Owning the language doesn't make hosted code safe by itself: games can call C, and even pure Tide can loop forever. In the sandbox, C only reaches the game's own memory and what the host gives it, so `extern` stays allowed. A hosted game's match code, and the C it calls, has to build for the web; a server build can leave out views and local code.

## Modding

What's agreed (2026-10-05); the design proposed for the rest, and its open questions, are in `docs/spec.md`, Mods. Two parts are built: `virtual` and `override`, in the language for any package (see Language), and the service's first slice, for web games (The build service, below), which is on hold. The rest isn't: tidec's rules for what a mod may be, games that load builds, the prompt before joining, the sandbox.

**Open again** (the owner, 2026-10-06: "its no longer decided"): who builds a game with its mods. That a service we run does was agreed, and its first slice was built, which is on hold (The build service, below). It isn't decided any more, and neither is what rested on it in the list below, each marked: builds fetched from the service, signed by it and revoked there, what the `mods` setting lets a game run, and a closed game's source being the service's alone. Nothing is agreed in its place. What's being weighed is in `docs/spec.md`, Mods: a game that loads a build of itself for its platform, as `tide run`'s host loads one, compiled on the player's machine from the mods' source where a compiler can run (the developer opting in to ship it), or made elsewhere and fetched where one can't. The rest of the list stands.

- (Who builds is open again, above.) Mods are a service we run, the third beside the relay and hosting: remote compilation. A mod is a package that someone other than the game's developer adds to it, and the service builds the game with its mods, whole, natively, for each set of them. So mod code is game code, with its storage, schedule and speed: nothing is interpreted (the owner: the modding community is big, and limiting it is rough), no game carries a runtime for mods, and the engine gains no dynamic path for them.
- (Where a build is fetched from, who signs it and how it's revoked are open again, above.) Every machine of a match runs the same build, local code included, fetched from the service, never from the host: mods never differ between a match's players, not even ones that only change views, which can show a player what gives them an advantage. Before a game joins a match of another build, local code is told its mods and has to say yes, so the game can ask its player, who then knows code from outside is about to be downloaded; nothing is fetched before that. The prompt is the game's own, and the engine draws none and enforces none (decided over a prompt of the host's that no game could skip: it would be our look and language in their game, and the sandbox is what protects players, not the prompt). A game whose code never says yes joins no such match; for mods a developer listed, asking is the game's choice; for mods from anyone, telling the player is a condition of the service, which turns them off for a game that doesn't. A build's mods are a set, whatever order they're listed in. Builds are signed with a key per game, and can be revoked.
- A mod is Tide alone: no `extern`, C, C++ or libraries. Its files are virtual, as a web page's are, and its code calls nothing that reaches outside the game (`Session.Connect` and `Clipboard.Copy` first; the list is part of the sandbox's work).
- A mod replaces a game's declaration with an explicit `override`, and only one the game marked `virtual`: what can be replaced is the developer's choice. An override matches by name and by parameters, both, filters among them: systems can have the same filters under different names. It's a substitution while compiling, and the override takes the replaced declaration's place in the schedule; nothing calls what was replaced for now (a `base()` may come later). Two mods replacing one declaration are an error that names both.
- (What it lets a game run is open again, above.) `mods` is a setting, false by default: with it, what `tide build` makes can run the builds the service signed for the game, and a game without it has none of this.
- (Open again, above.) A closed game's source is seen by the service alone; its modders get its declarations without their bodies.
- That Tide code can't corrupt memory is Tide's own promise, for every game (the owner: "a Tide issue, not a mod issue"): mods rest on it, and it has to be tested as one.
- On desktop, a modded build runs in the OS's sandbox as a second layer, since the compiler was never a security boundary. The web has the browser's. WebAssembly isn't that layer on desktop: mod code compiles with the game, so it would put the whole game in a runtime. A system that can't sandbox a build runs no mods; making the sandbox work wherever games ship is our job.
- Mods are the same on every platform Tide targets (the owner, 2026-10-06: "we should allow it and keep parity"), phones and consoles included: the engine holds no platform back for what a store allows. Whether a store or a system lets a game load code that came after it was installed is the game's developer's to know and to deal with (another store, an app that updates itself), and so is mods being less useful somewhere for it. This replaces what was agreed before, that such platforms have the mods a developer built in and no others.
- The build service, on hold: `notes/build-service.md`.

## Determinism

Given the same build and the same inputs, simulation results must be bit-identical on every supported platform.

### Floats

- Only the IEEE 754 basic operations (`+ - * /` and `sqrt`) can be trusted to give identical results across platforms. Build everything else from them.
- No FMA contraction. The compiler must never fuse `a*b + c` into a single instruction.
- No fast-math. No reassociation or reordering of float operations.
- Simulation code must not use the platform math library (`sin`, `cos`, `atan2`, `exp`, `pow`, and so on). Use the engine's own math library, which is built only from the basic operations.
- Vector math (vectors, matrices, quaternions) is written in-house: it only needs the basic operations, and third-party vector libraries often use approximate instructions or the platform math library.
- Transcendental functions may come from a third-party library, but only as source vendored into the engine and compiled with its flags. Prefer correctly rounded implementations, such as CORE-MATH: they return the same bits on every platform by definition.
- No approximate instructions such as `rsqrtps` or `rcpps`. Their results differ between Intel and AMD.
- Denormals stay enabled (no FTZ/DAZ) on every thread and every platform. WebAssembly can't flush denormals, so no other platform may either.
- Web builds use WebAssembly's SIMD (`-msimd128`, in `CMakeLists.txt` and `tide build --web`), whose operations give the same results on every machine, so the C compiler's vector loops run there too. No relaxed SIMD: its fused multiply-add gives different results on different machines.
- No x87 floating point.
- NaN in simulation state is a bug. NaN bit patterns are not portable.

### Parallelism

- Running in parallel must never change results. Thread timing must not decide the order of anything observable. That includes system order, command playback, reductions, and entity ID allocation.

### Systems that touch the same data

- It is not an error for two systems to write the same component. It's normal.
- Those systems run one after the other, in a deterministic order.
- Tooling tells the user why they didn't run in parallel. Example: "`MoveSystem` runs after `GravitySystem`: both write `Position`."
- Conflicting systems keep their order in the tick, and `[Before]`/`[After]` order systems too. A system starts as soon as everything it waits for is done; there are no barriers between stages. A system's stage is only how deep it is in that chain.
- A system splitting its entities across threads is the other kind of parallelism, and it doesn't change results either.
- Declared access that isn't used is a warning, since it makes other systems wait for nothing: a `mut` parameter that's never written, or a parameter that's never used (a component only needed as a filter belongs in `with`). The language server offers quick fixes, as it does for adding a missing `mut`.
