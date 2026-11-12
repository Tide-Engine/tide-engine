# The schedule and the tick on threads

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
  leaves after; local handlers (`game->events`) run before it, outside. `platform/src/embed.c` (Unity) doesn't
  call it: its frames stay on one thread.
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

Gaps:
- A round wakes the whole pool, as a tick's loop does; a loop a little over the bar on a 12-thread pool gains
  little. Nothing times a frame's loops (a tick's systems are timed since this morning).
- A tick must not run on the pool at the same time as a frame's loops: both are rounds, and the pool takes one
  caller's at a time. Today the host runs them one after the other on one thread. Overlapping a tick with the
  views (the game's other gap) has to settle that first.
- Local handlers' loops, sharded views and Unity's frames stay on one thread. A step still can't draw, send a
  local event, or fill a list of the code's by index; results go through the row's local fields or a
  singleton's list of plain values.
- The preparing pass and each step both look the row's cell up (`tide_attached_find`): about 30 ns a row twice.
  Handing the steps the cells' rows would halve it.
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
