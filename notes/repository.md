# Repository layout and reference history

Native authoring tool (2026-10-12): tools/tide-editor owns the desktop prefab
editor, its document layer and tests. It links tidec_core and tide_platform after
the platform target is declared, and is omitted from web/Android/platform-off
builds. The native installation adds bin/tide-editor; tide open launches that
sibling through compiler/cli/open.c. No library/generated runtime ABI changed.
Controls, project/save contracts, limits and validation are in notes/editors.md.

Repository navigation moved from `AGENTS.md` on 2026-10-11. Working rules and the current subsystem index remain in that guide. This file describes locations, not permission to work outside an assigned task. Agent ownership applies to any AI agent, as clarified in the guide; the runtime exception is recorded in `notes/runtime-ownership.md`.

## Layout

- `engine/`: the engine library (`tide`). Public headers go in `engine/include/tide/`, sources in `engine/src/`. New `.c` files are picked up automatically.
- `compiler/`: `tidec`, the Tide transpiler (maintained by the agent working on it). `compiler/src/asset_files.c` resolves what `asset` declarations name (files, globs, URLs with their cache), `mesh_files.c` reads the glTF and OBJ files a `Mesh<V>` asset names, and `folders.c` beside it lists folders and files for the compiler, the language server and tide. `compiler/tests/e2e/` holds programs compiled and run as tests. `compiler/tests/errors/` holds programs that must fail with the message on their first line (and say each `// expect:` line right after it, an error's note), and `compiler/tests/warnings/` programs that must compile with as many warnings as their first line says, and the texts on their `// expect:` lines (warnings' and notes'). `compiler/tests/shaders/` pins the GLSL and WGSL tidec writes for a program's frags and verts (`tidec --shaders`), as `compiler/tests/schedule/` pins a schedule. A test's packages are folders named as the packages are: `errors/<name>/<Package>/*.tide`, and `<name>/<Package>/*.tide` beside a schedule's, a shaders' or a warnings' `<name>.tide`. `compiler/tests/scale/` makes up a big game of many namespaces at test time, which tidec has to compile within a generous time: codegen names every declaration once (`name_program`), as a compile that made each name again at every use grew with the game's size squared. tidec reads files with Windows line endings (CRLF) exactly as LF ones: `e2e/crlf.tide` and `errors/*_crlf.tide` test it, and `.gitattributes` keeps them CRLF on every checkout.
- `compiler/cli/`: `tide`, the command users run (maintained by the agent working on it; see Packaging and releases). `compiler/cli/tests/` tests what it can without a network or a compiler: versions and releases, upgrades (see Packaging and releases), packages, which platform a library is for, apps and bundles and their RSA signatures, zips, `tide editors`, and `tide publish` against the build service run on this machine (`publish_test.mjs`, `tide_cli_publish`: see Modding). `cpp_test.cmake` there builds games with C++ and checks what tide says when one can't build, so it needs a tide that builds games: CI runs it on the package it made (`cmake -DTIDE=<tide> -DWORK=<folder> -P compiler/cli/tests/cpp_test.cmake`).
- `compiler/lsp/` (`tidels`, the Tide language server) and `tools/` (the VS Code extension, the JetBrains plugin and the grammar they share), both maintained by the agent working on them: `notes/editors.md`.
- `docs/`: the docs site (see Docs site). `docs/spec.md` is the language spec; `guide/`, `game/`, `packages/`, `multiplayer/`, `language/`, `tools/` and `c/` are the pages for users, written from the spec, `AGENTS.md` and subsystem notes (see Docs site for what goes where).
- `platform/`: the platform layer (`tide_platform`): window, frame loop, input devices and the renderer (see Rendering and platform), and rooms (`src/rooms.c`, with desktop WebRTC in `src/rtc/`), and runs with no window (`src/script.c`, `src/png.c`). Public headers in `platform/include/tide/`: `platform.h`, and the hosts' `run.h`, `host.h` and `migrate.h`. `desktop/`, `web/` and `android/` hold what's each one's own: its window (`src/window.h`) and, on the web, the page (`tide.js`) and the WebGPU backend; `font/` holds the font text is drawn in, and `third_party/` what's vendored (`stb_truetype.h`).
- `demo/`: a small game on the platform layer. `demo.tide` is the simulation and the views that draw it, and `main.c` is the host. It builds as `demo.html` on the web.
- `sandbox/`: the owner's experiments: a game with no C, built by `tide_add_game`.
- `examples/`: `duel`, the physics games `towers` and `sumo`, the `samples/` gallery, and `network/`, a game on a network of its own (`tide/network.h`: `notes/own-network.md`), which is the docs' walkthrough and has its test under `test/`. The physics games each have `tide.packages` and tests under `test/`. See `notes/physics-games.md` and `notes/docs-site.md`.
- `prototypes/`: CRACKED (`cracked/`, `notes/cracked.md`), Inside Out (`inside-out/`, `notes/inside-out.md`), Underfoot (`underfoot/`, `notes/underfoot.md`), FOLDLINE (`foldline/`, `notes/foldline.md`) and SWINDLE (`swindle/`, `notes/swindle.md`), opt-in through `TIDE_PRIVATE_PROTOTYPES`. `prototypes/CMakeLists.txt` defines the `private_games` staging target, which also collects Two Forks from `examples/samples/two-forks/`. Read each game's own note and `notes/private-prototypes.md` before changing its build or catalog staging. The six earlier prototypes were deleted on 2026-10-11 and are in git's history.
- `bench/`: benchmarks, games played by bots on one machine (see Benchmarks).
- `packages/`: the packages that come with tide (see Packaging and releases), a folder each, named as a game's `tide.packages` names it, with a `tide.packages` of its own that says `package Name`. They install into the package as they are, in `src/packages/`, but for what's this repo's own: `packages/CMakeLists.txt`, which builds their tests the way tide would build the package, and each one's `test/`. `box2d`, `box3d`, `rooms`, `lobby` and `steam` are the bundled packages (see Box2D and Box3D, under Packaging and releases; `notes/rooms-package.md`, `notes/lobby-package.md` and `notes/lobby-steam-package.md`). `steam` needs Steamworks' library, which isn't here: nothing builds it but its own test, against a fake of that library (`packages/steam/test/fake_steam.c`), and a game that doesn't list it builds none of it. `packages/test/` is what several packages' tests share: `rewind_network_test.c`, and `network_players.h`, players with real sessions in one program, served as the standard host serves one.
- `relay/`: dependency-free Node room/signalling service. `relay/fly.toml` records its deployment configuration. Room/protocol deadlines use `createRelay`'s injected monotonic `clock` (`now` and `wait`), and tests advance it explicitly; TURN credential caching separately uses wall time. Transport, relay and deployment details belong in `notes/webrtc-and-rooms.md`.
- `builds/`: dependency-free Node build service and `tide publish` integration, preserved with tests but on hold. The inactive configuration is in `builds/fly.toml` and `fly.builder.toml`; resuming it requires the owner's instruction. See `notes/build-service.md` for implementation/deployment history and `notes/modding.md` for which decisions remain open.
- `tests/`: tests built on the harness in `tests/tide_test.h`. New test files are picked up automatically.
- `.github/workflows/`, `.releaserc.json`, `install.ps1`, `install.sh`: releases and installing (see Packaging and releases), and the docs site.
- `cmake/`: shared compiler flags (`TideFlags.cmake`), the package (`package.cmake`), the file that locates clang (`clang-toolchain.cmake`), the web, MinGW and Android toolchains (`wasi-toolchain.cmake`, `mingw-toolchain.cmake`, `android-toolchain.cmake`), tide's built-in clang (`LLVM.cmake`), `tide_add_game` (`Tide.cmake`), the GLFW download (`GLFW.cmake`), files as C arrays (`embed.cmake`), and `tide_add_web_test` (`WebTest.cmake`), which runs a web page in headless Chrome or Edge as a test, and with `WEBGPU`, again on WebGPU.
- `notes/`: what was built and what code relies on, a file a subsystem (see Notes).

## Legacy section references

Older comments and notes refer to section names from AGENTS.md before its 2026-10-08 split. Resolve those references with this table; bare names are files under `notes/` with the `.md` suffix:

| Section as it was | Now in |
|---|---|
| Rendering and platform | `AGENTS.md` for its rules; `rendering`, `drawing`, `shaders`, `textures`, `render-targets`, `audio`, `gui`, `standard-host` |
| Tech stack | `AGENTS.md`, What Tide is; `assets-and-files` |
| Building | `AGENTS.md`, including Testing and merging and Cross-platform determinism tests; `testing` |
| Web builds | `web`; the rooms' tests in `webrtc-and-rooms` |
| MinGW builds, Android builds | `android-and-mingw` |
| Benchmarks | `benchmarks` |
| Tide programs | `tide-programs` |
| Docs site | `docs-site` |
| The physics games | `physics-games` |
| Packaging and releases | `cli-and-packages`, `releases-and-ci` |
| Box2D, Box3D, Rooms, Hot reloading | `box2d`, `box3d`, `rooms-package`, `hot-reload` |
| Layout | This file; `editors` |
| Runtime written by Claude for now | `runtime-ownership`, `input-devices` |
| Language | `AGENTS.md`, Language and runtime contracts; `language`, `c-interop`, `random`, `memory-safety`, `standard-host` (`Debug`, debug views) |
| ECS and simulation state | `AGENTS.md` for its rules; `ecs` |
| Networking | `AGENTS.md` for core decisions; `netcode`, `verified-and-local-handlers`, `webrtc-and-rooms` |
| Modding | `modding`; The build service in `build-service` |
| Determinism | `AGENTS.md`; Systems that touch the same data also in `schedule-and-threads` |

## Guide maintenance

The 2026-10-11 cleanup reduced the root guide to working rules, core architecture and a compact notes index. The layout and legacy section redirects moved here; modding requirements and withdrawn proposals moved to `notes/modding.md`. Detailed language behavior stays in `docs/spec.md` and `notes/language.md`; the guide retains the rules needed to preserve it.

The audit checked CMake presets, toolchain selection, shared flags, bundled packages, workflow triggers, the hidden-run environment, scheduler source and its phase fixture, and referenced paths. It corrected omitted Rooms, incomplete fixture paths, ambiguous runtime ownership, local state described as outside every world rather than outside the match world, and named phases confused with dependency depth. It removed the claim that joint optimization guarantees free calls. Service deployment history remains a record in its notes, not a live infrastructure audit.

Validation: all 44 notes are indexed, live paths in the guide and migrated notes resolve, and `git diff --check` passes. The deleted mods page remains explicitly identified as history in the build-service note; the schedule note's fixture references now include their actual `.tide` suffixes. No build, game run, CI dispatch or deployment is needed for these documentation-only edits.
