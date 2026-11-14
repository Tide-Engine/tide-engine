# Voxel worlds: what an endless world of blocks needs

Built 2026-11-14 on the owner's goal: "get all tools needed for a proper, efficient, bandwidth efficient, minecraft like game with infinite worlds, dimensions". The work started from a benchmark game written with the engine as it was, and closed what it measured. Everything language-visible here is Provisional (Claude's picks) until the owner confirms it: the spec says so where each is written (The server's store; State visibility).

## The game: `bench/voxels`

`voxels.tide` is the match, `main.c` its bots (`bench_voxels`, see `bench/bench.h`), `play.tide` what a person plays (`voxels`, a window; `--capture`/`--script` for runs with none).

- **A dimension is a scene** (`scene Dimension { number; seed; }`), loaded once each at the start. Its chunks are its scene's entities.
- **A chunk is an entity**: `Chunk { int3 at; dimension; seed; Grid3<Block> blocks = Grid3(16, 16, 16); stage; edits; }`, with a `Visibility` group of its own. A chunk as an entity is what the engine's rules already serve: visibility is per entity, shards go by position, `Destroy` unloads, and a `Loaded` event has an entity to go to. One open grid for a whole dimension has none of those (spec, Grids, Open).
- **Streaming is the server's alone.** The code that makes chunks and says who has them runs for components no player's machine has: `Streamer` on the dimension's own entity (who watches from where) and `Seen` on each chunk (who has it), both restricted to `Audience.None`. A handler or system that takes one never runs on a client, so clients neither run nor mispredict any of it. The first version kept a public `Map` of chunks in the dimension, and every client spawned and generated every player's chunks (2,102 entities held for 845 visible, 175 ms of simulation a second).
- **The box a player has** is `near` chunks sideways and `tall` up and down around the chunk they're in. `Move` sends `Want` to the dimension each tick; `Watch` does nothing unless the player's chunk changed, then removes the player from the chunks the box left and adds them to those it took in, spawning what isn't there.
- **Finding a chunk by its place** is an indexed loop over a query (`Find`, `At`, `Has`): `if (row.chunk.at.x != at.x || ...) continue;`. The index is by `at.x` alone (below, gaps).
- **A chunk's life**: spawned `Asked` with `Storage.Load(key, chunk)`; `Loaded` either unpacks what the store had or marks it `Missing`, which `Generate` fills from the seed (`Math.Perlin`); only then is the group's audience set, so a player never has a chunk that isn't made. `Idle` destroys a chunk `LINGER` ticks after its last player left, saving it first if a player changed it (`Pack`: runs of one block, a count and the block; 44 bytes a chunk on average in the runs below). `Idle` writes nothing while it waits (`emptied` is a tick, not a counter): a system that counts in every row makes every row's hash change every tick.
- **Digging and building**: `Put` goes to the world, `Place` finds the chunk on this machine and sends it `Change`. A client changes the chunks it has as the server does, so its own edits show with no wait.
- **On foot** (`play.tide`'s input): gravity, a jump, and a body that stops at blocks, by reading blocks through the same indexed query (`Blocked`, 12 lookups); the first solid block along the look direction is dug or built on. Bots fly through everything (`fly`).
- **What a machine draws** (`play.tide`): each chunk's faces as a `Mesh<Vertex3>` made by a `local worker` from the chunk's grid (a snapshot of it), kept in the chunk's `local` fields and drawn with `Draw.Mesh(mesh, transform)`; made again when `edits` changes. The task that awaits the worker (`Remesh(mut Chunk chunk, int version)`) writes `mesh`, `drawn`, `blank` and `meshing` itself (section 5). Past the chunks, the ground from the dimension's seed alone (`Ground3`, a square every 8 cells out to 768 cells, made on a worker around the player and again when they've gone a quarter of that): the same hills the server would make, with nothing sent for them. It's drawn 5 cells low so chunks cover it.

## What the engine was missing, and what was built

Measured with 4 bots flying across fresh terrain, `near` 6 and `tall` 2 (845 chunks each), 60 ticks a second, 50 ms each way, release, Windows, 12 threads. "Before" is the first version of the game on the engine as it was; "after" is the game as it is now on the engine as it is now, so the columns differ by both.

| | before | after |
|---|---|---|
| The run | a bot fell 61 ticks behind in 2.4 s and the run stopped | completes, bots at most 32 ticks behind |
| Server update, median / p99 | 9.34 / 21.8 ms | 1.39 / 6.6 ms |
| Server, hashing | 407 ms a second | 27 ms a second |
| Server, packing worlds | 55 ms a second (663 packs) | 0.02 ms a second (4 packs: the joins) |
| A client's update, p99 / max | 64 / 147 ms | 2.3 / 7.2 ms |
| A client, simulating | 175 ms a second | 14 ms a second |
| Sent to each bot | 118.7 KB a second | 20.9 KB a second |
| Entities a bot's machine held | 2,102 | 852 |

Standing still (nothing changing, 845 chunks, 4 bots): server hashing 140 ms a second before, 1.4 after; a client's 69 before, 1.1 after. With 8 bots moving (6,490 chunks loaded): server update 4.1 ms median, hashing 98 ms a second, 22.3 KB a second to each bot.

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

### 2. A tick's check that costs what changed (`client_state_codegen.inc`, `client_spans_codegen.inc`)

Every tick the server encoded and hashed every component of every entity, grids whole, for every world size; so did each client.

- **Containers in components hash as trees** (`tide_client_hv`), as singletons' did: a grid, list, map or set is its own hash, kept in the world's memo while every heap page it was read from hashes as it did. A client's worlds share one memo (keyed by its player, not the world's address: it hashes a different world of its ring each tick, and they share their pages).
- **An entity's hash is a sum**: what says which entity it is, plus a term for each component it has. What it hashes to for a recipient denied some components is the sum less their terms, with nothing read again (a chunk's `Seen` is denied to everyone, so every chunk was encoded twice a tick).
- **Rows are read where they are** (`tide_load_<Component>_at`, `tide_visibility_base_at` and `_denied_at` with one view of the group directory for the walk), not looked up by handle 8 to 10 times each.
- **Each row goes to the recipients who may have it**, from its audience's bits, not every row for every recipient.
- **What a table chunk's rows came to is kept** (`tide_client_kept`, with the memo): validated by the hashes of the chunk's own pages (a column each), of the heap pages its rows' values were read from, and of the entity table's pages its rows' slots are in (their snaps: `tide_entities_slot_page_hash`). Audiences are worked out again for the rows read again, and for every row when the group directory's pages changed or a scene's own row did.
- `TIDE_CLIENT_SPANS_CHECK` works every hash out both ways and stops where they differ (`bench_voxels_checked`); `TIDE_CLIENT_MEMO_OFF` keeps nothing. Simulation identity: `singleton and entity tree hashes`. The two pinned projection hashes moved (`visibility_projection_test.c`, `visibility_hash_memo_test.c`), memo on and off agreeing.

### 3. The server's store (`tide/storage.h`, `engine/src/storage.c`, `compiler/src/storage_check.inc`)

Nothing let match state reach a disk on a server: file calls are local code's, and a dedicated server runs none. Spec: The server's store.

- `Storage.Save`, `Load`, `Delete` compile to `tide_storage_ask(world, kind, key, bytes, target)`, which does nothing unless the world is bound to a `tide_storage` (`tide_storage_bind`): only a server binds, around its own run of a tick (`tick_run`). They're checked like the visibility directory's calls (`CALL_VISIBILITY` with `VISIBILITY_STORAGE`): match code and its helpers, and the code keeps its order (`calls_c`).
- After the tick (`tick_end`) the server carries the requests out against `tide_server_desc.io` (`tide_storage_serve`): saves and deletes at once, each load's answer a `TIDE_STORED_MESSAGE` (target, a number, found, key, bytes).
- Answers go in as shard messages do (`inject`, `dispatch_injected`, now `takes_messages`/`take_message` wherever the session asked `g->shards`): in the frame or by download for a game without visibility, in the journal, in a replay, 64 a tick. `tide_game.stored` (generated) sends `Loaded` with a cause of its own (the target and the answer's number).
- `Loaded` is Tide source the compiler adds to a program that says `Storage.Load` (`storage_prelude`, as `Web.Request`'s types are), so a game without it has no such event, and the language server knows it.
- **Hosts**: `tide_session_desc.io`, `tide_shards_desc.io`, `tide_hosting_desc.io` pass a `tide_io` down. The standard host gives the game's own files to a game whose table has `stored` (`tide_run_bind_io`, `tide_run_store`), and `tide_serve` does for a dedicated server. Files: `saved/<key>.bin` (`tide_storage_path`). The bench keeps them in memory (`bench_desc.io`).
- The 64 answers a tick is what paces a view's worth of new chunks: 845 chunks are asked for in one tick and made over 14.

### 4. Things found on the way

- `Storage.Load` beside `Loaded` handlers that take components follows the events' rule (every load names an entity), checked in `check_sends`.
- The first game's `Idle` counted in every chunk each tick; see above.

### 5. A local task writes its chunk's local fields (`check.c`, `codegen.c`; `notes/language.md`, Local tasks use local fields)

`Remesh` couldn't put a chunk's mesh in the chunk's own `local` fields ("a task can't use local fields yet"), so it left each in a local singleton's list (`Meshing.ready`, with the chunk's `Entity` and the frame it came in), which the chunk's view searched every frame and a `Sweep` view aged out when the chunk went first. Now a local task reads and writes the local fields of the components it takes, got again with the component each time it goes on, and `play.tide` is 35 lines shorter without the list, its struct and `Sweep`: the view starts `Remesh(chunk, chunk.edits + 1)`, the task awaits `Faces(chunk.blocks)` and sets `chunk.mesh`, `drawn`, `blank` and `meshing`. A chunk that leaves this machine while its faces are made takes its task with it, and the worker's result is dropped. The rules (gone, hidden, Forgotten, rollback, predicted and verified, hot reload, the match over) are in the spec's Local state, Local tasks and local fields (Provisional).

Checked: `tidec bench/voxels/voxels.tide bench/voxels/play.tide` and the `voxels` target build; `voxels --frames 300 --capture` draws 247 chunks with none waiting, and a script that digs four blocks in front of the player (`--script`, captures before and after) shows the hole in the remeshed chunk.

## Tests

- `bench_voxels` (`--quick --expect`): 3 bots, 8 s, chunks saved and loaded again; the server's hash is the same on 1 thread and 12. `bench_voxels_checked`: the same with every projection hash worked out twice.
- `tidec_e2e_storage` (`compiler/tests/e2e/storage.tide`, `storage_test.c`): a host with files kept in memory and a guest without, over a lossy loopback: saves, a load asked twice, a key that needs writing out, a 5,000-byte value (by download), a delete, a load after it; the guest's world has what the host's has and neither resyncs. Without files every load finds nothing. `tide_storage_path` for keys that would leave the folder.
- The repairs and hashes are under every visibility test there was: `tidec_e2e_visibility_*`, `tests/test_visibility_session.c` (in `tide_tests`), `earth_tests`, `bench_earth*`, `tidec_e2e_net_alone_seen`, `tidec_e2e_shards_visibility`.
- Validation of this work: the whole native release suite (3,410 tests; 4 GPU tests fail on a Vulkan validation message about depth bias and 2 private prototype tests on an older prototypes checkout, none touching this work), then the 131 networking, visibility, shard and storage tests again after the last change. Sanitizer and web runs are recorded in `notes/netcode.md` with the session's changes.

## Gaps

What a game of this kind still works around or lacks, most telling first.

- **Hashing still goes by rows where audiences change.** A player crossing a chunk border changes the group directory, and every row's audience is worked out again that tick (not its hash). With 8 bots moving the server spends 98 ms a second hashing 6,490 chunks. Next: find which groups changed (the directory's pages against a kept copy) and skip the table chunks whose rows' groups are none of them (group numbers are in order of making, so a chunk of rows covers a narrow range).
- **A system that writes grids runs on one thread in a game with visibility** (`tidec --schedule` says so of `Generate`: "the match hides state from players", one heap). Chunks are made one after another, 64 a tick at the most here. A generator that costs a millisecond a chunk needs either grids written across threads or a match `worker` (reserved, not built).
- **A load's answer is an event only.** No `await Storage.Load(key)`, no listing of keys, no `Pack`/`Unpack` of a value (the game writes loops over cells), and the files are read and written on the tick's thread between ticks.
- **Managed hosting keeps nothing**: `tide_managed_*` gives its servers no `io`. That's `Tide-Engine/tide-services`' to add. Every match of one server, and every shard of a world, shares one folder.
- **Indexed loops**: an equality of a `long` isn't a key (a chunk's place packed into one number can't be searched), and a guard on three components of a vector is indexed by the first alone. There's no `==` on vectors (the spec keeps comparisons scalar), so `Same(a, b)` is a function.
- **Far terrain is the seed's only.** What players built far away isn't in it. A coarse component of each far chunk, restricted to a wider audience than its blocks, is the engine's way to send it; nothing here does.
- **A chunk's faces at its edges are all drawn**: the worker gets one grid. Several grids as parameters (the six neighbours) work today; the game doesn't.
- Worker priorities (near chunks before far ones) are proposed in the spec, not built.
- `Transform` and physics are floats: a game this size keeps positions relative to something, as `float3 position` here would lose precision far out. `double3` exists for a game's own positions.
- Host migration is refused for games with visibility, so a world a player hosts ends with them unless it's saved.
