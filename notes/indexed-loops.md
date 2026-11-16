## Indexes that take in what changed (2026-11-16, built)

The second half of the plan below (its section 2), built as its way c, with the row-by-row part done another way
and one thing the plan had wrong, which is why the first half alone gained a voxels client nothing.

**What the plan had wrong.** "A client's tables change only when a repair lands" is true of its rows and not of
their pages, which are what a site's facts are. An event handler with a `mut` component takes every lane of that
component for writing in the table chunk its entity is in (`tide_table_column_mut` for each, then
`tide_scatter_some_*` of the lanes it wrote), and a page others hold is copied by whoever takes it for writing:
so in voxels a chunk's `at.x` page is another page after any `Change` or `Loaded` handled in its table chunk
(1,024 rows), with every byte the same. Counted in a 30 s run of 8 bots: the first key page of the chunks' sites
differed, with the rows and the entity column the same, in 36,278 of a client's 41,351 index builds, and in 3,770
of the server's 4,272. So a copy that took its source's index found its facts differing about once a tick and
made its own, as before. And "a plain pass is about 3 µs" (way a) is `bench_index`'s scan of an array: the
generated plain loop over a query goes through rows in identity's order at about 8 ns a row (77 ms for 1,000
lookups of 10,000 chunks, Exact keys below), 50 µs for 6,490, no cheaper than making the index. Way a wasn't built.

**What a loop's start does now** (a world's query by an exact key; `gen_ix_query_follow`, `tide_ix_follow_*` in
`index_codegen.inc`). A site's facts are, for each admitted table in turn, its chunks, then for each table chunk
its rows and the pages the index read of it (the entity column, the filters' presence bytes, the key's lanes, each
once), as many numbers for every chunk of a table.

1. **Looking**: the facts are compared, nothing written. All the same: the index is searched as it is.
2. **Taking** (any differs): the tables are gone through again. A chunk whose facts are the same keeps them, its
   page references moving over. One that differs has its new pages held, and its rows compared with the pages the
   site still holds of it, column by column, 64 rows at a time (`tide_ix_differs`): only a row whose bytes differ
   in a column the index reads, and the rows past the fewer of its rows then and now, are looked at. Such a row
   that is a hole, past the chunk's rows or kept out by the query's filters has its entry dropped
   (`tide_index_drop`); any other is put with its keys (`tide_index_put`, which leaves an entry whose numbers are
   the same, and moves one whose numbers changed). Then the old pages are let go. A chunk the table no longer has
   is cleared (`tide_index_clear`); one it didn't have is all new rows. So a tick costs the pages that were
   written (a compare) and the rows that changed (an entry each), not the rows there are.
3. **Whole** is the same code with nothing known (`fresh`): a site never made, another plan (a reload), a page
   that couldn't be held. Every chunk is "new".

- **An index others hold** (a copied world's and its source's, above) is never changed. Its site still takes the
  new pages for the old where the rows are the same (`tide_ix_follow.frozen`), which is most ticks; at the first row
  that differs it takes a copy of the index for its own (`tide_index_copy`: its groups and entries as they are, a
  few memcpys) and the walk goes on with that. So a client's copy searches its source's index on the ticks nothing
  changed, and pays a copy and the rows that changed on the others.
- **Searches running** (`readers`) when a site's index has to change: the loop makes one of its own, whole, and
  the site is left as it is, index and facts, for a later start to take in (before, the site's facts were taken
  and the site marked not ready, to be made whole next time).
- **A system's own loop** (`fast`, `valid`) is as before: found to hold once in a tick's run, then searched with no
  lock. `valid` is cleared before anything is changed.
- **Other kinds of index are as they were**: one of doubles (a sorted run, a grid sized from every entry) is
  checked by `tide_ix_walk` and made whole when a fact differs, a list's and a view's rows' likewise. A copy still
  takes a doubles index, and makes its own when a page differs.

**The runtime** (`tide/index.h`, `engine/src/index.c`): a **followed index**, a third kind beside the sorted or
grid one and the exact one built whole. `tide_index_begin_followed(ix, exact, dims, room)`; `tide_index_group(ix,
first, span)` names a run of ordinals (a table chunk's rows: `archetype << 32 | chunk << shift`, `1 << shift`
long), made the first time it's named; `tide_index_put`, `tide_index_drop`, `tide_index_clear` change the entries
of the group last named; `tide_index_end` makes it searchable again; `tide_index_followed(ix, exact, dims)` says
whether an index is one, built, of that shape; `tide_index_copy`; `tide_index_changes` counts entries made, moved
and dropped (tests). How it's kept:
- Each group has a run of **places**, one an ordinal, so an entry is found by its ordinal with no search (a
  directory of groups by their first ordinal, open addressing; groups are few). A place has its hash, ordinal,
  whole numbers and doubles.
- The entries whose hash is in a slot are a **ring** (`next`, `prev`: places), ascending by ordinal once built, so
  `tide_index_find_exact` walks one ring and its hits ascend with no sort, as the whole-built kind's do. Slots are
  a power of two, at least the entries (doubled, every entry linked again, when there are more).
- A put goes at its ring's end when its ordinal is past the last (always, for a first build, which comes in
  order). Otherwise it **waits**, and `tide_index_end` sorts the waiting entries by slot and ordinal and goes
  through each ring once for all of its; with more waiting than a quarter of the entries and 64, it links every
  entry again in order instead (groups by their first ordinal, places in turn), which is what bounds a slot most
  entries share (a bool key): never more than a pass over the entries.
- Contracts: a search gives exactly what an index built whole from the same entries gives, in the same order
  (`tide_index_find_exact`: equal whole numbers, doubles within the widened bounds or no number, ascending, once
  each), whatever order entries were put in. Between a change and `tide_index_end` a search gives every entry.
  `tide_index_find` (doubles) gives every entry, ascending. `tide_index_ordinals` gives none (it keeps no such
  list); `tide_index_count` its entries. Groups mustn't overlap (the caller's: a search would give an ordinal
  twice, never a memory error); an ordinal outside the group last named, anything before a group is named, and
  any of these calls on an index that isn't followed, do nothing; `tide_index_add`/`add_exact` on a followed
  index are puts. Places are 2^31 at most; sizes are checked (`bytes`), memory is `tide_alloc`'s.
- Threads: changed by one thread with no search running (the site's lock, no readers), searched by many once
  ended. `tide_index_end` on one that's built writes nothing, so a site that changed nothing may call it on an
  index others search. `tide_index_copy` only reads its source.

**What a stale entry can and can't do.** The generated loop checks every candidate again (its row below the
table's count, not a hole, the filters) and runs the guard on it, so an entry that shouldn't be there costs a
candidate and changes nothing; only a missing entry is a wrong result. The walk drops what it should anyway (a
count that's right, memory that's bounded), but the tests below can only catch the missing kind through the
game, and catch the other kind in the runtime's own (a followed index against one built whole, to the ordinal).

**Tests.**
- `tide_tests index_` (`tests/test_index.c`): `index_followed_gives_what_one_built_whole_gives`: 1 to 8 whole
  numbers with 0 to 3 doubles, in groups of 16, 64 and 1,024 ordinals of two tables, keys of three values (most
  entries in a slot), of a box, and any long, NaN and -inf doubles; 30 rounds each of entries made, moved, put
  again as they were, dropped, a group cleared and partly refilled, a group more, half of everything at once;
  told in any order of groups and backwards within one, only what changed or every entry; after each round every
  search (numbers entries have and numbers none has, bounds open, NaN and inside out) gives the hits of an index
  built whole from the same entries, and no more entries changed than were told. Before `tide_index_end`, a
  search gives every entry. `index_followed_keeps_its_order_when_most_entries_share_a_slot`: 6,000 entries of two
  values told backwards, then 30 and 2,000 moved a round (sorted in, and all linked again).
  `index_followed_copy_is_its_own`, `index_followed_refuses_what_it_cant_take_and_begins_again` (not followed,
  no group, outside the group, a group of none, more hits than a search's own room, begun again as each kind).
- `tidec_e2e_index_follow` (the game and its counts are described above): `index_takes_in_the_rows_that_changed`:
  a key changed where it is (2 table chunks taken in a site, the 200 rows that moved looked at); 40 spawned and a
  ninth destroyed in one tick, the holes filled from the tables' ends; half destroyed (a table a chunk shorter);
  2,000 spawned (two chunks more, exactly the new rows looked at); every item destroyed and as many spawned again;
  no index made whole anywhere, and each quiet tick after finds every index as it was.
  `index_takes_in_a_row_made_where_one_went_with_its_key`: an item destroyed and one spawned into its hole with
  the same key bytes, so only the entity column says an entry went and came (one row of one chunk looked at).
  `index_follows_a_world_rolled_back_and_one_copied_every_tick`: eight ticks of changes run again from a kept
  copy, the same hashes, with no index made whole or copied; a world copied each tick into one that never runs
  (a server's `before`) changes its indexes where they are; a world from bytes makes its indexes once.
  `index_follows_on_threads_as_on_one`: 36 ticks on one thread and on four, the world on four a copy of itself
  every third tick; every crate's search (one site, searched from every thread) right.
  `index_ring_of_copies_makes_an_index_only_when_rows_change` now requires no whole index on any tick, and a
  copied one for each site on the ticks rows changed.
- `tidec_e2e_indexed_loops`, `_plain` and `tidec_indexed_loops_same` (the differential: `Shift` changes every
  seventh cell's place where it is each tick and destroys some, `Renew` and `Expire` spawn and destroy in another
  table), `tidec_e2e_previous`: pass, as before.
- **Mutations**, each built and run against `tidec_e2e_index_follow` and `tidec_e2e_indexed_loops`, or `tide_tests
  index_` for the runtime's. The walk: an index others hold changed where it is (no copy taken) fails 3 tests;
  the compare looking at only the first row of each 64 that differ, 7 and the differential's; the entity column
  left out of the compare, the row made where one went with its key; rows past the chunk's old count not put, 4
  and the differential's; a chunk taken as the same by its rows and entity page alone, 5; the looking pass saying
  "as it was" whatever differs, 6 and the differential's; every row of a taken chunk looked at, the two tests that
  count rows; made whole rather than followed, 5 by their counts. The runtime: an entry moved without leaving
  its old ring, and a dropped one left in its ring, never end (a ring broken: the test's time runs out); an entry
  put at its ring's end out of order fails 4; one with other doubles taken as the same, 1; a waiting entry put
  before its ring's first without becoming the first, 3; an ordinal past its group taken, a crash.
- **Not caught by any test**: a gone chunk's entries not cleared and a hole's entry not dropped, through the game
  (stale entries change nothing, above; the runtime's own tests fail a drop that leaves its entry); the path for
  searches running when an index has to change (threads at the wrong moment: not something a test can place).

**Gaps.**
- An index of doubles is made whole when a page differs, though no row does: the same handlers' writes cost a
  loop by distance over rows that don't move an index a tick. Comparing the pages first, as above, would keep it
  (not built: no game measured has one over rows that are written and don't move).
- A handler takes every lane of a `mut` component for writing in its entity's table chunk, written or not
  (`tide_table_column_mut` of each): that is what makes the pages differ, and it copies them (12 pages of 4 KB for
  a voxels chunk's handler, once a tick for each table chunk a handler runs in). Taking only the lanes it scatters
  (`tide_scatter_some_*` has the mask) would save the copies and the compares both. The ECS's, not this task's.
- A followed index never gives memory back: a group's places stay when its table chunk goes (a table that was
  once large keeps places for its largest), and its slots don't shrink. Bounded by the most rows the tables had.
- `tide_index_copy` copies every entry for one row's change (a client whose copy's rows changed: about 4,100
  copies in a 30 s run of 8 bots, each of a table of 845 chunks, or 3,125 at twice the view).
- Rows' keys are found changed by their bytes in the lanes the index reads, a lane being 4 bytes of the
  component: a write to another field in the same lane (two `byte` fields side by side) looks at the row and
  finds its numbers the same (`tide_index_put` leaves it), which is right and costs a row.

## Copies that keep their source's indexes (2026-11-16, built)

The first half of the plan below (its section 1), built as written but for three things, each said here.

- **What a copy takes, and when.** `tide_world_copy` ends with `tide_index_cache_share(&to->tide_index_cache,
  from->tide_index_cache)`, which only notes the source's cache in the copy's (`tide_index_cache.from`, the cache
  counted by its holders: its world and each copy yet to take from it). The copy takes the sites when it next runs
  (`tide_index_cache_prepare`, serial): for each of the source's sites that is `ready` and `portable`, under the
  source site's lock (its world may be ticking on another thread), `tide_ix_site_share` copies its facts, takes a
  reference to each page it holds (under `tide_ix_refs`; a page whose count would wrap leaves the copy's site as it
  was), and takes the index itself, held by both (`tide_index_share`, a count in the runtime's `tide_index`), with
  `plan`, `most` and `pass`; `valid` 0, so the copy's first search of a tick walks the facts, a compare a chunk.
  A site the source can't give leaves the copy's own as it was, of its own facts.
- **Departure 1: when the copy runs, not when it's made.** The plan took the sites in `tide_world_copy` and kept
  "share lazily" for later. A server copies its world every tick for worlds that never run (`tick_begin`'s
  `before`), which would have paid for every site each tick, and would have left the server's own index held by
  two, so never changed where it is (the second half, below). So it's lazy from the start: a world kept and never
  run costs its source an increment. A source that was itself copied and hasn't run since passes on the cache it
  was to take from (a copy of a copy takes the first's), and a world copied back from its own copy takes nothing.
- **Departure 2: a list's site is never taken**, where the plan had the copy take the source's `epoch` too. A
  list's facts are the cache's run (`epoch`), the list's handle and its count: only good for one run of one cache,
  so there is nothing to gain from taking it, and taking it is the trap the plan names (the copy's own count
  reaching the source's with the list changed where it is). `tide_ix_site.portable` says a site's facts are pages
  and their rows alone (`tide_ix_walk_begin`'s third argument: true for a world's query, false for lists and a
  view's rows), and only such sites are taken. The copy's own list sites keep its own numbers, which only go up,
  so they needn't be emptied either.
- **Departure 3: the count of holders is the runtime's** (`tide_index_share`, `tide_index_shared`; `tide_index_free`
  frees with the last), not a `uint32_t *shared` beside the site's pointer: one allocation fewer, and the index
  can't be freed by a holder that forgot the count. New runtime calls: `notes/runtime-ownership.md`.
- **An index others hold is never built into**: `tide_ix_take`, where it would build into the site's index, first
  lets go of one `tide_index_shared` says others hold and makes its own. That is the only place a site's index is
  written (every path goes through it: checked for `gen_indexed_list` and `gen_ix_rows_search`, whose sites are
  never shared anyway).
- **Threads**: a copy takes sites under each source site's lock, so a source ticking on another thread is safe; two
  worlds that hold one index search it at once, which a built index allows; both counts are atomic.
  `TIDE_INDEX_CACHE_ABI` is 3 (the site's and the cache's layout). A cache of another ABI (met after a reload
  across builds of tidec) is now left where it is rather than freed with this build's layout.
- **Tests** (`tidec_e2e_index_follow`: `compiler/tests/e2e/index_follow.tide`, `index_follow_test.c`). The game
  has a plain loop beside each indexed one (it starts with an effect, so it goes through every row) and the test
  requires the two to find the same rows in the same order every tick; the build counts how each loop's start
  found its index (`TIDE_INDEX_STATS`: `tide_index_stats[5]`, as it was / followed / made whole / table chunks
  taken in / sites a copy took), without which every test would pass with the sharing never taken.
  The game: 3,000 items and 500 with crates found by their place and by kind and x (exact keys, three sites, one
  searched by every crate on threads), 200 lamps found by a float (an index of doubles), and a singleton's list
  rewritten where it is each tick; the test orders spawns, destroys and moves from C between ticks.
  `index_copy_runs_on_its_sources_indexes`: a copy makes no index but its list's and takes 4 sites, and takes none
  until it runs; a copy of a copy; copied back from its own copy; copied over a world with other indexes; the
  source freed before its copies, one of which never ran. `index_copy_and_its_source_part_ways`: 14 ticks of
  different orders each (rows come, go and move in one, then the other), each ending where a world nobody copied
  gets with the same orders. `index_ring_of_copies_makes_an_index_only_when_rows_change`: a ring of five worlds
  each ticking a copy of the one before, as a predicting client, with rows changing every ninth tick, and three
  ticks run again. `index_of_a_list_is_never_a_copys`: the epoch trap, with a copy one tick behind its source, and
  a world from bytes. `index_held_by_several_lasts_until_the_last_lets_go` in `tide_tests`.
- **Mutations** (each built and run): the unsharing in `tide_ix_take` off (an index others hold built into) fails
  `index_copy_and_its_source_part_ways` (by the lamps' index of doubles, the one kind still made whole there; two
  tests before the exact keys' sites stopped going through `tide_ix_take`); a list's site taken fails three, the
  trap's among them; a copy of a copy not passing on its source fails one; the pages' references not taken
  crashes the test. Not caught: taking a site that isn't `ready` (it only happens when a build ran beside searches
  on other threads, or a page's references were about to wrap).
- **On its own this gained voxels nothing** (a client's simulating 5.4 to 5.6 ms a second, within the runs'
  spread): the copy's facts differ from its source's about once a tick though no row does (Indexes that take in
  what changed, above, says why), so it made its own index as before. With that section's walk a copy keeps the
  index while the rows are the same and copies it when one changes.
- An index of doubles (a grid, a sorted run) is still made whole by a copy whose pages differ.

## Indexes that follow what changed, and copies that keep them: the plan (2026-11-16)

Built the same day: the two sections above say what was built and where it departs from this (copies take sites
when they run, not when they're made; a list's site is never taken; rows are compared by their pages, not kept
keys; way a wasn't built, its plain pass being 50 µs, not 3; and a client's pages change though its rows don't).
Kept as it was written, for the reasoning.

Asked for with the repairs' fix (notes/netcode.md, top section), which took the time there was; nothing below is
built or measured beyond what `notes/voxels.md` (At scale, What costs a chunk that's only held) already has. It is
the plan for its own task, as far as the code was read for it.

**What it costs now.** A world's query index is one site's (`tide_ix_site`): its facts are, for each table chunk of
each admitted archetype, the chunk's row count and the pages of its entity column, the filters' presence bytes and
the key's lanes (`gen_ix_query_facts`), each page held. Any fact that differs makes the whole index again
(`tide_ix_take`, then `gen_ix_begin`, `gen_ix_query_rows`, `tide_index_end`): about 10 µs for each 1,000 entries
(102 µs for 10,000, Exact keys below). And each world has its own cache (`tide_world_copy` leaves it).

- **A server** whose chunks come and go every tick makes `Has`, `Find` and `Firm`'s indexes of every chunk about
  twice a tick: 5.3 of its 30.7 ms a second of simulating at the default view (6,490 chunks), 19.6 of 83.6 at twice
  (17,599). It searches them a few times a tick. Sharing with copies does nothing for it: it ticks its own world.
- **A client** ticks a copy of its world each tick (`world_at(c, tick + 1)`), and the copy's cache is whatever that
  slot of its ring had the last time round: 1.7 indexes made a tick, 2.3 ms a second of the 9.6 a client simulates
  at the default view, 9.8 of 29.4 at twice. Its tables change only when a repair lands (about four times a second),
  so nearly all of that is the copy, not the world.

So the two halves are two changes, and each measures best for one side: **copies that keep their source's indexes**
for clients, **an index that takes in the table chunks that changed** for servers.

### 1. A copy keeps its source's indexes (codegen only: `index_codegen.inc`, `tide_world_copy` in `codegen.c`)

A site's facts are page identities, and a held page is never written (a writer copies it first), so an index whose
facts hold is right for any world with those pages: nothing about it is the world's own. What to build:

- `tide_ix_site.ix` shared by count: a `uint32_t *shared` beside it (NULL while the site is the only holder).
  `tide_ix_site_free` gives its share back and frees the index with the last. `tide_ix_take`, where it would build
  into `s->ix` (no readers), first lets go of a shared one and makes its own (`tide_index_make`): an index others
  hold is never built into. Nothing else writes a site's index (every path goes through `tide_ix_take`; check
  `gen_indexed_list` and `gen_ix_rows_search` when building it).
- `tide_index_cache_share(&to->tide_index_cache, from->tide_index_cache)` at the end of `tide_world_copy`: a cache
  for `to` of this build's sites (as `tide_index_cache_prepare` makes), and for each of `from`'s sites that is
  `ready` with no build running (its lock taken for the copy): `to`'s site lets go of what it held, takes a copy of
  the facts, a reference to each held page (under `tide_ix_refs`, as `tide_ix_page` does; a page whose count would
  wrap leaves the site not shared), a share of the index, `plan`, `most` and `pass`; `valid` 0, so the copy's first
  search in a tick's run walks the facts (a compare a chunk, not a build). Sites of `to` that aren't taken are
  emptied.
- **The trap**: a list's site (a singleton's list a system only reads) has the cache's `epoch` among its facts, and
  each cache counts its own. A copied site with the source's epoch could match a later run of the copy's by
  accident, for a list changed in place (same handle, same count). So the copy's cache takes the source's `epoch`
  too (`to->epoch = from->epoch`): every fact it holds is then of a run at or before that number, and its next
  `prepare` is past them all. Sites not shared must be emptied for the same reason, not left with `to`'s older
  numbers.
- Cost of a copy: for each site, its facts copied and its pages referenced (a few pages for each table chunk the
  site reads): not measured. A server pays it too, for the worlds it keeps
  (`tick_begin`'s `before`), which never tick; if that shows, share lazily (the copy keeps a reference to a
  per-cache snapshot of the sites, taken up at its first `prepare`).
- Threads: a world isn't copied while it ticks, so a copy never races its source's builds; two worlds that share an
  index search it at once, which a built index allows (`tide_index_find` reads only), and the count is atomic.
- Results can't change: the same index, the same facts check, candidates checked against the world's own table as
  now.
- Expected: a client's 1.7 builds a tick go to the ticks a repair lands in (about 0.07 a tick): 2.3 ms a second to
  about 0.2 at the default view, 9.8 to under 1 at twice. Measure as `notes/voxels.md` does (a client's simulating).

### 2. An index that takes in the table chunks that changed (runtime and codegen: `engine/src/index.c`, `index_codegen.inc`)

The facts already say which table chunk changed (the walk compares them in chunk order and knows the first that
differs; it would go on comparing instead of taking the rest anew). Three ways, cheapest first:

- **a. No index for a search or two.** A site that's rebuilt nearly every run and searched a few times costs more
  than the plain loop: about 66 µs to build 6,490 entries against about 3 µs a plain pass (102 µs and 4.69 µs for
  10,000, Exact keys below). `tide_ix_site` already counts
  searches that find most rows (`most`, `pass`); the same kind of count (searches since the last build, builds in a
  row with fewer than a handful) would send such a loop through `gen_query_walk`, which is written beside it
  already. Results are the plain loop's by the index's own contract. It is all a server of flying bots needs, and
  nothing once many players walk (12 lookups each a tick in `Blocked`).
- **b. An index for each table chunk.** One `tide_index` a table chunk, each with that chunk's facts; a search asks
  each in order and the hits ascend as now (ordinals are `archetype << 32 | row`, chunks in order). A changed chunk
  makes its own again, at what its own rows cost. But a search is then a lookup for each table chunk where it was
  one (about 20 ns each, Exact keys below), which a client with hundreds of lookups a frame would pay. Only worth
  it with a threshold on chunks, or under c.
- **c. An exact-key index that adds and removes** (the one that keeps both costs low). `build_exact` lays entries
  out by slot with a counting sort, which has no room to add. It would become an open table of slots, each a short
  run of (hash, ordinal, keys) kept in ordinal order (an insertion into a run of one or two), with the entries of
  each table chunk listed so a changed chunk can be diffed: for a chunk whose facts differ, read its rows' keys as
  `gen_ix_query_rows` does, compare with the keys kept for that chunk's ordinals, remove the entries that went or
  changed, add the ones that came or changed. `tide_index_find_exact`'s contract stays: the entries whose hash and
  whole numbers are equal and whose doubles are in the widened bounds, ascending by ordinal, once each. Costs what
  changed in the changed chunks; the table grows by rehashing when it's over half full (amortized). The kinds for
  doubles (`build_sorted`, `build_grid`) keep making the whole: their layouts (a sorted run, a grid sized from the
  bounding box) depend on every entry, and the games that have them (boids) change every key every tick anyway.
  New runtime calls (`tide_index_update_begin`, `_remove`, `_add_exact`, `_update_end`, or one that takes a chunk's
  keys whole) are the generated-C boundary's: `notes/runtime-ownership.md`, and the owner's to agree first.

### Tests the task asked for, for whichever is built

- **A key changing in place**: a row's key written with the same pages held elsewhere (the differential fixture's
  `Shift` does it each tick); for c, a chunk in which one row's key changes and the rest stand, found by the new key
  and no longer by the old.
- **Spawn and destroy in one tick**: a row gone and another made in its place with another key, and with the same
  key (`indexed_loops.tide`'s `Renew` and `Expire`); a table that shrinks by a chunk and grows again.
- **A copy diverging from its source**: copy a world, tick both with different input for some ticks, each against
  the plain build's (`tidec_indexed_loops_same` prints both builds' hashes: print the copy's too); the source
  changing while the copy doesn't and the other way round, a copy of a copy, a copy freed before and after its
  source, a copy made over a world that had other indexes (the ring), a list's site after a copy (the epoch trap:
  change a singleton's list in place with its count the same, on the tick the copy's epoch reaches the source's).
- A count of builds in a test's build (as `TIDE_CLIENT_CHANGES_STATS` is for the repairs) so a test can say a copy
  built nothing and a changed chunk built only itself; without one every test passes with the sharing never taken.
- Mutations to try: a shared index built into in place; the epoch not taken; a site not emptied; a page reference
  not taken (the page freed under the copy: ASan's); for c, a removed entry left, an added one put out of ordinal
  order, a chunk's kept keys not made the new ones.
- Order is the thing to hold: the hits ascend by ordinal, then the generated code sorts candidates by identity
  (Canonical logical entity order, below). Neither change touches that.

## A component handed on for its local fields alone (2026-11-15)

Found by `bench/voxels`' mesher (notes/voxels.md, sections 9 and 12): a view that starts a local task with its own
`mut Chunk chunk` had its loop over the other chunks by `chunk.at` refused, "changes Chunk.at of its own entity as it
goes" (or "its bound reads 'chunk', which the loop changes" where the bound read the view's chunk), though a local task
changes only local fields. Both checks took every `mut` argument as a write of the whole argument.

- **The rule** (`changes_local_fields_alone` in `indexing.c`): an argument to a `mut` parameter of a match component or
  singleton with local fields, whose `param.match_write` is false, changes only what's read through its local fields.
  `walk_writes_expr` notes it in `written_local`, which `loop_changes` counts only for a place read through a local
  field (a loop over the component's local list), and `expr_changes_param` doesn't count it as the code changing its
  own entity's key. Local fields are never keys or bounds, and `match_write` follows helper chains, task starts and awaits
  (`spread_local_field_use`, before `plan_loop_indexes`), so a function or task that writes a match field of it, after
  a wait too, counts as before. Spec: Indexed loops, A component handed on to change its local fields alone
  (Provisional).
- **Tests**: `schedule/indexed_loops.tide`'s `Patches` (a view handing its own component to a local task and to a
  helper that writes a local field) is "indexed by row.other.at.x, row.other.at.y and row.other.at.z as one exact
  key"; `tidec_e2e_task_grid_params`' `Chunks` view promises it with `indexed foreach`; `tidec_error_indexed_task_changes_key`
  (a match task that changes the key's field of a system's own entity) is refused with the reason as before, and so
  is `tidec_error_indexed_local_list_handed_on` (a view's `indexed foreach` over its component's local list, which a
  helper it hands the component to adds to; a first version that skipped the argument altogether let it through).
- Generated C of every game the build knows (579 source lists, plain and `--layout`), old and new tidec, is the same
  but for the two that need the change (notes/language.md, top section): no other loop's plan moved.
- **Correction (review, 2026-11-15)**: C writes what it's handed, and `match_write` is set only by Tide bodies, so an
  `extern`'s `mut` parameter, and a helper's that hands the component on to C, counted as changing local fields alone.
  `changes_local_fields_alone` now takes the callee and needs it not `extern` and the parameter not `c_passed` (settled
  before `plan_loop_indexes` by `settle_c_lends`, check.c). `tidec_error_indexed_extern_changes_key` (bound) and
  `tidec_error_indexed_extern_wrapper_changes_key` (the system's own key) compiled before; refused now.

## Exact keys (2026-11-14)

Found by `bench/voxels` (on its own branch, notes/voxels.md, Gaps): a chunk is found by its place, an `int3`,
with `if (row.chunk.at.x != at.x || row.chunk.at.y != at.y || row.chunk.at.z != at.z) continue;`, and
`choose()` only made a grid of float or double components, so the loop was indexed by `at.x` alone and each lookup
went through every chunk with that x (320 of 10,000 in the bench below). An equality on a `long`
(`row.chunk.key != key`, a place packed into one number) was refused as "its guard puts no bound on a number of the
row": `is_scalar_key` asked `type_dim(t) == 1`, which is 0 for `long` (types.c gives longs no dimension, as there
are no long vectors). Both are fixed; the spec's Indexed loops has the rules as Provisional (Exact keys, Which keys).

- **The analysis** (`indexing.c`). `long` is a key. A constraint is an **exact equality** (`exact_bound`) when its
  bounds are one leaf (`lo == hi`, as an equality gives) and both the key and the value are of exact types: `byte`,
  `int`, `long`, `char`, an enum, a bool, or a component of an int vector (`type_is_int_based` of the value). Then
  the C comparison is one of `int64_t`s (Tide has no unsigned integer but byte, which promotes exactly), so the
  equality is the index's. A byte against a float isn't one: C compares them as floats, and `(int64_t)` of the float
  would miss rows (a long past 2^53 against a double, too). The types are the ones the analysis sees after looking
  through the guard's typed locals and inlined helpers, which can hide a conversion (`double d = row.cell.key;`,
  `double AsDouble(long x) { return x; }`): `env_find` and `inline_call` count each one they look past (a name or
  a call standing for a value of another type), and a bound made while its comparison, or the condition around it,
  went through one is `inexact` (`bound.inexact`, `leaf`), never an exact key's, so it stays a widened double
  bound. Found by the review: the exact key found one row where C's double comparison passes 128. `add_constraint` keeps an exact equality exact: another
  bound on the same key is dropped (it could only narrow, and dropping a bound takes in more rows), and an exact
  equality replaces bounds a key had. `choose()`: a grid of 2-3 float/double components bounded both ways, as
  before; else **every** exact equality (up to `INDEX_EXACT_MOST`, 8, in the guard's order) as one key
  (`loop_index.exact`), with up to three of the guard's other constraints after them, both-bounded first, as
  doubles beside it; else as before. `keep_alike` keeps a query's keys to those whose binding is `previous` as the
  first key's is (one index reads one kind of table), the rest left to the guard. `query_source_still` checks each
  key's (component, field) once: an exact key can be in several fields of several components (`key_still` is the
  old one-field check).
- **The plan's text** (`loop_index_summary`): "indexed by row.chunk.at.x, row.chunk.at.y, row.chunk.at.z and
  row.chunk.dimension as one exact key", "..., and by row.chunk.heat and row.chunk.at.y" for the doubles, ", as the
  tick started: ..." for `previous`.
- **The runtime** (`tide/index.h`, `engine/src/index.c`): `tide_index_begin_exact(ix, exact, dims)`,
  `tide_index_add_exact(ix, whole, keys, ordinal)`, `tide_index_find_exact(ix, whole, lo, hi, hits)`, with
  `TIDE_INDEX_EXACT` 8 and 0-3 doubles. Built as the grid is, with a slot for a cell: slots are a power of two, at
  least the entries; each entry's whole numbers hash (`hash_whole`, multiply-xorshift steps and a splitmix
  finish) to a slot; a stable counting sort lays the entries out by slot, each slot's in the order added, with
  their hashes, whole numbers and doubles beside them. A search hashes its numbers, goes through that slot's run,
  and gives the entries whose hash and every whole number are equal and whose doubles are each within the bounds
  widened as `tide_index_find` widens them, or no number; a NaN bound leaves its double open. A slot's run is in
  the order added, so the hits ascend with no sort. Results depend on the entries and the search only; the hash
  decides cost. Mixing the two kinds is defined and safe: `tide_index_find` of an exact index and
  `tide_index_find_exact` of one of doubles give every entry, `tide_index_add` on an exact index adds whole numbers
  of 0, `tide_index_add_exact` on one of doubles adds its doubles.
- **The code** (`index_codegen.inc`): `gen_ix_bounds` writes an exact key's values as `tide_xeq` (`(int64_t)` of
  each leaf, a vector's component taken first) and its doubles' bounds in `tide_xlo`/`tide_xhi` (one zero element
  when there are none: C has no empty arrays); `gen_ix_keys` a row's `tide_xwhole` and `tide_xkey`;
  `gen_ix_begin`/`gen_ix_add`/`gen_ix_find` call the exact or the double runtime. Every place that wrote
  `tide_index_begin`/`add`/`find` (a world's query, rows made for views and handlers, lists, a parallel list's
  marks) goes through them, and `ix_plan_fits` replaced the `count <= 3` checks. A site's plan text and a list
  site's plan number take in `exact`, so a reload that changes a loop's kind of key builds it again. A borrowed
  query whose caller takes some of the key's bindings `previous` and some not (the analysis sees the function's own
  query) goes through every row in that variant, as one with no table does. `schedule_bytes_codegen.inc` takes in
  every key's lanes, not the first three.
- **What changes for games**: a guard with integer, enum or bool equalities and no float grid now gets an exact key
  where it had one key in doubles: `Count`'s `r.u.team != Team.Blue` in the fixture, `fields.tide`'s `row.b.team ==
  1` (the pinned schedules say so). Bool and small-enum keys put most rows in a slot, and a search finding most rows
  goes through every row as before (When most rows pass). Results are the plain loop's either way.

Tests:
- `tests/test_index.c` (`tide_tests index_`): `index_finds_exact_keys_as_a_scan_does` checks every exact count 1-8
  with 0-3 doubles, 1, 40 and 3,000 entries, against a scan (nothing missing, nothing whose numbers differ or whose
  doubles are outside a box widened 4 times more, ascending, once each): whole numbers of 3 values, of 41, at both
  ends of a long, past 2^62 and random; doubles with NaN and -inf, bounds open, NaN, inside out and one-sided.
  `index_finds_one_chunk_among_many_with_its_x`: 10,240 chunks of a 32 x 5 x 32 box in two dimensions, by place and
  by a long packed past 2^62, exactly one hit each, none for a place that isn't. `index_of_exact_keys_begins_again_and_mixes_safely`:
  never built, empty, not yet built (every entry), one slot of 900 equal entries in order, doubles beside them,
  inside out, begun again with 99 and 7 (taken as 8 and 3), and both kinds mixed.
- `tidec_e2e_indexed_loops` / `_plain` / `tidec_indexed_loops_same`: `Cell` (an int3 place, a dimension, a long key
  past 2^60, an enum, a byte, a char, a bool, a float with NaN on some), 512 in a box 8 x 4 x 8 in two dimensions
  (64 share each x); `Shift` moves every seventh onto another's place each tick (two at a place: `doubled`) and
  destroys some (holes), `Renew` spawns cells into another table that `Expire` destroys. `Seek` searches 24 places
  a tick by int3 + int, by the long, by int + int beside a float bound (NaN rows pass) and an int bound, by enum +
  byte-against-int + int + the body's last bool guard, by char + int, a byte against a float (no exact key), and
  longs compared as doubles through typed locals and a helper beside an int compared as a float (`converted`,
  which must find more than the exact keys; without `bound.inexact` the indexed and plain builds differ),
  each summed in the rows' order into `Found.sum`; `CountAt` is a borrowed query from a system and a view
  (`Glimpse`, whose own loop is a view's rows by int + enum); `Prod` a parallel loop over a query by int + int;
  `Tiles` a list of a read-only singleton (1,900 tiles, 300 on places that have one) by an int2's components. The
  test prints them all each tick, checks the place and key searches find the same cells, and that each kind found
  something on most ticks. `tidec_e2e_previous` / `tidec_previous_same`: `Pair` finds the next boid by its id and
  team as the tick started, in a system that changes the team, beside a `Rank` bound that isn't (`keep_alike`
  leaves it to the guard). `schedule/indexed_loops.tide` pins the plan texts (`Chunks`), `schedule/fields.txt` and
  the `Wrapped` line follow.
- All of the above pass in release and under Windows ASan (`sanitize`); web and the other platforms are CI's.
- Every game of the build generated (tidec alone, 542 runs, prototypes included) with no error: no planned loop
  came out without its index. The two failures of that run are `bench/versus`'s opt-in phase scripts, which look
  for `void tide_system_Step(` where codegen has written `TIDE_CODE void` since b12f8a6e3 (not this work). Of the
  games whose generated C now has an exact key, these native tests pass in release: `earth_tests`, `bench_earth`
  (its three pinned hashes), `bench_forest*`, `bench_render3d`, `towers_tests`, `sumo_tests`, `tide_render2d_pipeline`,
  the `tide_render3d_*` CPU tests, `tidec_e2e_row_lanes`, `tidec_e2e_view_guard`, `tidec_e2e_query_local_fields`,
  `tidec_safety_query_local_fields` and the `row_lanes`/`view_guard` codegen checks.

Measurements (release, Windows, this machine, one thread):

`bench_index` (runtime alone; a search and the guard on each candidate, 2,000 searches):

| Chunks | Build exact | Build by x | Search exact | Search by x | Plain loop | Candidates |
|---|---|---|---|---|---|---|
| 1,000 | 9.0 µs | 13.4 µs | 15.6 ns | 145 ns | 442 ns | 1 against 100 |
| 10,000 | 102 µs | 107 µs | 20.3 ns | 407 ns | 4.69 µs | 1 against 312 |
| 100,000 | 1.33 ms | 1.24 ms | 22.1 ns | 2.19 µs | 42.5 µs | 1 against 1,000 |

`bench_loops --n 0 --chunks 10000 --lookers 1000 --ways <w> --threads 1` (the game, `bench/loops/loops.tide`'s
`Look`: 10,000 chunks that don't move, 1,000 lookups a tick; a tick's time, before on the compiler as it was):

| Lookup | Before | After | Plain loop |
|---|---|---|---|
| By place, `int3` + `int` (`--ways 1`) | 6.4-7.1 ms (by `at.x`: 320 candidates) | 0.061 ms | 77.4 ms |
| By a `long` key (`--ways 2`) | 74.5-75.1 ms (not indexed) | 0.055 ms | 73.6 ms |
| Both (`--ways 3`) | 82.1-83.0 ms | 0.110-0.119 ms | 148.5 ms |

About 60 ns a lookup with the system around it. The world hashes after the runs are the ones before
(`80df2ea6b5754203`, `67796848efb17b51`, `94af1d1ade956c78`), and the bench checks every lookup found its one chunk
both ways.

Gaps:
- A float grid still comes first: `row.team == team && Math.Distance(...) < r` is found by the distance, the team
  left to the guard. A grid with whole numbers beside it (a dimension) would take both.
- `x == a || x == b` on one key is the range between them, not two exact searches; equalities under `||` of
  different keys give no bound, as before.
- `Entity`, `PlayerID` and `LocalEntity` aren't keys: `row.chunk.owner == player` and `row.target == this` go
  through every row.
- Two or three integer components bounded both ways (a box of cells, not one) aren't a grid: one of them is the key.
- A key is rebuilt whole when any page of its lanes changes, as other indexes are; a world whose chunks come and go
  each tick builds it each tick (102 µs for 10,000 at the runtime's level).
- Keys made to share a slot (a hash flood) cost a pass over that slot, which is at most the plain loop; never a
  wrong result.

## A key of the tick's start (2026-11-13)

A query binding marked `previous` reads its component as the tick started (schedule-and-threads.md, top section;
`docs/spec.md`, Read-only entity queries). For a loop whose key is in such a binding:

- **The plan** (`loop_index.previous`, `path_previous` in `indexing.c`) isn't refused for the system changing the
  key's field of its own entity, nor for the loop setting the component through its World
  (`query_source_still`): neither changes what the loop finds rows by. A loop in a function handed the query is
  still refused for a calling system that changes the key and has an unmarked query of the component; a system
  whose queries take it `previous` alone no longer counts.
- **The index** is of the tables the tick kept (`ix_query.before`, `ix_table` in `index_codegen.inc`): its facts
  are the kept pages (entity column, the filters' presence bytes, the key's lanes), so it's checked once in a
  tick's run and searched with no lock by every thread after (`fast` for any code), and kept across ticks while
  nobody changes those pages.
- **One site for every loop that makes the same index** (`ix_site_shared`, `gen.ix_shared`): found by the plan's
  whole text (archetypes, columns, lanes, key paths), not by the loop, so two systems and a function they call
  that search one query the same way build it once a tick between them. Their `most` and `pass` counts are the
  site's, so shared too: any value they hold only changes which of two equal loops runs.
- A candidate is still checked against the world's table as it is (its row, a hole, the filters) and against
  the kept table's count, and its `previous` bindings gathered from the kept table.

`tidec_previous_same` is the differential (the fixture with its indexes and built `NO_LOOP_INDEXES` print the same
each tick); `schedule/previous.tide` pins the plans.

## Narrow plain-query reads (2026-11-07)

The canonical plain traversal now narrows transient component copies to consumed
lanes and reuses eligible read-only chunk column pointers within one loop call.
Indexed candidate collection/gathering and index page proofs are unchanged.
`NO_LOOP_INDEXES` therefore exercises the new plain lowering against the existing
indexed path. Both native ASan and WASI pass `indexed_loops`,
`indexed_loops_plain` and `tidec_indexed_loops_same`, preserving exact tick hashes,
view output and ordinary list order. Escape fallbacks, live Set/COW handling and
focused regressions are documented in language.md, Narrow transient match-query
reads. This does not change which loops may be indexed or their guards/order.

## When most rows pass (2026-11-12)

Mount & Rifle's once-a-tick walks of its 1,000 soldiers start with `if (row.t.down) continue;`. That's a guard
on a bool, so each was an indexed loop, and with nobody down its search gave every row: 1,000 hits copied out,
their entities fetched, sorted by identity (`qsort`, 128-bit keys), then each row gathered whole with
`tide_table_gather`, whatever the body read. 74 ns a row on a 6-lane component and 340 on a 128-lane one, where
the plain loop was 37 (now 11: notes/ecs.md, What a row costs in entity order). Three changes, all in
`gen_indexed_query` (a world's query; rows made for views and lists are as they were):

- **Most rows found: every row instead.** The search runs first; `tide_ix_most(site, found, rows)` is true when
  `found * TIDE_IX_MOST > rows` (`IX_MOST` 6: more than one row in six), and the loop then frees its hits and
  goes through the plain loop, which was already written beside it for tables of fewer than `TIDE_IX_LEAST`
  rows. One in six is where the two cost the same on this machine: a candidate is about 50 ns (the sort, a
  `tide_table_get` a lane) and a plain row 11; at one row in eight the index took 6.6 µs of 1,000 rows against
  10, at one in four 12 against 10.
- **A site that keeps finding most stops searching for a while.** `tide_ix_site` gained `most` (searches in a
  row that found most) and `pass` (runs of the loop left to go through every row with no search). Each such
  search sets `pass` to 1, 3, 7... 63 runs; one that finds few sets `most` back to 0. So a loop whose key changes
  every tick and whose guard most rows pass doesn't check or build an index for each run that then isn't used.
  `tide_ix_pass` is asked before the bounds are worked out. The counts are read and written with relaxed atomics
  by whichever thread runs the loop; any value they hold only changes which of two equal loops runs.
  `TIDE_INDEX_CACHE_ABI` is 2 (the site's layout).
- **A candidate's copy holds the lanes the body reads** (`match_query_lanes`, as the plain loop's since
  2026-11-07), each a `memcpy` from `tide_table_get`; a body that could let the row escape still gathers all.
- **Building reads the keys' lanes only** (`gen_ix_query_rows`): a column pointer per key lane and a `memcpy`
  each into the row, where it looked up and gathered every lane of the key's component.

What a loop does is unchanged: the same rows pass the same guard in the same order. The spec's Indexed loops has
the rule as Provisional, and docs/game/systems.md says it in a paragraph.

Tests: the differential fixture's `Swing` (`indexed_loops.tide`) bounds `health` by a number that takes in
nothing, a few rows or nearly all of them from tick to tick, over two archetypes with a sum that depends on the
order, so one site goes through candidates, every row, and its passes; `indexed_loops_test.c` checks it found
few on some ticks and most on others, and `tidec_indexed_loops_same` compares it with the plain build's. The
fixture's other loops cover the rest: `Count`'s team and bool guards pass most rows, `Avoid`'s few.

Gap: lists and rows made for views still go through an index however many it finds. A list's candidates are
its elements in order with no sort or gather, so there's little to gain.

# Indexed loops

## Canonical logical entity order (2026-11-06)

Match query indexes retain their spatial/key search and existing page proofs.
Their internal ordinals still locate physical archetype rows, but candidate
execution order now follows opaque logical Entity identity across archetypes.
Generated code attaches full keys to hits, sorts those candidates with exact
128-bit comparison, then runs the existing guard and current-row checks. Up to
64 candidates use inline storage; larger searches use bounded checked allocation
released through existing early-return cleanup. The plain small-set fallback uses
the shared cached logical order. Indexed parallel query rows are gathered in the
same order and each step's causal origin and Random seed use its row identity,
not the candidate ordinal. Local-entity queries and ordinary lists retain their
previous order. The differential indexed/plain fixture must remain bit-identical.


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
- **A world's query** (`gen_indexed_query`; systems, match and local handlers, local-world queries, and borrowed live queries, each borrow variant its own site): ordinals are `archetype << 32 | row` (the table row, `chunk << shift | i`), in the order the plain loop goes. The site's facts are each admitted table's chunk count and, per chunk, its row count and its entity column, the presence columns the query's filters read and the lanes holding the keys (only those lanes: other lanes of the component can be written without the index noticing, as it doesn't read them), each page held. A candidate is checked again when visited (row below the table's count, not a hole, filters) and its bindings gathered with `tide_table_gather` from the table's current pages, as the plain loop reads them. Fewer than `TIDE_IX_LEAST` (32) rows go through the plain loop, columns read as they are: the body is written twice (both with the same GUI sites, `g->sites`), and one runs, picked by the tables' row count.
- **The cache** (`struct tide_index_cache`, a pointer at the end of `tide_world` and of `tide_local` in games with an indexed loop): one site (`tide_ix_site`) per loop and borrow variant, `tide_index_sites()` of them, defined at the end of the generated C once every loop is written. Derived: never copied (`tide_world_copy` leaves each world its own), hashed or packed (pack clears the pointer in the header; unpack clears it after its `memcpy`, after freeing the old cache); freed with the world. `tide_index_cache_prepare` runs serially at the start of each tick's run (`tide_world_tick_once`) and each frame (before `tide_local_hear`): it makes the cache, or makes it again when its ABI (`TIDE_INDEX_CACHE_ABI`) or site count differs (a reload), and counts the run (`epoch`). Each site's `plan` is a hash of what its code reads (archetypes, columns, lanes, key paths), so a reload that keeps the layout but changes a loop starts that site again. Outside a prepared run (no cache) a loop builds an index of its own each time.
- **Checking and building**: under the site's lock, the facts are compared as they come (`tide_ix_walk`); from the first that differs they're taken anew, pages held (a page whose count would wrap isn't held, and the site isn't trusted after: `full`). Pages are retained and released under one lock for every site (`tide_ix_refs`), as two sites can hold one page. A site whose facts hold searches its index outside the lock; one whose facts changed while other threads search its index builds one of its own for that search and marks the site not ready. Searches count themselves (`readers`).
- **A system's own loop** (`ix_fast`: a non-handler system's body, not borrowed, inlined, a task or a step): once its site is found to hold in a tick's run (`valid == epoch`), the rest of the system's entities search with no lock. While a system runs, only it could change what its loop reads, and the analysis rejects loops whose system changes the key field of its own entity, so it holds until the system is done. Without this, checking the facts under the lock for each of 10,000 entities made the benchmark's tick 22 ms rather than 9.5.
- **Rows made for the code** (`gen_indexed_rows`, `gen_ix_rows_search`): a view's `tide_q<n>` (and a borrowed query whose source is a view's) keeps its index at a site of the loop's own outside the worlds (`tide_ix_static`), its facts `tide_ix_made` (counted up each time the view runner makes rows, `gen_system_run`'s fill hook), the rows' address and count: an index lasts for a view's run, for all of its entities. A local handler's rows are a tick's record, made again for each run: an index of the loop's own each run. Fewer than 32 rows go through as they are.
- **A list** (`gen_indexed_list`), three ways: a field of a singleton a system or a view takes read-only (not local, not `verified`) keeps its index in the match's cache (systems) or the local state's (views), its facts the run (`epoch`), the world (views: shards draw several), the list's handle and count; nothing can change such a list while the code runs (the schedule; the code only reads it), so it lasts for the system's run, and for a view's frame. A list in a local nothing in the code writes after declaring it (`loop_index.list_local`, `unwritten_local` in indexing.c, from the code's writes as `loop_changes` tracks them) keeps its index at a site of the loop's own (`tide_ix_static`), its facts the declaration's stamp (`tide_ix_call`, a number counted up under a lock as each run of the declaration's statement gets one, `gen_ix_stamp` right after it), the handle and the count: so an inner loop over it reuses the index for one call, and the next call, whose list may be at the same scratch place, makes it again. Any other list (a component's, a `mut` singleton's, a local singleton's, a parameter's, a local the code writes, anything in a task) gets an index of the loop's own at each run. Below 32 elements every element is gone through. Element order is the list's.
- **Parallel loops**: over a world's query (`gen_parallel_query_call`), the index's candidates are gathered into the rows array the steps share out (`gen_indexed_query` with the row type; below 32 rows the plain gathering). Over rows made for the code (a view's, a local handler's), the candidate rows are copied into an array of the loop's own (`gen_ix_parallel_rows`), freed after the steps. Over a list, before the fork, an index of the list as it is (the loop's own: its steps change elements) marks the elements it gives (`tide_xm`, a bit each); a step for an unmarked element in the list does nothing, as its guard would have skipped it (`gen_par_nest`); out-of-range steps run as before. The four-lane batch path is off for indexed list loops.
- **The promise**: every loop the analysis plans goes through an index. `gen_stmt` checks after writing each loop (`ix_check`, against `ix_used`, which every indexed path marks) and refuses the game with an internal error if a planned loop came out plain; every game in the build (tests, examples, benchmarks, prototypes) builds with it. Loops whose body waits (`await`, in async code) get no plan: `index_why` says the search can't be kept across a wait, so `indexed` refuses them.
- **Memory safety**: keys may be NaN, infinite or huge (the index puts those in every search); a candidate is bounds-checked against the table when visited; hits live in a `tide_index_hits` freed however the loop is left (a `gen_open` cleanup, so `return` frees it); a restored world has other pages, so its sites rebuild; held pages keep a snapshot's pages alive until the site lets go (rebuild, world free).
- **Tests**: `tidec_e2e_indexed_loops` (`compiler/tests/e2e/indexed_loops.tide`): 3D distance guards over static walls (index kept across ticks) and moving movers (rebuilt every tick), a squared distance through a helper, an enum equality, a one-sided bound, an `||` band, a bool, lists of float3 and of structs in a singleton, a view's query, a query borrowed by a function from a system and from a view, NaN and infinite places, entities spawned and destroyed every few ticks, a parallel loop over a query and one over a list, a local handler's rows (the test ticks with `tide_world_tick_events` so they're heard) and a parallel loop over them, a parallel loop over a view's rows, a component's list, a `mut` singleton's list, a local singleton's list in a view, a local list kept across an inner loop's runs (one call a tick, so the next tick's list is where the last one was: a mutation dropping the stamp from its facts fails the comparison), and a local list written between the inner loop's runs. `tidec_e2e_indexed_loops_plain` is the same game built `NO_LOOP_INDEXES`; `tidec_indexed_loops_same` (`indexed_loops_same.cmake`) runs both and compares their worlds' hashes and what the views drew, tick by tick, for 80 ticks. Each also checks that rolling back to a snapshot gives the same ticks again, that a world copy and a packed and unpacked world tick as the original does, and (native) that 4 threads give what one does. A mutation narrowing every bound by 0.25 fails the comparison. Pass under Windows ASan (`sanitize`) and WASI (`web-debug`).
- **Benchmark** (`bench/loops`, `bench_loops` and `bench_loops_plain`; release, Windows, Ryzen 9 7900X, one thread; the tick doesn't take threads at these sizes, as the scheduler's estimate counts a query loop as one step): 10,000 boids each counting the boids within 2 (a query, every boid moving every tick, so the index is built again each tick), and 10,000 agents each looking for obstacles within 2 in a list of 10,000 that never changes.

| Per tick | Plain | Indexed |
|---|---|---|
| boids and agents, 10,000 each | 341 ms | 9.3 ms |
| 10,000 boids alone | 86 ms | 7.6 ms |
| 10,000 agents alone | 273 ms | 1.8 ms |
| 1,000 of each | 2.95 ms | 0.72 ms |
| 200 of each | 129 µs | 28 µs |
| 50 of each | 9.1 µs | 6.4 µs |
| 20 of each | 1.48 µs | 1.42 µs |

  At 20 the loops are below `TIDE_IX_LEAST` and run the plain loop (before, they listed the rows and gathered each: 5.4 µs a tick). The boids and agents alone were measured before that change, which leaves 10,000 as it was.
- **No index for one run alone (2026-11-13).** A list without a proof it's unchanged (a component's, a `mut` or
  local singleton's, a parameter's, a local the code writes, anything in a task) and a local handler's rows used to
  get an index of the loop's own at each run: every element read for its keys, a sort, a search, and the index's
  memory taken and given back, where the plain loop is one pass that tests the same keys. Found in Mount & Rifle
  (`Infantry.Eyepiece`, two loops over a part's vertices in the `HeldWeapon` view: `tide_realloc` under
  `grow_entries` and `build_sorted` were the frame thread's largest single cost in a `tide_sample --stacks` run;
  25 such loops in the game). Now `plan_loop` (`indexing.c`, `list_kept`) plans none for such a list and says why
  (`not indexed: its list could change between one run of the loop and the next...`, in `tide schedule` and the
  editors), and codegen writes the plain loop where only its context rules a kept index out (inside a task, a
  routine, a parallel loop's step: `gen_indexed_list`) and for a local handler's rows (`gen_indexed_rows`).
  `indexed foreach` still gets its index, each run: the code asked. Measured on Mount & Rifle's battle, three
  runs each alternating on a loaded machine (everything about twice its quiet time): `HeldWeapon` 0.73 to 0.49 ms
  a frame, the other views within their spread; `Campaign.Roam`, three such loops, 0.52 to 0.49.
- **Gaps**: A local list's site is shared by the threads running its code, so per-entity code with a local list on several threads rebuilds it more often than it reuses it (correct, slower). A loop in async code whose body waits isn't indexed.
