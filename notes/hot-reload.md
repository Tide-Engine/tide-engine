# Hot reloading

## Optional voice lifecycle (2026-10-22)

Retained native reload calls the old voice provider's stop after mixer forget
and before library unload, detaching session callbacks and freeing private
codec/sample state. Full restart uses the same stop through `tide_run_end`.
No speech history is serialized or carried into the next build. Package
contracts, sender-replacement/reconnect coverage and full validation are in
`notes/voice-package.md`.

Library loading for another program (2026-10-20): `reload.c`'s helpers for
carrying state over and loading, checking and swapping `game-<n>` libraries
moved, unchanged but for one line, into `platform/src/reload_library.inc`,
which `reload.c` and `embed.c` both include: each file including `tide/host.h`
has its own copy of the host's state, and these helpers work on it. Under
`tide/embed.h`, a full restart whose library won't load leaves the game stopped
instead of exiting. `tide run`'s behavior is unchanged. `notes/standard-host.md`.

## Double matrices (2026-10-20)

Double matrix fields use the existing PLAIN matrix layout category, preserving
identical types by bytes. A float/double matrix type change retains the new
default, as other matrix type changes did. Scalar/vector double widening from
the preceding increment is unchanged. No new migration runtime helper or
metadata enum is introduced. Validation: notes/language.md.

## Double layout values (2026-10-20)

TIDE_LAYOUT_DOUBLE is appended after the existing layout kinds, leaving their
values unchanged. Generated double scalar/vector fields use eight-byte elements
and ordinary numeric dimensions. Migration preserves identical double fields
and widens matching byte/int/long/float fields, including float vectors; narrowing
resets to the destination default. The new handwritten migration case checks
all four widening sources, a vector, an unchanged double and refused narrowing.
No new runtime function or layout-file structure is required. Validation:
notes/language.md.

Managed releases (2026-10-15): deployment does not hot-reload production worlds.
Release IDs identify immutable bundles; Simulation IDs govern admission while
`tide_game.hash` retains layout/migration identity. The new macro is appended
outside that layout hash. Client/server closure is computed before stripping.
Existing matches retain their release and ?match=id serves that release's client.
Exact-release durable recovery does not promise cross-release migration.
Contracts, asset watch regeneration and tests: managed-hosting.md.

Docs playground (2026-10-14): locally compiled Wasm is supplied through the
page's manual replacement mode, using exactly the existing web save/migrate
host and layout representation. Program and pack install together after
successful instantiation. The manual queue owns its bytes; restarting uses
only the most recent successful install and carries no previous state.
`platform/tests/reload_web.mjs` now covers ownership, initial/following/reset
installs, serialized restart, bad-module preservation and recovery without a
watch server. The actual docs browser fixture preserves a known counter while
adding a field, confirms the new default, and keeps rendering after a syntax
error. Room closure and the existing state migration rules do not change.
Full-suite and browser/compiler validation are in notes/docs-site.md.

## Retained asset values and heap packing (2026-10-11)

Asset-backed list fields retain assigned bytes independently of the loaded pack. Unload/replacement frees its own cache reference, while worlds/snapshots and detached origin hints retain their copies. Raw packed heaps now start with a self-contained immutable-content dictionary; migration copies that packed heap intact and emits a four-byte empty dictionary for a new empty heap. Optional encoding origins are omitted from raw saves/migration; values and hashes are preserved, though the new world may send full edited pages. Host/game storage and session callback ABIs changed, so rebuild them together; no compatibility with older packed heaps is promised. Snapshot/unload/raw-roundtrip regressions are in `test_asset_lists.c` and generated `asset_sharing`.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Packaging and releases

### Hot reloading

`BoneTransform` defaults (2026-10-08) are ordinary typed expressions: zero position, `quaternion.identity`, and `float3(1)` scale. Layout generation visits the model-related builtin structs too, so their default AST must obey the same contracts as source expressions even when the game never constructs a BoneTransform itself. The C writer handles constant members before reading their receiver. A first SkinnedMesh declaration in a watching run previously exposed receiver-less defaults and crashed during `gen_layout`; valid default expressions fix that path. `compiler/tests/shaders/bone_transform_defaults` pins numeric literal/default emission in GLSL/WGSL, and `platform/tests/skinned.tide` checks those default values in a custom vert on OpenGL, WebGL and WebGPU.

It's for quick iteration, enabled by default under `tide run`, native and web. `tide build` output and runs without a window (`--capture`, `--frames`, `--script`) have none of it.

- `tide run` builds the game as a library (`game-<n>.dll`, `.so` or `.dylib`) and a small host that loads it (`tide_host_run_library`, in `platform/src/reload.c`). tide watches the game's `.tide` files, and its C and C++ files, headers and libraries, and builds again once a change has settled. The game's C is part of its library, so the state C keeps starts over at each reload, and C++ globals are constructed again. Each build is linked under another name and renamed into place when it's whole; between two frames, the host swaps in the newest.
- A build with the same data layout keeps the match and the local state where they are (`tide_session_set_game`). The layout is `tide_game`'s hash: the generated header but for its first line.
- A build with another layout gets the match and the local state carried over by name (`tide/migrate.h`, `tide_session_migrate`). Keep as much state as possible, and when a big change breaks it, starting over is fine. No attribute for renamed fields, like Unity's `[FormerlySerializedAs]`, and no save files. The rules:
  - Fields of the same name keep their values. `int` to `float` or `long` and int vectors to float vectors of the same size convert, as Tide converts them implicitly, and so does `long` to `float`; enums keep their member by name. Other fields, and new ones, get their declared default; new text and lists start empty. A changed default doesn't touch existing values.
  - Entities keep their IDs. Without a removed component, an entity goes to the storage for the rest of its components. It's dropped when there's none.
  - A world whose scene is gone starts over.
  - Local fields carry over with the local state: a component's as rows of a type of their own, "<Component> local" (`tide_layout_world.attached`), field by field, and a singleton's as the local singleton they are.
  - The host says what didn't carry over: fields reset and entities dropped.
- tidec describes a game's layout for this only when asked (`--layout`, `codegen_options.layout`, `tide_add_game(... LAYOUT)`): `tide_game_layout` in the generated `.c`, not the header, so `tide_game`'s hash doesn't change. The migration is in the platform layer's static library (`platform/src/migrate.c`), so only programs that call it link it.
  - The map insertion-order representation (2026-10-08) tags plain layout fields holding maps or sets, including lists, arrays and other containers that hold them through structs, with `#map-order-2` (`layout_field_name` in `codegen.c`). Older heap representations reset instead of being copied with a new stride. Struct fields still migrate recursively so their unrelated fields carry over. Waiting-task fingerprints include this representation transitively too, even when the frame holds only a four-byte list handle. `compiler/tests/map_layout.cmake` checks header/layout markers and that identical async code with a map hidden inside its list element gets another task fingerprint; ordinary non-map containers keep their names.
- Skinned asset content is part of the generated game identity (2026-10-08), because skeletons, poses and geometry can drive match gameplay. `tide run` therefore rebuilds when a named model or external glTF buffer changes, instead of publishing only a new pack; games without skinned assets still repack their ordinary assets. A bad model publishes neither a build nor a pack and remains watched for a correction. The generated asset loader also preflights canonical model content against its build's hashes before loading a pack, preserving the old assets on a mismatch. The CLI watch regression is in `compiler/cli/tests/cache_test.mjs` (`--watch-only`); model/pack bounds are in `tests/test_skinned_decode.c` and `test_pack.c`.
  - The compiler's asset cache and the CLI watcher both preserve subsecond file timestamps (2026-10-09). A same-size external-buffer edit within one second must reread and decode the model before generating its new identity; the web-watch test fixes both timestamps within one second to pin this case without sleeps. Whole-second compiler timestamps on Linux/macOS previously allowed a detected edit to rebuild with stale content.
- In a match, the server's machine carries its world over, and its own player takes that world. A client of another machine carries its last verified world over, and the server sends what it lacks if it comes out different. Inputs sent for ticks to come are dropped: players' last inputs stand in until new ones arrive.
- A waiting task carries over to a build where its code and its frame's layout are the same: each table of tasks in `tide_game_layout` is named with a fingerprint of both, and the header says each one's code, so a change to async code changes the layout. Other waiting tasks are dropped, and the host says how many. A world's task order and the local world's frame count go on.
- A build that fails leaves the game running its last one. Ctrl+R in the watching game, or `r` and Enter in tide's terminal (through a `restart` file in the run's folder), fully reloads the game: match, local state, GUI and the game's C and C++ globals all start over, with no migration. On the web, Cmd+R does the same on macOS. The host says what each reload did, so under `tide run` it's a console program even with `--release`.
- A game started with `--join` or `--connect` joins again a second after its match ends without it leaving: when the server starts over, or still runs an older layout.
- Closing the native game window ends `tide run` with the game's exit code. The daily update check runs before a run, so no network request delays its ending.
- Each run has its own folder, `.tide/<configuration>/run/<tide's process ID>`, so a server and a client run from one folder never build over each other. Runs without a window build a standalone program there and delete it when it ends. Watching runs keep the last two published builds and the host's own copy of the active library (`loaded-<n>`), so a full reload can reopen it even after newer builds failed to load and the CLI removed its published file. The host removes its previous copy after a successful swap. The folder is deleted when the run ends, or by a later run once its tide is gone.
- A game's library has its own copy of the engine code it uses. So everything the host and the library share is data: worlds and local state (with their pages, from the C library's heap, which every build of the game shares), devices, the draw list and the GUI. Render targets are what a library keeps track of itself, so the new build takes over those the local state holds (`tide_host_game.rebind_resources`, the generated `tide_local_rebind_resources`). The library frees its scratch area and what's left of its targets' bookkeeping before it's unloaded (`tide_host_game.unload`).
- On Linux, a library's objects are compiled with `-fPIC`. The engine's are kept apart in `engine-pic`, so running and building don't rebuild each other's.
- On the web, each build is a WebAssembly program of its own. `tide run --web` serves the page on 127.0.0.1 (`compiler/cli/serve.c`), opens it (unless `--no-open`), and keeps running until it's stopped. The editors run it with `--no-open` and show the page themselves, at the address tide prints (`tide: the game is at ...`): VS Code's **Tide: Run on the Web** in its browser, and the JetBrains plugin's Tide run configurations in the Tide Game tool window. The page asks tide for the newest build 4 times a second (`/build`: the build and restart numbers; `/game-<n>.wasm`).
- The page starts each new build in the running one's place (`platform/web/tide.js`, between its `tide run` markers): it asks the running one for its state (`tide_reload_save`), deletes what it made in WebGL or WebGPU, and starts the new one, which carries that state over (`tide_host_run_web` in `platform/src/reload.c`). The state is the old layout, packed with offsets for pointers (`tide_layout_pack`), the server's world and the local state as bytes (`tide_world_pack`, `tide_local_pack`), the GUI, and what the host keeps of its own: whether the stats' drawer is open. The new session goes on from the world with this machine's player in it already (`tide_session_start_from`), so there's no second `PlayerJoined`. A room closes at a reload, for now: the new build goes on with the match closed, and the other players drop out.
- A match on a game's own network (`tide/network.h`, `notes/own-network.md`) isn't carried over a native reload: its transports are functions of the library that made them, so `swap` starts the game over, as a full reload does, before that library unloads. On the web a reload goes on with the match closed, as a room's does.
- Pages without watching (`tide build --web`, `cmake/web_page.mjs`) leave out the lines between tide.js's `tide run` markers, and web programs only link reload code when the watching run's `main.c` calls it.

## Confirmed clocks and collider history (2026-10-11)

The frame callback ABI now includes confirmed previous/fraction endpoints. Repository hosts and reload fixtures rebuild together. Client migration resets the confirmed presentation clock and invalidates the old presented input timestamp when adopting a new first tick. The generated completion-slot pointer is tick-local and omitted from migration/copy/pack/hash. Collider history is ordinary typed match data; missing/newly initialized history fails Rewind explicitly. Disabling maxRewind clears previously retained physics history on its next step.

## T? fields across a reload (2026-10-11)

A field declared `T?` is described in `tide_game_layout` as the struct it's kept as, a type named as the `T?` is ("int?") with the fields `value` and `ok`, so it carries over by name like any struct field: a value stays a value, and nothing stays nothing rather than becoming the declared default again. A field that was a `T` and is now a `T?`, or the other way, is another kind and starts at its default (not built: carrying the value across). `migrate_carries_optional_fields_as_a_value_or_nothing` (`platform/tests/migrate.c`) checks both, text included.

## Transform hierarchy layouts (2026-10-12)

Generated private hierarchy singleton maps and their node/entry types use existing
layout metadata, so retained links migrate with ordinary world-owned containers.
Private local component storage names are stable from the qualified source type;
public values use the source C representation. The former local-component
syntax/storage naming is intentionally replaced, without compatibility aliases.
Snapshot and raw-pack restoration are tested with live parent links; a dedicated
hierarchy hot-reload and portable-wire roundtrip remains future coverage.
