# Indexed loops

Agreed with the owner on 2026-11-05: a loop over a query's rows or a list's elements visits only the rows that can pass the loop's own guard, whenever the compiler can prove which ones those are. Nothing is declared: the compiler reads the guard. `indexed foreach` and `indexed parallel` promise it, and are an error when the loop can't be indexed. The spec section is "Indexed loops" in `docs/spec.md`; it says what is decided and what is Provisional.

## Decided (owner)

- Indexes are inferred from the code. The game writes the plain loop with a condition; there is no `near`, `by` or attribute naming what is searched.
- The index only skips rows that cannot pass. The game's own check still runs on each candidate, and candidates are visited in the loop's normal order, so results are bit-identical to the plain loop.
- `indexed` is a prefix modifier on a loop: `indexed foreach (...)` and `indexed parallel (...)`. `indexed` always comes first. `indexed (...)` alone is an error that says to add `foreach` or `parallel`.
- `indexed` is a promise, not a request: plain loops are indexed whenever possible; `indexed` makes "can't" an error with the reason.
- Indexing works on lists too, not only queries.
- Parallel loops over a query's rows exist (`parallel (var b in boids)`), and combine with `indexed`.
- No separate "pair system": each side of a pair writes itself; one-sided effects go through `Send`.

## Provisional (agent picks)

### The analysis (`compiler/src/indexing.c`)

For each `foreach`/`parallel` over a query or a list, the checker looks for a **guard prefix**: from the start of the loop body, statements that do nothing observable (immutable locals whose initializers are pure), followed by one or more `if (C) continue;`, or a final `if (C) { ... }` holding the rest of the body with no `else`. A `break`, `return` or any effect before a guard ends the prefix; guards after that point don't count, since rows that fail them have already done something.

The proceed condition (every guard's negation, and the wrapping `if`'s condition) is turned into **necessary bounds on keys**. A key is a numeric scalar reached from the row: a field path from a query binding (`b.other.position.x`) or from a list element (`o.pos.y`, `items[i].pos.y` in a parallel list loop). Rules:

- `A && B`: both sets of bounds; `A || B`: the hull of bounds on keys both sides bound; `!A`: pushed in (De Morgan, comparisons flipped). A negation under floats lets NaN pass, which is safe: rows with non-finite keys are always candidates.
- `key OP inv` and `inv OP key` (`<`, `<=`, `>`, `>=`, `==`), and `!=` under a negation. Bool keys: `key` and `!key`.
- Float and double keys only: `key + inv`, `key - inv`, `inv - key` against an invariant; `Math.Abs(key - inv) <= inv`.
- Distances between a row vector and an invariant vector: `Math.Distance`, `DistanceSq`, `Length(P - K)`, `LengthSq(P - K)`, `Dot(D, D)` with `D = P - K`, and sums of squared component differences. Each axis is bounded by the radius (the square root of a squared radius).
- Immutable locals in the prefix are replaced by their initializers; functions whose body is a single `return` are inlined, parameters replaced by arguments of the same type.
- Invariant means it reads nothing the loop changes: constants, parameters and locals the loop body never assigns, fields of those, and pure `Math` calls. No calls to C, no `World` reads in a loop that sets through its `World`, no list indexing.
- Integer keys take no arithmetic on the row side, since wrapping arithmetic has no monotone bound.

The bounds are evaluated before the loop as doubles, with the guard's own float operations where they decide overflow (a radius squared in float that overflows means no bound). The runtime widens every bound slightly, so a bound found with exact arithmetic is never narrower than the float guard.

The source must also stay the same while the loop runs: nothing in the loop body (or the helpers it calls) may write the key's component (a query) or the list. Otherwise the loop isn't indexed, with that reason.

`stmt.index` holds the plan; `stmt.index_why` holds the reason when there is none.

### The runtime (`engine/include/tide/index.h`, `engine/src/index.c`)

An index over entries of 1 to 3 double keys, each entry carrying a 64-bit ordinal added in ascending order:
- 1 key: a sorted array of (key, ordinal); an equality gives ordinals already in order, a range is sorted by ordinal.
- 2 or 3 keys: a hashed uniform grid in compressed rows, sized from the keys' bounding box and count, with each entry's keys stored for an exact box prefilter. A box that covers more cells than entries scans the entries instead.
- Entries with a NaN or infinite key are candidates for every search. A NaN bound gives every entry; an infinite bound leaves that axis open.
- `find` returns candidate ordinals in ascending order into a caller's buffer with inline room, so a small search allocates nothing.

### Where an index lives, and when it's still right

- **A world's query** (systems, match handlers): a cache entry per loop site in the world (`tide_index_cache`, derived, never copied, hashed or packed, like `tide_rows_cache`). It holds a reference to every page it was built from (the key's lanes, sparse presence, the entity column) and each chunk's row count. Holding a page means a writer copies it first, so an unchanged pointer proves unchanged data, as for the rows cache. The entry is checked at each loop start and rebuilt under a lock when anything differs. An index on data that doesn't change (walls, pickups) lasts across ticks.
- **Rows made for views and local handlers**: rebuilt when the rows are made again.
- **Lists**: a world's list is checked by the pages of its heap blocks, as above; a local variable's list keeps its index for the code's run that built it.

### Parallel loops over query rows

`parallel (var b in boids)` gathers the selected rows (or the index's candidates) into an array, then runs its steps over it like a parallel list loop. Rows are read-only, as in `foreach`; steps follow the existing parallel-step rules.

## Runtime status (2026-11-05)

Built: `engine/include/tide/index.h`, `engine/src/index.c`. `tide_index_make`/`free`; `begin(dims)`, `add(keys, ordinal)` and `end()` build an index (1 to 3 double keys, other counts taken as the nearest; memory is kept across `begin` for rebuilds). `tide_index_find(ix, lo, hi, hits)` fills a `tide_index_hits`, which has room for 64 ordinals and 128 work numbers inside it, so a small search allocates nothing; it points into itself, so it isn't moved while in use. `tide_index_ordinals` gives every entry's ordinal, for a loop that needs them all. `tide_index_lock`/`unlock` are an `__atomic` exchange lock that yields while waiting, and do nothing on single-threaded wasm. A built index is only read by `find`, so threads can search one at once, each with its own hits. A search before `end` gives every entry.

Contracts: hits are ascending ordinals, once each: exactly the finite entries inside the box widened by `(|lo|+|hi|)·2^-16 + 1e-30` on each axis (an infinite side isn't widened), merged with every entry that has a NaN or infinite key. A NaN bound gives every entry; lo > hi gives only the non-finite entries. Results depend only on the entries and the bounds (cell sizes change the cost only), and all arithmetic is IEEE basic operations (cell sizing uses `sqrt` and a Newton cube root).

- 1 key: the finite entries, stably radix-sorted by key (11-bit digits, digits every key shares skipped, -0 as 0); two binary searches give the range. A run already ascending (an equality on whole-number keys) is given as it is; others are ordered.
- 2 or 3 keys: a **dense** uniform grid rather than the hashed one planned above. Cells are sized from the finite entries' bounding box and count for about one entry a cell (an axis thinner than a cell has one cell), then the axis with the most cells is halved until there are at most 2N cells, so memory is O(N) whatever the keys, including widths whose ratio underflows. Entries are counting-sorted by cell number `x + cx·(y + cy·z)` with their keys beside them, so each row of cells along x is one run of the arrays: no hashing, collisions or dedupe. A cell coordinate is `(x/2 - low/2)·scale` (halves, so a box from -DBL_MAX to DBL_MAX has a finite width), clamped before it's converted, and monotone, so the cells of the widened bounds cover every entry inside them. A box covering at least half as many cells as there are entries scans every entry in order with the prefilter instead.
- A search's entries are ordered by insertion up to 32, by a bitmap over the finite entries when k·16 ≥ N, and otherwise by a radix sort over the bits N needs.
- Sizes are checked (`bytes`; counts are 32-bit) and memory comes from `tide_alloc`/`tide_realloc`; running out ends the program.

Tests (`tests/test_index.c`, `tide_tests index_`): every search is compared with a scan of every entry (nothing inside the exact box missing, nothing outside a box widened 4 times more, ascending, once each) over random 1-, 2- and 3-key entries, some with 5% NaN/±inf keys, and boxes of every kind (small, large, one-sided, open, inside out, equal bounds, a NaN bound); empty and never-built indexes; exact counts for equality on whole-number keys; 1,500 entries at one point; clusters at 0, ±1e-310, 4.9e-324, 1e300 and ±1.7e308 with -0; widths whose ratio underflows (the grid's halving); indexes of only non-finite keys; rebuilds with other key counts; 200,000 entries through every ordering path. They pass under Windows ASan (`sanitize`) and WASI (`web-debug`). A mutation removing the hashed grid's cell check was caught before the grid became dense.

Benchmark: `bench/index/index.c` (`bench_index`; its test runs `--quick`, which checks the indexed loop finds what the plain one does under a float distance guard). Entries are spread one per unit, and each search is for a radius holding about 10, including the game's own distance check on each candidate, against the plain loop over every entry. Release, Windows, this machine:

| Keys | N | Build | Search | Plain loop | Candidates |
|---|---|---|---|---|---|
| 1 | 1k | 11 µs | 70 ns | 388 ns | 11.1 |
| 1 | 10k | 125 µs | 165 ns | 3.9 µs | 11.2 |
| 1 | 100k | 1.44 ms | 276 ns | 38.6 µs | 14.0 |
| 2 | 1k | 8.7 µs | 101 ns | 583 ns | 12.6 |
| 2 | 10k | 88 µs | 218 ns | 6.0 µs | 13.5 |
| 2 | 100k | 1.01 ms | 278 ns | 64.3 µs | 13.7 |
| 3 | 1k | 10.6 µs | 367 ns | 740 ns | 16.9 |
| 3 | 10k | 107 µs | 518 ns | 7.7 µs | 17.4 |
| 3 | 100k | 1.18 ms | 702 ns | 73.2 µs | 18.6 |

The hashed grid first built searched 2 and 3 keys in 260 to 1,270 ns; a run per row of cells halved that. A build costs about 20 plain-loop searches at 100k, so building every tick pays once a loop searches a few dozen times a tick, and an index on unchanged data lasts.

Gaps: nothing uses it yet (compiler, `tide_index_cache`). Cells are sized from the bounding box, so a dense cluster inside a wide box puts many entries in a cell (correct, slower). `add` doesn't check that ordinals ascend; a caller that breaks it gets misordered hits, never a memory error.

## Analysis status (2026-11-05)

Built: `indexed` is a keyword (`T_INDEXED`, `parser.c`): it takes the `foreach` or `parallel` after it and sets `stmt.indexed`; anything else after it is a parse error with the fix. `plan_loop_indexes` (`compiler/src/indexing.c`) runs once the program is checked (after `warn_dropped_values` in `check.c`) over every system, view, handler, function and method body, and sets `stmt.index` or `stmt.index_why` on each `foreach`/`parallel` over a query or a list. `indexed` on a loop over anything else, or on one it can't plan, is an error at the `indexed` with the reason, a note at the place the reason is about, and a note on what an indexed loop looks like.

How it decides, beyond the design above:
- **Seeing through names.** `look` follows the guard's read-only locals and inlines calls of functions made of read-only locals and a `return` (at most 4 deep, never recursively), their arguments copied in where the parameters were (`subst`, which copies only the nodes that change, and only of kinds it knows: names, fields, unary and binary operators, elements, `?:`, `Math` calls and constructors). An argument must have its parameter's exact type: an implicit conversion stops the inlining.
- **What the loop changes** is recorded as places (`b.velocity`, `items[_]`), not variables, so a loop that adds to `body.velocity` can still bound by `body.position`. A component parameter of the loop's code is changing when the loop sets that component through a World, directly or through a function that's handed the World (its `world_uses`).
- **A query's key must keep its value between runs of the loop.** The world's index is reused while the pages it was made from are unchanged, and a system writes its own entity's lanes in place once it has begun them, so the analysis refuses a query loop whose code writes the key's field of its own entity anywhere in its body (an assignment under it, `++`, a `mut` argument, a `mut` method, `World.Set` of the component). Writing other fields is fine: their write-back leaves the key's values as they were. A loop over a borrowed query checks every system that can call the helper.
- **Choosing keys** prefers two or three components of one vector bounded both ways, then an equality, then one key bounded both ways, then one bounded one way.
- **Tooling.** `tidec --schedule` ends with "Loops that skip rows, and whether an index finds the rest", one line per loop that starts with a guard or is written `indexed` (`print_loop_indexes`, through `each_guarded_loop`). The language server shows the same as a code lens above the loop (`write_loop_lens`).

Tests: `compiler/tests/errors/indexed_*.tide` (a bare `indexed`, no guard, an effect before the guard, a bound the loop changes, the system writing its own key field, `World.Set` of the query's component, a grid); `compiler/tests/schedule/indexed_loops.tide` pins the plans and reasons of the shapes above.

Gaps: methods (`body.InRange(o)`) aren't inlined, only functions; `||` of bounds on different keys gives no bound; integer keys take no arithmetic on the row's side (it wraps); a bound from a property getter or a call to C isn't worked out.

## Code generation (2026-11-05)

Built: `compiler/src/index_codegen.inc` (included by `codegen.c` before `gen_query_foreach`). Every loop the analysis gives a plan (`stmt.index`) is generated through an index, unless the game is built with `tidec --no-loop-indexes` (hidden; `tide_add_game(... NO_LOOP_INDEXES)`), which generates every loop plainly for differential tests and benchmarks. Results are the plain loop's: the candidates come in the loop's order (ordinals ascend with it), each row is read as the plain loop reads it, and the unchanged body (guard included) runs for each.

- **Bounds**: each key's lo/hi worked out before the loop as doubles (`gen_ix_bounds`): a leaf is `(double)` of the expression generated where the loop is (a vector's component with `.x`/`.y`/`.z`), `ADD`/`SUB` in double, `SQRT` as `__builtin_sqrt` (correctly rounded), `MIN`/`MAX` as `tide_ix_min`/`max`, NaN when either side is. A missing side is infinite. Leaves are pure and invariant, so working them out first is unobservable. Integer keys need no conversion care: Tide has no unsigned ints but `byte`, which promotes exactly, and int/float conversions are explicit, so comparisons are mathematical and `(double)` is monotone.
- **A world's query** (`gen_indexed_query`; systems, match and local handlers, local-world queries, and borrowed live queries, each borrow variant its own site): ordinals are `archetype << 32 | row` (the table row, `chunk << shift | i`), in the order the plain loop goes. The site's facts are each admitted table's chunk count and, per chunk, its row count and its entity column, the presence columns the query's filters read and the lanes holding the keys (only those lanes: other lanes of the component can be written without the index noticing, as it doesn't read them), each page held. A candidate is checked again when visited (row below the table's count, not a hole, filters) and its bindings gathered with `tide_table_gather` from the table's current pages, as the plain loop reads them. Fewer than `TIDE_IX_LEAST` (32) rows: no index; the rows are listed and gone through the same way.
- **The cache** (`struct tide_index_cache`, a pointer at the end of `tide_world` and of `tide_local` in games with an indexed loop): one site (`tide_ix_site`) per loop and borrow variant, `tide_index_sites()` of them, defined at the end of the generated C once every loop is written. Derived: never copied (`tide_world_copy` leaves each world its own), hashed or packed (pack clears the pointer in the header; unpack clears it after its `memcpy`, after freeing the old cache); freed with the world. `tide_index_cache_prepare` runs serially at the start of each tick's run (`tide_world_tick_once`) and each frame (before `tide_local_hear`): it makes the cache, or makes it again when its ABI (`TIDE_INDEX_CACHE_ABI`) or site count differs (a reload), and counts the run (`epoch`). Each site's `plan` is a hash of what its code reads (archetypes, columns, lanes, key paths), so a reload that keeps the layout but changes a loop starts that site again. Outside a prepared run (no cache) a loop builds an index of its own each time.
- **Checking and building**: under the site's lock, the facts are compared as they come (`tide_ix_walk`); from the first that differs they're taken anew, pages held (a page whose count would wrap isn't held, and the site isn't trusted after: `full`). Pages are retained and released under one lock for every site (`tide_ix_refs`), as two sites can hold one page. A site whose facts hold searches its index outside the lock; one whose facts changed while other threads search its index builds one of its own for that search and marks the site not ready. Searches count themselves (`readers`).
- **A system's own loop** (`ix_fast`: a non-handler system's body, not borrowed, inlined, a task or a step): once its site is found to hold in a tick's run (`valid == epoch`), the rest of the system's entities search with no lock. While a system runs, only it could change what its loop reads, and the analysis rejects loops whose system changes the key field of its own entity, so it holds until the system is done. Without this, checking the facts under the lock for each of 10,000 entities made the benchmark's tick 22 ms rather than 9.5.
- **A view's rows** (`gen_indexed_rows`; a view's `tide_q<n>`, and a borrowed query whose source is a view's): a block-scope static site per loop, its facts `tide_ix_made` (counted up each time the view runner makes rows, `gen_system_run`'s fill hook), the rows' address and count: an index lasts for a view's run, for all of its entities. A local handler's rows (a tick's record) are gone through plainly.
- **A list** (`gen_indexed_list`): when it's a field of a singleton a system or a view takes read-only (not local, not `verified`), a site in the match's cache (systems) or the local state's (views) with facts the run (`epoch`), the world (views: shards draw several), the list's handle and count. Nothing can change such a list while the code runs (the schedule; the code only reads it), so the index lasts for the system's run, and for a view's frame. Lists in components, locals, parameters and anything else are gone through plainly: there's no cheap proof a list there didn't change, and pages of a world's heap can't be held from a system (heap writers run beside readers and change page references without a lock). Element order is the list's.
- **Parallel loops**: over a world's query (`gen_parallel_query_call`), the index's candidates are gathered into the rows array the steps share out (`gen_indexed_query` with the row type). Over a list, before the fork, an index of the list as it is (the loop's own: its steps change elements) marks the elements it gives (`tide_xm`, a bit each); a step for an unmarked element in the list does nothing, as its guard would have skipped it (`gen_par_nest`); out-of-range steps run as before. The four-lane batch path is off for indexed list loops. Over a view's rows: plainly, for now.
- **Memory safety**: keys may be NaN, infinite or huge (the index puts those in every search); a candidate is bounds-checked against the table when visited; hits live in a `tide_index_hits` freed however the loop is left (a `gen_open` cleanup, so `return` frees it); a restored world has other pages, so its sites rebuild; held pages keep a snapshot's pages alive until the site lets go (rebuild, world free).
- **Tests**: `tidec_e2e_indexed_loops` (`compiler/tests/e2e/indexed_loops.tide`): 3D distance guards over static walls (index kept across ticks) and moving movers (rebuilt every tick), a squared distance through a helper, an enum equality, a one-sided bound, an `||` band, a bool, lists of float3 and of structs in a singleton, a view's query, a query borrowed by a function from a system and from a view, NaN and infinite places, entities spawned and destroyed every few ticks, a parallel loop over a query and one over a list. `tidec_e2e_indexed_loops_plain` is the same game built `NO_LOOP_INDEXES`; `tidec_indexed_loops_same` (`indexed_loops_same.cmake`) runs both and compares their worlds' hashes and what the views drew, tick by tick, for 80 ticks. Each also checks that rolling back to a snapshot gives the same ticks again, that a world copy and a packed and unpacked world tick as the original does, and (native) that 4 threads give what one does. A mutation narrowing every bound by 0.25 fails the comparison. Pass under Windows ASan (`sanitize`) and WASI (`web-debug`).
- **Benchmark** (`bench/loops`, `bench_loops` and `bench_loops_plain`; release, Windows, Ryzen 9 7900X, one thread; the tick doesn't take threads at these sizes, as the scheduler's estimate counts a query loop as one step): 10,000 boids each counting the boids within 2 (a query, every boid moving every tick, so the index is built again each tick), and 10,000 agents each looking for obstacles within 2 in a list of 10,000 that never changes.

| Per tick | Plain | Indexed |
|---|---|---|
| boids and agents, 10,000 each | 353 ms | 9.5 ms |
| 10,000 boids alone | 86 ms | 7.6 ms |
| 10,000 agents alone | 273 ms | 1.8 ms |
| 1,000 of each | 3.16 ms | 0.74 ms |
| 200 of each | 134 µs | 30 µs |
| 50 of each | 9.6 µs | 6.6 µs |
| 20 of each | 1.5 µs | 5.4 µs |

  At 20 the loops are below `TIDE_IX_LEAST` and listed, then gathered row by row: about 4 ns a row more than the plain loop's column reads, 4 µs a tick.
- **Gaps**: a local handler's rows and a parallel loop over a view's rows go plainly; lists other than a read-only singleton's go plainly (a local list's index for its code's run isn't built); a cache in a hot-reloaded game's statics (views' rows sites) is left behind when the old library is unloaded (its indexes' memory only). A tiny loop gathers its rows one by one rather than reading columns: a plain loop for tiny tables would mean generating the body twice.
