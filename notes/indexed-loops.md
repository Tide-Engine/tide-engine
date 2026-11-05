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
