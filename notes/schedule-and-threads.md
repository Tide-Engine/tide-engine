# The schedule and the tick on threads

## `previous` bindings: a query that reads the tick's start (2026-11-13, evening)

The owner's task, after the day's three threads on the schedule: a system that changes a component and reads
other entities' through a query or its World stayed on one thread and waited for every writer (37 systems in the
tree), and Mount & Rifle worked round it by copying every soldier into a singleton's list each tick. He decided
the language: the word `previous` on a query binding, beside `verified`; explicit (unmarked queries are as they
were); it reads every entity's component as it was when the tick started, whatever has run since. Everything
else below is mine, Provisional in `docs/spec.md` (Read-only entity queries, `previous` on a binding).

```csharp
system BotSense(mut Bot bot, mut Trooper t, query (previous Trooper other) others)
```

### What it is

- **Where** (`check_previous_binding`, `compiler/src/check.c`; the parser reads the word where it reads
  `verified`, a name anywhere else): bindings of a system's queries (and since the same night a match handler's, and the World: below). An error with the fix for a filter, a
  system's own parameter, a view's or local handler's query, a function's own query
  parameter, `mut` (either side of the word), with `verified`, and a local component. `previous Body was, Body
  now` in one query is the existing "appears twice": a function handed a query finds its bindings by their
  component, which must be one.
- **Which entities**: the world's as it is. While systems run no row comes, goes or moves (spawns, destroys,
  adds, removes and holes filled all wait for the tick's end), so the world's tables have the rows they started
  the tick with, and a row of the world's table is the same row of the kept one. That is the whole rule for
  mixing: one walk of the world, each binding read from its own time. A row past what was kept (trusted C that
  spawned mid-tick) is left out, never read.
- **Not the schedule's** (`decl.previous_reads`, apart from `world_uses`): a `previous` binding adds no
  `world_use`, so `accesses_of` has no access for it, `find_conflicts` no wait in either direction,
  `world_reads_own_writes` and `rows_independent` nothing that keeps a system's entities on one thread, and
  `rows_alone` nothing against `restrict`. It needs no place in the lane masks either
  (`system_query_bytes`): what it reads is held by a reference, never a page another system could be replacing.
  Owned prediction's selection still counts it a read (`owned_code`): what the tick started with is what the
  tick before's writers left, so they're wanted.
- **`tide schedule`** says it apart: `reads any entity's Body as the tick started, through queries: no wait over
  it` (`previous_read_names`, `put_world_access`). The editors: a lens `Query: reads Body as the tick started`,
  the system's hover, the word's own hover and the binding's, `previous` offered in a system's query and the
  match's components after it, a keyword's token, no rename (`OCC_PREVIOUS`, `describe_previous`,
  `put_world_lens`, `describe_world` in `compiler/lsp/analysis.c`). `tidec --outline` marks such an access
  `"previous": true`. Both TextMate grammars scope the word `storage.modifier.previous.tide`.

### What a tick keeps (`compiler/src/previous_codegen.inc`)

- **Pages, held.** A component some query binds `previous` is `decl.kept_previous`. A game with any has
  `struct tide_previous *tide_previous` at the end of its `tide_world` (derived, as the loops' index cache is:
  not copied by `tide_world_copy`, cleared in a pack's header and after unpack, freed with the world) holding a
  `tide_table` for each archetype that stores a kept component. `tide_previous_keep(w)` runs serially in
  `tide_world_tick_on`/`_once`, before `tide_run_systems`: `tide_table_keep` (new, `tide/table.h`) gives each
  kept table the world's counts and holes and a reference to each page of the columns named (the entity column,
  the presence bytes of every sparse component, every lane of the kept components), NULL for the rest.
  Whoever writes one of those pages that tick sees it shared and writes a page of its own
  (`tide_table_lanes_begin`'s `apart`, `tide_table_lanes_begin_pieces`, `tide_table_scatter`), which is what
  every system already does beside a session's ring of snapshots. `tide_previous_release(w, false)` runs once
  the systems have joined, before `tide_apply_commands`: the pages are the world's alone again while the tick's
  changes apply. So nothing is kept between ticks but the kept tables' own arrays.
- **Heaps.** Where a kept component holds text, lists, maps, sets or grids, its owner's heap is copied whole
  for the tick (`tide_heaps_keep`, new in `tide/heap.h`: one owner's heap with its runs into a set of nothing
  else, sharing pages as `tide_heaps_copy` does), freed at the release. A row's `previous` value has its handles
  retagged as a `verified` value's are (`gen_confirmed_copy`: `tide_confirmed`, `TIDE_IN_VERIFIED`; a list or
  map whose elements hold text is copied to the scratch area with that text retagged), and each of the tick's
  threads says the kept heaps are what that names (`tide_text_use_verified`, in `tide_previous_keep` and
  `tide_prepare`). A tick had no other use for that fourth `where`: `verified` is an error in match code, and a
  frame sets its own before its views. Everything that changes a block finds its heap with `tide_heap_of`,
  which the kept one never is, so nothing writes there; copying a `previous` text or list into the world copies
  it, as a borrowed block always is.
- **Reads** (`gen_query_walk`, `gen_indexed_query`): for a `previous` binding the lanes come from `tide_qp`,
  the kept table of the row's archetype, in every path: the cached column pointers of a system's walk, the
  narrowed copies (`match_query_lanes`), a candidate's gather, a parallel loop's gathered rows. Such a binding
  never takes the path that reloads pages after a `World.Set` of its component (`query_changes_binding`): the
  kept pages don't change. A function handed the query (`g->borrow`) reads its source's bindings' time
  (`query_binding_previous`), each variant generated for its source as before.
- **A reload** (hot reload carries a world to new code): the struct says which tables it holds (`plan`, a hash
  of the kept archetypes); another build's is let go unknown (it holds no page between ticks) and made again.

### The index, once a tick (`index_codegen.inc`, `indexing.c`)

- **Planned whatever the system writes** (`loop_index.previous`, `path_previous`, `query_source_still`): a key
  in a `previous` binding can't change while the code runs, so the two refusals that guard a kept index (the
  system changes the key's field of its own entity; the loop sets the component through its World) don't apply.
  For a loop in a function handed the query, a system whose queries take the component `previous` alone no
  longer counts against it. This is what a flock needs: `bird.position += ...` in the system whose loop finds
  birds by `position`.
- **Made from the kept tables** (`ix_query.before`, `ix_table`): the site's facts and its rows are the kept
  tables' pages, so within a tick's run the site is checked once under its lock and searched with no lock after
  (`tide_ix_hold`'s `fast`, for any code now, not only a system's own loop), and across ticks an index of rows
  nobody changed is kept, as before.
- **Shared** (`ix_site_shared`): a site of the tick's start is found by all its index is made from (the
  archetypes, columns, lanes and key paths, as text), so every loop in the game that searches the same query by
  the same keys has one site, in a system, in another system, or in a function either hands its query to. In
  the fixture `Flock`, `Crowd` and `Nearest` (called from `Flock`) share site 0.
- A search that finds most rows still goes through every row, and fewer than 32 rows are gone through plainly,
  as for any indexed loop. `tide schedule` says `indexed by o.other.position.x, ... as the tick started: its
  index is made once a tick`.

### Tests

- `tidec_e2e_previous` (`compiler/tests/e2e/previous.tide`, `previous_test.c`): 1,500 boids in two archetypes
  with a sparse tag, a component of 27 lanes holding text, a list, a list of text and a map. Three systems that
  run first and read the world as it is, and three that run after every writer and read `previous`, fill sums
  that must be equal every tick (places; names, lists and maps; another component through a query of two
  bindings with a filter), with the same query unmarked giving another number. `Flock` changes the component it
  searches by position, across threads; `Crowd` searches the same way from another system; a function is handed
  the query; `Echo` copies a `previous` row's text and list into its own row and sends them in an event, while
  `Rename` changes them in runs on threads; a `parallel` loop over `previous` rows sends; entities are destroyed,
  spawned and get a sparse tag added and removed every tick. Ten ticks on a pool that's one thread saying it
  has eight and three rounds on the platform's, in place and beside a snapshot of the tick before: hash and
  singleton bytes equal one thread's each tick, the snapshot still hashes as it did. A copy, a pack and unpack
  (whose `tide_previous` is NULL) and a snapshot rolled back go on tick by tick as the world does.
  `tidec_e2e_previous_plain` is the game built `NO_LOOP_INDEXES`, and `tidec_previous_same` compares what the two
  print each tick (`indexed_loops_same.cmake` with `WORD` and `LEAST`). With the kept tables swapped for the
  world's own (`previous_table`), it crashes on its first tick under ASan.
- `compiler/tests/schedule/previous.tide`: `Crowd` (unmarked) on one thread after every writer, `Flock` (the
  same with the word) with only `both write Body.position` against `Move`, `Tally` at stage 1 with no wait,
  one query of each kind in one system, a function, a parallel loop, and the index lines.
- Errors: `compiler/tests/errors/previous_*.tide` (filter, `mut` before and after, own parameter, view, handler,
  function, local, twice).
- `lsp_previous` (`compiler/lsp/tests/lsp_test.c`): completion in and out of place, the three hovers, the
  lenses, no rename, the error with the word still explained, a parameter named `previous`. Grammar cases in
  `tools/tide-syntax/test`.

- `table_keeps_columns_as_they_were`, `heaps_keep_one_owners_heap` (tide_tests): the two runtime functions,
  references and all.

### The World, match handlers, and only the lanes read (2026-11-13, night)

The owner, of the first version's "not built" list: "lets try to gap [it]; specially the World reads ... we
should maybe have a ReadPrevious variant of Read or whatever we have in there". Built, all three; the names and
the rules are mine, Provisional in the spec.

- **`world.GetPrevious<T>(e)`, `HasPrevious<T>(e)`, `TryGetPrevious<T>(e, out var was)`** (`check_world_call`):
  `Get`, `Has` and `TryGet` of the entity as the tick started. The checker keeps the call's name as `Get`, `Has`
  or `TryGet` with `expr.world_previous` set, so everything that goes by those names (what's pure, what has an
  `out`, the copies) is as it was; the language server puts the written name back for its hover, signature and
  tokens. No `world_use` is noted: the component joins `decl.previous_reads`, as a binding's does, so the
  schedule has no access for it and `world_reads_own_writes` nothing to keep a system on one thread
  (`schedule/previous.tide`'s `Chase`: `mut Body`, `world.TryGetPrevious<Body>`, across threads). A function's
  reads reach its callers (`check_world_passing`: `previous_reads` and `previous_read_at` go up the calls, and
  such a function is handed the match's world), where a view, a local handler or a task that calls one is an
  error, as the direct call is.
- **One entity by its handle** (`tide_previous_<C>(w, e, out)`, `tide_previous_get_<C>`,
  `tide_previous_tryget_<C>`, generated): nothing kept (outside a tick, or once it let go) is false; else the
  entity's place as the tick started, the kept table of that archetype, its presence byte for a sparse
  component, a row below the kept count, the lanes gathered and its handles retagged. `out` NULL only asks.
- **Match handlers** (`check_previous_binding` lets them; `program.keeps_previous_entities`): a handler runs
  while the tick's changes apply, so the world's row is no longer the kept table's. A game where a handler has
  a `previous` binding, or a handler or any function reads through the World, also keeps the entity table as
  the tick started (`tide_entities_copy` into `tide_previous.entities`, pages shared) and lets go after
  `tide_apply_commands` instead of before it. The lookup above then goes by those entities, in systems too
  (the same places). A handler's query walks the world as it is and asks the lookup for each `previous`
  binding of each row (`late` in `gen_query_walk` and `gen_indexed_query`, by the code the query is a
  parameter of: a function handed a handler's query does the same): an entity that had none isn't a row. So
  one spawned or given the component this tick is left out, one that changed archetype is found where it was,
  and what handlers write this tick is never seen. `indexing.c` plans no index for a handler's loop keyed by
  such a binding, and says why.
- **Only the lanes read are kept** (`previous_wants`, `gen.previous_lanes`, `gen_previous_keep`): each place
  that writes a read of a kept table says which lanes (a walk's and a candidate's copies by `match_query_lanes`,
  an index's key lanes), or the whole component (a row that can escape, a parallel loop's rows, the lookup by
  handle, a handler's binding). `tide_previous_keep` is declared with the struct and defined once every loop is
  written, with those lanes alone; `bench_flock` keeps 7 of `Bird`'s 8. A read that didn't say so would find no
  page: the three kinds of place are all there are, and the fixture's ASan runs go through each.
- Tests: the fixture's `Follow` (a system that changes `Boid` and asks its friend's through all three calls),
  `AskAll` (every entity through the World and through a function, against what `Remember`, first in the tick,
  wrote into a `Memo`) and `Late` (a handler, after the tick's destroys, spawns, sparse tags and moves between
  two archetypes: a query of two `previous` bindings beside a current one, and the World's three calls; a
  spawned entity is no row, has nothing, and `HasPrevious` says so). `Tally.wrong` is 0 every tick on every
  pool, and fewer rows reach the handler than the systems asked. Errors: `previous_world_view`,
  `previous_world_function`, `previous_handler_indexed` (and `previous_handler` is gone: it's allowed).
  `schedule/previous.tide` gained `Chase`, `Lead` (through a function) and a handler. `lsp_previous`: the three
  methods offered and explained, the lens `World/query: reads Body as the tick started`, a handler's binding.

### Measured

**A flock** (`bench/flock`: one component, one system that steers each bird by the birds within 2 and moves it,
about ten in sight; `bench_flock` with the word, `bench_flock_now` the same game without it, which is on one
thread and can't index by the place it moves; `bench_loops --agents 0` is the workaround there was, the place and
the velocity in two components and two systems). Release, Windows, 7900X, best of three, ms a tick; each game's
hash is the same on 1, 5 and 12 threads, in place and beside a copy:

| Birds | Unmarked, 1 thread | ...12 | `previous`, 1 thread | ...12 | ...12, beside a copy of each tick | Two components, 1 | ...12 |
|---|---|---|---|---|---|---|---|
| 1,000 | 7.93 | 8.40 | 0.74 | 0.18 | 0.21 | 0.80 | 0.84 |
| 10,000 | 886 | 998 | 9.33 | 1.54 | 1.57 | 9.54 | 9.55 |
| 100,000 | | | 144 | 17 to 19 | | | |

So the word is worth the index (95 times at 10,000 on one thread) and then the threads (6 times more on twelve),
and keeping the tick's start costs nothing that shows: beside a copy, as in a session, the pages are shared
already. The two-component workaround got the index and stays on one thread at these sizes (tidec reckons a query
loop as one step, so the tick isn't thought worth threads).

**Mount & Rifle** (its working tree at a145828, as the sections below built it: `BATTLE_TEST`, `ARMY`, release,
12 threads, hidden, `--host --seed 42 --frames 2400 --profile-warmup 1500`, saves in a scratch folder). Before is
the game as it is on this checkout. After is one file changed in a scratch copy, nothing in its repository:
`Infantry.Disperse`, `Enlist` and `Gather` (crowd.tide: the list emptied, each soldier adding himself, the grids
made, all after `Move` and `BotAct`) made one `Gather(mut Crowd crowd, mut SceneryCollision scenery, Time time,
query (previous Trooper t) men)` that runs before `BotSense`, `BotAct` and `Move`. Five runs each, alternating,
each profile against its own build's `tide schedule --measured`; means, ms a tick:

| | Before | After |
|---|---|---|
| Systems, stages deep | 122, 74 | 120, 72 |
| Waits listed | 1,777 | 1,753 |
| Systems one after another | 3.97 (3.58 to 4.31) | 3.93 (3.74 to 4.13) |
| The longest chain of waits | 3.38 (3.04 to 3.67), 85% | 3.01 (2.89 to 3.12), 76% |
| ...systems long | 93 | 91 |
| The match's tick on the frame's thread | 4.05 (3.62 to 4.43) | 4.02 (3.79 to 4.23) |
| `Disperse` + `Enlist` + `Gather` | 0.008 + 0.27 + 0.21 | `Gather` 0.41, off the chain |

What it says: **the chain is 0.37 ms shorter** (what `Enlist` and `Gather` were on it, and their two links), and
the gathering itself a little cheaper (a query's rows walked once, where a per-entity system pushed into a list);
**the tick is where it was**, as the game's tick is still nearly all one chain and the thread that runs it does
the systems one after another when nothing else is ready. What the rewrite changes in the game, which is why it's
a measurement and not a patch: `Fly` (the rounds) read the crowd gathered after this tick's `Move`, and would now
hit men where they stood when the tick started, a tick's worth of movement behind. The bots (`BotSense`,
`BotAct`) read last tick's crowd already, so for them it's the same data. And the grids stay: `NearestFoes` wants
the four nearest within 300 m, which a guard's radius can't say (most of a battle is within 300 m, so an index
by distance would give most rows and the loop would go through all of them). A nearest-few search is its own
feature, not designed.

### Gaps

- **`previous Body was, Body now` in one query** is refused (two queries do it): a function handed a query
  finds a binding by its component.
- **A handler's loop keyed by a `previous` binding isn't indexed** (the tick's index is of rows as they were),
  and each of its rows looks its entity up in the entities as the tick started; a function handed a handler's
  query goes through every row silently. A game where a handler or a function reads the tick's start copies its
  entity table (page references) every tick and keeps everything until the handlers have run, so the tick's
  one-row writes copy the kept pages they touch, as beside a snapshot.
- **Through the World a component is kept whole**, and a handler's bindings too: only a system's loops say
  which lanes they read.
- **No `SetPrevious`, and nothing for a system's own parameter** (`previous Body mine`): its own entity through
  the World (`world.GetPrevious<Body>(this)`) does it. Views, local handlers and tasks take none of it.
- **A kept heap is its owner's whole heap**, copied (page references) every tick and its set made again.
  Components C can write, and every component of a game with shards or hidden state, keep the match's one heap.
- **A list or map of text in a `previous` row is copied to the scratch area for each row** whose lanes of it
  are read (as a `verified` value's is), and a `parallel` loop over rows of such a component stays on one
  thread whether or not its steps read them.
- **Candidates are still sorted by identity** for each search (the walk's order), and each is looked up in the
  world's table as well as the kept one.
- **Nearest-few searches** (above), and a search radius that takes in most rows, gain nothing from the index.
- A sharded game's and a hidden-state game's `previous` queries build and weren't run: no fixture has one.
- Trusted C that spawns or destroys while systems run moves rows under a `previous` query: rows past what was
  kept are left out, others would read another entity's values of the tick's start.

Validation: the whole native sanitize suite (Windows ASan, `TIDE_HEAP_CHECKS`), 3,290 of 3,290, built without
the private prototypes (their build stalled this machine and nothing of theirs uses the word). No pinned world
hash moved, and a game with no `previous` binding generates the C it did (every new line is behind
`keeps_previous` or a binding's own flag). On the web (`web-debug`, under Node, with threads):
`tidec_e2e_previous`, `tidec_e2e_previous_plain`, `tidec_previous_same` and `tide_tests`. The grammar's tests
(378) and the docs site's build pass. Linux, macOS, MinGW and Android are CI's; Windows ASan doesn't look for
stack-use-after-return, which Linux's does. `bench_flock`'s two `--expect` hashes are from this machine's release
build: another platform that disagrees has found a determinism bug, not a stale pin.

After the night's additions (the World, handlers, lanes): the whole native sanitize suite again, 3,292 of 3,292;
on the web the fixture, its plain twin, their comparison and `tide_tests`; the docs site's build. `bench_flock`'s
hashes are what they were.

## What the owner decided from the measurements (2026-11-13)

Asked whether Mount & Rifle does by hand what a feature would do better (its own neighbour grid of soldiers made
each tick, `Enlist`/`Gather`/`NearestFoes`; facts of a model worked out every frame and caches kept in a `mut`
local singleton several views write; `if (time.tick % n != 0) return;`; `[After]`/`[Before]` on views for what's
drawn on top), four proposals were put to him. His rulings:

- **`previous` on a query binding**, the component as the tick began: built in `t3/previous-bindings`. The word and
  that it's explicit are his; start of the tick rather than of the system, and where it's allowed, are picks.
- **Purity is inferred, with no keyword, and used**: the same pure call made again with the same inputs in one
  body is made once. **Remembering across calls is opt-in** (proposed `cached`; the word isn't ruled on). Built in
  `t3/cached-functions`.
- **No rated systems** (`[Every(n)]`, `[Spread(n)]`): "adds a lot of confusion and features that we don't need".
  Measured first: the six systems that begin with a tick test cost 0.013 ms a tick together, and running `Roam`,
  `Travel`, `Outfit` and `Upkeep` less often is the game's own choice to make with a guard.
- **`[After]` and `[Before]` on views are run order**, as on systems. What's drawn on top "should be potentially a
  graphics API thing versus when a view runs": a layer or order the draws name. Not designed; parked, as views
  beside each other gain that game nothing yet (below).

## A system's own rows' text and lists across threads: a heap in runs (2026-11-13, later)


The owner's task, one of three threads on the schedule that day: a per-entity system that changes its own rows'
text, lists, maps, sets or grids ran its entities on one thread, because a heap hands out blocks in a fixed order
and their offsets are in handles, hashed and packed every tick (82 systems in the tree, 18 of Mount & Rifle's).
Required: the world after the system bit-identical to one thread's on every machine and thread count.

Three ways were weighed, and the third built:

- *Blocks of the threads' own, renumbered at the join* and *sizes logged, offsets given at the join*: both leave
  handles that aren't final while rows run, so the join has to find and rewrite every one (in the rows' lanes, in
  lists of text, in queued spawns' values), one thread's work in proportion to what was allocated, and a walker
  generated for every type that can hold one. Not built.
- **A heap whose layout is by run of entities, not by time** (`tide/heap.h`, A heap in runs). The system's entities,
  in the order of their identities, are cut into runs: `min(32, count)` of them (`TIDE_HEAP_RUNS`), run `r` being
  keys `[count * r / runs, count * (r + 1) / runs)`. That depends on the world alone. Each run hands its blocks
  out of a heap of its own within the owner's, in its rows' order, so a block's offset is the same whichever
  thread had the run and whatever the other runs did. A task of `pieces` is runs
  `[runs * task / pieces, runs * (task + 1) / pieces)`; one thread's single task is all of them in order, through
  the same heaps. Nothing is rewritten and the join does next to nothing.

What it is:

- **An owner's heap has 32** (`tide_heap.runs`, made the first time one past the first is asked for; run 0 is the
  owner's heap as it was, where everything that isn't a run still allocates). A block says its run: 5 bits of its
  handle above the offset (bit 54 up, which offsets, 46 bits from bit 8, never reach) and of a `where` above the id
  (`TIDE_WHERE_RUN_SHIFT`). `tide_heaps_of` takes the run with the id, so `tide_heap_of(tide_handle_where(at))` is
  the block's own heap, and text.c, list.c, map.c and every inline read are as they were. Run-0 handles are the
  bits they were: a world no system changes in runs hashes and packs as before.
- **Which run code is on** is the thread's (`tide_heap_run_where`, set by `tide_heap_run_enter(run)` around each
  run's rows and put back by `tide_heap_run_leave`). Generated code says `TIDE_WHERE_RUN(TIDE_IN_MATCH, id)` for
  the memory of an owner some system changes in runs (`decl.heap_runs`, `decl_where`), which is the owner's
  `where` with the thread's run: outside runs that's run 0.
- **A field's own block may be in another run's heap** (the entity was in that run when it was made, or it was
  made outside runs). `tide_handle_in(at, where)` is "the field's own" now (same world and id, any run), which
  `own_block`, `drop_block` and `tide_text_release` use; they change and release a block through its own heap.
  Written, cut and added to within its room, a list stays where it is. A list in chunks that needs more chunks
  moves whole into the heap of the run that adds them (`grow_chunks`: `kept` only for the same run), as its block
  names its chunks by offset. A list side by side that grows was copied to a new block anyway.
- **Letting go of another run's block** (`tide_heap_release` when `tide_heap_away`): that heap's free lists are its
  run's thread's, so the block goes on this run's `away` list, is zeroed at the row's flush on this thread (`held`),
  and goes back to its own heap's free list in `tide_heap_runs_join`, runs in order. An asset list's lease in
  another run's heap is given up at the join too (`tide_heap_drop_later`). Outside runs a block goes to its own
  heap's `pending` and the owner's flush finds it (`runs_pending`).
- **Writing in place from any thread.** A page a snapshot shares is replaced with a copy by whichever thread gets
  there first (`write_shared_in_runs`: copied outside the heap's lock, put in the table under it, the loser's copy
  let go). The page it replaces **keeps the heap's references until the join** (`retired`): refs are how
  `tide_heap_write` tells a page is the heap's to write, and a thread that read the old page from the table a
  moment before would take it for the heap's own if the snapshot's were the only ones left. The run whose heap it
  is grows the table under the same lock. `tide_heap_write` reads the table as `tide_heap_block` does.
- **The compiler** (`rows_independent`, `system_splits_in_order`, `system_heap_runs` in `parallel.c`): a system
  that changes text or lists splits in order as one that queues does, unless one of its owners (`heap_owners`) is
  in the match's one heap (C can write it, or the game has shards or hidden state: no runs there), or holds a grid,
  or it starts tasks. `tide schedule` and the editors say which (`system_one_thread`). `gen_system` wraps the
  ordered walk in the runs' loop, flushes the run's heap after each row, and joins in the task when it had every
  run, else in `tide_done_<System>`, which such a system now always has.
- **Builds that check** (`TIDE_HEAP_CHECKS`): a block asked of a run's heap by code on another run ends the program.
- **Packed, hashed, copied**: a run's heap is another heap of the world, named by its id with the run above it
  (`tide_heaps_pack`, `tide_heaps_hash`), numbered slot by slot in a delta, copied and settled with its owner. An
  owner whose runs were never made costs one test (`after`).

Not grids, and why: a grid's directory names its chunks by offset as a list's block does, and its loops, windows
and caches (grid.c, about 900 lines) take one heap for the grid from a `where`. Moving a grid whole into a run's
heap when a row first adds a chunk is the same idea as the list's; it wasn't done here, so an owner that holds a
grid keeps its systems in order.

Tests:
- `tidec_e2e_row_heaps` (`compiler/tests/e2e/row_heaps.tide`): 900 bags in three archetypes whose system changes a
  list, text, a list of text, a map, a set and a list that goes past a page and is cut back; a system on one thread
  (it changes a singleton) changing the same lists between; two more in runs that spawn and send values holding
  text and lists; entities destroyed and spawned every tick, so the ones after them change runs. Fourteen ticks on
  a pool that's one thread saying it has eight, and five runs of fourteen on the platform's with a snapshot taken
  every tick: the hash and **the world's packed bytes** equal one thread's every tick; a pack, a delta over an
  unpacked base and a snapshot rolled back and run again on one thread give the same world. Every run's heap is in
  use after four ticks on one thread, and nothing a run kept is left. With every run given run 0's heap (a line in
  `tide_heap_run_enter`) the platform's pool crashes in it.
- `tests/test_heap.c`, four more: a run's blocks from its own heap's start and what a handle says; another run
  letting one go (readable until the flush, zeroed, back at the join, handed out again); a list in chunks written
  and cut where it is and moved whole when it needs chunks; a shared page's references kept until the join; copy,
  hash, pack and a delta with a run's heap.
- `tidec_e2e_heaps` as it was: its two per-entity systems are in runs now.
- Schedule goldens: `heaps` (two more systems: a grid's owner and one C can write, with the lines that say so) and
  `text` lost their "on one thread" lines.
- One world-hash pin moved, `rows` (`0x7c197dd64b4bb904`), and `row_lanes`'s expected call says `TIDE_WHERE_RUN`.

Validation: the whole native sanitize suite (Windows ASan, `TIDE_HEAP_CHECKS`), 3,283 tests: the two above failed
and are updated, `tide_server_smoke` failed in the full run and passes alone, `inside_out_tests` and
`underfoot_tests` (the private prototypes) fail as before; after the last changes to the heap's inline functions,
the 847 tests that touch heaps, rows, lists, text, the schedule, the debugger views, reload and the benchmarks
again. Web (`web-debug`, under Node, with threads): `tide_tests`, `rows` (the same new pin), `ordered_split`,
`heaps` and `row_heaps` pass. Linux, macOS, MinGW and Android are CI's.

Measured (benchmarks.md, A heap in runs): the fixture's tick on 12 threads 7.7-8.3 ms to 1.9-2.3; Mount & Rifle's
`Campaign.Roam` 0.33-0.36 ms to 0.18-0.20 and `Upkeep` 0.08 to 0.03.

**What Mount & Rifle showed.** Of the five systems the morning's table named, only those two were kept by the heap
alone. `Travel` sets `Campaign.Place` through its World, `Outfit` sets `Gear.Inventory`, `FallOnField` sets
`Field.Soldier`: `system_one_thread` says the first reason it finds, and the heap came first. None of its systems
is kept on one thread by the heap now; four are by reading through their World a component they change, the rest
by a `mut` singleton or a `Set`. So of the 0.8 ms the chain had in "own rows' text and lists", about 0.2 came off,
and the rest is the `Set` question the owner was asked the same morning (queue it where it's unobservable).

Gaps:
- **Grids** (above).
- **Owners in the match's one heap**: what C can write, and every owner of a game with shards or hidden state.
- **A run's heap costs memory**: a page for each run that ever allocated, for each owner (32 runs: half a
  megabyte an owner at most), and 208 bytes each in a full pack. Blocks freed in one run's heap are only handed
  out again by that run.
- **Entities change runs** as others come and go before them; a block made in the old run is released through the
  join, and a list in chunks is copied whole the next time it needs chunks.
- **One thread pays a little**: the fixture's tick on one thread went from 7.5-7.7 ms to 7.9-8.1.
- **Map lookups about 2% slower** in `versus_lookup` (a handle's `where` takes three more instructions and
  `tide_heaps_of` a test); `versus_buckets` and `versus_churn` within their runs' spread.
- **Windows' debugger views** (natvis) don't find a block in a heap of a run past the first: the expression that
  would is past what its engine bears (debugging.md). LLDB's do.
- 32 runs are the most a system's entities go in, whatever the threads: fine to 16 cores or so.
- **A gathering system** (the section below, merged the same day) isn't in runs: what it changes in a heap is its
  singletons', from its log on one thread, and the gather's own rule keeps out one that also changes its rows'
  text or lists (`system_heap_runs` leaves `decl.gathers` out). Such a system stays in order; doing both would
  be the runs' loop around the gather's rows.

## The schedule by fields, and gathering systems on threads (2026-11-13)

The owner's task, one of three threads on the schedule that day (the others: automatic parallel views; per-entity
systems that change their own rows' text or lists): make the analysis go by fields so fewer systems wait, and split
"gathering" systems across threads. Both built; what Mount & Rifle got from them is at the end, and it's little.

### Fields

Two systems that take the same component or singleton wait for each other only where one writes bytes the other
reads or writes (`accesses_meet`, `find_conflicts` in `compiler/src/parallel.c`). Before, any writer of `Party`
waited for every reader and writer of `Party`.

- **What a system touches is what its generated C does**, asked of the functions that decide that C
  (`compiler/src/schedule_bytes_codegen.inc`, `system_param_bytes` and `system_query_bytes`, declared in `ast.h`;
  `analyze_parallelism` runs at the end of `check`, and the analyses are plain walks of the checked code). Not a
  second analysis in the checker: the code generator already worked out, for copying fewer lanes, the bytes of a
  parameter a system's code can read (`code_reads`, lane_reads_codegen.inc) and write (`code_writes`,
  lane_writes_codegen.inc), through the functions a place of it is handed to, and a schedule that went by anything
  else could let two systems at pages one of them replaces. So:
  - **A component parameter**: reads are the lanes its rows copy (`system_param_lanes`: what the code reads, and
    what it writes, which is copied in to be put back), writes the lanes put back (`written_lanes`). Whole lanes,
    four bytes: a lane is a page of its own in a chunk (`t->pages[chunk * count + column]`, tide/table.h), begun,
    replaced and hashed by itself, exactly as two components' lanes already were for two systems on one chunk.
    Two `bool`s in one lane are one thing. Wherever a row's copy is whole (`system_param_splits` false: a
    handler, a system in C, one that starts tasks or Sets that component through its World, a virtual one, a
    local or sharded one, a component of one lane or of more than 1,024), so is the access.
  - **A singleton parameter**: its bytes where it is in the world's struct, which generated code reads and writes
    through a pointer, field by field (checked in the C: a `mut` place goes to a function by address, an array's
    element is written alone). Rounded out to whole fields (`schedule_whole_fields`), which is as far as the
    schedule says and leaves room for C that writes a field as one place. `code_reads` and `code_writes` didn't
    narrow through a singleton's fields (only a struct's or a component's: nothing asked them to); they do now,
    which only this asks. Whole for a system whose code isn't all there is (C, async, tasks, virtual, sharded).
  - **A query's rows**: the lanes its loops copy (`match_query_lanes`, the function `gen_query_walk` asks), plus
    the lanes an index of it is made from (`ix_key_binding`), where every use of the query in the system is a
    `foreach` over it in the system's own code. A parallel loop (rows gathered whole), a query handed to a
    function, or a component the World reaches too, is all of it.
  - **The World** (`Get`, `Has`, `TryGet`, `Set`): the whole component, as before.
- **A build for a debugger goes by whole components** (`program.rows_whole`): it copies each row whole
  (`system_rows_plain`), which would read a page another system is replacing, and the page it took the place of is
  freed when nothing else holds it. `codegen` runs `analyze_parallelism` again with it set when
  `codegen_options.split_plumbing` is, so a debug build's schedule has more waits than `tide schedule` prints.
  Singletons go by field in both.
- **A read that's a whole copy** (`F(tally)` by value, `var copy = tally;`) reads every field by the analysis, so
  nothing writes one meanwhile. A function handed a value it reads two fields of gets a copy whose other bytes
  another thread may be writing: the same bytes a view's partial copy leaves as they were, never read.
- **Text and lists are still each owner's**: two systems that change `ledger.entries` and `ledger.notes` wait for
  each other over Ledger's heap, as before. A heap for each field wasn't needed by anything measured (below), and
  isn't built.
- **What it says** (`put_conflict_data`, `conflict.field` and `more_fields`): `it writes Body.velocity, which this
  reads`, `both write Flags.hot (and 3 more of its fields)`, and the bare name where the two meet in every field or
  neither side is known by field. The editors' lenses and hovers and `tide --outline` say the same, through
  `describe_wait`. A wait a parameter and a query of one system both make is said once.
- Language-visible behavior: none. Provisional, mine: the granularity (lanes for components, fields for
  singletons), and the wording.

Tests:
- `tidec_e2e_fields` (`compiler/tests/e2e/fields.tide`): 3,000 men in three archetypes, six per-entity systems
  each writing lanes of its own of one component, one reading what another wrote the same tick, two that share a
  lane of `bool`s, three once-a-tick systems each filling a field of one singleton from a query's rows, and an
  indexed query loop. Twelve ticks on a pool that's one thread saying it has eight, and four rounds on the
  platform's, written in place and beside a snapshot of the tick before (every written lane then a page that
  takes another's place): the world's hash and the singleton's bytes are one thread's every tick, and the
  snapshot still hashes as it did.
- `compiler/tests/schedule/fields.tide`: singleton fields at once, through helpers; a field read that waits for its
  writer; two elements of one array waiting; a whole copy and a method reading every field; lanes apart and a
  shared lane; a query's rows by field, and whole where it's handed to a function or the World reaches it; two
  lists of one owner over its heap. `grids`, `heaps`, `indexed_loops` and `measured` name fields now, and
  `heaps` and `indexed_loops` lost waits (a system reading `pack.id` beside one changing `pack.items`).

### Gathering

A per-entity system with a `mut` singleton ran its entities in order on one thread (308 systems in the tree).
Where its rows only add to the singleton it now splits in order like a system that queues changes
(`system_splits_in_order`, which `decl.gathers` now also makes true; `rows_independent` lets its singletons and
their text through), and what the rows would change is logged and run after them, in their order.

- **What qualifies** (`system_gather_plan`, `compiler/src/gather_codegen.inc`, whose head says it in full; `decl.gathers`
  is set by `analyze_parallelism`). Every statement that names a singleton the system takes `mut` is an
  assignment into it, or a call that changes it (a list's or map's method on a place of it, a function or method
  handed a place of it `mut`), and cuts in two: the largest parts that don't name the singleton, which must be
  plain values of the row (`gather_plain`: fields, locals, constants, `this`, operators without overloads,
  `Math` and constructors, struct literals of those) and are worked out in the row; and the rest, run from the
  log. Text written out or interpolated from plain values is made again from the log. A read-only singleton, or
  a field of one, stays in the logged part as it is. A function of the game's in the logged part can't queue,
  spawn, roll, draw, start tasks, take the World or reach C (`gather_callee`). Any other code of the system may
  read of the singleton only fields the system never writes (`gather_free`, by the same bytes the schedule
  has). Not: a statement that's a loop's start or step (written into the loop's line, not as a statement), a
  parallel loop that names the singleton, a system that calls C (below), one with tasks, a World `Set`, text or
  lists of its own rows changed too, a virtual, sharded or local one, or a debugger's build.
- **Only where the rows keep work** (`gather_plan.little`, `decl.gathers_little`): by `step_cost`, what's left
  to the rows is at least what's logged. Otherwise it stays in order, and `system_one_thread` says `it changes
  the singleton Crowd, and adding to it is most of what its rows do`. Measured first without this rule, on
  Mount & Rifle's `Enlist` (a thousand rows that each add a count, a sum and a `Spot` to a list and call a
  function handed another singleton): 0.24 ms in order, 0.28 logged and run after, since the adding is all its
  rows do and stays one thread's. A crude test (a call counts 30 whatever it does); nothing times it.
- **A system that calls C doesn't gather** (`reaches_extern`). Box3D's `SynchronizeJoints` qualified otherwise,
  and its rows make and drop joints in a C world that has been called from one thread, in entity order, for as
  long as its `mut Physics` kept it there. C is trusted, but that trust was given under the old rule.
- **Generated code.** In the system's function, such a statement becomes: the row's values into locals
  (`tide_gc<n>`), then `if (tide_task_gather)` an entry pushed (`struct tide_gather_<System>_<n>`, the values),
  else the statement itself, with those locals where the values were (the statement's copy has names in their
  place, `expr.hoisted`, as a GUI scroll's held arguments do). After each row, `tide_gather_row()` where the
  system's own heap flush was. `tide_replay_<System>(tide_w)` goes through the tasks' logs in order and runs
  each entry's statement with its values, and at a row's end does what the row did (the scratch reset, the
  owners' heaps flushed), so a block freed by one row is free for the next as on one thread. `tide_done_<System>`
  calls it after the pieces' lanes are ended, before anything that waits for the system starts. With one piece
  (a tick on one thread, little work) nothing is logged and the statements run where they are: that path is the
  old code. In builds that check heaps, the replay says whose it changes (`tide_heaps_allow`). A statement that
  wasn't written through `gen_stmt` would run on the threads: the body's end checks every one was, and says
  "internal error" otherwise.
- **The runtime** (`tide/jobs.h`, `engine/src/jobs.c`; runtime-ownership.md): a log for each task
  (`tide_gather`), the running task's in `tide_task_gather` where its system goes in pieces, `tide_gather_push`,
  `tide_gather_row`, and `tide_gathered` for `done`. What `done` takes counts in the system's time and its
  profile row now.
- **Owned prediction**: a gathering system's `done` has its wrapper (`tide_owned_done_`), as one that owns
  pages does.
- What can tell: nothing in the match. What a function handed the singleton prints comes after its rows' own
  prints. Provisional, mine: all of what qualifies, the work rule, and the schedule's line (`across threads: what
  its entities change of Score is changed after them, in their order`).

Tests:
- `tidec_e2e_gather` (`compiler/tests/e2e/gather.tide`): 3,000 men in several archetypes, some left out; a
  system that changes its own component, sends, and adds to two singletons (counts by a clamped index, `float2`
  sums, a float total in a loop, a list of structs holding `this`, a list of interpolated text, a function with
  a least, a most and a list it adds to once, behind a guard on a field it never writes); one that changes no
  component (its `done` is the log alone); and one that reads what rows write, which stays in order. Eight ticks
  against a pool that's one thread saying it has eight and four rounds on the platform's, in place and beside a
  snapshot: the hash, the singletons' bytes (their handles too: the heaps handed out the same blocks) and every
  list's elements are one thread's each tick. Under ASan with heap checks.
- `jobs_a_gathering_systems_logs_reach_done_in_task_order` (`tests/test_jobs.c`): sixteen tasks run as thread 1
  first, their entries (twelve bytes, and none) and rows' ends reach `done` in task order, the logs start empty
  the next tick, and on one thread there's no log.
- `compiler/tests/schedule/gathering.tide`: five that gather (two of them at once, on different fields), and why
  each of six doesn't. `methods` and `shared_names` each had one that now does, and `parallel` one that would but
  for the work rule; `measured`'s `Count` reads its total back so its chain keeps a one-thread link.

### Mount & Rifle, before and after

Its working tree at a145828, built against this checkout as the section below says (`BATTLE_TEST`, `ARMY`,
release, 12 threads, hidden, `--host --seed 42 --frames 2400 --profile-warmup 1500`, its saves folder pointed at
a scratch one through `USERPROFILE`). Before is this checkout's compiler with both changes switched off by a
temporary flag (not kept), which prints the schedule `dev` did: 87 stages, the same chain. Two programs, ten runs
alternating, on a machine two other sessions were building on (earlier rounds ranged from 3.5 to 13.5 ms a tick
for one program and aren't used); each run's profile set against its own build's schedule with
`tide schedule --measured`. Mean of five, ms a tick:

| | Before | After |
|---|---|---|
| Waits listed in the schedule | 1,961 | 1,737 |
| Stages deep | 87 | 77 |
| Systems one after another | 3.54 (3.44 to 3.67) | 3.64 (3.49 to 3.72) |
| The longest chain of waits | 3.26 (3.17 to 3.39), 92% | 3.21 (3.06 to 3.29), 88% |
| ...systems long | 55 | 48 |
| The match's tick on the frame's thread | 3.68 (3.55 to 3.85) | 3.60 (3.44 to 3.68) |
| `Enlist` | 0.247 | 0.241 (in order, by the work rule) |
| `Measure` | 0.047 | 0.063 |

On the morning's one profile (the section below) the chain is 2.647 ms before and 2.544 after.

What it says: **the tick is where it was**, within what the runs vary by. Fields took 224 waits and ten stages
out, and four points off the chain's share, but the chain is real: Campaign's part is its own rows' text and
lists one after another on `Party` (the other thread's work), and Infantry's is data that flows
(`BotSense` writes what `BotAct` reads, `Disperse` empties what `Enlist` fills and `Gather` and `Fly` read). The
two waits the owner named joined them through whole components; by field they still join, through what's
really shared: `KeepInside` writes the `Trooper.position` that `BotSense` reads, and `Measure` reads the
`Catalog` a loading system writes. `Measure` got slower because it now runs beside other systems and waits for
a thread, which its time counts. Gathering gives this game nothing: its one gatherer of any cost, `Enlist`, only
adds, and `TakeField` (0.003 ms) is the only system that gathers. So a heap for each field isn't worth building
for it either: no wait on its chain is over two lists of one owner that a field would tell apart.

Gaps:
- **The third item, a `world.Set` held until the system's end**, isn't built. The log would carry it (a `Set` of
  a component the system reads nowhere, its entity and value logged, applied in order by `done`), and it's
  unobservable under that condition; no system of Mount & Rifle's calls `Set`, and 16 in the tree do. Still the
  owner's to decide.
- **A system that changes a component and reads others' through a query** (37 in the tree) still runs on one
  thread and writes its rows in place, even where the lanes it reads through the query aren't the ones it
  writes (`Separate` in `schedule/indexed_loops.tide`: writes `velocity`, reads others' `position`). By lanes
  those rows are independent; `world_reads_own_writes` and the in-place writing would both have to go by them.
- A query handed to a function, a parallel loop over a query, and everything through the World read the whole
  component.
- A debugger's build goes by whole components, so its schedule isn't the one `tide schedule` prints.
- An array's elements and a struct field's own fields are one field of a singleton.
- What's worth gathering is tidec's reckoning of the code, which counts a call as 30 whatever it does; the
  runtime times the system whole and can't tell the rows from the log.
- A gathering system's captures must be plain values: a row that adds a list or text it built (other than
  interpolated text), or a value from a function of the game's, stays in order. The value could be worked out
  in the row and its bytes logged, were scratch values able to cross threads.
- Systems that call C never gather, whatever the C is.

Validation: the whole native sanitize suite (Windows ASan, `TIDE_HEAP_CHECKS`), 3,270 of 3,274 at its one full
run, the four all this work's and fixed: `tidec_recompile`, `tidec_asset_usage` and `tidels_tests` read freed
memory (the gathering systems' plans were kept from one compilation to the next, in the last one's arena:
`gather_forget`, from `schedule_bytes_reset`, which `codegen` now calls too), and `tidec_outline_game` and the
language server's lens tests pinned the old wording. After the fix the compiler's, language server's and
engine's tests (2,987) pass; the packages' and games' weren't run again, as it changes no generated code. On
the web (`web-debug`, under Node, with threads): `tidec_e2e_gather`, `tidec_e2e_fields`, `tidec_e2e_ordered_split`,
`tidec_e2e_heaps` and `tide_tests`. The docs site builds. No pinned world hash moved: nothing here changes a
result.

## The schedule against measured time, and the one-thread reason in the editors (2026-11-13)

After the heaps (below), the owner asked what closes the gap further, and agreed an order: measure which waits
cost time first, then gather-and-replay or parallel views by what that shows, field-level conflicts only for
links on the chain, the editor line whenever.

Built:

- **`tidec --schedule --measured <report.json>`, `tide schedule --measured <report.json>`**
  (`print_schedule_measured`, `compiler/src/parallel.c`; `codegen_options.profile`, `build_options.measured`). The
  report is `tide run --profile`'s (`tide.profile`, its `systems` rows and `ticks.count`); a row is matched to a
  system by the profiler's name for it (`tool_name`, which `put_decl_name` without a namespace also writes) and
  its line, or by `system <index>` from a build with names left out. After the schedule it prints the systems'
  mean time a tick summed (the tick on one thread), **the longest chain of waits** (each system's finish is its
  own time plus the latest finish among the systems it waits for, over `decl.waits`, which lists every direct
  wait), that chain last to first with each link's wait as `describe_wait` says it and what keeps each system on
  one thread, and the twelve costliest systems, marked when they're on the chain. Rows of systems the code hasn't
  got are counted and said (another build's report). Read with `strstr`/`strtod` over the report's own writer's
  format (`engine/src/profile_report.c`), not a JSON parser: tidec has none, and the format is the engine's.
  The name is `--measured`, not `--profile`: `tide run --profile` writes a report and the help table has one
  entry an option. Provisional, mine.
- What the number is and isn't: a system's time in the report is from its first task's start to its last's end,
  so one whose entities already go across threads counts at what it took there, and one that waited for a free
  thread counts that wait. The chain is therefore a floor for *this* machine's split of entities, not for one
  thread's rows. Systems that didn't run in the profiled ticks count as nothing. Changes, handlers, tasks and the
  hash aren't in it (`ticks.changes_events_tasks_ms` is beside it in the report).
- **The editors say what keeps a system on one thread** (`analysis_code_lenses`, `describe_order`,
  `compiler/lsp/analysis.c`): a lens `on one thread: it changes the singleton Score` above the system, which goes
  to the place (`editor.action.goToLocations`), `its entities go across threads` on the stage lens of one that
  splits, and both on hover. They had each system's stage, waits and what runs alongside since the schedule
  lenses; the reason was only in `tide schedule`. One function says it for all three,
  `system_one_thread` (`ast.h`), which `put_one_thread` now calls.

Tests: `tidec_schedule_measured` (`compiler/tests/measured/`: a costliest system off the chain, a chain of three
with a one-thread link, a row the code hasn't got); `tidec_diagnostics_frontend_cli` (a report that can't be read,
a file that isn't one); `lsp_schedule_lenses_and_fixes` (the lens, its target, the hover's two forms);
`tide_cli_help`. Native only: tidec, tidels and tide don't run on the web.

Mount & Rifle through it (its working tree at a145828 in place, `tide_add_game` with `BATTLE_TEST` and `ARMY`
as a scratch `TIDE_EXTERNAL_DIRS` folder, release, 12 threads, hidden, `--host --seed 42 --frames 2400
--profile-warmup 1500`; one run on a shared machine, so to a tenth of a millisecond at best):

| A frame, mean ms | |
|---|---|
| match (the tick, on the frame's thread) | 3.03 |
| views | 3.72 |
| render | 0.97 |
| all | 7.74, against 6.94 for 144 Hz |

| The tick's systems | ms |
|---|---|
| one after another | 2.865 |
| the longest chain of waits | 2.647, 55 systems long |
| ...of which Infantry's | 1.61 |
| ...of which Campaign's | 1.03 |
| ...of which systems on one thread | about 1.05 (`Roam` 0.32, `Enlist` 0.21, `Travel` 0.20, `FallOnField`, `Outfit`, `Upkeep` 0.07 each) |

What it says: **92% of the systems' time is one chain**, so systems running alongside each other buys this game
0.2 ms, and all it gets from threads is inside systems (`BotAct` 0.53 and `BotSense` 0.38 are already across
them, and are the chain's two costliest). Box3D's 45 systems, which the wait count made look like the problem,
aren't among the twelve costliest. The campaign's part and the infantry's are joined by few waits
(`Infantry.Measure` reads the `Party` that `Campaign.Muster` writes; `Campaign.KeepInside` writes the `Trooper`
`BotSense` reads): were they apart, the floor would be the infantry's 1.6. And **the views cost more than the
tick**, in order on one thread: `OthersOnFoot` 0.67, `Present` 0.43, `HeldWeapon` 0.42, `DrawCompoundKits` 0.39,
`DrawFlora` 0.37. So by the order agreed: parallel views first, then the one-thread systems on the chain (own
rows' text and lists 0.8 ms of it, the gathering `Enlist` 0.2), then the waits that join campaign and infantry,
which is where field-level conflicts would be tried.

**The views' chain, and what it says about parallel views (2026-11-13, later).** The owner said to do the three
next, in threads of their own where they can be: field-level conflicts with gather-and-replay
(`t3/schedule-field-conflicts`), systems that change their own rows' text and lists
(`t3/schedule-row-heaps`), and parallel views here. Before building stage 1 of the views' plan (independent views
at once, each into a draw list of its own, joined in order), the measured schedule was given the views:
`analyze_parallelism` now runs its analysis over `prog->views` too (`analyze`, the same waits among views as
among systems: parameters, `World`/query uses, heaps, tasks, `[After]`/`[Before]`), and `--measured` prints a
second part from the report's `views` rows over its measured frames. Nothing else reads a view's waits: the
outline and the editors' lenses leave views as they were. The fixture has three views.

Mount & Rifle's battle (the run above): **79 views, 3.62 ms a frame one after another, and their longest chain is
3.48.** Run at once they would save 0.14 ms. Tried, in a build not kept, what the chain would be were its waits
taken away one kind at a time:

| The views' chain, of 3.62 ms | ms |
|---|---|
| as analysed | 3.48 |
| each local owner's text and lists in a heap of its own (the local world has one) | 3.33 |
| ...and views that start tasks not ordered with those that change text | 3.09 |
| ...and no wait at all over `Stage`, `Look`, `Land`, `PartBounds`, `FarCrowd`, `Bench` (more than field-level conflicts could give) | 3.08 |

What's left is `[After]`/`[Before]` the game wrote (`Present` after `HeldWeapon`, `Bloodshed` before `Present`,
written for the order things are drawn in, which the analysis can't tell from an order of data) and local
singletons each written by several views (`Sheet`, `SwardStore`, `Gear.PartBounds`'s lists). The costly views are
once-a-frame ones with several `mut` local singletons (`Present` 0.43, `HeldWeapon` 0.42, `DrawCompoundKits` 0.39,
`DrawFlora` 0.37, `Bloodshed` 0.25, `DrawFarCrowd` 0.21) and one per-entity view that takes six `mut` singletons
(`OthersOnFoot` 0.67): the same finding as 2026-11-04 on the route (benchmarks.md, Mount & Rifle's views), now
from the compiler rather than a reading. **So stage 1 was not built**: for this game it is worth at most half a
millisecond with two more engine changes under it, and 0.14 without. What its frame thread carries with
`ticksAlongsideViews` is views + render, 4.7 ms of a 6.94 budget; the tick is beside it. What would take the
views' own time down is inside the big views: a view's draws recorded on threads (its `parallel` loops already
run on the pool, a game-written split), or their work made cheaper. Put to the owner.

**How much of the views is recording draws (2026-11-13, later; the owner: "let's measure option 1").** Two
measures of the same battle, neither a build of anything:

- The profile's own `draw_diagnostics` (`mesh_recording_ms`: an ordinary or shaded 3D call's bounds, frustum
  tests, LOD and instances, after its mesh is found): **0.58 ms of the views' 3.62**, 16%. By view: `DrawCompoundKits`
  0.15 of 0.39, `Present` 0.11 of 0.43, `DrawFlora` 0.06 of 0.37, `DrawLand` 0.05 of 0.19, `DrawFarCrowd` 0.05 of
  0.21, `OthersOnFoot` 0.04 of 0.67, `HeldWeapon` 0.02 of 0.42, `Bloodshed` 0.01 of 0.25.
- `tide_sample --main --after 15` (the new `--after <seconds>` leaves the loading out) with `--ticks-alongside`,
  so the frame's thread has the views, the render and what a tick's beginning and end leave on it; 7,430 samples
  in the program, by the file each was in: draw recording (`draw.c`, `mesh.c`, `draw.h`, the culling files) about
  15%; lists, heaps, scratch values, entity slots and query indexes (`list.c/.h`, `heap.c/.h`, `text.h`,
  `entity.c/.h`, `index.c`) about 28%, of which some 3% is the tick's hash of heap pages; the game's own code and
  math 18%; skinning 6%; the renderer about 10%; the mixer 8% (a hidden run mixes on this thread); hashing 4%.

So recording draws on threads could take at most about half a millisecond off 3.6, and far less in practice
(the two views with the most recording are a tenth of a millisecond each). **Not built.** The larger share is the
views' own work reading lists and scratch values: `tide_heap_block` 2.9%, `tide_list_at` 2.3%, `tide_scratch_at`
2.2%, `build_sorted` (a query's index) 1.8%, `tide_entity_slot_of` 1.7%, `tide_handle_offset` 0.7%,
`tide_heaps_of` 0.5% of the thread. Whether a list's element read costs more than it must (its block found again
at every index, the heap found by id since the heaps' change) is the next thing worth measuring, as it's the
tick's cost too.

Answers given to the owner the same day, none built, the language ones his to decide:

- **`world.Set` queued by default, with a `SetNow`?** Asked whether the compiler could decide instead. It can for
  a `Set` in a system that reads that component nowhere else (not its own rows', not through World or a query):
  holding the writes until the system's done and applying them in entity order is then the same result, and the
  system's entities could go across threads. It can't for read-then-`Set` (Mount & Rifle's `Follow`:
  `TryGet<Party>(employer)`, take its money, `Set` it back): two rows with one employer must see each other's
  write, and a queued `Set` would lose one silently. Recommended: no new method, the compiler queues where it's
  unobservable; awaiting the owner.
- **`mut ItemIds`** (Mount & Rifle: one `int next` that every new item takes its uid from, passed on as
  `ids.next`): a row's uids depend on how many items the rows before it made, so the system's entities stay in
  order, and any two systems that make items wait for each other. What would free it is an id that isn't counted,
  made from the entity and the place in the code as a spawn's is; a language addition, not designed.
- Kinds of one-thread singleton system seen in Mount & Rifle: gathering (`Enlist`: counts, a float sum and a list
  it adds to, never read by a row), handing out numbers (`ItemIds`), reaching other rows (`Follow`). The first is
  what gather-and-replay would take: threads run the rows and log what each adds, one thread applies the logs in
  entity order, bit for bit what one thread makes (float sums included, as nothing is regrouped).

## A heap for each owner, and spawners that don't wait (2026-11-13)

Asked: review what can run in parallel, systems and views, where "strings or lists etc" break the assumptions.
Measured first, with `tidec --schedule` over every game in the tree and Mount & Rifle (its `game/` sources with
`BATTLE_TEST` and `ARMY`, the Rooms and Box3D packages):

| What kept code off threads | 523 games in the tree (3,066 systems) | Mount & Rifle (126 systems) |
|---|---|---|
| Per-entity system on one thread: a `mut` singleton | 308 | 12 |
| ...it changes text, lists or grids | 82 | 18 |
| ...it changes a component and reads others' through World or a query | 37 | 0 |
| ...it calls `Set` | 16 | 0 |
| Waits over nothing but the heap or spawn order | 731 of 38,723 | 1,322 of 2,925 |
| Systems that change the heap | | 72 of 126 |
| Schedule depth | | 106 stages |

So the gap was the one the owner named: every text, list, map, set and grid of a match lived in one heap, whose
blocks are handed out in a fixed order, so **every system that changed any of them waited for every other**, whatever
they changed, and Mount & Rifle's tick was a chain 106 systems long. A `Spawn`, `Add` or `Send` whose values hold a
list counted too (its values are made the world's own as they're queued), and so did any two systems that spawn.

Built:

- **A world has a heap for each owner** (`tide_heaps`, `tide/heap.h`, A world's heaps; `engine/src/heap.c`). An owner
  is a match component, singleton or event whose fields hold anything a heap keeps; what its fields hold is in its
  heap and nothing else is. A block says which heap it's in: 12 bits of every handle and `where` (`tide/text.h`:
  `TIDE_WHERE(world, id)`, `tide_handle_where`, `tide_handle_of`; a world's handle is its block's offset from bit 8
  up, which is free as blocks start at multiples of 16, so a handle is a byte longer as a varint, not five). Every
  runtime function already found its heap from a `where` or a handle (`tide_heap_of`, `tide_heap_read`) and told a
  field's own block from a borrowed one by `tide_handle_where(at) == where`, so text.c, list.c, map.c and grid.c are
  as they were: the same comparison now tells one owner's block from another's. `tide_heap` is still one heap, with
  its functions; `tide_heaps` is the set, one array by slot (`all`, made with a compare-and-swap the first time a
  heap is asked for, so a zeroed world is still an empty one), with `tide_heaps_of(set, id)` and
  `tide_heaps_copy/free/flush/settle/hash/stale/pack/unpack/pack_delta/unpack_delta/hash_pages/need_pages/used/
  pending/pages/pages_apart`. `tide_text_use`, `tide_world_heaps` and the
  readers handed a world (`tide_text_read`, `tide_list_read`, `tide_list_read_count`, `tide_grid_read`,
  `tide_grid_copy_from`) take the set.
- **Ids are made from names** (`assign_heaps` in `compiler/src/check.c`: FNV-1a of the qualified name into 1..4095),
  so they're the same in every build of a game and a world carried over by a hot reload keeps its handles. Two owners
  whose names collide share a heap (the schedule compares ids, `decl.heap_id`). A game says its ids once, from a
  constructor in its generated C (`tide_heaps_shape`, `gen_heap_shape`): a table from id to slot, and each slot's id.
  Without one every id is heap 0, which is what the engine's own tests, a worker's private heaps and a build step's
  heap are.
- **Id 0, the match's own heap**, holds what's no owner's (tasks' frames, the hierarchy, visibility groups, inputs)
  and every owner that can't have its own: the engine's built-in types and scenes; **an owner C can write** (one a
  system written in C takes `mut`, or that code hands, whole or in part, to a `mut` or `out` parameter of a callee
  that reaches C: `c_lend`, `decl.c_written`), since C says `TIDE_IN_MATCH` for what it writes, so packages' and
  games' C is as it was; and every owner of a game with shards or hidden state, whose generated code packs and reads
  one heap (`shard_*.inc`, `client_state_codegen.inc`). The generated header has `TIDE_WHERE_<Type>` for each owner,
  which C that writes a field of one it wasn't handed says instead of `TIDE_IN_MATCH` (tests that poke a world do).
- **Generated code** (`decl_where`, `place_where` in `compiler/src/codegen.c`): a place in a component's or
  singleton's memory is its owner's `where`; a spawn's and an add's values are made their components' own
  (`tide_own_spawn<a>` per component), an event's the event's, a singleton's defaults the singleton's; a system
  flushes the heaps of the owners it changes after each row (`tide_heap_flush(tide_heaps_of(...))`), everything
  serial flushes them all (`tide_heaps_flush`).
- **The schedule** (`find_conflicts` in `compiler/src/parallel.c`): two systems wait over a heap only when both
  change the same one (`decl.heap_owners`, noted wherever `writes_text` was: a write through a parameter, a `Set`, a
  queued value, a helper's). `tide schedule` says whose: "both change the text, lists or grids of Party". A system
  that starts tasks still waits for every system that changes a heap or spawns (a task runs whatever its code does
  until it first waits).
- **Spawning makes no system wait** (`CONFLICT_SPAWN` is gone): every system that spawns registers its entities once
  its tasks have joined and the spawner before it has registered its own (`tide_system_tasks.settles` is true for
  all of them, where it was for systems split in order; the runtime's chain was already there). Nothing in Tide can
  tell: a spawn's key is its row's entity and place in the code, an entity that isn't installed has no archetype
  either way, and no Tide code asks whether a key is registered (C can, with `tide_entity_alive`).
- **Builds that check** (`TIDE_HEAP_CHECKS`, on in the `sanitize` preset; `tide_heaps_allow`): each system's task
  says whose heaps it may change, and a block handed out, taken back or made a heap's own in another ends the
  program, naming the system and the heap's id. On its first run it found a hole that was there before:
  **assigning a `string?` (or any `T?` holding text or lists) counted as no heap write** (`holds_text` and
  `holds_list` don't look inside `T?`), so such a system ran alongside others that changed the heap, and a
  per-entity one went across threads. `decl_holds_text` and `check_writable` now use `type_in_heap`, which looks
  through structs, arrays, `T?` and grids.
- **Packed worlds**: the first heap as it was, then how many of the others have handed anything out and each with
  its id and size (`tide_heaps_pack`), so a world with one heap in use packs four bytes more. A delta has only the
  heaps that aren't their base's as it stands, by slot; the rest are shared with the base. A hash is the first
  heap's, then each other one's id and hash, skipping those that never handed anything out.
- **Hot reload** (`platform/src/migrate.c`, `gen_layout`): the heap's bytes still carry over whole. A layout field
  says the id of the heap its text and lists are in (`tide_layout_field.heap`, the struct's new last member), and a
  field only carries over to one with the same (`carries`): where an owner's id changes between builds (it's handed
  to C now, or no longer) its fields start again rather than carry handles into a heap that hasn't their blocks. A
  heap whose owner the new build hasn't is left out as the world is read. The id isn't in the layout's type names,
  which `tide unity`'s schema and C# read.
- **Two things the new handles broke, found by the suite and fixed.** A handle and a block's offset used to be the
  same number for a match's block, so `client_state_codegen.inc` handed a list's chunk offsets and a grid's record
  blocks to a function that takes a handle; they go to `tide_client_reach_block` now (the visibility hash memo kept
  hashes of pages nothing had read). And the natvis expressions (`debug_views_codegen.inc`): Windows' debugger
  engine gives up on a text's `DisplayString` past some length and never comes back from one a little longer
  (8 GB, then out of memory), so `view_block` is shorter than it was before this, not longer: a world's block is
  `(char*)(heap->page[at << 2 >> 24] + 1) + (at >> 8 & 0x3FF0)`, which rests on a block of more than a page
  starting its page. The same engine ran out of memory walking the stack through the checked task's wrapper while it
  called `tide_heaps_of` inline, which is one reason `tide_heaps_allow` takes ids.

Mount & Rifle's schedule with all of it: 2,925 waits to 1,907, none over the heap between different owners, and 87
stages deep where it was 106. What's left of the depth is data: 45 of Box3D's systems change its `Physics`
singleton one after another (a C physics world's pipeline), and most of the campaign's change `Party`. Its owners
that share the match's heap are Box3D's, which its C writes.

Measured, on the fixture (`compiler/tests/e2e/heaps.tide`; release, 12 threads): its tick on the pool went from
20.6 ms, which is one thread's, to 10.8, the length of its longest system (benchmarks.md, A heap for each owner).

Tests:
- `tidec_e2e_heaps` (`compiler/tests/e2e/heaps.tide`): every owner's handles say its own heap and nothing is in the
  match's; twelve ticks on a pool that's one thread saying it has eight, and five runs of twelve on the platform's,
  hash the same as one thread's every tick with the same lists and text; a world's bytes and a delta from a
  snapshot (unpacked over the unpacked base) give the world back, and both go on as one.
- `tests/test_heap.c`, three more: owners' blocks apart, a block of another owner's borrowed and copied, one heap's
  blocks coming and going without the other's moving; copies sharing every heap's pages until one changes, bytes
  with ids, a game without one of the owners leaving its heap out, a heap twice refused; a delta of nothing changed,
  of one owner changed (the other's page still the base's) and with no base.
- `compiler/tests/schedule/heaps.tide`: different owners at once, one owner's on entities that never meet one at a
  time, two spawners of a component holding text waiting over its heap and two spawners of one holding none not at
  all, two owners handed to C sharing the match's heap, and the list of those at the end. Six other goldens lost
  their heap waits (`text`, `singleton_heap`, `grids`, `lists_out`, `ordered`, `parallel_loops`).
- The pinned world hashes of every fixture, package test, example and benchmark whose world holds text or lists
  moved (a handle's bits changed): 60 values in 31 files, among them `nested`, `arrays`, `rows`, `rolls`,
  `crossplatform_text`, the Box2D and Box3D sequences, towers, sumo and the benchmarks' `--expect`.

Validation: the whole native sanitize suite (Windows ASan, with `TIDE_HEAP_CHECKS`), 3,279 of 3,281:
`inside_out_tests` and `underfoot_tests` (the private prototypes) fail as they did before it. On the web
(`web-debug`, under Node, with threads): the fixtures whose pins moved and the heap's own (`nested`, `arrays`,
`rows`, `rolls`, `crossplatform`, `crossplatform_text`, `chars`, `json`, `optional_fields`, `decode`, `lists`,
`text_fields`, `ordered_split`, `visibility_lists`, `visibility_hash_memo`, `heaps`, `tide_tests`) pass with the
values the native build made.

Correction (2026-11-13, later): the first push failed CI on every platform but Windows ASan. `check_writable`
recorded a `c_lend` for a built-in's call too, whose `called` is a decl on its caller's stack made for the
messages' name (`Random`'s methods, the skinned operations), and `assign_heaps` read it after the frame was gone:
whether an owner counted as C's to write, and so its heap, came out by platform and build, and the skinned, FBX
and sumo pins with it. Linux's ASan says so (stack-use-after-return); Windows' doesn't look for it. A lend is now
recorded only for a real callee, and `FBX_HASH`, `BRAWL_HASH` and `MATCH_HASH` are what Linux, macOS and this
machine then agree on.

Gaps, what still keeps code off threads, in the order the measurements above give them:
- **A `mut` singleton keeps a per-entity system on one thread** (308 of the tree's systems, 12 of Mount & Rifle's):
  the language's rule, as each row sees what the rows before it wrote. A system that only adds to one (a count, a
  sum, a list it appends to) could go in runs and join them in order; not designed. (Built later that day, for
  those whose rows keep work of their own: the top section, Gathering.)
- **A per-entity system that changes its rows' text or lists stays on one thread** (82 and 18): a heap's blocks go
  in its entities' order. Runs on threads would each need blocks of their own that the join renumbers, or a heap
  for each chunk; not designed. (Built later the same day: the top section. Text, lists, maps and sets; not
  grids.)
- **Owners C can write share one heap**, so Box3D's and Box2D's systems wait for every other such system over it
  (they wait for each other over their singleton anyway). C would have to say an owner's `where`
  (`TIDE_WHERE_<Type>` is in the header) and the compiler be told that it does.
- **Games with shards or hidden state keep one heap**: `shard_*.inc` and `client_state_codegen.inc` pack, hash and
  read one (`hs->heap`).
- **The local world has one heap.** Views run one after another, so nothing waits over it yet; automatic parallel
  views (the plan of 2026-11-04, stages 1 to 3, unbuilt) would want local owners' heaps too.
- **A system that starts tasks waits for every system that changes any heap or spawns.**
- **Systems written in C run their entities on one thread** (their rows ABI takes runs, so the split in order
  would fit), and so do systems that call `Set` or read through their World a component they change (37 and 16).
- **Steps of a parallel loop that read a list their code made stay on one thread** (the section of 2026-11-12
  below).
- **Event handlers and the tasks' pass** run one at a time at the end of the tick.
- Two owners whose names make one id share a heap (one pair in about a hundred owners): `tide schedule` says they
  share one, not why.
- `tide_heap_of` reads a table and a pointer more than it did; not measured against `dev` on the list benchmarks.



Mount & Rifle (1,000 soldiers, 144 Hz): a frame with a tick cost its thread tick + views + render, 9.4-9.9 ms
against 6.94, with nothing left to take off either. The owner lifted the 2026-11-04 "not now" the same day
("whatever it takes ... any 'not now' can be dealt with"). Built for the machine that runs the match; a client
of another machine's match is unchanged (gaps).

What it is: `settings { ticksAlongsideViews = true; }` (`tide_game.ticks_alongside`), `--ticks-alongside`, or
`tide_session_desc.alongside` / `tide_session_set_alongside`. The name, the default (off) and whether it should
turn itself on where a tick is costly are Provisional (spec, Settings).

- **A tick in three parts** (`engine/src/session.c`): `tick_begin` (joins and leaves, inputs, commands, shard
  messages, the frame's head, the hearing's log), `tick_run` (`tide_game.tick*` and the world's hash) and
  `tick_end` (the tick counted, `ended`, the hash patched in, `mirror_tick`, the stored frame, the journal, the
  players' projections). `server_tick` is the three in a row, as it always was. `tide_server_update` runs every
  due tick but the last that way; the last, where `can_fly` (the session asked, the pool can lead, and this
  machine's player is level with the server: `mirrored`), is begun *after* the update's sends (`fly_tick`), its
  `tick_run` handed to the pool's leading thread, and ended by `land` at the top of a later update. From begin
  to end the server's world is that thread's alone and `tick` doesn't count it; every session function that
  reads or changes the server lands first (`settle`: tear down, kick, ban, end, set_game, migrate, rewind,
  record, can_reload, `tide_session_server`, `tide_server_world`), and `mirror_play` leaves the server's world
  alone while one flies.
- **The pool's leading thread** (`tide_jobs.lead`, `led`; `platform/src/jobs.c`, desktop): one more thread than
  `threads`, made at the first `lead`, 8 MB of stack (it runs what the program's first thread would: handlers and
  changes). It asks for rounds as any caller does, as thread 0. Since two threads can now ask, **a round is one
  caller's at a time** (`pool.taken`): a round asked for while another is open runs on its caller's thread alone,
  which every work allows (`round_of` returns false; `run`'s timing of the two ways skips it, and its `choice`
  table is under the pool's lock). So a frame's loops, skinning's poses and audio's rounds on the frame's thread
  stay there while a tick has the pool, and a tick's hash round stays on the leader if the frame took the pool
  between the tick's rounds. This settles the gap noted the same morning ("a tick must not run on the pool at
  the same time as a frame's loops").
- **Views a frame behind.** With `alongside(s)` (asked, a leading thread, one server, a client), the view's
  moment is that of the update the views were last taken after (`tide_session_view` keeps `view_taken` and
  `view_shown`; calls between two updates give the same). `tide_client_view` works `at` out from that moment;
  where it's ticks before the latest (an update that ran several), it's between the two ticks around it, as
  far back as the ring has both (`ahead - ring + 2`), and the confirmed interval is held to `at - 1`, so near
  and far entities show one moment. A moment within 1e-6 of a tick is that tick's start, as `ticks_in` counts
  it. Nothing else of the client changed: `verified_view` adapts by itself to the later `verified`.
- **Which update ends it.** `tide_session_update` holds the server's update (nothing heard, sent or ticked)
  while a tick flies that isn't done, wasn't begun by the time the views were last taken (`flew_at >
  view_taken`: begun by a host's update between frames, like the standard host's while it waits to present)
  and no further tick is due. Otherwise it lands: the frame after the one that began it, which is the first
  whose views need it. A tick longer than a frame is waited for there (`tide_jobs.led`).
- **What it costs**, as the spec says: views one frame older; a tick's frame to other players at the next
  update instead of at once (the standard host's update while it waits to present sends it sooner); local
  handlers hear a tick the frame its world is first shown; and C that the match and views both call is on two
  threads at once.
- **C with state of its own** (`tide/jobs.h`): `tide_ticks_settle()` returns once the tick this thread handed
  over has run (`tide_tick_beside`, a thread-local the session sets with the tick's done word), and at once on
  any other thread. Box3D's `b3t_find`/`b3t_rebuild` and Box2D's `b2t_seek`/`b2t_recall` call it: a view's
  question waits for the tick that's stepping C's world, then restores its own state as a view of an older
  tick always did. So a view that asks physics gives the frame back the tick's time; nothing is corrupted.
- **Profiles.** The tick fills a profile of its own (`flight_profile`, through a copy of the jobs with it),
  taken into the host's at landing (`tide_profile_take_ticks`), so the frame's thread never reads rows a tick
  is writing. `tide_session_alongside_ns` is what the last one took; the report has `ticks_alongside` (a
  distribution apart; `cpu_phases.match` is then begin, end and any wait), and F7 a "Ticks alongside" row.
- **Measuring it** needs frames that aren't all ticks: `--frame-rate <n>` (`tide_headless_desc.frame_rate`)
  makes a hidden run's time up at another rate than 60 (the mixer renders a frame's worth at that rate).

Tests. `tests/test_alongside.c` (a stand-in leading "thread" that runs its work when waited for, the latest a
real one could, or at once): the same match byte for byte as ticks in the update, commands in the same ticks;
views show the world and alpha the frame before showed without it; a tick begun between frames isn't waited
for by the next frame and is by the one after; leave, restart and destroy land first; nothing without `lead`.
`tide_platform_alongside` (`platform/tests/alongside.c`: the standard host, the real pool, `jobs.tide`'s busy
match plus views over every body, text, grid and list, local handlers, and C a system and a view share): at
144, 60 and 24 frames a second, every frame's world hash, alpha, view sums and (with a tick a frame at most)
heard events are the frame before's with ticks in the update, bit for bit, and the final worlds equal. With the
settle taken out of the shared C, two of its three cases fail. Passing under Windows ASan.

Measured: benchmarks.md, Ticks beside the frame.

Gaps:
- **Clients of another machine's match**: prediction, confirmed ticks and ticks run again still run in the
  update. The same split would fit `run_tick`, with rollback's worlds the hard part.
- **The web**: its pool has no leading thread (`lead` NULL), so the setting does nothing there. A worker could
  lead rounds as desktop's does; the page's own thread can only spin while it waits.
- **Sharded matches** run their shards' ticks in the update.
- A frame's own rounds (view loops, skinning poses) run alone while a tick has the pool: the pool takes one
  caller's round at a time rather than sharing its threads between two.
- At fewer frames a second than ticks, a frame's earlier ticks run in its update and its handlers hear them
  up to a frame's worth of ticks before the views show them.
- A tick that doesn't fit in a frame is waited for by a sleeping frame thread; it doesn't help the tick.
- Unity's frames (`platform/src/embed.c`) don't ask for it.

## Per-entity systems that queue changes go to threads in entity order (2026-11-12)

Second round of Mount & Rifle's 1,000-soldier report: with measured cost in
(below), its two heaviest systems still ran on one thread, `Infantry.BotSense`
(2.34 ms a tick for ~970 rows) and `Infantry.BotAct` (2.96 ms). Two causes,
neither a threshold.

- **A fixed array's element counted as the heap** (`check_writable`,
  `compiler/src/check.c`). `in_list` was any `E_INDEX` on the way to the
  target, so `bot.aware[slot] = x` on an `float[N]` field marked the system
  `writes_text`: one thread, and a wait on every other system that changes
  text, lists or grids ("both change text, lists or grids"). A fixed array is
  in its component's lanes; only an index into something that isn't a
  `TY_ARRAY` (a list, a map, a grid, text) reaches the heap. An array whose
  elements hold text or lists is still caught by the target's own type.
  Functions' `heap_written` parameters use the same test, so helpers that
  write array elements through a `mut` parameter no longer mark their callers.
  In Mount & Rifle's schedule 18 per-entity systems stay heap writers, all
  with real list or text writes.
- **Any system that sent, spawned or destroyed stayed on one thread** since
  entities got logical identities (2026-11-06: `system_splits` refuses
  `acts[0]`, `spawns` and `queued_stmt`, because what a system queues must be
  in identity order, which chunks aren't). Nothing in that order needs one
  thread, though: a spawn's identity is its row's entity, the code and the
  site's count within that row (`tide_causal_entity`, counters made per row),
  never what ran before it, and a queue is joined in order whatever order its
  parts were made in. So such a system now **splits in order**
  (`system_splits_in_order`, `compiler/src/parallel.c`: `rows_independent`,
  the old conditions without the queueing ones, and it queues something):
  - Generated code (`gen_system`, `ordered`): its task is a run of the ordered
    keys, `[count * task / pieces, count * (task + 1) / pieces)`, walked as
    the whole order was, runs of contiguous rows merged up to the task's end.
    `tide_tasks_<System>` still returns 1 (one "chunk"), and the table says
    `splits`, so `pieces_for(1, work, threads)` decides the runs, by measured
    cost like any chunk's pieces (up to 64).
  - Lanes: the canonical walk already begins every chunk's written lanes with
    `tide_table_lanes_begin_pieces` and asks where they are per run
    (`tide_table_lanes_piece`), which is exactly what pieces of one chunk on
    several threads need. With one piece the task begins and ends them itself,
    as before; with more, `tide_own_<System>` and `tide_done_<System>` do
    (the same `gen_canonical_lanes`), which the runtime calls before the first
    task and after the last (`tide_system_tasks.own`, `done`).
  - Queues and spawns: each task records into its own queue, appended in task
    order (`run_on_threads`), which is key order. A system that spawns is
    `settles` again (the table had it false since 2026-11-06): its tasks'
    keys are registered when it joins, in task order, as
    `jobs_split_spawns_return_final_full_keys` pins for the runtime. Nothing
    in a row can tell: a spawn only pushes a command, and an entity not yet
    installed has no archetype either way. The schedule's spawn waits are
    unchanged (`CONFLICT_SPAWN` still orders spawners).
  - Each task fetches the ordered keys itself
    (`tide_entities_ordered_archetypes_slots`, cached, locked for the fetch).
    A registration on another thread retires the cache, and the next fetch
    rebuilds it with the same keys: only installed entities have an
    archetype, and nothing is installed while systems run.
  - The marks of rows that get past a system's first tests (`rows_live`, the
    same day's work on the walk) are made before any row runs, from the rows'
    own bytes. A task of several can't make them: other tasks' rows are
    changing by then. So with more than one piece every row is marked
    (`gen_canonical_live`, `LIVE_PIECED`; `LIVE_WHOLE` in `tide_own_` and
    `tide_done_`, which then begin and end every chunk) and each row's own
    first line decides, as it did before there were marks. With one piece the
    marks are made and used as they were.
  - No runtime function was added or changed.
- **`tide schedule` says what keeps a system on one thread**
  (`put_one_thread`): for a per-entity system written in Tide that splits
  neither way, one line after its reads and writes, `on one thread: it changes
  the singleton Score (game.tide:42)`: a `mut` singleton (the parameter's
  place), a heap write (`decl.writes_text_at`, the first place that set it:
  the write, the call of what sends or spawns text, the pass to a function
  that changes it), tasks, a `Set` through its World, or reading through its
  World or a query a component it changes. The game's question took a round
  trip this line answers.

Mount & Rifle built against this checkout (its origin/dev sources in a
scratch folder with junctions to its art, `tide_add_game` with `BATTLE_TEST`
and `ARMY`, release, 12 threads, hidden, `--host --seed 42 --frames 2400
--profile-warmup 1500`), mean ms a tick: `BotAct` 2.96 (the game's own figure
on nightly.24) to 0.63-1.0, `BotSense` 2.34 to 0.48-0.65, the match's tick
4.3-5.5 over two runs on a shared machine. Its log isn't a determinism check:
two identical runs differ, as the host paces ticks by the clock.

Tests: `tidec_e2e_ordered_split` (`compiler/tests/e2e/ordered_split.tide`):
3,000 soldiers in several archetypes, some left out, whose system writes fixed
arrays, spawns, sends a world event through a function and an entity event,
destroys and adds; a second system only sends; handlers log every event in
order. Six ticks on one thread against a fake eight-thread pool (the runs'
queues joined, every tick on threads) and against the platform's pool five
times over: world hashes and logs equal every tick. Giving the tasks the runs
in reverse fails it on the first tick. The schedule goldens gained their `on
one thread` lines and nothing else.

Validation: the whole native sanitize suite (Windows ASan), 3,267 of 3,270.
`tide_server_smoke` failed in the full run and passes alone. `inside_out_tests`
and `underfoot_tests` (the private prototypes, built into this tree) fail with
both compiler changes switched off too: their commands aren't taken, so that
checkout is behind the engine, not this. web-debug: the new fixture, the
parallel, canonical, stable-entity and event fixtures, `tide_platform_jobs`
and the schedule goldens. The docs build.

Gaps: extern per-entity systems still run on one thread (their rows ABI takes
runs, so the same split would fit; not done). A system that changes a
singleton or the heap is the language's rule. A chunk-split system
(`system_splits`) doesn't go in identity order and needs none.

## A frame's parallel loops on the pool, a row's local fields in steps, steps that read scratch values (2026-11-12)

From Mount & Rifle's view breakdown (1,000 soldiers toward 144 Hz; its biggest view goes through ~1,000
`mut Trooper` rows writing local fields). Asked: could a `parallel` loop over a query's rows in a view write the
row's local fields. Looking into it found two things that mattered more.

- **Every parallel loop in a view ran on the frame's thread.** `tide_parallel_for` shares a loop out inside a
  tick on threads (`current`), in a tick on one thread (`round_loop`, `serial_jobs`) and inside a worker
  (`tide_worker_parallel`, `worker_jobs`); a frame's views are none of those, so their loops ran in order,
  whatever they cost (Mount & Rifle's `DrawFlora` loops among them). An earlier line in rendering.md ("the
  views' parallel loops") assumed otherwise.
- **A step that read a list its code made crashed on another thread, in match code too.** A local's text, list,
  map, set or grid is in its thread's scratch area (`tide_scratch_slots`, thread-local), and a step is handed the
  handle by value: on another thread it's an offset into that thread's area. `parallel_views.tide`'s `Weigh`
  (a system: `mut List<float> scale = []; ... parallel (var k in t.values) ... scale[k % 64]`) segfaulted on the
  platform pool on `dev`. Nothing refused it; a loop *over* a local's list or grid was already refused.

Built:

- **`tide_loops_enter(jobs)` / `tide_loops_leave(previous)`** (`tide/jobs.h`, `engine/src/jobs.c`): a
  thread-local `frame_jobs`. Outside a tick, a loop whose cost is at least `ROUND_WORK` (the same bar as a tick
  on one thread: 50 us of tidec's units natively) takes the pool in a round of its own (`frame_loop`,
  `take_frame_loop`: `jobs->run`), cleared during the round so a loop inside a step runs in order. A joining
  thread only syncs memory: there's no tick to `prepare` it, so the steps set their thread up themselves
  (next point). A pool of one thread, or NULL, is none. Called by hosts, not by generated code:
  `tide/host.h` (`tide_run_frame`) enters around `frame_shards` and `frame` with `tide_run_gui.jobs` (the platform's pool) and
  leaves after; local handlers (`game->events`) run before it, outside. `platform/src/embed.c`'s own frame (a
  timeline's picture) enters too since the evening; its ordinary frames are the host's.
- **A view's steps set up the thread they land on** (`gen_par_local_use`, `gen_parallel_call` in
  `compiler/src/codegen.c`): the loop's call site reads the frame thread's three heaps
  (`tide_world_heaps[TIDE_IN_MATCH / LOCAL / VERIFIED]`) into locals that are captures of every view loop
  (`tide_heap_m`, `tide_heap_l`, `tide_heap_v`, in `par_captures`), and each step function starts with
  `tide_text_use`, `tide_text_use_verified` and, with assets, `tide_pack_use`. Nothing is put back: on the
  frame's thread they're what was there, and a pool thread's next tick or worker sets its own.
- **Loops that stay on their thread** (`par_stays`): the loop passes `tide_parallel_for` a cost of 0, which every
  path reads as "not worth sharing" (a tick on threads wants `LOOP_WORK`, rounds want `ROUND_WORK`, workers
  100,000), so its steps run in order where the loop is. Task sizes still come from the real cost. Three cases:
  steps that read a variable of the code around them whose type holds text, a list, a map, a set or a grid
  (`worker_has_storage` or `TY_GRID`, over `outer_locals_stmt`'s captures; a worker's captures are made its own
  already and are left alone), which is the crash's fix, for systems and handlers as for views; local code that
  isn't a view (local handlers: only views' steps set their thread up); a sharded game's views (which shard a
  view draws is its thread's).
  It's by type: a local that only borrows a world's list keeps its loop on one thread too.
- **A step reaches its row's local fields** (checker: `check_local_field`, `check_step_write`,
  `check_parallel_query`, `check_local_field_calls`; spec, Read-only entity queries, Provisional). Reads through
  any binding; writes through a `mut` binding's local fields, directly or handed to a function (the binding
  whole or one field). Codegen (`gen_parallel_query_call`, `gen_par_query_steps`): before the steps, a pass over
  `tide_rows` in order calls `tide_query_fields_mut_<C>_local` for each binding a foreach's row would make ready
  (`gen_query_field_refs`' test: a `mut` binding, or one the code can mutate through a query), which attaches
  storage and runs a reused identity's reset; each step then begins as a foreach's row does (`gen_query_fresh`,
  `gen_query_field_refs`), where the same call is now a find and a pointer, changing nothing shared. Rows of an
  indexed loop are made ready for its candidates only, as its foreach does. Refused: a step writing a local
  field that holds text or lists (`holds_heap_beside_meshes`), a `mut` binding handed to a function when its
  component's local part holds any, and a step reading the local fields of an outer parameter of a component
  the loop's rows write (the code's own entity is one of the rows).
- A `parallel` loop over a view's query now counts as going through it for lanes-only rows
  (`stmt_query_uses`, `stmt_row_reads` in `lane_reads_codegen.inc`): before, any `parallel` over a query made the
  view copy and blend every lane of every row.

Tests: `tidec_e2e_parallel_views` (`compiler/tests/e2e/parallel_views.tide`, 3,000 men in two archetypes): a
local singleton's list filled from rows and worked out by a parallel loop; a parallel loop over
`query (mut Man m)` whose steps skip some rows, read and write three local fields and write one through a
function, with a foreach after it reading what they left; a view whose steps read a list it made; and the
system above. With a pool that's one thread saying it has two, each frame makes exactly two rounds (the list's
and the rows'; the scratch reader's none) and every result is bit-equal to no pool; on the platform's pool,
ticks and frames, five runs of six frames equal one thread's (this is the run that crashed before).
`jobs_loops_outside_a_tick_call_a_frames_pool_in` (`tests/test_jobs.c`): no round before enter, one for a
costly loop and none for a cheap one or one inside a step, none for a pool of one thread, none after leave.
Errors: `parallel_query_local_text`, `_local_handed`, `_local_outer`, `_local_read_only` (replacing
`parallel_query_mut` and `parallel_query_local_field`, which are no longer errors). Measured: benchmarks.md,
Views' rows and loops on threads.

First use, and what it showed (2026-11-12, later): Mount & Rifle moved its near men's posing (PoseMan and two-arm
IK, ~0.1 ms a man) into `parallel (var row in men)` over `query (mut Trooper t)`, ~1,000 rows of which a guard
passes about six, on nightly.29, and took it out again: the views cost 1.0 ms where the one per-entity view had
cost 0.79. Its patch built against this checkout with a temporary trace around `frame_loop` (not kept): the
loop took the pool on every frame (1,001 tasks, a row each, reckoned at 45,944 units a row), and in the measured
frames a round took 140 to 196 us against 147 us for the same steps kept on the frame's thread (the same binary,
alternating runs). In the run's first 600 frames, where more men are posed at once, it was 837 us in order and
293 shared. So the loop wasn't the loss, and wasn't a gain either: in the steady battle about one man is posed
anew a frame (half the whole men reuse last frame's pose, by the game's own rule), a step can't be split, and a
round's wake is tens of microseconds. The loss was two more passes over the thousand men: the loop's view makes
1,000 blended rows and readies 1,000 cells (~0.1 ms) before any step runs, and the per-entity view that draws
from the results cost 0.23 to 0.28 ms for 994 rows that return on their first line, as each row's 128 lanes are
gathered and blended before the code runs.

Gaps:
- Built the same evening: **a view's guard** (ecs.md, top section), for a per-entity view's tests of local
  state; the game's `DrawWholeMen` went from 0.23-0.28 ms to 0.12 with its patch, what's left being the six men
  it draws. A view's query still makes every row whole before a `continue`.
- Built later the same evening: **a frame's loops are timed** (`frame_cost`, `frame_job` in
  `engine/src/jobs.c`), as a tick's systems are since the morning: on a pool that times its work
  (`tide_jobs.timed`) a loop outside a tick is known by its steps' function (32 kept by the frame's thread, the
  oldest replaced), and what it took against tidec's reckoning (`note_cost`, `work_of`; every thread's steps
  summed for a round) raises its work next time, never lowers it. So three men posed at 0.1 ms each, reckoned
  at a seventh of the bar, go to the pool from the second frame. A loop that passes no work (`par_stays`) and
  one of a single step stay put whatever they took. `frame_loop` now runs the steps itself where it doesn't
  share them. `jobs_a_frames_costly_steps_go_to_the_pool_once_timed` pins it: no round on an untimed pool, none
  the first time on a timed one, one each time after. With the rows' guard (ecs.md) Mount & Rifle's patch went
  from `PoseWholeMen` 0.19 ms to 0.12-0.17 and `DrawWholeMen` 0.12 to 0.09-0.15 (a busy machine): its three
  views now total about 0.6 ms against the 0.79 of the one view they replaced.
- Built the same evening: **`tide schedule` ends with every parallel loop** (`print_parallel_loops`,
  `par_stays_why` in codegen.c, `each_parallel_loop` in indexing.c): what its steps go through, a step's
  reckoning, and from how many steps it's worth the threads (the runtime's `LOOP_WORK` and `ROUND_WORK` in
  tidec's units, native; never fewer than two), or what keeps it on one thread, naming the variable. And the
  views with a guard. `compiler/tests/schedule/parallel_loops.tide` pins all of it; `grids.txt` gained its
  loops. Still not there: the profile doesn't count a view's rounds, and a reckoning counts a step that returns
  on its first line as its whole body.
- A step can't write a row's `Pose` (it holds a list), so the game copied 128 bones out to a fixed array and back.
  `without ownership` isn't a query filter.
- A round wakes the whole pool, as a tick's loop does; a loop a little over the bar on a 12-thread pool gains
  little. Nothing times a frame's loops (a tick's systems are timed since this morning).
- A tick must not run on the pool at the same time as a frame's loops: both are rounds, and the pool takes one
  caller's at a time. Today the host runs them one after the other on one thread. Overlapping a tick with the
  views (the game's other gap) has to settle that first. (Settled the same day: the pool gives a round to one
  caller at a time and the other's runs on its own thread, top section.)
- Local handlers' loops, sharded views and Unity's frames stay on one thread. A step still can't draw, send a
  local event, or fill a list of the code's by index; results go through the row's local fields or a
  singleton's list of plain values.
- The preparing pass and each step both look the row's cell up (`tide_attached_find`); since the evening the
  second is a guess that hits (ecs.md, top section).
- Steps reading scratch values stay on one thread rather than reading them across threads: scratch handles
  would have to name their thread, and its slot table not move while another reads it.

## Systems' work from their measured time (2026-11-12)

Gap, from Mount & Rifle (1,000 soldiers, nightly.13): a system of costly rows
ran on one thread however long it took. Not a row floor: `tide_run_systems`
goes by tidec's static work (`stmt_cost`: a call counts 30, a `Math.Sin` among
them, unknown loops 8 trips), and the report's `Heavy` (400 `Math.Sin` a row,
7.2 us a row measured) reckons at about 16,000 units a row, a twentieth of
what `PARALLEL_WORK`'s "half a millisecond" assumes. So 1,100 rows stayed
under `PARALLEL_WORK` (one thread, 7.8 ms) and 1,280 went over it (0.84 ms).
In a tick already on threads, the same underestimate kept a chunk in one piece
(`pieces_for`'s `worth`).

Built, in `engine/src/jobs.c` alone (no generated code or exported function
changed; `tide_jobs` gained a last field):

- `tide_jobs.timed`: the pool wants its ticks timed. The platform pools set it
  (desktop and web threads); a pool a test counts rounds or tasks with leaves
  it false and gets tidec's reckoning alone, the same every run, so the
  round-pinning fixtures (`parallel_work*`, `parallel_fill`, `test_jobs.c`)
  don't rest on a clock. `bench/versus`'s limited pool copies the real one's.
- Each system's time is measured every tick: on one thread, one clock read a
  system (each starts where the last ended); on threads, around each taking
  of tasks, summed (`system_state.spent`). `note_cost` keeps two sums per
  system, nanoseconds and tidec's work for the same ticks, each losing an
  eighth a tick: valid from the first tick, averaged over about eight, which
  is what a coarse clock (a browser's, 5 us and up) needs for systems
  shorter than its step. A tick the system didn't run in (no rows, an Owned
  predicted tick that left it out: work 0) adds nothing.
- `work_of(cost, work)` is the system's work this tick: tidec's, or
  `work * ns / work_then * WORK_PER_NS` (40 units a nanosecond, which is what
  `PARALLEL_WORK` = 0.5 ms and `PIECE_WORK` = a few us already meant) where
  that's more. Scaling by tidec's work keeps it per row: a system that gains
  rows is reckoned at once. Only ever raised: tidec overestimates cheap
  SIMD rows, and the benchmarks' thresholds were tuned on that.
- It feeds the tick's decision (the sum against `PARALLEL_WORK`) and
  `pieces_for`. Taking tasks a few at a time (`batch`) goes by it only for a
  system in pieces; whole chunks keep tidec's reckoning: by measured time
  `versus_particles` (a million cheap rows, 489 chunks) took chunks one at a
  time and ran 5% slower (1.06 against 1.00 ms; their time on threads is
  mostly waiting for memory).
- `MOST_PIECES` 16 to 64: one chunk of costly rows on 12 threads went in 16
  equal pieces, a round of 12 and then one of 4 (0.96 ms for the report's
  1,000 rows; 0.69 with 24).
- The sums are the tick thread's (`cost_tables`, thread-local, four tables
  keyed by the `tide_system_tasks` array and its count, the oldest replaced):
  never in a world, a snapshot or a hash. A server's and a client's worlds of
  one game share a table. After a hot reload a table's address may be reused
  with the same count: the stale sums are gone in a few ticks.

It rests on timing, deliberately: only which threads run what depends on it,
never a result (pieces and tasks merge in one thread's order, as before), as
with the desktop pool's `choice`. Feedback: time measured on threads is the
tasks' sum, which contention inflates, so a tick near the threshold that goes
onto threads tends to stay there; the cost of that error is small (workers
with nothing to do sleep), the other error cost 9x.

Measured (release, Windows, 7900X, 12 threads, hidden, `--profile` match mean
ms a tick, alternating runs; before = the same build with timing off):

| Game | Before | After |
|---|---|---|
| report's `Heavy`, 100 rows | 0.716 | 0.105 |
| 250 | 1.79 | 0.200 |
| 500 | 3.56 | 0.366 |
| 1,000 | 7.14 | 0.688 |
| 1,100 | 7.92 | 0.764 |
| 2,000 (was on threads already) | 1.36 | 1.36 |
| 8 chained systems of ~17 us, 1,000 rows (tick under 0.5 ms: one thread) | 0.138 | 0.137 |
| 40 chained systems of ~24 us, 1,000 rows | 0.95 | 0.39 |
| 40 chained systems of ~95 us | 3.86 | 0.68 |
| 100 chained systems of ~14 us | 1.40 | 0.90 |

Chained systems each write a component the next reads, so none run alongside:
the gain is each one's chunk in pieces. `versus_systems` at its defaults, 12
threads: 2.68 to 0.53-0.7 ms; `versus_particles`, `versus_wide`,
`versus_nbody`, `versus_events` as they were (within their runs' spread).
Seen on the way, not from this change: `versus_particles` takes about 1.0 ms a
tick against C's 0.083 on 12 threads, where 2026-10-20 measured about 0.09.

Tests: `jobs_costly_rows_go_in_pieces_once_timed` (`tests/test_jobs.c`): rows
tidec reckons at 1,000 units that spin for 2 ms stay one task on an untimed
pool for three ticks, and on a timed pool are one task the first tick, then
16 pieces (8 threads) every tick after. Results against one thread are the
existing fixtures' (`tide_platform_jobs` runs the real, timed pool natively
and under Node). Passing: sanitize (ASan) `tide_tests jobs`, the jobs, rows,
events and parallel e2e fixtures, `tide_platform_jobs`, `versus_systems`,
`versus_particles`; web-debug `tide_platform_jobs` and the parallel fixtures.

Gaps: a system that doesn't split (it changes text, a list, a grid or a
singleton, or calls `Set`) still runs on one thread whatever it costs; that's
the language's rule, not a threshold (one that only queues changes splits in
order since later that day, above). Nothing lowers tidec's
reckoning where it's too high. The static estimate itself is unchanged (a
system's row cost still doesn't count the bodies of the functions it calls,
as `step_cost` does for loop steps): it now only matters for a system's first
tick and for untimed pools. Web numbers weren't measured.

## Owned prediction's selected execution (2026-11-07)

The owner's revised Owned contract selects only owned work and conservative
data/effect dependencies during predicted ticks. Generated per-world selection
is computed serially before dispatch and read-only until systems join. Existing
waits, phase order, task recording/settlement and system profile indices remain;
excluded count/run/own/done wrappers perform no work. Confirmed/server ticks use
the full original table. No jobs runtime or exported ABI changed. Algorithm,
side-effect fallbacks, snapshot handling and native/WASI tests are in
language.md, Owned singleton dependency selection.

## Final logical spawn keys and canonical row ranges (2026-11-06)

Owner-approved Entity128 migration supersedes temporary handles and their
settlement below. A spawn returns its complete causal key immediately. Runtime
split tasks record those exact keys and register private storage in deterministic
source order when the system joins; Entity fields and queued values are never
rewritten. Generated match systems with ordered effects traverse admitted entities
in canonical logical-key order across archetypes. Independent numeric row work
retains its existing chunk/piece execution. C retains the complete final key.

Canonical traversal can visit a small run inside a physical chunk. Such a run
must use ordinary in-place copy-on-write, preserving the untouched rows; the
uninitialized apart-page optimization applies only when the run fills the whole
chunk. Otherwise each partial run erased other rows, repeatedly resetting a
Pack component and spawning another bag every tick. The shard movement test
exposed this; the canonical_rows fixture also checks untouched wide fields and
C writes across archetypes and retained snapshots.

Validation so far: scoped Windows ASan jobs/attached/protocol tests, the nine
existing shard movement/checkpoint/recovery cases, canonical_rows, stable_entities,
stable_async, handler_queries and visibility_predicted pass. Final native/web
coverage is recorded with the completed migration. Tagged shard borrowing and
its retry/clock contracts are in sharding.md.


## Scene preparation outside ticks (2026-11-05)

The approved prepare block (language.md) runs on the existing owned background worker provider, on one private match
world, with normal deterministic helpers/parallel loops. No system tick runs concurrently on that world. Pending
roots run in recorded load order. Source/complete world adoption stays on the session thread. Top-level preparation
statement boundaries flush mutable components and publish locked retained snapshots for observational views; loops
and helper interiors need not publish. Cancellation checks are at loops, helper entry and handler statements,
avoiding a check for every ordinary assignment in a hot helper. Leaving/reload waits for provider completion after
abandoning work, so trusted extern C must finish. Hosts without background/help support fall back synchronously,
as local workers do. Threaded publication/running-helper cancellation and exact generation counts are fixture-tested.
Readiness declares global scene reads even without an explicit World/query parameter, including through helpers;
`schedule/preparation` pins the resulting dependency. Schedule output lists authored preparation separately from
tick-end handlers. Parallel preparation callbacks bind packed constants on each helper thread; the assets fixture
checks exact results against serial preparation on native and WASI pools.

## Parallel loops over a query's rows (2026-11-05)

Owner's decision (notes/indexed-loops.md, the indexed-loops design): parallel
loops over a query's rows exist, and one-sided effects between entities go
through `Send`. Built: `parallel (var f in fighters)` in systems, views and
handlers (match and local). Picks, Provisional in `docs/spec.md` (Read-only
entity queries):

- **Checker** (`check_parallel_query`, `check_parallel_steps` in
  `compiler/src/check.c`). The loop sets `stmt.query` as a query foreach does,
  and its variable is the foreach's row type. `by`/`offset` and a typed
  variable are errors. The row is read-only: `check_step_write` names it
  (`query row 'f' is read-only ...`); `check_name` lets the loop's own row in
  (other rows stay refused in steps). Not yet: a query with a `mut` binding
  (refused at the loop), and attached local fields read in a step, directly
  (`check_local_field`) or through a helper (`check_local_field_calls`, by
  `call_site.parallel`). Steps keep every other parallel-step rule, with one
  exception in `check_step_expr`: a query loop's step may `Send` a match event
  of plain values; a command, a local event, or an event holding text or lists
  is an error naming why. Helpers that send are still refused (the `acts`
  check), as in every parallel step.
- **Codegen** (`gen_parallel_query_call`, `gen_par_query_steps`, the query
  branch of `gen_par_heads` in `compiler/src/codegen.c`). The rows are an
  array in the foreach's order: a view's or a local handler's own row array
  (`tide_q<n>`, `tide_q<n>n`), or, for a system or match handler, rows gathered
  into a `tide_par<n>_row` array (malloc'd, freed after the loop) by the
  foreach's own table walk (`gen_query_walk` with `gather`, which copies each
  row instead of running a body). Tasks are runs of `per` rows: `ceil(n/1024)`,
  at least `ceil(4096/step cost)` clamped to 1..16, as `tide_par_list_begin`
  picks blocks. `tide_parallel_for` runs them with the usual cost
  (`rows * step cost`). Each task's step function walks its run, binding the
  row as a copy; rolls are `tide_random_step(tide_par_dice, row index, 0, 0)`.
- **Order of sends.** When a step sends, the loop allocates one zeroed
  `tide_queue` per task; the step function points the thread-local
  `tide_task_queue` at its task's queue (restoring the previous value after),
  so `tide_cmd_push`'s `tide_recording` records there. After the join
  (`tide_memory_sync`), the queues are appended to
  `tide_recording(&tide_w->commands)` in task order and freed. Tasks are
  contiguous runs in row order, so the result is exactly what the foreach
  records, on any number of threads; in a split system the task queue is the
  system task's, which `jobs.c` merges and settles as before. In a match
  handler it's the world's queue being dispatched: appended items come after
  the current one, as ordinary pushes do (`tide_queue_append` keeps places).
  No runtime function was added: `tide_task_queue`, `tide_queue_append`,
  `tide_queue_free`, `tide_parallel_for`, `tide_realloc` are existing API.
- Work estimate: a query loop's work can't be told before the tick
  (`gen_parallel_work` returns false), so a tick with one runs on threads, as
  for a component's list.

Tests: `tidec_e2e_parallel_query` (`compiler/tests/e2e/parallel_query.tide`):
6,000 bodies over four matching and unmatched archetypes, a foreach, a system
parallel loop and a match handler's parallel loop with the same guard each send
a world event (with a costly computed value) and the system ones an entity
event; the logs written by the handlers must be identical in order and value,
on one thread, on a fake two-thread pool, and on the platform pool (5 runs,
hashes equal to one thread each tick). Reversing the queue merge was caught. A
view loop over its row array builds. Errors: `compiler/tests/errors/
parallel_query_*.tide` (by, typed row, row write, outer write, mut query, local
field, send of text, send of a local event, in a function, spawn). Passing:
sanitize (ASan) natively and `web-debug` (with threads).

Gaps: spawning, `Add`/`Remove`/`Destroy` from steps (the same per-task queues
would order them, but spawn handles need settling per loop task); helpers
that send; local-field reads; `mut` bindings; local events from view steps
(the local world's queue isn't `tide_recording`'s); the gathered array is
allocated each run (no cache across ticks yet; the indexed-loops cache would
own it).

## Parallel loops in ticks on one thread, and costly steps (2026-11-04)

Gap (nightly.21-32): a match `parallel` loop over a singleton's list ran at one
thread's speed. Cause: `tide_run_systems` decides threads before the systems
run, from each system's `count` (its parallel loops' terms read the list's
count or the grid's chunks as they are then). A list a system resizes that
tick, or a sized grid with no chunks yet, counts as empty, the tick runs on
one thread with `current` NULL, and `tide_parallel_for` ran the steps in
order. (The reported grid speedup came from a grid that already had chunks.)
Second, `stmt_cost` counts any call as 30, so a step that calls one costly
function was reckoned as nothing: 80-1,500 such steps stayed below both the
tick's `PARALLEL_WORK` and the loop's `LOOP_WORK`.

Fix, with results unchanged (steps are independent; random rolls are by
place; `tide_par_list_end` writes in order):
- In a tick on one thread, `tide_parallel_for` calls the pool in for the loop
  alone (`round_loop`, `engine/src/jobs.c`) when the loop's own cost, from its
  real step count, is at least `ROUND_WORK` (`PARALLEL_WORK / 10`: 2M units
  native, 6M web). Each joining thread runs `tick->prepare` before its first
  step, as a tick thread does; the caller's thread is already prepared.
  `serial_jobs` is cleared during the round, so a loop inside a step (helper
  C, which the checker can't see) runs in order. The decision is the static
  estimate against a constant: no timing.
- `step_cost` (`compiler/src/codegen.c`) is `stmt_cost` with the bodies of
  called Tide functions and methods counted, four calls deep (`cost_callees`),
  capped as before. It's used only for parallel loop steps (the tick term in
  `gen_parallel_work` and the cost passed to `tide_parallel_for`); system row
  costs are unchanged, and so is `loop_work_known` (a callee's unknown loop
  counts 8 trips, as in the system cost).
- `tide_par_list_begin` takes the step cost: tasks get at least
  `ceil(4096 / cost)` blocks, clamped to 1..16 (16 as before for cheap steps),
  still at most 1,024 tasks, so a list of a few hundred costly steps makes
  enough tasks for every thread.

Repro (`Probe` from the report, 361,201 elements, `Heavy` 400 Sin+Cos), release,
12 threads, `--profile`, systems' total ms over the run: before Par 2,494, Ser
2,498, Grid 2,490 (all on one thread: the grid had no chunks either); after
Par 450-483, Ser 2,458-2,662, Grid 228-249. Par's first round came out on the
home cache group (six cores) and Grid's on every group: the desktop pool times
both ways per work function (`choice`), so it's the trial order, not the loop.
Persistent short lists, mean per tick over 58 ticks: 80 steps 0.574 to 0.088
ms; 1,500 steps 10.3 to 1.04 ms.

Tests: `tidec_e2e_parallel_fill` (`compiler/tests/e2e/parallel_fill.tide`):
a list resized and looped in the same tick, and a 400-element list of costly
steps; a counting two-thread pool whose rounds run thread 1 first must see two
loop rounds in the first (one-thread) tick and one tick round in the second,
with hashes and every element equal to one thread; with the platform pool,
threads other than the caller's must join a first tick's loop rounds (all 12
did here), and hashes match. `jobs_costly_loops_in_a_tick_on_one_thread_call_threads_in`
(`tests/test_jobs.c`) pins one round for a costly loop, none for a cheap one or
a loop inside a step, `prepare` on the joining thread, every step once.
The existing `parallel_work*` fixtures still pin rounds (the constant variant
stays at none).

Language-level option not taken: a per-loop cost hint (an attribute on the
loop, or on a function, saying its steps are expensive) would let a game
force sharing where tidec's static estimate is still low (steps whose cost is
in C, or in loops with unknown bounds). Not added; the owner's call.

## Started operations and worker admission (2026-10-25)

Task registry reads/writes participate in static scheduling and propagate
through helpers. Writers serialize in the existing order. Task passes visit
already-waiting operation dependencies before their continuations using an
iterative walk over start-ordered due rows; every due row runs at most once
per pass. Captured handles and direct await preserve serial logical results.
Match registry/results are ordinary world state; local ones are machine-local.

Worker tickets use a hash index and retired-only collection instead of scanning
all live jobs for each lookup/release. Defaults limit private pending jobs to
64, retained input captures to 64 MiB and frame capture/result transfers to
4 MiB. Limits are soft: the crossing/oversized operation finishes so progress
is possible. Direct starts suspend before arguments when admission is full;
arguments containing waits precheck without reserving across the child wait,
then retry with their frozen values. First-class calls capture at call time:
their adapter-owned values are outside the private worker-capture budget.
No hard process/job allocation cap or new allocation failure result is promised.

Generated Tide safe points stop abandoned running calculations and synchronous
helper chains; trusted C finishes its current call. Parallel helpers inherit
the owner and stop taking iterations after cancellation. Cancelling one waiter
preserves other consumers; dropping the last consumer abandons worker adapters,
while ordinary async calls retain fire-and-forget. Disposal/rebind still drains
provider completion before unloading code. Tests and timing: language.md,
benchmarks.md.

The first clean voxel batch showed extra pure-worker safe-point overhead.
Pure workers now keep loop/body and helper-call checks without redundant checks
before every ordinary statement. Async tasks and task_cpu helper chains retain
statement checks, including immediate stopping after Cancel in a helper.
Full merged native/web coverage retains queued/running cancellation, shared
waiters, parallel helper stopping, self-cancel and teardown regressions.

## Background local calculations (2026-10-24)

Local workers share the platform's bounded pool with tick rounds. Their separate
queue does not count an executing calculation as part of a synchronous round.
Desktop workers check foreground invitations between jobs; browser workers
distinguish foreground invitations from queue wakeups. Queue publication is
synchronized, including Wasm memory growth. Match scheduling is unchanged.

Worker parallel_for queues bounded helper callbacks, and its caller consumes
iterations too. Helpers take priority; waiting workers only help loop work,
never recursively entering another whole calculation. A full pool therefore
needs no spare thread to finish its loops. Steps inherit private heap resolvers
and use independent scratch; generated capture/output materialization prevents
cross-thread scratch handles. Small loops or missing background/help callbacks
run serially. Without a background provider, a worker runs on its first poll.

Owner loss abandons publication without blocking that frame; disposal/rebind
drains callbacks before capture release or code unload. platform/tests/jobs.c
stresses twice the pool's thread count in background jobs with parallel loops,
checking off-frame execution, exact once-only results and frame-thread release.
Existing tick/pool regressions stay enabled. Language/lifetime tests: language.md.

The worker audit skips abandoned queue items before entering game code. Group
disposal atomically marks every item abandoned before helping the provider;
already-running callbacks still finish under the original completion contract.
test_worker pins skipping, full-group disposal and provider-owned item lifetime.

Read-only grid worker snapshots also participate in the existing automatic
foreach analysis. The planner receives a temporary slot for the already-private
input handle; steps cannot write it. Ordered reductions retain the serial row
walk. List-parallel helpers can read grid snapshots from the private input heap.
worker_grids pins both paths, including actual shared-pool list work.

## Schedule display grouped by stage (2026-10-22)

`print_schedule` groups systems by increasing dependency depth, preserving
their tick order within each stage, with a blank line between stages. A stable
counting pass builds a separate display array in linear time; it does not
reorder the program's systems, dependencies, queued effects or generated jobs.
`tide schedule`, `tidec --schedule` and game schedule targets share this output.
Stage numbers remain dependency depths, with no barrier between stages.

The existing exact-output fixtures cover independent late-declared systems
moving beside earlier systems of their stage, stable ordering within stages,
named phases and unchanged views/event-handler sections. The schedule guide's
demo now puts Steer and Expire together and explains when Fire can start.
The Box3D package's order fixture also reads this display; its expected names
now follow stage grouping for independent game/package systems.
Validation: warning-free full native/web debug builds with Clang 23.1.2 and
Node 24.21.0. The full native suite passed 2,553 of 2,554 tests; its sole failure
was the Box3D display expectation above. After refreshing it, all 23 schedule
and package-order checks pass. All 2,416 web tests pass, including WebGL,
D3D11 and WebGPU. The production docs build passes, the entire demo schedule
matches its guide example exactly, and all 20 compiler schedule goldens were
checked to change only stable stage ordering and group spacing. Diff checks
pass. Other operating systems, Android and sanitizers were not run locally;
no CI workflow was dispatched.

Merged origin/dev's documentation navigation changes, preserving both new
docs-site note entries at their shared insertion point. The merged production
docs build passes; there was no compiler or runtime overlap.

Cache groups in the desktop pool (2026-10-20, platform/src/jobs.c): cores that
share a last-level cache are a group, found on Windows from
GetLogicalProcessorInformationEx (the largest unified cache level; only where
there are several groups, each of two cores or more, between them every core,
at most 8). A Ryzen 9 7900X has two of six. Data moves between groups slowly:
a million particles tick in 0.086 ms on six threads pinned to one group, 0.38
on three of each and 0.27 on all twelve, the C as much as Tide, while N-body,
which is arithmetic, wants all twelve (0.43 against 0.62). So each worker is
pinned to its group (SetThreadGroupAffinity), the pool's home group is the one
it was made on, a thread that runs rounds is pinned to it from its first
round (unpinned, the system moved it between groups and the world with it:
0.13 ms where pinned was 0.086), and `run` takes either the home group's
threads or every group's, per work function (`choice`): its first six rounds
take each in turn, then the one whose last three rounds' middle time is
lower, and every 64th round the other one, so a work that changes is found
out. `run_some` and `more` stay in the home group first. Results never
depend on which threads run. Paired against the pool before, gated, two
rounds each on 12 threads: versus_particles 0.236 to about 0.09 ms, versus_life
0.21 to 0.13, versus_wide 10-15% less, versus_sand as it was, versus_nbody
within 5-10% (a few of its 32 rounds try the home group). macOS and the
web keep one group.

Linux and Android (2026-10-20, the owner's go-ahead with CI as the check):
`platform/src/cpu_groups.h` reads sysfs (`/sys/devices/system/cpu`:
`online`, each CPU's `cache/index*/{level,type,shared_cpu_list}`, the
highest unified level's being the groups, and
`topology/thread_siblings_list`, a core counted at its first CPU), with the
same rules as Windows: several groups, each of two cores or more, disjoint,
between them every online CPU, at most 8; otherwise one group and the old
count (`smt/active`). Workers pin themselves with `sched_setaffinity`, the
thread that runs rounds pins itself to the home group at its first round,
and the home group is the one `sched_getcpu` finds the pool made on
(`_GNU_SOURCE` in jobs.c on Linux and Android, for these and `CPU_SET`).
Android often shows no cache directories, or one L3 for every core
(DynamIQ), and keeps one group. `tide_platform_cpu_groups`
(`platform/tests/cpu_groups.c`) reads made-up trees on every system: a
7900X's two chiplets (two groups of six cores, SMT counted once), one
shared L3, groups of one core, sixteen CPUs without SMT in four groups, an
L2 last level, and none at all; and the list parser. It passes on Windows
and, built by hand, on WSL2's Ubuntu; there `jobs.c` builds warning-free
(strict C17) and a probe pointing the pool at the 7900X tree
(`TIDE_CPU_SYSFS`, a compile-time default for that) ran 20 rounds with the
caller and five workers on the home group's CPUs and six on the other's.
WSL2 itself shows one L3 for all 24 CPUs, so no multi-cache Linux machine
has run it: a Linux CI run, or a machine with chiplets, is its first real
test.

Grid parallel loops, performance pass (2026-10-20): `tide_par_window` works out a
row's runs (where each chunk's cells go in the window, and where they start in
their chunk's row) once for the window, as they're the same for every row that
doesn't pass the last int, and copies runs up to 256 bytes inline rather than
through memcpy, which a window called some 17,000 times a tick in versus_life.
`tide_par_window_put` compares and copies those runs inline too, and a task
whose own cells are exactly one whole chunk (one-cell blocks, the common case)
compares its rows and, if any changed, fills a buffer it takes uninitialized
(`buffer_for` with `whole`, grid.c) with every row of the window, rather than
copying the chunk into the buffer first and then the rows that changed: rows
that didn't change are the chunk's as it is, and no other task's own cells are
in that chunk. With the compiler's `writes_own` (language.md), versus_life
went from 0.583 to about 0.42 ms a tick on one thread; versus_sand, whose 2x2
blocks are mostly not chunk-aligned, about 5%. The geometry stays 64-bit where
it was (memory-safety.md).

Picking tasks on threads (2026-10-20), in `engine/src/jobs.c` alone, the
schedule and every result as they were:
- A thread takes a system's tasks a few at a time where each is little work
  (`BATCH_WORK`, 250,000 of tidec's units, as `PIECE_WORK`), as many as come to
  that, never so many that the system has fewer takings than the tick has
  threads (`system_state.batch`, `.batches`). Each task still runs with its own
  queue, temporary handles and settling, one after another on that thread, so
  what one thread would have recorded is unchanged; `finish_tasks` counts the
  takings' tasks done at once. Threads asked for a system's work count
  takings, not tasks.
- Looking for work starts at `tick_run.front`, the first system that may have
  something left to give out, which a thread moves past systems whose tasks
  and settling are all given out, rather than at system 0 every time.
- Each system's state is on a cache line of its own (`_Alignas(64)`, from an
  aligned start in its memory), so threads counting one system's tasks don't
  take the line from those looking at the next.
`versus_systems` (64 systems, 16,384 entities of each kind, tasks of 2,048 rows
of a cheap loop) went from 0.20 to 0.175 ms a tick on 2 threads and 0.14 to
0.12 on 4 (alternating runs); past 6 threads this machine's runs vary too much
to tell (threads on its second CCD). `jobs_cheap_tasks_are_taken_a_few_at_a_time`
(`tests/test_jobs.c`) checks that 64 cheap tasks on eight threads each run
once by their own number and ask for 7 threads, not 63; `platform/tests/jobs`
and the e2e row, world-set and schedule fixtures check results against one
thread. Measurements: notes/benchmarks.md.

Query local fields (2026-10-19): attached local access and mutable-query effects
propagate through synchronous helpers. Views/local handlers retain their
existing serial order and named phases; match component data remains read-only
and no World write is granted. Query mut helpers count as writers when lowering
source evaluation order, as ordinary mut arguments already do. No match scheduler/thread behavior
or runtime interface changes. Validation and alias contracts: language.md.

World.Set beside read-only component parameters (2026-10-18) uses the existing
global write effects and single-thread traversal, including writes propagated
through helpers. A row reads the current component when entered, then keeps
that value during its invocation. Generated page-slot reads prevent snapshot
sharing from changing later rows' observations. Mutable-overlap, parallel and
sort-comparison restrictions remain. No scheduling barrier, dependency rule or
runtime interface was added; `world_set_read` exercises the threaded scheduler.

Borrowed query helpers (2026-10-18) retain the source query's global component
reads and existing World reads-own-writes scheduling. World effects propagate
separately from borrowed query access, preventing implicit World capabilities.
Query operands are unsettled reads beside writers; source-order regression
calls read the old/new values around helper World.Set writes. An automatic grid
foreach containing a borrow stays ordered, while explicit parallel/callback
borrows are rejected. Existing multi-chunk/thread/snapshot query checks retain
their original deterministic pin; full validation is recorded in language.md.

Frame metadata and automatic loops (2026-10-18): Texture.size uses the frame
host's borrowed resolver, so explicit parallel steps read a precomputed int2
instead. Direct reads and helper chains give a diagnostic there. After helper
effects have propagated, the checker revisits automatic grid foreach loops
with the existing own_cell_only analysis; an indirect C/metadata read keeps the
loop ordered just as a direct one does. This corrects the earlier premature
parallel decision without changing the loop's serial semantics. texture_size
covers direct/helper ordered loops and two negative fixtures pin explicit-step
diagnostics. No worker receives a platform callback or frame-local pointer.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Determinism

### Systems that touch the same data

Named `[Stage(Enum.Member, offset)]` phases and the reported schedule stage are different concepts. Phase ordering adds dependencies between every earlier/later phase of the same enum, even for systems with disjoint data; offsets also order systems inside one phase. Different enums define independent phase orders. The reported stage number is the longest dependency depth computed by `analyze_parallelism`, not a global scheduling barrier: a system starts once its own predecessors finish. `compiler/tests/schedule/stages.tide` and `stages.txt` pin named-phase waits, offsets, same-offset concurrency, empty phases, views and event handlers. This distinction was clarified during the 2026-10-11 guide cleanup; compiler/runtime behavior and fixtures did not change.

- Two systems conflict when one writes a component or singleton the other reads or writes, unless they can never touch the same entity: the compiler proves that from the archetypes (`with Player` against `with Enemy`). From the archetypes alone (2026-10-06): a sparse component one system needs and the other excludes keeps them off the same entity, but not off the same pages, as both go through the archetype's chunks, so they wait for each other like any two (`can_meet` is `share_archetypes`). They used not to, and a chunk's page that a snapshot shares is replaced by whoever writes it (`tide_table_lanes_begin`): two such systems in one chunk at once each took a page of their own, and the one that ended last dropped what the other wrote. Made to meet there (a C function both call, which waits for the other), one of two entities lost its writes every tick. Nothing else may rest on two systems writing one chunk's lanes at once: a loop writes a page apart, and a chunk in pieces has a half-written page in the table while they run (see ECS and simulation state). A system that takes its entities' input, or their devices, reads whose each is, their `Owner` (`accesses_of`), so it waits for a system that changes `Owner`, which it didn't either. `compiler/tests/schedule/pages.tide` pins both. What code reaches through its `World` parameter (`world.Get<Health>(e)`, `Has`, `TryGet` and, with `mut World`, `Set`) is access to every entity's component, so it conflicts regardless; the checker takes it from the calls in the body (`decl.world_uses`, `check_world_call` in `check.c`) and in the functions the code passes its World to (`check_world_passing`: see Language), as the signature doesn't name the components, and `tidec --schedule` and the language server's hover and lens say it. A system that calls `Set`, or that reads through its `World` a component it also changes through a parameter, runs on one thread, and the latter writes its rows in place so what it reads of the rows it changed is the same on every machine (`world_reads_own_writes`). `Set<T>` is allowed beside read-only `T`, on any target and through helpers (extended 2026-10-18): each row reloads affected page addresses and takes its value on entry, including preceding rows' writes. Its value stays as entered until it returns, including copied containers whose stored allocations Set may reuse. Implicit Owner reads for input follow the current pages too. Mutable `T` still refuses overlapping Set, because its write-back could overwrite the same entity's Set (`compiler/tests/e2e/world_set_own.tide` and `world_set_read.tide`). Two systems that change the match's text or lists conflict too, as they share its heap. `Add`, `Remove`, `Destroy` and `Send` never conflict, because they're recorded and applied at the end of the tick in order. `Spawn` is recorded too, and only makes a spawning system that runs on one thread wait for those that spawn before it (below). What a function or method the system calls sends, spawns or changes is the system's own in all of this (`check_acts` in `check.c` sets `decl.spawns` and `decl.writes_text` on the system that calls one: see Language, in `notes/language.md`), recorded through the same `tide_w` into the same queue, a task's own on a thread. Event handlers run at the end of the tick too, as each event's turn comes, so they aren't part of the tick's schedule. Input and `Time` are only read.
- The tick runs on threads (`tide/jobs.h`): the schedule (`analyze_parallelism` in `compiler/src/parallel.c`) becomes a task graph, a task per system, or per chunk for a system that splits its entities. A system with fewer chunks than threads runs each chunk in pieces (`pieces_for` in `engine/src/jobs.c`: about two tasks a thread, each worth a few microseconds, up to 64 a chunk, 16 before 2026-11-12), its rows in order piece by piece, so a few heavy chunks keep every thread busy with the same results. Pieces of one chunk write the same pages at once, so none of them can take a page for it: before them, the system's `own` function begins every chunk's lanes of what it changes for all its pieces, and after them its `done` function ends them (`tide_system_tasks.own` and `done`, generated as `tide_own_<System>` and `tide_done_<System>`: `tide_table_lanes_begin_pieces` and `tide_table_lanes_end_pieces`). `own` is called by the first thread that comes for one of the system's tasks once what it waits for is done (`system_state.owned` in `engine/src/jobs.c`), never as the tick is set up: a system before it in the tick would find the pages `own` took, with nothing in them yet. `done` is called by the thread that ends the last piece, before the system's spawns settle and before anything that waits for it starts. A system written in C still has its pages made the world's own, copied, by its `own`, and has no `done`. `tests/test_jobs.c` checks the order of the calls with a pool of one thread that says it has eight. The platform layer has the threads (`tide_platform_jobs`, one per physical core: two threads of a tick on one core share its units and slow each other; on the web, one for each processor the browser counts, which is all it says: see Web builds); sessions take them in their desc and pass them to `tide_game.tick`. Each task records into a queue of its own, merged in the order one thread would have recorded it; the thread that runs ticks keeps the tasks' queues, pages and all, for the next tick. A tick with little work runs on one thread: tidec reckons each system's cost per entity (`stmt_cost`, counting a `for` from one number to another as many times as it runs, other loops as 8), and a parallel loop's steps as the world is now when its grid or list is a singleton's (`tide_grid_cells_about`, times 4 for what cells cost to read and write), and threads only start past about half a millisecond's work (one and a half on the web), as waking them and then reading on one thread what they changed in their caches costs more than they'd save below that. A worker with nothing to do looks for work 256 times in a row (`IDLE_LOOKS`, with the CPU's pause between looks where there's one), then leaves the tick and sleeps until there's work for it, which whatever makes the work asks it back for (`tide_jobs.more`: see Web builds, where it was first built, for what asks and why no call is lost). Desktop's pool sleeps its workers on a condition variable (`worker_loop` and `more` in `platform/src/jobs.c`), since 2026-10-05: before, a worker stayed in the tick until it was over, looking for work and yielding its core, and that slowed the threads that had work, most in ticks that mostly run on one. Measured on desktop (a release build on Windows, 12 cores, the least of runs taken in turn with the build before; saved as `desktop-idle-threads` and `desktop-idle-threads-before`): with 2,400 crates, Box3D's tick on 12 threads went from 1.62 ms to 1.32 and Box2D's from 0.84 to 0.74, which is what it takes on one thread; `bench_sand`'s clients simulate for 29 ms a second where it was 50 (191 where it was 236 at 1024 cells a side); `versus_life`'s tick went from 0.31 to 0.24 and `versus_sand`'s from 0.27 to 0.24; `bench_gallery` and the other versus benchmarks are as they were. More looks before sleeping were tried: 2,048 were no quicker, and 16,384 slower. The caller's thread can't leave a tick, which is its to finish: with nothing to do it yields its core after as many looks (`tide_yield`), so that a thread the system pushed aside doesn't hold the tick up for a time slice while this one spins. Web pages run on threads when they're cross-origin isolated. `platform/tests/jobs.tide` checks threads against one thread, tick by tick, natively and under Node.
- Besides components and singletons, systems wait for each other over what's the whole match's: changing text or lists (its heap; reading it alongside is safe, as the heap's page table is read atomically and replaced ones are freed once systems are done, `tide_heap_settle`), and spawning by a system that runs on one thread (entity IDs are handed out in order, and it hands them out as it runs). C is trusted and counts for neither. A system only splits its entities across threads when it changes no text, list, grid or singleton (`system_splits`).
- A parallel loop (`parallel (var at in cells by 2 offset o)` over a grid or `parallel (var i in items)` over a list, and a foreach over a grid whose steps only touch their own cell, `own_cell_only` in `compiler/src/check.c`) runs inside its system, a fork and join (`tide_parallel_for`): its steps go to a function of their own after the system's, with what they read of the code around them copied into a struct, and the pool's threads that have nothing else to do help (`help_loop` in `engine/src/jobs.c`) unless it's next to no work, as they're awake already and tidec can't tell how long the loops inside a step go on; in a tick on one thread, a loop worth it calls the pool in for itself (`round_loop`, 2026-11-04 above); outside a tick, or for a small loop, the system's thread runs them alone. A loop's steps count as its tick's work where they can be told (a singleton's grid or list, above); a tick with a loop whose steps can't runs on threads. Its tasks are tiles, the blocks that start in one chunk's place (`tide_par_tile`): every place a sized grid's blocks start in, or for a grid with an open axis, the places of the chunks it has and of those before them whose blocks get into them, where a block that takes in none of its chunks is skipped (`tide_par_live`). A step writes into a copy of its chunk (`tide_par_buffer`, made with a compare-and-swap, the first one's for all, from a pool kept across loops), and reads the grid itself, which doesn't change while the steps run; the loop's end puts the copies into the grid in chunk order (`tide_par_end`). Where it can (`plan_window` in `compiler/src/check.c`), a task reads the cells its blocks reach at their place plus constants once, into a window (`tide_par_window`), and its steps read and write a copy of it as plain memory, which the C compiler can run with SIMD; the rows that changed go into their chunks' copies (`tide_par_window_put`). A task's blocks are numbered from its own first one, and a window is where it starts and how many cells it is across, so nothing in a step's setup goes past an int wherever the loop's offset and cells are (see Language: no Tide code can corrupt memory). A parallel loop over a list (`parallel (var i in items)`, `tide/list.h`) gives each task a copy of its blocks' elements, which its steps write while they read the list itself, and the loop's end puts the runs that changed into the list, chunk by chunk (`tide_par_list_end`). A foreach over a grid that goes in order goes row by row (`tide_grid_rows`), over a grid with an open axis through the chunks it has, sorted, in the scratch area (`tide_scratch_memory`, freed with the area's mark).
- A system that splits and spawns gives temporary handles (`TIDE_ENTITY_TEMPORARY` in the generation, with its task's number; `tide_new_entity`). Once its tasks are done, it settles them (`tide_settled`): its entities get IDs in the order one thread would have given them, and its tasks' queues (`tide_settle_item`) and the entity fields of its `mut` components (`tide_settle_<system>`) get the real handles. Settling keeps the tick's order among spawning systems (each settles after the one before it that spawns is done) and comes before anything waiting for the system starts, so the IDs are the same as on one thread. Systems look entities up while one makes them, so the entity table's page table is read atomically like the heap's (`tide_entities_settle`). Text shows a temporary handle as `Entity(new)`, and C tells with `tide_entity_is_temporary`.
- The language server shows each system's stage and waits above it and on hover, and `tidec --schedule` (or the `<game>_schedule` build target) prints it as text for CI and agents. `compiler/tests/schedule/` pins its output. `tidec --outline` (`compiler/src/outline.c`, 2026-10-21) gives the same per system as JSON beside every other declaration (each stage, wait with its `describe_wait` reason, and the first few alongside, with the counts of the rest), for tide unity's `outline.json`; `compiler/tests/outline/` pins it.
- The analysis keeps who runs after whom as bits, a row a system, and whether two systems conflict as a yes or no; it only works out what a wait is over for the waits it lists. Those are every wait that's a system's own and the first 32 that go through another system, and it lists the first 32 systems each runs alongside, counting the rest (`decl.waits_unlisted`, `decl.alongside_count`): a game with thousands of systems has millions of pairs. Generated code names only a system's own waits (`tide_waits_<system>`), as one that goes through another system is done by the time that system is.

## Internal tick completion slots (2026-10-11)

The owner approved post-queue completion for physics history. Generated ticks allocate zeroed completion slots, one per extern system; only once-per-tick calls expose their own slot in tide_rows. Native systems register a stable context, not row pointers. Once jobs and queued changes finish, callbacks run serially in declaration order on the tick's caller, before Time increments. Registration cannot depend on worker arrival order. Slots/pointers are transient and never enter snapshots/hashes/packing. Physics SealHistory adds a final PhysicsStages.Step offset to register its callback; geometry capture remains at the physics step, while queued Snap/despawn metadata is final at completion.
