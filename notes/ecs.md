# ECS and simulation state

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

- Read-only entity queries are parameters of systems, views and synchronous event and command handlers: `query (Body body, with Fighter, without Shield) fighters`, iterated with `foreach (var fighter in fighters)`. Their nested component bindings and filters select a second set, never the code's outer entity. Each row exposes its `entity` handle and named read-only component values. In ordinary control flow outside parallel loops and callback blocks; no writable bindings, storing queries or whole rows, passing whole rows, nested query parameters or singletons. Queries can be explicitly borrowed by ordinary synchronous functions/methods, retaining the source selection and timing; see Borrowed query parameters in language.md. Async handlers and tasks take none yet. Component values can be copied or passed on. Traversal is normal deterministic ECS order, including sparse presence checks, with no Entity-ID or nearest-hit ordering promise. In a system, query bindings count as global component reads in access analysis, scheduling and tooling, and reuse the `World` reads-own-writes rule when the outer system writes one of them. Structural changes and events remain deferred. A comparison block and a parallel loop's step, which are functions of their own handed the parameters of the code around them, are never handed a query (`sort_captures`, `par_captures` in `compiler/src/codegen.c`): it's no C value, and a row can't be captured either, which the checker says. `compiler/tests/e2e/query_blocks` builds and runs both beside a query.
- **Queries in views and local handlers** (agreed 2026-10-05; `docs/spec.md`, Read-only entity queries, and its Provisional picks under Views and drawing): local code reads the match, so its queries do too, as that code sees it.
  - A view's rows are what the view sees: blended between ticks like its own parameters, with corrections smoothed, over the entities of the world it draws. `verified` in a query follows the rule for views: all of it `verified`, the confirmed world's entities (`param.query_verified_entities`); some of it, the drawn world's that are the same entity in the confirmed one (`param.query_verified_join`, by `tide_born_same`), filters included. A query is all the match's or all local: a local one reads the local world as it is, through the code a system's query generates.
  - A local handler's rows are the match as the tick that sent its event left it, once the tick is done (Provisional: not as each event's turn left things), and a query of local entities the local world as it is. A handler of a local event takes only those.
  - Read-only (Provisional): no `mut`, and a row has the match's fields, never an entity's local fields. A view with a query over the match doesn't run outside a match (`reads_match` in `compiler/src/codegen.c`).
  - **Going through a lot stays cheap:** a view's or a local handler's query over the match (`param.query_rows`, set in `check_params`) is handed its rows, an array made before the code runs, which `gen_query_foreach` loops over: a generated `tide_row_<view|handler>_<Name>_<i>` each, the entity and the bound components. A view's are made once for its run in a frame, in the scratch area (`gen_query_fill`: `tide_query_<View>_<i>`, called when the view first runs for an entity, so one no entity matches makes none; the run function moves its `tide_mark` past them and lets them go at its end). So a view that runs for each of N entities and goes through M others blends the M once, not N times. Measured in a release build, 2,048 bodies each going through all 2,048: about 2 ms a frame, against about 13 ms for a list a view fills by hand each frame, and about 80 ms when the rows are made again for each entity.
  - A tick records a listener's rows after its events, once for the tick, when it recorded an event with a handler that reads them: `tide_hear_<Event>` sets `tide_rows_wanted` and the event's `TIDE_HEARD_READS_ROWS`, and `tide_world_tick_events` calls `tide_hear_rows` (`gen_hear_rows`) once the tick is done, which pushes a `TIDE_HEARD_ROWS` record for each (`tide/events.h`: `handlers` says which rows, `same` how many, the rows follow, and `tide_event_log_trim` gives back what the sparse filters left unused). Queries with the same entities and components share one (`collect_heard_rows`, `same_rows`), and their layout is part of `TIDE_HEARD_BUILD`. A client's hearing sends a run's rows out with the first of its events that reads rows and went out (`tide_hearing_end`), and keeps them with the run for `verified` handlers. The frame finds an event's rows after it in the log (`tide_event_log_rows`: the first with its tick and `as`, which are its run's). Text, lists, maps, sets and grids aren't in them.
  - The language server says what a view's and a listener's queries read on hover and in the lens above them (`put_world_lens`), from the same `world_uses` a system's are in.
  - `compiler/tests/e2e/view_queries` checks it: rows blended between two ticks, an entity that came and one that went, filters, a local query, `verified` against a world that went otherwise and on a client that predicted wrong, and handlers' rows from their tick, once, with the confirmed tick's for a `verified` one.
- Archetypes derive from the spawn sites and from the Adds and Removes that can reach each one (`change_site` in `compiler/src/ast.h`, `derive_archetypes` in `check.c`): `this.Add` reaches what its system's filters admit, an Add on a local a Spawn in the same code set reaches that archetype, and an Add on an entity from a field or parameter reaches every archetype of its world. From an archetype, every combination of the components of the sites reaching it is reachable (their changes apply in any order at the tick's end), and the archetypes derived from it inherit what it could still apply this tick. Components are bits of a 256-bit mask (`comp_mask`), and a game has at most 1024 archetypes. Generated code for an Add or Remove that reaches no archetype stops the program (`tide_unreachable_change`) rather than leaving the entity as it was.
- Sparse components (`decl.is_sparse`: `[Sparse]`, or a tag any Add or Remove names) are no part of an archetype's mask: each archetype whose entities can have one (`program.possible`, inherited both ways along the moves between archetypes) stores a presence byte column and then its lanes, Add and Remove of it change the row in place (`apply_add`, `apply_remove` in the generated C), and loops, handlers and views skip the rows without it (`gen_row_filter`). Their filters admit an archetype by the components that aren't sparse (`filters_admit`). To the schedule a sparse component keeps no two systems apart (`can_meet` in `parallel.c`, below): entities with it and without it are rows of the same chunks. The layout marks them (`tide_layout_place.sparse`), and migration carries the presence byte with the value.
- Text, lists, maps and sets in fields live in the world's heap (`tide/heap.h`): blocks handed out and reused in a fixed order, so every machine gets the same offsets. Freed memory is zeroed. Generated code says where each text, list or map it writes is (`where`: the match's, the local world's, or no world's), never the address. A map (`Map<K, V>`, `Set<K>`, `tide/map.h`) is two lists: its entries in insertion order, each a signed 64-bit order, the key and the value, and an index of entry numbers by the key's hash, a power of two of slots under half full. Positive orders are live; removing an entry negates its order and zeroes the rest. Adding a key packs live entries together when more than half are dead and at least eight are. `tide_map` is 24 bytes aligned to eight: the two handles, live count, explicit zero padding and last insertion order. Clear retains that order; copies and assignments carry it with their entries. tidec generates each map type's entry struct, hash and comparison (`gen_map_helpers`), so the engine's part is generic.
  - Lookup specialization (2026-10-11): `tide_map_find` is forced inline in the header so clang can specialize a fixed generated kind, including hash/equality callbacks and entry addressing. It still uses checked list reads and the original hash/probe sequence, with no retained pointers, allocations or changed world bytes. Collision/wrap/chunk and snapshot regression coverage is in `tests/test_map.c`; the native/web suites and independent C benchmark digests agree. This does not parallelize shared map writes or change insertion/erase/iteration semantics. Paired measurements are in `notes/benchmarks.md`.
  - `tide_map_next` follows the last visited insertion order with a checked index hint. Normal traversal takes constant work per entry; after compaction it finds the first later order by binary search over the retained magnitudes, then skips removed entries. It changes no map and allocates nothing. Each generated foreach has its own two integer cursor locals, kept in a task's frame across waits: removals, reinsertion, Clear followed by additions, nested loops and another system's compaction cannot move a later insertion behind the cursor. `tests/test_map.c` checks those runtime boundaries and signed orders beyond 32 bits; `compiler/tests/e2e/map_iteration` checks exact map/set sequences, continue, nested cursors, async waits, unchanged snapshots and packed waiting-task restoration.
- A world's list whose elements take more than a page keeps them in chunks, heap blocks of a page each, listed in its own block (`tide/list.h`): changing an element copies and hashes its chunk, never the whole list, and growing adds chunks without moving elements. Reading and writing elements is inline, and code reads and writes them through a run cache for each list a function indexes (`tide_list_cache`, `tide_lc`: a slot for each place that names one, `pool.heads` and `pool.counts` apart, keyed by `cache_key` in `compiler/src/codegen.c`, and the array's size patched in once the body says how many, `declare_list_caches` and `close_list_caches`), so a loop finds each chunk once, and a write makes a run's page the world's own once and writes the rest where they are (`tide_list_cached_at_mut`, `tide_list_cache_fill_mut`: the cache says it's `writable`); it holds while the heap's `moves` count is what it was, and only below the list's count as it is then. Measured in `bench/versus/buckets` (2026-10-06, `list-caches` against `nested-lists` in `bench/results/versus.jsonl`): a flat list of 4,096 rings written a value a tick went from 0.082 ms a tick to 0.019, 2.2x C's arrays where it was 9x, and the list of lists from 0.165 to 0.043, which is the one `tide_list_at_mut` each inner list still costs, as each bucket's is another list. C, which takes a list as its elements side by side, gets a copy of a list in chunks, written back after a `mut` call (`tide_list_flatten`), chunk by chunk and only where the bytes differ (`put_elements` in `engine/src/list.c`). A chunk that differs and that a snapshot shares is made the list's own without what was in it (`tide_heap_write_over`), as all of it is written again. The copy is megabytes for a big list, so the last one is kept for the next call (`flat_take`, `flat_give`), each saying how much room it has in the bytes before it, as two can be out at once (a mesh's vertices and indices) and the one that's kept isn't always the last one made.
- `list.Resize(count)` sizes a list in one call (`tide_list_resize`; Provisional in `docs/spec.md`, Lists). Growing writes nothing but the count in a world: a heap's blocks start zeroed and whatever takes elements out of a list zeroes where they were (a page's hash covers them), so the room past a list's count is zeros. A list in the scratch area, whose memory is whatever was there, zeroes what it grows by. Shrinking a world's list zeroes the end of the last chunk it keeps and gives the chunks past it back to the heap (`cut`), where `Clear` keeps them all, zeroed; a list in chunks that's left with none gives its block back too, so there's never a list of no chunks. Elements that hold text let go of it first, in generated code, as `Clear`'s do. A list type's helper (`tide_list<n>_resize`) is only in the C of a game that calls it (`decl.resized`), so the others generate what they did.
- Grids (`Grid2<T>`, `Grid3<T>`, `tide/grid.h`) keep their cells in chunks of 4096 cells (fewer for cells over 4 bytes, so a chunk is never more than a page), a heap block each (its header is cells too, until it's released), listed in the grid's directory: a table by chunk position (by index for a grid sized on every axis with 65,536 chunks at most, else hashed) and each chunk's record (its position and block). A chunk only exists once a cell in it is set to something other than zero. Grids are fields of components, singletons and scenes only, never copied: tidec keeps them out of locals, structs, events, inputs and functions. Generated code reads and writes cells through a chunk cache per grid type in each function (`tide_grid_cache`, `tide_gc`), so a loop looks each chunk up once rather than at every cell; it holds while the heap's `moves` count (a page copied to be changed, or blocks released) is what it was, so whatever the code calls meanwhile, it never reads stale cells.
- Text and lists that code makes along the way live in a scratch area outside the world, cleared after each system, view and handler runs, and are only ever copied into a world. It's a stack of bytes per thread, addressed by offsets of 30 bits, which grows as it needs in pieces of whole slots of 1 MiB (`tide_scratch_at` in `tide/text.h`), each as big as what it's made for, so a list or text of any size is whole and the same whatever else was made before it; a piece goes when the area goes back to a mark from before it, the biggest (up to 64 MiB) kept as a spare for the next. Running out of memory, or of the offsets, ends the program with a message naming what didn't fit, as `tide_out_of_memory` does for worlds.
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
- Worlds grow as they need, with no limits but memory (no engine limit stands in for checking inputs: that's the game's): running out of memory ends the program. A world's heap is the exception: its offsets have 30 bits, so it holds 1 GiB, in blocks of 128 MiB at most (`engine/src/heap.c`), and past either the program ends the same way. The one depth that's checked is a chain of events, each sent or spawned by a handler of the one before: an endless one would never stop, so past `TIDE_MAX_CHAIN` (100,000) in one tick the program stops, naming the event. Each queued change records how far down its chain it is. Their data is in pages (`tide/page.h`): each archetype's rows in chunks, each chunk's column a page (`tide/table.h`), the entity table's slots and the heap. A snapshot (`tide_world_copy`) shares the world's pages, and whoever changes a shared page first gets a copy of their own, so a world and its snapshots cost the memory of what's different. A page nothing refers to any more is handed out again (`tide_page_give`, `tide_page_take` in `engine/src/page.c`): a client lets go of as many pages every tick as the tick changed, with the snapshot it drops, and takes as many, and a new page of these sizes comes from the system each time, with a page fault as each 4 KiB of it is first written, where one just let go of is still in the CPU's caches. Copying into new ones took five times as long (2,400 Box3D bodies: a tick from 2.5 ms to 1.75, `packages/box3d`'s `box3d_costs`; `bench_gallery`'s client snapshots a quarter less, `bench/results/gallery.jsonl`, `page-spares`). They're kept by size, each thread its own, and what a thread has too many of goes where the others find them, as the thread that drops a snapshot is often not the one whose system writes; a page that comes from there is another thread's, so the web's memory is synced (`tide_memory_sync`). What takes one sets every byte of it: it's whatever it was. A generated Tide system's loop over a chunk writes the lanes it declares `mut` in place when the table alone has their pages. When a snapshot shares any lane it changes, it reads all the changed lanes where they are and writes them apart, into pages of the table's own (`tide_table_lanes_begin` with `apart`, chosen by `tide_table_lanes_shared`): nothing is copied first, including for systems with sparse filters or input. Once the loop is done, a page with a row that changed is the table's and one with none is let go (`tide_table_lanes_end`, which keeps it for the next: `spares`), so a page that nothing changed stays shared, with its hash good, however many systems declare it `mut`. A system that reads through its World a component it changes (`world_reads_own_writes`) still writes in place, copying a shared page first: it must see its earlier rows' writes through the World on every machine. Handlers and spawns, which change one row, make the page their own as they write, and a component put back for one entity (a handler's `mut`, `world.Set`, `tide_set_<Component>`) only where a lane isn't what's there already (`tide_table_scatter`): code that changes one field of a component of forty lanes leaves the other pages shared and their hashes good.
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
  - Two measured gaps remain, and neither optimization is implemented: a chunk with mixed passing and skipped rows still runs its filtered loop, rather than sending runs of passing rows through `every`; and all lanes of a `mut` component are still written apart and compared at the end, even when the body never changes some of them. Knowing which individual lanes can change could avoid those writes and comparisons. These are implementation gaps, with no change to the language's `mut` rule.
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
