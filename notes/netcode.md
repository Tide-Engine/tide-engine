# Netcode: sessions, prediction, commands

## Whole projections wait their turn; a world sent unasked is no longer stopped before it goes; no protocol change (2026-11-20)

A review of the repairs' allowance (`REPAIR_UPDATE_MOST`, 4 MiB of what repairs by what changed read an update):
it counts those reads only. A repair that comes to the player's world again (`REPAIR_SNAPSHOT`) or to the hash offer
(`REPAIR_OFFER`) was counted as nothing and then read a whole projection, and a player's world asked for in a packet
(`start_snapshot`) and what one lacks of a hash offer (`send_client_lacking`) were read as the packet was. All in
`engine/src/session.c` (agent-maintained). **Nothing on the wire changed** (`PROTOCOL` as it was), nor any world or
hash.

**Measured** (`bench_voxels --players 100 --spread 256 --join-every 0.1 --seconds 20 --threads 12 --outage 4.5`: the
bench's new `--outage`, bench/README.md; Windows release, 12 threads, BelowNormal, the machine shared; timers put in
for the runs and taken out). Every bot is cut off for 4.5 s, past the 4 s of ticks the server keeps, and asks for a
repair as it's back: its base is gone, so each is its world again, in the update's sending, one after another. 50
of them in one update took 152.7 ms of its 162.4 (3.0 to 3.6 ms each: a projection of about 0.9 MB, 94 KB as
sent), 22 in a later one 82 of 95. And 13 of the 100 bots never had the server's world again (below).

| The outage, all 100 cut off | Longest update (ms) | p99 (ms) | Back after it: median, at most (s) | Never back |
|---|---:|---:|---:|---:|
| Before (two runs) | 152.0, 162.4 | 28.8, 32.6 | 0.28, 12.42 (of 87) | 13 |
| The transfer fix alone, no allowance | 146.2 | 32.0 | 0.30, 0.70 | 0 |
| An allowance of 4 MiB (built) | 32.7 | 29.2 | 0.38, 0.75 | 0 |
| ...of 2 MiB | 34.9 | 27.6 | 0.52, 0.95 | 0 |
| ...of 1 MiB | 25.8 | 22.8 | 0.67, 1.25 | 0 |

The updates are those from the outage's start until every bot is back; "back" is a verified world within half a
second of the server's, counted from the outage's end. With 20 of the 100 cut off (`--outage-bots 20`): longest
update 45.5 ms to 23.6, back 0.28 s at the median and 0.50 at most to 0.32 and 0.48; the 80 who played on were 49
ticks behind at most without the allowance and 45 with it, 25 or fewer in 99 frames of 100 both ways. One run each
but the first row; the p99 column is within the machine's noise, the longest update isn't.

- **A whole projection waits its turn** (`want_whole`, `prepare_wanted`, `connection.whole_wanted`,
  `WHOLE_UPDATE_MOST`). A player's world again, a repair by the hash offer and what a player lacks of one are asked
  for and kept on the connection (which, since when, the offer's tick, the mask of what's lacking), and an update
  prepares them, the one that has waited longest first, until it has read `WHOLE_UPDATE_MOST` (4 MiB, the repairs'
  line) of projections' bytes (`tide_server.whole_work`: what `client_state` gave, of the world and of the base
  where one is compared; a world packed whole for a game without projections counts as packed, once where players
  share the pack). The first of an update always goes. It's an allowance of its own beside the repairs': an update
  reads at most one of each, and neither keeps the other waiting.
- **Prepared where it was** (`tide_server_update`): after the update's packets and timeouts (a world a packet asked
  for, what one lacks), after its repairs are read and packed (`repair_wants_whole`: those that came to a world
  again or to the offer), and after its sending (a player `send_frames` found too far behind). So one within the
  allowance is read from the same world, at the same point, and goes in the same datagrams in the same update as
  before; only what's past the allowance moves, by whole updates. A repair by the offer that waited gathers its
  ends again as it's prepared (`repair_ends`, which `repair_read` uses too): the tick it was to, unless the player
  has it since or something else is on its way; its world again where the ends aren't kept any more.
- **While it waits**: a player whose world waits is sent a `WELCOME` naming no transfer each update (what a joiner
  gets until it writes back: "nothing yet") and no ticks, which it couldn't run; no repair is started for it
  (the due list leaves it out, as one being made) and none is taken from its packets (`on_client`: as one being
  sent). A put-off repair isn't sent over a world that waits. `stop_sending` lets go of what waits, with the mask.
  A hot reload still starts its players' worlds again at once (`start_snapshot`, counted in the update's work), one
  that waited among them.
- **A world sent unasked isn't stopped by what the player wrote before it heard of it** (`connection.sending_world`,
  `welcomed_number`, `welcomed_said`, `welcome_heard`; `send_welcome`, `on_client`). A `CLIENT` that wants nothing
  stops a transfer ("it has the world, or one that needs nothing"). A player too far behind for the ticks kept is
  sent its world without having asked, and went on saying it wanted nothing, as it had all along, until the
  `WELCOME` reached it: the server stopped the transfer on the next `CLIENT`, before any datagram of it had gone
  (it starts in the update's sending, and the next update reads packets first), having already moved the player's
  acknowledged tick to the world's. The player then had neither the world nor the ticks before it, asked for
  nothing, and the same happened every 4 s for good: the 13 bots above, each one that had loaded an old repair
  just as it came back and so asked for none. Now, for a player's world again, such a `CLIENT` stops the transfer
  only once one of its `CLIENT`s has echoed the time of a `WELCOME` naming the transfer, or a later one (the echo
  is the last time it heard the server say: `their_time`). Repairs, offers and patches stop as before: the player
  asked for those. A player on the server's machine names the transfer whole and stops it as before.

**Why everything else is the same.** Which projections an update prepares goes by their order of asking and the
bytes read, never by a clock or a thread. `bench_voxels` at 100 apart and at 8 bots ends on the pins of 2026-11-19
(hash `0x5165130e825fa70f` and what the bots took `0xb8fa79ab9a74ff3d`; `0x1fdc545078ce68e8` and
`0xb4e8a59de2f669db`): joins half a second apart never meet the allowance. Server updates in that 100-apart run:
7.41 ms median, 22.32 p99 (7.77 and 26.48 on 2026-11-19; one run, the machine shared).

**Found on the way**: with the worlds waiting, a bot's repair request that came during the wait was taken (its
acknowledged tick hadn't moved yet), outlived the world it was sent, and became a repair nobody asked for, which
the bot refused and paid for with its world again (3 to 9 of 100 in the first runs with an allowance; the 2 MiB
row is from before this was closed). Hence no request is taken while one waits.

**Tests.** `tests/test_visibility_session.c`:
`visibility_a_player_too_far_behind_takes_the_world_it_never_asked_for` (a match nothing is repaired in, 50 ms each
way, one player hearing nothing from 3 s to 7.5 s and sending all along: its world again once, verified within 30
ticks a second and a half after; with the old rule it's still at tick 176 then) and
`visibility_whole_projections_wait_their_turn_past_an_updates_allowance` (projections of 5 MiB: two players joining
in one update, and both repaired by the hash offer at tick 90, are never read for in the same update, the second a
single update after the first; without the allowance it fails). `bench_voxels_outage_1` and `_12`
(bench/CMakeLists.txt): 16 players cut off for 4.5 s, each sent a megabyte's projection again, all back, the same
world and the same taken on 1 and 12 threads; a run where a bot never is back fails.

Run on Windows, no pin moved: release, `tide_tests` (all) and the 29 fixtures listed in notes/voxels.md, Tests; ASan
(`sanitize`), `tide_tests visibility` (33) and `session` (24), `bench_voxels`, `_views`, `_reads_12`, `_outage_1`,
`_12`, `tidec_e2e_net`, `_net_join_big`, `tidec_e2e_visibility_host_lifecycle` and `_repairs`; the web
(`web-debug`, under Node), `tide_tests`, `bench_voxels`, `_reads_1`, `_outage_1` and `_12` (on the native pins),
`tidec_e2e_net` and `tidec_e2e_visibility_repairs`. Each new engine test fails with its change out: the old stop
rule back (the first), `WHOLE_UPDATE_MOST` out of reach (the second). The full suites and the other platforms are
CI's.

**Left.**
- The line is the owner's to move, as the repairs' is: 4 MiB is four or five such worlds an update (10 to 15 ms of
  it here) and costs a player 0.05 to 0.08 s; 1 MiB halves the longest update's share and doubles the wait.
- The projections are prepared one after another on the update's thread (each finds its entities on the pool).
  Several at once on the pool's threads, as repairs are read, would shorten both columns: `client_state` from
  several threads at once isn't something the generated code has been checked for.
- Voxels never reached the hash offer or what one lacks in these runs (no calls): they wait their turn by the same
  rule, which the engine test above covers, with no timing of their own.
- A game without projections whose player is sent a world of a tick it has already run past (`tick > ahead` is
  what it takes one by) still refuses it; only projections are taken whenever the server starts one.
- Nothing bounds a hot reload's worlds: every transfer starts again in the update that commits it, as before.

## Projection journals reuse the completed tick's hash (2026-11-19)

`tick_run` computes the authoritative whole-world hash after simulation. The
session already uses that hash for the frame and its replay record. The
generated projection check used to fold the whole world again to name the
same state in the repair journal. `tick_end` now passes the local hash to
`client_hashes` together with that world. Each recipient still gets a
projection computed from its permitted state every tick. The journal uses
the supplied hash as its node key and computes the key itself for callers
that pass NULL.

The hash covers the exact post-tick state. Between hashing and projection
checking, session work reads the world, updates connection and store state,
and copies to other worlds. The hash pointer is valid only for the call.
The journal keeps the value. `TIDE_CLIENT_SPANS_CHECK` independently recomputes
the key and checks recipient hashes from the rows. The walk fixture compares
repairs and the journal digest with and without a supplied key. The same
wire protocol and snapshot format apply.

See [Voxels at 100 spread bots](voxels.md#projection-journal-cost-at-100-spread-bots-2026-11-19)
for the workload, measurements and remaining tick costs.

## A whole projection's entities found on the pool; no protocol change (2026-11-17, joins)

What a player is sent of the world whole (a join, a transfer, a repair offer: generated `client_state`) asked
`tide_visibility_entity` of every entity of every archetype twice, once to count and once to write, each a lookup of
the entity and a walk to its scene and groups, on the update's thread. A server's world is every player's: at 100
players apart, 30,000 or more entities of which a joiner has a few hundred, and the updates where several joined
were the run's longest (`server, while one joins` p99 28 to 46 ms).

- **Once each, where it is**: `tide_client_see_keys` goes through the archetype's entities in the order of their
  identities with their slots (`tide_entities_ordered_archetypes_slots`), asks `tide_visibility_entity_at` (the
  same answer as `tide_visibility_entity` for a live entity at a known place, with a `tide_visibility_walk` keeping
  the scene's root and the directory found for the rows that share them) and keeps the places of those the
  recipient may have, which are then written. A client's world (it has only what it may) asks as before.
- **On the pool**: where the session handed this thread a pool (`tide_loops_enter`, which `start_snapshot`,
  `offer_client_repair` and `send_client_lacking` now do around the whole projection, on the update's thread; never
  from the repairs read on the pool) and there are 4,096 entities or more, parts of 2,048 run on its threads, each
  reading the world alone and writing its own flags, kept in order after. The bytes are the same however many
  threads: every run below ends on the same world hash, bytes per player and arrival ticks.
- **Measured** (`bench_voxels --players 100 --spread 256 --seconds 30`, 12 threads, two alternating pairs, the
  machine shared): updates while one joins, p99 28.5 and 46.1 ms to 14.1 and 15.6; the run's p99 21.2 and 27.7 to
  24.2 and 27.1 (the steady state, within the machine's noise); 27.8 KB a second for each bot, box fill 31 ticks at
  the median, hash `0x5165130e825fa70f` in all four.
- **Tests**: every native release target builds; the `bench_*`, `tidec_e2e_visibility*`, `_net*`, `_storage*`,
  `_local_fields`/`_events`, codegen pattern tests, `sumo_tests`, `earth_tests`, `towers_tests` and `tide_tests`
  (109) pass; under ASan (`sanitize`) `bench_voxels` (quick, `_arrives`, `_views`, `_reads_12`), the visibility
  runs, repairs and walks-on-the-pool fixtures, and 12 players apart joining. No pin moved.

## A check's rows on every thread; no protocol change (2026-11-17, parts)

What was left of the projection checks at 100 players apart (notes/voxels.md, A hundred apart, again): a check after
the Chunk table settled read 600 to 800 pieces on one thread (20 to 31 ms), 25 of the run's 30 longest updates; and
of every check, who may have what (audiences) and each player's sums were one thread's too (55 and 45 ms a second of
the server's 245 of hashing). Generated code (`client_spans_codegen.inc`, `client_state_codegen.inc`) and one line of
the session; **nothing on the wire, in a world or in any hash changed** (`PROTOCOL` stays 27; no pin moved).

- **The rows' part in parts** (`tide_client_part`): every table chunk's pieces, `TIDE_CLIENT_PART_PIECES` (4) at a
  time, each a call of `tide_client_rows_chunk<a>(..., chunk, moved, from, to, ...)`, the loop body that was inline.
  Where the check has `TIDE_CLIENT_SPLIT_ROWS` (4,096) rows or more, the memo and the walk, and a pool was handed to its
  thread, the parts run on the pool's threads (`tide_parallel_for`, a frame's loop) and are then taken in their order
  (`tide_client_parts_take`); else each table chunk is one call here, in order, as before. Before the parts: room for
  every chunk's pieces (`tide_client_span_pieces`, which grows the array they're in) and which of each chunk's columns
  were written since (`tide_client_span_written`, which hashes the columns' pages), so a part hashes no page; after
  each chunk's parts, its columns kept (`tide_client_span_kept`) here.
- **The memo in stages** (`tide_client_stage`, `tide_client_hasher.stage`): a part's hasher reads the memo and never
  writes it. An entry it uses is copied into its stage the first time (or made there: a put's, a big container's
  sums'), and is then changed there exactly as the memo's own would be (`tide_client_memo_get`, `_put`, `_sums` take
  the stage's entry where the hasher has one); each put and each sums call is noted, in order, as the memo grows by
  them (it rehashes before an insert past half its room). Taken in order (`tide_client_stage_merge`), a stage's calls
  grow the memo as one thread's would, and each entry the part used becomes the part's, the memo's `kept` counted once
  for each entry first used this call.
- **The journal in lists**: what a part notes (`tide_client_noted`, the pending rows of `tide_client_journal_row`) and,
  later, the rows whose audiences moved (`tide_client_added`, `tide_client_journal_add`'s) go into the journal's lists
  in the parts' order, then each table's rows past its end, as the serial order had them.
- **Who may have what, and each piece's routing, in parts** (`tide_client_parts_seen`): once the rows' parts are
  taken and the directory of groups compared (here), the same parts work out their pieces' audiences
  (`tide_client_seen_chunk<a>`, each with its own `tide_visibility_walk`) and then route them
  (`tide_client_route_chunk<a>`: what a piece's rows add to each player's sum), with routing scratch from a list of
  spares, one for each part at a time. The players' sums are then added up here from every piece's route (wrapping
  sums of what each piece kept: the same in any order, and they're added in the same order anyway).
- **The session** (`tick_end`, agent-maintained) hands its tick's pool to the thread around `client_hashes`
  (`tide_loops_enter(t->jobs)`, `tide_loops_leave`): runtime-ownership.md, top section.

**Why everything is one thread's.** A part reads the world (written by nobody during a check), the state kept by
the check before for its own pieces and rows, the walk (made here before the parts, and only read by them) and the
memo as the check began (busy, so no repair shares it, and not written until the parts are done); it writes only its
own pieces, their rows' kept arrays, its noted and added lists, its stage and its scratch. So what each part comes to
is what it came to read in turn on one thread: the only state one part's work could see of another's is the memo's
entries, and an entry is a container's (its handle), which a row's value holds. An entry used by one part's rows is
the memo's exactly as one thread leaves it. Were one handle read by two parts, the later part's copy would go into
the memo last, made from the memo as the check began rather than from the earlier part's: it holds whenever the
earlier one's would (its hashes are of the same bytes), and an entry is only ever a shortcut to them, so every hash,
the journal and what's sent are the same either way; only that entry's kept items could be another's.
Every page a part reads was hashed before it (the world's hash after the tick, the walk's on every heap and slots'
page), so `tide_page_hash` and `tide_client_page_hash` only read: no page's hash, `before` or lines written is
written by a part. `TIDE_CLIENT_MEMO_OFF` (no walk) and a check worked out again (`again`) never go in parts.

**Threads, checked.** Each part's state is the part's from its start to its end, `tide_parallel_for` returning once
every part has (the caller's thread takes parts as thread 0); the main hasher, the memo, the journal and every span's
columns are touched only before and after. The routing spares are under a lock. ASan (`sanitize`): `tide_tests`
`session`, `visibility`, the visibility fixtures and `tidec_e2e_visibility_walks_pool`. No ThreadSanitizer here. On the
web (`web-debug`) the fixtures' `_checked` variants, in parts, end on the same pins as natively.

**Tests.** `visibility_walks_checks_on_the_pool_are_one_threads` (`tidec_e2e_visibility_walks_pool`: every check in
parts of a piece, on the platform's pool, with every hash also worked out from the rows): two worlds through every
step of `visibility_walks_see_every_page_written_since`, one checked on one thread and one on the pool; after every
check each player's hash, each repair's bytes from the journal and `tide_client_check_digest` (a test build's hash of
the memo's entries in their containers' order and of every kept row, piece, walk and journal entry) are the same.
The `_checked` variants of the runs, pieces, blocks, walks, trees and repairs fixtures read every check in parts of a
piece here in order (`TIDE_CLIENT_PARTS_ALWAYS`), every hash worked out again from the rows, with the same pins.
Mutations, each alone: every other part's stage left out of the memo (the pool test fails on the digest, every hash
the same); a table chunk's noted rows taken from its first part alone (the pool test fails, at the first check whose
rows another part wrote down); its rows whose audiences moved taken from its first part alone (walks never moves an
audience, so the pool test passes; `tidec_e2e_visibility_repairs_checked`, in parts, fails: a repair from the rows
written down isn't one from every row).

**Measured** (notes/voxels.md, A check's rows on every thread): 100 apart, the checks 243 to 256 ms a second to 112
and p99 35 to 22 ms; 100 together 52 to 28.5 and p99 8.5 to 7.1; 32 apart 54.5 to 30.6 and p99 9.0 to 6.1; 8 bots and
one thread as they were. The same world hash, what the bots' machines took, bytes and box arrival ticks in every pair.

**Left.**
- What a check does on its own thread: the walk (every heap and slots' page's hash, 3 ms a second at 100 apart), the
  directory of groups compared (6), each chunk's columns looked at before and kept after, the stages and lists taken
  (8 with the rest), the journal's pending rows, the world's hash for the journal (6) and the players' sums (5). A
  walk by heaps on the pool, and the directory by its chunks, are what's next of it.
- A row a settle moved still has everything of it worked out again where it lands (its kept arrays are by row): the
  moves aren't said to the check. With the parts on the pool that's no longer an update's length (the checks after a
  settle are 5 to 13 ms where they were 20 to 31), only the threads' time.
- A piece a page of whose rows changed is read whole (its arrays made again), where few of its rows changed.
- The parts' scratch (their hashers, gathered lists and stages' tables) is fresh memory each check: a few hundred
  small allocations.
- Checks for several players at once is what this is: a check is every player's at once, and what's one player's of
  it (the sums of the pieces' routes and the inputs) is a few microseconds.

## The directory of groups, packed, is compared by its entries' orders; no protocol change (2026-11-17, a hundred apart)

The projection checks compare the directory of visibility groups (`tide_visibility_state.groups`, a map) with the copy
they kept, entry by entry where they are (`client_spans_codegen.inc`'s `tide_client_groups_diff`). A map packs its
live entries together once it has more dead than live (`map.c`'s `compact`, on an insert): every entry after the
first dead one moved, and each was taken as a group that changed and came to all its audience. At 100 players apart
(80,000 groups, a chunk's each, half of them dead every so often) that check was the run's longest: 11 to 12 ms
comparing the directory and 20 ms working every row's audience out again, and every player whose group moved was
said to have had entities come to them (`tide_game.client_came`), so their next repair was one of a view arriving.
Now a list that isn't the one kept (it packed, or it's small) goes by its entries' orders, which an entry keeps
wherever it is and which go up along the list, dead ones too: an order in both is compared, one in either alone came
or went. What's taken as changed is what changed; were orders ever out of order, it would be more than that, never
less (an entry met in one list alone is taken as changed). The list kept in chunks and still the one kept goes place by place,
as before. Generated code only; nothing on the wire changed (`PROTOCOL` stays 27), and a player's repairs and what's
said to have come to them differ only after a pack, where they were more than was true.

**Tests.** `visibility_came_says_whom_a_groups_audience_gained` (`tidec_e2e_visibility_repairs` and its variants):
three quarters of the groups destroyed, then one made, which packs the directory: it came to its own player alone;
with the place-by-place comparison back it fails (the groups that moved came to theirs). `bench_voxels_checked` (every
hash worked out from the rows too) for 15 s at 100 players apart passes.

## Repairs read on every thread; no protocol change (2026-11-17)

What was left of the hundred players (the section below, Left): an update's 20 to 32 repairs were read one after
another on the update's thread (10 to 12 ms), as `client_changes` took the match's memo for itself (`busy`), and a
second reader at once would have had another memo with nothing kept. Three changes (`engine/src/session.c`,
`recipient_delta.c`, agent-maintained; tidec's `client_state_codegen.inc`, `client_changes_codegen.inc`). **Nothing on
the wire changed** (`PROTOCOL` stays 27), nor any world or hash.

- **A repair's read writes nothing another reads** (generated). `client_changes` takes the match's memo with
  `tide_client_memo_share`, which any number of threads hold at once (`tide_client_memo.readers`; none while a check
  hashes with it, `busy`, and none is taken from its world or cleared while one holds it), and hashes with it read
  and never written (`tide_client_hasher.shared`, `reading`): `tide_client_memo_get` finds an entry and checks its
  blocks and pages by `tide_client_item_holds`, which writes nothing, not even the entry's page hashes, and works a
  page's hash out where it isn't kept without keeping it; `tide_client_memo_put` keeps nothing and
  `tide_client_memo_sums` gives none. The journal and the kept rows were only read already. So where a repair's base
  rows used to put their containers' hashes into the memo (over the current world's, which the next check then hashed
  again), now nothing is put; a read is what it was otherwise. What a read writes: its own lists, hasher and writers,
  the thread's heaps for text (`tide_text_use`, thread-local) and, under a lock, the frozen worlds' hashes and the
  session's rooms. Every page it reads is hashed already (a tick's world is hashed as it ends, `hash_of`; a kept
  tick's and a confirmed copy's pages are those of a world that was), so `tide_page_hash` only reads.
  `tide_client_changes_stats` (a test's) is counted with atomics.
- **An update reads its repairs on the pool and starts them in the connections' order** (`repairs_read`,
  `repairs_read_work`, `repair_take`, `repair_drop`). The players whose repair is due are listed in the connections'
  order (`repair_start` with its `c` and `tick`, nothing of the connection changed yet); with a pool of two threads or
  more (`tide_jobs.run`) and two or more due, every thread takes them and reads each (`repair_read`, which now changes
  nothing but its `repair_start` and the rooms: what it reads counts in `work`, and a big one says it's to be put off,
  `put_off`) and packs it (`repair_pack`). Then the update starts them in order exactly as one thread did: each while
  the update's reads are under `REPAIR_UPDATE_MOST` (its pending cleared, its stream's tick set, its `work` counted,
  put off if it said so); the rest are let go and wait for the next update, as before. What's sent goes by the bytes
  read, in the connections' order, never by which thread read what or when. Threads read ahead of knowing where the
  allowance ends (each read is a function of the worlds and its connection alone, so reading one that's then let go
  changes nothing); they stop taking reads past it once the reads before it are in (`stop`, `settled`), or once those
  before it read so far already make the allowance, so a crowd of views arriving at once reads a few too many, not
  all of them. They take views arriving first (`connection.came_at`), the longest to read, so the update doesn't wait
  for one begun last. With one thread, or one repair due, it's read, started and packed where it is, as before.
- **A pack's index of spans in kept room** (`tide_recipient_delta_pack_with`, `TIDE_RECIPIENT_INDEX_BYTES`): each pack
  that looks for spans took a 512 KiB index fresh from the system (`calloc`, past the heap's size for its own pages on
  Windows: mapped, faulted, zeroed and unmapped each time), and packs on twelve threads waited on each other for the
  address space: with the reads on the pool, 29 repairs' reads took 82 to 122 ms of threads' time where one thread took
  10. The session gives it room from its rooms (`client_changes_pack`, `read_rooms`), cleared for each pack: the same
  bytes. 29 reads then take 10 to 20 ms of threads' time, 4 to 6 ms of the update's.

**Why every datagram is the same**: which repairs an update starts, and what each is, is what one thread reading them
in turn made of them: the reads are functions of the server's worlds and each player's connection (nothing a read
writes is read by another, and nothing of a connection changes until the update starts its repair), and the update
starts them in the connections' order by the bytes each read. `bench_voxels_reads_1`, `_4` and `_12` (16 players joining
and hopping together every second at twice the view, the allowance met several times) pin what the bots' machines
took of what they were sent, each with the ticks the machine had played then (`--expect-taken`, bench/bench.c), the
same on 1, 4 and 12 threads; with the allowance left out of the update's choice where it reads on the pool, `_12`
fails (`0xf5c53a516c89022e`), its world's hash as it was. `bench_voxels_views` pins it too. `bench_voxels` at 8, 64 and
100 together, 100 apart and on one thread ends on the base's hash, bytes a bot and box arrival ticks in every pair
(notes/voxels.md, Repairs read on every thread).

**Threads, checked**: the read's state is listed above; the session's `repair_start` of each is one thread's from its
taking until `done` (`repair_reads.done`, `works`, released; the update reads them after `run` returns). ASan
(`sanitize`): `tide_tests session`, `visibility`, `page`, `tidec_e2e_visibility_*` and `bench_voxels_reads_*`
(notes/voxels.md, Tests). There's no ThreadSanitizer on this machine; Linux CI's UBSan runs the same.

**Measured** (notes/voxels.md, Repairs read on every thread): 100 together's 99th percentile 15 to 12.2, 12.5 ms on 12
threads, 64 together's 9.6, 10.0 to 8.6, 9.2; 8 bots and one thread as they were; the same bytes and ticks.

**Suspected of the handover's flake, and cleared** (2026-11-17; notes/webrtc-and-rooms.md, top section): CI's
`tide_platform_web_rooms_handover` failed (a joiner never welcomed) on the commit after this one and again later, but
that game has no `client_changes` and its session no pool, so none of this runs there, and the same failure was seen
on 2026-11-02. Checked too, by reading: the start loop keeps the serial rule, so the first due repair in the
connections' order always starts whatever it read (`repair_work` is 0 then), each read past the allowance is one the
serial loop wouldn't have begun in this update either, and a joiner's whole view, put off as `REPAIR_HEAVY`, leaves
the due list (`making`) until it's sent. Nothing moved; no pin changed.

**Left.**
- An update's longest read is now one big repair (a view arriving, 2 to 3 MB: read and packed in 4 to 7 ms, the view
  as it is now and the tick asked for one after the other on one thread). The two as two tasks was tried and wasn't
  faster (notes/voxels.md); a view's records written by ranges is what's left of it.
- The frozen worlds the reads compare are hashed before them on the update's thread (each kept tick's the first time
  it's asked for: 25 to 30 ms a second 100 apart, under 6 together), so no two threads work one out at once; on the
  pool, the distinct ones as tasks of their own, they'd be the threads'.
- The base's rows are still hashed again for every repair whose base they are; with the reads on the pool it's no
  longer the update's length.
- Packing's output and the envelope are still fresh memory (small: a repair's bytes as sent).
## A repair's read writes into room a read had before; no protocol change (2026-11-17)

What was left of the hundred players (the section below, Left; notes/voxels.md, A hundred players): an update's 20 to
32 repairs read on one thread, 15 to 32 ms. **Where the time was** (`bench_voxels --players 100`, 12 threads, timers
put into `client_changes`' generated C and taken out; cycles over a 15 s run's 17,000 reads): writing each entity's
record 76% (`now` 51, `was` 25), the base's rows looked at (`tide_client_see(b, ...)`) 14%, finding the two worlds in
the journal 7%. A record was about 8,000 cycles for 570 bytes, and it wasn't the encoding: **writing into fresh
memory was**. `now` and `was` (86 and 30 KB a read on average, megabytes for a view) were `malloc`ed and grown each
read and `free`d after packing on the pool, so each read's pages came from the system and faulted at their first write
(0.8 M page faults in the reads of that run, one for nearly every 4 KiB written), each far dearer in a busy process of
25 threads than in a test of one. Copying a kept record cost as much as reading it again (3,000 cycles for 200 bytes).

- **The session keeps the room** (`session.c`, `read_room`, `tide_server.read_rooms`): a repair's plan, `now` and
  `was` are written into writers taken from the room of repairs already packed (handed back in `free_repair_made`
  from whichever thread packed them, under a lock), at most 64 rooms and 64 MiB a server, let go of with it. Which
  room a read gets has no part in what it writes.
- **`tide_game.client_changes` writes into the caller's writers** (tide/session.h; runtime-ownership.md, top section,
  for the owner to confirm): each from its start, growing as needed; it returns whether it made one. Generated code
  (`client_changes_codegen.inc`) writes the same bytes it wrote into writers of its own.

**Why the datagrams are the same**: the bytes written are a function of the two worlds and the recipient alone, as
before; only where they're written moved. `bench_voxels` at 8, 64 and 100 together, 100 apart and on one thread ends
on the base's hash, bytes a bot and box arrival ticks in every pair (notes/voxels.md). `tidec_e2e_visibility_repairs`
(and `_checked`, `_memo_off`, `_wide`) works every repair from the rows written down into fresh writers and every walked
one into writers used again, bigger or smaller and holding the last repair's bytes, and fails on a byte's difference.

**Tried and taken out**: an update's records shared between its repairs (a cache by world, entity and which of its
sparse components the recipient may have, through a hook the session began and ended around its reads): 60% of
records came from it, but with fresh memory a copy cost what a read did, and with kept room the reads were no faster
with it (8.8 G cycles of records without, 9.6 to 9.8 with: working out the recipient's components for the key cost
what it saved). Not kept, so no new hook.

**Left.**
- ~~The reads are still one thread's (`client_changes` takes the match's memo): an update of 29 repairs reads in 10 to
  12 ms where it was 32 (notes/voxels.md).~~ Read on the pool since (Repairs read on every thread, above).
- A record is still about 2,500 cycles; the base's rows are hashed again for each repair whose base they are (a
  quarter of a read now): their terms are the recipient's only by the audience, so a kept tick's could be kept once.
- The pool's packing still writes into fresh memory (`tide_recipient_delta_pack`'s output and the envelope), off the
  reading thread; the update waits for it at the end of its reads. (Its index of spans is kept room since, and a
  repair read on the pool is packed by the thread that read it: above.)

## A kept tick's world let go of on the pool; a destruction journaled once for every player (2026-11-17)

What a server's tick costs where chunks come and go every tick (notes/voxels.md, The spread tick, has the
measurements). Two changes; **nothing on the wire or in a world changed** (`PROTOCOL` stays 27; every pin as it was).

- **A kept tick's world is let go of on the pool's background threads** (`session.c`'s `let_go`, `let_go_settle`;
  `tide_server.letting_go`): the world the ring no longer holds was let go of in `tick_end`, a pass over every page
  it has (at 100 players apart, 80,000 chunks: 0.6 to 1.8 M cycles each, its heaps' pages half of it, the entity
  table's a quarter, the tables' a fifth). It's handed to the pool as a helper where the pool has two threads or
  more (as a repair's packing is), and goes on beside the players' checks, the repairs and the next tick. One at a
  time: the next `tick_end` waits for it, helping, as do `tide_server_destroy` and a hot reload's `commit_server`
  (the old build's code lets it go). Safe because page references are atomic (runtime-ownership.md, top section):
  the world shares its pages with the server's world and the kept worlds after it, which are read, copied and
  written meanwhile, and only its references are touched; what it has alone (its tables' and heaps' own arrays, the
  memory of the world) is nobody else's; and its address isn't another world's until it's freed, so nothing kept by
  a world's address (the projections' memos, frozen worlds' hashes, both under their locks) is taken for it.
- **A destruction is journaled for each player who sees it from one audience** (`codegen.c`'s
  `tide_client_destroyed` and a lifecycle event's `tide_hear_*`; `tide_visibility_seers_of`, `tide_visibility_seer`
  in `visibility_codegen.inc`): a server journaling its players' local handlers asked `tide_visibility_entity` for
  each of the game's players, a walk to the entity, its scene and their groups each, 128 times for a game of 128
  (34,000 cycles a chunk destroyed, two thirds of a destruction). It's worked out once, and the same players get the
  same records.

**Tests.** `page_references_hold_while_another_thread_lets_snapshots_go` (`tests/test_page.c`, Windows: a thread
lets go of 400 kept copies of a table while this one copies it and writes it; every page is the table's alone at the
end and it hashes as a twin that shared nothing; with `tide_page_release` not atomic the process stops on a corrupt
heap). The bench's runs on 12 threads end on the hashes of 1 thread's (notes/voxels.md, Tests).

## Projection checks go by the pages written since the check before (2026-11-17)

What was left of the projection checks (the sections below, Left; notes/voxels.md, The spread case taken apart, 5f):
every check looked at every piece's heap pages and slots' pages, and where entities came and went, the writes
scattered over the world (free slots and blocks are taken again) made most pieces find a page of theirs changed, so
the blocks their rows read in it were hashed again and their rows' slots read again. Counted over a run of 32 players
apart (`bench_voxels_counts --players 32 --spread 256`, 2,736 checks): 54.7M heap pages' and 9.3M slots' pages'
hashes looked at (20k and 3.4k a check), 7.75M blocks hashed again (2.56 GB) of which 7.70M were as they were; the
pieces' part 1.64 s of the run's 3.4 s of checks.

**The runtime** (agreed with the owner 2026-11-17; runtime-ownership.md, top section; notes/ecs.md): a page keeps the
lines of it (64 to a page) written since its hash was made, and when a hash is made, the hash before (`before`) and
the lines written between (`wrote`). A heap's writes say their block's lines, a slot's change its slot's, everything
else all of the page. `tide_page_same_since(p, kept, from, bytes)`: the bytes are those of when the page hashed to
`kept`.

**The checks** (`client_spans_codegen.inc`, `client_state_codegen.inc`; generated, no session change):

- **A walk** (`tide_client_walk`, each check): every heap page and slots' page of the world, its hash now against the
  one the walk before found at its place, and the walk it last changed in (`tide_client_kept.walked`,
  `slots_walked`, a place's that comes or goes too). A piece keeps the walk as of which every one of its pages' kept
  hashes was its page's (`tide_client_piece.valid`: set where the check went through all its pages, or kept them
  anew), and passes over a page no walk since found changed without reading it: 20k pages a check looked at to 800.
- **The lines a piece's rows read** (`tide_client_piece.page_lines`, made as the piece is kept: its rows' blocks'
  lines in each heap page, its rows' slots' lines in each slots' page). A page that changed, whose hash before is the
  one the piece kept and none of whose lines it read were written in between (another entity's block or slot), has
  its new hash kept without its blocks or slots being read: 1.97M heap pages and 1.88M slots' pages a run, where each
  was its blocks hashed again or its rows' snaps read.
- **A block is as of the piece's hash of its page** (`tide_client_item_same_by`, `tide_client_piece_page_hash`): a
  piece's rows' blocks are what they were as of its page's kept hash, which goes on where the page changed in other
  lines, so a block of a page that changed is checked by the lines since that hash, else hashed again. And the pages
  of a piece that changed are checked against its rows' blocks in one go (`tide_client_piece_blocks_moved`), where
  each was a pass over them all.

**Why every page passed over is as it was.** Everything goes by a page's hash, which says its bytes (as every kept
hash already does), and nothing by which page or world it is. The walk: a place's hash is compared with the one the
walk before found there, every check, so a piece's kept hash, equal to the place's as of its `valid` walk, is equal
to it now while no walk since found it changed; `valid` is only set where every page of the piece was its page then.
The lines: the page's hash before is the piece's kept one, so its bytes then were those the piece read; the runtime
says every write's lines (a heap write its whole block's; a write that says nothing, all of it; a copy its source's
and what it doesn't copy), so bytes in no line of `wrote` are those bytes; and the piece's lines are every one of its
rows' blocks' (all of a page whole) and slots'. A slots' page is passed so only where no row's entity column changed
(as the snaps check went). A page from a world taken back, read from bytes or copied carries its own hash before: it
holds where that's the one kept, as the bytes then were the same. `TIDE_CLIENT_SPANS_CHECK` works every hash out
again from the rows and stops on a difference: `bench_voxels_checked` for 30 s with 8 bots, 32 together, 32 apart, 8
at `--near 12` and 64 together pass, ending on the hashes of the build before.

**Pins**: none moved (the hashes are of values); `bench_voxels`' and every fixture's as they were.

**Measured**: notes/voxels.md, Projection checks by the pages written (the table, both builds alternating).

**Tests.** `tidec_e2e_visibility_walks` (and `_checked`, `_memo_off`; `e2e/visibility_walks.tide`, `_test.c`): 1,200
plots, made a tick after another of their heap's, so a plot's grid's chunk shares its page with the next one made and
plot 64k-1's with 64k's, in two pieces; sixteen rounds (noise every other one), each at the first row of a piece and
the last of the one before: the target's and the neighbour's chunks in one tick; the neighbour's alone, then the
target's the tick after; the target's small map and the tick after the neighbour's, checked once, the world hashed
in between (its hash before isn't the kept one); the same after the neighbour's grid's chunk changed; the target's
list in a world taken back two ticks; the target snapped as the neighbour in the slot beside it goes; a plot made in
that slot; another of the piece before gone as a plot is made; the target snapped the tick after. Every check
against a copy's with nothing kept and repairs from what's kept against the rows'; each step shows against a twin
without it; pinned, the same in the three. The runtime's tests: notes/ecs.md, top section.

Mutations (each alone, against the visibility fixtures `_blocks`, `_pieces`, `_trees`, `_runs_checked`,
`_repairs_checked`, `_walks` and their `_checked`): a page held by its lines whatever they say (nine fail); held
without its hash before being the kept one (`_walks`); a walk after the one kept as of passed over (nine). Not
caught, and why: a block's lines taken as its first line alone, or its first 16 bytes' (a write says its whole
block's lines, so the first is in them whenever any is); `valid` set where a page loop broke (such a piece is read and
kept again, which sets it); places past a heap's end not found changed (a row whose block's page went changed its
handle); slots' pages passed where a row's entity changed (those rows are hashed again for it); a row's blocks taken
as of a piece's hash where the piece isn't kept; the slot after a row's taken for its lines (wrong only for the last
slot of a line, which no run here met); the snaps check by lines without its hash before. The runtime's mutations:
notes/ecs.md and the test names there.

**Left.** The pieces' part is now the pages' ids a piece goes through (about 60, 0.3 µs a piece; a walk that knew
which pieces read each page would go from the 800 that changed to theirs, but most pieces have about two changed a
check in the spread case), and keeping a piece's lines costs about 0.6 µs a keep. What a check costs now is mostly
the rows that did change: audiences worked out again, players' sums, rows read again and grids encoded. Big
containers' chunks and the directory of groups still go by their pages' hashes, where the lines would say more; a
memo entry's blocks by their own page hash.

## A hundred players: repairs packed while the next are read, confirmed worlds by tick, kept ticks hashed once; no protocol change (2026-11-17)

What a server's update is made of with 100 players (notes/voxels.md, A hundred players, has the game's side and every
table), and three changes in `engine/src/session.c` (agent-maintained) and one in tidec (`codegen.c`). **Nothing on
the wire changed** (`PROTOCOL` stays 27), and nothing of a world: the same datagrams in the same updates, the same
hashes (`bench_voxels` at 100 players together and apart ends on the build before's hash, bytes and ticks).

**Where the time was** (`bench_voxels --players 100`, 12 threads, timers put into `tide_server_update` and into
`client_changes`' generated C for the run and taken out; seconds over an 80-second run, 4,789 updates): repairs read
(`client_changes`) 8.8, packed (`tide_recipient_delta_pack`) 6.0, the players' projection hashes 6.0, the tick 5.5,
taking in what players sent 3.4 (of which the copies below about 1.7), a kept tick's world let go of 0.7. The longest
updates were an update's 20 to 32 repairs: read 15 to 20 ms and packed 15 to 17, one after the other on one thread.
Of a repair's read: `put_record` of each chunk it sends 60% (124 chunks a repair on average: other players' edits),
`tide_world_hash` of the world the player confirmed 18% (56 µs at 25,000 chunks, 311 at 79,000 apart), the rows of
that world it looks at 16%.

- **An update's repairs are three steps** (`repair_read`, `repair_post`/`repairs_packed`, `repair_send`; the
  `repair_start` of each): read in the connections' order with the same allowance (`REPAIR_UPDATE_MOST`) as before,
  each handed to the pool's threads for work in the background as soon as it's read (a helper, as `REPAIR_HEAVY`'s
  packing is), so it's packed while the next player's is read; then each is sent, or its world started again, or the
  hash offer made, in the connections' order, where the update's loop comes to its player. What a player is sent and
  when is what it was: the reads and their order are, packing is a function of the bytes read, and nothing between a
  player's read and its sending reads what another player's sending changes (each connection's own state). With no
  pool (or one thread) each is packed where the update waits for them. A repair put off this update
  (`connection.making`) is sent in a later one (`repair_making.update`): the loop that starts and sends is two loops
  now. Packing went from 6.0 s of the run to 1.2 s waited for.
- **A player's confirmed world is a kept tick's, by its tick** (`connection.verified_kept`, `verified_of`,
  `keep_verified`): every acknowledgement of a newer tick copied the server's kept world of that tick into the
  connection's `verified_world` (about 440 copies a second at 100 players, each a pass over every page, and the old
  copy let go of), for a base hardly ever used. Now a tick before the server's is held by its tick: the kept world of
  a tick doesn't change until the ring lets it go, and as it's let go (`tick_end`) the players still on it get their
  copy then. A tick that is the server's own (its world changes with the next tick) is copied as before. A hot reload,
  which lets go of the kept worlds, clears it with the copies. The copies went from 500 a second to 60 (the ticks'
  own), taking in what players sent 2.9 s to 1.3.
- **A kept tick's world is hashed once** (`tide_game.freeze_world`, tidec's `gen_world_frozen`): every repair hashes
  the world its player confirmed to find it in the checks' journal (`tide_client_journal_between`), and most players'
  repairs in an update confirmed the same few ticks. The session says a world won't change (each kept tick's, and
  each `verified_world` copy) and `tide_world_hash` keeps what such a world hashes to the first time: a table by
  address under a lock, a world leaving it as it's let go of (`tide_world_free`) or copied over (`tide_world_copy`).
  Generated for a game whose servers repair by what changed (`world_freezes`: visibility and a heap the hashes keep);
  NULL for the others, which keep nothing. `TIDE_CLIENT_SPANS_CHECK` (`bench_voxels_checked`) hashes a frozen world
  again each time and stops where it differs. The base's hash went from 1.29 s of the run to 0.33 together and 7.2 to
  1.9 apart; part of it came back as the rows of that world read colder (the hash had brought its pages in).

**Tests.** `bench_voxels_views` (several players' megabyte repairs, put off, on 1, 4 and 12 threads), `_checked` and
the 100-player runs above ending on the hashes of the build before; `tidec_e2e_visibility_*` (repairs by changes and
the hash offer, `visibility_repairs` against every row), `tidec_e2e_net*`, `tide_tests session` and `visibility`
(release and ASan); `sumo_tests`, `earth_tests`, `towers_tests`. Listed with results in notes/voxels.md, Tests.

**Left.**
- The reads are one thread's: `client_changes` takes the match's memo (`busy`), and a second at once would take
  another, with nothing kept, and walk every row. 60% of a read is its records, each chunk's grid written again for
  each player it goes to though the bytes are the same for all of them where nothing is denied: a record kept by its
  entity and value hash for the update would make a repair to 100 players one record and 99 copies. Both are in
  `client_changes_codegen.inc`, the projection checks' (another task's at the time).
- ~~A kept tick's world let go of is a pass over all its pages (`free_world`, 0.53 s of the 100-player run together and
  11.2 apart, 2.3 ms a tick): page counts aren't atomic, so it can't go on another thread while the tick copies pages.~~
  Page counts are atomic since, and it's let go of on the pool's threads (the section above).

## A whole view's repair: read in one update, packed on the pool, one big one an update; no protocol change (2026-11-16, wide views)

What a whole view costs the server to send at wide view distances (notes/voxels.md, A wide view without a long
update, has the measurements and the game's side). Where the time was, for one player's 3,125 chunks arriving (a
repair whose `now` is 3.2 to 3.9 MB, 380 KB as sent; `bench_voxels --near 12`, timers put in for the run and taken
out): `tide_game.client_changes` 4.4 to 8.6 ms, of which 3.4 was a grid's cells written a call each;
`tide_recipient_delta_pack` 5.4 to 8.5 ms, of which 2.1 to 3.5 was the XOR regions packed only to lose to the
spans; and two or three players' repairs in one update (bots that joined together, or hopped together). The
longest update at that view was 20 to 23 ms, all of it this. Four changes, three in `engine/src/session.c` and
`recipient_delta.c` (agent-maintained), one in tidec. **Nothing on the wire changed** (`PROTOCOL` stays 27): the
same envelope, the same codecs, and a codec's choice is in its first byte as before.

- **A grid's cells go a chunk at once in a projection's bytes** (`value_codec_codegen.inc`, the `TY_GRID` cases of
  `tide_client_put<n>`, `_get<n>` and `_skip<n>`): where a cell's bytes are its codec's (`client_codec_raw`, as a
  list's elements already went), one `tide_client_write` or read and `memcpy` of the chunk, where each cell was a
  call. The same bytes; a cell's own reader refused one nested past 256 deep, so the chunk's does. The check's hash
  had this since 2026-11-16 (Projection hashes: the rows that changed); the repairs' and projections' codec hadn't.
  A whole view's records were 16 M cycles of the 19 its read took (3.4 ms of 4); the server's `client_changes` over
  a run went from 8.9 to 9.9 ms a second to 6.9 to 7.5 at twice the view and from 18 to 19.5 to 13.3 at three
  times, and a client's unpack from 1.76 ms a second to 1.53.
- **Mostly new bytes are packed as spans first** (`tide_recipient_delta_pack`, `FRESH_LEAST`): where `now` is at
  least 256 KiB and four times its base or more (a view arriving, a first world), `copy_spans` runs first, and
  where it comes to an eighth of the bytes or less the XOR regions aren't packed to be compared with it. The spans
  won every such repair measured (192 KB against 282, 382 against 447, 401 against 508), so the bytes are the
  same; where the regions would have been smaller still, the transfer is that much bigger, never more than an
  eighth of `now`. Smaller or less new, both are packed and the smaller goes, as before.
- **A big repair is two updates' work** (`REPAIR_HEAVY`, 4 MiB of `now`; `client_changes_read`,
  `client_changes_pack`, `connection.making`, `put_off_repair`, `finish_client_repair`). `pack_client_changes` is
  two functions now: what changed, read from the two worlds (the plan, `now`, `was`, what died, the events: all
  that needs the worlds), and those bytes packed, which reads no world. A repair whose `now` is `REPAIR_HEAVY` or
  more, and that can't turn out to be one for the hash offer (`may_be_mostly_changed`: that fallback needs this
  update's worlds), is read in the update that starts it and packed and sent in the next. Where the pool has
  threads for work in the background (`tide_jobs.background` and `help`, two threads or more) the packing is
  handed to it as a helper as soon as the bytes are read, and the next update takes what it made
  (`packed_making`: it runs the job itself if no thread took it, and waits if one is at it); with no pool the next
  update packs it. Which repairs are put off goes by the bytes read, and when they're sent by the update, so the
  same datagrams go in the same updates on one thread and on twelve (`bench_voxels_views` pins it). The connection
  isn't `sending` meanwhile: a repair asked for again in between is dropped as one asked for during a transfer is,
  `stop_sending` drops what's being made (waiting for the pool first), and a hot reload starts such a player's
  world again as it does a transfer's.
- **A view arriving is read as the world is now first** (`start_client_repair`): `repair_as_it_is_now` used to be
  packed after the tick asked for, and sent in its place if a quarter bigger. Now it's read first, and where it is
  big it goes without the tick asked for being read at all (a join where the first player has made the view was
  two whole views read and packed in one update). Where it isn't big, both are packed and compared as before.
- **An update reads `REPAIR_UPDATE_MOST` of repairs** (4 MiB of `now`, `tide_server.repair_work`), then the
  repairs still to start wait for the next update (`repair_pending` stays). Players who join or come through a
  portal together were each a whole view read and packed in one update; now an update starts them until it has
  read that much (two views of 3,125 chunks, one of 6,845), in the connections' order, and a small repair (a
  border's row of chunks is 80 KB) waits only where megabytes were read before it in the same update. At 2 MiB
  (one view of 3,125 an update) eight players hopping together had their boxes 3 ticks later at the median and 9
  at most, for updates of 5.5 ms where two are 11: the line is the owner's to move.

**What it costs a player**: a repair of 4 MiB or more lands one tick later (near 18's whole views; near 12's are
3.2 to 3.9 MB and aren't put off). Nothing at the default view: `bench_voxels_arrives` is 21 ticks at the median
and 29 at most, as before.

**Tests**: `bench_voxels_views` (bench/CMakeLists.txt): four players joining at three times the view radius where
the first has made it, three repairs of 6.3 to 7.0 MB put off, the world pinned and the same with `--threads 1`
(packed by the next update), 4 (a pool with no background work: the same) and 12 (the pool's threads), and in
`bench_voxels_checked`, which works every repair out both ways. The grid codec is under every visibility fixture
with a grid (`tidec_e2e_visibility_blocks`, `_pieces`, `_runs`, `_trees`, `_repairs`: their pinned projection
hashes and repaired worlds are where they were). `tests/test_recipient_delta.c` passes as it was (its buffers are
under `FRESH_LEAST`, so both codecs are still compared there).

**Left.**
- The read itself is one update's and one thread's: about 2.3 ms for 3,125 chunks and 3.7 for 6,845 (`bench_voxels
  --worst`'s pack column; one of 14 ms was seen at three times the view on one thread, not taken apart). Its
  records could be written by ranges on the pool (each entity's record is its own bytes); `tide_client_see` and the
  memo it reads are one thread's (`busy`).
- A repair under `REPAIR_HEAVY` is still read and packed in one update (5.5 ms at 3.5 MB: 2.3 and 3). Putting those
  off too cost a tick for each repair of a box that arrives in several (near 12 with 8 bots: 35 ticks to 40 for a
  whole box at the median with the line at 2 MiB), which is why the line is where it is.
- Nothing tests a put-off repair that's dropped (a player who leaves in the tick between) but the paths it shares
  with a transfer's (`stop_sending`).

## A grid read from bytes left 64 bytes on its thread for good (2026-11-16, after the sweep)

**What grew.** A player's machine kept 64 bytes for every grid it was ever sent, outside every world: in voxels, for
every chunk that came into view. `tide_client_get<n>` for a grid (`value_codec_codegen.inc`) makes the grid in the
scratch area (`tide_grid_new`: its directory's block, 64 bytes) and takes it into the heap (`tide_grid_own`), and
nothing gave the scratch block back: the readers are called from `load_client_state`, `reload_client_state` and
`apply_client_changes`, which a session calls from `tide_client_update` with no mark around them, on a thread whose
scratch area nothing else resets (a system, a handler and a view reset to their own marks; a session's update isn't
inside one). So the thread's scratch area grew a 1 MiB piece at a time, each new piece asked for by whatever next
needed room (in `bench_voxels`, whose server is updated on the bots' thread, a `Want` handler's map: which is why
the pieces were counted as the server's). The same readers read a heard event's fields, a shard's messages and a
shard's rows, so a server reading grids from those did the same.

**Measured** (`bench_voxels --out 1 --hop 0 --watch 0 --threads 4`, 8 bots flying outward, in a build that counts
what malloc's callers hold: `bench/alloc_count.h`, notes/benchmarks.md; notes/voxels.md, The process's growth, has
every structure's bytes over 20 minutes). The bots' updates left 4,044,864 bytes in their thread's scratch area in
the first two minutes, always a multiple of 64 an update (64 for a chunk, 4,160 for the row of 65 a bot crossing a
chunk's border brings, 54,080 for a first world of 845). Scratch pieces held at minutes 2, 4, 6, 8 and 10: 7, 11,
14, 18 and 21 (7.3 to 22.0 MB): 1.84 MB a minute, for 472 chunks a second coming to the eight machines at 64 bytes
(1.81). After: 4 pieces, 4.2 MB, at every minute to the twentieth.

**The fix** is where the block is made: the reader takes a mark before `tide_grid_new` and goes back to it once the
grid is the heap's (`tide_handle_where(v->at) != TIDE_IN_SCRATCH`: a grid read where there is no heap stays the
scratch area's, as before). Nothing else is made between the two. No world, hash, byte sent or layout changes: the
10-minute run ends on the same hash (`0x1d2026427b0aaadd` at tick 36,229) before and after. The other readers make
nothing in the scratch area (text is set from the bytes read, lists and maps grow in the heap they're read into).

**Tests.** `tidec_e2e_visibility_runs` (and `_checked`, `_memo_off`), `visibility_runs_a_players_world_is_its_projection`:
the thread's scratch mark is where it was after the first load and after each of the 26 rounds of ticks, hashes, a
repair made and taken, and a whole load (chunks with grids come and go in each). Every session benchmark now fails
a run whose updates leave anything in their thread's scratch area (`bench.c`, `results.scratch_left`: the bots'
machines' and the server's apart), so `bench_voxels` and its variants, `bench_gallery`, `bench_sand` and
`bench_earth` (and `_split`, `_diverge`) check it on every run. With the fix out: `tidec_e2e_visibility_runs` fails
at the load and at each round, and `bench_voxels --quick` says "updates left 98432 bytes in their thread's scratch
area for good (the bots' machines' 98432, the server's 0)". With it, release on Windows: those, `tide_tests`, every
`tidec_e2e_visibility_*`, `tidec_e2e_index_follow`, `earth_tests` and `voxels_walk` pass, every pin where it was (42
tests). Not run here: the web build (one extra tree was this task's, the counting one) and the sanitizers.

**Left.** A session's update still has no mark of its own: anything else that ever makes scratch values under
`tide_client_update` or `tide_server_update` outside a handler, a system or a view would stay the same way. The
benchmarks' check is what would say so; nothing does today in voxels, gallery, sand or earth.

## A view arriving: repairs as the world is now, spare chunks, 512 chunks said, 256 answers a tick; protocol 27 (2026-11-16, last)

What a whole view costs to arrive (a join, a dimension hop: notes/voxels.md, A whole view arriving, has the
measurements and the game's side). Four changes in `engine/src/session.c` (agent-maintained), one generated export.
No world, hash, snapshot or layout changed; the wire did (`PROTOCOL` 27), and `bench_gallery`'s pinned world moved
because its joins are shorter (below).

- **A repair asked for of one tick goes as the world is now, where entities have come to the player since**
  (`start_client_repair`, `repair_as_it_is_now`, `connection.came_at`). A client asks for the tick after its
  verified one, and the first of a burst was answered with exactly that tick (a second within a second starts a
  stream, which goes to the server's tick). A view arriving differs for many ticks in a row (chunks shown as they're
  made), so the first repair held one tick's part of it and the rest waited a round trip for the next: with a hop,
  the first tick is the old dimension leaving, and the new one's chunks all came with the second repair, 16 ticks
  after they were asked for. Now, where the tick's check found entities come into the player's sight in a world
  later than the one asked for (`tide_game.client_came`, below), the repair is packed again from the same base to
  the server's world and sent in the first one's place if it's a quarter bigger, as a stream's first
  (`repair_stream_until`, `repair_gap` as a stream that starts). A tick the client's own run got wrong, with nothing
  arriving, is answered as before (one tick; nothing is packed twice), which is why it goes by what came and not by
  the repair's size: a border crossing's row of chunks is one tick's, and its repair stays one tick's.
- **`tide_game.client_came(world, recipients, count, out)`** (generated, `client_state_codegen.inc`; optional):
  whether a visibility group's audience gained each recipient in the checks of `world` since it was last asked.
  The check's comparison of the directory of groups with its copy (`tide_client_groups_diff`,
  `client_spans_codegen.inc`) already finds each group whose audience is another; it now ORs into
  `tide_client_kept.came` the players the new audience has that the old hadn't (a new group's whole audience; an
  entry that moved as the map packed counts whole). Byte-wise over `Audience`, so any number of players. Said of more
  players than it's true of at times, never of fewer; a build that keeps nothing (`TIDE_CLIENT_MEMO_OFF`) says it of
  everyone a group has. The session asks once a tick, after `client_hashes`, and keeps the world's tick
  (`came_at`). An entity spawned in sight by code the client runs too is no group's gain and isn't said; one spawned
  into a group that has the player is (a false alarm costs one more pack, sent only if it's bigger).
- **Spare chunks** (`MSG_SPARE`, `send_chunks`, `on_spare`, `take_spare`). A world or a repair goes in chunks of
  1,000 bytes, and a lost one was asked for by the client's next CLIENT and sent again: a round trip and a half
  (12 ticks here), with a box of 116 chunks losing at least one 7 times in 10 at 1% lost. Now a transfer of 32
  chunks or more (`SPARE_LEAST`), to a player whose loss is counted and isn't none, is followed block by block (64
  chunks, an update's worth: `SPARE_BLOCK`) by spares: each the XOR of every `stride`th chunk of the block, so
  chunks sent one after another are in different spares, and a chunk that's the only one of its spare missing is
  made from the spare and the rest. A spare for 16 chunks up to 2% lost, for 8 up to 5.6%, for 4 past that
  (`spare_group`, by `frame_copies`, the count ticks' copies go by); none until `LOSS_SERVERS` are counted
  (`loss_counted`: a connection's copies start at eight, which says nothing of its loss), so a player joining has
  none. Spares go once, over the update's 64 chunks, and are never sent again; what they don't save is asked for as
  before. Datagram: the header a CHUNK has, the transfer's number (u32), the block's first chunk (u32), the stride
  and the group (a byte each), 1,000 bytes. **Whatever a spare says**: its number is the transfer being received,
  its block starts on a multiple of 64 inside the transfer, its stride is 1 to 16 and no more than the block's
  chunks and the one the block's first spare said, its group is under its stride, and its bytes are a chunk's
  length, or it's left. A chunk shorter than 1,000 bytes (the last) made from a spare has to leave zeros after its
  bytes. A spare of the right numbers and other bytes makes a chunk of them (nothing says they aren't the server's:
  the header's cookie and nonce are what authenticates a CHUNK too), and the transfer then fails as any transfer of
  wrong bytes does, at its unpack or its hash, and is asked for again.
- **A CLIENT says 512 chunks past the first it lacks** (`CHUNK_WORDS` 8 words of 64, only as many as reach the last
  it has, after a count byte; it was one word always). The server keeps what it's told a chunk at a time
  (`chunk_had`, `chunk_top`), where it kept the one word and dropped it as the first lacking moved. A chunk past the
  64th after a lost one was sent again a round trip and a half after it went, though the client had it: 2% more
  bytes at voxels' default view, 28% at twice, 40% at three times, and a third of what `bench_gallery`'s joins sent.
  A transfer with nothing past the first lacking says a byte where it said eight.
- **256 of the store's answers a tick** in a game with projections (`TIDE_STORED_PER_TICK`, `dispatch_injected`),
  where every game took 64 of shard messages and answers together. Such a game's frames carry no answer (what one
  changes reaches players as state), so nothing but the tick's own work bounds them; a game that does a lot for
  each paces that itself (voxels' `Pace`). Shard messages stay 64, counted apart, in the order they came; a
  journal's record takes both (`replay_record`, `frame_names_each`). Any other game's answers are in its frames and
  stay 64. Provisional (Claude's pick; spec, The server's store, Order).

**Measured** (notes/voxels.md has the tables). One player standing, hopping every 3 s into a dimension whose chunks
have gone, the box of 845 chunks whole on its machine, ticks from its input (median, most), before to after: no loss
28 (28) to 20 (20); 1% lost 36 (44) to 21 (29); 5% lost 45 (71) to 29 (42). `bench_gallery`'s joins (a
4.4 MB world to each of 8): 2.47 s to 1.73 at 1% lost, 2.85 to 2.02 at 3%, 3.07 to 2.22 at 5%, 1.50 to 1.40 at none;
251.8 KB a second to each bot to 157.1 (328 to 218 at 3%).

**Pins.** `bench_gallery --quick`: `0xc8e6ab383a684a0a` to `0x57ef0aa3786fe0c6`, the same on 1 and 12 threads: a
bot whose world comes sooner samples its first input in another tick. Nothing else pinned moved for the session's
part (voxels' moved for its game's changes).

**Tests** (`tests/test_visibility_session.c`, the large game's client behind `visibility_wire`, which can lose one
SERVER in n so the server counts a loss, lose a chosen chunk of a watched transfer and its spares, count what came
of it, and give the client a datagram the test made; `visibility_differs_from`/`_to` move the ticks the large game's
client differs in, so the loss is counted before the repairs start):
- `visibility_spares_make_a_lost_chunk_again_before_its_asked_for`: 5% of SERVERs lost, then the server's build
  swaps under a repair and the whole projection (545 chunks) goes: 69 spares (8 to a block), chunk 7 lost once and
  never sent again; with its spares lost too, sent again once. No spare of any other transfer (the first world came
  before the loss was counted).
- `visibility_spares_go_only_where_datagrams_are_lost`: nothing lost, no spare, the lost chunk sent again.
- `visibility_spares_that_arent_ones_are_refused`: once every other chunk has come and before the lost one is sent
  again, its own spare with no stride, a stride of 17, a group past its stride, a block that starts at chunk 7, a
  block past the transfer, 999 bytes, and another transfer's number: none makes a chunk (the client's bytes stand),
  and the world isn't sent again.
- `visibility_a_spare_of_other_bytes_costs_the_world_again_and_no_more`: the same spare with other bytes is taken,
  the world fails its check, is sent again once, and the client has it.
- `visibility_chunks_past_a_lost_one_arent_sent_again`: a chunk lost twice in a transfer of 545, no loss counted:
  572 chunks came to the wire, the 25 past the 512 said and the lost one twice (the test allows 40 over).
- `tidec_e2e_visibility_repairs` (and `_checked`, `_memo_off`, `_wide`): `visibility_came_says_whom_a_groups_audience_gained`:
  a group given to another player came to that player alone, said once; a lane and a policy came to nobody; two
  checks before it's asked are both said; a group that shrank came to nobody and one that grew to those it gained;
  a group made with an audience; another world says nothing.
- `bench_voxels_arrives` (bench/CMakeLists.txt): the hopping player for a minute at 1% lost, the box whole in 21
  ticks at the median, failing past 23, its world pinned.

Mutation (each alone, the test named failing): the store's answers 64 a tick, or unbounded (`bench_voxels_arrives`,
by `--fill` and by its pin); no repair as the world is now (`_arrives`); `client_came` never saying, or saying a
group's whole audience (`visibility_came...`, and `_arrives` for the first); a spare kept and never taken
(`..._make_a_lost_chunk_again`); one word (`..._chunks_past_a_lost_one`); spares before the loss is counted
(`..._go_only_where`); a spare of any stride, of any number, of any block (`..._that_arent_ones`). Not caught by any
test: the zeros a short last chunk must leave after its bytes (a transfer whose last chunk is lost with its spare
kept isn't made by the fixture; the transfer's own check stands behind it).

**Validation** (release, Windows): `tide_tests` whole, every `tidec_e2e_visibility_*`, `_storage*`, `_net*`,
`_managed_store*`, `_shards*`, `_task_visibility`, `_scene_preparation_visibility`, `tide_platform_store_headless`,
`tide_platform_capture`, `tide_platform_reload`, `_reload_reads`, `earth_tests`, `bench_earth` (and `_split`,
`_diverge`), `bench_sand`, `bench_gallery`, `bench_voxels` (and `_forgets`, `_wide`, `_checked`, `_counts`,
`_arrives`), `voxels_walk`, `tide_platform_png`, `_png_decode`, `_gif`, `tide_cli_help`, `tide_cli_run_exit`: 67
CTests, all pass; `npm run build` in `docs/`. Sanitizers, the web and the other platforms are CI's.

**Left.**
- Spares start once a connection's first 512 SERVERs are counted, which a player who hops in its first seconds, or
  whose server is sending it repairs most of the time, waits longer for (a 30 s bench run's hops get them late or
  not at all: its boxes took 23 and 28 ticks at the median where the minute-long run's took 21). A transfer's own
  chunks say what's lost exactly (sent against had), and counting them would tell sooner. Not built.
- A repair is still one transfer at a time, each waiting for the last one's acknowledgement: a view that's made
  over more ticks than a round trip has (voxels at twice the view) arrives in as many round trips. A second
  transfer started on the first one's world before it's acknowledged would halve that; the chunk state is one
  transfer's throughout (`snapshot_number`, `chunk_*`), so it's a rewrite of it.
- The server could start the first repair itself, the tick entities come to a player, where it waits to be asked
  (a round trip before the nearest chunks land). The client takes a repair it didn't ask for, but only from the
  world it has confirmed, which the server knows a round trip late: the repair would have to name its base and the
  client hold it until its verified world reaches it. Not built; `came_at` is the signal it would go by.
- A hop's first repair says each of the 845 chunks that left by 16 bytes of identity (13.6 KB). A group that went
  whole could say so once.

## A whole projection the server starts is taken; a reload under repairs stops no client (2026-11-16, later still)

The bug left open below (protocol 26, Open): a hot reload during a stream of large repairs could stop a client for
good, the server sending a whole projection for the rest of the match and the client acknowledging no chunk of it.
Two causes, both in how a client reads a WELCOME (`on_welcome`, `engine/src/session.c`; agent-maintained), neither in
protocol 26 nor in the reload's own code. No wire, world, hash or generated code changed.

**Cause 1: a client refused a whole projection it hadn't asked for.** A server whose build changes while it is
comparing a repair with a player's world (`connection.repair_base`: the hashes or the patch of a hash offer are on
their way, or went and aren't acknowledged yet) lets go of the worlds it compares, which the old build made, and
starts the whole projection in the repair's place (`stop_recipient_comparisons`, `start_snapshot`).
`start_snapshot` is a commitment: the player's `frame_ack` becomes the server's tick, what was sent of the ticks is
forgotten, and while it's `sending` no repair is packed (`start_client_repair` returns) and a repair asked for a
tick that isn't `frame_ack + 1` is no request. The client took such a WELCOME only if it wanted a world
(`need_snapshot`, which it sets itself, in `client_set_game`, when *its* build changes while *it* is comparing) or
if the world's tick was past all it ran (`tick > ahead`). A client that had the repair whole already (its
acknowledgement on its way or lost), or that hadn't had the hashes' WELCOME yet, or whose build didn't change in
the same update (another machine's never does), wasn't comparing, runs ahead of the server, and refused it. It went
on asking for a repair of the tick after its verified one, which says `CLIENT_WANTS_WORLD` too, so the server
neither stopped sending (`newest && !wants`) nor answered: for good. The test's two sessions swap builds in the same
update, so it took a lost or late acknowledgement to show (3 seeds of 64); a server's build alone changing stops
45 of 64.
*Fix:* a `SEND_CLIENT_WORLD` newer than all the client was sent, of a tick not before its verified one, is taken
whether asked for or not (`restarted`). That's every unasked `start_snapshot` of a game with projections: this one,
a repair the server can't make (`start_client_repair`: a tick or a world it no longer keeps), a player too far
behind (`send_frames`). The last two got by on `tick > ahead`, which holds when the client's own ticks stopped too.

**Cause 2: a late WELCOME of the transfer before took the place of the one being received.** WELCOMEs go
unordered, one an update while something is sent. A client receiving transfer N+1 (the whole projection) that read
a late WELCOME of N (the repair's hashes the reload ended; a repair is wanted whenever its tick is past the
verified one) dropped what it had of N+1 and waited for N, which nobody sends; the next WELCOME of N+1 started it
again with no chunk, and the server, which had heard the client has the first of them (`chunk_ack` only goes up),
never sent those again. With cause 1 fixed, 1 seed of 64 still stopped this way with both builds swapped, 2 of 64
with the server's alone.
*Fix:* while a transfer is being received, a WELCOME of a lower number is old and is left alone (`number <=
snapshot_number`). A server's numbers only go up on a connection, and a client is one connection's (its nonce is
made once), so a lower number is never the newer thing. This is every game's `on_welcome`, with projections or not.

**Seeds** (`visibility_large_repairs_release_old_callbacks_on_compatible_reload`'s scenario, which now runs 1,800
steps and asks that the client is within 30 ticks of the server at the end with every hidden effect, where it
asked for `verified > 650` and more than 10 repairs at step 1,500; clients that stopped did so at ticks 89 to 450
of 900): seeds 1 to 104 and 61570 to 61590, 125 in all.

| Builds swapped | When (steps of 120 a second) | Stopped before | With cause 1 fixed alone | With cause 2 alone | Now |
|---|---|---|---|---|---|
| Both, in one update | 400 to 440, every 4 | 9 | 1 | 8 | 0 |
| The server's | 400 to 440, every 4 | 77 | 3 | 75 | 0 |
| The client's | 400 to 440, every 4 | 0 | 0 | 0 | 0 |
| Both | 400 and 401 | 20 | 2 | 19 | 0 |
| Both | 4 to 60, every 4 (the first world coming) | 0 | 0 | 0 | 0 |
| The server's | 4 to 60, every 4 | 0 | 0 | 0 | 0 |
| The server's | 200 to 1,100, every 37 | 96 | 9 | 93 | 0 |
| Both | 200 to 1,100, every 37 | 2 | 2 | 0 | 0 |

Also 0 now, not run before: the client's at 4 to 60 and at 200 to 1,100, the server's at 400 and 401. The test's
own seed is 61579 still; 61577, 25 and 61572 (the three that stopped after protocol 26) pass.

**Tests** (`tests/test_visibility_session.c`; `visibility_match` is the large game's host and client, the client
behind `visibility_wire`, which says what each WELCOME names, can lose a repair's hashes and can give the client a
WELCOME again late):
- `visibility_reloads_under_repairs_stop_no_client_whatever_is_lost`: seeds 1 to 64, both builds, 400 to 440. Seeds
  25, 45 and 48 stop with the session before.
- `visibility_reloads_of_the_server_alone_stop_no_client_whatever_is_lost`: seeds 1 to 64, odd ones 400 to 440 every
  4, even ones 200 to 1,100 every 37. 45 stop before.
- `visibility_reloads_of_either_side_at_any_time_stop_no_client`: 8 seeds each of the client's alone, two reloads in
  a row (both, and the server's), the first whole projection coming (both, either), and all through the stream
  (both, the client's). 6 of 64 stop before.
- `visibility_reload_whole_projection_is_taken_after_a_repair_the_server_hasnt_heard_of` and
  `..._in_place_of_a_repairs_hashes`: cause 1's two states made on a network that loses nothing (80 ms each way):
  the server's build (and both) swapped in the update the client loads a patch, and in the one a repair's hashes
  are first named. The whole projection is named next, of a tick the client ran past, and the client's verified
  world is that tick's within two seconds.
- `visibility_reload_late_welcome_of_the_repair_it_ended_costs_no_chunk`: cause 2: a repair's hashes are named and
  none comes, the server's build swaps, three updates of the whole projection come, the hashes' WELCOME is read
  again; the client has the projection within two seconds and less than 700 KB of it was sent.
- `visibility_reloads_during_big_command_uploads_lose_no_command`: a 100 KB command from each machine with builds
  swapped (the server's, the client's, both) as they go up, 10% lost: both run, once.

Mutation: with `session.c` as it was, six of the seven fail (the uploads' passes, as it did). With cause 1's line
out, the same six (cause 2's focused test can't be set up: no projection comes to interrupt); with cause 2's out,
its focused test and the two 64-seed tests fail.

**Validation** (release, Windows): `tide_tests` (778, the seven new ones 13 s of it), every `tidec_e2e_visibility_*`
(26), `_scene_preparation_visibility`, `_shards_visibility`, `_task_visibility`, `tidec_e2e_net`, `_net_no_input`,
`_net_ended`, `_net_alone_seen`, `_net_join_big`, `tide_platform_reload`, `tide_platform_reload_reads`,
`earth_tests`, `bench_gallery`, `bench_voxels` (and `_forgets`, `_wide`, `_checked`, `_counts`): 44 CTests, all
pass, no pinned hash or byte count moved.

**Sibling cases.** During a whole projection (the first, or one a reload started): the server compares nothing, so
its reload starts nothing; the client isn't comparing; tested (4 to 60, and every reload after the first of a
run). During a hash offer and during its patch: cause 1's states, tested both. The client's build alone: it asks
for the world (`need_snapshot`), and the server, whose transfer is a hash offer or a patch, starts it (`on_client`,
"Its comparison was discarded"); a repair by changes or by baseline being received goes on, as nothing of it is the
old build's but `drop_taken`, which makes its load fall back to a fresh one. Two reloads in a row: the second finds
no comparison. A repair superseded mid-stream: only `start_snapshot` supersedes a transfer (a repair isn't started
while one is sent), which is cause 2. A big command's upload: tested. A download (a big command to a player that
runs it, a store's answer, a shard message): `tide_session_set_game` touches neither side's (`download_*`,
`downloads`), read and not tested: a player with a projection gets none in this fixture, and games without
projections have no comparison for a reload to end.

**Left.**
- The server's reload sends the whole projection where it could let go of its worlds and wait to be asked: a client
  that had the repair already needs nothing, and one that was comparing asks. Half a megabyte here each time. Not
  changed: a client mid-hashes with no repair asked for would then ask for a world the same way, and the rule above
  is needed either way.
- After a whole projection, a client whose ticks go on differing doesn't predict until the stream ends
  (`catching_up` ends where the confirmed ticks reach the server's, and a repair lands behind them): its `ahead` is
  its verified tick, a repair at a time, for as long as every tick differs (this fixture's ticks 90 to 570). It
  goes on, a second or two behind, as far as before the reload, when it predicted. Seed 35 was this at step 1,500
  (verified 597 of 750, a repair to 700 on its way), not a stop. Whether a client in a stream should predict from
  a whole projection is the owner's.
- `tide_platform_reload_web` and the sanitizer, web and other platforms' runs are CI's.

## Repairs from the rows a check wrote down (2026-11-16, later)

A repair by changes (`tide_game.client_changes`, `client_changes_codegen.inc`) listed, for one player, every entity
that player may have in the world it confirmed and in the world now, to find the few that differ: every row of the
first had its audience worked out and its hash made (`tide_client_see`), every row of the second read from what the
tick's check kept. 70 to 95 ns for each entity the server has, for each player, about four times a second: a third of
a server's update with 8 players in `bench_voxels`, half at 64 (notes/voxels.md, At scale). Now a repair looks at the
rows that changed for its recipient between the two worlds, and at no others.

- **Each check writes down the rows it found changed** (`tide_client_journal`, in `tide_client_kept`, so it's the
  memo's and goes where the memo goes: `tide_client_memo_forget`, a memo given to another world). The check already
  knows them: a row is written down where what a recipient's part of it goes by came out another than was kept.
  In the rows' part of a check (`gen_client_hash_rows`): the row is new, another entity or other snaps
  (`ids_again`), a term of its sum moved (`gen_client_term_moved`), it's a hole where a row was kept, or it's past
  the table's end now. In the audiences' part: who may have the row, or one of its components, is another than was
  kept. Rows, not entities: a check knows a row whose entity went only as a hole.
- **Each with the players it could be another for** (`tide_client_change.who`, an `Audience`; where an `Audience` is
  4 words or fewer, 128 players: `client_changes_masked`, past that every change is everyone's to look at). A row
  that's another entity, has other snaps or a term everyone has: who had it and who has it now. A term of a
  component that can be denied: who may have that component now. An audience that changed: the players it came to or
  left, and the same of each component's. So a chunk whose `Seen` changed (denied everyone) is nobody's, and a chunk
  that came to one player is that player's alone. The rows' part doesn't know who has a row yet, so it keeps what it
  found (`tide_client_pending`: which terms, whether anything else, who had it) and the masks are made once the
  audiences are (the end of `gen_client_hash_rows`).
- **A check is a node**: the world's hash as the check left it (`tide_world_hash`) and the rows it wrote. Two worlds
  that hash as two nodes differ, for any player, only in rows written by the nodes after the earlier, up to the
  later, whichever way round. So worlds are known by what they hold, not by their address or their tick: a world
  changed outside a tick, or a copy ticked on its own, hashes as no node and its repair walks every row as before.
  The world the match ticks, at the tick its rows were kept at, is the last node without hashing it (it is the
  world those rows are already trusted to be: `current`).
- **A repair** (`tide_client_changes_work`): `tide_client_journal_between` finds the two nodes (the base is hashed:
  one `tide_world_hash` a repair), `tide_client_journal_mark` sets a bit for each row between them whose `who` has
  the recipient, and `tide_client_see` and `tide_client_see_kept` go through the rows with a bit
  (`tide_client_bit_from`) in each world. The lists they make leave out rows that are the same for the recipient in
  both worlds, which no plan names; everything after (the matching by entity, the plan, `now`, `was`) is the code it
  was, so the bytes are the same. Kept rows are used as before, only for the world at `tide_client_kept.tick`.
- **Bounds.** 256 checks (`TIDE_CLIENT_NODES`; the older half goes when it's full) and 2^19 rows
  (`TIDE_CLIENT_CHANGES_MOST`, 6 MB with a 4-byte audience; the oldest checks go). A check writes two rows at most
  for each row of the world. A repair whose nodes hold more than 8 rows for each row of the world and 4,096 walks.
- **A world most of whose rows change every tick** (a battle) would write them all each check for repairs that then
  walk. A check that writes more than half the world's rows and 64 (`TIDE_CLIENT_CHANGES_LEAST`) counts
  (`heavy_run`); after four in a row, a check writes none and says only that everything may have changed
  (`tide_client_node.all`: a repair across it walks), but each sixteenth of the run, which writes, and ends the run
  if it wrote few. One to three such checks in a row are written like any other: a player joining or a whole view
  asked for writes most of a small world's rows, nearly all of them one player's, and the other players' repairs
  across it look at none of them. (The first build wrote nothing in any check past half the world's rows: with 32
  bots 576 of 5,157 repairs walked, the ones across a join or a hop.) Not measured on Mount & Rifle, which isn't in
  this repository: by this rule a battle's checks write one tick in sixteen. A world checked again as it is (the same
  hash as the last node, nothing written) is no check of a run. A game with `TIDE_CLIENT_MEMO_OFF` finds every row
  changed in every check, so most of its checks write none.
- **`TIDE_CLIENT_SPANS_CHECK`** works every repair that went by the journal out again from every row and stops the
  program where a byte of the plan, `now` or `was` differs, as it does a tick's hashes. `TIDE_CLIENT_COUNTS` counts
  the repairs each way, the rows the first looked at, the rows written down, and why a repair walked (the world, or
  the one it was, hashed as no node; too many rows between), and times the check's `tide_world_hash`.
  `TIDE_CLIENT_CHANGES_STATS` (a test's) makes the first three a global, `tide_client_changes_stats`.
- No session change, no runtime change, no protocol or simulation identity change (the bytes are the same):
  notes/runtime-ownership.md, Repairs from the rows a check wrote down.

**Measured** (`bench_voxels`, release, Windows, 12 threads, BelowNormal, one run at a time, three runs each
alternating with the build before; medians, the repairs' ranges in brackets). The same bytes sent, ticks and world
hash in every pair (186.8, 383.0, 850.3 and 583.5 KB a second; `0x77babb9991dd25da`, `0x7b460b72bce7184a`,
`0x55da5c44144f9d4f`, `0xe756c4ee9cf79012`). Another agent's builds and tests ran on the machine through this set, so
the runs spread more than usual and a column nothing here touches moves with them (16 bots' simulating: the three
runs after fell in busier stretches than the three before).

| | 8 bots | 16 | 32 | 8, `--near 12` |
|---|---|---|---|---|
| Chunks the server has | 6,490 | 7,717 | 12,669 | 17,599 |
| Repairs, ms a second | 19.3 to 3.78 (18.4–20.5 to 3.27–3.82) | 39.3 to 8.63 (38.9–43.0 to 7.47–8.69) | 103.3 to 22.6 (86.8–109.0 to 18.6–25.3) | 66.7 to 9.35 (55.5–76.5 to 8.70–10.07) |
| ...each (33, 63, 113 and 33 a second) | 0.58 to 0.11 ms | 0.62 to 0.14 | 0.91 to 0.20 | 2.05 to 0.29 |
| Server update, median, ms | 0.86 to 0.63 | 1.61 to 1.26 | 3.57 to 2.56 | 2.37 to 1.62 |
| ...p99 | 5.37 to 5.20 | 7.89 to 6.48 | 15.23 to 10.49 | 15.55 to 12.59 |
| ...longest | 14.9 to 12.9 | 14.0 to 13.5 | 29.8 to 18.0 | 31.5 to 24.7 |
| Server, hashing, ms a second | 6.80 to 8.27 | 10.4 to 14.0 | 23.7 to 24.4 | 19.6 to 19.8 |
| Server, simulating | 31.9 to 33.3 | 46.6 to 51.2 | 76.2 to 77.5 | 87.6 to 85.5 |
| Server, in all | 66.4 to 54.3 | 110.0 to 90.3 | 237.7 to 158.6 | 191.7 to 130.5 |
| A client, simulating | 9.55 to 9.92 | 9.86 to 10.64 | 11.56 to 11.54 | 28.05 to 28.07 |

Three pairs an hour earlier, on a quieter machine, of a build that differed only in which checks write (it wrote
none past half the world's rows and 4,096): repairs 16.5 to 3.17, 47.3 to 8.69, 97.5 to 20.1 and 66.4 to 12.5 ms a
second; hashing 5.99 to 6.70, 12.8 to 14.1, 22.2 to 22.0 and 19.0 to 21.8; in all 58.7 to 45.9, 128.1 to 90.4,
227.4 to 145.0 and 191.3 to 139.6; simulating and a client's within 3% each way.

A repair now costs the chunks it sends (`tide_client_put_record` of each) and one hash of the base world (12 µs at
6,490 chunks; a world's hash goes through every page's kept hash). What the checks pay for it: a `tide_world_hash`
a check and the writing, about 0.7 to 1.5 ms a second of the hashing row at 8 and 16 bots (the hash 0.7 of it at 8:
24 ms of 2,017 checks). Counted (`bench_voxels_counts`): with 8 bots 1,114 repairs by the journal and none walked
(the other 8 packs are the bots' first worlds); 81,584 rows looked at, 73 a repair, where each walked 6,490 rows
twice; 153,864 rows written down by 2,017 checks, 76 a check. With 32 bots 5,157 by the journal and none walked,
96 rows a repair of 12,669, 265 written a check.

**Tests.** `tidec_e2e_visibility_repairs` (and `_checked`, `_memo_off`, `_wide`; `e2e/visibility_repairs.tide`, `_test.c`):
1,400 cells of their own groups, each with a `Secret` its Visibility restricts and a `[Sparse]` `Badge` one player
may have, 200 more in a scene that goes by its root's group, 48 pebbles all alike. 240 ticks, each with one change
picked at random of thirty: a group's audience and back (an entity coming into and leaving a player's sight with
nothing of its row changed), a lane everyone has, a lane some have, a grid's cell, a list grown, a policy set, reset
and set to nobody, a cell given another group and its own again, a `Tag` (another table) and back, a `Badge` given,
taken and changed, a cell destroyed, one made, one destroyed as another is made, forty made at once, a sixteenth of
the first 700 destroyed (holes the tick's end fills from the table's end: rows moved between pieces, and the table
shorter), the scene's group's audience, its root's group, the other group's audience, a group destroyed, a cell
snapped, every pebble destroyed as 48 are made (other entities in the same rows with the same values), and every
cell's value (most of the world's rows in one check). Each tick, every player's repair from the worlds 1, 2, 3, 5,
9, 14, 20 and 36 ticks old is worked out by the journal and by a lineage with nothing kept (every row), the two
byte for byte alike; and which way the first went is checked (`tide_client_changes_stats`): by the journal up to
14 ticks, walked at 36 (a journal of 32 checks here), and walked from a world before its own check and from a
world of a tick the match had but ticked on its own with another change (same tick, another world). Two worlds of
the past are repaired from each other both ways round, by the journal, though neither is the world the match
ticks. Then six ticks in a row that change every cell and twenty that change nothing, each checked twice (as a
hash asked for one player checks the same world again): the first four checks write their rows, the twelve after
write none (repairs across them walk), the sixteenth of the run writes and ends it.
6,772 repairs by the journal, 1,444 walked; their bytes and the world pinned, the same in the three variants with 4
players (`_memo_off`: which way each goes isn't checked). `_wide` is the same game with 160 players
(`visibility_repairs_wide.tide` in place of `_few.tide`: an `Audience` of five words, so no row written down keeps
one), every hash and every repair by the journal worked out twice; the same counts, its own pins. Every other visibility fixture's repairs from what's kept (one
tick old) go by the journal now too, against the rows' as before.

Each of these fails `tidec_e2e_visibility_repairs` (run one at a time against the final code and fixture): new
rows and other entities not written down; other entities alone (the pebbles); other snaps alone; a denied
component's term; any other term; a hole; rows past the table's end; an audience that changed; a component's
audience; who had a row that changed left out; who had a row that went left out; who may have a component whose
term moved left out; a check that wrote none taken as one with nothing to write; kept rows and the last node taken
for the world at any tick; a check with nothing to write made no node; nodes kept after the older half went left
where they were; the base taken as the node before without its hash; the world taken as the last node without its
hash; a run of checks that wrote most rows never ended; never begun (every check writes); no check counted as one;
the same world checked again ending a run, or counted as another check of it.
Two lines of the first build failed nothing and were taken out: writing a row down for which components it has
(their terms and audiences say it), and a never-seen row's audience in the audiences' part (the rows' part has it).

`bench_voxels_checked` at full length (8 bots, 32 bots, and 8 at `--near 12`, 30 s each: every repair by the
journal worked out again from every row) passes with the worlds and bytes above. Also passing, release on Windows:
all of `tidec_e2e_visibility_*`, `tidec_e2e_task_visibility`, `_scene_preparation_visibility`, `_shards_visibility`,
`_net_alone_seen`, `_storage_await_hidden`, `bench_voxels*`, `voxels_walk`, `bench_earth*`, `earth_tests`,
`tide_tests visibility` and `session`. Sanitizers, the web and the other platforms are CI's.

**Left.**
- The current tick's journal key now comes from `tick_run` (2026-11-19, above).
  The repair still looks up its base by its hash, which frozen worlds retain
  after the first read.
- A repair's own cost is its records: each chunk's grid encoded again for each player it comes to
  (`tide_client_put_record`), though the bytes are the same for all of them where nothing is denied.
- A game whose `Audience` is more than 128 players keeps no mask: every row written between two worlds is looked
  at for every recipient (still not the world; `_wide` is that path's test, not a measurement).
- The hash offer (`start_client_repair`'s fallback for repairs that are mostly entities the player has) still
  serializes the player's whole projection; nothing here touches it.

## Ticks in as many datagrams as the loss takes; inputs while they can be in time; protocol 26 (2026-11-16)

The largest bandwidth item of a real game (notes/voxels.md, At scale, Bandwidth): while the ticks a player hadn't
acknowledged fit one datagram together, the server sent all of them in every datagram (`again` was 0 while
`lacking + 32 <= TIDE_NET_MTU`), so each tick, with every player's changed input in it, went about as many times as
a round trip has updates: eight or nine at 133 ms. That was the session's way of losing no tick to a lost datagram:
the next datagram had it too. Once they didn't fit (32 players turning), each went once, and a lost one came 1.5 round
trips later. A player's own inputs went up the same way, every one until the server said it had a newer.

What a lost tick costs is more than the wait: a client's confirmed presentation clock (`verified_view`) advances with
real time and is held at the newest confirmed tick, so after the confirmed ticks stop for a round trip and come in a
burst it stays that far behind for good (until a repair, a reload or a resync sets it). What's drawn from the
confirmed world, and the view times a rewind goes by, are that much older from then on:
`tide_box3d_rewind_network`'s 20 frames a second with 10% lost had every stamp expire (350 ms old) with a first cut
of this that learned a network's loss by losing ticks. So ticks had to be lost in every copy almost never,
which is what the rule below is sized by. See Confirmed presentation below for the 2026-11-19 playback fix.

### What's built (`engine/src/session.c`; agent-maintained, no generated code or `tide_game` member involved)

- **A tick goes in a few datagrams in a row** (`send_frames`, `frame_goes`): `connection.frame_copies` of them, 2 at
  the least (`FRAME_COPIES`) and 8 at most (`FRAME_COPIES_MOST`). Each update's datagram has the tick just made and
  the ones before it that went fewer times than that (`sent_mark.sends`). So a lost datagram still costs nothing: the
  tick is in the next one.
- **How many is what the loss measured takes** (`servers_read`, `copies_for`): the fewest datagrams for a tick to be
  lost in all of them no more than once in 100,000 (`FRAME_LOST_ONE_IN`: once in 28 minutes at 60 ticks a second), by
  the share of the server's datagrams the player didn't read. 2 up to 0.3% lost, 3 to 2.1%, 4 to 5.6%, 5 to 10%, 6 to
  14.7%, 7 to 19.3%, 8 beyond. The loss is the client's own count, an authority, not a guess from timing: each CLIENT
  says how many SERVERs it has read so far (a byte that wraps) and echoes the time of the last, and the server
  remembers how many it had sent by each update's time (`said_marks`, the last 64 updates), so between two CLIENTs it
  knows how many it sent and how many came. One that came out of order shows as lost in one CLIENT and as one too
  many in the next, and is taken back (`loss_fresh`). The count is taken 512 SERVERs at a time (`LOSS_SERVERS`, 8.5 s),
  and sets the copies either way; and of the last 256 or fewer counted apart (`LOSS_SOON`), what's lost so far raises
  them at once, so a network that turns bad has four copies within a second and eight within a few. A connection
  starts at eight, as it was, until its first 512 are counted.
- **A tick lost in every copy goes again when a CLIENT shows it lost** (`FRAME_LOST`), not after a wait: the player
  read a datagram of a later update than the tick's last (`heard_said` past `sent_mark.said`) and still says it
  hasn't the tick. So it costs a round trip, where a tick sent once used to cost 1.5 (`resend_after`); and it adds a
  copy for the ticks after until the loss is measured again (a burst the average doesn't show). That it hasn't the
  tick is exact now: a CLIENT's bits are of the 64 ticks from the one it's at (`FRAMES_SAID`; they were of the 64
  after it), by their pieces, so a client that has a tick and hasn't run it yet (its clock, a repair, `Delay`) says
  so and isn't sent it again; before, that one tick went in every datagram. The mask is its newest CLIENT's (older
  ones come late), in games with projections too, where it was left as it was while a repair was asked for.
  With no word at all a tick still goes again after `resend_after`, as before.
- **Further copies never make a datagram, and take a quarter of one at most** (`FRAME_COPIES_ROOM`, 300 bytes: 18 KB
  a second at 60 updates), the newest ticks first: a tick's second datagram is worth more than an older one's fourth,
  and where they don't all fit, oldest first held every newer tick's copy back by a round trip (measured: 5% lost,
  verified worlds 4.69 ticks behind on average where 4.06 is what before had). Ticks too big for the quarter go once,
  as they did when what a player lacked didn't fit a datagram: 32 players turning every tick are 385 bytes a tick, and
  a second copy of that would be 15 KB a second more to each player than before. They gain the round trip in place of
  1.5.
- **A client's inputs go while they can be in time** (`send_client`, `INPUTS_SPARE`): the server says with every
  SERVER how long this machine's inputs wait for their ticks (`waited`, the same word `keep_early` goes by); an input
  sampled more ticks ago than that, one, and three to spare has had its tick run whichever datagram brings it, and
  isn't sent. Before, a CLIENT had every input since the newest the server said it had: a round trip's worth (9 or 10
  where 6 go now, at 100 ms and two ticks early), and all of them, to 255 or a datagram's room, while a world or a
  repair was on its way and no SERVER came. Tried with a fixed number first (a build not kept): the newest 4 gave the
  same share of late inputs as all of them at 1% and 5% lost and with 40 ms of jitter (0.00, 0.01 and 0.02%), 3 a
  little more (0.00, 0.02, 0.04%), 2 clearly more (0.02, 0.28, 0.35%).
- **The wire is `PROTOCOL` 26** (`session.c`, and the tests that write it by hand: `tests/test_commands.c`,
  `test_session_packets.c`, `test_session_view_time.c`, `packages/voice/test/voice_test.c`): a CLIENT's bits start at
  the tick it's at, and a byte follows them, the SERVERs it has read. Nothing else moved; a SERVER is as it was. No
  world, hash, snapshot, layout or generated code changed: this is what's sent of the same ticks.
- **Whatever a peer says** (hostile acknowledgements): the copies stay between 2 and 8; an echo of a time the server
  didn't say (past its last) is not taken; a tick goes at most once an update, in at most `PACKETS_PER_UPDATE`
  datagrams; a tick number past the server's is refused as before; bits for ticks that aren't made are the peer's own
  loss. A peer that says it never has anything, though it reads every datagram, is sent each tick it lacks once an
  update: what every player was sent before.

### What was weighed

The task's options, with 8 players standing and turning every tick (`bench_voxels --look 100 --still 100 --hop 0`,
120 s, 50 ms each way, 10 ms of jitter; KB a second of ticks to each player, then its verified world's ticks behind
the server's: on average / in 99 frames of 100 / in 999 of 1,000; measured with builds not kept, the room for
copies a whole datagram's):

| | none lost | 1% | 5% | 10% |
|---|---|---|---|---|
| Before: every unacknowledged tick in every datagram | 46.8, 4.00 / 4 / 4 | 46.9, 4.01 / 5 / 5 | 47.4, 4.05 / 5 / 6 | 48.0, 4.11 / 5 / 6 |
| Each tick once, again when shown lost | 7.2, 4.00 / 4 / 4 | 7.3, 4.49 / 13 / 14 | 7.5, 6.37 / 19 / 27 | 7.9, 8.52 / 24 / 36 |
| ...twice | 12.9, 4.00 / 4 / 4 | 12.9, 4.01 / 5 / 5 | 12.9, 4.23 / 12 / 19 | 13.0, 4.76 / 15 / 23 |
| ...three times | 18.5, 4.00 / 4 / 4 | 18.5, 4.01 / 5 / 5 | 18.6, 4.06 / 5 / 7 | 18.6, 4.19 / 7 / 17 |
| Twice, and once more for each tick lost in every copy, one fewer for each 4,096 acknowledged | 12.9, 4.00 / 4 / 4 | 14.2, 4.01 / 5 / 5 | 19.6, 4.07 / 5 / 11 | 23.6, 4.14 / 6 / 14 |
| As built: by the loss counted | 13.7, 4.00 / 4 / 4 | 19.2, 4.01 / 5 / 5 | 24.2, 4.06 / 5 / 6 | 24.2, 4.13 / 6 / 11 |

- **A set number of times** is right for one loss and wrong for the others: twice is as good as before at 1% and
  stalls one tick in 400 at 5%; three times pays for 5% on every network.
- **Sized by the ticks lost** (the fifth row; built first): it learns a network's loss by losing ticks, each a round
  trip's stall that the presentation clock then keeps, and it has to lose one now and then to know it can go down.
  With the loss counted, the copies are right before a tick is lost.
- **Older ticks every other update** is a set number of times spread out: the same bytes as half as many copies in a
  row, each a tick later.
- **Deltas between consecutive ticks' inputs**: a tick's inputs are deltas already, each from the slot's input before
  the tick; what repeated was the tick.
- **Not sending inputs of players a machine hasn't**: a slot any of whose entities is hidden from a recipient is
  withheld already (`client_input_masks`); voxels' avatars are public, so every machine runs every avatar and needs
  its input, or its own tick differs and a repair is sent in its place.

### Measured (`bench_voxels`; release, Windows, one run each: bytes, ticks and lags are the same every run of a build)

To each player and from it, KB a second, and how far behind the server's its verified world is, in ticks (on average
/ 99 frames of 100 / 999 of 1,000 / at most, from 5 s after it caught up: `bench.c` says so now, bench/README.md),
the share of its inputs that came too late, and ticks run for each played. 50 ms each way, 10 ms of jitter; 120 s
once everyone's in with 8 bots, 60 s with 32; before and after alternating.

| 8 players | | Ticks to each | All to each | From each | Behind: average / 99 of 100 / 999 of 1,000 / most | Late inputs | Ticks run a tick |
|---|---|---|---|---|---|---|---|
| Turning every tick, standing, none lost | before | 46.8 | 47.5 | 8.0 | 4.00 / 4 / 4 / 25 | 0.00% | 10.9 |
| | after | 13.7 | 14.4 | 6.8 | 4.00 / 4 / 4 / 25 | 0.00% | 10.9 |
| Turning every tick, standing, 1% lost | before | 46.9 | 47.7 | 8.0 | 4.01 / 5 / 5 / 32 | 0.00% | 11.8 |
| | after | 19.2 | 20.0 | 7.4 | 4.01 / 5 / 5 / 32 | 0.00% | 11.8 |
| Turning every tick, standing, 5% lost | before | 47.4 | 48.2 | 8.1 | 4.05 / 5 / 6 / 34 | 0.01% | 12.1 |
| | after | 24.2 | 25.0 | 7.8 | 4.06 / 5 / 6 / 37 | 0.00% | 12.1 |
| Turning every tick, standing, 10% lost | before | 48.0 | 48.9 | 8.2 | 4.11 / 5 / 6 / 46 | 0.01% | 12.2 |
| | after | 24.2 | 25.2 | 7.9 | 4.13 / 6 / 11 / 51 | 0.01% | 12.1 |
| Turning every tick, flying, none lost | before | 19.0 | 38.0 | 11.4 | 9.08 / 17 / 18 / 25 | 0.00% | 2.8 |
| | after | 9.6 | 28.6 | 8.5 | 9.08 / 17 / 18 / 25 | 0.00% | 2.8 |
| Turning every tick, flying, 1% lost | before | 18.0 | 37.6 | 11.8 | 9.58 / 23 / 31 / 41 | 0.00% | 3.0 |
| | after | 10.6 | 30.4 | 9.0 | 9.52 / 23 / 29 / 37 | 0.00% | 3.0 |
| Turning every tick, flying, 5% lost | before | 15.5 | 36.8 | 12.9 | 11.54 / 33 / 43 / 60 | 0.00% | 3.0 |
| | after | 9.7 | 31.4 | 9.3 | 11.11 / 32 / 47 / 59 | 0.00% | 3.0 |
| As they are, standing, none lost | before | 6.7 | 7.5 | 4.9 | 4.00 / 4 / 4 / 25 | 0.00% | 2.0 |
| | after | 3.1 | 3.9 | 4.6 | 4.00 / 4 / 4 / 25 | 0.00% | 2.0 |
| As they are, standing, 1% lost | before | 6.7 | 7.5 | 4.9 | 4.01 / 5 / 5 / 32 | 0.00% | 2.9 |
| | after | 3.8 | 4.5 | 4.7 | 4.01 / 5 / 5 / 32 | 0.00% | 2.9 |
| As they are, standing, 5% lost | before | 6.8 | 7.7 | 4.9 | 4.05 / 5 / 6 / 34 | 0.00% | 3.6 |
| | after | 4.7 | 5.5 | 4.8 | 4.05 / 5 / 6 / 34 | 0.00% | 3.6 |
| As they are, flying, none lost | before | 3.7 | 21.7 | 7.1 | 9.08 / 17 / 18 / 25 | 0.00% | 2.0 |
| | after | 2.0 | 20.1 | 6.3 | 9.08 / 17 / 18 / 25 | 0.00% | 2.0 |
| As they are, flying, 1% lost | before | 3.5 | 22.4 | 7.2 | 9.48 / 23 / 31 / 42 | 0.00% | 2.1 |
| | after | 2.4 | 21.2 | 6.4 | 9.52 / 23 / 29 / 37 | 0.00% | 2.1 |
| As they are, flying, 5% lost | before | 3.2 | 23.6 | 7.6 | 11.34 / 33 / 43 / 64 | 0.00% | 2.2 |
| | after | 2.6 | 23.4 | 6.6 | 11.30 / 31 / 41 / 64 | 0.00% | 2.2 |

| 32 players | | Ticks to each | All to each | From each | Behind: average / 99 of 100 / 999 of 1,000 / most | Late inputs | Ticks run a tick |
|---|---|---|---|---|---|---|---|
| Turning every tick, standing, none lost | before | 20.2 | 21.4 | 8.0 | 4.00 / 4 / 4 / 25 | 0.00% | 10.9 |
| | after | 20.7 | 21.9 | 6.8 | 4.00 / 4 / 4 / 25 | 0.00% | 10.9 |
| Turning every tick, standing, 1% lost | before | 20.5 | 21.8 | 8.0 | 4.88 / 16 / 19 / 33 | 0.00% | 10.7 |
| | after | 20.8 | 22.1 | 7.3 | 4.47 / 13 / 16 / 33 | 0.00% | 11.0 |
| Turning every tick, standing, 5% lost | before | 21.6 | 23.0 | 8.1 | 8.11 / 26 / 34 / 48 | 0.01% | 8.2 |
| | after | 21.6 | 23.0 | 7.7 | 6.25 / 19 / 25 / 41 | 0.00% | 9.1 |
| Turning every tick, flying, none lost | before | 16.8 | 41.8 | 11.4 | 9.70 / 17 / 18 / 25 | 0.00% | 2.4 |
| | after | 17.0 | 42.0 | 8.6 | 9.70 / 17 / 18 / 25 | 0.00% | 2.4 |
| Turning every tick, flying, 1% lost | before | 15.1 | 41.2 | 11.8 | 10.52 / 24 / 30 / 42 | 0.00% | 2.4 |
| | after | 16.1 | 42.2 | 9.0 | 10.46 / 24 / 30 / 42 | 0.00% | 2.4 |
| Turning every tick, flying, 5% lost | before | 11.5 | 39.4 | 13.3 | 13.14 / 36 / 46 / 60 | 0.00% | 2.5 |
| | after | 13.6 | 41.3 | 9.4 | 13.08 / 36 / 49 / 75 | 0.00% | 2.5 |
| As they are, standing, 1% lost | before | 6.1 | 7.4 | 4.9 | 4.01 / 5 / 5 / 39 | 0.00% | 3.0 |
| | after | 3.6 | 4.9 | 4.7 | 4.01 / 5 / 5 / 39 | 0.00% | 3.0 |
| As they are, flying, 1% lost | before | 4.1 | 27.6 | 7.3 | 10.37 / 24 / 30 / 42 | 0.00% | 2.0 |
| | after | 3.0 | 26.4 | 6.5 | 10.34 / 24 / 30 / 42 | 0.00% | 2.0 |

- **Ticks cost three tenths to three fifths of what they did** wherever a second copy fits, at every loss: 8 players
  turning, standing, 46.9 to 19.2 KB a second with 1% lost (13.7 with none, 24.2 with 5%); flying, 18.0 to 10.6.
  With bots as they are (inputs that change every five seconds) 6.7 to 3.8 and 3.5 to 2.4. The runs' first 8.5 s for
  each bot are at eight copies, as before, so a longer match's average is a little under these.
- **Lossy play is as it was** to 5% lost: standing, the same ticks behind on average, in 99 frames of 100 and in 999
  of 1,000, the same share of late inputs, the same ticks run again; flying, where repairs decide how far behind a
  client is, within what they vary by. With 10% lost the quarter of a datagram holds 8 players' ticks to four copies
  where the loss asks six: 4.13 ticks behind on average for 4.11, 6 for 5 in 99 of 100, 11 for 6 in 999 of 1,000.
- **32 players turning** were sent each tick once before, and are now (385 bytes a tick is past the quarter), with a
  lost tick back in a round trip where it took 1.5: standing, with 1% lost, 4.88 ticks behind on average to 4.47 and
  16 in 99 frames of 100 to 13; with 5%, 8.11 to 6.25 and 26 to 19, for the same bytes within half a kilobyte a
  second. Flying, where a repair often overtook a lost tick before the server's wait for it ran out, sending it at
  once costs more and gains nothing: 15.1 to 16.1 KB a second of ticks with 1% lost, 11.5 to 13.6 with 5% (39.4 to
  41.3 in all), as far behind as before. A second copy of each tick (a whole datagram's room, measured standing with the build
  before the quarter) made 4.88 and 16 into 4.02 and 5 with 1% lost, for 33.7 KB a second where it was 18.9: the
  owner's to weigh (`FRAME_COPIES_ROOM`).
- **From a player**: 8.0 to 7.4 KB a second standing and turning, 11.8 to 9.0 flying (a CLIENT had every input
  since the last SERVER while a repair came, which is much of the time there), with the same share late. The bots'
  inputs wait three ticks, so they keep seven; a session's player, a frame early, keeps five.
- **A match that's quiet** (`net_a_match_costs_little_to_keep_going`, two players, a tick's hash and little else): 7
  KB a second to each player before, 3.0 now (60,100 bytes on the wire in 10 s for 141,092), and 2.5 from each
  (25,462 for 25,955: a byte more a CLIENT, fewer inputs in it). The test measures from the tenth second now, past
  the first 512 SERVERs, and its ceilings are 50,000 and 22,000 bytes of payload where they were 140,000 and 28,000.
- `bench_gallery` (8 players, a world with no projections, 30 s): ticks 16.9 to 8.9 KB a second to each with 1%
  lost, 17.0 to 11.3 with 5%; 4.01 and 4.06 ticks behind on average before, 4.01 and 4.05 after; late inputs 0.03%
  both, and 0.19% to 0.06%.

### Tests

`tests/test_session_packets.c`, a raw peer that says what it likes of the ticks and datagrams it has and counts how
often each tick comes, and a real client whose server's datagrams a transport loses:

- `session_frames_go_in_two_datagrams_then_wait_for_word_of_them`: with the loss counted and none lost, every tick
  twice, one datagram an update whatever the peer lacks; with no word of it for 1.5 round trips, once more.
- `session_frames_go_in_eight_datagrams_until_the_loss_is_measured`.
- `session_frames_shown_lost_go_again_at_once_and_once`: a tick the peer says it hasn't goes in the next update's
  datagram, once; not for a CLIENT that echoes the update its last copy went in, nor for the same word again, nor for
  an older CLIENT come late; the tick it's at, which it has and hasn't run, doesn't; two ticks lost at once add one
  copy, and one shown lost twice counts once.
- `session_frames_copies_follow_the_loss_measured`: 1 in 100 lost, three copies; 1 in 8, six; none, two again; 1 in
  5 counted a CLIENT late (out of order), two; 1 in 4, four or more within 64 updates; all lost, eight and no more.
- `session_frames_hostile_acknowledgements_cost_a_bounded_number_of_datagrams`: an echo from the future, tick
  numbers past the server's, every bit set, any count of datagrams, a peer that never has the last twenty ticks.
- `session_frames_further_copies_take_a_quarter_of_a_datagram_at_most`: ninety ticks owed a copy get it a quarter
  of a datagram at a time in the one datagram an update sends, the newest first, and the ones older than 64 ticks
  don't.
- `session_frames_a_lost_datagram_stalls_nobody_and_two_cost_a_round_trip`: 50 ms each way; a datagram lost leaves
  the client's verified world no further behind than a tick more; two in a row lose a tick in both its copies, which
  comes within nine ticks, after which two in a row stall nobody (the raw peer's tests are what tell a tick shown
  lost from one the server stopped waiting for).
- `session_inputs_go_only_while_they_can_be_in_time`: a CLIENT has the newest six inputs (two ticks' wait, the next,
  three to spare) where it had nine or ten; with every SERVER lost for half a second it has no more, and no input is
  late for it.

Each of these fails one of them (25, with the code changed back afterwards): no tick shown lost going before the
wait; shown lost whatever the echo, or by the same update's; one copy at the least; every tick every update as
before; any echo taken; a lost tick adding no copy, or one each time; the tick it's at never said; the client's bits
from the tick after; copies whatever the loss, past eight, never sooner than a whole count, never lowered, two before
anything is counted; copies in a whole datagram's room, in any room, to the oldest first; datagrams out of order
counted as lost; an older CLIENT's bits taken; SERVERs not counted by either side; every unacknowledged input in a
CLIENT, or none to spare; nothing again when no word comes.

**Pins.** No world hash moved: `bench_gallery`, `bench_voxels` (and `_forgets`, `_wide`, `_checked`, `_counts`),
`bench_earth` (`_split`, `_diverge`), `bench_sand`, `bench_flock` and every e2e hash as they were. Bytes on the wire
did, on purpose: `tidec_e2e_local_events_same` and `_plain`'s clients' 15,332 to 15,254 (352 CLIENTs a byte more
each, fewer inputs in them; the server's 33,428 the same, as three seconds are within the first 512 SERVERs), and
`net_a_match_costs_little_to_keep_going` above. `visibility_large_repairs_release_old_callbacks_on_compatible_reload`
(`tests/test_visibility_session.c`) goes by which datagrams its network loses, and its seed moved from 61577 to
61579: over 61 seeds (1 to 40, 61570 to 61590) it passed 22 with the session before and passes 47 now; see Open,
below, for the 4 and 3 where it stops.

**Validation** (release, Windows; sanitizers, the web and the other platforms are CI's): `tide_tests` (766),
`tidec_e2e_net`, `_net_no_input`, `_net_ended`, `_net_alone_seen`, `_net_join_big`, `_seen`, `_guess`, `_predict`
(and `_near`, `_near_split`, `_kept`, `_owned`, `_owned_split`, `_range_control`, `_fields`, `_method`), `_view_time`,
`_handover`, `_players`, `_players_ceiling`, `_commands`, `_commands_channels`, `_command_lists`, `_local_events`
(`_same`, `_plain`), `_visibility_lifecycle`, `_host_lifecycle`, `_projection`, `_predicted`, `_lists`, `_groups`,
`_runs`, `_pieces`, `_blocks`, `_trees`, `_hash_memo`, `_shards`, `_shards_players`, `_shards_visibility`,
`_task_visibility`, `_storage_await` (`_shards`, `_hidden`), `_asset_sharing`, `_scene_preparation_visibility`,
`_optional_fields`, the benches above, `earth_tests`, `tide_voice`, `tide_voice_host`, `tide_box2d_rewind_network`,
`tide_box3d_rewind_network`, `tide_box3d_projected`, `tide_platform_travel`, `tide_server_smoke`, `_pack`, `_room`,
`_shards`, `_shards_room`, `_shards_journal`, `_entry`, `tide_network_udp`, `tide_network_hosted`, `tide_rooms_lobby`,
`tide_lobby_own`, `tide_lobby_relay`: 78 CTests, all pass.

### Open

- **A hosted match of the build before refuses this one** (`PROTOCOL` 26): the docs' Earth and anything on managed
  hosting need deploying again with it.
- **The quarter of a datagram** (`FRAME_COPIES_ROOM`) is a pick: with it nothing measured is sent more than 2 KB a
  second over what it was. A whole datagram's room gives 32 players turning a second copy (33.7 KB a second for
  18.9, no stall for a lost datagram) and 8 players at 10% lost their sixth. So are once in 100,000 ticks, 512 and 256 SERVERs, the
  eight a connection starts with, and three inputs to spare.
- **The confirmed presentation clock keeps a stall** (fixed 2026-11-19, Confirmed presentation below): a tick lost in
  every copy can still stall the view until confirmation recovers. Retransmission settings still use the rules above.
- (Fixed the same day: the top section, A whole projection the server starts is taken.)
  **A reload during a stream of repairs can stop a client for good**, before this change and after
  (`visibility_large_repairs_release_old_callbacks_on_compatible_reload` with seeds 14, 25, 61572 and 61584 on the
  session before; 25, 61572 and 61577 now): the server starts the world again for the new build (`SEND_CLIENT_WORLD`)
  and sends it for the rest of the run, the client acknowledges no chunk of it and its verified tick stays where the
  reload found it (141 to 177 of 750). Not looked into further: it's the reload's and the repairs', not the ticks'.
- A big command's download that a tick names and the client hasn't whole no longer makes the server send that tick
  again (the client says it has the tick's pieces; the download is acknowledged on its own). No test has a download
  later than its tick.
- A client more than a second's round trip from its server echoes updates the server no longer remembers
  (`SAID_KEPT` 64), so its loss isn't counted and its ticks go in eight datagrams, as before.

## Projection hashes: the rows that changed, and big containers by their chunks (2026-11-16)

Two costs the blocks (the section below) left in a tick's check of a world of thousands of chunks: a piece of 64 rows
read for one of its columns had each of its rows looked at (533k for 119k that changed), and a container that
changed was encoded and hashed whole (a dimension's `Streamer.chunks`, 6,490 entries, 105 µs each time). Now
(`client_spans_codegen.inc`, `client_state_codegen.inc`):

- **A piece keeps a copy of its columns' runs** (`tide_client_piece.lanes`: every column's 64 values, side by side)
  beside each run's hash (`runs`). A written column's run that hashes otherwise is compared with the copy value by
  value (`tide_client_rows_differ`): `tide_client_piece_same` says, for each component of the archetype, which rows
  of the piece a lane of it changed in, a bit a row (`changed`), and the same for the entity's and the scene's columns
  (`ids_rows`), and makes the copy and the hash the run's. So what says a row changed is its own bytes, exactly: the
  per-row digests of each component's lanes (`tide_client_rows.digests`, `tide_client_lanes_digest`) are gone, and a
  piece's memory is its rows' bytes once more (about 250 bytes a chunk in voxels) where it was 8 bytes a component a
  row. The hash stays because comparing every written run with its copy read twice the memory: the pieces' checks
  were about 12 ms a run dearer without it (counted: 165k runs compared over a run, for 10k pieces read). A piece that
  isn't kept whole (its rows are more or fewer: the table's last; or a row of it reads outside the heaps) still has
  its copy, so the rows it had are compared the same way and only the new ones are read whole (the first build read
  every row of such a piece: 6.5k more rows from the heap over a run, which the counts showed).
- **A piece read again keeps its arrays** (`inplace`): its rows' blocks (`row_pages`), its pages and their hashes
  stand while no row of it reads the heap again or is another entity than it was, and while they stand only the rows
  with a bit are looked at (`look`). A chunk whose `Seen` changed moves its term and its sum where they are; nothing
  of the 63 rows beside it is touched. The first row that reads the heap again, or is another entity, has the piece's
  arrays made again as before: the rows before it give the blocks and slots they had
  (`tide_client_piece_rows_again`), the rows after go on as they did. A slot's page of the piece's that changed makes
  every row's slot looked at (`tide_client_row_slot_same`: its entity there still, with the snaps it had), and a
  piece whose arrays stood takes its slots' pages' hashes as they are (`tide_client_piece_keep_slots`).
- **Who may have a row is worked out again only where what that goes by changed in the row** (`seen`): its scene
  (the entity's and the scene's columns), its Visibility's lanes or policies, or which of its components it has (a
  sparse one's presence byte). Before, any term of it that changed did (a chunk's `Seen`, its `Chunk`'s stage).
  `tide_client_rows.fresh` and `tide_client_piece.fresh` mean that now.
- **A container too big for one chunk of its list is a tree of its chunks** (`tide_client_sum`,
  `tide_client_memo_sums`, `tide_client_sum_kept`; `gen_client_hv`). A map or a set whose live entries are more
  bytes than `TIDE_LIST_CHUNK` is the sum of its entries' hashes, each `tide_client_mix(tide_hash(order, key,
  value))`: an entry's order is part of it, so the sum says the map with its order, wherever its entries are stored
  (the server's has dead entries among them, a player's machine's hasn't). A list whose elements are more bytes than
  that is the hashes of its elements a chunk's worth at a time (`TIDE_LIST_CHUNK` over an element's size, by index,
  not by how the list is stored), chained in order. What's hashed of the container is then a byte saying which (1, a
  sum; 2, chunks' hashes) and those 8 bytes. Which way a container goes is its count's alone, so a server and a
  player's machine agree, and so do a map that grows past a chunk and one that shrinks back.
  Each chunk's part is kept with the memo's entry for the container (`tide_client_memo_entry.sums`) where the entries
  or elements hold nothing in the heap (they are what their bytes are): a chunk whose page hashes as it did, with as
  many elements read of it, isn't read. Entries that hold lists or texts are summed without keeping (their own
  containers are the memo's as before). A list of elements whose bytes are their codec's is hashed where it is, a
  chunk at a time, with no copy.
- **A grid's cells whose bytes are their codec's go a chunk at once** (`tide_client_write` of the chunk) where each
  cell was a call: the same bytes, 3.7 µs a 16-cubed chunk of bytes to 1.1 (counted).
- The directory's copy reads its entries through a list cache (`tide_list_cached_at`) where each was a lookup.

**Pins.** Hashes of rows and of containers that fit a chunk are what they were: `bench_voxels`' world hash and bytes
sent, and every projection pin but one. A container too big for a chunk hashes to another number, by its nature
(another function of the same value), so `visibility_hash_memo_test.c`'s pin moved, `0xa15bc20786198050` to
`0xc8f1048a1b38d2a4` (its catalog's 40,000 bytes are a tree now; nothing else of it is more than a chunk), the same
with the memo on and off; a player's machine's own world, loaded from what it's sent, hashes to the server's in both
(that test, and the new one's every tick). Simulation identity: `singleton and entity tree hashes 3`
(`asset_usage.c`), so a build from before joins no match of this one. `TIDE_CLIENT_SPANS_CHECK` (`bench_voxels_checked`
with 8 bots for 30 s and quick, the fixtures' `_checked`) and `TIDE_CLIENT_MEMO_OFF` pass; kept rows are still exactly
the world's at `tide_client_kept.tick` (every fixture's repairs from what's kept against the rows'). No runtime change
(notes/runtime-ownership.md, Projection hashes: rows by their bytes, big containers by their chunks).

**Measured** (`bench_voxels`, 8 bots, 30 s, 4 threads, release, Windows, BelowNormal, one at a time, three runs
alternating with dev's build of the same tree; medians, ranges in brackets): server hashing (the world's hash and
the projections') 9.40 to 6.95 ms a second (9.40–9.50 to 6.00–6.99), update median 0.91 to 0.82 ms (0.88–0.91 to
0.72–0.83), p99 6.07 to 5.46 (6.04–6.24 to 5.33–5.73), simulate the same (32.0 and 31.3); a client's hashing 0.79
to 0.59 ms a second (0.79–0.81 to 0.52–0.59), its update median 0.16 both, p99 2.26 to 2.18. The same bytes sent
(186.8 KB a second) and world hash (0x77babb9991dd25da) in all six. An earlier three pairs, with another agent's
builds running: 10.06 to 7.31 (9.03–11.78 to 7.02–8.29). Standing still (`--still 100 --hop 0`, three pairs): hashing
0.66 to 0.64 ms a second (0.65–0.71 to 0.64–0.65), update median 0.08 both, p99 0.61 to 0.59; a client's 0.34 both.
Counted over a run (`bench_voxels_counts`, 2,017 checks; the two runs an hour apart on a shared machine: `sums`,
which nothing here touched, took 19.2 ms in the first and 15.8 in the second, so about a fifth of every drop is the
machine's):

| 8 bots moving | before | after |
|---|---|---|
| pieces read: a column's run / a heap page / nothing kept | 10,381 / 131 / 466 | the same |
| ...of which their arrays stood | 0 | 6,667 |
| rows looked at in pieces read | 533,328 | 280,038 |
| rows read from the heap again | 50,873 | 50,873 |
| rows something changed of (`rows fresh`) | 119,491 | 119,491 |
| big containers' chunks read again / kept | (4,948 containers whole, 476 not grids) | 1,079 / 1,676 |
| rows of pieces read | 125.5 ms | 43.7 |
| ...rows read from the heap again | 69.5 | 26.7 |
| ......maps and sets encoded | 33.8 | 4.7 |
| ......grids encoded | 16.4 | 4.7 |
| pieces checked | 80.3 | 73.9 |
| pieces kept | 11.2 | 6.0 |
| audiences / sums / groups | 27.9 / 19.2 / 12.2 | 22.3 / 15.8 / 8.3 |
| all of a check's parts | 279 ms | 172 |

The rows still looked at that nothing changed of (160k) are those of pieces whose arrays are made again (a row of
them read the heap again: new chunks come a few dozen at a time at a table's end), which every row of gives its
blocks to. `visibility_trees_measure` (a book of a map of 8,000 entries and a list of 40,000 ints, 4 players, 200
ticks each, three runs alternating with dev's build, medians): one entry set where it is each tick 177.6 to 11.6 µs
a check; an entry gone and another added 178.2 to 18.2; one number of the list 30.4 to 4.5; nothing changing 1.3 to
1.4.

**Tests.** `tidec_e2e_visibility_trees` (and `_checked`, `_memo_off`; `e2e/visibility_trees.tide`, `_test.c`): three
vaults, each a map of 2,000 entries, a set of 2,500, a list of 9,000 ints, one of 5,000 structs with padding, a map of
800 lists, a list of 2,200 texts and a map of 1,023 entries (a chunk's worth exactly); a singleton with a map of
3,000 and a list of 5,000; a book (the measurement's); 300 tiles with a small list, a `Tag`, and a `[Sparse]` `Badge`
one player of their audience may have. Thirty-eight changes: an entry where it is, in a map's first chunk and its
last; two keys taken out and put again, against a twin that puts them the other way round (only the entries' orders
tell them apart); one gone and another added with the count the same; a key put again with its value; a map growing
across chunks, shrinking, under a chunk's worth and over again; the set's; an element where it is, two swapped
between chunks, a list growing across chunks, its first element gone (every other moves along), down to one element
and up again, its last element gone where that's a 0 (its chunk's bytes stay: only the count says); an element with
padding; a list held by a big map's entry; a text of a big list; the map of a chunk's worth one over, changed there,
and back; the singleton's; three rows' small values and a later row's list in one piece and the other way round;
one of those values back as it was; that list's element where the list is now; a sparse component given and taken
(its byte in the row, back as it was); a row of the last piece while it gets another; a row gone, made and snapped
beside one that changes. Quiet, then with lists growing all over. Every tick, every player's hash is the one a copy
of the world hashes to with nothing kept, and the one that player's own world hashes to, loaded from what it's sent
(another layout, its maps' live entries alone, hashed with what the player's memo kept of the tick before's world);
a repair from what's kept is the rows'; each change shows against its twin; pinned, the same in all three. Each of
these fails it, or the pieces' or blocks' fixture: a chunk's page hash not compared, a chunk's count not compared
(the 0 gone), chunk sums kept for entries that hold lists, an entry's order left out of its hash (the twin), only
the first changed row of a run seen, a kept piece's copy not made the run's, nor its hash (the value back; the byte
back), a piece's arrays left standing though a row read the heap again, a Visibility's lanes not working its
audience out again (pieces), nor which components it has (the badge), a row's slot not looked at where a slot's page
changed (the snap, here and in blocks), rows whose entity or scene changed not looked at (pieces' rover). The pin of
`visibility_hash_memo` moved (above). All of `tidec_e2e_visibility_*`, `tidec_e2e_task_visibility`,
`_scene_preparation_visibility`, `_shards_visibility`, `_net_alone_seen`, `_storage_await_hidden`, `bench_voxels*`,
`bench_earth*`, `earth_tests` and `tide_tests visibility` pass in release on Windows; sanitizers, the web and the
other platforms are CI's.

**Left.** The pieces' checks are what a check mostly costs now (74 of 172 ms a run): 1,866 heap pages' hashes looked
at a call (25 a piece), 20 ms of blocks hashed again for another's block in their page (209 MB a run), 8 ms of
slots' snaps. Pages written since the last call, kept by the runtime (a heap's list of them, as the world's hash
finds them stale), would let a check go from the pages that changed to the pieces that read them, and look at no
other: a runtime change, the owner's to agree. What a piece's rows add to each player's sum is still added up whole
for a piece any row of which changed (16 ms a run, 64 rows for the 11 that changed): taking the old rows' part away
and adding the new would need each row's before its terms move. A big map whose entries hold lists or texts is
summed whole when it changes (no chunk kept), and a grid is encoded whole (a chunk's grid here is one chunk; a grid
of many chunks would want each chunk's hash kept as a list's are). A big list an element of which is inserted or
removed early has every chunk after it hashed again, as its elements all moved.

## Projection hashes kept by blocks (2026-11-15, later)

The pieces below left most of the check's reading to heap pages that changed for other entities' blocks: with 8 bots
moving in `bench_voxels`, 62% of the pieces read were read for a heap page, 233k rows were read from the heap again for
their pages alone and 14k grids encoded again for their directory's page. Now what's kept says which blocks it read
(`client_state_codegen.inc`, `client_spans_codegen.inc`):

- **A block, not a page** (`tide_client_item`: key, the page's hash, the block's own): a block smaller than a page in
  a page of one place is kept by its key (`tide_client_key`: the page's place above 15 bits, below them its size class
  plus one and its offset in 16 bytes; 0 there for a page whole, a list's chunk or a block of a page or more), with its
  page's hash and the hash of its `16 << class` bytes (`tide_hash_wide`). Its class is its header's, or for a grid's
  chunk, whose header is cells (grid.c), the one its reader says (`tide_grid_chunk_bytes`): the first build took a
  chunk's class from its cells, hashed 16 bytes of a 4 KiB chunk and missed a cell written beyond them
  (`visibility_pieces`, "a cell of a grid, where it is", caught it). `tide_client_item_same`: the page hashes as it did,
  or it doesn't and the block's bytes do, in which case the page's hash now is kept in the item's place, so a page that
  changed is hashed by its blocks once. Memo entries, rows' own pages and pieces' rows all keep items; a memo hit hands
  its items (hashes and all) to the row that read the container, and `tide_client_memo_put` works out the rest.
- **A piece**: its union of heap pages is as it was (sorted by heap and place now, so each heap is found once); a page
  of it that hashes otherwise has each item of its rows in that page checked (`tide_client_piece_blocks_same`) and,
  every one as it was, the page's new hash is kept and the piece passed over. In a piece read, a row's items are checked
  as the piece's were, so a row is read from the heap again only where a block of its own changed.
- **A slot's page** that changed (an entity of it came or went) has the snaps of the piece's rows whose slots are in it
  looked at (`tide_client_piece_snaps_same`; each row keeps its slot and snaps as its identity was last hashed, and a
  slot whose entity isn't the row's says it moved), so what says which entity a row is isn't hashed again for another
  entity's turnover. 14.5k pieces were read for a slot page alone once the heap's reads stopped hiding them.
- **The directory of groups** is copied entry by entry, as its list has them, live and dead
  (`tide_client_groups_diff`): a chunk of the list whose page hashes as it did isn't read. An entry that moved (the map
  packs its live entries once most are dead) takes both its old and new ids as changed: more audiences worked out
  again, never fewer. The ids are sorted afterwards; the old merge's "ids go up" fallback is gone with it.
- **What a piece's rows add to each player's sum** is kept with the piece (`route_players`, `route_sums`) while none of
  its rows was read again or had its audience worked out again (`routed`); a piece adds it for the players asked.
- A page's kept hash is read where it's of the bytes in use (`tide_client_page_hash`, as cached functions' keys read
  it), with no call into heap.c (notes/runtime-ownership.md, Projection hashes kept by blocks).

The hashes are of values, as before: no pin moved (`bench_voxels`' world hash, its bytes sent, every projection pin);
`TIDE_CLIENT_SPANS_CHECK` (`bench_voxels_checked`, 8 bots and quick, `tidec_e2e_visibility_*_checked`) and
`TIDE_CLIENT_MEMO_OFF` pass; kept rows are still exactly the world's at `tide_client_kept.tick`.

**Measuring**: `TIDE_CLIENT_COUNTS` (`bench_voxels_counts`, built beside the bench and run quick as a test) counts what a server's
checks read again and why, and how long each part took, printed at the program's end.

**Measured** (`bench_voxels`, 8 bots, 30 s, 4 threads, release, Windows, BelowNormal, the machine shared with another
agent's builds; three runs alternating before and after, medians, ranges in brackets): server hashing 18.1 to 9.5 ms
a second (17.8–18.2 to 8.5–9.9), update median 1.08 to 0.96 ms (1.07–1.08 to 0.83–0.97), p99 6.48 to 5.71
(6.27–6.73 to 5.54–5.81), simulate the same, the same bytes sent and world hash. Standing still (`--still 100
--hop 0`, two runs each): hashing 1.56 to 0.80 ms a second, update median 0.11 to 0.10, p99 0.65 to 0.62. A bot's
machine runs the same code for its own projection check (review, same day: three more alternating pairs, the same
settings, before built from the parent commit's two codegen files): a client's hashing 1.41 to 0.82 ms a second
(1.33–1.43 to 0.80–0.82), update median 0.17 to 0.17 ms (0.16–0.17 both), p99 2.63 to 2.33 (2.48–2.70 to 2.29–2.39);
the server's hashing in those runs 17.8 to 9.8 (16.5–18.2 to 9.4–10.0); world hash 0x77babb9991dd25da and bytes sent
the same in all six. Counted over a run (2,017 checks):

| 8 bots moving | before | after |
|---|---|---|
| pieces read: a column's run / a heap page / a slot's page / nothing kept | 10,381 / 15,874 / 5,319 / 466 | 10,381 / 131 / 0 / 466 |
| rows in pieces read | 1,845,659 | 533,328 |
| rows read from the heap again (for their pages alone) | 284,188 (233,474) | 50,873 (159) |
| grids encoded again | 14,026 | 4,472 |
| blocks hashed again (found as they were) | 0 | 397k (391k), 209 MB |

Rows fresh (something of them changed) 119,491 both. Where a check's 0.13 ms go now (a counting build, totals over
the run): pieces checked 76 ms (blocks of changed pages 20, slots' snaps 8; 3.8M heap pages looked at, 25 a piece: a
chunk's grid chunk is two to a page), rows of pieces read 118, audiences 27, sums 18 (41 before the routing kept),
groups 12 (64 before the directory went by chunks), pieces kept 11. Standing still: 1.0 µs a check of rows read, 2.5 of
pieces checked. `visibility_pieces_measure`: nothing changing 18.5 to 5.8 µs a check, every cell's `Cell` 803 to 745,
every `Secret` 272 to 268.

**Tests.** `tidec_e2e_visibility_blocks` (and `_checked`, `_memo_off`; `e2e/visibility_blocks.tide`, `_test.c`):
1,200 plots each with a group, a grid of two chunks of 16 cubed (one in use), a map of 40 entries (the memo's) and one
of 3, a small list; sixteen changes, each to a plot or the one made before it, whose blocks share its pages: the
neighbour's grid chunk, then the plot's the tick after; the same for the small map and the big one, in place; the
plot's grid gaining a chunk (its directory, among others'); its list let go, the neighbour given a list of the plot's
old bytes where it was, then one of other bytes where the neighbour's own was; five groups of every eight destroyed, a
group made (the directory packed), a group's audience whose entry moved; a plot snapped (`this.Snap()` in a system
reading `Plot`: its slot's page changes by its snaps alone); a plot destroyed and another made in its slot.
Quiet and with lists growing all over; each tick every player's hash against a copy's with nothing kept and repairs from
what's kept against the rows'; each change shows, against a twin of the world without it, both hashed by copies and the
twin given the world's `Control` (a public singleton, so before, every step "showed" by its step number:
`visibility_pieces_test.c` had the same, fixed too; its quiet round shows every step that way). Pinned, the same in all
three. Each of these fails it: a block's bytes not compared, a grid chunk's size from its header, a directory chunk
taken as unchanged by its list alone, a piece's routing kept across its audiences worked out again, a piece's changed
page taken as same without its blocks, a slot's page taken as same without its rows' snaps (the snap step). The pieces, runs, projection, hash memo, lists, groups, predicted and lifecycle
fixtures, `tidec_e2e_grid_bytes` and `bench_voxels*` pass; `bench_voxels_checked` with 8 bots for 30 s too.

**Corrections** (review, same day): the first version of this section said the snaps compare had no failing test,
as `this.Snap()` on a plot changed no player's projection hash. It does: a snap of a live plot (step 16, run before
the step that destroys the same target) shows against its twin, and with `|| slot->snaps != r->snaps[row]` removed
from `tide_client_piece_snaps_same` the fixture fails at that step (player 0's kept hash isn't the world's, and its
repair isn't the rows'). Snapping in `Change` (`mut Plot`) shows as well. The earlier attempt's cause wasn't kept; the
likeliest is a snap of a target already destroyed by step 14. A row whose entity has no slot (`tide_entity_slot_index`
gives `UINT32_MAX`: not on a server, whose tables hold no AWAY entities) gathered slot page `UINT32_MAX >> 10`, which
hashes 0 forever, so the slot the entity got later went unseen while its row and entity column stayed; such a row now
makes its piece not kept (`poisoned`), as a row that read outside the heaps does. No test reaches that state through
the language. `bench_voxels_counts` runs as a test now (notes/benchmarks.md).

**Left**: a container that changes is still encoded whole (`Streamer.chunks`, 6,490 entries, 105 µs): a tree of its chunks would
be another function of its value, and every projection pin would move, so it isn't done here; resuming its hash from a
kept prefix would need `tide_hash` to be resumable at a chunk's edge. Heap pages looked at are a piece's whole union
each call (25 a piece in voxels); a heap's pages written since the last call, kept by the runtime, would let a piece
look at none. (2026-11-16, the section above: containers too big for one chunk of their list are trees now, and only
those, so one pin moved; the heap pages are as they were.)

## Projection hashes kept 64 rows at a time (2026-11-15)

The tick's check (`tide_game_client_hashes`, `client_spans_codegen.inc`) kept what a table chunk's 1,024 rows came to,
and where any page of the chunk changed it compared every row of it, one by one: their lanes, their heap pages, their
groups. With 8 bots moving in `bench_voxels` (6,490 chunks in 7 table chunks) about two table chunks a tick were read
whole for a few dozen rows that changed, and every few ticks every row's audience was worked out again. Now:

- **A piece** (`tide_client_piece`, `TIDE_CLIENT_PIECE` 64 rows) is what's kept and passed over. A table chunk
  (`tide_client_span`) keeps each column page's hash and its rows; where none was written since its pieces were read
  (`tide_client_span_written`, by the hashes pages keep), each piece is checked by its heap and slot pages alone. Where
  some were, a piece keeps each column's run of its rows hashed (`tide_client_run_hash`, `tide_hash_wide` over the
  page's data), and only the written columns' runs are hashed again. A piece whose runs, rows' heap pages and slots'
  pages are as they were is not read (`tide_client_piece_same`).
- **In a piece read again** a row's lanes are digested only for the components a column of which changed in the piece
  (`tide_client_part_of<a>`, generated: a column's component; -1 for the entity's and the scene's), its own heap pages
  compared only where one of the piece's moved, and what says which entity it is hashed again only where the entity's
  or the scene's column or a slot's page moved; otherwise its sum moves by what its terms worked out again moved by
  (sums wrap, so that's exact).
- **A row's policies** (its Visibility's overrides) are kept as what they say (`tide_client_policies_digest`, in
  `tide_client_rows.policies`) beside the pages they're in. A row whose heap pages changed under it, with the same terms
  and policies, isn't fresh: its audience isn't worked out again. Before, every such row was.
- **Audiences**: a piece is looked at only where a row of it was worked out again, every row's must be, or groups
  changed and the piece's groups (the least and most id its rows' audiences go by, their own and their scenes', above 0,
  and a bit of 256 for each by `tide_client_mix`; and whether a row is restricted to a group by a policy) take in one
  that changed (`tide_client_changed_in`, the changed ids being in order). Inside, rows go by the row's check as before.
  A piece's groups are gathered again each time it's looked at.
- **A scene's root** makes every row's audience go again only where a root came, went (a hole, or the table's end,
  where a kept row was) or has another Visibility (its lanes or its term). Before, any change to a root's table chunk
  did: a dimension's `Streamer` map changes as chunks come and go, and 369 of 2,017 calls worked out all 6,490 rows.
- The pages a piece's rows reached are made once each by a table on the stack (`tide_client_gathered_once`, up to 512;
  sorted past that) where a sort took 4 µs a piece. A row whose pages aren't all kept (`page_count` `UINT32_MAX`) leaves
  its piece unkept, so a piece kept as it was has every row's pages in its union.

What's kept is still only a shortcut: the hashes are of values (no pin moved, `bench_voxels`' world hash and every
projection hash among them), kept rows' audience, sums, denials and terms are what the rows come to at
`tide_client_kept.tick`, which `client_changes` trusts, `TIDE_CLIENT_SPANS_CHECK` works each hash out both ways, and
`TIDE_CLIENT_MEMO_OFF` keeps nothing (every column written, every piece read). No runtime change: generated code calls
`tide_hash_wide` and reads table pages' data (`tide_page_data`).

**Tests.** `tidec_e2e_visibility_pieces` (and `_checked`, `_memo_off`; `e2e/visibility_pieces.tide`, `_test.c`): 2,600
cells with their own groups, grids, lists and a component each hides, in three table chunks, and 300 in a zone, a
scene whose root's group they go by; 256 cards, whose only blocks in the heap are their Visibility's policies (even
ones deny their `Face` to everyone from the start); 256 bags, a small list each, hundreds to a page; two rovers with a
`Transform`, and a parent for them in the zone. Twenty-two changes, each to a cell, card, bag or rover in a piece
nothing else touches, or to the zone: a group's audience; a grid's cell and a list's element where they are; a policy
where it is, and gone; a policy where there was none, then another where it is; a card's policy where it is, and
where there was none, then another where it is; a bag's element where it is (its page the bags' before it); a lane;
another group; a component gained (another table) and lost; a group destroyed; the zone's group's audience; a rover
taken into the zone's scene by `SetParent` (its row's scene column written in place, nothing else of it); another
group's audience, which a cell given it goes by; the root given another group, and its Visibility removed; a new cell;
holes past a 32nd of the table, which the tick's end fills from its end (rows go into other pieces and chunks). Each
with the world otherwise still, then again with lists growing in pages all over. Every tick, every player's hash is the
one a copy of the world hashes to with nothing kept, a repair by what changed from what the check kept is the one read
from the rows byte for byte, each change shows to some player (quiet round), and the hashes are pinned, the same in all
three. `_checked` also sorts every piece's pages to make them once each (`TIDE_CLIENT_ONCE_MOST` 4: what a piece past
512 of them does). Mutations each fail it: groups that changed ignored by the pieces, pieces kept by their columns
alone, a root's change ignored, written columns taken as unwritten, a changed policy not making its row fresh, the
entity's and scene's columns taken as never moving (at the rover: until it, nothing but a row's slot page moved with
them, which the slots' check sees), and the review's below: a Visibility's lanes not reading its policies again, its
policies read only beside a component that holds heap data, a row's first page dropped where it's the row before's
last, and the sort losing a page (`_checked`). `bench_voxels_checked` (8 bots, 30 s, and `--quick`) works every hash
out twice. The visibility, shard, storage, alone-seen fixtures and `bench_earth` pass.

**Corrections (review, 2026-11-15).** Two misses, both older than the pieces (dev's per-chunk walk had them), and two
smaller things:

- *A policy set where it is went unseen.* `Visibility.overrides` is recipient-private, so `client_codec_heapy` says a
  Visibility holds nothing in the heap, and its policies were reached only inside the block for components that do: in
  an archetype with none of those (`{Card, Face, Visibility}`) never, and in one with them only when that component's
  lanes or pages moved, not when the Visibility's did (its map made, grown or moved), so the row's pages lacked the
  map's blocks. `visibility.Set<T>(...)` on a key it has writes the entry in place (`tide_map_insert` gives the one
  there): the Visibility's lanes stay the same and only a page of its heap changes, which neither case looked at. The
  row's audience and denials stayed the old ones, every player's hash went on subtracting the old term, and
  `client_changes` (trusting what's kept at its tick) sent no repair: a reveal lost until something else of the row
  changed. Now every archetype that stores a Visibility reaches its policies' pages and digests them, and a change of
  the Visibility's lanes (`heap_again`) reads them again.
- *A row's first page was dropped where it was the row before's last.* `tide_client_gather` drops a number alike the
  one before it, and a row's own pages went into the piece's store after the rows before it, so a row whose first page
  was the last of the row before kept a range without it; a row with one page (a bag's small list, a card's policies
  in their heap's page) kept none, and `tide_client_row_pages_same` over an empty range says the same, so an element
  changed where it is in such a row was taken as unchanged (its neighbour's range had the page, so the piece was read,
  and the neighbour hashed again). The gather now takes a floor, the row's start, as the hasher's `floor` does for a
  container's pages (the same bug, 2026-11-04).
- *No pointer arithmetic on NULL*: `piece->pages + heap_pages`, a piece's `row_pages + page_from` and the store's were
  formed with a count of 0 and arrays not made yet (UBSan's pointer-overflow says so). They're formed only with a count
  above 0 now (`tide_client_row_keep_kept` checks a row's range against its piece's arrays first), and a row with no
  pages is the same as it was without looking.
- *A written column's run was hashed twice in a piece read again*, by its check and its keep: the check's hashes
  (`run`, one a column) go to `tide_client_piece_keep`, which hashes only where the check didn't (nothing kept, or
  `TIDE_CLIENT_MEMO_OFF`).

**Every row changing** (`visibility_pieces_measure`: the fixture's 2,900 cells, 4 players, 60 ticks each, release,
eight runs of each build alternating, medians): nothing changing 16.6 µs a check before the pieces, 18.4 with them; a
lane of every cell's `Cell` changing every tick (a component with a grid and a list) 1,094 µs to 697; every cell's
`Secret` 777 to 271. The review's fixes measure the same as the pieces without them (18.1, 696 and 272): a 256-byte run
hashed once less is lost in the rest. So a world whose every row changes costs less with pieces than without, and one
where nothing changes about 2 µs more (more checks, a piece's rather than a chunk's, for 2,900 rows).

**Measured** (`bench_voxels`, 8 bots, release, Windows, 12 threads, BelowNormal, three runs alternating with a build
of the same tree without the change, the machine shared; each figure the median of the three, their range in
brackets): server hashing (the world's hash and the projections') 43.0 to 18.4 ms a second (42.7–44.5 to 18.1–21.5),
update median 1.27 to 0.95 ms (1.23–1.27 to 0.94–0.96), p99 7.1 to 5.8 (7.0–7.2 to 5.4–6.8). Sent bytes and the
world hash the same. Standing still (`--still 100 --hop 0`): hashing 1.5 ms a second both, update 0.10 median both,
p99 0.53 to 0.46. The review's fixes, five runs of each alternating with the pieces without them (medians): hashing
19.5 and 19.0 ms a second (17.3–23.9 and 17.6–22.0), update median 0.99 and 0.97, p99 5.6 and 5.5, the same world
hash and bytes; standing still 1.44 and 1.40, one run each. Of the projections' 0.28 ms a call now (0.70 before; a
counting build): the chunks' table 0.20 (rows of pieces read 0.16, of which grids encoded again 0.05; the pieces'
checks 0.02, their keeps 0.02), the directory's copy 0.03, routing 0.02, the dimensions' `Streamer` maps 0.02,
audiences 0.01; the world's hash 0.03.

**Left** (taken on in the section above, Projection hashes kept by blocks): 62% of the pieces read (19k of 30k over
the run) are read because a heap page of theirs changed under them:
another entity's block in the same 16 KiB page (a grid's directory is a few hundred bytes, fifty to a page; a
Visibility's policies are two small lists), so 248k rows were hashed again with nothing of theirs changed, and 17.6k
grids encoded again for their directory's page. Next: keep, for blocks smaller than a page, their bytes' hash beside the
page's, and where a page's hash changed, hash again the small blocks of it a row or a container read (memo entries,
pieces, rows) before taking it as changed. Also: a container that changes is encoded whole (`Streamer.chunks`, 6,490
entries, 105 µs each time); the directory of groups is copied whole each tick it changed (6,490 groups); a row's
contributions to each player's sum are added up every tick.

## Projections over heaps per owner (2026-11-14, later)

A game with visibility has a heap for each owner and runs (schedule-and-threads.md, top section), so what's generated
for players reads a value wherever its handle says (`client_state_codegen.inc`, `client_spans_codegen.inc`,
`client_changes_codegen.inc`; no session or runtime change):

- **A heap page is named by its heap and its place**: `tide_client_place(heap, first)`, a `where`'s id and run (as
  `tide_heaps_of` takes them) above 32 bits and the page's first place below. `tide_client_hasher` holds the world's
  `tide_heaps`; `tide_client_reach_block(hs, heap, block)` takes the heap a block is in (a list's chunks and a grid's
  are in their block's, `tide_client_heap_of(handle)`); `tide_client_place_hash` gives a place's page size and hash,
  or false where no page starts there or the heap or run was never made (none is made: `tide_client_heap` reads
  `tide_heaps.all`, `tide_heap_slots` and `tide_heap.runs`, where `tide_heaps_of` would allocate). The memo's entries
  (`tide_client_page`: place, size, hash), a table chunk's kept heap pages, a row's own pages and the groups'
  directory's digest all use places, so a page of one heap never stands for another's page at the same place.
- **Values go where their owner's code would make them**: a projection's load, a reload, a repair's records and
  singletons, and the releases of what they replace say `decl_where(owner)` (`TIDE_WHERE_RUN(TIDE_IN_MATCH, id)` for
  an owner in runs, run 0 outside them), not `TIDE_IN_MATCH`.
- **What's sent and hashed is unchanged**: the projection hashes are of values (no pin moved), and the bytes sent and
  repaired by `bench_voxels` are the same byte for byte. A player's machine runs the same systems in runs on its own
  heaps; its projection hashes as the server's (`tidec_e2e_visibility_runs`).

## A command handler's `with ownership`: the sender's own entity (2026-11-14)

`command(Rename rename) RenamePlayer(mut Name name, with ownership)` runs only when the target's
`Owner.player` is the command's sender; `without ownership` only when it isn't (no Owner included). Nothing
in the session, the wire or the world changed: the dispatcher's per-entity case filters as a view's rows do,
with `tide_me = *tide_sender` (the `&c->sender` it's handed) where a view has `Session.player`. A command the
filter drops is handled as a guard's `return` is: its sequence is consumed, a resend is refused, replay
drops it again. The sender may go unnamed in the trigger when the handler filters; otherwise naming it is
still required. Checker, codegen, editors and tests: language.md, world.Edit, and `with ownership` on
command handlers.

## The host's tick beside its frame (2026-11-12)

A session whose host asks (`tide_session_desc.alongside`, the `ticksAlongsideViews` setting) runs the last tick
an update finds due on the pool's leading thread while the frame draws, and ends it at a later update: design,
contracts, tests and gaps are in schedule-and-threads.md (A tick beside the frame). What it changes here:
`server_tick` is `tick_begin`, `tick_run`, `tick_end`; a server can have one tick begun and not ended
(`flying`), during which nothing but `tick_run` touches it or its world; its own player's views are of the
update before; other players get that tick's frame at the host's next update. A match is the same match, tick
for tick. Clients of another machine's match, shards and the web are unchanged.
## Owned predicts its singleton dependencies (2026-11-07)

Owner-approved change replaces the earlier all-singletons behavior. Generated
Owned ticks select live owned entity work and conservative transitive singleton
producers. Unrelated singleton state remains at the prediction baseline until
confirmation; views can explicitly bind verified state. near_same excludes
unused singleton differences, while remote input changes and whole-heap/task/
structural differences can still force replay. The session protocol and existing
predict_far/near_same callbacks are unchanged; the compiler uses private derived
selection fields. Bounds, side-effect fallbacks and focused native/WASI
validation are in language.md, Owned singleton dependency selection. No game
file/default changed, and no Mount & Rifle frame-time improvement is claimed.

## Full-world Owned replay diagnosis (2026-11-07)

Read-only Mount & Rifle dev 241d92d supplies a true FULL_WORLD control on nightly
134. Its generated build omits recipient projection/group APIs. Supplied
profiles show Delay client/server match work both 32.6 ms/frame, about one tick
per frame; zero repairs and stable initial world bytes. Earlier no-TROOPER_SIGHT
runs retained campaign visibility and were not all-visible controls.

Owned still predicts singleton systems and their queries. The supplied Owned
profiles show BuildKitBodies host/client 1.919/19.051 ms/frame, while Roam is
19.452/18.515. Roam's expensive per-entity work therefore is not multiplied
across every partial tick. Row filtering does not exclude the singleton query
work. System intervals may overlap and must not be summed as CPU work.

Fixed-seed miniature experiments separately establish two replay gates:
as_predicted rejects changed foreign input before near_same; near_same rejects
singleton state derived from held far rows. Constant actions plus presented
engine viewTime reproduce 4858 replays in 720 updates without singleton work,
versus 301 with constant input and no presented timestamps. An all-row singleton
alone reproduces 4907 replays with constant input. All reach verified endpoint
717 with zero repairs/resyncs. Probe controls and limitations are in
[the retained diagnosis](repro/owned-replay-20261107/README.md).

These are existing semantics, not unconditional partial replay: the constant
control keeps 658 confirmations. Removing changing remote metadata blindly
would be unsafe when singleton/helpers consume it; suppressing singleton work
would change the approved Owned contract. A controller-only prediction execution
set requires a decision about singleton dependencies and side effects. The
current game samples animated muzzle/barrel and unconditionally stores stamped
viewTime in Orders.seen, but no actual-game field-by-field packet trace was
collected, so individual contributions to its replay count remain unmeasured.

A separate engine inefficiency was established and fixed: canonical systems
prepared writable pages for all-Far chunks before filtering out every row.
Contracts, equivalent-work probe and native/WASI validation are in language.md.
This scoped fix does not remove singleton work or promise a battle speedup.
No game file/default was changed; the other game process was left alone.

## Partial prediction repair comparison amplification (2026-11-07)

Established engine defect: copy_repair_candidate selected a predicted ring
endpoint whenever the offered target tick was <= ahead. Under range/Owned
prediction its excluded entities are intentionally stale. Delay, lacking that
endpoint, reconstructed the full tick from authenticated frames. Identical
permitted evolution could therefore be reused by Delay but transferred again
under partial prediction. Partial-prediction repairs now reconstruct privately
from verified state too; the verified endpoint itself remains directly usable.
Existing complete-frame validation, four-tick/catch-up work allowance, timeout,
event suppression, snapshot binding and exact final hashes remain in force.
No private state is sent and no comparison endpoint is adopted as verified.

A fixed-seed 12-second, 40 ms/way + jitter/3% loss regression freezes a changing
far payload only in predicted worlds, with a small legitimate hidden-state
correction. Before this fix it transfers 11,305,522 world bytes with 21 repairs;
afterward 1,046,157 bytes with 31 repairs (10.8x less). Both verify endpoint 717,
with zero resyncs. Owned gives the same post-fix result. Delay transfers 1,029,779
bytes, 31 repairs, endpoint 712. Exact stream equality is not expected because
the timelines/repair endpoints differ.

The same probe explains a CPU comparison trap: before the fix the range client
runs 185 full ticks and 2520 held/partial ticks, adopting repairs across the
remaining intervals. Afterward it runs 716 full and 2710 partial ticks. Delay
runs 711 full ticks. Thus high repair traffic can replace full simulation work;
a lower per-frame CPU figure can reflect skipped work instead of a cheaper
simulation. Quiet comparison work also consumes CPU. These are deterministic
work counts, not timing claims. The supplied battle's exact CPU/traffic
attribution still requires its own endpoint trace; it was not rerun here.

Negative-first native regression fails its 4 MiB bound with the old selection,
then passes after the fix. Native ASan and WASI visibility tests each pass 19/19.
Seven focused compiler/network/split/diagnostic CTests pass on each platform.
The earlier partial
v126 investigation report remains historical; this follow-up establishes an
engine defect that report had not found.

## Owned prediction (2026-11-07)

Owner-approved: predict this player's owned entities and their children, with
singleton systems still running. `settings { prediction = Prediction.Owned; }`
selects it. The compiler reuses predict_far/near_same and the sparse Far filter,
allowing the tag on every match archetype, including unpositioned entities.
Generated ownership lookup walks from the entity toward its ancestors, using
the nearest explicit Owner; no owner means excluded. The walk is bounded at
4096 ancestors like the spatial root lookup. Parent/Owner changes and entities
spawned during prediction are reclassified before the next predicted tick.
Session calls the existing marker every Owned predicted tick. Confirmed worlds
remain untagged and execute all permitted simulation. Singleton queries can
still read/write excluded entities unless authored with Far filters. Spatial
range and Owned are mutually exclusive. This does not create hidden inputs,
independent singleton predictions or a separate controller timeline.

The existing callback/layout/wire contracts are unchanged; tide_prediction
appends TIDE_PREDICTION_OWNED. No new generated runtime call. Rendering and
near_same retention use existing spatial machinery, including the 0.25-second
stale-state bound and conservative singleton/heap comparisons.

Native ASan and WASI pass the owned fixture, its split-C variant, and a range-5
control of the same source. They cover an owned root, inherited child, nearby
unowned entity, unpositioned unowned entity and a distant entity. Twelve-second
fixed-seed loopback comparisons with 100 ms latency have zero repairs/resyncs.
Owned and its Delay control each transfer 455 world bytes; range 5 and its Delay
control each transfer 439. Initial layouts differ between builds. Each predictive
case executes 9897 client ticks versus 690 for Delay. These are tick counts,
not CPU measurements; predicted ticks are partial while Delay ticks are whole.
Existing net, predict_near and predict_kept CTests pass on native/WASI. The new
Owned/range conflict diagnostic passes natively; docs production build passes
(unstaged example URL 404s remain expected).

This supports the expected absence of extra recurring world traffic in a fully
reproducible simulation. It does not explain the supplied nonseeded battle's
14.5 versus 6.5 ms frame snapshots or 466 versus 46 KB/s. Those need matched
work and same-tick mismatch/repair-transfer traces. Total packet bytes can also
differ with input lead, acknowledgement timing and retransmissions; world-byte
equality is not an assertion of byte-identical datagram streams. No Mount &
Rifle files/defaults were changed and no battle timing improvement is claimed.

Remaining feature coverage: explicit conflicting descendant Owner, ownership
changes/reparenting during prediction, sharded integration and physics package
integration are not isolated by this fixture. Owned marking currently scans all
match entities each predicted tick; no performance improvement is claimed for
that pass. Existing privacy/physics/Task boundaries below remain.

## Logical spawn identity and canonical execution, protocol 25 (2026-11-06)

The owner selected stable logical Entity IDs separate from storage, and ascending
logical Entity order for match queries and deferred changes. An Entity is now
two opaque 64-bit words, birth and lineage. Birth is a public simulation identity
clock (initially one, advancing after each tick), independent of 32-bit Time.tick
and every private allocation/free list. Lineage follows the declaration/call
site, causal parent/row/event and that origin's per-site occurrence. Hidden
allocations at other origins do not advance a visible spawn's sequence. No
component initializer bytes, private allocator counts or transport cookies seed
the key. The final key exists before Spawn returns, including worker execution;
queued changes install that exact key rather than settling temporary handles.

Repeated public player lifecycle events have per-player clock-local occurrence
counters; implicit Main starts have their own counter. Snapshots, raw/delta
state, recipient projections and layout migration preserve these public values.
Async frames retain causal paths/counters across waits. Duplicate/colliding keys
fail explicitly before publication; the allocator never probes for a different
logical key based on hidden occupancy. Dense private slots and exact-key lookup
remain storage details. A per-clock birth registry also catches a duplicate key
whose first entity has already been destroyed that clock.

Canonical traversal spans admitted archetypes and survives holes, compaction
and different projection layouts. Indexed queries sort their candidate keys
before applying the ordinary guards/body. A cached immutable ordered-key array
avoids sorting/allocating on every repeated query; borrowed arrays survive
structural changes until settle. Independent row-local work may remain parallel;
modelled structural/event effects retain canonical source order. Extern systems
receive canonical contiguous runs through their existing rows/COW adapter.
Ordinary list/map insertion order is unchanged, and trusted C retains its existing
determinism, external-state and thread-safety responsibilities.

Protocol 25 carries complete keys in lifecycle journals and command values.
Generated C, physics bridge records, embed packet version 5 / ABI 18, Unity,
debuggers and hot-reload metadata rebuild together. The recipient block/delta
repair scheme below remains in use; no private allocator or native physics
history is added to recipient state. Older Entity8 saves/wire formats are not
reinterpreted.

Focused native regressions cover forty projected/replayed cycles with unrelated
hidden spawns and targeted events, repeated same-clock membership and Main
starts, async spawning with different private task slots, and 3,000 permuted
entities across archetypes/chunks under snapshots with Tide and C writes. The
allocator diagnostic below now asserts matching public IDs and hashes, both
with a hidden entity and with ALL_VISIBLE. Its physical free lists still differ.
Runtime cache tests cover retained borrows, concurrent readers, COW snapshots
and bounded allocation under 20,000 spawn/despawn operations. Whole engine
unit/migration targets pass native ASan and WASI. After the initial broad suites,
all repaired targets pass the final 70-test native and 72-test web batches,
including deterministic pins, safety churn, physics, networking and demo pixels.
Browser threaded simulation retains its 240-tick/45-second bound (19.61 seconds).
Debugger, CLI/Unity packet tests and docs build also pass. Exact-game runs and
remaining CPU costs are recorded in the linked measurement artifact below.

This removes allocation/row-order coupling between otherwise independent visible
pieces. It does not supply hidden values needed by a coupled public calculation,
restore opaque native physics warm state after projection, or make public Task
handles independent of the task registry's allocation order. Those remain
separate boundaries; stable async spawn causes do not redesign Task identity.

### Measured battle outcome

The final exact-game results and commands are in
[the four-run summary](repro/mount-rifle-logical-identity-20261106-summary.json),
with intermediate failures and the late correction trace in
[the investigation](repro/mount-rifle-logical-identity-investigation.md).
These are native Windows optimized, unsanitized, hidden 2100-frame runs from
Mount & Rifle `349c35a`, two players, latency option 40 (reported ping 123 ms).
Rates use simulation seconds 20–34, not the slower wall clock.

| Configuration | Total received kB/s | World kB/s |
| --- | ---: | ---: |
| Public 1000-bot battle | 48.11 | 27.72 |
| Prediction.Delay | 34.27 | 15.69 |
| TROOPER_SIGHT | 786.06 | 734.45 |
| 10-bot control | 58.51 | 28.05 |

All four runs finish without resyncs. Across the complete bandwidth work, the
public case falls from 3113.23 kB/s total (protocol 22), through 128.31 kB/s after
protocol 24 repair compression/comparison, to 48.11 kB/s with logical identity.
It is not a zero-correction campaign: public repairs stay at three through 20
seconds, then reach 26 at 34 seconds. The next first-difference probe parses both
7,093,074-byte projections completely at endpoint 1301, with the same identity
clock 1302 and exactly one differing float: Brevno ammo depot's Works.stock is
0 on the server and 18.416666 on the client. Its hauler has no permitted Party
row. economy.tide's hidden Haul system consumes public depot stock when the
convoy arrives; the client cannot execute that hidden work. These authority
updates are expected. The first residual is not evidence of physics divergence.
TROOPER_SIGHT still couples public Crowd data to hidden troops and is not fixed
by making allocation and query order independent.

CPU is explicitly unresolved: public match work averages 123.90 ms/frame and
systems 15.64 ms/tick, with 15,977 ticks over 2100 frames. The canonical chunk
preparation correction removes a large introduced copy cost, but remaining
query lowering and changing remote action inputs still cost substantial work.
No return to the original reported 6.1 ms client tick is claimed.

### Full-width destruction journals

The instrumented battle caught a migration error before delivery: the first
post-startup endpoint difference at tick 413 was public clock 414 on the server
versus 413 on the client. The client had not run that tick. FRAME_DESTROYED
encoded two varint64 key words, but apply_frame_to's skip loop still consumed
varint32 and rejected the frame before simulation. The journal replay skip
loop had the same error. Both now consume full-width words. This was a frame
parsing failure, not evidence of native physics divergence.

`visibility_full_entity_destroyed_frames_verify_and_replay` uses both key words
above 32 bits, checks verification advances beyond 650 without another repair
across the destruction, and checks journal replay reaches the same hash. It
passes native ASan and WASI; all sixteen native visibility cases pass.

## Remaining scoped tasks after logical Entity identity

- **Recipient simulation completeness:** native physics resident/warm state is
  intentionally absent from permitted projections. Do not attribute a repair
  to physics without tracing its first differing logical value, or send private
  bodies/histories wholesale. Public Task handles still follow private task
  registry allocation order and need their own identity design if exposed to
  otherwise independent visible gameplay.
- **Canonical query CPU lowering:** remaining per-row overhead is measurable.
  BuildKitBodies scans all troopers for every walled place but only consumes
  position. Its canonical query performs exact-key lookup, reconstructs 88 lane
  pointers and gathers a full 352-byte Trooper per visit; the current optimized
  variant retains that gather call. The public profile averages 4.76 ms per
  BuildKitBodies call. Key caching avoids repeated sorting. Specialize narrow
  reads/reuse safe chunk metadata while preserving canonical order and immediate
  World.Set visibility; validate equivalent seeded work and snapshot/COW cases.
  This is a compiler performance follow-up, not a demonstrated random-gear cause
  or a measured attribution of the entire tick-time regression.
- **Owned prediction:** a first-class owned-and-descendant prediction mode and
  ping-aware far-state freshness remain unimplemented. Re-measure replay work
  after identity/order fixes before changing the stale interval. A small
  predictionRange still runs singleton/query work unless the game filters Far.
- **Server AI input:** match-derived authoritative input sampling remains Open
  in the spec; this change does not give server Sample match-state access.
- **Game coordinates/privacy:** Position float2 has ordinary XY coordinates.
  A 3D game's ground XZ point should use float3(x, 0, z), or omit Position when
  it describes a separate campaign abstraction. The engine cannot infer that
  mapping from float2. A public Crowd built from hidden fighters still couples
  public results to unavailable state and exposes their positions; partition it
  by permitted battle/group or keep presentation-only data local.

### Why an idle player still causes rollback in Mount & Rifle

In game commit `349c35a`, `controls.tide:32–57` samples `Look.barrel` and
`Look.muzzle` into `[Absolute]` input fields; an animated pose need not keep those
values constant while no key is pressed. With `maxRewind = 0.5`, the compiler also
adds the read-only `Controls.viewTime` field. Session input sampling stamps the
last presented tick/fraction after Sample (`session.c`, `sample_input`), so that
field advances even for an idle player. `infantry/body.tide:165–169` unconditionally
copies it into `Orders.seen`, and `infantry/fire.tide:149,173–181` uses the saved
value for the game's own shooter-lag capsule query. This revision does not call
Physics.Rewind, but it does consume the engine-stamped view time.

Input prediction holds the last remote values, and the let-go callback defaults
non-Absolute fields after the guess window; viewTime has no Absolute attribute.
Changed authoritative input therefore requires replay even when the other player
stands still. At final public frame 900, repairs were still the three startup
repairs, while stats showed ten ticks per frame, eight run again. This separates
input rollback from correction traffic. The 0.25-second far-state stale limit
cannot make a changed remote input or differing near singleton/heap compare equal.
These are source-level reasons for changing input; no isolated field-by-field
experiment measured their relative contribution.

Game-authored action fields can stay unchanged while unused, or derive from
deterministic match pose where that suits the game's aiming model. Stamp action
metadata when the action consumes it rather than changing unrelated persistent
input every frame. The built-in viewTime is engine-owned and Sample cannot
conditionally overwrite it; changing its stamping/prediction policy needs a
separate agreed input/rewind contract. A wider stale interval is not that fix.

The measured canonical traversal is not a CPU improvement claim: after eliminating
repeated whole-chunk COW copies, the public frame-900 tick is about 16 ms versus
the report's 6.1 ms reference, and rollback still multiplies that work. Singleton
systems such as BuildKitBodies still query every troop unless the game explicitly
excludes Far. Exact final profiles and remaining costs are recorded with the
logical-identity measurement artifact.

Read-only inspection of `campaign/compound_kit.tide:581–589` identifies a remaining
cost in BuildKitBodies: every walled Place scans every Trooper for nearest distance,
using only `t.position.xz`. The generated canonical walk resolves each logical key
to its current location and sets up the 88-lane Trooper row per candidate; the old
physical walk set up column pointers once per chunk. This is concrete additional
row setup/lookup work, not a measurement assigning the whole observed cost increase
to one operation. Narrower query reads and reuse of chunk setup are follow-up
optimizations; no further traversal redesign accompanies these measurements.

## Same-tick recipient repair comparison, protocol 24 (2026-11-06)

Fixing compression locality alone still retransmitted legitimate evolution from
the last confirmed tick, although the client had already computed most of it.
Large recipient repairs now offer canonical 16 KiB block hashes for the pinned
authoritative target. The client compares its own **same-tick** canonical bytes,
pins them outside the mutable prediction ring, and answers a bounded missing-block
mask. The server sends compressed permitted blocks only where the client differs.
The reconstructed complete canonical hash and the existing frame logical hash
must pass before the endpoint is adopted. A comparison never confirms a tick.

The server retains one target world, its original acknowledged base/tick and its
permitted lifecycle journal throughout both transfer phases. Stream targets stay
coalesced to recent authoritative endpoints; they are not limited to advancing
one mismatch tick per round trip. Snapshot number, target tick and authenticated
connection attempt bind the response; PATCH must immediately follow the hash
offer the client answered. No target is regenerated from a later world. Normal
packet/chunk budgets, retransmission and early-chunk handling apply. Over 64 MiB
of canonical bytes falls back to the ordinary codec because the fixed 512-byte
mask cannot describe more blocks; this is a negotiation bound, not a world limit.
Small canonical projections (under 128 KiB) avoid the extra round trip. When more
than half the blocks are missing, the pinned acknowledged-base codec is used.
For patches covering over 64 KiB of missing spans, both encodings are measured
and the smaller complete payload wins. Counting missing blocks alone can choose
poorly for a large mostly-static world. Tiny same-tick patches avoid this extra
packing work and skip serialization of the unused acknowledged baseline.

Delay clients have no predicted target world. While the hash transfer remains
pending, complete authenticated frames can replay into a private comparison
world, ignoring expected logical divergence but rejecting malformed frames or
pending preparation. Work uses the same per-update catch-up allowance, capped at
four quiet ticks; the server continues delivering frames during negotiation.
Quiet work changes no verified/predicted ring endpoint, presentation clock,
hearing record or download acknowledgement. It neither drops frames nor invents
remote input. Incomplete comparison after one second safely requests all blocks,
selecting the existing baseline codec. Adoption, new offers, reload, migration
and destruction release comparison caches using the correct build's callbacks.
Same-layout reload explicitly abandons a pending comparison and recovers through
a full snapshot, preventing a response from waiting forever for discarded bytes.

All negotiation data is derived from `client_state` for that recipient. Physical
worlds, allocator/free lists, private commands and physics history remain private.
This stage used protocol 24; protocol 25 above adds logical identities. There
was no new Tide syntax, generated callback or observable simulation policy in
the transfer change. It reduced repairs before the identity fix, without
requiring an entire evolving battle to correct a few differing handles.

An actual first-battle mismatch at tick 1149 had four differing bytes in one
16 KiB block: two newly spawned bullets had different entity handles, while their
other gameplay values matched. Offline negotiation over those **same-tick**
7,315,009-byte permitted projections needs 3,588 hash bytes, a 56-byte reply mask
and a 7,698-byte patch (11,286 bytes down before session/journal/packet overhead),
with exact reconstruction. This measures repair payloads, not live byte rate.

New 512 KiB changing/shifted-state loopback fixtures activate the complete path
over 40 ms/way, 10 ms jitter and 3% loss. Across twelve seconds, rollback confirms
717 ticks with 33 repairs, zero resyncs and 1,079,676 world bytes; Delay confirms
712 with 31 repairs, zero resyncs and 1,029,890 bytes. A 4.2-second receive pause
crosses frame history and safely recovers through one full resync, reaching tick
717 with 1,357,922 world bytes. The quiet-event fixture proves private comparison
work occurs without delivering future or duplicate local events. Native ASan and
WASI pass these fixtures and the existing visibility/session checks; final
integration and actual game traffic results are recorded with their run below.
The compatible-reload regression swaps and zeroes old game callback tables eleven
times during large repairs, then verifies continued progress and state. A spread
logical mismatch touches nine random comparison blocks but is cheaper as an
acknowledged delta: the selection regression confirms tick 717 with 33 repairs,
zero resyncs and 1,574,457 world bytes, below its 2 MiB transfer bound.

## Recipient delta locality, protocol 23 (2026-11-06)

The battle investigation below established that fixed-offset XOR regions lose
almost all reuse when an earlier variable-size value grows. Session projection
transfers now select an internal `recipient_delta` encoding over **only** the
current and acknowledged baseline's permitted canonical bytes. Candidates are
the old 16 KiB XOR regions, literal bytes, and LZ copy spans referencing either
that permitted baseline or already reconstructed output. No private world,
allocator metadata or physics history is transferred. Existing projection bytes,
logical hashes, event journals, tick/repair policy and game semantics are unchanged.

Two direct-mapped 65,536-entry indexes (512 KiB total), adaptively spaced baseline
anchors (at least 32 bytes apart) and one candidate per index per scan position
bound encoder work. Matched output spans are indexed every eight bytes rather
than scanning them again byte by byte; literal searches still visit every byte. Every
candidate is compared exactly; hash collisions only reduce compression. Output
copies allow overlap. The actual smaller payload wins; XOR results of at most
64 bytes or 1/32 of raw bytes skip LZ construction/scanning. This preserves the
common sparse/unchanged path. The session wire is protocol 23; raw command,
packet, view-time and voice fixtures follow it. Older peers are refused normally.
No generated runtime callback/API or match-state layout changes.

The private decoder checks baseline/output hashes, caller-provided decoded size,
exact encoded end, literal/copy spans by subtraction, and strictly earlier
output sources. Copies advance at least eight bytes, bounding token work;
forward overlap copying is linear in decoded bytes. It allocates nothing from
token lengths. Session decoded allocation follows the preexisting declared
projection size/allocation-exhaustion policy, with no arbitrary small world cap
(memory-safety.md, Recipient state decoding). Invalid output is never loaded as
a world. XOR decoding clears its output rather than requiring a zeroed caller.

Measured deterministic fixtures (1,000 records of 512 random bytes): a one-byte
prefix insertion falls from **512,206 to 123 bytes**. Twelve changed bytes per
record without shifting encode in 15,158 bytes; those changes plus the prefix
insertion encode in 42,264 bytes. A 7 MiB random baseline with a prefix insertion
and 1,000 scattered twelve-byte edits encodes in 152,660 bytes, covering index
load/early-region retention beyond the original 512 KiB fixture.
Exact round trips are required on native/WASI.
Unsanitized optimized Windows pack+unpack measurements, 16 iterations each, are
0.506 ms fixed and 1.676 ms shifted/changed, on a shared machine; these are codec
microbenchmarks, not the real game's bandwidth or whole-frame performance.

Windows ASan and WASI pass the four new codec cases, the shifted-record probe,
nine visibility and fifteen session cases. Tests cover empty/incompressible
payloads, insertion/deletion, shorter bases, overlapping distance-one copies,
every truncation in representative modes, bad hashes/bases/sizes, trailing bytes,
zero/small/overflowing spans and invalid 64-bit sources. Sixteen generated
network/prediction/visibility/view-time/voice integration CTests pass on each
platform. Independent decoder/encoder review found no remaining blocker.
The live measurements below subsequently established the first battle mismatch
and measured the combined protocol-23/24 changes. Repairs and the allocator
limitation below remain; compression locality alone was insufficient.

Index tuning used the actual game's consecutive **correction target** projections
(not necessarily the exact acknowledged repair baseline), 7,315,009 -> 7,315,431
bytes at ticks 1149 -> 1169 and 7,315,431 -> 7,314,111 at 1169 -> 1213. Original
fixed-region encoding takes 2,313,763 / 2,332,588 bytes; the final codec takes
265,732 / 272,350. Unsanitized optimized eight-iteration probes on those identical
byte pairs measure roughly 6-7 ms original pack, 12.5-16.1 ms new pack and
2.4-2.8 ms new unpack on the shared machine. Exact output/hash checks pass.
This guided adaptive anchors and sparse output indexing: a fixed 16K-entry index
with dense copied-span indexing took 550,660 / 712,151 bytes and 19-20 ms packing
on the same pairs. These are offline codec comparisons; they do not establish
the live recipient's final byte rate or confirmation lag.

## Mount & Rifle live battle measurements (2026-11-06)

The controlled Windows comparison uses game commit `349c35a`, its exact non-art
source assets, the local art/cache, optimized tools and runtime built in this
engine checkout, seed 0, two players, latency option 40, 2100 frames and 1280x720.
`BATTLE_TEST ARMY` creates 1000 bots; F7 is sent to both players at frame 100,
with player-2 captures at 900, 1500 and 2100. The executable receives:

```text
--players 2 --latency 40 --frames 2100 --size 1280 720 --stats --script battle.txt --profile profile.json
```

The script is:

```text
100 player 1 key f7
100 player 2 key f7
900 player 2 capture battle-900.png
1500 player 2 capture battle-1500.png
2100 player 2 capture battle-2100.png
```

Build with `--release --debug-views --define BATTLE_TEST --define ARMY --stats`,
adding the named switch below; the ten-bot control omits ARMY. Every invocation
sets `TIDE_HIDDEN=1`. The preserved protocol-22 baseline and final protocol-24
runtime use the same game source, assets, optimized platform and command line.
The baseline session source is the pre-codec implementation, with the new status
counters retained. The intermediate protocol-23 result is retained in the
[compact metrics artifact](repro/mount-rifle-network-20261106-summary.json).

After three startup repairs, the public battle ran on inputs until the first
battle mismatch at tick **1149**. Both same-tick permitted projections were
7,315,009 bytes; exactly four bytes differed. Two freshly spawned bullets had
matching shooters, bornTick 1148 and gameplay values, but different identities:
shooter `#1568.1` produced server `#3380.45` versus client `#3376.59`, and shooter
`#997.1` produced server `#3376.59` versus client `#3389.49`. This establishes
identity divergence as the observed trigger, followed by repeated repair
transfers. Projection loading loses physical slot/free-generation information;
its precise contribution to this game's allocation ordering remains inferred,
not an approved change to recipient identity semantics. Physics.Rewind is not
called by this game revision, as source inspection below explains.

Player-2 rates below are decimal KB per **simulation second**, measured from
cumulative counters at seconds 20 and 34. World bytes count transferred world
payload; total received also includes ordinary frames, inputs and protocol
traffic. Joins are outside this interval. All runs completed with zero resyncs.

| Build / game switch | World KB/s | Total received KB/s | Repairs at 34 s | Wall seconds for 2100 frames |
|---|---:|---:|---:|---:|
| Protocol 22, 1000 bots | 3028.5 | 3113.2 | 22 | 146.85 |
| Protocol 24, 1000 bots | 107.8 | 128.3 | 57 | 144.08 |
| Protocol 24, PREDICT_DELAY | 60.1 | 79.7 | 45 | 87.26 |
| Protocol 24, TROOPER_SIGHT | 654.8 | 699.6 | 78 | 92.14 |
| Protocol 22, ten-bot control | 665.8 | 695.5 | 59 | 40.82 |
| Protocol 24, ten-bot control | 58.7 | 85.1 | 51 | 41.05 |

The public 1000-bot battle therefore uses **28.1 times less world payload** and
**24.3 times less total received traffic** (about 1.03 Mbit/s total). At second
34 its cumulative world bytes fall from 55,732,919 to 6,279,847. More repairs can
finish because they are much smaller; repair count alone is not transfer volume.
Delay's late input delay stays at 12 ticks instead of growing throughout the
run. The paired ten-bot baseline verifies an 8.2-fold total-traffic improvement;
the reported nightly's 29 KB/s ten-bot behavior was not reproduced in this setup.

TROOPER_SIGHT remains substantially more expensive, about 5.60 Mbit/s total.
Selecting by actual encoded cost lowered its world traffic from 1588.2 to 654.8
KB/s. The game's public `Crowd` singleton contains positions of all server troops,
while a client rebuilds it from permitted troops. Even a small hidden subset can
therefore change public state and coupled AI decisions; it can also disclose
hidden positions. The game should give this derived data an appropriate audience
or rebuild it without mixing restricted and public state. The engine transfer
fix does not establish correctness of that visibility policy.

Hidden native runs advance a fixed 60-frame simulation clock; the table's rates
and the displayed FPS are **not real-time throughput/FPS benchmarks**. Wall
seconds are measured separately, and one run per configuration on a shared
machine does not establish a CPU improvement. The paired main profiles average
58.78 / 60.13 ms CPU work per frame and 38.72 / 41.68 ms match work before/after.
This change fixes transfer amplification, not repeated prediction work. Delay
averages 10.43 ms match work and Sight 9.30 ms, with different simulation work.
Full samples, cumulative counters, profile summaries, source hashes and the
intermediate results are in the artifact; raw world dumps are not committed.

## Mount & Rifle battle investigation and repair diagnostics (2026-11-06)

`tide_client_status.repairs` counts successful authoritative recipient repair
adoptions, separately from `resyncs` (full snapshot replacements). Initial joins,
rejected transfers and host mirrors do not count. It is cumulative for the client
object, like `resyncs` and `world_bytes`. The visibility backoff fixture checks
that repairs equal projection loads minus the initial snapshot. Native stats
presentation is recorded in standard-host.md.

The diagnostic counter alone did not fix the battle flood; the repair changes
and live measurements above followed it. Source inspection of Mount & Rifle
commit `349c35a` refutes its proposed Physics.Rewind
cause: its only Tide references to rewind are the maxRewind setting and the
hitbox.tide comment saying those capsules used to be Box3D bodies. Actual hits use
the game's posed capsules and shooter-lag displacement. No Physics.Rewind call
occurs in that commit's Tide sources. This does not remove the separately
documented physics-history limitation after projection loading.

Two mechanisms relevant to the report are established independently of timing:

- `as_predicted` rejects any changed foreign input. The game's `PlayerControl`
  copies the automatically sampled `viewTime` every tick (`body.tide`); that
  timestamp advances even when another player stands still. A stationary player
  therefore does not establish unchanged input or reusable predicted ticks.
  Near prediction additionally compares the entire heap and non-Native singleton
  state; its rebuilt `Crowd` lists and grids can reject near reuse even when all
  near components match. FAR_STALE_SECONDS only bounds successful reuse to 0.25 s;
  increasing it cannot make those rejected comparisons pass.
- Before the protocol-23 fix above, recipient repair compression split flat variable-length projection bytes at
  fixed 16 KiB offsets. A prefix insertion shifts every later region against its
  base. `delta_recipient_shifted_records_round_trip` provides a repeatable
  1000-record probe (512 random bytes per record), using the same region codec:
  one changed byte packs to 19 bytes, a one-byte prefix insertion to 512,206.
  Both decode exactly. This is synthetic evidence of compression locality, not
  a measurement of the game's component entropy or actual first mismatch.
  The existing maximum repair backoff with maxRewind 0.5 is seven ticks at 60 Hz;
  512 KB repairs at that rate already exceed 4 MB/s before packet overhead.

The original allocator probe (`notes/repro/recipient_allocator.tide` and `.c`)
shows a public-spawn mismatch after loading a valid projection: create a public
Dot and a hidden entity, destroy Dot, project/load, then both sides spawn Dot.
Their hashes agree immediately after load; the next public entity is server
`1:3` versus client `1:1`, and hashes differ. The projected loader discards free
slots/generations. This extends the existing Open spawn-identity gap ("A client's
own spawns go after the last entity it was sent", below) to public slot reuse.
The live trace above subsequently found bullet identity differences at the
first battle mismatch. The exact allocator ordering cause remains an inference.
Reconstructing a remembered client's
free list is insufficient: the server can have allocated those slots privately
since the last endpoint. Recipient projections intentionally omit allocator/free
lists; copying the full server allocator would disclose hidden lifetime activity.

Reproduce with this checkout's built tools (PowerShell, TIDE_HIDDEN=1):

```powershell
New-Item -ItemType Directory -Force build/recipient-allocator | Out-Null
build/sanitize/bin/tidec.exe notes/repro/recipient_allocator.tide -o build/recipient-allocator --name game
& 'C:/Program Files/LLVM/bin/clang.exe' -std=c17 -O2 -g -fsanitize=address -D_DLL -D_MT -Xclang --dependent-lib=msvcrt -Iengine/include -Ibuild/recipient-allocator notes/repro/recipient_allocator.c build/recipient-allocator/game.c build/sanitize/engine/tide.lib -o build/recipient-allocator/probe.exe
Copy-Item 'C:/Program Files/LLVM/lib/clang/23/lib/windows/clang_rt.asan_dynamic-x86_64.dll' build/recipient-allocator
build/recipient-allocator/probe.exe
```

The compiler/ASan paths above describe the measured Windows installation. The
probe now asserts equality with logical Entity IDs; add --define ALL_VISIBLE to
tidec for the public-only control. Historical output before the identity fix:

```text
before: free server 2 client 0 next server 3 client 1 hash equal 1
after: entity server 1:3 client 1:1 hash equal 0
```

After the logical identity change both Windows ASan variants exit successfully.
Despite server/client free heads 2/0 and next slots 3/1 (hidden) or 3/2 (all
visible), the new public Entity is identical on both sides and both before/after
recipient hashes agree. Exact key bits are diagnostic, not a public ID guarantee.

Historical follow-up scope, **recipient repair locality and battle divergence**
(addressed by the protocol 23–25 work above): first trace
the first differing logical fields/entity identities in the exact 1000-bot game,
including Delay, and distinguish initial mismatches from repair-induced ones.
The protocol-23 implementation above addresses variable-length transfer locality;
measure its exact-game effect before claiming the bandwidth issue resolved.
In parallel, resolve recipient-safe predicted spawn identities and slot reuse
without transferring hidden allocation metadata; confirm the probe's public
post-load spawns agree, including hidden intervening allocations, without pinning
private counts or histories. This required an owner decision; the owner selected
logical IDs and canonical traversal above, retaining the privacy boundary.
Measure bytes per recipient, repairs, resyncs, tick work and confirmation age
before/after for no-switch, Delay and TROOPER_SIGHT, plus the 10-bot control.
Acceptance includes native/WASI correction/lifecycle/view-time tests, lossy
variable-list/spawn/despawn cases and an exact-game bounded run. Arbitrarily
throttling repairs is not a substitute: it can move confirmed presentation
outside maxRewind while hiding the transfer volume. Sending private allocator or
physics-history state wholesale is not an acceptable fix either.

Validation: Windows ASan and WASI pass all nine visibility-filtered engine cases
and all twelve delta-filtered cases. The new probe pins successful round trips,
not its poor compression size, allowing future improvements. No actual battle
before/after bandwidth result or Internet/browser validation is claimed.

## Scene preparation and player readiness (2026-11-05)

Owner-approved scene preparation (language.md) uses optional tide_game callbacks `prepare_pending`, `prepare`,
`prepared_scene` and `prepared_command`. Sessions stop ticks while their private preparation job runs, continuing
receive/send/timeout processing. The server resumes on its own completion. Client prediction stops at a pending
load; generation starts only on the verified endpoint, then becomes the replay baseline. Startup page negotiation
can generate its ordinary initial baseline asynchronously before requesting missing pages. Slow peers use the
existing input history, catch-up and snapshot recovery. Recovery-journal replay prepares before the following tick.

Readiness is a private empty command sent once per completed root identity, through existing reliable authenticated
command sequencing. Its sender supplies the player; game payloads cannot name another sender. The generated world
stores player bits, clears departed players and preserves them in snapshots/hashes/packing. This is a completion
report, not proof of honest work; game logic owns any readiness barrier or kick. No new wire packet/protocol version.
All hosts/libraries rebuild for the tide_game callback layout (Session ABI preparation 1).

Prepared local/verified events merge ahead of the next tick's events. Visibility servers additionally record
recipient-filtered genuine lifecycle logs from preparation; projected clients never regenerate from potentially
missing private inputs and request the permitted prepared snapshot instead. The private ordinal singleton and
root ordinals are omitted/canonicalized in recipient codecs and lifecycle payloads. Completion and readiness remain
permitted match state. Leaving/restart/rewind/reload drain and discard private jobs/logs before releasing worlds/code.
Progress endpoints are available to views while initial baseline preparation is still connecting, without promoting
them to verified state. Tests and validation: language.md, Deterministic scene preparation.

Correction (2026-11-15): a projected client at a pending load asked for the server's world at `heard_tick`, the
latest tick it had heard of, but the server answers a repair only of the endpoint just past what the client confirmed
(`repair_tick == frame_ack + 1`), and the client asks once. Where frames past the preparation's tick had come by then
(a hosted match, whose server goes on at once after a quick preparation), it waited at that tick for good, its
prediction stopped. It asks for `verified + 1` now. `tidec_e2e_visibility_host_lifecycle` has a preparation in play:
the guest stopped at tick 151 before, and now takes the prepared world once (one resync). That guest hears `Shown`
for what the preparation spawned and no `Spawned`, as the frame whose journal has it is the one it doesn't run: open.

## Bans (2026-11-04)

The owner's ask, from mount-and-rifle, whose host re-kicked banned PlayerIDs
from a local `PlayerJoined` handler: a ban that kicks and refuses that player
for the rest of the match, and lifting it, surviving host migration. Names,
what a ban holds and the wire shape are the agent's picks (spec, Sessions,
Provisional; docs `multiplayer/matches.md`, Bans).

- **Language**: `Session.Ban(player, message = "")` and `Session.Unban(player)`,
  checked as `Kick` is (local code, a statement, a `PlayerID`, text). They go
  in the frame's kick queue (`tide_kicks`, `TIDE_GAME_PLAYERS` entries) as
  `TIDE_REQUEST_BAN` / `TIDE_REQUEST_UNBAN`, after the session requests; a
  `Start`/`Join`/`Connect`/`Leave` drops them with the kicks. `DisconnectReason`
  gains `Banned` (7, `TIDE_DISCONNECT_BANNED`), with the ban's message.
- **Server** (`session.c`): `tide_server_ban/unban/banned`; `tide_session_ban`
  and `_unban` on the session's server (nothing on a client or a sharded
  match); `tide_session_server(s)` gives hosts and tests the server.
  - State: `tide_player_set banned` and `ban_messages[players]`, by slot. A ban
    needs a slot someone played in (`digests[i] != 0`) and refuses this
    machine's own player and a first-time player still joining (not greeted,
    `new_player`), whom no PlayerID names yet and whose slot `drop_connection`
    would clear.
  - A connected banned player is sent away by `send_away` (kick's resending
    entry, with `kicked.banned`), so the goodbye says `BYE_BANNED` and so does
    every resend. A kick of a banned slot also says banned.
  - `on_hello` refuses before the kick list, the closed check and slot
    assignment: a HELLO whose cookie digest is a banned slot's, or, with a
    trusted identity host, whose nonzero identity is that slot's, gets
    `BYE_BANNED` + message (authenticated with the cookie and nonce it sent, as
    a not-yet-welcomed client accepts a BYE). The slot giving-away loop skips
    banned slots, so the PlayerID is kept for the banned player.
  - `tide_server_unban` clears the bit and message and the slot's kick entry
    (so the next HELLO is admitted without first being told of a kick), and
    starts its `away_since` if unset, so the slot can be given away in time.
- **Identity** (documented): a ban holds the slot, reached by cookie digest
  and, under `tide_server_desc.identity`, the account. A machine without the
  cookie is someone new: another device, cleared storage, or a session that
  joins a migrated match at the new host's address (`tide_session_join` drops
  the cookie for a different address, as for kicks). Accounts are what make a
  ban stick.
- **Survives**: host migration, through the HANDOVER record (after the
  in-match digests: `write_players(banned)`, then per banned slot its digest and
  message length + bytes; `handover_capacity` grows by a set and
  `(8 + TIDE_MESSAGE_BYTES) * players`), read into `handover.banned` /
  `ban_messages` (`free_handover`) and passed by `tide_session_take_over` as
  `tide_server_desc.banned` / `ban_messages` with the digests. A banned player
  who is still `present` (of a world it went on from, not back yet) stays in
  `in_match` and banned, and leaves on the usual 20 s. Recovery:
  `tide_server_recovery.banned` and optional `ban_messages`; managed hosting's
  checkpoint carries both (`tide/managed_run.h`, magic `TDM3`). Server create
  copies bans only with a `world` and only for slots with a digest. Not in the
  world, hashes or snapshots.
- Wire: `PROTOCOL` 22 (`BYE_BANNED = 4`, the HANDOVER tail).
- **Tests**: `net_a_match_bans_players` (lossy loopback: own player and empty
  slot refused, ban kicks with `Banned`/message, rejoin refused open and
  closed, a newcomer gets another slot, unban lets the same PlayerID in,
  recovery state has the ban and message); `handover_keeps_bans` (banned
  before the host leaves; the new host has the ban and refuses the old cookie
  with its message; unbanned, the same PlayerID is let in);
  `net_local_code_joins_rooms_and_connects_to_addresses` (request order and
  `Disconnected { reason = Banned, ... }`); `tidec_error_session_ban_not_a_player`.
- **Gaps**: bans of a sharded world (each shard's admission is separate);
  banning by address; a ban list that outlives the match; Ban/Unban results
  (they are requests, like Kick). The lobby and rooms packages show a
  `Banned` message as they do a kick's.

## Commands holding lists of values (2026-11-04)

The owner asked for the gap mount-and-rifle met on nightly.32 to be closed: `command Enlist { List<int> attributes; }`
was refused ("a command can't hold a list, map, set or grid"), and the game sent `int[6]`. A command now holds a
`List<T>` of any fixed-size value, in a field, a struct, a `T?` or an array; the picks are Provisional in
`docs/spec.md`, Commands. What's built, and what code relies on:

- **The checker** (`check_fields` in `compiler/src/check.c`): `holds_map_or_grid` refuses a map, set or grid anywhere in
  a command, lists' elements included ("a command can't hold a map, set or grid"); `holds_unsized_list` refuses a list
  whose elements hold text or a list, map, set or grid ("a command's list can't hold text or other lists"), with a note
  of what can. `[Clamp]`, `[Min]` and `[Max]` take a `List<T>` field as they take an array, bounding each element
  (`check_field_attributes`, `check_bound`).
- **The wire** (`gen_bits_list` in `compiler/src/codegen.c`, which `gen_bits` calls for a list of anything but bytes):
  the count as a varint, then each element through `gen_bits` as a field of its type, unaligned. `List<byte>` keeps its
  aligned raw path. Writing copies each element (`tide_list_at`) into a one-member struct `tide_element<k>` and shadows
  `in` with it, so `gen_bits`'s `in->` paths work unchanged; reading refuses a count over `TIDE_COMMAND_LIMIT` or over
  the bits left divided by the fewest an element takes (`bits_least`), before `tide_list_resize` makes the scratch
  list, then reads each element into a zeroed wrapper, repairs it (`gen_repair_value` against the element type's
  default, `gen_default`; for an enum with no member 0, its first member) and copies it in. So a hostile count costs at
  most about eight times the command's own bytes of scratch (bools: a bit each), released with the scratch mark both
  `check_command` and `command` take. The dispatch's `gen_bounds` loops a list's elements (`gen_bounds_list`) for a
  bound on the field and for a struct element's bounded fields (`type_has_bounds`).
- **Limits and room**: any list makes a command `TIDE_COMMAND_LIMIT` at most (`command_limit`, `decl_has_list`) and so
  chunked over `TIDE_COMMAND_BYTES`, as `List<byte>` was; the session needed nothing. Local `Send` sizes its buffer
  with `tide_command_room` (generated only in games with list commands) for every list in the value, through structs,
  `T?`s and arrays (`gen_command_rooms`, each element's most bytes from `bits_most`, which runs `gen_bits` into a
  throwaway buffer). The embed path (`tide_embed_command`) uses the same rooms; its `tide_embed_command_list` already
  took plain lists. Arrays of `List<byte>` used to be left out of the room and would have aborted at send.
- **Not built**: lists of text (each element's text would need its own bound and room; `string[N]` is the fixed form)
  and nested lists, maps and sets (maps' entries would need a wire form of keys and values; two lists do it now). A
  C# `TideCommandWriter` list of non-byte elements is untested from Unity.
- **Tests**: `compiler/tests/e2e/command_lists` (ints with an element bound, structs with defaults, a bounded field,
  an enum and a vector, floats, enums, entities, a list in a struct beside text, a `List<int>?`, `List<int>[2]`; every
  truncation and a trailing byte refused; a hand-written command with NaN, infinity, unknown enums and out-of-bounds
  values repaired; counts the bits can't hold and `UINT32_MAX` refused; 200,000 ints chunked through a lossy loopback
  session to the server's and the client's verified world), natively and on the web; `compiler/tests/errors/
  command_list_payload`, `command_list_of_lists`, `command_list_of_maps`, `command_map_payload`, `command_set_payload`,
  `optional_field_command_list`.

## Predicting near the player alone (2026-11-04)

The owner's ask: a client predicts what's near its player and leaves what's far
as the server confirmed it, ticking once (as `Prediction.Delay` would); near
meets far as if it were constant, and odd reconciles at the border are fine;
positions as shards find them. `settings { predictionRange = 40; }` (spec,
Settings, Provisional for the picks; docs `multiplayer/matches.md`, Predicting
what's near).

What's built:
- **A hidden sparse tag** (`check.c`): with the setting, `tide_far`, a builtin
  component with no fields (`program.predict_far`), sparse (`sparse_component`),
  possible in every match archetype that can have a position (`positioned`: a
  `[Position]` component or `Transform`, in its components or `possible`). Every
  match system and match handler that runs for each entity gets it in
  `without_mask`, as shards' ghosts do; a system written in C too (owner's yes,
  same day), whose wrapper hands C the runs of rows between tagged ones as it
  does for visibility presence (`gen_extern_filtered_call`). So the existing sparse-filter row loops (counted chunks,
  masked/selected/cached variants, apart loops copying skipped rows back) leave
  tagged rows out with no new loop code. Only predicted worlds ever have the tag:
  the server never sets it and a confirmed tick runs on a verified world, so
  server ticks and confirmed ticks run every row, at the cost of a presence
  count over each positioned chunk (zero pages, cached hashes).
- **`tide_game.predict_far(world, player) -> bool`** (`predict_far_codegen.inc`):
  the box around the positions of the player's own entities (`Owner`), grown by
  the range on each axis; each taggable row then gets the tag when its position
  is outside it, loses it otherwise, written (`tide_table_cell`, the page made
  its own and its hash reset) only where that changes. Positions are
  `tide_shard_where`'s (`gen_position_where`, split out of `gen_shard_where` and
  generated for unsharded games too) of the entity's root (`tide_far_where`,
  walking `tide_parent_of`). No owned positioned entity: nothing is tagged.
  NULL without the setting. Session ABI marker: `predict far (1)`.
- **`session.c`**: `tide_client.far_from`, the first tick run from a tagged
  world, reset wherever `let_go` is. `run_predicted` calls `predict_far` on the
  copied world when the tick is the verified one (each run of predictions starts
  there, untagged) or while nothing is far (`far_from` UINT32_MAX), so an entity
  moving out of range is noticed tick by tick; tags then carry along the chain by
  copy. `play` never takes a tick at or past `far_from` as predicted: it runs the
  frame on the verified world (whole), and the predicted ticks after it are stale
  and run again near alone (`predict_again`), `predict_from` clearing `far_from`
  past where it starts. With anything far, each confirmed tick is a whole tick
  plus a near-only rollback of the lead; the input-delay budget measures those
  predicted ticks (`note_tick_cost`), now near-only.
- **Corrections**: `tide_local_correct` skips an entity tagged in either world, as
  it skips a ghost in a sharded game: a far entity takes no accumulated error
  (with it, each confirmed tick read as a one-tick correction and smoothing
  lagged far entities by about six ticks); views blend it on the confirmed clock
  instead (below).

Tests: `tidec_e2e_predict_near` (`compiler/tests/e2e/predict_near*`, with a
system written in C, `predict_near_c.c`, which fails the test excluded): two
players on a 100 ms loopback, and on 120 ms with 60 ms jitter and 20% loss; a
walker 4 units from them and one 100 away, range 10. Every frame a client runs
ahead, the far walker in its predicted world is byte for byte its verified one
and the near one has run every predicted tick; then the mover runs to the far
walker and the two swap for its client, not the other's. No resyncs, and both
walkers' confirmed state follows the server's. Without the `far_from` check in
`play` both clients resync. Error fixtures `settings_prediction_range_zero` and
`settings_prediction_range_delay`.

**Far entities drawn blended** (same day, the owner: "we should blend them"):
the drawn (predicted) world has a far entity as last confirmed at both ends of
every tick, so it moved a tick at a step. Generated `tide_far_blend_<C>(w, e, v)`
(`gen_far_blend`, for blended components with nothing in a heap, `far_plain`):
for an entity tagged in the drawn world, its value in the presented confirmed
world, blended with the one before on the confirmed clock (`tide_blend_<C>`,
`tide_prev_<C>`), as a `verified` parameter's is; false for a near one or one
that world lacks (born the same, else drawn as before). View row copies and
view query rows call it before their ordinary blend, whole and lane-subset
paths. The confirmed pair comes from file statics `tide_far_verified`,
`tide_far_vprev`, `tide_far_valpha`, set by `tide_frame` before views (and per
other shard's world drawn), cleared after them, so views without `verified`
parameters needn't take the confirmed worlds (`takes_verified` would also turn
off shader preparation). Components holding text, lists or grids keep a tick
at a step: their handles are the drawn world's heap's.

**The marking pass, by columns** (same day): `gen_far_rows` reads each
archetype's position from its lanes chunk by chunk (`far_source`: the first
`[Position]` component it has, then `Transform`; a sparse one, which a row may
lack, goes through `tide_far_where`); a child in a hierarchy goes by its root
(`tide_parent_of`, then `tide_far_where`). A chunk's tag page is made its own
only where a tag changes. Measured (release, Windows, one thread, 50,001
entities over 1000 × 50 units, range 40, ~3% near; a scratch game, not kept):

| | lookups | columns |
|---|---|---|
| marking, first time on a verified world | 0.350 ms | 0.056 ms |
| marking again, nothing changing | 0.245 ms | 0.041 ms |

A whole tick of cheap systems (move, count) took 0.065 ms and the near-only one
0.080 ms: every chunk mixes near and far rows, and the filtered loops cost more
than the plain SIMD ones. With a system doing 32 steps of math per entity, a
whole tick took 43.2 ms and the near-only one 1.5 ms (marking 0.11 ms). The same
games without the setting: 0.037 ms and 42-43 ms a whole tick, so the far filter
costs every tick (the server's and confirmed ones too, though nothing is ever
tagged there) about 0.5 ns per row per system, which only shows on trivial
systems.

Tests (blending): `predict_near_draws_far_walkers_blended_as_confirmed` runs
`tide_frame` on the client's views each frame and checks the far walker drawn
at the confirmed pair's blend (and between its endpoints in over a quarter of
frames), the near one at the predicted pair's; it fails with the helper
returning false.

**`Far`, and the physics packages holding far bodies** (same day; the owner
named it and agreed the design: `with Far`/`without Far`, border islands
stepped whole, near bodies hearing `Touched`/`Hit` from held ones):
- `check.c` `far_filter`: `Far` in a `with`/`without` parameter of a system, C
  system or query names `program.predict_far`; without the setting, a builtin
  tag no archetype can have (`program.far_absent`, made on first use, so games
  that never name it don't change), so a package names it either way. `Far far`
  is an error (`errors/far_filter_binding`, `far_filter_query_binding`). A
  game's own type named `Far` wins the lookup. A system for each entity that
  needs the tag doesn't get it added to `without_mask` (it would never run).
  `warn_unmatched` says a system `with Far` never runs without the setting
  (`warnings/far_without_prediction_range`); package systems aren't warned.
  Extern systems' wrappers already split chunks into runs by presence, so the
  filter needed no codegen change. `tidec_e2e_predict_near` now also has a
  `with Far` system counting far walker ticks: equal to the ticks predicted past
  the verified one in every frame, zero on the server and in confirmed worlds.
- Box2D/Box3D (`notes/box3d.md`, `notes/box2d.md`, each `box*d/TIDE.md`):
  `HoldFar(Body, mut Physics, with Far)` at `Step` offset 19 hands the far
  bodies' ids to the bridge, and the step holds them once per run of predicted
  ticks (`b*World_HoldBodies`, noted into the stamp as `'f'`). `Leave` is
  `without Far`. Test `tide_box{2,3}d_predict_near`
  (`packages/test/predict_near_test.c`, the game in each package's `test/`).

Measured (release, Windows, a scratch game not kept: Box3D, 3,000 balls rolling
45 to 340 units from the players and 25 near them, range 40, a client on a
100 ms loopback, 14 ticks ahead; client ms a frame over 10 s, server and client
in one process):

| | whole world | range 40, holding | range 40, not holding |
|---|---|---|---|
| guesses always right | 2.8-2.9 | 11.9-12.8 | 33-34 |
| another player's input changing every 5 ticks | 19.5-21.2 | 14.4-14.9 | |

Where the 14.7 ms goes with the range: Box3D steps 4.1-4.5 (12.1-13.4 whole),
resident restores and commits 6.9 (5.7-5.9 whole), holding 0.3, the rest
Tide's ticks. Holding saves most of what the steps cost, but with anything far
every confirmed tick reruns the lead near-only, which the whole world skips
when guesses were right: the range costs four times as much there. And holding
3,000 bodies writes most of the arena, so the commit after it, and the
verified world's restore the next frame, copy most pages.

**Keeping the predicted ticks a confirmed tick leaves as they were** (same day;
the owner picked "skip, bounded"): with anything far, every confirmed tick
reran the near lead even when guesses were right, which made the range four
times dearer than predicting everything (table above). Now:
- `tide_game.near_same(predicted, confirmed)` (`gen_near_same`,
  `predict_far_codegen.inc`; session ABI `predict far (2)`): every match
  singleton and every row not tagged far in the predicted world, field by
  field leaving out `[Native]` fields (a physics package's stamps and handles
  differ between a held world and a whole one), sparse presence, scenes; and
  entities, commands, task tables and order, inputs and the heap by their
  hashes. Columns whose pages both worlds share from the tick before are
  skipped unread.
- `session.c` `play`: a confirmed tick whose inputs went as guessed but which
  ran something far (`guessed && tick >= far_from`) keeps the predicted world
  of its tick (`near_kept`, a spare world), runs whole over it, and if
  `near_same`, sets `far_from` to the next tick and leaves `stale` alone: the
  predicted ticks after it stand. `far_since`, the verified tick when the run
  of predictions started, bounds it: once the next tick is more than
  `FAR_STALE_SECONDS` (0.25) of ticks past it, they run again, which starts
  `far_since` over. `tide_client_status.near_kept` counts the kept ones.
- Test `tidec_e2e_predict_kept`: two still players, a near walker and a far
  one; in every frame ahead the near one ran every predicted tick, the far one
  is 0 to 15 ticks older than the last confirmed (older in over a quarter of
  frames), over 100 confirmations kept per client, no resyncs.
  `tidec_e2e_predict_near` keeps a predicted-world singleton (`FarSeen`), so it
  never keeps one and its exact far checks still hold; so does the physics test
  (its near raycast sees the far ball, which moved in the whole tick).
- The same scratch game (busier machine: whole world 3.5 rather than 2.9):

| client ms a frame | whole world | range 40, before | range 40, kept |
|---|---|---|---|
| guesses always right | 3.5-3.5 | 11.9-12.8 | 5.0-5.9 (716 kept) |
| another player's input changing every 5 ticks | 22.4-23.5 | 14.4-14.9 | 7.5-9.0 (611 kept) |

With Box3D marking less on sparse commits (notes/box3d.md, same day), the same
game, a quieter machine: guesses always right 3.9 ms a frame with the range,
2.65-2.7 whole; another player's input changing, 5.9 with the range, 18.5
whole.
What's left is mostly the whole confirmed tick and its whole commit each frame,
and the arena switching between the confirmed and held predicted states (a
restore each way; in this bench also the server's, in one process). A
predicted world in an arena of its own can't: an arena's bytes hold pointers
to where it is (notes/box3d.md). Running confirmed ticks in batches while
what's near goes as predicted would cut the switching, at the cost of the
confirmed world (far entities drawn, verified handlers) and divergence checks
lagging up to the batch: the owner declined it (2026-11-04, "stop here").

Gaps:
- Singleton systems going through queries still run far entities in predicted
  ticks unless their queries say `without Far`. A system's `mut` query rows
  write far entities like any other: the owner's decision, same day ("written
  like any other is correct").
- The filter's per-row cost in ticks that have no tags (the server's,
  confirmed ones): a world-level "nothing is tagged" would let them take the
  unfiltered loops.
- `World` reads in views (`world.Get<Body>(e)`) of a far entity are the drawn
  world's, not blended from the confirmed one.
- `near_same` compares every list and command whole (heap hash): a list
  written anywhere in the confirmed tick, a far entity's or a package's, reruns.
  Comparing near rows' lists alone would keep more.
- Box3D: holding many bodies marks most of the arena, so a held world's commit
  and the next restore copy most pages (6.9 ms a frame against 5.8 whole in the
  table). Holding without moving bodies between sets would avoid it.

## A client's own ticks of its projection; repairs that back off (2026-11-04)

Mount & Rifle played alone (route `tools/performance-route.txt`, seed 42, release)
had its client's own ticks differ from the server's projection every ~62 ticks for
the whole route: 1,338 to 1,357 repairs of 3-4 MB projections, packed and loaded.
A scratch probe (in `session.c` at each divergence, with the generated
`client_state` marking where every singleton, entity and component starts;
kept with the findings, not in the tree) compared the server's projection after
the tick with the client's run of it, from a projection both had alike. Four
causes, two of them the engine's. That was the lone player's own client, before
the host shared the server's world (the next section, merged the same day); the
causes are any remote client's, and the measurements below are from before
that change.

- **A client's hidden spawns were in its projection.** On a client
  `Visibility.CreateGroup` returned -1 whatever the audience, and
  `tide_visibility_entity` called every live entity visible, so a bot spawned into
  `Audience.None` by the client's own tick was in its projection and hash and not
  in the server's (315 parties at the first probe). Now a client's group is 0 (the
  default handle) when the audience has its player and -1 otherwise, and
  `tide_visibility_client_hidden` (the entity or its scene in a group below 0)
  keeps such entities out of `client_state`, the client's `client_hashes`
  (audience none instead of all), `tide_visibility_component` and
  `client_destroyed`. They stay in the client's world, and its systems see them,
  as the server's do. `Set` and `DestroyGroup` still do nothing on a client.
- **`[Native]` fields went in projections, the Resident didn't.** A client that
  loaded one had Box3D's bookkeeping (`Body.id`, `owners`, colliders, history)
  saying the bodies existed and an empty C world, so every query missed and its
  tick rewrote the bookkeeping. `client_state` and `client_hashes` now write each
  singleton and component with its `[Native]` fields 0 (`gen_client_bare` for a
  component's copy, `gen_client_put_singleton` for a singleton's, on the heap), so a
  loaded world has them 0, as on a shard an entity comes to, and the package makes
  its world again in the client's next tick (notes/box3d.md, same date); a client's
  own handles don't count in its hash. The value codecs themselves are unchanged:
  local handlers' event records and shards' entity records use them too, and a
  local handler taking `Physics` keeps its stamp. Box2D's `image` and both
  packages' collider and rewind lists no longer reach clients.
- **Game code** (written up for the game's thread: public `Works.hauler` checked
  with `world.Has` against a hidden party, and a public `Pathing` budget spent by
  hidden bots). With those changed in a scratch copy, what's left is what can't be
  predicted: the game's own commands, and parties coming into or out of sight.
- A client's spawn into a group it's in (the player's own party) has the client's
  group number (0), the server's another, so it's repaired once. Leaving
  VisibilityGroup numbers out of the codecs fixed that and broke shards, which
  move entities and shared singletons with them (`shards_visibility`, Earth); left
  as it is.

`recipient logical state schema 3` (`asset_usage.c`) is in the simulation
identity: builds before and after differ in projection bytes and refuse each
other. The wire and `PROTOCOL` are unchanged.

**Repairs back off** (`session.c`, `repair_gap_after`): a stream (two repairs
within a second) that has to start again within a second of the last one's end
repairs half as often as that one did. The server packs a streamed repair only
`repair_gap` ticks after the last (`repair_streamed`): 2, 4, 8 ticks, to a quarter
of a second at most, and at most a quarter of the game's `maxRewind` (the frames a
client presents must stay young enough to rewind to: 7 ticks for Mount & Rifle's
0.5 s, 3 for a quarter of a second). After a second with no stream it's every tick
again. The client's verified world falls that much further behind, as on a slower
network; reconnects and migration reset it.

Tried and taken back: letting a client in a stream confirm frames with its own
ticks and ask again only where one differs. It cut the scratch-fixed game's repairs
from 669 to 56, but a tick a client confirms itself (by the order-free projection
hash) can have its rows in another order than the server's (tables fill and settle
holes that hidden entities left), so the next repair's delta, made against the
server's bytes, failed against the client's and it resynced
(`net_alone_seen_hearing_its_events_runs_its_ticks`). That needs a repair without
a base (a wire change) or projections in a canonical row order (which changes the
order a client's systems see rows in): the owner's call. The same can happen today
to an exact repair after ticks a client confirmed itself.

Tests: `tidec_e2e_visibility_predicted` (a client loading the server's projection
each tick and running it: hidden spawns out of its projection, spawns into an
`Audience.All` group seen, and game `[Native]` fields neither sent nor hashed, over
a reload too); `tide_box3d_projected` (notes/box3d.md);
`visibility_repairs_back_off_while_a_client_goes_on_differing`
(`tests/test_visibility_session.c`: one client on a loopback with no delay, every
tick differing for 8 s: 137 loads, 18 in the last 4 s, most lag 14 ticks, against
489, 240 and 1 before; with `maxRewind` 0.25 s, a 3-tick gap and lag 3). All three
fail against the code before. `session_view_time_large_changing_repairs_keep_chunks_before_welcome`
is unchanged (292/320 bounded stamps, worst lag 18); with a quarter-second gap and
no rewind cap it had fallen to 263/320.

Validation: the whole native release suite (3,029 CTests, with the private
prototypes configured) passes; on `web-debug` (WASI, Node 24.22.1) and native
`sanitize` (Windows ASan), the engine test binary, `visibility_predicted`,
`visibility_projection`, `visibility_lists`, `shards_visibility`,
`net_alone_seen` and `tide_box3d_projected` pass (web also `visibility_groups`
and `visibility_lifecycle`). The docs site builds. Other operating systems,
Android and MinGW are CI's.

Measured on Mount & Rifle, the same build but for the session, two runs each (the
machine was shared and loaded, so times are high and vary; counts don't): repairs
packed 1,338 / 1,346 before, 401 / 424 after; pack and load 12.9 / 15.5 s over the
route before, 5.3 / 4.7 s after; match phase 8.96 / 10.56 ms a frame before,
7.89 / 6.55 after.

The former “after the last entity” spawn rule was resolved on 2026-11-06 by
logical identity above, without transferring private counts or histories.

Still open (the owner's):
- A repair base that doesn't depend on row order (above), which would let streams
  stop as soon as a client's ticks go as the server's.
- Physics queries from views of a world just loaded find nothing until the client
  ticks it (`Physics.Ready` is false at stamp 0). No game hits this today.
- Moving Box3D/Box2D bodies start again without warm starts or sleep state on a
  client after each projection, so their motion can differ until corrected.
- The backoff's numbers (a second, doubling, a quarter of a second, a quarter of
  `maxRewind`) are picks.
## The host's player shares the server's world (2026-11-04)

The owner's decision: the server's machine keeps no client world. Its player stays "a client" only in being a player like any other: it joins (`PlayerJoined`), has a `PlayerID`, owns entities, sends input and commands over the in-process loopback. The host may see everything the server does, so the match is ticked once there whoever else is in it. That replaces "A match played alone ticks once" below: `twin_ran`, `twin_take`, `twin_takes`, `host_runs`, `server_due`, `tide_client.host`/`host_due`/`taken_from`/`taken_to` are gone (`taken_world` stays, for remote projection reloads).

What's built (`engine/src/session.c`):
- **`tide_client.mirror`**, the server of the same session, set where the session makes its own player (`start_server`, `tide_session_take_over`). Never for shards (their clients are `tide_shard_client`'s) or a joined match.
- **Joining**: a mirror's HELLO claims neither a world nor `can_start`, so the server sends the plain world. On the WELCOME naming it, the client doesn't receive it: `load_snapshot` copies the server's world as it is (`snapshot_tick` = the server's tick) and the client then says it has the transfer whole (`got_number`). A later transfer is acknowledged the same way. Visibility games skip `hear_visibility_tick` at that load (the world isn't a projection).
- **Each server tick** (`server_tick`): `mirrored` is the session's player while `tide_session_update` updates the server (`tide_server.twin`), level with it (`verified == ahead == tick`, loaded, the same build). If it is and the game has local handlers that hear the match, the tick runs with `tick_events` into `tide_hearing_begin(&c->hearing, tick)` (`mirror_hearing`), or for visibility games with `tick_client_events(..., journals, all)` (below). After the tick, `mirror_tick` ends and confirms that hearing at once (predicted and verified handlers each hear it once), copies the server's world into the ring at `tick + 1` (`copy_world`: pages shared until either side writes), and sets `verified = ahead = tick + 1`.
- **`play`** for a mirror is `mirror_play`: no frame is applied, predicted, hashed or repaired (frames for ticks before `verified` are dropped as they arrive). Behind the server (after a build change, or a ring that had no room) it takes the server's world whole (`take_server_world`, now a full copy for a mirror even with `client_state`) and skips the hearing to there. It runs the clock part of `play` (`clock_back`, `keep_early`, sampling inputs up to the clock, `early_settled`). `adjust_delay` does nothing for a mirror.
- **Generated callback** (owner-approved design; compiler side): `tide_game.tick_client_events(world, jobs, per_player, all)` gets `all`, recording everything the tick sent, as `tick_events` does, into what was the generated `ignored` log. NULL keeps the old behavior. The Session ABI marker says `host events (1)`. Handwritten test games ignore it.

**The server does nothing for its own player that only a world of its own would need** (the owner: "seems like something we should do"). `tide_server.mirrors_local`, set by the session beside `tide_client.mirror`, makes `mirrors(s, c)` true for its `local` connection:
- `start_snapshot` packs nothing: the transfer is named and empty (`SEND_WORLD`, or `SEND_CLIENT_WORLD` with projections, 0 bytes), and it ends only once the client says it has that number (`on_client`: an empty transfer used to end at the first CLIENT, before the WELCOME naming it was even read, and started again and again).
- `send_frames` sends no frame. The connection has every tick (`frame_ack` = the server's tick, never catching up or sent the world again), and gets one SERVER packet an update for its times, its inputs' wait and its uploads.
- `server_tick` leaves it out of the recipients: no projection hash (`client_hashes`), no filtered inputs or journal (`store_client_frame`).
- Big commands don't wait for it to download them (`everyone_has`), and none goes down to it (`send_downloads`).

Measured with `net_alone_seen` alone (sanitize build, the change switched off by a scratch environment variable for comparison, three runs each): the host's session updates over 360 frames took 15.1 to 15.9 ms, against 25.9 to 28.3 ms. With a guest in, the guest's projection work is most of it (30.6 to 35.7 ms either way).

Tests: `net_a_host_with_another_player_ticks_once` also checks the host's player was sent no world bytes; the earlier tests above cover the rest. Validation: full native `sanitize` (3,036 CTests, prototypes included): all pass but `tide_server_smoke`, which failed once to listen on UDP port 7777 that something else on the machine held, and passes 20 runs of 20 alone (its dedicated server never mirrors). Web and other platforms are left to the Build workflow at the owner's word.

What changes for games: the host's views and local code see the whole match, hidden entities included (the spec's State visibility now says so; whether its views should be masked by its player's audience is Open there). Its local handlers hear every match event, including events from hidden state, which `hears_match_events` no longer decides. `tide_client_status.run_ticks`/`ahead_ticks` stay 0 for the host. The other machines are unchanged.

Correction (2026-11-15): in a game that hides state, `all` had the match's own events but no genuine `Spawned` or `Destroyed`, no destruction cause and no `Changed<...>`: the generated lifecycle hearing wrote only the players' journals while `tick_client_events` ran. So the host heard neither lifecycle event and ran `Hidden` for every entity the match destroyed. Fixed in the generated C (`tide_client_all`; notes/verified-and-local-handlers.md, top section), tested by `tidec_e2e_visibility_host_lifecycle`. `tick_begin` now also clears what a scene's preparation sent where no player of the server's machine takes what's heard (`heard_here`: a dedicated server, a shard, a host that doesn't call `tide_session_events`), where it used to keep it for good; for a host whose player is there but not level yet it waits, as before (the review's correction: the first cut cleared it there too).

Gaps:
- A lone developer no longer gets the twin's desync check (the host's client hashing its own run of each tick against the server's). Nondeterminism shows only with another machine in (`--players 2`, the networking tests). An opt-in check (running the twin again) was offered to the owner and isn't built.
- The host's presentation has no prediction correction to smooth, and draws the server's tick as it is.

Tests: `net_a_host_with_another_player_ticks_once` (`compiler/tests/e2e/net_test.c`: a host and a guest over 30 ms, 10 ms jitter and 5% loss, 300 uneven frames; the counted game ticks exactly once per confirmed tick, the host's player runs none, its world is the server's every frame, both players' inputs reach the match, no resyncs). `net_alone_seen` now checks the host instead: alone and with a guest, ticks run equal ticks confirmed, the host has all 8 walkers every frame while the guest sees fewer, and every Rang and every walker's Shown is heard once and once as verified (before, a game hearing Rang ran both ticks, and the twin's Shown counted only walkers in sight). `delta_players_coming_back_to_a_match_that_changed_hands_get_what_they_lack`: the new host's player receives 0 world bytes (it shares its server's world); the guest still gets what it lacks. The existing `net_a_session_played_alone_ticks_once` and `net_a_session_takes_a_new_build_of_its_game` pass unchanged.

Validation: full native `sanitize` (3,028 CTests, Windows ASan, tide-prototypes included through `TIDE_EXTERNAL_DIRS`) and full `web-debug` (2,899, engine only) pass, and the docs build. Other operating systems, Android and MinGW are CI's.

## Projection hashes follow what changed (2026-11-04)

Every tick the server hashes each player's projection (`client_hashes`). Its
singleton part used to encode every public singleton into one buffer and hash
it: in Mount & Rifle (Box3D's `Physics` with its rewind frames and colliders,
the campaign's build-once `Atlas`, `Ground`, `Pathing`, ...) that was 1.8 ms a
tick in release, more than the game's systems, and it grew with the world.

The singletons now hash as trees (`gen_client_hvs`, `compiler/src/client_state_codegen.inc`):
a value's bytes are its codec's (`tide_client_put<id>`), but each list, map, set
and grid in it is the 8 bytes of its own hash, of its bytes made the same way.
A container's hash is a function of its value alone, so the server and a client
(their heaps laid out differently) agree on it as before, on every platform and
build; it's only a different function of the same values, a 64-bit hash of
hashes (`singleton tree hashes` is in the simulation identity). Text stays inline.
Singletons holding nothing in the heap, entities, inputs and commands hash as
they did. Games with transform hierarchies hashed `client_state`'s bytes until
2026-11-15; they now hash as the others do, with each recipient's links and
anchors as one section (notes/voxels.md, section 1).

What makes it cheap: a container of 256 bytes or more keeps its hash in a memo,
by its handle and codec, with every heap page its value was read from (its own
blocks, its chunks, its elements' text and containers, all it holds; a hit
counts its pages into its parent's). The memo says the hash again while each of
those pages, by its place, has the same size, start and page hash. Pages keep
their hashes until written (`tide_heap_write` or a copy marks them), and the
server has just hashed its world, so an unchanged container costs a comparison
a page; a changed one is encoded again, its unchanged children taken from the
memo. A value that reads anything outside the match's heap (the pack's) isn't
kept. The memo is only a shortcut to a hash the value has: a page hash
collision is the only way it could be wrong, which is what the world's own hash
already rests on. Memos are the process's (`tide_client_memos`, 8, globals so no
thread's exit loses them), one per world last hashed, taken under a spin lock
and busy while a thread hashes with it; what a call didn't use goes once it's
most of what's kept. No runtime function or `tide_game` member was added:
generated code reads list, grid and page headers and calls `tide_heap_block_hash`.

Bug found on the way, kept as a test: the walk records each page once in a row,
and comparing against the last page recorded before the container's own start
left that page out of the container's set (a grid whose directory shared its
page with the list before it was taken from the memo after a cell changed).

Tests: `tidec_e2e_visibility_hash_memo` (`compiler/tests/e2e/visibility_hash_memo.tide`):
singletons with a list of structs with text and lists, a map of lists, a set, a
list in chunks, a ring of frames of points and notes, a grid; a tick changes one
thing deep in one of them. Over 150 ticks each projection hash differs from the
tick before, equals itself again, the snapshot of the tick before still hashes
as it did, and a world the projection loads into (another heap) hashes the same,
warm and, on odd ticks, after eight other worlds pushed its memo out. The 150
hashes together are pinned, and `tidec_e2e_visibility_hash_memo_off` (the same,
`TIDE_CLIENT_MEMO_OFF`: nothing kept) must give the same pin. A second case
changes values in place (a byte in a chunked list, an int in a list in a list's
element, a point of a frame), with and without a snapshot sharing their pages:
each change shows and undoing it gives the hash back. Validated native release,
`sanitize` (Windows ASan) and `web-debug`: these, the visibility, double,
event_containers and shards_visibility fixtures, `earth_tests`, `bench_earth*`
and `tide_cornerculling`, the games whose generated code changed (every game's
simulation ID moves, as with any compiler edit).

Measured (Windows, shared machine, so runs vary):
- Microbenchmark, that fixture's game in release (551 KB projection, one thing
  changed a tick): projection hash 0.25-0.28 ms a tick before, 0.042 after;
  hashed again with nothing changed, 0.25 ms before, 0.001 after.
- Mount & Rifle (HEAD 6e963ed, tide 0.6.0-dev from this branch with and without
  the change, route `tools/performance-route.txt`, seed 42, 3230 frames,
  1280x720, `--profile --profile-warmup 200`, a scratch timer around
  `client_hashes` in session.c): release 2.43, 2.21 and 2.51 ms a tick before,
  0.49, 1.03 (a run the machine's load slowed throughout) and 0.57 after; debug
  4.24 before, 1.57 after. The profile's match phase moves by about that much
  but is within the machine's noise between runs.

What's left: changed containers are encoded whole (a big list whose one element
changes is encoded again, not its changed chunk); entities are still hashed
every tick, each from its components; and the client's verification hash goes
the same way but on a world loaded or predicted afresh it finds less unchanged.

## Service the match while presentation waits (2026-11-03; Android 2026-11-14)

The owner prioritized networking after the focus measurements below. Desktop
Vulkan's end-present frame fence now waits to the next match deadline and calls
the standard host's match-only update between waits; Android's activity does
the same since 2026-11-14 (`notes/rendering.md`, Match updates during Vulkan
frame waits). The GPU still owns its
unfinished resources, and the existing two-frame queue limit remains. Rendering
details and remaining blocking points are in rendering.md; clock, local event,
input and correction-aging contracts are in standard-host.md.

This changes host scheduling, not the session protocol, world data, prediction
policy, tick hashes or Tide language. Fresh local input is still polled at the
rendered frame rate; remote input and session traffic can progress during this
specific wait. GPU execution/scheduling time and the background FPS difference
are separate from the networking improvement.

Windows ASan validation passes all 2,989 native CTests, including the forced
GPU waits, real host/session fixture, generated correction/RNG regression,
reload/embed, input, rendering and networking coverage. The browser fixture
uses an explicit exit after the shared test runner: returning from main alone
leaves Tide's asynchronous page alive. Its three assertions passed on the first
web run, and its completion wrapper now passes the browser runner too; the
updated fixture also passes again natively. Logs are in build/focus-probe.
The complete web-debug run passes the other 2,873 tests, including WebGL,
WebGPU and D3D11. With that fixture's recheck, all 2,874 web tests pass. No
test assertions were loosened, deterministic hashes changed, or extra CI
dispatched. Other desktop operating systems and Android remain CI coverage.

An optimized Mount & Rifle comparison used the same checkout-built executable
with the update hook disabled/enabled, seed 42, two direct-UDP peers, a 60 Hz
client, scripted E to request on-foot play and 60-second bounded muted runs.
The scratch end-present fence hook held completion for 100 ms on each host
frame from roughly seconds 12–59, while still requiring the actual GPU fence
to complete before resource reuse. This is injected presentation delay, not a
measurement of 100 ms of GPU work. The steady 15–58-second interval verified
host=1/remote peers=1 and client=0/connected for every sample in both runs.

Host gaps between session updates fell from mean 109.58 ms / p95 111.39 ms /
max 112.62 ms to 13.33 / 18.20 / 19.36 ms. It serviced 2,818 updates inside
waits in that interval. Host drawing stayed slow (9.13 vs 9.46 FPS), as intended;
the client's rate remained 59.54 vs 59.52 FPS. Neither peer accumulated late
inputs during either measured interval. These are update-cadence measurements,
not end-to-end button/display latency or an Internet benchmark. Final logs,
instrumented build scripts and network-summary.json are in build/focus-probe;
the four valid stall-before/network and stall-after/network processes exited 0.

The intended new foreground/background comparison was inconclusive: neither
window ever reported foreground, and the UI screenshot showed desktop wallpaper.
The desktop-state cause was not established, so those runs are excluded. An old
solo-play Escape step also canceled a client's lobby join in an initial trial;
the final script omits it and checks peer roles throughout. The earlier measured
focus bottleneck below motivated the patch; no new focus-FPS improvement is
claimed, nor any driver-policy change. New local input still waits for the next
normal device poll, and the other blocking GPU boundaries remain unchanged.

## Background rendering delays the next session update (2026-11-02)

The owner's Mount & Rifle focus report was reproduced with two independent
matches on the same PC, excluding match traffic as the cause. Owner-approved
visible tests used muted, auto-closing 60-second windows; all changes were
diagnostic copies under build/focus-probe, with TIDE_HIDDEN otherwise retained.
Current checkout-built game/engine, seed 42, scripted on-foot view, Vulkan FIFO,
RTX 4080 SUPER driver 617.42, 1920x1080 desktop. No settings or production code
changed. Real elapsed frame time and onscreen presentation replaced the hidden
runner's fixed time/offscreen target; ordinary scripts still bounded the run.

At 1280x720 with overlapping windows, the clean steady interval (12-59 seconds)
measured foreground 143.9 FPS versus background 23.8 FPS. Game-frame CPU work
was 4.19/6.78 ms; presentation wrapper 2.73/35.19 ms, including GPU fence waits
of 2.50/34.96 ms. Background fence p95 was 56.51 ms. Acquisition and queue-present
calls themselves were negligible. GPU timestamp intervals were 4.93/41.28 ms;
these include elapsed scheduling gaps, not just exclusive execution by that app.

A repeat with the test windows requesting focus in alternating ten-second
phases moved the slowdown with focus: each copy ran about 144 FPS foreground
and 30-32 FPS background. Side-by-side 940x700 windows also differed: 144 FPS
foreground, 82-83 background. Different dimensions mean this is not a measured
occlusion-only comparison, but coverage is not necessary for the slowdown.
Disabling the foreground copy's drawing, while keeping its game updates capped
at 60 Hz, raised the background game to 132 FPS. This implicates
competing rendering/presentation workloads rather than a fixed Tide focus cap;
the driver's exact scheduling decision is not established.

The dominant wait is the next-frame fence in `tide_gpu_vk_present`, after the
host's frame function on the same thread as its session updates. Thus a GPU-bound
background host/client also updates its match less often. Short-stall clock
recovery below does not remove persistent rendering waits. Separating update
cadence from readiness to render is follow-up work, not implemented by this test.

An external UI activation helper caused a separate access violation in GLFW's
`_glfwPollEventsWin32` modifier-key check (`win32_window.c:2159`), captured with
LLDB in build/focus-probe/debug-A.log. Clean runs avoided that helper and exited
normally; no ordinary-human focus crash or underlying cause is claimed. Final
measurements/logs/build scripts are in build/focus-probe (clean, timed, side and
capped runs; analyze-final.py/final-summary.json). No browser, other GPU or
other-OS coverage; no engine fix or new correctness-test claim accompanies this
measurement-only investigation.

## Short server stalls recover their input lead promptly (2026-11-02)

A half-second server pause left clients sending inputs hundreds of milliseconds
too early. It fit inside the prediction window, so `clock_back` did nothing;
ordinary `keep_early` waited 2 or 10 seconds before shedding the excess at 10%.
Other machines consequently saw fresh actions late well after the pause ended.

`clock_back` now also detects a smaller excess from two timed server replies:
the input clock is more than 100 ms (and more than two ticks) beyond the server
tick plus the configured lead and that reply's full round trip. Slow/reordered
traffic alone does not establish this excess. It removes the proven difference
at 50% of elapsed time, retaining one extra tick for the fractional server/RTT
parts rounded down. The existing two-reply hard reset beyond the input window
keeps its separate evidence counter and behavior.

This path never rewinds sampled inputs, replaces sent bytes or resets prediction.
Prediction temporarily advances at half speed, continuing to sample fresh input;
resetting the clock backward instead would stop sampling until previously sent
ticks came round again. No language, wire, game callback or simulation-state
layout change. Normal lead tuning remains unchanged. Fast correction may replace
ordinary slow correction, but cannot re-add unfinished fast debt. Wait feedback
is ignored until inputs sampled under the adjusted clock have run. Timed replies
can establish excess while visibility repairs lag; they do not depend on having
confirmed every advertised tick, since smooth recovery rewrites no history.

`seen_a_short_server_stall_recovers_without_freezing_own_input` tests two 500 ms
server pauses on the same connection, 30/60 Hz matches, 60 Hz updates, clean
30 ms/way and 80 ms/way with 10 ms jitter/3% loss. It checks monotonic sampling
and prediction, actual local presentation response, sample gaps, server response
to a new press two seconds after each pause, input wait and no resyncs. Linking
the same fixture with the original session fails 16 latency assertions. At
60 Hz on the lossy profile the server response falls from 31 frames to 7 after
both pauses; across all eight cases, later input wait stays at most 70 ms,
local presentation responds within one frame at 60 Hz/three at 30 Hz, sampling
gaps stay within two/four frames, and no input is late. The initial experiment
without the extra tick caused late inputs after recovery; the cushion fixes it.

Mount & Rifle paired diagnostic: identical compiled game/host, seed 42, 60 Hz
frames, 80 ms/way, 10 ms jitter, 3% loss, server updates paused from 7 to 7.5 s.
A press 0.83 s after resuming appeared in the host-side world in 517 ms before,
283 ms after; a press 2.5 s after resuming took 517 ms before, 150 ms after
(the previous no-stall baseline). During recovery the fixed local simulation
responded in 17 ms and its camera in 33 ms; afterward, immediately/17 ms.
Adaptive input delay stayed zero, with its real CPU clock enabled. These are
simulated elapsed-time/world-state measurements, not display or Internet/WebRTC
latency; the pause stops server updates only. Scratch artifacts and reproduction
are in build/input-probe/fast-comparison.json and build-fast.py. The game's
nightly pin is unchanged. The older reported inverted local/remote delay remains
unreproduced; this is not a general fix for arbitrary repair or CPU backlog.

Validation: Windows ASan and WASI pass the 15 session and eight visibility
cases, plus the complete seen, net, net_join_big, predict, guess, view_time and
visibility_lifecycle fixtures. Native/web repeated-stall measurements agree.
Existing long-stall recovery, delayed/reordered replies, joins, adaptive input
delay, prediction and repair checks pass. Independent source review identified
the need for separate hard/soft evidence counters, included above. Other native
platforms remain CI coverage; no browser or Internet transport test was run.

## Received projections reuse decoded state (2026-11-02)

Mount & Rifle now exercises the existing `reload_client_state` path for remote
snapshots and repairs as well as solo takes. `unpack_client_state` first validates
the delta and byte hash, copies the last accepted, never-ticked decoded projection,
and reloads over its original bytes. Missing/refused reloads use the existing fresh
loader. Re-serialization/hash, endpoint/frame hash and event-journal checks remain.
Only an accepted endpoint replaces the cache; prediction never mutates it. Existing
build-change, migration and destruction cleanup owns the cache. Games without the
callback keep the old path. No wire, language or generated-C API changes.

Cost: a remote client now retains one decoded projection and its bytes, using the
same cache as solo takes. World pages share with snapshots; changed pages copy on
write. This saves rebuilding static lists, maps and text during recurring repairs,
not serialization of the delta baseline or the checks after decoding.

Real-game experiment: current engine checkout, Mount & Rifle sources from
D:/Projects/mount-and-rifle, optimized Windows build, two standard-host players,
180 frames at 320x180, 80 ms one-way latency, 10 ms jitter, 3% loss. A scratch
switch disabled only reload reuse in the same instrumented executable. Both runs
decoded 41 projections, 39 eligible for reuse. Timed copy/reload-or-load work took
10.1850 ms total fresh versus 5.7131 ms with reuse (248.4 versus 139.3 us per
projection, 44% less). This is one paired run, not a whole-frame speedup or an
Internet connection-time result. Scratch logs/scripts are in build/mp-probe;
no timing calls, switches or logging remain in production.

Connection investigation: the baseline 240-frame run at those network conditions
loaded the remote initial snapshot at simulated time 0.833333 s: 56,244 compressed
bytes, endpoint tick 19 versus advertised tick 41. This excludes room signalling,
ICE/DTLS and actual bandwidth limits. Packing requests after the current update's
ticks instead produced 1.05 s in the same seeded network (changed traffic also
changes loss draws); that experiment was reverted. No initial-join improvement is
claimed. The game's pinned nightly.14 was not changed or benchmarked here.

Validation: 15 session/packet/view-time and eight visibility tests pass on Windows
ASan and WebAssembly/WASI (Node 24.22.1). The extended view-time test uses reloads
through lossy repairs, checks that each old world exactly matches its original
bytes despite intervening prediction, and deliberately refuses every third reload
to exercise fallback. Generated visibility_lists and net_alone_seen CTests pass
on both platforms, covering unchanged-page sharing, fresh-load equivalence,
truncated-codec rejection, prediction and solo takes. The real game's 240-frame
two-client run also completes with the change. Other native platforms are CI's;
actual Internet/WebRTC joining and browser rendering remain unmeasured.

## Protocol 21: 64-bit handles (2026-10-30)

`PROTOCOL` is 21 (`engine/src/session.c`, and the tests that write it by hand:
`tests/test_commands.c`, `test_session_packets.c`, `test_session_view_time.c`,
`packages/voice/test/voice_test.c`). Text, list, map, set and grid handles are
64 bits (notes/ecs.md, 64-bit handles), so every layout holding one, every page
and so every world hash changed: two builds on either side would connect and
then disagree. On the wire, deltas carry a handle column's zero high words as
runs of zeros, and the heap's numbers as varints (`tide_delta_number64`); raw
world bytes carry the heap's numbers and the asset dictionary's places as
`uint64_t`. Commands, inputs and recipient state carry values, never handles,
so they change only by the sizes of structs. Received heaps are checked at
least as strictly as before (notes/memory-safety.md, 64-bit handles).

## A lone player's take loaded over the last one (2026-11-02)

The owner approved a new generated callback for the follow-up below ("yes build it"). `tide_game.reload_client_state(world, data, size, had, had_size)` (`engine/include/tide/session.h`; generated by `gen_client_reload` in `compiler/src/client_state_codegen.inc` for every visibility game without transform hierarchies): `world` was loaded from `had` and is as it was loaded; it becomes what loading `data` would make it. It copies the world page by page (`tide_world_copy`). Then it goes past each singleton's and each archetype's bytes in both projections (`tide_client_skip<n>`, `tide_client_skip_rows<a>`: the value codecs' readers, making nothing) and keeps those whose bytes are the same. The rest are let go of, with what they held in the heap, before one `tide_heap_flush`, and read as `load_client_state` reads them. Entity slots are made again: those of a kept table from its rows, with their snaps as they were. Refused bytes leave the world as it was, as `load_client_state`'s do. Values come out as a fresh load's; only where the heap keeps them differs, and nothing reads that (a client's projected world is never sent, packed for others or hashed physically). The generated header's Session ABI marker says `reload projection (1)`, so hosts and games rebuild together. The wire, projection bytes and hashes are unchanged.

The session (`twin_take`): where the game has the callback, the client keeps the last projection it took as loaded (`tide_client.taken_world`, never ticked) and its bytes (`taken_bytes`). It reloads that world over them, falling back to a fresh load when there's none or the reload refuses, then shares it with the ring's world (`copy_world`). `drop_taken` frees it with the build that loaded it: as a build changes (`client_set_game`, migration) and when the client goes. Games without the callback (with hierarchies, or handwritten) take as before.

Measured on mount-and-rifle's route (as below): the match phase 0.48 and 0.50 ms a frame, the engine's part 0.34 and 0.36 ms (from 1.55 to 2.01 before any of this work, 0.59 to 0.68 after the first part). What's left is serializing and hashing the projection on the server and skipping the client's bytes to compare them.

Screenshots: rendering on this route isn't the same from run to run (its draw-call count differs in every run of every build). In 2 of about 12 runs of builds with the reload, the final capture differed from the reference by one level in 6 pixels, the same alternate image each time. A build that ran the reload but presented a fresh load (a temporary experiment) gave the same alternate image in 2 of 8 runs, so the reloaded world's contents aren't its cause. The merged `dev` before the reload (9abc3c4bc, which brought in the quick frustum test and idle pack-image changes) gives it in 2 of 10 runs too, while 11 runs of the build before that merge never did. So the variation arrived with that merge and isn't this work's; it's left to whoever owns those renderer changes.

Tests: `visibility_lists_reload_over_the_last_projection` (`compiler/tests/e2e/visibility_lists.tide`, whose `Churn` and `Grow` systems change some of the world each tick from the second: a singleton's list now and then, a component's text list, entities spawned and destroyed). Over 120 ticks each reload re-projects to the server's bytes and hash and has a fresh load's entity count. A tick predicted from it comes out as from a fresh load. On ticks that leave `Bulk` alone, its elements are still on the page shared with the world before (kept, not read again). Truncated new or old bytes are refused with the world as it was, and the heap stays within twice a fresh load's pages. `net_alone_seen` and the other visibility session tests take through the reload.

Validation: full native `sanitize` (2,976 CTests, Windows ASan) and `web-debug` (2,864) suites, engine only, pass. Other operating systems, Android and MinGW are CI's.

## A lone player's take, cheaper (2026-11-02)

Played alone with visibility, every tick the server serializes its projection for the player (`client_state`), the client loads it (`load_client_state`, `twin_take`), the server hashes every player's projection (`client_hashes`) and the client hashed the world it had just loaded. In mount-and-rifle (big public singletons: a 22,500-byte `List<Footing>`, catalogs of structs with text, maps) that was nearly all of a frame's match phase: 1.59 ms of engine work against 0.16 ms of systems on its performance route (`tools/performance-route.txt`, release, seed 42, 1280x720, profile warmup 200, this checkout's tide before the change). A stack sample put half of the load in `tide_list_insert`/`reserve` (lists read an element at a time) and most of the rest in per-field writer/reader calls.

What changed, none of it in a world's bytes, a projection's bytes, a hash or the wire:
- **List codecs in runs** (`compiler/src/value_codec_codegen.inc`). An element type whose codec bytes are its memory (`client_codec_raw`: numbers, vectors, entities, players, enums, and padding-free structs and arrays of them; not bools, T?, text or containers) is written as one copy per run of side-by-side elements (through `tide_list_cached_at`) and read the same way. Every list is grown by `tide_client_list_grow` exactly as `tide_list_add` grows it: an add that would take room calls the same `reserve` through `tide_list_resize(count + 1)`, and the adds that wouldn't are done at once. Room is filled before the list grows again, since a block a list outgrows is let go of with its elements still in it and those bytes are hashed with the heap. So a loaded world, and a shard message or local-event record read with the same codecs, is byte for byte what it was. `tide_client_write`/`tide_client_read` inline the writer's and reader's common case for every field.
- **A taken tick isn't hashed again** (`engine/src/session.c`, the confirmation loop): the frame's hash is the server's `client_hash` of the very world the client loaded, so the client accepts a tick it took (`taken_from`/`taken_to`) without hashing it. The server still hashes its world and every projection every tick; predicted, repaired and caught-up ticks are checked as before. A codec that loaded a world unlike the server's would now show at the next predicted tick rather than the taken one; the round trip is pinned by the visibility tests below.
- **New pages from the spares** (`engine/src/page.c`, `tide_page_new`): a page of a size kept spare (4 bytes to 16 KiB) is one let go of, zeroed, rather than `calloc`'s: a world loaded every tick takes as many as it let go of. Other sizes are still the system's.

Measured on the route (one tick a frame; the machine is shared, so runs vary by about 20%): the match phase from 1.76 and 2.26 ms a frame before to 0.90 and 0.78 after; less the systems, the engine's part from 1.59 and 2.01 ms to 0.68 and 0.59. The route's final screenshot is byte for byte the same. Of what's left, the take's load is about half (reading text, with its UTF-8 checks, entities and maps) and serializing and hashing the projection on the server a third. Going further means not rebuilding what didn't change: loading over the previous tick's taken world, keeping singletons whose bytes are the same. That needs a new `tide_game` callback (a generated-C/runtime contract), so it awaits the owner.

Tests: `tidec_e2e_visibility_lists` (`compiler/tests/e2e/visibility_lists.tide`): a projection with a chunked list of padding-free structs, a 20,000-byte list, small, padded and text lists, nested lists and a component's lists reads back to the same projection bytes and hash, and the loaded world's own hash (`tide_world_hash`: its heap's blocks and the bytes freed blocks keep) is pinned at the value the element-by-element codec gave, taken with tidec before the change. Every truncation is refused with the world left as it was. The existing `net_alone_seen` cases check the take each frame. `pages_let_go_are_handed_out_again` (`tests/test_heap.c`) now checks a new page is a spare, zeroed.

Validation: full native `sanitize` (2,976 CTests, Windows ASan) and full `web-debug` (2,864) suites, engine only (no external directories), pass; the one failure in each first run was that heap test's old assumption, fixed and rerun on both. Other operating systems, Android and MinGW are CI's.

## A match played alone ticks once (2026-10-30)

Superseded on 2026-11-04 by "The host's player shares the server's world" above: the mechanism below is removed. Kept for the measurements and the reasons.

A session that hosts its own match runs a server and this machine's client in one process, and each ran every tick: the client its prediction, the server the same tick with the same input. A game played alone paid for its simulation twice (a battle of 20000 fighters in the Bannerlord duel's tools/army: 11.9 ms a tick, 23.8 ms a frame at 60 ticks a second). Now, while `tide_session_update` updates its server, the server knows this machine's client (`tide_server.twin`), and a tick the client has run already as the server would is taken from it (`twin_ran`, a copy of its world) rather than run again: 11.4 ms a frame. Only when that's certain to be the same world: no recipient projections (`client_state`), no other player connected, the client predicting with nothing to catch up, repair or get again, its verified tick the one the server is about to run (so the world it ran from is the server's, checked by hash) and the frame as it predicted it (`as_predicted`: its own input and nothing else, no joins, leaves, commands or shard messages). Anything else, the server ticks as before. The client goes on hashing and confirming as it did.

Tests: `net_a_session_played_alone_ticks_once` (each tick run once over 240 frames of changing input, the worlds alike every frame); `net_a_session_takes_a_new_build_of_its_game` counts a new build's ticks once, not twice.

### Played alone, seeing part of the match (2026-10-30)

The Bannerlord duel's campaign still showed "Ticks 2.0 a frame". It has visibility (its `Sight` system hides parties more than 9 map units from the player's), so its server ticks through `tick_client_events` and never reached `twin_ran`, which refuses `client_state` anyway. A lone player doesn't see everything: the visibility a game sets decides, alone or not, and the hidden parties aren't in the player's world, so the server can't take it. The other way round can work: the server's projection for the player (`client_state` of its world, loaded with `load_client_state`) is what the server confirms, so the client can take that rather than run its part of the tick (`twin_take`). In the session's update the client is told the ticks its server runs then (`tide_client.host`, `host_due`, from `server_due`). It doesn't predict the next of them (`host_runs`). The server runs it as always, then loads its projection into the client's world after the tick. The client confirms it as a predicted tick: the hash is checked, but `as_predicted` isn't, since whatever the frame held is in the world already (`taken_from`/`taken_to`; a prediction or a loaded world over those ticks drops the mark). The conditions otherwise match `twin_ran`'s: the client predicting, loaded, with nothing to catch up, repair or receive, the tick its next to run, no other player connected, the same build.

What local handlers hear decides it. With visibility, the server journals only lifecycle events per player (Spawned, Destroyed), and the client finds Shown and Hidden in its worlds. The match's own events, though, a player hears only from its own run of a tick (docs/spec.md, visibility journals). So the take applies only to games whose local handlers hear none of those: `tide_game.hears_match_events`, generated when a non-local event other than the four has a local listener (owner's choice, 2026-10-30, over journaling the match's own events from the server's tick, which would let events computed from hidden state reach local code). The campaign hears its combat events (`combat/sounds.tide`: Clashed, Struck, Swung and others), so it still runs both ticks: at about 0.05 ms a tick there, around 0.05 ms a frame.

Tests: `net_alone_seen_ticks_once` (`compiler/tests/e2e/net_alone_seen.tide`: walkers passing in and out of a lone player's sight, spawned and destroyed; it claims `hears_match_events` false. Each tick runs once over 360 frames of uneven length, the player's projection is byte for byte the server's every frame, and Shown is heard once and once as verified). Before the change it ran 764 ticks for 360 confirmed. `net_alone_seen_hearing_its_events_runs_its_ticks`: the same game as generated, hearing its `Rang`, runs both and hears every Rang.

In that game, without the take, `verified Rang` is heard for 2 of 13 Rangs predicted. That is the documented behavior (docs/spec.md, Ticks a world sent again skips), not a hearing bug. The walkers come into sight about once a second, and each time the client's tick differs from the server's projection. Two repairs within a second start a second of streamed repairs (projected repair delivery, below), and a mismatch soon after a stream starts another. So the client spends most of its time on repaired ticks, which skip their hearing records (`tide_hearing_skip_to`).

The campaign, measured as a lone host idling on the map over 600 ticks (instrumented nightly 0.6.0-nightly.1 engine), spent 55 ms on server ticks and 45 ms on client ticks. It also ran 300 repairs: 51 ms packing on the server and 100 ms loading on the client. With the take forced on (its combat sounds lost; measurement only) it spent 50 ms on server ticks and 128 ms on takes (a 69 KB projection serialized and loaded each tick), with no repairs. Without repair streaming for the local connection it mismatched 587 ticks of 600 and took over 2 s, so streaming stays.

The campaign mismatches nearly every tick because its public `Crowd` singleton (`combat/strike.tide`, `Gather`) is rebuilt each tick from every `Fighter`, including fighters of AI battles hidden from the player. That also sends the hidden fighters' positions to every player. It is a game fix (a crowd per battle, hidden with it, or local), handed to the duel's thread on 2026-10-30. A compiler warning for public singletons written from queries that can match visibility-restricted entities is a possible follow-up, awaiting the owner.

## Protocol 20: rows entities left are holes (2026-10-29)

`PROTOCOL` is 20 (`engine/src/session.c`, and the tests that write it by hand:
`tests/test_commands.c`, `test_session_packets.c`, `test_session_view_time.c`,
`packages/voice/test/voice_test.c`). Builds before and after place rows
differently once entities leave (notes/ecs.md, Holes), so the same game would
agree on its first ticks and then disagree.

## Projected repair delivery on unordered traffic (2026-10-26)

The follow-up to the view-time fix below measures and fixes actual repair lag,
without changing maxRewind, timestamps, visibility policy, hashes or the wire.
The unchanged BREACH repro's first 480 ticks at 20 ms one-way latency, 5 ms
jitter and 3% loss had 101 repair adoptions: compressed size median 711 bytes,
maximum 98,249; transfer time median 133 ms, maximum 283 ms; adoption age median
150 ms, maximum 417 ms. Eleven requests already named endpoints over 100 ms
old when packed. Across all 1,800 ticks, 350 of 407 repairs were under 1 KB,
yet 192 of those small transfers took over 100 ms. A 64-chunk update budget
was not the bottleneck, nor was the confirmed presentation clock.

WELCOME and its chunks travel unordered. Jitter regularly delivered chunks
before WELCOME, and the client discarded them until timeout retransmission
(1.5 answer round trips, about 100 ms here). Projected clients now retain one
upcoming transfer's authenticated chunks, capped at one 64-chunk send burst
(64,512 bytes of lazy retained storage). Matching, accepted WELCOME metadata
feeds them through the ordinary exact-size/index/duplicate chunk validator.
Queued bytes are not acknowledged before that metadata validates their chunks;
world publication and verification still require the complete baseline,
canonical hash and repair checks. Overflow falls back to retransmission. Old transfers are ignored; pending
bytes clear on disconnect, reload or authoritative replacement and are freed
at client destruction. Address/cookie/attempt checks still precede buffering.

For projected transfers, an acknowledgement of later chunks permits one early
retry of each missing chunk; ordinary timeout retransmission remains available.
The existing 64-chunk update budget is unchanged. A byte per sent chunk bounds
the extra retry; packet reordering can cause at most one such extra copy per
chunk. Equal-prefix acknowledgement masks accumulate instead of forgetting
already received bits. Non-projected world transfers keep their original
early-chunk/timeout behavior.

Valid stream requests also refresh the recent-repair time. Periodic prediction
probes still run, but a failed probe resumes fresh coalesced repair immediately
instead of restarting with old exact endpoints. Accepted repair work is packed
after the update's authoritative ticks and all incoming acknowledgements:
stream requests choose the actual latest endpoint; exact requests preserve
their requested tick. Existing in-flight bytes/baselines remain stable, and
permitted lifecycle journals retain their original ordering and validation.
Pending work clears on reconnect and build migration. No verified clock moves
past an actual decoded endpoint; all admission/future/fraction/age guards stay.

A scratch 480-tick real-game trace of the bounded delivery/probe changes
retained 315 early chunks, adopted 179 repairs, and measured median 33 ms
transfer / 50 ms adoption age. Only the two initial catch-up repairs started
over 100 ms old; sampled receive-to-presentation lag was zero. The final
end-of-update scheduling additionally removes one tick of stream endpoint age.
Scratch instrumentation remains only under ignored build/debug; it is not
production telemetry or a changed game harness.

The CPU regression in `tests/test_session_view_time.c` now includes about 70 KB
of random public history rotating through 17 frames, changing every tick from
hidden authoritative consequences. Its transport holds WELCOME across an
update while chunks pass, over the same 20 ms/5 ms/3% profile. Every observed
confirmed world is checked against its authoritative canonical hash. Final
native/WASI agree: 128 confirmations, 292/320 supplied stamps inside the
unchanged bound, maximum confirmation lag 18 ticks, zero resyncs/lost actual
stamps. Linking the same fixture against e16a820 fails: 90 confirmations,
3/320 bounded stamps and a 46-tick worst lag. The existing render-pause case
still checks every rejected/accepted stamp; its expired-count floor now
measures the deliberate pause rather than relying on the removed network lag.

Final native/WASI validation passes the 15 session cases, seven existing
visibility cases (the visibility filter also selects the consecutive-repair
regression), generated `view_time`, and both complete Box2D/Box3D actual-frame
rewind-network fixtures. The unchanged 1,800-tick real BREACH repro passes on
both platforms with identical results: 378/386 canonical confirmed worlds,
177/194 hidden updates, 167/187 hidden movement updates, 52/57 availability
transitions and zero resyncs. All 3,503 supplied valid claims have zero
rejections at ages <=250 ms; only 117 older claims expire, versus 1,966 before
this follow-up. This is not a guarantee for arbitrary loss or a wider rewind
window: expired claims still fail, and each correction still advances only
when its actual permitted state arrives. No prototype source/assertion changed.
All commands retain TIDE_HIDDEN; no GPU, native window, browser or CI run.
Independent read-only review approved both the bounded transfer changes and
the final post-tick scheduling/reconnect/migration diff without blockers.

## Presented view times survive visibility repair (2026-10-26)

BREACH's real lossy culling test exposed two session bugs. Each new repair
moved `snapshot_tick`, which the server also used as its view-time floor;
legitimate frames already displayed or in flight then became pre-snapshot
claims. Coalesced repair adoption also cleared the client's `presented_view`
before the same update sampled input, although no replacement frame had drawn.

The connection now has a private `view_start`: the authoritative tick when
its first fully decoded, cookie/attempt-authenticated CLIENT is accepted while
`welcoming`. This is the admission floor for that connection attempt, including
tick zero. A cookie reconnect starts a new attempt and resets it at that
attempt's first authenticated CLIENT. Repairs, replacement transfers and repair
fallbacks leave this floor unchanged; `snapshot_tick` remains transfer state.
The last scalar tick/fraction actually marked presented survives a coalesced
repair until another frame is marked. Missing intermediate snapshots still
reset the confirmed interpolation endpoints. Initial/full-world adoption,
reload and authoritative timeline replacement retain their presentation reset.

For a supplied stamp, `at = tick - 1 + alpha` must still have a nonnegative
tick and a finite fraction in [0,1], be at least `view_start`, at most the
connection's server-advertised `view_limit` and execution tick, and have an
execution age no greater than the unchanged game's positive `maxRewind`.
Only server WELCOME/SERVER sending advances `view_limit`; a client claim or
acknowledgement cannot raise it. Authentication proves the connection, not what
a modified client displayed or whether every advertised datagram arrived.
Rejected stamps become invalid zero without losing ordinary input. Missing
inputs can still repeat a held stamp, so Physics.Rewind retains its independent
age/history checks. No API, generated-C contract or wire format changes.

`tests/test_session_view_time.c` exercises consecutive visibility repairs over
20 ms one-way latency, 5 ms jitter and 3% loss, with 120 Hz updates, 20 Hz
presentation and a two-thirds-second render pause. It checks each newly
received input against the actual preceding frame, correction adoption before
redraw, reveal/conceal, expired frames and continued prediction. Native/WASI
agree: 74 projections, 69 skipped transfer intervals, 374 accepted timestamps
(241 during continuous repairs), 91 expired stamps, zero lost actual stamps
and zero resyncs. Separately linking the original `866f7be77` session against
this regression fails (147 lost stamps and valid bounded claims rejected).
The raw-peer case covers tick-zero/nonzero admission, live-cookie reconnect,
pre-admission, future, negative, malformed/nonfinite fractions, expired claims
and an unadvertised stamp before its execution tick; ordinary input survives
every rejected timestamp.

Validation: warnings-as-errors native/WASI builds; both pass the session group
(14 cases), existing visibility group (seven cases), generated `view_time`,
and Box2D/Box3D actual-frame rewind-network fixtures. The visibility name filter
also runs the new consecutive-repair case. Node is 24.22.1. All commands use
TIDE_HIDDEN; no window, GPU, browser or CI run was started.

Independent read-only lifecycle/security reviews approved the minimal fix and
the final regression diff without blockers. Their scope is source review, not
a second test run. The spec's older Confirmed view time paragraph still calls
the lower bound the "connection snapshot"; this note records the corrected
connection-attempt admission contract. The owner-limited implementation task
changes only session internals, focused CPU tests and this note.

The unchanged strict prototype repro
`breach_culling_real_lossy_clients_receive_conceal_reveal_and_keep_predicting`
still fails identically on native/WASI after 1,800 ticks at 20 ms/way, 5 ms
jitter and 3% loss. All 3,501 supplied valid claims have zero rejections at
ages <=250 ms; 1,966 older claims are still rejected as required. The clients
match 212/196 canonical worlds with zero resyncs, but have only 12/11 hidden
updates and 10/10 hidden movement updates, below the unchanged test's bounds.
This fix closes the false-rejection/stamp-reset bugs; it does not establish
that real projection corrections keep every human presentation within the
rewind window. Correction delivery/confirmation lag remains an integration
gap (the earlier projection lag measurements below remain relevant). No game
test, culling policy, timestamp or maxRewind bound was weakened here. The repro
was built only in this checkout's CPU-only debug/web-debug slots, with BREACH
as their sole external directory; the prototype agent owns its final tests.

## A session's recorder and rewind (2026-10-26)

For Unity's time travel (notes/unity.md, Time travel), host-side session calls; generated code calls nothing new.

- `tide_session_record(s, recorder)`: `begin(world, tick)` then `tick(tick, record, size, world)` for the match this
  machine runs, from the server's journal (`tide_server_desc.journal`, set on the session's server by
  `record_server`). `begin` comes as it's set on a running match, at `start_server` (start, start_from), at
  `take_over`, and after `tide_session_migrate` (another layout's world). Sharded matches aren't recorded. NULL
  stops it (the server's journal is cleared).
- `tide_replay_check(game, world, record, size, jobs, &expected, &got)`: `tide_replay`'s run of a record, saying
  `TIDE_REPLAY_SAME`, `_DIFFERENT` (both hashes) or `_UNREADABLE`; `tide_replay` is a wrapper of it, as it was.
- `tide_replay_heard(game, world, record, size, jobs, heard, &expected, &got)` (2026-11-19; notes/unity.md, Time
  travel's gaps): the same run with an event log, or NULL for `tide_replay_check`'s. The record's tick is run as
  `tick_run` runs the server's own for its machine's local handlers: `tick_client_events` with per-player journals
  it drops and `heard` as its `all` in a game with projections, else `tick_events`, else `tick`; a scene's
  preparation records into it too. The world and its hash are the same with or without. A debugger's frames hear a
  tick made again through it (`tide_timeline_heard`).
- `tide_session_recorder.verified(user, tick, world, heard)` (2026-11-19, optional; notes/unity.md, Time travel's
  gaps, 2): a match another machine runs, followed. `record_client`, after the client's update in
  `tide_session_update`, tells each tick confirmed since the last told (`tide_session.followed`), reading
  `world_at(c, tick)` for the ticks from `max(first_tick, ahead + 2 - ring)` to `verified`, which the client's ring
  still holds as confirmed, and copying from the session's log (`s->heard`, where the host takes it) the records
  of the tick before with `as == TIDE_HEARD_VERIFIED`, as 0. It starts over at the latest tick (one call) when the
  client changed, a world came whole (`first_tick`), `verified` went back, or the next tick is out of the ring;
  `tide_session_record` starts it over too. Nothing on a server's machine. No client state
  changes and nothing is sent: it only reads. Since the same day, later (notes/unity.md, Time travel's gaps, 5):
  a sharded match is followed too, through its home moving between shards (kept by the shard client, not a client),
  with `tide_session_recorder.beside` after each `verified` (`record_beside`: the other followed shards' clients'
  worlds at that tick or their latest before it); and a client of a game with no projections isn't followed but
  **journaled**: `journal_client` (before each update) sets `tide_client.journal`, and the client, as it confirms a
  tick from its frame (before `drop_frame`), hands over a record made as `journal_tick` makes one (tick, the frame's
  size and bytes, the count of downloads it names and each one's number, size and bytes) with `world_at(c, tick)`
  and `world_at(c, tick + 1)`; `client_journaled` tells the recorder `begin` and `tick` as a server's machine's is
  told. It relies on a client's frame of such a game being the bytes the server journals (it is where no private
  journal is kept: `journal_tick`'s `private` is a projection game's), which `travel_records_another_machines_match`
  pins by replaying a client's recording against the server's hashes. `tide_session_remote_players` counts a
  sharded match's root's connections but this machine's.
- `tide_session_remote_players(s)`: connections on the server here that aren't this machine's (used, connected or
  joining).
- `tide_session_rewind(s, world)`: the server's world becomes `world`, then the new optional
  `tide_game.keep_commands(to, from)` (generated for every game with commands: a copy of `tide_command_state`)
  puts the live world's command bookkeeping back over it, so no command is lost or applied twice whatever the clients
  have pending; `ended` is read again, presentation corrections reset, and this machine's player takes the world
  (`take_server_world`). The server's tick count goes on. Clients of other machines aren't told: their next frame's
  hash isn't their world's, and they repair (projection games) or resync as on any divergence, from the server's
  history of frames before the rewind, which still describe the ticks their verified worlds are at. No wire change,
  so `PROTOCOL` stays 19. The generated header's Session ABI marker says `rewind (1)` for the `tide_game` member, so
  hosts and games rebuild together, and every game's layout hash moved once.

Tests: `tide_platform_travel`'s timeline cases (three sessions over a lossy loopback: recording, replay bit for bit,
a changed build's first differing tick, a rewind with two clients connected and a command in flight).

## Task handles follow authoritative visibility (2026-10-25)

The generated match task registry is excluded from recipient singleton codecs
and their simulation schema, alongside existing private continuations.
Cached result tables remain private too. Ordinary public Task fields carry
only identity; in a projection whose operation is absent they are stale,
with no remote-status/result lookup. Retained match starts on a projected
client return the default handle. Games publish permitted ordinary task
progress/results through state, as before.

task_visibility checks private cached/input markers are absent, canonical
projection hashes agree before/after repair, projected status is stale, and
client starts cannot alias the server's operation. No session wire protocol
or runtime callback changed. The owner deferred distributed match task
ownership: sharded games reject first-class match handle use for now, while
direct/discarded match async and local handles/workers remain supported.
Validation: language.md.

## Session cookie proof, protocol 19 (2026-10-24)

The owner approved the wire-protocol change after the packet audit below.
Every connected message in either direction now has the player's 64-bit cookie
and the current 32-bit connection attempt immediately after its four-byte
header. This includes inputs/acknowledgements, commands/uploads, frames/world
chunks/downloads, handover, local data/subscriptions and disconnects. Receivers
validate both before interpreting the payload or refreshing liveness. Unknown
message kinds do not refresh liveness. Commands/uploads retain their previous
attempt check through this common prefix. Protocol 19 prevents older peers
from silently using the former unauthenticated layouts.

HELLO cannot replace an existing connection at the same source address without
its cookie or the same nonzero identity from the trusted admission callback.
This check precedes replacement and game-mismatch disconnection. Genuine
cookie reconnects keep their slot and event behavior. A fresh client which
lost its cookie at an occupied address waits for the old connection's timeout;
the address alone grants no takeover. An initial HELLO still only receives a
WELCOME; its first valid cookie-bearing CLIENT proves receipt before joining.
WELCOME already echoes the attempt and supplies the cookie; after acceptance,
a later WELCOME cannot change that cookie. REFUSE now echoes the attempt and
is accepted only before a welcome. BYE requires its complete valid payload;
an ended server's initial HELLO reply echoes the offered cookie and attempt.
A kick notice received before WELCOME checks the cookie offered in HELLO,
so a player who missed a kick still hears it on reconnect.

Cookies are generated by hashing a private 32-byte key with a fixed-length
monotonic counter, rather than exposing reversible splitmix64 output. A player
learning their own cookie cannot invert it to calculate later cookies. The
existing host entropy callback seeds the key independently of match state,
including for unseeded games; all-zero entropy fails server creation. A
takeover now forwards that callback too. Custom network hosts must provide
system entropy. With no callback, test/benchmark hosts retain a private
process/time fallback without a cryptographic entropy guarantee; the standard
host's deterministic headless/explicit-seed runs still select this fallback.
No key/counter enters a world, hash, snapshot, journal or recovery metadata.
Existing reconnect cookies/digests survive migration/recovery as before.

These are bearer credentials against blind source-address spoofing, not a MAC
or raw-UDP encryption. They do not authenticate an initial server against an
on-path attacker or protect a cookie observed on plaintext UDP. WebRTC's
encrypted transport remains the path protection; a malicious authenticated
player still requires game-specific authoritative command/input validation.
Normal CLIENT/SERVER/chunk/BYE traffic gains 12 bytes per packet, commands and
uploads gain eight beyond their previous attempt, and local data gains four
beyond its previous cookie. Packet capacities still bound their payloads.

The eleven raw-peer/session packet cases cover wrong cookies, wrong/stale attempts,
truncated proof prefixes, spoofed input/disconnect/server-clock packets,
malformed disconnects/refusals, a live-address HELLO takeover, successful
reconnect and valid disconnect, timeout despite forged heartbeats, failed
entropy, plus the earlier upload/parser regressions. The voice fixture captures
its actual attempt and additionally forges it with the right cookie. Command
and damaged-snapshot wire fixtures follow protocol 19. All eleven packet cases
pass with session.c instrumented by Windows AddressSanitizer.

The local-events/plain traffic pins both move from 29,124/11,108 server/client
bytes to 33,156/15,332, exactly 12 bytes on each of their 336/352 connected
packets. World/delta pins stay unchanged. The ten-second bandwidth fixture
keeps its previous payload ceilings and separately counts the proof bytes:
native/WASI totals are 141,092/25,955/25,957 on the server/two clients, including
14,568/6,000/6,000 proof bytes.

Validation: both complete debug builds pass with warnings as errors. All 2,621
enabled native CTests pass (123.34 s), and all 2,479 enabled web CTests pass
(370.44 s), including Vulkan/OpenGL, WebGL/D3D11/WebGPU, browser rooms and
migration, voice, dedicated admission, recovery, commands/uploads and the
unchanged simulation/world/delta pins. Both retain the existing disabled
MicroDuck right-turn limitation. These engine-only configurations explicitly
omit TIDE_EXTERNAL_DIRS; no prototype/service changes or validation were
required. Node is 24.22.1 and all runs retain TIDE_HIDDEN/muted browser flags.
The eleven packet cases also pass with the final session.c under Windows ASan.
Other operating systems, Android, MinGW and the full sanitizer suite remain
CI coverage. No workflow was manually dispatched or deployment performed.

Integration: origin/dev's scene startup, prefab overrides and editor/CLI work
merged cleanly in `50bed96c`, with no session/runtime overlap. Since codegen is
a dependency, rebuilt the affected native/WASI networking, commands, recovery,
handover, players/ceiling, visibility, voice and prefab/startup targets. All 20
selected CTests pass on each platform, including the incoming scene-startup
script and the complete engine test executable. No full-suite repeat was
needed for this dependency-only integration; the complete pre-merge results
above remain the broad validation.

Push retry integration: fetched the shard recovery/admission changes through
`12f3cdac` after GitHub DNS became available through a command-scoped resolver
override. The C merge retains both the cookie/attempt gate and the admitted-slot
reuse exclusion/release; both prepend records are retained in the notes.
Rebuilt the affected native/WASI engine, networking, command, visibility,
voice, shard and Earth targets. All 25 selected CTests pass on each platform
(19.56/21.65 s), including the 12 shard-player cases, 15 Earth cases, all three
Earth benchmark pins and the packet regressions. The overlap is limited to
admission/reconnect and shard recovery, so no full-suite repeat was required.

## Shard recovery reserves player slots (2026-10-24)

The server's anonymous-slot reuse scan now excludes `admitted` players as
well as connected players and migration reservations. A restored full shard
previously let an anonymous peer replace a reserved cookie before its owner
returned. Global departures release admissions on every shard through
`tide_server_unadmit`; an unused slot clears its old identity so a new cookie
can be admitted there, while live connections and migration reservations keep
theirs. Invalid/out-of-game IDs do nothing. The shard instruction uses cookie
zero for release; this reservation fix left game packets unchanged (then
protocol 18). The combined build uses protocol 19's cookie proof above.

Neighbour retries preserve the player's cookie; terminal home kicks/refusals
are no longer hidden by `outlive_home`. The checkpoint/journal lifecycle fix,
its new internal checkpoint version and tests are in `notes/sharding.md`,
Border and recovery audit. Cookies remain private host/checkpoint metadata;
the generated membership bitset contains only public player-slot presence.

## Hostile client packet validation (2026-10-24)

`on_upload` now binds every chunk of an existing transfer number to its
original command size and sequence before indexing the retained chunk map or
copying payload bytes. Previously a peer could start a small upload, reuse its
number with a larger size/index and access beyond both original allocations.
A shorter size or changed sequence could also mix incompatible chunks. Rejected
chunks leave the original transfer usable; a new number still replaces it.

`on_client` now validates every input's span and codec, the exact packet end,
known flags, acknowledgement against the authoritative tick and non-wrapping
input tick arithmetic before greeting, acknowledging transfers or storing any
input. Canonical whole inputs are staged in one reused server buffer, avoiding
per-packet allocation after its capacity is established. Inputs beyond the
server's bounded future window cannot change arrival-lead bookkeeping. HELLO
also requires an exact packet end and known flags. Valid packet bytes, protocol
18, generated-C APIs, snapshots and deterministic results are unchanged.

`tests/test_session_packets.c` drives the public server with raw loopback
packets: changed upload size/sequence, out-of-allocation chunk indices,
truncated/empty/invalid final inputs, trailing bytes, future acknowledgements
and wrapping input ticks, followed by valid traffic and transfer replacement.
A far-future input followed by an ordinary input must still report subsequent
missing inputs as late, rather than retaining the first input's positive wait.
The original parser fails the atomic-input regression; a separately linked
Windows AddressSanitizer build reproduces its heap-buffer-overflow in
`on_upload`. The fixed session passes all five packet tests under ASan and
on native/WASI, including malformed HELLO retention and valid input delivery.

Validation: warning-free native/web debug builds; all 2,621 enabled native
and 2,479 enabled web engine CTests pass, including Vulkan/OpenGL,
WebGL/D3D11/WebGPU, rooms, migration and deterministic pins. Both retain the
existing disabled MicroDuck right-turn test. The final engine test executable,
with all five added packet cases, also passes as a whole on native/WASI.
Node is 24.22.1; runs retain TIDE_HIDDEN and muted browser flags.

The environment also configured tide-prototypes. Its Cracked direct-world
3,000-tick hash pin fails on both native/WASI with the same actual
`bf53c0610c47764e` versus expected `0a216d532fa6445c`; that test never calls
the changed session code. No prototype source or pin was changed. After all
engine checks finished, the remaining optional prototype browser playtests
were stopped (Inside Out's active WebGL playtest and the queued prototype
browser checks); they are not coverage for this task. Other operating systems,
Android, MinGW and the full sanitizer suite remain CI coverage; only the
targeted Windows session/packet ASan probe ran here. No CI was dispatched.

Audit boundary at the end of the initial parsing pass: command payload validation and connection-derived sender,
chunk/download bounds, recipient acknowledgement ownership and view-time
validation were checked. This does not establish authentication of the source
address: protocol 18 CLIENT and BYE still trust it, and a new HELLO can replace
an existing connection at that address. A session-cookie wire-protocol change
was raised with the owner separately and is not implemented by this parsing
fix. The approved protocol-19 follow-up above closes that session boundary.
Native/WebRTC transport security and game-specific command authorization
remain separate boundaries; this is not a complete transport/cryptography audit.

## A client sends at most once a tick unless it has news (2026-10-23)

`update_client` used to send a packet every update, so a client updating faster than the tick rate (a 144 Hz browser, a native client without vsync) sent more than it needed, and more than the hosting router allows an admission (100 a second). `send_client` now builds the packet every update and sends it when it heard from the server since it last sent (`heard_since_sent`, set by any packet taken in: so acknowledgements and pings go out as promptly as before), when anything in it after its two times differs from the last one sent (kept after `packet`, `client_sent_size`: a tick verified, a frame or chunk had, a repair asked for, a new input), or when 0.99 of a tick has passed since it last sent (`client_sent`; the slack keeps a host updating once a tick sending every update, which float rounding of the clock otherwise skipped now and then). Nothing on the wire changed, so `PROTOCOL` stays 18.

A first version sent on hearing, a new sample or a tick passing, and nothing else: a client's verification and repair requests made in an update with nothing heard waited for the tick, the hidden-effects test's worst verified lag (20% loss, 120 updates a second at 60 ticks) went from 104 ticks to 152 against its bound of 120, and a client updating once a tick skipped sends (the decode test's pinned hash moved). With the packet compared it's 107, and the decode pin is as it was. Loopback loss is drawn per packet, so fewer packets change which ones are lost: `rewind_network_test`'s seed moved from 82 to 83 with the first version (30 seeds compared, no change in the results' spread).

## Shard messages by download in games without commands (2026-10-23)

A sharded game's clients now report the progress of a download (a published command or shard message) in `MSG_CLIENT` whether or not the game has commands, and servers read it, as they did only for games with commands. A big shard message in a game without commands was never acknowledged, which held it and every later one at the server for good. `PROTOCOL` stays 18: only sharded games without commands send anything new, and their builds must match anyway. Server additions: `tide_server_injected_message`, `tide_server_drop_injected`. Session additions for sharded matches: `tide_session_can_reload`, `tide_session_save_shards`, `tide_session_start_shards_from`, `tide_session_shards`. Details: `notes/sharding.md`.

## A server's journal (2026-10-23)

Visibility recovery correction (2026-10-24): private commands and injected
shard messages were applied but omitted from the common frame, which the
journal had copied. The hosted Earth's saved journal reproduces a mismatch
at tick 4291, after 717 successfully replayed records from its checkpoint.
With projections and a journal enabled, dispatch now copies these effects
into tick-local, server-only buffers. `journal_tick` appends their entries to
a separate full replay frame; large payloads use record-local download IDs
and bytes in the existing journal format. Recipient frames, downloads, wire
protocol, tick order and hashes are unchanged. Games without a journal make
no payload copies. No new public/generated runtime API is introduced.

Every visibility-session case now replays every server record while checking
the original privacy, loss, correction and command assertions, including two
100 KB private uploads. A separate test covers small and 5 KB private shard
messages. Earth's real four-region test replays each shard throughout joins,
travel and leaves and compares final full-world hashes. Previously omitted
effects cannot be reconstructed from the old records; the fix protects new
journals rather than accepting an invalid historical hash.

Validation: full Windows debug suites pass 2,621 enabled native and 2,479 web
tests, including Vulkan/OpenGL, WebGL/D3D11/WebGPU and existing journal/recovery
coverage. The existing MicroDuck right-turn test stays disabled. Final Earth
presentation edits additionally pass its four CTests on native/web debug.
Other operating systems, Android, MinGW and sanitizers remain CI coverage;
no CI was manually dispatched.

`tide_server_desc.journal` gets, after each tick, a record of what went into
it: the tick's frame as players get it, then the bytes of every big command or
shard message the frame names by download. `tide_replay` runs that tick again
on the world from before it, as a client applies a frame, and checks the hash.
Nothing on the wire changes; a server without a journal does nothing new. The
orchestrator journals every shard and recovers from a checkpoint and records
(notes/sharding.md, The journal).

## Session protocol 18: shard messages (2026-10-23)

A sharded game's servers take messages from other shards
(`tide_server_inject`), applied before the tick after its commands and written
in the frame (`FRAME_SHARD` 128) inline or, over `TIDE_COMMAND_BYTES`,
published and named by download as a big command is. Clients apply them from
the frame at the same point; such a frame is never as predicted. A closed
server takes players `tide_server_admit` admitted with their cookie, as those
players. `PROTOCOL` went to 18 (`tests/test_commands.c` and the voice test
write it by hand); a game that isn't sharded sends nothing new. A sharded
session can't migrate. Details, limits and tests: notes/sharding.md.

## Unreliable local data for optional voice (2026-10-22)

Sessions now expose the C-only listen/send/data-generation API described in
`notes/voice-package.md`. Additive protocol-17 message kinds 12/13 carry
leased subscriptions and 1..512-byte local payloads. The server validates an
existing greeted sender and its cookie, applies a 60-packet/s four-burst limit,
and forwards only to other subscribed connections with server-supplied sender
identity/digest and the recipient's cookie. No payload is resent, run in a
tick, saved, rolled back or hashed. Unused sessions emit no new messages.
Existing protocol messages, world bytes and deterministic pins are unchanged.
Servers relay without Opus or a voice provider. Hostile-packet, optional-cost,
three-session loss/reconnect and actual three-process audio tests are recorded
with the package, alongside complete native/web validation.

## Session protocol 17: pages hashed by stripes (2026-10-20)

`tide_hash_wide`, every page's hash and so every world hash, is a new function
(notes/ecs.md, A page's hash by stripes). Peers already refuse another
`PROTOCOL` in HELLO (`engine/src/session.c`), so it goes from 16 to 17: two
builds that hash pages differently would otherwise connect and disagree about
every tick, and a joiner's page-hash negotiation would want every page. Nothing
else on the wire changed; `tests/test_commands.c` writes 17 by hand. Page
hashes reach the wire in each tick's world hash, in a world delta's (the hash
its receiver must come to) and in a joiner's list of page hashes
(`*_hash_pages`), all covered by the protocol number. Raw packed worlds hold
none (their bytes are unchanged, as the local fields/events fixtures pin), so
a world unpacked by either build is hashed by that build's function. Pinned
deltas moved in their bytes, not in size.

## Double Position presentation errors (2026-10-20)

Position-tagged double scalars/vectors now use the existing private correction
path in their declared precision. Frame fractions/decay timing remain shared
floats, widened for binary64 arithmetic. No session protocol, prediction,
simulation state, packing or runtime API changes. The extended view_corrections
E2E covers interpolation, correction/decay, cache privacy, snaps, identity and
confirmed removal; final validation is in notes/language.md.

## Double matrix columns (2026-10-20)

Generated input/command codecs walk double matrix columns as double vectors,
reusing the binary64 low-word-first representation and changed-bit deltas.
Nonfinite components repair to declared defaults before game input. Command,
raw/delta input, recipient state and replay E2E coverage now includes matrices;
no transport/session protocol or runtime networking function changes.
Validation: notes/language.md.

## Double input and command fields (2026-10-20)

Generated double scalar/vector payloads retain all 64 bits per component as two
ordered 32-bit words, low first. An input delta sends a changed flag followed by
those bits only when changed; +0/-0 and payload bits remain distinguishable.
Readers never put two stateful bit reads in one C operand expression. Repair and
bounds handle double leaves before input Sanitize/command dispatch. Existing
schema identity distinguishes the new types; the session protocol is unchanged.
The double E2E covers extreme bit patterns, whole/delta round trips, nonfinite
repair and actual command submission/dispatch with truncation rejection.
Full contracts and validation: notes/language.md.

## Shared player/connection ceiling (2026-10-20)

The owner approved 65,536 as the shared supported ceiling. TIDE_PLAYERS_MOST
in tide/player.h now governs maxPlayers, native room peers, dedicated-host
max_connections and the service CLI's validation. Defaults remain 16 players
per match and 256 connections per dedicated host; those settings still bound
different resources. This is a supported representation ceiling, not a measured
65,536-player throughput claim.

Session protocol 16 encodes welcome player indices, join/leave player indices,
handover part indices and tick-piece indices as varints. Pending server events
keep uint32 player indices. The generated Session ABI marker changes, so hosts
and games rebuild together. Small indices retain their previous wire sizes.
Tick sending retains its eight-packet update allowance and now resumes a large
frame at its next unsent piece, rather than repeatedly starting at piece zero.
Projected frames above 256 KB still use the existing chunked correction path as
an internal reassembly bound; that threshold no longer reflects index width.

Player sets remain bounded value bitsets (65,600 bits, including the server's
input slot), with a maintained nonzero-word prefix so small sets do not scan
the full ceiling. Initialize them with {0} and mutate through tide_players_*
to preserve that prefix. Wire masks and frame/handover capacity bounds use the
game's configured count. Per-player connections, inputs, Audience words and
client handover digests use the configured count, not the global ceiling.
The fixed bitsets are about 8 KiB of metadata each; they are not dynamically
sized. Frame-capacity arithmetic widens before allocation; unsupported oversized
handwritten game tables are refused. Kick validates against the game's own
player count before indexing its connections.

tide_server_recovery now takes caller-provided digest/identity output arrays,
each tide_game_players(game) entries; its getter preserves those pointers and
fills them. Managed checkpoint envelope 2 stores game-sized arrays without
pointers, and save/restore allocate the checkpoint off the stack. Client
handover metadata owns game-sized digest storage, released on replacement and
destruction. Generated raw and network serialization headers also use temporary
heap storage, so large configured input arrays do not exhaust the 1 MiB stack.
Serialization contents and deterministic world hashes are unchanged for an
unchanged game schema.

The players fixture now runs 270 real loopback sessions with maxPlayers 300,
including inputs, identity-preserving host migration, and all ten Audience
words through actual recipient projection. players_ceiling compiles maxPlayers
65536, exercises the last input/server slots and last Audience word, joins the
last slot using a restored match with preceding slots reserved, verifies ticks
and input delivery, round-trips its delta, reconnects with the same cookie, and
checks leave delivery and rejection above the ceiling. It does not instantiate
65,536 clients or benchmark that workload.

Validation: warning-free Windows native/web debug builds; all 2,406 native
and 2,353 web CTests are covered, including native Vulkan/OpenGL, WebGL,
WebGPU, browser RTC/migration/threading, compiler/LSP and deterministic pins.
The initial sole failure in each suite was the handwritten command fixture's
protocol-15 packet; it now sends 16. After that fix and the final oversized-frame
migration guards, the complete engine and eight affected session fixtures pass
again on both native and WASI. The docs production build passes. A separately
linked checkout-built WASM reactor with the ceiling fixture saves and restores
an identical 3,170,841-byte managed checkpoint, and rejects an invalid player-set
prefix. Its header also compiles under strict C17 with warnings as errors.
Vulkan tests ran without a configured validation layer. Android, Linux/macOS,
MinGW and sanitizer coverage remain CI work; no CI was dispatched.

Integration: origin/dev advanced with disjoint math/Screen metadata and private
aggregate-borrowing codegen. The two notes-only conflicts retain both records;
code merged without conflicts. Both complete debug builds and the docs build
pass after integration. The 21 affected native and 22 web checks pass, covering
the engine/session/player fixtures, incoming float-bit/aggregate/Screen cases,
Screen diagnostics, shader golden, native Vulkan/OpenGL and WebGL/WebGPU float
checks, editor harness and native/browser rooms. The exact-ceiling managed
checkpoint probe passes again against the merged runtime. No full-suite repeat
was needed for the disjoint compiler hunks.

Scene audience unification (2026-10-15): the compiler removed separate scene
membership storage/APIs and uses the root Visibility group for the enclosing
scene limit. Recipient projection, lifecycle journals, authoritative correction
and server-only group policy keep their existing runtime/session contracts.
The generated scene layouts/build identities change, without a tide_game ABI
or protocol change. Full-world serialization pins include the intentional scene
layout reduction; native and WASI agree on logical hashes/whole and delta bytes.
The local-events fixture's server traffic falls from 29,132 to 29,124 bytes;
client traffic remains 11,108. Language/tests/validation: notes/language.md.

## Per-recipient simulation and authoritative repair (2026-10-15)

Entity/component visibility now uses optional `tide_game.client_state`,
`load_client_state`, `client_hash` and `filter_client_inputs` callbacks. Generated
games using visibility supply them; ordinary games keep the existing shared
input/frame, physical-page hash and snapshot paths. Hosts and generated games
must rebuild together for the extended `tide_game` ABI. Integrated session
protocol 15 carries `tide_game.simulation` for compatibility, independently of
the layout/hot-reload `hash`; handwritten games with a zero Simulation ID use
the hash fallback. Visibility requires no additional protocol-number bump.

The server retains its complete authoritative world and hashes it every tick.
Each recipient also gets a hash of its permitted projection every tick, together
with permitted changed inputs. Projection bytes encode values, component
presence and stable entity handles, rather than allocator addresses/free lists.
Deep containers are reconstructed in a separate client heap. Normal match
singletons remain public; there is no singleton or field visibility. Private
group/component policy metadata, waiting task continuations and opaque Resident
state are not transmitted. Tasks and Residents remain authoritative server work;
their permitted ordinary state outcomes reconcile through the same repair path.
The generated codec/storage contracts and limitations are in `language.md` and
`ecs.md`.

Input filtering is deliberately conservative. The sender keeps its own input.
Another slot is withheld as a whole when any entity/component it controls is
hidden from that recipient; a server slot also follows unowned entities. This
can withhold movement needed by an otherwise visible entity and increase state
traffic. Completed ticks copy current inputs into previous inputs, so transmitted
endpoints do not disclose an earlier hidden input through the previous slot.
The bulk input callback filters all player/server slots with one entity-policy
pass; frame generation materializes the two endpoint input arrays once and
uses the existing single-slot callback only as an optional handwritten fallback.
Commands, including large uploads, execute only on the server in this mode;
other players never receive their payloads. Their permitted state results and
the sender's sequence acknowledgements arrive in authoritative projections.

An initial/recovery projection uses the existing numbered chunk transfer with
its retransmission and chunk acknowledgements. Clients never reconstruct a
private starting world through `pure_start`. A mismatched tick requests an exact
historical repair instead of an exceptional whole-world resync. Its canonical
bytes use `tide_delta`'s existing runs/XOR compression in 16 KiB regions against
the client's acknowledged verified tick, with the baseline tick explicit in the
transfer. The receiver applies the delta to that verified baseline, checks its
canonical hash, loads a separate projected world and replays later predictions.
It never patches an unverified prediction as if that were the shared baseline.

Repeated divergence cannot cost one round trip per simulated tick. Two distinct
repair requests within a second enable a bounded one-second correction stream.
Each acknowledged transfer is followed by the latest authoritative endpoint,
coalescing intervening ticks against the new verified baseline. Transfers already
in flight stay stable until acknowledged. Periodic input/hash probes return to
ordinary prediction when hidden effects stop. Coalesced checkpoints reset their
confirmed presentation interval rather than presenting missing intermediate
snapshots. They carry the ordered permitted lifecycle journals for their interval;
guessed ordinary events in replaced intermediate ticks are not invented as
verified events. Ordinary border corrections do not increment `resyncs`.

The server keeps one shared COW authoritative history plus each connection's
last acknowledged endpoint. Recipient-specific frame histories contain filtered
inputs/hashes and lifecycle journals. `tick_client_events` runs the server tick
once and records genuine Spawned/Destroyed at their actual event-time state,
with recipient-specific listener masks, scrubbed unread payload slots and
permitted query rows. `check_client_events` validates typed sizes, kinds, masks
and query records before received bytes can reach generated handler casts.
Private `TIDE_HEARD_DESTROYED_CAUSE` records distinguish actual permitted
destruction from concealment even when no Destroyed listener exists. Causes are
recorded at destruction time, so hiding then destroying an entity does not
disclose its death. They are never dispatched to game handlers. If old cause
history is unavailable during recovery, concealment remains the conservative
notification. Full recovery snapshots carry retained causes without replaying
missed lifecycle effects; `Forgotten` remains the resource-release hook when
neither presented endpoint retains the entity's attached local fields.
Large frame/journal batches use chunked repair rather than
overflowing the frame's piece index. Generated local delivery reconciles
Shown/Hidden once per presented frame against its retained predicted/verified
COW endpoints. Tick-level availability records do not directly drive resources:
same-tick conceal/reveal and canceled predicted concealment must reflect the
final frame endpoint. Private destruction causes reach only this internal local
delivery path, and guessed causes are removed before authoritative confirmation.
The authoritative journal currently routes genuine lifecycle events only;
arbitrary game events have no recipient audience API. Private commands can
therefore produce corrected permitted state, but their server-only custom local
events are not delivered to remote presentation handlers.

Projection games currently disable host handover, including handover metadata,
automatic migration and defensive partial-world takeover. A client projection
cannot become the full authoritative server. Source-code hot reload remains a
separate supported `tide_session_migrate` path; the hosting player's replacement
world is projected before adoption.

Hash/repair partitioning is internal and provisional: one canonical projection
hash goes each tick. Protocol 24 adds on-demand canonical 16 KiB block-hash/need
negotiation for large repairs; no hierarchical partition tree or inferred
dependency graph is implemented. Steady corrections put a client's verified
world up to 1.5 s behind at 133 ms round trips (`notes/benchmarks.md`, Earth
under load).

Every player's frame after a tick is built from what's alike for them worked
out once (2026-10-24; they were 80 to 90% of a server's work from 64 players,
and grew with the square of the players):
- **Projection hashes** (`tide_game.client_hashes`, every greeted player's at
  once; `client_hash` is the same for one). The projection hash is no longer
  the hash of `client_state`'s bytes: it's the singletons' hash (the command
  state each recipient's own), plus a sum of each permitted entity's hash (its
  archetype, handle, snaps, scene and permitted components, by the value codec)
  and of each input slot's, as permitted. An entity's audiences and its hash
  with all it has are worked out once a tick; a recipient denied some of its
  components gets another, computed once and kept for the next recipient with
  the same mask. The sum doesn't depend on row order, which a client's tables,
  missing hidden rows, needn't share with the server's. Games with transform
  hierarchies kept hashing `client_state`'s bytes, as each recipient's cut links
  and anchors differ, until 2026-11-15: those go as one section now (notes/voxels.md). A transfer's check is now the hash of the bytes the
  loaded world encodes to again (it compared `client_hash`, which was the same
  hash). `recipient logical state schema 2` is in the simulation identity
  (with `singleton tree hashes` after it since 2026-11-04, above).
- **Inputs** (`tide_game.client_input_masks`): which slots each recipient may
  have, before and after the tick, from each slot's owner's entities' audiences
  intersected once; the session reads every slot once and packs each changed
  slot's delta once (`frame_inputs`), packing again only for a recipient whose
  mask differs before and after. `filter_client_inputs` stays for the other
  paths (endpoints, repairs).
`earth_a_machine_hashes_its_world_as_the_server_does` checks a loaded
projection hashes as the server's, together and one by one, for players who
have each other's Walker and one who has neither's, and that the masks are
the filter's. The Session ABI marker says `client hashes (2)`.
Canonical insertion/removal can shift later byte regions and reduce delta reuse.
Physical-page sharing still makes authoritative history snapshots cheap, but
retained history and recipient journals add memory. These are optimization gaps,
not permission boundaries or reasons to send private bytes.

`tests/test_visibility_session.c` isolates the protocol with handwritten callback
games: input-driven prediction, hidden health effects, reveal/conceal, loss and
reordering, private small/two 100 KB command uploads, recovery after frame history,
final event-time destruction payloads, continuous hidden effects and rejected
partial-world host migration. Random unchanged regions pin baseline delta reuse;
the continuous case bounds confirmation lag under 160 ms RTT, 60 ms jitter and
20% loss. Fresh targeted native validation passes all six cases. The continuous
case simulates 700 server ticks in 11.67 seconds with hidden consequences on 480
ticks: both clients finish at verified tick 693, maximum observed confirmation
lag is 104 ticks (1.73 seconds), and neither resyncs. Numbered canonical transfers
send 33,835 and 33,551 bytes per client including the initial approximately 33 KiB
projection; this excludes normal frame/input traffic. Generated
visibility/group/projection/lifecycle fixtures exercise the language side. Final
native/web validation is recorded with the integrated work.

## Simulation compatibility and managed recovery (2026-10-15)

Session protocol is now 15: HELLO carries the 32-byte generated Simulation ID
and the receiver checks it independently of layout identity. Handwritten games
with all-zero Simulation ID retain the legacy hash fallback. Generated-header
ABI marker forces hosts to rebuild. Hosting envelope 2 checks simulation before
allocation and returns WRONG_VERSION; standard hosts propagate update guidance.
The intentional two-client local-events wire-byte pin increases by 64 bytes;
world deterministic hashes are unchanged. Tests cover unequal simulation with
equal layout, equal simulation with different layout, and checkpoint reconnect
cookie/identity restoration. Host-only recovery metadata preserves pending player
sets, identities/digests, seed, dt, closed and started state; it does not alter
world snapshots/hashes. Managed exact-release save envelopes and validation are
in managed-hosting.md. The original dedicated-host note below predates these
explicitly requested protocol/ABI changes.

## Dedicated match admission (2026-10-15)

hosting.h multiplexes independent servers over one transport and gates traffic
through create/join authorization before normal session messages. Optional
tide_server_desc.identity binds reconnect cookies to the verified account and
permits that account to resume without a cookie. Metadata is outside world
snapshots/hashes; legacy sessions keep their existing cookie behavior. The
session protocol is unchanged. Different client/server packs use protocol 14's
self-contained state-transfer fallback. Contracts/tests: dedicated-servers.md.

## Per-tick game input prediction (2026-10-13)

Owner-approved Predict()/[DontPredict] compile to optional
`tide_game.predict_inputs(world, keep) -> bool`. Before every predicted client
tick it adjusts remote player/server inputs, excluding the client's own player
and its local server slot. It applies marked defaults, Predict, then ordinary
repair/bounds/Sanitize. Changed guesses set current and previous input together,
so clearing or computing a guess invents no button edges, including nested bools.
The existing let-go callback still enforces the 0.15-second cutoff afterward;
with a custom predictor it reapplies past the cutoff so the method cannot restart
ordinary held movement. Absolute fields retain the custom guess.

input_guessed records the earliest custom-input change separately from let_go.
Confirmation cannot reuse its predicted hash: authoritative replay is required
even for a frame whose held input has no delta. Partial replay preserves an
earlier unconfirmed marker; completed or replaced timelines clear it. Snapshot
load, catch-up, migration and adopting the local server world reset both markers.
No marker is match state. Server ticks and confirmed replay never call Predict.
No bytes, world layouts, hash algorithms or wire protocol change. The tide_game
struct grows an optional callback, and its generated header ABI marker requires
hosts and games to rebuild together.

predict E2E uses two real loopback clients at 250 ms one-way latency and with
120 ms latency/60 ms jitter/20% loss. It checks remote action counts and edges
against confirmed state, immediate own actions, the existing nine-tick movement
limit, server/client final state equality, zero resyncs and no idle replay churn.
The unchanged guess/net/migration tests remain part of full validation.

Validation: full native/web suites, grammar and production docs build pass;
counts and remaining platform coverage are in notes/language.md.

## Asset-aware state transfer, protocol 14 (2026-10-11)

Self-contained restoration currently omits pack-offset affinity along with optional page origins. Such retained payloads may use full dictionary transmission even when identical catalog bytes are installed; explicit shared-base reuse still works. This is an encoding-size gap, not a value/hash or fallback failure.

The production `tide_game.pack_delta` callback now takes `bool assets` before its size output. The existing public generated `tide_world_pack_delta` remains self-contained by default. HELLO advertises the SHA-256 of exact original pack-file bytes (since pack format 4, the digest the build wrote into the header: notes/assets-and-files.md); wants/lacks requests advertise it too, with no added steady-tick bytes. Only equal nonzero digests enable pack references. A failed snapshot decode disables references for that client and uses the existing retry path to obtain self-contained state. Old protocol peers/builds reject through protocol/build identity. Generated headers carry the session ABI marker for all games and the retained-heap marker where applicable; hosts and games rebuild together.

Heap dictionaries carry ID32/count4/element4/users4/hint4 and either full bytes, a portable pack-file offset or UINT32_MAX for explicit XOR-base dictionary reuse, followed by one uint32 descriptor location per lease. The decoder verifies exact ID and shape and checks each registered descriptor; world hashing authenticates lease ID/location/count independently of registry order. Raw saves always include bytes. Detached page-sized list chunks may carry an asset-relative XOR region; the encoder compares actual complete full/patch sizes including 48 bytes of identity/slice metadata and picks patch only when smaller. Dense changes fall back to full. Patch decoding bounds the source slice, proves current immutable content and reconstructs ordinary page bytes; every result must satisfy heap descriptor/lease validation and the world hash. ECS regions retain their existing format. Save pages and small inline edited lists do not use asset patches yet.

Network world headers now contain only fixed-layout singleton values, chain/task order and current/previous inputs, excluding native-sized storage structs. Resident pointers are zeroed on both encode and decode. Page hash/need generation uses that same portable header. Native/WASI `local_events_same/plain` now pin identical whole/delta hashes; raw saves still pin each platform's local storage header. The unchanged 100-character project produces byte-identical known-pack/full packets on native/WASI, and both builds decode the other's packets, including full decode after pack unload. `asset_sharing` joins over loopback with latency/jitter/loss, deliberately rejects its first reference snapshot and verifies successful self-contained retry without resync. Timing/end-to-end join latency remain unmeasured. Structural evidence is in `bench/results/asset-production/`.

Build notes, moved here from `AGENTS.md` as they were: what's built, what code relies on, the tests and the measurements. `AGENTS.md` has the rules every session needs and the index of these files. The headings below are that file's sections as they were, and "see <Section>" names one of them: the index says which files each is in. What a note calls "below" or "above" may be in another of these files now: search `notes/` for its name.

## Networking

- Snapshot welcomes validate that their chunk count matches their byte count before changing client state. Each chunk must carry exactly its remaining portion (up to `CHUNK` bytes), so truncated chunks remain missing and are resent rather than being acknowledged and unpacked with unwritten bytes. Chunk counts use division and remainder without overflowing at the uint32 limit. `compiler/tests/e2e/net_test.c` corrupts real loopback transfers: zero-length chunks never reach unpacking, then normal retransmission joins without a resync; an inconsistent welcome followed by its valid copy still joins. The wire format and runtime contract are unchanged.

- **Known input-size hole, reproduced 2026-10-09:** `input Controls { float[400] payload; }` compiles with `max_input_bytes` 1700, but an input larger than the 1200-byte datagram cannot pass `send_client`'s space check. A three-second loopback match connects and samples values 1 through 400 while the server keeps zeroes. Oversized inputs have no chunked transfer or compiler diagnostic. Choosing a declaration limit or extending transfer support remains an owner language-design decision; no limit is established by this note.

- The command C contract is agreed: `tide_game.check_command(data, size, &channel)`, `command_sequence(world, player, channel)` and `command(world, sender, channel, sequence, data, size)` validate bytes (saying which channel the command's type is in), read the world's sequence in a channel and record a received command. `tide_session_send_command` and `tide_client_send_command` copy bytes into the pending queue, and derive the channel from the bytes. Generated `tide_local_command(local, channel, &size)` peeks the local queue's oldest command in a channel and `tide_local_command_sent(local, channel)` removes it after a successful send; the host table exposes both, and the standard host goes through the channels in turn, so a channel that's full at the session holds back only its own. Payloads include a type and target plus copied fields; string bytes and a `List<byte>`'s bytes, never heap handles, go over the network.
- **A command's ordering is its type's, per player** (agreed 2026-10-06; built the same day, the picks Provisional in `docs/spec.md`, Commands): `[Channel(Channels.Uploads)]` on a `command`, with an enum member of the game's, puts it in a channel of its own, in order among that channel's commands and against nothing else; `[Unordered]` gives it no order at all. The server still picks the tick, and every handler runs in the tick's end queue in the server's order. What's built, and what code relies on:
  - **Channels are numbered in the world's state, not named there:** 0 is the default, and the members `[Channel]` names are 1 to N in the order of their enums' declarations and then their members (`number_channels` in `compiler/src/check.c`, `program.channels`; `decl.channel`, `decl.unordered`), 63 of the game's own at most (`TIDE_COMMAND_CHANNELS_MOST`). `tide_game.command_channels` says how many (0 for 1), and `TIDE_COMMAND_UNORDERED` (`UINT32_MAX`) is the unordered ones' channel on every contract that takes one. The channel is never on the wire: the command's type is in its first 16 bits, and `check_command` says the type's channel, so a client can't put a command in another's.
  - **The sequence state is the hidden `tide_command_state` singleton** (`compiler/src/codegen.c`): `seq<p>` for the default channel, as it was, then `seq<p>_<k>` for channel k, then, only in a game with an unordered command, `top<p>` (the highest id applied) and `seen<p>` (a `long`: a bit for each of the `TIDE_COMMAND_WINDOW` ids down from the top). So a game with no channel attributes has the world layout it had. `tide_game.command_applied(world, player, id)` reads the window (NULL in a game with no unordered command): an id older than the window counts as applied, which is why a client never has more than 32 pending unordered ids, allocated in sequence from the top. An unordered command's id goes in `sequence`'s place everywhere.
  - **A `List<byte>` field is a file's bytes** (`type_has_bytes`): on the wire as a varint count and then the bytes on a whole byte (`tide_command_align`), packed into room the list's count sizes, through `tide_list_flatten`, and read into a scratch list (`tide_list_from`) that `tide_own_` copies into the match's heap with the rest of the event. A command with one is `TIDE_COMMAND_LIMIT` (4 MiB) packed bytes at most, any other `TIDE_COMMAND_BYTES` (1024) as before. Lists of other values came later (Commands holding lists of values, 2026-11-04, at the top).
  - **Over `TIDE_COMMAND_BYTES` a command goes in chunks** (`engine/src/session.c`: `MSG_UPLOAD` up, `MSG_DOWNLOAD` down, `PROTOCOL` 12): `CHUNK` bytes each, `COMMAND_CHUNKS_PER_UPDATE` (16) an update each way, under the inputs and frames, each sent again after `resend_after` without an acknowledgement. A client uploads one at a time (`send_upload`), the server acknowledges in its SERVER datagrams (`UPLOAD_PART`, the chunks it has, as a CLIENT says a world's; `UPLOAD_WHOLE`, with the download's number once it's published) and takes the whole command into the player's queue as any other (`take_command`). **No tick waits for a transfer:** when a big command's turn comes, the server publishes it (`publish_command`, `published`: `PUBLISHED_MOST` kept, until their tick's frame is gone), sends it down to each player (`send_downloads`, acknowledged in CLIENTs after what the world lacks), and puts it in a tick only once every player in the match has it (`everyone_has`; a player being sent a world, or catching up, is sent it with the ticks it lacks instead, as `needs_published` says by the tick of the world it was sent). The frame then names the download (`size` over `TIDE_COMMAND_BYTES`, then its number) instead of carrying the bytes; a client's `pending_frame` reads the names once its pieces are there (`read_frame_names`) and is complete once those downloads are whole (`frame_complete`). Its sender keeps the bytes and, told the number, says it has the download (`note_download`), so nothing goes down to it. The wire adds nothing for a game without commands (`g->command` guards both sides), so `net_a_match_costs_little_to_keep_going` stands as pinned.
  - **The local outbox** is `tide_outgoing_item`s (a malloc'd buffer, its size and channel) in the local state, `TIDE_COMMAND_OUTBOX` of them (32 a channel, the unordered ones counted as one), packed after the struct in `tide_local_pack` (each checked again as it's unpacked) and freed by `tide_local_free` and `tide_local_disconnected`.
  - Generated C: a game with no commands generates the C it did, byte for byte (old and new tidec over every game's source list); one with commands differs only in the contract's signatures, the outbox and `TIDE_COMMAND_CHANNELS` in its header, and so in its hash, which is one hash, not a layout change.
  - Tests: `compiler/tests/e2e/commands_channels` (channels numbered and their sequences apart, the window, a 3 MB command whole on one world and through the outbox's bytes, a chat line handled within half a second of a 3 MB upload on a lossy network while no player's verified tick falls behind, the same upload on a lossless one with no input late, 20 unordered commands once each under 30% loss, a late joiner's world with the sequences, a handover keeping every channel's and the window), natively and on the web; `compiler/tests/errors/command_channel_*.tide` and `command_unordered_*.tide`, one per diagnostic; `tests/test_commands.c` as it was, on the default channel.
- A match takes its configured maxPlayers (16 by default), up to the shared TIDE_PLAYERS_MOST ceiling of 65,536. Its count is part of the input/Audience layout and build compatibility. Current protocol, storage and fixture contracts are in Shared player/connection ceiling above.

- How late other players see you is the priority (the owner, 2026-10-05: "the entire point"; a player's own input was never the issue, and a change that made views draw the tick in progress was built by mistake and reverted the same day, as it also made each machine guess the others a tick further). What's between a key read on one machine and another's view showing it, beside the network: the wait for a tick to sample it, how early the client sends its inputs (the server holds each until its tick), the server's frame, and the watcher's.
  - A client sends its inputs as early as they have to be, and no earlier (`keep_early` in `engine/src/session.c`). The server says with everything it sends how long the player's inputs waited before their ticks ran (`note_wait`: from when each arrived, by its transport's clock, to the update that ran its tick, the least of the ticks an update ran; under 0 by a tick for each input missing when a tick ran, counted from the newest input the player sent, one that came too late among them: `connection.newest_sent`). It's said in 960ths of a second (`WAITS`), a signed varint after the newest input, which is a byte while the wait is under 65 ms; the tick before them went from 32 bits to a varint, so a SERVER is no bigger than it was for a match's first 9 hours at 60 ticks a second (a byte more after), and `PROTOCOL` went to 10. The least of those waits over a while is what counts, as being early is for the input that's nearly late.
  - What the client keeps that least at (`early_wanted`): its lead in ticks (`tide_client_desc.lead`: 0 on the server's machine, whose inputs go straight in), or, with `lead_frame`, which a session's player over a network has, one of its own frames where that's less (the owner, 2026-10-06, of what it saves: "1 tick would be huge"). A frame because inputs are read and sent a frame at a time, and every unacknowledged input goes again in each: so a frame that came late, or a datagram that was lost, still leaves the input in time. (Since 2026-11-16 the ones that can still be in time go again, not every unacknowledged one: the top section.) A machine's frame is the time between its updates, on the whole, the last while it watched (`frame`), and counts as a 60th of a second where it's shorter (`EARLY_FRAME`): that's what a match of 60 ticks a second has with a tick, so a fast display gets no less. Hosts that say `lead` alone keep whole ticks, as tests and benchmarks do.
  - It watches a while at a time. Under what's wanted, its clock goes ahead at once by what was missing (`clock_start`, to a 960th of a second, where it used to be whole ticks). Over it for the whole while by as much again (a tick at the most), its clock loses the difference, a tenth of the time that passes (`CLOCK_SLOW`, in `update_client`), so no tick stands still. The while is 2 seconds (`EARLY_LOOK`), and 10 after an input was nearly late (`EARLY_KEEP`): what made it so may come again, and has to stay away that long. One that was nearly late in the client's first second in the match (`EARLY_START`) doesn't count for that, as a machine starting up stutters, and that doesn't come again. After a change it watches again once what the server says is about the clock as it is now (below).
  - **What the server says only counts when it's about inputs the client sent by its clock as it is** (2026-10-06, found with Alley, the game Sumo took the place of: a player who joined a world of 160 KB mid-match ran the 60 ticks ahead a client may, its inputs waiting 0.9 seconds at the server, for ten seconds and more, and ran 2,736 predicted ticks again where the others ran 80). Two things made it so, and both are fixed:
    - The server ran ticks for the player while its world was on its way, 53 of them there, and said each was a tick the player's input was late for, counted from the tick it joined. They're ticks the player had no world to sample an input for. A server says nothing of a player's waits before the player's first input (`connection.sent_input`; `WAIT_UNKNOWN` on the wire, 3 bytes while it lasts, and `PROTOCOL` went to 11), and how late a player is goes by the newest input it sent, late ones too (`newest_sent`), where it went by the newest that came in time: a player whose inputs all come two ticks late is two ticks late, however long that has gone on.
    - The client took what it last heard as being about its clock once a round trip and three ticks had passed since it set it (`shown`). On a network that's late or loses datagrams, what it last heard was still from before, so it read that it was a second late and put its clock a second ahead. It goes by the tick now, never by a time: after a change, and as it joins, `early_settled` notes the first tick whose input is sampled when its time comes by the clock as it is (`early_tick`: the next one, once it has sampled up to where the clock is and isn't losing time any more), and nothing the server says counts until it has run that tick (`waited_tick`, the server's tick as it said so). Datagrams come in any order, so the newest is what it said last, by its tick.
    - A client behind the server catches up with its confirmed ticks before predicting (`fall_behind`, `play`): after a world arrives, after it arrives again on a resync, and after this machine stopped while the server went on. It discards predictions made from the old timeline, and samples nothing for ticks that can't get there in time (`skip_old_inputs`, `sampled_from`); that boundary is checked each catch-up update, so a second pause doesn't make it sample old ticks. Even outside catch-up, it samples no tick the server has already said it ran. A shorter pause with input delay enters catch-up too when new inputs are behind the server and a complete, due confirmed tick is left because the frame allowance is exhausted. Future inputs still go out while it catches up. Ticks it sampled nothing for aren't counted as late, nor do their waits change its clock; a fresh input that comes late still is. It predicts once every tick the server said it had run is confirmed, or the next confirmed tick's time hasn't come.
    - Catch-up work belongs to a frame's moment, shared by both client updates a session makes (`catch_up_room`), not to each call: 8 authoritative ticks at the most normally (`CATCH_UP_TICKS`), or twice the ticks of a consistently longer frame interval where that's more. The interval is the shorter of the last two, so one stalled frame doesn't ask for more. With a tick clock (`now_ns`), authoritative work has a budget of half that interval, checked between ticks, after running the ticks that came due and one more, capped at 8 mandatory ticks; a machine too slow for the match falls behind without each frame asking for more work than the last. Prediction and re-simulation keep their existing budgets.
    - A clock proven far ahead comes back at once (`clock_back`), as when the server's machine stopped and dropped time. In two updates that hear from the server, with every tick it says it ran confirmed here, the client's clock must be past the input window, and a clock set as a join sets it, from the server's tick and that answer's round trip, must fit within it. A delayed answer includes its delay, so a slow network alone doesn't reset the clock. Predictions past the new clock go, while inputs already sampled stay: it samples nothing more until their time comes.
    - `net_join_big_world_runs_no_further_ahead_than_its_inputs_need` (`compiler/tests/e2e/net_join_big_test.c`) joins a world that takes over a second to come on 8 lossy networks, and checks from its arrival that prediction starts only after catching up, stays within 20 ticks and 150 ms of input wait, and confirms at most 8 newly run ticks a frame. The same file checks a resync, two updates per frame, expensive ticks with made-up clocks, and bounded frames on a machine too slow for the match. `compiler/tests/e2e/seen` checks a client returning after a pause, a server returning after one with the client's clock restored, and delayed/reordered answers that leave a running clock alone.
  - Measured in made-up time, frames of a 60th of a second from a key read to another player's view showing it, on a network with no delay and one 30 ms each way. With inputs 2 to 6 ticks early, wherever in that the clock started: 3 and 6 frames at 60 ticks a second, 6.5 and 9.5 at 30, 8 and 12 at 20. A tick early at the least: 2 and 5, 4.5 and 7.5, 5 and 9. A frame: 2 and 5, 2.5 and 5.5, 3 and 6, with no input late. What's left at fewer ticks a second is the wait for a tick to read the key, half a tick on the whole.
  - All the time an input waits less is also time less that a machine runs ahead of the server, guessing the others: what it puts right when a guess was wrong is that much smaller.
  - `tide_client_status.early_ms` is the wait the server last said, and `late_ticks` how many ticks it ran without an input this machine had sampled for them. `server_tick` is the last tick the server said it had reached; `run_ticks` counts every tick this client ran, and `ahead_ticks` the predicted ones among them, including `again_ticks`. `compiler/tests/e2e/seen` pins the frames above as the most they may be, with none late, and checks that on a network that delays some datagrams 40 ms more than others the inputs go as early as that needs, that a machine of 30 frames a second keeps a 30th of a second, and that a frame that never came makes no input late.
- Running predicted ticks again is prediction's cost, and a client keeps it within what a frame can afford by delaying its input (`tide_prediction`, the `prediction` setting): it samples inputs as far ahead as ever (`sampled`), so they arrive in time, but runs ticks `delay` short of them. Predicting (`Prediction.Rollback`, the default), it starts with none and keeps the least at which running ticks again fits, by what that costs in practice, measured with the host's clock (`now_ns`, which `tide/host.h` sets to `tide_time_now_ns`); without a clock, as in tests and benchmarks, there's no delay and a run is the same on every machine, and the server's own machine (`tide_client_desc.lead` 0, whose inputs go straight in) takes none. What a rollback costs is measured as it happens (`note_tick_cost`): its first tick apart, which restores Box3D's resident pages in Sumo, as the median of the last five rollbacks' (`rerun_firsts`; none in the client's first second with a world, when everything is cold), and each after it at what a predicted tick costs (`tick_cost`, a moving mean, a sample of which counts for four ticks at the most: a stall isn't a cost); and what rollbacks have cost an update on the whole (`resim_load`, a moving mean over `LOAD_SECONDS`, an update counting for two budgets at the most, rescaled when the delay changes, as each rollback then runs a tick less or more). A rollback would run again about the client's lead of late and a tick (`lead_mean`, sampled only while it's level with the server: a server that fell silent, or a burst of its ticks still being confirmed, isn't a lead), and may take up to a frame, the time between its updates (`ROLLBACK_FRAMES`), and never less than `tide_client_desc.resim_budget` (4 ms unless the host says) nor than a 60th of a second (`ROLLBACK_SECONDS`), so a 144 Hz display gets no more delay than a 60 Hz one; rollbacks together may take up to the budget an update. It takes a tick more when either is over and its lead is settled (within `LEAD_SETTLED` ticks of its mean, and level: a client whose clock jumped while its machine stalled, or whose server's ticks are arriving in a burst, runs far ahead for a moment, and that moment is no reason for a delay that takes seconds to come back), and one back once both would still hold with a tick less, with a tenth to spare, for `ROOM_SECONDS` (half a second), or at once where half of what it may take would be to spare, with a round trip and three ticks between changes; and with no rollback for `QUIET_SECONDS`, nothing says the delay is still needed, so it takes none more and takes them back a tick at a time, which is what keeps a stale estimate from holding a client at a delay forever. `net_a_stall_leaves_no_lasting_delay` (`compiler/tests/e2e/net_test.c`) stalls a client's machine for a second and a half, and then its network, after which it runs 60 ticks ahead on its clock, and checks neither leaves it any delay. `tide_client_status` says what it goes by (`rollback_ms`, `rollback_most_ms`, `resim_ms`), which the lag harness records (the last three of `tide_run_netcode`'s values). Until 2026-10-07 it charged a full rollback of every predicted tick against the budget every frame, whether any happened, and took the delay back only once that was under a third of the budget for 5 seconds: so Sumo's players sat at whatever delay a join or a round's ring raise had pushed them to, with two ticks of lead on a desktop and one in a browser, whatever the ping, and no cut to the tick (the hash was never in `tick_cost`) could reach it (`bench/README.md`, Input delay in Sumo, which has the before and after). Two cuts before this one are in that section too: one that ratcheted to 41 ticks after a 600 ms stall of the machine, and one that gave the browser's host a delay from a cold first tick nothing refreshed. With `Prediction.Delay`, clients never predict: the delay starts at the lead and round trip plus half a round trip and two ticks, grows when a frame hadn't come by its tick, and shrinks once frames have come three ticks early for five seconds (`SLACK_SECONDS`, that mode's alone). Either way a frame whose tick the clock hasn't reached waits (`play`), so ticks run at the match's pace however early frames come, and views blend up to the clock less the delay. One change per round trip (`adjust_delay`).
- A client guesses the other players' inputs, and the server's, by their last ones for only so long (the owner, 2026-10-05: "we should limit how far we extrapolate inputs; if we have 500ms ping the corrections are WILD"): 0.15 seconds past the last tick the server confirmed (`GUESS_SECONDS`, or `tide_client_desc.guess_seconds`; to the nearest tick), and in the ticks it predicts past that, as if they'd let go. A guess that was wrong then moves things by what a player does in that long at most, where it was for as far as the client runs ahead, about its round trip. Agreed 2026-10-06: the 0.15 seconds, as the engine's with no setting for now; that fields are let go unless they're marked; and the mark, `[Absolute]`, on the input's own fields (`docs/spec.md`, Input, Decided; the details under it are Provisional). How:
  - Let go is the input's defaults, through the steps any input takes, but for the fields marked `[Absolute]` (the owner's name for it, over `[Hold]`: a field whose default isn't rest, as it says where something is, like an aim or a pointer's cell), which a guess keeps. The devices match code reads are let go with the rest. tidec generates it as `tide_game.let_go_inputs(world, keep)` (`gen_game_api` in `compiler/src/codegen.c`), in the generated `.c`, and not at all for an input that's all `[Absolute]`. It gives the tick before the same input (`previous_inputs`), so no `.down` or `.up` is true for a guess that lets go. `[Absolute]` is an attribute on the field and nothing else (`check_absolute_attribute`, `field_absolute`): nothing of it is in the header, so a game's hash is what it was.
  - It's only ever done to a predicted world: `run_predicted` in `engine/src/session.c`, at the first tick `guess` ticks past `verified`, and the ticks after start from that world. What the match is stays as it was: a slot whose input didn't arrive keeps its last one, on the server and in every tick it confirms.
  - A tick run that way is another world than the same tick with the last inputs kept (`tide_client.let_go` is the first such tick), so two things follow. It never counts as having gone as predicted (`tick < c->let_go` in `play`): its hash isn't the server's, and the client would ask for the world again. And once ticks before it are confirmed, so that it's within the guess, it runs again with the last inputs, and the ticks after it (`keep_guessing`, `predict_from`): while another player goes on doing something, a client further ahead than its guess runs the ticks past it again each time a tick is confirmed.
  - It costs nothing where nothing is let go: a client that runs no further ahead than its guess (a round trip under about a tenth of a second), and a guess that changes no input, as nobody is doing anything or all that differs is `[Absolute]`. The budget for running ticks again (`adjust_delay`) already counts every predicted tick as run again each frame, so it's as it was.
  - `compiler/tests/e2e/guess` checks it in made-up time, with both players a quarter of a second from the server: what a client shows of a player who runs and stops is never more than the guess past what it knows, which is the most their body moves in an update (30 ticks and more with no limit, which the same test shows), an `[Absolute]` field and this machine's own input go all the way, no button goes up for a guess, nothing runs again while nobody does anything, and no client's world ever differs from the server's, on a network that loses and reorders too. `compiler/tests/errors/absolute_*` has a file for each diagnostic.
- A ping is the network's round trip, and nothing else (agreed 2026-10-05: two players at one desk read 14 ms, two frames of waiting, where the owner expected 0 to 2; "logic depend on latency has other ways to use it"). `Session.ping`, the stats' ping and `tide_client_status.ping_ms` are from when this machine sent to when the answer arrived, less what the server held it. What an answer takes to be read, the waits for an update on both machines in it, is `answer_ms` (the client's `rtt_ms`), which is what the netcode still goes by: when to send again, how far ahead to run, how long to wait for a change to show. How:
  - A transport says when a datagram arrived, not when it was read: `tide_transport.clock`, its own clock in seconds, and `arrived`, when the datagram `receive` last gave arrived by it. Both NULL where it can't say (a wrapper in a test): datagrams then arrive as they're read, by the session's time, and a ping counts the waits (`clock_of`, `arrived_of` in `engine/src/session.c`).
  - The server notes when the datagram that said the time it echoes arrived (`connection.their_arrived`), and says how long it held it with every echo: `held_ms`, a varint of milliseconds after the echo in a SERVER (a byte, while it's under 128 ms) and 16 bits in a WELCOME. `PROTOCOL` went to 9 with it.
  - A client remembers when it sent each of its last 256 times, by the transport's clock (`said_time`), and `measure` finds the one echoed: the ping is the answer's arrival less that, less the held time. No session time is in it, so neither is the work of an update between reading and sending.
  - The loopback network's clock is its time, and a datagram arrived when its time came (`engine/src/net.c`): a machine's ping to its own server is 0, and tests and benchmarks repeat.
  - `tide_net_conditioned` (2026-10-27) wraps any transport with a loopback's queue and conditions, both ways: what `tide run --latency/--jitter/--loss` puts between a machine and a match it joins (notes/standard-host.md, A slow network to play on).
  - Rooms on the web: the page notes `performance.now()` as a datagram comes, or as the page lets go of one it held back (`Tide.lag`), which the program's clock is too (`room_arrived` in `platform/web/tide.js`).
  - UDP and desktop WebRTC read their sockets through `platform/src/arrivals.c`, which says when each datagram arrived, by `tide_time_now_ns`. Linux, macOS and Android stamp datagrams themselves (`SO_TIMESTAMP`, read with `recvmsg`; by the wall's clock, so it's how long ago that was, never longer than since the socket last had nothing). Windows only does from Windows 11 on, so there the system calls when the socket has something (`WSAEventSelect` on an event the thread pool waits on, `RegisterWaitForSingleObject`), and the call reads it and notes the time into a buffer the frame takes from (up to 256 KiB for sessions' UDP, 64 KiB for a WebRTC peer: what a socket held by itself); no thread of ours. WebRTC carries it up: the ICE socket's (`rtc_ice.arrived`), to the message it came in (`rtc_peer.arrived`), to the room (`backend_arrived` in `platform/src/rooms.c`). A host on UDP and in a room at once says each datagram's by the transport it came on.
  - `net_a_ping_is_the_networks_round_trip` (`compiler/tests/e2e/net_test.c`) checks it in made-up time: 80 ms on a network 40 ms each way with updates 11 and 22 ms apart, 0 on one that takes no time, and through a transport with no clock, the waits counted. `udp_a_ping_counts_no_wait_for_a_frame` (`platform/tests/udp.c`) checks real sockets with frames 60 ms apart: the least of 20 pings, as a busy machine can note one late (measured on Windows: 0 ms, with answers taking 120). In the docs' match on one machine it went from 13.9 ms to 0.6.
- Legacy whole-world path (`tide/session.h`, games without client projections): the server ticks the one true world and sends each player every tick: who joined or left, the inputs that changed, and a hash of the world after it. Inputs that didn't change or arrive keep the last one, on every machine. A client keeps a snapshot of every tick from the last one the server confirmed (the verified world) to the one it predicted. A tick that arrives as the client guessed only has its hash checked against its snapshot; one that went otherwise runs on the snapshot before it, and the ticks after it run again. Worlds go as deltas (`tide/delta.h`), each a numbered transfer in chunks. A region that goes is XOR'd byte by byte with the one a stride before it where that packs it shorter: the writer tries none, 1, 2, 4, 8 and the size of the region's rows or cells, counting the bytes each leaves that aren't zeros in a sample of it, 8 at a time, and the region says which. Joining players get the world, and a cookie that gets them their `PlayerID` back when they join again. In a game with a seed (`tide_game.seeded`), the server picks the match's as it starts it, and the world carries it. A hello alone gets a welcome: the join and the world wait for the player's first message back (`greet`), so a hello from a spoofed address costs a datagram, never a player in the match or megabytes sent there, and one that never writes back gives its slot up in two seconds with its cookie. A whole world is packed once a tick, however many players join in it (`pack`). A join or leave for the next tick is never dropped: a player who joins and leaves before the tick never was in the match, so the two cancel. The server keeps every tick since a world it sent until that player catches up, however long the world took to arrive (`keep_needed`), up to eight times its four seconds; a player that falls behind otherwise, or further than that, gets the world again. A player with a world to build it from (its own, when its hash differs; its last one, coming back to a match that changed hands; or, joining, the match as it started, which it starts itself when starting calls no C: `tide_game.pure_start`) gets the hashes of the server's pages first, after how the match started, says which its world lacks, and gets those; one that doesn't come out as the hash says is asked for again, whole. Worlds that go in one update anyway go whole. Inputs go as what changed (`write_input_delta`): in frames, from the slot's input as the world keeps it before the tick; from players, each from the tick before. In an input, ints, int enums, players and entities go as varints in bits (7 bits a group and a bit for whether more follow, `tide_bits_put_varint`), signed ones zig-zagged first, so small numbers take a byte; floats keep their 32 bits, and byte and ushort enums their 8 and 16. As a delta, an int or int enum that changed goes as by how much (`tide_bits_put_changed_difference`, wrapping), and a float as its bits' XOR with what it was, the run of bits that differ and where it starts (`tide_bits_put_changed_xor`, as Gorilla packs floats), or its own 32 bits where that's shorter. Packing never changes a value: every one comes back bit for bit, NaNs and -0 too, and `tests/test_bits.c` checks it. Datagrams keep times to 16 bits and small numbers as varints, and a tick where nothing happened is its hash and a byte. `net_a_match_costs_little_to_keep_going` pins what a match costs to keep going: a change that makes it cost more updates the test on purpose. A client sets its clock from the server's tick as it sent the world, not the world's own tick, which a transfer leaves behind. Everything is resent until acknowledged; nothing waits on a reliable stream. A machine that runs the server and stops for a while (a breakpoint, a browser that freezes the page) stops its server and its player with it: its session drops the time beyond what the server can run in one update, so the two never time each other out and nothing runs ahead to catch up. A client of another machine keeps to the real time.
- Host migration is a game's choice, off by default: the `hostMigration` setting (see `docs/spec.md`, Sessions). When a room's host leaves or times out, another player's machine goes on from its verified world, which is bit for bit the server's at that tick, and the others join it again with their cookies. The server sends its players a handover record (`MSG_HANDOVER` in `engine/src/session.c`): its room's code and key, its own player, whether it's open, and who's in the match with their cookies' SHA-256. The relay decides who hosts (`{migrate: code, key}`, in `relay/relay.mjs`): it pings the room's host, and gives the room to the first player there when the host is gone. `tide/host.h` drives it (`tide_run_migrate`), with `tide_platform_room_migrate` on both platforms. A goodbye can be lost, so a match that ends (`Session.End()`, or its last scene) also tells its transports (`tide_transport.end`), and a room tells the relay (`{end}`), which keeps it as ended for five minutes: a player who comes back to take it over hears it ended. The room keeps its relay connection (or makes one) until the relay has read it (on desktop, the relay's answer to the WebSocket close that follows says so: `rtc_ws_finish`, as a socket closed at once can be reset before the relay reads it), and asks for the room first on one that hadn't, since the relay may not know it yet: a room's code shows before the relay hears of it, and a host that lost the relay is reaching it again. Nothing about it is timed on the server. So in a match with host migration on, the server keeps nothing beside the world that the players don't also have: what it keeps of its own (each player's cookie, as a hash, and whether the match is open) goes to every player. A feature that keeps more on the server has to send it too, or not work in those matches. Games using recipient projections disable this handover path until a trusted full-server transfer exists, as described above. A match on an address (no relay to meet at again) ends as it does today.
- **Provisional:** `[Position]` and `[Rotation]` describe match fields for view interpolation and smooth prediction corrections. `[Position]` on a float or floating-point vector keeps linear tick interpolation and accumulates additive presentation error. `[Rotation]` on a float in radians or a quaternion uses shortest-path angular or relative-quaternion interpolation and correction error. Error is local to presentation and decays exponentially with frame time, with a provisional 75 ms half-life; later corrections accumulate, while ordinary movement follows the corrected simulation without being low-passed. Untagged fields keep ordinary tick interpolation. `entity.Snap()` and `singleton.Snap()` remain immediate and clear accumulated error. These attributes never change simulation writes, collisions, world hashes or network state. Ordinary structs can contain annotated built-in fields; correction smoothing through a type's custom `Interpolate` is deferred, with no new custom-type contract or rate setting agreed. Tags take no arguments and cannot combine with each other or `[Snap]` on one field. They do not belong on local state, inputs or events. A new match, hot reload or replacement without comparable presentation history clears the error; migration retains it while drawing the frozen world, then clears it on adoption of the new network timeline. See `docs/spec.md`, Provisional correction smoothing.

## Confirmed presentation and untrusted view times (2026-10-11)

`tide_view_worlds` supplies confirmed `verified`, `verified_previous`, `verified_alpha` and upper `verified_tick` separately from prediction. Its clock advances by elapsed real time, clamps to the latest confirmation and never extrapolates. Join catch-up skips its presentation backlog so a large download cannot fill the bounded snapshot ring and stall verification. The ring retains the displayed confirmed interval alongside prediction (maximum twice the prediction window plus four). Latest verified-world access remains latest confirmed state; view endpoints may lag it. Reload, resync and migration reset presentation onto their new timeline.

A presented frame stamps the next sampled input with `tick - 1 + alpha`, through optional generated tide_game callbacks, after Sample. The server treats this as untrusted: finite fraction/range, nonnegative tick, connection-attempt admission start, advertised server tick, execution tick and configured maximum age must pass. Repairs do not advance the admission start (the 2026-10-26 correction above). Rejection replaces only the timestamp with invalid zero; ordinary input is retained. Missing input can repeat an old stamp, so Physics.Rewind independently rejects stale/unavailable history. A timestamp cannot establish what a modified client displayed. Games validate shooter, origin, aim, ammo, cadence and damage against authoritative state.

`view_time` exercises engine stamping, fractional/held confirmed clocks and future, negative, out-of-range, NaN, pre-join and stale wire claims, with no resync and usable ordinary input. `net_join_big` covers bounded catch-up. Full networking/migration suites retain their existing behavior.

End-to-end frame/shot validation (2026-10-11): the two physics rewind-network fixtures independently schedule a server and two clients, draw actual verified view commands, mark their presented tick/fraction, sample and transmit shots, then compare server query results with the last drawn position. Profiles cover 20/30/60/144/240 FPS, 30/60/120 Hz simulation, varying frame intervals, 600 ms update/render stalls, independent 240 Hz updates/20 Hz rendering, 0/20/50/80/180 ms one-way latency, jitter up to 30 ms and loss up to 10%. Every received shot is checked against its actual server execution age; valid stamps preserve the sampled frame, stale stamps become invalid with no rewind fallback. The target's owner changes movement through normal input on that client's schedule.

The fixtures exposed a prediction-reuse bug: a server-rejected old view stamp can leave the server's input unchanged, so its frame carries no input delta even though the client's sampled stamp differs. as_predicted now compares held inputs for this client's slots against the confirmed base before reusing a predicted world. Otherwise it replays the authoritative held input instead of requesting an unnecessary resync. This uses existing input/scratch callbacks, changes no wire or generated-C ABI, and preserves bounded validation. The fixtures require zero resyncs.

### Packet starvation and immediate playback recovery (2026-11-19)

`session_view_time_after_packet_starvation` runs a 60 Hz match with 20 ms
one-way latency, 120 Hz updates and 24 Hz confirmed frame marks. For 333 ms it drops every server-to-client
datagram while input and rendering continue. In the final second, verification is at most four ticks behind
and prediction is ahead with no input delay or resync. At the last frame both the control and starved clients
verify two ticks behind and predict three ticks ahead. Before the fix the confirmed display was 2.5 ticks behind
in the control and 25.5 after starvation. With a quarter-second rewind window, the server accepted all 61
control timestamps and rejected all 61 starved timestamps in that final second.

`update_client` discards elapsed playback time when it clamps `verified_view` at the preceding confirmed
frontier. Predictions and inputs keep advancing, so this loss need not enter `play`'s `catching_up` mode, which
moves the display directly to the frontier when joining or falling behind on input or simulation work.
The owner chose to skip the display backlog once confirmation recovers. At the end of `play`'s confirmation loop,
when the client has reached the last reported server tick or the next complete tick is still ahead of its clock,
`verified_view` moves straight to the latest confirmed tick if its backlog exceeds one tick plus 100 ms and
two client update intervals. That margin leaves room for packet bursts and slow updates. An outstanding world
transfer or visibility repair blocks this path. The next input still names the last frame actually shown until
the host marks a new one. Confirming ticks, predicting, and the server's execution-age rule use their existing clocks.

The fixture requires one forward jump on recovery, ordinary interpolation before and afterward, and matching
confirmed world hashes. It checks the expiry of old stamps held through the outage and acceptance of the exact
sampled frame after recovery. On Windows ASan and Node WASI both runs now show a final 2.5-tick display lag and
61 accepted timestamps. On both platforms, the Box2D and Box3D rewind-network fixtures, view-correction cases,
and `tidec_e2e_net` and `tidec_e2e_net_join_big` pass. The documentation build passes.

## T? fields on the wire (2026-10-11)

A field declared `T?` (`notes/language.md`, T? fields) is part of a world's bytes like any field, so worlds, deltas and hashes need nothing for it. `gen_bits` (`compiler/src/codegen.c`) packs one in a command, and in an input sent whole, as a bit and then its value only when there is one, reading into zeros; in an input's delta it's the value's own delta and whether `ok` flipped (a bit each while nothing changes), and the read ends by zeroing one with no value. An input from outside with no value but bytes beside it, or padding that isn't zeros, is zeros before `Sanitize` (`gen_clear_padding`). No protocol number moved: a game with no `T?` field packs what it did. `compiler/tests/e2e/optional_fields` pins an empty input at 2 bytes and two commands at 19 and 14, checks every truncation of a command is refused, and that whole and delta inputs come back bit for bit. Left as it is: an input's delta sends a value's change even as the field goes to nothing.
