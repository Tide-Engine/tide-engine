# Shards: one world across servers

The language-level design, agreed with the owner on 2026-10-22, is `docs/spec.md`, Shards: what's Decided there is the owner's, what's Provisional the agent's picks. This file is how it's built, what code relies on, the tests and what's left.

## Status

Built (2026-10-23): the language surface, the generated shard code, the session's shard messages and the engine's orchestrator, with end-to-end tests of moving, ghosts, waiting on borders, forwarded events, checkpoints and splitting. Players moving between shards (clients following and switching their home shard), the hosting service's orchestrator across machines and the gaps below are not built yet.

## The language side

- `settings { shards = true; shardRange = 64; }` (`check_settings` in `compiler/src/check.c`): `shardRange` is required with `shards` and means nothing without it; both errors say why.
- `[Partition]` (`check_partition_attribute`): one `float2`, `float3`, `double2` or `double3` field of one match component; a second is an error naming the first, and one without `shards` is a warning. Without one, a root entity's `Transform.localPosition` decides (its world position: roots have no parent), and an entity with a parent goes with its root.
- `Shards.Owns(entity)` and `Shards.delay` (`compiler/src/builtins.c`): `tide_shard_owns_now` (the world the thread's tick or frame registered with `tide_shard_use`, true with none) and the thread-local `tide_shard_delay_now`. Any game can read them; in a game that isn't sharded they're true and 0.
- `ensure_shards` adds two hidden declarations: the sparse component `tide_ghost { int source; }`, which every match archetype can have (`derive_archetypes`), and the singleton `tide_shard_state`: `shard` (the id + 1, 0 for a world that's no shard), `role`, `sent` (the last number its messages had), `acked`, `next`/`end` (the entity index range it gives from), `singletons` (a bit for each of the game's singletons it owns), and lists of plain numbers and bytes: `regions` (7 doubles each: shard, box lo, box hi), `ranges`, `heard` (per sender, the last number applied), `pins` (borrowed entities kept until a tick), `asked`, `gone`, `held`, `outbox` and `waits`. They're match state like any, so snapshots, hashes, packing and the clients' worlds keep them.
- Every per-entity match system and handler is filtered `without tide_ghost` before archetypes are derived: a shard runs code for what it owns, and reads ghosts through queries and `World`.

## Generated code (`compiler/src/shard_codegen.inc`, `shard_messages_codegen.inc`, `shard_world_codegen.inc`)

- **Ownership.** `tide_shard_owned(w, e)`: alive here and no ghost, spawned this tick, destroyed here (`tide_entity_died_here`), or confirmed gone by its owner (`gone`). `tide_shard_may` is what `World.Set`, `Add`, `Remove`, `Destroy`, `Unload`, `Snap` and parenting call first: false, noting the running code's identity and what it touched (`tide_shard_note`), for anything else. A `mut` shared singleton is checked as a system or handler starts (`tide_shard_singleton_may`; a singleton is noted as the entity `{number, TIDE_ENTITY_TEMPORARY}`).
- **Identity.** A system sets `tide_shard_code` (1 + its index) and `tide_shard_this` as its body starts, and returns at once when the tick's skip set has it. Event deliveries do the same in the dispatcher (1 + systems + the event's number, and each code's occurrence count this run); a skipped delivery is parked (`tide_shard_park_<Event>`: its code, target, the tick it first tried, a command's sender, what it touched and the event's value) instead of run. `Destroyed` can't wait yet: its code is 0, so its changes across a border are dropped and counted.
- **The tick** (`gen_shard_tick`): `tide_world_tick_on` copies the world, runs the tick (`tide_world_tick_once`), and when anything was noted, takes the noted runs into the skip set, restores the copy and runs again, up to `TIDE_SHARD_RUNS` (8) runs; code 0 noting anything ends it at once. Both end with what was noted dropped and counted (`tide_shard_dropped`). Each run is deterministic, so a shard's clients do the same with nothing sent. The local event log is rewound with the world. After the last run, `tide_shard_after`: what waiting code needs is `held`, what it touched and doesn't own is asked for (`BORROW`, or `BORROW_SINGLETON` to the root), requests waiting to be lent are lent when they can be, borrowed singletons whose pins ran out go back to the root, and entities that left the region move (`tide_shard_handoff`).
- **Lending and priority.** A shard lends what it owns when asked, unless it's pinned (borrowed less than `TIDE_SHARD_PIN_TICKS`, 30 ticks, ago) or `held`, and the asker's number is higher than its own: then the request waits (`TIDE_SHARD_LEND` in `waits`). The lower shard always gets what it asks for, so code waiting on several shards' entities can't wait on each other forever; the first end-to-end run, before this rule, had two banks swapping places every 30 ticks for good.
- **Waiting deliveries** run first in `tide_apply_commands` (`tide_shard_resume`), each in full through its event's dispatcher, once this shard owns all it touched, with `Shards.delay` the ticks since it first tried.
- **Moving.** `tide_shard_handoff` sends each owned root whose position is in another region, a quarter of the range past its own, unless pinned: an `ARRIVE` with the tree's records, and the tree stays as ghosts of the shard it went to (the root removes it: it holds no copies).
- **Records.** An entity is its size, handle, archetype (the same build on every shard numbers them alike), snaps, scene, each component (sparse ones after whether they're there) through the value codecs recipient projections use, and its hierarchy links. `tide_shard_take_one` reads every value before changing anything, installs the handle (`tide_entity_install`), puts the row in its archetype or updates it in place, and sets the ghost tag; a ghost of something this shard owns is skipped.
- **Events** to an entity this shard doesn't own go to the shard that does (`tide_shard_forward_<Event>`, an `EVENT` routed by entity) and run there. Batched and split event dispatch is off in sharded games (`generating_shards`) so every delivery passes the check.
- **Indices.** Spawns take handles from the shard's ranges (`tide_shard_create`, through `tide_new_entity_by` and `tide_tick_systems.create` for systems that settle spawns on threads). An index is listed free only when the shard gave it (`tide_shard_free_slot`); an entity that left, or another shard's, is forgotten (`tide_entity_forget`, `TIDE_ARCHETYPE_AWAY` when it lives on), so no index is ever two entities. Indices of entities that moved aren't reused: the index space is spent by entities that cross borders (gap below).
- **Starting.** A region's world starts empty (`tide_shard_starting_empty` skips the start scene and Main's return), at the root's tick. `PlayerJoined`/`PlayerLeft` aren't sent by a shard's session: the root sends them when the orchestrator says (`PLAYER`). A region never ends the match.

## The session (`engine/src/session.c`, `PROTOCOL` 18)

- `tide_server_inject` queues a shard message for the server's coming ticks; `dispatch_injected` applies them before the tick (`tide_shard_game.receive`), after the tick's commands, and writes them in the frame (`FRAME_SHARD`): inline up to `TIDE_COMMAND_BYTES`, else published as a big command is and named by its download, the tick waiting for every player to have it. At most `TIDE_SHARD_MESSAGES_PER_TICK` (64) a tick, up to `TIDE_SHARD_MESSAGE_LIMIT` (64 MiB) each.
- Clients apply them from the frame at the same point. A frame with shard messages is never "as predicted" (`as_predicted`): before that, a region shard's clients asked for the world again every tick.

## The orchestrator (`engine/include/tide/shards.h`, `engine/src/shards.c`)

- One process, every shard a server; `tide_shards_update` updates them all with one clock, then carries each outbox (`carry`), acknowledging what it carried, and sends each region its neighbours' ghosts (`send_ghosts`: what changed since the last it sent, by each tree's bytes' hash, and drops for what it no longer holds near there; the root sends its unpositioned entities and its singletons to every region).
- Routing by entity goes through the directory (the shard each moved entity last went to) or the range its index is in. An `ARRIVE` updates the directory, and marks the tree as sent by its new owner to the shard it left, which holds it as a ghost.
- Ranges of 2^20 indices; a shard with less than half a range left gets another.
- Splitting: every 30 updates, a region whose shard owns more than `most_entities` is cut across its longest side at the median of its entities' positions (`sample`), keeping both halves at least twice the range across; the new shard starts at the root's tick and every shard is told the regions (`MAP`), after which what's in the new half moves there by position.
- Checkpoints (`tide_shards_save`/`load`): the ranges, the directory and every shard's packed world. Loading carries every outbox again from the start (receivers drop what they had by number), rebuilds what each shard was sent of ghosts from what it holds (`tide_shard_game.ghosts`), and tells every shard the regions again.

## Tests

`compiler/tests/e2e/shards.tide` and `shards_test.c`: twenty movers crossing the border of two regions, two banks paying each other through `World.Set` every four ticks (each change across the border waits until one shard owns both), and movers paying a bank with events forwarded across. Every step, no mover is owned by two shards and what the shards own never holds more coins than there are; at the end, at a moment nothing is in flight, every entity is owned once, all 200 event payments arrived once, every coin is accounted for and every transfer happened; no change was dropped; each shard's client was connected throughout with no resync. The same after a checkpoint restored mid-run, and with splitting on (a fourth and fifth shard appear).

## Gaps

- Players across shards: global PlayerIDs, clients following neighbouring shards and switching their home shard, and input reaching every shard that owns a player's entities. Next.
- `Destroyed` handlers' changes across a border are dropped (counted), as are those of task resumes and other code with no identity.
- Tasks owned by an entity don't move with it; Resident (physics) state doesn't either (spec, Open).
- Regions merge back never; index space isn't reclaimed from entities that moved.
- Unpositioned entities stay on the shard that spawned them rather than following their spawner.
- Visibility in sharded games (spec, Open); host migration doesn't apply (shards are servers).
- The hosting service's orchestrator across machines (`Tide-Engine/tide-services`).
