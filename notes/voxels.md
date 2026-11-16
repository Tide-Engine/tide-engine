# Voxel worlds: what an endless world of blocks needs

Built 2026-11-14 on the owner's goal: "get all tools needed for a proper, efficient, bandwidth efficient, minecraft like game with infinite worlds, dimensions". The work started from a benchmark game written with the engine as it was, and closed what it measured. Everything language-visible here is Provisional (Claude's picks) until the owner confirms it: the spec says so where each is written (The server's store; State visibility).

## The game: `bench/voxels`

`voxels.tide` is the match, `main.c` its bots (`bench_voxels`, see `bench/bench.h`), `play.tide` what a person plays (`voxels`, a window; `--capture`/`--script` for runs with none).

- **A dimension is a scene** (`scene Dimension { number; seed; }`), loaded once each at the start. Its chunks are its scene's entities.
- **A chunk is an entity**: `Chunk { int3 at; dimension; seed; Grid3<Block> blocks = Grid3(16, 16, 16); stage; edits; }`, with a `Visibility` group of its own. A chunk as an entity is what the engine's rules already serve: visibility is per entity, shards go by position, `Destroy` unloads, and a `Loaded` event has an entity to go to. One open grid for a whole dimension has none of those (spec, Grids, Open).
- **Streaming is the server's alone.** The code that makes chunks and says who has them runs for components no player's machine has: `Streamer` on the dimension's own entity (who watches from where) and `Seen` on each chunk (who has it), both restricted to `Audience.None`. A handler or system that takes one never runs on a client, so clients neither run nor mispredict any of it. The first version kept a public `Map` of chunks in the dimension, and every client spawned and generated every player's chunks (2,102 entities held for 845 visible, 175 ms of simulation a second).
- **The box a player has** is `near` chunks sideways and `tall` up and down around the chunk they're in. `Move` sends `Want` to the dimension each tick; `Watch` does nothing unless the player's chunk changed, then removes the player from the chunks the box left and adds them to those it took in, spawning what isn't there. It finds them in the dimension's own `Streamer.chunks` (a `Map<long, Entity>` by `KeyOf(at)`), which has a chunk from the tick it's spawned: two players' `Want`s in one tick can't make two chunks of one place, as a search of the tables (which have a spawn only once the tick's commands are applied) could. `Gone` takes a chunk out of it as the chunk goes. The box is asked for nearest first (2026-11-16, At scale, A whole view arriving): a square ring around the player's chunk at a time, the player's own height first, around each ring, as the store answers in the order it's asked. And over as many ticks as it takes at `Worlds.ask` new chunks a tick for the dimension (1,024; 2026-11-16, A wide view without a long update): `Streamer.asking` has how far through its box each player is, and the `Want` of the next tick goes on from there. The chunks of a box left whole say they emptied `Worlds.ask` a tick too (`Streamer.emptying`), so they go over a few ticks, a linger later.
- **Finding a chunk by its place** on any machine is an indexed loop over a query (`Find`, `At`, `Has`, `Firm`): `if (row.chunk.at.x != at.x || ...) continue;`, one exact key of the place and the dimension (notes/indexed-loops.md, Exact keys).
- **A chunk's life**: spawned `Asked` with `Storage.Load(key, chunk)`; `Loaded` either reads back what the store had or marks it `Missing`, which `Generate` fills from the seed (`Math.Perlin`); only then is the group's audience set, so a player never has a chunk that isn't made. What a dimension makes in a tick is bounded by what its chunks cost (`Pace`, `Worlds.make`: A whole view arriving): the store answers 256 loads a tick, and a tick that made every chunk it heard of would be as long as they are many. `Idle` sends a chunk `Expire` `Worlds.linger` ticks after its last player left, and `Drop` (its handler, which runs after the tick's `Want`s, so a player who came back that tick keeps it) saves it if players changed it since it was last saved (`chunk.blocks.ToBytes()`, section 4; `Fill` reads it back with `Grid3<Block>.FromBytes`, and makes the chunk from the seed if the bytes aren't a chunk's) and destroys it. `Keep` saves a changed chunk every `Worlds.keep` ticks while it's held, each chunk in a tick of its own, so a server that stops loses at most that much. `Retry` asks again for a chunk still `Asked` after `RETRY` ticks (an answer lost to a server's stop or a rewind) and `Fill` leaves one whose load `failed` as it is, to be asked again, rather than made afresh and saved over what couldn't be read. `Idle` writes nothing while it waits (`emptied` is a tick, not a counter): a system that counts in every row makes every row's hash change every tick. The bench sets `linger` and `keep` lower in quick runs, so chunks are saved and loaded again within its 8 s.
- **Digging and building**: `Put` goes to the world, `Place` finds the chunk on this machine and sends it `Change`. A client changes the chunks it has as the server does, so its own edits show with no wait.
- **On foot** (`play.tide`'s input): gravity, a jump, and a body that stops at blocks, by reading blocks through the same indexed query (`Blocked`, 12 lookups of `Firm`, which takes a chunk this machine hasn't made, or hasn't got, as solid: a body never steps into blocks that aren't there yet); it moves only while the chunks its feet and head are in are made. The first solid block along the look direction, from the eye's own cell, is dug, or built on where the cell before it isn't one the body is in (`Inside`). A dimension hop puts the body on the new dimension's seed ground (`SeedOf`, `Ground`) and lifts it a cell a tick out of anything solid there before it walks (`placing`). Mouse look is `pitch - look.y`, as docs/game/cameras.md has it. Bots fly through everything (`fly`).
- **What a machine draws** (`play.tide`): each chunk's faces as a `Mesh<Vertex3>` made by a `local worker` from the chunk's grid (a snapshot of it) and its six neighbours' grids (section 9; their sides as lists until section 12), kept in the chunk's `local` fields and drawn with `Draw.Mesh(mesh, transform)`; made again when `stamp` (each change's cell and block mixed in) differs from the one its faces were made at, so a correction that leaves as many changes of other blocks makes them again, and when a neighbour's side came, changed or went. The task that awaits the worker (`Remesh`) writes `mesh`, `drawn`, `blank` and `meshed` itself (section 7), and the view starts one only while the last one's handle (`local LocalTask job`) isn't running: a flag the task cleared would stay set where a task ends with its fields kept (a predicted destroy the server undoes, a hot reload of its code). Past the chunks, the ground from the dimension's seed (`Ground3`, a square every 8 cells out to 768 cells, made on a worker around the player each time they go into another chunk, and as the far regions this machine has change): the same hills the server would make, with nothing sent for them, but where a far region says players changed a column (section 8). It has no square where this machine has the chunks and the ground is within their height, so pits and what players dug show; it's drawn 5 cells low so chunks cover it at their edges.
- **Far regions** (section 8): an entity for each 8 by 8 chunk columns, with the top of each 8 by 8 cell column players changed (`Far`), in a group whose audience is the players within 6 regions (768 cells), wider than the chunks'; the server's side (`Summary`, `Seen`) keeps them as chunks change, sends what changed every half second, 64 columns at most, and keeps them in the store as it keeps chunks.

## What the engine was missing, and what was built

Measured with 4 bots flying across fresh terrain, `near` 6 and `tall` 2 (845 chunks each), 60 ticks a second, 50 ms each way, release, Windows, 12 threads. "Before" is the first version of the game on the engine as it was; "after" is the game as it is now on the engine as it is now, so the columns differ by both.

| | before | after |
|---|---|---|
| The run | a bot fell 61 ticks behind in 2.4 s and the run stopped | completes, bots at most 32 ticks behind |
| Server update, median / p99 | 9.34 / 21.8 ms | 0.37 / 4.95 ms |
| Server, hashing | 407 ms a second | 18.8 ms a second |
| Server, packing worlds | 55 ms a second (663 packs) | 0.01 ms a second (4 packs: the joins) |
| A client's update, p99 / max | 64 / 147 ms | 2.2 / 4.7 ms |
| A client, simulating | 175 ms a second | 16 ms a second |
| Sent to each bot | 118.7 KB a second | 19.7 KB a second |
| Entities a bot's machine held | 2,102 | 836 |

Standing still (nothing changing, 845 chunks, 4 bots, `--still 100 --hop 0`): server hashing 140 ms a second before, 1.0 after; a client's 69 before, 1.1 after. With 8 bots moving (`bench_voxels`, 6,490 chunks loaded): server update 1.20 ms median and 10.3 p99, hashing 44 ms a second, 22.3 KB a second to each bot, a client's update 2.8 ms p99. With everything of 2026-11-14 and 15 merged (chunks made on every core, section 5; the hierarchy section; keys by run): server simulate 24.3 ms a second (36.5 before), update 1.30 median and 7.1 p99, hashing 45.7 ms a second, 22.5 KB a second to each bot, a client's update 2.8 ms p99, the same 856 entities on a bot's machine. With rows kept 64 at a time (section 2, 2026-11-15; medians of three runs each): hashing 43.0 to 18.4 ms a second, update median 1.27 to 0.95 ms and p99 7.1 to 5.8, the same bytes sent and the same world; kept by their blocks (section 2, later), hashing 18.1 to 9.5 ms a second, update median 1.08 to 0.96 and p99 6.48 to 5.71. A review of the first version (its fixes are in the sections below: rows kept one by one, audiences only where groups changed, the store's files and outcomes) took the 8 bots' server update from 3.35 ms median to 1.20 and its hashing from 98 ms a second to 44.

### 1. A repair that costs what changed (`compiler/src/client_changes_codegen.inc`, `engine/src/session.c`)

A client whose projection hash differed got its whole projection again: the server serialized everything the player may have (3.4 MB for 845 chunks), offered 16 KiB block hashes, the client serialized its own to compare, and loaded a whole new world. Every chunk border a player crossed cost that on both sides.

Now `tide_game.client_changes(world, base, lineage, recipient, ...)` compares two worlds of the match as one recipient may have them, entity by entity, by the hashes the tick's own check keeps:

- **The plan**: for each entity whose permitted value differs or that came into the recipient's sight, a flag (it had it), its archetype and its identity; then the identities of those that left.
- **`now`**: what a projection starts with (recipient, identity clock, lifecycle counters, singletons, inputs as permitted) and each planned entity's record (snaps, scene, components as permitted). **`was`**: the same of the base world, with the records of the entities the recipient had.
- The session sends the plan and `now` as what differs from `was` (`tide_recipient_delta_pack`, codec 2 in `pack_client_state`'s envelope). The client makes `was` again from the world it confirmed (`client_changes_base`), unpacks `now`, copies that world and applies (`apply_client_changes`): singletons whose bytes differ are read again, planned entities dropped and added, the rest untouched. The world's projection hash has to equal the frame's (`client_hash`), or it asks for its world whole as before.
- **When**: `start_client_repair` tries it first. Where the packed bytes pass 32 KiB and most of `now` is entities the player already had (another value of each), it falls back to the hash offer, which compares the client's own run of the tick and may send far less (Mount & Rifle's battles). A world of chunks never does: its repairs are entities coming and going.
- What a machine's own ticks made that the server hides from it (group below 0) is dropped at each apply, as it was when a projection loaded (`tidec_e2e_net_alone_seen` fails without it).
- `tide_entity_erase` (`engine/src/entity.c`) takes an entity out of a table with no key left behind: `tide_entity_destroy` keeps the key until the slot is reused, and `tide_entity_install` refuses a key it still has, so an entity that left a client's sight could never come back.
- **A projection lists its entities by identity** (`tide_entities_ordered_archetypes`), not by row. The baseline codecs (0 and 1) need the client's bytes of a world to be the server's, and a client's rows are now wherever its repairs put them. This is the "repair base that doesn't depend on row order" `notes/netcode.md` left open; `recipient logical state schema 4`.
- **A game with transform hierarchies** (2026-11-15; it had none of this before, repairing whole projections and hashing its serialized projection whole each tick): a recipient's links (`tide_client_hierarchy_node`, the parent and siblings it may see) and anchors (a child's whole placement where its parent is hidden) depend on other entities, so they're no part of an entity's record or its terms. A projection, a repair's `now` and `was`, and each tick's hash carry them as one section after the rows (`tide_client_put_hierarchy`): the entities in the hierarchy's `nodes` map and the visibility state's `anchors` map, sorted by identity (`tide_client_hierarchy_keys`), those the recipient may see whose links or anchor say anything, each with its links and its anchor. A recipient taking one (`tide_client_take_hierarchy`, in a load and in `apply_client_changes`) empties both maps and fills them from it, refusing an entity it hasn't, one out of order or twice, an anchor beside a parent, or a matrix that isn't finite; `tide_client_hierarchy_valid` then checks the links as before. `reload_client_state` stays the others' (it keeps tables whose bytes are alike, and the links are no table's). Simulation identity: `recipient hierarchy section 1`. `visibility_projection_repairs_links_and_anchors_by_changes` repairs a root hidden (it goes, its children anchored), a child detached and the hidden root moved (only an anchor changes) and checks the plan's `was` made again, the hash and the bytes of a whole projection; it fails where `apply` doesn't take the section. `tidec_e2e_visibility_projection_checked` and `_memo_off` run the fixture with every hash worked out twice and with nothing kept; its pinned hash moved to `0xfb96027f537cbbbd`, the same in all three. The render3d tests whose games use both (`tide_render3d_pipeline`, `_pixels`, `_material_pixels`, `_shadow_batches`, `_shadow_batches_pixels`) pass.
- **A repair looks at the rows that changed for its player** (2026-11-16; `notes/netcode.md`, top section): each tick's check writes down the rows it found changed, each with the players it could be another for, and a repair goes through the rows written between the world the player confirmed and the world now, where it listed every entity of both for that player. The same plan, `now` and `was`, byte for byte. A repair went from 0.58 ms to 0.11 with 8 bots, 0.91 to 0.20 with 32 (At scale, What was fixed).
- After a repair by changes the client drops what it had kept of the projection it loaded last (`drop_taken`, `load_snapshot`'s `by_changes`): its world isn't that one any more, so a later repair's hash offer mustn't take those bytes as the client's.

### 2. A tick's check that costs what changed (`client_state_codegen.inc`, `client_spans_codegen.inc`)

Every tick the server encoded and hashed every component of every entity, grids whole, for every world size; so did each client.

- **Containers in components hash as trees** (`tide_client_hv`), as singletons' did: a grid, list, map or set is its own hash, kept in the world's memo while every heap page it was read from hashes as it did. A client's worlds share one memo (keyed by its player, not the world's address: it hashes a different world of its ring each tick, and they share their pages).
- **An entity's hash is a sum**: what says which entity it is, plus a term for each component it has. What it hashes to for a recipient denied some components is the sum less their terms, with nothing read again (a chunk's `Seen` is denied to everyone, so every chunk was encoded twice a tick).
- **Rows are read where they are** (`tide_load_<Component>_at`, `tide_visibility_base_at` and `_denied_at` with one view of the group directory for the walk), not looked up by handle 8 to 10 times each.
- **Each row goes to the recipients who may have it**, from its audience's bits, not every row for every recipient.
- **What a table chunk's rows came to is kept** (`tide_client_kept`, with the memo): validated by the hashes of the chunk's own pages (a column each), of the heap pages its rows' values were read from, and of the entity table's pages its rows' slots are in (their snaps: `tide_entities_slot_page_hash`).
- **Then what each row came to** (`tide_client_rows`, one per archetype): where a chunk's pages changed, each row's own pages and the heap pages its values came from are compared with what it was kept with, so a row is read again only where its own values changed, and a component's term only where that component's did (`row_keep`, `lanes_digest`). Its audience, its components' denials and its own group and scene group (`tide_visibility_base_at`'s `groups`) are kept beside them.
- **Audiences are worked out again only where they can have changed**: for rows read again, and for rows whose group or scene group is one the directory's pages say changed (`groups_diff`, a sorted copy of the directory's group ids and audiences against the last tick's). A scene's own row changing, or the directory being too big to copy, works every row out again.
- **The repairs read what the check kept**: `tide_client_see_kept` takes a row's audience and terms from `tide_client_rows` where the world is the one the tick's check hashed, and reads the row only where nothing is kept. What's kept is trusted, not checked against the world, so it must be the world's as it is: `tide_client_kept.tick` is the world's `Time.tick` when the check kept it, and `client_changes` reads every row where it isn't the world's tick now (a world that ticked with no check since; a session checks after each tick, before any repair).
- The memos are the process's, one for each world hashed (a server's; a player's machine's, whose worlds share one): `tide_client_memos`, made as asked for up to 1,024, and one nobody asked for in four times as many calls as there are memos goes to the next world that needs one. A memo is one thread's while it hashes (`busy`). It sweeps the entries whose pages changed once it holds twice what it held after its last sweep (`swept`), so a world whose containers come and go keeps a memo its size. A server's is found by the world's address, so `tide_world_free` empties any memo kept for it (`tide_client_memo_forget`): a world made where a freed one was (a dedicated server's next match, a test's next world on the stack) is another world.
- **Correction (2026-11-15)**: both of those were missing. CI failed `visibility_projection_repairs_links_and_anchors_by_changes` on the web, macOS and Android, never on Windows or Linux: the test before it in the file left a memo with kept rows at the same stack address, at the same tick, and the repair took the root as still seen (no record changed, none gone). Alone, every platform passed, and its later repairs passed by luck (their changes were links alone, which kept rows don't hold). `repaired_by_changes` now works each repair out before the tick's check (every row read) and after it (what it kept), the two byte for byte alike; the test checks the tick before the root is hidden, so the first is read across a tick with no check; `visibility_projection_changes_forget_a_freed_worlds_rows` frees a world hashed at tick 2 and makes another where it was, two ticks in with the root hidden. Each fails without its half of the fix, on Windows too.
- `TIDE_CLIENT_SPANS_CHECK` works every hash out both ways and stops where they differ (`bench_voxels_checked`); `TIDE_CLIENT_MEMO_OFF` keeps nothing. Simulation identity: `singleton and entity tree hashes`. The two pinned projection hashes moved (`visibility_projection_test.c`, `visibility_hash_memo_test.c`), memo on and off agreeing.
- **Kept 64 rows at a time** (2026-11-15; `notes/netcode.md`, top section, has the whole of it): a table chunk's rows are kept in pieces of 64, each passed over where the runs of its columns (hashed again only for columns whose page was written), its rows' heap pages and their slots' pages are as they were; in a piece read again only the components a column of which changed are digested, and a row's identity is hashed again only where its entity's, scene's or slot's columns moved. A Visibility's policies are kept as what they say, so a row whose pages changed under it with nothing of its own changed doesn't have its audience worked out again. Audiences go again a piece at a time, by the least and most group its rows go by and a bit for each; a scene's root makes everyone's go again only where it came, went or has another Visibility (a dimension's `Streamer` map made 369 of 2,017 calls work out every chunk's audience). Hashing with 8 bots moving went from 43.0 ms a second to 18.4 (medians of three runs); `tidec_e2e_visibility_pieces` makes each change that could go unseen in a piece nothing else touches. **Correction (review, 2026-11-15)**: a policy set where it is went unseen where nothing else of the row held heap data, or where the Visibility's map had been made or moved without the row's other values changing; and a row whose first page was the row before's last kept none of it (a small list each, many to a page). Both were older than the pieces; both are fixed and tested (cards and bags in the fixture), with the rest of the review in `notes/netcode.md`, top section.
- **Kept by blocks** (2026-11-15, later; `notes/netcode.md`, Projection hashes kept by blocks): what a row, a piece or a memo's container read is kept by its blocks, with each one's bytes hashed beside its page's hash, so a page that changed for another chunk's grid directory, grid chunk or policies has those blocks hashed again rather than its rows read; a slot's page that changed has its rows' snaps looked at; the directory of groups is copied a chunk of its list at a time; and a piece keeps what its rows add to each player's sum. Hashing with 8 bots moving went from 18.1 ms a second to 9.5 (medians of three alternating runs; standing still 1.56 to 0.80), update median 1.08 to 0.96 ms and p99 6.48 to 5.71; pieces read for a heap page 15,874 to 131 over a run, rows read from the heap again for their pages alone 233k to 159, grids encoded again 14k to 4.5k. `tidec_e2e_visibility_blocks` changes a block beside a neighbour's in its page, one let go and another made where it was, and the directory packed.
- **The rows that changed, and big containers by their chunks** (2026-11-16; `notes/netcode.md`, top section): a piece keeps a copy of its columns' runs beside their hashes, so a run that changed says which of its rows did, and in which component; only those rows are looked at, and the piece's arrays stand while none of them reads the heap again (a chunk whose `Seen` changed moves its term where it is). Who may have a row is worked out again only where its scene, its Visibility or which components it has changed. A map, set or list too big for one chunk of its list hashes as a tree of its chunks (a map as the sum of its entries' hashes, a list as its chunks' hashes in order), each chunk's part kept while its page hashes as it did: a dimension's `Streamer.chunks` costs the chunks that changed (about 4 µs each, two of every five of them a time) where it cost 105 µs whole. A grid's cells go a chunk at once (3.7 µs a chunk encoded to 1.1). Hashing with 8 bots moving went from 9.40 ms a second to 6.95 (medians of three alternating runs; standing still 0.66 and 0.64), update median 0.91 to 0.82 ms and p99 6.07 to 5.46, a client's hashing 0.79 to 0.59; rows looked at in pieces read 533k to 280k over a run. No pin of this game moved; a container more than a chunk hashes to another number (`visibility_hash_memo`'s pin). `tidec_e2e_visibility_trees` makes each change that a kept chunk or an unlooked-at row could miss.

### 3. The server's store (`tide/storage.h`, `engine/src/storage.c`, `compiler/src/storage_check.inc`; off the tick's thread, listing and awaiting, 3a to 3d; a hosted match's, 3e)

Nothing let match state reach a disk on a server: file calls are local code's, and a dedicated server runs none. Spec: The server's store.

- `Storage.Save`, `Load`, `Delete` and `List` compile to `tide_storage_ask(world, kind, key, bytes, after, to, target)` (through the generated `tide_storage_save`, `_load`, `_load_to`, `_delete`, `_list`, `_list_to`, `_list_after`, `_list_after_to`), which does nothing unless the world is bound to a `tide_storage` (`tide_storage_bind`): only a server binds, around its own run of a tick (`tick_run`). They're checked like the visibility directory's calls (`CALL_VISIBILITY` with `VISIBILITY_STORAGE`): match code and its helpers, and the code keeps its order (`calls_c`). An argument of the wrong type is "Storage.X takes its <name> as <type>, not <type>" (2026-11-15: a wrong type reached the C compiler before).
- After the tick (`tick_end`) the server hands what it asked over (`tide_storage_send` onto its `tide_storage_line`, `s->stored`): a batch, carried out on the provider's own thread where it has one (`tide_io.post`) and else at once, as before (2026-11-15; section 3a). Saves and deletes write; each load's and listing's answer is a `TIDE_STORED_MESSAGE` (`tide_stored`: kind, whom it's for, target, a number, outcome, key or prefix, then a load's bytes or a listing's `after`, `more` and keys).
- Answers go in as shard messages do (`inject_taken`, `dispatch_injected`, `takes_messages`/`take_message` wherever the session asked `g->shards`): in the frame or by download for a game without visibility, in the journal, in a replay, 64 a tick. `receive_stored` takes the ready batches' answers into the server's queue at the tick's end and again as the next tick begins, in the order they were asked. `tide_game.stored` (generated) sends `Loaded` or `Listed` with a cause of its own (the target and the answer's number), or puts an answer a match task waits for (`TIDE_STORAGE_TO_TASK`) into its frame (section 3d).
- `Loaded` and `Listed` are Tide source the compiler adds to a program that says `Storage.Load` or `Storage.List` (`storage_prelude(wanted)`, `storage_prelude_wanted` gives which, as `Web.Request`'s types are), so a game without them has no such events, and the language server knows them.
- **Hosts**: `tide_session_desc.io`, `tide_shards_desc.io`, `tide_hosting_desc.io` pass a `tide_io` down. The standard host makes the game's own files the store (`tide_run_bind_io`, `tide_run_store`): in a window's run always, so a reloaded build that first says `Storage` keeps what it saves; in a run with no window only for a game that uses files or the store, as the provider makes the game's folders at once and a test should leave none. `tide_serve` does for a dedicated server whose game uses the store (one that first does after a reload keeps nothing until the server starts again), naming a game without `appId` `dev.tide.<pack name less "-server">` as the player's build is named (`tide_run_name_game`), so a world hosted by a player and then served is the same folder. The bench keeps them in memory (`bench_desc.io`).
- **Files** (`tide_storage_path`): `saved/<key>.bin`, every byte but `a-z0-9-_` written `%XX`, `.` too except in the last part after its first byte (no folder ends in `.` or is another key's `x.bin`), a part named as a Windows device (`con`, `nul`, `com1`... before its first `.`) with its first byte written out, and a key with a part longer than 200 bytes so written as `saved/%%/<SHA-256>.bin`. The review found `a./b` and `a/b` one file on Windows, `x.bin/y` a folder clashing with key `x`, and `con` refused on Windows only.
- **Saves are atomic** at the provider's level: `<path>.new`, then `io->move` over the key's file. A delete removes the key's file. After a batch that changed anything, `flush_begin` and `flush_forget`: the provider commits on its thread, nobody waiting. A hashed key's save writes the key itself first, beside it (`saved/%%/<hash>.key`, also through `.new`), and its delete removes the value first and then the key file: a key file alone lists nothing.
- **Outcomes**: `TIDE_STORED_FOUND`, `MISSING` (the provider said not found) and `FAILED` (any other error, or a value past `TIDE_STORAGE_VALUE_MOST`), as `Loaded.found`/`.failed`. The log says which key failed and why. Before, a read error was "not found", and a game would make the chunk afresh and save over what it couldn't read.
- **Start and preparation**: the server binds the store around `g->start` (Spawned handlers of the match's start) and `tide_preparation_update_server` around a scene's preparation, whose requests are taken into the server's when the preparation is adopted; all are served with the next tick's. A host that runs the start itself on the world it gives the server (a sharded world's root in `shards.c`'s `start`, the bench's `make_world`) binds a `tide_storage` of its own around it and hands it over as `tide_server_desc.asked`, which `tide_server_create` takes into the server's before anything it asks again (2026-11-15, after review: a sharded world's `Spawned` saves, deletes and event loads went nowhere, as nothing was bound there).
- **Host migration is refused** for a game whose table has `stored` (`hands_over`, `can_migrate`, `tide_session_take_over`), and `hostMigration = true` beside `Storage` is a compile error: the store and the answers on their way are the hosting machine's.
- **Rewinding** (`tide_session_rewind`) drops queued answers (`tide_server_drop_injected(..., TIDE_STORED_MESSAGE)`), those of batches still being carried out as they come (`tide_storage_forget`: what those batches save and delete is done), and the requests not yet handed over.
- `tide_server_injected` counts shard messages only: answers waiting for a tick held up a shard's acknowledgements, reshapes and checkpoints (`drained`) while loads were asked every tick. A checkpoint doesn't carry them (spec, Open), nor batches on the files' thread.
- A hot reload (`commit_server`) publishes queued messages again: what was published of them went with the old build's frames. Batches still being carried out are taken by the new build as they come; a build without the store drops them (`stored_answer`), as nobody would dispatch them.
- **Language server**: `Storage.Save/Load/Delete/List` hover (every form of a call, as the occurrence doesn't keep how many arguments it had), signature help, parameter names (`key`, `bytes`, `entity`; a listing's `prefix`, `after`, `entity`, the second by the type the checker read it as) and completion in match code; `Flush` in local async code as before. Hover on `Listed` shows its fields.
- The `Loaded` prelude is added where a file's code has `Storage`, `.` and `Load` as words, spaces and line breaks allowed between them, not in comments (`storage_prelude_wanted`), and `Listed` where it has `Storage`, `.` and `List`.
- The 64 answers a tick is what paces a view's worth of new chunks: 845 chunks are asked for in one tick and made over 14.

### 3a. Off the tick's thread (2026-11-15)

The files were read and written in `tick_end`, on the thread that ends the tick (the session's own, which draws the frame, where ticks run alongside views). A player's first view asks 845 loads in one tick.

- **The provider's thread**: `tide_io` gained `post(user, work, context)` and `settle(user)` (`tide/io.h`): work run on a thread of the provider's own, in the order it's posted, and a wait for everything posted. The native provider (`platform/src/io.c`, `backend_post`, `post_worker`) has one thread for the process, started at the first post, a list under its own lock and two condition variables (work, done); `tide_platform_io_init` settles before it switches apps (the work uses the provider's folders). The web's has none: a page's files are its own thread's JS. The bench's and the tests' in-memory providers have none, so they're served as before.
- **The line** (`engine/src/storage.c`): `tide_storage_send` moves the tick's `tide_storage` into a batch (numbering its loads and listings from `line.next`, so numbers are what they were), appends it, and posts `carry_out`, or runs it there and then without `post`. `carry_out` serves the batch into its own answer list and sets `done` (release); the provider's thread touches nothing else. `tide_storage_receive` takes the first batches whose `done` it sees (acquire), in order, stopping at the first not done, so answers keep the order they were asked in whichever finished first. Every batch's requests run after the batch before's on the one thread, so a load after a save, in one tick or later, finds it.
- **Pacing**: answers go to the first tick after they're ready (`receive_stored` in `tick_end` and before `dispatch_injected` in `tick_begin`), 64 a tick as before. A provider without a thread gives the same ticks as before, byte for byte: `bench_voxels --quick --expect 0x1f3c165e43a78b10` is unchanged.
- **A run with no window** (the standard host's `--frames`, `--capture`, `--script`, `--players`), 2026-11-15 after review: its sessions have no clock so that it repeats (notes/standard-host.md), but its store was the platform's provider with its thread, so which tick an answer reached depended on the disk and the scheduler. `tide_run_bind_io` now hands such a run's sessions a copy of the provider with no `post` or `settle` (`tide_run_store_here`, as the bench's `--files-here`), so its requests are carried out as each tick ends and every answer reaches the next tick. `tide_platform_store_headless` (`platform/tests/store_headless.c`, through the real host with the embedded platform and the platform's files in a folder of the test's): 400 values of 2,000 bytes saved, loaded in tick 3, the first answer in tick 4 and all 400 found; with the provider's thread the first came in tick 5 or later.
- **A server that stops** (`tide_server_destroy`, after `land`): what's still in `s->storage` (a start's or a preparation's, never ticked) is sent with `answers` false, which carries out saves and deletes and skips loads and listings, and `tide_storage_finish` waits for each of its batches still on their way and frees every batch. Nothing waits for the flush: a clean stop hands the OS every write, a power cut can still lose what it hadn't flushed.
- **Waiting for one's own** (2026-11-15, after review): `tide_io.settle(user, context)` returns once the work posted with `context` has (and so everything before it on the one thread), or with NULL once everything posted before the call has. `finish` settles each batch of its line that isn't done, so a server that stops waits for its own batches and what was queued ahead of them, not for what other matches posted after (it settled everything, and `tide_hosting_stop` and the shards' updates, which destroy servers on their update thread, held every other match's ticks for the whole queue). The native provider keeps what its thread is running (`posts.running`) and looks for the context in its queue (`posted_still`). `storage_finish_waits_for_its_own_batches_alone` posts work after a line's batch that waits for the test, or 5 seconds: before, `finish` waited the 5 seconds.
- **A batch the provider can't queue** (`post` false: its 24 bytes weren't there, a test's queue is full), 2026-11-15 after review: was carried out at once on the sender's thread, ahead of and alongside the line's batches still queued, so a load could read before an earlier tick's save, an older save of a key land after a newer one, and two `save`s write one `<path>.new` at once. Now the line's batch before it is settled first, then it's posted again, and carried out at once only where that fails too, after everything of its line. `storage_line_a_batch_that_cant_be_queued_goes_after_those_before_it` refuses one post and then two (a load after a save still queued): before, the load found nothing.
- **Measured** (`storage_measure_tick_end` in `tidec_e2e_storage`, release, Windows, the platform's files in a folder, 845 chunks of 300 bytes saved, then 1,690 loads in one tick, 845 of them found, three rounds): carried out where the tick ends, 60 to 67 ms on the tick's thread; handed over, 0.004 to 0.006 ms, the provider's thread taking 57 to 67 ms. `bench_voxels --players 4 --seconds 15 --threads 4 --linger 30 --keep 90` with the platform's files in a folder (`--files <folder>`, the folder's `saved/` emptied first; `--files-here` the same carried out as each tick ends), two alternating rounds: the server's update p99 3.26 and 3.55 ms on the tick's thread, 2.35 and 2.29 ms off it; its longest 15.1 and 14.7 ms, 6.6 and 6.9 ms; the bots' verified worlds at most 40 ticks behind, 28 to 31 off it. Median, simulate and hashing the same.

### 3b. Listing keys (2026-11-15)

`Storage.List(prefix)`, `(prefix, after)`, `(prefix, entity)`, `(prefix, after, entity)`, answered by `Listed { string prefix; string after; List<string> keys; bool more; bool failed; }` (spec, The server's store, Listing keys). `after` is in the event beside `prefix` (the task proposed four fields): with it, two pages of one prefix asked in one tick are told apart, and an empty page still says where it started.

- **The checker** (`storage_check.inc`): 1 to 3 arguments; with two, the second is checked first and taken as `after` unless it's an `Entity` (`check_expr_want_checked(..., true)` then casts it). `bare_list`/`bare_list_at` as `bare_load`: a `Listed` handler that takes components needs the entity of every listing (`check_sends`' end).
- **Codegen** (`gen_storage`): the four helpers, and `tide_game.stored` reads a `tide_stored` (`tide_stored_read`) and sends `Loaded` (`tide_stored_text`, `tide_stored_bytes`) or `Listed` (`tide_stored_keys`, a `List<string>` in the scratch area) for `TIDE_STORAGE_TO_EVENT` answers.
- **What's listed** (`list_keys` in `storage.c`): the folder the prefix's whole parts make (`prefix_folder`: the parts before its last `/` that's a folder's in every key with the prefix, one after a part that isn't empty with a byte after it that isn't another; written as `tide_storage_path` writes `<parts>/x` less `/x.bin`, or nothing to walk where a part of them is too long, every such key being hashed), walked a folder at a time (`walk`: the provider's one-level listings of files, then of folders; a folder is walked only where `<folder>/a.bin` is a key's file, so a folder with a `.` or capitals holds none, and only where its keys can start with the prefix, come after `after` and, once 1,025 are held, before the greatest held: `worth_walking`). The provider's recursive listing isn't used: its paths don't say where the folder asked for ends.
- **Files back into keys** (`tide_storage_key`, the inverse of `tide_storage_path`): `saved/`, `.bin`, bytes of `a-z0-9-_./` as they are and `%XX` in capitals, UTF-8, and then the key written out again must be the very path, so a `.new`, a stray name, small hex digits, a `.` in a folder, a device's name as it is or a `/` written out where it's a folder's give nothing. Hashed keys (`walk_hashed`): each `<64 hex>.key` in `saved/%%` whose `.bin` is there too, read, its bytes a UTF-8 key whose path is that `.bin`.
- **Kept**: a heap of the 1,025 smallest keys past `after` with the prefix (fixed slots of 256 bytes, 263 KiB a listing), sorted at the end; 1,024 go in the answer and `more` says whether the 1,025th was there. A folder the provider can't list (an error but "not found") fails the listing where its keys could be among the answer's: no keys, `more` false, and the log says which prefix. Where they'd all be past the answer's last (the heap was full and the folder's keys start past its greatest), it doesn't, and the page after it does: the least of the failed folders' starts is kept (`failed`; failed folders are apart, as one in another isn't walked) and checked with `worth_walking` once the walk is done, so the answer doesn't depend on the order the provider gave the folders in. The hashed walk's failures fail it whatever the page. A provider without `list` fails every listing; no provider lists nothing.
- **Providers**: the native one's `list` (one level, as before); the web's (`fileList` in `tide.js`, filtered from the page's map of files); the bench's in-memory one gained `store_list` (one level from its table, folders once each); the tests' too.

### 3c. Answers for waiting tasks (2026-11-15)

Requests carry whom their answer is for (`tide_storage_request.to`, `TIDE_STORAGE_TO_EVENT` or `TIDE_STORAGE_TO_TASK`) beside `target`, and so does the answer (`tide_stored.to`). For a task, `target` is 16 bytes the generated code chooses, given back as they were; two awaits of one key in a tick are two operations, so they're asked twice (the dedupe compares `to` and `target`). Everything else is as for events: the line, the pacing, a rewind dropping it, a hot reload republishing it, a stopping server not reading it. The language side is 3d: the target is the operation's own identity, not a task handle's (the suggestion here first), as most tasks have no handle.

### 3d. Awaited loads and listings (2026-11-15; `storage_check.inc`, `codegen.c`'s `gen_stored_await`, `gen_frame_stored`, `gen_stored`)

`var bytes = await Storage.Load(key);` (`List<byte> fails IOError`) and `var page = await Storage.List(prefix[, after]);` (`StorageKeys fails IOError`, `StorageKeys { List<string> keys; bool more; }` in the storage prelude beside `Listed`) in match tasks: spec, The server's store, Awaiting a load or a listing. Claude's picks: the result types, `IOError.NotFound` for nothing kept and `IOError.Failed` for what couldn't be read or listed (where `InvalidData` was the other candidate: the store doesn't look at the bytes), `StorageKeys` as the struct's name, no entity and no handle.

- **The checker** (`check_storage_static` with `c->awaiting == e`): a load takes its key alone, a listing its prefix and `after` (strings both); an entity is "an awaited load's bytes are the task's, so it names no entity" with a note on the event form; an awaited `Save` or `Delete` "gives nothing to wait for". The rest is the statement's checks (match code, `calls_c`, the match heap's writes), then `e->storage_await`, `decl.awaits_store` (1 load, 2 listing) on the async function or handler, and `program.stored_load`/`stored_list` (the result types). No `Loaded`/`Listed` send is recorded, so a load that names no entity beside handlers that take components is fine. `check_await` takes it as it takes `Web.Fetch`. A local task's is "Storage.Load is the match's, and 'F' is this machine's task" where its side is already local; `var asked = Storage.Load(key);` keeps its "doesn't produce a value" with a note to await it.
- **The frame** (`add_task`): `tide_stored_op` (an `Entity`), `tide_stored_kind`, `tide_stored_answered`, `tide_stored_key` and `tide_stored_after` (text), and `tide_stored_load`/`tide_stored_list` (the result's value, error and ok) for the kinds the function awaits. Nothing in a frame of a function that doesn't await the store, so no other game's layout moved (`bench_voxels`' pin is the same).
- **The await** (`gen_stored_await`): the key and `after` worked out in order, `tide_stored_op = {tide_identity_clock, tide_causal_next(&tide_causal, site)}` (an awaited storage call is a causal site, `causal_expr`): the identity a spawn there would have, unique for the match's life and the same on every machine. The request goes into the frame, `tide_storage_ask(..., TIDE_STORAGE_TO_TASK, op)` asks (nothing unbound), and the task returns `INT32_MAX`, no tick to look again at. Once `tide_stored_answered`, the value is reloaded from the frame (a copy), and `tide_stored_done_*` lets go of the request and the answer and clears the op.
- **A key the store refuses** (2026-11-15, after review): `tide_storage_ask` refuses an empty key or one past 256 bytes, a prefix or an `after` past 256, with a line on stderr, and queues nothing, so such a task waited for ever (and asked again, refused again, after every rewind). Now the generated await checks `tide_storage_takes(kind, key, after)` (tide/storage.h, `static inline`, the same bounds `tide_storage_ask` keeps) after asking, and where it's false spills `{.error = IOError.InvalidPath}` into the frame's result and sets `tide_stored_answered`: the task goes on at once with the failure, in the tick it awaited, on every machine, from match values alone. The server's log still says why. `InvalidPath` is Claude's pick (spec, Awaiting a load or a listing). `storage_await.tide`'s `Refused` awaits an empty key, 257 bytes of key, a 257-byte prefix and a 300-byte `after`, then a 256-byte prefix, which is listed; each fails in the tick it started (`refusedLate` 0), on the host and the guest; with the check taken out every one of the await tests fails.
- **Asking again only what it awaits** (2026-11-15, after review): `tide_stored_ask_*` passed the frame's `tide_stored_kind` to `tide_storage_ask` as it was, and a frame from a checkpoint, a shard's message or a world a host gave (trusted, but not made here) that said 1 or 3 would have saved an empty value over its key or deleted it. It asks now only where the kind is one the function awaits (`TIDE_STORAGE_LOAD`, `TIDE_STORAGE_LIST`), and `tide_storage_ask` takes no save or delete for a task, nor a kind that isn't one (`storage_asks_nothing_for_a_task_but_loads_and_listings`).
- **The answer** (`gen_stored`, after the tasks, where `tide_game.stored` is made now): a `TO_TASK` answer builds the value once (`tide_stored_bytes`, `tide_stored_keys`) and walks the tables of tasks whose function awaits the store itself or through what it awaits (`task_awaits_store`), read-only (`tide_stored_waits_*` recurses into `tide_await_*` frames), and only the matching row's cell is taken for writing: `tide_stored_put_*` spills the value into that frame, and the row's `wake` becomes this tick, so the task goes on at its end with the others whose time has come. None found: false, nothing changed (an ended task's, a made-up target). The walk is a table scan per answer, at most 64 a tick; tables of tasks that don't await the store aren't walked. All of it is made only where some table of the match's tasks awaits the store (`awaits_store`): an async function that awaits one but that nothing starts has no table, and its program's `tide_game.stored` is the events' alone (`storage.tide`'s `Size` keeps that building; it didn't, as `tide_due` is the tasks' and wasn't there).
- **Asked again** (`tide_game.stored_again`, generated where a task awaits the store): the server calls it with its store bound after `tide_session_rewind` (whose answers on their way are dropped) and after making a server from a given world (`tide_server_desc.world`), and each waiting frame asks again from what it keeps (`tide_stored_ask_*`), in the order the tasks started. A sharded game's task that arrives with its entity (`tide_shard_take_tasks`) asks again there; `dispatch_injected` binds the server's store around the messages it takes before a tick, so the ask goes with that tick's requests. The answer to the shard it left finds no task.
- **Language server**: after `await Storage.`, in a match task, `Load` and `List` with their awaited forms (`storage_awaited_calls`); hover shows the awaited forms first where the call is awaited (its occurrence's type is the failable result), and after the others where it isn't; signature help has both. `StorageKeys`' fields hover from the prelude.

Tests: `tidec_e2e_storage_await` (`storage_await.tide`, nine tests): a host with files and a guest without over a lossy loopback, with requests carried out at once and every fifth frame: loads found (twice in a tick, as two tasks), missing, unreadable, by download (5,000 bytes), saved and awaited in one tick; listings page after page (1,100 keys), one that fails; a load through an async function that fails with `IOError` (`try`), after `Wait.Ticks`, after a save the task made; an async handler's load and listing; a task whose entity is destroyed in the tick it asked. Both worlds have every count and an order-sensitive hash of the keys, and neither resyncs; the same with a guest that joins once every task waits, with nothing carried out until then. A world packed while every task waits unpacks to the same hash, and rewinding to it asks again, once each: the reads after it are exactly the eight waiting loads and the two Deep asks after its first answer (it fails with the re-ask taken out). A server made from a world whose tasks wait (`tide_session_start_from`, after the first server stopped) asks again what each waits for, the same ten reads, and every task is answered (it fails without `stored_again` in `tide_server_create`). A replay of the match's journal (`tide_session_record`), answers carried out every fifth frame, makes every tick's world again (`tide_replay` checks each hash) and the same final world. A hot reload while they wait, new code of the same layout and then another layout carried over by `tide_migrate_world` where the platform is linked (`STORAGE_MIGRATION`, the test's own game layout, no task dropped) and else by copying (each old table zeroed), leaves every task answered once. An answer for no task is refused and the hash stays. Without files every load is `NotFound`. `Hold`, whose entity goes in the tick it asked, counts in `Vault.held` if it ever goes on (it was its entity's field, gone with it, so the check was empty). Keys the store refuses fail at once (above). `tidec_e2e_storage_await_shards`: eight movers whose tasks ask on the left cross to the right before anything is carried out, and each gets its answer there, once (it fails without the binding around the messages); the root's start saves a key, which is kept (it fails without the binding around `start`); a world saved (`tide_shards_save`) while every task waits and loaded again asks again, and the last mover is answered on the left long before it crosses (it fails without `stored_again` in `tide_server_create`); a world recovered from that checkpoint and the journal since, in whose ticks the answers came, has every shard's hash and every answer, and asks nothing again. `tidec_e2e_storage_await_hidden` (`storage_await_hidden.tide`, a game that hides state, 2026-11-15): three awaited loads and a listing on a host with files and a guest, answers carried out every fourth frame; the server's tasks have their answers, the guest's verified world has what they changed, and the files the match saved are there. Nine `tidec_error_storage_await_*`, `lsp_storage_awaited_completes_and_hovers`.

### 3e. A hosted match's store (2026-11-15; `tide/storage.h`'s `tide_storage_host`, `tide/managed_run.h`)

Managed hosting gave its servers no `io`, so a hosted match kept nothing. Now the service keeps it (its side is `Tide-Engine/tide-services`, `hosting/kept.mjs`, notes/managed-hosting.md there). Spec: The server's store, A hosted match.

- **A host that keeps the store itself** (`tide_storage_host`, `tide_storage_line.host`, `tide_server_desc.store`): a line with one hands each batch to `keep(user, requests, number)` as it's sent (in `tick_end`, and as a server stops, with `answers` false: only its saves and deletes, copied into a store of their own), and the host carries them out in that order. Loads and listings are numbered as before (`line.next`), and the host answers each once, in any order, with `tide_storage_answer` (`tide_server_store_answer` for a server's, which lands any tick in flight first). A batch with something to answer is on the line before `keep` is called (a host may answer inside it), with a slot for each answer and copies of its loads and listings alone (slot k's request at k: what an answer is checked against and made from), and is done once every slot has one; a batch with nothing to answer isn't put on the line. The tick's `tide_storage` is the host's for the call and freed after it. The host answers every number it's handed (`FAILED` where it has nothing else to say): no tick takes an answer past one still to come, so one never answered holds back every later one for good (tide/storage.h and managed_run.h say so). `tide_storage_receive` takes done batches in order, so answers keep the order asked whichever came first. `forget` (a rewind) marks a host's batches done and their answers refused; `finish` frees them without waiting, as what they save is the host's once handed over.
- **What's checked of an answer** (`tide_storage_answer`, `hosted_keys`): the number names a waiting slot (never asked, answered, or forgotten: false); a load's outcome is `MISSING`, `FOUND` or `FAILED`, bytes only with `FOUND`, at most `TIDE_STORAGE_VALUE_MOST`, no `more`; a listing's is `FOUND` or `FAILED` (no keys and no `more` with it), its keys each a varint and 1 to 256 bytes of UTF-8, starting with the prefix, each past `after` and the key before it, at most 1,024, `more` only with 1,024, nothing after the last. The message is made from the request (kind, whom, target, key, `after`) and the answer, with the keys written again (canonical varints), so the host names none of the rest.
- **The reactor** (`managed_run.h`): ABI stays 1; the store's calls are more of it, offered by the service at a version of their own, so a service older than them, whose worker imports exactly `send`, `entropy` and `journal` and whose upload check refuses any other import, runs a new module as before. No new import: WebAssembly has no optional ones, and one the older service doesn't give would fail the module's upload and its instantiation. `tide_managed_store(version)` before the server is made (1, or 0 for version 0 or a server made already); `tide_managed_store_requests()` after each call, the requests since the last as bytes in `tide_managed_bytes()` (now outside the sharded part: u32 count, then u8 kind, u64 id, u16 key, u32 data, little-endian); `tide_managed_stored(id, outcome, data, size, more)` before an update, 1 taken or 0 refused. Ids are `f64`, as a coordinator's match is: the server's number from a base, the last id given when the server was made (`tide_managed_store_began`), so an answer to a server a restore replaced names nothing of the new one. Every server the reactor makes has the store (`init`, `restore`, a shard's), and a shard's start is bound to a store of its own and handed over as `tide_server_desc.asked`, as `shards.c` does (it went nowhere before).
- **Recovery**: the service recovers a match with `init` then `restore`. The server `init` made ran the match's start, whose requests wait for its first tick; `restore` destroys it with `tide_managed_dropping` set, so what it never handed over is dropped (its start's saves would write over what the match kept). The restored server asks again what its tasks await (`stored_again`, in `tide_server_create`), with its first tick.
- **Tests** (`tidec_e2e_managed_store`, `managed_store.tide`, the reactor built natively with `TIDE_MANAGED_TEST`: no import or export attributes, and the weak game hooks as static pointers, as Mach-O has no undefined weak symbols): a fake service that carries requests out as it takes them and gives answers three updates later, last first: saves, loads found, missing and failed, a delete and a save before loads of their keys in one tick, a listing of a folder and one of 1,030 keys in two pages, the `Loaded` keys in the order asked, and tasks' loads and listing; a match checkpointed while its tasks wait, then a new instance's `init` and two `restore`s: exactly the tasks' two loads and a listing asked again, no start's save, the host's values the match's; a host that offers no store (every load missing, every listing empty, `tide_managed_stored` refused); 26 answers that aren't ones refused (ids 0, negative, NaN, a fraction, past the last, 1e300; a load's outcome 3, bytes beside `MISSING` and `FAILED`, `more` 1 and 2, NULL bytes, 32 MiB and a byte; a listing's keys unsorted, outside the prefix, twice, not UTF-8, cut short, empty, 257 bytes, 1,025 with and without `more`, `more` with two, keys and `more` beside `FAILED`, `MISSING`), then the right ones, each taken once and refused the second time, and the match as without them; an id of a server a restore replaced, once the new one's tasks asked again with ids of their own; and the line alone (answers out of order, a batch with nothing to answer, forget, a stop's saves alone, finish). Without the drop the recovery test fails three checks; without the ids' base the stale-id test does. A wasm32-wasip1 build of the fixture by hand (the engine's sources, the reactor, wasi-sdk 34, as `tide deploy` links one) imports `env.memory`, five WASI calls, `tide_host.entropy` and `tide_host.send`, nothing else, and ran under the services' worker as it was (no store): 30 updates, nothing handed over, a save of 26,873 bytes. Under the service's store (`tide-services`, `hosting/kept.mjs`, by hand with its `Matches`): 1,050 requests, 9 answers found and 4 nothing, none refused, the 1,030 page keys kept, `start` holding the match's 9 and not its start's 1, and the same after a failure and recovery (`init`, then `restore`). That run found a service bug (keys from its worker thread are `Uint8Array`s, so a delete missed its key), fixed and tested there.
- **Corrections (review, 2026-11-15)**: a hosted batch kept the tick's whole `tide_storage`, saved values and all, until its last answer came and every batch before it was received: a tick that saved 845 chunks and loaded one held the 8 MB through the service's round trip, in the match's bounded memory. It keeps copies of its loads and listings alone now (`send_hosted`, `copy_request`; `asked` is gone). Nothing said a host must answer every number, and the service had a path that dropped a load unanswered (its database refusing it), which stopped every later answer for the match: the headers say so, and the service answers everything (its notes). The sharded half of the reactor had no test and nothing here built it, and its journal import was spelled out with wasm's attributes, so a native build warned: it's `TIDE_MANAGED_IMPORT(journal)` now, and `tidec_e2e_managed_store_shards` builds it (below). Not done: a module that fails an id the host never answered after a while, as that's a timing rule, and a host that answers is the contract.
- **Tests, review**: `managed_store_a_host_may_answer_inside_keep` (a host that answers a load and a listing from inside `keep`: the answers are on the line when the send returns, a listing's keys are checked against the prefix and `after` the line copied, one before `after` refused, and the tick's requests, a 4,096-byte save among them, are freed). `tidec_e2e_managed_store_shards` (`managed_store_shards.tide`, `TIDE_GAME_SHARDED` with `TIDE_MANAGED_TEST`): a coordinator of a one-region world gives the root's start, `tide_managed_shard_new` makes the shard, its start's save is handed over with its first tick (it fails with `d->asked` taken out); its world packed at tick 3 and the journal since, in whose ticks the answer to an awaited load came; a new instance's `shard_stage`, `shard_replay` of every record and `shard_begin` has the same tick and hash, asks again for the one load still held and not for the answered one, and its task is answered there, once. A tick's answers are given together (a held load holds its tick's others), so the fixture asks the two in ticks of their own. The reactor compiles for wasm32-wasip1 with `-Wall -Wextra -Wpedantic -Werror` for both fixtures' headers (wasi-sdk 34's sysroot), its journal import still `tide_host.journal`.
- **Gaps**: the service's answers are paced as files' are (64 a tick), so a world's first view of 845 loads waits for them over 14 ticks, plus the service's reads. A shard's saves in ticks a restart replays from its journal aren't asked again, so those the service hadn't written when it stopped are lost, as for a native shard's files. A world's store and its checkpoint are written apart; since the review the service records a normal match's checkpoint only once everything asked before it is written, so a match recovered from a save can find its store ahead of its world (as with a dedicated server's folder), never behind. A shard's journal is synced every step, so the same fence would put the store's disk on every world step: not done.

### 3f. A load of a key never saved reads nothing (2026-11-16, night; `engine/src/storage.c`, The files a provider has)

Every chunk a player comes near is a load, and nearly all of them find nothing (What's left, 6: 567,041 loads in 20
minutes of 8 bots, none found), each a file looked for: 18 µs on the files' thread, or on the tick's in a run with no
window.

- **What's kept**: for each provider (its `user` and `read`, so the copies hosts make of a `tide_io` are one, and
  every server and shard of a process that share a folder share it), the number each file's path under `saved/`
  hashes to, letters small (a disk may take `A.bin` for `a.bin`). Listed once, folder by folder (the provider's
  one-level listings, as a listing of keys walks them), by the first load, on the thread that carries it out: the
  provider's own where it has one. Each save adds its path once it's written, whether or not the write worked. Deletes
  take nothing out: such a load is read and finds nothing.
- **What it answers**: a load whose path's number isn't there has no file, and is `MISSING` with no read. Any other
  load is read as before, so `FOUND`, `FAILED` and a `MISSING` the disk says are what they were. A provider with no
  `list`, or one a folder of which couldn't be listed, is read every time, as is one being listed by another thread.
  Answers, their order and their ticks are the same: every pinned world is where it was.
- **The rule it adds** (Claude's pick, Provisional; spec, The server's store): while servers run, `saved/` is the
  store's to make. A file put there by anything else (the game's local file calls, a person, another process on the
  same folder) may not be found until the process ends. A host that changes a provider's files or folders calls
  `tide_storage_forget_files` (the platform does when its app's folders change; the storage tests' host does as it
  empties its files), and the next load lists again. The owner's to confirm: the other way is the read for every load.
- **Measured** (release, Windows, 4 threads). The default run (8 bots, 30 s, the bench's store in memory): 14,551
  loads, 14,551 reads before and 49 after, all 49 found; the same hash. With the platform's files on the tick's
  thread (`--players 1 --still 100 --hop 3 --builders 0 --files-here <folder>`, 20 s, two runs each): at `--near 4`
  4,018 loads, each a file looked for before and none after, the server's longest update 4.57 and 4.64 ms to 2.14
  and 2.48; at `--near 12` 23,058 loads, 29.8 and 28.3 ms to 13.0 and 12.9 (what's left is the tick a box is spawned
  in: What's left, 5d). `--quick --files-here` from an empty folder ends on the quick run's pinned hash, 1,152 loads
  and 10 reads, and again over the 9 files it left.
- **Tests**: `storage_loads_of_keys_never_saved_read_nothing` in `tidec_e2e_storage` (the host's reads and listings
  counted: 205 loads of keys never saved read nothing after one listing; a key saved through the store is read from
  then on and its neighbours still aren't; a deleted one is read and gone; a file whose name differs by case is read;
  a copy of the provider is the same provider; a file put behind the store is found once the host says its files
  changed; no `list`, or a folder that can't be listed, reads every time). `tidec_e2e_storage` (17 tests),
  `_storage_await`, `_storage_await_shards`, `_storage_await_hidden`, `tide_platform_store_headless` and
  `bench_voxels` with its five variants pass. Not run: ASan and the web for this part, and the managed store's tests
  (a hosted line has no provider: nothing changed for it).
- **Left**: a store of very many files lists them all at the first load (one-level listings of every folder, once);
  what's kept is 16 to 32 bytes a file. A game that wants files dropped into a running server's folder to count has no
  call for it (`tide_storage_forget_files` is the host's).

### 4. Grids as bytes (`tide/grid.h`, `engine/src/grid.c`; `check_grid_from_bytes`, `gen_grid_bytes`)

The game saved a chunk with Tide loops over its 4,096 cells (`Pack`: runs of one block, a count and the block, at most 255; `Unpack` the other way), which every game with a grid to keep would write again. Now `cells.ToBytes()` and `Grid3<Block>.FromBytes(bytes)` (`Grid3<Block> fails IOError`) are built in: spec, Grids, Provisional, Grids as bytes; how they're built is in `notes/language.md`, Grids as bytes, and what keeps them safe in `notes/memory-safety.md`.

- **The format** (1): `'G'`, `0x10 | dims`, varints of the cell's bytes, the size (0 open) and the chunks that follow; each chunk its place less the one before's (zigzag varints, chunks in z, y, x order) and its cells as tokens, `(t >> 1) + 1` cells of one repeated (`t & 1`) or as they are. Only chunks with something in them; runs of 3 cells or more of a byte (2 of anything bigger) as one. What it writes is a function of the cells alone: chunks sorted (a merge sort: places come from bytes), cells repaired as `FromBytes` would read them (generated `tide_grid<k>_repair`), cells outside a sized grid's size zero, chunks of zeros left out.
- **The game**: `Idle` saves `chunk.blocks.ToBytes()`; `Fill` takes `Grid3<Block>.FromBytes(loaded.bytes) is Grid3<Block> saved` and gives the field `saved.Copy()` (a field takes a local's grid only by copying it: spec, Grids), else makes the chunk from the seed.
- **The pinned hash moved**, `bench_voxels`' and `bench_voxels_checked`' `--expect`, from `0x0ae800b4105e3290` to `0x1315d17fc05df405` on its own branch, and with the game's review fixes merged (The game, above: `Expire`, `Keep`, `Retry`, `linger` and `keep` lower in quick runs) to `0x9ee66673f5fb0bf4`, and with heaps per owner (section 5) to `0x1f3c165e43a78b10` (bench/CMakeLists.txt): the one chunk the quick run loads again gets a new grid in its field (`tide_grid_set`: the old directory released, a new one and its chunk made) where `Unpack` wrote cells into the grid it had, so the heap's blocks are elsewhere. The cells are the same. The same source with the old loops and the new compiler still gives the old hash; the new one is the same on 1 thread and 12, and with every projection hash worked out twice (`bench_voxels_checked`).
- **What a chunk is saved in** (4 bots, 20 s, `--players 4 --seconds 20 --threads 4`, 3,215 chunks, 25 changed by players): 10 saves, 297 bytes (30 a chunk) with `ToBytes`, 434 (43 a chunk) with `Pack`. A chunk of air is the header alone (7 bytes), a chunk of one block 13 (`Pack`: 34, 17 runs of 255 or fewer); for a layered chunk of ground with a cave the two are alike (414 and 399 bytes: a run of 65 cells or more is a token of two bytes and the cell, where `Pack`'s is a byte and the block up to 255), and for blocks mixed cell by cell `ToBytes` is half (3,479 and 6,660).
- **What it costs** (`tidec_e2e_grid_bytes`' `grid_bytes_measure`, release, Windows, a system running each 400 times, the tick of nothing taken off; the machine was shared, so a few runs' middle):

| a 16-cube of `Block` | `Pack` (Tide loops) | `ToBytes` | `Unpack` (Tide loops) | `FromBytes` |
|---|---|---|---|---|
| mixed cell by cell | 130 to 160 µs | 4.5 µs | 50 to 65 µs | 1.7 to 2.2 µs |
| ground, layers and a cave | 45 to 55 µs | 3.1 µs | 34 to 40 µs | 1.8 µs |

  About 15 to 35 times quicker each way. A save costs a server a few microseconds a chunk, and a load as much again for the field's copy of what was read.

- **A field given the grid read** (2026-11-15): `Fill`'s `chunk.blocks = saved.Copy()` made the grid in the scratch area (`FromBytes`), copied it there again (`Copy`), then into the world's heap (`tide_grid_set`). A field given `x.Copy()` now takes `x` itself where it's a grid code made (`tide_grid_set_copy`, generated: `tide_grid_set` copies what it's given into the field's heap and leaves it as it was; a world's grid, the pack's, or a place with no heap still copies first), so the bytes are read once and copied once whichever of the two ways `Fill` writes it (a field inside a list's, a map's or an array's element given `x.Copy()` still copies twice: notes/language.md). The heap's blocks come out as they did (the copy has the same records in the same order), so `bench_voxels`' pin stands. `grid_bytes_measure` (each of these twice, the second said: a field's old blocks are the heap's again only after the tick, so the first grows the heap; four runs, the machine shared): `is` and `Copy()` 2.4–2.7 µs a 16-cube to 1.7–2.2, the same as `?? Grid3(16, 16, 16)` (1.7–2.0, unchanged), of which `FromBytes` is 1.55–1.85. Reading straight into the field's heap would save the last copy, 0.2–0.3 µs: a reader into a heap (`engine/src/grid.c`), with its failures giving back what it made, and a `field = FromBytes(...)` that decodes into the field's heap and takes the grid as it is. Not done: it saves a tenth, and its blocks would come before the old grid's go, moving heap layouts and the hashes pinned on them.

### 5. Chunks made on every core (`compiler/src/check.c`, `parallel.c`, `visibility_check.inc`, `visibility_codegen.inc`, the client codegen)

A game that hides state kept every owner in the match's one heap, as what goes to players read containers through heap 0 only, so `Generate` ran its chunks one after another. Now (schedule-and-threads.md, top section, has the whole of it):

- Owners have heaps of their own and runs as in any game (`assign_heaps`: only shards keep one heap).
- What goes to players reads each value wherever its handle says: a page the hashes are kept by is its heap (id and run) and its place there (`tide_client_place`), and a projection, a reload and a repair make each value in its owner's heap (netcode.md, top section).
- `Generate` ends with `Visibility.Set(seen.group, seen.audience)`, which writes the directory in the match's own heap. A per-entity system's own `Set` no longer keeps its rows in order: on threads they log what they set and the system's end sets it in the entities' order (`tide_visibility_set_logged`), which is what one thread setting each comes to, as nothing reads a group's audience back.
- `tidec --schedule`: `Generate` was "on one thread: it changes the text, lists or grids of Chunk, which are in the match's one heap"; now "across threads: the visibility groups' audiences its entities set are set after them, in their order".
- Measured (8 bots, 12 threads, four alternating rounds, median): server simulate 29.8 to 26.6 ms a second, update p99 10.1 to 8.2 ms (median 1.38 to 1.40), snapshot 1.5 to 2.7 ms a second, a client's simulate 18.5 to 20.8; what's sent and repaired the same byte for byte. `Generate` alone, one thread against the pool in the same build (the test game `visibility_runs`, 40 chunks a tick): 1.32-1.58 ms a tick to 0.34-0.44.
- Pins: `bench_voxels --quick --expect` `0x0ae17b8a639f1fe3` on its own branch (was `0x0ae800b4105e3290`; a world's hash covers each heap's pages, and the values are in other heaps), the same on 1, 4 and 12 threads, checked and memo off; `0x1f3c165e43a78b10` merged with the game's fixes and grids as bytes (section 4), the same on 1 thread and 12. Projection hashes didn't move.

### 6. Things found on the way

- `Storage.Load` beside `Loaded` handlers that take components follows the events' rule (every load names an entity), checked in `check_sends`.
- The first game's `Idle` counted in every chunk each tick; see above.

### 7. A local task writes its chunk's local fields (`check.c`, `codegen.c`; `notes/language.md`, Local tasks use local fields)

`Remesh` couldn't put a chunk's mesh in the chunk's own `local` fields ("a task can't use local fields yet"), so it left each in a local singleton's list (`Meshing.ready`, with the chunk's `Entity` and the frame it came in), which the chunk's view searched every frame and a `Sweep` view aged out when the chunk went first. Now a local task reads and writes the local fields of the components it takes, got again with the component each time it goes on, and `play.tide` is 35 lines shorter without the list, its struct and `Sweep`: the view starts `Remesh(chunk, chunk.stamp)` and keeps its handle, the task awaits `Faces(chunk.blocks)` and sets `chunk.mesh`, `drawn`, `blank` and `meshed`. A chunk that leaves this machine while its faces are made takes its task with it, and the worker's result is dropped. The rules (gone, hidden, Forgotten, rollback, predicted and verified, hot reload, the match over) are in the spec's Local state, Local tasks and local fields (Provisional).

Checked: `tidec bench/voxels/voxels.tide bench/voxels/play.tide` and the `voxels` target build; `voxels --frames 300 --capture` draws 247 chunks with none waiting, and a script that digs four blocks in front of the player (`--script`, captures before and after) shows the hole in the remeshed chunk.

## Far regions and faces across chunk edges (2026-11-15)

Game code only (`bench/voxels`); no engine change. The two gaps this closes were "far terrain is the seed's only" and "a chunk's faces at its edges are all drawn".

### 8. Far regions: what players built, past the chunks (`voxels.tide`, `play.tide`'s `Beyond` and `Ground3`)

- **What one is.** An entity per region of 8 by 8 chunk columns (128 by 128 cells): `Far { int2 at; int dimension; List<Column> columns; int version; }`, public within its group, and the server's `Seen` (as a chunk's: group, audience, shown, emptied, saved, asked) and `Summary { stage; columns; edits; queued; waiting; }`, both restricted to `Audience.None`. A **column** is 8 by 8 cells, the far ground's own square (`FAR_STEP`), 256 to a region: `Column { byte at; Block top; int height; }`, the highest cell that isn't air there (water counts, as the far ground's sea does) and its block, the first in z-then-x order where several are as high. A region's `columns` are only those unlike the seed's, in order of `at`: a region nobody built in has none. Picked 8 by 8 cells rather than a chunk column's 16 by 16 so a summarised column takes the place of exactly one far square; "coarse" is the highest block of 64 cell columns.
- **Who has it.** Regions are entities of their own because their audience is wider than their chunks': a component can be restricted further than its entity, never wider. `Watch` (`Regions`) keeps a square of `Worlds.far` (6) regions around each player's region, never fewer than `(near + 7) / 8` (`FarOf`: the regions a box of `near` chunks each way can reach, so every chunk a player has is in a region they have; after review, as `--near 9 --far 1` left a box's outer chunks in regions nobody's square held, and their builds reached nobody), as it keeps the chunks' box: regions the square left lose the player, those it took in get them, made where there's none (hidden, `Asked`, `Storage.Load(FarAs(...))`). A region's group has the audience only while it has columns (`shown`), so a world nobody built in sends nobody a region. Regions are spawned and asked for after the chunks of the same `Want`, so the chunks come from the store as they did: asked first (so that a region was loaded before its chunks could change), they made the chunks' answers later and moved what players were sent (nobody building, 176.4 KB a second from the server before, 176.8 with the regions asked first, 176.0 with them after). A region that isn't loaded yet keeps what its chunks tell it instead (below).
- **Kept up to date.** `Note`, a server-only `Change` handler declared after `Apply` (handlers of one event run in declaration order), calls `Summarise`: where the chunk's edits are one past what its region last heard (`Seen.summed`), the quarter of the chunk the changed cell is in, else all four, as `Top { Part part; }` events to the region found by an indexed loop (`Far.at`, `Far.dimension` as one key). `PartOf` is a quarter's highest block in the chunk (`QuarterOf` makes the `Part`). A region not loaded yet keeps the parts (`Summary.waiting`), one a quarter of a chunk (its square and floor: a later part replaces the earlier, as it says all that one did; after review, as the list grew with every change while a region's load kept failing), and takes them once it is (`Recall`). `Take` works the column out from what the region has (`was`, or the seed's `SeedColumn`, 64 `Ground`s): a block above the top raises it; a top dug out of its chunk is looked for below (`Beneath`: the chunks the server has, down at most 8; under one it hasn't, the seed's top if that's in or below it, else that chunk's ceiling as stone); a change below the top changes nothing. A column like the seed's is dropped, any other kept; a change puts the region in its dimension's line (`Queue`, `Streamer.line`, once: `Summary.queued`). A chunk loaded with players' changes (`Fill`) is summarised once too, so a region saved before its chunks is put right. **Saved after them** (review, 2026-11-15) a region can have what no chunk has: the two save on unrelated ticks, so a server that stops after a region's save and before its chunk's (or a chunk whose bytes don't parse) gives back a region with a build, or a pit, that the chunk, made from the seed, hasn't; `Take` can't lower a top from a lower chunk, and nothing told the region of a chunk made from the seed, so every player in range saw it for good. Now a chunk `Generate` makes tells its region the tops of its quarters where the region (loaded) has columns over them (`Survey`, from the generator itself: it reads the regions through a query and sends `Top`, and stays across threads), and a region as it's loaded looks at each of its columns again against the chunks the server has between its top and the seed's (`Recheck`, after the parts that waited), then shows what it has whole. A column whose top is in a chunk the server hasn't stays as saved until that chunk is made. `bench_voxels_forgets` (`--forget 1`: the bench's store keeps the regions' saves and none of the chunks') fails with either half taken out (2 and 6 columns wrong at the audit's ticks, 8 with both). The polling first written (a system over every chunk each tick, looking for `summed != edits`) cost the server about 2 ms a second with nobody building; the handler costs nothing where nothing changes.
- **Paced.** `Publish`, on the dimension's entity, every `Worlds.gap` (30) ticks: the regions in the line in the order they changed, `Follow` making each `Far.columns` more like `Summary.columns` by `Worlds.pace` (64) column changes at most in all; a region left part-way stays first. `version` counts sendings that changed something. A region is shown once it has columns and hidden again once it has none left to send (`far.columns.count > 0 || (seen.shown && !all)`; after review: a removal sorted before an addition could take the last of the budget, leave `Far` empty for 30 ticks, and hide a region that had columns throughout, which every player in range lost and got whole again; no bot run makes that case, so it's the audit's hide check, below, and reasoning, not a test that fails without it). So what's built far off reaches a player at most twice a second, in one correction each time, of 64 columns at most, however much is built. Each sending is a correction of every player in range (a client can't predict a server-only change), which costs a few hundred bytes whatever it carries, so spreading the regions over ticks (each region at most every 30 ticks, 4 columns a tick, the first version) cost more than sending them together. Arrivals aren't paced: a region coming into a player's square, or loaded, comes whole, as a chunk does.
- **Kept.** `Tend` is a chunk's `Idle`, `Keep` and `Retry` in one system for regions (asked again after `RETRY`; `Expire` after `linger` with nobody; saved every `keep` while held, each in a tick of its own); `Forgo` saves one whose columns changed since and destroys it, `Faded` takes it out of `Streamer.regions`. Saved as `far/<dimension>/<x>_<z>` = `Json.Write(summary.columns).ToBytes()` (a column is about 36 bytes); `Recall` reads `Json.Read<List<Column>>`, puts the columns in order with each place once, takes what waited, looks again at the chunks (`Recheck`, above), and sends what it then has whole. Bytes that aren't such a list are an empty region, later saved over.
- **Drawn** (`play.tide`). `Beyond` reads the `Far` rows of its dimension (a view's query) within 7 regions of the player's chunk's, which the far ground's 768 cells each way can reach (after review: the hosting machine has every region of the match, and made its far ground again for a build 5,000 cells off): their `version`s, as one number (`Math.Hash(at, version)` summed, those with no columns left out), say when to make the far ground again; it's handed those columns as a `Map<long, Column>` by `SquareKey`. In `Ground3` a square with a column is drawn as one: its top at its height, and walls from there to the seed's ground at its corners, up or down, both sides of every triangle drawn (`Draw.Cull(Cull.None)`, as walls face out of what's built and into what's dug; `Walls`). Where this machine has the chunks, they show what's in their box, and the far ground the rest of a column: its top where that's below or above the box, its walls below and above it, and, where it goes on into the box from below, a cut at the box's bottom. **Correction (review, 2026-11-15)**: a column in the box's square was left out wherever its top was below the box's top, so a player 48 cells up or more saw a hole to the sky's colour where a column's top was below the box (a pit or a house in the valley under them) or in it (the base of their own pillar, of which the chunks show only the box's part); a column taller than the box had its top and no walls. Seen in captures from a pillar 74 cells up (`--script` that jumps and builds under the player, then steps off): the old build's square under the pillar was the sky's colour; walls alone made it a framed hole, which the cut closes. `Ground3` works the seed's height at each corner once (it did four times): 17.3 to 13.3 ms a far ground on a worker.

### 9. A chunk's faces across its edges (`play.tide`'s `Faces`, `Remesh`, `Chunks`, `Leaving`)

- **What a mesh is made from.** `Faces(blocks, east, west, above, below, north, south)`: its own grid and (until section 12, which hands it the neighbours' grids) its six neighbours' sides, `List<Block>`s of 1,536 cells (each of a chunk's six sides, 256 cells, in a fixed order), all air where the list is empty. It gives back, beside the faces, the chunk's own sides and a number for each side (`planes`, 0 where it's all air). A side shows where the block beside it is air, or water beside a block that isn't water (`Shows`): with the seams gone, a player under the sea saw through it, as the floor's sides against water were never drawn and only the chunks' edge faces had walled the sea in.
- **Where the sides were kept** (replaced 2026-11-15 by the neighbours' grids, section 12). `Sight.sides`, a local singleton's `Map<long, List<Block>>` by `KeyOf`, written by each chunk's `Remesh` as its faces are made: a chunk all of whose sides are air keeps none. The map is of one dimension: `Eyes` empties it and counts `Sight.epoch` as the player comes into one, and a chunk whose `epoch` isn't the map's makes its faces again. Picked over handing the worker its neighbours' grids: an async function can't take a grid, and a view that hands its chunk to a task as `mut` can't index a query of chunks ("changes Chunk.at of its own entity as it goes", though a task can only change a match component's local fields), so the view had no way to the neighbours' grids. Kept sides cost about 1.5 KB a chunk that has any.
- **Made again.** As a chunk's sides go into the map, each neighbour whose side toward it changed (by `planes`; one that wasn't kept counts as all air) is told (`Sight.bumped`, the frame it happened): a block changed inside a chunk is one mesh made again, and a chunk of sky that comes is nobody's. A chunk that leaves this machine (`local event(Forgotten) Leaving`) takes its sides out and tells those its non-air sides faced. A chunk begun before the frame it was told makes its faces again: at once for a change (a block dug at an edge shows the face beside it), and for a neighbour that came or went once none has for `SETTLE` (15) frames, as a view's worth of chunks comes over many frames and the faces between them are hidden ones (978 meshes at the start where every arrival made them again at once made 1,146). What nobody took within a frame is for a chunk this machine hasn't, and `Eyes` drops it.
- **Leaving is `Forgotten`** (review, 2026-11-15; it was `local event(Hidden)`). The review read the hosting machine as never hearing `Hidden`: its views see the whole match, and the spec has a genuine destruction suppress concealment, so a dropped chunk's sides would stay for good there and its neighbours keep faces hidden toward a chunk that's gone. Today's engine does deliver `Hidden` to the host as a chunk leaves its world (a two-player run walking apart, a probe logging the host's map against the chunks it has every 300 frames: equal throughout, and the same numbers with either handler, 730 sides at frame 1,800 and 500 at 2,100 as chunks went), so the map didn't grow; the handler leaned on what the spec says won't be. `Forgotten` is the spec's hook for a component's local fields going, whichever way the entity leaves, so `Leaving` is that now; a local field `place` keeps the chunk's place, as only local fields can be read there. This machine's own check: the views count the chunks they see whose sides are kept (`Sight.kept`, plus those kept since, `fresh`), and `Eyes` logs a line once more are kept for 60 frames running. `voxels_walk` (below) runs it; it passed with either handler on that day's engine, so it guarded the rule rather than failing without the fix. The engine was wrong (2026-11-15, later): a server journaling for its players recorded genuine `Spawned` and `Destroyed`, and the causes that tell a destruction from a concealment, into the players' journals alone, never into its own player's log, so the host heard no `Destroyed` and `Hidden` for every chunk dropped (notes/verified-and-local-handlers.md, top section). Now the host hears `Destroyed`, and `voxels_walk` with the old `Hidden` handler fails (81 chunks' sides kept, on the host); with `Forgotten` it passes.
- **A neighbour's faces shown with the chunk's** (review, 2026-11-15). A block dug at a chunk's edge uncovers a face of the neighbour's, which only the neighbour's new mesh has, and that's begun the frame after the chunk's new mesh shows: for a frame or two, a hole into the neighbour. Now where a chunk's side changed toward a neighbour this machine keeps sides of, its new faces wait (`next`, `holding`) and the neighbour goes into `Sight.awaited`; the neighbour's `Remesh` takes itself out once its faces were begun after that, and the chunk's view shows what waited once no neighbour of it is awaited, or after `HOLD` (10) frames. A script that walks into a hill digging (1,500 frames): 58 edits at an edge held, each shown 3 or 4 frames later with the neighbour's, none after `HOLD`; the tunnel looks as it should. An edit at an edge shows that much later than one inside a chunk.
- **Measured** (`voxels --frames 300 --size 960 540`, one player at the start, release, Windows, the machine shared; counts at frame 250 from a build that logs them, worker time from `--profile`, three runs each, the far ground's own jobs, 3 of them, measured apart and taken off): triangles of the chunks drawn 646,376 to 184,970 (151,340 without the sea floor's sides), chunks drawn 247 to 238 (those buried whole have no faces), meshes made 567 to 978 (1.7 a chunk: neighbours that come after it make its faces again once), a mesh 0.72 to 0.63 ms on a worker (0.68 to 0.54 in a quieter run before the sea floor's sides), all meshing in the 300 frames about 410 to 615 ms of workers' time, the views 0.29 to 0.43 ms a frame (more meshes begun, six lists copied for each). Frames 241 to 300: 1,077,771 to 460,221 indices drawn a frame, the GPU 0.047 to 0.028 ms a frame. The picture at the start is the same.

### 10. Measured (8 bots, 30 s, 4 threads, release, Windows, the machine shared; medians of three runs, before and after alternating)

"Default" is the bench as it was (bots wandering, digging and building as they fly); "nobody building" is `--builders 0` (bench flag, 2026-11-15: bots of each hundred that dig and build); "towers" is `--tower 10` (each bot flies straight out its own way for 10 s, then climbs at 12 cells a second building a tower: meant as 8 towers far apart, each in the others' far regions, its top changing 12 times a second; in this table they hopped between dimensions, see the correction below). Far's own bytes are best seen against the same build with sending switched off (`--gap 100000`), as region loads share the store's 64 answers a tick with chunks and move the chunks' timeline (towers: 6,037 chunks and 529 changes before, 6,073 and 576 after).

| | default | nobody building | towers |
|---|---|---|---|
| Sent to each bot, KB a second (before / after) | 22.5 / 23.7 | 22.0 / 22.0 | 16.4 / 17.1 |
| ...after, with far sending off / sent at every change (`--gap 0 --pace 256`) | 22.6 / 23.4 | | 16.6 / 17.4 |
| Server update, median / p99, ms | 0.93 / 5.95 to 0.92 / 5.44 | 0.76 / 5.14 to 0.80 / 5.17 | 0.65 / 5.60 to 0.78 / 5.02 |
| Server simulate / hashing, ms a second | 24.4 / 17.4 to 25.3 / 17.6 | 16.5 / 15.7 to 17.5 / 15.9 | 20.1 / 11.0 to 24.7 / 13.8 |
| A client's update, median / p99, ms | 0.16 / 2.48 to 0.16 / 2.47 | 0.15 / 2.12 to 0.15 / 2.10 | 0.12 / 1.86 to 0.14 / 2.07 |
| Far regions held by the server (with columns; sendings) | 443 (15; 64) | 443 (0; 0) | 390 (8; 207, 1,473 at every change) |

Far summaries cost a bot 0.5 to 1.1 KB a second where players build, and nothing where nobody does; at every change rather than paced, 0.8 either way here (the default's changes mostly ride on corrections the chunks cause anyway; a pattern changing columns every tick would have a correction every tick). The server holds a region entity for every region in a player's square, 443 here, which with nobody building costs it about 1 ms a second of simulating and 0.2 of hashing. The towers' simulate is the other timeline's as much as the far work: with sending off, the same build simulates 23.5 to 23.9 ms a second.

**Correction (review, 2026-11-15)**: two of those baselines weren't what they say. The towers above had hops on (`hop` 20 s, set before the tower's branch): each bot changed dimension once or twice, mid-climb, and built on in the other, so the towers were split between two dimensions. And `--gap 100000` doesn't switch far data off: a region loaded from the store is sent whole as it's loaded (`Recall`), so the regions the bots came back to still arrived (6 sendings in the default's run). `--tower` now keeps every bot in its first dimension (`hop` is 0 with it), and the numbers again, on the build after review (the same machine, shared; three alternating rounds; bytes the same each round, timings their middle):

| after review | default | nobody building | towers (one dimension) |
|---|---|---|---|
| Sent to each bot, KB a second: paced / `--gap 100000` / `--gap 0 --pace 256` | 23.4 / 22.5 / 23.2 | 22.0 | 13.6 / 13.1 / 13.8 |
| Server update, median / p99, ms | 0.93 / 5.89 | 0.76 / 5.84 | 0.40 / 2.90 |
| Server simulate / hashing, ms a second | 30.4 / 15.8 | 22.6 / 14.1 | 15.0 / 6.6 (sent at every change: 15.1 / 8.6) |
| Far regions held by the server (with columns; sendings) | 443 (15; 64; with `--gap 100000`, 6 sendings, the loaded ones) | 443 (0; 0) | 196 (4; 188, 1,483 at every change) |

The towers in one dimension stand 120 cells out, in the four regions about the start, and cost each bot 0.5 KB a second paced (0.7 at every change). Simulating costs more than the table before it here as a whole (the off and on runs alike: 30.5 and 30.4 ms a second in the default), so that's the machine that day, not the change.

### 11. The far regions' audit, and the rest of the review (2026-11-15)

- **`Audit`** (`voxels.tide`, a system on each dimension's entity, the server's alone, declared before `Generate` so it sees the regions as the last tick's events left them): every `Worlds.audit` ticks (bench flag `--audit`, 60 in a quick run, 0 in a full one) it counts in `Streamer.wrong` each region whose audience isn't the players whose square it's in (from `Streamer.watching`, `FarOf`); each region that has sent all it has (`!queued`) whose `Far.columns` aren't its `Summary.columns`, or that's shown without columns or hidden with them; each column the server can work out from its chunks (`Agrees`: going down the chunks of its column the server has, from the highest the players' boxes reach, to the first block, where neither a chunk above them nor a gap below could hold its top) whose region (or the seed, where it has none) says otherwise; and each chunk someone has that's in no region. `Publish` counts a region hidden while it has columns. `describe` prints `far checks every N ticks: audits, wrong`, and the run fails (exit 1) where any is wrong or none ran. A quick run audits 16 times in about the same time; every tick (`--audit 1`) 972 times, none wrong; the full run with `--audit 60`, 60 times and the towers 33, none wrong.
- **Rejected: "pace bounds column changes, not bytes"** (Far.columns is sorted, so an insertion moves every later column, and a repair would cost 8 bytes a column after it). The repair's codec (`tide_recipient_delta_pack`, notes/netcode.md, Recipient delta locality) takes copy spans from the baseline at any offset where its XOR regions don't pay, which is what a shift makes. Measured with the engine's own function (a scratch program, 6 KB of other records about a list of 8-byte columns): a column inserted before all the others costs 44 bytes whether the list has 8, 32, 64, 128 or 255 columns, against 38 for one changed in place and 60 for one removed from the front (each with the codec's 25-odd bytes of header). So pace bounds bytes too.

- `docs/game/endless-worlds.md` (Simulation, after State visibility): the game's shape for users, from chunks as entities to what it costs; Drawing what's far has faces across a chunk's edges and far regions (2026-11-15), the code shortened from the game's; after review, `Publish` whole but for its budget (the queue, `version`, showing and hiding), a chunk's new faces waiting for its neighbour's, `Forgotten`, and regions put right against their chunks. `docs/spec.md`, Grids, Open, names far regions beside the chunks. `docs/game/files.md` (Saving the match) and `docs/spec.md` (The server's store) have the store's rules.

### 12. The mesher takes its neighbours' grids (2026-11-15; `play.tide`'s `Faces`, `Remesh`, `Near`, `Bump`, `Chunks`, `Leaving`)

The two language limits section 9 worked around are gone (notes/language.md, top section): an async function takes a grid, a copy of its own made at the call, and a view that hands its chunk to a local task as `mut` changes only the chunk's local fields, so its loops over the chunks by their places are indexed. So:

- **`Faces(blocks, east, west, above, below, north, south)`** takes the six neighbours' `Grid3<Block>`s and reads the cell across each edge from them (`east[int3(0, y, z)]`); a neighbour this machine hasn't made is `default`, no grid's cells, all air, as an empty list was. It gives back the faces and `planes` (each side as one number, from the chunk's own cells, in the order and with the sums `sides` had), no longer the 1,536 cells of its sides.
- **`Near(chunks, at, dimension, side, mut present)`**, called six times by the `Chunks` view as it begins a chunk's faces: an `indexed foreach` over the view's `query (Chunk other) chunks` by the neighbour's place, dimension and `Stage.Made` (one exact key of five numbers), which returns `row.other.blocks.Copy()` and sets the side's bit in `present`, or `default`. `Remesh` takes the six grids (each its frame's own copy: a neighbour that changes while the worker runs doesn't change them) and `present`, which stands where `sight.sides.Has` did: the faces wait for the neighbour's (`awaited`) only where the neighbour is there.
- **Made again from the frame its neighbour's blocks were read** (`Bump(sight, at, now, since)`): `Sight.bumped` is a `Map<long, int3>` (the latest `since`, whether any was a change at once, the frame it was said in, which `Eyes` drops two frames on). `Remesh` bumps the neighbours whose side changed with `since` = the chunk's `begun`, the frame its blocks were read; a neighbour makes its faces again where `asked > begun`, so one whose faces were begun in that frame or later, which read these blocks, doesn't. Chunks that come together read each other's blocks and are each made once, where with sides every one of them was made again once the others' sides were there. `Leaving` bumps with `since` = the next frame, as before.
- **Gone**: `Sight.sides`, `SidesOf`, `Built.sides`, `SIDE`, the sides' writes in `Faces`, and this machine's check that a chunk's sides went with it (`Sight.kept`, `fresh`, `over` and the "sides kept" line, which `voxels_walk` failed on): there's nothing left to keep. `Sight.epoch` and `Chunk.epoch` stay: a task begun in a dimension the player left writes nothing into the new one's maps, and a chunk the hosting machine kept in another dimension makes its faces again when the player comes back.
- **Measured** as section 9 was (`voxels --frames 300 --size 960 540`, one player at the start, release, Windows, the machine shared with another agent's builds; counts at frame 250 from a build that logs them, worker time from `--profile`; the far ground's 3 jobs measured apart in a build that makes no chunk meshes, 50.9 to 52.1 ms, and taken off): triangles of the chunks drawn 184,970 and chunks drawn 238 in both, and the capture at frame 300 the same bytes; meshes made 978 to 792 (1.7 a chunk to 1.4); a mesh on a worker 0.55 to 0.57 ms to 0.36 (three runs each, quiet), and in three rounds alternating old and new as the machine got busier 0.65, 0.66, 0.83 to 0.40, 0.47, 0.43; all meshing in the 300 frames 538 to 557 ms of workers' time to 283 to 285 (quiet); the `Chunks` view 0.185 to 0.193 ms a frame to 0.124 to 0.139 (quiet; 0.200 to 0.236 against 0.128 to 0.158 alternating); results handed back 8.1 to 8.5 ms to 5.1 to 5.5. Why a mesh costs less was not taken apart: the worker no longer fills and hands back a list of 1,536 cells, nor tests six edges for each block, and the meshes it no longer makes are of the same chunks as the rest. The copies cost the view: each neighbour's grid is copied into the scratch area by `Near` and into the local state's heap as the task starts (and once more as the worker takes its snapshot), up to 4 KiB of cells each; the view is cheaper all the same, as it begins fewer meshes and copies no lists out of a map.
- **Same pictures**: the digging script of section 9 (a player who turns, jumps and digs along 57 cells of beach and grass, across four chunk edges, 1,500 frames) gives captures at frames 300, 500, 700, 900, 1,100, 1,300 and 1,500 that are the same bytes with the old mesher and the new; and `voxels_walk`'s two players with captures at frames 1,200, 2,000 and 2,690: the same bytes but the host's at 2,000, whose picture is the same and whose count of the chunks' triangles is 480,382 where the old one's was 526,942 (689 chunks drawn to 690): the host has every chunk of both players, and mid-walk the old mesher still drew faces toward chunks it had but whose sides it hadn't, which the new one reads at once. By frame 2,690 the two are the same again.
- **Checked**: `bench_voxels`, `_checked`, `_forgets` and `_wide` keep their pins (the match's code is as it was; only comments of `Chunk`'s local fields changed in voxels.tide), and `voxels_walk` passes. `docs/game/endless-worlds.md` (Faces across a chunk's edges) shows the grids version.
- **Correction (review, 2026-11-15): what neighbours read is compared, not the chunk's last mesh.** With sides kept, a chunk's faces toward a neighbour were made from the neighbour's kept sides, which were always its last mesh's, so bumping where a chunk's new planes differed from its last mesh's kept the two in step. Reading the neighbour's live blocks broke that: A's job runs (read side X), A's edge becomes Y, B begins reading Y, A's edge goes back to X, A's next mesh reads X, whose planes equal its last: no bump, and B's faces stay made against Y (a missing face, a hole into B) until something else remakes B. And a chunk whose side toward an edit was all air (a sky chunk over the top layer) was waited for (`present` was set for any chunk there), holding the edit's new faces 3 or 4 frames for a remesh that changed none of its faces. Now:
  - `Faces` gives back `near` beside `planes`: each neighbour grid's side toward the chunk, as it read it (`Plane`, the old sums in one function). `Remesh` writes `Sight.seen[key]` (`struct Edges`: `near`, `own` = `planes`, `frame` = `begun`) as its faces are made; the view sets `pending` = the frame as it begins one, so a record whose `pending > frame` is of faces being made, from what isn't known yet.
  - When a chunk's faces are made it compares with each neighbour's record, unless the neighbour's are being made (its own `Remesh` compares then, against this record): the neighbour is bumped (`since` = its `frame + 1`) where it read another side of this chunk than these planes, unless its own side there is all air; and the chunk bumps itself (at once, `since` = `begun + 1`) where it read another side of the neighbour than the neighbour's last faces were made from, unless its own side is all air. Whichever of two neighbours is made last compares, whichever was begun first, so the sequence above makes B's faces again. The new faces wait for a neighbour (`awaited`) only where that neighbour is bumped at once, so never for one whose side there is all air: `present` and `Chunk.planes` are gone.
  - `Leaving` bumps neighbours whose record read a side of it that wasn't air toward one of theirs that wasn't, or whose faces are being made; it runs for a chunk that was seen in the player's dimension in this epoch (`Chunk.read`, `place`, written each frame the view sees it, not only when its own faces were made), as a neighbour can read its blocks before its own first mesh is made.
  - **This machine's check, again** (`Sight.wrong`, `Chunk.stale`): each chunk once every 32 frames, where its faces and a neighbour's are settled (`Settled`: made, no job, not holding, made at the chunk's stamp, nothing asked), compares what it read of the neighbour with the neighbour's own record; two checks in a row that disagree log "Voxels: N chunks' faces made from a neighbour's side that has changed since", and `voxels_walk` fails on "changed since". With the neighbours' bumps switched off it logs 388 chunks in the walk; with only the chunk's own bump switched off (the order where the neighbour's faces were made first), 6: so the walk fails without either half of the fix. The walk passed 6 runs with the fix. The sequence of the review (a side that changes and changes back inside one job) isn't scripted; the walk's two players make the races it can.
  - **Measured** (`voxels --frames 300 --size 960 540 --profile`, three runs each, release, Windows, the machine shared): meshes 792 both (795 worker jobs with the far ground's 3), a mesh 0.42 to 0.44 ms on a worker either way; the `Chunks` view 0.125 to 0.128 ms a frame before, 0.150 to 0.155 after, of which the check is about 0.013 (0.138 to 0.142 with it off). A first version that worked out the records in the view as it began a mesh (12 `Plane`s of 256 cells each) cost 0.225 to 0.237.

### 13. The chunks around the player first (2026-11-15; `play.tide`'s `Chunks`, `Remesh`, `MakeFar`, `Measure`)

Game code, over the engine's new `priority:` on a local worker's call (notes/language.md, A local worker call's `priority:`; notes/schedule-and-threads.md, Worker priorities).

- **What says what.** `Chunks` hands `Remesh` a `WorkerPriority`, and `Remesh` gives it to `Faces`: `Urgent` for a chunk within `CLOSE` (2) chunks of the player's each way whose blocks changed or that has no faces yet (125 chunks: where the player looks first, and where a block the player digs is); `Normal` for those farther off; `Background` where only a neighbour came or went and nothing is urgent (`asked` alone: faces between two chunks that are mostly hidden ones). The far ground (`Ground3`, one job as long as a hundred chunks') is `Background`.
- **A build that measures** (`--define VOXELS_MEASURE=1`, `tide_add_game`'s `DEFINES`): the `Measure` view logs each frame's counts, the chunks within `CLOSE` and those of them with no faces, all the chunks here and those waiting, and whether the far ground is made. `Sight.close`, `closeWaiting` and `here` are counted in every build (three adds a chunk a frame).
- **Measured** (`voxels --script` with no window, 960 by 540: 400 frames standing at the start, then `H` to the other dimension and 500 frames there; release, Windows, a 12-core machine shared with other work; before is this game with no priorities on the worker runtime as it was, after is this section; three runs each, alternating). Frames from the player's coming into a dimension:

  | | chunks around the player have faces | all 405 chunks do | far ground made |
  |---|---|---|---|
  | start, before | 13, 13, 14 | 14, 14, 14 | 4 |
  | start, after | 9, 9, 9 | 12, 12, 12 | 4 |
  | other dimension, before | 14, 14, 12 | 15, 15, 14 | 6, 6, 5 |
  | other dimension, after | 12, 12, 12 | 14, 14, 14 | 6 |

  The same with the process kept to four processors and to two (affinity): 9 against 13 to 14 at the start, 11 to 12 against 12 to 14 after the jump. What bounds these runs is the server's sending of chunks, a tick a frame, which takes about 11 frames of each: a run with no window makes a frame in about a millisecond, so its workers have a sixteenth of the time a 60 Hz frame gives them, and the faces still come within a frame or two of their chunks. So the chunks around the player have faces as soon as they've all come, where they waited for the whole view before; all the chunks are done two frames sooner, since urgent calls have room of their own (64 normal calls could wait at once before, and no more were let in each frame); and the far ground comes when it did.
- **The same pictures and work**: `--frames 300 --capture` and the capture at the end of the script above are the same bytes before and after; 795 worker jobs in 300 frames both, 0.44 to 0.47 ms each on a worker, waiting 0.38 to 0.44 ms in the queue against 0.40 to 0.46 (`--profile`, two runs each).
- **Not measured**: a view farther than the game's 4 chunks, a window at 60 Hz (where threads have time to spare and admission is what's left), a machine with two cores for real (affinity leaves the pool its 11 threads), and a walk (chunks come a row at a time there).

## At scale (2026-11-16)

Whether the game "runs and handles it" past the sizes measured above (8 bots, 845 chunks a player): the view two and three times as far, 16, 32 and 64 players, one and two threads, two cores, and a player's own frame at 1920 by 1080. Release, Windows, a 12-core Ryzen 9 7900X shared with another agent's builds, which were idle during these runs (the machine's load read 1 to 2% before each set); `bench_voxels` with its defaults but what each row says (30 s once everyone's in, 50 ms each way, 10 ms of jitter, 1% lost), three runs each and their median unless it says one run; counts and bytes are the same every run. Times are a machine's own work, one after another on one thread (bench/bench.h), so the server's are a dedicated server's and a bot's are a player's machine's without its views.

What the bench gained for it (bench/README.md): the server's repairs are timed (they were in no column: a third of its update), an update's time in all and what the columns leave of it, what each bot is sent by kind of datagram, the chunks that come to a bot's machine and what their cells alone would be, how long a box takes to be there after a hop (`bench_desc.watch`, voxels' `--watch`), and bots that turn every tick as a mouse does (`--look`).

### In short

(Sections 1 to 5 are that morning's build. The day's fixes are under What was fixed and A whole view arriving, and
every table is measured again on the last build, with players apart, dimensions, a long run and the version to play
paced at 60 Hz, under The sweep again, which ends with what holds today.)

- **It runs.** The default (8 players, 845 chunks each) holds 60 ticks a second with room: the server's update 0.97 ms median and 5.8 ms p99, on one thread as on twelve, and held to two cores; a player's machine 0.09 ms median and 1.7 ms p99 for its ticks, and its whole frame at 1080p 0.2 ms median, 12 ms at worst. 32 players hold it barely (p99 15.7 ms); 64 don't (p99 43 to 47 ms). Twice the view holds it barely (p99 14.7 ms); three times doesn't (30 ms, and a client's verified world falls 70 ticks behind, past the 60 it predicts ahead).
- **Chunks cost 137 to 147 bytes each as sent**, a thirtieth of their cells, whatever the view or the player count. That part of the bandwidth is not high.
- **What is high is every player's input in every tick's frame, sent again in each datagram until it's acknowledged** (below, Bandwidth): nothing with bots, whose inputs change every five seconds; 13 to 44 KB a second to each player once all eight turn every tick as a mouse does (cut the same day, to 19.2 KB a second standing: Bandwidth, below). And a part lost from a repair of more than 64 parts has the parts after it sent twice: 2% more at the default view, 28% at twice, 40% at three times, with 1% lost.
- **What limited players** was the server's repairs: each walked every entity the server has, for each player, four times a second, so their cost went with players times all chunks (half the server's update at 64 players). Fixed later the same day (What was fixed, below): 32 players' p99 went from 15.2 ms to 10.5, and the tables below are from before it. **What limits the view** is the loops' indexes, made again whole whenever a chunk comes or goes (fixed later the same day too: a server's median update at twice the view 1.46 to 0.88 ms, a client's simulating 26.7 to 10.5 ms a second; What was fixed, below), and on a client the ticks it runs again while a whole view arrives.
- **Fixed here**: a predicting client sorted every entity it has again for each system and query, every tick, because a world's copy dropped its orders: half a client's simulating (18.4 to 9.6 ms a second at the default view, 122 to 55 at three times; its update's p99 2.5 to 1.7 ms).

### 1. View distance (8 bots; `--near 6`, `12`, `18 --lag 1200`)

| | near 6 | near 12 | near 18 |
|---|---|---|---|
| Chunks a player has / the server has | 845 / 6,490 | 3,125 / 17,599 | 6,845 / 31,857 |
| The server's world in memory | 20.2 MB | 54.9 MB | 105.3 MB |
| The process at most (server and 8 clients) | 547 MB | 1,358 MB | 2,553 MB |
| Server update, median / p99 / longest, ms | 0.97 / 5.80 / 14.6 | 2.63 / 14.7 / 26.4 | 5.05 / 30.5 / 57.0 |
| Server, ms a second: simulate / hashing / repairs / snapshots / the rest | 33.2 / 9.7 / 21.8 / 1.6 / 7.2 | 86.5 / 24.4 / 67.5 / 4.0 / 13.1 | 164 / 53.4 / 142 / 8.3 / 22.3 |
| ...in all | 73.5 | 195 | 391 |
| A repair, each (33, 33 and 32 a second) | 0.66 ms | 2.07 ms | 4.5 ms |
| A client's update, median / p99 / longest, ms | 0.09 / 1.66 / 8.1 | 0.28 / 4.87 / 23.7 | 0.54 / 9.58 / 56.5 |
| A client, simulating, ms a second | 9.6 | 29.4 | 55.3 |
| Sent to each player, KB a second | 23.4 | 72.9 | 159.7 |
| ...repairs' parts / ticks / the rest (one run) | 18.4 / 3.2 / 1.7 | 68.1 / 3.1 / 1.8 | 155.0 / 2.9 / 1.8 |
| ...what its machine got of worlds (each part once) | 18.0 | 53.0 | 110.7 |
| Chunks that came to a player's machine a second | 131.7 | 385.8 | 755.3 |
| ...as their cells alone (4,096 bytes each) | 540 KB | 1,580 KB | 3,094 KB |
| Bytes a chunk, as got | 137 | 137 | 147 |
| After a hop, nine tenths of the box there, ticks: median (most) | 28 (39) | 40 (79) | 62 (140) |
| ...all of it | 32 (56) | 47 (85) | 67 (207), one never before the next hop |
| A client's verified world behind the server's at most, ticks | 41 | 53 | 70 |

- **Nothing grows faster than the chunks.** With 3.7 and 8.1 times the chunks a player (2.7 and 4.9 times the server's), the server's simulating is 2.6 and 4.9 times, its hashing 2.5 and 5.5, its repairs 3.1 and 6.5, what's sent 3.1 and 6.8, a client's simulating 3.1 and 5.8. The server's simulating is 85 ns a chunk it holds a tick at every size, though only a few dozen come or go in a tick: see What costs a chunk that's only held, below.
- **What breaks first is the 60 Hz tick**, at three times the radius: the server's p99 30 ms. At twice it holds at p99 (14.7 ms; 14.1 to 17.1 over the three runs) with one update in a hundred near the whole frame.
- **A whole view arriving** (a join, a hop) is the other: a client's verified world falls behind while it comes, by 41, 53 and 70 ticks. A client predicts at most a second past what it knows (`PREDICTION_SECONDS`), so past 60 ticks its player's own input waits; the bench stops a run at 60 unless `--lag` says more, and `--near 18` needs it. Two things make the wait: the store answers 64 chunks a tick (section 3: 3,125 chunks take 49 ticks, 6,845 take 107), and a stream of repairs is sent every 2, 4, 8, then 15 ticks (`repair_gap_after`), each a round trip. With no loss the same runs are 25 and 30 ticks behind at near 6 and 12 (one run each): a lost part of a repair is most of the rest.
- **A client's longest update** (8, 24 and 57 ms) is the ticks it runs again when such a repair lands: as many ticks as it was behind, each costing what its world holds.

### 2. Players (the default view; `--players 16`, `32`, and 64 with `maxPlayers = 64` in a build not kept)

| | 8 | 16 | 32 | 64 (one run of each of three) |
|---|---|---|---|---|
| Chunks the server has | 6,490 | 7,717 | 12,669 | 17,598 |
| The server's world in memory | 20.2 MB | 26.4 MB | 36.8 MB | 54.0 MB |
| Server update, median / p99 / longest, ms | 0.97 / 5.80 / 14.6 | 2.03 / 9.51 / 15.6 | 3.71 / 15.7 / 31.9 | 7.0 to 7.9 / 43 to 47 / 62 to 69 |
| Server, ms a second: simulate / hashing / repairs / snapshots / the rest | 33.2 / 9.7 / 21.8 / 1.6 / 7.2 | 52.2 / 18.5 / 50.3 / 3.6 / 13.6 | 74.7 / 31.9 / 101.8 / 5.7 / 26.2 | 111 to 124 / 56 to 67 / 234 to 264 / 10 to 12 / 53 to 58 |
| ...in all | 73.5 | 138 | 240 | 464 to 525 |
| Repairs a second, and each | 33, 0.66 ms | 63, 0.80 ms | 113, 0.90 ms | 201, 1.2 to 1.3 ms |
| A client's update, median / p99 / longest, ms | 0.09 / 1.66 / 8.1 | 0.11 / 1.92 / 7.4 | 0.12 / 2.11 / 10.4 | 0.11 to 0.15 / 2.0 to 2.8 / 12 to 13 |
| Sent to each player, KB a second | 23.4 | 23.9 | 26.6 | 29.6 |
| After a hop, nine tenths of the box, ticks: median (most) | 28 (39) | 26 (45) | 27 (43) | 27 (55), 4 of the hops never whole |

- The bots start in one place and wander 30 seconds, so more of them share more chunks: 16 bots hold 1.2 times what 8 do, 32 hold 2.0, 64 hold 2.7. Players spread over a world hold 845 each, and everything below that goes with all chunks would be that much worse: these runs are the friendly case.
- **Per player nothing changes**: a client's update, what it's sent (23 to 30 KB a second; more players building near each other), how long a hop takes.
- **The server's update doubles with the players**, and its repairs more than double: 21.8, 50.3, 101.8 and about 250 ms a second, a third of its update at 8 players and half at 64. A repair is worked out for one player and costs 70 to 95 ns for every entity the server has (`tide_game_client_changes`: `tide_client_see` reads every row of the world the player confirmed, working out its audience, and the world as it is from what the tick's check kept), so the repairs' cost goes with players times all entities. 32 players spread out (27,000 chunks) would be about 270 ms a second by that, and 64 (54,000) more than a second a second.
- **32 players are the most that hold 60 Hz here** (p99 15.7 ms, 14.8 to 15.8 over three runs); 64 run, nobody's world goes wrong, and one update in a hundred takes three frames.
- `maxPlayers` is 32 in the game: the 64 was a build with it changed, which moves the world's layout and the pinned hashes, so it isn't kept.

### 3. A small machine (the default run; `--threads 1`, `2`, `4`; the process held to two cores by its affinity)

| | 12 threads | 4 | 2 | 1 | two cores, 12 threads | two cores, 2 threads | two cores, 1 thread |
|---|---|---|---|---|---|---|---|
| Server update, median / p99 / longest, ms | 0.99 / 6.05 / 17.4 | 0.96 / 6.13 / 16.3 | 0.98 / 5.88 / 14.5 | 0.89 / 5.70 / 13.8 | 1.02 / 6.11 / 16.3 | 1.01 / 6.34 / 15.7 | 1.03 / 6.34 / 16.3 |
| Server, simulating, ms a second | 34.3 | 33.8 | 34.4 | 30.3 | 34.7 | 35.5 | 34.3 |
| A client's update, median / p99 / longest, ms | 0.10 / 1.73 / 8.2 | 0.08 / 1.56 / 7.6 | 0.09 / 1.55 / 7.7 | 0.06 / 1.29 / 6.8 | 0.10 / 1.77 / 9.0 | 0.09 / 1.59 / 7.9 | 0.07 / 1.43 / 8.2 |
| A client, simulating, ms a second | 10.1 | 8.7 | 8.8 | 6.3 | 10.2 | 8.9 | 6.9 |

- **A 60 Hz tick holds on one thread and on two cores**: p99 under 6.4 ms everywhere, the longest of 1,900 updates 14 to 17 ms.
- **The tick gains nothing from threads, and loses a little**: one thread is the quickest for the server (30.3 against 34.3 ms a second) and by a third for a client (6.3 against 10.1). What a tick does here is spawn, load, repair and hash, a thread's work each; `Generate` goes across threads only on the ticks that make chunks, and a tick with nothing to make still hands it out in pieces (Gaps, A system that writes grids). A server for this game wants a fast core, not many.

### 4. A player's own frame (`voxels_near4`, `8`, `12`: the version to play at 1920 by 1080 with no window, its server in the same process)

`-DTIDE_VOXELS_VIEWS="4;8;12"` builds them (`VOXELS_NEAR`, and `VOXELS_MEASURE=1` for play.tide's own counts). Standing at the start for 600 frames, and walking for 1,100 with a hop at frame 500, `--profile`, three runs each (the first and last of each number below). A run with no window isn't paced: each frame is a tick, made as fast as it goes (about a millisecond), and the host has no way to hold one to 60 Hz (`--realtime` was turned down, notes/standard-host.md), so workers get a sixteenth of the time a real frame gives them and "frames" below are ticks.

| | near 4 (405 chunks) | near 8 (1,445) | near 12 (3,125) |
|---|---|---|---|
| Frame's CPU, standing: median / p95 / longest, ms | 0.21 to 0.22 / 0.85 to 1.12 / 10.9 to 12.6 | 0.54 to 0.58 / 3.6 to 3.7 / 30.9 to 39.6 | 1.10 to 1.15 / 5.9 to 6.2 / 57.4 to 59.2 |
| ...walking, with a hop | 0.24 / 1.6 to 1.8 / 12.3 to 21.4 | 0.62 to 0.68 / 4.2 to 5.1 / 30.2 to 34.4 | 1.63 to 1.98 / 10.9 to 11.1 / 58.2 to 61.6 |
| Of it, walking: the match (the server's tick and this machine's), median / p95 / longest | 0.07 / 0.15 to 0.18 / 9.4 to 16.7 | 0.10 to 0.13 / 1.1 to 1.3 / 25.7 to 29.8 | 0.21 to 0.23 / 6.0 / 55.5 to 60.3 |
| ...the views | 0.14 / 0.9 to 1.0 / 7.6 to 8.4 | 0.45 to 0.48 / 3.2 to 3.5 / 9.6 to 10.8 | 1.26 to 1.52 / 4.8 to 5.1 / 21.8 to 22.5 |
| ...the renderer's CPU | 0.03 / 0.06 / 10.9 to 12.2 | 0.05 / 0.21 to 0.27 / 9.6 to 9.9 | 0.12 / 0.42 to 0.48 / 11.5 |
| GPU, median / p95, ms (RTX 4080 SUPER) | 0.04 / 0.04 | 0.05 / 0.07 | 0.08 / 0.18 |
| Triangles drawn a frame, standing (chunks in view and the far ground) | 146,000 | 278,000 | 465,000 |
| Faces made on workers, standing: jobs, each, in all | 795, 0.43 to 0.45 ms, 0.35 s | 3,040, 0.39 to 0.40 ms, 1.2 s | 6,766, 0.38 to 0.40 ms, 2.6 to 2.7 s |
| The process at most, standing / after the hop | 248 / 365 MB | 410 to 424 / 695 MB | 653 to 680 / 1,187 MB |
| From the start: the 125 chunks around the player have faces / all chunks here / all have faces, frames | 9 / 9 / 12 | 19 / 25 / 33 | 34 / 51 / 73 to 76 |
| After the hop | 12 / 8 / 16 | 28 / 24 / 62 | 57 to 62 / 50 / 140 to 143 |

- **A 60 Hz player sees a steady frame**: its CPU is a fiftieth of a frame at the default view and a tenth at three times the chunks' radius, the GPU nothing. Fewer than one frame in twenty passes 16.6 ms at any size (p95 is 11 ms at most; the profile has no count of them).
- **And a hitch as a whole view is asked for**: the longest frame is the tick a box of chunks is spawned in on the machine that hosts (the match's part of it: 9 to 17 ms for 405 chunks, 26 to 35 for 1,445, 53 to 60 for 3,125). One frame late at the default view after a hop, two at twice the radius, three or four at three times. A player of someone else's server doesn't make that tick; theirs is the bots' client row of table 1.
- **Chunks appear at the store's pace**: 64 a tick, so a box is all there after 9, 25 and 51 frames (0.15, 0.4 and 0.85 s at 60 Hz), the ones around the player first (section 13), and has its faces 3, 8 and 25 frames after that here, where workers have a millisecond a frame; at 60 Hz the faces of a tick's 64 chunks (26 ms of workers' time, half as much again for the ones made twice) fit in the frame they come in on three workers.
- Each chunk's faces are made 1.5 times over at these sizes (1.4 at the default: Gaps, Faces across chunk edges), and the `Chunks` view looks at every chunk the machine has each frame (0.12, 0.40 and 0.87 ms a frame standing).
- Held to two cores (affinity), walking with the hop: the same within the runs' spread (near 4: 0.24 / 1.5 / 12.3 ms; near 12: 1.56 / 10.5 / 59.0).

### 5. Bandwidth

What a player is sent at the default view, 23.4 KB a second: 18.4 of repairs' parts (the chunks), 3.2 of ticks, 1.7 of what came and went for its local handlers. It sends 7.1.

- **Chunks: 137 bytes each.** 131.7 chunks come to a player's machine a second (about 65 from flying at 12 cells a second, 42 from a hop every 20 s, the rest its first box, joining), which is 540 KB a second of cells; they're sent as 18.0 KB, a thirtieth. Without hops, 89.5 chunks and 12.4 KB a second (139 bytes each). At twice and three times the view, 137 and 147 bytes each. A box of mostly air and stone is mostly chunks of one block (13 bytes as `ToBytes` writes one, section 4) around a layer of ground of a few hundred, plus each chunk's record: nothing abnormal, and nothing more to take without compressing across chunks.
- **A lost part costs more than itself in a big repair.** A repair goes in parts of 1,000 bytes, 64 an update, acknowledged by the first part missing and a mask of the 64 after it (`send_chunks`). A part lost from a longer repair holds the acknowledgement where it is, and every part past the mask, which the client has and can't say so, is sent again once it's 1.5 round trips old. With 1% lost: 18.4 KB a second of parts for 18.0 got at near 6 (2% more), 68.1 for 53.0 at near 12 (28%), 155.0 for 110.7 at near 18 (40%); with none lost, 51.8 for 53.1 at near 12. A whole box is 116 KB at the default view (116 parts), 430 KB at twice. Not changed here: holding those parts back until the mask reaches them would make a lossy join slower (each hole a round trip after the one before), and a wider mask is a protocol change.
- **Every player's input, in every frame, again and again.** A tick's frame has the inputs that changed, for every player, whether the recipient has that player's avatar or not. While the frames a client hasn't acknowledged fit in one datagram together, all of them go in every datagram (`send_frames`: `again` is 0 while `lacking + 32 <= TIDE_NET_MTU`); once they don't, each goes once, and again after 1.5 round trips. At this round trip (133 ms) that's 8 or 9 frames a datagram. The bots' inputs change every five seconds, so the table's ticks are small: 115 bytes a datagram standing still (6.4 KB a second), with 8 players as with 32. With every bot turning every tick as a mouse does (`--look 100`):

  | KB a second to each player | 8 players, flying | 8, standing | 32, flying | 32, standing |
  |---|---|---|---|---|
  | Ticks, bots as they are | 3.2 | 6.4 | 3.6 | 5.7 |
  | Ticks, every bot turning every tick | 16.5 | 44.0 | 13.0 | 18.9 |
  | All it's sent, turning | 37.4 | 46.8 | 38.4 | 21.0 |
  | What it sends, as they are / turning | 7.1 / 11.7 | 4.9 / 8.0 | 7.2 / 11.9 | 4.9 / 8.0 |

  Eight players who only look around cost each other 44 KB a second each, 790 bytes a datagram, where what's new in a tick is about 80 bytes by the same sum (eight inputs of two floats; not counted apart): the same frames eight or nine times over. With 32 the frames no longer fit a datagram together, so each goes once (385 bytes a datagram), which is why 32 players standing cost each other less than 8 do; and with repairs coming (flying) fewer ticks are sent at all (22 datagrams a second, not 56). This is the one number here that is abnormally high, and it's the one a real session would have: one run each, the bots' turns random, a hand's smoother. It's the session's way of losing no tick to a lost datagram: a frame that goes once and is lost comes 1.5 round trips late, and the others' inputs are guessed meanwhile. What would change it is the owner's to weigh: a frame sent a set number of times (twice loses one tick in ten thousand to 1% loss, for two copies rather than nine), a smaller size past which frames go once, or inputs only of the players a machine has.

  **Cut (2026-11-16, later; notes/netcode.md, Ticks in as many datagrams as the loss takes; session protocol 26).** A tick now goes in as many datagrams in a row as the loss a player's machine counts takes (two with none lost, three to 2%, four to 5.6%), then only once the machine's word shows it lost; the copies take a quarter of a datagram at most, so 32 players' ticks (385 bytes) go once, as they did. And a machine's inputs go while they can still be in time for their ticks, where every one went until the server said it had a newer. The same runs, 120 s with 8 bots and 60 s with 32, before to after, 1% lost:

  | KB a second | 8 players, flying | 8, standing | 32, flying | 32, standing |
  |---|---|---|---|---|
  | Ticks to each player, bots as they are | 3.5 to 2.4 | 6.7 to 3.8 | 4.1 to 3.0 | 6.1 to 3.6 |
  | Ticks, every bot turning every tick | 18.0 to 10.6 | 46.9 to 19.2 | 15.1 to 16.1 | 20.5 to 20.8 |
  | All it's sent, turning | 37.6 to 30.4 | 47.7 to 20.0 | 41.2 to 42.2 | 21.8 to 22.1 |
  | What it sends, turning | 11.8 to 9.0 | 8.0 to 7.4 | 11.8 to 9.0 | 8.0 to 7.3 |
  | Its verified world behind the server's on average, turning, ticks | 9.58 to 9.52 | 4.01 to 4.01 | 10.52 to 10.46 | 4.88 to 4.47 |

  With none lost the turning 8 standing are sent 13.7 KB a second of ticks, with 5% lost 24.2; a client's verified world is as far behind the server's as it was at each (the tables in netcode.md have 99 frames of 100 and 999 of 1,000). What's left of it is the copies of every player's input the loss asks for (two with none lost, three with 1%), and one where 32 turn at once.
- **Far regions, repairs against first sends**: as section 10 has them (0.5 to 1 KB a second where bots build); a repair here is almost all first sends, as a client can't predict a chunk it hasn't. The server's own count of what a player's machine got and the parts it sent agree within 2% at the default view.

### What costs a chunk that's only held

Three things make a tick cost what the world holds rather than what changed, found with `tide_sample` and counters put in for a run (taken out again):

- **A world's copy dropped its entities' orders** (fixed, below).
- **A loop's index is made again whole when any page of its key changes** (notes/indexed-loops.md, Gaps), and each world has its own. A server's chunks come and go every tick, so `Has`, `Find` and `Firm`'s indexes of every chunk are made about twice a tick: 5.3 of its 30.7 ms a second of simulating at near 6 on one thread, 19.6 of 83.6 at near 12. A client ticks a copy of its world each tick (`world_at(c, tick + 1)`), whose indexes are the ones that slot of its ring had the last time round, older than the last repair: 1.7 made a tick, 2.3 of a client's 13.1 ms a second at near 6 before the fix below, 9.8 of 47.8 at near 12. Not changed: an index that takes in what came and went (per chunk of the table) or a copy that takes its source's indexes with it are both the compiler's and the runtime's (`index_codegen.inc`, `engine/src/index.c`), and want their own task. (Both built later the same day: What was fixed, below.)
- **A repair read every row of the server's world for one player** (section 2). The world the player confirmed has nothing kept (`tide_client_kept` is of the tick's own world), so each of its rows had its audience worked out; 0.66 ms a repair at 6,900 entities, 2.1 ms at 18,000, 4.5 ms at 32,000. Fixed (below).
- `Idle`, `Keep` and `Retry` look at every chunk each tick by design (7% of a 32-player run's samples): a line of chunks by the tick they're due would cost what's due.

### What was fixed

- **A copied world keeps its entities' orders** (`engine/src/entity.c`, `tide_entities_copy`). A system or query that goes through its entities in identity's order asks the table for them sorted (`tide_entities_ordered_archetypes_slots`), kept until an entity comes, goes or changes archetype. A copy threw them away, and a client ticks a copy of its world every tick: each of its ticks scanned every slot and sorted every entity for each set of archetypes its systems use, 3.8 times a tick (129,610 times in a 34 s run of 8 clients; 6.7 of each client's 13.1 ms a second of simulating at near 6 on one thread, 25.6 of 47.8 at near 12). Now an order is shared by the table it was made in and its copies (a count of holders; one a table's change puts aside stays until that table settles, as before), and each side's changes are its own. `entity_orders_are_shared_with_copies_until_either_changes` (tests/test_entity.c): a copy has its source's orders with no sort, makes the ones its source hadn't, each side's change leaves the other's, a moved entity changes the subsets alone, and an order outlives the table it came from. Measured (12 threads; before one run, after three):

  | a client | near 6 | near 12 | near 18 |
  |---|---|---|---|
  | Simulating, ms a second | 18.4 to 9.6 | 60.5 to 29.4 | 122.2 to 55.3 |
  | Update, median / p99 / longest, ms | 0.17 / 2.50 / 9.7 to 0.09 / 1.66 / 8.1 | 0.57 / 8.35 / 30.9 to 0.28 / 4.87 / 23.7 | 1.15 / 17.7 / 79.4 to 0.54 / 9.58 / 56.5 |

  The server's simulating is as it was (it ticks its own world, whose orders it kept; it sorts once for each tick something came or went, 3.4 of 30.7 ms a second at near 6). Every world hash is the same: `bench_voxels`' pins stand, checked and counted; `tide_tests` (758), `tidec_e2e_visibility_runs`, `_pieces`, `_indexed_loops`, `_previous`, `_net` and `voxels_walk` pass.
- **A repair looks at the rows that changed for its player, not the world** (`client_spans_codegen.inc`'s journal, `client_changes_codegen.inc`; `notes/netcode.md`, top section, has how and the tests). Three runs each, alternating with the build before, medians; the same bytes sent and world hash in every pair:

  | | 8 bots | 16 | 32 | 8, `--near 12` |
  |---|---|---|---|---|
  | Repairs, ms a second | 19.3 to 3.78 | 39.3 to 8.63 | 103.3 to 22.6 | 66.7 to 9.35 |
  | A repair, each | 0.58 to 0.11 ms | 0.62 to 0.14 | 0.91 to 0.20 | 2.05 to 0.29 |
  | Server update, median / p99 / longest, ms | 0.86 / 5.37 / 14.9 to 0.63 / 5.20 / 12.9 | 1.61 / 7.89 / 14.0 to 1.26 / 6.48 / 13.5 | 3.57 / 15.23 / 29.8 to 2.56 / 10.49 / 18.0 | 2.37 / 15.55 / 31.5 to 1.62 / 12.59 / 24.7 |
  | Server, in all, ms a second | 66.4 to 54.3 | 110.0 to 90.3 | 237.7 to 158.6 | 191.7 to 130.5 |
  | ...simulating / hashing | 31.9 / 6.80 to 33.3 / 8.27 | 46.6 / 10.4 to 51.2 / 14.0 | 76.2 / 23.7 to 77.5 / 24.4 | 87.6 / 19.6 to 85.5 / 19.8 |

  (Another agent's builds ran through this set: the runs spread, and simulating, which nothing here touches, moves with them. `notes/netcode.md` has the ranges and a quieter set.) A repair is now what it sends: the chunks' records, and one hash of the world the player confirmed. Its cost no longer goes with players times all chunks: at 32 players a repair looks at 96 rows of 12,669. 32 players hold 60 Hz with room (p99 10.5 ms where it was 15.2); 64 weren't run again (`maxPlayers` is 32, as before). The checks pay a hash of the world more a tick (0.7 ms a second at the default, in the hashing row).
- **A loop's index takes in the rows that changed, and a copied world keeps its source's** (later the same day;
  `notes/indexed-loops.md`, top two sections, has how, the runtime's new calls and the tests). `Has`, `Find` and
  `Firm` find a chunk by its place through an index that was made again from every chunk whenever a page of the
  keys was another page. Now the site compares the pages it holds of each table chunk (1,024 rows) with the
  table's and puts or drops the rows whose bytes differ; and a client's copy of its world searches the index of
  the world it was copied from while the rows are the same, and copies it when one changes. Three runs each,
  alternating with the build before, medians; the same world hash and bytes sent in every pair:

  | | 8 bots | 8, `--near 12` | 32 bots | 8, one thread | 8, `--near 12`, one thread |
  |---|---|---|---|---|---|
  | Server, simulating, ms a second | 28.1 to 19.6 | 78.9 to 50.5 | 67.7 to 48.5 | 26.9 to 20.3 | 74.1 to 50.2 |
  | Server update, median / p99 / longest, ms | 0.50 / 4.45 / 12.1 to 0.32 / 4.23 / 11.9 | 1.46 / 12.18 / 20.9 to 0.88 / 11.39 / 19.9 | 2.11 / 8.88 / 16.0 to 1.67 / 8.12 / 16.0 | 0.46 / 4.31 / 12.1 to 0.31 / 4.03 / 11.9 | 1.36 / 11.70 / 20.2 to 0.87 / 11.34 / 19.5 |
  | Server, in all, ms a second | 44.3 to 35.1 | 118.6 to 88.1 | 130.4 to 108.8 | 42.5 to 34.9 | 111.7 to 85.0 |
  | A client, simulating, ms a second | 7.87 to 4.42 | 26.7 to 10.5 | 10.5 to 6.6 | 5.48 to 2.35 | 20.5 to 6.8 |
  | A client's update, median / p99 / longest, ms | 0.07 / 1.32 / 6.1 to 0.04 / 1.01 / 5.9 | 0.25 / 4.33 / 19.4 to 0.08 / 2.79 / 15.9 | 0.10 / 1.81 / 8.0 to 0.06 / 1.47 / 8.0 | 0.05 / 1.06 / 5.5 to 0.02 / 0.75 / 5.4 | 0.18 / 3.75 / 16.7 to 0.05 / 2.27 / 14.5 |

  The runs spread by 1 to 2% (28.0 to 28.5 and 19.6 to 19.8 for the first cell). A server's median update is a
  third shorter at every size and its p99 hardly moves: the long updates are the ticks a box of chunks is
  spawned in and the repairs, not the indexes. A client simulates in half the time at the default view and in
  two fifths at twice.

  **What's left of it**, counted and timed in a build that says so (the counts are `TIDE_INDEX_STATS`'; a clock
  around each walk that takes something in was put in for the runs and taken out), on one thread, 30 s, 8 bots:

  | | server | server, `--near 12` | a client | a client, `--near 12` |
  |---|---|---|---|---|
  | Loops' starts that found their index as it was | 48,492 | 65,876 | 171,000 | 231,000 |
  | ...that took in what changed | 4,272 | 4,345 | 5,170 | 5,490 |
  | Table chunks compared in those, and rows that differed | 8,934, 82,239 | 10,569, 166,496 | 5,200, 116,000 | 8,800, 387,000 |
  | Indexes copied (a row changed while the source held the index) | 0 | 0 | 516 | 590 |
  | Indexes made whole | 6 | 6 | 3 | 3 |
  | Time taking in, ms a second | 0.32 | 0.63 | 0.28 | 1.0 |

  Before, every one of the starts in the second row was an index made from every chunk the machine has: 4,278 a
  run for the server, 5,170 for a client (41,351 over the eight, 849 rows each at the default view). What a
  server pays now is a compare of the pages that were written (two of its seven table chunks a time, six columns
  of 1,024 rows) and an entry for each of 19 rows: 2.6 µs a time where making the index was about 66. It is still
  paid about twice a tick because a chunk's `Loaded` and `Change` handlers take every lane of the chunk's
  component for writing in its table chunk, so the key's pages are other pages after any of them though their
  bytes are the same (notes/indexed-loops.md, Gaps: taking only the lanes a handler writes would end most of it,
  and the 48 KB a handler's table chunk copies). A client's is that and a copy of the index, about fifteen times
  a second, when a repair or a prediction changes a chunk's row. With 32 bots the server's is 0.75 ms a second of
  its 48.5 and a client's about 0.35 of its 6.6. The looking itself (a compare of seven numbers a table chunk,
  under the site's lock, for each start that isn't a system's own loop already found to hold) is in the first
  row's counts and wasn't timed apart: the clock cost more than it does.

  The copy alone (a client's copy taking its source's index, without the taking in) gained nothing: a client's
  simulating 5.4 to 5.6 ms a second on one thread, one run each, because the pages differ about once a tick.
- **The bench says where a server's time and bytes go** (above; bench/README.md).
- **A handler takes the lanes it changed** (later still the same day; `notes/ecs.md`, top section, has how and
  the tests). `Loaded`, `Change` and the other handlers took all 12 lanes of `Chunk` (28 of `Streamer`, 8 of
  `Seen`, `Far` and `Summary`) for writing in the table chunk of each entity an event came to, a copy of each
  page a snapshot shared, and wrote two or three. Now a handler's row is read where it is and only a lane whose
  bytes changed has its page made the table's own. Three runs each, alternating with the build before, medians;
  the same world hash and bytes sent in every pair:

  | | 8 bots | 8, `--near 12` | 32 bots | 8, one thread | 8, `--near 12`, one thread |
  |---|---|---|---|---|---|
  | Server, simulating, ms a second | 19.70 to 18.36 | 50.0 to 49.2 | 48.5 to 46.1 | 20.3 to 19.4 | 50.0 to 49.2 |
  | ...hashing | 6.14 to 5.64 | 16.0 to 15.2 | 18.6 to 16.9 | 5.62 to 5.28 | 13.7 to 13.3 |
  | ...repairs | 2.92 to 2.76 | 8.42 to 8.22 | 17.5 to 17.0 | 2.88 to 2.74 | 8.30 to 8.16 |
  | Server update, median / p99, ms | 0.32 / 4.15 to 0.28 / 4.01 | 0.87 / 11.4 to 0.83 / 11.3 | 1.67 / 8.54 to 1.55 / 8.08 | 0.32 / 4.17 to 0.28 / 4.14 | 0.87 / 11.3 to 0.83 / 11.0 |
  | Server, in all, ms a second | 35.3 to 32.9 | 87.4 to 85.2 | 108.8 to 102.9 | 35.0 to 33.3 | 84.7 to 82.9 |
  | A client, simulating, ms a second | 4.49 to 3.86 | 10.6 to 9.7 | 6.59 to 5.76 | 2.36 to 1.90 | 6.79 to 6.10 |
  | A client's update, median / p99, ms | 0.04 / 1.01 to 0.03 / 0.92 | 0.08 / 2.80 to 0.08 / 2.70 | 0.06 / 1.47 to 0.05 / 1.35 | 0.02 / 0.76 to 0.02 / 0.69 | 0.05 / 2.25 to 0.05 / 2.16 |
  | A client, in all, ms a second | 6.34 to 5.53 | 15.6 to 14.4 | 9.41 to 8.33 | 4.11 to 3.49 | 11.3 to 10.4 |

  The runs of a cell are within 1.5% of each other (19.54 to 19.86 and 18.26 to 18.46 for the first). A server's
  p99 hardly moves: its long updates are the ticks a box of chunks is spawned in. A client simulates in six
  sevenths of the time at the default view (four fifths on one thread), and the server's hashing is a twelfth
  less: a page a handler took and didn't change was hashed again too.

  Counted in a build that says `TIDE_PAGE_STATS` and `TIDE_INDEX_STATS` (bench/README.md; one run each, the
  counts are the same every run), a frame's pages for the server, and for the eight clients (32 in the third
  column) together:

  | | 8 bots | 8, `--near 12` | 32 bots |
  |---|---|---|---|
  | Server: pages taken a frame (new, copied or written apart) | 130 to 63 | 174 to 101 | 210 to 100 |
  | ...of them copies of a page a snapshot shares, and their bytes | 87 to 20, 368 to 211 KB | 101 to 29, 499 to 320 KB | 154 to 45, 791 to 464 KB |
  | Clients: pages taken a frame | 954 to 347 | 1,268 to 590 | 3,416 to 1,255 |
  | ...copies, and their bytes | 667 to 59, 1,051 to 370 KB | 788 to 110, 1,776 to 822 KB | 2,425 to 264, 4,371 to 1,745 KB |
  | Server: loops' starts that took in what changed (of 52,764, 70,221 and 211,098) | 4,272 to 1,024 | 4,345 to 1,176 | 6,350 to 2,343 |
  | ...table chunks compared in those | 8,934 to 1,606 | 10,569 to 2,049 | 26,112 to 4,310 |
  | Clients: starts that took in what changed | 41,340 to 5,062 | 43,892 to 5,249 | 217,843 to 23,825 |
  | ...table chunks compared | 41,574 to 5,296 | 70,388 to 19,762 | 219,683 to 25,665 |

  The rows looked at, the indexes copied and the ones made whole are the same before and after (82,239 rows for
  the server at the default view, 930,536 and 4,128 copies for the clients): what went is every start that
  compared pages to find no row had changed, three in four of the server's and seven in eight of a client's. A
  page copied and not changed also stayed in each of a client's ring of worlds until the ring came round; the
  copies' bytes above are what a frame no longer adds to them. What the server still copies (20 pages a frame,
  211 KB) is pages that did change: the grids' and lists' heap pages, the entity table's, a spawned row's lanes.
  `bench_gallery` (its `Stroke` handler's C changed) and `bench_sand` (no C changed) are as they were, three
  and five runs each the same way: gallery's server 2.23 to 2.22 ms a second in all and a client 3.34 to 3.36,
  sand's 2.16 to 2.16 and 14.31 to 14.36. `versus_events`, which is nothing but handlers (200,000 events a tick
  to 50,000 entities, a field written each), 9.83 to 9.62 ms a tick on one thread, and 9.93 to 9.63 with each
  tick's world kept beside it.

### A whole view arriving (2026-11-16, last)

Items 4 and 6 of What's left, taken apart and mostly closed. The yardstick: one player who stands and hops every 3 s
into a dimension whose chunks have gone (`--players 1 --still 100 --hop 3 --builders 0`; linger is 2 s), so every hop
is a whole box of 845 chunks nobody has; the bench says when the box was asked for on the server, when it was made
there, and when the player's machine had it, in ticks from the tick its own input hopped (`bench_desc.watch_server`,
bench/README.md). 50 ms each way and 10 of jitter, as every run here.

**Where the time went**, before (ticks from the hop; no loss, and 1% lost):

| | no loss | 1% lost |
|---|---|---|
| The server runs the hop's tick and asks for the box (the player's machine runs that far ahead of it) | 7 | 8 |
| ...has all of it made | 22 | 23 |
| The player's machine has all of it | 28 | 36 (44 at most) |

- **Making, 15 ticks**: the store answered 64 loads a tick, so 845 chunks were made over 14.
- **Sending, 6 ticks and a round trip wasted**: the hop's own tick is the old dimension leaving (a repair of 13.6 KB:
  845 identities), which the client asks a repair of; that repair was of that one tick, and the box, shown over the
  14 ticks after, all came in the second, which waits for the first to be acknowledged. The big repair went 16 ticks
  after the box was asked for, whenever it was made.
- **A lost chunk, 8 ticks at 1% and 17 at 5%**: the box is 116 chunks of a repair, at least one is lost 7 times in
  10, and a lost one is asked for and sent again a round trip and a half later.
- **The server's longest update was never the tick a box is spawned in.** 845 chunks spawned, each with a group and
  a load asked, are 1.9 ms of handlers and 0.7 of hashing. It was `Generate`: a tick's 64 new chunks made on one
  thread, 9.0 to 9.6 ms where they have ground in them (4,096 cells of caves' noise each, 150 µs a chunk), the same
  on 1, 4 and 12 threads (`--worst`: the same ticks of the same run). A per-entity system's runs are ranges of the
  entities in identity's order, and chunks spawned together are next to each other in it, so every new chunk is one
  task's: `Generate` goes "across threads" and one of them does the work (notes/schedule-and-threads.md, top
  section). That is also why the ring-a-tick box (Tried and taken out) changed nothing: it spread the spawning,
  which wasn't the cost.
- **The hosting player's long frame is the store's files read on the tick's thread, which only a run with no
  window does** (it has no clock, so its answers can't wait for a disk: section 3a). Of 9.1 ms at 405 chunks, 7.3
  are 405 files found missing (18 µs each); of 54.7 ms at 3,125, 42.9. With the files on their own thread, as a
  window's run has them, the same ticks are 2.5 and 12.2 ms (`bench_voxels --files` against `--files-here`, one
  player hopping at `--near 4` and `12`): the tick the box is spawned in, 2.8 µs a chunk and its hashing. So a
  player hosting in a window never had the 9 to 60 ms; what's left of it is the spawning at wide views.

**What changed.**

- **The game asks for a box nearest first** (`Watch`): a square ring around the player's chunk at a time, in each
  the player's own height first and then one up, one down, two up, two down, and around the ring with each chunk
  beside the one before it. The store answers in the order it's asked and chunks are made in it, so what a player
  stands in and looks at is there before the edge of the view. (Row by row, the player's own chunk was in the
  middle of the order: made 7 ticks in.) Around the ring, not across it: entities go in a repair in the order they
  were spawned, and chunks side by side are alike, which the repair's codec takes; ring cells in rows (the ring's
  two sides alternating) made a box 9% bigger as sent, around the ring 3.7% (35.8 KB a second against 34.5 for the
  hopping player; the default run's bots are sent less than before in all, 22.3 KB a second against 23.1).
- **The game paces what it makes, by what a chunk costs** (`Pace`, a system on each dimension's entity;
  `Streamer.making`, `Seen.dear`, `ready`, `go`, `Worlds.make`). `Watch` puts each chunk it asks for on its
  dimension's line and says whether it has ground in it (its middle column's ground within a chunk of its floor:
  the cells that cost). `Fill` marks one the store had nothing for `ready`; `Pace` lets the line's ready chunks be
  made in order (`go`) up to `Worlds.make` a tick, a chunk of air or water counting 1 and one with ground 10
  (`DEAR`); `Generate` makes those. 480 by default: 48 chunks with ground a tick, about 7 ms of one thread, where
  the store's 64 answers a tick let through up to 64 of them (9.6 ms). A chunk still asked for, or whose load
  failed, keeps its place. Each dimension has its own allowance.
- **The store answers 256 loads a tick** in a game with projections (`TIDE_STORED_PER_TICK`, notes/netcode.md, top
  section), so the pace is the game's and a box of mostly air is made in a few ticks.
- **A repair asked for of one tick goes as the world is now where entities have come to the player since**, so the
  hop's first repair carries the box (`repair_as_it_is_now`, `tide_game.client_came`; netcode.md).
- **Spare chunks** (netcode.md): a repair of 32 chunks or more to a player who loses datagrams is followed, an
  update's worth at a time, by a few chunks that are others XORed together, and one lost chunk is made from the rest
  without being asked for.
- **A client says which chunks it has for 512 past the first it lacks** (64 before): what made a lost chunk of a
  long repair cost the chunks after it again.

**After** (the same run for 120 s, before and after from two builds side by side; the ticks are the same every run
of a build):

| The hopping player, ticks from its hop (median, most) | no loss | 1% lost | 5% lost |
|---|---|---|---|
| Default view: the box asked for / made / on its machine, before | 7 / 22 / 28 (28) | 8 / 23 / 36 (44) | 9 / 23 / 45 (71) |
| ...after | 7 / 15 / 20 (20) | 8 / 15 / 21 (29) | 9 / 16 / 29 (42) |
| Twice the view (`--near 12`, 3,125 chunks): on its machine, before | 63 (68) | 71 (92) | 88 (100) |
| ...after (made at 34) | 40 (40) | 47 (66) | 56 (73) |
| Its verified world behind the server's at most, ticks, default view | 25 to 18 | 34 to 31 | 61 to 47 |
| ...in 99 frames of 100 | 18 to 12 | 24 to 18 | 43 to 29 |
| Sent to it, KB a second, default view (a box every 3 s) | 39.5 to 40.0 | 41.7 to 42.8 | 46.2 to 48.5 |
| ...twice the view | 135.6 to 139.6 | 153.9 to 151.4 | 210.8 to 168.8 |
| Its machine's update, 99th percentile / longest, ms, default view | 0.38 / 2.5 to 0.66 / 3.7 | 0.42 / 2.8 to 0.70 / 4.2 | 0.43 / 4.0 to 0.78 / 4.1 |

The floor at this distance is about 12 ticks (7 until the server runs the hop, one to make the box, 4 to send it);
the 8 over it are the making.

The default run and its neighbours (8 bots, twice the view, 32 bots; three rounds, before and after in turn, medians
and the three runs' range; bytes and ticks are the same every run):

| | 8 bots | 8, `--near 12` | 32 bots |
|---|---|---|---|
| Server's longest update, ms | 11.95 (11.7–12.0) to 9.97 (9.3–11.0) | 20.0 (19.9–20.3) to 22.4 (21.9–23.0) | 14.9 (14.8–15.9) to 14.5 (14.4–15.1) |
| ...99th percentile | 4.03 (3.98–4.03) to 4.70 (4.68–4.88) | 11.3 (11.1–11.5) to 11.5 (11.3–11.5) | 8.41 (8.19–8.45) to 8.11 (8.10–8.31) |
| ...median | 0.28 to 0.26 | 0.83 to 0.68 | 1.55 to 1.52 |
| Server, in all, ms a second | 32.8 to 33.5 | 85.0 to 86.1 | 102.7 to 104.4 |
| Sent to each bot, KB a second | 23.1 to 22.3 | 72.6 to 57.2 | 25.9 to 25.3 |
| ...of it repairs' chunks, for what its machine got | 18.5 for 17.9 to 17.8 for 18.0 | 68.1 for 53.0 to 52.7 for 53.2 | 21.5 for 24.0 to 20.9 for 24.0 |
| After a hop, nine tenths of the box, ticks: median (most) | 27 (38) to 23 (37) | 40 (79) to 32 (57) | 27 (51) to 24 (37) |
| ...all of it | 31 (50) to 28 (45) | 47 (85) to 35 (57) | 27 (51) to 26 (37) |
| A bot's verified world behind at most / in 99 frames of 100 | 35 / 21 to 36 / 23 | 53 / 28 to 42 / 25 | 42 / 24 to 40 / 23 |
| A bot's longest update, ms | 5.5 (5.2–5.7) to 4.7 (4.5–5.4) | 15.0 (14.6–17.3) to 20.0 (19.2–21.2) | 6.2 to 6.3 (5.9–7.8) |

What it didn't buy, and what it cost:

- **The 99th percentile of the server's update at the default view is a sixth longer** (4.0 to 4.7 ms) while the
  longest is a sixth shorter: the same chunks are made in fewer, fuller ticks, each bounded by `make` where the old
  ones were bounded by 64 answers of whatever kind. `--make 320` gives 4.3 and 6.7 ms and takes two round trips for
  a whole box (the last fifth of it, the edge, comes with the second); 640 gives 4.65 and 10.4 (one run each, on
  the build before the spares). All three are one thread's `Generate`.
- **At twice the view the longest update is 2 ms longer** (20.0 to 22.4): it's a whole view's repair, 430 KB packed
  (8 ms) and squeezed (8 to 10 ms, `tide_recipient_delta_pack`, in "the rest") in one update, and now more often
  with a tick of making beside it. A bot's own longest update is longer too (15 to 20 ms: the whole box lands in one
  repair where it came in two). Neither is the spawning; both are what a repair of a whole view costs in one piece.
- **A 30 s run's hops gain less than the hopping player's** (31 to 28 ticks for all of a box): its hops are in a
  connection's first seconds, before its loss is counted, so their repairs have no spares (netcode.md, Left).
- **Around the ring costs 3.7% more bytes a box** than rows did (above). The steady state is as it was or less: a
  flying bot with no hops is sent 17.0 KB a second (12.4 of chunks), and spares go only with repairs of 32 chunks
  or more, which a border's row of 65 chunks (9) isn't.

`bench_voxels_arrives` pins it (Tests). Not done, with what each would take, is in What's left.

### The sweep again, on the last build (2026-11-16, last)

Every table of sections 1 to 5 measured again on the build this note ends with (the day's dev and A whole view
arriving), so the numbers are one build's; then what those sections didn't have. Release, Windows, the same 12-core
Ryzen 9 7900X, nothing else building (the machine's load read 1% before the set), the shell at BelowNormal, one run
at a time; `bench_voxels` with its defaults but what a row says (30 s once everyone's in, 50 ms each way, 10 of
jitter, 1% lost), three runs and their median unless it says otherwise, a range where the runs differ by more than a
few hundredths. Counts, bytes, ticks and lags are the same every run. "The process at most" is its peak working
set, the server's and every bot's machine's together (the bench says it now).

**1. View distance** (8 bots; `--near 6`, `12`, `18 --lag 1200`)

| | near 6 | near 12 | near 18 |
|---|---|---|---|
| Chunks a player has / the server has at the end | 845 / 6,490 | 3,125 / 16,941 | 6,845 / 32,827 |
| The server's world in memory | 20.1 MB | 54.6 MB | 102.7 MB |
| The process at most | 287 MB | 608 MB | 977 MB |
| Server update, median / p99 / longest, ms | 0.27 / 4.74 / 9.1 | 0.67 / 11.3 / 22.4 (21.3–23.5) | 1.58 / 24.9 / 43.4 (41.5–44.1) |
| Server, ms a second: simulate / hashing / repairs / snapshots / the rest | 18.9 / 5.7 / 2.9 / 0.9 / 5.4 | 49.1 / 14.6 / 8.7 / 2.2 / 11.0 | 93.3 / 29.3 / 17.4 / 4.9 / 20.7 |
| ...in all | 33.9 | 85.5 | 165.5 |
| A repair, each (33 a second) | 0.09 ms | 0.26 ms | 0.52 ms |
| A client's update, median / p99 / longest, ms | 0.03 / 0.94 / 5.2 | 0.08 / 2.52 / 20.5 | 0.13 / 4.35 / 38.1 |
| A client, simulating, ms a second | 4.0 | 9.7 | 15.6 |
| Sent to each player, KB a second | 22.3 | 57.2 | 116.0 |
| ...repairs' chunks / ticks / the rest | 17.8 / 2.8 / 1.7 | 52.7 / 2.8 / 1.8 | 111.6 / 2.6 / 1.8 |
| ...what its machine got of worlds (each chunk of them once) | 18.0 | 53.2 | 112.8 |
| Chunks that came to a player's machine a second | 131.6 | 387.2 | 770.8 |
| Bytes a chunk, as got | 137 | 137 | 146 |
| After a hop, nine tenths of the box there, ticks: median (most) | 23 (37) | 32 (57) | 44 (86) |
| ...all of it | 28 (45) | 35 (57), one never before the next hop | 45 (98) |
| A client's verified world behind the server's at most, ticks | 36 | 42 | 54 |
| ...on average / in 99 frames of 100 / in 999 of 1,000 | 9.6 / 23 / 29 | 9.7 / 25 / 31 | 10.2 / 32 / 43 |

Against section 1's table (that morning, before the repairs', the indexes', the handlers' and this section's
changes): the server's update in all is 73.5, 195 and 391 ms a second there and 33.9, 85.5 and 165.5 here; what a
player is sent 23.4, 72.9 and 159.7 KB a second there and 22.3, 57.2 and 116.0 here (the chunks sent twice are
gone: repairs' chunks are within 1% of what a machine got at every view, where they were 2, 28 and 40% over); a
client's verified world at most 41, 53 and 70 ticks behind there, 36, 42 and 54 here, so the widest view no longer
passes the 60 a client predicts ahead of.

**2. Players** (the default view; `--players 16`, `32`, and 64 with `maxPlayers = 64` in a build not kept, two runs)

| | 8 | 16 | 32 | 64 |
|---|---|---|---|---|
| Chunks the server has at the end | 6,490 | 7,717 | 12,669 | 17,598 |
| The server's world in memory | 20.1 MB | 26.2 MB | 36.5 MB | 53.7 MB |
| The process at most | 287 MB | 485 MB | 870 MB | 1,655 MB |
| Server update, median / p99 / longest, ms | 0.27 / 4.74 / 9.1 | 0.68 / 5.74 / 10.3 | 1.51 / 8.23 / 14.6 | 3.6 / 16.2 / 27.8 |
| Server, ms a second: simulate / hashing / repairs / snapshots / the rest | 18.9 / 5.7 / 2.9 / 0.9 / 5.4 | 30.4 / 9.9 / 7.0 / 1.7 / 10.0 | 46.0 / 17.1 / 17.5 / 3.1 / 20.8 | 76.0 / 33.9 / 48.0 / 7.8 / 47.7 |
| ...in all | 33.9 | 58.8 | 104.4 | 213.6 |
| Repairs a second, and each | 33, 0.09 ms | 64, 0.11 ms | 114, 0.15 ms | 201, 0.24 ms |
| A client's update, median / p99, ms | 0.03 / 0.94 | 0.04 / 1.12 | 0.05 / 1.37 | 0.07 / 1.88 |
| Sent to each player, KB a second | 22.3 | 23.0 | 25.3 | 27.9 |
| After a hop, nine tenths of the box, ticks: median (most) | 23 (37) | 26 (37) | 24 (37) | 24 (43) |
| A client's verified world behind at most, ticks | 36 | 38 | 40 | 51 |

**3. A small machine** (the default run; `--threads`; the process held to two cores by its affinity)

| | 12 threads | 4 | 2 | 1 | two cores, 12 threads | two cores, 2 threads | two cores, 1 thread |
|---|---|---|---|---|---|---|---|
| Server update, median / p99 / longest, ms | 0.27 / 4.74 / 9.1 | 0.26 / 4.63 / 9.2 | 0.29 / 4.65 / 9.1 | 0.28 / 4.60 / 9.5 | 0.28 / 4.66 / 9.3 | 0.29 / 4.62 / 9.1 | 0.27 / 4.71 / 9.3 |
| Server, simulating, ms a second | 18.9 | 18.8 | 19.8 | 19.9 | 19.0 | 19.8 | 19.7 |
| A client's update, median / p99, ms | 0.03 / 0.94 | 0.03 / 0.83 | 0.03 / 0.83 | 0.02 / 0.69 | 0.03 / 0.95 | 0.03 / 0.82 | 0.02 / 0.70 |
| A client, simulating, ms a second | 4.0 | 3.3 | 3.3 | 1.9 | 4.1 | 3.3 | 1.9 |

The same on every count: the tick is still one thread's work, and a server for this game wants one fast core. (No longer for the making of chunks: New chunks on every core, below.)

**4. A player's own frame** (`voxels_near4`, `8`, `12` at 1920 by 1080 with no window, its server in the same
process; standing 600 frames, and walking 1,100 with a hop at frame 500; `--profile`; three runs, their range)

| | near 4 (405 chunks) | near 8 (1,445) | near 12 (3,125) |
|---|---|---|---|
| Frame's CPU, standing: median / p95 / longest, ms | 0.29 / 1.1 to 1.3 / 9.9 to 10.9 | 0.59 to 0.62 / 3.4 to 3.5 / 29.0 to 29.7 | 1.07 to 1.09 / 6.5 to 6.9 / 55.2 to 56.1 |
| ...walking, with a hop | 0.31 / 1.8 / 9.6 to 10.9 | 0.67 to 0.68 / 5.0 to 5.1 / 28.9 to 30.9 | 1.37 to 1.39 / 8.9 to 9.0 / 55.2 to 55.8 |
| Of it, walking: the match, median / p95 / longest | 0.06 / 0.11 / 8.9 to 9.4 | 0.08 / 2.0 to 2.3 / 24.3 to 26.0 | 0.17 / 5.1 to 5.2 / 51.8 to 53.5 |
| ...the views | 0.22 / 1.1 / 6.1 to 7.1 | 0.53 / 2.8 to 2.9 / 9.1 | 1.1 / 4.1 / 21.2 to 21.3 |
| ...the renderer's CPU | 0.03 / 0.06 / 9.5 to 10.7 | 0.06 / 0.18 / 8.5 to 9.4 | 0.10 / 0.36 to 0.39 / 10.2 to 10.7 |
| Faces made on workers, standing: jobs, each | 821, 0.42 ms | 3,226, 0.38 ms | 7,154, 0.39 ms |
| The process at most, standing / walking | 250 / 384 MB | 424 / 731 MB | 713 / 1,231 MB |
| From the start: the 125 chunks around the player have faces / all chunks here / all have faces, frames | 7 / 6 / 11 to 12 | 7 / 13 / 30 | 7 / 24 / 149 |
| After the hop | 8 / 9 / 19 | 8 to 9 / 27 / 72 to 73 | 9 / 59 / 161 |

- The chunks around the player have faces 7 frames in at every view (9, 19 and 34 in section 4), and the whole box
  is there at 6, 13 and 24 (9, 25 and 51): nearest first, and 256 answers a tick.
- "All have faces" at near 12 is not the first box's: the player falls to the ground through two more levels of
  chunks (625 each, at frames 50 to 60 and 90 to 100) before the first box's faces are done, and the count reaches
  none only after those. The first box's chunks without faces go 1,896 at frame 24 (all here), 1,138 at 40, 433 at
  60 with 370 new ones among them: about 45 a frame, which is the meshing's pace in a run with no window (7,154 jobs
  over 175 frames, paced or not: such a run's workers finish with their frame), not the arrival's any more.
- The longest frame is the store's files on the tick's thread (A whole view arriving): a windowless run's own.
- Held to two cores (one run each, walking): near 4 0.33 / 1.9 ms median and p95 (its first frame took 2.4 s, the
  renderer starting on two cores), near 12 1.38 / 8.8 / 56.0.

**Paced at 60 Hz** (`--paced`, new: notes/standard-host.md; the same runs, each frame held to its sixtieth of a
second, so the runs take 10.3 and 18.7 s; one run standing and two walking; the captures are the same bytes with
and without it):

| | near 4 | near 8 | near 12 |
|---|---|---|---|
| Frames longer than 16.7 ms, standing (of 600) / walking with a hop (of 1,100) | 0 / 0 | 1 / 2 | 2 / 4 |
| The longest, ms | 15.2 / 13.4 | 28.4 / 29.3 | 55.2 / 55.4 |
| Frame's CPU, walking: median / p95, ms | 0.66 to 0.70 / 1.7 to 1.8 | 1.14 / 5.6 to 5.7 | 2.06 to 2.10 / 9.7 |
| From the start, in wall time: the chunks around the player have faces / all chunks here / all have faces | 0.12 / 0.10 / 0.18 s | 0.12 / 0.22 / 0.50 s | 0.12 / 0.40 / 2.5 s (above) |
| After the hop | 0.12 / 0.15 / 0.28 s | 0.12 / 0.45 / 1.25 s | 0.12 / 0.98 / 2.5 s |

Every frame over 16.7 ms is one a whole box is asked for in (the start, the hop, and at near 12 the levels the
player falls through): the store's files on the tick's thread, which a window's run doesn't have (2.5 and 12 ms
there, by the bench). So a player at 60 Hz, hosting or not, has no long frame at the default view of the version to
play; at three times its radius the hosting player has the tick a box of 3,125 is spawned in (12 ms: a frame's
worth with the frame's own 2 to 10).

**5. Bandwidth** (KB a second to each player; 60 s runs with 8 bots, 30 s with 32; flying is the default run,
standing `--still 100 --hop 0`; turning is `--look 100`: every bot's input another every tick. The 5% runs take
`--lag 240`: see below)

| | 8, flying | 8, standing | 32, flying | 32, standing |
|---|---|---|---|---|
| All it's sent, bots as they are: none lost / 1% / 5% | 20.5 / 21.2 / 22.2 | 4.7 / 5.3 / 6.2 | 24.8 / 25.3 / 26.1 | 5.1 / 5.6 / 6.3 |
| ...of it repairs' chunks | 16.5 / 16.9 / 17.7 | 1.4 / 1.4 / 1.4 | 20.5 / 20.9 / 21.5 | 1.9 / 1.9 / 2.0 |
| ...ticks | 2.3 / 2.5 / 2.7 | 3.3 / 3.9 / 4.7 | 2.8 / 3.0 / 2.9 | 3.2 / 3.6 / 4.2 |
| All it's sent, every bot turning: none lost / 1% / 5% | 28.8 / 29.7 / 29.6 | 15.6 / 20.8 / 25.2 | 39.3 / 38.8 / 37.5 | 21.3 / 21.5 / 22.3 |
| ...ticks | 9.7 / 10.2 / 9.3 | 14.2 / 19.3 / 23.7 | 14.8 / 13.9 / 12.0 | 19.4 / 19.5 / 20.2 |
| What it sends, as they are / turning (1% lost) | 6.4 / 8.9 | 4.7 / 7.3 | 6.4 / 9.0 | 4.7 / 7.3 |
| Its verified world behind at most, turning, ticks: none / 1% / 5% | 20 / 40 / 66 | 20 / 31 / 40 | 22 / 41 / 70 | 20 / 31 / 41 |

- A chunk is 137 bytes as got, 146 at three times the view, as before. Flying with no hops, 8 bots: 17.0 KB a
  second, 12.4 of it chunks.
- **Repairs' chunks against what a machine got**, 8 bots flying: at near 12, 52.0 for 53.3 with none lost, 52.7
  for 53.2 at 1%, 54.4 for 52.9 at 5% (it was 68.1 for 53.0 at 1%); at near 18, 109.6 for 112.4, 111.6 for 112.8 and
  127.2 for 112.3 (155.0 for 110.7 at 1%). What's over at 5% is the chunks lost and sent again and the spares.
- **At 5% lost a join can put a machine more than 60 ticks behind**, and the bench stops a run there unless `--lag`
  says more: the default run's did in the build before (bot 2, 2.25 s in), this build's with every bot turning did
  (bot 3, 2.75 s in), and which run does is the luck of what's lost. A joining player's world has no spares (its
  loss isn't counted yet) and its chunks are asked for again as before. Once in, the bots are 30 to 33 ticks behind
  in 99 frames of 100 at 5%, and 66 to 70 at most where they hop and turn.

**6. Players spread over the world** (new: `--spread 256` starts the bots 256 cells apart on a square grid, past a
default view's reach, so no two share a chunk; 32 bots two runs each)

| | 8 together | 8 spread | 8 spread, no hops | 32 together | 32 spread | 32 spread, no hops | 8 spread, `--near 12` (512 apart) | 64 spread (one run) |
|---|---|---|---|---|---|---|---|---|
| Chunks the server has at the end | 6,490 | 7,852 | 6,731 | 12,669 | 28,549 | 22,298 | 29,332 | 50,260 |
| The server's world in memory | 20.1 MB | 30.3 MB | 25.4 MB | 36.5 MB | 94.8 MB | 69.2 MB | 106.3 MB | 168.8 MB |
| The process at most | 287 MB | 380 MB | 306 MB | 870 MB | 1,546 MB | 1,095 MB | 870 MB | 3,342 MB |
| Server update, median / p99 / longest, ms | 0.27 / 4.74 / 9.1 | 0.64 / 5.79 / 10.3 | 0.40 / 4.54 / 8.3 | 1.51 / 8.23 / 14.6 | 6.24 / 17.6 / 24.3 | 4.21 / 11.6 / 16.6 | 2.60 / 15.6 / 22.4 | 18.5 / 55.0 / 131.9 |
| Server, ms a second: simulate / hashing / repairs | 18.9 / 5.7 / 2.9 | 37.9 / 12.1 / 3.8 | 26.2 / 9.3 / 2.7 | 46.0 / 17.1 / 17.5 | 202.1 / 69.1 / 34.9 | 129.6 / 48.1 / 25.2 | 124.1 / 41.2 / 13.0 | 497.0 / 179.3 / 110.2 |
| ...in all | 33.9 | 61.2 | 44.2 | 104.4 | 344.8 | 230.0 | 197.2 | 927.0 |
| Sent to each player, KB a second | 22.3 | 24.1 | 17.5 | 25.3 | 24.3 | 18.9 | 55.8 | 25.4 |
| A client's update, median / p99, ms | 0.03 / 0.94 | 0.03 / 1.00 | 0.03 / 0.89 | 0.05 / 1.37 | 0.05 / 1.44 | 0.04 / 1.27 | 0.08 / 2.42 | 0.06 / 2.42 |

- **The server's cost goes with the chunks it holds, and with the players a second time.** Bots that start together
  share most of their chunks for much of a 30 s run (the count at the end isn't the run's); spread, each holds its
  845 from the first tick. Per chunk held at the end, a second of the server's updates is 5.2 µs for 8 together,
  6.6 to 7.8 for 8 spread, 10.3 to 12.1 for 32 spread and 18.4 for 64: the repairs, each player's part of the checks
  and the session's own work for each player grow with the players as well.
- **32 players apart hold 60 Hz without hops (p99 11.6 ms) and pass the frame with them (17.6 ms)**; 64 apart don't
  (the average update is 15.5 ms of 16.7). What a player is sent and what its machine does are as with 8.

**7. Dimensions** (new: `--park 1` starts the nth bot in dimension n of `--dimensions`; 8 bots, no hops unless
it says; 32 parked two runs)

| | together, 2 dimensions (one used) | together, 64 dimensions (one used) | 4 in each of 2 | apart in one (`--spread 256`) | one in each of 8 | hopping every 5 s among 2 | ...among 8 | 32, one in each of 32 |
|---|---|---|---|---|---|---|---|---|
| Chunks the server has at the end | 4,103 | 4,103 | 5,600 | 6,731 | 7,839 | 6,921 | 6,921 | 31,213 |
| The server's world in memory | 12.74 MB | 12.77 MB | 17.4 MB | 25.4 MB | 29.3 MB | 24.9 MB | 27.2 MB | 97.7 MB |
| Server update, median / p99, ms | 0.17 / 3.19 | 0.17 / 3.04 | 0.24 / 4.20 | 0.39 / 4.52 | 0.59 / 4.07 | 0.35 / 5.64 | 0.38 / 6.38 | 7.52 / 17.2 |
| Server, in all, ms a second | 20.8 | 21.0 | 29.4 | 43.8 | 54.4 | 49.4 | 55.1 | 387.9 |
| ...for each chunk held at the end, µs | 5.1 | 5.1 | 5.2 | 6.5 | 6.9 | 7.1 | 8.0 | 12.4 |
| Sent to each player, KB a second | 17.0 | 17.1 | 16.7 | 17.5 | 16.2 | 37.5 | 39.1 | 17.9 |

- **A dimension nobody is in costs nothing**: 62 more of them are 0.03 MB of world and no time.
- **The cost goes with the chunks held, not with how many dimensions they're in**: 8 players a dimension each cost
  what 8 apart in one do for each chunk (6.9 against 6.5 µs; each dimension has its own far regions, 1,404 against
  the 319 that 8 apart in one share), and 32 a dimension each what 32 apart in one do (12.4 against 12.1; 5,473 far
  regions against 933).
- **Leaving a dimension frees its chunks after the linger, and the world keeps the memory.** One player hopping
  between two every 10 s (`--every 1`): the server holds 1,018 entities (845 chunks, 169 far regions), 2,032 for the
  2 s after a hop, and 1,018 again; its world in memory is 2.38 MB before the first hop, 4.51 while both are held,
  and 4.35, 5.23 and 5.32 MB after the first three hops with the same 1,018 entities: pages a table or a heap took
  are used again, not given back. A player's machine's world goes 1.53 to 3.00 MB at the first hop and stays.
  (Since that night a heap gives back the pages whose blocks are all free: notes/ecs.md, top section. The packed
  sizes here are as they were, as the world is the same; the pages in memory after a hop are a quarter fewer.)

**8. A long run** (new: `--out 1 --hop 0`, 8 bots flying straight out eight ways, digging and building as they go;
`--every 60`; 10 minutes, and 20 with `--watch 0`)

| Minute | 1 | 2 | 4 | 6 | 8 | 10 | 14 | 20 |
|---|---|---|---|---|---|---|---|---|
| The server's world in memory, MB | 21.7 | 22.1 | 24.0 | 24.0 | 24.0 | 24.0 | 24.0 | 24.0 |
| Entities it holds | 8,240 | 8,815 | 9,231 | 9,153 | 9,088 | 9,270 | 9,074 | 9,088 |
| Bot 0's world, MB | 2.07 | 2.15 | 2.15 | 2.15 | 2.19 | 2.19 | 2.19 | 2.23 |
| The process, MB | 316 | 384 | 421 (414 without the watching) | 431 (421) | 434 (424) | 446 (435) | (443) | (469) |
| Server updates in the minute before: average / longest, ms | 0.62 / 7.9 | 1.02 / 8.3 | 1.18 / 7.0 | 1.20 / 7.3 | 1.22 / 7.3 | 1.22 / 9.6 | (1.24 / 8.7) | (1.26 / 8.6) |

- **The worlds are level**: the server's from the fourth minute (24.0 MB, 9,000 to 9,300 entities), a player's
  machine's within a page or two (2.15 to 2.23 MB over 20 minutes). Updates are level from the third minute (1.2 ms
  on average; the longest in a minute 6.5 to 9.6 ms, each the tick all eight bots cross a chunk border in: 3 to 4 ms
  of handlers and 4 to 5.5 of hashing for eight rows of 65 chunks coming and eight going). Over the ten minutes:
  median 0.49 ms, p99 4.48, 67 ms a second, 12.9 KB a second to each bot.
- **The store grows with what's built**, as it should: 2,042 saved chunks and far regions in 142 KB after 10
  minutes, 4,108 in 287 KB after 20 (in memory in the bench, files on a server). 567,041 loads in 20 minutes, none
  found.
- **One player alone for two hours** (`--players 1`, 86 km out): the server's world 4.48 MB at 15 minutes, 4.62 at
  30, 4.64 at an hour, 4.79 from 75 minutes to the end; its updates 0.10 to 0.11 ms throughout.
- **The process grows where no world does: not located.** 3.4 MB a minute for the server and eight machines
  together over the 20 minutes (414 to 469 MB from minute 4), 0.25 to 0.28 MB a minute with one player over one and
  two hours. 0.03 MB a minute a machine of it is the bench's own samples (8 standing bots grow 0.24 MB a minute
  together, their worlds exactly level). The rest is about a hundred bytes for each chunk that came and went (472
  a second with eight bots), outside every world (their sizes above), not the
  store (the same with the platform's files in place of the bench's memory) and not the bench's watching (off). The
  projection checks' memos keep an entry for a container until its pages change, which a freed chunk's may never
  do: the first place to look. A day of 8 players flying would be 5 GB by this. (Located and fixed the same day,
  The process's growth, below: not the memos, which hold half a megabyte, but 64 bytes of a thread's scratch area
  for every chunk a player's machine was sent.)

**What holds 60 Hz today, plainly.** By the server's update at the 99th percentile against the 16.7 ms of a frame,
on this machine's one core (threads don't change it), 1% lost, 50 ms each way:

| Players | Default view (845 chunks a player, 96 cells each way) | Twice (3,125 chunks, 192 cells) | Three times (6,845, 288 cells) |
|---|---|---|---|
| 8, starting together | holds: 4.7 ms, longest 9 | holds: 11.3 ms, longest 22 | doesn't: 24.9 ms, longest 43 |
| 8, apart | holds: 5.8 ms, longest 10 | holds barely: 15.6 ms, longest 22 | (not run: more than the row above) |
| 16 | holds: 5.7 ms | | |
| 32, together | holds: 8.2 ms, longest 15 | | |
| 32, apart | holds without hops (11.6 ms); 17.6 with a hop each every 20 s | | |
| 64, together | at the frame: 16.2 ms, longest 28 | | |
| 64, apart | doesn't: 55 ms, and 15.5 ms on average | | |

- **Per player**, whatever the count: 17 KB a second flying (12.4 of chunks), 22 to 25 with a dimension hop every
  20 s, 5 standing; 29 to 39 flying and 16 to 22 standing where every player turns every tick; at twice the view 57,
  at three times 116. It sends 6.4 (8.9 turning). Its machine's ticks: 0.03 ms median and 0.9 at the 99th
  percentile at the default view, 0.13 and 4.4 at three times. A whole view is on its machine 20 ticks after it's
  asked for (a third of a second; 21 at 1% lost, 29 at 5%), 40 at twice the view.
- **What breaks first on the view**: the server's update. At three times the radius 8 players are 165 ms a second
  of one core on average and one update in a hundred is 25 ms: `Generate`'s chunks on one thread (93 of the 165 is
  simulating) and a whole view's repair packed and squeezed in one update (16 to 18 ms at twice the view). Not a
  client: its ticks are 4.4 ms at the 99th percentile, its verified world 54 ticks behind at most (70 before).
- **What breaks first on players**: the server's update again, by the chunks it holds: 32 players apart are 28,500
  chunks and 345 ms a second, of which 202 is simulating (every chunk looked at by `Idle`, `Keep`, `Retry` and the
  far regions' `Tend` each tick, and `Generate`), 69 the checks and 35 the repairs. Bandwidth doesn't: a player is
  sent what 8 are.
- **What breaks first on the network**: joining at 5% lost (a machine more than a second behind while its first
  world comes: section 5). Flying and hopping at 5% a machine is 30 ticks behind in 99 frames of 100.
- **What doesn't break**: dimensions (a cost only by the chunks in them), threads and cores (one core is the
  machine), a long run's worlds (level), a player's own frame at 60 Hz (no frame over 16.7 ms at the default view of
  the version to play; the long ones measured are a windowless run's).

### The process's growth, located (2026-11-16, after the sweep)

The long run of The sweep again, 8 (`bench_voxels --out 1 --hop 0 --watch 0 --threads 4 --every 60`: 8 bots flying
straight out, digging and building; release, Windows, the machine shared with another build, BelowNormal, one run
at a time). What grew was found by counting, not guessed: a tree with `bench/alloc_count.h` in front of every C
file (notes/benchmarks.md, What the process holds) keeps every block malloc gives by its caller, and the bench says
after each `--every` line who holds what.

**What it was: 64 bytes of a thread's scratch area for every chunk a player's machine was sent.** A grid read from
a world's bytes is made in the scratch area and taken into the heap, and nothing gave back the block it was made
in, on a thread nothing resets (notes/netcode.md, top section, has the code and the fix: one mark in the generated
reader). 472 chunks a second come to the eight machines: 1.81 MB a minute by that, and the scratch pieces counted
were 7.3, 11.5, 14.7, 18.9 and 22.0 MB at minutes 2 to 10 (1.84 MB a minute). In the bench it's one thread's (the
bots and the server are updated on the same one); in a game it's each player's own machine, 64 bytes a chunk that
comes into view, and a server's wherever it reads grids from a heard event or a shard's messages. Not the memos,
and nothing of the server's checks.

**Every structure, counted** (the fixed build, MB held at minutes 4, 8, 12, 16 and 20; a block is counted on the
side whose update was running when it was asked for, a page on whoever took it last, and a page several worlds
share once):

| | The server's updates | The eight machines' updates |
|---|---|---|
| Heaps' pages (the worlds a session keeps share them: the one it ticks, each tick's of its history, each player's base) | 189.3, 182.7, 210.5, 192.7, 192.2 | 22.7, 23.8, 25.4, 26.8, 24.2 |
| Entity slots' and their index's pages | 26.0, 23.2, 26.9, 24.0, 23.3 | 4.4, 4.6, 4.3, 4.3, 4.3 |
| Tables' pages | 15.6, 15.2, 18.4, 15.6, 15.0 | 9.8, 9.5, 9.7, 10.1, 8.6 |
| The projection checks' memo: its slots and its entries' items and sums | 0.33, 0.61, 0.57, 0.61, 0.64 | 0.83, 0.63, 0.56, 0.70, 0.70 |
| ...kept rows and pieces, the repair journal, the groups' entries | 5.04, 5.08, 5.10, 5.13, 5.14 | 4.10, 4.11, 4.11, 4.11, 4.12 |
| Loops' indexes (followed ones and the copies' own) | 1.59 at each | 12.5, 12.6, 11.7, 12.0, 11.9 |
| Entities' orders | 7.1, 7.1, 8.1, 7.7, 7.3 | 0.9, 0.9, 0.9, 0.9, 0.9 |
| Heaps' own tables | 4.09 at each | 0.83, 0.83, 0.83, 0.90, 0.96 |
| Command queues | 1.9, 2.1, 2.1, 2.1, 2.1 | 0.25, 0.09, 0.03, 0.02, 0.09 |
| The session's own (frames, connections, inputs) | 1.02 at each | 2.88 at each |
| Scratch pieces (all on the one thread) | 4.2 at each (11.5 and 18.9 at minutes 4 and 8 before the fix) | |
| The bench's own: its timings, and its store in memory | 0.07, 0.14, 0.27, 0.27, 0.53 and 0.13, 0.26, 0.43, 0.51, 0.79 | 0.55, 1.08, 2.13, 2.13, 4.22 |
| Everything malloc holds | 339.5, 331.7, 369.2, 347.7, 344.0 together | |

- **Level**: entity slots (a slot is taken again: the pages don't grow), the memo (half a megabyte a side, swept
  when it doubles: an entry of a container that's gone goes at the next sweep after its page is written again, and
  pages here are), loops' indexes, the heaps' tables, the queues, the session's own. The store's line holds a tick's
  answers at most (65 of them, 17 KB, or none, at each minute).
- **Up and down with no trend**: the heaps' pages, by 30 MB, with how many pages the kept worlds no longer share
  (more in a minute all eight bots cross chunk borders in). It is why a process's size steps up for a while
  after the worlds are level: what was once held stays the process's (the C library's, and the pages kept spare,
  which were 6 to 24 MB over a 24-minute run, of 8 MB a thread and 32 shared at most: `engine/src/page.c`).
- **Creeping, by its design**: kept rows and pieces have room for the most rows a table ever had and the most pages a
  piece ever read (0.10 MB in 16 minutes on the server, slower each minute: one piece more, 154, in the last ten); the bots' heaps'
  tables likewise (0.13 MB). The world itself is as big as the most it ever held (What's left, 8): 24.0 MB to 24.9
  over an hour, in two steps.
- **Growing for good, and the bench's**: a float for every update of every machine (4.75 MB by the twentieth
  minute, its room doubling as it fills), and its store in memory, which grows with what's built (70 bytes and a key for
  each saved chunk besides its bytes).

**The process, minute by minute** (an ordinary release build; working set, MB; the same run before and after, both
ending on hash `0x1d2026427b0aaadd` at tick 36,229):

| Minute | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | at most |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Before | 304.3 | 367.5 | 384.8 | 396.5 | 398.0 | 406.6 | 401.5 | 408.9 | 411.4 | 410.8 | 421 |
| After | 299.3 | 360.8 | 379.1 | 388.4 | 388.1 | 393.8 | 386.0 | 393.4 | 395.3 | 391.8 | 402 |

From the fourth minute, when the server's world is level: 2.4 MB a minute before, 0.6 after. Longer, after (one
run of 30 minutes and one of 60):

| Minute | 5 | 10 | 15 | 20 | 25 | 30 | 35 | 40 | 45 | 50 | 55 | 60 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 30 minutes | 386.1 | 389.3 | 401.6 | 410.7 | 404.0 | 410.5 | | | | | | |
| 60 minutes | 388.3 | 397.3 | 400.6 | 415.7 | 406.1 | 411.8 | 417.2 | 413.2 | 421.0 | 419.4 | 423.3 | 423.3 |
| ...the server's world | 23.96 | 23.99 | 24.02 | 24.02 | 24.05 | 24.02 | 24.02 | 24.11 | 24.11 | 24.12 | 24.85 | 24.86 |

- **To the twentieth minute it still rises, 25 MB in all** (it was 55 from the fourth to the twentieth yesterday:
  414 to 469): the bench's timings and store are 5 of it, the pages kept spare 7 (15 MB to 22, in a run of a build
  made to count them and not kept), and the rest is the
  process keeping what the heaps' pages reached at their most (above: what malloc holds is 30 MB more in one
  minute than in another, with no trend over the twenty).
- **From the twentieth to the sixtieth it's 7.6 MB, 0.19 MB a minute**, of which the bench's timings are 4.7 and
  its store about 1.0 (868 KB of saves for 431 at half an hour, and a key and an entry for each of 12,381); what's
  left, 1.9 MB in 40 minutes, is inside one minute's difference from the next (4 to 10 MB). A minute's readings
  are 404 to 417 over the 30-minute run's last ten, 413 to 423 over the hour's last twenty.
- So a server and its players' machines no longer grow with the chunks that came and went. What grows with time in
  an engine's own structures is the rooms of "the most there ever was" above: tenths of a megabyte an hour here.

**Not done.** The process's size by itself can't tell the C library keeping freed memory from something growing:
only the counted run can, and it was 20 minutes long. A real client (a window, its views) and a dedicated server
alone in its process weren't run for long; the counted table's two columns are the nearest to them. The web build
wasn't built for this change (notes/netcode.md, top section).
### New chunks on every core (2026-11-16, last)

Item 5 of What's left, closed for `Generate` (notes/schedule-and-threads.md, top section, has what was happening,
what was built, the tests and every table). `Generate` queues (`Survey` sends), so its runs were shares of the chunks
in the order they were spawned, and the chunks `Pace` lets through in a tick, spawned together, were the last run's:
one task of 24 made them all. A system that starts with a guard now has the rows the guard lets through shared out
where they're at most a quarter of its entities, in at most 16 runs. The game is as it was.

| 12 threads, before to after (30 s, 1% lost; the sweep's runs) | 8 bots | 8, `--near 12` | 8, `--near 18` | 32 bots | 32, `--spread 256` |
|---|---|---|---|---|---|
| `Generate`, its longest tick, ms | 7.08 to 1.48 | 9.32 to 1.85 | 10.8 to 2.58 | 8.52 to 1.68 | 13.3 to 2.85 |
| Server, simulating / in all, ms a second | 18.9 / 33.8 to 11.5 / 26.3 | 49.1 / 86.0 to 27.5 / 63.9 | 93.4 / 165.2 to 50.0 / 122.7 | 46.2 / 104.6 to 30.5 / 89.4 | 201.6 / 344.2 to 139.4 / 277.1 |
| Server update, 99th percentile / longest, ms | 4.62 / 9.15 to 3.12 / 6.05 | 11.45 / 22.4 to 9.64 / 19.3 | 25.3 / 41.0 to 24.7 / 41.2 | 8.07 / 14.8 to 6.39 / 14.3 | 17.8 / 29.7 to 15.2 / 32.7 |

On 4 threads the default run's `Generate` is 7.01 to 2.00 ms and its update's 99th percentile 4.62 to 3.22; on one
thread everything is as it was (34.2 to 34.3 ms a second in all at the default view, 84.6 to 85.0 at twice). Ticks,
bytes and how long a box takes are the same before and after: the same chunks are made in the same ticks.

- **A server for this game no longer wants one fast core for the making**: section 3's "the same on every count" was
  `Generate`. What's one thread's still is the handlers (a box spawned in one tick), the checks' part that isn't on
  the pool, and the repairs.
- **The longest update is no longer `Generate`'s** at any view: at the default view it's a repair (2 to 2.8 ms of
  packing and 2 to 4 of the rest), at twice the view a whole view's repair or the tick a box is spawned in (10 ms of
  handlers for 3,125 chunks, the hopping player's run), which is why the longest didn't move at three times the view
  or with 32 apart.
- **`make` could be several times what it is, on a server with cores**: `--make 1920` (192 chunks with ground a
  tick) makes the default run's same ticks 22.8 ms before, on 1 and 12 threads alike, and 4.5 after on 12; the
  hopping player's box at twice the view is made 22 ticks after the hop for 34 and on its machine after 39 for 47,
  with `Generate`'s longest tick 2.3 ms on 12 threads (6.9 on one, which is what a host on one thread would pay: the
  default stays 480, the owner's to choose).
- Pins moved (world hashes: new chunks' blocks are in other runs' heaps; each the same on 1, 4 and 12 threads and in
  `_checked` and `_counts`): `bench_voxels` `0x66c02ffc1011cc8f` to `0x3dc3bff0071473c0`, `_forgets`
  `0xc810e7b2b6568f95` to `0x2c97d3472753d585`, `_wide` `0x4ed657a4bc93d5c3` to `0xb5d63358d8e6f42a`, `_arrives`
  `0x7f07c9d38e49fd83` to `0xbfaff20756dbd116`.

### A wide view without a long update (2026-11-16, wide views)

Items 5a and 5d of What's left: the server's longest updates at two and three times the view, taken apart by timers
put into the generated C and the session for the runs (and taken out), then closed in the order they cost.
`notes/netcode.md`, top section, has the session's and the codec's side.

**Where the time was** (`--near 12`, 3,125 chunks a player; 12 threads):

- **A whole view's repair, 20 to 23 ms**: two or three players' in one update (bots that joined or hopped together),
  each `client_changes` 4.4 to 8.6 ms and `tide_recipient_delta_pack` 5.4 to 8.5. Of the first, 3.4 ms was a grid's
  cells written one call each (16 M cycles of 19); of the second, 2.1 to 3.5 ms was the XOR regions, packed only to
  lose to the spans every time. A join where the first player had made the view read and packed it twice (the tick
  asked for, then the world as it is now).
- **The tick a box is spawned in, 9.4 ms of handlers and 2.5 of hashing** (the hopping player): 13,600 cycles a
  chunk, of which `Storage.Load` 4,520, the spawn's command with its grid and policies 3,240, the group and the
  Visibility's policy 2,000, the map of chunks 1,130, applying the spawn 1,170, `Ground` 260. **The store's part
  was quadratic**: a load asked twice in a tick is asked once, and each ask compared itself with every request of
  the tick before it (1,560 on average of 3,125; 3,400 of 6,845).
- **The tick a box goes in, 9 to 12 ms of handlers and 3 to 4 of hashing**, a linger after its player left: 3,125
  chunks saved, forgotten and destroyed at 10,000 cycles each. It was in no list: the same length as the spawn's,
  two seconds after it.

**What changed.**

- **The store finds a load asked twice by what it asks** (`engine/src/storage.c`, `tide_storage.slots`,
  `asked_hash`, `index_requests`): an index of the tick's loads and listings beside the requests, made again where
  it isn't the requests' (a tick's were cleared or taken from another's). Requests, their order and their answers
  are what they were. `storage_asks_a_views_worth_once_each` in `tidec_e2e_storage`.
- **A grid's cells go a chunk at once in what a player is sent**, **mostly new bytes are packed as spans first**,
  **a repair of 4 MiB or more is read in one update and packed and sent in the next**, on the pool's threads
  between the two where it has them, **a view arriving is read as the world is now first**, and **an update starts
  repairs until it has read 4 MiB**: netcode.md. No byte on the wire is another; `PROTOCOL` stays 27.
- **The game asks for a box over the ticks it takes** (`Watch`, `Worlds.ask`, 1,024 new chunks a tick for a
  dimension; `Streamer.asking`, `had`, `asked`, `askedAt`). `Move` sends `Want` every tick, and a player whose box
  isn't all asked for goes on where the last tick stopped, nearest first as before. The store answers 256 a tick in
  the order asked, so the box is made in the ticks it was: asked for 10 ticks after the hop for 7 at twice the view
  and 13 for 7 at three times, made at 30 and 34 (nine tenths, all) and at 60 and 66 as before. Chunks that are
  there already cost none of the allowance: a player who comes where others are joins every chunk's audience in
  the first tick. A player who moves on before their box is all asked for has each chunk of the new one looked at
  (`had` says the box before was whole, and then its places are passed over by where they are, as they were).
- **A box left whole goes over the ticks it was asked for in** (`Watch`'s removal, `Streamer.emptying` and
  `emptied`): each `Worlds.ask` of a dimension's chunks that empty say they did a tick after the ones before, so
  `Idle` lets them go a tick apart, a linger later. A border's row of chunks (625 at twice the view) still goes
  together. `docs/game/endless-worlds.md` has both (Who has which chunks; Keeping what nobody is near).

**Tried and taken out here.**

- **Each chunk waiting a few ticks more by where it is** (`(at.x + at.z * 5 + at.y * 3) % n` in `Idle`, no count
  shared): a border's row then went over four ticks too, so every tick had chunks going, and the server's
  simulating at twice the view was 40 ms a second for 28 and its hashing 19 for 16 (a tick in which entities come or
  go costs more than one in which none do: not taken apart, What's left). With `Idle`'s guard split in two, its
  simulating was 14.6 for 11.7 at the default view: a system's rows are shared out by its first guard (New chunks
  on every core), and the guard has to be the whole of what lets a row through.
- **512 new chunks a tick**: the default view's 845 were asked for over two ticks, for nothing.
- **Every repair of 2 MiB or more put off an update, one big one read an update**: a box at twice the view arrives
  in three or four repairs, each a tick later (35 ticks to 40 at the median with 8 bots). At 4 MiB twice the view's
  repairs (3.2 to 3.9 MB) aren't put off.

**After** (`bench_voxels`, 30 s, 1% lost, release, Windows, the 12-core machine shared with two other agents'
builds, the shell at BelowNormal; two runs each, before and after in turn, both given; counts, bytes and ticks are
the same every run):

| 12 threads | 8 bots | 8, `--near 12` | 8, `--near 18` | 32, `--spread 256` |
|---|---|---|---|---|
| Server's longest update, ms | 6.5, 6.7 to 5.1, 6.2 | 19.7, 20.4 to 11.2, 12.1 | 43.6, 49.4 to 19.8, 43.3 | 24.8, 46.6 to 23.9, 24.3 |
| ...99th percentile | 3.34, 3.72 to 2.83, 2.88 | 10.1, 11.2 to 7.3, 8.0 | 24.6, 25.6 to 10.5, 14.5 | 17.8, 23.5 to 16.0, 17.0 |
| Server, simulating, ms a second | 12.8, 12.9 to 12.5, 14.2 | 29.3, 29.4 to 28.4, 29.9 | 50.9, 52.8 to 53.6, 61.8 | 147, 216 to 146, 150 |
| ...in all | 29.5, 29.7 to 27.8, 32.3 | 72.2, 72.6 to 63.5, 68.1 | 128, 139 to 119, 143 | 312, 421 to 286, 311 |
| Sent to each bot, KB a second | 22.3 to 22.3 | 57.2 to 57.1 | 116.0 to 116.0 | 24.3 to 24.3 |
| After a hop, nine tenths of the box, ticks: median (most) | 23 (37) to 23 (37) | 32 (57) to 31 (59) | 44 (86) to 49 (101) | 26 (50) to 28 (50) |
| ...all of it | 28 (45) to 25 (45) | 35 (57) to 43 (59) | 45 (98) to 52 (101) | 29 (51) to 30 (50) |
| A bot's verified world behind: at most / on average / in 99 frames of 100 | 36 / 9.6 / 23 to 41 / 9.3 / 23 | 42 / 9.7 / 25 to 42 / 9.9 / 25 | 54 / 10.2 / 32 to 59 / 10.5 / 33 | 41 / 10.2 / 23 to 42 / 10.2 / 23 |

| 1 thread | 8 bots | 8, `--near 12` | 8, `--near 18` | 32, `--spread 256` |
|---|---|---|---|---|
| Server's longest update, ms | 9.9, 10.0 to 9.1, 9.1 | 23.6, 25.1 to 13.6, 14.5 | 45.6, 49.8 to 23.5, 30.2 | 25.1, 25.4 to 24.5, 27.8 |
| ...99th percentile | 4.82, 4.85 to 4.39, 4.70 | 12.1, 12.1 to 9.3, 9.5 | 27.1, 27.3 to 14.6, 17.5 | 17.5, 18.1 to 17.5, 18.5 |
| Server, simulating, ms a second | 20.5, 20.6 to 20.7, 20.7 | 52.2, 56.9 to 52.2, 52.6 | 99, 106 to 105, 140 | 195, 202 to 197, 205 |
| ...in all | 36.7, 36.9 to 35.8, 36.0 | 93.3, 101.8 to 88.2, 88.9 | 182, 192 to 180, 238 | 340, 357 to 339, 358 |

(Bytes, ticks and lags are the 12 threads'. The second runs after at three times the view were disturbed: on 12
threads three packs that take 7 ms took 39 and a tick's systems 35, on one thread everything was a third longer;
they're given as they came.)

One player standing, hopping every 3 s into a dimension whose chunks have gone, for a minute (the yardstick of A
whole view arriving; one run each):

| The hopping player | `--near 12`, no loss | 1% lost | `--near 18`, no loss | 1% lost |
|---|---|---|---|---|
| Server's longest update, ms | 14.0 to 8.1 | 14.9 to 7.5 | 48.1 to 14.9 | 41.2 to 13.5 |
| ...99th percentile | 9.60 to 5.21 | 9.81 to 5.16 | 21.3 to 10.1 | 21.9 to 10.0 |
| The box asked for, ticks from the hop | 7 to 10 | 8 to 11 | 7 to 13 | 8 to 14 |
| ...made, nine tenths / all | 30 / 34 to 30 / 34 | 30 / 34 to 30 / 34 | 60 / 66 to 60 / 66 | 60 / 66 to 60 / 66 |
| ...on its machine, nine tenths: median (most) | 40 (40) to 40 (40) | 46 (52) to 41 (49) | 72 (72) to 72 (72) | 74 (87) to 72 (88) |
| ...all of it | 40 (40) to 40 (40) | 47 (56) to 47 (57) | 72 (72) to 72 (72) | 79 (87) to 78 (88) |
| Sent to it, KB a second | 140.5 to 140.9 | 151.1 to 153.4 | 310.5 to 310.6 | 335.1 to 339.3 |

What it bought, and what it didn't:

- **Three times the view holds a frame at the 99th percentile** (25 ms to 10.5 to 14.5 with 8 bots, 21 to 10 for
  one hopping), and its longest update is under half. One player's whole view at either distance is on its machine
  as soon as it was.
- **Eight bots hopping together have the last tenth of their boxes later**: all of a box 35 ticks to 43 at twice
  the view and 45 to 52 at three times, at the median (nine tenths 32 to 31 and 44 to 49). An update starts
  repairs until it has read 4 MiB of them, where it read and packed every one asked for, which is what the 20 and
  43 ms updates were. The line is `REPAIR_UPDATE_MOST` (netcode.md), the owner's to move.
- **32 players apart are as they were** at the longest (24 ms) and nearly at the 99th percentile: their long
  updates are handlers and hashing of ticks in which many borders are crossed at once (8 ms and 9), none of it a
  whole view. Not taken apart further.
- **The 1.3 to 1.5% more bytes for the hopping player at 1% lost** is spares and chunks sent again falling
  otherwise; the repairs' own bytes are the same.
- Pins moved (the game's: `Streamer`'s new fields, and a box asked for over ticks; each the same on 1, 4 and 12
  threads): `bench_voxels`, `_checked` and `_counts` `0x3dc3bff0071473c0` to `0xbcfc2dc35a87a503`; `_forgets`
  `0x2c97d3472753d585` to `0x2b9a11548cb985ff`; `_wide` `0xb5d63358d8e6f42a` to `0xb8ed0e286b0b17a8`; `_arrives`
  `0xbfaff20756dbd116` to `0x94dc963771b145ed` (21 ticks at the median, 29 at most, as before). New:
  `bench_voxels_views`, `0xf3129f527202eb20` (Tests).

### Tried and taken out

- **A box that grows a ring of chunks a tick** from the player's own chunk (`Watch` with a `reach` for each player), so no tick spawns a whole box and the nearest are asked for first. The server's longest update was no shorter (near 6: 14.6 to 20.6 ms; near 12: 26.4 to 32.7; near 18: 57.0 to 48.8), its p99 a little longer (5.8 to 6.3, 14.7 to 16.1), its hashing more (9.7 to 10.7, 24.4 to 31.0 ms a second: more ticks in which something becomes someone's), and a hop's box later (nine tenths in 34 ticks for 28, 54 for 40, 76 for 62). The spawning isn't what the longest update is made of on a dedicated server; it wasn't taken apart further. (Taken apart later the same day, A whole view arriving: the longest update was `Generate` making 64 chunks on one thread, which a ring a tick doesn't change; a box spawned in one tick is 2.7 ms at the default view. The box is asked for nearest first since, all in one tick.)
- **Ring cells row by row** (the first cut of nearest first: a ring's rows in order, so its two sides alternate): a box 9% bigger as sent, as a repair lists entities in the order they were spawned and its codec takes chunks that are alike side by side. Around the ring it's 3.7% (A whole view arriving).
- **Spares for every sixteenth chunk of a whole transfer, sent once it had all gone** (the first cut of the spare chunks): a transfer longer than a round trip has (twice the view and more) had its lost chunks asked for and sent again before its spares went. A block of 64 at a time now, with the update that sends it.
- **Spares from a connection's start** (its copies start at eight, which read as the worst loss): a quarter more bytes for every join's first world on a network that loses nothing. They start once the loss is counted.
- **A making allowance of 320 or 640** in place of 480: A whole view arriving has both.

### The sweep on the build with all of the above (2026-11-16, end of day)

`bench_voxels --players N [--near] [--spread] --seconds 30`, 1% lost, 12 threads, release, Windows, BelowNormal, one
run each, the machine shared (so a longest update of 60 ms and more on 12 threads is taken as the machine's: 32
bots on one thread have 12.5 at most). Hashes the same on 1 and 12 threads (8 and 32 bots).

| | Server, ms a second in all | Update, 99th percentile / longest, ms | KB a second for each bot | Whole box after a hop, ticks (median / most) |
|---|---|---|---|---|
| 8 | 26.9 (34.3 on one thread) | 2.7 / 4.6 | 22.3 | 25 / 45 |
| 16 | 61.7 | 5.1 / 100 | 22.9 | 26 / 49 |
| 32 | 108.2 (109.0 on one thread, 8.6 / 12.5) | 8.2 / 66 | 25.3 | 25 / 43 |
| 8, `--near 12` | 66.0 | 7.6 / 12.1 | 57.1 | 43 / 59 |
| 8, `--near 18` | 120.8 | 10.3 / 19.7 | 116.0 | 52 / 101 |
| 8, `--spread 256 --near 12` | 111.9 | 10.4 / 15.4 | 59.3 | 47 / 87 |
| 32, `--spread 256` | 336.2 | 21.6 / 202 | 24.3 | 30 / 50 |

Three times the view holds its ticks now; 32 apart is still at the edge (a 99th percentile past a tick's 16.7 ms:
5e and 5f below). 64 bots wasn't a run the bench made then (`maxPlayers` was 32; 64 since: The spread case taken apart).

### The spread case taken apart (2026-11-16, night)

What's left 5f and 9, and the sweep's failing row (32 players apart, p99 21.6 ms against a 16.7 ms tick). The bench's
`--worst` now also gives the server's systems over the run and its updates split in two, those after which it holds
as many entities as before and those in which some came or went, each with its parts (bench/README.md).

**Where the time was** (`--players 32 --spread 256 --worst 5`, 12 threads, the build before; ms an update, 731 updates
with as many entities after, 1,070 with some come or gone):

| | in all | simulate: systems / the rest | hash | snapshot | pack |
|---|---|---|---|---|---|
| As many entities after as before | 5.08 | 2.43 / 0.10 | 0.94 | 0.28 | 0.74 |
| Entities come or gone | 7.49 | 2.50 / 1.05 | 2.40 | 0.28 | 0.68 |

- **Most of the simulating was sorting entities** (`tide_entities_ordered_archetypes_slots`): any spawn, destroy or
  archetype change put every order aside, and each system's next asking scanned 28,500 slots and sorted the subset.
  That's in "systems" on nearly every tick (a tick with as many entities after had some come and as many go), which
  is why the systems' part was the same in both rows. Fixed: an order takes in what came and went (notes/ecs.md, top
  section).
- **`Idle` was the systems' biggest after `Generate`**: 24.8 ms a second on one thread, every one of 28,500 keys
  looked up and every row's cause hashed before its first test turned it away. Fixed in the compiler, with `Keep`,
  `Retry` and every system with a guard: notes/schedule-and-threads.md, top section. The game is as it was.
- **5f, what a tick with entities come or gone costs more, is the players' projection hashes (+1.5 ms) and the
  handlers and changes (+1 ms)**, not the tables' settling (`tide_table_settle` under 0.2% of a profile), nor the
  loops' indexes. The world's own hash is part of the projections' in a game that hides state (the bench's new split
  says 0.00 for it). Counted (`bench_voxels_counts`, the same run, 2,736 checks over 46 s, after the fixes): pieces
  checked 1,437 ms (of it a heap page's blocks hashed again for another block's change 416, slots looked at again
  where a slot's page changed 332), audiences worked out again 699, rows of pieces read 434, players' sums 252, rows
  read from the heap again 248. Every check looks at every piece (378 of 64 rows a call, 17,670 heap pages' kept
  hashes looked at), and a tick in which chunks come and go writes slots and heap blocks scattered over the world
  (free slots and blocks are taken again last freed first), so most pieces read something again. The handlers'
  part is the spawn path (5g) and `Drop`. Not changed here: the first is the projection checks' design (netcode.md,
  top section, Left: pages written since the last call, kept by the runtime, are the owner's to agree), the second
  another task's.

**Before and after** (`bench_voxels`, 30 s, 1% lost, release, Windows, the 12-core machine, BelowNormal, one run at a
time, two runs each alternating with the build before the order change; the two runs given where they differ; the
world hash, bytes and ticks the same in every pair):

| 12 threads | 8 bots | 32 together | 32, `--spread 256` | 8, `--near 12` |
|---|---|---|---|---|
| Server, ms a second: simulate | 12.6 to 8.4 | 34.1 to 17.6 | 146.1 to 60.8 | 31.4 to 19.9 |
| ...hash | 6.4 to 6.1 | 21.7 to 18.5 | 82.3 to 75.1 | 19.4 to 18.7 |
| ...pack | 2.5 to 2.5 | 16.8 to 15.1 | 35.3 to 34.2 | 7.9 to 7.9 |
| ...in all | 28.0 to 23.4 | 99.6 to 75.2 | 304.9 to 210.3 | 73.3 to 60.8 |
| Update, median, ms | 0.29 to 0.23 | 1.64 to 1.09 | 5.48 to 3.40 | 0.78 to 0.63 |
| ...99th percentile | 2.76, 2.75 to 2.76, 2.76 | 6.23, 6.55 to 6.45, 5.08 | 15.95, 18.80 to 13.21, 13.45 | 8.48, 8.21 to 7.94, 8.07 |
| ...longest | 4.5, 4.8 to 4.7, 9.2 | 9.4, 8.9 to 10.8, 8.3 | 24.7, 29.8 to 25.7, 24.2 | 11.9, 13.5 to 12.0, 13.3 |
| Sent to each bot, KB a second | 22.3 | 25.3 | 24.3 | 57.1 |
| After a hop, the box: nine tenths / all, ticks (median) | 23 / 25 | 25 / 25 | 28 / 30 | 31 / 43 |

| 1 thread | 8 bots | 32 together | 32, `--spread 256` | 8, `--near 12` |
|---|---|---|---|---|
| Server, simulate, ms a second | 20.8 to 14.5 | 56.8 to 33.7 | 226 to 113, 142 | 53.4 to 36.0 |
| ...in all | 35.8 to 29.5 | 124.2 to 102.6 | 400 to 297, 348 | 90.5 to 71.0 |
| Update, 99th percentile, ms | 4.46, 4.49 to 4.25, 4.26 | 9.25, 10.02 to 8.55, 8.18 | 21.8, 19.9 to 19.8, 25.2 | 9.66, 8.98 to 8.95, 8.35 |

(The second "after" run with 32 apart on one thread was disturbed: its hashing and snapshots were a fifth over the
first's, which nothing here touches.) After, the spread run's updates are 3.18 ms with as many entities after
(simulating 0.75) and 5.47 with some come or gone (simulating 1.69, of which 1.01 the handlers and changes; hashing
2.27).

- **32 players apart hold 60 Hz on a server with cores**: p99 13.2 to 13.5 ms where it was 16 to 19, the server's
  time two thirds of what it was. On one thread they still don't (p99 about 20 ms): the projections' checks and the
  repairs are one thread's (What's left, 2 and the Gaps' hashing).
- **Every run is cheaper**: the default run's server a sixth (simulating a third less), 32 together a quarter.
  Bytes, ticks, lags and how long a box takes are as they were; `bench_voxels_arrives` 21 ticks.

**64 players** (`maxPlayers = 64` now, so the bench takes them; an `Audience` is 8 bytes where it was 4, and every
pin moved: `bench_voxels`, `_checked` and `_counts` `0xbcfc2dc35a87a503` to `0x7c8770e96b602e97`; `_forgets`
`0x2b9a11548cb985ff` to `0xed5457653cb1cfac`; `_wide` `0xb8ed0e286b0b17a8` to `0xe41987d3a647df0e`; `_arrives`
`0x94dc963771b145ed` to `0xda849c267c6b8896`, 21 ticks at the median as before; `_views` `0xf3129f527202eb20` to
`0xe25d64c9a5a9a3e6`; each the same on 1, 4 and 12 threads). What the room costs a game of fewer players, 12 threads,
two runs each against the 32's build: 8 bots 24.1 to 25.1 ms a second in all, p99 2.70 to 2.93; 32 apart (one run)
208.6 to 221.4, p99 14.7 to 14.4. Bytes are as they were (22.3 and 24.3 KB a second); the default run's world is
another, so its hops land on other ticks (all of a box 25 to 27 at the median).

| 12 threads, `maxPlayers = 64` | 64 together | 64, `--spread 256` |
|---|---|---|
| Chunks at the end / the server's world in memory | 17,593 / 53.6 MB | 50,263 / 152.5 MB |
| Server, ms a second: simulate / hash / snapshot / pack / in all | 30.8 / 39.0 / 10.6 / 45.5 / 174.2 | 152 / 185 / 38.0 / 106 / 579 |
| Update, median / p99 / longest, ms | 2.78 / 14.0, 14.6 / 32, 25 | 9.8 / 45.7, 46.2 / 106, 122 |
| Sent to each bot, KB a second | 28.0 | 25.5 |
| After a hop, the box all there, ticks (median) | 25 | 29 |
| The process at most | 1.7 GB | 3.5 GB |

- **64 together hold 60 Hz** (p99 14 ms, against 16.2 in the sweep's build not kept); **64 apart don't** (p99 46 ms,
  9.9 at the median, 0.58 of a core on average). Apart, nearly every update has chunks come or go (1,503 of 1,801):
  15.9 ms each, of which hashing 5.5, the handlers and changes 2.8, packing 2.6, snapshots 1.0. What would close it
  is 5f's projection checks going from the pages that changed, the spawn path (5g), and the repairs on more threads.
- `watch_server` (the server's side of a hop) watches bots 0 to 31 alone, as its masks are 32 bits; the bots' own
  fill times are every bot's.

### What's left, most telling first

1. ~~Inputs of every player, in every unacknowledged frame (section 5): 13 to 44 KB a second a player with real hands on the mice.~~ Cut the same day (section 5, the last paragraph of that bullet): 19.2 KB a second with 8 turning and standing, 10.6 flying; 32 are as they were (20.8 standing), each tick once, and a second copy of theirs is the owner's to weigh (netcode.md, Open).
2. (Fixed, above: a repair walked the whole server's world for one player.) What a server's update is made of at 32 players now: simulating 77 ms a second, the rest 29, hashing 24, repairs 23.
3. ~~Loops' indexes made again whole as chunks come and go, and for every copy of a client's world (above): a fifth of a server's and a client's simulating.~~ Fixed the same day (What was fixed, the bullet on loops' indexes): a server's simulating 28.1 to 19.6 ms a second at the default view and 78.9 to 50.5 at twice, a client's 7.9 to 4.4 and 26.7 to 10.5; 0.3 to 1 ms a second of it was left, made by handlers taking every lane of their components for writing, which they no longer do (What was fixed, the last bullet: a server's simulating to 18.4 and 49.2, a client's to 3.9 and 9.7).
4. ~~A whole view at once: the store's 64 answers a tick and the repairs' stream gap put a client 41 to 70 ticks behind, past what it predicts at three times the view; and a lost part of a long repair sends the parts after it twice (section 5).~~ Mostly closed the same day (A whole view arriving): a box on a player's machine in 20 ticks for 28 (21 for 36 at 1% lost), a client 36 to 54 ticks behind at most, and repairs' chunks within 1% of what a machine gets. What's left of it is 5a to 5d.
5. ~~The tick is one thread's work (section 3), and **new chunks are made on one thread though `Generate` goes across them** (A whole view arriving; notes/schedule-and-threads.md, top section): a run of a per-entity system is a range of the entities in the order they were spawned, and chunks spawned together are one run's. It is the server's longest update at the default view (7 ms for 48 chunks with ground, `Pace`'s allowance; 9.6 for the store's 64 before) and most of its simulating at wide views (93 of 165 ms a second at three times the view). Runs chosen among the rows a system's guard lets through would spread them: tidec's and the runtime's, and every such system's heap layout and pinned hash with it. With it, `make` could be several times what it is, and a box made in a tick or two.~~ Closed the same day for `Generate` (New chunks on every core): its longest tick 7.1 to 1.5 ms on 12 threads at the default view, the server's simulating 18.9 to 11.5 ms a second. Left of it: `make` is still 480 (with 1920 a box at twice the view is made in 22 ticks for 34); a system with no heap and no queue whose guard lets through rows that came together is still one thread's (notes/schedule-and-threads.md, top section, Gaps: none of voxels' is costly); and 5a to 5d, which are now the longest updates.
   - 5a. ~~**A whole view's repair is packed and squeezed in one update**: 8 ms and 8 to 10 at twice the view (430 KB), the server's longest update there (22 ms) and at three times (43). In pieces over a few updates, or on another thread, it wouldn't be one update's.~~ Closed the same day (A wide view without a long update): 2.3 ms read and 3 packed at twice the view, and at three times read in one update (3.7 ms) and packed on the pool for the next. Left of it: the read is one thread's, and several players' views at once are started 4 MiB an update.
   - 5b. **One repair at a time, each waiting for the last one's acknowledgement** (netcode.md, top section, Left): at twice the view the box is made over 26 ticks and goes in the repairs that fit in them, a round trip each, the last landing 6 ticks after its chunks are made.
   - 5c. **Spares only once a connection's loss is counted** (netcode.md, Left): a join and a first hop have none.
   - 5d. ~~**The tick a box is spawned in is 2.8 µs a chunk**: 2.4 ms at the default view, 8.7 at twice, about 19 at three times, where it's a frame's worth by itself. A box asked for over a few ticks, nearest first, would bound it; the ring-a-tick variant (Tried and taken out) is that, and would pay now where it didn't.~~ Closed the same day (A wide view without a long update): 1,024 new chunks a tick, the store's part of each no longer going with how many were asked, and a box left going over as many ticks.
   - 5e. **The tick a player joins or leaves the audiences of a whole view of chunks that are there** (hopping to where others are, or away from them): 6,845 chunks' `Seen` and group changed in one handler, 11 ms of handlers and 7 of hashing at three times the view (A wide view without a long update: its longest update now, 20 ms). The allowance of `Watch` counts spawns alone.
   - 5f. **A tick in which entities come or go costs the server more than one in which none do**, whatever their number (seen where a border's row of chunks went over four ticks for one: simulating 28 to 40 ms a second at twice the view). Taken apart (The spread case taken apart): the orders were sorted again (fixed: 32 apart simulate 146 to 61 ms a second); what's left is the players' projection checks (+1.5 ms such a tick with 32 apart: slots and heap blocks written all over the world make most pieces read again) and the handlers (+1 ms: the spawn path, 5g, and `Drop`).
   - 5g. **A spawned chunk is 9,000 cycles** beside its load (the command with its grid and policies 3,240, its group and policy 2,000, the map 1,130, applying it 1,170): `tide_visibility_set_group` looks its group up three times, and a Visibility's one policy is a map made in the scratch area and copied.
6. ~~A hosting player's frame of 9 to 60 ms when a box is asked for (section 4), not taken apart.~~ Taken apart (A whole view arriving): it's the store's files found missing on the tick's thread, 18 µs each, which only a run with no window does; in a window the same ticks are 2.5 and 12 ms. What's left is 5d. ~~And that **every chunk a player comes near is a file looked for** (567,041 loads in 20 minutes of 8 bots flying, none found): a dimension that knew which of its chunks were ever saved (a set kept with its far regions, or a listing as it starts) would ask for those alone.~~ Closed the same night in the store itself, for every game (section 3f): it lists its files once and a load of a key never saved reads nothing (14,551 reads to 49 in the default run; a hosting run with no window's longest update 4.6 to 2.3 ms at `--near 4`, 29 to 13 at `--near 12`).
7. ~~The process grows about a hundred bytes for each chunk that came and went, in no world (The sweep again, 8): 3.4 MB a minute with 8 bots flying, not located; the projection checks' memos are the first place to look.~~ Located and fixed the same day (The process's growth): a grid read from bytes left the 64 bytes it was made in on its thread's scratch area, 1.84 MB a minute here. What's left is the bench's own (its timings and its store in memory) and what's in the next item.
8. ~~**The world's memory doesn't go back down** (The sweep again, 7): a dimension left frees its entities and keeps its pages, so a server's world is as big as the most it ever held at once.~~ Mostly the heaps', and partly closed the same night (notes/ecs.md, top section): a heap gives back the pages whose blocks are all free, with the world's bytes and hash as they were. Eight bots out and back (`--out 1 --back 40`): the server's heaps 1,214 pages at the most and 745 once they're back (1,214 before); one player hopping between two dimensions: 191 to 215 pages after a hop where it was 260 to 269. Tables already gave theirs back (12 chunks to 6); the entity table's slots stay (0.29 MB of 20). What's left of it: a page stays for one block that's out, as pages hold blocks of every size, so a world that shrank to a fifth keeps three fifths of its pages; packing them would change which block a heap hands out next, and every pinned hash. And the packed world (what `--every` prints) is as big as it was: it's the same world. Closed later the same night (notes/ecs.md, A heap's blocks in the lowest places with room): a heap's blocks go in the lowest places with room, a page for each size, a page goes with its last block and a packed heap carries no free blocks. Out and back, the server's world packed 20.3 MB at the end to 3.3 (17.6 at the most, where it was 21.1), its heaps' pages in memory 740 to 453 (7.4 MB for 2.75 of blocks out); hopping, 4.3 to 4.5 MB after each hop back to 2.1. What's left: each run's heap of each owner keeps a part-filled page of each size it holds, a chunk's grid block is 8 KiB for 4 KiB of cells, a block that stays keeps its page (125 pages with one out at the end), and the entity table's slots (0.29 MB).
9. ~~`Idle`, `Keep`, `Retry` and `Tend` look at every chunk and far region each tick (What costs a chunk that's only held): a part, not split here, of the 202 ms a second of simulating that 32 players apart cost the server.~~ Split and closed (The spread case taken apart): `Idle` was 24.8 ms a second on one thread, every key looked up and every row's cause hashed; with its guard's few rows' keys from the marks it's 2.9. `Keep` 1.5, `Tend` 1.0, `Retry` 0.5 (one thread, 32 apart): what's left of them is the marking, a pass over each table's columns.
10. Not measured: a second player's machine with its views (the bots have none), real disks under these sizes (section 3a has the store's files at the default), a real window (the paced run has none), more than 64 players.

## Tests

- 2026-11-16, night (The spread case taken apart, which lists the pins that moved with `maxPlayers = 64`):
  `entity_orders_take_in_what_came_and_went` (`tide_tests`, notes/ecs.md) and `tidec_e2e_guarded_rows`' `Marked` and
  `Plain` (notes/schedule-and-threads.md). Release, Windows: `bench_voxels` (all seven), `bench_gallery`,
  `bench_sand`, the `tidec_e2e_visibility_*`, `_guarded_rows`, `_row_grids`, `_row_heaps`, `_indexed*`, `_storage*`,
  `_kept_lanes`, `_row_lanes`, `_rows`, `_destroy_rows`, `_function_acts`, `_events_batch`, `_ordered_split`,
  `tidec_row_lanes_codegen`, `tidec_destroy_rows_codegen`; `tide_tests entity`, `order`, `table`, `snapshot`,
  `world` under ASan.
- The process's growth (2026-11-16, after the sweep): every session benchmark fails a run whose updates leave bytes in their thread's scratch area (`bench_voxels` and its variants, `bench_gallery`, `bench_sand`, `bench_earth*`), and `tidec_e2e_visibility_runs` (and `_checked`, `_memo_off`) checks the mark around a load and each repair; both fail with the fix out (`notes/netcode.md`, top section). After it, release on Windows, every pin where it was: `bench_voxels`, `_forgets`, `_wide`, `_arrives`, `_checked`, `_counts`, `voxels_walk`, `tide_tests`, the 26 `tidec_e2e_visibility_*`, `tidec_e2e_index_follow`, `earth_tests`, `bench_gallery`, `bench_sand`, `bench_earth` (and `_split`, `_diverge`): 42 tests.
- 2026-11-16, wide views (A wide view without a long update, which lists the pins that moved): `bench_voxels_views` (`--near 18 --lag 1200 --players 4 --seconds 4`, about a second): four players joining where the first has made 6,845 chunks, three repairs of 6.3 to 7.0 MB each read in one update and sent in the next; its world is pinned and is the same with `--threads 1` (the next update packs), 4 and 12 (the pool does) and in `bench_voxels_checked`. `storage_asks_a_views_worth_once_each` (`tidec_e2e_storage`): 3,000 loads asked twice over with saves between are 3,000 requests in the order asked; another entity, a task, a listing and a listing from another key are each another; a cleared tick finds nothing of the last, and one that took another's requests finds them.
- Pins moved 2026-11-16, last (New chunks on every core, which lists them): all four of `bench_voxels`' and so `_checked`'s and `_counts`'; `_arrives` is `0xbfaff20756dbd116` now.
- `bench_voxels_arrives` (2026-11-16, last; A whole view arriving): one player standing, hopping every three seconds for a minute at 1% lost; the run fails where the whole box takes more than 23 ticks to be on its machine at the median (`--fill`; it takes 21), and its world is pinned (`0x7f07c9d38e49fd83`, the same on 1 and 12 threads). It fails with the store's answers back at 64 a tick or unbounded, with no repair as the world is now, with `client_came` never saying, and with `Pace`'s allowance unbounded (by its pin). The session's tests of the spare chunks and the 512 chunks said, and `client_came`'s, are in notes/netcode.md, top section.
- Pins after the game's changes of that section (nearest first, `Pace` and its fields, `spread` and `park`): `bench_voxels`, `_checked` and `_counts` `0x4ebcc443d78e978b` to `0x66c02ffc1011cc8f`; `_forgets` `0xa199531280421472` to `0xc810e7b2b6568f95`; `_wide` `0xf5f8baa91878a156` to `0x4ed657a4bc93d5c3`; each the same on 1, 4 and 12 threads and with every projection hash worked out twice. `voxels_walk` passes; the audits of the far regions find nothing wrong.
- `tidec_e2e_handler_lanes`, `tidec_e2e_handler_lanes_net` and `tidec_handler_lanes_codegen` (2026-11-16, later still; `notes/ecs.md`, top section): a handler takes for writing only the lanes it changed. After it, release, Windows, all passing with every pinned hash where it was: `bench_voxels`, `_forgets`, `_wide`, `_checked` and `_counts`, `voxels_walk`, `bench_gallery`, `bench_sand`, `bench_earth` (and `_split`, `_diverge`), `earth_tests`, `tide_tests`, the `tidec_e2e_visibility_*`, `_previous*`, `_fields`, `_rows`, `_indexed_loops*`, `_index_follow`, events', commands', tasks', `world_set*`, `world_edit` and prediction fixtures, and the tests of every other game whose generated C changed (Box2D's and Box3D's courses and shards, towers, sumo, duel, the safety fixtures): 172 tests. The 30 s runs of 8 bots, 8 at `--near 12` and 32, on 12 threads and on one, end on the hashes of the build before.
- `tidec_e2e_index_follow` and `tide_tests index_followed_*` (2026-11-16, later; `notes/indexed-loops.md`, top two sections): loops' indexes that take in the rows that changed and copies of a world that keep its indexes, each indexed loop against a plain one beside it, with the build's own counts of how each index came to be right. After it: `bench_voxels`, `_forgets`, `_wide`, `_checked` and `_counts` with their pins where they were (quick, and 8 bots, 8 at `--near 12` and 32 for 30 s on 12 threads and on one, the same hashes as the build before), `voxels_walk`, `tidec_e2e_visibility_runs` (and `_checked`, `_memo_off`), `earth_tests`, `bench_earth` (and `_split`, `_diverge`), `bench_gallery`, `tidec_e2e_indexed_loops`, `tidec_e2e_previous` and their differentials: pass, release, Windows.
- `tidec_e2e_visibility_repairs` (and `_checked`, `_memo_off`, `_wide`; 2026-11-16): repairs from the rows the checks wrote down against repairs from every row, for worlds 1 to 36 ticks old, through 240 ticks of changes picked at random, and which way each went (`notes/netcode.md`, top section). `bench_voxels_checked` works every such repair out both ways too: 8 bots, 32, and 8 at `--near 12` for 30 s pass, the hashes and bytes where they were.
- `entity_orders_are_shared_with_copies_until_either_changes` (`tests/test_entity.c`, in `tide_tests`; At scale, What was fixed, 2026-11-16): a copied table has its source's orders, each side's changes its own. The two older order tests said a copy's order was another array; they say it's the same one now.
- `voxels_walk` and `bench_voxels`, `_forgets`, `_wide`, `_checked`, `_counts` after section 13 (2026-11-15): pass, the bench's hashes where they were (priorities are this machine's, and the bench has no views).
- `bench_voxels` (`--quick --expect`): 3 bots, 8 s, `linger` 5 (30 before 2026-11-15) and `keep` 90 ticks, `far` 1, so chunks are saved and loaded again (6 found), and far regions are made (21, 4 with columns), sent, saved (3) and loaded back (4, with what their chunks told them while they were asked for taken after: the run's three such regions were checked by a log, since taken out); the server's hash is the same on 1 thread, 4 and 12. `bench_voxels_checked`: the same with every projection hash worked out twice. Pin (2026-11-15, far regions and the shorter linger): `0x1f3c165e43a78b10` to `0x07f8d5a78a39ee59`; after review (the audit's fields and system, `Survey`, `Recheck`, `Publish`'s hiding, waiting parts one a quarter) `0x4ebcc443d78e978b`, on 1 thread, 4 and 12, checked or not. Bench flags of 2026-11-15: `builders`, `tower`, `far`, `pace`, `gap` (section 10), `audit` and `forget` (section 11); the report says how many far regions the server holds, how many have columns, how often they were sent, how many the store kept and gave back, and what the audit found. A quick run audits every 60 ticks and fails on anything wrong (section 11).
- `bench_voxels_forgets` (`--quick --forget 1`, pin `0xa199531280421472`): the store drops every chunk's save and keeps the far regions', as a server that stopped between them; fails without `Survey` or without `Recheck` (section 8). `bench_voxels_wide` (`--quick --near 9 --far 1`, pin `0xf5f8baa91878a156`): a player's box wider than their one far region each way; fails without `FarOf` (654 wrong). Both the same on 1, 4 and 12 threads, and checked.
- `voxels_walk` (label `gpu`, the `voxels` target, `bench/voxels/walk.txt`): two players with no window, the first hosting, walk apart for 2,700 frames, the second digging ahead; chunks go behind both, and the faces beside them are made again (`Leaving`). It failed where play.tide's own check logged that a machine kept a chunk's sides past it (section 9); since the mesher takes its neighbours' grids there are no sides to keep (section 12), and after review it fails where play.tide's new check logs a chunk's faces made from a neighbour's side that changed since, both settled ("changed since", section 12's correction). Its saves go in the build's `voxels_walk` folder, emptied first (`voxels_walk_clean`, a fixture). About 5 seconds.
- The play version (section 8 and 9), `voxels` with no window: `--frames 300 --capture` at the start draws the same picture as before with 184,970 triangles of chunks where it was 646,376; with far regions written into its store (`--data-dir`, a wall, a tower and a pit 150 cells off as `far/0/0_1.bin`'s JSON) the wall and tower stand past the chunks, where the seed alone has sea. `--players 2 --script`: the host jumps and builds under itself for 1,400 frames (a pillar of planks 43 high: `far/0/0_0.bin` is `[{"at":0,"top":"Plank","height":43}]`) while the second player, a projected client, walks backwards 158 cells; at frame 2,300 the client's capture shows the pillar as a far column, the host's avatar on it, where the old build showed only the seed's ground. Its walk ends under the sea, where the old build drew the water's chunk edges as walls and this one draws the sea floor.
- `tidec_e2e_visibility_pieces` (and `_checked`, `_memo_off`): rows kept 64 at a time (section 2), twenty-two changes each to one cell, card (its policies all it has in the heap), bag (a small list sharing its page) or rover (taken into another scene in its row) of a piece nothing else touches, or to a scene's root and its group, quiet and with pages changing all over; each tick's hashes against a copy's with nothing kept, and repairs from what's kept against the rows' (`notes/netcode.md`, top section).
- `tidec_e2e_visibility_blocks` (and `_checked`, `_memo_off`): rows kept by their blocks (section 2, later): a grid chunk, a small map and a big one changed where they are the tick after a neighbour's in the same pages, a grid gaining a chunk, a list let go and another made where it was with its bytes and then with others, five groups of eight destroyed and the directory packed, a plot destroyed and another made in its slot; each tick's hashes against a copy's with nothing kept, repairs from what's kept against the rows', each change shown against a twin without it (`notes/netcode.md`, top section).
- `tidec_e2e_visibility_trees` (and `_checked`, `_memo_off`; 2026-11-16): containers too big for one chunk of their list and rows looked at one by one (section 2, the last bullet): thirty-eight changes to big maps, sets and lists (an entry where it is, one gone and another added, two put again the other way round, growing and shrinking across chunks and across a chunk's worth, elements swapped between chunks, a last element gone that's a 0) and to rows of one piece (small values and a later row's list in one tick, a value back as it was, a sparse component given and taken, a row made, gone or snapped beside one that changes); each tick's hashes against a copy's with nothing kept and against the player's own world's, loaded from what it's sent, and repairs from what's kept against the rows' (`notes/netcode.md`, top section). `bench_voxels`, `_forgets`, `_wide`, `_checked` (quick, and 8 bots for 30 s) and `_counts` pass with their pins where they were.
- `tidec_e2e_visibility_runs` (and `_checked`, `_memo_off`): heaps per owner and runs in a game that hides state; one thread's world on threads, projections the same wherever values are, a shared group's audience by the entities' order, a player's world repaired by what changed into its own heaps (section 5).
- `tidec_e2e_storage` (`compiler/tests/e2e/storage.tide`, `storage_test.c`): a host with files kept in memory and a guest without, over a lossy loopback: saves (each a write of `.new` and a move), a load asked twice, a key that needs writing out, a Windows device's name (`con`), keys whose folders would clash with files (`a` and `a.bin/x`), a key the provider refuses to read (`failed`), a 5,000-byte value (by download), a delete, a load after it, a save and a load of one key in one tick; listings (2026-11-15) of a folder, asked twice in a tick, of a part's start (`lis`: `lista` too), of 1,100 keys in two pages (the handler asks the second after the first's last key; each answer a download), of a folder the host can't list (`failed`), beside stray files that must not list (a `.new`, another name, capitals, small hex digits, a folder with a `.`, a device's name as it is, a hashed value without its key, a key file without its value, a key file whose key isn't its hash) and two hashed keys, one deleted with its key file; the guest's world has what the host's has (counts and an order-sensitive hash of every page's keys against C's sorted list) and neither resyncs. The same with the requests carried out only every fifth frame (`deferred`, a `post` the test runs when it likes): the same worlds, batches waiting on each other. A server destroyed with three batches not carried out saves what they saved; a rewind with five batches being carried out drops their answers and takes the rewound ticks' once each. Without files every load finds nothing, every listing nothing, none fails. `tide_storage_path` for keys that would leave the folder, dots in folders, device names, and a key too long for a file's name; `tide_storage_key` gives 21 keys back from their paths and refuses 19 stray paths and a hashed one; listings straight through `tide_storage_serve` (prefixes in a folder, past one, ending inside a part, with `/`s that aren't folders', `con/`, `a.b/`, after a key or a string that isn't one, every key, none, 1,025 keys in pages of 1,024 and exactly 1,024 after the first, 1,100 folders in two pages with fewer folders listed than there are (those past the 1,025th held, and before `after`, left alone), a listing asked twice, a folder that can't be listed failing the pages that could have its keys and not the one before them, no provider, a provider that can't list); answers cut short at every byte, a key that isn't UTF-8, too many keys, no such kind; the line's order (a later batch done first waits for the one before), a forgotten batch's answers dropped and its save done, `finish` with work posted, `answers` false skipping loads and listings. With the platform (`STORAGE_PLATFORM`, linked where `TIDE_PLATFORM` is on): the platform's own files in `storage_files/` in the test's folder, listing a folder, every key and after a key past stray files, a delete of a hashed key, 20 batches on the provider's own thread answered in order, and `storage_measure_tick_end` (section 3a).
- `tidec_e2e_storage_await`, `tidec_e2e_storage_await_shards` and `tidec_e2e_storage_await_hidden` (section 3d): match tasks that await loads and listings, on a host and a guest, carried out at once and later, packed and rewound mid-wait, taken up again by another server, replayed from the journal, hot reloaded mid-wait (through the platform's migration), an ended task's answer, keys the store refuses, without files, across shards, from a shard checkpoint and from its journal, and in a game that hides state.
- `tidec_e2e_storage` (2026-11-15, after review): `storage_lists_every_key_from_a_tick_with_no_bytes` (a tick whose only request is `List("")` has no bytes: its prefix and `after` were `NULL + 0` and `memcmp(..., NULL, 0)`, which the Linux and macOS sanitizers stop on; read as `""` now, served and on the line), `storage_asks_nothing_for_a_task_but_loads_and_listings` (and `tide_storage_takes`' bounds), `storage_line_a_batch_that_cant_be_queued_goes_after_those_before_it`, `storage_finish_waits_for_its_own_batches_alone` (with the platform). `tide_platform_store_headless` (section 3a).
- `tidec_error_storage_*` (nine, and nine `tidec_error_storage_await_*` of section 3d): a listing in a view, in input sampling, with no arguments, a prefix, an `after` and an entity of the wrong types (the second's note says what it can be), one without an entity where a handler takes components; a save's bytes and a load's key of the wrong types. `lsp_storage_completes_hovers_and_names_listings` in `tidels_tests`.
- `entity_erase_leaves_a_table_that_packs_and_takes_the_key_again` (`tests/test_entity.c`, in `tide_tests`): a table an entity was erased from packs, unpacks, and installs the key again (the free list's null-key slots, `rebuild_index`).
- Grids as bytes: `tidec_e2e_grid_bytes` (45 checks in Tide and the bytes pinned by a hash every platform shares; the measurements above, with a field given the grid two ways), `tidec_safety_grid_bytes`, `tests/test_grid_bytes.c` (`grid_bytes_*` in `tide_tests`), seven `tidec_error_grid_bytes_*` and `lsp_grid_bytes_completes_hovers_and_checks`: `notes/language.md`, Grids as bytes.
- The repairs and hashes are under every visibility test there was: `tidec_e2e_visibility_*`, `tests/test_visibility_session.c` (in `tide_tests`), `earth_tests`, `bench_earth*`, `tidec_e2e_net_alone_seen`, `tidec_e2e_shards_visibility`.
- Validation of the first version: the whole native release suite (3,410 tests; 4 GPU tests fail on a Vulkan validation message about depth bias and 2 private prototype tests on an older prototypes checkout, none touching this work), then the 131 networking, visibility, shard and storage tests again after the last change. Sanitizer and web runs are recorded in `notes/netcode.md` with the session's changes.

## Gaps

What a game of this kind still works around or lacks, most telling first.

- **Hashing what changed** (rows kept 64 at a time, then by their blocks, then row by row with big containers as trees: section 2, 2026-11-15 and 16; 43 ms a second to 18.4, to 9.5, to 7.0, with 8 bots moving). Closed of it: pages changing for other entities' blocks, slots' pages for other entities' turnover, the directory of groups copied whole, each row's part of each player's sum added up every call, the rows of a piece read for one row's change, a big map encoded whole. What's left (`notes/netcode.md`, top section, Left): a piece looks at its whole union of heap pages each call (25 a piece here: two grid chunks to a page; 74 of a run's 172 ms of checks are the pieces'), where pages written since, kept by the runtime, would let a check go from the pages that changed to the pieces that read them: a runtime change, not made; a piece any row of which changed adds up every row's part of each player's sum again (16 ms a run).
- **A system that writes grids ran on one thread in a game with visibility**: closed 2026-11-14 (section 5), `Generate` goes across threads. What's left of it: `Idle` stays on one thread (`Visibility.DestroyGroup` and `Storage.Save` change the match's own heap); on a tick with nothing to make `Generate` still goes out in 24 pieces, which cost a client about 15 µs a tick on threads (each finds no rows since 2026-11-16, last, and a client's simulating on threads is a sixth less: New chunks on every core); a tick's new chunks were one task's until then; a generator past a few milliseconds a chunk would still want a match `worker` (reserved, not built).
- **Awaiting a load or a listing in a match task** was closed 2026-11-15 (section 3d), as were listing keys and reading and writing off the tick's thread (sections 3a, 3b). What's left of them:
  - No handle of a load: without `await` the call is the event's, so several loads awaited together are several tasks (the owner's to spell, spec Open). An answer walks the tables of tasks that await the store, one scan an answer; a game with tens of thousands of such tasks waiting would want an index by op.
  - In a game that hides state, players' frames don't carry a task's answer, and a match task's continuation is the server's alone there (netcode.md): a player's machine's own run of a task that awaits the store never gets its answer, and what the server's changes reaches players through its state, as any hidden-state task's does (`tidec_e2e_storage_await_hidden`, 2026-11-15).
  - A listing walks every file of the folder its prefix names, each page: a page of a flat folder of 100,000 chunks reads 100,000 names, on the files' thread. Keys made of folders narrow it (`chunks/<dimension>/<x>/...`); the provider has no sorted or ranged listing to go on from `after`.
  - Every match of a process shares the files' one thread, so a server's slow disk holds up another's answers (none of their ticks). A server that stops waits for its own batches, and so for what was queued ahead of them, on the thread that destroys it (a dedicated server's update): a match that ends on a busy server holds the others' ticks that long. Batches have no bound: a disk slower than what the ticks ask piles them up in memory.
  - Where the provider can't queue a batch twice (no memory for 24 bytes, twice), it's carried out on the sender's thread after its own line's batches, but beside other servers' that queued since: two matches writing one key at that moment could write its `.new` at once.
  - A server that stops doesn't wait for the disk's flush; a page carries the tick's requests out on its own thread still.
  - On Linux and macOS a file in `saved/` whose name has a `\` reads to the native provider as a file in a folder (its `join` and `resolve` take `\` for `/`), so it could list as a key whose load finds nothing; none of Tide's has one.
- **A field given the grid bytes copies it once** (2026-11-15, section 4: it was twice, `saved.Copy()` copying in the scratch area first; 2.5 µs a chunk to 1.9). Reading straight into the field's heap would save the copy that's left (0.2–0.3 µs of it), at the price of a reader into a heap and heap layouts, and the hashes pinned on them, moving; not done.
- **A big answer waits for every player** in a game without visibility: past `TIDE_COMMAND_BYTES` it's a download, and the answers behind it wait until everyone has it.
- **Loads on their way are lost** when a server stops (a save of the match doesn't carry them), when a shard starts from a checkpoint without its journal, and when a match is rewound past where they were asked. A game whose chunk waits in `Asked` for an answer that never comes has to ask again itself. An awaited load or listing is asked again from its task's frame in each of these but the stop without a save (section 3d).
- **Managed hosting keeps nothing yet.** The engine's side is on dev (section 3e: the reactor's store calls, which a service that doesn't offer them leaves unused, so a hosted match keeps nothing, as before). The service's side is built and reviewed but not merged or deployed: branch `store` of `Tide-Engine/tide-services` (aedecef). The owner, 2026-11-15: a storage service makes sense, and is out of scope for now. Its open choices wait with it (a store per world or per project, how long it's kept past its match, its limits, saves past a plan's allowance). As that branch has it, the service keeps a hosted match's store, each world's apart. Every match of one native server (`tide_hosting`), and every shard of a world, still shares one folder.
- **Indexed loops** find a chunk by its place and dimension as one exact key, or by a `long` it's packed into (2026-11-14, notes/indexed-loops.md). There's no `==` on vectors (the spec keeps comparisons scalar), so `Same(a, b)` is a function. The server finds its chunks in a map instead (`Streamer.chunks`): a search of the tables doesn't see the tick's spawns.
- **Far regions** (closed 2026-11-15, section 8; what's left of it):
  - The server holds an entity for every region in a player's square, built in or not (443 with 8 bots): about 1 ms a second of simulating and 0.2 of hashing with nobody building. Spawning only regions the store has (a dimension's `Storage.List("far/<d>/")` as it starts, or an awaited load in a match task that spawns on finding) would leave the rest as nothing, at the price of a listing that walks the folder each page, or a task for each load.
  - A column is coarse: the highest block of 64 cell columns, so one plank raises an 8 by 8 square, and a dug-out column under a chunk the server hasn't is guessed (the seed's top, or the chunk's ceiling as stone; put right once that chunk is made, by `Survey` or `Fill`). What's built under an overhang doesn't show. Where the player's chunks are, the part of a column below their box is drawn as the whole 8 by 8 square, so a 1-wide pillar stands on an 8-wide plinth below the box.
  - A region saved after its chunks keeps what they lost until the chunk holding the column's top is made on the server again (`Recheck`, `Survey`): a lost tower top 200 cells up, which nobody's box reaches, stays. Saving a region only with its chunks (in the same tick, after them) would keep the store whole at a stop; not done, as `Keep` would save a region's chunks in one tick rather than each in its own.
  - Nothing in a bot run makes a region's last sent column go while one is added past the budget, so `Publish`'s showing rule has no test that fails without it (section 8, Paced).
  - Each sending is a correction for each player in range, a few hundred bytes whatever it carries: bounded at two a second (`gap` 30), not free. A region coming into a player's square comes whole, unpaced, as a chunk does.
  - The far ground is made again whole (13 ms on a worker) when a far region within its reach changes, at most one at a time; a region's own mesh would make it cost what changed.
  - Region loads share the store's 64 answers a tick with chunks, after them.
- **Faces across chunk edges** (closed 2026-11-15, section 9; what's left of it):
  - At the start a chunk's faces are made 1.4 times on average (792 for 567 chunks; 1.7, 978, with sides): a neighbour that comes after its faces were begun makes them again, once neighbours stop coming for 15 frames. Chunks that come in the same frame read each other's blocks (section 12). A first mesh that waited a few frames for its neighbours would make fewer again, and show later.
  - A block changed at a chunk's edge shows 3 or 4 frames after one inside a chunk would, as the chunk's new faces wait for the neighbour's (section 9).
  - `voxels_walk` needs a GPU (no CI runs it). Its check (section 12's correction) compares settled chunks' records, so a stale face while either chunk is still being made goes unseen until both settle, and a side's number is a sum that two sides could share (as `planes` always were). A chunk whose blocks a neighbour read in the first frame it was there, before its own view ran that frame, and that leaves before the next, isn't in `Leaving`'s reach (its `read` and `place` aren't written yet). Since the host hears `Destroyed` rather than `Hidden` for a dropped chunk (2026-11-15, section 9), it fails with the old `Hidden` handler.
  - Two things in the language made the sides lists rather than grids (an async function couldn't take a grid, and a view that handed its component to a task as `mut` was taken to change every field of it): closed 2026-11-15, section 12. A remesh copies each neighbour's grid three times (into the scratch area in `Near`, into the local state's heap as the task starts, and into the worker's snapshot): up to 4 KiB of cells each, which the measurements of section 12 take in. A view that started the worker itself with `Near`'s grids (a `LocalTask<Built>` it keeps, which a task awaits) would copy each twice.
- **Runs by identity cost one thread something** (schedule-and-threads.md): a per-entity system in runs whose rows queue nothing is about 8 to 15% slower on one thread than with the old shares of the order (cheap list rows 14.4 to 16.5 ms, `row_grids` 7.5 to 9.1), for four times faster on the pool. A match hosted on one thread pays it. The owner took keeping it (2026-11-15): runs by identity stay for every system in runs whose rows queue nothing, not only costly rows or grids, as shares give the pool's gain back (schedule-and-threads.md, Runs by identity, reviewed).
- Worker priorities (near chunks before far ones) are proposed in the spec, not built.
- `Transform` and physics are floats: a game this size keeps positions relative to something, as `float3 position` here would lose precision far out. `double3` exists for a game's own positions.
- Host migration is refused for games with visibility and for games that use the store, so a world a player hosts ends with them; what they saved stays on their machine.
- **No identity of a player that outlasts the server's run, for match code.** A player's things between visits want a key that's theirs: `PlayerID` comes back only through a cookie the same running server gave, and the account a trusted identity host supplies (`tide_server_desc.identity`, `tide_server_player_identity`) stays in C. A game can have each machine send an ID of its own (a file it keeps) as a command and key its saves by it, unverified, as offline game servers do. A verified one in match code (`Storage.Save(player, key, bytes)` kept under the player's account, say) is another problem, not this work's (owner, 2026-11-15).
