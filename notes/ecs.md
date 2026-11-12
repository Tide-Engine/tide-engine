# ECS and simulation state

## A view's rows: the slot guessed, and a mut query's lanes (2026-11-12, later)

The same morning's "Remaining" said a per-entity view's row still cost about 50 ns in the blend's two lookups.
Measured with `bench_views` (20,000 rows, blended): 58 ns a row per entity, 53 for a query's row, where 2026-10-30
measured 10 and 8 (before entities had logical identities and a lookup became a hash of 16 bytes and a probe).
Mount & Rifle's views go through ~1,000 soldiers several times a frame.

- **A lookup guesses the slot first** (`tide_entity_slot_index`, `engine/src/entity.c`): a thread remembers the
  slot it last found an entity in and how far that was from the one before (`slot_hint`, `slot_stride`,
  thread-local), and looks at that slot and the one a stride on before hashing the key. A slot that is alive and
  holds the key is the entity's, in whichever table (the rule `tide_entity_location_at` rests on), so a guess
  is checked, never trusted: results are what the lookup gives. It fits what row code asks: the same entity
  again (what it was born as, then last tick's world, whose slots are a copy's), then the next row's entity,
  whose slot is most often one stride on, as rows and slots are both handed out in the order entities are made
  (men and their kit spawned in turn: a stride of 2). A table whose rows no longer follow its slots falls back
  to the lookup at the cost of two reads. Every caller gains: `tide_entity_location`, `tide_entity_born`,
  `tide_entity_alive`, the views' `tide_prev_<C>`.
- **A `parallel` loop over a view's query counts as a loop for its rows' lanes** (`stmt_query_uses`,
  `stmt_row_reads`, `lane_reads_codegen.inc`): a query named anywhere but as a foreach's value made its rows
  whole, and a parallel loop was "anywhere else".
- **Tests**: `entity_slot_guesses_give_only_the_keys_own_slot` (tide_tests): two tables, one a copy of the other
  with every third entity destroyed and others in their slots, asked in six orders (forward, back, strides of 2,
  7 and 37, alternating tables) against a scan of every slot; an unknown key, the null key, an empty table.
- **Measured** (benchmarks.md, Views' rows and loops on threads): a blended per-entity view row 58 to 21 ns, a
  query's row 53 to 13.
- **Remaining**: a blended row is still about twice 2026-10-30's (two checked slot reads and the born values'
  page, where there was one array index). A view's query with a `mut` binding looks its entity up once more a
  row for what it was born as, which the guess now answers. The GUI seed still hashes the entity for every
  per-entity view row.

## What a row costs in entity order: slots beside the ordered keys (2026-11-12)

Found profiling Mount & Rifle's 1,000-soldier battle toward 144 Hz. Two reports, one cause, plus two more found
on the way. A `foreach` over a query's ~1,000 rows cost 0.35 to 0.45 ms a tick, and per-entity systems cost 75 to
250 ns a row whatever they did (`LeaveField`, which reads one field and returns: 75 ns), where a probe's
per-entity system that writes its own component cost 6. Box3D's bookkeeping over 1,396 static bodies was 1.7 ms a
tick for the same reason. Results, row order, hashes and snapshots are what they were.

- **The cause: a key looked up for every row.** Since entities have logical identities (2026-11-06, below),
  anything that must go in entity order walks `tide_entities_ordered_archetypes`' sorted keys and asks
  `tide_entity_location` where each is: a hash of the 16-byte key, a probe of the map, then the slot. That is a
  query's `foreach` in match code (`gen_query_walk`), and every per-entity system that isn't split across
  threads (`gen_canonical_system_keys`: one that writes a singleton, takes a `World`, sends, spawns or destroys;
  extern per-entity systems too), which looked each key up twice (once to end the run before it, once to start
  its own). About 26 ns a row, more with the map out of cache. A per-entity system that only writes its own
  component goes by chunks and never paid it, which is why a probe of those looked fast.
- **Slots beside the keys** (`engine/src/entity.c`, `entity.h`): `tide_entities_ordered_archetypes_slots` gives
  the same cached keys and, beside them, the slot each was in when the order was made (one allocation, the slots
  after the keys; `tide_entities_ordered_archetypes` is now a call of it). `tide_entity_location_at(t, slot, key)`
  (inline) reads the slot: alive and holding that key, its archetype and row; anything else, `tide_entity_location`
  by the key as before. A slot is an entity's for as long as it lives in the table, and an order is retired
  whenever the keys change, so a slot that isn't the key's own means the entity was destroyed after a loop
  borrowed the arrays (its slot maybe a newer entity's: the key check refuses it), or went to another shard and
  came back to another slot; both give what the lookup gives. A row that moves keeps its slot, so row moves still
  keep the order. The walks above use it; nothing else changed in them.
- **A loop through an index that finds most rows** (`index_codegen.inc`; notes/indexed-loops.md, When most rows
  pass). The reported `foreach` starts with `if (row.t.down) continue;`, so it's an indexed loop, and its search
  gave back all 1,000 rows: found, sorted by identity and copied whole, 128 lanes each. Such a loop now goes
  through every row as the plain loop does, a candidate's copy holds only the lanes the body reads, and building
  an index copies only the lanes holding its keys.
- **A view's row blended from last tick's world looked its entity up five times** (`gen_prev_found`): its snaps
  and what it was born as in each world, then its place in the old one. `tide_prev_<C>` and `tide_prev_some_<C>`
  now find its slot once in each world (`tide_entity_slot_index`) and read the snaps, `tide_entity_born_at` and
  the place from there; `tide_load_<C>_at` and `tide_load_some_<C>_at` load by place (`tide_load_<C>` is a call
  of it). This is the views' own cost, not the tick's.
- **A chunk's lanes found once for the chunk, not for each run of rows** (later the same day, after the game
  measured nightly.24: its `FallOnField`, `Fall` and `Routed` were still 77 to 130 ns a row). Entities spawned in
  one tick are ordered by their lineage, a hash, so a battle's thousand soldiers come in an order that has nothing
  to do with their rows: nearly every run of rows is one row. `gen_system_chunk` wrote, for each run, where every
  lane the system uses is (`tide_table_column`, `tide_table_lanes_piece`: a page's header read for each lane),
  and two NULL stores for every lane of a `mut` component it doesn't use: 180 stores a row for the game's
  92-lane `Trooper`. Now (`gen_canonical_kept`): before the keys are gone through, each archetype has
  `tide_kept_<a>` (the chunk whose lanes were last found) and arrays `tide_kept_col_<C>_<a>`,
  `tide_kept_was_<C>_<a>` and `tide_kept_own_<C>_<a>`; a run names them (`tide_col_<C>`, `tide_was_<C>` and
  `tide_own_<C>` are pointers to them) and fills them only when its chunk isn't the one kept. Lanes no row
  copies are left unset rather than NULL: nothing names them (`gen_rows_columns` hands over the used ones by
  number). What keeps the pointers right for the task: the lanes it changes are all begun before its first row
  (`gen_canonical_lanes`), those it only reads nothing writes while it runs, and a component it sets through its
  World isn't kept (`tide_pages_<C>`, asked for each run as before). The entity and scene columns, whether the
  chunk has holes and the filters' presence bytes are still asked per run. Only systems in entity order whose
  lanes are prepared (`generating_canonical_prepared`); a one-Send or self-destroying system's batched loop
  (`gen_chunk`) and extern systems are as they were.
- **Tests**: `kept_lanes` (e2e, native and WASI): 2,600 entities of a wide component in two archetypes, more
  than a chunk of each, spawned in one tick, through a system that writes a singleton and far-apart lanes, one
  that reads behind a test and one that takes the World; every entity is checked (its own values, its place in
  the order no other has), written in place and apart beside a snapshot.
  `entity_ordered_archetypes_slots_give_each_keys_place` (tide_tests): slots against lookups, a row
  that moves, a destroyed key whose slot a newer entity took, a slot that isn't the key's, `tide_entity_born_at`.
  The indexed loops' differential fixture gained `Swing` (notes/indexed-loops.md). The walks themselves are what
  every game's tests run.
- **Measured**: notes/benchmarks.md, What a row costs in entity order.
- **Remaining** (not built):
  - A row in entity order is now about 11 ns in a query's `foreach` and 12 to 18 in a per-entity system, against
    6 for a system that goes by chunks. What's left is the slot's page (a load through the table's pages for each
    row), a call of the rows function for each run, and that rows come in the order of a hash, so each lane's
    bytes are read at random within the chunk.
  - A system in entity order that changes components copies each lane it can write, for every chunk, before its
    first row and compares it after its last (`gen_canonical_lanes`: prepared pages), whether or not a row wrote
    anything: a few ns a row for a handful of lanes, 250 ns a row in the probe for a system that writes one
    element of a `float[120]` (an array element's write counts as all of the array's lanes).
  - A per-entity view's row is still about 50 ns with nothing in its body: the blend's lookup in each world (two
    now), the correction's lookup in the local state (`tide_attached_find`, for a component with a `[Position]`
    or `[Rotation]` field), and the GUI seed's hash of the entity.
  - `tide_entities_ordered` (every archetype) has no slots: nothing walks it per row.

## A wide component's rows: the lanes a system reads, guards, calls' copies, views' rows when named (2026-11-10)

Found profiling Mount & Rifle toward 144 Hz (its notes/gaps.md, "A fat component costs its whole width"): its
`Campaign.Party` is 744 bytes in 186 lanes, 346 rows in six archetypes. Three costs were reported; the third's cause
was another one than reported. All fixes are in the compiler; no runtime function or layout changed, and results,
row order, hashes and snapshots are what they were.

- **A system's row copied every lane** (`row_lanes_codegen.inc`, `gen_row_copies`). A row gathered the whole
  component from its lanes, called the body and put the written lanes back, so a system that returned on its first
  line for 345 of 346 rows paid 186 loads a row. Three changes:
  - **Lanes by use.** A system's row copies the lanes its code can read (`code_reads`, the views' analysis, now for
    systems) and the ones it can write (`written_lanes`: they're put back whether or not the code wrote them, so
    they're copied even when never read). `system_param_lanes` is that mask, `system_param_splits` when a copy may be
    partial: a plain per-entity system (not a view, handler, extern, async, task-starting, virtual, override, local,
    preparation or sharded one), a component of 2 to 1,024 lanes and at most 4 KiB, not local, with no preparation
    state, that the system doesn't Set through its World. The copies are `memcpy` lines written into the row loop
    for the masked lanes (`gen_gather_lanes`), not a helper taking a mask: the release build's code is cut into
    parts, and a helper in another part would test 186 bits a call. A copy that reads nothing still takes lane 0,
    so the C value is something.
  - **Guards.** Where a system's code starts with `if (...) return;` statements whose tests are plain
    (`guard_plain`: fields of its component and singleton parameters, constants, enum members, `default`, a
    field's list's `count`, and operators on those with no overload; no call, no text, nothing that allocates,
    rolls or writes), those tests are a function of their own, `tide_guard_<System>` (`gen_system_body_variant`
    with `BODY_GUARD`), with the system's parameters. `system_guard_terms` takes the leading statements' tests, an
    `||`'s parts from the left, up to the first that isn't plain, so `if (!a.live || Busy(a)) return;` guards on
    `a.live`. The loop copies the lanes the tests read (`system_guard_lanes`), calls the guard, and makes the rest
    of the copies, runs the body and puts back only for the rows it lets through. Written apart (beside a
    snapshot), a row the guard leaves out still carries the lanes the system changes across: those are copied
    before the guard and put back after (`g->row_apart`). A parameter the tests read nothing of is handed to the
    guard as NULL (clang refuses a never-written variable passed as a const pointer). Not for systems that batch
    a Send or a Destroy.
  - **Lanes handed over.** The row loops and their tasks take only the lanes some row copies
    (`system_comp_lanes`): the others aren't parameters of `tide_rows_<System>_<n>`, aren't looked up in the table,
    and are NULL in `tide_in_`/`tide_col_`/`tide_was_`/`tide_own_`. This matters most for systems that run in
    entity order (not split across threads): their rows come in short runs, and each run looked up and passed
    every lane. The Owner a system's input is found by keeps every lane.
  - **Written lanes of a system that reaches the world** (`writes_own`, in `written_lanes`). A system that Set any
    component through its World, or read there one it changes, wrote every lane of what it takes `mut`
    (`rows_alone`, which is about `restrict`, was also the test for this). Only three things write a component's
    lanes beside the system's own parameter: a Set of that component (`world_sets_component`, still every lane),
    a task (still every lane) and preparation state (the same). A Set of another component, or a read of this
    one through the World (its rows are then written in place, as before), changes nothing of what a row writes.
    Mount & Rifle's Haul, Outfit, Pursue, Travel and StaleContracts read 2 to 95 of Party's lanes and write under
    40, and took all 186 for this.
- **What the read analysis learnt for systems** (`lane_reads_codegen.inc`). Views never write the match, so two
  cases never arose there. A read-only argument holding lists that a call hands over as a copy
  (`gen_routine_call`, where a `mut` argument could change them: `FindPathFor(ground, party, to, party.path)`)
  is read by `tide_copy_<T>` whatever the callee reads: its text and list handles are marked read (`heap_bytes`),
  the rest by what the callee's code reads. (Marked by the types alone, which is at least wherever the call copies.)
  The first build without this crashed the game: garbage list handles.
- **A view paid for a query it never walked**, and for a component with a `[Position]` or `[Rotation]` field
  copied all of it for each row.
  - **Rows when named** (`view_rows_lazy`, `view_rows_from`): a view that runs once a frame (not per entity) is
    handed what its rows are made from (`tide_view_frame`: the worlds and alphas) rather than the rows, and makes
    all its queries' rows before the first top-level statement that names one of its queries. A view that returns
    before that makes none. Which statement that is comes from the code as written (`stmt_query_uses`); the
    generated C of the statements before it is then searched for `tide_q<i>` (`text_names_rows`), and finding one
    is an internal error rather than rows read before they're made. Per-entity views make their rows before the
    first entity, as before. Nothing in a view's body resets the scratch area below the rows.
  - **Some lanes of a corrected component** (`view_lane_mask`): `decl_corrects` no longer refuses partial copies.
    The fields a correction changes are always among the lanes copied (`correction_bytes`), and
    `tide_correct_view_<C>` runs on the partial copy as it did on the whole one.
- **`world.Get` then `world.Set` wasn't the cost** (2 µs a pair for a Party with paths of 300 points). The
  measured 0.1 ms a row was the helper the rows went through: a call to a function that takes the world to Set
  copied every read-only argument holding lists (`call_changes_lists_of`: `takes_world_bindings` meant "copy"),
  so `Haul(party, world, ground, clock)` copied the ground's ten map-sized lists per call. Now a Set through the
  world counts only for the components the callee and what it calls can Set (`world_uses` with `writes`, which
  reach callers already): a copy only when one of those and the argument's type could hold the other. And a `mut`
  argument holding lists copies a read-only one only when the two could be the same lists: types that could hold
  one another, as before, and not places of two different parameters or locals of the calling code
  (`places_apart`, `owner_of_place`: a parameter of the code's own, or a local declared with its value, which
  keeps lists of its own; a query's row, a call's result, an `out` local or a block's parameter is no such
  place). So `FindPathFor(ground, party, to, party.path)` reads `ground` where it is and still copies `party`.
  The spec's sentence (Lists: "unless a `mut` argument of the same call could change it") stands.
- **Tests**: `row_lanes` (e2e, native and WASI): a component of about seventy lanes with text, lists and a
  `[Position]` field in two archetypes with a sparse tag; systems with a guard, a two-part guard on a read-only
  parameter, a lane written on some ticks and never read, a list's count in a guard, a helper handed the world and
  a singleton of lists, a helper whose Set does change what it was handed, the path pattern above, and a sparse
  filter with a guard, and one that reads its own component through its World behind a test only part of which
  is plain. A world written in place and one written apart beside a snapshot agree tick by tick, by hash and by
  value. Views: one that returns before its query (no rows), then goes on (the rows a whole copy gives,
  blended), one handing its query to a helper, and a per-entity one reading beside the corrected field.
  `row_lanes_codegen` pins the guard, the copies' order, the calls' arguments and where the views make their rows.
- **Validation**: the native suite under ASan (3,258 of 3,260; the two that fail are private prototypes' tests,
  `underfoot_tests` and `inside_out_tests`, whose generated C is byte for byte what the compiler before this
  generates, the build's identity apart); on WASI, `row_lanes`, `view_reads`, `wide_lanes`, `sparse_cache` with
  its private cases, `query_borrows`, `world_functions` and the rows' parameter test; the docs site's build.
  `sparse_cache_codegen`'s prefetch pin moved to the first lane its rows copy.
- **Measured**: notes/benchmarks.md, A wide component's rows. Mount & Rifle's tick 2.48 to 1.66 ms and its views
  3.41 to 2.94 ms (p50), on the game with its workarounds still in.
- **Remaining** (not built):
  - A system whose analysis says "all" still copies every lane for the rows its guard lets through: one that Sets
    its own component through the World, starts tasks, or hands the component whole to C, a method or a property.
  - Systems that run in entity order still pay their setup once per run of rows rather than once per chunk; with
    few lanes that's small, with all 186 it's most of what's left of a row.
  - A view's query handed to a helper, or a row's component copied to a local (`var p = row.p;`, which also copies
    its lists for every row), reads all of it. Reading `row.p.field` where it's used is what narrows.
  - Going through a query in match code costs about 70 ns a row in entity order (17 times through 346 `Spot`
    rows: 0.39 ms), which is most of what Mount & Rifle's Roam costs (1.3 ms a tick). Not a wide component's cost,
    and not looked into here.
- **A build for a debugger keeps whole copies and no guard** (`system_rows_plain`: not with
  `codegen_options.split_plumbing`, which `tide`'s debug builds ask for). The debugger test caught it: a system's
  parameter showed the fields its code doesn't read as whatever was in memory (`tidec_debugger_natvis`), and a
  breakpoint on a guarded system's first line would stop only for the rows that get past it. So debug and release
  builds differ here, in speed only; the suites' games are generated the release way. A view's partial copies are in
  both, as they have been since 2026-10-30.

## Sparse cache CI fixture capacities (2026-11-07)

The private sparse-cache differential fixture now derives chunk and task counts
from generated column metadata instead of assuming 2,048-row chunks. Entity128
makes the same 2,081/2,082-row tables span three 1,024-row chunks each. Retained
page storage grows with actual chunks; every fast/slow task still compares page
reference/hash/was metadata, used bytes, whole-world hashes and packed output,
including mixed-reference snapshots, short tails and zero-match selections.
The stale-presence test explicitly verifies a cache reference before forcing COW.
Its apparent COW failure was downstream of the old capacity assertion returning
before cache publication, not a failure to retain the published presence page.
Validation: fresh checkout-generated native optimized ASan private cases 7/7,
public cases 3/3 and the codegen ownership check pass. WASI's public/private/
codegen CTest targets pass 3/3 with the existing external-project configuration.
The raw Entity hostile-input fixture also avoids typed casts into its packed
28-byte-header payload: native slots require eight-byte alignment, so malformed
slot edits use aligned locals plus memcpy. The duplicate/free-list rejection
assertions are retained; the runtime already byte-copies into aligned pages.
Fresh Windows ASan entity tests pass 19/19; Linux CI supplies alignment UBSan.

## Logical entity identities and private slots (2026-11-06)

Owner-approved in response to mount-and-rifle's projected client divergence:
Entity is now the opaque 16-byte `{uint64_t birth, lineage}`. `birth` is the
public 64-bit simulation identity clock (starting at 1), not Time's wrapping
32-bit tick; `lineage` is the generated causal hash. Both zero is null. Generated
match spawning supplies the complete permanent key before Spawn returns; physical
slot reuse, hidden allocations and row compaction cannot choose it. The compiler
records the causal-key construction and canonical iteration contract in
language.md and schedule-and-threads.md. This intentionally changes Entity's C
ABI, packed worlds, seeds and world hashes; session protocol is 25.

`entity.c` separates that key from dense private slots. A full-key open-addressed
map resolves locations, and slot enumeration remains physical for implementation
walks. Lookup capacity follows live population: tombstone pressure rehashes at the
same capacity rather than growing with lifetime spawn/despawn count.
`tide_entities_ordered` caches sorted ALIVE logical keys (including pending
spawns). Short per-table atomic locking serializes lazy publication against
structural registration/destruction; queries borrow immutable arrays. A changed
keyset retires its prior array until `tide_entities_settle`, keeping an outer
query valid when it spawns, destroys or triggers a nested query. Location, snap-
count and presentation writes preserve the cache. Snapshots retain no cache
pointer and build their own on demand. `tide_entities_ordered_archetypes` owns
ascending unique archetype-ID sets and caches only their ALIVE keys; repeated
nested queries therefore traverse matching archetypes rather than every entity
in the world. Actual archetype changes invalidate these subsets, row-only moves
preserve them, and the global keyset cache survives both. Sparse presence remains
checked live by generated traversal. Subset arrays have the same retirement and
snapshot ownership rules; caller/code-reload pointers are never retained. Cache
pointers/locks and capacities never enter hashes or packed bytes. Native ASan tests
exercise four real Windows reader threads against 500 structural changes, retained nested-query arrays, snapshot
isolation and unchanged hashes. Map pages are COW snapshots; grown descriptors
stay until systems settle, so concurrent lookup does not follow freed index
storage. Capacity and addresses are derived metadata, excluded from hashing and
packing. The key never contains a slot, generation, worker number or hidden
allocation counter. Unkeyed trusted-C and LocalEntity allocation uses the separate
`{0, monotonic_counter}` namespace; it is not an interface for visibility-stable
match spawning.

The current-clock key registry includes destroyed spawns. A repeated full key,
backwards clock, zero match clock or exhausted identity namespace ends with an
explicit diagnostic; the allocator never probes for a different ID based on
occupancy. The registry is copied and serialized in sorted-key order with private
allocator state. Recipient projections import only permitted existing IDs with
`install`, reconstruct private dense storage and do not transmit hidden allocator
or collision-registry entries. The public world identity clock is transferred by
the generated canonical codec. Shard away markers retain the exact key while using
their own private slots; release takes the full key.

Full-key equality and lexicographic ordering are shared inline helpers; entity
hashing feeds all sixteen little-endian bytes. Random field seeds use that full
key. Event deduplication uses the permanent target key, never the older 32-bit
presentation `born` sidecar. Text prints both words as fixed hexadecimal values.
The old presentation sidecar remains outside match hashing/wire state.

Focused Windows ASan validation: 35 entity/table tests, 27 text/random/events
tests and 9 page/rows tests pass. New cases give server/client different hidden
allocation and recycled-slot histories, then verify identical public spawn IDs;
exercise high 64-bit key halves, COW snapshots and raw/delta reconstruction; and
reject every truncated keyed table and duplicate imported storage without changing
the destination. The complete WASI engine unit binary passed 679/680 initially;
the sole failure was the intentional `crossplatform_random` pin after full-key
field seeding. Native ASan independently produced the same new 0xEA0690516561A349
value, then that expected pin was updated. The 36 focused protocol25
command/session/ visibility/view-time native ASan tests also passed. Broader
generated-game validation accompanies the coordinated compiler ABI change. Earlier
index/generation and sparse global-slot entries below describe the superseded
representation.

The full-world delta now carries the current-clock collision registry as an
exact set edit against same-clock XOR baselines, or as separately negotiated
1024-key (16KB) hash/need regions. Full literal serialization remains bounded by
remaining input bytes. This avoids retransmitting all 20,000 seeded Stone keys
when the joiner already regenerated them. Native ASan and WASI both send 1,089
bytes in random_net's seeded-join regression (previously 320,943), preserving its
existing 16KB bound. Two new tests cover large unchanged registries, prefix
insertion and same-clock replacement, truncated transfers, cross-region repeated
or backwards keys and wrong clocks, preserving the destination on rejection.
The raw registry representation, logical hashes and projected privacy are unchanged.

The sparse rows/masks/cache and function-acts fixtures derive expected traversal
from an independent lexicographic full-key sort, retaining their ordinal-based
filter/value models and the cache fixture's physical-hole model. Initializer ordinal
and private row order are no longer traversal or event-delivery order oracles.
The refreshed WASI entity filter passes 31/31 including subset membership,
borrowed lifetime and snapshot tests; sparse rows/masks/cache, function-acts,
destroy-rows and stable-async pass 6/6. The existing 4096-Work/two-Fighter query
fixture now finishes in 1.63 seconds total (355.6 ms for its 16-tick match case),
where the first full-suite run exceeded its unchanged 120-second timeout while
walking every world key per inner query. Remaining query assertions/hash pins
are updated separately against canonical order and cross-platform results.
The browser's full threaded240-tick fixture initially exceeded its unchanged
45-second bound: profiling showed canonical system wrappers scanning every world
key, including thousands of irrelevant entities, rather than a worker failure.
Using the same admitted-archetype cache for systems and preparing their component
lanes once per chunk restores the fixture to 19.61 seconds on this machine. All
240 serial/pooled tick hashes, background-worker completion and sleeping-worker
assertions remain enabled. Temporary profiling was removed; failures now print
existing timing/progress counters to distinguish stalled work from CPU cost.

The final rows fixture derives chunk capacity from generated column metadata,
checks its original COW lane contracts across every actual chunk, and validates
Cargo's frozen/unfrozen value changes on every row. Extern-system batching
expectations come from an independent full-key sort and contiguous physical-run
model; Badge lane offsets come from offsetof/sizeof. Big-world snapshot lookups
use the snapshot's own logical key rather than a possibly destroyed current-world
slot. All three fixtures pass native ASan and WASI. Fresh canonical row/query,
World.Set/read/helper, destroy-rows and seeded-network tests pass WASI (10/10).

## Loop views: lists indexed through a view found before the loop (2026-11-02)

The owner, of the 14x gap between a mesh's normals over a world list and the same loop over C arrays: "im ok with redesigning things, if it doesnt touch language and is a net position = do it". No language or ordering change; generated code and `tide/list.h` only.

What the gap was, measured on a 256 x 256 plane (66,049 64-byte vertices in 260 chunks, 393,216 indices), release flags, the Geometry package's `RecalculateNormals` and the same loop written in Tide, best of 25 ticks: 6.1 ms against 0.45 ms for C over arrays. Refilling the run cache was a small part of it (a cache that remembered two runs cut refills from 264k to 2k and saved nothing); the cost was every element checking that its cache still holds (the list's handle, the run, the count behind the list's block, the heap's `moves` behind the heap, all reloaded from `tide_lc` on the stack because a miss hands the cache to a function), and `count` decoding the list's handle in every loop test.

- **Caches by index** (`cache_slot` in `compiler/src/codegen.c`, `index_key`): a list's cache is now its place's key and the index's variable (`vertices[a]`, `vertices[b]`, with `t + 1` and `t - 2` as `t`), so indexes that move through a list apart keep a run each. Any other index shares the list's one cache, as before. 6.1 to 4.2 ms.
- **Loop views** (`tide_list_loop`, `tide_list_loop_find`, `tide_list_loop_at`, `tide_list_loop_at_mut` in `tide/list.h`; `tide_list_loop_chunk` and `tide_list_loop_own` in `engine/src/list.c`): before a `for` or `while` loop, generated code finds each list the loop indexes and keeps the shape of (`open_loop_views`): its count, and where its elements are, side by side (`flat`; the scratch area's, an asset's), or its chunks' directory. An element is then a bounds check against that count and, in chunks, a look in a table of `TIDE_LIST_LOOP_WAYS` (16) chunks by their numbers, filled the first time one of a chunk's elements is read (its page as it is) or written (`tide_heap_write`, the world's own, as `tide_list_at_mut` makes it). A chunk read and then written is found again as the world's own, which later reads of it read; a list side by side is made the world's own at its first write. The view's helpers (`tide_list%d_lget`, `_lat`, `_lslot`, `_lset`) take the same arguments as the cached ones, so each access site picks one by name (`loop_view_of`), and `count` in the loop is the view's. A loop inside one with a view goes on with it.
- **What keeps a list's shape** (`lv_stmt`, `lv_expr`, `lv_keeps`): nothing in the loop's condition, step and body names the list, or a place that holds it, other than to index it or read its `count`: no method of it (`Add`, `Clear`, `Resize`, ...), no assignment to it or to what holds it, no argument naming it or what holds it (it could be a `mut` one), no method called on what holds it, and no local of its root's name. A list's element as an argument is a copy, read. And no view at all in a loop with a method call on the code's own value, an Action, a lambda, a World or query, an await, a `foreach`, a `switch`, or anything else the walk doesn't know; nor in inlined code, tasks, parallel steps or a Sort's comparison. Elements are plain values (`loop_viewed`: no text, lists, maps or sets in them), written as bytes.
- **Why it holds:** code reaches a list only through its place (functions see their parameters), so a list no code in the loop hands on, resizes or reassigns keeps its count, block and chunks for the whole loop. A page made the world's own moves the heap and leaves the old page to the snapshot that shares it, which is still read rightly; nothing in the loop reads the list through it once its view has the world's own. A loop that writes a list prepares it first (`tide_list_prepare`), so an asset's or the pack's elements are made its own before the view is found, as a slot's write would; a list a loop only reads is never prepared or made the world's own.
- **Measured** (same plane, best of 25): `RecalculateNormals` 6.1 to 1.8 ms (4x C, from 13.5x); `RecalculateTangents` 37 to 22; `WeldPositions` 18 to 16; appending shapes (`Plane`, 4,096 boxes) unchanged, as `Add` makes a loop keep its cache.
- **Tests:** `tidec_e2e_loop_views` (`compiler/tests/e2e/loop_views.tide`): scattered reads and writes of a list in chunks with two indexes, indexes past both ends, a list side by side written whole, nested loops sharing a view, a loop that adds (cache kept), a loop that hands the list to a function that writes it through its own view, a function's scratch lists, an asset's list read and a copy of it written while the asset stays as it was; and a snapshot taken before a tick keeping its chunks when the tick's loops write them, with rolling back to it giving the same world. The full native and web suites cover every game's loops with the change.
- **A parallel step's caches are counted in its own text** (`close_list_caches` at the end of each step in `gen_par_steps`): a step's `tide_lc` was sized only when the next function declared its caches, after the step's text had been copied out, so it stayed 1. Harmless while a step indexed one list through one cache; caches by index gave `tide_e2e_parallel_work`'s step two, and ASan caught the overflow.
- **Not done:** `Add` per element (shape building) is still one runtime call each; a `foreach` over a list, query loops and parallel loops have their own paths and get no views.

## 64-bit handles: built (2026-10-30)

As designed below; what differs from the design or follows from it, and what
holds it:

- **Lists stop at their count, not at 1 GiB.** A list's elements were capped at
  1 GiB of bytes, the offsets' reach; now a list grows to INT32_MAX elements
  (its count is an `int`) as memory allows, so `list.Resize(int.MaxValue)` on
  a list of ints asks for 8 GiB and, where that isn't there, ends the program
  as running out of memory does, where it was left as it was. The `lists`
  safety fixture's sizes line no longer tries it. A block side by side in a
  world's heap is still 128 MiB at most; one in the scratch area is as big as
  memory (`tide_scratch_block` takes a `uint64_t`).
- **Heap.** `LIMIT` is 2^46 (64 TiB, the 32-bit page indices' reach); received
  heaps are bounded by their bytes (notes/memory-safety.md, 64-bit handles).
- **Scratch area.** The slot table grows by doubling from 64 slots; the pack
  is read past `TIDE_PACK_BASE` from `tide_pack_memory`.
- **Generated C.** `type_layout` gives text, lists and grids {8, 8}, maps and
  sets {32, 8}, raw meshes {16, 8}; marks are `uint64_t` (`tide_mark`, the
  `const uint64_t tide_mark` parameter of row tasks); the few emitted locals
  that held a handle or a heap offset (`tide_blend_grid_snapshot`,
  `tide_worker_grid_capture`, retargeting assets' `base`) are `tide_handle`
  or `uint64_t`; the mesh-glob metadata row (`tide_ref_mesh_entry`) lost the
  pad it had after a 4-byte key. Every generated game was checked with clang's
  `-Wshorten-64-to-32` for narrowing from a 64-bit value, as were the engine,
  compiler and platform sources (all of which had none before).
- **C that mirrors layouts.** Box2D's, Box3D's and Newton's bridges
  (notes/box2d.md, box3d.md, newton.md). Box3D's live-shape "source" skipped
  its list handles as 4 bytes each (`(const char *)s + 4`), so with 8 the
  material bytes it compares moved under a hull's `offset`:
  `box3d_invalid_live_geometry_disables_and_can_be_repaired` caught it; it
  skips `sizeof(tide_list)` now.
- **Unity.** The Sample's `com.tide.game/schema.json` and the test fixture
  `compiler/cli/tests/unity_rules_physics/schema.json` came from `tide unity`
  runs of the old layout. They were laid out again by a script that applies C's
  rules to each field (text, lists and grids 8, maps 32, the rest as they were;
  defaults moved with their fields), checked first against schemas the old and
  the new tidec give the same games (platform/tests/embed.tide and the
  `nested` e2e game: identical but for the hash). Their `hash` is the old one,
  so the Sample's package is written again at its first real build. The
  Sample's `TideGame.cs` is what `unity_code.c` writes from it.
- **Tests that changed with the formats** (a helper agent converted most of
  the C tests by hand-checked rules): the pack tests (`tests/test_pack.c`,
  `pack_fingerprint_test.c`: 64-bit builder offsets, forged `link`s,
  `tide_pack_region_size`, 24-byte trailers), the list limits
  (`tests/test_list.c`), lane numbers of a component with text and a list
  (`rows_test.c`: a handle's high word stays the same zero, so only its low
  lane is written apart), the embed packet's map keys (`embed.c`), an empty
  pack as C (48 bytes: a 40-byte header to 16), mesh value swaps
  (`mesh_values_test.c`, 16-byte elements), grid textures' handles
  (`textures_test.c`), and `uint32_t` copies of handles in Earth's and the
  assets' tests.
- **Pins that moved** (world, page or physics-image hashes, and one size),
  each new value the same natively (Windows ASan) and on the web (WASI and
  browsers), as the shared pins say: `compiler/tests/CMakeLists.txt`'s
  `NESTED_HASH`, `ARRAYS_HASH`, `FBX_HASH`, `SKINNED_HASH`, `CHARS_HASH`,
  `JSON_HASH`, `OPTIONAL_FIELDS_HASH`; `assets_same.h`'s
  `ASSETS_EMPTY_WORLD_HASH`; `DECODE_WORLD_HASH`; the `rows`, `rolls`,
  `crossplatform_text` and `retargeting_dynamic` worlds; Sumo's and Towers'
  match and replay hashes; Box2D's and Box3D's world, sequence and image
  hashes (stack, sleep, course, kit, complete, joints, mesh); Newton's robot
  and microduck worlds; `bench_sand`, `bench_gallery` and `bench_earth`'s
  `--expect`s; an empty pack as C, 32 bytes to 48. The image hashes moved as
  the packages' C state holds records of Tide types with lists. Pins of games
  that hold no text, lists, maps or grids in the match didn't move (the
  crossplatform math and randomness pins among them), and nor did the asset
  fingerprints (`tidec_pack_fingerprint`).
- **Validation**: native `sanitize` 2,910 tests and web 2,798, all passing;
  CI runs the other platforms.
- **Measured** (notes/benchmarks.md, 64-bit handles, before and after): the
  versus programs, views, Sand and Gallery within -3% to +3% of before in
  release; worlds a few hundred bytes a megabyte bigger in memory, and the
  same on the wire.
- **Not done**: the world as bytes (`tide_world_pack`, `tide_reader`) still
  counts in `uint32_t`, so a world packed whole stays under 4 GiB, a limit of
  the serialization rather than of handles; a pack's file entry and a text are
  still at most 4 GiB each (`tide_pack_bytes` takes a `uint32_t`). Neither was
  asked for.

## 64-bit handles: design (2026-10-30)

The owner's decision ("Full 64-bit handles"), knowing it changes every layout
holding text, lists, maps, sets or grids, the world's bytes and hash, and the
generated C. Why: a handle was one `uint32_t`, two bits saying where its block
is and 30 its offset, so a world's heap and a thread's scratch area stopped at
1 GiB and the asset pack, mapped into the scratch area's offsets from
`TIDE_PACK_BASE` (2^29), at 512 MiB, which bannerlord-duel's textures passed
(`past_handles` in `engine/src/pack.c`). Every tag value is taken, so there was
no cheap widening.

**Layout.** A handle is `tide_handle`, a `uint64_t`: where (`TIDE_IN_MATCH` 0,
`TIDE_IN_LOCAL` 1, `TIDE_IN_SCRATCH` 2, `TIDE_IN_VERIFIED` 3, as before) in the
top two bits (`TIDE_WHERE_SHIFT` 62), the offset in the low 62. Everything
reads and makes them through `tide_handle_where`, `tide_handle_offset` and
`tide_handle_of` (`tide/text.h`). The tag stays on top, rather than in low
bits that alignment frees, so a match handle (tag 0) below 4 GiB has a zero
high word: what deltas and hashes of handle columns see is mostly zeros, as
before. Considered and not taken: keeping 32 bits and counting offsets in
16-byte units, which gives 16 GiB a heap, another limit of handle bits (and
the scratch area's text is only 4-aligned).

- **Heap** (`tide/heap.h`): offsets, `used`, `free[]` and `pending` are 64 bits.
  Page indices (`pages`, `room`, `tide_page.first`) stay 32 bits, so it stops
  at 2^32 places of 16 KiB, 64 TiB (`LIMIT`), which memory reaches first.
  A block's header stays 16 bytes: `link` (its size class in the low 8 bits,
  above them the next block of a free or pending list, or a pack block's
  trailer distance, which `next` held), then `a` and `b` as they were,
  through `tide_block_class`, `tide_block_next` and `tide_block_link`.
- **Lists**: a chunked list's directory holds 8-byte chunk offsets (was 4).
- **Grids**: a chunk's record is `{int32_t chunk[3]; uint32_t zero; uint64_t
  block}`, 24 bytes (was 16). A grid's `Texture` keeps the handle's low word in
  `image.version`, where it was, and the high word in `image.width`, which a
  grid's texture didn't use.
- **Maps and sets**: `tide_map` is 32 bytes (two handles, live count, zero
  padding, last order; was 24). Entries hold 8-byte handles for text keys and
  values that hold text or lists.
- **Scratch area** (`tide/text.h`): offsets and marks are 64 bits. A thread's
  slot table grows as its pieces need (a thread-local pointer and its room)
  instead of 1,024 fixed slots, so it holds what memory holds. Blocks are at
  least 8-aligned (their header has a `uint64_t`).
- **Pack** (`tide/pack.h`): the region is at `TIDE_PACK_BASE` = 2^61 in the
  scratch area's offsets. `tide_scratch_at` tests for it (one predictable
  branch) and reads it from its own base, a thread-local copy `tide_pack_use`
  sets, where the slots mapped it before. Format version 3: the header's
  region place and size, entries' offset and size and the builder's sizes are
  64 bits; handles in the region are 8 bytes, as in the game's structs. The
  builder's and the loader's limits are memory (and wasm32's 4 GiB).

**Memory.** A text, list or grid field is 8 bytes, 8-aligned (was 4): two of a
component's 4-byte lanes, and up to 4 bytes of padding where it follows a
4-byte field. A map or set field is 32 bytes (was 24), a `Mesh` 16 (was 8), a
`SkinnedMesh`'s lists 24 (was 12). Heap blocks cost what they did; a chunked
list 4 bytes more a 16 KiB chunk, a grid 8 more a chunk.

**Wire and hash.** No hash function changes, but page bytes do, so every
pinned world hash moves, and `PROTOCOL` goes from 20 to 21. Raw world bytes
(saves, a join with no base) grow by 4 bytes a handle a row and the heap's
numbers go as `uint64_t`; deltas send handle columns XOR'd and as runs of
zeros, and the heap's numbers as varints, so small handles stay small. The
asset dictionary's descriptor locations and pack offsets are 64 bits (its
`UINT32_MAX` "same as the base" mark becomes `UINT64_MAX`). Commands and
inputs carry text and list bytes, never handles, and recipient (visibility)
state encodes values, not offsets: neither changes but by struct sizes.
Generated headers say `Heap ABI: ... 64-bit handles (1)`; the Unity embed ABI goes up (8), as
its C# mirrors change (`TideText`, `TideList`, `TideGrid` hold a `ulong`,
`TideMap` is 32 bytes).

**Phases.** (1) The helpers above, with `tide_handle` still 32 bits, through
the engine, generated C, packages and tests: no byte changes, every pin
stays. (2) The widening: `tide_handle` 64 bits and everything above, the
compiler's layouts (`type_layout`), generated marks and handle locals, debug
views, migration, the Unity mirrors, packages, docs; pins and `PROTOCOL` move,
native and web agreeing on each.

**What C changes.** `.at` is a `tide_handle` (`uint64_t`); a scratch mark
(`tide_scratch_mark`) is a `uint64_t`, which a `uint32_t` would cut; functions
that take or give a block's offset (`tide_heap_alloc`, `tide_heap_release`,
`tide_heap_block`, `tide_scratch_block`'s handle, `tide_grid_record.block`,
the pack builder's offsets) take or give `uint64_t`; `tide_block`'s
`size_class` and `next` are `link`, read and made through its helpers. Code
that only calls `tide_text_*`, `tide_list_*` and the rest is unchanged.

## Views copy and blend only the lanes they read; helpers read other singletons' lists uncopied (2026-10-30)

Two costs bannerlord-duel's tools/army (20,000 fighters) found in views that
run once per entity, both in the generated C:

- **A read-only list argument copied for any `mut` list beside it**
  (`gen_routine_call`, `call_changes_lists_of`). A call copied every read-only
  argument holding lists (the receiver too) whenever any `mut` argument held
  a list, whatever its type: `KeepFar(mut FarCrowd crowd, FarSkins skins, ...)`
  copied all of `FarSkins` (four lists, one of 25,905 matrices, 1.7 MB) into
  the scratch area for each fighter, about 30 µs a call, 337 ms a frame in the
  army. Now it copies only when a Set through the world could change it, or a
  `mut` argument or `mut` receiver holds lists and one of the two types can
  hold the other (`value_contains_type` either way, as `indirect_value`
  already asked): lists of types neither holds are never the same lists. The
  spec already said so (docs/spec.md, Lists: "unless a `mut` argument of the
  same call could change it"); the code was coarser.
- **A view copying and blending the whole component for each entity**
  (`lane_reads_codegen.inc`). A per-entity view gathered every lane of each
  component it takes, found the entity in last tick's world, gathered all of
  it there too (`tide_table_get` a lane) and blended every field; a view's
  query row did the same for every row. For a 75-lane component that read one
  field, 7 times the cost of a small one. The read analysis mirrors
  `code_writes`: the bytes of a parameter, or of a query row's binding (`row.w`
  of `foreach (var row in all)`), the code can read, narrowed through fields
  and swizzles and through Tide helpers it hands a place of it to
  (`code_reads`, memoized per function and parameter). The value whole, an
  element's container, a property, a method's receiver, C, a built-in, a
  virtual or override, an async callee, a call back into a function being
  worked out, the row variable whole, and a query named anywhere but as a
  foreach's value read all of it. A local field (`f.guards.x`) reads none of
  the lanes. Hidden fields (snap counts) are always in the mask, as blending
  compares them. A view then calls `tide_gather_some_<C>`,
  `tide_prev_some_<C>` (`tide_load_some_<C>`) and `tide_blend_some_<C>` with
  a constant word mask; they're `always_inline`, so clang folds the mask and
  keeps only the lanes read (without it, a call tested 75 bits and gained
  only half). The lanes not read are left uninitialized in the copy, which
  nothing reads. Generated only for components a view reads some of
  (`reads_some_used`).
  - Not partial: components a `frag` or `vert` takes (a draw sends them from
    the view's copy, which no expression in the view names; `instancing` and
    `shaders` caught it), `verified` parameters and joins, components with corrections
    (`decl_corrects`; partial too since 2026-11-10, the corrected fields always
    among the lanes: see the top section), an `Interpolate`, blending that reads the heap
    (`blend_reads_heap`), sharded games, `mut` local components (put back
    whole), views that start tasks, and components past 1,024 lanes or 4 KiB.
  - The duel's `DrawFighter` reads 183 of `Fighter`'s 308 bytes (its pose
    helpers read most fields); before the local-field rule it was all 308.
- **Correction (nightly.62)**: the analysis knew a parameter by its address,
  and `bind_views` (codegen) adds a `tide_bound<n>` parameter to a view for
  what its frags and verts read that it doesn't take, after its code was
  checked. When that grew the view's parameters past their room (eight to
  nine), they moved, and the code's names still pointed at the copy from
  before (the arena keeps it): nothing matched, the view seemed to read
  nothing of its component, and the lanes it read were never copied.
  bannerlord-duel's campaign `Figures` (eight parameters, frags reading
  singletons) read `party.doing` and `party.at` as garbage and drew no
  figures. The analysis now knows a parameter by name and type too
  (`same_param`; a wrong match can only add reads). `view_reads`' `Figures`
  is that shape and fails without it.
- **Tests**: `view_reads` (a component of about twenty lanes: fields, a struct's field, a
  helper handed it whole, a field that jumped alone, `[Snap]`, a component that
  jumped whole, query rows, a `mut` view with local fields read through a
  member, and frames with no tick before; read-only lists beside a `mut` list
  of another singleton, and beside one their own singleton holds, which still
  reads the list as the call began) and `view_reads_codegen` (`ReadSome`,
  `Marked` and the `Rows` query gather and blend some; `ReadAll` all; `Keep`'s
  read-only singleton isn't copied, `Count`'s is). Measured with
  `bench_views`: notes/benchmarks.md, Views over wide components.

## Wide components' lanes, an element's fields, and text passed to helpers (2026-10-30)

Found measuring a Tide game of 20,000 sword fighters (bannerlord-duel's
tools/army): its `Fighter` is 77 lanes and holds text (its gear's names).

- **Masks past 64 lanes** (`lane_writes_codegen.inc`): `lane_mask` is 16
  words, so components of up to 1,024 lanes put back only the lanes a system
  writes (`LANE_MASK_MOST`; wider ones keep every lane). Up to 64 the generated
  C is what it was; wider, `tide_scatter_some_<C>` takes the words as an array
  (`const uint64_t *some`), passed as a compound literal, and the cached-chunk
  shortcut tests `mask[lane >> 6] >> (lane & 63)` (`lane_mask_c`,
  `lane_test_c`).
- **An element's field** (bug, live for every component on `dev` before this):
  `writes_place_of` took `s.pair[1].b` for `s.pair[0].b`, the element being all
  of its array but the field after it narrowing to its offset in the first
  element. Beside a kept snapshot, the write to the second element was lost.
  A place through an index is now `element`, which a field can't narrow.
  Lifting the 64-lane cap exposed it: `optional_fields`' `Holder` (82 lanes)
  changed its pinned hash.
- **Text passed whole to a helper** (`check.c`, `heap_pass`,
  `settle_heap_passes`): a system's component holding text, passed whole to a
  Tide function's `mut` or `out` parameter, made the system `writes_text`
  whatever the callee did: no threads, and ordered behind every system that
  changes the heap (the fighters' `Fight` was in stage 38 of 44, behind
  Box3D's). Now a function's parameter records whether its code can change
  text, lists or grids through it (`param.heap_written`: a write to text, a
  list, a grid or an element, the whole of one holding them, or passing it on
  to a callee whose parameter does, settled to a fixpoint), and the system
  changes the heap only if the callee's parameter does. Extern, async, virtual
  and override callees, methods and anything else count as before.
- **Tests**: `wide_lanes` (an element's field beside a kept snapshot, a
  72-lane component with text written apart, its snapshot as the tick before)
  and `wide_lanes_codegen` (the word mask; `Step`, which hands its component
  to helpers that change no text, doesn't flush the heap; `Name`, which does,
  does). The element case fails without its fix. Full native suite (2,906)
  passes; web not run locally.
- **Measured** (tools/army, 20,000 fighters, release, same runtime): a tick
  13.4 ms to 11.0; `Fight` 2.3 ms to 0.6, now in stage 2 beside Box3D.

## Lanes a system can't write stay where they are (2026-10-29)

Following the owner's thread-scaling question: Tide's dense and `[Lasting]`
loops were already at memory bandwidth (one thread moves 36 MB in 0.42 ms at
1M rows; hand-written C and Bevy's table storage stop scaling at the same wall),
so the lever was bytes. On a client, a system writing a snapshot-shared chunk
apart took a page for every lane of each `mut` component, copied the lanes its
body never writes (clang made them `memcpy` calls), compared all of them at the
end and handed back the unchanged ones.

- **The analysis** (`compiler/src/lane_writes_codegen.inc`): for a system's
  single `mut` parameter of a component, the bytes its body can write:
  assignments and steps to it or its fields (by field offset; a swizzle by its
  first and last component), places passed to `mut` parameters (through the
  callee's own code, memoized per function and parameter; C, built-ins and a
  call back into a function being worked out write all of the place), `out`
  arguments and GUI `arg_mut`, any method called on a place of it, and
  everything inside blocks, lambdas and steps. A property, an element, a local
  part or anything it can't place writes all of what it's in; a field `Snap()`
  writes the whole component (its hidden counter). Views, systems that don't
  run alone (until 2026-11-10, when `writes_own` took its place and only tasks
  and preparation state count: see the top section; `rows_alone`: World writes, World reads of what they change,
  tasks), `World.Set` of the component, two `mut` parameters of one component
  and components of more than 64 lanes keep every lane. `written_lanes` turns
  bytes into a lane mask.
- **Codegen**: `tide_scatter_some_<Component>` puts back only the masked lanes
  (`gen_row_puts`, used by row functions, `gen_chunk` and handlers, all of which
  begin unwritten lanes as copies or leave them in place). `gen_system_chunk`
  and the pieces' own/done begin, check, piece and end only runs of written
  lanes (`written_runs`); unwritten lanes read and "write" pointers are the
  column where it is (`gen_unwritten_lanes`), never stored to. The cached-chunk
  shortcut checks and marks only written lanes, as the ordinary task begins
  them; a system writing all lanes generates the C it did. Unwritten pages stay
  shared with their hashes kept.
- **Tests**: the full native and web suites; `field_snap` caught the Snap
  counter and `sparse_cache_private` the shortcut's page-state parity, both
  fixed before merging; `sparse_cache_codegen` pins the masked shortcut.
  Measurements: notes/benchmarks.md, Lanes a system can't write.

## Holes: rows entities left stay where they were (2026-10-29)

The owner's request after the `[Lasting]` client measurements: removing a row
no longer fills it from the end. Filling writes a row of every column of the
hole's chunk, which under a kept snapshot copies all of that chunk's pages
(about 13 cold 8 KiB pages for the sleep benchmark's bodies), then rehashes
them. That made a moving tag cost about 20 us per toggle per tick on a client.

- **Contract** (`tide/table.h`, `engine/src/table.c`): `tide_table_remove` on an
  archetype's table writes the null entity into the row's entity cell and
  leaves the rest as it was: a hole. The last row is dropped instead, with any
  holes before it. `tide_table_add` fills the lowest hole of the last chunk
  (zeroed), which an append would write anyway, and otherwise appends: filling
  a hole further in would copy that chunk under a snapshot. At the end of each
  tick's changes, generated `apply_commands` calls `tide_table_settle` per
  archetype, which fills holes from the end, lowest first, once they're more
  than a 32nd of a table's rows (1/8 and 1/16 left `versus_churn` fragmented,
  1/64 settled too often; filling the lowest hole on arrival copied a cold
  chunk per arrival and kept only a tenth of the gain). Where holes are
  depends on the world's changes alone, never on page sharing, so server,
  predicting client and resimulation agree. `holes`, `hole_from` (no hole below
  it) and `chunk_holes` follow from the rows: generated unpack and delta unpack
  call `tide_table_count_holes` for archetype tables; snapshots copy them.
  Tasks' and attached tables never get holes (`tide_table_remove_row` is as it
  was). Raw saves grow by the zeroed new header fields (16 bytes a table on
  64-bit hosts, 12 on WASM); packing zeroes table headers, so no address leaves.
- **Loops**: system tasks (`gen_system_chunk`), the threaded own/done steps and
  C systems call their row functions once per run of entities
  (`tide_table_run`); a whole chunk (`tide_table_whole`) is one run with no
  scan. A chunk with holes is never written apart: its changed lanes are copied
  first (`tide_table_lanes_begin` with `apart` false), so hole bytes stay and
  page hashes stay deterministic. Sparse pass counts are taken per run there,
  and retained sparse selections and the cached-chunk task shortcut aren't
  used for it (`gen_rows_cache_lookup` gives NULL). Every other generated row
  loop skips holes by their entity (`tide_row_gone`): views and batched sends
  (`gen_chunk`), queries and query fills, heard rows, availability, client state
  and recipient hashes, shards, unloads (which also re-check the count, as a
  destroy can drop trailing holes), local-field and resource walks, task traces
  and the Unity embed. `tide_world_entity_count` counts `tide_table_live`.
  Hosts get `TIDE_ROW_GONE(world, arch, row)` (docs/c/hosts.md); the versus
  harnesses use it. Unpack's `placed` check skips holes. Hot-reload migration
  carries no hole (`platform/src/migrate.c`).
- **Order**: rows of a table now come in the order holes and appends give them,
  deterministic everywhere but not swap-remove's. Games whose results depend on
  entity order (a shared random stream drawn per entity, `safety_churn`) get
  other, equally deterministic values: pins moved for crossplatform,
  towers, sumo and safety_churn; local_fields/local_events logical, whole and
  delta hashes didn't move, only their raw save sizes. `PROTOCOL` is 20.
- **Tests**: `tests/test_table.c` (holes, runs, lowest fill, trailing drop,
  settle, recount); `destroy_rows` and `sparse_cache` models now model holes and
  match exactly; `lasting`, `big_world` (pack/unpack with holes), `shards`
  (checkpoint recovery) and every existing suite pass native and WASI.
- **Gaps**: a hole keeps its old component bytes (released text and list handles
  included) until filled; nothing reads them. Tables with many holes run more,
  shorter row calls until settled. Measurements: notes/benchmarks.md.

## Raw-save pins after the derived cache slot (2026-10-29)

CI run 40787272828 agrees across platforms on unchanged local-fields logical
world/whole-transfer/delta hashes, but its two equivalence fixtures still pin
raw saves from before the zero cache pointer was appended. Only those raw pins
change: 391 to 399 bytes on 64-bit hosts, 363 to 367 on WASI. Removing exactly
the verified-zero pointer slot recovers both prior raw byte hashes in both
fixtures. A direct zero-slot assertion guards against serializing cache ownership.
Both fixture CTests pass optimized native/WASI; no simulation or transfer value
changes. Detailed hashes and CI scope: releases-and-ci.md, Raw-save pins and
journal isolation in run 40787272828.

## Pure row updates before conditional destruction (2026-10-28)

Internal compiler optimization, preserving component bytes, entity IDs, deferred
order, snapshots and hashes: a narrowly eligible per-entity system whose plain
row-local arithmetic ends in `if (predicate) this.Destroy()` can compute its
updated row and predicate without touching the command queue. Its ordinary
generated body remains for fallback paths; a private bool-returning update
variant supplies the eligible rows loop. Each bounded block of at most 128 rows
keeps its predicates in a C-stack bool array, writes components back through the
existing in-place/apart machinery, then records the flagged destroys in original
ascending row order before the next block. No allocation, runtime API, stored
pointer, world layout or scheduling rule changes. This separates ordinary SIMD
row arithmetic from the conditional queue writes that previously kept it scalar.

The eligibility proof accepts only numeric/plain local declarations and
assignments to own mutable component fields or locals, followed by that one
conditional self-destroy with no else. Expressions allow primitive arithmetic,
plain field/local/constant reads and numeric constructors. User functions,
overloaded operators, properties, indexing, heap/resource values, C calls, random,
tasks, World/query/input/local-field access and mutable singletons fall back.
Sharded, local and filtered archetype paths also retain ordinary recording.
No earlier row can reach a later row's state through an eligible body; the only
queue effect is its final self-destroy, whose relative order is unchanged.

For non-hierarchy/non-sharded destruction, private generated `destroy_at` takes
the generation-checked location already found by `apply_destroy` while deciding
whether the target unloads a scene. The existing `destroy_now` helper still looks
up a fresh location for other callers. Reuse happens before any lifecycle handler;
the destruction body retains its original lookup/handler/removal lifetime and
scene/hierarchy/shard behavior.

`destroy_rows` checks an independent C model over 4,099 initial and 17 later
births: block/chunk tails, exact arithmetic/float bits, divide-by-zero, signed
overflow, masked shifts, destruction-handler values and order, row swaps and
LIFO slot/generation reuse. Serial/pieced execution, private/shared pages and
packed restore must agree. C observers and event-sending helpers beside user
operators pin fallback recording order; sparse filters pin untouched rows. The accompanying
generated-C check requires the optimized helper/flag loop for the numeric case
and refuses it for the C/helper/operator fallbacks. Validation and measurements are
recorded by the coordinator in benchmarks.md.

## Map views across a function (2026-10-20)

Owner-approved: generated code keeps a view of each map it reads or sets by
key for the whole function (`tide_map_view`, `tide/map.h`), so lookup after
lookup doesn't find the map's two lists again (heap, page table, block) at
every one, and a big map's chunks are found once each. Same probe order,
same entries, same results and hashes; no layout changes.

- `tide_list_view` (`tide/list.h`) is now filled in place
  (`tide_list_view_find`) and can be held: `tide_list_view_hold` finds the list
  again only when what it found may have changed, which `tide_list_view_holds`
  tells from the list's handle (`tide_list.at`), its heap's `moves` (a block
  grown, moved or released, a page copied to be a world's own, a page's hash
  asked for) and its count against its block's (elements added or removed in
  place). Writing an element where it is changes neither, and is read through
  the view, as it's the same memory. A list in the scratch area is a view of
  its place, which any list with that handle has.
- In chunks, the first 32 chunks' element places are found the first time
  one of theirs is read (`known`, a bit each), so a probe is the chunk's
  place, from the view, and the element: what was the directory, the page
  table, the page and the element. Past 32 chunks (1,023 16-byte entries a
  chunk, so past about 32,000 entries) the rest go the old way.
- `tide_map_find_in(view, map, kind, probe)` holds both lists' views and
  probes; `tide_map_find` makes a view of its own for the one lookup, as
  before. `tide_map_view_entry` reads the entry found.
- A set through a view (`tide_map%d_cset`) writes the value of a key that's
  there in place (`tide_list_at_mut`, so a page shared with a snapshot is
  copied first, as always) when neither list is an asset's or the pack's
  (`borrowed`), which `tide_map_insert` would make the world's own first;
  anything else, and every new key, goes through `tide_map%d_set` as before.
- `tide_list_cache_fill_mut` finds a side-by-side list's block once, where it
  found it twice (in `tide_list_at_mut`, then again for the cache).

The views are C locals of the function (and of each parallel loop's task),
not state: nothing is kept between calls, snapshots, threads or reloads.
Results: notes/benchmarks.md, Map views and Linux cache groups.

Per-row scratch reset and heap flush inline (2026-10-20): generated code
resets the scratch area to its mark and flushes the world's heap after every
row of a system that makes text or lists (`tide_scratch_reset(tide_mark);
tide_heap_flush(&tide_w->heap);`), and most rows make and release nothing.
`tide_scratch_mark`, `tide_scratch_reset` (`tide/text.h`) and
`tide_heap_flush` (`tide/heap.h`) are now inline, same names, signatures and
effects: the mark is `tide_scratch_top` (text.c's `scratch_used`, now an
extern thread-local, as `tide_scratch_slots` is), a reset to a mark at or
past it does nothing, and a flush with nothing pending does nothing; only
otherwise do they call `tide_scratch_reset_to` and `tide_heap_flush_pending`,
the functions as they were. The rollback agent measured the two calls at
about 5% of versus_rollback's ticks (Rollback, the schedule and thread
scaling, in notes/benchmarks.md).

## A page's hash by stripes (2026-10-20)

The owner's call on a faster page hash: "Measure first", and switch only if a
candidate beats the hash there was natively and as WebAssembly. One did; the
measurements, the candidates that didn't and the quality checks are in
notes/benchmarks.md, Page hash, measured. This supersedes the four chains of
2026-10-07 below ("A page's hash is `tide_hash_wide`"), whose other contracts
(only pages use it, `tide_hash` and the rest unchanged) stand.

- **What it is** (`tide_hash_wide`, `engine/src/net.c`): XXH3's accumulation
  with keys and an end of our own. The run, zeros after it to a whole number
  of 64-byte stripes, goes a stripe at a time into eight 64-bit accumulators,
  one a word: for word j, d, and its key word k, accumulator j adds
  `lo32(d ^ k) * hi32(d ^ k)` and accumulator `j ^ 1` adds d (so a product of
  zero loses nothing, and a word's sum is never where its product is). Stripe
  n of each block of 16 takes key words n to n + 7; after each block every
  accumulator becomes `((a ^ a >> 47) ^ key[16 + j]) * 0x9E3779B1`. Then the
  size and the eight accumulators are `tide_hash`'s chain's words and its end
  is `tide_hash`'s. Keys and starting values are splitmix64's numbers from
  "tidehash" (`0x7469646568617368`).
- **The same bits everywhere, by construction and by test**: integer arithmetic
  only. Three ways compute it: clang's two-word vectors (`pmuludq` on SSE2,
  `umlal` on NEON, plain words where a build has no SIMD), WebAssembly's SIMD
  (two vectors' halves gathered by two shuffles, then `extmul_low`/`_high`,
  where `i64x2.mul` is three multiplies under V8), and on x86-64 AVX2's
  four-word vectors where `cpuid` finds AVX2 and the system saves its
  registers (looked at once, a relaxed atomic; inline `cpuid`/`xgetbv` as Box3D's
  dispatch does). Native ways ask for the next block while hashing one
  (`__builtin_prefetch`, a hint). `tide_hash_wide_baseline` (new, `tide/net.h`)
  is the way without AVX2, so a machine with AVX2 tests both; generated code
  calls nothing new. NEON's way is compiled from the same C as SSE2's (clang
  makes `xtn`/`shrn`/`umlal` of it, checked in its assembly) and is tested only
  where CI runs ARM64.
- `PROTOCOL` is 17 (notes/netcode.md): two builds that hash pages differently
  would connect and then disagree about every tick.
- Every pinned world hash moved and nothing else did: the list is in
  notes/benchmarks.md, Page hash, measured.
- Tests (`tests/test_hash.c`, natively and under Node): both ways against the
  definition written plainly (bytes read one at a time) at every length to 300
  from three starts and around blocks and pages, of bytes and of zeros; one bit
  anywhere in 16 KiB; the same bit (0, 31, 32, 33, 62, 63 of each word) in
  every pair of 32 stripes of zeros, in a block and across a scramble; zeros of
  every length to 2,100; four pinned numbers. `bench_hash --quick` also checks
  each of its SIMD ways against its plain definition on both builds.

## Events in runs (2026-10-20)

Internal, with no layout, hash, order or interface change (pinned hashes are
unchanged; measurements in notes/benchmarks.md, Events, second round):

- The queue (`tide_queue`) has a second shape of event, which only generated
  code makes and reads: a run, one item of kind `TIDE_CMD_EVENTS` holding many
  events of one kind, each its target and value, in the order they were sent
  (notes/language.md, Events in runs). A system whose whole body is one Send
  records them, 12 bytes an event for an `int` event where a `tide_command`
  of its own took 24, and the end of the tick takes them in a loop of their
  own. A run is never in the queue between ticks, so a world's bytes, hash,
  snapshots and deltas never hold one, and `tide_command_fits` refuses one.
- Ordinary event items in a row (the same size, kind, event and depth, back
  to back in a page) are taken together the same way: generated code counts
  them from the page's `used`, which relies on a page's items being back to
  back (tide/table.h says so), and moves the queue's place past them.
- A run's dispatcher, or a row of items', finds its targets in the one table
  its handlers take (`index - first`, checked against the row's handle, else
  `tide_entity_location`) with that table's count, page array and first
  handle read once for them all. That rests on handlers never moving an
  archetype's rows: structural changes wait in the queue, and what a handler
  writes changes lane pages only, never the entity column or the page array.
  Anything that changes that rule changes this.
- `tide_table_column_mut` (inline, tide/table.h) no longer writes the page
  pointer back when the page is the table's alone (the case for every write
  but a snapshot's shared page's first): the same function and results, one
  store less for each lane a handler writes.

## Performance pass: lists, maps and grid windows (2026-10-20)

Internal, with no layout, hash, order or interface change (pinned hashes are
unchanged; measurements in notes/benchmarks.md, Performance pass):

- Cached list writes no longer prepare a list the cache already holds
  writable (see language.md for why that's sound). In bench/versus/buckets a
  flat list's write went from 0.050 to 0.028 ms a tick (7x C to 4x).
- `tide_map_find` finds both of a map's lists once (`tide_list_view_of`,
  `tide_list_view_at` in `tide/list.h`: the heap, page table and block that
  `tide_list_at` finds again at every element), not at every slot and entry a
  probe reads; `empty_slot` in map.c does the same. The view holds while the
  code that made it runs on: the page table it read stays until the heap
  settles, and a page copied meanwhile has the same elements. Same probe
  order, same results; asset lists of another element size read as empty, as
  `tide_list_asset_at` has them.
- `tide_map_hash_bytes` writes `tide_hash`'s steps out inline for keys of up
  to 16 bytes, so a lookup with a key of known size makes no call; longer keys
  call `tide_hash`. They must stay the same function: `map_key_hash_is_tide_hash_at_every_size`
  (`tests/test_map.c`) compares them at every size up to 40 bytes.
- `tide_list_prepare` is inline, its signature and what it does the same: a
  list that's a world's own or the scratch area's (not the pack's) returns
  after one look at its block, and only an asset's or the pack's calls
  `tide_list_prepare_borrowed` (the function as it was). A cached write that
  misses (each bucket of a list of lists) and every map insert, which
  prepares both lists, called it: the list of lists in bench/versus/buckets
  went from 0.069 to 0.061 ms a tick.
- Together, 16,384 keys' steady lookups went from 1.92 to 1.41 ms a tick.
  What's left is the chain of dependent loads a probe makes through a chunked
  list (directory, page, element, twice) on a random pattern: about 8x C.

Lane bookkeeping inline (2026-10-20): `tide_table_lanes_shared`,
`tide_table_lanes_begin` and `tide_table_lanes_end` are `static inline` in
`tide/table.h`, with the same names, arguments and behavior, so a chunk whose
lanes the table alone has (no snapshot shares them) costs no call. What a
shared lane costs (taking a page, copying rows, comparing and swapping pages)
stays out of line, in `tide_table_lane_apart` and `tide_table_lane_end_apart`,
`static inline` `noinline` helpers each file that uses them has a copy of, not
more code at every call. No exported function was added and generated code
calls what it called. `versus_systems` with 64 entities of each kind went from
4.8 to 3.9 µs a tick (median of alternating runs) and with 4,096 from 65.2 to
63.8 µs; its executable grew 34 KB (6%). Measurements: notes/benchmarks.md.

Recorded containers (2026-10-19): local event queries capture their statically
read container fields at tick end, including reads through borrowed helpers.
Equal query shapes retain one shared record/tail. Direct parameters still use
the event turn; availability reads the delivered endpoint. Serialized values
are decoded once per delivery batch into a separate temporary heap, never a
match snapshot or live offset. Match bytes/hash/storage are unchanged. Older
omitted-heap statements below are superseded; contracts/tests: language.md.

Attached local fields through query rows (2026-10-19) supersede the historical
read-only/local-field prohibition below. Local code resolves current attached
state; mut bindings write those fields while match rows keep their existing
live, blended, verified or recorded timing. Private row birth metadata prevents
using another entity's fields on a reused handle. Read defaults allocate no row;
mut bindings borrow owned storage at row entry. Outer mutable aliases share storage, and a
birth reset reinitializes its cell in place to retain active pointer validity.
No match state or runtime API changed. Details and validation: language.md.

Mutable parameters beside World.Set (2026-10-19) follow the owner-approved
replacement rule: a self Set updates the stored component and active mutable
copy; later parameter edits use ordinary write-back timing. A private C-stack
context follows synchronous helper calls and is rebuilt at each handler/task
resume. No context pointer is stored in ECS/world/task state. Replacement uses
the active copy's current owned container handles, and final overlapping row
writes reacquire the table's current pages. Read-only entry values retain their
existing copy rule. No runtime API, public C ABI, world layout, hash, packing
or scheduling rule changed. world_set_mut and notes/language.md record coverage.

Read-only parameters beside World.Set (2026-10-18) reload affected component
lanes from stable chunk page slots before each row. Earlier Set writes are
visible regardless of snapshot sharing, across chunks and archetypes. The
current invocation keeps its component value; containers that Set could reuse
are copied through existing scratch value helpers. Input's implicit Owner read
uses the same page reload. Ordinary cached columns, mutable companion lanes
and staged/SIMD paths are unchanged. Structural changes remain deferred and
there is no new runtime API, layout or world state. See notes/language.md and
`world_set_read` for contracts and validation.

Borrowed helper queries (2026-10-18) retain their caller's selection and data
timing using source-specialized generated loops. Live sources pass an existing
world pointer; blended/recorded sources pass their existing row pointer/count,
projecting requested components without rebuilding the set. Passing allocates
no collection or snapshot and changes no ECS/runtime/world layout. Bindings
are read-only; explicit World.Set keeps existing replacement-page semantics.
Queries/whole rows cannot escape into state or task frames. Contracts and
native/web coverage are recorded in language.md, Borrowed query parameters.

## Queries in match event/command handlers (2026-10-18)

Synchronous match handlers now use existing current-world query traversal at
their serial queue turn. The checker restriction was lifted; no ECS storage,
runtime call, iteration order or deferred-queue behavior changed. Queries are
independent of target filtering and remain read-only. Earlier completed writes
and already-applied changes are visible; current mut component parameters keep
ordinary write-back timing. Nested traversal after helper World.Set uses the
existing replaced-page path. `handler_queries` checks sparse selection,
lifecycle visibility, deferred destroys, heap-bearing values, COW and replay,
plus global/targeted command dispatch. Async queries remain unavailable.

## Recipient projections and sparse entity metadata (2026-10-15)

Visibility is physical absence in a recipient world. Components targeted by a
typed visibility policy become internally sparse before archetype derivation.
Existing presence columns therefore drive systems, views, tasks, queries,
Has/Get and extern-system row filtering without deriving every hidden-component
combination. Archetype identity cannot reveal which optional component is hidden.
The server retains the complete world; shared audience groups and component
policies remain ordinary snapshot/hash/reload state there.

Recipient hashes encode permitted values, not heap offsets, allocator free lists
or private policy maps. Generated codecs recurse through fields, text, Lists,
Maps, Sets, arrays and Grids. Map insertion order remains observable and is kept;
grid chunk records are sorted by coordinates, including allocated all-zero
chunks because their iteration extent is observable. Only visible entities and
present permitted components are installed. Hierarchy links skip hidden siblings
and exclude hidden parents; the decoder validates links and cycles before
publishing a temporary world. A projected world with no visible scenes neither
reconstructs Main nor declares that the server match ended.

Cut hierarchy links need permitted spatial consequences as well as local field
values. A private projected boundary anchor retains the exact affine prefix and
rotation needed by a subtree with a permitted Transform, including shear from
nonuniform ancestor scales. It does not materialize hidden parent entities or
their other components. Source local Transform fields remain unchanged. Omit
anchors for subtrees without a permitted Transform: merely knowing an entity
handle does not authorize extra hidden ancestor placement. Reparenting and
destruction release obsolete anchors. This metadata uses the existing generated
map/snapshot/reload machinery and exists only in visibility-enabled games.

Global entity IDs can be far apart in chunked worlds. `tide_entities.pages` is now
a pointer-table span, with NULL pages representing never-materialized metadata.
Reads return an absent zero slot; writes allocate only the addressed page and
preserve copy-on-write. Snapshot retain/free, born metadata, physical hashes,
raw packing, deltas and page repair all support holes. Dense formats remain
unchanged. Sparse raw/delta state names populated page indices in ascending
order; sparse page-hash lists carry an explicit sentinel and indexed hashes.
Unknown gaps are reserved rather than recycled for client prediction.

Coverage: `test_entity_sparse.c` checks high IDs, absence, copy-on-write, birth
metadata, predicted allocation, raw and delta roundtrips, truncated data and
page-hash repair. `visibility_projection` checks IDs beyond 1<<20 with absent
intermediate pages, nested container values, private marker omission, canonical
hash agreement despite different heap/grid allocation histories, real component
absence, C/Tide query filtering and filtered hierarchy destruction.

## Immutable asset descriptors (2026-10-11)

`tide_list` remains a four-byte tagged offset. A `TIDE_LIST_ASSET` block holds logical count and a 32-byte content ID in a 64-byte allocation, not a pointer or pack offset. IDs hash `TideListAsset1`, uint32 count/element shape and exact bytes. An outside-state refcounted heap asset pool retains the immutable payload; each live descriptor contributes one lease. Snapshots retain the pool along with pages. Pool replacement retires its old allocation until heap settle, and lookup includes retired pools so a local borrow remains readable when a helper clears its source during the same expression. Publication and reader lookup use release/acquire. Unique pools update existing lease counts without allocating a pool for every field. The current pack has a separate locked immutable cache, cleared on unload/default geometry writes; retained worlds keep their old bytes.

The owner approved `tide_list_prepare(list, element_size, where)`. Generated writable access passes actual destination ownership before touching an element. Asset reads/copies stay borrowed; preparation copies only at first write and releases a descriptor only when that destination owns it. Same known-ID field assignment is a no-op (32-byte identity comparison); an already-owned field reuses storage on reset. Plain generated Clear releases descriptors without detachment. Nested/text-containing ownership loops detach outer storage before retagging, deep copying or releasing children. Nonasset list copying remains unchanged. C callers must prepare a borrowed local with scratch ownership before mutation; the raw `_at_mut` rejects immutable storage.

Heap hashes still cover allocator metadata and pages every tick, including descriptor IDs. They also authenticate a commutative sum/count of descriptor ID/location leases, independent of pool hash-table order; omitted lease metadata must not pass a network world hash check. They exclude pool pointers, proof and optional detached-chunk origins. Raw pack carries a content dictionary once, each entry's descriptor offsets and normal heap pages; deltas can reference the matching pack or an explicit shared base. Received state validates dictionary shapes/IDs, unique entries and registered descriptor spans/identities/counts, with no duplicate or free/pending descriptor locations. Live grid chunks overwrite their allocation headers with cell data, so scanning every allocation as a block header is invalid; the first full suites caught that mistake, corrected by the explicit registry. Intentional descriptor representation changes the skinned/FBX physical-world hash pins; native and WASI agreed before pinning. `tests/test_asset_lists.c` and generated `asset_sharing` cover ownership, snapshot/unload lifetime, mixed grid/asset state and transfer. Optional origin hints are COW retained outside match state and cleared on allocation release. Ordinary local-to-field copies do not currently propagate hints; small nonchunked edited lists and raw saves use their full representation. This affects encoding size only.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## ECS and simulation state

- Read-only entity queries are parameters of systems, views and synchronous event and command handlers: `query (Body body, with Fighter, without Shield) fighters`, iterated with `foreach (var fighter in fighters)`. Their nested component bindings and filters select a second set, never the code's outer entity. Each row exposes its `entity` handle and named read-only component values. In ordinary control flow outside parallel loops and callback blocks, or as a parallel loop's own rows (`parallel (var f in fighters)`, 2026-11-05: notes/schedule-and-threads.md); no writable bindings, storing queries or whole rows, passing whole rows, nested query parameters or singletons. Queries can be explicitly borrowed by ordinary synchronous functions/methods, retaining the source selection and timing; see Borrowed query parameters in language.md. Async handlers and tasks take none yet. Component values can be copied or passed on. Traversal is normal deterministic ECS order, including sparse presence checks, with no Entity-ID or nearest-hit ordering promise. In a system, query bindings count as global component reads in access analysis, scheduling and tooling, and reuse the `World` reads-own-writes rule when the outer system writes one of them. Structural changes and events remain deferred. A comparison block and a parallel loop's step, which are functions of their own handed the parameters of the code around them, are never handed a query (`sort_captures`, `par_captures` in `compiler/src/codegen.c`): it's no C value, and a row can't be captured either, which the checker says. `compiler/tests/e2e/query_blocks` builds and runs both beside a query.
- **Queries in views and local handlers** (agreed 2026-10-05; `docs/spec.md`, Read-only entity queries, and its Provisional picks under Views and drawing): local code reads the match, so its queries do too, as that code sees it.
  - A view's rows are what the view sees: blended between ticks like its own parameters, with corrections smoothed, over the entities of the world it draws. `verified` in a query follows the rule for views: all of it `verified`, the confirmed world's entities (`param.query_verified_entities`); some of it, the drawn world's that are the same entity in the confirmed one (`param.query_verified_join`, by `tide_born_same`), filters included. A query is all the match's or all local: a local one reads the local world as it is, through the code a system's query generates.
  - A local handler's rows are the match as the tick that sent its event left it, once the tick is done (Provisional: not as each event's turn left things), and a query of local entities the local world as it is. A handler of a local event takes only those.
  - Read-only (Provisional): no `mut`, and a row has the match's fields, never an entity's local fields. A view with a query over the match doesn't run outside a match (`reads_match` in `compiler/src/codegen.c`).
  - **Going through a lot stays cheap:** a view's or a local handler's query over the match (`param.query_rows`, set in `check_params`) is handed its rows, an array made before the code runs, which `gen_query_foreach` loops over: a generated `tide_row_<view|handler>_<Name>_<i>` each, the entity and the bound components. A view's are made once for its run in a frame, in the scratch area (`gen_query_fill`: `tide_query_<View>_<i>`, called when the view first runs for an entity, so one no entity matches makes none; the run function moves its `tide_mark` past them and lets them go at its end). So a view that runs for each of N entities and goes through M others blends the M once, not N times. A row's binding is gathered and blended only in the lanes the view reads of it (`view_query_lanes`; Views copy and blend only the lanes they read, above), as is a per-entity view's own component. Measured in a release build, 2,048 bodies each going through all 2,048: about 2 ms a frame, against about 13 ms for a list a view fills by hand each frame, and about 80 ms when the rows are made again for each entity.
  - A tick records a listener's rows after its events, once for the tick, when it recorded an event with a handler that reads them: `tide_hear_<Event>` sets `tide_rows_wanted` and the event's `TIDE_HEARD_READS_ROWS`, and `tide_world_tick_events` calls `tide_hear_rows` (`gen_hear_rows`) once the tick is done, which pushes a `TIDE_HEARD_ROWS` record for each (`tide/events.h`: `handlers` says which rows, `same` how many, the rows follow, and `tide_event_log_trim` gives back what the sparse filters left unused). Queries with the same entities and components share one (`collect_heard_rows`, `same_rows`), and their layout is part of `TIDE_HEARD_BUILD`. A client's hearing sends a run's rows out with the first of its events that reads rows and went out (`tide_hearing_end`), and keeps them with the run for `verified` handlers. The frame finds an event's rows after it in the log (`tide_event_log_rows`: the first with its tick and `as`, which are its run's). Text, lists, maps, sets and grids aren't in them.
  - The language server says what a view's and a listener's queries read on hover and in the lens above them (`put_world_lens`), from the same `world_uses` a system's are in.
  - `compiler/tests/e2e/view_queries` checks it: rows blended between two ticks, an entity that came and one that went, filters, a local query, `verified` against a world that went otherwise and on a client that predicted wrong, and handlers' rows from their tick, once, with the confirmed tick's for a `verified` one.
- Archetypes derive from the spawn sites and from the Adds and Removes that can reach each one (`change_site` in `compiler/src/ast.h`, `derive_archetypes` in `check.c`): `this.Add` reaches what its system's filters admit, an Add on a local a Spawn in the same code set reaches that archetype, and an Add on an entity from a field or parameter reaches every archetype of its world. From an archetype, every combination of the components of the sites reaching it is reachable (their changes apply in any order at the tick's end), and the archetypes derived from it inherit what it could still apply this tick. Components are bits of a 256-bit mask (`comp_mask`), and a game has at most 1024 archetypes. Generated code for an Add or Remove that reaches no archetype stops the program (`tide_unreachable_change`) rather than leaving the entity as it was.
- Sparse components (`decl.is_sparse`: `[Sparse]`, or a tag kept in the row to stay within the archetype limit, see below) are no part of an archetype's mask: each archetype whose entities can have one (`program.possible`, inherited both ways along the moves between archetypes) stores a presence byte column and then its lanes, Add and Remove of it change the row in place (`apply_add`, `apply_remove` in the generated C), and loops, handlers and views skip the rows without it (`gen_row_filter`). Their filters admit an archetype by the components that aren't sparse (`filters_admit`). To the schedule a sparse component keeps no two systems apart (`can_meet` in `parallel.c`, below): entities with it and without it are rows of the same chunks. The layout marks them (`tide_layout_place.sparse`), and migration carries the presence byte with the value.
  - Tags move entities by default (owner's decision, 2026-10-29, replacing the day's `[Lasting]` opt-in and the earlier rule that a toggled tag was sparse by itself): `sparse_component_source` in `check.c` is `[Sparse]` or `decl.sparse_fallback`. `derive_archetypes` tries the archetypes with every other component moving; past `MAX_ARCHETYPES` it marks one more tag `sparse_fallback` and tries again (`fallback_tag`: first tags named by an Add or Remove whose site has no `from` and no `spawn`, which reaches every archetype, then any toggled tag, each in declaration order), and only errors when no tag is left. Components with fields never fall back. Foldline's `Glimpse`, `LastWitness` and `WellFall` fall back, so it keeps its 672 archetypes. `compiler/tests/e2e/tag_moves` checks moves against a `[Sparse]` tag, snapshots and packing; `tag_fallback` twelve tags added through a field (4,096 combinations a base) beside a bounded tag that still moves. Measurements: notes/benchmarks.md (Moving tags under a client's rollback, Holes, Lanes a system can't write).
  - Add and Remove are a function per component (`tide_apply_add<n>`, `tide_apply_remove<n>`, `noinline`), which `tide_apply_add`/`_remove` dispatch to by the change's component. As one function, `tag_fallback`'s 13 tags times 768 archetypes passed V8's limit of locals in one WebAssembly function in an unoptimized build (`local count too large`).
- Text, lists, maps and sets in fields live in the world's heap (`tide/heap.h`): blocks handed out and reused in a fixed order, so every machine gets the same offsets. Freed memory is zeroed. Generated code says where each text, list or map it writes is (`where`: the match's, the local world's, or no world's), never the address. A map (`Map<K, V>`, `Set<K>`, `tide/map.h`) is two lists: its entries in insertion order, each a signed 64-bit order, the key and the value, and an index of entry numbers by the key's hash, a power of two of slots under half full. Positive orders are live; removing an entry negates its order and zeroes the rest. Adding a key packs live entries together when more than half are dead and at least eight are. `tide_map` is 24 bytes aligned to eight: the two handles, live count, explicit zero padding and last insertion order. Clear retains that order; copies and assignments carry it with their entries. tidec generates each map type's entry struct, hash and comparison (`gen_map_helpers`), so the engine's part is generic.
  - Lookup specialization (2026-10-11): `tide_map_find` is forced inline in the header so clang can specialize a fixed generated kind, including hash/equality callbacks and entry addressing. It still uses checked list reads and the original hash/probe sequence, with no retained pointers, allocations or changed world bytes. Collision/wrap/chunk and snapshot regression coverage is in `tests/test_map.c`; the native/web suites and independent C benchmark digests agree. This does not parallelize shared map writes or change insertion/erase/iteration semantics. Paired measurements are in `notes/benchmarks.md`.
  - `tide_map_next` follows the last visited insertion order with a checked index hint. Normal traversal takes constant work per entry; after compaction it finds the first later order by binary search over the retained magnitudes, then skips removed entries. It changes no map and allocates nothing. Each generated foreach has its own two integer cursor locals, kept in a task's frame across waits: removals, reinsertion, Clear followed by additions, nested loops and another system's compaction cannot move a later insertion behind the cursor. `tests/test_map.c` checks those runtime boundaries and signed orders beyond 32 bits; `compiler/tests/e2e/map_iteration` checks exact map/set sequences, continue, nested cursors, async waits, unchanged snapshots and packed waiting-task restoration.
- A world's list whose elements take more than a page keeps them in chunks, heap blocks of a page each, listed in its own block (`tide/list.h`): changing an element copies and hashes its chunk, never the whole list, and growing adds chunks without moving elements. Reading and writing elements is inline, and code reads and writes them through a run cache for each list a function indexes (`tide_list_cache`, `tide_lc`: a slot for each place that names one, `pool.heads` and `pool.counts` apart, keyed by `cache_key` in `compiler/src/codegen.c`, and the array's size patched in once the body says how many, `declare_list_caches` and `close_list_caches`), so a loop finds each chunk once, and a write makes a run's page the world's own once and writes the rest where they are (`tide_list_cached_at_mut`, `tide_list_cache_fill_mut`: the cache says it's `writable`); it holds while the heap's `moves` count is what it was, and only below the list's count as it is then. Measured in `bench/versus/buckets` (2026-10-06, `list-caches` against `nested-lists` in `bench/results/versus.jsonl`): a flat list of 4,096 rings written a value a tick went from 0.082 ms a tick to 0.019, 2.2x C's arrays where it was 9x, and the list of lists from 0.165 to 0.043, which is the one `tide_list_at_mut` each inner list still costs, as each bucket's is another list. C, which takes a list as its elements side by side, gets a copy of a list in chunks, written back after a `mut` call (`tide_list_flatten`), chunk by chunk and only where the bytes differ (`put_elements` in `engine/src/list.c`). A chunk that differs and that a snapshot shares is made the list's own without what was in it (`tide_heap_write_over`), as all of it is written again. The copy is megabytes for a big list, so the last one is kept for the next call (`flat_take`, `flat_give`), each saying how much room it has in the bytes before it, as two can be out at once (a mesh's vertices and indices) and the one that's kept isn't always the last one made.
- `list.Resize(count)` sizes a list in one call (`tide_list_resize`; Provisional in `docs/spec.md`, Lists). Growing writes nothing but the count in a world: a heap's blocks start zeroed and whatever takes elements out of a list zeroes where they were (a page's hash covers them), so the room past a list's count is zeros. A list in the scratch area, whose memory is whatever was there, zeroes what it grows by. Shrinking a world's list zeroes the end of the last chunk it keeps and gives the chunks past it back to the heap (`cut`), where `Clear` keeps them all, zeroed; a list in chunks that's left with none gives its block back too, so there's never a list of no chunks. Elements that hold text let go of it first, in generated code, as `Clear`'s do. A list type's helper (`tide_list<n>_resize`) is only in the C of a game that calls it (`decl.resized`), so the others generate what they did.
- Grids (`Grid2<T>`, `Grid3<T>`, `tide/grid.h`) keep their cells in chunks of 4096 cells (fewer for cells over 4 bytes, so a chunk is never more than a page), a heap block each (its header is cells too, until it's released), listed in the grid's directory: a table by chunk position (by index for a grid sized on every axis with 65,536 chunks at most, else hashed) and each chunk's record (its position and block). A chunk only exists once a cell in it is set to something other than zero. Grids are fields of components, singletons and scenes only, never copied: tidec keeps them out of locals, structs, events, inputs and functions. Generated code reads and writes cells through a chunk cache per grid type in each function (`tide_grid_cache`, `tide_gc`), so a loop looks each chunk up once rather than at every cell; it holds while the heap's `moves` count (a page copied to be changed, or blocks released) is what it was, so whatever the code calls meanwhile, it never reads stale cells.
- Text and lists that code makes along the way live in a scratch area outside the world, cleared after each system, view and handler runs, and are only ever copied into a world. It's a stack of bytes per thread, addressed by offsets of 30 bits, which grows as it needs in pieces of whole slots of 1 MiB (`tide_scratch_at` in `tide/text.h`), each as big as what it's made for, so a list or text of any size is whole and the same whatever else was made before it; a piece goes when the area goes back to a mark from before it, the biggest (up to 64 MiB) kept as a spare for the next. Running out of memory, or of the offsets, ends the program with a message naming what didn't fit, as `tide_out_of_memory` does for worlds.
- **Spares go once nothing takes them, and memory is waited for** (2026-11-06, the owner, after mount-and-rifle's native crash: "do 2 and 3 ... if instead of a crash we have a stutter i think thats more acceptable"). The crash: `bind_nodes` (`skinned.c`, a `SkinnedMesh` draw in the view `Infantry.HeldWeapon`) asked for 876 bytes with 2,097,040 in the area, a new 1 MiB piece, and `malloc` gave NULL: the dump said 84.8 of 94.2 GB committed system-wide just after, the game 6.9 GB private, no job limit, so Windows' commit was full for a moment. Now:
  - A spare (what `tide_scratch_reset_to` keeps of the pieces it lets go) that nothing took for a second goes (`tide_scratch_trim(idle_ns)`, `SPARE_IDLE_NS`): checked when a thread leaves a tick's work (`work` in `engine/src/jobs.c`, the library's own copy in a reloading run, as the host's pool would trim the host's copy of the area), and every 256th time the area goes back to empty with a spare kept. When a spare is let go or taken again its time starts over, so code that makes something big for each entity, each frame, keeps it; a thread that did once doesn't keep up to 64 MiB of commit for good. Where things go in the area doesn't change: a piece made of the spare takes only the slots asked for, as before.
  - A piece's `malloc` that fails waits (`tide_memory_wait`, notes/memory-safety.md, Running out of memory) and tries again; past the wait, or when the offsets are used up (which says so, and doesn't wait), the program ends as before, now with the memory's figures (`tide_memory_ran_out`).
  - Test: `scratch_spare_goes_when_nothing_takes_it` (`tests/test_scratch.c`).
- A piece of text is where its text is: `Substring` gives a `tide_str` into the same bytes, and copies nothing (2026-10-08, with `char`: see `notes/language.md`). So a `tide_str`'s next byte is a NUL only when it's a whole text or the end of one, which was already so for text another was joined to in place: what needs C text goes through `tide_str_c`, never `ptr` alone. A text's characters are read by index (`tide_str_char`) and in turn (`tide_str_next`), inline where its bytes and its characters are as many, and by counting from the start otherwise.
- Text searches (`Contains`, `IndexOf`, and the searches in `Replace`, `engine/src/text.c`) compare counted bytes, with `IndexOf` converting the first matching byte offset to a UTF-8 character index afterwards. Patterns shorter than 32 bytes skip to their first byte with `memchr`; longer ones use Crochemore and Perrin's Two-Way search, with constant extra space and linear worst-case work even for repeated prefixes. `Replace` searches each remaining suffix, keeps nonoverlapping matches, and returns its input when nothing matches or the pattern is empty. No search allocates. `tests/test_text.c` compares both search paths against a plain reference over periodic and varied counted buffers (including zero and high bytes), checks Unicode positions/replacements, and searches/replaces a long nearly matching repeated prefix. Measured on Windows in release, seven batches of ten searches, a 1 MiB string of `a` and a 16 KiB pattern of `a` ending in `b`: the median was 164.1 ms before and 1.05 ms after; the straightforward linear KMP reference took 3.24 and 3.36 ms respectively. These are local microbenchmark times, not a cross-platform timing promise.
- Tasks are state machines tidec makes from async code (`gen_task_resume` in `compiler/src/codegen.c`), as C# does: a resume function per async function or handler and world, which goes on from where its frame says it stopped, until it waits again or ends. A frame is a generated struct: where it stopped, its parameters, its value, the frames of the async functions it awaits (so a task is one frame however deep it awaits, and nothing awaits itself), and a slot for each C local in scope at a wait, which goes in before the task waits and comes back after. No task keeps an address: components and singletons are got again from the task's owner entity each time it goes on. A world keeps its waiting tasks in a `tide_table` (one column: `tide_task`, then the frame) per async function and handler, and goes through them in its task pass, once its tick's changes (or its frame's) are in: those whose time has come go on one at a time, in the order they started, on copies of their frames, as tasks they start may move rows; those whose owner is gone end. A system that starts tasks runs them until they first wait, inside it, so it doesn't split, and waits for systems that start tasks, spawn or change text (`CONFLICT_TASKS`).
- Local fields (`local List<Vertex3> verts;` in a match component or singleton) are local state for the match's entities, and nothing of them is in the match: its layout, hash, deltas and what a joiner downloads are what they'd be without them. `compiler/tests/e2e/local_fields_same.h` pins that against the same game without them, and a change to how worlds are kept or sent updates its numbers.
  - The checker takes them out of their declaration into a declaration of their own, its local part (`decl.local_part`, `split_local_fields` in `compiler/src/check.c`): a local component or singleton of the same name, in no list of names, whose fields `box.verts` finds (`check_local_field`). So nothing that lays the match out can see them. Its struct is `<Name>_local`.
  - A singleton's local part is a local singleton. A component's is in a table of the local world, `tide_fields_<Component>` (`tide/attached.h`): the entity, the value and what the entity was born as (below), a row for each entity something was written for, found from the entity's index (two generations of one index can each have a row, as the two worlds can each have another entity there). The value is one column, so code reads and writes it where it is.
  - Code reaches them through a parameter that comes with a second pointer, to the local part (`has_fields_ptr`, `tide_lf_<name>` in generated C): a view's, a `Forgotten` handler's, and a function's, which the checker marks (`param.local_use`) and follows through calls (`check_local_field_calls`). A view's wrapper finds each row's (`gen_row_fields`): where there's none, the defaults to read, and for a `mut` parameter a copy of them, which becomes a row if the view changed it. Defaults that need the heap are an entity's own from the first run of a view that takes them as `mut`.
  - They go at the start of `tide_frame` (`tide_local_forget`), when either world's tick changed: an entity's when neither the world views draw nor the verified one has it with the component, born as the row's was, a singleton's (back to its defaults) when there's no match, and all of them after `tide_local_disconnected`. Handlers of `Forgotten` run first, with the fields there; it's never queued.
  - What they hold is in the local world's heap. The match only has a heap for what the match holds (`world_heap` in `compiler/src/codegen.c`).
  - A rollback can give an entity's handle to another entity (a predicted spawn, where the server had another), so a row is kept for the entity, not the handle: by what it was born as, a number of the tick and what it was spawned with (its archetype, its scene and each component's bytes, never the spawn's struct, which has padding: `tide_born` in the generated C, set as the spawn applies). The entity table keeps it beside the slots (`tide_entity_set_born`, `tide_entity_born` in `tide/entity.h`: pages after the slots' in `tide_entities.page`, made when one is first set, which snapshots share and copy when they change). It's beside the world and never in it: not hashed, packed or sent, so a world that came as bytes or as a delta has none, and 0, not known, is the same as anything (`tide_born_same`); rows C sets have 0 too. A game with local fields or correction-tagged fields on a component sets it (`entities_born` in `compiler/src/codegen.c`), so others' spawns and entity tables are as they were. A replay that spawns something else on a handle drops what was kept there, and the same spawn again keeps it; hot reloading carries each row's with it.
- Worlds grow as they need, with no limits but memory (no engine limit stands in for checking inputs: that's the game's): running out of memory ends the program. A world's heap is the exception: its offsets have 30 bits, so it holds 1 GiB, in blocks of 128 MiB at most (`engine/src/heap.c`), and past either the program ends the same way. The one depth that's checked is a chain of events, each sent or spawned by a handler of the one before: an endless one would never stop, so past `TIDE_MAX_CHAIN` (100,000) in one tick the program stops, naming the event. Each queued change records how far down its chain it is. Their data is in pages (`tide/page.h`): each archetype's rows in chunks, each chunk's column a page (`tide/table.h`), the entity table's slots and the heap. A snapshot (`tide_world_copy`) shares the world's pages, and whoever changes a shared page first gets a copy of their own, so a world and its snapshots cost the memory of what's different. A page nothing refers to any more is handed out again (`tide_page_give`, `tide_page_take` in `engine/src/page.c`): a client lets go of as many pages every tick as the tick changed, with the snapshot it drops, and takes as many, and a new page of these sizes comes from the system each time, with a page fault as each 4 KiB of it is first written, where one just let go of is still in the CPU's caches. Copying into new ones took five times as long (2,400 Box3D bodies: a tick from 2.5 ms to 1.75, `packages/box3d`'s `box3d_costs`; `bench_gallery`'s client snapshots a quarter less, `bench/results/gallery.jsonl`, `page-spares`). They're kept by size, each thread its own, and what a thread has too many of goes where the others find them, as the thread that drops a snapshot is often not the one whose system writes; a page that comes from there is another thread's, so the web's memory is synced (`tide_memory_sync`). What takes one sets every byte of it: it's whatever it was. A new, zeroed page (`tide_page_new`) of these sizes is a spare zeroed too (2026-11-02; a projection loaded every tick takes as many as it let go of, notes/netcode.md, A lone player's take, cheaper); other sizes are `calloc`'s. A generated Tide system's loop over a chunk writes the lanes it declares `mut` in place when the table alone has their pages. When a snapshot shares any lane it changes, it reads all the changed lanes where they are and writes them apart, into pages of the table's own (`tide_table_lanes_begin` with `apart`, chosen by `tide_table_lanes_shared`): nothing is copied first, including for systems with sparse filters or input. Once the loop is done, a page with a row that changed is the table's and one with none is let go (`tide_table_lanes_end`, which keeps it for the next: `spares`), so a page that nothing changed stays shared, with its hash good, however many systems declare it `mut`. A system that reads through its World a component it changes (`world_reads_own_writes`) still writes in place, copying a shared page first: it must see its earlier rows' writes through the World on every machine. Handlers and spawns, which change one row, make the page their own as they write, and a component put back for one entity (a handler's `mut`, `world.Set`, `tide_set_<Component>`) only where a lane isn't what's there already (`tide_table_scatter`): code that changes one field of a component of forty lanes leaves the other pages shared and their hashes good.
  - Sparse filters count matching rows before the chunk's lanes are taken (`gen_rows_count`). A whole chunk with none is left alone, its pages and hashes too. When writing apart, the loop writes the rows it skips as they were, so filtered systems need no first copy of a page.
  - A system that takes its entities' input reads their Owner from the lanes the loop reads (`gen_row_owner`), including when it changes Owner and writes it apart. The schedule counts that as a read of Owner (see Determinism).
  - A chunk in pieces begins its changed lanes once before the pieces and ends them once after (`tide_table_lanes_begin_pieces`, `tide_table_lanes_piece`, `tide_table_lanes_end_pieces`; see Determinism for who calls them). Apart, the page to write is in the table from the start and remembers the old page in `tide_page.was`; each piece writes all its rows, including the skipped ones unchanged. The end keeps the old shared page when no row changed. A whole chunk with no matching rows takes no pages for its pieces.
  - Systems written in C still copy shared pages before their chunks run in pieces; handlers, spawns and other one-row updates keep their existing path. These paths must read what they write in place.
  - This rests on the schedule keeping two systems off the same chunk's lanes at once when either writes them, including systems whose sparse filters admit opposite rows (see Determinism). `compiler/tests/e2e/rows` compares whole chunks and pieces, private and shared pages, and one and several threads, and checks unchanged pages stay shared; `tests/test_table.c` checks the pieces functions.
- A component is kept in lanes: columns of 4 bytes of it each (2 or 1 when its size isn't a multiple of 4: `tide_lane_width` in `tide/table.h`), so each of its fields is an array of its own, as C written for speed keeps them. Generated code gathers an entity's components from their lanes into copies (`tide_gather_<Component>`), and puts the `mut` ones back once it's done with the entity (`tide_scatter_<Component>`), so a system's ifs become selects and its loop over entities runs with SIMD. That only holds while the C compiler can tell the lanes' places don't change under the loop:
  - A system's loop over a chunk's rows is a function of its own (`gen_rows` in `compiler/src/codegen.c`): `tide_rows_<System>_<n>`, called for each chunk or piece. Its parameters include each component lane separately, with `restrict` where the loop has exclusive access to its lanes (`rows_alone`). `TIDE_ROWS` keeps the function from being inlined, so the C compiler can use that promise across the whole loop without checking every read/write pair for overlap. Wide loops can then vectorize without those overlap checks; whether a loop vectorizes still depends on its body and filters.
  - The function makes the arrays the loop reads from its own parameters (`tide_in_<Component>` to read what it changes, `tide_col_<Component>` to write and to read the rest). Their places stay fixed through the loop; arrays handed to other functions could otherwise be taken to change under a write through a byte pointer.
  - A system that can write apart has separate in-place and apart functions: the former reads and writes a changed lane through one pointer, the latter through two. A system that can write apart and has sparse filters also has filter-free versions (`every`). After counting the matching rows in the chunk or task's piece, if all pass it calls `every`; if none pass a whole chunk is skipped; otherwise the filter stays in the loop. Apart, the mixed loop gathers and puts back each changed component for every row, running the body only for the matching rows (`through`).
  - Archetypes share functions whose generated text from the parameters on is the same (`rows_list`): the loop depends on the rows' columns, including scenes, Owner and sparse presence, rather than on which table supplied them. More archetypes with the same columns add calls, not copies of the loop.
  - `restrict` is omitted when code can reach the loop's lanes another way: writes through World, reads through World of a component it also changes, and starting tasks, which get and put back their entity's components themselves (`rows_alone`). Those systems keep the same aliasing behavior.
  - At most 240 scalar lane pointers are parameters of a rows function (`ROWS_LANES_MOST`), counting read and write pointers separately when apart. The whole signature also stays within [V8's 1,000-parameter limit](https://chromium.googlesource.com/v8/v8.git/+/main/src/wasm/wasm-limits.h) for Wasm functions (`ROWS_PARAMS_MOST`): sparse presence columns, fallback arrays, and the leading parameters count too. A component that would cross either budget is passed as arrays of pointers instead, copied into arrays of the loop's own; the fallback has no `restrict` promise for those lanes. `tidec_rows_parameters` checks a legal game at the component limit and, in the web suite, validates its compiled Wasm module.
  - Views keep their loops in their run functions (`gen_chunk`), with only the in-place version: the local world they change has no snapshots. A system whose one Send can be batched also keeps its existing path.
  - A measured gap remains: a chunk with mixed passing and skipped rows still runs its filtered loop, rather than sending runs of passing rows through `every`. The other one, every lane of a `mut` component written apart and compared even when the body never changes some, is closed (below, Lanes a system can't write, 2026-10-29). Neither touches the language's `mut` rule.
  - The wide loops without a filter vectorize at width 4 on SSE2 and Wasm, and 8 on AVX2; mixed filters remain scalar. Five alternating native runs on Windows, 12 cores, using the least saved median and the same corrected C snapshots in both builds (`row-functions-final-before` and `row-functions-final`, measured before later dev integration), put a million entities of twelve float lanes, six changed, at 6.22 -> 3.62 ms a tick on one thread while the prior state is kept. With a sparse tag and every row passing, without a kept state, it is 3.77 -> 1.32; with none passing and a kept state, 2.99 -> 0.105. Unfiltered private rows on twelve offered threads measured 0.615 -> 0.671, while identical C measured 0.534 -> 0.583; that result does not isolate dispatch costs from variation. The settings, ranges, hashes and compile measurements are in `bench/results/row-functions.md` and its metadata.
  - Five alternating end-to-end build rounds of 100 helper functions and 100 entity systems, using one or four archetypes and the least saved median, show the fixture tradeoff: with four archetypes, a Tide-body edit in native release is 33.07 -> 3.16 s, and in web release 16.22 -> 2.69 s; native debug is 0.62 -> 0.73 s and web debug 0.90 -> 0.95 s. Cold builds and unchanged/C/header edits are in the report. These Windows measurements include build completion and were taken with recorded CPU contention; their ranges and the separate direct Clang phases qualify the comparison.
  - A task's components are copies too, loaded each time it goes on and put back when it waits or ends, and around what it awaits or starts, which may change them (`gen_entity_copies` in `compiler/src/codegen.c`).
- Each page keeps the hash of what's in use in it until it changes, so a world's hash (`tide_world_hash`) only reads what changed since the last one. It covers contents, never addresses or capacity, so it's the same on every machine for the same state. The command queue, empty between ticks, isn't shared.
- **A page's hash is `tide_hash_wide`** (2026-10-07; the owner, of a faster hash that would move every pinned world hash: "if its faster at no cost other than out tests then its not even a question, update it"). Its four chains are superseded by stripes (2026-10-20, A page's hash by stripes, above); the rest of this entry stands. What's built, and what code relies on:
  - It's four of `tide_hash`'s chains side by side (`engine/src/net.c`): chain k takes the words at 8k, 32 + 8k and on, with `tide_hash`'s own step (`add_word`); what's left after the last round of 32 bytes goes a word a chain, the last short and zeros past its bytes; then the size and the four chains are one chain's words, and the end is `tide_hash`'s. One chain has the CPU wait for each word's multiplies before the next word; four don't. Three more were measured and not taken: eight chains were no quicker here, and one multiply a word, with four chains or eight, was a tenth quicker, but a word that differs in one bit near its middle then leaves its chain differing in the top bit alone, which the chain's next word undoes if it differs in its own top bit.
  - **Only pages use it** (`tide_page_hash`). `tide_hash`, `tide_hash_more` and `tide_hash_end` are what they were, for everything that isn't a page: how a world's hash puts its pages' hashes together, the physics packages' stamps, what entities are born as, shaders' ids. `Math.Hash` and the seeding of `Random` are functions of their own. So no simulation changed, which the pins say: the physics packages' pins of their bodies and of their own images' bytes, and what was answered, stayed, and every pinned hash of a world moved (40 values in 17 files, two of them decimal in `compiler/tests/CMakeLists.txt`, four the web's own in `local_fields_same.h` and `local_events_same.h`, whose deltas carry the hash).
  - `PROTOCOL` went to 13: two builds of one game that hash pages differently would connect, and then disagree about every tick. `tests/test_commands.c` writes that number by hand.
  - Before resident state (2026-10-07), measured with Sumo's full ring, by `sumo_costs` (three runs each, a quiet machine): keeping and hashing a world on one thread from 0.28 ms to 0.12 natively and to 0.135 under Node; with threads called in (below) from 0.105 to 0.06, and from 0.117 to 0.077.
  - Tests: `tests/test_hash.c`, natively and under Node: it's what its definition says, written the plain way, at every length around its rounds and words; a page that differs in one bit anywhere is another number, the top and middle bits of a word in each chain among them, and so is a run of zeros of another length; and four pinned numbers, the same everywhere.
- **What changed is hashed on a few of the pool's threads, where it's megabytes** (2026-10-06, the owner of cores that sat idle while one hashed: "sounds like we should"; how is Claude's). A page's hash is its own piece of work, and a session hashes between ticks, when the pool has nothing to do. What's built, and what code relies on:
  - `tide_game.hash_world_on(world, jobs)` is the hash sessions ask for when they have threads (`hash_of` in `engine/src/session.c`: the server after each tick, a client as each tick is confirmed). The generated one (`tide_game_hash_on`, in the `.c` alone, so no game's header or hash moved) gathers every page `tide_world_hash` is about to ask of whose kept hash isn't of what it holds (`tide_entities_stale`, `tide_table_stale`, `tide_heap_stale`, into a `tide_stale_pages`, `tide/page.h`), has `tide_pages_hash` hash them, and then calls `tide_world_hash`, which finds each kept. So the hash is the one it was, by the code it was: the threads only fill what it reads. A container that joins a world's hash joins that gathering too, or its pages are hashed on one thread as before, which is slower and never wrong.
  - `tide_pages_hash` asks for a thread more for each half megabyte to hash (`SHARED_HASH_BYTES`), three at the most (`SHARED_HASH_MOST`), and with less than half a megabyte does nothing: the hash that follows does it. Each thread takes the next page nobody has (a counter), so a page is hashed by one thread, and the list has no page twice, as a world has none twice among what it hashes (a heap's page of several places is one).
  - **It asks through `tide_jobs.run_some(self, others, work, context)`, never `run`** (new, and the platform layer's pools have it: desktop's wakes that many workers where `run` wakes all, the web's notifies that many). Waking all of them is what was tried first, and it lost what it gained: on this machine's 12 cores the hash took 0.10 ms where it took 0.28, and the ticks around it 0.69 ms where they took 0.585, the ones after a hash on one thread as much as the ones after a shared one. Waking one, two, three and five took the hash to 0.17, 0.13, 0.105 and 0.09, a run each, with the ticks within a few hundredths of a millisecond of where they were. Why every thread awake slows the ticks wasn't found out. A pool with no `run_some` (a host's own) isn't asked.
  - On the web a round that asks for a few makes no workers: they're made by the first round that wants them all, as a tick with work for threads does, and until then such a round is the caller's alone (`run_some` in `platform/src/jobs.c`). A page pays far more for its workers than hashing would save it; Sumo's are made as its first ring is raised, whose step is shared out.
  - Before resident state (2026-10-07), measured with Sumo (`sumo_costs`, which hashed every other tick's world this way where its program has the platform's pool, so the two are of one scene; old and new in turn, four runs natively and three under Node): keeping and hashing a world with four bots at a full ring 0.28 ms to 0.105 natively and 0.28 to 0.117 under Node, after a shock 0.44 to 0.16 and 0.43 to 0.18, with the ticks between as they were (0.57 to 0.58 natively).
  - Tests: `page_stale_pages_are_hashed_on_threads_before_the_hash` (`tests/test_page.c`, a pool that takes turns: what's gathered, how many threads are asked for, what's left to the hash that follows, pools of other kinds); `jobs_hash_what_changed_on_threads` and `jobs_some_workers_join` (`platform/tests/jobs.c`, the platform's pool: 60 rounds of changes to a table of 4.8 MB against a twin hashed on one thread, and a game's world through `hash_world_on`), natively and under Node.
- A chunk has 2048 rows (`arch_shift`): as many as keep its widest column, the entity's 8 bytes, to a 16 KiB page, so a lane's pages are 8 KiB at most. The first chunk starts with room for 4 rows and grows, so archetypes with a few entities take little memory.
- Structural changes and events wait in the world's queue until the end of the tick (`tide_queue` in `tide/table.h`, `tide_command` in the generated header). Each is queued with the bytes it has, what's before `data` and the member of `data` it carries (`TIDE_COMMAND_SIZE`), so an event costs its own size whatever the game's biggest spawn is: nothing copies or reads a whole `tide_command`. An item starts with its size, which is how the queue goes from one to the next (`tide_queue_next`, from a `tide_queue_place`), and is whole in one page. A task's queue hands its pages to the world's, for the empty ones the world kept (`tide_queue_append`), so merging the tasks' queues copies next to nothing and, after the first ticks, allocates nothing. A world unpacked from bytes checks that each change is as big as its kind needs (`tide_command_fits`), since applying one reads that much.

## Completed-tick collider metadata (2026-10-11)

Retained asset integration: the descriptor ID/location sum and count used by hashing are cached in the refcounted pool and updated on lease creation/removal, avoiding a registry walk each tick. Decode reconstructs those caches from received locations before its world-hash check. Empty asset dictionaries keep the original ordinary-heap unpack path, without new validation allocations. Native/WASI pinned hashes are unchanged; 112 native/104 web overlapping checks pass after merging optional fields and function task support. The `asset_sharing` fixture adds optional asset-list field ownership/clear/retention.

Physics history stores plain collider records and frame lists in the match heap. Capture reads row identity and pre-flush Snap metadata; an owner-approved completion callback runs after all queued Snap/despawn/event changes and refreshes/removes records for the final endpoint. Frames are copied out before inner-list edits, and outer-list cells are reacquired after heap writes, retaining COW/cache safety. Context preparation/querying changes no match state. Rewind tests pin snapshot/pack/unpack/rollback and unchanged world hashes during queries.

## T? fields in the world (2026-10-11)

A field declared `T?` is a struct in its holder's bytes, `tide_optional_<T>`: the value as the field would be without the `?`, then `bool ok`, then padding written out (`notes/language.md`, T? fields). It adds no storage of another kind: a component's lanes, a singleton's bytes, a list's elements, an event's and a spawn's bytes in the queue, and a task's frame hold it as they hold any struct, and its text and lists are in the heap of whatever holds it. One with no value is all zeros: generated code writes zeros when a field is given `null` or a `T?` with none, releases the text and lists of a field an assignment left with none (the end of each `tide_assign_<struct>`), and gives one no defaults. So a page's hash doesn't depend on what a field held before it was taken away; the heap, which gave a block out and took it back, is as it is after any text or list. No runtime function is new. `compiler/tests/e2e/optional_fields` checks the bytes, the hash, a copy, the world as bytes and a delta.

## Transform hierarchy foundation (2026-10-12)

The compiler emits private match/local hierarchy maps and deferred parent commands
using existing checked entity, map, heap and command machinery. Match links are
simulation state, included in snapshots, hashes, packing and generated reload
layouts; local links stay in tide_local. No runtime function was added. Links are
entity relationships independent of public component value shapes. Destruction
walks descendants iteratively, children before parent; no recursive C stack is
needed. Generation/cycle checks run when commands apply, after temporary handle
settlement. Transform composition and keep-world reparenting use Tide's existing
deterministic matrix/quaternion math. Contracts, tests and remaining gaps are
recorded in language.md's shared placement/Transform entry.

## Authored prefab construction (2026-10-12)

Compiler-generated prefab recorders reserve ordinary checked entity handles,
record every row and parent link, then record root-first initialization commands.
A private pending flag suppresses each row's ordinary Spawned dispatch until the
complete tree is installed. Normal spawns use zero pending and retain their order.
References and parent commands use the existing temporary-handle settlement path.
Text/list ownership remains the ordinary spawn ownership machinery. No engine
function or public generated-C ABI was added.

Scene.Load adds a private scene component to the authored root, per world, with
the existing visibility/membership fields. Spawn leaves it out. The existing
scene-column, unload, hierarchy destruction, hash, snapshot and raw-pack machinery
therefore handles both kinds without a wrapper root or runtime prefab registry.
The prefab e2e checks complete initialization, root settings, subtree lifetime,
local isolation, snapshot/raw-pack preservation and serial/split-job equality.
Portable-wire migration and queued prefab hot reload have no dedicated fixture.
