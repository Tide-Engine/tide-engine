# Pure functions, merged calls and `cached`

Built 2026-11-13 from the owner's decisions of that day (docs/spec.md, Pure functions, has them under Decided;
everything else here is the building agent's pick, Provisional there). Three parts: the checker works out which
functions are pure (`compiler/src/purity.c`), repeated calls of one in a body become one (the same file), and a
`cached` function remembers its results (`compiler/src/cached_codegen.inc`).

Why: Mount & Rifle recomputes facts that never change, every frame. `Infantry.Eyepiece` scanned a scope's model
twice a frame for each scope, calling `PartMesh(catalog, def)` once for each scan, and the game keeps hand-made
caches in a `mut` local singleton (`PartBounds`) that several views write, one of the waits that chain its views.
A cached function is only read by its callers, so such a cache makes no view or system wait.

## Purity (`infer_purity`)

- **Where it runs.** In `check()`, after `check_build_code`: every body is checked by then and the marks the
  checker keeps of what code does through its calls (`acts`, `rolls`, `calls_c`, `reaches_extern`, `uses_io`,
  `draws` and the rest) are settled. The editors get it too (hover), as `check()` is what they run.
- **A whitelist.** `walk_expr`/`walk_stmt` go through a function's or method's body and the first thing they don't
  know to be free of effects makes it impure: `decl.pure`, `impure_why` (text that follows "it ..."), `impure_at`,
  `impure_through` (the callee, when the reason is a call). An expression kind, a `builtin_call` or a statement
  kind added to the language later is impure until it's listed in purity.c. Writes need no rule: with no `mut` or
  `out` parameter and a method not `mut`, `check_writable` already refuses every write but one to a `mut` local.
- **What's listed as pure:** literals, names of parameters, locals, constants, assets and types; fields, indexes,
  operators (a struct's operator is a call), `?:`, `??`, `is`, `try`, `!`, switch expressions, text with values in
  it, list and struct literals, `++` (of a `mut` local); `CALL_CONSTRUCT`, `CALL_ENUM_COUNT`, `CALL_TEXT`,
  `CALL_LIST`, `CALL_MAP`, `CALL_GRID`, `CALL_NEW_GRID`, `CALL_NEW_MESH`, `CALL_MESH_DECODE`, `CALL_JSON`,
  `CALL_RANDOM` (a generator value's own methods), `CALL_SKINNED`; `CALL_BUILTIN` on `Math`, `Path`, `string` and
  the numeric types (`pure_owner`), on a value (`position.Normalized()`), `Random(seed)`, and `Debug.Assert`.
- **A call of an `async` function or a worker** is "starts 'X', which runs as a task", awaited or not.
- **Never pure:** a value whose type is this machine's or the world's (`outside_type`: World, query, Action, draw
  list, textures, render targets, GPU lists, fonts, residents, `Devices` and its records), a lambda, anything
  with `expr.gpu`, a local field, an input edge, a task's state, a call with a block, `await`, a parallel loop, a
  loop over a query. Signature: `async`, worker, `audio`, a `mut` method, a function that takes an `Action` or
  borrows a query, a `mut` or `out` parameter, `Time`, `Session`.
- **Calls.** Each function's callees are kept with where it first calls them (`called`). After the walk, a fixed
  point makes a function that calls an impure one impure ("calls 'X', which isn't pure"); functions that call each
  other are pure when none of them does anything impure itself. Then `check_flags` looks at the checker's own
  marks for the ones still pure (it should find nothing: it's there in case the walk missed something the rest of
  the compiler knows), and the fixed point runs again.
- **`cached` errors** (`say_cached_errors`): no result, not pure (the root cause's text, a note for each call on
  the way to it with its line, then the line that does it), takes the input, gives a grid. `cached` on a method,
  an `extern` function or anything that isn't a function is the parser's error.
- **Generics.** An instantiation is a function like any other; `generic_instance` copies `is_cached` from the
  generic. A `cached` generic has a table for each instantiation.

## One call for several (`merge_pure_calls`)

- **Where it runs.** In `check()` right after `infer_purity`, when there are no errors, the program isn't an
  editor's (`asset_metadata_only`) and `tidec --no-call-merging` wasn't given (`program.calls_as_written`). After
  `plan_loop_indexes` (the loops' indexes are planned from the code as written), before `analyze_parallelism`, so
  the schedule, `tide schedule` and codegen all see the code as it will run.
- **It changes the tree.** The first of several equal calls is copied into a new `S_VAR` (`tide_same<n>`, not
  `mut`, `read` set), put before its statement in the block's `stmts`, and the call's node becomes an `E_NAME`
  bound to it, in place; the later calls' nodes become the same name. Everything after sees an ordinary read-only
  local, so captures, borrows, copies of lists (`gen_value_of` on a local isn't fresh) and debuggers need nothing
  of their own. In place, because other nodes may hold the same pointer (a settled call's `written`, a mesh
  draw's two members over one Mesh expression); `expr.pure_seen` keeps a node two places hold from being gone
  through twice.
- **Candidates** (`candidate`): `pure_call` (a `CALL_FUNCTION`/`CALL_METHOD` of a pure function with code that
  can't fail, no block, no task), a type a local can hold (`mergeable_type`: no void, grid, `T?`, failable,
  resource), and `stable` arguments. `stable` is the rule of what a body can't change: literals, constants,
  assets, non-`mut` locals with a value (not a `for`'s variable, a query's row, an `is` or `out` name), read-only
  parameters (anything but numbers and text only when the code can't change the world: `code_changes_world`),
  `this` likewise, fields, indexes, operators, `?:`, struct and list literals, text with values, constructors,
  `Math`, text methods, a list's `Contains`/`IndexOf`, a map's `Has`/`Get`, and pure calls.
- **Equal** (`pure_same_expr`): the same nodes, or the same kind, type and what they name (bindings by pointer),
  through their operands; a settled call's arguments are compared as settled, so named and left-out arguments
  are equal to written ones.
- **Where the first one counts** (`unsure`): not on the right of `&&`, `||`, `??`, in a side of `?:`, in a switch
  expression, a `while`'s condition, a `for`'s parts, a switch statement's sections, or a statement that is some
  other statement's whole body with no braces: there's no place before it that's sure to run and in scope.
- **Scope** (`known`): what a block found is forgotten at its end. `merge_apart` starts over for a parallel
  loop's body, a loop over a query or a grid, and a call's block, which other code may run on other threads or
  copy elsewhere.
- **Left alone:** a parameter's default (the same node at every call that leaves it out, `written_arg`), `++`
  and its assignment, an assignment's own place (what works out an index in it is still gone through), a
  property's assignment whole (its setter's call reads the getter's call as its place), `await`, lambdas, a
  loop's guards when an index was planned from them (`guards`: the body's leading locals and `if`s), `async`
  code, workers, `audio`, build functions, functions that take an `Action`, the input's methods, shaders and
  everything a shader's code reaches through calls, methods, operators and getters (`reached_stmt`: the
  shader's writer reads the same tree).
- **What "the code can change the world" is** (`code_changes_world`): it sets through the `World` (itself or a
  helper: `world_sets`, a `world_uses` that writes, a `mut World` parameter), reaches C, starts a task, or has a
  query whose rows it can write (one of them may be the entity it runs for). Then only numbers and text among
  its parameters are stable.
- **Order of the new locals.** Several can go before one statement: by when their calls were found
  (`available.found`), so a call inside another's arguments comes first and the outer one's local reads it.

## `cached` (`cached_codegen.inc`)

- **Shape.** `gen_routine` writes a cached function's code as `tide_cached_run_<function_...>` and then
  `gen_cached_wrapper` writes the function's own name: build the key, `tide_cached_find`, return the kept result
  or run the code and `tide_cached_place` it. `cached_routine` says which functions get one (cached, pure, a
  result the table can keep); `tidec --no-cached` turns all of it off.
- **Generated C calls no runtime function that wasn't there.** The table's code is helpers written into the
  game's C (`gen_cached_tables`): `tide_cached_lock`/`unlock` (a spin on an atomic, as `tide_client_memo_lock`),
  `tide_cached_begin`/`value`/`text`/`handle`/`pages`, `tide_cached_entries`/`find`/`place`/`let_go`. They use
  `tide_hash*`, `tide_alloc_zeroed`, `tide_heap_read`, `tide_handle_where`, `tide_pack_holds`,
  `tide_pack_generation`, `tide_text_use`, `tide_heap_run_enter`/`leave`, `tide_heaps_free`, and for results that
  hold text or lists the walkers workers use (`gen_worker_snapshot`, `gen_worker_own`).
- **The table** (`tide_cached`): a static for each function, zeroed until the first call. 64 sets of 4 entries
  (`TIDE_CACHED_SETS`, `TIDE_CACHED_WAYS`), each entry `[last used | key's hash | key | result]`, a lock, a clock
  and counts for each set. `tide_cached_free()` empties every table and `tide_cached_counts(&hits, &misses,
  &dropped)` adds them up; both are in the generated header under `TIDE_HAS_CACHED`.
- **The key** is bytes on the stack: 64 of head (the pack's generation, two hashes and the length of its text,
  four heaps' versions) and then each argument's own bytes (`sizeof` its C type; generated types' padding is
  written and zeroed). `gen_cached_handles` adds each handle a value has of its own to the key's heaps.
  - A handle in the pack: nothing more (the generation is in the head).
  - A handle in the scratch area: `ok = false`, and the wrapper calls the code.
  - A handle in a world: that owner's heap's version (`tide_cached_pages`: `used`, and each page's `hash` and
    `hashed`, for the owner's first heap and each of its runs' heaps), once for each owner, up to four. A page
    whose `hashed` is `UINT32_MAX` has no hash: `ok = false`. Nothing is hashed and nothing is written, as other
    threads read the same pages; `tide_page_hash` (page.c) stores the hash, then `hashed`, with a release, so a
    reader that sees `hashed` set reads the hash that goes with it.
  - What a list's elements hold in turn isn't walked: it's in the same owner's heap, whose version covers it.
- **Why the version is right.** A value's bytes hold its handles, and a block changes only through its page
  being marked (`tide_heap_write` and the rest set `hashed = UINT32_MAX`) until the tick's end hashes it again.
  Same handles and same page hashes are the same contents, in whichever world; a world rolled back has the
  pages it had. The hashes are 64 bits, as the world's own hash is. A system that writes one field of a
  component while another reads a list of it (the schedule goes by fields) makes that owner's pages unhashed,
  which the reader sees as no version, or, if it read them a moment before, as the version they had, which is
  still what its own list is.
- **Results that hold text or lists** (`holds`): the table has `tide_heaps` of its own. Keeping one: a copy all
  in the scratch area first (`gen_worker_snapshot`, while the thread's heaps are still the world's, so what the
  result borrows of a world is read there), then, with the table's heaps standing in for the local world's
  (`tide_text_use`), `gen_worker_own(..., TIDE_IN_LOCAL)`. Finding one: the same stand-in, and
  `gen_worker_snapshot` makes the caller's copy in its scratch area. Both leave the system's heap runs first
  (`tide_heap_run_enter(0)` then `tide_heap_run_leave(0)`, undone after): the table's heap is in no system's
  runs, and `TIDE_HEAP_CHECKS` ends a program that asks run 0's heap for a block from run 1. Such a table uses
  one lock (its first set's) and lets go of everything when a set is full (`tide_heaps_free`), as its blocks have
  no release walker of their own.
- **Locks** are held to look an entry up and copy its result out, or to store one; never while the code runs.
  A cached function that calls itself looks each smaller call up in turn.
- **Reload.** A game's library is loaded anew (native and web), so its statics start zeroed: the new code has
  empty tables. `tide_run_unload` (platform/include/tide/run.h) calls `tide_cached_free()` so the old library's
  memory goes with it.

## Tests

- `tidec_e2e_pure_calls` and `tidec_e2e_pure_calls_plain` (`compiler/tests/e2e/pure_calls.tide`,
  `pure_calls_test.c`): one game built as it is and with `NO_CALL_MERGING NO_CACHED`. Each prints its world's
  hash and what its view drew for 48 ticks, checks a call's own copy of a remembered list and struct, a snapshot
  rolled back to (the tables then hold later ticks' results), a copy and a packed world, two worlds sharing the
  tables, four threads against one (80,000 entities asking the same cached functions at once), and
  `tide_cached_free`. The cached build checks its counts: most calls found, some run, some let go of (700
  arguments a tick for one function).
- `tidec_pure_calls_same` (`pure_calls_same.cmake`): the two builds print the same lines, and the merged build's
  C has exactly the twelve locals it should (each by the call it holds), none for a call after a `mut` local
  changed, one on the right of `&&`, or one that rolls; the plain build has no local and no table.
- `compiler/tests/errors/cached_*.tide`: each reason a cached function isn't pure (rolls, `mut`, `out`, sends,
  spawns, destroys, World, Time, Devices, C, a call chain three deep with its notes, log, `Frame.dt`,
  `Random.seed`, async, a task, a local field, a texture, a parallel loop, a function value), no result, a grid, a
  method, `extern`, a component.
- `compiler/tests/shaders/cached.tide`/`.txt`: a frag that calls a cached function and a pure one twice, written
  as plain calls.
- `lsp_pure_functions` (compiler/lsp/tests/lsp_test.c): hover says pure, cached, and why not.
- The grammar test's `cached functions` case and its probe lines (tools/tide-syntax).
- `bench_cached`, `bench_cached_plain` (`--quick` as tests).

## Measured (2026-11-13)

Windows 11, Ryzen 9 7900X shared with other sessions' builds, clang 23, the `release` preset.

**A lookup against the call** (`bench_cached` and `bench_cached_plain`, `bench/cached`): 20,000 probes each
calling one function a tick, the best of 40 ticks, nanoseconds a call less a tick of the probes with no call
(8.7 ns each). "Found": 16 arguments between the probes. "Never found": `--spread 20000`, every call a new
argument, so it runs the code and keeps a result (and lets go of one).

| Function | Itself | Cached, found | Cached, never found |
|---|---|---|---|
| `Tiny(float)`: `x * 0.5 + 1` | -0.3 | 18.6 | 25.3 |
| `Curve`: 16 sines | 153 | 17.6 | 218 |
| `Curve`: 256 sines | 2,447 | 18.5 | 2,716 |
| `Size(Box)`: 24 bytes in | -0.2 | 19.9 | 26.3 |
| `Bounds(Catalog, int)`: 64 corners of a list | 507 | 36 to 40 | 707 |
| `BigBounds(Big, int)`: the same, a 6,000-part singleton (about 5 MB, 300 pages) | 517 | 1,331 | 2,191 |
| `Letters(string)` (with making the text) | 24.4 | 46.9 | 70.2 |
| `Outline`: a list of 64 `float3` out | 1,427 | 66.8 | 1,935 |

Both builds end on the same world hash. A plain lookup is about 18 ns. Each owner's heap an argument is in adds
a pass over its pages' headers: about 20 ns for the small catalog, 1.3 µs for 300 pages (4.4 ns a page, the
pages' headers being apart in memory; folding each hash in with two multiplications rather than a call of
`tide_hash_more` changed nothing, so it's the reads). A list result costs its copy.

**From 8 threads** (`--threads 8 --n 200000`, wall nanoseconds a call): found, `Tiny` 8.9, `Curve` 16 sines 9.2
(22.4 itself), 256 sines 10.4 (361 itself), `Bounds` 13.3 (105 itself), `BigBounds` 204 (105 itself), `Outline`
189 (321 itself). One thread's 18 ns becomes 9, not 2.3: 16 arguments are 16 sets, and eight threads wait on
their locks. A table whose results hold lists has one lock, which is `Outline`'s 189.

**Every game's C** (old habit, codegen-diff over every `.sources` list in build/sanitize, 529 of them, the
prototypes' too, `--no-line`, with and without `--no-call-merging`): 480 generate the same C but for the
simulation identity, 45 have merged calls (193 calls in all; the most in inside_out, 16, and banked, 12), 4 can't
be compiled outside the build (their build code needs a C compiler). The full sanitize build compiled every one
of them with the merging on.

**Mount & Rifle** (a scratch copy of its `game` folder with junctions to its art, this checkout's sanitize tidec
with `--build-cc`; nothing written to its repository): compiles, with 52 calls merged in 35 functions' names,
`Gear.PartMesh` in `Infantry.Eyepiece` among them (its two scans read one `tide_same1`), `Campaign.CellOf` five
times, `TreeSize` and `Gear.Center` four each. Not run: timing `HeldWeapon` with `Eyepiece` and `MeshBox` marked
`cached` needs a release build of the engine with the game as an external folder and its battle script, which
wasn't done here.

## Gaps

- Calls that `fail` or give a `T?` aren't merged, nor calls on a query's row, and a call isn't moved out of a
  loop.
- A list made on the spot (and a struct that holds one) as a cached function's argument runs the function:
  nothing compares what's in it. So does any argument in a heap that changed earlier in the same tick.
- A cached function's version of a world list is its whole owner's: one entity's list changing is a new version
  for every entity's. Getting it reads every page's header of that owner's heaps (two numbers for each 16 KB), at
  every call: `bench_cached`'s BigBounds row is that cost. One counter a heap bumped at every change would make
  it one read, but that's a field of `tide_heap` every write path has to keep, which is the owner's to agree to.
- The local world's heap is never hashed (only a drawn list's own page is, by mesh.c), so a local singleton's or
  local component's lists never have a version and a cached function that takes one always runs. Hashing the
  local heaps at a frame's start would give them one, at the cost of going through every local page's header
  each frame (Mount & Rifle keeps tens of megabytes of local lists), so it wasn't done.
- A table whose results hold text or lists lets go of all of them when a set is full.
- No `cached` methods. No report of the tables in the stats drawer or `tide schedule`.
- Two threads asking for the same new result both run the function.
