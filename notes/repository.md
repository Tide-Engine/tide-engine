# Repository layout and reference history

Experimental Newton (2026-10-22): `packages/newton`, namespace `Newton`, adds
statically exported NVIDIA Newton Featherstone CPU kernels for articulated
bodies and effort control, with a two-player arm example in `examples/robotics/arm`.
Native and WASM compile the same scalar source; articulation, rollback and
canonical state transfer tests agree. The collision profile adds spheres,
compound spheres and static planes with upstream rigid-contact forces; no
CUDA runtime, Box3D fallback or separate training engine. Contracts,
upstream pins and missing coverage: `notes/newton.md`.

Optional voice (2026-10-22): `packages/voice` is the sixth bundled package,
with vendored libopus and its own Tide/C bridge. The engine contains only
registration/local-session-data support; games omitting it link no codec.
Contracts and validation: `notes/voice-package.md`.

Managed hosting (2026-10-15): the Node/SQLite service is in tide-services
(below). compiler/cli/deploy.c owns local bundle/login/upload commands;
managed_run.h is the generated game's platform-free WASI reactor host.

Native authoring tool (2026-10-12): tools/tide-editor owns the desktop prefab
editor, its document layer and tests. It links tidec_core and tide_platform after
the platform target is declared, and is omitted from web/Android/platform-off
builds. The native installation adds bin/tide-editor; tide open launches that
sibling through compiler/cli/open.c. No library/generated runtime ABI changed.
Controls, project/save contracts, limits and validation are in notes/editors.md.

Repository navigation moved from `AGENTS.md` on 2026-10-11. Working rules and the current subsystem index remain in that guide. This file describes locations, not permission to work outside an assigned task. Agent ownership applies to any AI agent, as clarified in the guide; the runtime exception is recorded in `notes/runtime-ownership.md`.

## Layout

Optional CPU visibility (2026-10-26): `packages/cornerculling`, namespace
`CornerCulling`, supplies conservative axis-aligned box queries, a spatial tree
and retested candidate caches in ordinary Tide. No renderer/physics dependency
or engine runtime API is added. Contracts and coverage: `corner-culling.md`.

- `engine/`: the engine library (`tide`). Public headers go in `engine/include/tide/`, sources in `engine/src/`. New `.c` files are picked up automatically.
- `compiler/`: `tidec`, the Tide transpiler (maintained by the agent working on it). `compiler/src/asset_files.c` resolves what `asset` declarations name (files, globs, URLs with their cache), `mesh_files.c` reads the glTF and OBJ files a `Mesh<V>` asset names, and `folders.c` beside it lists folders and files for the compiler, the language server and tide. `compiler/tests/e2e/` holds programs compiled and run as tests. `compiler/tests/errors/` holds programs that must fail with the message on their first line (and say each `// expect:` line right after it, an error's note), and `compiler/tests/warnings/` programs that must compile with as many warnings as their first line says, and the texts on their `// expect:` lines (warnings' and notes'). `compiler/tests/shaders/` pins the GLSL and WGSL tidec writes for a program's frags and verts (`tidec --shaders`), as `compiler/tests/schedule/` pins a schedule and `compiler/tests/outline/` the JSON of what a game declares (`tidec --outline`). A test's packages are folders named as the packages are: `errors/<name>/<Package>/*.tide`, and `<name>/<Package>/*.tide` beside a schedule's, a shaders' or a warnings' `<name>.tide`. `compiler/tests/scale/` makes up a big game of many namespaces at test time, which tidec has to compile within a generous time: codegen names every declaration once (`name_program`), as a compile that made each name again at every use grew with the game's size squared. tidec reads files with Windows line endings (CRLF) exactly as LF ones: `e2e/crlf.tide` and `errors/*_crlf.tide` test it, and `.gitattributes` keeps them CRLF on every checkout.
- `compiler/cli/`: `tide`, the command users run (maintained by the agent working on it; see Packaging and releases). `compiler/cli/tests/` tests what it can without a network or a compiler: versions and releases, upgrades (see Packaging and releases), packages, which platform a library is for, apps and bundles and their RSA signatures, zips and `tide editors`. `tide publish` is tested against the build service in tide-services (`builds/publish-cli.mjs`). `cpp_test.cmake` there builds games with C++ and checks what tide says when one can't build, so it needs a tide that builds games: CI runs it on the package it made (`cmake -DTIDE=<tide> -DWORK=<folder> -P compiler/cli/tests/cpp_test.cmake`).
- `compiler/lsp/` (`tidels`, the Tide language server) and `tools/` (the VS Code extension, the JetBrains plugin and the grammar they share), both maintained by the agent working on them: `notes/editors.md`.
- `docs/`: the docs site (see Docs site). `docs/spec.md` is the language spec; `guide/`, `game/`, `packages/`, `multiplayer/`, `language/`, `tools/` and `c/` are the pages for users, written from the spec, `AGENTS.md` and subsystem notes (see Docs site for what goes where).
- `platform/`: the platform layer (`tide_platform`): window, frame loop, input devices and the renderer (see Rendering and platform), and rooms (`src/rooms.c`, with desktop WebRTC in `src/rtc/`), and runs with no window (`src/script.c`, `src/png.c`). Public headers in `platform/include/tide/`: `platform.h`, and the hosts' `run.h`, `host.h` and `migrate.h`. `desktop/`, `web/` and `android/` hold what's each one's own: its window (`src/window.h`) and, on the web, the page (`tide.js`) and the WebGPU backend; `src/gpu_vk.c` is desktop's Vulkan backend; `tools/spirv_shaders.c` is the build-time tool that makes the renderer's own shaders' SPIR-V (built from `compiler/CMakeLists.txt`, so cross builds get a native one as they get tidec); `font/` holds the font text is drawn in, and `third_party/` what's vendored (`stb_truetype.h`, fflate, and Khronos's Vulkan headers in `vulkan/`).
- `demo/`: a small game on the platform layer. `demo.tide` is the simulation and the views that draw it, and `main.c` is the host. It builds as `demo.html` on the web.
- `examples/`: `duel`, the physics games `towers` and `sumo`, `robotics/arm` and `robotics/microduck` (Newton articulated control and transition recording), the `samples/` gallery, and `network/`, a game on a network of its own (`tide/network.h`: `notes/own-network.md`), which is the docs' walkthrough and has its test under `test/`. The physics games each have `tide.packages` and tests under `test/`. See `notes/physics-games.md`, `notes/newton.md` and `notes/docs-site.md`.
- `bench/`: benchmarks, games played by bots on one machine (see Benchmarks).
- `packages/`: the packages that come with tide (see Packaging and releases), a folder each, named as a game's `tide.packages` names it, with a `tide.packages` of its own that says `package Name`. They install into the package as they are, in `src/packages/`, but for what's this repo's own: `packages/CMakeLists.txt`, which builds their tests the way tide would build the package, and each one's `test/`. `render3d`, `cornerculling`, `box2d`, `box3d`, `newton`, `rooms`, `lobby`, `steam` and `voice` are the packages in this tree; Newton's initial profile covers articulated rigid bodies (see `notes/box2d.md`, `notes/box3d.md`, `notes/newton.md`, `notes/rooms-package.md`, `notes/lobby-package.md`, `notes/lobby-steam-package.md` and `notes/voice-package.md`). `steam` needs Steamworks' library, which isn't here: nothing builds it but its own test, against a fake of that library (`packages/steam/test/fake_steam.c`), and a game that doesn't list it builds none of it. `packages/test/` is what several packages' tests share: `rewind_network_test.c`, and `network_players.h`, players with real sessions in one program, served as the standard host serves one.
- `relay/`: dependency-free Node room/signalling service. `relay/fly.toml` records its deployment configuration. Room/protocol deadlines use `createRelay`'s injected monotonic `clock` (`now` and `wait`), and tests advance it explicitly; TURN credential caching separately uses wall time. Transport, relay and deployment details belong in `notes/webrtc-and-rooms.md`.
- `tests/`: tests built on the harness in `tests/tide_test.h`. New test files are picked up automatically.
- `.github/workflows/`, `.releaserc.json`, `install.ps1`, `install.sh`: releases and installing (see Packaging and releases), and the docs site.
- `cmake/`: shared compiler flags (`TideFlags.cmake`), the package (`package.cmake`), the file that locates clang (`clang-toolchain.cmake`), the web, MinGW and Android toolchains (`wasi-toolchain.cmake`, `mingw-toolchain.cmake`, `android-toolchain.cmake`), tide's built-in clang (`LLVM.cmake`), `tide_add_game` (`Tide.cmake`), the GLFW download (`GLFW.cmake`), files as C arrays (`embed.cmake`), and `tide_add_web_test` (`WebTest.cmake`), which runs a web page in headless Chrome or Edge as a test, and with `WEBGPU`, again on WebGPU.
- `unity/`: Unity's Tide package, `com.tide.unity` (a git URL package: `?path=/unity/com.tide.unity`; the game's own C# is written by tide, `compiler/cli/unity_code.c`) and `Sample/`, a Unity project using the package by relative path, with its play mode test, a player smoke check and `web-smoke.mjs` for its WebGL player. The converter's fixture, a project Unity wrote, is `compiler/cli/tests/unity_project`. `notes/unity.md`.
- `notes/`: what was built and what code relies on, a file a subsystem (see Notes).

## Other repositories (2026-10-21)

The owner chose to license the engine `MIT OR Apache-2.0` and to move what isn't the engine into private repositories of their own, keeping docs and the relay here. Both were split from this repo with their history (`git filter-repo`), and both follow this repo's `dev` with no pinned version.

| Repository | What moved there | Its notes |
|---|---|---|
| `Tide-Engine/tide-prototypes` | `prototypes/` (now its root), the catalog cards and thumbnails that were in `docs/src/routes/admin/prototypes/+page.server.ts` and `docs/static/prototypes/` (now `catalog/`) | `cracked`, `inside-out`, `underfoot`, `foldline`, `foldline-redesign`, `sunday-supper`, `gravewake`, `private-prototypes` |
| `Tide-Engine/tide-services` | `hosting/`, `builds/`, `.github/workflows/hosting.yml`, and `compiler/cli/tests/publish_test.mjs` (now `builds/publish-cli.mjs`, with the `builds` and `tide_cli_publish` tests leaving this repo's CTest and the package's build-service check leaving the Build workflow) | `managed-hosting`, `build-service` |

How they connect:

- **`TIDE_EXTERNAL_DIRS`** (root `CMakeLists.txt`) is a list of folders outside this repo built with it, each `add_subdirectory`'d into `external/<folder name>` after the examples, so `PROJECT_SOURCE_DIR` and every engine target are the engine's. The cache variable defaults to the environment variable of that name, a path list in the system's separator, read at first configure. A missing `CMakeLists.txt` stops the configure. It replaces `TIDE_PRIVATE_PROTOTYPES`; the prototypes have no other switch.
- **The website catalog** comes from what the prototypes' build stages: `private_games` depends on `prototype_catalog`, which copies `catalog/catalog.json` to `docs/prototypes.json` and each card's thumbnail to `docs/static/prototypes/<id>/`, beside the `<id>_page` targets' games (both ignored by git here). `docs/prototypes.js` reads the file (absent: no prototypes, every such address an ordinary path) and makes the public-root and retired-alias patterns from its ids; `admin.js` takes the ids from `server.js` and the dev server, the catalog page reads the cards at request time, and the image includes the file when it exists. Ids are lowercase words joined by dashes.
- **The engine's code from the other side**: both repos' `engine.mjs` finds the engine checkout (`TIDE_ENGINE_DIR`, else a `tide-engine` folder beside the repo) and imports `relay/relay.mjs` from it; the prototypes' CMake passes `TIDE_ENGINE_DIR` to the browser tests that start a relay.
- **CI**: the Docs workflow checks out `tide-prototypes` at `dev` with the read-only deploy key in `PROTOTYPES_DEPLOY_KEY` and builds with it; without the key it builds the site with no prototypes, and a run on `dev` (a deploy) fails instead. The services' Hosting workflow checks out this repo's `dev` (public, so with no key) and writes the engine commit it tested into the run's summary.

Carrying a branch made before the split: its changes to the moved paths go to the new repo as a patch, then the engine branch takes `dev`'s deletion. From the engine branch, with `<base>` its merge base with the commit before the split (`git log -1 --format=%H --diff-filter=D origin/dev -- prototypes/CMakeLists.txt`, then its parent):

```sh
git diff <base> HEAD -- prototypes | git -C <tide-prototypes> apply -3 -p2
git diff <base> HEAD -- notes/gravewake.md | git -C <tide-prototypes> apply -3   # its notes keep their paths
git diff <base> HEAD -- hosting builds notes/managed-hosting.md | git -C <tide-services> apply -3
git merge origin/dev   # take the deletions: git rm -r the moved paths it reports
```

## Public, with its history (2026-10-21)

The owner chose to publish the engine with its whole history, the prototypes', services' and notes' past included, after one rewrite of commit metadata only: `Co-Authored-By:` trailers naming Claude or Codex were removed from messages, and the three commits authored as `Claude <noreply@anthropic.com>` were credited to BlenMiner (`git filter-repo` with a mailmap and a message callback, on `dev`, `release` and all 169 tags). Every tree is unchanged, so builds, tags and releases are the same; commit IDs from the first changed commit on are new. Commit IDs cited in notes and `bench/README.md` were mapped to the new ones (`bench/results` keeps its recorded revisions, which its saved digests cover). A full scan of every blob for credentials found only test fixtures. `notes/`, `AGENTS.md` and `CLAUDE.md` are public.

- The `release` ruleset forbids force-pushes; it was disabled for the rewrite's push and re-enabled.
- Local branches made before the rewrite hold the old commits. The main checkout's shared `.git` has a `pre-push` hook that refuses to push any of them (`.git/rewritten-commits.txt`, the 1,885 old IDs), and any commit with an AI co-author trailer or an agent author. Carry such a branch over as a patch onto the new `dev`: `git diff <its base> <its tip> | git apply -3`.

## Cleanup (2026-10-21)

After the split, the owner removed what no longer belonged in the engine: `GAME_TRIAL_FEEDBACK.md` (the 2026-10-06 AI game-builder trial's internal report), `notes/mockup-games.md` (the record that mockup.games moved to `Tide-Engine/mockup-games`), the former self-hosted runners' `.github/runner/README.md` and `start-runner.ps1` (`setup-linux.sh` stays for the Linux cross build's xwin step), the AI onboarding trials' saved games, transcripts and checks under `notes/ai-onboarding/` (their summary stays in `notes/ai-onboarding.md`), and `sandbox/`, the owner's experiments. All are in git history at `bf647de4`. `bench/results` stays: the benchmark scripts read and append to it.

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
