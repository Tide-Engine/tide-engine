# Debugging Tide code in native debuggers

## Windows ASan debugger fixture (2026-10-29)

The native sanitizer suite exposed dbgeng stopping at a first-chance access
violation in ASan's initialization, before the fixture's Tide breakpoint.
Windows ASan commits shadow-memory pages on demand through its exception
handler; [Microsoft documents these first-chance stops](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-known-issues#asan-64-bit-first-chance-exceptions).
`natvis_test.cmake` now starts its command script with `sxd av`, allowing the
handler to run while retaining second-chance breaks for unhandled violations.
It also explicitly rejects sanitizer reports captured from the debug session.
All existing Tide line, stepping, local-variable and natvis assertions remain.

Validation: `tidec_debugger_natvis` passes with the Windows `sanitize` build
(Clang 23.1.2 ASan) and the ordinary native `debug` build. The latter's missing
fixture executable was built from this checkout before its successful run.
No debugger-driver code, compiler output or runtime instrumentation changed.

Built 2026-10-26, asked for by the owner: Tide code debuggable in native
debuggers, so a game's systems step as Tide rather than as generated C, most of
all inside Unity (the match is a native library there) and in `tide run`. User
docs: `docs/tools/debugging.md`; the editors' READMEs and
`unity/com.tide.unity/README.md` have their parts.

## What's built

**Lines.** tidec already wrote `#line` directives back to `.tide` lines
(`codegen_options.line_directives`; `--no-line` turns them off). Two gaps:

- A statement's C often takes several lines (a foreach's loop header and
  element, a block's braces), and after `#line N` C counts on, N + 1, N + 2:
  those lines were other statements' lines, so a debugger stepped through
  them, and a breakpoint on a later Tide line found the loop header too
  (dbgeng: "Ambiguous symbol"). `pin_lines` in `codegen.c`, run as `game.c` is
  written, gives each code line after the first under a `.tide` `#line` the same
  directive again, and recounts the directives naming `game.c` itself
  (`line_reset`) as the lines move them. Blank lines get none.
- A function of the game's own code opened at generated-C lines: its
  prologue (the `(void)` lines, list caches) mapped to `game.c`, so stepping
  into a Tide function stopped in generated C first. `code_start` emits the
  declaration's `#line` before the signature of systems, views, handlers
  (`gen_system_body`), functions, methods, properties and operators
  (`gen_routine`), a task's body, the input's Sample and sort comparators.

**Stepping over plumbing.** In line mode `TIDE_HELPER` (the generated
plumbing's helpers: list, map and grid accessors, blends, text ownership) is
`static inline __attribute__((unused, nodebug))`, and Tide-code functions are
declared `TIDE_CODE` (no `nodebug`), in their prototypes too
(`code_helper`): an attribute on any declaration applies to the definition.
Debuggers step over functions without debug info, so Step Into on a Tide line
goes into the Tide function it calls. `-DTIDE_DEBUG_PLUMBING` gives the helpers
their debug info back, for debugging the generated C. Rows, tasks and the tick
keep theirs: they are `static`/`TIDE_ROWS`, and stepping out of a system shows
where the engine called it. The engine's own inline helpers (`tide_add_i`,
`tide_list_count`) come from engine headers with debug info: `#pragma clang
attribute` can't apply `nodebug`, and the headers are the runtime's, so a step
can enter them; LLDB's views set `step-avoid-regexp` for them, and the docs
give Visual Studio a `.natstepfilter`.

**Debug info flags** (`compiler/cli/build.c` `config_flags`, game objects of
non-release builds; `tide_add_game(VISUALIZERS)`): `-gno-column-info`, as the C
columns would point into `.tide` lines at places that mean nothing (LLDB drew
a caret there), and `-fno-eliminate-unused-debug-types`, as a type only the
nodebug plumbing names (a list's element struct, a map's `tide_map<n>_entry`)
had no debug info and the natvis couldn't cast to it.

**Names.** Locals and parameters are already their Tide names
(`local_cname`): the exceptions are a trailing `_` for a name C reserves or a
type takes, `tide_this` for `this`, and `tide_in<n>_` before the locals of a
function inlined because it takes an `Action` (its caller's block runs inside
it, so the names must differ). C can't rename a variable in debug info, so
those stay, documented. Function names are C's (`tide_system_Move`,
`tide_function_Heal`, `tide_method_Stats_IsDead`).

**Views** (`tidec --visualizers`, `compiler/src/debug_views_codegen.inc`):
`<name>.natvis` and `<name>_lldb.py` beside the generated C.

- Text, lists and maps are handles (`tide/text.h`): the views resolve them as
  `tide_block_at` does, through the stopped thread's `tide_world_heaps` and
  `tide_scratch_slots` thread-locals (native TLS on Windows' MinGW target:
  `S_GTHREAD32`, which dbgeng evaluates in natvis), the heap's page table, and
  for a list in chunks, its chunk's page. The constants come from the runtime's
  headers (`TIDE_SCRATCH_SHIFT`, `TIDE_HEAP_PAGE_SHIFT`, `TIDE_LIST_CHUNKED`,
  `TIDE_LIST_ASSET`, `TIDE_LIST_CHUNK`), written into each file, so a runtime
  change to them follows; a change to the heap's or page's structs needs these
  expressions changed too (the debugger tests fail).
- Natvis matches C types, and Windows' debug info keeps no typedefs, so every
  `List<T>` is a `tide_list`: a list field's elements show through its
  struct's natvis, which knows the field's element type (a `Synthetic` with
  `IndexListItems`). Locals and parameters have no such struct, so in debug
  builds they have a type of their own (asked for by the owner, 2026-10-26,
  over showing their count only): see Typed list locals, below. Enum fields likewise (C's enums are `int32_t`
  typedefs): a `Synthetic` per member. Maps and sets: `CustomListItems` over
  the live entries (`tide_order > 0`), `[key] = value`. Also: entities
  (`Entity(3:1)`, `none`), `tide_str`, `T?`/`T fails E`, arrays, vectors,
  `tide_color`, and the engine's per-function caches (`tide_list_cache`,
  `tide_map_view`) shown as "the engine's" with no children.
- LLDB's Python matches the typed locals' structs too (`TIDE_LOCAL_LISTS`,
  `TideLocalList`), and the other list locals and parameters (async code's,
  an Action's function's, `mut` ones) by their frame (`TIDE_LOCALS`, by
  function C name and local name), and a
  struct field's list through a registry by load address (`TideStruct` fills
  it as it makes children). DWARF keeps typedefs, so enum summaries go by the
  enum's typedef name everywhere. The runtime part is
  `compiler/src/debug_views.py`, embedded in tidec (`cmake/embed.cmake`, also
  in `docs/playground/CMakeLists.txt` for `tidec_browser`), followed by the
  game's tables. `tide_register` also sets `step-avoid-regexp` to every
  `tide_` name but the Tide-code prefixes, an ERE built by
  `_not_starting_with` (LLDB's regexes have no lookahead).
- Natvis for a type no program has is ignored, so the generator writes one for
  every struct-like declaration with a list, map, set or enum field, map
  entries included, without tracking which are emitted.

**Builds.** `tide`'s native non-release builds (`debug_views` in `build.c`:
not web, Android, managed or `--release`) pass `.visualizers`, link the natvis
into the PDB (`-Xlinker -Xlink=-natvis:<gen>/game.natvis`: lld's MinGW driver
passes it to its COFF linker), and put `<stem>.natvis` and `<stem>_lldb.py`
beside the output (`place_debug_views`): `tide build`'s program,
`tide run`'s and `tide unity`'s `game-<n>` libraries (as `game.natvis` and
`game_lldb.py`, the newest build's; each build's own natvis is in its `.pdb`).
On macOS a standalone debug program also gets a dSYM (`dsymutil` from `PATH`)
with the LLDB views in `Contents/Resources/Python`, which LLDB loads with
`target.load-script-from-symbol-file`; watched libraries go without, so a
reload doesn't wait for dsymutil (LLDB reads their debug map's objects in the
game's cache). `tide_add_game(... VISUALIZERS)` does the same for a CMake
game (`/NATVIS:` on Windows' own target).

**Hot reload and Unity.** Each library has an absolute `.pdb` path, so the
host's `loaded-<n>` copy (`reload_library.inc`) finds its own `game-<n>.pdb`
with that build's natvis. Breakpoints bind when the module loads. The host
deletes `game-<n-2>.pdb` with its library; a debugger holding it open keeps it.

**Editors.**

- VS Code (`tools/tide-vscode/src/debug.js`): `contributes.breakpoints` for
  `tide`, and the `tide` debug type (`resolveDebugConfiguration`, before
  variable substitution, doing `${workspaceFolder}` itself, so VS Code resolves
  the result again with the native debugger's provider). Configurations:
  Attach to Unity (Tide) (Unity editors from `tasklist`/`ps`, each one's
  project from its command line's `-projectPath`, picked when there are
  several), Debug Tide game (`tide build <game> -o <game>/.tide/debugger/<name>`
  then launch) and Attach to Tide game (tide run) (the host in the game's
  `.tide/<configuration>/run/<n>/`). They become `cppvsdbg` on Windows (with
  `visualizerFile`; the natvis is in the `.pdb` too) or `lldb-dap`/CodeLLDB
  (`initCommands` importing the views) elsewhere. They refuse in Restricted
  Mode: debugging runs the game's code, and LLDB its Python.
- JetBrains (`tools/tide-jetbrains`): `TideBreakpoints` implements CLion's
  `cidr.debugger.lineBreakpointFileTypesProvider` with TextMate's file type
  (what `.tide` files are; so other TextMate files take breakpoints too),
  registered in `tide-cidr.xml` behind an optional dependency on
  `com.intellij.modules.cidr.debugger`. It compiles against a stub interface
  (`src/cidrStubs`, `Set<?> getFileTypes()`, the real method's descriptor),
  never packaged; Plugin Verifier checks it against CLion. `TideAttach`:
  Run > Attach to Unity (Tide) and Debug Tide Game (starts the Tide run
  configuration when the game isn't running, waits for its host, at most
  three minutes for a first build, and attaches), both through the platform's
  `XAttachDebuggerProvider`, preferring a native provider. CLion has no
  launch-under-debugger configuration a plugin can make without its own API,
  so Debug Tide Game attaches to `tide run`'s host: code before the attach
  (first `Spawned` handlers) runs again after Ctrl+R.
- Visual Studio: documented (attach to `Unity.exe`, native; open `tide
  build`'s `.exe`; the `.natstepfilter`), not scripted.

## Tests and where they ran

- `tidec_debugger_natvis` (Windows' own target only: `WIN32 AND NOT MINGW`):
  `compiler/tests/debugger/fixture.tide`, built with `VISUALIZERS` at `-O0`, run
  under `tide_dbgeng_drive` (`dbgeng_drive.c`: Windows' own `dbgeng.dll` from
  System32, the engine of WinDbg/cdb, with its natvis support; no install).
  `natvis_test.cmake` finds the lines by `// [marker]` comments and checks: the
  natvis embedded in the PDB, a breakpoint on a `.tide` line with the system's
  frame at it, a source step landing on the next Tide line, a breakpoint in a
  function with its caller at the call, locals by Tide name, a local list's
  and a list parameter's elements through their own types, list field elements, a list of structs, text from the heap, an enum
  name, an entity, a map's entries and a set. Passes locally (Windows 11, clang
  23.1.2, dbgeng 10.0.26100).
- `tidec_debugger_lldb` (where `TIDE_LLDB` is found: Linux by default, any
  platform with `-DTIDE_LLDB=`): the same fixture and checks under LLDB with
  the views, plus local and parameter list elements, no columns and hidden
  caches. Passes under WSL Ubuntu 24.04 with apt.llvm.org's LLDB 23.1.5
  (unpacked without root; the checkout's own Linux build, `TIDE_PLATFORM=OFF`).
  CI's Linux jobs install `lldb-$major` for it (`build.yml`); macOS isn't
  registered (finding Xcode's LLDB, and its permission to debug on a runner,
  are unverified), nor Windows' LLDB, which reads no thread-locals from a PDB.
- By hand on Windows: `tide build` and `tide unity --once` of a scratch game
  from a scratch installation of this checkout: natvis stream in each
  `.pdb`, `game.natvis`/`game_lldb.py` beside; a host loading `tide_embed.dll`
  as Unity's `TideRunner` does, with the game as `loaded-1.dll`, stopping at a
  `.tide` breakpoint with Tide's frames and natvis values.
- The Unity editor itself (6000.6.0f1, batch mode, the Sample's PlayMode
  tests, on a scratch copy with this checkout's tide on `PATH`), started under
  dbgeng: on `loaded-<n>.dll`'s load its `.pdb` and embedded natvis loaded, a
  breakpoint at `Bounce.tide:92` bound in `tide_system_Spinning`, and when a
  later test loaded the next `loaded-<n>` the breakpoint (`bu`) bound there
  and hit: the frame at `Bounce.tide @ 92`, `spinner`, `transform` and `time`
  by name, `tide_this` as `Entity(3:1)`, vectors as `(x, y, z)`. This found
  the quaternion view reading fields the struct hasn't (`value` holds them),
  now fixed and in the fixture. An editor attach from VS Code, CLion or Visual
  Studio wasn't tried; they use the same `.pdb` and natvis.
- Every game's generated C (408 `.sources` lists of this build, base tidec
  vs this one): `--no-line` output identical but for the simulation identity,
  which hashes the compiler's sources; and in line mode, compiling each
  `game.c` with and without `TIDE_DEBUG_PLUMBING` gives the same `.tide` line
  rows (`llvm-readobj --codeview`), so `nodebug` drops no line of Tide code.
- Sumo's `game.c`: 2.30 MB to 2.79 MB (pinned lines), object 12% smaller
  (plumbing without debug info), compile time the same within noise (0.7 to
  0.9 s at `-O0 -g`).
- VS Code: `test/debug.test.mjs` (manifest contributions, debugger choice,
  Unity and `tide run` process matching, configurations), 35/35 with the
  adapter suite; the bundle builds with esbuild. The real-editor host suite
  wasn't run (it opens VS Code windows). JetBrains: `TideAttachTest` and
  `TidePlatformTest.testDebugActionsAreRegisteredAndTideFilesAreWhereBreakpointsGo`,
  12/12 with `buildPlugin`; Plugin Verifier: Compatible with CLion 2026.2.3
  (CL-262.10968.117). Breakpoints in CLion's gutter and an attach in CLion,
  Visual Studio or VS Code weren't tried by hand.

## Typed list locals (2026-10-26)

The owner asked for local lists' elements in natvis debuggers too. With
`codegen_options.visualizers` (tide's debug builds, `tide_add_game
VISUALIZERS`, the new `TIDE_VISUALIZERS` CMake option for every native game),
generated C declares a `typedef struct tide_local_list<n> { tide_list list; }`
for each List<T> (number n, as the list helpers are) and gives it to:

- a list local (`S_VAR`, not `out`/discarded): `const tide_local_list<n> name =
  {<value>};`, mutable where the local is;
- a function's or method's by-value list parameter: the definition takes it
  as `const tide_list tide_arg_<name>` (`g->typing_params` around
  `gen_routine_signature`; prototypes keep the plain name) and copies it into
  `const tide_local_list<n> <name> = {tide_arg_<name>};` before its code;
- their captures by comparisons and parallel steps, which copy the struct.

References read `<name>.list` (`gen_expr`'s `E_NAME`). `typed_list_local` and
`typed_list_param` decide from the declaration alone (and no async code, no
code copied into an Action's call, no extern/worker/async functions, no
`mut`, `out`, `c_indirect` or task parameters), so a declaration, its
references and its captures agree whenever each is generated, and a site that
missed `.list` is a compile error rather than a different meaning: the struct
is the list's 4 bytes at offset 0, so even a pointer taken to it is the
list's. Without visualizers nothing changes (all 408 source lists' generated
C identical with and without line directives, as before).

Validation: every game of this build (415 source lists with the external
prototypes) generates and compiles with `-Wall -Wextra -Wpedantic -Werror`
with visualizers as without: 1360 typed locals, typed parameters in 84
games, 41 typed captures. The whole native debug suite with `TIDE_VISUALIZERS`
on (every native game typed, its own build tree) passes 2807/2807 (the natvis
test once its fixture was rebuilt after its expectations changed mid-build). The natvis test expands a local list and a list parameter
(`[0x0] : 1 [Type: int]`, through `tide_local_list0`), and the LLDB test
reads them as `(const tide_local_list0) recent = count = 3 { [0] = 1 ... }`.
Web builds have no visualizers, so their generated C is unchanged.

## Gaps

- In natvis debuggers, list locals and parameters that aren't typed (async
  code's, an Action's function's, `mut` parameters, `foreach` variables over
  lists of lists, `out` locals) show their count only; LLDB types them by
  their frame.
- Grids show their handle. Nested containers (a list of lists) show the inner
  ones' counts in natvis.
- The engine's inline helpers can be stepped into outside LLDB (and Visual
  Studio with the step filter).
- macOS: dSYMs and the LLDB views are built, not run here. Linux's LLDB test
  ran under WSL only; CI runs it next.
- CLion's TextMate-wide breakpoint gutter; Rider's native debugger shares the
  extension point and wasn't tried.
- A reload that changes types writes new LLDB views; LLDB needs them imported
  again. Natvis per build is in each `.pdb`.
